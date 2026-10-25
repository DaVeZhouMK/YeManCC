// Exact lifecycle/dispatch extractions are filled by the new runner. SDK, PnP,
// reader guard, clocks and unrelated diagnostics are inert stand-ins only.
using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Serialization;
using Stopwatch = Program.FakeStopwatch;
#pragma warning disable CS0414, CS0649
internal static partial class Program
{
    @@PHASE@@
    @@OBSERVATION@@
    @@SAME_TUPLE@@
    @@IDEMPOTENT@@
    private sealed record CommandEnvelope(string Command,string RequestId,ulong Sequence,string RunId,ulong Epoch,
        ulong PowerGeneration,string TargetId,string OwnerIdentity,string Persona,string ProfileIdentity,string StartupNonce,
        ulong ConfigRevision,string ConfigHash)
    { public bool MsiSteamDeckReleaseFence {get;init;} }
    private const int VirtualEndpointRemovalTimeoutMs=3500,VirtualEndpointPollIntervalMs=50,SwitchReconcileFamilyWaitMs=2500,MaxMessageBytes=16384;
    private const string LockedCoreSha256="fixture-core",LockedCoreMvid="fixture-mvid";
    private static bool _screenPadOwnsReport;
    private static uint _screenPadPacketCounter;
    private static readonly List<object> Results=new();
    private static int _failures;
    private static class Fixture
    {
        internal static readonly List<string> Actions=new(),Timing=new();
        internal static bool StopOk=true,IdleThrows,NeutralFails,NeutralThrows,FrameFails,PrepareFails,ArrivalFails;
        internal static readonly Queue<bool> StopResults=new();
        internal static string StopDetail="sdk-output-stop-timeout";
        internal static int GuardCalls,Frames,Creates,Removes,TimingCreates;
        internal static bool? DiagnosticComplete;
        internal static long Now;
        internal static void Sleep(int ms){Now+=ms;}
        internal static void Reset(){Actions.Clear();Timing.Clear();StopOk=true;IdleThrows=NeutralFails=NeutralThrows=FrameFails=PrepareFails=ArrivalFails=false;StopResults.Clear();StopDetail="sdk-output-stop-timeout";GuardCalls=Frames=Creates=Removes=TimingCreates=0;Now=0;DiagnosticComplete=null;}
    }
    internal sealed class FakeStopwatch
    {
        private readonly long _start=Fixture.Now;
        internal static FakeStopwatch StartNew()=>new();
        internal static long GetTimestamp()=>Fixture.Now;
        internal static TimeSpan GetElapsedTime(long start)=>TimeSpan.FromMilliseconds(Fixture.Now-start);
        internal long ElapsedMilliseconds=>Fixture.Now-_start;
    }
    private sealed class HMController : IDisposable
    {public void Dispose()=>Fixture.Actions.Add("controller.dispose");}
    private sealed class HMProfile
    {internal ushort VendorId=0x054C,ProductId=0x0DF2;internal bool RequiresUsbipBackend;}
    private sealed class HMContext : IDisposable
    {
        internal HMContext(){Fixture.Actions.Add("context.create");}
        internal void LoadDefaultProfiles(){Fixture.Actions.Add("profiles.load");}
        internal HMProfile GetProfile(string name)=>new(){RequiresUsbipBackend=name=="steam-deck-composite"};
        internal HMController CreateController(HMProfile profile){Fixture.Actions.Add("controller.create");++Fixture.Creates;if(Fixture.PrepareFails)throw new IOException("fixture-create-failure");return new();}
        internal static void RemoveAllVirtualControllers(bool preserveInstall){Fixture.Actions.Add("remove:"+(++Fixture.Removes));}
        public void Dispose()=>Fixture.Actions.Add("context.dispose");
    }
    private static class HostControllerOutputStop
    {
        internal static bool TryStop(HMController controller,out string detail)
        {++Fixture.GuardCalls;var stopped=Fixture.StopResults.Count==0?Fixture.StopOk:Fixture.StopResults.Dequeue();Fixture.Actions.Add("output.stop:"+stopped);detail=stopped?"sdk-output-reader-stopped":Fixture.StopDetail;return stopped;}
    }
    private sealed class HostReleaseTimingEvidence
    {
        @@TIMING_ENUM@@
        internal static HostReleaseTimingEvidence TryCreate(){++Fixture.TimingCreates;Fixture.Timing.Add("create");return new();}
        internal void Begin(Stage stage)=>Fixture.Timing.Add("begin:"+stage);
        internal void End(Stage stage)=>Fixture.Timing.Add("end:"+stage);
        internal void Complete(bool ok)=>Fixture.Timing.Add("complete:"+ok);
        internal void Finish()=>Fixture.Timing.Add("finish");
    }
    private sealed class Lifetime
    {internal void ReleaseAttempt()=>Fixture.Actions.Add("lifetime.release");internal int BeginAttempt()=>1;internal void Created(int ticket,object? c){}internal void Failure(int ticket){} }
    private sealed class Cost {internal void InvalidateController(){} }
    private sealed class StageEvidence {internal void Decode(params object[] values){} }
    private static class MsiPnpCensusDiagnostic
    {internal sealed class Receipt{}internal sealed class Scope:IDisposable{internal Scope(bool enabled){}public void Dispose(){}}}
    private sealed class MsiNonSdReleaseDiagnostic
    {
        internal MsiNonSdReleaseDiagnostic(string persona){}
        internal static bool ShouldObserve(bool requested,string persona)=>false;
        internal void TrySetBeforeCreateCensus(object? census){}
    }
    private sealed class XboxSdkDiagnostics:IDisposable
    {internal static XboxSdkDiagnostics TryBegin(params object[] values)=>new();internal void SetEndpoint(string s){}public void Dispose(){} }
    // This boundary remains disabled in these common-path scenarios. It is not
    // a substitute for or extension of the separate MSI-SD policy tests.
    private sealed class MsiSteamDeckReleaseFence1726
    {
        internal MsiSteamDeckReleaseFence1726(CommandEnvelope e){}
        internal bool HasReleaseReceipt=>false;
        internal bool ReleaseSucceeded=>true;
        internal string ReleaseDetail=>"fixture-fence";
        internal static VirtualEndpointObservation Read(Func<VirtualEndpointObservation> read)=>read();
        internal void ConfirmUnresolvedRelease(VirtualEndpointObservation o){}
        internal VirtualEndpointObservation Wait(Func<VirtualEndpointObservation> read,Func<long> tick,Action<int> sleep,int timeout,int interval)=>read();
        internal bool IsOwnedAbsent(VirtualEndpointObservation o)=>o.IsFullyAbsent;
        internal bool CompleteRelease(VirtualEndpointObservation a,VirtualEndpointObservation b,List<string> errors,out string detail){detail=string.Join(';',errors);return errors.Count==0;}
        internal bool TryPin(VirtualEndpointObservation a,VirtualEndpointObservation b,out string detail){detail="";return true;}
    }
    private static VirtualEndpointObservation Absent()=>new(true,0,0,0,0,0,true,"fixture-absent");
    private static VirtualEndpointObservation ObserveVirtualEndpoint(string? persona=null,bool requireInstanceIds=false){Fixture.Actions.Add("observe");return Absent();}
    private static VirtualEndpointObservation WaitForEndpointFamilyRemoval(int timeoutMs,bool requireInstanceIds=false)=>Absent();
    private static VirtualEndpointObservation WaitForVirtualEndpointArrival(string persona,bool requireInstanceIds=false)=>Fixture.ArrivalFails?Absent():persona=="steamdeck"?new(true,0,0,0,1,1,true,"fixture-started"):new(true,1,persona=="xbox360"?1:0,1,0,0,true,"fixture-started");
    private static HMProfile? ApplySonyAxisDeclarationSwap(HMProfile? p)=>p;
    private static bool TryEnsureDriverInstalled(HMContext context,out string detail,bool requireUsbip){detail="fixture-backend";return true;}
    private static T CreateTargetWithBackendWarning<T>(Func<(bool Ready,string Detail)> prepare,Func<T> create,out string receipt){receipt=prepare().Detail;return create();}
    private static string DescribeEndpointCounts(VirtualEndpointObservation o)=>o.Detail;
    private static string DescribePrepareException(Exception e)=>";fixture-exception";
    private static void NoteCommand(string command){}
    private static bool TryParseEnvelope(string message,out CommandEnvelope? envelope,out string error){try{envelope=JsonSerializer.Deserialize<CommandEnvelope>(message);error="";return envelope is not null;}catch{envelope=null;error="fixture-parse";return false;}}
    @@WAIT@@
    private sealed partial class HostSession
    {
        private readonly object _lifecycleGate=new();
        private readonly string _startupNonce="fixture-nonce",_hostInstanceId="fixture-host";
        private readonly Dictionary<string,IdempotentReply> _idempotentReplies=new(StringComparer.Ordinal);
        private readonly Lifetime? _targetCreationLifetime=new();
        private readonly Cost? _msiCostDiagnostics=null;
        private readonly StageEvidence? _msiHostStageEvidence=null;
        private readonly bool _costFlagEnabled=false,_msiNonSdReleaseDiagnosticRequested=false;
        private long _costJsonParseUsSum,_costJsonParseCount,_lastActivityAt;
        private ulong _lastSequence;
        private bool _assetVerified=true,_targetPnpAdmitted=true,_virtualEndpointReconciled;
        private Phase _phase;
        private bool _steamDeckReleasePending,_outputReleasePending,_idleCtsReleasePending;
        private HostControllerIdleCtsRelease.Captured? _idleCtsRelease;
        private HMController? _controller;
        private HMContext? _context;
        private CommandEnvelope? _tuple;
        private VirtualEndpointObservation _virtualEndpoint=Absent();
        private HostReleaseTimingEvidence? _releaseTimingEvidence;
        private CommandEnvelope? _releaseTimingBinding;
        private MsiNonSdReleaseDiagnostic? _msiNonSdReleaseDiagnostic=new("dualsense-edge");
        private CommandEnvelope? _msiNonSdReleaseDiagnosticBinding;
        private MsiSteamDeckReleaseFence1726? _msiSteamDeckReleaseFence1726;
        @@PENDING_PROPERTY@@
        internal HostSession(string persona="dualsense-edge",Phase phase=Phase.Active,bool objects=true)
        {
            _phase=phase;_tuple=Envelope("HELLO","seed",0,persona);if(objects){_controller=new();_context=new();}
            Fixture.Actions.Clear();
        }
        internal CommandEnvelope Request(string command,string requestId)=>_tuple! with{Command=command,RequestId=requestId,Sequence=_lastSequence+1};
        internal string Send(CommandEnvelope request,out bool shutdown)=>HandleLocked(JsonSerializer.Serialize(request),out shutdown);
        internal string PhaseName=>_phase.ToString();
        internal bool Pending=>LocalReleasePending;
        internal object? Controller=>_controller;
        internal object? Context=>_context;
        internal void SeedOutputPending(bool value)=>_outputReleasePending=value;
        internal void SeedIdlePending(bool value)=>_steamDeckReleasePending=value;
        internal bool PrepareDirect(CommandEnvelope e,out string detail)=>TryPrepare(e,out detail);
        private static void RecordCostRole(string s){}
        private void ResetBackendSubmitStats(){}
        private void ArmReportChain1410Safely(CommandEnvelope e){}
        private string ComposeBackendSubmitStatsDetail()=>"fixture-stats";
        private void CaptureMsiSdkDeclaredIdentityDiagnostic(){}
        private void BeginMsiNonSdReleaseDiagnostic()=>Fixture.Actions.Add("diagnostic.begin");
        private void ObserveMsiNonSdReleaseDiagnostic(VirtualEndpointObservation o,long tick){}
        private void CompleteMsiNonSdReleaseDiagnostic(bool ok){Fixture.DiagnosticComplete=ok;Fixture.Actions.Add("diagnostic.complete:"+ok);}
        private void LogUsbipTransportLifecycleSafely(string reason){}
        private void CloseReportChain1410Safely(string reason)=>Fixture.Actions.Add("report.close");
        private static void StopSteamDeckIdleForRelease(HMController c){Fixture.Actions.Add("idle.stop");if(Fixture.IdleThrows)throw new IOException("fixture-idle");}
        private bool TrySubmitNeutral(out string detail,CommandEnvelope? e=null,string reason="release-local")
        {Fixture.Actions.Add("neutral");detail="fixture-neutral-failed";if(Fixture.NeutralThrows)throw new IOException(detail);return !Fixture.NeutralFails;}
        private bool TrySubmitFrame(CommandEnvelope e,out string detail){++Fixture.Frames;Fixture.Actions.Add("frame");detail="fixture-frame-failed";return !Fixture.FrameFails;}
        private string Ack(CommandEnvelope e,string status,string? detail=null,bool? firstFrame=null,string? releaseDisposition=null)=>JsonSerializer.Serialize(new{ok=true,status,detail,releaseDisposition,firstFrame});
        private string Error(CommandEnvelope? e,string status,string detail,string? releaseDisposition=null)=>JsonSerializer.Serialize(new{ok=false,status,detail,releaseDisposition});
        @@LIFECYCLE_METHODS@@
        private string HandleLocked(string message,out bool shutdown)
        {
            @@DISPATCH_PROLOGUE@@
            @@DISPATCH_VALIDATION@@
            switch(envelope.Command)
            {
                @@DISPATCH_CASES@@
                default:return Error(envelope,"fixture-unsupported","fixture supports selected lifecycle cases only");
            }
        }
    }
    private static class ScreenPadSubmissionEvidence { internal static void Reset(){} }
    private static class HostControllerIdleCtsRelease
    {
        internal sealed class Captured
        {
            private bool _returned;
            internal void MarkControllerDisposeReturned(){_returned=true;Fixture.Actions.Add("sdk.dispose-returned");}
            internal bool TryRelease(out string detail){detail="idle-cts-released";Fixture.Actions.Add("idle.cts-dispose");return _returned;}
        }
        internal static bool TryCapture(HMController controller,out Captured? captured,out string detail)
        {captured=new();detail="idle-cts-captured";Fixture.Actions.Add("idle.cts-capture");return true;}
    }
    private static CommandEnvelope Envelope(string cmd,string id,ulong sequence,string persona)=>new(cmd,id,sequence,"fixture-run",7,1,"fixture-target","fixture-owner",persona,
        persona switch{"steamdeck"=>"steam-deck-composite","elite"=>"xbox-elite-v2","dualshock4"=>"dualshock-4-v2","xbox360"=>"xbox-360-wired",_=>persona},"fixture-nonce",1,"fixture-config");
    private static void Check(bool ok,string why="assertion failed"){if(!ok)throw new InvalidOperationException(why);}
    private static JsonElement View(string response){using var d=JsonDocument.Parse(response);return d.RootElement.Clone();}
    private static string Status(string response)=>View(response).GetProperty("status").GetString()!;
    private static void Retained(HostSession s,object? controller,object? context)
    {
        Check(s.PhaseName=="Quiesced"&&s.Pending,"pending phase lost");Check(ReferenceEquals(controller,s.Controller)&&ReferenceEquals(context,s.Context),"objects replaced or cleared");
        Check(!Fixture.Actions.Contains("controller.dispose")&&!Fixture.Actions.Contains("context.dispose")&&Fixture.Removes==0&&Fixture.Creates==0,"cleanup/create escaped pending guard");
    }
    private static void Ordered(params string[] actions)
    {var pos=-1;foreach(var action in actions){var found=Fixture.Actions.FindIndex(pos+1,x=>x==action);Check(found>pos,"missing/out-of-order "+action);pos=found;}}
    private static void TimingBalanced()
    {
        var active=new HashSet<string>();foreach(var item in Fixture.Timing){if(item.StartsWith("begin:")){Check(active.Add(item[6..]),"duplicate timing begin");}else if(item.StartsWith("end:")){Check(active.Remove(item[4..]),"unmatched timing end");}}
        Check(active.Count==0,"unfinished timing stage");
    }
    private static void Test(string name,Action body)
    {
        Fixture.Reset();try{body();Results.Add(new{name,status="pass",actions=Fixture.Actions.ToArray(),timing=Fixture.Timing.ToArray()});}
        catch(Exception ex){++_failures;Results.Add(new{name,status="fail",error=ex.Message,actions=Fixture.Actions.ToArray(),timing=Fixture.Timing.ToArray()});}
    }
    public static int Main()
    {
        Test("release-stop-false-retains-objects-no-dispose-sweep-or-create",()=>{
            var s=new HostSession();var c=s.Controller;var x=s.Context;Fixture.StopOk=false;var response=s.Send(s.Request("RELEASE_TARGET","r1"),out var shutdown);
            Check(!View(response).GetProperty("ok").GetBoolean()&&!shutdown&&Status(response)=="release-local-dispose-failed");Retained(s,c,x);Check(Fixture.DiagnosticComplete==false&&Fixture.GuardCalls==1);Ordered("neutral","output.stop:False","diagnostic.complete:False");TimingBalanced();
        });
        Test("shutdown-stop-false-does-not-order-exit",()=>{
            var s=new HostSession();var c=s.Controller;var x=s.Context;Fixture.StopOk=false;var response=s.Send(s.Request("SHUTDOWN","s1"),out var shutdown);
            Check(!shutdown&&Status(response)=="shutdown-local-dispose-failed");Retained(s,c,x);TimingBalanced();
        });
        Test("fresh-release-request-retries-same-objects-then-ordered-cleanup",()=>{
            var s=new HostSession();var c=s.Controller;var x=s.Context;Fixture.StopOk=false;s.Send(s.Request("RELEASE_TARGET","r1"),out _);Retained(s,c,x);
            Fixture.Actions.Clear();Fixture.StopOk=true;var response=s.Send(s.Request("RELEASE_TARGET","r2"),out var shutdown);
            Check(View(response).GetProperty("ok").GetBoolean()&&!shutdown&&!s.Pending&&s.PhaseName=="Released"&&s.Controller is null&&s.Context is null);
            Ordered("neutral","output.stop:True","controller.dispose","report.close","remove:1","observe","context.dispose");TimingBalanced();
        });
        Test("old-request-replay-does-not-repeat-guard-or-cleanup",()=>{
            var s=new HostSession();Fixture.StopOk=false;var request=s.Request("RELEASE_TARGET","replay");var first=s.Send(request,out _);var actions=Fixture.Actions.ToArray();var timing=Fixture.Timing.ToArray();
            Fixture.StopOk=true;Check(s.Send(request,out var shutdown)==first&&!shutdown);Check(Fixture.Actions.SequenceEqual(actions)&&Fixture.Timing.SequenceEqual(timing)&&Fixture.GuardCalls==1&&s.Pending); Check(Status(s.Send(request with{Sequence=request.Sequence+1},out _))=="protocol-error"&&Fixture.GuardCalls==1,"request-id content mismatch repeated guard");
            var fresh=s.Send(s.Request("RELEASE_TARGET","fresh"),out _);Check(View(fresh).GetProperty("ok").GetBoolean());var count=Fixture.Actions.Count;
            Check(s.Send(request,out _)==first&&Fixture.Actions.Count==count&&s.PhaseName=="Released"&&!s.Pending,"cached failure mutated successful retry");
        });
        Test("pending-SUBMIT_FRAME-rejected-without-submit-or-guard",()=>{
            var s=new HostSession();Fixture.StopOk=false;s.Send(s.Request("RELEASE_TARGET","r"),out _);var calls=Fixture.GuardCalls;
            var response=s.Send(s.Request("SUBMIT_FRAME","f"),out _);Check(Status(response)=="state-rejected"&&Fixture.Frames==0&&Fixture.GuardCalls==calls&&s.PhaseName=="Quiesced");
        });
        Test("pending-SUBMIT_NEUTRAL-cannot-reactivate-writer",()=>{
            var s=new HostSession();Fixture.StopOk=false;s.Send(s.Request("RELEASE_TARGET","r"),out _);var calls=Fixture.GuardCalls;
            Check(Status(s.Send(s.Request("SUBMIT_NEUTRAL","n"),out _))=="neutral-accepted"&&s.PhaseName=="Quiesced"&&s.Pending&&Fixture.GuardCalls==calls);
            Check(Status(s.Send(s.Request("SUBMIT_FRAME","f"),out _))=="state-rejected"&&Fixture.Frames==0);
        });
        Test("recreate-stop-false-does-not-create-or-discard-target",()=>{
            var s=new HostSession();var c=s.Controller;var x=s.Context;Fixture.StopOk=false;var request=s.Request("RECREATE_TARGET","rc");var response=s.Send(request,out _);
            Check(Status(response)=="recreate-release-failed");Retained(s,c,x);var calls=Fixture.GuardCalls;Check(s.Send(request,out _)==response&&Fixture.GuardCalls==calls);Check(Fixture.TimingCreates==0,"internal recreate allocated a release collector");
        });
        Test("fresh-recreate-after-failure-clears-pending-and-creates-once",()=>{
            var s=new HostSession();Fixture.StopOk=false;s.Send(s.Request("RECREATE_TARGET","rc1"),out _);Fixture.StopOk=true;
            Check(Status(s.Send(s.Request("RECREATE_TARGET","rc2"),out _))=="target-prepared"&&s.PhaseName=="Prepared"&&!s.Pending&&Fixture.Creates==1);Ordered("output.stop:True","controller.dispose","context.dispose","controller.create");
        });
        Test("successful-create-resets-stale-output-pending-actual-TryPrepare",()=>{
            var s=new HostSession(phase:Phase.Hello,objects:false);s.SeedOutputPending(true);Check(s.PrepareDirect(s.Request("PREPARE_TARGET","p"),out _)&&!s.Pending&&Fixture.Creates==1);
        });
        Test("frame-failure-release-pending-cannot-advance-to-Released",()=>{
            var s=new HostSession();var c=s.Controller;var x=s.Context;Fixture.FrameFails=true;Fixture.StopOk=false;
            var response=s.Send(s.Request("SUBMIT_FRAME","f1"),out _);Check(Status(response)=="frame-rejected"&&Fixture.Frames==1);Retained(s,c,x);
            Check(Fixture.TimingCreates==0,"frame-failure cleanup allocated collector");var calls=Fixture.GuardCalls;s.Send(s.Request("SUBMIT_FRAME","f2"),out _);Check(Fixture.GuardCalls==calls&&Fixture.Frames==1);
        });
        Test("neutral-false-and-stop-false-errors-both-preserved",()=>{
            var s=new HostSession();Fixture.NeutralFails=true;Fixture.StopOk=false;var response=s.Send(s.Request("RELEASE_TARGET","r"),out _);var detail=View(response).GetProperty("detail").GetString()!;
            Check(detail.Contains("neutral:fixture-neutral-failed")&&detail.Contains("controller-output-stop:sdk-output-stop-timeout")&&Fixture.DiagnosticComplete==false);Check(s.Pending&&s.Controller is not null);TimingBalanced();
        });
        Test("neutral-exception-and-stop-false-errors-both-preserved",()=>{
            var s=new HostSession();Fixture.NeutralThrows=true;Fixture.StopOk=false;var response=s.Send(s.Request("RELEASE_TARGET","r"),out _);var detail=View(response).GetProperty("detail").GetString()!;
            Check(detail.Contains("neutral:IOException")&&detail.Contains("controller-output-stop:")&&s.Pending);TimingBalanced();
        });
        Test("SD-idle-failure-precedes-output-guard-and-preserves-target",()=>{
            var s=new HostSession("steamdeck");var c=s.Controller;var x=s.Context;Fixture.IdleThrows=true;Fixture.StopOk=false;
            var response=s.Send(s.Request("SHUTDOWN","sd"),out var shutdown);Check(!shutdown&&Status(response)=="shutdown-local-dispose-failed"&&Fixture.GuardCalls==0);Retained(s,c,x);
            Check(!Fixture.Timing.Any(x=>x.Contains("OutputReaderStop"))&&!Fixture.Actions.Contains("neutral"));TimingBalanced();
        });
        Test("normal-release-and-shutdown-keep-original-success-order",()=>{
            foreach(var command in new[]{"RELEASE_TARGET","SHUTDOWN"}){Fixture.Reset();var s=new HostSession();var response=s.Send(s.Request(command,"ok"),out var shutdown);
            Check(View(response).GetProperty("ok").GetBoolean()&&shutdown==(command=="SHUTDOWN")&&!s.Pending&&s.PhaseName=="Released");Ordered("neutral","output.stop:True","controller.dispose","remove:1","context.dispose");TimingBalanced();}
        });
        Test("timing-output-stop-stage-balanced-before-Dispose-and-schema-has-13",()=>{
            Check(Enum.GetValues<HostReleaseTimingEvidence.Stage>().Length==13);var s=new HostSession();s.Send(s.Request("RELEASE_TARGET","t"),out _);
            Check(Fixture.Timing.Count(x=>x=="begin:OutputReaderStop")==1&&Fixture.Timing.Count(x=>x=="end:OutputReaderStop")==1);
            Check(Fixture.Timing.IndexOf("end:OutputReaderStop")<Fixture.Timing.IndexOf("begin:ControllerDispose"));Check(Fixture.Timing.Contains("complete:True")&&Fixture.Timing.Last()=="finish");TimingBalanced();
        });
        Test("failed-guard-timing-does-not-claim-disposal-stage",()=>{
            var s=new HostSession();Fixture.StopOk=false;s.Send(s.Request("RELEASE_TARGET","t"),out _);Check(Fixture.Timing.Contains("end:OutputReaderStop")&&Fixture.Timing.Contains("complete:False"));
            Check(!Fixture.Timing.Contains("begin:ControllerDispose")&&!Fixture.Timing.Contains("begin:ContextDispose")&&!Fixture.Timing.Contains("begin:RemoveAllAttempt1"));TimingBalanced();
        });
        Test("combined-pending-property-covers-each-reason-and-empty-target-clears-output",()=>{
            var s=new HostSession(objects:false);s.SeedOutputPending(true);Check(s.Pending);s.SeedOutputPending(false);s.SeedIdlePending(true);Check(s.Pending);s.SeedIdlePending(false);Check(!s.Pending);
            s.SeedOutputPending(true);Check(s.PrepareDirect(s.Request("PREPARE_TARGET","seed"),out _));Check(!s.Pending);
        });
        Test("same-guard-for-all-personas-without-generalizing-MSI-fence",()=>{
            foreach(var persona in new[]{"dualsense-edge","dualsense","dualshock4","elite","xbox360","steamdeck"}){Fixture.Reset();var s=new HostSession(persona);var c=s.Controller;var x=s.Context;Fixture.StopOk=false;s.Send(s.Request("RELEASE_TARGET","p"),out _);Retained(s,c,x);Check(Fixture.GuardCalls==1);}
        });
        Test("nonce-tuple-and-sequence-validation-before-guard",()=>{
            var s=new HostSession();var r=s.Request("RELEASE_TARGET","x");Check(Status(s.Send(r with{StartupNonce="foreign"},out _))=="protocol-error");
            Check(Status(s.Send(r with{RunId="foreign"},out _))=="protocol-error");Check(Status(s.Send(r with{Sequence=2},out _))=="protocol-error");Check(Fixture.GuardCalls==0);
        });
        Test("same-session-shutdown-failure-replay-then-fresh-success-orders-exit",()=>{
            var s=new HostSession();var c=s.Controller;var x=s.Context;Fixture.StopOk=false;var first=s.Request("SHUTDOWN","sd1");
            Check(Status(s.Send(first,out var shutdown))=="shutdown-local-dispose-failed"&&!shutdown);Retained(s,c,x);var actions=Fixture.Actions.Count;
            Check(Status(s.Send(first,out shutdown))=="protocol-error"&&!shutdown&&Fixture.Actions.Count==actions&&Fixture.GuardCalls==1,"stale shutdown repeated release");
            Fixture.StopOk=true;Fixture.Actions.Clear();Check(Status(s.Send(s.Request("SHUTDOWN","sd2"),out shutdown))=="shutdown"&&shutdown&&!s.Pending);
            Ordered("neutral","output.stop:True","controller.dispose","remove:1","context.dispose");TimingBalanced();
        });
        Test("recreate-new-target-PnP-failure-and-stop-false-retains-new-target",()=>{
            var s=new HostSession();var old=s.Controller;Fixture.StopResults.Enqueue(true);Fixture.StopResults.Enqueue(false);Fixture.ArrivalFails=true;
            var response=s.Send(s.Request("RECREATE_TARGET","new-pnp-failed"),out var shutdown);
            Check(Status(response)=="recreate-rejected"&&!shutdown&&s.Pending&&s.PhaseName=="Quiesced","prepare-failure phase lost output pending");
            Check(s.Controller is not null&&!ReferenceEquals(s.Controller,old)&&s.Context is not null&&Fixture.Creates==1,"new failed-admission objects discarded or recreated");
            Check(Fixture.GuardCalls==2&&Fixture.Actions.Count(x=>x=="controller.dispose")==1&&Fixture.Actions.Count(x=>x=="context.dispose")==1&&Fixture.Removes==1,"failed new-reader guard cleaned up new target");
            Check(Fixture.DiagnosticComplete==false);TimingBalanced();
        });
        Test("normal-submit-neutral-and-frame-do-not-enter-release-or-create-timing",()=>{
            var s=new HostSession(phase:Phase.Prepared);Check(Status(s.Send(s.Request("SUBMIT_NEUTRAL","n"),out _))=="neutral-accepted"&&s.PhaseName=="Neutralized");
            Check(Status(s.Send(s.Request("SUBMIT_FRAME","f"),out _))=="frame-accepted"&&s.PhaseName=="Active"&&Fixture.Frames==1);
            Check(Fixture.GuardCalls==0&&Fixture.Removes==0&&Fixture.TimingCreates==0&&Fixture.Creates==0);
        });
        Test("no-production-Host-SDK-or-PInvoke-compiled",()=>{
            var assembly=typeof(Program).Assembly;Check(assembly.GetReferencedAssemblies().All(a=>a.Name!="YeManInputHost"&&!(a.Name??"").Contains("HIDMaestro",StringComparison.OrdinalIgnoreCase)));
            Check(assembly.GetTypes().SelectMany(t=>t.GetMethods(System.Reflection.BindingFlags.Public|System.Reflection.BindingFlags.NonPublic|System.Reflection.BindingFlags.Static|System.Reflection.BindingFlags.Instance)).All(m=>(m.Attributes&System.Reflection.MethodAttributes.PinvokeImpl)==0));
        });
        Console.WriteLine(JsonSerializer.Serialize(new{version=1,passed=Results.Count-_failures,failed=_failures,realHostRun=false,realSDKRun=false,realThreadStarted=false,deviceOrPnPAPIInvoked=false,cases=Results},new JsonSerializerOptions{WriteIndented=true}));return _failures==0?0:1;
    }
}