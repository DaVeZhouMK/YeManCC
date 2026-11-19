#define main canonical_main
#include "Canonical.observed.cpp"
#undef main
#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <unordered_map>
using DWORD=unsigned long;using UINT=unsigned;using WORD=unsigned short;using SHORT=short;using HRESULT=long;using LPCCH=const char*;using LPWSTR=wchar_t*;
constexpr HRESULT S_OK=0;constexpr unsigned CP_UTF8=65001;constexpr DWORD ERROR_NO_UNICODE_TRANSLATION=1113;
#define SUCCEEDED(hr) ((hr)>=0)
extern "C" __declspec(dllimport) void* __stdcall GetCurrentThread();
extern "C" __declspec(dllimport) int __stdcall QueryThreadCycleTime(void*,unsigned long long*);
extern "C" __declspec(dllimport) int __stdcall MultiByteToWideChar(UINT,DWORD,LPCCH,int,LPWSTR,int);
extern "C" __declspec(dllimport) void __stdcall SetLastError(DWORD);
extern "C" __declspec(dllimport) int __stdcall WideCharToMultiByte(UINT,DWORD,const wchar_t*,int,char*,int,const char*,int*);
constexpr WORD XINPUT_GAMEPAD_A=0x1000,XINPUT_GAMEPAD_B=0x2000,XINPUT_GAMEPAD_X=0x4000,XINPUT_GAMEPAD_Y=0x8000,XINPUT_GAMEPAD_LEFT_SHOULDER=0x100,XINPUT_GAMEPAD_RIGHT_SHOULDER=0x200,XINPUT_GAMEPAD_BACK=0x20,XINPUT_GAMEPAD_START=0x10,XINPUT_GAMEPAD_LEFT_THUMB=0x40,XINPUT_GAMEPAD_RIGHT_THUMB=0x80,XINPUT_GAMEPAD_DPAD_UP=1,XINPUT_GAMEPAD_DPAD_DOWN=2,XINPUT_GAMEPAD_DPAD_LEFT=4,XINPUT_GAMEPAD_DPAD_RIGHT=8;
constexpr unsigned XINPUT_GAMEPAD_TRIGGER_THRESHOLD=30;
#include "baseline.inc"
#include "candidate.inc"
#include "oldControl.inc"
#include "negative.inc"
static unsigned extraChecks=0;
static void extra(bool ok,const char* text){++extraChecks;if(!ok)throw std::runtime_error(text);}
static void sourceNow(){static unsigned long long seq=0;mockReadStarted=mockTick-1;mockSourceWindow={++seq,mockReadStarted,mockTick,mockPower,g_xinputTopologyEpoch.load(),true};}
static XINPUT_GAMEPAD makePad(unsigned n){XINPUT_GAMEPAD p{};p.wButtons=static_cast<WORD>(n*1027);p.bLeftTrigger=static_cast<std::uint8_t>(n);p.bRightTrigger=static_cast<std::uint8_t>(255-n);p.sThumbLX=static_cast<SHORT>(n*751);p.sThumbLY=static_cast<SHORT>(n*137);p.sThumbRX=static_cast<SHORT>(-32768+n);p.sThumbRY=static_cast<SHORT>(32767-n);return p;}
struct Trace {std::vector<std::pair<int,std::wstring>> posts;std::vector<int> resolutions;std::vector<std::string> reads,scopes,diagnostics;unsigned calls=0,conversions=0;bool committed=false,connected=false;XINPUT_GAMEPAD pad{};std::string error;};
#define TAKE_TRACE(N) {try{N::publication(pub,live,pad,source,mockSourceWindow,mask);}catch(const std::exception&e){t.error=e.what();}t.posts=N::posts;t.resolutions=N::resolutions;t.reads=N::reads;t.scopes=N::scopeEvents;t.diagnostics=N::diagnosticEvents;t.calls=N::postCalls;t.conversions=N::conversionApiCalls;t.committed=N::g_gamepadStatePublished;t.connected=N::g_publishedPadConnected;t.pad=N::g_publishedPad;}
static Trace call(unsigned arm,bool pub,bool live,const XINPUT_GAMEPAD&pad,const std::string&source,unsigned mask){Trace t;if(arm==0)TAKE_TRACE(baseline)else if(arm==1)TAKE_TRACE(candidate)else TAKE_TRACE(negative)return t;}
static void same(const Trace&a,const Trace&b){extra(a.posts==b.posts,"entire UTF16 event wire and receiver order");extra(a.resolutions==b.resolutions,"current renderer resolution order");extra(a.reads==b.reads,"fresh getter call order");extra(a.scopes==b.scopes&&a.diagnostics==b.diagnostics,"original diagnostic event order");extra(a.calls==b.calls&&a.conversions==b.conversions,"same post/conversion attempts");extra(a.committed==b.committed&&a.connected==b.connected&&!memcmp(&a.pad,&b.pad,sizeof(a.pad)),"published ownership state");extra(a.error==b.error,"same nonallocation exception result");}
#define CLEAR_TRACE(N) N::posts.clear();N::resolutions.clear();N::reads.clear();N::scopeEvents.clear();N::diagnosticEvents.clear();N::postCalls=N::conversionApiCalls=0
#define SET_PORT(N) N::oem=n%256;N::driverState=static_cast<int>(n%5);N::driverNotice=static_cast<int>(n%3);N::driverResult=wide?std::string("驱动😀/\\\"\n")+std::string(257,'x'):std::to_string(n%7);N::driverSession=n%9?"session":std::string("nul\0tail",8);N::g_externalPriorityRevision.store(n);N::g_backendInstallPrompt=n%2!=0;N::g_xinputConnectedMask=n%16;N::g_xinputPrimarySlot=n%4;N::g_xinputSpecialButton=n%2!=0;N::rumble=n%3!=0;N::lighting=n%3==0;N::externalPriority={{"active",n%2!=0},{"label",wide?"外设😀":"pad"}};for(unsigned k=0;k<4;++k)N::g_xinputPads[k]=n%2?pad:makePad(n+k)
static void differential(){
 reset();baseline::portReset(4);candidate::portReset(4);
 for(unsigned n=0;n<6000;++n){
  if(n%101==0){reset();baseline::portReset(1+static_cast<int>(n%4));candidate::portReset(1+static_cast<int>(n%4));}
  mockTick+=8;sourceNow();const auto pad=makePad(n);const bool wide=n%7==0;SET_PORT(baseline);SET_PORT(candidate);
  g_inputHostFaultHoldsStart.store(n%29==0);g_inputHostRecoveryFallbackState.store(static_cast<int>(n%4));g_externalPriorityVirtualSuspended.store(n%31==0);mockAlive=n%37!=0;mockSourceWindow.readContextStable=n%41!=0;g_powerLifecycle.store(n%43==0?PowerLifecycle::Resuming:PowerLifecycle::Ready);
  if(n%101==1){const auto r=g_virtualStartProtection.admit("elite",mockTick);extra(r.accepted,"protection admits original request");}
  if(n%101==4){const auto v=g_virtualStartProtection.view(mockTick);g_virtualStartProtection.beginVerification(v.requestId,"success",mockTick);}
  if(n%101==6){const auto v=g_virtualStartProtection.view(mockTick);g_virtualStartProtection.finishVerification(v.requestId,"success",mockTick);}
  if(n%97==0){baseline::throwAtPost=candidate::throwAtPost=1;baseline::g_gamepadStatePublished=candidate::g_gamepadStatePublished=false;}else baseline::throwAtPost=candidate::throwAtPost=-1;
  if(n%89==0)baseline::failConversionAt=candidate::failConversionAt=1;else baseline::failConversionAt=candidate::failConversionAt=-1;
  if(n%83==0){baseline::driverResult=candidate::driverResult=std::string("\xC0\x80",2);baseline::g_gamepadStatePublished=candidate::g_gamepadStatePublished=false;}
  const bool pub=n%11!=0,live=n%5!=0;const std::string source=wide?"物理😀":"physical-current";const unsigned mask=n%16;
  CLEAR_TRACE(baseline);CLEAR_TRACE(candidate);const auto a=call(0,pub,live,pad,source,mask),b=call(1,pub,live,pad,source,mask);same(a,b);
  if(!pub){extra(a.reads.empty()&&a.posts.empty(),"disabled UI lane has no getters/posts");}
  if(a.error.empty()&&a.committed&&pub){
   CLEAR_TRACE(baseline);CLEAR_TRACE(candidate);const auto c=call(0,true,live,pad,source,mask),d=call(1,true,live,pad,source,mask);same(c,d);extra(c.posts.empty(),"unchanged second sample keeps original no-publication gate");
  }
 }
 reset();baseline::portReset(0);candidate::portReset(0);const auto pad=makePad(19);same(call(0,true,true,pad,"hidden",1),call(1,true,true,pad,"hidden",1));extra(baseline::reads.empty()&&baseline::posts.empty(),"no view returns before reads");
 std::cout<<json{{"pass",true},{"checks",extraChecks},{"streamSteps",6000},{"scope","Full publication segment; original45 status cases separate"}}.dump();
}
using PubFn=void(*)(bool,bool,const XINPUT_GAMEPAD&,const std::string&,const ymcc::VirtualOutputSourceWindow&,unsigned);
struct Measure{double wall=0;unsigned long long cycles=0,allocs=0,bytes=0,checksum=0;};
static volatile unsigned long long costSink=0;
static Measure measure(unsigned arm,const std::string&shape){
 reset();baseline::portReset(shape=="hidden-no-view"?0:4);candidate::portReset(shape=="hidden-no-view"?0:4);oldControl::portReset(shape=="hidden-no-view"?0:4);baseline::observing=candidate::observing=oldControl::observing=false;
 const bool wide=shape=="changed-wide",live=shape!="changed-disconnected",changed=shape.starts_with("changed-");unsigned n=0;const auto pad=makePad(17);SET_PORT(baseline);SET_PORT(candidate);SET_PORT(oldControl);sourceNow();
 const PubFn fn=arm==0?baseline::publication:arm==1?candidate::publication:oldControl::publication;
 fn(true,live,pad,"physical-current",mockSourceWindow,1);unsigned long long c0=0,c1=0,sum=0;extra(QueryThreadCycleTime(GetCurrentThread(),&c0)!=0,"cycles begin");allocationRequests=allocationBytes=0;trackAlloc=true;const auto start=std::chrono::steady_clock::now();
 for(unsigned i=0;i<512;++i){const auto next=changed?makePad(i):pad;if(changed){if(arm==0)baseline::g_externalPriorityRevision.store(i+100);else if(arm==1)candidate::g_externalPriorityRevision.store(i+100);else oldControl::g_externalPriorityRevision.store(i+100);}fn(true,live,next,"physical-current",mockSourceWindow,1);sum+=arm==0?baseline::sink:arm==1?candidate::sink:oldControl::sink;}
 const double wall=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();trackAlloc=false;extra(QueryThreadCycleTime(GetCurrentThread(),&c1)!=0,"cycles end");costSink=sum;return{wall,c1-c0,allocationRequests,allocationBytes,sum};
}
static void cost(){json rows=json::array();for(unsigned campaign=0;campaign<2;++campaign)for(const auto&shape:std::vector<std::string>{"changed-connected","changed-disconnected","changed-wide","unchanged-connected","hidden-no-view"}){for(unsigned a=0;a<3;++a)(void)measure(a,shape);for(unsigned pair=0;pair<11;++pair){Measure a,b,c;if((campaign+pair)%2){b=measure(1,shape);a=measure(0,shape);}else{a=measure(0,shape);b=measure(1,shape);}c=measure(2,shape);rows.push_back({{"campaign",campaign},{"shape",shape},{"pair",pair},{"iterations",512},{"beforeWall",a.wall},{"afterWall",b.wall},{"controlWall",c.wall},{"beforeCycles",a.cycles},{"afterCycles",b.cycles},{"controlCycles",c.cycles},{"beforeAllocations",a.allocs},{"afterAllocations",b.allocs},{"beforeBytes",a.bytes},{"afterBytes",b.bytes}});}}std::cout<<json{{"pass",true},{"rows",rows}}.dump();}
int main(int argc,char**argv){try{if(argc!=2)return 2;const std::string mode=argv[1];if(mode=="canonical")return canonical_main();if(mode=="semantics"){differential();return 0;}if(mode=="cost"){cost();return 0;}if(mode=="negative-field"){reset();baseline::portReset(1);negative::portReset(1);sourceNow();const auto pad=makePad(17);same(call(0,true,true,pad,"physical",1),call(2,true,true,pad,"physical",1));return 0;}return 2;}catch(const std::exception&e){std::cerr<<json{{"error",e.what()},{"checks",extraChecks}}.dump();return 1;}}
