#pragma once
#include "json.hpp"

namespace ymcc::inputEdgeLogScope {
inline constexpr unsigned rowSchema=1,version=1;
inline constexpr char marker[]="edgeScopeSchema";

inline nlohmann::json fields() {
    return {
        {"hookFlagMeaning","keyboard:1=extended,2=lower-IL-injected,16=injected,128=up;mouse:1=injected,2=lower-IL-injected;flags-are-not-sender-PID"},
        {"selfReturnMeaning","count-based prefix assumption only;not OS or consumer acceptance;inspection capped at64"},
        {"coverage","diagnostic-only passive LL callbacks, raw keyboard/mouse and observed YMCC own send returns;upstream hooks may suppress;Windows can silently remove timed-out hooks;installed handle is not liveness proof"},
        {"sequenceScope","callback-arrival order within this writer;not hardware,network or cross-process ordering"}
    };
}
}
