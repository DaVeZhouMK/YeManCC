#pragma once
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>

// R3 normal-execution continuation. No independent timer, player retry
// admission or Stop lives here. Exact old tuple or historical duties must be
// retired by the existing return owner before the original remaining Start.
// Cold observation uses the same queue/ticket/deadline, never a second cycle.
namespace ymcc::inputHost::switchExecutionReturnRuntime {
struct State {
    std::uint64_t requestId=0,power=0,oldEpoch=0;
    std::string oldRunId,persona;
    bool pending=false,returned=false,queued=false;
    bool cold=false;
    unsigned coldPasses=0;
    std::uint64_t nextObservation=0;
};
static State state; // g_inputHostMx, including optional logging.
static void logLocked(const char* stage,const char* reason,bool startReturned=false) noexcept {
    try {const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
        appendNativeLifecycleLog("input-host-switch-execution",{
            {"lifecycleVersion",ymcc::VirtualStartProtection::lifecycleVersion},
            {"stage",stage},{"reason",reason},{"requestId",state.requestId},
            {"persona",state.persona},{"oldHostEpoch",state.oldEpoch},{"oldRunId",state.oldRunId},
            {"powerGeneration",state.power},{"pending",state.pending},{"returned",state.returned},
            {"queued",state.queued},{"startReturned",startReturned},
            {"coldHistoricalReturn",state.cold},{"coldPasses",state.coldPasses},
            {"elapsedMs",view.requestId==state.requestId?json(view.elapsedMs):json(nullptr)},
            {"remainingMs",view.requestId==state.requestId?json(view.remainingMs):json(nullptr)},
            {"fallbackAttempts",view.requestId==state.requestId?json(view.fallbackAttempts):json(nullptr)},
            {"resultScope","execution-only"},{"gameInputVerified",false}});
    }catch(...){}
}
static bool currentLocked() {
    return state.pending && currentPowerGeneration()==state.power &&
        g_virtualStartProtection.canContinueExecution(state.requestId,state.persona,inputHostStartProtectionNow()) &&
        inputHostConfiguredPersona()==state.persona && !inputHostExitFenceRaised() &&
        !g_exitRequested.load(std::memory_order_acquire) && !g_inputCaptureStop.load(std::memory_order_acquire);
}
static bool cleanLocked() {
    return !g_inputHostProcess && !g_inputHostEpoch && !g_inputHostHello && !g_inputHostPrepared &&
        !g_inputHostNeutralized && !g_inputHostActive && !g_hidHideVisibilityJournal.active &&
        !g_inputHostReleaseQuarantined && !g_msiSteamDeckReleaseDebt.required();
}
static bool pausedLocked() {
    return (!state.cold && (!g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled)) ||
        g_externalPriorityVirtualSuspended.load(std::memory_order_acquire) ||
        g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready ||
        !g_inputReady.load(std::memory_order_acquire) || !sgVirtualWriterAllowedNow() ||
        g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire) ||
        g_shortcutRecordingActive.load(std::memory_order_acquire) || physicalReleaseWindowActive();
}
static void retireLocked(const char* reason) {
    state.pending=false;state.queued=false;
    g_inputHostSwitchExecutionReturnPending.store(false,std::memory_order_release);
    logLocked("return-continuation-retired",reason);
}
}

static bool inputHostSwitchExecutionAwaitingLocked(std::uint64_t ticket) {
    using namespace ymcc::inputHost::switchExecutionReturnRuntime;
    if(!state.pending)return false;
    if(!currentLocked()){retireLocked("request-power-or-intent-retired");return false;}
    return ticket==state.requestId;
}
// Cold restoration uses this same pending continuation and queue, with no
// second lifetime or retry admission. Three bounded observation passes only;
// they never create a target and do not spend the classified fallback quota.
static bool inputHostSwitchExecutionBeginColdLocked(std::uint64_t ticket,
        ymcc::targetReturn::coldExecution::Reason reason) {
    using namespace ymcc::inputHost::switchExecutionReturnRuntime;
    if(state.pending)return inputHostSwitchExecutionAwaitingLocked(ticket);
    const auto persona=inputHostConfiguredPersona();
    if(!g_virtualStartProtection.canContinueExecution(ticket,persona,inputHostStartProtectionNow()))return false;
    state={ticket,currentPowerGeneration(),0,{},persona,true,false,false,true,
        reason==ymcc::targetReturn::coldExecution::Reason::ContextRetired?0U:1U,inputHostStartProtectionNow()+1000};
    g_inputHostSwitchExecutionReturnPending.store(true,std::memory_order_release);
    logLocked("cold-return-begin",ymcc::targetReturn::coldExecution::reasonName(reason));return true;
}
// The same cold return consumer serves startup-OFF and a later admitted OFF
// with no local Host/journal. Return true only when this owner handles the
// historical duty; the caller must not emit a competing empty Stop candidate.
static bool inputHostPerformColdStartupReturn() {
    using namespace ymcc::inputHost::switchExecutionReturnRuntime;
    std::lock_guard<InputHostMutex> lock(g_inputHostMx);
    if(inputHostConfiguredPersona()!="disabled" || inputHostTargetReturnHistorySettledLocked())return false;
    // No local Host alone is insufficient: MSI mode return or a retired PS
    // endpoint still belongs to normal Stop. Never skip that owner's cleanup.
    if(!cleanLocked() || g_psEndpointOwnership.retirementRequired() ||
            g_msiDinputReturnOwed.load(std::memory_order_acquire) ||
            g_msiDinputRouteDinputArmed.load(std::memory_order_acquire) ||
            g_msiPersonaModeHandoff.active)return false;
    if(state.pending && inputHostSwitchExecutionAwaitingLocked(
            g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId))return true;
    const auto admission=inputHostStartProtectionConsume("disabled");
    if(!admission.accepted || !g_virtualStartProtection.canContinueExecution(admission.requestId,"disabled",inputHostStartProtectionNow()))return false;
    g_inputHostStartProtectionBoundTicket=admission.requestId;
    g_inputHostSwitchVerificationPending.store(admission.requestId,std::memory_order_release);
    const auto result=inputHostTargetReturnColdPassLocked(admission.requestId,currentPowerGeneration(),"disabled");
    if(result.clean())inputHostStartProtectionFinish(admission.requestId,"success","cold-owned-return-execution-complete");
    else if(!inputHostSwitchExecutionBeginColdLocked(admission.requestId,result.reason))
        inputHostStartProtectionFinish(admission.requestId,"failure","cold-return-continuation-refused");
    return true;
}
static bool inputHostSwitchExecutionBeginReturnLocked(std::uint64_t ticket) {
    using namespace ymcc::inputHost::switchExecutionReturnRuntime;
    if(state.pending)return inputHostSwitchExecutionAwaitingLocked(ticket);
    const auto persona=inputHostConfiguredPersona();
    if(!g_virtualStartProtection.canContinueExecution(ticket,persona,inputHostStartProtectionNow()) ||
        !g_inputHostProcess || !g_inputHostEpoch || g_inputHostRunId.empty() ||
        g_inputHostBoundPowerGeneration!=currentPowerGeneration())return false;
    state={ticket,currentPowerGeneration(),g_inputHostEpoch,g_inputHostRunId,persona,true,false,false};
    g_inputHostSwitchExecutionReturnPending.store(true,std::memory_order_release);
    logLocked("return-begin","normal-persona-retirement");return true;
}
static void inputHostSwitchExecutionReturnStoppedLocked() {
    using namespace ymcc::inputHost::switchExecutionReturnRuntime;
    if(!state.pending)return;
    if(cleanLocked() && !g_inputHostStartBlocked){retireLocked("normal-return-completed-inline");return;}
    // Do not classify an expected delayed return as a failed creation or spend
    // fallback. The return owner alone can prove endpoint/physical restoration.
    g_inputHostFaultHoldsStart.store(true,std::memory_order_release);
    logLocked("return-wait","old-owned-target-not-yet-retired");
}
static bool inputHostSwitchExecutionReturnedLocked(std::uint64_t epoch,const std::string& runId) {
    using namespace ymcc::inputHost::switchExecutionReturnRuntime;
    if(!currentLocked() || !epoch || epoch!=state.oldEpoch || runId!=state.oldRunId || !cleanLocked())return false;
    state.returned=true;
    logLocked("return-proven","exact-normal-old-tuple-retired");return true;
}
// G3E field 2026-10-21: a NEW admitted player intent can arrive while an
// older failed Host is still returning. Keep it on the existing R3 continuation;
// quarantine alone may retire the old tuple, never erase this remaining Start.
// Called on the lifecycle owner with g_inputHostMx held. This adopts no cleanup
// authority: the exact old quarantine owner still proves endpoint/physical return.
static bool inputHostSwitchExecutionPreserveMsiIntentLocked(std::uint64_t ticket,
        std::uint64_t oldEpoch,const std::string& oldRunId,bool returned) {
    using namespace ymcc::inputHost::switchExecutionReturnRuntime;
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    const auto setting=g_msiExplicitRearmTicket.snapshot();
    if(!msiClawKnownBoardAuthority() || !setting.wanted || !setting.sequence ||
            setting.settingPower!=currentPowerGeneration() || setting.persona!=view.persona ||
            !ticket || !oldEpoch || oldRunId.empty() ||
            view.requestId!=ticket || view.phase!="pending" || !view.locked ||
            !ymcc::controllerPersona::currentOutput(view.persona) ||
            view.persona!=inputHostConfiguredPersona() || !g_realStickTest.enabled ||
            !g_realStickTest.virtualTargetEnabled || g_inputHostProcess || inputHostExitFenceRaised() ||
            g_exitRequested.load(std::memory_order_acquire) || g_inputCaptureStop.load(std::memory_order_acquire))return false;
    if(returned ? !cleanLocked() : (!g_inputHostReleaseQuarantined ||
            g_inputHostEpoch!=oldEpoch || g_inputHostRunId!=oldRunId))return false;
    if(state.pending){
        if(currentLocked() && state.requestId==ticket)return true;
        retireLocked("older-return-intent-retired");
    }
    state={ticket,currentPowerGeneration(),oldEpoch,oldRunId,view.persona,true,returned,false};
    g_inputHostSwitchExecutionReturnPending.store(true,std::memory_order_release);
    logLocked(returned?"return-proven":"return-wait","new-explicit-MSI-intent-retains-old-return");
    return true;
}
static bool inputHostSwitchExecutionHoldMsiIntent(std::uint64_t ticket) {
    std::lock_guard<InputHostMutex> lock(g_inputHostMx);
    return inputHostSwitchExecutionPreserveMsiIntentLocked(ticket,g_inputHostEpoch,g_inputHostRunId,false);
}
static void inputHostSwitchExecutionReturnMaintenanceLocked() {
    using namespace ymcc::inputHost::switchExecutionReturnRuntime;
    if(!state.pending)return;
    if(!currentLocked()){retireLocked("request-power-or-intent-retired");return;}
    if(state.queued || !cleanLocked() || pausedLocked())return;
    if(state.cold){
        if(inputHostStartProtectionNow()<state.nextObservation)return;
        const auto environment=ymcc::inputHost::targetReturnRuntime::ColdEnvironment{state.requestId,state.power,state.persona};
        if(!environment.current())return; // known current owner/power/focus pause
    }else if(!state.returned)return;
    state.queued=true;
    try {const auto reason="switch-execution-return:"+std::to_string(state.requestId);
        if(!inputHostQueueLifecycleWork(reason.c_str()))retireLocked("queue-refused");
    }catch(...){retireLocked("queue-exception");}
}
static void inputHostPerformSwitchExecutionReturn(std::uint64_t ticket) {
    using namespace ymcc::inputHost::switchExecutionReturnRuntime;
    std::uint64_t power=0,deadline=0;
    {
        std::lock_guard<InputHostMutex> lock(g_inputHostMx);
        if(!state.pending || !state.queued || state.requestId!=ticket)return;
        state.queued=false;
        if(!currentLocked()){retireLocked("queued-request-retired");return;}
        if(!cleanLocked() || pausedLocked() || (!state.cold && !state.returned))return;
        if(state.cold){
            const auto result=inputHostTargetReturnColdPassLocked(ticket,state.power,state.persona);
            // An OS observation/file commit can outlast this request. Never
            // apply its failure hold or remaining Start to a newer intent.
            if(!currentLocked()){retireLocked("cold-observation-context-retired");return;}
            if(!result.clean()){
                if(result.reason!=ymcc::targetReturn::coldExecution::Reason::ContextRetired)++state.coldPasses;
                if(state.coldPasses>=3){
                    retireLocked("cold-return-bounded-observations-exhausted");
                    g_inputHostFaultHoldsStart.store(true,std::memory_order_release);
                    inputHostStartProtectionFinish(ticket,"failure","cold-return-still-unproven");
                }else state.nextObservation=inputHostStartProtectionNow()+1000;
                return;
            }
            state.returned=true;
            if(state.persona=="disabled"){
                retireLocked("cold-owned-return-execution-complete");
                inputHostStartProtectionFinish(ticket,"success","cold-owned-return-execution-complete");
                return;
            }
        }
        const auto now=inputHostStartProtectionNow();
        const auto view=g_virtualStartProtection.view(now);
        if(view.remainingMs>(std::numeric_limits<std::uint64_t>::max)()-now)return;
        power=state.power;deadline=now+view.remainingMs;
        // Exact return was proven. Claim only this remaining Start, once, on the
        // serialized owner; neither a second Stop nor fallback reservation occurs.
        state.pending=false;state.queued=false;
        g_inputHostSwitchExecutionReturnPending.store(false,std::memory_order_release);
        if(!state.cold){g_inputHostStartBlocked=false;g_inputHostFaultHoldsStart.store(false,std::memory_order_release);}
        logLocked("return-start","remaining-normal-start-claimed");
    }
    // Reuse the protected Start deadline context; it grants no fallback quota.
    const auto oldDeadline=g_inputHostSwitchRecoveryStartDeadline;
    const auto oldRequest=g_inputHostSwitchRecoveryStartRequestId,oldPower=g_inputHostSwitchRecoveryStartPower;
    struct Restore {std::uint64_t deadline,request,power;~Restore(){g_inputHostSwitchRecoveryStartDeadline=deadline;
        g_inputHostSwitchRecoveryStartRequestId=request;g_inputHostSwitchRecoveryStartPower=power;}}
        restore{oldDeadline,oldRequest,oldPower};
    g_inputHostSwitchRecoveryStartDeadline=oldDeadline && oldDeadline<deadline?oldDeadline:deadline;
    g_inputHostSwitchRecoveryStartRequestId=ticket;g_inputHostSwitchRecoveryStartPower=power;
    const bool started=inputHostStart();
    recordStartupInputAttempt(started);
    if(started){ymccSlotHandoffSchedule();inputHostStampBuildSettle();}
    std::lock_guard<InputHostMutex> lock(g_inputHostMx);
    if(state.requestId==ticket)logLocked("return-start-result",started?"prepared-awaiting-current-source":"normal-start-failed",started);
}
