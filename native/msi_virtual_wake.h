#pragma once
#include <cstdint>
namespace ymcc::msi {
// Engineering ceiling for MSI's mode-switch/re-enumeration wake route. It is
// not a measured firmware latency. Other families keep their existing budget.
inline constexpr uint64_t virtualWakeMaxAgeMs=60000;
struct VirtualWakeFacts {
    bool knownBoard=false,targetEnabled=false,powerReady=false,exitFence=false;
    uint64_t generation=0,currentGeneration=0,armTick=0,now=0;
};
inline bool virtualWakeCurrent(const VirtualWakeFacts& f) {
    return f.knownBoard && f.targetEnabled && !f.exitFence && f.generation!=0 &&
        f.generation==f.currentGeneration && f.armTick!=0 && f.now>=f.armTick &&
        f.now-f.armTick<virtualWakeMaxAgeMs;
}
struct VirtualWakeRearmFacts : VirtualWakeFacts {
    bool startBlocked=false,releaseRestored=false,quarantined=false;
    bool visibilityJournalActive=false,hostPresent=false,transportFault=false,faultHold=false;
    uint64_t rearmedGeneration=0;
};
inline bool virtualWakeMayRearm(const VirtualWakeRearmFacts& f) {
    return virtualWakeCurrent(f) && f.powerReady && f.startBlocked && f.releaseRestored &&
        !f.quarantined && !f.visibilityJournalActive && !f.hostPresent &&
        !f.transportFault && !f.faultHold && f.rearmedGeneration!=f.generation;
}
inline bool virtualWakeMayWaitSource(const VirtualWakeFacts& f,bool routeArmed,uint64_t hostEpoch) {
    // Waiting does not authorize a source, hide an interface, or write a mode.
    return virtualWakeCurrent(f) && f.powerReady && routeArmed && hostEpoch!=0;
}
}
