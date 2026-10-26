#pragma once
#include <cstdint>
#include <limits>
namespace ymcc::staticwake {
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
