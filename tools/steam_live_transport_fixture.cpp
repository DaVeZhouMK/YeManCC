#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <iostream>
#include <future>
#include <thread>
#include "steam_live_cdp.h"
using namespace ymcc::steamlive;
static void expect(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);std::cout<<"PASS "<<message<<"\n";}
static Json target() {return {{"type","page"},{"title","SharedJSContext"},{"url","https://steamloopback.host/index.html"},{"webSocketDebuggerUrl","ws://127.0.0.1:8080/devtools/page/ABC-123"}};}
static constexpr unsigned short testPort=8080;
static const Endpoint testEndpoint{1,testPort,L"127.0.0.1"};
static bool rejected(const Json& list) {try {targetPath(list,testEndpoint);return false;}catch(...){return true;}}
int main(int argc,char** argv) {try {
 for(const char* op:{"desktopMouse.get","desktopMouse.set"}){
   std::promise<void> locked,release;auto ready=locked.get_future();auto done=release.get_future();
   std::thread owner([&]{std::lock_guard guard(transportMutex);locked.set_value();done.wait_for(std::chrono::milliseconds(500));});ready.wait();
   const auto started=std::chrono::steady_clock::now();
   const auto receipt=run(L"S:\\YMCC-owned-offline-fixture",{{"operation",op}});
   const auto elapsed=std::chrono::steady_clock::now()-started;release.set_value();owner.join();
   expect(!receipt.value("ok",true)&&receipt.value("reason",std::string{})=="live-busy"&&!receipt.value("mutated",true)&&elapsed<std::chrono::milliseconds(200),"desktop mouse never queues behind another Steam transport owner");
 }

 expect(isRunningLinkRead({{"operation","sleep.inspect"},{"runningLinkRead",true}}),"running link inspection has typed read-only admission");
 expect(isRunningLinkRead({{"operation","sleep.session.inspect"},{"runningLinkRead",true}}),"independent running link account bind is a read");
 for(const auto* op:{"sleep.reconcile","sleep.input-resume","settings.set","menu.button","overlay.set","unknown"})
   expect(!isRunningLinkRead({{"operation",op},{"runningLinkRead",true}}),"running-link flag cannot admit a mutator or unknown operation");
 for(const auto& value:Json::array({false,0,1,"true",nullptr,Json::object(),Json::array()}))
   expect(!isRunningLinkRead({{"operation","sleep.inspect"},{"runningLinkRead",value}}),"malformed or off running-link flags retain ordinary transport");
 expect(!isRunningLinkRead({{"operation","sleep.inspect"}}),"ordinary sleep inspection retains its original transport path");
 expect(!isRunningLinkRead({{"operation",1},{"runningLinkRead",true}}),"invalid operation cannot request background admission");
 auto t=target();expect(targetPath(Json::array({t}),testEndpoint)=="/devtools/page/ABC-123","only verified local SharedJSContext selected");
 for(const auto* ws:{"ws://evil.example/devtools/page/A","ws://127.0.0.1:9090/devtools/page/A","ws://127.0.0.1:8080/devtools/page/A?url=evil","ws://127.0.0.1:8080/devtools/page/../../json"}) {t=target();t["webSocketDebuggerUrl"]=ws;expect(rejected(Json::array({t})),"untrusted websocket URL refused");}
 t=target();expect(rejected(Json::array({t,t})),"ambiguous shared context refused");t["title"]="OtherWindow";expect(rejected(Json::array({t})),"other UI page never selected");
 expect(canDefer({{"ok",false},{"reason","live-debug-unavailable"},{"mutated",false}}),"disabled debugging may defer");
 expect(!canDefer({{"ok",false},{"reason","live-debug-unavailable"},{"mutated",true}}),"uncertain mutation cannot defer");
 expect(!canDefer({{"ok",false},{"reason","live-editor-busy"},{"mutated",false}}),"dirty editor cannot defer");


 for(const unsigned short port:std::vector<unsigned short>{7777,39217,65000}) {
   t=target();t["webSocketDebuggerUrl"]="ws://127.0.0.1:"+std::to_string(port)+"/devtools/page/ABC-123";
   expect(targetPath(Json::array({t}),Endpoint{987,port,L"127.0.0.1"})=="/devtools/page/ABC-123","dynamic Steam-owned port is accepted, not fixed to this PC");
 }
 t=target();t["webSocketDebuggerUrl"]="ws://[::1]:39217/devtools/page/ABC-123";
 expect(targetPath(Json::array({t}),Endpoint{987,39217,L"::1"})=="/devtools/page/ABC-123","IPv6 loopback target parsing supported");


 expect(listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\steamwebhelper.exe",L"S:\\Steam\\steam.exe",4000,4000),"sleep discovery binds a helper to the exact expected Steam PID");
 expect(!listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\steamwebhelper.exe",L"S:\\Steam\\steam.exe",5000,4000),"same-root helper from another Steam process cannot serve the captured sleep session");
 expect(listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\steamwebhelper.exe",L"S:\\Steam\\steam.exe",5000,0),"ordinary transport retains existing unbound PID behavior");
 expect(!listenerProcessBound(L"S:\\Steam",L"S:\\Steam-other\\steamwebhelper.exe",L"S:\\Steam\\steam.exe",4000,4000),"prefix lookalike is not a verified UI helper");
 expect(!listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\fake.exe",L"S:\\Steam\\steam.exe",4000,4000),"another executable cannot provide an account witness");
 expect(!listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\steamwebhelper.exe",L"S:\\Steam\\game.exe",4000,4000),"foreign parent is rejected even with expected PID metadata");
 if(argc==4 && std::string(argv[1])=="--live-overlay-roundtrip") {
   const std::string root=argv[2];const std::wstring steam(root.begin(),root.end());const DWORD account=static_cast<DWORD>(std::stoul(argv[3]));
   const auto before=run(steam,{{"operation","overlay.get"},{"account",account}});
   if(!before.value("ok",false))throw std::runtime_error("original overlay read failed; no mutation");
   const bool enabled=before.value("value",1)!=0;
   const auto change=run(steam,{{"operation","overlay.set"},{"account",account},{"enabled",!enabled}});
   // Restore original even if change returned an uncertain acknowledgement.
   const auto restore=run(steam,{{"operation","overlay.set"},{"account",account},{"enabled",enabled}});
   const auto after=run(steam,{{"operation","overlay.get"},{"account",account}});
   std::cout<<Json{{"before",before},{"change",change},{"restore",restore},{"after",after}}.dump(2)<<"\n";
   if(!change.value("ok",false)||!restore.value("ok",false)||!after.value("ok",false)||after.value("value",-1)!=before.value("value",-2))return 3;
 }
 if(argc==6 && std::string(argv[1])=="--live-readonly") {
   const std::string root=argv[2],path=argv[4],type=argv[5];const DWORD account=static_cast<DWORD>(std::stoul(argv[3]));
   const std::wstring steam(root.begin(),root.end());
   const auto overlay=run(steam,{{"operation","overlay.get"},{"account",account}});
   const auto mouse=run(steam,{{"operation","mouse.get"},{"account",account},{"path",path},{"controllerType",type}});
   std::cout<<Json{{"overlay",overlay},{"mouse",mouse},{"hardwareWrites",0}}.dump(2)<<"\n";
   if(!overlay.value("ok",false)||!mouse.value("ok",false))return 2;
 }
 if(argc==4 && std::string(argv[1])=="--live-api-contract-read") {
   const std::string root=argv[2];const DWORD account=static_cast<DWORD>(std::stoul(argv[3]));
   const std::wstring steam(root.begin(),root.end());
   const auto result=run(steam,{{"operation","diagnostics.apps.contract.read"},{"account",account}});
   std::cout<<Json{{"apiContractRead",result},{"hardwareWrites",0}}.dump(2)<<"\n";
   if(!result.value("ok",false)||result.value("mutated",true)||!result.value("methodsNotCalled",false))return 2;
 }
 if(argc==4 && std::string(argv[1])=="--live-api-surface-read") {
   const std::string root=argv[2];const DWORD account=static_cast<DWORD>(std::stoul(argv[3]));
   const std::wstring steam(root.begin(),root.end());
   const auto result=run(steam,{{"operation","diagnostics.apps.api.read"},{"account",account}});
   std::cout<<Json{{"apiSurfaceRead",result},{"hardwareWrites",0}}.dump(2)<<"\n";
   if(!result.value("ok",false)||result.value("mutated",true)||!result.value("apiValuesNotRead",false))return 2;
 }
 if((argc==4||argc==5) && std::string(argv[1])=="--live-running-read") {
   const std::string root=argv[2];const DWORD account=static_cast<DWORD>(std::stoul(argv[3]));
   const std::wstring steam(root.begin(),root.end());
   Json ids=Json::array();if(argc==5){const auto appid=static_cast<std::uint32_t>(std::stoull(argv[4]));if(!appid)throw std::runtime_error("invalid appid");ids.push_back(appid);}
   const auto result=run(steam,{{"operation","sleep.inspect"},{"account",account},{"appids",ids},{"runningLinkRead",true},{"nativeFocusFeedbackRead",true}});
   std::cout<<Json{{"runningLinkRead",result},{"hardwareWrites",0}}.dump(2)<<"\n";
   if(!result.value("ok",false)||result.value("mutated",true))return 2;
 }
 if((argc==4||argc==5) && std::string(argv[1])=="--live-app-lifetime-read") {
   const std::string root=argv[2];const DWORD account=static_cast<DWORD>(std::stoul(argv[3]));
   const std::wstring steam(root.begin(),root.end());
   Json ids=Json::array();if(argc==5){const auto appid=static_cast<std::uint32_t>(std::stoull(argv[4]));if(!appid)throw std::runtime_error("invalid appid");ids.push_back(appid);}
   const auto result=run(steam,{{"operation","sleep.inspect"},{"account",account},{"appids",ids},{"runningLinkRead",true},{"nativeFocusFeedbackRead",true},{"nativeAppLifetimeFeedbackRead",true}});
   std::cout<<Json{{"appLifetimeRead",result},{"hardwareWrites",0}}.dump(2)<<"\n";
   if(!result.value("ok",false)||result.value("mutated",true))return 2;
 }
 std::cout<<"STEAM_LIVE_TRANSPORT_OK\n";return 0;
}catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}}
