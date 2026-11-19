
struct ExitFixtureToken { std::atomic<unsigned>* disposed; ~ExitFixtureToken(){disposed->fetch_add(1);} };
struct ExitFixtureGuard {
 std::atomic<bool> release{false},entered{false},returned{false};
 std::atomic<unsigned> activeDisposed{0},pendingDisposed{0},pendingCalls{0};
 std::thread stopper;HANDLE observedThread=nullptr;
 std::unique_lock<std::mutex> traceLock{g_traceMx,std::defer_lock};
 bool observedThreadReturned=false,duplicateClosed=false,traceClosed=false;
 std::string trace;
 ExitFixtureGuard(){
  const HANDLE handle=CreateFileW(L"trace-owned.log",GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("owned trace CreateNew");
  if(!CloseHandle(handle))throw std::runtime_error("owned trace create handle return");
  if(_wfopen_s(&g_traceFile,L"trace-owned.log",L"r+b")!=0||!g_traceFile)throw std::runtime_error("owned existing trace FILE");
  g_traceOn=true;g_traceT0=GetTickCount64();
 }
 template<class Predicate>static bool wait(Predicate predicate,unsigned ms=2000){
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
  while(!predicate()){if(std::chrono::steady_clock::now()>=deadline)return false;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return true;
 }
 void observe(){if(g_poolThreads.empty())throw std::runtime_error("missing original owned worker");if(!DuplicateHandle(GetCurrentProcess(),g_poolThreads.front().native_handle(),GetCurrentProcess(),&observedThread,0,FALSE,DUPLICATE_SAME_ACCESS))throw std::runtime_error("exact worker observer duplicate");}
 void finish(){
  schedulePoolFaultAfter=-1;if(traceLock.owns_lock())traceLock.unlock();release=true;
  if(stopper.joinable())stopper.join();
  if(!g_poolThreads.empty())poolStop(2000);
  if(observedThread){observedThreadReturned=WaitForSingleObject(observedThread,2000)==WAIT_OBJECT_0;duplicateClosed=CloseHandle(observedThread)!=FALSE;observedThread=nullptr;}
  g_traceOn=false;
  if(g_traceFile){fflush(g_traceFile);fseek(g_traceFile,0,SEEK_SET);char data[512]{};const auto bytes=fread(data,1,sizeof(data)-1,g_traceFile);trace.assign(data,bytes);traceClosed=fclose(g_traceFile)==0;g_traceFile=nullptr;}
 }
 ~ExitFixtureGuard(){finish();}
};
#if defined(YMCC_STATIC_WAKE_REAL_DIAGNOSTIC)
static void fixtureDiagnosticWrite(const char* kind,const char* name,const json& detail,const std::string* encoded){
 fixtureDiagEntered.fetch_add(1,std::memory_order_release);
 while(fixtureDiagBlock.load(std::memory_order_acquire)&&fixtureDiagRelease&&!fixtureDiagRelease->load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));
 const auto* replay=inputHostCriticalDiagnosticReplayTime();const auto* domainPayload=inputHostCriticalDiagnosticReplayDomainPayload();
 {std::lock_guard<std::mutex> lock(fixtureDiagWriteMx);fixtureDiagWrites.push_back({{"kind",kind},{"name",name},{"detail",detail},{"replayTime",replay?*replay:""},{"encoded",encoded?*encoded:""},{"domainPayload",domainPayload?*domainPayload:""},{"threadId",GetCurrentThreadId()}});}
 fixtureDiagReturned.fetch_add(1,std::memory_order_release);
}
static int realPoolDiagnosticCase(const std::string& selected){
 unsigned checks=0;std::string failure;json before=nullptr,atStop=nullptr,final=nullptr,refused=nullptr,cooldown=nullptr,writes=json::array();
 DWORD workerWaitAtStop=WAIT_FAILED,workerExitAtStop=0;bool observerReturned=false,observerClosed=false,traceClosed=false,ownerPreserved=false;long long stopElapsedUs=0;unsigned poolEntries=0;std::uint64_t activeCharge=0;
 const auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);};
 try{
  if(!(selected=="schedule-pool-exit-diag-active"||selected=="schedule-pool-exit-diag-abandoned"||selected=="schedule-pool-exit-diag-refusal-retry"))throw std::runtime_error("unknown exact diagnostic pool scenario");
  reset();g_poolSize=1;fixturePoolThreadEntries=0;fixtureDiagEntered=0;fixtureDiagReturned=0;fixtureDiagBlock=false;
  expect(inputHostCriticalDiagnostics::queue.load()==nullptr&&g_poolThreads.empty(),"actual diagnostic owner is initially absent and has no executor");
  inputHostDeferNativeDiagnostic("owned-before-start",{{"time","owned-before"}},false,false);
  expect(inputHostCriticalDiagnostics::unavailable==1&&inputHostCriticalDiagnostics::queue.load()==nullptr&&g_poolThreads.empty(),"offer before cold start cannot allocate diagnostic owner or create worker");
  ExitFixtureGuard guard;fixtureDiagRelease=&guard.release;
  const bool active=selected=="schedule-pool-exit-diag-active";
  expect(poolSubmit([&]{guard.entered=true;if(!active)while(!guard.release.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));}),"actual cold pool accepts one finite owned callback");
  expect(ExitFixtureGuard::wait([&]{return guard.entered.load();}),"actual worker entered before diagnostic offers");guard.observe();
  auto* owner=inputHostCriticalDiagnostics::queue.load();expect(owner!=nullptr&&g_poolStarted&&g_poolThreads.size()==1,"original cold startup publishes exactly one real diagnostic owner and one worker");
  if(active){
   fixtureDiagBlock=true;json detail={{"time","owned-active-time"},{"value",159}};
   activeCharge=std::string("owned.active").size()+std::string("owned-active-time").size()+detail.dump().size();
   inputHostDeferNativeDiagnostic("owned.active",detail,false,false);
   expect(ExitFixtureGuard::wait([&]{return fixtureDiagEntered.load()==1;}),"actual Dispatcher<Record> drain reaches explicit active sink");
   inputHostDeferDomainDiagnostic("owned","owned.pending.domain",{{"value",1}});
   inputHostDeferNativeDiagnostic("owned.pending.mirror",{{"value",2}},false,true);
   inputHostDeferAppDiagnostic("owned.pending.app","pending");before=inputHostCriticalDiagnosticsEvidence();
   expect(before.at("accepted")==4&&before.at("pendingRows")==3&&before.at("sinkActive")==true&&before.at("payloadBytes").get<std::uint64_t>()>activeCharge,"active data and three real pending records are all charged before close");
  }else if(selected=="schedule-pool-exit-diag-abandoned"){
   inputHostDeferNativeDiagnostic("owned.pending",{{"time","owned-abandon-time"}},false,false);before=inputHostCriticalDiagnosticsEvidence();
   expect(before.at("accepted")==1&&before.at("pendingRows")==1&&before.at("sinkActive")==false&&before.at("payloadBytes").get<std::uint64_t>()>0,"accepted actual drain callback remains behind exact owned active worker");
   expect(fixtureDiagEntered==0,"pending accepted diagnostic has not reached sink");
  }else{
   std::atomic<unsigned> drained{0};for(unsigned i=0;i<IPC_POOL_QUEUE_LIMIT;++i)expect(poolSubmit([&]{drained.fetch_add(1);}),"fill original finite pool queue without starting another worker");
   inputHostDeferNativeDiagnostic("owned.refused",{{"time","owned-refused-time"}},false,false);refused=inputHostCriticalDiagnosticsEvidence();
   expect(refused.at("submitFailed")==1&&refused.at("pendingRows")==0&&refused.at("payloadBytes")==0&&refused.at("closed")==false,"actual existing-pool refusal discards data without sealing diagnostic owner");
   inputHostDeferAppDiagnostic("owned.cooldown","owned");cooldown=inputHostCriticalDiagnosticsEvidence();expect(cooldown.at("rejectedCooldown")==1&&fixtureDiagEntered==0,"original one-second cooldown refuses immediate retry without sink work");
   guard.release=true;expect(ExitFixtureGuard::wait([&]{return drained.load()==IPC_POOL_QUEUE_LIMIT;}),"all exact preexisting queued callbacks drain naturally");
   std::this_thread::sleep_for(std::chrono::milliseconds(1001));
   inputHostDeferNativeDiagnostic("owned.native",{{"time","owned-native-time"},{"value",3}},false,false);
   expect(ExitFixtureGuard::wait([&]{return fixtureDiagReturned.load()==1;}),"later original offer after cooldown resumes same real owner");
   inputHostDeferNativeDiagnostic("owned.mirror",{{"value",4}},false,true);expect(ExitFixtureGuard::wait([&]{return fixtureDiagReturned.load()==2;}),"actual runtime mirror route returns");
   inputHostDeferDomainDiagnostic("owned","owned.domain",{{"value",5}});expect(ExitFixtureGuard::wait([&]{return fixtureDiagReturned.load()==3;}),"actual runtime domain route returns");
   inputHostDeferAppDiagnostic("owned.app","owned-message");expect(ExitFixtureGuard::wait([&]{return fixtureDiagReturned.load()==4;}),"actual runtime app route returns");
   expect(ExitFixtureGuard::wait([&]{auto s=owner->snapshot();return !s.sinkActive&&s.pendingRows==0&&s.payloadBytes==0;}),"all completed records return serialized payload charge");before=inputHostCriticalDiagnosticsEvidence();
   expect(before.at("accepted")==4&&before.at("sinkReturned")==4&&before.at("submitFailed")==1&&before.at("closed")==false,"refusal retry keeps actual owner and accepts each later original route once");
  }
  const auto begin=std::chrono::steady_clock::now();poolStop(active||selected=="schedule-pool-exit-diag-abandoned"?30:2000);stopElapsedUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
  atStop=inputHostCriticalDiagnosticsEvidence();workerWaitAtStop=WaitForSingleObject(guard.observedThread,0);expect(GetExitCodeThread(guard.observedThread,&workerExitAtStop)!=FALSE,"exact original worker exit code can be observed");
  ownerPreserved=inputHostCriticalDiagnostics::queue.load()==owner;expect(ownerPreserved&&atStop.at("closed")==true&&g_poolStopping&&!g_poolStarted&&g_poolThreads.empty()&&jobs.empty(),"actual diagnostic close and original public pool stop seal same owner");
  if(active){expect(workerWaitAtStop==WAIT_TIMEOUT&&workerExitAtStop==STILL_ACTIVE&&atStop.at("sinkActive")==true&&atStop.at("payloadBytes")==activeCharge&&atStop.at("pendingRows")==0&&atStop.at("pendingDiscarded")==3&&atStop.at("sinkReturned")==0,"closed owner retains exactly active record charge until owned sink returns");}
  else if(selected=="schedule-pool-exit-diag-abandoned"){expect(workerWaitAtStop==WAIT_TIMEOUT&&atStop.at("callbackAbandoned")==1&&atStop.at("pendingDiscarded")==1&&atStop.at("pendingRows")==0&&atStop.at("payloadBytes")==0&&atStop.at("sinkActive")==false&&fixtureDiagEntered==0,"real dropped accepted callback closes same owner and releases pending data without sink execution");}
  else expect(workerWaitAtStop==WAIT_OBJECT_0&&workerExitAtStop==0,"idle actual diagnostic worker exits before normal stop returns");
  const auto entered=fixtureDiagEntered.load();inputHostDeferAppDiagnostic("owned.after-close","reject");auto afterClosed=inputHostCriticalDiagnosticsEvidence();expect(afterClosed.at("rejectedClosed").get<unsigned>()>=1&&fixtureDiagEntered==entered,"post-close actual offers cannot replay or start sink");
  inputHostCriticalDiagnosticsStartup();expect(inputHostCriticalDiagnostics::queue.load()==owner&&owner->snapshot().closed,"actual repeated startup cannot replace or reopen closed process owner");expect(!poolSubmit([]{}),"original terminal pool cannot revive after diagnostic close");
  guard.release=true;expect(WaitForSingleObject(guard.observedThread,2000)==WAIT_OBJECT_0,"exact real worker returns after fixture releases only its own finite sink or callback");
  final=inputHostCriticalDiagnosticsEvidence();expect(final.at("closed")==true&&final.at("pendingRows")==0&&final.at("payloadBytes")==0&&final.at("sinkActive")==false,"after exact worker return no pending or active payload charge remains");
  expect(final.at("sinkReturned")==unsigned(active?1:selected=="schedule-pool-exit-diag-abandoned"?0:4),"sink return count distinguishes active write,pending abandonment and later recovery");
  {std::lock_guard<std::mutex> lock(fixtureDiagWriteMx);writes=fixtureDiagWrites;}
  if(active)expect(writes.size()==1&&writes[0].at("name")=="owned.active"&&writes[0].at("replayTime")=="owned-active-time"&&writes[0].at("encoded")==json({{"time","owned-active-time"},{"value",159}}).dump(),"actual native runtime write retains original producer time and encoded record");
  else if(selected=="schedule-pool-exit-diag-refusal-retry")expect(writes.size()==4&&writes[0].at("kind")=="native"&&writes[1].at("kind")=="mirror"&&writes[2].at("kind")=="domain"&&writes[3].at("kind")=="app"&&!writes[2].at("domainPayload").get<std::string>().empty(),"all actual runtime routing bodies consume their exact retained records");
  else expect(writes.empty(),"abandoned accepted callback does not write diagnostic data");
  guard.finish();observerReturned=guard.observedThreadReturned;observerClosed=guard.duplicateClosed;traceClosed=guard.traceClosed;poolEntries=fixturePoolThreadEntries.load();expect(observerReturned&&observerClosed&&traceClosed&&poolEntries==1,"one actual pool worker and exact duplicate plus own trace handles all return");fixtureDiagRelease=nullptr;
 }catch(const std::exception& e){failure=e.what();}
 json out={{"scenario",selected},{"assertions",checks},{"actualDiagnosticRuntime",true},{"actualDispatcherRecord",true},{"actualPoolTrySubmitExisting",true},{"before",before},{"atProductStop",atStop},{"final",final},{"refused",refused},{"cooldown",cooldown},{"writes",writes},{"activeCharge",activeCharge},{"workerWaitAtProductStop",workerWaitAtStop},{"workerExitAtProductStop",workerExitAtStop},{"observerReturned",observerReturned},{"observerClosed",observerClosed},{"traceClosed",traceClosed},{"ownerPreserved",ownerPreserved},{"poolWorkerEntries",poolEntries},{"stopElapsedUs",stopElapsedUs},{"diagnosticQueueDeleted",false},{"durableFileWriteProven",false},{"realSleep",false},{"hardwareCalls",0}};
 if(!failure.empty())out["failed"]=failure;std::cout<<out.dump()<<char(10);return failure.empty()?0:1;
}
#endif
static int realPoolExitCase(const std::string& selected){
 unsigned checks=0;bool returnedWhileTraceHeld=false,observedAliveAtStop=false,activeRetainedAtStop=false,pendingDisposedAtStop=false;
 unsigned actualWaitAtStop=WAIT_FAILED;long long stopElapsedUs=0;std::string trace;bool observerReturned=false,observerClosed=false,traceClosed=false;unsigned activeDisposed=0,pendingDisposed=0,pendingCalls=0;std::string failure;
 const auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);};
 try{
  if(!(selected=="schedule-pool-exit-trace-format"||selected=="schedule-pool-exit-trace-contention"||selected=="schedule-pool-exit-timeout-retains"||selected=="schedule-pool-exit-partial-actual-join"||selected=="schedule-pool-exit-finite-complete"||selected=="schedule-pool-exit-no-trace"||selected=="schedule-pool-exit-infinite"||selected=="schedule-pool-exit-idle-trace"||selected=="schedule-pool-exit-empty-join"))throw std::runtime_error("unknown exact production exit scenario");
  ExitFixtureGuard guard;g_poolSize=1;fixturePoolThreadEntries=0;
  if(selected=="schedule-pool-exit-trace-format"){
   traceLog("trace-format value=%d word=%s",17,"owned");
#if defined(YMCC_STATIC_WAKE_HAS_TRACE_TRY)
   traceLogTry("trace-format value=%d word=%s",18,"idle");
#else
   traceLog("trace-format value=%d word=%s",18,"idle");
#endif
   expect(g_poolThreads.empty()&&jobs.empty()&&fixturePoolThreadEntries==0,"ordinary and idle try diagnostics do not create pool work");
  }else if(selected=="schedule-pool-exit-empty-join"){
   std::thread empty;expect(joinThreadBoundedForExit(empty,30,"owned-empty"),"empty original thread returns success without trace or wait");expect(!empty.joinable()&&g_poolThreads.empty(),"empty helper cannot start workers");
  }else if(selected=="schedule-pool-exit-partial-actual-join"){
   g_poolSize=3;g_poolThreads.reserve(3);schedulePoolFaultAfter=1;bool failed=false;try{std::lock_guard<std::mutex> lock(g_poolMx);poolStartLocked();}catch(const std::bad_alloc&){failed=true;}schedulePoolFaultAfter=-1;
   expect(failed&&g_poolThreads.size()==1&&!g_poolStarted,"original partial startup keeps one actual worker before publication");guard.observe();poolStop(2000);
   expect(g_poolThreads.empty()&&!g_poolStarted&&g_poolStopping&&g_poolCancel,"actual production join clears partial worker owner");
   expect(WaitForSingleObject(guard.observedThread,0)==WAIT_OBJECT_0,"partial exact native worker has really exited before observer is closed");
  }else if(selected=="schedule-pool-exit-finite-complete"||selected=="schedule-pool-exit-infinite"){
   std::atomic<unsigned> calls{0};expect(poolSubmit([&]{calls.fetch_add(1);}),"complete original pool accepts one owned callback");expect(ExitFixtureGuard::wait([&]{return calls==1;}),"actual callback completed before finite/infinite stop");guard.observe();
   poolStop(selected=="schedule-pool-exit-infinite"?INFINITE:2000);
   expect(g_poolThreads.empty()&&!g_poolStarted&&g_poolStopping&&g_poolCancel,"original complete pool stop clears exact owner");expect(WaitForSingleObject(guard.observedThread,0)==WAIT_OBJECT_0,"complete native worker really signaled after original helper");
  }else{
   auto active=std::make_shared<ExitFixtureToken>();active->disposed=&guard.activeDisposed;std::weak_ptr<ExitFixtureToken> activeWeak=active;
   expect(poolSubmit([active,&guard]{guard.entered=true;while(!guard.release.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));}),"actual original worker accepts finite controlled callback owner");active.reset();
   expect(ExitFixtureGuard::wait([&]{return guard.entered.load();}),"callback is actually running before stop");guard.observe();
   auto pending=std::make_shared<ExitFixtureToken>();pending->disposed=&guard.pendingDisposed;std::weak_ptr<ExitFixtureToken> pendingWeak=pending;
   expect(poolSubmit([pending,&guard]{guard.pendingCalls.fetch_add(1);}),"second owner is queued behind exact active callback");pending.reset();
   const bool held=selected=="schedule-pool-exit-trace-contention"||selected=="schedule-pool-exit-no-trace";
   if(selected=="schedule-pool-exit-no-trace")g_traceOn=false;
   if(held)guard.traceLock.lock();
   const auto started=std::chrono::steady_clock::now();guard.stopper=std::thread([&]{poolStop(30);stopElapsedUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();guard.returned=true;});
   returnedWhileTraceHeld=ExitFixtureGuard::wait([&]{return guard.returned.load();},200);
   if(held)guard.traceLock.unlock();
   expect(ExitFixtureGuard::wait([&]{return guard.returned.load();}),"original stop returns after controlled logger lock released");guard.stopper.join();
   actualWaitAtStop=WaitForSingleObject(guard.observedThread,0);observedAliveAtStop=actualWaitAtStop==WAIT_TIMEOUT;
   activeRetainedAtStop=!activeWeak.expired()&&guard.activeDisposed==0;pendingDisposedAtStop=pendingWeak.expired()&&guard.pendingDisposed==1;
   expect(g_poolThreads.empty()&&!g_poolStarted&&g_poolStopping&&g_poolCancel&&jobs.empty(),"exit timeout seals admission and drops pending queue");
   expect(observedAliveAtStop&&activeRetainedAtStop,"empty vector after detach is not callback or thread resource return");
   expect(pendingDisposedAtStop&&guard.pendingCalls==0,"pending callback owner destroyed without executing or replaying");
   expect(!poolSubmit([]{}),"later submission cannot revive sealed pool or retry detached callback");
   guard.release=true;expect(WaitForSingleObject(guard.observedThread,2000)==WAIT_OBJECT_0,"finite exact detached worker eventually returns after fixture releases only own callback");
   expect(ExitFixtureGuard::wait([&]{return activeWeak.expired();}),"active closure owner is destroyed only after actual callback finishes");
  }
  guard.finish();trace=guard.trace;observerReturned=guard.observedThreadReturned;observerClosed=guard.duplicateClosed;traceClosed=guard.traceClosed;activeDisposed=guard.activeDisposed;pendingDisposed=guard.pendingDisposed;pendingCalls=guard.pendingCalls;
  expect(traceClosed,"owned trace FILE and underlying exact HANDLE returned");
  if(selected!="schedule-pool-exit-empty-join"&&selected!="schedule-pool-exit-trace-format")expect(observerReturned&&observerClosed,"exact duplicated thread observer is signaled then returned");
  if(selected=="schedule-pool-exit-trace-format")expect(trace.find("] trace-format value=17 word=owned\n")!=std::string::npos&&trace.find("] trace-format value=18 word=idle\n")!=std::string::npos&&std::count(trace.begin(),trace.end(),'\n')==2,"shared original formatter preserves two complete FILE receipts and scalar arguments");
  if(selected=="schedule-pool-exit-trace-contention")expect(returnedWhileTraceHeld,"bounded exit must not wait for occupied trace mutex after worker deadline");
  if(selected=="schedule-pool-exit-idle-trace"||selected=="schedule-pool-exit-timeout-retains")expect(trace.find("exit worker detached name=ipc-pool wait=258")!=std::string::npos,"idle real logger retains original wait/name diagnostic");
  if(selected=="schedule-pool-exit-finite-complete"||selected=="schedule-pool-exit-partial-actual-join"||selected=="schedule-pool-exit-infinite"||selected=="schedule-pool-exit-empty-join"||selected=="schedule-pool-exit-no-trace")expect(trace.empty(),"successful or disabled original logger emits no detach diagnostic");
 }catch(const std::exception& e){failure=e.what();}
 json result={{"scenario",selected},{"assertions",checks},{"actualPoolStop",true},{"actualExitJoin",true},{"actualTraceLog",true},{"returnedWhileTraceHeld",returnedWhileTraceHeld},{"workerWaitAtStop",actualWaitAtStop},{"observedAliveAtStop",observedAliveAtStop},{"activeRetainedAtStop",activeRetainedAtStop},{"pendingDisposedAtStop",pendingDisposedAtStop},{"stopElapsedUs",stopElapsedUs},{"activeDisposed",activeDisposed},{"pendingDisposed",pendingDisposed},{"pendingCalls",pendingCalls},{"workerEntries",fixturePoolThreadEntries.load()},{"remainingThreads",g_poolThreads.size()},{"jobs",jobs.size()},{"observedThreadReturned",observerReturned},{"observerClosed",observerClosed},{"traceClosed",traceClosed},{"trace",trace},{"realSleep",false},{"hardwareCalls",0}};
 if(!failure.empty())result["failed"]=failure;std::cout<<result.dump()<<'\n';return failure.empty()?0:1;
}
