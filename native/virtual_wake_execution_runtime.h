#pragma once
#include <charconv>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

// VC-LIFECYCLE-20261020-R3, reference comments only.
// Power recovery is ordinary serialized execution. Capture its power episode
// and player origin at ARM, preserve the first real-time budget, and bind each
// queued item to its actual Host epoch. A protected player origin additionally
// retains its original30s deadline; this is never a fallback reservation.
// Old work cannot publish a newer kept-target proof or clear a newer arm.
namespace ymcc::inputHost::wakeExecutionRuntime {
// Keep the opt-in inert until InputHost can recreate its own target without
// the ordinary orphan sweep or the SDK's implicit driver deployment.
static constexpr bool kFseTargetOnlyReconnectAvailable = false;
struct Work {
    std::uint64_t epoch=0,generation=0,requestId=0,serial=0,playerDeadline=0;
    std::uint64_t suspendSerial=0;
    bool suspendBound=false;
    bool fseReconnectOptedIn=false,fseReconnectClaimed=false;
    std::uint64_t fseReconnectIntent=0,fseReconnectHost=0;
    ULONGLONG armedAt=0,maxAgeMs=0;
    // MSI source admission may retain request 0; capture identity at real ARM,
    // never from a later HELLO epoch or an unrelated TLS deadline.
    std::uint64_t msiIntentRevision=0;
    std::string msiPersona;
    bool msiSourceBound=false;
    // GPD normal wake may inherit an expired first ON, without renewing it.
    // Capture only at the real ARM; keep MSI admission independent.
    std::uint64_t gpdIntentRevision=0;
    std::string gpdPersona;
    bool gpdStartBound=false;
    std::uint64_t win5ReaderHost=0,win5ReaderDemand=0;
    bool protectedPlayer=false,completed=false;
    unsigned recreateAttempts=0; // Original wake episode; never renewed by dispatch or notification.
    unsigned startAttempts=0; // Empty-Host creation attempts; waiting for old return is not an attempt.
    unsigned keptHideFailures=0; // Same wake episode; failed hide must not publish kept completion.
    unsigned physicalReturnWaits=0;
    std::uint64_t physicalReturnWaitHost=0;
    bool pendingPhysicalReturnRetry=false; // Diagnostic origin of the next dispatch only.
};
static std::mutex mx; // Metadata only; never held across pipe/mode/HidHide IO.
static Work episode;
// Only the old tuple this exact wake execution attempted to retire may grant
// continuation after the common return owner proves endpoint/visibility release.
struct ReturnWait { Work work; std::uint64_t host=0; std::string run,persona; bool pending=false; };
static ReturnWait returnWait;
// Completed endpoint replacement may precede its first actual accepted input.
// This is a read-only stage of the SAME player request, never a new wake arm,
// another target command, fallback reservation or renewed inspection window.
struct SourceWait {
    std::uint64_t serial=0,request=0,host=0,power=0,targetRevision=0,playerDeadline=0;
};
static SourceWait sourceWait;
static std::uint64_t nextSerial=0;
static std::uint64_t fseReconnectLastSuspend=0;
static thread_local const Work* active=nullptr;
static bool sameLocked(const Work& w) noexcept {
    return w.serial && w.serial==episode.serial && w.generation==episode.generation &&
        w.requestId==episode.requestId;
}
static void log(const Work& w,const char* stage,const char* reason,unsigned physicalReturnWaits=0,bool repeatedPhysicalWait=false,
        const char* win5ReaderPermit=nullptr,std::uint64_t win5ReaderNotice=0) noexcept {
    try{json row={
        {"lifecycleVersion",ymcc::VirtualStartProtection::lifecycleVersion},
        {"stage",stage},{"reason",reason},{"wakeSerial",w.serial},
        {"powerGeneration",w.generation},{"hostEpoch",w.epoch},
        {"requestId",w.requestId},{"protectedPlayer",w.protectedPlayer},
        {"originalPlayerDeadline",w.playerDeadline},{"armedAt",w.armedAt},
        {"maxAgeMs",w.maxAgeMs},{"role","normal-power-execution"},
        {"fallbackCharged",false},{"candidateIsFinal",false},{"recreateAttempts",w.recreateAttempts},{"startAttempts",w.startAttempts},{"keptHideFailures",w.keptHideFailures}};
        if(w.suspendBound)row["suspendSerial"]=w.suspendSerial;
        if(physicalReturnWaits)row["physicalReturnWaits"]=physicalReturnWaits;
        if(win5ReaderPermit){
            row["win5ReaderPermit"]=win5ReaderPermit;
            row["win5ReaderArmHost"]=w.win5ReaderHost;
            row["win5ReaderArmDemand"]=w.win5ReaderDemand;
            row["win5ReaderCurrentDemand"]=g_inputHostRecoveryIntent.revision();
            row["win5ReaderNotificationSequence"]=win5ReaderNotice;
        }
        appendNativeLifecycleLog(repeatedPhysicalWait?"wake-physical-return-wait-sample":"input-host-wake-execution",row);
    }catch(...){}
}
static const char* retiredLocked(const Work& w) {
    if(!sameLocked(w)||episode.completed)return "wake-episode-retired";
    if(w.generation!=currentPowerGeneration())return "power-generation-retired";
    if(w.suspendBound && w.suspendSerial!=g_sharedPowerSuspendSerial.load(std::memory_order_acquire))return "power-suspend-boundary-retired";
    const auto now=GetTickCount64();
    if(!w.armedAt || now<w.armedAt || now-w.armedAt>=w.maxAgeMs)return "original-wake-budget-expired";
    const auto clock=inputHostStartProtectionNow();const auto view=g_virtualStartProtection.view(clock);
    if(view.requestId!=w.requestId)return "player-request-retired";
    if(w.protectedPlayer && (clock>=w.playerDeadline ||
        !g_virtualStartProtection.canContinueExecution(w.requestId,view.persona,clock)))
        return "original-player-period-finished";
    if(inputHostExitFenceRaised()||g_exitRequested.load(std::memory_order_acquire)||g_inputCaptureStop.load(std::memory_order_acquire))return "exit-or-capture-stop";
    return nullptr;
}
static bool paused() {
    return !g_inputReady.load(std::memory_order_acquire) || !sgVirtualWriterAllowedNow() ||
        g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire) ||
        g_externalPriorityVirtualSuspended.load(std::memory_order_acquire) ||
        g_shortcutRecordingActive.load(std::memory_order_acquire) || physicalReleaseWindowActive();
}
static bool wanted() {
    return g_realStickTest.enabled && inputHostConfiguredPersona()!="disabled" &&
        (g_realStickTest.virtualTargetEnabled || g_externalPriorityVirtualSuspended.load(std::memory_order_acquire) ||
         g_shortcutRecordingActive.load(std::memory_order_acquire));
}
// Take a value: the argument may be returnWait.work, which retirement clears.
// Its original identity must survive that clear in the diagnostic receipt.
static void retireLocked(Work w,const char* reason) {
    unsigned waits=0;
    if(sameLocked(w)){waits=episode.physicalReturnWaits;episode.completed=true;if(returnWait.work.serial==w.serial)returnWait={};g_wakeRescanRequested.store(false,std::memory_order_release);}
    log(w,"retired",reason,waits);
}
static void retire(const Work& w,const char* reason) {
    std::lock_guard<std::mutex> lock(mx);retireLocked(w,reason);
}
static void retry(const Work& w,ULONGLONG backoff,const char* reason,bool physicalReturnWait=false) {
    std::lock_guard<std::mutex> lock(mx);
    if(const auto stale=retiredLocked(w)){retireLocked(w,stale);return;}
    if(!wanted()){retireLocked(w,"player-intent-off");return;}
    episode.pendingPhysicalReturnRetry=physicalReturnWait;
    g_wakeHotReplaceRetryAfterTick.store(GetTickCount64()+backoff,std::memory_order_release);
    g_wakeRescanRequested.store(true,std::memory_order_release);
    if(!physicalReturnWait){log(w,"retained",reason);return;}
    const bool repeated=episode.physicalReturnWaits && episode.physicalReturnWaitHost==w.epoch;
    ++episode.physicalReturnWaits;
    episode.physicalReturnWaitHost=w.epoch;
    if(repeated && !domainLogEnabled("virtual"))return;
    log(w,"retained",reason,episode.physicalReturnWaits,repeated);
}
// Caller holds the Host mutex and has checked this wake's admission. A Win5
// SD Host can outlive the USB source; there is nothing current to hide yet.
// Wait inside the original wake instead of spending an actual hide attempt.
static bool deferWin5KeptHideForSourceLocked(const Work& w) {
    if(g_machineIdentity.family!=YmccFamily::Gpd ||
        g_machineIdentity.deviceClass!="HandheldCompanion.Devices.GPDWin5" ||
        g_inputHostBoundPersona!="steamdeck" || !w.epoch || w.epoch!=g_inputHostEpoch ||
        !sgShouldKeepInputDuringStandby() || !g_inputHostProcess || !inputHostProcessAliveLocked() ||
        !g_inputHostPrepared || !g_inputHostNeutralized || g_inputHostTransportFault ||
        g_inputHostReleaseQuarantined || g_inputHostRunId.empty() ||
        !g_hidHideVisibilityJournal.active || g_hidHideVisibilityJournal.transactionId!=g_inputHostRunId ||
        g_hidHideVisibilityJournal.physicalSources.size()!=1 || inputHostWakePhysicalHandshakeReady())return false;
    const auto source=inputHostRecoveryHealthLatestSourceLocked();
    if(source.live && ymcc::input::recoveryHealth::fresh(source,GetTickCount64(),w.generation,
        g_xinputTopologyEpoch.load(std::memory_order_acquire)))return false;
    retry(w,kWakeHotReplaceBackoffMs[0],"kept-target-waiting-for-physical-return",true);
    return true;
}
static bool before(const Work& w,const char* stage) {
    std::lock_guard<std::mutex> lock(mx);
    if(const auto stale=retiredLocked(w)){retireLocked(w,stale);return false;}
    if(episode.fseReconnectClaimed)return false;
    if(!wanted()){retireLocked(w,"player-intent-off");return false;}
    if(paused()){
        episode.pendingPhysicalReturnRetry=false;
        g_wakeRescanRequested.store(true,std::memory_order_release);
        g_wakeHotReplaceRetryAfterTick.store(GetTickCount64()+250,std::memory_order_release);
        log(w,"retained",stage);return false;
    }
    const auto currentEpoch=inputHostEpochToken();
    if(currentEpoch!=w.epoch){
        if(!currentEpoch){episode.pendingPhysicalReturnRetry=false;g_wakeRescanRequested.store(true,std::memory_order_release);log(w,"retained","old-host-retired-recapture-empty-epoch");}
        else retireLocked(w,"host-epoch-retired");
        return false;
    }
    return true;
}
static std::string encode(const Work& w) {
    auto value="wake-hot-replace:"+std::to_string(w.epoch)+":power:"+std::to_string(w.generation)+
        ":request:"+std::to_string(w.requestId)+":wake:"+std::to_string(w.serial);
    if(msiClawKnownBoardAuthority())value+=":msi-power:"+std::to_string(w.generation);
    return value;
}
static bool number(const std::string& text,std::uint64_t& value) {
    if(text.empty())return false;const auto r=std::from_chars(text.data(),text.data()+text.size(),value);
    return r.ec==std::errc{}&&r.ptr==text.data()+text.size();
}
static bool load(const std::string& reason,Work& w) {
    const std::string prefix="wake-hot-replace:";if(reason.rfind(prefix,0)!=0)return false;
    std::istringstream text(reason.substr(prefix.size()));std::vector<std::string> fields;std::string part;
    while(std::getline(text,part,':'))fields.push_back(part);
    if((fields.size()!=7&&fields.size()!=9)||fields[1]!="power"||fields[3]!="request"||fields[5]!="wake")return false;
    Work key;if(!number(fields[0],key.epoch)||!number(fields[2],key.generation)||!number(fields[4],key.requestId)||!number(fields[6],key.serial))return false;
    if(fields.size()==9){std::uint64_t msi=0;if(fields[7]!="msi-power"||!number(fields[8],msi)||msi!=key.generation)return false;}
    std::lock_guard<std::mutex> lock(mx);if(!sameLocked(key))return false;
    w=episode;w.epoch=key.epoch;return true;
}
static void arm(std::uint64_t generation) {
    if(!generation)return;const auto clock=inputHostStartProtectionNow();const auto view=g_virtualStartProtection.view(clock);const auto now=GetTickCount64();
    std::lock_guard<std::mutex> lock(mx);
    const auto suspendSerial=g_sharedPowerSuspendSerial.load(std::memory_order_acquire);
    const auto reconnectIntent=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
    const auto reconnectHost=inputHostEpochToken();
    const bool fseReconnectOptedIn=kFseTargetOnlyReconnectAvailable && suspendSerial && reconnectHost &&
        g_controllerFseWakeReconnect.load(std::memory_order_acquire) &&
        !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) &&
        reconnectIntent==g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
    const auto readerDemand=g_inputHostRecoveryIntent.revision();
    const bool win5KeptSd=inputHostWin5KeptSteamDeckWake(inputHostConfiguredPersona());
    const bool suspendBound=fseReconnectOptedIn || win5KeptSd || msiClawKnownBoardAuthority() ||
        (sgShouldKeepInputDuringStandby() && psWakePolicyActive());
    if(suspendBound && generation!=currentPowerGeneration())return;
    // The existing public resume dispatcher can deliver another wake without
    // changing generation. A public revocation, not another resume, changes the key.
    const bool sameGeneration=episode.serial && episode.generation==generation;
    if(sameGeneration && (!suspendBound || episode.suspendSerial==suspendSerial)){
        if(episode.requestId!=view.requestId){retireLocked(episode,"duplicate-power-notification-from-retired-player");return;}
        if(!episode.completed)g_wakeRescanRequested.store(true,std::memory_order_release);
        log(episode,"duplicate","original-deadlines-retained");return;
    }
    if(sameGeneration && !wanted())return;
    if(sgShouldKeepInputDuringStandby()&&g_wakeKeptTargetGeneration.load(std::memory_order_acquire)==generation &&
        (!suspendBound || g_wakeKeptTargetSuspendSerial.load(std::memory_order_acquire)==suspendSerial))return;
    if(msiClawKnownBoardAuthority()&&g_msiVirtualWakeGeneration.load(std::memory_order_acquire)!=generation){
        g_msiVirtualWakeStartAttempts.store(0,std::memory_order_release);g_msiVirtualWakeArmTick.store(now,std::memory_order_release);g_msiVirtualWakeGeneration.store(generation,std::memory_order_release);
        try{appendNativeLifecycleLog("msi-virtual-wake-policy",{{"generation",generation},{"wakeBudgetMs",ymcc::msi::virtualWakeMaxAgeMs},{"genericBudgetMs",kWakeRescanMaxAgeMs},{"board",g_machineIdentity.product},{"policy","msi-family-only-bounded-not-firmware-latency"},{"duplicateNotificationsExtendDeadline",false}});}catch(...){}
    }
    if(++nextSerial==0)++nextSerial;
    returnWait={};sourceWait={};episode={};episode.generation=generation;episode.requestId=view.requestId;episode.serial=nextSerial;
    episode.suspendBound=suspendBound;episode.suspendSerial=suspendSerial;
    episode.fseReconnectOptedIn=fseReconnectOptedIn;
    episode.fseReconnectIntent=reconnectIntent;
    episode.fseReconnectHost=reconnectHost;
    // Bind continued output demand, not the publication revision of settings.
    // An in-flight writer is fenced at the reader; it need not cancel this wake.
    if(win5KeptSd && suspendSerial && reconnectHost && reconnectHost==inputHostEpochToken() &&
       readerDemand && readerDemand==g_inputHostRecoveryIntent.revision()){
        episode.win5ReaderHost=reconnectHost;
        episode.win5ReaderDemand=readerDemand;
    }
    episode.armedAt=msiClawKnownBoardAuthority()?g_msiVirtualWakeArmTick.load(std::memory_order_acquire):now;
    episode.maxAgeMs=inputHostWakeMaxAgeMs();episode.protectedPlayer=view.locked;
    if(msiClawKnownBoardAuthority() && !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)){
        episode.msiIntentRevision=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
        episode.msiPersona=inputHostConfiguredPersona();episode.msiSourceBound=true;
    }
    if(nativeForegroundProfile()==ymcc::nativeForeground::Profile::Gpd && view.requestId &&
            !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)){
        episode.gpdIntentRevision=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
        episode.gpdPersona=inputHostConfiguredPersona();
        episode.gpdStartBound=episode.gpdPersona==view.persona && episode.gpdPersona!="disabled" &&
            !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) &&
            episode.gpdIntentRevision==g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
    }
    if(view.locked)episode.playerDeadline=clock+view.remainingMs;
    g_wakeRescanGeneration.store(generation,std::memory_order_release);g_wakeRescanArmTick.store(episode.armedAt,std::memory_order_release);
    g_wakeHotReplaceRetryAfterTick.store(0,std::memory_order_release);g_wakeRescanRequested.store(true,std::memory_order_release);
    g_wakeTruthSnapshotUntilTick.store(now+30000,std::memory_order_release);log(episode,"armed","explicit-power-wake");
}
static bool pending(Work& w) {
    // The idle BUS tick can skip the lock. Recheck before borrowing the wake episode.
    if(!g_wakeRescanRequested.load(std::memory_order_acquire))return false;
    std::lock_guard<std::mutex> lock(mx);if(!g_wakeRescanRequested.load(std::memory_order_acquire))return false;
    w=episode;w.epoch=inputHostEpochToken();
    if(const auto stale=retiredLocked(w)){retireLocked(w,stale);return false;}
    if(!wanted()){retireLocked(w,"player-intent-off");return false;}
    return true;
}
struct QueueReceipt { unsigned afterPhysicalReturnWaits=0; bool coalesced=false; };
template<class Submit>
static bool queue(const Work& w,std::uint64_t epoch,QueueReceipt& receipt,Submit&& submit) {
    receipt={};
    std::string reason;
    {std::lock_guard<std::mutex> lock(mx);
        if(const auto stale=retiredLocked(w)){retireLocked(w,stale);return false;}
        if(paused()||!wanted())return false;
        auto queued=w;queued.epoch=epoch;reason=encode(queued);episode.epoch=epoch;
        if(episode.pendingPhysicalReturnRetry&&episode.physicalReturnWaitHost==epoch)
            receipt.afterPhysicalReturnWaits=episode.physicalReturnWaits;
        episode.pendingPhysicalReturnRetry=false;
        g_wakeRescanRequested.store(false,std::memory_order_release);g_wakeHotReplaceRetryAfterTick.store(0,std::memory_order_release);
    }
    if(submit(reason.c_str(),&receipt.coalesced))return true;
    retry(w,250,"queue-refused");return false;
}
// Caller has returned from IO. Host->metadata lock order is never reversed;
// arm/pending/retry touch metadata only and never wait for the Host mutex.
static bool beginDeadHostStart(const Work& w) {
    std::lock_guard<InputHostMutex> hostLock(g_inputHostMx);std::lock_guard<std::mutex> lock(mx);
    if(const auto stale=retiredLocked(w)){retireLocked(w,stale);return false;}
    if(paused()||!wanted()||g_inputHostReleaseQuarantined||g_inputHostStartBlocked||g_inputHostFaultHoldsStart.load())return false;
    if(g_inputHostProcess){
        // A failed liveness query is unknown, never evidence that a live old
        // target can be replaced. Only its actual signaled child may retire.
        if(WaitForSingleObject(g_inputHostProcess,0)!=WAIT_OBJECT_0||!g_inputHostEpoch||g_inputHostEpoch!=w.epoch||g_inputHostRunId.empty())return false;
        returnWait={w,g_inputHostEpoch,g_inputHostRunId,inputHostConfiguredPersona(),true};
        log(w,"return-begin","signaled-old-host-original-wake-retirement");
    } else {
        // Common normal-wake execution shares the existing attempt ceiling.
        // Do not spend it on the mandatory old-Host Stop/return continuation.
        if(episode.startAttempts>=kWakeHotReplaceMaxAttempts){retireLocked(w,"dead-host-start-budget-exhausted");return false;}
        ++episode.startAttempts;log(episode,"start-attempt","original-wake-budget");
    }
    return true;
}
static bool awaitingReturn(const Work& w) {
    std::lock_guard<std::mutex> lock(mx);
    return returnWait.pending&&sameLocked(w)&&returnWait.work.serial==w.serial&&!retiredLocked(w);
}
// A post-final fault can already have stopped this Host before the new wake.
// Keep the original wake budget while the existing quarantine worker returns it.
static bool waitForStoppedMsiReturnLocked(const Work& w) {
    if(!g_inputHostMx.ownedByCurrentThread() || !w.epoch || w.epoch!=g_inputHostEpoch ||
       g_inputHostRunId.empty() || !inputHostRuntimeMsiReturnRequiredLocked())return false;
    std::lock_guard<std::mutex> lock(mx);
    ymcc::sleeppower::SharedPowerV1 power;
    if(retiredLocked(w)||paused()||!wanted()||!w.msiSourceBound||
       g_inputHostFaultHoldsStart.load(std::memory_order_acquire)||
       g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)||
       w.msiIntentRevision!=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)||
       w.msiPersona!=inputHostConfiguredPersona()||
       !captureSharedPowerLifecycle(power)||power.generation!=w.generation||power.suspendSerial!=w.suspendSerial)return false;
    returnWait={w,g_inputHostEpoch,g_inputHostRunId,w.msiPersona,true};
    log(w,"return-begin","quarantined-stopped-msi-host");
    return true;
}
// One real RECREATE per queue dispatch. Retain the count with the original
// wake, return owner control during backoff, and never reserve fallback quota.
static bool reserveRecreate(Work& w,unsigned& attempt) {
    std::lock_guard<std::mutex> lock(mx);
    if(const auto stale=retiredLocked(w)){retireLocked(w,stale);return false;}
    if(paused()||!wanted())return false;
    if(episode.recreateAttempts>=kWakeHotReplaceMaxAttempts){retireLocked(w,"recreate-budget-exhausted");return false;}
    attempt=++episode.recreateAttempts;w.recreateAttempts=attempt;log(w,"recreate-attempt","original-wake-budget");return true;
}
static void recreateRejected(const Work& w) {
    std::lock_guard<InputHostMutex> hostLock(g_inputHostMx);std::lock_guard<std::mutex> lock(mx);
    if(retiredLocked(w)||paused()||!w.protectedPlayer)return;
    // Only an original validated Host negative, never missing source, a pause,
    // timeout or failed admission, may invalidate provisional execution success.
    inputHostStartProtectionFinish(w.requestId,"failure","wake-owned-recreate-negative-receipt");
    log(episode,"negative-receipt","original-execution-candidate-invalidated");
}
static bool keptSourceCurrentLocked(const Work& w) {
    if(!g_xinputPhysicalSourceLocked && !g_hidHideVisibilityJournal.active &&
       !g_msiDinputRouteDinputArmed.load(std::memory_order_acquire) && !g_inputHostFreshNeutralAdmitted)return true;
    const auto source=inputHostRecoveryHealthLatestSourceLocked();
    return source.live && source.stable && source.incarnation && source.continuity &&
        source.continuity!=std::numeric_limits<std::uint64_t>::max() &&
        source.identity==g_xinputSlotIdentityEpoch.load(std::memory_order_acquire) &&
        ymcc::input::recoveryHealth::fresh(source,GetTickCount64(),w.generation,
            g_xinputTopologyEpoch.load(std::memory_order_acquire));
}
// Sample the OS outside both locks. Silence isn't evidence of disconnection.
static ULONGLONG observeFseReconnect(const Work& w) {
    if(!w.fseReconnectOptedIn || !g_controllerFseWakeReconnect.load(std::memory_order_acquire))return 0;
    const auto observedAt=GetTickCount64();
    const auto fse=ymcc::bootmode::readPlatform();
    return fse.activeKnown && fse.active ? observedAt : 0;
}
static bool fseReconnectCurrentLocked(const Work& w,const ymcc::sleeppower::SharedPowerV1& power) {
    return w.fseReconnectOptedIn && !retiredLocked(w) && !paused() &&
        power.generation==w.generation && power.suspendSerial==w.suspendSerial && sharedPowerLifecycleCurrent(power) &&
        !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) &&
        w.fseReconnectIntent==g_inputHostVirtualIntentRevision.load(std::memory_order_acquire) &&
        w.epoch && w.epoch==w.fseReconnectHost && w.epoch==g_inputHostEpoch &&
        g_realStickTest.enabled && g_realStickTest.virtualTargetEnabled && g_inputHostPrepared &&
        !g_inputHostReleaseQuarantined && !g_inputHostStartBlocked && !g_inputHostTransportFault &&
        !g_inputHostFaultHoldsStart.load(std::memory_order_acquire) && g_inputHostProcess && inputHostProcessAliveLocked();
}
static DWORD fseReconnectBudgetLocked(const Work& w,ULONGLONG fseObservedAt) {
    const auto now=GetTickCount64();
    if(!kFseTargetOnlyReconnectAvailable || !fseObservedAt || now<fseObservedAt || now-fseObservedAt>500 ||
       !g_controllerFseWakeReconnect.load(std::memory_order_acquire) || episode.fseReconnectClaimed ||
       episode.recreateAttempts || !w.suspendSerial || w.suspendSerial<=fseReconnectLastSuspend ||
       now<w.armedAt || now-w.armedAt>=w.maxAgeMs)return 0;
    auto remaining=w.maxAgeMs-(now-w.armedAt);
    if(w.protectedPlayer){
        const auto clock=inputHostStartProtectionNow();
        if(clock>=w.playerDeadline)return 0;
        remaining=(std::min<ULONGLONG>)(remaining,w.playerDeadline-clock);
    }
    // Reserve time for the existing 2s neutral request, within this wake.
    return remaining>=5000 ? static_cast<DWORD>((std::min<ULONGLONG>)(remaining-2000,kWakeRecreateTimeoutMs)) : 0;
}
static DWORD claimFseReconnectLocked(const Work& w,ULONGLONG fseObservedAt) {
    std::lock_guard<std::mutex> lock(mx);
    ymcc::sleeppower::SharedPowerV1 power;
    if(!captureSharedPowerLifecycle(power) || !fseReconnectCurrentLocked(w,power) ||
        !g_inputHostNeutralized || !keptSourceCurrentLocked(w))return 0;
    const auto budget=fseReconnectBudgetLocked(w,fseObservedAt);
    if(!budget)return 0;
    episode.fseReconnectClaimed=true;
    fseReconnectLastSuspend=w.suspendSerial;
    ++episode.recreateAttempts;
    log(episode,"recreate-attempt","fse-opt-in-one-owned-endpoint-reconnect");
    return budget;
}
static bool continueFseReconnectLocked(const Work& w) {
    std::lock_guard<std::mutex> lock(mx);
    ymcc::sleeppower::SharedPowerV1 power;
    return episode.fseReconnectClaimed && captureSharedPowerLifecycle(power) && fseReconnectCurrentLocked(w,power);
}
// Only called after current kept-host, physical-hide and public wake proof.
// Publishing is not permission to read through an active settings writer.
static const char* publishWin5WakeReader(std::uint64_t armHost,std::uint64_t host,std::uint64_t demand) noexcept {
    if(!armHost || armHost!=host)return "arm-host-mismatch";
    if(!demand || demand!=g_inputHostRecoveryIntent.revision())return "demand-changed";
    g_win5WakeReaderDemand.store(demand,std::memory_order_release);
    g_win5WakeReaderHost.store(host,std::memory_order_release);
    return "published";
}
static bool complete(const Work& w,bool kept,bool newStart=false,bool visibilityRefreshed=false,bool* offerReconnect=nullptr,
        ULONGLONG fseObservedAt=0,bool fseRecreated=false,const ymcc::sleeppower::SharedPowerV1* keptHidePower=nullptr) {
    if(offerReconnect)*offerReconnect=false;
    std::lock_guard<InputHostMutex> hostLock(g_inputHostMx);std::lock_guard<std::mutex> lock(mx);
    if(const auto stale=retiredLocked(w)){retireLocked(w,stale);return false;}
    if(episode.fseReconnectClaimed!=fseRecreated || (fseRecreated&&(kept||newStart)))return false;
    episode.pendingPhysicalReturnRetry=false;
    if(paused()||!wanted()){g_wakeRescanRequested.store(true,std::memory_order_release);return false;}
    if(!g_inputHostProcess||!inputHostProcessAliveLocked()||!g_inputHostPrepared||!g_inputHostNeutralized||
        (!newStart&&g_inputHostEpoch!=w.epoch)){retireLocked(w,"completed-host-proof-unavailable");return false;}
    // A notification may already have been consumed by the time the refresh
    // returns. Validate its original power admission before using the result.
    ymcc::sleeppower::SharedPowerV1 power;
    const auto captureCurrentWakePower=[&]{return captureSharedPowerLifecycle(power) && power.generation==w.generation &&
        (!w.suspendBound || power.suspendSerial==w.suspendSerial);};
    if(kept && (!keptHidePower || !captureCurrentWakePower() || !ymcc::sleeppower::samePowerRevocation(*keptHidePower,power))){
        g_wakeHotReplaceRetryAfterTick.store(GetTickCount64()+250,std::memory_order_release);
        g_wakeRescanRequested.store(true,std::memory_order_release);
        log(episode,"retained","kept-hide-shared-power-changed");return false;
    }
    // VC-WAKE-KEPT-VISIBILITY-20261111: keeping the endpoint is intentional,
    // but an unresolved physical identity or failed hide is unfinished NORMAL
    // wake execution. Keep its original Host, request, arm and hard deadlines;
    // retry only this stage, never recreate a target or reserve fallback quota.
    if(kept && !visibilityRefreshed){
        ++episode.keptHideFailures;
        if(episode.keptHideFailures>=kWakeHotReplaceMaxAttempts){
            retireLocked(episode,"kept-target-hide-budget-exhausted");return false;
        }
        g_wakeHotReplaceRetryAfterTick.store(GetTickCount64()+
            kWakeHotReplaceBackoffMs[episode.keptHideFailures-1],std::memory_order_release);
        g_wakeRescanRequested.store(true,std::memory_order_release);
        log(episode,"retained","kept-target-hide-unproven");return false;
    }
    // A successful hide/config query is not physical input. Consume the
    // original canonical sampler's fresh source proof before publishing the
    // kept-target wake binding. Missing source only waits in this same wake;
    // it is never a fabricated transport error or a request to recreate.
    if(!kept && !captureCurrentWakePower()){
        g_wakeRescanRequested.store(true,std::memory_order_release);return false;
    }
    // A motion/touch-only target may have been created without any physical
    // source. Preserve that existing route. A locked source (even mask0), an
    // owned physical journal, managed route, or this Host's prior physical
    // neutral admission keeps the source obligation; loss cannot erase it.
    if(kept && !keptSourceCurrentLocked(w)){
        g_wakeHotReplaceRetryAfterTick.store(GetTickCount64()+kWakeHotReplaceBackoffMs[0],std::memory_order_release);
        g_wakeRescanRequested.store(true,std::memory_order_release);
        log(episode,"retained","kept-target-current-source-unproven");return false;
    }
    if(const auto stale=retiredLocked(w)){retireLocked(w,stale);return false;}
    if(paused()||!wanted()){g_wakeRescanRequested.store(true,std::memory_order_release);return false;}
    // A pending shared notification revokes this completion even before its
    // generation changes. Diagnostic-only publication does not revoke it.
    if(!sharedPowerLifecycleCurrent(power)){
        g_wakeRescanRequested.store(true,std::memory_order_release);return false;
    }
    if(fseRecreated && !fseReconnectCurrentLocked(w,power))return false;
    const char* win5ReaderPermit=nullptr;
    if(kept){
        if(offerReconnect && fseReconnectBudgetLocked(w,fseObservedAt) && fseReconnectCurrentLocked(w,power)){
            *offerReconnect=true;
            return false;
        }
        g_inputHostKeptTupleWakeProof=w.protectedPlayer && w.requestId && g_inputHostBoundPowerGeneration
            ? InputHostKeptTupleWakeProof{g_inputHostEpoch,w.generation,w.requestId,g_inputHostBoundPowerGeneration,power.suspendSerial}
            : InputHostKeptTupleWakeProof{};
        const bool win5Reader=inputHostWin5KeptSteamDeckWake(g_inputHostBoundPersona);
        g_win5WakeReaderHost.store(0,std::memory_order_release);
        g_wakeKeptTargetSuspendSerial.store(power.suspendSerial,std::memory_order_release);
        g_wakeKeptTargetGeneration.store(w.generation,std::memory_order_release);
        if(win5Reader){
            if(!visibilityRefreshed || !power.suspendSerial || power.suspendSerial!=w.suspendSerial)win5ReaderPermit="wake-proof-mismatch";
            else win5ReaderPermit=publishWin5WakeReader(w.win5ReaderHost,w.epoch,w.win5ReaderDemand);
        }
    }
    else if(!newStart){
        if(g_psEndpointOwnership.capable()&&!g_psEndpointOwnership.snapshot().valid){retireLocked(w,"recreated-target-ownership-unproven");return false;}
        g_inputHostRecreatedWakePowerProof={g_inputHostEpoch,w.generation,power.suspendSerial};
        log(w,"target-proof","same-host-recreate-and-neutral-current-power-proven");
    }
    if(returnWait.work.serial==w.serial)returnWait={};
    episode.epoch=g_inputHostEpoch;
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    if(w.protectedPlayer&&view.requestId==w.requestId&&view.candidateOutcome=="failure"){
        sourceWait={w.serial,w.requestId,g_inputHostEpoch,w.generation,g_psEndpointOwnership.revision(),w.playerDeadline};
        log(episode,"await-current-source","endpoint-ready-original-request-awaits-actual-report");
    }
    episode.completed=true;g_wakeHoldPhysicalUnhide.store(false,std::memory_order_release);g_wakeRescanRequested.store(false,std::memory_order_release);
    log(episode,"complete",fseRecreated?"fse-owned-endpoint-and-neutral-proven":
        kept?"same-host-current-power-proven":"normal-power-execution-complete",episode.physicalReturnWaits,
        false,win5ReaderPermit,power.observedNotificationSequence);return true;
}
// Caller holds only metadata (or Host -> lease -> metadata). No IO/queue or
// Host acquisition here. A lease observer may retain its exact existing owner;
// actual Start and first helper dispatch separately require the real TLS Scope.
static std::uint64_t gpdRemainingLocked(const Work& w,std::uint64_t ticket,const std::string& persona,std::uint64_t clock) {
    if(!w.gpdStartBound || !ticket || w.requestId!=ticket || w.gpdPersona!=persona ||
            nativeForegroundProfile()!=ymcc::nativeForeground::Profile::Gpd ||
            g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) ||
            w.gpdIntentRevision!=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire) ||
            inputHostConfiguredPersona()!=persona || !g_realStickTest.virtualTargetEnabled ||
            g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready ||
            retiredLocked(w) || paused() || !wanted())return 0;
    const auto view=g_virtualStartProtection.view(clock);
    if(view.requestId!=ticket || view.persona!=persona || view.phase=="cancelled")return 0;
    const auto now=GetTickCount64();
    if(now<w.armedAt || now-w.armedAt>=w.maxAgeMs)return 0;
    // Persona/protection reads above may wait while settings changes. Recheck
    // the immutable ARM intent and actual clocks after those reads, never
    // accepting a same-persona ON update as this old execution's authority.
    const auto finalClock=inputHostStartProtectionNow();
    const auto finalNow=GetTickCount64();
    if(finalClock<clock || finalNow<now || finalNow<w.armedAt || finalNow-w.armedAt>=w.maxAgeMs ||
            g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) ||
            w.gpdIntentRevision!=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire) ||
            w.generation!=currentPowerGeneration() ||
            g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready ||
            !g_inputReady || g_inputHostPowerAdmissionRevoked || inputHostExitFenceRaised() ||
            g_exitRequested || g_inputCaptureStop || !g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled)return 0;
    auto remaining=static_cast<std::uint64_t>(w.maxAgeMs-(finalNow-w.armedAt));
    if(w.protectedPlayer){
        if(finalClock>=w.playerDeadline)return 0;
        remaining=(std::min)(remaining,w.playerDeadline-finalClock);
    }
    return remaining;
}
struct Scope {
    const Work* previous;std::uint64_t oldDeadline,oldRequest,oldPower;
    explicit Scope(const Work& w):previous(active),oldDeadline(g_inputHostSwitchRecoveryStartDeadline),oldRequest(g_inputHostSwitchRecoveryStartRequestId),oldPower(g_inputHostSwitchRecoveryStartPower){
        active=&w;const auto now=GetTickCount64(),clock=inputHostStartProtectionNow();
        const auto age=now>=w.armedAt?now-w.armedAt:w.maxAgeMs;
        auto deadline=clock+(age<w.maxAgeMs?w.maxAgeMs-age:0);
        if(w.protectedPlayer&&w.playerDeadline<deadline)deadline=w.playerDeadline;
        if(oldDeadline&&oldDeadline<deadline)deadline=oldDeadline;
        g_inputHostSwitchRecoveryStartDeadline=deadline;g_inputHostSwitchRecoveryStartRequestId=w.requestId;g_inputHostSwitchRecoveryStartPower=w.generation;
    }
    ~Scope(){active=previous;g_inputHostSwitchRecoveryStartDeadline=oldDeadline;g_inputHostSwitchRecoveryStartRequestId=oldRequest;g_inputHostSwitchRecoveryStartPower=oldPower;}
};
} // namespace ymcc::inputHost::wakeExecutionRuntime
// Read-only authority for the MSI HELLO source gate. Only this active normal
// wake execution can authorize its previously completed player target; an old
// queue token or an unrelated thread-local deadline grants no such authority.
static std::uint64_t inputHostWakeMsiSourceWaitRemaining(std::uint64_t ticket,const std::string& persona,std::uint64_t now,bool* activeExecution) {
    namespace runtime=ymcc::inputHost::wakeExecutionRuntime;
    if(activeExecution)*activeExecution=msiClawKnownBoardAuthority() && runtime::active;
    if(!msiClawKnownBoardAuthority() || !runtime::active)return 0;
    const auto& work=*runtime::active;
    const auto view=g_virtualStartProtection.view(now);
    if(!work.msiSourceBound || view.phase=="cancelled" || view.requestId!=ticket || work.requestId!=ticket ||
            work.msiPersona!=persona || g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) ||
            work.msiIntentRevision!=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire) ||
            work.generation!=currentPowerGeneration() ||
            inputHostConfiguredPersona()!=persona || !g_realStickTest.virtualTargetEnabled ||
            g_inputHostSwitchRecoveryStartRequestId!=ticket ||
            g_inputHostSwitchRecoveryStartPower!=work.generation ||
            !g_inputHostSwitchRecoveryStartDeadline || now>=g_inputHostSwitchRecoveryStartDeadline)return 0;
    std::lock_guard<std::mutex> lock(runtime::mx);
    if(runtime::retiredLocked(work) || runtime::paused() || !runtime::wanted())return 0;
    return g_inputHostSwitchRecoveryStartDeadline-now;
}
// GPD writer/post-hide execution consumes the original player period OR the
// actual normal wake's original budget. No new ticket, timeout or fallback.
static std::uint64_t inputHostGpdStartRemaining(std::uint64_t ticket,const std::string& persona,std::uint64_t now,std::uint64_t* wakeSerial) {
    namespace runtime=ymcc::inputHost::wakeExecutionRuntime;
    if(wakeSerial)*wakeSerial=0;
    if(!g_inputHostMx.ownedByCurrentThread() || nativeForegroundProfile()!=ymcc::nativeForeground::Profile::Gpd)return 0;
    if(runtime::active && runtime::active->gpdStartBound){
        const auto& w=*runtime::active;
        if(g_inputHostSwitchRecoveryStartRequestId!=ticket || g_inputHostSwitchRecoveryStartPower!=w.generation ||
                !g_inputHostSwitchRecoveryStartDeadline || now>=g_inputHostSwitchRecoveryStartDeadline)return 0;
        std::lock_guard<std::mutex> lock(runtime::mx);
        const auto remaining=runtime::gpdRemainingLocked(w,ticket,persona,now);
        const auto finalNow=inputHostStartProtectionNow();
        if(!remaining || finalNow<now || finalNow>=g_inputHostSwitchRecoveryStartDeadline ||
                g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) ||
                w.gpdIntentRevision!=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire))return 0;
        if(wakeSerial)*wakeSerial=w.serial;
        return (std::min)(remaining,g_inputHostSwitchRecoveryStartDeadline-finalNow);
    }
    const auto view=g_virtualStartProtection.view(now);
    return ticket && view.requestId==ticket && view.persona==persona && view.locked?view.remainingMs:0;
}
// Only the existing post-hide lease can retain this cookie, captured by Start.
// Other threads may check/poll that owner while Start releases HostMx to wait.
// They cannot establish a new helper or renew its separately saved deadline.
static std::uint64_t inputHostGpdPostHideWakeRemaining(std::uint64_t ticket,const std::string& persona,std::uint64_t now,std::uint64_t wakeSerial) {
    if(!wakeSerial || !g_inputHostMx.ownedByCurrentThread())return 0;
    namespace runtime=ymcc::inputHost::wakeExecutionRuntime;
    std::lock_guard<std::mutex> lock(runtime::mx);
    const auto& w=runtime::episode;
    return w.serial==wakeSerial?runtime::gpdRemainingLocked(w,ticket,persona,now):0;
}
static bool inputHostWakeReturnedLocked(std::uint64_t retiredHost,const std::string& retiredRun) {
    namespace runtime=ymcc::inputHost::wakeExecutionRuntime;
    std::lock_guard<std::mutex> lock(runtime::mx);auto& debt=runtime::returnWait;
    if(!debt.pending||debt.host!=retiredHost||debt.run!=retiredRun)return false;
    if(const auto stale=runtime::retiredLocked(debt.work)){runtime::retireLocked(debt.work,stale);return false;}
    if(debt.work.msiSourceBound && (g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)||
       debt.work.msiIntentRevision!=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire))){
        runtime::retireLocked(debt.work,"return-msi-intent-retired");return false;
    }
    if(!runtime::wanted()||inputHostConfiguredPersona()!=debt.persona||
        g_inputHostProcess||g_inputHostReleaseQuarantined||g_hidHideVisibilityJournal.active||
        g_inputHostPrepared||g_inputHostNeutralized||g_inputHostTransportFault||g_inputHostFaultHoldsStart.load())return false;
    // Preserve expected pauses and the original expiry; no request/clock/quota
    // is renewed. Only this debt's old completed return clears its start block.
    const auto work=debt.work;debt={};g_inputHostStartBlocked=false;
    runtime::episode.pendingPhysicalReturnRetry=false;
    g_wakeHotReplaceRetryAfterTick.store(0,std::memory_order_release);
    g_wakeRescanRequested.store(true,std::memory_order_release);
    runtime::log(work,"return-proven","common-owner-proved-exact-old-tuple-released");return true;
}
static std::string inputHostWakeWorkReason(unsigned long long epoch,unsigned long long generation) {
    namespace runtime=ymcc::inputHost::wakeExecutionRuntime;
    std::lock_guard<std::mutex> lock(runtime::mx);auto work=runtime::episode;work.epoch=epoch;
    if(work.generation!=generation){work={};work.epoch=epoch;work.generation=generation;}
    return runtime::encode(work);
}
static bool inputHostWakeExecutionPending(std::uint64_t ticket) {
    namespace runtime=ymcc::inputHost::wakeExecutionRuntime;
    std::lock_guard<std::mutex> lock(runtime::mx);const auto& w=runtime::episode;
    auto& awaiting=runtime::sourceWait;
    if(awaiting.request){
        const auto clock=inputHostStartProtectionNow();const auto view=g_virtualStartProtection.view(clock);
        const bool current=ticket==awaiting.request&&view.requestId==awaiting.request&&view.locked&&
            awaiting.serial==w.serial&&awaiting.host==g_inputHostEpoch&&awaiting.power==currentPowerGeneration()&&
            (!w.suspendBound || w.suspendSerial==g_sharedPowerSuspendSerial.load(std::memory_order_acquire))&&
            awaiting.targetRevision==g_psEndpointOwnership.revision()&&clock<awaiting.playerDeadline;
        if(!current)awaiting={};
        else if(ymcc::inputHost::switchVerificationRuntime::currentOutputProvenLocked(view)){
            runtime::log(w,"source-verified","actual-current-source-report-completed-original-execution");awaiting={};
        }else return true; // Original30s cap remains; no IO or new repair is scheduled.
    }
    return ticket&&w.protectedPlayer&&w.requestId==ticket&&g_wakeRescanRequested.load(std::memory_order_acquire)&&!runtime::retiredLocked(w);
}

// VC-ROG-IDLE-WAKE-HOLD-20261024-R1 (isolated candidate).
// Keep existing protection whenever a target is wanted or any return owner is
// unresolved. An OFF, fully retired ROG session has no source to protect from
// unhide: do not create a new orphan hold that can block the next ON request.
// This helper is called only at the three original suspend edges, never from
// a timer/frame. It never clears an existing hold, starts a target, or retries.
static void inputHostArmWakePhysicalHold(const char* origin) {
    bool protect=true; // Unknown/contended observation keeps the existing policy.
    if(ymcc::inputHost::switchVerificationRuntime::rogXboxSignalScope()) {
        const bool alreadyOwned=g_inputHostMx.ownedByCurrentThread();
        std::unique_lock<InputHostMutex> lock(g_inputHostMx,std::defer_lock);
        if(alreadyOwned || lock.try_lock()) {
            protect=(g_realStickTest.enabled && g_realStickTest.virtualTargetEnabled) ||
                g_inputHostEpoch!=0 || !inputHostOwnedCleanupProvenLocked() ||
                g_inputHostSwitchExecutionReturnPending.load(std::memory_order_acquire) ||
                g_inputHostSwitchRecoveryPending.load(std::memory_order_acquire) ||
                g_inputHostRecoveryPending.load(std::memory_order_acquire) ||
                g_inputHostTupleHealPending.load(std::memory_order_acquire) ||
                g_msiNativeForegroundCyclePending.load(std::memory_order_acquire) ||
                g_physicalReleaseWindowDutyActive.load(std::memory_order_acquire);
        }
    }
    if(protect) {
        g_wakeHoldPhysicalUnhide.store(true,std::memory_order_release);
        return;
    }
    // Diagnostic loss cannot turn the no-device/no-owner decision into an arm.
    try {appendNativeLifecycleLog("input-host-idle-wake-hold-skipped",{
        {"policy","VC-ROG-IDLE-WAKE-HOLD-20261024-R1"},{"origin",origin?origin:"unknown"},
        {"reason","off-owned-cleanup-proven"},{"newHoldArmed",false},
        {"existingHoldCleared",false},{"deviceWrite",false},{"targetCreated",false}});}catch(...){}
}
