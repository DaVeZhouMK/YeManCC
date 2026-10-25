#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
namespace ymcc::steamlink::waitpolicy {
using Tick = std::uint64_t;
constexpr Tick maximum = (std::numeric_limits<Tick>::max)();
constexpr Tick add(Tick now, Tick delay) noexcept { return delay > maximum-now ? maximum : now+delay; }
constexpr std::uint32_t timeout(Tick now, Tick due, bool scheduled) noexcept {
    return !scheduled ? UINT32_MAX : due <= now ? 0 : static_cast<std::uint32_t>((std::min)(due-now, Tick(UINT32_MAX-1)));
}
// Only a notified directory has a short deadline. Idle healthy captures keep
// their State deadline; neither directory noise nor hints authorize an action.
class Policy {
    bool directoryPending_ = false, readPending_ = false, readSeen_ = false;
    Tick directoryDue_ = 0, readDue_ = 0, lastRead_ = 0;
    void scheduleRead(Tick due) noexcept {
        readDue_=readPending_ ? (std::min)(readDue_,due) : due;
        readPending_=true;
    }
public:
    void directoryNotified(Tick now) noexcept {
        if (!directoryPending_) { directoryPending_=true; directoryDue_=add(now,100); }
    }
    bool directoryPending() const noexcept { return directoryPending_; }
    bool directoryReady(Tick now) const noexcept { return directoryPending_ && now>=directoryDue_; }
    void directoryChecked() noexcept { directoryPending_=false; }
    void relevantChanged(Tick now) noexcept {
        scheduleRead(readSeen_ ? (std::max)(now,add(lastRead_,500)) : now);
    }
    // Console config/overlay chatter can hint recovery, but association reads
    // keep the healthy cadence. A background event cannot postpone urgency.
    void backgroundChanged(Tick now) noexcept {
        scheduleRead(readSeen_ ? (std::max)(now,add(lastRead_,10000)) : now);
    }
    bool readReady(Tick now) const noexcept { return readPending_ && now>=readDue_; }
    void observed(Tick now) noexcept { readPending_=false; readSeen_=true; lastRead_=now; }
    bool scheduled() const noexcept { return directoryPending_ || readPending_; }
    Tick nextDue(Tick due) const noexcept {
        if(directoryPending_) due=(std::min)(due,directoryDue_);
        if(readPending_) due=(std::min)(due,readDue_);
        return due;
    }
};
} // namespace ymcc::steamlink::waitpolicy
