#pragma once
#include <cstdint>
#include <mutex>

namespace ymcc::sleeppower {
struct RecoveryResumeCommitStamp {
    std::uint64_t generation=0,suspendSerial=0,tick=0;
    bool operator==(const RecoveryResumeCommitStamp&) const noexcept = default;
};
// Native-only, separate from the mapping ABI and the display wake timestamp.
class RecoveryResumeCommit {
    mutable std::mutex mutex_;
    RecoveryResumeCommitStamp stamp_;
public:
    void record(RecoveryResumeCommitStamp stamp) noexcept {
        std::lock_guard lock(mutex_);
        stamp_=stamp;
    }
    RecoveryResumeCommitStamp read() const noexcept {
        std::lock_guard lock(mutex_);
        return stamp_;
    }
};
}
