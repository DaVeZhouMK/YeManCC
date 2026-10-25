// Exact production enumeration callback and HID metadata bodies are supplied
// by the runner. All COM/HID boundaries below are inert fixture functions.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <new>
#include <string>
#include <vector>
#include "json.hpp"
#include "msi_physical_name_evidence.h"
using json=nlohmann::json;
static bool failNextAllocation=false;
void* operator new(std::size_t n){if(failNextAllocation){failNextAllocation=false;throw std::bad_alloc();}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
enum class Fault{None,Receipt,AfterOpen,AfterCaps,AfterProduct};
static Fault fault=Fault::None;
static bool faultUsed=false;
static unsigned metadataOpened=0,metadataClosed=0,preparsedOpened=0,preparsedClosed=0;
static void armAllocation(Fault at){if(fault==at&&!faultUsed){faultUsed=true;failNextAllocation=true;}}
struct HIDP_CAPS{USHORT UsagePage=1,Usage=5;};using PHIDP_PREPARSED_DATA=void*;
#define HIDP_STATUS_SUCCESS 0x110000
static ULONGLONG fixtureNow=100;
static ULONGLONG fixtureTick(){return ++fixtureNow;}
static HANDLE fixtureOpen(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE){++metadataOpened;SetLastError(111);armAllocation(Fault::AfterOpen);return reinterpret_cast<HANDLE>(77);}
static BOOL fixtureClose(HANDLE){++metadataClosed;SetLastError(222);return TRUE;}
static BOOL fixturePreparsed(HANDLE,PHIDP_PREPARSED_DATA* p){++preparsedOpened;*p=reinterpret_cast<void*>(99);return TRUE;}
static LONG fixtureCaps(PHIDP_PREPARSED_DATA,HIDP_CAPS* p){p->UsagePage=1;p->Usage=5;armAllocation(Fault::AfterCaps);return HIDP_STATUS_SUCCESS;}
static BOOL fixtureFree(PHIDP_PREPARSED_DATA){++preparsedClosed;SetLastError(333);return TRUE;}
static BOOL fixtureProduct(HANDLE,void* p,ULONG){std::wcscpy(static_cast<wchar_t*>(p),L"fixture-product");armAllocation(Fault::AfterProduct);return TRUE;}
#define GetTickCount64 fixtureTick
#define CreateFileW fixtureOpen
#define CloseHandle fixtureClose
#define HidD_GetPreparsedData fixturePreparsed
#define HidP_GetCaps fixtureCaps
#define HidD_FreePreparsedData fixtureFree
#define HidD_GetProductString fixtureProduct
static std::atomic<unsigned> g_msiNameEvidenceFailures{0};
static std::string W2U(const std::wstring& s){return {s.begin(),s.end()};}
static std::string msiGuidToString(const GUID& g){if(fault==Fault::Receipt&&!faultUsed){faultUsed=true;throw std::bad_alloc();}return std::to_string(g.Data1);}
struct DIDEVICEINSTANCEW{DWORD dwSize=sizeof(DIDEVICEINSTANCEW);GUID guidInstance{};DWORD dwDevType=0;wchar_t tszProductName[MAX_PATH]=L"fixture";};
struct DIDEVCAPS{DWORD dwSize=0,dwAxes=6,dwButtons=17,dwPOVs=1;};
struct DIPROPHEADER{DWORD dwSize=0,dwHeaderSize=0,dwObj=0,dwHow=0;};
struct DIPROPDWORD{DIPROPHEADER diph;DWORD dwData=0;};
struct DIPROPGUIDANDPATH{DIPROPHEADER diph;wchar_t wszPath[MAX_PATH]{};};
static constexpr int DIPROP_VIDPID=1,DIPROP_GUIDANDPATH=2,DIPH_DEVICE=0;
static constexpr BOOL DIENUM_CONTINUE=1,DIENUM_STOP=0;
static const struct Capability{DWORD vendorId=0x0DB0,pidDInput=0x1902;const char* mainInterfaceToken="MI_00&COL01";}kMsiClawCapability;
static bool msiPathMentionsAsciiToken(const wchar_t* path,const char* token){std::wstring wide;while(*token)wide.push_back(*token++);return std::wcsstr(path,wide.c_str())!=nullptr;}
struct IDirectInputDevice8W{
 DWORD vid=0x0DB0,pid=0x1902;bool main=true,pathOk=true,createOk=true;unsigned releases=0,id=0;
 HRESULT GetProperty(int property,DIPROPHEADER* header){if(property==DIPROP_VIDPID){reinterpret_cast<DIPROPDWORD*>(header)->dwData=MAKELONG(vid,pid);return S_OK;}
  if(!pathOk)return E_FAIL;std::wcscpy(reinterpret_cast<DIPROPGUIDANDPATH*>(header)->wszPath,main?L"fixture:MI_00&COL01":L"fixture:MI_02&COL03");return S_OK;}
 HRESULT GetCapabilities(DIDEVCAPS* c){c->dwAxes=6;c->dwButtons=17;c->dwPOVs=1;return S_OK;}
 ULONG Release(){++releases;return 0;}
};
struct IDirectInput8W{IDirectInputDevice8W devices[6];HRESULT CreateDevice(const GUID& g,IDirectInputDevice8W** out,void*){auto& d=devices[g.Data1];if(!d.createOk)return E_FAIL;*out=&d;return S_OK;}};
static bool g_inputHostMsiStageRequested=false,failCommandLog=false;
static unsigned commandLogRows=0;
static void appendNativeLifecycleLog(const char*,json){if(failCommandLog)throw std::bad_alloc();++commandLogRows;}
#include "production-extracted.h"
static json checks=json::array(),witnesses=json::array();
static void check(const char* name,bool pass){checks.push_back({{"name",name},{"pass",pass}});}
struct Outcome{bool threw=false;int selected=-1;unsigned opened=0,closed=0,prep=0,freed=0,failures=0;json candidateRows=json::array();};
template<class Context,class Callback>static Outcome exercise(bool logging,Fault injected,unsigned shape,Callback callback){
 IDirectInput8W di;for(unsigned i=0;i<6;++i)di.devices[i].id=i;
 // Nonvendor, wrong PID, missing path, wrong interface, then two valid mains.
 di.devices[0].vid=0x1234;di.devices[1].pid=0x1901;di.devices[2].pathOk=false;di.devices[3].main=false;
 if(shape==1){di.devices[4].pathOk=false;di.devices[5].main=false;}
 if(shape==2){di.devices[4].createOk=false;}
 Context c;c.di=&di;c.catalogueEnabled=logging;c.nameCapture={{"metadataCaptureId","fixture-capture"}};
 fixtureNow=100;fault=injected;faultUsed=false;metadataOpened=metadataClosed=preparsedOpened=preparsedClosed=0;g_msiNameEvidenceFailures=0;
 bool threw=false;for(unsigned i=0;i<6;++i){DIDEVICEINSTANCEW inst;inst.guidInstance.Data1=i;BOOL next=DIENUM_CONTINUE;
  try{next=callback(&inst,&c);}catch(...){threw=true;break;}if(next==DIENUM_STOP)break;}
 failNextAllocation=false;fault=Fault::None;
 return {threw,c.pad?static_cast<int>(c.pad->id):-1,metadataOpened,metadataClosed,preparsedOpened,preparsedClosed,g_msiNameEvidenceFailures.load(),c.candidates};
}
static json evidence(const Outcome& o){return {{"exceptionEscaped",o.threw},{"selected",o.selected},{"metadataOpened",o.opened},{"metadataClosed",o.closed},{"preparsedOpened",o.prep},{"preparsedClosed",o.freed},{"diagnosticFailures",o.failures}};}
int main(){
 for(unsigned shape=0;shape<3;++shape){
  const auto off=exercise<after::Ctx>(false,Fault::None,shape,after::callback);
  const auto on=exercise<after::Ctx>(true,Fault::None,shape,after::callback);
  const int expected=shape==1?-1:shape==2?5:4;
  check("logging-on-off-keeps-first-admitted-source",!off.threw&&!on.threw&&off.selected==expected&&on.selected==expected);
  check("logging-off-does-not-open-metadata-handles",off.opened==0&&off.closed==0);
  check("normal-logging-on-closes-every-metadata-handle",on.opened==on.closed&&on.prep==on.freed);
  const auto oldOn=exercise<before::Ctx>(true,Fault::None,shape,before::callback);
  check("normal-diagnostic-payload-keeps-all-prior-fields-and-values",oldOn.candidateRows==on.candidateRows);
 }
 for(auto injected:{Fault::Receipt,Fault::AfterOpen,Fault::AfterCaps,Fault::AfterProduct}){
  const auto off=exercise<after::Ctx>(false,injected,0,after::callback);
  check("detail-off-does-not-enter-optional-fault-path-or-change-source",!off.threw&&off.selected==4&&off.opened==0&&off.failures==0);
  const auto old=exercise<before::Ctx>(true,injected,0,before::callback);
  const auto current=exercise<after::Ctx>(true,injected,0,after::callback);
  check("baseline-diagnostic-fault-escapes-enumeration-before-selection",old.threw&&old.selected==-1);
  check("current-diagnostic-fault-does-not-block-source-selection",!current.threw&&current.selected==4&&current.failures>0);
  check("current-fault-keeps-metadata-resources-balanced",current.opened==current.closed&&current.prep==current.freed);
  witnesses.push_back({{"fault",static_cast<int>(injected)},{"before",evidence(old)},{"after",evidence(current)}});
 }
 // Direct helper contract: even a failed log write preserves the caller's OS error.
 for(auto injected:{Fault::None,Fault::AfterOpen,Fault::AfterCaps,Fault::AfterProduct}){
  json row={{"productUtf16",json::array({65})}};fault=injected;faultUsed=false;metadataOpened=metadataClosed=preparsedOpened=preparsedClosed=0;
  SetLastError(4321);bool threw=false;try{after::msiDinputEnumerationHidMetadata(row,L"fixture");}catch(...){threw=true;}
  const DWORD error=GetLastError();failNextAllocation=false;fault=Fault::None;
  check("metadata-helper-is-exception-isolated-and-preserves-last-error",!threw&&error==4321&&metadataOpened==metadataClosed&&preparsedOpened==preparsedClosed);
 }
 const json commandRow={{"command","SUBMIT_FRAME"},{"ok",true},{"payload","fixture"}};
 for(bool optIn:{false,true})for(bool failing:{false,true}){
  g_inputHostMsiStageRequested=optIn;failCommandLog=failing;
  bool oldThrew=false,newThrew=false;try{before::inputHostCommandResultLog(commandRow);}catch(...){oldThrew=true;}
  try{after::inputHostCommandResultLog(commandRow);}catch(...){newThrew=true;}
  check("command-logger-baseline-exposes-different-failure-semantics-by-opt-in",oldThrew==(failing&&!optIn));
  check("command-logger-current-failure-is-independent-of-logging-opt-in",!newThrew);
 }
 failCommandLog=false;
 // The JSON parameter copy fails before the sink function body; the wrapper
 // must isolate that boundary as well as an exception inside the sink.
 g_inputHostMsiStageRequested=false;failNextAllocation=true;bool copyThrew=false;
 try{after::inputHostCommandResultLog(commandRow);}catch(...){copyThrew=true;}
 failNextAllocation=false;
 check("command-logger-argument-copy-failure-is-also-isolated",!copyThrew);
 unsigned failures=0;for(const auto& c:checks)if(!c["pass"].get<bool>())++failures;
 std::cout<<json({{"checks",checks},{"witnesses",witnesses},{"failures",failures},
  {"scope","exact callback/metadata bodies; fake COM/HID and allocation failures; no live device or control process"}}).dump(2)<<std::endl;return failures?3:0;
}
