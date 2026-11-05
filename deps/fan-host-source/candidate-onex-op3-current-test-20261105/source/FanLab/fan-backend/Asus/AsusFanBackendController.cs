using System;
using System.Runtime.InteropServices;

namespace YeManFanBackend.Asus;

/**
 * HC-SLIM-01 R3 §3/§4.2：ASUS ACPI 风扇叶组件（传输/封包/收据）。
 *
 * 来源：`HandheldCompanion/Devices/ASUS/AsusACPI.cs`（按函数/必要类型抽取，不继承整类）。
 * 保留：kernel32 传输、ACPI 路径/控制码、handle 锁、返回长度、异常分类、scope 归属与
 * CPU/GPU/Mid 次序；`CallMethodObserved`/`DeviceSetObserved`/`DeviceGetBufferObserved` 的
 * 封包与旧返回值语义逐字保留。
 * 移除（§3 明令不迁移）：`AsusMode`/`AsusGPU`/GPU 模式（XG/Eco）、电池（BatteryLimit）、
 * PPT/性能模式常量、`DeviceInit`/`DeviceWatchDog`/`DeviceGet`/`IsSupported`/`SetFanSpeed`/
 * `SetFanRange`/`IsXGConnected`/`IsInvalidCurve`/`IsEmptyCurve`/`FixFanCurve` 及未使用的
 * `CreateEvent`/`WaitForSingleObject` P/Invoke。
 * 唯一替换：HC 的 `LogManager.LogError` 改为 Host 注入的 `DiagnosticSink`（不引 Shared 全库）。
 */
public static class AsusACPI
{
    /**
     * Host 注入的诊断接收器（替代 HC 的 `LogManager`）。为 null 时静默——
     * 诊断失败与控制调用结果无关，不得影响调用返回值。
     */
    public static Action<string>? DiagnosticSink { get; set; }

    private static void Report(string message)
    {
        try
        {
            DiagnosticSink?.Invoke(message);
        }
        catch
        {
            // 诊断接收器自身异常与控制调用结果无关：不抛出、不改写调用返回值。
        }
    }

    private sealed class KernelTransport : IAsusAcpiTransport
    {
        public bool IsHandleValid(IntPtr handle) => handle != IntPtr.Zero && handle != new IntPtr(-1);

        public bool TryOpen(out IntPtr handle, out int win32Error)
        {
            handle = CreateFile(
                FILE_NAME,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                IntPtr.Zero,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                IntPtr.Zero
            );
            win32Error = IsHandleValid(handle) ? 0 : Marshal.GetLastWin32Error();
            return IsHandleValid(handle);
        }

        public bool Control(IntPtr handle, uint ioControlCode, byte[] inBuffer, byte[] outBuffer, out uint bytesReturned, out int win32Error)
        {
            bytesReturned = 0;
            bool success = DeviceIoControl(
                handle,
                ioControlCode,
                inBuffer,
                (uint)inBuffer.Length,
                outBuffer,
                (uint)outBuffer.Length,
                ref bytesReturned,
                IntPtr.Zero
            );
            win32Error = success ? 0 : Marshal.GetLastWin32Error();
            return success;
        }

        public void Close(IntPtr handle) => CloseHandle(handle);
    }

    private static IAsusAcpiTransport transport = new KernelTransport();

    /// <summary>
    /// 无硬件联调注入缝：替换后只改变 OS/驱动边界的行为，封包/曲线变换/钳制逻辑不变。
    /// 传 null 恢复真实 kernel32 传输。
    /// </summary>
    public static void SetTransportForInjection(IAsusAcpiTransport? injected) =>
        transport = injected ?? new KernelTransport();

    public static IAsusAcpiTransport CurrentTransport => transport;

    const string FILE_NAME = @"\\.\\ATKACPI";
    const uint CONTROL_CODE = 0x0022240C;

    const uint DSTS = 0x53545344;
    const uint DEVS = 0x53564544;
    const uint INIT = 0x54494E49;
    const uint WDOG = 0x474F4457;

    public const uint DevsCPUFan = 0x00110022;
    public const uint DevsGPUFan = 0x00110023;

    public const uint DevsCPUFanCurve = 0x00110024;
    public const uint DevsGPUFanCurve = 0x00110025;
    public const uint DevsMidFanCurve = 0x00110032;

    public const uint CPU_Fan = 0x00110013;
    public const uint GPU_Fan = 0x00110014;
    public const uint Mid_Fan = 0x00110031;

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateFile(
        string lpFileName,
        uint dwDesiredAccess,
        uint dwShareMode,
        IntPtr lpSecurityAttributes,
        uint dwCreationDisposition,
        uint dwFlagsAndAttributes,
        IntPtr hTemplateFile
    );

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool DeviceIoControl(
        IntPtr hDevice,
        uint dwIoControlCode,
        byte[] lpInBuffer,
        uint nInBufferSize,
        byte[] lpOutBuffer,
        uint nOutBufferSize,
        ref uint lpBytesReturned,
        IntPtr lpOverlapped
    );

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr hObject);

    private const uint GENERIC_READ = 0x80000000;
    private const uint GENERIC_WRITE = 0x40000000;
    private const uint OPEN_EXISTING = 3;
    private const uint FILE_ATTRIBUTE_NORMAL = 0x80;
    private const uint FILE_SHARE_READ = 1;
    private const uint FILE_SHARE_WRITE = 2;

    private static readonly IntPtr INVALID_HANDLE_VALUE = new(-1);
    private static readonly object handleLock = new();
    private static IntPtr handle = INVALID_HANDLE_VALUE;

    public static bool IsOpen => transport.IsHandleValid(handle);

    /**
     * 把结构化观察接到原调用边界。
     * 保留原调用次序与“句柄无效即返回（零缓冲）”的旧行为；收据如实区分
     * 未发起 / 已发起失败 / 异常不确定 / 短响应 / 完整响应，厂商语义保持 unknown。
     */
    private static AsusAcpiCallReceipt Control(
        uint dwIoControlCode,
        byte[] lpInBuffer,
        byte[] lpOutBuffer,
        string callKind,
        AsusFan? fan,
        uint deviceId)
    {
        var receipt = new AsusAcpiCallReceipt(callKind, fan, dwIoControlCode, deviceId, lpInBuffer.Length, lpOutBuffer.Length);
        lock (handleLock)
        {
            var activeTransport = transport;
            if (!activeTransport.IsHandleValid(handle))
            {
                // 句柄无效 = 未发起（不是成功，也不是“零结果”）。
                receipt.MarkNotInitiated();
            }
            else
            {
                try
                {
                    bool success = activeTransport.Control(
                        handle,
                        dwIoControlCode,
                        lpInBuffer,
                        lpOutBuffer,
                        out uint lpBytesReturned,
                        out int win32Error);
                    receipt.RecordIo(success, lpBytesReturned, win32Error, lpOutBuffer);
                }
                catch (Exception ex)
                {
                    // 无法证明是否已发起：不臆测。
                    receipt.RecordException(ex);
                }
            }
        }
        AsusAcpiObservations.Publish(receipt);
        return receipt;
    }

    public static bool Open()
    {
        lock (handleLock)
        {
            if (IsOpen)
                return true;

            transport.TryOpen(out handle, out _);

            if (!IsOpen)
            {
                Report("Can't connect to Asus ACPI");
                return false;
            }

            return true;
        }
    }

    public static void Close()
    {
        lock (handleLock)
        {
            if (IsOpen)
                transport.Close(handle);

            handle = INVALID_HANDLE_VALUE;
        }
    }

    public static byte[] CallMethodObserved(uint MethodID, byte[] args, out AsusAcpiCallReceipt receipt) =>
        CallMethodObserved(MethodID, args, MethodKind(MethodID), null, 0, out receipt);

    internal static byte[] CallMethodObserved(
        uint MethodID,
        byte[] args,
        string callKind,
        AsusFan? fan,
        uint deviceId,
        out AsusAcpiCallReceipt receipt)
    {
        byte[] acpiBuf = new byte[8 + args.Length];
        byte[] outBuffer = new byte[16];

        BitConverter.GetBytes(MethodID).CopyTo(acpiBuf, 0);
        BitConverter.GetBytes((uint)args.Length).CopyTo(acpiBuf, 4);
        Array.Copy(args, 0, acpiBuf, 8, args.Length);

        receipt = Control(CONTROL_CODE, acpiBuf, outBuffer, callKind, fan, deviceId);

        return outBuffer;
    }

    private static string MethodKind(uint methodId) => methodId switch
    {
        DEVS => "DEVS",
        DSTS => "DSTS",
        INIT => "INIT",
        WDOG => "WDOG",
        _ => "Method"
    };

    private static AsusFan? FanForDeviceId(uint deviceId) => deviceId switch
    {
        DevsCPUFanCurve or DevsCPUFan => AsusFan.CPU,
        DevsGPUFanCurve or DevsGPUFan => AsusFan.GPU,
        DevsMidFanCurve or Mid_Fan => AsusFan.Mid,
        _ => null
    };

    public static int DeviceSetObserved(uint DeviceID, byte[] Params, string logName, out AsusAcpiCallReceipt receipt)
    {
        byte[] args = new byte[4 + Params.Length];
        BitConverter.GetBytes(DeviceID).CopyTo(args, 0);
        Params.CopyTo(args, 4);

        // 旧返回值语义保持不变（含句柄无效时的零缓冲解析）；真实情况由 receipt 给出。
        byte[] status = CallMethodObserved(DEVS, args, logName ?? "DeviceSet", FanForDeviceId(DeviceID), DeviceID, out receipt);
        return BitConverter.ToInt32(status, 0);
    }

    public static byte[] DeviceGetBufferObserved(uint DeviceID, uint Status, out AsusAcpiCallReceipt receipt)
    {
        byte[] args = new byte[8];
        BitConverter.GetBytes(DeviceID).CopyTo(args, 0);
        BitConverter.GetBytes(Status).CopyTo(args, 4);

        return CallMethodObserved(DSTS, args, "DeviceGetBuffer", FanForDeviceId(DeviceID), DeviceID, out receipt);
    }

    public static int SetFanCurveObserved(AsusFan device, byte[] curve, out AsusAcpiCallReceipt receipt)
    {
        if (curve.Length != 16)
        {
            receipt = new AsusAcpiCallReceipt("SetFanCurve", device, DEVS, 0, 0, 16);
            receipt.MarkNotInitiated();
            AsusAcpiObservations.Publish(receipt);
            return -1;
        }

        int fanScale = 100;

        // it seems to be a bug, when some old model's bios can go nuts if fan is set to 100%
        for (int i = 8; i < curve.Length; i++)
            curve[i] = (byte)(Math.Max((byte)0, Math.Min((byte)99, curve[i])) * fanScale / 100);

        // 复用真实封包路径（DeviceSet → DEVS + 参数内设备 ID/曲线字节），并按通道分别留收据。
        switch (device)
        {
            case AsusFan.GPU:
                return DeviceSetObserved(DevsGPUFanCurve, curve, "FanGPU", out receipt);
            case AsusFan.Mid:
                return DeviceSetObserved(DevsMidFanCurve, curve, "FanMid", out receipt);
            default:
                return DeviceSetObserved(DevsCPUFanCurve, curve, "FanCPU", out receipt);
        }
    }

    public static byte[] GetFanCurveObserved(AsusFan device, int mode, out AsusAcpiCallReceipt receipt)
    {
        uint fan_mode;

        // because it's asus, and modes are swapped here
        switch (mode)
        {
            case 1: fan_mode = 2; break;
            case 2: fan_mode = 1; break;
            default: fan_mode = 0; break;
        }

        switch (device)
        {
            case AsusFan.GPU:
                return DeviceGetBufferObserved(DevsGPUFanCurve, fan_mode, out receipt);
            case AsusFan.Mid:
                return DeviceGetBufferObserved(DevsMidFanCurve, fan_mode, out receipt);
            default:
                return DeviceGetBufferObserved(DevsCPUFanCurve, fan_mode, out receipt);
        }
    }
}
