// Historical in-process callback regression; not the owned binding production acceptance.
#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1
// Production Runtime. Only this test image can be launched; no installed apps/Steam/Loader.
#include "../native/decky_sidebar_runtime.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <fstream>
using namespace ymcc::deckysidebar;
static unsigned checks=0;
static void need(bool ok,const char* label){if(!ok)throw std::runtime_error(label);++checks;}
template<class F>static bool waitFor(F f,DWORD ms=1500){const auto end=GetTickCount64()+ms;do{if(f())return true;Sleep(2);}while(GetTickCount64()<end);return f();}
static Config inert(){Config c;c.executable=L"NEVER-LAUNCH.exe";c.home=L"NEVER-WRITE";c.preflight=[]{return std::string("test-no-launch");};return c;}
int wmain(int argc,wchar_t** argv){
 wchar_t mode[16]{};if(GetEnvironmentVariableW(L"YMCC_RUNTIME_PREPARATION_CHILD",mode,16)){
  if(mode[0]==L'c')return 37;Sleep(30000);return 0;
 }
 try{
  need(argc==2,"explicit test output root");const auto scope=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(scope);
  {
   Runtime r;std::atomic<unsigned> prep{0},preflight{0},admissions{0};auto c=inert();
   c.prepareEnvironment=[&](const std::function<bool()>& current){need(current(),"preparation current on entry");++prep;return std::string{};};
   c.preflight=[&]{++preflight;return std::string("test-no-launch");};
   c.launchAdmission=[&]{++admissions;return LaunchAdmission{false,"manual-only",false};};
   r.initialize(std::move(c));need(!r.requestPreparationRefresh(1),"OFF rejects Ready without consuming generation");
   r.setEnabled(true);Sleep(40);need(prep==0,"unknown presence does not prepare");
   r.observeSteam(0);need(waitFor([&]{return prep==1&&r.snapshot().phase=="waiting-steam";}),"known absent Steam prepares on existing worker");
   need(preflight==0&&admissions==0&&r.snapshot().pid==0,"absent preparation grants no Loader IO");
   need(r.requestPreparationRefresh(1),"Ready accepted without Steam or Loader");need(waitFor([&]{return prep==2;}),"Ready absent wakes preparation");
   for(unsigned i=0;i<40;++i)need(!r.requestPreparationRefresh(1),"same Ready generation coalesced");
   need(!r.requestPreparationRefresh(0),"zero Ready rejected");r.observeSteam(1);need(waitFor([&]{return r.snapshot().phase=="waiting-manual-steam";}),"existing launch admission still vetoes");
   const auto count=admissions.load();need(r.requestPreparationRefresh(2),"new Ready when admission blocked");need(waitFor([&]{return prep==3&&admissions>count;}),"worker prepares under blocked admission");
   need(preflight==0&&r.snapshot().admissionAttempts==1,"Ready never renews admission window or grants launch");
   r.setEnabled(false,"decky-render-failed");need(!r.requestPreparationRefresh(3),"fault OFF rejects refresh");r.observeSteam(0);r.observeSteam(1,true);Sleep(30);
   need(prep==3&&!r.snapshot().enabled&&r.snapshot().reason=="decky-render-failed","late observers cannot resurrect fault OFF");
   r.setEnabled(true);need(waitFor([&]{return prep==4;}),"explicit enable prepares again");r.shutdown();
  }
  for(const auto reason:{"preparation-deferred","preparation-cancelled","resource-missing","resource-hash-mismatch","resource-read-failed","preparation-failed"}){
   Runtime r;std::atomic<unsigned> prep{0};auto c=inert();c.prepareEnvironment=[&](const auto&){return ++prep==1?std::string(reason):std::string{};};
   r.initialize(std::move(c));r.observeSteam(0);r.setEnabled(true);need(waitFor([&]{return prep==1;}),"deferred attempt executed");
   Sleep(60);need(prep==1,"deferred or cancelled callback cannot spin or start timer");
   r.observeSteam(1);need(waitFor([&]{return prep==2;}),"unfinished preparation retried on next intent");
   r.observeSteam(1);Sleep(30);need(prep==2,"duplicate observation does not retry unfinished work");
   r.observeSteam(0);need(waitFor([&]{return r.snapshot().phase=="waiting-steam";}),"successful retry settles after Steam changes");need(prep==2,"success completes epoch across subsequent Steam intents");
   r.requestPreparationRefresh(10);need(waitFor([&]{return prep==3;}),"fresh Ready refreshes successful preparation");r.shutdown();
  }
  {
   Runtime r;std::atomic<unsigned> prep{0},canceled{0},writes{0};auto c=inert();
   c.prepareEnvironment=[&](const std::function<bool()>& current){const auto n=++prep;if(n==1){while(current())Sleep(1);++canceled;return std::string("preparation-cancelled");}if(current())++writes;return std::string{};};
   r.initialize(std::move(c));r.observeSteam(0);r.setEnabled(true);need(waitFor([&]{return prep==1;}),"old-intent preparation entered");
   r.observeSteam(1);need(waitFor([&]{return canceled==1&&writes==1;}),"replacement intent cancels obsolete preparation and retries once");r.shutdown();
  }
  {
   Runtime r;std::atomic<bool> entered{false},cancelSeen{false};auto c=inert();c.prepareEnvironment=[&](const auto& current){entered=true;while(current())Sleep(1);cancelSeen=true;return std::string("preparation-cancelled");};
   r.initialize(std::move(c));r.observeSteam(0);r.setEnabled(true);need(waitFor([&]{return entered.load();}),"OFF fixture preparation entered");
   const auto t=GetTickCount64();r.shutdown();need(cancelSeen&&GetTickCount64()-t<1000,"OFF invalidates predicate before joining preparation");need(r.snapshot().pid==0,"cancelled preparation creates no child");
  }
  {
   Runtime r;std::atomic<bool> entered{false},replaced{false};std::atomic<unsigned> calls{0};auto c=inert();
   c.prepareEnvironment=[&](const auto& current){if(++calls==1){entered=true;while(current())Sleep(1);replaced=true;return std::string("preparation-cancelled");}return std::string{};};
   r.initialize(std::move(c));r.observeSteam(0);r.setEnabled(true);need(waitFor([&]{return entered.load();}),"Ready replacement fixture entered");r.requestPreparationRefresh(1);
   need(waitFor([&]{return replaced&&calls==2;}),"new Ready cancels older preparation even without Steam change");r.shutdown();
  }
  wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);
  {
   SetEnvironmentVariableW(L"YMCC_RUNTIME_PREPARATION_CHILD",L"hold");
   Runtime r;std::atomic<unsigned> prep{0};std::atomic<bool> block{false},entered{false},release{false},stopDone{false};auto c=inert();c.executable=image;c.home=(scope/L"healthy-home").wstring();std::filesystem::create_directories(c.home);
   c.preflight=[]{return std::string{};};c.prepareEnvironment=[&](const auto&){++prep;return std::string{};};
   c.refreshBinding=[&]{if(block){entered=true;while(!release)Sleep(1);}return std::string{};};
   r.initialize(std::move(c));r.observeSteam(1,true);r.setEnabled(true);need(waitFor([&]{return r.snapshot().pid!=0;},4000),"owned test child launched");
   auto child=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,r.snapshot().pid);need(child!=nullptr,"own child lease retained");
   const auto n=prep.load();for(unsigned i=1;i<=20;++i)r.requestPreparationRefresh(i);Sleep(60);
   need(prep==n&&WaitForSingleObject(child,0)==WAIT_TIMEOUT,"Ready never preheats or restarts healthy owned child");
   block=true;need(r.requestBindingRefresh(100),"hold real worker inside binding callback");need(waitFor([&]{return entered.load();}),"uncooperative callback actually entered");
   const auto t=GetTickCount64();std::thread stopper([&]{r.shutdown();stopDone=true;});
   const bool childExited=WaitForSingleObject(child,2000)==WAIT_OBJECT_0;const auto revocationMs=GetTickCount64()-t;const bool joined=stopDone.load();
   // Fixture release is cleanup ONLY: full uncooperative shutdown must remain reported unsupported.
   release=true;stopper.join();CloseHandle(child);SetEnvironmentVariableW(L"YMCC_RUNTIME_PREPARATION_CHILD",nullptr);
   need(childExited&&revocationMs<2000,"stop revokes owned child BEFORE uncooperative callback release");need(!joined,"uncooperative worker cannot falsely report joined before callback release");
   std::ofstream(scope/L"uncooperative-boundary.json")<<"{\"ownedChildExitedBeforeRelease\":"<<(childExited?"true":"false")<<",\"workerJoinedBeforeRelease\":"<<(joined?"true":"false")<<",\"fullShutdownAcceptance\":false,\"revocationMs\":"<<revocationMs<<",\"fixtureReleaseWasCleanupOnly\":true}";
  }
  {
   SetEnvironmentVariableW(L"YMCC_RUNTIME_PREPARATION_CHILD",L"crash");Runtime r;std::atomic<unsigned> prep{0},launches{0};auto c=inert();c.executable=image;c.home=(scope/L"crash-home").wstring();std::filesystem::create_directories(c.home);
   c.preflight=[]{return std::string{};};c.prepareEnvironment=[&](const auto&){++prep;return std::string{};};c.changed=[&](const State& s){if(s.phase=="loader-started")++launches;};r.initialize(std::move(c));r.observeSteam(1,true);r.setEnabled(true);
   need(waitFor([&]{return r.snapshot().reason=="loader-crash-loop";},25000),"original five-crash breaker reached");need(launches==5,"exact original five-crash launch budget");
   const auto count=prep.load();for(unsigned i=1;i<=30;++i){r.requestPreparationRefresh(i);r.requestBindingRefresh(i);r.requestTransportRecovery();r.requestLaunchAdmissionRefresh();}
   need(waitFor([&]{return prep>count;}),"Ready can prepare at crash limit");Sleep(350);need(launches==5&&r.snapshot().reason=="loader-crash-loop","preparation and recovery cannot renew crash budget");r.shutdown();SetEnvironmentVariableW(L"YMCC_RUNTIME_PREPARATION_CHILD",nullptr);
  }
  std::ofstream(scope/L"result.json")<<"{\"passed\":true,\"checks\":"<<checks<<",\"realSteamActions\":0,\"officialLoaderExecuted\":false,\"fullUncooperativeShutdownAccepted\":false}";
  std::cout<<"RUNTIME_PREPARATION_OK checks="<<checks<<" fullUncooperativeShutdownAccepted=false\n";return 0;
 }catch(const std::exception& e){std::cerr<<"RUNTIME_PREPARATION_FAILED "<<e.what()<<'\n';return 1;}
}
