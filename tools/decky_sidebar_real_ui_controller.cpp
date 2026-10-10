// Development-only supervisor. Optional real native bootstrap uses production Runtime/Broker/injector unchanged.
// Owns only its pinned Loader Job. Steam is a read-observed process, never a kill target.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include "../native/decky_sidebar_runtime.h"
#include "../native/decky_sidebar_broker.h"
#include "../native/decky_sidebar_bootstrap.h"
#include "json.hpp"
#include <iostream>
#include <mutex>
using nlohmann::json;
static std::mutex output;
static void emit(const json& value){std::lock_guard lock(output);std::cout<<value.dump()<<std::endl;}
int wmain(int argc,wchar_t** argv){
 try{
  const bool nativeBootstrap=argc==4&&std::wstring(argv[3])==L"native-bootstrap";
  if(argc!=3&&!nativeBootstrap)throw std::runtime_error("Expected pinned loader, owned UI home, optional native-bootstrap");
  const auto executable=std::filesystem::weakly_canonical(argv[1]),home=std::filesystem::weakly_canonical(argv[2]);
  const auto allowed=std::filesystem::weakly_canonical(L"G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Decky-Sidebar\\Build\\RealSteamUI");
  const auto allowed21=std::filesystem::weakly_canonical(L"G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Decky-Console21\\Build\\RealSteamUI");
  const auto allowed22=std::filesystem::weakly_canonical(L"G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Decky-Console22\\Build\\RealSteamUI");
  const auto allowed23=std::filesystem::weakly_canonical(L"G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Decky-Console23\\Build\\RealSteamUI");
  const auto allowed25=std::filesystem::weakly_canonical(LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Launch25\Build\RealSteamUI)");
  const auto allowed27=std::filesystem::weakly_canonical(LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Input27\Build\RealSteamUI)");
  if(executable.filename()!=L"PluginLoader_noconsole.exe"||(home.parent_path().parent_path()!=allowed && home.parent_path().parent_path()!=allowed21 && home.parent_path().parent_path()!=allowed22 && home.parent_path().parent_path()!=allowed23 && home.parent_path().parent_path()!=allowed25 && home.parent_path().parent_path()!=allowed27)||home.filename()!=L"home")throw std::runtime_error("UI supervisor path scope rejected");
  if(!std::filesystem::is_regular_file(home/L"plugins"/L"ymcc-sidebar"/L"dist"/L"index.js"))throw std::runtime_error("Plugin missing");
  ymcc::deckymirror::Broker broker;ymcc::deckymirror::CancelSignal cancellation;ymcc::deckymirror::ContextHandleSlot context;ymcc::deckymirror::BootstrapWatch watch;
  ymcc::deckysidebar::Runtime runtime;ymcc::steamsession::Observer observer;
  auto inject=[&]() -> std::string {
   if(!nativeBootstrap)return {};
   wchar_t root[32768]{};DWORD bytes=sizeof(root);
   if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,root,&bytes)!=ERROR_SUCCESS)return "mirror-bootstrap-steam-unavailable";
   const auto value=broker.bootstrap();if(value.empty())return "mirror-broker-not-active";
   HANDLE observed=nullptr;const auto receipt=ymcc::deckymirror::injectBootstrap(root,value,cancellation.current(),&observed,&watch,[](const json& detail){auto event=detail;event["type"]="bootstrap-watch";emit(event);});
   context.replace(observed);
   emit({{"type","native-bootstrap"},{"ok",receipt.value("ok",false)},{"reason",receipt.value("reason",std::string{})},{"mutated",receipt.value("mutated",false)},{"contextPid",receipt.value("contextPid",0)},{"contextCreated",receipt.value("contextCreated",0ULL)},{"runId",value.value("runId",std::string{})},{"credentialsRecorded",false}});
   return receipt.value("ok",false)?std::string{}:receipt.value("reason",std::string("mirror-bootstrap-failed"));
  };
  auto startMirror=[&]() -> std::string {
   if(!nativeBootstrap)return {};
   broker.stop();
   if(!broker.start({[&](const std::string& connection,const json& request){const auto status=broker.status();emit({{"type","request"},{"connection",connection},{"runId",status["runId"]},{"revision",status["revision"]},{"request",request}});},[&](const std::string& connection,const std::string& runId,bool connected){const auto status=broker.status();emit({{"type","peer"},{"connection",connection},{"runId",runId},{"connected",connected},{"revision",status["revision"]}});}}))return "mirror-broker-start-failed";
   const auto result=inject();if(!result.empty())broker.stop();return result;
  };
  runtime.initialize({executable.wstring(),home.wstring(),[] {return std::string{};},[&](const ymcc::deckysidebar::State& s){
   if(nativeBootstrap&&s.phase!="starting"&&s.phase!="loader-started"){watch.stop();broker.stop();}
   if(s.phase=="loader-started"&&s.pid){
    ymcc::deckylaunch::Handle process,token;process.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,s.pid);
    const bool tokenRead=process.value&&OpenProcessToken(process.value,TOKEN_QUERY,&token.value);
    emit({{"type","loader-security"},{"pid",s.pid},{"tokenRead",tokenRead},{"unelevated",tokenRead&&ymcc::deckylaunch::unelevated(token.value)}});
   }
   emit({{"type","runtime"},{"phase",s.phase},{"pid",s.pid},{"enabled",s.enabled},{"steamKnown",s.steamKnown},{"steamRunning",s.steamRunning},{"revision",s.revision},{"bindingAttempts",s.bindingAttempts},{"reason",s.reason},{"error",s.error},{"launchStage",s.launchStage},{"launchMode",s.launchMode}});
  },startMirror,[&] {cancellation.renew();},[&] {cancellation.cancel();},startMirror,[&] {return context.take();}});
  runtime.setEnabled(true);
  if(!observer.start([&](bool changed){runtime.observeSteam(observer.steamPresence(),changed);},[] {return false;}))throw std::runtime_error("Existing Steam observer failed");
  runtime.observeSteam(observer.steamPresence());emit({{"type","controller-ready"},{"nativeBootstrap",nativeBootstrap},{"settingsWritten",false},{"hardware",false}});
  std::string line;
  while(std::getline(std::cin,line)){
   if(line=="quit")break;
   if(line=="status"){const auto s=runtime.snapshot();emit({{"type","status"},{"phase",s.phase},{"pid",s.pid},{"reason",s.reason}});continue;}
   if(nativeBootstrap){const auto request=json::parse(line);const auto type=request.value("type",std::string{});
    if(type=="attach"){auto result=broker.status();result["owner"]="YMCC-native";result["pid"]=GetCurrentProcessId();result["type"]="attach";result["id"]=request["id"];emit(result);}
    else if(type=="refresh"){const auto generation=request.value("generation",0ULL);emit({{"type","refresh-admission"},{"generation",generation},{"accepted",runtime.requestBindingRefresh(generation)},{"actualOsSleep",false}});}
    else if(type=="reply")broker.reply(request.value("connection",std::string{}),request["message"]);
    else throw std::runtime_error("Unexpected UI controller request");
   }
  }
  runtime.setEnabled(false);observer.stop();runtime.shutdown();broker.stop();
  emit({{"type","finished"},{"pid",runtime.snapshot().pid},{"brokerActive",broker.status()["active"]},{"steamKilled",false}});return 0;
 }catch(const std::exception& error){emit({{"type","error"},{"error",error.what()}});return 2;}
}
