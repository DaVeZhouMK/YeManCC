// SDK-free entry: real PsEndpointOwnership and extracted real HostSession.
// CM functions below read private tables only. The normal Host/pipe Main is unused.
using System.Text.Json;
using YeManInputHost.Diagnostics;

internal static class PsEndpointOwnershipTestEntry
{
    private static int Main() => Program.RunPsEndpointOwnershipTests();
}

internal static partial class Program
{
    private sealed class PsNode(string id)
    {
        internal string Id = id;
        internal uint Child, Sibling, Status = 8, Problem;
    }
    private sealed class PsGraph : PsEndpointOwnership.ICmReader
    {
        internal readonly Dictionary<uint, PsNode> Rows = new();
        internal readonly HashSet<uint> Seen = new();
        internal string Failure = "";
        internal uint FailureNode;
        internal bool WrongLocate, Throw;
        internal int Calls, RootSiblingCalls;
        private int Tick(string op, uint node = 0)
        {
            ++Calls; if (node != 0) Seen.Add(node);
            if (Throw) throw new IOException("private-CM-failure");
            return Failure == op && (FailureNode == 0 || FailureNode == node) ? 5 : 0;
        }
        public int Locate(string id, out uint node)
        {
            node = 0; var error = Tick("locate"); if (error != 0) return error;
            if (WrongLocate) { node = 1; return 0; }
            foreach (var row in Rows) if (StringComparer.OrdinalIgnoreCase.Equals(row.Value.Id, id)) { node = row.Key; return 0; }
            return 13;
        }
        public int GetChild(uint node, out uint child)
        {
            child = 0; var error = Tick("child", node); if (error != 0) return error;
            if (Rows.TryGetValue(node, out var row) && row.Child != 0) { child = row.Child; return 0; } return 13;
        }
        public int GetSibling(uint node, out uint sibling)
        {
            if (node == 1) ++RootSiblingCalls;
            sibling = 0; var error = Tick("sibling", node); if (error != 0) return error;
            if (Rows.TryGetValue(node, out var row) && row.Sibling != 0) { sibling = row.Sibling; return 0; } return 13;
        }
        public int GetDeviceId(uint node, out string id)
        {
            id = ""; var error = Tick("id", node); if (error != 0) return error;
            if (!Rows.TryGetValue(node, out var row)) return 13; id = row.Id; return 0;
        }
        public int GetStatus(uint node, out uint status, out uint problem)
        {
            status = problem = 0; var error = Tick("status", node); if (error != 0) return error;
            if (!Rows.TryGetValue(node, out var row)) return 13; status = row.Status; problem = row.Problem; return 0;
        }
    }
    private static readonly List<object> PsRows = new(), PsReplies = new();
    private static int PsChecks, PsFailures;
    private static void PsCheck(bool value, string message = "PS ownership assertion")
    { ++PsChecks; if (!value) throw new Exception(message); }
    private static void PsCase(string name, Action action)
    {
        Fixture.Reset(); Fixture.OutputStops = Fixture.IdleStops = true; Fixture.FrameFails = false;
        Fixture.IdleCaptureFails = Fixture.IdleCtsDisposeFails = false;
        PsOwnershipCmReader.Override = null; PsOwnershipCmReader.Reads = 0; PsFixtureMissingSdkRoot = false; _managedSession = null;
        Fixture.OnSweep = () => Fixture.Current = Empty(); Fixture.OnObserve = ManagedPnpObservation;
        try { action(); PsRows.Add(new { name, pass = true }); }
        catch (Exception ex) { ++PsFailures; PsRows.Add(new { name, pass = false, error = ex.ToString() }); }
    }
    private static PsGraph PsValid(string? root = null, string? hid = null)
    {
        var graph = new PsGraph(); graph.Rows[1] = new(root ?? ManagedPsRoot(1)) { Child = 2 };
        graph.Rows[2] = new(hid ?? ManagedPsHid(1)); return graph;
    }
    private static PsEndpointOwnership PsOwner(string? root = null, bool nullId = false)
    {
        var owner = new PsEndpointOwnership(new(typeof(HMController).FullName!, typeof(HMController).Module.ModuleVersionId));
        owner.BeginCreate(PsBinding()); var target = new HMController(nullId ? null : root ?? ManagedPsRoot(1));
        owner.Created(target); PsObjects[owner] = target; return owner;
    }
    private static readonly Dictionary<PsEndpointOwnership, HMController> PsObjects = new();
    private static PsEndpointOwnership.Binding PsBinding(string run = "ps-run") => new("ps-host", run, 17, "target", PsEndpointOwnership.Persona);
    private static PsEndpointOwnership.Evidence PsCapture(PsEndpointOwnership owner, PsGraph graph, bool verified = true) =>
        owner.Capture(PsObjects[owner], verified, graph)!;
    private static void PsIncomplete(PsEndpointOwnership.Evidence evidence, string? reason = null)
    {
        PsCheck(!evidence.Complete && evidence.HidInstanceIds.Count == 0, "incomplete never publishes partial HID pin");
        if (reason is not null) PsCheck(evidence.Reason == reason, evidence.Reason + " != " + reason);
    }
    private static Dictionary<string, object?> PsMessage(string command, ulong sequence)
    {
        var message = Message(command, sequence, run: "ps-run");
        message["persona"] = message["profileIdentity"] = "dualsense-edge";
        return message;
    }
    private static JsonElement PsSend(HostSession session, string command, ulong sequence, Action<Dictionary<string, object?>>? change = null)
    {
        var message = PsMessage(command, sequence); change?.Invoke(message);
        var response = Send(session, message, out _); PsReplies.Add(new { command, response }); return response;
    }
    private static HostSession PsHello()
    {
        var session = new HostSession("fake-nonce"); _managedSession = session;
        var hello = PsSend(session, "HELLO", 1); PsCheck(hello.GetProperty("ok").GetBoolean());
        PsCheck(hello.GetProperty("psEndpointOwnershipVersion").GetInt32() == 1);
        PsCheck(!hello.TryGetProperty("psEndpointOwnership", out _)); return session;
    }
    private static JsonElement PsEvidence(JsonElement response)
    {
        PsCheck(response.GetProperty("ok").GetBoolean(), response.ToString());
        var evidence = response.GetProperty("psEndpointOwnership");
        PsCheck(evidence.GetProperty("schemaVersion").GetInt32() == 1);
        PsCheck(evidence.GetProperty("persona").GetString() == "dualsense-edge");
        foreach (var key in new[] { "hostInstanceId", "runId", "epoch", "targetId" })
            PsCheck(evidence.GetProperty(key).GetRawText() == response.GetProperty(key).GetRawText(), "ownership tuple " + key);
        PsCheck(evidence.GetProperty("bindingGeneration").GetUInt64() > 0);
        PsCheck(evidence.GetProperty("source").GetString() == "sdk-instance-id-and-cm-subtree"); return evidence;
    }
    internal static int RunPsEndpointOwnershipTests()
    {
        PsCase("helper/current SDK root and HID descendant", () => {
            var owner = PsOwner(); var graph = PsValid(); var e = PsCapture(owner, graph);
            PsCheck(e.Complete && e.SdkRootInstanceId == ManagedPsRoot(1) && e.HidInstanceIds.SequenceEqual(new[] { ManagedPsHid(1) }));
            PsCheck(graph.RootSiblingCalls == 0 && graph.Calls <= 1 + 5 * PsEndpointOwnership.MaximumNodes);
        });
        PsCase("helper/same-model foreign root and its HID excluded", () => {
            var owner = PsOwner(); var graph = PsValid(); graph.Rows[1].Sibling = 50;
            graph.Rows[50] = new(ManagedPsRoot(99)) { Child = 51 }; graph.Rows[51] = new(ManagedPsHid(99));
            var e = PsCapture(owner, graph); PsCheck(e.Complete && e.HidInstanceIds.Count == 1 && !graph.Seen.Contains(50) && !graph.Seen.Contains(51));
        });
        PsCase("helper/missing SDK root with peer only", () => PsIncomplete(PsCapture(PsOwner(), PsValid(ManagedPsRoot(99), ManagedPsHid(99))), "cm-root-unavailable"));
        PsCase("helper/locate cannot substitute a foreign root", () => { var g = PsValid(ManagedPsRoot(99)); g.WrongLocate = true; PsIncomplete(PsCapture(PsOwner(), g), "cm-root-identity-mismatch"); });
        PsCase("helper/root with no HID descendant", () => { var g = PsValid(); g.Rows[1].Child = 0; PsIncomplete(PsCapture(PsOwner(), g), "hid-descendant-unavailable"); });
        foreach (var (operation, reason) in new[] { ("child", "cm-child-read-failed"), ("sibling", "cm-sibling-read-failed"), ("id", "cm-device-id-unavailable"), ("status", "cm-status-unavailable") })
            PsCase("helper/CM " + operation + " read failure", () => { var g = PsValid(); g.Failure = operation; g.FailureNode = 2; PsIncomplete(PsCapture(PsOwner(), g), reason); });
        PsCase("helper/unstarted HID", () => { var g = PsValid(); g.Rows[2].Status = 0; PsIncomplete(PsCapture(PsOwner(), g), "cm-node-not-started"); });
        PsCase("helper/root problem code", () => { var g = PsValid(); g.Rows[1].Problem = 22; PsIncomplete(PsCapture(PsOwner(), g), "cm-node-not-started"); });
        PsCase("helper/child cycle", () => { var g = PsValid(); g.Rows[2].Child = 1; PsIncomplete(PsCapture(PsOwner(), g), "cm-cycle-or-duplicate-node"); });
        PsCase("helper/sibling cycle", () => { var g = PsValid(); g.Rows[2].Sibling = 2; PsIncomplete(PsCapture(PsOwner(), g), "cm-cycle-or-duplicate-node"); });
        PsCase("helper/duplicate instance ID via distinct node", () => { var g = PsValid(); g.Rows[2].Sibling = 3; g.Rows[3] = new(ManagedPsHid(1).ToLowerInvariant()); PsIncomplete(PsCapture(PsOwner(), g), "cm-duplicate-instance-id"); });
        PsCase("helper/128 nodes complete", () => { var g = PsValid(); for (uint i = 2; i <= 128; ++i) g.Rows[i] = new("HID\\N" + i) { Sibling = i == 128 ? 0 : i + 1 }; var e = PsCapture(PsOwner(), g); PsCheck(e.Complete && e.HidInstanceIds.Count == 127); });
        PsCase("helper/129 nodes truncated and bounded", () => { var g = PsValid(); for (uint i = 2; i <= 129; ++i) g.Rows[i] = new("HID\\N" + i) { Sibling = i == 129 ? 0 : i + 1 }; PsIncomplete(PsCapture(PsOwner(), g), "cm-node-limit"); PsCheck(g.Seen.Count <= 128 && g.Calls <= 1 + 5 * 128); });
        PsCase("helper/depth16 accepted", () => { var g = PsValid(); for (uint i = 2; i <= 17; ++i) g.Rows[i] = new((i == 17 ? "HID\\" : "SWD\\") + i) { Child = i == 17 ? 0 : i + 1 }; PsCheck(PsCapture(PsOwner(), g).Complete); });
        PsCase("helper/depth17 incomplete", () => { var g = PsValid(); for (uint i = 2; i <= 18; ++i) g.Rows[i] = new("HID\\N" + i) { Child = i == 18 ? 0 : i + 1 }; PsIncomplete(PsCapture(PsOwner(), g), "cm-depth-limit"); PsCheck(g.Seen.Count <= 17); });
        PsCase("helper/USBip null SDK identity unavailable without CM", () => { var g = PsValid(); PsIncomplete(PsCapture(PsOwner(nullId: true), g), "sdk-instance-id-unavailable"); PsCheck(g.Calls == 0); });
        PsCase("helper/empty SDK identity unavailable", () => { var g = PsValid(); PsIncomplete(PsCapture(PsOwner(""), g), "sdk-instance-id-unavailable"); PsCheck(g.Calls == 0); });
        PsCase("helper/oversized SDK ID never truncated into a pin", () => { var g = PsValid(); PsIncomplete(PsCapture(PsOwner("SWD\\" + new string('X', 257)), g), "sdk-instance-id-invalid"); PsCheck(g.Calls == 0); });
        PsCase("helper/unverified asset cannot query CM", () => { var g = PsValid(); PsIncomplete(PsCapture(PsOwner(), g, false), "sdk-asset-unverified"); PsCheck(g.Calls == 0); });
        PsCase("helper/SDK MVID mismatch", () => { var owner = new PsEndpointOwnership(new(typeof(HMController).FullName!, Guid.Empty)); owner.BeginCreate(PsBinding()); var c = new HMController(ManagedPsRoot(1)); owner.Created(c); var g = PsValid(); PsIncomplete(owner.Capture(c, true, g)!, "sdk-type-metadata-mismatch"); PsCheck(g.Calls == 0); });
        PsCase("helper/old object cannot impersonate current binding", () => { var owner = PsOwner(); var g = PsValid(); PsIncomplete(owner.Capture(new HMController(ManagedPsRoot(1)), true, g)!, "current-sdk-controller-unavailable"); PsCheck(g.Calls == 0); });
        PsCase("helper/release invalidation and new generation recapture", () => { var owner = PsOwner(); var old = PsObjects[owner]; var first = PsCapture(owner, PsValid()); owner.Invalidate("release-started"); PsIncomplete(PsCapture(owner, PsValid()), "binding-invalidated"); owner.BeginCreate(PsBinding()); var next = new HMController(ManagedPsRoot(2)); owner.Created(next); PsIncomplete(owner.Capture(old, true, PsValid())!, "current-sdk-controller-unavailable"); var e = owner.Capture(next, true, PsValid(ManagedPsRoot(2), ManagedPsHid(2)))!; PsCheck(e.Complete && e.BindingGeneration == first.BindingGeneration + 1 && first.HidInstanceIds[0] == ManagedPsHid(1)); });
        PsCase("helper/same root HID reenumeration retains target generation", () => { var owner = PsOwner(); var first = PsCapture(owner, PsValid()); var next = PsCapture(owner, PsValid(hid: ManagedPsHid(1) + "_REENUM")); PsCheck(next.Complete && next.BindingGeneration == first.BindingGeneration && next.HidInstanceIds.Single().EndsWith("_REENUM")); });
        PsCase("helper/read failure invalidates earlier complete observation", () => { var owner = PsOwner(); PsCheck(PsCapture(owner, PsValid()).Complete); var next = PsCapture(owner, PsValid(ManagedPsRoot(99))); PsIncomplete(next); PsCheck(next.BindingGeneration == 1 && !owner.ForBinding(PsBinding())!.Complete); });
        PsCase("helper/CM exceptions are incomplete", () => { var g = PsValid(); g.Throw = true; PsIncomplete(PsCapture(PsOwner(), g), "ownership-read-exception"); });
        PsCase("helper/evidence byte cap does not emit partial IDs", () => { var g = PsValid(); for (uint i = 2; i <= 128; ++i) g.Rows[i] = new("HID\\" + i + new string('L', 150)) { Sibling = i == 128 ? 0 : i + 1 }; PsIncomplete(PsCapture(PsOwner(), g), "ownership-evidence-limit"); });
        PsCase("helper/serialization is pure and binding specific", () => { var owner = PsOwner(); var g = PsValid(); PsCapture(owner, g); var calls = g.Calls; var text = owner.Append("{\"ok\":true}", PsBinding(), 16384); PsCheck(text.Contains("psEndpointOwnership") && g.Calls == calls && owner.BindingGeneration == 1); PsCheck(owner.Append("{\"ok\":true}", PsBinding("foreign-run"), 16384) == "{\"ok\":true}"); });
        PsCase("host/HELLO and PREPARE exact producer fields", () => { var s = PsHello(); var e = PsEvidence(PsSend(s, "PREPARE_TARGET", 2)); PsCheck(e.GetProperty("complete").GetBoolean() && e.GetProperty("sdkRootInstanceId").GetString() == ManagedPsRoot(1)); PsCheck(e.GetProperty("hidInstanceIds")[0].GetString() == ManagedPsHid(1) && PsOwnershipCmReader.Reads > 0); });
        PsCase("host/QUERY uses current object without generation or create", () => { var s = PsHello(); var first = PsEvidence(PsSend(s, "PREPARE_TARGET", 2)); var calls = PsOwnershipCmReader.Reads; var query = PsSend(s, "QUERY_TARGET_OWNERSHIP", 3); var e = PsEvidence(query); PsCheck(query.GetProperty("status").GetString() == "target-ownership" && e.GetProperty("complete").GetBoolean()); PsCheck(Fixture.Creates == 1 && e.GetProperty("bindingGeneration").GetUInt64() == first.GetProperty("bindingGeneration").GetUInt64() && PsOwnershipCmReader.Reads > calls); });
        PsCase("host/QUERY incomplete is successful ACK and recaptures", () => { var s = PsHello(); PsSend(s, "PREPARE_TARGET", 2); PsOwnershipCmReader.Override = PsValid(ManagedPsRoot(99)); var query = PsSend(s, "QUERY_TARGET_OWNERSHIP", 3); var e = PsEvidence(query); PsCheck(query.GetProperty("status").GetString() == "target-ownership" && !e.GetProperty("complete").GetBoolean() && e.GetProperty("bindingGeneration").GetUInt64() == 1); PsOwnershipCmReader.Override = null; PsCheck(PsEvidence(PsSend(s, "QUERY_TARGET_OWNERSHIP", 4)).GetProperty("complete").GetBoolean() && Fixture.Creates == 1); });
        PsCase("host/no ownership fields or CM work on neutral and frames", () => { var s = PsHello(); PsSend(s, "PREPARE_TARGET", 2); var calls = PsOwnershipCmReader.Reads; var neutral = PsSend(s, "SUBMIT_NEUTRAL", 3); PsCheck(neutral.GetProperty("ok").GetBoolean() && !neutral.TryGetProperty("psEndpointOwnership", out _)); for (ulong i = 4; i < 14; ++i) { var frame = PsSend(s, "SUBMIT_FRAME", i, m => { m["buttons"]=0;m["lx"]=0;m["ly"]=0;m["rx"]=0;m["ry"]=0;m["lt"]=0;m["rt"]=0;m["sampleSequence"]=i;m["monotonicTick"]=i;m["motionPairProofId"]="offline";m["normalizedInputHash"]=CanonicalFrameHash(0,0,0,0,0,0,0); }); PsCheck(frame.GetProperty("ok").GetBoolean() && !frame.TryGetProperty("psEndpointOwnership", out _)); } PsCheck(PsOwnershipCmReader.Reads == calls); });
        PsCase("host/RECREATE new generation and original idempotent receipt", () => { var s = PsHello(); var original = PsSend(s, "PREPARE_TARGET", 2); var recreate = PsSend(s, "RECREATE_TARGET", 3); var e = PsEvidence(recreate); PsCheck(e.GetProperty("complete").GetBoolean() && e.GetProperty("bindingGeneration").GetUInt64() == 2 && e.GetProperty("sdkRootInstanceId").GetString() == ManagedPsRoot(2)); var calls = PsOwnershipCmReader.Reads; var replay = PsSend(s, "PREPARE_TARGET", 2); PsCheck(replay.GetRawText() == original.GetRawText() && Fixture.Creates == 2 && PsOwnershipCmReader.Reads == calls); PsCheck(PsEvidence(PsSend(s, "QUERY_TARGET_OWNERSHIP", 4)).GetProperty("bindingGeneration").GetUInt64() == 2); });
        PsCase("host/new target incomplete still ACKs current generation", () => { var s = PsHello(); PsSend(s, "PREPARE_TARGET", 2); PsOwnershipCmReader.Override = PsValid(); var recreate = PsSend(s, "RECREATE_TARGET", 3); var e = PsEvidence(recreate); PsCheck(recreate.GetProperty("status").GetString() == "target-prepared" && !e.GetProperty("complete").GetBoolean() && e.GetProperty("bindingGeneration").GetUInt64() == 2 && s.HasController); PsOwnershipCmReader.Override = null; var query = PsEvidence(PsSend(s, "QUERY_TARGET_OWNERSHIP", 4)); PsCheck(query.GetProperty("complete").GetBoolean() && query.GetProperty("bindingGeneration").GetUInt64() == 2 && Fixture.Creates == 2); });
        PsCase("host/ownership unavailable does not block prepare or output", () => { var s = PsHello(); PsOwnershipCmReader.Override = PsValid(ManagedPsRoot(99)); var e = PsEvidence(PsSend(s, "PREPARE_TARGET", 2)); PsCheck(!e.GetProperty("complete").GetBoolean() && s.HasController); PsCheck(PsSend(s, "SUBMIT_NEUTRAL", 3).GetProperty("ok").GetBoolean()); });
        PsCase("host/null SDK root does not disable the target", () => { PsFixtureMissingSdkRoot = true; var s = PsHello(); var e = PsEvidence(PsSend(s, "PREPARE_TARGET", 2)); PsCheck(!e.GetProperty("complete").GetBoolean() && e.GetProperty("reason").GetString() == "sdk-instance-id-unavailable" && s.HasController && PsOwnershipCmReader.Reads == 0); PsCheck(PsSend(s, "SUBMIT_NEUTRAL", 3).GetProperty("ok").GetBoolean()); var q = PsSend(s, "QUERY_TARGET_OWNERSHIP", 4); PsCheck(q.GetProperty("status").GetString() == "target-ownership" && !PsEvidence(q).GetProperty("complete").GetBoolean() && Fixture.Creates == 1); });
        PsCase("host/QUERY replay is original receipt without a second read", () => { var s = PsHello(); PsSend(s, "PREPARE_TARGET", 2); var first = PsSend(s, "QUERY_TARGET_OWNERSHIP", 3); var reads = PsOwnershipCmReader.Reads; PsOwnershipCmReader.Override = PsValid(ManagedPsRoot(99)); PsCheck(PsSend(s, "QUERY_TARGET_OWNERSHIP", 3).GetRawText() == first.GetRawText() && PsOwnershipCmReader.Reads == reads); });
        PsCase("host/factory exception still invalidates old generation", () => { var s = PsHello(); PsSend(s, "PREPARE_TARGET", 2); Fixture.CreateThrows = true; var failed = PsSend(s, "RECREATE_TARGET", 3); PsCheck(!failed.GetProperty("ok").GetBoolean()); var e = failed.GetProperty("psEndpointOwnership"); PsCheck(!e.GetProperty("complete").GetBoolean() && e.GetProperty("bindingGeneration").GetUInt64() == 2); });
        PsCase("host/release invalidates even if writer barrier fails", () => { var s = PsHello(); PsSend(s, "PREPARE_TARGET", 2); Fixture.OutputStops = false; PsCheck(!PsSend(s, "RELEASE_TARGET", 3).GetProperty("ok").GetBoolean()); var calls = PsOwnershipCmReader.Reads; var e = PsEvidence(PsSend(s, "QUERY_TARGET_OWNERSHIP", 4)); PsCheck(!e.GetProperty("complete").GetBoolean() && e.GetProperty("bindingGeneration").GetUInt64() == 1 && PsOwnershipCmReader.Reads == calls); });
        PsCase("host/QUERY standard tuple and nonce checks precede CM", () => { var s = PsHello(); PsSend(s, "PREPARE_TARGET", 2); var calls = PsOwnershipCmReader.Reads; foreach (var key in new[] { "runId", "ownerIdentity", "startupNonce", "targetId", "configHash" }) { var r = PsSend(s, "QUERY_TARGET_OWNERSHIP", 3, m => m[key] = key == "configHash" ? new string('b',64) : "foreign"); PsCheck(!r.GetProperty("ok").GetBoolean() && PsOwnershipCmReader.Reads == calls); } PsCheck(PsSend(s, "QUERY_TARGET_OWNERSHIP", 3).GetProperty("ok").GetBoolean()); });
        PsCase("host/non-PS lifecycle has no PS evidence or CM reads", () => { var s = Prepared(); var query = Send(s, "TUPLE_SUBMIT_STATS", 3); PsCheck(query.GetProperty("ok").GetBoolean() && !query.TryGetProperty("psEndpointOwnership", out _) && PsOwnershipCmReader.Reads == 0); });
        Console.WriteLine(JsonSerializer.Serialize(new { status = PsFailures == 0 ? "PASS" : "FAIL", checks = PsChecks, cases = PsRows, failures = PsFailures, hostReplies = PsReplies, normalHostMainExecuted = false, realSdkLoaded = false, hardwareIo = false, privateCmLeaves = true }));
        return PsFailures == 0 ? 0 : 1;
    }
}

