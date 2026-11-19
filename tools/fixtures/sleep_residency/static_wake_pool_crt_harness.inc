#if !defined(YMCC_STATIC_POOL_CRT_CASES_ONLY)
#ifndef YMCC_STATIC_POOL_CRT_SHIM_DEFINED
#define YMCC_STATIC_POOL_CRT_SHIM_DEFINED 1
#include <process.h>
#include <errno.h>
static thread_local int fixtureCrtRefuseAfter=-1;
static volatile LONG fixtureCrtCalls=0,fixtureCrtForwarded=0,fixtureCrtRefusals=0;
static uintptr_t __cdecl fixturePoolBeginThread(void* security,unsigned stackSize,unsigned (__stdcall* start)(void*),void* argument,unsigned flags,unsigned* threadId){
 InterlockedIncrement(&fixtureCrtCalls);
 if(fixtureCrtRefuseAfter>=0){
  if(fixtureCrtRefuseAfter==0){fixtureCrtRefuseAfter=-1;InterlockedIncrement(&fixtureCrtRefusals);errno=EAGAIN;_set_doserrno(ERROR_NOT_ENOUGH_MEMORY);return 0;}
  --fixtureCrtRefuseAfter;
 }
 const auto handle=_beginthreadex(security,stackSize,start,argument,flags,threadId);
 if(handle)InterlockedIncrement(&fixtureCrtForwarded);
 return handle;
}
#define _beginthreadex fixturePoolBeginThread
#include <thread>
#undef _beginthreadex
#include <system_error>
#endif
#else
namespace static_wake_schedule_runtime {
struct CrtFixtureToken {std::atomic<unsigned>* disposed;~CrtFixtureToken(){disposed->fetch_add(1);}};

struct AllWorkerObservation {
 HANDLE handle=nullptr;DWORD id=0,ownerPid=0,atStop=WAIT_FAILED,exitAtStop=STILL_ACTIVE,finalWait=WAIT_FAILED;
 std::uint64_t birth=0;bool retainedFromPartial=false,sameSlotAfterFill=false,closed=false;DWORD closeError=0;
};
struct AllWorkerObservers {
 ExitFixtureGuard& guard;std::atomic<bool> tailRelease{false};std::vector<AllWorkerObservation> rows;
 static std::uint64_t born(HANDLE handle){FILETIME creation{},exit{},kernel{},user{};if(!GetThreadTimes(handle,&creation,&exit,&kernel,&user))throw std::runtime_error("owned observer creation time");return(static_cast<std::uint64_t>(creation.dwHighDateTime)<<32)|creation.dwLowDateTime;}
 void capture(size_t first,bool partial){for(size_t i=first;i<g_poolThreads.size();++i){HANDLE duplicate=nullptr;if(!DuplicateHandle(GetCurrentProcess(),g_poolThreads[i].native_handle(),GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS))throw std::runtime_error("all-worker exact DuplicateHandle");rows.push_back({});auto& r=rows.back();r.handle=duplicate;r.id=GetThreadId(duplicate);r.ownerPid=GetProcessIdOfThread(duplicate);r.birth=born(duplicate);r.retainedFromPartial=partial;}}
 void finish() noexcept {
  guard.release=true;tailRelease=true;
  if(guard.stopper.joinable())guard.stopper.join();
  if(!g_poolThreads.empty())poolStop(2000);
  const auto deadline=GetTickCount64()+2000;
  for(auto& r:rows)if(r.handle){const auto now=GetTickCount64();r.finalWait=WaitForSingleObject(r.handle,now>=deadline?0:static_cast<DWORD>(deadline-now));if(CloseHandle(r.handle)){r.closed=true;r.handle=nullptr;}else r.closeError=GetLastError();}
  guard.finish();
 }
 ~AllWorkerObservers(){finish();}
};
static int realPoolAllWorkerHandlesCase(const std::string& selected){
 unsigned checks=0,entered=0,completed=0,activeDisposed=0,pendingDisposed=0,pendingCalls=0,partialCount=0,signaledAtStop=0,heldAtStop=0,observerClosed=0,finalSignaled=0;DWORD heldId=0;
 bool pendingDroppedBeforeRelease=false,traceClosed=false,allKernelBirthsRetained=false;std::vector<json> observations;std::string failure,trace;
#if defined(YMCC_STATIC_POOL_ALL_HANDLES_HOLD_TAIL)
 constexpr bool holdOneTail=true;
#else
 constexpr bool holdOneTail=false;
#endif
 const auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);};
 try{
  reset();InterlockedExchange(&fixtureCrtCalls,0);InterlockedExchange(&fixtureCrtForwarded,0);InterlockedExchange(&fixtureCrtRefusals,0);fixtureCrtRefuseAfter=-1;fixturePoolThreadEntries=0;g_poolSize=16;
  ExitFixtureGuard guard;AllWorkerObservers owners{guard};owners.rows.reserve(16);std::atomic<unsigned> active{0},finished{0};std::atomic<DWORD> tailId{0};
  sgStaticScanPreference(true,true);pump();expect(timerDue==61000,"all-worker case keeps original first absolute minute");fixtureCrtRefuseAfter=7;tick=timerDue;sgStaticScanTimer();fixtureCrtRefuseAfter=-1;
  expect(fixtureCrtRefusals==1&&fixtureCrtForwarded==7&&!g_poolStarted&&g_poolThreads.size()==7&&!g_sgStaticScanRunning.load(std::memory_order_acquire)&&scans==0,"seventh retained worker set survives eighth actual CRT refusal without ready publication");
  owners.capture(0,true);partialCount=static_cast<unsigned>(owners.rows.size());expect(partialCount==7,"every retained partial worker gets an exact observer before retry");
  ++testScheduleGeneration;tick+=1000;sgStaticScanSchedule(false);pump();expect(timerDue==tick+60000,"later valid wake keeps original full minute");tick=timerDue;sgStaticScanTimer();
  expect(ExitFixtureGuard::wait([&]{return !g_sgStaticScanRunning.load(std::memory_order_acquire);}),"full original delayed scan returns before observation cutoff");expect(g_poolStarted&&g_poolThreads.size()==16&&scans==1&&scanWrites==2&&fixtureCrtForwarded==16,"one later scan fills only nine missing workers without replay");
  allKernelBirthsRetained=true;for(size_t i=0;i<owners.rows.size();++i){auto& r=owners.rows[i];r.sameSlotAfterFill=GetThreadId(g_poolThreads[i].native_handle())==r.id&&AllWorkerObservers::born(g_poolThreads[i].native_handle())==r.birth;allKernelBirthsRetained=allKernelBirthsRetained&&r.sameSlotAfterFill;expect(r.sameSlotAfterFill,"each original partial kernel thread remains in its exact public owner slot");}
  owners.capture(7,false);expect(owners.rows.size()==16&&allKernelBirthsRetained,"all sixteen exact observers cover full requested original pool");
  for(size_t i=0;i<owners.rows.size();++i){auto& r=owners.rows[i];expect(r.id!=0&&r.ownerPid==GetCurrentProcessId()&&r.birth!=0,"every observer belongs to exact current owned process and nonzero creation time");expect(WaitForSingleObject(r.handle,0)==WAIT_TIMEOUT,"every original worker is actually alive before stop");for(size_t j=0;j<i;++j)expect(r.id!=owners.rows[j].id||r.birth!=owners.rows[j].birth,"kernel thread identities are distinct");}
  std::vector<std::weak_ptr<CrtFixtureToken>> weak;weak.reserve(16);
  for(unsigned i=0;i<16;++i){auto token=std::make_shared<CrtFixtureToken>();token->disposed=&guard.activeDisposed;weak.push_back(token);expect(poolSubmit([token,&guard,&owners,&active,&finished,&tailId,i]{active.fetch_add(1);while(!guard.release.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));if constexpr(holdOneTail){if(i==15){tailId=GetCurrentThreadId();while(!owners.tailRelease.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));}}finished.fetch_add(1);}),"full actual pool accepts one finite owned active callback per worker");}
  expect(ExitFixtureGuard::wait([&]{return active.load()==16;}),"all sixteen callbacks actually enter before pending owner is queued");
  auto pending=std::make_shared<CrtFixtureToken>();pending->disposed=&guard.pendingDisposed;std::weak_ptr<CrtFixtureToken> pendingWeak=pending;expect(poolSubmit([pending,&guard]{guard.pendingCalls.fetch_add(1);}),"seventeenth callback remains pending behind sixteen active owners");pending.reset();
  guard.stopper=std::thread([&]{poolStop(2000);guard.returned=true;});const bool stopping=ExitFixtureGuard::wait([&]{std::lock_guard<std::mutex> lock(g_poolMx);return g_poolStopping&&g_poolQ.empty();});pendingDroppedBeforeRelease=pendingWeak.expired()&&guard.pendingDisposed==1&&guard.pendingCalls==0;guard.release=true;guard.stopper.join();
  expect(stopping&&guard.returned&&pendingDroppedBeforeRelease,"original stop destroys pending owner before releasing active callbacks");expect(g_poolThreads.empty()&&!g_poolStarted&&g_poolStopping&&g_poolCancel&&!poolSubmit([]{}),"public stopped pool remains sealed with no callback replay");
  heldId=tailId.load();for(auto& r:owners.rows){r.atStop=WaitForSingleObject(r.handle,0);if(!GetExitCodeThread(r.handle,&r.exitAtStop))throw std::runtime_error("exact observer exit-code query");if(r.atStop==WAIT_OBJECT_0)++signaledAtStop;else if(r.atStop==WAIT_TIMEOUT)++heldAtStop;expect(AllWorkerObservers::born(r.handle)==r.birth,"same exact kernel object remains queryable after product primary handle release");}
  owners.tailRelease=true;owners.finish();entered=active.load();completed=finished.load();activeDisposed=guard.activeDisposed;pendingDisposed=guard.pendingDisposed;pendingCalls=guard.pendingCalls;traceClosed=guard.traceClosed;trace=guard.trace;
  for(const auto& r:owners.rows){if(r.closed)++observerClosed;if(r.finalWait==WAIT_OBJECT_0)++finalSignaled;observations.push_back({{"id",r.id},{"ownerPid",r.ownerPid},{"birth",std::to_string(r.birth)},{"retainedFromPartial",r.retainedFromPartial},{"sameSlotAfterFill",r.sameSlotAfterFill},{"atProductStopWait",r.atStop},{"exitCodeAtProductStop",r.exitAtStop},{"finalWaitAfterOwnedRelease",r.finalWait},{"observerClosed",r.closed},{"closeError",r.closeError}});}
  expect(observerClosed==16&&finalSignaled==16&&traceClosed,"all exact duplicate observers and trace FILE are returned after bounded own release");expect(entered==16&&completed==16&&activeDisposed==16&&pendingDisposed==1&&pendingCalls==0,"all active callback owners return and pending callback never executes");expect(std::all_of(weak.begin(),weak.end(),[](const auto& r){return r.expired();}),"all sixteen exact active owner leases expire after actual worker completion");
  expect(signaledAtStop==16&&heldAtStop==0,"all exact worker observers must be signaled when production stop returns");expect(std::all_of(observations.begin(),observations.end(),[](const json& row){return row["exitCodeAtProductStop"]==0;}),"every actual worker returns original zero exit before Native process exit");
 }catch(const std::exception& e){fixtureCrtRefuseAfter=-1;failure=e.what();}
 json out={{"scenario",selected},{"assertions",checks},{"requestedWorkers",16},{"partialObservers",partialCount},{"observerCount",observations.size()},{"signaledAtProductStop",signaledAtStop},{"heldAtProductStop",heldAtStop},{"heldWorkerId",heldId},{"heldTailControl",holdOneTail},{"finalSignaledAfterOwnedRelease",finalSignaled},{"observerClosed",observerClosed},{"retainedKernelBirths",allKernelBirthsRetained},{"callbacksEntered",entered},{"callbacksFinished",completed},{"activeOwnersDisposed",activeDisposed},{"pendingOwnerDisposed",pendingDisposed},{"pendingCalls",pendingCalls},{"pendingDroppedBeforeActiveRelease",pendingDroppedBeforeRelease},{"traceClosed",traceClosed},{"trace",trace},{"observations",observations},{"crtCalls",static_cast<LONG>(fixtureCrtCalls)},{"crtForwarded",static_cast<LONG>(fixtureCrtForwarded)},{"crtRefusals",static_cast<LONG>(fixtureCrtRefusals)},{"workerEntries",fixturePoolThreadEntries.load()},{"scans",scans},{"scanWrites",scanWrites},{"jobs",jobs.size()},{"remainingThreads",g_poolThreads.size()},{"realSleep",false},{"hardwareCalls",0},{"naturalOSExhaustion",false}};
 if(!failure.empty())out["failed"]=failure;std::cout<<out.dump()<<'\n';return failure.empty()?0:1;
}

static int realPoolCrtCase(const std::string& selected){
 if(selected=="schedule-pool-exit-crt-all-worker-handles")return realPoolAllWorkerHandlesCase(selected);
 unsigned checks=0,errorCode=0,failedTokenDisposed=0,auxiliaryJoins=0;bool systemErrorSeen=false,badAllocSeen=false,stopSealedBeforeLate=false,lateAccepted=false;
 unsigned forwardsBeforeLate=0,forwardsAfterLate=0,lateActions=0;unsigned observerCount=0;bool observerReturned=false,observerClosed=false,traceClosed=false;std::string errorCategory,trace,failure;
 const auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);};
 try{
  if(!(selected=="schedule-pool-exit-crt-first-direct"||selected=="schedule-pool-exit-crt-empty-refusal-stop"||selected=="schedule-pool-exit-crt-never-started-stop"||selected=="schedule-pool-exit-crt-partial-wake"||selected=="schedule-pool-exit-crt-partial-stop"||selected=="schedule-pool-exit-crt-repeat-wake"||selected=="schedule-pool-exit-crt-off-reenable"||selected=="schedule-pool-exit-crt-tls-isolation"))throw std::runtime_error("unknown exact CRT pool scenario");
  reset();InterlockedExchange(&fixtureCrtCalls,0);InterlockedExchange(&fixtureCrtForwarded,0);InterlockedExchange(&fixtureCrtRefusals,0);fixtureCrtRefuseAfter=-1;fixturePoolThreadEntries=0;g_poolSize=3;
  ExitFixtureGuard guard;
  const auto createRefusal=[&]{
   auto& disposed=guard.activeDisposed;disposed=0;auto token=std::make_shared<CrtFixtureToken>();token->disposed=&disposed;std::weak_ptr<CrtFixtureToken> weak=token;std::function<void()> job=[token]{};token.reset();
   fixtureCrtRefuseAfter=0;
   try{(void)poolSubmit(std::move(job));}catch(const std::system_error& e){systemErrorSeen=true;errorCode=static_cast<unsigned>(e.code().value());errorCategory=e.code().category().name();expect(e.code()==std::make_error_code(std::errc::resource_unavailable_try_again),"actual MSVC null CRT handle becomes resource_unavailable_try_again system_error");}catch(const std::bad_alloc&){badAllocSeen=true;}
   fixtureCrtRefuseAfter=-1;failedTokenDisposed=disposed.load();
   expect(systemErrorSeen&&!badAllocSeen&&fixtureCrtRefusals==1,"refusal crosses actual std::thread exception path rather than bad_alloc");
   expect(weak.expired()&&failedTokenDisposed==1,"failed original submission releases its exact closure owner");
   expect(!g_poolStarted&&g_poolThreads.empty()&&jobs.empty(),"CRT creation refusal cannot publish an empty ready pool");
   traceLog("owned CRT refusal code=%u",errorCode);
  };
  const auto completeDirect=[&]{std::atomic<unsigned> completed{0};expect(poolSubmit([&]{completed.fetch_add(1);}),"later original request can complete full cold startup after CRT refusal");expect(ExitFixtureGuard::wait([&]{return completed.load()==1;}),"later real callback runs exactly once");expect(g_poolStarted&&g_poolThreads.size()==3,"ready publishes only all three requested original workers");guard.observe();observerCount=1;poolStop(2000);};
  const auto prime=[&]{sgStaticScanPreference(true,true);pump();expect(timerDue==61000&&scans==0,"original startup keeps one absolute minute before work");};
  const auto failScheduled=[&](int after){fixtureCrtRefuseAfter=after;tick=timerDue;sgStaticScanTimer();fixtureCrtRefuseAfter=-1;expect(!g_sgStaticScanRunning.load(std::memory_order_acquire)&&jobs.empty()&&scans==0,"real CRT throw is contained by original scheduler and releases running state");expect(!g_poolStarted&&g_poolThreads.size()==static_cast<size_t>(after),"failed creation retains only exact earlier successful workers without ready publication");};
  const auto completeScheduled=[&]{tick=timerDue;sgStaticScanTimer();expect(ExitFixtureGuard::wait([&]{return !g_sgStaticScanRunning.load(std::memory_order_acquire);}),"later actual sleep worker completes before finite observation cutoff");expect(scans==1&&scanWrites==2&&jobs.empty()&&g_poolStarted&&g_poolThreads.size()==3,"later valid event scans once with full original pool and no replay");guard.observe();observerCount=1;poolStop(2000);};
  if(selected=="schedule-pool-exit-crt-first-direct") {createRefusal();completeDirect();}
  else if(selected=="schedule-pool-exit-crt-empty-refusal-stop"||selected=="schedule-pool-exit-crt-never-started-stop"){
   if(selected=="schedule-pool-exit-crt-empty-refusal-stop")createRefusal();
   poolStop(2000);stopSealedBeforeLate=g_poolStopping&&g_poolCancel&&!g_poolStarted&&g_poolThreads.empty();
   forwardsBeforeLate=static_cast<unsigned>(fixtureCrtForwarded);std::atomic<unsigned> called{0};lateAccepted=poolSubmit([&]{called.fetch_add(1);});
   if(lateAccepted)expect(ExitFixtureGuard::wait([&]{return called.load()==1;}),"unexpected late accepted callback remains finite for owned cleanup");
   forwardsAfterLate=static_cast<unsigned>(fixtureCrtForwarded);lateActions=called.load();
   if(!g_poolThreads.empty()){guard.observe();observerCount=1;}
  }else if(selected=="schedule-pool-exit-crt-partial-wake"){
   prime();failScheduled(1);expect(fixtureCrtRefusals==1&&fixtureCrtForwarded==1,"one real worker remains before second CRT refusal");
   ++testScheduleGeneration;tick+=1000;sgStaticScanSchedule(false);pump();expect(timerDue==tick+60000,"later confirmed wake retains original fresh minute");completeScheduled();expect(fixtureCrtForwarded==3,"later original request fills only two missing workers");
  }else if(selected=="schedule-pool-exit-crt-partial-stop"){
   prime();failScheduled(1);guard.observe();observerCount=1;poolStop(2000);expect(g_poolStopping&&g_poolThreads.empty(),"partial CRT startup is joined by actual public stop");const auto calls=fixtureCrtCalls;expect(!poolSubmit([]{})&&fixtureCrtCalls==calls,"sealed partial pool cannot restart after shutdown");
  }else if(selected=="schedule-pool-exit-crt-repeat-wake"){
   prime();for(unsigned n=0;n<3;++n){if(n){++testScheduleGeneration;tick+=1000;sgStaticScanSchedule(false);pump();}failScheduled(0);expect(fixtureCrtRefusals==static_cast<LONG>(n+1),"each distinct original event reaches exactly one controlled CRT refusal");}
   expect(fixtureCrtForwarded==0&&fixturePoolThreadEntries==0,"repeated refusals create no hidden worker or queued callback");++testScheduleGeneration;tick+=1000;sgStaticScanSchedule(false);pump();expect(timerDue==tick+60000,"event after repeated failure has original absolute deadline without refund");completeScheduled();
  }else if(selected=="schedule-pool-exit-crt-off-reenable"){
   prime();failScheduled(0);sgStaticScanPreference(false);pump();expect(!g_sgStaticScanDelay.pending&&jobs.empty(),"OFF after CRT refusal retains no demand or replayed work");sgStaticScanPreference(true);pump();expect(timerDue==tick+60000&&scans==0,"explicit ON after refusal retains original full minute");completeScheduled();
  }else if(selected=="schedule-pool-exit-crt-tls-isolation"){
   struct Other {std::atomic<bool> go{false},done{false};std::atomic<unsigned> joins{0};std::thread owner;~Other(){go=true;if(owner.joinable())owner.join();}} other;
   other.owner=std::thread([&]{while(!other.go.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));std::thread leaf([]{});leaf.join();other.joins.fetch_add(1);other.done=true;});
   fixtureCrtRefuseAfter=0;other.go=true;expect(ExitFixtureGuard::wait([&]{return other.done.load();}),"other thread forwards its own real CRT creation while main refusal is armed");other.owner.join();other.joins.fetch_add(1);auxiliaryJoins=other.joins.load();expect(fixtureCrtRefusals==0&&fixtureCrtForwarded==2&&auxiliaryJoins==2,"controlled refusal is thread local and both finite helper threads join");createRefusal();completeDirect();
  }
  fixtureCrtRefuseAfter=-1;guard.finish();observerReturned=guard.observedThreadReturned;observerClosed=guard.duplicateClosed;traceClosed=guard.traceClosed;trace=guard.trace;
  expect(traceClosed&&g_poolThreads.empty()&&jobs.empty()&&!g_poolStarted,"actual final stop and owned trace return before result");
  if(observerCount)expect(observerReturned&&observerClosed,"exact owned worker observer signals before its duplicate is closed");
  if(selected=="schedule-pool-exit-crt-empty-refusal-stop"||selected=="schedule-pool-exit-crt-never-started-stop")expect(stopSealedBeforeLate&&!lateAccepted&&forwardsBeforeLate==forwardsAfterLate&&lateActions==0,"empty public stop must seal admission before return and forbid later worker creation");
  else expect(g_poolStopping&&g_poolCancel&&!poolSubmit([]{}),"final public stop preserves cancellation and no-revival fence");
 }catch(const std::exception& e){fixtureCrtRefuseAfter=-1;failure=e.what();}
 json out={{"scenario",selected},{"assertions",checks},{"controlledCrtRefusal",true},{"naturalOSExhaustion",false},{"systemErrorSeen",systemErrorSeen},{"errorCode",errorCode},{"errorCategory",errorCategory},{"badAllocSeen",badAllocSeen},{"crtCalls",static_cast<LONG>(fixtureCrtCalls)},{"crtForwarded",static_cast<LONG>(fixtureCrtForwarded)},{"crtRefusals",static_cast<LONG>(fixtureCrtRefusals)},{"workerEntries",fixturePoolThreadEntries.load()},{"failedClosureDisposed",failedTokenDisposed},{"auxiliaryJoins",auxiliaryJoins},{"stopSealedBeforeLate",stopSealedBeforeLate},{"lateAccepted",lateAccepted},{"forwardsBeforeLate",forwardsBeforeLate},{"forwardsAfterLate",forwardsAfterLate},{"lateActions",lateActions},{"observerCount",observerCount},{"observedThreadReturned",observerReturned},{"observerClosed",observerClosed},{"traceClosed",traceClosed},{"trace",trace},{"scans",scans},{"scanWrites",scanWrites},{"running",g_sgStaticScanRunning.load()},{"jobs",jobs.size()},{"remainingThreads",g_poolThreads.size()},{"realSleep",false},{"hardwareCalls",0}};
 if(!failure.empty())out["failed"]=failure;std::cout<<out.dump()<<'\n';return failure.empty()?0:1;
}
}
#endif
