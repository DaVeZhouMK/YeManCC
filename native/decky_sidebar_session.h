#pragma once
#include "decky_frontend_state.h"
// Decky account selection is local UI context, not a registry consistency gate.
// Sleep, offline mode and account switching can leave ActiveUser stale. Prefer
// the current same-process UI witness when these sources disagree. Matching
// accounts keep the original binding path without adding a new UI watch gate.
// Never infer an account from loginusers/userdata or a different Steam process.
#include "steam_session_observer.h"
#include "steam_account_facts.h"
#include "json.hpp"
namespace ymcc::deckysession {
using Json=nlohmann::json;
struct Account { DWORD value=0; bool fromUi=false; };
inline DWORD positiveAccount(const Json& v) {
    return steamaccount::positiveAccount32(v);
}
inline Account resolve(const steamsession::Session& session,const Json& mode) {
    if(!session.pid||!session.created||session.root.empty())return {};
    try {
        const auto selected=steamaccount::select(steamaccount::project(session,mode),steamaccount::Policy::UiLocal);
        return {selected.value,selected.fromUi};
    }catch(...) { return {session.account,false}; }
}
inline bool bigPicture(const steamsession::Session& session,const Json& mode) {
    try{const auto account=resolve(session,mode);const auto observed=positiveAccount(mode.value("observedAccount",Json()));
        return account.value&&(!observed||observed==account.value)&&mode.value("known",false)&&mode.value("running",false)&&
        positiveAccount(mode.at("pid"))==session.pid&&mode.value("birth",std::string{})==std::to_string(session.created)&&
        mode.value("mode",std::string{})=="bigpicture";}
    catch(...){return false;}
}
inline bool admissionReadSatisfied(const steamsession::Session& session,const Json& mode,bool currentRecentRead) {
    const auto name=mode.find("mode");
    if(name==mode.end())return false;
    // The original desktop park and slow renewal for incomplete evidence stay.
    if(*name=="desktop")return mode.value("known",false)&&mode.value("running",false);
    const auto frontend=mode.find("deckyFrontendLoaded");
    return currentRecentRead&&frontend!=mode.end()&&frontend->is_boolean()&&bigPicture(session,mode);
}
inline bool registryCompatible(DWORD observed,DWORD expected,bool uiProof) {
    // A typed current-UI request is checked in the Steam document immediately
    // before bootstrap. A second ActiveUser comparison only rejects stale
    // registry snapshots and adds no proof about that actual target document.
    return expected&&(uiProof||observed==expected);
}
// Read-only projection for the existing mode-read log. Never an admission
// result: resolve()/bigPicture() above remain the only business consumers.
// Keep identifiers out; missing process proof must not look like a bad account.
inline Json diagnostic(const steamsession::Session& session,const Json& mode) {
    const bool object=mode.is_object();
    const auto flag=[&](const char* key){const auto it=mode.find(key);return object&&it!=mode.end()&&it->is_boolean()&&it->get<bool>();};
    const auto watch=mode.find("deckyAccountWatchAvailable");
    const bool watchKnown=object&&watch!=mode.end()&&watch->is_boolean();
    const auto account=object?positiveAccount(mode.value("observedAccount",Json())):0;
    const auto pid=object?positiveAccount(mode.value("pid",Json())):0;
    const auto birth=mode.find("birth"),name=mode.find("mode");
    const bool identity=session.pid&&session.created&&pid==session.pid&&object&&birth!=mode.end()&&birth->is_string()&&birth->get_ref<const std::string&>()==std::to_string(session.created);
    const std::string ui=object&&name!=mode.end()&&name->is_string()?name->get<std::string>():std::string{};
    const bool comparable=session.account&&account;
    Json result={{"sessionPidPresent",session.pid!=0},{"sessionBirthPresent",session.created!=0},{"sessionRootPresent",!session.root.empty()},
        {"registryAccountPresent",session.account!=0},{"modeKnown",flag("known")},{"modeRunning",flag("running")},
        {"modeIdentityConfirmed",identity},{"uiAccountPresent",account!=0},{"uiAccountWatchKnown",watchKnown},
        {"uiAccountWatchAvailable",flag("deckyAccountWatchAvailable")},{"uiRegistryAccountComparable",comparable}};
    // Win5 wake logs could prove the mirror but lost these already-read official
    // layer facts. Unknown is separate from false; this projection adds no IO,
    // new events or admission/recovery decisions.
    for(const auto& field: {std::pair{"deckyBackendConnected","deckyBackendKnown"},
                           std::pair{"deckyHooksInstalled","deckyHooksKnown"},std::pair{"deckyClientConnected","deckyClientKnown"},std::pair{"deckyClientChecking","deckyClientCheckingKnown"},std::pair{"deckyClientVerified","deckyClientVerifiedKnown"}}){
        const auto value=mode.find(field.first);
        const bool known=object&&value!=mode.end()&&value->is_boolean();
        result[field.second]=known;
        if(known)result[field.first]=*value;
    }
    const auto backendState=mode.find("deckyBackendState");
    if(object&&backendState!=mode.end()&&backendState->is_string()&&deckyfrontend::backendState(backendState->get_ref<const std::string&>()))result["deckyBackendState"]=*backendState;
    const auto clientState=mode.find("deckyClientState");
    if(object&&clientState!=mode.end()&&clientState->is_string()&&deckyfrontend::clientState(clientState->get_ref<const std::string&>()))result["deckyClientState"]=*clientState;
    // Describe only an already-returned busy result; no retry or action authority.
    const auto busyReason=mode.find("reason");
    if(object&&busyReason!=mode.end()&&busyReason->is_string()&&busyReason->get_ref<const std::string&>()=="live-busy"){
        const auto local=mode.find("localReadBusy");
        result["modeReadBusyKind"]=local==mode.end()?"shared-live-channel":
            mode.size()==7&&identity&&flag("known")&&flag("running")&&ui=="unknown"&&
            local->is_boolean()&&local->get<bool>()?"native-read-lock":"unknown";
    }
    const auto selected=resolve(session,mode);
    result["uiAccountSelected"]=selected.fromUi;
    result["registryMismatchIgnored"]=comparable&&session.account!=account&&selected.fromUi;
    if(comparable)result["uiRegistryAccountMatches"]=session.account==account;
    const char* stage="verified-steam-ui";
    if(!session.pid)stage="session-pid-missing";
    else if(!session.created)stage="session-birth-missing";
    else if(session.root.empty())stage="session-root-missing";
    else if(!flag("known")||!flag("running")||(ui!="desktop"&&ui!="bigpicture"))stage="mode-unconfirmed";
    else if(!identity)stage="mode-identity-mismatch";
    else if(ui=="desktop")stage="desktop";
    else if(comparable&&session.account!=account)stage="account-mismatch";
    else if(session.account)stage="registry-account";
    else if(!account)stage="ui-account-missing";
    else if(!flag("deckyAccountWatchAvailable"))stage="ui-account-watch-unconfirmed";
    result["proofStage"]=stage;return result;
}
// Conflict telemetry projects one publication from the common Steam owner.
// It never rereads HKCU or grants account authority. A missing/foreign process
// receipt stays unknown; zero is a readable raw value, not a logged-out proof.
inline Json diagnosticWithIdentity(const steamsession::Session& session,const Json& mode,
        const std::shared_ptr<const steamsession::IdentitySnapshot>& identity) {
    auto result=diagnostic(session,mode);
    if(result.value("proofStage",std::string{})!="account-mismatch")return result;
    result["registryReadFromSharedSnapshot"]=true;
    result["registrySnapshotAvailable"]=bool(identity);
    const bool sameProcess=identity&&identity->presence==1&&identity->session.pid==session.pid&&
        identity->session.created==session.created&&identity->session.root==session.root;
    result["registrySnapshotMatchesProcess"]=sameProcess;
    if(!sameProcess){
        result["registryReadPidKnown"]=false;result["registryReadPidError"]=ERROR_NOT_READY;
        result["registryReadKnown"]=false;result["registryReadError"]=ERROR_NOT_READY;return result;
    }
    const bool pidKnown=identity->registeredPidReadError==ERROR_SUCCESS;
    result["registryReadPidKnown"]=pidKnown;
    if(pidKnown){
        result["registryReadPidPresent"]=identity->registeredPid!=0;
        result["registryReadPidMatchesSession"]=identity->registeredPid==session.pid;
    }else result["registryReadPidError"]=static_cast<unsigned long>(identity->registeredPidReadError);
    const bool known=identity->accountReadError==ERROR_SUCCESS;
    result["registryReadKnown"]=known;
    if(!known){result["registryReadError"]=static_cast<unsigned long>(identity->accountReadError);return result;}
    const auto current=identity->session.account;
    result["registryReadAccountPresent"]=current!=0;
    result["registryReadMatchesSession"]=current==session.account;
    result["registryReadMatchesUi"]=current==positiveAccount(mode.value("observedAccount",Json()));
    return result;
}
} // namespace ymcc::deckysession
