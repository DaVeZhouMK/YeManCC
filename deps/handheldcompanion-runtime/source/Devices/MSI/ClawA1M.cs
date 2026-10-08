using HandheldCompanion.Commands.Functions.HC;
using HandheldCompanion.Devices.MSI;
using HandheldCompanion.Extensions;
using HandheldCompanion.Inputs;
using HandheldCompanion.Managers;
using HandheldCompanion.Misc;
using HandheldCompanion.Shared;
using HandheldCompanion.Utils;
using HandheldCompanion.Views;
using HidLibrary;
using iNKORE.UI.WPF.Modern.Controls;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Management;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Windows.Media;
using WindowsInput.Events;
using static HandheldCompanion.IGCL.IGCLBackend;
using static HandheldCompanion.Utils.DeviceUtils;

namespace HandheldCompanion.Devices;

public class ClawA1M : IDevice
{
    protected enum WMIEventCode
    {
        LaunchMcxMainUI = 41, // 0x00000029
        LaunchMcxOSD = 88, // 0x00000058
    }

    protected readonly Dictionary<WMIEventCode, ButtonFlags> keyMapping = new()
    {
        { 0, ButtonFlags.None },
        { WMIEventCode.LaunchMcxMainUI, ButtonFlags.OEM1 },
        { WMIEventCode.LaunchMcxOSD, ButtonFlags.OEM2 },
    };

    protected enum GamepadMode
    {
        Offline,
        XInput,
        DirectInput,
        MSI,
        Desktop,
        BIOS,
        TESTING,
    }

    protected enum MKeysFunction
    {
        Macro,
        Combination,
    }

    public enum CommandType
    {
        EnterProfileConfig = 1,
        ExitProfileConfig = 2,
        WriteProfile = 3,
        ReadProfile = 4,
        ReadProfileAck = 5,
        Ack = 6,
        SwitchProfile = 7,
        WriteProfileToEEPRom = 8,
        SyncRGB = 9,
        ReadRGBStatusAck = 10, // 0x0000000A
        ReadCurrentProfile = 11, // 0x0000000B
        ReadCurrentProfileAck = 12, // 0x0000000C
        ReadRGBStatus = 13, // 0x0000000D
        SyncToROM = 34, // 0x00000022
        RestoreFromROM = 35, // 0x00000023
        SwitchMode = 36, // 0x00000024
        ReadGamepadMode = 38, // 0x00000026
        GamepadModeAck = 39, // 0x00000027
        ResetDevice = 40, // 0x00000028
        SetFeatureState = 44, // 0x0000002C
        DisableDevice = 45, // 0x0000002D
        SetMotionStatus = 47, // 0x0000002F
        MotionDataAck = 48, // 0x00000030
        RGBControl = 224, // 0x000000E0
        CalibrationControl = 253, // 0x000000FD
        CalibrationAck = 254, // 0x000000FE
    }

    public enum BatteryMode
    {
        BestForMobility,
        Balanced,
        BestForBattery,
        Custom,
    }

    public enum ShiftType
    {
        None = -1,
        SportMode = 0,
        ComfortMode = 1,
        GreenMode = 2,
        ECO = 3,
        User = 4,
    }

    public enum ShiftModeCalcType
    {
        Active,
        Deactive,
        ChangeToCurrentShiftType,
    }

    #region imports
    [DllImport("UEFIVaribleDll.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int GetUEFIVariableEx(string name, string guid, byte[] box);

    [DllImport("UEFIVaribleDll.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern bool SetUEFIVariableEx(string name, string guid, byte[] box, int len);
    #endregion

    private ManagementEventWatcher? specialKeyWatcher;

    // todo: find the right value, this is placeholder
    private const byte INPUT_HID_ID = 0x01;
    protected GamepadMode gamepadMode = GamepadMode.MSI;

    protected string WmiScope { get; set; } = "root\\WMI";
    protected string WmiPath { get; set; } = "MSI_ACPI.InstanceName='ACPI\\PNP0C14\\0_0'";

    protected const int PID_XINPUT = 0x1901;
    protected const int PID_DINPUT = 0x1902;
    protected const int PID_TESTING = 0x1903;

    protected string MsIDCVarData = "DD96BAAF-145E-4F56-B1CF-193256298E99";
    private const string WmiAcpiRegKey = @"SYSTEM\CurrentControlSet\Services\WmiAcpi";
    private const string WmiAcpiRegValue = "MofImagePath";

    protected int WmiMajorVersion;
    protected int WmiMinorVersion;

    protected bool isNew_EC => WmiMajorVersion > 1;

    private bool ClawOpen = false;
    public override bool IsOpen => DeviceOpen && ClawOpen;

    // FAN-936 ---------------------------------------------------------------
    // Serializes the MSI fan/EC channel so that a curve write and a release
    // routine (FanView disable, automatic hand-back, host exit, HC Close) can
    // never interleave on the wire.
    private readonly object msiFanGate = new();

    // FAN-936 R4: MSI fan-table wire contract. The Set_Fan request is always 32
    // bytes; the leading byte is the block index, leaving exactly 31 writable
    // payload bytes. The device answers with a leading status/flag byte that WMI
    // strips before the payload reaches us, so a good real-device read is
    // payload31, not payload32. The pre-write length check and the copy range
    // must share these definitions so they cannot drift apart again.
    private const int MsiFanTableRequestLength = 32;      // full Set_Fan package
    private const int MsiFanTableHeaderLength = 1;        // block index at [0]
    private const int MsiFanTableWritableDataLength = MsiFanTableRequestLength - MsiFanTableHeaderLength; // 31

    // Last structured results of the MSI fan channel. Plain properties so the
    // host can read them back through reflection.
    public MsiFanTableWriteResult? LastMsiFanTableWrite { get; private set; }
    public MsiFanBitWriteResult? LastMsiFanControlWrite { get; private set; }
    public MsiFanBitWriteResult? LastMsiFanFullSpeedWrite { get; private set; }
    public MsiFanReleaseResult? LastMsiFanRelease { get; private set; }

    // FAN-936 R4: opaque token the host sets before dispatching a release. Every
    // release result echoes it back so the host can prove a receipt belongs to
    // the current host instance / device session / control cycle / operation and
    // not to an earlier or foreign release.
    public string? MsiReleaseOperationToken { get; set; }

    // FAN-936 R1: monotonic count of every entry into the single release routine.
    // The host reads it by reflection immediately before and after the Close
    // hand-back boundary, so it can tell "the release routine was actually
    // entered" from "Close never reached the hand-back" without inferring
    // "not attempted" from an empty/stale result field. Same name as the mock's
    // counter so the host reads both through one code path.
    public int ReleaseMsiFanControlInvocationCount { get; private set; }

    private static readonly DeviceVersion[] deviceVersions =
    {
        // MS-1T41
        new DeviceVersion() { Firmware = 0x163, RGB = [0x01, 0xFA], M1DInput = [0x00, 0x7A], M2DInput = [0x01, 0x1F], M1XInput = [0x00, 0x7B], M2XInput = [0x01, 0x20] },
        new DeviceVersion() { Firmware = 0x166, RGB = [0x02, 0x4A], M1DInput = [0x00, 0xBA], M2DInput = [0x01, 0x63], M1XInput = [0x00, 0xBB], M2XInput = [0x01, 0x64] },
        new DeviceVersion() { Firmware = 0x167, RGB = [0x02, 0x4A], M1DInput = [0x00, 0xBA], M2DInput = [0x01, 0x63], M1XInput = [0x00, 0xBB], M2XInput = [0x01, 0x64] },

        // MS-1T42, MS-1T52
        new DeviceVersion() { Firmware = 0x211, RGB = [0x01, 0xFA], M1DInput = [0x00, 0x7A], M2DInput = [0x01, 0x1F], M1XInput = [0x00, 0x7B], M2XInput = [0x01, 0x20] },
        new DeviceVersion() { Firmware = 0x217, RGB = [0x02, 0x4A], M1DInput = [0x00, 0xBA], M2DInput = [0x01, 0x63], M1XInput = [0x00, 0xBB], M2XInput = [0x01, 0x64] },
        new DeviceVersion() { Firmware = 0x219, RGB = [0x02, 0x4A], M1DInput = [0x00, 0xBA], M2DInput = [0x01, 0x63], M1XInput = [0x00, 0xBB], M2XInput = [0x01, 0x64] },

        // MS-1T8K
        new DeviceVersion() { Firmware = 0x308, RGB = [0x02, 0x4A], M1DInput = [0x00, 0xBA], M2DInput = [0x01, 0x63], M1XInput = [0x00, 0xBB], M2XInput = [0x01, 0x64] },
        
        // MS-1T91
        new DeviceVersion() { Firmware = 0x411, RGB = [0x02, 0x4A], M1DInput = [0x00, 0xBA], M2DInput = [0x01, 0x63], M1XInput = [0x00, 0xBB], M2XInput = [0x01, 0x64] },
    };

    protected int Firmware;
    public DeviceVersion? FirmwareDevice => deviceVersions.MinBy(version => Math.Abs(version.Firmware - Firmware));
    public override bool IsSupported => FirmwareDevice?.Firmware == Firmware;

    public ClawA1M()
    {
        // device specific settings
        ProductIllustration = "device_msi_claw";

        // used to monitor OEM specific inputs
        vendorId = 0x0DB0;
        productIds = [PID_XINPUT, PID_DINPUT, PID_TESTING];
        hidFilters = new()
        {
            { PID_XINPUT, new HidFilter(unchecked((short)0xFFA0), unchecked(0x0001)) },
            { PID_DINPUT, new HidFilter(unchecked((short)0xFFF0), unchecked(0x0040)) },
        };

        // https://www.intel.com/content/www/us/en/products/sku/236847/intel-core-ultra-7-processor-155h-24m-cache-up-to-4-80-ghz/specifications.html
        nTDP = new double[] { 30, 30, 35 };
        cTDP = new double[] { 20, 45 };
        GfxClock = new double[] { 100, 2250 };
        CpuClock = 4800;

        GyroMatrix = new()
        {
            Axis = new Vector3(1.0f, 1.0f, -1.0f),
            AxisSwap = new SortedDictionary<char, char>
            {
                { 'X', 'X' },
                { 'Y', 'Z' },
                { 'Z', 'Y' }
            }
        };

        AcceleroMatrix = new()
        {
            Axis = new Vector3(-1.0f, -1.0f, 1.0f),
            AxisSwap = new SortedDictionary<char, char>
            {
                { 'X', 'X' },
                { 'Y', 'Z' },
                { 'Z', 'Y' }
            }
        };

        // device specific capacities
        Capabilities |= DeviceCapabilities.FanControl;
        Capabilities |= DeviceCapabilities.DynamicLighting;

        Capabilities |= DeviceCapabilities.FanOverride;
        Capabilities |= DeviceCapabilities.OEMCPU;
        Capabilities |= DeviceCapabilities.BatteryChargeLimit;
        Capabilities |= DeviceCapabilities.BatteryChargeLimitPercent;

        // dynamic lighting capacities
        DynamicLightingCapabilities |= LEDLevel.SolidColor;
        DynamicLightingCapabilities |= LEDLevel.Ambilight;

        // battery bypass settings
        BatteryBypassMin = 60;
        BatteryBypassMax = 100;
        BatteryBypassStep = 20;

        DevicePowerProfiles.Add(new(Properties.Resources.PowerProfileMSIClawBetterBattery, Properties.Resources.PowerProfileMSIClawBetterBatteryDesc)
        {
            Default = true,
            DeviceDefault = true,
            OSPowerMode = OSPowerMode.BetterBattery,
            CPUBoostLevel = CPUBoostLevel.Disabled,
            Guid = BetterBatteryGuid,
            TDPOverrideEnabled = true,
            TDPOverrideValues = new[] { 20.0d, 20.0d, 20.0d },
            IntelEnduranceGamingEnabled = true,
            IntelEnduranceGamingPreset = (int)ctl_3d_endurance_gaming_mode_t.MAX // 30fps
        });

        DevicePowerProfiles.Add(new(Properties.Resources.PowerProfileMSIClawBetterPerformance, Properties.Resources.PowerProfileMSIClawBetterPerformanceDesc)
        {
            Default = true,
            DeviceDefault = true,
            OSPowerMode = OSPowerMode.BetterPerformance,
            Guid = BetterPerformanceGuid,
            TDPOverrideEnabled = true,
            TDPOverrideValues = new[] { 30.0d, 30.0d, 30.0d },
            IntelEnduranceGamingEnabled = true,
            IntelEnduranceGamingPreset = (int)ctl_3d_endurance_gaming_mode_t.PERFORMANCE // 60fps
        });

        DevicePowerProfiles.Add(new(Properties.Resources.PowerProfileMSIClawBestPerformance, Properties.Resources.PowerProfileMSIClawBestPerformanceDesc)
        {
            Default = true,
            DeviceDefault = true,
            OSPowerMode = OSPowerMode.BestPerformance,
            Guid = BestPerformanceGuid,
            TDPOverrideEnabled = true,
            TDPOverrideValues = new[] { 35.0d, 35.0d, 35.0d },
            IntelEnduranceGamingEnabled = false,
            IntelEnduranceGamingPreset = (int)ctl_3d_endurance_gaming_mode_t.PERFORMANCE // GPU Auto TDP is Off, FPS depends on the game, and it can be up to 120
        });

        OEMChords.Add(new KeyboardChord(name: "CLAW", button: ButtonFlags.OEM1));
        OEMChords.Add(new KeyboardChord(name: "QS", button: ButtonFlags.OEM2));
        OEMChords.Add(new KeyboardChord(name: "M1", button: ButtonFlags.OEM3));
        OEMChords.Add(new KeyboardChord(name: "M2", button: ButtonFlags.OEM4));

        OEMChords.Add(new KeyboardChord("LButton",
            [KeyCode.LButton | KeyCode.OemClear],
            [KeyCode.LButton | KeyCode.OemClear],
            true, ButtonFlags.None
        ));

        // Hacky, BIOS 10F
        OEMChords.Add(new KeyboardChord("QS", [KeyCode.LWin, KeyCode.G], [KeyCode.G, KeyCode.LWin], false, ButtonFlags.OEM2));
        OEMChords.Add(new KeyboardChord("QS, Long-press", [KeyCode.LWin, KeyCode.Tab], [KeyCode.Tab, KeyCode.LWin], false, ButtonFlags.OEM2));

        // prepare hotkeys
        DeviceHotkeys[typeof(MainWindowCommands)].inputsChord.ButtonState[ButtonFlags.OEM1] = true;
        DeviceHotkeys[typeof(QuickToolsCommands)].inputsChord.ButtonState[ButtonFlags.OEM2] = true;
    }

    public override bool Open()
    {
        bool success = base.Open();
        if (!success)
            return false;

        if (hidDevices.TryGetValue(INPUT_HID_ID, out var xinputDevice))
        {
            // update firmware
            Firmware = xinputDevice.Attributes.Version;

            LogManager.LogInformation("Device Firmware: {0:X4}, {1}", Firmware, IsSupported ? "Supported" : "Unsupported");
        }

        SetShiftMode(ShiftModeCalcType.Deactive);

        // OverBoost
        int uefiVariableEx = 0;
        byte[] box = GetMsiDCVarData(ref uefiVariableEx);
        if (uefiVariableEx != 0)
        {
            if (box[1] == 0)
            {
                InitOverBoost(true);
                Thread.Sleep(600);
            }

            /*
            // Check if OverBoostSup is enabled
            bool OverBoostSup = GetOverBoostSup();
            if (OverBoostSup)
            {
                // Check if OverBoost is enabled
                bool OverBoost = GetOverBoost();
                if (OverBoost)
                {
                    // disable OverBoost ?
                }
            }
            */
        }

        // make sure M1/M2 are recognized as buttons
        if (hidDevices.TryGetValue(INPUT_HID_ID, out HidDevice? device))
        {
            Thread.Sleep(300);
            device.Write(GetM12(true, gamepadMode == GamepadMode.XInput), 0, 64);
            Thread.Sleep(500);
            device.Write(GetM12(false, gamepadMode == GamepadMode.XInput), 0, 64);
            Thread.Sleep(500);
            SyncToROM();
            Thread.Sleep(500);
            SwitchMode(gamepadMode);
            Thread.Sleep(2000);
        }

        // set flag
        ClawOpen = true;

        // prepare WMI
        GetWMI();

        // unlock TDP
        set_long_limit(35);
        set_short_limit(35);

        // start WMI event monitor
        StartWatching();

        return true;
    }

    public override void PowerProfileManager_Applied(PowerProfile profile, UpdateSource source)
    {
        if (profile.FanProfile.fanMode == FanMode.Software)
        {
            // MSI Claw goes from 0% to 150%
            byte[] fanTable = new byte[8];
            fanTable[0] = (byte)(profile.FanProfile.fanSpeeds[4] / 100.0d * 150.0d);    // 20% (backup ?)
            fanTable[1] = (byte)(profile.FanProfile.fanSpeeds[0] / 100.0d * 150.0d);    // 0%
            fanTable[2] = (byte)(profile.FanProfile.fanSpeeds[2] / 100.0d * 150.0d);    // 20%
            fanTable[3] = (byte)(profile.FanProfile.fanSpeeds[5] / 100.0d * 150.0d);    // 50%
            fanTable[4] = (byte)(profile.FanProfile.fanSpeeds[6] / 100.0d * 150.0d);    // 60%
            fanTable[5] = (byte)(profile.FanProfile.fanSpeeds[8] / 100.0d * 150.0d);    // 80%
            fanTable[6] = (byte)(profile.FanProfile.fanSpeeds[9] / 100.0d * 150.0d);    // 90%
            fanTable[7] = (byte)(profile.FanProfile.fanSpeeds[10] / 100.0d * 150.0d);   // 100%

            // FAN-936 R1: update the fan table first, then only latch software
            // control once that write is fully verified. If the table could not be
            // read or written, no software-control success state is established;
            // the channel is left as-is instead of being reported as controlled.
            // (only the six editable duties are replaced, the block boundaries
            // and the tail of each block are preserved.)
            MsiFanTableWriteResult tableWrite = SetFanTable(fanTable, profile.FanProfile.fanMode.ToString());
            if (tableWrite.Success)
            {
                SetFanControl(true);
            }
            else
            {
                LogManager.LogWarning("[FAN-936] MSI fan table not fully verified ({0}); software control not engaged", tableWrite.Summary);
            }
        }
        else
        {
            // FAN-936 R2: hardware hand-back. Do not rewrite a fixed fan table
            // through this callback - that would clobber the user's preexisting
            // table. Hand the channel back to the firmware and preserve whatever
            // table is present. With no valid baseline the release routine
            // reports an honest firmware-auto / unknown result.
            ReleaseMsiFanControl("power-profile-hardware-release");
        }

        // MSI Center, API_UserScenario
        bool IsDcMode = SystemInformation.PowerStatus.PowerLineStatus == PowerLineStatus.Offline;
        if (profile.Guid == BetterBatteryGuid)
        {
            SetShiftMode(ShiftModeCalcType.ChangeToCurrentShiftType, IsDcMode ? ShiftType.None : ShiftType.ECO);
        }
        else if (profile.Guid == BetterPerformanceGuid)
        {
            SetShiftMode(ShiftModeCalcType.ChangeToCurrentShiftType, IsDcMode ? ShiftType.None : ShiftType.GreenMode);
        }
        else if (profile.Guid == BestPerformanceGuid)
        {
            SetShiftMode(ShiftModeCalcType.ChangeToCurrentShiftType, IsDcMode ? ShiftType.None : ShiftType.SportMode);
        }
        else
        {
            SetShiftMode(ShiftModeCalcType.ChangeToCurrentShiftType, IsDcMode ? ShiftType.None : ShiftType.SportMode);
        }
    }

    private int LEDBrightness = 100;
    private Color LEDMainColor = Colors.Black;
    private Color LEDSecondColor = Colors.Black;

    protected override void QuerySettings()
    {
        // raise events
        SettingsManager_SettingValueChanged("MSIClawControllerIndex", ManagerFactory.settingsManager.GetInt("MSIClawControllerIndex"), false, false);
        SettingsManager_SettingValueChanged("BatteryChargeLimit", ManagerFactory.settingsManager.GetInt("BatteryChargeLimit"), false, false);
        SettingsManager_SettingValueChanged("BatteryChargeLimitPercent", ManagerFactory.settingsManager.GetInt("BatteryChargeLimitPercent"), false, false);

        base.QuerySettings();
    }

    protected override void SettingsManager_SettingValueChanged(string name, object? value, bool temporary, bool initializing)
    {
        switch (name)
        {
            case "BatteryChargeLimit":
                bool enabled = Convert.ToBoolean(value);
                SetBatteryMaster(enabled);
                break;
            case "BatteryChargeLimitPercent":
                int percent = Convert.ToInt32(value);
                SetBatteryChargeLimit(percent);
                break;
            case "MSIClawControllerIndex":
                {
                    gamepadMode = (GamepadMode)Convert.ToInt32(value);
                    ApplyM12Configuration();
                    SwitchMode(gamepadMode);
                }
                break;
        }

        base.SettingsManager_SettingValueChanged(name, value, temporary, initializing);
    }

    public override void Close()
    {
        // Hand the fan channel back before tearing the session down:
        // 152 (full speed) first, then 212 (software control).
        ReleaseMsiFanControl("hc-close");

        // stop WMI event monitor
        StopWatching();

        // configure controller to XInput
        SwitchMode(GamepadMode.XInput);

        // close devices
        foreach (HidDevice hidDevice in hidDevices.Values)
            hidDevice.Dispose();
        hidDevices.Clear();

        // set flag
        ClawOpen = false;

        base.Close();
    }

    private void ApplyM12Configuration()
    {
        if (!hidDevices.TryGetValue(INPUT_HID_ID, out HidDevice? device))
            return;

        Thread.Sleep(300);
        device.Write(GetM12(true, gamepadMode == GamepadMode.XInput), 0, 64);
        Thread.Sleep(500);
        device.Write(GetM12(false, gamepadMode == GamepadMode.XInput), 0, 64);
        Thread.Sleep(500);
        SyncToROM();
        Thread.Sleep(500);
    }

    protected byte[] GetMsiDCVarData(ref int uefiVariableEx)
    {
        byte[] box = new byte[4096];
        uefiVariableEx = GetUEFIVariableEx("MsiDCVarData", MsIDCVarData, box);
        return box;
    }

    protected void InitOverBoost(bool enabled)
    {
        int uefiVariableEx = 0;
        byte[] box = GetMsiDCVarData(ref uefiVariableEx);
        Thread.Sleep(600);

        // set value
        box[1] = (byte)(enabled ? 1 : 0);
        SetUEFIVariableEx("MsiDCVarData", MsIDCVarData, box, uefiVariableEx);
        Thread.Sleep(600);
    }

    public async void SetOverBoost(bool enabled)
    {
        int uefiVariableEx = 0;
        byte[] box = GetMsiDCVarData(ref uefiVariableEx);
        Thread.Sleep(600);

        // set value
        box[6] = (byte)(enabled ? 1 : 0);
        SetUEFIVariableEx("MsiDCVarData", MsIDCVarData, box, uefiVariableEx);
        Thread.Sleep(600);

        Task<ContentDialogResult> dialogTask = new Dialog(MainWindow.GetCurrent())
        {
            Title = Properties.Resources.Dialog_ForceRestartTitle,
            Content = Properties.Resources.Dialog_ForceRestartDesc,
            DefaultButton = ContentDialogButton.Close,
            CloseButtonText = Properties.Resources.Dialog_No,
            PrimaryButtonText = Properties.Resources.Dialog_Yes
        }.ShowAsync();

        await dialogTask; // sync call

        switch (dialogTask.Result)
        {
            case ContentDialogResult.Primary:
                DeviceUtils.RestartComputer();
                break;
            case ContentDialogResult.Secondary:
                break;
        }
    }

    public bool HasOverBoost()
    {
        int uefiVariableEx = 0;
        byte[] box = GetMsiDCVarData(ref uefiVariableEx);
        if (uefiVariableEx != 0)
            return box[1] != 0;
        return false;
    }

    public bool GetOverBoost()
    {
        int uefiVariableEx = 0;
        byte[] box = GetMsiDCVarData(ref uefiVariableEx);
        if (uefiVariableEx != 0)
            return box[6] != 0;
        return false;
    }

    public bool GetOverBoostSup()
    {
        int uefiVariableEx = 0;
        byte[] box = GetMsiDCVarData(ref uefiVariableEx);
        if (uefiVariableEx != 0)
            return box[7] != 0;
        return false;
    }

    protected void GetWMI()
    {
        byte iDataBlockIndex = 1;

        byte[] dataWMI = WMI.Get(WmiScope, WmiPath, "Get_WMI", iDataBlockIndex, 32, out bool readWMI);
        if (dataWMI.Length > 2 && dataWMI[1] >= 2)
        {
            this.WmiMajorVersion = dataWMI[1];
            this.WmiMinorVersion = dataWMI[2];
        }
    }

    protected bool SetMotionStatus(bool enabled)
    {
        if (hidDevices.TryGetValue(INPUT_HID_ID, out HidDevice? device))
        {
            byte[] msg = { 15, 0, 0, 60, (byte)CommandType.SetMotionStatus, (byte)(enabled ? 1 : 0) };
            if (device.Write(msg, 0, 64))
            {
                LogManager.LogInformation("Successfully SetMotionStatus to {0}", enabled);
                return true;
            }
            else
            {
                LogManager.LogWarning("Failed to SetMotionStatus to {0}", enabled);
                return false;
            }
        }

        return false;
    }

    protected bool SwitchMode(GamepadMode gamepadMode)
    {
        if (hidDevices.TryGetValue(INPUT_HID_ID, out HidDevice? device))
        {
            byte[] msg = { 15, 0, 0, 60, (byte)CommandType.SwitchMode, (byte)gamepadMode, (byte)MKeysFunction.Macro };
            if (device.Write(msg, 0, 64))
            {
                LogManager.LogInformation("Successfully switched controller mode to {0}", gamepadMode);
                return true;
            }
            else
            {
                LogManager.LogWarning("Failed to switch controller mode to {0}", gamepadMode);
                return false;
            }
        }

        return false;
    }

    public bool SwitchToXInput()
    {
        return SwitchMode(GamepadMode.XInput);
    }

    public bool SwitchToDirectInput()
    {
        return SwitchMode(GamepadMode.DirectInput);
    }

    public bool SwitchToDesktop()
    {
        return SwitchMode(GamepadMode.Desktop);
    }

    protected bool SyncToROM()
    {
        if (hidDevices.TryGetValue(INPUT_HID_ID, out HidDevice? device))
        {
            byte[] msg = { 15, 0, 0, 60, (byte)CommandType.SyncToROM };
            if (device.Write(msg, 0, 64))
            {
                LogManager.LogInformation("Successfully synced to ROM");
                return true;
            }
            else
            {
                LogManager.LogWarning("Failed to sync to ROM");
                return false;
            }
        }

        return false;
    }

    public override bool IsReady()
    {
        // Early return if device is already bound and connected
        if (hidDevices.TryGetValue(INPUT_HID_ID, out HidDevice? boundDevice))
        {
            if (boundDevice.IsConnected /* && boundDevice.IsOpen */)
                return true;
        }

        // Search for supported HidDevice
        IEnumerable <HidDevice> devices = GetHidDevices(vendorId, productIds, 0);
        foreach (HidDevice device in devices)
        {
            if (!device.IsConnected)
                continue;

            if (!hidFilters.TryGetValue(device.Attributes.ProductId, out HidFilter hidFilter))
                continue;

            if (device.Capabilities.UsagePage != hidFilter.UsagePage || device.Capabilities.Usage != hidFilter.Usage)
                continue;

            // update device
            hidDevices[INPUT_HID_ID] = device;

            return true;
        }

        return false;
    }

    private static string GetMsiApCfgTargetPath()
    {
        var win = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
        return Path.Combine(win, "SysWOW64", "msiapcfg.dll");
    }

    private static bool CheckAndDeployWmiAcpi()
    {
        try
        {
            string target = GetMsiApCfgTargetPath();
            if (File.Exists(target))
                return true;

            var uri = new Uri("pack://application:,,,/Resources/msiapcfg.dll", UriKind.Absolute);
            using var s = System.Windows.Application.GetResourceStream(uri).Stream;
            using var ms = new MemoryStream();
            s.CopyTo(ms);
            byte[] payload = ms.ToArray();
            if (payload is null || payload.Length == 0)
            {
                LogManager.LogWarning("msiapcfg.dll resource not found.");
                return false;
            }

            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.WriteAllBytes(target, payload);
            LogManager.LogInformation("Deployed {0}", target);
            return true;
        }
        catch (Exception ex)
        {
            LogManager.LogWarning("Failed to deploy msiapcfg.dll: {0}", ex.Message);
            return false;
        }
    }

    private static bool CheckAndFixRegistry()
    {
        try
        {
            // Ensure the key exists
            RegistryUtils.CreateKey(WmiAcpiRegKey); // returns true/false, safe to ignore
            string want = GetMsiApCfgTargetPath();

            // Check current value
            var current = RegistryUtils.GetString(WmiAcpiRegKey, WmiAcpiRegValue);
            if (!string.Equals(current, want, StringComparison.OrdinalIgnoreCase))
            {
                RegistryUtils.SetValue(WmiAcpiRegKey, WmiAcpiRegValue, want);
                LogManager.LogInformation(@"Set {0}\{1} = {2}", WmiAcpiRegKey, WmiAcpiRegValue, want);
            }

            return true;
        }
        catch (Exception ex)
        {
            LogManager.LogWarning("Failed to ensure WmiAcpi registry: {0}", ex.Message);
            return false;
        }
    }

    private static bool MsiEventClassExists()
    {
        try
        {
            var scope = new ManagementScope(@"\\.\root\WMI");
            scope.Connect();
            using var searcher = new ManagementObjectSearcher(
                scope, new WqlObjectQuery("SELECT * FROM meta_class WHERE __class = 'MSI_Event'"));
            using var results = searcher.Get();
            return results != null && results.Count > 0;
        }
        catch
        {
            return false;
        }
    }

    private static bool RestartAcpiPnpDevice()
    {
        try
        {
            // Find ACPI\PNP0C14* instances
            var scope = new ManagementScope(@"\\.\root\CIMV2");
            scope.Connect();
            using var searcher = new ManagementObjectSearcher(scope,
                new ObjectQuery("SELECT PNPDeviceID FROM Win32_PnPEntity WHERE PNPDeviceID LIKE 'ACPI\\\\PNP0C14%'"));

            foreach (ManagementObject mo in searcher.Get())
            {
                string? id = mo["PNPDeviceID"] as string;
                if (string.IsNullOrWhiteSpace(id))
                    continue;

                // Prefer a dedicated Restart if you add it; otherwise Disable->Enable
                if (PnPUtil.RestartDevice(id))
                {
                    LogManager.LogInformation("Restarted device: {0}", id);
                    return true;
                }
            }

            LogManager.LogWarning("No ACPI\\PNP0C14 instance found.");
            return false;
        }
        catch (Exception ex)
        {
            LogManager.LogWarning("Failed to restart ACPI\\PNP0C14: {0}", ex.Message);
            return false;
        }
    }

    public override bool SetLedBrightness(int brightness)
    {
        // store value
        LEDBrightness = brightness;

        if (hidDevices.TryGetValue(INPUT_HID_ID, out HidDevice? device))
            return device.Write(GetRGB(brightness, LEDMainColor, LEDSecondColor), 0, 64);

        return false;
    }

    public override bool SetLedColor(Color MainColor, Color SecondaryColor, LEDLevel level, int speed = 100)
    {
        // store values
        LEDMainColor = MainColor;
        LEDSecondColor = SecondaryColor;

        if (hidDevices.TryGetValue(INPUT_HID_ID, out HidDevice? device))
        {
            switch (level)
            {
                case LEDLevel.SolidColor:
                    return device.Write(GetRGB(LEDBrightness, MainColor, MainColor), 0, 64);
                case LEDLevel.Ambilight:
                    return device.Write(GetRGB(LEDBrightness, MainColor, SecondaryColor), 0, 64);
            }
        }

        return false;
    }

    private byte[] GetRGB(double brightness, Color MainColor, Color SecondaryColor)
    {
        // grab the right array (or null if no device)
        byte[]? RGBdata = FirmwareDevice?.RGB;

        // pick actual values
        byte add1 = RGBdata != null ? RGBdata[0] : (byte)0x01;
        byte add2 = RGBdata != null ? RGBdata[1] : (byte)0xFA;

        List<byte> data = new List<byte>
        {
            // Preamble
            0x0F, 0x00, 0x00, 0x3C,

            // Write first profile
            0x21, 0x01,

            // Start at
            add1, add2,

            // Write 31 bytes
            0x20,

            // Index, Frame num, Effect, Speed, Brightness
            0x00, 0x01, 0x09, 0x03,
            (byte)Math.Max(0, Math.Min(100, (int)brightness))
        };

        // Append [red, green, blue] * 9
        // right is 0, 1, 2, 3
        // left is 4, 5, 6, 7
        // buttons is 8

        for (int i = 0; i < 9; i++)
        {
            data.Add(i < 4 ? SecondaryColor.R : MainColor.R);
            data.Add(i < 4 ? SecondaryColor.G : MainColor.G);
            data.Add(i < 4 ? SecondaryColor.B : MainColor.B);
        }

        return data.ToArray();
    }

    private byte[] GetM12(bool useM1)
    {
        return GetM12(useM1, false);
    }

    private byte[] GetM12(bool useM1, bool xinput)
    {
        // grab the right array (or null if no device)
        byte[]? data = xinput
            ? (useM1 ? FirmwareDevice?.M1XInput : FirmwareDevice?.M2XInput)
            : (useM1 ? FirmwareDevice?.M1DInput : FirmwareDevice?.M2DInput);

        // choose your two fallback bytes
        byte defaultAdd1 = useM1 ? (byte)0x00 : (byte)0x01;
        byte defaultAdd2 = useM1 ? (byte)0x7A : (byte)0x1F;

        if (xinput)
            defaultAdd2++;

        // pick actual values
        byte add1 = data != null ? data[0] : defaultAdd1;
        byte add2 = data != null ? data[1] : defaultAdd2;

        return xinput
            ?
            [
                0x0F, 0x00, 0x00, 0x3C,
                0x21, 0x01,
                add1, add2,
                0x07,
                0x04, 0x00,
                useM1 ? (byte)0x7A : (byte)0x7D,
                0xFF, 0xFF, 0xFF, 0xFF
            ]
            :
            [
                0x0F, 0x00, 0x00, 0x3C,
                0x21, 0x01,
                add1, add2,
                0x02, 0x01, 0x00
            ];
    }

    protected override void Device_Removed()
    {
        // close device
        if (hidDevices.TryGetValue(INPUT_HID_ID, out HidDevice? device))
        {
            try { device.Dispose(); } catch { }
        }
    }

    protected override async void Device_Inserted(bool reScan = false)
    {
        // if you still want to automatically re-attach:
        if (reScan)
            await WaitUntilReady();

        // listen for events
        if (hidDevices.TryGetValue(INPUT_HID_ID, out HidDevice? device))
        {
            device.OpenDevice();

            ApplyM12Configuration();
            SwitchMode(gamepadMode);
        }
    }

    protected async void StartWatching()
    {
        try
        {
            // Ensure DLL is present
            CheckAndDeployWmiAcpi();

            // Ensure registry points WmiAcpi to the DLL
            CheckAndFixRegistry();

            // Ensure MSI_Event exists; if not, restart ACPI\PNP0C14 and wait a bit
            if (!MsiEventClassExists())
            {
                RestartAcpiPnpDevice();

                // small wait-loop
                Task timeout = Task.Delay(TimeSpan.FromSeconds(5));
                while (!timeout.IsCompleted && !MsiEventClassExists())
                    await Task.Delay(250).ConfigureAwait(false);
            }

            // Start watcher if available
            if (!MsiEventClassExists())
            {
                LogManager.LogWarning("Failed to hook into MSI_Event");
                return;
            }

            ManagementScope scope = new ManagementScope("\\\\.\\root\\WMI");
            specialKeyWatcher = new ManagementEventWatcher(scope, new WqlEventQuery("SELECT * FROM MSI_Event"));
            specialKeyWatcher.EventArrived += onWMIEvent;
            specialKeyWatcher.Start();
        }
        catch (Exception ex)
        {
            LogManager.LogError("Exception configuring MSI_Event monitor: {0}", ex.Message);
        }
    }

    protected void StopWatching()
    {
        try
        {
            if (specialKeyWatcher is not null)
            {
                specialKeyWatcher.EventArrived -= onWMIEvent;
                specialKeyWatcher.Stop();
                specialKeyWatcher.Dispose();
                specialKeyWatcher = null;
            }
        }
        catch (Exception ex)
        {
            LogManager.LogError("Exception unconfiguring MSI_Event monitor: {0}", ex.Message);
        }
    }

    private void onWMIEvent(object sender, EventArrivedEventArgs e)
    {
        int WMIEvent = Convert.ToInt32(e.NewEvent.Properties["MSIEvt"].Value);
        WMIEventCode key = (WMIEventCode)(WMIEvent & byte.MaxValue);

        // LogManager.LogDebug("Received MSI WMI Event Code {0}", (int)key);

        if (!keyMapping.ContainsKey(key))
            return;

        // get button
        ButtonFlags button = keyMapping[key];
        switch (key)
        {
            default:
            case WMIEventCode.LaunchMcxMainUI:  // MSI Claw: Click
            case WMIEventCode.LaunchMcxOSD:     // Quick Settings: Click
                KeyPressAndRelease(button, KeyPressDelay);
                break;
        }
    }

    private void SetBatteryMaster(bool enable)
    {
        // Data block index specific to battery mode settings
        byte dataBlockIndex = 215;

        // Get the current battery data (1 byte) from the device
        byte[] data = WMI.Get(WmiScope, WmiPath, "Get_Data", dataBlockIndex, 1, out bool readSuccess);
        if (readSuccess)
            data[0] = data[0].SetBit(7, enable);

        // Build the complete 32-byte package
        byte[] fullPackage = new byte[32];
        fullPackage[0] = dataBlockIndex;
        fullPackage[1] = data[0];

        // Set the battery mode using the package.
        WMI.Set(WmiScope, WmiPath, "Set_Data", fullPackage);
    }

    private bool GetBatteryChargeLimit(ref byte currentValue)
    {
        // Data block index specific to battery mode settings
        byte dataBlockIndex = 215;

        // Get the current battery data (1 byte) from the device
        byte[] data = WMI.Get(WmiScope, WmiPath, "Get_Data", dataBlockIndex, 1, out bool readSuccess);
        if (readSuccess)
            currentValue = data[0];

        return readSuccess;
    }

    private void SetBatteryChargeLimit(int chargeLimit)
    {
        // Data block index specific to battery mode settings
        byte dataBlockIndex = 215;

        // Get the current battery data (1 byte) from the device
        byte currentValue = 0;
        GetBatteryChargeLimit(ref currentValue);

        // Update mask
        byte mask = (byte)(currentValue & (uint)sbyte.MaxValue);

        // Build the complete 32-byte package
        byte[] fullPackage = new byte[32];
        fullPackage[0] = dataBlockIndex;
        fullPackage[1] = (byte)(currentValue - mask + chargeLimit);

        // Set the battery mode using the package.
        WMI.Set(WmiScope, WmiPath, "Set_Data", fullPackage);
    }

    // FAN-936 ---------------------------------------------------------------
    // MSI fan channel: read-only sampling, hardened bit writes, and one shared
    // release routine for FanView disable / automatic hand-back / host exit /
    // HC Close.
    //
    // The rules below were taken from the CTW reference that was validated
    // off-line and are deliberately conservative:
    //   * never write the whole 8-byte fan table: read the full block first,
    //     change only payload[1..6], keep payload[0], payload[7] and the tail;
    //   * hand back in the order 152 (full speed) then 212 (software control);
    //   * a read failure means "do not write", never "assume success";
    //   * read back after every write and report ok / failed / unknown honestly.

    private static readonly Lazy<string> BootIdentity = new(() =>
    {
        try
        {
            DateTime bootUtc = DateTime.UtcNow - TimeSpan.FromMilliseconds(Environment.TickCount64);
            return bootUtc.ToString("yyyy-MM-ddTHH:mm:ssZ");
        }
        catch
        {
            return "unknown-boot";
        }
    });

    private static readonly Lazy<string> HostInstanceId = new(() =>
    {
        try
        {
            using System.Diagnostics.Process process = System.Diagnostics.Process.GetCurrentProcess();
            return $"{Environment.ProcessId}@{process.StartTime.ToUniversalTime():yyyy-MM-ddTHH:mm:ssZ}";
        }
        catch
        {
            return $"{Environment.ProcessId}@unknown";
        }
    });

    public sealed class MsiFanBitWriteResult
    {
        public string ReaderVersion { get; set; } = "FAN-936/1";
        public string Name { get; set; } = string.Empty;
        public byte WriteBlockIndex { get; set; }
        public WMI.WmiIoOutcome ReadOutcome { get; set; } = WMI.WmiIoOutcome.NotAttempted;
        public bool ReadEffective { get; set; }
        public byte Before { get; set; }
        public byte Target { get; set; }
        public bool AlreadyAtTarget { get; set; }
        public bool WriteAttempted { get; set; }
        public WMI.WmiIoOutcome WriteOutcome { get; set; } = WMI.WmiIoOutcome.NotAttempted;
        public WMI.WmiIoOutcome ReadbackOutcome { get; set; } = WMI.WmiIoOutcome.NotAttempted;
        public bool ReadbackValid { get; set; }
        public byte ReadbackValue { get; set; }
        public bool Verified { get; set; }
        public string Status { get; set; } = "unknown";
        public string Detail { get; set; } = string.Empty;
    }

    public sealed class MsiFanReleaseResult
    {
        public string ReaderVersion { get; set; } = "FAN-936/1";
        public string Cause { get; set; } = string.Empty;
        public string DeviceType { get; set; } = string.Empty;
        // FAN-936-R1 section 5.2: false when Open() has not observed the firmware yet,
        // in which case Firmware is "unknown" and not a measured 0x0000.
        public bool FirmwareKnown { get; set; }
        public string Firmware { get; set; } = string.Empty;
        public string HostInstanceId { get; set; } = string.Empty;
        public string BootIdentity { get; set; } = string.Empty;
        public string OperationToken { get; set; } = string.Empty;
        public string StartedAtUtc { get; set; } = string.Empty;
        public string CompletedAtUtc { get; set; } = string.Empty;
        public MsiFanBitWriteResult? FullSpeed152 { get; set; }
        public MsiFanBitWriteResult? SoftwareControl212 { get; set; }
        public bool Success { get; set; }
        public bool Failed { get; set; }
        public string Status { get; set; } = "unknown";
        public string Summary { get; set; } = string.Empty;
    }

    public sealed class MsiFanTableBlockResult
    {
        public byte Block { get; set; }
        public WMI.WmiIoOutcome ReadOutcome { get; set; } = WMI.WmiIoOutcome.NotAttempted;
        public bool ReadEffective { get; set; }
        public bool WriteAttempted { get; set; }
        public WMI.WmiIoOutcome WriteOutcome { get; set; } = WMI.WmiIoOutcome.NotAttempted;
        public WMI.WmiIoOutcome ReadbackOutcome { get; set; } = WMI.WmiIoOutcome.NotAttempted;
        public bool Verified { get; set; }
        public string Status { get; set; } = "unknown";
        public int[] RequestedDuties6 { get; set; } = Array.Empty<int>();
        public int[] ReadbackDuties6 { get; set; } = Array.Empty<int>();

        // payload[0] and payload[7]: the two boundary bytes that must survive.
        public byte[] PreservedPayload { get; set; } = Array.Empty<byte>();

        // FAN-936 R4 wire-contract evidence. rawLength is derived from the
        // decoder (WMI.GetStructured strips exactly one leading status/flag
        // byte), so rawLength == payloadLength + 1; it is not a separately
        // captured raw sample.
        public int RequestLength { get; set; }
        public int RequiredPayloadLength { get; set; }
        public int PayloadLength { get; set; }
        public int RawLength { get; set; }

        // FAN-936 R4: honest write responsibility.
        // not-written | write-unknown | written-unverified | verified
        public string WriteDisposition { get; set; } = "not-written";
        public string Detail { get; set; } = string.Empty;
    }

    public sealed class MsiFanTableWriteResult
    {
        public string ReaderVersion { get; set; } = "FAN-936/1";
        public string DeviceType { get; set; } = string.Empty;
        public string HostInstanceId { get; set; } = string.Empty;
        public string BootIdentity { get; set; } = string.Empty;
        public string FanMode { get; set; } = string.Empty;
        public string CapturedAtUtc { get; set; } = string.Empty;
        public List<MsiFanTableBlockResult> Blocks { get; set; } = new();
        public bool Success { get; set; }
        public string Status { get; set; } = "unknown";

        // FAN-936 R4: never report a two-block write as all-or-nothing when only
        // one block was written. Disposition aggregate:
        // verified | written-unverified | partial | not-written
        public bool PartialWrite { get; set; }
        public string WriteDisposition { get; set; } = "not-written";
        public string Summary { get; set; } = string.Empty;
    }

    public sealed class MsiFanStateSnapshot
    {
        public string ReaderVersion { get; set; } = "FAN-936/1";
        public bool ReadOnlyNoHardwareWrite { get; set; } = true;
        public string DeviceType { get; set; } = string.Empty;
        // FAN-936-R1 section 5.2: Firmware is only observed after Open() reads the
        // controller version. Until then it is unknown and MUST NOT be reported as a
        // measured 0x0000 - that would be indistinguishable from a real read of 0.
        public bool FirmwareKnown { get; set; }
        public string Firmware { get; set; } = string.Empty;
        public string FirmwareHex { get; set; } = string.Empty;
        public string HostInstanceId { get; set; } = string.Empty;
        public string BootIdentity { get; set; } = string.Empty;
        public bool DeviceOpen { get; set; }
        public bool ClawOpen { get; set; }
        public string CapturedAtUtc { get; set; } = string.Empty;

        public byte[] FanTableBlock1Raw { get; set; } = Array.Empty<byte>();
        public WMI.WmiIoOutcome FanTableBlock1Outcome { get; set; } = WMI.WmiIoOutcome.NotAttempted;
        public byte[] FanTableBlock2Raw { get; set; } = Array.Empty<byte>();
        public WMI.WmiIoOutcome FanTableBlock2Outcome { get; set; } = WMI.WmiIoOutcome.NotAttempted;

        // Get_Data(152): bit7 is the firmware "full speed" latch.
        public byte[] FullSpeedRaw { get; set; } = Array.Empty<byte>();
        public bool FullSpeedBit7 { get; set; }
        public WMI.WmiIoOutcome FullSpeedOutcome { get; set; } = WMI.WmiIoOutcome.NotAttempted;

        // Get_AP(1): first byte bit7 is the software fan control latch.
        public byte[] SoftwareControlRaw { get; set; } = Array.Empty<byte>();
        public byte SoftwareControlFirstByte { get; set; }
        public bool SoftwareControlBit7 { get; set; }
        public WMI.WmiIoOutcome SoftwareControlOutcome { get; set; } = WMI.WmiIoOutcome.NotAttempted;

        // Get_AP(0): byte[2] is the Shift/UserScenario value.
        public byte[] ShiftRaw { get; set; } = Array.Empty<byte>();
        public int ShiftValue { get; set; } = -1;
        public WMI.WmiIoOutcome ShiftOutcome { get; set; } = WMI.WmiIoOutcome.NotAttempted;

        public bool CoreReadsEffective { get; set; }
        public string FailureStage { get; set; } = string.Empty;
        public string Summary { get; set; } = string.Empty;
    }

    /// <summary>
    /// Read-only MSI fan/EC state sample. Never calls Open() and never writes
    /// to the hardware, so it is safe to take before YMCC or the MSI front end
    /// has touched the device.
    /// </summary>
    public MsiFanStateSnapshot ReadMsiFanStateReadOnly()
    {
        // FAN-936-R1 section 5.2: the firmware is unknown until Open() has read it.
        // Report it as "unknown" rather than a fabricated 0x0000.
        bool firmwareKnown = Firmware != 0;
        MsiFanStateSnapshot snapshot = new()
        {
            DeviceType = GetType().Name,
            FirmwareKnown = firmwareKnown,
            Firmware = firmwareKnown ? $"0x{Firmware:X4}" : "unknown",
            FirmwareHex = firmwareKnown ? Firmware.ToString("X4") : string.Empty,
            HostInstanceId = HostInstanceId.Value,
            BootIdentity = BootIdentity.Value,
            DeviceOpen = DeviceOpen,
            ClawOpen = ClawOpen,
            CapturedAtUtc = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"),
        };

        List<string> failures = new();

        lock (msiFanGate)
        {
            WMI.WmiIoResult fan1 = WMI.GetStructured(WmiScope, WmiPath, "Get_Fan", 1, 32, 8);
            WMI.WmiIoResult fan2 = WMI.GetStructured(WmiScope, WmiPath, "Get_Fan", 2, 32, 8);
            WMI.WmiIoResult fullSpeed = WMI.GetStructured(WmiScope, WmiPath, "Get_Data", 152, 1, 1);
            WMI.WmiIoResult softwareControl = WMI.GetStructured(WmiScope, WmiPath, "Get_AP", 1, WMI.GetAPLength(1), 1);
            WMI.WmiIoResult shift = WMI.GetStructured(WmiScope, WmiPath, "Get_AP", 0, WMI.GetAPLength(0), 3);

            snapshot.FanTableBlock1Raw = fan1.Data;
            snapshot.FanTableBlock1Outcome = fan1.Outcome;
            snapshot.FanTableBlock2Raw = fan2.Data;
            snapshot.FanTableBlock2Outcome = fan2.Outcome;

            snapshot.FullSpeedRaw = fullSpeed.Data;
            snapshot.FullSpeedOutcome = fullSpeed.Outcome;
            snapshot.FullSpeedBit7 = fullSpeed.IsUsable && fullSpeed.Data[0].GetBit(7);

            snapshot.SoftwareControlRaw = softwareControl.Data;
            snapshot.SoftwareControlOutcome = softwareControl.Outcome;
            snapshot.SoftwareControlFirstByte = softwareControl.Data.Length > 0 ? softwareControl.Data[0] : (byte)0;
            snapshot.SoftwareControlBit7 = softwareControl.IsUsable && softwareControl.Data[0].GetBit(7);

            snapshot.ShiftRaw = shift.Data;
            snapshot.ShiftOutcome = shift.Outcome;
            if (shift.IsUsable)
                snapshot.ShiftValue = shift.Data[2];

            if (!fan1.IsUsable) failures.Add($"fanTable1={fan1.Outcome}");
            if (!fan2.IsUsable) failures.Add($"fanTable2={fan2.Outcome}");
            if (!fullSpeed.IsUsable) failures.Add($"fullSpeed152={fullSpeed.Outcome}");
            if (!softwareControl.IsUsable) failures.Add($"softwareControl212={softwareControl.Outcome}");
            if (!shift.IsUsable) failures.Add($"shiftAP0={shift.Outcome}");
        }

        snapshot.CoreReadsEffective = failures.Count == 0;
        snapshot.FailureStage = string.Join(";", failures);
        snapshot.Summary = snapshot.CoreReadsEffective
            ? $"read-only sample ok: fullSpeed152.bit7={snapshot.FullSpeedBit7} software212.bit7={snapshot.SoftwareControlBit7} shift={snapshot.ShiftValue}"
            : $"read-only sample incomplete: {snapshot.FailureStage}";

        return snapshot;
    }

    /// <summary>
    /// The single MSI release routine. Clears the firmware full-speed latch
    /// (152) first and the software control latch (212) second, preserving
    /// every other bit, and never writes when the read is not effective.
    /// </summary>
    public MsiFanReleaseResult ReleaseMsiFanControl(string cause)
    {
        // FAN-936 R1: count the entry, not the success. The host uses the delta
        // across the Close boundary to know the hand-back routine was entered
        // even when it later throws or publishes no bound receipt.
        ReleaseMsiFanControlInvocationCount++;

        // FAN-936-R1 section 5.2: do not present an unobserved firmware as measured 0.
        bool firmwareKnown = Firmware != 0;
        MsiFanReleaseResult result = new()
        {
            Cause = cause,
            DeviceType = GetType().Name,
            FirmwareKnown = firmwareKnown,
            Firmware = firmwareKnown ? $"0x{Firmware:X4}" : "unknown",
            HostInstanceId = HostInstanceId.Value,
            BootIdentity = BootIdentity.Value,
            OperationToken = MsiReleaseOperationToken ?? string.Empty,
            StartedAtUtc = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"),
        };

        lock (msiFanGate)
        {
            // CTW order: full speed first, then software control.
            result.FullSpeed152 = SetControlBit("fullspeed152", 152, "Get_Data", 152, 1, 1, false);
            result.SoftwareControl212 = SetControlBit("softwarecontrol212", 212, "Get_AP", 1, WMI.GetAPLength(1), 1, false);
        }

        bool fullSpeedAcceptable = IsReleaseStepAcceptable(result.FullSpeed152);
        bool softwareAcceptable = IsReleaseStepAcceptable(result.SoftwareControl212);

        result.Success = fullSpeedAcceptable && softwareAcceptable;
        result.Failed = !result.Success;
        // FAN-936 §3: a failed read / missing readback is "unknown" (fail-closed),
        // not "failed": the Host must not treat "could not tell" as "cleared".
        result.Status = result.Success
            ? "ok"
            : result.FullSpeed152?.Status == "unknown" || result.SoftwareControl212?.Status == "unknown"
                ? "unknown"
                : "failed";
        result.CompletedAtUtc = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ");
        result.Summary = $"release[{cause}] fullSpeed152={result.FullSpeed152?.Status}/{result.FullSpeed152?.ReadOutcome} software212={result.SoftwareControl212?.Status}/{result.SoftwareControl212?.ReadOutcome}";

        LastMsiFanRelease = result;

        if (result.Success)
            LogManager.LogInformation("[FAN-936] MSI fan release ok: {0}", result.Summary);
        else
            LogManager.LogWarning("[FAN-936] MSI fan release NOT verified: {0}", result.Summary);

        return result;
    }

    private static bool IsReleaseStepAcceptable(MsiFanBitWriteResult? step)
    {
        return step != null && (step.Status == "ok" || step.Status == "skipped-already-target");
    }

    /// <summary>
    /// Reads one byte, flips bit7 to the requested value, writes the block back
    /// and reads it again. Nothing is written unless the initial read is
    /// effective, and an already-correct bit is never rewritten.
    /// </summary>
    private MsiFanBitWriteResult SetControlBit(string name, byte writeBlockIndex, string readMethod, byte readBlockIndex, int readLength, int readMinimum, bool value)
    {
        MsiFanBitWriteResult step = new()
        {
            Name = name,
            WriteBlockIndex = writeBlockIndex,
        };

        WMI.WmiIoResult read = WMI.GetStructured(WmiScope, WmiPath, readMethod, readBlockIndex, readLength, readMinimum);
        step.ReadOutcome = read.Outcome;
        step.ReadEffective = read.IsUsable;

        if (!read.IsUsable || !read.TryGetFirstByte(out byte before))
        {
            step.Status = ClassifyMsiReadFailure(read.Outcome);
            step.Detail = $"read unavailable: {read.Outcome} {read.Detail}";
            return step;
        }

        step.Before = before;
        step.Target = before.SetBit(7, value);

        if (before.GetBit(7) == value)
        {
            step.AlreadyAtTarget = true;
            step.ReadbackValid = true;
            step.ReadbackValue = before;
            step.Verified = true;
            step.Status = "skipped-already-target";
            step.Detail = "bit7 already at target, no write issued";
            return step;
        }

        // Keep every bit except bit7: only the target byte changes.
        byte[] fullPackage = new byte[32];
        fullPackage[0] = writeBlockIndex;
        fullPackage[1] = step.Target;

        step.WriteAttempted = true;
        WMI.WmiIoResult write = WMI.SetStructured(WmiScope, WmiPath, "Set_Data", fullPackage);
        step.WriteOutcome = write.Outcome;

        if (!write.TransportOk)
        {
            step.Status = write.Outcome == WMI.WmiIoOutcome.Exception ? "unknown" : "failed";
            step.Detail = $"write failed: {write.Outcome} {write.Detail}";
            return step;
        }

        WMI.WmiIoResult readback = WMI.GetStructured(WmiScope, WmiPath, readMethod, readBlockIndex, readLength, readMinimum);
        step.ReadbackOutcome = readback.Outcome;

        if (!readback.IsUsable || !readback.TryGetFirstByte(out byte after))
        {
            step.Status = "unknown";
            step.Detail = $"readback unavailable: {readback.Outcome} {readback.Detail}";
            return step;
        }

        step.ReadbackValid = true;
        step.ReadbackValue = after;

        if (after.GetBit(7) != value)
        {
            step.Status = "failed";
            step.Detail = "readback bit7 does not match target";
            return step;
        }

        step.Verified = true;
        step.Status = "ok";
        step.Detail = "bit7 written and verified";
        return step;
    }

    private static string ClassifyMsiReadFailure(WMI.WmiIoOutcome outcome)
    {
        switch (outcome)
        {
            case WMI.WmiIoOutcome.Exception:
            case WMI.WmiIoOutcome.TransportFailed:
            case WMI.WmiIoOutcome.InParamsUnavailable:
                return "unknown";
            default:
                return "failed";
        }
    }

    /// <summary>
    /// Writes the fan curve without ever touching the block boundary bytes: the
    /// 31-byte writable payload (payload[0], payload[7] and the tail
    /// payload[8..30]) is read back and carried verbatim, only the six editable
    /// duties (payload[1..6]) are replaced.
    /// </summary>
    private MsiFanTableWriteResult SetFanTable(byte[] fanTable, string fanMode)
    {
        MsiFanTableWriteResult result = new()
        {
            DeviceType = GetType().Name,
            HostInstanceId = HostInstanceId.Value,
            BootIdentity = BootIdentity.Value,
            FanMode = fanMode,
            CapturedAtUtc = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"),
        };

        lock (msiFanGate)
        {
            /*
             * iDataBlockIndex = 1; // CPU
             * iDataBlockIndex = 2; // GPU
             */
            for (byte iDataBlockIndex = 1; iDataBlockIndex <= 2; iDataBlockIndex++)
                result.Blocks.Add(WriteMsiFanTableBlock(iDataBlockIndex, fanTable));
        }

        result.Success = result.Blocks.Count == 2 && result.Blocks.TrueForAll(block => block.Status == "ok");
        result.Status = result.Success
            ? "ok"
            : result.Blocks.Exists(block => block.Status == "unknown") ? "unknown" : "failed";

        // FAN-936 R4: aggregate the real write responsibility across the two
        // blocks. The write is non-atomic and sequential, so when only one block
        // was written the result must say "partial", never "all" or "nothing".
        // FAN-936 R2: a block whose post-invocation outcome is unknown may have
        // been written, so that uncertainty must dominate the aggregate — it can
        // neither be reported as a clean "not-written" nor be called "partial".
        int total = result.Blocks.Count;
        int notWritten = result.Blocks.FindAll(block => block.WriteDisposition == "not-written").Count;
        int verified = result.Blocks.FindAll(block => block.WriteDisposition == "verified").Count;
        int writeUnknown = result.Blocks.FindAll(block => block.WriteDisposition == "write-unknown").Count;
        result.PartialWrite = writeUnknown == 0
            && notWritten > 0 && notWritten < total
            && verified > 0;
        result.WriteDisposition =
            writeUnknown > 0 ? "write-unknown"
            : total > 0 && verified == total ? "verified"
            : total > 0 && notWritten == total ? "not-written"
            : notWritten > 0 ? "partial"
            : "written-unverified";

        result.Summary = string.Join("; ", result.Blocks.ConvertAll(block =>
            $"block{block.Block}={block.Status}/{block.WriteDisposition}"));

        LastMsiFanTableWrite = result;
        return result;
    }

    private MsiFanTableBlockResult WriteMsiFanTableBlock(byte block, byte[] fanTable)
    {
        MsiFanTableBlockResult blockResult = new()
        {
            Block = block,
            RequestedDuties6 = new int[6],
            RequestLength = MsiFanTableRequestLength,
            RequiredPayloadLength = MsiFanTableWritableDataLength,
        };

        // Read the whole block first: the boundary bytes and the tail must
        // survive verbatim. A partial read leaves the tail unknown, so the block
        // cannot be written back as if it had been preserved.
        // default: 49, 0, 40, 49, 58, 67, 75, 75
        WMI.WmiIoResult read = WMI.GetStructured(WmiScope, WmiPath, "Get_Fan", block, MsiFanTableRequestLength, 8);
        blockResult.ReadOutcome = read.Outcome;
        blockResult.ReadEffective = read.IsUsable;

        if (!read.IsUsable)
        {
            blockResult.Status = ClassifyMsiReadFailure(read.Outcome);
            blockResult.WriteDisposition = "not-written";
            blockResult.Detail = $"read unavailable: {read.Outcome} {read.Detail}";
            return blockResult;
        }

        blockResult.PayloadLength = read.Data.Length;
        // WMI.GetStructured strips exactly the leading status/flag byte, so the
        // raw response length is the payload length plus that one byte.
        blockResult.RawLength = read.Data.Length + 1;

        // FAN-936 R4: a payload shorter than the 31-byte writable data region
        // means the tail of the block is unknown. Do not zero-pad the missing
        // bytes and claim the block was preserved - refuse the whole write
        // instead. The full-length real-device payload is 31 bytes, so this
        // check must not reject it (that was the R3 defect).
        if (read.Data.Length < MsiFanTableWritableDataLength)
        {
            blockResult.Status = "unknown";
            blockResult.WriteDisposition = "not-written";
            blockResult.Detail = $"read truncated: {read.Data.Length}/{MsiFanTableWritableDataLength} bytes; block not written";
            return blockResult;
        }

        for (int i = 0; i < 6; i++)
            blockResult.RequestedDuties6[i] = Math.Clamp((int)fanTable[i + 1], 0, 150);

        // FAN-936 R4: keep the pre-existing 32-byte request boundary; do not
        // widen Set_Fan to 33 bytes. After the leading block index only 31
        // payload bytes fit, so the request carries read.Data[0..30] verbatim.
        // If a synthetic/future response supplies a 32-byte payload, its 32nd
        // byte is explicitly outside this write's preservation guarantee: it is
        // not sent, so it cannot be claimed as preserved. The check above and
        // this copy share MsiFanTableWritableDataLength so they cannot drift.
        byte[] fullPackage = new byte[MsiFanTableRequestLength];
        fullPackage[0] = block;
        Array.Copy(read.Data, 0, fullPackage, MsiFanTableHeaderLength, MsiFanTableWritableDataLength);

        // payload[1..6] are the six editable duties.
        for (int i = 0; i < 6; i++)
            fullPackage[2 + i] = (byte)blockResult.RequestedDuties6[i];

        // payload[0] and payload[7] as read back from the device.
        blockResult.PreservedPayload = new[] { fullPackage[1], fullPackage[8] };

        blockResult.WriteAttempted = true;
        WMI.WmiIoResult write = WMI.SetStructured(WmiScope, WmiPath, "Set_Fan", fullPackage);
        blockResult.WriteOutcome = write.Outcome;

        if (!write.TransportOk)
        {
            // FAN-936 R2: only a failure known to precede the WMI call — the
            // in-parameters could not be built, so Set_Fan was never invoked —
            // proves nothing was written. A transport failure after InvokeMethod
            // returned (a null response) and an exception both prove only that
            // the *response* is missing; the device may already have applied the
            // write, so the physical outcome is unknown, never "not-written".
            bool neverInvoked = write.Outcome == WMI.WmiIoOutcome.InParamsUnavailable;
            blockResult.Status = neverInvoked ? "failed" : "unknown";
            blockResult.WriteDisposition = neverInvoked ? "not-written" : "write-unknown";
            blockResult.Detail = $"write failed: {write.Outcome} {write.Detail}";
            return blockResult;
        }

        WMI.WmiIoResult readback = WMI.GetStructured(WmiScope, WmiPath, "Get_Fan", block, MsiFanTableRequestLength, 8);
        blockResult.ReadbackOutcome = readback.Outcome;

        if (!readback.IsUsable || readback.Data.Length < 7)
        {
            // The write was transported; only the confirmation is missing.
            blockResult.Status = "unknown";
            blockResult.WriteDisposition = "written-unverified";
            blockResult.Detail = $"readback unavailable: {readback.Outcome} {readback.Detail}";
            return blockResult;
        }

        int[] readbackDuties = new int[6];
        bool match = true;
        for (int i = 0; i < 6; i++)
        {
            readbackDuties[i] = readback.Data[1 + i];
            if (readbackDuties[i] != blockResult.RequestedDuties6[i])
                match = false;
        }

        blockResult.ReadbackDuties6 = readbackDuties;

        if (!match)
        {
            blockResult.Status = "failed";
            blockResult.WriteDisposition = "written-unverified";
            blockResult.Detail = "readback duties do not match the requested duties";
            return blockResult;
        }

        blockResult.Verified = true;
        blockResult.Status = "ok";
        blockResult.WriteDisposition = "verified";
        blockResult.Detail = "six duties written and verified";
        return blockResult;
    }

    public override void set_long_limit(int limit)
    {
        SetCPUPowerLimit(80, limit);
    }

    public override void set_short_limit(int limit)
    {
        SetCPUPowerLimit(81, limit);
    }

    protected void SetCPUPowerLimit(int iDataBlockIndex, int limit)
    {
        /*
         * iDataBlockIndex = 80; // Long (SPL | PL1)
         * iDataBlockIndex = 81; // Short (sPPT | PL2)
         * iDataBlockIndex = 82; // Very short (fPPT AMD only)
         */

        // Build the complete 32-byte package:
        byte[] fullPackage = new byte[32];
        fullPackage[0] = (byte)iDataBlockIndex;
        fullPackage[1] = (byte)limit;

        WMI.Set(WmiScope, WmiPath, "Set_Data", fullPackage);
    }

    public override void SetFanControl(bool enable, int mode = 0)
    {
        // 212: software fan control latch (Get_AP block 1, bit7 of the first byte).
        MsiFanBitWriteResult step;

        lock (msiFanGate)
        {
            step = SetControlBit("softwarecontrol212", 212, "Get_AP", 1, WMI.GetAPLength(1), 1, enable);
            LastMsiFanControlWrite = step;
        }

        if (step.Status == "ok" || step.Status == "skipped-already-target")
            LogManager.LogInformation("[FAN-936] MSI software fan control set to {0}: {1}", enable, step.Status);
        else
            LogManager.LogWarning("[FAN-936] MSI software fan control NOT applied: {0} {1}", step.Status, step.Detail);
    }

    public void SetFanFullSpeed(bool enable)
    {
        // 152: firmware full speed latch (Get_Data block 152, bit7).
        MsiFanBitWriteResult step;

        lock (msiFanGate)
        {
            step = SetControlBit("fullspeed152", 152, "Get_Data", 152, 1, 1, enable);
            LastMsiFanFullSpeedWrite = step;
        }

        if (step.Status == "ok" || step.Status == "skipped-already-target")
            LogManager.LogInformation("[FAN-936] MSI fan full speed set to {0}: {1}", enable, step.Status);
        else
            LogManager.LogWarning("[FAN-936] MSI fan full speed NOT applied: {0} {1}", step.Status, step.Detail);
    }

    public int GetShiftValue()
    {
        byte iDataBlockIndex = 0;

        // Optional: decode the value if needed.
        // bool isSupported = (shiftValue & 128) != 0;
        // bool isActive = (shiftValue & 64) != 0;
        // int modeValue = shiftValue & 0x3F; // lower 6 bits
        byte[] data = WMI.Get(WmiScope, WmiPath, "Get_AP", iDataBlockIndex, WMI.GetAPLength(iDataBlockIndex), out bool readSuccess);
        if (readSuccess)
            return data[2];

        return -1;
    }

    public void SetShiftValue(int newShiftValue)
    {
        byte iDataBlockIndex = 210;

        byte[] fullPackage = new byte[32];
        fullPackage[0] = iDataBlockIndex;
        fullPackage[1] = (byte)newShiftValue;

        // Write the package back to the EC.
        WMI.Set(WmiScope, WmiPath, "Set_Data", fullPackage);
    }

    public bool IsShiftSupported()
    {
        int currentValue = GetShiftValue();
        return (currentValue & 128) != 0;
    }

    protected virtual int GetShiftModeValue(ShiftType shiftType)
    {
        return shiftType switch
        {
            ShiftType.SportMode => 4,
            ShiftType.ComfortMode => 0,
            ShiftType.GreenMode => 1,
            ShiftType.ECO => 2,
            ShiftType.User => 3,
            _ => 0,
        };
    }

    public void SetShiftMode(ShiftModeCalcType calcType, ShiftType shiftType = ShiftType.None)
    {
        if (!IsShiftSupported())
            return;

        int ShiftModeValueInEC = GetShiftValue();
        ShiftModeValueInEC &= 195;

        switch (calcType)
        {
            case ShiftModeCalcType.Active:
                ShiftModeValueInEC |= 128;
                ShiftModeValueInEC |= 64;
                break;
            case ShiftModeCalcType.Deactive:
                ShiftModeValueInEC |= 128;
                ShiftModeValueInEC &= 191;
                break;
            case ShiftModeCalcType.ChangeToCurrentShiftType:
                ShiftModeValueInEC |= 192;
                ShiftModeValueInEC &= 252;
                ShiftModeValueInEC += GetShiftModeValue(shiftType);
                break;
        }

        SetShiftValue(ShiftModeValueInEC);
    }

    public override string GetGlyph(ButtonFlags button)
    {
        switch (button)
        {
            case ButtonFlags.OEM1:
                return "\uE010";
            case ButtonFlags.OEM2:
                return "\uE011";
            case ButtonFlags.OEM3:
                return "\u2212";
            case ButtonFlags.OEM4:
                return "\u2213";
        }

        return defaultGlyph;
    }
}