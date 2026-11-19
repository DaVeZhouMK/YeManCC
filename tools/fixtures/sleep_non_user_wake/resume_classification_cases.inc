std::vector<std::string> resumeCases;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void passed(const std::string& name){resumeCases.push_back(name);std::cout<<"PASS "<<name<<"\n";}
void drainActualWake(){unsigned count=0;while(!actualWakeQueue.empty()){check(++count<20,"bounded actual queue");auto item=actualWakeQueue.front();actualWakeQueue.erase(actualWakeQueue.begin());consumeActualWake(item);}}
void baseCycle(bool pid,bool nonUser){
 reset(pid);sgResetKernelPowerIntervalObservations();actualWakeQueue.clear();classificationTimerDue=timeoutTimerDue=0;
 g_lastSuspendNotifyTick=g_lastSuspendReceiptTick=0;
 g_sgLastModernStandbyIntentTick=0;g_sgSleepQueryGateClosed=g_sgSleepQueryOwnsGeneration=false;g_sgSleepQueryGeneration=0;
 g_resumeReadyGeneration=0;g_sgResleepEnabled=nonUser;g_sgRetrySleepTimeout=true;
 g_sgSleepTimeoutWindow={};g_sgKernelSystemWake=0;
#ifdef YMCC_TIMEOUT_OBSERVATION_ENABLE
 g_sgSleepTimeoutObservation={};g_sgSleepTimeoutGeneration=0;resetTimeoutSourcePorts();
#endif
 g_sleepPowerDispatch.~Dispatch();new (&g_sleepPowerDispatch) ymcc::sleeppower::Dispatch();
#ifdef CURRENT_COORDINATOR
 g_sgResumeClassification.generation=0;g_sgResumeClassification.observedGeneration=0;
 g_sgWakeClassificationBlockedGeneration=0;softwareReadyNotices=0;
#ifdef YMCC_FIXTURE_ACTUAL_SHARED_PUBLICATION
 g_sharedPowerPublicationRevision=0;g_sharedPowerProjectionPending=false;
 classificationPublicationPosts=classificationBatteryNotices=0;
 g_recoveryPowerBoundary.revocations=g_sharedPowerRevocations.revocations=0;
#endif
 g_sgResumeClassification.origin=g_sgResumeClassification.attemptTick=g_sgResumeClassification.firstReceiptTick=g_sgResumeClassification.deadlineTick=g_sgResumeClassification.interactiveReceiptTick=0;
#endif
 sgObserveEntryFailureDisplay(0,"field-session-off");
 handleSuspendAdmission();g_sgPauseWorkCompletedGeneration=generation;
 g_sgInSuspend=frozen;g_sgGameActuallySuspended=frozen;
}
void originalEntry(ULONGLONG original){
 sgRememberKernelSleepEntry(1,original);g_sgLastKernel506Reason=1;g_sgLastKernel506EventFileTime=original;
}
void fieldScenario(bool nonUser,bool pid,int order,ULONGLONG durationUs=0){
 baseCycle(pid,nonUser);const auto gen=generation,origin=nowFile;
 // Both field durations preserve original-event time separately from GUI
 // collection. No DRIPS zero or event category alone is a failure assertion.
 at(13000);originalEntry(origin);
 if(order==1)handle506(1,origin);
 resumeAutomatic();drainActualWake();resumeInteractive();drainActualWake();
 check(g_sgLastPowerButtonSleepIntentFileTime.load()==origin,"early PBT cleared original sleep intent");
 check(g_sgTask.generation==gen&&g_sgRepairEligible,"early PBT cleared shared sleep task");
 check(requestCount==0,"broadcast alone dispatched sleep");
 if(order==2)sgNoteExternalDeviceNodeChange();
 if(order!=1)handle506(1,origin);
 check(generation==gen,"late original506 allocated another generation");
 check(sgSleepTimeoutOriginalIntent()==origin,"late original506 lost independent timeout arm");
 const auto exit=origin+(durationUs?durationUs:(order==1?2985537ULL:3675033ULL))*10ULL;
 at(14000);g_sgLastKernel507EventFileTime=exit;handle507Native(32,exit);
 check(sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::EntryFailure,"late507 failed shared fast admission");
 check(requestCount==0&&timerDue,"classification must use original scheduled retry");
 dispatch();check(requestCount==1,"actual retry dispatch missing");
 passed(std::string("field-pbt-late506-507/nonUser=")+(nonUser?"on":"off")+"/pid="+(pid?"yes":"no")+"/order="+std::to_string(order)+(durationUs?"/durationUs="+std::to_string(durationUs):""));
}
// CURRENT_COORDINATOR_CONTROLS
void noEvidence(bool interactive){
 baseCycle(true,false);const auto origin=nowFile,gen=generation;
 at(13000);originalEntry(origin);resumeAutomatic();if(interactive)resumeInteractive();drainActualWake();
 check(g_powerLifecycle==PowerLifecycle::Ready&&frozen,"input Ready must retain exact lease while pending");
 const auto due=classificationTimerDue;
 for(int i=0;i<100;i++){at(13001+i);resumeInteractive();}
 check(classificationTimerDue==due,"duplicate PBT extended first classification deadline");
 at(due);sgResumeClassificationTick();drainActualWake();
 check(!frozen&&resumeCount==1&&!requestCount&&!classificationTimerDue,"missing evidence did not return exact lease normally");
 check(generation==gen&&g_sgTask.generation==0,"normal completion rebuilt or retained task");
 // Timeout is independently keyed to original506; arm it on the frozen
 // original entry before the broadcasts in the dedicated deadline case.
 passed(interactive?"missing-evidence-bounded-return/interactive":"missing-evidence-bounded-return/automatic");
}
void displayEvidence(){
 baseCycle(false,false);const auto origin=nowFile;at(13000);originalEntry(origin);
 resumeAutomatic();drainActualWake();handle506(1,origin);
 at(13100);sgObserveEntryFailureDisplay(1,"field-session-on");const auto monitor=monitorTimerDue;
 check(monitor==18100,"original display five-second window missing");
 at(classificationTimerDue);sgResumeClassificationTick();
 check(classificationTimerDue==monitor&&requestCount==0,"classification shortened or restarted display window");
 at(monitor-1);sgResumeClassificationTick();check(!requestCount&&!sgSleepRetryActive(),"display confirmation shortened");
 at(monitor);sgMonitorEntryFailureTick();
 check(sgSleepRetryActive(),"Ready pending witness was not consumed by display failure");
 sgResumeClassificationTick();check(sgSleepRetryActive(),"late classification timer canceled admitted retry");
 dispatch();check(requestCount==1,"display failure did not reach original sender");passed("display-five-second-remaining-window");
}
void manualBeforeGui(){
 baseCycle(true,false);const auto original=nowFile-1000;at(13000);originalEntry(original);
 // Callback-side real human evidence precedes fallback origin, but is newer
 // than original506. It must be compared after refinement, before GUI delivery.
 g_sgLastPowerButtonWakeEventFileTime=original+500;g_sgOriginalPowerButtonWakeFileTime=original+500;
 resumeInteractive();drainActualWake();
 check(!sgSleepRetryActive()&&!frozen&&!requestCount,"original human callback did not immediately veto recovery wait");
 check(!classificationTimerDue,"human veto left classifier timer");passed("human-original-time-veto-before-gui");
}
void manualDuringPending(){
 baseCycle(true,false);const auto origin=nowFile;at(13000);originalEntry(origin);resumeAutomatic();drainActualWake();handle506(1,origin);
 at(14000);g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;
 handle507Native(1,nowFile);drainActualWake();check(!frozen&&!requestCount&&!classificationTimerDue,"human GUI veto did not return lease");
 sgResumeClassificationTick();check(!requestCount,"late timer sent sleep after human veto");passed("human-veto-pending-and-late-timer");
}
void windowsHandoff(){
 baseCycle(true,false);const auto origin=nowFile;at(13000);originalEntry(origin);resumeAutomatic();drainActualWake();handle506(1,origin);
 at(14000);g_sgLastKernel507EventFileTime=nowFile;sgSleepTimeoutRecordSystemSuccess(nowFile);handle507Native(20,nowFile);drainActualWake();
 check(frozen&&!requestCount&&!classificationTimerDue,"Reason20 prematurely completed Windows S4 handoff");
 handleS4();drainActualWake();check(!frozen&&!requestCount,"completed S4 boundary did not recover exact lease");passed("windows-handoff-awaits-completed-s4");
}
void delayedWork(){
 baseCycle(true,false);const auto origin=nowFile;at(13000);originalEntry(origin);resumeAutomatic();resumeInteractive();handle506(1,origin);
 const auto exit=origin+3600*10000ULL;at(14000);g_sgLastKernel507EventFileTime=exit;handle507Native(32,exit);
 check(sgSleepRetryActive(),"fast retry was not admitted before delayed wake queue");
 actualWakeQueue.push_back({SgWork::WakeSuspend,generation});drainActualWake();
 check(sgSleepRetryActive()&&frozen&&!requestCount,"old wake worker cleared admitted retry or exact lease");
 dispatch();check(requestCount==1,"late work prevented original dispatch");passed("delayed-common-worker-cannot-clear-new-retry");
}
void newerEntry(){
 baseCycle(true,false);const auto origin=nowFile;at(13000);originalEntry(origin);resumeAutomatic();drainActualWake();handle506(1,origin);
 const auto old=generation;at(15000);originalEntry(nowFile);handle506(1,nowFile);
 check(generation>old&&!classificationTimerDue,"new original506 did not retire old wait and allocate its own generation");
 const auto gen=generation;sgResumeClassificationTick();check(generation==gen&&!requestCount,"late old timer touched successor");passed("new-original-entry-retires-old-wait");
}
void timeoutIndependent(){
 baseCycle(false,false);const auto origin=nowFile;originalEntry(origin);handle506(1,origin);
 const auto deadline=timeoutTimerDue;check(deadline!=0,"independent timeout not armed");
 at(13000);resumeAutomatic();resumeInteractive();drainActualWake();check(timeoutTimerDue==deadline,"PBT prematurely stopped or refunded240s");
 at(classificationTimerDue);sgResumeClassificationTick();drainActualWake();
 check(timeoutTimerDue==deadline&&sgSleepTimeoutOriginalIntent()==origin,"classification expiry retired independent240s");
 at(deadline);sgSleepTimeoutTick();check(sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::SleepTimeout,"original240s consumer did not admit its shared retry");
 passed("independent240s-survives-unclassified-broadcasts");
}
void retiredLifetime(bool exit){
 baseCycle(true,false);const auto origin=nowFile;at(13000);originalEntry(origin);resumeAutomatic();drainActualWake();
 if(exit)g_exitRequested=true;else g_guardEnabled=false;
 at(classificationTimerDue);sgResumeClassificationTick();drainActualWake();
 check(!requestCount&&!classificationTimerDue,"retired lifetime kept timer or dispatched sleep");
 passed(exit?"exit-retires-pending-observation":"disabled-guard-retires-pending-observation");
}
void lateReceipt(){
 baseCycle(true,false);const auto original=nowFile;at(13000);originalEntry(original);handle506(1,original);
 const auto before=original-10000;
 ymcc::sleeppower::Dispatch::Receipt receipt{9,9999,PBT_APMRESUMESUSPEND,true,before};
 check(sgIgnorePowerReturnBeforeCurrentIntent(receipt),"old callback predating original506 was accepted");
 check(frozen&&g_sgTask.generation==generation&&!classificationTimerDue&&!requestCount,"old callback changed successor transaction");
 receipt.tick=0;receipt.fileTime100ns=0;
 check(sgIgnorePowerReturnBeforeCurrentIntent(receipt),"unknown forwarded receive time was rejuvenated");
 // A genuine wake may be received before delayed suspend GUI processing;
 // its original receive FILETIME after506 is sufficient, not UI trigger tick.
 receipt.tick=9999;receipt.fileTime100ns=original+10000;
 check(!sgIgnorePowerReturnBeforeCurrentIntent(receipt),"valid earlier receipt was confused with earlier GUI processing");
 for(const auto code:{PBT_APMQUERYSUSPENDFAILED,PBT_APMQUERYSTANDBYFAILED}){
  receipt={9,9999,static_cast<ULONG>(code),true,before};
  check(sgIgnorePowerReturnBeforeCurrentIntent(receipt),"old query rejection predating original506 could affect the current sleep");
  receipt.tick=0;receipt.fileTime100ns=0;
  check(sgIgnorePowerReturnBeforeCurrentIntent(receipt),"unknown forwarded query rejection could affect the current sleep");
  receipt.tick=9999;receipt.fileTime100ns=original+10000;
  check(!sgIgnorePowerReturnBeforeCurrentIntent(receipt),"fresh query rejection changes the existing cancellation path");
  g_sgRetryInProgress=true;g_sgTask.retryKind=SgRetryKind::EntryFailure;
  g_sgInternalSleepRequest={SgRetryKind::EntryFailure,10000,original+10000};
  check(sgIgnorePowerReturnBeforeCurrentIntent(receipt),"query rejection received before this attempt could advance it");
  receipt.tick=10000;
  check(!sgIgnorePowerReturnBeforeCurrentIntent(receipt),"equal receipt tick was falsely treated as proof of an old query rejection");
  g_sgInternalSleepRequest={};g_sgTask.retryKind=SgRetryKind::None;g_sgRetryInProgress=false;
 }
 passed("old-and-unknown-callback-receipt-versus-original506");
}
void manualCallbackThenBroadcast(){
 baseCycle(true,false);const auto original=nowFile;at(13000);originalEntry(original);resumeAutomatic();drainActualWake();
 at(14000);g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;
 resumeInteractive();drainActualWake();
 check(!classificationTimerDue&&!frozen&&!requestCount,"callback veto followed by PBT left pending observation");
 check(sharedSleepWakeClassified(generation),"final human wake did not reopen software classification gate");
 passed("human-callback-veto-then-PBT-retires-pending");
}
const char* retryEntryPublishAt=nullptr;
void retryExitWithNewEntryDuringClassification(const char* publishAt){
 baseCycle(false,false);const auto original=nowFile;at(13000);originalEntry(original);resumeAutomatic();handle506(1,original);drainActualWake();
 at(14000);const auto firstExit=original+3600*10000ULL;g_sgLastKernel507EventFileTime=firstExit;handle507Native(32,firstExit);dispatch();
 check(requestCount==1&&sgSleepRetryActive()&&!timerDue,"first request was not issued and waiting");
 const auto marker=g_sgInternalSleepRequest.dispatchFileTime;
 at(nowTick+100);const auto oldExit=nowFile;
 retryEntryPublishAt=publishAt;
 if(publishAt)fixtureSleepFactHook=[](const char* event){
  if(strcmp(event,retryEntryPublishAt)!=0)return;
  fixtureSleepFactHook=nullptr;
  at(nowTick+1);
  sgSleepTimeoutRecordKernel(506,20,nowFile,generation);
  sgPublishSleepEventTime(g_sgLatestSleepEntryFileTime,nowFile);
 };
 handle507Native(7,oldExit);
 if(publishAt){
  check(!fixtureSleepFactHook,"new original entry was not published at the selected log");
  check(!timerDue&&requestCount==1,"old wake kept another retry after a newer 506");
  if(strcmp(publishAt,"s0-wake-classified")==0){
   check(g_sgInternalSleepRequest.dispatchFileTime==marker,"old wake retired the request after a newer 506");
   passed("new-kernel-entry-during-wake-classification-vetoes-retry");
  }else passed("new-kernel-entry-during-retry-schedule-cancels-timer");
 }else{
  check(timerDue&&requestCount==1,"uninterrupted short exit did not follow original retry policy");
  passed("short-exit-without-new-entry-keeps-retry-policy");
 }
}
void repeatedAttempt(){
 baseCycle(false,false);const auto original=nowFile;at(13000);originalEntry(original);resumeAutomatic();handle506(1,original);drainActualWake();
 at(14000);const auto exit=original+3600*10000ULL;g_sgLastKernel507EventFileTime=exit;handle507Native(32,exit);dispatch();
 check(requestCount==1&&!sharedSleepWakeClassified(generation),"accepted retry lost shared classification block");
 confirmRetrySuspend();check(!sgSleepRetryActive(),"retry confirmation failed");
 const auto attempt=g_sgSleepTriggerTick;at(attempt+3000);originalEntry(g_sgInternalSleepRequest.dispatchFileTime);
 resumeAutomatic();drainActualWake();check(classificationTimerDue!=0,"next actual same-generation attempt could not classify");
 check(requestCount==1,"next broadcast refunded or dispatched another request");
 passed("same-generation-next-confirmed-attempt-has-own-first-deadline");
}
void softwareGate(){
 baseCycle(false,false);const auto original=nowFile,gen=generation;
 check(!sharedSleepWakeClassified(gen),"software final wake allowed at sleep boundary");
 at(13000);originalEntry(original);resumeAutomatic();drainActualWake();
 check(!sharedSleepWakeClassified(gen),"system Ready borrowed final software wake permission");
 at(classificationTimerDue);sgResumeClassificationTick();drainActualWake();
#ifdef YMCC_FIXTURE_ACTUAL_SHARED_PUBLICATION
 check(sharedSleepWakeClassified(gen)&&g_sharedPowerPublicationRevision.load()>0&&
  g_sharedPowerProjectionPending.load()&&classificationPublicationPosts==1&&classificationBatteryNotices>0,
  "same-generation final software projection was not published and queued once");
 const auto published=g_sharedPowerPublicationRevision.load();
 publishSharedPowerLifecycle("fixture-coalesced-publication");
 check(g_sharedPowerPublicationRevision.load()==published+1&&classificationPublicationPosts==1&&generation==gen,
  "actual publisher did not coalesce a burst without changing power generation");
#else
 check(sharedSleepWakeClassified(gen)&&softwareReadyNotices==1,"same-generation final software notification missing");
#endif
 g_hardwareWriteGate=false;g_inputReady=false;g_powerLifecycle=PowerLifecycle::Resuming;
 check(sharedSleepWakeClassified(gen),"software classification gate incorrectly requires hardware/InputHost Ready");
 check(!sharedSleepWakeClassified(gen-1),"old generation software admission");g_exitRequested=true;
 check(!sharedSleepWakeClassified(gen),"exit software admission");
#ifdef YMCC_FIXTURE_ACTUAL_SHARED_PUBLICATION
 const auto stopped=g_sharedPowerPublicationRevision.load();
 const auto battery=classificationBatteryNotices;
 publishSharedPowerLifecycle("fixture-after-exit");
 check(g_sharedPowerPublicationRevision.load()==stopped&&classificationBatteryNotices==battery&&classificationPublicationPosts==1,
  "actual publisher notified after exit");
#endif
 passed("pure-software-classification-gate-and-same-generation-final-notice");
}
void wakeBeforeEntryGui(bool nonUser){
 baseCycle(false,nonUser);const auto original=nowFile,gen=generation;
 at(13000);originalEntry(original);resumeAutomatic();resumeInteractive();drainActualWake();
 at(14000);const auto exit=original+3600*10000ULL;g_sgLastKernel507EventFileTime=exit;handle507Native(32,exit);
 const auto due=timerDue;check(sgSleepRetryActive()&&due,"507 before506GUI did not admit retry");
 handle506(1,original);check(generation==gen&&sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::EntryFailure&&timerDue==due,"late506GUI reset admitted retry or generation");
 check(sgSleepTimeoutOriginalIntent()==original,"late506GUI after retry lost240s identity");
 dispatch();check(requestCount==1,"late506GUI canceled accepted retry");passed(nonUser?"507-before506GUI-nonUser-on":"507-before506GUI-nonUser-off");
}
void criticalDuringPending(){
 baseCycle(true,false);const auto original=nowFile,gen=generation;
 at(13000);originalEntry(original);resumeAutomatic();drainActualWake();
 check(classificationTimerDue&&frozen,"critical control did not begin pending");
 at(14000);resumeCritical();drainActualWake();
 check(!classificationTimerDue&&!frozen&&resumeCount==1&&!requestCount,"critical resume left pending observation or lease");
 sgResumeClassificationTick();drainActualWake();
 check(generation==gen&&!requestCount&&sharedSleepWakeClassified(gen),"late timer changed critical final wake");
 passed("critical-resume-retires-pending-and-late-timer");
}
void missingKernelEvidence(bool pid){
 baseCycle(pid,false);const auto gen=generation;at(13000);
 // No original506 callback, no506 GUI and no507 at all.
 resumeAutomatic();resumeInteractive();drainActualWake();
 check(classificationTimerDue&&g_sgTask.generation==gen,"missing Kernel evidence did not enter bounded wait");
 const auto due=classificationTimerDue;at(due);sgResumeClassificationTick();drainActualWake();
 check(!classificationTimerDue&&!requestCount&&!frozen&&g_sgTask.generation==0,"missing all Kernel evidence failed normal bounded return");
 check(resumeCount==(pid?1:0)&&generation==gen&&sharedSleepWakeClassified(gen),"missing Kernel evidence changed exact owner or generation");
 passed(pid?"missing506-and507-normal-return/pid=yes":"missing506-and507-normal-return/pid=no");
}
void actualSettingsCancellation(){
 baseCycle(true,false);const auto original=nowFile,gen=generation;
 at(13000);originalEntry(original);resumeAutomatic();drainActualWake();
 const auto due=classificationTimerDue;g_sgRetryEntryFailure=false;
 consumeActualSettingsCancellation();drainActualWake();
 check(classificationTimerDue==due&&frozen&&g_guardEnabled,"one remaining classification feature was canceled");
 g_sgRetrySleepTimeout=false;consumeActualSettingsCancellation();drainActualWake();
 check(!classificationTimerDue&&!frozen&&!requestCount&&g_sgTask.generation==0&&g_guardEnabled,"actual settings branch did not return pending lease");
 sgResumeClassificationTick();drainActualWake();
 check(generation==gen&&!requestCount&&sharedSleepWakeClassified(gen),"late timer revived disabled classification");
 passed("actual-setConfig-both-features-off-while-guard-on");
}

#ifdef YMCC_PUBLIC_CLASSIFICATION_FACTS
static unsigned publicFactChecks=0;static json publicFactRows=json::array();
static void publicFactRequire(bool ok,const char* why){++publicFactChecks;check(ok,why);}
static void publicFactsReset(){baseCycle(false,false);ymcc::sleeppower::nativeWakeFacts.resetLocked();const auto origin=nowFile;ymcc::sleeppower::nativeWakeFacts.kernel(generation,506,1,-1,origin,nowFile);at(13000);originalEntry(origin);resumeAutomatic();drainActualWake();}
static bool publicFactsAllowed(){const auto f=[](){std::lock_guard lock(ymcc::sleeppower::nativeWakeFacts.mutex);return ymcc::sleeppower::nativeWakeFacts.snapshotForLocked(generation,4,1,generation);}();ymcc::sleeppower::CompletedWake v;v.generation=generation;v.epoch=4;v.boundary=1;v.boundaryGeneration=generation;v.readyGeneration=f.readyCommit.generation;v.readyEpoch=f.readyCommit.configEpoch;v.readyBoundary=f.readyCommit.boundaryFileTime;v.interactiveGeneration=f.interactiveResume.generation;v.interactiveEpoch=f.interactiveResume.configEpoch;v.interactiveBoundary=f.interactiveResume.boundaryFileTime;v.interactiveAt=f.interactiveResume.eventFileTime;v.completedGeneration=f.completedResume.generation;v.completedEpoch=f.completedResume.configEpoch;v.completedBoundary=f.completedResume.boundaryFileTime;v.completedAt=f.completedResume.eventFileTime;v.latestEntry=f.kernelSleepEntry.eventFileTime;v.now=nowFile;v.limitMs=60000;v.eventId=f.completedResume.eventId;v.ownerAllowed=sharedSleepWakeClassified(generation);v.retrying=sgSleepRetryActive();v.ready=g_powerLifecycle==PowerLifecycle::Ready;return ymcc::sleeppower::completedWake(v)==ymcc::sleeppower::CompletedWakeResult::Allowed;}
static void publicFactFinish(){ymcc::sleeppower::nativeWakeFacts.kernel(generation,507,1,-1,nowFile,nowFile);at(classificationTimerDue);sgResumeClassificationTick();drainActualWake();ymcc::sleeppower::nativeWakeFacts.ready(generation);}
static int publicFactsMain(bool firstOnly,bool retryOnly=false){
 const auto test=[&](const char* name,const auto& fn){if(retryOnly&&strcmp(name,"accepted-retry-held-interactive-remains-public")!=0)return;if(firstOnly&&!publicFactRows.empty())return;const auto n=publicFactChecks;try{fn();publicFactRows.push_back({{"name",name},{"pass",true},{"checks",publicFactChecks-n}});}catch(const std::exception& e){publicFactRows.push_back({{"name",name},{"pass",false},{"checks",publicFactChecks-n},{"reason",e.what()}});}};
 test("held-interactive-pbt-retained-and-joins-after-classification",[]{publicFactsReset();const auto first=classificationTimerDue;resumeInteractive();const auto f=ymcc::sleeppower::nativeWakeFacts.snapshot();publicFactRequire(f.interactiveResume.generation==generation&&f.interactiveResume.eventFileTime==nowFile,"held-pbt-missing-from-public-native-owner");publicFactRequire(!publicFactsAllowed()&&!sharedSleepWakeClassified(generation),"pending-evidence-granted-action");publicFactFinish();publicFactRequire(publicFactsAllowed(),"classification-complete-cannot-join-original-public-pbt");publicFactRequire(!requestCount&&nowTick==first,"classification-clock-or-action-refunded");});
 test("critical-interactive-is-native-evidence",[]{publicFactsReset();publicActualInteractiveIngress(PBT_APMRESUMECRITICAL,{21,nowTick,PBT_APMRESUMECRITICAL,false,nowFile},false);publicFactRequire(ymcc::sleeppower::nativeWakeFacts.snapshot().interactiveResume.eventFileTime==nowFile,"critical-public-receipt-missing");publicFactRequire(!publicFactsAllowed(),"critical-proof-bypassed-held-classification");});
 test("duplicate-held-pbt-keeps-first-classification-deadline",[]{publicFactsReset();const auto due=classificationTimerDue;resumeInteractive();for(int i=0;i<10;++i){at(13001+i);resumeInteractive();}publicFactRequire(classificationTimerDue==due&&!requestCount,"duplicate-held-pbt-refunded-clock-or-sleep");publicFactRequire(ymcc::sleeppower::nativeWakeFacts.snapshot().interactiveResume.receiptTick==nowTick,"later-real-public-receipt-not-observed");publicFactFinish();publicFactRequire(publicFactsAllowed(),"last-current-interactive-fact-not-joinable");});
 test("delayed-gui-preserves-original-receipt-time",[]{publicFactsReset();const auto original=nowFile;at(14000);publicActualInteractiveIngress(PBT_APMRESUMESUSPEND,{22,13000,PBT_APMRESUMESUSPEND,true,original},true);publicFactRequire(ymcc::sleeppower::nativeWakeFacts.snapshot().interactiveResume.eventFileTime==original,"queued-pbt-rejuvenated-to-gui-time");publicFactRequire(!publicFactsAllowed(),"queued-pbt-granted-while-held");});
 test("future-interactive-receipt-refused",[]{publicFactsReset();publicActualInteractiveIngress(PBT_APMRESUMESUSPEND,{23,nowTick+1,PBT_APMRESUMESUSPEND,true,nowFile},true);publicFactRequire(!ymcc::sleeppower::nativeWakeFacts.snapshot().interactiveResume.eventFileTime,"future-pbt-receipt-granted");});
 test("expired-interactive-receipt-refused",[]{publicFactsReset();at(73000);publicActualInteractiveIngress(PBT_APMRESUMESUSPEND,{24,13000,PBT_APMRESUMESUSPEND,true,nowFile-60000ULL*10000},true);publicFactRequire(!ymcc::sleeppower::nativeWakeFacts.snapshot().interactiveResume.eventFileTime,"expired-pbt-receipt-rejuvenated");});
 test("automatic-broadcast-is-not-interactive-evidence",[]{publicFactsReset();publicActualInteractiveIngress(PBT_APMRESUMEAUTOMATIC,{25,nowTick,PBT_APMRESUMEAUTOMATIC,true,nowFile},true);publicFactRequire(!ymcc::sleeppower::nativeWakeFacts.snapshot().interactiveResume.eventFileTime,"automatic-broadcast-promoted-to-interactive");});
 test("late-new-entry-still-revokes-held-interactive-join",[]{publicFactsReset();resumeInteractive();at(14000);ymcc::sleeppower::nativeWakeFacts.kernel(generation,506,20,-1,nowFile,nowFile);publicFactFinish();publicFactRequire(!publicFactsAllowed(),"late-entry-borrowed-older-held-pbt");publicFactRequire(!requestCount,"public-facts-test-issued-sleep");});
 test("accepted-retry-held-interactive-remains-public",[]{publicFactsReset();const auto origin=ymcc::sleeppower::nativeWakeFacts.snapshot().kernelSleepEntry.eventFileTime;at(14000);const auto firstExit=origin+3600*10000ULL;g_sgLastKernel507EventFileTime=firstExit;handle507Native(32,firstExit);dispatch();publicFactRequire(requestCount==1&&sgSleepRetryActive(),"actual-retry-not-accepted");at(nowTick+1);resumeInteractive();publicFactRequire(ymcc::sleeppower::nativeWakeFacts.snapshot().interactiveResume.eventFileTime==nowFile,"accepted-retry-lost-public-interactive");publicFactRequire(!publicFactsAllowed(),"accepted-retry-public-proof-granted-action");at(nowTick+1);ymcc::sleeppower::nativeWakeFacts.kernel(generation,507,1,-1,nowFile,nowFile);g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;handle507Native(1,nowFile);drainActualWake();ymcc::sleeppower::nativeWakeFacts.ready(generation);publicFactRequire(!sgSleepRetryActive()&&publicFactsAllowed(),"valid-final-wake-cannot-join-accepted-retry-pbt");publicFactRequire(requestCount==1,"accepted-retry-replayed-sleep");});
 const auto retryReturn=[&](int control){
  baseCycle(true,false);
  const auto gen=generation,origin=nowFile;
  ymcc::sleeppower::nativeWakeFacts.resetLocked();
  ymcc::sleeppower::nativeWakeFacts.kernel(gen,506,1,-1,origin,nowFile);
  originalEntry(origin);handle506(1,origin);
  at(12000);const auto firstExit=origin+1553*10000ULL;
  g_sgLastKernel507EventFileTime=firstExit;handle507Native(32,firstExit);dispatch();
  publicFactRequire(requestCount==1&&sgSleepRetryActive(),"retry not accepted");
  at(92752);const auto oldS4=origin+13000*10000ULL;
  sgSleepTimeoutRecordSystemSuccess(oldS4);
  ymcc::sleeppower::nativeWakeFacts.kernel(gen,107,-1,5,oldS4,nowFile);
  at(94000);const auto retryEntry=nowFile-640*10000ULL,finalExit=nowFile-73*10000ULL;
  sgSleepTimeoutRecordKernel(506,20,retryEntry,gen);
  ymcc::sleeppower::nativeWakeFacts.kernel(gen,506,20,-1,retryEntry,nowFile);
  sgRememberKernelSleepEntry(20,retryEntry);
  handleSuspendAdmission();
  publicFactRequire(!sgSleepRetryActive()&&g_powerLifecycle==PowerLifecycle::Suspended&&generation==gen&&frozen,"retry suspend did not retain original target");
  if(control==1){
   at(94009);const auto fresh=nowFile;
   sgSleepTimeoutRecordSystemSuccess(fresh);
   ymcc::sleeppower::nativeWakeFacts.kernel(gen,107,-1,5,fresh,nowFile);
   handleS4(gen,static_cast<LPARAM>(fresh));drainActualWake();
   publicFactRequire(!frozen&&resumeCount==1&&!sgResumeClassificationHeld(gen),"current S4 was blocked by the retry classifier");
   publicFactRequire(generation==gen&&requestCount==1,"current S4 changed generation or retried");
   return;
  }
  at(94018);resumeInteractive();drainActualWake();
  at(94027);handleS4(gen,static_cast<LPARAM>(oldS4));drainActualWake();
  publicFactRequire(std::any_of(facts.begin(),facts.end(),[](const auto& f){return f.first=="s4-wake-ignored";}),"older S4 GUI message was not rejected");
  const auto earlyResumes=resumeCount;
  if(control==2){
   const auto due=classificationTimerDue;
   publicFactRequire(due>nowTick&&frozen&&earlyResumes==0,"missing final evidence did not hold the lease");
   at(due-1);resumeInteractive();drainActualWake();
   publicFactRequire(classificationTimerDue==due&&resumeCount==0,"duplicate PBT reset the original wait or released early");
   at(due);sgResumeClassificationTick();drainActualWake();
   publicFactRequire(!frozen&&resumeCount==1&&generation==gen&&requestCount==1,"missing 507 stranded the lease or retried");
   return;
  }
  std::cout<<"C196_EARLY "<<json{{"retryActive",sgSleepRetryActive()},{"classificationHeld",sgResumeClassificationHeld(gen)},{"sharedClassified",sharedSleepWakeClassified(gen)},{"frozen",frozen},{"resumes",earlyResumes},{"classificationDue",classificationTimerDue},{"timeoutDue",timeoutTimerDue},{"closedIntent",g_sgClosedSleepIntentFileTime.load()},{"intent",g_sgLastPowerButtonSleepIntentFileTime.load()}}.dump()<<"\n";
  at(95015);ymcc::sleeppower::nativeWakeFacts.kernel(gen,507,1,-1,finalExit,nowFile);
  g_sgLastPowerButtonWakeEventFileTime=finalExit;g_sgOriginalPowerButtonWakeFileTime=finalExit;
  handle507Native(1,finalExit);drainActualWake();
  std::cout<<"C196_LATE "<<json{{"frozen",frozen},{"resumes",resumeCount},{"requests",requestCount},{"generation",generation},{"sharedClassified",sharedSleepWakeClassified(gen)}}.dump()<<"\n";
  publicFactRequire(earlyResumes==0,"original lease admitted before final classification");
  publicFactRequire(!frozen&&resumeCount==1&&requestCount==1&&generation==gen,"same lease did not recover once after 507");
 };
 test("retry-suspend-before-late-507",[&]{retryReturn(0);});
 test("retry-suspend-current-s4-completion",[&]{retryReturn(1);});
 test("retry-suspend-without-507-releases-at-original-deadline",[&]{retryReturn(2);});
 unsigned failed=0;for(const auto& row:publicFactRows)if(row["pass"]==false)++failed;std::cout<<"PUBLIC_CLASSIFICATION_FACTS "<<json{{"tests",publicFactRows.size()},{"checks",publicFactChecks},{"failed",failed},{"cases",publicFactRows},{"actualNativeOwner",true},{"actualPrelude",true},{"actualClassification",true},{"nativeOsActions",0}}.dump()<<"\n";return failed?2:0;
}
#endif

int main(int argc,char** argv){try{
#ifdef YMCC_PUBLIC_CLASSIFICATION_FACTS
 if(argc==2&&(strcmp(argv[1],"--classification-public-facts-only")==0||strcmp(argv[1],"--classification-public-facts-first")==0||strcmp(argv[1],"--classification-public-facts-retry-only")==0))return publicFactsMain(strcmp(argv[1],"--classification-public-facts-first")==0,strcmp(argv[1],"--classification-public-facts-retry-only")==0);
#endif

 if(argc!=1&&!(argc==2&&(strcmp(argv[1],"--classification-lifetime-only")==0||strcmp(argv[1],"--classification-edge-only")==0||strcmp(argv[1],"--classification-publication-only")==0||strcmp(argv[1],"--s3")==0||strcmp(argv[1],"--caps-unknown")==0)))throw std::runtime_error("unknown-resume-classification-selector");
 if(argc==2&&strcmp(argv[1],"--classification-publication-only")==0){softwareGate();}
 else if(argc==2&&strcmp(argv[1],"--classification-lifetime-only")==0){criticalDuringPending();missingKernelEvidence(true);missingKernelEvidence(false);actualSettingsCancellation();}
 else if(argc==2&&strcmp(argv[1],"--classification-edge-only")==0){wakeBeforeEntryGui(false);wakeBeforeEntryGui(true);}
 else
 if(argc==2&&(strcmp(argv[1],"--s3")==0||strcmp(argv[1],"--caps-unknown")==0)){
  platformAoAc=false;platformCapsKnown=strcmp(argv[1],"--caps-unknown")!=0;
  baseCycle(true,false);at(13000);resumeInteractive();drainActualWake();
  check(!classificationTimerDue&&!frozen&&!requestCount,"classic or unknown capability lost normal final wake");
  passed(argv[1]);
 }else{
  for(bool nonUser:{false,true})for(bool pid:{false,true})for(int order:{0,1,2})fieldScenario(nonUser,pid,order);
  noEvidence(false);noEvidence(true);displayEvidence();manualBeforeGui();manualDuringPending();
  windowsHandoff();delayedWork();newerEntry();timeoutIndependent();retiredLifetime(false);retiredLifetime(true);
  lateReceipt();manualCallbackThenBroadcast();retryExitWithNewEntryDuringClassification(nullptr);
  retryExitWithNewEntryDuringClassification("s0-wake-classified");retryExitWithNewEntryDuringClassification("sleep-retry-scheduled");
  repeatedAttempt();softwareGate();
  wakeBeforeEntryGui(false);wakeBeforeEntryGui(true);
  criticalDuringPending();missingKernelEvidence(true);missingKernelEvidence(false);actualSettingsCancellation();
 }
 std::cout<<"RESUME_CLASSIFICATION "<<json({{"cases",resumeCases},{"count",resumeCases.size()},{"actualCommon",true},{"actualWorkerArms",3},{"osSleep",false}}).dump()<<"\n";
 return 0;
}catch(const std::exception& e){std::cerr<<"ASSERTION: "<<e.what()<<"\n";return 2;}}
