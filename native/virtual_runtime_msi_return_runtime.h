#pragma once
// Exact post-final MSI return only. The original quarantine worker calls this;
// BUS sees only due metadata. No mode write, selected-slot admission or new timer.
namespace ymcc::inputHost::runtimeMsiReturn {
struct Proof {
    std::uint64_t serial=0,episode=0,mode=0,sourceEvent=0;
    unsigned topology=0,identity=0,checks=0;
    ULONGLONG nextAt=0,firstAt=0;
    unsigned long long firstSequence=0;
    bool captured=false,qualified=false;
    msiRecovery::Record expected;
    ymcc::sleeppower::SharedPowerV1 powerScope;
    MsiOwnedReturnSignal signal;
};
static Proof proof; // Existing Host mutex; never shares normal-OFF proof state.
static bool incidentTokens() {
    namespace runtime=ymcc::inputHost::runtimeRecovery;
    // Retiring the fault revokes its Start, not the exact stopped Host's return duty.
    return g_inputHostMx.ownedByCurrentThread() && proof.captured && proof.serial==runtime::state.serial &&
        runtime::state.stopInvoked && runtime::oldTuple() &&
        !runtime::stale(true,runtime::PowerScope::CurrentReturn) && msiClawKnownBoardAuthority() && hardwareWriteAllowed() &&
        !g_inputHostProcess && !g_inputHostPrimaryThread && g_inputHostReleaseQuarantined &&
        !g_inputHostActive && !g_msiDinputRouteDinputArmed.load() &&
        g_msiDinputReturnDebtEpisode.load()==proof.episode && !g_msiNativeForegroundCyclePending.load() &&
        !g_msiNativeForegroundPowerHold.load() && !msiForegroundIsolationBorrowBusy();
}
static bool incidentCurrent() {return incidentTokens() && msiDinputDeviceTargetAbsent();}
static bool tokensCurrent(unsigned long long mode,unsigned topology,unsigned identity,unsigned long long event) {
    return incidentTokens() && !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) &&
        g_msiFeedbackModeGeneration.load()==mode && g_xinputTopologyEpoch.load()==topology &&
        g_xinputSlotIdentityEpoch.load()==identity && g_msiOwnedReturnSourceEvent.load()==event;
}
static bool recordCurrent() { // g_msiCrashMx held, no nested lock acquisition.
    const auto& record=g_msiCrashRecord;
    const auto originalAttempt=proof.expected.modeAttemptOwner;
    const auto ownAttempt=std::to_string(proof.expected.ownerPid)+":"+proof.expected.ownerCreationTime;
    return g_msiCrashLoaded && !g_msiCrashStoreInvalid &&
        msiOwnedReturnSameReceiptOwner(proof.expected,record) &&
        (record.modeReturnPending ? (record.modeAttemptOwner==originalAttempt || record.modeAttemptOwner==ownAttempt) :
            record.modeAttemptOwner.empty());
}
static bool sameSuspend(const ymcc::sleeppower::SharedPowerV1& power) noexcept {
    return proof.powerScope.generation==power.generation && proof.powerScope.suspendSerial==power.suspendSerial;
}
}
static void inputHostRuntimeMsiReturnCaptureLocked() {
    using namespace ymcc::inputHost::runtimeMsiReturn;
    proof={};
    if(!g_inputHostMx.ownedByCurrentThread() || !msiClawKnownBoardAuthority() || !g_msiDinputReturnOwed.load())return;
    std::lock_guard<std::mutex> durable(g_msiCrashMx);
    const auto& record=g_msiCrashRecord;
    if(!g_msiCrashLoaded || g_msiCrashStoreInvalid || !msiRecovery::validRecord(record) || !record.modeReturnPending ||
        record.boardKey!=msiCrashBoardKey() || record.ownerPid!=GetCurrentProcessId() ||
        record.ownerCreationTime!=msiCrashProcessCreation(GetCurrentProcess()) || record.locationPaths.empty())return;
    proof.expected=record;proof.serial=ymcc::inputHost::runtimeRecovery::state.serial;
    proof.episode=g_msiDinputReturnDebtEpisode.load();proof.captured=proof.episode!=0;
}
static bool inputHostRuntimeMsiReturnRequiredLocked() {
    return ymcc::inputHost::runtimeMsiReturn::incidentCurrent();
}
static bool inputHostRuntimeMsiReturnProbeDueLocked() {
    using namespace ymcc::inputHost::runtimeMsiReturn;
    if(!incidentCurrent() || (!proof.qualified && GetTickCount64()<proof.nextAt))return false;
    ymcc::sleeppower::SharedPowerV1 power;
    return captureSharedPowerLifecycle(power) &&
        (proof.qualified || proof.checks<20 || !sameSuspend(power));
}
static bool inputHostRuntimeMsiReturnProofCurrentLocked() {
    using namespace ymcc::inputHost::runtimeMsiReturn;
    if(!proof.qualified || !sharedPowerLifecycleCurrent(proof.powerScope) ||
        !tokensCurrent(proof.mode,proof.topology,proof.identity,proof.sourceEvent) ||
        g_msiDinputReturnOwed.load() || !msiOwnedReturnSignalCurrent(proof.signal,currentPowerGeneration(),proof.topology,GetTickCount64()))return false;
    {std::unique_lock<std::mutex> durable(g_msiCrashMx,std::try_to_lock);
     if(!durable.owns_lock() || !recordCurrent() || g_msiCrashRecord.modeReturnPending)return false;}
    InputCaptureGamepadSnapshot value;DWORD physical=0;
    {
        std::unique_lock<std::mutex> read(g_gamepadReadMx,std::try_to_lock);if(!read.owns_lock())return false;
        std::unique_lock<std::mutex> sample(g_inputCaptureGamepadSnapshotMx,std::try_to_lock);if(!sample.owns_lock())return false;
        physical=xinputPhysicalConnectedMask();value=g_inputCaptureGamepadSnapshot;
    }
    const bool freshSample=(physical&proof.signal.mask)==proof.signal.mask && value.live &&
        (value.inputSource=="xinput" || value.inputSource=="xinput-any-connected") &&
        value.readAttemptSequence>=proof.signal.sequence && value.readIdentityGeneration==proof.identity &&
        ymcc::virtualOutputSourceCurrent({value.readAttemptSequence,value.readStartedTick,value.readCompletedTick,
            value.readPowerGeneration,value.readTopologyGeneration,value.readContextStable},GetTickCount64(),currentPowerGeneration(),proof.topology);
    // A try-lock or sample copy can cross cancellation/generation retirement.
    // Revalidate after releasing both sample locks; no stale proof publication.
    return freshSample && tokensCurrent(proof.mode,proof.topology,proof.identity,proof.sourceEvent) &&
        msiOwnedReturnSignalCurrent(proof.signal,currentPowerGeneration(),proof.topology,GetTickCount64()) &&
        sharedPowerLifecycleCurrent(value.sharedPower) && sharedPowerLifecycleCurrent(proof.powerScope);
}
static bool inputHostRuntimeMsiReturnPrepareLocked() {
    using namespace ymcc::inputHost::runtimeMsiReturn;
    namespace runtime=ymcc::inputHost::runtimeRecovery;
    if(inputHostRuntimeMsiReturnProofCurrentLocked())return true;
    proof.qualified=false;
    const auto now=GetTickCount64();
    if(!incidentCurrent() || now<proof.nextAt || runtime::held(now))return false;
    ymcc::sleeppower::SharedPowerV1 observation;
    if(!captureSharedPowerLifecycle(observation) || !sharedPowerLifecycleCurrent(observation))return false;
    // Refresh only the read budget; retain the old owner and the next probe time.
    if(!sameSuspend(observation))proof.checks=0;
    if(!ymcc::sleeppower::samePowerRevocation(proof.powerScope,observation)) {proof.firstAt=0;proof.firstSequence=0;}
    proof.powerScope=observation;
    if(proof.checks>=20)return false;
    ++proof.checks;proof.nextAt=now+1000;
    if(!inputHostPipeRetirementProvenLocked())return false;
    std::unique_lock<std::mutex> pump(g_msiPersonaPumpMx,std::try_to_lock);if(!pump.owns_lock())return false;
    const auto mode=g_msiFeedbackModeGeneration.load(),event=g_msiOwnedReturnSourceEvent.load();
    const auto topology=g_xinputTopologyEpoch.load(),identity=g_xinputSlotIdentityEpoch.load();
    const auto power=observation.generation;
    {std::unique_lock<std::mutex> durable(g_msiCrashMx,std::try_to_lock);
     if(!durable.owns_lock() || !recordCurrent())return false;}
    const auto signal=msiOwnedReturnReadSignalFor(proof.expected,power,topology,now);
    if(!signal.matched || !tokensCurrent(mode,topology,identity,event) || !sharedPowerLifecycleCurrent(observation)) {
        proof.firstAt=0;proof.firstSequence=0;return false;
    }
    if(!proof.firstSequence || mode!=proof.mode || topology!=proof.topology || identity!=proof.identity || event!=proof.sourceEvent) {
        proof.mode=mode;proof.topology=topology;proof.identity=identity;proof.sourceEvent=event;
        proof.firstAt=GetTickCount64();proof.firstSequence=signal.sequence;return false;
    }
    if(signal.sequence==proof.firstSequence || GetTickCount64()<proof.firstAt || GetTickCount64()-proof.firstAt<300)return false;
    // Existing SD/PS duties stay independent. Elite/SD also retain full-profile
    // target absence and exact old Host/writer lifetime checks before any return.
    if(g_psEndpointOwnership.retirementRequired()) {
        if(!psRetiredReturnProvenLocked())return false;
    } else {
        if(g_msiSteamDeckReleaseDebt.required() && !msiSteamDeckRecheckOwnedRemovalDebt())return false;
        if(!inputHostTargetReturnBeforeRuntimeRestoreLocked(g_inputHostRunId,g_inputHostEpoch))return false;
    }
    const auto current=[&] {
        return tokensCurrent(mode,topology,identity,event) && recordCurrent() &&
            msiOwnedReturnSignalCurrent(signal,power,topology,GetTickCount64()) && sharedPowerLifecycleCurrent(observation);
    };
    bool owed=false;{std::unique_lock<std::mutex> durable(g_msiCrashMx,std::try_to_lock);
      if(!durable.owns_lock() || !current())return false;owed=g_msiCrashRecord.modeReturnPending;}
    if(owed && !msiDinputTryDischargeReturnDebt(false,true,signal.mask,"runtime-same-owned-MSI-two-fresh-windows",current))return false;
    if(!tokensCurrent(mode,topology,identity,event) || g_msiDinputReturnOwed.load() ||
        !msiOwnedReturnSignalCurrent(signal,power,topology,GetTickCount64()) || !sharedPowerLifecycleCurrent(observation))return false;
    proof.signal=signal;proof.qualified=true;
    runtime::note("msi-return-proven","same-owned-source-two-fresh-windows-and-original-target-proof");
    return inputHostRuntimeMsiReturnProofCurrentLocked();
}
