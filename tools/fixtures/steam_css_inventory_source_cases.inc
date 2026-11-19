#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <thread>
#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <json.hpp>
#include "steam_css_inventory_source_fixture.inc"
namespace CssInventorySourceCases {
using Query=LONG(NTAPI*)(ULONG,void*,ULONG,ULONG*);
inline Query query=nullptr;
inline std::string selected;
inline unsigned queries=0,sharedCalls=0,readFactories=0;
inline bool sourceRevoked=false;
inline LONG NTAPI observedQuery(ULONG kind,void* buffer,ULONG bytes,ULONG* returned){
 ++queries;const auto result=query(kind,buffer,bytes,returned);
 if(selected=="revoked-inventory"&&!sourceRevoked){sourceRevoked=true;CssSourceFixture::owner->suspendReads();}
 return result;
}
inline FARPROC WINAPI resolve(HMODULE h,LPCSTR n){
 const auto actual=::GetProcAddress(h,n);
 if(n&&reinterpret_cast<UINT_PTR>(n)>65535&&std::string_view(n)=="NtQuerySystemInformation"){
  query=reinterpret_cast<Query>(actual);return actual?reinterpret_cast<FARPROC>(observedQuery):nullptr;
 }
 return actual;
}
}
#define GetProcAddress CssInventorySourceCases::resolve
#include "steam_cssloader_inventory.h"
#undef GetProcAddress
namespace CssInventorySourceCases {
using Json=nlohmann::json;
inline Json checks=Json::array();
inline void check(bool value,const char* name){checks.push_back({{"name",name},{"pass",value}});if(!value)throw std::runtime_error(name);}
template<class B,class Reader,class Factory>void serve(B& b,const ymcc::steamsession::Session& s,bool allowed,const Reader& read,const Factory& factory){
 if constexpr(requires{b.serve(s,allowed,read,factory);})b.serve(s,allowed,read,factory);
 else b.serve(s,allowed,read);
}
struct View {
 HANDLE mapping=nullptr;DWORD* data=nullptr;
 ~View(){if(data)UnmapViewOfFile(data);if(mapping)CloseHandle(mapping);}
};
inline int entry(int argc,wchar_t** argv){
 const std::vector<std::string> names={"normal-inventory","no-owner-inventory","revoked-inventory","normal-shared","no-owner-shared","revoked-shared","held-shared-drain","disabled"};
 if(argc!=4||std::wstring_view(argv[1])!=L"--css-source"||std::wstring_view(argv[2])!=L"--case"){
  std::cout<<Json{{"status","SELECTION_REJECTED"},{"casesStarted",0}}.dump()<<std::endl;return 2;}
 const std::wstring input=argv[3];selected.assign(input.begin(),input.end());
 if(std::find(names.begin(),names.end(),selected)==names.end()){
  std::cout<<Json{{"status","SELECTION_REJECTED"},{"casesStarted",0}}.dump()<<std::endl;return 2;}
 DWORD initialBeforeWarmup=0,before=0,after=0;bool drainedWhileHeld=false;std::atomic<bool> drained=false;std::thread drainer;
 struct Join{std::thread& thread;~Join(){if(thread.joinable())thread.join();}}join{drainer};
 try{
  check(GetProcessHandleCount(GetCurrentProcess(),&initialBeforeWarmup)!=FALSE,"actual pre-initialization handles");
  {ymcc::steamcssinventory::Bridge initialization;check(initialization.initialize(),"one own resource-only channel initialization before steady baseline");}
  check(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"actual warmed initial handles");
  Json result;
  {
   const bool noOwner=selected.rfind("no-owner",0)==0,shared=selected.find("shared")!=std::string::npos,allowed=selected!="disabled";
   CssSourceFixture::start(noOwner);
   ymcc::steamcssinventory::Bridge bridge;check(bridge.initialize(),"actual original protected inventory mapping initialized");
   View view;view.mapping=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,bridge.name().c_str());check(view.mapping!=nullptr,"own actual mapping opened");
   constexpr DWORD total=(32+128*5)*sizeof(DWORD)+65536*2,payload=(32+128*5)*sizeof(DWORD);
   view.data=static_cast<DWORD*>(MapViewOfFile(view.mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,total));check(view.data!=nullptr,"own actual mapping view");
   auto* v=view.data;check(v[0]==0x59435049&&v[1]==2,"unchanged original protocol header");
   const std::string body="{\"operation\":\"fixture-read-only\"}";v[10]=shared?1:0;v[11]=shared?static_cast<DWORD>(body.size()):0;
   if(shared)CopyMemory(reinterpret_cast<char*>(v)+payload,body.data(),body.size());
   InterlockedExchange(reinterpret_cast<volatile LONG*>(v+2),37);
   const auto callback=[&](const Json& request,const std::function<bool()>& current){
    ++sharedCalls;check(current()&&request==Json{{"operation","fixture-read-only"}},"real Bridge parsed original request and current");
    if(selected=="revoked-shared")CssSourceFixture::owner->suspendReads();
    if(selected=="held-shared-drain"){
     drainer=std::thread([&]{CssSourceFixture::owner->drain();drained.store(true);});
     const auto end=GetTickCount64()+1000;while(CssSourceFixture::owner->read().current()&&GetTickCount64()<end)Sleep(1);
     Sleep(50);drainedWhileHeld=drained.load();
    }
    return Json{{"ok",true},{"mutated",false}}; // Declared read-only terminal; no network or Steam application.
   };
   const auto factory=[]{++readFactories;return CssSourceFixture::read();};
   ymcc::steamsession::Session steam{L"fixture-only",GetCurrentProcessId(),42,17};
   serve(bridge,steam,allowed,callback,factory);
   if(drainer.joinable())drainer.join();
   result={{"reply",v[3]},{"success",v[4]},{"error",v[9]},{"rows",v[5]},{"providerPid",v[6]},{"payloadBytes",v[12]}};
   if(noOwner&&!shared)check(queries==0,"nonowner rejected before real inventory query");
   if(noOwner&&shared)check(sharedCalls==0,"nonowner rejected before shared callback");
   if(selected=="revoked-inventory")check(sourceRevoked&&queries>0&&v[4]==0,"revoked actual inventory cannot publish success");
   if(selected=="revoked-shared")check(sharedCalls==1&&v[4]==0,"revoked shared callback cannot publish success");
   if(selected=="held-shared-drain")check(!drainedWhileHeld&&drained.load()&&v[4]==0,"actual shared read holds source until callback returns");
   if(selected=="normal-inventory")check(queries>0&&sharedCalls==0&&v[4]==1&&v[9]==0,"owned normal inventory preserves original success");
   if(selected=="normal-shared")check(queries==0&&sharedCalls==1&&v[4]==1&&v[9]==0,"owned normal shared read preserves original success without inventory");
   if(!allowed)check(readFactories==0&&queries==0&&sharedCalls==0&&v[4]==0,"disabled performs no source read query or callback");
   if(noOwner)check(v[4]==0&&v[6]==0&&v[12]==0,"nonowner reply remains unavailable");
   check(v[3]==37,"original serial completion published once");
  }
  CssSourceFixture::owner.reset();CssSourceFixture::blocker.reset();
  check(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&after==before,"all actual source and bridge handles returned");
  std::cout<<Json{{"status","PASS"},{"selection",selected},{"casesStarted",1},{"checks",checks},{"queries",queries},{"sharedCalls",sharedCalls},{"sourceFactories",readFactories},{"reply",result},{"initialBeforeWarmup",initialBeforeWarmup},{"beforeHandles",before},{"afterHandles",after},{"realSteamOperations",0},{"terminalSharedReadDeclared",true},{"sourceReady",false}}.dump()<<std::endl;return 0;
 }catch(const std::exception& e){if(drainer.joinable())drainer.join();CssSourceFixture::owner.reset();CssSourceFixture::blocker.reset();std::cout<<Json{{"status","FAIL"},{"selection",selected},{"error",e.what()},{"checks",checks},{"queries",queries},{"sharedCalls",sharedCalls},{"initialBeforeWarmup",initialBeforeWarmup},{"beforeHandles",before},{"afterHandles",after},{"sourceReady",false}}.dump()<<std::endl;return 1;}
}
}
int wmain(int argc,wchar_t** argv){return CssInventorySourceCases::entry(argc,argv);}
