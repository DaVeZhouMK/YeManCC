// Fake-only .NET fixture. Current Host methods are extracted as data and
// compiled with inert types. NO HIDMaestro reference or SDK/device construction.
using System.Security.Cryptography;
using System.Text.Json;
using YeManInputHost.Diagnostics;

internal static class MsiReportChainSelftest20261012
{
    private static int _tests;
    private static void Check(bool ok, string message = "assertion") { if (!ok) throw new Exception(message); }
    private static void Test(string name, Action body) { body(); ++_tests; Console.WriteLine("PASS " + name); }
    private static MsiReportChainTuple20261012 Tuple(string run = "fake-run") =>
        new(true, 9844, 2444, run, 2, 1, "dualsense-edge", "dualsense-edge", "fake-host", "HID\\HIDCLASS\\fake-child");
    private static MsiReportChainFrame20261012 Frame(MsiReportChainTuple20261012 tuple, ulong seq,
        string reason = "live-frame", uint? source = 0x1000, long? encoder = null) => new(tuple, seq, seq + 100, 6, encoder ?? (long)seq, reason, source);
    private static MsiReportChainActivation20261012 Activation(MsiReportChainTuple20261012 t) =>
        new(true, true, true, true, t.NativePid, t.RunId, t.Epoch, t.PowerGeneration, "explicit-fake-session");
    private static byte[] Raw(int buttons = 0, long index = 0) => ExtractedHostBuilder20261012.Build(new FakeFrame { Buttons = buttons }, index);
    private static string Sha(byte[] data) => Convert.ToHexString(SHA256.HashData(data));
    private static MsiPersonaReportChain20261012 Armed(MsiReportChainTuple20261012? tuple = null, Func<byte[], string>? digest = null,
        Func<MsiReportChainEntry20261012, string>? formatter = null, Action<string>? logger = null)
    {
        var t = new MsiPersonaReportChain20261012(() => 0, digest, formatter, logger); var boundTuple = tuple ?? Tuple(); Check(t.Arm(boundTuple, Activation(boundTuple))); return t;
    }
    private sealed class FakeHostCounts
    {
        public int Calls, Ok, Fail;
        public void Submit(MsiPersonaReportChain20261012 trace, MsiReportChainFrame20261012 frame, byte[] raw, Exception? error = null)
        {
            try { trace.InvokeRaw(frame, raw, bytes => { ++Calls; if (error != null) throw error; }); ++Ok; }
            catch { ++Fail; }
        }
    }
    public static int Main(string[] args)
    {
        Test("extracted actual Host neutral is data-only63, center128, hat8, inactive contacts", () => {
            var raw = Raw(); Check(raw.Length == 63 && raw.Take(4).All(x => x == 128));
            Check(raw[7] == 8 && raw[32] == 0x81 && raw[36] == 0x82 && raw[52] == 10);
        });
        Test("locked Edge defaults agree with data-only minus-one projection", () => {
            var profilePath = args.Length == 1 ? args[0] : throw new Exception("Pinned Edge JSON path required");
            using var p = JsonDocument.Parse(File.ReadAllText(profilePath));
            Check(p.RootElement.GetProperty("extendedReport").GetProperty("size").GetInt32() == 64);
            Check(!p.RootElement.GetProperty("extendedReport").TryGetProperty("idleFrameIntervalMs", out _));
            var raw = Raw(0x1000, 42); var expected = (byte[])raw.Clone();
            foreach (var patch in p.RootElement.GetProperty("inputDefaults").EnumerateArray())
                expected[patch.GetProperty("byte").GetInt32() - 1] = (byte)patch.GetProperty("value").GetInt32();
            Check(expected.SequenceEqual(MsiPersonaReportChain20261012.ProjectLockedEdgeOverlay(raw)));
            Check(!expected.SequenceEqual(raw));
        });
        Test("held current Host button does not alternate neutral/live or create repeated edges", () => {
            int calls = 0, logs = 0; var trace = Armed(formatter: e => JsonSerializer.Serialize(e), logger: _ => ++logs); var t = Tuple();
            trace.InvokeRaw(Frame(t, 1, source: 0), Raw(), _ => ++calls);
            for (ulong i = 2; i <= 127; ++i) trace.InvokeRaw(Frame(t, i), Raw(0x1000, (long)i), _ => ++calls);
            trace.InvokeRaw(Frame(t, 128, source: 0), Raw(0, 128), _ => ++calls);
            var snap = trace.Snapshot(); Check(calls == 128 && logs == 3 && snap.Entries.Length == 3);
            Check(snap.Entries.Count(e => e.PressMask is > 0) == 1 && snap.Entries.Count(e => e.ReleaseMask is > 0) == 1);
            Check(snap.UnchangedReportsSuppressed == 125);
        });
        Test("actual submit argument digest differs from predicted overlay and predicted driver wire", () => {
            var t = Tuple(); var trace = Armed(); var raw = Raw(0x1000, 7); byte[]? argument = null;
            trace.InvokeRaw(Frame(t, 72, encoder: 7), raw, x => argument = x);
            var e = trace.Snapshot().Entries.Single(); Check(ReferenceEquals(argument, raw));
            Check(e.EncodedArgumentSha256 == Sha(raw)); Check(e.PredictedEdgeOverlaySha256 != e.EncodedArgumentSha256);
            var wire = new byte[64]; wire[0] = 1; MsiPersonaReportChain20261012.ProjectLockedEdgeOverlay(raw).CopyTo(wire, 1);
            Check(e.PredictedDriverWireSha256 == Sha(wire)); Check(e.DeliveryScope.Contains("not-ACK"));
        });
        Test("deliberately injected neutral/live alternation is visible, not called a hardware bug", () => {
            var t = Tuple(); var trace = Armed();
            trace.InvokeRaw(Frame(t, 1, source: 0), Raw(), _ => { });
            trace.InvokeRaw(Frame(t, 2), Raw(0x1000, 2), _ => { });
            trace.InvokeRaw(Frame(t, 3, "submit-neutral-command"), Raw(0, 3), _ => { });
            trace.InvokeRaw(Frame(t, 4), Raw(0x1000, 4), _ => { });
            var s = trace.Snapshot(); Check(s.Entries.Count(x => x.PressMask is > 0) == 2);
            Check(s.Entries.Last().RepressAfterNeutralWithSameSourceBits); Check(s.Scope.Contains("not-driver"));
        });
        Test("source changed across neutral does not get same-source repress label", () => {
            var t = Tuple(); var trace = Armed(); trace.InvokeRaw(Frame(t, 1), Raw(0x1000), _ => { });
            trace.InvokeRaw(Frame(t, 2, "release-neutral"), Raw(), _ => { });
            trace.InvokeRaw(Frame(t, 3, source: 0x2000), Raw(0x2000), _ => { });
            Check(!trace.Snapshot().Entries.Last().RepressAfterNeutralWithSameSourceBits);
        });
        Test("wire sequence byte wrap255-to0 does not create digital edges", () => {
            var t = Tuple(); var trace = Armed(); trace.InvokeRaw(Frame(t, 1), Raw(0x1000, 255), _ => { });
            trace.InvokeRaw(Frame(t, 2), Raw(0x1000, 256), _ => { }); Check(trace.Snapshot().Entries.Length == 1);
        });
        Test("IMU and rolling timestamp differences are not press/release events", () => {
            var t = Tuple(); var trace = Armed(); trace.InvokeRaw(Frame(t, 1), Raw(0x1000, 1), _ => { });
            var raw = ExtractedHostBuilder20261012.Build(new FakeFrame { Buttons = 0x1000, ImuAdmitted = true, ImuGyroX = 10, ImuAccelZ = 1 }, 2);
            trace.InvokeRaw(Frame(t, 2), raw, _ => { }); Check(trace.Snapshot().Entries.Length == 1);
        });
        Test("inactive-to-active touch without click is not an extra button edge", () => {
            var t = Tuple(); var trace = Armed(); trace.InvokeRaw(Frame(t, 1, source: 0), Raw(), _ => { });
            var raw = ExtractedHostBuilder20261012.Build(new FakeFrame { ScreenPadsPresent = true, ScreenPadLeft = new(true, false, 0, 0, 100) }, 2);
            trace.InvokeRaw(Frame(t, 2, source: 0), raw, _ => { }); Check(trace.Snapshot().Entries.Length == 1);
        });
        Test("touch click in actual extracted builder is a digital edge", () => {
            var t = Tuple(); var trace = Armed(); trace.InvokeRaw(Frame(t, 1, source: 0), Raw(), _ => { });
            var raw = ExtractedHostBuilder20261012.Build(new FakeFrame { ScreenPadsPresent = true, ScreenPadLeft = new(true, true, 0, 0, 100) }, 2);
            trace.InvokeRaw(Frame(t, 2, source: 0), raw, _ => { }); Check(trace.Snapshot().Entries.Last().PressMask == 2u << 12);
        });
        Test("guide/mute and Edge function bits use current Host byte9 without layout shift", () => {
            var raw = ExtractedHostBuilder20261012.Build(new FakeFrame { ScreenButtons = 1 | 2 | 4 | 16 }, 4);
            Check((raw[9] & 0x35) == 0x35); Check((MsiPersonaReportChain20261012.DecodeDualSenseDigitalMask(raw) & (0x35u << 12)) == (0x35u << 12));
        });
        Test("Edge rear bits stay in existing builder mapping, diagnostics do not remap", () => {
            var raw = ExtractedHostBuilder20261012.Build(new FakeFrame { BackButtons = 3 }, 1); Check(raw[9] == 0xC0);
        });
        Test("analog trigger motion while digitally down is not repeated digital press", () => {
            var t = Tuple(); var trace = Armed();
            trace.InvokeRaw(Frame(t, 1), ExtractedHostBuilder20261012.Build(new FakeFrame { LtRaw = 0.3f }, 1), _ => { });
            trace.InvokeRaw(Frame(t, 2), ExtractedHostBuilder20261012.Build(new FakeFrame { LtRaw = 0.6f }, 2), _ => { });
            Check(trace.Snapshot().Entries.Length == 1);
        });
        Test("reserved byte9 bit3 cannot create a fictitious button", () => {
            var a = Raw(); var b = (byte[])a.Clone(); b[9] |= 8;
            Check(MsiPersonaReportChain20261012.DecodeDualSenseDigitalMask(a) == MsiPersonaReportChain20261012.DecodeDualSenseDigitalMask(b));
        });
        Test("invalid hat is unknown evidence but backend argument is still unchanged", () => {
            var trace = Armed(); var raw = Raw(); raw[7] = 15; int calls = 0;
            trace.InvokeRaw(Frame(Tuple(), 1), raw, x => { ++calls; Check(x[7] == 15); });
            Check(calls == 1 && trace.Snapshot().Entries.Single().EncodedDigitalMask == null);
        });
        Test("RID-included64 input is not misdecoded as data-only63", () => {
            var trace = Armed(); var raw = new byte[64]; raw[0] = 1; int calls = 0;
            trace.InvokeRaw(Frame(Tuple(), 1), raw, x => { ++calls; Check(ReferenceEquals(x, raw)); });
            Check(calls == 1 && trace.Snapshot().Entries.Single().EncodedDigitalMask == null);
        });
        Test("oversized argument is never copied/hashed or replaced", () => {
            int digests = 0, calls = 0; var trace = Armed(digest: d => { ++digests; return Sha(d); }); var raw = new byte[4096];
            trace.InvokeRaw(Frame(Tuple(), 1), raw, x => { ++calls; Check(x.Length == 4096); });
            Check(calls == 1 && digests == 0 && trace.Snapshot().Entries.Single().EncodedArgumentSha256 == null);
        });
        Test("default disabled diagnostics invoke backend once with no hash or logger", () => {
            int digest = 0, log = 0, calls = 0; var trace = new MsiPersonaReportChain20261012(diagnosticDigest: _ => { ++digest; return "x"; }, formatter: _ => "x", logger: _ => ++log);
            trace.InvokeRaw(Frame(Tuple(), 1), Raw(), _ => ++calls); Check(calls == 1 && digest == 0 && log == 0 && trace.Snapshot().Entries.Length == 0);
        });
        Test("ROG/GPD/common cannot arm this MSI-only trace", () => {
            var trace = new MsiPersonaReportChain20261012(); Check(!trace.Arm(Tuple() with { IsMsi = false }, Activation(Tuple())));
        });
        Test("profile/persona drift cannot arm trace", () => { var trace = new MsiPersonaReportChain20261012(); Check(!trace.Arm(Tuple() with { Profile = "xbox-elite-v2" }, Activation(Tuple()))); });
        Test("more than128 output records cannot be requested", () => { var trace = new MsiPersonaReportChain20261012(); Check(!trace.Arm(Tuple(), Activation(Tuple()), capacity: 129)); });
        Test("more than30s duration cannot be requested", () => { var trace = new MsiPersonaReportChain20261012(); Check(!trace.Arm(Tuple(), Activation(Tuple()), durationMs: 30001)); });
        Test("expiry stops diagnostics only, never submission", () => {
            long now = 0; var trace = new MsiPersonaReportChain20261012(() => now); Check(trace.Arm(Tuple(), Activation(Tuple()), durationMs: 1)); now = 1;
            int calls = 0; trace.InvokeRaw(Frame(Tuple(), 1), Raw(), _ => ++calls); Check(calls == 1 && trace.Snapshot().Entries.Length == 0);
        });
        Test("128 meaningful record cap cannot add retries/neutrals or suppress calls", () => {
            int calls = 0, logs = 0; var trace = Armed(formatter: _ => "edge", logger: _ => ++logs); var t = Tuple();
            for (ulong i = 1; i <= 1000; ++i) trace.InvokeRaw(Frame(t, i), Raw((i & 1) == 0 ? 0x1000 : 0, (long)i), _ => ++calls);
            Check(calls == 1000 && logs == 128 && trace.Snapshot().Entries.Length == 128);
        });
        Test("4096 attempt budget does not create a high-frequency log loop", () => {
            var t = Tuple(); int calls = 0, logs = 0; var trace = Armed(formatter: _ => "edge", logger: _ => ++logs);
            for (ulong i = 1; i <= 5000; ++i) trace.InvokeRaw(Frame(t, i), Raw(0x1000, (long)i), _ => ++calls);
            Check(calls == 5000 && logs == 1 && trace.Snapshot().AttemptsCaptured == 4096);
        });
        Test("digest exceptions cannot change successful Host backend counts", () => {
            var trace = Armed(digest: _ => throw new IOException("digest")); var counts = new FakeHostCounts();
            counts.Submit(trace, Frame(Tuple(), 1), Raw(0x1000)); Check(counts.Calls == 1 && counts.Ok == 1 && counts.Fail == 0);
        });
        Test("injected diagnostic digest sees a clone and cannot modify submitted bytes", () => {
            var raw = Raw(0x1000); var before = (byte[])raw.Clone();
            var trace = Armed(digest: copy => { Array.Fill(copy, (byte)255); return "mutated-diagnostic-copy"; });
            trace.InvokeRaw(Frame(Tuple(), 1), raw, bytes => Check(bytes.SequenceEqual(before))); Check(raw.SequenceEqual(before));
        });
        Test("serialization exceptions do not turn successful raw submits into failures", () => {
            var trace = Armed(formatter: _ => throw new IOException("serialization"), logger: _ => throw new Exception("must not reach logger"));
            var counts = new FakeHostCounts(); counts.Submit(trace, Frame(Tuple(), 1), Raw()); counts.Submit(trace, Frame(Tuple(), 2), Raw(0x1000));
            Check(counts.Calls == 2 && counts.Ok == 2 && counts.Fail == 0); Check(trace.Snapshot().Entries.Last().PressMask is > 0);
        });
        Test("logger exceptions preserve backend counts and captured release edge", () => {
            var trace = Armed(formatter: _ => "edge", logger: _ => throw new IOException("logger")); var counts = new FakeHostCounts();
            counts.Submit(trace, Frame(Tuple(), 1), Raw()); counts.Submit(trace, Frame(Tuple(), 2), Raw(0x1000)); counts.Submit(trace, Frame(Tuple(), 3), Raw());
            Check(counts.Calls == 3 && counts.Ok == 3 && counts.Fail == 0 && trace.Snapshot().Entries.Last().ReleaseMask is > 0);
        });
        Test("clock exception after successful SDK call is diagnostic-only", () => {
            int clocks = 0; var trace = new MsiPersonaReportChain20261012(() => ++clocks == 3 ? throw new IOException("clock") : 0);
            Check(trace.Arm(Tuple(), Activation(Tuple()))); var counts = new FakeHostCounts(); counts.Submit(trace, Frame(Tuple(), 1), Raw());
            Check(counts.Calls == 1 && counts.Ok == 1 && counts.Fail == 0);
        });
        Test("Arm clock failure refuses tracing, not product input", () => { var trace = new MsiPersonaReportChain20261012(() => throw new IOException("arm")); Check(!trace.Arm(Tuple(), Activation(Tuple()))); });
        Test("backend exception identity survives failing serializer/logger without retry", () => {
            var trace = Armed(formatter: _ => throw new IOException("diagnostic"), logger: _ => { }); var expected = new IOException("backend"); int calls = 0;
            try { trace.InvokeRaw(Frame(Tuple(), 1), Raw(), _ => { ++calls; throw expected; }); throw new Exception("missing failure"); }
            catch (IOException ex) { Check(ReferenceEquals(ex, expected)); } Check(calls == 1);
        });
        Test("backend throw invalidates digital continuity because a partial write is unknown", () => {
            var trace = Armed(); var t = Tuple(); trace.InvokeRaw(Frame(t, 1), Raw(), _ => { });
            try { trace.InvokeRaw(Frame(t, 2), Raw(0x1000), _ => throw new IOException("backend")); } catch (IOException) { }
            trace.InvokeRaw(Frame(t, 3), Raw(0x1000), _ => { }); Check(!trace.Snapshot().Entries.Last().HasReturnedSubmitBaseline);
        });
        Test("retarget during submit discards old completion rather than relabeling tuple", () => {
            var trace = Armed(); trace.InvokeRaw(Frame(Tuple(), 1), Raw(), _ => Check(trace.Arm(Tuple("new-run"), Activation(Tuple("new-run")))));
            Check(trace.Snapshot().Entries.Length == 0 && trace.Snapshot().StaleCompletionsDiscarded == 1);
        });
        Test("explicit neutral diagnostic identity is original caller tuple/sequence, not dummy1", () => {
            var trace = Armed(); var frame = Frame(Tuple(), 3425, "submit-neutral-command", source: null, encoder: 100);
            trace.InvokeRaw(frame, Raw(0, 100), _ => { }); var e = trace.Snapshot().Entries.Single();
            Check(e.Frame.CommandSequence == 3425 && e.Frame.SourceFrameSequence == 3525 && e.Frame.EncoderFrameIndex == 100 && e.Frame.Tuple.RunId == "fake-run");
        });
        Test("wrong current run identity is not attributed merely by time", () => {
            int calls = 0; var trace = Armed(); trace.InvokeRaw(Frame(Tuple("wrong"), 1), Raw(), _ => ++calls);
            Check(calls == 1 && trace.Snapshot().Entries.Length == 0);
        });
        Test("Elite SDK-state boundary explicitly has no encoded report/wire digest", () => {
            var tuple = Tuple() with { Persona = "elite", Profile = "xbox-elite-v2" }; var trace = Armed(tuple); int calls = 0;
            trace.InvokeUnencodedState(Frame(tuple, 1), () => ++calls); var e = trace.Snapshot().Entries.Single();
            Check(calls == 1 && e.BackendApi == "SubmitState" && e.EncodedArgumentSha256 == null && e.EncodedDigitalMask == null && e.EncodingScope.Contains("not-observed"));
        });
        Test("Elite logger/serializer failure cannot cause additional state or raw submits", () => {
            var tuple = Tuple() with { Persona = "elite", Profile = "xbox-elite-v2" }; var trace = Armed(tuple, formatter: _ => throw new IOException("serializer"), logger: _ => { });
            int calls = 0; trace.InvokeUnencodedState(Frame(tuple, 1), () => ++calls); Check(calls == 1);
        });
        Test("regular DualSense raw argument is traced but Edge projection is not fabricated", () => {
            var tuple = Tuple() with { Persona = "dualsense", Profile = "dualsense" }; var trace = Armed(tuple);
            trace.InvokeRaw(Frame(tuple, 1), Raw(), _ => { }); var e = trace.Snapshot().Entries.Single(); Check(e.EncodedArgumentSha256 != null && e.PredictedEdgeOverlaySha256 == null);
        });
        Test("actual Host dpad neutral/up changes only expected direction edge bits", () => {
            var trace = Armed(); var t = Tuple(); trace.InvokeRaw(Frame(t, 1), Raw(), _ => { });
            trace.InvokeRaw(Frame(t, 2), Raw(1, 2), _ => { }); Check(trace.Snapshot().Entries.Last().PressMask == 1u << 20);
        });
        Test("overlapping backend calls cannot manufacture ordered digital continuity", () => {
            var trace = Armed(); var t = Tuple(); int calls = 0;
            trace.InvokeRaw(Frame(t, 1), Raw(), _ => ++calls);
            trace.InvokeRaw(Frame(t, 2), Raw(0x1000), _ => {
                ++calls; trace.InvokeRaw(Frame(t, 3), Raw(), _ => ++calls);
            });
            var entries = trace.Snapshot().Entries; Check(calls == 3 && entries.Length == 3);
            Check(entries.Skip(1).All(e => e.DeliveryScope.Contains("concurrent-calls") && !e.HasReturnedSubmitBaseline));
        });
        Test("early digest/clock gaps invalidate continuity, not backend success", () => {
            int invocation = 0; var trace = Armed(digest: bytes => ++invocation == 4 ? throw new IOException("gap") : Sha(bytes));
            var counts = new FakeHostCounts(); var t = Tuple(); counts.Submit(trace, Frame(t, 1), Raw());
            counts.Submit(trace, Frame(t, 2), Raw(0x1000)); counts.Submit(trace, Frame(t, 3), Raw(0x1000));
            Check(counts.Calls == 3 && counts.Ok == 3 && counts.Fail == 0);
            Check(!trace.Snapshot().Entries.Last().HasReturnedSubmitBaseline);
        });
        Test("neutral submission can legitimately have no physical source sample", () => {
            var trace = Armed(); var f = Frame(Tuple(), 44, "release-neutral", source: null) with { SourceFrameSequence = null, SourceGeneration = null };
            trace.InvokeRaw(f, Raw(), _ => { }); var e = trace.Snapshot().Entries.Single();
            Check(e.Frame.SourceFrameSequence == null && e.Frame.SourceGeneration == null && e.Frame.CommandSequence == 44);
        });
        Test("disarm stops hashing/logging but cannot stop release-neutral backend call", () => {
            int calls = 0, logs = 0; var trace = Armed(formatter: _ => "record", logger: _ => ++logs); trace.Disarm();
            trace.InvokeRaw(Frame(Tuple(), 55, "release-neutral"), Raw(), _ => ++calls); Check(calls == 1 && logs == 0);
        });
        Test("source-generation replacement cannot become same-source neutral/live anomaly", () => {
            var trace = Armed(); var t = Tuple(); trace.InvokeRaw(Frame(t, 1), Raw(0x1000), _ => { });
            trace.InvokeRaw(Frame(t, 2, "submit-neutral-command"), Raw(), _ => { });
            trace.InvokeRaw(Frame(t, 3) with { SourceGeneration = 7 }, Raw(0x1000), _ => { });
            Check(!trace.Snapshot().Entries.Last().RepressAfterNeutralWithSameSourceBits);
        });
        Test("stale source sequence cannot become same-source neutral/live anomaly", () => {
            var trace = Armed(); var t = Tuple(); trace.InvokeRaw(Frame(t, 40), Raw(0x1000), _ => { });
            trace.InvokeRaw(Frame(t, 41, "submit-neutral-command"), Raw(), _ => { });
            trace.InvokeRaw(Frame(t, 42) with { SourceFrameSequence = 100 }, Raw(0x1000), _ => { });
            Check(!trace.Snapshot().Entries.Last().RepressAfterNeutralWithSameSourceBits);
        });
        Test("persona+IsMsi alone cannot activate without board authority", () => {
            var trace = new MsiPersonaReportChain20261012(); Check(!trace.Arm(Tuple(), Activation(Tuple()) with { MsiClawKnownBoardAuthority = false }));
        });
        Test("VisibilityDiagnostic compile gate off cannot activate trace", () => {
            var trace = new MsiPersonaReportChain20261012(); Check(!trace.Arm(Tuple(), Activation(Tuple()) with { VisibilityDiagnosticBuild = false }));
        });
        Test("virtual detail-log gate off cannot activate trace", () => {
            var trace = new MsiPersonaReportChain20261012(); Check(!trace.Arm(Tuple(), Activation(Tuple()) with { VirtualDetailLogEnabled = false }));
        });
        Test("detail diagnostics alone are not an explicit bounded session request", () => {
            var trace = new MsiPersonaReportChain20261012(); Check(!trace.Arm(Tuple(), Activation(Tuple()) with { ExplicitSessionRequested = false }));
        });
        Test("diagnostic activation receipt must match native run epoch power tuple", () => {
            var trace = new MsiPersonaReportChain20261012(); var a = Activation(Tuple());
            Check(!trace.Arm(Tuple(), a with { NativePid = 16908 })); Check(!trace.Arm(Tuple(), a with { RunId = "old" }));
            Check(!trace.Arm(Tuple(), a with { Epoch = 3 })); Check(!trace.Arm(Tuple(), a with { PowerGeneration = 2 }));
        });
        Test("empty diagnostic session ID cannot activate trace", () => {
            var trace = new MsiPersonaReportChain20261012(); Check(!trace.Arm(Tuple(), Activation(Tuple()) with { DiagnosticSessionId = "" }));
        });
        Console.WriteLine(JsonSerializer.Serialize(new { result = "PASS", fakeTests = _tests, SDKLoaded = false, productExecuted = false,
            hardwareObserved = false, claim = "extracted-Host-builder-and-submit-boundary-exception-isolation-only;hardware-resolution-pending" }));
        return 0;
    }
}



