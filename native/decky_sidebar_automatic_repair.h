#pragma once
// Missing-backend observation never authorizes rebuilding Steam UI.
// Transport/binding recovery remains with its existing owners; UI refresh needs a click.
// One automatic dispatch per verified Steam document. Existing runtime events
// call observe(); no timer, polling service, settings replay or new executable.
#include "decky_sidebar_manual_repair.h"
#include "steam_ui_epoch.h"
namespace ymcc::deckyrepair {
class AutomaticRepair {
    mutable std::mutex mutex_;
    steamsession::Session session_;
    std::string document_;
    bool spent_=false,probing_=false;
    unsigned sessionAttempts_=0;
    unsigned long long checked_=0;
    Json status_={{"phase","idle"},{"attempted",false}};
public:
    Json snapshot(unsigned long long now=0)const{
        std::lock_guard lock(mutex_);auto result=status_;
        if(now&&checked_&&now>=checked_&&now-checked_>=30000&&result.value("phase",std::string{})=="recovering"){
            result["phase"]="manual-required";result["reason"]="recovery-confirmation-timeout";
        }
        return result;
    }
    // Called only after an owned Loader + authenticated ready native snapshot.
    // This closes a successful recovery episode; same-document no-replay remains.
    void confirmed(const steamsession::Session& session){
        std::lock_guard lock(mutex_);if(session==session_&&!probing_){sessionAttempts_=0;status_={{"phase","idle"},{"attempted",false}};}
    }
    void observe(ManualRepair& shared,const Dependencies& deps,const std::function<void()>& changed={}){
        try{
            if(deps.isolated||deps.exiting())return;
            const auto session=deps.session();const auto state=deps.runtime();
            if(!repairable(state)||state.value("pid",0u)!=0)return; // Owned healthy backends never refresh automatically.
            const auto mode=deps.mode(session);
            const auto epoch=mode.value("modeEpoch",std::string{});
            const auto parsed=steamui::parseModeEpoch(epoch);
            if(!parsed.valid||target(session,mode,deps.installed(),deps.loader).is_null())return;
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
