using System.Buffers.Binary;
using System.Text.Json;
using YeManInputHost.Diagnostics;
using Environment = FixtureEnvironment;
using Stopwatch = FixtureStopwatch;

// Only extracted methods are inserted below. All external boundaries are inert;
// no HostSession, SDK assembly, target, pipe or real shared mapping is constructed.
internal static class FixtureEnvironment { internal static long TickCount64 => 5000; }
internal static class FixtureStopwatch
{
    internal static long GetTimestamp() => 2000;
    internal static TimeSpan GetElapsedTime(long _) => TimeSpan.FromTicks(1000);
}
internal sealed record Config(string Persona = "steamdeck", string ProfileIdentity = "steam-deck-composite",
    bool Gate = true, bool Due = true, bool Cost = true, bool Flag = true, bool Ready = true,
    bool ProfileThrow = false, bool BuildThrow = false, bool BackendThrow = false,
    bool DiagnosticThrow = false, bool Deferred = false, bool WorkerPresent = true,
    bool WorkerReject = false, bool WorkerFlagThrow = false, int SinkFailurePhase = 0);
internal sealed record CommandEnvelope(string Persona, string ProfileIdentity,
    ulong Sequence = 3376, ulong BackButtons = 0);
internal sealed record Scope(string RunId, ulong Epoch, ulong PowerGeneration, string Persona, string ProfileIdentity);
internal sealed record Window(long EpochMs, long CapturedTick, long WindowMs,
    long Frames, long[] SubmitUs, long JsonSumUs, long JsonCount, long HidSumUs, long HidCount,
    long ReadSumUs, long ReadCount, long Rejected, long HidHideFull, long HidHideState,
    KeyValuePair<string, int>[] Roles, string Phase, string HostInstanceId, string? RunId,
    ulong Epoch, ulong PowerGeneration, string? Persona, string? ProfileIdentity, ulong Sequence,
    long ControllerGeneration, WeakReference<object>? Controller,
    Func<object?, bool, object?>? ReadApprovedSdk, bool AssetVerified)
{
    internal object? SteamDeckPadSubmissions { get; init; }
}
internal sealed class UsbipTransportObservation
{
    internal object Capture(object? _, bool __) => throw new Exception("SDK observation must not run on producer");
}
internal abstract class Harness
{
    protected MsiHostStageEvidence? _msiHostStageEvidence;
    protected FakeController? _controller;
    protected FakeContext? _context;
    protected Config cfg = new();
    protected bool _costFlagEnabled, _msiCostDeferredRequested;
    protected long _costWindowStartTicks;
    protected long _costFrames, _costFramesRejected, _costJsonParseUsSum, _costJsonParseCount,
        _costHidSubmitUsSum, _costHidSubmitCount, _costPipeReadUsSum, _costPipeReadCount,
        _costHidHideQueryFull, _costHidHideQueryState;
    protected int _costSubmitUsCount;
    protected long[] _costSubmitUsSamples = new long[32];
    protected long _lastSubmitElapsedUs, _backendSubmitOk, _backendSubmitFail;
    protected string _lastBackendDetail = "";
    protected ulong _lastBackendSequence;
    protected uint _dualSenseFrameCounter;
    protected FakeCpu? _costCpuWindow;
    protected FakeWorker? _msiCostDiagnostics;
    protected bool _assetVerified = true;
    protected Scope? _tuple;
    protected UsbipTransportObservation? _usbipTransportObservation;
    protected readonly Dictionary<string, int> _costRoleOsTid = new();
    protected string _phase = "Active", _hostInstanceId = "0123456789abcdef0123456789abcdef";
    protected ulong _lastSequence = 3376;
    protected FakePads ScreenPadSubmissionEvidence = null!;
    internal readonly List<string> Business = new(), Order = new();
    internal readonly List<int> Stages = new();
    internal readonly List<long> FramesAtCostEnd = new();
    internal readonly Clock ClockSource = new();
    internal int FactoryCalls;
    internal long SuccessCount => _backendSubmitOk;
    internal long FailureCount => _backendSubmitFail;
    internal ulong LastSequence => _lastBackendSequence;
    internal uint SonyCounter => _dualSenseFrameCounter;
    internal bool CountersReset => _costFrames == 0 && _costFramesRejected == 0 && _costSubmitUsCount == 0 &&
        _costHidSubmitCount == 0 && _costHidSubmitUsSum == 0;
    internal abstract bool Run(CommandEnvelope envelope, out string detail);
    internal void Init(Config c)
    {
        cfg = c;
        _costFlagEnabled = c.Cost; _costWindowStartTicks = c.Due ? 0 : 4900;
        _msiCostDeferredRequested = c.Deferred;
        _controller = c.Ready ? new(this) : null; _context = c.Ready ? new(this) : null;
        _tuple = new("run-7", 7, 3, c.Persona, c.ProfileIdentity);
        _costCpuWindow = new(this); ScreenPadSubmissionEvidence = new(this);
        _msiCostDiagnostics = c.WorkerPresent ? new(this) : null;
        if (c.Gate)
        {
            var sink = new Sink(this);
            _msiHostStageEvidence = MsiHostStageEvidence.TryCreateForTest(111, 222,
                _hostInstanceId, ClockSource, new Factory(this, sink));
            if (_msiHostStageEvidence is null) throw new Exception("fixture factory rejected");
            _msiHostStageEvidence.BeginRead(); _msiHostStageEvidence.EndRead(123);
            _msiHostStageEvidence.BeginHandle();
            _msiHostStageEvidence.Decode("req-3376", 3376, "run-7", 7, 3, "SUBMIT_FRAME");
        }
        Business.Clear(); Order.Clear(); Stages.Clear();
    }
    internal void Mark(string s) { Business.Add(s); Order.Add(s); }
    protected object Build(string kind)
    {
        Mark("build:" + kind);
        if (cfg.BuildThrow) throw new FormatException("synthetic builder");
        return new object();
    }
    protected object BuildEliteState(object p, CommandEnvelope e) => Build("elite");
    protected object BuildDualSenseDataOnlyReport(object p, CommandEnvelope e, uint frame, object paddles) => Build("edge");
    protected object MapDsEdgePaddles(ulong buttons) { Mark("paddles"); return new object(); }
    protected void SubmitSteamDeckWithScreenPads(FakeController c, object p, CommandEnvelope e, bool cost)
    { Build("deck"); c.SubmitState(new object()); }
    protected void SubmitSonyReportChain1410(FakeController c, CommandEnvelope e, object r, uint frame, string reason)
        => c.SubmitRawReport(r);
    protected void SubmitEliteReportChain1410(FakeController c, object s, CommandEnvelope e, string reason)
        => c.SubmitState(s);
    protected void RecordCostRole(string role) => Mark("role:" + role);
    protected bool CostLoggingRequestedByFlag() { Mark("flag"); return cfg.Flag; }
    protected void CostDiagnostics()
    {
        Mark("diagnostics");
        if (cfg.DiagnosticThrow) throw new IOException("synthetic cost payload");
    }
    internal (bool? Ok, string Detail, string Error, string State) Outcome()
    {
        bool? ok = null; string detail = "", error = "";
        try { ok = Run(new(cfg.Persona, cfg.ProfileIdentity), out detail); }
        catch (Exception ex) { error = ex.GetType().Name; }
        var state = JsonSerializer.Serialize(new { Business, _backendSubmitOk, _backendSubmitFail,
            _lastBackendDetail, _lastBackendSequence, _lastSubmitElapsedUs, _costFrames,
            _costFramesRejected, _costSubmitUsCount, _costHidSubmitCount, _costHidSubmitUsSum,
            _costFlagEnabled, _costWindowStartTicks, _dualSenseFrameCounter });
        return (ok, detail, error, state);
    }
    internal sealed class FakeContext(Harness owner)
    {
        internal object GetProfile(string name)
        {
            owner.Mark("profile:" + name);
            if (owner.cfg.ProfileThrow) throw new InvalidOperationException("synthetic profile");
            return new object();
        }
    }
    internal sealed class FakeController(Harness owner)
    {
        private void Submit(string type)
        {
            owner.Mark("sdk:" + type);
            if (owner.cfg.BackendThrow) throw new InvalidOperationException("synthetic backend");
        }
        internal void SubmitState(object _) => Submit("state");
        internal void SubmitRawReport(object _) => Submit("raw");
    }
    protected sealed class FakeCpu(Harness owner) { internal void Reset() => owner.Mark("cpu.reset"); }
    protected sealed class FakePads(Harness owner)
    { internal object Capture() { owner.Mark("pad.capture"); return new object(); } }
    protected sealed class FakeWorker(Harness owner)
    {
        internal bool LoggingEnabled
        {
            get
            {
                owner.Mark("worker.flag");
                if (owner.cfg.WorkerFlagThrow) throw new IOException("synthetic worker flag");
                return owner.cfg.Flag;
            }
        }
        internal long ControllerGeneration => 7;
        internal bool TryRejectBeforeSnapshot() { owner.Mark("worker.reject"); return owner.cfg.WorkerReject; }
        internal bool TryRecord(Window window)
        {
            owner.Mark("worker.record:" + window.Persona + ":" + window.Frames);
            if (owner.cfg.DiagnosticThrow) throw new IOException("synthetic worker record");
            return true;
        }
    }
    internal sealed class Clock : MsiHostStageEvidence.IClock
    { internal int Calls; public long Frequency => 1000; public long GetTimestamp() => 1000 + Calls++; }
    internal sealed class Factory(Harness owner, Sink sink) : MsiHostStageEvidence.ISinkFactory
    {
        public MsiHostStageEvidence.ISink CreateNew(string name, int capacity)
        {
            ++owner.FactoryCalls;
            if (capacity != 1024) throw new Exception("stage contract capacity");
            return sink;
        }
    }
    internal sealed class Sink(Harness owner) : MsiHostStageEvidence.ISink
    {
        private readonly byte[] bytes = new byte[1024];
        private bool failed;
        public void WriteSequence(ulong sequence)
        {
            BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(24, 8), sequence);
            if ((sequence & 1) != 0) return;
            int phase = (int)BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(56, 8));
            owner.Stages.Add(phase); owner.Order.Add("s" + phase);
            if (phase == 15) owner.FramesAtCostEnd.Add(owner._costFrames);
            if (!failed && phase == owner.cfg.SinkFailurePhase)
            { failed = true; throw new IOException("synthetic stage sink"); }
        }
        public void WriteData(byte[] buffer, int offset, int count) => buffer.AsSpan(offset, count).CopyTo(bytes.AsSpan(offset, count));
        public void Dispose() { }
    }
}
internal sealed class HookHarness : Harness
{
    internal override bool Run(CommandEnvelope envelope, out string detail) => TrySubmitFrame(envelope, out detail);
    // CURRENT_HOOKED_METHODS
}
internal sealed class PlainHarness : Harness
{
    internal override bool Run(CommandEnvelope envelope, out string detail) => TrySubmitFrame(envelope, out detail);
    // CURRENT_PLAIN_METHODS
}
internal static class CurrentStageFixture
{
    private static int checks, cases, parserCases;
    private static void Check(bool condition, string message)
    { ++checks; if (!condition) throw new Exception(message); }
    private static void Test(Config c, bool admitted, string build, string sdk)
    {
        ++cases;
        var hooked = new HookHarness(); var plain = new PlainHarness();
        hooked.Init(c); plain.Init(c);
        var actual = hooked.Outcome(); var expected = plain.Outcome();
        Check(actual == expected, "diagnostic hooks changed behavior: " + c + "\n" + actual + "\n" + expected);
        Check(actual.Error == "", "unexpected escaping error: " + c);
        bool ready = admitted && c.Ready;
        bool success = ready && !c.ProfileThrow && !c.BuildThrow && !c.BackendThrow;
        string detail = !admitted ? "persona-not-admitted" : !c.Ready ? "target-not-ready" : success ? "report-submitted" :
            "submit-exception:" + (c.BuildThrow ? "FormatException" : "InvalidOperationException");
        Check(actual.Ok == success && actual.Detail == detail, "admission/submit contract: " + c);
        Check(hooked.SuccessCount == (success ? 1 : 0) && hooked.FailureCount == (ready && !success ? 1 : 0), "submit counters: " + c);
        Check(hooked.LastSequence == (ready ? 3376UL : 0UL), "last accepted-attempt sequence: " + c);
        Check(hooked.SonyCounter == (ready && !c.ProfileThrow && c.Persona == "dualsense-edge" ? 1U : 0U), "Edge frame counter: " + c);
        if (!ready) Check(hooked.Business.Count == 0 && hooked.Stages.Count == 0, "rejected/absent target did external work: " + c);
        else
        {
            Check(hooked.Business.First() == "profile:" + c.ProfileIdentity, "profile identity dispatch: " + c);
            var builds = hooked.Business.Where(x => x.StartsWith("build:")).ToArray();
            Check(builds.SequenceEqual(c.ProfileThrow ? [] : new[] { "build:" + build }), "one current builder only: " + c);
            var submissions = hooked.Business.Where(x => x.StartsWith("sdk:")).ToArray();
            Check(submissions.SequenceEqual(c.ProfileThrow || c.BuildThrow ? [] : new[] { "sdk:" + sdk }), "one current backend only: " + c);
            if (c.Deferred) Check(!hooked.Business.Contains("flag") && !hooked.Business.Contains("diagnostics"), "MSI deferred branch used synchronous diagnostic: " + c);
        }
        var phases = new List<int>();
        bool due = ready && c.Due && (c.Cost || c.Deferred);
        if (ready && c.Gate)
        {
            phases.Add(6);
            if (!c.ProfileThrow) phases.AddRange([12, 13]);
            if (due) phases.AddRange([14, 15]);
            phases.Add(7);
            int failedAt = phases.IndexOf(c.SinkFailurePhase);
            if (failedAt >= 0)
            {
                phases = phases.Take(failedAt + 1).ToList();
                phases.Add(11); // A failed diagnostic sink makes one best-effort Closed publication.
            }
        }
        Check(hooked.Stages.SequenceEqual(phases), "stage order/finally contract: " + c + "; got=" + string.Join(',', hooked.Stages));
        if (due)
        {
            Check(hooked.CountersReset, "due cost boundary did not reset counters: " + c);
            Check(hooked.FramesAtCostEnd.All(x => x == 0), "cost-end published before actual reset: " + c);
        }
        if (!c.Gate) Check(hooked.FactoryCalls == 0 && hooked.ClockSource.Calls == 0, "closed stage gate must have no sink/clock calls");
        else Check(hooked.FactoryCalls == 1, "stage factory must remain single-writer");
    }
    // CURRENT_ARGUMENT_PARSER
    private static void Parser()
    {
        string[] Good() => ["--pipe", "pipe-name", "--nonce", new string('a', 32), "--parent-pid", "111"];
        void Test(string[] args, bool accepted, bool stage = false)
        {
            ++parserCases;
            bool ok = TryParseArguments(args, out var pipe, out var nonce, out var pid, out var flag, out var error);
            Check(ok == accepted && (!ok || flag == stage), "argument admission case " + parserCases);
            if (ok) Check(pipe == "pipe-name" && nonce == new string('a', 32) && pid == 111, "argument values changed");
        }
        Test(Good(), true); Test([..Good(), "--msi-host-stage-evidence-v1"], true, true);
        foreach (var extra in new[] { "--other", "--MSI-HOST-STAGE-EVIDENCE-V1", "" }) Test([..Good(), extra], false);
        Test([..Good(), "--msi-host-stage-evidence-v1", "extra"], false);
        for (int n = 0; n < 6; ++n) Test(Good().Take(n).ToArray(), false);
        foreach (int i in new[] { 0, 2, 4 }) { var args = Good(); args[i] = "--wrong"; Test(args, false); }
        foreach (var value in new[] { "", " ", new string('p', 181), "pipe\n" }) { var args = Good(); args[1] = value; Test(args, false); }
        foreach (var value in new[] { "", new string('a', 31), new string('a', 161), new string('a', 32) + "\n" }) { var args = Good(); args[3] = value; Test(args, false); }
        foreach (var value in new[] { "0", "-1", "abc", "2147483648" }) { var args = Good(); args[5] = value; Test(args, false); }
    }
    private static int Main()
    {
        try
        {
            Parser();
            Config[] variations = [new Config(), new Config(Due: false), new Config(Cost: false), new Config(Flag: false),
                new Config(Ready: false), new Config(ProfileThrow: true), new Config(BuildThrow: true), new Config(BackendThrow: true),
                new Config(DiagnosticThrow: true), new Config(Deferred: true), new Config(Deferred: true, Cost: false),
                new Config(Deferred: true, WorkerPresent: false), new Config(Deferred: true, WorkerReject: true),
                new Config(Deferred: true, WorkerFlagThrow: true), new Config(Deferred: true, DiagnosticThrow: true),
                new Config(Deferred: true, Flag: false), new Config(Deferred: true, Due: false),
                ..new[] { 6, 7, 12, 13, 14, 15 }.Select(phase => new Config(SinkFailurePhase: phase)),
                ..new[] { 6, 7, 12, 13, 14, 15 }.Select(phase => new Config(Deferred: true, SinkFailurePhase: phase)) ];
            var identities = new[] {
                ("steamdeck", "steam-deck-composite", true, "deck", "state"),
                ("dualsense-edge", "dualsense-edge", true, "edge", "raw"),
                ("elite", "xbox-elite-v2", true, "elite", "state"),
                ("steamdeck", "dualsense-edge", false, "", ""),
                ("dualsense-edge", "dualsense", false, "", ""),
                ("elite", "xbox360", false, "", ""),
                ("xbox360", "xbox360", false, "", ""),
                ("dualshock4", "dualshock4", false, "", ""),
                ("dualsense", "dualsense", false, "", ""),
                ("disabled", "", false, "", ""), ("other", "", false, "", ""),
                ("SteamDeck", "steam-deck-composite", false, "", "") };
            foreach (var (persona, profile, admitted, build, sdk) in identities)
                foreach (var variant in variations)
                    foreach (bool gate in new[] { false, true })
                        Test(variant with { Persona = persona, ProfileIdentity = profile, Gate = gate }, admitted, build, sdk);
            Console.WriteLine(JsonSerializer.Serialize(new { pass = true, cases, parserCases, assertions = checks,
                scope = "Current extracted admission/submit/cost/reset methods; real stage publisher with fake sink; only legacy synchronous diagnostic payload is opaque. Three current outputs and rejected retired/mismatched identities.",
                hostConstructed = false, productionAssemblyLoaded = false, realMappingCalls = 0,
                hardwareOrInputApiCalls = 0, realBackendCalls = 0 }));
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine(JsonSerializer.Serialize(new { pass = false, cases, parserCases, assertions = checks, error = ex.ToString() }));
            return 1;
        }
    }
}
