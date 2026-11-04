#pragma once
// Ordinary automatic recovery retains the manual UI boundary. The explicit
// final-cleanup product policy (2026-11-01) adds two gentle attempts separated
// by 30 seconds, then one durable, verified stale-context rebuild. No disk cache
// or user configuration is deleted, and an uncertain cleanup is never replayed.
// One automatic dispatch per verified Steam document. Existing runtime events
// call observe(); no timer, polling service, settings replay or new executable.
#include "decky_sidebar_manual_repair.h"
#include "steam_ui_epoch.h"
#include "decky_sidebar_session.h"
namespace ymcc::deckyrepair {
class AutomaticRepair {
    mutable std::mutex mutex_;
    steamsession::Session session_;
    std::string document_;
    bool spent_=false,probing_=false;
    unsigned sessionAttempts_=0;
    unsigned long long checked_=0,cleanupDue_=0,cleanupNotBefore_=0,episodeGeneration_=0,completionGeneration_=0;
    unsigned gentleAttempts_=0;
    std::string cleanupIdentity_,spentDocument_;DWORD spentAccount_=0;
    Json status_={{"phase","idle"},{"attempted",false}};
    // End waiting/status ownership, not the same-Steam final-cleanup fuse.
    // A callback already in flight must not resurrect this closed episode.
    void closeEpisodeLocked(){
        ++episodeGeneration_;checked_=cleanupDue_=cleanupNotBefore_=0;
        gentleAttempts_=0;cleanupIdentity_.clear();status_={{"phase","idle"},{"attempted",false}};
    }
public:
    Json snapshot(unsigned long long now=0)const{
        std::lock_guard lock(mutex_);auto result=status_;
        if(now&&checked_&&now>=checked_&&now-checked_>=30000&&result.value("phase",std::string{})=="recovering"){
            result["phase"]="manual-required";result["reason"]="recovery-confirmation-timeout";
        }
        return result;
    }
    // Borrow the existing Runtime worker wait; never create another timer/thread.
    unsigned long nextTimeout(unsigned long long now)const{
        std::lock_guard lock(mutex_);if(!cleanupDue_)return 0xfffffffful;
        return now>=cleanupDue_?0ul:static_cast<unsigned long>((std::min)(cleanupDue_-now,30000ull));
    }
    void endEpisode(){
        std::lock_guard lock(mutex_);++completionGeneration_;closeEpisodeLocked();
    }
    unsigned long long completionEpoch()const{std::lock_guard lock(mutex_);return completionGeneration_;}
    // Called only after an owned Loader + authenticated ready native snapshot.
    // This closes a successful recovery episode; same-document no-replay remains.
    void confirmed(const steamsession::Session& session){
        std::lock_guard lock(mutex_);if(session==session_){sessionAttempts_=0;closeEpisodeLocked();}
    }
    // Only a private native confirmation which has read back BOTH durable
    // copies can complete this host's spent lifecycle. A different verified
    // ready document starts the next lifecycle; plain ready/OFF/sleep/account
    // events and missing/uncertain receipts cannot refund the old operation.
    bool completedManaged(const steamsession::Session& effective,const Json& ledger,const std::string& document,
            const std::string& host,const std::string& build,const std::string& loader,unsigned long long epoch){
        const auto expected=deckyupdate::managed::record(effective,document,host,build,loader,"ready");
        if(expected.is_null()||ledger!=expected||!effective.account)return false;
        std::lock_guard lock(mutex_);
        if(epoch!=completionGeneration_||probing_||!spent_||document_!=deckyupdate::ticket(effective)||spentDocument_.empty()||spentDocument_==document||
           session_.root!=effective.root||session_.pid!=effective.pid||session_.created!=effective.created||spentAccount_!=effective.account)return false;
        spent_=false;document_.clear();spentDocument_.clear();spentAccount_=0;sessionAttempts_=0;closeEpisodeLocked();return true;
    }
    void observeCleanup(ManualRepair& shared,const Dependencies& deps,const steamsession::Session& session,
        const Json& state,const Json& mode,const Json& selected,DWORD account,const std::function<void()>& changed){
        const auto now=deps.now();const auto epoch=mode.value("modeEpoch",std::string{});
        const auto power=deps.powerGeneration?deps.powerGeneration():0;unsigned long long episode=0;
        const auto current=[&]{return !deps.exiting()&&deps.session()==session&&
            (!deps.powerGeneration||deps.powerGeneration()==power);};
        // Effective account, not registry availability, owns these gentle attempts.
        // Offline ActiveUser=0 can outlive several verified CurrentUser changes.
        const auto identity=epoch+":"+selected.dump()+":"+std::to_string(account)+":"+std::to_string(power);
        {
            std::lock_guard lock(mutex_);if(probing_)return;
            if(session.pid!=session_.pid||session.created!=session_.created||session.root!=session_.root||cleanupIdentity_!=identity){
                ++episodeGeneration_;cleanupIdentity_=identity;gentleAttempts_=0;cleanupDue_=cleanupNotBefore_=0;checked_=0;
            }
            session_=session; // Same effective account keeps its debt across registry/UI-source handoff.
            if(spent_&&document_==deckyupdate::ticket(session))return;
            // A shared mode refresh intentionally publishes pending. Parking its
            // wait must not erase spent attempts or the original minimum gap.
            if(cleanupNotBefore_&&now<cleanupNotBefore_){cleanupDue_=cleanupNotBefore_;return;}
            episode=episodeGeneration_;probing_=true;cleanupDue_=0;
        }
        struct Finish{AutomaticRepair& self;~Finish(){std::lock_guard lock(self.mutex_);self.probing_=false;}}finish{*this};
        if(!deps.cleanupAllowed(session)){
            std::lock_guard lock(mutex_);if(episode!=episodeGeneration_)return;
            status_={{"phase","manual-required"},{"attempted",false},{"reason","decky-cleanup-not-admitted"}};return;
        }
        const auto failure=deps.cleanupWorker?std::string{}:deps.preflight();
        if(!failure.empty()){
            {std::lock_guard lock(mutex_);if(episode!=episodeGeneration_)return;status_={{"phase","manual-required"},{"attempted",false},{"reason",failure}};}
            if(changed)changed();return;
        }
        if(!current()){endEpisode();return;}
        bool gentle=false;
        {
            std::lock_guard lock(mutex_);if(episode!=episodeGeneration_)return;
            gentle=gentleAttempts_<2;
            if(gentle){++gentleAttempts_;cleanupDue_=cleanupNotBefore_=now+30000;status_={{"phase","recovering"},{"attempted",true},{"reason","decky-gentle-recovery"},{"gentleAttempts",gentleAttempts_}};}
            else{spent_=true;document_=deckyupdate::ticket(session);spentDocument_=deckyupdate::managed::document(epoch);spentAccount_=account;status_={{"phase","recovering"},{"attempted",true},{"reason","decky-stale-context-cleanup"}};}
        }
        if(changed)changed();
        if(gentle){if(deps.gentleRecovery)deps.gentleRecovery(session);return;}
        const Json args={{"finalCleanup",true},{"pid",session.pid},{"birth",std::to_string(session.created)},
            {"revision",state.at("revision")},{"modeEpoch",epoch}};
        const auto result=shared.request(args,deps,Origin::FinalCleanup);
        const auto finished=deps.now(); // Confirmation gets its own window after slow recovery IO.
        if(!current())endEpisode(); // A completed Ready->sleep->Ready transition still revokes old work.
        {
            std::lock_guard lock(mutex_);
            const bool noEffect=!result.value("ok",false)&&result.contains("mutated")&&
                !result.value("mutated",true)&&!result.value("uncertain",false);
            const bool awaiting=noEffect&&result.value("awaitCurrentIdentity",false)&&
                result.value("reason",std::string{})=="decky-cleanup-account-unconfirmed";
            const bool unstarted=noEffect&&result.value("cleanupNotStarted",false);
            // No other probe can start until Finish releases probing_. Thus a
            // proven no-reservation reply may release this pending RAM attempt,
            // even after OFF/ready invalidated its status/timer ownership.
            // An OFF immediately before shared.request also has a certified
            // no-child/no-reservation result. Release only its closed RAM slot;
            // active fatal failures do not gain a fresh automatic retry loop.
            if(awaiting||(episode!=episodeGeneration_&&unstarted))spent_=false;
            if(episode!=episodeGeneration_)return;
            if(awaiting){
                // The isolated shared consumer proved no reservation/dispatch.
                // Keep the durable fuse untouched and borrow the existing wait
                // before a fresh gentle episode; stale cached proof cannot spin.
                checked_=0;gentleAttempts_=0;
                cleanupDue_=cleanupNotBefore_=finished+30000;
                status_={{"phase","waiting"},{"attempted",false},{"reason","decky-cleanup-account-unconfirmed"}};
            }else{
                checked_=finished?finished:1;
                status_={{"phase",result.value("ok",false)||result.value("mutated",false)?"recovering":"manual-required"},
                    {"attempted",true},{"reason",result.value("reason",std::string("decky-stale-context-cleanup"))},{"uncertain",result.value("uncertain",false)}};
            }
        }
        if(changed)changed();
    }
    void observe(ManualRepair& shared,const Dependencies& deps,const std::function<void()>& changed={}){
        try{
            if(deps.isolated||deps.exiting()){endEpisode();return;}
            const auto session=deps.session();const auto state=deps.runtime();
            if(!repairable(state)||state.value("pid",0u)!=0){
                std::lock_guard lock(mutex_);cleanupDue_=0;
                if(!state.value("enabled",false)||state.value("pid",0u)!=0||!state.value("steamRunning",false)||
                    session.pid!=session_.pid||session.created!=session_.created||session.root!=session_.root){
                    cleanupNotBefore_=0;gentleAttempts_=0;cleanupIdentity_.clear();
                }
                return;
            } // Owned healthy backends never refresh automatically.
            const auto mode=deps.mode(session);
            const auto epoch=mode.value("modeEpoch",std::string{});
            const auto parsed=steamui::parseModeEpoch(epoch);
            const auto account=deckysession::resolve(session,mode);
            const auto observed=deckysession::positiveAccount(mode.value("observedAccount",Json()));
            const auto selected=target(session,mode,deps.installed(),deps.loader);
            // Conflicting or missing offline identity is a parked observation,
            // never a failure belonging to either account. Actual child and JS
            // still independently revalidate identity before any UI operation.
            if(!parsed.valid||selected.is_null()||!account.value||(observed&&observed!=account.value)){
                std::lock_guard lock(mutex_);cleanupDue_=0;return; // Unknown proof parks; it never refills the episode.
            }
            if(deps.cleanupAllowed){
                if(!cleanupAccountConfirmed(session,mode)){std::lock_guard lock(mutex_);cleanupDue_=0;return;}
                observeCleanup(shared,deps,session,state,mode,selected,account.value,changed);return;
            }
            const auto document=std::string(parsed.document);const auto now=deps.now();
            {
                std::lock_guard lock(mutex_);
                if(probing_)return;
                if(!(session==session_)){session_=session;sessionAttempts_=0;document_.clear();spent_=false;checked_=0;}
                if(document!=document_){document_=document;spent_=false;checked_=0;status_={{"phase","idle"},{"attempted",false}};}
                if(sessionAttempts_>=2){status_={{"phase","manual-required"},{"attempted",true},{"reason","automatic-budget-exhausted"}};return;}
                if(spent_||(checked_&&(now<checked_||now-checked_<30000)))return;
                probing_=true;checked_=now?now:1;
            }
            struct Finish{AutomaticRepair& self;~Finish(){std::lock_guard lock(self.mutex_);self.probing_=false;}}finish{*this};
            // A live/foreign backend or invalid resources never authorize a
            // refresh. A later existing UI demand may inspect again after 30s.
            if(!deps.preflight().empty())return;
            {
                std::lock_guard lock(mutex_);spent_=true;++sessionAttempts_;status_={{"phase","recovering"},{"attempted",true}};
            }
            if(changed)changed();
            const Json args={{"automatic",true},{"pid",session.pid},{"birth",std::to_string(session.created)},{"revision",state.at("revision")}};
            const auto result=shared.request(args,deps,Origin::Automatic);
            {
                std::lock_guard lock(mutex_);
                status_={{"phase",result.value("ok",false)||result.value("mutated",false)?"recovering":"manual-required"},
                    {"attempted",result.value("reason",std::string{})!="steam-ui-refresh-requires-user"},{"reason",result.value("reason",std::string{})},{"uncertain",result.value("uncertain",false)}};
            }
            if(changed)changed();
        }catch(...){/* IO failure is not a retry grant; the event owner remains alive. */}
    }
};
}
