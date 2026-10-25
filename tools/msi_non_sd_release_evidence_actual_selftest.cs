// Compiled ONLY with the offline extracted fixture. No real Host or SDK runs.
using System.Text.Json;
using YeManInputHost.Diagnostics;
internal static partial class Program
{
    internal static class Fixture
    {
        public static long Now;
        public static int Probes, Sweeps, Creates;
        public static bool CostInitThrows;
        public static bool ProbeThrows, ControllerThrows, ContextThrows, SweepThrows, CreateThrows, NeutralFails, NeutralThrows;
        public static VirtualEndpointObservation Current;
        public static Func<VirtualEndpointObservation>? OnObserve;
        public static Action? OnSweep, OnControllerDispose;
        public static Action<int>? OnSleep;
        public static string? GenericPersona;
        public static string NextRoot = "USB\\VID_28DE&PID_1205\\OWNED-1";
        public static List<string> Events = new();
        public static void Sleep(int milliseconds) { Now += milliseconds; OnSleep?.Invoke(milliseconds); }
        public static void Reset()
        {
            Now = Probes = Sweeps = Creates = 0;
            CostInitThrows = false;
            ProbeThrows = ControllerThrows = ContextThrows = SweepThrows = CreateThrows = NeutralFails = NeutralThrows = false;
            GenericPersona=null; Current = Empty(); OnObserve = null; OnSweep = null; OnControllerDispose = null; OnSleep = null;
            NextRoot = "USB\\VID_28DE&PID_1205\\OWNED-1"; Events.Clear();
        }
    }
    private static int _tests;
    private static void Check(bool condition, string detail = "assertion failed") { if (!condition) throw new Exception(detail); }
    private static void Test(string name, Action body) { Fixture.Reset(); body(); ++_tests; Console.WriteLine("PASS " + name); }
    internal static VirtualEndpointObservation Empty(string detail = "fresh-final-empty") =>
        new(true, 0, 0, 0, 0, 0, false, detail)
        { SteamDeckRootInstanceIds = Array.AsReadOnly(Array.Empty<string>()), SteamDeckHidChildInstanceIds = Array.AsReadOnly(Array.Empty<string>()), PresentInstanceIds = Array.AsReadOnly(Array.Empty<string>()) };
    internal static VirtualEndpointObservation Arrival(string root) =>
        new(true, 0, 0, 0, 1, 2, true, "pinned-arrival:" + root)
        { SteamDeckRootInstanceIds = Array.AsReadOnly(new[] { root }), SteamDeckHidChildInstanceIds = Array.AsReadOnly(new[] { Child(root, 0), Child(root, 1) }), PresentInstanceIds = Array.AsReadOnly(new[] { root, Child(root, 0), Child(root, 1) }) };
    private static string Child(string root, int n) => "HID\\VID_28DE&PID_1205&MI_0" + n + "\\" + root.Split('\\').Last() + "-CHILD" + n;
    private static CommandEnvelope Env(string command = "PREPARE_TARGET", ulong sequence = 2, ulong epoch = 17, string run = "exact-msi-run", bool enabled = true) =>
        new(command, "req-" + sequence, sequence, run, epoch, 3, "target", "coordinator", "steamdeck", "steam-deck-composite", "fake-nonce", 1, new string('a', 64))
        { MsiSteamDeckReleaseFence = command == "PREPARE_TARGET" && enabled };
    private static Dictionary<string, object?> Message(string command, ulong sequence = 1, ulong epoch = 17, string run = "exact-msi-run") =>
        new() { ["protocolVersion"] = 1, ["command"] = command, ["requestId"] = "req-" + sequence, ["sequence"] = sequence, ["runId"] = run, ["epoch"] = epoch, ["powerGeneration"] = 3,
            ["targetId"] = "target", ["ownerIdentity"] = "coordinator", ["persona"] = "steamdeck", ["profileIdentity"] = "steam-deck-composite", ["startupNonce"] = "fake-nonce", ["configRevision"] = 1, ["configHash"] = new string('a', 64) };
    private static JsonElement Send(HostSession session, Dictionary<string, object?> message, out bool shutdown)
    { using var doc = JsonDocument.Parse(session.Handle(JsonSerializer.Serialize(message), out shutdown)); return doc.RootElement.Clone(); }
    private static JsonElement Send(HostSession session, string command, ulong sequence, bool? enabled = null)
    { var m = Message(command, sequence); if (enabled.HasValue) m["msiSteamDeckReleaseFence"] = enabled.Value; return Send(session, m, out _); }
    private static HostSession Prepared(bool enabled = true)
    {
        var session = new HostSession("fake-nonce");
        Check(Send(session, "HELLO", 1).GetProperty("ok").GetBoolean());
        var prepare = Send(session, "PREPARE_TARGET", 2, enabled ? true : null);
        Check(prepare.GetProperty("ok").GetBoolean(), prepare.ToString());
        return session;
    }
    private static MsiSteamDeckReleaseFence1726 Pinned()
    { var fence = new MsiSteamDeckReleaseFence1726(Env()); Check(fence.TryPin(Empty(), Arrival(Fixture.NextRoot), out _)); return fence; }
    private static void Wire(JsonElement evidence, bool absent, int remaining)
    {
        Check(evidence.GetProperty("version").GetInt32() == 1);
        Check(evidence.GetProperty("required").GetBoolean());
        Check(evidence.GetProperty("runId").GetString() == "exact-msi-run");
        Check(evidence.GetProperty("epoch").GetUInt64() == 17);
        Check(evidence.GetProperty("persona").GetString() == "steamdeck");
        Check(evidence.GetProperty("profileIdentity").GetString() == "steam-deck-composite");
        Check(evidence.GetProperty("probeSucceeded").GetBoolean());
        Check(evidence.GetProperty("absentVerified").GetBoolean() == absent);
        Check(evidence.GetProperty("ownedTargetCount").GetInt32() == 3);
        Check(evidence.GetProperty("ownedTargets").GetArrayLength() == 3);
        Check(evidence.GetProperty("remainingOwnedTargetCount").GetInt32() == remaining);
        Check(evidence.GetProperty("scope").GetString() == "local-PnP");
    }
    private static Dictionary<string,object?> GenericMessage(string persona,string command,ulong sequence)
    {
        var m=Message(command,sequence);m["persona"]=persona;
        m["profileIdentity"]=persona switch {"xbox360"=>"xbox-360-wired","elite"=>"xbox-elite-v2","dualshock4"=>"dualshock-4-v2",_=>persona};return m;
    }
    private static string ExistingResponse(JsonElement response)
    {
        var node=System.Text.Json.Nodes.JsonNode.Parse(response.GetRawText())!.AsObject();
        node.Remove("msiNonSdReleaseEvidence");node.Remove("msiNonSdReleaseEvidenceVersion");
        // MSI HELLO intentionally adds diagnostic scheduling capabilities only.
        if(node["command"]?.GetValue<string>()=="HELLO" && node["detail"] is {} capabilityDetail) {
            var capability=System.Text.Json.Nodes.JsonNode.Parse(capabilityDetail.GetValue<string>())!.AsObject();
            capability.Remove("msiCostWindowDeferred");capability.Remove("msiCostWindowDeferredAvailable");
            node["detail"]=capability.ToJsonString();
        }
        // Elite's existing Stopwatch timing measures the wall clock, not behavior.
        if(node["detail"] is {} detail) node["detail"]=System.Text.RegularExpressions.Regex.Replace(detail.GetValue<string>(),@";xbox-startup-ms:.*$", "");
        return node.ToJsonString();
    }
    private static (string[] Replies,string[] Events,int Probes,int Sweeps,int Creates) GenericScenario(string persona,bool optin,string failure)
    {
        Fixture.Reset();var session=new HostSession("fake-nonce",0,optin);var replies=new List<string>();
        replies.Add(ExistingResponse(Send(session,GenericMessage(persona,"HELLO",1),out _)));
        Fixture.GenericPersona=persona;
        replies.Add(ExistingResponse(Send(session,GenericMessage(persona,"PREPARE_TARGET",2),out _)));
        Fixture.ControllerThrows=failure=="controller";Fixture.ContextThrows=failure=="context";
        Fixture.NeutralFails=failure=="neutral";Fixture.SweepThrows=failure=="sweep";
        Fixture.ProbeThrows=failure=="probe";
        if(failure!="two-polls") Fixture.OnControllerDispose=()=>Fixture.Current=Empty("fresh-zero-before-sweep");
        Fixture.OnSweep=()=>Fixture.Current=Empty("fresh-zero-after-sweep");
        replies.Add(ExistingResponse(Send(session,GenericMessage(persona,"RELEASE_TARGET",3),out _)));
        replies.Add(ExistingResponse(Send(session,GenericMessage(persona,"SHUTDOWN",4),out _)));
        return (replies.ToArray(),Fixture.Events.Where(e=>e is not ("cost-diagnostic-create" or "cost-generation-invalidated")).ToArray(),Fixture.Probes,Fixture.Sweeps,Fixture.Creates);
    }
    private static int Main()
    {
        try
        {
            foreach(var persona in new[]{"xbox360","elite","dualshock4","dualsense","dualsense-edge"}) {
                Test("MSI non-SD existing-poll receipt "+persona,()=>{
                    var session=new HostSession("fake-nonce",0,true);
                    Dictionary<string,object?> Msg(string command,ulong sequence){var m=Message(command,sequence);m["persona"]=persona;m["profileIdentity"]=persona switch {"xbox360"=>"xbox-360-wired","elite"=>"xbox-elite-v2","dualshock4"=>"dualshock-4-v2",_=>persona};return m;}
                    var hello=Send(session,Msg("HELLO",1),out _);Check(hello.GetProperty("msiNonSdReleaseEvidenceVersion").GetInt32()==1);
                    Fixture.GenericPersona=persona;
                    var prepare=Send(session,Msg("PREPARE_TARGET",2),out _);Check(prepare.GetProperty("ok").GetBoolean(),prepare.ToString());
                    Fixture.OnSweep=()=>Fixture.Current=Empty("fresh-zero");
                    var release=Send(session,Msg("RELEASE_TARGET",3),out _);Check(release.GetProperty("ok").GetBoolean(),release.ToString());
                    Check(release.GetProperty("targetPnpPrimaryRootCount").GetInt32()==1,"legacy prepare cache must not be changed into control proof");
                    var e=release.GetProperty("msiNonSdReleaseEvidence");Check(!e.GetProperty("exactOwnedAbsenceProven").GetBoolean());
                    var identity=e.GetProperty("sdkDeclaredIdentity");
                    Check(identity.GetProperty("observationState").GetString()=="verified-mvid-mismatch","fake SDK must stay unknown to the version-bound reader");
                    foreach(var proof in new[]{"exactOwnedIdsPinned","classificationComplete","exactOwnedAbsenceProven","physicalReturnVerified","steamConsumptionVerified"})
                        Check(!identity.GetProperty(proof).GetBoolean(),"SDK declaration diagnostic granted proof "+proof);
                    Check(e.GetProperty("cachedBefore").GetProperty("primaryRootCount").GetInt32()==1);
                    Check(e.GetProperty("freshObservations").GetArrayLength()==1);
                    Check(e.GetProperty("freshObservations")[0].GetProperty("observation").GetProperty("primaryRootCount").GetInt32()==0);
                    var probes=Fixture.Probes;var sweeps=Fixture.Sweeps;
                    var shutdown=Send(session,Msg("SHUTDOWN",4),out var stopped);Check(stopped&&shutdown.GetProperty("ok").GetBoolean());
                    Check(Fixture.Probes==probes&&Fixture.Sweeps==sweeps,"no extra PnP/SDK work for null SDK shutdown");
                    var second=shutdown.GetProperty("msiNonSdReleaseEvidence");Check(second.GetProperty("freshObservations").GetArrayLength()==0);
                    Check(second.GetProperty("previous").GetProperty("freshObservationCount").GetInt32()==1);
                    Check(!second.GetProperty("exactOwnedAbsenceProven").GetBoolean());
                    Check(second.GetProperty("sdkDeclaredIdentity").GetRawText()==identity.GetRawText(),"null-object SHUTDOWN must retain the declaration as historical evidence, not recapture");
                });
                Test("nonoptin no diagnostic fields "+persona,()=>{
                    var session=new HostSession("fake-nonce");
                    Dictionary<string,object?> Msg(string command,ulong sequence){var m=Message(command,sequence);m["persona"]=persona;m["profileIdentity"]=persona switch {"xbox360"=>"xbox-360-wired","elite"=>"xbox-elite-v2","dualshock4"=>"dualshock-4-v2",_=>persona};return m;}
                    var hello=Send(session,Msg("HELLO",1),out _);Check(!hello.TryGetProperty("msiNonSdReleaseEvidenceVersion",out _));
                    Fixture.GenericPersona=persona;Check(Send(session,Msg("PREPARE_TARGET",2),out _).GetProperty("ok").GetBoolean());Fixture.OnSweep=()=>Fixture.Current=Empty();
                    var release=Send(session,Msg("RELEASE_TARGET",3),out _);Check(release.GetProperty("ok").GetBoolean());Check(!release.TryGetProperty("msiNonSdReleaseEvidence",out _));
                });
            }
            foreach(var persona in new[]{"xbox360","elite","dualshock4","dualsense","dualsense-edge"}) {
                foreach(var failure in new[]{"none","two-polls","neutral","controller","context","sweep","probe"})
                    Test("diagnostic optin preserves existing responses and SDK/PnP actions "+persona+"/"+failure,()=>{
                        var off=GenericScenario(persona,false,failure);var on=GenericScenario(persona,true,failure);
                        Check(off.Replies.SequenceEqual(on.Replies),"existing replies differ");
                        Check(off.Events.SequenceEqual(on.Events),"existing action sequence differs");
                        Check(off.Probes==on.Probes&&off.Sweeps==on.Sweeps&&off.Creates==on.Creates,"SDK/PnP call counts differ");
                    });
                Test("replayed release has identical evidence and no extra actions "+persona,()=>{
                    var session=new HostSession("fake-nonce",0,true);Send(session,GenericMessage(persona,"HELLO",1),out _);
                    Fixture.GenericPersona=persona;Check(Send(session,GenericMessage(persona,"PREPARE_TARGET",2),out _).GetProperty("ok").GetBoolean());
                    Fixture.OnSweep=()=>Fixture.Current=Empty();var release=GenericMessage(persona,"RELEASE_TARGET",3);
                    var first=Send(session,release,out _);var probes=Fixture.Probes;var events=Fixture.Events.Count;
                    var second=Send(session,release,out _);Check(first.GetRawText()==second.GetRawText());Check(Fixture.Probes==probes&&Fixture.Events.Count==events);
                });
                foreach(var field in new[]{"runId","epoch","powerGeneration","targetId","ownerIdentity","persona","profileIdentity","startupNonce","configRevision","configHash"})
                    Test("foreign release tuple receives no MSI evidence "+persona+"/"+field,()=>{
                        var session=new HostSession("fake-nonce",0,true);Send(session,GenericMessage(persona,"HELLO",1),out _);
                        Fixture.GenericPersona=persona;Check(Send(session,GenericMessage(persona,"PREPARE_TARGET",2),out _).GetProperty("ok").GetBoolean());
                        Fixture.OnSweep=()=>Fixture.Current=Empty();Check(Send(session,GenericMessage(persona,"RELEASE_TARGET",3),out _).TryGetProperty("msiNonSdReleaseEvidence",out _));
                        var foreign=GenericMessage(persona,"RELEASE_TARGET",4);
                        foreign[field]=field switch {"epoch"=>18UL,"powerGeneration"=>4UL,"configRevision"=>2,"configHash"=>new string('b',64),"persona"=>"steamdeck","profileIdentity"=>"steam-deck-composite",_=>"foreign"};
                        var probes=Fixture.Probes;var events=Fixture.Events.Count;var rejected=Send(session,foreign,out _);
                        Check(!rejected.GetProperty("ok").GetBoolean());Check(!rejected.TryGetProperty("msiNonSdReleaseEvidence",out _));
                        Check(Fixture.Probes==probes&&Fixture.Events.Count==events,"foreign tuple changed lifecycle");
                    });
            }
            foreach(var persona in new[]{"xbox360","elite","dualshock4","dualsense","dualsense-edge"}) {
                foreach(var failCreate in new[]{false,true})
                    Test("SDK declaration reset on recreate "+persona+"/create-fails="+failCreate,()=>{
                        var s=new HostSession("fake-nonce",0,true);
                        Send(s,GenericMessage(persona,"HELLO",1),out _);Fixture.GenericPersona=persona;Fixture.Now=100;
                        Check(Send(s,GenericMessage(persona,"PREPARE_TARGET",2),out _).GetProperty("ok").GetBoolean());
                        Fixture.OnSweep=()=>Fixture.Current=Empty();Fixture.Now=200;Fixture.CreateThrows=failCreate;
                        var recreated=Send(s,GenericMessage(persona,"RECREATE_TARGET",3),out _);
                        Check(recreated.GetProperty("ok").GetBoolean()==!failCreate);
                        var r=Send(s,GenericMessage(persona,"RELEASE_TARGET",4),out _);
                        var identity=r.GetProperty("msiNonSdReleaseEvidence").GetProperty("sdkDeclaredIdentity");
                        if(failCreate)Check(identity.ValueKind==JsonValueKind.Null,"failed new create reused old controller identity");
                        else Check(identity.GetProperty("capturedAtTick").GetInt64()==200,"new controller did not replace the old capture tick");
                    });
            }
            Test("MSI optin SD exposes capability but keeps non-SD evidence absent",()=>{
                var session=new HostSession("fake-nonce",0,true);Send(session,"HELLO",1);Check(Send(session,"PREPARE_TARGET",2,true).GetProperty("ok").GetBoolean());
                Fixture.OnSweep=()=>Fixture.Current=Empty();var release=Send(session,"RELEASE_TARGET",3);
                Check(release.TryGetProperty("msiSteamDeckReleaseEvidence",out _));Check(!release.TryGetProperty("msiNonSdReleaseEvidence",out _));
            });
            Test("existing PnP read gaps survive cached/before-create/fresh wire without proof promotion",()=>{
                MsiPnpCensusDiagnostic.Receipt census;
                using(var gate=new MsiPnpCensusDiagnostic.Scope(true))
                using(var observation=new MsiPnpCensusDiagnostic.ObservationScope(20)) {
                    var capture=observation.Capture!;capture.TryNodeEnumerated();capture.TryMissingInstanceId();
                    capture.TryHardwareIdsReadFailed(5,64);capture.TryEnumerationEnd();census=capture.TryComplete(true,21)!;
                }
                var session=new HostSession("fake-nonce",0,true);Send(session,GenericMessage("dualshock4","HELLO",1),out _);
                Fixture.GenericPersona="dualshock4";Fixture.OnObserve=()=>Fixture.Current with {MsiPnpCensus=census};
                Check(Send(session,GenericMessage("dualshock4","PREPARE_TARGET",2),out _).GetProperty("ok").GetBoolean());
                Fixture.OnSweep=()=>Fixture.Current=Empty();var response=Send(session,GenericMessage("dualshock4","RELEASE_TARGET",3),out _);
                var evidence=response.GetProperty("msiNonSdReleaseEvidence");
                foreach(var observed in new[]{evidence.GetProperty("beforeCreateCensus"),
                    evidence.GetProperty("cachedBefore").GetProperty("existingPnpCensus"),
                    evidence.GetProperty("freshObservations")[0].GetProperty("observation").GetProperty("existingPnpCensus")}) {
                    Check(observed.GetProperty("missingInstanceIdCount").GetInt32()==1);
                    Check(observed.GetProperty("hardwareIdReadFailureCount").GetInt32()==1);
                    Check(!observed.GetProperty("classificationComplete").GetBoolean());Check(!observed.GetProperty("exactOwnedAbsenceProven").GetBoolean());
                }
                Check(evidence.GetProperty("freshObservations")[0].GetProperty("observation").GetProperty("genericCountOnlyAbsent").GetBoolean());
                Check(!evidence.GetProperty("exactOwnedAbsenceProven").GetBoolean());
            });
            Test("bounded nested PnP candidate data uses wire omission without changing release result",()=>{
                MsiPnpCensusDiagnostic.Receipt census;
                using(var gate=new MsiPnpCensusDiagnostic.Scope(true))
                using(var observation=new MsiPnpCensusDiagnostic.ObservationScope(20)) {
                    var capture=observation.Capture!;capture.TryCandidates("primary-root",Enumerable.Range(0,32).Select(i=>new string('x',250)+i));
                    capture.TryEnumerationEnd();census=capture.TryComplete(true,21)!;
                }
                var session=new HostSession("fake-nonce",0,true);Send(session,GenericMessage("dualshock4","HELLO",1),out _);
                Fixture.GenericPersona="dualshock4";Fixture.OnObserve=()=>Fixture.Current with {MsiPnpCensus=census};
                Check(Send(session,GenericMessage("dualshock4","PREPARE_TARGET",2),out _).GetProperty("ok").GetBoolean());
                Fixture.OnSweep=()=>Fixture.Current=Empty();var response=Send(session,GenericMessage("dualshock4","RELEASE_TARGET",3),out _);
                Check(response.GetProperty("ok").GetBoolean());Check(response.GetProperty("targetPnpPrimaryRootCount").GetInt32()==1);
                Check(response.GetProperty("msiNonSdReleaseEvidence").GetProperty("observationState").GetString()=="omitted-message-budget");
                Check(System.Text.Encoding.UTF8.GetByteCount(response.GetRawText())<=MaxMessageBytes);
            });
            Test("HELLO root capability exactly v1 and existing report-chain capability preserved", () => {
                var response = Send(new HostSession("fake-nonce"), "HELLO", 1);
                Check(response.GetProperty("msiSteamDeckReleaseFenceVersion").GetInt32() == 1);
                Check(response.GetProperty("msiReportChainDiagnosticVersion").GetInt32() == MsiReportChainProtocol1410.DiagnosticVersion);
                Check(!response.TryGetProperty("msiSteamDeckReleaseEvidence", out _)); Check(Fixture.Probes == 0 && Fixture.Creates == 0);
            });
            foreach (var value in new object?[] { "true", 1, 0, null, new { enabled = true }, Array.Empty<int>() })
                Test("strict PREPARE bool rejects " + JsonSerializer.Serialize(value), () => {
                    var m = Message("PREPARE_TARGET"); m["msiSteamDeckReleaseFence"] = value;
                    Check(!TryParseEnvelope(JsonSerializer.Serialize(m), out _, out var error)); Check(error == "invalid-msi-steamdeck-release-fence");
                });
            foreach (var value in new[] { true, false })
                Test("strict PREPARE accepts bool " + value, () => {
                    var m = Message("PREPARE_TARGET"); m["msiSteamDeckReleaseFence"] = value;
                    Check(TryParseEnvelope(JsonSerializer.Serialize(m), out var e, out _)); Check(e!.MsiSteamDeckReleaseFence == value);
                    Check(JsonSerializer.Serialize(e).Contains("\"msiSteamDeckReleaseFence\":" + (value ? "true" : "false")));
                });
            Test("omitted flag is false", () => { Check(TryParseEnvelope(JsonSerializer.Serialize(Message("PREPARE_TARGET")), out var e, out _)); Check(!e!.MsiSteamDeckReleaseFence); });
            foreach (var command in new[] { "HELLO", "RECREATE_TARGET", "RELEASE_TARGET", "SHUTDOWN", "HIDHIDE_UNHIDE", "SUBMIT_FRAME", "SUBMIT_NEUTRAL" })
                Test("flag rejected outside PREPARE: " + command, () => {
                    var m = Message(command); m["msiSteamDeckReleaseFence"] = false;
                    Check(!TryParseEnvelope(JsonSerializer.Serialize(m), out _, out var error)); Check(error == "invalid-msi-steamdeck-release-fence");
                });
            foreach (var pair in new[] { ("dualshock4", "dualshock-4-v2"), ("xbox360", "xbox-360-wired"), ("steamdeck", "wrong-profile") })
                Test("flag rejected wrong persona/profile: " + pair, () => {
                    var m = Message("PREPARE_TARGET"); m["persona"] = pair.Item1; m["profileIdentity"] = pair.Item2; m["msiSteamDeckReleaseFence"] = true;
                    Check(!TryParseEnvelope(JsonSerializer.Serialize(m), out _, out var error)); Check(error == "invalid-msi-steamdeck-release-fence");
                });
            Test("duplicate flag rejected, not advisory metadata", () => {
                var json = JsonSerializer.Serialize(Message("PREPARE_TARGET")); json = json[..^1] + ",\"msiSteamDeckReleaseFence\":true,\"msiSteamDeckReleaseFence\":false}";
                Check(!TryParseEnvelope(json, out _, out var e)); Check(e == "duplicate-field:msiSteamDeckReleaseFence");
            });
            Test("request id replay cannot change per-epoch flag", () => {
                var s = Prepared(false); var changed = Send(s, "PREPARE_TARGET", 2, true);
                Check(!changed.GetProperty("ok").GetBoolean() && changed.GetProperty("detail").GetString() == "request-id-content-mismatch"); Check(Fixture.Creates == 1);
            });
            Test("PREPARE publishes pinned evidence before active; release IDs exactly equal", () => {
                var s = new HostSession("fake-nonce"); Send(s, "HELLO", 1); var prepared = Send(s, "PREPARE_TARGET", 2, true);
                var pin = prepared.GetProperty("msiSteamDeckReleaseEvidence"); Wire(pin, false, 3);
                Fixture.OnSweep = () => Fixture.Current = Empty(); var release = Send(s, "RELEASE_TARGET", 3);
                Check(release.GetProperty("ok").GetBoolean()); var receipt = release.GetProperty("msiSteamDeckReleaseEvidence"); Wire(receipt, true, 0);
                Check(pin.GetProperty("ownedTargets").GetRawText() == receipt.GetProperty("ownedTargets").GetRawText());
                Check(receipt.GetProperty("beforeOwnedPresentCount").GetInt32() == 3);
                Check(receipt.GetProperty("afterOwnedPresentCount").GetInt32() == 0);
                Check(Fixture.Sweeps == 1); Check(Fixture.Events.IndexOf("neutral") < Fixture.Events.IndexOf("controller-dispose"));
            });
            Test("old-negative legacy IsAbsent ignores SteamDeck but opt-in times out", () => {
                Check(Arrival(Fixture.NextRoot).IsAbsent && !Arrival(Fixture.NextRoot).IsFullyAbsent);
                var s = Prepared(); var release = Send(s, "RELEASE_TARGET", 3);
                Check(!release.GetProperty("ok").GetBoolean()); Wire(release.GetProperty("msiSteamDeckReleaseEvidence"), false, 3);
                Check(Fixture.Now == 7000 && Fixture.Sweeps == 2, "must be exactly existing 2 x 3500ms, 1 retry");
            });
            Test("old-negative stale prepare PnP snapshot replaced with final observed snapshot", () => {
                var s = Prepared(); Fixture.OnSweep = () => Fixture.Current = Empty("fresh-release-snapshot");
                var release = Send(s, "RELEASE_TARGET", 3); Check(release.GetProperty("targetPnpDetail").GetString() == "fresh-release-snapshot");
                Check(!release.GetProperty("targetPnpAllStarted").GetBoolean());
                Check(release.GetProperty("msiSteamDeckReleaseEvidence").GetProperty("afterSteamDeckHidChildCount").GetInt32() == 0);
            });
            Test("finite wait includes deadline observation and does not oversleep", () => {
                var f = Pinned(); var calls = 0;
                var final = f.Wait(() => ++calls < 4 ? Arrival(Fixture.NextRoot) : Empty(), () => Fixture.Now, Fixture.Sleep, 125, 50);
                Check(f.IsOwnedAbsent(final) && Fixture.Now == 125 && calls == 4);
            });
            Test("bounded helper timeout keeps owned targets present", () => {
                var f = Pinned(); var calls = 0;
                var final = f.Wait(() => { calls++; return Arrival(Fixture.NextRoot); }, () => Fixture.Now, Fixture.Sleep, 125, 50);
                Check(!f.IsOwnedAbsent(final) && Fixture.Now == 125 && calls == 4);
            });
            Test("fake arrival delayed until finite poll sees unique started root", () => {
                Fixture.OnObserve = () => Fixture.Creates == 0 ? Empty() : Fixture.Now < 100 ? Empty() : Arrival(Fixture.NextRoot);
                var s = Prepared(); Check(s.Receipt?.OwnedTargetCount == 3 && Fixture.Now == 100);
            });
            Test("root disappearance is insufficient while a pinned HID remains", () => {
                var f = Pinned(); var a = Arrival(Fixture.NextRoot); var lingering = Empty("lingering-owned-HID") with { PresentInstanceIds = Array.AsReadOnly(new[] { Child(Fixture.NextRoot, 1) }), SteamDeckHidChildCount = 1 };
                var final = f.Wait(() => lingering, () => Fixture.Now, Fixture.Sleep, 125, 50);
                var errors = new List<string>(); Check(!f.CompleteRelease(a, final, errors, out _)); Check(f.Evidence!.RemainingOwnedTargetCount == 1 && !f.Evidence.AbsentVerified);
            });
            Test("unrelated SteamDeck root/HIDs are not counted as owned", () => {
                var s = Prepared(); Fixture.OnSweep = () => Fixture.Current = Arrival("USB\\VID_28DE&PID_1205\\FOREIGN");
                var r = Send(s, "RELEASE_TARGET", 3); Check(r.GetProperty("ok").GetBoolean()); Wire(r.GetProperty("msiSteamDeckReleaseEvidence"), true, 0);
                Check(r.GetProperty("msiSteamDeckReleaseEvidence").GetProperty("afterSteamDeckRootCount").GetInt32() == 1); Check(Fixture.Sweeps == 1 && Fixture.Now == 0);
                Check(!r.GetProperty("msiSteamDeckReleaseEvidence").GetProperty("ownedTargets").GetRawText().Contains("FOREIGN"));
            });
            Test("owned exact-ID matching ignores VID case but not foreign instances", () => {
                var f = Pinned(); var a = Arrival(Fixture.NextRoot); Check(!f.IsOwnedAbsent(a with { PresentInstanceIds = Array.AsReadOnly(a.PresentInstanceIds!.Select(x => x.ToLowerInvariant()).ToArray()) }));
                Check(f.IsOwnedAbsent(Arrival("USB\\VID_28DE&PID_1205\\FOREIGN")));
            });
            Test("no pins without successful zero reconciliation", () => {
                var f = new MsiSteamDeckReleaseFence1726(Env()); Check(!f.TryPin(Arrival(Fixture.NextRoot), Arrival(Fixture.NextRoot), out _));
                Check(!f.TryPin(Empty() with { ProbeSucceeded = false }, Arrival(Fixture.NextRoot), out _));
                Check(!f.TryPin(Empty() with { PresentInstanceIds = null }, Arrival(Fixture.NextRoot), out _));
            });
            Test("strict unique started admission, unknown identities never pinned", () => {
                var a = Arrival(Fixture.NextRoot); var f = new MsiSteamDeckReleaseFence1726(Env());
                foreach (var bad in new[] { a with { AllStarted = false }, a with { SteamDeckRootCount = 2 }, a with { SteamDeckRootInstanceIds = null }, a with { SteamDeckHidChildInstanceIds = null }, a with { PresentInstanceIds = null }, a with { SteamDeckHidChildCount = 3 }, a with { PrimaryRootCount = 1 }, a with { SteamDeckHidChildInstanceIds = Array.AsReadOnly(new[] { Child(Fixture.NextRoot, 0), Child(Fixture.NextRoot, 0) }) } })
                    Check(!f.TryPin(Empty(), bad, out _));
            });
            Test("missing pinned ID in arrival present enumeration rejects", () => {
                var f = new MsiSteamDeckReleaseFence1726(Env()); var a = Arrival(Fixture.NextRoot);
                Check(!f.TryPin(Empty(), a with { PresentInstanceIds = Array.AsReadOnly(new[] { Fixture.NextRoot }) }, out _));
            });
            Test("helper observation exception cannot certify absence", () => {
                var f = Pinned(); var final = f.Wait(() => throw new IOException("fake-enumeration"), () => Fixture.Now, Fixture.Sleep, 3500, 50);
                Check(!final.ProbeSucceeded && !f.IsOwnedAbsent(final)); Check(!f.CompleteRelease(Arrival(Fixture.NextRoot), final, new(), out _)); Check(!f.Evidence!.ProbeSucceeded && !f.Evidence.AbsentVerified);
            });
            Test("clock/sleep exception is explicit failure", () => {
                var f = Pinned(); var final = f.Wait(() => Arrival(Fixture.NextRoot), () => Fixture.Now, _ => throw new IOException("fake-sleep"), 3500, 50);
                Check(!final.ProbeSucceeded && !f.IsOwnedAbsent(final));
                Check(!f.Wait(() => Empty(), () => throw new IOException("fake-clock"), Fixture.Sleep, 3500, 50).ProbeSucceeded);
            });
            Test("probe failure during first wait latches despite retry empty snapshot", () => {
                var f = Pinned(); f.Wait(() => Empty() with { ProbeSucceeded = false }, () => Fixture.Now, Fixture.Sleep, 3500, 50);
                var final = f.Wait(() => Empty(), () => Fixture.Now, Fixture.Sleep, 3500, 50);
                Check(!f.CompleteRelease(Arrival(Fixture.NextRoot), final, new(), out _)); Check(!f.Evidence!.ProbeSucceeded && !f.Evidence.AbsentVerified);
            });
            Test("full session release probe failure does not turn into local-null success", () => {
                var s = Prepared(); Fixture.ProbeThrows = true;
                var r = Send(s, "RELEASE_TARGET", 3); Check(!r.GetProperty("ok").GetBoolean()); var evidence = r.GetProperty("msiSteamDeckReleaseEvidence");
                Check(!evidence.GetProperty("probeSucceeded").GetBoolean() && !evidence.GetProperty("absentVerified").GetBoolean());
                Check(evidence.GetProperty("remainingOwnedTargetCount").GetInt32() == -1);
                Fixture.ProbeThrows = false; Fixture.Current = Empty(); var probes = Fixture.Probes; var shutdown = Send(s, "SHUTDOWN", 4);
                Check(!shutdown.GetProperty("ok").GetBoolean()); var confirmation = shutdown.GetProperty("msiSteamDeckReleaseEvidence");
                foreach (var field in evidence.EnumerateObject()) Check(confirmation.GetProperty(field.Name).GetRawText() == field.Value.GetRawText());
                Check(Fixture.Probes == probes + 1 && confirmation.GetProperty("shutdownProbeSucceeded").GetBoolean() && confirmation.GetProperty("shutdownRemainingOwnedTargetCount").GetInt32() == 0);
            });
            Test("unknown-owned receipt survives second SHUTDOWN with no controller/context", () => {
                var s = new HostSession("fake-nonce"); Send(s, "HELLO", 1); Fixture.CreateThrows = true;
                var prepare = Send(s, "PREPARE_TARGET", 2, true); Check(!prepare.GetProperty("ok").GetBoolean()); var e = prepare.GetProperty("msiSteamDeckReleaseEvidence");
                Check(e.GetProperty("ownedTargetCount").GetInt32() == 0 && !e.GetProperty("absentVerified").GetBoolean());
                var shutdown = Send(s, "SHUTDOWN", 3); Check(!shutdown.GetProperty("ok").GetBoolean()); var confirmation = shutdown.GetProperty("msiSteamDeckReleaseEvidence");
                foreach (var field in e.EnumerateObject()) Check(confirmation.GetProperty(field.Name).GetRawText() == field.Value.GetRawText());
                Check(confirmation.GetProperty("shutdownRemainingOwnedTargetCount").GetInt32() == -1);
            });
            Test("before-release probe failure cannot be erased by successful after probe", () => {
                var s = Prepared(); var first = true; Fixture.OnObserve = () => { if (first) { first = false; throw new IOException("fake-before"); } return Fixture.Current; };
                Fixture.OnSweep = () => Fixture.Current = Empty(); var r = Send(s, "RELEASE_TARGET", 3);
                Check(!r.GetProperty("ok").GetBoolean()); Check(!r.GetProperty("msiSteamDeckReleaseEvidence").GetProperty("probeSucceeded").GetBoolean());
            });
            foreach (var failure in new[] { "neutral-false", "neutral-throw", "controller", "context", "sweep" })
                Test("cleanup error persists to SHUTDOWN: " + failure, () => {
                    var s = Prepared(); Fixture.OnSweep = () => Fixture.Current = Empty();
                    Fixture.NeutralFails = failure == "neutral-false"; Fixture.NeutralThrows = failure == "neutral-throw"; Fixture.ControllerThrows = failure == "controller"; Fixture.ContextThrows = failure == "context"; Fixture.SweepThrows = failure == "sweep";
                    var r = Send(s, "RELEASE_TARGET", 3); Check(!r.GetProperty("ok").GetBoolean()); var detail = r.GetProperty("detail").GetString(); var probes = Fixture.Probes;
                    var shutdown = Send(s, "SHUTDOWN", 4); Check(!shutdown.GetProperty("ok").GetBoolean() && shutdown.GetProperty("detail").GetString() == detail); Check(Fixture.Probes == probes + 1);
                });
            Test("failed-release SHUTDOWN rechecks still-owned IDs read-only without upgrading proof", () => {
                var s = Prepared(); var r = Send(s, "RELEASE_TARGET", 3); Check(!r.GetProperty("ok").GetBoolean()); var sweeps = Fixture.Sweeps; var now = Fixture.Now;
                var shutdown = Send(s, "SHUTDOWN", 4); Check(!shutdown.GetProperty("ok").GetBoolean()); var e = shutdown.GetProperty("msiSteamDeckReleaseEvidence");
                Check(e.GetProperty("shutdownRemainingOwnedTargetCount").GetInt32() == 3 && !e.GetProperty("absentVerified").GetBoolean());
                Check(Fixture.Sweeps == sweeps && Fixture.Now == now);
            });
            Test("SHUTDOWN preserves successful receipt exactly, no extra sweeps/observations", () => {
                var s = Prepared(); Fixture.OnSweep = () => Fixture.Current = Empty(); var r = Send(s, "RELEASE_TARGET", 3); var probes = Fixture.Probes; var sweeps = Fixture.Sweeps;
                var shutdown = Send(s, Message("SHUTDOWN", 4), out var exits); Check(exits && shutdown.GetProperty("ok").GetBoolean());
                Check(shutdown.GetProperty("msiSteamDeckReleaseEvidence").GetRawText() == r.GetProperty("msiSteamDeckReleaseEvidence").GetRawText()); Check(Fixture.Probes == probes && Fixture.Sweeps == sweeps);
            });
            Test("HIDHIDE commands remain accepted after RELEASE; no implicit SDK create", () => {
                var s = Prepared(); Fixture.OnSweep = () => Fixture.Current = Empty(); Send(s, "RELEASE_TARGET", 3); var creates = Fixture.Creates; var seq = 4UL;
                foreach (var command in new[] { "HIDHIDE_UNHIDE", "HIDHIDE_CYCLE", "HIDHIDE_CYCLE_BEGIN", "HIDHIDE_CYCLE_POLL", "HIDHIDE_QUERY", "HIDHIDE_QUERY_STATE", "HIDHIDE_CLOAK" })
                {
                    var m = Message(command, seq++); if (command == "HIDHIDE_UNHIDE" || command.Contains("CYCLE")) m["path"] = "USB\\FAKE-PHYSICAL"; if (command == "HIDHIDE_CLOAK") m["cloak"] = false;
                    var r = Send(s, m, out var exits); Check(r.GetProperty("ok").GetBoolean() && !exits, r.ToString());
                }
                Check(Fixture.Creates == creates && Fixture.Events.Contains("HIDHIDE_UNHIDE") && Fixture.Events.Contains("HIDHIDE_CYCLE"));
                Check(!Send(s, "SUBMIT_NEUTRAL", seq++).GetProperty("ok").GetBoolean());
                Check(!Send(s, "RECREATE_TARGET", seq++).GetProperty("ok").GetBoolean());
                Check(Fixture.Creates == creates); Check(Send(s, "SHUTDOWN", seq).GetProperty("ok").GetBoolean());
            });
            Test("non-opt-in SteamDeck legacy release and stale snapshot remain unchanged", () => {
                var s = Prepared(false); var r = Send(s, "RELEASE_TARGET", 3);
                Check(r.GetProperty("ok").GetBoolean() && Fixture.Now == 0 && Fixture.Sweeps == 1);
                Check(!r.TryGetProperty("msiSteamDeckReleaseEvidence", out _)); Check(r.GetProperty("targetPnpDetail").GetString()!.StartsWith("pinned-arrival:"));
            });
            Test("explicit false matches omitted non-opt-in", () => {
                var s = new HostSession("fake-nonce"); Send(s, "HELLO", 1); var prepare = Send(s, "PREPARE_TARGET", 2, false);
                Check(prepare.GetProperty("ok").GetBoolean() && !prepare.TryGetProperty("msiSteamDeckReleaseEvidence", out _)); Check(Send(s, "RELEASE_TARGET", 3).GetProperty("ok").GetBoolean());
            });
            Test("40 independent epochs pin/reset/release without previous-session success reuse", () => {
                for (var i = 1; i <= 40; i++)
                {
                    Fixture.Reset(); Fixture.NextRoot = "USB\\VID_28DE&PID_1205\\EPOCH-" + i;
                    var s = new HostSession("fake-nonce"); var e = Env(epoch: (ulong)i, run: "run-" + i);
                    Check(s.PrepareDirect(e, out _)); Check(s.Receipt!.Epoch == (ulong)i && s.Receipt.RunId == "run-" + i && !s.Receipt.AbsentVerified);
                    var pin = s.Receipt.OwnedTargets.ToArray(); Fixture.OnSweep = () => Fixture.Current = Empty(); Check(s.ReleaseLocal(out _));
                    Check(s.Receipt!.AbsentVerified && pin.SequenceEqual(s.Receipt.OwnedTargets));
                    Fixture.NextRoot = "USB\\VID_28DE&PID_1205\\NEXT-" + i; Fixture.OnSweep = null; Fixture.Current = Empty();
                    Check(s.PrepareDirect(e with { Epoch = (ulong)i + 100 }, out _)); Check(!s.Receipt!.AbsentVerified && s.Receipt.OwnedTargets.All(id => id.Contains("NEXT-" + i)));
                    Check(!s.ReleaseLocal(out _) && !s.Receipt.AbsentVerified); // still-present new endpoint cannot reuse old success
                }
            });
            Test("new non-opt-in PREPARE resets old success, zero evidence", () => {
                var s = Prepared(); Fixture.OnSweep = () => Fixture.Current = Empty(); Check(s.ReleaseLocal(out _)); Check(s.Receipt!.AbsentVerified);
                Fixture.OnSweep = null; Fixture.Current = Empty(); Check(s.PrepareDirect(Env(epoch: 18, enabled: false), out _)); Check(s.Receipt is null); Check(s.ReleaseLocal(out _));
            });
            Test("failed PREPARE retry resets unresolved ownership, fresh pin only", () => {
                var s = new HostSession("fake-nonce"); Send(s, "HELLO", 1); Fixture.CreateThrows = true; Check(!Send(s, "PREPARE_TARGET", 2, true).GetProperty("ok").GetBoolean());
                Fixture.CreateThrows = false; Fixture.NextRoot = "USB\\VID_28DE&PID_1205\\RETRY";
                var prepare = Send(s, "PREPARE_TARGET", 3, true); Check(prepare.GetProperty("ok").GetBoolean()); Wire(prepare.GetProperty("msiSteamDeckReleaseEvidence"), false, 3);
            });
            Test("full prepare rejects nonzero SteamDeck reconciliation before SDK creation", () => {
                Fixture.Current = Arrival("USB\\VID_28DE&PID_1205\\STALE"); var s = new HostSession("fake-nonce"); Send(s, "HELLO", 1);
                var r = Send(s, "PREPARE_TARGET", 2, true); Check(!r.GetProperty("ok").GetBoolean()); Check(Fixture.Creates == 0); Check(!r.GetProperty("msiSteamDeckReleaseEvidence").GetProperty("absentVerified").GetBoolean());
            });
            Test("arrival with two roots fails pin despite legacy SteamDeck >=1 predicate", () => {
                var a = Arrival(Fixture.NextRoot); var foreign = "USB\\VID_28DE&PID_1205\\FOREIGN";
                Fixture.OnObserve = () => Fixture.Creates == 0 ? Empty() : a with { SteamDeckRootCount = 2, SteamDeckRootInstanceIds = Array.AsReadOnly(new[] { Fixture.NextRoot, foreign }), PresentInstanceIds = Array.AsReadOnly(a.PresentInstanceIds!.Append(foreign).ToArray()) };
                var s = new HostSession("fake-nonce"); Send(s, "HELLO", 1); Check(!Send(s, "PREPARE_TARGET", 2, true).GetProperty("ok").GetBoolean()); Check(s.Receipt?.OwnedTargetCount == 0);
            });
            Test("immutable owned target receipt cannot be changed via caller arrays or IList", () => {
                var root = Fixture.NextRoot; var roots = new[] { root }; var children = new[] { Child(root, 0), Child(root, 1) }; var a = Arrival(root) with { SteamDeckRootInstanceIds = roots, SteamDeckHidChildInstanceIds = children };
                var f = new MsiSteamDeckReleaseFence1726(Env()); Check(f.TryPin(Empty(), a, out _)); roots[0] = "OTHER"; children[0] = "OTHER";
                Check(f.Evidence!.OwnedTargets.All(id => id != "OTHER")); var blocked = false; try { ((IList<string>)f.Evidence.OwnedTargets)[0] = "OTHER"; } catch (NotSupportedException) { blocked = true; } Check(blocked);
            });
            Test("write actual Host wire fixture for native parser pairing", () => {
                var s = new HostSession("fake-nonce"); var hello = Send(s, "HELLO", 1); var prepare = Send(s, "PREPARE_TARGET", 2, true);
                Fixture.OnSweep = () => Fixture.Current = Empty(); var release = Send(s, "RELEASE_TARGET", 3); var shutdown = Send(s, "SHUTDOWN", 4);
                File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "wire-success.json"), JsonSerializer.Serialize(new { hello, prepare, release, shutdown }));
                Fixture.Reset(); var bad = new HostSession("fake-nonce"); Send(bad, "HELLO", 1); var badPrepare = Send(bad, "PREPARE_TARGET", 2, true); var badRelease = Send(bad, "RELEASE_TARGET", 3);
                var stillPresent = Send(bad, "SHUTDOWN", 4); Fixture.Current = Empty(); var lateEmpty = Send(bad, "SHUTDOWN", 5);
                File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "wire-failure.json"), JsonSerializer.Serialize(new { prepare = badPrepare, release = badRelease, stillPresent, lateEmpty }));
            });
            Test("non-MSI diagnostic launch has no cost worker or capability", () => {
                var s = new HostSession("fake-nonce");
                Check(s.CostDiagnosticLifetime is null);
                var hello = Send(s,"HELLO",1);
                Check(hello.GetProperty("detail").GetString()=="{\"hidhideStateQuery\":true}");
                Check(!Fixture.Events.Contains("cost-diagnostic-create"));
            });
            Test("MSI cost diagnostic capability reports requested and available", () => {
                var s = new HostSession("fake-nonce",0,true);
                Check(s.CostDiagnosticLifetime is not null);
                using var capability = JsonDocument.Parse(Send(s,"HELLO",1).GetProperty("detail").GetString()!);
                Check(capability.RootElement.GetProperty("msiCostWindowDeferred").GetBoolean());
                Check(capability.RootElement.GetProperty("msiCostWindowDeferredAvailable").GetBoolean());
                Check(Fixture.Events.Count(e=>e=="cost-diagnostic-create")==1);
                s.CostDiagnosticLifetime?.Dispose();
                Check(Fixture.Events.Count(e=>e=="cost-diagnostic-dispose")==1);
            });
            Test("MSI cost worker initialization failure leaves HELLO and prepare usable", () => {
                Fixture.CostInitThrows=true;
                var s = new HostSession("fake-nonce",0,true);
                Check(s.CostDiagnosticLifetime is null);
                var hello = Send(s,"HELLO",1);Check(hello.GetProperty("ok").GetBoolean());
                using var capability = JsonDocument.Parse(hello.GetProperty("detail").GetString()!);
                Check(capability.RootElement.GetProperty("msiCostWindowDeferred").GetBoolean());
                Check(!capability.RootElement.GetProperty("msiCostWindowDeferredAvailable").GetBoolean());
                Check(Send(s,"PREPARE_TARGET",2,true).GetProperty("ok").GetBoolean());
            });
            Test("actual MSI prepare and release invalidate passive controller generation first", () => {
                var s = new HostSession("fake-nonce",0,true);Send(s,"HELLO",1);Fixture.Events.Clear();
                Check(Send(s,"PREPARE_TARGET",2,true).GetProperty("ok").GetBoolean());
                Check(Fixture.Events[0]=="cost-generation-invalidated");
                Fixture.OnSweep=()=>Fixture.Current=Empty();Fixture.Events.Clear();
                Check(Send(s,"RELEASE_TARGET",3).GetProperty("ok").GetBoolean());
                Check(Fixture.Events[0]=="cost-generation-invalidated");
                Check(Fixture.Events.Count(e=>e=="controller-dispose")==1);
            });
            Console.WriteLine($"PASS {_tests} offline test groups; production helper + extracted parser/dispatch/prepare/release/waits/serializer, 40 repeat epochs. No real product/SDK/PnP/service/registry/Steam operations.");
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}
