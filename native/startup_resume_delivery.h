#pragma once
#include <optional>
#include "sleep_power_state.h"

namespace ymcc::sleepStartupResume {

// Serialize with the existing SleepWork queue. This tracks delivery of one
// startup request; only the public snapshot and registration authorize work.
class Delivery {
    using Power = sleeppower::SharedPowerV1;
    enum class State { Idle, Pending, Closed };
    State state_ = State::Idle;
    std::optional<Power> offered_, queued_;
    bool active_ = false;

public:
    bool request() noexcept {
        if (state_ != State::Idle) return false;
        state_ = State::Pending;
        return true;
    }

    // The caller confirms this snapshot is current under the queue lock.
    bool offer(const Power& current) noexcept {
        if (state_ != State::Pending || !sleeppower::powerAdmitted(current)) return false;
        if (offered_ && (current.publicationRevision < offered_->publicationRevision ||
                        sleeppower::samePowerRevocation(*offered_, current))) return false;
        offered_ = queued_ = current;
        return true;
    }

    bool runnable() const noexcept {
        return state_ == State::Pending && !active_ && queued_.has_value();
    }

    std::optional<Power> take() noexcept {
        if (!runnable()) return {};
        auto result = queued_;
        queued_.reset();
        active_ = true;
        return result;
    }

    void finish(bool allOwnedRecordsSettled) noexcept {
        if (!active_) return;
        active_ = false;
        if (allOwnedRecordsSettled) stop();
    }

    void stop() noexcept {
        state_ = State::Closed;
        queued_.reset();
    }
};

} // namespace ymcc::sleepStartupResume
