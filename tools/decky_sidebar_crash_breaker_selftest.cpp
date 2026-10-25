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
static void policy(){
 LoaderCrashBudget b;
 for(unsigned n=1;n<LoaderCrashBudget::maxUnstableExits;++n){check(b.exited(10),"bounded crash may recover");check(b.failures()==n,"crash count exact");}
 check(!b.exited(10)&&b.blocked(),"fifth short-lived crash opens breaker");
 for(unsigned n=0;n<100;++n)check(!b.exited(0)&&b.failures()==5,"blocked count saturates");
 b.reset();check(!b.blocked()&&b.failures()==0,"explicit reset renews budget");
 b.exited(10);b.exited(10);check(b.exited(LoaderCrashBudget::stableUptimeMs)&&b.failures()==1,"sustained health renews before next failure");
 b.healthy(LoaderCrashBudget::stableUptimeMs);check(b.failures()==0,"healthy owner resets budget");
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
  runtime.observeSteam(1,true);runtime.setEnabled(true);
  check(wait("loader-crash-loop",25000),"production runtime stops repeated inert child exits");
  const auto count=launches.load();check(count==5,"exactly five owned launches before stop");
  runtime.requestTransportRecovery();runtime.observeSteam(1);runtime.requestBindingRefresh(1);runtime.requestLaunchAdmissionRefresh();
  {std::unique_lock lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(1100));}
  check(launches==count&&runtime.snapshot().reason=="loader-crash-loop","connection and mode wakes cannot bypass crash breaker");
  runtime.observeSteam(-1);runtime.observeSteam(1);
  {std::unique_lock lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(700));}
  check(launches==count,"unknown observation cannot renew crash budget");
  runtime.observeSteam(1,true);
  {std::unique_lock lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(700),[&]{return launches.load()>count;});}
  check(launches>count,"verified new session renews launch budget");runtime.shutdown();
  check(runtime.snapshot().pid==0,"shutdown leaves no owned child");
  std::ofstream output(scope/L"result.json");output<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"productionRuntime\":true,\"realSteamActions\":0,\"productStarted\":false,\"ownedInertChildLaunches\":"<<launches.load()<<"}";
  std::cout<<"DECKY_CRASH_BREAKER_OK checks="<<checks<<" launches="<<launches.load()<<" realSteamActions=0\n";return 0;
 }catch(const std::exception& e){std::cerr<<"DECKY_CRASH_BREAKER_FAILED "<<e.what()<<"\n";return 1;}
}
