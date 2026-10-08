using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace HandheldCompanion.Devices.Lenovo
{
    /// <summary>
    /// Complete interop surface for SapientiaUsb.dll.
    ///
    /// This file is split in two layers:
    /// 1) SapientiaUsb: typed wrappers for signatures that are either already present in your wrapper
    ///    or strongly suggested by the export names and observed behavior.
    /// 2) SapientiaUsb.Raw: exact export-name coverage for all exported symbols through NativeLibrary,
    ///    so you can bind additional delegates safely as signatures are confirmed.
    ///
    /// Notes:
    /// - On x64 Windows, the ABI is unified, so CallingConvention.Winapi is appropriate here.
    /// - Native bool returns are marshalled as 1-byte bool where that is the most likely intent.
    /// - A few complex keymap / macro APIs are intentionally left in the Raw layer until their
    ///   buffer layouts are fully confirmed.
    /// </summary>
    public static class SapientiaUsb
    {
        private const string DllName = "SapientiaUsb.dll";
        private static readonly object _lock = new object();

        #region Delegates / callbacks

        public delegate void GyroStateCbFunc(int leftGyroState, int rightGyroState);
        public delegate void GyroDataBackFunc(int leftGyroX, int leftGyroY, int rightGyroX, int rightGyroY);
        public delegate void GyroSensorStatusCbFunc(GyroSensorStatus leftGyro, GyroSensorStatus rightGyro);

        public delegate void VoidCallback();
        public delegate void IntCallback(int value);
        public delegate void TwoIntCallback(int left, int right);
        public delegate void ThreeIntCallback(int a, int b, int c);
        public delegate void FourIntCallback(int a, int b, int c, int d);
        public delegate void StickCallback(int lx, int ly, int rx, int ry);
        public delegate void TriggerCallback(int lt, int rt);
        public delegate void TouchPadCallback(int x, int y, int state);
        public delegate void LinkStateBackFunc(int state);
        public delegate void PowerBackFunc(int state);
        public delegate void MouseWheelStatusBackFunc(int delta);
        public delegate void TestButtonBackFunc(int button, int state);
        public delegate void UpdateBackFunc(int state);
        public delegate void FPSSwitchBackFunc(int enabled);
        public delegate void FindDeviceFunc(int device);
        public delegate void FinishCalibrationCallback(int state);
        public delegate void TerminateMacroRecordCallback(int state);

        #endregion

        #region Structs

        [StructLayout(LayoutKind.Sequential)]
        public struct GyroSensorStatus
        {
            public uint gyro_timestamp;
            public int g_sensor_ax;
            public int g_sensor_ay;
            public int g_sensor_az;
            public int g_sensor_gx;
            public int g_sensor_gy;
            public int g_sensor_gz;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct LightionProfile
        {
            public int effect;
            public int r;
            public int g;
            public int b;
            public int brightness;
            public int speed;
            public int profile;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct LegionJoystickCurveProfile
        {
            public int X1;
            public int X2;
            public int Y1;
            public int Y2;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct LegionTriggerDeadzone
        {
            public int Deadzone;
            public int Margin;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct VERSION
        {
            public int verPro;
            public int verCMD;
            public int verFir;
            public int verHard;
        }

        #endregion

        #region Native imports - core / confirmed

        [DllImport(DllName, EntryPoint = "Init", CallingConvention = CallingConvention.Winapi)]
        private static extern void InitInternal();

        [DllImport(DllName, EntryPoint = "dllInit", CallingConvention = CallingConvention.Winapi)]
        private static extern void DllInitInternal();

        [DllImport(DllName, EntryPoint = "FreeSapientiaUsb", CallingConvention = CallingConvention.Winapi)]
        private static extern void FreeSapientiaUsbInternal();

        [DllImport(DllName, EntryPoint = "GetLastErr", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetLastErrInternal();

        [DllImport(DllName, EntryPoint = "SetGamePadMode", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGamePadModeInternal(int modeType);

        [DllImport(DllName, EntryPoint = "GetGamePadMode", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetGamePadModeInternal();

        [DllImport(DllName, EntryPoint = "GetLeftGyroStatus", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetLeftGyroStatusInternal();

        [DllImport(DllName, EntryPoint = "SetLeftGyroStatus", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetLeftGyroStatusInternal(int status);

        [DllImport(DllName, EntryPoint = "GetRightGyroStatus", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetRightGyroStatusInternal();

        [DllImport(DllName, EntryPoint = "SetRightGyroStatus", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetRightGyroStatusInternal(int status);

        [DllImport(DllName, EntryPoint = "SetGyroState", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGyroStateInternal(int gyroIndex, int value);

        [DllImport(DllName, EntryPoint = "SetGyroStateCbFunc", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGyroStateCbFuncInternal(GyroStateCbFunc cbFunc);

        [DllImport(DllName, EntryPoint = "GetGyroMode", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetGyroModeInternal();

        [DllImport(DllName, EntryPoint = "GetGyroModeStatus", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetGyroModeStatusInternal(int mode, int gyroIndex);

        [DllImport(DllName, EntryPoint = "SetGyroModeStatus", CallingConvention = CallingConvention.Winapi)]
        private static extern int SetGyroModeStatusInternal(int mode, int gyroIndex, int gyroStatus);

        [DllImport(DllName, EntryPoint = "SetGyroDataBackFunc", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGyroDataBackFuncInternal(GyroDataBackFunc callback);

        [DllImport(DllName, EntryPoint = "SetGyroSensorStatusBackFunc", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGyroSensorStatusBackFuncInternal(GyroSensorStatusCbFunc callback);

        [DllImport(DllName, EntryPoint = "GetLlightingEffectEnable", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetLlightingEffectEnableInternal(int device);

        [DllImport(DllName, EntryPoint = "SetLightingEnable", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetLightingEnableInternal(int device, [MarshalAs(UnmanagedType.I1)] bool enabled);

        [DllImport(DllName, EntryPoint = "GetCurrentLightProfile", CallingConvention = CallingConvention.Winapi)]
        private static extern LightionProfile GetCurrentLightProfileInternal(int device);

        [DllImport(DllName, EntryPoint = "SetLightingEffectProfileID", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetLightingEffectProfileIDInternal(int device, LightionProfile profile);

        [DllImport(DllName, EntryPoint = "GetQuickLightingEffect", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetQuickLightingEffectInternal(int device);

        [DllImport(DllName, EntryPoint = "GetQuickLightingEffectEnable", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetQuickLightingEffectEnableInternal(int device);

        [DllImport(DllName, EntryPoint = "SetQuickLightingEffectEnable", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetQuickLightingEffectEnableInternal(int device, [MarshalAs(UnmanagedType.I1)] bool enable);

        [DllImport(DllName, EntryPoint = "SetQuickLightingEffect", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetQuickLightingEffectInternal(int device, int index);

        [DllImport(DllName, EntryPoint = "GetTouchPadStatus", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetTouchPadStatusInternal();

        [DllImport(DllName, EntryPoint = "GetTrackpadStatus", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetTrackpadStatusInternal(int device);

        [DllImport(DllName, EntryPoint = "SetTouchPadStatus", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetTouchPadStatusInternal(int state);

        [DllImport(DllName, EntryPoint = "SetTrackpadStatus", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetTrackpadStatusInternal(int device, int state);

        [DllImport(DllName, EntryPoint = "SetDeviceDefault", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetDeviceDefaultInternal(int device);

        [DllImport(DllName, EntryPoint = "getUSBVerify", CallingConvention = CallingConvention.Winapi)]
        private static extern VERSION GetUSBVerifyInternal(int device);

        [DllImport(DllName, EntryPoint = "GetStickCustomCurve", CallingConvention = CallingConvention.Winapi)]
        private static extern LegionJoystickCurveProfile GetStickCustomCurveInternal(int device);

        [DllImport(DllName, EntryPoint = "SetStickCustomCurve", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetStickCustomCurveInternal(int device, LegionJoystickCurveProfile curveProfile);

        [DllImport(DllName, EntryPoint = "GetStickCustomDeadzone", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetStickCustomDeadzoneInternal(int device);

        [DllImport(DllName, EntryPoint = "SetStickCustomDeadzone", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetStickCustomDeadzoneInternal(int device, int deadzone);

        [DllImport(DllName, EntryPoint = "SetGyroSensorDataOnorOff", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGyroSensorDataOnorOffInternal(int device, int status);

        [DllImport(DllName, EntryPoint = "GetGyroSensorDataOnorOff", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetGyroSensorDataOnorOffInternal(int device);

        [DllImport(DllName, EntryPoint = "GetTriggerDeadzoneAndMargin", CallingConvention = CallingConvention.Winapi)]
        private static extern LegionTriggerDeadzone GetTriggerDeadzoneAndMarginInternal(int device);

        [DllImport(DllName, EntryPoint = "SetTriggerDeadzoneAndMargin", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetTriggerDeadzoneAndMarginInternal(int device, LegionTriggerDeadzone deadzone);

        [DllImport(DllName, EntryPoint = "GetAutoSleepTime", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetAutoSleepTimeInternal(int device);

        [DllImport(DllName, EntryPoint = "SetAutoSleepTime", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetAutoSleepTimeInternal(int device, int autoSleepTime);

        [DllImport(DllName, EntryPoint = "GetLightingMode", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetLightingModeInternal(int device);

        [DllImport(DllName, EntryPoint = "SetLightingMode", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetLightingModeInternal(int device, int mode);

        [DllImport(DllName, EntryPoint = "GetMouseCurrentDPI", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetMouseCurrentDPIInternal(int device);

        [DllImport(DllName, EntryPoint = "GetMouseDPIProfileID", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetMouseDPIProfileIDInternal(int device);

        [DllImport(DllName, EntryPoint = "SetMouseDPIProfileID", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetMouseDPIProfileIDInternal(int device, int profileId);

        [DllImport(DllName, EntryPoint = "SetMouseDPI", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetMouseDPIInternal(int device, int dpi);

        [DllImport(DllName, EntryPoint = "GetTouchpadVibrateState", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetTouchpadVibrateStateInternal(int device);

        [DllImport(DllName, EntryPoint = "SetTouchpadVibrateState", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetTouchpadVibrateStateInternal(int device, int state);

        [DllImport(DllName, EntryPoint = "GetVibrationMode", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetVibrationModeInternal(int device);

        [DllImport(DllName, EntryPoint = "SetVibrationMode", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetVibrationModeInternal(int device, int mode);

        [DllImport(DllName, EntryPoint = "GetVibrationStatus", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetVibrationStatusInternal(int device);

        [DllImport(DllName, EntryPoint = "SetVibrationStatus", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetVibrationStatusInternal(int device, int status);

        [DllImport(DllName, EntryPoint = "GetVibrateNotifyState", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetVibrateNotifyStateInternal(int device);

        [DllImport(DllName, EntryPoint = "SetVibrateNotifyState", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetVibrateNotifyStateInternal(int device, int state);

        [DllImport(DllName, EntryPoint = "GetCurrentCurveType", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetCurrentCurveTypeInternal(int device);

        [DllImport(DllName, EntryPoint = "GetStickPresetCurve", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetStickPresetCurveInternal(int device);

        [DllImport(DllName, EntryPoint = "SetStickPresetCurve", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetStickPresetCurveInternal(int device, int preset);

        [DllImport(DllName, EntryPoint = "GetGyroAxisReverse", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetGyroAxisReverseInternal(int device);

        [DllImport(DllName, EntryPoint = "SetGyroAxisReverse", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGyroAxisReverseInternal(int device, int reverseMask);

        [DllImport(DllName, EntryPoint = "GetGyroDeadzone", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetGyroDeadzoneInternal(int device);

        [DllImport(DllName, EntryPoint = "SetGyroDeadzone", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGyroDeadzoneInternal(int device, int value);

        [DllImport(DllName, EntryPoint = "GetGyroDeadzoneCompensate", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetGyroDeadzoneCompensateInternal(int device);

        [DllImport(DllName, EntryPoint = "SetGyroDeadzoneCompensate", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGyroDeadzoneCompensateInternal(int device, int value);

        [DllImport(DllName, EntryPoint = "GetGyroMappingTrigger", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetGyroMappingTriggerInternal(int device);

        [DllImport(DllName, EntryPoint = "SetGyroMappingTrigger", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGyroMappingTriggerInternal(int device, int trigger);

        [DllImport(DllName, EntryPoint = "GetGyroMappingType", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetGyroMappingTypeInternal(int device);

        [DllImport(DllName, EntryPoint = "SetGyroMappingType", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGyroMappingTypeInternal(int device, int type);

        [DllImport(DllName, EntryPoint = "GetGyroOutputMixer", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetGyroOutputMixerInternal(int device);

        [DllImport(DllName, EntryPoint = "SetGyroOutputMixer", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGyroOutputMixerInternal(int device, int mixer);

        [DllImport(DllName, EntryPoint = "GetGyroSensitivity", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetGyroSensitivityInternal(int device);

        [DllImport(DllName, EntryPoint = "SetGyroSensitivity", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGyroSensitivityInternal(int device, int sensitivity);

        [DllImport(DllName, EntryPoint = "GetGyroXYSensitivity", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetGyroXYSensitivityInternal(int device, int axis);

        [DllImport(DllName, EntryPoint = "SetGyroXYSensitivity", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGyroXYSensitivityInternal(int device, int axis, int sensitivity);

        [DllImport(DllName, EntryPoint = "SetGyroEnabled", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGyroEnabledInternal(int device, int enabled);

        [DllImport(DllName, EntryPoint = "GetFPSMode", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetFPSModeInternal(int device);

        [DllImport(DllName, EntryPoint = "SetFPSMode", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetFPSModeInternal(int device, int mode);

        [DllImport(DllName, EntryPoint = "GetFPSModeStatus", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetFPSModeStatusInternal(int device);

        [DllImport(DllName, EntryPoint = "SetGamepadOnorOff", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetGamepadOnorOffInternal(int device, int enabled);

        [DllImport(DllName, EntryPoint = "GetLegionLRAndMenuViewMode", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetLegionLRAndMenuViewModeInternal(int device);

        [DllImport(DllName, EntryPoint = "SetLegionLRAndMenuViewMode", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetLegionLRAndMenuViewModeInternal(int device, int mode);

        [DllImport(DllName, EntryPoint = "GetMachineType", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetMachineTypeInternal();

        [DllImport(DllName, EntryPoint = "GetYButton", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetYButtonInternal(int device);

        [DllImport(DllName, EntryPoint = "SetYButton", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetYButtonInternal(int device, int value);

        [DllImport(DllName, EntryPoint = "SetYButton2", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetYButton2Internal(int device, int value);

        [DllImport(DllName, EntryPoint = "SetDeviceSN", CallingConvention = CallingConvention.Winapi, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetDeviceSNInternal(int device, string serialNumber);

        [DllImport(DllName, EntryPoint = "GetDeviceSN", CallingConvention = CallingConvention.Winapi)]
        private static extern IntPtr GetDeviceSNInternal(int device);

        [DllImport(DllName, EntryPoint = "SendButtonTest", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SendButtonTestInternal(int device, int button, int value);

        [DllImport(DllName, EntryPoint = "SetTestInputMode", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetTestInputModeInternal(int device, int mode);

        [DllImport(DllName, EntryPoint = "sendVibrate", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SendVibrateInternal(int which, byte strength);

        [DllImport(DllName, EntryPoint = "setHandleMode", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetHandleModeInternal(byte mode);

        [DllImport(DllName, EntryPoint = "setMode", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetModeInternal(int mode);

        [DllImport(DllName, EntryPoint = "StartGyroCalibration", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool StartGyroCalibrationInternal(int device);

        [DllImport(DllName, EntryPoint = "StopGyroCalibration", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool StopGyroCalibrationInternal(int device);

        [DllImport(DllName, EntryPoint = "StartStickCalibration", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool StartStickCalibrationInternal(int device);

        [DllImport(DllName, EntryPoint = "StopStickCalibration", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool StopStickCalibrationInternal(int device);

        [DllImport(DllName, EntryPoint = "StartTriggerCalibration", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool StartTriggerCalibrationInternal(int device);

        [DllImport(DllName, EntryPoint = "StopTriggerCalibration", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool StopTriggerCalibrationInternal(int device);

        [DllImport(DllName, EntryPoint = "StartStickTriggerCalibration", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool StartStickTriggerCalibrationInternal(int device);

        [DllImport(DllName, EntryPoint = "StopStickTriggerCalibration", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool StopStickTriggerCalibrationInternal(int device);

        [DllImport(DllName, EntryPoint = "RockerCalibration", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool RockerCalibrationInternal(int device, int state);

        [DllImport(DllName, EntryPoint = "RockerCalibrationEnd", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool RockerCalibrationEndInternal(int device);

        [DllImport(DllName, EntryPoint = "RockerCalibrationFinish", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool RockerCalibrationFinishInternal(int device);

        [DllImport(DllName, EntryPoint = "HandleFeelCalibration", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool HandleFeelCalibrationInternal(int device, int state);

        [DllImport(DllName, EntryPoint = "HandleFeelCalibrationEnd", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool HandleFeelCalibrationEndInternal(int device);

        [DllImport(DllName, EntryPoint = "HandleFeelCalibrationFinish", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool HandleFeelCalibrationFinishInternal(int device);

        [DllImport(DllName, EntryPoint = "feelRockback", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool FeelRockbackInternal(int device, int enabled);

        [DllImport(DllName, EntryPoint = "GetUpdateState", CallingConvention = CallingConvention.Winapi)]
        private static extern int GetUpdateStateInternal();

        [DllImport(DllName, EntryPoint = "SendUpdataOTAcmd", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SendUpdataOTACmdInternal(int cmd, int value);

        #endregion

        #region Native imports - callback registration

        [DllImport(DllName, EntryPoint = "SetFPSSwitchBackFun", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetFPSSwitchBackFunInternal(FPSSwitchBackFunc callback);

        [DllImport(DllName, EntryPoint = "SetFindDeviceFunc", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetFindDeviceFuncInternal(FindDeviceFunc callback);

        [DllImport(DllName, EntryPoint = "SetFinishCalibrationCallback", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetFinishCalibrationCallbackInternal(FinishCalibrationCallback callback);

        [DllImport(DllName, EntryPoint = "SetLinkStateBackFunc", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetLinkStateBackFuncInternal(LinkStateBackFunc callback);

        [DllImport(DllName, EntryPoint = "SetMouseWheelStatusBackFunc", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetMouseWheelStatusBackFuncInternal(MouseWheelStatusBackFunc callback);

        [DllImport(DllName, EntryPoint = "SetPowerBackFunc", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetPowerBackFuncInternal(PowerBackFunc callback);

        [DllImport(DllName, EntryPoint = "SetStickBackFunc", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetStickBackFuncInternal(StickCallback callback);

        [DllImport(DllName, EntryPoint = "SetTerminateMacroRecordCallback", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetTerminateMacroRecordCallbackInternal(TerminateMacroRecordCallback callback);

        [DllImport(DllName, EntryPoint = "SetTestButtonBackFunc", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetTestButtonBackFuncInternal(TestButtonBackFunc callback);

        [DllImport(DllName, EntryPoint = "SetTouchPadBackFunc", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetTouchPadBackFuncInternal(TouchPadCallback callback);

        [DllImport(DllName, EntryPoint = "SetTriggerBackFunc", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetTriggerBackFuncInternal(TriggerCallback callback);

        [DllImport(DllName, EntryPoint = "SetUpdateBackFun", CallingConvention = CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool SetUpdateBackFunInternal(UpdateBackFunc callback);

        #endregion

        #region Public typed wrappers

        public static void Init() { lock (_lock) InitInternal(); }
        public static void DllInit() { lock (_lock) DllInitInternal(); }
        public static void FreeSapientiaUsb() { lock (_lock) FreeSapientiaUsbInternal(); }
        public static int GetLastErr() { lock (_lock) return GetLastErrInternal(); }

        public static bool SetGamePadMode(int modeType) { lock (_lock) return SetGamePadModeInternal(modeType); }
        public static int GetGamePadMode() { lock (_lock) return GetGamePadModeInternal(); }

        public static int GetLeftGyroStatus() { lock (_lock) return GetLeftGyroStatusInternal(); }
        public static bool SetLeftGyroStatus(int status) { lock (_lock) return SetLeftGyroStatusInternal(status); }
        public static int GetRightGyroStatus() { lock (_lock) return GetRightGyroStatusInternal(); }
        public static bool SetRightGyroStatus(int status) { lock (_lock) return SetRightGyroStatusInternal(status); }
        public static bool SetGyroState(int gyroIndex, int value) { lock (_lock) return SetGyroStateInternal(gyroIndex, value); }
        public static bool SetGyroStateCbFunc(GyroStateCbFunc cb) { lock (_lock) return SetGyroStateCbFuncInternal(cb); }
        public static int GetGyroMode() { lock (_lock) return GetGyroModeInternal(); }
        public static int GetGyroModeStatus(int mode, int gyroIndex) { lock (_lock) return GetGyroModeStatusInternal(mode, gyroIndex); }
        public static int SetGyroModeStatus(int mode, int gyroIndex, int gyroStatus) { lock (_lock) return SetGyroModeStatusInternal(mode, gyroIndex, gyroStatus); }
        public static bool SetGyroDataBackFunc(GyroDataBackFunc callback) { lock (_lock) return SetGyroDataBackFuncInternal(callback); }
        public static bool SetGyroSensorStatusBackFunc(GyroSensorStatusCbFunc callback) { lock (_lock) return SetGyroSensorStatusBackFuncInternal(callback); }

        public static int GetLlightingEffectEnable(int device) { lock (_lock) return GetLlightingEffectEnableInternal(device); }
        public static bool SetLightingEnable(int device, bool enabled) { lock (_lock) return SetLightingEnableInternal(device, enabled); }
        public static LightionProfile GetCurrentLightProfile(int device) { lock (_lock) return GetCurrentLightProfileInternal(device); }
        public static bool SetLightingEffectProfileID(int device, LightionProfile profile) { lock (_lock) return SetLightingEffectProfileIDInternal(device, profile); }
        public static int GetLightingMode(int device) { lock (_lock) return GetLightingModeInternal(device); }
        public static bool SetLightingMode(int device, int mode) { lock (_lock) return SetLightingModeInternal(device, mode); }
        public static int GetQuickLightingEffect(int device) { lock (_lock) return GetQuickLightingEffectInternal(device); }
        public static int GetQuickLightingEffectEnable(int device) { lock (_lock) return GetQuickLightingEffectEnableInternal(device); }
        public static bool SetQuickLightingEffectEnable(int device, bool enable) { lock (_lock) return SetQuickLightingEffectEnableInternal(device, enable); }
        public static bool SetQuickLightingEffect(int device, int index) { lock (_lock) return SetQuickLightingEffectInternal(device, index); }

        public static int GetTouchPadStatus() { lock (_lock) return GetTouchPadStatusInternal(); }
        public static int GetTrackpadStatus(int device) { lock (_lock) return GetTrackpadStatusInternal(device); }
        public static bool SetTouchPadStatus(int state) { lock (_lock) return SetTouchPadStatusInternal(state); }
        public static bool SetTrackpadStatus(int device, int state) { lock (_lock) return SetTrackpadStatusInternal(device, state); }
        public static int GetTouchpadVibrateState(int device) { lock (_lock) return GetTouchpadVibrateStateInternal(device); }
        public static bool SetTouchpadVibrateState(int device, int state) { lock (_lock) return SetTouchpadVibrateStateInternal(device, state); }

        public static bool SetDeviceDefault(int device) { lock (_lock) return SetDeviceDefaultInternal(device); }
        public static VERSION GetUSBVerify(int device) { lock (_lock) return GetUSBVerifyInternal(device); }
        public static string? GetDeviceSN(int device)
        {
            lock (_lock)
            {
                var ptr = GetDeviceSNInternal(device);
                return ptr == IntPtr.Zero ? null : Marshal.PtrToStringAnsi(ptr);
            }
        }
        public static bool SetDeviceSN(int device, string serialNumber) { lock (_lock) return SetDeviceSNInternal(device, serialNumber); }
        public static int GetMachineType() { lock (_lock) return GetMachineTypeInternal(); }

        public static LegionJoystickCurveProfile GetStickCustomCurve(int device) { lock (_lock) return GetStickCustomCurveInternal(device); }
        public static bool SetStickCustomCurve(int device, LegionJoystickCurveProfile curveProfile) { lock (_lock) return SetStickCustomCurveInternal(device, curveProfile); }
        public static int GetStickCustomDeadzone(int device) { lock (_lock) return GetStickCustomDeadzoneInternal(device); }
        public static bool SetStickCustomDeadzone(int device, int deadzone) { lock (_lock) return SetStickCustomDeadzoneInternal(device, deadzone); }
        public static int GetStickPresetCurve(int device) { lock (_lock) return GetStickPresetCurveInternal(device); }
        public static bool SetStickPresetCurve(int device, int preset) { lock (_lock) return SetStickPresetCurveInternal(device, preset); }
        public static int GetCurrentCurveType(int device) { lock (_lock) return GetCurrentCurveTypeInternal(device); }

        public static bool SetGyroSensorDataOnorOff(int device, int status) { lock (_lock) return SetGyroSensorDataOnorOffInternal(device, status); }
        public static int GetGyroSensorDataOnorOff(int device) { lock (_lock) return GetGyroSensorDataOnorOffInternal(device); }
        public static int GetGyroAxisReverse(int device) { lock (_lock) return GetGyroAxisReverseInternal(device); }
        public static bool SetGyroAxisReverse(int device, int reverseMask) { lock (_lock) return SetGyroAxisReverseInternal(device, reverseMask); }
        public static int GetGyroDeadzone(int device) { lock (_lock) return GetGyroDeadzoneInternal(device); }
        public static bool SetGyroDeadzone(int device, int value) { lock (_lock) return SetGyroDeadzoneInternal(device, value); }
        public static int GetGyroDeadzoneCompensate(int device) { lock (_lock) return GetGyroDeadzoneCompensateInternal(device); }
        public static bool SetGyroDeadzoneCompensate(int device, int value) { lock (_lock) return SetGyroDeadzoneCompensateInternal(device, value); }
        public static int GetGyroMappingTrigger(int device) { lock (_lock) return GetGyroMappingTriggerInternal(device); }
        public static bool SetGyroMappingTrigger(int device, int trigger) { lock (_lock) return SetGyroMappingTriggerInternal(device, trigger); }
        public static int GetGyroMappingType(int device) { lock (_lock) return GetGyroMappingTypeInternal(device); }
        public static bool SetGyroMappingType(int device, int type) { lock (_lock) return SetGyroMappingTypeInternal(device, type); }
        public static int GetGyroOutputMixer(int device) { lock (_lock) return GetGyroOutputMixerInternal(device); }
        public static bool SetGyroOutputMixer(int device, int mixer) { lock (_lock) return SetGyroOutputMixerInternal(device, mixer); }
        public static int GetGyroSensitivity(int device) { lock (_lock) return GetGyroSensitivityInternal(device); }
        public static bool SetGyroSensitivity(int device, int sensitivity) { lock (_lock) return SetGyroSensitivityInternal(device, sensitivity); }
        public static int GetGyroXYSensitivity(int device, int axis) { lock (_lock) return GetGyroXYSensitivityInternal(device, axis); }
        public static bool SetGyroXYSensitivity(int device, int axis, int sensitivity) { lock (_lock) return SetGyroXYSensitivityInternal(device, axis, sensitivity); }
        public static bool SetGyroEnabled(int device, int enabled) { lock (_lock) return SetGyroEnabledInternal(device, enabled); }

        public static LegionTriggerDeadzone GetTriggerDeadzoneAndMargin(int device) { lock (_lock) return GetTriggerDeadzoneAndMarginInternal(device); }
        public static bool SetTriggerDeadzoneAndMargin(int device, LegionTriggerDeadzone deadzone) { lock (_lock) return SetTriggerDeadzoneAndMarginInternal(device, deadzone); }

        public static int GetAutoSleepTime(int device) { lock (_lock) return GetAutoSleepTimeInternal(device); }
        public static bool SetAutoSleepTime(int device, int autoSleepTime) { lock (_lock) return SetAutoSleepTimeInternal(device, autoSleepTime); }

        public static int GetMouseCurrentDPI(int device) { lock (_lock) return GetMouseCurrentDPIInternal(device); }
        public static int GetMouseDPIProfileID(int device) { lock (_lock) return GetMouseDPIProfileIDInternal(device); }
        public static bool SetMouseDPIProfileID(int device, int profileId) { lock (_lock) return SetMouseDPIProfileIDInternal(device, profileId); }
        public static bool SetMouseDPI(int device, int dpi) { lock (_lock) return SetMouseDPIInternal(device, dpi); }

        public static int GetVibrationMode(int device) { lock (_lock) return GetVibrationModeInternal(device); }
        public static bool SetVibrationMode(int device, int mode) { lock (_lock) return SetVibrationModeInternal(device, mode); }
        public static int GetVibrationStatus(int device) { lock (_lock) return GetVibrationStatusInternal(device); }
        public static bool SetVibrationStatus(int device, int status) { lock (_lock) return SetVibrationStatusInternal(device, status); }
        public static int GetVibrateNotifyState(int device) { lock (_lock) return GetVibrateNotifyStateInternal(device); }
        public static bool SetVibrateNotifyState(int device, int state) { lock (_lock) return SetVibrateNotifyStateInternal(device, state); }
        public static bool SendVibrate(int which, byte strength) { lock (_lock) return SendVibrateInternal(which, strength); }

        public static int GetFPSMode(int device) { lock (_lock) return GetFPSModeInternal(device); }
        public static bool SetFPSMode(int device, int mode) { lock (_lock) return SetFPSModeInternal(device, mode); }
        public static int GetFPSModeStatus(int device) { lock (_lock) return GetFPSModeStatusInternal(device); }
        public static bool SetGamepadOnorOff(int device, int enabled) { lock (_lock) return SetGamepadOnorOffInternal(device, enabled); }

        public static int GetLegionLRAndMenuViewMode(int device) { lock (_lock) return GetLegionLRAndMenuViewModeInternal(device); }
        public static bool SetLegionLRAndMenuViewMode(int device, int mode) { lock (_lock) return SetLegionLRAndMenuViewModeInternal(device, mode); }
        public static int GetYButton(int device) { lock (_lock) return GetYButtonInternal(device); }
        public static bool SetYButton(int device, int value) { lock (_lock) return SetYButtonInternal(device, value); }
        public static bool SetYButton2(int device, int value) { lock (_lock) return SetYButton2Internal(device, value); }
        public static bool SetHandleMode(byte mode) { lock (_lock) return SetHandleModeInternal(mode); }
        public static bool SetMode(int mode) { lock (_lock) return SetModeInternal(mode); }

        public static bool StartGyroCalibration(int device) { lock (_lock) return StartGyroCalibrationInternal(device); }
        public static bool StopGyroCalibration(int device) { lock (_lock) return StopGyroCalibrationInternal(device); }
        public static bool StartStickCalibration(int device) { lock (_lock) return StartStickCalibrationInternal(device); }
        public static bool StopStickCalibration(int device) { lock (_lock) return StopStickCalibrationInternal(device); }
        public static bool StartTriggerCalibration(int device) { lock (_lock) return StartTriggerCalibrationInternal(device); }
        public static bool StopTriggerCalibration(int device) { lock (_lock) return StopTriggerCalibrationInternal(device); }
        public static bool StartStickTriggerCalibration(int device) { lock (_lock) return StartStickTriggerCalibrationInternal(device); }
        public static bool StopStickTriggerCalibration(int device) { lock (_lock) return StopStickTriggerCalibrationInternal(device); }
        public static bool RockerCalibration(int device, int state) { lock (_lock) return RockerCalibrationInternal(device, state); }
        public static bool RockerCalibrationEnd(int device) { lock (_lock) return RockerCalibrationEndInternal(device); }
        public static bool RockerCalibrationFinish(int device) { lock (_lock) return RockerCalibrationFinishInternal(device); }
        public static bool HandleFeelCalibration(int device, int state) { lock (_lock) return HandleFeelCalibrationInternal(device, state); }
        public static bool HandleFeelCalibrationEnd(int device) { lock (_lock) return HandleFeelCalibrationEndInternal(device); }
        public static bool HandleFeelCalibrationFinish(int device) { lock (_lock) return HandleFeelCalibrationFinishInternal(device); }
        public static bool FeelRockback(int device, int enabled) { lock (_lock) return FeelRockbackInternal(device, enabled); }

        public static bool SendButtonTest(int device, int button, int value) { lock (_lock) return SendButtonTestInternal(device, button, value); }
        public static bool SetTestInputMode(int device, int mode) { lock (_lock) return SetTestInputModeInternal(device, mode); }

        public static int GetUpdateState() { lock (_lock) return GetUpdateStateInternal(); }
        public static bool SendUpdataOTACmd(int cmd, int value) { lock (_lock) return SendUpdataOTACmdInternal(cmd, value); }

        public static bool SetFPSSwitchBackFun(FPSSwitchBackFunc callback) { lock (_lock) return SetFPSSwitchBackFunInternal(callback); }
        public static bool SetFindDeviceFunc(FindDeviceFunc callback) { lock (_lock) return SetFindDeviceFuncInternal(callback); }
        public static bool SetFinishCalibrationCallback(FinishCalibrationCallback callback) { lock (_lock) return SetFinishCalibrationCallbackInternal(callback); }
        public static bool SetLinkStateBackFunc(LinkStateBackFunc callback) { lock (_lock) return SetLinkStateBackFuncInternal(callback); }
        public static bool SetMouseWheelStatusBackFunc(MouseWheelStatusBackFunc callback) { lock (_lock) return SetMouseWheelStatusBackFuncInternal(callback); }
        public static bool SetPowerBackFunc(PowerBackFunc callback) { lock (_lock) return SetPowerBackFuncInternal(callback); }
        public static bool SetStickBackFunc(StickCallback callback) { lock (_lock) return SetStickBackFuncInternal(callback); }
        public static bool SetTerminateMacroRecordCallback(TerminateMacroRecordCallback callback) { lock (_lock) return SetTerminateMacroRecordCallbackInternal(callback); }
        public static bool SetTestButtonBackFunc(TestButtonBackFunc callback) { lock (_lock) return SetTestButtonBackFuncInternal(callback); }
        public static bool SetTouchPadBackFunc(TouchPadCallback callback) { lock (_lock) return SetTouchPadBackFuncInternal(callback); }
        public static bool SetTriggerBackFunc(TriggerCallback callback) { lock (_lock) return SetTriggerBackFuncInternal(callback); }
        public static bool SetUpdateBackFun(UpdateBackFunc callback) { lock (_lock) return SetUpdateBackFunInternal(callback); }

        #endregion

        #region Raw export coverage

        public static class Raw
        {
            private const string RawDllName = DllName;
            private static readonly object Sync = new object();
            private static IntPtr _module;
            private static readonly Dictionary<string, Delegate> Cache = new Dictionary<string, Delegate>(StringComparer.Ordinal);

            public static readonly string[] ExportNames =
            {
                "FreeKeyMapData",
                "FreeSapientiaUsb",
                "GetAllFPSKeyMap",
                "GetAllGamepadKeyMap",
                "GetAllMacroConfigData",
                "GetAutoSleepTime",
                "GetCurrentCurveType",
                "GetCurrentLightProfile",
                "GetDeviceSN",
                "GetFPSKMP",
                "GetFPSKMPButonValue",
                "GetFPSKeyMap",
                "GetFPSMode",
                "GetFPSModeStatus",
                "GetGamePadMode",
                "GetGamepadKeyMap",
                "GetGyroAxisReverse",
                "GetGyroDeadzone",
                "GetGyroDeadzoneCompensate",
                "GetGyroMappingTrigger",
                "GetGyroMappingType",
                "GetGyroMode",
                "GetGyroModeStatus",
                "GetGyroOutputMixer",
                "GetGyroSensitivity",
                "GetGyroSensorDataOnorOff",
                "GetGyroXYSensitivity",
                "GetLastErr",
                "GetLeftGyroStatus",
                "GetLegionLRAndMenuViewMode",
                "GetLightingMode",
                "GetLlightingEffectEnable",
                "GetMachineType",
                "GetMouseCurrentDPI",
                "GetMouseDPIList",
                "GetMouseDPIProfileID",
                "GetQuickLightingEffect",
                "GetQuickLightingEffectEnable",
                "GetRightGyroStatus",
                "GetStickCustomCurve",
                "GetStickCustomDeadzone",
                "GetStickPresetCurve",
                "GetTouchPadStatus",
                "GetTouchpadVibrateState",
                "GetTrackpadStatus",
                "GetTriggerDeadzoneAndMargin",
                "GetUpdateState",
                "GetVibrateNotifyState",
                "GetVibrationMode",
                "GetVibrationStatus",
                "GetYButton",
                "HandleFeelCalibration",
                "HandleFeelCalibrationEnd",
                "HandleFeelCalibrationFinish",
                "Init",
                "RockerCalibration",
                "RockerCalibrationEnd",
                "RockerCalibrationFinish",
                "SendButtonTest",
                "SendUpdataOTAcmd",
                "SetAutoSleepTime",
                "SetDeviceDefault",
                "SetDeviceSN",
                "SetFPSKMP",
                "SetFPSKeyMap",
                "SetFPSMode",
                "SetFPSSwitchBackFun",
                "SetFindDeviceFunc",
                "SetFinishCalibrationCallback",
                "SetGamePadMode",
                "SetGamepadKeyMap",
                "SetGamepadOnorOff",
                "SetGyroAxisReverse",
                "SetGyroDataBackFunc",
                "SetGyroDeadzone",
                "SetGyroDeadzoneCompensate",
                "SetGyroEnabled",
                "SetGyroMappingTrigger",
                "SetGyroMappingType",
                "SetGyroModeStatus",
                "SetGyroOutputMixer",
                "SetGyroSensitivity",
                "SetGyroSensorDataOnorOff",
                "SetGyroSensorStatusBackFunc",
                "SetGyroState",
                "SetGyroStateCbFunc",
                "SetGyroXYSensitivity",
                "SetLeftGyroStatus",
                "SetLegionLRAndMenuViewMode",
                "SetLightingEffectProfileID",
                "SetLightingEnable",
                "SetLightingMode",
                "SetLinkStateBackFunc",
                "SetMacroSubKey",
                "SetMouseDPI",
                "SetMouseDPIProfileID",
                "SetMouseWheelStatusBackFunc",
                "SetPowerBackFunc",
                "SetQuickLightingEffect",
                "SetQuickLightingEffectEnable",
                "SetRightGyroStatus",
                "SetStickBackFunc",
                "SetStickCustomCurve",
                "SetStickCustomDeadzone",
                "SetStickPresetCurve",
                "SetTerminateMacroRecordCallback",
                "SetTestButtonBackFunc",
                "SetTestInputMode",
                "SetTouchPadBackFunc",
                "SetTouchPadStatus",
                "SetTouchpadVibrateState",
                "SetTrackpadStatus",
                "SetTriggerBackFunc",
                "SetTriggerDeadzoneAndMargin",
                "SetUpdateBackFun",
                "SetVibrateNotifyState",
                "SetVibrationMode",
                "SetVibrationStatus",
                "SetYButton",
                "SetYButton2",
                "SimulatePhysicalKeys",
                "StartGyroCalibration",
                "StartMacroRecord",
                "StartStickCalibration",
                "StartStickTriggerCalibration",
                "StartTriggerCalibration",
                "StopGyroCalibration",
                "StopMacroRecord",
                "StopStickCalibration",
                "StopStickTriggerCalibration",
                "StopTriggerCalibration",
                "dllInit",
                "feelRockback",
                "getUSBVerify",
                "sendVibrate",
                "setHandleMode",
                "setMode"
            };

            public static IntPtr ModuleHandle
            {
                get
                {
                    lock (Sync)
                    {
                        if (_module == IntPtr.Zero)
                        {
                            _module = NativeLibrary.Load(RawDllName);
                        }
                        return _module;
                    }
                }
            }

            public static IntPtr GetExport(string name) => NativeLibrary.GetExport(ModuleHandle, name);

            public static TDelegate Bind<TDelegate>(string exportName) where TDelegate : Delegate
            {
                lock (Sync)
                {
                    if (Cache.TryGetValue(exportName, out var del))
                        return (TDelegate)del;

                    IntPtr ptr = GetExport(exportName);
                    TDelegate typed = Marshal.GetDelegateForFunctionPointer<TDelegate>(ptr);
                    Cache[exportName] = typed;
                    return typed;
                }
            }
        }

        #endregion
    }
}