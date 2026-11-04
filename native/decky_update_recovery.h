#pragma once
// Decky-only continuation of an already active YMCC plugin across an update.
// Never grants CSS/Steam mode/start authority. Ordinary launches without update
// evidence remain observation-only; a user/safety off veto wins until a new process.
#include "steam_session_observer.h"
#include "steam_ui_epoch.h"
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
// V2 is an evidence chain, not a refill counter. A reservation remains spent
// until THIS native host has observed the resulting different Steam document,
// its own live official backend and an authenticated native ready snapshot.
// Legacy v1/unknown history never becomes ready merely because a host restarted.
namespace managed {
inline std::string document(const std::string& epoch){const auto value=steamui::parseModeEpoch(epoch);return value.valid?std::string(value.document):std::string{};}
inline bool valid(const Json& value){
    try{return value.is_object()&&value.size()==7&&value.at("schemaVersion").is_number_integer()&&value.at("schemaVersion")==2&&
        value.at("steamSession").is_string()&&validTicket(value.at("steamSession").get<std::string>())&&
        value.at("host").is_string()&&validTicket(value.at("host").get<std::string>())&&
        value.at("document").is_string()&&!value.at("document").get<std::string>().empty()&&
        value.at("document").get<std::string>().size()<=75&&value.at("document").get<std::string>().find(':')==std::string::npos&&
        value.at("frontendBuildId").is_string()&&fingerprint(value.at("frontendBuildId").get<std::string>())&&
        value.at("loaderVersion").is_string()&&loaderVersion(value.at("loaderVersion").get<std::string>())&&
        (value.at("status")=="reserved"||value.at("status")=="ready");}catch(...){return false;}
}
inline Json record(const steamsession::Session& session,const std::string& doc,const std::string& host,
        const std::string& build,const std::string& loader,const char* status){
    Json result={{"schemaVersion",2},{"steamSession",ticket(session)},{"host",host},{"document",doc},
        {"frontendBuildId",build},{"loaderVersion",loader},{"status",status}};return valid(result)?result:Json();
}
inline bool available(const Json& ledger,const steamsession::Session& session,const std::string& doc){
    return valid(ledger)&&(ledger.at("steamSession")!=ticket(session)||
        (!doc.empty()&&ledger.at("status")=="ready"&&ledger.at("document")==doc));
}
inline Json completed(const Json& ledger,const steamsession::Session& session,const std::string& doc,
        const std::string& host,const std::string& build,const std::string& loader,const std::string& manualOrigin={}){
    if(!ledger.is_null()){
        if(!valid(ledger)){
            // A positively different Steam PID/birth ends the old v1 scope.
            // Same-process v1 and all malformed/unknown history stay spent.
            if(!ledger.is_object()||ledger.size()!=2||!ledger.contains("schemaVersion")||!ledger["schemaVersion"].is_number_integer()||ledger["schemaVersion"]!=1||
               !ledger.contains("steamSession")||!ledger["steamSession"].is_string()||
               !validTicket(ledger["steamSession"].get<std::string>()))return {};
            // A current explicit manual repair can establish a NEW native-owned
            // healthy lifecycle after legacy history. It needs its real starting
            // document and a different verified ready document; this is never an
            // inference about the unknown old v1 operation or an automatic refund.
            if(ledger["steamSession"]==ticket(session)&&
               (manualOrigin.empty()||manualOrigin==doc||manualOrigin.size()>75||manualOrigin.find(':')!=std::string::npos))return {};
            return record(session,doc,host,build,loader,"ready");
        }
        if(ledger.at("steamSession")==ticket(session)&&ledger.at("status")=="reserved"&&
           (ledger.at("host")!=host||ledger.at("document")==doc))return {};
    }
    return record(session,doc,host,build,loader,"ready");
}
}
// RAM-only provenance of THIS host's positively acknowledged explicit repair.
// OFF clears it; power/account/session mismatches and a new host have no witness.
// It certifies no success itself. The private ready-confirmation path still
// requires the actual new document, owned backend and two durable copies.
class ManualBaseline {
    std::mutex mutex_;steamsession::Session session_;std::string original_;unsigned long long power_=0,generation_=0;
public:
    unsigned long long epoch(){std::lock_guard lock(mutex_);return generation_;}
    void capture(const steamsession::Session& session,const std::string& original,unsigned long long power,unsigned long long expectedEpoch){
        if(ticket(session).empty()||!session.account||original.empty()||original.size()>75||original.find(':')!=std::string::npos)return;
        std::lock_guard lock(mutex_);if(expectedEpoch!=generation_)return;
        session_=session;original_=original;power_=power;
    }
    void clear(){std::lock_guard lock(mutex_);++generation_;session_={};original_.clear();power_=0;}
    std::string origin(const steamsession::Session& session,const std::string& current,unsigned long long power,unsigned long long* observedEpoch=nullptr){
        std::lock_guard lock(mutex_);
        if(!(session==session_)||power!=power_||current.empty()||current==original_)return {};
        if(observedEpoch)*observedEpoch=generation_;
        return original_;
    }
};
// One bounded, observational backend-confirmation episode. The Runtime owner
// borrows its normal wait; no standalone timer or healthy-state polling. A lost
// or uncertain DISPATCH is never retried by this pre-dispatch proof schedule.
class ManagedProofWindow {
    std::string key_;steamsession::RetryWindow retry_;unsigned long long until_=0;bool finished_=true;
public:
    bool take(const std::string& key,unsigned long long now){
        if(key_!=key){key_=key;retry_.begin(now);until_=now+30000;finished_=false;return true;}
        if(finished_)return false;
        if(now>=until_){finished_=true;return true;} // One final observation at the original 30s boundary.
        return retry_.take(now);
    }
    DWORD timeout(unsigned long long now)const{
        if(finished_)return INFINITE;
        return (std::min)(retry_.timeout(now),now>=until_?0ul:static_cast<DWORD>(until_-now));
    }
    void dispatched(){finished_=true;}
};
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
    // Only the private native worker may supply this pair after reading BOTH
    // durable copies. JS mode/account callbacks do not carry this authority.
    bool continueManaged(const steamsession::Session& s,const Json& state,const Json& primary,const Json& backup,
            const std::string& host,const std::string& installedBuild,const std::string& installedLoader){
        if(primary!=backup||!managed::valid(primary)||primary.at("status")!="ready"||
           primary.at("steamSession")!=ticket(s)||primary.at("host")==host||!validTicket(host)||
           primary.at("document")!=managed::document(state.value("modeEpoch",std::string{}))||
           primary.at("frontendBuildId")!=installedBuild||primary.at("loaderVersion")!=installedLoader||
           !allowsRetainedFrontend(s,state,installedBuild,installedLoader))return false;
        std::lock_guard lock(mutex_);if(veto_)return false;resumed_=s;return true;
    }
    // Explicit repair never clears a concurrent user/fault OFF. Only the existing
    // ON switch may reset that veto. A failed/uncertain refresh grants no retries.
    bool allowManualRepair(const steamsession::Session& s){std::lock_guard lock(mutex_);if(veto_)return false;resumed_=s;explicitTicket_.clear();return true;}
    bool vetoed()const{std::lock_guard lock(mutex_);return veto_;}
    bool allows(const steamsession::Session& s)const{std::lock_guard lock(mutex_);return !veto_&&same(s,resumed_);}
};
}
