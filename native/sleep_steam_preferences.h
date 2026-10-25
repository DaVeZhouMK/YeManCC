#pragma once
#include "json.hpp"
namespace ymcc::sleepprefs {
using Json=nlohmann::json;
inline constexpr bool kAutoWakeRecoveryDefaultEnabled=true;
// Deprecated/abandoned: disabling Steam Overlay is no longer a wake strategy.
inline constexpr bool kOverlayOffFixDeprecated=true;
inline constexpr bool kOverlayOffFixDefaultEnabled=false;
inline bool autoWakePreference(const Json& sleep) {
    const auto choice=sleep.find("autoWakeRecovery");
    if(choice==sleep.end())return kAutoWakeRecoveryDefaultEnabled;
    // Preserve an explicit off; malformed saved values never enable writes.
    return choice->is_boolean()&&choice->get<bool>();
}
inline Json retiredOverlayReply() {
    return {{"ok",false},{"accepted",false},{"enabled",false},{"deprecated",true},
        {"userReachable",false},{"status","retired"},{"via","deprecated"},
        {"reason","sleep-steam-overlay-fix-deprecated"},{"pending",false},{"busy",false},
        {"desiredValue",nullptr},{"mutated",false},{"healthVerified",false}};
}
}
