#pragma once
#include "input_idle_diagnostics.h"
#include "input_idle_windows.h"
#include "json.hpp"
namespace ymcc::idle {
inline nlohmann::json environmentJson(const Environment& e) {
    DWORD dimSec=0,offSec=0;
    const bool dimOk=selectedTimeout(e,true,dimSec),offOk=selectedTimeout(e,false,offSec);
    return {{"lastInputInfoOk",e.lastInputOk},{"lastInputTick32",e.lastInputTick},
        {"lastInputError",e.lastInputError},{"sessionInputIdleValid",e.idleValid},{"sessionInputIdleMs",e.idleMs},
        {"acLineStatus",e.acLineStatus},{"powerSourceKnown",e.sourceKnown},{"activePlanStatus",e.activePlanStatus},
        {"acDimReadStatus",e.acDimStatus},{"dcDimReadStatus",e.dcDimStatus},
        {"acOffReadStatus",e.acOffStatus},{"dcOffReadStatus",e.dcOffStatus},
        {"acDimTimeoutSec",e.acDimSec},{"dcDimTimeoutSec",e.dcDimSec},
        {"acOffTimeoutSec",e.acOffSec},{"dcOffTimeoutSec",e.dcOffSec},
        {"selectedDimTimeoutKnown",dimOk},{"selectedDimTimeoutSec",dimSec},
        {"selectedOffTimeoutKnown",offOk},{"selectedOffTimeoutSec",offSec},
        {"dimTimeoutComparedWithSessionInput",timeoutElapsed(e.idleValid,e.idleMs,dimOk,dimSec)},
        {"offTimeoutComparedWithSessionInput",timeoutElapsed(e.idleValid,e.idleMs,offOk,offSec)},
        {"executionStateStatus",e.executionStatus},{"executionStateKnown",e.executionStatus==0},
        {"executionStateFlags",e.executionStatus==0?nlohmann::json(e.executionFlags):nlohmann::json(nullptr)},
        {"displayRequired",e.executionStatus==0?nlohmann::json((e.executionFlags&ES_DISPLAY_REQUIRED)!=0):nlohmann::json(nullptr)},
        {"systemRequired",e.executionStatus==0?nlohmann::json((e.executionFlags&ES_SYSTEM_REQUIRED)!=0):nlohmann::json(nullptr)},
        {"scope","GetLastInputInfo is current-session evidence, not every HID/Steam activity; timeout comparison is not the OS display-idle timer or causal proof; powercfg requests supplies request owners"}};
}
inline nlohmann::json padJson(const Pad& p) {
    return {{"buttons",p.buttons},{"back",p.back},{"screen",p.screen},{"sticks",p.sticks},{"triggers",p.triggers}};
}
inline nlohmann::json channelJson(const Channel& c,uint64_t now) {
    return {{"samples",c.samples},{"contentChanges",c.changes},{"rawActiveFrames",c.rawActiveFrames},
        {"meaningfulFrames",c.meaningfulFrames},{"smallAnalogOnlyFrames",c.smallAnalogOnlyFrames},
        {"lastMeaningfulAgeMs",c.lastMeaningfulTick?nlohmann::json(now>=c.lastMeaningfulTick?now-c.lastMeaningfulTick:0):nlohmann::json(nullptr)},
        {"haveLast",c.haveLast},{"lastContent",c.haveLast?padJson(c.last):nlohmann::json(nullptr)}};
}
} // namespace ymcc::idle
