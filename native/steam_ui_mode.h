#pragma once
// UI mode uses Valve's process-wide UI API, not geometry/foreground/start flags.
#include "steam_session_observer.h"
#include <shellapi.h>
#include "steam_live_cdp.h"
#include "steam_ui_read_budget.h"
#include "decky_update_recovery.h"
namespace ymcc::steamui {
using Json = nlohmann::json;
inline Json read(steamsession::Observer& observer, bool force = false) {
    const auto session = observer.sessionSnapshot();
    const int presence = observer.steamPresence();
    Json state{{"known", presence >= 0}, {"running", presence == 1}, {"mode", presence == 0 ? "stopped" : "unknown"},
        {"pid", session.pid}, {"birth", std::to_string(session.created)}, {"reason", ""}};
    if (presence != 1 || !session.pid || session.root.empty()) return state;
    static std::mutex readMutex;
    static ReadBudget budget; static steamsession::Session cachedSession; static Json cached;
    std::unique_lock<std::mutex> readGuard(readMutex, std::try_to_lock);
    if (!readGuard.owns_lock()) { state["reason"] = "live-busy"; return state; }
    if (cachedSession.pid != session.pid || cachedSession.created != session.created || cachedSession.root != session.root) {
        cachedSession = session; cached = Json{}; budget.reset();
    }
    if (!budget.begin(GetTickCount64(), force)) {
        if (cached.is_object()) return cached;
        state["reason"] = "ui-auto-query-paused"; return state;
    }
    ULONGLONG birth = 0;
    HANDLE process = steamsession::verifiedProcess(session.pid, session.root, &birth);
    if (!process || birth != session.created) {
        if (process) CloseHandle(process);
        state["known"] = false; state["reason"] = "steam-session-changed"; budget.finish(false); cached = state; return state;
    }
    const auto stillCurrent = [&] { return observer.sessionSnapshot() == session && WaitForSingleObject(process, 0) == WAIT_TIMEOUT; };
    const auto result = steamlive::run(session.root, {{"operation", "ui.mode.get"}}, stillCurrent, 4000);
    const bool valid = stillCurrent(); CloseHandle(process);
    if (!valid) { state["known"] = false; state["reason"] = "steam-session-changed"; budget.finish(false); cached = state; return state; }
    if (result.value("ok", false)) {
        const auto mode = result.value("mode", std::string("unknown"));
        if (mode == "desktop" || mode == "bigpicture") {
            state["mode"] = mode;
            const auto epoch = result.value("modeEpoch", std::string{});
            if (epoch.size() <= 96) state["modeEpoch"] = epoch;
            if (result.contains("deckyFrontendLoaded") && result["deckyFrontendLoaded"].is_boolean())
                state["deckyFrontendLoaded"] = result["deckyFrontendLoaded"];
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
inline Json set(steamsession::Observer& observer, const Json& args) {
    // Restored settings, passive reads and old argument shapes are never a mode command.
    if (!args.is_object() || !args.contains("userInitiated") || !args["userInitiated"].is_boolean() || !args["userInitiated"].get<bool>())
        throw std::runtime_error("Steam UI switch requires an explicit user action");
    if (args.size() != 4 || !args.contains("mode") || !args["mode"].is_string() ||
        !args.contains("pid") || !args["pid"].is_number_unsigned() || !args.contains("birth") || !args["birth"].is_string())
        throw std::runtime_error("Invalid Steam UI switch");
    const auto mode = args["mode"].get<std::string>();
    const auto uri = uriFor(mode); // Fixed allowlist: no arbitrary executable/URL from UI.
    const auto session = observer.sessionSnapshot();
    if (!session.pid || observer.steamPresence() != 1 || args["pid"].get<uint64_t>() != session.pid ||
        args["birth"].get<std::string>() != std::to_string(session.created))
        return {{"ok", false}, {"reason", "steam-session-changed"}};
    ULONGLONG birth = 0;
    HANDLE process = steamsession::verifiedProcess(session.pid, session.root, &birth);
    if (!process || birth != session.created) {
        if (process) CloseHandle(process);
        return {{"ok", false}, {"reason", "steam-session-changed"}};
    }
    const auto exe = session.root + L"\\steam.exe";
    const std::wstring parameters = std::wstring(L"-url \"") + uri + L"\"";
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_FLAG_NO_UI; info.lpVerb = L"open"; info.lpFile = exe.c_str();
    info.lpParameters = parameters.c_str(); info.lpDirectory = session.root.c_str(); info.nShow = SW_SHOWNORMAL;
    const bool current = observer.sessionSnapshot() == session && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    const bool accepted = current && ShellExecuteExW(&info);
    CloseHandle(process);
    // Submission is NOT proof of a transition. UI waits for a real mode read.
    return {{"ok", accepted}, {"reason", accepted ? "" : current ? "steam-ui-command-failed" : "steam-session-changed"},
        {"requestedMode", mode}};
}

// Bring the already-running Big Picture UI forward only on an explicit click.
// Reuse the fixed native mode command without granting any loader/CSS startup.
inline Json showBigPicture(steamsession::Observer& observer, const Json& args) {
    if (!args.is_object() || !args.contains("userInitiated") || !args["userInitiated"].is_boolean() || !args["userInitiated"].get<bool>())
        throw std::runtime_error("Steam Big Picture display requires an explicit user action");
    if (args.size() != 3 || !args.contains("pid") || !args["pid"].is_number_unsigned() || !args.contains("birth") || !args["birth"].is_string())
        throw std::runtime_error("Invalid Steam Big Picture display request");
    const auto state = read(observer, true);
    if (!state.value("known", false) || !state.value("running", false) ||
        state.value("pid", uint64_t{0}) != args["pid"].get<uint64_t>() || state.value("birth", std::string{}) != args["birth"].get<std::string>())
        return {{"ok", false}, {"reason", "steam-session-changed"}};
    if (state.value("mode", std::string{}) != "bigpicture") return {{"ok", false}, {"reason", "steam-bigpicture-required"}};
    auto request = args; request["mode"] = "bigpicture";
    return set(observer, request); // One fixed URI to this verified client; no fallback or delayed replay.
}

// Graceful fixed-command exit of the same verified client. A submission is not
// an exit receipt; observers must later confirm the process is stopped.
inline Json stop(steamsession::Observer& observer,const Json& args) {
    if(!args.is_object()||!args.contains("userInitiated")||!args["userInitiated"].is_boolean()||!args["userInitiated"].get<bool>())
        throw std::runtime_error("Steam exit requires an explicit user action");
    if(args.size()!=3||!args.contains("pid")||!args["pid"].is_number_unsigned()||!args.contains("birth")||!args["birth"].is_string())
        throw std::runtime_error("Invalid Steam exit request");
    const auto session=observer.sessionSnapshot();
    if(!session.pid||observer.steamPresence()!=1||args["pid"].get<uint64_t>()!=session.pid||args["birth"].get<std::string>()!=std::to_string(session.created))
        return {{"ok",false},{"reason","steam-session-changed"}};
    ULONGLONG birth=0;HANDLE process=steamsession::verifiedProcess(session.pid,session.root,&birth);
    if(!process||birth!=session.created){if(process)CloseHandle(process);return {{"ok",false},{"reason","steam-session-changed"}};}
    const auto exe=session.root+L"\\steam.exe";
    SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_FLAG_NO_UI;info.lpVerb=L"open";info.lpFile=exe.c_str();
    info.lpParameters=L"-shutdown";info.lpDirectory=session.root.c_str();info.nShow=SW_HIDE;
    const bool current=observer.sessionSnapshot()==session&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;
    const bool accepted=current&&ShellExecuteExW(&info);CloseHandle(process);
    return {{"ok",accepted},{"reason",accepted?"":current?"steam-exit-command-failed":"steam-session-changed"},{"requestedState","stopped"}};
}
}
