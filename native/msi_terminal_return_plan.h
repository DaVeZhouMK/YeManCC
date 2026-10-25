#pragma once

#include <cstdint>
#include <limits>

namespace ymcc::msi::terminalReturn {

struct Facts {
    bool knownBoard = false;
    bool exitFence = false;
    bool powerReady = false;
    bool targetEnabled = false;
    bool routeArmed = false;
    bool returnOwed = false;
    // Actual process/prepared ownership only; a UI mirror is not host presence.
    bool hostPresent = false;
    // Nonzero generation tokens; zero means unknown, not eligible to plan.
    std::uint64_t currentPower = 0;
    std::uint64_t currentMode = 0;
};

// A serialized caller supplies monotonic milliseconds and current Facts. This
// schedules lookups for an ALREADY authorized input-host-stop mode1 return.
// It grants no write/mode2 authority, changes no return debt, and proves no
// external recovery. The caller owns native Outcome classification and must
// recheck context before issuing any I/O.
class Plan {
public:
    static constexpr std::uint64_t lifetimeMs = 10000;
    static constexpr std::uint64_t probeIntervalMs = 500;
    static constexpr unsigned maxProbes = 20;

    // Call only after that authorized stop's initial attempt was strictly
    // missingTransport and did not attempt a write. Only the initial gate
    // winner calls begin. An inactive plan requires
    // a fresh authorization to begin again; a probe result cannot renew it.
    // Planning may precede host cleanup, so begin ignores hostPresent. The
    // caller must checkContext on every tick, before due and before I/O, with
    // actual host ownership gone. Refuse an active plan unchanged, even if its
    // deadline has elapsed.
    bool begin(std::uint64_t now, const Facts& facts) noexcept {
        if (active_ || !eligible(facts) ||
            now > (std::numeric_limits<std::uint64_t>::max)() - lifetimeMs)
            return false;
        active_ = true;
        startedAt_ = lastNow_ = now;
        deadline_ = now + lifetimeMs;
        next_ = now + probeIntervalMs;
        probes_ = 0;
        power_ = facts.currentPower;
        mode_ = facts.currentMode;
        return true;
    }

    // Any disallowed or changed context ends this plan permanently. A later
    // matching context cannot revive it. Facts and returnOwed are never mutated.
    bool checkContext(const Facts& facts) noexcept {
        if (!active_) return false;
        if (!eligible(facts) || facts.hostPresent || facts.currentPower != power_ ||
            facts.currentMode != mode_)
            cancel();
        return active_;
    }

    // Inclusive deadline: +500, +1000, ..., +10000 permits exactly 20 probes.
    // Claims one probe and spaces the next from this actual claim, so a late
    // caller cannot catch up with a burst or extend the original deadline.
    // The final claimed probe remains active until its outcome is reported.
    bool due(std::uint64_t now) noexcept {
        expire(now);
        if (!active_) return false;
        if (next_ == 0 || probes_ >= maxProbes) {
            cancel();
            return false;
        }
        if (now < next_) return false;
        ++probes_;
        // Zero means there is no further schedulable probe. Subtraction is safe
        // after expire(); addition is performed only when within the deadline.
        next_ = probes_ < maxProbes && deadline_ - now >= probeIntervalMs
            ? now + probeIntervalMs : 0;
        return true;
    }

    // missingTransport is classified by the caller: no devices, vendor matches,
    // open failures, write attempts or errors, and native I/O says no-device.
    // All other outcomes (including unknown), and ANY write attempt regardless
    // of success/failure/timeout, complete the plan. True means it is retained.
    bool observeAttempt(bool writeAttempted, bool missingTransport) noexcept {
        if (active_ && (writeAttempted || !missingTransport || next_ == 0))
            cancel();
        return active_;
    }

    // True only when this call terminates an active plan for time expiry or
    // clock rollback. The last observed time includes calls before a probe is
    // due. Neither expiration nor cancellation clears the caller's return debt.
    bool expire(std::uint64_t now) noexcept {
        if (!active_) return false;
        if (now < lastNow_ || now > deadline_) {
            cancel();
            return true;
        }
        lastNow_ = now;
        return false;
    }

    void cancel() noexcept {
        active_ = false;
        next_ = 0;
    }

    bool active() const noexcept { return active_; }
    std::uint64_t startedAt() const noexcept { return startedAt_; }
    std::uint64_t deadline() const noexcept { return deadline_; }
    // Zero means none; after the final claim the outcome still needs handling.
    std::uint64_t nextProbeAt() const noexcept { return next_; }
    unsigned probes() const noexcept { return probes_; }
    std::uint64_t power() const noexcept { return power_; }
    std::uint64_t mode() const noexcept { return mode_; }

private:
    static bool eligible(const Facts& facts) noexcept {
        return facts.knownBoard && !facts.exitFence && facts.powerReady &&
            !facts.targetEnabled && !facts.routeArmed && facts.returnOwed &&
            facts.currentPower != 0 && facts.currentMode != 0;
    }

    bool active_ = false;
    std::uint64_t startedAt_ = 0;
    std::uint64_t lastNow_ = 0;
    std::uint64_t deadline_ = 0;
    std::uint64_t next_ = 0;
    unsigned probes_ = 0;
    std::uint64_t power_ = 0;
    std::uint64_t mode_ = 0;
};

} // namespace ymcc::msi::terminalReturn
