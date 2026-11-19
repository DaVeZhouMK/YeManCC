#pragma once
#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// One terminal return owned by the existing lifecycle queue and Host mutex.
// This contains no idle/axis/source timeout and creates no replacement target.
namespace ymcc::inputHost::recoveryFallback {
struct Request {
    std::uint64_t serial=0,request=0,power=0,demand=0,epoch=0,epochCounter=0;
    std::string persona,run,instance,physicalRoot,physicalRoute;
    std::vector<std::wstring> physicalTargets;
    std::shared_ptr<sharedPower::Scope> powerScope;
    ULONGLONG deadline=0,nextProbe=0,stoppedAt=0;
    bool pending=false,queued=false,stopInvoked=false;
    int lastEvidence=-1;
};
static Request request;
static std::uint64_t nextSerial=0;
inline constexpr const char* prefix="recovery-exhausted-physical-fallback:";
static void note(const char* stage,const char* reason,bool cleanup=false,bool visible=false,bool source=false) noexcept {
    try {appendNativeLifecycleLog("input-host-recovery-fallback",{
        {"state",stage},{"reason",reason},{"serial",request.serial},{"requestId",request.request},
        {"powerGeneration",request.power},{"demandRevision",request.demand},
        {"hostEpoch",request.epoch},{"runId",request.run},{"persona",request.persona},
        {"pending",request.pending},{"stopInvoked",request.stopInvoked},
        {"ownedCleanupProven",cleanup},{"ownedVisibilityRestored",visible},{"freshPhysicalRead",source},
        {"physicalReturnScope","current classified native read and exact owned return;not external application acceptance"},
        {"gameInputVerified",false},{"savedPersonaChanged",false},
        {"policy","terminal-failure-exact-owner-stop-return-no-restart"}});}catch(...){}
}
static const char* stale() {
    if(!g_inputHostMx.ownedByCurrentThread())return "host-lock-not-owned";
    if(inputHostExitFenceRaised() || g_exitRequested.load() || g_inputCaptureStop.load())return "exit-or-capture-stop";
    if(!request.powerScope || !request.powerScope->current() ||
       currentPowerGeneration()!=request.power || g_powerLifecycle.load()!=PowerLifecycle::Ready ||
       g_inputHostPowerAdmissionRevoked.load())return "public-power-retired";
    if(!g_inputHostRecoveryTerminalLogged.load() || !request.demand ||
       g_inputHostRecoveryIntent.revision()!=request.demand)return "recovery-demand-retired";
    if(!g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled ||
       inputHostConfiguredPersona()!=request.persona || g_externalPriorityVirtualSuspended.load())return "player-intent-retired";
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    if(view.requestId!=request.request || view.persona!=request.persona || view.phase=="cancelled")return "player-period-retired";
    if(g_inputHostEpochCounter.load()!=request.epochCounter)return "host-creation-retired";
    if(g_inputHostEpoch && (g_inputHostEpoch!=request.epoch || g_inputHostRunId!=request.run ||
       g_inputHostHostInstanceId!=request.instance))return "host-owner-retired";
    return nullptr;
}
// A terminal handoff owns the same fault token before its one Stop runs.
// A spent Stop remains a no-replay fact even after cancellation/expiry; it
// grants no new action or proof of return. Never borrow another Host's claim.
static bool ownsFaultCleanup(std::uint64_t epoch) {
    if(!g_inputHostMx.ownedByCurrentThread() || !epoch || !request.serial ||
       request.epoch!=epoch || g_inputHostEpoch!=epoch ||
       request.epochCounter!=g_inputHostEpochCounter.load() ||
       request.run.empty() || request.instance.empty() ||
       request.run!=g_inputHostRunId || request.instance!=g_inputHostHostInstanceId)return false;
    if(request.stopInvoked)return true;
    // Only the original live public scope and continued demand can own an
    // unexecuted Stop. Missing/retired capture leaves ordinary cleanup intact.
    return request.pending && GetTickCount64()<request.deadline && !stale();
}

static void retire(const char* why) noexcept {
    request.pending=false;request.queued=false;request.powerScope.reset();note("retired",why);
}
static void queue() noexcept {
    if(!request.pending || request.queued)return;
    try {
        const auto work=std::string(prefix)+std::to_string(request.serial);
        request.queued=true;
        if(!inputHostQueueLifecycleWork(work.c_str())){request.queued=false;note("queue-refused","original-queue-refused");}
    }catch(...){request.queued=false;note("queue-refused","queue-exception");}
}
} // namespace ymcc::inputHost::recoveryFallback

static std::uint64_t inputHostRecoveryFallbackWorkToken(const std::string& work) noexcept {
    namespace fallback=ymcc::inputHost::recoveryFallback;
    const std::string_view view(work),prefix(fallback::prefix);
    if(!view.starts_with(prefix))return 0;
    const auto digits=view.substr(prefix.size());
    if(digits.empty())return 0;
    std::uint64_t serial=0;const auto result=std::from_chars(digits.data(),digits.data()+digits.size(),serial);
    return result.ec==std::errc{} && result.ptr==digits.data()+digits.size()?serial:0;
}

// Existing shared exhaustion producer; logging cannot suppress its cleanup work.
static void inputHostRecoveryTerminalLocked(const char* via) {
    namespace fallback=ymcc::inputHost::recoveryFallback;
    if(!g_inputHostMx.ownedByCurrentThread() || g_inputHostRecoveryTerminalLogged.load())return;
    if(!inputHostRecoveryBudgetExhaustedLocked(GetTickCount64()) ||
       !g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled ||
       inputHostExitFenceRaised() || g_exitRequested.load() || g_inputCaptureStop.load() ||
       g_powerLifecycle.load()!=PowerLifecycle::Ready || g_inputHostPowerAdmissionRevoked.load() ||
       g_inputHostVirtualIntentWriters.load() || g_externalPriorityVirtualSuspended.load())return;
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    const auto now=GetTickCount64();
    if(!view.requestId || view.persona=="disabled" || view.persona!=inputHostConfiguredPersona() ||
       view.phase=="cancelled" || fallback::nextSerial==(std::numeric_limits<std::uint64_t>::max)() ||
       now>(std::numeric_limits<ULONGLONG>::max)()-ymcc::VirtualStartProtection::maxMs)return;
    auto powerScope=ymcc::inputHost::sharedPower::Scope::create(&readSharedSystemPower,g_sharedPowerRevocations,
        sgShouldKeepInputDuringStandby(),ymcc::inputHost::sharedPower::Use::Lifecycle);
    if(!powerScope)return;
    fallback::Request captured;
    captured.serial=fallback::nextSerial+1;captured.request=view.requestId;
    captured.power=currentPowerGeneration();captured.demand=g_inputHostRecoveryIntent.revision();
    captured.persona=view.persona;captured.epoch=g_inputHostEpoch;captured.epochCounter=g_inputHostEpochCounter.load();
    captured.run=g_inputHostRunId;captured.instance=g_inputHostHostInstanceId;
    captured.powerScope=std::move(powerScope);captured.deadline=now+ymcc::VirtualStartProtection::maxMs;captured.pending=true;
    if(!captured.demand || !captured.powerScope->current())return;
    fallback::request=std::move(captured);++fallback::nextSerial;
    g_inputHostRecoveryTerminalLogged.store(true,std::memory_order_release);
    g_inputHostFaultHoldsStart.store(true,std::memory_order_release);
    g_inputHostRecoveryFallbackState.store(1,std::memory_order_release);
    try {appendNativeLifecycleLog("input-host-recovery-exhausted",{
        {"via",via?via:"unknown"},{"episodeAttempts",g_inputHostRecoveryEpisodeAttempts.load()},
        {"rollingCount",inputHostRecoveryRollingCountLocked(GetTickCount64())},
        {"serial",fallback::request.serial},{"requestId",view.requestId},
        {"policy","bounded-recovery-then-physical-fallback"}});}catch(...){}
    fallback::note("requested",via?via:"shared-recovery-exhausted");
    fallback::queue();
}

// Existing maintenance tick only; a queued proof pass never repeats Stop.
static void inputHostRecoveryFallbackMaintenanceLocked() {
    namespace fallback=ymcc::inputHost::recoveryFallback;
    auto& request=fallback::request;
    if(!request.pending)return;
    if(const auto reason=fallback::stale()){fallback::retire(reason);return;}
    if(g_inputHostVirtualIntentWriters.load())return;
    const auto now=GetTickCount64();
    if(now>=request.deadline) {
        g_inputHostRecoveryFallbackState.store(3,std::memory_order_release);
        fallback::retire("owned-return-observation-expired-unproven");return;
    }
    if(request.queued || now<request.nextProbe)return;
    request.nextProbe=now+1000;fallback::queue();
}

