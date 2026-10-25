using System.Buffers.Binary;
using System.Reflection;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using HIDMaestro;
using YeManInputHost.Diagnostics;

// SDK controller/context/profile and idle-clock stop are fake. SDK state/enum types are the pinned real assembly.
internal sealed class HMProfile { public string Id { get; init; } = "dualsense-edge"; public int InputReportSize { get; init; } = 64; }
internal sealed class HMContext
{
    private static readonly HMProfile SdProfile = new() { Id = "steam-deck-composite", InputReportSize = 64 };
    public HMProfile GetProfile(string id) => id == "steam-deck-composite" ? SdProfile : new() { Id = id, InputReportSize = id == "xbox-elite-v2" ? 0 : 64 };
}
internal sealed class HMController
{
    public long RawCalls, StateCalls;
    private readonly byte[] _extendedReportBuffer = new byte[64];
    public void SeedIdleCounter(uint counter) => BinaryPrimitives.WriteUInt32LittleEndian(_extendedReportBuffer.AsSpan(4), counter);
    public readonly byte[] LastRaw = new byte[2048];
    public int LastRawLength;
    public HMGamepadState LastState;
    public Exception? Failure;
    public Action? DuringSubmit;
    public byte[]? ExpectedRaw;
    public bool SameRawStorage = true;
    public void SubmitRawReport(ReadOnlySpan<byte> bytes)
    {
        ++RawCalls; LastRawLength = bytes.Length;
        if (ExpectedRaw is not null && bytes.Length != 0)
            SameRawStorage &= Unsafe.AreSame(ref MemoryMarshal.GetReference(bytes), ref ExpectedRaw[0]);
        bytes[..Math.Min(bytes.Length, LastRaw.Length)].CopyTo(LastRaw);
        DuringSubmit?.Invoke(); if (Failure is not null) throw Failure;
    }
    public void SubmitState(in HMGamepadState state)
    { ++StateCalls; LastState = state; DuringSubmit?.Invoke(); if (Failure is not null) throw Failure; }
}
internal static partial class Program
{
    private static string EvidenceDirectory = "";
    private static long FakeNow = 100_000_000;
    private static long _dualSenseFrameCounter;
    private const byte SonyWireBatteryFull = 0x0A;
    private const int DualSenseTouchWidth = 1920, DualSenseTouchHeight = 1080, DsEdgePaddleLeft = 0x40, DsEdgePaddleRight = 0x80;
    private readonly record struct ScreenPad(bool Active, bool Click, short X, short Y, ushort Pressure);
    /*ACTUAL_COMMON*/
    private sealed class Observation { public bool IsAbsent = false; public string Detail = "fake-data-only"; }
    private class Fields
    {
        public HMContext? _context = new();
        public HMController? _controller = new();
        public CommandEnvelope? _tuple;
        public string _startupNonce = "fixture-nonce", _hostInstanceId = "host-fixture";
        public MsiReportChainRuntime1410? _reportChain1410;
        public readonly MsiHostStageEvidence? _msiHostStageEvidence = null;
        public ulong _lastSequence = 1, _lastBackendSequence;
        public bool _costFlagEnabled, _targetPnpAdmitted = true;
        public readonly Observation _virtualEndpoint = new();
        public long _lastSubmitElapsedUs, _backendSubmitOk, _backendSubmitFail, _backendNeutralOk, _backendNeutralFail;
        public long _costHidSubmitUsSum, _costHidSubmitCount;
        public string _lastBackendDetail = "";
        protected static string CostStateDir => Path.Combine(EvidenceDirectory, "unexpected-default-sink");
        protected static void RecordCostRole(string role) { }
        protected static void RecordCostFrame(long elapsed, bool rejected) { }
    }
    private sealed class HostSession : Fields
    {
        private HostVirtualSignalEvidence? _virtualSignalEvidence;
        /*ACTUAL_CURRENT*/
        public HostSession(CommandEnvelope e, bool enabled, Action<string>? sink, string? dir = null)
        {
            _tuple = e; _lastSequence = e.Sequence; _costFlagEnabled = enabled;
            _virtualSignalEvidence = new(dir ?? EvidenceDirectory, () => FakeNow, sink);
        }
        public void Raw(CommandEnvelope? e, byte[] bytes, string reason = "live-frame", CommandEnvelope? canonical = null) =>
            SubmitSonyReportChain1410(_controller!, e, bytes, 123, reason, canonical);
        public void State(CommandEnvelope? e, HMGamepadState state, string reason = "live-frame", CommandEnvelope? canonical = null) =>
            SubmitEliteReportChain1410(_controller!, state, e, reason, canonical);
        public bool Frame(CommandEnvelope e, out string detail) => TrySubmitFrame(e, out detail);
        public bool Neutral(CommandEnvelope? e, out string detail) => TrySubmitNeutral(out detail, e);
        public void Toggle(bool enabled) { _costFlagEnabled = enabled; if (!enabled) ResetVirtualSignalEvidenceSafely(); }
        public void Rebuild(CommandEnvelope e) { CloseReportChain1410Safely("target-prepare"); _controller = new(); _tuple = e; _lastSequence = e.Sequence; }
        public void Close() => CloseReportChain1410Safely("release-local");
        public void InstallClock(Func<long> clock, Action<string> sink) => _virtualSignalEvidence = new(EvidenceDirectory, clock, sink);
        public HostVirtualSignalEvidence Collector => _virtualSignalEvidence!;
        public void Sd(CommandEnvelope canonical, CommandEnvelope? cause, string reason = "live-frame", bool neutral = false) =>
            SubmitSteamDeckWithScreenPads(_controller!, _context!.GetProfile("steam-deck-composite"), canonical, _costFlagEnabled, neutral,
                signalSession: this, signalCause: cause, signalReason: reason);
    }
    private sealed class HostSessionBefore : Fields
    {
        /*ACTUAL_BASELINE*/
        public HostSessionBefore(CommandEnvelope e) { _tuple = e; _lastSequence = e.Sequence; }
        public void Raw(CommandEnvelope e, byte[] bytes) => SubmitSonyReportChain1410(_controller!, e, bytes, 123, "live-frame");
        public void State(CommandEnvelope e, HMGamepadState state) => SubmitEliteReportChain1410(_controller!, state, e, "live-frame");
        public bool Frame(CommandEnvelope e, out string detail) => TrySubmitFrame(e, out detail);
        public bool Neutral(CommandEnvelope? e, out string detail) => TrySubmitNeutral(out detail, e);
        public void Sd(CommandEnvelope canonical, bool neutral = false) =>
            SubmitSteamDeckWithScreenPadsBefore(_controller!, _context!.GetProfile("steam-deck-composite"), canonical, _costFlagEnabled, neutral);
    }
    private static readonly List<string> Passed = new(), ParameterSamples = new();
    private static void Check(bool ok, string why) { if (!ok) throw new Exception(why); }
    private static void Pass(string name) { Passed.Add(name); Console.WriteLine("PASS " + name); }
    private static CommandEnvelope Env(string persona = "dualsense-edge", ulong sequence = 10, ulong? tick = null) =>
        new("SUBMIT_FRAME", "request-" + sequence, sequence, "run-fixture", 11, 12, "target-fixture", "coordinator", persona,
            persona == "elite" ? "xbox-elite-v2" : persona == "steamdeck" ? "steam-deck-composite" : "dualsense-edge", "fixture-nonce", 1, new string('a', 64))
        {
            Buttons = 0x1341, Lx = .25f, Ly = -.75f, Rx = .5f, Ry = -.125f, Lt = .2f, Rt = .6f,
            LtRaw = .3f, RtRaw = .7f, BackButtons = 3, ScreenButtons = 3,
            ScreenPadsPresent = true, ScreenPadLeft = new(true, true, -1234, 5678, 100),
            MonotonicTick = tick ?? (ulong)FakeNow, SampleSequence = sequence + 100,
            ImuAdmitted = true, ImuGyroX = 120, ImuGyroY = -23, ImuAccelZ = 1,
            ImuQuaternionPresent = true, ImuQuatW = .5, ImuQuatX = -.25, ImuQuatY = .125, ImuQuatZ = -.0625
        };
    private static void Advance(long ms = 1000) => FakeNow += ms;
    private static JsonElement Parse(string s) => JsonDocument.Parse(s).RootElement.Clone();
    private static JsonElement Last(List<string> lines) => Parse(lines[^1]);
    private static void CatchSame(Action action, Exception expected)
    { try { action(); throw new Exception("SDK did not throw"); } catch (Exception e) { Check(ReferenceEquals(e, expected), "SDK exception identity changed: " + e); } }
    private static void SameState(HMGamepadState a, HMGamepadState b)
    {
        Check(a.Buttons == b.Buttons && a.Hat == b.Hat && ReferenceEquals(a.Axes, b.Axes), "state argument changed");
        Check(JsonSerializer.Serialize(a, new JsonSerializerOptions { IncludeFields = true }) ==
              JsonSerializer.Serialize(b, new JsonSerializerOptions { IncludeFields = true }), "state values changed");
    }
    private static void ResetSd(HMController controller, uint seed = 123, bool owned = false)
    {
        controller.SeedIdleCounter(seed); controller.ExpectedRaw = ScreenPadReportBuffer;
        _screenPadOwnsReport = owned; _screenPadPacketCounter = seed;
        Array.Clear(ScreenPadReportBuffer); ScreenPadSubmissionEvidence.Reset();
        HostControllerIdleCtsRelease.Stops = 0; HostControllerIdleCtsRelease.Allowed = true;
    }
    private static void CheckSdBytes(HMController actual, HMController before)
    {
        Check(actual.RawCalls == before.RawCalls && actual.StateCalls == 0 && before.StateCalls == 0, "SD call counts differ");
        Check(actual.LastRawLength == 64 && before.LastRawLength == 64 && actual.LastRaw.AsSpan().SequenceEqual(before.LastRaw), "SD final raw argument changed");
        Check(actual.SameRawStorage && before.SameRawStorage, "SD reused buffer storage changed");
    }
    private static void SteamDeckActualBoundary()
    {
        foreach (var enabled in new[] { false, true })
        foreach (var fail in new[] { false, true })
        {
            Advance(10_000); var e = Env("steamdeck"); var logs = new List<string>();
            var h = new HostSession(e, enabled, logs.Add); var before = new HostSessionBefore(e) { _costFlagEnabled = enabled };
            var sdk = new IOException("SD-original-sdk"); if (fail) h._controller!.Failure = before._controller!.Failure = sdk;
            ResetSd(h._controller!); var ok = h.Frame(e, out var detail); var counter = _screenPadPacketCounter;
            var ownership = _screenPadOwnsReport; var stops = HostControllerIdleCtsRelease.Stops;
            var padAfter = ScreenPadSubmissionEvidence.Capture();
            ResetSd(before._controller!); var oldOk = before.Frame(e, out var oldDetail);
            var padBefore = ScreenPadSubmissionEvidence.Capture();
            Check(ok == oldOk && detail == oldDetail && counter == _screenPadPacketCounter && ownership == _screenPadOwnsReport && stops == HostControllerIdleCtsRelease.Stops && stops == 1, "SD live return/counter/ownership changed");
            CheckSdBytes(h._controller!, before._controller!);
            Check(JsonSerializer.Serialize(padAfter with { TargetGeneration = 0 }) == JsonSerializer.Serialize(padBefore with { TargetGeneration = 0 }), "existing SD pad evidence changed");
            Check(logs.Count == (enabled ? 1 : 0), "SD live not observed or OFF wrote");
            if (enabled)
            {
                var j = Last(logs); ParameterSamples.Add(logs[^1]);
                Check(j.GetProperty("request").GetProperty("persona").GetString() == "steamdeck" && j.GetProperty("request").GetProperty("commandSequence").GetUInt64() == e.Sequence, "SD tuple/sequence incorrect");
                Check(j.GetProperty("sdkArguments").GetProperty("length").GetInt32() == 64 && j.GetProperty("sdkArguments").GetProperty("sha256").GetString() == Convert.ToHexString(SHA256.HashData(h._controller!.LastRaw.AsSpan(0, 64))), "SD report not actual final buffer");
                Check(j.GetProperty("sdkArguments").GetProperty("hex").GetString() == Convert.ToHexString(h._controller!.LastRaw.AsSpan(0, 64)) && !j.GetProperty("sdkArguments").GetProperty("hexTruncated").GetBoolean(), "SD raw hex wrong");
                Check(j.GetProperty("canonical").GetProperty("quatW").GetDouble() == e.ImuQuatW && j.GetProperty("encoderIndex").GetUInt32() == counter, "SD quaternion/counter missing");
                Check(j.GetProperty("outcome").GetString() == (fail ? "threw" : "returned") && !j.GetProperty("coverage").GetString()!.Contains("excludes-SteamDeck"), "SD false coverage/outcome");
            }
            Advance(); var request = e with { Command = "SUBMIT_NEUTRAL", Sequence = 888 };
            ResetSd(h._controller!, uint.MaxValue, owned: true); ok = h.Neutral(request, out detail); counter = _screenPadPacketCounter;
            stops = HostControllerIdleCtsRelease.Stops; padAfter = ScreenPadSubmissionEvidence.Capture();
            ResetSd(before._controller!, uint.MaxValue, owned: true); oldOk = before.Neutral(request, out oldDetail); padBefore = ScreenPadSubmissionEvidence.Capture();
            Check(ok == oldOk && detail == oldDetail && counter == 0 && _screenPadPacketCounter == 0 && stops == 0 && HostControllerIdleCtsRelease.Stops == 0, "SD neutral counter wrap/ownership changed");
            CheckSdBytes(h._controller!, before._controller!);
            Check(JsonSerializer.Serialize(padAfter with { TargetGeneration = 0 }) == JsonSerializer.Serialize(padBefore with { TargetGeneration = 0 }), "SD neutral pad evidence changed");
            if (enabled)
            {
                var j = Last(logs); ParameterSamples.Add(logs[^1]);
                Check(j.GetProperty("request").GetProperty("commandSequence").GetUInt64() == 888 && j.GetProperty("canonical").GetProperty("buttons").GetInt32() == 0 &&
                    !j.GetProperty("canonical").GetProperty("screenPadLeft").GetProperty("Active").GetBoolean() && !j.GetProperty("wireTickKnown").GetBoolean() && !j.GetProperty("joinEligible").GetBoolean(), "SD neutral logs stale live input");
            }
            // Call the actual partial boundary directly to inspect original exception identity (Frame retains its existing catch).
            Advance(); e = e with { MonotonicTick = (ulong)FakeNow }; h._controller!.Failure = sdk;
            ResetSd(h._controller); var calls = h._controller.RawCalls;
            CatchSame(() => h.Sd(e, e), sdk); Check(h._controller.RawCalls == calls + 1, "SD threw more/less than once");
            ResetSd(before._controller!); before._controller!.Failure = sdk;
            CatchSame(() => before.Sd(e), sdk);
            CheckSdBytes(h._controller, before._controller);
        }
        Advance(); var live = Env("steamdeck"); var sinkCalls = 0;
        var badLogger = new HostSession(live, true, _ => { ++sinkCalls; throw new IOException("SD-logger"); });
        ResetSd(badLogger._controller!); badLogger.Sd(live, live); Check(badLogger._controller!.RawCalls == 1 && sinkCalls == 1, "SD logger failure prevented SDK");
        Advance(); live = live with { MonotonicTick = (ulong)FakeNow }; var exception = new ApplicationException("SD-sdk"); badLogger._controller.Failure = exception;
        CatchSame(() => badLogger.Sd(live, live), exception); Check(badLogger._controller.RawCalls == 2 && sinkCalls == 2, "SD logger replaced SDK failure");
        Advance(); var logs2 = new List<string>(); live = Env("steamdeck"); var clockFailure = new HostSession(live, true, logs2.Add);
        ResetSd(clockFailure._controller!); clockFailure.InstallClock(() => throw new IOException("SD-clock"), logs2.Add); clockFailure.Sd(live, live);
        Check(clockFailure._controller!.RawCalls == 1, "SD diagnostic Begin prevented SDK");
        Advance(); var owned = new HostSession(Env("steamdeck"), true, logs2.Add); ResetSd(owned._controller!);
        HostControllerIdleCtsRelease.Allowed = false; var priorLogs = logs2.Count;
        Check(!owned.Frame(Env("steamdeck"), out var barrierDetail) && barrierDetail == "submit-exception:InvalidOperationException" && owned._controller!.RawCalls == 0 && logs2.Count == priorLogs && !_screenPadOwnsReport, "SD failed ownership barrier changed");
        HostControllerIdleCtsRelease.Allowed = true;
        Pass("SD actual live/neutral A/B: final 64-byte buffer/storage, packet wrap/ownership, pad evidence, exactly once/original throw, logger/clock isolation; no false SD coverage");
    }
    private static void SteamDeckRateAndIsolation()
    {
        Advance(10_000); FakeNow = FakeNow / 1000 * 1000; var start = FakeNow; var live = Env("steamdeck"); var logs = new List<string>();
        var h = new HostSession(live, true, logs.Add); ResetSd(h._controller!);
        var neutral = live with { Command = "SUBMIT_NEUTRAL", Buttons = 0, MonotonicTick = 0, SampleSequence = 0 };
        var firstSequences = new Dictionary<ulong, ulong>();
        for (var ms = 0; ms <= 10_000; ms++)
        {
            FakeNow = start + ms;
            for (var sub = 0; sub < 10; sub++)
            {
                live = live with { Sequence = (ulong)(ms * 10 + sub + 1), SampleSequence = (ulong)(ms * 10 + sub + 501), MonotonicTick = (ulong)FakeNow };
                firstSequences.TryAdd(live.MonotonicTick / 1000, live.Sequence); h.Sd(live, live);
            }
            h.Sd(neutral, live with { Command = "SUBMIT_NEUTRAL" }, "release-neutral", neutral: true);
        }
        var entries = logs.Select(Parse).ToArray(); var joins = entries.Where(j => j.GetProperty("joinEligible").GetBoolean()).ToArray();
        Check(h._controller!.RawCalls == 110011 && h._controller.StateCalls == 0 && HostControllerIdleCtsRelease.Stops == 1, "SD high rate changed calls/ownership");
        Check(joins.Length == 11 && entries.Length == 22 && entries.GroupBy(j => j.GetProperty("tick").GetInt64() / 1000).All(g => g.Count() <= 2), "SD rate exceeds quota or joins missing");
        foreach (var j in joins) Check(j.GetProperty("request").GetProperty("commandSequence").GetUInt64() == firstSequences[j.GetProperty("sampleBucket").GetUInt64()], "SD not first actual sequence");
        var windows = entries.Where(j => j.GetProperty("window").ValueKind != JsonValueKind.Null).Select(j => j.GetProperty("window")).ToArray();
        Check(windows.Length == 2 && windows.Sum(w => w.GetProperty("calls").GetInt64()) == 110001 && windows.All(w => w.GetProperty("elapsedMs").GetInt64() == 5000 && w.GetProperty("threw").GetInt64() == 0 && w.GetProperty("neutral").GetProperty("Calls").GetInt64() > 0), "SD 5s windows/counters wrong");
        File.WriteAllLines(Path.Combine(EvidenceDirectory, "steamdeck-highrate-samples.jsonl"), logs);
        // Same actual SD partial/encoder/evidence path, held bucket; no parameter/evidence heap on unsampled calls.
        for (var i = 0; i < 20000; i++) h.Sd(live, live);
        var alloc = GC.GetAllocatedBytesForCurrentThread(); for (var i = 0; i < 100000; i++) h.Sd(live, live); alloc = GC.GetAllocatedBytesForCurrentThread() - alloc;
        Check(alloc == 0, "SD unsampled allocations: " + alloc);
        var count = logs.Count; h.Toggle(false); Advance(10000); for (var i = 0; i < 100; i++) h.Sd(live, live); Check(logs.Count == count, "SD OFF wrote");
        h.Toggle(true); Advance(); live = live with { MonotonicTick = (ulong)FakeNow }; h.Sd(live, live);
        Advance(5000); live = live with { MonotonicTick = (ulong)FakeNow }; h.Sd(live, live);
        Check(Last(logs).GetProperty("window").GetProperty("calls").GetInt64() == 2, "SD old counters revived after OFF");
        Advance(); live = live with { RunId = "sd-new-run", Epoch = 31, MonotonicTick = (ulong)FakeNow }; h.Rebuild(live); ResetSd(h._controller!); h.Sd(live, live);
        Check(Last(logs).GetProperty("request").GetProperty("runId").GetString() == "sd-new-run", "SD old target binding revived");
        Advance(); live = live with { MonotonicTick = (ulong)FakeNow }; count = logs.Count;
        h._controller!.DuringSubmit = () => h.Toggle(false); h.Sd(live, live); Check(logs.Count == count, "SD completion wrote after OFF");
        h._controller.DuringSubmit = null; h.Toggle(true); Advance(); live = live with { MonotonicTick = (ulong)FakeNow }; h.Sd(live, live);
        Advance(); live = live with { MonotonicTick = (ulong)FakeNow }; count = logs.Count;
        h._controller.DuringSubmit = () => h.Rebuild(live with { Epoch = 32 }); h.Sd(live, live); Check(logs.Count == count, "SD stale completion survived rebuild");
        Pass("SD 110011 active calls, 22 samples/11 exact joins, two 5s windows, <=2/s; 100k unsampled allocations=0; OFF/rebuild/stale completion isolated");
    }
    private static void ParametersAndExceptions()
    {
        foreach (var persona in new[] { "dualsense-edge", "elite" })
        foreach (var enabled in new[] { false, true })
        {
            Advance(); var e = Env(persona); var logs = new List<string>();
            var actual = new HostSession(e, enabled, logs.Add); var before = new HostSessionBefore(e);
            var bytes = Enumerable.Range(0, 63).Select(n => (byte)n).ToArray(); var copy = bytes.ToArray();
            actual._controller!.ExpectedRaw = before._controller!.ExpectedRaw = bytes;
            var state = BuildEliteState(new HMProfile { Id = "xbox-elite-v2" }, e);
            if (persona == "elite") { before.State(e, state); actual.State(e, state); SameState(actual._controller.LastState, before._controller.LastState); }
            else { before.Raw(e, bytes); actual.Raw(e, bytes); Check(actual._controller.SameRawStorage && before._controller.SameRawStorage, "raw storage replaced"); }
            Check(bytes.SequenceEqual(copy), "input byte array mutated");
            Check(actual._controller.RawCalls == before._controller.RawCalls && actual._controller.StateCalls == before._controller.StateCalls, "call count changed");
            Check(logs.Count == (enabled ? 1 : 0), "disabled logging or first sample wrong");
            if (enabled)
            {
                ParameterSamples.Add(logs[^1]);
                var j = Last(logs); Check(j.GetProperty("request").GetProperty("commandSequence").GetUInt64() == e.Sequence, "sequence wrong");
                Check(j.GetProperty("sampleBucket").GetUInt64() == e.MonotonicTick / 1000 && j.GetProperty("wireSampleSequence").GetUInt64() == e.SampleSequence, "wire identity wrong");
                Check(j.GetProperty("canonical").GetProperty("buttons").GetInt32() == e.Buttons, "canonical wrong");
                Check(j.GetProperty("evidenceScope").GetString() == "SDK-call-only;not-driver/game-delivery", "scope overclaims");
                if (persona == "elite") Check(!j.GetProperty("sdkArguments").TryGetProperty("sha256", out _), "Elite pretends encoded report");
                else Check(j.GetProperty("sdkArguments").GetProperty("sha256").GetString() == Convert.ToHexString(SHA256.HashData(bytes)), "wrong report digest");
            }
            var sdkError = new InvalidOperationException("same-sdk-exception");
            Advance(); e = e with { Sequence = e.Sequence + 1, MonotonicTick = (ulong)FakeNow };
            actual._controller.Failure = before._controller.Failure = sdkError;
            if (persona == "elite") { CatchSame(() => before.State(e, state), sdkError); CatchSame(() => actual.State(e, state), sdkError); }
            else { CatchSame(() => before.Raw(e, bytes), sdkError); CatchSame(() => actual.Raw(e, bytes), sdkError); }
            Check(actual._controller.RawCalls + actual._controller.StateCalls == 2, "throw retried SDK");
            if (enabled) Check(Last(logs).GetProperty("outcome").GetString() == "threw", "throw lost");
        }
        Pass("raw storage/state identity, exact values, exactly once, SDK exception object preserved; flag on/off");
    }
    private static void ActualFrameAndNeutral()
    {
        foreach (var persona in new[] { "dualsense-edge", "elite" })
        foreach (var failed in new[] { false, true })
        {
            Advance(); var e = Env(persona); var logs = new List<string>(); var actual = new HostSession(e, true, logs.Add); var before = new HostSessionBefore(e);
            if (failed) actual._controller!.Failure = before._controller!.Failure = new IOException("sdk-failure");
            _dualSenseFrameCounter = 44; var a = actual.Frame(e, out var ad); var after = _dualSenseFrameCounter;
            _dualSenseFrameCounter = 44; var b = before.Frame(e, out var bd);
            Check(a == b && ad == bd && after == _dualSenseFrameCounter, "live behavior/sequence differs from before");
            Check(actual._controller!.LastRaw.AsSpan().SequenceEqual(before._controller!.LastRaw), "live raw bytes changed");
            if (persona == "elite") Check(JsonSerializer.Serialize(actual._controller.LastState, new JsonSerializerOptions { IncludeFields = true }) == JsonSerializer.Serialize(before._controller.LastState, new JsonSerializerOptions { IncludeFields = true }), "live Elite differs");
            Advance(); var neutralRequest = e with { Command = "SUBMIT_NEUTRAL", Sequence = 99 };
            _dualSenseFrameCounter = 77; a = actual.Neutral(neutralRequest, out ad); after = _dualSenseFrameCounter;
            _dualSenseFrameCounter = 77; b = before.Neutral(neutralRequest, out bd);
            Check(a == b && ad == bd && after == _dualSenseFrameCounter, "neutral behavior/sequence differs");
            var j = Last(logs); Check(j.GetProperty("canonical").GetProperty("buttons").GetInt32() == 0 && j.GetProperty("canonical").GetProperty("lx").GetDouble() == 0, "neutral logged trigger controls");
            Check(j.GetProperty("request").GetProperty("commandSequence").GetUInt64() == 99 && !j.GetProperty("joinEligible").GetBoolean(), "neutral identity wrong");
            Check(actual._controller.RawCalls + actual._controller.StateCalls == 2, "actual source path changed calls");
        }
        Advance(); var internalEnvelope = Env(); var internalLogs = new List<string>(); var internalHost = new HostSession(internalEnvelope, true, internalLogs.Add);
        Check(internalHost.Neutral(null, out _), "internal neutral failed");
        var internalEntry = Last(internalLogs);
        Check(internalEntry.GetProperty("request").GetProperty("commandSequenceSource").GetString() == "last-accepted-command-context" &&
            internalEntry.GetProperty("request").GetProperty("command").ValueKind == JsonValueKind.Null &&
            !internalEntry.GetProperty("wireTickKnown").GetBoolean(), "internal neutral invented actual wire request");
        Pass("actual live and neutral paths A/B: bytes, SDK state, return/detail, encoder counter unchanged");
    }
    private static void DiagnosticFailures()
    {
        Advance(); var e = Env(); var calls = 0; var logs = new List<string>();
        var h = new HostSession(e, true, _ => { ++calls; throw new IOException("logger-failure"); });
        var raw = new byte[63]; h.Raw(e, raw); Check(h._controller!.RawCalls == 1 && calls == 1, "logger prevented success");
        Advance(); e = e with { MonotonicTick = (ulong)FakeNow };
        var sdkError = new ApplicationException("original-sdk"); h._controller.Failure = sdkError;
        CatchSame(() => h.Raw(e, raw), sdkError); Check(h._controller.RawCalls == 2 && calls == 2, "logger replaced SDK failure");
        Advance(); e = Env("elite"); h = new(e, true, logs.Add);
        var badState = BuildEliteState(new HMProfile { Id = "xbox-elite-v2" }, e); badState.Axes![HMAxis.X] = float.NaN;
        h.State(e, badState); Check(h._controller!.StateCalls == 1 && float.IsNaN(h._controller.LastState.Axes![HMAxis.X]), "serialization failure changed SDK argument");
        Advance(); h.InstallClock(() => throw new Exception("clock-failure"), logs.Add); h.State(Env("elite"), badState);
        Check(h._controller.StateCalls == 2, "Begin diagnostic failure prevented SDK call");
        Advance(); e = Env(); h = new(e, true, logs.Add); var original = Enumerable.Range(0, 63).Select(n => (byte)n).ToArray();
        var expectedHash = Convert.ToHexString(SHA256.HashData(original)); h._controller!.DuringSubmit = () => original[0] = 255;
        h.Raw(e, original); Check(Last(logs).GetProperty("sdkArguments").GetProperty("sha256").GetString() == expectedHash, "snapshot taken after SDK");
        Pass("logger/serialization/clock failures isolated; snapshots precede SDK mutation");
    }
    private static void RateAndWindows()
    {
        Advance(10_000); FakeNow = FakeNow / 1000 * 1000; var start = FakeNow; var e = Env();
        var logs = new List<string>(); var h = new HostSession(e, true, logs.Add); var raw = new byte[63];
        var expectedFirst = new Dictionary<ulong, ulong>(); long total = 0, success = 0, failures = 0;
        for (var ms = 0; ms <= 20_000; ++ms)
        {
            FakeNow = start + ms;
            for (var sub = 0; sub < 10; ++sub)
            {
                var seq = (ulong)(ms * 10 + sub + 1); e = e with { Sequence = seq, SampleSequence = seq + 500, MonotonicTick = (ulong)FakeNow, Buttons = sub };
                expectedFirst.TryAdd(e.MonotonicTick / 1000, seq);
                if (ms % 123 == 0 && sub == 0) { h._controller!.Failure = new IOException("expected"); ++failures; } else { h._controller!.Failure = null; ++success; }
                try { h.Raw(e, raw); } catch (IOException) { } ++total;
            }
            // 1k/s internal traffic; all categories remain O(1), no live bucket consumption.
            h._controller!.Failure = null;
            h.Raw(e, raw, ms % 3 == 0 ? "internal-touch" : ms % 3 == 1 ? "internal-idle" : "release-neutral", e with { Buttons = 0 });
            ++total; ++success;
        }
        var entries = logs.Select(Parse).ToArray(); var live = entries.Where(j => j.GetProperty("joinEligible").GetBoolean()).ToArray();
        Check(live.Length == 21, "not one live sample per wire bucket");
        foreach (var j in live) Check(j.GetProperty("request").GetProperty("commandSequence").GetUInt64() == expectedFirst[j.GetProperty("sampleBucket").GetUInt64()], "not bucket's first command");
        Check(entries.GroupBy(j => j.GetProperty("tick").GetInt64() / 1000).All(g => g.Count() <= 2), "over 2 samples per second");
        var windows = entries.Where(j => j.GetProperty("window").ValueKind != JsonValueKind.Null).Select(j => j.GetProperty("window")).ToArray();
        Check(windows.Length == 4 && windows.All(w => w.GetProperty("elapsedMs").GetInt64() == 5000), "5 second windows missing");
        Check(windows.Sum(w => w.GetProperty("calls").GetInt64()) == 220001, "window call counts wrong");
        Check(windows.Sum(w => w.GetProperty("threw").GetInt64()) == failures, "window failures wrong");
        Check(windows.All(w => w.GetProperty("calls").GetInt64() == w.GetProperty("returned").GetInt64() + w.GetProperty("threw").GetInt64()), "window conservation");
        foreach (var category in new[] { "liveFrame", "internalTouch", "internalIdle", "neutral" })
            Check(windows.Sum(w => w.GetProperty(category).GetProperty("Calls").GetInt64()) > 0, "missing category " + category);
        Check(h._controller!.RawCalls == total, "high frequency SDK calls changed");
        File.WriteAllLines(Path.Combine(EvidenceDirectory, "highrate-samples.jsonl"), logs);
        Pass($"continuous high rate: {total} SDK calls; {entries.Length} samples, 21 exact first-live joins, four 5s windows, <=2 samples/fixed second");
        Advance(10_000); logs.Clear(); e = Env(); h = new(e, true, logs.Add);
        h.Raw(e, raw); var seq0 = e.Sequence;
        // Host clock changes alone cannot change the wire sampler's bucket or its first sequence.
        for (var i = 0; i < 20; i++) { Advance(); h.Raw(e with { Sequence = seq0 + (ulong)i + 1 }, raw); }
        Check(logs.Select(Parse).Count(j => j.GetProperty("joinEligible").GetBoolean()) == 1, "host clock used as wire clock");
        Check(logs.Select(Parse).Count(j => j.GetProperty("window").ValueKind != JsonValueKind.Null) == 4, "held-wire heartbeat missing");
        Advance(10_000); logs.Clear(); e = Env() with { MonotonicTick = 0 }; h = new(e, true, logs.Add); h.Raw(e, raw);
        Check(!Last(logs).GetProperty("wireTickKnown").GetBoolean() && Last(logs).GetProperty("wireMonotonicTick").ValueKind == JsonValueKind.Null, "unknown wire tick fabricated");
        Pass("wire bucket independent of Host clock; held-wire heartbeat; unknown tick remains unknown");
    }
    private static void IsolationAndAllocation()
    {
        Advance(10_000); var logs = new List<string>(); var e = Env(); var h = new HostSession(e, true, logs.Add); var raw = new byte[63]; h.Raw(e, raw);
        h.Toggle(false); Advance(10_000); for (var i = 0; i < 100; ++i) h.Raw(e, raw); Check(logs.Count == 1, "OFF wrote logs");
        h.Toggle(true); Advance(); e = e with { MonotonicTick = (ulong)FakeNow }; h.Raw(e, raw);
        Advance(5000); e = e with { MonotonicTick = (ulong)FakeNow }; h.Raw(e, raw);
        Check(Last(logs).GetProperty("window").GetProperty("calls").GetInt64() == 2, "OFF counters revived");
        Advance(); var n = e with { RunId = "new-run", Epoch = 22, PowerGeneration = 99, MonotonicTick = (ulong)FakeNow }; h.Rebuild(n); h.Raw(n, raw);
        Check(Last(logs).GetProperty("request").GetProperty("runId").GetString() == "new-run", "old binding resurrected");
        Advance(5000); n = n with { MonotonicTick = (ulong)FakeNow }; h.Raw(n, raw);
        Check(Last(logs).GetProperty("window").GetProperty("calls").GetInt64() == 2, "rebuild counters revived");
        Advance(); n = n with { MonotonicTick = (ulong)FakeNow }; var prior = logs.Count;
        h._controller!.DuringSubmit = () => h.Toggle(false); h.Raw(n, raw); Check(logs.Count == prior, "stale completion wrote after OFF");
        h._controller.DuringSubmit = null; h.Toggle(true); Advance(); n = n with { MonotonicTick = (ulong)FakeNow }; h.Raw(n, raw);
        Advance(); n = n with { MonotonicTick = (ulong)FakeNow }; prior = logs.Count;
        h._controller.DuringSubmit = () => h.Rebuild(n with { Epoch = 23 }); h.Raw(n, raw); Check(logs.Count == prior, "old target completion survived rebuild");
        Advance(10_000); e = Env(); logs.Clear(); h = new(e, true, logs.Add); h.Raw(e, raw);
        for (var i = 0; i < 20_000; i++) h.Raw(e, raw);
        var begin = GC.GetAllocatedBytesForCurrentThread(); for (var i = 0; i < 100_000; i++) h.Raw(e, raw); var rawAllocated = GC.GetAllocatedBytesForCurrentThread() - begin;
        Advance(); var elite = Env("elite"); var state = BuildEliteState(new HMProfile { Id = "xbox-elite-v2" }, elite); h = new(elite, true, logs.Add); h.State(elite, state);
        for (var i = 0; i < 20_000; i++) h.State(elite, state);
        begin = GC.GetAllocatedBytesForCurrentThread(); for (var i = 0; i < 100_000; i++) h.State(elite, state); var stateAllocated = GC.GetAllocatedBytesForCurrentThread() - begin;
        Check(rawAllocated == 0 && stateAllocated == 0, $"unsampled allocations raw={rawAllocated} state={stateAllocated}");
        Pass($"OFF/re-enable/rebuild/stale completion isolated; 100k unsampled raw/state calls allocate {rawAllocated}/{stateAllocated} bytes");
    }
    private static void GlobalQuotaAndArmedTracker()
    {
        Advance(10_000); var logs = new List<string>(); var e = Env(); var raw = new byte[63];
        var first = new HostSession(e, true, logs.Add); first.Raw(e, raw);
        var second = new HostSession(e with { Epoch = 90 }, true, logs.Add);
        second.Raw(e with { Epoch = 90, Sequence = 500 }, raw);
        second.Raw(e with { Epoch = 90, Sequence = 501 }, raw);
        Check(logs.Count == 1, "new instance bypassed process live quota or backfilled bucket");
        second.Raw(e with { Epoch = 90 }, raw, "internal-touch", e);
        Check(logs.Count == 2, "internal sample consumed live quota");
        Advance(); e = e with { Epoch = 90, MonotonicTick = (ulong)FakeNow, Sequence = 502 };
        second.Raw(e, raw); Check(Last(logs).GetProperty("request").GetProperty("commandSequence").GetUInt64() == 502, "next bucket first lost");
        // A stale wire bucket must never revive process-wide live admission.
        first.Raw(e with { Epoch = 11, MonotonicTick = (ulong)(FakeNow - 1000), Sequence = 503 }, raw);
        Check(logs.Count == 3, "old wire bucket revived");
        foreach (var persona in new[] { "dualsense-edge", "elite" })
        {
            Advance(); e = Env(persona); logs.Clear(); var h = new HostSession(e, true, logs.Add);
            var runtime = new MsiReportChainRuntime1410(clock: () => FakeNow,
                digest: _ => throw new IOException("tracker-digest"),
                formatter: _ => throw new IOException("tracker-formatter"), sink: _ => throw new IOException("tracker-sink"));
            var activation = new MsiReportChainActivation20261012(true, true, true, true, 42, e.RunId, e.Epoch, e.PowerGeneration, "fixture-session");
            Check(runtime.ArmSafely(new(activation, 10000, 128), 42, 43, h._hostInstanceId, e.Persona,
                e.ProfileIdentity, e.RunId, e.Epoch, e.PowerGeneration, "pnp-hid-ids=HID\\FIXTURE[HID\\VID_054C&PID_0DF2];", true), "real opt-in tracker did not arm");
            h._reportChain1410 = runtime; h._controller!.ExpectedRaw = raw;
            var state = BuildEliteState(new HMProfile { Id = "xbox-elite-v2" }, e);
            if (persona == "elite") h.State(e, state); else h.Raw(e, raw);
            Check(h._controller.RawCalls + h._controller.StateCalls == 1 && h._controller.SameRawStorage, "armed tracker changed call count/storage");
            Advance(); e = e with { MonotonicTick = (ulong)FakeNow }; var sdk = new IOException("armed-sdk-throw"); h._controller.Failure = sdk;
            if (persona == "elite") CatchSame(() => h.State(e, state), sdk); else CatchSame(() => h.Raw(e, raw), sdk);
            Check(h._controller.RawCalls + h._controller.StateCalls == 2 && Last(logs).GetProperty("outcome").GetString() == "threw", "armed tracker lost SDK outcome");
            h.Close();
        }
        Advance(); e = Env() with { RunId = new string('界', 160), RequestId = new string('界', 160), TargetId = new string('界', 160), OwnerIdentity = new string('界', 160) };
        logs.Clear(); var unicode = new HostSession(e, true, logs.Add); unicode.Raw(e, raw);
        Check(Last(logs).GetProperty("request").GetProperty("runId").GetString() == e.RunId && Encoding.UTF8.GetByteCount(logs[0]) + 1 <= 8192, "valid protocol identity truncated or oversized");
        Pass("process quota across instances/rebuilds, no later-command backfill, stale bucket rejected; armed real report-chain failures isolated; full protocol IDs preserved");
    }
    private static void RotationAndLineBound()
    {
        Advance(10_000); var dir = Path.Combine(EvidenceDirectory, "writer"); var e = Env(); var h = new HostSession(e, true, null, dir);
        var raw = new byte[512];
        var file = Path.Combine(dir, "input-host-signal.log"); var prev = file + ".prev";
        Check(!File.Exists(file) && !File.Exists(prev), "rotation test requires a fresh evidence directory");
        for (var i = 0; i < HostVirtualSignalEvidence.MaximumFileBytes / 512 + 2 && !File.Exists(prev); i++)
        { Advance(); e = e with { Sequence = (ulong)i + 1, MonotonicTick = (ulong)FakeNow }; h.Raw(e, raw); }
        Check(File.Exists(prev), "rotation absent");
        Check(new FileInfo(prev).Length >= HostVirtualSignalEvidence.MaximumFileBytes - HostVirtualSignalEvidence.MaximumLineBytes, "rotation did not cross the actual configured file bound");
        foreach (var p in new[] { file, prev })
        {
            Check(new FileInfo(p).Length <= HostVirtualSignalEvidence.MaximumFileBytes, "file grew unbounded");
            foreach (var line in File.ReadLines(p)) { Check(Encoding.UTF8.GetByteCount(line) + 1 <= 8192, "line >8KiB"); _ = Parse(line); }
        }
        var j = Parse(File.ReadLines(file).Last()); Check(j.GetProperty("sdkArguments").GetProperty("hexTruncated").GetBoolean() && j.GetProperty("sdkArguments").GetProperty("hex").GetString()!.Length == 256, "raw hex bound wrong");
        var logs = new List<string>(); Advance(); e = Env("elite"); h = new(e, true, logs.Add);
        var state = BuildEliteState(new HMProfile { Id = "xbox-elite-v2" }, e); for (var i = 50; i < 4000; i++) state.Axes![(HMAxis)i] = .5f;
        h.State(e, state); Check(Last(logs).GetProperty("sampleOmitted").GetString() == "line-byte-budget" && Encoding.UTF8.GetByteCount(logs[0]) + 1 <= 8192, "oversize fallback wrong");
        Pass("bounded .log/.prev rotation, every line parseable <=8KiB, raw hex truncation, oversized-state receipt explicit");
    }
    private static int Main(string[] args)
    {
        EvidenceDirectory = args[0]; Directory.CreateDirectory(EvidenceDirectory);
        try
        {
            SteamDeckActualBoundary(); SteamDeckRateAndIsolation(); ParametersAndExceptions(); ActualFrameAndNeutral(); DiagnosticFailures(); RateAndWindows(); IsolationAndAllocation(); GlobalQuotaAndArmedTracker(); RotationAndLineBound();
            File.WriteAllLines(Path.Combine(EvidenceDirectory, "sdk-parameter-samples.jsonl"), ParameterSamples);
            var result = new { pass = true, tests = Passed, sdkControllersConstructed = 0, devicesRead = 0, productDeployed = false, output = EvidenceDirectory };
            File.WriteAllText(Path.Combine(EvidenceDirectory, "result.json"), JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}

namespace YeManInputHost.Diagnostics
{
    // External SDK-clock boundary only. Actual TakeScreenPadReportOwnership reflection/seed consumption is compiled above.
    internal static class HostControllerIdleCtsRelease
    {
        internal static int Stops;
        internal static bool Allowed = true;
        internal static bool TryStopInputClock(global::HMController controller, out string detail)
        { ++Stops; detail = Allowed ? "fake-clock-stopped" : "fake-clock-stop-rejected"; return Allowed; }
    }
}
