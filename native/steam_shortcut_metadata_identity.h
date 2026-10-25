#pragma once
#include "steam_native_record_diagnostic.h"
#include <cstdint>
#include <functional>
#include <string>
namespace ymcc::shortcutmetadata {
using Json=nlohmann::json;
using Read=ymcc::steamrecord::Read;
using Stable=ymcc::steamrecord::Stable;
using Clock=std::function<std::uint64_t()>;
enum class Phase { Baseline, FollowOriginal };
struct Scope {
    std::uint32_t steamPid=0,gamePid=0,windowsSession=0;
    std::uint64_t steamBirth=0,gameBirth=0,generation=0,epoch=0;
    std::string steamPathKey,gamePathKey;
    bool operator==(const Scope&) const = default;
};
// Private in-memory witness: never serialize or log this object.
struct Witness {
    Scope scope;
    std::uint64_t moduleBase=0,moduleSize=0,account64=0,gameId64=0;
    std::uint32_t appid=0;
    std::uint64_t user=0,manager=0,node=0,record=0;
    bool valid=false;
};
struct Query {
    ymcc::steamrecord::Query native;
    std::uint32_t appid=0;
    Scope scope;
    Phase phase=Phase::FollowOriginal;
    Witness original;
    // Supplied only by the existing original tracker / Off / single-owner gate.
    bool originalTrackerBound=false;
};
struct Outcome { Json diagnostic; Witness witness; };
inline Outcome observe(const Query&,const Read&,const Stable&,const Clock& = {});
}
