#pragma once
#include <cstdint>
#include "controller_persona.h"
#include <string>
namespace ymcc::msi::persona {
#ifdef YMCC_MSI_PERSONA_MODE_HANDOFF
inline constexpr bool buildEnabled=true;
#else
inline constexpr bool buildEnabled=false;
#endif
inline bool virtualPersona(const std::string& p){return ymcc::controllerPersona::currentOutput(p);}
struct Facts {
    bool enabled=false,knownBoard=false,startScope=false,personaOnly=false,targetWanted=false,pumpQuiesced=false;
    bool oldTargetPrepared=false,sourceKnown=false,routeArmed=false,routeConfirmed=false,modeOwned=false;
    bool powerReady=false,exitFence=false,releaseProven=false,visibilityHeld=false,quarantined=false,sourceStillCurrent=false;
    uint64_t epoch=0,power=0,boundPower=0;unsigned sourceGeneration=0;
    std::string from,to;
};
inline bool capture(const Facts& f){return f.enabled&&f.knownBoard&&f.startScope&&f.personaOnly&&f.targetWanted&&f.pumpQuiesced&&f.oldTargetPrepared&&f.sourceKnown&&f.routeArmed&&f.routeConfirmed&&f.modeOwned&&f.powerReady&&!f.exitFence&&f.epoch!=0&&f.power!=0&&f.power==f.boundPower&&f.sourceGeneration!=0&&virtualPersona(f.from)&&virtualPersona(f.to)&&f.from!=f.to;}
inline bool retain(const Facts& f){return capture(f)&&f.releaseProven&&f.visibilityHeld&&!f.quarantined&&f.sourceStillCurrent;}
inline bool resume(bool active,bool wanted,bool ready,bool exit,uint64_t oldEpoch,uint64_t newEpoch,uint64_t oldPower,uint64_t newPower){return active&&wanted&&ready&&!exit&&oldEpoch!=0&&newEpoch!=0&&newEpoch!=oldEpoch&&oldPower!=0&&oldPower==newPower;}
}
