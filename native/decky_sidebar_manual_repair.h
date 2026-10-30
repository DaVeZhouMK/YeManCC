#pragma once
// 2026-10-22 field: an old Steam document can outlive its owned Loader and a
// YMCC package change. Automatic admission MUST keep rejecting reinjection.
// Only the explicit manual IPC may reserve one UI refresh; no automatic recovery, settings replay,
// Loader patch, Steam process restart, polling, or crash-budget reset belongs here.
#include "decky_update_recovery.h"
#include <functional>
#include <array>
namespace ymcc::deckyrepair {
using Json=nlohmann::json;
enum class Origin { Manual, Automatic };
// A retained bridge only proves YMCC transport. With no owned Loader it cannot
// certify the official backend/store; an explicit click may repair that state.
inline bool repairable(const Json& runtime){
    try{return runtime.at("enabled")==true&&
        ((runtime.at("pid").is_number_unsigned()&&runtime.at("pid").get<unsigned long long>()>0&&runtime.at("phase")=="loader-started")||
        (runtime.at("pid")==0&&
        ((runtime.at("phase")=="waiting-manual-steam"&&runtime.at("reason")=="decky-context-already-loaded")||
         (runtime.at("phase")=="frontend-attached"&&runtime.at("reason")=="retained-frontend-connected")||
         // A rejected retained rebind revokes the old transport, not the user's
         // manual repair entry. request() must freshly verify session/context.
         (runtime.at("phase")=="unavailable"&&runtime.at("reason")=="mirror-bootstrap-session-changed"&&
          runtime.value("steamKnown",false)&&runtime.value("steamRunning",false)))));
    }catch(...){return false;}
}
// A live owned backend is kept running. A manual request only refreshes the
// verified Steam document. Port-free admission is reserved for absent backends;
// resource/version failures still veto both paths.
inline std::string repairPreflight(const Json& environment,const Json& runtime,const std::function<std::string()>& ports){
    if(!environment.value("ready",false))return environment.value("reason",std::string("environment-check-failed"));
    if(repairable(runtime)&&runtime.value("pid",0u)>0)return {};
    return ports();
}
inline bool requestMatches(const Json& args,const steamsession::Session& session,const Json& runtime,Origin origin=Origin::Manual){
    try{return args.is_object()&&args.size()==4&&args.at(origin==Origin::Manual?"userInitiated":"automatic")==true&&
        args.at("pid").is_number_unsigned()&&args.at("pid")==session.pid&&
        args.at("birth")==std::to_string(session.created)&&args.at("revision")==runtime.at("revision")&&
        repairable(runtime)&&(origin==Origin::Manual||runtime.at("pid")==0)&&session.pid&&session.created&&!session.root.empty();
    }catch(...){return false;}
}
// Explicit repair pins the version actually observed in this exact document.
// Package/UI versions can alternate during any managed upgrade, not just 330/331.
// Accept a verified equal/older stable document for a user-requested refresh;
// a newer document cannot be downgraded by an older installed package. Automatic
// retained binding still requires exact Loader/build equality in Recovery.
inline bool manualLoaderContext(const std::string& installed,const std::string& observed){
    const auto parse=[](const std::string& value)->std::array<unsigned long,3>{
        if(!deckyupdate::loaderVersion(value))throw std::invalid_argument("loader-version");
        std::array<unsigned long,3> parts{};size_t begin=1;
        for(size_t i=0;i<3;++i){const auto end=value.find('.',begin);const auto item=value.substr(begin,end==std::string::npos?end:end-begin);
            if(item.size()>1&&item.front()=='0')throw std::invalid_argument("loader-version");
            parts[i]=std::stoul(item);begin=end==std::string::npos?value.size():end+1;}
        if(!parts[0])throw std::invalid_argument("loader-version");return parts;
    };
    try{return parse(observed)<=parse(installed);}catch(...){return false;}
}
inline Json target(const steamsession::Session& session,const Json& mode,const std::string& installed,const std::string& loader){
    try{
        if(!session.pid||!session.created||session.root.empty()||!deckyupdate::fingerprint(installed)||!deckyupdate::loaderVersion(loader)||
            mode.at("known")!=true||mode.at("running")!=true||mode.at("pid")!=session.pid||mode.at("birth")!=std::to_string(session.created)||
            mode.at("mode")!="bigpicture"||mode.at("deckyFrontendLoaded")!=true)return Json{};
        const auto empty=mode.value("deckyEmptyFrontend",Json());
        if(empty.is_object()&&empty.size()==3&&empty.value("protocol",0)==1&&
            empty.value("frontendBuildId",std::string{})==installed&&manualLoaderContext(loader,empty.value("loaderVersion",std::string{})))
            return {{"operation","decky.repairConnection"},{"userInitiated",true},{"expectedFrontendBuild",nullptr},
                {"installedFrontendBuild",installed},{"loaderVersion",empty.at("loaderVersion")}};
        const auto prior=deckyupdate::residue(mode.at("deckyUpdateResidue"));
        if(prior.empty()||!manualLoaderContext(loader,prior.value("loaderVersion",std::string{})))return Json{};
        return {{"operation","decky.repairConnection"},{"userInitiated",true},{"expectedFrontendBuild",prior.at("frontendBuildId")},
            {"installedFrontendBuild",installed},{"loaderVersion",prior.at("loaderVersion")}};
    }catch(...){return Json{};}
}
class AttemptBudget {
    std::mutex mutex_;bool inFlight_=false;bool spent_=false;steamsession::Session session_;unsigned long long last_=0;
public:
    // Consume before dispatch, including uncertain outcomes. No automatic retry/refund.
    bool reserve(const steamsession::Session& session,unsigned long long now){
        std::lock_guard lock(mutex_);if(inFlight_||(spent_&&session==session_&&(now<last_||now-last_<30000)))return false;
        inFlight_=true;spent_=true;session_=session;last_=now;return true;
    }
    void finish(){std::lock_guard lock(mutex_);inFlight_=false;}
};
struct Dependencies {
    bool isolated=false;
    std::function<steamsession::Session()> session;
    std::function<Json()> runtime;
    std::function<Json(const steamsession::Session&)> mode;
    std::function<std::string()> installed;
    std::string loader;
    std::function<std::string()> preflight;
    std::function<bool()> exiting;
    std::function<bool(const steamsession::Session&)> consent;
    std::function<Json(const steamsession::Session&,const Json&,const std::function<bool()>&)> dispatch;
    std::function<unsigned long long()> now;
    // Explicit manual demand requires current context evidence, not an older
    // mode-cache entry from before official plugin startup. Auto never forces IO.
    std::function<Json(const steamsession::Session&)> freshMode;
};
class ManualRepair {
    AttemptBudget budget_;
public:
    Json request(const Json& args,const Dependencies& deps,Origin origin=Origin::Manual){
        const auto refuse=[](const std::string& reason){return Json{{"ok",false},{"reason",reason},{"mutated",false}};};
        bool dispatchEntered=false;
        try{
            if(deps.isolated||deps.exiting())return refuse("isolated-or-exiting");
            // Rebuilding Steam UI can show Big Picture even without a mode/start command.
            // Automatic recovery must use transport rebinds, never this UI refresh.
            if(origin==Origin::Automatic)return refuse("steam-ui-refresh-requires-user");
            const auto session=deps.session();
            if(!requestMatches(args,session,deps.runtime(),origin))return refuse("decky-repair-state-changed");
            const auto installed=deps.installed();
            const auto mode=origin==Origin::Manual&&deps.freshMode?deps.freshMode(session):deps.mode(session);
            auto action=target(session,mode,installed,deps.loader);
            if(action.is_null())return refuse("decky-repair-context-changed");
            if(origin==Origin::Automatic){action.erase("userInitiated");action["automatic"]=true;action["operation"]="decky.recoverConnection";}
            const auto failure=deps.preflight();if(!failure.empty())return refuse(failure);
            if(!budget_.reserve(session,deps.now()))return refuse("decky-repair-wait");
            struct Finish{AttemptBudget& budget;~Finish(){budget.finish();}}finish{budget_};
            const auto current=[&]{return !deps.exiting()&&deps.session()==session&&requestMatches(args,session,deps.runtime(),origin);};
            const auto admit=[&]{
                if(!current()||deps.installed()!=installed||!deps.preflight().empty())return false;
                if(!deps.consent(session))return false;
                return current(); // OFF during consent/IO cannot be admitted from the old revision.
            };
            // Once transport is entered an exception may follow a real refresh.
            // Keep the reservation spent and report uncertainty, never no-op.
            dispatchEntered=true;
            return deps.dispatch(session,action,admit);
        }catch(...){
            if(dispatchEntered)return Json{{"ok",false},{"reason","decky-repair-result-unconfirmed"},{"mutated",true},{"uncertain",true}};
            return refuse("decky-repair-unavailable");
        }
    }
};
}
