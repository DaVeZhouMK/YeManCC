#pragma once
#include "virtual_target_return_cold_execution.h"
#include "virtual_target_return_pnp_absence.h"

// Included after targetReturnRuntime's real producer/normal-return definitions.
// All writes remain on g_inputHostMx and the original R3 absolute deadline.
// The continuation lives in virtual_switch_execution_return_runtime.h.
static bool inputHostTargetReturnHistorySettledLocked() {
    return !ymcc::inputHost::targetReturnRuntime::state.coldHistoryPending;
}
static ymcc::targetReturn::pnpAbsence::Result inputHostTargetReturnColdAbsence(
        const std::vector<std::string>& knownIds) {
    return ymcc::targetReturn::pnpAbsence::verify(knownIds);
}
static bool inputHostTargetReturnColdHidden(std::vector<std::string>& hidden) {
    std::string raw;DWORD code=1;
    return hidHideRun(L"--dev-list",&raw,&code)&&code==ERROR_SUCCESS&&
        ymcc::targetReturn::coldExecution::parseHidden(raw,hidden);
}
namespace ymcc::inputHost::targetReturnRuntime {
struct ColdEnvironment {
    std::uint64_t ticket=0,power=0;
    std::string persona;
    bool absenceObserved=false;
    ymcc::targetReturn::pnpAbsence::Result absenceResult;
    bool physicalAbsenceObserved=false;
    ymcc::targetReturn::pnpAbsence::Result physicalAbsenceResult;
    bool current() const {
        if(!g_inputHostMx.ownedByCurrentThread() || !ticket ||
            !g_virtualStartProtection.canContinueExecution(ticket,persona,inputHostStartProtectionNow()) ||
            currentPowerGeneration()!=power || inputHostConfiguredPersona()!=persona || !contextCurrent() ||
            inputHostExitFenceRaised() || g_exitRequested.load(std::memory_order_acquire) ||
            g_inputCaptureStop.load(std::memory_order_acquire) ||
            g_inputHostProcess || g_inputHostHello || g_inputHostPrepared || g_inputHostPrepareMayHaveReachedHost ||
            g_inputHostNeutralized || g_inputHostActive || g_hidHideVisibilityJournal.active ||
            g_inputHostReleaseQuarantined || g_msiSteamDeckReleaseDebt.required() ||
            g_psEndpointOwnership.retirementRequired() || msiForegroundIsolationBorrowBusy() ||
            g_externalPriorityVirtualSuspended.load(std::memory_order_acquire) ||
            g_shortcutRecordingActive.load(std::memory_order_acquire) ||
            g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready ||
            !g_inputReady.load(std::memory_order_acquire) || !sgVirtualWriterAllowedNow() ||
            g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire))return false;
        // Native foreground has an intentional physical lease of its own. A
        // cold duty must never unhide through that current user's ownership.
        std::unique_lock<std::mutex> lease(g_msiNativeForegroundMx,std::try_to_lock);
        return lease.owns_lock() && !g_msiNativeForegroundLease.active &&
            !g_msiNativeForegroundLease.cycleProcess && !g_msiNativeForegroundLease.restorePending;
    }
    const duty::Record* record()const{return state.session?state.session->record():nullptr;}
    bool nativeDead(const duty::ProcessIdentity& value){return inputHostTargetReturnProbePhysicalWriter(value).state==duty::process::State::Dead;}
    bool hostDead(const duty::ProcessIdentity& value){return inputHostTargetReturnProbePhysicalWriter(value).state==duty::process::State::Dead;}
    bool writersQuiet(){return physicalWritersQuiescent();}
    bool targetAbsent(const std::vector<std::string>& ids){
        absenceObserved=true;absenceResult=inputHostTargetReturnColdAbsence(ids);return absenceResult.absent();
    }
    bool visibility(std::vector<std::string>& hidden){return inputHostTargetReturnColdHidden(hidden);}
    bool pathProven(const std::string& root,const std::string& path){
        const auto ancestry=inputHostTargetReturnVerifyPhysicalPath(root,path);
        if(ancestry.proven())return true;
        // A reboot/mode change may remove the old devnode while its exact
        // owned HidHide configuration key remains. This operation only removes
        // that recorded key; it does not cycle/remove/reconfigure a device.
        // CM failure alone is insufficient: require a COMPLETE present-device
        // census with this exact old path absent as well as all virtual targets.
        if(ancestry.reason!=duty::physicalPath::Reason::TargetLocateFailed ||
            ancestry.configurationReturn!=CR_NO_SUCH_DEVNODE)return false;
        physicalAbsenceObserved=true;
        physicalAbsenceResult=inputHostTargetReturnColdAbsence({path});
        return physicalAbsenceResult.absent();
    }
    bool unhide(const std::string& path){
        if(!current())return false;
        DWORD code=1;return hidHideRun(L"--dev-unhide \""+U2W(path)+L"\"",nullptr,&code)&&code==ERROR_SUCCESS;
    }
    bool consumePath(const duty::Origin& origin,const std::string& path){
        return current()&&state.session&&state.session->ready()&&
            state.session->consumePath(state.session->record()->revision,origin,path);
    }
    bool consumeOrigin(const duty::Origin& origin){
        return current()&&state.session&&state.session->ready()&&
            state.session->consumeOrigin(state.session->record()->revision,origin);
    }
};
}
// VC-MSI-COLD-ENTRY-20261024-R1: queue only the original Elite Start so its
// cold-return consumer can run before source discovery. No BUS IO or admission.
static bool inputHostTargetReturnMayQueueMsiColdStart() {
    if(!msiClawKnownBoardAuthority())return false;
    std::unique_lock<InputHostMutex> host(g_inputHostMx,std::try_to_lock);
    if(!host.owns_lock() || g_inputHostStartBlocked ||
        g_inputHostFaultHoldsStart.load(std::memory_order_acquire) ||
        g_inputHostSwitchExecutionReturnPending.load(std::memory_order_acquire) ||
        g_inputHostSwitchRecoveryPending.load(std::memory_order_acquire))return false;
    using namespace ymcc::inputHost::targetReturnRuntime;
    if(!state.coldHistoryPending)return false;
    const auto* record=state.session?state.session->record():nullptr;
    if(!record || record->obligations.empty() ||
        record->machineBinding.rfind("windows-machine-guid:",0)!=0 ||
        !std::all_of(record->obligations.begin(),record->obligations.end(),
            [](const auto& item){return item.origin.cliWriterProtocol==1;}))return false;
    const auto request=g_virtualStartProtection.view(inputHostStartProtectionNow());
    // Bootstrap only the saved Elite startup before this process has any R3
    // request. Start, not BUS, consumes the first ticket and its 30s budget.
    if(!request.requestId && request.phase=="idle")
        return g_startupInputState.load(std::memory_order_acquire)==ymcc::StartupInputState::Waiting &&
            g_realStickTest.enabled && g_realStickTest.virtualTargetEnabled &&
            inputHostConfiguredPersona()=="elite" &&
            !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire);
    if(!request.requestId || !request.locked || request.phase!="pending" ||
        request.persona!="elite" || !request.remainingMs ||
        !g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled ||
        inputHostConfiguredPersona()!=request.persona ||
        g_inputHostVirtualIntentWriters.load(std::memory_order_acquire))return false;
    // A pending continuation owns its three existing observations. Never turn
    // retained history into another Start cycle, ticket, fallback or deadline.
    const ColdEnvironment environment{request.requestId,currentPowerGeneration(),request.persona};
    return environment.current();
}

static ymcc::targetReturn::coldExecution::Result inputHostTargetReturnColdPassLocked(
        std::uint64_t ticket,std::uint64_t power,const std::string& persona) {
    using namespace ymcc::inputHost::targetReturnRuntime;
    using namespace ymcc::targetReturn::coldExecution;
    if(!g_inputHostMx.ownedByCurrentThread())return {ymcc::targetReturn::coldExecution::State::Deferred,Reason::ContextRetired};
    // Loading history is read-only with respect to the device. A clean policy
    // return/idempotent Start needs no cold work and may have no active player
    // period. Require the original R3 period only when historical duties exist.
    if(!open())return {ymcc::targetReturn::coldExecution::State::Deferred,Reason::StoreUnavailable};
    if(!state.coldHistoryPending)
        return {ymcc::targetReturn::coldExecution::State::Clean,Reason::Complete};
    const auto clock=inputHostStartProtectionNow();
    const auto view=g_virtualStartProtection.view(clock);
    const auto oldDeadline=g_inputHostSwitchRecoveryStartDeadline;
    const auto oldRequest=g_inputHostSwitchRecoveryStartRequestId,oldPower=g_inputHostSwitchRecoveryStartPower;
    struct Restore {std::uint64_t deadline,request,power;~Restore(){g_inputHostSwitchRecoveryStartDeadline=deadline;
        g_inputHostSwitchRecoveryStartRequestId=request;g_inputHostSwitchRecoveryStartPower=power;}}
        restore{oldDeadline,oldRequest,oldPower};
    if(view.requestId!=ticket||!view.locked||view.persona!=persona||!view.remainingMs||
        view.remainingMs>(std::numeric_limits<std::uint64_t>::max)()-clock)
        return {ymcc::targetReturn::coldExecution::State::Deferred,Reason::ContextRetired};
    const auto original=clock+view.remainingMs;
    g_inputHostSwitchRecoveryStartDeadline=oldDeadline&&oldDeadline<original?oldDeadline:original;
    g_inputHostSwitchRecoveryStartRequestId=ticket;g_inputHostSwitchRecoveryStartPower=power;
    ColdEnvironment environment{ticket,power,persona};
    const auto result=run(environment);
    if(result.clean())state.coldHistoryPending=false;
    try{appendNativeLifecycleLog("input-host-cold-return",{
        {"lifecycleVersion",ymcc::VirtualStartProtection::lifecycleVersion},{"requestId",ticket},
        {"powerGeneration",power},{"persona",persona},{"stage",result.clean()?"execution-complete":"execution-deferred"},
        {"reason",reasonName(result.reason)},{"originKey",result.originKey},{"pathInstanceId",result.pathInstanceId},
        {"targetAbsenceObserved",environment.absenceObserved},
        {"targetAbsenceState",environment.absenceObserved?ymcc::targetReturn::pnpAbsence::stateName(environment.absenceResult.state):"unobserved"},
        {"targetAbsenceReason",environment.absenceObserved?ymcc::targetReturn::pnpAbsence::reasonName(environment.absenceResult.reason):"unobserved"},
        {"targetAbsenceScannedNodes",environment.absenceResult.scannedNodes},
        {"targetAbsenceWin32Error",environment.absenceResult.win32Error},
        {"targetAbsenceConfigurationReturn",environment.absenceResult.configurationReturn},
        {"targetAbsenceFailedInstance",environment.absenceResult.failedInstanceId},
        {"targetAbsenceDeviceIdChars",environment.absenceResult.deviceIdChars},
        {"targetAbsenceDeviceIdHead",environment.absenceResult.deviceIdHead},
        {"targetAbsenceDeviceIdTail",environment.absenceResult.deviceIdTail},
        {"targetAbsenceHardwareIdsBytes",environment.absenceResult.hardwareIdsBytes},
        {"targetAbsenceHardwareIdsType",environment.absenceResult.hardwareIdsType},
        {"targetAbsenceHardwareIdsHead",environment.absenceResult.hardwareIdsHead},
        {"targetAbsenceHardwareIdsTail",environment.absenceResult.hardwareIdsTail},
        {"physicalPathAbsenceObserved",environment.physicalAbsenceObserved},
        {"physicalPathAbsenceState",environment.physicalAbsenceObserved?ymcc::targetReturn::pnpAbsence::stateName(environment.physicalAbsenceResult.state):"unobserved"},
        {"physicalPathAbsenceReason",environment.physicalAbsenceObserved?ymcc::targetReturn::pnpAbsence::reasonName(environment.physicalAbsenceResult.reason):"unobserved"},
        {"unhideAttempts",result.unhideAttempts},
        {"settledPaths",result.settledPaths},{"settledOrigins",result.settledOrigins},
        {"originsRemaining",state.session&&state.session->record()?state.session->record()->obligations.size():0},
        {"gameInputVerified",false},{"fallbackAttempt",false},{"deadlineRenewed",false}});}catch(...){}
    // Old history must return before native foreground can borrow the source.
    // Replay the existing UI intent only on the clean edge and still-current
    // original OFF. This queues existing work; no new owner/timer/device IO.
    if(result.clean() && persona=="disabled" && environment.current())
        msiNativeForegroundUiEvent(true);
    return result;
}
// Bootstrap loads provenance before the input BUS can attempt a target. It
// enqueues only; the existing lifecycle worker owns any later device operation.
static void inputHostTargetReturnQueueColdStartup() {
    using namespace ymcc::inputHost::targetReturnRuntime;
    std::lock_guard<InputHostMutex> lock(g_inputHostMx);
    (void)open();
    if(state.coldHistoryPending)inputHostQueueLifecycleWork("startup-owned-return");
}
