#pragma once
// Source-directed preemption of the actual BUS producer and actual worker.
// The existing log endpoint schedules another BUS tick after worker validation,
// before completion. This is not a field attribution or a production hook.
static void msiLateHideInflightPrepare(){
 seed();fixtureWakeScenario=true;g_inputCaptureStop=false;g_inputHostExitFence=false;
 g_startupInputState=ymcc::StartupInputState::Ready;g_inputReady=true;
 g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;
 g_sleepRecoveryOutputWanted=true;g_externalPriorityVirtualSuspended=false;
 g_inputHostBuildSettleUntilTick=0;g_realStickTest.virtualTargetPersona="elite";
 g_inputHostEpoch=7;g_inputHostEpochMirror=7;g_inputHostRunId="late-hide-inflight-host-7";
 g_msiDinputRouteDinputArmed=true;g_msiDinputRouteConfirmed=true;
 g_xinputPhysicalSourceLocked=true;g_physicalReleaseWindowStage=0;g_physicalReleaseWindowDeadline=0;
 {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);
  g_msiMainSource=HidHideMsiMainSourceRecord{};g_msiMainSource.valid=true;
  g_msiMainSource.generation=11;g_msiMainSource.interfacePath="offline-msi-inflight-source";
 }
 inputHostLateHideStateRestoreForTest(InputHostLateHideStateSnapshot{});
 {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);
  g_inputHostLifecycleWorkQueue.clear();g_inputHostLifecycleWorkRequested=false;
  g_inputHostLifecycleWorkInFlight=false;
 }
 fixtureEvents.clear();g_startEntrySeam.trace.clear();
}
static void msiLateHideInflightConsumerCases(){
 msiLateHideInflightPrepare();
 cases.push_back({{"name","same-episode-BUS-reentry-during-actual-worker"},
  {"fieldAttribution",false},{"controlledExternalPort","existing-native-lifecycle-log-output"}});
 check(msiDinputMainSourceEligible(),"actual BUS route has a confirmed eligible MSI source");
 inputHostCoordinatorLateSourceVisibilityTick(true,true);
 InputHostLateHidePendingView first;check(inputHostLateHidePendingRead(&first),"actual outer BUS producer commits a pending episode");
 check(first.episode.managedMsiRoute&&first.episode.recordGen==11&&first.token>0,"actual pending has current MSI source identity");
 bool injected=false;unsigned long long tokenDuring=0;std::size_t depthDuring=0;
 fixtureLateHideDuringDecision=[&](){
  injected=true;check(inputHostLateHideInFlightToken()==first.token,"actual worker owns first token at BUS preemption boundary");
  inputHostCoordinatorLateSourceVisibilityTick(true,true);
  InputHostLateHidePendingView during;inputHostLateHidePendingRead(&during);tokenDuring=during.token;
  {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);depthDuring=g_inputHostLifecycleWorkQueue.size();}
 };
 inputHostRunQueuedLifecycleWork();
 check(injected,"real worker validation reaches the declared log endpoint");
 check(inputHostLateHideEpisodeAlreadyCompleted(first.episode),"first actual worker successfully completes the episode");
 cases.back()["duringReentry"]={{"initialToken",first.token},{"tokenAfterBUS",tokenDuring},{"queueDepthAfterBUS",depthDuring}};
 check(tokenDuring==first.token&&depthDuring==0,"same in-flight MSI episode reuses the worker token without enqueueing another hide");
 // Consume any real residual notification. Never clear its state in the test.
 inputHostRunQueuedLifecycleWork();
 const auto hides=std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"hidhide-apply");
 unsigned applied=0;for(const auto& event:fixtureEvents)if(event.value("event","")=="late-hide-complete"&&event["detail"].value("outcome","")=="applied"&&event["detail"].value("runId","")==first.episode.runId)++applied;
 InputHostLateHidePendingView after;const bool pending=inputHostLateHidePendingRead(&after);
 cases.back()["afterDrain"]={{"hideCalls",hides},{"appliedCompletions",applied},{"hasPending",pending},{"requested",g_inputHostLifecycleWorkRequested.load()}};
 check(hides==1&&applied==1&&!pending&&!g_inputHostLifecycleWorkRequested.load(),"one MSI source episode has exactly one actual successful hide after in-flight BUS reentry");
 cases.back()["events"]=fixtureEvents;fixtureWakeScenario=false;
}

static void msiLateHideInflightEdgeCases(){
 using Outcome=InputHostLateHideEnqueueOutcome;
 const auto pending=[](){InputHostLateHidePendingView p;inputHostLateHidePendingRead(&p);return p;};
 const auto depth=[](){std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);return g_inputHostLifecycleWorkQueue.size();};
 const auto hides=[](){return std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"hidhide-apply");};
 msiLateHideInflightPrepare();cases.push_back({{"name","same-MSI-pending-keeps-token-before-worker"},{"fieldAttribution",false}});
 inputHostCoordinatorLateSourceVisibilityTick(true,true);const auto queued=pending();
 for(unsigned i=0;i<100;++i)inputHostCoordinatorLateSourceVisibilityTick(true,true);
 const auto repeated=pending();
 check(repeated.hasPending&&repeated.token==queued.token&&repeated.firstTick==queued.firstTick&&depth()==1,"queued MSI repeat keeps the same token, first tick and single notification");
 inputHostRunQueuedLifecycleWork();check(hides()==1&&inputHostLateHideEpisodeAlreadyCompleted(queued.episode)&&!pending().hasPending,"queued MSI coalescing still completes one real worker hide");
 cases.back()["firstToken"]=queued.token;cases.back()["afterRepeatsToken"]=repeated.token;

 msiLateHideInflightPrepare();cases.push_back({{"name","failed-MSI-inflight-work-allows-next-real-producer-retry"},{"fieldAttribution",false}});
 inputHostCoordinatorLateSourceVisibilityTick(true,true);const auto failed=pending();g_startEntrySeam.hidHideApplyResult=false;
 fixtureLateHideDuringDecision=[](){for(unsigned i=0;i<10;++i)inputHostCoordinatorLateSourceVisibilityTick(true,true);};
 inputHostRunQueuedLifecycleWork();
 check(hides()==1&&!inputHostLateHideEpisodeAlreadyCompleted(failed.episode)&&!pending().hasPending&&depth()==0,"failed coalesced worker does not mark complete or leave a second queued hide");
 g_startEntrySeam.hidHideApplyResult=true;inputHostCoordinatorLateSourceVisibilityTick(true,true);const auto retry=pending();
 check(retry.hasPending&&retry.token>failed.token&&depth()==1,"next BUS producer after actual failure gets a fresh admissible pending token");
 inputHostRunQueuedLifecycleWork();check(hides()==2&&inputHostLateHideEpisodeAlreadyCompleted(failed.episode),"one actual retry successfully completes the failed episode");

 msiLateHideInflightPrepare();cases.push_back({{"name","new-source-generation-during-MSI-worker-is-not-coalesced"},{"fieldAttribution",false}});
 inputHostCoordinatorLateSourceVisibilityTick(true,true);const auto prior=pending();InputHostLateHidePendingView successor;
 fixtureLateHideDuringDecision=[&](){
  {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);++g_msiMainSource.generation;}
  g_xinputTopologyEpoch.fetch_add(1);inputHostCoordinatorLateSourceVisibilityTick(true,true);successor=pending();
 };
 inputHostRunQueuedLifecycleWork();
 check(successor.hasPending&&successor.token>prior.token&&successor.episode.sourceGen!=prior.episode.sourceGen&&successor.episode.recordGen!=prior.episode.recordGen,"new source identity retains an independent successor token");
 check(pending().hasPending&&pending().token==successor.token&&depth()==1,"old worker cleanup cannot clear the new source pending");
 inputHostRunQueuedLifecycleWork();check(hides()==2&&inputHostLateHideEpisodeAlreadyCompleted(successor.episode)&&!pending().hasPending,"new source generation executes and completes through its own queued worker");

 msiLateHideInflightPrepare();cases.push_back({{"name","stranded-MSI-pending-without-owner-is-republished"},{"fieldAttribution",false}});
 const auto hostRun=g_inputHostRunId;unsigned long long firstToken=0;
 InputHostLateHideEpisodeKey stranded;stranded.runId=hostRun;stranded.hostGen=g_inputHostEpoch;stranded.sourceGen=g_xinputTopologyEpoch.load();
 HidHideMsiMainSourceRecord record;const bool live=msiDinputManagedMainSourceActive(record);inputHostLateHideApplyEpisodeMetadata(&stranded,true,live,record.generation);
 firstToken=inputHostLateHidePendingCommit(stranded,"",false);
 check(pending().hasPending&&depth()==0&&!g_inputHostLifecycleWorkRequested.load()&&inputHostLateHideInFlightToken()==0,"controlled retained metadata has neither queued nor in-flight owner");
 inputHostCoordinatorLateSourceVisibilityTick(true,true);
 check(pending().token>firstToken&&depth()==1&&g_inputHostLifecycleWorkRequested.load(),"metadata without an owner is not falsely coalesced");
 inputHostRunQueuedLifecycleWork();check(hides()==1&&inputHostLateHideEpisodeAlreadyCompleted(stranded),"republished retained episode completes normally");

 msiLateHideInflightPrepare();cases.push_back({{"name","completed-between-admission-and-queue-is-not-republished"},{"fieldAttribution",false}});
 inputHostCoordinatorLateSourceVisibilityTick(true,true);const auto completed=pending();inputHostRunQueuedLifecycleWork();
 const std::string tuple;InputHostLateHidePublication publication{&completed.episode,&tuple,false};bool coalesced=false;
 const bool accepted=inputHostQueueLifecycleWorkInternal("late-hide-after-source-return",&coalesced,&publication);
 const auto observedCompleted=[]<typename Publication>(const Publication& value){
  if constexpr(requires { value.alreadyCompleted; }) return value.alreadyCompleted;
  else return false; // Preserved pre-fix product type can still compile the red mode.
 };
 check(accepted&&observedCompleted(publication)&&!coalesced&&publication.token==0,"queue-boundary completed result retains explicit completed ownership");
 check(depth()==0&&!pending().hasPending&&!g_inputHostLifecycleWorkRequested.load()&&hides()==1,"completed episode cannot acquire a second queue notification");


 // Cross-generation ownership is exercised through the actual BUS and worker.
 // All preemption remains at the existing diagnostic output endpoint; no
 // production code, timer, driver call or lifecycle budget is replaced.
 {
  msiLateHideInflightPrepare();cases.push_back({{"name","successor-pending-survives-failed-old-worker"},{"fieldAttribution",false}});
  inputHostCoordinatorLateSourceVisibilityTick(true,true);const auto prior=pending();InputHostLateHidePendingView next;
  g_startEntrySeam.hidHideApplyResult=false;
  fixtureLateHideDuringDecision=[&](){
   {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);++g_msiMainSource.generation;}
   g_xinputTopologyEpoch.fetch_add(1);inputHostCoordinatorLateSourceVisibilityTick(true,true);next=pending();
  };
  inputHostRunQueuedLifecycleWork();
  check(next.hasPending&&next.token>prior.token&&next.episode.recordGen!=prior.episode.recordGen,"failed old worker observes a separately published successor identity");
  check(pending().hasPending&&pending().token==next.token&&depth()==1&&g_inputHostLifecycleWorkRequested.load()&&!g_inputHostLifecycleWorkInFlight.load()&&inputHostLateHideInFlightToken()==0,"failed old worker returns its claim without clearing successor pending or notification");
  check(hides()==1&&!inputHostLateHideEpisodeAlreadyCompleted(prior.episode),"failed old hide cannot claim a completed source episode");
  g_startEntrySeam.hidHideApplyResult=true;inputHostRunQueuedLifecycleWork();
  check(hides()==2&&inputHostLateHideEpisodeAlreadyCompleted(next.episode)&&!pending().hasPending&&depth()==0&&!g_inputHostLifecycleWorkRequested.load(),"successor after failed old hide completes without another BUS producer");
  cases.back()["priorToken"]=prior.token;cases.back()["successorToken"]=next.token;cases.back()["events"]=fixtureEvents;
 }
 {
  msiLateHideInflightPrepare();cases.push_back({{"name","successor-pending-survives-old-decision-exception"},{"fieldAttribution",false}});
  inputHostCoordinatorLateSourceVisibilityTick(true,true);const auto prior=pending();InputHostLateHidePendingView next;bool injected=false;
  fixtureLateHideDuringDecision=[&](){
   {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);++g_msiMainSource.generation;}
   g_xinputTopologyEpoch.fetch_add(1);inputHostCoordinatorLateSourceVisibilityTick(true,true);next=pending();injected=true;
   throw std::runtime_error("offline-successor-published-before-old-hide");
  };
  inputHostRunQueuedLifecycleWork();
  check(injected&&next.hasPending&&next.token>prior.token,"actual decision endpoint publishes successor before the declared exception");
  check(hides()==0&&!inputHostLateHideEpisodeAlreadyCompleted(prior.episode),"decision exception issues no hide and cannot report old completion");
  check(pending().hasPending&&pending().token==next.token&&depth()==1&&g_inputHostLifecycleWorkRequested.load()&&!g_inputHostLifecycleWorkInFlight.load()&&inputHostLateHideInFlightToken()==0,"exceptional old owner retires marker while retaining queued successor responsibility");
  inputHostRunQueuedLifecycleWork();
  check(hides()==1&&inputHostLateHideEpisodeAlreadyCompleted(next.episode)&&!pending().hasPending&&depth()==0&&!g_inputHostLifecycleWorkRequested.load(),"queued successor after old decision exception completes without republishing");
  cases.back()["priorToken"]=prior.token;cases.back()["successorToken"]=next.token;cases.back()["events"]=fixtureEvents;
 }
 {
  msiLateHideInflightPrepare();cases.push_back({{"name","multiple-source-generations-retain-latest-single-notification"},{"fieldAttribution",false}});
  inputHostCoordinatorLateSourceVisibilityTick(true,true);const auto prior=pending();InputHostLateHidePendingView replaced,latest;bool stable=false;
  fixtureLateHideDuringDecision=[&](){
   for(unsigned generation=0;generation<2;++generation){
    {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);++g_msiMainSource.generation;}
    g_xinputTopologyEpoch.fetch_add(1);inputHostCoordinatorLateSourceVisibilityTick(true,true);
    if(generation==0)replaced=pending();else latest=pending();
   }
   for(unsigned repeat=0;repeat<100;++repeat)inputHostCoordinatorLateSourceVisibilityTick(true,true);
   stable=pending().token==latest.token&&pending().firstTick==latest.firstTick&&depth()==1;
  };
  inputHostRunQueuedLifecycleWork();
  check(replaced.token>prior.token&&latest.token>replaced.token&&latest.episode.recordGen>replaced.episode.recordGen,"distinct successive source generations each receive a new token");
  check(stable&&pending().hasPending&&pending().token==latest.token&&depth()==1,"one hundred latest-source notifications retain one pending token and original first tick");
  check(hides()==1&&inputHostLateHideEpisodeAlreadyCompleted(prior.episode)&&!inputHostLateHideEpisodeAlreadyCompleted(replaced.episode),"old completion cannot claim a replaced queued generation");
  inputHostRunQueuedLifecycleWork();
  check(hides()==2&&inputHostLateHideEpisodeAlreadyCompleted(latest.episode)&&!pending().hasPending&&depth()==0&&!g_inputHostLifecycleWorkRequested.load(),"only latest queued generation executes after original worker completion");
  cases.back()["priorToken"]=prior.token;cases.back()["replacedToken"]=replaced.token;cases.back()["latestToken"]=latest.token;cases.back()["events"]=fixtureEvents;
 }
 {
  msiLateHideInflightPrepare();cases.push_back({{"name","successor-pending-survives-old-completion-log-exception"},{"fieldAttribution",false}});
  inputHostCoordinatorLateSourceVisibilityTick(true,true);const auto prior=pending();InputHostLateHidePendingView next;
  fixtureLateHideDuringDecision=[&](){
   {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);++g_msiMainSource.generation;}
   g_xinputTopologyEpoch.fetch_add(1);inputHostCoordinatorLateSourceVisibilityTick(true,true);next=pending();throwLogEvent="late-hide-complete";
  };
  inputHostRunQueuedLifecycleWork();
  check(hides()==1&&inputHostLateHideEpisodeAlreadyCompleted(prior.episode),"completion-log exception retains actual old hide success already committed");
  check(next.hasPending&&pending().token==next.token&&depth()==1&&g_inputHostLifecycleWorkRequested.load()&&!g_inputHostLifecycleWorkInFlight.load()&&inputHostLateHideInFlightToken()==0,"post-hide exception retires old owner without discarding successor notification");
  inputHostCoordinatorLateSourceVisibilityTick(true,true);
  check(pending().token==next.token&&depth()==1,"BUS repeat after post-hide exception reuses the existing queued successor");
  inputHostRunQueuedLifecycleWork();
  check(hides()==2&&inputHostLateHideEpisodeAlreadyCompleted(next.episode)&&!pending().hasPending&&depth()==0&&!g_inputHostLifecycleWorkRequested.load(),"post-hide exception cannot replay old hide or strand queued successor");
  cases.back()["priorToken"]=prior.token;cases.back()["successorToken"]=next.token;cases.back()["events"]=fixtureEvents;
 }

 for(const auto& [name,family,product,armed]:std::vector<std::tuple<const char*,YmccFamily,const char*,bool>>{
   {"ROG",YmccFamily::AsusRogAllyX,"ROG Ally X",true},
   {"GPD",YmccFamily::Gpd,"GPD WIN 5",true},
   {"unknown-MSI-board",YmccFamily::MsiClaw,"MS-UNREGISTERED",true},
   {"disarmed-MSI",YmccFamily::MsiClaw,"MS-1T52",false}}){
  msiLateHideInflightPrepare();cases.push_back({{"name",std::string("legacy-pending-semantics/")+name},{"fieldAttribution",false}});
  g_machineIdentity.family=family;g_machineIdentity.product=product;g_msiDinputRouteDinputArmed=armed;
  InputHostLateHideEpisodeKey legacy;legacy.runId=g_inputHostRunId;legacy.hostGen=g_inputHostEpoch;legacy.sourceGen=g_xinputTopologyEpoch.load();
  inputHostLateHideApplyEpisodeMetadata(&legacy,inputHostLateHideManagedMsiRouteType(),false,0);
  unsigned long long t1=0,t2=0;bool c1=false,c2=false;
  const auto first=inputHostLateHideEnqueueEpisode(legacy,"",false,&t1,&c1);
  const auto second=inputHostLateHideEnqueueEpisode(legacy,"",false,&t2,&c2);
  check(!inputHostLateHideManagedMsiRouteType()&&first==Outcome::Enqueued&&second==Outcome::Coalesced&&t2>t1&&c2&&depth()==1,"non-managed route retains original pending token rotation and queue semantics");
 }
 fixtureWakeScenario=false;
}

static void msiLateHideOrphanedInflightCase(){
 msiLateHideInflightPrepare();cases.push_back({{"name","actual-diagnostic-exception-retires-worker-but-keeps-late-hide-marker"},{"fieldAttribution",false}});
 inputHostCoordinatorLateSourceVisibilityTick(true,true);InputHostLateHidePendingView first;inputHostLateHidePendingRead(&first);
 throwLogEvent="late-hide-exec-decision";inputHostRunQueuedLifecycleWork();
 InputHostLateHidePendingView retained;inputHostLateHidePendingRead(&retained);
 const bool ownerActive=g_inputHostLifecycleWorkInFlight.load();
 check(!ownerActive&&retained.hasPending&&g_inputHostLifecycleWorkQueue.empty(),"actual worker exception leaves retained metadata after its lifecycle claim exits");
 cases.back()["afterException"]={{"lifecycleOwnerActive",ownerActive},{"lateHideMarkerToken",inputHostLateHideInFlightToken()},{"hasPending",retained.hasPending},{"token",retained.token}};
 inputHostCoordinatorLateSourceVisibilityTick(true,true);InputHostLateHidePendingView retry;inputHostLateHidePendingRead(&retry);
 check(retry.hasPending&&retry.token>first.token&&g_inputHostLifecycleWorkRequested.load()&&g_inputHostLifecycleWorkQueue.size()==1,"retained late-hide marker without a running owner permits a fresh BUS notification");
 inputHostRunQueuedLifecycleWork();
 const auto hides=std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"hidhide-apply");
 check(hides==1&&inputHostLateHideEpisodeAlreadyCompleted(first.episode),"a new actual worker completes the episode after the diagnostic exception");
 cases.back()["afterRetry"]={{"hideCalls",hides},{"completed",inputHostLateHideEpisodeAlreadyCompleted(first.episode)}};
 cases.back()["events"]=fixtureEvents;fixtureWakeScenario=false;
}
