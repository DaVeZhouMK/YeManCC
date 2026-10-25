#pragma once
// Native desktop carrier ONLY. No graphics injection, Steam calls, hardware
// telemetry, settings, process scanning timers or input synthesis. The renderer
// receives a strict display-only IPC whitelist, never the main IPC dispatcher.
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <dcomp.h>
#pragma comment(lib, "dcomp.lib")
#include "json.hpp"
#include "monitor_hud_placement.h"
#include "monitor_hud_serialization.h"
#include "monitor_hud_input_policy.h"
#include "monitor_hud_panel_input.h"
#include "monitor_hud_recovery.h"
#include "monitor_hud_visibility.h"
#include "monitor_hud_drag.h"
#include "touch_haptic_dispatch.h"
#include "screen_touchpad_input_surface.h"
#include <cmath>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace ymcc::desktophud {
using Json=nlohmann::json;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
inline std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);if(!n)throw std::runtime_error("Invalid UTF-8");std::wstring v(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),v.data(),n);return v;}
inline std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);std::string v(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),v.data(),n,nullptr,nullptr);return v;}
inline bool tokenValid(const std::string& s){return s.size()>=16&&s.size()<=80&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>='a'&&c<='z'||c>='A'&&c<='Z'||c>='0'&&c<='9'||c=='-';});}
inline ULONGLONG wallNow(){FILETIME f{};GetSystemTimeAsFileTime(&f);ULARGE_INTEGER t{};t.LowPart=f.dwLowDateTime;t.HighPart=f.dwHighDateTime;return (t.QuadPart-116444736000000000ULL)/10000;}
struct Target {HWND window=nullptr;DWORD pid=0;ULONGLONG created=0;HANDLE process=nullptr;~Target(){if(process)CloseHandle(process);}};
inline std::shared_ptr<Target> bindTarget(DWORD pid,ULONGLONG created){
 if(!pid||pid==GetCurrentProcessId()||!created)throw std::runtime_error("Invalid desktop HUD process identity");
 auto t=std::make_shared<Target>();t->pid=pid;t->created=created;
 t->process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
 FILETIME b{},e{},k{},u{};DWORD targetSession=0,ownSession=0;
 if(!t->process||!GetProcessTimes(t->process,&b,&e,&k,&u)||WaitForSingleObject(t->process,0)!=WAIT_TIMEOUT||
   !ProcessIdToSessionId(pid,&targetSession)||!ProcessIdToSessionId(GetCurrentProcessId(),&ownSession)||targetSession!=ownSession||
   ((ULONGLONG)b.dwHighDateTime<<32|b.dwLowDateTime)!=created)throw std::runtime_error("Desktop HUD target identity not confirmed");
 struct Search {DWORD pid;HWND found=nullptr;long long area=-1;} search{pid};
 EnumWindows([](HWND h,LPARAM p)->BOOL{auto& s=*reinterpret_cast<Search*>(p);DWORD pid=0;GetWindowThreadProcessId(h,&pid);
   if(pid!=s.pid||!IsWindowVisible(h)||IsIconic(h)||GetWindow(h,GW_OWNER))return TRUE;
   RECT r{};if(!GetClientRect(h,&r)||r.right<=0||r.bottom<=0)return TRUE;
   const auto area=(long long)r.right*r.bottom;if(area>s.area){s.area=area;s.found=h;}return TRUE;
 },reinterpret_cast<LPARAM>(&search));t->window=search.found;
 if(!t->window)throw std::runtime_error("Target has no visible normal/borderless window");return t;
}
struct Config {
 std::function<bool(ICoreWebView2*)> serve;
 std::function<void(const std::string&,const Json&)> notify;
 HWND anchor=nullptr;bool noticeOnly=false;
 screeninput::Runtime* inputRuntime=nullptr;std::function<void(bool,bool)> haptic;
 std::function<void(int,int)> radialSummon;std::function<bool()> radialHoldEnabled;std::function<unsigned()> radialHoldDuration;
};
class Carrier {
#if defined(YMCC_RADIAL_NATIVE_TEST) || defined(YMCC_MONITOR_NOTICE_NATIVE_TEST)
 friend struct CarrierTestAccess; // Owned native renderer acceptance only.
#endif
 static constexpr UINT updateMessage=WM_APP+47;
 static constexpr UINT_PTR holdTimer=65047;
 static constexpr UINT_PTR radialFadeTimer=65048;
 static constexpr UINT_PTR leaseTimer=0x5948;
 struct State {
   Carrier* owner=nullptr;int id=0;unsigned long long noticeLease=0;std::string nonce;std::shared_ptr<Target> target;bool global=false,notice=false;HMONITOR monitor=nullptr;
   HWND hwnd=nullptr;ComPtr<ICoreWebView2Controller> controller;ComPtr<ICoreWebView2> view;
   ComPtr<ICoreWebView2CompositionController> composition;ComPtr<IDCompositionDevice> device;ComPtr<IDCompositionTarget> visualTarget;ComPtr<IDCompositionVisual> visual;
   bool closed=false,ready=false,armed=false,layout=false,queued=false,queuedRaise=false,crosshair=false,failed=false;
   HWND inputWindow=nullptr;hudinput::PanelInput input;huddrag::Gesture drag;touchhaptic::Motion dragHaptic;bool inputSuspended=false,mouseContact=false,touchContact=false;UINT32 mousePointerId=0,touchPointerId=0;POINTER_INPUT_TYPE touchType=PT_TOUCH;hudrecovery::Budget inputRecovery;std::string inputError;RECT panel{},placementArea{};
   bool radialContact=false,radialArmed=false;unsigned radialContactId=0;ULONGLONG radialDownAt=0;POINT radialDown{};
   std::string activeTemplate="A";unsigned long long templateRevision=0;
   int width=0,height=0,position=1;unsigned long long seq=0,expires=0;
   DWORD policyGamePid=0;hudvisibility::Mode visibility=hudvisibility::Mode::Always;bool policyWaiting=false;unsigned long long visibilityRevision=0;
   Json inputWithdrawal; // Last actual input withdrawal only; no periodic collection.
   std::string url,reason="creating",layoutKey,viewportKey;unsigned long long paintedSeq=0;
 };
 std::shared_ptr<State> current;
 ComPtr<ICoreWebView2Environment> environment;
 bool radialSuspended=false;ULONGLONG radialHideAt=0;
 Config config;int nextId=1;hudrecovery::Budget recovery;
 unsigned long long noticeLeaseSerial=0;std::string noticeLeaseNonce;bool noticeLeaseRetired=true;
 inline static thread_local std::vector<HWINEVENTHOOK> sharedHooks;
 inline static thread_local std::vector<Carrier*> eventOwners;
 static bool alive(const State& s){if(s.global)return true;DWORD pid=0;return s.target&&s.target->process&&WaitForSingleObject(s.target->process,0)==WAIT_TIMEOUT&&
   IsWindow(s.target->window)&&GetWindowThreadProcessId(s.target->window,&pid)&&pid==s.target->pid;}
 static bool monitorArea(const State& s,RECT& r){
   MONITORINFO info{sizeof(info)};const auto monitor=s.monitor?s.monitor:MonitorFromWindow(nullptr,MONITOR_DEFAULTTOPRIMARY);
   if(!GetMonitorInfoW(monitor,&info)&&!GetMonitorInfoW(MonitorFromWindow(nullptr,MONITOR_DEFAULTTOPRIMARY),&info))return false;
   r=info.rcWork;return r.right>r.left&&r.bottom>r.top;
 }
 static bool screenRects(const State& s,RECT& full,RECT& work){
   MONITORINFO info{sizeof(info)};
   const auto monitor=s.global?s.monitor:MonitorFromWindow(s.target?s.target->window:nullptr,MONITOR_DEFAULTTOPRIMARY);
   if(!GetMonitorInfoW(monitor,&info)&&!GetMonitorInfoW(MonitorFromWindow(nullptr,MONITOR_DEFAULTTOPRIMARY),&info))return false;
   full=info.rcMonitor;work=info.rcWork;return full.right>full.left&&full.bottom>full.top;
 }
 static bool displayPlacementMatches(const State& s){
   RECT actual{},expected=s.panel;
   if(s.crosshair){RECT work{};if(!screenRects(s,expected,work))return false;}
   return s.hwnd&&GetWindowRect(s.hwnd,&actual)&&EqualRect(&actual,&expected);
 }
 static Json viewportInfo(const State& s){
   RECT full{},work{};if(!screenRects(s,full,work))return Json::object();
   return {{"id",s.id},{"width",full.right-full.left},{"height",full.bottom-full.top},
     {"workArea",{{"left",work.left-full.left},{"top",work.top-full.top},{"width",work.right-work.left},{"height",work.bottom-work.top}}}};
 }
 static bool clientArea(const State& s,RECT& r){
   if(s.global){
     MONITORINFO info{sizeof(info)};
     const auto monitor=s.monitor?s.monitor:MonitorFromWindow(nullptr,MONITOR_DEFAULTTOPRIMARY);
     if(!GetMonitorInfoW(monitor,&info)&&!GetMonitorInfoW(MonitorFromWindow(nullptr,MONITOR_DEFAULTTOPRIMARY),&info))return false;
     r=info.rcWork;return r.right>r.left&&r.bottom>r.top;
   }
   if(!alive(s)||IsIconic(s.target->window)||!IsWindowVisible(s.target->window)||!GetClientRect(s.target->window,&r))return false;
   POINT a{r.left,r.top},b{r.right,r.bottom};if(!ClientToScreen(s.target->window,&a)||!ClientToScreen(s.target->window,&b))return false;
   r={a.x,a.y,b.x,b.y};return r.right>r.left&&r.bottom>r.top;
 }
 // This is display-stack maintenance only, never a window-admission/FPS policy.
 // Uses the foreground already sampled by refresh; no scanner or timer added.
 static bool foregroundOccludes(const State& s,HWND foreground){
   if(!s.global||!foreground||foreground==s.hwnd)return false;
   const HWND root=GetAncestor(foreground,GA_ROOT);if(!root||root==s.hwnd||!IsWindowVisible(root)||IsIconic(root)||
     !(GetWindowLongPtrW(root,GWL_EXSTYLE)&WS_EX_TOPMOST))return false;
   RECT hud{},game{},intersection{};if(!GetWindowRect(s.hwnd,&hud)||!GetWindowRect(root,&game)||!IntersectRect(&intersection,&hud,&game))return false;
   // Bounded traversal of the top-level Z stack, no process or telemetry reads.
   HWND before=GetWindow(s.hwnd,GW_HWNDPREV);
   for(int count=0;before&&count<128;++count,before=GetWindow(before,GW_HWNDPREV))if(before==root)return true;
   return false;
 }
 bool owns(const std::shared_ptr<State>& s)const{return current==s&&!s->closed&&!s->failed&&s->hwnd;}
 // WebView callbacks may not synchronously destroy their own controller. Hide
 // immediately, then retire this exact HWND on its existing posted-update lane.
 // The carrier-wide budget survives HWND/JS-owner recreation and never polls.
 // A hidden output must hide its WebView input/composition tree too. An
 // unarmed pre-owned HWND is not permission to keep its controller visible.
 static void suspendRenderer(State* s){
   if(s&&s->controller){BOOL visible=TRUE;if(FAILED(s->controller->get_IsVisible(&visible))||visible)s->controller->put_IsVisible(FALSE);}
 }
 static void hideOutput(State* s,bool suspendRender=true){
   if(!s)return;if(s->hwnd)ShowWindow(s->hwnd,SW_HIDE);
   if(s->owner&&s->owner->current.get()==s)s->owner->stopInteraction(s->owner->current);
   if(suspendRender)suspendRenderer(s);
   else if(s->controller&&s->armed&&s->ready)s->controller->put_IsVisible(TRUE);
 }
 void fail(const std::shared_ptr<State>& s,const std::string& reason){
   if(!owns(s))return;
   s->failed=true;s->ready=false;s->armed=false;s->layout=false;s->reason=reason;
   recovery.failed();hideOutput(s.get());
   // If posting fails, the already-armed native lease timer is the fallback.
   // Incoming frame ACK failure also causes the parent to close the exact owner.
   PostMessageW(s->hwnd,updateMessage,0,0);
 }
 static Json inputStateBeforeWithdrawal(const State& s){
   RECT rect{};if(s.inputWindow)GetWindowRect(s.inputWindow,&rect);POINT point{};const bool cursorKnown=GetPhysicalCursorPos(&point)!=FALSE;
   GUITHREADINFO gui{sizeof(gui)};const bool guiKnown=GetGUIThreadInfo(GetCurrentThreadId(),&gui)!=FALSE;
   const HWND capture=guiKnown?gui.hwndCapture:GetCapture();wchar_t className[96]{};if(capture)GetClassNameW(capture,className,96);
   return {{"inputMode","native-panel-no-capture-v1"},{"inputBoundsPolicy","panel-point-v1"},{"recordedAtUnixMs",wallNow()},
     {"inputVisible",s.inputWindow&&IsWindowVisible(s.inputWindow)},{"displayVisible",s.hwnd&&IsWindowVisible(s.hwnd)},
     {"displayTransparent",hudinput::inert(s.hwnd)},{"inputDpi",s.inputWindow?GetDpiForWindow(s.inputWindow):0u},
     {"inputRect",{{"x",rect.left},{"y",rect.top},{"width",rect.right-rect.left},{"height",rect.bottom-rect.top}}},
     {"panelRect",{{"x",s.panel.left},{"y",s.panel.top},{"width",s.panel.right-s.panel.left},{"height",s.panel.bottom-s.panel.top}}},
     {"cursorKnown",cursorKnown},{"cursorOutsideInput",cursorKnown&&!PtInRect(&rect,point)},{"cursorOutsidePanel",cursorKnown&&!PtInRect(&s.panel,point)},
     {"captureWindow",(unsigned long long)reinterpret_cast<uintptr_t>(capture)},{"captureIsInput",capture&&capture==s.inputWindow},
     {"captureClass",utf8(className)},{"dragContact",s.drag.contact()},{"dragging",s.drag.dragging()},
     {"mouseContact",s.mouseContact},{"touchContact",s.touchContact},{"radialContact",s.radialContact}};
 }
 void emit(const std::shared_ptr<State>& s,const std::string& name){
   if(!config.notify)return;Json value={{"id",s->id},{"nonce",s->nonce},{"reason",s->reason},{"ready",s->ready},{"armed",s->armed},{"visible",s->hwnd&&IsWindowVisible(s->hwnd)},{"seq",s->seq},{"visibilityRevision",s->visibilityRevision}};
   if(name=="desktopHud.closed"&&s->inputWithdrawal.is_object()){try{value["inputWithdrawal"]=s->inputWithdrawal;}catch(...){/* Diagnostic allocation cannot block ownership cleanup. */}}
   if(name=="desktopHud.closed"){try{value["inputRelease"]={{"inputDestroyed",!s->inputWindow||!IsWindow(s->inputWindow)},
     {"displayDestroyed",!s->hwnd||!IsWindow(s->hwnd)},{"surfaceReleased",s->input.terminalReleased()},
     {"controllerReleased",!s->controller},{"contactsCleared",!s->drag.contact()&&!s->mouseContact&&!s->touchContact&&!s->radialContact&&!s->radialArmed},
     {"captureRetained",s->inputWindow&&GetCapture()==s->inputWindow}};}catch(...){/* Closing is already complete; missing diagnostic stays unknown. */}}

   config.notify(name,value);
 }
 bool policyPermits(const State& s,HWND foreground)const{
   if(s.notice||!s.global||s.visibility==hudvisibility::Mode::Always)return true;
   const DWORD game=s.policyGamePid;
   DWORD focused=0;if(game&&s.visibility==hudvisibility::Mode::GameFocused&&foreground)GetWindowThreadProcessId(foreground,&focused);
   return hudvisibility::permits(s.visibility,game,focused);
 }
 Json placement(const State& s)const {
   const auto& p=s.drag.position;Json j={{"dragLocked",p.locked},{"dragRevision",p.revision}};
   if(p.placed){j["dragX"]=p.x;j["dragY"]=p.y;}return j;
 }
 void interaction(const std::shared_ptr<State>& s,bool toast=false,bool commit=false){
   if(commit)s->drag.position.revision=huddrag::nextRevision(s->drag.position.revision,wallNow());
   Json value={{"id",s->id},{"nonce",s->nonce},{"seq",s->seq},{"dragging",s->drag.dragging()},{"toast",toast},{"placement",placement(*s)}};
   send(s,{{"event","desktopHud.interaction"},{"data",value}});
   if(commit&&config.notify)config.notify("desktopHud.placement",value);
 }
 void stopInteraction(const std::shared_ptr<State>& s,bool destroy=false){
   if(s->inputSuspended)return;s->inputSuspended=true;
   // Preserve the last live owner before hide/release destroys the evidence.
   // Hidden idle refreshes do not replace it and do not allocate/log samples.
   if((s->inputWindow&&IsWindowVisible(s->inputWindow))||s->drag.contact()||s->mouseContact||s->touchContact||s->radialContact){try{s->inputWithdrawal=inputStateBeforeWithdrawal(*s);}catch(...){/* Preserve the previous bounded diagnostic and always release input. */}}
   if(s->hwnd)KillTimer(s->hwnd,holdTimer);
   const bool active=s->drag.contact()||s->radialContact||s->radialArmed;s->mouseContact=false;s->mousePointerId=0;s->touchContact=false;s->touchPointerId=0;s->radialContact=false;s->radialArmed=false;const bool moving=s->drag.dragging();
   // Hide before cancellation callbacks or COM work can pump messages.
   if(destroy&&!s->input.quiesce())s->inputError="drag-input-quiesce-failed";
   if(s->inputWindow)ShowWindow(s->inputWindow,SW_HIDE);
   s->input.cancel();s->drag.cancel();s->dragHaptic={};if(active&&config.haptic)config.haptic(false,false);
   // Input cancellation/hide may never deliver an UP. Revoke visual hold state too.
   if(active&&!s->closed)send(s,{{"event","desktopHud.radialHold"},{"data",{{"active",false}}}});
   if(destroy){s->input.close();if(s->inputWindow)DestroyWindow(s->inputWindow);if(s->inputWindow&&!IsWindow(s->inputWindow))s->inputWindow=nullptr;}
   s->inputSuspended=false;if(moving&&!s->closed)interaction(s,false,true);
 }
 static bool windowMouse(State* raw,HWND hwnd,UINT message,WPARAM buttons,LPARAM coordinates,UINT32 pointerId=0){
   if(message!=WM_CAPTURECHANGED&&message!=WM_LBUTTONDOWN&&message!=WM_LBUTTONDBLCLK&&message!=WM_LBUTTONUP&&message!=WM_MOUSEMOVE)return false;
   if(!raw||!raw->owner||raw->inputSuspended||!raw->input.usesWindowMouse())return false;
   auto* owner=raw->owner;auto s=owner->current;
   if(!s||s.get()!=raw||!owner->owns(s)||s->inputWindow!=hwnd||!IsWindowVisible(hwnd)||!IsWindowVisible(s->hwnd))return true;
   constexpr UINT32 mouseId=0xFFFFFFFEu;
   const float x=static_cast<short>(LOWORD(coordinates)),y=static_cast<short>(HIWORD(coordinates));
   const auto dispatch=[&](screeninput::Phase phase){inputEvent(hwnd,{10,mouseId,PT_MOUSE,phase,x,y,phase==screeninput::Phase::Cancel},s.get());};
   const auto release=[&]{TRACKMOUSEEVENT track{sizeof(track),TME_CANCEL|TME_LEAVE,hwnd,0};TrackMouseEvent(&track);};
   if(message==WM_CAPTURECHANGED){
     if(s->mouseContact&&reinterpret_cast<HWND>(coordinates)!=hwnd){s->mouseContact=false;s->mousePointerId=0;dispatch(screeninput::Phase::Cancel);release();}return true;
   }
   if(message!=WM_LBUTTONDOWN&&message!=WM_LBUTTONDBLCLK&&message!=WM_LBUTTONUP&&message!=WM_MOUSEMOVE)return false;
   // Touch/pen use native WM_POINTER. Promoted mouse messages must
   // not produce a second DOWN/UP or a second template switch.
   if((static_cast<ULONG_PTR>(GetMessageExtraInfo())&0xFFFFFF00u)==0xFF515700u)return true;
   if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK){
     // A new remote press can follow a lost UP without an intervening MOVE.
     // Retire our previous press first; outside coordinates cannot retain it.
     if(s->mouseContact){s->mouseContact=false;s->mousePointerId=0;dispatch(screeninput::Phase::Cancel);release();}
     if(s->drag.contact()||s->radialContact||x<0||y<0||x>=s->width||y>=s->height)return true;
     dispatch(screeninput::Phase::Down);
     if(!s->drag.contact()&&!s->radialContact)return true;
     s->mouseContact=true;s->mousePointerId=pointerId;
     // A missing remote UP must never turn this background panel into a
     // desktop-wide mouse owner. Native bounded hit-testing owns every frame.
     TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);return true;
   }
   if(!s->mouseContact)return true;
   if(message==WM_MOUSEMOVE&&(buttons&MK_LBUTTON)){dispatch(screeninput::Phase::Move);return true;}
   s->mouseContact=false;s->mousePointerId=0;
   dispatch(message==WM_LBUTTONUP?screeninput::Phase::Up:screeninput::Phase::Cancel);
   release();return true;
 }
 static LRESULT CALLBACK inputProc(HWND h,UINT m,WPARAM w,LPARAM l){
   auto* s=reinterpret_cast<State*>(GetWindowLongPtrW(h,GWLP_USERDATA));
   if(m==WM_NCCREATE){s=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
   // The shell may hide/cancel the bounded input HWND independently of the
   // display. Withdraw only that contact owner; never reset desktop input.
   if((m==WM_CANCELMODE||(m==WM_SHOWWINDOW&&!w))&&s&&s->owner&&s->owner->current.get()==s){s->owner->stopInteraction(s->owner->current);return 0;}
   if(m==WM_DESTROY&&s&&s->inputWindow==h&&!s->inputSuspended&&s->owner&&s->owner->current.get()==s){
     auto* owner=s->owner;auto state=owner->current;s->inputWindow=nullptr;
     SetWindowLongPtrW(h,GWLP_USERDATA,0);
     owner->failInteraction(state,"drag-input-window-destroyed");return 0;
   }
   if(m==WM_NCDESTROY)SetWindowLongPtrW(h,GWLP_USERDATA,0);
   // Keep the input footprint bounded at the native mutation boundary, even
   // between data refreshes (shell/DPI/remote window changes are independent).
   if(m==WM_WINDOWPOSCHANGING&&l&&s&&!s->inputSuspended&&s->owner&&s->owner->current.get()==s&&s->inputWindow==h){
     auto* pos=reinterpret_cast<WINDOWPOS*>(l);
     if(!(pos->flags&SWP_NOMOVE)){pos->x=s->panel.left;pos->y=s->panel.top;}
     if(!(pos->flags&SWP_NOSIZE)){pos->cx=s->panel.right-s->panel.left;pos->cy=s->panel.bottom-s->panel.top;}
   }
   if(m==WM_WINDOWPOSCHANGED&&s&&!s->inputSuspended&&s->owner&&s->owner->current.get()==s&&s->inputWindow==h){
     RECT actual{};
     // SWP_NOSENDCHANGING and a downstream WndProc can bypass the pre-guard.
     // Withdraw synchronously; do not wait for the next telemetry frame.
     if(!GetWindowRect(h,&actual)||!EqualRect(&actual,&s->panel)){
       auto state=s->owner->current;s->owner->failInteraction(state,"drag-input-placement-failed");return 0;
     }
   }
   if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_POINTERACTIVATE)return PA_NOACTIVATE;
   if(m==WM_TOUCHHITTESTING){
     const auto* input=reinterpret_cast<const TOUCH_HIT_TESTING_INPUT*>(l);RECT actual{};
     // A fingertip ellipse can overlap the panel while the touch POINT is
     // outside it. Reject during target selection, not after consuming DOWN.
     if(!input||!s||s->inputSuspended||!s->owner||s->owner->current.get()!=s||
        !s->input.active()||!IsWindowVisible(h)||!IsWindowVisible(s->hwnd)||!GetWindowRect(h,&actual)||
        !PtInRect(&actual,input->point)||!PtInRect(&s->panel,input->point))return hudinput::rejectTouchHitTest(l);
     return DefWindowProcW(h,m,w,l);
   }
   if(windowMouse(s,h,m,w,l))return 0;
   if(m==WM_MOUSELEAVE){
     // Window movement can queue a stale LEAVE. Keep a drag only while the
     // physical cursor is still over its current bounded input rectangle.
     if(s&&s->mouseContact){POINT point{};RECT rect{};
       if(!GetCursorPos(&point)||!GetWindowRect(h,&rect)||!PtInRect(&rect,point))windowMouse(s,h,WM_CAPTURECHANGED,0,0);
     }return 0;
   }
   if(m==WM_POINTERCAPTURECHANGED){
     if(s&&s->owner&&s->owner->current.get()==s){
       if(s->mouseContact&&s->mousePointerId==GET_POINTERID_WPARAM(w))windowMouse(s,h,WM_CAPTURECHANGED,0,l);
       else if(s->touchContact&&s->touchPointerId==GET_POINTERID_WPARAM(w)){
         const auto id=s->touchPointerId;s->touchContact=false;
         inputEvent(h,{10,id,s->touchType,screeninput::Phase::Cancel,0,0,true},s);
       }
     }return 0;
   }
   if(m==WM_POINTERDOWN||m==WM_POINTERUPDATE||m==WM_POINTERUP){
     if(!s||!s->owner||s->inputSuspended)return 0;
     const auto pointerId=GET_POINTERID_WPARAM(w);
     const auto cancelUnreadableContact=[&]{
       // UP/UPDATE can outlive its native pointer metadata. Retire only this
       // known contact; never strand a drag or cancel a different pointer.
       const auto state=s->owner->current;
       if(state.get()==s&&s->inputWindow==h&&s->touchContact&&s->touchPointerId==pointerId)s->owner->stopInteraction(state);
     };
     POINTER_INFO pointer{};if(!GetPointerInfo(pointerId,&pointer)){cancelUnreadableContact();return 0;}
     POINT point=pointer.ptPixelLocation;if(!ScreenToClient(h,&point)){cancelUnreadableContact();return 0;}
     if(pointer.pointerType==PT_MOUSE){
       // Use Windows' legacy mouse compatibility path. Handling PT_MOUSE as a
       // pointer gives it implicit capture even when GetCapture() is null.
       // The legacy consumer below never calls SetCapture.
       return DefWindowProcW(h,m,w,l);
     }else if(pointer.pointerType==PT_TOUCH||pointer.pointerType==PT_PEN){
       const bool cancelled=(pointer.pointerFlags&POINTER_FLAG_CANCELED)!=0;
       if(m==WM_POINTERDOWN){
         if(cancelled||s->touchContact||s->mouseContact||point.x<0||point.y<0||point.x>=s->width||point.y>=s->height)return 0;
         s->touchContact=true;s->touchPointerId=pointer.pointerId;s->touchType=pointer.pointerType;
       }else if(!s->touchContact||s->touchPointerId!=pointer.pointerId)return 0;
       const auto phase=cancelled?screeninput::Phase::Cancel:m==WM_POINTERDOWN?screeninput::Phase::Down:m==WM_POINTERUP?screeninput::Phase::Up:screeninput::Phase::Move;
       if(phase==screeninput::Phase::Up||cancelled)s->touchContact=false;
       inputEvent(h,{10,pointer.pointerId,pointer.pointerType,phase,(float)point.x,(float)point.y,cancelled,pointer.dwTime,true},s);
     }
     return 0; // Never promote a handled pointer into a second click.
   }
   if(m==WM_ERASEBKGND)return 1;if(m==WM_PAINT){PAINTSTRUCT p;BeginPaint(h,&p);EndPaint(h,&p);return 0;}
   return DefWindowProcW(h,m,w,l);
 }
 static void inputEvent(HWND source,const screeninput::Event& e,void* context){
   if(e.type!=PT_TOUCH&&e.type!=PT_PEN&&e.type!=PT_MOUSE)return;
   auto* raw=static_cast<State*>(context);if(!raw||raw->inputSuspended||!raw->owner)return;
   auto* owner=raw->owner;auto s=owner->current;if(!s||s.get()!=raw||!owner->owns(s)||!IsWindowVisible(s->hwnd)||!s->inputWindow||source!=s->inputWindow||!IsWindowVisible(s->inputWindow))return;
   POINT p{(LONG)std::lround(e.x),(LONG)std::lround(e.y)};if(!ClientToScreen(s->inputWindow,&p))return;
   if(e.phase==screeninput::Phase::Down&&(e.x<0||e.y<0||e.x>=s->width||e.y>=s->height))return;
   const auto tick=GetTickCount64();const auto at=huddrag::sourceEventTick(tick,e.sourceTime,e.timeKnown);const bool cancelled=e.cancelled||e.phase==screeninput::Phase::Cancel;
   const bool wheelHold=owner->config.radialSummon&&owner->config.radialHoldEnabled&&owner->config.radialHoldEnabled();
   if(wheelHold&&e.phase==screeninput::Phase::Down&&!s->radialContact){s->radialContact=true;s->radialArmed=false;s->radialContactId=e.id;s->radialDownAt=at;s->radialDown=p;
     owner->send(s,{{"event","desktopHud.radialHold"},{"data",{{"active",true},{"duration",owner->config.radialHoldDuration?owner->config.radialHoldDuration():650}}}});
   }
   if(s->radialContact&&e.id==s->radialContactId&&(cancelled||std::hypot(double(p.x-s->radialDown.x),double(p.y-s->radialDown.y))>=7)){s->radialContact=false;s->radialArmed=false;KillTimer(s->hwnd,holdTimer);owner->send(s,{{"event","desktopHud.radialHold"},{"data",{{"active",false}}}});}
   // A second contact cannot finish or replace the monitor hold owner.
   if(s->radialContact&&e.id!=s->radialContactId)return;
   if(e.phase==screeninput::Phase::Down){if(s->drag.begin(e.id,e.type==PT_TOUCH||e.type==PT_PEN,p.x,p.y,s->panel.left,s->panel.top,at)){if(wheelHold)SetTimer(s->hwnd,holdTimer,owner->config.radialHoldDuration?owner->config.radialHoldDuration():650,nullptr);s->dragHaptic.begin((float)p.x,(float)p.y,s->width,s->height,at);if(e.type!=PT_MOUSE&&owner->config.haptic)owner->config.haptic(true,true);}else if(wheelHold&&s->radialContact)SetTimer(s->hwnd,holdTimer,owner->config.radialHoldDuration?owner->config.radialHoldDuration():650,nullptr);return;}
   if(e.phase==screeninput::Phase::Move&&!cancelled){
     const auto a=s->placementArea;auto r=s->drag.move(e.id,p.x,p.y,a.left,a.top,a.right-a.left-s->width,a.bottom-a.top-s->height,s->width,s->height);
     if(r.started)KillTimer(s->hwnd,holdTimer);
     if(e.type!=PT_MOUSE&&r.moved&&s->dragHaptic.move((float)p.x,(float)p.y,at)&&owner->config.haptic)owner->config.haptic(true,true);
     if(r.moved){owner->refresh(s);owner->interaction(s);}return;
   }
   if(wheelHold&&s->radialContact&&e.id==s->radialContactId&&e.phase==screeninput::Phase::Up&&!cancelled&&at>=s->radialDownAt&&at-s->radialDownAt>=(owner->config.radialHoldDuration?owner->config.radialHoldDuration():650)){
     s->radialContact=false;s->radialArmed=false;KillTimer(s->hwnd,holdTimer);s->drag.cancel();owner->send(s,{{"event","desktopHud.radialHold"},{"data",{{"active",false}}}});if(owner->config.haptic)owner->config.haptic(false,false);owner->config.radialSummon(s->panel.left+(s->panel.right-s->panel.left)/2,s->panel.top+(s->panel.bottom-s->panel.top)/2);return;
   }
   s->radialContact=false;s->radialArmed=false;owner->send(s,{{"event","desktopHud.radialHold"},{"data",{{"active",false}}}});
   auto r=s->drag.end(e.id,p.x,p.y,at,cancelled,GetDoubleClickTime());
   if(!s->drag.contact())KillTimer(s->hwnd,holdTimer);
   if(r.switchTemplate){
     s->activeTemplate=s->activeTemplate=="A"?"B":"A";s->templateRevision=std::max(s->templateRevision+1,wallNow());
     if(owner->config.notify)owner->config.notify("desktopHud.template",{{"id",s->id},{"nonce",s->nonce},{"seq",s->seq},{"template",s->activeTemplate},{"revision",s->templateRevision}});
   }

   if(r.ended){owner->interaction(s,false,true);owner->refresh(s);}
   if(!s->drag.contact()){s->dragHaptic={};if(owner->config.haptic)owner->config.haptic(false,false);}
 }
 void failInteraction(const std::shared_ptr<State>& s,const std::string& reason){
   s->inputError=reason;s->inputRecovery.failed();stopInteraction(s,true);
   if(config.notify){Json value={{"id",s->id},{"nonce",s->nonce},{"reason",reason},{"attempts",s->inputRecovery.failures()}};
     if(s->inputWithdrawal.is_object()){try{value["inputWithdrawal"]=s->inputWithdrawal;}catch(...){}}
     config.notify("desktopHud.inputUnavailable",value);
   }
 }
 void syncInteraction(const std::shared_ptr<State>& s){
   if(s->notice||!config.inputRuntime||!s->inputRecovery.available())return;
   if(s->drag.position.locked&&!(config.radialHoldEnabled&&config.radialHoldEnabled())){stopInteraction(s);return;}
   if(!s->inputWindow){
     WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=inputProc;c.lpszClassName=L"YMCC_MonitorDragInput";
     if(!RegisterClassW(&c)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){failInteraction(s,"drag-class-unavailable");return;}
     s->inputWindow=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,c.lpszClassName,L"",WS_POPUP,
       s->panel.left,s->panel.top,s->width,s->height,nullptr,nullptr,c.hInstance,s.get());
     if(!s->inputWindow){failInteraction(s,"drag-window-unavailable");return;}
     if(!s->input.attach(s->inputWindow)){failInteraction(s,"drag-input-unavailable-"+std::to_string((long)s->input.error));return;}
   }
   // Native pointer/mouse coordinates belong to the current HWND frame.
   // Follow the contact without capturing the desktop or creating an input site.
   const HWND inputWindow=s->inputWindow;
   RECT before{};GetWindowRect(s->inputWindow,&before);
   const bool size=before.right-before.left!=s->width||before.bottom-before.top!=s->height;
   if(before.left!=s->panel.left||before.top!=s->panel.top||size||!IsWindowVisible(s->inputWindow)){
     if(!SetWindowPos(s->inputWindow,HWND_TOPMOST,s->panel.left,s->panel.top,s->width,s->height,SWP_NOACTIVATE|SWP_SHOWWINDOW)){
       failInteraction(s,"drag-input-placement-failed");return;
     }
     if(s->inputWindow!=inputWindow)return; // Native placement callback already withdrew it.
     RECT applied{};if(!GetWindowRect(s->inputWindow,&applied)||!EqualRect(&applied,&s->panel)){
       failInteraction(s,"drag-input-placement-failed");return;
     }
   }else if(GetWindow(s->hwnd,GW_HWNDPREV)!=s->inputWindow&&
       !SetWindowPos(s->inputWindow,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)){
     failInteraction(s,"drag-input-placement-failed");return;
   }
   if(s->inputWindow!=inputWindow)return;
   // No COM dispatcher drain or independent input callbacks in the monitor path.
   const bool inputReady=s->input.ensureMousePass();
   if(s->inputWindow!=inputWindow)return;
   if(!inputReady){failInteraction(s,"drag-input-repair-failed");return;}
   // A layered upload may also change native window geometry. Verify the
   // final input rectangle after all native work, before returning to input.
   RECT finalRect{};if(!GetWindowRect(s->inputWindow,&finalRect)||!EqualRect(&finalRect,&s->panel))failInteraction(s,"drag-input-placement-failed");
 }
 void refresh(const std::shared_ptr<State>& s,bool raise=false){
   if(!owns(s))return;
   if(radialSuspended){stopInteraction(s);if(!radialHideAt||GetTickCount64()>=radialHideAt)hideOutput(s.get());return;}
   // Revalidate before showing/raising, including returning-game and data events.
   // Fail closed: an invalid display HWND may not become an input blocker.
   if(!hudinput::ensure(s->hwnd)){s->armed=false;s->reason="input-transparency-lost";hideOutput(s.get());emit(s,"desktopHud.unavailable");return;}
   if(s->ready){
     const auto viewport=viewportInfo(*s);const auto key=viewport.dump();
     if(key!=s->viewportKey){s->viewportKey=key;s->layout=false;hideOutput(s.get(),!s->armed);send(s,{{"event","desktopHud.viewport"},{"data",viewport}});return;}
   }
   RECT r{};const HWND foreground=GetForegroundWindow();
   const bool eligible=s->ready&&s->armed&&s->layout&&s->expires>wallNow()&&clientArea(*s,r)&&
     (s->global||foreground==s->target->window||GetAncestor(foreground,GA_ROOT)==s->target->window);
   if(!eligible){hideOutput(s.get(),!s->ready||!s->armed||s->expires<=wallNow());return;}
   if(!policyPermits(*s,foreground)){
     if(IsWindowVisible(s->hwnd))hideOutput(s.get());
     s->reason=s->visibility==hudvisibility::Mode::GameRunning?"waiting-game":"waiting-game-focus";
     if(!s->policyWaiting){s->policyWaiting=true;++s->visibilityRevision;emit(s,"desktopHud.prepared");}
     return;
   }
   const bool resumed=s->policyWaiting;s->policyWaiting=false;s->reason="active";
   RECT monitor=r;if(s->global&&!monitorArea(*s,monitor))return;
   const auto area=[](const RECT& v){return hudplacement::Area{static_cast<int>(v.left),static_cast<int>(v.top),static_cast<int>(v.right),static_cast<int>(v.bottom)};};
   auto p=hudplacement::place(area(r),area(monitor),!s->global,s->width,s->height,s->position);
   s->placementArea={p.area.left,p.area.top,p.area.right,p.area.bottom};
   if(s->drag.position.placed){p.x=p.area.left+(int)std::min(std::max(0,p.area.right-p.area.left-s->width),(int)std::lround(s->drag.position.x*(p.area.right-p.area.left)));p.y=p.area.top+(int)std::min(std::max(0,p.area.bottom-p.area.top-s->height),(int)std::lround(s->drag.position.y*(p.area.bottom-p.area.top)));}
   s->panel={p.x,p.y,p.x+s->width,p.y+s->height};
   int outputWidth=s->width,outputHeight=s->height;
   if(s->crosshair){
     RECT full{},work{};if(!screenRects(*s,full,work))return;
     // Use rcMonitor, never rcWork: taskbar insets cannot move the aiming point.
     p.area=area(full);p.x=full.left;p.y=full.top;p.fits=hudplacement::fits(area(work),s->width,s->height);
     outputWidth=full.right-full.left;outputHeight=full.bottom-full.top;
   }
   if(!p.fits){s->reason=s->global?"content-exceeds-global-viewport":"content-exceeds-game-window";hideOutput(s.get());emit(s,"desktopHud.unavailable");return;}
   RECT old{};GetWindowRect(s->hwnd,&old);
   const bool geometryChanged=old.left!=p.x||old.top!=p.y||old.right-old.left!=outputWidth||old.bottom-old.top!=outputHeight||!IsWindowVisible(s->hwnd);
   if(geometryChanged){
     SetWindowPos(s->hwnd,HWND_TOPMOST,p.x,p.y,outputWidth,outputHeight,SWP_NOACTIVATE|SWP_SHOWWINDOW);
     if(s->controller){s->controller->put_IsVisible(TRUE);s->controller->NotifyParentWindowPositionChanged();}if(s->device)s->device->Commit();
   }else if(hudplacement::needsRaise(s->global,raise,foregroundOccludes(*s,foreground))){
     // A returning borderless game can promote itself AFTER the foreground
     // event. Repair on that event or a confirmed overlap in the existing
     // data cadence; never raise an already-front HUD or steal game input.
     SetWindowPos(s->hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
   }
   // A rejected or altered display placement must not leave a live input
   // panel at coordinates where the rendered monitor no longer exists.
   if(!displayPlacementMatches(*s)){
     s->reason="display-placement-failed";hideOutput(s.get());emit(s,"desktopHud.unavailable");return;
   }
   syncInteraction(s);
   if(resumed&&IsWindowVisible(s->hwnd)){++s->visibilityRevision;emit(s,"desktopHud.visibility");}
 }
 void send(const std::shared_ptr<State>& s,const Json& value){if(s->view&&!s->closed)s->view->PostWebMessageAsJson(wide(value.dump()).c_str());}
 bool admitted(const State& s,const Json& a){return a.is_object()&&a.value("hudId",0)==s.id&&a.value("nonce",std::string{})==s.nonce;}
 void receive(const std::shared_ptr<State>& s,ICoreWebView2WebMessageReceivedEventArgs* args){
   if(!owns(s)||!args)return;LPWSTR raw=nullptr,source=nullptr;
   if(FAILED(args->get_Source(&source))||!source)return;
   const bool same=std::wstring(source)==wide(s->url);CoTaskMemFree(source);if(!same)return;
   if(FAILED(args->get_WebMessageAsJson(&raw))||!raw)return;
   std::wstring text(raw);CoTaskMemFree(raw);if(text.size()>65536)return;
   Json response;
   try{
     const Json req=Json::parse(utf8(text));response["id"]=req.value("id",-1);const auto cmd=req.value("cmd",std::string{});const auto a=req.value("args",Json::object());
     if(!admitted(*s,a))throw std::runtime_error("HUD renderer ownership denied");
     if(cmd=="desktopHud.ready"){
       s->ready=true;s->reason="renderer-ready";RECT r{};clientArea(*s,r);if(s->notice){s->crosshair=true;KillTimer(s->hwnd,leaseTimer);}
       response["result"]=viewportInfo(*s);s->viewportKey=response["result"].dump();
       emit(s,"desktopHud.ready");
     }else if(cmd=="desktopHud.layout"){
       const double w=a.value("width",0.0),h=a.value("height",0.0);const int p=a.value("position",1);
       if(!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0||w>16384||h>16384||p<1||p>6)throw std::runtime_error("Invalid HUD layout");
       const auto layoutSeq=a.value("seq",0ULL);
       // A ResizeObserver callback can race a newer source/appearance frame.
       // Ignore an OLD layout; it is not a renderer failure or a reason to
       // destroy a valid HWND while changing from bar back to corner.
       if(layoutSeq<s->seq){response["result"]=true;send(s,response);return;}
       if(layoutSeq!=s->seq||!s->seq)throw std::runtime_error("Invalid layout receipt");
       s->width=(int)std::ceil(w);s->height=(int)std::ceil(h);s->position=p;s->layout=true;refresh(s);response["result"]=true;
       if(IsWindowVisible(s->hwnd)&&s->paintedSeq!=s->seq){s->paintedSeq=s->seq;emit(s,"desktopHud.painted");}
     }else if(cmd=="desktopHud.close"){
       response["result"]=true;send(s,response);close(s->id,s->nonce,"renderer-close");return;
     }else throw std::runtime_error("Desktop HUD is display-only; command denied");
   }catch(const std::exception& e){response["error"]=e.what();}
   send(s,response);
 }
 static void CALLBACK observed(HWINEVENTHOOK,DWORD event,HWND hwnd,LONG object,LONG child,DWORD,DWORD){
   for(auto* owner:eventOwners){if(!owner||!owner->current)continue;auto s=owner->current;
   if(!s->hwnd||s->closed||!hwnd||object!=OBJID_WINDOW||child!=CHILDID_SELF)continue;
   const bool ownHud=hwnd==s->hwnd||IsChild(s->hwnd,hwnd)||hwnd==s->inputWindow||(s->inputWindow&&IsChild(s->inputWindow,hwnd));
   const HWND foreground=GetForegroundWindow();const HWND root=foreground?GetAncestor(foreground,GA_ROOT):nullptr;
   const bool related=s->global?(hwnd==foreground||hwnd==root):hwnd==s->target->window;
   if(!hudplacement::relevant(ownHud,event==EVENT_SYSTEM_FOREGROUND,related))continue;
   s->queuedRaise=s->queuedRaise||event==EVENT_SYSTEM_FOREGROUND||event==EVENT_OBJECT_SHOW||event==EVENT_OBJECT_REORDER||event==EVENT_SYSTEM_MINIMIZEEND;
   if(!s->queued){s->queued=true;if(!PostMessageW(s->hwnd,updateMessage,0,0))s->queued=false;}
   }
 }
 static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
   auto* s=reinterpret_cast<State*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
   if(msg==WM_NCCREATE){s=reinterpret_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
   if(s){
     if(msg==WM_TIMER&&w==holdTimer){KillTimer(hwnd,holdTimer);if(s->owner&&s->owner->current.get()==s){auto* owner=s->owner;
       if(s->radialContact&&owner->config.radialHoldEnabled&&owner->config.radialHoldEnabled()){
         const auto duration=owner->config.radialHoldDuration?owner->config.radialHoldDuration():650;
         const auto elapsed=GetTickCount64()-s->radialDownAt;
         if(elapsed<duration){SetTimer(hwnd,holdTimer,(UINT)(duration-elapsed),nullptr);return 0;}
         // Reaching the threshold only arms this contact. The matching UP
         // owns the summon; movement, cancellation and hide can still revoke it.
         if(!s->radialArmed){s->radialArmed=true;owner->send(owner->current,{{"event","desktopHud.radialHold"},{"data",{{"active",true},{"ready",true},{"duration",duration}}}});}
       }}
       return 0;}
     if(msg==WM_SHOWWINDOW&&!w&&s->owner&&s->owner->current.get()==s){suspendRenderer(s);s->owner->stopInteraction(s->owner->current);}
     if(msg==WM_WINDOWPOSCHANGED&&s->ready&&s->layout&&!s->closed&&s->inputWindow&&IsWindowVisible(s->inputWindow)&&s->owner&&s->owner->current.get()==s&&!displayPlacementMatches(*s)){
       // Display and input are independent top-level HWNDs. An external move
       // invalidates the old hit region immediately, even between data frames.
       s->owner->stopInteraction(s->owner->current);
     }
     if(msg==WM_DISPLAYCHANGE||msg==WM_DPICHANGED||msg==WM_SETTINGCHANGE){if(s->owner&&s->owner->current.get()==s)s->owner->refresh(s->owner->current,true);return 0;}
     if(msg==WM_STYLECHANGING&&hudinput::guardStyle(w,l))return 0;
     if(msg==WM_STYLECHANGED||msg==WM_ENABLE){
       if(!hudinput::ensure(hwnd)){hideOutput(s);s->armed=false;s->reason="input-transparency-lost";}
       return 0;
     }
     if(msg==WM_SETFOCUS||msg==WM_ACTIVATE)return 0; // Never pass activation into the renderer.
     if(msg==WM_NCHITTEST)return HTTRANSPARENT;
     if(msg==WM_TOUCHHITTESTING)return hudinput::rejectTouchHitTest(l);
     if(msg==WM_POINTERACTIVATE)return PA_NOACTIVATE;
     if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
     if(msg==WM_ERASEBKGND)return 1;
     if(msg==WM_SIZE&&s->controller){RECT r{};GetClientRect(hwnd,&r);s->controller->put_Bounds(r);if(s->device)s->device->Commit();return 0;}
     if(msg==updateMessage&&s->failed){if(s->owner&&s->owner->current.get()==s)s->owner->close(s->id,s->nonce,s->reason);return 0;}
     if(msg==updateMessage){s->queued=false;const bool raise=s->queuedRaise;s->queuedRaise=false;if(s->owner&&s->owner->current.get()==s)s->owner->refresh(s->owner->current,raise);return 0;}
     if(msg==WM_TIMER&&w==radialFadeTimer){KillTimer(hwnd,radialFadeTimer);if(s->owner&&s->owner->radialSuspended){
       const auto now=GetTickCount64(),deadline=s->owner->radialHideAt;
       if(deadline>now){if(!SetTimer(hwnd,radialFadeTimer,(UINT)(deadline-now),nullptr))hideOutput(s);}
       else hideOutput(s);
     }return 0;}
     if(msg==WM_TIMER&&w==leaseTimer&&s->failed){if(s->owner&&s->owner->current.get()==s)s->owner->close(s->id,s->nonce,s->reason);return 0;}
     if(msg==WM_TIMER&&w==leaseTimer&&s->notice&&!s->ready){s->owner->close(s->id,s->nonce,"notice-renderer-ready-timeout");return 0;}
     if(msg==WM_TIMER&&w==leaseTimer){KillTimer(hwnd,leaseTimer);s->armed=false;s->reason="parent-data-lease-expired";hideOutput(s);if(s->notice)s->owner->send(s->owner->current,{{"event","monitorNotice.hide"},{"data",Json::object()}});if(s->owner&&s->owner->current.get()==s)s->owner->emit(s->owner->current,"desktopHud.expired");return 0;}
     if(msg==WM_CLOSE){if(s->owner)s->owner->close(s->id,s->nonce,"window-close");return 0;}
     if(msg==WM_DESTROY){
       auto* owner=s->owner;auto state=owner&&owner->current.get()==s?owner->current:std::shared_ptr<State>{};
       // The display and input are independent top-level HWNDs. Losing the
       // display must retire both before a later native callback can run.
       s->hwnd=nullptr;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
       if(state&&!state->closed){owner->recovery.failed();owner->close(state->id,state->nonce,"display-window-destroyed");}
       return 0;
     }
   }
   return DefWindowProcW(hwnd,msg,w,l);
 }
 void unhook(){eventOwners.erase(std::remove(eventOwners.begin(),eventOwners.end(),this),eventOwners.end());if(eventOwners.empty()){for(auto h:sharedHooks)UnhookWinEvent(h);sharedHooks.clear();}}
 void hook(){if(std::find(eventOwners.begin(),eventOwners.end(),this)!=eventOwners.end())return;eventOwners.push_back(this);if(!sharedHooks.empty())return;for(auto range:{std::pair<DWORD,DWORD>{EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND},
     {EVENT_SYSTEM_MINIMIZESTART,EVENT_SYSTEM_MINIMIZEEND},{EVENT_OBJECT_DESTROY,EVENT_OBJECT_DESTROY},{EVENT_OBJECT_SHOW,EVENT_OBJECT_SHOW},{EVENT_OBJECT_REORDER,EVENT_OBJECT_REORDER},{EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE}}){
    auto h=SetWinEventHook(range.first,range.second,nullptr,observed,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    if(!h){unhook();throw std::runtime_error("Desktop HUD foreground watcher failed");}sharedHooks.push_back(h);
 }}
public:
 void suspendForRadial(bool value){
  if(radialSuspended==value)return;radialSuspended=value;radialHideAt=value?GetTickCount64()+240:0;
  if(current){auto s=current;KillTimer(s->hwnd,radialFadeTimer);
   if(value)stopInteraction(s);
   send(s,{{"event","desktopHud.radialTransition"},{"data",{{"open",value},{"durationMs",240}}}});
   if(value&&IsWindowVisible(s->hwnd)&&!SetTimer(s->hwnd,radialFadeTimer,240,nullptr))radialHideAt=0;
   refresh(s);
  }
 }
 void initialize(ICoreWebView2Environment* env,Config cfg){environment=env;config=std::move(cfg);}
 bool available()const{return environment&&bool(config.serve)&&recovery.available();}
 // One monotonic native lease, constant storage. Old queued creates cannot
 // regain admission after retirement or after a newer lease replaces them.
 Json reserveNotice(const std::string& nonce){
   if(!config.noticeOnly||!tokenValid(nonce)||current)throw std::runtime_error("Notice owner reservation rejected");
   if(noticeLeaseSerial>=9007199254740991ULL)throw std::runtime_error("Notice owner generation exhausted");
   ++noticeLeaseSerial;noticeLeaseNonce=nonce;noticeLeaseRetired=false;
   return {{"leaseId",noticeLeaseSerial},{"nonce",nonce}};
 }
 Json retireNotice(unsigned long long lease,const std::string& nonce){
   if(!config.noticeOnly||!lease||!tokenValid(nonce))throw std::runtime_error("Invalid notice retirement");
   const bool obsolete=lease<noticeLeaseSerial;
   if(obsolete)return {{"leaseRetired",true},{"resourcesDestroyed",true},{"ownedOnly",true},{"obsolete",true}};
   if(lease!=noticeLeaseSerial||nonce!=noticeLeaseNonce)return {{"leaseRetired",false},{"resourcesDestroyed",false},{"ownedOnly",true}};
   noticeLeaseRetired=true; // Revoke create admission BEFORE any destruction.
   if(current&&(current->noticeLease!=lease||current->nonce!=nonce))return {{"leaseRetired",true},{"resourcesDestroyed",false},{"ownedOnly",true}};
   const bool destroyed=!current||close(current->id,current->nonce,"notice-owner-retired");
   return {{"leaseRetired",true},{"resourcesDestroyed",destroyed},{"ownedOnly",true},{"obsolete",false}};
 }
 Json create(DWORD pid,ULONGLONG birth,const std::string& nonce,bool global=false,unsigned long long lease=0){
   if(config.noticeOnly&&(!lease||lease!=noticeLeaseSerial||nonce!=noticeLeaseNonce||noticeLeaseRetired))throw std::runtime_error("Notice owner lease retired or replaced");
   if(!available())throw std::runtime_error(recovery.available()?"Native HUD WebView2 environment unavailable":"Native HUD recovery stopped after repeated renderer failures");
   if(current)throw std::runtime_error("A desktop HUD is already owned");if(!tokenValid(nonce))throw std::runtime_error("Invalid HUD nonce");
   auto s=std::make_shared<State>();s->owner=this;s->id=nextId++;s->nonce=nonce;s->global=global;s->notice=config.noticeOnly;s->noticeLease=s->notice?lease:0;
   if(global)s->monitor=MonitorFromWindow(config.anchor,MONITOR_DEFAULTTOPRIMARY);else s->target=bindTarget(pid,birth);
   s->url=std::string("https://app.localhost/")+(s->notice?"notice-output.html":"hud-output.html")+"?hudId="+std::to_string(s->id)+"&nonce="+nonce;
   WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YMCC_DesktopMonitorHUD";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
   if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("HUD class registration failed");
   // Deliberately NO Win32 owner: main YMCC minimization must not hide this HUD.
   RECT client{};clientArea(*s,client);const int width=std::max(1L,client.right-client.left),height=std::max(1L,client.bottom-client.top);
   s->hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT,cls.lpszClassName,L"YMCC Monitor HUD",WS_POPUP|WS_DISABLED,
      client.left,client.top,width,height,nullptr,nullptr,cls.hInstance,s.get());
   if(!s->hwnd)throw std::runtime_error("HUD HWND creation failed");
   if(!hudinput::ensure(s->hwnd)){DestroyWindow(s->hwnd);throw std::runtime_error("HUD input transparency unavailable");}
   if(!hudinput::registerTouchPassthrough(s->hwnd)){DestroyWindow(s->hwnd);throw std::runtime_error("HUD touch hit testing unavailable");}current=s;
   try{hook();}catch(...){close(s->id,nonce,"hook-failed");throw;}
   // A create/renderer failure cannot leave an invisible orphan indefinitely.
   SetTimer(s->hwnd,leaseTimer,12000,nullptr);
   ComPtr<ICoreWebView2Environment3> env3;
   if(FAILED(environment.As(&env3))){fail(s,"composition-environment-unavailable");throw std::runtime_error("Windowless WebView2 unsupported");}
   const HRESULT hr=env3->CreateCoreWebView2CompositionController(s->hwnd,Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
    [weak=std::weak_ptr<State>(s)](HRESULT hr,ICoreWebView2CompositionController* composition)->HRESULT{
     const auto s=weak.lock();
     ComPtr<ICoreWebView2Controller> controller;if(composition)composition->QueryInterface(IID_PPV_ARGS(&controller));
     // Creation can complete after OFF/close. Revoke the controller input site
     // before either attaching its visual or retiring a late callback.
     if(!s||!s->owner||!s->owner->owns(s)){if(controller)controller->Close();return S_OK;}
     auto* owner=s->owner;
     if(controller&&FAILED(controller->put_IsVisible(FALSE))){controller->Close();owner->fail(s,"renderer-input-suspension-failed");return S_OK;}
     if(FAILED(hr)||!controller){owner->fail(s,"webview-create-failed");return S_OK;}
     s->composition=composition;s->controller=controller;
     // A true GPU-composited transparent root, not a windowed renderer trapped
     // inside WS_EX_LAYERED (which can produce a black rectangle).
     if(FAILED(DCompositionCreateDevice(nullptr,IID_PPV_ARGS(&s->device)))||
       FAILED(s->device->CreateTargetForHwnd(s->hwnd,TRUE,&s->visualTarget))||FAILED(s->device->CreateVisual(&s->visual))||
       FAILED(s->visualTarget->SetRoot(s->visual.Get()))||FAILED(composition->put_RootVisualTarget(s->visual.Get()))||FAILED(s->device->Commit())){
       owner->fail(s,"composition-target-failed");return S_OK;
     }
     if(FAILED(s->controller->get_CoreWebView2(&s->view))||!s->view){owner->fail(s,"webview-instance-unavailable");return S_OK;}RECT r{};GetClientRect(s->hwnd,&r);controller->put_Bounds(r);
     ComPtr<ICoreWebView2Controller2> c2;ComPtr<ICoreWebView2Controller3> c3;
     if(FAILED(controller->QueryInterface(IID_PPV_ARGS(&c2)))||FAILED(controller->QueryInterface(IID_PPV_ARGS(&c3)))||
       FAILED(c2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{0,0,0,0}))||FAILED(c3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS))||FAILED(c3->put_ShouldDetectMonitorScaleChanges(FALSE))||FAILED(c3->put_RasterizationScale(1.0))||!owner->config.serve(s->view.Get())){
       owner->fail(s,"transparent-renderer-not-supported");return S_OK;
     }
     ComPtr<ICoreWebView2Settings> settings;s->view->get_Settings(&settings);if(settings){settings->put_AreDefaultContextMenusEnabled(FALSE);settings->put_AreDevToolsEnabled(FALSE);ComPtr<ICoreWebView2Settings3> s3;if(SUCCEEDED(settings.As(&s3)))s3->put_AreBrowserAcceleratorKeysEnabled(FALSE);settings->put_IsZoomControlEnabled(FALSE);}controller->put_ZoomFactor(1.0);controller->put_IsVisible(FALSE);
     EventRegistrationToken token{};
     // Reuse the weak creation lease; event callbacks never retain State.
     const HRESULT failureHook=s->view->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
      [weak](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs* args)->HRESULT{
       const auto state=weak.lock();if(!state||!state->owner||!args)return S_OK;
       COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
       if(FAILED(args->get_ProcessFailedKind(&kind))||kind==COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED||
          kind==COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED||kind==COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE)
         state->owner->fail(state,"webview-process-failed-"+std::to_string(static_cast<int>(kind)));
       return S_OK;
      }).Get(),&token);
     if(FAILED(failureHook)){owner->fail(s,"webview-failure-observer-unavailable");return S_OK;}
     s->view->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([weak](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* a)->HRESULT{const auto s=weak.lock();if(!s||!s->owner)return S_OK;LPWSTR uri=nullptr;if(SUCCEEDED(a->get_Uri(&uri))&&uri){if(std::wstring(uri)!=wide(s->url))a->put_Cancel(TRUE);CoTaskMemFree(uri);}return S_OK;}).Get(),&token);
     s->view->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* a)->HRESULT{a->put_Handled(TRUE);return S_OK;}).Get(),&token);
     s->view->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([weak](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* a)->HRESULT{const auto s=weak.lock();if(s&&s->owner)s->owner->receive(s,a);return S_OK;}).Get(),&token);
     if(FAILED(s->view->Navigate(wide(s->url).c_str())))owner->fail(s,"webview-navigation-rejected");return S_OK;
    }).Get());
   if(FAILED(hr)){fail(s,"webview-request-failed");throw std::runtime_error("WebView2 request rejected");}
   return {{"id",s->id},{"nonce",nonce},{"width",width},{"height",height},{"targetPid",pid},{"targetCreated",std::to_string(birth)},
     {"mode",global?"desktop-global":"desktop-window-or-borderless"},{"exclusiveFullscreenSupported",false},{"inputTransparent",true}};
 }
 Json occupied(HMONITOR monitor)const {
   if(!current||!current->hwnd||!IsWindowVisible(current->hwnd)||!current->armed)return Json::array();
   RECT full{},work{};if(!screenRects(*current,full,work))return Json::array();MONITORINFO other{sizeof(other)};
   if(!GetMonitorInfoW(monitor,&other)||other.rcMonitor.left!=full.left||other.rcMonitor.top!=full.top)return Json::array();
   RECT rect{};GetWindowRect(current->hwnd,&rect);
   if(current->crosshair){rect=current->panel;}
   return Json::array({{{"x",rect.left-full.left},{"y",rect.top-full.top},{"width",rect.right-rect.left},{"height",rect.bottom-rect.top}}});
 }
 bool hasPointerContact()const{return current&&(current->touchContact||current->mouseContact||current->drag.contact()||current->radialContact||current->radialArmed);}
 HMONITOR display()const{return current?current->monitor:MonitorFromWindow(config.anchor,MONITOR_DEFAULTTOPRIMARY);}
 bool noticeHide(int id,const std::string& nonce,unsigned long long sequence=0){auto s=current;if(!s||!s->notice||s->id!=id||s->nonce!=nonce||sequence<s->seq)return false;s->armed=false;KillTimer(s->hwnd,leaseTimer);hideOutput(s.get());send(s,{{"event","monitorNotice.hide"},{"data",Json::object()}});return true;}
 bool noticeFrame(int id,const std::string& nonce,const Json& payload){
   auto s=current;if(!s||!s->notice||s->id!=id||s->nonce!=nonce||!owns(s)||!s->ready||!payload.is_object())return false;
   if(payload.dump().size()>8192||payload.value("type",std::string{})!="monitor-notice"||payload.value("protocol",0)!=1||payload.value("hudId",0)!=id||payload.value("nonce",std::string{})!=nonce||!payload.contains("feedback"))return false;
   const auto now=wallNow(),expires=payload.value("expiresAt",0ULL),sequence=payload.value("seq",0ULL);const auto& feedback=payload["feedback"];
   if(!feedback.is_object()||expires<=now||expires>now+10000||sequence<=s->seq||feedback.value("expiresAtUnixMs",0ULL)!=expires)return false;
   hideOutput(s.get());s->seq=sequence;s->expires=expires;s->armed=true;s->layout=false;s->crosshair=true;s->reason="transient-notice";
   // Resume only to measure this admitted frame. HWND stays hidden until its
   // matching layout receipt; stale geometry cannot flash during a new notice.
   if(s->controller&&FAILED(s->controller->put_IsVisible(TRUE))){s->armed=false;hideOutput(s.get());return false;}
   KillTimer(s->hwnd,leaseTimer);SetTimer(s->hwnd,leaseTimer,(UINT)(expires-now),nullptr);send(s,{{"event","monitorNotice.frame"},{"data",payload}});refresh(s);return true;
 }
 Json status()const{
   if(!current)return {{"leaseProtocol",config.noticeOnly?1:0},{"available",available()},{"rendererFailures",recovery.failures()},{"recoveryBlocked",!recovery.available()},{"id",0},{"ready",false},{"armed",false},{"visible",false},{"hiddenConfirmed",true}};
   auto s=current;RECT inputRect{};if(s->inputWindow)GetWindowRect(s->inputWindow,&inputRect);
   return {{"inputMode","native-panel-no-capture-v1"},{"inputBoundsPolicy","panel-point-v1"},{"dragInputVisible",s->inputWindow&&IsWindowVisible(s->inputWindow)},
     {"dragInputRect",{{"x",inputRect.left},{"y",inputRect.top},{"width",inputRect.right-inputRect.left},{"height",inputRect.bottom-inputRect.top}}},
     {"dragInputCapturesMouse",s->inputWindow&&GetCapture()==s->inputWindow},{"dragContact",s->drag.contact()},{"touchContact",s->touchContact},
     {"leaseProtocol",config.noticeOnly?1:0},{"leaseId",s->noticeLease},{"scope",s->global?"global":"game"},{"available",available()},{"rendererFailures",recovery.failures()},{"recoveryBlocked",!recovery.available()},{"id",s->id},{"nonce",s->nonce},{"ready",s->ready},{"armed",s->armed},{"visible",s->hwnd&&IsWindowVisible(s->hwnd)},
     {"hiddenConfirmed",!s->hwnd||!IsWindowVisible(s->hwnd)},{"targetAlive",alive(*s)},{"targetPid",s->target?s->target->pid:0},{"targetCreated",std::to_string(s->target?s->target->created:0)},
     {"reason",s->reason},{"inputTransparent",hudinput::inert(s->hwnd)},{"inputEnabled",s->hwnd&&IsWindowEnabled(s->hwnd)},
     {"pointerStyles",s->hwnd?(unsigned long long)GetWindowLongPtrW(s->hwnd,GWL_EXSTYLE):0ULL}};
 }
 bool frame(int id,const std::string& nonce,const Json& payload){
   auto s=current;if(!s||s->notice||s->id!=id||s->nonce!=nonce||!owns(s)||!s->ready||!payload.is_object())return false;
   const std::string body=payload.dump();if(body.size()>65536)return false;
   const auto now=wallNow(),expires=payload.value("expiresAt",0ULL),seq=payload.value("seq",0ULL);
   if(expires<=now||expires>now+7000||seq<=s->seq||payload.value("nonce",std::string{})!=nonce||payload.value("hudId",0)!=id||
      !payload.contains("data")||payload["data"].value("source",std::string{})!="ymcc-uniform")return false;
   if(!alive(*s)){s->armed=false;hideOutput(s.get());s->reason="target-exited";emit(s,"desktopHud.unavailable");return false;}
   const auto appearance=payload.value("preferences",Json::object()).value("ymcc",Json::object());
   huddrag::Position drag;drag.locked=appearance.value("dragLocked",false);drag.revision=appearance.value("dragRevision",0ULL);
   if(appearance.contains("dragX")&&appearance.contains("dragY")&&appearance["dragX"].is_number()&&appearance["dragY"].is_number()){
     drag.x=appearance["dragX"].get<double>();drag.y=appearance["dragY"].get<double>();drag.placed=std::isfinite(drag.x)&&std::isfinite(drag.y)&&drag.x>=0&&drag.x<=1&&drag.y>=0&&drag.y<=1;
   }
   // An explicit lock from the settings page cancels any current contact.
   if(drag.locked&&drag.revision>s->drag.position.revision)stopInteraction(s);
   s->drag.adopt(drag);
   const auto preferences=payload.value("preferences",Json::object());
   const auto templateRevision=preferences.value("ymccTemplateRevision",0ULL);
   if(templateRevision>=s->templateRevision){s->templateRevision=templateRevision;s->activeTemplate=preferences.value("ymccTemplate",std::string{"A"})=="B"?"B":"A";}
   s->visibility=hudvisibility::mode(appearance.value("visibility",std::string{"always"}));
   s->policyGamePid=s->visibility==hudvisibility::Mode::Always?0:hudvisibility::policyGamePid(payload);
   const auto crosshair=appearance.value("crosshair",std::string{"none"});
   s->crosshair=crosshair=="dot"||crosshair=="cross"||crosshair=="gap"||crosshair=="ring"||crosshair=="ring-dot";
   const auto shape=Json{{"crosshair",s->crosshair},{"layout",appearance.value("layout",std::string{})},{"scale",appearance.value("scale",1.0)},{"gap",appearance.value("gap",8)},
     {"fields",appearance.value("fields",Json::object())}}.dump();
   if(shape!=s->layoutKey){s->layoutKey=shape;s->layout=false;hideOutput(s.get());RECT r{};if(clientArea(*s,r))SetWindowPos(s->hwnd,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE|SWP_NOZORDER);}
   s->seq=seq;s->expires=expires;s->armed=payload.value("show",false);s->reason=s->armed?"active":"hidden";
   KillTimer(s->hwnd,leaseTimer);SetTimer(s->hwnd,leaseTimer,(UINT)std::max(1ULL,expires-now),nullptr);
   if(s->armed&&s->controller&&FAILED(s->controller->put_IsVisible(TRUE))){s->armed=false;hideOutput(s.get());return false;}
   // Identical JSON event bytes; serialize the body once, without a JSON-tree copy.
   if(s->view&&!s->closed)s->view->PostWebMessageAsJson(wide(hudserialization::desktopFrameEvent(body)).c_str());
   refresh(s);return true;
 }
 bool close(int id,const std::string& nonce,const std::string& reason="explicit-close"){
   auto s=current;if(!s)return true;if(s->id!=id||s->nonce!=nonce)return false;
   if(s->notice&&s->noticeLease==noticeLeaseSerial&&s->nonce==noticeLeaseNonce)noticeLeaseRetired=true;
   s->closed=true;s->ready=false;s->armed=false;s->reason=reason;stopInteraction(s,true);unhook();
   if(s->hwnd){KillTimer(s->hwnd,leaseTimer);KillTimer(s->hwnd,radialFadeTimer);hideOutput(s.get());}
   // Detach this visual before closing its WebView input/composition site.
   // Clear only this carrier; main UI and shared controlled-input runtime stay owned.
   if(s->composition)s->composition->put_RootVisualTarget(nullptr);if(s->visualTarget)s->visualTarget->SetRoot(nullptr);if(s->device)s->device->Commit();
   if(s->controller)s->controller->Close();s->view.Reset();s->controller.Reset();s->composition.Reset();s->visual.Reset();s->visualTarget.Reset();s->device.Reset();if(s->hwnd)DestroyWindow(s->hwnd);
   if(s->hwnd&&IsWindow(s->hwnd)||s->inputWindow&&IsWindow(s->inputWindow)){s->reason="owned-window-destruction-unconfirmed";return false;}
   s->hwnd=nullptr;s->inputWindow=nullptr;emit(s,"desktopHud.closed");s->owner=nullptr;current.reset();return true;
 }
 void shutdown(){noticeLeaseRetired=true;if(current)close(current->id,current->nonce,"main-process-shutdown");unhook();environment.Reset();}
 ~Carrier(){shutdown();}
};
} // namespace ymcc::desktophud
