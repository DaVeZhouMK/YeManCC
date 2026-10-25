#pragma once

#include "controller_motion_processor.h"
#include <limits>

// Caller-serialized calibration of a borrowed Session handle. The caller keeps
// the library/handle alive and supplies nonthrowing, non-reentrant C callbacks.
// No ownership, ProcessMotion, clock acquisition, persistence, or destructor
// callbacks: prepare grants one receipt; the caller processes it then observes.
namespace ymcc::controller_motion {

struct CalibrationOps {
    void (*reset)(void*) = nullptr;
    void (*setMode)(void*, int) = nullptr;
    float (*getConfidence)(void*) = nullptr;
    void (*getOffset)(void*, float*, float*, float*) = nullptr;
    void (*setOffset)(void*, float, float, float, int) = nullptr;

    bool complete() const {
        return reset && setMode && getConfidence && getOffset && setOffset;
    }
    friend bool operator==(const CalibrationOps& a, const CalibrationOps& b) {
        return a.reset == b.reset && a.setMode == b.setMode &&
               a.getConfidence == b.getConfidence && a.getOffset == b.getOffset &&
               a.setOffset == b.setOffset;
    }
    friend bool operator!=(const CalibrationOps& a, const CalibrationOps& b) {
        return !(a == b);
    }
};

enum class CalibrationOutcome {
    None, Succeeded, TimedOut, SourceRetired, InvalidConfidence, InvalidOffset,
    ProcessFailed, PersistFailed, ClockRegressed, Aborted
};

class CalibrationTransaction final {
public:
    CalibrationTransaction() = default;
    ~CalibrationTransaction() = default;
    CalibrationTransaction(const CalibrationTransaction&) = delete;
    CalibrationTransaction& operator=(const CalibrationTransaction&) = delete;
    CalibrationTransaction(CalibrationTransaction&&) = delete;
    CalibrationTransaction& operator=(CalibrationTransaction&&) = delete;

    // A live transaction cannot be silently overwritten. Invalid begin leaves
    // the previous outcome intact. Zero window/age are literal boundary values.
    bool begin(const Key& key, void* handle, const CalibrationOps& ops,
               const Calibration& baseline, std::uint64_t now,
               std::uint64_t windowMs = 5000,
               std::uint64_t maxSampleAgeMs = 1500) {
        if (active_ || !key.valid() || !handle || !ops.complete() || !baseline.valid())
            return false;
        key_ = key;
        handle_ = handle;
        ops_ = ops;
        baseline_ = baseline;
        startedAt_ = lastNow_ = now;
        windowMs_ = windowMs;
        maxSampleAgeMs_ = maxSampleAgeMs;
        lastReceipt_ = pendingReceipt_ = 0;
        initialized_ = false;
        confidence_ = 0;
        candidate_.reset();
        outcome_ = CalibrationOutcome::None;
        active_ = true;
        return true;
    }

    bool prepare(const Key& key, void* handle, const CalibrationOps& ops,
                 std::uint64_t now, std::uint64_t sampleReceipt) {
        if (!admit(key, handle, ops)) return false;
        pendingReceipt_ = 0;
        if (!admitTime(now)) return false;
        if (candidate_ || !fresh(now, sampleReceipt) || sampleReceipt <= lastReceipt_)
            return false;
        if (!initialized_) {
            ops_.reset(handle_);
            ops_.setMode(handle_, 3); // HC Stillness | SensorFusion
            initialized_ = true;
        }
        lastReceipt_ = pendingReceipt_ = sampleReceipt;
        return true;
    }

    // true means one authorized, processed observation was accepted. Read
    // candidate() to distinguish convergence from an ordinary confidence poll.
    bool observe(const Key& key, void* handle, const CalibrationOps& ops,
                 std::uint64_t now, std::uint64_t sampleReceipt, bool processed) {
        if (!admit(key, handle, ops)) return false;
        const std::uint64_t authorized = pendingReceipt_;
        pendingReceipt_ = 0; // No receipt can be observed twice, even on failure.
        if (!admitTime(now)) return false;
        if (!authorized || sampleReceipt != authorized || !fresh(now, sampleReceipt))
            return false;
        if (!processed) {
            rollback(CalibrationOutcome::ProcessFailed);
            return false;
        }
        const float value = ops_.getConfidence(handle_);
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
            rollback(CalibrationOutcome::InvalidConfidence);
            return false;
        }
        confidence_ = value;
        // Compare float to float: 0.99f itself must not pass the user's >99% gate.
        if (confidence_ > 0.99f) {
            const float missing = std::numeric_limits<float>::quiet_NaN();
            Calibration next{missing, missing, missing, static_cast<int>(confidence_ * 10.0f)};
            ops_.getOffset(handle_, &next.x, &next.y, &next.z);
            if (!next.valid()) {
                rollback(CalibrationOutcome::InvalidOffset);
                return false;
            }
            candidate_ = next;
        }
        return true;
    }

    // Persistence is external: save candidate first, then supply commit=true.
    // A missing candidate cannot commit; the active transaction is retained.
    bool finish(const Key& key, void* handle, const CalibrationOps& ops, bool commit) {
        if (!admit(key, handle, ops)) return false;
        if (!commit) {
            rollback(CalibrationOutcome::PersistFailed);
            return false;
        }
        if (!candidate_) return false;
        apply(*candidate_);
        active_ = false;
        pendingReceipt_ = 0;
        outcome_ = CalibrationOutcome::Succeeded;
        return true;
    }

    void cancel(const Key& key, void* handle, const CalibrationOps& ops,
                CalibrationOutcome reason = CalibrationOutcome::Aborted) {
        if (admit(key, handle, ops)) rollback(reason);
    }

    bool active() const { return active_; }
    CalibrationOutcome outcome() const { return outcome_; }
    float confidence() const { return confidence_; }
    std::optional<Calibration> candidate() const { return candidate_; }

private:
    bool admit(const Key& key, void* handle, const CalibrationOps& ops) {
        if (!active_) return false;
        if (!(key == key_) || handle != handle_ || !handle || !ops.complete() || !(ops == ops_)) {
            active_ = false;
            pendingReceipt_ = 0;
            candidate_.reset();
            outcome_ = CalibrationOutcome::SourceRetired;
            return false; // Never restore via an old or replacement handle/library.
        }
        return true;
    }
    bool admitTime(std::uint64_t now) {
        if (now < lastNow_) {
            rollback(CalibrationOutcome::ClockRegressed);
            return false;
        }
        lastNow_ = now;
        if (now - startedAt_ >= windowMs_) {
            rollback(CalibrationOutcome::TimedOut);
            return false;
        }
        return true;
    }
    bool fresh(std::uint64_t now, std::uint64_t receipt) const {
        return receipt != 0 && receipt <= now && now - receipt <= maxSampleAgeMs_;
    }
    void apply(const Calibration& value) {
        ops_.setOffset(handle_, value.x, value.y, value.z, value.weight);
        ops_.setMode(handle_, 0); // HC Manual
    }
    void rollback(CalibrationOutcome reason) {
        apply(baseline_);
        active_ = false;
        pendingReceipt_ = 0;
        candidate_.reset();
        outcome_ = reason;
    }

    Key key_;
    void* handle_ = nullptr;
    CalibrationOps ops_;
    Calibration baseline_;
    std::uint64_t startedAt_ = 0, lastNow_ = 0;
    std::uint64_t windowMs_ = 5000, maxSampleAgeMs_ = 1500;
    std::uint64_t lastReceipt_ = 0, pendingReceipt_ = 0;
    bool active_ = false, initialized_ = false;
    float confidence_ = 0;
    std::optional<Calibration> candidate_;
    CalibrationOutcome outcome_ = CalibrationOutcome::None;
};

} // namespace ymcc::controller_motion
