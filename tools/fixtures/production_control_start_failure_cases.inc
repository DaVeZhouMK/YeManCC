
static std::optional<int> sfDispatch(int argc,wchar_t** argv){
 if(argc<3||std::wstring_view(argv[2])!=L"--production-control-worker-create-failure")return {};
 if(argc!=3||!argv[1][0])return 41;
 const auto root=std::filesystem::absolute(argv[1]);if(std::filesystem::exists(root))return 41;
 std::filesystem::create_directories(root);fixtureFilesystemRoot=root.wstring();pcActive=true;
 bool escaped=false,complete=false;std::string error;unsigned startCode=0;std::weak_ptr<SteamOriginalRecovery::Control> weak;
 try{
  InterlockedExchange(&sfArm,1);
  try{FixtureProductionControlOriginal::steamOriginalRecoveryStart();}catch(const std::system_error& value){escaped=true;startCode=static_cast<unsigned>(value.code().value());}catch(...){escaped=true;}
  auto c=SteamOriginalRecovery::current();weak=c;pcRequire(bool(c),"real Control published before CRT thread failure");
  pcRequire(sfBeginCalls==1&&sfRefusals==1&&!sfArm&&SteamOriginalRecovery::worker.finished()&&pcWorkerTid==0,"actual thread construction failure before worker body");
  pcRequire(c->mutationAvailability&&c->mutationAvailability->event()&&c->observationEvent,"original retained events held by failed Control");
  SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::TargetChanged);
  std::size_t pending=0;bool stopped=false;{std::lock_guard lock(c->mx);pending=c->pending.size();stopped=c->stop;}
  const auto beforeCalls=sfBeginCalls;FixtureProductionControlOriginal::steamOriginalRecoveryStart();
  pcRequire(sfBeginCalls==beforeCalls&&SteamOriginalRecovery::current()==c,"existing published Control blocks second launch");
  const auto diagnostic=SteamOriginalRecovery::get();
  std::cout<<"START_FAILURE_OBSERVATION "<<json{{"escaped",escaped},{"systemErrorCode",startCode},{"threadStartCalls",static_cast<LONG>(sfBeginCalls)},{"threadRefusals",static_cast<LONG>(sfRefusals)},{"pendingAfterEvent",pending},{"stop",stopped},{"phase",diagnostic.value("phase",std::string{})},{"reason",diagnostic.value("reason",std::string{})},{"actualWorkerBodyEntered",pcWorkerTid!=0},{"originalWorkerFinished",SteamOriginalRecovery::worker.finished()},{"sourceReady",false}}.dump()<<std::endl;
  steamOriginalRecoveryShutdown();c.reset();{std::lock_guard lock(SteamOriginalRecovery::lifecycleMx);SteamOriginalRecovery::control.reset();}
  const auto account=pcAccountAfterJoin();pcRequire(weak.expired()&&!account.fault&&account.live==0,"retained original Control events return through shutdown/final owner");
  pcRequire(pcRunOneCalls==0&&fixtureAdmissionCalls==0&&fixtureFreshReadCalls==0&&fixturePublisherCalls==0&&engineCalls.empty()&&registryReads==0&&nativeReadProcessQueries==0,"no source or business admission during creation failure");
  pcRequire(!escaped&&stopped&&pending==0&&diagnostic.value("phase",std::string{})=="blocked"&&diagnostic.value("reason",std::string{})=="worker-start-failed","WORKER_START_FAILURE_ESCAPED_OR_ACCEPTED_DEAD_QUEUE");complete=true;
 }catch(const std::exception& value){error=value.what();try{steamOriginalRecoveryShutdown();std::lock_guard lock(SteamOriginalRecovery::lifecycleMx);SteamOriginalRecovery::control.reset();}catch(...){} }
 const auto final=pcAccountAfterJoin();std::cout<<"START_FAILURE_RESULT "<<json{{"complete",complete},{"error",error},{"eventLiveDebt",final.live},{"eventStrictSuccess",!final.fault},{"escaped",escaped},{"refusals",static_cast<LONG>(sfRefusals)},{"workerBodyCalls",pcWorkerTid.load()},{"runOneCalls",pcRunOneCalls.load()},{"sourceReady",false},{"processWideResourcePass",false},{"nativeSteamAuthority",false}}.dump()<<std::endl;
 return complete?0:1;
}
