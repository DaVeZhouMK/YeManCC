#pragma once
#include <atomic>
#include <charconv>
#include <limits>
#include <string>
#include <vector>
#include "virtual_controller_runtime_contract.h"

// Only initial/persona work queued during an admitted player period is bound.
// This is immutable queue provenance, not another lifecycle/state machine.
// Steady policy return and power recovery keep their existing separate authority.
// VC-QUEUED-START-EXPIRY-20261024-R1: a matching expired player request must
// never lose its provenance and fall through as ordinary unbound policy work.
namespace ymcc::inputHost::queuedPlayerStart {
struct Work {
    std::uint64_t request = 0, power = 0, deadline = 0;
    std::string persona, reason;
    bool bound = false;
};
inline constexpr const char* prefix="player-start:";
static bool startReason(const std::string& reason) noexcept {
    return reason=="initial-target-admission"||reason=="persona-transition";
}
// Diagnostic deduplication only, never an admission/clock/recovery authority.
static std::atomic<std::uint64_t> lastExpiredPlayerWorkNoted{0};
static bool refuseExpired(const ymcc::VirtualStartProtection::View& view,
        const std::string& persona,const char* reason) noexcept {
    if(!view.requestId || view.phase!="expired" || view.persona!=persona)return false;
    inputHostRecordSwitchTimeout(view);
    auto previous=lastExpiredPlayerWorkNoted.load(std::memory_order_acquire);
    do {if(previous>=view.requestId)return true;}
    while(!lastExpiredPlayerWorkNoted.compare_exchange_weak(previous,view.requestId,std::memory_order_acq_rel));
    try{appendNativeLifecycleLog("input-host-player-start-work",{
        {"lifecycleVersion",runtimeContract::lifecycleVersion},{"action","rejected"},
        {"reason","expired-player-start-cannot-become-policy-work"},{"queuedReason",reason?reason:"unknown"},
        {"requestId",view.requestId},{"persona",view.persona},{"powerGeneration",currentPowerGeneration()},
        {"originalBudgetMs",ymcc::VirtualStartProtection::maxMs},{"elapsedMs",view.elapsedMs},
        {"remainingMs",view.remainingMs},{"newHostCreated",false},{"budgetRenewed",false},
        {"policy","VC-QUEUED-START-EXPIRY-20261024-R1"}});}catch(...){}
    return true;
}
static std::string bind(const std::string& reason) {
    if(!startReason(reason))return reason;
    const auto now=inputHostStartProtectionNow();const auto view=g_virtualStartProtection.view(now);
    if(refuseExpired(view,inputHostConfiguredPersona(),reason.c_str()))return {};
    if(!view.locked||!view.requestId||!view.remainingMs||view.persona!=inputHostConfiguredPersona()||
        !ymcc::controllerPersona::currentOutput(view.persona)||
        view.remainingMs>(std::numeric_limits<std::uint64_t>::max)()-now)return reason;
    return std::string(prefix)+std::to_string(view.requestId)+":"+std::to_string(currentPowerGeneration())+
        ":"+std::to_string(now+view.remainingMs)+":"+view.persona+":"+reason;
}
// Unbound policy work keeps its existing authority; only the typed prefix is parsed.
static bool decode(const std::string& wire,Work& work) {
    if(wire.rfind(prefix,0)!=0){
        // Boot-without-ticket and steady policy work retain their old authority.
        // Preserve only the raw initial/persona provenance across dispatch to Start.
        if(startReason(wire))work.reason=wire;
        return true;
    }
    const auto text=wire.substr(std::char_traits<char>::length(prefix));
    std::vector<std::string> parts;std::size_t begin=0;
    for(;;){const auto end=text.find(':',begin);parts.push_back(text.substr(begin,end==std::string::npos?end:end-begin));if(end==std::string::npos)break;begin=end+1;if(parts.size()>5)return false;}
    if(parts.size()!=5)return false;
    const auto number=[](const std::string& text,std::uint64_t& value){if(text.empty())return false;const auto r=std::from_chars(text.data(),text.data()+text.size(),value);return r.ec==std::errc{}&&r.ptr==text.data()+text.size()&&value;};
    if(!number(parts[0],work.request)||!number(parts[1],work.power)||!number(parts[2],work.deadline)||
        !ymcc::controllerPersona::currentOutput(parts[3])||!startReason(parts[4]))return false;
    work.persona=parts[3];work.reason=parts[4];work.bound=true;return true;
}
static void note(const Work& work,const char* action,const char* reason) noexcept {
    try{appendNativeLifecycleLog("input-host-player-start-work",{
        {"lifecycleVersion",runtimeContract::lifecycleVersion},{"action",action},{"reason",reason},
        {"requestId",work.request},{"powerGeneration",work.power},{"originalDeadline",work.deadline},
        {"persona",work.persona},{"queuedReason",work.reason},{"newHostCreated",false},{"budgetRenewed",false}});}catch(...){}
}
static const char* stale(const Work& work) {
    if(!work.bound)return nullptr;
    const auto now=inputHostStartProtectionNow();const auto view=g_virtualStartProtection.view(now);
    if(view.requestId!=work.request)return "player-request-retired";
    if(currentPowerGeneration()!=work.power)return "power-generation-retired";
    if(now>=work.deadline||!view.locked){if(view.phase=="expired")inputHostRecordSwitchTimeout(view);return "original-player-period-finished";}
    if(view.persona!=work.persona||inputHostConfiguredPersona()!=work.persona)return "player-persona-retired";
    if(g_inputHostSwitchRecoveryStartDeadline&&
        (g_inputHostSwitchRecoveryStartRequestId!=work.request||g_inputHostSwitchRecoveryStartPower!=work.power))return "outer-execution-context-retired";
    return nullptr;
}
// The worker uses the same deadline throat as normal Start, pipe stages and
// fallback. Requeue retains the original wire; scope exit restores outer authority.
static thread_local bool executingPlayerStartWork=false;
struct Scope {
    bool previousPlayerWork=executingPlayerStartWork;
    std::uint64_t deadline=g_inputHostSwitchRecoveryStartDeadline,request=g_inputHostSwitchRecoveryStartRequestId,power=g_inputHostSwitchRecoveryStartPower;
    explicit Scope(const Work& work){
        executingPlayerStartWork=previousPlayerWork||work.bound||startReason(work.reason);
        if(!work.bound)return;
        g_inputHostSwitchRecoveryStartDeadline=deadline&&deadline<work.deadline?deadline:work.deadline;
        g_inputHostSwitchRecoveryStartRequestId=work.request;g_inputHostSwitchRecoveryStartPower=work.power;
    }
    ~Scope(){g_inputHostSwitchRecoveryStartDeadline=deadline;g_inputHostSwitchRecoveryStartRequestId=request;g_inputHostSwitchRecoveryStartPower=power;
        executingPlayerStartWork=previousPlayerWork;}
};
} // namespace ymcc::inputHost::queuedPlayerStart
