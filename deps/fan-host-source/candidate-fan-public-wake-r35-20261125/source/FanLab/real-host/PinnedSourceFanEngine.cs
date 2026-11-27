using System;
namespace YeManFanHost;

internal sealed partial class FanHostEngine
{
    // A completed fault cycle can own an already-retired source session.
    // Parent exit consumes that exact completion; it does not restart its budget.
    private bool TrySettleCompletedSourceParentExit()
    {
        if(!System.Threading.Monitor.TryEnter(gate))return false;
        try
        {
            var backend=realBackend;
            if(stopped||backend?.SourceControlMode!=true||backend.OperationTimedOut||backend.SourceIoPending||
                !backend.SourceControlCloseComplete||backend.SourceCloseIsOwed||!backend.SourceOwnerEntered||
                backend.IsOpen||backend.OpenAttempted||state.OpenCalled||state.OpenEventsCalled||
                state.HardwareWritesEnabled||state.HardwareWrites||state.UnknownState||state.HcCloseCleanupPending||
                replacementHandoffInFlight||!IsOemReleaseSafe(state.HardwareWritesObserved,backend.OemReleaseActionKind)||
                (state.HardwareWritesObserved&&!backend.SourceAutomaticReturnObserved))return false;
            lock(acceptanceGate)
            {
                var completed=recoveryCycle;
                if(completed is not {Completed:true,Terminal:false}||recoveryJournalBlocked||controlIntentJournalBlocked)return false;
                var durable=ReadPersistedRecoveryCycle();
                if(recoveryJournalBlocked||durable is not {Completed:true,Terminal:false}||
                    durable.CycleId!=completed.CycleId||durable.ParentPid!=completed.ParentPid||
                    durable.StartedAtUtc!=completed.StartedAtUtc||durable.Attempts!=completed.Attempts||
                    durable.Replacements!=completed.Replacements)return false;
                parentExitAcceptance??=new ParentExitAcceptance(diagnostics.HostInstanceId,completed.CycleId,
                    "completed-source-retirement",ParentExitAcceptanceScope,options.ParentPid??0,DateTimeOffset.UtcNow);
            }
            ClearLease();activeCurve=null;suspendedCurve=null;state.CloseCalled=true;
            state.State="Stopped";stopped=true;
            diagnostics.Write("parent-exit-handoff.source-already-retired",new {
                cycleId=recoveryCycle!.CycleId,generation=backend.SourceGeneration,
                newRecoveryBudget=false,newHardwareActions=false,physicalOwnershipClaimed=false });
            return true;
        }
        finally{System.Threading.Monitor.Exit(gate);}
    }

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
