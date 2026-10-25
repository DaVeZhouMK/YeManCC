// Actual Carrier retirement and ownership on an isolated, off-screen Win32 HWND.
// No live YMCC/WebView renderer/driver/game is touched. The runner only exposes
// private members in a temporary byte-for-byte production header copy.
#include "carrier-under-test.h"
#include <cassert>
#include <iostream>
using namespace ymcc::desktophud;
class InertEnvironment final : public ICoreWebView2Environment {
 ULONG refs=1;
public:
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** value)override{
  if(!value)return E_POINTER;*value=nullptr;
  if(id==__uuidof(IUnknown)||id==__uuidof(ICoreWebView2Environment)){*value=static_cast<ICoreWebView2Environment*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
 }
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
 ULONG STDMETHODCALLTYPE Release()override{const auto remaining=--refs;if(!remaining)delete this;return remaining;}
 HRESULT STDMETHODCALLTYPE CreateCoreWebView2Controller(HWND,ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE CreateWebResourceResponse(IStream*,int,LPCWSTR,LPCWSTR,ICoreWebView2WebResourceResponse**)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE get_BrowserVersionString(LPWSTR*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE add_NewBrowserVersionAvailable(ICoreWebView2NewBrowserVersionAvailableEventHandler*,EventRegistrationToken*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE remove_NewBrowserVersionAvailable(EventRegistrationToken)override{return E_NOTIMPL;}
};
int main(){
 Json checks=Json::array();const auto check=[&](const char* name,bool value){if(!value){std::cerr<<name<<"\n";std::abort();}checks.push_back(name);};
 ymcc::hudrecovery::Budget budget;check("initial budget admits HUD",budget.available()&&budget.failures()==0);
 for(unsigned i=1;i<=3;++i){budget.failed();check("budget allows at most two recreations",budget.failures()==i&&budget.available()==(i<3));}
 for(int i=0;i<10000;++i)budget.failed();check("budget saturates rather than overflow",budget.failures()==3&&!budget.available());
 Carrier carrier;ComPtr<ICoreWebView2Environment> env;env.Attach(new InertEnvironment());
 Json events=Json::array();Config cfg{[](ICoreWebView2*){return true;},[&](const std::string& name,const Json& value){events.push_back({{"event",name},{"value",value}});},nullptr};
 carrier.initialize(env.Get(),cfg);check("inert environment admits initial availability",carrier.available());
 WNDCLASSW cls{};cls.lpfnWndProc=Carrier::proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YMCC_HudRecoveryIsolatedTest";assert(RegisterClassW(&cls));
 std::shared_ptr<Carrier::State> stale;
 const auto seed=[&](){
  auto s=std::make_shared<Carrier::State>();s->owner=&carrier;s->id=carrier.nextId++;s->nonce="isolated-hud-recovery-owner";s->global=true;s->armed=s->ready=s->layout=true;s->expires=wallNow()+6000;
  s->hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,cls.lpszClassName,L"Isolated HUD recovery test",WS_POPUP|WS_DISABLED,-32000,-32000,4,4,nullptr,nullptr,cls.hInstance,s.get());assert(s->hwnd);carrier.current=s;return s;
 };
 const auto dispatch=[](){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}};
 for(unsigned failure=1;failure<=3;++failure){
  auto s=seed();const auto hwnd=s->hwnd;ShowWindow(hwnd,SW_SHOWNOACTIVATE);check("isolated HWND initially visible off screen",IsWindowVisible(hwnd));
  carrier.fail(s,"webview-process-failed-1");
  check("failure hides synchronously and revokes readiness",!IsWindowVisible(hwnd)&&!s->armed&&!s->ready&&!s->layout&&s->failed);
  check("controller/HWND retirement deferred beyond event callback",carrier.current==s&&IsWindow(hwnd)&&events.size()==failure-1);
  for(int i=0;i<1000;++i)carrier.fail(s,"duplicate-fault");check("duplicate process notifications spend one budget entry",carrier.recovery.failures()==failure);
  carrier.refresh(s,true);check("foreground/display repair cannot reshow failed HUD",!IsWindowVisible(hwnd));
  check("new data cannot rearm failed renderer",!carrier.frame(s->id,s->nonce,Json::object()));
  if(failure==2)SendMessageW(hwnd,WM_TIMER,Carrier::leaseTimer,0);else dispatch();
  check("posted retirement or existing lease fallback closes exact HWND",!carrier.current&&!IsWindow(hwnd)&&s->closed&&s->owner==nullptr);
  check("one close receipt per failed generation",events.size()==failure&&events.back()["value"]["id"]==s->id&&events.back()["value"]["nonce"]==s->nonce&&events.back()["value"]["reason"]=="webview-process-failed-1");
  check("two recoverable faults then native availability blocks",carrier.available()==(failure<3));
  carrier.initialize(env.Get(),cfg);check("status/init cannot reset process-lifetime budget",carrier.recovery.failures()==failure&&carrier.available()==(failure<3));
  if(stale){carrier.fail(stale,"late-stale-notification");check("retired owner cannot consume new budget",carrier.recovery.failures()==failure);}stale=s;
 }
 const auto status=carrier.status();check("status exposes failure count and terminal block",status["recoveryBlocked"]==true&&status["rendererFailures"]==3&&status["hiddenConfirmed"]==true);
 bool refused=false;try{carrier.create(0,0,"isolated-new-hud-owner",true);}catch(const std::runtime_error&){refused=true;}
 check("blocked create refuses before new renderer or window",refused&&!carrier.current);
 dispatch();check("late posted messages do not create or retain output",!carrier.current&&events.size()==3);
 // Closing a faulted HWND before deferred dispatch must not affect a new owner.
 Carrier replaced;replaced.initialize(env.Get(),cfg);auto prior=std::make_shared<Carrier::State>();prior->owner=&replaced;prior->id=1;prior->nonce="isolated-old-owner";prior->global=true;
 prior->hwnd=CreateWindowExW(0,cls.lpszClassName,L"",WS_POPUP,-32000,-32000,4,4,nullptr,nullptr,cls.hInstance,prior.get());replaced.current=prior;replaced.fail(prior,"fault");assert(replaced.close(1,prior->nonce));
 auto next=std::make_shared<Carrier::State>();next->owner=&replaced;next->id=2;next->nonce="isolated-next-owner";next->global=true;
 next->hwnd=CreateWindowExW(0,cls.lpszClassName,L"",WS_POPUP,-32000,-32000,4,4,nullptr,nullptr,cls.hInstance,next.get());replaced.current=next;replaced.fail(prior,"late");dispatch();
 check("old fault and queued work cannot retire replacement HWND",replaced.current==next&&!next->closed&&replaced.recovery.failures()==1);replaced.close(2,next->nonce);

 // No WebView, telemetry or visible overlay: seed inert off-screen notice owners.
 {
  Carrier first,second;first.initialize(env.Get(),cfg);second.initialize(env.Get(),cfg);
  first.hook();second.hook();
  check("two carriers share one OS watcher set",Carrier::eventOwners.size()==2&&Carrier::sharedHooks.size()==6);
  first.unhook();check("retiring one carrier retains the other watchers",Carrier::eventOwners.size()==1&&Carrier::sharedHooks.size()==6);
  second.unhook();check("last carrier releases all OS watchers",Carrier::eventOwners.empty()&&Carrier::sharedHooks.empty());
  auto notice=std::make_shared<Carrier::State>();notice->owner=&first;notice->notice=true;notice->global=true;notice->id=77;notice->nonce="isolated-notice-owner";notice->ready=true;
  notice->hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,cls.lpszClassName,L"Isolated notice admission",WS_POPUP|WS_DISABLED,-32000,-32000,4,4,nullptr,nullptr,cls.hInstance,notice.get());assert(notice->hwnd);first.current=notice;
  const auto expires=wallNow()+2500;Json value={{"type","monitor-notice"},{"protocol",1},{"hudId",77},{"nonce",notice->nonce},{"seq",1},{"expiresAt",expires},{"feedback",{{"expiresAtUnixMs",expires}}}};
  check("notice rejects wrong owner",!first.noticeFrame(77,"wrong-owner",value));
  check("notice admits bounded display event without opening sensors",first.noticeFrame(77,notice->nonce,value)&&notice->armed&&notice->seq==1&&!IsWindowVisible(notice->hwnd));
  check("duplicate native frame cannot renew lease",!first.noticeFrame(77,notice->nonce,value));
  check("notice surface cannot accept sensor frames",!first.frame(77,notice->nonce,Json::object()));
  check("OFF notice hide does not enable persistent output",first.noticeHide(77,notice->nonce,1)&&!notice->armed&&!IsWindowVisible(notice->hwnd));
  check("notice closes only its own descriptor",!first.close(77,"wrong-owner")&&first.close(77,notice->nonce));
 }
 UnregisterClassW(cls.lpszClassName,cls.hInstance);
 std::cout<<Json{{"ok",true},{"passed",checks.size()},{"checks",checks},{"actualCarrierFunctions",true},{"actualIsolatedWin32Windows",true},{"realRendererCrashInjected",false},{"installedAppChanged",false}}.dump(2)<<"\n";
}
