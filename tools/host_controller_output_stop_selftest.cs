using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;
using System.Threading;
using HIDMaestro;
using YeManInputHost.Diagnostics;
using Guard = YeManInputHost.Diagnostics.HostControllerOutputStop;
using Idle = YeManInputHost.Diagnostics.HostControllerIdleCtsRelease;

#pragma warning disable CS0169, CS0414, CS0649
namespace OutputStopFixture
{
    internal static class World
    {
        internal static readonly List<string> Trace = new();
        internal static bool Mapped, Registered, ContextRetained;
        internal static int UnsafeUnmaps;
        internal static FakeReader? Reader, IdleReader;
        internal static void Reset()
        {
            Trace.Clear(); Mapped = Registered = ContextRetained = false;
            UnsafeUnmaps = 0; Reader = IdleReader = null; FakeReader.CurrentThread = null;
        }
    }
    internal sealed class FakeCts
    {
        internal int Cancels;
        internal bool CancelThrows, Disposed;
        internal Action? OnCancel;
        internal void Cancel()
        {
            ++Cancels; World.Trace.Add("cancel");
            if (CancelThrows || Disposed) throw new InvalidOperationException("private-cancel-detail");
            OnCancel?.Invoke();
        }
        internal void Dispose() { Disposed = true; World.Trace.Add("cts-dispose"); }
    }
    internal sealed class FakeReader
    {
        internal static FakeReader? CurrentThread;
        internal int Joins, LastBudget;
        internal bool Alive = true, StopsOnJoin = true, JoinThrows, Unstarted;
        internal Action? OnStopped;
        internal ThreadState ThreadState => Unstarted ? ThreadState.Unstarted | ThreadState.Background : Alive ? ThreadState.Running : ThreadState.Stopped;
        internal bool Join(int ms)
        {
            ++Joins; LastBudget = ms; World.Trace.Add("join:" + ms);
            if (JoinThrows || Unstarted) throw new ThreadStateException("private-join-detail");
            if (!Alive) return true;
            if (!StopsOnJoin || ms == 0) return false;
            Alive = false; OnStopped?.Invoke(); return true;
        }
        public static bool operator ==(System.Threading.Thread? a, FakeReader? b) => false;
        public static bool operator !=(System.Threading.Thread? a, FakeReader? b) => true;
        public override bool Equals(object? value) => ReferenceEquals(this, value);
        public override int GetHashCode() => base.GetHashCode();
    }
    internal sealed class FakeWake
    {
        internal bool Disposed;
        internal int Sets;
        internal void Set() { if (Disposed) throw new ObjectDisposedException("wake"); ++Sets; }
        internal void Dispose() { Disposed = true; }
    }
    internal sealed class FakeHost : IDisposable { public void Dispose() { World.Trace.Add("host-stop"); } }
    internal sealed class Tick { }
    internal sealed class OutputRuntime : Guard.IRuntime
    {
        internal Type Type = typeof(HMController);
        internal bool Identity = true, CurrentReader, BindingThrows, BudgetThrows, LockAvailable = true, InsideLock;
        internal int Budget = 500;
        public Type ControllerType => Type;
        public Type CancellationSourceType => typeof(FakeCts);
        public Type ReaderType => typeof(FakeReader);
        public bool IsPinnedCore(Type type) => BindingThrows ? throw new InvalidOperationException("private-binding-detail") : Identity;
        public int ReadJoinBudgetMilliseconds(Type type) => BudgetThrows ? throw new InvalidOperationException("private-budget-detail") : Budget;
        public bool IsCurrentReader(object reader) => CurrentReader;
        public ThreadState ReadThreadState(object reader) => ((FakeReader)reader).ThreadState;
        public bool TryEnterCancellationLock(object gate, int ms) { InsideLock = LockAvailable; return LockAvailable; }
        public void ExitCancellationLock(object gate) { if (!InsideLock) throw new InvalidOperationException(); InsideLock = false; }
        public void Cancel(object cts) { if (!InsideLock) throw new InvalidOperationException("cancel without SDK lock"); ((FakeCts)cts).Cancel(); }
        public bool Join(object reader, int ms) => ((FakeReader)reader).Join(ms);
    }
    internal sealed class IdleRuntime : Idle.IRuntime
    {
        internal Type Type = typeof(HMController);
        internal bool Identity = true, ClockIdentity = true, CurrentReader, StopThrows;
        internal int Budget = 500, StopCalls;
        public Type ControllerType => Type;
        public Type ClockInterfaceType => typeof(HIDMaestro.Internal.IDeviceClock);
        public Type ReaderType => typeof(FakeReader);
        public bool IsPinnedCore(Type type) => Identity;
        public bool IsPinnedClock(Type type) => ClockIdentity && type == typeof(HIDMaestro.Internal.DeviceClock<Tick>);
        public int ReadJoinBudgetMilliseconds(Type type) => Budget;
        public bool IsCurrentReader(object reader) => CurrentReader;
        public ThreadState ReadThreadState(object reader) => ((FakeReader)reader).ThreadState;
        public bool HasTerminated(object reader) => ((FakeReader)reader).Join(0);
        public object ReadClockThread(object clock) => ((HIDMaestro.Internal.IDeviceClock)clock).Thread;
        public void StopClock(object clock, int ms)
        {
            ++StopCalls;
            if (StopThrows) throw new InvalidOperationException("private-clock-detail");
            ((HIDMaestro.Internal.IDeviceClock)clock).Stop(ms);
        }
    }
    internal sealed class Missing { }
    internal sealed class MissingCancellationLock
    {
        private readonly FakeCts _outputCts = new();
        private readonly FakeReader _outputThread = new();
        private readonly nint _outputView = (nint)1;
    }
    internal sealed class MutableOutputThread
    {
        private readonly FakeCts _outputCts = new();
        private FakeReader _outputThread = new();
        private readonly nint _outputView = (nint)1;
        private readonly object _outputCtsLock = new();
        private bool _outputCtsClosed;
    }
    internal sealed class LegacyIdleOnly
    {
        private readonly FakeCts _idleCts = new();
        private FakeReader? _idleThread;
        private bool _disposed;
    }
}
namespace HIDMaestro.Internal
{
    using OutputStopFixture;
    internal interface IDeviceClock
    {
        void Stop(int joinMs);
        FakeReader Thread { get; }
    }
    // Exact Stop/Wake/Close bodies extracted from the verified release DLL
    // are compiled in another partial declaration. Construction here is fake.
    internal sealed partial class DeviceClock<TTick> : IDeviceClock
    {
        private readonly FakeReader _thread;
        private readonly object _lifetime = new();
        private readonly FakeWake _wake = new();
        private readonly nint _timer = 0;
        private bool _stop, _closed;
        public FakeReader Thread => _thread;
        internal bool Closed => _closed;
        internal bool Stopped => _stop;
        internal bool WakeDisposed => _wake.Disposed;
        internal DeviceClock(FakeReader reader) { _thread = reader; reader.OnStopped = Close; }
    }
    internal static class ClockNative { internal static void CloseHandle(nint timer) => throw new InvalidOperationException("no native timer in fixture"); }
}
namespace HIDMaestro
{
    using OutputStopFixture;
    using HIDMaestro.Internal;
    internal sealed partial class HMController
    {
        private bool _disposed;
        private readonly object _submitLock = new(), _pidStateLock = new();
        private readonly HMContext _context = new();
        private readonly FakeHost? _host = null;
        private IDeviceClock? _idleClock;
        private FakeReader? _idleThread;
        private readonly nint _outputView;
        private readonly FakeReader? _outputThread;
        private readonly FakeCts _outputCts;
        private readonly object _outputCtsLock = new();
        private bool _outputCtsClosed;
        private void SendReleaseFrames() { World.Trace.Add("release-input"); }
        private void OutputPoll() { World.Trace.Add("output-poll"); }
        internal int Index => 0;
        internal bool IsDisposed => _disposed;
        internal bool OutputClosed => _outputCtsClosed;
        internal FakeCts Cts => _outputCts;
        internal void SetOutputClosed() => CloseOutputCts();
        internal void RunOutputFinally() => OutputPollLoop();
        internal void ReplaceIdleForNegativeTest(IDeviceClock? clock, FakeReader? reader) { _idleClock = clock; _idleThread = reader; }
        internal HMController(FakeCts? cts = null, FakeReader? reader = null, nint view = 0, IDeviceClock? clock = null, FakeReader? idleReader = null)
        {
            _outputCts = cts ?? new(); _outputThread = reader; _outputView = view;
            _idleClock = clock; _idleThread = idleReader;
            if (reader is not null) reader.OnStopped = CloseOutputCts;
            World.Reader = reader; World.IdleReader = idleReader;
            World.Mapped = view != 0; World.Registered = reader is not null; World.ContextRetained = true;
        }
    }
    internal sealed class HMContext
    {
        internal void OnControllerDisposing(HMController controller)
        {
            World.ContextRetained = false;
            if (World.Mapped && (World.Reader?.Alive == true || World.IdleReader?.Alive == true)) ++World.UnsafeUnmaps;
            World.Mapped = false; World.Trace.Add("unmap");
        }
    }
    internal static class SharedMemoryIO { internal static void UnregisterOutputPump(int index) { World.Registered = false; World.Trace.Add("unregister"); } }
    internal static class TimeoutScale { internal static int Apply(int ms) => ms; }
}
internal static class HostControllerOutputStopSelfTest
{
    private static readonly List<object> Results = new();
    private static int _failed;
    private static object? _metadata;
    private static void Check(bool condition, string why = "assertion failed") { if (!condition) throw new InvalidOperationException(why); }
    private static void Test(string name, Action body)
    {
        OutputStopFixture.World.Reset();
        try { body(); Results.Add(new { name, status = "pass" }); }
        catch (Exception ex) { ++_failed; Results.Add(new { name, status = "fail", error = ex.Message }); }
    }
    private static bool Stop(object? controller, out string detail, OutputStopFixture.OutputRuntime? runtime = null) =>
        Guard.TryStopAgainstRuntime(controller, runtime ?? new(), out detail);
    private static bool StopIdle(object? controller, out string detail, OutputStopFixture.IdleRuntime? runtime = null) =>
        Idle.TryStopInputClockAgainstRuntime(controller, runtime ?? new(), out detail);
    private static void Retained(HMController controller)
    {
        Check(!controller.IsDisposed && OutputStopFixture.World.ContextRetained && OutputStopFixture.World.UnsafeUnmaps == 0, "failure released SDK state");
        Check(!OutputStopFixture.World.Trace.Contains("unregister") && !OutputStopFixture.World.Trace.Contains("unmap"), "barrier performed cleanup");
    }
    private static string Signature(MetadataReader reader, FieldDefinition field)
    {
        var blob = reader.GetBlobReader(field.Signature); blob.ReadSignatureHeader();
        var code = blob.ReadSignatureTypeCode();
        if (code == SignatureTypeCode.IntPtr) return "System.IntPtr";
        if (code == SignatureTypeCode.Double) return "System.Double";
        if (code == SignatureTypeCode.Boolean) return "System.Boolean";
        if (code == SignatureTypeCode.Object) return "System.Object";
        if (code != SignatureTypeCode.TypeHandle) throw new InvalidOperationException("unexpected field signature: " + code);
        var handle = blob.ReadTypeHandle();
        if (handle.Kind == HandleKind.TypeReference)
        {
            var type = reader.GetTypeReference((TypeReferenceHandle)handle);
            return reader.GetString(type.Namespace) + "." + reader.GetString(type.Name);
        }
        var def = reader.GetTypeDefinition((TypeDefinitionHandle)handle);
        return reader.GetString(def.Namespace) + "." + reader.GetString(def.Name);
    }
    private static void CheckPinnedMetadata(string sdk)
    {
        var bytes = File.ReadAllBytes(sdk);
        Check(bytes.Length == 63076352 && Convert.ToHexString(SHA256.HashData(bytes)).Equals("8FE7BDB21EC497591F527B7B87C4BC7B41B45C35A06CC211A7A0DCE48838ED2C", StringComparison.Ordinal), "SDK hash mismatch");
        using var pe = new PEReader(new MemoryStream(bytes, writable: false)); var r = pe.GetMetadataReader();
        var mvid = r.GetGuid(r.GetModuleDefinition().Mvid); var assembly = r.GetAssemblyDefinition();
        Check(mvid == new Guid(Guard.LockedCoreMvid) && assembly.Version == new Version(2, 0, 0, 0) && r.GetString(assembly.Name) == "HIDMaestro.Core", "identity mismatch");
        var controller = r.TypeDefinitions.Select(r.GetTypeDefinition).Single(t => r.GetString(t.Namespace) == "HIDMaestro" && r.GetString(t.Name) == "HMController");
        var expected = new Dictionary<string, (string Name, bool ReadOnly)>
        {
            ["_outputCts"] = ("System.Threading.CancellationTokenSource", true), ["_outputThread"] = ("System.Threading.Thread", true),
            ["_outputView"] = ("System.IntPtr", true), ["_outputCtsLock"] = ("System.Object", true), ["_outputCtsClosed"] = ("System.Boolean", false),
            ["_idleClock"] = ("HIDMaestro.Internal.IDeviceClock", false), ["_idleThread"] = ("System.Threading.Thread", false), ["_disposed"] = ("System.Boolean", false)
        };
        var rows = new List<object>();
        foreach (var pair in expected)
        {
            var f = controller.GetFields().Select(r.GetFieldDefinition).Single(f => r.GetString(f.Name) == pair.Key);
            Check((f.Attributes & FieldAttributes.FieldAccessMask) == FieldAttributes.Private && ((f.Attributes & FieldAttributes.InitOnly) != 0) == pair.Value.ReadOnly && (f.Attributes & FieldAttributes.Static) == 0, "field flags mismatch: " + pair.Key);
            Check(Signature(r, f) == pair.Value.Name, "field signature mismatch: " + pair.Key);
            rows.Add(new { name = pair.Key, type = pair.Value.Name, attributes = f.Attributes.ToString() });
        }
        Check(!controller.GetFields().Select(r.GetFieldDefinition).Any(f => r.GetString(f.Name) == "_idleCts"), "legacy idle CTS unexpectedly present");
        var scale = r.TypeDefinitions.Select(r.GetTypeDefinition).Single(t => r.GetString(t.Namespace) == "HIDMaestro.Internal" && r.GetString(t.Name) == "TimeoutScale");
        var factor = scale.GetFields().Select(r.GetFieldDefinition).Single(f => r.GetString(f.Name) == "Factor");
        Check(Signature(r, factor) == "System.Double" && (factor.Attributes & (FieldAttributes.Static | FieldAttributes.InitOnly)) == (FieldAttributes.Static | FieldAttributes.InitOnly) && (factor.Attributes & FieldAttributes.FieldAccessMask) == FieldAttributes.Public, "timeout factor mismatch");
        var clockInterface = r.TypeDefinitions.Select(r.GetTypeDefinition).Single(t => r.GetString(t.Namespace) == "HIDMaestro.Internal" && r.GetString(t.Name) == "IDeviceClock");
        Check((clockInterface.Attributes & TypeAttributes.Interface) != 0, "clock is not an interface");
        var stop = clockInterface.GetMethods().Select(r.GetMethodDefinition).Single(m => r.GetString(m.Name) == "Stop");
        var stopSig = r.GetBlobReader(stop.Signature); stopSig.ReadSignatureHeader();
        Check(stopSig.ReadCompressedInteger() == 1 && stopSig.ReadSignatureTypeCode() == SignatureTypeCode.Void && stopSig.ReadSignatureTypeCode() == SignatureTypeCode.Int32, "clock Stop signature");
        var clock = r.TypeDefinitions.Select(r.GetTypeDefinition).Single(t => r.GetString(t.Namespace) == "HIDMaestro.Internal" && r.GetString(t.Name) == "DeviceClock`1");
        Check((clock.Attributes & TypeAttributes.Sealed) != 0, "clock must be sealed");
        var streamTick = r.TypeDefinitions.Select(r.GetTypeDefinition).Single(t => r.GetString(t.Namespace) == "HIDMaestro.Internal" && r.GetString(t.Name) == "StreamTick");
        var methods = new List<object>();
        foreach (var name in new[] { "Dispose", "StopThreads", "StopWorkers", "CloseOutputCts", "OutputPollLoop" })
        {
            var m = controller.GetMethods().Select(r.GetMethodDefinition).Single(m => r.GetString(m.Name) == name);
            methods.Add(new { name, ilSha256 = Convert.ToHexString(SHA256.HashData(pe.GetMethodBody(m.RelativeVirtualAddress).GetILBytes()!)) });
        }
        _metadata = new { sdkBytes = bytes.Length, mvid, assemblyName = r.GetString(assembly.Name), version = assembly.Version.ToString(), fields = rows, methods, clock = "sealed DeviceClock<StreamTick> implements IDeviceClock.Stop(int)", targetAssemblyLoaded = false };
    }
    public static int Main(string[] args)
    {
        Test("pinned-release-PE-metadata-read-as-bytes-only", () => CheckPinnedMetadata(args.Single()));
        Test("production-entry-rejects-stub-before-operations", () =>
        {
            var c = new HMController(new(), new(), (nint)1);
            Check(!Guard.TryStop(c, out var d) && d == "sdk-output-stop-core-identity-mismatch");
            Check(!Idle.TryStopInputClock(c, out d) && d == "sdk-idle-clock-core-identity-mismatch"); Retained(c);
        });
        Test("null-and-unknown-identities-fail-closed", () =>
        {
            Check(!Stop(null, out _) && !StopIdle(null, out _)); var c = new HMController();
            Check(!Stop(c, out _, new() { Identity = false }) && !StopIdle(c, out _, new() { Identity = false })); Retained(c);
        });
        Test("old-or-mutable-output-contract-rejected", () =>
        {
            foreach (var c in new object[] { new OutputStopFixture.Missing(), new OutputStopFixture.MissingCancellationLock(), new OutputStopFixture.MutableOutputThread() })
            { Check(!Stop(c, out var d, new() { Type = c.GetType() }) && d == "sdk-output-stop-field-contract-mismatch"); }
        });
        Test("output-cancel-is-under-SDK-lock-and-join-proves-exit", () =>
        {
            var cts = new OutputStopFixture.FakeCts(); var r = new OutputStopFixture.FakeReader(); var c = new HMController(cts, r, (nint)1);
            Check(Stop(c, out var d) && d == "sdk-output-reader-stopped" && !r.Alive && cts.Disposed && c.OutputClosed); Retained(c);
        });
        Test("closed-source-is-never-cancelled-but-reader-must-still-join", () =>
        {
            var cts = new OutputStopFixture.FakeCts(); var r = new OutputStopFixture.FakeReader { StopsOnJoin = false }; var c = new HMController(cts, r, (nint)1);
            c.SetOutputClosed(); Check(!Stop(c, out var d) && d == "sdk-output-stop-timeout" && cts.Cancels == 0); Retained(c);
            r.StopsOnJoin = true; Check(Stop(c, out _) && cts.Cancels == 0);
        });
        Test("lock-timeout-does-not-cancel-or-release", () =>
        {
            var cts = new OutputStopFixture.FakeCts(); var r = new OutputStopFixture.FakeReader(); var c = new HMController(cts, r, (nint)1);
            Check(!Stop(c, out var d, new() { LockAvailable = false }) && d == "sdk-output-stop-lock-timeout" && cts.Cancels == 0 && r.Joins == 0); Retained(c);
        });
        Test("no-reader-and-never-started-reader-have-exact-zero-view-proof", () =>
        {
            Check(Stop(new HMController(), out var d) && d == "sdk-output-reader-not-created");
            var r = new OutputStopFixture.FakeReader { Unstarted = true, Alive = false }; var c = new HMController(new(), r);
            Check(Stop(c, out d) && d == "sdk-output-reader-never-started" && r.Joins == 0);
            Check(!Stop(new HMController(new(), null, (nint)1), out d) && d == "sdk-output-stop-reader-missing-with-mapping");
            Check(!Stop(new HMController(new(), r, (nint)1), out d) && d == "sdk-output-stop-unstarted-with-mapping");
        });
        Test("output-self-join-and-invalid-budgets-fail-before-cancel", () =>
        {
            var cts = new OutputStopFixture.FakeCts(); var c = new HMController(cts, new(), (nint)1);
            Check(!Stop(c, out var d, new() { CurrentReader = true }) && d == "sdk-output-stop-self-join");
            foreach (var b in new[] { -1, 0, 501, int.MaxValue }) Check(!Stop(c, out _, new() { Budget = b }));
            Check(cts.Cancels == 0); Retained(c);
        });
        Test("output-cancel-and-join-exceptions-remain-retryable", () =>
        {
            var cts = new OutputStopFixture.FakeCts { CancelThrows = true }; var r = new OutputStopFixture.FakeReader(); var c = new HMController(cts, r, (nint)1);
            Check(!Stop(c, out var d) && d == "sdk-output-stop-cancel-failed"); Retained(c);
            cts.CancelThrows = false; r.JoinThrows = true; Check(!Stop(c, out d) && d == "sdk-output-stop-join-failed"); Retained(c);
            r.JoinThrows = false; Check(Stop(c, out _));
        });
        Test("timeout-factor-capped-without-increasing-small-budget", () =>
        {
            Check(Guard.BoundJoinBudget(.1) == 50 && Guard.BoundJoinBudget(.333) == 166 && Guard.BoundJoinBudget(2) == 500);
            foreach (var f in new[] { double.NaN, double.PositiveInfinity, -.1, 0, .099, 100.1 }) Check(Guard.BoundJoinBudget(f) == 0);
        });
        Test("upstream-output-finally-closes-CTS-once", () =>
        {
            var c = new HMController(); c.RunOutputFinally(); c.RunOutputFinally(); Check(c.Cts.Disposed && c.OutputClosed && c.Cts.Cancels == 0);
        });
        Test("exact-2.0-Dispose-still-unmaps-after-bounded-join-timeout-without-barrier", () =>
        {
            var r = new OutputStopFixture.FakeReader { StopsOnJoin = false }; var c = new HMController(new(), r, (nint)1); c.Dispose();
            Check(r.Alive && c.IsDisposed && OutputStopFixture.World.UnsafeUnmaps == 1 && !c.Cts.Disposed);
        });
        Test("output-barrier-timeout-retains-then-retry-allows-exact-Dispose", () =>
        {
            var r = new OutputStopFixture.FakeReader { StopsOnJoin = false }; var c = new HMController(new(), r, (nint)1);
            if (Stop(c, out _)) c.Dispose(); Retained(c);
            r.StopsOnJoin = true; Check(Stop(c, out _)); c.Dispose();
            Check(c.IsDisposed && !OutputStopFixture.World.Mapped && OutputStopFixture.World.UnsafeUnmaps == 0 && c.Cts.Disposed);
        });
        Test("idle-old-CTS-contract-rejected", () =>
        {
            var c = new OutputStopFixture.LegacyIdleOnly();
            Check(!StopIdle(c, out var d, new() { Type = c.GetType() }) && d == "sdk-idle-clock-field-contract-mismatch");
        });
        Test("no-idle-clock-needs-no-local-resource-patch", () =>
        {
            var c = new HMController(); Check(Idle.TryCaptureAgainstRuntime(c, new OutputStopFixture.IdleRuntime(), out var receipt, out var d) && receipt is not null);
            Check(!receipt!.TryRelease(out d)); c.Dispose(); receipt.MarkControllerDisposeReturned(); Check(receipt.TryRelease(out d));
            Check(d == "sdk-idle-clock-release-complete" && OutputStopFixture.World.UnsafeUnmaps == 0 && c.Cts.Disposed);
        });
        Test("idle-stop-uses-exact-clock-Stop-and-confirms-thread-ended", () =>
        {
            var r = new OutputStopFixture.FakeReader(); var clock = new HIDMaestro.Internal.DeviceClock<OutputStopFixture.Tick>(r);
            var c = new HMController(clock: clock, idleReader: r); var runtime = new OutputStopFixture.IdleRuntime();
            Check(StopIdle(c, out var d, runtime) && d == "sdk-idle-clock-stopped" && !r.Alive && runtime.StopCalls == 1);
            Check(clock.Stopped && clock.Closed && clock.WakeDisposed && !c.IsDisposed); Retained(c);
        });
        Test("idle-clock-timeout-refuses-raw-takeover-and-release", () =>
        {
            var r = new OutputStopFixture.FakeReader { StopsOnJoin = false }; var clock = new HIDMaestro.Internal.DeviceClock<OutputStopFixture.Tick>(r);
            var c = new HMController(clock: clock, idleReader: r);
            Check(!StopIdle(c, out var d) && d == "sdk-idle-clock-timeout" && !clock.Closed); Retained(c);
            r.StopsOnJoin = true; Check(StopIdle(c, out _) && clock.WakeDisposed);
        });
        Test("unstarted-idle-clock-closes-owned-handles-without-starting", () =>
        {
            var r = new OutputStopFixture.FakeReader { Unstarted = true, Alive = false }; var clock = new HIDMaestro.Internal.DeviceClock<OutputStopFixture.Tick>(r);
            var c = new HMController(clock: clock, idleReader: r);
            Check(StopIdle(c, out var d) && d == "sdk-idle-clock-never-started" && clock.Closed && clock.WakeDisposed && r.Joins == 0);
        });
        Test("idle-self-join-clock-identity-and-reader-mismatch-are-inert", () =>
        {
            var r = new OutputStopFixture.FakeReader(); var clock = new HIDMaestro.Internal.DeviceClock<OutputStopFixture.Tick>(r);
            var c = new HMController(clock: clock, idleReader: r);
            Check(!StopIdle(c, out _, new() { CurrentReader = true })); Check(!StopIdle(c, out _, new() { ClockIdentity = false })); Check(!clock.Stopped);
            c.ReplaceIdleForNegativeTest(clock, new()); Check(!StopIdle(c, out _) && !clock.Stopped); Retained(c);
        });
        Test("idle-invalid-budget-stop-throw-disposed-and-orphan-thread-fail-closed", () =>
        {
            var r = new OutputStopFixture.FakeReader(); var clock = new HIDMaestro.Internal.DeviceClock<OutputStopFixture.Tick>(r);
            var c = new HMController(clock: clock, idleReader: r);
            Check(!StopIdle(c, out _, new() { Budget = 501 })); Check(!StopIdle(c, out var d, new() { StopThrows = true }) && d == "sdk-idle-clock-stop-failed" && !clock.Stopped);
            Check(!StopIdle(new HMController(idleReader: new()), out _));
            c.Dispose(); Check(!StopIdle(c, out d) && d == "sdk-idle-clock-controller-disposed");
        });
        Test("idle-receipt-requires-normal-Dispose-return-and-stable-capture", () =>
        {
            var r = new OutputStopFixture.FakeReader(); var clock = new HIDMaestro.Internal.DeviceClock<OutputStopFixture.Tick>(r); var c = new HMController(clock: clock, idleReader: r);
            Check(Idle.TryCaptureAgainstRuntime(c, new OutputStopFixture.IdleRuntime(), out var receipt, out _) && receipt is not null);
            Check(!receipt!.TryRelease(out _)); c.Dispose(); Check(!receipt.TryRelease(out _));
            receipt.MarkControllerDisposeReturned(); Check(receipt.TryRelease(out _) && receipt.TryRelease(out _));
            c.ReplaceIdleForNegativeTest(null, null); Check(!receipt.TryRelease(out _));
        });
        Test("both-barriers-then-exact-Dispose-leave-no-worker-or-owned-handles", () =>
        {
            var r = new OutputStopFixture.FakeReader(); var idle = new OutputStopFixture.FakeReader(); var clock = new HIDMaestro.Internal.DeviceClock<OutputStopFixture.Tick>(idle);
            var c = new HMController(new(), r, (nint)1, clock, idle);
            Check(Stop(c, out _)); Check(Idle.TryCaptureAgainstRuntime(c, new OutputStopFixture.IdleRuntime(), out var receipt, out _) && receipt is not null);
            c.Dispose(); receipt!.MarkControllerDisposeReturned(); Check(receipt.TryRelease(out _));
            Check(!r.Alive && !idle.Alive && c.Cts.Disposed && clock.WakeDisposed && OutputStopFixture.World.UnsafeUnmaps == 0);
        });
        Test("no-real-SDK-host-device-PInvoke-or-thread-start", () =>
        {
            var a = Assembly.GetExecutingAssembly();
            Check(a.GetReferencedAssemblies().All(n => n.Name != "YeManInputHost" && !(n.Name ?? "").Contains("HIDMaestro", StringComparison.OrdinalIgnoreCase)));
            Check(a.GetTypes().SelectMany(t => t.GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance)).All(m => (m.Attributes & MethodAttributes.PinvokeImpl) == 0));
        });
        Console.WriteLine(JsonSerializer.Serialize(new { version = 2, passed = Results.Count - _failed, failed = _failed,
            pureFakeOperations = true, targetAssemblyLoaded = false, realThreadsStarted = false, devicesOrDriversInvoked = false,
            metadata = _metadata, cases = Results }, new JsonSerializerOptions { WriteIndented = true }));
        return _failed == 0 ? 0 : 1;
    }
}