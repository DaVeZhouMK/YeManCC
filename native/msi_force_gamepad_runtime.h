#pragma once
#include "msi_force_gamepad_plan.h"
#include "msi_force_gamepad_policy.h"

static std::mutex g_msiForceGamepadMx;
static ymcc::msi::forceGamepad::Plan g_msiForceGamepadPlan;
static std::atomic<bool> g_msiForceGamepadPending{false},g_msiForceGamepadRestorePending{false};
static bool g_msiForceGamepadPreferenceSeen=false;
static std::atomic<bool> g_msiForceGamepadEnabled{false};
static std::atomic<uint64_t> g_msiForceGamepadRevision{0},g_msiForceGamepadSequence{0};
static bool msiForceGamepadNativeDebtClear(); // defined after the original crash-return owner
static bool msiForceGamepadEligible() {
    return !g_aiFanMockSession.isolated && msiClawKnownBoardAuthority() &&
        !g_exitRequested.load(std::memory_order_acquire) && !inputHostExitFenceRaised() &&
        oemVendorStackShieldIsWanted();
}
static void msiForceGamepadRequest(const char* reason,unsigned delayMs) {
    if(!msiForceGamepadEligible())return;
    using namespace ymcc::msi::forceGamepad;
    const auto trigger=reason && std::strcmp(reason,"lb-rb")==0 ? Trigger::Shoulders :
        reason && std::strcmp(reason,"window-open")==0 ? Trigger::WindowOpen :
        reason && std::strcmp(reason,"wake")==0 ? Trigger::Wake : Trigger::Startup;
    std::lock_guard lock(g_msiForceGamepadMx);
    if(g_msiForceGamepadPlan.request(trigger,GetTickCount64(),currentPowerGeneration(),delayMs)) {
        g_msiForceGamepadSequence.store(g_msiForceGamepadPlan.sequence(),std::memory_order_release);
        g_msiForceGamepadPending.store(true,std::memory_order_release);
    }
}
static void msiForceGamepadShieldChanged(bool enabled) {
    if(g_aiFanMockSession.isolated || !msiClawKnownBoardAuthority())return;
    bool changed;
    {
        std::lock_guard lock(g_msiForceGamepadMx);
        changed=g_msiForceGamepadPlan.setEnabled(enabled);
        if(!changed && g_msiForceGamepadPreferenceSeen)return;
        g_msiForceGamepadPreferenceSeen=true;
        g_msiForceGamepadRevision.store(g_msiForceGamepadPlan.revision(),std::memory_order_release);
        g_msiForceGamepadEnabled.store(enabled,std::memory_order_release);
        g_msiForceGamepadPending.store(false,std::memory_order_release);
        // OFF must attempt inherited cleanup too; the policy owner verifies the journal.
        g_msiForceGamepadRestorePending.store(!enabled,std::memory_order_release);
    }
    if(enabled)msiForceGamepadRequest("startup",0);
}
// Passed into the existing bounded HID writer while its slot mutex is held.
// This predicate must stay atomic-only: never acquire policy/Host/source locks.
static bool msiForceGamepadCurrent(uint64_t power,uint64_t revision,uint64_t sequence=0) {
    return g_msiForceGamepadEnabled.load(std::memory_order_acquire)&&
        g_msiForceGamepadRevision.load(std::memory_order_acquire)==revision&&
        (!sequence||g_msiForceGamepadSequence.load(std::memory_order_acquire)==sequence)&&
        currentPowerGeneration()==power&&hardwareWriteAllowed()&&
        !g_exitRequested.load(std::memory_order_acquire)&&!inputHostExitFenceRaised();
}
// Both BUS and UI enter through the existing single physical-read lock.
// Healthy idle cost: one atomic read and one pure button-edge check, no I/O.
static void msiForceGamepadObserveShoulders(bool live,WORD buttons) {
    if(!g_msiForceGamepadEnabled.load(std::memory_order_acquire))return;
    static ymcc::msi::forceGamepad::ShoulderEdge edge;
    if(edge.observe(live,buttons))msiForceGamepadRequest("lb-rb",3000);
}
static json msiForceGamepadObserveMode() {
    MsiClawHidDiag diag;const auto handle=msiClawOpenModeCommand(diag);
    msiClawCloseModeCommand(handle);
    if(!diag.deviceFound||diag.interfacePath.empty())return {{"modeKnown",false},{"reason","command-device-unavailable"}};
    // Reuse the existing bounded 0x26 protocol query, including strict 0x10
    // response parsing and retained cancellation storage. One event, one query.
    return ymcc::msi::cause::queryMode(U2W(diag.interfacePath));
}
// Caller holds the original Host transaction lock on the lifecycle thread.
static bool msiForceGamepadApply(const std::function<bool()>& current,const char* reason) {
    if(!current())return false;
    const bool policy=msiForceGamepadPolicyApply(current);
    if(!current())return false;
    const bool ownedRoute=g_msiDinputRouteDinputArmed.load()||g_msiDinputSourceValid.load()||
        g_msiDinputReturnOwed.load()||!msiDinputDeviceTargetAbsent();
    const bool virtualBusy=g_inputHostProcess||g_inputHostPrepared||g_inputHostPrepareMayHaveReachedHost;
    // A healthy mode2 source belongs to the existing virtual lifecycle. A
    // summon is not permission to tear it down or allocate another mode retry.
    if(ownedRoute||virtualBusy||g_realStickTest.virtualTargetEnabled) {
        if(!g_msiDinputSourceValid.load()&&!g_msiDinputRouteConfirmed.load()&&
           g_msiDinputRouteDinputArmed.load()&&g_msiDinputReturnOwed.load()&&
           g_realStickTest.enabled&&g_realStickTest.virtualTargetEnabled) {
            std::unique_lock<std::mutex> pump(g_msiPersonaPumpMx,std::try_to_lock);
            if(pump.owns_lock()&&current()) {
                const auto power=currentPowerGeneration(),epoch=inputHostEpochToken();
                const auto intent=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
                const auto observed=msiForceGamepadObserveMode();
                const auto mode=observed.value("observedMode",0);
                auto rearmCurrent=[&]{return current()&&currentPowerGeneration()==power&&inputHostEpochToken()==epoch&&
                    g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)==intent&&
                    !g_inputHostVirtualIntentWriters.load()&&g_realStickTest.enabled&&g_realStickTest.virtualTargetEnabled&&
                    g_msiDinputRouteDinputArmed.load()&&g_msiDinputReturnOwed.load()&&
                    !g_msiDinputRouteConfirmed.load()&&!g_msiDinputSourceValid.load();};
                if(observed.value("modeKnown",false)&&(mode==1||mode==4)&&rearmCurrent())
                    (void)msiClawRequestDInputMode("force-gamepad-observed-mode",nullptr,rearmCurrent);
            }
        }
        appendNativeLifecycleLog("msi-claw-gamepad-mode",{{"action","force-gamepad-preserve-capture"},
            {"reason",reason},{"mode",2},{"sent",false},{"writeAttempted",false},
            {"policyApplied",policy},{"deviceModeVerified",false},{"powerGeneration",currentPowerGeneration()}});
        return true;
    }
    const auto power=currentPowerGeneration();const auto host=inputHostEpochToken();
    // These original-owner checks run before entering the HID slot. The held
    // Host/pump locks then exclude new targets and physical source bindings.
    std::unique_lock<std::mutex> pump(g_msiPersonaPumpMx,std::try_to_lock);
    if(!pump.owns_lock()||!msiDinputDeviceTargetAbsent()||!msiForceGamepadNativeDebtClear())return false;
    auto allowed=[&] {
        return current()&&hardwareWriteAllowed()&&currentPowerGeneration()==power&&inputHostEpochToken()==host&&
            !g_inputHostReleaseQuarantined&&!g_inputHostProcess&&!g_inputHostPrepared&&
            !g_inputHostPrepareMayHaveReachedHost&&!g_msiDinputRouteDinputArmed.load()&&
            !g_msiDinputSourceValid.load()&&!g_msiDinputReturnOwed.load()&&
            !g_msiNativeForegroundCyclePending.load()&&!g_msiNativeForegroundPowerHold.load()&&
            !msiForegroundIsolationBorrowBusy();
    };
    if(!allowed())return false;
    const auto observed=msiForceGamepadObserveMode();
    const bool known=observed.value("modeKnown",false);
    const int mode=observed.value("observedMode",0);
    appendNativeLifecycleLog("msi-claw-gamepad-mode",{{"action","force-gamepad-check"},
        {"reason",reason},{"modeKnown",known},{"observedMode",mode},{"writeAttempted",false},
        {"deviceModeVerified",false},{"powerGeneration",power}});
    if(!allowed()||observed.value("ioRetained",false))return false;
    if(known&&mode==1)return true;
    // A firmware-combo check with no response must not guess or loop writes.
    if(!known&&std::strcmp(reason,"lb-rb-after-3s")==0)return false;
    MsiClawHidDiag diag;const HANDLE handle=msiClawOpenModeCommand(diag);
    const auto outcome=msiClawSwitchModeOnHandle(1,handle,diag,false,allowed);
    if(outcome.write.ok)msiNoteDisruptiveTx("force-gamepad-native");
    appendNativeLifecycleLog("msi-claw-gamepad-mode",{{"action","force-gamepad-native"},
        {"reason",reason},{"mode",1},{"sent",outcome.write.ok},{"writeAttempted",outcome.write.attempted},
        {"writeError",outcome.write.lastError},{"policyApplied",policy},{"deviceModeVerified",false},
        {"powerGeneration",power},{"hostEpoch",host}});
    return outcome.write.ok;
}
static void msiForceGamepadBeforeVirtualStart() {
    if(!msiForceGamepadEligible()||ymStartSeamActive()||!hardwareWriteAllowed())return;
    const auto power=currentPowerGeneration();
    const auto intent=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
    uint64_t revision=0;
    // Initial startup consumes the pending generic event here; a delayed LB/RB
    // event is retained and will preserve the newly established DInput source.
    {
        std::lock_guard lock(g_msiForceGamepadMx);
        revision=g_msiForceGamepadPlan.revision();
        ymcc::msi::forceGamepad::Request consumed;
        g_msiForceGamepadPlan.take(GetTickCount64(),power,true,consumed);
        g_msiForceGamepadPending.store(g_msiForceGamepadPlan.pending(),std::memory_order_release);
    }
    // The original Ensure immediately below this hook owns the one physical
    // transition to mode2. Never pulse mode1 here: its USB re-enumeration could
    // remove the command interface just before that existing mode2 write.
    const bool applied=msiForceGamepadPolicyApply([=]{
        return msiForceGamepadCurrent(power,revision)&&
            g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)==intent;
    });
    appendNativeLifecycleLog("msi-claw-gamepad-mode",{{"action","force-gamepad-before-virtual"},
        {"reason","policy-before-existing-dinput-owner"},{"policyApplied",applied},
        {"mode",2},{"writeAttempted",false},{"deviceModeVerified",false},
        {"powerGeneration",power}});
}
static void msiForceGamepadMaintenance() {
    if(!g_msiForceGamepadPending.load(std::memory_order_acquire)&&
       !g_msiForceGamepadRestorePending.load(std::memory_order_acquire))return;
    std::unique_lock<InputHostMutex> host(g_inputHostMx,std::try_to_lock);
    if(!host.owns_lock())return;
    if(g_msiForceGamepadRestorePending.exchange(false,std::memory_order_acq_rel)) {
        (void)msiForceGamepadPolicyRestore([]{return !oemVendorStackShieldIsWanted();});
    }
    if(!g_msiForceGamepadPending.load(std::memory_order_acquire))return;
    ymcc::msi::forceGamepad::Request request;
    {
        std::lock_guard lock(g_msiForceGamepadMx);
        const bool taken=g_msiForceGamepadPlan.take(GetTickCount64(),currentPowerGeneration(),
            hardwareWriteAllowed()&&!g_inputHostVirtualIntentWriters.load(std::memory_order_acquire),request);
        g_msiForceGamepadPending.store(g_msiForceGamepadPlan.pending(),std::memory_order_release);
        if(!taken)return;
    }
    auto current=[&] {
        return msiForceGamepadCurrent(request.power,request.revision,request.sequence);
    };
    (void)msiForceGamepadApply(current,request.trigger==ymcc::msi::forceGamepad::Trigger::Shoulders?"lb-rb-after-3s":
        request.trigger==ymcc::msi::forceGamepad::Trigger::WindowOpen?"window-open":
        request.trigger==ymcc::msi::forceGamepad::Trigger::Wake?"wake":"startup");
}
static void msiForceGamepadExitRestore() {
    {
        std::lock_guard lock(g_msiForceGamepadMx);
        g_msiForceGamepadPlan.setEnabled(false);
        g_msiForceGamepadRevision.store(g_msiForceGamepadPlan.revision(),std::memory_order_release);
        g_msiForceGamepadEnabled.store(false,std::memory_order_release);
        g_msiForceGamepadPending.store(false,std::memory_order_release);
        g_msiForceGamepadRestorePending.store(false,std::memory_order_release);
    }
    if(msiClawKnownBoardAuthority()&&!g_aiFanMockSession.isolated)
        (void)msiForceGamepadPolicyRestore([]{return !oemVendorStackShieldIsWanted();});
}
