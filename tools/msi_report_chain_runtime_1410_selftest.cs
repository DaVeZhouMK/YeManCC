// Actual extracted Host parser/builders/wrapped backend/neutral/response tests.
// Inert HM types only. LB+RB reservation/summon proposal WITHDRAWN/out of scope.
// No SDK/native loading, hardware observer, shortcut, OEM or service behavior.
using System.Security.Cryptography;
using System.Text.Json;
using YeManInputHost.Diagnostics;
internal static partial class Program
{
    private static int _cases;
    private static void Check(bool ok,string message="assertion"){if(!ok)throw new Exception(message);}
    private static void Test(string name,Action body){body();++_cases;Console.WriteLine("PASS "+name);}
    private static Dictionary<string,object?> Message(string cmd="PREPARE_TARGET", string persona="dualsense-edge",ulong seq=1,int buttons=0)
    {
        var m=new Dictionary<string,object?>{{"protocolVersion",1},{"command",cmd},{"requestId","request-"+seq},{"sequence",seq},
            {"runId","fake-run"},{"epoch",2},{"powerGeneration",1},{"targetId","fake-target"},{"ownerIdentity","coordinator"},
            {"persona",persona},{"profileIdentity",persona=="elite"?"xbox-elite-v2":persona=="xbox360"?"xbox-360-wired":persona},
            {"startupNonce","fake-nonce"},{"configRevision",1},{"configHash",new string('a',64)}};
        if(cmd=="SUBMIT_FRAME")foreach(var kv in new Dictionary<string,object?>{{"buttons",buttons},{"lx",0},{"ly",0},{"rx",0},{"ry",0},{"lt",0},{"rt",0},
            {"sampleSequence",seq+100},{"monotonicTick",1000},{"normalizedInputHash",CanonicalFrameHash(buttons,0,0,0,0,0,0)},{"motionPairProofId","fake-proof"}})m[kv.Key]=kv.Value;
        return m;
    }
    private static Dictionary<string,object?> Activation()=>new(){{"schemaVersion",1},{"enabled",true},{"msiClawKnownBoardAuthority",true},
        {"visibilityDiagnosticBuild",true},{"virtualDetailLogEnabled",true},{"explicitSessionRequested",true},{"nativePid",9844},
        {"runId","fake-run"},{"epoch",2},{"powerGeneration",1},{"diagnosticSessionId","diag-session"},{"durationMs",10000},{"maxMeaningfulEdges",128}};
    private static Dictionary<string,object?> Source(ulong seq=200,ulong generation=6)=>new(){{"schemaVersion",1},{"diagnosticSessionId","diag-session"},{"sourceFrameSequence",seq},{"sourceGeneration",generation}};
    private static CommandEnvelope Parse(Dictionary<string,object?> m)
    {Check(TryParseEnvelope(JsonSerializer.Serialize(m),out var e,out var err),err);return e!;}
    private static CommandEnvelope Prepared(string persona="dualsense-edge",object? diagnostic=null)
    {var m=Message(persona:persona);m["msiReportChainDiagnostic"]=diagnostic??Activation();return Parse(m);}
    private static HostSession Session(CommandEnvelope e, List<string>? logs=null,Func<byte[],string>? digest=null,Func<object,string>? formatter=null,Action<string>? sink=null)
    {
        var s=new HostSession(e);
        s.Response(e with{Command="HELLO"},true,"host-hello");
        if(logs!=null||digest!=null||formatter!=null||sink!=null)
        {s.TestRuntimeFactory=()=>new MsiReportChainRuntime1410(()=>0,digest,formatter,sink??(line=>logs!.Add(line)));s.ArmProduction(e);}
        return s;
    }
    private static CommandEnvelope Frame(ulong seq=2,int buttons=0,object? source=null,string persona="dualsense-edge")
    {var m=Message("SUBMIT_FRAME",persona,seq,buttons);if(source!=null)m["msiReportChainSource"]=source;return Parse(m);}
    private static JsonElement LastEntry(List<string> logs)
    {using var d=JsonDocument.Parse(logs.Last(x=>x.Contains("msi-report-chain-submit")));return d.RootElement.GetProperty("entry").Clone();}
    private static string Controls(CommandEnvelope e)=>JsonSerializer.Serialize(e);
    private static void BaselineParse(Dictionary<string,object?> m)
    {
        var text=JsonSerializer.Serialize(m);var now=TryParseEnvelope(text,out var current,out var error); var old=TryParseEnvelopeBefore1410(text,out var previous,out var oldError);
        Check(now==old&&error==oldError);if(now)Check(Controls(current!)==Controls(previous!));
    }
    private static int Main()
    {
        Test("ordinary PREPARE parser and serialized controls unchanged",()=>BaselineParse(Message()));
        Test("ordinary frame parser/hash/raw values unchanged",()=>BaselineParse(Message("SUBMIT_FRAME",seq:2,buttons:0x1000)));
        Test("ordinary neutral parser unchanged",()=>BaselineParse(Message("SUBMIT_NEUTRAL",seq:3)));
        foreach(var cmd in new[]{"HELLO","RECREATE_TARGET","QUIESCE","RELEASE_TARGET","SHUTDOWN","TUPLE_SUBMIT_STATS"})Test("ordinary "+cmd+" parser unchanged",()=>BaselineParse(Message(cmd)));
        Test("HELLO capability is ACK ROOT version1, not detail mutation",()=>{
            var e=Parse(Message("HELLO"));var s=Session(e);using var j=JsonDocument.Parse(s.Response(e,true,"host-hello"));
            Check(j.RootElement.GetProperty("msiReportChainDiagnosticVersion").GetInt32()==1&&j.RootElement.GetProperty("detail").GetString()=="detail");
        });
        Test("failed HELLO does not advertise tested capability",()=>{var e=Parse(Message("HELLO"));using var j=JsonDocument.Parse(Session(e).Response(e,false,"asset-mismatch"));Check(!j.RootElement.TryGetProperty("msiReportChainDiagnosticVersion",out _));});
        Test("PREPARE defaultOFF receipt bytes are identical to before",()=>{var e=Parse(Message());var s=Session(e);Check(s.Response(e,true,"target-prepared")==s.ResponseBefore(e,true,"target-prepared"));});
        Test("defaultOFF frame/neutral receipt bytes unchanged",()=>{var e=Frame();var s=Session(e);Check(s.Response(e,true,"frame-accepted")==s.ResponseBefore(e,true,"frame-accepted"));});
        Test("valid PREPARE metadata typed, strict, no control fingerprint delta",()=>{
            var e=Prepared();Check(e.ReportChainPrepare1410!=null&&e.ReportChainPrepareSpecified1410);Check(Controls(e)==Controls(Parse(Message())));
        });
        foreach(var key in new[]{"enabled","msiClawKnownBoardAuthority","visibilityDiagnosticBuild","virtualDetailLogEnabled","explicitSessionRequested"})
            Test("gate "+key+" false -> traceOFF not control rejection",()=>{var a=Activation();a[key]=false;var e=Prepared(diagnostic:a);Check(e.ReportChainPrepare1410==null&&e.ReportChainPrepareSpecified1410);});
        foreach(var kv in new Dictionary<string,object?>{{"schemaVersion",2},{"nativePid",0},{"runId","old"},{"epoch",3},{"powerGeneration",2},{"diagnosticSessionId",""},{"durationMs",30001},{"maxMeaningfulEdges",129}})
            Test("invalid "+kv.Key+" -> traceOFF only",()=>{var a=Activation();a[kv.Key]=kv.Value;Check(Prepared(diagnostic:a).ReportChainPrepare1410==null);});
        Test("maxMeaningfulEdges aliases capacity and hostEpoch rejected inside trace only",()=>{var a=Activation();a["capacity"]=128;Check(Prepared(diagnostic:a).ReportChainPrepare1410==null);a=Activation();a["hostEpoch"]=2;Check(Prepared(diagnostic:a).ReportChainPrepare1410==null);});
        Test("unknown nested auth metadata -> OFF not PREPARE rejection",()=>{var a=Activation();a["bogus"]=true;Check(Prepared(diagnostic:a).ReportChainPrepare1410==null);});
        Test("wrong metadata container/string type -> OFF not PREPARE rejection",()=>{foreach(var x in new object[]{"oops",new[]{1,2},123,true})Check(Prepared(diagnostic:x).ReportChainPrepare1410==null);});
        Test("wrong auth boolean type cannot arm",()=>{var a=Activation();a["enabled"]="true";Check(Prepared(diagnostic:a).ReportChainPrepare1410==null);});
        Test("duration/capacity defaults exact10000/128",()=>{var a=Activation();a.Remove("durationMs");a.Remove("maxMeaningfulEdges");var p=Prepared(diagnostic:a).ReportChainPrepare1410!;Check(p.DurationMs==10000&&p.MaxMeaningfulEdges==128);});
        Test("sessionID maximum128 enforced",()=>{var a=Activation();a["diagnosticSessionId"]=new string('x',129);Check(Prepared(diagnostic:a).ReportChainPrepare1410==null);});
        Test("duplicate TOP-LEVEL diagnostic field disables trace, preserves PREPARE",()=>{
            var m=Message();m["msiReportChainDiagnostic"]=Activation();var j=JsonSerializer.Serialize(m);j=j[..^1]+",\"msiReportChainDiagnostic\":"+JsonSerializer.Serialize(Activation())+"}";
            Check(TryParseEnvelope(j,out var e,out _)&&e!.ReportChainPrepare1410==null&&e.ReportChainPrepareSpecified1410);
        });
        Test("duplicate nested diagnostic field disables trace",()=>{
            var m=Message();var j=JsonSerializer.Serialize(m);var a=JsonSerializer.Serialize(Activation());a=a[..^1]+",\"enabled\":true}";
            j=j[..^1]+",\"msiReportChainDiagnostic\":"+a+"}";Check(TryParseEnvelope(j,out var e,out _)&&e!.ReportChainPrepare1410==null);
        });
        Test("ordinary duplicate field rejection and priority preserved",()=>{
            var j=JsonSerializer.Serialize(Message());j=j[..^1]+",\"epoch\":2}";var a=TryParseEnvelope(j,out _,out var e);var b=TryParseEnvelopeBefore1410(j,out _,out var f);Check(!a&&!b&&e==f&&e=="duplicate-field:epoch");
        });
        Test("unknown ordinary control field retains protocol rejection",()=>{var m=Message();m["unknownControl"]=1;BaselineParse(m);});
        foreach(var cmd in new[]{"HELLO","RECREATE_TARGET","SUBMIT_FRAME","SUBMIT_NEUTRAL","RELEASE_TARGET"})
            Test("PREPARE diagnostic field on "+cmd+" is inert metadata only",()=>{var m=Message(cmd);m["msiReportChainDiagnostic"]=Activation();var e=Parse(m);Check(e.ReportChainPrepare1410==null&&!e.ReportChainPrepareSpecified1410);});
        Test("launch-parentPID must match: nativePid metadata not sufficient",()=>{
            var a=Activation();a["nativePid"]=16908;var logs=new List<string>();var s=Session(Prepared(diagnostic:a),logs);Check(s._reportChain1410==null&&logs.Count==0);
        });
        Test("persona alone cannot activate ROG/GPD/common trace",()=>{
            var s=Session(Parse(Message()));s.ArmProduction(Parse(Message()));Check(s._reportChain1410==null);
        });
        Test("actual Host post-admission arm and PREPARE ack include same sessionID",()=>{
            var logs=new List<string>();var e=Prepared();var s=Session(e,logs);Check(s._reportChain1410?.Armed==true&&logs.Count==0);
            using var d=JsonDocument.Parse(s.Response(e,true,"target-prepared"));Check(d.RootElement.GetProperty("msiReportChainDiagnosticArmed").GetBoolean());Check(d.RootElement.GetProperty("msiReportChainDiagnosticSessionId").GetString()=="diag-session");
        });
        Test("invalid PREPARE request ack says false without target failure",()=>{
            var e=Prepared(diagnostic:"invalid");using var d=JsonDocument.Parse(Session(e).Response(e,true,"target-prepared"));Check(d.RootElement.GetProperty("ok").GetBoolean()&&!d.RootElement.GetProperty("msiReportChainDiagnosticArmed").GetBoolean());Check(!d.RootElement.TryGetProperty("msiReportChainDiagnosticSessionId",out _));
        });
        Test("diagnostic factory exception -> OFF without PREPARE effects",()=>{
            var e=Prepared();var s=Session(e);s.TestRuntimeFactory=()=>throw new IOException("factory");s.ArmProduction(e);Check(s._reportChain1410==null);
        });
        Test("Sony actual wrapper bytes/calls/counters identical to before",()=>{
            var e=Frame(buttons:0x1000);var before=Session(e);_dualSenseFrameCounter=20;Check(before.FrameBefore(e,out var a));var bytes=before._controller!.LastRaw!;
            var now=Session(e);_dualSenseFrameCounter=20;Check(now.Frame(e,out var b));Check(bytes.SequenceEqual(now._controller!.LastRaw!)&&a==b&&before.Counts==now.Counts&&now._controller.RawCalls==1);
        });
        Test("Elite actual state wrapper axes/buttons/calls/counters unchanged",()=>{
            var e=Frame(buttons:0x1000,persona:"elite");var old=Session(e);var now=Session(e);Check(old.FrameBefore(e,out var a)&&now.Frame(e,out var b)&&a==b);Check(old.Counts==now.Counts&&now._controller!.StateCalls==1);Check(old._controller!.LastState.Buttons==now._controller!.LastState.Buttons&&old._controller.LastState.Axes!.SequenceEqual(now._controller!.LastState.Axes!));
        });
        Test("defaultOFF neutral actual raw bytes/counters unchanged",()=>{
            var e=Parse(Message("SUBMIT_NEUTRAL",seq:33));var old=Session(e);_dualSenseFrameCounter=30;Check(old.NeutralBefore(out var a));var bytes=old._controller!.LastRaw!;
            var now=Session(e);_dualSenseFrameCounter=30;Check(now.Neutral(e,out var b));Check(a==b&&old.Counts==now.Counts&&bytes.SequenceEqual(now._controller!.LastRaw!)&&now._controller.RawCalls==1);
        });
        Test("negotiated valid source records true encoded argument + original source tuple",()=>{
            var logs=new List<string>();var s=Session(Prepared(),logs);var e=Frame(55,0x1000,Source(700,9));Check(s.Frame(e,out _));var entry=LastEntry(logs);var f=entry.GetProperty("Frame");
            Check(f.GetProperty("CommandSequence").GetUInt64()==55&&f.GetProperty("SourceFrameSequence").GetUInt64()==700&&f.GetProperty("SourceGeneration").GetUInt64()==9);
            Check(entry.GetProperty("EncodedArgumentSha256").GetString()==Convert.ToHexString(SHA256.HashData(s._controller!.LastRaw!)));
        });
        Test("missing optional source is unknown, not SampleSequence-derived proof",()=>{
            var logs=new List<string>();var s=Session(Prepared(),logs);Check(s.Frame(Frame(7,0x1000),out _));var f=LastEntry(logs).GetProperty("Frame");Check(f.GetProperty("SourceFrameSequence").ValueKind==JsonValueKind.Null&&f.GetProperty("SourceNativeButtons").ValueKind==JsonValueKind.Null);
        });
        Test("wrong source session is unknown but exact SDK submit succeeds",()=>{
            var f=Source();f["diagnosticSessionId"]="wrong";var logs=new List<string>();var s=Session(Prepared(),logs);Check(s.Frame(Frame(8,0x1000,f),out _)&&s._controller!.RawCalls==1);Check(LastEntry(logs).GetProperty("Frame").GetProperty("SourceGeneration").ValueKind==JsonValueKind.Null);
        });
        foreach(var kv in new Dictionary<string,object?>{{"schemaVersion",2},{"sourceFrameSequence",0},{"sourceGeneration",-1},{"diagnosticSessionId",true},{"unexpected",1}})
            Test("invalid source "+kv.Key+" cannot reject control",()=>{var f=Source();f[kv.Key]=kv.Value;var e=Frame(9,0x1000,f);Check(e.ReportChainSource1410==null);var s=Session(e);Check(s.Frame(e,out _)&&s._controller!.RawCalls==1);});
        Test("source metadata on non-submit command ignored",()=>{var m=Message();m["msiReportChainSource"]=Source();Check(Parse(m).ReportChainSource1410==null);});
        Test("source without activation has no logging/control impact",()=>{var e=Frame(source:Source());var s=Session(e);Check(s.Frame(e,out _)&&s._reportChain1410==null&&s._controller!.RawCalls==1);});
        Test("actual explicit neutral traces incoming sequence instead of dummyneutral1",()=>{
            var m=Message("SUBMIT_NEUTRAL",seq:901);m["msiReportChainSource"]=Source(888,4);var logs=new List<string>();var s=Session(Prepared(),logs);Check(s.Neutral(Parse(m),out _));var f=LastEntry(logs).GetProperty("Frame");
            Check(f.GetProperty("CommandSequence").GetUInt64()==901&&f.GetProperty("SourceFrameSequence").GetUInt64()==888&&f.GetProperty("Reason").GetString()=="submit-neutral-command");Check(f.GetProperty("Tuple").GetProperty("RunId").GetString()=="fake-run");
        });
        Test("release neutral source is unknown and session clear is durable once",()=>{
            var logs=new List<string>();var s=Session(Prepared(),logs);Check(s.Neutral(null,out _,"release-neutral"));Check(LastEntry(logs).GetProperty("Frame").GetProperty("SourceGeneration").ValueKind==JsonValueKind.Null);s.Close("release-local");s.Close("again");Check(logs.Count(x=>x.Contains("session-close"))==1&&s._reportChain1410==null);
        });
        Test("events only logged after backend return, never before actual submit",()=>{
            var logs=new List<string>();var s=Session(Prepared(),logs);s._controller!.DuringSubmit=()=>Check(logs.Count==0);Check(s.Frame(Frame(source:Source()),out _));Check(logs.Count==1);
        });
        foreach(var stage in new[]{"digest","formatter","sink"})Test(stage+" exception cannot change real wrapper success/calls/counts",()=>{
            var logs=new List<string>();var s=Session(Prepared(),logs,stage=="digest"?(_=>throw new IOException("digest")):null,
                stage=="formatter"?(_=>throw new IOException("serialize")):null,stage=="sink"?(_=>throw new IOException("logger")):null);
            Check(s.Frame(Frame(source:Source()),out var d)&&d=="report-submitted"&&s._controller!.RawCalls==1&&s.Counts.Ok==1&&s.Counts.Fail==0);
            Check(s.Neutral(Parse(Message("SUBMIT_NEUTRAL",seq:5)),out d)&&d=="neutral-report-submitted"&&s._controller!.RawCalls==2&&s.Counts.NeutralOk==1&&s.Counts.NeutralFail==0);
        });
        Test("backend exception status/counter unchanged even with throwing logger",()=>{
            var e=Frame(source:Source());var old=Session(e);old._controller!.Failure=new InvalidOperationException("backend");Check(!old.FrameBefore(e,out var a));
            var now=Session(Prepared(),new List<string>(),formatter:_=>throw new IOException("logger"));now._controller!.Failure=old._controller.Failure;Check(!now.Frame(e,out var b));Check(a==b&&old.Counts==now.Counts&&now._controller.RawCalls==1);
        });
        Test("Elite trace explicitly leaves encoded bytes unknown and calls state once",()=>{
            var logs=new List<string>();var s=Session(Prepared("elite"),logs);Check(s.Frame(Frame(source:Source(),persona:"elite"),out _)&&s._controller!.StateCalls==1&&s._controller.RawCalls==0);
            Check(LastEntry(logs).GetProperty("EncodedArgumentSha256").ValueKind==JsonValueKind.Null);
        });
        Test("128 meaningful-edge budget cannot suppress controller calls",()=>{
            var logs=new List<string>();var s=Session(Prepared(),logs);for(ulong i=2;i<302;i++)Check(s.Frame(Frame(i,(i%2)==0?0x1000:0,Source(i)),out _));
            Check(logs.Count(x=>x.Contains("msi-report-chain-submit"))==128&&logs.Count(x=>x.Contains("session-close"))==1&&s._controller!.RawCalls==300&&s.Counts.Ok==300);
        });
        Test("held reports do not create high-frequency log loop",()=>{
            var logs=new List<string>();var s=Session(Prepared(),logs);for(ulong i=2;i<252;i++)Check(s.Frame(Frame(i,0x1000,Source(i)),out _));Check(logs.Count==1&&s._controller!.RawCalls==250);
        });
        Test("expiry clears tracing only and closes prior completed snapshot",()=>{
            long clock=0;var logs=new List<string>();var e=Prepared();var s=Session(e);s.TestRuntimeFactory=()=>new(()=>clock,sink:logs.Add);s.ArmProduction(e);Check(s.Frame(Frame(source:Source()),out _));clock=10000;Check(s.Frame(Frame(3,0x1000,Source(3)),out _));Check(logs.Count(x=>x.Contains("session-close"))==1&&s._controller!.RawCalls==2);
        });
        Test("retarget mismatch closes trace without changing backend control",()=>{
            var logs=new List<string>();var s=Session(Prepared(),logs);Check(s.Frame(Frame(source:Source()),out _));var e=Frame(3,0x1000,Source()) with{RunId="different"};Check(s.Frame(e,out _)&&s._controller!.RawCalls==2);Check(logs.Any(x=>x.Contains("tuple-changed")));
        });
        Test("actual arm before HELLO capability advertisement remains OFF",()=>{
            var e=Prepared();var s=new HostSession(e);int factories=0;s.TestRuntimeFactory=()=>{++factories;return new(sink:_=>{});};s.ArmProduction(e);Check(s._reportChain1410==null&&factories==0);
        });
        Test("target not admitted cannot activate even with valid capability/metadata",()=>{
            var e=Prepared();var s=Session(e);s.SetAdmitted(false);int factories=0;s.TestRuntimeFactory=()=>{++factories;return new(sink:_=>{});};s.ArmProduction(e);Check(s._reportChain1410==null&&factories==0);
        });
        foreach(var fault in new[]{3,4,5,6})Test("clock fault boundary"+fault+" cannot change actual SDK result/count",()=>{
            var calls=0;var e=Prepared();var s=Session(e);var logs=new List<string>();s.TestRuntimeFactory=()=>new(()=>++calls==fault?throw new IOException("clock"):0,sink:logs.Add);s.ArmProduction(e);
            Check(s.Frame(Frame(source:Source()),out var detail)&&detail=="report-submitted"&&s._controller!.RawCalls==1&&s.Counts.Ok==1&&s.Counts.Fail==0);
        });
        Test("disposed diagnostic JSON is traceOFF without propagating parser exception",()=>{
            var d=JsonDocument.Parse(JsonSerializer.Serialize(Activation()));var el=d.RootElement;d.Dispose();Check(MsiReportChainProtocol1410.ParsePrepareSafely(el,"PREPARE_TARGET","fake-run",2,1)==null);
        });
        Test("inactive malformed source changes no bytes/counts/errors versus ordinary before",()=>{
            var m=Message("SUBMIT_FRAME",seq:77,buttons:0x1000);m["msiReportChainSource"]=new Dictionary<string,object?>{{"schemaVersion",1},{"diagnosticSessionId","x"},{"sourceFrameSequence","bad"},{"sourceGeneration",-4}};
            var current=Parse(m);m.Remove("msiReportChainSource");var plain=Parse(m);var old=Session(plain);_dualSenseFrameCounter=200;Check(old.FrameBefore(plain,out var a));var data=old._controller!.LastRaw!;
            var now=Session(current);_dualSenseFrameCounter=200;Check(now.Frame(current,out var b));Check(a==b&&old.Counts==now.Counts&&data.SequenceEqual(now._controller!.LastRaw!)&&now._controller.RawCalls==1);
        });
        Test("duplicate source metadata is unknown only, not control rejection",()=>{
            var m=Message("SUBMIT_FRAME",seq:88);m["msiReportChainSource"]=Source();var text=JsonSerializer.Serialize(m);text=text[..^1]+",\"msiReportChainSource\":"+JsonSerializer.Serialize(Source())+"}";
            Check(TryParseEnvelope(text,out var e,out _)&&e!.ReportChainSource1410==null);
        });
        Test("compat SourceNativeButtons logs final submitted canonical bits, never raw-source proof",()=>{
            var logs=new List<string>();var s=Session(Prepared(),logs);Check(s.Frame(Frame(20,0x1000,Source()),out _));var e=LastEntry(logs);
            Check(e.GetProperty("Frame").GetProperty("SourceNativeButtons").GetUInt32()==0x1000);
            var scope=e.GetProperty("DeliveryScope").GetString()!;Check(scope.Contains("native-final-submitted-frame-buttons")&&scope.Contains("raw-physical-source-bits-unavailable"));
        });
        Test("neutral/repress compatibility candidate never claims same physical press",()=>{
            var logs=new List<string>();var s=Session(Prepared(),logs);Check(s.Frame(Frame(21,0x1000,Source(500)),out _));Check(s.Neutral(Parse(Message("SUBMIT_NEUTRAL",seq:22)),out _));Check(s.Frame(Frame(23,0x1000,Source(501)),out _));
            var e=LastEntry(logs);Check(e.GetProperty("RepressAfterNeutralWithSameSourceBits").GetBoolean());Check(e.GetProperty("DeliveryScope").GetString()!.Contains("repress-not-same-physical-press-proof"));
        });
        Console.WriteLine(JsonSerializer.Serialize(new{result="PASS",actualMemberFakeTests=_cases,SDKLoaded=false,productExecuted=false,hardwareObserved=false,scope="virtual-backend-diagnostics-only; shoulder reservation withdrawn"}));return 0;
    }
}



