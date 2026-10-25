#pragma once
// Source-directed scheduling evidence, not attribution of a field failure.
// Actual enqueue and worker execute on two threads. The existing OS-clock port
// pauses the producer after notification publication, before pending commit.
static void msiLateHidePublicationConsumerCases(){
 seed();fixtureWakeScenario=true;
 g_inputCaptureStop=false;g_inputHostExitFence=false;
 g_startupInputState=ymcc::StartupInputState::Ready;g_inputReady=true;
 g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;
 g_sleepRecoveryOutputWanted=true;g_externalPriorityVirtualSuspended=false;
 g_inputHostBuildSettleUntilTick=0;g_realStickTest.virtualTargetPersona="elite";
 g_inputHostEpoch=7;g_inputHostEpochMirror=7;g_inputHostRunId="late-hide-publication-host-7";
 g_msiDinputRouteDinputArmed=true;
 g_physicalReleaseWindowStage=0;g_physicalReleaseWindowDeadline=0;
 {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);
  g_msiMainSource=HidHideMsiMainSourceRecord{};g_msiMainSource.valid=true;
  g_msiMainSource.generation=11;g_msiMainSource.interfacePath="offline-msi-publication-source";
 }
 inputHostLateHideStateRestoreForTest(InputHostLateHideStateSnapshot{});
 {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);
  g_inputHostLifecycleWorkQueue.clear();g_inputHostLifecycleWorkRequested=false;
  g_inputHostLifecycleWorkInFlight=false;
 }
 fixtureEvents.clear();g_startEntrySeam.trace.clear();
 InputHostLateHideEpisodeKey episode;episode.runId=g_inputHostRunId;
 episode.hostGen=7;episode.sourceGen=713;
 HidHideMsiMainSourceRecord record;
 const bool sourceActive=msiDinputManagedMainSourceActive(record);
 inputHostLateHideApplyEpisodeMetadata(&episode,inputHostLateHideManagedMsiRouteType(),sourceActive,record.generation);
 cases.push_back({{"name","actual-producer-notification-before-pending-commit"},
  {"fieldAttribution",false},{"controlledExternalPort","read-only-monotonic-clock"},
  {"producerAndWorkerAreSeparateThreads",true}});
 check(episode.managedMsiRoute&&episode.recordGen==11,"actual managed MSI episode is selected");
 std::promise<void> published,resume;
 auto publicationReady=published.get_future();auto mayResume=resume.get_future().share();
 std::atomic<DWORD> producerId=0;std::atomic<bool> paused=false,barrierTimedOut=false;
 std::string producerError;
 InputHostLateHideEnqueueOutcome outcome=InputHostLateHideEnqueueOutcome::Refused;
 unsigned long long token=0;bool coalesced=false;
 fixtureMonotonicInterleave=[&](){
  if(GetCurrentThreadId()!=producerId.load(std::memory_order_acquire)||paused.load())return;
    InputHostLateHidePendingView pending;
  if(inputHostLateHidePendingRead(&pending))return;
  bool expected=false;if(!paused.compare_exchange_strong(expected,true))return;
  published.set_value();
  if(mayResume.wait_for(std::chrono::seconds(2))!=std::future_status::ready)barrierTimedOut=true;
 };
 std::jthread producer([&](){
  producerId=GetCurrentThreadId();
  try{outcome=inputHostLateHideEnqueueEpisode(episode,"",false,&token,&coalesced);}
  catch(const std::exception& ex){producerError=ex.what();}
 });
 const bool observed=publicationReady.wait_for(std::chrono::seconds(2))==std::future_status::ready;
 check(observed,"actual producer reaches the pending-commit clock boundary");
 // The scheduler probes the real queue mutex. If held, a worker cannot dequeue
 // until metadata commit returns. Otherwise run the actual consumer now.
 bool queueGuarded=false;
 if(observed){
  if(g_inputHostLifecycleWorkMx.try_lock()){g_inputHostLifecycleWorkMx.unlock();inputHostRunQueuedLifecycleWork();}
  else queueGuarded=true;
 }
 cases.back()["queueGuardedAtCommitBoundary"]=queueGuarded;
 resume.set_value();producer.join();fixtureMonotonicInterleave={};
 check(!barrierTimedOut.load()&&producerError.empty(),"controlled scheduling completes without timeout or producer exception");
 check(outcome==InputHostLateHideEnqueueOutcome::Enqueued&&token!=0,"actual enqueue reports accepted work with a pending token");
 InputHostLateHidePendingView after;const bool pending=inputHostLateHidePendingRead(&after);
 std::size_t depth=0;{std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);depth=g_inputHostLifecycleWorkQueue.size();}
 const bool requested=g_inputHostLifecycleWorkRequested.load(std::memory_order_acquire);
 const bool completed=inputHostLateHideEpisodeAlreadyCompleted(episode);
 cases.back()["afterAccepted"]={{"hasPending",pending},{"queueDepth",depth},
  {"requested",requested},{"completed",completed},{"token",after.token}};
 check(completed||(pending&&requested&&depth>0),"accepted MSI late-hide work retains a drainable notification or completed receipt");
 inputHostRunQueuedLifecycleWork();
 check(inputHostLateHideEpisodeAlreadyCompleted(episode),"accepted MSI notification executes without a second producer");
 const auto hidesBefore=std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"hidhide-apply");
 // Observe the existing next-producer recovery separately: it cannot retroactively
 // prove the first accepted notification was retained. No new product retry added.
 unsigned long long nextToken=0;bool nextCoalesced=false;
 const auto next=inputHostLateHideEnqueueEpisode(episode,"",false,&nextToken,&nextCoalesced);
 if(next!=InputHostLateHideEnqueueOutcome::AlreadyCompleted)inputHostRunQueuedLifecycleWork();
 const auto hidesAfter=std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"hidhide-apply");
 cases.back()["afterSeparateProducer"]={{"hidesBefore",hidesBefore},{"hidesAfter",hidesAfter},
  {"completed",inputHostLateHideEpisodeAlreadyCompleted(episode)}};
 check(inputHostLateHideEpisodeAlreadyCompleted(episode)&&hidesAfter==1,
  "a separate later producer can consume the pending episode exactly once");
 cases.back()["events"]=fixtureEvents;
 fixtureWakeScenario=false;
}
