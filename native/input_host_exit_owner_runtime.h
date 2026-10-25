#pragma once
// Terminal app-exit resource owner, on the existing cleanup worker/HostMx.
// This is not another player transaction or target recovery state machine.
// The business Owned predicate includes hardware/history and cannot be used as
// a local exit wait. Those responsibilities remain separately retained.
static std::atomic<bool> g_inputHostExitOrdinaryStopAttempted{false};
static std::atomic<bool> g_inputHostExitLocalReturned{false};

static bool inputHostExitLocalOwnershipReleased() noexcept {
    if(!g_inputHostExitLocalReturned.load(std::memory_order_acquire))return false;
    try {
        std::unique_lock<InputHostMutex> lock(g_inputHostMx,std::try_to_lock);
        if(!lock.owns_lock()||g_inputHostProcess||g_inputHostPrimaryThread||g_inputHostServerPipe||g_inputHostUnadoptedPipe)return false;
        if(auto* owner=ymcc::inputHost::pipeTransport::existingProcessRuntime()){
            const auto state=owner->snapshot();return state.available&&state.stopped;
        }
        return true;
    }catch(...){return false;}
}
static void inputHostNoteExitStopAttempted() noexcept {
    g_inputHostExitOrdinaryStopAttempted.store(true,std::memory_order_release);
}
static bool inputHostRetireLocalResourcesForExit() {
    const ymcc::inputHost::exitRetirement::Authority authority{
        g_exitRequested.load(std::memory_order_acquire),inputHostExitFenceRaised(),
        inputCaptureExitOwnershipReleased(),g_inputHostExitOrdinaryStopAttempted.load(std::memory_order_acquire)};
    if(!authority.permits())return false;
    std::unique_lock<InputHostMutex> lock(g_inputHostMx);
    // The producer has returned. Native's pipe is borrowed; its Runtime keeps
    // the original/duplicate/event/pending IO until actual retirement.
    if(g_inputHostServerPipe){inputHostPipeCloseOwned(g_inputHostServerPipe);g_inputHostServerPipe=nullptr;}
    g_inputHostPipeConnected=false;g_inputHostActive=false;
    const auto process=ymcc::inputHost::exitRetirement::retire(
        g_inputHostProcess,g_inputHostCreatedIdentity,authority,g_inputHostExitForceSpent,1000);
    const bool primary=inputHostPrimaryThreadRetirementProvenLocked("app-exit-owned-return");
    const bool pipe=inputHostPipeRetirementProvenLocked();
    const unsigned debts=(g_inputHostProcess?1u:0u)|(!primary?2u:0u)|(!pipe?4u:0u);
    g_inputHostExitLocalReturned.store(debts==0,std::memory_order_release);
    static unsigned previous=~0u;
    if(debts!=previous||process.forceAttempted){
        try {appendNativeLifecycleLog("input-host-exit-ownership",{
            {"version",1},{"scope","local-process-primary-pipe-only;target-and-physical-return-separate"},
            {"exitSessionId",g_exitSessionId},{"runId",g_inputHostRunId},{"epoch",g_inputHostEpoch},
            {"debts",debts},{"localOwnersReturned",debts==0},
            {"processOwnerRetained",g_inputHostProcess!=nullptr},{"primaryOwnerRetained",g_inputHostPrimaryThread!=nullptr},
            {"pipeReturned",pipe},{"forceAttempted",process.forceAttempted},{"forceAccepted",process.forceAccepted},
            {"forceError",process.forceError},{"forceBudgetSpent",process.forceBudgetSpent},
            {"creationIdentityVerified",process.identityVerified},{"expectedPid",process.expected.pid},
            {"initialWaitState",process.initialWaitState},{"initialWaitError",process.initialWaitError},
            {"processExitObserved",process.process.exitConfirmed},{"processHandleClosed",process.process.handleClosed},
            {"processCloseError",process.process.closeError},{"forceWaitBudgetMs",process.waitBudgetMs},
            {"targetReturnVerified",false},{"physicalReturnVerified",false}});}catch(...){}
        previous=debts;
    }
    // Existing Clear retains quarantine/endpoint/physical/durable duties. A
    // forced process exit is never changed into RELEASE_TARGET or physical ACK.
    if(debts==0){
        // Do not turn a post-force cached XInput plane into proof that a
        // generic HID target disappeared. Reuse only exact owned-PnP scopes,
        // or the already-proven no-target-command path, for an exit return.
        const bool qualifiedReturn=g_msiSteamDeckReleaseDebt.required()||
            g_psEndpointOwnership.retirementRequired()||inputHostPipeOnlyReturnAdmittedLocked();
        inputHostClearLocked();
        lock.unlock();
        if(qualifiedReturn)inputHostReconcileQuarantineOnLifecycleThread();
    }
    return debts==0;
}
static void inputHostAwaitLocalRetirementForExit() {
    const auto began=GetTickCount64();bool reported=false;
    for(;;){
        if(inputHostRetireLocalResourcesForExit())return;
        if(!reported&&GetTickCount64()-began>=30000){reported=true;
            try{appendNativeLifecycleLog("input-host-exit-retained",{
                {"exitSessionId",g_exitSessionId},{"observationBudgetMs",30000},
                {"localOwnersReturned",false},{"boundedTotalDestructionProven",false},
                {"scope","same-owner-retirement-only;no-new-Start-or-target"}});}catch(...){}
        }
        // Permanent OS Close or pending IO remains UNKNOWN and retains its
        // owner. Observation cannot renew the one force budget or report DONE.
        Sleep(250);
    }
}
