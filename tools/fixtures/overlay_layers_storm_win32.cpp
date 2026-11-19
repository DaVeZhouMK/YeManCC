// Actual display-stack consumers on a private desktop that is never switched in.
// The foreground identity is a declared adapter; no WebView renderer or devices.
#define YMCC_MONITOR_NOTICE_NATIVE_TEST 1
#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#include <windows.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string_view>
#include <vector>
#include <json.hpp>
namespace probe {
inline HWND foreground=nullptr;inline unsigned posts=0,raises=0,activationRequests=0,postFailures=0;
inline bool nativeEvents=false;
inline HWND watchedSurface=nullptr;
inline unsigned nativeCallbacks=0,surfaceRaises=0;
struct WindowEvent {DWORD event;HWND window;LONG object;DWORD thread;};
inline std::vector<WindowEvent>* observedEvents=nullptr;
inline bool powerAllowed=true;
struct Hook {HWINEVENTHOOK handle;WINEVENTPROC callback;DWORD event;};
inline std::array<Hook,5> hooks{};
static void CALLBACK ignoreOsDelivery(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD){}
static void CALLBACK countOsDelivery(HWINEVENTHOOK handle,DWORD event,HWND h,LONG object,LONG child,DWORD thread,DWORD time){
 for(const auto& entry:hooks)if(entry.handle==handle){
  ++nativeCallbacks;
  if(observedEvents&&observedEvents->size()<observedEvents->capacity())observedEvents->push_back({event,h,object,thread});
  entry.callback(handle,event,h,object,child,thread,time);return;
 }
}
inline HWINEVENTHOOK hook(DWORD first,DWORD last,HMODULE module,WINEVENTPROC callback,DWORD pid,DWORD thread,DWORD flags){
 // Register real hooks, but replay the overlay callbacks in a fixed order.
 for(auto& entry:hooks){
  if(entry.handle)continue;
  const auto handle=::SetWinEventHook(first,last,module,nativeEvents?countOsDelivery:ignoreOsDelivery,pid,thread,flags);
  if(handle)entry={handle,callback,first};
  return handle;
 }
 return nullptr;
}
inline BOOL unhook(HWINEVENTHOOK handle){
 const BOOL ok=::UnhookWinEvent(handle);
 if(ok)for(auto& entry:hooks)if(entry.handle==handle)entry={};
 return ok;
}
inline bool offerPeer(DWORD event,HWND h){
 for(const auto& entry:hooks)if(entry.handle&&entry.event==event){
  entry.callback(entry.handle,event,h,OBJID_WINDOW,CHILDID_SELF,GetCurrentThreadId(),0);return true;
 }
 return false;
}
inline HWND getForeground(){return foreground;}
inline DWORD windowProcess(HWND h,DWORD* pid){const auto thread=::GetWindowThreadProcessId(h,pid);if(pid&&h==foreground)*pid=GetCurrentProcessId()+1;return thread;}
inline BOOL post(HWND h,UINT m,WPARAM w,LPARAM l){++posts;if(postFailures){--postFailures;return FALSE;}return ::PostMessageW(h,m,w,l);}
inline BOOL position(HWND h,HWND after,int x,int y,int w,int height,UINT flags){if(!(flags&SWP_NOACTIVATE))++activationRequests;if(after==HWND_TOPMOST){++raises;if(h==watchedSurface)++surfaceRaises;}return ::SetWindowPos(h,after,x,y,w,height,flags);}
}
#include "screen_touchpads.h"
#define GetForegroundWindow probe::getForeground
#define GetWindowThreadProcessId probe::windowProcess
#define PostMessageW probe::post
#define SetWindowPos probe::position
#include "screen_touchpad_owner.h"
#include "monitor_desktop_hud.h"
#define SetWinEventHook probe::hook
#define UnhookWinEvent probe::unhook
#include "game_overlay_presentation.h"
#undef UnhookWinEvent
#undef SetWinEventHook
#include "game_overlay_display_tree_lease.h"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef PostMessageW
#undef SetWindowPos
using json=nlohmann::json;
static ymcc::gameoverlay::Carrier overlay;
static ymcc::desktophud::Carrier g_desktopMonitorHud,g_monitorNoticeCarrier;
static HWND g_gameOverlaySurface=nullptr;
struct PanelPort {bool attached=true;bool ready()const{return attached;}};static PanelPort g_gameOverlayPanel;
#include "peers.inc"
static void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static LRESULT CALLBACK hostProc(HWND h,UINT m,WPARAM w,LPARAM l){
 LRESULT result=0;if(overlay.protectInput(h,m,w,l,result))return result;if(overlay.handle(h,m,w,l))return 0;
 if(m==ymcc::screenpads::kOwnerVisibilityMessage){ymcc::screenpads::consumeOwnerVisibility(w);return 0;}
 return DefWindowProcW(h,m,w,l);
}
static HWND makeWindow(const wchar_t* cls,int x=100,int y=100,int width=300,int height=180){
 const auto h=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,cls,L"",WS_POPUP,x,y,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 need(h!=nullptr,"owned HWND creation");return h;
}
static bool above(HWND a,HWND b){for(HWND h=GetWindow(b,GW_HWNDPREV);h;h=GetWindow(h,GW_HWNDPREV))if(h==a)return true;return false;}
static unsigned drain(unsigned budget=512){MSG m{};unsigned count=0;while(count<budget&&PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){DispatchMessageW(&m);++count;}need(count<budget,"message storm did not converge within fixed budget");return count;}
static void pumpNativeFor(ULONGLONG ms){
 const auto deadline=GetTickCount64()+ms;unsigned dispatched=0;
 for(;;){
  dispatched+=drain(4096);need(dispatched<16384,"native message storm exceeded budget");
  const auto now=GetTickCount64();if(now>=deadline)return;
  need(MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(deadline-now),QS_ALLINPUT,MWMO_INPUTAVAILABLE)!=WAIT_FAILED,"native queue wait");
 }
}
static void dispatchOverlayCheck(){
 // Isolate the posted check from scheduled maintenance timers.
 MSG m{};const auto id=ymcc::gameoverlay::Carrier::message;
 need(PeekMessageW(&m,g_gameOverlaySurface,id,id,PM_REMOVE)!=FALSE,"expected one posted overlay check");
 DispatchMessageW(&m);
}
namespace ymcc::desktophud {struct CarrierTestAccess {
 static HWND attach(Carrier& c,bool notice){
  static screeninput::Runtime unusedRuntime; // Only the existing panel-input enable gate; no input apartment is started.
  Config cfg;cfg.noticeOnly=notice;cfg.inputRuntime=notice?nullptr:&unusedRuntime;cfg.powerAllowed=[]{return probe::powerAllowed;};c.initialize(nullptr,cfg);
  auto s=std::make_shared<Carrier::State>();s->owner=&c;s->id=c.nextId++;s->nonce=notice?"storm-notice-nonce-aaaaaaaa":"storm-monitor-nonce-aaaaaaaa";
  if(notice)s->noticeLease=c.reserveNotice(s->nonce)["leaseId"].get<unsigned long long>();
  s->global=true;s->notice=notice;s->crosshair=notice;s->ready=true;s->armed=true;s->layout=true;s->width=320;s->height=100;s->position=1;s->expires=wallNow()+60000;
  WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=Carrier::proc;cls.lpszClassName=L"YMCC_Storm_ActualHud";
  if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("HUD class");
  c.current=s;s->hwnd=CreateWindowExW(hudinput::requiredExStyle|WS_EX_TOPMOST,cls.lpszClassName,L"",WS_POPUP|WS_DISABLED,0,0,320,100,nullptr,nullptr,cls.hInstance,s.get());
  need(s->hwnd!=nullptr,"actual HUD HWND");s->viewportKey=Carrier::viewportInfo(*s).dump();c.hook();c.refresh(s,true);return s->hwnd;
 }
 static void observed(HWND target){Carrier::observed(sharedHook(),EVENT_SYSTEM_FOREGROUND,target,OBJID_WINDOW,CHILDID_SELF,GetCurrentThreadId()+1,0);}
 static HWINEVENTHOOK sharedHook(){return Carrier::sharedHooks.empty()?nullptr:Carrier::sharedHooks.front();}
 static void refresh(Carrier& c,bool raise=false){if(c.current)c.refresh(c.current,raise);}
 static void expire(Carrier& c){need(c.current!=nullptr,"notice current");c.current->expires=wallNow()-1;SendMessageW(c.current->hwnd,WM_TIMER,Carrier::leaseTimer,0);}
 static void rearm(Carrier& c){auto s=c.current;s->armed=true;s->layout=true;s->expires=wallNow()+60000;c.refresh(s,true);}
 static bool queued(Carrier& c){return c.current&&c.current->queued;}
 static HWND inputWindow(Carrier& c){return c.current?c.current->inputWindow:nullptr;}
 static bool contact(Carrier& c){return c.current&&(c.current->mouseContact||c.current->drag.contact());}
 static void showTimedNotice(Carrier& c,unsigned lifetimeMs){
  const auto s=c.current;need(s&&s->notice&&s->hwnd,"mixed notice owner");
  const auto expires=wallNow()+lifetimeMs;
  const Json payload={{"type","monitor-notice"},{"protocol",1},{"hudId",s->id},{"nonce",s->nonce},
                      {"seq",s->seq+1},{"expiresAt",expires},{"feedback",{{"expiresAtUnixMs",expires}}}};
  need(c.noticeFrame(s->id,s->nonce,payload)&&!IsWindowVisible(s->hwnd),"mixed notice waits for matching frame");
  s->layout=true; // Only the renderer layout receipt is substituted; keep the real expiry timer.
  c.refresh(s,true);
  need(IsWindowVisible(s->hwnd),"mixed notice visible after layout receipt");
 }
 static bool freshNoticeRejectsLateWork(Carrier& c){
  const auto s=c.current;
  if(!s||!s->notice||!s->hwnd)return false;
  const auto sequence=s->seq+1;
  const auto expires=wallNow()+9000;
  const Json payload={{"type","monitor-notice"},{"protocol",1},{"hudId",s->id},{"nonce",s->nonce},
                      {"seq",sequence},{"expiresAt",expires},{"feedback",{{"expiresAtUnixMs",expires}}}};
  if(!c.noticeFrame(s->id,s->nonce,payload)||IsWindowVisible(s->hwnd))return false;
  rearm(c); // Substitute the fresh notice's renderer layout receipt.
  return !c.noticeFrame(s->id,s->nonce,payload)&&!c.noticeHide(s->id,s->nonce,sequence-1)&&
         IsWindowVisible(s->hwnd)&&s->seq==sequence;
 }
 static unsigned owners(){return static_cast<unsigned>(Carrier::eventOwners.size());}
};}
static std::wstring inputDesktop(){HDESK h=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);wchar_t name[256]{};DWORD bytes=0;if(h){GetUserObjectInformationW(h,UOI_NAME,name,sizeof(name),&bytes);CloseDesktop(h);}return name;}
static void runMixedCompetition(HWND monitor,HWND notice,HWND touch,HWND outline,unsigned& scans,unsigned& layouts,unsigned& contextChecks,
 const auto& beginOverlay,json& result){
 using Access=ymcc::desktophud::CarrierTestAccess;
 const auto display=g_gameOverlaySurface,monitorInput=Access::inputWindow(g_desktopMonitorHud);
 const auto session=overlay.snapshot();const auto stableLayouts=layouts;
 const auto peersVisible=[&]{return IsWindowVisible(monitor)&&IsWindowVisible(touch)&&IsWindowVisible(outline)&&
  IsWindowVisible(monitorInput)&&monitorInput==Access::inputWindow(g_desktopMonitorHud)&&GetCapture()==nullptr;};
 const auto overlayIntact=[&]{return overlay.snapshot()==session&&overlay.inputReady()&&IsWindowVisible(display)&&
  peersVisible()&&layouts==stableLayouts;};
 // Each wave uses real window changes mixed with a bounded, explicit WinEvent burst.
 constexpr std::array<std::string_view,6> orders={"MTN","MNT","TMN","TNM","NMT","NTM"};
 constexpr unsigned burst=192;
 const std::array<HWND,4> producers={monitor,outline,notice,display};
 result["mixedWaves"]=json::array();
 result["mixedExplicitEvents"]=0;
 const auto mutate=[&](char owner){
  if(owner=='M')Access::refresh(g_desktopMonitorHud,true);
  else if(owner=='T'){
   ShowWindow(outline,SW_HIDE);ShowWindow(outline,SW_SHOWNOACTIVATE);
   ymcc::screenpads::restoreScreenLayerPair(touch,outline,probe::foreground,true,{ymcc::screenpads::ScreenPairInputKind::Pad,0});
  }else Access::showTimedNotice(g_monitorNoticeCarrier,320);
 };
 for(unsigned wave=0;wave<12;++wave){
  const auto order=orders[wave%orders.size()];
  const auto nScans=scans,nRaises=probe::surfaceRaises,nCallbacks=probe::nativeCallbacks;
  for(unsigned i=0,j=0;i<4;++i){
   if(i==wave%4)overlay.requestCheck();else mutate(order[j++]);
  }
  need(overlayIntact()&&IsWindowVisible(notice)&&gameOverlaySurfacePeerOccluded(),"mixed four-way overlap before delivery");
  for(unsigned i=0;i<burst;++i)NotifyWinEvent(i%8<4?EVENT_OBJECT_REORDER:EVENT_OBJECT_SHOW,
    producers[(i+wave)%producers.size()],OBJID_WINDOW,CHILDID_SELF);
  result["mixedExplicitEvents"]=result["mixedExplicitEvents"].get<unsigned>()+burst;
  pumpNativeFor(48);
  const auto initialRepairs=probe::surfaceRaises-nRaises,initialScans=scans-nScans;
  need(overlayIntact()&&IsWindowVisible(notice)&&!gameOverlaySurfacePeerOccluded(),"mixed four-way overlap settles");
  need(probe::nativeCallbacks-nCallbacks>=burst&&initialRepairs==1&&initialScans<=3,"mixed burst coalesces to one repair and bounded scans");
  const auto waitingScans=scans,waitingCallbacks=probe::nativeCallbacks;
  const auto expiryDeadline=GetTickCount64()+1000;
  while(IsWindowVisible(notice)&&GetTickCount64()<expiryDeadline)pumpNativeFor(24);
  need(!IsWindowVisible(notice)&&overlayIntact()&&!gameOverlaySurfacePeerOccluded(),"natural notice expiry does not withdraw other layers");
  need(probe::surfaceRaises==nRaises+1,"notice expiry causes no overlay raise");
  const auto quietScans=scans,quietCallbacks=probe::nativeCallbacks;
  pumpNativeFor(48);
  need(overlayIntact()&&!IsWindowVisible(notice)&&scans==quietScans&&probe::nativeCallbacks==quietCallbacks&&
   probe::surfaceRaises==nRaises+1,"mixed callbacks stay quiet after notice expires");
  result["mixedWaves"].push_back({{"order",order},{"overlaySlot",wave%4},{"explicitEvents",burst},
   {"burstCallbacks",waitingCallbacks-nCallbacks},{"burstScans",initialScans},{"overlayRepairs",initialRepairs},
   {"expiryCallbacks",quietCallbacks-waitingCallbacks},{"expiryScans",quietScans-waitingScans},{"notificationExpired",true}});
 }
 // Retire the live overlay with its own posted check and peer events still in flight.
 Access::rearm(g_monitorNoticeCarrier);
 mutate('M');mutate('T');
 const auto postsBefore=probe::posts;
 overlay.requestCheck();
 NotifyWinEvent(EVENT_OBJECT_REORDER,notice,OBJID_WINDOW,CHILDID_SELF);
 result["mixedExplicitEvents"]=result["mixedExplicitEvents"].get<unsigned>()+1;
 need(overlayIntact()&&IsWindowVisible(notice)&&probe::posts==postsBefore+1&&gameOverlaySurfacePeerOccluded(),"mixed close starts with a live queued check");
 overlay.end("native-mixed-close-with-work-pending");
 const auto closedRaises=probe::surfaceRaises,closedScans=scans;
 pumpNativeFor(64);
 need(!overlay.active()&&!overlay.inputReady()&&!IsWindowVisible(display)&&peersVisible()&&IsWindowVisible(notice)&&
  probe::surfaceRaises==closedRaises&&scans==closedScans&&layouts==stableLayouts,"mixed pending work cannot reopen UI or close peers");
 result["mixedCloseWithPending"]={{"overlayClosed",true},{"otherOwnersVisible",true},{"overlayWorkAfterClose",0}};

 // Keep the HWND and peer owners, but replace the overlay session before
 // Windows dispatches its posted check. Then restack the peers around a fresh notice.
 result["mixedReopenWaves"]=json::array();
 constexpr unsigned reopenBurst=64;
 for(const auto order:orders){
  need(beginOverlay(),"start retiring overlay session");
  const auto retired=overlay.snapshot();
  overlay.requestCheck();
  overlay.end("native-mixed-reopen-retire");
  need(beginOverlay(),"reopen overlay on same HWND before dispatch");
  const auto fresh=overlay.snapshot();
  need(fresh&&retired&&fresh->session>retired->session&&fresh->carrier==retired->carrier&&
   overlay.inputReady(*fresh)&&!overlay.inputReady(*retired),"only the new session owns input");
  for(const char producer:order)mutate(producer);
  need(gameOverlaySurfacePeerOccluded()&&peersVisible()&&IsWindowVisible(notice),"reopened overlay starts occluded with peers visible");
  for(unsigned i=0;i<reopenBurst;++i)NotifyWinEvent(i%8<4?EVENT_OBJECT_REORDER:EVENT_OBJECT_SHOW,
   producers[i%producers.size()],OBJID_WINDOW,CHILDID_SELF);
  const auto postsBeforeRevoke=probe::posts;
  overlay.revoke(retired);
  need(probe::posts==postsBeforeRevoke&&overlay.inputReady(*fresh),"late revocation does not queue work or revoke successor");
  overlay.requestCheck(); // The successor must retain its own queued check.
  MSG oldCheck{};
  const auto checkMessage=ymcc::gameoverlay::Carrier::message;
  need(PeekMessageW(&oldCheck,display,checkMessage,checkMessage,PM_REMOVE)&&oldCheck.wParam==retired->session,
   "original retired check still queued for same HWND");
  const auto reopenScans=scans,reopenRaises=probe::surfaceRaises,reopenLayouts=layouts,checksBeforeOld=contextChecks;
  DispatchMessageW(&oldCheck);
  need(overlay.snapshot()==fresh&&overlay.inputReady(*fresh)&&contextChecks==checksBeforeOld&&scans==reopenScans&&
   probe::surfaceRaises==reopenRaises&&layouts==reopenLayouts&&gameOverlaySurfacePeerOccluded(),
   "retired check cannot consume the successor's peer work");
  pumpNativeFor(48);
  const auto layoutPasses=layouts-reopenLayouts;
  const auto repairs=probe::surfaceRaises-reopenRaises-layoutPasses;
  need(overlay.snapshot()==fresh&&overlay.inputReady(*fresh)&&IsWindowVisible(display)&&peersVisible()&&
   IsWindowVisible(notice)&&!gameOverlaySurfacePeerOccluded()&&repairs==1&&scans-reopenScans<=3,
   "successor repairs overlap once while all peers stay visible");
  result["mixedExplicitEvents"]=result["mixedExplicitEvents"].get<unsigned>()+reopenBurst;
  result["mixedReopenWaves"].push_back({{"order",order},{"explicitEvents",reopenBurst},{"scans",scans-reopenScans},
   {"repairs",repairs},{"oldCheckIgnored",true},{"oldRevokeIgnored",true},{"successorInputReady",true}});
  overlay.end("native-mixed-reopen-complete");
  pumpNativeFor(16);
  need(!overlay.active()&&!IsWindowVisible(display)&&peersVisible()&&IsWindowVisible(notice),"successor close leaves peers visible");
 }
}
static void runNativeCompetition(HWND monitor,HWND notice,HWND touch,HWND outline,const ymcc::customtouch::Item& custom,json& result){
 using Access=ymcc::desktophud::CarrierTestAccess;
 const HWND display=g_gameOverlaySurface;
 const auto close=[](ymcc::gameoverlay::Carrier* carrier){carrier->end("native-competition-complete");};
 probe::watchedSurface=display;
 constexpr unsigned rounds=16,burst=128;
 unsigned scans=0,layouts=0,contextChecks=0,maxPeerScans=0,maxNoticeScans=0;
 const std::unique_ptr<ymcc::gameoverlay::Carrier,decltype(close)> guard(&overlay,close);
 ymcc::gameoverlay::Binding b;b.globalProbe=true;b.originalUiInput=true;b.power=1;b.sharedPower.generation=1;b.sharedPower.flags=ymcc::sleeppower::kKnown;
 const auto beginOverlay=[&]{return overlay.begin(display,b,{[&](const auto&){++contextChecks;return true;},[&]{++layouts;return true;},[]{return true;},[](bool,const char*){},{},{},[&]{++scans;return gameOverlaySurfacePeerOccluded();}});};
 need(beginOverlay(),"native hook overlay begin");
 // The three production placement timers are armed sequentially, not in parallel.
 const auto startupDeadline=GetTickCount64()+2400;
 while(layouts<4&&overlay.active()&&GetTickCount64()<startupDeadline)pumpNativeFor(30);
 need(layouts==4&&overlay.active(),"initial layout and all three maintenance passes complete");
 pumpNativeFor(48);
 const auto stableLayouts=layouts;
 const auto stableInitialRaises=probe::surfaceRaises;
 result["roundsCompleted"]=0;
 for(unsigned i=0;i<=rounds;++i){
  const bool automatic=i==0;
  const auto fourVisible=[&]{return IsWindowVisible(display)&&IsWindowVisible(monitor)&&IsWindowVisible(notice)&&IsWindowVisible(touch)&&IsWindowVisible(outline);};
  need(fourVisible(),"all four owners visible at round entry");
  const auto restack=[](HWND h){need(SetWindowPos(h,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,"native peer restack");};
  const auto showPeer=[&]{ShowWindow(outline,SW_HIDE);ShowWindow(outline,SW_SHOWNOACTIVATE);restack(touch);restack(outline);};
  const auto nCallbacks=probe::nativeCallbacks,nRaises=probe::surfaceRaises,nScans=scans;
  if(i%2)showPeer();
  Access::refresh(g_desktopMonitorHud,true);
  if(!(i%2))showPeer();
  need(gameOverlaySurfacePeerOccluded(),"actual overlapping peer above overlay before WinEvent delivery");
  if(!automatic)for(unsigned j=0;j<burst;++j)NotifyWinEvent(j%2?EVENT_OBJECT_REORDER:EVENT_OBJECT_SHOW,j%2?monitor:outline,OBJID_WINDOW,CHILDID_SELF);
  pumpNativeFor(48);
  if(automatic)result["automaticPeerWindowEvents"]=probe::nativeCallbacks-nCallbacks;
  else maxPeerScans=std::max(maxPeerScans,scans-nScans);
  need(probe::nativeCallbacks>nCallbacks&&scans>nScans,"OS delivered production peer callbacks");
  need(probe::surfaceRaises==nRaises+1&&scans-nScans<burst,"one restack, bounded peer scans including echoes");
  need(above(display,touch)&&above(display,outline)&&above(display,monitor)&&above(display,Access::inputWindow(g_desktopMonitorHud))&&fourVisible(),"WM-event recovery preserves all four owners and puts overlay over its peers");
  const auto quietScans=scans,stableRaises=probe::surfaceRaises,noticeCallbacks=probe::nativeCallbacks;
  restack(notice);
  need(above(notice,display)&&!gameOverlaySurfacePeerOccluded(),"notification can overlay without competing for input order");
  for(unsigned j=0;j<burst;++j)NotifyWinEvent(EVENT_OBJECT_REORDER,notice,OBJID_WINDOW,CHILDID_SELF);
  pumpNativeFor(48);
  maxNoticeScans=std::max(maxNoticeScans,scans-quietScans);
  result["lastRound"]={{"peerScans",quietScans-nScans},{"noticeScans",scans-quietScans},
    {"noticeWindowEvents",probe::nativeCallbacks-noticeCallbacks},{"noticeOverlayRaises",probe::surfaceRaises-stableRaises},{"layouts",layouts}};
  need(probe::nativeCallbacks>noticeCallbacks,"OS delivered notice callbacks");
  need(probe::surfaceRaises==stableRaises&&scans-quietScans<burst&&fourVisible(),"visible notification burst does not restack overlay");
  const auto drainedScans=scans,drainedCallbacks=probe::nativeCallbacks;
  pumpNativeFor(32);
  need(scans==drainedScans&&probe::nativeCallbacks==drainedCallbacks&&probe::surfaceRaises==stableRaises&&layouts==stableLayouts,"no self-sustaining restack, scan or relayout");
  result["roundsCompleted"]=i+1;
 }
 result["surfaceRaisesDuringCompetition"]=probe::surfaceRaises-stableInitialRaises;
 result["pureRestacks"]=json::array();
 const auto pure=[&](const char* name,const auto& mutate,HWND peer,bool expectedOcclusion){
  need(SetWindowPos(display,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,"restore fixture start order");
  pumpNativeFor(40);
  need(!gameOverlaySurfacePeerOccluded()&&IsWindowVisible(peer),"pure restack control begins in settled order");
  RECT before{},after{};
  need(GetWindowRect(peer,&before)!=FALSE,"pure restack peer bounds before");
  const auto previousCallbacks=probe::nativeCallbacks,previousScans=scans,previousRaises=probe::surfaceRaises;
  std::vector<probe::WindowEvent> events;events.reserve(256);
  const auto stopCapture=[](std::vector<probe::WindowEvent>*){probe::observedEvents=nullptr;};
  const std::unique_ptr<std::vector<probe::WindowEvent>,decltype(stopCapture)> capture(&events,stopCapture);
  probe::observedEvents=&events;
  mutate();
  need(IsWindowVisible(peer)&&GetWindowRect(peer,&after)&&EqualRect(&before,&after),"pure case preserves visible peer geometry");
  need(above(peer,display),"pure restack puts the producer above the overlay");
  RECT displayBounds{},intersection{};
  need(GetWindowRect(display,&displayBounds)!=FALSE,"pure restack overlay bounds");
  const bool geometricallyOverlaps=IntersectRect(&intersection,&displayBounds,&after)!=FALSE;
  const bool startsOccluded=gameOverlaySurfacePeerOccluded();
  pumpNativeFor(180);
  probe::observedEvents=nullptr;
  json eventFacts=json::array();
  for(const auto& e:events){
   const char* owner=e.window==peer?"producer":e.window==display?"overlay":e.window==GetDesktopWindow()?"desktop":e.window==nullptr?"null":
      e.window==Access::inputWindow(g_desktopMonitorHud)?"monitor-input":e.window==touch?"touch-input":"other";
   eventFacts.push_back({{"event",e.event},{"owner",owner},{"object",e.object},{"sameThread",e.thread==GetCurrentThreadId()}});
  }
  const auto repairs=probe::surfaceRaises-previousRaises;
  const bool endsOccluded=gameOverlaySurfacePeerOccluded();
  const bool endsAbove=above(peer,display);
  result["pureRestacks"].push_back({{"name",name},{"callbacks",probe::nativeCallbacks-previousCallbacks},{"scans",scans-previousScans},
   {"repairs",repairs},{"overlaps",geometricallyOverlaps},{"startsOccluded",startsOccluded},{"endsOccluded",endsOccluded},{"endsAbove",endsAbove},{"events",eventFacts}});
  return startsOccluded==expectedOcclusion&&!endsOccluded&&endsAbove!=expectedOcclusion&&
   repairs==(expectedOcclusion?1u:0u)&&IsWindowVisible(peer)&&overlay.active()&&layouts==stableLayouts;
 };
 need(pure("monitor-refresh",[]{Access::refresh(g_desktopMonitorHud,true);},monitor,true),"monitor overlap settles");
 need(pure("touchpad-pair",[&]{ymcc::screenpads::restoreScreenLayerPair(touch,outline,probe::foreground,true,{ymcc::screenpads::ScreenPairInputKind::Pad,0});},outline,true),"touchpad overlap settles");
 need(pure("notice-refresh",[]{Access::refresh(g_monitorNoticeCarrier,true);},notice,false),"notice may stay above without repair");
 ShowWindow(custom.window,SW_SHOWNOACTIVATE);
 const bool inputOnly=pure("custom-touch-input-only",[&]{
  need(!IsWindowVisible(custom.outline),"auto-hidden custom artwork stays hidden");
  ymcc::screenpads::restoreScreenLayerPair(custom.window,custom.outline,probe::foreground,true,
    {ymcc::screenpads::ScreenPairInputKind::Custom,0,&custom});
 },custom.window,true);
 need(SetWindowPos(custom.window,nullptr,800,100,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"place custom input outside the overlay");
 ShowWindow(custom.outline,SW_SHOWNOACTIVATE);
 const bool artworkOnly=pure("custom-touch-artwork-only",[&]{
  RECT inputBounds{},displayBounds{},intersection{};
  need(GetWindowRect(custom.window,&inputBounds)&&GetWindowRect(display,&displayBounds)&&
   !IntersectRect(&intersection,&inputBounds,&displayBounds),"only custom artwork intersects the overlay");
  need(SetWindowPos(custom.outline,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,"raise custom artwork");
 },custom.outline,true);
 ShowWindow(custom.outline,SW_HIDE);
 const bool disjoint=pure("custom-touch-nonoverlap",[&]{
  ymcc::screenpads::restoreScreenLayerPair(custom.window,custom.outline,probe::foreground,true,
    {ymcc::screenpads::ScreenPairInputKind::Custom,0,&custom});
 },custom.window,false);
 need(inputOnly&&artworkOnly&&disjoint,"custom touch overlap is repaired without changing disjoint or hidden state");
 ShowWindow(custom.window,SW_HIDE);
 runMixedCompetition(monitor,notice,touch,outline,scans,layouts,contextChecks,beginOverlay,result);
 need(!IsWindowVisible(display)&&IsWindowVisible(monitor)&&IsWindowVisible(notice)&&IsWindowVisible(touch)&&IsWindowVisible(outline),"closing native-test overlay preserves other owners");
 result["nativeRounds"]=rounds+1;
 result["legacyExplicitNativeEvents"]=(rounds*2+1)*burst;
 result["explicitNativeEvents"]=result["legacyExplicitNativeEvents"].get<unsigned>()+result["mixedExplicitEvents"].get<unsigned>();
 result["nativeCallbacks"]=probe::nativeCallbacks;
 result["maxPeerScansPerBurst"]=maxPeerScans;
 result["maxNoticeScansPerBurst"]=maxNoticeScans;
 result["initialLayouts"]=stableLayouts;
 result["surfaceRaises"]=probe::surfaceRaises;
}
int wmain(int argc,wchar_t** argv){
 if(argc!=2&&!(argc==3&&std::wstring_view(argv[2])==L"--native-hooks"))return 2;
 probe::nativeEvents=argc==3;
 json result={{"scope","actual Carrier/monitor/notice/touchpad visibility consumers; private never-switched desktop; substituted foreground identity; no WebView pixels or physical input proof"},{"nativeHookDelivery",probe::nativeEvents}};
 json cases=json::array();bool ok=true;auto check=[&](const char* name,bool pass){cases.push_back({{"case",name},{"pass",pass}});need(pass,name);};
 const auto before=inputDesktop();const auto original=GetThreadDesktop(GetCurrentThreadId());
 const auto name=L"YMCC_FourLayerStorm_"+std::to_wstring(GetCurrentProcessId());
 HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS,nullptr);
 if(!desktop||!SetThreadDesktop(desktop))return 4;
 HWND host=nullptr,child=nullptr,monitor=nullptr,notice=nullptr,customWindow=nullptr,customOutline=nullptr;
 try{
  WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=hostProc;cls.lpszClassName=L"YMCC_FourLayerStorm_Host";need(RegisterClassW(&cls)!=0,"host class");
  host=makeWindow(cls.lpszClassName);probe::foreground=makeWindow(cls.lpszClassName,0,0,1600,1000);ShowWindow(probe::foreground,SW_SHOWNOACTIVATE);
  using namespace ymcc::screenpads;instance=GetModuleHandleW(nullptr);dryRun=true;owner=host;initialized=true;available=true;
  inputPowerAllowed=[]{return probe::powerAllowed;};
  core.config.enabled=true;core.config.left=Mode::Deck;core.config.right=Mode::Off;core.config.autoHideSeconds=0;
  windows[0]=makeWindow(cls.lpszClassName);outlines[0]=makeWindow(cls.lpszClassName);ymcc::screeninput::ensureDisplay(outlines[0]);
  ShowWindow(windows[0],SW_SHOWNOACTIVATE);ShowWindow(outlines[0],SW_SHOWNOACTIVATE);syncOwnerVisibility();
  monitor=ymcc::desktophud::CarrierTestAccess::attach(g_desktopMonitorHud,false);
  notice=ymcc::desktophud::CarrierTestAccess::attach(g_monitorNoticeCarrier,true);
  g_gameOverlaySurface=makeWindow(cls.lpszClassName,0,0,600,600);
  child=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,10,10,g_gameOverlaySurface,nullptr,instance,nullptr);need(child!=nullptr,"child HWND");
  ymcc::gameoverlayinput::DisplayTreeLease tree;tree.begin(g_gameOverlaySurface);need(tree.protect(),"display tree protected");
  check("initial-monitor-touchpad-notice-all-visible",IsWindowVisible(monitor)&&IsWindowVisible(notice)&&IsWindowVisible(windows[0])&&IsWindowVisible(outlines[0]));
  check("actual-monitor-panel-receives-input-without-capture",ymcc::desktophud::CarrierTestAccess::inputWindow(g_desktopMonitorHud)&&GetCapture()==nullptr);
  check("notification-is-not-a-competing-interactive-peer",!gameOverlayOwnPresentationPeer(notice)&&gameOverlayOwnPresentationPeer(monitor)&&gameOverlayOwnPresentationPeer(windows[0]));
  if(probe::nativeEvents){
   customWindow=makeWindow(cls.lpszClassName,130,130,110,110);
   customOutline=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,cls.lpszClassName,L"",
    WS_POPUP|WS_DISABLED,120,120,130,130,customWindow,nullptr,instance,nullptr);
   need(customOutline&&ymcc::screeninput::ensureDisplay(customOutline),"custom artwork is display-only");
   auto custom=std::make_shared<ymcc::customtouch::Item>();custom->window=customWindow;custom->outline=customOutline;
   ymcc::customtouch::items.push_back(custom);
   runNativeCompetition(monitor,notice,windows[0],outlines[0],*custom,result);
   check("actual-OS-hook-delivery-settles-all-four-layers",true);
   check("native-stress-did-not-request-focus-or-inject-input",probe::activationRequests==0&&injectedCount==0&&sentKeys==0&&!sentClick);
  }else{
  unsigned maxDrain=0,peerScans=0,maxPeerPosts=0,maxPeerScans=0,noticeOnlyRepairs=0;
  std::uint64_t retiredSession=0;
  const auto loopStart=GetTickCount64();
  for(unsigned cycle=0;cycle<96;++cycle){
   ymcc::gameoverlay::Binding b;b.globalProbe=true;b.originalUiInput=true;b.power=1;b.sharedPower.generation=1;b.sharedPower.flags=ymcc::sleeppower::kKnown;
   need(overlay.begin(g_gameOverlaySurface,b,{[](const auto&){return true;},[]{return true;},[]{return true;},[](bool,const char*){},{},{},[&]{++peerScans;return gameOverlaySurfacePeerOccluded();}}),"cycle overlay begin");
   // All four actual consumers see one large external transition burst.
   probe::posts=0;
   for(unsigned i=0;i<1000;++i){ymcc::desktophud::CarrierTestAccess::observed(probe::foreground);screenVisibilityObserved(ownerVisibilityHooks[0],EVENT_SYSTEM_FOREGROUND,probe::foreground,OBJID_WINDOW,CHILDID_SELF,GetCurrentThreadId()+1,0);overlay.requestCheck();}
   need(probe::posts<=4,"one queued update per active consumer");maxDrain=std::max(maxDrain,drain());
   if(cycle==0){
    probe::postFailures=4;
    ymcc::desktophud::CarrierTestAccess::observed(probe::foreground);queueOwnerVisibility(true);overlay.requestCheck();
    need(probe::postFailures==0,"all four post failure ports exercised");
    ymcc::desktophud::CarrierTestAccess::observed(probe::foreground);queueOwnerVisibility(true);overlay.requestCheck();maxDrain=std::max(maxDrain,drain());
   }
   // Alternate peer restack order and require the native callback to repair it, without a relayout.
   const auto raiseTouch=[&]{return SetWindowPos(windows[0],HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE;};
   if(cycle%2)need(raiseTouch(),"touch peer raise");
   ymcc::desktophud::CarrierTestAccess::refresh(g_desktopMonitorHud,true);
   if(!(cycle%2))need(raiseTouch(),"touch peer raise");
   need(gameOverlaySurfacePeerOccluded(),"positive control: an overlapping peer is above the overlay");
   probe::posts=0;const auto scansBefore=peerScans;
   for(unsigned i=0;i<1000;++i)need(probe::offerPeer(i%2?EVENT_OBJECT_REORDER:EVENT_OBJECT_SHOW,i%2?monitor:windows[0]),"peer hook installed");
   maxPeerPosts=std::max(maxPeerPosts,probe::posts);
   need(probe::posts==1&&peerScans==scansBefore,"peer events queue one check without inline scans");
   if(retiredSession){
    SendMessageW(g_gameOverlaySurface,ymcc::gameoverlay::Carrier::message,static_cast<WPARAM>(retiredSession),0);
    need(peerScans==scansBefore&&gameOverlaySurfacePeerOccluded(),"retired overlay message cannot act on successor");
   }
   dispatchOverlayCheck();
   maxPeerScans=std::max(maxPeerScans,peerScans-scansBefore);
   need(peerScans-scansBefore==1&&!gameOverlaySurfacePeerOccluded(),"one queued scan repairs the overlapping peers");
   need(above(g_gameOverlaySurface,monitor)&&above(g_gameOverlaySurface,windows[0]),"interactive overlay retains order over overlapping own peers");
   const HWND input=ymcc::desktophud::CarrierTestAccess::inputWindow(g_desktopMonitorHud);
   need(input&&above(g_gameOverlaySurface,input),"overlay is above monitor pointer receiver");
   need(SetWindowPos(notice,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,"notice peer raise");
   RECT overlayBounds{},noticeBounds{},overlap{};
   need(above(notice,g_gameOverlaySurface)&&GetWindowRect(notice,&noticeBounds)&&GetWindowRect(g_gameOverlaySurface,&overlayBounds)&&
        IntersectRect(&overlap,&overlayBounds,&noticeBounds)&&!gameOverlaySurfacePeerOccluded(),"notice-only control: only the notice is above and overlapping");
   const auto raisesBefore=probe::raises,scansBeforeNotice=peerScans;
   for(unsigned i=0;i<1000;++i)need(probe::offerPeer(EVENT_OBJECT_REORDER,notice),"notice event hook installed");
   dispatchOverlayCheck();
   noticeOnlyRepairs+=probe::raises-raisesBefore;
   need(peerScans-scansBeforeNotice==1&&probe::raises==raisesBefore,"notification event burst does not make overlay reassert topmost");
   maxDrain=std::max(maxDrain,drain());
   SendMessageW(input,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(20,20));
   need(ymcc::desktophud::CarrierTestAccess::contact(g_desktopMonitorHud),"actual monitor contact admitted");
   ymcc::desktophud::CarrierTestAccess::expire(g_monitorNoticeCarrier);
   need(!IsWindowVisible(notice)&&IsWindowVisible(monitor)&&IsWindowVisible(windows[0])&&overlay.active(),"notice expiry must not hide other layers");
   need(ymcc::desktophud::CarrierTestAccess::freshNoticeRejectsLateWork(g_monitorNoticeCarrier),"duplicate frame and late hide cannot retire fresh notice");
   need(ymcc::desktophud::CarrierTestAccess::contact(g_desktopMonitorHud),"notification lifecycle does not cancel another input owner");
   SendMessageW(input,WM_CANCELMODE,0,0);need(!ymcc::desktophud::CarrierTestAccess::contact(g_desktopMonitorHud)&&GetCapture()==nullptr,"lost UP cancellation releases only own monitor contact");
   ymcc::desktophud::CarrierTestAccess::refresh(g_desktopMonitorHud);
   ShowWindow(windows[0],SW_HIDE);ShowWindow(outlines[0],SW_HIDE);cancelOwnerVisibilityWork();
   const auto epoch=ownerVisibilityEpoch;need(!consumeOwnerVisibility(epoch-1),"retired touch visibility message ignored");
   need(IsWindowVisible(monitor)&&IsWindowVisible(notice)&&overlay.active(),"touchpad hide must not close independent carriers");
   ShowWindow(windows[0],SW_SHOWNOACTIVATE);ShowWindow(outlines[0],SW_SHOWNOACTIVATE);syncOwnerVisibility();
   core.pads[0].down=true;release();need(!core.pads[0].down,"touch release retires contact during display changes");
   retiredSession=overlay.snapshot()->session;
   overlay.end("cycle-end");overlay.end("duplicate-end");maxDrain=std::max(maxDrain,drain());
   need(!overlay.active()&&IsWindowVisible(monitor)&&IsWindowVisible(notice)&&IsWindowVisible(windows[0]),"overlay close preserves other three owners");
   need(!ymcc::desktophud::CarrierTestAccess::queued(g_desktopMonitorHud)&&!ymcc::desktophud::CarrierTestAccess::queued(g_monitorNoticeCarrier)&&!ownerVisibilityQueued,"queues converge");
  }
  check("96-simultaneous-show-hide-expiry-contact-release-cycles",true);
  check("alternating-peer-restacks-recover-without-layout-fallback",true);
  check("retired-overlay-and-notice-work-cannot-affect-successor",true);
  check("96-notice-only-bursts-cause-no-overlay-topmost-repair",noticeOnlyRepairs==0);
  const auto uploads=outlineUploadCount;const auto raises=probe::raises;for(unsigned i=0;i<100;++i)drain();
  check("settled-queues-do-not-raise-or-rebuild-artwork",raises==probe::raises&&uploads==outlineUploadCount);
  check("no-consumer-requested-window-activation",probe::activationRequests==0);
  check("no-device-input-was-injected",injectedCount==0&&sentKeys==0&&!sentClick);
  for(unsigned cycle=0;cycle<32;++cycle){
   ymcc::sleeppower::SharedPowerV1 power;power.generation=1;power.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;power.suspendSerial=cycle;power.observedNotificationSequence=cycle*2;
   ownerSharedPowerChanged(power);probe::powerAllowed=true;ymcc::desktophud::CarrierTestAccess::rearm(g_desktopMonitorHud);ymcc::desktophud::CarrierTestAccess::rearm(g_monitorNoticeCarrier);
   ymcc::gameoverlay::Binding b;b.globalProbe=true;b.originalUiInput=true;b.power=1;b.sharedPower=power;
   need(overlay.begin(g_gameOverlaySurface,b,{[](const auto&){return true;},[]{return true;},[]{return true;},[](bool,const char*){},{},{},[]{return gameOverlaySurfacePeerOccluded();}}),"power-cycle begin");
   core.pads[0].down=true;const auto epoch=ownerVisibilityEpoch;
   probe::powerAllowed=false;power.flags|=ymcc::sleeppower::kNotificationPending;++power.observedNotificationSequence;
   ownerSharedPowerChanged(power);g_desktopMonitorHud.sharedPowerChanged();g_monitorNoticeCarrier.sharedPowerChanged();overlay.sharedPowerChanged(power);drain();
   need(!overlay.active()&&!IsWindowVisible(monitor)&&!IsWindowVisible(notice)&&!core.pads[0].down&&ownerPowerWithdrawn,"shared pending withdraws all four actual consumers");
   need(!consumeOwnerVisibility(epoch),"pre-suspend touch visibility work cannot resume old epoch");
   probe::powerAllowed=true;power.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;++power.suspendSerial;++power.observedNotificationSequence;
   ownerSharedPowerChanged(power);g_desktopMonitorHud.sharedPowerChanged();g_monitorNoticeCarrier.sharedPowerChanged();overlay.sharedPowerChanged(power);drain();
   need(!overlay.active()&&!IsWindowVisible(monitor)&&!IsWindowVisible(notice)&&!ownerPowerWithdrawn,"Ready does not replay closed UI or stale display frame");
   ymcc::desktophud::CarrierTestAccess::rearm(g_desktopMonitorHud);need(IsWindowVisible(monitor)&&!IsWindowVisible(notice),"fresh monitor frame can recover independently of expired notice");
  }
  check("32-common-power-pending-ready-cycles-with-actual-four-consumers",true);
  check("power-cycles-produce-no-real-input",injectedCount==0&&sentKeys==0&&!sentClick);
  check("temporary-child-input-styles-returned",tree.restore()&&IsWindowEnabled(child)&&!(GetWindowLongPtrW(child,GWL_EXSTYLE)&WS_EX_TRANSPARENT));
  result["cycles"]=96;result["sharedPowerCycles"]=32;result["offeredExternalEvents"]=96*3000;result["maxMessagesToDrain"]=maxDrain;result["elapsedMs"]=GetTickCount64()-loopStart;
  result["overlayPeerAndNoticeCallbacks"]=96*2000;result["maxPeerPostsPerBurst"]=maxPeerPosts;result["maxPeerScansPerBurst"]=maxPeerScans;result["noticeOnlyOverlayRepairs"]=noticeOnlyRepairs;
  result["overlayEventDelivery"]="production callbacks replayed via registered test hook handles; OS timing not validated";
  }
 }catch(const std::exception& e){ok=false;result["failure"]=e.what();result["lastError"]=GetLastError();}
 overlay.end("fixture-exit");g_desktopMonitorHud.shutdown();g_monitorNoticeCarrier.shutdown();
 using namespace ymcc::screenpads;stopOwnerVisibility();core.pads[0].down=false;
 ymcc::customtouch::items.clear();
 for(HWND* h:{&customOutline,&customWindow,&windows[0],&outlines[0],&g_gameOverlaySurface,&probe::foreground,&host}){if(*h&&IsWindow(*h))DestroyWindow(*h);*h=nullptr;}
 owner=nullptr;initialized=false;available=false;inputPowerAllowed=nullptr;
 try{drain();}catch(...){ok=false;}
 result["monitorEventOwnersAfterClose"]=ymcc::desktophud::CarrierTestAccess::owners();
 result["overlayHooksAfterClose"]=overlay.ownedHookCount();result["touchVisibilityWatchingAfterClose"]=ownerVisibilityActive();
 const auto liveTestHooks=std::count_if(probe::hooks.begin(),probe::hooks.end(),[](const auto& h){return h.handle!=nullptr;});
 result["trackedOverlayHooksAfterClose"]=liveTestHooks;
 const bool restored=SetThreadDesktop(original)!=FALSE,closed=CloseDesktop(desktop)!=FALSE;
 result["privateDesktopClosed"]=closed;result["threadDesktopRestored"]=restored;result["inputDesktopUnchanged"]=before==inputDesktop();
 result["cases"]=cases;result["pass"]=ok&&restored&&closed&&before==inputDesktop()&&!overlay.ownedHookCount()&&!ownerVisibilityActive()&&ymcc::desktophud::CarrierTestAccess::owners()==0&&liveTestHooks==0;
 std::ofstream out{std::filesystem::path(argv[1])};out<<result.dump(2);return result["pass"].get<bool>()&&out.good()?0:1;
}
