#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <optional>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include "input_edge_evidence.h"
static ymcc::inputEdges::Recorder g_inputEdgeEvents;
static WPARAM expectedWParam=0;
#ifndef YMCC_ROG_FSE_DIAGNOSTICS
#define YMCC_ROG_FSE_DIAGNOSTICS 1
#endif
static std::vector<std::string> events;
static const char* currentCase="";
static unsigned checks=0, failures=0;
static void ck(bool ok,const char* text){++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL %s: %s\n",currentCase,text);}}
static HANDLE device=reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(0x1234));
static HWND g_hwnd=reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x1111));
static HWND g_rawInputSinkHwnd=reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x2222));
alignas(DWORD) static BYTE g_riBuf[1024];
struct Counts {
 std::atomic<unsigned> messages{0},sinkMessages{0},mainMessages{0},headerFailures{0},lastHeaderType{0},hidHeaders{0},keyboardHeaders{0},mouseHeaders{0},negativeCacheHits{0},payloadQueries{0},lastPayloadBytes{0},payloadQueryFailures{0},lastError{0},oversizedPayloads{0},payloadReads{0},payloadReadFailures{0},hidCaptureCalls{0};
} g_rogRawDispatch;
static std::atomic<unsigned> g_c15aHeaderProbes{0},g_c15aHeaderFallbacks{0},g_c15aNegativeHits{0};
static bool observed=true,negative=false,admit=true;
static bool receiptPresent=true,motionRoute=false,detailEnabled=true;
static std::atomic<bool> g_oemRawPathActive{false};
static bool g_oemRawVkAllowed[256]{};
static std::atomic<uint32_t> g_oemKbDownRaw{0};
static bool machineIsRogAllyFamily(){return observed;}
static bool domainLogEnabled(const char* p){ck(std::strcmp(p,"virtual")==0,"diagnostic domain");return detailEnabled;}
static bool tickCostEnabled(){return detailEnabled;}
static bool inputCaptureLegionControllerMotionRoute(){return motionRoute;}
static std::optional<unsigned long long> legionMotionMessageReceiptTick(){events.push_back("receipt");return receiptPresent?std::optional<unsigned long long>(987654ULL):std::nullopt;}
struct UiCostWmInputScope {UiCostWmInputScope(){events.push_back("ui+");}~UiCostWmInputScope(){events.push_back("ui-");}};
static constexpr int kGpReadUiWmInput=9;
struct GpReadSourceScope {explicit GpReadSourceScope(int n){ck(n==9,"read source");events.push_back("gp+");}~GpReadSourceScope(){events.push_back("gp-");}};
static void inputEdgeObserveRaw(const RAWINPUT& r){ck(r.header.hDevice==device,"edge packet device");events.push_back("edge");}
static void keyboardModifierObserveRaw(const RAWINPUT& r){ck(r.data.keyboard.VKey==65,"modifier packet bytes");events.push_back("modifier");}
static bool hidHideRawInputNegativeCacheHit(HANDLE h){ck(h==device,"negative device");events.push_back("negative");return negative;}
static void uiCostNoteWmInputFiltered(){events.push_back("filtered");}
static void gamepadEval(){events.push_back("eval");}
static void hidHideObserveRawInputDevice(HANDLE h){ck(h==device,"identity device");events.push_back("identity");}
static void physicalHidCaptureRawInput(RAWINPUT* r,std::optional<unsigned long long> at){ck(r->data.hid.dwSizeHid==3&&r->data.hid.dwCount==1&&r->data.hid.bRawData[0]==0x5a,"physical payload bytes");ck(receiptPresent?(at&&*at==987654ULL):!at,"receipt timestamp transfer");events.push_back("physical");}
static void rogCaptureRawInput(RAWINPUT* r){ck(r->data.hid.bRawData[0]==0x5a,"ROG payload");events.push_back("rog");}
static bool oemRawKbAdmitDevice(HANDLE h){ck(h==device,"OEM device");events.push_back("admit");return admit;}
static uint32_t oemBackHookVkBit(DWORD k){ck(k==65,"OEM vk");events.push_back("bit");return 4;}
static void oemBackHookRecompute(){events.push_back("recompute");}
struct Reply {UINT result;UINT size;DWORD error;};
static RAWINPUTHEADER header{};
static RAWINPUT packet{};
static UINT headerResult=sizeof(RAWINPUTHEADER);
static std::vector<Reply> queries,reads;
static size_t qi=0,ri=0;
static DWORD errorValue=0;
static unsigned expectedReadFailures=0,expectedQueryFailures=0,expectedOversized=0,expectedLastError=0,expectedLastBytes=0,errorCalls=0;
static bool hasReceipt(){for(const auto& e:events)if(e=="receipt")return true;return false;}
static UINT WINAPI fakeRaw(HRAWINPUT h,UINT cmd,LPVOID data,PUINT size,UINT hs){
 ck(hasReceipt(),"receipt before every raw API");ck(h==reinterpret_cast<HRAWINPUT>(static_cast<UINT_PTR>(0x3333)),"packet handle");ck(hs==sizeof(RAWINPUTHEADER),"real header ABI");
 if(cmd==RID_HEADER){events.push_back("header");ck(data!=nullptr&&*size==sizeof(RAWINPUTHEADER),"fixed header read");std::memcpy(data,&header,sizeof(header));return headerResult;}
 ck(cmd==RID_INPUT,"RID_INPUT command");
 if(!data){events.push_back("query");ck(qi<queries.size(),"bounded declared query");if(qi>=queries.size())return ~0u;auto a=queries[qi++];*size=a.size;errorValue=a.error;expectedLastBytes=a.size;expectedQueryFailures+=a.result==~0u;expectedOversized+=a.size>1024;if(a.result==~0u)expectedLastError=a.error;return a.result;}
 events.push_back("read");ck(ri<reads.size(),"bounded declared read");if(ri>=reads.size())return ~0u;auto a=reads[ri++];
 ck(data==g_riBuf,"original buffer");ck(*size<=1024,"read capacity bounded");
 if(a.result!=~0u&&a.result<=*size)std::memcpy(data,&packet,a.result<sizeof(packet)?a.result:sizeof(packet));
 if(qi==0&&ri==1)expectedLastBytes=header.dwSize;
 *size=a.size;errorValue=a.error;if(a.result!=a.size){++expectedReadFailures;expectedLastError=a.result==~0u?a.error:0;}
 return a.result;
}
static DWORD WINAPI fakeError(){++errorCalls;events.push_back("error");return errorValue;}
static LRESULT WINAPI fakeDef(HWND h,UINT m,WPARAM w,LPARAM l){ck(h==g_hwnd&&m==WM_INPUT&&w==expectedWParam&&l==0x3333,"default cleanup args");events.push_back("def");return 77;}
#define GetRawInputData fakeRaw
#define GetLastError fakeError
#define DefWindowProcW fakeDef
#include "dispatcher.generated.h"
#undef GetRawInputData
#undef GetLastError
#undef DefWindowProcW
static void reset(UINT type=RIM_TYPEHID,bool observe=true){
 ck(g_inputEdgeEvents.enable(true),"default diagnostic gate");expectedWParam=0;
 events.clear();negative=false;admit=true;observed=observe;receiptPresent=detailEnabled=true;motionRoute=false;g_oemRawPathActive=false;g_oemKbDownRaw=0;std::memset(g_oemRawVkAllowed,0,sizeof(g_oemRawVkAllowed));std::memset(g_riBuf,0xcc,sizeof(g_riBuf));
 std::memset(&packet,0,sizeof(packet));packet.header.dwType=type;packet.header.dwSize=sizeof(packet);packet.header.hDevice=device;packet.data.hid.dwSizeHid=3;packet.data.hid.dwCount=1;packet.data.hid.bRawData[0]=0x5a;
 if(type==RIM_TYPEKEYBOARD){packet.data.keyboard.VKey=65;packet.data.keyboard.Flags=0;}
 header=packet.header;headerResult=sizeof(header);queries.clear();reads={{sizeof(packet),sizeof(packet),0}};qi=ri=0;errorValue=0;errorCalls=0;expectedReadFailures=expectedQueryFailures=expectedOversized=expectedLastError=expectedLastBytes=0;
#define ZERO(n) g_rogRawDispatch.n=0
 ZERO(messages);ZERO(sinkMessages);ZERO(mainMessages);ZERO(headerFailures);ZERO(lastHeaderType);ZERO(hidHeaders);ZERO(keyboardHeaders);ZERO(mouseHeaders);ZERO(negativeCacheHits);ZERO(payloadQueries);ZERO(lastPayloadBytes);ZERO(payloadQueryFailures);ZERO(lastError);ZERO(oversizedPayloads);ZERO(payloadReads);ZERO(payloadReadFailures);ZERO(hidCaptureCalls);
#undef ZERO
 g_c15aHeaderProbes=0;g_c15aHeaderFallbacks=0;g_c15aNegativeHits=0;
}
static unsigned count(const char* s){unsigned n=0;for(const auto& e:events)if(e==s)++n;return n;}
static void sequence(const std::vector<std::string>& core){
 std::vector<std::string> actual;for(const auto& e:events)if(e!="header"&&e!="negative"&&e!="query"&&e!="read"&&e!="error")actual.push_back(e);
 std::vector<std::string> want={"receipt","ui+","gp+"};want.insert(want.end(),core.begin(),core.end());want.push_back("gp-");want.push_back("ui-");ck(actual==want,"effect order and exact multiplicity");
}
static void dispatch(const std::vector<std::string>& core,LRESULT result=0,WPARAM w=0){
 expectedWParam=w;
 const auto r=gamepadDispatchRawInput(g_hwnd,w,0x3333);ck(r==result,"return/cleanup result");sequence(core);ck(qi==queries.size()&&ri==reads.size(),"all and only scripted API calls");
 ck(g_c15aHeaderProbes==(detailEnabled?1u:0u)&&g_c15aHeaderFallbacks==(detailEnabled&&headerResult!=sizeof(header)?1u:0u),"header cost counters");
#if YMCC_ROG_FSE_DIAGNOSTICS
 if(observed&&detailEnabled){ck(g_rogRawDispatch.messages==1&&g_rogRawDispatch.mainMessages==1&&g_rogRawDispatch.sinkMessages==0,"message counters");ck(g_rogRawDispatch.payloadQueries==qi&&g_rogRawDispatch.payloadReads==ri,"query/read diagnostic counts equal actual calls");ck(g_rogRawDispatch.payloadQueryFailures==expectedQueryFailures&&g_rogRawDispatch.payloadReadFailures==expectedReadFailures,"error counters");ck(g_rogRawDispatch.lastError==expectedLastError&&g_rogRawDispatch.oversizedPayloads==expectedOversized,"last error and oversize counters");ck(g_rogRawDispatch.lastPayloadBytes==expectedLastBytes,"last payload bytes");ck(g_rogRawDispatch.hidCaptureCalls==count("physical"),"capture diagnostic");ck(g_rogRawDispatch.headerFailures==(headerResult!=sizeof(header)?1u:0u),"header failures");ck(g_rogRawDispatch.negativeCacheHits==(negative?1u:0u),"negative diagnostic");}
 else ck(g_rogRawDispatch.messages==0&&g_rogRawDispatch.payloadQueries==0&&g_rogRawDispatch.payloadReads==0&&g_rogRawDispatch.lastPayloadBytes==0&&g_rogRawDispatch.lastError==0,"unobserved diagnostic no mutations");
#else
 ck(g_rogRawDispatch.messages==0&&g_rogRawDispatch.payloadQueries==0&&g_rogRawDispatch.payloadReads==0,"compiled-out diagnostics");
#endif
}
static const std::vector<std::string> hid={"edge","identity","physical","rog","eval"};
static void normal(){for(bool obs:{false,true}){reset(RIM_TYPEHID,obs);dispatch(hid);ck(count("header")==1&&count("query")==0&&count("read")==1,"common 3 to 2 API calls");}reset(RIM_TYPEMOUSE);dispatch({"edge"});
 for(bool receipt:{false,true})for(bool route:{false,true})for(bool detail:{false,true}) {
  reset(RIM_TYPEHID);receiptPresent=receipt;motionRoute=route;detailEnabled=detail;
  bool retain=receipt||route||detail;
#ifdef YMCC_FAULT_INJECT
  retain=true;
#endif
  dispatch(retain?hid:std::vector<std::string>{"edge","identity","rog","eval"});
  ck(count("identity")==1&&count("eval")==1&&count("rog")==1,"ordinary observation canonical evaluation and ROG path retain exact count");
  ck(count("header")==1&&count("read")==1,"original raw packet reads retained");
 }
}
static void keyboard(){reset(RIM_TYPEKEYBOARD);dispatch({"edge","modifier"});for(bool allow:{false,true})for(bool accepts:{false,true}){reset(RIM_TYPEKEYBOARD);g_oemRawPathActive=true;g_oemRawVkAllowed[65]=allow;admit=accepts;std::vector<std::string> v={"edge","modifier"};if(allow)v.push_back("admit");if(allow&&accepts){v.push_back("bit");v.push_back("recompute");v.push_back("eval");}v.push_back("def");dispatch(v,77);ck(g_oemKbDownRaw==(allow&&accepts?4u:0u),"OEM allowed state");}
 for(unsigned i=0;i<4;++i){reset(RIM_TYPEKEYBOARD);g_oemRawPathActive=true;g_oemRawVkAllowed[65]=true;g_oemKbDownRaw=(i==1||i==2)?4u:0u;packet.data.keyboard.Flags=i>=2?RI_KEY_BREAK:0;std::vector<std::string> v={"edge","modifier","admit","bit"};if(i==0||i==2){v.push_back("recompute");v.push_back("eval");}v.push_back("def");dispatch(v,77);ck(g_oemKbDownRaw==(i<2?4u:0u),"down/up/repeat state");}}
static void negativeCase(){reset();negative=true;reads.clear();dispatch({"filtered","eval","def"},77);ck(count("read")==0&&count("query")==0&&g_c15aNegativeHits==1,"negative cache no payload");}
static void headerFallback(){for(UINT hr:{~0u,0u,static_cast<UINT>(sizeof(RAWINPUTHEADER)-1)}){reset();headerResult=hr;queries={{0,sizeof(packet),0}};dispatch(hid);ck(qi==1,"failed/short header original query");}for(UINT size:{0u,1u,static_cast<UINT>(sizeof(RAWINPUTHEADER)-1),1025u,~0u}){reset();header.dwSize=size;queries={{0,sizeof(packet),0}};dispatch(hid);ck(qi==1,"invalid total header requires query");}}
static void queryBounds(){for(UINT size:{0u,1025u,~0u}){reset();header.dwSize=0;queries={{~0u,size,ERROR_INVALID_DATA}};reads.clear();dispatch({});}reset();header.dwSize=1024;packet.header.dwSize=1024;reads={{1024,1024,0}};dispatch(hid);}
static void readFailures(){reset();reads={{~0u,sizeof(packet),ERROR_INVALID_HANDLE}};dispatch({});ck(count("query")==0,"non-buffer error no retry");for(UINT bytes:{0u,1u,static_cast<UINT>(sizeof(RAWINPUTHEADER)-1),static_cast<UINT>(sizeof(packet)-1)}){reset();reads={{bytes,sizeof(packet),0}};dispatch({});}}
static void insufficient(){reset();reads={{~0u,100,ERROR_INSUFFICIENT_BUFFER},{sizeof(packet),sizeof(packet),0}};queries={{0,sizeof(packet),0}};dispatch(hid);ck(qi==1&&ri==2,"single bounded recovery");for(UINT size:{0u,1025u,~0u}){reset();reads={{~0u,100,ERROR_INSUFFICIENT_BUFFER}};queries={{~0u,size,ERROR_INVALID_DATA}};dispatch({});}reset();reads={{~0u,100,ERROR_INSUFFICIENT_BUFFER},{~0u,200,ERROR_INSUFFICIENT_BUFFER}};queries={{0,sizeof(packet),0}};dispatch({});ck(qi==1&&ri==2,"second insufficient does not loop");}
static void finalBounds(){for(bool fallback:{false,true})for(UINT size:{0u,1u,static_cast<UINT>(sizeof(RAWINPUTHEADER)-1),1025u,~0u}){reset();reads={{size,size,ERROR_INVALID_DATA}};if(fallback){reads.insert(reads.begin(),{~0u,100,ERROR_INSUFFICIENT_BUFFER});queries={{0,sizeof(packet),0}};}dispatch({});ck(count("edge")==0&&count("eval")==0,"malformed final size never published");}}

static void mouseOff(){
 for(bool obs:{false,true})for(bool detail:{false,true})
 for(UINT size:{static_cast<UINT>(sizeof(RAWINPUTHEADER)),static_cast<UINT>(sizeof(RAWINPUT)),1024u}){
  reset(RIM_TYPEMOUSE,obs);detailEnabled=detail;ck(g_inputEdgeEvents.enable(false),"disable mouse diagnostics");
  header.dwSize=size;reads.clear();if(obs&&detail)expectedLastBytes=size;
  dispatch({"def"},77);ck(count("header")==1&&count("read")==0&&count("query")==0,"ordinary mouse no payload reads");
  ck(count("edge")==0&&count("eval")==0&&count("identity")==0&&count("physical")==0&&count("rog")==0,"mouse skip has no downstream calls");
 }
}
static void mouseOn(){
 for(WPARAM w:{static_cast<WPARAM>(RIM_INPUT),static_cast<WPARAM>(RIM_INPUTSINK)}){
  reset(RIM_TYPEMOUSE);ck(g_inputEdgeEvents.enabled(),"mouse diagnostic gate enabled");dispatch({"edge"},0,w);
  ck(count("read")==1&&count("edge")==1&&count("def")==0,"mouse diagnostics retain one payload read");
 }
}
static void mouseSwitch(){
 reset(RIM_TYPEMOUSE);bool prior=true;
 for(bool enabled:{false,true,false,true}){
  reset(RIM_TYPEMOUSE);ck(g_inputEdgeEvents.enable(prior),"restore actual gate before transition");
  ck(g_inputEdgeEvents.enable(enabled)&&g_inputEdgeEvents.enabled()==enabled,"actual recorder gate transition");
  if(!enabled){reads.clear();expectedLastBytes=header.dwSize;}
  dispatch(enabled?std::vector<std::string>{"edge"}:std::vector<std::string>{"def"},enabled?0:77);
  ck(count("read")==(enabled?1u:0u),"each message observes current diagnostic gate");prior=enabled;
 }
}
static void mouseCleanup(){
 for(WPARAM w:{static_cast<WPARAM>(RIM_INPUT),static_cast<WPARAM>(RIM_INPUTSINK)}){
  reset(RIM_TYPEMOUSE);ck(g_inputEdgeEvents.enable(false),"disable mouse diagnostics");reads.clear();expectedLastBytes=header.dwSize;
  dispatch({"def"},77,w);ck(count("def")==1&&count("read")==0,"foreground and background default cleanup exactly once");
 }
}
static void mouseFallback(){
 for(UINT hr:{~0u,0u,static_cast<UINT>(sizeof(RAWINPUTHEADER)-1),static_cast<UINT>(sizeof(RAWINPUTHEADER)+1)}){
  reset(RIM_TYPEMOUSE);ck(g_inputEdgeEvents.enable(false),"disable mouse diagnostics");headerResult=hr;queries={{0,sizeof(packet),0}};
  dispatch({"edge"});ck(qi==1&&ri==1&&count("def")==0,"abnormal mouse header preserves original fallback");
 }
 for(UINT size:{0u,1u,static_cast<UINT>(sizeof(RAWINPUTHEADER)-1),1025u,~0u}){
  reset(RIM_TYPEMOUSE);ck(g_inputEdgeEvents.enable(false),"disable mouse diagnostics");header.dwSize=size;queries={{0,sizeof(packet),0}};
  dispatch({"edge"});ck(qi==1&&ri==1&&count("def")==0,"unbounded mouse header preserves original fallback");
 }
}
static void mouseNonMouse(){
 reset(RIM_TYPEKEYBOARD);ck(g_inputEdgeEvents.enable(false),"keyboard edge diagnostics off");
 g_oemRawPathActive=true;g_oemRawVkAllowed[65]=true;
 dispatch({"edge","modifier","admit","bit","recompute","eval","def"},77);
 ck(g_oemKbDownRaw==4&&ri==1,"original keyboard edge ownership and payload retained");
 for(bool receipt:{false,true}){
  reset(RIM_TYPEHID);ck(g_inputEdgeEvents.enable(false),"HID edge diagnostics off");receiptPresent=receipt;detailEnabled=false;
  bool retain=receipt;
#ifdef YMCC_FAULT_INJECT
  retain=true;
#endif
  dispatch(retain?hid:std::vector<std::string>{"edge","identity","rog","eval"});
  ck(ri==1&&count("identity")==1&&count("rog")==1&&count("eval")==1,"ordinary HID Legion ROG canonical dispatch retained");
 }
}

struct Case {const char* name;void(*fn)();};
static const Case cases[]={{"raw-packet-normal",normal},{"raw-packet-keyboard-edges",keyboard},{"raw-packet-negative-cache",negativeCase},{"raw-packet-header-fallback",headerFallback},{"raw-packet-query-bounds",queryBounds},{"raw-packet-read-failures",readFailures},{"raw-packet-insufficient-recovery",insufficient},{"raw-packet-final-bounds",finalBounds}};
static const Case mouseCases[]={{"raw-mouse-diag-off",mouseOff},{"raw-mouse-diag-on",mouseOn},{"raw-mouse-diag-switch",mouseSwitch},{"raw-mouse-default-cleanup",mouseCleanup},{"raw-mouse-header-fallback",mouseFallback},{"raw-mouse-nonmouse-retained",mouseNonMouse}};
int main(int argc,char** argv){(void)&machineIsRogAllyFamily;(void)&domainLogEnabled;(void)&inputCaptureLegionControllerMotionRoute;const char* selected=nullptr;const bool mouseOnly=argc==2&&std::strcmp(argv[1],"--mouse-payload-admission")==0;if(argc==3&&std::strcmp(argv[1],"--case")==0)selected=argv[2];else if(argc!=1&&!mouseOnly)return 2;unsigned n=0;const auto run=[&](const Case& c){if(selected&&std::strcmp(c.name,selected)!=0)return;currentCase=c.name;unsigned b=checks,f=failures;c.fn();++n;std::printf("CASE %s checks=%u failures=%u\n",c.name,checks-b,failures-f);};if(!mouseOnly)for(const auto& c:cases)run(c);if(mouseOnly||selected)for(const auto& c:mouseCases)run(c);if(!n)return 2;std::printf("SUMMARY cases=%u checks=%u failures=%u\n",n,checks,failures);return failures?1:0;}
