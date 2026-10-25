// Historical in-process callback regression; not the owned binding production acceptance.
#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1
// Exact production safety policy + normal Runtime off path with one self-built inert child.
// Explicit task-only directory, no Steam, third-party Loader, user settings or hardware.
#include "../native/decky_sidebar_runtime.h"
#include "../native/decky_sidebar_health.h"
#include <condition_variable>
#include <iostream>
using namespace ymcc::deckysidebar;
int wmain(int argc,wchar_t** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 if(argc==1){Sleep(15000);return 0;} // This same test image is the only permitted child.
 unsigned checks=0;auto check=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);++checks;};
 try{
  wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);
  const auto scope=std::filesystem::absolute(argv[1]).lexically_normal();
  check(scope.filename()==L"health-close-fixture"&&scope.parent_path()==std::filesystem::path(image).parent_path(),"fixture is next to this test image in explicit task output");
  std::filesystem::create_directories(scope/L"home");
  Runtime runtime;std::atomic<unsigned> launched{0};std::mutex mutex;std::condition_variable changed;
  runtime.initialize({image,(scope/L"home").wstring(),[]{return std::string{};},[&](const State& state){if(state.phase=="loader-started")++launched;changed.notify_all();}});
  runtime.observeSteam(1,true);runtime.setEnabled(true);
  {std::unique_lock lock(mutex);check(changed.wait_for(lock,std::chrono::seconds(3),[&]{return runtime.snapshot().phase=="loader-started";}),"production runtime starts only self-built inert child");}
  const auto before=runtime.snapshot();HANDLE child=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,before.pid);check(child!=nullptr,"owned child handle retained for termination proof");
  wchar_t childImage[32768]{};DWORD length=32768;const bool same=QueryFullProcessImageNameW(child,0,childImage,&length)&&_wcsicmp(image,childImage)==0;
  if(!same){CloseHandle(child);throw std::runtime_error("child image mismatch");}++checks;
  const std::string run(32,'a');nlohmann::json event={{"reason","decky-render-failed"},{"runId",run}},state={{"enabled",before.enabled},{"phase",before.phase}};
  check(ymcc::deckyhealth::matches(event,state,run),"scoped actual fatal evidence admitted");
  runtime.setEnabled(false,"decky-render-failed");
  const bool terminated=WaitForSingleObject(child,1000)==WAIT_OBJECT_0;CloseHandle(child);check(terminated,"normal off kills and waits for only the owned Job child");
  check(!runtime.snapshot().enabled&&runtime.snapshot().phase=="disabled"&&runtime.snapshot().pid==0,"off state and process ownership are consistent");
  check(runtime.snapshot().reason=="decky-render-failed","close reason survives worker final disabled publication");
  const auto n=launched.load();runtime.observeSteam(0);runtime.observeSteam(1,true);runtime.requestTransportRecovery();runtime.requestBindingRefresh(1);runtime.requestLaunchAdmissionRefresh();
  check(launched==n&&!runtime.snapshot().enabled&&runtime.snapshot().reason=="decky-render-failed","session and transport events cannot reopen safety close");
  runtime.setEnabled(true);
  {std::unique_lock lock(mutex);check(changed.wait_for(lock,std::chrono::seconds(3),[&]{return launched>n;}),"explicit user re-enable can start a new owned child");}
  check(runtime.snapshot().reason!="decky-render-failed","explicit enable clears runtime fault");runtime.shutdown();
  std::cout<<"DECKY_HEALTH_CLOSE_OK checks="<<checks<<" ownedInertChildren="<<launched.load()<<" realSteamActions=0\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
