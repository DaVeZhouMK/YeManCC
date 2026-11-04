using System;
namespace YeManFanHost;

internal sealed partial class FanHostEngine
{
    private void ApplySourceSnapshot(HostState snapshot)
    {
        var backend=realBackend;if(backend?.SourceControlMode!=true)return;
        snapshot.SourceFanMode=true;snapshot.SourceFanOwnerEntered=backend.SourceOwnerEntered;
        snapshot.SourceFanGeneration=backend.SourceGeneration;
        snapshot.SourceFanClosePending=backend.SourceCloseIsOwed;
        snapshot.SourceFanClosed=backend.SourceControlCloseComplete;
        snapshot.SourceFanAutomaticReturned=backend.SourceAutomaticReturnObserved;
        snapshot.HardwareWritesObserved|=backend.SourceWritesObserved;
        snapshot.RecoveryClaimEvidence=System.Text.Json.JsonSerializer.SerializeToElement(new {
            sourceFanGeneration=snapshot.SourceFanGeneration,
            oemReleaseActionKind=snapshot.OemReleaseActionKind,
            oemReleaseActionClass=ReleaseEvidenceClass(snapshot.HardwareWritesObserved,snapshot.OemReleaseActionKind),
            virtualCloseIsNotPhysicalRelease=true, oemPhysicalOwnershipConfirmed=false
        }, new System.Text.Json.JsonSerializerOptions(JsonDefaults.Options) {
            DefaultIgnoreCondition=System.Text.Json.Serialization.JsonIgnoreCondition.Never });
        snapshot.OemPhysicalOwnershipConfirmed=false;
    }
    private void SynchronizeSourceReleaseEvidence()
    {
        var backend=realBackend!;
        state.HardwareWritesObserved|=backend.SourceWritesObserved;
        state.OemReleaseActionKind=backend.OemReleaseActionKind;
        state.OemReleaseActionEvidence=backend.OemReleaseActionEvidence;
        state.OemRestoreEvidence=backend.OemRestoreEvidence;
        state.OemOwnershipStatus=backend.OemOwnershipStatus;
        // Source register readback and handle closure are explicit software
        // observations. No HC callback or physical takeover was observed.
        state.OemRestoreConfirmed=false;state.HcRestoreCallbackReturned=false;
        state.OemPhysicalOwnershipConfirmed=false;
        state.HcVirtualCloseReturned=backend.HcVirtualCloseReturned;
        state.HcDeviceManagerStopCompleted=backend.HcDeviceManagerStopCompleted;
        state.HcDeviceManagerStopInvoked=backend.HcDeviceManagerStopInvoked;
        state.HcManagerCleanupEvidence=backend.HcManagerCleanupEvidence;
        state.HcDeviceManagerLifecycle=backend.HcDeviceManagerLifecycle;
        state.HcManagerFactoryLifecycle=backend.HcManagerFactoryLifecycle;
        ApplySourceSnapshot(state);
    }
    private bool CloseSourceSessionForLifecycle(bool stopDeviceManager)
    {
        var backend=realBackend!;state.HardwareWritesObserved|=backend.SourceWritesObserved;
        state.HardwareWritesEnabled=false;
        try
        {
            backend.Close(stopDeviceManager);SynchronizeSourceReleaseEvidence();
            bool releaseSafe=IsOemReleaseSafe(state.HardwareWritesObserved,state.OemReleaseActionKind);
            if(!backend.SourceControlCloseComplete||backend.SourceCloseIsOwed||!releaseSafe)
                throw new InvalidOperationException("Source return or exact terminal closure not observed");
            state.OpenCalled=false;state.OpenEventsCalled=false;
            state.HcCloseCleanupPending=false;state.UnknownState=false;state.RecoveryReadbackRequired=false;
            state.LastError=null;
            if(stopDeviceManager)ClearLease();
            diagnostics.Write("restore.source-session-closed",new { generation=backend.SourceGeneration,
                automaticReturned=backend.SourceAutomaticReturnObserved,terminalClosed=backend.SourceControlCloseComplete,
                state.OemReleaseActionKind,hcLifecycleClaimed=false,physicalObservation="not-observed" });
            return true;
        }
        catch(Exception error)
        {
            SynchronizeSourceReleaseEvidence();state.UnknownState=true;state.RecoveryReadbackRequired=true;
            state.HcCloseCleanupPending=backend.SourceCloseIsOwed||backend.OperationTimedOut;
            state.LastError=ExceptionText(error);
            diagnostics.Write("restore.source-session-pending",new {error=state.LastError,backend.SourceIoPending,
                backend.SourceCloseIsOwed,physicalObservation="not-observed"});
            return false;
        }
    }
    private bool RestoreSourceSessionForEngine(bool close,bool stopDeviceManager,string cause)
    {
        state.SetFanControlRestoreAttempted=true;
        var result=CloseSourceSessionForLifecycle(close&&stopDeviceManager);
        if(result)
        {
            state.State=close?"AwaitingCloseCleanup":"AwaitingControl";
            ClearLease();
        }
        return result;
    }
}
