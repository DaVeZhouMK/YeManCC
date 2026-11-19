#pragma once
#include "decky_frontend_state.h"
// UI mode uses Valve's process-wide UI API, not geometry/foreground/start flags.
#include "steam_session_observer.h"
#include <shellapi.h>
#include <vector>
#include "steam_live_cdp.h"
#include "steam_failure_diagnostics.h"
#include "steam_ui_read_budget.h"
#include "steam_command_coordinator.h"
#include <optional>
#include "decky_update_recovery.h"
namespace ymcc::steamui {
using Json = nlohmann::json;
inline Json read(steamsession::Observer& observer, bool force = false, const std::function<bool()>& ownerCurrent = {},
    steamlive::ModeTransportWait modeWait = steamlive::ModeTransportWait::Immediate,
    steamlive::ModeTransportObservation* modeObservation = nullptr) {
    if(modeObservation)*modeObservation={};
    const auto identity = observer.identitySnapshot();
    const auto session = identity ? identity->session : steamsession::Session{};
    const int presence = identity ? identity->presence : -1;
    Json::object_t initial;
    initial.emplace("known", presence >= 0);
    initial.emplace("running", presence == 1);
    initial.emplace("mode", presence == 0 ? "stopped" : "unknown");
    initial.emplace("pid", session.pid);
    initial.emplace("birth", std::to_string(session.created));
    initial.emplace("reason", "");
    Json state(std::move(initial));
    const auto ownerAlive = [&]() noexcept { try { return !ownerCurrent || ownerCurrent(); } catch (...) { return false; } };
    const auto cancelled = [&] { state["known"] = false; state["reason"] = "shared-mode-read-cancelled"; return state; };
    if (!ownerAlive()) return cancelled();
    if (presence != 1 || !session.pid || session.root.empty()) return state;
    static std::mutex readMutex;
    static ReadBudget budget; static steamsession::Session cachedSession; static Json cached;
    static std::uint64_t cachedProcessEpoch=0,cachedAccountEpoch=0;
    std::unique_lock<std::mutex> readGuard(readMutex, std::try_to_lock);
    if (!readGuard.owns_lock()) { state["reason"] = "live-busy"; state["localReadBusy"] = true; return state; }
    if (!ownerAlive()) return cancelled();
    if (cachedSession.pid != session.pid || cachedSession.created != session.created || cachedSession.root != session.root) {
        cachedSession = session; cached = Json{}; budget.reset();
    }
    // Producer/account invalidation discards old UI proof without refunding
    // the existing automatic read budget. Only a real process/session change
    // above or the original explicit force request resets that allowance.
    if (cachedProcessEpoch!=identity->processEpoch || cachedAccountEpoch!=identity->accountEpoch) {
        cached=Json{};cachedProcessEpoch=identity->processEpoch;cachedAccountEpoch=identity->accountEpoch;
    }
    if (!budget.begin(GetTickCount64(), force)) {
        if (cached.is_object()) {
            // A cached UI proof still belongs to the original request and identity.
            // Refusal spends no new read slot and cannot trigger discovery.
            if (!ownerAlive()) return cancelled();
            if (!observer.sameIdentityEpoch(identity)) {
                state["known"] = false; state["reason"] = "steam-session-changed"; return state;
            }
            return cached;
        }
        state["reason"] = "ui-auto-query-paused"; return state;
    }
    const auto stillCurrent = [&] { return ownerAlive() && observer.sameIdentityEpoch(identity); };
    const auto result = steamlive::runOwned(observer,session.root, {{"operation", "ui.mode.get"}}, {}, 4000, stillCurrent, &session,
        false, nullptr, modeWait, modeObservation);
    // Preserve only this read's existing safe error before a late identity
    // invalidation returns. The state retains its original PID/birth; facts
    // never turn a failure into mode proof or alter the read budget.
    try{const auto failure=result.find("steamFailure");
        if(!result.value("ok",false)&&failure!=result.end()&&failure->is_object())
            state["steamFailure"]=steamfailure::normalize(*failure);
    }catch(...){} // Diagnostics must not affect the original read outcome.
    if (!ownerAlive()) { budget.finish(false); cached = Json{}; return cancelled(); }
    if (!stillCurrent()) { state["known"] = false; state["reason"] = "steam-session-changed"; budget.finish(false); cached = state; return state; }
    if (result.value("ok", false)) {
        const auto mode = result.value("mode", std::string("unknown"));
        if (mode == "desktop" || mode == "bigpicture") {
            state["mode"] = mode;
            const auto copyBoolean = [&](const char* field) {
                const auto value = result.find(field);
                if (value != result.end() && value->is_boolean()) state[field] = *value;
            };
            // Private read-only proof for Decky offline startup; no account is
            // adopted into the shared observer or any controller/settings owner.
            const auto account=result.find("observedAccount");
            if(account!=result.end()&&account->is_number_integer()&&
               (account->is_number_unsigned()?account->get<uint64_t>()>0&&account->get<uint64_t>()<=MAXDWORD:
                account->get<int64_t>()>0&&account->get<int64_t>()<=MAXDWORD))state["observedAccount"]=*account;
            copyBoolean("deckyAccountWatchAvailable");
            const auto epoch = result.value("modeEpoch", std::string{});
            if (epoch.size() <= 96) state["modeEpoch"] = epoch;
            copyBoolean("deckyFrontendLoaded");
            for(const char* field:{"deckyBackendConnected","deckyHooksInstalled","deckyClientConnected","deckyClientChecking","deckyClientVerified"})
                copyBoolean(field);
            const auto backendState=result.find("deckyBackendState");
            if(backendState!=result.end()&&backendState->is_string()&&deckyfrontend::backendState(backendState->get_ref<const std::string&>()))state["deckyBackendState"]=*backendState;
            const auto clientState=result.find("deckyClientState");
            if(clientState!=result.end()&&clientState->is_string()&&deckyfrontend::clientState(clientState->get_ref<const std::string&>()))state["deckyClientState"]=*clientState;
            const auto residue=ymcc::deckyupdate::residue(result.value("deckyUpdateResidue",Json()));
            if(!residue.empty())state["deckyUpdateResidue"]=residue;
            const auto empty=result.value("deckyEmptyFrontend",Json());
            if(empty.is_object()&&empty.size()==3&&empty.value("protocol",0)==1&&
                ymcc::deckyupdate::fingerprint(empty.value("frontendBuildId",std::string{}))&&
                ymcc::deckyupdate::loaderVersion(empty.value("loaderVersion",std::string{})))state["deckyEmptyFrontend"]=empty;
        }
        else state["reason"] = "steam-ui-mode-unsupported";
    } else state["reason"] = result.value("reason", std::string("live-read-unavailable"));
    budget.finish(state.value("known", false) && (state.value("mode", std::string{}) == "desktop" || state.value("mode", std::string{}) == "bigpicture"));
    cached = state; return state;
}
inline const wchar_t* uriFor(const std::string& mode) {
    if (mode == "bigpicture") return L"steam://open/bigpicture";
    if (mode == "desktop") return L"steam://close/bigpicture";
    throw std::runtime_error("Invalid Steam UI mode");
}
// A fixed command to the same verified running client. The repeatable owner
// check never reserves recovery permission; admitAction may do so, once, only
// after process verification and immediately before the final dispatch checks.
enum class ClientCommand { BigPicture, Desktop, Stop, Home };
struct ClientDispatch {
    bool current=false; bool accepted=false;
    steamcommand::Disposition disposition=steamcommand::Disposition::Unavailable;
    steamcommand::DispatchResult typed()const noexcept {
        return {accepted?steamcommand::Disposition::Submitted:disposition!=steamcommand::Disposition::Unavailable?disposition:
            current?steamcommand::Disposition::Failed:steamcommand::Disposition::Revoked};
    }
};
// The held client must belong to this Windows user and logon session.
// Unknown token facts never authorize the fixed command or reserve recovery.
inline bool clientCommandProcessOwner(HANDLE process) noexcept {
    try {
        struct Token { HANDLE value=nullptr;~Token(){if(value)CloseHandle(value);} } own,target;
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&own.value)||
           !OpenProcessToken(process,TOKEN_QUERY,&target.value))return false;
        DWORD ownSession=0,targetSession=0,ownBytes=0,targetBytes=0;
        if(!GetTokenInformation(own.value,TokenSessionId,&ownSession,sizeof(ownSession),&ownBytes)||
           !GetTokenInformation(target.value,TokenSessionId,&targetSession,sizeof(targetSession),&targetBytes)||
           ownBytes!=sizeof(ownSession)||targetBytes!=sizeof(targetSession)||ownSession!=targetSession)return false;
        const auto user=[](HANDLE token,std::vector<BYTE>& storage)->PSID {
            DWORD bytes=0;
            if(GetTokenInformation(token,TokenUser,nullptr,0,&bytes)||GetLastError()!=ERROR_INSUFFICIENT_BUFFER||
               bytes<sizeof(TOKEN_USER)||bytes>65536)return nullptr;
            storage.resize(bytes);DWORD returned=0;
            if(!GetTokenInformation(token,TokenUser,storage.data(),bytes,&returned)||
               returned<sizeof(TOKEN_USER)||returned>bytes)return nullptr;
            const auto sid=reinterpret_cast<const TOKEN_USER*>(storage.data())->User.Sid;
            const auto base=reinterpret_cast<UINT_PTR>(storage.data()),address=reinterpret_cast<UINT_PTR>(sid);
            if(address<base||address-base>returned||returned-(address-base)<8)return nullptr;
            const auto size=8u+4u*reinterpret_cast<const SID*>(sid)->SubAuthorityCount;
            if(size>returned-(address-base)||!IsValidSid(sid))return nullptr;
            return sid;
        };
        std::vector<BYTE> ownUser,targetUser;
        const auto ownSid=user(own.value,ownUser),targetSid=user(target.value,targetUser);
        return ownSid&&targetSid&&EqualSid(ownSid,targetSid)!=FALSE;
    } catch(...) {return false;}
}
inline bool clientCommandCurrent(const std::function<bool()>& ownerCurrent) noexcept {
    try { return ownerCurrent && ownerCurrent(); } catch(...) { return false; }
}
inline ClientDispatch dispatchClientCommand(const steamsession::Session& session,ClientCommand command,
        bool displayTarget,const std::function<bool()>& ownerCurrent,const std::function<bool()>& admitAction={},
        const std::function<bool()>& sessionCurrent={}) {
    const wchar_t* parameters=nullptr;
    switch(command){
    case ClientCommand::BigPicture:parameters=L"-url \"steam://open/bigpicture\"";break;
    case ClientCommand::Desktop:parameters=L"-url \"steam://close/bigpicture\"";break;
    case ClientCommand::Stop:parameters=L"-shutdown";break;
    case ClientCommand::Home:parameters=L"-url \"steam://open/gamepadui\"";break;
    default:return {};
    }
    if(!session.pid||!session.created||session.root.empty()||!clientCommandCurrent(ownerCurrent))return {};
    std::optional<steamcommand::Scope> operation;
    if(!steamcommand::remote){
        operation.emplace(steamcommand::Target{session.pid,session.created,session.root});
        if(!*operation)return {false,false,operation->failure()};
    }
    ULONGLONG birth=0;
    struct Process { HANDLE value=nullptr;~Process(){if(value)CloseHandle(value);} } process;
    process.value=steamsession::verifiedProcess(session.pid,session.root,&birth);
    if(!process.value||birth!=session.created)return {};
    const auto exe=session.root+L"\\steam.exe";
    SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_FLAG_NO_UI;info.lpVerb=L"open";
    info.lpFile=exe.c_str();info.lpParameters=parameters;info.lpDirectory=session.root.c_str();
    info.nShow=displayTarget?SW_SHOWNORMAL:SW_HIDE;
    // A caller's identity read may wait for its mutex. Finish that read first,
    // then recheck the held process and the original exit/power owner.
    const auto ready=[&]{return (!sessionCurrent||clientCommandCurrent(sessionCurrent))&&
        clientCommandProcessOwner(process.value)&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT&&clientCommandCurrent(ownerCurrent);};
    bool current=ready();
    if(!current)return {};
    const auto effect=command==ClientCommand::Desktop?steamcommand::Effect::Desktop:
        command==ClientCommand::BigPicture?steamcommand::Effect::BigPicture:
        command==ClientCommand::Stop?steamcommand::Effect::Stop:steamcommand::Effect::Home;
    steamcommand::Dispatch dispatch(effect);
    if(!dispatch)return {false,false,steamcommand::remote?steamcommand::Disposition::Revoked:steamcommand::Disposition::Busy};
    if(current&&admitAction){
        if(!admitAction())return {};
        current=ready();
    }
    const bool accepted=current&&ShellExecuteExW(&info);
    dispatch.complete(accepted);
    return {current,accepted,accepted?steamcommand::Disposition::Submitted:
        current?steamcommand::Disposition::Failed:steamcommand::Disposition::Revoked}; // Submission only.
}
// Manual main-process commands retain the same source through the original
// synchronous dispatch. Authenticated delegated callers keep the raw entry.
template<class Source>
inline ClientDispatch dispatchClientCommandOwned(Source& source,const steamsession::Session& session,ClientCommand command,
        bool displayTarget,const std::function<bool()>& ownerCurrent,const std::function<bool()>& admitAction={},
        const std::function<bool()>& sessionCurrent={}) {
    const auto reading=source.sourceRead();
    if(!reading)return {};
    const auto view=reading.view();
    const auto current=[&]() noexcept{return view.current()&&clientCommandCurrent(ownerCurrent);};
    if(!current())return {};
    steamcommand::Scope operation({session.pid,session.created,session.root},steamcommand::Plan::Single,false);
    if(!operation)return {false,false,operation.failure()};
    return dispatchClientCommand(session,command,displayTarget,current,admitAction,sessionCurrent);
}
inline Json set(steamsession::Observer& observer, const Json& args,const std::function<bool()>& ownerCurrent={}) {
    // Restored settings, passive reads and old argument shapes are never a mode command.
    if (!args.is_object() || !args.contains("userInitiated") || !args["userInitiated"].is_boolean() || !args["userInitiated"].get<bool>())
        throw std::runtime_error("Steam UI switch requires an explicit user action");
    if (args.size() != 4 || !args.contains("mode") || !args["mode"].is_string() ||
        !args.contains("pid") || !args["pid"].is_number_unsigned() || !args.contains("birth") || !args["birth"].is_string())
        throw std::runtime_error("Invalid Steam UI switch");
    const auto mode = args["mode"].get<std::string>();
    uriFor(mode); // Fixed allowlist: no arbitrary executable/URL from UI.
    const auto identity = observer.identitySnapshot();
    const auto session = identity ? identity->session : steamsession::Session{};
    if (!identity || identity->presence != 1 || !session.pid || args["pid"].get<uint64_t>() != session.pid ||
        args["birth"].get<std::string>() != std::to_string(session.created))
        return {{"ok", false}, {"reason", "steam-session-changed"}};
    const auto result=dispatchClientCommandOwned(observer,session,mode=="bigpicture"?ClientCommand::BigPicture:ClientCommand::Desktop,true,
        ownerCurrent,{},[&]{return observer.sameIdentityEpoch(identity);});
    if(result.disposition==steamcommand::Disposition::Busy)
        return {{"ok",false},{"reason","steam-command-busy"},{"requestedMode",mode},{"commandSubmitted",false}};
    // Preserve submission separately when the original owner retires during
    // the shell call. Do not confirm against identity retired during this call.
    if (!clientCommandCurrent(ownerCurrent) || !observer.sameIdentityEpoch(identity))
        return {{"ok",false},{"reason","steam-session-changed"},{"requestedMode",mode},{"commandSubmitted",result.accepted}};
    // Submission is NOT proof of a transition. UI waits for a real mode read.
    return {{"ok",result.accepted},{"reason",result.accepted?"":result.current?"steam-ui-command-failed":"steam-session-changed"},
        {"requestedMode",mode}};
}

// Internal recovery port. Only the private Decky child calls this with a
// fresh account witness + durable reservation; no fabricated manual intent.
inline bool dispatchRecoveryMode(const steamsession::Session& session,const std::string& mode,const std::function<bool()>& admitAction,
        const std::function<bool()>& ownerCurrent){
    uriFor(mode);
    if(!admitAction)return false; // Missing recovery authorization never becomes a manual dispatch.
    // Reopening Big Picture has display intent; desktop close stays hidden.
    return dispatchClientCommand(session,mode=="bigpicture"?ClientCommand::BigPicture:ClientCommand::Desktop,
        mode=="bigpicture",ownerCurrent,admitAction).accepted;
}

// Bring the already-running Big Picture UI forward only on an explicit click.
// Reuse the fixed native mode command without granting any loader/CSS startup.
inline Json showBigPicture(steamsession::Observer& observer, const Json& args,const std::function<bool()>& ownerCurrent={}) {
    if (!args.is_object() || !args.contains("userInitiated") || !args["userInitiated"].is_boolean() || !args["userInitiated"].get<bool>())
        throw std::runtime_error("Steam Big Picture display requires an explicit user action");
    if (args.size() != 3 || !args.contains("pid") || !args["pid"].is_number_unsigned() || !args.contains("birth") || !args["birth"].is_string())
        throw std::runtime_error("Invalid Steam Big Picture display request");
    if(!clientCommandCurrent(ownerCurrent))return {{"ok",false},{"reason","steam-session-changed"}};
    // Keep the original public identity across the independent mode read;
    // a same-valued successor cannot turn this old display intent into a command.
    const auto identity=observer.identitySnapshot();
    if(!identity||identity->presence!=1||identity->session.pid!=args["pid"].get<uint64_t>()||
       std::to_string(identity->session.created)!=args["birth"].get<std::string>())
        return {{"ok",false},{"reason","steam-session-changed"}};
    const auto current=[&]{return clientCommandCurrent(ownerCurrent)&&observer.sameIdentityEpoch(identity);};
    const auto state = read(observer, true,current);
    if(!current())return {{"ok",false},{"reason","steam-session-changed"}};
    if (!state.value("known", false) || !state.value("running", false) ||
        state.value("pid", uint64_t{0}) != args["pid"].get<uint64_t>() || state.value("birth", std::string{}) != args["birth"].get<std::string>())
        return {{"ok", false}, {"reason", "steam-session-changed"}};
    if (state.value("mode", std::string{}) != "bigpicture") return {{"ok", false}, {"reason", "steam-bigpicture-required"}};
    auto request = args; request["mode"] = "bigpicture";
    return set(observer, request,current); // One fixed URI to this verified client; no fallback or delayed replay.
}

// Graceful fixed-command exit of the same verified client. A submission is not
// an exit receipt; observers must later confirm the process is stopped.
inline Json stop(steamsession::Observer& observer,const Json& args,const std::function<bool()>& ownerCurrent={}) {
    if(!args.is_object()||!args.contains("userInitiated")||!args["userInitiated"].is_boolean()||!args["userInitiated"].get<bool>())
        throw std::runtime_error("Steam exit requires an explicit user action");
    if(args.size()!=3||!args.contains("pid")||!args["pid"].is_number_unsigned()||!args.contains("birth")||!args["birth"].is_string())
        throw std::runtime_error("Invalid Steam exit request");
    const auto identity=observer.identitySnapshot();
    const auto session=identity?identity->session:steamsession::Session{};
    if(!identity||identity->presence!=1||!session.pid||args["pid"].get<uint64_t>()!=session.pid||args["birth"].get<std::string>()!=std::to_string(session.created))
        return {{"ok",false},{"reason","steam-session-changed"}};
    const auto result=dispatchClientCommandOwned(observer,session,ClientCommand::Stop,false,
        ownerCurrent,{},[&]{return observer.sameIdentityEpoch(identity);});
    if(result.disposition==steamcommand::Disposition::Busy)
        return {{"ok",false},{"reason","steam-command-busy"},{"requestedState","stopped"},{"commandSubmitted",false}};
    // This receipt reports the original shell submission only. Normal fast
    // exit may retire identity before return; no successor action is admitted.
    return {{"ok",result.accepted},{"reason",result.accepted?"":result.current?"steam-exit-command-failed":"steam-session-changed"},{"requestedState","stopped"}};
}
}
