#pragma once
#include <cstdlib>
#include <sstream>

// VC-LIFECYCLE-20261020-R3 reference, non-executable contract:
// A rejected current report captures Host/power/request/command sequence here.
// An old queued token cannot adopt a replacement Host or a newer player ticket.
// During a player cycle: observe -> normal 10s inspection -> one central fallback.
// A later accepted current-source report cancels an unspent repair; idle/neutral
// cleanup ACKs cannot do that. No invented pipe error and no independent timer.
static bool inputHostTupleRecoveryCurrentLocked(const InputHostTupleRecoveryToken& token) {
    if (!token.epoch || token.epoch!=g_inputHostEpoch || token.power!=currentPowerGeneration())return false;
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    if(view.requestId!=token.requestId || (!token.protectedCycle && view.locked))return false;
    if (token.protectedCycle) {
        if (!view.locked || !view.remainingMs ||
            (view.phase!="pending" && view.phase!="verifying"))return false;
    }
    return true;
}
static bool inputHostTupleFaultCurrentLocked(const ymcc::VirtualStartProtection::View& view) {
    return g_inputHostTupleFault.protectedCycle && g_inputHostTupleFault.requestId==view.requestId &&
        inputHostTupleRecoveryCurrentLocked(g_inputHostTupleFault);
}
static bool inputHostTupleReturnOwnedLocked(std::uint64_t epoch) {
    const auto& normal=ymcc::inputHost::switchExecutionReturnRuntime::state;
    const auto& recovery=g_inputHostSwitchRecovery;
    return (normal.pending && normal.oldEpoch==epoch) ||
        (recovery.hostEpoch==epoch && recovery.stopInvoked) ||
        g_inputHostReleaseQuarantined || g_inputHostStartBlocked || !g_inputHostProcess;
}
static InputHostTupleRecoveryToken inputHostTupleRecoveryTokenLocked(std::uint64_t epoch,std::uint64_t sequence) {
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    return {epoch,currentPowerGeneration(),view.requestId,sequence,g_inputHostTupleAccepted.acceptedSerial,view.locked};
}
static std::string inputHostTupleRecoveryReason(const InputHostTupleRecoveryToken& token) {
    return "tuple-mismatch-reopen:"+std::to_string(token.epoch)+":"+std::to_string(token.power)+":"+
        std::to_string(token.requestId)+":"+std::to_string(token.sequence)+":"+
        std::to_string(token.acceptedSerial)+":"+(token.protectedCycle?"1":"0");
}
static void inputHostTupleRecoveryAcceptedLocked(std::uint64_t epoch,std::uint64_t sequence,bool sourceEligible) {
    if(!sourceEligible)return;
    const auto serial=g_inputHostTupleAccepted.acceptedSerial;
    if(serial==(std::numeric_limits<std::uint64_t>::max)())return;
    // Only fully validated current-source report acceptance advances this order.
    // It is not a source-read sequence or a pipe command sequence.
    g_inputHostTupleAccepted=inputHostTupleRecoveryTokenLocked(epoch,sequence);
    g_inputHostTupleAccepted.acceptedSerial=serial+1;
    if(!g_inputHostTupleFault.epoch || g_inputHostTupleFault.epoch!=epoch ||
        !inputHostTupleRecoveryCurrentLocked(g_inputHostTupleFault) ||
        g_inputHostTupleAccepted.acceptedSerial<=g_inputHostTupleFault.acceptedSerial)return;
    // Only the actual accepted current report calls here, under the Host mutex.
    // A fault already charged/Stopped remains owned by the existing return path.
    auto& request=g_inputHostSwitchRecovery;
    if(request.requestId==g_inputHostTupleFault.requestId && request.tupleFault) {
        if(request.spent || request.stopInvoked)return;
        request.pending=false;request.queued=false;
        g_inputHostSwitchRecoveryPending.store(false,std::memory_order_release);
        request={}; // No fallback reserved. A later distinct real fault may be observed.
    }
    g_inputHostTupleFault={};
}
static bool inputHostTupleRecoveryMaintenanceLocked(std::string& reason) {
    if(!g_inputHostRecoveryPending.load(std::memory_order_acquire) ||
        g_inputHostTupleHealPending.load(std::memory_order_acquire))return false;
    // A boolean without its original token is not authority to invent current work.
    if(!inputHostTupleRecoveryCurrentLocked(g_inputHostDeferredTuple) ||
        inputHostTupleReturnOwnedLocked(g_inputHostDeferredTuple.epoch)) {
        g_inputHostDeferredTuple={};g_inputHostRecoveryPending.store(false,std::memory_order_release);
        g_inputHostRecoveryDeferredUntil.store(0,std::memory_order_release);return false;
    }
    const auto now=GetTickCount64();
    const auto due=g_inputHostRecoveryDeferredUntil.load(std::memory_order_acquire);
    const auto last=g_inputHostRecoveryLastStartTick.load(std::memory_order_acquire);
    if(inputHostRecoveryAdmissionLocked()!=InputHostRecoveryAdmission::Allowed ||
        (due && now<due) || (last && (now<last || now-last<kRecoveryCooldownMs)))return false;
    reason=inputHostTupleRecoveryReason(g_inputHostDeferredTuple);
    g_inputHostRecoveryPending.store(false,std::memory_order_release);g_inputHostDeferredTuple={};return true;
}
static void inputHostPerformTupleRecovery(const std::string& reason) {
    constexpr std::size_t prefix=22;
    InputHostTupleRecoveryToken token;
    std::istringstream text(reason.substr(prefix));char colon=0;unsigned protect=0;
    text>>token.epoch;
    const bool full=text.peek()==':';
    if(full) {
        text>>colon>>token.power>>colon>>token.requestId>>colon>>token.sequence>>colon>>token.acceptedSerial>>colon>>protect;
        if(!text || text.peek()!=std::char_traits<char>::eof())return;
        token.protectedCycle=protect==1;
    }
    std::lock_guard<InputHostMutex> lock(g_inputHostMx);
    // A bare epoch does not establish the original player-cycle owner. Every
    // production producer emits the complete token; never reconstruct its origin.
    if(!full || !inputHostTupleRecoveryCurrentLocked(token)){
        try{appendNativeLifecycleLog("input-host-tuple-recovery",{
            {"stage","stale"},{"reason",full?"original-host-power-or-request-retired":"unbound-queue-token"},
            {"queuedRequestId",token.requestId},{"currentRequestId",g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId},
            {"queuedHostEpoch",token.epoch},{"currentHostEpoch",g_inputHostEpoch},
            {"queuedPower",token.power},{"currentPower",currentPowerGeneration()},
            {"queuedProtectedCycle",token.protectedCycle},{"stopInvoked",false},{"charged",false}});}catch(...){}
        return;
    }
    if(inputHostTupleReturnOwnedLocked(token.epoch))return;
    if(!inputHostProcessAliveLocked())return;
    if(full && token.protectedCycle && g_inputHostTupleAccepted.epoch==token.epoch &&
            g_inputHostTupleAccepted.power==token.power && g_inputHostTupleAccepted.requestId==token.requestId &&
            g_inputHostTupleAccepted.acceptedSerial>token.acceptedSerial)return;
    if(token.protectedCycle) {
        g_inputHostTupleFault=token;
        inputHostSwitchRecoveryOfferLocked(token.requestId,token.power,ERROR_SUCCESS,true);
        // The shared maintenance/executor owns inspection, charge, Stop and Start.
        // Merely observing this sequence fault cannot Stop or spend anything.
        return;
    }
    const auto admission=inputHostRecoveryAdmissionLocked();
    const auto now=GetTickCount64();
    const auto last=g_inputHostRecoveryLastStartTick.load(std::memory_order_acquire);
    if(admission==InputHostRecoveryAdmission::Cancelled)return;
    if(admission==InputHostRecoveryAdmission::DeferredPower ||
        g_shortcutRecordingActive.load(std::memory_order_acquire) || physicalReleaseWindowActive() ||
        g_externalPriorityVirtualSuspended.load(std::memory_order_acquire) ||
        (last && (now<last || now-last<kRecoveryCooldownMs))) {
        g_inputHostDeferredTuple=token;
        g_inputHostRecoveryPending.store(true,std::memory_order_release);
        g_inputHostRecoveryDeferredUntil.store(last?last+kRecoveryCooldownMs:now,std::memory_order_release);return;
    }
    if(admission==InputHostRecoveryAdmission::BudgetExhausted) {
        inputHostRecoveryTerminalLocked("current-tuple-reopen");return;
    }
    // Established post-cycle repair retains the shared public budgets and exact
    // old return owner. This never borrows a newer player ticket.
    inputHostRecoveryChargeAttemptLocked(now);
    g_inputHostTupleHealPending.store(true,std::memory_order_release);
    inputHostStopLocked("tuple-mismatch-reopen");
}
