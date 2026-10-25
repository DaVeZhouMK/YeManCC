#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <json.hpp>
namespace ymcc::msi::trace {
inline bool enabled(bool knownBoard,bool detailed){return knownBoard&&detailed;}
struct Context {
    uint64_t sequence=0,tick=0,hostEpoch=0,powerGeneration=0;
    unsigned sourceGeneration=0,sourcePid=0;
    bool routeArmed=false,routeConfirmed=false,sourceValid=false,returnOwed=false,recordValid=false;
    std::string ownerToken,sourcePath;
};
inline nlohmann::json row(const char* stage,const Context& c,nlohmann::json facts){
    return {{"formatVersion",1},{"traceSequence",c.sequence},{"observedTick",c.tick},
        {"stage",stage?stage:"unspecified"},{"hostEpoch",c.hostEpoch},{"ownerToken",c.ownerToken},
        {"powerGenerationAtObservation",c.powerGeneration},{"routeArmed",c.routeArmed},
        {"routeConfirmed",c.routeConfirmed},{"sourceValid",c.sourceValid},{"returnOwed",c.returnOwed},
        {"sourceRecord",{{"valid",c.recordValid},{"generation",c.sourceGeneration},{"productId",c.sourcePid},{"interfacePath",c.sourcePath}}},
        {"facts",std::move(facts)},{"diagnosticOnly",true},{"traceChangesControlPolicy",false},
        {"observerRole","YMCC privileged metadata; not Steam or a verified unexempt observer"},
        {"externalSteamInterfaceMapping","unverified; never infer a HID identity from SDL index or count"}};
}
}
