using System.Diagnostics;
using System.Text.Json;
using YeManBatteryHost;

// Test ports wrap the original Fake and MemoryStore. No alternate policy/router exists here.
internal sealed class LifecycleRig : IDisposable
{
    internal readonly Route Route=Policy.Routes.First(route=>route.RouteId=="ayaneo-cec-bypass");
    internal readonly MemoryStore Store=new();
    internal readonly Fake Device;
    internal readonly Controller Controller;
    internal readonly HostDomainRouter Router;
    internal readonly ManualResetEventSlim ReadEntered=new(false),ReadRelease=new(false);
    internal bool BlockRead;
    internal int Opens,Disposes,ActiveLeases,PeakLeases,FactoryCalls;
    internal readonly Identity Identity;
    internal LifecycleRig()
    {
        Identity=new(Route.Identity.Manufacturers[0],Route.Identity.Field=="boardProduct"?Route.Identity.Values[0]:"board",Route.Identity.Field=="systemProduct"?Route.Identity.Values[0]:"system","AMD Ryzen HX 370","2.10",2,80);
        Device=new(Route);
        Controller=new(Identity,Store,_=>new Lease(this),()=> (true,true,90));
        Router=new(()=>{FactoryCalls++;return Controller;},_=>throw new IOException("SECONDARY_DISPLAY_NOT_IN_BATTERY_TEST"),()=>true);
    }
    internal Snapshot Call(string op,Snapshot? state=null,int? target=null)
    {
        var request=new Dictionary<string,object?> { ["op"]=op,["protocol"]=RuntimeContract.Protocol,["sourceId"]=RuntimeContract.SourceId };
        if(state is not null)request["fence"]=state.Fence;
        if(target is not null)request["target"]=target;
        var envelope=JsonSerializer.SerializeToElement(request);RuntimeContract.Validate(envelope);
        return (Snapshot)Router.Dispatch(envelope);
    }
    internal Snapshot Enable()
    {
        var before=Call("get");var after=Call("set",before,80);
        BatteryLifecycleCases.Require(after.Ok&&after.Managed&&Device.Raw==0x55,"ORIGINAL_SOFTWARE_POLICY_NOT_ACTIVE");return after;
    }
    public void Dispose(){BlockRead=false;ReadRelease.Set();Router.Shutdown();ReadEntered.Dispose();ReadRelease.Dispose();}
    sealed class Lease : IChargeProvider
    {
        readonly LifecycleRig owner;bool disposed;
        internal Lease(LifecycleRig owner){this.owner=owner;owner.Opens++;owner.ActiveLeases++;owner.PeakLeases=Math.Max(owner.PeakLeases,owner.ActiveLeases);}
        public bool CanReadLimit=>owner.Device.CanReadLimit;
        public Reading Read(){if(owner.BlockRead){owner.ReadEntered.Set();if(!owner.ReadRelease.Wait(10000))throw new IOException("OWNED_FIXTURE_READ_NEVER_RELEASED");}return owner.Device.Read();}
        public void Write(int value,Reading before)=>owner.Device.Write(value,before);
        public void Dispose(){if(disposed)return;disposed=true;owner.ActiveLeases--;owner.Disposes++;}
    }
}
internal static class BatteryLifecycleCases
{
    static readonly List<object> Results=[];
    internal static void Require(bool value,string assertion){if(!value)throw new InvalidOperationException(assertion);}
    static void Record(string id,string requirement,Func<object> run)
    {
        var clock=Stopwatch.StartNew();try{var measurements=run();Results.Add(new {id,requirement,status="PASS",passed=true,elapsedMs=clock.ElapsedMilliseconds,measurements});}
        catch(Exception error){Results.Add(new {id,requirement,status="FAIL",passed=false,elapsedMs=clock.ElapsedMilliseconds,error=error.Message});}
    }
    static object NormalExit()
    {
        using var rig=new LifecycleRig();rig.Enable();var writes=rig.Device.Writes;rig.Router.Shutdown();rig.Router.Shutdown();
        Require(rig.Device.Raw==0xaa&&rig.ActiveLeases==0&&rig.Opens==rig.Disposes&&!rig.Store.Data.ContainsKey(Controller.RecoveryFile),"EXIT_MUST_RESTORE_OWNED_BYTE_AND_CLOSE_PROVIDER_LEASES");
        return new {ownerRevoked=true,softwareOwnedByteRestored=true,originalByte=0xaa,currentByte=rig.Device.Raw,openProviderLeases=rig.ActiveLeases,providerOpens=rig.Opens,providerDisposes=rig.Disposes,returnWrites=rig.Device.Writes-writes,processesOrPipesMeasured=false};
    }
    static object StalledStop(string receiptRoot)
    {
        using var rig=new LifecycleRig();rig.Enable();rig.BlockRead=true;
        var read=Task.Run(()=>rig.Call("get"));Require(rig.ReadEntered.Wait(3000),"REAL_PRODUCT_READ_MUST_BE_PENDING");
        using var shutdownEntered=new ManualResetEventSlim(false);
        var stop=Task.Run(()=>{shutdownEntered.Set();rig.Router.Shutdown();});Require(shutdownEntered.Wait(3000),"REAL_SHUTDOWN_TASK_NOT_SCHEDULED");
        var clock=Stopwatch.StartNew();const int deadlineMs=500;bool completed=stop.Wait(deadlineMs);
        var atDeadline=new {requirement="destroyWhenStopStalls",deadlineMs,elapsedMs=clock.ElapsedMilliseconds,stopCompletedBeforeFixtureRelease=completed,fixtureReleaseSent=false,activeProviderLeases=rig.ActiveLeases,readTaskCompleted=read.IsCompleted,shutdownTaskCompleted=stop.IsCompleted,pendingOwnedOperations=(read.IsCompleted?0:1)+(stop.IsCompleted?0:1),currentSoftwareByte=rig.Device.Raw,scope="actual HostDomainRouter lock and actual Controller with original Fake blocked only at its read port; no production Host process or hardware"};
        File.WriteAllText(Path.Combine(receiptRoot,"stalled-at-deadline.json"),JsonSerializer.Serialize(atDeadline,new JsonSerializerOptions{WriteIndented=true}));
        // Release is fixture cleanup after measurement, never counted as a product fallback.
        rig.BlockRead=false;rig.ReadRelease.Set();Require(Task.WaitAll([read,stop],5000),"OWNED_FIXTURE_CLEANUP_DID_NOT_RETURN");
        File.WriteAllText(Path.Combine(receiptRoot,"stalled-after-fixture-release.json"),JsonSerializer.Serialize(new {readCompleted=read.IsCompletedSuccessfully,stopCompleted=stop.IsCompletedSuccessfully,rig.ActiveLeases,providerLeasesRestored=rig.Opens==rig.Disposes,rig.Device.Raw,fixtureReleaseRequired=!completed},new JsonSerializerOptions{WriteIndented=true}));
        Require(completed&&rig.ActiveLeases==0,"STOP_MUST_FINISH_BEFORE_FIXTURE_RELEASE");return atDeadline;
    }
    static object LateAfterExit()
    {
        using var rig=new LifecycleRig();rig.Enable();rig.Router.Shutdown();var opens=rig.Opens;var writes=rig.Device.Writes;int rejected=0;
        for(int index=0;index<12;index++){rig.Router.Resume();rig.Router.Tick();try{rig.Call("get");}catch(IOException){rejected++;}}
        Require(rejected==12&&rig.Opens==opens&&rig.Device.Writes==writes&&rig.ActiveLeases==0&&rig.Device.Raw==0xaa,"LATE_EVENTS_CANNOT_REOPEN_STOPPED_OWNER");
        return new {lateCallbacksDelivered=36,dispatchesRejected=rejected,newProviderOpens=rig.Opens-opens,newWrites=rig.Device.Writes-writes,openProviderLeases=rig.ActiveLeases,currentSoftwareByte=rig.Device.Raw};
    }
    static object ExplicitOff()
    {
        using var rig=new LifecycleRig();rig.Enable();var released=rig.Call("release");Require(released.Ok&&!released.Managed&&released.RequestedLimit==100,"EXPLICIT_OFF_MUST_ACK_UNMANAGED");var writes=rig.Device.Writes;
        for(int index=0;index<12;index++){rig.Router.Suspend();rig.Router.Resume();rig.Router.Tick();}
        var after=rig.Call("get");Require(after.Ok&&!after.Managed&&after.RequestedLimit==100&&rig.Device.Raw==0xaa&&rig.Device.Writes==writes&&rig.ActiveLeases==0,"EXPLICIT_OFF_CANNOT_REPLAY_SAVED_TARGET");
        return new {explicitOffHonored=true,recoveryEdgesDelivered=36,additionalWrites=rig.Device.Writes-writes,managed=after.Managed,currentSoftwareByte=rig.Device.Raw,openProviderLeases=rig.ActiveLeases};
    }
    static object SleepHealth()
    {
        using var rig=new LifecycleRig();var before=rig.Enable();rig.Router.Suspend();Require(rig.Device.Raw==0xaa,"SUSPEND_MUST_RETURN_OWNED_SOFTWARE_BYTE");var paused=rig.Call("get");Require(!paused.Ok,"SUSPENDED_CONTROLLER_CANNOT_REPORT_HEALTHY");
        rig.Router.Resume();var current=rig.Call("get");Require(current.Ok&&current.Supported&&current.Managed&&current.RequestedLimit==80&&rig.Device.Raw==0x55&&current.Fence!=before.Fence,"RESUME_MUST_REACH_REAL_CURRENT_POLICY_HEALTH");
        var writes=rig.Device.Writes;var stale=rig.Call("set",before,60);Require(!stale.Ok&&rig.Device.Writes==writes,"OLD_FENCE_CANNOT_AUTHORIZE_RECOVERED_SESSION_WRITE");
        return new {logicalSleepAndResumeExecuted=true,healthObserved=current.Ok&&current.Supported,managed=current.Managed,target=current.RequestedLimit,restoredSoftwareByte=rig.Device.Raw,currentFenceChanged=current.Fence!=before.Fence,staleFenceRejected=!stale.Ok,physicalSleepPerformed=false,hostPowerEventGenerationMeasured=false};
    }
    static object TransientRead()
    {
        using var rig=new LifecycleRig();rig.Enable();var writes=rig.Device.Writes;rig.Device.FailRead=true;var failed=rig.Call("get");Require(!failed.Ok,"TRANSIENT_READ_MUST_BE_VISIBLE_FAILURE");rig.Device.FailRead=false;rig.Router.Tick();var recovered=rig.Call("get");
        Require(recovered.Ok&&recovered.Supported&&recovered.Managed&&rig.Device.Raw==0x55&&rig.Device.Writes==writes&&rig.ActiveLeases==0,"TRANSIENT_READ_MUST_RECOVER_WITHOUT_MUTATION_REPLAY");
        return new {failureActuallyObserved=!failed.Ok,recovered=recovered.Ok&&recovered.Supported,actualPolicyStillActive=recovered.Managed,currentSoftwareByte=rig.Device.Raw,unconfirmedWritesReplayed=rig.Device.Writes-writes,logicalRecoveryTicks=1,openProviderLeases=rig.ActiveLeases,uiReadRetryBudgetMeasuredHere=false};
    }
    static object ConflictLimits()
    {
        using var rig=new LifecycleRig();rig.Enable();rig.Device.Raw=42;rig.Router.Tick();var writes=rig.Device.Writes;
        for(int index=0;index<12;index++){rig.Router.Tick();rig.Router.Resume();}
        var rejected=rig.Call("get");Require(!rejected.Supported&&rig.Device.Raw==42&&rig.Device.Writes==writes&&rig.FactoryCalls==1&&rig.ActiveLeases==0,"CONFLICT_LIMIT_MUST_NOT_OVERWRITE_FOREIGN_STATE_OR_RECREATE_OWNER");
        return new {limitOrConflictReached=true,externalBytePreserved=rig.Device.Raw,additionalWrites=rig.Device.Writes-writes,additionalOwnerStarts=rig.FactoryCalls-1,recoveryEdgesDelivered=24,supported=rejected.Supported,openProviderLeases=rig.ActiveLeases,originalUiThreeRetryBudgetFromSeparateOriginalCases=true};
    }
    static int Main(string[] args)
    {
        if(args.Length is not (1 or 3)||!Path.IsPathFullyQualified(args[0])||(args.Length==3&&(args[1]!="--case"||args[2]!="late-after-exit")))return 250;
        Directory.CreateDirectory(args[0]);var only=args.Length==3?args[2]:null;
        if(only is null){Record("normal-exit","destroyOnExit",NormalExit);Record("stalled-stop","destroyWhenStopStalls",()=>StalledStop(args[0]));}
        Record("late-after-exit","preventResurrectionAfterExit",LateAfterExit);
        if(only is null){Record("explicit-off","explicitOffStaysOff",ExplicitOff);Record("sleep-health","recoverAfterSleep",SleepHealth);Record("transient-read-health","recoverAfterTransientFailure",TransientRead);Record("conflict-limits","respectRecoveryLimits",ConflictLimits);}
        bool passed=Results.All(row=>JsonSerializer.SerializeToElement(row).GetProperty("passed").GetBoolean());
        File.WriteAllText(Path.Combine(args[0],"managed-lifecycle.json"),JsonSerializer.Serialize(new {schemaVersion=1,status=passed?"PASS":"FAIL",cases=Results,producer="actual HostDomainRouter/Controller with original MemoryStore/Fake and measured provider leases",componentOnly=true,parentFeatureAcceptance="INCOMPLETE",productionHostMain=false,productionServe=false,realNamedPipe=false,physicalDevices=false},new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine(JsonSerializer.Serialize(new {passed,cases=Results.Count,parentFeatureAcceptance="INCOMPLETE"}));return passed?0:only is null?2:23;
    }
}
