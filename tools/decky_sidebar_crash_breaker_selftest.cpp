// Historical in-process callback regression; not the owned binding production acceptance.
#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1
#include "../native/decky_sidebar_runtime.h"
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace ymcc::deckysidebar;
static int checks=0;
static void check(bool value,const char* label){if(!value)throw std::runtime_error(label);++checks;}
static void observedSession(Runtime& runtime,const ymcc::steamsession::Session& session){
 runtime.observeSteam(1,session); // Exact current automatic caller: verified observer snapshot.
}
static void policy(){
 LoaderCrashBudget b;
 for(unsigned n=1;n<LoaderCrashBudget::maxUnstableExits;++n){check(b.exited(10),"bounded crash may recover");check(b.failures()==n,"crash count exact");}
 check(!b.exited(10)&&b.blocked(),"fifth short-lived crash opens breaker");
 for(unsigned n=0;n<100;++n)check(!b.exited(0)&&b.failures()==5,"blocked count saturates");
 b.reset();check(!b.blocked()&&b.failures()==0,"explicit reset renews budget");
 b.exited(10);b.exited(10);b.healthy(LoaderCrashBudget::stableUptimeMs);check(b.exited(LoaderCrashBudget::stableUptimeMs)&&b.failures()==1,"sustained health renews before next failure");
 b.healthy(LoaderCrashBudget::stableUptimeMs);check(b.failures()==0,"healthy owner resets budget");
 // Mere lifetime cannot forgive repeated failures that never had a verified mirror/backend/hooks receipt.
 LoaderCrashBudget unconfirmed;
 for(unsigned n=1;n<=LoaderCrashBudget::maxUnstableExits;++n){
  const bool retry=unconfirmed.exited(LoaderCrashBudget::stableUptimeMs+1);
  check(unconfirmed.failures()==n,"long-lived unconfirmed exits retain exact crash debt");
  check(retry==(n<LoaderCrashBudget::maxUnstableExits),"five unconfirmed long-lived exits open the same breaker");
 }
 check(!unconfirmed.exited(24ull*60*60*1000)&&unconfirmed.blocked(),"a day alive without health cannot reopen a blocked breaker");
 unconfirmed.healthy(LoaderCrashBudget::stableUptimeMs-1);
 check(unconfirmed.blocked(),"short verified health cannot refund the stable-uptime budget");
 unconfirmed.healthy(LoaderCrashBudget::stableUptimeMs);
 check(unconfirmed.exited(LoaderCrashBudget::stableUptimeMs+1)&&unconfirmed.failures()==1,"explicit stable verified health still renews the budget");
 // The actual exit API must consume the current-child witness as well as lifetime.
 LoaderCrashBudget witnessed;witnessed.exited(10);witnessed.exited(10);
 check(witnessed.exited(LoaderCrashBudget::stableUptimeMs-1,true)&&witnessed.failures()==3,"brief full-channel witness keeps prior crash debt");
 check(witnessed.exited(LoaderCrashBudget::stableUptimeMs,true)&&witnessed.failures()==1,"full-channel witness and stable lifetime renew crash budget on exit");
 check(witnessed.exited(LoaderCrashBudget::stableUptimeMs+1,false)&&witnessed.failures()==2,"later unconfirmed child cannot borrow an earlier health witness");
 ymcc::deckysidebar::LoaderSessionBudget sessions;
 check(!sessions.observe(L"",1,1,1),"empty Steam root cannot grant budget");
 check(!sessions.observe(L"fixture",0,1,1),"missing Steam pid cannot grant budget");
 check(!sessions.observe(L"fixture",1,0,1),"missing Steam birth cannot grant budget");
 check(sessions.observe(L"fixture",1,2,73),"first verified instance grants budget");
 for(unsigned n=0;n<12;++n){
  check(!sessions.observe(L"fixture",1,2,0),"missing account cannot grant budget");
  check(!sessions.observe(L"fixture",1,2,73),"returning same account cannot grant budget");
 }
 check(sessions.observe(L"fixture",1,2,74),"different known account grants once");
 check(!sessions.observe(L"fixture",1,2,74),"repeated account cannot grant twice");
 check(sessions.observe(L"fixture",1,3,0),"new process birth grants despite pending account");
 check(sessions.observe(L"fixture",1,3,74),"first known account of new process grants once");
 check(!sessions.observe(L"fixture",1,3,0),"new process later account loss cannot grant");
 check(!sessions.observe(L"fixture",1,3,74),"new process returning account cannot grant");
 check(sessions.observe(L"fixture",2,3,74),"new verified pid grants");
 check(sessions.observe(L"fixture-other",2,3,74),"new verified install root grants");
 ConnectionRetryWindow w;w.begin(100);check(w.timeout(100)==250,"existing transport backoff retained");
}
int wmain(int argc,wchar_t** argv){
 if(argc==1)return 37; // Source-built inert child: no product code, file writes or Steam operations.
 try{
  policy();const std::filesystem::path scope=std::filesystem::absolute(argv[1]);
  check(scope.filename()==L"crash-fixture","explicit owned fixture root");std::filesystem::create_directories(scope/L"home");
  wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);
  Runtime runtime;std::atomic<unsigned> launches{0};std::mutex mutex;std::condition_variable changed;
  runtime.initialize({image,(scope/L"home").wstring(),[]{return std::string{};},[&](const State& state){if(state.phase=="loader-started")++launches;changed.notify_all();}});
  auto wait=[&](const char* reason,unsigned ms){std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::milliseconds(ms),[&]{return runtime.snapshot().reason==reason;});};
  ymcc::steamsession::Session session{(scope/L"home").wstring(),41,73,101};
  observedSession(runtime,session);runtime.setEnabled(true);
  check(wait("loader-crash-loop",25000),"production runtime stops repeated inert child exits");
  const auto count=launches.load();check(count==5,"exactly five owned launches before stop");
  runtime.requestTransportRecovery();runtime.observeSteam(1);runtime.requestBindingRefresh(1);runtime.requestLaunchAdmissionRefresh();
  {std::unique_lock lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(1100),[&]{return launches.load()!=count;});}
  check(launches==count&&runtime.snapshot().reason=="loader-crash-loop","connection and mode wakes cannot bypass crash breaker");
  runtime.observeSteam(-1);runtime.observeSteam(1);
  {std::unique_lock lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(700),[&]{return launches.load()!=count;});}
  check(launches==count,"unknown observation cannot renew crash budget");
  for(unsigned n=0;n<12;++n){session.account=0;observedSession(runtime,session);session.account=73;observedSession(runtime,session);}
  {std::unique_lock lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(700),[&]{return launches.load()!=count;});}
  check(launches==count,"same-instance missing and returning account cannot renew crash budget");

  session.account=74;observedSession(runtime,session);
  {std::unique_lock lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(700),[&]{return launches.load()>count;});}
  check(launches>count,"verified new session renews launch budget");runtime.shutdown();
  check(runtime.snapshot().pid==0,"shutdown leaves no owned child");
  std::ofstream output(scope/L"result.json");output<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"productionRuntime\":true,\"realSteamActions\":0,\"productStarted\":false,\"ownedInertChildLaunches\":"<<launches.load()<<"}";
  std::cout<<"DECKY_CRASH_BREAKER_OK checks="<<checks<<" launches="<<launches.load()<<" realSteamActions=0\n";return 0;
 }catch(const std::exception& e){std::cerr<<"DECKY_CRASH_BREAKER_FAILED "<<e.what()<<"\n";return 1;}
}
