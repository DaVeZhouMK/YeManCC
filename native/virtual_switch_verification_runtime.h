#pragma once
#include <cstdint>
#include <limits>
#include <mutex>

// Included after virtual_switch_recovery_runtime.h. Except admission timeout
// logging (which omits Host/inspection facts), the caller owns
// g_inputHostMx. Only cached facts and existing process liveness are observed;
// no lifecycle dispatch, pipe/device I/O, settings write or retry is performed.
namespace ymcc::inputHost::switchVerificationRuntime {
using View = ymcc::VirtualStartProtection::View;
static std::atomic<std::uint64_t> lastFinalRequestId{0}; // Monotonic final observations across admission and worker.
struct Observation {
    std::uint64_t requestId=0,checks=0,healthyChecks=0,excludedChecks=0,knownFaultChecks=0;
    const char* lastState="unobserved";
    const char* lastEvidence="unobserved";
};
static Observation observation; // Existing Host mutex; no per-frame timer or allocation.
static bool psEndpointFaultCurrentLocked(const View& view) {
    const auto& fault=g_inputHostPsEndpointFault;
    return fault.requestId && fault.requestId==view.requestId && view.persona=="dualsense-edge" &&
        fault.hostEpoch==g_inputHostEpoch && fault.power==currentPowerGeneration() &&
        fault.targetRevision==g_psEndpointOwnership.revision();
}
static void clearPsEndpointFaultLocked(std::uint64_t request,std::uint64_t host,
        std::uint64_t power,std::uint64_t revision) {
    const auto& fault=g_inputHostPsEndpointFault;
    if(fault.requestId==request && fault.hostEpoch==host && fault.power==power && fault.targetRevision==revision)
        g_inputHostPsEndpointFault={};
}

// Scalar provenance only, separate from recovery-budget health. An accepted
// foreground-neutral frame is valid local transport evidence; it must not
// broaden the recovery health window's eligibility or reset any budget.
struct AcceptedSource {
    std::uint64_t requestId=0,targetRevision=0,msiModeGeneration=0;
    ymcc::input::recoveryHealth::Context context{};
    ymcc::input::recoveryHealth::Source source{};
};
static AcceptedSource acceptedSource;
// VC-ROG-OFF-BASELINE-20261024-R3 (isolated candidate).
// This is a historical return obligation, NOT current output acceptance. A
// same-Host QUIESCE/RECREATE invalidates acceptedSource but cannot erase the
// XInput source the Host actually used. No Host gains current output proof from it.
// Capture only at existing forget edges: no per-frame work, I/O or retry.
struct RogReturnBaseline {
    std::uint64_t host=0,power=0,requestId=0,sequence=0;
    bool returnPending=false; // Survives failed OFF; cleared only at verified OFF final.
};
static RogReturnBaseline rogReturnBaseline;
// VC-ROG-OFF-EXECUTION-SOURCE-20261024-R1. Retained real receipt only:
// VirtualStartProtection alone owns phase/deadline/final result. No second
// lifecycle, thread, queue, timer, I/O or retry budget is created here.
struct RogOffSuccessReceipt {
    std::uint64_t requestId=0;
    const char* origin="none";
    const char* evidence="unobserved";
};
static RogOffSuccessReceipt rogOffSuccessReceipt; // Same existing Host mutex.

// VC-ROG-OFF-SIGNAL-20261024-R6: an orderly Host shutdown can be followed
// by loss of the ROG Xbox XInput source. Local cleanup alone must not report
// successful OFF in that case. This is a cached signal veto, not permission
// to change a device, retry, or claim Steam/exact-device input recovery.
struct RogOffSignal {
    std::uint64_t requestId=0,power=0,after=0,since=0,continuity=0,incarnation=0;
    unsigned identity=0,topology=0;
    std::uint64_t firstReadSequence=0,lastReadSequence=0,lastReadCompleted=0;
    RogReturnBaseline baseline{}; // Immutable evidence for this exact OFF ticket.
};
static RogOffSignal rogOffSignal; // Same g_inputHostMx owner; no timer or device I/O.
static bool rogXboxSignalScope() noexcept {
    return g_machineIdentity.family==YmccFamily::AsusRogAllyX ||
        g_machineIdentity.family==YmccFamily::AsusRogAlly;
}
static void rememberRogOffSignalLocked() {
    if (!rogXboxSignalScope()) return;
    // Epoch is monotonic for this native process. Forget must run before the
    // original acceptance is cleared, including ON sleep/replacement edges.
    // Discarding actual accepted XInput creates an eventual physical-return
    // obligation, including ON replacement/fault edges before an OFF ticket.
    // It survives Host retirement but can only veto OFF, never prove a new
    // Host's current output, authorize a start, or replay an old player intent.
    if (acceptedSource.requestId && acceptedSource.context.host &&
            acceptedSource.context.host==g_inputHostEpoch && acceptedSource.context.power &&
            acceptedSource.source.route==ymcc::input::recoveryHealth::Route::XInput &&
            acceptedSource.source.sequence && acceptedSource.source.completed) {
        rogReturnBaseline={acceptedSource.context.host,acceptedSource.context.power,
            acceptedSource.requestId,acceptedSource.source.sequence,true};
    }
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    if (!view.locked || view.persona!="disabled" || inputHostConfiguredPersona()!="disabled" ||
            rogOffSignal.requestId==view.requestId || !rogReturnBaseline.host ||
            !rogReturnBaseline.returnPending) return;
    // Historical power is diagnostic only. This obligation must be discharged
    // by NEW reads in the current OFF power generation, never by the baseline.
    // Retain the first arm/after tick across repeated teardown and Clear calls.
    rogOffSignal={view.requestId,currentPowerGeneration(),GetTickCount64()};
    rogOffSignal.baseline=rogReturnBaseline;
}
// VC-ROG-OFF-READ-SPAN-20261024-R1: a long read is not sustained input.
// Canonical freshness permits up to1500ms since read start. Anchor coverage
// at the FIRST COMPLETED read and require a newer sequence, so one1200ms
// read (or repeat observations of it) cannot satisfy the one-second window.
static bool rogOffSignalObservedLocked(const View& view,const char** evidence=nullptr) {
    const auto result=[&](bool ok,const char* reason){if(evidence)*evidence=reason;return ok;};
    const auto resetSpan=[] {
        rogOffSignal.since=0;rogOffSignal.firstReadSequence=0;
        rogOffSignal.lastReadSequence=0;rogOffSignal.lastReadCompleted=0;
    };
    if (rogXboxSignalScope() && g_msiNativeForegroundCyclePending.load(std::memory_order_acquire)) {
        resetSpan();
        return result(false,"disabled-rog-foreground-helper-pending");
    }
    if (!rogXboxSignalScope() || rogOffSignal.requestId!=view.requestId)
        return result(true,"disabled-cleanup-proven");
    const auto source=inputHostRecoveryHealthLatestSourceLocked();
    const auto power=currentPowerGeneration();
    const auto topology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
    const auto identity=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire);
    if (power!=rogOffSignal.power || !source.live || !source.stable ||
            source.route!=ymcc::input::recoveryHealth::Route::XInput ||
            !source.incarnation || !source.continuity ||
            source.continuity==std::numeric_limits<std::uint64_t>::max() ||
            source.identity!=identity || source.started<rogOffSignal.after ||
            !ymcc::input::recoveryHealth::fresh(source,GetTickCount64(),power,topology)) {
        resetSpan();
        return result(false,"disabled-expected-rog-xinput-unobserved");
    }
    if (!rogOffSignal.since || rogOffSignal.continuity!=source.continuity ||
            rogOffSignal.incarnation!=source.incarnation || rogOffSignal.identity!=identity ||
            rogOffSignal.topology!=topology) {
        rogOffSignal.since=source.completed;
        rogOffSignal.firstReadSequence=source.sequence;
        rogOffSignal.continuity=source.continuity;rogOffSignal.incarnation=source.incarnation;
        rogOffSignal.identity=identity;rogOffSignal.topology=topology;
    } else if (source.sequence<rogOffSignal.lastReadSequence ||
            source.completed<rogOffSignal.lastReadCompleted ||
            (source.sequence==rogOffSignal.lastReadSequence && source.completed!=rogOffSignal.lastReadCompleted)) {
        resetSpan();
        return result(false,"disabled-expected-rog-xinput-read-regressed");
    }
    rogOffSignal.lastReadSequence=source.sequence;
    rogOffSignal.lastReadCompleted=source.completed;
    const bool advanced=source.sequence>rogOffSignal.firstReadSequence;
    const bool settled=advanced && source.completed>=rogOffSignal.since &&
        source.completed-rogOffSignal.since>=1000;
    return result(settled,settled ? "disabled-expected-rog-xinput-observed" :
        (!advanced ? "disabled-expected-rog-xinput-awaiting-next-read" : "disabled-expected-rog-xinput-settling"));
}
// Only the unique successful final may retire this obligation. A transient
// good read during inspection followed by signal loss must retain it. Expiry,
// failure, another ticket, or cleanup alone cannot erase the missing signal.
static void finishRogOffSignalLocked(const View& view,bool success) noexcept {
    if (!success || !rogXboxSignalScope() || view.persona!="disabled" ||
            rogOffSignal.requestId!=view.requestId) return;
    const auto& proof=rogOffSignal.baseline;
    if (rogReturnBaseline.returnPending && rogReturnBaseline.host==proof.host &&
            rogReturnBaseline.power==proof.power && rogReturnBaseline.requestId==proof.requestId &&
            rogReturnBaseline.sequence==proof.sequence) rogReturnBaseline={};
}
static ymcc::input::recoveryHealth::Context acceptedContextLocked() {
    auto context=inputHostRecoveryHealthContextLocked(false);
    context.intent=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
    context.allowed=context.allowed && g_inputHostHello &&
        g_inputHostRequestedPersonaMatchesBound.load(std::memory_order_acquire);
    return context;
}
static bool readCurrent(const ymcc::input::recoveryHealth::Source& source,
        const ymcc::input::recoveryHealth::Context& context,std::uint64_t now) noexcept {
    return context.allowed && context.host && context.power && source.live && source.stable &&
        source.incarnation && source.continuity && source.continuity!=std::numeric_limits<std::uint64_t>::max() &&
        source.identity==context.identity &&
        ymcc::input::recoveryHealth::fresh(source,now,context.power,context.topology);
}
static AcceptedSource captureAcceptedSourceLocked(bool physicalSourceAdmitted) {
    const auto ticket=g_inputHostSwitchVerificationPending.load(std::memory_order_acquire);
    if (!ticket) return {}; // Ordinary steady output adds only this scalar flag read.
    const auto source=inputHostRecoveryHealthSource(t_commonFrameSource,physicalSourceAdmitted);
    const auto mode=source.route==ymcc::input::recoveryHealth::Route::MsiDinput && t_commonFrameSource
        ? t_commonFrameSource->readMsiModeGeneration : 0;
    return {ticket,g_psEndpointOwnership.revision(),mode,acceptedContextLocked(),source};
}
static bool acceptedModeCurrentLocked(const AcceptedSource& value) noexcept {
    // MSI mode generation may change without a new source incarnation or
    // continuity. It is a read/ACK binding token, never an OEM mode write.
    return value.source.route!=ymcc::input::recoveryHealth::Route::MsiDinput ||
        value.msiModeGeneration==g_msiFeedbackModeGeneration.load(std::memory_order_acquire);
}
static bool acceptCurrentSourceLocked(const AcceptedSource& submitted,bool eligible) {
    if (!submitted.requestId || !eligible ||
            g_inputHostSwitchVerificationPending.load(std::memory_order_acquire)!=submitted.requestId) return false;
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    if (view.requestId!=submitted.requestId || !view.locked ||
            (view.phase!="pending" && view.phase!="verifying")) return false;
    // Settings admission publishes the new ticket before the BUS reconciles
    // its requested persona. An in-flight old Host ACK must not borrow that
    // ticket and start the new persona's ten-second inspection early.
    if (view.persona!=inputHostBoundPersonaValue() || view.persona!=inputHostConfiguredPersona() ||
            !g_inputHostRequestedPersonaMatchesBound.load(std::memory_order_acquire)) return false;
    const auto context=acceptedContextLocked();
    const auto latest=inputHostRecoveryHealthLatestSourceLocked();
    const auto now=GetTickCount64();
    if (!acceptedModeCurrentLocked(submitted) || submitted.targetRevision!=g_psEndpointOwnership.revision() || !(submitted.context==context) || !readCurrent(submitted.source,context,now) ||
            !readCurrent(latest,context,now) ||
            !ymcc::input::recoveryHealth::sameSource(submitted.source,latest) ||
            submitted.source.continuity!=latest.continuity ||
            submitted.source.sequence>latest.sequence || submitted.source.completed>latest.completed) return false;
    acceptedSource=submitted; // Only the actual accepted report consumer calls this.
    return true;
}
static bool acceptedSourceCurrentLocked(const View& view,const ymcc::input::recoveryHealth::Source& latest) {
    const auto context=acceptedContextLocked();
    // Acceptance belongs to a lineage, not changing packet contents. Continued
    // fresh reads with the same lineage may be idle; they need no forced IO.
    return acceptedModeCurrentLocked(acceptedSource) && acceptedSource.requestId==view.requestId && acceptedSource.targetRevision==g_psEndpointOwnership.revision() && acceptedSource.context==context &&
        acceptedSource.source.sequence && acceptedSource.source.completed &&
        ymcc::input::recoveryHealth::sameSource(acceptedSource.source,latest) &&
        acceptedSource.source.continuity==latest.continuity &&
        acceptedSource.source.sequence<=latest.sequence && acceptedSource.source.completed<=latest.completed;
}


static void finalLogLocked(const View& view, const char* outcome, const char* reason, bool hostFacts=true) noexcept {
    if (!view.requestId) return;
    auto previous=lastFinalRequestId.load(std::memory_order_acquire);
    do { if (previous>=view.requestId) return; }
    while(!lastFinalRequestId.compare_exchange_weak(previous,view.requestId,std::memory_order_acq_rel));
    try {
        const bool observed=hostFacts && observation.requestId==view.requestId;
        appendNativeLifecycleLog("input-host-switch-verification", {
            {"stage","final"},{"requestId",view.requestId},{"persona",view.persona},
            {"lifecycleVersion",ymcc::VirtualStartProtection::lifecycleVersion},
            {"fallbackAttempts",view.fallbackAttempts},
            {"executionInspectionStartedElapsedMs",view.inspectionStartedElapsedMs},
            {"executionInspectionDeadlineElapsedMs",view.inspectionDeadlineElapsedMs},
            {"candidateOutcome",view.candidateOutcome},{"candidateIsFinal",false},{"inspectionFactsAvailable",hostFacts},
            {"outcome",outcome},{"finalReason",reason},{"elapsedMs",view.elapsedMs},
            {"hostEpoch",hostFacts?json(g_inputHostEpoch):json(nullptr)},{"powerGeneration",currentPowerGeneration()},
            {"inspectionChecks",hostFacts?json(observed?observation.checks:0):json(nullptr)},
            {"healthyChecks",hostFacts?json(observed?observation.healthyChecks:0):json(nullptr)},
            {"excludedChecks",hostFacts?json(observed?observation.excludedChecks:0):json(nullptr)},
            {"knownFaultChecks",hostFacts?json(observed?observation.knownFaultChecks:0):json(nullptr)},
            {"lastInspectionState",observed?observation.lastState:"unobserved"},
            {"lastInspectionEvidence",observed?observation.lastEvidence:"unobserved"},
            {"acceptedSourceRequestId",hostFacts?json(acceptedSource.requestId):json(nullptr)},
            {"acceptedSourceHostEpoch",hostFacts?json(acceptedSource.context.host):json(nullptr)},
            {"acceptedSourceContinuity",hostFacts?json(acceptedSource.source.continuity):json(nullptr)},
            {"acceptedSourceMsiModeGeneration",hostFacts?json(acceptedSource.msiModeGeneration):json(nullptr)},
            {"acceptedSourceSequence",hostFacts?json(acceptedSource.source.sequence):json(nullptr)},
            {"offSignalPolicy","VC-ROG-OFF-SIGNAL-20261024-R6"},
            {"offSignalBaselinePolicy","VC-ROG-OFF-BASELINE-20261024-R3"},
            {"offSignalReadSpanPolicy","VC-ROG-OFF-READ-SPAN-20261024-R1"},
            {"offSignalExecutionPolicy","VC-ROG-OFF-EXECUTION-SOURCE-20261024-R1"},
            {"offSignalObservationPower",hostFacts && rogOffSignal.requestId==view.requestId?json(rogOffSignal.power):json(nullptr)},
            {"offSignalObservationAfterTick",hostFacts && rogOffSignal.requestId==view.requestId?json(rogOffSignal.after):json(nullptr)},
            {"offSignalFirstReadSequence",hostFacts && rogOffSignal.requestId==view.requestId?json(rogOffSignal.firstReadSequence):json(nullptr)},
            {"offSignalLastReadSequence",hostFacts && rogOffSignal.requestId==view.requestId?json(rogOffSignal.lastReadSequence):json(nullptr)},
            {"offSignalLastReadCompleted",hostFacts && rogOffSignal.requestId==view.requestId?json(rogOffSignal.lastReadCompleted):json(nullptr)},
            {"offSignalBaselineHostEpoch",hostFacts && rogOffSignal.requestId==view.requestId?json(rogOffSignal.baseline.host):json(nullptr)},
            {"offSignalBaselinePower",hostFacts && rogOffSignal.requestId==view.requestId?json(rogOffSignal.baseline.power):json(nullptr)},
            {"offSignalBaselineRequestId",hostFacts && rogOffSignal.requestId==view.requestId?json(rogOffSignal.baseline.requestId):json(nullptr)},
            {"offSignalBaselineSequence",hostFacts && rogOffSignal.requestId==view.requestId?json(rogOffSignal.baseline.sequence):json(nullptr)},
            {"offSignalReturnDutyPending",hostFacts?json(rogReturnBaseline.returnPending):json(nullptr)},
            {"offSignalCheckRequired",hostFacts && rogOffSignal.requestId==view.requestId},
            {"offSignalStableSince",hostFacts && rogOffSignal.requestId==view.requestId?json(rogOffSignal.since):json(nullptr)},
            {"physicalReturnVerified",false},{"gameInputVerified",false},{"resultScope","local-switch-verification"}});
    } catch (...) { /* Optional diagnostics never repeat or change the result. */ }
}

static bool busyLocked() {
    // The foreground owner shares HostMx with this consumer. Its live helper
    // may outlast local Host cleanup; OFF cannot finalize while it can still
    // mutate the source. Polling/retirement remains with its original owner,
    // and the original R3 hard deadline is never renewed here.
    if(inputHostConfiguredPersona()=="disabled" && rogXboxSignalScope() &&
            g_msiNativeForegroundCyclePending.load(std::memory_order_acquire))return true;
    if(inputHostWakeExecutionPending(g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId))return true;
    if(g_inputHostPsEndpointFault.waitingForAdmission &&
        psEndpointFaultCurrentLocked(g_virtualStartProtection.view(inputHostStartProtectionNow())))return true;
    // Only an active return owner delays OFF inspection. A dormant/failed
    // obligation still vetoes success below, but is not itself busy work.
    if(inputHostConfiguredPersona()=="disabled" && msiClawKnownBoardAuthority() &&
            (g_msiOwnedReturnWorking.load(std::memory_order_acquire) ||
             g_msiTerminalReturnPending.load(std::memory_order_acquire)))return true;
    if (g_inputHostSwitchExecutionReturnPending.load(std::memory_order_acquire) ||
            g_inputHostLifecycleWorkRequested.load(std::memory_order_acquire) ||
            g_inputHostLifecycleWorkInFlight.load(std::memory_order_acquire) ||
            g_inputHostSwitchRecoveryPending.load(std::memory_order_acquire) ||
            g_inputHostRecoveryPending.load(std::memory_order_acquire) ||
            g_inputHostTupleHealPending.load(std::memory_order_acquire) ||
            g_ymccSlotHandoffPlanActive.load(std::memory_order_acquire) ||
            g_physicalReleaseWindowDutyActive.load(std::memory_order_acquire)) return true;
    std::lock_guard<std::mutex> queueLock(g_inputHostLifecycleWorkMx);
    return !g_inputHostLifecycleWorkQueue.empty();
}

static bool pausedLocked() {
    // Expected suppression is neither a fault nor permission to retry. Keep
    // the original deadline during recording, handoff or power pauses.
    // YMCC focus owns legitimate neutral output; its absence of game actions
    // is neither a fault nor a reason to block cached-source verification.
    return g_shortcutRecordingActive.load(std::memory_order_acquire) ||
        physicalReleaseWindowActive() || g_externalPriorityVirtualSuspended.load(std::memory_order_acquire) ||
        g_powerLifecycle.load(std::memory_order_acquire) != PowerLifecycle::Ready ||
        !g_inputReady.load(std::memory_order_acquire) || !sgVirtualWriterAllowedNow() ||
        g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire) ||
        inputHostExitFenceRaised() || g_exitRequested.load(std::memory_order_acquire) ||
        g_inputCaptureStop.load(std::memory_order_acquire);
}

static bool disabledCleanLocked() {
    return inputHostDisabledCleanupProvenLocked();
}

static bool currentOutputProvenLocked(const View& view, const char** missing=nullptr) {
    const auto absent=[&](const char* reason){if(missing)*missing=reason;return false;};
    if(inputHostTupleFaultCurrentLocked(view))return absent("current-owned-tuple-receipt-fault");
    const auto power=currentPowerGeneration();
    if(psEndpointFaultCurrentLocked(view))return absent("owned-ps-endpoint-known-absent");
    if (!g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled) return absent("intent-inactive");
    if (!inputHostProcessAliveLocked()) return absent("host-liveness-unproven");
    if (!g_inputHostHello || !g_inputHostPrepared || !g_inputHostNeutralized || !g_inputHostActive || !g_inputHostEpoch)
        return absent("host-publication-not-complete");
    if (inputHostBoundPersonaValue()!=view.persona || !g_inputHostRequestedPersonaMatchesBound.load(std::memory_order_acquire))
        return absent("current-persona-binding-unproven");
    if (g_inputHostTransportFault || g_inputHostPipeRetired) return absent("host-transport-not-current");
    if (g_inputHostReleaseQuarantined || g_inputHostStartBlocked || g_inputHostFaultHoldsStart.load(std::memory_order_acquire) ||
            g_inputHostRecoveryIntentReset.load(std::memory_order_acquire)) return absent("return-or-rearm-pending");
    if (!(g_inputHostBoundPowerGeneration==power ||
            ymcc::sleeprecovery::inputBindingAllowed(g_inputHostBoundPowerGeneration,power,
                inputHostWakeBindingProofLocked()))) return absent("power-binding-unproven");
    const auto source=inputHostRecoveryHealthLatestSourceLocked();
    if (!source.live) return absent("physical-source-unavailable");
    if (!source.stable) return absent("physical-source-not-stable");
    if (!source.incarnation || !source.continuity || source.continuity==std::numeric_limits<std::uint64_t>::max())
        return absent("source-lineage-unproven");
    if (source.identity!=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire)) return absent("source-identity-changed");
    if (!ymcc::input::recoveryHealth::fresh(source,GetTickCount64(),power,g_xinputTopologyEpoch.load(std::memory_order_acquire)))
        return absent("source-not-current-and-fresh");
    if (source.route==ymcc::input::recoveryHealth::Route::MsiDinput && acceptedSource.requestId==view.requestId &&
            !acceptedModeCurrentLocked(acceptedSource)) return absent("current-msi-mode-accepted-report-unproven");
    if (!acceptedSourceCurrentLocked(view,source)) return absent("current-source-accepted-report-unproven");
    if (missing)*missing="current-host-and-source-proven";
    return true;
}
static void logRogOffExecutionLocked(const View& view,const char* stage,const char* evidence) noexcept {
    try { appendNativeLifecycleLog("input-host-rog-off-execution",{
        {"policy","VC-ROG-OFF-EXECUTION-SOURCE-20261024-R1"},
        {"stage",stage},{"evidence",evidence},{"requestId",view.requestId},
        {"receiptRequestId",rogOffSuccessReceipt.requestId},{"receiptOrigin",rogOffSuccessReceipt.origin},
        {"phase",view.phase},{"elapsedMs",view.elapsedMs},{"remainingMs",view.remainingMs},
        {"powerGeneration",currentPowerGeneration()},{"observationPower",rogOffSignal.power},
        {"observationRequestId",rogOffSignal.requestId},{"observationAfterTick",rogOffSignal.after},
        {"physicalDeviceIdentityVerified",false},{"gameInputVerified",false},
        {"candidateIsFinal",false},{"role","normal-execution-source-wait"},
        {"newDeviceOperation",false},{"fallbackCharged",false},{"deadlineRenewed",false}}); } catch(...) {}
}
static bool rogOffExecutionReceiptReadyLocked(const View& view) {
    rememberRogOffSignalLocked();
    const bool clean=disabledCleanLocked();
    const bool paused=pausedLocked();
    const bool busy=busyLocked();
    const char* evidence="disabled-cleanup-pending";
    // Keep the existing helper/source reset gates observable even while busy.
    const bool source=clean && rogOffSignalObservedLocked(view,&evidence);
    if(!clean)evidence="disabled-cleanup-pending";
    else if(paused)evidence="expected-pause";
    else if(busy)evidence="execution-owner-busy";
    if(std::strcmp(rogOffSuccessReceipt.evidence,evidence)!=0){
        rogOffSuccessReceipt.evidence=evidence;
        logRogOffExecutionLocked(view,"awaiting-execution-completion",evidence);
    }
    return clean && !paused && !busy && source;
}
static void progressRogOffExecutionReceiptLocked(const View& view) {
    if(!rogOffSuccessReceipt.requestId)return;
    if(view.requestId!=rogOffSuccessReceipt.requestId || !view.locked || view.phase!="pending" ||
            view.persona!="disabled" || inputHostConfiguredPersona()!="disabled" || !rogXboxSignalScope()) {
        logRogOffExecutionLocked(view,"retired","receipt-request-or-execution-retired");
        rogOffSuccessReceipt={};return;
    }
    if(!rogOffExecutionReceiptReadyLocked(view))return;
    // Tc belongs to the SAME actual receipt plus current source, with the
    // original admission deadline. A race/expiry is refused by the R3 owner.
    const auto ticket=rogOffSuccessReceipt.requestId;
    logRogOffExecutionLocked(view,"execution-complete","local-cleanup-and-current-xinput-observation-proven");
    rogOffSuccessReceipt={};
    inputHostStartProtectionFinish(ticket,"success","rog-off-source-execution-complete");
}
} // namespace ymcc::inputHost::switchVerificationRuntime

static bool inputHostRogOffExecutionDefersSuccess(std::uint64_t ticket,const char* origin) {
    using namespace ymcc::inputHost::switchVerificationRuntime;
    // Both actual OFF success producers own this mutex: Stop's RAII result and
    // the cold-return executor. Other receipt kinds keep their original route.
    if(!ticket || !origin || !g_inputHostMx.ownedByCurrentThread() || !rogXboxSignalScope())return false;
    const bool cold=std::strcmp(origin,"cold-owned-return-execution-complete")==0;
    if(!cold && std::strcmp(origin,"stop-terminal-result")!=0)return false;
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    if(view.requestId!=ticket || !view.locked || view.phase!="pending" ||
            view.persona!="disabled" || inputHostConfiguredPersona()!="disabled")return false;
    rememberRogOffSignalLocked();
    if(!rogReturnBaseline.returnPending && rogOffSignal.requestId!=ticket)return false;
    if(rogOffSuccessReceipt.requestId!=ticket)
        rogOffSuccessReceipt={ticket,cold?"cold-owned-return-execution-complete":"stop-terminal-result","unobserved"};
    if(rogOffExecutionReceiptReadyLocked(view)){
        logRogOffExecutionLocked(view,"execution-complete","local-cleanup-and-current-xinput-observation-proven");
        rogOffSuccessReceipt={};return false;
    }
    return true;
}

static void inputHostRecordSwitchTimeout(const ymcc::VirtualStartProtection::View& view) noexcept {
    if (view.phase=="expired")
        ymcc::inputHost::switchVerificationRuntime::finalLogLocked(view,"failure","original-admission-deadline-expired",false);
}
static void inputHostSwitchVerificationMaintenanceLocked() {
    using namespace ymcc::inputHost::switchVerificationRuntime;
    auto view = g_virtualStartProtection.view(inputHostStartProtectionNow());
    // This is still ordinary execution. Consume only an actual retained local
    // success receipt; empty state alone cannot fabricate an execution result.
    progressRogOffExecutionReceiptLocked(view);
    view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    if (!view.locked) {
        auto expected=view.requestId;
        g_inputHostSwitchVerificationPending.compare_exchange_strong(expected,0,std::memory_order_acq_rel);
        if (view.phase == "expired") finalLogLocked(view,"failure","original-admission-deadline-expired");
        return;
    }
    // Inspect throughout the verification window on the existing worker.
    // Idle packet contents and foreground neutral are not failure predicates.
    if (!view.verificationPending || inputHostConfiguredPersona()!=view.persona) return;
    // Empty-Host / cold-return execution can complete without calling Stop or
    // Forget. Bind the unresolved return obligation to THIS admitted OFF only,
    // at its existing verification owner; do not replay any old ticket/clock.
    if (view.persona=="disabled" && rogXboxSignalScope() && rogReturnBaseline.returnPending &&
            rogOffSignal.requestId!=view.requestId) rememberRogOffSignalLocked();
    if (observation.requestId!=view.requestId) observation={view.requestId};
    ++observation.checks;
    const bool expectedPause=pausedLocked();
    bool healthy=false;
    const bool knownFault=!expectedPause && view.persona=="steamdeck" &&
        g_inputHostTransportFault &&
        ymcc::inputHost::switchRecoveryRuntime::retryable(g_inputHostSwitchRecoveryTransportError);
    const char* observedState="evidence-unknown";
    const char* observedEvidence="expected-pause";
    if (expectedPause) { ++observation.excludedChecks;observedState="expected-paused"; }
    else if (knownFault) {
        ++observation.knownFaultChecks;observedState="classified-transport-fault";observedEvidence="actual-retryable-pipe-error";
        // This is a diagnosed fault, never a timer/idle-triggered rebuild.
        inputHostSwitchRecoveryOfferLocked(view.requestId,currentPowerGeneration(),
            g_inputHostSwitchRecoveryTransportError);
        if (!g_inputHostSwitchRecoveryPending.load(std::memory_order_acquire))
            inputHostStartProtectionFinish(view.requestId,"failure","verification-known-fault-no-retry");
    } else if (inputHostTupleFaultCurrentLocked(view)) {
        ++observation.knownFaultChecks;observedState="classified-tuple-receipt-fault";
        observedEvidence="actual-current-sequence-stale-or-gap";
    } else if (psEndpointFaultCurrentLocked(view)) {
        ++observation.knownFaultChecks;observedState="classified-owned-endpoint-fault";
        observedEvidence="complete-current-owned-ps-endpoint-absence";
    } else {
        if (view.persona=="disabled") {
            const bool clean=disabledCleanLocked();
            healthy=clean && rogOffSignalObservedLocked(view,&observedEvidence);
            if (!clean) observedEvidence="disabled-cleanup-pending";
        } else healthy=currentOutputProvenLocked(view,&observedEvidence);
        if (healthy) { ++observation.healthyChecks;observedState="current-local-chain-healthy"; }
    }
    if (std::strcmp(observation.lastState,observedState)!=0 || std::strcmp(observation.lastEvidence,observedEvidence)!=0) {
        observation.lastState=observedState;observation.lastEvidence=observedEvidence;
        try {appendNativeLifecycleLog("input-host-switch-verification",{
            {"stage","inspection"},{"requestId",view.requestId},{"state",observedState},{"evidence",observedEvidence},
            {"lifecycleVersion",ymcc::VirtualStartProtection::lifecycleVersion},{"fallbackAttempts",view.fallbackAttempts},
            {"checks",observation.checks},{"verificationRemainingMs",view.verificationRemainingMs},
            {"remainingMs",view.remainingMs},{"candidateIsFinal",false},{"replay",false}});}catch(...){}
    }
    // Time passing never substitutes for actual inspection or completed cleanup.
    if (view.verificationRemainingMs || expectedPause || busyLocked()) return;
    const auto inspected=g_virtualStartProtection.view(inputHostStartProtectionNow());
    if (inspected.requestId!=view.requestId || !inspected.verificationPending) return;
    const char* outcome = "failure";
    const char* reason = "candidate-failure";
    if (view.persona == "disabled") {
        const bool clean = disabledCleanLocked();
        const char* signalEvidence="disabled-cleanup-pending";
        const bool signal=clean && rogOffSignalObservedLocked(view,&signalEvidence);
        outcome = clean && signal ? "success" : "failure";
        reason = !clean ? "disabled-local-cleanup-unproven" :
            (!signal ? signalEvidence : (rogOffSignal.requestId==view.requestId
                ? "disabled-cleanup-and-rog-xinput-observed" : "disabled-local-cleanup-verified"));
    } else if (currentOutputProvenLocked(view)) {
        // A candidate failure is provisional. A current accepted report may
        // later prove this same request; there is still only one final outcome.
        outcome = "success";
        reason = "current-intent-host-and-fresh-source-verified";
    } else if (inspected.candidateOutcome != "failure") {
        // Missing/neutral/stale evidence alone never diagnoses a fault or rebuild.
        return;
    }
    const auto current = g_virtualStartProtection.view(inputHostStartProtectionNow());
    if (current.requestId != view.requestId || current.persona != view.persona ||
            current.candidateOutcome != inspected.candidateOutcome ||
            inputHostConfiguredPersona() != view.persona) return;
    if (!g_virtualStartProtection.finishVerification(view.requestId,outcome,inputHostStartProtectionNow())) return;
    auto expected=view.requestId;
    g_inputHostSwitchVerificationPending.compare_exchange_strong(expected,0,std::memory_order_acq_rel);
    finishRogOffSignalLocked(current,std::strcmp(outcome,"success")==0);
    finalLogLocked(current,outcome,reason);
}

static void inputHostSwitchVerificationForgetAcceptedLocked() noexcept {
    // Capture before the first teardown clears acceptedSource. Failure to
    // inspect this optional expectation must never skip clearing acceptance.
    try {ymcc::inputHost::switchVerificationRuntime::rememberRogOffSignalLocked();} catch (...) {}
    ymcc::inputHost::switchVerificationRuntime::acceptedSource={};
}
