#define NOMINMAX
#define YMCC_MONITOR_NOTICE_NATIVE_TEST 1
#include <windows.h>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>
namespace probe {
std::vector<HWND> owned;std::map<HWND,unsigned> deny;std::map<std::pair<HWND,UINT_PTR>,unsigned> timerTicks;unsigned destroyed=0,rejected=0,ticks=0;
BOOL WINAPI destroy(HWND window){if(!window)return FALSE;if(std::find(owned.begin(),owned.end(),window)==owned.end()){++rejected;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}if(deny[window]){--deny[window];SetLastError(ERROR_ACCESS_DENIED);return FALSE;}const BOOL result=::DestroyWindow(window);if(result)++destroyed;return result;}
}
#define DestroyWindow probe::destroy
#include "monitor_desktop_hud.h"
#undef DestroyWindow
using namespace ymcc::desktophud;
using Microsoft::WRL::RuntimeClass;using Microsoft::WRL::RuntimeClassFlags;using Microsoft::WRL::ClassicCom;using Microsoft::WRL::Make;
static unsigned controllerLive=0,controllerDestroyed=0,controllerClosed=0;
class TestController final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,ICoreWebView2Controller>{
public:HRESULT closeResult=S_OK;TestController(){++controllerLive;}~TestController(){--controllerLive;++controllerDestroyed;}
 HRESULT STDMETHODCALLTYPE get_IsVisible(BOOL* p)override{*p=FALSE;return S_OK;}HRESULT STDMETHODCALLTYPE put_IsVisible(BOOL)override{return S_OK;}
 HRESULT STDMETHODCALLTYPE get_Bounds(RECT* p)override{*p={};return S_OK;}HRESULT STDMETHODCALLTYPE put_Bounds(RECT)override{return S_OK;}
 HRESULT STDMETHODCALLTYPE get_ZoomFactor(double* p)override{*p=1;return S_OK;}HRESULT STDMETHODCALLTYPE put_ZoomFactor(double)override{return S_OK;}
 HRESULT STDMETHODCALLTYPE add_ZoomFactorChanged(ICoreWebView2ZoomFactorChangedEventHandler*,EventRegistrationToken*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE remove_ZoomFactorChanged(EventRegistrationToken)override{return S_OK;}
 HRESULT STDMETHODCALLTYPE SetBoundsAndZoomFactor(RECT,double)override{return S_OK;}
 HRESULT STDMETHODCALLTYPE MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE add_MoveFocusRequested(ICoreWebView2MoveFocusRequestedEventHandler*,EventRegistrationToken*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE remove_MoveFocusRequested(EventRegistrationToken)override{return S_OK;}
 HRESULT STDMETHODCALLTYPE add_GotFocus(ICoreWebView2FocusChangedEventHandler*,EventRegistrationToken*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken)override{return S_OK;}
 HRESULT STDMETHODCALLTYPE add_LostFocus(ICoreWebView2FocusChangedEventHandler*,EventRegistrationToken*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE remove_LostFocus(EventRegistrationToken)override{return S_OK;}
 HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(ICoreWebView2AcceleratorKeyPressedEventHandler*,EventRegistrationToken*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyPressed(EventRegistrationToken)override{return S_OK;}
 HRESULT STDMETHODCALLTYPE get_ParentWindow(HWND* p)override{*p=nullptr;return S_OK;}HRESULT STDMETHODCALLTYPE put_ParentWindow(HWND)override{return S_OK;}
 HRESULT STDMETHODCALLTYPE NotifyParentWindowPositionChanged()override{return S_OK;}
 HRESULT STDMETHODCALLTYPE Close()override{++controllerClosed;return closeResult;}
 HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2** p)override{*p=nullptr;return E_NOTIMPL;}
};
static LRESULT CALLBACK privateProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_TIMER){++probe::ticks;++probe::timerTicks[{h,static_cast<UINT_PTR>(w)}];return 0;}return DefWindowProcW(h,m,w,l);}
static HWND makeHidden(){HWND h=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"YMCC_Notice_Owned_Lease_Test",L"Private notice lease fixture",WS_POPUP|WS_DISABLED,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!h)throw std::runtime_error("Own hidden window creation failed");probe::owned.push_back(h);return h;}
namespace ymcc::desktophud {struct CarrierTestAccess {
 static std::pair<HWND,HWND> attach(Carrier& c,unsigned long long lease,const std::string& nonce,HRESULT closeResult=S_OK){
  if(c.current)throw std::runtime_error("Test attach requires empty carrier");auto s=std::make_shared<Carrier::State>();s->owner=&c;s->id=c.nextId++;s->noticeLease=lease;s->nonce=nonce;s->notice=true;s->global=true;s->ready=true;s->hwnd=makeHidden();s->inputWindow=makeHidden();
  auto controller=Make<TestController>();controller->closeResult=closeResult;s->controller=controller;c.current=s;
  if(!SetTimer(s->hwnd,Carrier::leaseTimer,10,nullptr)||!SetTimer(s->hwnd,Carrier::holdTimer,10,nullptr)||!SetTimer(s->inputWindow,777,10,nullptr))throw std::runtime_error("Own timers not armed");return {s->hwnd,s->inputWindow};
 }
 static Json timers(const std::pair<HWND,HWND>& windows){return {{"mainWindowAlive",bool(IsWindow(windows.first))},{"inputWindowAlive",bool(IsWindow(windows.second))},{"leaseTicks",probe::timerTicks[{windows.first,Carrier::leaseTimer}]},{"holdTicks",probe::timerTicks[{windows.first,Carrier::holdTimer}]},{"inputTicks",probe::timerTicks[{windows.second,777}]},{"controllerLive",controllerLive},{"allTimerTicks",probe::ticks},{"observedAtMs",GetTickCount64()}};}
 static unsigned deliveredTimers(const std::pair<HWND,HWND>& windows){return probe::timerTicks[{windows.first,Carrier::leaseTimer}]+probe::timerTicks[{windows.first,Carrier::holdTimer}]+probe::timerTicks[{windows.second,777}];}
 static int id(Carrier& c){return c.current?c.current->id:0;}
 static bool retained(Carrier& c){return c.current!=nullptr;}
 static bool retired(Carrier& c){return c.noticeLeaseRetired;}
 static void forceGeneration(Carrier& c,unsigned long long value){c.noticeLeaseSerial=value;}
};}
using json=Json;
#include "monitor_notice_log.h"
static std::vector<Json> noticeDiagnosticReceipts;
static void appendNativeLifecycleLog(const char* event,Json value){value["event"]=event;noticeDiagnosticReceipts.push_back(std::move(value));}
#include "notice-log-helper.inc"
static Carrier g_monitorNoticeCarrier;
static std::map<std::string,std::function<Json(const Json&)>> nativeHandlers;
static void ipc_on(const std::string& name,std::function<Json(const Json&)> handler){nativeHandlers[name]=std::move(handler);}
static void registerExactNoticeHandlers(){
 const auto initializeNotice=[]{Config config;config.noticeOnly=true;g_monitorNoticeCarrier.initialize(nullptr,std::move(config));};
#include "notice-ipc-handlers.inc"
}
static void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void pump(DWORD ms){const auto end=GetTickCount64()+ms;while(GetTickCount64()<end){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(1);}}
// Observe actual WM_TIMER delivery from this pair of owned HWNDs. The native
// process still has its original 15s budget; a missing timer keeps the original failure.
static void pumpUntilOwnTimer(const std::pair<HWND,HWND>& windows,DWORD budgetMs){const auto before=CarrierTestAccess::deliveredTimers(windows);const auto end=GetTickCount64()+budgetMs;do{pump(1);}while(CarrierTestAccess::deliveredTimers(windows)==before&&GetTickCount64()<end);}
static Json fixtureResources(){unsigned windows=0;for(HWND h:probe::owned)if(IsWindow(h))++windows;return {{"liveOwnedWindowEntries",windows},{"controllerLive",controllerLive},{"controllerDestroyed",controllerDestroyed},{"allTimerTicks",probe::ticks}};}
int wmain(int argc,wchar_t** argv){if(argc!=2)return 10;Json cases=Json::array(),timerDiagnostics=Json::array();bool all=true;WNDCLASSW cls{};cls.lpfnWndProc=privateProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YMCC_Notice_Owned_Lease_Test";if(!RegisterClassW(&cls))return 11;
 auto test=[&](const char* id,auto action){const auto before=GetTickCount64();const auto resourcesBefore=fixtureResources();try{action();cases.push_back({{"id",id},{"passed",true},{"durationMs",GetTickCount64()-before},{"resourcesBefore",resourcesBefore},{"resourcesAfter",fixtureResources()}});}catch(const std::exception& error){all=false;cases.push_back({{"id",id},{"passed",false},{"error",error.what()},{"durationMs",GetTickCount64()-before},{"resourcesBefore",resourcesBefore},{"resourcesAfter",fixtureResources()}});}};
 auto setup=[](Carrier& c){Config config;config.noticeOnly=true;c.initialize(nullptr,std::move(config));};
 const std::string a="owned-notice-lease-aaaaaaaa",b="owned-notice-lease-bbbbbbbb";
 test("retirement-before-create-revokes-native-admission-without-a-window",[&]{Carrier c;setup(c);auto reservation=c.reserveNotice(a);const auto lease=reservation["leaseId"].get<unsigned long long>();auto result=c.retireNotice(lease,a);need(result["leaseRetired"]&&result["resourcesDestroyed"],"Reservation retirement not acknowledged");bool rejected=false;try{c.create(0,0,a,true,lease);}catch(const std::exception& e){rejected=std::string(e.what()).find("lease retired or replaced")!=std::string::npos;}need(rejected&&!CarrierTestAccess::retained(c),"Late retired create regained native admission");});
 test("exact-native-retirement-destroys-two-owned-windows-timers-and-controller-reference",[&]{Carrier c;setup(c);auto lease=c.reserveNotice(a)["leaseId"].get<unsigned long long>();auto windows=CarrierTestAccess::attach(c,lease,a);const auto armedAt=GetTickCount64();Json observation={{"mode","bounded-own-timer-delivery"},{"waitBudgetMs",500},{"afterAttach",CarrierTestAccess::timers(windows)}};pumpUntilOwnTimer(windows,500);observation["afterBoundedPump"]=CarrierTestAccess::timers(windows);observation["waitElapsedMs"]=GetTickCount64()-armedAt;timerDiagnostics.push_back(observation);need(probe::ticks>0&&controllerLive==1,"Own HWND timers/controller were not active");auto result=c.retireNotice(lease,a);need(result["resourcesDestroyed"]&&result["ownedOnly"],"Retirement not confirmed");need(!IsWindow(windows.first)&&!IsWindow(windows.second)&&!controllerLive&&!CarrierTestAccess::retained(c),"Own native resources remain");const auto ticks=probe::ticks;pump(30);need(probe::ticks==ticks,"Destroyed native timer fired again");});
 test("foreign-nonce-and-future-generation-cannot-destroy-owned-resources",[&]{Carrier c;setup(c);auto lease=c.reserveNotice(a)["leaseId"].get<unsigned long long>();auto windows=CarrierTestAccess::attach(c,lease,a);need(!c.retireNotice(lease,b)["leaseRetired"].get<bool>(),"Foreign nonce admitted");need(!c.retireNotice(lease+1,a)["leaseRetired"].get<bool>(),"Future generation admitted");need(IsWindow(windows.first)&&IsWindow(windows.second),"Unmatched request destroyed windows");need(c.retireNotice(lease,a)["resourcesDestroyed"],"Exact cleanup failed");});
 test("obsolete-retirement-cannot-close-the-newer-owner",[&]{Carrier c;setup(c);auto first=c.reserveNotice(a)["leaseId"].get<unsigned long long>();c.retireNotice(first,a);auto second=c.reserveNotice(b)["leaseId"].get<unsigned long long>();auto windows=CarrierTestAccess::attach(c,second,b);auto old=c.retireNotice(first,a);need(old["obsolete"]&&IsWindow(windows.first)&&controllerLive==1,"Old retirement touched current owner");need(c.retireNotice(second,b)["resourcesDestroyed"],"Current owner cleanup failed");});
 test("ordinary-close-revokes-create-before-window-callbacks",[&]{Carrier c;setup(c);auto lease=c.reserveNotice(a)["leaseId"].get<unsigned long long>();auto windows=CarrierTestAccess::attach(c,lease,a);need(c.close(CarrierTestAccess::id(c),a),"Ordinary close failed");need(CarrierTestAccess::retired(c)&&!IsWindow(windows.first),"Ordinary close left create lease active");});
 test("first-destroywindow-failure-keeps-owner-for-exact-retirement-retry",[&]{Carrier c;setup(c);auto lease=c.reserveNotice(a)["leaseId"].get<unsigned long long>();auto windows=CarrierTestAccess::attach(c,lease,a);probe::deny[windows.first]=1;need(!c.close(CarrierTestAccess::id(c),a),"Failed DestroyWindow was reported as success");need(IsWindow(windows.first)&&CarrierTestAccess::retained(c),"Failed resource ownership was discarded");need(c.retireNotice(lease,a)["resourcesDestroyed"]&&!IsWindow(windows.first),"Owned retirement retry failed");});
 test("input-window-destruction-failure-also-retains-and-retries-owner",[&]{Carrier c;setup(c);auto lease=c.reserveNotice(a)["leaseId"].get<unsigned long long>();auto windows=CarrierTestAccess::attach(c,lease,a);probe::deny[windows.second]=1;need(!c.close(CarrierTestAccess::id(c),a),"Failed input window destruction reported success");need(IsWindow(windows.second)&&CarrierTestAccess::retained(c),"Input resource owner discarded");need(c.retireNotice(lease,a)["resourcesDestroyed"]&&!IsWindow(windows.second),"Input window retry failed");});
 test("controller-close-error-still-releases-owned-com-reference-and-windows",[&]{Carrier c;setup(c);auto lease=c.reserveNotice(a)["leaseId"].get<unsigned long long>();auto windows=CarrierTestAccess::attach(c,lease,a,E_FAIL);need(c.retireNotice(lease,a)["resourcesDestroyed"],"Retirement failed with HRESULT error");need(!controllerLive&&!IsWindow(windows.first)&&!IsWindow(windows.second),"Owned ABI test controller or HWND retained");});
 test("repeated-retirement-is-idempotent-and-native-generation-never-wraps",[&]{Carrier c;setup(c);auto lease=c.reserveNotice(a)["leaseId"].get<unsigned long long>();for(int i=0;i<5;i++)need(c.retireNotice(lease,a)["resourcesDestroyed"],"Repeated retirement changed result");CarrierTestAccess::forceGeneration(c,9007199254740991ULL);bool refused=false;try{c.reserveNotice(b);}catch(...){refused=true;}need(refused,"JSON-safe generation wrapped");});
 test("exact-main-reserve-create-retire-handlers-enforce-native-lease-and-request-shape",[&]{registerExactNoticeHandlers();auto reserved=nativeHandlers.at("monitorNotice.reserve")({{"nonce",a}});const auto lease=reserved["leaseId"].get<unsigned long long>();need(nativeHandlers.at("monitorNotice.retire")({{"leaseId",lease},{"nonce",a}})["resourcesDestroyed"],"Exact main retirement handler failed");bool retired=false,shape=false;try{nativeHandlers.at("monitorNotice.create")({{"leaseId",lease},{"nonce",a}});}catch(const std::exception& e){retired=std::string(e.what()).find("lease retired or replaced")!=std::string::npos;}try{nativeHandlers.at("monitorNotice.retire")({{"hudId",1},{"nonce",a}});}catch(...){shape=true;}need(retired&&shape,"Exact main request admission was bypassed");});
 unsigned remaining=0;for(HWND h:probe::owned)if(IsWindow(h))++remaining;need(!controllerLive,"Controller reference remained at final audit");Json report={{"status",all&&remaining==0?"PASS":"FAIL"},{"cases",cases},{"timerDiagnostics",timerDiagnostics},{"counts",{{"cases",cases.size()},{"passed",std::count_if(cases.begin(),cases.end(),[](const auto& c){return c["passed"].template get<bool>();})}}},{"remainingOwnedWindows",remaining},{"controllerLive",controllerLive},{"controllerDestroyed",controllerDestroyed},{"controllerCloseCalls",controllerClosed},{"actualDestroyWindowSuccesses",probe::destroyed},{"unownedDestroyAttempts",probe::rejected},{"hiddenOwnWindowsOnly",true},{"productionCarrierExecuted",true},{"controllerAbiTestDouble",true},{"realWebView2ControllerExecuted",false},{"deviceIo",false},{"normalYmccStarted",false},{"operatingSystemSleep",false},{"manualSleepAcceptanceReady",false}};std::ofstream file(argv[1]);file<<report.dump(2)<<'\n';for(HWND h:probe::owned)if(IsWindow(h))::DestroyWindow(h);return report["status"]=="PASS"?0:2;
}
