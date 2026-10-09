#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <iostream>
#include "steam_live_cdp.h"
using namespace ymcc::steamlive;
static void expect(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);std::cout<<"PASS "<<message<<"\n";}
static Json target() {return {{"type","page"},{"title","SharedJSContext"},{"url","https://steamloopback.host/index.html"},{"webSocketDebuggerUrl","ws://127.0.0.1:8080/devtools/page/ABC-123"}};}
static constexpr unsigned short testPort=8080;
static const Endpoint testEndpoint{1,testPort,L"127.0.0.1"};
static bool rejected(const Json& list) {try {targetPath(list,testEndpoint);return false;}catch(...){return true;}}
int main(int argc,char** argv) {try {
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
 std::cout<<"STEAM_LIVE_TRANSPORT_OK\n";return 0;
}catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}}
