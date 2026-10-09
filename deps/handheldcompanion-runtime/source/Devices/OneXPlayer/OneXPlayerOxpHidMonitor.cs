using hidapi;
using hidapi.Native;
using System;
using System.Threading;
using System.Threading.Tasks;

namespace HandheldCompanion.Devices;

public enum OxpHidInitProfile
{
    None = 0,
    X1 = 1,
    X1Mini = 2,
    Apex = 3,
}

internal sealed class OneXPlayerOxpHidMonitor : IDisposable
{
    public const ushort VID = 0x1A86;
    public const ushort PID = 0xFE00;
    public const int InterfaceNumber = 0x02;
    public const ushort InputReportLength = 64;

    private const byte FrameMarker = 0x3F;
    private const byte ButtonCommandId = 0xB2;
    private const byte StatusCommandId = 0xB8;
    private const byte VibrationCommandId = 0xB3;

    private readonly HidDevice _hidDevice = new(VID, PID, InputReportLength, -1);
    private readonly bool[] _buttonStates = new bool[0x25];
    private CancellationTokenSource? _cancellationTokenSource;
    private Task? _readTask;
    private OxpHidInitProfile _initProfile = OxpHidInitProfile.None;
    private bool _reinitializeRequested;

    public event Action<byte, bool>? ButtonChanged;

    public bool IsOpen => _hidDevice.IsDeviceValid;

    public bool Open(OxpHidInitProfile initProfile)
    {
        IntPtr devEnum = HidApiNative.hid_enumerate(VID, PID);
        IntPtr deviceInfo = devEnum;

        try
        {
            while (deviceInfo != IntPtr.Zero)
            {
                HidDeviceInfo hidDeviceInfo = new(deviceInfo);
                if (hidDeviceInfo.InterfaceNumber == InterfaceNumber)
                {
                    if (_hidDevice.OpenDevice(hidDeviceInfo.Path))
                    {
                        _initProfile = initProfile;
                        InitializeProfile();

                        _cancellationTokenSource = new CancellationTokenSource();
                        _readTask = Task.Run(() => ReadLoop(_cancellationTokenSource.Token));
                        return true;
                    }
                }

                deviceInfo = hidDeviceInfo.NextDevicePtr;
            }
        }
        finally
        {
            HidApiNative.hid_free_enumeration(devEnum);
        }

        return false;
    }

    public void Close()
    {
        if (_cancellationTokenSource is not null)
        {
            _cancellationTokenSource.Cancel();
            _cancellationTokenSource.Dispose();
            _cancellationTokenSource = null;
        }

        try
        {
            _readTask?.Wait(250);
        }
        catch
        { }

        _readTask = null;
        _reinitializeRequested = false;
        _initProfile = OxpHidInitProfile.None;

        Array.Clear(_buttonStates, 0, _buttonStates.Length);
        _hidDevice.Close();
    }

    public void Dispose()
    {
        Close();
    }

    private void ReadLoop(CancellationToken cancellationToken)
    {
        byte[] report = new byte[InputReportLength];

        while (!cancellationToken.IsCancellationRequested)
        {
            int bytesRead;
            try
            {
                bytesRead = _hidDevice.Read(report, 10);
            }
            catch
            {
                break;
            }

            if (bytesRead < InputReportLength)
                continue;

            if (_reinitializeRequested)
            {
                InitializeProfile();
                _reinitializeRequested = false;
            }

            if (report[1] != FrameMarker || report[InputReportLength - 2] != FrameMarker)
                continue;

            if (report[0] == StatusCommandId)
            {
                HandleStatusReport(report);
                continue;
            }

            if (report[0] != ButtonCommandId)
                continue;

            byte buttonId = report[6];
            if (buttonId >= _buttonStates.Length)
                continue;

            bool pressed = report[12] == 0x01;
            if (_buttonStates[buttonId] == pressed)
                continue;

            _buttonStates[buttonId] = pressed;
            ButtonChanged?.Invoke(buttonId, pressed);
        }
    }

    private void HandleStatusReport(byte[] report)
    {
        if (_initProfile != OxpHidInitProfile.X1Mini)
            return;

        if (report[3] == 0xFE)
            _reinitializeRequested = true;
    }

    private void InitializeProfile()
    {
        switch (_initProfile)
        {
            case OxpHidInitProfile.X1:
                WriteCommand(0xB4, BuildRemapPage1(0x01));
                Thread.Sleep(50);
                WriteCommand(0xB4, BuildRemapPage2(0x01, 0x67, 0x66));
                break;
            case OxpHidInitProfile.X1Mini:
                WriteCommand(0xB4, BuildRemapPage1(0x01));
                Thread.Sleep(50);
                WriteCommand(0xB4, BuildRemapPage2(0x01, 0x67, 0x66));
                Thread.Sleep(50);
                WriteCommand(VibrationCommandId, BuildMotorLevelPayload(0x05));
                break;
            case OxpHidInitProfile.Apex:
                WriteCommand(0xB4, BuildRemapPage1(0x01));
                Thread.Sleep(100);
                WriteCommand(0xB4, BuildRemapPage2(0x01, 0x67, 0x66));
                Thread.Sleep(100);
                WriteCommand(0xB2, [0x01, 0x1F, 0x40, 0x03, 0x02, 0x03, 0x00, 0x00, 0x00, 0x01]);
                Thread.Sleep(200);
                WriteCommand(0xB2, [0x00, 0x01, 0x02]);
                break;
        }
    }

    private static byte[] BuildMotorLevelPayload(byte level)
    {
        return [0x01, 0x05, level];
    }

    private void InitializeRemap()
    {
        WriteCommand(0xB2, [0x01, 0x1F, 0x40, 0x03, 0x02, 0x03, 0x00, 0x00, 0x00, 0x01]);
        Thread.Sleep(50);
        WriteCommand(0xB4, BuildRemapPage1(0x01));
        Thread.Sleep(50);
        WriteCommand(0xB4, BuildRemapPage2(0x01, 0x66, 0x67));
        Thread.Sleep(50);
        WriteCommand(0xB2, [0x00, 0x01, 0x02]);
    }

    private void WriteCommand(byte commandId, byte[] payload)
    {
        _hidDevice.Write(BuildCommand(commandId, payload));
    }

    private static byte[] BuildCommand(byte commandId, byte[] payload, byte index = 0x01)
    {
        byte[] command = new byte[InputReportLength];
        command[0] = commandId;
        command[1] = FrameMarker;
        command[2] = index;

        int count = Math.Min(payload.Length, InputReportLength - 5);
        Array.Copy(payload, 0, command, 3, count);

        command[InputReportLength - 2] = FrameMarker;
        command[InputReportLength - 1] = commandId;
        return command;
    }

    private static byte[] BuildRemapPage1(byte preset)
    {
        return
        [
            0x02, 0x38, 0x20, 0x01, preset,
            0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
            0x02, 0x01, 0x02, 0x00, 0x00, 0x00,
            0x03, 0x01, 0x03, 0x00, 0x00, 0x00,
            0x04, 0x01, 0x04, 0x00, 0x00, 0x00,
            0x05, 0x01, 0x05, 0x00, 0x00, 0x00,
            0x06, 0x01, 0x06, 0x00, 0x00, 0x00,
            0x07, 0x01, 0x07, 0x00, 0x00, 0x00,
            0x08, 0x01, 0x08, 0x00, 0x00, 0x00,
            0x09, 0x01, 0x09, 0x00, 0x00, 0x00,
        ];
    }

    private static byte[] BuildRemapPage2(byte preset, byte m1KeyCode, byte m2KeyCode)
    {
        return
        [
            0x02, 0x38, 0x20, 0x02, preset,
            0x0A, 0x01, 0x0A, 0x00, 0x00, 0x00,
            0x0B, 0x01, 0x0B, 0x00, 0x00, 0x00,
            0x0C, 0x01, 0x0C, 0x00, 0x00, 0x00,
            0x0D, 0x01, 0x0D, 0x00, 0x00, 0x00,
            0x0E, 0x01, 0x0E, 0x00, 0x00, 0x00,
            0x0F, 0x01, 0x0F, 0x00, 0x00, 0x00,
            0x10, 0x01, 0x10, 0x00, 0x00, 0x00,
            0x22, 0x02, 0x01, m1KeyCode, 0x00, 0x00,
            0x23, 0x02, 0x01, m2KeyCode, 0x00, 0x00,
        ];
    }
}
