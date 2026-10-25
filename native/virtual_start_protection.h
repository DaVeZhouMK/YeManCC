#pragma once
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>

namespace ymcc {
// Virtual-switch lifecycle contract VC-LIFECYCLE-20261020-R3 (single authority).
// Reference comments only: no runtime parser/configuration/MD execution.
// Maintain new log-confirmed cases in the existing actual-consumer fixtures and
// public coordinator gates; preserve the original failing case and source binding.
// Timeline: T0=admission; Tc=first ready-input/closed-output or failed execution receipt.
// PREPARE/HELLO success alone is not Tc and cannot claim usable input.
//   admit -> serialized execution -> first candidate -> 10s active execution inspection
//         -> healthy: unique final; otherwise: classified, bounded fallback -> final.
// Output is usable as soon as execution supplies accepted current-source reports;
// inspection keeps other player ON/OFF/persona transactions locked.
// Ordinary return/cleanup and known late-source/ACK handling belong to EXECUTION.
// R3: a timed-out old persona retirement waits on its exact old tuple; verified
// return resumes only the remaining normal Start, with zero fallback quota.
// Mandatory fault cleanup may run during inspection, without charging fallback.
// Only AFTER inspection may one fallback reopen the same intent, with real return
// proof, a fresh source and shared budgets. It never resets either deadline.
// At admission +30s: unique timeout failure and UI release; ownership safety still
// forbids a new Start from racing unfinished return. No concurrent Stop/Start.
// Missing/idle/focus-neutral input alone cannot diagnose a fault or justify fallback.
// Improve known execution cases using exported evidence, not runtime self-learning.
// This class owns timing/admission/retry reservation; the existing serialized Host
// owner performs IO and the common verifier owns the sole final success/failure.
class VirtualStartProtection {
public:
    using Tick = std::uint64_t;
    static constexpr const char* lifecycleVersion = "VC-LIFECYCLE-20261020-R3";
    static constexpr Tick maxMs = 30000, executionInspectionMs = 10000;
    static constexpr Tick settleMs = executionInspectionMs; // Existing bridge/fixture field name.
    static constexpr unsigned fallbackMaxAttempts = 1;
    struct View {
        bool locked = false;
        Tick requestId = 0, remainingMs = 0, elapsedMs = 0;
        std::string phase = "idle", persona;
        bool verificationPending = false;
        Tick verificationRemainingMs = 0;
        std::string candidateOutcome;
        unsigned fallbackAttempts = 0;
        Tick inspectionStartedElapsedMs = 0, inspectionDeadlineElapsedMs = 0;
    };
    struct Admission { bool accepted = false; Tick requestId = 0; bool created = false; };

    // Every player transition, including OFF, waits for the admitted transaction.
    Admission admit(const std::string& persona, Tick now) {
        std::lock_guard<std::mutex> guard(mx_);
        if (viewLocked(now).locked) return {};
        return beginLocked(persona, now);
    }
    // Settings consumer / internal recovery inherits its admitted intent. An
    // unrelated target cannot enter until admission expires or settles.
    Admission consume(const std::string& persona, Tick now) {
        std::lock_guard<std::mutex> guard(mx_);
        const auto view = viewLocked(now);
        if (view.locked) {
            const bool inherited = phase_ == "pending" && persona_ == persona;
            return {inherited, inherited ? id_ : 0, false};
        }
        // A delayed consumer does not renew the original request's 30s deadline.
        if (phase_ == "expired" && persona_ == persona) return {true,id_,false};
        return beginLocked(persona, now);
    }
    Tick pendingTicket(const std::string& persona, Tick now) {
        std::lock_guard<std::mutex> guard(mx_);
        const auto view = viewLocked(now);
        return view.locked && phase_ == "pending" && persona_ == persona ? id_ : 0;
    }
    // Existing operational recovery belongs to the same already admitted intent.
    // It cannot admit a user request, renew a deadline or revive explicit cancellation.
    Tick existingTicket(const std::string& persona, Tick now) {
        std::lock_guard<std::mutex> guard(mx_);
        viewLocked(now);
        return id_ && persona_ == persona && phase_ != "cancelled" ? id_ : 0;
    }
    // Stale completions and repeated receipts cannot unlock or extend a newer request.
    bool finish(Tick ticket, const char* outcome, Tick now) {
        std::lock_guard<std::mutex> guard(mx_);
        if (!ticket || ticket != id_ || phase_ != "pending") return false;
        if (!viewLocked(now).locked) return false;
        phase_ = outcome;
        terminalAt_ = now;
        return true;
    }
    // Native lifecycle reports a candidate first. The single verification
    // consumer owns final success/failure and admission release.
    bool beginVerification(Tick ticket, const char* outcome, Tick now) {
        std::lock_guard<std::mutex> guard(mx_);
        if (!ticket || ticket!=id_ || !viewLocked(now).locked ||
                (phase_!="pending" && phase_!="verifying")) return false;
        if (phase_=="verifying") {
            // An observed failure may invalidate a candidate success, without
            // renewing the first verification deadline or publishing a final.
            if (std::string(outcome)=="failure") candidateOutcome_="failure";
            return false;
        }
        phase_="verifying";candidateOutcome_=outcome;terminalAt_=now;
        return true;
    }
    bool finishVerification(Tick ticket, const char* outcome, Tick now) {
        std::lock_guard<std::mutex> guard(mx_);
        const auto view=viewLocked(now);
        if (!ticket || ticket!=id_ || phase_!="verifying" ||
                !view.locked || view.verificationRemainingMs) return false;
        phase_=outcome;verificationComplete_=true;return true;
    }
    // Reserve a fallback, never another player admission or a renewed clock.
    // IO also requires a classified fault, exact ownership, current source and
    // shared budget. Reopening after execution cleanup requires proven return.
    bool canContinueExecution(Tick ticket, const std::string& persona, Tick now) {
        std::lock_guard<std::mutex> guard(mx_);
        const auto v=viewLocked(now);
        return ticket && ticket==id_ && v.locked && v.persona==persona &&
            (v.phase=="pending" || v.phase=="verifying");
    }
    bool executionInspectionComplete(Tick now) {
        std::lock_guard<std::mutex> guard(mx_);
        return executionInspectionCompleteLocked(now);
    }
    bool fallbackAllowed(Tick ticket, Tick now) {
        std::lock_guard<std::mutex> guard(mx_);
        return fallbackAllowedLocked(ticket, now);
    }
    bool reserveFallback(Tick ticket, Tick now) {
        std::lock_guard<std::mutex> guard(mx_);
        if (!fallbackAllowedLocked(ticket, now)) return false;
        ++fallbackAttempts_;
        return true;
    }
    bool cancel(Tick now) {
        std::lock_guard<std::mutex> guard(mx_);
        if (!viewLocked(now).locked || phase_ != "pending") return false;
        phase_ = "cancelled"; terminalAt_ = now; return true;
    }
    View view(Tick now) {
        std::lock_guard<std::mutex> guard(mx_);
        return viewLocked(now);
    }
private:
    bool executionInspectionCompleteLocked(Tick now) {
        const auto view = viewLocked(now);
        return view.locked && view.verificationPending && !view.verificationRemainingMs;
    }
    bool fallbackAllowedLocked(Tick ticket, Tick now) {
        return ticket && ticket == id_ && fallbackAttempts_ < fallbackMaxAttempts &&
            executionInspectionCompleteLocked(now);
    }
    Admission beginLocked(const std::string& persona, Tick now) {
        ++id_; if (!id_) ++id_;
        admittedAt_ = now; terminalAt_ = 0; persona_ = persona; phase_ = "pending";
        candidateOutcome_.clear();verificationComplete_=false;fallbackAttempts_=0;
        return {true, id_, true};
    }
    View viewLocked(Tick now) {
        const auto elapsed = now >= admittedAt_ ? now - admittedAt_ : 0;
        Tick remaining = id_ && elapsed < maxMs ? maxMs - elapsed : 0;
        if (phase_ != "pending" && phase_ != "verifying" && phase_ != "idle" && phase_ != "expired") {
            const auto after = now >= terminalAt_ ? now - terminalAt_ : 0;
            remaining = (std::min)(remaining, after < settleMs ? settleMs - after : Tick{0});
        }
        if (id_ && !remaining && (phase_ == "pending" || phase_ == "verifying")) phase_ = "expired";
        if (verificationComplete_) remaining=0;
        const bool verifying=phase_=="verifying";
        const Tick after=now>=terminalAt_?now-terminalAt_:0;
        const bool hasReceipt=!candidateOutcome_.empty();
        const Tick started=hasReceipt && terminalAt_>=admittedAt_?terminalAt_-admittedAt_:0;
        const Tick inspectionDeadline=hasReceipt?(std::min)(maxMs,started+executionInspectionMs):0;
        return {id_ != 0 && remaining != 0, id_, remaining, id_ ? elapsed : 0, phase_, persona_,
            verifying,verifying && after<settleMs?settleMs-after:0,candidateOutcome_,fallbackAttempts_,started,inspectionDeadline};
    }
    std::mutex mx_;
    Tick id_ = 0, admittedAt_ = 0, terminalAt_ = 0;
    std::string phase_ = "idle", persona_, candidateOutcome_;
    bool verificationComplete_=false;
    unsigned fallbackAttempts_=0;
};
} // namespace ymcc
