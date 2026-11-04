#pragma once
#include <cstdint>
#include <limits>
namespace ymcc::staticwake {
// Initial collection only: admit one bounded Windows read unit every 4 ms.
// Commit from the actual admission time, never accumulate catch-up credits.
// Strict readback, writes and restore do not use this pacer.
struct CollectionReadPace {
    using U64=std::uint64_t;
    static constexpr U64 gapMs=4;
    U64 next=0,admitted=0,requestedWaitMs=0;
    void reset(){next=0;admitted=0;requestedWaitMs=0;}
    U64 remaining(U64 now) const {return now<next?next-now:0;}
    void admit(U64 now){
        next=now>std::numeric_limits<U64>::max()-gapMs?std::numeric_limits<U64>::max():now+gapMs;
        ++admitted;
    }
    template<class Clock,class Allowed,class Wait>
    bool before(Clock clock,Allowed allowed,Wait sleep) {
        while(allowed()) {
            const auto now=clock(),wait=remaining(now);
            if(!wait){admit(now);return allowed();}
            const auto slice=wait<gapMs?wait:gapMs;
            requestedWaitMs+=slice;sleep(slice);
        }
        return false;
    }
};
// One pending event, one scan after 60s. No repeating poll and no callback counting.
struct ScanDelay {
    using U64=std::uint64_t;
    static constexpr U64 delayMs=60000;
    U64 due=0,lastWake=0;bool pending=false,startupSeen=false;
    void cancel(){pending=false;due=0;}
    bool arm(U64 now,bool startup,U64 wakeGeneration=0) {
        if(startup){if(startupSeen)return false;startupSeen=true;}
        else if(wakeGeneration){if(wakeGeneration==lastWake)return false;lastWake=wakeGeneration;}
        if(now>std::numeric_limits<U64>::max()-delayMs)return false;
        due=now+delayMs;pending=true;return true;
    }
    struct Decision {bool dispatch=false;U64 remaining=0;};
    Decision poll(U64 now,bool enabled,bool awake) {
        if(!pending)return {};
        if(!enabled||!awake){cancel();return {};}
        if(now<due)return {false,due-now};
        cancel();return {true,0};
    }
};
}
