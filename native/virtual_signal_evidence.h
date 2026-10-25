#pragma once
#include <cstdint>
// Passive evidence only. No device/clock/heap/thread APIs and no input admission.
namespace ymcc::virtualSignal {
struct Window {
    std::uint64_t observations=0, attempts=0, accepted=0, neutral=0,
        neutralAccepted=0, repeatedKnownSourceReads=0, clockRegressions=0,
        selectedSourceKnownReads=0, selectedSourceLiveReads=0, selectedSourceNonzeroReads=0;
};
struct Decision {
    bool record=false, wireClockKnown=false;
    std::uint64_t bucket=0, elapsedMs=0;
    Window window{};
};
class Sampler {
    Window pending_{};
    std::uint64_t lastBucket_=0, lastUnknownBucket_=0, lastClock_=0, lastUnknownClock_=0, windowStart_=0;
    std::uint64_t lastRead_=0, lastPower_=0, lastRevision_=0, lastMode_=0;
    bool haveBucket_=false, haveUnknownBucket_=false, haveWindow_=false, haveRead_=false;
public:
    static constexpr std::uint64_t intervalMs=1000;
    Decision observe(std::uint64_t wireTick, std::uint64_t observedTick,
        bool attempted, bool accepted, bool neutral, std::uint64_t read,
        std::uint64_t power, std::uint64_t revision, std::uint64_t mode,
        bool sourceKnown=false, bool sourceLive=false, bool sourceNonzero=false) noexcept {
        ++pending_.observations;
        pending_.selectedSourceKnownReads+=sourceKnown;
        pending_.selectedSourceLiveReads+=sourceKnown&&sourceLive;
        pending_.selectedSourceNonzeroReads+=sourceKnown&&sourceLive&&sourceNonzero;
        pending_.attempts+=attempted; pending_.accepted+=accepted;
        pending_.neutral+=neutral; pending_.neutralAccepted+=neutral&&accepted;
        const bool sameSource=haveRead_ && power==lastPower_ && revision==lastRevision_ && mode==lastMode_;
        if (read && sameSource && lastRead_==read) ++pending_.repeatedKnownSourceReads;
        haveRead_=read!=0; lastRead_=read; lastPower_=power; lastRevision_=revision; lastMode_=mode;
        // Both native and Host select the first live request in the same existing
        // wire monotonicTick bucket. Internal SDK reports must not consume it.
        const bool known=attempted && wireTick!=0;
        const auto clock=known?wireTick:observedTick;
        if (!haveWindow_) {windowStart_=clock;haveWindow_=true;}
        if (known ? (haveBucket_ && clock<lastClock_) :
            (haveUnknownBucket_ && clock<lastUnknownClock_)) {++pending_.clockRegressions;return {};}
        const auto bucket=clock/intervalMs;
        if (known ? (haveBucket_ && bucket<=lastBucket_) :
            (haveUnknownBucket_ && bucket<=lastUnknownBucket_)) return {};
        Decision result{true,known,bucket,clock>=windowStart_?clock-windowStart_:0,pending_};
        if(known){haveBucket_=true;lastBucket_=bucket;lastClock_=clock;}
        else {haveUnknownBucket_=true;lastUnknownBucket_=bucket;lastUnknownClock_=clock;}
        windowStart_=clock;pending_={};
        return result;
    }
};
inline bool excessiveCallbackRate(std::uint64_t ticks,std::uint64_t windowMs,
                                  std::uint64_t expectedHz) noexcept {
    return windowMs && expectedHz && static_cast<long double>(ticks)*1000.0L >
        static_cast<long double>(windowMs)*static_cast<long double>(expectedHz)*2.0L;
}
}
