// Historical in-process callback regression; not the owned binding production acceptance.
#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1
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
  if(build!=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-SharedSteamSignal-20261014\Build)" && build!=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Sidebar\Build)" && build!=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-StartupRecovery-20261014\Build)" && build!=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Input27\Build)" && build!=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-OpenBlocked-20261013\Build)" && build!=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-SelfHeal-20261013\Build)")throw std::runtime_error("fixture outside explicit task root");
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
  {
   Runtime runtime;std::atomic<unsigned> preparations{0},refreshes{0};
   runtime.initialize({executable,home,[]{return std::string{};},{},[&]{++refreshes;return std::string{};},{},{},[&]{++preparations;return std::string{};}});
   runtime.observeSteam(1);runtime.setEnabled(true);check(until([&]{return runtime.snapshot().pid!=0;}),"owned inert child starts for unexpected exit test");
   const auto first=runtime.snapshot().pid;HANDLE owned=OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,first);
   check(owned!=nullptr,"only fixture child lease opened");check(TerminateProcess(owned,1)!=FALSE,"inert fixture child exits unexpectedly");CloseHandle(owned);
   check(until([&]{const auto state=runtime.snapshot();return state.pid&&state.pid!=first&&state.phase=="loader-started";}),"unexpected owned exit automatically restarts without Steam mode change");
   check(preparations==2,"one recovery launch performs fresh preparation");
   const auto second=runtime.snapshot().pid;check(runtime.requestTransportRecovery(),"watch failure event opens outage recovery");
   check(until([&]{return refreshes==1;}),"watch recovery reaches existing transport binder");check(runtime.snapshot().pid==second,"watch recovery preserves owned loader");
   runtime.shutdown();check(!runtime.requestTransportRecovery(),"disabled demand cannot reopen recovery");
  }
  {
   Runtime runtime;std::atomic<bool> ready{false};std::atomic<unsigned> preparations{0};
   ymcc::deckysidebar::Config cfg;cfg.executable=executable;cfg.home=home;
   cfg.preflight=[]{return std::string{};};cfg.prepareBinding=[&]{++preparations;return std::string{};};
   cfg.launchAdmission=[&]{return ready?ymcc::deckysidebar::LaunchAdmission{}:ymcc::deckysidebar::LaunchAdmission{false,"steam-ui-state-pending",true,"inert-delayed-ui"};};
   runtime.initialize(cfg);runtime.observeSteam(1);runtime.setEnabled(true);
   check(until([&]{return runtime.snapshot().admissionRetry;}),"initial delayed startup opens an admission cycle");
   ready=true;
   check(until([&]{return runtime.snapshot().pid!=0;}),"owned child starts after delayed initial admission");
   const auto first=runtime.snapshot().pid;HANDLE owned=OpenProcess(PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,first);
   wchar_t image[32768]{};DWORD length=32768;
   check(owned&&QueryFullProcessImageNameW(owned,0,image,&length)&&!_wcsicmp(image,executable.c_str()),"exit test verifies exact source-built fixture image");
   ready=false;check(TerminateProcess(owned,1)!=FALSE,"only verified inert fixture exits");CloseHandle(owned);
   check(until([&]{return runtime.snapshot().phase=="waiting-manual-steam";}),"loader restart waits for genuine UI confirmation");
   check(!runtime.snapshot().pid&&runtime.snapshot().admissionRetry,"pending restart stays scheduled during mode outage");
   ready=true;
   check(until([&]{const auto s=runtime.snapshot();return s.pid&&s.pid!=first&&s.phase=="loader-started";},3500),"confirmed UI resumes an owned-loader restart without a new Steam session or user action");
   check(preparations==2,"recovery performs one fresh preparation without duplicate children");runtime.shutdown();
  }
  std::cout<<"Steam listener startup race tests passed: "<<checks<<"; actualSteam=0; thirdParty=0; configWrites=0; hardware=0\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
