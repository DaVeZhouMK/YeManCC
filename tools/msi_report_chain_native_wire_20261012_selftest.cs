// Parent actual-TU native JSON -> frozen current Host actual parser/builders /
// fake-call boundaries. All HM types are inert. No SDK/device/product execution.
// LB+RB reservation/summon changes WITHDRAWN/out of scope. Native gates are not
// retested here. Correlation IDs are references, not raw physical-source proof.
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using YeManInputHost.Diagnostics;
internal static partial class Program
{
    private static readonly List<string> Passed = new();
    private static void Check(bool ok,string message="assertion"){if(!ok)throw new Exception(message);}
    private static void Test(string name,Action body){body();Passed.Add(name);Console.WriteLine("PASS "+name);}
    private static CommandEnvelope ParseActual(string wire)
    {Check(TryParseEnvelope(wire,out var e,out var err),"actual parser: "+err);return e!;}
    private static CommandEnvelope ParseLegacy(string wire)
    {Check(TryParseEnvelopeBefore1410(wire,out var e,out var err),"legacy parser: "+err);return e!;}
    private static string Fingerprint(CommandEnvelope e,bool ignoreIndependentRequestId=false)=>
        JsonSerializer.Serialize(ignoreIndependentRequestId?e with{RequestId="independent-native-request-id"}:e);
    private static string ChangeMetadata(string wire,string field,Action<JsonObject> change)
    {
        var root=JsonNode.Parse(wire)!.AsObject();change(root[field]!.AsObject());return root.ToJsonString();
    }
    private static HostSession Session(CommandEnvelope e,int nativePid)
    {
        var s=new HostSession(e,nativePid);
        // Capability receipt uses actual extracted response code. No full
        // HELLO/Prepare controller handshake or SDK factory is executed.
        s.Response(e with{Command="HELLO"},true,"host-hello");return s;
    }
    private static byte[] SonyBytes(CommandEnvelope e,long index)=>
        BuildDualSenseDataOnlyReport(new HMProfile{Id=e.ProfileIdentity,InputReportSize=64},e,index,
            e.Persona=="dualsense-edge"?MapDsEdgePaddles(e.BackButtons):0);
    private static string Sha(byte[] bytes)=>Convert.ToHexString(SHA256.HashData(bytes));
    private static string WireSha(string wire)=>Sha(Encoding.UTF8.GetBytes(wire));
    private static JsonElement LastEntry(List<string> logs)
    {using var d=JsonDocument.Parse(logs.Last(x=>x.Contains("msi-report-chain-submit")));return d.RootElement.GetProperty("entry").Clone();}
    private static int Main(string[] args)
    {
        if(args.Length!=2)throw new ArgumentException("Copied actual native fixture and output receipt required");
        using var artifact=JsonDocument.Parse(File.ReadAllBytes(args[0]));var root=artifact.RootElement;
        Check(root.GetProperty("schemaVersion").GetInt32()==1,"parent fixture schemaVersion must be1");
        var prepare=root.GetProperty("prepare"); var frame=root.GetProperty("frame");
        var prepareBase=prepare.GetProperty("base").GetString()!; var prepareDiagnostic=prepare.GetProperty("withDiagnostic").GetString()!;
        var frameBase=frame.GetProperty("base").GetString()!; var frameSource=frame.GetProperty("withSource").GetString()!;
        var sourceSequence=frame.GetProperty("sourceSequence").GetUInt64();var sourceGeneration=frame.GetProperty("sourceGeneration").GetUInt64();
        var pb=ParseActual(prepareBase);var pd=ParseActual(prepareDiagnostic);var fb=ParseActual(frameBase);var fd=ParseActual(frameSource);
        var authorization=pd.ReportChainPrepare1410??throw new Exception("actual native PREPARE metadata not admitted by Host parser");
        var source=fd.ReportChainSource1410??throw new Exception("actual native SUBMIT metadata not admitted by Host parser");
        var nativePid=authorization.Activation.NativePid;byte[]? encodedBase=null;byte[]? encodedSource=null;

        Test("parent actual fixture schema1 and required four native wire strings",()=>{
            Check(prepareBase.Length>0&&prepareDiagnostic.Length>0&&frameBase.Length>0&&frameSource.Length>0&&sourceSequence>0&&sourceGeneration>0);
        });
        Test("actual PREPARE base accepted by current and legacy-control projection",()=>Check(Fingerprint(pb)==Fingerprint(ParseLegacy(prepareBase))));
        Test("actual SUBMIT controls match legacy with explicit inactive-quaternion projection",()=>Check(Fingerprint(fb)==Fingerprint(ParseLegacy(frameBase)), "current="+Fingerprint(fb)+" legacy="+Fingerprint(ParseLegacy(frameBase))));
        Test("legacy parser rejects actual native PREPARE optional as unknown-field: negotiation necessary",()=>{
            Check(!TryParseEnvelopeBefore1410(prepareDiagnostic,out _,out var error)&&error=="unknown-field");
        });
        Test("legacy parser rejects actual native SUBMIT source optional as unknown-field",()=>{
            Check(!TryParseEnvelopeBefore1410(frameSource,out _,out var error)&&error=="unknown-field");
        });
        Test("PREPARE controls identical except independently generated native requestId",()=>{
            Check(Fingerprint(pb,true)==Fingerprint(pd,true));
            // The actual envelope builder generated independent request IDs.
            // This is explicitly excluded from parity, never called byte-identical.
            Check(pb.Sequence==pd.Sequence&&pb.Command=="PREPARE_TARGET"&&pd.Command=="PREPARE_TARGET");
        });
        Test("frame optional metadata does not alter any ordinary parsed field/fingerprint",()=>Check(Fingerprint(fb)==Fingerprint(fd)));
        Test("current inactive IMU keeps identity quaternion; optional metadata cannot change it",()=>{if(!fb.ImuAdmitted)Check(fb.ImuQuatW==1 && fb.ImuQuatX==0 && fb.ImuQuatY==0 && fb.ImuQuatZ==0 && fd.ImuQuatW==1 && fd.ImuQuatX==0 && fd.ImuQuatY==0 && fd.ImuQuatZ==0);});
        Test("Host strict PREPARE tuple epoch/power/run matches actual native authorization",()=>{
            var a=authorization.Activation;Check(a.RunId==pd.RunId&&a.Epoch==pd.Epoch&&a.PowerGeneration==pd.PowerGeneration&&a.NativePid>0);
            Check(authorization.DurationMs==10000&&authorization.MaxMeaningfulEdges==128);
        });
        Test("native PREPARE and SUBMIT selected persona/profile/target tuple match",()=>{
            Check(pd.RunId==fd.RunId&&pd.Epoch==fd.Epoch&&pd.PowerGeneration==fd.PowerGeneration&&pd.Persona==fd.Persona&&pd.ProfileIdentity==fd.ProfileIdentity&&pd.TargetId==fd.TargetId&&pd.OwnerIdentity==fd.OwnerIdentity&&pd.StartupNonce==fd.StartupNonce);
        });
        Test("parent supplied UInt64 source sequence/generation survive exact Host parsing",()=>{
            Check(source.SourceFrameSequence==sourceSequence&&source.SourceGeneration==sourceGeneration);
        });
        Test("parent corrected actual artifact same-session binding: no rewriting fixture",()=>{
            Check(authorization.Activation.DiagnosticSessionId==source.DiagnosticSessionId);
        });
        Test("native normalizedInputHash exactly64 and recomputed by current actual Host",()=>{
            Check(fd.NormalizedInputHash.Length==64&&fd.NormalizedInputHash==CanonicalFrameHash(fd.Buttons,fd.Lx,fd.Ly,fd.Rx,fd.Ry,fd.Lt,fd.Rt));
            Check(fd.NormalizedInputHash==fb.NormalizedInputHash);
        });
        Test("optional native frame preserves raw domain/buttons/sticks/triggers",()=>{
            Check(fd.HasRawDomain==fb.HasRawDomain&&fd.RawButtons==fb.RawButtons&&fd.RawThumbLX==fb.RawThumbLX&&fd.RawThumbLY==fb.RawThumbLY&&fd.RawThumbRX==fb.RawThumbRX&&fd.RawThumbRY==fb.RawThumbRY&&fd.RawLeftTrigger==fb.RawLeftTrigger&&fd.RawRightTrigger==fb.RawRightTrigger);
        });
        Test("actual Sony encoded data-only63 bytes identical base versus withSource",()=>{
            Check(fd.Persona == "dualsense-edge","parent artifact must select current Edge for actual raw-report comparison");
            encodedBase=SonyBytes(fb,817);encodedSource=SonyBytes(fd,817);Check(encodedBase.Length==63&&encodedBase.SequenceEqual(encodedSource));
        });
        Test("actual Sony metadata changes no digital edges, timestamps, contacts or battery bytes",()=>{
            Check(MsiPersonaReportChain20261012.DecodeDualSenseDigitalMask(encodedBase!)==MsiPersonaReportChain20261012.DecodeDualSenseDigitalMask(encodedSource!));
            Check(encodedBase!.AsSpan(27,4).SequenceEqual(encodedSource!.AsSpan(27,4))&&encodedBase.AsSpan(32,8).SequenceEqual(encodedSource.AsSpan(32,8))&&encodedBase![52]==encodedSource![52]);
        });
        Test("actual Elite state builder values also unaffected by source metadata",()=>{
            var p=new HMProfile{Id="xbox-elite-v2",InputReportSize=0};var a=BuildEliteState(p,fb);var b=BuildEliteState(p,fd);
            Check(a.Buttons==b.Buttons&&a.Hat==b.Hat&&a.Axes!.SequenceEqual(b.Axes!));
        });
        Test("actual fake-call raw boundary once and encoded bytes/counters identical legacy before",()=>{
            var old=Session(fb,nativePid);_dualSenseFrameCounter=817;Check(old.FrameBefore(fb,out var beforeDetail));
            var now=Session(fd,nativePid);_dualSenseFrameCounter=817;Check(now.Frame(fd,out var detail));
            Check(old._controller!.LastRaw!.SequenceEqual(now._controller!.LastRaw!)&&old._controller.RawCalls==1&&now._controller.RawCalls==1&&old.Counts==now.Counts&&beforeDetail==detail);
        });
        Test("source optional without activation remains OFF and calls backend once",()=>{
            var now=Session(fd,nativePid);Check(now.Frame(fd,out _)&&now._controller!.RawCalls==1&&now._reportChain1410==null);
        });
        Test("actual authorization bound to supplied nativePID; mismatched launch-parent refused",()=>{
            var logs=new List<string>();var runtime=new MsiReportChainRuntime1410(()=>0,sink:logs.Add);var wrongPid=nativePid==int.MaxValue?nativePid-1:nativePid+1;
            const string pnp="pnp-hid-ids=HID\\HIDCLASS\\FAKE[HID\\VID_054C&PID_0DF2];";
            Check(!runtime.ArmSafely(authorization,wrongPid,101,"fake-host",pd.Persona,pd.ProfileIdentity,pd.RunId,pd.Epoch,pd.PowerGeneration,pnp,true)&&!runtime.Armed&&logs.Count==0);
            Check(runtime.ArmSafely(authorization,nativePid,101,"fake-host",pd.Persona,pd.ProfileIdentity,pd.RunId,pd.Epoch,pd.PowerGeneration,pnp,true));
            runtime.CloseSafely("fake-complete");
        });
        Test("actual native metadata -> extracted Host wrap -> fake SDK call correlation",()=>{
            var logs=new List<string>();var session=Session(pd,nativePid);session.TestRuntimeFactory=()=>new(()=>0,sink:logs.Add);session.ArmProduction(pd);Check(session._reportChain1410?.Armed==true);
            _dualSenseFrameCounter=817;Check(session.Frame(fd,out var detail)&&detail=="report-submitted"&&session._controller!.RawCalls==1);
            var entry=LastEntry(logs);var f=entry.GetProperty("Frame");var tuple=f.GetProperty("Tuple");
            Check(f.GetProperty("SourceFrameSequence").GetUInt64()==sourceSequence&&f.GetProperty("SourceGeneration").GetUInt64()==sourceGeneration);
            Check(f.GetProperty("CommandSequence").GetUInt64()==fd.Sequence&&tuple.GetProperty("NativePid").GetInt32()==nativePid&&tuple.GetProperty("RunId").GetString()==fd.RunId);
            Check(entry.GetProperty("EncodedArgumentSha256").GetString()==Sha(encodedSource!));session.Close("fake-complete");
        });
        Test("metadata reference never labelled raw physical-source bits or delivered hardware proof",()=>{
            var logs=new List<string>();var session=Session(pd,nativePid);session.TestRuntimeFactory=()=>new(()=>0,sink:logs.Add);session.ArmProduction(pd);Check(session.Frame(fd,out _));
            var e=LastEntry(logs);Check(e.GetProperty("DeliveryScope").GetString()!.Contains("raw-physical-source-bits-unavailable")&&e.GetProperty("DeliveryScope").GetString()!.Contains("not-ACK-driver-delivery-or-Steam-consumption-proof"));
        });
        foreach(var key in new[]{"sourceFrameSequence","sourceGeneration","schemaVersion"})
            Test("invalid optional native source "+key+" -> unknown only, controls/bytes unchanged",()=>{
                var invalid=ChangeMetadata(frameSource,"msiReportChainSource",m=>m[key]=0);var e=ParseActual(invalid);Check(e.ReportChainSource1410==null&&Fingerprint(e)==Fingerprint(fb)&&SonyBytes(e,817).SequenceEqual(encodedBase!));
                var s=Session(e,nativePid);Check(s.Frame(e,out _)&&s._controller!.RawCalls==1);
            });
        Test("unknown nested native source key disables source only",()=>{
            var invalid=ChangeMetadata(frameSource,"msiReportChainSource",m=>m["unrecognized"]=1);var e=ParseActual(invalid);Check(e.ReportChainSource1410==null&&Fingerprint(e)==Fingerprint(fb));
        });
        Test("wrong source container ignored without ordinary-control rejection",()=>{
            var m=JsonNode.Parse(frameSource)!.AsObject();m["msiReportChainSource"]="invalid";var e=ParseActual(m.ToJsonString());Check(e.ReportChainSource1410==null&&Fingerprint(e)==Fingerprint(fb));
        });
        Test("unknown nested PREPARE optional authorizer disables trace only",()=>{
            var invalid=ChangeMetadata(prepareDiagnostic,"msiReportChainDiagnostic",m=>m["unrecognized"]=1);var e=ParseActual(invalid);Check(e.ReportChainPrepareSpecified1410&&e.ReportChainPrepare1410==null&&Fingerprint(e)==Fingerprint(pd));
        });
        Test("native activation epoch mismatch disables optional only, ordinary epoch unchanged",()=>{
            var invalid=ChangeMetadata(prepareDiagnostic,"msiReportChainDiagnostic",m=>m["epoch"]=pd.Epoch+1);var e=ParseActual(invalid);Check(e.ReportChainPrepare1410==null&&e.Epoch==pd.Epoch&&Fingerprint(e)==Fingerprint(pd));
        });
        Test("duplicate top-level native source -> unknown only, parser still accepts controls",()=>{
            using var d=JsonDocument.Parse(frameSource);var metadata=d.RootElement.GetProperty("msiReportChainSource").GetRawText();
            var text=frameSource.TrimEnd();text=text[..^1]+",\"msiReportChainSource\":"+metadata+"}";var e=ParseActual(text);Check(e.ReportChainSource1410==null&&Fingerprint(e)==Fingerprint(fb));
        });
        Test("duplicate nested native source -> unknown only",()=>{
            var needle="\"msiReportChainSource\":{";var at=frameSource.IndexOf(needle,StringComparison.Ordinal);Check(at>=0);
            var text=frameSource.Insert(at+needle.Length,"\"schemaVersion\":1,");var e=ParseActual(text);Check(e.ReportChainSource1410==null&&Fingerprint(e)==Fingerprint(fb));
        });
        Test("ordinary unknown control field still rejected, not downgraded as trace metadata",()=>{
            var m=JsonNode.Parse(frameBase)!.AsObject();m["unknownControl"]=1;Check(!TryParseEnvelope(m.ToJsonString(),out _,out var error)&&error=="unknown-field");
        });
        Test("negotiated source counter uses UInt64 not rounded IEEE754 Number",()=>{
            const ulong exact=9007199254740993UL;var text=ChangeMetadata(frameSource,"msiReportChainSource",m=>{m["sourceFrameSequence"]=exact;m["sourceGeneration"]=ulong.MaxValue;});var e=ParseActual(text);Check(e.ReportChainSource1410!.SourceFrameSequence==exact&&e.ReportChainSource1410.SourceGeneration==ulong.MaxValue);
        });
        Test("normalization hash validation still rejects changed controls with stale native hash",()=>{
            var m=JsonNode.Parse(frameSource)!.AsObject();m["buttons"]=fd.Buttons^0x1000;Check(!TryParseEnvelope(m.ToJsonString(),out _,out var error)&&error=="normalized-input-hash-mismatch");
        });
        Test("throwing trace digest cannot alter current raw wrapper result or count",()=>{
            var s=Session(pd,nativePid);s.TestRuntimeFactory=()=>new(()=>0,digest:_=>throw new IOException("digest"),sink:_=>{});s.ArmProduction(pd);
            Check(s.Frame(fd,out var detail)&&detail=="report-submitted"&&s._controller!.RawCalls==1&&s.Counts.Ok==1&&s.Counts.Fail==0);
        });
        Test("throwing trace logger cannot alter current raw wrapper result or count",()=>{
            var s=Session(pd,nativePid);s.TestRuntimeFactory=()=>new(()=>0,sink:_=>throw new IOException("logger"));s.ArmProduction(pd);
            Check(s.Frame(fd,out var detail)&&detail=="report-submitted"&&s._controller!.RawCalls==1&&s.Counts.Ok==1&&s.Counts.Fail==0);
        });
        var report=new {schemaVersion=1,result="PASS",actualNativeHostWireTests=Passed.Count,tests=Passed,
            fixturePath=Path.GetFullPath(args[0]),fixtureSha256=Sha(File.ReadAllBytes(args[0])),
            nativePidBinding=new {suppliedNativePid=nativePid,runId=pd.RunId,epoch=pd.Epoch,powerGeneration=pd.PowerGeneration,diagnosticSessionId=authorization.Activation.DiagnosticSessionId,
                scope="bound to actual native fixture metadata and inert Host parent ID;not live process identity"},
            sourceCorrelation=new {sourceSequence,sourceGeneration,sourceSessionId=source.DiagnosticSessionId,normalizedInputHash=fd.NormalizedInputHash,
                scope="native final submitted-frame reference only;raw physical-source bits unavailable"},
            nativeWireHashes=new {prepareBase=WireSha(prepareBase),prepareWithDiagnostic=WireSha(prepareDiagnostic),frameBase=WireSha(frameBase),frameWithSource=WireSha(frameSource)},
            encodedSonyDataOnly=new {length=encodedBase!.Length,baseSha256=Sha(encodedBase),withSourceSha256=Sha(encodedSource!),equal=encodedBase.SequenceEqual(encodedSource!),scope="actual current Host builder argument;fake backend;not driver/Steam delivery"},
            prepareRequestIdParity=new {baseId=pb.RequestId,diagnosticId=pd.RequestId,excludedOnlyIndependentRequestId=true},
            SDKLoaded=false,productExecuted=false,hardwareObserved=false,fullProductHandshakeTested=false,
            claim="actual parent-generated native wire accepted by frozen current Host parser/fake-call boundary;not hardware repair or live paired package verification"};
        File.WriteAllText(args[1],JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true})+Environment.NewLine);
        Console.WriteLine(JsonSerializer.Serialize(new{result="PASS",actualNativeHostWireTests=Passed.Count,nativePid,sourceSequence,sourceGeneration,encodedBytesEqual=true,SDKLoaded=false,productExecuted=false,hardwareObserved=false}));return 0;
    }
}

