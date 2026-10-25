#pragma once

#include <cstdint>

namespace ymcc::msi::ownedReturn {

// Pure policy for an ALREADY owned native XInput return debt. Serialized calls
// only: the parent owns synchronization (including any atomic idle/UI mirrors),
// exact root/location identity, fresh sample validation, tickets, and all I/O.
// There is no Host epoch binding, timer, worker, polling, or device authority.
class Recovery {
public:
    static constexpr std::uint64_t observationIntervalMs = 1000;
    static constexpr std::uint64_t stableMatchMs = 300;
    static constexpr std::uint64_t retryDelayMs = 1500;
    static constexpr unsigned maxObservations = 20;

    // episode is the nonzero g_msiDinputReturnDebtEpisode, not a Host epoch.
    // Only an explicit, different episode starts over. Reentry, including after
    // completion/cancel, cannot renew the window, permission, or retry budget.
    // Zero is rejected unchanged, so it cannot accidentally cancel an old debt.
    // retryAllowed is classified by the parent: no writeAttempted, confirmed
    // zero-byte disconnected, or an initial successful write with no XInput and
    // fresh DInput at the same owned root. Unknown/ambiguous I/O is not eligible.
    bool begin(std::uint64_t now, std::uint64_t episode,
               bool retryAllowed) noexcept {
        if (episode == 0 || episode == episode_) return false;
        *this = Recovery{};
        episode_ = episode;
        startedAt_ = lastNow_ = now;
        retryAllowed_ = retryAllowed;
        state_ = State::observing;
        return true;
    }

    // Only a new mode2 or an explicit cancellation calls this. Keep the episode
    // tombstone and consumed budget; repeating begin(oldEpisode) cannot rearm it.
    void cancel() noexcept {
        if (episode_ != 0) state_ = State::cancelled;
        miss();
    }

    // A caller may resume this SAME physical obligation after an observed
    // failed mode2 request and a newer explicit OFF. This is not begin(): the
    // consumed retry and original eligibility can only be retained or reduced.
    // The caller must revalidate its durable owner/root before invoking this.
    bool resumeCancelled(std::uint64_t now,std::uint64_t episode,bool retryAllowed) noexcept {
        if(state_!=State::cancelled || !episode || episode!=episode_ || !acceptTime(now))return false;
        retryAllowed_=retryAllowed_ && retryAllowed;
        state_=State::observing;generationsKnown_=false;observations_=0;miss();
        return true;
    }

    bool pending() const noexcept {
        return state_ == State::observing || state_ == State::dormant;
    }
    bool working() const noexcept { return state_ == State::observing; }
    bool failed() const noexcept { return state_ == State::dormant; }
    bool completed() const noexcept { return state_ == State::completed; }
    bool cancelled() const noexcept { return state_ == State::cancelled; }
    std::uint64_t episode() const noexcept { return episode_; }
    bool retryUsed() const noexcept { return retryUsed_; }
    unsigned observations() const noexcept { return observations_; }

    // Claims one observation, at most 20 per window and one per actual second.
    // The first valid call binds generation tokens. Subsequent token changes
    // open a window and invalidate signal stability, even if rate limited.
    // Zero power is unknown: it neither grants a probe nor refreshes a window.
    // Exhaustion stays dormant indefinitely until an existing topology/power
    // event changes these tokens; elapsed time alone never renews a window.
    // sourceEvent is an existing physical identity/arrival edge when Windows
    // did not also emit a topology notification; it is not a per-frame tick.
    // The rate limit survives event refreshes; the retry budget is per episode.
    bool due(std::uint64_t now, std::uint64_t topology,
             std::uint64_t power, std::uint64_t sourceEvent = 0) noexcept {
        if (!pending() || !acceptTime(now)) return false;
        if (power == 0) {
            miss();
            return false;
        }
        if (!generationsKnown_ || topology != topology_ || power != power_ || sourceEvent != sourceEvent_) {
            generationsKnown_ = true;
            topology_ = topology;
            power_ = power;
            sourceEvent_ = sourceEvent;
            observations_ = 0;
            state_ = State::observing;
            miss();
        }
        if (!working() ||
            (hasObservation_ && now - lastObservationAt_ < observationIntervalMs))
            return false;
        hasObservation_ = true;
        lastObservationAt_ = now;
        ++observations_;
        if (observations_ == maxObservations) state_ = State::dormant;
        return true;
    }

    // Call only for a CURRENT fresh XInput sample already checked against the
    // exact owned root/location. A write receipt, slot presence, cached sample,
    // virtual controller, or a DInput match cannot call this as XInput evidence.
    // sequence is a nonzero fresh sample identity, NOT XINPUT dwPacketNumber
    // (a stationary controller may keep the same packet number indefinitely).
    // due gates requested work, not evidence: fresh samples delivered by the
    // parent's existing lifecycle can settle even dormant debt. This also lets
    // the 20th granted observation complete after due has exhausted its window.
    // Reconcile current topology/power through due before delivering evidence.
    bool observeMatch(std::uint64_t now, std::uint64_t sequence,bool completeOnMatch=true) noexcept {
        if (!pending() || !acceptTime(now)) return false;
        if (sequence == 0) {
            miss();
            return false;
        }
        if (!hasStableMatch_) {
            hasStableMatch_ = true;
            stableAt_ = now;
            firstSequence_ = lastSequence_ = sequence;
            return false;
        }
        if (sequence == firstSequence_ || sequence == lastSequence_) return false;
        lastSequence_ = sequence;
        if (now - stableAt_ < stableMatchMs) return false;
        return completeOnMatch ? completeMatch() : true;
    }

    // Runtime can stage a stable match across durable I/O. A failed commit
    // leaves this same bounded observer and retry budget intact; no mode write
    // is needed to retry settlement with the next fresh matching read window.
    bool completeMatch() noexcept {
        if(!pending() || !hasStableMatch_ || firstSequence_==lastSequence_ ||
           lastNow_-stableAt_<stableMatchMs)return false;
        state_ = State::completed;
        miss();
        return true;
    }

    void miss() noexcept {
        hasStableMatch_ = false;
        stableAt_ = firstSequence_ = lastSequence_ = 0;
    }

    // Read-only preparation gate. It never grants a write or consumes a retry;
    // the parent may use it to release only its proven rebound local source.
    bool retryDue(std::uint64_t now) const noexcept {
        return pending() && retryAllowed_ && !retryUsed_ && now>=lastNow_ &&
            now>=startedAt_ && now-startedAt_>=retryDelayMs;
    }

    // A true result CONSUMES the single retry BEFORE the parent performs I/O.
    // safe includes the parent's current power/lifecycle/ticket guards;
    // matchingDinput means fresh, confirmed DInput at this exact owned root.
    // noAmbiguousIo is a per-call parent fact, never a stored permission field.
    // A retry result cannot complete the debt, renew observation, or refund the
    // budget. Recheck real guards immediately before any actual device write.
    bool claimRetry(std::uint64_t now, bool safe, bool matchingDinput,
                    bool noAmbiguousIo) noexcept {
        if (!pending() || !acceptTime(now) || !retryAllowed_ || retryUsed_ ||
            !safe || !matchingDinput || !noAmbiguousIo ||
            now - startedAt_ < retryDelayMs)
            return false;
        retryUsed_ = true;
        miss();
        return true;
    }

private:
    enum class State : unsigned char { idle, observing, dormant, completed, cancelled };

    // Preserve a time high-water mark on rollback. No unsigned underflow, catch-up
    // loop, or deadline addition is used, including near UINT64_MAX.
    bool acceptTime(std::uint64_t now) noexcept {
        if (now < lastNow_) {
            miss();
            return false;
        }
        lastNow_ = now;
        return true;
    }

    State state_ = State::idle;
    bool retryAllowed_ = false;
    bool retryUsed_ = false;
    bool generationsKnown_ = false;
    bool hasObservation_ = false;
    bool hasStableMatch_ = false;
    unsigned observations_ = 0;
    std::uint64_t episode_ = 0;
    std::uint64_t startedAt_ = 0;
    std::uint64_t lastNow_ = 0;
    std::uint64_t lastObservationAt_ = 0;
    std::uint64_t topology_ = 0;
    std::uint64_t power_ = 0;
    std::uint64_t sourceEvent_ = 0;
    std::uint64_t stableAt_ = 0;
    std::uint64_t firstSequence_ = 0;
    std::uint64_t lastSequence_ = 0;
};

} // namespace ymcc::msi::ownedReturn
