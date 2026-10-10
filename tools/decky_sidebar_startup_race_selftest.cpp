// Exact production Runtime, source-built inert child only. No Steam/CDP/Loader/business/hardware IO.
#include "../native/decky_sidebar_runtime.h"
#include <iostream>
#include <atomic>
using namespace ymcc::deckysidebar;
static bool until(const std::function<bool()>& predicate,DWORD maximum=4000){const auto end=GetTickCount64()+maximum;do{if(predicate())return true;Sleep(5);}while(GetTickCount64()<end);return false;}
int main(){
 unsigned checks=0;const auto check=[&](bool condition,const char* label){if(!condition)throw std::runtime_error(label);++checks;};
 try{
  wchar_t path[32768];const auto size=GetModuleFileNameW(nullptr,path,32768);if(!size||size>=32768)throw std::runtime_error("fixture path");
  const std::wstring executable(path,size);const auto build=std::filesystem::path(executable).parent_path();
  if(build!=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Input27\Build)")throw std::runtime_error("fixture outside explicit task root");
  const auto home=(build/L"YMCC-Decky-Startup-Race-Fixture-Home").wstring();
  wchar_t inherited[32768];const auto count=GetEnvironmentVariableW(L"UNPRIVILEGED_PATH",inherited,32768);
  if(count&&count<32768&&home==std::wstring(inherited,count)){Sleep(40000);return 0;}
  std::filesystem::create_directories(home);
  {
   Runtime runtime;std::atomic<unsigned> preparations{0};
   runtime.initialize({executable,home,[]{return std::string{};},{},{},{},{},[&]{return ++preparations==1?std::string("live-debug-unavailable"):std::string{};}});
   runtime.observeSteam(1);runtime.setEnabled(true);
   check(until([&]{return preparations==1&&runtime.snapshot().phase=="unavailable";}),"startup missing listener observed");
   check(runtime.snapshot().bindingRetry,"missing listener must open finite retry, not terminal unavailable");
   check(!runtime.snapshot().pid,"no Loader before verified binding");
   check(until([&]{return runtime.snapshot().phase=="loader-started"&&runtime.snapshot().pid!=0;}),"stable Steam session late listener recovers without new session event");
   check(preparations==2,"one bounded retry and then idle");const auto pid=runtime.snapshot().pid;Sleep(150);check(runtime.snapshot().pid==pid&&preparations==2,"no duplicate child or periodic binding after success");
   runtime.shutdown();check(!runtime.snapshot().pid,"owned inert child stopped");
  }
  {
   Runtime runtime;std::atomic<unsigned> preparations{0};
   runtime.initialize({executable,home,[]{return std::string{};},{},{},{},{},[&]{++preparations;return std::string("live-debug-unavailable");}});
   runtime.observeSteam(1);runtime.setEnabled(true);check(until([&]{return runtime.snapshot().bindingRetry;}),"cancel case has bounded startup window");runtime.setEnabled(false);Sleep(1100);
   check(preparations==1&&!runtime.snapshot().pid,"disable cancels retry before launch");runtime.shutdown();
  }
  {
   Runtime runtime;std::atomic<unsigned> preparations{0};
   runtime.initialize({executable,home,[]{return std::string{};},{},{},{},{},[&]{++preparations;return std::string("live-debug-not-loopback");}});
   runtime.observeSteam(1);runtime.setEnabled(true);check(until([&]{return runtime.snapshot().phase=="unavailable";}),"network-exposed listener rejected");Sleep(1100);
   check(preparations==1&&!runtime.snapshot().bindingRetry&&!runtime.snapshot().pid,"permanent admission error never retried");runtime.shutdown();
  }
  {
   Runtime runtime;std::atomic<unsigned> refreshes{0};
   runtime.initialize({executable,home,[]{return std::string{};},{},[&]{return ++refreshes==1?std::string("live-debug-unavailable"):std::string{};},{},{},[]{return std::string{};}});
   runtime.observeSteam(1);runtime.setEnabled(true);check(until([&]{return runtime.snapshot().pid!=0;}),"existing inert child starts");const auto pid=runtime.snapshot().pid;
   check(runtime.requestBindingRefresh(42),"original demand opens transport-only refresh");check(until([&]{return runtime.snapshot().bindingRetry;}),"existing child missing listener gets bounded transport window");
   check(until([&]{return refreshes==2&&!runtime.snapshot().bindingRetry;}),"transport becomes available on finite retry");check(runtime.snapshot().pid==pid,"transport retry never restarts the child");runtime.shutdown();
  }
  std::cout<<"Steam listener startup race tests passed: "<<checks<<"; actualSteam=0; thirdParty=0; configWrites=0; hardware=0\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
