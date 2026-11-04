#pragma once
// 2026-10-22 field: an old Steam document can outlive its owned Loader and a
// YMCC package change. Automatic admission MUST keep rejecting reinjection.
// Manual IPC remains separate from ordinary automatic transport recovery. The
// 2026-11-01 final-cleanup policy may reserve ONE verified stale-document rebuild
// after two gentle attempts. V2 keeps an uncertain reservation spent, and a
// native-proven completed lifecycle can continue in a later YMCC host. Same-host
// automatic budget, v1 history and OFF remain independent. No settings replay,
// Loader patch, Steam process restart or crash-budget reset belongs here.
#include "decky_update_recovery.h"
#include "decky_sidebar_session.h"
#include "decky_bigpicture_recovery.h"
#include <functional>
#include <array>
namespace ymcc::deckyrepair {
using Json=nlohmann::json;
enum class Origin { Manual, Automatic, FinalCleanup, ReopenBigPicture };
// Final UI cleanup needs the same live CurrentUser witness as its actual JS
// callback, including when the registry already has a nonzero ActiveUser.
inline bool cleanupAccountConfirmed(const steamsession::Session& session,const Json& mode){
    try{
        const auto account=deckysession::resolve(session,mode);
        return account.value&&mode.value("deckyAccountWatchAvailable",false)&&
            deckysession::positiveAccount(mode.value("observedAccount",Json()))==account.value;
    }catch(...){return false;}
}
// This bounded ledger is YMCC recovery metadata, never plugin/user settings.
// Corrupt or foreign metadata cannot refill a consumed cleanup reservation.
inline bool cleanupAvailable(const Json& ledger,const steamsession::Session& session,const std::string& document={}){
    const auto ticket=deckyupdate::ticket(session);if(ticket.empty())return false;
    if(ledger.is_null())return true;
    if(deckyupdate::managed::valid(ledger))return deckyupdate::managed::available(ledger,session,document);
    return ledger.is_object()&&ledger.size()==2&&ledger.contains("schemaVersion")&&ledger["schemaVersion"].is_number_integer()&&ledger["schemaVersion"]==1&&
        ledger.contains("steamSession")&&ledger["steamSession"].is_string()&&
        deckyupdate::validTicket(ledger["steamSession"].get<std::string>())&&ledger["steamSession"]!=ticket;
}
// Missing native metadata is a fresh configuration; an explicit null/corrupt
// value is lost history, never another grant. Callers must inspect both durable
// copies before rotating either one; a valid spent backup outranks a rolled-back
// main document even when its unrelated business settings are still readable.
inline bool cleanupDocumentAvailable(const Json& document,const steamsession::Session& session,const std::string& currentDocument={},const char* key="deckyContextCleanup"){
    if(!document.is_object())return false;
    const auto ledger=document.find(key);
    return ledger==document.end()?cleanupAvailable(Json(),session):
        (!ledger->is_null()&&cleanupAvailable(*ledger,session,currentDocument));
}
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
    try{return args.is_object()&&args.size()==(origin==Origin::FinalCleanup||origin==Origin::ReopenBigPicture?5u:4u)&&
        args.at(origin==Origin::Manual?"userInitiated":origin==Origin::FinalCleanup?"finalCleanup":origin==Origin::ReopenBigPicture?"reopenBigPicture":"automatic")==true&&
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
    struct Cooldown {bool spent=false;steamsession::Session session;unsigned long long last=0;};
    std::mutex mutex_;bool inFlight_=false;
    std::array<Cooldown,4> cooldowns_{};
public:
    // All origins still share one in-flight operation. Each trusted origin has
    // its own cooldown: a refused document cleanup must not spend the next
    // Big Picture recovery step's opportunity. Switching origins never clears
    // another origin's cooldown. Durable UI reservations remain independent.
    bool reserve(const steamsession::Session& session,unsigned long long now,Origin origin=Origin::Manual){
        std::lock_guard lock(mutex_);
        const auto index=static_cast<size_t>(origin);
        if(inFlight_||index>=cooldowns_.size())return false;
        auto& slot=cooldowns_[index];
        if(slot.spent&&session==slot.session&&(now<slot.last||now-slot.last<30000))return false;
        inFlight_=true;slot.spent=true;slot.session=session;slot.last=now;return true;
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
    // Optional product policy; absent means the original manual-only UI boundary.
    std::function<bool(const steamsession::Session&)> cleanupAllowed;
    std::function<bool(const steamsession::Session&)> reserveCleanup;
    std::function<void(const steamsession::Session&)> gentleRecovery;
    std::function<Json(const steamsession::Session&,const Json&,const std::function<bool()>&)> cleanupWorker;
    std::function<unsigned long long()> powerGeneration;
    // Only an explicitly requested, positively acknowledged manual dispatch can
    // seed a new legacy baseline; completion is proven later by the native owner.
    std::function<unsigned long long()> manualGeneration;
    std::function<void(const steamsession::Session&,const Json&,unsigned long long,unsigned long long)> manualDispatched;
    std::function<Json(const std::function<bool()>&)> reopenBigPicture; // Private native child only; no UI-supplied mode/URL.
};
class ManualRepair {
    AttemptBudget budget_;
public:
    Json request(const Json& args,const Dependencies& deps,Origin origin=Origin::Manual){
        bool cleanupNotStarted=true;
        const auto refuse=[&](const std::string& reason){
            Json result={{"ok",false},{"reason",reason},{"mutated",false}};
            // Only our exact pre-child/pre-reservation control flow can certify
            // this no-op. A failing reservation may already have persisted data.
            if((origin==Origin::FinalCleanup||origin==Origin::ReopenBigPicture)&&cleanupNotStarted)result["cleanupNotStarted"]=true;
            return result;
        };
        bool dispatchEntered=false;
        try{
            if(deps.isolated||deps.exiting())return refuse("isolated-or-exiting");
            // Rebuilding Steam UI can show Big Picture even without a mode/start command.
            // Automatic recovery must use transport rebinds, never this UI refresh.
            if(origin==Origin::Automatic)return refuse("steam-ui-refresh-requires-user");
            const auto session=deps.session();
            // A Ready->sleep->Ready cycle can finish between callback checks.
            // Bind admission to its generation as well as the current level.
            const auto power=deps.powerGeneration?deps.powerGeneration():0;
            const auto manualEpoch=origin==Origin::Manual&&deps.manualGeneration?deps.manualGeneration():0;
            if(!requestMatches(args,session,deps.runtime(),origin))return refuse("decky-repair-state-changed");
            const bool cleanup=origin==Origin::FinalCleanup||origin==Origin::ReopenBigPicture;
            if(cleanup&&deps.cleanupWorker){
                if(!deps.cleanupAllowed||!deps.cleanupAllowed(session))return refuse("decky-cleanup-not-admitted");
                if(!budget_.reserve(session,deps.now(),origin))return refuse("decky-repair-wait");
                struct Finish{AttemptBudget& budget;~Finish(){budget.finish();}}finish{budget_};
                const auto current=[&]{return !deps.exiting()&&(!deps.powerGeneration||deps.powerGeneration()==power)&&
                    deps.session()==session&&requestMatches(args,session,deps.runtime(),origin);};
                if(!current()||!deps.consent(session))return refuse("decky-cleanup-state-changed");
                cleanupNotStarted=false;dispatchEntered=true;return deps.cleanupWorker(session,args,current);
            }
            const auto installed=deps.installed();
            if(cleanup&&(!deps.cleanupAllowed||!deps.cleanupAllowed(session)||!deps.reserveCleanup||!deps.freshMode||
                (origin==Origin::ReopenBigPicture&&!deps.reopenBigPicture)))
                return refuse("decky-cleanup-not-admitted");
            const auto mode=origin!=Origin::Automatic&&deps.freshMode?deps.freshMode(session):deps.mode(session);
            // Only this pre-reservation refusal can safely leave final cleanup
            // available. No dispatched/uncertain result carries this authority.
            if(cleanup&&!cleanupAccountConfirmed(session,mode))return Json{{"ok",false},{"mutated",false},
                {"reason","decky-cleanup-account-unconfirmed"},{"awaitCurrentIdentity",true}};
            auto action=target(session,mode,installed,deps.loader);
            if(action.is_null())return refuse("decky-repair-context-changed");
            if(cleanup){
                const auto epoch=args.value("modeEpoch",std::string{});
                if(epoch.empty()||mode.value("modeEpoch",std::string{})!=epoch)return refuse("decky-cleanup-context-changed");
                action.erase("userInitiated");action["cleanupAuthorized"]=true;
                action["operation"]="decky.cleanupConnection";action["expectedModeEpoch"]=epoch;
                if(!session.account)return refuse("decky-cleanup-account-unconfirmed");
                action["expectedAccount"]=session.account;
            }
            const auto failure=deps.preflight();if(!failure.empty())return refuse(failure);
            if(!budget_.reserve(session,deps.now(),origin))return refuse("decky-repair-wait");
            struct Finish{AttemptBudget& budget;~Finish(){budget.finish();}}finish{budget_};
            const auto current=[&]{return !deps.exiting()&&(!deps.powerGeneration||deps.powerGeneration()==power)&&
                    deps.session()==session&&requestMatches(args,session,deps.runtime(),origin);};
            bool admitted=false;
            const auto admit=[&]{
                if(!current()||deps.installed()!=installed||!deps.preflight().empty())return false;
                if(!deps.consent(session))return false;
                admitted=current();return admitted; // OFF during consent/IO cannot be admitted from the old revision.
            };
            // Spend durable cleanup permission BEFORE transport, including uncertain or
            // no-reply dispatches. A YMCC restart cannot create an automatic loop.
            if(cleanup){
                if(!current()||!deps.cleanupAllowed(session))return refuse("decky-cleanup-budget-unavailable");
                cleanupNotStarted=false;
                if(!deps.reserveCleanup(session))return refuse("decky-cleanup-budget-unavailable");
            }
            // Once transport is entered an exception may follow a real refresh.
            // Keep the reservation spent and report uncertainty, never no-op.
            dispatchEntered=true;
            Json result;
            if(origin==Origin::ReopenBigPicture){
                if(!admit())return refuse("decky-cleanup-state-changed");
                result=deps.reopenBigPicture(current); // Hash/preflight once, not on every bounded mode wait.
            }else result=deps.dispatch(session,action,admit);
            if(origin==Origin::Manual&&admitted&&deps.manualDispatched&&result.value("ok",false)&&
               result.value("requestDispatched",false)&&result.value("mutated",false)&&!result.value("uncertain",false)&&
               !deps.exiting()&&(!deps.powerGeneration||deps.powerGeneration()==power)&&deps.session()==session){
                try{deps.manualDispatched(session,mode,power,manualEpoch);}catch(...){/* Provenance failure cannot alter the actual operation result. */}
            }
            return result;
        }catch(...){
            if(dispatchEntered)return Json{{"ok",false},{"reason","decky-repair-result-unconfirmed"},{"mutated",true},{"uncertain",true}};
            return refuse("decky-repair-unavailable");
        }
    }
};
}
