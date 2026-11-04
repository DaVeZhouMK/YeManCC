using System;

namespace YeManFanBackend.Asus;

/**
 * HC-SLIM-01 R3 §3：ASUS 风扇通道枚举。
 *
 * 只保留曲线读写实际使用的三条通道（CPU/GPU/Mid）；**不迁移** XGM
 * （XG Mobile 外接坞，属 §3 明令不迁移的 GPU 模式/无关控制命令范畴）。
 * 数值与 HC 原 `AsusFan` 逐字一致，便于同输入对拍。
 */
public enum AsusFan
{
    CPU = 0,
    GPU = 1,
    Mid = 2,
}

/**
 * HC-SLIM-01 R3 §3：ACPI 传输边界（OS/驱动边界的唯一注入点）。
 * 默认实现是真实 kernel32（CreateFile/DeviceIoControl/CloseHandle）；无硬件联调注入替身。
 * 与 HC 原接口逐字一致。
 */
public interface IAsusAcpiTransport
{
    bool IsHandleValid(IntPtr handle);
    /// <summary>打开 ACPI 句柄；失败返回 false 并给出 Win32 错误码（handle 未定义）。</summary>
    bool TryOpen(out IntPtr handle, out int win32Error);
    bool Control(IntPtr handle, uint ioControlCode, byte[] inBuffer, byte[] outBuffer, out uint bytesReturned, out int win32Error);
    void Close(IntPtr handle);
}
