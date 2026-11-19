
static std::optional<int> qaDispatch(int argc,wchar_t** argv){
 if(argc<3||std::wstring_view(argv[2])!=L"--production-control-initial-enqueue-failure")return {};
 if(argc!=3||!argv[1][0])return 41;
 const auto root=std::filesystem::absolute(argv[1]);if(std::filesystem::exists(root))return 41;
 std::filesystem::create_directories(root);fixtureFilesystemRoot=root.wstring();pcActive=true;
 bool escaped=false,complete=false,joined=false,failureMarkerAfterJoin=false;std::string error;std::weak_ptr<SteamOriginalRecovery::Control> weak;
 unsigned denied=0,entries=0;std::size_t deniedBytes=0,pendingAfter=0;bool stopped=false;json diagnostic;
 try{
  pcEntered=pcCreateEventW(nullptr,TRUE,FALSE,nullptr);pcRelease=pcCreateEventW(nullptr,TRUE,FALSE,nullptr);pcRequire(pcEntered&&pcRelease,"initial queue owned handshake events");
  qaArmAfterLaunch=true;
  try{FixtureProductionControlOriginal::steamOriginalRecoveryStart();}catch(const std::bad_alloc&){escaped=true;}
  denied=qaDeniedAllocations;deniedBytes=qaDeniedBytes;entries=qaQueueEntries;
  pcRequire(denied==1&&deniedBytes>0&&!qaArmed&&!qaArmAfterLaunch&&!qaInEnqueue,"exact one actual deque allocation refusal after successful launch");
  auto c=SteamOriginalRecovery::current();weak=c;pcRequire(bool(c),"successful Start published actual Control");
  pcRequire(sfBeginCalls==1&&sfRefusals==0&&WaitForSingleObject(pcEntered,5000)==WAIT_OBJECT_0&&pcWorkerTid!=0,"real successful RecoveryWorker reached original run publish");
  pcRequire(!SteamOriginalRecovery::worker.finished()&&c->mutationAvailability&&c->mutationAvailability->event()&&c->observationEvent,"worker and original retained resources remain live before join");
  SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::TargetChanged);
  {std::lock_guard lock(c->mx);pendingAfter=c->pending.size();stopped=c->stop;}
  diagnostic=SteamOriginalRecovery::get();
  const auto starts=sfBeginCalls;FixtureProductionControlOriginal::steamOriginalRecoveryStart();pcRequire(sfBeginCalls==starts&&SteamOriginalRecovery::current()==c,"published owner never retries startup");
  std::cout<<"INITIAL_QUEUE_OBSERVATION "<<json{{"escaped",escaped},{"successfulThreadStarts",static_cast<LONG>(sfBeginCalls)},{"threadCreateRefusals",static_cast<LONG>(sfRefusals)},{"deniedAllocations",denied},{"deniedBytes",deniedBytes},{"initialQueueEntries",entries},{"pendingAfterEvent",pendingAfter},{"stop",stopped},{"activeWorkerTid",pcWorkerTid.load()},{"phase",diagnostic.value("phase",std::string{})},{"reason",diagnostic.value("reason",std::string{})},{"initialEventEnqueueFailed",diagnostic.value("initialEventEnqueueFailed",false)},{"originalControlStillRetainsEvents",bool(c->mutationAvailability&&c->mutationAvailability->event()&&c->observationEvent)},{"sourceReady",false}}.dump()<<std::endl;
  steamOriginalRecoveryRequestStop();pcRequire(SetEvent(pcRelease)!=FALSE,"release real worker for original stop path");steamOriginalRecoveryShutdown();joined=true;
  pcRequire(SteamOriginalRecovery::worker.finished()&&pcStoppedPublishes==1&&!pcGateTimedOut,"original run naturally returned and Shutdown joined");
  failureMarkerAfterJoin=SteamOriginalRecovery::get().value("initialEventEnqueueFailed",false);pcRequire(failureMarkerAfterJoin==!escaped,"initial enqueue diagnostic marker survives later waiting/stopped publications");SteamOriginalRecovery::worker.rethrowIfFailed();c.reset();{std::lock_guard lock(SteamOriginalRecovery::lifecycleMx);SteamOriginalRecovery::control.reset();}
  pcRequire(weak.expired()&&pcCloseHandle(pcRelease)&&pcCloseHandle(pcEntered),"final original owner and own handshake events returned");pcRelease=nullptr;pcEntered=nullptr;
  const auto final=pcAccountAfterJoin();pcRequire(final.live==0&&!final.fault,"exact event accounting no debt");
  pcRequire(pcRunOneCalls==0&&fixtureAdmissionCalls==0&&fixtureFreshReadCalls==0&&fixturePublisherCalls==0&&engineCalls.empty()&&registryReads==0&&nativeReadProcessQueries==0,"zero source/admit/engine/real Steam work");
  pcRequire(!escaped&&stopped&&pendingAfter==0&&diagnostic.value("initialEventEnqueueFailed",false),"INITIAL_QUEUE_FAILURE_ESCAPED_OR_ACCEPTED_NEW_EVENTS");complete=true;
 }catch(const std::exception& value){error=value.what();qaArmed=false;qaArmAfterLaunch=false;
  try{steamOriginalRecoveryRequestStop();if(pcRelease)SetEvent(pcRelease);steamOriginalRecoveryShutdown();joined=true;std::lock_guard lock(SteamOriginalRecovery::lifecycleMx);SteamOriginalRecovery::control.reset();}catch(...){}
  if(joined){if(pcRelease&&pcCloseHandle(pcRelease))pcRelease=nullptr;if(pcEntered&&pcCloseHandle(pcEntered))pcEntered=nullptr;}
 }
 const auto final=pcAccountAfterJoin();std::cout<<"INITIAL_QUEUE_RESULT "<<json{{"complete",complete},{"error",error},{"escaped",escaped},{"joined",joined},{"failureMarkerAfterJoin",failureMarkerAfterJoin},{"deniedAllocations",denied},{"eventLiveDebt",final.live},{"eventStrictSuccess",!final.fault},{"weakControlExpired",weak.expired()},{"runOneCalls",pcRunOneCalls.load()},{"sourceReady",false},{"processWideResourcePass",false},{"nativeSteamAuthority",false}}.dump()<<std::endl;
 return complete?0:1;
}
