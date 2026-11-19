#pragma once
#include "input_idle_diagnostics.h"
#include "input_idle_scope.h"
#include "input_idle_windows.h"
#include "json.hpp"
namespace ymcc::idle {
inline nlohmann::json environmentJson(const Environment& e, bool includeScope = true) {
    DWORD dimSec=0,offSec=0;
    const bool dimOk=selectedTimeout(e,true,dimSec),offOk=selectedTimeout(e,false,offSec);
    nlohmann::json result=nlohmann::json::object();
    result.emplace("lastInputInfoOk",e.lastInputOk);
    result.emplace("lastInputTick32",e.lastInputTick);
    result.emplace("lastInputError",e.lastInputError);
    result.emplace("sessionInputIdleValid",e.idleValid);
    result.emplace("sessionInputIdleMs",e.idleMs);
    result.emplace("acLineStatus",e.acLineStatus);
    result.emplace("powerSourceKnown",e.sourceKnown);
    result.emplace("activePlanStatus",e.activePlanStatus);
    result.emplace("acDimReadStatus",e.acDimStatus);
    result.emplace("dcDimReadStatus",e.dcDimStatus);
    result.emplace("acOffReadStatus",e.acOffStatus);
    result.emplace("dcOffReadStatus",e.dcOffStatus);
    result.emplace("acDimTimeoutSec",e.acDimSec);
    result.emplace("dcDimTimeoutSec",e.dcDimSec);
    result.emplace("acOffTimeoutSec",e.acOffSec);
    result.emplace("dcOffTimeoutSec",e.dcOffSec);
    result.emplace("selectedDimTimeoutKnown",dimOk);
    result.emplace("selectedDimTimeoutSec",dimSec);
    result.emplace("selectedOffTimeoutKnown",offOk);
    result.emplace("selectedOffTimeoutSec",offSec);
    result.emplace("dimTimeoutComparedWithSessionInput",timeoutElapsed(e.idleValid,e.idleMs,dimOk,dimSec));
    result.emplace("offTimeoutComparedWithSessionInput",timeoutElapsed(e.idleValid,e.idleMs,offOk,offSec));
    result.emplace("executionStateStatus",e.executionStatus);
    result.emplace("executionStateKnown",e.executionStatus==0);
    result.emplace("executionStateFlags",e.executionStatus==0?nlohmann::json(e.executionFlags):nlohmann::json(nullptr));
    result.emplace("displayRequired",e.executionStatus==0?nlohmann::json((e.executionFlags&ES_DISPLAY_REQUIRED)!=0):nlohmann::json(nullptr));
    result.emplace("systemRequired",e.executionStatus==0?nlohmann::json((e.executionFlags&ES_SYSTEM_REQUIRED)!=0):nlohmann::json(nullptr));
    if (includeScope) result.emplace("scope", EnvironmentScope);
    return result;
}
inline nlohmann::json padJson(const Pad& p) {
    nlohmann::json result=nlohmann::json::object();
    result.emplace("buttons",p.buttons);
    result.emplace("back",p.back);
    result.emplace("screen",p.screen);
    result.emplace("sticks",p.sticks);
    result.emplace("triggers",p.triggers);
    return result;
}
inline nlohmann::json channelJson(const Channel& c,uint64_t now) {
    nlohmann::json result=nlohmann::json::object();
    result.emplace("samples",c.samples);
    result.emplace("contentChanges",c.changes);
    result.emplace("rawActiveFrames",c.rawActiveFrames);
    result.emplace("meaningfulFrames",c.meaningfulFrames);
    result.emplace("smallAnalogOnlyFrames",c.smallAnalogOnlyFrames);
    result.emplace("lastMeaningfulAgeMs",c.lastMeaningfulTick?nlohmann::json(now>=c.lastMeaningfulTick?now-c.lastMeaningfulTick:0):nlohmann::json(nullptr));
    result.emplace("haveLast",c.haveLast);
    result.emplace("lastContent",c.haveLast?padJson(c.last):nlohmann::json(nullptr));
    return result;
}
} // namespace ymcc::idle
