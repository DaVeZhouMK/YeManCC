#pragma once
// Explicit cold start only. Manual dispatch has one bounded native Shell fallback;
// logon retains its consumed grant and cross-process time fence. No BAT or URI.
#include <string>
#include <vector>
#include <set>
#include <cstdint>
namespace ymcc::steamlaunch {
enum class Origin { Manual, Logon };
inline constexpr auto startupArguments=L" -bigpicture -offline -noverifyfiles -nobootstrapupdate -skipinitialbootstrap -no-browser -nofriendsui";
struct Ledger { bool bootConsumed=false, dispatched=false; uint64_t dispatchedAt=0; };
struct Presence { int steam=-1; std::set<std::wstring> active, uncertain; };
struct Result { bool ok=false, dispatched=false, alreadyRunning=false; unsigned addonsStarted=0, addonsFailed=0; std::string reason; };
// Only a terminal known not to have left a child can authorize a manual retry.
enum class SteamSpawnOutcome { Dispatched, NotCreated, OwnedChildDestroyed, Uncertain };
template<class Ports> bool dispatchManualSteam(Ports& io,const std::wstring& executable,const std::wstring& arguments) {
    if(arguments!=startupArguments)return false;
    const auto primary=io.spawnSteamPrimary(executable,arguments);
    if(primary==SteamSpawnOutcome::Dispatched)return true;
    if(primary!=SteamSpawnOutcome::NotCreated&&primary!=SteamSpawnOutcome::OwnedChildDestroyed)return false;
    if(!io.manualSteamFallbackAllowed(executable))return false;
    // Exactly one call. Failure/unknown Shell receipt is never replayed here.
    return io.spawnSteamShell(executable,arguments);
}
// Ports implement only environment IO. Tests execute this same transaction.
template<class Ports> Result run(Ports& io, Origin origin) {
    Result result;
    auto fail=[&](const char* reason){result.reason=reason;return result;};
    if(origin==Origin::Logon && !io.bootAuthorized())return fail("steam-logon-not-authorized");
    if(!io.lock())return fail("steam-launch-busy");
    struct Unlock {Ports& io;~Unlock(){io.unlock();}} unlock{io};
    Ledger state;
    if(!io.readLedger(state))return fail("steam-launch-ledger-unavailable");
    if(origin==Origin::Logon) {
        if(state.bootConsumed){result.ok=true;result.reason="steam-logon-already-consumed";return result;}
        // Consume before observing/starting. Restarting YMCC or the helper, failed
        // launches and a Steam the user later closes never reopen this authority.
        state.bootConsumed=true;
        if(!io.saveLedger(state))return fail("steam-launch-ledger-unavailable");
    }
    const auto presence=io.snapshot({});
    if(presence.steam<0)return fail("steam-state-unknown");
    if(presence.steam>0){result.ok=true;result.alreadyRunning=true;result.reason="steam-already-running";return result;}
    const auto now=io.now();
    if(origin==Origin::Logon && state.dispatched && (now<state.dispatchedAt || now-state.dispatchedAt<40000))return fail("steam-launch-confirming");
    const auto executable=io.steamExecutable();
    if(executable.empty())return fail("steam-executable-missing");
    if(origin==Origin::Logon && !io.bootAuthorized())return fail("steam-logon-not-authorized");
    state.dispatched=true;state.dispatchedAt=now;
    // Cross-process receipt survives YMCC/WebView restart and lost IPC replies.
    if(!io.saveLedger(state))return fail("steam-launch-ledger-unavailable");
    // Issue Steam before reading or waiting for linked application setup.
    // Never send startup arguments to Steam that appeared during validation.
    const auto finalPresence=io.snapshot({});
    if(finalPresence.steam<0)return fail("steam-state-unknown");
    if(finalPresence.steam>0){result.ok=true;result.alreadyRunning=true;result.reason="steam-already-running";return result;}
    if(origin==Origin::Logon && !io.bootAuthorized())return fail("steam-logon-not-authorized");
    const bool dispatched=origin==Origin::Manual
        ? io.spawnManualSteam(executable,startupArguments)
        : io.spawn(executable,startupArguments,true);
    if(!dispatched){
        result.reason="steam-launch-refused";
        const auto diagnostic=io.steamLaunchFailure();
        if(!diagnostic.empty())result.reason+=": "+diagnostic;
        return result;
    }
    result.ok=true;result.dispatched=true;result.reason="steam-launch-dispatched";
    try {
        const auto addons=io.addons();
        if(!addons.empty()) {
            const auto linkedPresence=io.snapshot(addons);
            if(linkedPresence.steam<0){result.addonsFailed=static_cast<unsigned>(addons.size());return result;}
            for(const auto& addon:addons) {
                if(linkedPresence.active.count(addon)||linkedPresence.uncertain.count(addon))continue;
                if(io.spawn(addon,L"",false))++result.addonsStarted;else ++result.addonsFailed;
            }
        }
    } catch(...) {
        // Linked work cannot turn an already issued Steam into an apparent
        // non-dispatch and invite another launch of the same explicit action.
        ++result.addonsFailed;
    }
    return result;
}
}
