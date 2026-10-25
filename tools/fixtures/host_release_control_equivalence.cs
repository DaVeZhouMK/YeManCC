// Offline template. Only the marked members/cases are filled with exact source
// extractions by verify-host-release-control-equivalence.mjs. No SDK references.
using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;

internal static partial class Program
{
    @@CONSTANTS@@
    @@OBSERVATION@@
    @@PHASE@@
    private sealed record CommandEnvelope(string Command, string Persona, string RunId="fixture-run", ulong Epoch=7, string ProfileIdentity="fixture-profile");
    private static bool SameTuple(CommandEnvelope a,CommandEnvelope b) => a.RunId==b.RunId && a.Epoch==b.Epoch && a.Persona==b.Persona;
    private const string LockedCoreSha256="inert-diagnostic-core";
    private const string LockedCoreMvid="inert-diagnostic-mvid";

    private sealed class Case
    {
        public string Name {get;set;}="";
        public string Persona {get;set;}="elite";
        public bool Msi {get;set;}
        public bool Controller {get;set;}=true;
        public bool Context {get;set;}=true;
        public bool Tuple {get;set;}=true;
        public bool Diagnostic {get;set;}=true;
        public bool IdleThrow {get;set;}
        public string Neutral {get;set;}="ok";
        public bool ControllerThrow {get;set;}
        public bool Remove1Throw {get;set;}
        public bool Remove2Throw {get;set;}
        public string Wait1 {get;set;}="absent";
        public string Wait2 {get;set;}="absent";
        public bool ContextThrow {get;set;}
        public bool ReportCloseThrow {get;set;}
        public bool OnlySteamRoots {get;set;}
        public string Cache {get;set;}="none";
        public bool CachedObserveThrow {get;set;}
        public bool Sequence {get;set;}
        public bool Internal {get;set;}
        public string Command {get;set;}="RELEASE_TARGET";
        public bool Lifetime {get;set;}=true;
        public bool CostDiagnostic {get;set;}=true;
    }
    private static class Fixture
    {
        internal static Case C=null!;
        internal static string Mode="recording";
        internal static long Tick;
        internal static int Removes;
        internal static List<string> Actions=new(),Timing=new();
        internal static long Now => Tick;
        internal static void Sleep(int ms){Actions.Add("sleep:"+ms);Tick+=ms;}
        internal static VirtualEndpointObservation Observation(bool absent,bool failed=false)
        {
            if(failed)return new(false,0,0,0,0,0,false,"fixture-probe-failed");
            if(absent)return new(true,0,0,0,0,0,true,"fixture-absent") {PresentInstanceIds=Array.Empty<string>(),SteamDeckRootInstanceIds=Array.Empty<string>(),SteamDeckHidChildInstanceIds=Array.Empty<string>()};
            bool steam=C.Msi || C.OnlySteamRoots;
            return new(true,steam?0:1,0,steam?0:1,steam?1:0,steam?1:0,true,"fixture-present") {
                PresentInstanceIds=new[]{"USB\\FIXTURE_ROOT","HID\\FIXTURE_CHILD"},
                SteamDeckRootInstanceIds=new[]{"USB\\FIXTURE_ROOT"},SteamDeckHidChildInstanceIds=new[]{"HID\\FIXTURE_CHILD"}};
        }
    }
    private static VirtualEndpointObservation ObserveVirtualEndpoint(string? persona=null,bool requireInstanceIds=false)
    {
        Fixture.Actions.Add("observe:"+(persona??"null")+":ids="+requireInstanceIds+":attempt="+Fixture.Removes+":tick="+Fixture.Now);
        if(Fixture.Removes==0){if(Fixture.C.CachedObserveThrow)throw new IOException("cached-probe");return Fixture.Observation(false);}
        string outcome=Fixture.Removes==1?Fixture.C.Wait1:Fixture.C.Wait2;
        if(outcome=="throw")throw new IOException("wait-"+Fixture.Removes);
        return Fixture.Observation(outcome=="absent",outcome=="probe-failed");
    }
    @@WAIT@@
    private sealed class FakeController : IDisposable
    {
        public void Dispose(){Fixture.Actions.Add("controller.dispose");if(Fixture.C.ControllerThrow)throw new InvalidOperationException("controller-dispose");}
    }
    private sealed class HMContext : IDisposable
    {
        internal static void RemoveAllVirtualControllers(bool preserveBackend)
        {
            int n=++Fixture.Removes;Fixture.Actions.Add("remove:"+n+":preserve="+preserveBackend);
            if(n==1&&Fixture.C.Remove1Throw)throw new IOException("remove-1");
            if(n==2&&Fixture.C.Remove2Throw)throw new InvalidOperationException("remove-2");
        }
        public void Dispose(){Fixture.Actions.Add("context.dispose");if(Fixture.C.ContextThrow)throw new ApplicationException("context-dispose");}
    }
    private sealed class Lifetime { public void ReleaseAttempt()=>Fixture.Actions.Add("lifetime.release-attempt"); }
    private sealed class CostDiagnostic { public void InvalidateController()=>Fixture.Actions.Add("cost.invalidate"); }
    private static class MsiPnpCensusDiagnostic
    {
        internal sealed class Receipt {}
        internal sealed class Scope : IDisposable
        {
            internal Scope(bool enabled)=>Fixture.Actions.Add("pnp-scope.begin:"+enabled);
            public void Dispose()=>Fixture.Actions.Add("pnp-scope.dispose");
        }
    }
    private sealed class XboxSdkDiagnostics : IDisposable
    {
        internal static XboxSdkDiagnostics TryBegin(string persona,string host,string run,ulong epoch,string operation,string core,string mvid)
        {Fixture.Actions.Add("sdk-diagnostics.begin:"+persona+":"+host+":"+run+":"+epoch+":"+operation+":"+core+":"+mvid);return new();}
        internal void SetEndpoint(string detail)=>Fixture.Actions.Add("sdk-diagnostics.endpoint:"+detail);
        public void Dispose()=>Fixture.Actions.Add("sdk-diagnostics.dispose");
    }
    // This records timing calls only. It deliberately has no access to Host
    // objects, business actions, virtual time, observations, results or errors.
    // Its no-throw API contract is the separate collector agent's test scope.
    private sealed class HostReleaseTimingEvidence
    {
        internal enum Stage {LifecycleGate,SteamDeckIdleStop,DiagnosticsBefore,Neutral,ControllerDispose,ReportChainClose,RemoveAllAttempt1,EndpointWaitAttempt1,RemoveAllAttempt2,EndpointWaitAttempt2,ContextDispose,FinalObservation}
        internal static HostReleaseTimingEvidence? TryCreate(){Fixture.Timing.Add("create:"+Fixture.Mode);return Fixture.Mode=="unavailable"?null:new();}
        internal void Begin(Stage s)=>Fixture.Timing.Add("begin:"+s);
        internal void End(Stage s)=>Fixture.Timing.Add("end:"+s);
        internal void Complete(bool ok)=>Fixture.Timing.Add("complete:"+ok);
        internal void Finish()=>Fixture.Timing.Add("finish");
        internal string TryAppend(string response,int maxBytes)=>response;
    }
    private sealed class HostSession
    {
        private readonly object _lifecycleGate=new();
        private Lifetime? _targetCreationLifetime;
        private CostDiagnostic? _msiCostDiagnostics;
        private object? _msiNonSdReleaseDiagnostic;
        private MsiSteamDeckReleaseFence1726? _msiSteamDeckReleaseFence1726;
        private FakeController? _controller;
        private HMContext? _context;
        private CommandEnvelope? _tuple;
        private string _hostInstanceId="fixture-instance";
        private Phase _phase=Phase.Active;
        private bool _steamDeckReleasePending,_targetPnpAdmitted=true;
        private VirtualEndpointObservation _virtualEndpoint;
        private HostReleaseTimingEvidence? _releaseTimingEvidence;
        private CommandEnvelope? _releaseTimingBinding;
        internal HostSession()
        {
            var c=Fixture.C;
            _tuple=c.Tuple?new(c.Command,c.Persona):null;
            _controller=c.Controller?new():null;_context=c.Context?new():null;
            _targetCreationLifetime=c.Lifetime?new():null;_msiCostDiagnostics=c.CostDiagnostic?new():null;
            _msiNonSdReleaseDiagnostic=c.Diagnostic?new():null;
            _virtualEndpoint=Fixture.Observation(false);
            if(c.Msi)
            {
                _msiSteamDeckReleaseFence1726=new(new("PREPARE_TARGET","steamdeck"));
                if(!_msiSteamDeckReleaseFence1726.TryPin(Fixture.Observation(true),Fixture.Observation(false),out _))throw new Exception("fixture pin setup failed");
                if(c.Cache!="none")_msiSteamDeckReleaseFence1726.CompleteRelease(Fixture.Observation(false),Fixture.Observation(c.Cache=="success"),new(),out _);
            }
        }
        private static void StopSteamDeckIdleForRelease(FakeController controller)
        {Fixture.Actions.Add("idle.stop");if(Fixture.C.IdleThrow)throw new InvalidOperationException("idle-stop");}
        private void BeginMsiNonSdReleaseDiagnostic()=>Fixture.Actions.Add("non-sd.begin:"+(_msiNonSdReleaseDiagnostic is not null));
        private void ObserveMsiNonSdReleaseDiagnostic(VirtualEndpointObservation o,long begin)=>Fixture.Actions.Add("non-sd.observe:"+o.Detail+":begin="+begin+":end="+Fixture.Now);
        private void CompleteMsiNonSdReleaseDiagnostic(bool ok)=>Fixture.Actions.Add("non-sd.complete:"+ok);
        private void LogUsbipTransportLifecycleSafely(string phase)=>Fixture.Actions.Add("transport:"+phase);
        private void CloseReportChain1410Safely(string reason)
        {Fixture.Actions.Add("report-chain.close:"+reason);if(Fixture.C.ReportCloseThrow)throw new FormatException("fixture-report-close");}
        private bool TrySubmitNeutral(out string detail)
        {
            Fixture.Actions.Add("neutral");detail=Fixture.C.Neutral=="false-blank"?"  ":"fixture-neutral";
            if(Fixture.C.Neutral=="throw")throw new IOException("fixture-neutral");return Fixture.C.Neutral is not ("false" or "false-blank");
        }
        @@RELEASE_METHODS@@
        private string Ack(CommandEnvelope e,string status,string? detail=null,string? releaseDisposition=null)=>JsonSerializer.Serialize(new {ok=true,status,detail,releaseDisposition});
        private string Error(CommandEnvelope e,string status,string detail,string? releaseDisposition=null)=>JsonSerializer.Serialize(new {ok=false,status,detail,releaseDisposition});
        private void RememberIdempotent(CommandEnvelope e,string response)=>Fixture.Actions.Add("remember:"+e.Command+":"+response);
        private string DispatchRelease(CommandEnvelope envelope,out bool shutdown)
        {
            shutdown=false;
            switch(envelope.Command)
            {
                @@RELEASE_CASES@@
                default:throw new InvalidOperationException("fixture supports release commands only");
            }
        }
        internal object Run()
        {
            var steps=new List<object>();
            void Invoke(string command,bool dispatch)
            {
                string detail="<unassigned>";bool? ok=null;string? response=null,exception=null;bool shutdown=false;
                var e=new CommandEnvelope(command,Fixture.C.Persona);
                try
                {
                    if(dispatch)response=DispatchRelease(e,out shutdown);
                    else { @@INVOKE_RELEASE@@ }
                }
                catch(Exception ex){exception=ex.GetType().Name+":"+ex.Message;}
                steps.Add(new {command,ok,detail,response,exception,shutdown,phase=_phase.ToString(),pending=_steamDeckReleasePending,
                    controller=_controller is not null,context=_context is not null,targetPnpAdmitted=_targetPnpAdmitted,
                    virtualEndpoint=_virtualEndpoint.Detail,fence=_msiSteamDeckReleaseFence1726?.Evidence,
                    hasReleaseReceipt=_msiSteamDeckReleaseFence1726?.HasReleaseReceipt,releaseSucceeded=_msiSteamDeckReleaseFence1726?.ReleaseSucceeded,
                    releaseDetail=_msiSteamDeckReleaseFence1726?.ReleaseDetail,clock=Fixture.Now,removes=Fixture.Removes});
            }
            if(Fixture.C.Sequence){Invoke("RELEASE_TARGET",true);Invoke("SHUTDOWN",true);}
            else Invoke(Fixture.C.Command,false);
            return new {name=Fixture.C.Name,business=new {steps,actions=Fixture.Actions.ToArray()},timing=Fixture.Timing.ToArray()};
        }
    }
    public static void Main(string[] args)
    {
        Fixture.Mode=args.Length>1?args[1]:"recording";
        var cases=JsonSerializer.Deserialize<Case[]>(File.ReadAllText(args[0]))!;
        var output=new List<object>();
        foreach(var c in cases)
        {
            Fixture.C=c;Fixture.Tick=10000;Fixture.Removes=0;Fixture.Actions=new();Fixture.Timing=new();
            var session=new HostSession();Fixture.Actions.Clear();output.Add(session.Run());
        }
        Console.WriteLine(JsonSerializer.Serialize(output));
    }
}
