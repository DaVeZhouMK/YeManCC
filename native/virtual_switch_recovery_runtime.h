#pragma once
#include <cstdint>
#include <exception>
#include <limits>
#include <mutex>
#include <string>

// Included after inputHostStart. State/prototypes belong to main.cpp. All
// request fields and shared budgets use g_inputHostMx; only Pending is atomic.
namespace ymcc::inputHost::switchRecoveryRuntime {
using Tick = std::uint64_t;
static bool executionInspectionCompleteLocked(Tick now) {
    return g_virtualStartProtection.executionInspectionComplete(now);
}
static bool retryable(DWORD error) noexcept {
    return error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA ||
        error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_SEM_TIMEOUT ||
        error == ERROR_TIMEOUT || error == WAIT_TIMEOUT;
}
static void logLocked(const char* stage, const char* reason,
        const InputHostSwitchRecoveryRequest& request, bool startReturned = false, const char* exceptionDetail = nullptr) noexcept {
    try {
        const auto view = g_virtualStartProtection.view(inputHostStartProtectionNow());
        appendNativeLifecycleLog("input-host-switch-recovery", {
            {"stage",stage},{"reason",reason},{"requestId",request.requestId},
            {"lifecycleVersion",ymcc::VirtualStartProtection::lifecycleVersion},
            {"fallbackAttempts",view.requestId==request.requestId?json(view.fallbackAttempts):json(nullptr)},
            {"executionInspectionRemainingMs",view.requestId==request.requestId?json(view.verificationRemainingMs):json(nullptr)},
            {"powerGeneration",request.power},{"hostEpoch",request.hostEpoch},
            {"deadline",request.deadline},{"error",static_cast<unsigned long>(request.error)},
            {"faultKind",request.tupleFault?"tuple-receipt":"transport"},{"persona",request.persona},
            {"pending",request.pending},{"queued",request.queued},{"spent",request.spent},
            {"awaitingCleanup",request.awaitingCleanup},{"stopInvoked",request.stopInvoked},
            {"executionCleanup",request.executionCleanup},
            {"episodeAttempts",g_inputHostRecoveryEpisodeAttempts.load(std::memory_order_acquire)},
            {"rollingCount",inputHostRecoveryRollingCountLocked(GetTickCount64())},
            {"startReturned",startReturned},{"exceptionDetail",exceptionDetail?exceptionDetail:""},{"gameInputVerified",false},
            {"requestPhase",view.requestId == request.requestId ? view.phase : "retired"},
            {"resultScope","cycle-observation-only"}});
    } catch (...) { /* Optional observation cannot repeat or interrupt lifecycle work. */ }
}
// Report a failure candidate through main's shared verification wrapper only.
// This runtime never calls VirtualStartProtection::finish or unlocks a request;
// the common 10s verification / original 30s cap gate owns the sole final result.
static void failureCandidateLocked(Tick ticket) noexcept {
    try {
        const auto view = g_virtualStartProtection.view(inputHostStartProtectionNow());
        if (ticket && view.requestId == ticket &&
                (view.phase == "pending" || view.phase == "verifying"))
            inputHostStartProtectionFinish(ticket,"failure","switch-recovery-candidate-failure");
    } catch (...) {}
}
static void retireLocked(const char* stage, const char* reason) noexcept {
    auto& request = g_inputHostSwitchRecovery;
    request.pending = false;
    request.queued = false;
    request.awaitingCleanup = false;
    request.spent = true; // Sticky for this ticket, including terminal refusals.
    g_inputHostSwitchRecoveryPending.store(false,std::memory_order_release);
    failureCandidateLocked(request.requestId);
    logLocked(stage,reason,request);
}
static const char* staleLocked(InputHostSwitchRecoveryRequest& request, Tick now,
        bool checkHostEpoch = true) {
    const auto view = g_virtualStartProtection.view(now);
    if (!request.requestId || view.requestId != request.requestId) return "request-retired";
    if (view.phase != "pending" && view.phase != "verifying") return "request-final-or-expired";
    if (!view.locked || !view.remainingMs || now >= request.deadline)
        return "request-deadline-or-cancellation";
    // Ten seconds is the inspection window, not a second retry hard cap.
    // An already diagnosed repair still belongs to the original 30s budget.
    if (view.persona != request.persona || inputHostConfiguredPersona() != request.persona ||
            (!request.tupleFault && request.persona != "steamdeck") ||
            !g_realStickTest.enabled || (!g_realStickTest.virtualTargetEnabled &&
                !g_externalPriorityVirtualSuspended.load(std::memory_order_acquire)))
        return "steamdeck-intent-retired";
    if (inputHostExitFenceRaised() || g_exitRequested.load(std::memory_order_acquire) ||
            g_inputCaptureStop.load(std::memory_order_acquire)) return "exit-or-capture-stop";
    if (currentPowerGeneration() != request.power) return "power-generation-retired";
    if (checkHostEpoch && g_inputHostEpoch != request.hostEpoch) return "host-epoch-retired";
    return nullptr;
}
static bool cleanLocked() noexcept {
    return !g_inputHostProcess && !g_inputHostHello && !g_inputHostPrepared &&
        !g_inputHostNeutralized && !g_inputHostActive && !g_hidHideVisibilityJournal.active &&
        !g_inputHostReleaseQuarantined && !g_inputHostStartBlocked;
}
// A verification fault may still own its target/journal. Only this exact current
// SteamDeck tuple may be stopped; identity/permission/asset failures are excluded
// by the classified Offer producer, never inferred from feed/UI observations.
static bool recoverableOwnedStateLocked(Tick power,bool tupleFault=false) noexcept {
    return cleanLocked() || (power && currentPowerGeneration() == power &&
        g_inputHostProcess && g_inputHostHello && !g_inputHostRunId.empty() && g_inputHostEpoch &&
        (tupleFault ? g_inputHostBoundPersona == inputHostConfiguredPersona() : g_inputHostBoundPersona == "steamdeck") &&
        g_inputHostBoundPowerGeneration == power && (tupleFault || g_inputHostTransportFault) &&
        !g_inputHostReleaseQuarantined && !g_inputHostStartBlocked);
}
// now is system uptime (GetTickCount64), never the injectable guard clock.
static bool heldLocked(Tick now, bool requireSource = true, bool checkCooldown = true) {
    // These are expected pauses/unknown evidence, never a new fault or retry.
    if (g_externalPriorityVirtualSuspended.load(std::memory_order_acquire) ||
            g_powerLifecycle.load(std::memory_order_acquire) != PowerLifecycle::Ready ||
            g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire) ||
            !g_inputReady.load(std::memory_order_acquire) || !sgVirtualWriterAllowedNow() ||
            g_shortcutRecordingActive.load(std::memory_order_acquire) ||
            physicalReleaseWindowActive()) return true;
    if (g_inputHostTupleHealPending.load(std::memory_order_acquire) ||
            g_inputHostRecoveryPending.load(std::memory_order_acquire)) return true;
    if (checkCooldown) {
        const auto last=g_inputHostRecoveryLastStartTick.load(std::memory_order_acquire);
        if (last && (now<last || now-last<kRecoveryCooldownMs)) return true;
    }
    if (!requireSource) return false;
    const auto source = inputHostRecoveryHealthLatestSourceLocked();
    if (!source.live || !source.stable ||
            source.identity != g_xinputSlotIdentityEpoch.load(std::memory_order_acquire) ||
            !ymcc::input::recoveryHealth::fresh(source,now,currentPowerGeneration(),
                g_xinputTopologyEpoch.load(std::memory_order_acquire))) return true;
    return false; // A spent continuation never consumes another cooldown or budget.
}
// A real failed report may have blocked the capture loop long enough to age
// its cached source. That missing freshness does not invent a fault; the exact
// owned transport fault already proves it. Stop may proceed, Start still waits
// for a fresh current source. A clean failed startup retains its source gate.
static bool stopHeldLocked(Tick now) {
    const bool ownedFault=g_inputHostProcess && (g_inputHostTransportFault || g_inputHostSwitchRecovery.tupleFault);
    return heldLocked(now,!ownedFault);
}
static bool budgetBlockedLocked(Tick now) {
    return inputHostRecoveryBudgetExhaustedLocked(now) ||
        g_inputHostRecoveryTerminalLogged.load(std::memory_order_acquire) ||
        g_inputHostRecoveryFallbackState.load(std::memory_order_acquire) != 0;
}
// Read-only ownership validation except for adopting epoch zero after the
// existing return owner actually cleared this old tuple. Never rebind a new Host.
static const char* awaitingCleanupCurrentLocked(InputHostSwitchRecoveryRequest& request, Tick now) {
    if ((!request.spent && !request.executionCleanup) || !request.stopInvoked || !request.awaitingCleanup)
        return "invalid-cleanup-continuation";
    if (const char* reason = staleLocked(request,now,false)) return reason;
    if (g_inputHostEpoch == request.hostEpoch) return nullptr;
    if (g_inputHostEpoch == 0 && cleanLocked()) {
        request.hostEpoch = 0;
        return nullptr;
    }
    return "host-epoch-retired";
}
// Called only after this round's actual Stop. Proof/cleanup belongs entirely to
// the existing quarantine/return owner; this helper changes no ownership debt.
static void awaitCleanupLocked(const char* reason) noexcept {
    auto& request = g_inputHostSwitchRecovery;
    const bool first = !request.awaitingCleanup;
    request.hostEpoch = g_inputHostEpoch;
    request.pending = true;
    request.queued = false;
    request.awaitingCleanup = true; // spent stays true; Stop and budget are never replayed.
    g_inputHostSwitchRecoveryPending.store(true,std::memory_order_release);
    if (first) logLocked("held",reason,request);
}
} // namespace ymcc::inputHost::switchRecoveryRuntime

// Only classified startup/verification transport faults offer work. UI focus,
// normal idle and source-pending never establish a transport fault.
static void inputHostSwitchRecoveryOfferLocked(std::uint64_t ticket,
        std::uint64_t power, DWORD error,bool tupleFault) {
    namespace runtime = ymcc::inputHost::switchRecoveryRuntime;
    if (!ticket || !power || (!tupleFault && !runtime::retryable(error))) return;
    auto& request = g_inputHostSwitchRecovery;
    if (request.requestId == ticket) return; // Never rearm/renew this ticket.
    const auto now = inputHostStartProtectionNow();
    const auto view = g_virtualStartProtection.view(now);
    const auto remaining = view.remainingMs;
    if (view.requestId != ticket || view.persona == "disabled" ||
            (!tupleFault && view.persona != "steamdeck") ||
            (view.phase != "pending" && view.phase != "verifying") ||
            !view.locked || !remaining ||
            currentPowerGeneration() != power || inputHostConfiguredPersona() != view.persona ||
            !g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled ||
            inputHostExitFenceRaised() || g_exitRequested.load(std::memory_order_acquire) ||
            g_inputCaptureStop.load(std::memory_order_acquire) || !runtime::recoverableOwnedStateLocked(power,tupleFault) ||
            remaining > (std::numeric_limits<std::uint64_t>::max)() - now) return;
    // Preserve the original 30s remainder; the 10s inspection is not a retry hard cap.
    // A newer ticket may replace state; an older queued ticket can never borrow it.
    request = {};
    request.requestId = ticket;
    request.power = power;
    request.hostEpoch = g_inputHostEpoch;
    request.stoppedRunId = g_inputHostRunId;
    request.deadline = now + remaining;
    request.error = error;
    request.persona = view.persona;
    request.tupleFault = tupleFault;
    request.pending = true;
    g_inputHostSwitchRecoveryPending.store(true,std::memory_order_release);
    // A real failed execution supplies Tc. Admission elapsed time alone can
    // never finish inspection or manufacture a retry opportunity.
    if (view.phase=="pending")
        inputHostStartProtectionFinish(ticket,"failure",tupleFault?"execution-classified-tuple-failure":"execution-classified-transport-failure");
    runtime::logLocked("scheduled",tupleFault?"classified-tuple-receipt-failure":"classified-transport-failure",request);
}

static void inputHostSwitchRecoveryMaintenanceLocked() {
    namespace runtime = ymcc::inputHost::switchRecoveryRuntime;
    auto& request = g_inputHostSwitchRecovery;
    if (!request.pending) return;
    // Even a queued continuation must expire or retire with its own ticket.
    const auto now = inputHostStartProtectionNow();
    const char* stale = request.awaitingCleanup
        ? runtime::awaitingCleanupCurrentLocked(request,now) : runtime::staleLocked(request,now);
    if (stale) {
        runtime::retireLocked("stale",stale);
        return;
    }
    if (request.queued) return;
    // Fault observation cannot rebuild during execution's inspection window.
    // Mandatory cleanup has its own serialized path and consumes no retry.
    if (!runtime::executionInspectionCompleteLocked(now)) return;
    if (request.awaitingCleanup) {
        // Unknown proof only waits. In particular, never clear quarantine,
        // StartBlocked, a journal or a retained process to make this condition true.
        if (!runtime::cleanLocked() || g_inputHostEpoch != 0) return;
        if (runtime::heldLocked(GetTickCount64(),true,!request.spent)) return;
        if (!request.spent && runtime::budgetBlockedLocked(GetTickCount64())) {
            runtime::retireLocked("blocked","budget-after-execution-cleanup");
            return;
        }
    } else {
        if (runtime::stopHeldLocked(GetTickCount64())) return;
        if (request.spent || (!request.tupleFault && !runtime::retryable(request.error)) ||
                runtime::budgetBlockedLocked(GetTickCount64()) ||
                !runtime::recoverableOwnedStateLocked(request.power,request.tupleFault)) {
            runtime::retireLocked("blocked","budget-or-cleanup-not-admitted");
            return;
        }
    }
    const auto reason = "switch-recovery:" + std::to_string(request.requestId);
    request.queued = true; // Pending/atomic remain true while this token is queued.
    try {
        if (!inputHostQueueLifecycleWork(reason.c_str()))
            runtime::retireLocked("blocked","queue-refused");
    } catch (...) {
        // A late partially enqueued token sees pending=false; no attempted IO replays.
        runtime::retireLocked("blocked","queue-exception");
    }
}

// Existing serialized lifecycle owner only; enters WITHOUT Host lock. The first
// dispatch owns Stop + one budget charge. AwaitingCleanup resumes only Start.
static void inputHostPerformSwitchRecovery(std::uint64_t ticket) {
    namespace runtime = ymcc::inputHost::switchRecoveryRuntime;
    InputHostSwitchRecoveryRequest attempt;
    bool startReturned = false;
    const char* result = "start-failed";
    std::string exceptionDetail;
    try {
        {
            std::lock_guard<InputHostMutex> lock(g_inputHostMx);
            auto& request = g_inputHostSwitchRecovery;
            if (!ticket || request.requestId != ticket || !request.pending || !request.queued ||
                    (request.spent && !request.awaitingCleanup)) return;
            request.queued = false;
            const bool continuation = request.awaitingCleanup;
            const auto now = inputHostStartProtectionNow();
            const char* stale = continuation
                ? runtime::awaitingCleanupCurrentLocked(request,now) : runtime::staleLocked(request,now);
            if (stale) {
                runtime::retireLocked("stale",stale);
                return;
            }
            if (!runtime::executionInspectionCompleteLocked(now)) return;
            if (continuation) {
                if (!runtime::cleanLocked() || g_inputHostEpoch != 0) return;
                if (runtime::heldLocked(GetTickCount64(),true,!request.spent)) return;
                // Execution already owns Stop. First fallback still checks shared
                // budgets; a spent continuation never charges those twice.
            } else {
                const auto recoveryNow = GetTickCount64();
                if (runtime::stopHeldLocked(recoveryNow)) return;
                if ((!request.tupleFault && !runtime::retryable(request.error)) || runtime::budgetBlockedLocked(recoveryNow) ||
                        !runtime::recoverableOwnedStateLocked(request.power,request.tupleFault)) {
                    runtime::retireLocked("blocked","budget-or-cleanup-not-admitted");
                    return;
                }
            }
            if (!request.spent) {
                const auto recoveryNow = GetTickCount64();
                if (runtime::budgetBlockedLocked(recoveryNow) ||
                        !g_virtualStartProtection.reserveFallback(ticket,now)) {
                    runtime::retireLocked("blocked","fallback-contract-or-budget");
                    return;
                }
                request.spent = true; // Reserve BEFORE IO; never renew/replay.
                inputHostRecoveryChargeAttemptLocked(recoveryNow);
                attempt = request;
                runtime::logLocked("started",continuation ? "classified-reopen-after-execution-cleanup" : "classified-stop-reopen",attempt);
            }
            if (!continuation) {
                // Same Host lock as final validation; cleanup is never cut short.
                // If this throws, the catch reports a failure candidate, never a replay.
                g_inputHostFaultHoldsStart.store(true,std::memory_order_release);
                request.stopInvoked=true;attempt.stopInvoked=true;
                inputHostStopLocked("switch-recovery");
                if (const char* reason = runtime::staleLocked(attempt,inputHostStartProtectionNow(),false)) {
                    runtime::retireLocked("stale",reason);
                    return;
                }
                if (!runtime::cleanLocked() || g_inputHostEpoch != 0) {
                    runtime::awaitCleanupLocked("awaiting-existing-return-proof");
                    return;
                }
                request.hostEpoch = g_inputHostEpoch; // Actual Stop cleared the old tuple to zero.
                if (runtime::heldLocked(GetTickCount64(),true,false)) {
                    runtime::awaitCleanupLocked("awaiting-post-cleanup-pause");
                    return;
                }
            }
            // Exactly one Start may leave this state. Duplicate queued tokens now
            // see spent && !awaitingCleanup and cannot Stop/Start/charge again.
            request.pending = false;
            request.queued = false;
            request.awaitingCleanup = false;
            g_inputHostSwitchRecoveryPending.store(false,std::memory_order_release);
            attempt = request;
            if (continuation) runtime::logLocked("started","resume-start-after-cleanup",attempt);
            // Only this classified, unchanged, strictly cleaned round may lift
            // its fault hold. Ownership debt and shared budgets remain untouched.
            g_inputHostFaultHoldsStart.store(false,std::memory_order_release);
        }
        struct StartDeadlineScope {
            std::uint64_t deadline, requestId, power;
            explicit StartDeadlineScope(const InputHostSwitchRecoveryRequest& attempt)
                : deadline(g_inputHostSwitchRecoveryStartDeadline),
                  requestId(g_inputHostSwitchRecoveryStartRequestId),
                  power(g_inputHostSwitchRecoveryStartPower) {
                g_inputHostSwitchRecoveryStartDeadline = deadline && deadline < attempt.deadline
                    ? deadline : attempt.deadline;
                g_inputHostSwitchRecoveryStartRequestId = attempt.requestId;
                g_inputHostSwitchRecoveryStartPower = attempt.power;
            }
            ~StartDeadlineScope() {
                g_inputHostSwitchRecoveryStartDeadline = deadline;
                g_inputHostSwitchRecoveryStartRequestId = requestId;
                g_inputHostSwitchRecoveryStartPower = power;
            }
        } deadlineScope(attempt);
        startReturned = inputHostStart();
        // Main's verifier owns candidate evidence and the only final result/unlock.
        result = startReturned ? "start-returned-candidate-awaiting-verification" : "start-failed";
    } catch (const std::exception& error) {
        result="stop-or-start-exception";
        // Bounded printable detail is optional evidence. It cannot throw from
        // an exception handler or introduce malformed UTF-8 into log export.
        try {const auto* text=error.what();for(std::size_t i=0;text && text[i] && i<512;++i){
            const auto c=static_cast<unsigned char>(text[i]);exceptionDetail.push_back(c>=32&&c<127?static_cast<char>(c):'?');}}
        catch (...) {}
    } catch (...) { result = "non-std-stop-or-start-exception"; }
    {
        std::lock_guard<InputHostMutex> lock(g_inputHostMx);
        if (!attempt.requestId) return; // Exception before any reserved IO; pending work can retry admission.
        const auto view = g_virtualStartProtection.view(inputHostStartProtectionNow());
        const bool same = view.requestId == ticket && currentPowerGeneration() == attempt.power;
        if (!startReturned) {
            if (same && (view.phase == "pending" || view.phase == "verifying" || view.phase == "failure"))
                g_inputHostFaultHoldsStart.store(true,std::memory_order_release);
            runtime::failureCandidateLocked(attempt.requestId);
        }
        runtime::logLocked("result",same ? result : "request-retired-during-operation",attempt,startReturned,exceptionDetail.c_str());
    }
}

// The exact queued fault cleanup is also a recovery dispatch opportunity. If
// recovery cannot claim it, retain the original safety cleanup path.
static bool inputHostConsumeSwitchFaultCleanup(std::uint64_t faultEpoch) {
    namespace runtime = ymcc::inputHost::switchRecoveryRuntime;
    std::uint64_t ticket=0;
    {
        std::lock_guard<InputHostMutex> lock(g_inputHostMx);
        if (faultEpoch && g_inputHostEpoch!=faultEpoch) return true;
        const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
        if (g_inputHostTransportFault)
            inputHostSwitchRecoveryOfferLocked(view.requestId,currentPowerGeneration(),g_inputHostSwitchRecoveryTransportError);
        auto& request=g_inputHostSwitchRecovery;
        // A spent old Stop remains owned even after UI expiry. Its queued
        // duplicate cannot repeat Stop while quarantine return is still active.
        if (faultEpoch && request.hostEpoch==faultEpoch && request.stopInvoked &&
                !g_inputHostProcess && g_inputHostReleaseQuarantined &&
                !request.stoppedRunId.empty() && request.stoppedRunId==g_inputHostRunId) {
            runtime::logLocked("coalesced","fault-cleanup-already-stopped",request);
            return true;
        }
        if (!faultEpoch || request.requestId!=view.requestId || request.hostEpoch!=faultEpoch ||
                runtime::staleLocked(request,inputHostStartProtectionNow())) return false;
        if (request.stopInvoked && (request.spent || request.executionCleanup) && request.awaitingCleanup && request.pending) {
            runtime::logLocked("coalesced","fault-cleanup-already-owned",request);
            return true;
        }
        if (!request.pending || request.spent) return false;
        if (!runtime::executionInspectionCompleteLocked(inputHostStartProtectionNow())) {
            // The original fault token owns immediate safety Stop. Publish its
            // ownership BEFORE IO; duplicate tokens cannot repeat the cleanup.
            g_inputHostFaultHoldsStart.store(true,std::memory_order_release);
            request.stopInvoked=true;
            request.executionCleanup=true;
            request.awaitingCleanup=true;
            request.queued=false;
            try {
                appendNativeLifecycleLog("input-host-switch-execution", {
                    {"lifecycleVersion",ymcc::VirtualStartProtection::lifecycleVersion},
                    {"stage","fault-cleanup"},{"requestId",request.requestId},
                    {"hostEpoch",request.hostEpoch},{"powerGeneration",request.power},
                    {"fallbackAttempts",view.fallbackAttempts},{"resultScope","execution-only"}});
            } catch (...) {}
            inputHostStopLocked("switch-execution-fault-cleanup");
            // Existing return proof still owns quarantine/physical restoration.
            // Even a fully clean Stop cannot authorize Start before inspection.
            return true;
        }
        if (runtime::stopHeldLocked(GetTickCount64()) || runtime::budgetBlockedLocked(GetTickCount64()) ||
                !runtime::recoverableOwnedStateLocked(request.power)) {
            runtime::retireLocked("blocked","fault-cleanup-priority");
            return false;
        }
        request.queued=true;
        ticket=request.requestId;
    }
    inputHostPerformSwitchRecovery(ticket);
    std::lock_guard<InputHostMutex> lock(g_inputHostMx);
    // Power/exit/pause can change between reservation and execution. A skipped
    // recovery did not acquire cleanup ownership; keep the original safety Stop.
    return g_inputHostEpoch!=faultEpoch ||
        (g_inputHostSwitchRecovery.requestId==ticket && g_inputHostSwitchRecovery.stopInvoked);
}

// Real quarantine return owner only, after endpoint absence, physical restore
// readback and Clear. This consumes no debt and cannot authorize another tuple.
static bool inputHostSwitchRecoveryReturnedLocked(std::uint64_t retiredEpoch,const std::string& retiredRunId) {
    namespace runtime = ymcc::inputHost::switchRecoveryRuntime;
    auto& request=g_inputHostSwitchRecovery;
    if (!request.pending || (!request.spent && !request.executionCleanup) || !request.stopInvoked || !request.awaitingCleanup || !retiredEpoch ||
            request.hostEpoch!=retiredEpoch || request.stoppedRunId.empty() ||
            request.stoppedRunId!=retiredRunId || runtime::staleLocked(request,inputHostStartProtectionNow(),false) ||
            g_inputHostEpoch || g_inputHostProcess || g_inputHostHello || g_inputHostPrepared ||
            g_inputHostNeutralized || g_inputHostActive || g_hidHideVisibilityJournal.active ||
            g_inputHostReleaseQuarantined || g_msiSteamDeckReleaseDebt.required()) return false;
    g_inputHostStartBlocked=false;
    request.hostEpoch=0;
    runtime::logLocked("return-proven","same-intent-rearm-authorized",request);
    return true;
}
