// Verbatim production configuration and auto-hide functions; deterministic HWND,
// drawing and clock boundaries. No Windows headers, devices or input injection.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <deque>
#include <functional>
#include <vector>
#include "json.hpp"
using Json=nlohmann::json;
using UINT64=unsigned long long;using HRESULT=std::int32_t;using INT_PTR=std::intptr_t;
constexpr HRESULT S_OK=0,E_ABORT=static_cast<HRESULT>(0x80004004u),E_FAIL=static_cast<HRESULT>(0x80004005u);
using UINT=unsigned;using UINT32=unsigned;using DWORD=unsigned long;using ULONGLONG=unsigned long long;using UINT_PTR=std::uintptr_t;
struct RECT {int left=0,top=0,right=100,bottom=50;};
struct Window {bool visible=false;RECT rect{};};using HWND=Window*;
inline HWND HWND_TOPMOST=nullptr;inline void* instance=nullptr;
constexpr unsigned WS_EX_TOPMOST=1,WS_EX_TOOLWINDOW=2,WS_EX_NOACTIVATE=4,WS_EX_NOREDIRECTIONBITMAP=8,WS_EX_LAYERED=16,WS_EX_TRANSPARENT=32,WS_POPUP=64,WS_DISABLED=128,SWP_NOACTIVATE=256;
inline std::deque<Window> createdWindows;
inline HWND CreateWindowExW(unsigned,const wchar_t*,const wchar_t*,unsigned,int x,int y,int w,int h,HWND,void*,void*,void*){createdWindows.push_back({false,{x,y,x+w,y+h}});return &createdWindows.back();}
inline bool SetWindowPos(HWND h,HWND,int x,int y,int w,int height,unsigned){if(!h)return false;h->rect={x,y,x+w,y+height};return true;}

#define CALLBACK
constexpr int SW_HIDE=0,SW_SHOWNOACTIVATE=4,USER_TIMER_MINIMUM=10;
constexpr DWORD ERROR_GEN_FAILURE=31,ERROR_NOT_ENOUGH_MEMORY=8,ERROR_NOT_READY=21;
inline ULONGLONG clockNow=1000;inline unsigned timerArms=0,timerStops=0,draws=0,refreshes=0,releases=0,contactSyncs=0;
inline UINT_PTR lastTimer=0;inline HWND lastTimerWindow=nullptr;inline ULONGLONG lastDelay=0;
inline bool IsWindow(HWND h){return h!=nullptr;}
inline bool IsWindowVisible(HWND h){return h&&h->visible;}
inline void ShowWindow(HWND h,int command){if(h)h->visible=command!=SW_HIDE;}
inline ULONGLONG GetTickCount64(){return clockNow;}
inline DWORD GetLastError(){return 0;}
inline void KillTimer(HWND,UINT_PTR){++timerStops;}
inline UINT_PTR SetTimer(HWND h,UINT_PTR id,UINT delay,void(*)(HWND,UINT,UINT_PTR,DWORD)){++timerArms;lastTimer=id;lastTimerWindow=h;lastDelay=delay;return id;}
namespace ymcc::screeninput {
inline bool ensureDisplay(HWND h){return h!=nullptr;}
struct Event{};using Sink=void(*)(HWND,const Event&,void*);struct Runtime{};
struct Surface {
 HWND root_=nullptr;HRESULT error=S_OK;bool ready=true;unsigned calls=0,attaches=0,closes=0,cancels=0;
 std::deque<HRESULT> failures;std::function<void()> duringSync,duringClose;
 HWND root()const{return root_;}bool active()const{return root_&&ready;}
 bool invoke(){++calls;const auto code=failures.empty()?S_OK:failures.front();if(!failures.empty())failures.pop_front();auto callback=std::move(duringSync);duringSync={};if(callback)callback();error=code;ready=code==S_OK;return ready;}
 bool sync(bool){return invoke();}
 bool attach(Runtime&,HWND h,int,Sink,void*){++attaches;root_=h;return invoke();}
 void cancel(){++cancels;}
 void close(){++closes;root_=nullptr;ready=false;auto callback=std::move(duringClose);duringClose={};if(callback)callback();}
};
}
namespace sp {
#include "types.inc"
#include "config.inc"
#include "auto_hide_state.inc"
inline AutoHideState autoHide;
inline bool initialized=true,inputTeardown=false;
inline DWORD windowError=0;
inline std::mutex mutex;
struct Core {Config config;struct Pad{bool down=false;};std::array<Pad,2> pads;};inline Core core;
struct Feedback{unsigned mask(){return 0;}};inline Feedback feedbackOnlyContacts;
inline std::atomic<int> activeProfile{1},appliedProfile{1};
inline std::atomic<bool> enabled{true},nativeMode{true},available{true},psAvailable{false},controlTargetEnabled{true},controlEdgeEnabled{false};
inline Window ownerStorage;inline std::atomic<HWND> owner{&ownerStorage};
inline std::array<Window,2> inputStorage{},outlineStorage{};
inline std::array<Window,7> buttonInputStorage{},buttonOutlineStorage{};
inline std::array<HWND,2> windows{},outlines{};
inline std::array<HWND,7> buttonWindows{},buttonOutlines{};
inline bool nativeInputCandidateRequested=true;
inline ymcc::screeninput::Runtime nativeInputRuntime;
inline std::array<ymcc::screeninput::Surface,7> nativeButtonSurfaces;
inline UINT64 buttonRefreshEpoch=0;
#include "recovery_state.inc"
inline ButtonInputRecovery buttonInputRecovery;
inline void nativeButtonSink(HWND,const ymcc::screeninput::Event&,void*){}

inline std::array<UINT32,7> buttonContacts{};
inline void (*quickAccessContextObserver)()=nullptr;
inline Config getConfig(){std::lock_guard<std::mutex> lock(mutex);return core.config;}
inline bool nativePad(Mode mode){return mode==Mode::Deck||mode==Mode::DualSense;}
inline bool permitted(Mode mode){return mode!=Mode::Off&&(mode!=Mode::Deck||available.load())&&(mode!=Mode::DualSense||psAvailable.load());}
inline Mode surfaceMode(const Config& cfg,int side){return cfg.layout==Layout::Single?cfg.single:(side?cfg.right:cfg.left);}
#include "selectors.inc"
#include "button_permitted.inc"
inline bool renderOutline(int,int){++draws;return true;}
inline bool renderButton(int,int,bool=false){++draws;return true;}
inline void buttonsRepaint(int){++draws;}
inline void syncContactVisuals(){++contactSyncs;}
inline std::array<RECT,7> buttonRects(const RECT&,int,const RECT*,SummonPosition,unsigned){return {};}
#include "button_sync.inc"
#include "buttons_refresh.inc"
#include "buttons_need_refresh.inc"
inline void autoHideSync(bool reset=false);
#include "screen_touchpad_auto_hide.h"
inline void release(){++releases;core.pads={};buttonContacts={};autoHide.specialContacts={};}
// A layout collaborator only: real configuration, visibility-mask computation,
// wake-up rendering, timeout calculation and timer callbacks run from production.
inline void refresh(){
 ++refreshes;windowError=0;const auto cfg=getConfig();
 for(int i=0;i<2;++i){const bool show=cfg.masterEnabled&&cfg.enabled&&(cfg.layout!=Layout::Single||i==0)&&permitted(surfaceMode(cfg,i));ShowWindow(windows[i],show?SW_SHOWNOACTIVATE:SW_HIDE);ShowWindow(outlines[i],show&&!autoHide.hidden?SW_SHOWNOACTIVATE:SW_HIDE);}
 if(!buttonsRefresh(cfg,{}, {},false))return;
 autoHideSync();
}
#include "configure.inc"
#include "button_visibility.inc"
}
using namespace sp;
inline Json cases=Json::array();inline bool passed=true;
void check(const char* name,bool ok){cases.push_back({{"name",name},{"ok",ok}});passed=passed&&ok;}
void setup(int profile=1,bool pads=false,StandaloneSpecialMode standalone=StandaloneSpecialMode::Off){
 initialized=true;inputTeardown=false;windowError=0;clockNow=1000;timerArms=timerStops=draws=refreshes=releases=contactSyncs=0;lastTimer=0;
 autoHide={};core={};core.config.enabled=pads;core.config.specialMask=profile==3?1:3;core.config.specialEnabled=profile!=0;core.config.standaloneSpecialMode=standalone;
 if(profile==0)core.config.specialMask=0;
 activeProfile=profile;appliedProfile=profile;controlTargetEnabled=profile!=0;controlEdgeEnabled=profile==2;available=profile==1;psAvailable=profile==2;
 buttonContacts={};buttonRefreshEpoch=0;buttonInputRecovery={};nativeInputCandidateRequested=true;createdWindows.clear();
 for(int i=0;i<2;++i){inputStorage[i]={};outlineStorage[i]={};windows[i]=&inputStorage[i];outlines[i]=&outlineStorage[i];}
 for(int i=0;i<7;++i){buttonInputStorage[i]={};buttonOutlineStorage[i]={};buttonWindows[i]=&buttonInputStorage[i];buttonOutlines[i]=&buttonOutlineStorage[i];}
 for(int i=0;i<7;++i){nativeButtonSurfaces[i]={};nativeButtonSurfaces[i].root_=buttonWindows[i];}
 refresh();
}
void expire(){clockNow=autoHide.lastTouch+60000;autoHideTimerProc(autoHide.timerWindow,0,autoHide.timer,0);}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 const std::array<int,5> profiles={0,0,1,2,3};
 const std::array<const char*,5> names={"standalone-Steam","standalone-PS5","Steam-Deck","PS5","Xbox"};
 for(int i=0;i<5;++i){
  setup(profiles[i],false,i==0?StandaloneSpecialMode::SteamDeck:i==1?StandaloneSpecialMode::PS5:StandaloneSpecialMode::Off);
  const Config saved=getConfig();expire();const auto hidden=buttonVisibilityState(saved);
  check((std::string(names[i])+"-idle-hides-artwork-only").c_str(),autoHide.hidden&&hidden["inputVisibleMask"]!=0&&hidden["artworkVisibleMask"]==0);
  const unsigned beforeRefresh=refreshes,beforeRelease=releases;const auto stale=lastTimer;const auto staleWindow=lastTimerWindow;
  const bool reset=configure(saved,profiles[i],true);
  const auto visible=buttonVisibilityState(saved);
  check((std::string(names[i])+"-explicit-same-selection-reveals-and-restarts").c_str(),!autoHide.hidden&&autoHide.timer&&visible["artworkVisibleMask"]==visible["permittedMask"]&&autoHide.lastTouch==clockNow);
  check((std::string(names[i])+"-same-selection-does-not-rebuild-or-release-input").c_str(),!reset&&refreshes==beforeRefresh&&releases==beforeRelease);
  // Keep independent timer assertions meaningful in the old-policy negative.
  if(autoHide.hidden)autoHideSync(true);
  const auto timer=autoHide.timer;clockNow+=30000;autoHideTimerProc(staleWindow,0,stale,0);
  check((std::string(names[i])+"-old-queued-timer-cannot-hide-new-preview").c_str(),autoHide.timer==timer&&!autoHide.hidden);
 }
 setup(1,true);expire();auto saved=getConfig();configure(saved,1);refresh();
 check("ordinary-refresh-preserves-auto-hidden-state",autoHide.hidden&&autoHide.timer==0&&buttonVisibilityState(saved)["artworkVisibleMask"]==0);
 setup(1,true);expire();saved=getConfig();configure(saved,1,true);const auto deadline=autoHide.lastTouch;clockNow=deadline+60000;autoHideTimerProc(autoHide.timerWindow,0,autoHide.timer,0);
 check("explicit-preview-still-expires-after-full-timeout",autoHide.hidden&&autoHide.timer==0);
 setup(1,true);expire();saved=getConfig();saved.specialMask=2;configure(saved,1,true);
 check("changed-special-selection-shows-only-selected-key",!autoHide.hidden&&buttonVisibilityState(saved)["artworkVisibleMask"]==4);
 setup(1,false);expire();saved=getConfig();saved.specialMask=0;saved.specialEnabled=false;configure(saved,1,true);
 check("select-off-does-not-show-buttons-or-arm-timer",!autoHide.hidden&&!autoHide.timer&&buttonVisibilityState(saved)["artworkVisibleMask"]==0);
 setup(1,true);expire();saved=getConfig();saved.masterEnabled=false;configure(saved,1,true);
 check("master-off-does-not-show-buttons-or-arm-timer",!autoHide.timer&&buttonVisibilityState(saved)["inputVisibleMask"]==0);
 setup(1,true);expire();saved=getConfig();saved.autoHideSeconds=0;configure(saved,1,true);
 check("disable-auto-hide-restores-artwork-without-timer",!autoHide.hidden&&!autoHide.timer&&buttonVisibilityState(saved)["artworkVisibleMask"]==6);
 setup(1,true);expire();saved=getConfig();inputTeardown=true;const auto priorArms=timerArms;configure(saved,1,true);
 check("terminal-owner-refuses-settings-reveal",autoHide.hidden&&timerArms==priorArms&&buttonVisibilityState(saved)["artworkVisibleMask"]==0);
 setup(1,false);expire();saved=getConfig();saved.summonPosition=SummonPosition::Right;saved.rearMask=15;configure(saved,1,true);expire();
 check("summon-and-rear-keys-retain-original-nonhiding-behavior",autoHide.hidden&&buttonVisibilityState(saved)["artworkVisibleMask"]==121);
 setup(1,true);expire();saved=getConfig();configure(saved,1,true);if(autoHide.hidden)autoHideSync(true);const auto arms=timerArms,paint=draws;for(int i=0;i<10000;++i){clockNow+=1;autoHideTouch();}
 check("10000-touch-moves-do-not-rearm-or-redraw",timerArms==arms&&draws==paint);
 core.pads[0].down=true;clockNow+=60000;autoHideTimerProc(autoHide.timerWindow,0,autoHide.timer,0);
 check("stationary-held-contact-prevents-hiding-and-polling",!autoHide.hidden&&!autoHide.timer);
 core.pads[0].down=false;autoHideTouch();
 check("release-resumes-single-deadline",autoHide.timer&&lastDelay==60000);
 setup(1,false);saved=getConfig();activeProfile=2;
 check("visibility-diagnostics-separate-permission-from-old-windows",buttonVisibilityState(saved)["permittedMask"]==0&&buttonVisibilityState(saved)["inputVisibleMask"]==6);
 // Field failure: left key missing, right key visible, selected mask unchanged.
 setup(1,false);saved=getConfig();buttonInputStorage[1].visible=buttonOutlineStorage[1].visible=false;nativeButtonSurfaces[1].ready=false;
 check("same-settings-repair-hidden-missing-button",configure(saved,1,true)&&buttonVisibilityState(saved)["artworkVisibleMask"]==6&&nativeButtonSurfaces[1].active());
 setup(1,false);saved=getConfig();buttonOutlineStorage[1].visible=false;
 check("same-settings-repair-unexpected-hidden-artwork",configure(saved,1,true)&&buttonVisibilityState(saved)["artworkVisibleMask"]==6);
 setup(1,false);nativeButtonSurfaces[1].failures={E_ABORT,S_OK};const auto beforeCalls=nativeButtonSurfaces[1].calls;refresh();
 check("sync-E_ABORT-single-rebind-restores-both-keys",!windowError&&buttonVisibilityState(getConfig())["artworkVisibleMask"]==6&&nativeButtonSurfaces[1].calls==beforeCalls+2&&buttonInputRecovery.recovered==1);
 setup(1,false);nativeButtonSurfaces[1].root_=nullptr;nativeButtonSurfaces[1].failures={E_ABORT,S_OK};refresh();
 check("attach-E_ABORT-single-rebind-restores-both-keys",!windowError&&buttonVisibilityState(getConfig())["artworkVisibleMask"]==6&&nativeButtonSurfaces[1].attaches==2&&buttonInputRecovery.recovered==1);
 setup(1,false);nativeButtonSurfaces[1].failures={E_ABORT,E_ABORT,S_OK};refresh();const auto permanentCalls=nativeButtonSurfaces[1].calls;
 check("repeated-E_ABORT-stops-after-two-attempts",windowError==static_cast<DWORD>(E_ABORT)&&buttonVisibilityState(getConfig())["artworkVisibleMask"]==4&&buttonInputRecovery.failed==1&&nativeButtonSurfaces[1].failures.size()==1);
 // Keep the manual-repair assertion independent from the retry negative.
 buttonInputStorage[1].visible=buttonOutlineStorage[1].visible=false;nativeButtonSurfaces[1].ready=false;
 nativeButtonSurfaces[1].failures={S_OK};configure(getConfig(),1,true);
 check("same-settings-recover-after-bounded-retry-exhaustion",!windowError&&buttonVisibilityState(getConfig())["artworkVisibleMask"]==6&&nativeButtonSurfaces[1].calls>permanentCalls);
 setup(1,false);nativeButtonSurfaces[1].failures={E_FAIL,S_OK};refresh();
 check("other-errors-keep-error-without-automatic-retry",windowError==static_cast<DWORD>(E_FAIL)&&buttonVisibilityState(getConfig())["artworkVisibleMask"]==4&&buttonInputRecovery.retries==0&&nativeButtonSurfaces[1].failures.size()==1);
 setup(1,false);nativeButtonSurfaces[1].failures={E_ABORT};nativeButtonSurfaces[1].duringSync=[](){inputTeardown=true;initialized=false;for(auto h:buttonWindows)ShowWindow(h,SW_HIDE);for(auto h:buttonOutlines)ShowWindow(h,SW_HIDE);};refresh();
 check("exit-during-sync-no-retry-no-later-button-reshow",buttonInputRecovery.retries==0&&buttonVisibilityState(getConfig())["inputVisibleMask"]==0);
 setup(1,false);nativeButtonSurfaces[1].failures={E_ABORT};nativeButtonSurfaces[1].duringClose=[](){inputTeardown=true;initialized=false;for(auto h:buttonWindows)ShowWindow(h,SW_HIDE);};refresh();
 check("exit-during-recovery-close-no-reattach",buttonInputRecovery.retries==0&&nativeButtonSurfaces[1].root()==nullptr);
 setup(1,false);nativeButtonSurfaces[1].failures={E_ABORT};nativeButtonSurfaces[1].duringSync=[](){activeProfile=2;};refresh();
 check("changed-persona-during-sync-no-retry-or-close-new-source",buttonInputRecovery.retries==0&&nativeButtonSurfaces[1].closes==0);
 setup(1,false);nativeButtonSurfaces[1].failures={E_ABORT};nativeButtonSurfaces[1].duringSync=[](){controlTargetEnabled=false;};refresh();
 check("disabled-target-during-sync-no-retry",buttonInputRecovery.retries==0&&nativeButtonSurfaces[1].closes==0);
 setup(1,false);nativeButtonSurfaces[1].failures={E_ABORT};nativeButtonSurfaces[1].duringSync=[](){nativeInputCandidateRequested=false;};refresh();
 check("disabled-native-input-during-sync-no-retry",buttonInputRecovery.retries==0&&nativeButtonSurfaces[1].closes==0);
 setup(1,false);nativeButtonSurfaces[1].failures={E_ABORT};nativeButtonSurfaces[1].duringSync=[](){++buttonRefreshEpoch;nativeButtonSurfaces[1].root_=buttonWindows[1];};refresh();
 check("superseded-refresh-does-not-close-newer-source",buttonInputRecovery.retries==0&&nativeButtonSurfaces[1].closes==0);
 setup(1,false);inputTeardown=true;initialized=false;const auto terminalCalls=nativeButtonSurfaces[1].calls;buttonNativeInputSync(1,true);
 check("terminal-before-sync-makes-no-native-call",nativeButtonSurfaces[1].calls==terminalCalls);
 setup(1,false);const auto unchangedCalls=nativeButtonSurfaces[1].calls;configure(getConfig(),1,true);
 check("healthy-same-selection-adds-no-source-sync",nativeButtonSurfaces[1].calls==unchangedCalls);
 const auto failed=std::count_if(cases.begin(),cases.end(),[](const Json& c){return !c["ok"].get<bool>();});
 std::ofstream out(argv[1]);out<<Json{{"ok",passed},{"checks",cases.size()},{"failed",failed},{"cases",cases},{"realWindowsCreated",false},{"inputInjected",false}}.dump(2);
 std::cout<<"checks="<<cases.size()<<" failed="<<failed<<"\n";return passed?0:1;
}
