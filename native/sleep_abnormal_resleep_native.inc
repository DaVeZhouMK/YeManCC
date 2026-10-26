// Production policy: conservative Windows events; DRIPS is observation only.
// The staged high-power engine below is retained for historical regression,
// with no production Arm/Owned entry, setting, flag or IPC that can start it.
// UI-thread episode; Windows event callbacks only post bounded observations.
// REQUIRED ordinary sleep-facts evidence: source/arm/phase, bounded quality and
// bad-interval aggregates, deadline, request/result, stop. Never gate these on
// g_sgFactMonitorEnabled or remove them as a logging-cost optimization. No raw spam.
static ymcc::abnormalsleep::Episode g_sgAbnormalEpisode;
static std::atomic<ULONGLONG> g_sgAbnormalOrigin{0};
static std::atomic<ULONGLONG> g_sgAbnormalHumanIntent{0};
static ULONGLONG g_sgAbnormalLastHumanIntent=0,g_sgAbnormalLastOwnedRequest=0;
static bool g_sgAbnormalOwnedRetry=false;
static unsigned g_sgAbnormalDispatchStage=4;
static ULONGLONG g_sgAbnormalDispatchOrigin=0,g_sgAbnormalTimerDue=0;
static bool g_sgAbnormalDispatchVetoLogged=false;
static std::atomic<unsigned> g_sgAbnormalPending{0};
static std::atomic<bool> g_sgAbnormalDropped{false},g_sgAbnormalStopRequested{false};
// Retained across cleanup so success delivered during source cleanup still vetoes dispatch.
static std::atomic<ULONGLONG> g_sgAbnormalSuccessOrigin{0},g_sgAbnormalSuccessFileTime{0};
static std::atomic<unsigned> g_sgAbnormalSuccessEventId{0};
static bool g_sgAbnormalDisplayOff=false;
static int g_sgAbnormalQuality=-1;
static ULONGLONG g_sgAbnormalEvidenceLogTick=0;
static unsigned g_sgAbnormalEvidenceEvents=0,g_sgAbnormalEvidenceChanges=0,g_sgAbnormalDeadlineLogged=0;
static json sgAbnormalEvidenceSummary(const ymcc::abnormalsleep::Episode& episode,ULONGLONG now) {
    return {{"phase",sgFormatFactFileTime(episode.origin)},{"originalHumanIntent",sgFormatFactFileTime(episode.originalHumanIntent)},
        {"badBegin",sgFormatFactFileTime(episode.badBegin)},{"badEnd",sgFormatFactFileTime(episode.badEnd)},
        {"continuousBadMs",episode.badEnd>=episode.badBegin?(episode.badEnd-episode.badBegin)/10000:0},
        {"fresh",episode.badEnd&&now>=episode.badEnd&&now-episode.badEnd<=ymcc::abnormalsleep::Episode::freshMs*10000},
        {"generation",episode.generation},{"automaticRequests",g_sgAutomaticSleepBudget.requests()}};
}
struct SgAbnormalEvent {ULONGLONG origin=0,fileTime=0;unsigned id=0;std::string xml;};
#include "sleep_abnormal_etw_native.inc"
static void sgAbnormalStop(const char* reason) {
    // Revoke actions first, but retain the observation identity while ETW
    // drains: Windows success already in its buffers must still veto shutdown.
    g_sgAbnormalStopRequested.store(true,std::memory_order_release);
    const auto origin=g_sgAbnormalOrigin.load(std::memory_order_acquire);
    const auto humanIntent=g_sgAbnormalHumanIntent.load(std::memory_order_acquire);
    const bool had=origin!=0;
    const auto evidence=sgAbnormalEvidenceSummary(g_sgAbnormalEpisode,sgNowFileTime());
    const char* cause=reason;
    if(had&&g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)>=humanIntent)cause="power-button-wake";
    else if(had&&g_sgAbnormalSuccessOrigin.load(std::memory_order_acquire)==origin)cause="windows-effective-low-power";
    else if(had&&g_sgAbnormalDropped.load(std::memory_order_acquire))cause="live-source-gap";
    if(g_hwnd)KillTimer(g_hwnd,SG_ABNORMAL_TIMER_ID);
    g_sgAbnormalTimerDue=0;g_sgAbnormalDispatchStage=4;g_sgAbnormalDispatchOrigin=0;
    const bool sourceClosed=sgAbnormalEtwStop();
    g_sgAbnormalOrigin.store(0,std::memory_order_release);
    if(had&&g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)>=humanIntent)cause="power-button-wake";
    else if(had&&g_sgAbnormalSuccessOrigin.load(std::memory_order_acquire)==origin)cause="windows-effective-low-power";
    else if(!sourceClosed)cause="live-source-cleanup-UNKNOWN";
    else if(had&&g_sgAbnormalDropped.load(std::memory_order_acquire))cause="live-source-gap";
    MSG message{};
    while(g_hwnd&&PeekMessageW(&message,g_hwnd,WM_SG_ABNORMAL_EVIDENCE,WM_SG_ABNORMAL_EVIDENCE,PM_REMOVE)) {
        if(message.lParam){delete reinterpret_cast<SgAbnormalEvent*>(message.lParam);g_sgAbnormalPending.fetch_sub(1);}
    }
    g_sgAbnormalEpisode.cancel();g_sgAbnormalQuality=-1;g_sgAbnormalOwnedRetry=false;
    g_sgAbnormalStopRequested.store(false,std::memory_order_release);
    if(had)sgRecordFact("abnormal-sleep-finished",{{"reason",cause},{"trigger",reason},{"origin",sgFormatFactFileTime(origin)},{"originalHumanIntent",sgFormatFactFileTime(humanIntent)},
        {"evidence",evidence},{"sourceClosed",sourceClosed},{"healthyEventId",g_sgAbnormalSuccessOrigin.load()==origin?g_sgAbnormalSuccessEventId.load():0},
        {"healthyEventTime",sgFormatFactFileTime(g_sgAbnormalSuccessOrigin.load()==origin?g_sgAbnormalSuccessFileTime.load():0)},{"evidenceEventsSinceLog",g_sgAbnormalEvidenceEvents},{"qualityChangesSinceLog",g_sgAbnormalEvidenceChanges}});
}
static void sgAbnormalStopAsync() {
    g_sgAbnormalStopRequested.store(true,std::memory_order_release);
    if(g_hwnd)PostMessageW(g_hwnd,WM_SG_ABNORMAL_EVIDENCE,0,0);
}
static bool sgAbnormalPermitted() {
    const auto origin=g_sgAbnormalOrigin.load(std::memory_order_acquire);
    return origin&&!g_sgAbnormalStopRequested.load(std::memory_order_acquire)&&!g_sgAbnormalDropped.load(std::memory_order_acquire)&&
        g_sgAbnormalSuccessOrigin.load(std::memory_order_acquire)!=origin&&g_guardEnabled&&g_sgRetryEntryFailure&&!g_exitRequested.load()&&
        (!g_sgAbnormalOwnedRetry||g_sgResleepEnabled)&&
        g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)<g_sgAbnormalHumanIntent.load(std::memory_order_acquire);
}
static void sgAbnormalSchedule() {
    if(!g_sgAbnormalEpisode.active()){sgAbnormalStop("completed");return;}
    const auto tick=GetTickCount64(),delay=g_sgAbnormalEpisode.delay(tick);
    if(!g_hwnd||!delay){sgAbnormalStop("timer-unavailable");return;}
    // Only reset the owned timer when its ABSOLUTE target changes. Repeated
    // evidence/UI notifications do not create additional wakeups or rearm it.
    const auto due=tick+delay;
    if(g_sgAbnormalTimerDue==due)return;
    KillTimer(g_hwnd,SG_ABNORMAL_TIMER_ID);
    if(!SetTimer(g_hwnd,SG_ABNORMAL_TIMER_ID,static_cast<UINT>(delay),nullptr)) {
        sgAbnormalStop("timer-unavailable");return;
    }
    g_sgAbnormalTimerDue=due;
}
static bool sgAbnormalDispatchCurrent() {
    using namespace ymcc::abnormalsleep;
    const auto phase=g_powerLifecycle.load(std::memory_order_acquire);
    const auto nowTick=GetTickCount64(),nowFileTime=sgNowFileTime();
    const bool same=g_sgAbnormalDispatchOrigin&&g_sgAbnormalDispatchOrigin==g_sgAbnormalEpisode.origin;
    const auto decision=same?g_sgAbnormalEpisode.dispatch(g_sgAbnormalDispatchStage,nowTick,nowFileTime):Decision{};
    const bool permitted=same&&!g_sgAbnormalPending.load(std::memory_order_acquire)&&
        g_sgAbnormalEpisode.generation==currentPowerGeneration()&&
        (phase==PowerLifecycle::Suspending||phase==PowerLifecycle::Suspended)&&decision.action!=Action::None;
    if(!permitted&&!g_sgAbnormalDispatchVetoLogged) {
        g_sgAbnormalDispatchVetoLogged=true;
        // REQUIRED once-per-selected-stage ordinary evidence; never raw spam.
        sgRecordFact("abnormal-sleep-final-dispatch-veto",{{"stage",g_sgAbnormalDispatchStage},
            {"reason",decision.reason},{"elapsedMs",nowTick>=g_sgAbnormalEpisode.startTick?nowTick-g_sgAbnormalEpisode.startTick:0},
            {"pendingEvidence",g_sgAbnormalPending.load()},{"evidence",sgAbnormalEvidenceSummary(g_sgAbnormalEpisode,nowFileTime)},
            {"mandatoryEvidence",true}});
    }
    // The fact journal may itself block or yield. Never use pre-log time as an
    // action permit. Failure remains failure; success emits no blocking log.
    return sgAbnormalPermitted()&&g_sgAbnormalDisplayOff&&!sgSleepRetryActive()&&permitted;
}
static bool sgAbnormalDispatchReady() {
    // No positive source-stat cache. QUERY may block: current clocks, the
    // selected stage/window, fresh evidence, pending callbacks and human vetoes
    // are all re-read AFTER it, including every OS fallback in main.cpp.
    if(!sgAbnormalEtwHealthy())return false;
    return sgAbnormalDispatchCurrent();
}
static void sgAbnormalTick(bool evidenceEvent=false) {
    // main owns WM_TIMER delivery and kills its timer before this call. Only
    // event-driven evidence callbacks may reuse a still-owned absolute target.
    // An old/early WM_TIMER must reinstall a newer target that main just killed.
    if(!evidenceEvent)g_sgAbnormalTimerDue=0;
    using namespace ymcc::abnormalsleep;
    if(!sgAbnormalPermitted()){sgAbnormalStop("disabled-wake-success-or-source-gap");return;}
    const auto phase=g_powerLifecycle.load(std::memory_order_acquire);
    const bool context=g_sgAbnormalDisplayOff&&!sgSleepRetryActive()&&
        g_sgAbnormalEpisode.generation==currentPowerGeneration()&&
        (phase==PowerLifecycle::Suspending||phase==PowerLifecycle::Suspended);
    if(!context)g_sgAbnormalEpisode.invalidate();
    const auto before=g_sgAbnormalEpisode;
    const auto nowTick=GetTickCount64(),nowFileTime=sgNowFileTime();
    const auto decision=g_sgAbnormalEpisode.poll(nowTick,nowFileTime,true);
    const auto stage=decision.stage<4?decision.stage:before.next;
    if(stage<4&&nowTick>=before.startTick&&nowTick-before.startTick>=Episode::deadlines[stage]&&
        !(g_sgAbnormalDeadlineLogged&(1U<<stage))) {
        g_sgAbnormalDeadlineLogged|=1U<<stage;
        sgRecordFact("abnormal-sleep-deadline",{{"stage",stage},{"deadlineMs",Episode::deadlines[stage]},
            {"elapsedMs",nowTick-before.startTick},{"reason",decision.reason},{"action",static_cast<int>(decision.action)},
            {"contextValid",context},{"evidence",sgAbnormalEvidenceSummary(before,nowFileTime)}});
    }
    if(decision.action==Action::None){
        if(!g_sgAbnormalEpisode.active())sgAbnormalStop(decision.reason);
        else sgAbnormalSchedule();return;
    }
    g_sgAbnormalDispatchStage=decision.stage;g_sgAbnormalDispatchOrigin=g_sgAbnormalEpisode.origin;
    g_sgAbnormalDispatchVetoLogged=false;
    const auto origin=g_sgAbnormalEpisode.origin;
    const auto humanIntent=g_sgAbnormalEpisode.originalHumanIntent;
    const bool ownedRetry=g_sgAbnormalOwnedRetry;
    // The event callback high-water mark is checked again at the final boundary.
    if(!context||!sgAbnormalPermitted()){sgAbnormalStop("dispatch-canceled");return;}
    if(!sgAbnormalDispatchReady()){sgAbnormalStop("final-source-or-human-veto");return;}
    if(decision.action==Action::PowerOff) {
        // Keep the success witness alive during privilege preparation, then end
        // the episode before the API. Every denial/veto/failure is terminal.
        SgSystemSleepRequestResult privilege;
        const bool prepared=sgPrepareSleepPrivilege(privilege);
        const bool finalSourceAndIntent=sgAbnormalDispatchReady();
        const auto terminalEvidence=g_sgAbnormalEpisode;
        sgAbnormalStop("normal-poweroff-attempt");
        const auto terminalDecision=terminalEvidence.dispatch(decision.stage,GetTickCount64(),sgNowFileTime());
        const auto terminalPhase=g_powerLifecycle.load(std::memory_order_acquire);
        bool accepted=false;DWORD error=ERROR_SUCCESS;
        if(!prepared)error=privilege.shutdownPrivilegeError;
        else if(!finalSourceAndIntent||terminalDecision.action!=Action::PowerOff||
            terminalEvidence.generation!=currentPowerGeneration()||!g_sgAbnormalDisplayOff||sgSleepRetryActive()||
            (terminalPhase!=PowerLifecycle::Suspending&&terminalPhase!=PowerLifecycle::Suspended)||
            g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)>=humanIntent||
            g_sgAbnormalSuccessOrigin.load(std::memory_order_acquire)==origin||
            g_sgAbnormalDropped.load(std::memory_order_acquire)||!g_guardEnabled||!g_sgRetryEntryFailure||
            (ownedRetry&&!g_sgResleepEnabled)||g_exitRequested.load())error=ERROR_CANCELLED;
        else {SetLastError(ERROR_SUCCESS);accepted=ExitWindowsEx(EWX_POWEROFF,0)!=FALSE;error=accepted?ERROR_SUCCESS:GetLastError();}
        sgRecordFact("abnormal-sleep-poweroff-result",{{"accepted",accepted},{"error",error},{"force",false},{"terminal",true},
            {"finalReason",terminalDecision.reason},{"evidence",sgAbnormalEvidenceSummary(terminalEvidence,sgNowFileTime())}});
        return;
    }
    if(!g_sgAutomaticSleepBudget.consume()) {sgAbnormalStop("shared-budget-exhausted");return;}
    sgRecordFact("abnormal-sleep-request",{{"stage",decision.stage},{"elapsedMs",GetTickCount64()-g_sgAbnormalEpisode.startTick},
        {"kind",decision.action==Action::Hibernate?"hibernate":"sleep"},{"automaticRequests",g_sgAutomaticSleepBudget.requests()},
        {"evidence",sgAbnormalEvidenceSummary(g_sgAbnormalEpisode,sgNowFileTime())}});
    g_fanPhysicalWake.retryBoundary(currentPowerGeneration(),sgNowFileTime());
    sgMarkInternalSleepRequest(SgRetryKind::EntryFailure);
    const auto marker=g_sgInternalSleepRequest;
    const auto first=decision.action==Action::Hibernate?ymcc::sleeprequest::Mode::Hibernate:ymcc::sleeprequest::Mode::ModernStandby;
    const auto last=decision.action==Action::Hibernate?ymcc::sleeprequest::Mode::Hibernate:ymcc::sleeprequest::Mode::Sleep;
    if(!sgAbnormalDispatchReady()) {
        if(g_sgInternalSleepRequest.kind==marker.kind&&g_sgInternalSleepRequest.dispatchFileTime==marker.dispatchFileTime)
            sgClearInternalSleepRequest("abnormal-final-dispatch-veto");
        sgAbnormalStop("final-source-or-human-veto");return;
    }
    const auto request=sgRequestSystemSleep(first,last,true);
    if(!request.accepted&&g_sgInternalSleepRequest.kind==marker.kind&&
        g_sgInternalSleepRequest.dispatchTick==marker.dispatchTick&&
        g_sgInternalSleepRequest.dispatchFileTime==marker.dispatchFileTime)
        sgClearInternalSleepRequest("abnormal-sleep-request-rejected");
    sgRecordFact("abnormal-sleep-request-result",{{"stage",decision.stage},{"accepted",request.accepted},
        {"error",request.requestError},{"target",ymcc::sleeprequest::name(request.target)}});
    // SetSuspendState may return only after resume; never rearm a canceled episode.
    if(!g_sgAbnormalEpisode.active()||g_sgAbnormalEpisode.origin!=origin)return;
    if(decision.action==Action::Hibernate)g_sgAbnormalEpisode.hibernateResult(request.accepted);
    else g_sgAbnormalEpisode.invalidate();
    if(request.requestError==ERROR_CANCELLED){sgAbnormalStop("request-canceled");return;}
    sgAbnormalSchedule();
}
static void sgAbnormalEtwPublish(unsigned id,const std::string& xml,ULONGLONG at,ULONGLONG origin) {
    if(!origin||origin!=g_sgAbnormalOrigin.load(std::memory_order_acquire))return;
    if(ymcc::abnormalsleep::Episode::healthyForPhase(ymcc::abnormalsleep::decode(id,xml,at),origin,sgNowFileTime())) {
        g_sgAbnormalSuccessFileTime.store(at);g_sgAbnormalSuccessEventId.store(id);
        g_sgAbnormalSuccessOrigin.store(origin,std::memory_order_release);
    }
    if(g_sgAbnormalStopRequested.load(std::memory_order_acquire)&&
        g_sgAbnormalSuccessOrigin.load(std::memory_order_acquire)!=origin)g_sgAbnormalDropped.store(true,std::memory_order_release);
    if(g_sgAbnormalPending.fetch_add(1)>=8){g_sgAbnormalPending.fetch_sub(1);sgAbnormalEtwGap("ui-queue-overflow",ERROR_NOT_ENOUGH_MEMORY);return;}
    try {
        auto item=std::make_unique<SgAbnormalEvent>();item->origin=origin;item->fileTime=at;item->id=id;item->xml=xml;
        if(PostMessageW(g_hwnd,WM_SG_ABNORMAL_EVIDENCE,0,reinterpret_cast<LPARAM>(item.get())))item.release();
        else {g_sgAbnormalPending.fetch_sub(1);sgAbnormalEtwGap("ui-post-failed",GetLastError());}
    } catch(...) {g_sgAbnormalPending.fetch_sub(1);sgAbnormalEtwGap("event-allocation-failed",ERROR_NOT_ENOUGH_MEMORY);}
}
static void sgAbnormalPostCompleted(const std::string& xml,ULONGLONG at) {
    const auto origin=g_sgAbnormalOrigin.load(std::memory_order_acquire);
    if(!origin||at<origin)return;
    const auto now=sgNowFileTime();
    if(ymcc::abnormalsleep::Episode::healthyForPhase(ymcc::abnormalsleep::decode(507,xml,at),origin,now)) {
        g_sgAbnormalSuccessFileTime.store(at);g_sgAbnormalSuccessEventId.store(507);
        g_sgAbnormalSuccessOrigin.store(origin,std::memory_order_release);
    }
    if(g_sgAbnormalPending.fetch_add(1)>=8){g_sgAbnormalPending.fetch_sub(1);g_sgAbnormalDropped.store(true);return;}
    try {
        auto item=std::make_unique<SgAbnormalEvent>();item->origin=origin;item->fileTime=at;item->id=507;item->xml=xml;
        if(PostMessageW(g_hwnd,WM_SG_ABNORMAL_EVIDENCE,0,reinterpret_cast<LPARAM>(item.get())))item.release();
        else {g_sgAbnormalPending.fetch_sub(1);sgAbnormalEtwGap("completed-ui-post-failed",GetLastError());}
    } catch(...) {g_sgAbnormalPending.fetch_sub(1);g_sgAbnormalDropped.store(true);}
}
static void sgAbnormalHandleEvent(LPARAM value) {
    std::unique_ptr<SgAbnormalEvent> event(reinterpret_cast<SgAbnormalEvent*>(value));
    if(event)g_sgAbnormalPending.fetch_sub(1);
    if(!event){if(!sgAbnormalPermitted())sgAbnormalStop("disabled-wake-success-or-source-gap");return;}
    if(event->origin!=g_sgAbnormalOrigin.load(std::memory_order_acquire)||!g_sgAbnormalEpisode.active())return;
    if(!sgAbnormalPermitted()){sgAbnormalStop("disabled-wake-success-or-source-gap");return;}
    if(event->id==554) {
        // Stats are after-the-fact/action evidence, never proof of successful DRIPS.
        // Do not emit per-device snapshots or re-dispatch based on transition counts.
        return;
    }
    const auto observation=ymcc::abnormalsleep::decode(event->id,event->xml,event->fileTime);
    if(observation.quality==ymcc::abnormalsleep::Quality::Healthy) {
        g_sgAbnormalEpisode.observe(observation,sgNowFileTime());
        if(!g_sgAbnormalEpisode.active())sgAbnormalStop("windows-effective-low-power");
        return;
    }
    const bool live=g_sgAbnormalDisplayOff&&g_sgAbnormalEpisode.generation==currentPowerGeneration();
    if(!live){g_sgAbnormalEpisode.invalidate();return;}
    g_sgAbnormalEpisode.observe(observation,sgNowFileTime());
    if(!g_sgAbnormalEpisode.active()){sgAbnormalStop("windows-effective-low-power");return;}
    const auto quality=static_cast<int>(observation.quality);
    if(g_sgAbnormalEvidenceEvents<std::numeric_limits<unsigned>::max())++g_sgAbnormalEvidenceEvents;
    if(quality!=g_sgAbnormalQuality&&g_sgAbnormalEvidenceChanges<std::numeric_limits<unsigned>::max())++g_sgAbnormalEvidenceChanges;
    g_sgAbnormalQuality=quality;
    const auto nowTick=GetTickCount64();
    // First sample, then at most one aggregate per30seconds. Deadline/request
    // records independently retain the precise interval used for a decision.
    if(!g_sgAbnormalEvidenceLogTick||nowTick-g_sgAbnormalEvidenceLogTick>=30000){
        g_sgAbnormalEvidenceLogTick=nowTick;
        sgRecordFact("abnormal-sleep-evidence-state",{{"eventId",event->id},{"quality",quality},
            {"eventFileTime",sgFormatFactFileTime(event->fileTime)},{"beginFileTime",sgFormatFactFileTime(observation.begin)},
            {"events",g_sgAbnormalEvidenceEvents},{"qualityChanges",g_sgAbnormalEvidenceChanges},
            {"evidence",sgAbnormalEvidenceSummary(g_sgAbnormalEpisode,sgNowFileTime())}});
        g_sgAbnormalEvidenceEvents=g_sgAbnormalEvidenceChanges=0;
    }
    // Source events may satisfy a deadline within its bounded dispatch window.
    const auto delay=g_sgAbnormalEpisode.delay(GetTickCount64());
    if(delay&&GetTickCount64()-g_sgAbnormalEpisode.startTick>=ymcc::abnormalsleep::Episode::deadlines[g_sgAbnormalEpisode.next])sgAbnormalTick(true);
}
// Read-only and thread independent: true only while this episode owns a
// successfully created real-time ETW source. No channel/configuration probing.
// Idle means no source; it does not authorize a persistent preflight listener.
static bool sgAbnormalSourceReady() {
    return g_sgAbnormalEtw.ready.load(std::memory_order_acquire)&&
        !g_sgAbnormalDropped.load(std::memory_order_acquire)&&
        g_sgAbnormalOrigin.load(std::memory_order_acquire)!=0;
}
// Historical staged consumer only: the fixture calls this directly. Neither
// production entry below starts a phase; keep all original source/safety gates.
[[maybe_unused]] static bool sgAbnormalStartPhase(ULONGLONG originalHumanIntent,ULONGLONG requestFileTime,bool ownedRetry) {
    sgAbnormalStop("new-bounded-phase");
    if(!g_sgAbnormalEpisode.arm(requestFileTime,GetTickCount64(),sgNowFileTime(),currentPowerGeneration(),originalHumanIntent))return false;
    g_sgAbnormalOwnedRetry=ownedRetry;
    g_sgAbnormalEvidenceLogTick=0;g_sgAbnormalEvidenceEvents=g_sgAbnormalEvidenceChanges=g_sgAbnormalDeadlineLogged=0;
    g_sgAbnormalDropped.store(false,std::memory_order_release);
    g_sgAbnormalHumanIntent.store(originalHumanIntent,std::memory_order_release);
    g_sgAbnormalOrigin.store(requestFileTime,std::memory_order_release);
    if(g_sgAbnormalEtw.session||g_sgAbnormalEtw.worker||g_sgAbnormalEtw.consumer.load()!=INVALID_PROCESSTRACE_HANDLE) {
        sgAbnormalStop("previous-live-source-not-released");return false;
    }
    // Metadata channel16 is not a subscribable EventLog channel. Only an owned,
    // filtered real-time ETW session is a live source; no channel writes/replay.
    if(!sgAbnormalEtwStart(requestFileTime)) {
        sgRecordFact("abnormal-sleep-source-unavailable",{{"reason","episode-etw-unavailable"},
            {"assessment","UNKNOWN"},{"mandatoryEvidence",true}});
        sgAbnormalStop("no-live-source");return false;
    }
    if(!sgAbnormalPermitted()){sgAbnormalStop("source-start-vetoed");return false;}
    sgRecordFact("abnormal-sleep-armed",{{"origin",sgFormatFactFileTime(requestFileTime)},
        {"originalHumanIntent",sgFormatFactFileTime(originalHumanIntent)},{"ownedRetry",ownedRetry},{"sourceReady",true},{"source","episode-etw"},{"mandatoryEvidence",true},
        {"deadlinesMs",ymcc::abnormalsleep::Episode::deadlines},{"detailLogsRequired",false},{"polling",false}});
    sgAbnormalSchedule();return g_sgAbnormalEpisode.active();
}
static bool sgAbnormalConservativeNoticeCurrent(ULONGLONG originalHumanIntent,ULONGLONG requestFileTime) {
    if(!originalHumanIntent||requestFileTime<originalHumanIntent||!g_guardEnabled||!g_sgRetryEntryFailure||
        g_exitRequested.load()||!sgModernStandbyRepairSupported()||
        g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)>=originalHumanIntent)return false;
    const auto now=sgNowFileTime();
    return now>=requestFileTime&&(now-requestFileTime)/10000<=10000;
}
static void sgAbnormalRecordConservativeIntent(ULONGLONG originalHumanIntent,ULONGLONG requestFileTime,const char* trigger) {
    // Reuse the original human-intent high-water mark; no new policy state.
    if(originalHumanIntent<=g_sgAbnormalLastHumanIntent)return;
    g_sgAbnormalLastHumanIntent=originalHumanIntent;
    // REQUIRED ordinary fact once per current original human intent. DRIPS
    // accounting remains information in the existing507 capture. This record
    // neither depends on detail logging nor authorizes any power/source work.
    sgRecordFact("abnormal-sleep-conservative-policy",{{"policy","conservative-windows-events"},
        {"drips","drips-observation-only"},{"highPower","highPower-deferred"},
        {"originalHumanIntent",sgFormatFactFileTime(originalHumanIntent)},
        {"requestFileTime",sgFormatFactFileTime(requestFileTime)},{"trigger",trigger},
        {"automaticHighPowerRepair",false},{"automaticRequests",g_sgAutomaticSleepBudget.requests()},
        {"detailLogsRequired",false},{"mandatoryEvidence",true}});
}
static void sgAbnormalArm(int reason,ULONGLONG eventTime) {
    // Conservative production entry: log the policy, leave Windows/manual and
    // the parent's event-based legacy request untouched. No phase/source/timer.
    if(reason!=1||!sgAbnormalConservativeNoticeCurrent(eventTime,eventTime)||
        eventTime<=g_sgAbnormalLastHumanIntent)return;
    sgAbnormalRecordConservativeIntent(eventTime,eventTime,"power-button-506");
}
// UI thread: AFTER the parent's authorization, shared-budget consume and
// sgMarkInternalSleepRequest(UnexpectedWake), immediately BEFORE its legacy API.
// Always false: no staged observation was started. This result is informational,
// never a veto/replay/refund of the independently authorized legacy request.
static bool sgAbnormalObserveOwnedRetry(ULONGLONG originalHumanIntent,ULONGLONG requestFileTime) {
    if(!sgAbnormalConservativeNoticeCurrent(originalHumanIntent,requestFileTime)||!g_sgResleepEnabled||
        !g_sgAutomaticSleepBudget.requests()||g_sgInternalSleepRequest.kind!=SgRetryKind::UnexpectedWake||
        g_sgInternalSleepRequest.dispatchFileTime!=requestFileTime||requestFileTime<=g_sgAbnormalLastOwnedRequest)return false;
    g_sgAbnormalLastOwnedRequest=requestFileTime;
    sgAbnormalRecordConservativeIntent(originalHumanIntent,requestFileTime,"legacy-owned-retry");
    return false;
}
// UI thread: before any accepted/rejected early return from the parent's API.
// API acceptance and PBT are not effective low-power success witnesses.
static void sgAbnormalOwnedRetryResult(ULONGLONG originalHumanIntent,ULONGLONG requestFileTime,bool accepted) {
    if(!g_sgAbnormalEpisode.active()||g_sgAbnormalEpisode.origin!=requestFileTime||
        g_sgAbnormalEpisode.originalHumanIntent!=originalHumanIntent)return;
    sgRecordFact("abnormal-sleep-owned-retry-result",{{"phase",sgFormatFactFileTime(requestFileTime)},
        {"originalHumanIntent",sgFormatFactFileTime(originalHumanIntent)},{"accepted",accepted},{"effectiveLowPowerConfirmed",false}});
    if(!accepted)sgAbnormalStop("owned-retry-request-rejected");
    else if(!sgAbnormalPermitted())sgAbnormalStop("owned-retry-result-vetoed");
}
static void sgAbnormalInternalEntry() {
    if(g_sgAbnormalEpisode.active()){g_sgAbnormalEpisode.generation=currentPowerGeneration();g_sgAbnormalEpisode.invalidate();}
}
static void sgAbnormalSuspendBoundary() {
    if(g_sgAbnormalEpisode.active()&&g_sgAutomaticSleepTarget.load()==ymcc::sleeprequest::Mode::Hibernate)
        sgAbnormalStop("hibernate-suspend-boundary");
}
static void sgAbnormalDisplay(DWORD state) {
    g_sgAbnormalDisplayOff=state==0;
    // Display-on alone is not a manual wake classification; it only vetoes action.
    if(state!=0)g_sgAbnormalEpisode.invalidate();
}
