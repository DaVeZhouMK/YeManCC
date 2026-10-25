// Historical in-process callback regression; not the owned binding production acceptance.
#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1
// Production Runtime + Win32 ownership probes. Only this source-built image is launched.
#include <windows.h>
#include <userenv.h>
#include <filesystem>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <json.hpp>
using nlohmann::json;
namespace probe {
std::mutex mutex;
std::map<HANDLE,std::string> handles;
std::atomic<unsigned> launches{0},jobStops{0},jobCloseFallbacks{0};
std::atomic<bool> denyJobStop{false};
void add(HANDLE h,const char* kind){if(h&&h!=INVALID_HANDLE_VALUE){std::lock_guard l(mutex);handles[h]=kind;}}
size_t remaining(){std::lock_guard l(mutex);return handles.size();}
HANDLE WINAPI event(LPSECURITY_ATTRIBUTES a,BOOL manual,BOOL initial,LPCWSTR name){auto h=::CreateEventW(a,manual,initial,name);add(h,"event");return h;}
HANDLE WINAPI job(LPSECURITY_ATTRIBUTES a,LPCWSTR name){auto h=::CreateJobObjectW(a,name);add(h,"job");return h;}
BOOL WINAPI close(HANDLE h){bool fallback=false;{std::lock_guard l(mutex);auto i=handles.find(h);fallback=i!=handles.end()&&i->second=="job"&&denyJobStop;}
 const auto ok=::CloseHandle(h);if(ok){std::lock_guard l(mutex);handles.erase(h);if(fallback)++jobCloseFallbacks;}return ok;}
BOOL WINAPI duplicate(HANDLE sourceProcess,HANDLE source,HANDLE targetProcess,LPHANDLE target,DWORD access,BOOL inherit,DWORD options){
 std::string kind="duplicated-handle";{std::lock_guard lock(mutex);auto i=handles.find(source);if(i!=handles.end())kind=i->second+"-lease";}
 const auto ok=::DuplicateHandle(sourceProcess,source,targetProcess,target,access,inherit,options);
 if(ok&&target&&::GetProcessId(targetProcess)==::GetCurrentProcessId())add(*target,kind.c_str());
 if(ok&&(options&DUPLICATE_CLOSE_SOURCE)){std::lock_guard lock(mutex);handles.erase(source);}return ok;
}
BOOL WINAPI stopJob(HANDLE h,UINT code){++jobStops;if(denyJobStop){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::TerminateJobObject(h,code);}
BOOL WINAPI create(LPCWSTR a,LPWSTR b,LPSECURITY_ATTRIBUTES c,LPSECURITY_ATTRIBUTES d,BOOL e,DWORD flags,LPVOID env,LPCWSTR dir,LPSTARTUPINFOW si,LPPROCESS_INFORMATION pi){auto ok=::CreateProcessW(a,b,c,d,e,flags,env,dir,si,pi);if(ok){add(pi->hProcess,"process");add(pi->hThread,"primary-thread");++launches;}return ok;}
BOOL WINAPI createToken(HANDLE token,DWORD logon,LPCWSTR a,LPWSTR b,DWORD flags,LPVOID env,LPCWSTR dir,LPSTARTUPINFOW si,LPPROCESS_INFORMATION pi){auto ok=::CreateProcessWithTokenW(token,logon,a,b,flags,env,dir,si,pi);if(ok){add(pi->hProcess,"process");add(pi->hThread,"primary-thread");++launches;}return ok;}
}
#define CreateEventW probe::event
#define CreateJobObjectW probe::job
#define CloseHandle probe::close
#define TerminateJobObject probe::stopJob
#define DuplicateHandle probe::duplicate
#define CreateProcessW probe::create
#define CreateProcessWithTokenW probe::createToken
#include "decky_sidebar_runtime.h"
#undef CreateEventW
#undef CreateJobObjectW
#undef CloseHandle
#undef TerminateJobObject
#undef DuplicateHandle
#undef CreateProcessW
#undef CreateProcessWithTokenW
#include "decky_sidebar_health.h"
using namespace ymcc::deckysidebar;
static Runtime* activeRuntime=nullptr;
#define g_deckySidebarRuntime (*activeRuntime)
enum class PowerLifecycle {Ready,Suspending,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static unsigned long long powerGeneration=1;
static unsigned long long currentPowerGeneration(){return powerGeneration;}
static void stopPowerResumeWatchdog(){}
static json diagnostics=json::array();
static void deckySidebarDiagnostic(const char* event,const json& detail,bool){diagnostics.push_back({{"event",event},{"detail",detail}});}
#include "committed-ready-consumer.inc"
#undef g_deckySidebarRuntime
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static std::wstring image;
static std::filesystem::path output;
static json cases=json::array(),observations=json::array();
struct Fixture {
 Runtime runtime;std::atomic<unsigned> binds{0};
 Fixture(const std::string& name,std::function<std::string()> refresh={}){
  // Keep each inert fixture home below the managed evidence path budget.
  // Case names and shutdown budgets remain the original public observations.
  static const std::map<std::string,std::wstring> homes={{"exit",L"e"},{"off",L"o"},{"fatal",L"f"},{"late",L"l"},{"job-stop-failure",L"j"},{"binding-stall",L"b"},{"ready",L"r"},{"crash",L"c"}};
  const auto home=output/homes.at(name)/L"h";std::filesystem::create_directories(home);
  Config c;c.executable=image;c.home=home.wstring();c.preflight=[]{return std::string{};};
  c.refreshBinding=refresh?refresh:std::function<std::string()>([this]{++binds;return std::string{};});
  runtime.initialize(std::move(c));runtime.observeSteam(1,true);
 }
 ~Fixture(){runtime.shutdown();}
 void start(){need(runtime.setEnabled(true),"worker enabled");need(wait([&]{return runtime.snapshot().pid!=0;},3500),"source-built owned process started");}
 template<class F>bool wait(F f,DWORD ms){const auto end=GetTickCount64()+ms;do{if(f())return true;Sleep(5);}while(GetTickCount64()<end);return f();}
 HANDLE child(){const auto s=runtime.snapshot();auto h=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,s.pid);need(h!=nullptr,"retain own child handle");wchar_t p[32768];DWORD n=32768;need(QueryFullProcessImageNameW(h,0,p,&n)&&_wcsicmp(p,image.c_str())==0,"child is this source-built image");return h;}
};
static void record(const char* id,const char* requirement,const std::function<json()>& fn){json m=json::object();bool passed=false;std::string error;try{m=fn();passed=m.value("acceptance",true);}catch(const std::exception& e){error=e.what();}cases.push_back({{"id",id},{"name",id},{"passed",passed},{"measurements",m},{"error",error}});if(*requirement)observations.push_back({{"requirement",requirement},{"caseId",id},{"passed",passed},{"measurements",m}});}
int wmain(int argc,wchar_t** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 if(argc==1){if(GetEnvironmentVariableW(L"YMCC_LIFECYCLE_CRASH_CHILD",nullptr,0))return 37;Sleep(60000);return 0;}
 try{
  wchar_t p[32768];GetModuleFileNameW(nullptr,p,32768);image=p;output=std::filesystem::absolute(argv[1]);
  need(output.parent_path()==std::filesystem::path(image).parent_path()&&output.filename()==L"native-fixtures","explicit output next to this fixture image");std::filesystem::create_directories(output);
  record("native.exit-clears-owned-process-job-worker-event","destroyOnExit",[]{
   Fixture f("exit");f.start();auto h=f.child();const auto before=probe::remaining();const auto starts=probe::launches.load();const auto t=GetTickCount64();f.runtime.shutdown();const auto elapsed=GetTickCount64()-t;const bool exited=WaitForSingleObject(h,1200)==WAIT_OBJECT_0;CloseHandle(h);auto s=f.runtime.snapshot();
   need(before>=3&&exited&&s.pid==0&&!s.enabled&&probe::remaining()==0,"exit releases actual owned process and all tracked runtime handles");
   return json{{"heldResourcesBeforeExit",before},{"remainingOwnedResources",probe::remaining()},{"ownedProcessExitObserved",exited},{"workerJoined",true},{"bounded",elapsed<4500},{"ownerRevoked",!s.enabled},{"elapsedMs",elapsed},{"starts",starts}};
  });
  record("native.explicit-off-and-fatal-off-veto-late-wakes","explicitOffStaysOff",[]{
   unsigned rejected=0;ULONGLONG maxOffMs=0;for(const auto& reason:{std::string{},std::string("decky-render-failed")}){Fixture f(reason.empty()?"off":"fatal");f.start();auto h=f.child();if(!reason.empty()){const std::string run(32,'a');need(ymcc::deckyhealth::matches(json{{"reason",reason},{"runId",run}},json{{"enabled",true},{"phase","loader-started"}},run),"actual fatal policy admits scoped error");}
   const auto offStarted=GetTickCount64();f.runtime.setEnabled(false,reason);maxOffMs=(std::max)(maxOffMs,GetTickCount64()-offStarted);need(WaitForSingleObject(h,1000)==WAIT_OBJECT_0,"OFF actually terminates owned child");CloseHandle(h);const auto starts=probe::launches.load();for(unsigned i=0;i<16;++i){f.runtime.observeSteam(1,true);rejected+=!f.runtime.requestTransportRecovery();rejected+=!f.runtime.requestBindingRefresh(i+1);rejected+=!f.runtime.requestLaunchAdmissionRefresh();f.runtime.requestConnectionCheck();}Sleep(40);need(probe::launches==starts&&!f.runtime.snapshot().enabled&&f.runtime.snapshot().reason==reason,"OFF and fatal OFF survive all late wakes");}
   return json{{"bounded",maxOffMs<4500},{"elapsedMs",maxOffMs},{"ownerRevoked",true},{"explicitOffHonored",true},{"fatalOffHonored",true},{"rejectedLateRequests",rejected},{"additionalStarts",0},{"remainingOwnedResources",probe::remaining()}};
  });
  record("native.late-actual-recovery-callback-after-exit","preventResurrectionAfterExit",[]{
   Fixture f("late");f.start();auto h=f.child();const auto exitStarted=GetTickCount64();f.runtime.shutdown();const auto shutdownElapsed=GetTickCount64()-exitStarted;need(WaitForSingleObject(h,1000)==WAIT_OBJECT_0,"exit before late callback");CloseHandle(h);const auto starts=probe::launches.load();const auto lateStarted=GetTickCount64();unsigned delivered=0;
   for(unsigned i=0;i<8;++i){++delivered;f.runtime.requestTransportRecovery();f.runtime.requestBindingRefresh(i+2);f.runtime.requestConnectionCheck();f.runtime.observeSteam(1,true);}
   Sleep(40);need(probe::launches==starts&&probe::remaining()==0&&!f.runtime.snapshot().enabled,"late recovery cannot re-create worker or process");return json{{"bounded",shutdownElapsed<4500},{"shutdownElapsedMs",shutdownElapsed},{"lateDeliveryElapsedMs",GetTickCount64()-lateStarted},{"lateCallbacksDelivered",delivered},{"restartsAfterExit",probe::launches-starts},{"remainingOwnedResources",probe::remaining()},{"ownerRevoked",true}};
  });
  record("native.job-stop-failure-falls-back-to-owned-job-close","destroyWhenStopStalls",[]{
   // One unrelated inert sentinel is outside the Runtime Job; it must survive.
   STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION other{};std::wstring cmd=L"\""+image+L"\"";need(CreateProcessW(image.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&other),"inert unrelated sentinel");CloseHandle(other.hThread);
   Fixture f("job-stop-failure");f.start();auto h=f.child();const auto fallbackBefore=probe::jobCloseFallbacks.load();const auto t=GetTickCount64();probe::denyJobStop=true;f.runtime.shutdown();probe::denyJobStop=false;const auto elapsed=GetTickCount64()-t;const bool exited=WaitForSingleObject(h,1200)==WAIT_OBJECT_0,sentinelAlive=WaitForSingleObject(other.hProcess,0)==WAIT_TIMEOUT;CloseHandle(h);TerminateProcess(other.hProcess,0);WaitForSingleObject(other.hProcess,1000);CloseHandle(other.hProcess);
   const bool fallback=probe::jobCloseFallbacks>fallbackBefore;need(exited&&sentinelAlive&&fallback&&probe::remaining()==0,"real KILL_ON_JOB_CLOSE fallback only destroys owned child");
   return json{{"fallbackExercised",fallback},{"fallbackOwnedOnly",sentinelAlive},{"remainingOwnedResources",probe::remaining()},{"bounded",elapsed<4500},{"ownerRevoked",!f.runtime.snapshot().enabled},{"elapsedMs",elapsed},{"injection","TerminateJobObject denied on owned Job; real CloseHandle kills owned Job"}};
  });
  record("native.stalled-binding-stop-requires-fixture-release","destroyWhenStopStalls",[]{
   std::mutex mutex;std::condition_variable cv;bool entered=false,release=false;std::atomic<bool> block{false},done{false};
   Fixture f("binding-stall",[&]{if(block){std::unique_lock l(mutex);entered=true;cv.notify_all();cv.wait(l,[&]{return release;});}return std::string{};});f.start();auto h=f.child();block=true;need(f.runtime.requestBindingRefresh(900),"start actual blocked binding");{std::unique_lock l(mutex);need(cv.wait_for(l,std::chrono::seconds(2),[&]{return entered;}),"production worker entered held callback");}
   const auto t=GetTickCount64();std::thread stop([&]{f.runtime.shutdown();done=true;});Sleep(3200);const bool completedBeforeRelease=done;const auto heldAtDeadline=probe::remaining();const bool childAlive=WaitForSingleObject(h,0)==WAIT_TIMEOUT;{std::lock_guard l(mutex);release=true;}cv.notify_all();stop.join();const auto elapsed=GetTickCount64()-t;const bool exited=WaitForSingleObject(h,1200)==WAIT_OBJECT_0;CloseHandle(h);
   return json{{"acceptance",completedBeforeRelease&&!childAlive},{"bounded",completedBeforeRelease},{"ownerRevoked",!f.runtime.snapshot().enabled},{"fallbackExercised",false},{"fallbackOwnedOnly",false},{"completedBeforeFixtureRelease",completedBeforeRelease},{"heldResourcesAtDeadline",heldAtDeadline},{"ownedChildAliveAtDeadline",childAlive},{"ownedChildRevokedBeforeCallbackRelease",!childAlive},{"deadlineMs",3200},{"elapsedMs",elapsed},{"fixtureReleaseRequired",!completedBeforeRelease},{"remainingOwnedResources",probe::remaining()},{"ownedProcessExitedAfterRelease",exited},{"gap","Runtime synchronously joins an uncooperative binding callback; no bounded callback-stop fallback observed"}};
  });
  record("native.committed-ready-executes-original-rebind-consumer","",[]{
   Fixture f("ready");activeRuntime=&f.runtime;f.start();auto before=f.binds.load(),starts=probe::launches.load();powerGeneration=2;g_powerLifecycle=PowerLifecycle::Suspended;consumeCommittedReady();Sleep(40);const auto sleeping=f.binds.load();g_powerLifecycle=PowerLifecycle::Resuming;consumeCommittedReady();Sleep(40);const auto resuming=f.binds.load();g_powerLifecycle=PowerLifecycle::Ready;consumeCommittedReady();need(f.wait([&]{return f.binds>before;},1000),"Ready consumes original requestBindingRefresh");auto after=f.binds.load();consumeCommittedReady();Sleep(40);need(before==sleeping&&before==resuming&&after==before+1&&f.binds==after&&probe::launches==starts,"Ready only once per generation and no Loader restart");f.runtime.shutdown();activeRuntime=nullptr;
   return json{{"sleepObserved",true},{"readyGranted",true},{"currentGeneration",2},{"bindingCallsBefore",before},{"bindingCallsSleeping",sleeping},{"bindingCallsResuming",resuming},{"bindingCallsAfterReady",after},{"additionalStarts",probe::launches-starts},{"healthObserved",false},{"scope","exact native committed-ready consumer; frontend health separately executed by host/client cases"},{"diagnostics",diagnostics}};
  });
  record("native.crash-budget-cannot-renew-on-recovery-or-unknown","respectRecoveryLimits",[]{
   SetEnvironmentVariableW(L"YMCC_LIFECYCLE_CRASH_CHILD",L"1");Fixture f("crash");const auto starts=probe::launches.load();f.runtime.setEnabled(true);need(f.wait([&]{return f.runtime.snapshot().reason=="loader-crash-loop";},24000),"actual Runtime reaches five-crash limit");SetEnvironmentVariableW(L"YMCC_LIFECYCLE_CRASH_CHILD",nullptr);const auto atLimit=probe::launches.load();need(atLimit-starts==LoaderCrashBudget::maxUnstableExits,"exact original crash budget");for(unsigned i=0;i<20;++i){f.runtime.requestTransportRecovery();f.runtime.requestBindingRefresh(1000+i);f.runtime.requestLaunchAdmissionRefresh();f.runtime.requestConnectionCheck();f.runtime.observeSteam(-1);f.runtime.observeSteam(1);}Sleep(400);const auto added=probe::launches.load()-atLimit;need(added==0&&f.runtime.snapshot().reason=="loader-crash-loop","unknown and recovery cannot reset crash budget");f.runtime.setEnabled(false);f.runtime.observeSteam(1,true);Sleep(40);const bool off=!f.runtime.snapshot().enabled&&probe::launches==atLimit;need(off,"OFF still vetoes verified session");
   return json{{"limitReached",true},{"attempts",atLimit-starts},{"maxAttempts",LoaderCrashBudget::maxUnstableExits},{"additionalStarts",added},{"explicitOffHonored",off},{"remainingOwnedResources",probe::remaining()}};
  });
  const bool pass=std::all_of(cases.begin(),cases.end(),[](const json& c){return c.at("passed").get<bool>();});
  json report={{"schemaVersion",1},{"passed",pass},{"nativeExecuted",true},{"realSteamActions",0},{"officialLoaderExecuted",false},{"ownedChildrenAreThisTestImage",true},{"cases",cases},{"lifecycleEvidence",{{"schemaVersion",1},{"features",json::array({{{"id","plugins.decky"},{"observations",observations}}})}}}};
  std::ofstream(output.parent_path()/L"native-result.json")<<report.dump(2);std::cout<<"DECKY_PERSISTENT_NATIVE cases="<<cases.size()<<" passed="<<pass<<"\n";return pass?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
