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

// Exact approved SDK profile/catalog lookup is exercised on inert data shells.
// SDK target submit/idle-stop terminals remain fake; no SDK device is created.
internal static class ProfileData {
 private static readonly SdkDataOnlyProfiles Catalog = Load();
 private static readonly Dictionary<string,HMProfile> All = (Dictionary<string,HMProfile>)typeof(SdkDataOnlyProfiles).GetField("_profiles",BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(Catalog)!;
 private static readonly Dictionary<string,HMProfile> Bound = new(StringComparer.OrdinalIgnoreCase);
 private static SdkDataOnlyProfiles Load() { var data=new SdkDataOnlyProfiles();data.LoadDefaultProfiles();return data; }
 internal static HMProfile Stock(string id) => All[id];
 internal static HMProfile ForTarget(string id) {
  if(!Bound.TryGetValue(id,out var profile))Bound[id]=profile=Program.PatchProfileForFixture(Stock(id));
  return profile;
 }
 internal static HIDMaestro.HMContext CreateContext() {
  var context=(HIDMaestro.HMContext)RuntimeHelpers.GetUninitializedObject(typeof(HIDMaestro.HMContext));GC.SuppressFinalize(context);
  const BindingFlags flags=BindingFlags.Instance|BindingFlags.NonPublic;
  typeof(HIDMaestro.HMContext).GetField("_lock",flags)!.SetValue(context,new object());
  typeof(HIDMaestro.HMContext).GetField("_profilesById",flags)!.SetValue(context,All);
  typeof(HIDMaestro.HMContext).GetField("_disposed",flags)!.SetValue(context,false);
  return context;
 }
}
internal sealed class HMController
{
    public HMProfile Profile { get; }
    public HMController(HMProfile? profile=null) { Profile=profile??ProfileData.ForTarget("dualsense-edge"); }
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
    internal static HMProfile PatchProfileForFixture(HMProfile profile) => ApplySonyAxisDeclarationSwap(profile)!;
    private sealed class Observation { public bool IsAbsent = false; public string Detail = "fake-data-only"; }
    private class Fields
    {
        public HMContext? _context = ProfileData.CreateContext();
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
        protected HostSdkFirstWitness? _sdkFirstWitness;
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
            _controller = new(ProfileData.ForTarget(e.ProfileIdentity));
            _virtualSignalEvidence = new(dir ?? EvidenceDirectory, () => FakeNow, sink);
        }
        public void Raw(CommandEnvelope? e, byte[] bytes, string reason = "live-frame", CommandEnvelope? canonical = null) =>
            SubmitSonyReportChain1410(_controller!, e, bytes, 123, reason, canonical);
        public void State(CommandEnvelope? e, HMGamepadState state, string reason = "live-frame", CommandEnvelope? canonical = null) =>
            SubmitEliteReportChain1410(_controller!, state, e, reason, canonical);
        public bool Frame(CommandEnvelope e, out string detail) => TrySubmitFrame(e, out detail);
        public bool Neutral(CommandEnvelope? e, out string detail) => TrySubmitNeutral(out detail, e);
        public void Toggle(bool enabled) { _costFlagEnabled = enabled; if (!enabled) ResetVirtualSignalEvidenceSafely(); }
        public void Rebuild(CommandEnvelope e) { CloseReportChain1410Safely("target-prepare"); _controller = new(ProfileData.ForTarget(e.ProfileIdentity)); _tuple = e; _lastSequence = e.Sequence; }
        public void Close() => CloseReportChain1410Safely("release-local");
        public void InstallClock(Func<long> clock, Action<string> sink) => _virtualSignalEvidence = new(EvidenceDirectory, clock, sink);
        public HostVirtualSignalEvidence Collector => _virtualSignalEvidence!;
        public void BindSdkLifetime(string ordinal,string token)=>(_sdkFirstWitness??=new()).BindLifetime(_controller,new{creationOrdinal=ordinal,lifetimeToken=token,closed=false});
        public string SdkResponse(string response="{}")=>_sdkFirstWitness?.TryAppend(response,65536)??response;

        public void Sd(CommandEnvelope canonical, CommandEnvelope? cause, string reason = "live-frame", bool neutral = false) =>
            SubmitSteamDeckWithScreenPads(_controller!, _context!.GetProfile("steam-deck-composite"), canonical, _costFlagEnabled, neutral,
                signalSession: this, signalCause: cause, signalReason: reason);
    }
    private sealed class HostSessionBefore : Fields
    {
        /*ACTUAL_BASELINE*/
        public HostSessionBefore(CommandEnvelope e) { _tuple = e; _lastSequence = e.Sequence; _controller=new(ProfileData.ForTarget(e.ProfileIdentity)); }
        public void Raw(CommandEnvelope e, byte[] bytes) => SubmitSonyReportChain1410(_controller!, e, bytes, 123, "live-frame");
        public void State(CommandEnvelope e, HMGamepadState state) => SubmitEliteReportChain1410(_controller!, state, e, "live-frame");
        public bool Frame(CommandEnvelope e, out string detail) => TrySubmitFrame(e, out detail);
        public bool Neutral(CommandEnvelope? e, out string detail) => TrySubmitNeutral(out detail, e);
        public void Sd(CommandEnvelope canonical, bool neutral = false) =>
            SubmitSteamDeckWithScreenPadsBefore(_controller!, _context!.GetProfile("steam-deck-composite"), canonical, _costFlagEnabled, neutral);
    }
    private sealed class HostSessionResetBefore : Fields
    {
        private HostVirtualSignalEvidence? _virtualSignalEvidence;
        /*ACTUAL_RESET_BASELINE*/
        public HostSessionResetBefore(CommandEnvelope e, bool enabled, Action<string> sink)
        { _tuple = e; _lastSequence = e.Sequence; _costFlagEnabled = enabled; _virtualSignalEvidence = new(EvidenceDirectory, () => FakeNow, sink); }
        public void Raw(CommandEnvelope e, byte[] report) => SubmitSonyReportChain1410(_controller!, e, report, 123, "live-frame");
        public void State(CommandEnvelope e, HMGamepadState state) => SubmitEliteReportChain1410(_controller!, state, e, "live-frame");
        public void Toggle(bool enabled) { _costFlagEnabled = enabled; if (!enabled) ResetVirtualSignalEvidenceSafely(); }
    }
    private sealed class HostSessionProfileBefore : Fields {
        private HostVirtualSignalEvidence? _virtualSignalEvidence;
        /*ACTUAL_PROFILE_BASELINE*/
        public HostSessionProfileBefore(CommandEnvelope e,bool enabled,Action<string> sink) {
            _tuple=e;_lastSequence=e.Sequence;_costFlagEnabled=enabled;_controller=new(ProfileData.ForTarget(e.ProfileIdentity));
            _virtualSignalEvidence=new(EvidenceDirectory,()=>FakeNow,sink);
        }
        public bool Frame(CommandEnvelope e,out string detail)=>TrySubmitFrame(e,out detail);
        public bool Neutral(out string detail)=>TrySubmitNeutral(out detail);
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
        h._controller.DuringSubmit = () => h.Rebuild(live with { Epoch = 32 }); h.Sd(live, live); CheckOnlyLifecycleTails(logs, count, "SD stale completion survived rebuild");
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
            var state = BuildEliteState(ProfileData.Stock("xbox-elite-v2"), e);
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
        var badState = BuildEliteState(ProfileData.Stock("xbox-elite-v2"), e); badState.Axes![HMAxis.X] = float.NaN;
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
        h._controller.DuringSubmit = () => h.Rebuild(n with { Epoch = 23 }); h.Raw(n, raw); CheckOnlyLifecycleTails(logs, prior, "old target completion survived rebuild");
        Advance(10_000); e = Env(); logs.Clear(); h = new(e, true, logs.Add); h.Raw(e, raw);
        for (var i = 0; i < 20_000; i++) h.Raw(e, raw);
        var begin = GC.GetAllocatedBytesForCurrentThread(); for (var i = 0; i < 100_000; i++) h.Raw(e, raw); var rawAllocated = GC.GetAllocatedBytesForCurrentThread() - begin;
        Advance(); var elite = Env("elite"); var state = BuildEliteState(ProfileData.Stock("xbox-elite-v2"), elite); h = new(elite, true, logs.Add); h.State(elite, state);
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
            var state = BuildEliteState(ProfileData.Stock("xbox-elite-v2"), e);
            if (persona == "elite") h.State(e, state); else h.Raw(e, raw);
            Check(h._controller.RawCalls + h._controller.StateCalls == 1 && h._controller.SameRawStorage, "armed tracker changed call count/storage");
            Advance(); e = e with { MonotonicTick = (ulong)FakeNow }; var sdk = new IOException("armed-sdk-throw"); h._controller.Failure = sdk;
            if (persona == "elite") CatchSame(() => h.State(e, state), sdk); else CatchSame(() => h.Raw(e, raw), sdk);
            Check(h._controller.RawCalls + h._controller.StateCalls == 2 && Last(logs).GetProperty("outcome").GetString() == "threw", "armed tracker lost SDK outcome");
            h.Close();
        }
        foreach (char c in new[] { 'a', '界' }) {
            Advance(); e = Env() with { RunId = new string(c, 160), RequestId = new string(c, 160), TargetId = new string(c, 160), OwnerIdentity = new string(c, 160) };
            logs.Clear(); var unicode = new HostSession(e, true, logs.Add); unicode.Raw(e, raw);
            Check(unicode._controller!.RawCalls == 1, "maximum protocol identity suppressed SDK submission");
            if (c == 'a') {
                Check(logs.Count == 1 && Last(logs).GetProperty("request").GetProperty("runId").GetString() == e.RunId &&
                    Last(logs).GetProperty("request").GetProperty("requestId").GetString() == e.RequestId &&
                    Encoding.UTF8.GetByteCount(logs[0]) + 1 <= 8192, "in-budget maximum protocol identity truncated or oversized");
            } else {
                // Current production explicitly refuses an oversized summary
                // and retains the input identity plus cumulative loss in memory.
                // The old unconditional disk-row requirement remains a baseline
                // FAIL receipt, not newly passing under the original assertion.
                var pending = JsonSerializer.SerializeToElement(unicode.Collector.SnapshotPendingSafely(), new JsonSerializerOptions { IncludeFields = true });
                Check(logs.Count == 0 && pending.GetProperty("tailNotFlushed").GetBoolean() &&
                    pending.GetProperty("tailPendingAttempts").GetInt64() == 1, "oversize summary lost pending evidence or wrote an unbounded row");
                var request = pending.GetProperty("request");
                Check(request.GetProperty("runId").GetString() == e.RunId && request.GetProperty("targetId").GetString() == e.TargetId &&
                    request.GetProperty("ownerIdentity").GetString() == e.OwnerIdentity &&
                    pending.GetProperty("parameterWindow").GetProperty("first").GetProperty("requestId").GetString() == e.RequestId,
                    "oversize valid protocol identities not retained in original memory snapshot");
                var loss = pending.GetProperty("loss");
                Check(loss.GetProperty("samplesOmittedForLineBudget").GetInt64() == 1 && loss.GetProperty("oversizeEmissionFailures").GetInt64() == 1,
                    "oversize refusal did not retain exact sample/summary failure counters");
                unicode.Toggle(false);
                var reset = JsonSerializer.SerializeToElement(unicode.Collector.SnapshotPendingSafely(), new JsonSerializerOptions { IncludeFields = true });
                Check(reset.GetProperty("loss").GetProperty("oversizeEmissionFailures").GetInt64() == 1 &&
                    reset.GetProperty("loss").GetProperty("resetDiscardedAttempts").GetInt64() == 1, "OFF erased retained oversize/discard evidence");
            }
        }
        Pass("process quota/stale buckets/tracker isolation; maximum protocol IDs preserved on emitted row or explicit in-memory loss/refusal boundary");
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
        var state = BuildEliteState(ProfileData.Stock("xbox-elite-v2"), e); for (var i = 50; i < 4000; i++) state.Axes![(HMAxis)i] = .5f;
        h.State(e, state); Check(Last(logs).GetProperty("sampleOmitted").GetString() == "line-byte-budget" && Encoding.UTF8.GetByteCount(logs[0]) + 1 <= 8192, "oversize fallback wrong");
        Pass("bounded .log/.prev rotation, every line parseable <=8KiB, raw hex truncation, oversized-state receipt explicit");
    }
    private static void CheckOnlyLifecycleTails(List<string> logs, int before, string reason)
    {
        // Current production resets flush already observed parameter tails.
        // A tail is not another SDK attempt or a stale frame completion.
        foreach (string line in logs.Skip(before)) {
            var entry = Parse(line);
            Check(entry.TryGetProperty("kind", out var kind) && kind.GetString() == "host-virtual-signal-tail" &&
                entry.GetProperty("sdkAttempt").ValueKind == JsonValueKind.False &&
                entry.GetProperty("joinEligible").ValueKind == JsonValueKind.False, reason);
        }
    }
    private static void ResetOnceAndLateCompletion()
    {
        Advance(10_000); var e = Env(); var raw = new byte[63]; var logs = new List<string>();
        var h = new HostSession(e, true, logs.Add); h.Raw(e, raw);
        var generation = typeof(HostVirtualSignalEvidence).GetField("_generation", BindingFlags.NonPublic | BindingFlags.Instance)!;
        long Before() => (long)generation.GetValue(h.Collector)!;
        long active = Before(); h.Toggle(false); long reset = Before();
        Check(reset == active + 1, "disabled transition must invalidate actual pending attempt once");
        var pendingAfterReset = JsonSerializer.Serialize(h.Collector.SnapshotPendingSafely());
        for (int i = 0; i < 1000; ++i) h.Raw(e, raw);
        Check(Before() == reset, "already-reset disabled stream repeated diagnostic clear");
        Check(JsonSerializer.Serialize(h.Collector.SnapshotPendingSafely()) == pendingAfterReset,
            "disabled reports changed retained loss or pending evidence");
        // Explicit lifecycle/flag-edge cleanup remains permitted and unchanged.
        h.Close(); h.Toggle(false);
        h.Toggle(true); Advance(); e = e with { MonotonicTick = (ulong)FakeNow }; h.Raw(e, raw);
        var logCount = logs.Count;
        h._controller!.DuringSubmit = () => { h.Toggle(false); h.Toggle(true); };
        Advance(); e = e with { MonotonicTick = (ulong)FakeNow }; h.Raw(e, raw);
        Check(logs.Count == logCount, "disabled then re-enabled stale completion emitted old SDK attempt");
        h._controller.DuringSubmit = null;
        h.Close(); h.Toggle(false);
        Pass("one reset per enabled diagnostic activity; disabled repeated SDK submission preserves counters; OFF/ON late completion invalidated");
    }
    [DllImport("kernel32.dll")] private static extern IntPtr GetCurrentThread();
    [DllImport("kernel32.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool QueryThreadCycleTime(IntPtr thread, out ulong cycles);
    private static ulong ThreadCycles() => QueryThreadCycleTime(GetCurrentThread(), out ulong value) ? value : throw new Exception("thread cycles unavailable");
    private sealed record Cost(double CyclesPerCall, double NanosecondsPerCall, long AllocatedBytes);
    private static Cost MeasureBoundary(Action submit, int calls)
    {
        long alloc = GC.GetAllocatedBytesForCurrentThread(); ulong cycles = ThreadCycles(); long tick = Stopwatch.GetTimestamp();
        for (int i = 0; i < calls; ++i) submit();
        double ns = Stopwatch.GetElapsedTime(tick).TotalNanoseconds / calls;
        ulong spent = ThreadCycles() - cycles;
        return new((double)spent / calls, ns, GC.GetAllocatedBytesForCurrentThread() - alloc);
    }
    private static double Median(IEnumerable<double> data) { var a = data.Order().ToArray(); return a[a.Length / 2]; }
    private static int MeasureReset()
    {
        const int pairs = 11, calls = 200_000;
        var rows = new List<object>(); bool all = true;
        foreach (bool typed in new[] { false, true }) foreach (bool enabled in new[] { false, true })
        {
            Advance(10_000); var e = Env(typed ? "elite" : "dualsense-edge"); var raw = new byte[63];
            var state = BuildEliteState(ProfileData.Stock("xbox-elite-v2"), e);
            var candidate = new HostSession(e, true, static _ => { });
            var before = new HostSessionResetBefore(e, true, static _ => { });
            Action candidateSubmit = typed ? () => candidate.State(e, state) : () => candidate.Raw(e, raw);
            Action beforeSubmit = typed ? () => before.State(e, state) : () => before.Raw(e, raw);
            candidateSubmit(); beforeSubmit();
            if (!enabled) { candidate.Toggle(false); before.Toggle(false); }
            for (int i = 0; i < 60_000; ++i) { beforeSubmit(); candidateSubmit(); }
            var b = new List<Cost>(); var c = new List<Cost>(); var legs = new List<object>();
            for (int round = 0; round < pairs; ++round)
            {
                Cost baseline, updated;
                if ((round & 1) == 0) { baseline = MeasureBoundary(beforeSubmit, calls); updated = MeasureBoundary(candidateSubmit, calls); }
                else { updated = MeasureBoundary(candidateSubmit, calls); baseline = MeasureBoundary(beforeSubmit, calls); }
                b.Add(baseline); c.Add(updated); legs.Add(new { round, baseline, candidate = updated, candidateFirst = (round & 1) != 0 });
            }
            var reduction = Median(b.Zip(c, (a, z) => 100.0 * (a.CyclesPerCall - z.CyclesPerCall) / a.CyclesPerCall));
            var ratioReduction = 100.0 * (Median(b.Select(v => v.CyclesPerCall)) - Median(c.Select(v => v.CyclesPerCall))) / Median(b.Select(v => v.CyclesPerCall));
            int faster = b.Zip(c).Count(v => v.Second.CyclesPerCall < v.First.CyclesPerCall);
            bool allocations = b.Zip(c).All(v => v.Second.AllocatedBytes <= v.First.AllocatedBytes);
            bool passed = allocations && (enabled ? reduction >= -5 && ratioReduction >= -5 : reduction >= 5 && ratioReduction >= 5 && faster >= 9);
            all &= passed;
            rows.Add(new { typed, enabled, callsPerLeg = calls, pairs, baselineCycles = Median(b.Select(v => v.CyclesPerCall)), candidateCycles = Median(c.Select(v => v.CyclesPerCall)),
                pairedMedianReductionPercent = reduction, medianRatioReductionPercent = ratioReduction, fasterPairs = faster, allocationsNonincreasing = allocations, passed, legs });
            Check(candidate._controller!.RawCalls + candidate._controller.StateCalls == before._controller!.RawCalls + before._controller.StateCalls,
                "benchmark did not preserve actual SDK submission count");
        }
        var result = new { pass = all, rows, rawAndTypedSdkArgumentBoundary = true, allOriginalDiagnosticsWhenEnabled = true,
            realThreadCpuCycles = true, fakeSdkTerminal = true, cadenceChanged = false, processCpuOrEndToEndLatencyProven = false };
        File.WriteAllText(Path.Combine(EvidenceDirectory, "reset-performance.json"), JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
        return all ? 0 : 2;
    }

    private static void SameIndependentlyBuiltState(HMGamepadState a,HMGamepadState b) {
        Check(a.Axes is not null && b.Axes is not null && !ReferenceEquals(a.Axes,b.Axes),"separate report builders unexpectedly share mutable axes");
        Check(a.Buttons==b.Buttons && a.Hat==b.Hat && a.Axes!.Count==b.Axes!.Count,"independent state shape changed");
        foreach(var axis in a.Axes)Check(b.Axes!.TryGetValue(axis.Key,out var other) && BitConverter.SingleToInt32Bits(axis.Value)==BitConverter.SingleToInt32Bits(other),"independent state axis bits changed");
        Check(JsonSerializer.Serialize(a,new JsonSerializerOptions{IncludeFields=true})==JsonSerializer.Serialize(b,new JsonSerializerOptions{IncludeFields=true}),"independent state fields changed");
    }
    private static void ProfileReadyStates()
    {
        foreach (string persona in new[] { "steamdeck", "dualsense-edge", "elite" }) foreach(bool enabled in new[]{false,true}) {
            Advance(10_000); var e=Env(persona); var after=new HostSession(e,enabled,static _=>{}); var before=new HostSessionProfileBefore(e,enabled,static _=>{});
            var bound=after._controller!.Profile;var stock=after._context!.GetProfile(e.ProfileIdentity)!;
            Check(bound.Id==stock.Id && bound.InputReportSize==stock.InputReportSize,"bound profile metadata changed");
            Check(persona=="dualsense-edge" ? !ReferenceEquals(bound,stock) && bound.DescriptorHex!=stock.DescriptorHex : ReferenceEquals(bound,stock),"actual selected profile identity proof");
            ResetSd(before._controller!);_dualSenseFrameCounter=253;
            Check(before.Frame(e,out var bd),"baseline ready frame refused");var bc=_screenPadPacketCounter;
            ResetSd(after._controller!);_dualSenseFrameCounter=253;
            Check(after.Frame(e,out var ad)&&ad==bd && _screenPadPacketCounter==bc,"bound profile changed frame result/counter");
            Check(after._controller.RawCalls==before._controller!.RawCalls && after._controller.StateCalls==before._controller.StateCalls,"bound profile changed SDK count");
            if(persona=="elite") SameIndependentlyBuiltState(after._controller.LastState,before._controller.LastState);
            else Check(after._controller.LastRawLength==before._controller.LastRawLength && after._controller.LastRaw.AsSpan(0,after._controller.LastRawLength).SequenceEqual(before._controller.LastRaw.AsSpan(0,before._controller.LastRawLength)),"bound profile changed raw frame bytes");
            ResetSd(before._controller);_dualSenseFrameCounter=255;Check(before.Neutral(out bd),"baseline neutral refused");bc=_screenPadPacketCounter;
            ResetSd(after._controller);_dualSenseFrameCounter=255;Check(after.Neutral(null,out ad)&&ad==bd && bc==_screenPadPacketCounter,"bound profile changed neutral result/counter");
            if(persona=="elite") SameIndependentlyBuiltState(after._controller.LastState,before._controller.LastState);
            else Check(after._controller.LastRaw.AsSpan(0,after._controller.LastRawLength).SequenceEqual(before._controller.LastRaw.AsSpan(0,before._controller.LastRawLength)),"bound profile changed neutral bytes");
            // Reachable context-teardown state: target reference already detached.
            after._controller=null;before._controller=null;
            typeof(HIDMaestro.HMContext).GetField("_disposed",BindingFlags.Instance|BindingFlags.NonPublic)!.SetValue(after._context,true);
            typeof(HIDMaestro.HMContext).GetField("_disposed",BindingFlags.Instance|BindingFlags.NonPublic)!.SetValue(before._context,true);
            Check(!after.Frame(e,out ad)&&!before.Frame(e,out bd)&&ad=="target-not-ready"&&bd==ad,"detached target allowed disposed-context submission");
            Check(!after.Neutral(null,out ad)&&!before.Neutral(out bd)&&ad=="target-not-ready"&&bd==ad,"detached target allowed disposed-context neutral");
        }
        Pass("actual SDK catalog vs readonly selected profile: six live/neutral byte-state/counter cases, patched Edge, detached target/context guard preserved");
    }
    private static int MeasureProfiles()
    {
        const int pairs=11,calls=100_000;var rows=new List<object>();bool all=true;
        foreach(string persona in new[]{"steamdeck","dualsense-edge","elite"})foreach(bool enabled in new[]{false,true}) {
            Advance(10_000);var e=Env(persona);var after=new HostSession(e,enabled,static _=>{});var before=new HostSessionProfileBefore(e,enabled,static _=>{});
            ResetSd(after._controller!); // Both variants use one serialized actual SD buffer/clock ownership.
            Action a=()=>{if(!after.Frame(e,out _))throw new Exception("candidate frame refused");};
            Action b=()=>{if(!before.Frame(e,out _))throw new Exception("baseline frame refused");};
            for(int i=0;i<60_000;i++){b();a();}
            var aa=new List<Cost>();var bb=new List<Cost>();var legs=new List<object>();
            for(int round=0;round<pairs;round++) {
                Cost oldCost,newCost;
                if((round&1)==0){oldCost=MeasureBoundary(b,calls);newCost=MeasureBoundary(a,calls);}
                else{newCost=MeasureBoundary(a,calls);oldCost=MeasureBoundary(b,calls);}
                bb.Add(oldCost);aa.Add(newCost);legs.Add(new{round,baseline=oldCost,candidate=newCost,candidateFirst=(round&1)!=0});
            }
            double improvement=Median(bb.Zip(aa,(x,y)=>100*(x.CyclesPerCall-y.CyclesPerCall)/x.CyclesPerCall));
            double ratio=100*(Median(bb.Select(v=>v.CyclesPerCall))-Median(aa.Select(v=>v.CyclesPerCall)))/Median(bb.Select(v=>v.CyclesPerCall));
            int faster=bb.Zip(aa).Count(v=>v.Second.CyclesPerCall<v.First.CyclesPerCall);
            bool allocated=bb.Zip(aa).All(v=>v.Second.AllocatedBytes<=v.First.AllocatedBytes);
            double elapsedRegression=100*(Median(aa.Select(v=>v.NanosecondsPerCall))/Median(bb.Select(v=>v.NanosecondsPerCall))-1);
            bool passed=improvement>=3 && ratio>=3 && faster>=9 && allocated && elapsedRegression<=5;
            all&=passed;
            Check(after._controller!.RawCalls+after._controller.StateCalls==before._controller!.RawCalls+before._controller.StateCalls,"profile benchmark SDK count differs");
            rows.Add(new{persona,enabled,pairs,callsPerLeg=calls,baselineCycles=Median(bb.Select(v=>v.CyclesPerCall)),candidateCycles=Median(aa.Select(v=>v.CyclesPerCall)),pairedMedianReductionPercent=improvement,medianRatioReductionPercent=ratio,fasterPairs=faster,allocationsNonincreasing=allocated,elapsedRegressionPercent=elapsedRegression,passed,legs});
        }
        var result=new{pass=all,rows,realSdkGetProfile=true,profileCatalogCount=337,inertContextNoConstructors=true,fakeSdkSubmitTerminal=true,fullOriginalTrySubmitFrame=true,
            scope="original TrySubmitFrame/encoder/diagnostic wrappers; exact SDK profile lookup and actual data model; fake final device-submit and cost-role/frame ports; not parse/pipe/driver/game/process CPU",sharedSourceChanged=false};
        File.WriteAllText(Path.Combine(EvidenceDirectory,"profile-performance.json"),JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));return all?0:2;
    }
    private static void FirstSdkWitnessReplies()
    {
        var e=Env() with {Buttons=4096,Lx=0,Ly=0,Rx=0,Ry=0,Lt=0,Rt=0,BackButtons=0,ScreenButtons=0};
        var session=new HostSession(e,false,null);session.BindSdkLifetime("1","fixture-life-1");
        var raw=new byte[]{0x11,0x22,0x33};session.Raw(e,raw);
        var response=session.SdkResponse();using var doc=JsonDocument.Parse(response);
        var witness=doc.RootElement.GetProperty("sdkFirstWitnesses")[0];var sample=witness.GetProperty("sample");
        Check(witness.GetProperty("outcome").GetString()=="returned"&&sample.GetProperty("sdkArguments").GetProperty("hex").GetString()=="112233","actual SDK argument/return missing");
        Check(sample.GetProperty("lifetimeKnown").GetBoolean()&&sample.GetProperty("targetCreationLifetimeEvidence").GetProperty("lifetimeToken").GetString()=="fixture-life-1","creation lifetime anchor missing");
        Check(!response.Contains("fixture-nonce")&&!sample.GetProperty("request").TryGetProperty("startupNonce",out _),"SDK first witness retained transport auth");
        File.WriteAllText(Path.Combine(EvidenceDirectory,"sdk-first-response.json"),response);
        for(int i=0;i<50000;++i)session.Raw(e,raw);
        Check(session._controller!.RawCalls==50001&&session.SdkResponse()=="{}","repeated controls produced more first witnesses or changed SDK calls");
        var neutral=e with {Buttons=0};session.Raw(neutral,new byte[]{0,0,0});
        using(var zero=JsonDocument.Parse(session.SdkResponse()))Check(zero.RootElement.GetProperty("sdkFirstWitnesses").GetArrayLength()==1,"first neutral missing");
        session.Rebuild(e);session.BindSdkLifetime("2","fixture-life-2");session.Raw(e,raw);
        using(var rebuilt=JsonDocument.Parse(session.SdkResponse()))Check(rebuilt.RootElement.GetProperty("sdkFirstWitnesses")[0].GetProperty("sample").GetProperty("targetCreationLifetimeEvidence").GetProperty("lifetimeToken").GetString()=="fixture-life-2","same tuple new creation was suppressed");
        session.Rebuild(e);session.BindSdkLifetime("3","fixture-life-3");session._controller!.Failure=new InvalidOperationException("fake-sdk-failure");
        bool propagated=false;try{session.Raw(e,raw);}catch(InvalidOperationException){propagated=true;}
        using(var failed=JsonDocument.Parse(session.SdkResponse()))Check(propagated&&failed.RootElement.GetProperty("sdkFirstWitnesses")[0].GetProperty("outcome").GetString()=="threw","SDK exception changed or was labelled success");
        session._controller!.Failure=null;session.Raw(e,raw);
        using(var returned=JsonDocument.Parse(session.SdkResponse())){
            var row=returned.RootElement.GetProperty("sdkFirstWitnesses")[0];
            Check(row.GetProperty("outcome").GetString()=="returned"&&!row.GetProperty("firstAttempt").GetBoolean()&&row.GetProperty("firstReturned").GetBoolean(),"first throw suppressed later first returned SDK call");
        }
        session.Rebuild(e);session.BindSdkLifetime("4","fixture-life-4");session._controller!.Failure=null;session.Raw(e,raw);
        Check(session.SdkResponse(new string('x',65536)+"}")==new string('x',65536)+"}","oversize diagnostics altered original response");
        using(var retained=JsonDocument.Parse(session.SdkResponse()))Check(retained.RootElement.GetProperty("sdkFirstWitnessCoverage").GetProperty("emissionFailures").GetInt64()==1,"oversize reply consumed pending evidence");
        Pass("FirstSdkWitnessReplies: detail-off,50000 repeats,actual arguments,creation lifetime,SDK throw,no transport auth");
    }
    private static void FirstSdkWitnessLossAndBudget()
    {
        var e=Env() with {Buttons=4096,Lx=0,Ly=0,Rx=0,Ry=0,Lt=0,Rt=0,BackButtons=0,ScreenButtons=0};
        var controller=new object();var helper=new HostSdkFirstWitness();helper.BindLifetime(controller,new{lifetimeToken="loss-life",closed=false});
        var attempt=helper.Begin("fixture-host",controller,e,e,1,true,"live-frame",null,new byte[]{1,2,3},null);
        var gate=typeof(HostSdkFirstWitness).GetField("_gate",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic)!.GetValue(helper)!;
        using var held=new System.Threading.ManualResetEventSlim();using var release=new System.Threading.ManualResetEventSlim();
        var blocker=new System.Threading.Thread(()=>{lock(gate){held.Set();release.Wait(TimeSpan.FromSeconds(5));}});blocker.IsBackground=true;blocker.Start();
        try{Check(held.Wait(TimeSpan.FromSeconds(5)),"contention fixture did not acquire gate");helper.Complete(attempt,null);}
        finally{release.Set();Check(blocker.Join(TimeSpan.FromSeconds(5)),"contention fixture thread leaked");}
        Check(helper.HasPending,"dropped first sample did not mark coverage pending");
        using(var lost=JsonDocument.Parse(helper.TryAppend("{}",16384)))Check(lost.RootElement.GetProperty("sdkFirstWitnessCoverage").GetProperty("dropped").GetInt64()==1&&lost.RootElement.GetProperty("sdkFirstWitnesses").GetArrayLength()==0,"drop without another input type was invisible");
        helper=new HostSdkFirstWitness();helper.BindLifetime(controller,new{lifetimeToken="batch-life",closed=false});
        for(ulong i=1;i<=17;++i){var binding=e with {Epoch=e.Epoch+i};var a=helper.Begin("fixture-host",controller,binding,binding,i,true,"live-frame",null,new byte[512],null);helper.Complete(a,null);}
        var total=0;for(var i=0;i<16;++i){using var part=JsonDocument.Parse(helper.TryAppend("{}",16384));total+=part.RootElement.GetProperty("sdkFirstWitnesses").GetArrayLength();Check(part.RootElement.GetProperty("sdkFirstWitnessCoverage").GetProperty("dropped").GetInt64()==1,"pending full omission not reported");}
        Check(total==16&&!helper.HasPending,"bounded batch did not drain one actual witness per existing ACK");
        helper=new HostSdkFirstWitness();helper.BindLifetime(controller,new{lifetimeToken="budget-life",closed=false});
        attempt=helper.Begin("fixture-host",controller,e,e,1,true,"live-frame",null,new byte[512],null);helper.Complete(attempt,null);
        var crowded=new string('x',1000)+"}";
        for(var i=0;i<4;++i)Check(helper.TryAppend(crowded,1024)==crowded,"budget deferral changed original response");
        Check(!helper.HasPending,"permanent budget shortage retained a per-frame hot path");
        var allocated=GC.GetAllocatedBytesForCurrentThread();for(var i=0;i<50000;++i)helper.TryAppend(crowded,1024);
        Check(GC.GetAllocatedBytesForCurrentThread()==allocated,"budget-exhausted path allocated on later frames");
        helper.BindLifetime(controller,new{lifetimeToken="budget-next-life",closed=false});
        attempt=helper.Begin("fixture-host",controller,e,e,1,true,"live-frame",null,new byte[]{4},null);helper.Complete(attempt,null);
        using(var next=JsonDocument.Parse(helper.TryAppend("{}",16384)))Check(next.RootElement.GetProperty("sdkFirstWitnessCoverage").GetProperty("emissionFailures").GetInt64()==4&&next.RootElement.GetProperty("sdkFirstWitnessCoverage").GetProperty("dropped").GetInt64()==1,"next lifetime lost bounded-budget omissions");
        helper=new HostSdkFirstWitness();helper.BindLifetime(controller,new{lifetimeToken="repeated-throw-budget",closed=false});
        var repeatedFailure=new InvalidOperationException("observer-only-failure");
        for(var i=0;i<4;++i){
            attempt=helper.Begin("fixture-host",controller,e,e,(ulong)i+1,true,"live-frame",null,new byte[512],null);helper.Complete(attempt,repeatedFailure);
            Check(helper.TryAppend(crowded,1024)==crowded,"repeated failure modified original response");
        }
        Check(!helper.HasPending,"repeated throws refunded the same pending batch budget");
        Pass("FirstSdkWitnessLossAndBudget: first-throw-then-returned,drop-only-coverage,16-sample-batch,4-deferrals,50000 no-allocation frames");
    }
    private static int Main(string[] args)
    {
        EvidenceDirectory = args[0]; Directory.CreateDirectory(EvidenceDirectory);
        if (args.Length == 2 && args[1] == "measure-reset") return MeasureReset();
        if (args.Length == 2 && args[1] == "measure-profiles") return MeasureProfiles();
        try
        {
            FirstSdkWitnessReplies(); FirstSdkWitnessLossAndBudget(); ResetOnceAndLateCompletion(); SteamDeckActualBoundary(); SteamDeckRateAndIsolation(); ParametersAndExceptions(); ActualFrameAndNeutral(); DiagnosticFailures(); RateAndWindows(); IsolationAndAllocation(); GlobalQuotaAndArmedTracker(); RotationAndLineBound(); ProfileReadyStates();
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
