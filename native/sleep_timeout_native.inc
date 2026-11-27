// Included by main.cpp after the existing sleep retry lifecycle. All state
// mutations and timers are UI-thread owned; completion facts are shared with callbacks.
#include "sleep_kernel_cycle_native.inc"
static ymcc::sleeptimeout::Window g_sgSleepTimeoutWindow;
static ULONGLONG sgSleepTimeoutSuccessfulExitTime() {
    return g_sgKernelSystemWake.load(std::memory_order_acquire);
}
// Only the public final 507 handler can bind this age proof. It neither
// changes the live intent/lease nor makes a PBT receipt into a wake fact.
struct SgContinuedWake {
    ULONGLONG generation=0,sequence=0,entry=0,exit=0,origin=0;
    int reason=-1;
};
static SgContinuedWake g_sgContinuedWake;
static ULONGLONG sgContinuedWakeOrigin(const SgContinuedWake& wake) {
    const auto boundary=g_sleepPowerDispatch.latestSleepBoundary();
    if(!wake.entry||!wake.sequence||boundary.sequence!=wake.sequence||boundary.type!=PBT_APMSUSPEND ||
       g_sleepPowerDispatch.pendingSuspend()||g_sgTask.generation!=wake.generation||g_sgTask.mode!=SgSleepMode::S3||
       !g_sgRepairEligible||g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire)!=boundary.fileTime100ns||
       !boundary.fileTime100ns||g_sgClosedSleepIntentFileTime.load(std::memory_order_acquire)>=boundary.fileTime100ns)
        return 0;
    const auto entry=g_sgKernelCycles.continuedUserEntry(wake.generation,boundary.fileTime100ns,wake.entry,
        wake.exit,wake.reason,SG_S0_ENTRY_FAILURE_WINDOW_MS);
    if(!entry.at||g_sgLatestUserSleepEntryFileTime.load(std::memory_order_acquire)!=entry.at ||
       sgUserWakeCancelsSleepIntent(entry.at,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire))||
       sgSleepTimeoutSuccessfulExitTime()>=entry.at ||
       g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire)!=wake.entry||
       g_sgLastKernel507EventFileTime.load(std::memory_order_acquire)!=wake.exit||
       wake.sequence!=g_sleepPowerDispatch.observedSleepBoundarySequence()||wake.generation!=currentPowerGeneration())return 0;
    return entry.at;
}
static void sgBindContinuedWake(int reason,ULONGLONG at) {
    const auto generation=currentPowerGeneration();
    if(g_sgContinuedWake.generation==generation||!at||at>sgNowFileTime())return;
    SgContinuedWake wake{generation,g_sleepPowerDispatch.observedSleepBoundarySequence(),
        g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire),at,0,reason};
    wake.origin=sgContinuedWakeOrigin(wake);
    if(wake.origin)g_sgContinuedWake=wake;
}
static bool sgContinuedWakeAge(ULONGLONG& age) {
    age=0;
    const auto& wake=g_sgContinuedWake;
    if(!wake.origin||wake.generation!=currentPowerGeneration())return true;
    if(sgContinuedWakeOrigin(wake)!=wake.origin)return false;
    age=(wake.exit-wake.origin)/10000ULL;
    return true;
}
// UI-owned observation only. PBT is never a physical-button action permit.
// The existing timer and pool are used once; there is no standby poller.
struct SgSleepTimeoutOriginalJob {
    ymcc::sleeppower::SharedPowerV1 power;
    ULONGLONG observed=0,closed=0,entry=0,completionDueTick=0,completedTick=0;
    DWORD error=ERROR_SUCCESS;
    bool veto=false;
    std::atomic<bool> complete{false};
};
struct SgSleepTimeoutObservation {
    ULONGLONG generation=0,origin=0,lastGeneration=0;
    bool waiting=false,joined=false,classificationSettled=false;
    std::shared_ptr<SgSleepTimeoutOriginalJob> job;
};
static SgSleepTimeoutObservation g_sgSleepTimeoutObservation;
static ULONGLONG g_sgSleepTimeoutGeneration=0;
// Continuation ticket only; the public classifier retains its first deadline.
static ULONGLONG g_sgSleepTimeoutClassificationGeneration=0,g_sgSleepTimeoutClassificationOrigin=0;
static ymcc::sleeppower::SharedPowerV1 g_sgSleepTimeoutClassificationPower;

// Original timeout owner only: a staged lineage is not a power scope or action.
struct SgSleepTimeoutReentry {
    ULONGLONG origin=0,from=0,to=0;
    ymcc::sleeppower::Dispatch::Receipt receipt;
    bool pending=false,adopted=false,publicationDeferred=false;
};
static SgSleepTimeoutReentry g_sgSleepTimeoutReentry;
static ULONGLONG g_sgSleepTimeoutAdoptedEntry=0;
static ULONGLONG g_sgSleepTimeoutOriginalNativeBoundary=0,g_sgSleepTimeoutEvidenceEndTick=0;
static ULONGLONG g_sgSleepTimeoutEvidenceEntry=0;
static ULONGLONG g_sgSleepTimeoutPendingExitTick=0;
static ULONGLONG g_sgSleepTimeoutPendingSuccessorExit=0;
// Original Native callback cache; window retirement cannot erase source facts.
static void sgResetKernelPowerIntervalObservations() noexcept {
    g_sgKernelCycles.reset();
    g_sgContinuedWake={};
}
static void sgSleepTimeoutClearReentry() noexcept {
    g_sgSleepTimeoutAdoptedEntry=0;
    g_sgSleepTimeoutReentry={};

}
static void sgSleepTimeoutRecordKernel(int id,int reason,ULONGLONG at,ULONGLONG generation,
                                       bool continuing=false,bool sessionEnded=false,
                                       ymcc::sleeppower::KernelCycleIdentity identity={}) noexcept {
    g_sgKernelCycles.record(id,{at<=sgNowFileTime()?at:0,generation,reason,0,continuing,sessionEnded,0,identity});
}
static void sgSleepTimeoutStageReentry(ULONGLONG generation,const ymcc::sleeppower::Dispatch::Receipt& receipt) {
    const auto origin=g_sgSleepTimeoutWindow.origin,from=g_sgSleepTimeoutGeneration;
    g_sgSleepTimeoutReentry={};
    if(!g_sgSleepTimeoutWindow.waiting||g_sgSleepTimeoutWindow.requested||!origin||!from||
       from==~ULONGLONG{0}||generation!=from+1||generation!=currentPowerGeneration()||
       g_sgTask.generation!=generation||g_sgTask.mode!=SgSleepMode::S3||!g_sgRepairEligible||
       receipt.type!=PBT_APMSUSPEND||!receipt.sequence||!receipt.tick||
       !receipt.fileTime100ns||receipt.fileTime100ns<origin||
       receipt.fileTime100ns-origin>SG_S0_ENTRY_FAILURE_WINDOW_MS*10000ULL||
       g_sleepPowerDispatch.latestSleepBoundary().sequence!=receipt.sequence||
       g_sgLatestUserSleepEntryFileTime.load(std::memory_order_acquire)!=origin)return;
    g_sgSleepTimeoutReentry={origin,from,generation,receipt,true,false};
}
static bool sgSleepTimeoutReentryPending() {
    const auto& link=g_sgSleepTimeoutReentry;
    return link.pending&&g_sgSleepTimeoutWindow.waiting&&!g_sgSleepTimeoutWindow.requested&&
        link.origin==g_sgSleepTimeoutWindow.origin&&link.from==g_sgSleepTimeoutGeneration&&
        link.to==currentPowerGeneration()&&!g_sleepPowerDispatch.pendingSuspend()&&
        g_sleepPowerDispatch.latestSleepBoundary().sequence==link.receipt.sequence&&
        g_sgLatestUserSleepEntryFileTime.load(std::memory_order_acquire)==link.origin&&
        g_sgClosedSleepIntentFileTime.load(std::memory_order_acquire)<link.origin&&
        !sgUserWakeCancelsSleepIntent(link.origin,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire))&&
        !sgSleepTimeoutSuccessAfter(link.origin)&&g_guardEnabled&&g_sgRetrySleepTimeout&&
        !g_exitRequested.load(std::memory_order_acquire);
}
static bool sgKernelPowerWakePredatesLatestEntry(int,ULONGLONG at,ULONGLONG* laterEntry) {
    if(!at)return false;
    auto latest=g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire);
    const auto recorded=g_sgKernelCycles.latest().entry;
    if(recorded.generation&&recorded.at>latest)latest=recorded.at;
    if(at>=latest)return false;
    if(laterEntry)*laterEntry=latest;
    return true;
}
static bool sgSleepTimeoutDeferReentryPublication() {
    if(!sgSleepTimeoutReentryPending())return false;
    auto& link=g_sgSleepTimeoutReentry;
    const auto [entry,exit]=g_sgKernelCycles.latest();
    if((entry.generation!=link.from&&entry.generation!=link.to)||(exit.generation!=link.from&&exit.generation!=link.to)||entry.reason<0||entry.reason==1||entry.reason==3||
       exit.continuing||exit.reason==1||exit.reason==20||!exit.at||exit.at<link.origin||
       entry.at<=exit.at||entry.at<=g_sgSleepTimeoutAdoptedEntry||exit.at<g_sgSleepTimeoutAdoptedEntry||
       entry.at>link.receipt.fileTime100ns||
       g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire)>=entry.at)return false;
    link.publicationDeferred=true;return true;
}
static bool sgSleepTimeoutAdoptReentry() {
    auto& link=g_sgSleepTimeoutReentry;
    if(!link.pending||link.origin!=g_sgSleepTimeoutWindow.origin||
       link.from!=g_sgSleepTimeoutGeneration||link.to!=currentPowerGeneration()||
       g_sleepPowerDispatch.pendingSuspend()||g_sleepPowerDispatch.latestSleepBoundary().sequence!=link.receipt.sequence||
       g_sgLatestUserSleepEntryFileTime.load(std::memory_order_acquire)!=link.origin||
       g_sgClosedSleepIntentFileTime.load(std::memory_order_acquire)>=link.origin||
       sgUserWakeCancelsSleepIntent(link.origin,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire))||
       sgSleepTimeoutSuccessAfter(link.origin)||!g_guardEnabled||!g_sgRetrySleepTimeout||
       g_exitRequested.load(std::memory_order_acquire))return false;
    const auto [entry,exit]=g_sgKernelCycles.latest();
    // Original source order, not journal/GUI order. A bare/unclassified or
    // unrelated newer Suspend has no matching pre-receipt platform interval.
    if((entry.generation!=link.from&&entry.generation!=link.to)||(exit.generation!=link.from&&exit.generation!=link.to)||entry.reason<0||entry.reason==1||entry.reason==3||
       exit.continuing||exit.reason==1||exit.reason==20||!exit.at||exit.at<link.origin||
       entry.at<=exit.at||entry.at<=g_sgSleepTimeoutAdoptedEntry||exit.at<g_sgSleepTimeoutAdoptedEntry||
       entry.at>link.receipt.fileTime100ns||
       g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire)!=entry.at)return false;
    link.pending=false;link.adopted=true;
    g_sgSleepTimeoutAdoptedEntry=entry.at;
    g_sgSleepTimeoutGeneration=link.to;
    // No Window::arm, timer registration, action charge or game operation.
    return true;
}
static bool sgSleepTimeoutReentryCurrent() {
    const auto& link=g_sgSleepTimeoutReentry;
    return !link.adopted||(link.origin==g_sgSleepTimeoutWindow.origin&&link.to==currentPowerGeneration()&&
        g_sleepPowerDispatch.latestSleepBoundary().sequence==link.receipt.sequence&&
        !g_sleepPowerDispatch.pendingSuspend()&&
        g_sgLatestUserSleepEntryFileTime.load(std::memory_order_acquire)==link.origin);
}

static bool sgSleepTimeoutObservationCurrent() {
    const auto& observation=g_sgSleepTimeoutObservation;
    const auto inherited=g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire);
    const bool originalTask=g_sgTask.generation==observation.generation&&
        g_sgTask.mode==SgSleepMode::S3&&g_sgRepairEligible;
    // Input/game resource completion is independent of this original deadline.
    // Only the same previously admitted observation may outlive its task.
    const bool taskHeld=originalTask || (observation.classificationSettled&&g_sgTask.generation==0);
    return observation.waiting&&observation.origin&&observation.generation==currentPowerGeneration()&&
        taskHeld&&g_guardEnabled&&g_sgRetrySleepTimeout&&!g_exitRequested.load(std::memory_order_acquire)&&
        inherited&&inherited<=observation.origin&&observation.origin-inherited<=SG_S0_ENTRY_FAILURE_WINDOW_MS*10000ULL&&
        g_sgClosedSleepIntentFileTime.load(std::memory_order_acquire)<inherited&&
        !sgUserWakeCancelsSleepIntent(inherited,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire))&&
        !sgSleepTimeoutSuccessAfter(inherited);
}
static bool sgSleepTimeoutObservationHeld(unsigned long long generation) {
    return generation&&generation==currentPowerGeneration()&&generation==g_sgSleepTimeoutObservation.generation&&
        (sgSleepTimeoutObservationCurrent()||
         (g_sgSleepTimeoutObservation.joined&&g_sgSleepTimeoutWindow.active()&&sgSleepTimeoutCurrent()));
}
static bool sgSleepTimeoutSettleClassification(unsigned long long generation) {
    if(!sgSleepTimeoutObservationHeld(generation))return false;
    g_sgSleepTimeoutObservation.classificationSettled=true;
    return true;
}
static bool sgSleepTimeoutJoinEntry(int reason,ULONGLONG original) {
    auto& observation=g_sgSleepTimeoutObservation;
    if(reason!=1||!original||!observation.classificationSettled||
       !sgSleepTimeoutObservationHeld(currentPowerGeneration()))return false;
    if(g_sgSleepTimeoutWindow.active())return original==g_sgSleepTimeoutWindow.origin;
    if(original>observation.origin||observation.origin-original>SG_S0_ENTRY_FAILURE_WINDOW_MS*10000ULL||
       original<=g_sgClosedSleepIntentFileTime.load(std::memory_order_acquire)||
       g_sgLatestUserSleepEntryFileTime.load(std::memory_order_acquire)!=original||
       sgUserWakeCancelsSleepIntent(original,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire))||
       sgSleepTimeoutSuccessAfter(original))return false;
    g_sgLastPowerButtonSleepIntentFileTime.store(original,std::memory_order_release);
    sgSleepTimeoutArm(reason,original);
    if(!g_sgSleepTimeoutWindow.active())sgSleepTimeoutStop("settled-original-not-armed");
    // This exact original was consumed even if final action admission failed.
    // It must never fall through to re-open the completed public resource task.
    return true;
}
static void sgSleepTimeoutObserveSuspend() {
    if(!g_sgRepairEligible||!g_guardEnabled||!g_sgRetrySleepTimeout||g_exitRequested.load(std::memory_order_acquire)||
       g_sgTask.mode!=SgSleepMode::S3||g_sgTask.generation!=currentPowerGeneration())return;
    const auto origin=g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire);
    if(!origin||origin<=g_sgClosedSleepIntentFileTime.load(std::memory_order_acquire)||
       sgUserWakeCancelsSleepIntent(origin,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire))||
       sgSleepTimeoutSuccessAfter(origin))return;
    // The legacy entry high-water also admits Reason3; it is not button proof.
    // Only the original reason-bearing handler or this bounded read may arm.
    if(g_sgSleepTimeoutWindow.active())return;
    auto& observation=g_sgSleepTimeoutObservation;
    if(observation.lastGeneration==currentPowerGeneration())return;
    sgSleepTimeoutStop("new-observed-suspend");
    observation.generation=observation.lastGeneration=currentPowerGeneration();
    observation.origin=origin;observation.waiting=true;observation.joined=false;observation.classificationSettled=false;
    const auto now=sgNowFileTime();
    if(now<origin||origin>(std::numeric_limits<ULONGLONG>::max)()-ymcc::sleeptimeout::Window::delayMs*10000ULL){
        sgSleepTimeoutStop("observation-clock-invalid");return;
    }
    const auto elapsed=(now-origin)/10000ULL;
    const auto remaining=elapsed>=ymcc::sleeptimeout::Window::delayMs?1ULL:ymcc::sleeptimeout::Window::delayMs-elapsed;
    if(!g_hwnd||!SetTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID,static_cast<UINT>(remaining),nullptr)){
        sgSleepTimeoutStop("observation-timer-failed");return;
    }
    try{sgRecordFact("sleep-timeout-observation-armed",{{"generation",observation.generation},
        {"observedIntent100ns",origin},{"remainingMs",remaining},{"deadlineMs",ymcc::sleeptimeout::Window::delayMs},
        {"confirmedPhysicalButton",false},{"powerActionAuthorized",false},{"polling",false},
        {"detailLogsRequired",false},{"mandatoryEvidence",true}});}catch(...){}
}
static void sgSleepTimeoutReadOriginal(const std::shared_ptr<SgSleepTimeoutOriginalJob>& job) {
    const auto result=sgReadOriginalKernelCycle(job->power,job->observed,job->closed);
    job->error=result.error;
    job->entry=result.fact.entry;
    job->veto=result.fact.veto();
}
static bool sgSleepTimeoutResolveObservation() {
    auto& observation=g_sgSleepTimeoutObservation;
    if(!sgSleepTimeoutObservationCurrent()){sgSleepTimeoutStop("observation-revoked");return false;}
    const auto now=sgNowFileTime();
    if(now<observation.origin){sgSleepTimeoutStop("observation-clock-rollback");return false;}
    const auto elapsed=(now-observation.origin)/10000ULL;
    if(elapsed<ymcc::sleeptimeout::Window::delayMs){
        if(!SetTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID,static_cast<UINT>(ymcc::sleeptimeout::Window::delayMs-elapsed),nullptr))
            sgSleepTimeoutStop("observation-timer-failed");
        return false;
    }
    if(!observation.job){
        auto job=std::make_shared<SgSleepTimeoutOriginalJob>();
        if(!ymcc::sleeppower::capturePowerObservation(&readSharedSystemPower,job->power)||
           (job->power.flags&ymcc::sleeppower::kNotificationPending)||!sgSleepTimeoutObservationCurrent()){
            sgSleepTimeoutStop("observation-source-not-current");return false;
        }
        job->observed=observation.origin;job->closed=g_sgClosedSleepIntentFileTime.load(std::memory_order_acquire);
        job->completionDueTick=GetTickCount64()+2000;
        observation.job=job;
        const auto window=g_hwnd;
        if(!SetTimer(window,SG_SLEEP_TIMEOUT_TIMER_ID,2000,nullptr)||!poolTrySubmitExisting([job,window]{
            sgSleepTimeoutReadOriginal(job);job->completedTick=GetTickCount64();job->complete.store(true,std::memory_order_release);
            if(!g_exitRequested.load(std::memory_order_acquire))PostMessageW(window,WM_TIMER,SG_SLEEP_TIMEOUT_TIMER_ID,0);
        })){
            sgSleepTimeoutStop("observation-source-unavailable");return false;
        }
        try{sgRecordFact("sleep-timeout-original-source-requested",{{"generation",observation.generation},
            {"observedIntent100ns",observation.origin},{"maximumEvents",8},{"maximumSourceMs",1000},
            {"maximumCompletionMs",2000},{"oneShot",true},{"powerActionAuthorized",false},
            {"detailLogsRequired",false},{"mandatoryEvidence",true}});}catch(...){}
        return false;
    }
    const auto job=observation.job;
    if(!job->complete.load(std::memory_order_acquire)){
        const auto tick=GetTickCount64();
        if(tick<job->completionDueTick){
            if(!SetTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID,static_cast<UINT>(job->completionDueTick-tick),nullptr))
                sgSleepTimeoutStop("observation-timer-failed");
        }else sgSleepTimeoutStop("observation-source-expired");
        return false;
    }
    if(!job->completedTick||job->completedTick>=job->completionDueTick||GetTickCount64()>=job->completionDueTick){
        sgSleepTimeoutStop("observation-source-expired");return false;
    }
    if(job->error||job->veto||!job->entry||
       !ymcc::sleeppower::powerObservationCurrent(&readSharedSystemPower,job->power)||!sgSleepTimeoutObservationCurrent()){
        try{sgRecordFact("sleep-timeout-original-source-rejected",{{"generation",observation.generation},
            {"error",job->error},{"successOrManualVeto",job->veto},{"originalEntry100ns",job->entry},
            {"powerActionAuthorized",false},{"detailLogsRequired",false},{"mandatoryEvidence",true}});}catch(...){}
        sgSleepTimeoutStop("observation-original-unconfirmed-or-vetoed");return false;
    }
    // Feed original Windows evidence through the existing native high-water
    // and refinement path. Neither observed wall time nor PBT becomes Reason1.
    sgRememberKernelSleepEntry(1,job->entry);
    if(observation.classificationSettled)sgSleepTimeoutJoinEntry(1,job->entry);
    else{sgRefreshSleepTriggerOrigin();sgSleepTimeoutArm(1,job->entry);}
    const bool joined=g_sgSleepTimeoutWindow.waiting&&sgSleepTimeoutCurrent();
    // Arm preserves its standalone one-shot contract. This same timer delivery
    // consumes the overdue join immediately, so do not retain that extra tick.
    if(joined&&g_hwnd)KillTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID);
    if(!joined&&(observation.waiting||observation.joined))
        sgSleepTimeoutStop("observation-original-not-current");
    return joined;
}


static void sgSleepTimeoutRetireClassification(const char* reason,unsigned long long generation) {
    sgSleepTimeoutStop(reason);
    if(g_sgTask.generation==generation&&sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::SleepTimeout)
        sgCancelEntryFailureRetry(reason,SgRetryKind::SleepTimeout);
}
static bool sgSleepTimeoutDeferClassification() {
    const auto generation=currentPowerGeneration();
    if(!sgResumeClassificationHeld(generation))return false;
    if(g_sgSleepTimeoutClassificationGeneration!=generation||g_sgSleepTimeoutClassificationOrigin!=g_sgSleepTimeoutWindow.origin){
        if(!ymcc::sleeppower::capturePowerObservation(&readSharedSystemPower,g_sgSleepTimeoutClassificationPower)||
           (g_sgSleepTimeoutClassificationPower.flags&ymcc::sleeppower::kNotificationPending)){
            sgSleepTimeoutRetireClassification("classification-source-revoked",generation);return true;
        }
        g_sgSleepTimeoutClassificationGeneration=generation;g_sgSleepTimeoutClassificationOrigin=g_sgSleepTimeoutWindow.origin;
        try{sgRecordFact("sleep-timeout-classification-deferred",{{"generation",generation},
            {"originalHumanIntent100ns",g_sgSleepTimeoutWindow.origin},
            {"deadlineMs",ymcc::sleeptimeout::Window::delayMs},{"existingClassificationReused",true},
            {"deadlineRestarted",false},{"powerActionAuthorized",false},{"polling",false},
            {"detailLogsRequired",false},{"mandatoryEvidence",true}});}catch(...){}
    }
    return true;
}
// Duplicate resume deliveries and the original classification's Ready return
// may change phase/sequence. Only the existing Native suspend serial/generation,
// pending suspension and monotonic publication can revoke this continuation.
// This observation grants no action; the original sender captures again.
static bool sgSleepTimeoutClassificationPowerCurrent() noexcept {
    ymcc::sleeppower::SharedPowerV1 current;
    return ymcc::sleeppower::capturePowerObservation(&readSharedSystemPower,current)&&
        current.generation==g_sgSleepTimeoutClassificationPower.generation&&
        current.suspendSerial==g_sgSleepTimeoutClassificationPower.suspendSerial&&
        !(current.flags&ymcc::sleeppower::kNotificationPending)&&
        current.publicationRevision>=g_sgSleepTimeoutClassificationPower.publicationRevision;
}
static void sgSleepTimeoutClassificationComplete(unsigned long long generation) noexcept {
    if(!generation||g_sgSleepTimeoutClassificationGeneration!=generation)return;
    const auto origin=g_sgSleepTimeoutClassificationOrigin;
    g_sgSleepTimeoutClassificationGeneration=0;g_sgSleepTimeoutClassificationOrigin=0;
    if(g_sgSleepTimeoutGeneration!=generation||g_sgSleepTimeoutWindow.origin!=origin)return;
    // Return only the existing timer after the classifier retires. All action
    // permission, manual/success and Native current checks remain downstream.
    if(generation!=currentPowerGeneration()||!sgSleepTimeoutCurrent()||
       !sgSleepTimeoutClassificationPowerCurrent()){
        try{sgSleepTimeoutRetireClassification("classification-return-revoked",generation);}catch(...){}
        return;
    }
    const auto now=sgNowFileTime();
    if((g_sgSleepTimeoutWindow.requested||g_sgSleepTimeoutWindow.due(now))&&
       !g_sgSleepTimeoutWindow.canDispatchAt(now)){
        try{sgSleepTimeoutRetireClassification("timeout-dispatch-window-expired",generation);}catch(...){}
        return;
    }
    UINT_PTR timer=0;
    if(g_sgSleepTimeoutWindow.waiting)timer=SG_SLEEP_TIMEOUT_TIMER_ID;
    else if(g_sgSleepTimeoutWindow.requested&&sgSleepRetryActive()&&
            g_sgTask.retryKind==SgRetryKind::SleepTimeout&&g_sgRetryDueTick)timer=SG_RETRY_TIMER_ID;
    if(!timer)return;
    if(!g_hwnd||!SetTimer(g_hwnd,timer,1,nullptr)){
        try{sgSleepTimeoutRetireClassification("classification-return-timer-failed",generation);}catch(...){}
        return;
    }
    try{sgRecordFact("sleep-timeout-classification-returned",{{"generation",generation},
        {"originalHumanIntent100ns",g_sgSleepTimeoutWindow.origin},{"deadlineRestarted",false},
        {"scheduledRetryContinued",timer==SG_RETRY_TIMER_ID},{"powerActionAuthorized",false},
        {"detailLogsRequired",false},{"mandatoryEvidence",true}});}catch(...){}
}

static void sgSleepTimeoutStop(const char* reason) noexcept {
    g_sgSleepTimeoutOriginalNativeBoundary=g_sgSleepTimeoutEvidenceEndTick=g_sgSleepTimeoutEvidenceEntry=0;
    g_sgSleepTimeoutPendingExitTick=g_sgSleepTimeoutPendingSuccessorExit=0;
    sgSleepTimeoutClearReentry();
    sgCancelRetryAdmission(SgRetryKind::SleepTimeout);
    // Retire only this settled original, including a newer Native cycle whose
    // admitted origin is strictly later. Never close at collection/current time.
    const auto settledGeneration=g_sgSleepTimeoutObservation.generation;
    const auto currentGeneration=currentPowerGeneration();
    const auto settledOrigin=g_sgSleepTimeoutObservation.origin;
    const auto nextOrigin=g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire);
    if(g_sgSleepTimeoutObservation.classificationSettled&&settledOrigin&&settledGeneration&&
       (settledGeneration==currentGeneration ||
        (settledGeneration<currentGeneration&&nextOrigin>settledOrigin)))
        sgPublishSleepEventTime(g_sgClosedSleepIntentFileTime,settledOrigin);
    const bool active=g_sgSleepTimeoutWindow.active()||g_sgSleepTimeoutObservation.waiting;
    const auto origin=g_sgSleepTimeoutWindow.active()?g_sgSleepTimeoutWindow.origin:g_sgSleepTimeoutObservation.origin;
    g_sgSleepTimeoutObservation.waiting=false;g_sgSleepTimeoutObservation.joined=false;g_sgSleepTimeoutObservation.classificationSettled=false;
    g_sgSleepTimeoutObservation.job.reset();g_sgSleepTimeoutGeneration=0;g_sgSleepTimeoutClassificationGeneration=0;g_sgSleepTimeoutClassificationOrigin=0;
    g_sgSleepTimeoutWindow.retire();
    if(g_hwnd)KillTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID);
    if(active)try {sgRecordFact("sleep-timeout-stopped",{{"reason",reason?reason:"unknown"},
        {"originalHumanIntent100ns",origin},{"detailLogsRequired",false},{"mandatoryEvidence",true}});}catch(...){}
}
static ULONGLONG sgSleepTimeoutOriginalIntent() {
    return g_sgSleepTimeoutWindow.active()?g_sgSleepTimeoutWindow.origin:0;
}
static bool sgSleepTimeoutCurrent() {
    if(g_sgSleepTimeoutEvidenceEndTick)return false;
    if(g_sgSleepTimeoutGeneration!=currentPowerGeneration())sgSleepTimeoutAdoptReentry();
    if(!sgSleepTimeoutReentryCurrent())return false;
    const auto origin=g_sgSleepTimeoutWindow.origin;
    const auto ownedEntry=g_sgSleepTimeoutAdoptedEntry?g_sgSleepTimeoutAdoptedEntry:origin;
    const auto inherited=g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire);
    return g_sgSleepTimeoutWindow.active()&&origin&&g_sgSleepTimeoutGeneration==currentPowerGeneration()&&g_guardEnabled&&g_sgRetrySleepTimeout&&
        !g_exitRequested.load(std::memory_order_acquire)&&
        // The independent old wake guard may retire its own marker after an
        // early automatic wake. It cannot erase this separately enabled deadline.
        (inherited==0||inherited==origin)&&
        g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire)<=ownedEntry&&
        !sgUserWakeCancelsSleepIntent(origin,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire))&&
        !sgSleepTimeoutSuccessAfter(origin)&&
        (!g_sgSleepTimeoutAdoptedEntry||!sgSleepTimeoutSuccessAfter(g_sgSleepTimeoutAdoptedEntry));
}
static void sgSleepTimeoutResumeDeferredReentry() {
    auto& link=g_sgSleepTimeoutReentry;
    if(!link.publicationDeferred)return;
    if(!sgSleepTimeoutCurrent()){
        if(sgSleepTimeoutDeferReentryPublication())return;
        sgSleepTimeoutStop("deferred-reentry-revoked");return;
    }
    link.publicationDeferred=false;
    if(!g_sgSleepTimeoutWindow.waiting||sgPowerButtonSleepMode()!=SgSleepMode::S3||!sgSleepTimeoutCurrent()){
        sgSleepTimeoutStop("deferred-reentry-setting-veto");return;
    }
    const auto now=sgNowFileTime();
    if(now<g_sgSleepTimeoutWindow.origin){sgSleepTimeoutStop("deferred-reentry-clock-rollback");return;}
    const auto delay=(std::max)(1ULL,static_cast<ULONGLONG>(g_sgSleepTimeoutWindow.remainingMs(now)));
    if(!g_hwnd||!SetTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID,static_cast<UINT>(delay),nullptr))
        sgSleepTimeoutStop("deferred-reentry-timer-failed");
}
static bool sgSleepTimeoutDispatchPermitted() {
    // Read the applicable AC/DC action only at arm/deadline/actual request.
    // Recheck callback-side veto after that OS read and immediately before dispatch.
    return g_sgSleepTimeoutWindow.requested&&g_sgSleepTimeoutWindow.canDispatchAt(sgNowFileTime())&&
        sgSleepTimeoutCurrent()&&sgPowerButtonSleepMode()==SgSleepMode::S3&&sgSleepTimeoutCurrent()&&
        g_sgSleepTimeoutWindow.canDispatchAt(sgNowFileTime());
}
static void sgSleepTimeoutArm(int reason,ULONGLONG original) {
    // Only original physical-button506 Reason1 can arm this independent feature.
    // Programmatic506, lid/idle, screen-off, S4 and unknown actions cannot arm it.
    if(reason!=1||!original||!g_guardEnabled||!g_sgRetrySleepTimeout||
        g_exitRequested.load(std::memory_order_acquire)||
        g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire)!=original||
        g_sgLatestUserSleepEntryFileTime.load(std::memory_order_acquire)!=original||
        sgPowerButtonSleepMode()!=SgSleepMode::S3)return;
    const auto now=sgNowFileTime();
    if(sgUserWakeCancelsSleepIntent(original,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire))||
       sgSleepTimeoutSuccessAfter(original))return;
    if(!g_sgSleepTimeoutWindow.arm(original,now))return;
    sgSleepTimeoutClearReentry();
    g_sgSleepTimeoutGeneration=currentPowerGeneration();
    g_sgSleepTimeoutOriginalNativeBoundary=g_sleepPowerDispatch.observedSleepBoundarySequence();
    g_sgSleepTimeoutEvidenceEndTick=g_sgSleepTimeoutEvidenceEntry=0;
    g_sgSleepTimeoutPendingExitTick=g_sgSleepTimeoutPendingSuccessorExit=0;
    if(g_sgSleepTimeoutObservation.waiting&&g_sgSleepTimeoutObservation.generation==currentPowerGeneration()&&
       original<=g_sgSleepTimeoutObservation.origin&&g_sgSleepTimeoutObservation.origin-original<=SG_S0_ENTRY_FAILURE_WINDOW_MS*10000ULL){
        g_sgSleepTimeoutObservation.waiting=false;g_sgSleepTimeoutObservation.joined=true;g_sgSleepTimeoutObservation.job.reset();
    }
    const auto wait=(std::max)(1ULL,static_cast<ULONGLONG>(g_sgSleepTimeoutWindow.remainingMs(now)));
    if(!g_hwnd||!SetTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID,static_cast<UINT>(wait),nullptr)) {
        sgSleepTimeoutStop("timer-registration-failed");return;
    }
    // REQUIRED ordinary evidence: do not move behind detailed/high-frequency logging.
    // Disk failure cannot grant, repeat or block a power action.
    try {sgRecordFact("sleep-timeout-armed",{{"originalHumanIntent100ns",original},
        {"deadlineMs",ymcc::sleeptimeout::Window::delayMs},{"remainingMs",wait},
        {"policy","user-selected-elapsed-time-fallback"},{"physicalPowerAssessment","UNKNOWN"},
        {"detailLogsRequired",false},{"mandatoryEvidence",true},{"polling",false}});}catch(...){}
}
static bool sgSleepTimeoutSuccessAfter(ULONGLONG original) {
    return original&&(sgSleepTimeoutSuccessfulExitTime()>=original||
        g_sgKernelCycles.entered(original,currentPowerGeneration(),SG_S0_ENTRY_FAILURE_WINDOW_MS));
}
static void sgSleepTimeoutRecordSystemSuccess(ULONGLONG at) noexcept {
    if(!at||at>sgNowFileTime())return;
    auto previous=g_sgKernelSystemWake.load(std::memory_order_acquire);
    while(at>previous&&!g_sgKernelSystemWake.compare_exchange_weak(previous,at,std::memory_order_acq_rel)){}
}
static void sgSleepTimeoutObserveCompleted(const json& evidence,ULONGLONG at,ULONGLONG generation,int reason) noexcept {
    if(at&&at<=sgNowFileTime())g_sgKernelCycles.observeSleepExit(sgKernelSleepExit(evidence,at,generation,reason));
}
static void sgSleepTimeoutPowerBroadcast(ULONG code) {
    if(!g_sgSleepTimeoutWindow.active()&&!g_sgSleepTimeoutObservation.waiting)return;
    const auto origin=g_sgSleepTimeoutWindow.active()?g_sgSleepTimeoutWindow.origin:g_sgSleepTimeoutObservation.origin;
    // S0 interactive broadcasts precede original507 classification. Only
    // confirmed human wake or classic/S4 completion retires this independent
    // deadline; an observation window never refunds or recreates it.
    if(code==PBT_APMRESUMECRITICAL || (code==PBT_APMRESUMESUSPEND &&
       (!sgModernStandbyRepairSupported()||sgAutomaticClassicSleepPending()||
        sgUserWakeCancelsSleepIntent(origin,
            g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)))))
        sgSleepTimeoutStop("interactive-or-critical-resume");
    else if(code==PBT_APMSUSPEND&&(g_sgSleepTimeoutWindow.requested||!sgModernStandbyRepairSupported()))
        sgSleepTimeoutStop("windows-suspend-confirmed");
}
static const char* sgSleepTimeoutEvidenceVeto() {
    const auto origin=g_sgSleepTimeoutWindow.origin;
    if(!g_guardEnabled||!g_sgRetrySleepTimeout||g_exitRequested.load(std::memory_order_acquire))return "settings-or-exit";
    if(g_sgSleepTimeoutGeneration!=currentPowerGeneration()||g_sleepPowerDispatch.pendingSuspend()||
       g_sleepPowerDispatch.observedSleepBoundarySequence()!=g_sgSleepTimeoutOriginalNativeBoundary)return "new-native-suspend";
    if(sgUserWakeCancelsSleepIntent(origin,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)))return "manual-wake";
    if(sgSleepTimeoutSuccessfulExitTime()>=origin)return "hibernate-or-s4";
    const auto inherited=g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire);
    if((inherited&&inherited!=origin)||g_sgLatestUserSleepEntryFileTime.load(std::memory_order_acquire)!=origin||
       g_sgClosedSleepIntentFileTime.load(std::memory_order_acquire)>=origin)return "new-or-closed-human-intent";
    const auto latest=g_sgKernelCycles.latest().exit;
    if(latest.at>=origin&&!latest.continuing)return "final-kernel-exit";
    return nullptr;
}
static bool sgSleepTimeoutWaitForContinuationEvidence(ULONGLONG now) {
    auto& window=g_sgSleepTimeoutWindow;
    if(!window.due(now)||!window.canDispatchAt(now)||g_sgSleepTimeoutAdoptedEntry||
       sgSleepTimeoutEvidenceVeto()||sgPowerButtonSleepMode()!=SgSleepMode::S3||sgSleepTimeoutEvidenceVeto())return false;
    const auto entry=g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire);
    const auto generation=g_sgSleepTimeoutGeneration;
    using State=ymcc::sleeppower::KernelCycleObservations::BriefContinuationState;
    const auto pair=g_sgKernelCycles.briefContinuationState(window.origin,generation,entry);
    if(pair.state==State::Rejected||
       entry!=g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire)||sgSleepTimeoutEvidenceVeto())return false;
    using Policy=ymcc::sleeptimeout::Window;
    const auto remaining=Policy::delayMs+Policy::dispatchGraceMs-(now-window.origin)/Policy::unitsPerMs;
    // Retain only observation until the original grace expires. This mode can never become a sender.
    const auto pending=pair.state==State::AwaitingExit||pair.state==State::AwaitingEntry;
    constexpr ULONGLONG pairRecheckMs=1000;
    const auto interval=pending?(std::min)(static_cast<ULONGLONG>(remaining),pairRecheckMs):remaining;
    if(!g_hwnd||!SetTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID,static_cast<UINT>(interval),nullptr))return false;
    const auto tick=GetTickCount64();
    g_sgSleepTimeoutEvidenceEndTick=tick+remaining;
    g_sgSleepTimeoutPendingExitTick=pending?tick+interval:0;
    g_sgSleepTimeoutPendingSuccessorExit=pair.state==State::AwaitingEntry?pair.tailExit:0;
    g_sgSleepTimeoutEvidenceEntry=entry;
    try {sgRecordFact(pending?"sleep-timeout-continuation-pair-pending":"sleep-timeout-continuation-evidence-wait",{{"originalHumanIntent100ns",window.origin},
        {"originalDeadlineMs",Policy::delayMs},{"expiresAfterMs",remaining},{"latestEntry100ns",entry},
         {"pairRecheckMs",pending?interval:0},{"pendingExit100ns",g_sgSleepTimeoutPendingSuccessorExit},
        {"deadlineRestarted",false},{"powerActionAuthorized",false},{"mandatoryEvidence",true}});}catch(...){}
    return true;
}
static void sgSleepTimeoutTick() {
    if(g_sgSleepTimeoutEvidenceEndTick){
        const auto entry=g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire);
        const auto veto=sgSleepTimeoutEvidenceVeto();
        if(veto)sgSleepTimeoutStop(veto);
        else if(g_sgSleepTimeoutPendingExitTick){
            const auto tick=GetTickCount64(),now=sgNowFileTime();
            const auto origin=g_sgSleepTimeoutWindow.origin;
            const auto pendingExit=g_sgSleepTimeoutPendingSuccessorExit;
            if(!pendingExit&&entry!=g_sgSleepTimeoutEvidenceEntry)sgSleepTimeoutStop("continuation-entry-unattributed");
            else if(now<origin)sgSleepTimeoutStop("clock-rollback");
            else if(tick>=g_sgSleepTimeoutEvidenceEndTick||!g_sgSleepTimeoutWindow.canDispatchAt(now))
                sgSleepTimeoutStop("continuation-evidence-window-expired");
            else if(tick>=g_sgSleepTimeoutPendingExitTick){
                using State=ymcc::sleeppower::KernelCycleObservations::BriefContinuationState;
                const auto pair=g_sgKernelCycles.briefContinuationState(origin,g_sgSleepTimeoutGeneration,entry);
                if(pair.state!=State::Paired||(pendingExit&&pair.tailExit!=pendingExit)||
                   entry!=g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire)||sgSleepTimeoutEvidenceVeto())
                    sgSleepTimeoutStop("continuation-pair-unconfirmed");
                else {
                    using Policy=ymcc::sleeptimeout::Window;
                    const auto remaining=(std::min)(g_sgSleepTimeoutEvidenceEndTick-tick,static_cast<ULONGLONG>(
                        Policy::delayMs+Policy::dispatchGraceMs-(now-origin)/Policy::unitsPerMs));
                    if(!g_hwnd||!SetTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID,static_cast<UINT>(remaining),nullptr))
                        sgSleepTimeoutStop("continuation-evidence-timer-failed");
                    else {
                        g_sgSleepTimeoutPendingExitTick=g_sgSleepTimeoutPendingSuccessorExit=0;
                        g_sgSleepTimeoutEvidenceEntry=entry;
                        try {sgRecordFact("sleep-timeout-continuation-evidence-wait",{{"originalHumanIntent100ns",origin},
                            {"originalDeadlineMs",Policy::delayMs},{"expiresAfterMs",remaining},{"latestEntry100ns",entry},
                            {"pairedAfterDeadline",true},{"pendingExit100ns",pendingExit},{"deadlineRestarted",false},{"powerActionAuthorized",false},
                            {"mandatoryEvidence",true}});}catch(...){}
                    }
                }
            }
        }
        else if(entry!=g_sgSleepTimeoutEvidenceEntry&&
                !g_sgKernelCycles.briefContinuationsFrom(g_sgSleepTimeoutWindow.origin,g_sgSleepTimeoutGeneration,entry))
            sgSleepTimeoutStop("continuation-entry-unattributed");
        else if(sgNowFileTime()<g_sgSleepTimeoutWindow.origin)sgSleepTimeoutStop("clock-rollback");
        else if(GetTickCount64()>=g_sgSleepTimeoutEvidenceEndTick||
                !g_sgSleepTimeoutWindow.canDispatchAt(sgNowFileTime()))
            sgSleepTimeoutStop("continuation-evidence-window-expired");
        else g_sgSleepTimeoutEvidenceEntry=entry;
        return;
    }
    if(g_hwnd)KillTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID);
    if(g_sgSleepTimeoutObservation.waiting&&!sgSleepTimeoutResolveObservation())return;
    if(!g_sgSleepTimeoutWindow.waiting)return;
    if(!sgSleepTimeoutCurrent()||sgPowerButtonSleepMode()!=SgSleepMode::S3||!sgSleepTimeoutCurrent()) {
        if(sgSleepTimeoutDeferReentryPublication())return;
        if(sgSleepTimeoutWaitForContinuationEvidence(sgNowFileTime()))return;
        const auto origin=g_sgSleepTimeoutWindow.origin;
        const auto ownedEntry=g_sgSleepTimeoutAdoptedEntry?g_sgSleepTimeoutAdoptedEntry:origin;
        try {const auto veto=sgSleepTimeoutEvidenceVeto();sgRecordFact("sleep-timeout-deadline-blocked",{
            {"originalHumanIntent100ns",origin},{"newerOriginalEntry",g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire)>ownedEntry},
            {"originalEntryHadSleep",g_sgKernelCycles.entered(origin,currentPowerGeneration(),SG_S0_ENTRY_FAILURE_WINDOW_MS)},
            {"systemWakeAfterIntent",sgSleepTimeoutSuccessfulExitTime()>=origin},
            {"hardVeto",veto?json(veto):json(nullptr)},{"powerActionAuthorized",false},{"mandatoryEvidence",true}});}catch(...){}
        sgSleepTimeoutStop("intent-setting-wake-or-success-veto");return;
    }
    const auto now=sgNowFileTime();
    if(now<g_sgSleepTimeoutWindow.origin){sgSleepTimeoutStop("clock-rollback");return;}
    if(!g_sgSleepTimeoutWindow.due(now)) {
        const auto wait=(std::max)(1ULL,static_cast<ULONGLONG>(g_sgSleepTimeoutWindow.remainingMs(now)));
        if(!g_hwnd||!SetTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID,static_cast<UINT>(wait),nullptr))
            sgSleepTimeoutStop("timer-registration-failed");
        return;
    }
    // An overdue timer can arrive with resume broadcasts. Preserve their
    // existing classification instead of racing a delayed human507.
    if(sgSleepTimeoutDeferClassification())return;
    if(!g_sgSleepTimeoutWindow.canDispatchAt(now)){
        sgSleepTimeoutStop("timeout-dispatch-window-expired");return;
    }
    const auto sleepBoundary=g_sleepPowerDispatch.observedSleepBoundarySequence();
    // The deadline can join an unissued first request, but cannot own a
    // second send. Leave the window for the current Windows lifecycle to retire.
    if(sgSleepRetryActive() && (g_sgTask.retryAttempt!=0 || !g_sgRetryDueTick ||
                               g_sgTask.generation!=currentPowerGeneration())) {
        sgObserveUnattributedSleepRetryFailure("sleep-timeout-240s");
        return;
    }
    if(!g_sgSleepTimeoutWindow.request(now))return;
    try {sgRecordFact("sleep-timeout-due",{{"originalHumanIntent100ns",g_sgSleepTimeoutWindow.origin},
        {"elapsedMs",(now-g_sgSleepTimeoutWindow.origin)/10000},
        {"api","SetSuspendState"},{"arguments",json::array({false,false,false})},
        {"physicalPowerAssessment","UNKNOWN"},{"detailLogsRequired",false},{"mandatoryEvidence",true}});}catch(...){}
    if(!sgSleepTimeoutDispatchPermitted()){sgSleepTimeoutStop("final-deadline-veto");return;}
    // Do not change an existing retry's owner ahead of a newer public Suspend.
    if(g_sleepPowerDispatch.pendingSuspend()||g_sleepPowerDispatch.observedSleepBoundarySequence()!=sleepBoundary){
        sgSleepTimeoutStop("public-sleep-boundary-before-timeout-handoff");return;
    }
    if(sgSleepRetryActive()) {
        g_sgTask.retryKind=SgRetryKind::SleepTimeout;
    } else sgStartSleepRetry(SgRetryKind::SleepTimeout,"sleep-timeout-240s");
}
