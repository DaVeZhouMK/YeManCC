#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <mmsystem.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>

// Private WinMM timers only. No YMCC function, audited DLL, device or service runs.
namespace {
constexpr DWORD gateTimeout=4000,parkTimeout=2000,legacyObserveTimeout=1000,syncObserveWindow=150,joinTimeout=5000;
constexpr UINT periodMs=8;
struct Context {
 SRWLOCK lifetimeGate=SRWLOCK_INIT;
 std::mutex bus;
 std::atomic<bool> closed{false},stop{false},retired{false},released{false};
 bool faultBeforeBus=false,synchronous=false;
 UINT timerId=0;
 HANDLE entered=nullptr,parked=nullptr,release=nullptr,completed=nullptr,cancelStarted=nullptr,cancelReturned=nullptr,ownerRetired=nullptr,userEvent=nullptr;
 std::atomic<unsigned> order{0},callbacks{0},active{0},extraCallbacks{0},busAdmissions{0},lateAdmissions{0},busySkips{0},tailSignals{0},diagnostics{0},eventErrors{0};
 std::atomic<unsigned> enterOrder{0},busOrder{0},parkOrder{0},cancelStartOrder{0},cancelReturnOrder{0},drainOrder{0},retireOrder{0},releaseOrder{0},tailOrder{0},completeOrder{0};
 std::atomic<DWORD> callbackThread{0},cancelThread{0},gateWait{WAIT_FAILED};
 std::atomic<UINT> killResult{~UINT{0}};
 std::atomic<LONGLONG> cancelStartQpc{0},cancelEndQpc{0},releaseQpc{0};
 unsigned createdEvents=0,closedEvents=0;
};
// Context and lifetime gate remain valid until process exit, even for a late
// dispatch that only checks closed. No callback uses an event after gate closure.
std::array<Context,8> contexts;
unsigned checks=0,failures=0;
const char* currentCase="setup";
LONGLONG frequency=0;
unsigned mark(Context& c,std::atomic<unsigned>& slot){const auto n=c.order.fetch_add(1)+1;slot.store(n);return n;}
LONGLONG qpc(){LARGE_INTEGER v{};QueryPerformanceCounter(&v);return v.QuadPart;}
void check(bool good,const char* text){++checks;if(!good){++failures;std::cout<<"{\"type\":\"assertion\",\"scenario\":\""<<currentCase<<"\",\"expression\":\""<<text<<"\",\"passed\":false}\n";}}
#define CHECK(x) check(static_cast<bool>(x),#x)
void signal(Context& c,HANDLE event){if(!SetEvent(event))c.eventErrors.fetch_add(1);}
void releaseGate(Context& c){if(!c.released.exchange(true)){mark(c,c.releaseOrder);c.releaseQpc=qpc();signal(c,c.release);}}
struct ReleaseGuard {Context& c;~ReleaseGuard(){releaseGate(c);}};
[[noreturn]] void unsafeExit(Context& c,const char* reason){releaseGate(c);std::cout<<"{\"type\":\"fatal\",\"reason\":\""<<reason<<"\",\"success\":false,\"cleanup\":\"process-exit-without-closing-live-callback-handles\"}\n";ExitProcess(3);}
void CALLBACK timerCallback(UINT,UINT,DWORD_PTR user,DWORD_PTR,DWORD_PTR){
 auto& c=*reinterpret_cast<Context*>(user);
 AcquireSRWLockShared(&c.lifetimeGate);
 if(c.closed.load()){ReleaseSRWLockShared(&c.lifetimeGate);return;}
 c.active.fetch_add(1);
 const auto number=c.callbacks.fetch_add(1)+1;
 if(number!=1){c.extraCallbacks.fetch_add(1);c.active.fetch_sub(1);ReleaseSRWLockShared(&c.lifetimeGate);return;}
 c.callbackThread=GetCurrentThreadId();mark(c,c.enterOrder);signal(c,c.entered);
 if(c.faultBeforeBus){mark(c,c.parkOrder);signal(c,c.parked);c.gateWait=WaitForSingleObject(c.release,gateTimeout);}
 {
  // Keep the actual production unique_lock scope through SetEvent/diagnostics.
  // Tail scenario makes try_lock fail, so its busy-skip tail owns no BUS lock.
  std::unique_lock<std::mutex> tickLock(c.bus,std::try_to_lock);
  if(!tickLock.owns_lock())c.busySkips.fetch_add(1);
  else if(!c.stop.load()){
   c.busAdmissions.fetch_add(1);
   if(c.retired.load())c.lateAdmissions.fetch_add(1); // Counter only, never a DLL call.
  }
  mark(c,c.busOrder);
  if(!c.faultBeforeBus){mark(c,c.parkOrder);signal(c,c.parked);c.gateWait=WaitForSingleObject(c.release,gateTimeout);}
  mark(c,c.tailOrder);signal(c,c.userEvent);c.tailSignals.fetch_add(1);c.diagnostics.fetch_add(1);
 }
 mark(c,c.completeOrder);signal(c,c.completed);
 c.active.fetch_sub(1);
 ReleaseSRWLockShared(&c.lifetimeGate);
}
DWORD WINAPI cancelWorker(void* data){
 auto& c=*static_cast<Context*>(data);c.cancelThread=GetCurrentThreadId();mark(c,c.cancelStartOrder);c.cancelStartQpc=qpc();signal(c,c.cancelStarted);
 c.killResult=timeKillEvent(c.timerId);c.cancelEndQpc=qpc();mark(c,c.cancelReturnOrder);signal(c,c.cancelReturned);
 {std::lock_guard<std::mutex> lock(c.bus);mark(c,c.drainOrder);}
 c.retired=true;mark(c,c.retireOrder);signal(c,c.ownerRetired);return 0;
}
std::array<HANDLE*,8> events(Context& c){return {&c.entered,&c.parked,&c.release,&c.completed,&c.cancelStarted,&c.cancelReturned,&c.ownerRetired,&c.userEvent};}
bool makeEvents(Context& c){for(auto* event:events(c)){*event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!*event)return false;++c.createdEvents;}return true;}
void closeEvents(Context& c){
 AcquireSRWLockExclusive(&c.lifetimeGate);c.closed=true;
 CHECK(c.active.load()==0);
 for(auto* event:events(c))if(*event){CHECK(CloseHandle(*event)!=FALSE);*event=nullptr;++c.closedEvents;}
 ReleaseSRWLockExclusive(&c.lifetimeGate);
}
void row(Context& c,unsigned repeat,unsigned beforeChecks,unsigned beforeFailures,DWORD observation,bool earlyReturn,bool earlyComplete,bool earlyUser,unsigned activeAtObservation,bool atRisk){
 std::cout<<"{\"type\":\"observation\",\"scenario\":\""<<currentCase<<"\",\"repeat\":"<<repeat
 <<",\"faultBeforeBus\":"<<c.faultBeforeBus<<",\"synchronous\":"<<c.synchronous
 <<",\"periodMs\":"<<periodMs<<",\"flags\":"<<(TIME_PERIODIC|(c.synchronous?TIME_KILL_SYNCHRONOUS:0))
 <<",\"observationWait\":"<<observation<<",\"earlyCancelReturnObserved\":"<<earlyReturn
 <<",\"earlyCallbackComplete\":"<<earlyComplete<<",\"earlyUserEvent\":"<<earlyUser<<",\"activeAtObservation\":"<<activeAtObservation
 <<",\"unsafeCloseWouldOverlapCallback\":"<<atRisk<<",\"stopStayedFalse\":"<<!c.stop.load()
 <<",\"callbacksEntered\":"<<c.callbacks<<",\"extraCallbacks\":"<<c.extraCallbacks<<",\"activeAfterDrain\":"<<c.active
 <<",\"busAdmissions\":"<<c.busAdmissions<<",\"lateBusAdmissions\":"<<c.lateAdmissions<<",\"busySkips\":"<<c.busySkips
 <<",\"tailSignals\":"<<c.tailSignals<<",\"tailDiagnostics\":"<<c.diagnostics<<",\"eventErrors\":"<<c.eventErrors
 <<",\"timeKillResult\":"<<c.killResult<<",\"callbackGateWait\":"<<c.gateWait<<",\"callbackThread\":"<<c.callbackThread<<",\"cancelThread\":"<<c.cancelThread
 <<",\"enterOrder\":"<<c.enterOrder<<",\"busOrder\":"<<c.busOrder<<",\"parkOrder\":"<<c.parkOrder
 <<",\"cancelStartOrder\":"<<c.cancelStartOrder<<",\"cancelReturnOrder\":"<<c.cancelReturnOrder<<",\"busDrainOrder\":"<<c.drainOrder
 <<",\"ownerRetireOrder\":"<<c.retireOrder<<",\"releaseOrder\":"<<c.releaseOrder<<",\"tailAccessOrder\":"<<c.tailOrder<<",\"callbackCompleteOrder\":"<<c.completeOrder
 <<",\"cancelStartQpc\":"<<c.cancelStartQpc<<",\"cancelEndQpc\":"<<c.cancelEndQpc<<",\"releaseQpc\":"<<c.releaseQpc<<",\"qpcFrequency\":"<<frequency
 <<",\"createdEvents\":"<<c.createdEvents<<",\"closedEvents\":"<<c.closedEvents<<",\"cancelThreadJoined\":true,\"contextGateClosed\":"<<c.closed
 <<",\"checks\":"<<(checks-beforeChecks)<<",\"failures\":"<<(failures-beforeFailures)<<"}\n";
}
void runCase(Context& c,bool fault,bool synchronous,unsigned repeat){
 currentCase=fault?(synchronous?"fault-before-bus-sync":"fault-before-bus-legacy"):(synchronous?"busy-skip-tail-sync":"busy-skip-tail-legacy");
 const auto beforeChecks=checks,beforeFailures=failures;c.faultBeforeBus=fault;c.synchronous=synchronous;
 CHECK(makeEvents(c));if(c.createdEvents!=8){closeEvents(c);return;}
 ReleaseGuard release{c};
 std::unique_lock<std::mutex> busyHolder(c.bus,std::defer_lock);if(!fault)busyHolder.lock();
 c.timerId=timeSetEvent(periodMs,1,&timerCallback,reinterpret_cast<DWORD_PTR>(&c),TIME_PERIODIC|(synchronous?TIME_KILL_SYNCHRONOUS:0));
 CHECK(c.timerId!=0);if(!c.timerId){if(busyHolder.owns_lock())busyHolder.unlock();releaseGate(c);closeEvents(c);return;}
 const auto parked=WaitForSingleObject(c.parked,parkTimeout);CHECK(parked==WAIT_OBJECT_0);
 CHECK(WaitForSingleObject(c.entered,0)==WAIT_OBJECT_0);if(busyHolder.owns_lock())busyHolder.unlock();
 HANDLE canceller=CreateThread(nullptr,0,&cancelWorker,&c,0,nullptr);CHECK(canceller!=nullptr);if(!canceller)unsafeExit(c,"cancel-thread-creation-failed");
 CHECK(WaitForSingleObject(c.cancelStarted,1000)==WAIT_OBJECT_0);
 const DWORD observation=WaitForSingleObject(synchronous?c.cancelReturned:c.ownerRetired,synchronous?syncObserveWindow:legacyObserveTimeout);
 const bool earlyReturn=WaitForSingleObject(c.cancelReturned,0)==WAIT_OBJECT_0;
 const bool earlyComplete=WaitForSingleObject(c.completed,0)==WAIT_OBJECT_0;
 const bool earlyUser=WaitForSingleObject(c.userEvent,0)==WAIT_OBJECT_0;
 const auto activeAtObservation=c.active.load();
 if(synchronous){CHECK(observation==WAIT_TIMEOUT);CHECK(!earlyReturn);CHECK(!c.retired.load());}
 else {CHECK(observation==WAIT_OBJECT_0);CHECK(earlyReturn);CHECK(c.killResult.load()==TIMERR_NOERROR);CHECK(c.retired.load());CHECK(c.drainOrder.load()>c.cancelReturnOrder.load());}
 CHECK(!earlyComplete&&!earlyUser);CHECK(activeAtObservation==1);CHECK(!c.stop.load());
 const bool atRisk=!synchronous&&earlyReturn&&!earlyComplete;
 // Unconditional controller release, including all failed-observation paths.
 releaseGate(c);
 CHECK(WaitForSingleObject(c.completed,joinTimeout)==WAIT_OBJECT_0);
 if(WaitForSingleObject(canceller,joinTimeout)!=WAIT_OBJECT_0)unsafeExit(c,"cancel-did-not-return-after-gate-release");
 DWORD exitCode=~DWORD{0};CHECK(GetExitCodeThread(canceller,&exitCode)!=FALSE);CHECK(exitCode==0);CHECK(CloseHandle(canceller)!=FALSE);
 // Excludes callback accesses while closing, including any late dispatch. Context
 // is static; a late callback sees closed before reading any released resource.
 AcquireSRWLockExclusive(&c.lifetimeGate);CHECK(c.active.load()==0);ReleaseSRWLockExclusive(&c.lifetimeGate);
 CHECK(c.killResult.load()==TIMERR_NOERROR);CHECK(c.gateWait.load()==WAIT_OBJECT_0);CHECK(c.callbacks.load()==1&&c.extraCallbacks.load()==0);
 CHECK(c.eventErrors.load()==0&&c.tailSignals.load()==1&&c.diagnostics.load()==1);CHECK(c.callbackThread.load()!=c.cancelThread.load()&&c.callbackThread.load()!=GetCurrentThreadId());
 CHECK(c.parkOrder.load()<c.releaseOrder.load()&&c.releaseOrder.load()<c.tailOrder.load()&&c.tailOrder.load()<c.completeOrder.load());
 if(synchronous){CHECK(c.completeOrder.load()<c.cancelReturnOrder.load());CHECK(c.cancelReturnOrder.load()<c.retireOrder.load());CHECK(c.releaseQpc.load()<=c.cancelEndQpc.load());}
 else {CHECK(c.cancelReturnOrder.load()<c.drainOrder.load()&&c.drainOrder.load()<c.retireOrder.load());CHECK(c.retireOrder.load()<c.releaseOrder.load());CHECK(c.cancelEndQpc.load()<c.releaseQpc.load());CHECK(atRisk);}
 if(fault){CHECK(c.busAdmissions.load()==1&&c.busySkips.load()==0);CHECK(c.lateAdmissions.load()==(synchronous?0u:1u));if(!synchronous)CHECK(c.retireOrder.load()<c.busOrder.load());}
 else CHECK(c.busAdmissions.load()==0&&c.busySkips.load()==1&&c.lateAdmissions.load()==0);
 closeEvents(c);CHECK(c.createdEvents==c.closedEvents);
 row(c,repeat,beforeChecks,beforeFailures,observation,earlyReturn,earlyComplete,earlyUser,activeAtObservation,atRisk);
}
}
int main(){std::cout<<std::boolalpha;std::cout.setf(std::ios::unitbuf);
 wchar_t modulePath[32768]{};const auto module=GetModuleHandleW(L"winmm.dll");
 const auto pathLength=module?GetModuleFileNameW(module,modulePath,32768):0;CHECK(pathLength>0&&pathLength<32768);
 const auto utf8Length=WideCharToMultiByte(CP_UTF8,0,modulePath,static_cast<int>(pathLength),nullptr,0,nullptr,nullptr);
 std::string utf8(static_cast<std::size_t>(utf8Length),'\0');if(utf8Length)WideCharToMultiByte(CP_UTF8,0,modulePath,static_cast<int>(pathLength),utf8.data(),utf8Length,nullptr,nullptr);
 std::cout<<"{\"type\":\"host\",\"pid\":"<<GetCurrentProcessId()<<",\"winmmPath\":\"";
 for(const char c:utf8){if(c=='\\'||c=='"')std::cout<<'\\';std::cout<<c;}std::cout<<"\"}\n";
LARGE_INTEGER f{};CHECK(QueryPerformanceFrequency(&f)!=FALSE);frequency=f.QuadPart;CHECK(frequency>0);
 unsigned i=0;for(unsigned repeat=1;repeat<=2;++repeat)for(const bool fault:{false,true})for(const bool sync:{false,true})runCase(contexts[i++],fault,sync,repeat);
 std::cout<<"{\"type\":\"summary\",\"observations\":"<<i<<",\"classes\":2,\"repeats\":2,\"checks\":"<<checks<<",\"failures\":"<<failures<<"}\n";return failures?1:0;}
