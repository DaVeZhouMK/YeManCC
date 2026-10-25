#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace ymcc::monitorrecovery {
// Slow recovery on the existing collector, with one owned transaction at a
// time. A core upstream may retry indefinitely while YMCC still needs it;
// every completed failure waits at least 30 seconds, including a busy mutex.
struct RetryBudget {
    static constexpr std::uint64_t retryDelayMs=30000;
    unsigned attempts=0;std::uint64_t retryAt=0,generation=0,demandGeneration=0;bool waiting=false;
    bool observeGeneration(std::uint64_t current,bool ready,std::uint64_t demand=1) noexcept {
        if(!ready||!current||!demand||(generation==current&&demandGeneration==demand))return false;
        generation=current;demandGeneration=demand;healthy();return true;
    }
    void healthy() noexcept {attempts=0;retryAt=0;waiting=false;}
    void started() noexcept {if(attempts!=(std::numeric_limits<unsigned>::max)())++attempts;waiting=false;retryAt=0;}
    bool failed(int,std::uint64_t now,bool=true) noexcept {
        retryAt=now+retryDelayMs;waiting=true;return true;
    }
    bool due(std::uint64_t now) noexcept {
        if(!waiting||now<retryAt)return false;
        waiting=false;return true;
    }
};

// This runs only on the existing monitor collector. A readable mapping is
// allowed to recover after wake; it cannot suppress recovery indefinitely
// when its producer has already failed the ordinary freshness budget.
struct StalledProducer {
    static constexpr std::uint64_t graceMs = 15000;
    bool tracking = false;
    std::uint64_t sinceTick = 0;
    void reset() noexcept { tracking = false; sinceTick = 0; }
    bool observe(bool readable, bool sharedOk, bool fresh, bool resumeWait,
                 std::uint64_t nowTick, std::uint64_t freshnessBudgetMs) noexcept {
        if (!readable || !sharedOk || fresh || resumeWait) { reset(); return false; }
        if (!tracking || nowTick < sinceTick) {
            tracking = true; sinceTick = nowTick; return false;
        }
        const auto wait = (std::max)(graceMs, freshnessBudgetMs);
        return nowTick - sinceTick >= wait;
    }
};
}
