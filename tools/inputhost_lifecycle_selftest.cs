// SDK-free compatibility and assertions for the existing exact-consumer fixture.
// Every optional observer is an inert no-op. No SDK or PnP API is referenced.
using System.Text.Json;
internal static partial class Program
{
    internal static partial class Fixture
    {
        internal static bool OutputStops=true, IdleStops=true, FrameFails;
        internal static bool IdleCaptureFails, IdleCtsDisposeFails;
        internal static int FrameSubmitCalls;
        internal static long NextSubmitElapsedUs=137;
        internal static bool CompactAppend;
        internal static readonly List<object> FrameControllers=new();
    }
    private const long LockedCoreBytes=63076352;
    private const string CostStateDir="inert-no-files";
    private static Stage? _msiHostStageEvidence;
    private sealed class Stage { public void Decode(string a,ulong b,string c,ulong d,ulong e,string f){} }
    private static bool CostLoggingRequestedByFlag()=>false;
    private static class ScreenPadSubmissionEvidence { public static void Reset(){} }
    private static void StopSteamDeckIdleForRelease(HMController controller)
    { Fixture.Events.Add("idle-stop");if(!Fixture.IdleStops)throw new InvalidOperationException("injected-idle-stall"); }
    private static class HostControllerOutputStop
    {
        public static bool TryStop(HMController controller,out string detail)
        {Fixture.Events.Add("output-stop");detail=Fixture.OutputStops?"stopped":"injected-reader-stall";return Fixture.OutputStops;}
    }
    // This remains an explicit terminal stand-in in the existing lifecycle
    // fixture. The independent SDK-stop suite executes the real CTS helper.
    private static class HostControllerIdleCtsRelease
    {
        internal sealed class Captured
        {
            private bool _returned;
            public void MarkControllerDisposeReturned(){_returned=true;Fixture.Events.Add("sdk-dispose-returned");}
            public bool TryRelease(out string detail)
            {
                Fixture.Events.Add("idle-cts-release");
                detail=Fixture.IdleCtsDisposeFails?"injected-idle-cts-dispose-failure":"released";
                return _returned&&!Fixture.IdleCtsDisposeFails;
            }
        }
        public static bool TryCapture(HMController controller,out Captured? capture,out string detail)
        {
            Fixture.Events.Add("idle-cts-capture");detail="injected-idle-cts-capture";
            capture=Fixture.IdleCaptureFails?null:new Captured();return capture is not null;
        }
    }
    internal static class MsiPnpCensusDiagnostic { internal sealed class Receipt {} internal sealed class Scope : IDisposable { public Scope(bool enabled){} public void Dispose(){} } }
    private sealed class MsiNonSdReleaseDiagnostic
    {
        public MsiNonSdReleaseDiagnostic(string persona){}
        public static bool ShouldObserve(bool enabled,string persona)=>false;
        public void TrySetBeforeCreateCensus(object? observation){}
        public string TryAppend(string message,int max)=>message;
    }
    private sealed class CostDiagnostic { public CostDiagnostic(bool enabled,string dir){} public void InvalidateController(){} }
    // Optional diagnostic no-op only; emits no SDK witness or lifecycle evidence.
    private sealed class HostSdkFirstWitness { public void BindLifetime(object? controller,object? lifetime){}
        public string TryAppend(string message,int maxBytes)=>message; }
    private sealed class TargetEvidence
    {
        public object? Capture()=>null;
        public long BeginAttempt()=>0;public void Failure(long attempt){} public void Created(long attempt,HMController controller){}public void ReleaseAttempt(){}
        public string TryAppend(string message,string command,int max)=>Fixture.CompactAppend&&command=="SUBMIT_FRAME"?message[..^1]+",\"fixtureAppend\":true}":message;
    }
    private sealed class HostReleaseTimingEvidence
    {
        public enum Stage {LifecycleGate,SteamDeckIdleStop,OutputReaderStop,Neutral,ControllerDispose,ReportChainClose,RemoveAllAttempt1,RemoveAllAttempt2,EndpointWaitAttempt1,EndpointWaitAttempt2,ContextDispose,FinalObservation}
        public static HostReleaseTimingEvidence? TryCreate()=>null;public void Begin(Stage stage){}public void End(Stage stage){}public void Complete(bool ok){}public void Finish(){}
        public string TryAppend(string message,int max)=>message;
    }
    private static class HostRuntimeBinaryIdentity
    {
        public sealed class Snapshot {}
        public static Snapshot? Capture(string a,string b,string c,string d,string e,long f,string g,string h)=>null;
        public static string AppendToHello(string message,Snapshot? identity,int max)=>message;
    }
    private sealed partial class HostSession
    {
        private bool _steamDeckReleasePending,_outputReleasePending,_idleCtsReleasePending;
        private HostControllerIdleCtsRelease.Captured? _idleCtsRelease;
        private string _sdkDisposeFailure="";
        private bool LocalReleasePending=>_steamDeckReleasePending||_outputReleasePending||_idleCtsReleasePending||_sdkDisposeFailure.Length!=0;
        private readonly bool _msiNonSdReleaseDiagnosticRequested,_msiCostDeferredRequested;
        private readonly CostDiagnostic? _msiCostDiagnostics;
        private TargetEvidence? _targetCreationLifetime;
        private HostSdkFirstWitness? _sdkFirstWitness;
        private MsiNonSdReleaseDiagnostic? _msiNonSdReleaseDiagnostic;
        private CommandEnvelope? _msiNonSdReleaseDiagnosticBinding,_releaseTimingBinding;
        private HostReleaseTimingEvidence? _releaseTimingEvidence;
        private HostRuntimeBinaryIdentity.Snapshot? _runtimeBinaryIdentity;
        private void BeginMsiNonSdReleaseDiagnostic(){} private void CompleteMsiNonSdReleaseDiagnostic(bool ok){}
        private void ObserveMsiNonSdReleaseDiagnostic(VirtualEndpointObservation observation,long tick){}
        private void CaptureMsiSdkDeclaredIdentityDiagnostic(){}private void LogUsbipTransportLifecycleSafely(string phase){}
        public bool HasController=>_controller is not null;public bool HasContext=>_context is not null;public string PhaseName=>_phase.ToString();
    }
    private static Dictionary<string,object> Group(string name,Action test)
    {
        Fixture.Reset();Fixture.OutputStops=Fixture.IdleStops=true;Fixture.FrameFails=false;Fixture.IdleCaptureFails=Fixture.IdleCtsDisposeFails=false;
        try{test();return new(){["name"]=name,["pass"]=true};}
        catch(Exception ex){return new(){["name"]=name,["pass"]=false,["error"]=ex.ToString(),["trace"]=Fixture.Events.ToArray()};}
    }
    private static void Accept(JsonElement response)=>Check(response.GetProperty("ok").GetBoolean(),response.ToString());
    private static void Reject(JsonElement response,string status)=>Check(!response.GetProperty("ok").GetBoolean()&&response.GetProperty("status").GetString()==status,response.ToString());
    private static HostSession Ready()
    {var s=Prepared();Fixture.OnSweep=()=>Fixture.Current=Empty();return s;}
    private static void NoDispose()=>Check(!Fixture.Events.Contains("controller-dispose")&&!Fixture.Events.Contains("context-dispose")&&!Fixture.Events.Contains("existing-sweep"),"resources escaped writer barrier");
    private static bool ContinueLegacy;
    private static readonly List<Dictionary<string,object>> LegacyCases=new();
    private static void LegacyRecord(string name,Action test)
    {
        try {test();LegacyCases.Add(new(){["name"]=name,["pass"]=true});}
        catch(Exception ex) {LegacyCases.Add(new(){["name"]=name,["pass"]=false,["error"]=ex.Message});}
    }
    private static Dictionary<string,object?> FrameMessage(ulong sequence)
    {
        var m=Message("SUBMIT_FRAME",sequence);
        m["buttons"]=0;m["lx"]=0;m["ly"]=0;m["rx"]=0;m["ry"]=0;m["lt"]=0;m["rt"]=0;
        m["sampleSequence"]=sequence;m["monotonicTick"]=sequence;m["motionPairProofId"]="offline";
        m["normalizedInputHash"]=CanonicalFrameHash(0,0,0,0,0,0,0);
        return m;
    }
    private static JsonElement Frame(HostSession s,ulong sequence)=>Send(s,FrameMessage(sequence),out _);
    private static int LifecycleMain(Func<int> runLegacy)
    {
        ContinueLegacy=true;var legacyExit=runLegacy();
        var rows=new List<Dictionary<string,object>>();
        rows.Add(Group("HELLO PREPARE NEUTRAL FRAME QUIESCE RELEASE SHUTDOWN actual dispatcher",()=>{
            var s=Ready();Accept(Send(s,"SUBMIT_NEUTRAL",3));Accept(Frame(s,4));Accept(Send(s,"QUIESCE",5));
            Reject(Frame(s,6),"state-rejected");Accept(Send(s,"RELEASE_TARGET",7));
            Check(!s.HasController&&!s.HasContext);var shut=Message("SHUTDOWN",8);Accept(Send(s,shut,out var shutdown));Check(shutdown);
            Check(Fixture.Events.IndexOf("idle-stop")<Fixture.Events.IndexOf("controller-dispose"));
            Check(Fixture.Events.IndexOf("output-stop")<Fixture.Events.IndexOf("controller-dispose"));
        }));
        foreach(var pair in new[]{("dualsense-edge","dualsense-edge"),("elite","xbox-elite-v2")})
            rows.Add(Group(pair.Item1+" current target lifecycle and barrier retry",()=>{
                var s=new HostSession("fake-nonce");
                JsonElement SendPersona(string cmd,ulong seq){var m=Message(cmd,seq);m["persona"]=pair.Item1;m["profileIdentity"]=pair.Item2;return Send(s,m,out _);}
                Fixture.OnObserve=()=>Fixture.Creates==0||Fixture.Sweeps>0?Empty():new VirtualEndpointObservation(true,1,0,1,0,0,true,"current-persona-arrival");
                Accept(SendPersona("HELLO",1));Accept(SendPersona("PREPARE_TARGET",2));Accept(SendPersona("SUBMIT_NEUTRAL",3));Accept(SendPersona("QUIESCE",4));
                Fixture.OutputStops=false;Fixture.Events.Clear();Reject(SendPersona("RELEASE_TARGET",5),"release-local-dispose-failed");NoDispose();Check(s.HasController&&s.HasContext);
                Fixture.OutputStops=true;Accept(SendPersona("RELEASE_TARGET",6));Accept(SendPersona("SHUTDOWN",7));Check(!s.HasController&&!s.HasContext);
            }));
        rows.Add(Group("current parser rejects all three obsolete wrong-pair inputs",()=>{
            foreach(var pair in new[]{("dualshock4","dualshock-4-v2"),("xbox360","xbox-360-wired"),("steamdeck","wrong-profile")}){
                var m=Message("PREPARE_TARGET");m["persona"]=pair.Item1;m["profileIdentity"]=pair.Item2;m["msiSteamDeckReleaseFence"]=true;
                Check(!TryParseEnvelope(JsonSerializer.Serialize(m),out _,out var error));Check(error=="persona-not-admitted",error);
            }
        }));
        rows.Add(Group("output stop failure retains resources and RELEASE retry disposes",()=>{
            var s=Ready();Fixture.OutputStops=false;Fixture.Events.Clear();Reject(Send(s,"RELEASE_TARGET",3),"release-local-dispose-failed");
            Check(s.HasController&&s.HasContext&&s.PhaseName=="Quiesced");NoDispose();
            Fixture.OutputStops=true;Accept(Send(s,"RELEASE_TARGET",4));Check(!s.HasController&&!s.HasContext);
        }));
        rows.Add(Group("output stop failure rejects SHUTDOWN until successful retry",()=>{
            var s=Ready();Fixture.OutputStops=false;Fixture.Events.Clear();Reject(Send(s,Message("SHUTDOWN",3),out var shutdown),"shutdown-local-dispose-failed");
            Check(!shutdown&&s.HasController&&s.HasContext);NoDispose();Fixture.OutputStops=true;Accept(Send(s,Message("SHUTDOWN",4),out shutdown));Check(shutdown);
        }));
        rows.Add(Group("output stop failure prevents RECREATE from creating second target",()=>{
            var s=Ready();Fixture.OutputStops=false;Fixture.Events.Clear();Reject(Send(s,"RECREATE_TARGET",3),"recreate-release-failed");NoDispose();Check(Fixture.Creates==1&&s.HasController&&s.HasContext);
            Fixture.OutputStops=true;Accept(Send(s,"RECREATE_TARGET",4));Check(Fixture.Creates==2&&s.HasController);
        }));
        rows.Add(Group("idle stop failure prevents output stop and destruction",()=>{
            var s=Ready();Fixture.IdleStops=false;Fixture.Events.Clear();Reject(Send(s,"RELEASE_TARGET",3),"release-local-dispose-failed");
            NoDispose();Check(!Fixture.Events.Contains("output-stop"));Fixture.IdleStops=true;Accept(Send(s,"RELEASE_TARGET",4));
        }));
        rows.Add(Group("partial PREPARE retains objects when release barrier fails; retry gates create",()=>{
            var s=new HostSession("fake-nonce");Accept(Send(s,"HELLO",1));Fixture.OutputStops=false;
            Fixture.OnObserve=()=>Fixture.Creates==0?Empty():Empty("arrival-never-admitted");
            Reject(Send(s,"PREPARE_TARGET",2,true),"prepare-rejected");Check(s.HasController&&s.HasContext&&s.PhaseName=="Quiesced");NoDispose();
            Reject(Send(s,"PREPARE_TARGET",3,true),"state-rejected");Reject(Send(s,"RECREATE_TARGET",4),"recreate-release-failed");Check(Fixture.Creates==1);
            Fixture.OutputStops=true;var cleanup=Send(s,Message("SHUTDOWN",5),out var shutdown);
            Reject(cleanup,"shutdown-local-dispose-failed");Check(shutdown&&!s.HasController&&!s.HasContext);
            Check(!cleanup.GetProperty("msiSteamDeckReleaseEvidence").GetProperty("absentVerified").GetBoolean());
        }));
        rows.Add(Group("frame backend failure releases and makes next frame terminal",()=>{
            var s=Ready();Accept(Send(s,"SUBMIT_NEUTRAL",3));Fixture.FrameFails=true;Reject(Frame(s,4),"frame-rejected");
            Check(!s.HasController&&!s.HasContext);Reject(Frame(s,5),"state-rejected");
        }));
        rows.Add(Group("frame backend failure with stalled output keeps resources for retry",()=>{
            var s=Ready();Accept(Send(s,"SUBMIT_NEUTRAL",3));Fixture.FrameFails=true;Fixture.OutputStops=false;Fixture.Events.Clear();
            Reject(Frame(s,4),"frame-rejected");Check(s.HasController&&s.HasContext&&s.PhaseName=="Quiesced");NoDispose();Fixture.OutputStops=true;Accept(Send(s,"RELEASE_TARGET",5));
        }));
        rows.Add(Group("target tuple change cannot replace retained target or release pins",()=>{
            var s=Ready();Fixture.OutputStops=false;Reject(Send(s,"RELEASE_TARGET",3),"release-local-dispose-failed");Fixture.Events.Clear();
            var change=Message("RECREATE_TARGET",4);change["epoch"]=18;Reject(Send(s,change,out _),"protocol-error");
            Check(Fixture.Creates==1&&s.HasController&&Fixture.Events.Count==0);Fixture.OutputStops=true;Accept(Send(s,"RELEASE_TARGET",4));
        }));
        rows.Add(Group("idempotent PREPARE replay creates once; changed replay rejected",()=>{
            var s=Ready();var before=Fixture.Creates;Accept(Send(s,"PREPARE_TARGET",2,true));Check(Fixture.Creates==before);
            Reject(Send(s,"PREPARE_TARGET",2,false),"protocol-error");Check(Fixture.Creates==before);
        }));
        rows.Add(Group("idle CTS capture failure keeps controller and blocks SDK Dispose",()=>{
            var s=Ready();Fixture.IdleCaptureFails=true;Fixture.Events.Clear();
            Reject(Send(s,"RELEASE_TARGET",3),"release-local-dispose-failed");
            Check(s.HasController&&s.HasContext&&s.PhaseName=="Quiesced");NoDispose();
            Fixture.IdleCaptureFails=false;Accept(Send(s,"RELEASE_TARGET",4));
        }));
        rows.Add(Group("idle CTS disposal follows successful SDK return and retries same pending resource",()=>{
            var s=Ready();Fixture.IdleCtsDisposeFails=true;Fixture.Events.Clear();
            Reject(Send(s,"RELEASE_TARGET",3),"release-local-dispose-failed");
            Check(!s.HasController&&s.HasContext&&s.PhaseName=="Quiesced");
            Check(Fixture.Events.IndexOf("controller-dispose")<Fixture.Events.IndexOf("idle-cts-release"));
            Check(!Fixture.Events.Contains("context-dispose")&&!Fixture.Events.Contains("existing-sweep"));
            Fixture.IdleCtsDisposeFails=false;Accept(Send(s,"RELEASE_TARGET",4));
            Check(!s.HasContext&&Fixture.Events.Count(e=>e=="controller-dispose")==1&&Fixture.Events.Count(e=>e=="idle-cts-capture")==1);
        }));
        rows.Add(Group("SDK Dispose exception never authorizes idle CTS explicit disposal",()=>{
            var s=Ready();Fixture.ControllerThrows=true;Fixture.Events.Clear();
            Reject(Send(s,"RELEASE_TARGET",3),"release-local-dispose-failed");
            Check(!Fixture.Events.Contains("sdk-dispose-returned")&&!Fixture.Events.Contains("idle-cts-release"));
        }));
        foreach(var failure in new[]{"controller","context"})rows.Add(Group("SDK Dispose "+failure+" exception survives second recreate without false release",()=>{
            var s=Prepared(false);Fixture.OnSweep=()=>Fixture.Current=Empty();Fixture.Events.Clear();
            Fixture.ControllerThrows=failure=="controller";Fixture.ContextThrows=failure=="context";
            var first=Send(s,"RECREATE_TARGET",3);var afterFirstController=s.HasController;var afterFirstContext=s.HasContext;
            Fixture.ControllerThrows=Fixture.ContextThrows=false;
            var second=Send(s,"RECREATE_TARGET",4);var release=Send(s,"RELEASE_TARGET",5);
            Reject(first,"recreate-release-failed");Reject(second,"recreate-release-failed");Reject(release,"release-local-dispose-failed");
            Check(afterFirstContext&&s.HasContext,"a failed SDK Dispose must retain owned context for unresolved-release evidence");
            if(failure=="controller")Check(afterFirstController&&s.HasController,"a failed controller Dispose cannot drop the only owned controller reference");
            Check(Fixture.Creates==1&&s.PhaseName=="Quiesced","no second target may be created after irreversible SDK cleanup exception");
            var shutdown=Send(s,Message("SHUTDOWN",6),out var exits);Reject(shutdown,"shutdown-local-dispose-failed");Check(!exits,"unresolved SDK cleanup cannot acknowledge Host shutdown");
            Check(Fixture.Events.Count(e=>e=="controller-dispose")==1,"SDK disposed flag may already be set; do not retry the same Dispose as fresh proof");
        }));
        rows.Add(Group("RECREATE preserves MSI owned absence requirement",()=>{
            var s=Ready();Accept(Send(s,"RECREATE_TARGET",3));Fixture.OnSweep=null;var r=Send(s,"RELEASE_TARGET",4);
            Reject(r,"release-local-dispose-failed");Check(!r.GetProperty("msiSteamDeckReleaseEvidence").GetProperty("absentVerified").GetBoolean());
        }));
        var legacyFailures=LegacyCases.Where(r=>!(bool)r["pass"]).ToArray();
        var legacyCompatible=legacyExit==0&&legacyFailures.Length==3&&legacyFailures.Select(r=>(string)r["name"]).Order().SequenceEqual(new[]{
            "flag rejected wrong persona/profile: (dualshock4, dualshock-4-v2)",
            "flag rejected wrong persona/profile: (xbox360, xbox-360-wired)",
            "flag rejected wrong persona/profile: (steamdeck, wrong-profile)"}.Order());
        var passed=legacyCompatible&&rows.All(r=>(bool)r["pass"]);
        Console.WriteLine(JsonSerializer.Serialize(new{passed,legacyExit,legacyCases=LegacyCases,legacyFailures,cases=rows,hardwareIo=false,productEntryExecuted=false,scope="Exact current parser/dispatch/prepare/release/waits/serializer; injected SDK/neutral/frame/stop results and no-op diagnostics"}));return passed?0:1;
    }

#if COMPACT_FRAME_REPLY_FIXTURE
    // This section is compiled only for the explicit compact group. All calls
    // enter the current production Handle/parser/dispatcher/serializer.
    private sealed partial class HostSession
    {
        internal object? FixtureController=>_controller;
        internal bool FixtureBindingLive=>_compactFrameReplies.TryGetToken(_controller,out _);
        internal string FixtureCurrentToken=>_compactFrameReplies.TryGetToken(_controller,out var token)?token:string.Empty;
        internal string FixturePreparedAck {get;set;}="";
        internal void FixtureEnableAppend()=>_targetCreationLifetime=new TargetEvidence();
        internal string FixtureAppendBinding(string response,string command="PREPARE_TARGET",bool ok=true,string status="target-prepared")=>AppendCompactFrameReplyBinding(response,Env(command),ok,status);
    }
    private static bool LowerToken(string? token)=>token is {Length:64}&&token.All(c=>c is >= '0' and <= '9' or >= 'a' and <= 'f');
    private static string PreparedToken(JsonElement response)
    {
        Accept(response);Check(response.GetProperty("status").GetString()=="target-prepared");
        Check(response.TryGetProperty("hostInstanceId",out _)&&!response.TryGetProperty("compactFrameReplyVersion",out _),"prepare/recreate must use the full ACK");
        var token=response.GetProperty("frameReplyBinding").GetString();Check(LowerToken(token),"prepared ACK must mint a lower hex token");return token!;
    }
    private static HostSession CompactPrepared(out string token)
    {
        var s=new HostSession("fake-nonce");Accept(Send(s,"HELLO",1));
        var prepared=Send(s,"PREPARE_TARGET",2,true);s.FixturePreparedAck=prepared.GetRawText();token=PreparedToken(prepared);
        Check(s.FixtureController is not null&&s.FixtureBindingLive,"actual TryPrepare must own the non-null dummy SDK controller");
        Fixture.OnSweep=()=>Fixture.Current=Empty();return s;
    }
    private static HostSession CompactReady(out string token)
    {var s=CompactPrepared(out token);Accept(Send(s,"SUBMIT_NEUTRAL",3));return s;}
    private static JsonElement CompactFrame(HostSession s,ulong sequence,string token,string? requestId=null)
    {
        var m=FrameMessage(sequence);m["frameReplyBinding"]=token;if(requestId is not null)m["requestId"]=requestId;
        return Send(s,m,out _);
    }
    private static void CompactShape(JsonElement response,ulong sequence,string requestId,string token,bool first,long elapsed,bool appended=false)
    {
        Accept(response);Check(response.GetProperty("compactFrameReplyVersion").GetInt32()==1);
        Check(response.EnumerateObject().Count()==(appended?11:10),"compact base must contain exactly ten fields before original append hooks");
        Check(response.GetProperty("protocolVersion").GetInt32()==1&&response.GetProperty("command").GetString()=="SUBMIT_FRAME");
        Check(response.GetProperty("status").GetString()=="frame-accepted"&&response.GetProperty("sequence").GetUInt64()==sequence);
        Check(response.GetProperty("requestId").GetString()==requestId&&response.GetProperty("frameReplyBinding").GetString()==token);
        Check(response.GetProperty("firstFrame").GetBoolean()==first&&response.GetProperty("submitElapsedUs").GetInt64()==elapsed);
        Check(!response.TryGetProperty("hostInstanceId",out _));
        if(appended)Check(response.GetProperty("fixtureAppend").GetBoolean(),"production dynamic append path was bypassed");
    }
    private static void RejectBeforeSdk(HostSession s,Dictionary<string,object?> request,string status="protocol-error",string? detail=null)
    {
        var calls=Fixture.FrameSubmitCalls;var creates=Fixture.Creates;var events=Fixture.Events.Count;
        var rejected=Send(s,request,out _);
        Check(Fixture.FrameSubmitCalls==calls&&Fixture.Creates==creates&&Fixture.Events.Count==events,
            detail=="frame-reply-binding-mismatch"?"wrong binding reached backend":"invalid identity reached SDK or lifecycle IO");
        Reject(rejected,status);
        if(detail is not null)Check(rejected.GetProperty("detail").GetString()==detail);
        Check(!rejected.TryGetProperty("compactFrameReplyVersion",out _),"errors must retain the full response");
    }
    private static void RejectBindingBeforeSdk(HostSession s,Dictionary<string,object?> request)=>RejectBeforeSdk(s,request,"state-rejected","frame-reply-binding-mismatch");
    private sealed class EqualController
    {public override bool Equals(object? other)=>other is EqualController;public override int GetHashCode()=>1;}
    private static int CompactFrameReplyMain(string? selectedCase=null)
    {
        int? fullWireBytes=null,compactWireBytes=null;object? wireByteSemantics=null;
        var tests=new List<(string Name,Action Body)>();
        tests.Add(("compact: full compatibility and fresh compact wire ACK",()=>{
            var fullHost=CompactReady(out _);var full=Frame(fullHost,4);Accept(full);
            Check(full.TryGetProperty("hostInstanceId",out _)&&!full.TryGetProperty("compactFrameReplyVersion",out _),"omitting binding must preserve full ACK");
            // An independent offline backend snapshot, not another physical target.
            Fixture.Current=Empty();var compactHost=CompactReady(out var token);
            var compact=CompactFrame(compactHost,4,token);CompactShape(compact,4,"req-4",token,true,137);
            foreach(var field in new[]{"protocolVersion","ok","command","requestId","sequence","status","firstFrame","submitElapsedUs"})
                Check(full.GetProperty(field).GetRawText()==compact.GetProperty(field).GetRawText(),"wire comparison semantics differ: "+field);
            fullWireBytes=System.Text.Encoding.UTF8.GetByteCount(full.GetRawText());compactWireBytes=System.Text.Encoding.UTF8.GetByteCount(compact.GetRawText());
            Check(fullWireBytes>compactWireBytes);wireByteSemantics=new{source="two actual HostSession.Handle responses",sequence=4,requestId="req-4",status="frame-accepted",firstFrame=true,submitElapsedUs=137,communicationOnly=true};
            Fixture.NextSubmitElapsedUs=long.MaxValue-17;var request="req-5-雪-\"\\";
            CompactShape(CompactFrame(compactHost,5,token,request),5,request,token,false,Fixture.NextSubmitElapsedUs);
            compactHost.FixtureEnableAppend();Fixture.CompactAppend=true;Fixture.NextSubmitElapsedUs=811;
            CompactShape(CompactFrame(compactHost,6,token),6,"req-6",token,false,811,true);
            Fixture.FrameFails=true;var failed=CompactFrame(compactHost,7,token);Reject(failed,"frame-rejected");
            Check(!failed.TryGetProperty("compactFrameReplyVersion",out _)&&!compactHost.FixtureBindingLive,"backend failure must revoke and retain the full error");
        }));
        tests.Add(("compact: incorrect binding rejects before SDK and resumes at next sequence",()=>{
            var s=CompactReady(out var token);var request=FrameMessage(4);
            request["frameReplyBinding"]=token[0]=='0'?"1"+token[1..]:"0"+token[1..];
            RejectBindingBeforeSdk(s,request);
            var stale=FrameMessage(4);stale["frameReplyBinding"]=token;RejectBeforeSdk(s,stale);
            CompactShape(CompactFrame(s,5,token),5,"req-5",token,true,137);
            // IsSha256 admits uppercase; the ordinal binding check rejects it
            // after sequence admission, so this rejection consumes seq 6.
            var upper=FrameMessage(6);upper["frameReplyBinding"]=new string('A',64);RejectBindingBeforeSdk(s,upper);
            CompactShape(CompactFrame(s,7,token),7,"req-7",token,false,137);
            Check(Fixture.FrameSubmitCalls==2&&Fixture.FrameControllers.All(c=>ReferenceEquals(c,s.FixtureController)));
        }));
        tests.Add(("compact: recreate rotates owner and cached prepare cannot reauthorize old generation",()=>{
            var s=CompactReady(out var oldToken);var oldController=s.FixtureController;var creates=Fixture.Creates;
            var sameGenerationReplay=Send(s,"PREPARE_TARGET",2,true);
            Check(sameGenerationReplay.GetRawText()==s.FixturePreparedAck&&PreparedToken(sameGenerationReplay)==oldToken&&Fixture.Creates==creates&&ReferenceEquals(oldController,s.FixtureController),"same-generation cached PREPARE must return identical bytes without recreating or revoking");
            CompactShape(CompactFrame(s,4,oldToken),4,"req-4",oldToken,true,137);
            var token=PreparedToken(Send(s,"RECREATE_TARGET",5));
            Check(token!=oldToken&&s.FixtureController is not null&&!ReferenceEquals(oldController,s.FixtureController)&&Fixture.Creates==creates+1);
            creates=Fixture.Creates;var replay=Send(s,"PREPARE_TARGET",2,true);
            Check(replay.GetRawText()==s.FixturePreparedAck&&PreparedToken(replay)==oldToken,"historical PREPARE must retain the original cached ACK bytes and old token");
            Check(Fixture.Creates==creates&&s.FixtureCurrentToken==token&&s.FixtureBindingLive,"historical ACK must not rebind or reauthorize its old controller generation");Accept(Send(s,"SUBMIT_NEUTRAL",6));
            var stale=FrameMessage(7);stale["frameReplyBinding"]=oldToken;RejectBindingBeforeSdk(s,stale);
            CompactShape(CompactFrame(s,8,token),8,"req-8",token,true,137);
        }));
        tests.Add(("compact: quiesce legacy resume release and shutdown revoke authority",()=>{
            var s=CompactReady(out var token);var controller=s.FixtureController;
            Accept(Send(s,"QUIESCE",4));Check(!s.FixtureBindingLive);
            // Pre-execution plan correction from production source: neutral in
            // Quiesced retains that phase and the same owner; it cannot resume
            // frames or mint a binding. RECREATE is required before submission.
            Accept(Send(s,"SUBMIT_NEUTRAL",5));
            Check(s.PhaseName=="Quiesced"&&ReferenceEquals(controller,s.FixtureController)&&!s.FixtureBindingLive&&s.FixtureCurrentToken=="","quiesced neutral must preserve phase and owner without minting a binding");
            var old=FrameMessage(6);old["frameReplyBinding"]=token;RejectBindingBeforeSdk(s,old);
            RejectBeforeSdk(s,FrameMessage(7),"state-rejected","frame-requires-neutral");
            Check(Fixture.FrameSubmitCalls==0&&s.PhaseName=="Quiesced"&&!s.FixtureBindingLive);
            var newToken=PreparedToken(Send(s,"RECREATE_TARGET",8));
            Check(newToken!=token&&s.FixtureController is not null&&!ReferenceEquals(controller,s.FixtureController)&&s.FixtureCurrentToken==newToken);
            Accept(Send(s,"SUBMIT_NEUTRAL",9));
            var legacy=Frame(s,10);Accept(legacy);
            Check(legacy.TryGetProperty("hostInstanceId",out _)&&!legacy.TryGetProperty("compactFrameReplyVersion",out _)&&legacy.GetProperty("firstFrame").GetBoolean()&&s.FixtureCurrentToken==newToken);
            CompactShape(CompactFrame(s,11,newToken),11,"req-11",newToken,false,137);
            Check(Fixture.FrameSubmitCalls==2&&Fixture.FrameControllers.All(c=>ReferenceEquals(c,s.FixtureController)));
            var release=Send(s,"RELEASE_TARGET",12);Accept(release);Check(!s.FixtureBindingLive&&!s.HasController);
            old=FrameMessage(13);old["frameReplyBinding"]=newToken;RejectBindingBeforeSdk(s,old);
            var shut=Message("SHUTDOWN",14);Accept(Send(s,shut,out var exits));Check(exits&&!s.FixtureBindingLive);
            Fixture.Current=Empty();var t=CompactReady(out _);Accept(Send(t,Message("SHUTDOWN",4),out exits));Check(exits&&!t.FixtureBindingLive&&!t.HasController);
        }));
        tests.Add(("compact: independent HostSession rejects another session token",()=>{
            var first=CompactReady(out var foreign);var firstOwner=first.FixtureController;
            Fixture.Current=Empty();var second=CompactReady(out var token);
            Check(token!=foreign&&!ReferenceEquals(firstOwner,second.FixtureController));
            var m=FrameMessage(4);m["frameReplyBinding"]=foreign;RejectBindingBeforeSdk(second,m);
            CompactShape(CompactFrame(second,5,token),5,"req-5",token,true,137);
            CompactShape(CompactFrame(first,4,foreign),4,"req-4",foreign,true,137);
        }));
        tests.Add(("compact: warm canonical hash never bypasses nonce or tuple",()=>{
            var s=CompactReady(out var token);CompactShape(CompactFrame(s,4,token),4,"req-4",token,true,137);
            foreach(var field in new[]{"startupNonce","runId","epoch"}){
                var m=FrameMessage(5);m["frameReplyBinding"]=token;m[field]=field=="epoch"?(object)18UL:"wrong-identity";
                RejectBeforeSdk(s,m);
            }
            CompactShape(CompactFrame(s,5,token),5,"req-5",token,false,137);
            Check(Fixture.FrameSubmitCalls==2,"warm identical payload must still validate identity");
        }));
        tests.Add(("compact: current grammar rejects unknown typed and duplicate bindings",()=>{
            var s=CompactReady(out var token);
            foreach(var invalid in new object?[]{null,17,true,"",new string('a',63),new string('g',64)}){
                var m=FrameMessage(4);m["frameReplyBinding"]=invalid;RejectBeforeSdk(s,m);
            }
            var unknown=FrameMessage(4);unknown["frameReplyBinding"]=token;unknown["unknownBinding"]=token;RejectBeforeSdk(s,unknown);
            var valid=FrameMessage(4);valid["frameReplyBinding"]=token;var raw=JsonSerializer.Serialize(valid);
            var duplicate=raw[..^1]+",\"frameReplyBinding\":"+JsonSerializer.Serialize(token)+"}";
            var calls=Fixture.FrameSubmitCalls;using(var doc=JsonDocument.Parse(s.Handle(duplicate,out _)))Reject(doc.RootElement,"protocol-error");
            Check(Fixture.FrameSubmitCalls==calls);CompactShape(CompactFrame(s,4,token),4,"req-4",token,true,137);
        }));
        tests.Add(("compact: binding helper revocation and converter values stay fresh",()=>{
            var binding=new CompactFrameReplyBinding();var a=new EqualController();var b=new EqualController();
            var old=binding.Bind(a);Check(LowerToken(old)&&binding.Matches(a,old)&&!binding.Matches(b,old));
            Check(!binding.TryGetToken(b,out var missing)&&missing=="");var token=binding.Bind(b);
            Check(LowerToken(token)&&token!=old&&!binding.Matches(a,old)&&binding.TryGetToken(b,out var current)&&current==token);
            Check(!binding.Matches(b,null)&&!binding.Matches(b,old));
            var threw=false;try{binding.Bind(null!);}catch(ArgumentNullException){threw=true;}
            Check(threw&&!binding.Matches(b,token)&&!binding.TryGetToken(b,out missing)&&missing=="");
            token=binding.Bind(a);binding.Revoke();binding.Revoke();Check(!binding.Matches(a,token)&&!binding.TryGetToken(a,out _));
            token=binding.Bind(b);const ulong seq=9007199254740993UL;
            using var one=JsonDocument.Parse(JsonSerializer.Serialize(new CompactFrameReplyFields(1,true,"SUBMIT_FRAME","雪-\"\\",seq,token,"frame-accepted",true,long.MaxValue-3)));
            CompactShape(one.RootElement,seq,"雪-\"\\",token,true,long.MaxValue-3);
            using var two=JsonDocument.Parse(JsonSerializer.Serialize(new CompactFrameReplyFields(1,true,"SUBMIT_FRAME","次",seq+1,token,"frame-accepted",false,long.MinValue+3)));
            CompactShape(two.RootElement,seq+1,"次",token,false,long.MinValue+3);
        }));
        tests.Add(("compact: final UTF8 advertisement stays within message budget",()=>{
            var host=CompactPrepared(out var token);const string empty="{\"diagnostic\":\"\"}";
            var advertised=host.FixtureAppendBinding(empty);
            using(var document=JsonDocument.Parse(advertised))Check(document.RootElement.GetProperty("frameReplyBinding").GetString()==token);
            var suffixBytes=System.Text.Encoding.UTF8.GetByteCount(advertised)-System.Text.Encoding.UTF8.GetByteCount(empty);
            string DiagnosticBytes(int bytes,bool unicode){
                var available=bytes-System.Text.Encoding.UTF8.GetByteCount(empty);Check(available>=0);
                var padding=unicode?new string('雪',available/3)+new string('x',available%3):new string('x',available);
                var response="{\"diagnostic\":\""+padding+"\"}";Check(System.Text.Encoding.UTF8.GetByteCount(response)==bytes);return response;
            }
            foreach(var unicode in new[]{false,true}){
                var exact=DiagnosticBytes(MaxMessageBytes-suffixBytes,unicode);
                var fit=host.FixtureAppendBinding(exact);Check(System.Text.Encoding.UTF8.GetByteCount(fit)==MaxMessageBytes);
                using(var document=JsonDocument.Parse(fit))Check(document.RootElement.GetProperty("frameReplyBinding").GetString()==token);
                var over=DiagnosticBytes(MaxMessageBytes-suffixBytes+1,unicode);Check(host.FixtureAppendBinding(over)==over,"UTF8 overflow must suppress advertisement");
                var alreadyOver=DiagnosticBytes(MaxMessageBytes+1,unicode);Check(host.FixtureAppendBinding(alreadyOver)==alreadyOver);
            }
            Check(host.FixtureAppendBinding(empty,"RECREATE_TARGET")==advertised);
            Check(host.FixtureAppendBinding(empty,"SUBMIT_FRAME")==empty&&host.FixtureAppendBinding(empty,ok:false)==empty&&host.FixtureAppendBinding(empty,status:"prepare-rejected")==empty);
            // Real final helper boundary only. Serializer terminal wiring is
            // bound by the extracted production member/source identity; no PS,
            // diagnostic payload producer, device or motor callback is invoked.
        }));
        if(selectedCase is not null&&!tests.Any(t=>t.Name==selectedCase)){
            Console.Error.WriteLine("Unknown or empty compact case selection");return 2;
        }
        var rows=tests.Where(t=>selectedCase is null||t.Name==selectedCase).Select(t=>Group(t.Name,()=>{
            Fixture.FrameSubmitCalls=0;Fixture.FrameControllers.Clear();Fixture.NextSubmitElapsedUs=137;Fixture.CompactAppend=false;t.Body();
        })).ToArray();
        var passed=rows.Length>0&&rows.All(r=>(bool)r["pass"]);
        Console.WriteLine(JsonSerializer.Serialize(new{passed,group="compact-frame-reply",selectedCase,cases=rows,fullWireBytes,compactWireBytes,wireByteSemantics,
            hardwareIo=false,productEntryExecuted=false,legacyExecuted=false,
            boundaryHelperScope="Final UTF8 advertisement helper boundary only for synthetic large responses; actual serializer terminal wiring/source extracted and statically bound. PS/diagnostic producers/device/motor NOT_RUN.",
            scope="Current production Handle/parse/dispatch/prepare/release/serializer and real compact helper; inert SDK terminal records frames/owner references and injected elapsed us. UTF8 sizes are communication only, not CPU or installed-device evidence."}));return passed?0:1;
    }
#endif

#if COMPACT_FRAME_REPLY_COST
    // Current source, two independently prepared HostSessions. The full arm is
    // the current Host's legacy-compatible reply mode, not a historical build.
    private static readonly (string Persona,string Profile)[] CompactCostPersonas={
        /* compact-cost-persona */ ("steamdeck","steam-deck-composite"),
        /* compact-cost-persona */ ("dualsense-edge","dualsense-edge"),
        /* compact-cost-persona */ ("elite","xbox-elite-v2")};
    private const int CostWarmup=32,CostPairs=11,CostBatchSize=128,CostFramesPerSide=CostWarmup+CostPairs*CostBatchSize;
    internal static partial class Fixture
    {
        internal static long CostSubmitCalls;
        internal static object? CostLastController;
    }
    [System.Runtime.InteropServices.DllImport("kernel32.dll",EntryPoint="QueryThreadCycleTime",SetLastError=true)]
    private static extern bool CostQueryThreadCycleTime(IntPtr thread,out ulong cycles);
    [System.Runtime.InteropServices.DllImport("kernel32.dll",EntryPoint="GetCurrentThread")]
    private static extern IntPtr CostGetCurrentThread();
    private readonly record struct CostStart(ulong cycles,long allocated,long wall);
    private sealed record CostCounters(ulong cyclesStart,ulong cyclesEnd,long allocatedStart,long allocatedEnd,long wallStart,long wallEnd)
    {
        public ulong cycles=>cyclesEnd-cyclesStart;
        public long threadAllocatedBytes=>allocatedEnd-allocatedStart;
        public long wallTicks=>wallEnd-wallStart;
        public double wallMs=>wallTicks*1000.0/System.Diagnostics.Stopwatch.Frequency;
    }
    private static ulong CostCycles()
    {
        if(!CostQueryThreadCycleTime(CostGetCurrentThread(),out var cycles))
            throw new InvalidOperationException("QueryThreadCycleTime unavailable: "+System.Runtime.InteropServices.Marshal.GetLastWin32Error());
        return cycles;
    }
    private static CostStart CostBegin()=>new(CostCycles(),GC.GetAllocatedBytesForCurrentThread(),System.Diagnostics.Stopwatch.GetTimestamp());
    private static CostCounters CostEnd(CostStart start)
    {
        var wall=System.Diagnostics.Stopwatch.GetTimestamp();var allocated=GC.GetAllocatedBytesForCurrentThread();var cycles=CostCycles();
        Check(cycles>=start.cycles&&allocated>=start.allocated&&wall>=start.wall,"cost counters moved backwards");
        return new(start.cycles,cycles,start.allocated,allocated,start.wall,wall);
    }
    private sealed record CostSide(HostSession session,string token,CostCounters setup,string[] setupResponses);
    private sealed record CostPayload(int requestBytes,int responseBytes,int validatedResponses,string firstRequest,string lastRequest,string firstResponse,string lastResponse);
    private static Dictionary<string,object?> CostMessage(string command,ulong sequence,string persona,string profile,string run)
    {
        var m=Message(command,sequence);m["persona"]=persona;m["profileIdentity"]=profile;m["runId"]=run;return m;
    }
    private static CostSide CostPrepare(string persona,string profile,string run)
    {
        Fixture.Reset();Fixture.OutputStops=Fixture.IdleStops=true;Fixture.FrameFails=Fixture.IdleCaptureFails=Fixture.IdleCtsDisposeFails=Fixture.CompactAppend=false;
        Fixture.FrameSubmitCalls=0;Fixture.FrameControllers.Clear();Fixture.NextSubmitElapsedUs=137;
        Fixture.OnObserve=()=>Fixture.Creates==0||Fixture.Sweeps>0?Empty():persona=="steamdeck"?Arrival(Fixture.NextRoot):new VirtualEndpointObservation(true,1,0,1,0,0,true,"cost-persona-arrival");
        Fixture.OnSweep=()=>Fixture.Current=Empty();
        var prepare=CostMessage("PREPARE_TARGET",2,persona,profile,run);if(persona=="steamdeck")prepare["msiSteamDeckReleaseFence"]=true;
        var requests=new[]{JsonSerializer.Serialize(CostMessage("HELLO",1,persona,profile,run)),JsonSerializer.Serialize(prepare),JsonSerializer.Serialize(CostMessage("SUBMIT_NEUTRAL",3,persona,profile,run))};
        var responses=new string[3];var shutdown=false;
        var start=CostBegin();var session=new HostSession("fake-nonce");
        for(var i=0;i<requests.Length;i++){responses[i]=session.Handle(requests[i],out var exits);shutdown|=exits;}
        var counters=CostEnd(start);
        Check(!shutdown);using(var hello=JsonDocument.Parse(responses[0]))Accept(hello.RootElement);
        string token;using(var prepared=JsonDocument.Parse(responses[1]))token=PreparedToken(prepared.RootElement);
        using(var neutral=JsonDocument.Parse(responses[2]))Accept(neutral.RootElement);
        Check(session.PhaseName=="Neutralized"&&session.FixtureController is not null&&session.FixtureCurrentToken==token);
        return new(session,token,counters,responses);
    }
    private static (string[] full,string[] compact) CostRequests(string persona,string profile,string run,bool imu,bool changing,string token)
    {
        var full=new string[CostFramesPerSide];var compact=new string[CostFramesPerSide];
        for(var i=0;i<CostFramesPerSide;i++){
            var sequence=4UL+(ulong)i;var m=CostMessage("SUBMIT_FRAME",sequence,persona,profile,run);
            var buttons=changing?(i*17)&0xffff:4096;var lx=changing?((i%255)-127)/128f:0f;var ly=.5f;var rx=-.25f;var ry=0f;
            var lt=changing?(i%17)/16f:.25f;var rt=.75f;
            m["requestId"]="cost-req-"+sequence;m["buttons"]=buttons;m["lx"]=lx;m["ly"]=ly;m["rx"]=rx;m["ry"]=ry;m["lt"]=lt;m["rt"]=rt;
            m["sampleSequence"]=1UL+(ulong)i;m["monotonicTick"]=100000UL+(ulong)i;m["motionPairProofId"]="cost-motion-pair";
            // The real canonical helper recomputes changing hashes here, before
            // all measurement windows. Handle still computes/validates its hash.
            m["normalizedInputHash"]=CanonicalFrameHash(buttons,lx,ly,rx,ry,lt,rt);
            if(imu)m["imu"]=new{admitted=true,gyroDps=new{x=changing?i*.01:.125,y=-.25,z=.5},accelG=new{x=.125,y=changing?i*.0001:0.0,z=1.0},quaternion=new{w=1.0,x=0.0,y=0.0,z=0.0}};
            full[i]=JsonSerializer.Serialize(m);m["frameReplyBinding"]=token;compact[i]=JsonSerializer.Serialize(m);
        }
        // Actual parser shape validation is outside the windows, on first/last
        // requests of both arms. These are not additional Handle invocations.
        foreach(var messages in new[]{full,compact})foreach(var index in new[]{0,CostFramesPerSide-1}){
            Check(TryParseEnvelope(messages[index],out var envelope,out var error),error);
            Check(envelope!.Persona==persona&&envelope.Sequence==4UL+(ulong)index&&envelope.SampleSequence==1UL+(ulong)index&&envelope.MonotonicTick==100000UL+(ulong)index);
            Check(envelope.ImuAdmitted==imu&&envelope.ImuQuaternionPresent==imu);
            if(imu)Check(envelope.ImuAccelZ==1.0&&envelope.ImuQuatW==1.0&&envelope.ImuGyroY==-.25);
        }
        return(full,compact);
    }
    private static CostCounters CostHandleBatch(CostSide side,string[] requests,int offset,string[] responses,out bool shutdown)
    {
        shutdown=false;var start=CostBegin();
        for(var i=0;i<responses.Length;i++){responses[i]=side.session.Handle(requests[offset+i],out var exits);shutdown|=exits;}
        return CostEnd(start);
    }
    private static CostPayload CostValidate(CostSide side,string[] requests,int offset,string[] responses,bool compact,string persona,string profile)
    {
        var requestBytes=0;var responseBytes=0;
        for(var i=0;i<responses.Length;i++){
            var index=offset+i;var sequence=4UL+(ulong)index;var requestId="cost-req-"+sequence;
            using var document=JsonDocument.Parse(responses[i]);var response=document.RootElement;
            // Eight shared semantic properties, checked for every response
            // outside both timed endpoints of the pair.
            Accept(response);Check(response.GetProperty("protocolVersion").GetInt32()==1&&response.GetProperty("command").GetString()=="SUBMIT_FRAME");
            Check(response.GetProperty("requestId").GetString()==requestId&&response.GetProperty("sequence").GetUInt64()==sequence&&response.GetProperty("status").GetString()=="frame-accepted");
            Check(response.GetProperty("firstFrame").GetBoolean()==(index==0)&&response.GetProperty("submitElapsedUs").GetInt64()==137);
            if(compact)Check(response.GetProperty("compactFrameReplyVersion").GetInt32()==1&&response.GetProperty("frameReplyBinding").GetString()==side.token&&!response.TryGetProperty("hostInstanceId",out _));
            else Check(!response.TryGetProperty("compactFrameReplyVersion",out _)&&response.GetProperty("persona").GetString()==persona&&response.GetProperty("profileIdentity").GetString()==profile&&response.TryGetProperty("hostInstanceId",out _));
            requestBytes+=System.Text.Encoding.UTF8.GetByteCount(requests[index]);responseBytes+=System.Text.Encoding.UTF8.GetByteCount(responses[i]);
        }
        return new(requestBytes,responseBytes,responses.Length,requests[offset],requests[offset+responses.Length-1],responses[0],responses[^1]);
    }
    private static void CostCleanup(CostSide side,string persona,string profile,string run)
    {
        var message=CostMessage("SHUTDOWN",4UL+(ulong)CostFramesPerSide,persona,profile,run);
        using var response=JsonDocument.Parse(side.session.Handle(JsonSerializer.Serialize(message),out var shutdown));Accept(response.RootElement);
        Check(shutdown&&!side.session.HasController&&!side.session.HasContext&&!side.session.FixtureBindingLive);
    }
    private static int CompactFrameReplyCostMain()
    {
        var rows=new List<object>();var passed=true;var measuredCalls=0;var warmupCalls=0;var setupCalls=0;var cleanupCalls=0;
        var culture=System.Globalization.CultureInfo.CurrentCulture.Name;var uiCulture=System.Globalization.CultureInfo.CurrentUICulture.Name;
        var decimalSeparator=System.Globalization.CultureInfo.CurrentCulture.NumberFormat.NumberDecimalSeparator;
        var rawPath=Path.GetFullPath("compact-frame-reply-cost.raw.jsonl");
        using var raw=new StreamWriter(new FileStream(rawPath,FileMode.CreateNew,FileAccess.Write,FileShare.Read),new System.Text.UTF8Encoding(false)){AutoFlush=true};
        void Raw(object value)=>raw.WriteLine(JsonSerializer.Serialize(value));
        Raw(new{recordType="design",group="compact-frame-reply-cost",culture,uiCulture,decimalSeparator,warmupPerSide=CostWarmup,pairedRounds=CostPairs,framesPerRoundPerSide=CostBatchSize,expectedMeasuredHandleCalls=33792,fullArm="Current-source newly prepared HostSession, optional binding omitted; not a historical product baseline",setupScope="HostSession construction and HELLO/PREPARE_TARGET/SUBMIT_NEUTRAL Handle calls, including actual TryPrepare and Bind; request formatting and response checks excluded",counterScope="raw QueryThreadCycleTime ulong start/end/delta; never converted to milliseconds",sdkTerminal="same cost-only no-allocation counter/reference/constant-elapsed stub for both arms",responseValidation="all eight shared properties on every response outside both pair windows",motorCallbacks="NOT_RUN",sdkWorkerCost="NOT_MEASURED"});
        try{
            Check(OperatingSystem.IsWindows(),"QueryThreadCycleTime requires Windows");_ = CostCycles();_ = GC.GetAllocatedBytesForCurrentThread();_ = System.Diagnostics.Stopwatch.GetTimestamp();
            foreach(var pair in CompactCostPersonas)foreach(var imu in new[]{false,true})foreach(var changing in new[]{false,true}){
                var name=pair.Persona+"/imu-"+(imu?"on":"off")+"/"+(changing?"changing":"static");var run="cost-"+name.Replace('/','-');
                var full=CostPrepare(pair.Persona,pair.Profile,run);setupCalls+=3;Raw(new{recordType="setup",name,arm="full",metrics=full.setup,responses=full.setupResponses});
                var compact=CostPrepare(pair.Persona,pair.Profile,run);setupCalls+=3;Raw(new{recordType="setup",name,arm="compact",metrics=compact.setup,responses=compact.setupResponses});
                Check(!ReferenceEquals(full.session.FixtureController,compact.session.FixtureController)&&full.token!=compact.token);
                var requests=CostRequests(pair.Persona,pair.Profile,run,imu,changing,compact.token);
                foreach(var arm in new[]{false,true}){
                    var side=arm?compact:full;var messages=arm?requests.compact:requests.full;var responses=new string[CostWarmup];var callsBefore=Fixture.CostSubmitCalls;
                    for(var i=0;i<responses.Length;i++){responses[i]=side.session.Handle(messages[i],out var exits);Check(!exits);}
                    Check(Fixture.CostSubmitCalls-callsBefore==CostWarmup&&ReferenceEquals(Fixture.CostLastController,side.session.FixtureController));
                    var payload=CostValidate(side,messages,0,responses,arm,pair.Persona,pair.Profile);warmupCalls+=CostWarmup;
                    Raw(new{recordType="warmup",name,arm=arm?"compact":"full",handleCalls=CostWarmup,payload,excludedFromMeasured=true});
                }
                var rounds=new List<object>();var fullCycles=new List<ulong>();var compactCycles=new List<ulong>();var deltas=new List<decimal>();
                for(var round=0;round<CostPairs;round++){
                    Fixture.Events.Clear();Fixture.FrameControllers.Clear();var callsBefore=Fixture.CostSubmitCalls;
                    var offset=CostWarmup+round*CostBatchSize;var fullResponses=new string[CostBatchSize];var compactResponses=new string[CostBatchSize];
                    CostCounters fullMetrics,compactMetrics;bool fullShutdown,compactShutdown;
                    if(round%2==0){fullMetrics=CostHandleBatch(full,requests.full,offset,fullResponses,out fullShutdown);compactMetrics=CostHandleBatch(compact,requests.compact,offset,compactResponses,out compactShutdown);}
                    else{compactMetrics=CostHandleBatch(compact,requests.compact,offset,compactResponses,out compactShutdown);fullMetrics=CostHandleBatch(full,requests.full,offset,fullResponses,out fullShutdown);}
                    // Both windows have ended before parsing/UTF8 counts/logging.
                    measuredCalls+=2*CostBatchSize;
                    Raw(new{recordType="pair-measured",name,round=round+1,order=round%2==0?"AB":"BA",framesPerArm=CostBatchSize,fullMetrics,compactMetrics,fullShutdown,compactShutdown,measuredCalls});
                    Check(!fullShutdown&&!compactShutdown&&Fixture.CostSubmitCalls-callsBefore==2*CostBatchSize);
                    Check(Fixture.Events.Count==0&&Fixture.FrameControllers.Count==0,"cost SDK terminal unexpectedly grew semantic recorders");
                    Check(System.Globalization.CultureInfo.CurrentCulture.Name==culture&&System.Globalization.CultureInfo.CurrentUICulture.Name==uiCulture&&System.Globalization.CultureInfo.CurrentCulture.NumberFormat.NumberDecimalSeparator==decimalSeparator,"culture changed between arms");
                    var fullPayload=CostValidate(full,requests.full,offset,fullResponses,false,pair.Persona,pair.Profile);
                    var compactPayload=CostValidate(compact,requests.compact,offset,compactResponses,true,pair.Persona,pair.Profile);
                    var delta=(decimal)compactMetrics.cycles-fullMetrics.cycles;fullCycles.Add(fullMetrics.cycles);compactCycles.Add(compactMetrics.cycles);deltas.Add(delta);
                    var row=new{round=round+1,order=round%2==0?"AB":"BA",firstSequence=4UL+(ulong)offset,lastSequence=3UL+(ulong)(offset+CostBatchSize),framesPerArm=CostBatchSize,
                        full=new{metrics=fullMetrics,payload=fullPayload},compact=new{metrics=compactMetrics,payload=compactPayload},compactMinusFullCycles=delta};
                    rounds.Add(row);Raw(new{recordType="pair",name,data=row});
                }
                CostCleanup(full,pair.Persona,pair.Profile,run);cleanupCalls++;CostCleanup(compact,pair.Persona,pair.Profile,run);cleanupCalls++;
                var result=new{name,pass=true,persona=pair.Persona,profile=pair.Profile,imu=imu?"on":"off",canonical=changing?"changing":"static",setup=new{full=full.setup,compact=compact.setup,includesBindAndWholeHostSetup=true},warmupPerArm=CostWarmup,measuredCallsPerArm=CostPairs*CostBatchSize,rounds,
                    medianFullCycles=fullCycles.Order().ElementAt(CostPairs/2),medianCompactCycles=compactCycles.Order().ElementAt(CostPairs/2),medianPairedCompactMinusFullCycles=deltas.Order().ElementAt(CostPairs/2)};
                rows.Add(result);Raw(new{recordType="case-complete",data=result});
            }
            Check(rows.Count==12&&measuredCalls==33792&&warmupCalls==768&&setupCalls==72&&cleanupCalls==24,"cost fixed workload count drift");
        }catch(Exception exception){passed=false;Raw(new{recordType="failure",error=exception.ToString(),measuredCalls,warmupCalls,setupCalls,cleanupCalls});rows.Add(new{name="INCOMPLETE",pass=false,error=exception.ToString()});}
        Console.WriteLine(JsonSerializer.Serialize(new{passed,group="compact-frame-reply-cost",cases=rows,measuredHandleCalls=measuredCalls,warmupHandleCalls=warmupCalls,setupHandleCalls=setupCalls,cleanupHandleCalls=cleanupCalls,
            warmupPerSide=CostWarmup,pairedRounds=CostPairs,framesPerRoundPerSide=CostBatchSize,stopwatchFrequency=System.Diagnostics.Stopwatch.Frequency,managedThreadId=Environment.CurrentManagedThreadId,culture,uiCulture,decimalSeparator,rawPath,
            cyclesUnit="raw ulong thread cycles; no milliseconds conversion",wallUnit="Stopwatch ticks and derived wallMs",allocationUnit="GC.GetAllocatedBytesForCurrentThread bytes",fullArm="Current-source newly prepared HostSession, legacy-compatible full replies; not historical before-product",
            passMeaning="Fixed workload completed and actual Handle response semantics validated; no speed threshold or required improvement",sdkWorkerCost="NOT_MEASURED",motorOutputCallbackStatus="NOT_RUN",hardwareIo=false,productEntryExecuted=false,legacyExecuted=false,semanticSuiteExecuted=false,
            scope="Actual production Handle/parse/hash/tuple/serializer and original AppendGameOutputObservation plus remaining response appenders. Same inert SDK terminal in both arms. Setup+Bind separate; requests/hash construction and all response validation/UTF8/logging outside measured pair windows. No installed-device CPU claim."}));return passed?0:1;
    }
#endif
}
