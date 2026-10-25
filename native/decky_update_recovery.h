#pragma once
// Decky-only continuation of an already active YMCC plugin across an update.
// Never grants CSS/Steam mode/start authority. Ordinary launches without update
// evidence remain observation-only; a user/safety off veto wins until a new process.
#include "steam_session_observer.h"
#include "json.hpp"
#include <mutex>
#include <string>
namespace ymcc::deckyupdate {
using Json=nlohmann::json;
inline bool fingerprint(const std::string& s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
inline bool loaderVersion(const std::string& s){if(s.size()<6||s.size()>32||s[0]!='v')return false;unsigned dots=0,digits=0;for(size_t i=1;i<s.size();++i){if(s[i]=='.'){if(!digits||++dots>2)return false;digits=0;}else if(s[i]>='0'&&s[i]<='9')++digits;else return false;}return dots==2&&digits;}
inline std::string ticket(const steamsession::Session& s){return s.pid&&s.created&&!s.root.empty()?std::to_string(s.pid)+":"+std::to_string(s.created):std::string{};}
// The panel may be hidden with no current peer. Runtime Job ownership, not UI
// visibility, proves the previous managed Loader should survive this update.
inline std::string handoffTicket(const steamsession::Session& s,bool enabled,const std::string& phase,DWORD loaderPid,bool brokerActive){return enabled&&phase=="loader-started"&&loaderPid&&brokerActive?ticket(s):std::string{};}
inline bool validTicket(const std::string& s){
    if(s.size()<3||s.size()>31||s.find_first_not_of("0123456789:")!=std::string::npos)return false;
    const auto p=s.find(':');if(p==std::string::npos||p==0||p+1>=s.size()||s.find(':',p+1)!=std::string::npos||s[0]=='0'||s[p+1]=='0')return false;
    try{const auto pid=std::stoull(s.substr(0,p)),birth=std::stoull(s.substr(p+1));return pid<=MAXDWORD&&pid&&birth;}catch(...){return false;}
}
inline Json residue(const Json& raw){
    try{
        if(!raw.is_object()||(raw.size()!=4&&raw.size()!=5)||raw.at("protocol")!=1||raw.at("active")!=true||raw.at("bridgePresent")!=true||!raw.at("frontendBuildId").is_string()||!fingerprint(raw.at("frontendBuildId").get<std::string>()))return Json::object();
        if(raw.size()==5&&(!raw.contains("loaderVersion")||!raw["loaderVersion"].is_string()||!loaderVersion(raw["loaderVersion"].get<std::string>())))return Json::object();
        return raw;
    }catch(...){return Json::object();}
}
class Recovery {
    mutable std::mutex mutex_;
    steamsession::Session resumed_;
    std::string explicitTicket_;
    bool veto_=false; // Manual/safety off cannot be undone by another cached read.
    static bool same(const steamsession::Session& a,const steamsession::Session& b){return a.pid&&a.created&&!a.root.empty()&&a==b;}
public:
    void configure(const std::string& value){std::lock_guard lock(mutex_);explicitTicket_=validTicket(value)?value:std::string{};}
    void veto(){std::lock_guard lock(mutex_);veto_=true;resumed_={};explicitTicket_.clear();}
    // The existing explicit sidebar ON action is fresh Decky-only consent.
    // Actual Big Picture mode and resource/hash checks still precede any launch.
    void userEnable(const steamsession::Session& s){std::lock_guard lock(mutex_);veto_=false;resumed_=s;explicitTicket_.clear();}
    // Run only from the shared verified mode reader's completion; no timer or
    // discovery socket. Recheck the same session again at launch admission.
    std::string consider(const steamsession::Session& s,const Json& state,const std::string& installedBuild,bool enabled,const std::string& installedLoader={}){
        std::lock_guard lock(mutex_);
        if(!same(s,resumed_))resumed_={};
        try{
            if(veto_||!enabled||!s.pid||!s.created||s.root.empty()||!state.value("known",false)||!state.value("running",false)||state.at("pid")!=s.pid||state.value("birth",std::string{})!=std::to_string(s.created))return {};
            const auto mode=state.value("mode",std::string{});
            if(mode=="desktop"){resumed_={};explicitTicket_.clear();return {};}
            if(mode!="bigpicture"||same(s,resumed_))return {};
            std::string reason;
            if(explicitTicket_==ticket(s))reason="updater-decky-handoff";
            else {
                const auto prior=residue(state.value("deckyUpdateResidue",Json()));
                if(prior.empty())return {};
                const bool frontendChanged=fingerprint(installedBuild)&&prior.value("frontendBuildId",std::string{})!=installedBuild;
                const bool loaderChanged=loaderVersion(installedLoader)&&prior.contains("loaderVersion")&&prior.value("loaderVersion",std::string{})!=installedLoader;
                if(!frontendChanged&&!loaderChanged)return {};
                reason=frontendChanged?"updated-decky-frontend-pending":"updated-decky-loader-pending";
            }
            resumed_=s;explicitTicket_.clear();return reason;
        }catch(...){return {};}
    }
    // Existing exact YMCC frontend can reconnect its authenticated broker without
    // starting/restarting Loader or granting Steam/CSS authority. OFF remains a veto.
    bool allowsRetainedFrontend(const steamsession::Session& s,const Json& state,const std::string& installedBuild,const std::string& installedLoader)const {
        std::lock_guard lock(mutex_);
        try {
            if(veto_||!s.pid||!s.created||s.root.empty()||!fingerprint(installedBuild)||!loaderVersion(installedLoader)||
               !state.value("known",false)||!state.value("running",false)||state.at("pid")!=s.pid||
               state.value("birth",std::string{})!=std::to_string(s.created)||state.value("mode",std::string{})!="bigpicture"||
               !state.contains("deckyFrontendLoaded")||state["deckyFrontendLoaded"]!=true)return false;
            const auto prior=residue(state.value("deckyUpdateResidue",Json()));
            return !prior.empty()&&prior.value("frontendBuildId",std::string{})==installedBuild&&
                   prior.value("loaderVersion",std::string{})==installedLoader;
        }catch(...){return false;}
    }
    // Explicit repair never clears a concurrent user/fault OFF. Only the existing
    // ON switch may reset that veto. A failed/uncertain refresh grants no retries.
    bool allowManualRepair(const steamsession::Session& s){std::lock_guard lock(mutex_);if(veto_)return false;resumed_=s;explicitTicket_.clear();return true;}
    bool allows(const steamsession::Session& s)const{std::lock_guard lock(mutex_);return !veto_&&same(s,resumed_);}
};
}
