#pragma once
#include <cstdint>

namespace ymcc::msi {
// Early recovery needs a fresh, physical PID_1901 game-input observation AND
// a confirmed, mode-2-owned route in the SAME Host epoch. Enumeration, an old
// lease, a virtual XInput slot, or a successful write alone is not authority.
struct ModeRecoveryFacts {
    bool knownBoard=false, targetEnabled=false, routeArmed=false;
    bool routeConfirmed=false, modeWriteOwned=false, exitFence=false;
    uint64_t hostEpoch=0, confirmedEpoch=0, observedEpoch=0;
    uint64_t observedTick=0, now=0;
};
inline bool observedXinputReturnAdmitted(const ModeRecoveryFacts& f) {
    return f.knownBoard && f.targetEnabled && f.routeArmed && !f.routeConfirmed &&
        f.modeWriteOwned && !f.exitFence && f.hostEpoch!=0 &&
        f.confirmedEpoch==f.hostEpoch && f.observedEpoch==f.hostEpoch &&
        f.observedTick!=0 && f.now>=f.observedTick && f.now-f.observedTick<=2000;
}
}
