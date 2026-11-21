// Actual surface lifecycle functions plus real Carrier/DisplayTreeLease.
// WebView/COM and panel input attachment are declared faultable ports.
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <json.hpp>
static unsigned removeFailures=0,destroyFailures=0;static HWND failedDestroy=nullptr;
static BOOL fixtureRemoveSubclass(HWND h,SUBCLASSPROC p,UINT_PTR id){if(removeFailures){--removeFailures;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::RemoveWindowSubclass(h,p,id);}
static BOOL fixtureDestroy(HWND h){if(destroyFailures){--destroyFailures;failedDestroy=h;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::DestroyWindow(h);}
#define RemoveWindowSubclass fixtureRemoveSubclass
#include "game_overlay_presentation.h"
#include "game_overlay_display_tree_lease.h"
#include "shortcut_window_toggle.h"
#include "window_taskbar_layout.h"
#include "sleep_power_dispatch.h"
#undef RemoveWindowSubclass
using json=nlohmann::json;
static ymcc::gameoverlay::Carrier g_gameOverlay;
static HWND g_hwnd=nullptr,g_gameOverlaySurface=nullptr,child=nullptr;
static bool g_gameOverlaySurfaceAttached=false,g_webviewReady=true,g_deferFirstShow=false;
static std::atomic<bool> g_fullHeight=false;
static std::atomic<bool> g_exitRequested=false,g_shortcutRecordingActive=false;
static ymcc::sleeppower::SharedPowerV1 sharedPower;
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* value,std::uint32_t bytes){if(!value||bytes<sizeof(*value))return 0;*value=sharedPower;return 1;}
struct FocusSessionPort{bool active=false,lightNoGame=false,returning=false,lightForegroundAcquired=false;};static FocusSessionPort g_focusSession;
static unsigned returnFailures=0,attachFailures=0,submitFailures=0,retargetCalls=0;
struct ControllerPort {bool visible=false;HRESULT put_IsVisible(BOOL v){visible=v!=FALSE;return S_OK;}HRESULT put_Bounds(RECT){return submitFailures?(--submitFailures,E_FAIL):S_OK;}HRESULT NotifyParentWindowPositionChanged(){return S_OK;}};
struct ViewPort{HRESULT ExecuteScript(const wchar_t*,void*){return S_OK;}};
static ControllerPort controller;static ViewPort view;static ControllerPort* g_ctrl=&controller;static ViewPort* g_view=&view;
struct CompositionPort {
 bool enabled=true,background=false;HWND parent=nullptr;
 bool active()const{return enabled;}
 bool handle(HWND,UINT,WPARAM,LPARAM,LRESULT&){return false;}
 void setBackgroundInput(bool on){background=on;}
 HRESULT retarget(HWND h){++retargetCalls;if(h==g_hwnd&&returnFailures){--returnFailures;return E_FAIL;}parent=h;if(child)SetParent(child,h);return S_OK;}
};static CompositionPort g_mainWebViewComposition;
struct PanelPort {
 bool attached=false;void close(){attached=false;}
 bool attach(HWND,CompositionPort&){if(attachFailures){--attachFailures;return false;}attached=true;return true;}
 bool synchronize(){return attached;}bool ready()const{return attached;}bool retired()const{return false;}
};static PanelPort g_gameOverlayPanel;
static ymcc::gameoverlayinput::DisplayTreeLease g_gameOverlayDisplayTree;
struct EditorPort{unsigned owner=0;bool enabled=false;bool active()const{return enabled;}};static EditorPort g_controllerEditorWidth;
struct ReturnPort{void invalidate(){}void begin(std::uint64_t,ULONGLONG){}void end(std::uint64_t,ULONGLONG){}};static ReturnPort g_gameOverlayReturn;
static std::atomic<std::shared_ptr<const ymcc::gameoverlay::Binding>> g_gameOverlayReturning;
struct FocusTargetSnapshot{HWND hwnd=nullptr;DWORD pid=0;bool valid=false;};
static HWND summonForeground=nullptr;
static ULONGLONG summonTick=100;
static long long g_nativeKeyboardShortcutRevision=1;
static bool g_mainWindowWasMaximized=false;
static bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& out);
static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& expected);
static void gameOverlaySurfaceRecord(const char*,const json& =json::object())noexcept{}
static void endControllerEditorWidth(unsigned){g_controllerEditorWidth.enabled=false;}
static bool radialHasInputOwner(){return false;}static bool customSteamLibraryInputSuppressed(){return false;}
static bool focusTargetIdentityMatches(const FocusTargetSnapshot&,DWORD){return true;}
static std::uint64_t currentPowerGeneration(){return 1;}
static bool gameOverlayBindingCurrent(const ymcc::gameoverlay::Binding& b){return ymcc::presentationpower::current(&readSharedSystemPower,b.sharedPower,b.originalUiInput&&g_gameOverlay.inputReady());}
static bool applyControllerEditorWidth(){return true;}static bool applyFullHeightLayout(HMONITOR){return true;}static bool windowPlacementIsLeft(){return false;}
static void closeSplash(){}static void controllerIsolationOverlayRendererProbe(){}static void ipc_emit(const char*,const json&){}
static void gameOverlayStop(const char* reason){g_gameOverlay.end(reason);}
static constexpr UINT SUMMON_FOCUS_TIMER_ID=1,RETURN_GAME_FOCUS_TIMER_ID=2;
static LRESULT windowOverlayPrelude(HWND h,UINT m,WPARAM w,LPARAM l){
#include "window-overlay-prelude.inc"
 return -1;
}
static bool gameOverlayOwnPresentationPeer(HWND){return false;}
#define DestroyWindow fixtureDestroy
#include "surface-consumers.inc"
#undef DestroyWindow
enum class GameOverlayAttempt{Ordinary,Begun,Refused};
static std::uint64_t g_summonGeneration=0;static std::atomic<std::uint64_t> g_summonRegistrationGeneration=0;
static std::atomic<bool> g_focusIsolationGameOpen=false;static FocusTargetSnapshot g_pendingSummonTarget;
static unsigned registrationCalls=0,ordinaryShows=0;static bool refuseRoute=false,ordinaryRoute=false;
static GameOverlayAttempt gameOverlayTryBegin(const FocusTargetSnapshot& target,bool maximize){if(ordinaryRoute)return GameOverlayAttempt::Ordinary;return !refuseRoute&&gameOverlaySurfaceShow(target,[]{ymcc::gameoverlay::Binding b;b.globalProbe=true;b.originalUiInput=true;b.power=1;return b;}(),false,{},maximize)?GameOverlayAttempt::Begun:GameOverlayAttempt::Refused;}
static void focusShortcutInputEvidence(const char*,std::uint64_t,bool){}static void appendNativeLifecycleLog(const char*,const json&){}
static std::atomic<bool> g_focusIsolationPrepared=false;
static std::atomic<ULONGLONG> g_focusLightProofNotBefore=0;
static std::pair<std::uint64_t,std::uint64_t> g_focusIsolationPowerPeriod;
static unsigned focusPowerReturns=0;
static void focusPrepareIsolationBeforeShow(){g_focusIsolationPrepared=true;g_focusIsolationPowerPeriod={sharedPower.generation,sharedPower.suspendSerial};}
static void focusEndIsolationOnUi(){g_focusIsolationPrepared=false;}
static void focusClearSession(){++focusPowerReturns;g_focusSession={};g_gameOverlay.end("controller-focus-return");}
static void focusBeginReturnToPreviousWindow(ULONGLONG){ShowWindow(g_hwnd,SW_HIDE);}
static bool focusLightNoGameQualified(ULONGLONG){return false;}static bool focusLightIsolationCurrent(){return true;}
static json nativeSummonCandidateSnapshot(const FocusTargetSnapshot&,std::uint64_t generation){return{{"generation",generation}};}
static void queueSummonGameRegistration(const FocusTargetSnapshot&,std::uint64_t){++registrationCalls;}
static void bringToFront(HWND h,bool maximize){++ordinaryShows;ShowWindow(h,maximize?SW_MAXIMIZE:SW_SHOWNOACTIVATE);g_ctrl->put_IsVisible(TRUE);}
#define GetForegroundWindow() summonForeground
#define GetTickCount64() summonTick
#include "summon-consumer.inc"
#undef GetTickCount64
#undef GetForegroundWindow
// All other public power consumers are inert ports. The actual projector and
// actual monitor delivery deduplication are extracted unchanged from main.cpp.
static std::atomic<bool> g_sharedPowerProjectionPending=false,g_sensorSuspendCallbackActive=false;
static std::atomic<bool> g_sleepPowerRedeliveryArmed=false;
static std::atomic<bool> g_powerResumeCommitRedelivery=false,g_powerInputRecoverRedelivery=false;
static std::atomic<std::uint64_t> g_sharedPowerPublicationRevision=0;
static unsigned redeliveries=0,otherPowerConsumers=0,monitorInvalidations=0;
static void sleepPowerArmRedelivery(HWND){++redeliveries;g_sleepPowerRedeliveryArmed=true;}
static void sleepPowerArmUnknownRedelivery(HWND window,unsigned long long){sleepPowerArmRedelivery(window);}
static void sharedPowerProjectionConsumerFailed(const char*,const ymcc::sleeppower::SharedPowerV1* =nullptr,const std::uint64_t* =nullptr){}
static bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& value){value=sharedPower;return ymcc::sleeppower::powerAdmitted(value);}
static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& value){return ymcc::sleeppower::powerAdmitted(sharedPower)&&ymcc::sleeppower::samePowerRevocation(value,sharedPower);}
struct MonitorPort{unsigned deliveries=0;void sharedPowerChanged(){++deliveries;}};static MonitorPort g_desktopMonitorHud,g_monitorNoticeCarrier;
static std::mutex g_nativeMonitorMx;struct MonitorCvPort{unsigned notifications=0;void notify_all(){++notifications;}};static MonitorCvPort g_nativeMonitorCv;
static void monitorSnapshotInvalidate(){++monitorInvalidations;}
static void inputCaptureResumeSensorsForPower(){++otherPowerConsumers;}
static bool msiNativeForegroundSharedPowerReady(const ymcc::sleeppower::SharedPowerV1&){++otherPowerConsumers;return true;}
static bool sharedSleepWakeClassified(std::uint64_t){return true;}static bool sgSleepRetryActive(){return false;}
namespace ymcc::sleeppower {
struct WakePort {struct Snapshot{std::uint64_t revision=1;};void ready(std::uint64_t){}Snapshot snapshot()const{return {};}};static WakePort nativeWakeFacts;
static bool wakeDeliveryAlreadyCurrent(bool known,const SharedPowerV1& before,const SharedPowerV1& now,std::uint64_t beforeWake,std::uint64_t nowWake){return known&&samePowerRevocation(before,now)&&beforeWake==nowWake;}
}
struct SleepRecoveryAuto{static void powerReady(std::uint64_t){++otherPowerConsumers;}};
static void steamOriginalRecoveryPowerReady(std::uint64_t){++otherPowerConsumers;}
namespace ymcc::lossless {struct RuntimePort{void powerChanged(){++otherPowerConsumers;}};static RuntimePort& runtimeMonitor(){static RuntimePort value;return value;}}
struct SteamControllerCursor{static void power(const ymcc::sleeppower::SharedPowerV1&){++otherPowerConsumers;}};
namespace ymcc::screenpads {static void ownerSharedPowerChanged(const sleeppower::SharedPowerV1&){++otherPowerConsumers;}}
namespace ymcc::inputHost::msiPowerWork {static void readyDelivery(const sleeppower::SharedPowerV1&){++otherPowerConsumers;}}
static void sleepPowerRedeliverCommitted(HWND){++otherPowerConsumers;}
enum class FanReadyDelivery { Idle, Settled, Deferred };
static std::uint64_t currentFanPowerGeneration(){return sharedPower.generation;}
static FanReadyDelivery fanHostScheduleEmergencyResume(std::uint64_t){++otherPowerConsumers;return FanReadyDelivery::Settled;}
static void steamAppearancePublicReady(unsigned long long)noexcept{++otherPowerConsumers;}
static void sgOfferStartupResume(const ymcc::sleeppower::SharedPowerV1&){++otherPowerConsumers;}
static json sharedPowerLifecycleSnapshot(){return {{"known",ymcc::sleeppower::powerObservationKnown(sharedPower)},
 {"hardwareWritesAllowed",ymcc::sleeppower::powerAdmitted(sharedPower)},{"generation",sharedPower.generation},{"phase",sharedPower.phase}};}
#include "public-power-consumers.inc"
static void publishPowerToActualProjector(){sharedPower.publicationRevision=++g_sharedPowerPublicationRevision;g_sharedPowerProjectionPending=true;projectSharedPowerLifecycle(g_hwnd);}
static void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static ymcc::gameoverlay::Binding binding(){ymcc::gameoverlay::Binding b;b.globalProbe=true;b.originalUiInput=true;b.power=1;return b;}
static void cleanup(){sharedPower={};sharedPower.generation=1;sharedPower.flags=ymcc::sleeppower::kKnown;removeFailures=destroyFailures=0;g_gameOverlay.end("fixture-cleanup");returnFailures=attachFailures=submitFailures=0;gameOverlaySurfaceRestore();if(failedDestroy&&IsWindow(failedDestroy))DestroyWindow(failedDestroy);failedDestroy=nullptr;g_mainWebViewComposition.enabled=true;g_ctrl=&controller;g_view=&view;}
#include "game_overlay_surface_render_ready_cases.inc"
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;json result={{"scope","actual surface and render-ready callback; real owned private-desktop HWNDs, Carrier and DisplayTreeLease; declared COM/panel attachment fault ports; no full renderer or physical input"}};json cases=json::array();bool ok=true;
 const auto original=GetThreadDesktop(GetCurrentThreadId());auto name=L"YMCC_SurfaceStorm_"+std::to_wstring(GetCurrentProcessId());auto desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS,nullptr);if(!desktop||!SetThreadDesktop(desktop))return 3;
 WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=DefWindowProcW;cls.lpszClassName=L"YMCC_SurfaceStorm_Original";RegisterClassW(&cls);
 g_hwnd=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_NOREDIRECTIONBITMAP,cls.lpszClassName,L"",WS_POPUP,0,0,500,600,nullptr,nullptr,cls.hInstance,nullptr);
 child=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,20,20,g_hwnd,nullptr,cls.hInstance,nullptr);g_mainWebViewComposition.parent=g_hwnd;
 auto test=[&](const char* name,auto action){try{cleanup();action();cases.push_back({{"case",name},{"pass",true}});}catch(const std::exception& e){ok=false;cases.push_back({{"case",name},{"pass",false},{"failure",e.what()}});}cleanup();};
 test("96-setup-show-hide-return-cycles",[]{for(unsigned i=0;i<96;++i){need(gameOverlaySurfaceShow({},binding(),false),"show");need(g_gameOverlay.active()&&GetParent(child)==g_gameOverlaySurface,"same controller child borrowed");g_gameOverlay.end("normal-close");need(!g_gameOverlaySurface&&!g_gameOverlaySurfaceAttached&&GetParent(child)==g_hwnd&&IsWindowEnabled(child),"full return");}});
 test("original-summon-handler-400-toggle-actions-keep-one-route",[]{const auto start=g_summonGeneration;const auto registrations=registrationCalls;for(unsigned i=0;i<400;++i){need(nativeYmccShortcutSummon({}),"actual original summon accepted");need(g_gameOverlay.active()==(i%2==0),"original toggle owns one presentation");need(!IsWindowVisible(g_hwnd),"floating route cannot show original main HWND");need(g_summonRegistrationGeneration.load()==g_summonGeneration,"hide invalidates late original registration generation too");}need(g_summonGeneration==start+400&&registrationCalls==registrations+200&&ordinaryShows==0,"one registration for each actual open, no ordinary route");});
 test("failed-floating-route-does-not-open-ordinary-window",[]{refuseRoute=true;const auto registrations=registrationCalls;const bool accepted=nativeYmccShortcutSummon({});refuseRoute=false;need(!accepted,"refused action must not report success");need(!g_gameOverlay.active()&&!IsWindowVisible(g_hwnd)&&registrationCalls==registrations&&ordinaryShows==0,"refusal cannot create duplicate presentation or registration");});
 test("minimized-main-retains-state-and-restores-maximized-surface",[]{
  ShowWindow(g_hwnd,SW_SHOWMINNOACTIVE);g_mainWindowWasMaximized=true;
  need(IsWindowVisible(g_hwnd)&&IsIconic(g_hwnd),"test requires visible minimized HWND");
  need(nativeYmccShortcutSummon({})&&g_gameOverlay.active(),"summon from minimized main");
  RECT actual{},wanted{};need(GetWindowRect(g_gameOverlaySurface,&actual)&&ymcc::windowlayout::safeMonitorArea(MonitorFromWindow(g_gameOverlaySurface,MONITOR_DEFAULTTONEAREST),wanted)&&EqualRect(&wanted,&actual),"floating surface uses actual maximized work area");
  need(IsIconic(g_hwnd)&&nativeYmccShortcutSummon({})&&!g_gameOverlay.active()&&IsIconic(g_hwnd)&&GetParent(child)==g_hwnd,"close restores parent without raising the main window");
  g_mainWindowWasMaximized=false;ShowWindow(g_hwnd,SW_RESTORE);ShowWindow(g_hwnd,SW_HIDE);
 });
 test("delayed-render-coalesces-and-replays-once-with-maximize",[]{
  summonForeground=g_hwnd;const FocusTargetSnapshot target{g_hwnd,GetCurrentProcessId(),true};
  const auto start=registrationCalls;g_webviewReady=false;summonTick=100;
  need(nativeYmccShortcutSummon(target)&&!g_gameOverlay.active()&&registrationCalls==start,"accepted while renderer is busy without touching display");
  ++summonTick;need(nativeYmccShortcutSummon(target,true)&&g_pendingShortcutSummon->requestedAt==100,"second edge coalesces and does not extend deadline");
  summonTick=2400;g_webviewReady=true;replayShortcutSummonAfterRender();replayShortcutSummonAfterRender();
  need(g_gameOverlay.active()&&!g_pendingShortcutSummon&&registrationCalls==start+1,"one presentation on delayed readiness");
  RECT actual{},wanted{};need(GetWindowRect(g_gameOverlaySurface,&actual)&&ymcc::windowlayout::safeMonitorArea(MonitorFromWindow(g_gameOverlaySurface,MONITOR_DEFAULTTONEAREST),wanted)&&EqualRect(&wanted,&actual),"maximize survives render delay");
 });
 test("deferred-request-retires-on-foreground-sleep-rules-expiry-or-explicit-close",[]{
  const auto start=registrationCalls;
  for(int cause=0;cause<6;++cause){
   summonForeground=g_hwnd;g_webviewReady=false;summonTick=100;
   need(nativeYmccShortcutSummon({g_hwnd,GetCurrentProcessId(),true}),"queue deferred request");
   if(cause==0)summonForeground=nullptr;
   if(cause==1)++sharedPower.suspendSerial;
   if(cause==2)++g_nativeKeyboardShortcutRevision;
   if(cause==3)summonTick+=ymcc::kSummonIntentLifetimeMs+1;
   if(cause==4)++g_summonGeneration;
   if(cause==5)shortcutSummonClearRenderWait();
   g_webviewReady=true;replayShortcutSummonAfterRender();
   need(!g_pendingShortcutSummon&&!g_gameOverlay.active()&&registrationCalls==start,"invalidated request never opens");
  }
 });
 test("shared-power-unknown-pending-and-nonready-refuse-before-attachment",[]{for(unsigned shape=0;shape<5;++shape){sharedPower.generation=1;sharedPower.flags=shape==0?0:ymcc::sleeppower::kKnown;sharedPower.phase=shape>1?shape-1:0;if(shape==1)sharedPower.flags|=ymcc::sleeppower::kNotificationPending;const auto calls=retargetCalls;need(!gameOverlaySurfaceShow({},binding(),false)&&!g_gameOverlaySurface&&retargetCalls==calls,"held power cannot borrow or show browser");}});
 test("shared-ready-without-hardware-admission-still-allows-settings",[]{sharedPower.flags=ymcc::sleeppower::kKnown;need(!ymcc::sleeppower::powerAdmitted(sharedPower)&&gameOverlaySurfaceShow({},binding(),false),"presentation Ready must not require hardware admission");});
 test("metadata-only-shared-publication-does-not-recreate-ui",[]{need(gameOverlaySurfaceShow({},binding(),false),"show");const auto surface=g_gameOverlaySurface;const auto calls=retargetCalls;for(unsigned i=0;i<1000;++i){++sharedPower.publicationRevision;g_gameOverlay.sharedPowerChanged(sharedPower);}need(g_gameOverlay.active()&&surface==g_gameOverlaySurface&&calls==retargetCalls,"metadata cannot replay display work");});
 test("late-ready-resume-preserves-committed-original-only",[]{
  need(gameOverlaySurfaceShow({},binding(),false),"show");const auto shown=g_gameOverlay.snapshot();const auto surface=g_gameOverlaySurface;
  ++sharedPower.observedNotificationSequence;publishPowerToActualProjector();
  need(!ymcc::presentationpower::current(shown->sharedPower,sharedPower),"strict admission must reject an old pre-notification ticket");
  need(g_gameOverlay.snapshot()==shown&&g_gameOverlay.inputReady()&&gameOverlayBindingCurrent(*shown)&&GetParent(child)==surface,"ready-only receipt must not retire the committed original UI");
  ++sharedPower.suspendSerial;publishPowerToActualProjector();
  need(!g_gameOverlay.active()&&GetParent(child)==g_hwnd,"a real boundary still returns the original UI");
 });
 test("same-generation-pending-retires-ui-and-ready-does-not-reopen-it",[]{need(gameOverlaySurfaceShow({},binding(),false),"show");sharedPower.flags|=ymcc::sleeppower::kNotificationPending;g_gameOverlay.sharedPowerChanged(sharedPower);need(!g_gameOverlay.active()&&!g_gameOverlaySurface&&GetParent(child)==g_hwnd,"pending notification retires original display");sharedPower.flags=ymcc::sleeppower::kKnown;g_gameOverlay.sharedPowerChanged(sharedPower);need(!g_gameOverlay.active()&&!g_gameOverlaySurface,"Ready cannot reopen a closed presentation");need(gameOverlaySurfaceShow({},binding(),false),"next explicit summon works");});
 test("shared-focus-isolation-preserves-current-session-and-retires-old-suspend-serial",[]{
  sharedPower.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
  for(const bool light:{false,true}){
   const auto returns=focusPowerReturns;
   focusPrepareIsolationBeforeShow();g_focusSession={true,light};
   ++sharedPower.observedNotificationSequence;publishPowerToActualProjector();
   need(g_focusSession.active&&g_focusIsolationPrepared&&focusPowerReturns==returns,"late notification and Ready publication without a new sleep cannot retire the fresh focus session");
   ++sharedPower.suspendSerial;publishPowerToActualProjector();
   need(!g_focusSession.active&&!g_focusIsolationPrepared&&focusPowerReturns==returns+1,"coalesced same-generation sleep retires the old focus session");
   publishPowerToActualProjector();
   need(!g_focusSession.active&&focusPowerReturns==returns+1,"duplicate Ready neither replays nor opens a session");
   focusPrepareIsolationBeforeShow();g_focusSession={true,light};publishPowerToActualProjector();
   need(g_focusSession.active&&focusPowerReturns==returns+1,"fresh post-wake focus is not retired by delayed projection of its own episode");
   sharedPower.phase=1;publishPowerToActualProjector();
   need(!g_focusSession.active&&focusPowerReturns==returns+2,"real non-Ready phase retires even before a serial advances");
   sharedPower.phase=0;publishPowerToActualProjector();
  }
 });
 test("coalesced-suspend-serial-ready-retires-old-presentation",[]{need(gameOverlaySurfaceShow({},binding(),false),"show");++sharedPower.suspendSerial;g_gameOverlay.sharedPowerChanged(sharedPower);need(!g_gameOverlay.active()&&!g_gameOverlaySurface,"final Ready still retires pre-sleep session");});
 test("first-unknown-monitor-projection-is-delivered-once",[]{sharedPower.flags=0;const auto monitor=g_desktopMonitorHud.deliveries,notice=g_monitorNoticeCarrier.deliveries;monitorSharedPowerChanged();need(g_desktopMonitorHud.deliveries==monitor+1&&g_monitorNoticeCarrier.deliveries==notice+1,"initial UNKNOWN still reaches existing owners");monitorSharedPowerChanged();need(g_desktopMonitorHud.deliveries==monitor+1&&g_monitorNoticeCarrier.deliveries==notice+1,"unchanged UNKNOWN is not replayed");});
 test("monitor-notice-current-power-metadata-is-not-replayed",[]{sharedPower.flags|=ymcc::sleeppower::kHardwareAllowed;monitorSharedPowerChanged();const auto monitor=g_desktopMonitorHud.deliveries,notice=g_monitorNoticeCarrier.deliveries,notify=g_nativeMonitorCv.notifications;for(unsigned i=0;i<1000;++i){++sharedPower.publicationRevision;monitorSharedPowerChanged();}need(g_desktopMonitorHud.deliveries==monitor&&g_monitorNoticeCarrier.deliveries==notice&&g_nativeMonitorCv.notifications==notify,"metadata must not refresh renderers or wake collection again");sharedPower.flags|=ymcc::sleeppower::kNotificationPending;monitorSharedPowerChanged();need(g_desktopMonitorHud.deliveries==monitor+1&&g_monitorNoticeCarrier.deliveries==notice+1,"actual revocation reaches both owners");});
 test("actual-common-projector-retires-pending-ui-and-does-not-reopen",[]{publishPowerToActualProjector();need(gameOverlaySurfaceShow({},binding(),false),"show");sharedPower.flags|=ymcc::sleeppower::kNotificationPending;publishPowerToActualProjector();need(!g_gameOverlay.active()&&!g_gameOverlaySurface,"actual shared projection must retire UI");sharedPower.flags=ymcc::sleeppower::kKnown;publishPowerToActualProjector();need(!g_gameOverlay.active()&&!g_gameOverlaySurface,"actual Ready projection cannot summon UI");});
 test("failed-return-at-sleep-boundary-recovers-only-on-explicit-post-wake-summon",[]{publishPowerToActualProjector();need(gameOverlaySurfaceShow({},binding(),false),"show");const auto old=g_gameOverlaySurface;returnFailures=1;sharedPower.flags|=ymcc::sleeppower::kNotificationPending;publishPowerToActualProjector();need(!g_gameOverlay.active()&&g_gameOverlaySurface==old&&!IsWindowVisible(old),"failed sleep-boundary return stays hidden and owned");sharedPower.flags=ymcc::sleeppower::kKnown;++sharedPower.suspendSerial;publishPowerToActualProjector();need(!g_gameOverlay.active()&&g_gameOverlaySurface==old,"Ready does not retry or replay hidden owner");need(gameOverlaySurfaceShow({},binding(),false),"explicit post-wake summon recovers");});
 test("actual-common-projector-contains-reentrant-close-publication",[]{publishPowerToActualProjector();need(gameOverlaySurfaceShow({},binding(),false,[]{++g_sharedPowerPublicationRevision;g_sharedPowerProjectionPending=true;projectSharedPowerLifecycle(g_hwnd);}),"show");const auto before=redeliveries;sharedPower.flags|=ymcc::sleeppower::kNotificationPending;publishPowerToActualProjector();need(!g_gameOverlay.active()&&redeliveries>before&&g_sharedPowerProjectionPending,"reentrant publication is deferred by existing owner");projectSharedPowerLifecycle(g_hwnd);need(!g_sharedPowerProjectionPending&&!g_gameOverlay.active(),"deferred current state completes without reopening");});
 test("return-failure-retries-on-next-explicit-summon",[]{
  need(gameOverlaySurfaceShow({},binding(),false),"show");const auto old=g_gameOverlaySurface;returnFailures=1;g_gameOverlay.end("close-fault");
  need(g_gameOverlaySurface==old&&IsWindow(old)&&!IsWindowVisible(old)&&!g_gameOverlay.active(),"failed return retains hidden owner");const auto calls=retargetCalls;
  need(gameOverlaySurfaceShow({},binding(),false),"next explicit summon must recover failed return");need(retargetCalls==calls+2,"one bounded return retry then one new attachment");
 });
 test("persistent-return-failure-stays-bounded-and-later-recovers",[]{
  need(gameOverlaySurfaceShow({},binding(),false),"show");returnFailures=10;g_gameOverlay.end("close-fault");
  const auto calls=retargetCalls;need(!gameOverlaySurfaceShow({},binding(),false),"persistent failure refuses partial UI");need(retargetCalls==calls+1,"one repair attempt per explicit summon");
  returnFailures=0;need(gameOverlaySurfaceShow({},binding(),false),"later retry recovers");
 });
 test("panel-attach-failure-cleans-and-next-summon-recovers",[]{attachFailures=1;need(!gameOverlaySurfaceShow({},binding(),false),"failed panel refuses");need(!g_gameOverlaySurface&&!g_gameOverlay.active()&&GetParent(child)==g_hwnd,"partial attachment cleaned");need(gameOverlaySurfaceShow({},binding(),false),"next show");});
 test("submit-failure-cleans-and-next-summon-recovers",[]{submitFailures=1;need(!gameOverlaySurfaceShow({},binding(),false),"failed submission refuses");need(!g_gameOverlaySurface&&!g_gameOverlay.active()&&GetParent(child)==g_hwnd,"failed submission cleaned");need(gameOverlaySurfaceShow({},binding(),false),"next show");});
 test("child-restore-failure-retains-retryable-lease",[]{need(gameOverlaySurfaceShow({},binding(),false),"show");removeFailures=1;const auto old=g_gameOverlaySurface;g_gameOverlay.end("child-restore-fault");need(g_gameOverlaySurface==old&&IsWindow(old)&&!IsWindowVisible(old),"failed child restore keeps hidden retryable owner");need(gameOverlaySurfaceRestore(),"next explicit restore recovers");need(IsWindowEnabled(child)&&!(GetWindowLongPtrW(child,GWL_EXSTYLE)&WS_EX_TRANSPARENT),"child returns original interaction policy after retry");});
 test("failed-window-destruction-retains-owner-until-retry",[]{need(gameOverlaySurfaceShow({},binding(),false),"show");const auto old=g_gameOverlaySurface;destroyFailures=1;g_gameOverlay.end("destroy-fault");need(g_gameOverlaySurface==old&&IsWindow(old)&&!IsWindowVisible(old),"failed destruction cannot forget owned HWND");need(gameOverlaySurfaceRestore()&&!IsWindow(old)&&!g_gameOverlaySurface,"next explicit close destroys retained owner");});
 test("raw-reentrant-resume-cannot-close-the-current-window-overlay",[]{
  need(gameOverlaySurfaceShow({},binding(),false),"show");const auto surface=g_gameOverlaySurface;
  ymcc::sleeppower::Dispatch dispatch;
  const auto active=dispatch.window(PBT_APMRESUMEAUTOMATIC,100);
  need(bool(active),"original native dispatch owns a receipt");
  need(windowOverlayPrelude(g_hwnd,WM_POWERBROADCAST,PBT_APMRESUMESUSPEND,0)==-1,"power reaches the common consumer");
  need(!dispatch.window(PBT_APMRESUMESUSPEND,101),"actual dispatcher defers reentrant raw receipt");
  need(g_gameOverlay.active()&&g_gameOverlaySurface==surface&&GetParent(child)==surface,"raw wake cannot retire the current presentation before common admission");
  dispatch.complete(active,102);
  sharedPower.flags|=ymcc::sleeppower::kNotificationPending;publishPowerToActualProjector();
  need(!g_gameOverlay.active()&&!g_gameOverlaySurface&&GetParent(child)==g_hwnd,"effective common revocation returns the surface and its child");
  sharedPower.flags=ymcc::sleeppower::kKnown;++sharedPower.suspendSerial;publishPowerToActualProjector();
  need(!g_gameOverlay.active()&&gameOverlaySurfaceShow({},binding(),false),"next admitted summon recovers without an automatic reopen");
  windowOverlayPrelude(g_hwnd,WM_CLOSE,0,0);
  need(!g_gameOverlay.active()&&!g_gameOverlaySurface,"ordinary window close still returns presentation");
 });
 runReadinessCases(test);
 test("controller-close-releases-retained-hidden-owner",[]{need(gameOverlaySurfaceShow({},binding(),false),"show");returnFailures=2;g_gameOverlay.end("close-fault");const auto old=g_gameOverlaySurface;g_mainWebViewComposition.enabled=false;g_ctrl=nullptr;g_view=nullptr;gameOverlaySurfaceControllerClosed();need(!g_gameOverlaySurface&&!g_gameOverlaySurfaceAttached&&!IsWindow(old),"closed controller permits final hidden owner destruction");child=nullptr;});
 cleanup();if(g_gameOverlaySurface&&IsWindow(g_gameOverlaySurface))DestroyWindow(g_gameOverlaySurface);g_gameOverlaySurface=nullptr;if(g_hwnd)DestroyWindow(g_hwnd);g_hwnd=nullptr;
 result["cases"]=cases;result["hooksAfterClose"]=g_gameOverlay.ownedHookCount();const bool restored=SetThreadDesktop(original)!=FALSE,closed=CloseDesktop(desktop)!=FALSE;result["privateDesktopClosed"]=closed;result["pass"]=ok&&restored&&closed&&!g_gameOverlay.ownedHookCount();
 std::ofstream out{std::filesystem::path(argv[1])};out<<result.dump(2);return result["pass"].get<bool>()&&out.good()?0:1;
}
