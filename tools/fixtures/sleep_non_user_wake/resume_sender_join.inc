void joinArmOriginalField(){
 baseCycle(true,false);joinInjectSuspendOnDispatch=false;const auto origin=nowFile;
 at(13000);originalEntry(origin);resumeAutomatic();resumeInteractive();drainActualWake();handle506(1,origin);
 at(14000);const auto exit=origin+3675033ULL*10ULL;g_sgLastKernel507EventFileTime=exit;handle507Native(32,exit);
 check(sgSleepRetryActive()&&timerDue&&frozen&&!requestCount,"joined original field failed to arm one original retry");
}
void joinPendingDuringActualSender(){
 joinArmOriginalField();const auto originalGeneration=generation;
 joinInjectSuspendOnDispatch=true;dispatch();
 check(requestCount==0,"new PBT during actual sender still reached system sleep terminal");
 check(g_sleepPowerDispatch.pendingSuspend()&&g_sensorSuspendCallbackActive,"actual callback did not retain Native pending and sensor pause");
 check(frozen&&generation==originalGeneration,"pending Windows suspend must retain exact paused lease");
 check(!sgSleepRetryActive(),"canceled sender retained a scheduled retry");
 passed("joined-field-new-pbt-inside-actual-sender");
}
void joinPendingBeforeOldTimer(){
 joinArmOriginalField();const auto old=generation;
 suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);dispatch();
 check(requestCount==0&&frozen,"old scheduled timer crossed pending Native suspend or returned paused lease");
 const auto pending=g_sleepPowerDispatch.pendingReplay();check(bool(pending),"new suspend has no original delivery receipt");
 const auto claim=g_sleepPowerDispatch.forwarded(pending.sequence,pending.type,nowTick);
 check(bool(claim),"new suspend receipt cannot be claimed");handleSuspendAdmission(PBT_APMSUSPEND,&claim);g_sleepPowerDispatch.complete(claim,nowTick);
 check(!g_sleepPowerDispatch.pendingSuspend(),"current suspend receipt completion stranded pending state");
 at(nowTick+60000);g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;
 handle507Native(1,nowFile);resumeAutomatic();resumeInteractive();drainActualWake();
 check(!requestCount&&!frozen&&!classificationTimerDue,"new Windows cycle could not complete normal exact-lease return");
 check(generation>=old&&resumeCount==1,"normal successor duplicated or missed one exact lease return");
 passed("joined-old-timer-new-suspend-full-return");
}
void joinLongReason1Control(){
 baseCycle(true,false);joinInjectSuspendOnDispatch=false;const auto original=nowFile;
 at(10000+1348983);originalEntry(original);
 const auto exit=original+1348983184ULL*10ULL;
 g_sgLastPowerButtonWakeEventFileTime=exit;g_sgOriginalPowerButtonWakeFileTime=exit;
 resumeAutomatic();resumeInteractive();drainActualWake();handle506(1,original);
 g_sgLastKernel507EventFileTime=exit;handle507Native(1,exit);drainActualWake();
 check(!requestCount&&!sgSleepRetryActive()&&!classificationTimerDue,"long Reason1 entered automatic resleep or quick observation");
 check(!frozen&&resumeCount==1,"long manual wake lost exact lease return");
 // ROG002519: the real Reason1 handler retires intent before classification
 // completion. Its ordinary receipt must retain the exact accepted witness.
 baseCycle(true,false);const auto fieldOrigin=nowFile,fieldGeneration=generation;
 originalEntry(fieldOrigin);handle506(1,fieldOrigin);
 at(nowTick+27174);resumeAutomatic();resumeInteractive();drainActualWake();
 check(classificationTimerDue&&g_sgResumeClassification.origin==fieldOrigin,
       "manual receipt: original classification witness was not established");
 check(g_sgAutomaticSleepBudget.consume()&&g_sgAutomaticSleepBudget.consume(),
       "manual receipt: pre-existing request account could not be charged");
 const auto fieldWake=nowFile;g_sgLastPowerButtonWakeEventFileTime=fieldWake;
 g_sgOriginalPowerButtonWakeFileTime=fieldWake;g_sgLastKernel507EventFileTime=fieldWake;
 handle507Native(1,fieldWake);drainActualWake();
 const auto receipt=[&]() -> const json& {
  const json* value=nullptr;unsigned count=0;
  for(const auto& fact:facts)if(fact.first=="resume-classification-complete"){value=&fact.second;++count;}
  check(count==1&&value,"manual receipt: expected one original completion record");return *value;
 };
 const auto& human=receipt();
 check(g_sgLastPowerButtonSleepIntentFileTime.load()==0&&human.value("confirmedUserWake",false),
       "manual receipt: retired original intent mislabeled confirmed power-button wake");
 check(human.at("originalHumanIntent100ns")==fieldOrigin&&human.at("powerButtonWake100ns")==fieldWake,
       "manual receipt: completion lost original intent or exact wake evidence");
 check(!requestCount&&!sgSleepRetryActive()&&!classificationTimerDue&&!timeoutTimerDue&&!timerDue&&
       !frozen&&resumeCount==1&&generation==fieldGeneration&&g_sgAutomaticSleepBudget.requests()==0,
       "manual receipt: diagnostic correction changed cancellation, owner return or request account");
 // The parsed message time is zero when missing even when the callback
 // published a nonzero collection-time fallback. Keep that absence explicit.
 for(const std::string mode:{"missing-original","later-callback","future-original"}){
  baseCycle(false,false);const auto original=nowFile;originalEntry(original);handle506(1,original);
  at(nowTick+2000);resumeAutomatic();drainActualWake();
  check(classificationTimerDue,"manual receipt: parsed-message control did not establish classifier");
  const auto eventTime=mode=="missing-original"?0ULL:mode=="future-original"?nowFile+10000:nowFile-10000;
  g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=mode=="missing-original"?0:nowFile;
  handle507Native(1,eventTime);drainActualWake();const auto& exact=receipt();
  check(exact.value("confirmedUserWake",false)==(mode=="later-callback"),
        "manual receipt: missing/future original or later callback replaced this message witness");
  check(exact.at("powerButtonWake100ns")==eventTime,
        "manual receipt: original GUI message time was replaced with callback collection time");
  check(!requestCount&&!sgSleepRetryActive()&&!classificationTimerDue,
        "manual receipt: missing original diagnostic changed accepted human cancellation");
 }
 // Reason text alone, missing/old evidence, or a different current intent
 // cannot manufacture a confirmed human wake or refund the request account.
 for(const std::string mode:{"missing","old","unknown-reason","new-intent"}){
  baseCycle(false,false);const auto original=nowFile;originalEntry(original);handle506(1,original);
  at(nowTick+2000);resumeAutomatic();drainActualWake();
  check(classificationTimerDue&&g_sgResumeClassification.origin==original,
        "manual receipt: negative control lost original classifier");
  sgClearUnexpectedWake("fixture-retired-original",true);
  const auto observed=mode=="missing"?0ULL:mode=="old"?original-1:nowFile;
  g_sgLastPowerButtonWakeEventFileTime=observed;g_sgOriginalPowerButtonWakeFileTime=observed;
  if(mode=="new-intent"){g_sgLastPowerButtonSleepIntentFileTime=nowFile+1;originalEntry(nowFile+1);}
  check(g_sgAutomaticSleepBudget.consume(),"manual receipt: negative request charge failed");
  sgFinishResumeClassification(mode=="unknown-reason"?"no-entry-failure-evidence":"power-button-wake");
  const auto& rejected=receipt();
  check(!rejected.value("confirmedUserWake",true),"manual receipt: reason/missing/older/foreign intent fabricated human proof");
  check(rejected.at("originalHumanIntent100ns")==original&&rejected.at("powerButtonWake100ns")==0&&rejected.at("observedPowerButtonWake100ns")==observed,
        "manual receipt: negative evidence values were replaced");
  check(!requestCount&&g_sgAutomaticSleepBudget.requests()==1,
        "manual receipt: diagnostic-only branch dispatched or refunded a request");
 }
 passed("joined-original-long-residency-reason1");
}
void joinConsumedSuspendBeforeOldTimer(){
 joinArmOriginalField();const auto old=generation,due=timerDue;const auto spent=g_sgAutomaticSleepBudget.requests();
 suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
 const auto pending=g_sleepPowerDispatch.pendingReplay();check(bool(pending),"consumed suspend lost original receipt");
 const auto claim=g_sleepPowerDispatch.forwarded(pending.sequence,pending.type,nowTick);check(bool(claim),"consumed suspend cannot claim original receipt");
 handleSuspendAdmission(PBT_APMSUSPEND,&claim);g_sleepPowerDispatch.complete(claim,nowTick);
 check(!g_sleepPowerDispatch.pendingSuspend()&&!sgSleepRetryActive()&&!timerDue,"completed new suspend retained old scheduled attempt");
 check(frozen&&generation==old&&!requestCount,"consumed suspend lost exact paused lease or sent another sleep");
 at(due);sgDispatchSameModeRetry();
 check(!requestCount&&g_sgAutomaticSleepBudget.requests()==spent,"already-queued old timer dispatched or spent after consumed suspend");
 at(nowTick+60000);g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;
 handle507Native(1,nowFile);resumeAutomatic();resumeInteractive();drainActualWake();
 check(!frozen&&resumeCount==1&&!classificationTimerDue&&!sgSleepRetryActive(),"consumed suspend successor failed exact lease return");
 sgDispatchSameModeRetry();check(!requestCount,"retired old timer dispatched after normal successor return");
 passed("joined-consumed-suspend-before-old-timer-full-return");
}
void joinDuplicateResumeBeforeRetry(){
 joinArmOriginalField();const auto old=generation,due=timerDue,origin=g_sgLastPowerButtonSleepIntentFileTime.load();
 const auto spent=g_sgAutomaticSleepBudget.requests(),attempt=g_sgTask.retryAttempt;
 resumeAutomatic();resumeInteractive();drainActualWake();
 check(sgSleepRetryActive()&&timerDue==due&&g_sgTask.retryAttempt==attempt,"duplicate resume replaced original scheduled attempt or deadline");
 check(generation==old&&g_sgTask.generation==old&&g_sgLastPowerButtonSleepIntentFileTime.load()==origin&&frozen,"duplicate resume replaced original intent or lease");
 check(!requestCount&&g_sgAutomaticSleepBudget.requests()==spent,"duplicate resume sent sleep or changed original action account");
 dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==spent+1,"current original retry did not send and charge exactly once");
 sgDispatchSameModeRetry();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==spent+1,"late duplicate timer replayed accepted request");
 passed("joined-duplicate-resume-before-retry-keeps-original-attempt");
}

// Same joined consumer, ROG 193736: resources return before the late evidence.
// Windows SleepEntered=false is field provenance, not a second model/predicate.
ULONGLONG joinLateEntryAfterClassification(){
 baseCycle(true,false);joinInjectSuspendOnDispatch=joinInjectManualOnDispatch=false;
 const auto origin=nowFile,start=nowTick,gen=generation,attempt=g_sgSleepTriggerTick;
 originalEntry(origin);handle506(1,origin);
 check(generation==gen&&g_sgTask.generation==gen&&frozen,"ROG193736: early original506 changed original sleep owner");
 at(start+1375);resumeAutomatic();resumeInteractive();drainActualWake();
 check(classificationTimerDue==start+3375&&frozen&&!requestCount,"ROG193736: first receipt did not own original two-second classification");
 at(classificationTimerDue);sgResumeClassificationTick();drainActualWake();
 check(!classificationTimerDue&&!sgSleepRetryActive()&&!timerDue&&!monitorTimerDue&&!requestCount,
  "ROG193736: classification expiry dispatched or retained active work before evidence");
 check(!frozen&&resumeCount==1&&g_sgTask.generation==0&&g_sgTask.mode==SgSleepMode::Unknown,
  "ROG193736: normal classification did not return the exact game lease and task");
 check(generation==gen&&g_powerLifecycle==PowerLifecycle::Ready&&sharedSleepWakeClassified(gen),
  "ROG193736: observation blocked ordinary software/input Ready or changed power generation");
 check(attempt==start&&g_sgLastPowerButtonSleepIntentFileTime.load()==origin,
  "ROG193736: original entry identity was replaced before delayed evidence");
 return origin;
}
void joinLateEntryDeliver507(ULONGLONG origin){
 const auto stamp=origin+4398478ULL*10ULL;
 g_sgLastKernel507EventFileTime=stamp;handle507Native(32,static_cast<LPARAM>(stamp));
}
void joinLateEntryCheckScheduled(ULONGLONG origin,const char* failure){
 check(sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::EntryFailure&&timerDue,failure);
 check(g_sgTask.generation==generation&&g_sgTask.mode==SgSleepMode::S3&&g_sgTask.retryAttempt==0,
  "ROG193736: late evidence did not join the original unissued EntryFailure attempt");
 check(g_sgSleepTriggerTick==(origin-epoch)/10000ULL&&
  g_entryFailureQuickDeadline==origin+SG_S0_ENTRY_FAILURE_WINDOW_MS*10000ULL,
  "ROG193736: late evidence restarted the original trigger or thirty-second deadline");
 check(timerDue==nowTick+SG_NON_USER_WAKE_CLASSIFY_MS&&g_sgRetryDueTick==timerDue&&!requestCount,
  "ROG193736: late evidence bypassed/refunded original expanded-exit scheduler grace");
 check(!frozen&&resumeCount==1&&!classificationTimerDue,
  "ROG193736: late admission re-froze or returned the already recovered game lease");
}
void joinLateEntryField(){
 const auto origin=joinLateEntryAfterClassification(),gen=generation,start=(origin-epoch)/10000ULL;
 at(start+4414);sgObserveEntryFailureDisplay(1,"rog193736-session-on");
 at(start+5415);joinLateEntryDeliver507(origin);
 joinLateEntryCheckScheduled(origin,"ROG193736: late507 after completed classifier did not rejoin original EntryFailure");
 check(g_sgAutomaticSleepBudget.requests()==0&&generation==gen,"ROG193736: evidence consumed a request or changed generation");
 dispatch();drainActualWake();
 // joinSuspend executes the actual sender and rejects any non-FALSE argument.
 check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1&&generation==gen,
  "ROG193736: original actual tripleFALSE sender did not send and charge exactly once");
 sgDispatchSameModeRetry();check(requestCount==1,"ROG193736: queued timer replayed accepted sleep");
 passed("joined-rog193736-classifier-complete-late507-original-retry");
}
static void joinMonitorSleepIngress(bool duringJournal){
 const auto origin=joinLateEntryAfterClassification(),start=(origin-epoch)/10000ULL,gen=generation;
 at(start+4414);sgObserveEntryFailureDisplay(1,"monitor-original-on");
 const auto due=monitorTimerDue;
 check(due==start+9414&&g_monitorEntryFailureTimerArmed,"monitor ingress: original deadline missing");
 at(due);
 const auto publish=[] {suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);};
 if(duringJournal){
  joinCleanupFactEvent="monitor-entry-failure-timeout";joinCleanupFactSource.clear();
  joinCleanupFactFired=false;joinCleanupFactHook=publish;
 }else publish();
 const auto active=g_sgModernStandbyActive,classified=g_sgModernWakeClassified;
 const auto classifiedTick=g_sgModernWakeClassifiedTick,interrupted=g_entryFailureInterruptedAt;
 const auto expanded=g_entryFailureExpanded;
 const auto deadline=g_entryFailureQuickDeadline;
 const auto spent=g_sgAutomaticSleepBudget.requests();
 sgMonitorEntryFailureTick();
 check((!duringJournal||joinCleanupFactFired)&&g_sleepPowerDispatch.pendingSuspend()&&generation==gen,
  "monitor ingress: real Suspend callback did not precede window delivery");
 check(!g_monitorEntryFailureTimerArmed&&!monitorTimerDue&&!timerDue&&!requestCount&&!sgSleepRetryActive()&&
  g_sgAutomaticSleepBudget.requests()==spent,"monitor ingress: stale timer scheduled or charged retry");
 check(g_sgModernStandbyActive==active&&g_sgModernWakeClassified==classified&&
  g_sgModernWakeClassifiedTick==classifiedTick&&g_entryFailureInterruptedAt==interrupted&&
  g_entryFailureQuickDeadline==deadline&&g_entryFailureExpanded==expanded,
  "monitor ingress: stale timeout published classification after newer Suspend");
 check(!frozen&&resumeCount==1,"monitor ingress: old timeout touched returned lease");
}
void joinLateEntryDisplay(){
 const auto origin=joinLateEntryAfterClassification(),start=(origin-epoch)/10000ULL,gen=generation;
 at(start+4414);sgObserveEntryFailureDisplay(1,"rog193736-display-only-on");
 const auto due=monitorTimerDue;
 check(due==start+9414&&g_monitorEntryFailureTimerArmed,
  "ROG193736: late display after completed classifier did not arm original five-second monitor");
 check(!frozen&&resumeCount==1&&g_sgTask.generation==0&&!requestCount&&!timerDue&&!classificationTimerDue,
  "ROG193736: display observation rebuilt a task or held game resources before confirmation");
 at(due-1);sgMonitorEntryFailureTick();
 check(monitorTimerDue==due&&!sgSleepRetryActive()&&!requestCount,
  "ROG193736: pure display shortened/refunded original five-second confirmation");
 at(due);sgMonitorEntryFailureTick();
 joinLateEntryCheckScheduled(origin,"ROG193736: original display confirmation did not rejoin EntryFailure");
 check(generation==gen&&g_sgAutomaticSleepBudget.requests()==0,"ROG193736: display confirmation spent or replaced original account");
 dispatch();drainActualWake();
 check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1&&generation==gen,
  "ROG193736: pure display did not reach actual tripleFALSE sender exactly once");
 joinMonitorSleepIngress(false);joinMonitorSleepIngress(true);
 passed("joined-late-display-after-classifier-original-five-second-confirmation");
}
void joinLateEntryVeto(const std::string& kind){
 // Two real OFF paths deliberately share one parameter selection. Neither
 // fixture path clears/recreates the new bool ticket or restores a task.
 const unsigned branches=kind=="settings-off-on"?2U:1U;
 for(unsigned branch=0;branch<branches;++branch){
  const auto origin=joinLateEntryAfterClassification(),start=(origin-epoch)/10000ULL,gen=generation;
  const auto spent=g_sgAutomaticSleepBudget.requests();ULONGLONG oldMonitor=0;
  at(start+4000);
  if(kind=="manual-before-gui"){
   g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;
  }else if(kind=="success")sgSleepTimeoutRecordSystemSuccess(nowFile);
  else if(kind=="settings-off-on"){
   if(branch==0){
    g_sgRetryEntryFailure=false;sgCancelEntryFailureRetry("entry-failure-setting-disabled");g_sgRetryEntryFailure=true;
   }else{
    g_guardEnabled=false;sgFinishResumeClassification("guard-disabled");g_guardEnabled=true;
   }
  }else if(kind=="new-suspend"){
   suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
   check(g_sleepPowerDispatch.pendingSuspend(),"ROG193736: new-suspend control lacks real Native pending receipt");
  }else if(kind=="new506"){
   at(start+5000);originalEntry(nowFile);handle506(1,nowFile);
   check(generation>gen&&g_sgLastPowerButtonSleepIntentFileTime.load()>origin,
    "ROG193736: newer original506 did not establish its independent current owner");
  }else if(kind=="expired")at(start+SG_S0_ENTRY_FAILURE_WINDOW_MS+1);
  else if(kind!="display-off-on")throw std::runtime_error("unknown-late-entry-veto-parameter");
  at((std::max)(nowTick,start+4414));sgObserveEntryFailureDisplay(1,"rog193736-veto-session-on");
  if(kind=="display-off-on"){
   oldMonitor=monitorTimerDue;check(oldMonitor==start+9414,"ROG193736: display OFF control never owned original monitor");
   at(start+4500);sgObserveEntryFailureDisplay(0,"rog193736-real-late-off");
   at(start+4600);sgObserveEntryFailureDisplay(1,"rog193736-after-real-late-off");
  }
  check(!monitorTimerDue&&!g_monitorEntryFailureTimerArmed&&!timerDue&&!sgSleepRetryActive(),
   "ROG193736: veto/disable/new-boundary/expiry retained or revived late display permission");
  at((std::max)(nowTick,start+5415));joinLateEntryDeliver507(origin);drainActualWake();
  sgResumeClassificationTick();sgMonitorEntryFailureTick();sgDispatchSameModeRetry();drainActualWake();
  if(oldMonitor){at(oldMonitor);sgMonitorEntryFailureTick();sgDispatchSameModeRetry();}
  check(!requestCount&&!sgSleepRetryActive()&&!timerDue&&!monitorTimerDue&&g_sgAutomaticSleepBudget.requests()==spent,
   "ROG193736: late507/queued timer crossed original veto or changed cumulative budget");
  if(kind!="new506")check(generation==gen,"ROG193736: veto allocated a replacement sleep generation");
  if(kind!="new-suspend"&&kind!="new506")check(!frozen&&resumeCount==1,
   "ROG193736: veto touched the already returned exact game lease");
  if(kind=="manual-before-gui"){
   // Deliver the human GUI event only after the adverse evidence was rejected.
   handle507Native(1,static_cast<LPARAM>(g_sgOriginalPowerButtonWakeFileTime.load()));drainActualWake();
   check(!requestCount&&!sgSleepRetryActive()&&!timerDue&&!frozen&&resumeCount==1,
    "ROG193736: delayed human GUI wake caused autonomous sleep or duplicate resource recovery");
  }
 }
 passed("joined-late-entry-veto/"+kind);
}
void joinLateEntryDuplicates(){
 const auto origin=joinLateEntryAfterClassification(),start=(origin-epoch)/10000ULL,gen=generation;
 check(g_sgAutomaticSleepBudget.consume()&&g_sgAutomaticSleepBudget.consume(),"ROG193736: duplicate control cannot retain two prior spent slots");
 at(start+4414);sgObserveEntryFailureDisplay(1,"rog193736-duplicate-on");
 const auto monitor=monitorTimerDue;
 check(monitor==start+9414,"ROG193736: duplicate control lacks original display deadline");
 at(start+4415);sgObserveEntryFailureDisplay(1,"rog193736-duplicate-on");
 check(monitorTimerDue==monitor&&g_sgAutomaticSleepBudget.requests()==2,
  "ROG193736: duplicate ON extended confirmation or refunded previous requests");
 at(start+5415);joinLateEntryDeliver507(origin);
 joinLateEntryCheckScheduled(origin,"ROG193736: duplicate control did not admit one original late retry");
 const auto due=timerDue,deadline=g_entryFailureQuickDeadline,attempt=g_sgSleepTriggerTick;
 for(unsigned i=0;i<3;++i){
  at(start+5416+i);resumeAutomatic();resumeInteractive();drainActualWake();
  handle506(1,origin);joinLateEntryDeliver507(origin);sgObserveEntryFailureDisplay(1,"rog193736-duplicate-evidence");
  sgDispatchSameModeRetry();
  check(timerDue==due&&g_sgRetryDueTick==due&&g_sgTask.retryAttempt==0&&g_entryFailureQuickDeadline==deadline,
   "ROG193736: duplicate receipt/506/507/display extended scheduled retry or advanced its attempt");
  check(generation==gen&&g_sgSleepTriggerTick==attempt&&g_sgLastPowerButtonSleepIntentFileTime.load()==origin&&
   !requestCount&&g_sgAutomaticSleepBudget.requests()==2,
   "ROG193736: duplicate late evidence replaced original identity or refunded/spent a request");
 }
 dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==3,"ROG193736: retained final third slot did not send exactly once");
 const auto acceptedStamp=g_sgInternalSleepRequest.dispatchFileTime;const auto acceptedAttempt=g_sgTask.retryAttempt;const auto acceptedTrigger=g_sgSleepTriggerTick;
 for(unsigned i=0;i<3;++i){
  at(nowTick+1);joinLateEntryDeliver507(origin);resumeAutomatic();resumeInteractive();drainActualWake();
  sgObserveEntryFailureDisplay(1,"rog193736-after-accepted-send");sgResumeClassificationTick();sgMonitorEntryFailureTick();sgDispatchSameModeRetry();
  check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==3&&g_sgRetryDueTick==0&&
   g_sgTask.retryAttempt==acceptedAttempt&&g_sgInternalSleepRequest.dispatchFileTime==acceptedStamp,
   "ROG193736: late old event advanced/replayed accepted retry or refunded exhausted account");
  check(generation==gen&&g_sgSleepTriggerTick==acceptedTrigger&&g_entryFailureQuickDeadline==deadline&&
   g_sgLastPowerButtonSleepIntentFileTime.load()==origin&&!timerDue,
   "ROG193736: post-send duplicate extended original window or replaced current action");
 }
 passed("joined-late-entry-duplicates-preserve-origin-deadlines-last-third-slot");
}


// Controlled terminal interleavings in the same actual joined consumer.
// These values are expected observations/injection inputs, never power facts.
struct JoinRepairContext {
 ULONGLONG origin=0,start=0,generation=0,due=0,deadline=0,trigger=0;
 unsigned hookCalls=0;std::string veto;
} joinRepair;
struct JoinRepairHooksScope {
 ~JoinRepairHooksScope(){joinRecoveryHook=joinPrivilegeHook=joinDispatchHook=nullptr;}
};
ULONGLONG joinRepairSettle(bool timeout,bool nonUser,bool callbackKnown,bool guiKnown,bool drainFinal=true){
 joinRecoveryHook=joinPrivilegeHook=joinDispatchHook=nullptr;
 baseCycle(true,nonUser);joinInjectSuspendOnDispatch=joinInjectManualOnDispatch=false;
 const auto origin=nowFile,start=nowTick,gen=generation;
 if(!timeout){g_sgRetrySleepTimeout=false;sgSleepTimeoutStop("fixture-timeout-off-before-classification");}
 if(callbackKnown)originalEntry(origin);
 if(guiKnown)handle506(1,origin);
 at(start+1375);resumeAutomatic();resumeInteractive();drainActualWake();
 check(classificationTimerDue==start+3375&&frozen&&!requestCount,
  "repair: original two-second classifier/lease missing before settlement");
 at(classificationTimerDue);sgResumeClassificationTick();
 if(drainFinal)drainActualWake();
 check(generation==gen&&!classificationTimerDue&&!sgSleepRetryActive()&&!timerDue&&!requestCount,
  "repair: settling observation advanced generation or dispatched active work");
 if(drainFinal)check(!frozen&&resumeCount==1&&g_sgTask.generation==0&&g_sgTask.mode==SgSleepMode::Unknown,
  "repair: ordinary settlement failed exact resource return and task retirement");
 else check(frozen&&!actualWakeQueue.empty(),"repair: old resource worker was not queued for controlled terminal interleaving");
 check(g_sgLastPowerButtonSleepIntentFileTime.load()==origin&&g_sgClosedSleepIntentFileTime.load()<origin,
  "repair: timeout-OFF/raw entry observation lost original receipt or closed its lifetime");
 if(!timeout)check(!timeoutTimerDue&&!g_sgRetrySleepTimeout,
  "repair: independent entry observation borrowed the disabled240-second feature");
 return origin;
}
void joinRepairSchedule(ULONGLONG origin){
 const auto start=(origin-epoch)/10000ULL;
 at(start+4414);sgObserveEntryFailureDisplay(1,"repair-late-entry-on");
 at(start+5415);joinLateEntryDeliver507(origin);
 joinLateEntryCheckScheduled(origin,"repair: late evidence failed original EntryFailure admission");
}
void joinRepairRecoveryInterleave(){
 ++joinRepair.hookCalls;
 check(!frozen&&resumeCount==1&&!requestCount,"repair: recovery hook did not follow exact original PID terminal");
 joinRepairSchedule(joinRepair.origin);
 joinRepair.due=timerDue;joinRepair.deadline=g_entryFailureQuickDeadline;joinRepair.trigger=g_sgSleepTriggerTick;
 check(g_sgRetryInProgress&&g_sgTask.retryKind==SgRetryKind::EntryFailure,
  "repair: terminal interleave did not publish exclusive retry ownership before old cleanup");
}
void joinRepairOldWorker(){
 JoinRepairHooksScope hooks;
 const auto origin=joinRepairSettle(true,false,true,true,false);
 joinRepair={};joinRepair.origin=origin;joinRepair.generation=generation;
 check(g_sgAutomaticSleepBudget.consume()&&g_sgAutomaticSleepBudget.consume(),"repair: old-worker control lacks prior spent budget");
 joinRecoveryHook=joinRepairRecoveryInterleave;
 drainActualWake();
 check(joinRepair.hookCalls==1&&!joinRecoveryHook,"repair: original resume_suspend worker missed controlled recovery terminal");
 check(sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::EntryFailure&&g_sgRetryInProgress&&
  g_sgTask.generation==joinRepair.generation&&g_sgTask.retryAttempt==0,
  "repair: old resume_suspend cleanup erased late-entry retry admitted inside recovery terminal");
 check(timerDue==joinRepair.due&&g_sgRetryDueTick==joinRepair.due&&
  g_entryFailureQuickDeadline==joinRepair.deadline&&g_sgSleepTriggerTick==joinRepair.trigger&&
  g_sgLastPowerButtonSleepIntentFileTime.load()==origin&&g_sgAutomaticSleepBudget.requests()==2,
  "repair: old cleanup replaced original deadline, trigger, human anchor or cumulative request account");
 check(!frozen&&resumeCount==1,"repair: old worker re-froze or double-returned its resource lease");
 dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==3,
  "repair: surviving late retry did not send once using the final third original slot");
 passed("joined-repair-old-resume-suspend-terminal-interleave-preserves-new-retry");
}
void joinRepairTimeoutOffDuplicate(){
 JoinRepairHooksScope hooks;const auto origin=joinRepairSettle(false,true,true,true),start=(origin-epoch)/10000ULL,gen=generation;
 at(start+3500);resumeAutomatic();resumeInteractive();drainActualWake();
 check(generation==gen&&!frozen&&resumeCount==1&&g_sgTask.generation==0&&!requestCount&&!classificationTimerDue,
  "repair: late duplicate PBT held resources or restarted completed classification");
 check(g_sgLastPowerButtonSleepIntentFileTime.load()==origin&&g_sgClosedSleepIntentFileTime.load()<origin,
  "repair: non-user-ON duplicate PBT erased independent timeout-OFF entry observation");
 joinRepairSchedule(origin);const auto due=timerDue,deadline=g_entryFailureQuickDeadline;
 at(nowTick+1);resumeAutomatic();resumeInteractive();drainActualWake();
 check(timerDue==due&&g_entryFailureQuickDeadline==deadline&&g_sgAutomaticSleepBudget.requests()==0,
  "repair: duplicate PBT extended admitted original late retry or consumed/refunded budget");
 dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1&&generation==gen,
  "repair: independent timeout-OFF late evidence failed actual original sender");
 passed("joined-repair-timeout-off-non-user-on-duplicate-PBT-retains-entry-observation");
}
void joinRepairApplyVeto(){
 ++joinRepair.hookCalls;at(nowTick+1);
 if(joinRepair.veto=="guard-off-on"){
  g_guardEnabled=false;sgCancelEntryFailureRetry("guard-disabled");sgFinishResumeClassification("guard-disabled");g_guardEnabled=true;
 }else if(joinRepair.veto=="entry-off-on"){
  g_sgRetryEntryFailure=false;sgCancelEntryFailureRetry("entry-failure-setting-disabled");g_sgRetryEntryFailure=true;
 }else if(joinRepair.veto=="manual-callback"){
  g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;
 }else if(joinRepair.veto=="windows-success")sgSleepTimeoutRecordSystemSuccess(nowFile);
 else if(joinRepair.veto=="new-off")sgObserveEntryFailureDisplay(0,"repair-new-real-display-off");
 else if(joinRepair.veto=="new506"){originalEntry(nowFile);handle506(1,nowFile);}
 else throw std::runtime_error("unknown-repair-veto-parameter");
 check(!requestCount,"repair: veto endpoint was reached only after the system sleep terminal");
}
void joinFinalReadManualKinds();
void joinNewEntryRequestBoundaries();
void joinNonButtonEntryBoundaries();
void joinPartialEntryPublication();
void joinFailedReturnEntryBoundaries();
void joinCleanupEntryBoundaries();
void joinCleanupSignalBoundaries();
void joinManualWakePublication();
void joinRepairVetoStages(const std::string& kind){
 if(kind=="new506")joinCleanupEntryBoundaries();
 if(kind=="new506")joinCleanupSignalBoundaries();
 if(kind=="new506")joinFailedReturnEntryBoundaries();
 for(unsigned stage=0;stage<(kind=="manual-callback"?4U:3U);++stage){
  JoinRepairHooksScope hooks;
  const auto origin=joinRepairSettle(true,false,true,true);joinRepairSchedule(origin);
  joinRepair={};joinRepair.origin=origin;joinRepair.veto=kind;const auto due=timerDue;
  const auto spent=g_sgAutomaticSleepBudget.requests();
  if(stage==0){joinRepairApplyVeto();at(due);sgDispatchSameModeRetry();}
  else{
   if(stage==3)joinDispatchHook=[]{joinPowerReadHook=joinRepairApplyVeto;};
   else if(stage==1)joinPrivilegeHook=joinRepairApplyVeto;else joinDispatchHook=joinRepairApplyVeto;
   at(due);sgDispatchSameModeRetry();
  }
  drainActualWake();
  const auto expected=spent+(stage==0?0U:1U);
  check(joinRepair.hookCalls==1&&!joinPrivilegeHook&&!joinDispatchHook&&!joinPowerReadHook,
   "repair: selected queued/privilege/dispatch terminal interleave was not consumed once");
  check(!requestCount&&!sgSleepRetryActive()&&!timerDue&&g_sgAutomaticSleepBudget.requests()==expected,
   "repair: veto during queue/actual sender preparation reached sleep or refunded charged request");
  // Adverse late events retain their original timestamps. These never call
  // any new ticket API or manually repair task fields.
  at(nowTick+1);sgObserveEntryFailureDisplay(1,"repair-veto-late-old-on");joinLateEntryDeliver507(origin);
  if(kind!="new506"){resumeAutomatic();resumeInteractive();drainActualWake();}
  if(classificationTimerDue){at((std::max)(nowTick,classificationTimerDue));sgResumeClassificationTick();drainActualWake();}
  if(monitorTimerDue){at((std::max)(nowTick,monitorTimerDue));sgMonitorEntryFailureTick();}
  sgDispatchSameModeRetry();
  check(!requestCount&&!sgSleepRetryActive()&&!timerDue&&g_sgAutomaticSleepBudget.requests()==expected,
   "repair: OFF-ON/manual/success/new-boundary veto revived via duplicate PBT/507/display/timer");
  check(!frozen&&resumeCount==1,"repair: preparation veto duplicated or stranded already-returned resource lease");
 }
 if(kind=="manual-callback"){joinFinalReadManualKinds();joinManualWakePublication();}
 if(kind=="new506")joinNewEntryRequestBoundaries();
 if(kind=="new506")joinNonButtonEntryBoundaries();
 if(kind=="new506")joinPartialEntryPublication();
 passed("joined-repair-veto-three-stages/"+kind);
}
void joinRepairLate506ThenExit(){
 for(const bool callbackKnown:{false,true}){
  JoinRepairHooksScope hooks;
  const auto origin=joinRepairSettle(false,true,callbackKnown,false),start=(origin-epoch)/10000ULL,gen=generation;
  at(start+4000);originalEntry(origin);handle506(1,origin);
  check(generation==gen&&!frozen&&resumeCount==1&&g_sgTask.generation==0&&!sgSleepRetryActive(),
   "repair: late original506 rebuilt generation/task or replayed pause before failure evidence");
  check(g_sgLastPowerButtonSleepIntentFileTime.load()==origin&&!timeoutTimerDue&&g_sgAutomaticSleepBudget.requests()==0,
   "repair: late original506 changed original identity, disabled timeout or existing account");
  joinRepairSchedule(origin);dispatch();
  check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1&&generation==gen&&resumeCount==1,
   "repair: callback-only/unknown original506 GUI arriving after settlement lost its late507 retry");
 }
 passed("joined-repair-timeout-off-original506-after-settlement-then507");
}
void joinRepairEvidenceBefore506(){
 // Exit cache path and display-only path share the same original source
 // entry; the first ON starts the unchanged five-second deadline in both.
 for(const bool exitFirst:{false,true}){
  JoinRepairHooksScope hooks;
  const auto origin=joinRepairSettle(false,true,false,false),start=(origin-epoch)/10000ULL,gen=generation;
  at(start+4414);sgObserveEntryFailureDisplay(1,"repair-first-on-before506");
  if(exitFirst){at(start+5415);joinLateEntryDeliver507(origin);}
  check(!requestCount&&!sgSleepRetryActive()&&!frozen&&g_sgAutomaticSleepBudget.requests()==0,
   "repair: raw observation without original506 authorized a sleep action");
  at(start+6000);originalEntry(origin);handle506(1,origin);
  check(generation==gen&&!frozen&&resumeCount==1&&g_sgAutomaticSleepBudget.requests()==0,
   "repair: late confirmed original506 replayed resource work or allocated/refunded an owner");
  if(exitFirst){
   joinLateEntryCheckScheduled(origin,"repair: cached actual507 did not join after late original506");
  }else{
   const auto firstOnDue=start+9414;
   check(monitorTimerDue==firstOnDue&&g_monitorEntryFailureDeadline==origin+9414ULL*10000ULL,
    "repair: original506 refinement restarted first ON five-second confirmation deadline");
   at(firstOnDue-1);sgMonitorEntryFailureTick();
   check(monitorTimerDue==firstOnDue&&!sgSleepRetryActive()&&!requestCount,
    "repair: late original506 shortened or extended original continuous display confirmation");
   at(firstOnDue);sgMonitorEntryFailureTick();
   joinLateEntryCheckScheduled(origin,"repair: display evidence before late original506 failed original retry");
  }
  dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1&&generation==gen,
   "repair: cached507/firstON plus late original506 did not reach exactly one tripleFALSE send");
 }
 passed("joined-repair-timeout-off507-or-firstON-before-original506");
}
void joinRepairUnknownOrNonButton506(){
 for(const int reason:{-1,0}){
  JoinRepairHooksScope hooks;
  const auto origin=joinRepairSettle(false,true,false,false),start=(origin-epoch)/10000ULL;
  at(start+4000);
  if(reason>=0){
   sgRememberKernelSleepEntry(reason,origin);g_sgLastKernel506Reason=reason;g_sgLastKernel506EventFileTime=origin;
   handle506(static_cast<WPARAM>(reason),static_cast<LPARAM>(origin));
  }
  at(start+4414);sgObserveEntryFailureDisplay(1,"repair-unconfirmed-on");
  at(start+5415);joinLateEntryDeliver507(origin);
  at(start+9414);sgMonitorEntryFailureTick();sgResumeClassificationTick();sgDispatchSameModeRetry();drainActualWake();
  check(!requestCount&&!sgSleepRetryActive()&&!timerDue&&!frozen&&resumeCount==1&&g_sgAutomaticSleepBudget.requests()==0,
   "repair: unknown/non-button506 lent power permission to raw observation, display or cached507");
 }
 passed("joined-repair-timeout-off-unknown-or-non-button506-cannot-authorize");
}


void joinRepairRetiredOrInvalidOriginal506(){
 for(const std::string kind:{"expired","entry-off-on","guard-off-on","missing","future","older-than-latest"}){
  JoinRepairHooksScope hooks;
  const auto origin=joinRepairSettle(false,true,true,true),start=(origin-epoch)/10000ULL;
  ULONGLONG incoming=origin;
  if(kind=="expired")at(start+SG_S0_ENTRY_FAILURE_WINDOW_MS+1);
  else if(kind=="entry-off-on"){
   g_sgRetryEntryFailure=false;sgCancelEntryFailureRetry("entry-failure-setting-disabled");g_sgRetryEntryFailure=true;
  }else if(kind=="guard-off-on"){
   g_guardEnabled=false;sgFinishResumeClassification("guard-disabled");g_guardEnabled=true;
  }else if(kind=="missing")incoming=0;
  else if(kind=="future")incoming=nowFile+10000ULL;
  else{
   at(start+6000);originalEntry(nowFile);handle506(1,nowFile);
   check(g_sgLatestUserSleepEntryFileTime.load()>origin,"repair: older506 control lacks a real newer confirmed user entry");
  }
  const auto gen=generation,anchor=g_sgLastPowerButtonSleepIntentFileTime.load();
  const auto task=g_sgTask.generation,trigger=g_sgSleepTriggerTick;
  const auto pauses=suspendQueued;const auto spent=g_sgAutomaticSleepBudget.requests();
  handle506(1,static_cast<LPARAM>(incoming));
  check(generation==gen&&suspendQueued==pauses&&g_sgTask.generation==task&&g_sgSleepTriggerTick==trigger,
   "repair: retired/missing/future/older original506 rebuilt generation, task, trigger or pause work");
  check(g_sgLastPowerButtonSleepIntentFileTime.load()==anchor&&!requestCount&&!sgSleepRetryActive()&&
   !timerDue&&!timeoutTimerDue&&g_sgAutomaticSleepBudget.requests()==spent,
   "repair: invalid or retired original506 revived an intent, request or disabled timeout");
  if(kind!="older-than-latest"){
   at(nowTick+1);sgObserveEntryFailureDisplay(1,"repair-invalid506-old-on");joinLateEntryDeliver507(origin);
   if(monitorTimerDue){at(monitorTimerDue);sgMonitorEntryFailureTick();}
   sgDispatchSameModeRetry();
   check(!requestCount&&!sgSleepRetryActive()&&!timerDue&&g_sgAutomaticSleepBudget.requests()==spent,
    "repair: rejected original506 revived through subsequent old ON/507/timer");
  }
 }
 passed("joined-repair-retired-or-invalid-original506-six-lifecycle-boundaries");
}
#ifdef YMCC_FIXTURE_ENTRY_HANDOFF_READY_ENABLE
// A real other thread owns the production operation mutex. Its wait is only
// the bounded fixture barrier; no sleep/game worker or external process starts.
class JoinRepairOtherMutexOwner {
 std::mutex barrier;std::condition_variable condition;bool held=false,released=false;std::thread owner;
 public:
 JoinRepairOtherMutexOwner():owner([this]{
  std::unique_lock<std::mutex> operation(g_sgOpMtx);
  std::unique_lock<std::mutex> gate(barrier);held=true;condition.notify_all();
  condition.wait(gate,[this]{return released;});
 }){}
 bool ready(){std::unique_lock<std::mutex> gate(barrier);return condition.wait_for(gate,std::chrono::seconds(2),[this]{return held;});}
 void stop(){
  {std::lock_guard<std::mutex> gate(barrier);released=true;}
  condition.notify_all();if(owner.joinable())owner.join();
 }
 ~JoinRepairOtherMutexOwner(){stop();}
};
void joinRepairBusyMutexHandoff(){
 JoinRepairHooksScope hooks;
 const auto origin=joinRepairSettle(false,false,true,true),start=(origin-epoch)/10000ULL,gen=generation;
 check(g_sgAutomaticSleepBudget.consume()&&g_sgAutomaticSleepBudget.consume(),"repair: mutex handoff control lacks two original spent requests");
 const auto notices=fixtureEntryHandoffNotifies,posts=fixtureEntryHandoffPostAttempts;
 const auto deliveries=fixtureEntryHandoffDeliveries.size();
 JoinRepairOtherMutexOwner owner;check(owner.ready(),"repair: other real thread failed to acquire actual operation mutex");
 at(start+4414);sgObserveEntryFailureDisplay(1,"repair-mutex-busy-on");
 at(start+5415);const auto before=std::chrono::steady_clock::now();joinLateEntryDeliver507(origin);
 check(std::chrono::steady_clock::now()-before<std::chrono::milliseconds(250),"repair: UI-side join blocked on worker operation mutex");
 check(!sgSleepRetryActive()&&!timerDue&&!monitorTimerDue&&!classificationTimerDue&&!timeoutTimerDue&&!requestCount&&
  g_sgTask.generation==0&&g_sgAutomaticSleepBudget.requests()==2&&g_sgEntryHandoffPending.load(),
  "repair: busy operation mutex granted action, rebuilt task, spent budget or created retry polling");
 check(fixtureEntryHandoffNotifies==notices+1&&fixtureEntryHandoffPostAttempts==posts&&fixtureEntryHandoffDeliveries.size()==deliveries,
  "repair: UI busy join did not issue exactly one original worker notice without a timer/post");
 sgResumeEntryObservationHandoffTick();
 check(fixtureEntryHandoffNotifies==notices+1&&!requestCount&&!timerDue&&g_sgEntryHandoffPending.load(),
  "repair: repeated busy UI service duplicated notification, timer or action");
 owner.stop();
 check(sgServiceEntryObservationHandoff(),"repair: actual worker handoff service did not consume original pending request");
 check(!g_sgEntryHandoffPending.load()&&fixtureEntryHandoffPostAttempts==posts+1&&
  fixtureEntryHandoffDeliveries.size()==deliveries+1&&!timerDue&&!requestCount,
  "repair: actual worker service failed one original WM_TIMER post or started a retry timer/API itself");
 check(!sgServiceEntryObservationHandoff()&&fixtureEntryHandoffPostAttempts==posts+1,
  "repair: consumed pending handoff replayed its WM_TIMER delivery");
 const auto delivery=fixtureEntryHandoffDeliveries.back();fixtureEntryHandoffDeliveries.pop_back();
 check(delivery.window==g_hwnd&&delivery.message==WM_TIMER&&delivery.timer==SG_RETRY_TIMER_ID&&delivery.payload==0,
  "repair: actual service posted a different UI route or changed payload");
 // These are the original two operations in the actual retry timer arm.
 sgConsumeEntryObservationHandoffReady(WM_TIMER);sgDispatchSameModeRetry();
 joinLateEntryCheckScheduled(origin,"repair: original Ready entry failed late-entry retry admission after mutex return");
 check(generation==gen&&g_sgAutomaticSleepBudget.requests()==2&&g_entryFailureQuickDeadline==origin+30000ULL*10000ULL,
  "repair: worker handoff replaced original owner, deadline or cumulative account");
 dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==3,
  "repair: actual handoff continuation did not send once using original final third slot");
 passed("joined-repair-real-other-thread-mutex-busy-original-worker-handoff");
}
#endif


void joinRepairWakeRecoveryHeaderPrefix(){
 JoinRepairHooksScope hooks;
 reset(false);joinHeaderAdmissionCount=0;
 joinActualWakeRecoveryAdmissionPrefix(currentPowerGeneration());
 check(joinHeaderAdmissionCount==1,"repair: actual header admission prefix rejected healthy recovery");
 const auto origin=joinRepairSettle(true,false,true,true);joinRepairSchedule(origin);
 joinHeaderAdmissionCount=0;joinActualWakeRecoveryAdmissionPrefix(currentPowerGeneration());
 check(joinHeaderAdmissionCount==0,"repair: actual header admission prefix admitted recovery across active entry retry");
 joinRepairSettle(true,false,true,true);suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
 joinHeaderAdmissionCount=0;joinActualWakeRecoveryAdmissionPrefix(currentPowerGeneration());
 check(joinHeaderAdmissionCount==0,"repair: actual header admission prefix admitted recovery across Native pending suspend");
 const auto manualOrigin=joinRepairSettle(true,false,true,true);joinRepairSchedule(manualOrigin);
 g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;
 handle507Native(1,static_cast<LPARAM>(nowFile));drainActualWake();
 check(!sgSleepRetryActive(),"repair: manual header control did not cancel actual retry");
 joinHeaderAdmissionCount=0;joinActualWakeRecoveryAdmissionPrefix(currentPowerGeneration());
 check(joinHeaderAdmissionCount==1&&!requestCount,"repair: actual header prefix blocked normally canceled manual recovery");
 passed("joined-repair-actual-wake-game-recovery-header-prefix-four-admissions");
}
#ifdef YMCC_FIXTURE_ENTRY_HANDOFF_READY_ENABLE
struct JoinRepairReadyPostScope {
 ~JoinRepairReadyPostScope(){fixtureEntryHandoffPostAccepted=true;fixtureEntryHandoffProbeLocks=false;}
};
ULONGLONG joinRepairReadyPending(){
 const auto origin=joinRepairSettle(false,false,true,true),start=(origin-epoch)/10000ULL;
 JoinRepairOtherMutexOwner owner;check(owner.ready(),"repair: Ready delivery control did not acquire actual op mutex on another thread");
 at(start+4414);sgObserveEntryFailureDisplay(1,"repair-ready-pending-on");
 at(start+5415);joinLateEntryDeliver507(origin);
 check(g_sgEntryHandoffPending.load()&&!sgSleepRetryActive()&&!requestCount&&!timerDue,
  "repair: actual busy UI join missed pending handoff or dispatched/polled before operation ownership returned");
 owner.stop();return origin;
}
void joinRepairReadyDeliveryModes(){
 for(const std::string kind:{"post-success","post-failure","cancel-late","cleanup-preserves","duplicate-delivery"}){
  JoinRepairHooksScope hooks;JoinRepairReadyPostScope ports;
  const auto origin=joinRepairReadyPending();const auto gen=generation;
  const auto posts=fixtureEntryHandoffPostAttempts,probes=fixtureEntryHandoffLockProbes;
  const auto deliveries=fixtureEntryHandoffDeliveries.size();
  fixtureEntryHandoffPostAccepted=kind!="post-failure";fixtureEntryHandoffProbeLocks=true;
  check(sgServiceEntryObservationHandoff(),"repair: actual Ready service did not consume pending handoff");
  check(!g_sgEntryHandoffPending.load()&&g_sgEntryHandoffReady.load()&&
   fixtureEntryHandoffPostAttempts==posts+1&&fixtureEntryHandoffLockProbes==probes+1&&
   fixtureEntryHandoffOpAvailable&&fixtureEntryHandoffWorkUnavailable,
   "repair: actual service lost Ready on Post failure or held op mutex while queue delivery lock was held");
  check(!timerDue&&!monitorTimerDue&&!classificationTimerDue&&!timeoutTimerDue&&!requestCount&&
   g_sgAutomaticSleepBudget.requests()==0&&!sgSleepRetryActive(),
   "repair: worker Ready/Post service created a timer, polled, spent budget or called sleep");
  const auto expectedDeliveries=deliveries+(kind=="post-failure"?0U:1U);
  check(fixtureEntryHandoffDeliveries.size()==expectedDeliveries,
   "repair: Post result did not match counted original WM_TIMER delivery");
  check(!sgServiceEntryObservationHandoff()&&fixtureEntryHandoffPostAttempts==posts+1,
   "repair: idle service reposted or reacquired a consumed pending handoff");
  for(const UINT message:{UINT(WM_POWERBROADCAST),UINT(kSleepPowerForwardMessage),UINT(kSharedPowerStateMessage),UINT(WM_CLOSE),UINT(WM_DESTROY),UINT(WM_NCDESTROY),UINT(WM_QUERYENDSESSION),UINT(WM_ENDSESSION)}){
   sgConsumeEntryObservationHandoffReady(message);
   check(g_sgEntryHandoffReady.load()&&!requestCount&&!sgSleepRetryActive()&&!timerDue&&g_sgAutomaticSleepBudget.requests()==0,
    "repair: priority power/close message consumed Ready or initiated autonomous sleep before its own handling");
  }
  if(kind=="cancel-late"){
   sgRevokeResumeEntryObservation();
   check(!g_sgEntryHandoffPending.load()&&!g_sgEntryHandoffReady.load(),
    "repair: actual revocation did not clear both pending and Ready in its delivery section");
  }else if(kind=="cleanup-preserves"){
   sgStopSleepRetry(true);
   check(g_sgEntryHandoffReady.load()&&!timerDue&&!requestCount,
    "repair: normal preserve cleanup swallowed unissued Ready or manufactured fallback timer");
  }
  // Actual WndProc-entry consumer: failed Post is not retried or converted
  // to worker SetTimer. A later ordinary entry consumes the same Ready once.
  sgConsumeEntryObservationHandoffReady(WM_NULL);sgDispatchSameModeRetry();
  if(fixtureEntryHandoffDeliveries.size()>deliveries){
   const auto delivery=fixtureEntryHandoffDeliveries.back();fixtureEntryHandoffDeliveries.pop_back();
   check(delivery.window==g_hwnd&&delivery.message==WM_TIMER&&delivery.timer==SG_RETRY_TIMER_ID&&delivery.payload==0,
    "repair: Ready service changed the original WM_TIMER route");
  }
  check(!g_sgEntryHandoffReady.load(),"repair: actual entry consumer did not exchange Ready once");
  if(kind=="cancel-late"){
   sgConsumeEntryObservationHandoffReady(WM_NULL);sgDispatchSameModeRetry();
   check(!requestCount&&!sgSleepRetryActive()&&!timerDue&&g_sgAutomaticSleepBudget.requests()==0,
    "repair: canceled Ready revived on late original WM_TIMER/entry");
  }else{
   joinLateEntryCheckScheduled(origin,"repair: original Ready consumer failed original late-entry retry admission");
   const auto due=timerDue,deadline=g_entryFailureQuickDeadline;
   sgConsumeEntryObservationHandoffReady(WM_NULL);sgDispatchSameModeRetry();
   check(timerDue==due&&g_entryFailureQuickDeadline==deadline&&!requestCount&&g_sgAutomaticSleepBudget.requests()==0,
    "repair: duplicate Ready/WM_TIMER prolonged original deadline or spent/refunded budget");
   dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1&&generation==gen,
    "repair: Ready/Post result did not continue one original tripleFALSE request");
  }
 }
 passed("joined-repair-final-ready-delivery-five-original-post-cancel-cleanup-modes");
}
#endif

// Same canonical consumer. Actual recovery final cleanup versus UI admission.
struct JoinAdmissionGate;
static JoinAdmissionGate* joinAdmissionGate=nullptr;
struct JoinAdmissionGate {
 std::mutex mutex;std::condition_variable condition;bool reached=false,released=false,timedOut=false;
 std::exception_ptr error;std::thread worker;
 void pause(){std::unique_lock<std::mutex> lock(mutex);reached=true;condition.notify_all();
  timedOut=!condition.wait_for(lock,std::chrono::seconds(2),[this]{return released;});}
 bool ready(){std::unique_lock<std::mutex> lock(mutex);return condition.wait_for(lock,std::chrono::seconds(2),[this]{return reached;});}
 void release(){ {std::lock_guard<std::mutex> lock(mutex);released=true;}condition.notify_all();if(worker.joinable())worker.join(); }
 ~JoinAdmissionGate(){release();joinAdmissionGate=nullptr;joinCleanupHook=nullptr;joinRecoveryHook=nullptr;}
};
void joinAdmissionArmFinalCleanup(){joinCleanupHook=[]{joinAdmissionGate->pause();};}
ULONGLONG joinAdmissionPrepare(bool unexpected,unsigned initialSpent=2){
 baseCycle(true,unexpected);joinInjectSuspendOnDispatch=joinInjectManualOnDispatch=false;
 const auto origin=nowFile;originalEntry(origin);handle506(1,origin);
 g_powerLifecycle=PowerLifecycle::Resuming;
 at(nowTick+240000);g_sgNonUserWakeObserved=unexpected;
 for(unsigned prior=0;prior<initialSpent;++prior)check(g_sgAutomaticSleepBudget.consume(),"admission: original requests not charged");
 check(g_sgTask.generation==generation&&!sgSleepRetryActive(),"admission: initial original task is not current");
 return origin;
}
void joinAdmissionStart(bool unexpected){if(unexpected)sgEvaluateExternalDeviceWake();else sgSleepTimeoutTick();}
void joinAdmissionReach(JoinAdmissionGate& gate){
 joinAdmissionGate=&gate;joinRecoveryHook=joinAdmissionArmFinalCleanup;
 const auto gen=generation;
 gate.worker=std::thread([&gate,gen]{try{consumeActualWake({SgWork::WakeSuspend,gen});}catch(...){gate.error=std::current_exception();}});
 check(gate.ready(),"admission: actual worker did not reach final stop after its locked guard");
 check(!frozen&&resumeCount==1,"admission: worker barrier preceded exact lease return");
}
void joinAdmissionReturn(JoinAdmissionGate& gate){
 gate.release();if(gate.error)std::rethrow_exception(gate.error);
 check(!gate.timedOut,"admission: actual final cleanup barrier timed out");
 if(g_sgEntryHandoffPending.load()){
  check(sgServiceEntryObservationHandoff(),"admission: existing worker did not deliver deferred admission");
  sgConsumeEntryObservationHandoffReady(WM_TIMER);
 }
}
static ULONGLONG joinAdmissionPublishedGeneration=0;
void joinAdmissionConsumeOldWorker(){
 std::exception_ptr error;
 std::thread worker([&]{try{consumeActualWake({SgWork::WakeSuspend,joinAdmissionPublishedGeneration});}catch(...){error=std::current_exception();}});
 worker.join();if(error)std::rethrow_exception(error);
}
void joinAdmissionReverse(bool unexpected){
 const auto origin=joinAdmissionPrepare(unexpected);joinAdmissionPublishedGeneration=generation;
 joinStartPublishedHook=joinAdmissionConsumeOldWorker;joinAdmissionStart(unexpected);
 check(!joinStartPublishedHook&&sgSleepRetryActive()&&timerDue&&g_sgTask.generation==generation&&
       g_sgLastPowerButtonSleepIntentFileTime.load()==origin&&frozen&&resumeCount==0&&g_sgAutomaticSleepBudget.requests()==2,
       "admission: a later worker crossed retry publication before external admission effects");
 dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==3,
       "admission: start-first ordering lost the original last request slot");
}
void joinAdmissionCleanup(bool unexpected){
 const auto origin=joinAdmissionPrepare(unexpected);const auto gen=generation;
 JoinAdmissionGate gate;joinAdmissionReach(gate);
 const auto notices=fixtureEntryHandoffNotifies;
 const auto started=std::chrono::steady_clock::now();joinAdmissionStart(unexpected);
 check(std::chrono::steady_clock::now()-started<std::chrono::milliseconds(250),"admission: UI blocked on old worker final lock");
 joinAdmissionStart(unexpected);joinAdmissionStart(unexpected);
 joinAdmissionReturn(gate);
 check(sgSleepRetryActive()&&timerDue&&g_sgRetryDueTick==timerDue&&
       g_sgTask.retryKind==(unexpected?SgRetryKind::UnexpectedWake:SgRetryKind::SleepTimeout),
       "admission: old final recovery cleanup erased a newly eligible non-entry retry");
 check(generation==gen&&g_sgLastPowerButtonSleepIntentFileTime.load()==origin&&
       g_sgAutomaticSleepBudget.requests()==2&&!requestCount&&resumeCount==1,
       "admission: deferred start replaced original owner, spent/refunded or repeated resource return");
 check(fixtureEntryHandoffNotifies==notices+1,"admission: repeated lock-busy delivery notified a worker more than once");
 dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==3,
       "admission: surviving timeout/unexpected retry did not send the final original slot exactly once");
 sgDispatchSameModeRetry();check(requestCount==1,"admission: accepted deferred start repeated");
 joinAdmissionReverse(unexpected);
 passed(unexpected?"admission-old-cleanup-preserves-unexpected":"admission-old-cleanup-preserves-timeout");
}
void joinAdmissionVetoes(bool unexpected){
 for(const std::string kind:{"manual","success","off-on","generation","pending","new-intent","exit"}){
  joinAdmissionPrepare(unexpected);JoinAdmissionGate gate;joinAdmissionReach(gate);joinAdmissionStart(unexpected);
  gate.release();if(gate.error)std::rethrow_exception(gate.error);
  if(kind=="manual"){g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;}
  else if(kind=="success")sgSleepTimeoutRecordSystemSuccess(nowFile);
  else if(kind=="off-on"){
   if(unexpected){g_sgResleepEnabled=false;sgClearUnexpectedWake("setting-disabled",false);g_sgResleepEnabled=true;}
   else{g_sgRetrySleepTimeout=false;sgSleepTimeoutStop("timeout-setting-disabled");g_sgRetrySleepTimeout=true;}
  }else if(kind=="generation")++generation;
  else if(kind=="pending")suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
  else if(kind=="new-intent")g_sgLastPowerButtonSleepIntentFileTime=nowFile+10000ULL;
  else g_exitRequested=true;
  if(g_sgEntryHandoffPending.load())sgServiceEntryObservationHandoff();
  sgConsumeEntryObservationHandoffReady(WM_TIMER);sgDispatchSameModeRetry();
  check(!requestCount&&!sgSleepRetryActive()&&!timerDue&&g_sgAutomaticSleepBudget.requests()==2,
        "admission: stale deferred start crossed manual/success/off/new source/exit veto");
  g_exitRequested=false;
 }
 passed(unexpected?"admission-unexpected-seven-final-vetoes":"admission-timeout-seven-final-vetoes");
}
void joinAdmissionPublication(){
 for(const bool unexpected:{false,true}){
  joinAdmissionPrepare(unexpected);JoinAdmissionGate gate;joinAdmissionReach(gate);joinAdmissionStart(unexpected);
  gate.release();if(gate.error)std::rethrow_exception(gate.error);
  ++g_sharedPowerPublicationRevision;
  if(g_sgEntryHandoffPending.load())sgServiceEntryObservationHandoff();
  sgConsumeEntryObservationHandoffReady(WM_TIMER);
  check(sgSleepRetryActive()&&timerDue&&g_sgAutomaticSleepBudget.requests()==2,
        "admission: harmless Ready/publication projection retired original deferred start");
  dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==3,
        "admission: publication-only continuation did not preserve original budget");
 }
 passed("admission-publication-only-keeps-original-two-kinds");
}

void joinAdmissionClassification(){
 baseCycle(true,true);joinInjectSuspendOnDispatch=joinInjectManualOnDispatch=false;
 const auto origin=nowFile,gen=generation,start=nowTick;
 originalEntry(origin);handle506(1,origin);
 at(start+130000);resumeAutomatic();resumeInteractive();
 check(sgResumeClassificationHeld(gen)&&classificationTimerDue&&g_sgSleepTimeoutWindow.active(),
       "admission: long original did not establish actual two-second resume classification");
 const auto timeoutDue=timeoutTimerDue;
 JoinRepairOtherMutexOwner owner;check(owner.ready(),"admission: old operation did not hold mutex during classification");
 g_sgLastKernel507EventFileTime=nowFile;handle507Native(32,nowFile);
 check(g_sgEntryHandoffPending.load()&&!sgSleepRetryActive(),
       "admission: non-user507 did not defer its existing unexpected-wake request");
 at(classificationTimerDue);sgResumeClassificationTick();
 check(!classificationTimerDue&&g_sgLastPowerButtonSleepIntentFileTime.load()==origin&&timeoutTimerDue==timeoutDue,
       "admission: normal classification changed original human intent or timeout deadline");
 owner.stop();check(sgServiceEntryObservationHandoff(),"admission: normal classification lost original worker signal");
 sgConsumeEntryObservationHandoffReady(WM_TIMER);
 check(sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::UnexpectedWake&&timerDue&&
       generation==gen&&g_sgLastPowerButtonSleepIntentFileTime.load()==origin,
       "admission: ordinary classification completion erased the pending unexpected-wake admission");
 dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,
       "admission: classification-returned original request did not reach one real sender port");
 passed("admission-normal-classification-preserves-busy-unexpected-ticket");
}

void joinNonEntryDeliveryBoundary(){
 for(const bool unexpected:{false,true})for(const std::string mode:{"post-success","post-failure","cancel-before-ready","cancel-after-ready","ordinary-cleanup","late-old-message"}){
  JoinRepairReadyPostScope ports;
  const auto origin=joinAdmissionPrepare(unexpected),gen=generation;
  const auto posts=fixtureEntryHandoffPostAttempts,notices=fixtureEntryHandoffNotifies;
  JoinRepairOtherMutexOwner owner;check(owner.ready(),"final delivery: actual operation owner missing");
  joinAdmissionStart(unexpected);joinAdmissionStart(unexpected);
  check(g_sgEntryHandoffPending.load()&&!sgSleepRetryActive()&&!requestCount&&
        fixtureEntryHandoffNotifies==notices+1,"final delivery: repeated busy admission spun, spent or lost original event");
  if(mode=="cancel-before-ready")sgStopSleepRetry();
  owner.stop();fixtureEntryHandoffPostAccepted=mode!="post-failure";
  check(sgServiceEntryObservationHandoff(),"final delivery: original worker did not consume pending work");
  check(g_sgEntryHandoffReady.load()&&fixtureEntryHandoffPostAttempts==posts+1&&!sgSleepRetryActive()&&!requestCount,
        "final delivery: Post result lost Ready or made worker issue a power request");
  check(!sgServiceEntryObservationHandoff()&&fixtureEntryHandoffPostAttempts==posts+1,
        "final delivery: idle original service repeated Post");
  if(mode=="cancel-after-ready")sgStopSleepRetry();
  if(mode=="ordinary-cleanup")sgStopSleepRetry(true);
  for(const UINT message:{UINT(WM_POWERBROADCAST),UINT(kSleepPowerForwardMessage),UINT(kSharedPowerStateMessage),
                         UINT(WM_CLOSE),UINT(WM_DESTROY),UINT(WM_NCDESTROY),UINT(WM_QUERYENDSESSION),UINT(WM_ENDSESSION)}){
   sgConsumeEntryObservationHandoffReady(message);
   check(!requestCount&&!sgSleepRetryActive()&&!timerDue,"final delivery: power/exit priority was crossed by pending autonomous work");
  }
  sgConsumeEntryObservationHandoffReady(WM_NULL);sgDispatchSameModeRetry();
  const bool cancelled=mode=="cancel-before-ready"||mode=="cancel-after-ready";
  if(cancelled){
   check(!sgSleepRetryActive()&&!timerDue&&!requestCount&&g_sgAutomaticSleepBudget.requests()==2,
         "final delivery: default cancellation revived an old deferred request");
  }else{
   check(sgSleepRetryActive()&&timerDue&&generation==gen&&g_sgLastPowerButtonSleepIntentFileTime.load()==origin&&
         g_sgAutomaticSleepBudget.requests()==2,"final delivery: successful/failed Post lost original same-intent scheduling");
   const auto due=timerDue;
   sgConsumeEntryObservationHandoffReady(WM_NULL);sgDispatchSameModeRetry();
   check(timerDue==due&&!requestCount&&g_sgAutomaticSleepBudget.requests()==2,
         "final delivery: a stale/duplicate ordinary message extended due or charged budget");
   dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==3,
         "final delivery: unissued original continuation did not send exactly the last slot");
   sgDispatchSameModeRetry();check(requestCount==1,"final delivery: stale original retry timer resent accepted request");
  }
  fixtureEntryHandoffDeliveries.clear();
 }
 passed("final-both-nonentry-kinds-six-delivery-cancel-cleanup-modes");
}
void joinNonEntryCoalescingBoundary(){
 for(const bool firstUnexpected:{false,true}){
  const auto origin=joinAdmissionPrepare(true),gen=generation;
  JoinRepairOtherMutexOwner owner;check(owner.ready(),"final coalescing: operation owner missing");
  joinAdmissionStart(firstUnexpected);joinAdmissionStart(!firstUnexpected);joinAdmissionStart(firstUnexpected);
  check(g_sgEntryHandoffPending.load()&&!sgSleepRetryActive()&&g_sgAutomaticSleepBudget.requests()==2,
        "final coalescing: two features sharing one intent spent/rebuilt before operation return");
  owner.stop();check(sgServiceEntryObservationHandoff(),"final coalescing: missing single worker return");
  sgConsumeEntryObservationHandoffReady(WM_NULL);
  check(sgSleepRetryActive()&&g_sgTask.retryKind==(firstUnexpected?SgRetryKind::UnexpectedWake:SgRetryKind::SleepTimeout)&&
        g_sgLastPowerButtonSleepIntentFileTime.load()==origin&&generation==gen,
        "final coalescing: later feature replaced first original deferred request");
  dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==3,"final coalescing: both features dispatched or refunded shared budget");
  fixtureEntryHandoffDeliveries.clear();
 }
 passed("final-two-feature-admission-coalesces-first-with-one-shared-last-slot");
}
static json joinAuxiliaryEvidence(const std::string& mode){
 json values={{"DurationInUs",1000ULL},{"DripsResidencyInUs",0ULL},{"HwDripsResidencyInUs",0ULL}};
 if(mode=="software")values["DripsResidencyInUs"]=1ULL;
 else if(mode=="hardware")values["HwDripsResidencyInUs"]=1ULL;
 else if(mode=="missing")values=json::object();
 else if(mode=="invalid")values["DripsResidencyInUs"]=1001ULL;
 return {{"windowsSleepStage","entered"},{"values",values}};
}
void joinDripsAuxiliaryDirect(){
 std::vector<std::string> parameters;
 for(const std::string mode:{"software","hardware","zero","missing","invalid"}){
  joinAdmissionPrepare(true);sgEvaluateExternalDeviceWake();
  check(sgSleepRetryActive()&&timerDue,"DRIPS direct: original unexpected retry missing");
  const auto success=sgSleepTimeoutSuccessfulExitTime();
  sgSleepTimeoutObserveCompleted(joinAuxiliaryEvidence(mode),nowFile,generation,5);
  check(sgSleepTimeoutSuccessfulExitTime()==success,"DRIPS direct: auxiliary counter fabricated conclusive Windows success");
  dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==3,"DRIPS direct: auxiliary accounting changed original request/budget");
  parameters.push_back(mode);
 }
 std::cout<<"DRIPS_DIRECT_PARAMETERS "<<json(parameters).dump()<<'\n';
 passed("drips-auxiliary-only-direct-five-counter-shapes-original-sender");
}
void joinDripsAuxiliarySource(){
 std::vector<std::string> parameters;
 for(const std::string mode:{"positive","zero","missing","invalid"}){
  baseCycle(false,false);const auto origin=nowFile,deadline=timeoutTimerDue;
  auto xml=timeoutXml(507,32,origin+10000ULL,true);
  const std::string field="<Data Name=\"DripsResidencyInUs\">1000</Data>";
  const auto atField=xml.find(field);check(atField!=std::string::npos,"DRIPS source: counter input missing");
  if(mode=="zero")xml.replace(atField,field.size(),"<Data Name=\"DripsResidencyInUs\">0</Data>");
  else if(mode=="missing")xml.erase(atField,field.size());
  else if(mode=="invalid")xml.replace(atField,field.size(),"<Data Name=\"DripsResidencyInUs\">1000001</Data>");
  timeoutSourceEvents={xml,timeoutXml(506,1,origin)};
  at(deadline);sgSleepTimeoutTick();drainTimeoutSource();sgSleepTimeoutTick();
  check(sgSleepRetryActive()&&timerDue&&timeoutSourceHandles.empty()&&timeoutSourceQueries==1,
        "DRIPS source: auxiliary accounting vetoed original deadline or retained query handles");
  dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,"DRIPS source: original one-shot timeout sender/budget changed");
  sgSleepTimeoutTick();sgDispatchSameModeRetry();check(requestCount==1,"DRIPS source: original request replayed");
  parameters.push_back(mode);
 }
 std::cout<<"DRIPS_SOURCE_PARAMETERS "<<json(parameters).dump()<<'\n';
 passed("drips-auxiliary-only-original-query-four-counter-shapes");
}

void joinKernelSuccessBeforeJournal();
static void joinPublishOrdinarySleepExit(ULONGLONG origin,ULONGLONG durationUs){
 joinKernelXml=timeoutXml(507,32,nowFile,false);
 joinKernelXml.insert(joinKernelXml.find("</EventData>"),
  "<Data Name=\"SleepEntered\">true</Data><Data Name=\"IsCsSessionInProgressOnExit\">false</Data>"
  "<Data Name=\"DurationInUs\">"+std::to_string(durationUs)+"</Data><Data Name=\"SleepDurationInUs\">"+std::to_string(durationUs)+"</Data>"
  "<Data Name=\"DripsResidencyInUs\">0</Data><Data Name=\"HwDripsResidencyInUs\">0</Data>");
 sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
 check(sgSleepTimeoutSuccessAfter(origin)==(durationUs==(nowFile-origin)/10),"paired success: exact completion readback differed");
}
static ULONGLONG joinUnexpectedWithOriginalEntry(){
 const auto origin=joinAdmissionPrepare(true);
 joinKernelXml=timeoutXml(506,1,origin);
 sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
 check(!sgSleepTimeoutSuccessAfter(origin),"paired success: original entry already completed");
 return origin;
}
static void joinUnexpectedPairedSuccess(){
 static ULONGLONG origin,systemWake;
 static unsigned calls;
 static const auto publish=[] {
  ++calls;
  joinPublishOrdinarySleepExit(origin,(nowFile-origin)/10);
  check(sgSleepTimeoutSuccessfulExitTime()==systemWake,
        "paired success: ordinary callback did not publish exact Windows completion independently of DRIPS");
 };
 for(const unsigned stage:{0U,1U,2U,3U}){
  JoinRepairHooksScope hooks;
  origin=joinUnexpectedWithOriginalEntry();
  const auto gen=generation;
  sgEvaluateExternalDeviceWake();
  check(sgSleepRetryActive()&&timerDue&&!requestCount,"paired success: unexpected retry not scheduled");
  systemWake=sgSleepTimeoutSuccessfulExitTime();
  calls=0;
  if(stage==0)publish();else if(stage==1)joinPrivilegeHook=publish;
  else if(stage==2)joinDispatchHook=publish;else joinDispatchHook=[] {joinPowerReadHook=publish;};
  dispatch();
  check(calls==1&&!requestCount,"paired success: newly completed ordinary Windows cycle crossed unexpected-wake sender");
  check(generation==gen&&!sgSleepRetryActive()&&!timerDue&&g_sgLastPowerButtonSleepIntentFileTime.load()==0&&
        g_sgAutomaticSleepBudget.requests()==(stage==0?2U:3U),
        "paired success: superseded retry did not retire or changed the charged budget");
 }
 for(const bool knownBeforeAdmission:{false,true}){
  origin=joinUnexpectedWithOriginalEntry();
  systemWake=sgSleepTimeoutSuccessfulExitTime();
  if(knownBeforeAdmission)publish();
  sgEvaluateExternalDeviceWake();
  check(sgSleepRetryActive()&&timerDue,"paired success: prior evidence vetoed a later unexpected wake");
  if(knownBeforeAdmission)sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  else joinPublishOrdinarySleepExit(origin,1000000);
  dispatch();
  check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==3,
        "paired success: duplicate prior completion or mismatched interval stopped the legitimate request");
 }
}
void joinUnexpectedNewSuccess(){
 for(const unsigned stage:{0U,1U,2U}){
  const auto origin=joinAdmissionPrepare(true),gen=generation;
  sgEvaluateExternalDeviceWake();check(sgSleepRetryActive()&&timerDue&&!requestCount,"final success: unexpected retry not scheduled");
  const auto publish=[]{
   joinKernelBeforeJournal=nullptr;joinKernelXml=timeoutXml(507,20,nowFile,false);
   sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  };
  if(stage==0)publish();else if(stage==1)joinPrivilegeHook=publish;else joinDispatchHook=publish;
  dispatch();
  check(!requestCount,"final success: newly confirmed Windows success crossed unexpected-wake sender");
  check(generation==gen&&g_sgLastPowerButtonSleepIntentFileTime.load()==0&&
        g_sgClosedSleepIntentFileTime.load()>=origin&&g_sgAutomaticSleepBudget.requests()==(stage==0?2U:3U),
        "final success: cancellation did not retire this exact intent or preserve original charged budget");
  g_sgNonUserWakeObserved=true;sgEvaluateExternalDeviceWake();sgDispatchSameModeRetry();
  check(!requestCount&&!sgSleepRetryActive()&&!timerDue,
        "final success: late old wake evidence revived a success-cancelled intent");
 }
 // Prior success belongs to the sleep that subsequently woke unexpectedly.
 // Equal/older duplicate observations cannot turn it into a new cancellation.
 joinAdmissionPrepare(true);
 sgSleepTimeoutRecordSystemSuccess(nowFile-10000ULL);
 const auto previous=sgSleepTimeoutSuccessfulExitTime();
 sgEvaluateExternalDeviceWake();check(sgSleepRetryActive()&&timerDue,"final success: prior successful sleep blocked a genuine later unexpected wake");
 sgSleepTimeoutRecordSystemSuccess(previous);sgSleepTimeoutRecordSystemSuccess(previous-10000ULL);
 dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==3,
       "final success: unchanged/older successful evidence canceled legitimate unexpected wake repair");
 joinKernelSuccessBeforeJournal();
 joinUnexpectedPairedSuccess();
 passed("final-new-windows-success-vetoes-unexpected-queued-privilege-log-stages");
}

void joinUnexpectedResumeWitness(){
 joinAdmissionPrepare(true);sgSleepTimeoutRecordSystemSuccess(nowFile-10000ULL);
 const auto admitted=sgSleepTimeoutSuccessfulExitTime();
 JoinRepairOtherMutexOwner owner;check(owner.ready(),"final resume witness: original operation not held");
 sgEvaluateExternalDeviceWake();check(g_sgEntryHandoffPending.load()&&!sgSleepRetryActive(),"final resume witness: no original deferred operation");
 owner.stop();check(sgServiceEntryObservationHandoff(),"final resume witness: original operation completion not delivered");
 joinPowerReadHook=[]{sgSleepTimeoutRecordSystemSuccess(nowFile);};
 sgConsumeEntryObservationHandoffReady(WM_NULL);
 if(sgSleepRetryActive())dispatch();
 check(!joinPowerReadHook&&sgSleepTimeoutSuccessfulExitTime()>admitted,
       "final resume witness: native observation leaf did not publish strictly newer positive success");
 check(!requestCount&&!sgSleepRetryActive()&&!timerDue&&g_sgAutomaticSleepBudget.requests()==2,
       "final resume witness: continuation resampled new Windows success as a fresh baseline");
 fixtureEntryHandoffDeliveries.clear();
 {
  static ULONGLONG origin;
  origin=joinUnexpectedWithOriginalEntry();
  JoinRepairOtherMutexOwner busy;check(busy.ready(),"paired defer: original operation not held");
  sgEvaluateExternalDeviceWake();check(g_sgEntryHandoffPending.load()&&!sgSleepRetryActive(),"paired defer: retry was not deferred");
  busy.stop();check(sgServiceEntryObservationHandoff(),"paired defer: original operation completion not delivered");
  joinPowerReadHook=[] {joinPublishOrdinarySleepExit(origin,(nowFile-origin)/10);};
  sgConsumeEntryObservationHandoffReady(WM_NULL);
  if(sgSleepRetryActive())dispatch();
  check(!joinPowerReadHook&&!requestCount&&!sgSleepRetryActive()&&!timerDue&&g_sgAutomaticSleepBudget.requests()==2,
        "paired defer: continuation resampled ordinary completion or spent a request");
  fixtureEntryHandoffDeliveries.clear();
 }
 passed("final-deferred-original-success-witness-survives-native-observation-read");
}


void joinManualWakePublication(){
 // Raw original wake may precede the later GUI collection-time fallback.
 // Use the actual complete WinEvt callback and both real atomic publishers.
 for(const unsigned kind:{0U,1U,2U})for(const unsigned stage:{0U,1U,2U,3U}){
  JoinRepairHooksScope hooks;
  if(kind==0){const auto origin=joinRepairSettle(true,false,true,true);joinRepairSchedule(origin);}
  else{joinAdmissionPrepare(kind==2);joinAdmissionStart(kind==2);}
  check(sgSleepRetryActive()&&timerDue,"manual publication: original admitted retry missing");
  const auto sourceEntry=g_sgLatestUserSleepEntryFileTime.load(),collectedIntent=nowFile;
  const auto rawWake=collectedIntent-10000ULL,priorWake=rawWake-10000ULL;
  const auto priorHighWater=collectedIntent-1;
  check(sourceEntry&&sourceEntry<=priorWake&&rawWake<collectedIntent,"manual publication: original/fallback time ordering not established");
  if(kind!=1)g_sgLastPowerButtonSleepIntentFileTime=collectedIntent;
  g_sgLastPowerButtonWakeEventFileTime=stage==3?priorHighWater:(stage==2?priorWake:0);
  g_sgOriginalPowerButtonWakeFileTime=stage==2?priorWake:0;
  const auto spent=g_sgAutomaticSleepBudget.requests();unsigned called=0;
  const auto interleave=[&]{
   ++called;
   if(stage!=0)check(g_sgOriginalPowerButtonWakeFileTime.load()==rawWake,"manual publication: original publisher did not execute before observer");
   dispatch();
   check(!requestCount,"manual publication: partial callback witness crossed actual sleep sender");
  };
  joinKernelAfterOriginalWake=nullptr;joinKernelAfterConfirmedWake=nullptr;
  if(stage==0||stage==3)joinKernelAfterConfirmedWake=interleave;else joinKernelAfterOriginalWake=interleave;
  joinKernelXml=timeoutXml(507,1,rawWake,false);
  sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  check(called==1&&!joinKernelAfterOriginalWake&&!joinKernelAfterConfirmedWake,"manual publication: exact original publisher interleave not consumed once");
  check(!sgSleepRetryActive()&&!timerDue&&g_sgAutomaticSleepBudget.requests()==spent,"manual publication: cancellation stranded work or refunded prior requests");
  const auto expectedHighWater=stage==3?priorHighWater:rawWake;
  check(g_sgOriginalPowerButtonWakeFileTime.load()==rawWake&&g_sgLastPowerButtonWakeEventFileTime.load()==expectedHighWater,"manual publication: final markers lost original wake or regressed highwater");
 }
 // An older callback cannot veto a newer original506-confirmed sleep.
 {
  JoinRepairHooksScope hooks;const auto origin=joinRepairSettle(true,false,true,true);joinRepairSchedule(origin);
  const auto spent=g_sgAutomaticSleepBudget.requests();unsigned called=0;
  g_sgLastPowerButtonWakeEventFileTime=0;g_sgOriginalPowerButtonWakeFileTime=0;
  joinKernelAfterOriginalWake=[&]{++called;dispatch();};
  joinKernelXml=timeoutXml(507,1,origin-1,false);
  sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  check(called==1&&requestCount==1&&g_sgAutomaticSleepBudget.requests()==spent+1,"manual publication: older original wake blocked newer admitted sleep");
 }
 // Missing original time retains the existing collection-time human veto.
 {
  JoinRepairHooksScope hooks;const auto origin=joinRepairSettle(true,false,true,true);joinRepairSchedule(origin);
  (void)origin;const auto spent=g_sgAutomaticSleepBudget.requests();unsigned called=0;
  g_sgLastPowerButtonWakeEventFileTime=0;g_sgOriginalPowerButtonWakeFileTime=0;
  joinKernelAfterConfirmedWake=[&]{++called;dispatch();};
  joinKernelXml=timeoutXml(507,1,nowFile,false);
  const auto begin=joinKernelXml.find("<TimeCreated "),end=joinKernelXml.find("/>",begin);
  check(begin!=std::string::npos&&end!=std::string::npos,"manual publication: missing-time XML anchor");
  joinKernelXml.erase(begin,end+2-begin);
  sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  check(called==1&&!requestCount&&!sgSleepRetryActive()&&!timerDue&&g_sgAutomaticSleepBudget.requests()==spent,"manual publication: missing original time lost existing manual veto");
  check(g_sgOriginalPowerButtonWakeFileTime.load()==0,"manual publication: missing original time manufactured raw witness");
 }
}

void joinFinalReadManualKinds(){
 // The real shared reader is retained. Only its existing hardware-admission
 // leaf publishes a callback witness while the final cancellation reads it.
 for(const bool unexpected:{false,true}){
  JoinRepairHooksScope hooks;joinAdmissionPrepare(unexpected);joinAdmissionStart(unexpected);
  check(sgSleepRetryActive()&&timerDue,"final manual: original non-entry retry did not arm");
  joinDispatchHook=[]{joinPowerReadHook=[]{
   g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;
  };};
  dispatch();
  check(!joinDispatchHook&&!joinPowerReadHook&&!requestCount&&!sgSleepRetryActive()&&!timerDue,
        "final manual: completed wake inside shared read crossed non-entry sender");
  check(g_sgAutomaticSleepBudget.requests()==3,"final manual: cancellation refunded original non-entry slot");
 }
 // Absent or older evidence must not block an otherwise eligible new entry.
 for(const bool oldWake:{false,true}){
  JoinRepairHooksScope hooks;const auto origin=joinRepairSettle(true,false,true,true);joinRepairSchedule(origin);
  const auto spent=g_sgAutomaticSleepBudget.requests();
  if(oldWake){g_sgLastPowerButtonWakeEventFileTime=origin-1;g_sgOriginalPowerButtonWakeFileTime=origin-1;}
  dispatch();
  check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==spent+1,
        "final manual: missing/older callback rejected a current valid entry retry");
 }
}

void joinKernelSuccessBeforeJournal(){
 for(const unsigned kind:{0U,1U,2U}){
  JoinRepairHooksScope hooks;
  if(kind==0){const auto origin=joinRepairSettle(true,false,true,true);joinRepairSchedule(origin);}
  else{joinAdmissionPrepare(kind==2);joinAdmissionStart(kind==2);}
  check(sgSleepRetryActive()&&timerDue,"callback success: original retry did not arm");
  joinKernelXml=timeoutXml(507,20,nowFile,false);const auto before=joinKernelJournalPoints;
  joinKernelBeforeJournal=[]{dispatch();};
  sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  check(joinKernelJournalPoints==before+1&&!joinKernelBeforeJournal,
        "callback success: complete original callback did not reach journal interleave once");
  check(!requestCount&&!sgSleepRetryActive()&&!timerDue,
        "callback success: completed Windows success waited behind journal and crossed sender");
 }
 // Unavailable/zero/invalid completed accounting is not success and never
 // turns into an automatic failure signal. Keep the original timeout policy.
 for(const std::string mode:{"positive","zero","not-entered","inconsistent","missing-time","throw"}){
  JoinRepairHooksScope hooks;const auto origin=joinRepairSettle(true,false,true,true);joinRepairSchedule(origin);
  joinKernelXml=timeoutXml(507,32,nowFile,true);
  const auto change=[&](const std::string& from,const std::string& to){
   const auto at=joinKernelXml.find(from);check(at!=std::string::npos,"callback negative XML anchor missing");
   joinKernelXml.replace(at,from.size(),to);
  };
  if(mode=="zero")change("<Data Name=\"DripsResidencyInUs\">1000</Data>","<Data Name=\"DripsResidencyInUs\">0</Data>");
  else if(mode=="not-entered")change("<Data Name=\"SleepEntered\">true</Data>","<Data Name=\"SleepEntered\">false</Data>");
  else if(mode=="inconsistent")change("<Data Name=\"DurationInUs\">1000000</Data>","<Data Name=\"DurationInUs\">100</Data>");
  else if(mode=="throw")joinKernelEvidenceThrows=true;
  else if(mode=="missing-time"){const auto at=joinKernelXml.find("<TimeCreated "),end=joinKernelXml.find("/>",at);check(at!=std::string::npos&&end!=std::string::npos,"callback missing-time anchor");joinKernelXml.erase(at,end+2-at);}
  const auto success=sgSleepTimeoutSuccessfulExitTime(),builds=static_cast<ULONGLONG>(joinKernelEvidenceBuilds);joinKernelBeforeJournal=[]{dispatch();};
  sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  check(requestCount==1&&sgSleepTimeoutSuccessfulExitTime()==success,
        "callback negative: unknown accounting manufactured success or blocked current valid retry");
  check(joinKernelEvidenceBuilds==builds+1&&!joinKernelEvidenceThrows,
        "callback negative: original evidence construction repeated or failed to return");
  if(mode=="throw"){
   const auto found=std::find_if(facts.rbegin(),facts.rend(),[](const auto& row){return row.first=="kernel-power";});
   check(found!=facts.rend()&&found->second.at("residencyParsed")==false,
         "callback parse fault escaped or lost ordinary unavailable-accounting diagnostic");
  }
 }
 // Success already known at admission belongs to the prior completed sleep.
 joinAdmissionPrepare(true);joinKernelBeforeJournal=nullptr;joinKernelXml=timeoutXml(507,32,nowFile-10000,true);
 sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
 joinAdmissionStart(true);dispatch();
 check(requestCount==1,"callback prior success blocked a genuine subsequent unexpected wake");
}



static ULONGLONG joinNew506Prior=0,joinNew506Stamp=0;
static unsigned joinNew506Callbacks=0;
static void joinNew506Publish(){
 at(nowTick+1);const auto stamp=joinNew506Stamp?joinNew506Stamp:nowFile;
 joinKernelXml=timeoutXml(506,1,stamp,false);
 sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
 ++joinNew506Callbacks;
 check(g_sgLastKernel506EventFileTime.load()==stamp,"new506 matrix: actual original callback not consumed");
 check(g_sgLatestUserSleepEntryFileTime.load()==(std::max)(joinNew506Prior,stamp),"new506 matrix: original entry publisher lost monotonicity");
}
static void joinNew506Arm(unsigned kind,unsigned initialSpent=2){
 if(kind==0){const auto origin=joinRepairSettle(true,false,true,true);joinRepairSchedule(origin);}
 else{joinAdmissionPrepare(kind==2,initialSpent);joinAdmissionStart(kind==2);}
 check(sgSleepRetryActive()&&timerDue,"new506 matrix: original eligible repair missing");
 joinNew506Prior=g_sgLatestUserSleepEntryFileTime.load();joinNew506Stamp=0;joinNew506Callbacks=0;
}
void joinNewEntryRequestBoundaries(){
 json observations=json::array();
 for(const unsigned kind:{0U,1U,2U})for(const unsigned stage:{0U,1U,2U,3U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind);
  const auto spent=g_sgAutomaticSleepBudget.requests();const auto resumes=resumeCount;const auto gen=generation;const bool wasFrozen=frozen;
  if(stage==0)joinNew506Publish();
  else if(stage==1)joinPrivilegeHook=joinNew506Publish;
  else if(stage==2)joinDispatchHook=joinNew506Publish;
  else joinDispatchHook=[]{joinPowerReadHook=joinNew506Publish;};
  dispatch();
  observations.push_back({{"kind",kind},{"stage",stage},{"calls",requestCount},{"requests",g_sgAutomaticSleepBudget.requests()}});
  check(joinNew506Callbacks==1&&!joinPrivilegeHook&&!joinDispatchHook&&!joinPowerReadHook,"new506 matrix: selected hook not consumed once");
  check(!requestCount&&!sgSleepRetryActive()&&!timerDue,"new506 matrix: newer Windows entry crossed old automatic sender");
  check(generation==gen&&resumeCount==resumes&&frozen==wasFrozen,"new506 matrix: cancellation disrupted current Windows sleep or exact game lease");
  check(g_sgAutomaticSleepBudget.requests()>=spent&&g_sgAutomaticSleepBudget.requests()<=spent+1,"new506 matrix: old budget refunded or consumed twice");
  sgDispatchSameModeRetry();check(!requestCount&&!timerDue,"new506 matrix: stale queued timer revived cancelled work");
 }
 for(const unsigned kind:{0U,1U,2U})for(const unsigned witness:{0U,1U,2U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind);const auto spent=g_sgAutomaticSleepBudget.requests();
  if(witness){joinNew506Stamp=joinNew506Prior-(witness==2?1:0);joinDispatchHook=joinNew506Publish;}
  dispatch();
  observations.push_back({{"kind",kind},{"witness",witness},{"calls",requestCount},{"requests",g_sgAutomaticSleepBudget.requests()}});
  check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==spent+1,"new506 matrix: absent/same/older original entry blocked legitimate retry");
  check(joinNew506Callbacks==(witness?1U:0U),"new506 matrix: positive callback count wrong");
 }

 // No initial original506: its later delivery still belongs to the old sleep.
 for(const unsigned kind:{0U,1U,2U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind);const auto original=joinNew506Prior;const auto spent=g_sgAutomaticSleepBudget.requests();
  g_sgLatestUserSleepEntryFileTime=0;g_sgLastKernel506EventFileTime=0;g_sgLastKernel506Reason=-1;
  sgScheduleNextSleepRetry("fixture-original506-previously-unavailable");
  joinNew506Prior=0;joinNew506Stamp=original;joinDispatchHook=joinNew506Publish;
  dispatch();observations.push_back({{"kind",kind},{"lateFirstOriginal",true},{"calls",requestCount},{"requests",g_sgAutomaticSleepBudget.requests()}});
  check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==spent+1,"late-first506: original entry older than queued request blocked legitimate retry");
 }

 std::cout<<"NEW506_MATRIX "<<observations.dump()<<'\n';
 // The existing parent alone records this canonical case.
}


static int joinAnyReason=0;
static unsigned joinAnyCalls=0,joinAnyMode=0;
static ULONGLONG joinAnyTime=0,joinAnyOlder=0;
static void joinAnyEntryOne(int reason,ULONGLONG stamp,bool missing=false){
 joinKernelXml=timeoutXml(506,reason,stamp,false);
 if(missing){const auto a=joinKernelXml.find("<TimeCreated "),b=joinKernelXml.find("/>",a);check(a!=std::string::npos&&b!=std::string::npos,"any-entry: missing-time fixture anchor");joinKernelXml.erase(a,b+2-a);}
 sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));++joinAnyCalls;
}
static void joinAnyEntryPublish(){
 at(nowTick+1);const auto atFile=joinAnyTime?joinAnyTime:nowFile;
 joinAnyEntryOne(joinAnyReason,atFile,joinAnyMode==1);
 if(joinAnyMode==2)joinAnyEntryOne(joinAnyReason,joinAnyOlder);
}
void joinNonButtonEntryBoundaries(){
 json observations=json::array();
 for(const unsigned kind:{0U,1U,2U})for(const int reason:{0,7,28,32})for(const unsigned stage:{0U,1U,2U,3U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind);joinAnyCalls=joinAnyMode=0;joinAnyTime=0;joinAnyReason=reason;
  const auto userEntry=g_sgLatestUserSleepEntryFileTime.load(),intent=g_sgLastPowerButtonSleepIntentFileTime.load();const auto gen=generation;const auto resumes=resumeCount;const bool paused=frozen;
  if(stage==0)joinAnyEntryPublish();else if(stage==1)joinPrivilegeHook=joinAnyEntryPublish;
  else if(stage==2)joinDispatchHook=joinAnyEntryPublish;else joinDispatchHook=[]{joinPowerReadHook=joinAnyEntryPublish;};
  dispatch();
  check(joinAnyCalls==1&&!joinPrivilegeHook&&!joinDispatchHook&&!joinPowerReadHook,"any-entry: selected callback not consumed once");
  check(!requestCount&&!sgSleepRetryActive()&&!timerDue,"any-entry: new nonbutton entry crossed previous automatic sender");
  check(g_sgLatestUserSleepEntryFileTime.load()==userEntry&&g_sgLastPowerButtonSleepIntentFileTime.load()==intent,"any-entry: nonbutton callback borrowed human intent");
  check(generation==gen&&resumeCount==resumes&&frozen==paused,"any-entry: cancellation changed Windows sleep or exact paused lease");
  observations.push_back({{"kind",kind},{"reason",reason},{"stage",stage},{"calls",requestCount}});
 }
 for(const unsigned kind:{0U,1U,2U})for(const int reason:{0,32}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind);joinAnyCalls=0;joinAnyMode=2;joinAnyTime=0;joinAnyReason=reason;joinAnyOlder=nowFile-10000;
  joinDispatchHook=joinAnyEntryPublish;dispatch();
  check(joinAnyCalls==2&&!requestCount&&!sgSleepRetryActive()&&!timerDue,"any-entry: out-of-order older506 erased new entry veto");
  check(g_sgLastKernel506EventFileTime.load()==joinAnyOlder&&g_sgLatestSleepEntryFileTime.load()>joinAnyOlder,"any-entry: highwater rewrote legacy event observation or regressed");
  observations.push_back({{"kind",kind},{"reason",reason},{"oldAfterNew",true},{"calls",requestCount}});
 }
 // Existing EntryFailure's legacy latest-event guard remains outside this
 // new witness. Prove no new cancellation authority for invalid timestamps
 // using both non-entry consumers that previously had no valid506 evidence.
 for(const unsigned kind:{1U,2U})for(const unsigned mode:{0U,1U,2U,3U,4U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind);joinAnyCalls=0;joinAnyMode=mode==0?1:0;joinAnyReason=0;
  const auto before=g_sgLatestSleepEntryFileTime.load();const auto spent=g_sgAutomaticSleepBudget.requests();
  joinAnyTime=mode==1?nowFile+100000000ULL:mode==2?g_sgTask.retryEntryFileTime:mode==3?joinNew506Prior:joinNew506Prior-1;
  if(mode==4){g_sgLatestSleepEntryFileTime=0;sgScheduleNextSleepRetry("first-old-nonbutton-original");joinAnyTime=joinNew506Prior;}
  const auto timeoutOrigin=sgSleepTimeoutOriginalIntent();
  const bool outsideTimeoutOrigin=kind==1&&mode==2&&joinAnyTime>timeoutOrigin;
  joinDispatchHook=joinAnyEntryPublish;dispatch();
  check(joinAnyCalls==1&&g_sgAutomaticSleepBudget.requests()==spent+1,"any-entry: missing callback or dispatch charge");
  if(outsideTimeoutOrigin)check(!requestCount&&!timerDue&&!sgSleepRetryActive(),"any-entry: elapsed sender borrowed a later 506 into the original window");
  else check(requestCount==1,"any-entry: missing/future/queued/old/first-delayed time canceled legitimate non-entry request");
  if(mode<=1)check(g_sgLatestSleepEntryFileTime.load()==before,"any-entry: missing/future XML time became valid raw-entry evidence");
  observations.push_back({{"kind",kind},{"positiveMode",mode},{"outsideTimeoutOrigin",outsideTimeoutOrigin},{"calls",requestCount}});
 }
 std::cout<<"ANY_ENTRY_MATRIX "<<observations.dump()<<'\n';
}
void joinPartialEntryPublication(){
 json observations=json::array();
 for(const unsigned kind:{0U,1U,2U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind);
  g_sensorSuspendCallbackActive=false;g_powerLifecycle=PowerLifecycle::Resuming;
  g_sgLastKernel506EventFileTime=g_sgLatestUserSleepEntryFileTime.load();g_sgLastKernel507EventFileTime=nowFile;
  const auto gen=generation;const auto resumes=resumeCount;const bool paused=frozen;unsigned calls=0;
  at((std::max)(nowTick,timerDue)+1);const auto stamp=nowFile;
  joinKernelAfterOriginalWake=[&]{
   ++calls;check(g_sgLatestSleepEntryFileTime.load()==stamp,"partial entry: original highwater not published first");
   dispatch();
   observations.push_back({{"kind",kind},{"calls",requestCount},{"phase",static_cast<unsigned>(g_powerLifecycle.load())},{"resumesDelta",resumeCount-resumes},{"pausedBefore",paused},{"pausedAfter",frozen}});
  };
  joinAnyEntryOne(0,stamp);
  check(calls==1&&!joinKernelAfterOriginalWake&&!requestCount&&!sgSleepRetryActive(),"partial entry: exact original publisher hook missing or old request sent");
  check(generation==gen,"partial entry: cancellation changed original generation");
 }
 std::cout<<"PARTIAL_ENTRY_RECOVERY "<<observations.dump()<<'\n';
 for(const auto& row:observations)check(row.at("phase")!=static_cast<unsigned>(PowerLifecycle::Ready)&&row.at("resumesDelta")==0&&row.at("pausedBefore")==row.at("pausedAfter"),"partial entry: request cancellation reopened Ready or returned lease before Native sleep fact");
}

void joinFailedReturnEntryBoundaries(){
 json observations=json::array();
 for(const unsigned kind:{0U,1U,2U})for(const unsigned stage:{0U,1U,2U})for(const bool exhausted:{false,true}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind,exhausted?2U:0U);joinAnyCalls=joinAnyMode=0;joinAnyTime=0;joinAnyReason=0;
  if(exhausted){g_sgTask.retryAttempt=SG_MAX_ENTRY_RETRIES-1;while(g_sgAutomaticSleepBudget.requests()<SG_MAX_ENTRY_RETRIES-1)check(g_sgAutomaticSleepBudget.consume(),"return entry: prepare original last request budget");}
  const auto gen=generation;const auto resumes=resumeCount;const bool paused=frozen;
  requestAccepted=false;
  if(stage==0)joinAfterCallHook=joinAnyEntryPublish;else if(stage==1)joinRouteResultHook=joinAnyEntryPublish;else joinRetryResultHook=joinAnyEntryPublish;
  dispatch();const auto firstCalls=requestCount;const auto phase=static_cast<unsigned>(g_powerLifecycle.load());
  sgDispatchSameModeRetry();if(timerDue)dispatch();
  observations.push_back({{"kind",kind},{"stage",stage},{"exhausted",exhausted},{"firstCalls",firstCalls},{"calls",requestCount},
   {"active",sgSleepRetryActive()},{"timer",timerDue},{"phase",phase},{"resumes",resumeCount-resumes},{"pausedBefore",paused},{"pausedAfter",frozen},{"generationSame",generation==gen},{"callbacks",joinAnyCalls},
   {"hookEmpty",!joinAfterCallHook&&!joinRouteResultHook&&!joinRetryResultHook}});
  joinAfterCallHook=joinRouteResultHook=joinRetryResultHook=nullptr;requestAccepted=true;
 }
 std::cout<<"FAILED_RETURN_ENTRY_MATRIX "<<observations.dump()<<'\n';
 for(const auto& row:observations){
  check(row.at("firstCalls")==1&&row.at("calls")==1,"return entry: failed API superseded by new entry repeated Windows call");
  check(row.at("active")==false&&row.at("timer")==0,"return entry: old retry not retired after superseding Windows entry");
  check(row.at("phase")!=static_cast<unsigned>(PowerLifecycle::Ready)&&row.at("phase")!=static_cast<unsigned>(PowerLifecycle::Resuming)&&row.at("resumes")==0&&row.at("pausedBefore")==row.at("pausedAfter"),
        "return entry: failed old frame reopened recovery during a new Windows entry");
  check(row.at("generationSame")==true&&row.at("callbacks")==1&&row.at("hookEmpty")==true,"return entry: boundary selection or owner changed");
 }
 // An accepted call's own entry remains Windows-owned, with no second send.
 for(const unsigned kind:{0U,1U,2U})for(const unsigned stage:{0U,1U,2U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind);joinAnyCalls=joinAnyMode=0;joinAnyTime=0;joinAnyReason=0;
  const auto gen=generation;const auto resumes=resumeCount;requestAccepted=true;
  if(stage==0)joinAfterCallHook=joinAnyEntryPublish;else if(stage==1)joinRouteResultHook=joinAnyEntryPublish;else joinRetryResultHook=joinAnyEntryPublish;
  dispatch();sgDispatchSameModeRetry();
  check(requestCount==1&&sgSleepRetryActive()&&!timerDue&&generation==gen&&resumeCount==resumes,"return entry: accepted own entry disturbed Windows-owned pending request");
  check(joinAnyCalls==1&&!joinAfterCallHook&&!joinRouteResultHook&&!joinRetryResultHook,"return entry: accepted own callback selection");
 }
 for(const unsigned kind:{1U,2U})for(const unsigned mode:{0U,1U,2U,3U,4U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind,0);joinAnyCalls=joinAnyMode=0;joinAnyReason=0;requestAccepted=false;
  const auto before=g_sgLatestSleepEntryFileTime.load();const auto spent=g_sgAutomaticSleepBudget.requests();
  joinAnyTime=mode==1?g_sgTask.retryEntryFileTime:mode==2?joinNew506Prior:mode==3?nowFile+100000000ULL:0;
  if(mode==4)joinAnyMode=1;
  if(mode)joinRetryResultHook=joinAnyEntryPublish;
  dispatch();
  check(requestCount==1&&sgSleepRetryActive()&&timerDue&&g_sgAutomaticSleepBudget.requests()==spent+1,"return entry: absent/same/old/future/missing timestamp suppressed legal rejected-request retry");
  if(mode==3||mode==4)check(g_sgLatestSleepEntryFileTime.load()==before,"return entry: invalid timestamp published new veto");
  check(joinAnyCalls==(mode?1U:0U)&&!joinRetryResultHook,"return entry: positive control callback selection");
  joinAfterCallHook=joinRouteResultHook=joinRetryResultHook=nullptr;requestAccepted=true;
 }
}


void joinCleanupEntryBoundaries(){
 json observations=json::array();
 for(const unsigned kind:{0U,1U,2U})for(const unsigned stage:{0U,1U,2U,3U,4U})for(const int reason:{0,1,32}){
  if((stage==2&&kind!=2)||(stage==3&&kind!=1))continue;
  const bool exhausted=stage!=0;
  JoinRepairHooksScope hooks;joinNew506Arm(kind,0);joinAnyCalls=joinAnyMode=0;joinAnyTime=0;joinAnyReason=reason;
  if(exhausted){g_sgTask.retryAttempt=SG_MAX_ENTRY_RETRIES-1;while(g_sgAutomaticSleepBudget.requests()<SG_MAX_ENTRY_RETRIES-1)check(g_sgAutomaticSleepBudget.consume(),"cleanup entry: prepare original final budget slot");}
  const auto gen=generation;const auto resumes=resumeCount;const bool paused=frozen;const auto cutoff=g_sgTask.retryEntryFileTime;
  requestAccepted=false;keepInput=stage==4;
  joinCleanupFactEvent=stage==0?"internal-sleep-request-cleared":stage==1?"sleep-retry-finished":stage==2?"unexpected-wake-cleared":stage==3?"sleep-timeout-stopped":"sleep-retry-exhausted-keep-writer";
  joinCleanupFactSource=stage==0?"sleep-retry-request-rejected":stage==2?"sleep-retry-exhausted":"";
  joinCleanupFactFired=false;joinCleanupFactHook=joinAnyEntryPublish;
  dispatch();
  const auto phase=static_cast<unsigned>(g_powerLifecycle.load());const auto closed=g_sgClosedSleepIntentFileTime.load();
  sgDispatchSameModeRetry();if(timerDue)dispatch();
  observations.push_back({{"kind",kind},{"stage",stage},{"reason",reason},{"calls",requestCount},{"active",sgSleepRetryActive()},{"timer",timerDue},
    {"phase",phase},{"resumes",resumeCount-resumes},{"pausedBefore",paused},{"pausedAfter",frozen},{"generationSame",generation==gen},{"fired",joinCleanupFactFired},
    {"callbacks",joinAnyCalls},{"closed",closed},{"newEntry",g_sgLatestSleepEntryFileTime.load()},{"cutoffBefore",cutoff}});
  joinCleanupFactHook=nullptr;requestAccepted=true;keepInput=false;
 }
 std::cout<<"CLEANUP_ENTRY_MATRIX "<<observations.dump()<<'\n';
 for(const auto& row:observations){
  check(row.at("fired")==true&&row.at("callbacks")==1,"cleanup entry: selected journal callback was not consumed exactly once");
  check(row.at("calls")==1&&row.at("active")==false&&row.at("timer")==0,"cleanup entry: late callback rebased and resent original repair");
  check(row.at("phase")!=static_cast<unsigned>(PowerLifecycle::Ready)&&row.at("phase")!=static_cast<unsigned>(PowerLifecycle::Resuming)&&row.at("resumes")==0&&row.at("pausedBefore")==row.at("pausedAfter"),
    "cleanup entry: late callback allowed exhausted frame to restore resources");
  check(row.at("closed")<row.at("newEntry"),"cleanup entry: old exhaustion retired the new Windows entry");
  check(row.at("generationSame")==true,"cleanup entry: cleanup changed original generation");
 }
 // No new evidence still permits original recovery or the remaining retry.
 for(const unsigned kind:{0U,1U,2U})for(const bool exhausted:{false,true}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind,0);requestAccepted=false;keepInput=false;joinCleanupFactHook=nullptr;
  if(exhausted){g_sgTask.retryAttempt=SG_MAX_ENTRY_RETRIES-1;while(g_sgAutomaticSleepBudget.requests()<SG_MAX_ENTRY_RETRIES-1)check(g_sgAutomaticSleepBudget.consume(),"cleanup control: last original slot");}
  const auto gen=generation;const auto origin=g_sgLastPowerButtonSleepIntentFileTime.load();const auto spent=g_sgAutomaticSleepBudget.requests();
  dispatch();
  check(requestCount==1&&generation==gen&&g_sgAutomaticSleepBudget.requests()==spent+1,"cleanup control: ordinary failure changed budget or owner");
  if(exhausted)check(!sgSleepRetryActive()&&!timerDue&&g_powerLifecycle.load()==PowerLifecycle::Resuming&&g_sgClosedSleepIntentFileTime.load()>=origin,
                    "cleanup control: unopposed exhaustion lost existing recovery");
  else check(sgSleepRetryActive()&&timerDue,"cleanup control: unopposed rejection lost remaining retry");
  requestAccepted=true;
 }
 // Accepted internal506 can legitimately be followed by an observed failure.
 for(const unsigned kind:{0U,1U,2U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind,0);joinAnyCalls=joinAnyMode=0;joinAnyTime=0;joinAnyReason=0;requestAccepted=true;
  joinAfterCallHook=joinAnyEntryPublish;dispatch();const auto ownEntry=g_sgLatestSleepEntryFileTime.load();
  sgObserveUnattributedSleepRetryFailure("fixture-accepted-entry-failed");sgDispatchSameModeRetry();
  check(requestCount==1&&sgSleepRetryActive()&&!timerDue&&g_sgLatestSleepEntryFileTime.load()==ownEntry,
    "cleanup control: unbound failure replaced an accepted attempt or its entry");
 }
}

static unsigned joinCleanupSignalMode=0;
static ULONGLONG joinCleanupSignalTime=0;
static void joinCleanupSignal(){
 at(nowTick+1);joinCleanupSignalTime=nowFile;
 if(joinCleanupSignalMode==0){++generation;return;}
 if(joinCleanupSignalMode==1){sgSleepTimeoutRecordSystemSuccess(nowFile);return;}
 if(joinCleanupSignalMode==2){
  joinKernelXml=timeoutXml(507,1,nowFile,false);
  sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));return;
 }
 suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
}
void joinCleanupSignalBoundaries(){
 json rows=json::array();
 for(const unsigned mode:{0U,1U,2U,3U})for(const unsigned kind:{0U,1U,2U})for(const unsigned stage:{0U,1U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind,0);requestAccepted=false;keepInput=false;joinCleanupSignalMode=mode;
  if(stage==1){g_sgTask.retryAttempt=SG_MAX_ENTRY_RETRIES-1;while(g_sgAutomaticSleepBudget.requests()<SG_MAX_ENTRY_RETRIES-1)check(g_sgAutomaticSleepBudget.consume(),"cleanup signal: final original slot");}
  const auto gen=generation;const bool paused=frozen;const auto resumed=resumeCount;
  joinCleanupFactEvent=stage==0?"internal-sleep-request-cleared":"sleep-retry-finished";joinCleanupFactSource=stage==0?"sleep-retry-request-rejected":"";
  joinCleanupFactFired=false;joinCleanupFactHook=joinCleanupSignal;dispatch();sgDispatchSameModeRetry();if(timerDue)dispatch();
  rows.push_back({{"mode",mode},{"kind",kind},{"stage",stage},{"fired",joinCleanupFactFired},{"calls",requestCount},{"phase",static_cast<unsigned>(g_powerLifecycle.load())},{"paused",frozen},{"pausedBefore",paused},{"resumes",resumeCount-resumed},{"active",sgSleepRetryActive()},{"timer",timerDue},{"generation",generation},{"oldGeneration",gen}});
  check(joinCleanupFactFired&&requestCount==1,"cleanup signal: superseded return issued another power request");
  if(mode!=0)check(!sgSleepRetryActive()&&!timerDue,"cleanup signal: callback did not retire old retry");
  check(g_powerLifecycle.load()!=PowerLifecycle::Ready&&g_powerLifecycle.load()!=PowerLifecycle::Resuming&&frozen==paused&&resumeCount==resumed,
        "cleanup signal: stale exhaustion recovered over newer Native evidence");
  if(mode==2){
   handle507Native(1,joinCleanupSignalTime);drainActualWake();
   check(!frozen&&!sgSleepRetryActive()&&!timerDue&&requestCount==1,"cleanup signal: deferred cancellation blocked confirmed human GUI recovery");
  }
  joinCleanupFactHook=nullptr;requestAccepted=true;
 }
 std::cout<<"CLEANUP_SIGNAL_MATRIX "<<rows.dump()<<'\n';
}


static unsigned joinPreparationMode=0;
static void joinPreparationPublicInput(){
 const ULONG codes[]={PBT_APMRESUMEAUTOMATIC,PBT_APMRESUMESUSPEND,PBT_APMRESUMECRITICAL,PBT_APMQUERYSUSPENDFAILED,PBT_APMQUERYSTANDBYFAILED,PBT_APMSUSPEND};
 if(joinPreparationMode<3||joinPreparationMode==5)suspendResumeSubscriptionCallback(nullptr,codes[joinPreparationMode],nullptr);
 else if(joinPreparationMode<5)g_sleepPowerDispatch.callback(codes[joinPreparationMode],nowTick,[](const auto&){return true;},nowFile);
 else if(joinPreparationMode==6)++g_sharedPowerPublicationRevision;
 else if(joinPreparationMode==7)++g_sharedPowerSuspendSerial;
 else if(joinPreparationMode==8)++generation;
 else if(joinPreparationMode==9)g_powerLifecycle=PowerLifecycle::Ready;
 else if(joinPreparationMode==11)g_exitRequested=true;
}
void joinPreparationPublicObservation(){
 json rows=json::array();
 for(const unsigned kind:{0U,1U,2U})for(const unsigned mode:{0U,1U,2U,3U,4U,5U,6U,7U,8U,9U,10U,11U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind,0);requestAccepted=true;
  const auto spent=g_sgAutomaticSleepBudget.requests();const auto oldGeneration=generation;
  joinPreparationMode=mode;joinCleanupFactEvent="internal-sleep-request-armed";
  joinCleanupFactSource.clear();joinCleanupFactFired=false;joinCleanupFactHook=joinPreparationPublicInput;
  dispatch();check(joinCleanupFactFired&&!joinCleanupFactHook,"preparation: original request journal hook was not reached once");
  const bool expectedCall=mode==6||mode==10;
  std::cout<<"PREPARATION_OBSERVATION "<<json({{"kind",kind},{"input",mode},{"calls",requestCount},{"expected",expectedCall?1:0}}).dump()<<std::endl;
  check(requestCount==(expectedCall?1:0),"preparation: source change was recaptured as a fresh sender permit");
  check(g_sgAutomaticSleepBudget.requests()==spent+1,"preparation: original request charge was refunded or duplicated");
  rows.push_back({{"kind",kind},{"input",mode},{"requests",requestCount},{"spentBefore",spent},{"spentAfter",g_sgAutomaticSleepBudget.requests()},{"generationBefore",oldGeneration},{"generationAfter",generation},{"publicSequence",g_sleepPowerDispatch.observedSequence()},{"phase",static_cast<unsigned>(g_powerLifecycle.load())}});
  joinCleanupFactHook=nullptr;g_exitRequested=false;
 }
 std::cout<<"RETRY_PREPARATION_PUBLIC_MATRIX "<<rows.dump()<<'\n';
 passed("joined-retry-preparation-original-public-observation");
}

static unsigned joinReturnObservationMode=0;
static unsigned joinReturnObservationHooks=0;
static void joinReturnObservationInput(){
 ++joinReturnObservationHooks;
 if(joinReturnObservationMode==0)++g_sharedPowerSuspendSerial;
 else if(joinReturnObservationMode==1)g_powerLifecycle=PowerLifecycle::Ready;
 else if(joinReturnObservationMode==2)g_powerLifecycle=PowerLifecycle::Resuming;
 else if(joinReturnObservationMode==3)suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
 else if(joinReturnObservationMode==4)++g_sharedPowerPublicationRevision;
}
void joinReturnPublicObservation(){
 json rows=json::array();
 for(const unsigned kind:{0U,1U,2U})for(const unsigned stage:{0U,1U,2U,3U,4U,5U})for(const unsigned mode:{0U,1U,2U,3U,4U,5U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind,0);requestAccepted=false;keepInput=stage==5;
  if(stage>=4){g_sgTask.retryAttempt=SG_MAX_ENTRY_RETRIES-1;while(g_sgAutomaticSleepBudget.requests()<SG_MAX_ENTRY_RETRIES-1)check(g_sgAutomaticSleepBudget.consume(),"return observation: original final request slot");}
  const auto spent=g_sgAutomaticSleepBudget.requests();const auto gen=generation;const auto resumed=resumeCount;const bool paused=frozen;
  joinReturnObservationMode=mode;joinReturnObservationHooks=0;
  if(stage==0)joinAfterCallHook=joinReturnObservationInput;
  else if(stage==1)joinRouteResultHook=joinReturnObservationInput;
  else if(stage==2)joinRetryResultHook=joinReturnObservationInput;
  else {joinCleanupFactEvent=stage==3?"internal-sleep-request-cleared":stage==4?"sleep-retry-finished":"sleep-retry-exhausted-keep-writer";joinCleanupFactSource=stage==3?"sleep-retry-request-rejected":"";joinCleanupFactFired=false;joinCleanupFactHook=joinReturnObservationInput;}
  dispatch();
  rows.push_back({{"kind",kind},{"stage",stage},{"input",mode},{"calls",requestCount},{"active",sgSleepRetryActive()},{"timer",timerDue},{"phase",static_cast<unsigned>(g_powerLifecycle.load())},{"paused",frozen},{"resumes",resumeCount-resumed}});
  std::cout<<"RETURN_OBSERVATION "<<rows.back().dump()<<std::endl;
  check(joinReturnObservationHooks==1,"return observation: original return boundary did not run exactly once");
  check(requestCount==1&&generation==gen&&g_sgAutomaticSleepBudget.requests()==spent+1,"return observation: original failed API charge changed");
  if(mode<4){
   check(!sgSleepRetryActive()&&!timerDue,"return observation: revoked public observation rearmed the old sleep request");
   check(frozen==paused&&resumeCount==resumed,"return observation: obsolete failed-return frame recovered the game");
   const auto expected=mode==1?PowerLifecycle::Ready:mode==2?PowerLifecycle::Resuming:PowerLifecycle::Suspending;
   check(g_powerLifecycle.load()==expected,"return observation: obsolete frame overwrote the newer public phase");
   sgDispatchSameModeRetry();check(requestCount==1,"return observation: stale timer replayed a revoked return");
  }else if(stage>=4){
   check(!sgSleepRetryActive()&&!timerDue&&g_powerLifecycle.load()==PowerLifecycle::Resuming,"return observation: unchanged/diagnostic publication blocked original exhausted recovery");
   sgDispatchSameModeRetry();check(requestCount==1,"return observation: exhausted positive control replayed an API");
  }else{
   check(sgSleepRetryActive()&&timerDue,"return observation: unchanged/diagnostic publication blocked legitimate fallback retry");
   requestAccepted=true;dispatch();check(requestCount==2&&g_sgAutomaticSleepBudget.requests()==spent+2,"return observation: legitimate retry lost its original budget");
  }
  joinAfterCallHook=joinRouteResultHook=joinRetryResultHook=joinCleanupFactHook=nullptr;requestAccepted=true;keepInput=false;
 }
 std::cout<<"RETURN_PUBLIC_OBSERVATION_MATRIX "<<rows.dump()<<'\n';
 passed("joined-failed-return-retains-public-observation");
}

static unsigned joinAdvanceMode=0,joinAdvanceHooks=0;
static void joinAdvanceInput(){
 ++joinAdvanceHooks;
 if(joinAdvanceMode==0)joinNew506Publish();
 else if(joinAdvanceMode==1){joinAnyMode=0;joinAnyReason=0;joinAnyTime=0;joinAnyEntryPublish();}
 else if(joinAdvanceMode==2)++g_sharedPowerSuspendSerial;
 else if(joinAdvanceMode==3)g_powerLifecycle=PowerLifecycle::Ready;
 else if(joinAdvanceMode==4)++g_sharedPowerPublicationRevision;
 else if(joinAdvanceMode==6){joinCleanupSignalMode=2;joinCleanupSignal();}
 else if(joinAdvanceMode==7)suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
}
static void joinUnattributedRetryIngress(){
  for(const unsigned arm:{0U,1U,2U}){
   JoinRepairHooksScope hooks;
   if(arm==2)joinAdmissionPrepare(true,0);
   else joinNew506Arm(arm==0?0:1,0);
   if(arm==2)joinAdmissionStart(true);
   requestAccepted=true;
   dispatch();
   check(requestCount==1&&sgSleepRetryActive()&&!timerDue,"first sleep call did not return accepted-pending");
   const auto generationBefore=generation;const auto priorBudget=g_sgAutomaticSleepBudget.requests();
   const auto priorKind=static_cast<int>(g_sgTask.retryKind);
   const auto beforeCount=requestCount;
   const auto marker=g_sgInternalSleepRequest.dispatchFileTime;
   const bool beforeFrozen=frozen;
   auto windowBefore=g_sgSleepTimeoutWindow;
   const auto beforeResume=resumeCount;
   std::uint64_t receiptSequence=0;
   if(arm==0){
    at(nowTick+91);joinKernelXml=timeoutXml(507,7,nowFile,false);
    sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
    check(g_sgLastKernel507EventFileTime==nowFile,"actual Kernel callback did not receive the original 507");
    handle507Native(7,nowFile);
   }
   else if(arm==1){
    at(nowTick+91);
    ymcc::sleeppower::Dispatch::Receipt ingress;
    g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPENDFAILED,nowTick,[&](const auto& r){ingress=r;return true;},nowFile);
    const auto forwarded=g_sleepPowerDispatch.forwarded(ingress.sequence,PBT_APMQUERYSUSPENDFAILED,nowTick);
    check(bool(forwarded)&&!sgIgnorePowerReturnBeforeCurrentIntent(forwarded),"actual public chronology check rejected the delayed event");
    receiptSequence=forwarded.sequence;
    check(timeoutQueryFailureActiveBranch(),"original query-failed arm not reached");
    g_sleepPowerDispatch.complete(forwarded,nowTick);
   }
   else {
    const auto origin=g_sgLastPowerButtonSleepIntentFileTime.load();
    check(origin!=0,"timer control requires original user anchor");
    g_sgRetrySleepTimeout=true;
    g_sgSleepTimeoutWindow={};
    sgSleepTimeoutArm(1,origin);
    check(g_sgSleepTimeoutWindow.waiting,"original timeout did not arm");
    windowBefore=g_sgSleepTimeoutWindow;
    const auto due=(origin-epoch)/10000ULL+240000;
    if(nowTick<due){at(due-1);sgSleepTimeoutTick();check(requestCount==1&&!timerDue,"retry before the original 240 second deadline");}
    at((std::max)(nowTick,static_cast<ULONGLONG>(due)));sgSleepTimeoutTick();
   }
   const auto armed=timerDue;
   if(armed)dispatch();
   check(!armed&&requestCount==beforeCount&&g_sgAutomaticSleepBudget.requests()==priorBudget&&
      static_cast<int>(g_sgTask.retryKind)==priorKind,"unattributed observation changed existing request or budget");
   check(g_sgInternalSleepRequest.dispatchFileTime==marker&&g_sgSleepTimeoutWindow.origin==windowBefore.origin&&
       g_sgSleepTimeoutWindow.requested==windowBefore.requested&&g_sgSleepTimeoutWindow.waiting==windowBefore.waiting,
     "unattributed observation consumed the active request marker or original timeout window");
   check(std::any_of(facts.begin(),facts.end(),[](const auto& fact){
      return fact.first=="sleep-retry-failure-attribution"&&fact.second.at("attribution")=="unknown"&&
          fact.second.at("retryAuthorized")==false;
   }),"consumer did not explicitly report the unknown attribution");
   at(nowTick+1);resumeAutomatic();drainActualWake();
   check(sgSleepRetryActive()&&frozen==beforeFrozen&&resumeCount==beforeResume&&requestCount==beforeCount,
      "unattributed observation or automatic wake released or retried the accepted attempt");
   at(nowTick+1);const auto manual=nowFile;
   joinKernelXml=timeoutXml(507,1,manual,false);
   sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
   handle507Native(1,manual);drainActualWake();
   handle507Native(1,manual);drainActualWake();
   check(!frozen&&resumeCount==beforeResume+static_cast<int>(beforeFrozen)&&!sgSleepRetryActive()&&!timerDue&&requestCount==beforeCount,
      "original button wake failed to restore the exact lease once without another sleep call");
   auto calls=json::array();
   for(const auto& [name,body]:facts)if(name=="sleep-request-route-result"){
    for(const auto& attempt:body.at("attempts")){
     const auto& api=attempt.at("apiCall");
     if(!api.is_null())calls.push_back({{"serial",api.at("callSerial")},{"accepted",api.at("accepted")},{"generation",api.at("generation")}});
    }
   }
   std::cout<<"ATTRIBUTION_ROW "<<json{{"arm",arm},{"generationBefore",generationBefore},{"generationAfter",generation},
    {"priorKind",priorKind},{"afterKind",static_cast<int>(g_sgTask.retryKind)},{"beforeCalls",beforeCount},
    {"afterCalls",requestCount},{"priorBudget",priorBudget},{"afterBudget",g_sgAutomaticSleepBudget.requests()},
    {"armed",armed},{"receiptSequence",receiptSequence},{"requestIdentitySupplied",false},{"calls",calls}}.dump()<<std::endl;
  }
}
void joinRetryAdvanceObservation(bool exhausted=false){
 if(!exhausted)joinUnattributedRetryIngress();
 json rows=json::array();
 for(const unsigned kind:{1U,2U,0U})for(const unsigned stage:(exhausted?std::vector<unsigned>{0U}:std::vector<unsigned>{0U,1U}))for(const unsigned mode:{0U,1U,2U,3U,4U,5U,6U,7U,8U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind,0);requestAccepted=true;
  if(exhausted){g_sgTask.retryAttempt=SG_MAX_ENTRY_RETRIES-1;while(g_sgAutomaticSleepBudget.requests()<SG_MAX_ENTRY_RETRIES-1)check(g_sgAutomaticSleepBudget.consume(),"advance: original final attempt slot");}
  dispatch();
  check(requestCount==1&&sgSleepRetryActive()&&!timerDue,"advance: did not reach one original accepted request");
  if(mode==8){resumeAutomatic();drainActualWake();check(sgSleepRetryActive(),"advance: prior automatic duplicate lost original request");}
  const auto gen=generation;const auto spent=g_sgAutomaticSleepBudget.requests();const auto resumed=resumeCount;const bool paused=frozen;
  if(stage==1)check(timeoutQueryFailureActiveBranch(),"advance: repeated unknown query missed its branch");
  joinAdvanceMode=mode;joinAdvanceHooks=0;joinCleanupFactEvent="sleep-retry-failure-attribution";joinCleanupFactSource="retry-query-rejected";joinCleanupFactFired=false;joinCleanupFactHook=joinAdvanceInput;
  check(timeoutQueryFailureActiveBranch(),"advance: original active query-failure arm was not reached");
  const bool requeued=timerDue!=0;
  if(timerDue)dispatch();
  rows.push_back({{"kind",kind},{"stage",stage},{"input",mode},{"calls",requestCount},{"requeued",requeued},{"active",sgSleepRetryActive()},{"timer",timerDue},{"spentBefore",spent},{"spentAfter",g_sgAutomaticSleepBudget.requests()},{"phase",static_cast<unsigned>(g_powerLifecycle.load())},{"resumes",resumeCount-resumed}});
  std::cout<<"RETRY_ADVANCE_OBSERVATION "<<rows.back().dump()<<std::endl;
  check(joinAdvanceHooks==1&&joinCleanupFactFired,"advance: selected original journal did not run once");
  const bool valid=mode==4||mode==5||mode==8;
  if(valid){
   check(!requeued&&requestCount==1&&sgSleepRetryActive()&&g_sgAutomaticSleepBudget.requests()==spent&&frozen==paused&&resumeCount==resumed,
       "advance: unknown query replaced or retired a current accepted request");
  }else{
   check(!requeued&&requestCount==1&&!sgSleepRetryActive()&&!timerDue,"advance: obsolete query-failure frame rebased and resent after new evidence");
   check(g_sgAutomaticSleepBudget.requests()==spent&&generation==gen,"advance: revoked frame spent/refunded original request budget or changed owner");
   check(frozen==paused&&resumeCount==resumed,"advance: revoked query-failure frame recovered game before current Native evidence");
   check(g_powerLifecycle.load()==(mode==3?PowerLifecycle::Ready:PowerLifecycle::Suspending),"advance: obsolete frame overwrote current public phase");
   sgDispatchSameModeRetry();check(requestCount==1,"advance: stale timer revived revoked continuation");
  }
  joinCleanupFactHook=nullptr;
 }
 std::cout<<(exhausted?"RETRY_ADVANCE_EXHAUSTION_MATRIX ":"RETRY_ADVANCE_MATRIX ")<<rows.dump()<<'\n';passed(exhausted?"joined-original-query-failure-advance-exhaustion":"joined-original-query-failure-advance-observation");
}

static unsigned joinScheduledFacts(){return static_cast<unsigned>(std::count_if(facts.begin(),facts.end(),[](const auto& fact){return fact.first=="sleep-retry-scheduled";}));}
void joinDuplicateQueryDeadline(){
 json rows=json::array();
 for(const unsigned kind:{1U,2U,0U})for(const unsigned sent:{1U,2U})for(const unsigned edge:{1U,0U,2U,3U,4U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind,0);requestAccepted=false;dispatch();if(sent==2)dispatch();requestAccepted=true;
  check(requestCount==static_cast<int>(sent)&&sgSleepRetryActive()&&timerDue>nowTick,"duplicate query: exact rejection did not schedule its original deadline");
  check(timeoutQueryFailureActiveBranch(),"duplicate query: unknown query did not reach active branch");
  const auto due=timerDue,entry=g_sgTask.retryEntryFileTime,trigger=g_sgSleepTriggerTick,gen=generation;
  const auto attempts=g_sgTask.retryAttempt;const auto spent=g_sgAutomaticSleepBudget.requests();const auto scheduled=joinScheduledFacts();const auto resumed=resumeCount;const bool paused=frozen;
  const auto duplicateTick=edge==0?nowTick:edge==1?nowTick+1:edge==2?due-1:edge==3?due:due+1;at(duplicateTick);
  check(timeoutQueryFailureActiveBranch(),"duplicate query: repeated original failure arm missing");
  rows.push_back({{"kind",kind},{"sent",sent},{"edge",edge},{"originalDue",due},{"duplicateTick",duplicateTick},{"dueAfter",timerDue},{"entryBefore",entry},{"entryAfter",g_sgTask.retryEntryFileTime},{"scheduledBefore",scheduled},{"scheduledAfter",joinScheduledFacts()},{"calls",requestCount}});
  std::cout<<"DUPLICATE_QUERY_OBSERVATION "<<rows.back().dump()<<std::endl;
  check(timerDue==due&&g_sgRetryDueTick==due,"duplicate query: repeated failure moved original retry deadline");
  check(g_sgTask.retryEntryFileTime==entry&&g_sgTask.retryAttempt==attempts&&g_sgSleepTriggerTick==trigger,"duplicate query: repeated failure rebased original attempt");
  check(joinScheduledFacts()==scheduled&&g_sgAutomaticSleepBudget.requests()==spent&&requestCount==static_cast<int>(sent),"duplicate query: repeated failure rescheduled, dispatched or changed request account");
  check(generation==gen&&resumeCount==resumed&&frozen==paused,"duplicate query: repeated failure changed owner or recovered game");
  at((std::max)(nowTick,due));
  const bool originalQuickExpired=kind==0&&g_entryFailureExpanded&&g_entryFailureQuickDeadline&&nowFile>g_entryFailureQuickDeadline;
  sgDispatchSameModeRetry();
  check(requestCount==static_cast<int>(sent)+(originalQuickExpired?0:1),"duplicate query: original timer did not retain original expiry/send semantics");
  check(g_sgAutomaticSleepBudget.requests()==spent+(originalQuickExpired?0:1),"duplicate query: original timer spent/refunded outside original account");
  sgDispatchSameModeRetry();check(requestCount==static_cast<int>(sent)+(originalQuickExpired?0:1),"duplicate query: queued duplicate timer replayed accepted request");
  rows.back()["originalQuickExpired"]=originalQuickExpired;rows.back()["callsAfterOriginalTimer"]=requestCount;rows.back()["spentAfterOriginalTimer"]=g_sgAutomaticSleepBudget.requests();
 }
 for(const unsigned kind:{1U,2U,0U})for(const unsigned input:{0U,1U}){
  JoinRepairHooksScope hooks;joinNew506Arm(kind,0);requestAccepted=false;dispatch();requestAccepted=true;timeoutQueryFailureActiveBranch();
  const auto spent=g_sgAutomaticSleepBudget.requests();const auto resumed=resumeCount;const bool paused=frozen;
  if(input==0)suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
  else{joinCleanupSignalMode=2;joinCleanupSignal();}
  check(timeoutQueryFailureActiveBranch(),"duplicate query: cancellation control did not reach original arm");
  check(!sgSleepRetryActive()&&!timerDue&&requestCount==1&&g_sgAutomaticSleepBudget.requests()==spent,"duplicate query: already queued guard masked public/manual cancellation");
  check(frozen==paused&&resumeCount==resumed,"duplicate query: cancellation control recovered ahead of current Native boundary");
  rows.push_back({{"kind",kind},{"cancellationInput",input},{"calls",requestCount},{"active",sgSleepRetryActive()},{"timer",timerDue}});
 }
 std::cout<<"DUPLICATE_QUERY_MATRIX "<<rows.dump()<<'\n';passed("joined-duplicate-query-keeps-original-deadline");
}

static unsigned joinTimerInputMode=0,joinTimerInputs=0;
static void joinTimerInput(){
 ++joinTimerInputs;
 if(joinTimerInputMode==2||joinTimerInputMode==6||joinTimerInputMode==9)joinNew506Publish();
 else if(joinTimerInputMode==3||joinTimerInputMode==8)++g_sharedPowerSuspendSerial;
 else if(joinTimerInputMode==4)suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
 else if(joinTimerInputMode==5){joinCleanupSignalMode=2;joinCleanupSignal();}
 else if(joinTimerInputMode==7)suspendResumeSubscriptionCallback(nullptr,PBT_APMRESUMEAUTOMATIC,nullptr);
 else if(joinTimerInputMode==10)++generation;
 else if(joinTimerInputMode==11)g_exitRequested=true;
}
static unsigned joinTimerFailureFacts(){return static_cast<unsigned>(std::count_if(facts.begin(),facts.end(),[](const auto& row){return row.first=="sleep-retry-timer-failed";}));}
void joinRetryTimerFailure(){
 json rows=json::array();
 const auto one=[&](unsigned kind,unsigned stage,unsigned input){
  JoinRepairHooksScope hooks;joinTimerFail=false;joinTimerApiHook=joinTimerFinishHook=nullptr;joinCleanupFactHook=nullptr;requestAccepted=true;
  ULONGLONG origin=0;
  if(stage==0){
   if(kind==0){origin=joinRepairSettle(true,false,true,true);const auto start=(origin-epoch)/10000ULL;at(start+4414);sgObserveEntryFailureDisplay(1,"timer-failure-late-entry");at(start+5415);}
   else origin=joinAdmissionPrepare(kind==2,0);
  }else if(stage==4){
   baseCycle(true,false);origin=nowFile;originalEntry(origin);handle506(1,origin);sgStartSleepRetry(SgRetryKind::EntryFailure,"timer-pause-readiness");
   check(sgSleepRetryActive()&&timerDue,"timer failure: original fresh entry did not schedule");g_sgPauseWorkCompletedGeneration=0;
  }else{
   joinNew506Arm(kind,0);if(stage==1)dispatch();
  }
  if(stage==3){at(timerDue-1);fakeKillTimer(g_hwnd,SG_RETRY_TIMER_ID);}
  if(stage==4){at(timerDue);fakeKillTimer(g_hwnd,SG_RETRY_TIMER_ID);}
  const auto calls=requestCount;const auto spent=g_sgAutomaticSleepBudget.requests();const auto resumed=resumeCount;const bool paused=frozen;const auto queued=actualWakeQueue.size();const auto scheduled=joinScheduledFacts();const auto failed=joinTimerFailureFacts();
  joinNew506Prior=g_sgLatestUserSleepEntryFileTime.load();joinNew506Stamp=0;joinNew506Callbacks=0;
  joinTimerInputMode=input;joinTimerInputs=0;joinTimerCalls=joinTimerFailures=0;joinTimerFail=input!=1;joinCleanupFactFired=false;
  if(input==6||input==7){joinCleanupFactEvent="sleep-retry-timer-failed";joinCleanupFactSource.clear();joinCleanupFactHook=joinTimerInput;}
  else if(input==8||input==9)joinTimerFinishHook=joinTimerInput;
  else if(input>=2)joinTimerApiHook=joinTimerInput;
  if(stage==0){if(kind==0)joinLateEntryDeliver507(origin);else joinAdmissionStart(kind==2);}
  else if(stage==1){
   check(timeoutQueryFailureActiveBranch(),"timer failure: original query-failure branch missing");
   check(joinTimerCalls==0&&requestCount==calls&&g_sgAutomaticSleepBudget.requests()==spent&&sgSleepRetryActive()&&!timerDue&&frozen==paused&&resumeCount==resumed,
      "timer failure: an unbound query started a timer or released the accepted attempt");
   joinTimerApiHook=joinTimerFinishHook=joinCleanupFactHook=nullptr;joinTimerFail=false;
   return;
  }else if(stage==2){requestAccepted=false;dispatch();requestAccepted=true;}
  else sgDispatchSameModeRetry();
  rows.push_back({{"kind",kind},{"stage",stage},{"input",input},{"timerCalls",joinTimerCalls},{"timerFailures",joinTimerFailures},{"active",sgSleepRetryActive()},{"timer",timerDue},{"due",g_sgRetryDueTick},{"requests",requestCount},{"budget",g_sgAutomaticSleepBudget.requests()},{"phase",static_cast<unsigned>(g_powerLifecycle.load())},{"phaseAtTimer",joinTimerPhase},{"wakeQueueBefore",queued},{"wakeQueueAfter",actualWakeQueue.size()},{"pausedBefore",paused},{"pausedAfter",frozen}});
  std::cout<<"RETRY_TIMER_OBSERVATION "<<rows.back().dump()<<std::endl;
  check(joinTimerCalls==1&&joinTimerFailures==(input==1?0U:1U),"timer failure: selected original API boundary was not called once");
  check(requestCount==calls+(stage==2?1:0)&&g_sgAutomaticSleepBudget.requests()==spent+(stage==2?1:0),"timer failure: timer arm dispatched or refunded system sleep");
  if(input==1){
   check(sgSleepRetryActive()&&timerDue,"timer failure: normal SetTimer success lost original retry");
   if(stage==4)g_sgPauseWorkCompletedGeneration=generation;
   dispatch();check(requestCount==calls+(stage==2?2:1),"timer failure: normal success lost original next send");
  }else{
   check(!sgSleepRetryActive()&&!timerDue&&!g_sgRetryDueTick,"timer failure: active retry stranded without an actual timer");
   check(joinTimerFailureFacts()==failed+1&&joinScheduledFacts()==scheduled,"timer failure: failed arm was reported as scheduled or lost its mandatory failure fact");
   if(input==0){
    check(g_powerLifecycle.load()==PowerLifecycle::Resuming,"timer failure: current failed attempt did not use existing recovery");
    drainActualWake();check(!frozen&&resumeCount==resumed+(paused?1:0),"timer failure: existing exact lease was not returned once");
   }else{
    check(joinTimerInputs==1,"timer failure: selected original callback/yield boundary was not reached once");
    check(actualWakeQueue.size()==queued&&frozen==paused&&resumeCount==resumed,"timer failure: revoked timer frame recovered over newer Native evidence");
    check(static_cast<unsigned>(g_powerLifecycle.load())==joinTimerPhase,"timer failure: revoked timer frame overwrote current public phase");
   }
   sgDispatchSameModeRetry();check(requestCount==calls+(stage==2?1:0),"timer failure: already queued WM_TIMER revived failed work");
  }
  joinTimerApiHook=joinTimerFinishHook=joinCleanupFactHook=nullptr;joinTimerFail=false;g_exitRequested=false;
 };
 for(const unsigned kind:{1U,2U,0U})for(const unsigned stage:{0U,1U,2U,3U})for(const unsigned input:{0U,1U,2U,3U,4U,5U,6U,7U})one(kind,stage,input);
 for(const unsigned input:{0U,1U,2U,3U,4U,5U,6U,7U})one(0,4,input);
 for(const unsigned input:{8U,9U,10U,11U})one(1,0,input);
 std::cout<<"RETRY_TIMER_MATRIX "<<rows.dump()<<'\n';passed("joined-original-retry-timer-failure-owned-terminal");
}



void joinField074830NewBoundaryVeto(){
 baseCycle(true,false);const auto original=nowFile,start=nowTick;
 originalEntry(original);handle506(1,original);const auto due=timeoutTimerDue,old=generation;
 at(start+1117);resumeAutomatic();resumeInteractive();drainActualWake();
 at(start+1332);suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
 const auto pending=g_sleepPowerDispatch.pendingReplay();check(bool(pending),"new-boundary control has no real receipt");
 const auto claim=g_sleepPowerDispatch.forwarded(pending.sequence,pending.type,nowTick);check(bool(claim),"new-boundary control has no claimed receipt");
 handleSuspendAdmission(PBT_APMSUSPEND,&claim);g_sleepPowerDispatch.complete(claim,nowTick);
 check(generation>old&&frozen&&!requestCount,"new-boundary control did not retire the old native generation");
 at(due);sgSleepTimeoutTick();if(timerDue)dispatch();
 check(!requestCount&&!g_sgAutomaticSleepBudget.requests(),
       "unclassified new Suspend borrowed old physical506 and sent old timeout across a retired generation");
 passed("field074830-new-suspend-without-continuation-rejects-old-timeout");
}

void joinField074830ReentryTimeout(){
 baseCycle(true,false);joinInjectSuspendOnDispatch=joinInjectManualOnDispatch=false;
 const auto originalGeneration=generation,originalObserved=nowFile,start=nowTick;
 const auto original=originalObserved-485435ULL;
 originalEntry(original);handle506(1,original);
 const auto originalDue=timeoutTimerDue;
 check(originalDue&&g_sgSleepTimeoutGeneration==generation,"field074830: original physical506 did not arm its independent window");
 // 506:53.9138436, PBT:53.9623915; old507:55.0747674;
 // next506:55.0766043, Resume PBT:55.0791288, nextSuspend PBT:55.2946458.
 at(start+1117);resumeAutomatic();resumeInteractive();drainActualWake();
 check(classificationTimerDue&&!sgSleepRetryActive()&&generation==originalGeneration,
       "field074830: first resume did not retain the original classification");
 at(start+1332);suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
 const auto pending=g_sleepPowerDispatch.pendingReplay();check(bool(pending),"field074830: new Suspend receipt missing");
 const auto claim=g_sleepPowerDispatch.forwarded(pending.sequence,pending.type,nowTick);
 check(bool(claim),"field074830: original receipt cannot be claimed");
 handleSuspendAdmission(PBT_APMSUSPEND,&claim);g_sleepPowerDispatch.complete(claim,nowTick);
 check(generation>originalGeneration&&!g_sleepPowerDispatch.pendingSuspend()&&frozen&&!requestCount,
       "field074830: new admitted Suspend did not own a distinct current generation and retained lease");
 const auto successorGeneration=generation;
 const auto oldExit=original+11609238ULL,nextEntry=original+11627607ULL;
 at(start+2052);
 // Original callback publishes source high-water before the GUI consumes its
 // posts; journal row order is not the atomic publication order.
 joinKernelXml=timeoutXml(507,16777220,oldExit,false);
 sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
 joinKernelXml=timeoutXml(506,16777220,nextEntry,false);
 sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
 handle507Native(16777220,oldExit);handle506(16777220,nextEntry);drainActualWake();
 check(!sgSleepRetryActive(),"field074830: delayed non-button506 source should match field entryFailure=false, not schedule a quick retry");
 std::cout<<"FIELD074830_STATE "<<json{{"originalGeneration",originalGeneration},{"successorGeneration",successorGeneration},
  {"originalIntent100ns",original},{"old507100ns",oldExit},{"next506100ns",nextEntry},
  {"windowActive",g_sgSleepTimeoutWindow.active()},{"timeoutGeneration",g_sgSleepTimeoutGeneration},
  {"timeoutTimerDue",timeoutTimerDue},{"originalDue",originalDue},{"timeoutCurrent",sgSleepTimeoutCurrent()},
  {"actionRequests",requestCount},{"facts",facts}}.dump()<<"\n";
 at(originalDue);sgSleepTimeoutTick();
 if(timerDue)dispatch();
 check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,
       "field074830: original 240-second fallback was lost after automatic reentry; no sender call or action charge");
 passed("field074830-original-timeout-after-automatic-reentry");
}


void joinField074830OwnerMatrix(){
 json results=json::array();
 for(const std::string kind: {"duplicate","manual913","new-human","success","off","exit","unrelated-entry"}){
  baseCycle(true,false);joinInjectSuspendOnDispatch=joinInjectManualOnDispatch=false;
  const auto originalGeneration=generation,originalObserved=nowFile,start=nowTick;
  const auto original=originalObserved-485435ULL;
  originalEntry(original);handle506(1,original);const auto due=timeoutTimerDue;
  check(due&&g_sgSleepTimeoutGeneration==generation,"owner: original window missing");
  at(start+1117);resumeAutomatic();resumeInteractive();drainActualWake();
  at(start+1332);suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
  const auto pending=g_sleepPowerDispatch.pendingReplay();check(bool(pending),"owner: no original receipt");
  const auto claim=g_sleepPowerDispatch.forwarded(pending.sequence,pending.type,nowTick);check(bool(claim),"owner: receipt not claimed");
  handleSuspendAdmission(PBT_APMSUSPEND,&claim);g_sleepPowerDispatch.complete(claim,nowTick);
  const auto successor=generation;check(successor==originalGeneration+1&&frozen,"owner: missing retained exact lease");
  const auto oldExit=original+11609238ULL;
  const auto nextEntry=kind=="unrelated-entry"?claim.fileTime100ns+10000ULL:original+11627607ULL;
  at(start+2052);
  joinKernelXml=timeoutXml(507,16777220,oldExit,false);sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  joinKernelXml=timeoutXml(506,16777220,nextEntry,false);sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  handle507Native(16777220,oldExit);handle506(16777220,nextEntry);drainActualWake();
  if(kind=="unrelated-entry"){
   at(due);sgSleepTimeoutTick();if(timerDue)dispatch();
   check(!requestCount&&!g_sgAutomaticSleepBudget.requests(),"owner: unrelated post-receipt entry granted original intent");
  }else{
   check(sgSleepTimeoutCurrent()&&g_sgSleepTimeoutGeneration==successor&&g_sgSleepTimeoutWindow.origin==original&&timeoutTimerDue==due,
         "owner: typed adoption changed original origin/deadline or lost current relation");
   if(kind!="duplicate")check(g_sgAutomaticSleepBudget.consume()&&g_sgAutomaticSleepBudget.consume(),"owner: original two charges missing");
   if(kind=="manual913"||kind=="new-human"){
    at(kind=="manual913"?start+913183:due);
    joinKernelXml=timeoutXml(kind=="manual913"?507:506,1,nowFile,false);
    sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
   }else{at(due);if(kind=="success")sgSleepTimeoutRecordSystemSuccess(nowFile);if(kind=="off")g_sgRetrySleepTimeout=false;if(kind=="exit")g_exitRequested=true;}
   const auto queries=timeoutSourceQueries;sgSleepTimeoutTick();if(timerDue)dispatch();
   if(kind=="duplicate"){
    check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,"owner: continued original did not issue once");
    sgSleepTimeoutTick();if(timerDue)dispatch();
    check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,"owner: duplicate timer replayed original action");
   }else check(!requestCount&&g_sgAutomaticSleepBudget.requests()==2&&frozen&&resumeCount==0&&timeoutSourceQueries==queries,
              "owner: callback/final veto changed original action account, query or exact lease");
  }
  results.push_back(kind);
 }
 std::cout<<"OWNER074830_MATRIX "<<results.dump()<<"\n";
 passed("field074830-owner-adoption-and-seven-boundary-parameters");
}


void joinField074830OwnerInterval(bool fresh){
 baseCycle(true,false);joinInjectSuspendOnDispatch=joinInjectManualOnDispatch=false;
 const auto originalObserved=nowFile,start=nowTick,originalGeneration=generation;
 const auto original=originalObserved-485435ULL;
 originalEntry(original);handle506(1,original);const auto due=timeoutTimerDue;
 const auto admit=[&](){
  suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
  const auto pending=g_sleepPowerDispatch.pendingReplay();check(bool(pending),"interval: receipt missing");
  const auto claim=g_sleepPowerDispatch.forwarded(pending.sequence,pending.type,nowTick);check(bool(claim),"interval: receipt not claimed");
  handleSuspendAdmission(PBT_APMSUSPEND,&claim);g_sleepPowerDispatch.complete(claim,nowTick);
 };
 const auto publish=[&](ULONGLONG exit,ULONGLONG entry){
  joinKernelXml=timeoutXml(507,16777220,exit,false);sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  joinKernelXml=timeoutXml(506,16777220,entry,false);sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  handle507Native(16777220,exit);handle506(16777220,entry);drainActualWake();
 };
 at(start+1117);resumeAutomatic();resumeInteractive();drainActualWake();
 at(start+1332);admit();const auto firstExit=original+11609238ULL,firstEntry=original+11627607ULL;
 at(start+2052);publish(firstExit,firstEntry);
 check(generation==originalGeneration+1&&sgSleepTimeoutCurrent()&&frozen,"interval: initial relation did not adopt original");
 at(start+2700);resumeAutomatic();resumeInteractive();drainActualWake();
 at(start+2800);admit();check(generation==originalGeneration+2&&frozen&&!requestCount,"interval: subsequent actual boundary missing");
 at(start+3500);publish(fresh?original+21000000ULL:firstExit,fresh?original+22000000ULL:firstEntry);
 at(due);sgSleepTimeoutTick();if(timerDue)dispatch();
 if(fresh)check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,"interval: fresh successor interval lost original deadline");
 else check(!requestCount&&!g_sgAutomaticSleepBudget.requests(),"interval: reused old506/507 evidence authorized a second new Suspend");
 passed(fresh?"field074830-fresh-second-interval-retains-original-deadline":"field074830-replayed-old-interval-cannot-adopt-another-boundary");
}


void joinOwnerPublicationOverlap(bool vetoes){
 const std::vector<std::string> kinds=vetoes?std::vector<std::string>{"manual","success","off","exit","new-human","new-pbt"}:std::vector<std::string>{"continue"};
 json observed=json::array();
 for(const auto& kind:kinds){
  baseCycle(true,false);joinInjectSuspendOnDispatch=joinInjectManualOnDispatch=false;
  const auto originalObserved=nowFile,start=nowTick,oldGeneration=generation;
  const auto original=originalObserved-485435ULL,oldExit=original+11609238ULL,nextEntry=original+11627607ULL;
  originalEntry(original);handle506(1,original);const auto due=timeoutTimerDue;
  if(vetoes)check(g_sgAutomaticSleepBudget.consume()&&g_sgAutomaticSleepBudget.consume(),"publication: original action account missing");
  at(start+1117);resumeAutomatic();resumeInteractive();drainActualWake();
  at(start+1332);suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
  const auto receipt=g_sleepPowerDispatch.pendingReplay();check(bool(receipt),"publication: no original Suspend receipt");
  const auto claim=g_sleepPowerDispatch.forwarded(receipt.sequence,receipt.type,nowTick);check(bool(claim),"publication: receipt was not claimed");
  handleSuspendAdmission(PBT_APMSUSPEND,&claim);g_sleepPowerDispatch.complete(claim,nowTick);
  check(generation==oldGeneration+1&&frozen&&!requestCount,"publication: second original boundary not admitted");
  at(start+2052);joinKernelXml=timeoutXml(507,16777220,oldExit,false);
  sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  bool interleaved=false;
  joinKernelAfterTimeoutRecord=[&](){
   interleaved=true;
   check(g_sgLatestSleepEntryFileTime.load()!=nextEntry,"publication: source high-water already advanced before the intended seam");
   if(kind=="manual"||kind=="new-human"){
    joinKernelXml=timeoutXml(kind=="manual"?507:506,1,nowFile,false);
    sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
   }
   if(kind=="success")sgSleepTimeoutRecordSystemSuccess(nowFile);
   if(kind=="off")g_sgRetrySleepTimeout=false;
   if(kind=="exit")g_exitRequested=true;
   if(kind=="new-pbt")suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
   handle507Native(16777220,oldExit);drainActualWake();
   if(!vetoes)check(g_sgSleepTimeoutWindow.active()&&g_sgSleepTimeoutWindow.origin==original&&
                    !requestCount&&g_sgAutomaticSleepBudget.requests()==0,
                    "publication: old507 GUI retired pending original between coherent record and source high-water");
  };
  joinKernelXml=timeoutXml(506,16777220,nextEntry,false);
  sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  check(interleaved&&!joinKernelAfterTimeoutRecord,"publication: real callback interleave not consumed once");
  handle506(16777220,nextEntry);drainActualWake();
  at(due);const auto queries=timeoutSourceQueries;sgSleepTimeoutTick();if(timerDue)dispatch();
  if(!vetoes)check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,"publication: source completion did not continue original deadline once");
  else check(!requestCount&&g_sgAutomaticSleepBudget.requests()==2&&timeoutSourceQueries==queries,
             "publication: pending observation crossed a callback/final veto or refunded original actions");
  observed.push_back(kind);
 }
 std::cout<<"OWNER_PUBLICATION_PARAMETERS "<<observed.dump()<<"\n";
 passed(vetoes?"owner-pending-observation-six-vetoes-before-source-publication":"owner-old507-between-record-and-source-publication-keeps-original");
}


void joinOwnerOverduePublication(bool entryUnconsumed,const char* veto=nullptr,bool report=true){
 baseCycle(true,false);joinInjectSuspendOnDispatch=joinInjectManualOnDispatch=false;
 const auto originalObserved=nowFile,start=nowTick;
 const auto original=originalObserved-485435ULL,oldExit=original+11609238ULL,nextEntry=original+11627607ULL;
 originalEntry(original);handle506(1,original);const auto due=timeoutTimerDue;
 if(veto)check(g_sgAutomaticSleepBudget.consume()&&g_sgAutomaticSleepBudget.consume(),"publication-overdue: original two charges missing");
 at(start+1117);resumeAutomatic();resumeInteractive();drainActualWake();
 at(start+1332);suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
 const auto receipt=g_sleepPowerDispatch.pendingReplay();check(bool(receipt),"publication-overdue: missing receipt");
 const auto claim=g_sleepPowerDispatch.forwarded(receipt.sequence,receipt.type,nowTick);check(bool(claim),"publication-overdue: unclaimed receipt");
 handleSuspendAdmission(PBT_APMSUSPEND,&claim);g_sleepPowerDispatch.complete(claim,nowTick);
 check(!sgSleepRetryActive()&&!timerDue,"previous interval: successor already owned retry before old507 GUI");
 at(start+2052);joinKernelXml=timeoutXml(507,16777220,oldExit,false);
 sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
 bool interleaved=false;
 joinKernelAfterTimeoutRecord=[&](){
  interleaved=true;check(g_sgLatestSleepEntryFileTime.load()!=nextEntry,"publication-overdue: intended source seam missed");
  handle507Native(16777220,oldExit);drainActualWake();
  check(!sgSleepRetryActive()&&!timerDue,"previous interval: old507 started successor EntryFailure retry before source publication");
  check(g_sgSleepTimeoutWindow.active(),"publication-overdue: GUI retired pending original before timer");
  at(due);sgSleepTimeoutTick();
  check(g_sgSleepTimeoutWindow.waiting&&g_sgSleepTimeoutWindow.origin==original&&!requestCount&&
        g_sgAutomaticSleepBudget.requests()==(veto?2U:0U)&&!timeoutTimerDue,
        "publication-overdue: original timer retired an in-progress known source publication");
 };
 joinKernelXml=timeoutXml(506,16777220,nextEntry,false);
 sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
 check(interleaved&&joinDeferredPrefixSupported,"publication-overdue: actual original WndProc prefix unavailable");
 // The existing ordinary WndProc ingress runs even when the specific entry
 // GUI message remains unconsumed. No new post, timer owner or poller is used.

 if(veto){
  const std::string kind=veto;const auto queries=timeoutSourceQueries;
  if(kind=="manual"||kind=="new-human"){
   joinKernelXml=timeoutXml(kind=="manual"?507:506,1,nowFile,false);
   sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  }
  if(kind=="success")sgSleepTimeoutRecordSystemSuccess(nowFile);
  if(kind=="off")g_sgRetrySleepTimeout=false;
  if(kind=="exit")g_exitRequested=true;
  if(kind=="new-pbt")suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
  joinActualDeferredWndProcPrefix();sgSleepTimeoutTick();if(timerDue)dispatch();
  check(!requestCount&&g_sgAutomaticSleepBudget.requests()==2&&!g_sgSleepTimeoutWindow.active()&&
        !timeoutTimerDue&&timeoutSourceQueries==queries&&frozen&&resumeCount==0,
        "publication-overdue: deferred owner crossed veto, refunded charges, queried or returned exact lease");
  if(report)passed("owner-overdue-one-deferred-veto");
  return;
 }
 joinActualDeferredWndProcPrefix();
 if(!entryUnconsumed){handle506(16777220,nextEntry);drainActualWake();}
 check(g_sgSleepTimeoutWindow.origin==original&&timeoutTimerDue&&timeoutTimerDue<=due+1&&
       !requestCount&&!g_sgAutomaticSleepBudget.requests(),"publication-overdue: original owner did not restore one due timer without action");
 at(timeoutTimerDue);sgSleepTimeoutTick();if(timerDue)dispatch();
 check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,"publication-overdue: restored original deadline did not send once");
 joinActualDeferredWndProcPrefix();sgSleepTimeoutTick();if(timerDue)dispatch();
 check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1&&!timeoutTimerDue,
       "publication-overdue: ordinary prefix replayed timer or refunded action");
 passed(entryUnconsumed?"owner-overdue-source-publication-ordinary-prefix-without-entry-handler":"owner-overdue-source-publication-restores-original-timer-once");
}


void joinOwnerOverdueVetoes(){
 json parameters=json::array();
 for(const char* kind:{"manual","success","off","exit","new-human","new-pbt"}){
  joinOwnerOverduePublication(true,kind,false);parameters.push_back(kind);
 }
 std::cout<<"OWNER_OVERDUE_VETO_PARAMETERS "<<parameters.dump()<<"\n";
 passed("owner-overdue-deferred-publication-six-vetoes");
}


void joinKernelDeliveryOrder(const std::string& kind){
 baseCycle(true,false);joinInjectSuspendOnDispatch=joinInjectManualOnDispatch=false;
 const auto observed=nowFile,start=nowTick,from=generation;
 const auto original=observed-485435ULL,oldExit=original+11609238ULL,nextEntry=original+11627607ULL;
 if(kind=="timeout-off"){g_sgRetrySleepTimeout=false;sgSleepTimeoutStop("controlled-existing-timeout-off");}
 originalEntry(original);handle506(1,original);const auto due=timeoutTimerDue;
 check(kind=="timeout-off"?!due:bool(due),"source order: initial independent timeout mode mismatch");
 at(start+1117);resumeAutomatic();resumeInteractive();drainActualWake();
 const auto receive=[&](){suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);};
 const auto consume=[&](){const auto r=g_sleepPowerDispatch.pendingReplay();check(bool(r),"source order: no pending receipt");const auto c=g_sleepPowerDispatch.forwarded(r.sequence,r.type,nowTick);check(bool(c),"source order: no claimed receipt");handleSuspendAdmission(PBT_APMSUSPEND,&c);g_sleepPowerDispatch.complete(c,nowTick);};
 const auto callbacks=[&](){
  joinKernelXml=timeoutXml(507,16777220,oldExit,false);sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  joinKernelXml=timeoutXml(506,16777220,nextEntry,false);sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
 };
 if(kind=="timeout-off"){
  at(start+1332);receive();consume();at(start+2052);
  joinKernelXml=timeoutXml(507,16777220,oldExit,false);sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  bool used=false;joinKernelAfterTimeoutRecord=[&](){used=true;handle507Native(16777220,oldExit);drainActualWake();
   check(!sgSleepRetryActive()&&!timerDue,"source order: timeout OFF let old507 start successor quick retry");};
  joinKernelXml=timeoutXml(506,16777220,nextEntry,false);sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  check(used&&!requestCount&&!g_sgAutomaticSleepBudget.requests()&&frozen&&resumeCount==0,"source order: feature OFF altered native lease or action account");
 }else{
  if(kind=="before-receipt"){at(start+1300);callbacks();at(start+1332);receive();}
  else{at(start+1332);receive();at(start+2052);callbacks();}
  consume();check(generation==from+1&&!sgSleepRetryActive()&&!requestCount,"source order: original owner did not admit one successor generation");
  handle507Native(16777220,oldExit);handle506(16777220,nextEntry);drainActualWake();
  at(due);sgSleepTimeoutTick();if(timerDue)dispatch();
  check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,
        "source order: original deadline lost when kernel source callback preceded Suspend GUI admission");
 }
 passed(kind=="timeout-off"?"native-old507-interval-attribution-independent-of-timeout-feature":kind=="before-receipt"?"owner-kernel-source-before-suspend-receipt-keeps-original-deadline":"owner-kernel-source-before-suspend-gui-keeps-original-deadline");
}

static std::string completedWin5Session(ULONGLONG stamp){
 auto xml=timeoutXml(507,5,stamp);
 xml.insert(xml.find("</EventData>"),"<Data Name=\"SleepEntered\">true</Data><Data Name=\"DurationInUs\">3286870421</Data><Data Name=\"SleepDurationInUs\">3286825339</Data><Data Name=\"IsCsSessionInProgressOnExit\">false</Data>");
 return xml;
}
static void joinWin5CompletedTimeout(){
 baseCycle(false,false);const auto origin=nowFile,entryTick=nowTick;
 joinKernelXml=timeoutXml(506,1,origin);sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
 at(entryTick+3286869);resumeAutomatic();resumeInteractive();drainActualWake();handle506(1,origin);
 check(g_sgSleepTimeoutWindow.waiting&&classificationTimerDue,"Win5 001301: overdue original timeout and classifier did not join");
 const auto exit=origin+32868694796ULL;at(entryTick+3286870);joinKernelXml=completedWin5Session(exit);
 sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
 check(sgSleepTimeoutSuccessAfter(origin),"Win5 001301: original callback did not settle the matching button intent");
 handle507Native(5,exit);drainActualWake();
 if(classificationTimerDue){at((std::max)(nowTick,classificationTimerDue));sgResumeClassificationTick();drainActualWake();}
 sgSleepTimeoutTick();if(timerDue)dispatch();
 check(!requestCount&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active()&&!timeoutTimerDue,
       "Win5 001301: classifier or overdue timer retained a second suspend request");

 baseCycle(false,false);const auto queryOrigin=nowFile,deadline=timeoutTimerDue;
 const auto queryExit=queryOrigin+32868694796ULL;
 timeoutSourceEvents={completedWin5Session(queryExit),timeoutXml(506,1,queryOrigin)};
 at((std::max)(deadline,nowTick+3286870));sgSleepTimeoutTick();drainTimeoutSource();sgSleepTimeoutTick();
 if(timerDue)dispatch();
 check(timeoutSourceQueries==1&&timeoutSourceHandles.empty()&&!requestCount&&!sgSleepTimeoutOriginalIntent(),
       "Win5 001301: bounded original-event source missed completed session or retained handles");

 // Drive a completion for the subsequent OS cycle after the original timer
 // has already sent one request. The first intent's accounting stays separate.
 for(const bool superseded:{false,true}){
  JoinRepairHooksScope hooks;joinNew506Arm(1,0);requestAccepted=true;
  const auto original=g_sgLastPowerButtonSleepIntentFileTime.load();
  dispatch();const auto request=g_sgInternalSleepRequest.dispatchFileTime;
  check(requestCount==1&&request>original&&!timerDue,"completed retry cycle: one accepted request");
  at(nowTick+1);const auto entry=nowFile;
  joinKernelXml=timeoutXml(506,0,entry,false);
  sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  handle506(0,entry);
  at(nowTick+20);const auto exit=nowFile;
  joinKernelXml=timeoutXml(507,32,exit,false);
  joinKernelXml.insert(joinKernelXml.find("</EventData>"),
   "<Data Name=\"SleepEntered\">true</Data><Data Name=\"DurationInUs\">20000</Data>"
   "<Data Name=\"SleepDurationInUs\">12000</Data><Data Name=\"IsCsSessionInProgressOnExit\">false</Data>");
  sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  check(sgSleepTimeoutSuccessAfter(entry)&&!sgSleepTimeoutSuccessAfter(original),
        "completed retry cycle: completion is bound to its exact entry");
  if(superseded){
   at(nowTick+1);joinKernelXml=timeoutXml(506,32,nowFile,false);
   sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
  }
  handle507Native(32,exit);handle507Native(32,exit);
  check(!timerDue&&requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,
        "completed retry cycle: callback, duplicate and superseded exit must not schedule or charge another retry");
  check(superseded ? sgSleepRetryActive() : !sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active(),
        "completed retry cycle: only the current exit can retire the old timeout owner");
 }
 passed("win5-001301-completed-session-retires-callback-and-query-timeout");
}
static void joinWin5ManualWithoutResidency(){
 const auto deliver=[](int id,int reason,ULONGLONG stamp){
  joinKernelXml=timeoutXml(id,reason,stamp);
  if(id==507)joinKernelXml.insert(joinKernelXml.find("</EventData>"),
   "<Data Name=\"SleepEntered\">false</Data><Data Name=\"SleepDurationInUs\">0</Data>"
   "<Data Name=\"ScreenOffDurationInUs\">8232626</Data>");
  sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(0xb01));
 };
 for(const bool queuedEntry:{false,true}){
  baseCycle(true,false);const auto origin=nowFile,start=nowTick,gen=generation;
  deliver(506,1,origin);
  if(!queuedEntry)handle506(1,origin);
  at(start+8233);resumeAutomatic();resumeInteractive();drainActualWake();
  const auto wake=origin+82326260ULL;deliver(507,1,wake);
  check(!sgSleepTimeoutSuccessAfter(origin),"Win5 223310: no-sleep manual exit counted as Windows completion");
  handle507Native(1,wake);drainActualWake();
  if(queuedEntry)handle506(1,origin);
  deliver(507,1,wake);handle507Native(1,wake);drainActualWake();
  at(start+240001);sgSleepTimeoutTick();if(timerDue)dispatch();
  check(!requestCount&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active()&&!timeoutTimerDue,
        "Win5 223310: manual abort or duplicate issued automatic sleep");
  check(!frozen&&resumeCount==1&&generation==gen&&g_powerLifecycle.load()==PowerLifecycle::Ready,
        "Win5 223310: manual abort did not restore the exact lease and Ready once");
 }
 // A bad original timestamp may veto work in this callback's current cycle.
 // It must not cancel a subsequent original sleep request.
 baseCycle(false,false);const auto wake=nowFile,scanTick=nowTick;
 deliver(507,1,wake+100000000ULL);
 at(scanTick+1);const auto next=nowFile;
 deliver(506,1,next);handle506(1,next);
 check(g_sgLastPowerButtonSleepIntentFileTime.load()==next&&sgSleepTimeoutOriginalIntent()==next,
       "manual future stamp: callback veto canceled a newer original 506");
 check(!sgUserWakeCancelsSleepIntent(next,g_sgLastPowerButtonWakeEventFileTime.load())&&
       !sgSleepTimeoutSuccessAfter(next),"manual future stamp: bad evidence leaked to successor");
 passed("win5-223310-manual-exit-without-residency");
}
int main(int argc,char** argv){try{
 if(argc>2||(argc==2&&argv[1][0]==0)){std::cerr<<"unknown or empty joined selector\n";return 3;}
 const std::string selected=argc==2?argv[1]:"";const bool suffix=selected.rfind("from:",0)==0;
 const auto exact=suffix?selected.substr(5):selected;bool reached=false;unsigned selectedCount=0;
 std::vector<std::string> selectedNames;json joinedCaseBindings=json::array();
 const auto run=[&](const char* name,const std::function<void()>& body){if(exact==name)reached=true;if(!selected.empty()&&(suffix?!reached:exact!=name))return;++selectedCount;const auto before=resumeCases.size();body();check(resumeCases.size()==before+1,"joined selection must produce one original case");selectedNames.emplace_back(name);joinedCaseBindings.push_back(json{{"selector",name},{"case",resumeCases.back()}});};
 run("retry-timer-failure",joinRetryTimerFailure);
 run("completed-s0-timeout",joinWin5CompletedTimeout);
 run("manual-s0-without-residency",joinWin5ManualWithoutResidency);
 run("field074830-timeout-reentry",joinField074830ReentryTimeout);
 run("field074830-new-suspend-veto",joinField074830NewBoundaryVeto);
 run("field074830-owner-matrix",joinField074830OwnerMatrix);
 run("field074830-replayed-interval",[]{joinField074830OwnerInterval(false);});
 run("field074830-fresh-interval",[]{joinField074830OwnerInterval(true);});
 run("owner-overlap-publication",[]{joinOwnerPublicationOverlap(false);});
 run("owner-overlap-vetoes",[]{joinOwnerPublicationOverlap(true);});
 run("owner-overdue-publication",[]{joinOwnerOverduePublication(false);});
 run("owner-overdue-entry-unconsumed",[]{joinOwnerOverduePublication(true);});
 run("owner-overdue-vetoes",joinOwnerOverdueVetoes);
 run("owner-source-timeout-off",[]{joinKernelDeliveryOrder("timeout-off");});
 run("owner-source-before-gui",[]{joinKernelDeliveryOrder("before-gui");});
 run("owner-source-before-receipt",[]{joinKernelDeliveryOrder("before-receipt");});
 run("duplicate-query-deadline",joinDuplicateQueryDeadline);
 run("retry-advance-exhaustion-observation",[]{joinRetryAdvanceObservation(true);});
 run("retry-advance-observation",[]{joinRetryAdvanceObservation();});
 run("return-public-observation",joinReturnPublicObservation);
 run("preparation-public-observation",joinPreparationPublicObservation);
 run("field-first-pid",[]{fieldScenario(false,true,0);});
 run("field-second-no-pid",[]{fieldScenario(false,false,1);});
 run("long-reason1-control",joinLongReason1Control);
 run("new-pbt-inside-sender",joinPendingDuringActualSender);
 run("new-suspend-before-old-timer",joinPendingBeforeOldTimer);
 run("exact-second-late-gui-2985537us",[]{fieldScenario(false,false,0,2985537ULL);});
 run("consumed-suspend-before-old-timer",joinConsumedSuspendBeforeOldTimer);
 run("duplicate-resume-before-retry",joinDuplicateResumeBeforeRetry);
 run("timeout-pbt-first",[]{timeoutObservationField(false);});
 run("timeout-late-original",[]{timeoutObservationField(true);});
#ifdef YMCC_TIMEOUT_OBSERVATION_ENABLE
 run("timeout-manual475",timeoutObservationManual475);
 run("timeout-veto-manual",[]{timeoutObservationVeto("manual");});
 run("timeout-veto-success",[]{timeoutObservationVeto("success");});
 run("timeout-veto-off",[]{timeoutObservationVeto("off");});
 run("timeout-veto-exit",[]{timeoutObservationVeto("exit");});
 run("timeout-veto-generation",[]{timeoutObservationVeto("generation");});
 run("timeout-veto-new-pbt",[]{timeoutObservationVeto("new-pbt");});
 run("timeout-source-unknown",[]{timeoutObservationSource("unknown");});
 run("timeout-source-non-button",[]{timeoutObservationSource("non-button");});
 run("timeout-source-old-entry",[]{timeoutObservationSource("old-entry");});
 run("timeout-source-new-entry",[]{timeoutObservationSource("new-entry");});
 run("timeout-source-manual-exit",[]{timeoutObservationSource("manual-exit");});
 run("timeout-source-success-exit",[]{timeoutObservationSource("success-exit");});
 run("timeout-source-render-failed",[]{timeoutObservationSource("render-failed");});
 run("timeout-source-query-failed",[]{timeoutObservationSource("query-failed");});
 run("timeout-source-pool-busy",[]{timeoutObservationSource("pool-busy");});
 run("timeout-duplicate-and-early",timeoutObservationDuplicate);
 run("timeout-initial-s4",[]{timeoutObservationAction(static_cast<int>(SgSleepMode::S4));});
 run("timeout-initial-unknown",[]{timeoutObservationAction(static_cast<int>(SgSleepMode::Unknown));});
 run("timeout-query-expired",timeoutObservationLateCompletion);
 run("timeout-final-manual-veto",timeoutObservationFinalManual);
 run("timeout-query-interrupted",timeoutObservationSourceInterruption);
 run("timeout-query-max-eight",[]{
  baseCycle(false,false);timeoutSourceEvents.assign(9,timeoutXml(507,32,nowFile));
  at(timeoutTimerDue);sgSleepTimeoutTick();drainTimeoutSource();sgSleepTimeoutTick();
  check(timeoutSourceQueries==1&&timeoutSourceHandles.empty()&&!requestCount&&!timeoutTimerDue,"bounded no-entry query exceeded one batch or leaked");
  passed("timeout-query-eight-event-limit-without-original");
 });
 run("timeout-completed-late",[]{timeoutObservationCompletionDeadline(true);});
 run("timeout-completed-ui-late",[]{timeoutObservationCompletionDeadline(false);});
 run("timeout-query-failure-retry",[]{timeoutObservationQueryFailure(true);});
 run("timeout-query-failure-pending",[]{timeoutObservationQueryFailure(false);});
 run("timeout-refined-reason3",[]{timeoutObservationReason3Cache(true);});
 run("timeout-cached-reason3",[]{timeoutObservationReason3Cache(false);});
#endif
 run("receipt-query-230s",[]{receiptDelayed(true,230000);});
 run("receipt-suspend-230s",[]{receiptDelayed(false,230000);});
 run("receipt-query-475s",[]{receiptDelayed(true,475000);});
 run("receipt-suspend-475s",[]{receiptDelayed(false,475000);});
 run("receipt-query-confirmation",receiptConfirmation);
 run("receipt-late-original",receiptOriginal);
 run("receipt-manual238",receiptManual238);
 run("receipt-success",receiptSuccess);
 run("receipt-new-suspend",receiptNewSuspend);
 run("receipt-unknown-time",[]{receiptInvalid(false);});
 run("receipt-future-time",[]{receiptInvalid(true);});
 run("receipt-source-missing",receiptMissing);
 run("receipt-settled-late-original",[]{receiptSettled(true);});
 run("receipt-settled-source",[]{receiptSettled(false);});
 run("receipt-settled-cancel",receiptSettledCancel);
 run("receipt-settled-proof",receiptSettledProof);
 run("receipt-settled-budget",receiptSettledBudget);
 run("receipt-settled-arm-refused",receiptSettledArmRefused);
 run("receipt-settled-retired-original",receiptSettledRetiredOriginal);
 run("consumer-delayed-manual443",consumerDelayedManual443);
 run("consumer-classification-expiry",[]{consumerClassificationExpiry(false);});
 run("consumer-classification-duplicates",[]{consumerClassificationExpiry(true);});
 run("consumer-classification-veto",consumerClassificationVeto);
 run("consumer-no-classification",consumerNoClassification);
 run("consumer-remaining-budget",consumerRemainingBudget);
 run("consumer-scheduled-manual",[]{consumerResumeAfterScheduled(true);});
 run("consumer-scheduled-unknown",[]{consumerResumeAfterScheduled(false);});
 run("consumer-return-timer-failed",[]{consumerReturnTimerFailure(false);});
 run("consumer-retry-timer-failed",[]{consumerReturnTimerFailure(true);});
 run("late-entry-rog193736-field",joinLateEntryField);
 run("late-entry-display-five-second",joinLateEntryDisplay);
 run("late-entry-veto-manual-before-gui",[]{joinLateEntryVeto("manual-before-gui");});
 run("late-entry-veto-success",[]{joinLateEntryVeto("success");});
 run("late-entry-veto-settings-off-on",[]{joinLateEntryVeto("settings-off-on");});
 run("late-entry-veto-display-off-on",[]{joinLateEntryVeto("display-off-on");});
 run("late-entry-veto-new-suspend",[]{joinLateEntryVeto("new-suspend");});
 run("late-entry-veto-new506",[]{joinLateEntryVeto("new506");});
 run("late-entry-veto-expired",[]{joinLateEntryVeto("expired");});
 run("late-entry-duplicates",joinLateEntryDuplicates);
 run("repair-old-worker-terminal-interleave",joinRepairOldWorker);
 run("repair-timeout-off-nonuser-duplicate-pbt",joinRepairTimeoutOffDuplicate);
 run("repair-veto-guard-off-on-stages",[]{joinRepairVetoStages("guard-off-on");});
 run("repair-veto-entry-off-on-stages",[]{joinRepairVetoStages("entry-off-on");});
 run("repair-veto-manual-callback-stages",[]{joinRepairVetoStages("manual-callback");});
 run("repair-veto-windows-success-stages",[]{joinRepairVetoStages("windows-success");});
 run("repair-veto-new-off-stages",[]{joinRepairVetoStages("new-off");});
 run("repair-veto-new506-stages",[]{joinRepairVetoStages("new506");});
 run("repair-timeout-off-late506-then507",joinRepairLate506ThenExit);
 run("repair-timeout-off507-on-before506",joinRepairEvidenceBefore506);
 run("repair-timeout-off-unknown-nonbutton506",joinRepairUnknownOrNonButton506);
 run("repair-retired-invalid-original506",joinRepairRetiredOrInvalidOriginal506);
#ifdef YMCC_FIXTURE_ENTRY_HANDOFF_READY_ENABLE
 run("repair-real-mutex-handoff",joinRepairBusyMutexHandoff);
#endif
 run("repair-header-recovery-prefix",joinRepairWakeRecoveryHeaderPrefix);
#ifdef YMCC_FIXTURE_ENTRY_HANDOFF_READY_ENABLE
 run("repair-ready-handoff-delivery",joinRepairReadyDeliveryModes);
#endif

 run("admission-cleanup-timeout",[]{joinAdmissionCleanup(false);});
 run("admission-cleanup-unexpected",[]{joinAdmissionCleanup(true);});
 run("admission-timeout-vetoes",[]{joinAdmissionVetoes(false);});
 run("admission-unexpected-vetoes",[]{joinAdmissionVetoes(true);});
 run("admission-publication-only",joinAdmissionPublication);
 run("admission-classification-keeps-unexpected",joinAdmissionClassification);
 run("boundary-nonentry-delivery",joinNonEntryDeliveryBoundary);
 run("boundary-nonentry-coalescing",joinNonEntryCoalescingBoundary);
 run("drips-direct-only",joinDripsAuxiliaryDirect);
 run("drips-source-only",joinDripsAuxiliarySource);
 run("boundary-unexpected-new-success",joinUnexpectedNewSuccess);
 run("boundary-unexpected-resume-witness",joinUnexpectedResumeWitness);
 if(!selectedCount){std::cerr<<"unknown or empty joined selector\n";return 3;}
 std::cout<<"RESUME_SENDER_JOIN "<<json{{"cases",resumeCases},{"count",resumeCases.size()},{"assertions",joinAssertions},{"selectors",selectedNames},{"caseBindings",joinedCaseBindings},{"actualSender",true},{"actualReader",true},{"actualCommonAndWorkers",true},{"realSystemSleep",false}}.dump()<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<"ASSERTION: "<<e.what()<<"\n";return 2;}}
