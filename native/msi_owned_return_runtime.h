#include "virtual_output_status.h"
// Included after the MSI durable ownership helpers. All work uses the existing
// serialized lifecycle owner. The BUS only contributes its existing cached read.
static std::mutex g_msiOwnedReturnMx;
static ymcc::msi::ownedReturn::Recovery g_msiOwnedReturnRecovery;
static msiRecovery::Record g_msiOwnedReturnExpected;
static bool g_msiOwnedReturnRetrySafe=false;
// After durable mode return completes, only exact OFF handoff evidence may
// need refreshing. This never reopens mode debt or its consumed write budget.
static std::atomic<bool> g_msiOwnedReturnOffHandoffPending{false};
struct MsiOwnedReturnOffRequalification {
    ymcc::msi::ownedReturnOff::Proof context;
    ULONGLONG after=0,stableAt=0;
    unsigned long long firstSequence=0;
    unsigned observations=0;
    bool bound=false;
};
static MsiOwnedReturnOffRequalification g_msiOwnedReturnOffRequalification;
static bool g_msiOwnedReturnInterruptedByMode2=false,g_msiOwnedReturnFailedMode2Observed=false;
static std::string g_msiOwnedReturnLastNote;
// ON route abandonment may consume the terminal gate without finding a command
// interface. Keep its actual outcome for a later explicit OFF; never refund the
// terminal gate or start another owner while the target is still enabled.
struct MsiOwnedReturnTerminalReceipt {
    unsigned long long episode=0;
    msiRecovery::Record expected;
    MsiClawCommandOutcome outcome;
    bool usable=false,retrySafe=false;
};
static MsiOwnedReturnTerminalReceipt g_msiOwnedReturnTerminalReceipt;
static bool msiOwnedReturnSameReceiptOwner(const msiRecovery::Record& a,const msiRecovery::Record& b) {
    return a.boardKey==b.boardKey && a.rootInstance==b.rootInstance && a.locationPaths==b.locationPaths &&
        a.ownerPid==b.ownerPid && a.ownerCreationTime==b.ownerCreationTime && a.transactionId==b.transactionId;
}
static void msiOwnedReturnRetainOnTerminal(const MsiClawCommandOutcome& out) {
    if(!g_msiDinputReturnOwed.load(std::memory_order_acquire) ||
       !g_msiMode1TerminalAttempted.load(std::memory_order_acquire))return;
    std::lock_guard<std::mutex> state(g_msiOwnedReturnMx);
    const auto episode=g_msiDinputReturnDebtEpisode.load(std::memory_order_acquire);
    msiRecovery::Record record;
    {std::lock_guard<std::mutex> durable(g_msiCrashMx);if(g_msiCrashStoreInvalid)return;record=g_msiCrashRecord;}
    if(!episode || !msiRecovery::validRecord(record) || !record.modeReturnPending ||
       record.boardKey!=msiCrashBoardKey() || record.ownerPid!=GetCurrentProcessId() ||
       record.ownerCreationTime!=msiCrashProcessCreation(GetCurrentProcess()))return;
    // This handoff grants only the proven missing-transport case from the field.
    // Unknown/attempted/partial/completed/retained I/O can still be observed, but
    // cannot gain a write merely because this later OFF has no current outcome.
    const bool retrySafe=msiTerminalReturnMissingTransport(out);
    auto& saved=g_msiOwnedReturnTerminalReceipt;
    if(saved.episode!=episode)saved={episode,record,out,true,retrySafe};
    else {
        saved.usable=saved.usable && msiOwnedReturnSameReceiptOwner(saved.expected,record);
        saved.retrySafe=saved.retrySafe && retrySafe;saved.outcome=out;
    }
    appendNativeLifecycleLog("msi-owned-return-recovery",{{"action","on-terminal-outcome-retained"},
        {"reason","dinput-route-window-exhausted"},{"returnDebtEpisodeId",episode},
        {"writeAttempted",out.write.attempted},{"ioState",msiClawIoStateName(out.write.state)},
        {"offHandoffRetryEligible",saved.usable && saved.retrySafe},
        {"physicalSignalVerified",false},{"modeWriteIssued",false}});
}
static std::atomic<ULONGLONG> g_msiOwnedReturnObserveAfter{0};
static std::atomic<unsigned> g_msiOwnedReturnSeenTopology{0};
static std::atomic<unsigned long long> g_msiOwnedReturnSeenPower{0};
static std::atomic<unsigned long long> g_msiOwnedReturnSeenSourceEvent{0};

static void msiOwnedReturnPublish() {
    g_msiOwnedReturnPending.store(g_msiOwnedReturnRecovery.pending(),std::memory_order_release);
    g_msiOwnedReturnWorking.store(g_msiOwnedReturnRecovery.working(),std::memory_order_release);
    g_msiOwnedReturnFailed.store(g_msiOwnedReturnRecovery.failed(),std::memory_order_release);
}
static void msiOwnedReturnNote(const char* action,const char* reason,const MsiClawCommandOutcome* out=nullptr) {
    const std::string key=std::string(action)+"|"+reason;
    if(key==g_msiOwnedReturnLastNote)return;
    g_msiOwnedReturnLastNote=key;
    json row={{"action",action},{"reason",reason},{"expectedInput","physical-XInput-PID1901"},
        {"expectedRoot",g_msiOwnedReturnExpected.rootInstance},{"expectedLocations",g_msiOwnedReturnExpected.locationPaths},
        {"returnDebtEpisodeId",g_msiOwnedReturnRecovery.episode()},
        {"pending",g_msiOwnedReturnRecovery.pending()},{"working",g_msiOwnedReturnRecovery.working()},
        {"failed",g_msiOwnedReturnRecovery.failed()},{"retryUsed",g_msiOwnedReturnRecovery.retryUsed()},
        {"observationCount",g_msiOwnedReturnRecovery.observations()},
        {"modeReturnOwed",g_msiDinputReturnOwed.load()},{"physicalSignalVerified",false},
        {"steamConsumptionVerified",false},{"scope","same-owned-board-root-location;existing-read-window;no-Steam-poll"}};
    if(out){row["sent"]=out->write.ok;row["writeAttempted"]=out->write.attempted;
        row["writeError"]=out->write.lastError;row["written"]=out->write.written;
        row["ioState"]=msiClawIoStateName(out->write.state);row["commandProductId"]=out->diag.productId;}
    appendNativeLifecycleLog("msi-owned-return-recovery",row);
}
static bool msiOwnedReturnRetryable(const MsiClawCommandOutcome& out) {
    if(out.write.handleRetained || out.write.timedOut)return false;
    if(out.write.ok)return true; // a retry still needs a live matching PID1902, never just absent input
    if(!out.write.attempted)return out.write.state==MsiClawIoState::NotAttempted ||
        out.write.state==MsiClawIoState::NoDevice || out.write.state==MsiClawIoState::SlotBusyRetired ||
        (out.write.state==MsiClawIoState::StartFailed && out.write.lastError==ERROR_WRITE_FAULT);
    return out.write.written==0 && out.write.state==MsiClawIoState::StartFailed &&
        (out.write.lastError==ERROR_NO_SUCH_DEVICE || out.write.lastError==ERROR_DEVICE_NOT_CONNECTED ||
         out.write.lastError==ERROR_INVALID_HANDLE);
}
static void msiOwnedReturnRecordInitial(const char* reason,const MsiClawCommandOutcome& out) {
    if(msiClawKnownBoardAuthority() && !ymStartSeamActive() && reason &&
       std::strcmp(reason,"dinput-route-window-exhausted")==0) {
        msiOwnedReturnRetainOnTerminal(out);return;
    }
    if(!msiClawKnownBoardAuthority() || ymStartSeamActive() || !reason ||
       (std::strcmp(reason,"input-host-stop") && std::strcmp(reason,"input-host-stop-deferred-transport")) ||
       g_realStickTest.virtualTargetEnabled || inputHostConfiguredPersona()!="disabled" ||
       !g_msiDinputReturnOwed.load(std::memory_order_acquire))return;
    std::lock_guard<std::mutex> state(g_msiOwnedReturnMx);
    msiRecovery::Record record;
    {std::lock_guard<std::mutex> durable(g_msiCrashMx);record=g_msiCrashRecord;}
    if(!msiRecovery::validRecord(record) || !record.modeReturnPending || record.boardKey!=msiCrashBoardKey() ||
       record.ownerPid!=GetCurrentProcessId() || record.ownerCreationTime!=msiCrashProcessCreation(GetCurrentProcess()))return;
    const auto episode=g_msiDinputReturnDebtEpisode.load();
    const auto& terminal=g_msiOwnedReturnTerminalReceipt;
    const bool priorTerminal=episode && terminal.episode==episode && g_msiMode1TerminalAttempted.load();
    if(priorTerminal && (!terminal.usable || !msiOwnedReturnSameReceiptOwner(terminal.expected,record)))return;
    // A rebound DInput source can enter the no-current-write deferred branch
    // before OFF intent runs. Its default NotAttempted is not the older result;
    // preserve that original outcome's stricter permission through this path too.
    const bool retrySafe=msiOwnedReturnRetryable(out) && (!priorTerminal || terminal.retrySafe);
    if(!g_msiOwnedReturnRecovery.begin(GetTickCount64(),episode,retrySafe)) {
        // A deferred no-write return can be followed by the first REAL outcome
        // of this same duty. It may reduce permission, never renew the window
        // or upgrade/reset the single retry. Retire any newly scheduled legacy
        // plan so only this observer can reconcile the existing obligation.
        if(episode && episode==g_msiOwnedReturnRecovery.episode() && g_msiOwnedReturnRecovery.pending() &&
           record.boardKey==g_msiOwnedReturnExpected.boardKey && record.rootInstance==g_msiOwnedReturnExpected.rootInstance &&
           record.locationPaths==g_msiOwnedReturnExpected.locationPaths &&
           record.ownerPid==g_msiOwnedReturnExpected.ownerPid && record.ownerCreationTime==g_msiOwnedReturnExpected.ownerCreationTime) {
            g_msiOwnedReturnRetrySafe=g_msiOwnedReturnRetrySafe && retrySafe;
            if(g_msiTerminalReturnPending.load(std::memory_order_acquire))
                msiTerminalReturnCancel("owned-return-outcome-refresh");
            msiOwnedReturnNote("outcome-refreshed","same-duty-retry-permission-can-only-decrease",&out);
        }
        return;
    }
    g_msiOwnedReturnExpected=record;g_msiOwnedReturnRetrySafe=retrySafe;g_msiOwnedReturnLastNote.clear();
    g_msiOwnedReturnMatchedEpisode.store(0,std::memory_order_release);
    g_msiOwnedReturnObserveAfter.store(0,std::memory_order_release);
    msiTerminalReturnCancel("owned-return-reconciliation"); // one return owner, never two retry plans
    msiOwnedReturnPublish();msiOwnedReturnNote("tracking","initial-return-awaits-matching-physical-signal",&out);
}
static void msiOwnedReturnCancel(const char* reason) {
    std::lock_guard<std::mutex> state(g_msiOwnedReturnMx);
    const bool mode2=reason && std::strcmp(reason,"new-dinput-request")==0;
    if(!mode2){g_msiOwnedReturnInterruptedByMode2=false;g_msiOwnedReturnFailedMode2Observed=false;}
    if(!g_msiOwnedReturnRecovery.pending()){
        // The settings lane can cancel first; the real mode2 caller still
        // identifies this cancellation as an attempted takeover of SAME debt.
        if(mode2 && g_msiOwnedReturnRecovery.cancelled() && g_msiDinputReturnOwed.load() &&
           g_msiOwnedReturnRecovery.episode()==g_msiDinputReturnDebtEpisode.load()){
            g_msiOwnedReturnInterruptedByMode2=true;g_msiOwnedReturnFailedMode2Observed=false;
        }
        return;
    }
    g_msiOwnedReturnInterruptedByMode2=mode2;g_msiOwnedReturnFailedMode2Observed=false;
    g_msiOwnedReturnRecovery.cancel();msiOwnedReturnPublish();
    g_msiOwnedReturnMatchedEpisode.store(0,std::memory_order_release);
    msiOwnedReturnNote("cancelled",reason?reason:"explicit-cancel");
}
// Called by the real mode2 caller before it advances the physical debt episode.
// A failure can preserve the cancelled obligation; it cannot refund a retry.
static void msiOwnedReturnMode2Outcome(const MsiClawCommandOutcome& outcome) {
    std::lock_guard<std::mutex> state(g_msiOwnedReturnMx);
    if(!g_msiOwnedReturnInterruptedByMode2 ||
       g_msiDinputReturnDebtEpisode.load()!=g_msiOwnedReturnRecovery.episode())return;
    g_msiOwnedReturnFailedMode2Observed=!outcome.write.ok;
    if(outcome.write.ok){g_msiOwnedReturnInterruptedByMode2=false;return;}
    g_msiOwnedReturnRetrySafe=g_msiOwnedReturnRetrySafe && msiOwnedReturnRetryable(outcome);
    msiOwnedReturnNote("duty-retained","new-mode2-failed-old-physical-return-still-owned",&outcome);
}
// Runs only from the existing terminal return gate. It observes a NEW OFF,
// not an automatic begin(oldEpisode); the same durable device is mandatory.
static void msiOwnedReturnResumeAfterFailedMode2(const char* reason) {
    if(!reason || std::strcmp(reason,"input-host-stop") || !msiClawKnownBoardAuthority() ||
       ymStartSeamActive() || inputHostExitFenceRaised() || g_realStickTest.virtualTargetEnabled ||
       inputHostConfiguredPersona()!="disabled" || !g_msiDinputReturnOwed.load())return;
    std::lock_guard<std::mutex> state(g_msiOwnedReturnMx);
    if(!g_msiOwnedReturnInterruptedByMode2 || !g_msiOwnedReturnFailedMode2Observed)return;
    msiRecovery::Record record;
    {std::lock_guard<std::mutex> durable(g_msiCrashMx);record=g_msiCrashRecord;}
    if(!msiRecovery::validRecord(record) || !record.modeReturnPending || record.boardKey!=msiCrashBoardKey() ||
       record.ownerPid!=GetCurrentProcessId() || record.ownerCreationTime!=msiCrashProcessCreation(GetCurrentProcess()) ||
       record.boardKey!=g_msiOwnedReturnExpected.boardKey ||
       !msiRecovery::sameDeviceRoot(g_msiOwnedReturnExpected,record.rootInstance,record.locationPaths))return;
    if(!g_msiOwnedReturnRecovery.resumeCancelled(GetTickCount64(),g_msiDinputReturnDebtEpisode.load(),g_msiOwnedReturnRetrySafe))return;
    g_msiOwnedReturnExpected=record;g_msiOwnedReturnInterruptedByMode2=false;g_msiOwnedReturnFailedMode2Observed=false;
    g_msiOwnedReturnObserveAfter.store(0,std::memory_order_release);g_msiOwnedReturnLastNote.clear();
    msiTerminalReturnCancel("resumed-owned-physical-duty");
    msiOwnedReturnPublish();msiOwnedReturnNote("observation-resumed","explicit-OFF-after-failed-mode2-same-debt-no-refunded-retry");
}
// Existing OFF intent owns the handoff after canonical Stop (which can have
// returned at the already-consumed terminal gate). The saved outcome is evidence
// only for this exact durable transaction and episode. begin cannot renew an old
// observer or its consumed retry; normal maintenance retains every write gate.
static void msiOwnedReturnAdoptOnTerminal() {
    if(!g_msiMode1TerminalAttempted.load(std::memory_order_acquire) ||
       !g_msiDinputReturnOwed.load(std::memory_order_acquire) ||
       g_realStickTest.virtualTargetEnabled || inputHostConfiguredPersona()!="disabled")return;
    std::lock_guard<std::mutex> state(g_msiOwnedReturnMx);
    const auto episode=g_msiDinputReturnDebtEpisode.load(std::memory_order_acquire);
    const auto& saved=g_msiOwnedReturnTerminalReceipt;
    if(!episode || saved.episode!=episode || !saved.usable)return;
    msiRecovery::Record record;
    {std::lock_guard<std::mutex> durable(g_msiCrashMx);if(g_msiCrashStoreInvalid)return;record=g_msiCrashRecord;}
    if(!msiRecovery::validRecord(record) || !record.modeReturnPending ||
       record.boardKey!=msiCrashBoardKey() || record.ownerPid!=GetCurrentProcessId() ||
       record.ownerCreationTime!=msiCrashProcessCreation(GetCurrentProcess()) ||
       !msiOwnedReturnSameReceiptOwner(saved.expected,record) ||
       !g_msiOwnedReturnRecovery.begin(GetTickCount64(),episode,saved.retrySafe))return;
    g_msiOwnedReturnExpected=record;g_msiOwnedReturnRetrySafe=saved.retrySafe;
    g_msiOwnedReturnLastNote.clear();g_msiOwnedReturnMatchedEpisode.store(0,std::memory_order_release);
    g_msiOwnedReturnObserveAfter.store(0,std::memory_order_release);
    msiTerminalReturnCancel("explicit-OFF-adopts-ON-terminal-duty");
    msiOwnedReturnPublish();msiOwnedReturnNote("tracking","explicit-OFF-adopts-same-ON-terminal-outcome",&saved.outcome);
}
// Explicit OFF is processed even when a failed new mode2 never created a
// Host/journal and canonical Stop therefore has nothing local left to release.
static void msiOwnedReturnObserveOffIntent() {
    if(!msiClawKnownBoardAuthority())return;
    std::lock_guard<InputHostMutex> host(g_inputHostMx);
    std::lock_guard<std::mutex> pump(g_msiPersonaPumpMx);
    msiOwnedReturnResumeAfterFailedMode2("input-host-stop");
    msiOwnedReturnAdoptOnTerminal();
}
// No mutex acquisition; caller already owns Host/pump/state. OFF settings are
// checked separately and bound by the captured intent revision at settlement.
static bool msiOwnedReturnNonblockingContext(unsigned long long power,unsigned long long mode,unsigned long long episode) {
    return msiClawKnownBoardAuthority() && hardwareWriteAllowed() && !inputHostExitFenceRaised() &&
        !g_msiNativeForegroundCyclePending.load() && !g_msiNativeForegroundPowerHold.load() &&
        !msiForegroundIsolationBorrowBusy() &&
        !g_inputHostProcess && (!g_inputHostPrepared || g_inputHostReleaseQuarantined) &&
        !g_inputHostActive && !g_msiDinputRouteDinputArmed.load() &&
        g_msiDinputReturnOwed.load() && currentPowerGeneration()==power &&
        g_msiFeedbackModeGeneration.load()==mode && g_msiDinputReturnDebtEpisode.load()==episode;
}
static bool msiOwnedReturnContext(unsigned long long power,unsigned long long mode,unsigned long long episode) {
    return !g_realStickTest.virtualTargetEnabled && inputHostConfiguredPersona()=="disabled" &&
        msiOwnedReturnNonblockingContext(power,mode,episode);
}
// Caller holds g_msiCrashMx; used by the final durable settlement validator.
static bool msiOwnedReturnRecordCurrentLocked(bool expectModePending=true) {
    const auto& now=g_msiCrashRecord;const auto& expected=g_msiOwnedReturnExpected;
    return !g_msiCrashStoreInvalid && now.modeReturnPending==expectModePending &&
        now.boardKey==expected.boardKey && now.rootInstance==expected.rootInstance &&
        now.locationPaths==expected.locationPaths && now.ownerPid==expected.ownerPid &&
        now.ownerCreationTime==expected.ownerCreationTime;
}
static bool msiOwnedReturnRecordCurrent(bool expectModePending=true) {
    std::lock_guard<std::mutex> durable(g_msiCrashMx);
    return msiOwnedReturnRecordCurrentLocked(expectModePending);
}
// Host, source-pump and owned-return locks are held by the existing worker.
// The normal MSI reader remains enabled in virtual-OFF and may have rebound
// after Stop released it. Only THIS owned, freshly resolved PID1902 source can
// be quiesced for the already eligible single return retry; other sources stay.
static bool msiOwnedReturnQuiesceReboundDinput(unsigned long long power,unsigned long long mode,
        unsigned topology,unsigned long long episode,unsigned long long sourceEvent) {
    if(msiDinputDeviceTargetAbsent())return true;
    HidHideMsiMainSourceRecord bound;
    if(!msiDinputMainSourceRecordSnapshot(bound) || !bound.valid || !bound.generation ||
       bound.vendorId!=0x0db0 || bound.productId!=0x1902 || bound.interfacePath.empty() || bound.usbInstance.empty())return false;
    std::wstring root;
    if(!deviceInterfaceUsbInstance(U2W(bound.interfacePath).c_str(),root) ||
       hidHideUpper(W2U(root))!=hidHideUpper(bound.usbInstance) ||
       !msiRecovery::sameDeviceRoot(g_msiOwnedReturnExpected,W2U(root),msiCrashLocations(W2U(root))))return false;
    HidHideMsiMainSourceRecord current;
    if(!msiOwnedReturnContext(power,mode,episode) || g_xinputTopologyEpoch.load()!=topology ||
       g_msiOwnedReturnSourceEvent.load()!=sourceEvent || !msiOwnedReturnRecordCurrent() ||
       !msiDinputMainSourceRecordSnapshot(current) || current.generation!=bound.generation ||
       current.interfacePath!=bound.interfacePath || current.usbInstance!=bound.usbInstance ||
       current.vendorId!=bound.vendorId || current.productId!=bound.productId)return false;
    msiDinputReleasePad("owned-return-retry-quiesce");
    msiDinputClearOwnedState("owned-return-retry-quiesce");
    const bool released=msiOwnedReturnContext(power,mode,episode) && g_xinputTopologyEpoch.load()==topology &&
        g_msiOwnedReturnSourceEvent.load()==sourceEvent && msiOwnedReturnRecordCurrent() && msiDinputDeviceTargetAbsent();
    msiOwnedReturnNote(released?"source-quiesced":"waiting",released?
        "same-owned-rebound-DInput-released-before-single-retry":"owned-source-release-context-unproven");
    return released;
}
struct MsiOwnedReturnSignal {
    bool matched=false;unsigned mask=0;unsigned long long sequence=0;
    ULONGLONG readStartedTick=0,readCompletedTick=0;unsigned readIdentity=0;
};
static bool msiOwnedReturnSignalCurrent(const MsiOwnedReturnSignal& signal,
        unsigned long long power,unsigned topology,ULONGLONG now) {
    return signal.matched && signal.readIdentity==g_xinputSlotIdentityEpoch.load(std::memory_order_acquire) &&
        ymcc::virtualOutputSourceCurrent({signal.sequence,signal.readStartedTick,signal.readCompletedTick,
            power,topology,true},now,power,topology);
}
static MsiOwnedReturnSignal msiOwnedReturnReadSignal(unsigned long long power,unsigned topology,ULONGLONG now) {
    MsiOwnedReturnSignal signal;
    std::array<std::wstring,4> paths;unsigned mask=0;
    decltype(g_xinputSlotIdentityEpoch.load(std::memory_order_acquire)) identity=0;
    InputCaptureGamepadSnapshot sample;
    {
        std::unique_lock<std::mutex> read(g_gamepadReadMx,std::try_to_lock);
        if(!read.owns_lock())return signal;
        identity=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire);
        mask=xinputPhysicalConnectedMask();
        for(unsigned slot=0;slot<4;++slot)if(mask&(1u<<slot)){
            const auto proof=g_xinputFeedbackIdentity[slot].capture(identity,power);
            if(proof.valid && !proof.legacyFallback)paths[slot]=g_msiOwnedReturnSlotPaths[slot];
        }
        std::lock_guard<std::mutex> snapshot(g_inputCaptureGamepadSnapshotMx);sample=g_inputCaptureGamepadSnapshot;
    }
    const auto sampleCurrent=[&](ULONGLONG at) {
        return sample.live && sample.readIdentityGeneration==identity &&
            identity==g_xinputSlotIdentityEpoch.load(std::memory_order_acquire) &&
            currentPowerGeneration()==power && g_xinputTopologyEpoch.load(std::memory_order_acquire)==topology &&
            ymcc::virtualOutputSourceCurrent({sample.readAttemptSequence,sample.readStartedTick,sample.readCompletedTick,
                sample.readPowerGeneration,sample.readTopologyGeneration,sample.readContextStable},at,power,topology) &&
            (sample.inputSource=="xinput" || sample.inputSource=="xinput-any-connected");
    };
    if(!sampleCurrent(now))return signal;
    unsigned matches=0;
    for(unsigned slot=0;slot<4;++slot)if((mask&sample.connectedMask&(1u<<slot)) && !paths[slot].empty()){
        std::wstring root;
        if(!deviceInterfaceUsbInstance(paths[slot].c_str(),root))continue;
        const auto usb=W2U(root);
        if(hidHideUpper(usb).find("USB\\VID_0DB0&PID_1901\\")!=0)continue;
        if(msiRecovery::sameDeviceRoot(g_msiOwnedReturnExpected,usb,msiCrashLocations(usb))){
            ++matches;signal.mask=1u<<slot;
        }
    }
    // Parent-root/location queries may block. A cached match is not authority
    // to discharge debt after the read window or identity has retired.
    if(!sampleCurrent(GetTickCount64()))return {};
    signal.matched=matches==1;signal.sequence=sample.readAttemptSequence;
    signal.readStartedTick=sample.readStartedTick;signal.readCompletedTick=sample.readCompletedTick;
    signal.readIdentity=sample.readIdentityGeneration;
    return signal;
}
// Called only at the existing Host-owned return/absence checks. This does
// not admit an XInput source or replace any independent SD/PS PnP duty.
static bool msiOwnedReturnOffEndpointAbsent() {
    using namespace ymcc::msi::ownedReturnOff;
    Proof proof;
    if(!msiClawKnownBoardAuthority()||!g_msiOwnedReturnOffEndpoint.snapshot(proof))return false;
    const auto contextCurrent=[&] {
        return proof.episode && proof.readSequence && hardwareWriteAllowed() && !inputHostExitFenceRaised() &&
            !g_realStickTest.virtualTargetEnabled && inputHostConfiguredPersona()=="disabled" &&
            !g_inputHostProcess && !g_inputHostActive && (!g_inputHostPrepared||g_inputHostReleaseQuarantined) &&
            !g_msiDinputRouteDinputArmed.load(std::memory_order_acquire) &&
            !g_msiDinputReturnOwed.load(std::memory_order_acquire) &&
            !g_msiNativeForegroundCyclePending.load() && !g_msiNativeForegroundPowerHold.load() &&
            !msiForegroundIsolationBorrowBusy() &&
            g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)==0 &&
            g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)==proof.intent &&
            inputHostEpochToken()==proof.host && currentPowerGeneration()==proof.power &&
            g_msiFeedbackModeGeneration.load(std::memory_order_acquire)==proof.mode &&
            g_xinputTopologyEpoch.load(std::memory_order_acquire)==proof.topology &&
            g_xinputSlotIdentityEpoch.load(std::memory_order_acquire)==proof.identity &&
            g_msiOwnedReturnSourceEvent.load(std::memory_order_acquire)==proof.sourceEvent &&
            g_msiDinputReturnDebtEpisode.load(std::memory_order_acquire)==proof.episode &&
            g_msiOwnedReturnMatchedEpisode.load(std::memory_order_acquire)==proof.episode &&
            g_msiOwnedReturnMatchedPower.load(std::memory_order_acquire)==proof.power &&
            g_msiOwnedReturnMatchedTopology.load(std::memory_order_acquire)==proof.topology;
    };
    if(!contextCurrent())return false;
    InputCaptureGamepadSnapshot sample;
    unsigned rawConnectedMask=0;
    {std::unique_lock<std::mutex> read(g_gamepadReadMx,std::try_to_lock);
        if(!read.owns_lock())return false;
        std::unique_lock<std::mutex> snapshot(g_inputCaptureGamepadSnapshotMx,std::try_to_lock);
        if(!snapshot.owns_lock())return false;
        rawConnectedMask=g_xinputConnectedMask;sample=g_inputCaptureGamepadSnapshot;}
    const auto now=GetTickCount64();
    const bool freshSameSource=exactSinglePhysicalMask(proof.mask,rawConnectedMask) && sample.live && sample.readIdentityGeneration==proof.identity &&
        sample.readAttemptSequence>=proof.readSequence && (sample.connectedMask&proof.mask)==proof.mask &&
        (sample.inputSource=="xinput"||sample.inputSource=="xinput-any-connected") &&
        ymcc::virtualOutputSourceCurrent({sample.readAttemptSequence,sample.readStartedTick,sample.readCompletedTick,
            sample.readPowerGeneration,sample.readTopologyGeneration,sample.readContextStable},now,proof.power,proof.topology);
    return freshSameSource && contextCurrent();
}
// Existing lifecycle worker only. A settings revision may retire an otherwise
// valid OFF proof after the mode debt settled; re-read the same owned device
// through two new windows instead of copying old tokens or issuing mode writes.
static void msiOwnedReturnOffHandoffMaintenance(ULONGLONG now) {
    using namespace ymcc::msi::ownedReturnOff;
    if(!g_msiOwnedReturnOffHandoffPending.load(std::memory_order_acquire))return;
    std::unique_lock<InputHostMutex> host(g_inputHostMx,std::try_to_lock);if(!host.owns_lock())return;
    std::unique_lock<std::mutex> pump(g_msiPersonaPumpMx,std::try_to_lock);if(!pump.owns_lock())return;
    std::unique_lock<std::mutex> state(g_msiOwnedReturnMx,std::try_to_lock);if(!state.owns_lock())return;
    if((!g_inputHostReleaseQuarantined && !g_hidHideVisibilityJournal.active) ||
       !g_msiOwnedReturnRecovery.completed() || g_realStickTest.virtualTargetEnabled ||
       inputHostConfiguredPersona()!="disabled" ||
       g_msiDinputReturnDebtEpisode.load()!=g_msiOwnedReturnRecovery.episode()) {
        g_msiOwnedReturnOffHandoffPending.store(false,std::memory_order_release);return;
    }
    Proof context{g_msiOwnedReturnRecovery.episode(),currentPowerGeneration(),g_msiFeedbackModeGeneration.load(),
        g_inputHostVirtualIntentRevision.load(std::memory_order_acquire),inputHostEpochToken(),
        g_msiOwnedReturnSourceEvent.load(std::memory_order_acquire),0,g_xinputTopologyEpoch.load(),
        g_xinputSlotIdentityEpoch.load(std::memory_order_acquire),0};
    const auto current=[&] {
        return msiClawKnownBoardAuthority() && hardwareWriteAllowed() && !inputHostExitFenceRaised() &&
            !g_realStickTest.virtualTargetEnabled && inputHostConfiguredPersona()=="disabled" &&
            !g_inputHostProcess && !g_inputHostActive && (!g_inputHostPrepared||g_inputHostReleaseQuarantined) &&
            !g_msiDinputRouteDinputArmed.load() && !g_msiDinputReturnOwed.load() &&
            !g_msiNativeForegroundCyclePending.load() && !g_msiNativeForegroundPowerHold.load() &&
            !msiForegroundIsolationBorrowBusy() && g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)==0 &&
            context.intent==g_inputHostVirtualIntentRevision.load(std::memory_order_acquire) &&
            context.host==inputHostEpochToken() && context.power==currentPowerGeneration() &&
            context.mode==g_msiFeedbackModeGeneration.load() && context.topology==g_xinputTopologyEpoch.load() &&
            context.identity==g_xinputSlotIdentityEpoch.load() && context.sourceEvent==g_msiOwnedReturnSourceEvent.load() &&
            context.episode==g_msiDinputReturnDebtEpisode.load();
    };
    if(!current())return;
    Proof published;
    if(g_msiOwnedReturnOffEndpoint.snapshot(published) && sameContext(published,context))return;
    auto& observer=g_msiOwnedReturnOffRequalification;
    if(!observer.bound || !sameContext(observer.context,context)) {
        observer.context=context;observer.bound=true;observer.firstSequence=0;observer.stableAt=0;observer.observations=0;
    }
    if(now<observer.after || observer.observations>=ymcc::msi::ownedReturn::Recovery::maxObservations)return;
    observer.after=now+ymcc::msi::ownedReturn::Recovery::observationIntervalMs;++observer.observations;
    const auto signal=msiOwnedReturnReadSignal(context.power,context.topology,GetTickCount64());
    if(!msiOwnedReturnRecordCurrent(false) || !current() || signal.readIdentity!=context.identity ||
       !msiOwnedReturnSignalCurrent(signal,context.power,context.topology,GetTickCount64())) {
        observer.firstSequence=0;observer.stableAt=0;return;
    }
    const auto at=GetTickCount64();
    if(!observer.firstSequence) {observer.firstSequence=signal.sequence;observer.stableAt=at;return;}
    if(signal.sequence==observer.firstSequence || at<observer.stableAt ||
       at-observer.stableAt<ymcc::msi::ownedReturn::Recovery::stableMatchMs)return;
    // The durable-record check may take time; never publish a retired intent.
    if(!current() || !msiOwnedReturnSignalCurrent(signal,context.power,context.topology,GetTickCount64())) {
        observer.firstSequence=0;observer.stableAt=0;return;
    }
    context.readSequence=signal.sequence;context.mask=signal.mask;
    g_msiOwnedReturnMatchedPower.store(context.power,std::memory_order_release);
    g_msiOwnedReturnMatchedTopology.store(context.topology,std::memory_order_release);
    g_msiOwnedReturnMatchedEpisode.store(context.episode,std::memory_order_release);
    g_msiOwnedReturnOffEndpoint.publish(context);
    appendNativeLifecycleLog("msi-owned-return-recovery",{{"action","off-handoff-proof-requalified"},
        {"returnDebtEpisodeId",context.episode},{"lastReadSequence",signal.sequence},{"matchedMask",signal.mask},
        {"intentRevision",context.intent},{"physicalSignalVerified",true},{"modeWriteIssued",false},
        {"steamConsumptionVerified",false},{"scope","same-owned-root-location-two-fresh-windows-OFF-handoff-only"}});
}
static void msiOwnedReturnMaintenance(ULONGLONG now) {
    msiOwnedReturnOffHandoffMaintenance(now);
    if(!g_msiOwnedReturnPending.load(std::memory_order_acquire))return;
    const bool sameEvent=g_msiOwnedReturnSeenTopology.load(std::memory_order_acquire)==g_xinputTopologyEpoch.load(std::memory_order_acquire) &&
        g_msiOwnedReturnSeenPower.load(std::memory_order_acquire)==currentPowerGeneration() &&
        g_msiOwnedReturnSeenSourceEvent.load(std::memory_order_acquire)==g_msiOwnedReturnSourceEvent.load(std::memory_order_acquire);
    if(sameEvent && (!g_msiOwnedReturnWorking.load(std::memory_order_acquire) ||
       now<g_msiOwnedReturnObserveAfter.load(std::memory_order_acquire)))return;
    std::unique_lock<InputHostMutex> host(g_inputHostMx,std::try_to_lock);if(!host.owns_lock())return;
    std::unique_lock<std::mutex> pump(g_msiPersonaPumpMx,std::try_to_lock);if(!pump.owns_lock())return;
    std::unique_lock<std::mutex> state(g_msiOwnedReturnMx,std::try_to_lock);if(!state.owns_lock())return;
    if(g_realStickTest.virtualTargetEnabled || inputHostConfiguredPersona()!="disabled" ||
       g_msiDinputReturnDebtEpisode.load()!=g_msiOwnedReturnRecovery.episode()){
        g_msiOwnedReturnRecovery.cancel();msiOwnedReturnPublish();msiOwnedReturnNote("cancelled","new-target-or-return-episode");return;
    }
    const auto power=currentPowerGeneration();const auto mode=g_msiFeedbackModeGeneration.load();
    const auto topology=g_xinputTopologyEpoch.load();const auto episode=g_msiOwnedReturnRecovery.episode();
    const auto hostEpoch=inputHostEpochToken();
    const auto intentRevision=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
    if(g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)!=0 ||
       !msiOwnedReturnContext(power,mode,episode))return; // keep duty during partial settings/Host/power changes
    const auto sourceEvent=g_msiOwnedReturnSourceEvent.load(std::memory_order_acquire);
    if(!g_msiOwnedReturnRecovery.due(now,topology,power,sourceEvent)){msiOwnedReturnPublish();return;}
    g_msiOwnedReturnSeenTopology.store(topology,std::memory_order_release);
    g_msiOwnedReturnSeenPower.store(power,std::memory_order_release);
    g_msiOwnedReturnSeenSourceEvent.store(sourceEvent,std::memory_order_release);
    g_msiOwnedReturnObserveAfter.store(now+ymcc::msi::ownedReturn::Recovery::observationIntervalMs,std::memory_order_release);
    msiOwnedReturnPublish();
    if(!msiOwnedReturnRecordCurrent()){
        g_msiOwnedReturnRecovery.miss();msiOwnedReturnNote("waiting","owned-record-missing-or-changed");return;
    }
    const auto signal=msiOwnedReturnReadSignal(power,topology,GetTickCount64());
    const auto settlementTokensCurrent=[&] {
        return msiOwnedReturnNonblockingContext(power,mode,episode) && g_xinputTopologyEpoch.load()==topology &&
            g_msiOwnedReturnSourceEvent.load()==sourceEvent && inputHostEpochToken()==hostEpoch &&
            g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)==0 &&
            g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)==intentRevision &&
            (!signal.matched || msiOwnedReturnSignalCurrent(signal,power,topology,GetTickCount64()));
    };
    const auto settlementCurrent=[&] {
        return msiOwnedReturnContext(power,mode,episode) && settlementTokensCurrent();
    };
    if(!msiOwnedReturnRecordCurrent() || !settlementCurrent()){
        g_msiOwnedReturnRecovery.miss();msiOwnedReturnNote("waiting","context-changed-during-signal-query");return;
    }
    if(signal.matched){
        if(g_msiOwnedReturnRecovery.observeMatch(GetTickCount64(),signal.sequence,false)){
            if(!settlementCurrent()) {g_msiOwnedReturnRecovery.miss();return;}
            // Runs only after the durable owner obtains its mutex, immediately
            // before store. Never acquire g_msiCrashMx again from this callback.
            // The captured, unchanged intent revision binds the OFF settings;
            // this callback never takes the persona, sample or source mutex.
            const auto durableCurrent=[&] {return msiOwnedReturnRecordCurrentLocked() && settlementTokensCurrent();};
            if(!msiDinputTryDischargeReturnDebt(false,true,signal.mask,"matched-owned-XInput-stable-read-windows",durableCurrent)) {
                msiOwnedReturnPublish();
                msiOwnedReturnNote("settlement-pending","physical-match-durable-settlement-not-committed");return;
            }
            (void)g_msiOwnedReturnRecovery.completeMatch();
            g_msiOwnedReturnOffHandoffPending.store(true,std::memory_order_release);
            g_msiOwnedReturnOffRequalification={};
            g_msiOwnedReturnMatchedPower.store(power,std::memory_order_release);
            g_msiOwnedReturnMatchedTopology.store(topology,std::memory_order_release);
            g_msiOwnedReturnMatchedEpisode.store(episode,std::memory_order_release);
            // Preserve the exact same-owned, two-fresh-window qualification for
            // the existing OFF quarantine owner. Never widen source admission.
            g_msiOwnedReturnOffEndpoint.publish({episode,power,mode,intentRevision,hostEpoch,sourceEvent,
                signal.sequence,topology,signal.readIdentity,signal.mask});
            msiOwnedReturnPublish();
            appendNativeLifecycleLog("msi-owned-return-recovery",{{"action","physical-signal-confirmed"},
                {"returnDebtEpisodeId",episode},{"expectedRoot",g_msiOwnedReturnExpected.rootInstance},
                {"matchedMask",signal.mask},{"lastReadSequence",signal.sequence},{"physicalSignalVerified",true},
                {"readStartedTick",signal.readStartedTick},{"readCompletedTick",signal.readCompletedTick},
                {"readIdentityGeneration",signal.readIdentity},{"readFreshnessPolicy","canonical-start-window-1500ms-query-context-rechecked"},
                {"steamConsumptionVerified",false},{"modeWriteIssued",false}});
        }else msiOwnedReturnNote("observing","matching-physical-signal-awaits-second-fresh-read-window");
        return;
    }
    g_msiOwnedReturnRecovery.miss();
    if(g_msiOwnedReturnRecovery.retryUsed() || !g_msiOwnedReturnRetrySafe){
        msiOwnedReturnNote(g_msiOwnedReturnRecovery.failed()?"unconfirmed":"waiting","no-matching-physical-signal-no-further-write");return;
    }
    // Busy refers to an OLDER I/O, not this command's writeAttempted bit.
    // A nonblocking completed-only reap preserves the retry while it is live.
    if(!msiOwnedReturnCommandReady()){
        msiOwnedReturnNote(g_msiOwnedReturnRecovery.failed()?"unconfirmed":"waiting","prior-command-IO-still-owned-retry-not-consumed");return;
    }
    // This is a new handle and live exact root observation, not the cached old
    // command path. PID1901 means it is already XInput: never switch it again.
    MsiClawHidDiag diag;HANDLE handle=msiClawOpenModeCommand(diag);
    std::wstring root;bool matchingDinput=false;
    if(handle!=INVALID_HANDLE_VALUE && diag.productId==0x1902 && !diag.interfacePath.empty() &&
       deviceInterfaceUsbInstance(U2W(diag.interfacePath).c_str(),root)){
        const auto usb=W2U(root);
        matchingDinput=msiRecovery::sameDeviceRoot(g_msiOwnedReturnExpected,usb,msiCrashLocations(usb));
    }
    if(matchingDinput && g_msiOwnedReturnRecovery.retryDue(GetTickCount64()) &&
       !msiDinputDeviceTargetAbsent() &&
       !msiOwnedReturnQuiesceReboundDinput(power,mode,topology,episode,sourceEvent)) {
        msiClawCloseModeCommand(handle);msiOwnedReturnPublish();
        msiOwnedReturnNote(g_msiOwnedReturnRecovery.failed()?"unconfirmed":"waiting","rebound-DInput-not-proven-owned-no-retry");return;
    }
    const auto sourceRevision=g_msiMainSourceRevision.load(std::memory_order_acquire);
    const bool safe=msiOwnedReturnContext(power,mode,episode) && inputHostEpochToken()==hostEpoch && g_xinputTopologyEpoch.load()==topology &&
        g_msiOwnedReturnSourceEvent.load()==sourceEvent && msiOwnedReturnRecordCurrent() && msiDinputDeviceTargetAbsent() &&
        g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)==0 &&
        g_inputHostVirtualIntentRevision.load()==intentRevision && g_msiMainSourceRevision.load()==sourceRevision;
    if(!g_msiOwnedReturnRecovery.claimRetry(GetTickCount64(),safe,matchingDinput,g_msiOwnedReturnRetrySafe)){
        msiClawCloseModeCommand(handle);msiOwnedReturnPublish();
        msiOwnedReturnNote(g_msiOwnedReturnRecovery.failed()?"unconfirmed":"waiting","same-owned-DInput-or-retry-context-not-ready");return;
    }
    msiOwnedReturnPublish();msiOwnedReturnNote("retry-claimed","same-owned-DInput-no-expected-XInput-single-retry");
    // Full persona/source/durable facts were verified above, outside the I/O
    // slot. Host/pump/owned locks remain held; revisions revoke concurrent
    // settings/source mutations. Only atomics and lock-free leaf gates below.
    const auto contextCurrent=[power,mode,topology,episode,sourceEvent,hostEpoch,intentRevision,sourceRevision] {
        return msiClawKnownBoardAuthority() && hardwareWriteAllowed() && !inputHostExitFenceRaised() &&
            !g_msiNativeForegroundCyclePending.load() && !g_msiNativeForegroundPowerHold.load() &&
            !msiForegroundIsolationBorrowBusy() && !g_inputHostProcess &&
            (!g_inputHostPrepared || g_inputHostReleaseQuarantined) && !g_inputHostActive &&
            !g_msiDinputRouteDinputArmed.load() && g_msiDinputReturnOwed.load() &&
            currentPowerGeneration()==power && g_msiFeedbackModeGeneration.load()==mode &&
            g_msiDinputReturnDebtEpisode.load()==episode && inputHostEpochToken()==hostEpoch &&
            g_xinputTopologyEpoch.load()==topology && g_msiOwnedReturnSourceEvent.load()==sourceEvent &&
            g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)==0 &&
            g_inputHostVirtualIntentRevision.load()==intentRevision && g_msiMainSourceRevision.load()==sourceRevision;
    };
    const auto outcome=msiClawSwitchModeOnHandle(1,handle,diag,true,contextCurrent);
    if(outcome.write.ok)msiNoteDisruptiveTx("switch-xinput-owned-return-retry");
    msiOwnedReturnNote("retry-finished","command-result-is-not-physical-return-proof",&outcome);
    // Do not begin another plan or discharge on a send receipt. Only later,
    // exact matching fresh XInput windows above can finish the pending return.
}
