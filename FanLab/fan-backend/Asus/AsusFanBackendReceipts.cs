using System;
using System.Collections.Generic;
using System.Threading;

namespace YeManFanBackend.Asus;

/**
 * HC-SLIM-01 R3 §3/§4.2：ASUS 风扇叶组件的结构化传输观察。
 *
 * 来源：`HandheldCompanion/Devices/ASUS/AsusAcpiObservations.cs`（逐字迁移，仅改命名空间）。
 * 规则保持不变：
 * - 观察接在 ACPI 调用边界上，复用真实封包函数、曲线变换与钳制逻辑；
 * - 输入长度、输出容量、有效返回长度**分开**记录；
 * - 句柄无效 = **未发起**，不是成功；
 * - 异常时若无法证明是否发起则不臆测（`InitiatedUncertain`）；
 * - 厂商语义未知继续标 unknown，不判成功；
 * - 不允许为了取回执额外发送硬件写入；
 * - 调用可归入**操作作用域**（`BeginScope`）——Host 只消费本次操作的逐扇收据，
 *   不得用全局队列近似归属（历史失败不得污染本次成功，本次失败也不得因记录被取走而漏判）。
 */

/// <summary>一次 ACPI 传输的分类结果。</summary>
public enum AsusAcpiTransportStatus
{
    /// <summary>句柄无效：调用**未发起**（不是成功，也不得把零缓冲解析成结果）。</summary>
    NotInitiatedHandleInvalid,
    /// <summary>已发起但 I/O 失败（Win32 错误码见记录）。</summary>
    InitiatedFailed,
    /// <summary>传输调用抛异常：**无法证明**是否已发起。</summary>
    IoException,
    /// <summary>传输成功但返回长度不足（语义 unknown）。</summary>
    TransportedShort,
    /// <summary>传输成功且返回长度达到输出容量（厂商语义仍标 unknown）。</summary>
    TransportedComplete
}

/// <summary>厂商语义判定（未核实的一律 unknown，不得当成成功）。</summary>
public enum AsusAcpiVendorSemantics
{
    Unknown = 0
}

/// <summary>
/// 一次 AsusACPI 调用的传输收据。**每次调用各自留结果**，不得用最后一次覆盖部分失败。
/// </summary>
public sealed class AsusAcpiCallReceipt
{
    public AsusAcpiCallReceipt(
        string callKind,
        AsusFan? fan,
        uint methodId,
        uint deviceId,
        int inputLength,
        int outputCapacity)
    {
        CallKind = callKind;
        Fan = fan;
        MethodId = methodId;
        DeviceId = deviceId;
        InputLength = inputLength;
        OutputCapacity = outputCapacity;
        StartedMonoMs = Environment.TickCount64;
    }

    /// <summary>调用种类（SetFanCurve / GetFanCurve / DeviceSet / DeviceGetBuffer / DEVS / DSTS / …）。</summary>
    public string CallKind { get; }
    /// <summary>本调用所属的操作作用域 ID（无作用域时为 null；Host 只消费本次操作的收据）。</summary>
    public string? ScopeId { get; internal set; }
    /// <summary>本调用归属的风扇通道（CPU/GPU/Mid）；不适用时为 null。</summary>
    public AsusFan? Fan { get; }
    /// <summary>DEVS / DSTS / INIT / WDOG 方法 ID（真实封包值）。</summary>
    public uint MethodId { get; }
    /// <summary>参数中的设备 ID（例如 DevsCPUFanCurve=0x00110024）；不适用时为 0。</summary>
    public uint DeviceId { get; }
    /// <summary>**输入长度**（送入 DeviceIoControl 的字节数）。</summary>
    public int InputLength { get; }
    /// <summary>**输出容量**（调用方提供的缓冲区大小）。</summary>
    public int OutputCapacity { get; }
    /// <summary>**有效返回长度**（lpBytesReturned，且不超过输出容量）。</summary>
    public int ValidReturnedLength { get; private set; }
    public bool Initiated { get; private set; }
    /// <summary>异常路径：无法证明是否已发起（不得当成未发起，也不得当成已发起）。</summary>
    public bool InitiatedUncertain { get; private set; }
    public bool IoReturned { get; private set; }
    public bool IoSuccess { get; private set; }
    public int? Win32Error { get; private set; }
    public string? ExceptionType { get; private set; }
    public string? ExceptionMessage { get; private set; }
    public AsusAcpiTransportStatus Status { get; private set; } = AsusAcpiTransportStatus.NotInitiatedHandleInvalid;
    public AsusAcpiVendorSemantics VendorSemantics { get; } = AsusAcpiVendorSemantics.Unknown;
    /// <summary>原始输出（仅取有效范围 0..min(输出容量, 有效返回长度)）；I/O 失败或未发起时为空。</summary>
    public byte[] RawOutput { get; private set; } = Array.Empty<byte>();
    public long StartedMonoMs { get; }
    public long? EndedMonoMs { get; private set; }

    internal void MarkNotInitiated()
    {
        Initiated = false;
        IoReturned = false;
        IoSuccess = false;
        Status = AsusAcpiTransportStatus.NotInitiatedHandleInvalid;
        RawOutput = Array.Empty<byte>();
        ValidReturnedLength = 0;
        EndedMonoMs = Environment.TickCount64;
    }

    internal void RecordIo(bool success, uint bytesReturned, int win32Error, byte[] outBuffer)
    {
        Initiated = true;
        IoReturned = true;
        IoSuccess = success;
        Win32Error = success ? null : win32Error;
        EndedMonoMs = Environment.TickCount64;

        if (!success)
        {
            Status = AsusAcpiTransportStatus.InitiatedFailed;
            RawOutput = Array.Empty<byte>();
            ValidReturnedLength = 0;
            return;
        }

        int capacity = Math.Min(outBuffer.Length, int.MaxValue);
        int valid = (int)Math.Min((uint)capacity, bytesReturned);
        if (valid < 0) valid = 0;
        ValidReturnedLength = valid;
        RawOutput = valid > 0 ? outBuffer[..valid] : Array.Empty<byte>();
        Status = valid >= outBuffer.Length
            ? AsusAcpiTransportStatus.TransportedComplete
            : AsusAcpiTransportStatus.TransportedShort;
    }

    internal void RecordException(Exception exception)
    {
        InitiatedUncertain = true;
        IoReturned = false;
        IoSuccess = false;
        ExceptionType = exception.GetType().Name;
        ExceptionMessage = exception.Message;
        Status = AsusAcpiTransportStatus.IoException;
        RawOutput = Array.Empty<byte>();
        ValidReturnedLength = 0;
        EndedMonoMs = Environment.TickCount64;
    }

    public string Describe() =>
        $"kind={CallKind} scope={ScopeId ?? "-"} fan={(Fan?.ToString() ?? "-")} method=0x{MethodId:X8} device=0x{DeviceId:X8} " +
        $"in={InputLength} capacity={OutputCapacity} returned={ValidReturnedLength} status={Status} " +
        $"initiated={Initiated} uncertain={InitiatedUncertain} ioReturned={IoReturned} ok={IoSuccess} " +
        $"win32={(Win32Error?.ToString() ?? "-")} semantics={VendorSemantics}" +
        (ExceptionType is null ? string.Empty : $" exception={ExceptionType}:{ExceptionMessage}");
}

/**
 * 一次操作的写入作用域。调用方（Host/Restore/Enable 链）用 `BeginScope` 打开，
 * 本次调用链上产生的收据自动归入本作用域；Host **只消费本作用域**的收据判断本次操作。
 *
 * 生命周期规则：
 * - 作用域由**操作**的结束（Dispose）关闭，不按线程身份分类；
 * - 结束后的迟到收据**不得静默丢失**：进入 `LateReceipts` 并计入 `LateReceiptCount`，
 *   与已消费收据分开报告（迟到收据不属于本操作的判定）；
 * - `disposed` 会真正置真，重复 Dispose 幂等。
 */
public sealed class AsusAcpiCallScope : IDisposable
{
    private readonly List<AsusAcpiCallReceipt> receipts = new();
    private readonly List<AsusAcpiCallReceipt> lateReceipts = new();
    private readonly object sync = new();
    private bool disposed;

    internal AsusAcpiCallScope(string scopeKind, string scopeId, AsusAcpiCallScope? parent)
    {
        ScopeKind = scopeKind;
        ScopeId = scopeId;
        Parent = parent;
        StartedMonoMs = Environment.TickCount64;
    }

    public string ScopeKind { get; }
    public string ScopeId { get; }
    internal AsusAcpiCallScope? Parent { get; }
    public long StartedMonoMs { get; }
    public bool IsClosed { get { lock (sync) return disposed; } }
    public long EndedMonoMs { get; private set; }

    internal void Accept(AsusAcpiCallReceipt receipt)
    {
        lock (sync)
        {
            if (disposed)
            {
                // 结束后迟到：如实留存，不悄悄丢弃，也不混入本操作的判定收据。
                lateReceipts.Add(receipt);
                return;
            }
            receipts.Add(receipt);
        }
    }

    /// <summary>本作用域已收到的收据数（不清空；不含迟到收据）。</summary>
    public int Count
    {
        get { lock (sync) return receipts.Count; }
    }

    /// <summary>作用域结束后的迟到收据数（单独报告）。</summary>
    public int LateReceiptCount
    {
        get { lock (sync) return lateReceipts.Count; }
    }

    /// <summary>取走本作用域的收据（**只属于本操作**；不清空全局诊断队列，不含迟到收据）。</summary>
    public AsusAcpiCallReceipt[] TakeReceipts()
    {
        lock (sync)
        {
            var copy = receipts.ToArray();
            receipts.Clear();
            return copy;
        }
    }

    public AsusAcpiCallReceipt[] PeekReceipts()
    {
        lock (sync) return receipts.ToArray();
    }

    /// <summary>读取结束后迟到收据（诊断用；不参与本操作判定）。</summary>
    public AsusAcpiCallReceipt[] PeekLateReceipts()
    {
        lock (sync) return lateReceipts.ToArray();
    }

    public void Dispose()
    {
        lock (sync)
        {
            if (disposed) return;      // 幂等
            disposed = true;
            EndedMonoMs = Environment.TickCount64;
        }
        AsusAcpiObservations.EndScope(this);
    }

    public string Describe() =>
        $"scope={ScopeKind}/{ScopeId} receipts={Count} late={LateReceiptCount} closed={IsClosed} " +
        $"startedMs={StartedMonoMs} endedMs={(EndedMonoMs == 0 ? "-" : EndedMonoMs.ToString())}";
}

/**
 * 收据注册表：全局队列（诊断/兼容，Host 不用于操作归属）+ 执行上下文作用域（操作归属）。
 * 发布失败不得影响控制调用。
 */
public static class AsusAcpiObservations
{
    public const int ApiVersion = 3;
    private const int MaxReceipts = 128;
    private static readonly object sync = new();
    private static readonly List<AsusAcpiCallReceipt> receipts = new();

    /**
     * 作用域在执行上下文内流动：
     * - AsyncLocal 随 ExecutionContext 传播，因此 `await` 续体与 `Task.Run` 等子工作**可能**看到同一作用域；
     * - `new Thread`/线程池上预先存在且未携带该上下文的工作**不会**看到；
     * - 因此归属按**操作的开始/结束**定义（BeginScope/Dispose），不按线程身份分类；
     *   作用域结束后发布的收据进入 `LateReceiptCount`，单独报告而不静默丢弃。
     */
    private static readonly AsyncLocal<AsusAcpiCallScope?> currentScope = new();
    private static int scopeSequence;

    /// <summary>打开一个操作作用域；调用方必须 Dispose（可嵌套，内层覆盖外层）。</summary>
    public static AsusAcpiCallScope BeginScope(string scopeKind)
    {
        // 直接使用 Interlocked.Increment 的返回值铸号，避免并发下“递增后重读”产生重复标识。
        var sequence = Interlocked.Increment(ref scopeSequence);
        var scope = new AsusAcpiCallScope(
            scopeKind ?? "unspecified",
            $"scope-{sequence}",
            currentScope.Value);
        currentScope.Value = scope;
        return scope;
    }

    internal static void EndScope(AsusAcpiCallScope scope)
    {
        if (ReferenceEquals(currentScope.Value, scope))
            currentScope.Value = scope.Parent;
    }

    /// <summary>当前执行上下文的作用域（无则 null）；诊断用。</summary>
    public static AsusAcpiCallScope? CurrentScope => currentScope.Value;

    internal static void Publish(AsusAcpiCallReceipt receipt)
    {
        try
        {
            var scope = currentScope.Value;
            if (scope is not null)
            {
                receipt.ScopeId = scope.ScopeId;
                scope.Accept(receipt);
            }
        }
        catch
        {
            // 作用域记账失败与控制调用结果无关：不抛出、不改写调用返回值。
        }
        try
        {
            lock (sync)
            {
                receipts.Add(receipt);
                while (receipts.Count > MaxReceipts)
                    receipts.RemoveAt(0);
            }
        }
        catch
        {
            // 观测发布失败与控制调用结果无关：不抛出、不改写调用返回值。
        }
    }

    public static AsusAcpiCallReceipt[] PeekReceipts()
    {
        lock (sync) return receipts.ToArray();
    }

    public static AsusAcpiCallReceipt[] TakeReceipts()
    {
        lock (sync)
        {
            var copy = receipts.ToArray();
            receipts.Clear();
            return copy;
        }
    }

    /// <summary>测试/联调辅助：清空已记录收据（不改变任何控制行为）。</summary>
    public static void ClearReceipts()
    {
        lock (sync) receipts.Clear();
    }
}
