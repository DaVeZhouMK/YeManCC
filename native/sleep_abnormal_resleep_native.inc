// UI-thread episode; Windows event callbacks only post bounded observations.
// REQUIRED ordinary sleep-facts evidence: source/arm/phase, bounded quality and
// bad-interval aggregates, deadline, request/result, stop. Never gate these on
// g_sgFactMonitorEnabled or remove them as a logging-cost optimization. No raw spam.
static ymcc::abnormalsleep::Episode g_sgAbnormalEpisode;
static EVT_HANDLE g_sgAbnormalSubscription=nullptr;
static std::atomic<ULONGLONG> g_sgAbnormalOrigin{0};
static std::atomic<ULONGLONG> g_sgAbnormalHumanIntent{0};
static ULONGLONG g_sgAbnormalLastHumanIntent=0,g_sgAbnormalLastOwnedRequest=0;
static bool g_sgAbnormalOwnedRetry=false;
static std::atomic<unsigned> g_sgAbnormalPending{0};
static std::atomic<bool> g_sgAbnormalDropped{false},g_sgAbnormalStopRequested{false};
// Retained across cleanup so success delivered during EvtClose still vetoes dispatch.
static std::atomic<ULONGLONG> g_sgAbnormalSuccessOrigin{0};
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
static void sgAbnormalStop(const char* reason) {
    const auto origin=g_sgAbnormalOrigin.exchange(0,std::memory_order_acq_rel);
    const auto humanIntent=g_sgAbnormalHumanIntent.load(std::memory_order_acquire);
    const bool had=origin!=0;
    const auto evidence=sgAbnormalEvidenceSummary(g_sgAbnormalEpisode,sgNowFileTime());
    const char* cause=reason;
    if(had&&g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)>=humanIntent)cause="power-button-wake";
    else if(had&&g_sgAbnormalSuccessOrigin.load(std::memory_order_acquire)==origin)cause="windows-effective-low-power";
    else if(had&&g_sgAbnormalDropped.load(std::memory_order_acquire))cause="live-source-gap";
    if(g_hwnd)KillTimer(g_hwnd,SG_ABNORMAL_TIMER_ID);
    if(g_sgAbnormalSubscription){EvtClose(g_sgAbnormalSubscription);g_sgAbnormalSubscription=nullptr;}
    MSG message{};
    while(g_hwnd&&PeekMessageW(&message,g_hwnd,WM_SG_ABNORMAL_EVIDENCE,WM_SG_ABNORMAL_EVIDENCE,PM_REMOVE)) {
        if(message.lParam){delete reinterpret_cast<SgAbnormalEvent*>(message.lParam);g_sgAbnormalPending.fetch_sub(1);}
    }
    g_sgAbnormalEpisode.cancel();g_sgAbnormalQuality=-1;g_sgAbnormalOwnedRetry=false;
    g_sgAbnormalStopRequested.store(false,std::memory_order_release);
    if(had)sgRecordFact("abnormal-sleep-finished",{{"reason",cause},{"trigger",reason},{"origin",sgFormatFactFileTime(origin)},
        {"evidence",evidence},{"evidenceEventsSinceLog",g_sgAbnormalEvidenceEvents},{"qualityChangesSinceLog",g_sgAbnormalEvidenceChanges}});
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
    if(g_hwnd)KillTimer(g_hwnd,SG_ABNORMAL_TIMER_ID);
    if(!g_sgAbnormalEpisode.active()){sgAbnormalStop("completed");return;}
    const auto delay=g_sgAbnormalEpisode.delay(GetTickCount64());
    if(!g_hwnd||!delay||!SetTimer(g_hwnd,SG_ABNORMAL_TIMER_ID,static_cast<UINT>(delay),nullptr))
        sgAbnormalStop("timer-unavailable");
}
static void sgAbnormalTick() {
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
    const auto origin=g_sgAbnormalEpisode.origin;
    const auto humanIntent=g_sgAbnormalEpisode.originalHumanIntent;
    const bool ownedRetry=g_sgAbnormalOwnedRetry;
    // The event callback high-water mark is checked again at the final boundary.
    if(!context||!sgAbnormalPermitted()){sgAbnormalStop("dispatch-canceled");return;}
    if(decision.action==Action::PowerOff) {
        // Keep the success witness alive during privilege preparation, then end
        // the episode before the API. Every denial/veto/failure is terminal.
        SgSystemSleepRequestResult privilege;
        const bool prepared=sgPrepareSleepPrivilege(privilege);
        sgAbnormalStop("normal-poweroff-attempt");
        bool accepted=false;DWORD error=ERROR_SUCCESS;
        if(!prepared)error=privilege.shutdownPrivilegeError;
        else if(g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)>=humanIntent||
            g_sgAbnormalSuccessOrigin.load(std::memory_order_acquire)==origin||
            g_sgAbnormalDropped.load(std::memory_order_acquire)||!g_guardEnabled||!g_sgRetryEntryFailure||
            (ownedRetry&&!g_sgResleepEnabled)||g_exitRequested.load())error=ERROR_CANCELLED;
        else {SetLastError(ERROR_SUCCESS);accepted=ExitWindowsEx(EWX_POWEROFF,0)!=FALSE;error=accepted?ERROR_SUCCESS:GetLastError();}
        sgRecordFact("abnormal-sleep-poweroff-result",{{"accepted",accepted},{"error",error},{"force",false},{"terminal",true}});
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
static DWORD WINAPI sgAbnormalEventCallback(EVT_SUBSCRIBE_NOTIFY_ACTION action,PVOID,EVT_HANDLE event) {
    if(action==EvtSubscribeActionError){g_sgAbnormalDropped.store(true,std::memory_order_release);if(g_hwnd)PostMessageW(g_hwnd,WM_SG_ABNORMAL_EVIDENCE,0,0);return ERROR_SUCCESS;}
    if(action!=EvtSubscribeActionDeliver||!event||!g_hwnd)return ERROR_SUCCESS;
    const auto origin=g_sgAbnormalOrigin.load(std::memory_order_acquire);if(!origin)return ERROR_SUCCESS;
    if(g_sgAbnormalPending.fetch_add(1)>=8){g_sgAbnormalPending.fetch_sub(1);g_sgAbnormalDropped.store(true);return ERROR_SUCCESS;}
    try {
        std::string xml;
        if(!sgRenderKernelPowerEvent(event,xml)||xml.size()>65536){g_sgAbnormalPending.fetch_sub(1);g_sgAbnormalDropped.store(true,std::memory_order_release);return ERROR_SUCCESS;}
        const auto id=static_cast<unsigned>(sgKernelPowerEventId(xml));
        if(id!=161&&id!=522&&id!=554){g_sgAbnormalPending.fetch_sub(1);return ERROR_SUCCESS;}
        const auto at=sgKernelPowerEventFileTime(xml);
        const auto now=sgNowFileTime();
        if(ymcc::abnormalsleep::Episode::healthyForPhase(ymcc::abnormalsleep::decode(id,xml,at),origin,now))
            g_sgAbnormalSuccessOrigin.store(origin,std::memory_order_release);
        auto payload=std::make_unique<SgAbnormalEvent>();payload->origin=origin;payload->fileTime=at;payload->id=id;payload->xml=std::move(xml);
        if(PostMessageW(g_hwnd,WM_SG_ABNORMAL_EVIDENCE,0,reinterpret_cast<LPARAM>(payload.get())))payload.release();
        else g_sgAbnormalPending.fetch_sub(1);
    } catch(...) {g_sgAbnormalPending.fetch_sub(1);g_sgAbnormalDropped.store(true);}
    return ERROR_SUCCESS;
}
static void sgAbnormalPostCompleted(const std::string& xml,ULONGLONG at) {
    const auto origin=g_sgAbnormalOrigin.load(std::memory_order_acquire);
    if(!origin||at<origin)return;
    const auto now=sgNowFileTime();
    if(ymcc::abnormalsleep::Episode::healthyForPhase(ymcc::abnormalsleep::decode(507,xml,at),origin,now))
        g_sgAbnormalSuccessOrigin.store(origin,std::memory_order_release);
    if(g_sgAbnormalPending.fetch_add(1)>=8){g_sgAbnormalPending.fetch_sub(1);g_sgAbnormalDropped.store(true);return;}
    try {
        auto item=std::make_unique<SgAbnormalEvent>();item->origin=origin;item->fileTime=at;item->id=507;item->xml=xml;
        if(PostMessageW(g_hwnd,WM_SG_ABNORMAL_EVIDENCE,0,reinterpret_cast<LPARAM>(item.get())))item.release();
        else g_sgAbnormalPending.fetch_sub(1);
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
    if(delay&&GetTickCount64()-g_sgAbnormalEpisode.startTick>=ymcc::abnormalsleep::Episode::deadlines[g_sgAbnormalEpisode.next])sgAbnormalTick();
}
// Read-only and thread independent. The static-wake owner prepares/restores the
// channel once on explicit scan/repair enable (including persisted ON at startup).
// This helper neither writes configuration nor starts a persistent listener.
static bool sgAbnormalSourceReady() {
    const auto config=EvtOpenChannelConfig(nullptr,L"Microsoft-Windows-Kernel-Power/Diagnostic",0);
    EVT_VARIANT enabled{};DWORD used=0;
    const bool available=config&&EvtGetChannelConfigProperty(config,EvtChannelConfigEnabled,0,sizeof(enabled),&enabled,&used)&&(enabled.Type&EVT_VARIANT_TYPE_MASK)==EvtVarTypeBoolean&&enabled.BooleanVal;
    if(config)EvtClose(config);
    return available;
}
static bool sgAbnormalStartPhase(ULONGLONG originalHumanIntent,ULONGLONG requestFileTime,bool ownedRetry) {
    sgAbnormalStop("new-bounded-phase");
    if(!g_sgAbnormalEpisode.arm(requestFileTime,GetTickCount64(),sgNowFileTime(),currentPowerGeneration(),originalHumanIntent))return false;
    g_sgAbnormalOwnedRetry=ownedRetry;
    g_sgAbnormalEvidenceLogTick=0;g_sgAbnormalEvidenceEvents=g_sgAbnormalEvidenceChanges=g_sgAbnormalDeadlineLogged=0;
    g_sgAbnormalDropped.store(false,std::memory_order_release);
    g_sgAbnormalHumanIntent.store(originalHumanIntent,std::memory_order_release);
    g_sgAbnormalOrigin.store(requestFileTime,std::memory_order_release);
    if(!sgAbnormalSourceReady()) {
        sgRecordFact("abnormal-sleep-source-unavailable",{{"reason","diagnostic-channel-disabled-or-unreadable"},
            {"sourcePreparation","static-wake-owner"},{"assessment","UNKNOWN"}});
        sgAbnormalStop("no-live-source");return false;
    }
    g_sgAbnormalSubscription=EvtSubscribe(nullptr,nullptr,L"Microsoft-Windows-Kernel-Power/Diagnostic",
        L"*[System[Provider[@Name='Microsoft-Windows-Kernel-Power'] and (EventID=161 or EventID=522 or EventID=554)]]",
        nullptr,nullptr,sgAbnormalEventCallback,EvtSubscribeToFutureEvents);
    if(!g_sgAbnormalSubscription){const auto error=GetLastError();sgRecordFact("abnormal-sleep-source-unavailable",{{"error",error},{"assessment","UNKNOWN"}});sgAbnormalStop("subscription-unavailable");return false;}
    sgRecordFact("abnormal-sleep-armed",{{"origin",sgFormatFactFileTime(requestFileTime)},
        {"originalHumanIntent",sgFormatFactFileTime(originalHumanIntent)},{"ownedRetry",ownedRetry},{"sourceReady",true},
        {"deadlinesMs",ymcc::abnormalsleep::Episode::deadlines},{"detailLogsRequired",false},{"polling",false}});
    sgAbnormalSchedule();return g_sgAbnormalEpisode.active();
}
static void sgAbnormalArm(int reason,ULONGLONG eventTime) {
    // Only a fresh confirmed physical intent starts the original phase.
    if(reason!=1||!g_guardEnabled||!g_sgRetryEntryFailure||g_exitRequested.load()||!sgModernStandbyRepairSupported())return;
    if(eventTime<=g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)||eventTime<=g_sgAbnormalLastHumanIntent)return;
    g_sgAbnormalLastHumanIntent=eventTime;
    sgAbnormalStartPhase(eventTime,eventTime,false);
}
// UI thread: AFTER the parent's authorization, shared-budget consume and
// sgMarkInternalSleepRequest(UnexpectedWake), immediately BEFORE its power API.
// false means observation is unavailable; it never authorizes/replays that API.
static bool sgAbnormalObserveOwnedRetry(ULONGLONG originalHumanIntent,ULONGLONG requestFileTime) {
    if(!originalHumanIntent||requestFileTime<originalHumanIntent||!g_guardEnabled||!g_sgRetryEntryFailure||
        !g_sgResleepEnabled||g_exitRequested.load()||!sgModernStandbyRepairSupported()||
        g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)>=originalHumanIntent||
        !g_sgAutomaticSleepBudget.requests()||g_sgInternalSleepRequest.kind!=SgRetryKind::UnexpectedWake||
        g_sgInternalSleepRequest.dispatchFileTime!=requestFileTime)return false;
    if(requestFileTime<=g_sgAbnormalLastOwnedRequest)
        return g_sgAbnormalEpisode.active()&&g_sgAbnormalEpisode.origin==requestFileTime&&g_sgAbnormalEpisode.originalHumanIntent==originalHumanIntent;
    g_sgAbnormalLastOwnedRequest=requestFileTime;
    g_sgAbnormalLastHumanIntent=(std::max)(g_sgAbnormalLastHumanIntent,originalHumanIntent);
    return sgAbnormalStartPhase(originalHumanIntent,requestFileTime,true);
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
