#pragma once
#include <cstdint>

// MSI DInput source admission only. Read completion ordinals are NOT hardware
// report timestamps. Neutral is a release policy, never a corruption verdict.
namespace ymcc::msi::inputAdmission {
struct Key {
    std::uint64_t binding = 0, power = 0;
    bool operator==(const Key&) const = default;
};
struct Receipt {
    bool admitted = false, opened = false, firstPending = false;
    unsigned neutralReads = 0;
};
class Gate {
    Key key_{};
    std::uint64_t lastCompleted_ = 0;
    unsigned neutralReads_ = 0;
    bool haveKey_ = false, ready_ = false;
public:
    static constexpr unsigned requiredNeutralReads = 3;
    void reset() noexcept {
        key_ = {}; lastCompleted_ = 0; neutralReads_ = 0; haveKey_ = false; ready_ = false;
    }
    bool ready() const noexcept { return ready_; }
    unsigned neutralReads() const noexcept { return neutralReads_; }
    Receipt observe(Key key, bool neutral, std::uint64_t completed) noexcept {
        Receipt result;
        if (!haveKey_ || !(key_ == key)) {
            reset(); key_ = key; haveKey_ = true; result.firstPending = true;
        }
        if (!key.binding || !key.power || !completed) return result;
        if (ready_) { result.admitted = true; result.neutralReads = neutralReads_; return result; }
        if (!neutral || (lastCompleted_ && completed < lastCompleted_)) neutralReads_ = 0;
        else if (!lastCompleted_ || completed > lastCompleted_) ++neutralReads_;
        // Multiple reads completed in the same millisecond cannot advance readiness.
        lastCompleted_ = completed;
        result.neutralReads = neutralReads_;
        if (neutralReads_ >= requiredNeutralReads) {
            ready_ = true; result.admitted = true; result.opened = true;
        }
        return result;
    }
};
} // namespace ymcc::msi::inputAdmission
