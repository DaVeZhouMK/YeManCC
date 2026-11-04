#pragma once
// Higher recovery tier: one close and one open, each observed in the SAME
// Steam/account. Runs inside the existing cancellable private native child.
#include "decky_sidebar_session.h"
namespace ymcc::deckyrepair::bigpicture {
using Json=nlohmann::json;
template<class Current,class Read,class Send,class Now,class Wait>
Json run(const steamsession::Session& session,Current current,Read read,Send send,Now now,Wait wait){
    bool entered=false;
    const char* stage="initial-read";
    const char* lastMode="unknown";
    // Diagnostics use only existing same-session reads. Dispatch acceptance
    // is not a mode transition, and these fields never grant replay consent.
    const auto fail=[&](const char* reason){return Json{{"ok",false},{"mutated",entered},{"uncertain",entered},{"reason",reason},
        {"launchStage",stage},{"launchMode",lastMode}};};
    const auto observedMode=[&](const Json& mode){
        const auto value=mode.value("mode",std::string{});
        lastMode=value=="desktop"?"desktop":value=="bigpicture"?"bigpicture":"unknown";
        return value;
    };
    const auto same=[&](const Json& mode){return mode.value("known",false)&&mode.value("running",false)&&
        mode.value("pid",0u)==session.pid&&mode.value("birth",std::string{})==std::to_string(session.created)&&
        session.account&&deckysession::positiveAccount(mode.value("observedAccount",Json()))==session.account&&
        mode.value("deckyAccountWatchAvailable",false);};
    try{
        const auto initial=read();
        if(!current()||!same(initial))return fail("decky-bigpicture-state-changed");
        if(observedMode(initial)!="bigpicture")return fail("decky-bigpicture-state-changed");
        const auto started=now();
        const auto await=[&](const char* expected,unsigned long long deadline){
            while(current()&&now()<deadline){
                lastMode="unknown";
                const auto mode=read();
                if(!current()||now()>=deadline)return false;
                const auto observed=deckysession::positiveAccount(mode.value("observedAccount",Json()));
                if(observed&&observed!=session.account)return false;
                if(same(mode)&&observedMode(mode)==expected)return true;
                wait();
            }
            return false;
        };
        if(!current())return fail("decky-bigpicture-cancelled");
        entered=true; // External dispatch entered: never replay an uncertain result.
        stage="close-submit";
        if(!send("desktop"))return fail("decky-bigpicture-close-unconfirmed");
        stage="close-readback";
        if(!await("desktop",started+12000))return fail("decky-bigpicture-close-unconfirmed");
        if(!current())return fail("decky-bigpicture-cancelled");
        stage="open-submit";
        if(!send("bigpicture"))return fail("decky-bigpicture-open-unconfirmed");
        stage="open-readback";
        if(!await("bigpicture",started+24000))return fail("decky-bigpicture-open-unconfirmed");
        return {{"ok",true},{"mutated",true},{"reason","decky-bigpicture-reopened"},
            {"launchStage","complete"},{"launchMode",lastMode}}; // Mode receipt only.
    }catch(...){return fail("decky-bigpicture-result-unconfirmed");}
}
}
