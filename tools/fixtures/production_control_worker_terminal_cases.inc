
static std::optional<int> wtDispatch(int argc,wchar_t** argv){
 if(argc<3)return {};
 const std::wstring_view name(argv[2]);unsigned mode=0;
 if(name==L"--production-control-worker-storage-terminal")mode=1;
 else if(name==L"--production-control-worker-take-next-terminal")mode=2;
 else if(name==L"--production-control-worker-invalid-config-live")mode=3;
 else return {};
 if(argc!=3||!argv[1][0])return 41;
 if(mode==3){wtMode=3;const auto result=pcRun(argv[1],0);std::cout<<"WORKER_INVALID_CONFIG_LIVE_RESULT "<<json{{"code",result},{"invalidConfigReads",wtInvalidConfigReads.load()},{"ordinaryOriginalControlExecuted",true},{"sourceReady",false}}.dump()<<std::endl;return result;}
 const auto root=std::filesystem::absolute(argv[1]);if(std::filesystem::exists(root))return 41;
 std::filesystem::create_directories(root);fixtureFilesystemRoot=root.wstring();pcActive=true;wtMode=mode;wtTakeNextArmed=(mode==2);
 if(mode==1){std::ofstream sentinel(root/L"runtime",std::ios::binary);sentinel<<"terminal-runtime-root-sentinel";sentinel.close();if(!sentinel)throw std::runtime_error("terminal-storage-sentinel-create");}
 bool complete=false,joined=false,stopped=false,activeAfter=false,enabledAfter=false,ownsAfter=false;std::string error;std::size_t pendingBefore=0,pendingAfter=0;unsigned long long sequenceBefore=0,sequenceAfter=0;json diagnostic;
 std::weak_ptr<SteamOriginalRecovery::Control> weakControl;std::weak_ptr<void> weakObservation;std::weak_ptr<ymcc::steamcommand::AvailabilityEvent> weakSignal;
 try{
  pcEntered=pcCreateEventW(nullptr,TRUE,FALSE,nullptr);pcRelease=pcCreateEventW(nullptr,TRUE,FALSE,nullptr);pcRequire(pcEntered&&pcRelease,"worker-terminal own handshake events");
  FixtureProductionControlOriginal::steamOriginalRecoveryStart();auto c=SteamOriginalRecovery::current();pcRequire(bool(c),"actual published Control");weakControl=c;weakObservation=c->observationEvent;weakSignal=c->mutationAvailability;
  if(mode==2){pcRequire(WaitForSingleObject(pcEntered,5000)==WAIT_OBJECT_0&&!SteamOriginalRecovery::worker.finished(),"actual worker reached waiting-events gate");pcRequire(SetEvent(pcRelease)!=FALSE,"release real original worker to takeNext");}
  const auto until=GetTickCount64()+5000;while(!SteamOriginalRecovery::worker.finished()&&GetTickCount64()<until)Sleep(1);
  pcRequire(SteamOriginalRecovery::worker.finished(),"actual successful worker task/captures completed");SteamOriginalRecovery::worker.rethrowIfFailed();
  pcRequire(sfBeginCalls==1&&sfRefusals==0&&wtTakeNextThrows==(mode==2?1u:0u),"real worker launch plus exactly selected terminal boundary");
  {std::lock_guard lock(c->mx);pendingBefore=c->pending.size();sequenceBefore=c->sequence;stopped=c->stop;}
  activeAfter=c->active.load();enabledAfter=c->enabled.load();ownsAfter=c->ownsRunning.load();
  pcRequire(c->mutationAvailability&&c->mutationAvailability->event()&&c->observationEvent&&!weakControl.expired()&&!weakObservation.expired()&&!weakSignal.expired(),"terminal worker retains exact original Control resources until Shutdown/final owner");
  SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::TargetChanged);
  {std::lock_guard lock(c->mx);pendingAfter=c->pending.size();sequenceAfter=c->sequence;}
  const auto starts=sfBeginCalls;FixtureProductionControlOriginal::steamOriginalRecoveryStart();pcRequire(sfBeginCalls==starts&&SteamOriginalRecovery::current()==c,"finished original owner never replaced or automatically retried");diagnostic=SteamOriginalRecovery::get();
  std::cout<<"WORKER_TERMINAL_OBSERVATION "<<json{{"mode",mode},{"successfulThreadStarts",static_cast<LONG>(sfBeginCalls)},{"threadRefusals",static_cast<LONG>(sfRefusals)},{"workerFinished",SteamOriginalRecovery::worker.finished()},{"stopBeforeManualShutdown",stopped},{"pendingBeforeLateEvent",pendingBefore},{"pendingAfterLateEvent",pendingAfter},{"sequenceBeforeLateEvent",sequenceBefore},{"sequenceAfterLateEvent",sequenceAfter},{"activeAfterWorker",activeAfter},{"enabledAfterWorker",enabledAfter},{"ownsAfterWorker",ownsAfter},{"takeNextThrows",wtTakeNextThrows.load()},{"phase",diagnostic.value("phase",std::string{})},{"reason",diagnostic.value("reason",std::string{})},{"resourcesStillOriginalOwnerHeld",true},{"runOneCalls",pcRunOneCalls.load()},{"sourceReady",false}}.dump()<<std::endl;
  if(mode==1){std::ifstream sentinel(root/L"runtime",std::ios::binary);const std::string bytes(std::istreambuf_iterator<char>(sentinel),{});pcRequire(bytes=="terminal-runtime-root-sentinel"&&diagnostic.value("phase",std::string())=="blocked"&&diagnostic.value("reason",std::string())=="storage-type-refused","actual original storage type refusal and sentinel bytes preserved");}
  else pcRequire(diagnostic.value("reason",std::string())=="worker-exception","original outer worker exception diagnostic preserved");
  steamOriginalRecoveryShutdown();joined=true;pcRequire(SteamOriginalRecovery::worker.finished()&&!pcGateTimedOut,"original Shutdown joins already returned worker");
  c.reset();{std::lock_guard lock(SteamOriginalRecovery::lifecycleMx);SteamOriginalRecovery::control.reset();}
  pcRequire(weakControl.expired()&&weakObservation.expired()&&weakSignal.expired(),"only final original owner releases event captures");
  pcRequire(pcCloseHandle(pcRelease)&&pcCloseHandle(pcEntered),"return own terminal handshake events");pcRelease=nullptr;pcEntered=nullptr;
  const auto final=pcAccountAfterJoin();pcRequire(final.live==0&&!final.fault,"direct event accounting zero debt");
  pcRequire(pcRunOneCalls==0&&fixtureAdmissionCalls==0&&fixtureFreshReadCalls==0&&fixturePublisherCalls==0&&engineCalls.empty()&&registryReads==0&&nativeReadProcessQueries==0,"selected terminal precedes all business/source/native consumers");
  pcRequire(stopped&&pendingAfter==pendingBefore&&sequenceAfter==sequenceBefore&&!activeAfter&&!enabledAfter&&!ownsAfter,"TERMINAL_WORKER_STILL_ACCEPTS_EVENTS");complete=true;
 }catch(const std::exception& value){error=value.what();try{steamOriginalRecoveryRequestStop();if(pcRelease)SetEvent(pcRelease);steamOriginalRecoveryShutdown();joined=true;std::lock_guard lock(SteamOriginalRecovery::lifecycleMx);SteamOriginalRecovery::control.reset();}catch(...){}if(joined){if(pcRelease&&pcCloseHandle(pcRelease))pcRelease=nullptr;if(pcEntered&&pcCloseHandle(pcEntered))pcEntered=nullptr;}}
 const auto final=pcAccountAfterJoin();std::cout<<"WORKER_TERMINAL_RESULT "<<json{{"mode",mode},{"complete",complete},{"error",error},{"joined",joined},{"eventLiveDebt",final.live},{"eventStrictSuccess",!final.fault},{"weakControlExpired",weakControl.expired()},{"weakObservationExpired",weakObservation.expired()},{"weakSignalExpired",weakSignal.expired()},{"runOneCalls",pcRunOneCalls.load()},{"sourceReady",false},{"nativeSteamAuthority",false},{"processWideResourcePass",false}}.dump()<<std::endl;
 return complete?0:1;
}
