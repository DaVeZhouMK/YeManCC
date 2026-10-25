#pragma once
#include <cstdint>

namespace ymcc::steamOriginal {
enum class RecoveryRoute { LegacyDynamic, SemiStatic };
inline const char* routeName(RecoveryRoute route) {
    return route==RecoveryRoute::LegacyDynamic ? "legacy-dynamic" : "semi-static";
}
// Independent legacy admission. S3/S4 PBT resumes have no mandatory 507/107.
// The timestamp is recorded once by the already-admitted power owner; a queue
// retry, tracker notification or route fallback cannot refresh this budget.
struct LegacyWakeProof {
    std::uint64_t generation=0,epoch=0,eventFileTime=0,boundaryFileTime=0;
    std::uint64_t boundaryGeneration=0,armedTick=0,latestEntryFileTime=0;
    bool admitted=false,sourceMatched=false,operationAllowed=false;
    bool operator==(const LegacyWakeProof&) const = default;
    bool valid(std::uint64_t requestedGeneration,std::uint64_t requestedEpoch,
        std::uint64_t nowFileTime,std::uint64_t nowTick) const {
        return requestedGeneration&&requestedEpoch&&generation==requestedGeneration&&epoch==requestedEpoch&&
            admitted&&sourceMatched&&operationAllowed&&boundaryGeneration&&boundaryFileTime&&
            eventFileTime>boundaryFileTime&&eventFileTime>latestEntryFileTime&&nowFileTime>=eventFileTime&&
            nowFileTime-eventFileTime<60000ULL*10000ULL&&armedTick&&nowTick>=armedTick&&nowTick-armedTick<60000ULL;
    }
};
} // namespace ymcc::steamOriginal
