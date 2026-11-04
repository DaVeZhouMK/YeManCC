using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using YeManFanBackend.Capabilities;
namespace YeManFanHost;

internal sealed partial class RealHcBackend
{
    private const bool SourceHostOuterLifecycleIntegrated = true;
    private bool sourceConsumerForSelfTest;
    private Func<IPinnedFanTransport>? sourceTerminalForSelfTest;
    private PinnedSourceFanSession? sourceFanSession;
    private long sourceFanAdmissionEpoch,sourceFanControlCycle;
    private bool sourceFanOpening;
    private static readonly FanRoute SourceFanRoute = new(FanRouteKind.GenericDuty,
        "Fixed HC source fan owner", false, false, FanRestoreStrategy.PinnedSourceAutomatic,
        FanEvidenceMode.SourceTransport, FanRoutePrecondition.SourceTransportOwned);
    private bool SourceControlConnected => pinnedFanSource is not null &&
        (SourceHostOuterLifecycleIntegrated || sourceConsumerForSelfTest);
    private FanRoute? PinnedSourceRoute=>SourceControlConnected?SourceFanRoute:null;
    internal bool SourceControlMode=>SourceControlConnected;
    internal bool SourceOwnerEntered=>sourceFanSession is not null;
    internal bool SourceIoPending=>sourceFanSession?.PendingIo==true;
    internal long? SourceGeneration=>sourceFanSession?.Generation;
    internal string? CurrentControlOpenOperationId=>SourceControlMode&&sourceFanSession is not null
        ? $"source-open:{backendHostInstanceId}:{sourceFanSession.Generation}:{sourceFanAdmissionEpoch}:{sourceFanControlCycle}"
        : LastCompatEntryOperationIdObserved;
    private bool TryObserveSourceReopenReceipt(FanHostEngine.ReopenBinding? binding,string host,string? cycle,
        long sessionGeneration,out string detail)
    {
        if(binding is null){detail="source-reopen:no-binding";return false;}
        if(sessionGeneration!=SourceGeneration||!SourceSessionReady(out detail))
        {detail="source-reopen:owner-not-current";return false;}
        var reason=FanHostEngine.ReopenBindingRejection(binding.HostInstanceId,host,binding.CycleId,cycle,
            binding.SessionGeneration,sessionGeneration,binding.BaselineOperationId,CurrentControlOpenOperationId);
        detail=reason??"source-reopen:exact-new-catalog-generation-current-session";
        return reason is null;
    }
    internal bool SourceTransportCloseConfirmed=>sourceFanSession is {Phase:PinnedFanPhase.Closed,PendingIo:false};
    internal bool SourceAutomaticReturnObserved=>sourceFanSession?.AutomaticReturnObserved==true;
    internal bool SourceWritesObserved=>sourceFanSession?.ControlWasAttempted==true;
    internal bool SourceCloseIsOwed=>sourceFanSession is not null&&!SourceTransportCloseConfirmed;
    internal bool SourceControlCloseComplete=>!IsOpen && (sourceFanSession is null ? !sourceFanOpening : SourceTransportCloseConfirmed);
    private bool SourcePermit(long epoch)
    {
        if(!SourceControlConnected||Volatile.Read(ref closeWriteBlocked)!=0||
            Volatile.Read(ref suspendGateEpoch)!=epoch||Volatile.Read(ref hcFactoryDisposed)!=0)
            return false;
        try{return CanWrite&&(sourceFanOpening||IsOpen);}catch{return false;}
    }
    private void OpenPinnedSourceCore()
    {
        EnsureRealWriteAuthorization();
        if(SourceCloseIsOwed)throw new FanApiException(409,"SOURCE_FAN_RETURN_PENDING","Previous source owner has not retired");
        var source=pinnedFanSource!;sourceFanAdmissionEpoch=Volatile.Read(ref suspendGateEpoch);
        sourceFanControlCycle=ControlCycleId;
        if(Volatile.Read(ref closeWriteBlocked)!=0)throw new FanApiException(409,"SOURCE_FAN_CLOSED","Source Host is closed");
        sourceFanOpening=true;openAttempted=true;closed=false;
        oemRestoreVerified=false;oemRestoreEvidence="none";oemReleaseActionKind="not-attempted";
        oemReleaseActionEvidence="none";oemOwnershipStatus="source-owner-opening";
        try
        {
            var context=Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(
                backendHostInstanceId+"|"+ControlCycleId+"|"+sourceFanAdmissionEpoch+"|"+options.SessionToken)));
            sourceFanSession=FanCapabilityCatalog.AcquirePinnedFanSource(source,context,
                ()=>SourcePermit(sourceFanAdmissionEpoch),()=>sourceConsumerForSelfTest
                    ? (sourceTerminalForSelfTest?.Invoke()??throw new InvalidOperationException("SOURCE_FIXTURE_TERMINAL_MISSING"))
                    : PinnedSourceFanTransports.Open(assembly!,source));
            sourceFanSession.Open();
            if(!SourcePermit(sourceFanAdmissionEpoch))throw new InvalidOperationException("SOURCE_FAN_OPEN_SUPERSEDED");
            IsOpen=true;oemBaselineCaptured=true;baselineRestoreRoute=source.ModelClass;
            baselineStrategy=FanRestoreStrategy.PinnedSourceAutomatic;
            sessionRouteMarker=SourceSessionMarker(sourceFanSession);
            oemOwnershipStatus="source-mode-observed-no-control-write";
        }
        catch
        {
            sourceFanSession?.Revoke();
            if(sourceFanSession?.TryStop()!=false){IsOpen=false;openAttempted=false;closed=true;sessionRouteMarker=null;}
            throw;
        }
        finally{sourceFanOpening=false;}
    }
    private string SourceSessionMarker(PinnedSourceFanSession owner)=>
        $"source:{owner.Descriptor.DeviceId}:{owner.Generation}:{sourceFanAdmissionEpoch}:{sourceFanControlCycle}";
    private bool SourceSessionReady(out string reason)
    {
        var owner=sourceFanSession;
        if(!IsOpen||owner is null||!ReferenceEquals(owner.Descriptor,pinnedFanSource)||
            owner.Phase is not (PinnedFanPhase.Ready or PinnedFanPhase.Active)||
            !oemBaselineCaptured||baselineRestoreRoute!=SourceModelClass||
            Volatile.Read(ref suspendGateEpoch)!=sourceFanAdmissionEpoch||sourceFanControlCycle!=ControlCycleId||
            baselineStrategy!=FanRestoreStrategy.PinnedSourceAutomatic||
            !string.Equals(sessionRouteMarker,SourceSessionMarker(owner),StringComparison.Ordinal))
        {reason="source-owner-or-generation-not-ready";return false;}
        reason="source-owner-ready";return true;
    }
    private bool ApplyPinnedSourceCurveCore(IReadOnlyList<FanNode> nodes,long admittedEpoch)
    {
        EnsureRealWriteSession();ValidateCurve(nodes);
        if(admittedEpoch!=sourceFanAdmissionEpoch||!SourcePermit(admittedEpoch))
            throw new FanApiException(409,"SOURCE_FAN_SUPERSEDED","Curve admission is stale");
        var duties=Enumerable.Range(0,11).Select(i=>Interpolate(nodes,i*10)).ToArray();
        var curve=activeFanProfile is PinnedSourceFanCurve previous
            ? previous.WithCurve(duties) : new PinnedSourceFanCurve(pinnedFanSource!,duties);
        activeCurveDuties=duties;activeFanProfile=curve;lastDispatchedTemperature=null;
        lastDispatchedFanProfile=null;lastTemperatureC=null;lastDutyPercent=null;
        lastControlWriteEvidence="source-curve-armed-awaiting-valid-temperature";
        // One existing shared-memory sensor owner. The test seam creates only
        // its dormant object and stamps the real freshness field before callback.
        if(sourceConsumerForSelfTest)cpuTemperatureMonitor??=new HWiNFOTemperatureMonitor();
        else EnsureCpuTemperatureLoop();
        return true;
    }
    private void WritePinnedSourceDuty(double duty)
    {
        var owner=sourceFanSession??throw new InvalidOperationException("SOURCE_FAN_OWNER_MISSING");
        var operation=CaptureFanDispatchOwner();
        var receipt=owner.ApplyPercent(duty,()=>SourcePermit(sourceFanAdmissionEpoch)&&
            IsFanDispatchOwnerCurrent(operation)&&!AreTemperatureWritesBlocked());
        lastControlWriteEvidence="source-transport-returned-physical-effect-not-observed";
        Diagnostic?.Invoke("source.fan-duty.returned",new { receipt.DeviceId,receipt.Generation,
            receipt.RegisterValue,receipt.SourceCommit,receipt.PhysicalEffectVerified });
    }
    private void RetirePinnedSourceCore()
    {
        var owner=sourceFanSession;owner?.Revoke();StopCpuTemperatureMonitor();
        activeCurveDuties=null;
        if(owner is not null&&!owner.TryStop())
        {
            oemReleaseActionKind=FanHostEngine.ReleaseActionKindName(OemReleaseAction.Unknown);
            oemReleaseActionEvidence="source-return-or-terminal-close-pending";
            throw new FanApiException(409,"SOURCE_FAN_RETURN_PENDING",owner.LastFailure??"Issued source operation pending");
        }
        var written=owner?.ControlWasAttempted==true;
        if(written&&owner?.AutomaticReturnObserved!=true)
            throw new FanApiException(409,"SOURCE_FAN_RETURN_UNPROVEN","Automatic mode was not observed");
        oemReleaseActionKind=written ? "proven-by-source-auto-register-and-terminal-close" :
            FanHostEngine.ReleaseActionKindName(OemReleaseAction.NotApplicable);
        oemReleaseActionEvidence=written?"source-auto-register-and-exact-terminal-close-observed":"source-no-control-write-terminal-closed";
        oemRestoreEvidence=oemReleaseActionEvidence;oemOwnershipStatus="source-resources-retired-physical-effect-not-observed";
        // Explicit source evidence must never masquerade as an HC callback,
        // virtual Close or ManagerFactory lifecycle receipt.
        oemRestoreVerified=false;hcOemReleaseCallbackCompleted=false;
        IsOpen=false;openAttempted=false;oemBaselineCaptured=false;closed=true;
        sessionRouteMarker=null;retiredControlCycleId=ControlCycleId;
    }
    internal void InstallPinnedSourceConsumerForSelfTest(string manufacturer,string product,Func<IPinnedFanTransport> factory)
    {
        if(device is not null||assembly is not null||IsOpen)throw new InvalidOperationException("Fixture requires dormant Host");
        if(!SelectPinnedSourceFanIdentity(manufacturer,product))throw new InvalidOperationException("Fixture source identity missing");
        sourceTerminalForSelfTest=factory;sourceConsumerForSelfTest=true;
        Volatile.Write(ref hcCoreInitialized,true);
    }
    internal Action<float?> CapturePinnedSourceTemperatureForSelfTest()
    {
        if(!sourceConsumerForSelfTest||cpuTemperatureMonitor is null)throw new InvalidOperationException("Source monitor missing");
        var captured=cpuTemperatureMonitor;
        return value=>
        {
            var field=typeof(HWiNFOTemperatureMonitor).GetField("sampleReadTick",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic)!;
            field.SetValue(captured,Environment.TickCount64);OnHcCpuTemperatureChanged(captured,value);
        };
    }
    internal void DeliverPinnedSourceTemperatureForSelfTest(float? value,bool foreignMonitor=false,long? sampledTick=null)
    {
        if(!sourceConsumerForSelfTest)throw new InvalidOperationException("Source fixture seam unavailable");
        var monitor=foreignMonitor?new HWiNFOTemperatureMonitor():cpuTemperatureMonitor??
            throw new InvalidOperationException("Source monitor is not armed");
        var field=typeof(HWiNFOTemperatureMonitor).GetField("sampleReadTick",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic)
            ??throw new InvalidOperationException("Original monitor freshness field changed");
        field.SetValue(monitor,sampledTick??Environment.TickCount64);
        OnHcCpuTemperatureChanged(monitor,value);
        if(foreignMonitor)monitor.Dispose();
    }
}
