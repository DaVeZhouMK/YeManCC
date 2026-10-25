#pragma once
// Consumer fault policy. Read-only CDP facts, not a guess from a version number or console keyword.
// Keep source URLs and messages private; only the reviewed reason code crosses into settings/logs.
#include "json.hpp"
#include <string>
#include <algorithm>
namespace ymcc::deckyhealth {
using Json=nlohmann::json;
inline bool terminal(const std::string& code){return code=="decky-frontend-incompatible"||code=="decky-render-failed"||code=="loader-crash-loop";}
inline bool coreUrl(const std::string& url){
    if(url.size()>1024)return false;
    const auto pathname=url.substr(0,url.find_first_of("?#"));
    for(const char* prefix:{"http://127.0.0.1:1337/frontend/","http://localhost:1337/frontend/"}){
        const std::string p=prefix;if(pathname.rfind(p,0)==0&&pathname.size()>p.size()&&pathname.find("..",p.size())==std::string::npos&&url.find_first_of("\\\r\n") ==std::string::npos)return true;
    }
    return false;
}
inline std::string exception(const Json& packet,int context){
    try{
        if(context<=0||packet.value("method",std::string{})!="Runtime.exceptionThrown")return {};
        const auto& d=packet.at("params").at("exceptionDetails");
        if(d.value("executionContextId",0)!=context)return {};
        const auto& e=d.at("exception");if(e.value("subtype",std::string{})!="error")return {};
        const auto type=e.value("className",std::string{}),text=e.value("description",std::string{});
        if(text.empty()||text.size()>8192||text.find("/plugins/")!=std::string::npos||text.find("decky://decky/plugin/")!=std::string::npos||text.find("decky://decky/legacy_plugin/")!=std::string::npos)return {};
        if(!((type=="TypeError"&&(text.find("Cannot read properties of undefined")!=std::string::npos||text.find("Cannot read properties of null")!=std::string::npos||text.find("is not a function")!=std::string::npos))||(type=="ReferenceError"&&text.find("is not defined")!=std::string::npos)))return {};
        const auto& frames=d.at("stackTrace").at("callFrames");
        if(!frames.is_array()||frames.empty()||frames.size()>64||!coreUrl(frames.front().value("url",std::string{})))return {};
        for(size_t n=0;n<(std::min)(size_t(8),frames.size());++n){const auto url=frames[n].value("url",std::string{});if(url.find("/plugins/")!=std::string::npos||url.rfind("decky://decky/",0)==0)return {};}
        const auto boot=frames.front().value("url",std::string{});const auto entry=boot.substr(0,boot.find_first_of("?#"));if(entry!="http://127.0.0.1:1337/frontend/index.js"&&entry!="http://localhost:1337/frontend/index.js")return {};
        return "decky-frontend-incompatible";
    }catch(...){return {};}
}
inline bool boundarySignal(const Json& packet,int context,const std::string& run){
    try{
        if(context<=0||packet.value("method",std::string{})!="Runtime.bindingCalled")return false;
        const auto& p=packet.at("params");if(p.value("executionContextId",0)!=context||p.value("name",std::string{})!="__YMCC_DECKY_HEALTH_V1")return false;
        const auto raw=p.value("payload",std::string{});if(raw.size()>160)return false;
        const auto value=Json::parse(raw);return value.size()==2&&value.value("runId",std::string{})==run&&value.value("code",std::string{})=="decky-render-failed";
    }catch(...){return false;}
}
// A terminal watch packet is already scoped to the authenticated binding.
// Keep its admission until the UI owner consumes it; automatic retries must not
// replace the runId underneath that queued event. All access is under Runtime's
// state mutex. This stores no token, process handle, timer or settings data.
class PendingStop {
    std::uint64_t intent_=0;
    std::string run_,reason_;
public:
    bool capture(const Json& detail,std::uint64_t currentIntent,const std::string& bindingRun){
        try{
            const auto reason=detail.value("reason",std::string{}),run=detail.value("runId",std::string{});
            if(!currentIntent||bindingRun.size()!=32||bindingRun.find_first_not_of("0123456789abcdef")!=std::string::npos||
                detail.value("bootstrapStage",std::string{})!="terminal"||run!=bindingRun||
                (reason!="decky-render-failed"&&reason!="decky-frontend-incompatible"))return false;
            if(active(currentIntent))return reason_==reason&&run_==run;
            reason_=reason;run_=run;intent_=currentIntent;return true;
        }catch(...){return false;}
    }
    bool active(std::uint64_t currentIntent)const{return intent_&&intent_==currentIntent&&!run_.empty();}
    bool matches(const Json& event,std::uint64_t currentIntent,const std::string& brokerRun)const{
        try{return active(currentIntent)&&run_==brokerRun&&event.value("runId",std::string{})==run_&&event.value("reason",std::string{})==reason_;}
        catch(...){return false;}
    }
    void clear(){intent_=0;run_.clear();reason_.clear();}
};
// Recheck a queued safety event on the YMCC owner thread; stale contexts/revisions cannot close a new session.
inline bool matches(const Json& event,const Json& state,const std::string& currentRun){
    try{
        const auto code=event.value("reason",std::string{});if(!terminal(code)||!state.value("enabled",false))return false;
        if(code=="loader-crash-loop")return state.value("phase",std::string{})=="failed"&&state.value("reason",std::string{})==code&&event.at("revision")==state.at("revision");
        const auto phase=state.value("phase",std::string{});
        // A retained exact frontend owns a live binding/watch but no Loader PID.
        // Its confirmed fatal page must use the same normal OFF path; otherwise
        // the watch deinitializes Decky while YMCC discards the owner notification.
        // Admission is narrow: current observed Steam, exact retained phase/reason,
        // no owned Loader, and the still-current authenticated run below.
        const bool retained=phase=="frontend-attached"&&state.value("steamKnown",false)&&state.value("steamRunning",false)&&
            state.value("reason",std::string{})=="retained-frontend-connected"&&state.at("pid").get<std::uint64_t>()==0;
        return (phase=="starting"||phase=="loader-started"||retained)&&!currentRun.empty()&&event.value("runId",std::string{})==currentRun;
    }catch(...){return false;}
}
}
