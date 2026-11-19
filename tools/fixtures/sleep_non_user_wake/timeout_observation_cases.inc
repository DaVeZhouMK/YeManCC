static void timeoutObservationField(bool deliverCallback=false){
 baseCycle(true,false);const auto gen=generation,origin=nowFile;
 check(timeoutTimerDue!=0,"PBT-first sleep lost the original 240-second pending observation");
#ifdef YMCC_TIMEOUT_OBSERVATION_ENABLE
 check(g_sgSleepTimeoutObservation.waiting&&!g_sgSleepTimeoutWindow.active()&&!requestCount,"PBT observation became an action permit");
 const auto deadline=timeoutTimerDue;
 if(deliverCallback){at(nowTick+230000);originalEntry(origin);sgRefreshSleepTriggerOrigin();handle506(1,origin);
  check(timeoutTimerDue==deadline&&sgSleepTimeoutOriginalIntent()==origin,"late original506 refunded or lost observed deadline");
 }else timeoutSourceEvents={timeoutXml(506,1,origin)};
 at(deadline);sgSleepTimeoutTick();
 if(!deliverCallback){check(timeoutSourceQueries==0&&timeoutSourceWork.size()==1&&!requestCount,"deadline did not offer exactly one inert source read");drainTimeoutSource();sgSleepTimeoutTick();}
 check(g_sgTask.generation==gen&&sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::SleepTimeout,"original deadline failed to join original sleep retry");
 dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,"confirmed overdue original did not dispatch and charge exactly once");
 sgSleepTimeoutTick();sgDispatchSameModeRetry();check(requestCount==1,"completed timeout replayed automatic suspend");
#endif
 passed(deliverCallback?"timeout-late-original506-preserves-pbt-deadline":"timeout-pbt-first-original-source-reaches-actual-sender");
}
#ifdef YMCC_TIMEOUT_OBSERVATION_ENABLE
static void timeoutObservationManual475(){
 baseCycle(true,false);const auto origin=nowFile;at(nowTick+474890);originalEntry(origin);
 sgRefreshSleepTriggerOrigin();resumeAutomatic();resumeInteractive();
 check(sgResumeClassificationOwned(generation)&&timeoutTimerDue,"475-second late original lost exact classification hold or deadline");
 timeoutSourceEvents={timeoutXml(507,1,nowFile),timeoutXml(506,1,origin)};
 sgSleepTimeoutTick();drainTimeoutSource();sgSleepTimeoutTick();
 g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;g_sgLastKernel507EventFileTime=nowFile;
 handle507Native(1,nowFile);drainActualWake();
 check(!requestCount&&!frozen&&resumeCount==1&&!timeoutTimerDue,"475-second manual wake was re-slept or retained game lease");
 passed("timeout-field475s-manual-wake-stops-original-observation");
}
static void timeoutObservationVeto(const std::string& boundary){
 baseCycle(false,false);const auto origin=nowFile,deadline=timeoutTimerDue;
 timeoutSourceEvents={timeoutXml(506,1,origin)};
 at(deadline);sgSleepTimeoutTick();check(timeoutSourceWork.size()==1,"missing one pending query");
 if(boundary=="manual")g_sgLastPowerButtonWakeEventFileTime=nowFile;
 else if(boundary=="success")sgSleepTimeoutRecordSystemSuccess(nowFile);
 else if(boundary=="off")g_sgRetrySleepTimeout=false;
 else if(boundary=="exit")g_exitRequested=true;
 else if(boundary=="generation")++generation;
 else if(boundary=="new-pbt")suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
 drainTimeoutSource();sgSleepTimeoutTick();
 check(!requestCount&&!timeoutTimerDue&&!g_sgSleepTimeoutObservation.waiting,"pending timeout survived original cancellation");
 passed("timeout-pending-source-canceled-"+boundary);
}
static void timeoutObservationSource(const std::string& kind){
 baseCycle(false,false);const auto origin=nowFile,deadline=timeoutTimerDue;
 if(kind=="unknown")timeoutSourceEvents={};
 else if(kind=="non-button")timeoutSourceEvents={timeoutXml(506,3,origin)};
 else if(kind=="old-entry")timeoutSourceEvents={timeoutXml(506,1,origin-31000*10000ULL)};
 else if(kind=="new-entry")timeoutSourceEvents={timeoutXml(506,1,origin+10000ULL)};
 else if(kind=="manual-exit")timeoutSourceEvents={timeoutXml(507,1,origin+10000ULL),timeoutXml(506,1,origin)};
 else if(kind=="success-exit")timeoutSourceEvents={timeoutXml(507,20,origin+10000ULL,false),timeoutXml(506,1,origin)};
 else if(kind=="render-failed"){timeoutSourceEvents={timeoutXml(506,1,origin)};timeoutSourceRenderAllowed=false;}
 else if(kind=="query-failed")timeoutSourceQueryAllowed=false;
 else if(kind=="pool-busy")timeoutSourcePoolAllowed=false;
 at(deadline);sgSleepTimeoutTick();drainTimeoutSource();sgSleepTimeoutTick();
 check(!requestCount&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active()&&!timeoutTimerDue&&!g_sgSleepTimeoutObservation.waiting&&timeoutSourceHandles.empty(),"unconfirmed/vetoed source dispatched or retained timer/handles");
 check(timeoutSourceQueries<=1,"unconfirmed timeout source repeated discovery");
 passed("timeout-source-rejects-"+kind);
}
static void timeoutObservationDuplicate(){
 baseCycle(false,false);const auto origin=nowFile,deadline=timeoutTimerDue;
 at(nowTick+1000);sgSleepTimeoutObserveSuspend();sgSleepTimeoutObserveSuspend();
 check(timeoutTimerDue==deadline&&g_sgSleepTimeoutObservation.origin==origin,"duplicate PBT refunded pending deadline");
 at(deadline-1);sgSleepTimeoutTick();check(timeoutTimerDue==deadline&&!timeoutSourceQueries&&!requestCount,"early pending timer accelerated source or request");
 sgSleepTimeoutStop("explicit-cancel");sgSleepTimeoutObserveSuspend();
 check(!timeoutTimerDue&&!g_sgSleepTimeoutObservation.waiting,"same canceled observation was revived");
 passed("timeout-pending-duplicate-early-and-retired-original");
}
static void timeoutObservationAction(int action){
 baseCycle(false,false);sgSleepTimeoutStop("case-initial-observation");g_sgSleepTimeoutObservation={};
 g_sgTask.mode=static_cast<SgSleepMode>(action);sgSleepTimeoutObserveSuspend();
 check(!timeoutTimerDue&&!g_sgSleepTimeoutObservation.waiting&&!requestCount,"nonSleep initial action armed observation");
 passed("timeout-initial-nonSleep-action-"+std::to_string(action));
}
static void timeoutObservationLateCompletion(){
 baseCycle(false,false);const auto origin=nowFile;timeoutSourceEvents={timeoutXml(506,1,origin)};
 at(timeoutTimerDue);sgSleepTimeoutTick();const auto job=g_sgSleepTimeoutObservation.job;
 at(nowTick+2001);sgSleepTimeoutTick();
 check(!g_sgSleepTimeoutObservation.waiting&&!timeoutTimerDue&&!requestCount,"source completion deadline was extended");
 drainTimeoutSource();sgSleepTimeoutTick();
 check(job->complete.load()&&!timeoutTimerDue&&!requestCount&&timeoutSourceQueries==1,"late worker resurrected retired request or repeated query");
 passed("timeout-source-expiry-and-late-completion-return");
}
static void timeoutObservationFinalManual(){
 baseCycle(false,false);timeoutSourceEvents={timeoutXml(506,1,nowFile)};
 at(timeoutTimerDue);sgSleepTimeoutTick();drainTimeoutSource();sgSleepTimeoutTick();
 check(sgSleepRetryActive(),"final manual test did not reach original retry");
 joinInjectManualOnDispatch=true;dispatch();
 if(requestCount||sgSleepRetryActive()||timeoutTimerDue)std::cerr<<"FINAL_MANUAL_STATE "<<json{{"api",requestCount},{"retry",sgSleepRetryActive()},{"timeoutDue",timeoutTimerDue},{"manualInjected",!joinInjectManualOnDispatch},{"facts",facts}}.dump()<<"\n";
 check(!requestCount&&!sgSleepRetryActive()&&!timeoutTimerDue,"final dispatch log manual wake reached Windows sleep call");
 passed("timeout-final-sender-manual-veto-after-log");
}
static void timeoutObservationSourceInterruption(){
 baseCycle(false,false);timeoutSourceEvents={timeoutXml(506,1,nowFile)};
 timeoutSourceAfterRender=[]{suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);};
 at(timeoutTimerDue);sgSleepTimeoutTick();drainTimeoutSource();sgSleepTimeoutTick();
 check(!requestCount&&!timeoutTimerDue&&timeoutSourceHandles.empty(),"new native receipt during event query leaked or authorized");
 passed("timeout-query-return-rechecks-native-notification");
}

static void timeoutObservationReason3Cache(bool refined){
 baseCycle(false,false);const auto origin=nowFile,deadline=timeoutTimerDue;
 if(refined){
  const auto entry=origin-10000ULL;sgRememberKernelSleepEntry(3,entry);g_sgLastKernel506Reason=3;g_sgLastKernel506EventFileTime=entry;
  sgRefreshSleepTriggerOrigin();
 }else{
  sgSleepTimeoutStop("cache-proof-setup");g_sgSleepTimeoutObservation={};
  sgRememberKernelSleepEntry(3,origin);g_sgLastKernel506Reason=3;g_sgLastKernel506EventFileTime=origin;
  sgSleepTimeoutObserveSuspend();
 }
 check(!g_sgSleepTimeoutWindow.active()&&!sgSleepRetryActive()&&!requestCount&&timeoutTimerDue==deadline,"Reason3 cached timestamp manufactured original button proof");
 passed(refined?"timeout-refined-Reason3-is-observation-only":"timeout-cached-Reason3-is-observation-only");
}

static void timeoutObservationCompletionDeadline(bool workerLate){
 baseCycle(false,false);timeoutSourceEvents={timeoutXml(506,1,nowFile)};
 at(timeoutTimerDue);sgSleepTimeoutTick();const auto job=g_sgSleepTimeoutObservation.job;
 check(bool(job),"completion deadline case did not submit original read");
 if(workerLate)timeoutSourceAfterRender=[]{at(nowTick+2001);};
 drainTimeoutSource();if(!workerLate)at(nowTick+2001);
 check(job->complete.load(),"worker did not complete before delayed UI");
 sgSleepTimeoutTick();
 check(!requestCount&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active()&&!timeoutTimerDue&&!g_sgSleepTimeoutObservation.waiting,"expired completed result authorized or retained timeout");
 passed(workerLate?"timeout-worker-completed-after-deadline-before-UI":"timeout-timely-worker-result-expired-before-UI");
}
static void timeoutObservationQueryFailure(bool active){
 baseCycle(false,false);const auto origin=nowFile;
 if(active){
  timeoutSourceEvents={timeoutXml(506,1,origin)};at(timeoutTimerDue);sgSleepTimeoutTick();drainTimeoutSource();sgSleepTimeoutTick();dispatch();
  check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1&&sgSleepRetryActive(),"query failure test did not reach first original send");
  timeoutQueryFailureActiveBranch();
  check(sgSleepRetryActive()&&g_sgSleepTimeoutWindow.requested&&!timerDue,"unbound query lost the original request or armed another");
  sgDispatchSameModeRetry();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,"unbound query added a send or refunded shared budget");
 }else{
  timeoutQueryFailureActiveBranch();
  check(!timeoutTimerDue&&!g_sgSleepTimeoutObservation.waiting&&!g_sgSleepTimeoutWindow.active()&&!requestCount,"query failure retained an unconfirmed observation");
 }
 passed(active?"timeout-query-rejection-keeps-original-retry-budget":"timeout-query-rejection-retires-unconfirmed-observation");
}

#endif

// Public ingress origin successor; all actual power/Windows calls remain counted ports.
static void receiptFresh(){
 baseCycle(false,false);sgSleepTimeoutStop("fixture-empty-cycle");g_sgSleepTimeoutObservation={};g_sgSleepTimeoutWindow={};
 g_sgTask={};g_sgRepairEligible=false;g_sgSleepIntentArmed=false;g_sgSleepCycleActive=false;g_sgSleepTriggerTick=0;
 g_sgLastPowerButtonSleepIntentFileTime=0;g_sgClosedSleepIntentFileTime=0;g_sgLatestUserSleepEntryFileTime=0;
 g_sgSleepQueryGateClosed=g_sgSleepQueryOwnsGeneration=false;g_sgSleepQueryGeneration=0;
 g_powerLifecycle=PowerLifecycle::Ready;g_lastSuspendNotifyTick=g_lastSuspendReceiptTick=0;g_resumeReadyGeneration=0;
 g_sleepPowerDispatch.~Dispatch();new (&g_sleepPowerDispatch) ymcc::sleeppower::Dispatch();
}
static ymcc::sleeppower::Dispatch::Receipt receiptAdmit(bool query,ULONGLONG delay){
 const auto code=query?PBT_APMQUERYSUSPEND:PBT_APMSUSPEND;
 ymcc::sleeppower::Dispatch::Receipt original;
 g_sleepPowerDispatch.callback(code,nowTick,[&](const auto& r){original=r;return true;},nowFile);
 at(nowTick+delay);
 const auto received=g_sleepPowerDispatch.forwarded(original.sequence,code,nowTick);
 check(bool(received)&&received.tick==original.tick&&received.fileTime100ns==original.fileTime100ns,"actual Dispatch lost original ingress");
 if(query)handleQueryAdmission(&received);else handleSuspendAdmission(code,&received);
 return original;
}
static void receiptDelayed(bool query,ULONGLONG delay){
 receiptFresh();const auto original=receiptAdmit(query,delay);const auto gen=generation;
 check(g_sgSleepTriggerTick==original.tick,"GUI delay replaced original monotonic trigger");
 check(g_sgLastPowerButtonSleepIntentFileTime.load()==original.fileTime100ns,"GUI delay replaced original observed intent");
 const auto expected=delay>=240000?nowTick+1:original.tick+240000;
 check(timeoutTimerDue==expected&&g_sgSleepTimeoutObservation.waiting&&!requestCount,"late public receipt delayed240s or granted action");
 timeoutSourceEvents={timeoutXml(506,1,original.fileTime100ns)};
 at(expected);sgSleepTimeoutTick();drainTimeoutSource();sgSleepTimeoutTick();
 check(generation==gen&&sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::SleepTimeout,"original elapsed deadline did not reach same Native retry");
 dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,"original source must dispatch and charge exactly once");
 sgSleepTimeoutTick();sgDispatchSameModeRetry();check(requestCount==1,"queued duplicate replayed original request");
 passed(std::string("public-receipt-")+(query?"query":"suspend")+"-delay"+std::to_string(delay));
}
static void receiptConfirmation(){
 receiptFresh();const auto original=receiptAdmit(true,230000);const auto due=timeoutTimerDue,gen=generation;
 at(nowTick+5000);handleSuspendAdmission();check(timeoutTimerDue==due&&generation==gen,"suspend confirmation reset original query deadline/generation");
 at(nowTick+5000);handleSuspendAdmission();check(timeoutTimerDue==due&&generation==gen,"duplicate confirmation reset original query deadline/generation");
 check(g_sgSleepTimeoutObservation.origin==original.fileTime100ns&&!requestCount,"confirmation authorized original observation");
 passed("public-query-confirm-and-duplicate-preserve-first-deadline");
}
static void receiptOriginal(){
 receiptFresh();const auto original=receiptAdmit(false,230000);const auto due=timeoutTimerDue,gen=generation;
 originalEntry(original.fileTime100ns);handle506(1,original.fileTime100ns);
 check(timeoutTimerDue==due&&sgSleepTimeoutOriginalIntent()==original.fileTime100ns&&generation==gen,"late original506 restarted elapsed window or Native generation");
 passed("public-delayed-suspend-late506-same-deadline-and-generation");
}
static void receiptManual238(){
 receiptFresh();const auto original=receiptAdmit(false,230000);at(original.tick+238177);
 g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;
 resumeAutomatic();resumeInteractive();drainActualWake();at(original.tick+240000);sgSleepTimeoutTick();
 check(!requestCount&&!timeoutTimerDue&&!g_sgSleepTimeoutObservation.waiting&&!sgSleepRetryActive(),"238s human wake must veto original240s");
 passed("public-delayed-suspend-manual238-veto");
}
static void receiptSuccess(){
 receiptFresh();const auto original=receiptAdmit(false,230000);sgSleepTimeoutRecordSystemSuccess(nowFile);
 at(original.tick+240000);sgSleepTimeoutTick();check(!requestCount&&!timeoutTimerDue&&!g_sgSleepTimeoutObservation.waiting,"Windows success must veto original observed deadline");
 passed("public-delayed-suspend-Windows-success-veto");
}
static void receiptNewSuspend(){
 receiptFresh();const auto original=receiptAdmit(false,230000);const auto due=timeoutTimerDue;
 suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
 at(due);sgSleepTimeoutTick();drainTimeoutSource();sgSleepTimeoutTick();
 check(!requestCount&&!timeoutTimerDue&&!g_sgSleepTimeoutObservation.waiting,"new pending Native suspend must revoke old deadline action");
 passed("public-delayed-suspend-new-ingress-revokes-old-source");
}
static void receiptInvalid(bool future){
 for(bool nonUser:{false,true}){
 receiptFresh();g_sgResleepEnabled=nonUser;const auto code=PBT_APMSUSPEND;
 const auto received=g_sleepPowerDispatch.window(code,future?nowTick+1:0,future?nowFile+10000:0);
 handleSuspendAdmission(code,&received);
 check(!timeoutTimerDue&&!g_sgSleepTimeoutObservation.waiting&&!requestCount,"unknown/future ingress manufactured fresh timeout origin");
 }
 passed(future?"public-future-receipt-does-not-manufacture-origin":"public-unknown-receipt-does-not-manufacture-origin");
}
static void receiptMissing(){
 receiptFresh();const auto original=receiptAdmit(false,230000);at(original.tick+240000);sgSleepTimeoutTick();drainTimeoutSource();sgSleepTimeoutTick();
 check(timeoutSourceQueries==1&&!requestCount&&!timeoutTimerDue&&!g_sgSleepTimeoutObservation.waiting&&timeoutSourceHandles.empty(),"missing original proof repeated query or dispatched");
 passed("public-delayed-receipt-missing-original-source-closes-once");
}

static ymcc::sleeppower::Dispatch::Receipt receiptSettledSetup(){
 receiptFresh();const auto original=receiptAdmit(false,0);const auto gen=generation;
 at(nowTick+1000);resumeAutomatic();resumeInteractive();drainActualWake();
 check(classificationTimerDue!=0,"missing bounded original classification");
 at(classificationTimerDue);sgResumeClassificationTick();drainActualWake();
 check(g_powerLifecycle==PowerLifecycle::Ready&&g_sgTask.generation==0&&!frozen&&!classificationTimerDue,"classification settlement retained original resource task");
 check(generation==gen&&g_sgSleepTimeoutObservation.waiting&&timeoutTimerDue==original.tick+240000,"unknown classification erased original observation/deadline");
 at(original.tick+30000);resumeAutomatic();resumeInteractive();drainActualWake();
 check(generation==gen&&timeoutTimerDue==original.tick+240000,"late duplicate resume replaced settled observation generation/deadline");
 return original;
}
static void receiptSettled(bool originalCallback){
 const auto original=receiptSettledSetup();const auto gen=generation,due=timeoutTimerDue;
 if(originalCallback){at(original.tick+230000);originalEntry(original.fileTime100ns);handle506(1,original.fileTime100ns);
  check(generation==gen&&g_sgTask.generation==0&&timeoutTimerDue==due,"late original506 reopened shared resource task/deadline");
 }else timeoutSourceEvents={timeoutXml(506,1,original.fileTime100ns)};
 at(due);sgSleepTimeoutTick();if(!originalCallback){drainTimeoutSource();sgSleepTimeoutTick();}
 check(sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::SleepTimeout&&generation==gen,"settled original failed same-owner timeout admission");
 dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,"settled source did not use exactly one original sender/budget");
 passed(originalCallback?"public-settled-resource-late506-keeps-original240s":"public-settled-resource-source-read-keeps-original240s");
}
static void receiptSettledCancel(){
 for(const auto kind:{"manual","success","off","exit","generation","new-pbt","query-failed"}){
  const auto original=receiptSettledSetup();const auto due=timeoutTimerDue;
  if(std::string(kind)=="manual")g_sgLastPowerButtonWakeEventFileTime=nowFile;
  else if(std::string(kind)=="success")sgSleepTimeoutRecordSystemSuccess(nowFile);
  else if(std::string(kind)=="off")g_sgRetrySleepTimeout=false;
  else if(std::string(kind)=="exit")g_exitRequested=true;
  else if(std::string(kind)=="generation")++generation;
  else if(std::string(kind)=="new-pbt")suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
  else timeoutQueryFailureActiveBranch();
  timeoutSourceEvents={timeoutXml(506,1,original.fileTime100ns)};
  at(due);sgSleepTimeoutTick();drainTimeoutSource();sgSleepTimeoutTick();
  check(!requestCount&&!timeoutTimerDue&&!g_sgSleepTimeoutObservation.waiting&&!g_sgSleepTimeoutWindow.active(),"settled observation survived real veto");
 }
 passed("public-settled-observation-seven-real-cancellation-boundaries");
}

static void receiptSettledProof(){
 for(const auto kind:{"reason3","too-old","after-observation","closed","newer-original"}){
  const auto original=receiptSettledSetup();auto entry=original.fileTime100ns;int reason=1;
  if(std::string(kind)=="reason3")reason=3;
  else if(std::string(kind)=="too-old")entry-=31000*10000ULL;
  else if(std::string(kind)=="after-observation")entry+=10000;
  else if(std::string(kind)=="closed")g_sgClosedSleepIntentFileTime=entry;
  sgRememberKernelSleepEntry(reason,entry);
  if(std::string(kind)=="newer-original")sgRememberKernelSleepEntry(1,entry+10000);
  check(!sgSleepTimeoutJoinEntry(reason,entry)&&!g_sgSleepTimeoutWindow.active()&&!requestCount,"settled observation borrowed unproved/wrong/closed original");
 }
 passed("public-settled-observation-five-original-evidence-fences");
}
static void receiptSettledBudget(){
 const auto original=receiptSettledSetup();const auto due=timeoutTimerDue;
 check(g_sgAutomaticSleepBudget.consume()&&g_sgAutomaticSleepBudget.consume(),"prior cumulative budget not prepared");
 originalEntry(original.fileTime100ns);handle506(1,original.fileTime100ns);
 check(g_sgAutomaticSleepBudget.requests()==2&&timeoutTimerDue==due,"late original506 refunded settled action budget/deadline");
 at(due);sgSleepTimeoutTick();requestAccepted=false;dispatch();drainActualWake();
 check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==3&&!sgSleepRetryActive(),"settled retry exceeded remaining original action budget");
 sgSleepTimeoutTick();sgDispatchSameModeRetry();check(requestCount==1,"settled exhausted budget replayed original request");
 passed("public-settled-original-retains-two-spent-slots-and-final-third-only");
}

static void checkReturnedTimeoutObservationRefused(bool newerEntry){
 baseCycle(false,false);const auto original=nowFile,gen=generation;
 timeoutSourceEvents={timeoutXml(506,1,original)};
 at(timeoutTimerDue);sgSleepTimeoutTick();const auto returned=g_sgSleepTimeoutObservation.job;
 drainTimeoutSource();
 check(returned&&returned->complete.load()&&returned->entry==original&&timeoutSourceQueries==1,
       "bounded read did not return the original 506");
 check(sgSleepTimeoutObservationHeld(gen)&&!g_sgSleepTimeoutObservation.classificationSettled,
       "fixture requires an unsettled observation for the original generation");
 if(newerEntry){
  g_sgLatestSleepEntryFileTime.store(original+10000);
  sgRememberKernelSleepEntry(1,original+10000);
 }else suspendConfiguredMode=SgSleepMode::S4;
 sgSleepTimeoutTick();
 check(!requestCount&&!sgSleepRetryActive()&&!timeoutTimerDue&&timeoutSourceHandles.empty(),
       "refused timeout readback sent or retained a timer or Windows handle");
 check(!sgSleepTimeoutObservationHeld(gen)&&!g_sgSleepTimeoutObservation.waiting&&!g_sgSleepTimeoutObservation.joined&&
       !g_sgSleepTimeoutObservation.job&&!g_sgSleepTimeoutWindow.active(),
       newerEntry?"newer 506 left the completed timeout observation held":"current button action left the completed timeout observation held");
 check(generation==gen&&g_sgClosedSleepIntentFileTime.load()<original,
       "local timeout refusal changed the public generation or closed its intent");
}

static void receiptSettledArmRefused(){
 const auto original=receiptSettledSetup();const auto gen=generation;
 at(original.tick+230000);suspendConfiguredMode=SgSleepMode::S4;originalEntry(original.fileTime100ns);handle506(1,original.fileTime100ns);
 check(generation==gen&&g_sgTask.generation==0&&!g_sgSleepTimeoutWindow.active()&&!requestCount,"matched late original whose arm was refused reopened shared sleep cycle");
 check(!timeoutTimerDue&&!g_sgSleepTimeoutObservation.waiting,"matched refused original kept an unusable observation");
 checkReturnedTimeoutObservationRefused(false);
 checkReturnedTimeoutObservationRefused(true);
 passed("public-settled-matched-original-final-action-refusal-stays-consumed");
}
static void receiptSettledRetiredOriginal(){
 for(const auto kind:{"off","query-failed","source-unknown","new-cycle-first"}){
  const auto original=receiptSettledSetup();const auto gen=generation;
  if(std::string(kind)=="new-cycle-first"){
   at(nowTick+1000);const auto fresh=nowFile;originalEntry(fresh);handle506(1,fresh);
   check(generation==gen+1&&sgSleepTimeoutOriginalIntent()==fresh,"new original did not replace settled observation normally");
   handle506(1,original.fileTime100ns);
   check(generation==gen+1&&g_sgLastPowerButtonSleepIntentFileTime.load()==fresh&&sgSleepTimeoutOriginalIntent()==fresh,"old original overwrote a newer Native cycle");
   continue;
  }
  if(std::string(kind)=="off"){g_sgRetrySleepTimeout=false;sgSleepTimeoutStop("feature-off");}
  else if(std::string(kind)=="query-failed")timeoutQueryFailureActiveBranch();
  else{at(timeoutTimerDue);sgSleepTimeoutTick();drainTimeoutSource();sgSleepTimeoutTick();}
  originalEntry(original.fileTime100ns);handle506(1,original.fileTime100ns);
  check(generation==gen&&g_sgTask.generation==0&&!g_sgSleepTimeoutWindow.active()&&!requestCount,"retired observation accepted its old queued original as a new cycle");
  at(nowTick+1000);g_sgRetrySleepTimeout=true;const auto fresh=nowFile;originalEntry(fresh);handle506(1,fresh);
  check(generation==gen+1&&g_sgTask.generation==generation&&sgSleepTimeoutOriginalIntent()==fresh,"original retirement fence blocked or reused a truly newer sleep");
 }
 passed("public-settled-retired-old506-refused-new-cycle-still-admitted");
}


// SleepTimeout consumes the existing public classification; no fresh grace,
// user proof, power generation, game task or action budget is created here.
static ULONGLONG consumerOverdueResume443(){
 receiptFresh();const auto original=receiptAdmit(false,0);
 at(original.tick+443000);originalEntry(original.fileTime100ns);
 resumeAutomatic();resumeInteractive();drainActualWake();handle506(1,original.fileTime100ns);
 check(sgResumeClassificationHeld(generation)&&classificationTimerDue&&g_sgSleepTimeoutWindow.waiting,"field443 setup lost original public classification or overdue timeout");
 return original.fileTime100ns;
}
static void consumerDelayedManual443(){
 const auto original=consumerOverdueResume443(),resumeTick=nowTick;
 const auto classificationDeadline=classificationTimerDue;
 sgSleepTimeoutTick();at(resumeTick+700);sgDispatchSameModeRetry();
 check(!requestCount,"overdue timeout erased existing classification and slept before delayed human507");
 check(sgResumeClassificationHeld(generation)&&classificationTimerDue==classificationDeadline&&!sgSleepRetryActive(),"timeout replaced original classification owner or deadline");
 at(resumeTick+900);g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;g_sgLastKernel507EventFileTime=nowFile;
 handle507Native(1,nowFile);drainActualWake();sgSleepTimeoutTick();sgDispatchSameModeRetry();
 check(!requestCount&&!timeoutTimerDue&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active()&&!classificationTimerDue,"late manual wake did not retire both original consumers");
 check(g_sgAutomaticSleepBudget.requests()==0&&g_sgClosedSleepIntentFileTime.load()>=original,"waiting spent budget or manual closure lost original anchor");
 passed("consumer443s-timeout-waits-original-classification-delayed-manual507");
}
static void consumerClassificationExpiry(bool duplicates){
 const auto original=consumerOverdueResume443(),resumeTick=nowTick,deadline=classificationTimerDue;
 const auto elapsed=sgNowFileTime()-original;sgSleepTimeoutTick();
 if(duplicates){
  for(unsigned i=0;i<10;++i){at(resumeTick+10+i);resumeAutomatic();resumeInteractive();sgSleepTimeoutTick();}
 }
 check(classificationTimerDue==deadline&&!timeoutTimerDue&&!sgSleepRetryActive()&&!requestCount,"overdue timeout duplicated/replaced original classification timer");
 at(deadline);sgResumeClassificationTick();drainActualWake();
 check(!classificationTimerDue&&timeoutTimerDue&&g_sgSleepTimeoutWindow.waiting&&sgSleepTimeoutOriginalIntent()==original,"classification completion lost overdue original continuation");
 const auto first=timeoutTimerDue;sgStopResumeClassificationWait("duplicate-finished-observation");
 check(timeoutTimerDue==first,"duplicate finish redelivered consumed continuation");
 at(timeoutTimerDue);sgSleepTimeoutTick();dispatch();
 check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1&&sgNowFileTime()-original>=elapsed,"unknown completion did not continue original overdue sender/budget");
 sgSleepTimeoutTick();sgDispatchSameModeRetry();check(requestCount==1,"continued overdue request duplicated");
 passed(duplicates?"consumer-classification-duplicates-preserve-first-window":"consumer-classification-unknown-continues-original-overdue-timeout");
}
static void consumerClassificationVeto(){
 for(const auto kind:{"manual","success","off","exit","generation","new-pbt"}){
  consumerOverdueResume443();sgSleepTimeoutTick();const auto due=classificationTimerDue;
  if(std::string(kind)=="manual")g_sgLastPowerButtonWakeEventFileTime=nowFile;
  else if(std::string(kind)=="success")sgSleepTimeoutRecordSystemSuccess(nowFile);
  else if(std::string(kind)=="off")g_sgRetrySleepTimeout=false;
  else if(std::string(kind)=="exit")g_exitRequested=true;
  else if(std::string(kind)=="generation")++generation;
  else suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
  at(due);sgResumeClassificationTick();drainActualWake();sgSleepTimeoutTick();sgDispatchSameModeRetry();
  // A newly pending native notification is vetoed by the actual sender when
  // the existing500ms retry reaches its deadline; do not inspect it early.
  if(sgSleepRetryActive())dispatch();
  if(requestCount||timeoutTimerDue||sgSleepRetryActive()||g_sgSleepTimeoutWindow.active())std::cerr<<"CONSUMER_VETO_STATE "<<json{{"kind",kind},{"requests",requestCount},{"timeoutDue",timeoutTimerDue},{"retry",sgSleepRetryActive()},{"window",g_sgSleepTimeoutWindow.active()},{"classificationDue",classificationTimerDue},{"facts",facts}}.dump()<<"\n";
  check(!requestCount&&!timeoutTimerDue&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active(),"original veto resurrected deferred timeout continuation");
 }
 passed("consumer-classification-deferred-six-original-vetoes");
}
static void consumerNoClassification(){
 receiptFresh();const auto original=receiptAdmit(false,0);originalEntry(original.fileTime100ns);handle506(1,original.fileTime100ns);
 at(original.tick+240000);sgSleepTimeoutTick();
 check(sgSleepRetryActive()&&timerDue&&!classificationTimerDue,"without resume observation original240s sender gained a classification wait");
 dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,"plain original240s was blocked by new consumer join");
 passed("consumer-no-resume-classification-keeps-original240s-send");
}
static void consumerRemainingBudget(){
 consumerOverdueResume443();check(g_sgAutomaticSleepBudget.consume()&&g_sgAutomaticSleepBudget.consume(),"prior request slots missing");
 const auto due=classificationTimerDue;sgSleepTimeoutTick();at(due);sgResumeClassificationTick();drainActualWake();
 at(timeoutTimerDue);sgSleepTimeoutTick();requestAccepted=false;dispatch();drainActualWake();
 check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==3&&!sgSleepRetryActive(),"classification continuation refunded or exceeded existing cumulative budget");
 sgSleepTimeoutTick();sgDispatchSameModeRetry();check(requestCount==1,"last spent slot resumed retired timeout");
 passed("consumer-classification-continuation-keeps-last-third-slot-only");
}


static void consumerResumeAfterScheduled(bool manual){
 receiptFresh();const auto original=receiptAdmit(false,0);
 at(original.tick+443000);originalEntry(original.fileTime100ns);handle506(1,original.fileTime100ns);
 sgSleepTimeoutTick();const auto originalDue=timerDue;check(sgSleepRetryActive()&&originalDue&&!requestCount,"late resume setup did not schedule unissued timeout");
 at(nowTick+100);const auto resumeTick=nowTick;resumeAutomatic();resumeInteractive();drainActualWake();
 check(sgResumeClassificationHeld(generation)&&classificationTimerDue,"first resume after scheduled timeout lost original classification grace");
 const auto classifyDue=classificationTimerDue;at(originalDue);sgDispatchSameModeRetry();
 check(!requestCount&&sgSleepRetryActive()&&g_sgRetryDueTick==originalDue&&!timerDue,"pending classification failed to hold original unissued retry without polling/refund");
 if(manual){
  at(resumeTick+900);g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;g_sgLastKernel507EventFileTime=nowFile;handle507Native(1,nowFile);drainActualWake();sgDispatchSameModeRetry();
  if(requestCount||sgSleepRetryActive()||g_sgSleepTimeoutWindow.active()||classificationTimerDue)std::cerr<<"CONSUMER_SCHEDULED_MANUAL_STATE "<<json{{"requests",requestCount},{"retry",sgSleepRetryActive()},{"window",g_sgSleepTimeoutWindow.active()},{"classificationDue",classificationTimerDue},{"token",g_sgSleepTimeoutClassificationGeneration},{"facts",facts}}.dump()<<"\n";
  check(!requestCount&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active()&&!classificationTimerDue,"late first resume manual wake reached Windows sleep");
 }else{
  at(classifyDue);sgResumeClassificationTick();drainActualWake();
  check(timerDue&&g_sgRetryDueTick==originalDue&&!classificationTimerDue,"classification completion lost/refunded already scheduled original retry");
  dispatch();check(requestCount==1&&g_sgAutomaticSleepBudget.requests()==1,"scheduled retry did not continue once after original classification");
 }
 passed(manual?"consumer-resume-after-scheduled-timeout-delayed-manual-veto":"consumer-resume-after-scheduled-timeout-unknown-continues-once");
}


static void consumerReturnTimerFailure(bool scheduled){
 if(scheduled){
  receiptFresh();const auto original=receiptAdmit(false,0);at(original.tick+443000);originalEntry(original.fileTime100ns);handle506(1,original.fileTime100ns);
  sgSleepTimeoutTick();const auto due=timerDue;at(nowTick+100);resumeAutomatic();resumeInteractive();drainActualWake();at(due);sgDispatchSameModeRetry();
  check(sgSleepRetryActive()&&!timerDue&&sgResumeClassificationHeld(generation),"timer-failure setup did not hold scheduled original");
 }else{consumerOverdueResume443();sgSleepTimeoutTick();}
 const auto classifyDue=classificationTimerDue;timeoutConsumerFailTimer=scheduled?SG_RETRY_TIMER_ID:SG_SLEEP_TIMEOUT_TIMER_ID;
 at(classifyDue);sgResumeClassificationTick();drainActualWake();
 check(!timeoutConsumerFailTimer&&!requestCount&&!sgSleepRetryActive()&&!timerDue&&!timeoutTimerDue&&!g_sgSleepTimeoutWindow.active(),"failed continuation timer stranded active retry or granted action");
 sgSleepTimeoutTick();sgDispatchSameModeRetry();check(!requestCount,"failed continuation replayed original sender");
 passed(scheduled?"consumer-scheduled-retry-return-timer-failure-retires":"consumer-overdue-window-return-timer-failure-retires");
}
