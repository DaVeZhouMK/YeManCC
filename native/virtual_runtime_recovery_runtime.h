#pragma once
#include <cstdint>
#include <limits>
#include <string>

// VC-LIFECYCLE-20261020-R3 extension: a real transport fault AFTER a settled
// player cycle is a separate runtime incident. Never reopen that old final or
// renew its clocks. The original serial worker owns one safety Stop; only real
// old-target/physical/pipe return permits a distinct bounded recovery Start.
// Healthy idle and intentional focus-neutral input never offer an incident.
namespace ymcc::inputHost::runtimeRecovery {
struct State {
    std::uint64_t serial=0,requestId=0,hostEpoch=0,power=0,intentRevision=0,demandRevision=0,sequence=0,recoveryRequestId=0;
    std::string runId,hostInstanceId,persona,command;
    DWORD error=ERROR_SUCCESS;
    bool pending=false,queued=false,stopInvoked=false,spent=false;
    bool monitoring=false,wavePermit=false,launching=false;
    std::uint64_t wave=0,retryAt=0,nextProbe=0;
    bool preHostWriterWait=false; // exact observed defer, never inferred from idle/no signal
    std::uint64_t observationUntil=0;
    std::string nativeWriterAttempt;

};
static State state; // Existing Host mutex. Retain spent identity for deduplication.
static std::uint64_t nextSerial=0;
static bool retryable(DWORD error) noexcept {
    return error==ERROR_BROKEN_PIPE || error==ERROR_NO_DATA || error==ERROR_PIPE_NOT_CONNECTED ||
        error==ERROR_SEM_TIMEOUT || error==ERROR_TIMEOUT || error==WAIT_TIMEOUT;
}
static void note(const char* stage,const char* reason) noexcept {
    try {
        appendNativeLifecycleLog("input-host-runtime-recovery",{
            {"lifecycleVersion",ymcc::VirtualStartProtection::lifecycleVersion},
            {"stage",stage},{"reason",reason},{"serial",state.serial},{"requestId",state.requestId},
            {"recoveryRequestId",state.recoveryRequestId},{"hostEpoch",state.hostEpoch},
            {"wave",state.wave},{"retryAtTick",state.retryAt},{"nextProbeTick",state.nextProbe},
            {"monitoring",state.monitoring},{"wavePermit",state.wavePermit},
            {"origin",state.preHostWriterWait?"pre-host-native-writer-wait":"post-final-transport-fault"},
            {"observationUntilTick",state.observationUntil},{"nativeWriterAttempt",state.nativeWriterAttempt},
            {"automaticAttemptLimit",state.preHostWriterWait?json(1):json(nullptr)},
            {"runId",state.runId},{"hostInstanceId",state.hostInstanceId},{"persona",state.persona},
            {"powerGeneration",state.power},{"intentRevision",state.intentRevision},
            {"demandRevision",state.demandRevision},{"currentDemandRevision",g_inputHostRecoveryIntent.revision()},
            {"command",state.command},{"sequence",state.sequence},{"error",static_cast<unsigned long>(state.error)},
            {"pending",state.pending},{"queued",state.queued},{"stopInvoked",state.stopInvoked},{"spent",state.spent},
            {"episodeAttempts",g_inputHostRecoveryEpisodeAttempts.load(std::memory_order_acquire)},
            {"rollingCount",inputHostRecoveryRollingCountLocked(GetTickCount64())},
            {"resultScope","runtime-incident-not-old-player-final"},{"gameInputVerified",false}});
    } catch(...) { /* Diagnostics cannot repeat a partially executed action. */ }
}
static void retire(const char* reason) noexcept {
    state.pending=false;state.queued=false;state.spent=true;state.monitoring=false;state.launching=false;
    g_inputHostRuntimeRecoveryPending.store(false,std::memory_order_release);
    note("retired",reason); // No Finish on the old player request.
}
// A real current Host may remain usable after its player request timed out.
// That final remains failed; a later real pipe fault is a separate incident.
// Unfinished/cancelled requests never qualify as settled operational demand.
static bool operationalTerminal(const ymcc::VirtualStartProtection::View& view) noexcept {
    return !view.locked && (view.phase=="success" || view.phase=="failure" || view.phase=="expired");
}
static const char* stale(bool includeHost=true) {
    if(inputHostExitFenceRaised() || g_exitRequested.load(std::memory_order_acquire) ||
        g_inputCaptureStop.load(std::memory_order_acquire))return "exit-or-capture-stop";
    if(currentPowerGeneration()!=state.power ||
        g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire) ||
        g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready)return "power-retired";
    // Snapshot publication is frequent (including identical values and ACKs).
    // Only a changed effective target/owner revokes continued recovery demand.
    // An active publisher is a temporary action barrier, never a new incident.
    if(!state.demandRevision || g_inputHostRecoveryIntent.revision()!=state.demandRevision)
        return "intent-retired";
    if(!g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) &&
        (!g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled ||
         inputHostConfiguredPersona()!=state.persona))return "intent-retired";
    // Recheck this origin's original observation deadline in the actual
    // executor as well as Maintenance. A queued token carries no fresh time
    // allowance. An already-admitted recovery keeps its own bounded period.
    if(state.preHostWriterWait && !state.recoveryRequestId &&
        (!state.observationUntil || GetTickCount64()>=state.observationUntil))
        return "pre-host-observation-budget-ended";
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    if(view.persona!=state.persona || view.requestId!=(state.recoveryRequestId?state.recoveryRequestId:state.requestId))return "player-period-retired";
    if(!state.recoveryRequestId) {
        if(state.preHostWriterWait) {
            if(view.phase!="pending" && view.phase!="verifying" && view.phase!="failure" && view.phase!="expired")
                return "pre-host-period-ended-without-failure";
        } else if(!operationalTerminal(view))return "player-period-retired";
    }
    if(includeHost && !state.launching && g_inputHostEpoch && (g_inputHostEpoch!=state.hostEpoch || g_inputHostRunId!=state.runId ||
        g_inputHostHostInstanceId!=state.hostInstanceId))return "host-owner-retired";
    return nullptr;
}
static bool oldTuple() {
    return g_inputHostEpoch==state.hostEpoch && g_inputHostRunId==state.runId &&
        g_inputHostHostInstanceId==state.hostInstanceId;
}
// Include every current in-memory/durable target. A failed successor can
// already have cleared its Native tuple; the earlier incident's settled epoch
// alone must never authorize forgetting that successor's outstanding duty.
static bool allTargetsReturned() {
    const auto& targets=ymcc::inputHost::targetReturnRuntime::state;
    const auto* record=targets.session?targets.session->record():nullptr;
    return record && targets.targets.empty() && targets.proven.empty() && record->obligations.empty();
}
// Existing Host -> native-lease lock order. This only reads the old owner's
// in-memory retirement result; it never polls/kills a process or clears debt.
static bool nativeWriterReturned() {
    std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
    const auto& owner=g_msiNativeForegroundLease;
    return !owner.active && !owner.cycleProcess && !owner.nativeRefreshWriter &&
        !g_msiNativeForegroundCyclePending.load(std::memory_order_acquire) &&
        !g_postHideCycleOwned.load(std::memory_order_acquire);
}
static bool returned() {
    return !g_inputHostEpoch && g_inputHostRunId.empty() && !g_inputHostProcess &&
        !g_inputHostPrimaryThread && !g_inputHostServerPipe && !g_inputHostHello &&
        !g_inputHostPrepared && !g_inputHostNeutralized && !g_inputHostActive &&
        !g_inputHostReleaseQuarantined && !g_hidHideVisibilityJournal.active &&
        inputHostOwnedCleanupProvenLocked() && allTargetsReturned() &&
        inputHostPipeRetirementProvenLocked() &&
        (state.preHostWriterWait && !state.hostEpoch ? nativeWriterReturned() :
            inputHostTargetReturnRuntimeSettledLocked(state.runId,state.hostEpoch));
}
static bool held(std::uint64_t now) {
    // Reuse source freshness, intentional-pause and cooldown rules. No new read,
    // device census, worker, timer or fault inference is introduced here.
    return g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) ||
        ymcc::inputHost::switchRecoveryRuntime::heldLocked(now);
}

// Only a classified post-final incident whose own recovery period has failed
// may request the existing shared final physical fallback. Missing/idle source
// and the pre-Host observation origin never grant that action. The shared
// terminal owner captures cancellation/cleanup identity and executes its Stop;
// runtime only retires its own dispatch before handing over, with no refund.
static bool terminalFailedRecovery(std::uint64_t now) {
    if(state.preHostWriterWait || !retryable(state.error) ||
       (state.command!="SUBMIT_FRAME" && state.command!="HIDHIDE_QUERY_STATE"))return false;
    if(!inputHostRecoveryBudgetExhaustedLocked(now) &&
       !g_inputHostRecoveryTerminalLogged.load(std::memory_order_acquire))return false;
    if(stale())return false;
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    const bool failedRecovery=state.recoveryRequestId && !view.locked &&
        (view.phase=="failure" || view.phase=="expired");
    // A NEW classified wire fault on an operational Host is not healthy
    // recovery success. An already-spent common budget cannot strand this
    // distinct incident before it ever acquires a recovery request.
    const bool newSettledFault=!state.recoveryRequestId && state.pending &&
        !state.monitoring && !state.launching && !state.spent && operationalTerminal(view);
    if(!failedRecovery && !newSettledFault)return false;
    // The shared owner can temporarily refuse capture (e.g. no current public
    // power Scope). Its void call is not an action receipt. Keep this exact
    // incident until the original terminal publication really exists.
    if(g_externalPriorityVirtualSuspended.load(std::memory_order_acquire))return true;
    if(now<state.nextProbe)return true;
    inputHostRecoveryTerminalLocked("runtime-failed-period-shared-budget-exhausted");
    if(g_inputHostRecoveryTerminalLogged.load(std::memory_order_acquire))
        retire("failed-recovery-shared-budget-exhausted");
    else state.nextProbe=now+1000; // Existing observation cadence, no new budget.
    return true;
}

static bool budgetBlocked(std::uint64_t now) {
    const auto fallback=g_inputHostRecoveryFallbackState.load(std::memory_order_acquire);
    // 1 is an existing physical-fallback owner; 3 is unproven return.
    // A wave can never override either. 2 is the original positive physical
    // readback, and still needs all target/pipe/visibility/durable return proof.
    if(fallback!=0 && (fallback!=2 || !returned()))return true;
    return inputHostRecoveryBudgetExhaustedLocked(now) ||
        g_inputHostRecoveryTerminalLogged.load(std::memory_order_acquire) ||
        (!state.wavePermit && fallback!=0);
}
// A new wave is an explicit slow action allowance, never a refund of the
// shared episode or a claim of health. Source/return UNKNOWN cannot consume it.
static bool waveReady(std::uint64_t now) {
    // A slow wave never bypasses the common episode/rolling or final terminal.
    if(inputHostRecoveryBudgetExhaustedLocked(now) ||
       g_inputHostRecoveryTerminalLogged.load(std::memory_order_acquire))return false;
    // This origin receives ONE shared-budget attempt only. It cannot borrow
    // persistent transport waves, roll a deadline or refund an episode.
    if(state.preHostWriterWait)return state.wave==0 && !budgetBlocked(now);
    const auto fallback=g_inputHostRecoveryFallbackState.load(std::memory_order_acquire);
    if(fallback!=0 && (fallback!=2 || !returned()))return false;
    if(!state.retryAt && !budgetBlocked(now))return true;
    if(!state.retryAt){
        const auto delay=state.wave==0?30000ULL:state.wave==1?60000ULL:120000ULL;
        state.retryAt=now+delay;
        note("backoff","persistent-on-new-wave-scheduled-no-episode-refund");
    }
    if(now<state.retryAt || inputHostRecoveryRollingCountLocked(now)!=0)return false;
    state.wavePermit=true;
    return true;
}
static void queue() noexcept {
    try {
        state.queued=true;
        if(!inputHostQueueLifecycleWork(("runtime-transport-recovery:"+std::to_string(state.serial)).c_str()))
            retire("queue-refused");
    } catch(...) {retire("queue-exception");}
}
} // namespace ymcc::inputHost::runtimeRecovery


// The actual Start creation/validated HELLO sites alone provide this lineage.
// A shared request number observed later is insufficient to adopt any Host.
static void inputHostRuntimeRecoveryBindStartTupleLocked(std::uint64_t request,
        std::uint64_t power,const std::string& persona,bool created) noexcept {
    namespace runtime=ymcc::inputHost::runtimeRecovery;
    try {
        if(!g_inputHostMx.ownedByCurrentThread())return;
        auto& state=runtime::state;
        const auto now=inputHostStartProtectionNow();
        const auto view=g_virtualStartProtection.view(now);
        if(!state.monitoring || !state.spent || state.pending || !state.recoveryRequestId ||
            request!=state.recoveryRequestId || request!=g_inputHostSwitchRecoveryStartRequestId ||
            power!=state.power || power!=g_inputHostSwitchRecoveryStartPower ||
            persona!=state.persona || !g_inputHostSwitchRecoveryStartDeadline ||
            now>=g_inputHostSwitchRecoveryStartDeadline || !view.locked || !view.remainingMs ||
            runtime::stale(false) || !g_inputHostEpoch || g_inputHostRunId.empty() ||
            g_inputHostVirtualIntentWriters.load(std::memory_order_acquire))return;
        if(created) {
            // Start has returned all old resources before assigning this tuple.
            // Recording it grants no PREPARE/Start/return permission or quota.
            if(g_inputHostProcess || g_inputHostPrimaryThread || g_inputHostServerPipe ||
                g_inputHostHello || g_inputHostPrepared || g_inputHostNeutralized || g_inputHostActive ||
                !g_inputHostHostInstanceId.empty() || g_inputHostReleaseQuarantined ||
                g_hidHideVisibilityJournal.active || !runtime::allTargetsReturned())return;
        } else if(state.hostEpoch!=g_inputHostEpoch || state.runId!=g_inputHostRunId ||
            (!state.hostInstanceId.empty() && state.hostInstanceId!=g_inputHostHostInstanceId))return;
        state.hostEpoch=g_inputHostEpoch;state.runId=g_inputHostRunId;
        state.hostInstanceId=g_inputHostHostInstanceId;state.stopInvoked=false;
        runtime::note("successor-bound",created?"same-recovery-period-actual-start-created":"same-created-tuple-validated-hello");
    } catch(...) { /* A failed observation grants no action or replay. */ }
}

// A pre-Host observation may finish its exact old native writer after the
// original player period expires. This only authorizes returning that owner;
// normal queued admission / the existing recovery executor still own Start.
// No new incident, ticket, budget, source read or device action is created here.
static bool inputHostRuntimeRecoveryNativeWriterMayReturnLocked(const std::string& attempt) noexcept {
    namespace runtime=ymcc::inputHost::runtimeRecovery;
    try {
        if(!g_inputHostMx.ownedByCurrentThread() || attempt.empty())return false;
        const auto& state=runtime::state;
        if(!state.preHostWriterWait || !state.pending || state.monitoring || state.launching || state.queued ||
            state.spent || state.wave || state.recoveryRequestId || state.nativeWriterAttempt!=attempt ||
            g_inputHostEpoch || g_inputHostProcess || !g_inputHostRunId.empty() ||
            g_hidHideVisibilityJournal.active || g_inputHostReleaseQuarantined ||
            g_inputHostVirtualIntentWriters.load(std::memory_order_acquire))return false;
        return runtime::stale()==nullptr;
    }catch(...){return false;}
}

// Called only in the actual serial dispatch branch where native ownership
// refused this immutable player Start. A raw/unbound policy item, ordinary OFF,
// missing source or absent Host alone cannot manufacture a recovery incident.
static void inputHostRuntimeRecoveryObserveNativeWait(
        const ymcc::inputHost::queuedPlayerStart::Work& work) noexcept {
    namespace runtime=ymcc::inputHost::runtimeRecovery;
    try {
        if(!work.bound || !ymcc::inputHost::queuedPlayerStart::startReason(work.reason))return;
        std::lock_guard<InputHostMutex> lock(g_inputHostMx);
        if(ymcc::inputHost::queuedPlayerStart::stale(work))return;
        if(inputHostExitFenceRaised() || g_exitRequested.load(std::memory_order_acquire) ||
            g_inputCaptureStop.load(std::memory_order_acquire) ||
            g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire) ||
            g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready ||
            g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) ||
            !g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled ||
            g_inputHostEpoch || g_inputHostProcess || !g_inputHostRunId.empty())return;
        auto& state=runtime::state;
        if(state.pending || state.monitoring || state.launching) {
            if(const char* reason=runtime::stale())runtime::retire(reason);
            else return; // repeated defer never renews observation or retry rights
        }
        std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
        const auto& owner=g_msiNativeForegroundLease;
        if(!owner.active || owner.postHideCycle ||
                !g_msiNativeForegroundCyclePending.load(std::memory_order_acquire) ||
                (!owner.cycleProcess && !owner.nativeRefreshWriter) ||
                runtime::nextSerial==(std::numeric_limits<std::uint64_t>::max)())return;
        const auto now=GetTickCount64();
        // 180s is a background observation ceiling for both observed ~65s OS
        // calls plus original return (about 131s total). It does not lock UI or promise recovery.
        constexpr std::uint64_t observationMaxMs=180000;
        if(now>(std::numeric_limits<std::uint64_t>::max)()-observationMaxMs)return;
        runtime::State offered;
        offered.serial=++runtime::nextSerial;offered.requestId=work.request;
        offered.power=work.power;offered.persona=work.persona;
        offered.intentRevision=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
        offered.demandRevision=g_inputHostRecoveryIntent.revision();
        offered.command="native-writer-deferred-player-start";
        offered.preHostWriterWait=true;offered.pending=true;offered.stopInvoked=true;
        offered.observationUntil=now+observationMaxMs;
        const auto* attempt=owner.nativeRefreshWriter?owner.nativeRefreshWriter->attempt():nullptr;
        offered.nativeWriterAttempt=attempt?attempt->attemptId:"owned-native-cycle";
        state=std::move(offered);
        g_inputHostRuntimeRecoveryPending.store(true,std::memory_order_release);
        runtime::note("observing","real-native-writer-deferred-before-first-host");
    }catch(...) { /* Observation failure grants no command or new retry. */ }
}

// Caller holds the original Host mutex at the classified actual pipe failure.
// Teardown commands and not-yet-final Start verification keep their old owners.
static void inputHostRuntimeRecoveryOfferLocked(const char* command,std::uint64_t sequence,DWORD error) {
    namespace runtime=ymcc::inputHost::runtimeRecovery;
    if(!g_inputHostMx.ownedByCurrentThread() || !command || !runtime::retryable(error) ||
        (std::strcmp(command,"SUBMIT_FRAME")!=0 && std::strcmp(command,"HIDHIDE_QUERY_STATE")!=0))return;
    auto& state=runtime::state;
    // Deduplicate the faulted old Host, never its verified replacement.
    // Start-return binds the replacement for failure cleanup; that binding is
    // not a permanent ban on later incidents in the same long-lived ON intent.
    // A new fault before the normal observer sees the successful recovery final
    // is admitted by that same final below; an unfinished Start remains fenced.
    if(state.pending || state.launching || (!state.recoveryRequestId &&
        state.hostEpoch==g_inputHostEpoch && state.runId==g_inputHostRunId &&
        state.hostInstanceId==g_inputHostHostInstanceId))return;
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    if(!view.requestId || !runtime::operationalTerminal(view) || view.persona=="disabled" ||
        !g_inputHostEpoch || g_inputHostRunId.empty() || g_inputHostHostInstanceId.empty() ||
        !g_inputHostProcess || !g_inputHostHello || !g_inputHostPrepared || !g_inputHostNeutralized ||
        g_inputHostReleaseQuarantined || g_inputHostStartBlocked || !g_inputHostTransportFault ||
        g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) ||
        view.persona!=g_inputHostBoundPersona || view.persona!=inputHostConfiguredPersona() ||
        !g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled ||
        inputHostExitFenceRaised() || g_exitRequested.load(std::memory_order_acquire) ||
        g_inputCaptureStop.load(std::memory_order_acquire) ||
        g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire) ||
        g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready ||
        runtime::nextSerial==(std::numeric_limits<std::uint64_t>::max)())return;
    runtime::State offered;
    offered.serial=++runtime::nextSerial;offered.requestId=view.requestId;
    offered.hostEpoch=g_inputHostEpoch;offered.runId=g_inputHostRunId;
    offered.hostInstanceId=g_inputHostHostInstanceId;offered.persona=view.persona;
    offered.power=currentPowerGeneration();offered.intentRevision=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
    offered.demandRevision=g_inputHostRecoveryIntent.revision();
    offered.command=command;offered.sequence=sequence;offered.error=error;offered.pending=true;
    state=std::move(offered);
    inputHostRuntimeMsiReturnCaptureLocked();
    g_inputHostRuntimeRecoveryPending.store(true,std::memory_order_release);
    runtime::note("offered",view.phase=="success"?"classified-transport-fault-after-final-success":
        view.phase=="failure"?"classified-transport-fault-after-final-failure":
        "classified-transport-fault-after-expired-player-period");
}

static void inputHostRuntimeRecoveryMaintenanceLocked() {
    namespace runtime=ymcc::inputHost::runtimeRecovery;
    auto& state=runtime::state;
    if(!state.pending && !state.monitoring)return;
    const auto now=GetTickCount64();
    // Intent/exit/power cancellation is checked on every worker tick; slow
    // observation never delays OFF. No extra thread/timer/IO is introduced.
    if(const char* reason=runtime::stale()){runtime::retire(reason);return;}
    if(g_inputHostVirtualIntentWriters.load(std::memory_order_acquire))return;
    if(state.launching)return; // Existing serial Start owns the not-yet-returned attempt.
    if(runtime::terminalFailedRecovery(now))return;
    if(state.preHostWriterWait && !state.recoveryRequestId) {
        if(now>=state.observationUntil){runtime::retire("pre-host-observation-budget-ended");return;}
        const auto original=g_virtualStartProtection.view(inputHostStartProtectionNow());
        if(original.locked || (original.phase!="failure" && original.phase!="expired"))return;
        if(runtime::budgetBlocked(now)){runtime::retire("pre-host-shared-recovery-budget-exhausted");return;}
    }
    if(state.monitoring){
        const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
        if(view.locked || (view.phase!="success" && view.phase!="failure" && view.phase!="expired"))return;
        state.monitoring=false;
        if(view.phase=="success"){
            g_inputHostRuntimeRecoveryPending.store(false,std::memory_order_release);
            runtime::note("completed","new-period-success-original-final-unchanged");return;
        }
        if(state.preHostWriterWait) {
            runtime::retire("pre-host-single-recovery-period-failed");return;
        }
        // The failed recovery's OWN final stays failed. Retain ON demand;
        // its original cleanup owner must finish before a distinct new ticket.
        if(runtime::nextSerial==(std::numeric_limits<std::uint64_t>::max)()){
            runtime::retire("serial-exhausted");return;
        }
        // Rotate the dispatch identity between attempts. A delayed token from
        // the prior attempt cannot borrow a later queued continuation.
        state.serial=++runtime::nextSerial;
        state.pending=true;state.spent=false;state.wavePermit=false;
        // Start-return already bound the exact successor tuple when present.
        // If its owner has since cleared it, global durable return still gates
        // the next action; do not adopt an unrelated later tuple here.
        state.stopInvoked=!g_inputHostEpoch;
        state.retryAt=now+(state.wave<=1?60000ULL:120000ULL);
        runtime::note("backoff","recovery-period-failed-retain-current-on-demand");
    }
    if(now<state.nextProbe)return;
    state.nextProbe=now+1000;
    if(const char* reason=runtime::stale()){runtime::retire(reason);return;}
    if(state.queued)return;
    // Mandatory safety Stop is uncharged. UNKNOWN return remains with the
    // existing cleanup owner; waiting cannot mint a new ticket or retry quota.
    if(!state.stopInvoked){
        if(!g_externalPriorityVirtualSuspended.load(std::memory_order_acquire))runtime::queue();
        return;
    }
    if(!runtime::returned() || !runtime::waveReady(now) || runtime::held(now))return;
    if(const char* reason=runtime::stale()){runtime::retire(reason);return;}
    runtime::queue();
}

// Called by the SAME serialized lifecycle dispatcher, without the Host mutex.
static void inputHostPerformRuntimeRecovery(std::uint64_t serial) {
    namespace runtime=ymcc::inputHost::runtimeRecovery;
    std::uint64_t newTicket=0,power=0,deadline=0;
    try {
        {
            std::lock_guard<InputHostMutex> lock(g_inputHostMx);
            auto& state=runtime::state;
            if(!serial || state.serial!=serial || !state.pending || !state.queued || state.spent)return;
            state.queued=false;
            if(const char* reason=runtime::stale()){runtime::retire(reason);return;}
            if(g_inputHostVirtualIntentWriters.load(std::memory_order_acquire))return;
            if(!state.stopInvoked && !runtime::oldTuple()) {
                if(g_inputHostEpoch){runtime::retire("old-tuple-not-owned-for-stop");return;}
                // The normal failed Start/quarantine owner may complete return
                // after this incident queued its safety Stop. An absent tuple
                // is not lost authority: wait for the exact old duty and every
                // actual resource to settle, then continue without replaying
                // that already-completed Stop. Never adopt a successor epoch.
                if(!runtime::returned())return;
                state.stopInvoked=true;
                runtime::note("waiting","existing-return-completed-before-runtime-stop");
            }
            if(!state.stopInvoked) {
                state.stopInvoked=true; // Publish before IO; even an exception cannot replay Stop.
                g_inputHostFaultHoldsStart.store(true,std::memory_order_release);
                runtime::note("cleanup","single-owned-safety-stop");
                inputHostStopLocked("runtime-transport-recovery");
                if(const char* reason=runtime::stale()){runtime::retire(reason);return;}
                runtime::note("waiting","existing-old-target-and-physical-return-proof");
            }
            if(!runtime::returned() || runtime::held(GetTickCount64()))return;
            // Pipe retirement and source snapshots may yield. A return proof
            // does not transfer the old incident to a newer intent/power owner.
            if(const char* reason=runtime::stale()){runtime::retire(reason);return;}
            if(!runtime::waveReady(GetTickCount64()))return;
            // Clear retires the old source admission. A fresh cached sample is
            // not a completed new pre-target scan: request the existing canonical
            // boundary BEFORE consuming a ticket or charging this runtime attempt.
            // MSI establishes its managed DInput route inside ordinary Start;
            // HID source-pending allowance stays the same as that Start path.
            if(!msiClawKnownBoardAuthority() && !inputHostCanCreateTargetWithoutPhysicalSource()) {
                const auto topology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
                const bool boundaryCurrent=g_xinputPreTargetBoundaryScanned.load(std::memory_order_acquire) &&
                    g_xinputBoundaryScanEpoch.load(std::memory_order_acquire)==topology;
                if(!xinputPhysicalConnectedMask() || !boundaryCurrent) {
                    // Same existing canonical BUS full-scan request. Do not
                    // admit/lock/align a source before ordinary Start's writer
                    // fence and cold-return checks; no device write here.
                    g_xinputSlotFullScanEpochStart.store(topology,std::memory_order_release);
                    g_xinputSlotFullScanOnce.store(true,std::memory_order_release);
                    if(const char* reason=runtime::stale()){runtime::retire(reason);}
                    return;
                }
            }
            if(const char* reason=runtime::stale()){runtime::retire(reason);return;}
            if(!runtime::returned() || runtime::held(GetTickCount64()))return;
            if(!runtime::waveReady(GetTickCount64()))return;
            // Consume only after actual return. A simultaneous new player request
            // is never inherited here; the prior final remains unchanged.
            const auto recoveryOwner=state.recoveryRequestId?state.recoveryRequestId:state.requestId;
            ymcc::VirtualStartProtection::Admission admission;
            if(state.preHostWriterWait) {
                const auto original=g_virtualStartProtection.view(inputHostStartProtectionNow());
                if(original.phase=="expired")inputHostRecordSwitchTimeout(original);
                // Logging/return/source ports may have yielded. Recheck OFF,
                // intent and power, then use the observation's BOOT clock for
                // its deadline. The admission clock belongs only to R3 timing.
                const auto admissionClock=inputHostStartProtectionNow();
                if(const char* reason=runtime::stale()){runtime::retire(reason);return;}
                if(!state.observationUntil || GetTickCount64()>=state.observationUntil) {
                    runtime::retire("pre-host-observation-budget-ended");return;
                }
                admission=g_virtualStartProtection.admitRecoveryAfterTerminal(state.requestId,state.persona,admissionClock);
            } else {
                const auto original=g_virtualStartProtection.view(inputHostStartProtectionNow());
                if(original.phase=="success")admission=inputHostStartProtectionConsume(state.persona);
                else {
                    if(original.phase=="expired")inputHostRecordSwitchTimeout(original);
                    // The old failure is immutable. Bind the distinct repair to
                    // this exact terminal request after logging can yield.
                    if(const char* reason=runtime::stale()){runtime::retire(reason);return;}
                    admission=g_virtualStartProtection.admitRecoveryAfterTerminal(
                        recoveryOwner,state.persona,inputHostStartProtectionNow());
                }
            }
            if(!admission.accepted || !admission.created || admission.requestId==recoveryOwner) {
                runtime::retire("distinct-recovery-period-not-admitted");return;
            }
            state.recoveryRequestId=admission.requestId;newTicket=admission.requestId;power=state.power;
            const auto now=inputHostStartProtectionNow();
            deadline=now+g_virtualStartProtection.view(now).remainingMs;
            state.pending=false;state.spent=true;state.monitoring=true;state.launching=true;
            ++state.wave;state.wavePermit=false;state.retryAt=0;state.nextProbe=0;
            g_inputHostRuntimeRecoveryPending.store(true,std::memory_order_release);
            inputHostRecoveryChargeAttemptLocked(GetTickCount64());
            g_inputHostStartBlocked=false;g_inputHostFaultHoldsStart.store(false,std::memory_order_release);
            inputHostStartProtectionLog("accepted","runtime-transport-recovery");
            runtime::note("restart","returned-same-persona-new-bounded-period");
        }
        struct Scope {
            std::uint64_t deadline=g_inputHostSwitchRecoveryStartDeadline,requestId=g_inputHostSwitchRecoveryStartRequestId,
                power=g_inputHostSwitchRecoveryStartPower;
            ~Scope(){g_inputHostSwitchRecoveryStartDeadline=deadline;g_inputHostSwitchRecoveryStartRequestId=requestId;
                g_inputHostSwitchRecoveryStartPower=power;}
        } scope;
        g_inputHostSwitchRecoveryStartDeadline=scope.deadline?(std::min)(scope.deadline,deadline):deadline;
        g_inputHostSwitchRecoveryStartRequestId=newTicket;g_inputHostSwitchRecoveryStartPower=power;
        const bool started=inputHostStart(); // Normal execution/verifier owns the only new final.
        std::lock_guard<InputHostMutex> lock(g_inputHostMx);
        auto& state=runtime::state;
        if(state.recoveryRequestId!=newTicket)return;
        state.launching=false;
        const auto current=g_virtualStartProtection.view(inputHostStartProtectionNow());
        if(current.requestId!=newTicket || current.persona!=state.persona){runtime::retire("start-owner-retired");return;}
        if(g_inputHostEpoch){
            state.hostEpoch=g_inputHostEpoch;state.runId=g_inputHostRunId;
            state.hostInstanceId=g_inputHostHostInstanceId;state.stopInvoked=false;
        }
        runtime::note("start-returned",started?"candidate-awaiting-normal-verification":"normal-start-rejected");
        if(!started)inputHostStartProtectionFinish(newTicket,"failure","runtime-recovery-start-rejected");
    }catch(...) {
        std::lock_guard<InputHostMutex> lock(g_inputHostMx);
        auto& state=runtime::state;
        if(newTicket && state.recoveryRequestId==newTicket){
            state.launching=false;
            if(g_inputHostEpoch){state.hostEpoch=g_inputHostEpoch;state.runId=g_inputHostRunId;
                state.hostInstanceId=g_inputHostHostInstanceId;state.stopInvoked=false;}
            inputHostStartProtectionFinish(newTicket,"failure","runtime-recovery-exception");
            runtime::note("start-returned","exception-awaiting-original-final-and-cleanup");
        }else runtime::retire("runtime-recovery-exception-no-replay");
    }
}

static bool inputHostConsumeRuntimeFaultCleanup(std::uint64_t oldEpoch) {
    namespace runtime=ymcc::inputHost::runtimeRecovery;
    std::lock_guard<InputHostMutex> lock(g_inputHostMx);
    // A matching pending incident owns mandatory Stop, or has already invoked
    // it. Late duplicate frame-fault tokens cannot Stop its successor Host.
    // A late exact-epoch fault token can only clean that epoch. It never
    // acquires authority over a successor, including after attempt rotation.
    return oldEpoch && (oldEpoch!=g_inputHostEpoch ||
        (runtime::state.hostEpoch==oldEpoch &&
         (runtime::state.pending || runtime::state.stopInvoked)) ||
        ymcc::inputHost::recoveryFallback::ownsFaultCleanup(oldEpoch));
}
