using System;
using System.Buffers.Binary;
using System.Globalization;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

// Experimental physical-reader entry, before normal virtual Host/SDK initialization.
// GetState can discover OpenXInput devices/update its table and LEDs internally;
// no virtual SDK, Enable, slot setter, firmware or controller-mode API is invoked.
internal static class PhysicalSourceReader
{
    public const string Command = "--physical-source-reader-v1";
    internal const string SelfTestCommand = "--selftest-physical-source-reader-v1";
    internal const int FrameBytes = 104;
    private const uint Disconnected = 1167;
    private const ushort GuideMask = 0x0400;
    private const int PathCapacity = 4096;

    // Process exit codes; distinct from the DWORD at wire offset 12.
    internal enum ExitCode
    {
        Success = 0, InvalidArguments = 20, UnsupportedPlatform = 21,
        OwnTokenRejected = 22, ParentRejected = 23, DllRejected = 24,
        BindingRejected = 25, ConnectTimeout = 26, PipeClientRejected = 27,
        PipeClosed = 28, WriteTimeout = 29, Cancelled = 30,
        UnexpectedFailure = 31, ParentExited = 32, ParentOpenFailed = 33,
        ParentBirthQueryFailed = 34, ParentBirthMismatch = 35, ParentTokenFailed = 36,
        ParentUserSessionRejected = 37, ParentImageQueryFailed = 38,
        SelfTestFailed = 40, ParentOpenAccessDenied = 43, ParentTokenAccessDenied = 44, PipeAccessDenied = 45
    }

    private sealed record Options(string PipeName, string Nonce, int ParentPid,
        ulong ParentBirth, string DllPath, string DevicePath, ulong Power,
        ulong Topology, ulong Epoch, uint ExpectedSlot, ulong NonceTag);

    internal static async Task<int> RunAsync(string[] args)
    {
        // Selftest returns before ANY Windows API, DLL load or device access.
        if (args is { Length: 1 } && args[0] == SelfTestCommand) return RunSelfTest();
        if (!TryParse(args, out var options)) return (int)ExitCode.InvalidArguments;
        if (!OperatingSystem.IsWindows()) return (int)ExitCode.UnsupportedPlatform;
        try
        {
            using var ownToken = OpenToken(GetCurrentProcess());
            if (ownToken is null || !TryTokenDword(ownToken, 20, out var elevated) ||
                elevated != 0 || !TryIntegrity(ownToken, out var integrity) || integrity > 0x2000)
                return (int)ExitCode.OwnTokenRejected;
            using var parent = ParentGuard.TryOpen(options!, ownToken, out var parentFailure);
            if (parent is null) return (int)parentFailure;
            if (!DllPathMatchesParent(options!.DllPath, parent.ImagePath)) return (int)ExitCode.DllRejected;
            using var source = NativeSource.TryOpen(options.DllPath);
            if (source is null) return (int)ExitCode.DllRejected;
            if (!parent.IsAlive) return (int)ExitCode.ParentExited;
            uint? bound;
            try { bound = await BindWhenReadyAsync(source.Path, source.State, options.DevicePath,
                () => parent.IsAlive, static () => Environment.TickCount64,
                static token => Task.Delay(32, token), parent.ExitToken).ConfigureAwait(false); }
            catch (OperationCanceledException) { return (int)ExitCode.ParentExited; }
            if (bound is null) return (int)ExitCode.BindingRejected;
            uint slot = bound.Value;
            return await ServeAsync(options, parent, source, slot).ConfigureAwait(false);
        }
        catch (UnauthorizedAccessException) { return (int)ExitCode.PipeAccessDenied; }
        catch { return (int)ExitCode.UnexpectedFailure; } // No sensitive stdout/stderr.
    }

    private static bool TryParse(string[]? args, out Options? options)
    {
        options = null;
        if (args is null || args.Length != 11 || args.Any(static a => a is null) || args[0] != Command) return false;
        string nonce = args[2];
        if (nonce.Length != 32 || !nonce.All(static c => char.IsAsciiHexDigit(c))) return false;
        string pipe = args[1];
        if (pipe.Length > 200 || !pipe.StartsWith("ymcc-source-", StringComparison.Ordinal) ||
            !pipe.EndsWith("-" + nonce, StringComparison.OrdinalIgnoreCase) ||
            !pipe.All(static c => char.IsAsciiLetterOrDigit(c) || c is '-' or '_')) return false;
        if (!int.TryParse(args[3], NumberStyles.None, CultureInfo.InvariantCulture, out int pid) || pid <= 0 ||
            !U64(args[4], out ulong birth) || birth == 0 || !U64(args[7], out ulong power) ||
            !U64(args[8], out ulong topology) || !U64(args[9], out ulong epoch) ||
            args[10].Length != 1 || args[10][0] < '0' || args[10][0] > '3' || !ValidExactPath(args[6])) return false;
        try
        {
            if (!Path.IsPathFullyQualified(args[5]) || args[5].IndexOf('\0') >= 0) return false;
            string dll = Path.GetFullPath(args[5]);
            if (!string.Equals(Path.GetFileName(dll), "xinput1_4.dll", StringComparison.OrdinalIgnoreCase)) return false;
            options = new Options(pipe, nonce, pid, birth, dll, args[6], power, topology, epoch,
                (uint)(args[10][0] - '0'), BinaryPrimitives.ReadUInt64LittleEndian(Convert.FromHexString(nonce)));
            return true;
        }
        catch { return false; }
    }

    private static bool U64(string value, out ulong result) =>
        ulong.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out result);
    private static bool ValidExactPath(string path) => path.Length is > 0 and < PathCapacity - 1 &&
        !string.IsNullOrWhiteSpace(path) && !path.Any(static c => char.IsControl(c));
    private static bool DllPathMatchesParent(string dll, string image)
    {
        try
        {
            return Path.IsPathFullyQualified(image) &&
                string.Equals(Path.GetFileName(dll), "xinput1_4.dll", StringComparison.OrdinalIgnoreCase) &&
                string.Equals(Path.GetDirectoryName(Path.GetFullPath(dll)),
                    Path.GetDirectoryName(Path.GetFullPath(image)), StringComparison.OrdinalIgnoreCase);
        }
        catch { return false; }
    }

    private delegate string? PathReader(uint slot);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    private delegate uint StateReader(uint slot, out XState state);
    private static bool Exact(string? actual, string expected) => actual is not null &&
        ValidExactPath(actual) && string.Equals(actual, expected, StringComparison.OrdinalIgnoreCase);
    private static bool TryBind(PathReader path, string expected, out uint slot)
    {
        slot = uint.MaxValue;
        int matches = 0;
        try
        {
            for (uint candidate = 0; candidate < 4; candidate++)
                if (Exact(path(candidate), expected)) { slot = candidate; matches++; }
            // Scan this process mapping; native expectedSlot is only echoed on the wire.
            return matches == 1;
        }
        catch { slot = uint.MaxValue; return false; }
    }

    // Older OpenXInput path exports only inspect a process-local device table.
    // Initial GetState dispatch discovers an empty table; path-only polling cannot.
    // Discard every unbound state (including errors/pressed buttons/axes). This is
    // discovery only: exact unique identity is still required before ServeAsync.
    // Discovery may update OpenXInput's table/LED state internally. No Enable,
    // mapping setter, mode API, extra child or longer connect budget is requested.
    private static string? DiscoverDevicePath(PathReader path, StateReader discover, uint slot)
    {
        if (slot > 3) return null;
        // Let TryBind reject the ENTIRE scan if any slot query throws; a
        // partial scan cannot establish that the expected identity is unique.
        _ = discover(slot, out _);
        return path(slot);
    }

    // Startup-only discovery; streaming keeps its before/after path checks.
    private static async Task<uint?> BindWhenReadyAsync(PathReader path, StateReader discover, string expected,
        Func<bool> alive, Func<long> clock, Func<CancellationToken, Task> delay, CancellationToken token)
    {
        long start = clock();
        for (;;)
        {
            token.ThrowIfCancellationRequested();
            if (!alive()) throw new OperationCanceledException(token);
            if (TryBind(candidate => DiscoverDevicePath(path, discover, candidate), expected, out uint slot)) return slot;
            if (clock() - start >= 2000) return null;
            await delay(token).ConfigureAwait(false);
        }
    }

    // Match HC/native's existing NULL-size-query, then UTF-16-fill contract.
    private static string? ReadDevicePath(GetDevicePath path, uint slot)
    {
        uint count = 0;
        if (slot > 3 || path(slot, IntPtr.Zero, ref count) != 0 || count == 0 || count >= PathCapacity) return null;
        uint capacity = count;
        var buffer = new char[capacity + 1];
        var pinned = GCHandle.Alloc(buffer, GCHandleType.Pinned);
        try
        {
            if (path(slot, pinned.AddrOfPinnedObject(), ref count) != 0 || count == 0 || count > capacity) return null;
            int end = Array.IndexOf(buffer, '\0');
            if (end <= 0 || end > count) return null;
            string result = new(buffer, 0, end);
            return ValidExactPath(result) ? result : null;
        }
        finally { pinned.Free(); }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Pad
    {
        internal ushort Buttons;
        internal byte LT, RT;
        internal short LX, LY, RX, RY;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct XState { internal uint Packet; internal Pad Pad; }
    private readonly record struct Sample(bool Bound, uint ActualSlot, XState State, bool Guide);

    // Production and mocks execute this same consumer. Only the selected exact
    // source reaches streaming GetState; ordinal 100 supplies ONLY the GUIDE bit.
    private static Sample ReadSample(PathReader path, StateReader state, StateReader? guide,
        ref uint slot, string expected)
    {
        try
        {
            if (slot > 3) return new Sample(false, 0, default, false);
            if (!Exact(path(slot), expected))
            {
                // At most one four-slot rescan per sample, only after cached
                // identity fails. Revalidate the newly located slot before IO.
                if (!TryBind(path, expected, out uint rebound)) return new Sample(false, slot, default, false);
                slot = rebound;
                if (!Exact(path(slot), expected)) return new Sample(false, slot, default, false);
            }
            uint result = state(slot, out var value);
            bool guidePressed = false;
            if (result == 0 && guide is not null)
            {
                try { guidePressed = guide(slot, out var extra) == 0 && (extra.Pad.Buttons & GuideMask) != 0; }
                catch { /* Optional GUIDE failure supplies no ordinary pad values. */ }
            }
            if (!Exact(path(slot), expected) || result != 0) return new Sample(false, slot, default, false);
            value.Pad.Buttons = (ushort)(value.Pad.Buttons & ~GuideMask);
            return new Sample(true, slot, value, guidePressed);
        }
        catch { return new Sample(false, slot, default, false); }
    }

    private static ulong NextSequence(ref ulong sequence) => sequence = checked(sequence + 1);
    private static void WriteFrame(Span<byte> frame, Options o, ulong sequence,
        ulong tick, uint ownPid, Sample sample)
    {
        if (frame.Length != FrameBytes) throw new ArgumentException(nameof(frame));
        frame.Clear(); // Zero pad, packet and flags on failure. Offset 92 is readerActualSlot.
        BinaryPrimitives.WriteUInt32LittleEndian(frame, 0x31535256);
        BinaryPrimitives.WriteUInt32LittleEndian(frame[4..], 1);
        BinaryPrimitives.WriteUInt32LittleEndian(frame[8..], FrameBytes);
        BinaryPrimitives.WriteUInt32LittleEndian(frame[12..], sample.Bound ? 0 : Disconnected);
        BinaryPrimitives.WriteUInt64LittleEndian(frame[16..], sequence);
        BinaryPrimitives.WriteUInt64LittleEndian(frame[24..], tick);
        BinaryPrimitives.WriteUInt64LittleEndian(frame[32..], o.ParentBirth);
        BinaryPrimitives.WriteUInt64LittleEndian(frame[40..], o.Power);
        BinaryPrimitives.WriteUInt64LittleEndian(frame[48..], o.Topology);
        BinaryPrimitives.WriteUInt64LittleEndian(frame[56..], o.Epoch);
        BinaryPrimitives.WriteUInt32LittleEndian(frame[64..], o.ExpectedSlot);
        BinaryPrimitives.WriteUInt32LittleEndian(frame[76..], ownPid);
        BinaryPrimitives.WriteUInt32LittleEndian(frame[92..], sample.ActualSlot);
        BinaryPrimitives.WriteUInt64LittleEndian(frame[96..], o.NonceTag);
        if (!sample.Bound) return;
        BinaryPrimitives.WriteUInt32LittleEndian(frame[68..], sample.State.Packet);
        BinaryPrimitives.WriteUInt32LittleEndian(frame[72..], 1u | 2u | (sample.Guide ? 4u : 0u));
        BinaryPrimitives.WriteUInt16LittleEndian(frame[80..], (ushort)(sample.State.Pad.Buttons & ~GuideMask));
        frame[82] = sample.State.Pad.LT; frame[83] = sample.State.Pad.RT;
        BinaryPrimitives.WriteInt16LittleEndian(frame[84..], sample.State.Pad.LX);
        BinaryPrimitives.WriteInt16LittleEndian(frame[86..], sample.State.Pad.LY);
        BinaryPrimitives.WriteInt16LittleEndian(frame[88..], sample.State.Pad.RX);
        BinaryPrimitives.WriteInt16LittleEndian(frame[90..], sample.State.Pad.RY);
    }
    private static async Task<int> ServeAsync(Options o, ParentGuard parent, NativeSource source, uint slot)
    {
        using var stop = new CancellationTokenSource();
        using var lifetime = CancellationTokenSource.CreateLinkedTokenSource(stop.Token, parent.ExitToken);
        ConsoleCancelEventHandler cancel = (_, e) => { e.Cancel = true; stop.Cancel(); };
        Console.CancelKeyPress += cancel;
        try
        {
            using var pipe = new NamedPipeServerStream(o.PipeName, PipeDirection.InOut, 1,
                PipeTransmissionMode.Byte, PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
            using (var connect = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token))
            {
                connect.CancelAfter(TimeSpan.FromSeconds(5));
                try
                {
                    await BoundedPipeAsync(pipe.WaitForConnectionAsync(connect.Token), pipe,
                        TimeSpan.FromSeconds(5), connect.Token).ConfigureAwait(false);
                }
                catch (Exception ex) when (ex is OperationCanceledException or TimeoutException)
                {
                    return (int)(parent.ExitToken.IsCancellationRequested ? ExitCode.ParentExited :
                        stop.IsCancellationRequested ? ExitCode.Cancelled : ExitCode.ConnectTimeout);
                }
            }
            if (!parent.IsAlive) return (int)ExitCode.ParentExited;
            if (!GetNamedPipeClientProcessId(pipe.SafePipeHandle, out uint client) || client != (uint)o.ParentPid)
                return (int)ExitCode.PipeClientRejected;
            Task peer = WatchPeerAsync(pipe, lifetime);
            // Observer only: no HID state is admitted to ReadSample or the wire.
            Task hidEvidence = Task.Run(() => ObserveHidSourceAsync(o, lifetime.Token));
            try
            {
                var buffer = new byte[FrameBytes];
                ulong sequence = 0;
                using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(8));
                do
                {
                    lifetime.Token.ThrowIfCancellationRequested();
                    if (!parent.IsAlive) return (int)ExitCode.ParentExited;
                    var sample = ReadSample(source.Path, source.State, source.Guide, ref slot, o.DevicePath);
                    lifetime.Token.ThrowIfCancellationRequested();
                    if (!parent.IsAlive) return (int)ExitCode.ParentExited;
                    WriteFrame(buffer, o, NextSequence(ref sequence),
                        unchecked((ulong)Environment.TickCount64), (uint)Environment.ProcessId, sample);
                    using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(1));
                    using var write = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token, deadline.Token);
                    try
                    {
                        await BoundedPipeAsync(pipe.WriteAsync(buffer.AsMemory(), write.Token).AsTask(),
                            pipe, TimeSpan.FromSeconds(1), write.Token).ConfigureAwait(false);
                    }
                    catch (OperationCanceledException)
                    { return (int)(parent.ExitToken.IsCancellationRequested ? ExitCode.ParentExited :
                        stop.IsCancellationRequested ? ExitCode.Cancelled :
                        deadline.IsCancellationRequested ? ExitCode.WriteTimeout : ExitCode.PipeClosed); }
                    catch (TimeoutException) { return (int)ExitCode.WriteTimeout; }
                }
                while (await timer.WaitForNextTickAsync(lifetime.Token).ConfigureAwait(false));
                return (int)ExitCode.Success;
            }
            catch (OperationCanceledException)
            {
                return (int)(parent.ExitToken.IsCancellationRequested ? ExitCode.ParentExited :
                    stop.IsCancellationRequested ? ExitCode.Cancelled : ExitCode.PipeClosed);
            }
            catch (Exception ex) when (ex is IOException or ObjectDisposedException)
            { return (int)ExitCode.PipeClosed; }
            finally
            {
                lifetime.Cancel();
                pipe.Dispose();
                try { await peer.WaitAsync(TimeSpan.FromSeconds(1)).ConfigureAwait(false); }
                catch { Observe(peer); }
                try { await hidEvidence.WaitAsync(TimeSpan.FromSeconds(1)).ConfigureAwait(false); }
                catch { Observe(hidEvidence); }
            }
        }
        finally { Console.CancelKeyPress -= cancel; }
    }

    private static async Task ObserveHidSourceAsync(Options o, CancellationToken token)
    {
        try
        {
            token.ThrowIfCancellationRequested();
            using var witness = PhysicalSourceReaderHidEvidence.TryOpen(o.DevicePath, o.ParentPid, o.Power, o.Topology, o.Epoch);
            if (witness is not null) await witness.RunAsync(token).ConfigureAwait(false);
        }
        catch { /* Evidence failure never changes the admitted XInput source. */ }
    }

    private static async Task WatchPeerAsync(NamedPipeServerStream pipe, CancellationTokenSource lifetime)
    {
        try
        {
            // No client payload is defined. EOF, input, or failure ends this one
            // session. The pending read detects closure even between writes.
            await pipe.ReadAsync(new byte[1].AsMemory(), lifetime.Token).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is IOException or OperationCanceledException or ObjectDisposedException) { }
        finally { lifetime.Cancel(); }
    }

    private static async Task BoundedPipeAsync(Task operation, NamedPipeServerStream pipe,
        TimeSpan timeout, CancellationToken token)
    {
        try { await operation.WaitAsync(timeout, token).ConfigureAwait(false); }
        catch
        {
            // Disposal aborts overlapped I/O. Do not wait indefinitely for a
            // blocked write during teardown; only observe its late completion.
            pipe.Dispose(); Observe(operation); throw;
        }
    }
    private static void Observe(Task task) => _ = task.ContinueWith(static t => { _ = t.Exception; },
        CancellationToken.None, TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously,
        TaskScheduler.Default);

    private sealed class NativeSource : IDisposable
    {
        private readonly LibraryHandle _module;
        private readonly GetDevicePath _path;
        internal PathReader Path => ReadPath;
        internal StateReader State { get; }
        internal StateReader? Guide { get; }
        private NativeSource(LibraryHandle module, GetDevicePath path, StateReader state, StateReader? guide)
        { _module = module; _path = path; State = state; Guide = guide; }

        internal static NativeSource? TryOpen(string file)
        {
            var module = LoadLibraryExW(file, IntPtr.Zero, 0x00000100 | 0x00001000);
            if (module.IsInvalid) { module.Dispose(); return null; }
            try
            {
                var actual = new StringBuilder(32768);
                uint length = GetModuleFileNameW(module, actual, (uint)actual.Capacity);
                if (length == 0 || length >= actual.Capacity ||
                    !string.Equals(System.IO.Path.GetFullPath(actual.ToString()), file, StringComparison.OrdinalIgnoreCase)) return null;
                IntPtr path = GetProcAddress(module, "OpenXInputGetDevicePath");
                IntPtr state = GetProcAddress(module, "XInputGetState");
                if (path == IntPtr.Zero || state == IntPtr.Zero) return null;
                IntPtr guide = GetProcAddressOrdinal(module, (IntPtr)100);
                var source = new NativeSource(module, Marshal.GetDelegateForFunctionPointer<GetDevicePath>(path),
                    Marshal.GetDelegateForFunctionPointer<StateReader>(state), guide == IntPtr.Zero ? null :
                    Marshal.GetDelegateForFunctionPointer<StateReader>(guide));
                module = null!; // Ownership transfers only after required exports resolve.
                return source;
            }
            finally { module?.Dispose(); }
        }
        private string? ReadPath(uint slot) => ReadDevicePath(_path, slot);
        public void Dispose() => _module.Dispose();
    }
    private sealed class LibraryHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        public LibraryHandle() : base(true) { }
        protected override bool ReleaseHandle() => FreeLibrary(handle);
    }

    // ONE retained process handle validates birth/image/token and owns the OS
    // lifetime wait. There is no second OpenProcess or PID-reuse race.
    private sealed class ParentGuard : IDisposable
    {
        private sealed class ProcessWait : WaitHandle
        { internal ProcessWait(SafeWaitHandle handle) => SafeWaitHandle = handle; }
        private readonly SafeWaitHandle _handle;
        private readonly ProcessWait _wait;
        private readonly CancellationTokenSource _exit = new();
        private readonly RegisteredWaitHandle _registration;
        private readonly object _gate = new();
        private bool _disposed;
        internal string ImagePath { get; }
        internal CancellationToken ExitToken => _exit.Token;
        internal bool IsAlive => !_handle.IsClosed && !_handle.IsInvalid && WaitForSingleObject(_handle, 0) == 258;
        private ParentGuard(SafeWaitHandle handle, string image)
        {
            _handle = handle; ImagePath = image; _wait = new ProcessWait(handle);
            _registration = ThreadPool.RegisterWaitForSingleObject(_wait, static (state, _) =>
            {
                var parent = (ParentGuard)state!;
                lock (parent._gate)
                {
                    if (parent._disposed) return;
                    try { parent._exit.Cancel(); } catch (AggregateException) { }
                }
            }, this, Timeout.Infinite, true);
        }
        internal static ParentGuard? TryOpen(Options o, SafeAccessTokenHandle ownToken, out ExitCode failure)
        {
            failure = ExitCode.ParentRejected;
            var handle = OpenProcess(0x00100000 | 0x00001000, false, o.ParentPid);
            if (handle.IsInvalid)
            {
                failure = Marshal.GetLastWin32Error() == 5 ? ExitCode.ParentOpenAccessDenied : ExitCode.ParentOpenFailed;
                handle.Dispose(); return null;
            }
            try
            {
                failure = ExitCode.ParentExited;
                if (WaitForSingleObject(handle, 0) != 258) return null;
                failure = ExitCode.ParentBirthQueryFailed;
                if (!GetProcessTimes(handle, out var born, out _, out _, out _)) return null;
                failure = ExitCode.ParentBirthMismatch;
                if (born.Value != o.ParentBirth) return null;
                using var parentToken = OpenToken(handle.DangerousGetHandle());
                if (parentToken is null)
                {
                    failure = Marshal.GetLastWin32Error() == 5 ? ExitCode.ParentTokenAccessDenied : ExitCode.ParentTokenFailed;
                    return null;
                }
                failure = ExitCode.ParentUserSessionRejected;
                if (!SameUserSession(ownToken, parentToken)) return null;
                var image = new StringBuilder(32768);
                uint size = (uint)image.Capacity;
                failure = ExitCode.ParentImageQueryFailed;
                if (!QueryFullProcessImageNameW(handle, 0, image, ref size) || size == 0 || size >= image.Capacity) return null;
                failure = ExitCode.ParentExited;
                if (WaitForSingleObject(handle, 0) != 258) return null;
                var result = new ParentGuard(handle, image.ToString());
                handle = null!; failure = ExitCode.Success;
                return result;
            }
            finally { handle?.Dispose(); }
        }
        public void Dispose()
        {
            lock (_gate)
            {
                if (_disposed) return;
                _disposed = true; _registration.Unregister(null); _wait.Dispose(); _exit.Dispose();
            }
        }
    }

    private static SafeAccessTokenHandle? OpenToken(IntPtr process)
    {
        if (OpenProcessToken(process, 0x0008, out var token)) return token;
        token?.Dispose(); return null;
    }
    private static bool SameUserSession(SafeAccessTokenHandle own, SafeAccessTokenHandle parent)
    {
        if (!TryTokenDword(own, 12, out uint ownSession) || !TryTokenDword(parent, 12, out uint parentSession) ||
            ownSession != parentSession) return false;
        IntPtr a = TokenBuffer(own, 1), b = TokenBuffer(parent, 1);
        try
        {
            if (a == IntPtr.Zero || b == IntPtr.Zero) return false;
            IntPtr sidA = Marshal.ReadIntPtr(a), sidB = Marshal.ReadIntPtr(b);
            return IsValidSid(sidA) && IsValidSid(sidB) && EqualSid(sidA, sidB);
        }
        finally { if (a != IntPtr.Zero) Marshal.FreeHGlobal(a); if (b != IntPtr.Zero) Marshal.FreeHGlobal(b); }
    }
    private static IntPtr TokenBuffer(SafeAccessTokenHandle token, int kind)
    {
        if (GetTokenInformation(token, kind, IntPtr.Zero, 0, out uint bytes) ||
            Marshal.GetLastWin32Error() != 122 || bytes is < 4 or > 65536) return IntPtr.Zero;
        IntPtr buffer = Marshal.AllocHGlobal((int)bytes);
        if (GetTokenInformation(token, kind, buffer, bytes, out uint written) && written <= bytes) return buffer;
        Marshal.FreeHGlobal(buffer); return IntPtr.Zero;
    }
    private static bool TryTokenDword(SafeAccessTokenHandle token, int kind, out uint value)
    {
        value = 0;
        // Fixed DWORD classes (Elevation/SessionId) can report ERROR_BAD_LENGTH
        // for a zero-byte sizing call. Read their exact ABI size directly;
        // keep the variable SID buffer path and all permission limits unchanged.
        if (!GetTokenInformationDword(token, kind, out uint observed, sizeof(uint), out uint written) ||
            written != sizeof(uint)) return false;
        value = observed; return true;
    }
    private static bool TryIntegrity(SafeAccessTokenHandle token, out uint rid)
    {
        rid = uint.MaxValue;
        IntPtr buffer = TokenBuffer(token, 25);
        if (buffer == IntPtr.Zero) return false;
        try
        {
            IntPtr sid = Marshal.ReadIntPtr(buffer);
            if (!IsValidSid(sid)) return false;
            IntPtr countPtr = GetSidSubAuthorityCount(sid);
            if (countPtr == IntPtr.Zero) return false;
            byte count = Marshal.ReadByte(countPtr);
            if (count == 0) return false;
            IntPtr authority = GetSidSubAuthority(sid, (uint)(count - 1));
            if (authority == IntPtr.Zero) return false;
            rid = unchecked((uint)Marshal.ReadInt32(authority)); return true;
        }
        finally { Marshal.FreeHGlobal(buffer); }
    }
    internal static int RunSelfTest()
    {
        try { SelfTestCore(static _ => { }); return (int)ExitCode.Success; }
        catch { return (int)ExitCode.SelfTestFailed; }
    }

    // Pure mocks; injects delegates into the actual production sampling consumer.
    // No library, token, process, pipe or physical device API is called here.
    private static void SelfTestCore(Action<string> passed)
    {
        void Check(bool condition, string name)
        { if (!condition) throw new InvalidOperationException(name); passed(name); }
        string nonce = "00112233445566778899aabbccddeeff";
        string[] args = { Command, "ymcc-source-mock-" + nonce, nonce, "1234", "123456789012345678",
            @"C:\mock\xinput1_4.dll", @"\\?\hid#physical-exact", "18446744073709551615", "22", "33", "0" };
        Check(PhysicalSourceReaderHidEvidence.RunPureSelfTest(), "hid-evidence.pure-layout-and-bounds-no-device-io");
        Check(TryParse(args, out var parsed), "args.valid-exactly-11");
        var o = parsed!;
        Check(o.NonceTag == 0x7766554433221100UL, "args.nonce-first-8-little-endian");
        Check(!TryParse(null, out _) && !TryParse(args[..10], out _) &&
            !TryParse(args.Append("extra").ToArray(), out _), "args.count-null-short-extra-rejected");
        void Bad(int index, string value, string name)
        { var bad = (string[])args.Clone(); bad[index] = value; Check(!TryParse(bad, out _), name); }
        Bad(0, "--other", "args.command");
        Bad(1, "ymcc-source-wrong-" + new string('f', 32), "args.pipe-nonce-mismatch");
        Bad(1, @"ymcc-source-\bad-" + nonce, "args.pipe-path-separator");
        Bad(1, "other-" + nonce, "args.pipe-prefix");
        Bad(2, new string('g', 32), "args.nonce-hex");
        Bad(2, nonce[..30], "args.nonce-length");
        Bad(3, "0", "args.parent-zero");
        Bad(3, "2147483648", "args.parent-overflow");
        Bad(4, "0", "args.birth-zero");
        Bad(4, "18446744073709551616", "args.birth-overflow");
        Bad(5, "xinput1_4.dll", "args.dll-relative");
        Bad(5, @"C:\mock\xinput9_1_0.dll", "args.dll-basename");
        Bad(6, "", "args.path-empty");
        Bad(6, "path\0suffix", "args.path-nul");
        Bad(7, "+1", "args.numeric-sign");
        Bad(8, " 2", "args.numeric-whitespace");
        Bad(9, "-1", "args.epoch-negative");
        Bad(10, "4", "args.slot-range");
        Bad(10, "02", "args.slot-one-digit");
        Check(DllPathMatchesParent(o.DllPath, @"c:\MOCK\parent.exe"), "dll.same-parent-directory-case-insensitive");
        Check(!DllPathMatchesParent(o.DllPath, @"C:\other\parent.exe"), "dll.other-parent-directory-rejected");
        Check(!DllPathMatchesParent(o.DllPath, "parent.exe"), "dll.unknown-parent-image-rejected");
        int nullQueries = 0, fills = 0;
        bool visible = false;
        string exportedPath = o.DevicePath;
        GetDevicePath fakeExport = (uint slot, IntPtr output, ref uint count) =>
        {
            if (output == IntPtr.Zero)
            {
                nullQueries++;
                if (count != 0) throw new InvalidOperationException("size query must start at zero");
                if (!visible || slot != 3) return Disconnected;
                count = (uint)exportedPath.Length + 1;
                return 0;
            }
            fills++;
            if (count < exportedPath.Length + 1) return 122;
            Marshal.Copy((exportedPath + "\0").ToCharArray(), 0, output, exportedPath.Length + 1);
            count = (uint)exportedPath.Length + 1;
            return 0;
        };
        Check(ReadDevicePath(fakeExport, 3) is null && nullQueries == 1 && fills == 0,
            "export.pending-null-query-does-not-fill");
        visible = true;
        Check(ReadDevicePath(fakeExport, 3) == o.DevicePath && nullQueries == 2 && fills == 1,
            "export.null-size-query-before-utf16-fill");
        visible = false;
        StateReader noDiscovery = (uint _, out XState value) => { value = default; return Disconnected; };
        long mockTick = 0;
        int delays = 0;
        uint? coldSlot = BindWhenReadyAsync(slot => ReadDevicePath(fakeExport, slot), noDiscovery, o.DevicePath,
            static () => true, () => mockTick, _ =>
            { delays++; mockTick += 32; if (mockTick >= 384) visible = true; return Task.CompletedTask; },
            CancellationToken.None).GetAwaiter().GetResult();
        Check(coldSlot == 3 && mockTick == 384 && delays == 12,
            "binding.cold-export-pending384ms-then-visible-without-child-restart");
        mockTick = 0;
        Check(BindWhenReadyAsync(_ => null, noDiscovery, o.DevicePath, static () => true, () => mockTick,
            _ => { mockTick += 32; return Task.CompletedTask; }, CancellationToken.None).GetAwaiter().GetResult() is null &&
            mockTick >= 2000 && mockTick < 2032, "binding.cold-unavailable-bounded-2seconds");
        bool parentStopped = false;
        try
        {
            BindWhenReadyAsync(_ => null, noDiscovery, o.DevicePath, static () => false, static () => 0,
                _ => throw new InvalidOperationException("must not wait"), CancellationToken.None).GetAwaiter().GetResult();
        }
        catch (OperationCanceledException) { parentStopped = true; }
        Check(parentStopped, "binding.cold-parent-exit-cancels-before-scan");
        // The path becomes visible ONLY when discovery is actually called, not
        // because mock time advances. This catches the R4 cold-binding deadlock.
        visible = false;
        int discoveryCalls = 0;
        StateReader discovers = (uint candidate, out XState value) =>
        {
            discoveryCalls++;
            value = new XState { Packet = 999, Pad = new Pad { Buttons = 0xffff,
                LT = 255, RT = 254, LX = short.MaxValue, LY = short.MinValue, RX = 1234, RY = -2345 } };
            if (candidate == 3) visible = true;
            return Disconnected; // Discovery return/sample must not admit a frame.
        };
        Check(!TryBind(candidate => ReadDevicePath(fakeExport, candidate), o.DevicePath, out _) &&
            discoveryCalls == 0, "binding.path-only-cold-table-never-discovers");
        mockTick = 0;
        uint? discovered = BindWhenReadyAsync(candidate => ReadDevicePath(fakeExport, candidate), discovers,
            o.DevicePath, static () => true, () => mockTick,
            _ => throw new InvalidOperationException("discovery must not wait"), CancellationToken.None)
            .GetAwaiter().GetResult();
        Check(discovered == 3 && discoveryCalls == 4 && mockTick == 0,
            "binding.state-discovery-before-path-cold-table-matches-exactly");
        uint discoveredSlot = discovered ?? uint.MaxValue;
        StateReader freshZero = (uint candidate, out XState value) =>
        { value = default; return candidate == 3 ? 0u : Disconnected; };
        var afterDiscovery = ReadSample(candidate => ReadDevicePath(fakeExport, candidate), freshZero, null,
            ref discoveredSlot, o.DevicePath);
        byte[] firstFrame = new byte[FrameBytes];
        WriteFrame(firstFrame, o, 1, 1234, 1234, afterDiscovery);
        Check(afterDiscovery.Bound && afterDiscovery.State.Packet == 0 &&
            firstFrame.AsSpan(80, 12).ToArray().All(static b => b == 0),
            "binding.unbound-discovery-pressed-values-discarded-first-frame-fresh-zero");
        mockTick = 0;
        Check(BindWhenReadyAsync(_ => o.DevicePath + "suffix", discovers, o.DevicePath,
            static () => true, () => mockTick, _ => { mockTick += 32; return Task.CompletedTask; },
            CancellationToken.None).GetAwaiter().GetResult() is null,
            "binding.discovery-cannot-admit-prefix-only-path");
        mockTick = 0;
        Check(BindWhenReadyAsync(_ => o.DevicePath, noDiscovery, o.DevicePath,
            static () => true, () => mockTick, _ => { mockTick += 32; return Task.CompletedTask; },
            CancellationToken.None).GetAwaiter().GetResult() is null,
            "binding.discovery-cannot-admit-duplicate-paths");
        mockTick = 0;
        StateReader discoveryThrows = (uint _, out XState value) =>
        { value = default; throw new IOException("discovery failed"); };
        Check(BindWhenReadyAsync(_ => o.DevicePath, discoveryThrows, o.DevicePath,
            static () => true, () => mockTick, _ => { mockTick += 32; return Task.CompletedTask; },
            CancellationToken.None).GetAwaiter().GetResult() is null,
            "binding.discovery-exception-does-not-admit-even-visible-path");
        mockTick = 0;
        StateReader partialDiscoveryThrows = (uint candidate, out XState value) =>
        { value = default; if (candidate == 1) throw new IOException("partial discovery failed"); return 0; };
        Check(BindWhenReadyAsync(_ => o.DevicePath, partialDiscoveryThrows, o.DevicePath,
            static () => true, () => mockTick, _ => { mockTick += 32; return Task.CompletedTask; },
            CancellationToken.None).GetAwaiter().GetResult() is null,
            "binding.partial-discovery-exception-rejects-entire-scan");
        int beforeCancelled = discoveryCalls;
        using (var cancelledDiscovery = new CancellationTokenSource())
        {
            cancelledDiscovery.Cancel();
            bool cancelledBeforeDiscovery = false;
            try
            {
                BindWhenReadyAsync(_ => o.DevicePath, discovers, o.DevicePath,
                    static () => true, static () => 0, _ => Task.CompletedTask, cancelledDiscovery.Token)
                    .GetAwaiter().GetResult();
            }
            catch (OperationCanceledException) { cancelledBeforeDiscovery = true; }
            Check(cancelledBeforeDiscovery && discoveryCalls == beforeCancelled,
                "binding.cancelled-generation-does-not-run-discovery");
        }
        GetDevicePath oversized = (uint _, IntPtr output, ref uint count) => { count = PathCapacity; return 0; };
        Check(ReadDevicePath(oversized, 3) is null, "export.oversized-query-rejected");
        GetDevicePath changedSize = (uint _, IntPtr output, ref uint count) =>
        { if (output == IntPtr.Zero) { count = 20; return 0; } count = 30; return 122; };
        Check(ReadDevicePath(changedSize, 3) is null, "export.size-change-between-query-fill-rejected");
        GetDevicePath emptyFill = (uint _, IntPtr output, ref uint count) => { count = 20; return 0; };
        Check(ReadDevicePath(emptyFill, 3) is null, "export.empty-fill-rejected");
        int paths = 0, states = 0, guides = 0;
        uint mappedSlot = 2;
        PathReader path = slot => { paths++; return slot == mappedSlot ? o.DevicePath.ToUpperInvariant() : null; };
        Check(TryBind(path, o.DevicePath, out uint selected) && selected == 2 && paths == 4,
            "binding.scan-all-4-exact-case-insensitive-native0-reader2");
        Check(!TryBind(_ => o.DevicePath, o.DevicePath, out _), "binding.duplicate-rejected");
        Check(!TryBind(_ => null, o.DevicePath, out _), "binding.unknown-rejected");
        Check(!TryBind(_ => o.DevicePath + "suffix", o.DevicePath, out _), "binding.prefix-match-rejected");
        Check(!TryBind(_ => throw new IOException(), o.DevicePath, out _), "binding.exception-rejected");
        XState current = new() { Packet = 0xa1b2c3d4, Pad = new Pad { Buttons = 0x1401,
            LT = 213, RT = 249, LX = short.MinValue, LY = short.MaxValue, RX = -1234, RY = 2345 } };
        uint stateResult = 0, guideResult = 0;
        StateReader state = (uint slot, out XState value) =>
        { if (slot != mappedSlot) throw new InvalidOperationException("unselected read"); states++; value = current; return stateResult; };
        StateReader guide = (uint slot, out XState value) =>
        { if (slot != mappedSlot) throw new InvalidOperationException("unselected guide"); guides++;
            value = new XState { Packet = 999, Pad = new Pad { Buttons = GuideMask, LT = 1, RT = 2, LX = 44 } }; return guideResult; };
        paths = 0;
        Sample sample = ReadSample(path, state, guide, ref selected, o.DevicePath);
        Check(sample.Bound && sample.ActualSlot == 2 && paths == 2 && states == 1 && guides == 1,
            "sample.cached-reader-slot-two-path-checks-no-enumeration");
        Check(sample.State.Pad.LT == 213 && sample.State.Pad.RT == 249, "sample.independent-simultaneous-triggers");
        Check(sample.Guide && sample.State.Pad.Buttons == 0x1001 && sample.State.Packet == current.Packet &&
            sample.State.Pad.LX == short.MinValue, "sample.ordinal100-guide-only");
        Sample noGuide = ReadSample(path, state, null, ref selected, o.DevicePath);
        Check(noGuide.Bound && !noGuide.Guide && (noGuide.State.Pad.Buttons & GuideMask) == 0,
            "sample.standard-state-never-supplies-guide");
        guideResult = Disconnected;
        Check(ReadSample(path, state, guide, ref selected, o.DevicePath) is { Bound: true, Guide: false },
            "sample.optional-guide-failure");
        guideResult = 0;
        int previousStates = states, previousGuides = guides;
        paths = 0;
        PathReader unknown = _ => { paths++; return null; };
        Check(!ReadSample(unknown, state, guide, ref selected, o.DevicePath).Bound && states == previousStates &&
            guides == previousGuides && paths == 5, "sample.before-unknown-single-bounded-rescan-no-state-read");
        Check(!ReadSample(_ => o.DevicePath + "other", state, guide, ref selected, o.DevicePath).Bound && states == previousStates,
            "sample.before-mismatch-no-state-read");
        int order = 0;
        Sample reordered = ReadSample(_ => ++order == 1 ? o.DevicePath : @"\\?\hid#replacement",
            state, guide, ref selected, o.DevicePath);
        Check(!reordered.Bound && reordered.ActualSlot == 2, "sample.after-mismatch-zeroed-preserves-attempted-slot");
        order = 0;
        Check(!ReadSample(_ => ++order == 1 ? o.DevicePath : null, state, guide, ref selected, o.DevicePath).Bound,
            "sample.after-unknown-zeroed");
        stateResult = 5; previousGuides = guides;
        Check(!ReadSample(path, state, guide, ref selected, o.DevicePath).Bound && guides == previousGuides,
            "sample.any-state-error-disconnected-no-guide");
        stateResult = 0;
        Check(!ReadSample(_ => throw new IOException(), state, guide, ref selected, o.DevicePath).Bound,
            "sample.path-exception-zeroed");
        StateReader throws = (uint _, out XState value) => { value = current; throw new IOException(); };
        Check(!ReadSample(path, throws, guide, ref selected, o.DevicePath).Bound, "sample.state-exception-zeroed");
        Check(ReadSample(path, state, throws, ref selected, o.DevicePath) is { Bound: true, Guide: false },
            "sample.guide-exception-ignored");
        mappedSlot = 3; paths = 0;
        Sample migrated = ReadSample(path, state, guide, ref selected, o.DevicePath);
        Check(migrated.Bound && selected == 3 && migrated.ActualSlot == 3 && paths == 7,
            "sample.migration-one-rescan4-new-before-and-after");
        paths = 0;
        Check(ReadSample(path, state, guide, ref selected, o.DevicePath).Bound && paths == 2,
            "sample.migrated-slot-cached-no-full-enumeration");
        byte[] frame = new byte[FrameBytes];
        ulong sequence = 0;
        WriteFrame(frame, o, NextSequence(ref sequence), 0x8877665544332211, 4321, sample);
        uint U32(int offset) => BinaryPrimitives.ReadUInt32LittleEndian(frame.AsSpan(offset));
        ulong ULong(int offset) => BinaryPrimitives.ReadUInt64LittleEndian(frame.AsSpan(offset));
        short I16(int offset) => BinaryPrimitives.ReadInt16LittleEndian(frame.AsSpan(offset));
        Check(frame.Length == 104 && U32(0) == 0x31535256 && U32(4) == 1 && U32(8) == 104 && U32(12) == 0,
            "wire.104bytes-magic-version-size-success");
        Check(ULong(16) == 1 && ULong(24) == 0x8877665544332211 && ULong(32) == o.ParentBirth &&
            ULong(40) == ulong.MaxValue && ULong(48) == 22 && ULong(56) == 33, "wire.sequence-tick-birth-generations");
        Check(U32(64) == 0 && U32(68) == 0xa1b2c3d4 && U32(72) == 7 && U32(76) == 4321 && U32(92) == 2,
            "wire.native-expected-slot0-reader-actual-slot2-packet-flags-pid");
        Check(BinaryPrimitives.ReadUInt16LittleEndian(frame.AsSpan(80)) == 0x1001 && frame[82] == 213 && frame[83] == 249 &&
            I16(84) == short.MinValue && I16(86) == short.MaxValue && I16(88) == -1234 && I16(90) == 2345,
            "wire.buttons-dual-triggers-signed-axes");
        Check(ULong(96) == 0x7766554433221100UL, "wire.nonce-tag");
        WriteFrame(frame, o, NextSequence(ref sequence), 2, 4321, reordered);
        Check(U32(12) == Disconnected && U32(68) == 0 && U32(72) == 0 && U32(92) == 2 &&
            frame.AsSpan(80, 12).ToArray().All(static b => b == 0), "wire.failure-clears-stale-pad-packet-flags-keeps-actual-slot");
        Check(ULong(16) == 2, "wire.sequence-increments-on-failure");
        current = new XState { Packet = 77 };
        Sample centered = ReadSample(path, state, null, ref selected, o.DevicePath);
        WriteFrame(frame, o, NextSequence(ref sequence), 3, 4321, centered);
        Check(U32(12) == 0 && U32(72) == 3 && U32(68) == 77 && U32(92) == 3 &&
            frame.AsSpan(80, 12).ToArray().All(static b => b == 0), "wire.return-to-center-no-stale-values-reader-slot3");
        Check(ULong(16) == 3, "wire.strict-sequence-across-recovery");
        sequence = ulong.MaxValue;
        bool overflow = false;
        try { NextSequence(ref sequence); } catch (OverflowException) { overflow = true; }
        Check(overflow, "wire.sequence-never-wraps");
        Check(Marshal.SizeOf<Pad>() == 12 && Marshal.SizeOf<XState>() == 16, "abi.xinput-pad12-state16");
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct FileTime
    {
        internal uint Low, High;
        internal readonly ulong Value => ((ulong)High << 32) | Low;
    }
    [UnmanagedFunctionPointer(CallingConvention.Winapi, CharSet = CharSet.Unicode)]
    private delegate uint GetDevicePath(uint slot,
        IntPtr buffer,
        ref uint count);

    [DllImport("kernel32.dll")] private static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern SafeWaitHandle OpenProcess(uint access, [MarshalAs(UnmanagedType.Bool)] bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(SafeWaitHandle handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetProcessTimes(SafeWaitHandle process, out FileTime creation,
        out FileTime exit, out FileTime kernel, out FileTime user);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool QueryFullProcessImageNameW(SafeWaitHandle process, uint flags, StringBuilder image, ref uint size);
    [DllImport("advapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool OpenProcessToken(IntPtr process, uint access, out SafeAccessTokenHandle token);
    [DllImport("advapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetTokenInformation(SafeAccessTokenHandle token, int kind, IntPtr buffer, uint bytes, out uint returned);
    [DllImport("advapi32.dll", EntryPoint = "GetTokenInformation", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetTokenInformationDword(SafeAccessTokenHandle token, int kind, out uint value, uint bytes, out uint returned);
    [DllImport("advapi32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)] private static extern bool IsValidSid(IntPtr sid);
    [DllImport("advapi32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)] private static extern bool EqualSid(IntPtr a, IntPtr b);
    [DllImport("advapi32.dll")] private static extern IntPtr GetSidSubAuthorityCount(IntPtr sid);
    [DllImport("advapi32.dll")] private static extern IntPtr GetSidSubAuthority(IntPtr sid, uint index);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetNamedPipeClientProcessId(SafePipeHandle pipe, out uint pid);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
    private static extern LibraryHandle LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
    private static extern uint GetModuleFileNameW(LibraryHandle module, StringBuilder file, uint size);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
    private static extern IntPtr GetProcAddress(LibraryHandle module, string name);
    [DllImport("kernel32.dll", EntryPoint = "GetProcAddress", ExactSpelling = true)]
    private static extern IntPtr GetProcAddressOrdinal(LibraryHandle module, IntPtr ordinal);
    [DllImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)] private static extern bool FreeLibrary(IntPtr module);
}
