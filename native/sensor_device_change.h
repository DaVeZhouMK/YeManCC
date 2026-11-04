#pragma once
// An event hint for the existing sensor STA. No sensor API, writer or timer owner.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include "sensor_inventory_cache.h"

namespace ymcc::sensors::device_change {
class Hint {
    struct Pending {
        std::weak_ptr<const std::atomic<std::uint64_t>> owner;
        std::uint64_t generation=0,power=0,serial=0,due=0;
    };
    mutable std::mutex mutex_;
    std::optional<Pending> pending_;
    std::weak_ptr<const std::atomic<std::uint64_t>> lastOwner_;
    std::uint64_t next_=0,lastPower_=0,lastAttempt_=0;
    bool attempted_=false;
    static std::uint64_t add(std::uint64_t a,std::uint64_t b) noexcept {
        const auto max=(std::numeric_limits<std::uint64_t>::max)();return a>max-b?max:a+b;
    }
    static bool sameOwner(const std::weak_ptr<const std::atomic<std::uint64_t>>& a,
        const std::shared_ptr<const std::atomic<std::uint64_t>>& b) {
        const auto pinned=a.lock();return pinned&&b&&pinned.get()==b.get()&&!pinned.owner_before(b)&&!b.owner_before(pinned);
    }
    static bool matches(const Pending& pending,const inventory::Ticket& current) {
        return current.power&&pending.power==current.power&&pending.generation==current.binding.bindingGeneration&&
            windows_motion::current(current.binding)&&sameOwner(pending.owner,current.binding.activeBinding);
    }
public:
    static constexpr std::uint64_t mergeMs=250,minAttemptIntervalMs=2000;
    // Keep only one hint for a current scope. It never keeps a sensor sink alive.
    bool note(const inventory::Ticket& current,std::uint64_t now) {
        if(!current.power||!windows_motion::current(current.binding))return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if(pending_&&matches(*pending_,current))return false;
        if(next_==(std::numeric_limits<std::uint64_t>::max)())return false;
        auto due=add(now,mergeMs);
        if(attempted_&&lastPower_==current.power&&sameOwner(lastOwner_,current.binding.activeBinding))
            due=(std::max)(due,add(lastAttempt_,minAttemptIntervalMs));
        pending_=Pending{current.binding.activeBinding,current.binding.bindingGeneration,current.power,++next_,due};
        return true;
    }
    // Shortens only the existing STA wait while one event hint is pending.
    std::uint32_t waitMs(const inventory::Ticket& current,std::uint64_t now,std::uint32_t base) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!pending_)return base;
        if(!matches(*pending_,current)){pending_.reset();return base;}
        return now>=pending_->due?0:static_cast<std::uint32_t>((std::min)(std::uint64_t{base},pending_->due-now));
    }
    std::optional<std::uint64_t> claim(const inventory::Ticket& current,std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!pending_)return {};
        if(!matches(*pending_,current)){pending_.reset();return {};}
        if(now<pending_->due)return {};
        const auto serial=pending_->serial;
        lastOwner_=pending_->owner;lastPower_=pending_->power;lastAttempt_=now;attempted_=true;
        pending_.reset();return serial;
    }
    // Stop/suspend cancels queued work; keep the throttle across a failed cold start.
    void reset(){std::lock_guard<std::mutex> lock(mutex_);pending_.reset();}
};
} // namespace ymcc::sensors::device_change
