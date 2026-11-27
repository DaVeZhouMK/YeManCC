#pragma once
#include <cstdint>

namespace ymcc::msi {
struct DinputReprobeObservation {
    bool enumerated=false, completeNoMatch=false;
    long enumerationHresult=0;
    unsigned unreadableCandidates=0;
    int vendorCandidates=0, dinputPidCandidates=0, rejectedNonMainInterface=0, rejectedPath=0;
    bool operator==(const DinputReprobeObservation&) const = default;
};

struct DinputReprobeLogState {
    struct Context {
        std::uint64_t episode=0,power=0,delivery=0;
        unsigned phase=0,episodeOpens=0,bindCount=0;
        bool operator==(const Context&) const = default;
    };
    Context context;
    DinputReprobeObservation last;
    bool known=false;

    bool repeat(const Context& current,const DinputReprobeObservation& observation,bool bound,bool reoffer) noexcept {
        const bool cleanMiss=!bound&&observation.completeNoMatch;
        const bool repeated=cleanMiss&&known&&!reoffer&&current==context&&observation==last;
        known=cleanMiss;
        context=current;
        last=observation;
        return repeated;
    }
};
} // namespace ymcc::msi
