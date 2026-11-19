#pragma once
// Explicit cold start only. Manual dispatch has one bounded native Shell fallback;
// Logon retains its consumed grant. The advanced owner requires a separate
// protected receipt and the shared final dispatch fence. No BAT or URI.
#include <string>
#include <vector>
#include <set>
#include <cstdint>
#include <functional>
#include <algorithm>
#include <cwctype>
#include <concepts>
namespace ymcc::steamlaunch {
enum class Origin { Manual, Logon, BootSelected, AdvancedLogon };
using FinalDispatchGate = std::function<bool(const std::function<bool()>&)>;
// Return a refusal reason, or retain the caller's synchronous command slot
// through receipt update, dispatch, and any owned-child cleanup.
using LaunchCommandGate = std::function<const char*()>;
struct FinalDispatchResult { bool invoked=false, resumed=false, threw=false; };
// Synchronous same-thread borrowed callback: never retain or defer resume.
// The leaf's result is authoritative, even if the gate lies, throws after
// dispatch, or calls twice. No child cleanup belongs inside this helper.
inline FinalDispatchResult dispatchFinalOnce(const FinalDispatchGate& gate, const std::function<bool()>& resume) {
    FinalDispatchResult result;
    const auto once=[&] {
        if(result.invoked)return false;
        result.invoked=true;
        result.resumed=resume();
        return result.resumed;
    };
    try { if(gate)(void)gate(once);else (void)once(); }
    catch(...) { result.threw=true; }
    return result;
}
inline constexpr auto startupArguments=L" -bigpicture -offline -noverifyfiles -nobootstrapupdate -skipinitialbootstrap -no-browser -nofriendsui";
inline constexpr auto advancedStartupArguments=L" -bigpicture -noverifyfiles -nobootstrapupdate -skipinitialbootstrap";
struct Ledger { bool bootConsumed=false, dispatched=false; uint64_t dispatchedAt=0; };
// Bind to the authenticated logon, not an individual request nonce. Consumption
// must survive owner restart and lost replies. Invalid or uncertain storage must
// fail closed. Never back this port with the ordinary local-appdata file.
struct AdvancedLogonReceipt {
    virtual ~AdvancedLogonReceipt()=default;
    virtual bool lock() noexcept=0;
    virtual void unlock() noexcept=0;
    virtual bool readLedger(Ledger&) noexcept=0;
    virtual bool saveLedger(const Ledger&) noexcept=0;
};
struct Presence { int steam=-1; std::set<std::wstring> active, uncertain; };
struct Result { bool ok=false, dispatched=false, alreadyRunning=false; unsigned addonsStarted=0, addonsFailed=0; std::string reason; };
// Only a terminal known not to have left a child can authorize a manual retry.
enum class SteamSpawnOutcome { Dispatched, NotCreated, OwnedChildDestroyed, Uncertain };
// Selection is a constraint on the existing installation source, not a new discovery source.
inline std::wstring launchExecutableKey(std::wstring value) {
    std::replace(value.begin(),value.end(),L'/',L'\\');
    for(auto& c:value)c=std::towlower(c);
    return value;
}
inline bool manualSteamArgumentsAllowed(const std::wstring& value){return value.empty()||value==startupArguments;}
template<class Ports> bool dispatchManualSteam(Ports& io,const std::wstring& executable,const std::wstring& arguments) {
    if(!manualSteamArgumentsAllowed(arguments))return false;
    const auto primary=io.spawnSteamPrimary(executable,arguments);
    if(primary==SteamSpawnOutcome::Dispatched)return true;
    if(primary!=SteamSpawnOutcome::NotCreated&&primary!=SteamSpawnOutcome::OwnedChildDestroyed)return false;
    if(!io.manualSteamFallbackAllowed(executable))return false;
    // Exactly one call. Failure/unknown Shell receipt is never replayed here.
    return io.spawnSteamShell(executable,arguments);
}
namespace detail {
template<class Ports,class Receipt,class Current,class Authorized>
Result runWithReceipt(Ports& io, Receipt& receipt, Origin origin, const std::function<void()>& beforeSteamStart,
                      const std::wstring& selectedExecutable, const Current& isCurrent, const Authorized& authorized,
                      const LaunchCommandGate& commandGate) {
    Result result;
    auto fail=[&](const char* reason){result.reason=reason;return result;};
    const bool advanced=origin==Origin::AdvancedLogon;
    const bool logon=origin==Origin::Logon||advanced;
    if(!receipt.lock())return fail("steam-launch-busy");
    struct Unlock {Receipt& receipt;~Unlock(){receipt.unlock();}} unlock{receipt};
    if(!isCurrent())return fail("steam-launch-request-expired");
    Ledger state;
    if(!receipt.readLedger(state))return fail("steam-launch-ledger-unavailable");
    if(!isCurrent())return fail("steam-launch-request-expired");
    if(logon) {
        if(state.bootConsumed){result.ok=true;result.reason="steam-logon-already-consumed";return result;}
        if(advanced) {
            // Hold the shared command slot before consuming this logon's grant.
            try{if(const auto* reason=commandGate())return fail(reason);}
            catch(...){return fail("steam-advanced-logon-command-unavailable");}
            if(!isCurrent())return fail("steam-launch-request-expired");
            if(!authorized())return fail("steam-logon-not-authorized");
            if(!isCurrent())return fail("steam-launch-request-expired");
        }
        // Consume before observing/starting. Restarting YMCC or the helper, failed
        // launches and a Steam the user later closes never reopen this authority.
        state.bootConsumed=true;
        if(!receipt.saveLedger(state))return fail("steam-launch-ledger-unavailable");
        if(advanced&&!isCurrent())return fail("steam-launch-request-expired");
    }
    const auto presence=io.snapshot({});
    if(!isCurrent())return fail("steam-launch-request-expired");
    if(presence.steam<0)return fail("steam-state-unknown");
    if(presence.steam>0){result.ok=true;result.alreadyRunning=true;result.reason="steam-already-running";return result;}
    const auto now=io.now();
    if(origin!=Origin::Manual && state.dispatched && (now<state.dispatchedAt || now-state.dispatchedAt<40000))return fail("steam-launch-confirming");
    const auto executable=io.steamExecutable();
    if(!isCurrent())return fail("steam-launch-request-expired");
    if(executable.empty())return fail("steam-executable-missing");
    if(!selectedExecutable.empty()&&launchExecutableKey(selectedExecutable)!=launchExecutableKey(executable))
        return fail("steam-selected-executable-changed");
    if(logon && !authorized())return fail("steam-logon-not-authorized");
    if(!isCurrent())return fail("steam-launch-request-expired");
    if((origin==Origin::BootSelected||origin==Origin::Manual)&&commandGate){
        if(const auto* reason=commandGate())return fail(reason);
        if(!isCurrent())return fail("steam-launch-request-expired");
    }
    state.dispatched=true;state.dispatchedAt=now;
    // Cross-process receipt survives YMCC/WebView restart and lost IPC replies.
    if(!receipt.saveLedger(state))return fail("steam-launch-ledger-unavailable");
    if(!isCurrent())return fail("steam-launch-request-expired");
    // Issue Steam before reading or waiting for linked application setup.
    // Never send startup arguments to Steam that appeared during validation.
    if(origin==Origin::Logon && beforeSteamStart) {
        try {beforeSteamStart();} catch(...) {} // Optional settings cannot revoke the launch grant.
    }
    if(!isCurrent())return fail("steam-launch-request-expired");
    const auto finalPresence=io.snapshot({});
    if(!isCurrent())return fail("steam-launch-request-expired");
    if(finalPresence.steam<0)return fail("steam-state-unknown");
    if(finalPresence.steam>0){result.ok=true;result.alreadyRunning=true;result.reason="steam-already-running";return result;}
    if(logon && !authorized())return fail("steam-logon-not-authorized");
    if(!isCurrent())return fail("steam-launch-request-expired");
    const bool dispatched=origin==Origin::Manual
        ? io.spawnManualSteam(executable,selectedExecutable.empty()?startupArguments:L"")
        : io.spawn(executable,origin==Origin::BootSelected?L"":advanced?advancedStartupArguments:startupArguments,true);
    if(!dispatched){
        result.reason="steam-launch-refused";
        const auto diagnostic=io.steamLaunchFailure();
        if(!diagnostic.empty())result.reason+=": "+diagnostic;
        return result;
    }
    result.ok=true;result.dispatched=true;result.reason="steam-launch-dispatched";
    // A linked program button preserves its original plain launch and does not launch other links.
    if(advanced||origin==Origin::BootSelected||!selectedExecutable.empty())return result;
    try {
        if(!isCurrent())return result; // An issued Steam remains a dispatch.
        const auto addons=io.addons();
        if(!addons.empty()) {
            const auto linkedPresence=io.snapshot(addons);
            if(!isCurrent())return result;
            if(linkedPresence.steam<0){result.addonsFailed=static_cast<unsigned>(addons.size());return result;}
            for(const auto& addon:addons) {
                if(!isCurrent())return result;
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
} // namespace detail

// current is a read-only cancellation checkpoint, not final dispatch authority.
template<class Ports> Result run(Ports& io, Origin origin, const std::function<void()>& beforeSteamStart = {}, const std::wstring& selectedExecutable = {}, const std::function<bool()>& current = {}, const LaunchCommandGate& commandGate = {}) {
    Result result;
    auto fail=[&](const char* reason){result.reason=reason;return result;};
    const bool advanced=origin==Origin::AdvancedLogon;
    const bool logon=origin==Origin::Logon||advanced;
    if(origin!=Origin::Manual&&!logon&&origin!=Origin::BootSelected)return fail("steam-launch-origin-invalid");
    if(advanced&&(!current||beforeSteamStart||!selectedExecutable.empty()))return fail("steam-advanced-logon-context-invalid");
    if(advanced) {
        if constexpr(requires { io.hasFinalDispatchGate(); io.advancedLogonAuthorized(); }) {
            if(!io.hasFinalDispatchGate())return fail("steam-advanced-logon-final-gate-required");
            if(!commandGate)return fail("steam-advanced-logon-command-gate-required");
        }else return fail("steam-advanced-logon-owner-required");
    }
    if(origin==Origin::BootSelected&&(selectedExecutable.empty()||!current))return fail("steam-boot-selected-context-required");
    if(origin==Origin::BootSelected) {
        if constexpr(requires { io.hasFinalDispatchGate(); })
            if(!io.hasFinalDispatchGate())return fail("steam-boot-selected-final-gate-required");
    }
    bool revoked=false;
    const auto isCurrent=[&] {
        if(revoked)return false;
        try { if(current&&!current())revoked=true; }catch(...) { revoked=true; }
        return !revoked;
    };
    const auto authorized=[&] {
        if(!advanced)return io.bootAuthorized();
        if constexpr(requires { io.advancedLogonAuthorized(); }) {
            try {return static_cast<bool>(io.advancedLogonAuthorized());}catch(...) {return false;}
        }else return false;
    };
    if(!isCurrent())return fail("steam-launch-request-expired");
    if(origin==Origin::Logon && !selectedExecutable.empty())return fail("steam-selected-executable-manual-only");
    if(logon && !authorized())return fail("steam-logon-not-authorized");
    if(advanced) {
        if(!isCurrent())return fail("steam-launch-request-expired");
        if constexpr(requires { { io.advancedLogonReceipt() } noexcept -> std::same_as<AdvancedLogonReceipt*>; }) {
            if(auto* receipt=io.advancedLogonReceipt())
                return detail::runWithReceipt(io,*receipt,origin,beforeSteamStart,selectedExecutable,isCurrent,authorized,commandGate);
        }
        return fail("steam-advanced-logon-protected-receipt-required");
    }
    return detail::runWithReceipt(io,io,origin,beforeSteamStart,selectedExecutable,isCurrent,authorized,commandGate);
}
}
