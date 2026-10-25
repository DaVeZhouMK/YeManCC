#pragma once
#include <cstdint>
#include <mutex>

namespace ymcc::fan_resume {
// One recovery owner, one coalesced NEWER wake. No timer, host or writer.
class RequestOwner {
    mutable std::mutex mutex_;
    std::uint64_t owner_ = 0;
    std::uint64_t pending_ = 0;
public:
    bool begin(std::uint64_t generation) {
        if (!generation) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (owner_) {
            if (generation > owner_ && generation > pending_) pending_ = generation;
            return false;
        }
        owner_ = generation;
        return true;
    }
    std::uint64_t finish(std::uint64_t generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!generation || owner_ != generation) return 0;
        owner_ = 0;
        auto next = pending_;
        pending_ = 0;
        return next > generation ? next : 0;
    }
    std::uint64_t pending() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pending_;
    }
};
}
