using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

// R6 diagnostic observer ONLY. Caller gates ROG + exact bound XInput identity and
// runs TryOpen/RunAsync on its independent observer task. Nothing here can submit
// controller output. Read completion time is evidence, NOT hardware freshness or
// source attribution; neither zero bytes nor a report count proves valid input.
internal sealed class PhysicalSourceReaderHidEvidence : IDisposable
{
    private const int MaxPathChars = 4096, MaxReportBytes = 4096, RawPrefixBytes = 96;
    private const int MaxValueCaps = 64, MaxUsageValues = 32, PendingWaitMs = 250, ObserverBudgetMs = 30000;
    private const int MaxLogBytes = 1024 * 1024, MaxLineBytes = 32 * 1024;
    private const uint HidSuccess = 0x00110000;
    private static readonly Guid HidInterface = new("4d1e55b2-f16f-11cf-88cb-001111000030");
    private static readonly object LogGate = new();
    private static long droppedLogLines;
    private readonly object resources = new();
    private readonly CancellationTokenSource stop = new();
    private readonly Context context;
    private readonly HidCaps caps;
    private readonly ValueCap[] valueCaps;
    private readonly uint valueCapsStatus;
    private readonly string valueCapsNote;
    private FileStream? stream;
    private PreparsedHandle? preparsed;
    private bool disposed;
    private int runStarted;

    private sealed record Context(int HelperPid, int ParentPid, ulong Power, ulong Topology,
        ulong Epoch, string ExactXinputPath, string Alias);

    private PhysicalSourceReaderHidEvidence(Context context, FileStream stream,
        PreparsedHandle preparsed, HidCaps caps, ValueCap[] valueCaps,
        uint valueCapsStatus, string valueCapsNote)
    {
        this.context = context; this.stream = stream; this.preparsed = preparsed;
        this.caps = caps; this.valueCaps = valueCaps;
        this.valueCapsStatus = valueCapsStatus; this.valueCapsNote = valueCapsNote;
    }

    internal static PhysicalSourceReaderHidEvidence? TryOpen(string exactXinputPath,
        int parentPid, ulong power, ulong topology, ulong epoch)
    {
        var ctx = new Context(Environment.ProcessId, parentPid, power, topology, epoch,
            exactXinputPath ?? "", "");
        string stage = "validate";
        SafeFileHandle? handle = null;
        PreparsedHandle? pp = null;
        FileStream? input = null;
        try
        {
            Log(ctx, "starting", new { stage, diagnosticOnly = true });
            if (!OperatingSystem.IsWindows() || parentPid <= 0 || !ValidPath(exactXinputPath))
            { Fail(ctx, stage, 87, new { reason = "unsupported-platform-or-invalid-argument" }); return null; }
            stage = "hid-interface-alias";
            var aliasBuffer = new StringBuilder(MaxPathChars);
            uint length = MaxPathChars;
            var guid = HidInterface;
            uint cr = Native.CM_Get_Device_Interface_AliasW(ctx.ExactXinputPath, ref guid,
                aliasBuffer, ref length, 0);
            if (cr != 0)
            {
                Fail(ctx, stage, Native.CM_MapCrToWin32Err(cr, 31),
                    new { configRet = cr, requiredChars = length }); return null;
            }
            string alias = aliasBuffer.ToString();
            if (length > MaxPathChars || !ValidPath(alias))
            { Fail(ctx, stage, 13, new { reason = "invalid-alias", requiredChars = length }); return null; }
            ctx = ctx with { Alias = alias };
            stage = "create-file-read-only";
            // Exact CM alias; READ only, shared READ|WRITE, OPEN_EXISTING, OVERLAPPED.
            handle = Native.CreateFileW(alias, 0x80000000, 3, IntPtr.Zero, 3, 0x40000000, IntPtr.Zero);
            if (handle.IsInvalid)
            { Fail(ctx, stage, (uint)Marshal.GetLastPInvokeError()); return null; }
            stage = "hid-attributes";
            var attributes = new HidAttributes { Size = (uint)Marshal.SizeOf<HidAttributes>() };
            if (!Native.HidD_GetAttributes(handle, ref attributes))
            { Fail(ctx, stage, (uint)Marshal.GetLastPInvokeError()); return null; }
            if (attributes.VendorId != 0x0b05 || (attributes.ProductId != 0x1abe && attributes.ProductId != 0x1b4c))
            {
                Fail(ctx, stage, 0, new { reason = "additional-rog-attribute-check-rejected",
                    vendorId = attributes.VendorId, productId = attributes.ProductId }); return null;
            }
            stage = "hid-preparsed-data";
            bool gotPreparsed = Native.HidD_GetPreparsedData(handle, out IntPtr rawPreparsed);
            int ppError = Marshal.GetLastPInvokeError();
            if (rawPreparsed != IntPtr.Zero) pp = new PreparsedHandle(rawPreparsed);
            if (!gotPreparsed || pp is null || pp.IsInvalid)
            { Fail(ctx, stage, (uint)ppError); return null; }
            stage = "hid-input-caps";
            uint capsStatus = Native.HidP_GetCaps(pp, out HidCaps caps);
            if (capsStatus != HidSuccess || caps.InputReportByteLength is < 1 or > MaxReportBytes)
            {
                Fail(ctx, stage, 0, new { hidStatus = Hex(capsStatus),
                    inputReportByteLength = caps.InputReportByteLength }); return null;
            }
            // Optional parser metadata; skip rather than allocate from an unbounded descriptor.
            ValueCap[] values = Array.Empty<ValueCap>();
            uint valuesStatus = 0;
            string valuesNote = "no-input-value-caps";
            if (caps.NumberInputValueCaps > MaxValueCaps) valuesNote = "value-caps-budget-exceeded";
            else if (caps.NumberInputValueCaps != 0)
            {
                values = new ValueCap[caps.NumberInputValueCaps];
                ushort count = (ushort)values.Length;
                valuesStatus = Native.HidP_GetValueCaps(0, values, ref count, pp);
                if (valuesStatus != HidSuccess || count > values.Length)
                { values = Array.Empty<ValueCap>(); valuesNote = "value-caps-query-failed"; }
                else { Array.Resize(ref values, count); valuesNote = "bounded-value-caps"; }
            }
            stage = "file-stream";
            // Buffer size 1 disables FileStream read-ahead: one read, one HID report.
            input = new FileStream(handle, FileAccess.Read, 1, isAsync: true);
            handle = null; // FileStream now owns the exact handle.
            var observer = new PhysicalSourceReaderHidEvidence(ctx, input, pp, caps,
                values, valuesStatus, valuesNote);
            input = null; pp = null;
            Log(ctx, "opened", new { stage, vendorId = attributes.VendorId,
                productId = attributes.ProductId, versionNumber = attributes.VersionNumber,
                capsStatus = Hex(capsStatus), caps = CapsEvidence(caps),
                valueCapsStatus = Hex(valuesStatus), valueCapsNote = valuesNote,
                valueCaps = DescribeCaps(values) });
            return observer;
        }
        catch (Exception ex)
        { Fail(ctx, stage, Error(ex), new { exception = ex.GetType().Name, hresult = Hex((uint)ex.HResult) }); return null; }
        finally
        { try { input?.Dispose(); } catch { } try { handle?.Dispose(); } catch { } try { pp?.Dispose(); } catch { } }
    }

    internal async Task RunAsync(CancellationToken cancellationToken)
    {
        if (Interlocked.CompareExchange(ref runStarted, 1, 0) != 0) return;
        CancellationTokenSource? lifetime = null;
        Task<ReadCompletion>? pending = null;
        var window = new ReportWindow();
        ulong sequence = 0;
        long? lastOriginalReadTick = null;
        long started = Environment.TickCount64, lastPendingLog = started, lastWindowLog = started;
        byte[] report = new byte[caps.InputReportByteLength];
        FileStream reader;
        try
        {
            lock (resources)
            {
                if (disposed || stream is null) return;
                reader = stream;
                lifetime = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, stop.Token);
                lifetime.CancelAfter(ObserverBudgetMs);
            }
            while (!lifetime.IsCancellationRequested)
            {
                // One outstanding read. WaitAsync's 250ms timeout does NOT cancel,
                // replace, or resubmit that read. ReadOnce stamps its completion.
                pending ??= ReadOnceAsync(reader, report, lifetime.Token);
                ReadCompletion completed;
                try
                {
                    completed = await pending.WaitAsync(TimeSpan.FromMilliseconds(PendingWaitMs),
                        lifetime.Token).ConfigureAwait(false);
                }
                catch (TimeoutException)
                {
                    long now = Environment.TickCount64;
                    if (now - lastPendingLog >= 1000)
                    {
                        Log(context, "read-pending", new { stage = "read", win32Error = 0,
                            elapsedMs = now - started, observerBudgetMs = ObserverBudgetMs,
                            pendingReadCount = 1, rawReportSequence = sequence, lastOriginalReadTick,
                            reason = "same-pending-read-no-completed-report-no-synthetic-sample" });
                        lastPendingLog = now;
                    }
                    if (now - lastWindowLog >= 1000)
                    { EmitWindow(window, "one-second-window-while-pending"); window.Clear(); lastWindowLog = now; }
                    continue;
                }
                pending = null; // Fully observed; only now may another read be created.
                int actual = completed.Length;
                long originalReadTick = completed.Tick;
                if (actual != report.Length)
                {
                    EmitWindow(window, "before-incomplete-report"); window.Clear();
                    Log(context, "incomplete-report", new { stage = "read", win32Error = 0,
                        actualLength = actual, expectedLength = report.Length,
                        originalReadCompletionTick = originalReadTick,
                        lastOriginalReadTick, rawReportSequence = sequence, sequenceAdvanced = false,
                        reportId = actual > 0 ? (int?)report[0] : null,
                        raw = Convert.ToHexString(report.AsSpan(0, Math.Min(actual, RawPrefixBytes))),
                        reason = "not-concatenated-not-padded-observer-ended" });
                    return;
                }
                lastOriginalReadTick = originalReadTick;
                window.Add(report, checked(++sequence), originalReadTick);
                if (originalReadTick - lastWindowLog >= 1000)
                { EmitWindow(window, "one-second-window"); window.Clear(); lastWindowLog = originalReadTick; }
                // Observer only (~63 completed reads/s at most). Never changes the
                // original completion timestamp or the independent 8ms XInput loop.
                await Task.Delay(16, lifetime.Token).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException)
        {
            Log(context, "cancelled", new { stage = "read", win32Error = 0,
                reason = Environment.TickCount64 - started >= ObserverBudgetMs ? "observer-budget-ended" : "parent-off-or-dispose",
                rawReportSequence = sequence, lastOriginalReadTick });
        }
        catch (ObjectDisposedException)
        { Log(context, "closed", new { stage = "read", win32Error = 0 }); }
        catch (Exception ex)
        { Fail(context, "observe", Error(ex), new { exception = ex.GetType().Name, hresult = Hex((uint)ex.HResult) }); }
        finally
        {
            try { EmitWindow(window, "observer-end"); } catch { }
            // Close the exact handle before waiting for its one possible pending
            // read. No parser call overlaps preparsed-data destruction (same lock).
            Dispose();
            if (pending is not null)
            {
                try { _ = await pending.WaitAsync(TimeSpan.FromMilliseconds(500)).ConfigureAwait(false); }
                catch (TimeoutException)
                {
                    // A misbehaving driver must not retain an unobserved fault or
                    // restart the observer. At most this one closed-handle read lives on.
                    _ = pending.ContinueWith(static t => { if (t.IsFaulted) _ = t.Exception; },
                        CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
                    Log(context, "read-close-pending", new { stage = "read-cleanup", win32Error = 0,
                        waitMs = 500, pendingReadCount = 1, streamCloseRequested = true });
                }
                catch (Exception) { /* cancellation/fault has been observed */ }
            }
            lifetime?.Dispose();
            Log(context, "stopped", new { stage = "observer-end", rawReportSequence = sequence,
                lastOriginalReadTick, observerBudgetMs = ObserverBudgetMs });
        }
    }

    private readonly record struct ReadCompletion(int Length, long Tick);
    private static async Task<ReadCompletion> ReadOnceAsync(FileStream reader, byte[] report, CancellationToken token)
    {
        int length = await reader.ReadAsync(report.AsMemory(), token).ConfigureAwait(false);
        long originalCompletionTick = Environment.TickCount64;
        return new ReadCompletion(length, originalCompletionTick);
    }
    private void EmitWindow(ReportWindow window, string reason)
    {
        if (window.Count == 0) return;
        object[] rawValues = Array.Empty<object>();
        bool parserAvailable;
        lock (resources)
        {
            parserAvailable = !disposed && preparsed is not null && !preparsed.IsClosed;
            if (parserAvailable) rawValues = ParseValues(preparsed!, window.LastFullReport, valueCaps);
        }
        Log(context, "report-window", new { stage = "input-report", reason,
            observedReportCount = window.Count, readCount = window.Count,
            completeLengthReadCount = window.Count,
            firstLastSequence = new[] { window.FirstSequence, window.LastSequence },
            rawReportSequence = window.LastSequence,
            firstLastOriginalTick = new[] { window.FirstTick, window.LastTick },
            lastReadTick = window.LastTick, lastOriginalReadTick = window.LastTick,
            reportId = window.ReportId, actualLength = window.ActualLength,
            expectedLength = caps.InputReportByteLength,
            raw = Convert.ToHexString(window.LastRaw),
            rawOr = Convert.ToHexString(window.Or), rawAnd = Convert.ToHexString(window.And),
            rawPrefixLength = window.LastRaw.Length,
            rawPrefixTruncated = window.ActualLength > RawPrefixBytes,
            nonAllZeroReportCount = window.NonAllZeroReports,
            nonZeroByteObservations = window.NonZeroByteObservations,
            byteObservationScope = "nonzero-count-full-report;raw-or-and-prefix-only",
            caps = CapsEvidence(caps), valueCapsStatus = Hex(valueCapsStatus), valueCapsNote,
            valueCaps = DescribeCaps(valueCaps), parserAvailable, rawValues,
            rawValuesLimit = MaxUsageValues, valueCapsLimit = MaxValueCaps, valuesAreNotExhaustive = true,
            rawValuesScope = "last-complete-report-only-not-canonical;arrays-first-value-only" });
    }

    private static object[] ParseValues(PreparsedHandle pp, byte[] report, ValueCap[] caps)
    {
        var result = new List<object>(MaxUsageValues);
        foreach (ValueCap cap in caps)
        {
            if (cap.ReportId != report[0]) continue;
            int count = BoundedUsageCount(cap, MaxUsageValues - result.Count);
            for (int i = 0; i < count; i++)
            {
                ushort usage = (ushort)(cap.UsageMin + i);
                uint status = Native.HidP_GetUsageValue(0, cap.UsagePage, cap.LinkCollection,
                    usage, out uint value, pp, report, (uint)report.Length);
                result.Add(new { usagePage = cap.UsagePage, usage, linkCollection = cap.LinkCollection,
                    reportId = cap.ReportId, rawValue = status == HidSuccess ? (uint?)value : null,
                    hidStatus = Hex(status), cap.BitSize, cap.ReportCount, cap.LogicalMin, cap.LogicalMax });
            }
            if (result.Count == MaxUsageValues) break;
        }
        return result.ToArray();
    }

    private static int BoundedUsageCount(ValueCap cap, int remaining)
    {
        if (remaining <= 0) return 0;
        if (cap.IsRange == 0) return 1;
        if (cap.IsRange != 1 || cap.UsageMax < cap.UsageMin) return 0;
        return Math.Min(remaining, (int)cap.UsageMax - cap.UsageMin + 1);
    }

    public void Dispose()
    {
        lock (resources)
        {
            if (disposed) return;
            disposed = true;
            try { stop.Cancel(); } catch { }
            try { stream?.Dispose(); } catch { }
            stream = null;
            try { preparsed?.Dispose(); } catch { }
            preparsed = null;
            stop.Dispose();
        }
    }

    // No WinAPI, device, file, SDK, log, or process calls in this self test.
    internal static bool RunPureSelfTest()
    {
        try
        {
            if (Marshal.SizeOf<HidAttributes>() != 12 || Marshal.SizeOf<HidCaps>() != 64 ||
                Marshal.SizeOf<ValueCap>() != 72 ||
                Marshal.OffsetOf<HidCaps>(nameof(HidCaps.NumberInputValueCaps)).ToInt32() != 48 ||
                Marshal.OffsetOf<ValueCap>(nameof(ValueCap.LogicalMin)).ToInt32() != 40 ||
                Marshal.OffsetOf<ValueCap>(nameof(ValueCap.UsageMin)).ToInt32() != 56) return false;
            if (BoundedUsageCount(new ValueCap { IsRange = 1, UsageMin = 0, UsageMax = 65535 }, 32) != 32 ||
                BoundedUsageCount(new ValueCap { IsRange = 1, UsageMin = 9, UsageMax = 8 }, 32) != 0 ||
                BoundedUsageCount(new ValueCap { IsRange = 0, UsageMin = 65535 }, 32) != 1 ||
                BoundedUsageCount(default, 0) != 0) return false;
            var window = new ReportWindow();
            byte[] first = { 1, 0, 0xf0 }, last = { 1, 0x0f, 0 };
            window.Add(first, 4, 1000); window.Add(last, 5, 1007);
            last[1] = 0xff; // A pending subsequent read must not overwrite this window snapshot.
            if (window.Count != 2 || window.FirstSequence != 4 || window.LastSequence != 5 ||
                window.FirstTick != 1000 || window.LastTick != 1007 || window.NonAllZeroReports != 2 ||
                window.NonZeroByteObservations != 4 || window.LastFullReport[1] != 0x0f || Convert.ToHexString(window.Or) != "010FF0" ||
                Convert.ToHexString(window.And) != "010000" || Convert.ToHexString(window.LastRaw) != "010F00") return false;
            window.Clear();
            window.Add(new byte[MaxReportBytes], 6, 2000);
            return window.Count == 1 && window.NonAllZeroReports == 0 && window.NonZeroByteObservations == 0 &&
                window.LastRaw.Length == RawPrefixBytes && window.ActualLength == MaxReportBytes;
        }
        catch { return false; }
    }

    private sealed class ReportWindow
    {
        internal ulong Count, FirstSequence, LastSequence, NonAllZeroReports, NonZeroByteObservations;
        internal long FirstTick, LastTick;
        internal byte ReportId;
        internal int ActualLength;
        internal byte[] LastFullReport = Array.Empty<byte>();
        internal byte[] LastRaw = Array.Empty<byte>(), Or = Array.Empty<byte>(), And = Array.Empty<byte>();
        internal void Add(ReadOnlySpan<byte> report, ulong sequence, long tick)
        {
            int prefix = Math.Min(report.Length, RawPrefixBytes);
            if (Count == 0)
            {
                FirstSequence = sequence; FirstTick = tick;
                LastFullReport = new byte[report.Length];
                LastRaw = new byte[prefix]; Or = new byte[prefix]; And = report[..prefix].ToArray();
            }
            Count++; LastSequence = sequence; LastTick = tick;
            ReportId = report[0]; ActualLength = report.Length;
            report.CopyTo(LastFullReport);
            report[..prefix].CopyTo(LastRaw);
            for (int i = 0; i < prefix; i++) { Or[i] |= report[i]; And[i] &= report[i]; }
            ulong nonzero = 0;
            foreach (byte b in report) if (b != 0) nonzero++;
            if (nonzero != 0) NonAllZeroReports++;
            NonZeroByteObservations += nonzero;
        }
        internal void Clear()
        {
            Count = FirstSequence = LastSequence = NonAllZeroReports = NonZeroByteObservations = 0;
            FirstTick = LastTick = 0; ReportId = 0; ActualLength = 0;
            LastFullReport = LastRaw = Or = And = Array.Empty<byte>();
        }
    }

    private static bool ValidPath(string? path)
    {
        if (string.IsNullOrWhiteSpace(path) || path.Length >= MaxPathChars) return false;
        foreach (char c in path) if (char.IsControl(c)) return false;
        return true;
    }
    private static object CapsEvidence(HidCaps c) => new { c.Usage, c.UsagePage,
        c.InputReportByteLength, c.OutputReportByteLength, c.FeatureReportByteLength,
        c.NumberInputButtonCaps, c.NumberInputValueCaps, c.NumberInputDataIndices };
    private static object[] DescribeCaps(ValueCap[] caps)
    {
        var rows = new object[caps.Length];
        for (int i = 0; i < caps.Length; i++)
        {
            ValueCap c = caps[i];
            rows[i] = new { c.UsagePage, c.ReportId, c.LinkCollection, c.IsAlias, c.IsRange,
                c.UsageMin, usageMax = c.IsRange == 1 ? c.UsageMax : c.UsageMin,
                c.BitSize, c.ReportCount, c.LogicalMin, c.LogicalMax, c.HasNull };
        }
        return rows;
    }
    private static string Hex(uint value) => "0x" + value.ToString("X8", System.Globalization.CultureInfo.InvariantCulture);
    private static uint Error(Exception ex) => ex is Win32Exception win32 ? (uint)win32.NativeErrorCode :
        ex is IOException ? (uint)ex.HResult & 0xffff : 0;
    private static void Fail(Context ctx, string stage, uint error, object? detail = null) =>
        Log(ctx, "failed", new { stage, win32Error = error, detail });

    // Called only on the caller-owned independent observer task. Best effort, no
    // retry or background logger; contention drops a row rather than waiting on a
    // different observer. Synchronous disk I/O never runs in the XInput loop.
    private static void Log(Context ctx, string kind, object detail)
    {
        if (!Monitor.TryEnter(LogGate)) { Interlocked.Increment(ref droppedLogLines); return; }
        try
        {
            string line = JsonSerializer.Serialize(new { schema = "ymcc.physical-hid-evidence.v1", kind,
                helperPID = ctx.HelperPid, parentPID = ctx.ParentPid, epoch = ctx.Epoch,
                power = ctx.Power, topology = ctx.Topology, exactXinputPath = ctx.ExactXinputPath,
                alias = ctx.Alias, diagnosticOnly = true, hardwareFreshnessProven = false,
                sourceAttributionProven = false, reportCountIsInputValidity = false,
                observationScope = "observer-read-report-window-not-all-hardware-reports-or-production-time",
                zeroBytesAreMappingValidity = false, logTick = Environment.TickCount64,
                droppedLogLines = Interlocked.Read(ref droppedLogLines), detail });
            byte[] bytes = Encoding.UTF8.GetBytes(line + "\n");
            if (bytes.Length > MaxLineBytes) { Interlocked.Increment(ref droppedLogLines); return; }
            string directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "YeManCC");
            Directory.CreateDirectory(directory);
            string path = Path.Combine(directory, "physical-source-reader.log");
            if (File.Exists(path) && new FileInfo(path).Length + bytes.Length > MaxLogBytes)
                File.Move(path, path + ".prev", overwrite: true);
            using var file = new FileStream(path, FileMode.Append, FileAccess.Write,
                FileShare.ReadWrite | FileShare.Delete, 1);
            file.Write(bytes);
        }
        catch { Interlocked.Increment(ref droppedLogLines); }
        finally { Monitor.Exit(LogGate); }
    }

    // SDK primary source: Windows Kits/10/Include/10.0.26100.0/shared/{hidpi,hidsdi}.h
    // pshpack4.h: BOOLEAN/UCHAR=1, USHORT/USAGE=2, ULONG/LONG/NTSTATUS=4.
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    private struct HidAttributes { internal uint Size; internal ushort VendorId, ProductId, VersionNumber; }
    [StructLayout(LayoutKind.Explicit, Pack = 4, Size = 64)]
    private struct HidCaps
    {
        [FieldOffset(0)] internal ushort Usage;
        [FieldOffset(2)] internal ushort UsagePage;
        [FieldOffset(4)] internal ushort InputReportByteLength;
        [FieldOffset(6)] internal ushort OutputReportByteLength;
        [FieldOffset(8)] internal ushort FeatureReportByteLength;
        [FieldOffset(44)] internal ushort NumberLinkCollectionNodes;
        [FieldOffset(46)] internal ushort NumberInputButtonCaps;
        [FieldOffset(48)] internal ushort NumberInputValueCaps;
        [FieldOffset(50)] internal ushort NumberInputDataIndices;
    }
    [StructLayout(LayoutKind.Explicit, Pack = 4, Size = 72)]
    private struct ValueCap
    {
        [FieldOffset(0)] internal ushort UsagePage;
        [FieldOffset(2)] internal byte ReportId;
        [FieldOffset(3)] internal byte IsAlias;
        [FieldOffset(6)] internal ushort LinkCollection;
        [FieldOffset(12)] internal byte IsRange;
        [FieldOffset(16)] internal byte HasNull;
        [FieldOffset(18)] internal ushort BitSize;
        [FieldOffset(20)] internal ushort ReportCount;
        [FieldOffset(40)] internal int LogicalMin;
        [FieldOffset(44)] internal int LogicalMax;
        [FieldOffset(56)] internal ushort UsageMin;
        [FieldOffset(58)] internal ushort UsageMax;
    }
    private sealed class PreparsedHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        internal PreparsedHandle(IntPtr value) : base(true) => SetHandle(value);
        protected override bool ReleaseHandle() => Native.HidD_FreePreparsedData(handle);
    }
    private static class Native
    {
        [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        internal static extern uint CM_Get_Device_Interface_AliasW(string path, ref Guid guid,
            StringBuilder alias, ref uint length, uint flags);
        [DllImport("cfgmgr32.dll", ExactSpelling = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        internal static extern uint CM_MapCrToWin32Err(uint configRet, uint defaultError);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        internal static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
            IntPtr security, uint disposition, uint flags, IntPtr template);
        [DllImport("hid.dll", SetLastError = true, ExactSpelling = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool HidD_GetAttributes(SafeFileHandle device, ref HidAttributes attributes);
        [DllImport("hid.dll", SetLastError = true, ExactSpelling = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool HidD_GetPreparsedData(SafeFileHandle device, out IntPtr data);
        [DllImport("hid.dll", ExactSpelling = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool HidD_FreePreparsedData(IntPtr data);
        [DllImport("hid.dll", ExactSpelling = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        internal static extern uint HidP_GetCaps(PreparsedHandle data, out HidCaps caps);
        [DllImport("hid.dll", ExactSpelling = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        internal static extern uint HidP_GetValueCaps(int reportType, [Out] ValueCap[] caps,
            ref ushort length, PreparsedHandle data);
        [DllImport("hid.dll", ExactSpelling = true)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        internal static extern uint HidP_GetUsageValue(int reportType, ushort page, ushort link,
            ushort usage, out uint value, PreparsedHandle data, [In] byte[] report, uint length);
    }
}



