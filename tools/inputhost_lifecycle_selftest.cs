// SDK-free compatibility and assertions for the existing exact-consumer fixture.
// Every optional observer is an inert no-op. No SDK or PnP API is referenced.
using System.Text.Json;
internal static partial class Program
{
    internal static partial class Fixture
    {
        internal static bool OutputStops=true, IdleStops=true, FrameFails;
        internal static bool IdleCaptureFails, IdleCtsDisposeFails;
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
    private sealed class TargetEvidence
    {
        public long BeginAttempt()=>0;public void Failure(long attempt){} public void Created(long attempt,HMController controller){}public void ReleaseAttempt(){}
        public string TryAppend(string message,string command,int max)=>message;
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
    private static JsonElement Frame(HostSession s,ulong sequence)
    {
        var m=Message("SUBMIT_FRAME",sequence);
        m["buttons"]=0;m["lx"]=0;m["ly"]=0;m["rx"]=0;m["ry"]=0;m["lt"]=0;m["rt"]=0;
        m["sampleSequence"]=sequence;m["monotonicTick"]=sequence;m["motionPairProofId"]="offline";
        m["normalizedInputHash"]=CanonicalFrameHash(0,0,0,0,0,0,0);
        return Send(s,m,out _);
    }
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
}
