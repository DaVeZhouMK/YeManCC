// Retired light isolation: actual source consumers with inert OS/UI ports.
#include "sleep_power_state.h"
#include "sleep_power_observation.h"
#ifndef FOCUS_CONTROLLER_DIAGNOSTIC
#define FOCUS_CONTROLLER_DIAGNOSTIC 0
#endif
#if FOCUS_CONTROLLER_DIAGNOSTIC
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
#include "virtual_output_status.h"
using json=nlohmann::json;
namespace ymcc::touchhaptic {struct SourceToken {};}
#include "actual_controller_diagnostic_types.inc"
struct DiagnosticBinding {
 unsigned long long session=7,originalUiToken=9,power=3,birth=55;
 HWND carrier=nullptr,target=nullptr;DWORD pid=777;
 bool originalUiInput=true,visualProbe=false,globalProbe=false;
};
struct DiagnosticOverlay {
 std::shared_ptr<const DiagnosticBinding> binding;bool ready=true;
 auto snapshot()const{return binding;}bool inputReady()const{return ready;}
};
struct DiagnosticGame {struct {DWORD pid=777;} game;unsigned long long generation=8;};
struct DiagnosticWindow {
 RECT bounds{0,0,800,600};DWORD pid=777,cloak=0;
 LONG_PTR style=WS_POPUP,extended=WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_LAYERED;
 BOOL visible=TRUE,iconic=FALSE,enabled=TRUE;bool rectRead=true,layerRead=true;
 HRESULT cloakStatus=S_OK,frameStatus=S_OK;BYTE alpha=180;
};
namespace diagnostic {
 static unsigned checks=0,reads=0,scriptCalls=0;static DWORD error=12345;
 static ULONGLONG now=1000,power=3;static bool sinkThrows=false,monitorRead=true,keyboardThrows=false,domainThrows=false;
 static std::map<std::uintptr_t,DiagnosticWindow> windows;
 static std::vector<HWND> order;static std::vector<json> rows,keyboardRows;
 static std::vector<std::string> events,caseNames;static HWND foreground=nullptr;
 using Completion=std::function<HRESULT(HRESULT,LPCWSTR)>;
 static Completion completion;static HRESULT dispatchStatus=S_OK;
 static void check(bool condition,const char* name){++checks;if(!condition)throw std::runtime_error(name);}
 static HWND window(std::uintptr_t value){return reinterpret_cast<HWND>(value);}
 static DWORD getError(){return error;}static void setError(DWORD value){error=value;}
 static ULONGLONG tick(){error=731;return now;}
 static DiagnosticWindow& at(HWND handle){return windows.at(reinterpret_cast<std::uintptr_t>(handle));}
 static BOOL isWindow(HWND handle){error=732;return windows.contains(reinterpret_cast<std::uintptr_t>(handle));}
 static BOOL visible(HWND handle){return at(handle).visible;}static BOOL iconic(HWND handle){return at(handle).iconic;}
 static BOOL enabled(HWND handle){return at(handle).enabled;}
 static DWORD process(HWND handle,DWORD* pid){*pid=at(handle).pid;return 12;}
 static BOOL bounds(HWND handle,RECT* result){*result=at(handle).bounds;return at(handle).rectRead;}
 static HRESULT attribute(HWND handle,DWORD attribute,void* result,DWORD){
  auto& value=at(handle);if(attribute==DWMWA_CLOAKED){*static_cast<DWORD*>(result)=value.cloak;return value.cloakStatus;}
  *static_cast<RECT*>(result)=value.bounds;return value.frameStatus;
 }
 static HMONITOR monitor(HWND,DWORD){return reinterpret_cast<HMONITOR>(1);}
 static BOOL monitorInfo(HMONITOR,MONITORINFO* info){info->rcMonitor={0,0,1920,1080};return monitorRead;}
 static BOOL intersection(RECT* result,const RECT* first,const RECT* second){
  result->left=(std::max)(first->left,second->left);result->top=(std::max)(first->top,second->top);
  result->right=(std::min)(first->right,second->right);result->bottom=(std::min)(first->bottom,second->bottom);
  return result->right>result->left&&result->bottom>result->top;
 }
 static LONG_PTR style(HWND handle,int index){return index==GWL_STYLE?at(handle).style:at(handle).extended;}
 static HWND parent(HWND){return nullptr;}static HWND owner(HWND,UINT){return window(44);}
 static BOOL layered(HWND handle,COLORREF* key,BYTE* alpha,DWORD* flags){*key=0;*alpha=at(handle).alpha;*flags=LWA_ALPHA;return at(handle).layerRead;}
 static HWND foregroundWindow(){error=733;return foreground;}static HWND ancestor(HWND handle,UINT){return handle;}
 static BOOL enumerate(WNDENUMPROC callback,LPARAM raw){for(const auto handle:order)if(!callback(handle,raw))break;return TRUE;}
 static int className(HWND,wchar_t* output,int capacity){const std::wstring value=L"DiagnosticWindow";const auto count=(std::min)(value.size(),static_cast<std::size_t>(capacity-1));std::copy_n(value.c_str(),count,output);output[count]=0;return static_cast<int>(count);}
}
static HWND g_hwnd=diagnostic::window(42);
static DiagnosticOverlay g_gameOverlay;
static std::atomic<unsigned> g_xinputTopologyEpoch{5},g_xinputSlotIdentityEpoch{6};
static std::atomic<unsigned long long> g_webviewGeneration{4},g_focusShortcutEvidenceFailures{0};
static std::atomic<bool> g_controllerFrontendIsolationEnabled{true},g_focusIsolationGameOpen{true},g_keyboardModifierEventGate{false};
static std::atomic<std::shared_ptr<const DiagnosticGame>> g_gameValveObservation;
static std::mutex g_inputCaptureGamepadSnapshotMx;
static InputCaptureGamepadSnapshot g_inputCaptureGamepadSnapshot;
static bool g_webviewReady=true;
static unsigned long long currentPowerGeneration(){return diagnostic::power;}
static std::string W2U(const wchar_t* value){std::string result;for(;*value;++value)result.push_back(static_cast<char>(*value));return result;}
struct DiagnosticController {
 BOOL visible=TRUE;RECT bounds{0,0,800,600};HRESULT visibleStatus=S_OK,boundsStatus=S_OK;
 HRESULT get_IsVisible(BOOL* value){diagnostic::error=734;*value=visible;return visibleStatus;}
 HRESULT get_Bounds(RECT* value){*value=bounds;return boundsStatus;}
};
struct ICoreWebView2ExecuteScriptCompletedHandler {};
template<class Interface> struct DiagnosticCallback {std::shared_ptr<diagnostic::Completion> value;auto Get(){return value.get();}};
template<class Interface,class Function> static DiagnosticCallback<Interface> Callback(Function value){return {std::make_shared<diagnostic::Completion>(value)};}
struct DiagnosticView {
 HRESULT ExecuteScript(LPCWSTR script,diagnostic::Completion* callback){
  ++diagnostic::scriptCalls;diagnostic::error=735;
  diagnostic::check(std::wstring(script).find(L"getBoundingClientRect")!=std::wstring::npos,"actual-renderer-script");
  if(SUCCEEDED(diagnostic::dispatchStatus))diagnostic::completion=*callback;
  return diagnostic::dispatchStatus;
 }
};
static DiagnosticController controller;static DiagnosticView diagnosticViewObject;
static DiagnosticController* g_ctrl=&controller;static DiagnosticView* g_view=&diagnosticViewObject;
static void appendNativeLifecycleLog(const char* event,const json& row){diagnostic::error=736;if(diagnostic::sinkThrows)throw std::runtime_error("diagnostic sink");diagnostic::events.emplace_back(event);diagnostic::rows.push_back(row);}
namespace ymcc::keyboardEvidence {
struct Snapshot {};
static Snapshot read(){++diagnostic::reads;if(diagnostic::keyboardThrows)throw std::runtime_error("keyboard snapshot");return {};}
static json toJson(const Snapshot&){return json::object();}
}
namespace ymcc::idle {
struct PhysicalSnapshot {
 bool haveLastSample=false,latestPhysicalLive=false,contextKnown=false,virtualEnabled=false;
 unsigned contextTransitions=0,connectedMask=0,buttons=0;ULONGLONG observationTick=0,sampleTick=0,powerGeneration=0;
};
}
static struct DiagnosticIdle {bool peekPhysical(ymcc::idle::PhysicalSnapshot&){return false;}} g_inputIdleActivity;
static void appendDomainLog(const char*,const char*,const json& row){if(diagnostic::domainThrows)throw std::runtime_error("keyboard sink");diagnostic::keyboardRows.push_back(row);}
#define GetLastError diagnostic::getError
#define SetLastError diagnostic::setError
#define GetTickCount64 diagnostic::tick
#define IsWindow diagnostic::isWindow
#define IsWindowVisible diagnostic::visible
#define IsIconic diagnostic::iconic
#define IsWindowEnabled diagnostic::enabled
#define GetWindowThreadProcessId diagnostic::process
#define GetWindowRect diagnostic::bounds
#define DwmGetWindowAttribute diagnostic::attribute
#define MonitorFromWindow diagnostic::monitor
#define GetMonitorInfoW diagnostic::monitorInfo
#define IntersectRect diagnostic::intersection
#define GetWindowLongPtrW diagnostic::style
#define GetParent diagnostic::parent
#define GetWindow diagnostic::owner
#define GetLayeredWindowAttributes diagnostic::layered
#define GetForegroundWindow diagnostic::foregroundWindow
#define GetAncestor diagnostic::ancestor
#define EnumWindows diagnostic::enumerate
#define GetClassNameW diagnostic::className
#include FOCUS_ACTUAL_INCLUDE
static void diagnosticReset(){
 diagnostic::error=12345;diagnostic::now=1000;diagnostic::power=3;diagnostic::reads=diagnostic::scriptCalls=0;
 diagnostic::sinkThrows=diagnostic::keyboardThrows=diagnostic::domainThrows=false;diagnostic::monitorRead=true;diagnostic::dispatchStatus=S_OK;diagnostic::completion={};
 diagnostic::rows.clear();diagnostic::events.clear();diagnostic::keyboardRows.clear();diagnostic::windows.clear();
 for(const auto value:{42u,43u,44u,45u})diagnostic::windows.emplace(value,DiagnosticWindow{});
 diagnostic::foreground=diagnostic::window(44);diagnostic::order={diagnostic::window(45),diagnostic::window(44),diagnostic::window(43),g_hwnd};
 auto binding=std::make_shared<DiagnosticBinding>();binding->carrier=diagnostic::window(43);binding->target=diagnostic::window(44);g_gameOverlay.binding=binding;g_gameOverlay.ready=true;
 g_inputCaptureGamepadSnapshot={};g_inputCaptureGamepadSnapshot.live=true;g_inputCaptureGamepadSnapshot.inputSource="xinput";
 g_inputCaptureGamepadSnapshot.readAttemptSequence=17;g_inputCaptureGamepadSnapshot.readStartedTick=975;g_inputCaptureGamepadSnapshot.readCompletedTick=980;
 g_inputCaptureGamepadSnapshot.readPowerGeneration=3;g_inputCaptureGamepadSnapshot.readTopologyGeneration=5;g_inputCaptureGamepadSnapshot.readIdentityGeneration=6;
 g_inputCaptureGamepadSnapshot.readContextStable=true;g_inputCaptureGamepadSnapshot.primarySlot=1;g_inputCaptureGamepadSnapshot.connectedMask=2;
 g_inputCaptureGamepadSnapshot.pad={0x1300,255,120,-18000,17000,-25000,26000};g_inputCaptureGamepadSnapshot.specialXbox=true;
 g_webviewGeneration=4;g_xinputTopologyEpoch=5;g_xinputSlotIdentityEpoch=6;g_focusShortcutEvidenceFailures=0;
 g_controllerFrontendIsolationEnabled=true;g_focusIsolationGameOpen=true;g_keyboardModifierEventGate=false;g_webviewReady=true;
 g_gameValveObservation.store(std::make_shared<DiagnosticGame>());controller={};g_ctrl=&controller;g_view=&diagnosticViewObject;
}
static json diagnosticState(){
 const auto& sample=g_inputCaptureGamepadSnapshot;json windows=json::array();
 for(const auto& [handle,value]:diagnostic::windows)windows.push_back({handle,value.visible,value.iconic,value.enabled,value.cloak,value.style,value.extended,
  value.bounds.left,value.bounds.top,value.bounds.right,value.bounds.bottom,value.alpha});
 return {windows,reinterpret_cast<std::uintptr_t>(diagnostic::foreground),reinterpret_cast<std::uintptr_t>(g_gameOverlay.binding.get()),g_gameOverlay.ready,
  sample.readAttemptSequence,sample.live,sample.pad.wButtons,sample.pad.bLeftTrigger,sample.pad.bRightTrigger,
  sample.pad.sThumbLX,sample.pad.sThumbLY,sample.pad.sThumbRX,sample.pad.sThumbRY,sample.specialXbox,
  diagnostic::power,g_webviewGeneration.load(),g_controllerFrontendIsolationEnabled.load(),g_focusIsolationGameOpen.load(),
  controller.visible,controller.bounds.left,controller.bounds.top,controller.bounds.right,controller.bounds.bottom};
}
template<class Function> static void diagnosticCall(Function function){
 const auto state=diagnosticState();const auto error=diagnostic::error;function();
 diagnostic::check(diagnostic::error==error,"diagnostic-preserves-last-error");
 diagnostic::check(diagnosticState()==state,"diagnostic-changed-input-focus-or-state");
}
static void diagnosticPhysicalCases(){
 diagnosticReset();json result;diagnosticCall([&]{result=controllerIsolationPhysicalEvidence();});
 diagnostic::check(result["captured"]==true&&result["buttons"]==0x1300&&result["guide"]==true,"physical-held-buttons");
 diagnostic::check(result["lt"]==255&&result["rt"]==120&&result["lx"]==-18000&&result["ly"]==17000&&result["rx"]==-25000&&result["ry"]==26000,"physical-held-axes-triggers");
 diagnostic::check(result["sampleSequence"]==17&&result["readAgeMs"]==20,"physical-cache-provenance");
 diagnostic::check(result["gameReleaseReceiptProven"]==false,"physical-does-not-prove-release");
 g_inputCaptureGamepadSnapshot={};diagnosticCall([&]{result=controllerIsolationPhysicalEvidence();});
 diagnostic::check(result["captured"]==true&&result["live"]==false&&result["sampleSequence"]==0&&result["readAgeMs"].is_null(),"physical-no-sample-unknown-age");
 std::promise<void> locked,release;auto released=release.get_future();std::atomic<bool> timedOut=false;
 std::thread holder([&]{std::lock_guard lock(g_inputCaptureGamepadSnapshotMx);locked.set_value();if(released.wait_for(std::chrono::seconds(1))!=std::future_status::ready)timedOut=true;});
 locked.get_future().wait();const auto error=diagnostic::error;result=controllerIsolationPhysicalEvidence();release.set_value();holder.join();
 diagnostic::check(!timedOut&&result==json{{"captured",false},{"reason","snapshot-contended"}}&&diagnostic::error==error,"physical-trylock-contended");
 diagnosticReset();g_inputCaptureGamepadSnapshot.readCompletedTick=1100;diagnosticCall([&]{result=controllerIsolationPhysicalEvidence();});
 diagnostic::check(result["readAgeMs"].is_null(),"physical-future-sample-age-unknown");
}
static void diagnosticWindowCases(){
 diagnosticReset();json result;const auto display=diagnostic::window(43);
 diagnosticCall([&]{result=controllerIsolationWindowEvidence(nullptr);});diagnostic::check(result["valid"]==false,"window-null");
 diagnosticCall([&]{result=controllerIsolationWindowEvidence(diagnostic::window(999));});diagnostic::check(result["valid"]==false,"window-invalid");
 auto& state=diagnostic::at(display);state.visible=FALSE;state.iconic=TRUE;state.cloak=1;state.bounds={-2000,-1000,-1200,-400};state.alpha=0;
 diagnosticCall([&]{result=controllerIsolationWindowEvidence(display);});
 diagnostic::check(result["visibleFlag"]==false&&result["iconic"]==true&&result["cloaked"]==1&&result["intersectsMonitor"]==false,"window-hidden-offscreen-cloaked");
 diagnostic::check(result["layered"]["alpha"]==0&&result["screenVisibleVerified"]==false,"window-flags-not-visual-proof");
 state.bounds={10,10,10,40};state.cloakStatus=E_FAIL;state.frameStatus=E_FAIL;
 diagnosticCall([&]{result=controllerIsolationWindowEvidence(display);});
 diagnostic::check(result["positiveArea"]==false&&result["cloaked"].is_null()&&result["frameBounds"].is_null(),"window-failed-queries-unknown");
}
static void diagnosticPresentationCases(){
 diagnosticReset();json result;diagnosticCall([&]{result=controllerIsolationPresentationEvidence();});
 diagnostic::check(result["foregroundIsTarget"]==true&&result["foregroundIsMain"]==false&&result["overlayActive"]==true,"presentation-game-keeps-foreground");
 diagnostic::check(result["overlay"]["originalUiInput"]==true&&result["overlay"]["classInputReady"]==true&&result["overlay"]["session"]==7,"presentation-original-overlay-flags");
 const auto flags=result["display"]["exStyle"].get<unsigned long long>();
 diagnostic::check((flags&WS_EX_NOACTIVATE)&&(flags&WS_EX_TOOLWINDOW)&&result["display"]["layered"]["alpha"]==180,"presentation-native-window-flags");
 diagnostic::check(result["zOrder"]["displayFound"]==true&&result["zOrder"]["above"].size()==2&&!result["zOrder"]["pixelOcclusionProven"].get<bool>()&&!result["crossSourceAtomic"].get<bool>(),"presentation-zorder-not-pixel-proof");
 g_gameOverlay.binding.reset();diagnosticCall([&]{result=controllerIsolationPresentationEvidence();});
 diagnostic::check(result["overlayActive"]==false&&result["display"]["hwnd"]==reinterpret_cast<std::uintptr_t>(g_hwnd),"presentation-no-overlay-uses-main");
}
static void diagnosticBoundaryCases(){
 diagnosticReset();diagnosticCall([]{focusShortcutInputEvidence("before-decision",81,true);});
 diagnostic::check(diagnostic::rows.size()==1&&diagnostic::events==std::vector<std::string>{"controller-isolation-boundary"}&&diagnostic::reads==0&&diagnostic::keyboardRows.empty(),"independent-keyboard-gate");
 diagnostic::check(diagnostic::rows[0]["summonGeneration"]==81&&diagnostic::rows[0]["phase"]=="before-decision"&&diagnostic::rows[0]["gamePid"]==777&&!diagnostic::rows[0]["keyReleaseProven"].get<bool>(),"boundary-session-game-correlation");
 g_keyboardModifierEventGate=true;diagnosticCall([]{focusShortcutInputEvidence("show-returned",81,true);});
 diagnostic::check(diagnostic::rows.size()==2&&diagnostic::reads==1&&diagnostic::keyboardRows.size()==2,"existing-keyboard-gate-preserved");
 controller.visibleStatus=E_FAIL;controller.boundsStatus=E_FAIL;
 diagnosticCall([]{controllerIsolationSummonEvidence("show-returned",81,true);});
 diagnostic::check(diagnostic::rows.back()["webview"]["visible"].is_null()&&diagnostic::rows.back()["webview"]["bounds"].is_null(),"webview-getter-failure-unknown");
 diagnostic::sinkThrows=true;diagnosticCall([]{controllerIsolationSummonEvidence("sink-failure",81,false);});
}
static void diagnosticRendererCases(){
 const auto deliver=[](HRESULT status,const wchar_t* value){auto callback=diagnostic::completion;diagnostic::check(static_cast<bool>(callback),"renderer-callback-captured");diagnostic::check(callback(status,value)==S_OK,"renderer-callback-return");};
 diagnosticReset();diagnosticCall([]{controllerIsolationOverlayRendererProbe();});
 diagnostic::check(diagnostic::scriptCalls==1&&diagnostic::rows.size()==1&&diagnostic::rows[0]["phase"]=="renderer-requested","renderer-single-request");
 deliver(S_OK,LR"("{\"readyState\":\"complete\",\"visibility\":\"visible\",\"hasFocus\":false,\"app\":true}")");
 diagnostic::check(diagnostic::rows.back()["phase"]=="renderer-replied"&&diagnostic::rows.back()["parse"]=="parsed"&&diagnostic::rows.back()["renderer"]["app"]==true&&diagnostic::rows.back()["currentSession"]==true&&!diagnostic::rows.back()["screenVisibleVerified"].get<bool>(),"renderer-normal-receipt-not-visual-proof");
 diagnosticReset();diagnostic::dispatchStatus=E_FAIL;diagnosticCall([]{controllerIsolationOverlayRendererProbe();});
 diagnostic::check(diagnostic::rows.size()==1&&diagnostic::rows[0]["hresult"]==static_cast<long long>(E_FAIL)&&!diagnostic::completion,"renderer-dispatch-failed");
 diagnosticReset();controllerIsolationOverlayRendererProbe();deliver(E_FAIL,nullptr);
 diagnostic::check(diagnostic::rows.back()["parse"]=="unavailable"&&diagnostic::rows.back()["renderer"].is_null(),"renderer-completion-failed");
 diagnosticReset();controllerIsolationOverlayRendererProbe();diagnostic::now+=60000;
 diagnostic::check(diagnostic::rows.size()==1&&diagnostic::rows[0]["phase"]=="renderer-requested"&&diagnostic::scriptCalls==1,"renderer-no-reply-remains-unconfirmed-without-retry");
 for(unsigned change=0;change<3;++change){
  diagnosticReset();controllerIsolationOverlayRendererProbe();
  if(change==0){auto replacement=std::make_shared<DiagnosticBinding>(*g_gameOverlay.binding);replacement->session=8;g_gameOverlay.binding=replacement;}
  if(change==1)++diagnostic::power;
  if(change==2)++g_webviewGeneration;
  const auto state=diagnosticState();deliver(S_OK,LR"("{\"app\":true}")");
  diagnostic::check(diagnostic::rows.back()["currentSession"]==false&&diagnosticState()==state,"renderer-old-session-or-generation");
 }
 diagnosticReset();controllerIsolationOverlayRendererProbe();deliver(S_OK,L"not-json");
 diagnostic::check(diagnostic::rows.back()["phase"]=="renderer-reply-invalid","renderer-malformed-reply");
 diagnosticReset();controllerIsolationOverlayRendererProbe();const std::wstring oversized(8193,L'x');deliver(S_OK,oversized.c_str());
 diagnostic::check(diagnostic::rows.back()["parse"]=="oversize"&&diagnostic::rows.back()["renderer"].is_null(),"renderer-oversize-bounded");
 diagnosticReset();g_view=nullptr;diagnosticCall([]{controllerIsolationOverlayRendererProbe();});diagnostic::check(diagnostic::rows.empty(),"renderer-no-view-no-request");
 diagnosticReset();g_gameOverlay.binding.reset();diagnosticCall([]{controllerIsolationOverlayRendererProbe();});diagnostic::check(diagnostic::rows.empty(),"renderer-no-overlay-no-request");
}
static void diagnosticRendererErrorCases(){
 for(unsigned failure=0;failure<4;++failure){
  diagnosticReset();controllerIsolationOverlayRendererProbe();diagnostic::error=12345;
  diagnostic::sinkThrows=failure==3;const auto state=diagnosticState();
  diagnostic::check(diagnostic::completion(failure==1?E_FAIL:S_OK,failure==2?L"invalid":LR"("{\"app\":true}")")==S_OK,"renderer-callback-error-path-return");
  diagnostic::check(diagnostic::error==12345,"renderer-callback-preserves-last-error");
  diagnostic::check(diagnosticState()==state,"renderer-callback-changed-input-focus-or-state");
 }
}
static void diagnosticWrapperCompatibilityCases(){
 for(bool keyboardGate:{false,true})for(unsigned failure=0;failure<3;++failure){
  diagnosticReset();g_keyboardModifierEventGate=keyboardGate;diagnostic::keyboardThrows=failure==1;diagnostic::domainThrows=failure==2;
  diagnosticCall([]{focusShortcutInputEvidence("before-decision",81,true);});
  diagnosticCall([]{focusShortcutInputEvidence("show-returned",81,true);});
  diagnostic::check(diagnostic::rows.empty(),"compatibility-helper-port-has-no-side-effects");
  diagnostic::check(diagnostic::reads==(keyboardGate?2u:0u),"compatibility-original-keyboard-gate");
  diagnostic::check(diagnostic::keyboardRows.size()==(keyboardGate&&failure==0?4u:0u),"compatibility-original-mirror-count");
  diagnostic::check(g_focusShortcutEvidenceFailures.load()==(keyboardGate&&failure?2u:0u),"compatibility-original-failure-accounting");
  if(keyboardGate&&failure==0)diagnostic::check(diagnostic::keyboardRows[0]["phase"]=="before-decision"&&diagnostic::keyboardRows[2]["phase"]=="show-returned"&&diagnostic::keyboardRows[0]["summonGeneration"]==81,"compatibility-original-phase-generation");
 }
}
int main(int argc,char** argv){
 std::string selected;
 try{
  diagnostic::check(argc==1||(argc==3&&std::string(argv[1])=="--case"),"diagnostic-case-arguments");if(argc==3)selected=argv[2];
  const std::vector<std::pair<const char*,void(*)()>> diagnosticCases={{"physical",diagnosticPhysicalCases},{"window",diagnosticWindowCases},
   {"presentation",diagnosticPresentationCases},{"boundary",diagnosticBoundaryCases},{"renderer",diagnosticRendererCases},{"renderer-error",diagnosticRendererErrorCases}};
  if(selected=="wrapper-compat"){diagnosticWrapperCompatibilityCases();diagnostic::caseNames.push_back(selected);}
  for(const auto& entry:diagnosticCases)if(selected.empty()||selected==entry.first){entry.second();diagnostic::caseNames.emplace_back(entry.first);}
  diagnostic::check(!diagnostic::caseNames.empty(),"unknown-diagnostic-case");
  std::cout<<json{{"pass",true},{"checks",diagnostic::checks},{"mode","diagnostic"},{"selectedCase",selected},{"cases",diagnostic::caseNames},
   {"realInputOrUiExecuted",false},{"rendererReceiptPort","inert"},{"screenVisibleVerified",false},{"gameReleaseReceiptProven",false}}.dump()<<'\n';return 0;
 }catch(const std::exception& error){std::cout<<json{{"pass",false},{"checks",diagnostic::checks},{"mode","diagnostic"},{"selectedCase",selected},{"cases",diagnostic::caseNames},{"error",error.what()}}.dump()<<'\n';std::cerr<<error.what()<<'\n';return 1;}
}
#elif FOCUS_LIGHT_RETIRED
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "input_idle_diagnostics.h"
#include "json.hpp"
using json=nlohmann::json;
namespace fake {
unsigned checks=0,reads=0,registrationCalls=0;DWORD error=12345,foregroundPid=444;
bool valid=true,hide=false,throwRead=false,throwSink=false,integrated=false,ownForeground=false,ui=true,radial=false,child=false;
ULONGLONG now=1000;unsigned topmostReleases=0,handoffCancels=0,losslessCancels=0;
std::vector<std::string> controls,domains;std::vector<json> rows,lifecycleRows;std::vector<std::pair<std::string,json>> events;
std::function<void()> onBring;
void check(bool condition,const char* text){++checks;if(!condition)throw std::runtime_error(text);}
HWND window(std::uintptr_t n){return reinterpret_cast<HWND>(n);}
DWORD getError(){return error;}void setError(DWORD value){error=value;}
ULONGLONG tick(){error=777;return now;}
BOOL isWindow(HWND){controls.push_back("IsWindow");error=222;return valid;}
BOOL visible(HWND){return TRUE;}BOOL iconic(HWND){return FALSE;}BOOL zoomed(HWND){return FALSE;}
HWND foreground(){return window(ownForeground?42:44);}HWND ancestor(HWND h,UINT){return h;}
LONG_PTR style(HWND,int){return 0;}
}
namespace ymcc::keyboardEvidence {
struct Snapshot {unsigned mask=0;DWORD pid=0;};
Snapshot read(){++fake::reads;fake::error=888;if(fake::throwRead)throw std::runtime_error("fake snapshot failure");return {17,fake::foregroundPid};}
json toJson(const Snapshot& s){return {{"asyncDownMask",s.mask},{"foregroundPidBefore",s.pid},{"foregroundPidAfter",s.pid},{"captureBeginTick",1000},{"captureEndTick",1000},{"sourceAttribution","unobserved"}};}
}
namespace ymcc {
struct ShortcutWindowVisibility {bool restoreMaximized=false;};
static bool summonPressHides(bool,bool,bool,bool,bool){fake::controls.push_back("summon-policy");return fake::hide;}
static bool summonPressHides(const ShortcutWindowVisibility&,bool,bool){return summonPressHides(true,true,false,false,false);}
}
static ymcc::ShortcutWindowVisibility readShortcutWindowVisibility(HWND){return {};}
#include "actual_focus_types.inc"
static HWND g_hwnd=fake::window(42);
static FocusSessionState g_focusSession;
static FocusTargetSnapshot g_pendingSummonTarget;
static unsigned long long g_focusSessionSequence=0;
static std::atomic<unsigned long long> g_summonGeneration{0},g_summonRegistrationGeneration{0};
static std::atomic<bool> g_focusIsolationPrepared{false},g_focusIsolationGameOpen{false},g_keyboardModifierEventGate{false},g_fullHeight{false};
static std::pair<std::uint64_t,std::uint64_t> g_focusIsolationPowerPeriod;
static unsigned long long currentPowerGeneration(){return 1;}
static std::atomic<std::uint64_t> g_sharedPowerSuspendSerial{0};
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1*,uint32_t){return 0;}
static std::atomic<bool> g_controllerFrontendIsolationEnabled{true};
static std::atomic<std::uint64_t> g_focusShortcutEvidenceFailures{0};
static std::atomic<std::shared_ptr<const FocusLightIsolationTicket>> g_focusLightIsolationTicket;
static std::atomic<std::shared_ptr<const NativeGameValveSnapshot>> g_gameValveObservation;
static ymcc::idle::Accumulator g_inputIdleActivity;
namespace ymcc::idle::detail {struct AccumulatorTestAccess {
static void lock(Accumulator& a){a.mx_.lock();}static void unlock(Accumulator& a){a.mx_.unlock();}
};}
static bool focusPrepareSummonSession();
static void appendNativeLifecycleLog(const char*,const json& value){fake::controls.push_back("lifecycle-log");fake::error=999;fake::lifecycleRows.push_back(value);}
static void appendDomainLog(const char* domain,const char* event,const json& row){if(std::string(event)!="focus-shortcut-input")throw std::runtime_error("wrong event");fake::error=123;if(fake::throwSink)throw std::runtime_error("fake sink failure");fake::domains.emplace_back(domain);fake::rows.push_back(row);}
static void controllerIsolationSummonEvidence(const char*,unsigned long long,bool) noexcept {}
static void focusLosslessReturnCancel(){++fake::losslessCancels;}
static void taskViewInvalidateHandover(){}
static void focusLightHandoffCancel(){++fake::handoffCancels;}
static void focusReleaseOwnedTopmost(){++fake::topmostReleases;}
static FocusTargetSnapshot focusCaptureTarget(HWND h){FocusTargetSnapshot t;t.hwnd=h;t.valid=true;return t;}
static void focusBeginReturnToPreviousWindow(ULONGLONG){fake::controls.push_back("return-request");fake::foregroundPid=444;fake::error=333;}
static void bringToFront(HWND,bool){fake::controls.push_back("bringToFront");if(fake::integrated)fake::check(focusPrepareSummonSession(),"ordinary focus preparation failed");if(fake::onBring)fake::onBring();fake::foregroundPid=9848;fake::error=444;}
static json nativeSummonCandidateSnapshot(const FocusTargetSnapshot& t,unsigned long long){return {{"valid",t.valid}};}
static void ipc_emit(const char* name,const json& value){fake::controls.push_back("ipc_emit");fake::events.emplace_back(name,value);fake::error=555;}
static void queueSummonGameRegistration(const FocusTargetSnapshot&,unsigned long long){++fake::registrationCalls;fake::controls.push_back("queue-registration");fake::error=666;}
static bool radialHasInputOwner(){return fake::radial;}
static bool customSteamLibraryInputSuppressed(){return fake::child;}
static bool gamepadUiInputEligible(){return fake::ui;}
static void reset(){
fake::reads=fake::registrationCalls=0;fake::error=12345;fake::foregroundPid=444;fake::valid=true;fake::hide=fake::throwRead=fake::throwSink=fake::integrated=fake::ownForeground=fake::radial=fake::child=false;fake::ui=true;fake::now=1000;
fake::controls.clear();fake::domains.clear();fake::rows.clear();fake::lifecycleRows.clear();fake::events.clear();fake::onBring={};
g_focusSession={};g_pendingSummonTarget={};g_focusSessionSequence=0;g_summonGeneration=0;g_summonRegistrationGeneration=0;g_focusIsolationPrepared=false;g_focusIsolationGameOpen=false;g_keyboardModifierEventGate=false;g_fullHeight=false;g_focusShortcutEvidenceFailures=0;g_controllerFrontendIsolationEnabled=true;g_focusLightIsolationTicket.store({});g_gameValveObservation.store({});
g_inputIdleActivity.enable(false,0);g_inputIdleActivity.enable(true,800);ymcc::idle::Pad pad{};pad.buttons=0x300;g_inputIdleActivity.physical(true,pad,900,true,1,7,"msi-dinput");
}
#define GetLastError fake::getError
#define SetLastError fake::setError
#define GetTickCount64 fake::tick
#define IsWindow fake::isWindow
#define IsWindowVisible fake::visible
#define IsIconic fake::iconic
#define IsZoomed fake::zoomed
#define GetForegroundWindow fake::foreground
#define GetAncestor fake::ancestor
#define GetWindowLongPtrW fake::style
#include FOCUS_ACTUAL_INCLUDE
#if FOCUS_BASELINE_AVAILABLE
#include "baseline_focus.inc"
#endif
static void retiredSelection(){
 reset();auto proof=std::make_shared<NativeGameValveSnapshot>();proof->discoveryState=NativeGameDiscoveryState::Complete;proof->qualificationObservedAt=fake::now;g_gameValveObservation.store(proof);
 for(unsigned i=0;i<8;++i){fake::now+=750;fake::check(!focusLightNoGameQualified(fake::now),"retired no-game admission reopened");fake::check(!focusLightIsolationBegin(),"retired ticket creation reopened");fake::check(!focusLightIsolationCurrent(),"retired ticket became current");fake::check(!g_focusLightIsolationTicket.load(),"retired selection allocated a ticket");}
 auto old=std::make_shared<FocusLightIsolationTicket>();g_focusLightIsolationTicket.store(old);fake::check(!focusLightIsolationCurrent(),"stale ticket revived retired isolation");focusEndIsolationOnUi();fake::check(!g_focusLightIsolationTicket.load(),"ordinary end retained retired ticket");
}
static void retiredUiRoute(){
 reset();g_focusLightIsolationTicket.store(std::make_shared<FocusLightIsolationTicket>());
 for(bool enabled:{false,true})for(bool ui:{false,true})for(bool radial:{false,true})for(bool child:{false,true}){g_controllerFrontendIsolationEnabled=enabled;fake::ui=ui;fake::radial=radial;fake::child=child;fake::check(msiNativeForegroundUiEligible()==(radial||child||(enabled&&ui)),"retired exemption changed ordinary output isolation");}
}
static void retiredGameLatch(){
 reset();auto observation=std::make_shared<NativeGameValveSnapshot>();observation->discoveryState=NativeGameDiscoveryState::Complete;g_gameValveObservation.store(observation);focusPrepareIsolationBeforeShow();fake::check(g_focusIsolationPrepared&&!g_focusIsolationGameOpen,"ordinary no-game preflight failed");
 auto game=std::make_shared<NativeGameValveSnapshot>(*observation);game->game.pid=777;g_gameValveObservation.store(game);focusPrepareIsolationBeforeShow();fake::check(g_focusIsolationGameOpen,"game latch lost during retired admission");g_gameValveObservation.store(observation);focusPrepareIsolationBeforeShow();fake::check(g_focusIsolationGameOpen,"no-game observation erased active ordinary latch");focusEndIsolationOnUi();fake::check(!g_focusIsolationPrepared&&!g_focusIsolationGameOpen,"ordinary end did not return its latch");
}
static void retiredSummon(){
 for(bool gameOpen:{false,true}){reset();fake::integrated=true;auto observation=std::make_shared<NativeGameValveSnapshot>();observation->game.pid=gameOpen?777:0;observation->discoveryState=NativeGameDiscoveryState::Complete;g_gameValveObservation.store(observation);const auto target=focusCaptureTarget(fake::window(44));fake::check(nativeYmccShortcutSummon(target,true),"ordinary summon failed");fake::check(g_focusSession.active&&!g_focusSession.lightNoGame&&!g_focusSession.strictReturnOnly&&!g_focusLightIsolationTicket.load(),"ordinary summon selected retired mode");fake::check(fake::registrationCalls==1,"ordinary summon lost registration queue");fake::check(!fake::events.empty()&&!fake::events.back().second.contains("skipGameRegistration"),"ordinary candidate kept light skip-registration");fake::check(g_focusSession.target.hwnd==target.hwnd,"ordinary summon lost captured return target");}
 reset();fake::integrated=true;fake::onBring=[] {++g_summonGeneration;};const auto target=focusCaptureTarget(fake::window(44));fake::check(nativeYmccShortcutSummon(target,true),"reentrant summon changed result");fake::check(fake::registrationCalls==0&&fake::events.empty(),"retired late summon published after replacement");
}
int main(int argc,char**argv){
 unsigned scenarios=0,comparisons=0;std::string selected;std::vector<std::string> completed;
 try{
  if(argc!=1){fake::check(argc==3&&std::string(argv[1])=="--case","invalid case selection");selected=argv[2];}
  const std::vector<std::pair<const char*,void(*)()>> retiredCases={
   {"retired-selection",retiredSelection},{"retired-ui-route",retiredUiRoute},{"retired-game-latch",retiredGameLatch},{"retired-summon",retiredSummon}
  };
  if(selected.empty())legacyCases(scenarios,comparisons);
  for(const auto& entry:retiredCases)if(selected.empty()||selected==entry.first){entry.second();completed.emplace_back(entry.first);}
  fake::check(!completed.empty(),"unknown or empty case selection");
  std::cout<<json{{"pass",true},{"checks",fake::checks},{"controlScenarios",scenarios},{"baselineComparisons",comparisons},{"lightScenarios",completed},{"selectedCase",selected},{"retiredSourcePolicy",true},{"historicalLightScenariosExecuted",false},{"actualOsInput",false},{"realSteamAccess",false},{"candidateMetadataPort","inert; candidate JSON producer not executed"}}.dump()<<'\n';return 0;
 }catch(const std::exception&e){std::cout<<json{{"pass",false},{"checks",fake::checks},{"controlScenarios",scenarios},{"baselineComparisons",comparisons},{"lightScenarios",completed},{"selectedCase",selected},{"error",e.what()}}.dump()<<'\n';std::cerr<<e.what()<<'\n';return 1;}
}
#else
// Actual shortcut wrapper + scalar physical peek; all OS/UI/input APIs are fakes.
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include "input_idle_diagnostics.h"
#include "json.hpp"
using json=nlohmann::json;
namespace fake {
unsigned checks=0,reads=0;DWORD error=12345,foregroundPid=444;bool valid=true,hide=false,throwRead=false,throwSink=false;
ULONGLONG now=1000,created=101;DWORD identityPid=444;unsigned registrationCalls=0,ruleWrites=0,legacyReturns=0,ownerEvents=0;
 unsigned showCalls=0,steamUiChanges=0;bool steamUiLastTicketPresent=false,ownerEventAfterClear=false;
bool durableSucceeds=true;unsigned durableWrites=0,downloadCancels=0,publicationNotifications=0;
bool integrated=false,visibleWindow=true,iconicWindow=false,refocusSucceeds=true,returnSucceeds=true,usable=true,identityKnown=true,blocked=false,paused=false;
std::atomic<bool> isolationSettingsOn{true};bool uiAuthority=true,radialOwner=false,childOwner=false;unsigned uiGateReads=0,deviceWorkRequests=0;
HWND window(std::uintptr_t value){return reinterpret_cast<HWND>(value);}
HWND foreground=window(44);
std::wstring identityPath=L"C:\\Apps\\desktop.exe",identityClass=L"DesktopApplication";
std::vector<HWND> focusAttempts;std::vector<bool> refocusAttempts;
std::map<UINT_PTR,UINT> timers;std::vector<std::pair<std::string,json>> events;
std::function<void()> onHide,onShow,onMemory,onPosition;
 std::function<void()> onBringTop,onForeground,onActive,onFocus,onMoveFocus;
 HRESULT moveFocusResult=S_OK;
 unsigned moveFocusCalls=0,activeCalls=0,focusCalls=0,boundsCalls=0,altFallbackCalls=0;
  bool keyboardReadable=true,focusAfterAlt=false;
  bool taskViewPaused=false;
  bool returnTargetRejected=false,returnTargetRootOwner=false;
  int queuedModifier=0,asyncModifier=0;
  UINT firstAltInserted=2;
  std::vector<UINT> altRequestCounts;
  std::function<void()> onKeyboardRead,onAlt;
 std::vector<BOOL> controllerVisibility;
std::vector<std::wstring> whitelist,excludes;
std::vector<std::string> controls;std::vector<json>rows;std::vector<std::string>domains;
std::vector<json> lifecycleRows;
std::vector<std::function<void()>> handoffJobs;
bool acceptHandoff=true;
bool throwHandoffQueue=false;
unsigned handoffExecutions=0;
unsigned uiRecheckPosts=0,nativeSettingDrops=0;
bool recordNativeEligibility=false,lastNativeEligibility=false;
bool nativeDesired=false;
bool exerciseSteamLease=false,steamInstanceAvailable=true,acceptSteamLeaseQueue=true;
std::string persona="disabled";
json steamLeaseOverride;
std::vector<std::function<void()>> steamLeaseJobs;
std::vector<json> steamLeaseRequests;
std::function<void()> onSteamLeaseRequest;
std::function<void()> onUiOutputEligibility;
void check(bool okay,const char* label){++checks;if(!okay)throw std::runtime_error(label);}
DWORD getError()noexcept{return error;}void setError(DWORD e)noexcept{error=e;}
ULONGLONG tick()noexcept{error=777;return now;}
BOOL isWindow(HWND){controls.push_back("IsWindow");error=222;return valid;}
BOOL visible(HWND){controls.push_back("IsWindowVisible");return visibleWindow;}
BOOL iconic(HWND){controls.push_back("IsIconic");return iconicWindow;}
BOOL zoomed(HWND){controls.push_back("IsZoomed");return FALSE;}
HWND getForeground(){return foreground;}
BOOL keyboardState(PBYTE keys){std::fill_n(keys,256,BYTE{});if(queuedModifier)keys[queuedModifier]=0x80;if(onKeyboardRead){auto action=std::exchange(onKeyboardRead,{});action();}return keyboardReadable;}
SHORT asyncKeyState(int key){return key==asyncModifier?static_cast<SHORT>(0x8000):0;}
HWND ancestor(HWND hwnd,UINT){return hwnd;}
DWORD processId(){return 9848;}
LONG_PTR windowLong(HWND,int){return 0;}
BOOL position(HWND,HWND order,int,int,int,int,UINT){if(order==HWND_NOTOPMOST&&onPosition){auto action=onPosition;action();}return TRUE;}
DWORD windowPid(HWND,DWORD* pid){*pid=identityPid;return 1;}
BOOL setForeground(HWND hwnd){const bool main=hwnd==window(42);if(!main)focusAttempts.push_back(hwnd);const bool succeeded=main?refocusSucceeds:returnSucceeds;if(succeeded)foreground=hwnd;if(onForeground){auto action=std::exchange(onForeground,{});action();}return succeeded;}
BOOL bringTop(HWND){if(onBringTop){auto action=std::exchange(onBringTop,{});action();}return TRUE;}
BOOL show(HWND,int){visibleWindow=true;iconicWindow=false;return TRUE;}
HWND activate(HWND hwnd){++activeCalls;if(onActive){auto action=std::exchange(onActive,{});action();}return hwnd;}
HWND focus(HWND hwnd){++focusCalls;if(onFocus){auto action=std::exchange(onFocus,{});action();}return hwnd;}
BOOL clientRect(HWND,RECT* bounds){*bounds={0,0,1280,800};return TRUE;}
BOOL killTimer(HWND,UINT_PTR timer){timers.erase(timer);return TRUE;}
UINT_PTR setTimer(HWND,UINT_PTR timer,UINT interval,TIMERPROC){timers[timer]=interval;return timer;}
HANDLE openProcess(DWORD,BOOL,DWORD){return reinterpret_cast<HANDLE>(std::uintptr_t(1));}
BOOL memoryInfo(HANDLE,PROCESS_MEMORY_COUNTERS* memory,DWORD){memory->WorkingSetSize=512ULL*1024*1024;if(onMemory)onMemory();return TRUE;}
BOOL closeHandle(HANDLE){return TRUE;}
BOOL updateWindow(HWND){return TRUE;}
BOOL postMessage(HWND,UINT message,WPARAM,LPARAM){if(message==WM_APP+828)++uiRecheckPosts;return TRUE;}
}
namespace ymcc::keyboardEvidence {
struct Snapshot {unsigned mask=0;DWORD pid=0;};
Snapshot read(){++fake::reads;fake::error=888;if(fake::throwRead)throw std::runtime_error("fake snapshot failure");return {17,fake::foregroundPid};}
json toJson(const Snapshot& s){return {{"asyncDownMask",s.mask},{"foregroundPidBefore",s.pid},{"foregroundPidAfter",s.pid},{"captureBeginTick",1000},{"captureEndTick",1000},{"sourceAttribution","unobserved"}};}
}
namespace ymcc {
struct ShortcutWindowVisibility {bool restoreMaximized=false;};
static bool summonPressHides(bool,bool,bool,bool,bool){fake::controls.push_back("summon-policy");return fake::hide;}
static bool summonPressHides(const ShortcutWindowVisibility&,bool,bool){return summonPressHides(true,true,false,false,false);}
}
static ymcc::ShortcutWindowVisibility readShortcutWindowVisibility(HWND){return {};}
#include "sleep_power_revocation.h"
#include "sleep_power_dispatch.h"
namespace ymcc::lightforeground {
struct Request {HWND target{};HWND origin{};DWORD parentPid{};ULONGLONG parentCreated{};DWORD originPid{};ULONGLONG originCreated{};ULONGLONG deadline{};};
#include "actual_light_cancellation.inc"
static std::function<void()> beforeForegroundAction;
static bool signalled(const std::shared_ptr<Cancellation>& cancellation){return WaitForSingleObject(cancellation->event,0)==WAIT_OBJECT_0;}
struct Result {bool attempted=false;bool foreground=false;bool reclaimed=true;DWORD error=0;DWORD exitCode=0;const char* reason="fake-not-started";};
static ULONGLONG processCreated(DWORD){return fake::created;}
static Result execute(const Request& request,const std::shared_ptr<Cancellation>& cancellation){
 ++fake::handoffExecutions;
 if(const auto action=std::exchange(beforeForegroundAction,{}))action();
 if(signalled(cancellation)||fake::now>=request.deadline||fake::foreground!=request.origin)return {false,false,true,0,0,"fake-cancelled"};
 fake::foreground=request.target;return {true,true,true,0,0,"fake-acquired"};
}
}
#include "actual_focus_types.inc"
struct WindowsSettlePortState {
 ymcc::lightforeground::Request request{fake::window(42),fake::window(44),9848,101,444,101,1250};
 ULONGLONG currentTime=1000;
 bool deliverySucceeded=true;
 mutable unsigned sendCalls=0;
 mutable HWND sentTarget=nullptr;
 mutable UINT sentMessage=WM_USER,sentFlags=0,sentTimeout=0;
 mutable WPARAM sentWparam=1;
 mutable LPARAM sentLparam=1;
 mutable bool replyProvided=false;
 std::function<void()> onSend;
 ULONGLONG now()const{return currentTime;}
 LRESULT fakeForegroundSendMessageTimeout(HWND target,UINT message,WPARAM wparam,LPARAM lparam,UINT flags,UINT timeout,DWORD_PTR* reply)const{
  ++sendCalls;sentTarget=target;sentMessage=message;sentWparam=wparam;sentLparam=lparam;sentFlags=flags;sentTimeout=timeout;replyProvided=reply!=nullptr;
  if(reply)*reply=0;
  if(onSend)onSend();
  return deliverySucceeded?1:0;
 }
};
static std::shared_ptr<FocusLightHandoffState>g_focusLightHandoff;
static HWND g_hwnd=reinterpret_cast<HWND>(std::uintptr_t(42));
struct OrdinaryOverlayPort {
 struct Binding { bool originalUiInput=false; };
 std::shared_ptr<const Binding> snapshot() const {return {};}
 bool active() const {return false;}
 void requestCheck() const {}
};
static OrdinaryOverlayPort g_gameOverlay;
enum class GameOverlayAttempt {Ordinary,Begun,Refused};
static GameOverlayAttempt gameOverlayTryBegin(const FocusTargetSnapshot&,bool){return GameOverlayAttempt::Ordinary;}
static bool shortcutSummonRenderBusy(){return false;}
static bool shortcutSummonWaitForRender(const FocusTargetSnapshot&,bool){return false;}
static void shortcutSummonClearRenderWait(){}
static bool gameOverlayInputEligible(){return false;}
static bool gameOverlayOutputOwns(){return false;}
static bool fixtureUiProbeCanIdle=true;
static bool steamLightGamepadUiProbeCanIdle(){return fixtureUiProbeCanIdle;}
static void gameOverlayStop(const char*){}
static HWND mainUiPresentationWindow(){return g_hwnd;}
static void controllerFseWakeReconnectRefresh(const json&){}
static constexpr unsigned COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC=0;
struct FixtureWebViewController {
 HRESULT MoveFocus(unsigned){++fake::moveFocusCalls;if(fake::onMoveFocus){auto action=std::exchange(fake::onMoveFocus,{});action();}return fake::moveFocusResult;}
 HRESULT put_IsVisible(BOOL visible){fake::controllerVisibility.push_back(visible);return S_OK;}
 HRESULT put_Bounds(RECT){++fake::boundsCalls;return S_OK;}
};
static FixtureWebViewController fixtureController;
static FixtureWebViewController* g_ctrl=&fixtureController;
namespace ymcc::keyboardEvents {enum class SourceId {FocusAltFallback};}
static UINT keyboardSelfSendWithEvidence(ymcc::keyboardEvents::SourceId,UINT count,const INPUT*){
 ++fake::altFallbackCalls;fake::altRequestCounts.push_back(count);
 const UINT inserted=fake::altFallbackCalls==1?fake::firstAltInserted:count;
 if(inserted==2&&fake::focusAfterAlt)fake::refocusSucceeds=true;
 if(fake::onAlt){auto action=std::exchange(fake::onAlt,{});action();}
 return inserted;
}
static std::atomic<unsigned long long>g_summonGeneration{0};
static std::atomic<unsigned long long>g_summonRegistrationGeneration{0},g_gameRulesEpoch{1},g_inputHostEpochMirror{2},g_msiMainSourceRevision{3};
static std::atomic<ULONGLONG>g_focusLightProofNotBefore{0};
static std::atomic<bool>g_focusIsolationPrepared{false},g_focusIsolationGameOpen{false};
static std::atomic<unsigned>g_xinputTopologyEpoch{4};
static std::atomic<bool>g_exitRequested{false};
static std::atomic<bool>& g_controllerFrontendIsolationEnabled=fake::isolationSettingsOn;
enum class PowerLifecycle {Ready,Suspending,Resuming};
static std::atomic<PowerLifecycle>g_powerLifecycle{PowerLifecycle::Ready};
static unsigned long long fixturePower=7;
static unsigned long long currentPowerGeneration(){return fixturePower;}
namespace fixtureSharedPower {
static ymcc::sleeppower::SharedPowerV1 value;
static bool available=true;
static unsigned unavailableReads=0;
static std::atomic<unsigned> reads{0};
static void (*afterRead)()=nullptr;
static int __cdecl read(ymcc::sleeppower::SharedPowerV1* output,std::uint32_t bytes){
 reads.fetch_add(1,std::memory_order_relaxed);
 if(!available||!output||bytes!=sizeof(*output))return 0;
 if(unavailableReads){--unavailableReads;return 0;}
 *output=value;if(!output->generation)output->generation=fixturePower;
 if(const auto callback=std::exchange(afterRead,nullptr))callback();
 return 1;
}
}
static bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& output)noexcept{return ymcc::sleeppower::capturePower(fixtureSharedPower::read,output);}
static std::pair<std::uint64_t,std::uint64_t> g_focusIsolationPowerPeriod;
static std::atomic<std::uint64_t> g_sharedPowerSuspendSerial{0};
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* output,uint32_t bytes){return fixtureSharedPower::read(output,bytes);}
static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& captured)noexcept{return ymcc::sleeppower::powerCurrent(fixtureSharedPower::read,captured);}
static ymcc::sleeppower::RevocationMirrors fixturePowerRevocations;
[[maybe_unused]] static auto observeSharedPowerRevocation(const ymcc::sleeppower::SharedPowerV1& captured) noexcept {
 return fixturePowerRevocations.observe(fixtureSharedPower::read,captured);
}
namespace ymcc::gameInventory {static std::atomic<unsigned long long>invalidation{9};}
static std::atomic<std::shared_ptr<const NativeGameValveSnapshot>>g_gameValveObservation;
static std::shared_ptr<const NativeGameValveSnapshot>g_focusLightFallbackObservation;
static std::atomic<std::shared_ptr<const FocusLightIsolationTicket>>g_focusLightIsolationTicket;
static std::mutex g_gameValvePublishMx;
static unsigned long long g_gameValvePublishedGeneration=0;
static std::atomic<bool>g_updateGameDetected{true};
static std::atomic<ULONGLONG>g_updateGameObservationTick{0};
static constexpr UINT WM_LIGHT_ISOLATION_RECHECK=WM_APP+828;
static FocusSessionState g_focusSession;
static unsigned long long g_focusSessionSequence=0;
static int g_summonFocusRetries=0;
static bool g_summonAltTried=false,g_bClosePending=false,g_deferFirstShow=false;
static bool g_webviewReady=true;
static std::atomic<bool> g_emptyTelemetryHiddenHint{false};
static std::atomic<unsigned long long> g_webviewGeneration{1};
static int g_firstShowCmd=SW_SHOWNORMAL;
static ULONGLONG g_bClosePendingSince=0,g_bCloseReadyAt=0;
static constexpr UINT_PTR SUMMON_FOCUS_TIMER_ID=1,RETURN_GAME_FOCUS_TIMER_ID=2;
static std::atomic<bool>g_fullHeight{false},g_keyboardModifierEventGate{false};
static std::atomic<std::uint64_t>g_focusShortcutEvidenceFailures{0};
static FocusTargetSnapshot g_pendingSummonTarget;
static ymcc::idle::Accumulator g_inputIdleActivity;
static void actualFocusBeginReturnToPreviousWindow(ULONGLONG);
static void actualBringToFront(HWND,bool);
static void focusClearSession(bool retainFloatingPresentation=false);
static bool focusLightIsolationCurrent();
static bool focusLightMayRefocus(ULONGLONG);
static bool msiNativeForegroundUiEligible();
static std::atomic<bool> g_msiNativeForegroundObserverActive{true};
static std::atomic<bool> g_msiNativeForegroundDesiredUi{false};
static std::atomic<bool> g_hardwareWriteGate{true};
static std::atomic<ULONGLONG> g_focusLightSuspendProofAfter{0};
static void revokeSharedPowerMirrors(){}
namespace ymcc::batterycharge {static void notifySystemPower(){}}
static void sgStaticScanCancel(const char*){}
static void msiDinputInvalidateFeedbackLease(){}
template<class... Args>static void traceLog(const char*,Args...){}
static void focusLightHandoffCancel();
static void focusLightHandoffRequest();
static void focusLightHandoffCompleteOnUi();
static bool focusSummonRefocus(bool);
static bool focusLightHandoffSettingsEnabled(){return g_controllerFrontendIsolationEnabled.load();}
static bool controllerFrontendIsolationSetting(const json& input){return input.value("enabled",true);}
static void msiForegroundIsolationSetUiOwned(bool enabled){if(!enabled)++fake::nativeSettingDrops;}
static bool poolSubmit(std::function<void()> job){if(fake::throwHandoffQueue)throw std::runtime_error("queue allocation");if(!fake::acceptHandoff)return false;fake::handoffJobs.push_back(std::move(job));return true;}
static void focusBeginReturnToPreviousWindow(ULONGLONG now){fake::controls.push_back("return-request");if(fake::integrated)actualFocusBeginReturnToPreviousWindow(now);fake::foregroundPid=444;fake::error=333;}
static void bringToFront(HWND hwnd,bool maximize){fake::controls.push_back("bringToFront");if(fake::integrated)actualBringToFront(hwnd,maximize);fake::foregroundPid=9848;fake::error=444;}
static void ipc_emit(const char* name,const json& value){fake::controls.push_back("ipc_emit");fake::events.emplace_back(name,value);fake::error=555;}
static void queueSummonGameRegistration(const FocusTargetSnapshot&,unsigned long long){++fake::registrationCalls;fake::controls.push_back("queue-registration");fake::error=666;}
static void appendNativeLifecycleLog(const char*,const json& detail){fake::controls.push_back("lifecycle-log");fake::error=999;fake::lifecycleRows.push_back(detail);}
static void appendDomainLog(const char* domain,const char* event,const json& row){if(std::string(event)!="focus-shortcut-input")throw std::runtime_error("wrong event");fake::error=123;if(fake::throwSink)throw std::runtime_error("fake sink failure");fake::domains.emplace_back(domain);fake::rows.push_back(row);}
static void controllerIsolationSummonEvidence(const char*,unsigned long long,bool) noexcept {}
static void focusLosslessReturnCancel(){}
static void taskViewInvalidateHandover(){}
static bool taskViewFocusHandoffPaused(){return fake::taskViewPaused;}
static struct {bool active()const{return false;}} g_gestureSummon;
static bool focusLosslessReturnBegin(ULONGLONG){++fake::legacyReturns;return false;}
static bool focusLosslessReturnTimerTick(ULONGLONG){return false;}
static bool focusForegroundIsTransientOrShell(){return false;}
static bool refocusPreviousWindow(){++fake::legacyReturns;return false;}
static void actualSteamLightGamepadUiChanged();
static bool steamLightGamepadFallbackRequired();
static bool steamLightGamepadAttemptActive();
static void steamLightGamepadEndIsolation();
inline void steamLightGamepadUiChanged(){++fake::steamUiChanges;fake::steamUiLastTicketPresent=bool(g_focusLightIsolationTicket.load());if(fake::exerciseSteamLease)actualSteamLightGamepadUiChanged();}
inline void msiNativeForegroundUiEvent(bool force=false){++fake::ownerEvents;fake::ownerEventAfterClear=!g_focusSession.active&&!g_focusLightIsolationTicket.load()&&g_summonRegistrationGeneration==0;const bool eligible=msiNativeForegroundUiEligible();const bool previous=g_msiNativeForegroundDesiredUi.exchange(eligible);if(fake::recordNativeEligibility){fake::lastNativeEligibility=eligible;if(force||previous!=eligible)++fake::deviceWorkRequests;fake::nativeDesired=eligible;}else ++fake::deviceWorkRequests;}
static bool actualGamepadUiOutputIsolationEligible();
static bool actualGamepadUiInputEligible();
static bool useActualUiInputGate=false;
static bool gamepadUiInputEligible(){return useActualUiInputGate?actualGamepadUiInputEligible():fake::radialOwner || fake::childOwner || fake::uiAuthority;}
static bool gamepadUiOutputIsolationEligible(){++fake::uiGateReads;const bool eligible=actualGamepadUiOutputIsolationEligible();if(auto action=std::exchange(fake::onUiOutputEligibility,{}))action();return eligible;}
inline bool radialHasInputOwner(){return fake::radialOwner;}
inline bool customSteamLibraryInputSuppressed(){return fake::childOwner;}
static bool focusGameControlBlocked(){return fake::blocked;}
static bool focusPidIsManuallyPaused(DWORD){return fake::paused;}
static bool isUsableFocusWindow(HWND,DWORD){return fake::usable;}
static bool focusQueryProcessIdentity(DWORD,std::wstring* path,ULONGLONG* created){*path=fake::identityPath;*created=fake::created;return fake::identityKnown;}
static bool steamLightGamepadPoolSubmit(std::function<void()> job){if(!fake::acceptSteamLeaseQueue)return false;fake::steamLeaseJobs.push_back(std::move(job));return true;}
static double sgNowEpoch(){return static_cast<double>(fake::now)/1000.0;}
static std::string inputHostConfiguredPersona(){return fake::persona;}
static std::atomic<unsigned> g_inputHostVirtualIntentWriters{0};
static std::atomic<unsigned long long> g_inputHostVirtualIntentRevision{1};
class InputHostMutex {
public:
 bool owned=false,busy=false;
 bool ownedByCurrentThread()const{return owned;}
 bool try_lock(){return !busy;}
 void unlock(){}
};
static InputHostMutex g_inputHostMx;
struct FixtureVirtualRequest {
 unsigned long long requestId=0;
 std::string persona="disabled",phase="idle";
};
static struct FixtureVirtualProtection {
 FixtureVirtualRequest request;
 FixtureVirtualRequest view(ULONGLONG)const{return request;}
} g_virtualStartProtection;
static ULONGLONG inputHostStartProtectionNow(){return fake::now;}
static unsigned long long g_inputHostEpoch=0;
static std::string g_inputHostRunId;
static HANDLE g_inputHostProcess=nullptr,g_inputHostPrimaryThread=nullptr,g_inputHostServerPipe=nullptr,g_inputHostUnadoptedPipe=nullptr;
static bool g_inputHostHello=false,g_inputHostPrepared=false,g_inputHostPrepareMayHaveReachedHost=false;
static bool g_inputHostNeutralized=false,g_inputHostActive=false,g_inputHostReleaseQuarantined=false;
static struct FixtureVisibilityJournal {bool active=false;} g_hidHideVisibilityJournal;
static struct FixtureSteamDeckDebt {bool owed=false;bool required()const{return owed;}} g_msiSteamDeckReleaseDebt;
static struct FixtureEndpointDebt {bool owed=false;bool retirementRequired()const{return owed;}} g_psEndpointOwnership;
static std::atomic<bool> g_msiDinputReturnOwed{false},g_inputHostSwitchExecutionReturnPending{false};
static std::atomic<bool> g_inputHostSwitchRecoveryPending{false},g_inputHostRecoveryPending{false};
static std::atomic<bool> g_inputHostRuntimeRecoveryPending{false},g_inputHostTupleHealPending{false};
static std::atomic<bool> g_msiNativeForegroundCyclePending{false},g_postHideCycleOwned{false};
namespace ymcc::inputHost::pipeTransport {
struct FixturePipeSnapshot {bool available=true,stopped=true;};
struct FixturePipeRuntime {
 FixturePipeSnapshot value;
 std::function<void()> onSnapshot;
 FixturePipeSnapshot snapshot(){if(onSnapshot){auto callback=std::exchange(onSnapshot,{});callback();}return value;}
};
static FixturePipeRuntime fixtureRuntime;
static bool fixturePresent=false;
static FixturePipeRuntime* existingProcessRuntime(){return fixturePresent?&fixtureRuntime:nullptr;}
}
namespace ymcc::inputHost::targetReturnRuntime {
struct FixtureRecord {std::vector<int> obligations;};
struct FixtureSession {
 FixtureRecord value;
 bool isReady=true,hasRecord=true;
 bool ready()const{return isReady;}
 const FixtureRecord* record()const{return hasRecord?&value:nullptr;}
};
static struct FixtureState {
 bool coldHistoryPending=false;
 std::vector<int> targets,physicals,proven;
 std::shared_ptr<FixtureSession> session;
} state;
}
static bool fixtureNativeVisibilityHistoryPending=false;
static bool inputHostNativeVisibilityHistoryPending(){return fixtureNativeVisibilityHistoryPending;}
static void resetVirtualPorts(){
 fixtureSharedPower::value={};fixtureSharedPower::value.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;fixtureSharedPower::available=true;fixtureSharedPower::unavailableReads=0;fixtureSharedPower::afterRead=nullptr;
 g_hardwareWriteGate=true;g_focusLightSuspendProofAfter=0;
 g_sharedPowerSuspendSerial=0;
 g_inputHostVirtualIntentWriters=0;g_inputHostVirtualIntentRevision=1;g_inputHostMx={};g_virtualStartProtection.request={};
 g_inputHostEpoch=0;g_inputHostRunId.clear();
 g_inputHostProcess=g_inputHostPrimaryThread=g_inputHostServerPipe=g_inputHostUnadoptedPipe=nullptr;
 g_inputHostHello=g_inputHostPrepared=g_inputHostPrepareMayHaveReachedHost=false;
 g_inputHostNeutralized=g_inputHostActive=g_inputHostReleaseQuarantined=false;
 g_hidHideVisibilityJournal.active=false;g_msiSteamDeckReleaseDebt.owed=g_psEndpointOwnership.owed=false;
 g_msiDinputReturnOwed=g_inputHostSwitchExecutionReturnPending=false;
 g_inputHostSwitchRecoveryPending=g_inputHostRecoveryPending=false;
 g_inputHostRuntimeRecoveryPending=g_inputHostTupleHealPending=false;
 g_msiNativeForegroundCyclePending=g_postHideCycleOwned=false;
 ymcc::inputHost::pipeTransport::fixtureRuntime={};ymcc::inputHost::pipeTransport::fixturePresent=false;
 ymcc::inputHost::targetReturnRuntime::state={};fixtureNativeVisibilityHistoryPending=false;
}
namespace ymcc::deckymirror {static std::string randomHex(unsigned){return std::string(32,'a');}}
struct FixtureSteamSession {DWORD pid=123,account=456;ULONGLONG created=101;std::wstring root=L"C:\\Steam";bool operator==(const FixtureSteamSession&)const=default;};
static struct FixtureSteamObserver {FixtureSteamSession sessionSnapshot()const{return fake::steamInstanceAvailable?FixtureSteamSession{}:FixtureSteamSession{0,0,0,L""};}} g_steamSettingsObserver;
static bool sleepSteamLiveProcessMatches(const json&,const std::wstring&){return fake::steamInstanceAvailable;}
static json steamLiveRequest(DWORD,const json& request,const std::function<bool()>& current){fake::steamLeaseRequests.push_back(request);if(fake::onSteamLeaseRequest){auto action=std::exchange(fake::onSteamLeaseRequest,{});action();}if(!current())return {{"ok",false},{"reason","request-cancelled"}};if(!fake::steamLeaseOverride.is_null())return fake::steamLeaseOverride;return {{"ok",true},{"enabled",request.at("enabled")},{"serial",request.at("serial")},{"remainingMs",2500},{"scope","steam-gamepad-navigation"}};}
static std::wstring focusWindowClass(HWND){return fake::identityClass;}
static FocusTargetSnapshot focusCaptureTarget(HWND hwnd){FocusTargetSnapshot target;if(fake::returnTargetRejected)return target;target.hwnd=fake::returnTargetRootOwner?fake::window(99):hwnd;target.pid=fake::identityPid;target.processCreated=fake::created;target.path=fake::identityPath;target.className=fake::identityClass;target.valid=hwnd!=nullptr;return target;}
static void focusReflowMainWindow(){}
static void actualMainWindowBeforeShow(HWND,int);
static void showWindowAnimated(HWND hwnd,int showCmd,bool){actualMainWindowBeforeShow(hwnd,showCmd);++fake::showCalls;fake::visibleWindow=true;if(fake::onShow){auto action=fake::onShow;action();}}
static void hideWindowAnimated(HWND){fake::visibleWindow=false;if(fake::onHide){auto action=fake::onHide;action();}}
static bool actualRefocusWebView(bool,const ymcc::lightforeground::Request*);
static bool refocusWebView(bool useAlt,const ymcc::lightforeground::Request* origin=nullptr){fake::refocusAttempts.push_back(useAlt);return actualRefocusWebView(useAlt,origin);}
static std::string W2U(const std::wstring& value){std::string result;for(const auto character:value)result.push_back(static_cast<char>(character));return result;}
static std::wstring sgBaseName(const std::wstring& value){return value.substr(value.find_last_of(L"\\/")+1);}
static std::mutex g_gameRulesMx;
static const std::wstring SG_DIR=L"fake-rules",SG_GAME_WHITELIST=L"fake-whitelist";
static constexpr SIZE_T SG_CAPTURE_MIN_WS=256ULL*1024*1024;
namespace ymcc::gamerules {struct FilesGuard {explicit FilesGuard(const std::wstring&) {}};}
static std::vector<std::wstring> sgExcludes(){return fake::excludes;}
static std::vector<std::wstring> sgGameWhitelist(){return fake::whitelist;}
static bool nativeMonitorExcluded(const std::wstring&){return false;}
static bool sgNameExcludedBy(const std::wstring& name,const std::vector<std::wstring>& values){return std::any_of(values.begin(),values.end(),[&](const auto& value){return _wcsicmp(name.c_str(),value.c_str())==0;});}
static bool sgWriteGameRuleFile(const std::wstring&,const std::vector<std::wstring>& values){++fake::ruleWrites;fake::whitelist=values;return true;}
static void nativeReconcileGameRulesLocked(bool){}
static void sgReadRecognitionGameRules(std::vector<std::wstring>& excludes,std::vector<std::wstring>& whitelist){excludes=sgExcludes();whitelist=sgGameWhitelist();}
static bool nativeWriteGameRulesLocked(const std::function<bool()>& write){if(!write())return false;++g_gameRulesEpoch;return true;}
struct FixtureDownloadControl {struct Status {unsigned operation=1;};Status status(){return {};}void cancel(unsigned){++fake::downloadCancels;}};
static FixtureDownloadControl g_updateDownloadControl;
static const std::wstring MONITOR_GAME_TARGET_JSON=L"fake-monitor-snapshot";
static bool sgWriteMonitorSnapshotDurable(const std::wstring&,const std::string&){++fake::durableWrites;return fake::durableSucceeds;}
static json nativeGameTargetJson(const NativeGameValveSnapshot& target){return {{"generation",target.generation},{"pid",target.game.pid}};}
static void steamOriginalRecoveryTargetChanged(DWORD,ULONGLONG,const std::wstring&,unsigned long long){}
static void gamepadSerialPostEvent(const char*,const json&){++fake::publicationNotifications;}
static void steamGameLinkNotify(){}
static void mousePolicyGameContextChanged(){}
namespace gameObservationProducer {
struct ProcessPort {
 DWORD pid=1001;
 std::wstring name=L"game",path=L"C:\\Games\\game.exe";
 SIZE_T workingSet=1024ULL*1024*1024;
 ULONGLONG birth=101;
 DWORD openError=0,session=1,waitState=WAIT_TIMEOUT;
 bool memoryKnown=true,sessionKnown=true,birthKnown=true,imageKnown=true;
};
inline std::vector<ProcessPort> processes;
inline ULONGLONG clock=100000;
inline DWORD lastError=0;
inline unsigned opens=0,closes=0,liveHandles=0,memoryReads=0,fullSnapshotReads=0,basicReads=0,legacyReads=0;
inline bool fullAvailable=true,basicAvailable=true,legacyAvailable=true,currentSessionKnown=true;
inline HWND foregroundWindow=fake::window(42),g_hwnd=fake::window(42);
inline std::atomic<unsigned long long> g_gameValveOnePidChecks{0},g_gameValveDetailQueries{0},g_gameValveIdentityRejects{0},g_gameValveIdentityUnverified{0},g_gameValveConfiguredNamePasses{0},g_gameValveFullScans{0};
inline bool tickCostEnabled(){return false;}
inline DWORD portGetLastError(){return lastError;}
inline void portSetLastError(DWORD value){lastError=value;}
inline ULONGLONG portGetTickCount64(){return clock;}
inline DWORD portGetCurrentProcessId(){return 9848;}
inline ProcessPort* process(DWORD pid){auto found=std::find_if(processes.begin(),processes.end(),[&](const auto& row){return row.pid==pid;});return found==processes.end()?nullptr:&*found;}
inline HANDLE portOpenProcess(DWORD,BOOL,DWORD pid){auto row=process(pid);if(!row||row->openError){lastError=row?row->openError:ERROR_INVALID_PARAMETER;return nullptr;}++opens;++liveHandles;return static_cast<HANDLE>(row);}
inline BOOL portCloseHandle(HANDLE handle){fake::check(handle&&liveHandles,"producer closed an unowned process handle");--liveHandles;++closes;return TRUE;}
inline BOOL portGetProcessMemoryInfo(HANDLE handle,PROCESS_MEMORY_COUNTERS* memory,DWORD){++memoryReads;const auto row=static_cast<ProcessPort*>(handle);if(!row->memoryKnown)return FALSE;memory->WorkingSetSize=row->workingSet;return TRUE;}
inline BOOL portQueryFullProcessImageNameW(HANDLE handle,DWORD,wchar_t* buffer,DWORD* length){const auto row=static_cast<ProcessPort*>(handle);if(!row->imageKnown||row->path.size()>=*length)return FALSE;std::copy(row->path.begin(),row->path.end(),buffer);*length=static_cast<DWORD>(row->path.size());return TRUE;}
inline BOOL portProcessIdToSessionId(DWORD pid,DWORD* session){if(pid==portGetCurrentProcessId()){*session=1;return currentSessionKnown;}const auto row=process(pid);if(!row||!row->sessionKnown)return FALSE;*session=row->session;return TRUE;}
inline BOOL portGetProcessTimes(HANDLE handle,FILETIME* birth,FILETIME*,FILETIME*,FILETIME*){const auto row=static_cast<ProcessPort*>(handle);if(!row->birthKnown)return FALSE;birth->dwLowDateTime=static_cast<DWORD>(row->birth);birth->dwHighDateTime=static_cast<DWORD>(row->birth>>32);return TRUE;}
inline DWORD portWaitForSingleObject(HANDLE handle,DWORD){return static_cast<ProcessPort*>(handle)->waitState;}
inline BOOL portIsWindowVisible(HWND){return TRUE;}
inline BOOL portIsIconic(HWND){return FALSE;}
inline HWND portGetForegroundWindow(){return foregroundWindow;}
inline HWND portGetAncestor(HWND window,UINT){return window;}
inline std::wstring sgBaseName(const std::wstring& path){auto name=path.substr(path.find_last_of(L"\\/")+1);if(name.size()>4&&name.substr(name.size()-4)==L".exe")name.resize(name.size()-4);return name;}
inline bool sgNameExcludedBy(const std::wstring& name,const std::vector<std::wstring>& names){return std::any_of(names.begin(),names.end(),[&](const auto& value){return _wcsicmp(name.c_str(),value.c_str())==0;});}
inline bool nativeMonitorExcluded(const std::wstring& name){return name==L"excluded";}
inline bool nativeIsConfiguredGameExe(const std::wstring& name,const std::vector<std::wstring>& names){return sgNameExcludedBy(name,names);}
inline std::wstring sgMatchingGameWhitelistRule(const std::wstring& name,const std::vector<std::wstring>& names){return sgNameExcludedBy(name,names)?name:L"";}
enum class NativeNameIdentity {Match,Mismatch,Unverifiable};
inline NativeNameIdentity nativeNameIdentityVerdict(const std::wstring& name,const std::wstring& path){return path.empty()?NativeNameIdentity::Unverifiable:(sgBaseName(path)==name?NativeNameIdentity::Match:NativeNameIdentity::Mismatch);}
inline std::wstring nativeProcessWindowTitle(DWORD){return L"fixture-game";}
namespace ymcc::processInventory {
enum class Status {Complete,Unavailable};
struct Row {DWORD pid;std::wstring name;};
struct Snapshot {Status status=Status::Unavailable;ULONGLONG observedAt=0;std::vector<Row> rows;};
inline void invalidate(){}
inline std::shared_ptr<const Snapshot> read(){++legacyReads;auto snapshot=std::make_shared<Snapshot>();snapshot->status=legacyAvailable?Status::Complete:Status::Unavailable;snapshot->observedAt=clock;for(const auto& row:processes)snapshot->rows.push_back({row.pid,row.name});return snapshot;}
namespace detail {struct LastErrorScope {DWORD saved=lastError;~LastErrorScope(){lastError=saved;}};}
}
namespace ymcc::gameInventory {
struct BasicEntry {DWORD pid=0;std::wstring_view name;ULONG64 sequence=0;DWORD parentPid=0;};
struct Entry {DWORD pid;std::wstring name;SIZE_T workingSet;ULONGLONG processCreated;};
class SharedBasicSnapshot {
 std::vector<BasicEntry> rows;
public:
 SharedBasicSnapshot(){++basicReads;for(const auto& row:processes)rows.push_back({row.pid,row.name,row.birth,0});}
 bool valid()const{return basicAvailable;}
 ULONGLONG observedAt()const{return clock;}
 const std::vector<BasicEntry>& entries()const{return rows;}
};
class Snapshot {
 std::vector<Entry> rows;
public:
 Snapshot(){++fullSnapshotReads;for(const auto& row:processes)rows.push_back({row.pid,row.name,row.workingSet,row.birth});}
 bool valid()const{return fullAvailable;}
 bool creationTimesVerified()const{return fullAvailable;}
 const std::vector<Entry>& entries()const{return rows;}
};
}
}
#define GetLastError fake::getError
#define SetLastError fake::setError
#define GetTickCount64 fake::tick
#define IsWindow fake::isWindow
#define IsWindowVisible fake::visible
#define IsIconic fake::iconic
#define IsZoomed fake::zoomed
#define GetForegroundWindow fake::getForeground
#define GetKeyboardState fake::keyboardState
#define GetAsyncKeyState fake::asyncKeyState
#define GetAncestor fake::ancestor
#define GetCurrentProcessId fake::processId
#define GetWindowLongPtrW fake::windowLong
#define SetWindowPos fake::position
#define GetWindowThreadProcessId fake::windowPid
#define SetForegroundWindow fake::setForeground
#define BringWindowToTop fake::bringTop
#define ShowWindow fake::show
#define SetActiveWindow fake::activate
#define SetFocus fake::focus
#define GetClientRect fake::clientRect
#define KillTimer fake::killTimer
#define SetTimer fake::setTimer
#define OpenProcess fake::openProcess
#undef GetProcessMemoryInfo
#define GetProcessMemoryInfo fake::memoryInfo
#define CloseHandle fake::closeHandle
#define UpdateWindow fake::updateWindow
#define PostMessageW fake::postMessage
// Scalar no-active-recovery-owner publication port; cancellation/Steam owners are
// outside this focus consumer and are not established by this stand-in.
#include FOCUS_ACTUAL_INCLUDE
#if FOCUS_BASELINE_AVAILABLE
#include "baseline_focus.inc"
#endif
#undef GetLastError
#undef SetLastError
#undef GetTickCount64
#undef IsWindow
#undef IsWindowVisible
#undef IsIconic
#undef IsZoomed
#undef GetForegroundWindow
#undef GetKeyboardState
#undef GetAsyncKeyState
#undef GetAncestor
#undef GetCurrentProcessId
#undef GetWindowLongPtrW
#undef SetWindowPos
#undef GetWindowThreadProcessId
#undef SetForegroundWindow
#undef BringWindowToTop
#undef ShowWindow
#undef SetActiveWindow
#undef SetFocus
#undef GetClientRect
#undef KillTimer
#undef SetTimer
#undef OpenProcess
#undef GetProcessMemoryInfo
#undef CloseHandle
#undef UpdateWindow
#undef PostMessageW
namespace ymcc::idle::detail {struct AccumulatorTestAccess {
static void lock(Accumulator& a){a.mx_.lock();}static void unlock(Accumulator& a){a.mx_.unlock();}
};}
static void reset(){
 resetVirtualPorts();
 fake::steamLeaseJobs.clear();fake::steamLeaseRequests.clear();fake::onSteamLeaseRequest={};fake::onUiOutputEligibility={};fake::steamLeaseOverride=nullptr;
 fake::exerciseSteamLease=false;fake::steamInstanceAvailable=fake::acceptSteamLeaseQueue=true;
 fake::persona="disabled";
 g_steamLightGamepadIntent.store({});g_steamLightGamepadLeaseState.store({});g_steamLightGamepadQueued=false;
 g_steamLightGamepadSettledSerial=0;g_steamLightGamepadReleaseUntil=0;g_steamLightGamepadSerial=0;g_steamLightGamepadNextRenewal=0;
 focusLightHandoffCancel();fake::handoffJobs.clear();g_focusLightHandoff.reset();fake::acceptHandoff=true;fake::throwHandoffQueue=false;fake::handoffExecutions=0;g_webviewReady=true;
 g_emptyTelemetryHiddenHint=false;
 fake::uiRecheckPosts=fake::nativeSettingDrops=0;
 fake::recordNativeEligibility=false;fake::lastNativeEligibility=false;g_msiNativeForegroundObserverActive=true;
 useActualUiInputGate=false;
 fake::nativeDesired=false;
 g_msiNativeForegroundDesiredUi=false;
 g_focusIsolationPrepared=false;g_focusIsolationGameOpen=false;
 g_focusLightFallbackObservation.reset();
 fake::controls.clear();fake::rows.clear();fake::domains.clear();fake::reads=0;fake::error=12345;fake::foregroundPid=444;fake::valid=true;fake::hide=false;fake::throwRead=fake::throwSink=false;
 fake::now=1000;fake::created=101;fake::identityPid=444;fake::registrationCalls=fake::ruleWrites=fake::legacyReturns=fake::ownerEvents=0;
 fake::showCalls=fake::steamUiChanges=0;fake::steamUiLastTicketPresent=fake::ownerEventAfterClear=false;
 fake::durableSucceeds=true;fake::durableWrites=fake::downloadCancels=fake::publicationNotifications=0;g_gameValvePublishedGeneration=0;g_updateGameDetected=true;g_updateGameObservationTick=0;
 fake::integrated=false;fake::visibleWindow=fake::refocusSucceeds=fake::returnSucceeds=fake::usable=fake::identityKnown=true;fake::blocked=fake::paused=false;
 fake::iconicWindow=false;
 fake::moveFocusResult=S_OK;fake::moveFocusCalls=fake::activeCalls=fake::focusCalls=fake::boundsCalls=fake::altFallbackCalls=0;fake::controllerVisibility.clear();g_ctrl=&fixtureController;g_webviewGeneration=1;
  fake::keyboardReadable=true;fake::focusAfterAlt=false;fake::queuedModifier=fake::asyncModifier=0;fake::firstAltInserted=2;fake::altRequestCounts.clear();fake::onKeyboardRead={};fake::onAlt={};
  fake::taskViewPaused=false;
  fake::returnTargetRejected=fake::returnTargetRootOwner=false;fake::lifecycleRows.clear();
 fake::onBringTop={};fake::onForeground={};fake::onActive={};fake::onFocus={};fake::onMoveFocus={};
 fake::isolationSettingsOn=fake::uiAuthority=true;fake::radialOwner=fake::childOwner=false;fake::uiGateReads=fake::deviceWorkRequests=0;
 fake::foreground=fake::window(44);fake::identityPath=L"C:\\Apps\\desktop.exe";fake::identityClass=L"DesktopApplication";
 fake::focusAttempts.clear();fake::refocusAttempts.clear();fake::timers.clear();fake::events.clear();fake::onHide={};fake::onShow={};fake::onMemory={};fake::onPosition={};fake::whitelist.clear();fake::excludes.clear();
 g_summonGeneration=0;g_summonRegistrationGeneration=0;g_focusLightProofNotBefore=0;g_focusShortcutEvidenceFailures=0;g_keyboardModifierEventGate=false;g_pendingSummonTarget={};g_focusSession={};g_focusSessionSequence=0;
 g_focusLightIsolationTicket.store({});g_gameValveObservation.store({});g_gameRulesEpoch=1;g_inputHostEpochMirror=2;g_msiMainSourceRevision=3;g_xinputTopologyEpoch=4;fixturePower=7;ymcc::gameInventory::invalidation=9;g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;
 g_fullHeight=false;g_summonFocusRetries=0;g_summonAltTried=g_bClosePending=g_deferFirstShow=false;g_bClosePendingSince=g_bCloseReadyAt=0;
 g_inputIdleActivity.enable(false,0);g_inputIdleActivity.enable(true,800);ymcc::idle::Pad pad{};pad.buttons=0x300;g_inputIdleActivity.physical(true,pad,900,true,1,7,"msi-dinput");
}
static void peekCases(){
 ymcc::idle::Accumulator a;ymcc::idle::PhysicalSnapshot s{};s.buttons=555;
 fake::check(!a.peekPhysical(s)&&s.buttons==555,"disabled peek changed output");
 a.enable(true,100);ymcc::idle::Pad p{};p.buttons=0x100;a.physical(true,p,110,true,1,7,"one");
 fake::check(a.peekPhysical(s)&&s.sampleTick==110&&s.observationTick==110&&s.latestPhysicalLive&&s.buttons==0x100,"fresh physical snapshot identity");
 auto w=a.take(120);fake::check(w.physical.samples==1&&w.physical.lastSampleTick==110,"peek drained source counter");
 fake::check(a.peekPhysical(s)&&s.sampleTick==110&&s.buttons==0x100&&a.take(121).physical.samples==0,"take erased latest scalar sample");
 a.physical(false,p,130,true,0,7,"one");fake::check(a.peekPhysical(s)&&!s.latestPhysicalLive&&s.sampleTick==110&&s.observationTick==130&&s.haveLastSample,"source absence relabelled old sample fresh");
 a.physical(false,p,140,false,0,8,"two");fake::check(a.peekPhysical(s)&&!s.haveLastSample&&s.powerGeneration==8,"source change retained valid old pad");
 ymcc::idle::detail::AccumulatorTestAccess::lock(a);s.buttons=556;
 const bool got=a.peekPhysical(s);ymcc::idle::detail::AccumulatorTestAccess::unlock(a);
 fake::check(!got&&s.buttons==556,"contended peek blocked or changed output");
 a.enable(false,150);a.enable(true,160);fake::check(a.peekPhysical(s)&&!s.haveLastSample&&!s.latestPhysicalLive&&s.observationTick==0,"reset retained stale source metadata");
}
struct Control {bool result;std::vector<std::string> calls;DWORD error;};
static Control run(bool old,bool enabled,bool hide,bool valid,bool throwRead=false,bool throwSink=false){
 reset();g_keyboardModifierEventGate=enabled;fake::hide=hide;fake::valid=valid;fake::throwRead=throwRead;fake::throwSink=throwSink;FocusTargetSnapshot target;target.valid=true;
 bool result=false;
#if FOCUS_BASELINE_AVAILABLE
 result=old?baselineNativeYmccShortcutSummon(target,true):nativeYmccShortcutSummon(target,true);
#else
 fake::check(!old,"baseline not selected");result=nativeYmccShortcutSummon(target,true);
#endif
 return {result,fake::controls,fake::error};
}
static void legacyCases(unsigned& scenarios,unsigned& comparisons){
 peekCases();
 for(bool enabled:{false,true})for(bool hide:{false,true})for(bool valid:{false,true})for(unsigned failure:{0u,1u,2u}){
#if FOCUS_BASELINE_AVAILABLE
  const auto old=run(true,enabled,hide,valid,failure==1,failure==2);
#endif
  const auto now=run(false,enabled,hide,valid,failure==1,failure==2);++scenarios;
#if FOCUS_BASELINE_AVAILABLE
  ++comparisons;
  fake::check(old.result==now.result&&old.calls==now.calls&&old.error==now.error,"diagnostics changed original summon policy/control/error outcome");
#else
  (void)comparisons;
#endif
  fake::check(now.result==valid&&!g_focusSession.lightNoGame,"legacy summon changed control result or selected light mode");
  if(!enabled||!valid)fake::check(fake::reads==0&&fake::rows.empty(),"closed/invalid gate queried or logged keyboard");
  if(enabled&&valid&&failure)fake::check(g_focusShortcutEvidenceFailures==2,"snapshot/sink failures escaped or not retained");
 }
 run(false,true,false,true);fake::check(fake::rows.size()==4&&fake::reads==2,"show boundary count");
 fake::check(fake::rows[0]==fake::rows[1]&&fake::rows[2]==fake::rows[3]&&fake::domains==std::vector<std::string>{"focus","virtual","focus","virtual"},"boundary mirror differs");
 fake::check(fake::rows[0]["phase"]=="before-decision"&&fake::rows[2]["phase"]=="show-returned"&&fake::rows[0]["summonGeneration"]==fake::rows[2]["summonGeneration"],"phase/generation correlation lost");
 fake::check(fake::rows[0]["keyboardModifiers"]["foregroundPidBefore"]==444&&fake::rows[2]["keyboardModifiers"]["foregroundPidBefore"]==9848,"sampled foreground order lost");
 const auto pad=fake::rows[0]["physicalAtBoundary"];
 fake::check(pad["leftShoulderInLastSample"]==true&&pad["rightShoulderInLastSample"]==true&&pad["sampleAgeMs"]==100&&pad["sampleIsLatestLiveObservation"]==true,"physical shoulder/sample freshness missing");
 fake::check(fake::rows[0]["crossSourceAtomic"]==false&&fake::rows[0]["keyboardProducerIdentified"]==false&&fake::rows[0]["keyReleaseProven"]==false,"boundary evidence overclaim");
 run(false,true,true,true);fake::check(fake::rows[2]["phase"]=="return-requested","hide boundary mislabeled focus completion");
 reset();g_keyboardModifierEventGate=true;ymcc::idle::detail::AccumulatorTestAccess::lock(g_inputIdleActivity);
 focusShortcutInputEvidence("test",5,false);ymcc::idle::detail::AccumulatorTestAccess::unlock(g_inputIdleActivity);
 fake::check(fake::rows.size()==2&&fake::rows[0]["physicalAtBoundary"]["captured"]==false,"contended capture was not unknown");
 fake::check(fake::error==12345,"diagnostic standalone call changed LastError");
 auto after=g_inputIdleActivity.take(1100);fake::check(after.physical.samples==1,"boundary peek consumed idle evidence");
}
static void publishProof(){
 auto observation=std::make_shared<NativeGameValveSnapshot>();observation->discoveryState=NativeGameDiscoveryState::Complete;observation->qualificationObservedAt=fake::now;
 observation->rulesEpoch=g_gameRulesEpoch.load();observation->qualificationRulesEpoch=g_gameRulesEpoch.load();observation->qualificationPower=fixturePower;observation->qualificationInventory=ymcc::gameInventory::invalidation.load();g_gameValveObservation.store(observation);
}
static void startLight(bool focused=true){
 reset();fake::integrated=true;fake::refocusSucceeds=focused;publishProof();
 fake::check(nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true),"light summon refused valid window");
 fake::check(g_focusSession.lightNoGame&&g_focusSession.active,"qualified summon failed to select light session");
}
static json summonEvent(){
 for(auto event=fake::events.rbegin();event!=fake::events.rend();++event)if(event->first=="gamepad.summon")return event->second;
 throw std::runtime_error("summon candidate missing");
}
static void proofBasic(){
 reset();fake::check(!focusLightNoGameQualified(fake::now),"absent proof admitted");publishProof();fake::check(focusLightNoGameQualified(fake::now),"fresh complete no-game proof rejected");
 const auto original=*g_gameValveObservation.load();
 for(unsigned counterexample=0;counterexample<7;++counterexample){
  auto observation=std::make_shared<NativeGameValveSnapshot>(original);
  switch(counterexample){
   case 0:observation->game.pid=123;break;
   case 1:observation->discoveryState=NativeGameDiscoveryState::Pending;break;
   case 2:observation->discoveryState=NativeGameDiscoveryState::Unavailable;break;
   case 3:observation->qualificationObservedAt=0;break;
   case 4:observation->qualificationObservedAt=fake::now+1;break;
   case 5:++observation->qualificationRulesEpoch;break;
   case 6:++observation->qualificationPower;break;
  }
  g_gameValveObservation.store(observation);fake::check(!focusLightNoGameQualified(fake::now),"incomplete or mismatched no-game proof admitted");
 }
 publishProof();g_powerLifecycle=PowerLifecycle::Resuming;fake::check(!focusLightNoGameQualified(fake::now),"non-Ready power admitted proof");
 g_powerLifecycle=PowerLifecycle::Ready;g_exitRequested=true;fake::check(!focusLightNoGameQualified(fake::now),"exit admitted proof");
}
static void proofExpiry(){
 reset();publishProof();fake::now=31000;fake::check(focusLightNoGameQualified(fake::now),"30000ms proof boundary rejected");
 auto heartbeat=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());heartbeat->generation=500;heartbeat->lastSeenEpoch=999999;g_gameValveObservation.store(heartbeat);
 ++fake::now;fake::check(!focusLightNoGameQualified(fake::now),"expired proof admitted");
}
static void proofInventory(){reset();publishProof();++ymcc::gameInventory::invalidation;fake::check(!focusLightNoGameQualified(fake::now),"inventory invalidation admitted");}
static void proofCutoff(){
 reset();publishProof();g_focusLightProofNotBefore=1001;fake::now=1001;fake::check(!focusLightNoGameQualified(fake::now),"pre-sleep proof admitted after cutoff");
 publishProof();fake::check(focusLightNoGameQualified(fake::now),"post-cutoff proof rejected");
}
static void ticketSource(){
 reset();publishProof();fake::check(focusLightIsolationBegin(),"fresh ticket rejected");++g_msiMainSourceRevision;
 bool current=true;std::thread observer([&]{current=focusLightIsolationCurrent();});observer.join();
 fake::check(!current&&!g_focusLightIsolationTicket.load(),"changed source retained ticket");
 --g_msiMainSourceRevision;fake::check(!focusLightIsolationCurrent(),"restored source resurrected revoked ticket");
}
static void ticketEpochs(){
 for(unsigned counterexample=0;counterexample<8;++counterexample){
  reset();publishProof();fake::check(focusLightIsolationBegin(),"epoch setup ticket rejected");
  switch(counterexample){
   case 0:++fixturePower;break;
   case 1:++g_gameRulesEpoch;break;
   case 2:++g_inputHostEpochMirror;break;
   case 3:++g_xinputTopologyEpoch;break;
   case 4:++ymcc::gameInventory::invalidation;break;
   case 5:fake::now=31001;break;
   case 6:fake::persona="steamdeck";break;
   case 7:++g_inputHostVirtualIntentRevision;break;
  }
  fake::check(!focusLightIsolationCurrent()&&!g_focusLightIsolationTicket.load(),"changed epoch or expired proof retained ticket");
  publishProof();fake::check(!focusLightIsolationCurrent(),"fresh proof resurrected revoked ticket without begin");
 }
}
static void lightWrapper(){
 startLight();const auto candidate=summonEvent();
 fake::check(candidate["isolationMode"]=="light-no-game"&&candidate["skipGameRegistration"]==true&&candidate["valid"]==true&&candidate["pid"]==444,"light candidate lost mode or captured identity");
 fake::check(fake::registrationCalls==0,"light summon queued registration");
 fake::check(fake::refocusAttempts==std::vector<bool>{false}&&g_focusSession.lightForegroundAcquired,"light summon injected Alt fallback");
 fake::check(fake::steamUiChanges>0&&fake::steamUiLastTicketPresent,"focused light summon delayed Steam isolation until a later input tick");
 fake::check(g_focusSession.target.hwnd==fake::window(44)&&g_focusSession.strictReturnOnly,"light session lost exact return origin");
 const auto sequence=g_focusSessionSequence;fake::check(focusPrepareSummonSession()&&g_focusSessionSequence==sequence,"repeated focused prepare replaced light session");
 startLight(false);fake::refocusSucceeds=true;fake::moveFocusResult=E_FAIL;
 fake::check(fake::timers.at(SUMMON_FOCUS_TIMER_ID)==64&&g_focusSession.summonNextAt==1064,"first light retry was not scheduled at 64ms");
 const auto initialAttempts=fake::refocusAttempts.size();
 fake::now=1063;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::check(fake::refocusAttempts.size()==initialAttempts,"early timer bypassed light retry gate");
 fake::now=1064;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::check(!g_focusSession.lightForegroundAcquired,"failed MoveFocus declared light foreground acquired");
 fake::check(fake::moveFocusCalls==1&&fake::timers.contains(SUMMON_FOCUS_TIMER_ID)&&fake::timers.at(SUMMON_FOCUS_TIMER_ID)==80,"failed MoveFocus lost bounded light timer");
 fake::moveFocusResult=S_OK;fake::now=1143;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::check(!g_focusSession.lightForegroundAcquired&&fake::moveFocusCalls==1,"later light retry bypassed normal cadence");
 fake::now=1144;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::check(g_focusSession.lightForegroundAcquired&&fake::moveFocusCalls==2&&!fake::timers.contains(SUMMON_FOCUS_TIMER_ID),"next 80ms MoveFocus did not recover light summon");
 startLight(false);fake::refocusSucceeds=true;fake::foreground=fake::window(55);
 fake::now=1064;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::check(!g_focusSession.active&&fake::foreground==fake::window(55)&&fake::moveFocusCalls==0,"early light retry stole the new foreground");
 startLight();fake::onMoveFocus=[] {focusClearSession();};
 fake::check(!refocusWebView(false),"refocus accepted cleared session after MoveFocus");
 fake::check(!g_focusSession.active&&!g_focusSession.lightForegroundAcquired,"MoveFocus callback revived cleared session");
 startLight();fake::onMoveFocus=[] {fake::foreground=fake::window(55);};
 fake::check(!refocusWebView(false)&&fake::foreground==fake::window(55),"refocus accepted foreground departure during MoveFocus");
 startLight();const auto proofMoves=fake::moveFocusCalls;fake::onBringTop=[] {++g_msiMainSourceRevision;};
 fake::check(!refocusWebView(false)&&fake::moveFocusCalls==proofMoves,"refocus continued after proof revocation");
 startLight();const auto foregroundMoves=fake::moveFocusCalls;fake::onBringTop=[] {fake::foreground=fake::window(55);};
 fake::check(!refocusWebView(false)&&fake::foreground==fake::window(55)&&fake::moveFocusCalls==foregroundMoves,"refocus stole third-party foreground during callback");
 startLight();const auto activeMoves=fake::moveFocusCalls;fake::onActive=[] {focusClearSession();};
 fake::check(!refocusWebView(false)&&fake::moveFocusCalls==activeMoves,"refocus continued after active-window session clear");
 startLight();fake::onMoveFocus=[] {++g_webviewGeneration;};
 fake::check(!refocusWebView(false),"refocus accepted retired WebView generation after MoveFocus");
 for(unsigned revoked=0;revoked<4;++revoked){
  reset();publishProof();fake::integrated=true;
  fake::onShow=[revoked]{
   if(revoked==0)++g_inputHostVirtualIntentRevision;
   if(revoked==1)fixtureSharedPower::value.flags|=ymcc::sleeppower::kNotificationPending;
   if(revoked==2)fake::persona="steamdeck";
   if(revoked==3)++g_msiMainSourceRevision;
  };
  fake::check(nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true),"show cancellation changed summon result");
  fake::check(!summonEvent().value("skipGameRegistration",false)&&summonEvent().value("isolationMode","")!="light-no-game","revoked show published light skip-registration");
  fake::check(fake::registrationCalls==1&&!focusLightIsolationCurrent(),"revoked show skipped ordinary registration or retained light ticket");
 }
}
static void lightCancelledDuringBring(){
 reset();fake::integrated=true;publishProof();fake::onShow=[] {++g_inputHostEpochMirror;};
 nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true);
 fake::check(!g_focusSession.active&&g_focusSession.strictReturnOnly&&fake::refocusAttempts.empty(),"cancelled bring kept or focused stale light session");
 fake::check(!summonEvent().value("skipGameRegistration",false)&&fake::registrationCalls==1,"cancelled light selection bypassed ordinary registration");
}
static void refocusUserChoice(){
 startLight();fake::foreground=fake::window(55);fake::check(!focusLightMayRefocus(fake::now),"acquired focus stole user choice");
 focusLightForegroundChanged();fake::check(!g_focusSession.active&&!g_focusLightIsolationTicket.load()&&g_focusSession.strictReturnOnly,"foreground departure failed to cancel light session");
 reset();fake::foreground=fake::window(55);nudgeWebViewAfterResume(false);
 fake::check(fake::activeCalls==0&&fake::moveFocusCalls==0&&fake::foreground==fake::window(55),"background visible nudge attempted focus");
 fake::check(fake::controllerVisibility==std::vector<BOOL>{TRUE}&&fake::boundsCalls==1,"background nudge lost nonfocus compositor refresh");
 fake::foreground=g_hwnd;nudgeWebViewAfterResume(false);
 fake::check(fake::activeCalls==1&&fake::moveFocusCalls==1,"foreground nudge did not move WebView focus");
 fake::onActive=[] {fake::foreground=fake::window(55);};nudgeWebViewAfterResume(false);
 fake::check(fake::moveFocusCalls==1&&fake::foreground==fake::window(55),"nudge moved focus after reentrant foreground departure");
 fake::foreground=g_hwnd;fake::onActive=[] {focusClearSession();};nudgeWebViewAfterResume(false);
 fake::check(fake::moveFocusCalls==1,"nudge moved focus after reentrant session clear");
}
static void refocusEntering(){
 startLight(false);fake::check(focusLightMayRefocus(fake::now),"origin denied bounded entering retry");
 fake::now=1079;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);fake::check(fake::refocusAttempts.size()==1,"entering retry bypassed 80ms spacing");
 fake::now=1080;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);fake::check(fake::refocusAttempts==std::vector<bool>{false,false},"light timer repeated explicit-entry Alt fallback");
 fake::foreground=nullptr;fake::check(focusLightMayRefocus(fake::now),"empty transient foreground denied entering retry");
 fake::foreground=fake::window(55);fake::check(!focusLightMayRefocus(fake::now),"entering retry stole third-party focus");
 focusLightForegroundChanged();fake::check(!g_focusSession.active,"entering foreground departure failed to cancel");
 startLight(false);fake::now=g_focusSession.summonDeadline;fake::check(!focusLightMayRefocus(fake::now),"summon deadline admitted retry");
 actualFocusTimer(SUMMON_FOCUS_TIMER_ID);fake::check(!g_focusSession.active&&fake::timers.empty(),"expired summon retained retry timer");
}
static void lightHandoff(){
 startLight(false);
 fake::check(g_focusLightHandoff&&fake::handoffJobs.size()==1&&g_focusSession.lightHandoffAttempted,"failed standard focus did not queue one bounded handoff");
 const auto first=g_focusLightHandoff;
 fake::now=1080;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::check(fake::handoffJobs.size()==1&&g_focusLightHandoff==first,"focus retry duplicated handoff worker");
 fake::refocusSucceeds=true;fake::handoffJobs.front()();const auto before=fake::steamUiChanges;
 focusLightRecheckOnUi();
 fake::check(!g_focusLightHandoff&&g_focusSession.lightForegroundAcquired&&fake::foreground==g_hwnd&&fake::steamUiChanges==before+1,"acknowledged handoff did not immediately arm Steam isolation");
 fake::check(fake::handoffExecutions==1&&fake::refocusAttempts.back()==false&&!fake::timers.contains(SUMMON_FOCUS_TIMER_ID),"handoff used Alt or retained focus retry");
 startLight(false);const auto cancelled=g_focusLightHandoff;focusClearSession();fake::handoffJobs.front()();focusLightRecheckOnUi();
 fake::check(ymcc::lightforeground::signalled(cancelled->cancellation)&&!g_focusSession.active&&fake::foreground!=g_hwnd&&!g_focusLightHandoff,"hidden light session retained or revived handoff");
 startLight(false);const auto returningPending=g_focusLightHandoff;actualFocusBeginReturnToPreviousWindow(fake::now);fake::handoffJobs.front()();
 focusLightHandoffCompleteOnUi();
 fake::check(ymcc::lightforeground::signalled(returningPending->cancellation)&&fake::foreground!=g_hwnd&&!g_focusLightHandoff,"return failed to cancel foreground handoff");
 startLight(false);fake::foreground=fake::window(55);focusLightForegroundChanged();fake::handoffJobs.front()();
 fake::check(fake::foreground==fake::window(55)&&!g_focusSession.active,"handoff stole user-selected foreground");
 startLight(false);fake::refocusSucceeds=true;fake::handoffJobs.front()();++g_focusSessionSequence;const auto staleAttempts=fake::refocusAttempts.size();
 focusLightHandoffCompleteOnUi();
 fake::check(fake::refocusAttempts.size()==staleAttempts&&!g_focusSession.lightForegroundAcquired,"stale handoff session acquired focus");
 startLight(false);fake::refocusSucceeds=true;fake::handoffJobs.front()();++g_msiMainSourceRevision;const auto sourceAttempts=fake::refocusAttempts.size();
 focusLightRecheckOnUi();
 fake::check(fake::refocusAttempts.size()==sourceAttempts&&!g_focusSession.active&&!g_focusLightHandoff,"revoked source accepted completed handoff");
 startLight(false);fake::refocusSucceeds=true;fake::handoffJobs.front()();fake::now=g_focusSession.summonDeadline;const auto expiredAttempts=fake::refocusAttempts.size();
 focusLightHandoffCompleteOnUi();
 fake::check(fake::refocusAttempts.size()==expiredAttempts&&!g_focusSession.lightForegroundAcquired,"expired handoff reply acquired focus");
 startLight(false);fake::refocusSucceeds=true;fake::handoffJobs.front()();g_focusLightHandoff->result.reclaimed=false;const auto unreclaimedAttempts=fake::refocusAttempts.size();
 focusLightHandoffCompleteOnUi();
 fake::check(fake::refocusAttempts.size()==unreclaimedAttempts&&!g_focusSession.lightForegroundAcquired,"unreclaimed handoff was accepted as complete");
 fake::check(g_focusLightHandoff&&g_focusLightHandoff->reported&&!g_focusLightHandoff->result.reclaimed&&fake::moveFocusCalls==0,"unreclaimed handoff lost retained responsibility or moved focus");
 startLight(false);const auto normalAcquire=g_focusLightHandoff;fake::refocusSucceeds=true;fake::now=1080;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::handoffJobs.front()();focusLightRecheckOnUi();
 fake::check(ymcc::lightforeground::signalled(normalAcquire->cancellation)&&g_focusSession.lightForegroundAcquired&&fake::foreground==g_hwnd,"normal focus completion left competing handoff alive");
 reset();fake::integrated=true;fake::acceptHandoff=false;fake::refocusSucceeds=false;publishProof();
 nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true);
 fake::check(g_focusSession.active&&g_focusSession.lightHandoffAttempted&&!g_focusLightHandoff&&fake::timers.contains(SUMMON_FOCUS_TIMER_ID),"queue rejection lost bounded standard retry");
 reset();fake::integrated=true;fake::refocusSucceeds=false;
 nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true);
 fake::check(!g_focusSession.lightNoGame&&fake::handoffJobs.empty()&&fake::refocusAttempts==std::vector<bool>{true},"original isolation entered light handoff or lost original compatibility");
 startLight(false);fake::refocusSucceeds=true;fake::handoffJobs.front()();g_focusLightHandoff->result.foreground=false;
 focusLightHandoffCompleteOnUi();
 fake::check(g_focusSession.lightForegroundAcquired&&fake::moveFocusCalls==1&&!g_focusLightHandoff&&!fake::timers.contains(SUMMON_FOCUS_TIMER_ID),"current foreground handoff rejected stale worker snapshot");
 startLight(false);fake::refocusSucceeds=true;fake::handoffJobs.front()();fake::moveFocusResult=E_FAIL;fake::killTimer(g_hwnd,SUMMON_FOCUS_TIMER_ID);
 fake::now=1024;
 focusLightHandoffCompleteOnUi();
 fake::check(!g_focusSession.lightForegroundAcquired&&fake::timers.contains(SUMMON_FOCUS_TIMER_ID)&&fake::timers.at(SUMMON_FOCUS_TIMER_ID)==40,"failed handoff postponed the first light retry");
 fake::moveFocusResult=S_OK;fake::now=1063;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::check(!g_focusSession.lightForegroundAcquired&&fake::moveFocusCalls==1,"handoff bypassed the first retry deadline");
 fake::now=1064;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::check(g_focusSession.lightForegroundAcquired&&!fake::timers.contains(SUMMON_FOCUS_TIMER_ID),"handoff MoveFocus retry did not recover");
 startLight(false);fake::refocusSucceeds=true;fake::handoffJobs.front()();fake::moveFocusResult=E_FAIL;fake::now=1069;
 focusLightHandoffCompleteOnUi();
 fake::check(fake::timers.at(SUMMON_FOCUS_TIMER_ID)==1,"overdue first light retry was postponed");
 fake::now=1070;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::check(!g_focusSession.lightForegroundAcquired&&fake::timers.at(SUMMON_FOCUS_TIMER_ID)==80,"early rearm changed later light retry cadence");
 startLight(false);fake::refocusSucceeds=true;fake::handoffJobs.front()();const auto thirdPartyMoves=fake::moveFocusCalls;fake::foreground=fake::window(55);
 focusLightHandoffCompleteOnUi();
 fake::check(fake::moveFocusCalls==thirdPartyMoves&&!g_focusSession.lightForegroundAcquired&&fake::foreground==fake::window(55),"completed handoff stole third-party foreground");
 // Replay the Win5 boundary: physical preview updates while foreground was
 // rejected. Only the existing explicit summon may recover through one Alt.
 const auto rejected=[](const char* reason="foreground-api-not-accepted",bool noReturnTarget=false,bool normalizedReturn=false){
  reset();fake::integrated=true;fake::refocusSucceeds=false;fake::returnTargetRejected=noReturnTarget;fake::returnTargetRootOwner=normalizedReturn;publishProof();
  fake::check(nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true),"light summon refused valid window");
  fake::check(g_focusSession.lightNoGame&&g_focusSession.active,"qualified summon failed to select light session");
  const auto pending=g_focusLightHandoff;
  pending->result={true,false,true,ERROR_ACCESS_DENIED,0,reason};pending->completed=true;
  fake::focusAfterAlt=true;return pending;
 };
 for(const char* reason:{"foreground-api-not-accepted","foreground-not-observed","foreground-queue-unconfirmed","deadline"}){
  const auto pending=rejected(reason);focusLightHandoffCompleteOnUi();
  fake::check(fake::altFallbackCalls==1&&g_summonAltTried&&fake::foreground==g_hwnd&&g_focusSession.lightForegroundAcquired&&fake::moveFocusCalls==1,"rejected foreground had no bounded keyboard recovery");
  fake::check(!g_focusLightHandoff&&!fake::timers.contains(SUMMON_FOCUS_TIMER_ID)&&pending->reported,"keyboard recovery retained a worker or timer");
  focusLightHandoffCompleteOnUi();fake::now=1080;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
  fake::check(fake::altFallbackCalls==1,"completed keyboard recovery repeated input");
 }
 for(unsigned counterexample=0;counterexample<22;++counterexample){
  const auto pending=rejected();
  switch(counterexample){
   case 0:fake::now=g_focusSession.summonDeadline;break;
   case 1:fake::foreground=fake::window(55);break;
   case 2:++g_msiMainSourceRevision;break;
   case 3:g_focusSession.returning=true;break;
   case 4:pending->result.reclaimed=false;break;
   case 5:pending->reported=true;break;
   case 6:pending->result.attempted=false;break;
   case 7:pending->result.reason="cancelled";break;
   case 8:fake::keyboardReadable=false;break;
   case 9:fake::queuedModifier=VK_LMENU;break;
   case 10:fake::asyncModifier=VK_RCONTROL;break;
   case 11:++fake::created;break;
   case 12:++fake::identityPid;break;
   case 13:fake::visibleWindow=false;break;
   case 14:fake::iconicWindow=true;break;
   case 15:fake::taskViewPaused=true;break;
   case 16:g_summonAltTried=true;break;
   case 17:fake::isolationSettingsOn=false;break;
   case 18:g_webviewReady=false;break;
   case 19:g_exitRequested=true;break;
   case 20:++g_focusSessionSequence;break;
   case 21:pending->result.reason="detach-failed";break;
  }
  focusLightHandoffCompleteOnUi();
  fake::check(fake::altFallbackCalls==0&&!g_focusSession.lightForegroundAcquired,"unsafe keyboard recovery crossed a summon boundary");
 }
 for(unsigned race=0;race<3;++race){
  rejected();fake::onKeyboardRead=[race]{if(race==0)fake::foreground=fake::window(55);else if(race==1)focusClearSession();else fake::taskViewPaused=true;};
  focusLightHandoffCompleteOnUi();
  fake::check(fake::altFallbackCalls==0&&!g_focusSession.lightForegroundAcquired,"keyboard state read raced past summon cancellation");
 }
 rejected();fake::onAlt=[] {fake::foreground=fake::window(55);};focusLightHandoffCompleteOnUi();
 fake::check(fake::foreground==fake::window(55)&&!g_focusSession.lightForegroundAcquired&&fake::moveFocusCalls==0,"keyboard fallback stole a reentrant user choice");
 for(UINT inserted:{0u,1u}){
  rejected();fake::firstAltInserted=inserted;focusLightHandoffCompleteOnUi();
  fake::check(fake::altRequestCounts==(inserted==1?std::vector<UINT>{2,1}:std::vector<UINT>{2})&&!g_focusSession.lightForegroundAcquired&&fake::moveFocusCalls==0,"partial keyboard recovery did not release Alt or authorized navigation");
 }
 rejected();fake::moveFocusResult=E_FAIL;fake::now=1024;focusLightHandoffCompleteOnUi();
 fake::check(!g_focusSession.lightForegroundAcquired&&fake::timers.at(SUMMON_FOCUS_TIMER_ID)==40&&fake::altFallbackCalls==1,"keyboard recovery postponed the first light retry");
 fake::moveFocusResult=S_OK;fake::now=1064;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::check(g_focusSession.lightForegroundAcquired&&fake::altFallbackCalls==1&&!fake::timers.contains(SUMMON_FOCUS_TIMER_ID),"keyboard recovery repeated Alt or lost WebView completion");
 for(unsigned variant=0;variant<2;++variant){
  rejected("foreground-api-not-accepted",variant==0,variant==1);
  const auto returnTarget=g_focusSession.target;focusLightHandoffCompleteOnUi();
  fake::check(fake::altFallbackCalls==1&&g_focusSession.lightForegroundAcquired&&fake::foreground==g_hwnd,"filtered or normalized return target blocked exact-origin summon");
  fake::check(g_focusSession.target.valid==returnTarget.valid&&g_focusSession.target.hwnd==returnTarget.hwnd,"foreground recovery changed return-target policy");
  bool dispatched=false,complete=false;
  for(const auto& row:fake::lifecycleRows){
   if(row.value("reason","")=="keyboard-fallback-dispatched")dispatched=row.value("attempted",false)&&row.value("error",0)==2;
   if(row.value("reason","")=="keyboard-fallback-complete")complete=!row.contains("attempted");
  }
  fake::check(dispatched&&complete,"keyboard completion log confused requested recovery with actual dispatch");
 }
 for(unsigned originFault=0;originFault<6;++originFault){
  const auto pending=rejected("foreground-api-not-accepted",true);
  switch(originFault){
   case 0:pending->request.originCreated=0;break;
   case 1:++pending->request.originCreated;break;
   case 2:++pending->request.originPid;break;
   case 3:pending->request.target=fake::window(99);break;
   case 4:++pending->request.parentPid;break;
   case 5:pending->request.origin=fake::window(55);break;
  }
  focusLightHandoffCompleteOnUi();
  fake::check(fake::altFallbackCalls==0&&!g_focusSession.lightForegroundAcquired,"exact-origin fallback accepted an invalid handoff identity");
 }
 rejected();refocusWebView(true);
 fake::check(fake::altFallbackCalls==0&&!g_focusSession.lightForegroundAcquired,"light retry without worker origin injected input");
}
struct FakeForegroundWorker : WindowsSettlePort {
 HWND observedForeground=request.origin;
 DWORD cancellationState=WAIT_TIMEOUT,targetThread=2,capturedTargetThread=2;
 ULONGLONG parentCreated=request.parentCreated,originCreated=request.originCreated;
 bool apiAccepted=true,observeOnSettle=true,attached=false;
 unsigned setCalls=0,settleCalls=0;
 std::vector<std::string> trace;
 std::function<void()> duringSettle;
 FakeForegroundWorker(){onSend=[this]{if(observeOnSettle)observedForeground=request.target;if(duringSettle)duringSettle();};}
 DWORD cancelState()const{return cancellationState;}
 bool parentMatches()const{return parentCreated==request.parentCreated;}
 bool targetMatches()const{return targetThread==capturedTargetThread;}
 bool originMatches()const{return originCreated==request.originCreated;}
 HWND foreground()const{return observedForeground;}
 void queue(){trace.push_back("queue");}
 bool attach(bool connect){trace.push_back(connect?"attach":"detach");attached=connect;return true;}
 bool setForeground(){++setCalls;trace.push_back("set");return apiAccepted;}
 bool settleForeground(){++settleCalls;trace.push_back("settle");fake::check(!attached,"foreground settlement retained input attachment");return WindowsSettlePort::settleForeground();}
};
static void lightHandoffLifecycle(){
 using ymcc::lightforeground::detail::Code;
 using ymcc::lightforeground::detail::handoff;
 using ymcc::lightforeground::detail::reason;
 using ymcc::lightforeground::detail::error;
 FakeForegroundWorker delayed;
 fake::check(handoff(delayed)==Code::foreground,"accepted foreground queue was sampled before settlement");
 fake::check(delayed.trace==std::vector<std::string>{"queue","attach","set","detach","settle"}&&delayed.setCalls==1&&delayed.settleCalls==1&&delayed.sendCalls==1,"foreground worker repeated or reordered handoff");
 for(const ULONGLONG remaining:std::array<ULONGLONG,6>{250,51,50,49,1,0}){
  WindowsSettlePort port;port.request.deadline=port.currentTime+remaining;
  fake::check(port.settleForeground()==(remaining!=0),"foreground settlement deadline result changed");
  fake::check(port.sendCalls==(remaining?1u:0u),"foreground settlement sent outside deadline or repeated");
  if(remaining)fake::check(port.sentTarget==port.request.target&&port.sentMessage==WM_NULL&&port.sentWparam==0&&port.sentLparam==0&&port.sentFlags==(SMTO_ABORTIFHUNG|SMTO_BLOCK)&&port.sentTimeout==(std::min)(remaining,ULONGLONG{50})&&port.replyProvided,"foreground settlement message or remaining timeout changed");
 }
 WindowsSettlePort overdue;overdue.request.deadline=overdue.currentTime-1;
 fake::check(!overdue.settleForeground()&&overdue.sendCalls==0,"expired foreground settlement sent a message");
 FakeForegroundWorker rejected;rejected.apiAccepted=false;
 fake::check(handoff(rejected)==Code::foregroundApiRejected&&rejected.setCalls==1&&rejected.settleCalls==0&&rejected.sendCalls==0&&rejected.trace==std::vector<std::string>{"queue","attach","set","detach"},"rejected foreground API entered settlement");
 FakeForegroundWorker unobserved;unobserved.observeOnSettle=false;
 fake::check(handoff(unobserved)==Code::foregroundDenied&&unobserved.setCalls==1&&unobserved.settleCalls==1&&unobserved.sendCalls==1,"settled queue without foreground was accepted");
 FakeForegroundWorker timeout;timeout.observeOnSettle=false;timeout.deliverySucceeded=false;
 fake::check(handoff(timeout)==Code::foregroundQueueUnconfirmed&&timeout.setCalls==1&&timeout.settleCalls==1&&timeout.sendCalls==1,"unconfirmed queue lost distinct failure");
 fake::check(std::string(reason(Code::foregroundApiRejected))=="foreground-api-not-accepted"&&error(Code::foregroundApiRejected)==ERROR_ACCESS_DENIED,"foreground API rejection mapping changed");
 fake::check(std::string(reason(Code::foregroundDenied))=="foreground-not-observed"&&error(Code::foregroundDenied)==ERROR_NOT_READY,"foreground observation failure reused access denied");
 fake::check(std::string(reason(Code::foregroundQueueUnconfirmed))=="foreground-queue-unconfirmed"&&error(Code::foregroundQueueUnconfirmed)==ERROR_TIMEOUT,"foreground queue timeout mapping changed");
 const std::array<Code,6> invalidated={Code::cancelled,Code::expired,Code::foregroundChanged,Code::parentIdentity,Code::targetIdentity,Code::originIdentity};
 for(std::size_t invalidation=0;invalidation<invalidated.size();++invalidation){
  FakeForegroundWorker stale;
  stale.duringSettle=[&]{switch(invalidation){case 0:stale.cancellationState=WAIT_OBJECT_0;break;case 1:stale.currentTime=stale.request.deadline;break;case 2:stale.observedForeground=fake::window(55);break;case 3:++stale.parentCreated;break;case 4:++stale.targetThread;break;case 5:++stale.originCreated;break;}};
  const auto outcome=handoff(stale);
  fake::check(outcome==invalidated[invalidation],invalidation==0?"settlement cancellation accepted foreground":"settlement accepted expired, foreign or replaced identity");
  fake::check(stale.setCalls==1&&stale.settleCalls==1&&stale.sendCalls==1&&!stale.attached,"settlement invalidation repeated or retained handoff");
 }
 startLight(false);const auto old=g_focusLightHandoff;
 focusClearSession();
 fake::check(g_focusLightHandoff==old&&ymcc::lightforeground::signalled(old->cancellation)&&!old->completed.load(),"cancel lost unretired handoff responsibility");
 fake::foreground=fake::window(44);publishProof();
 nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true);
 fake::check(g_focusLightHandoff==old&&fake::handoffJobs.size()==1&&!g_focusSession.lightHandoffAttempted,"new summon replaced an unretired handoff");
 fake::handoffJobs.front()();focusLightHandoffCompleteOnUi();
 fake::check(!g_focusLightHandoff&&!g_focusSession.lightForegroundAcquired,"cancelled old worker revived current focus");
 fake::now=1080;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::check(g_focusLightHandoff&&g_focusLightHandoff!=old&&fake::handoffJobs.size()==2,"new session failed to resume after confirmed old retirement");
 startLight(false);const auto dropped=g_focusLightHandoff;focusClearSession();fake::handoffJobs.clear();
 fake::check(dropped->completed.load()&&dropped->result.reclaimed&&!dropped->result.attempted,"discarded pool task retained unfinished handoff");
 focusLightHandoffCompleteOnUi();fake::check(!g_focusLightHandoff,"discarded pool task was not retired by original UI lane");
 startLight(false);const auto uncertain=g_focusLightHandoff;fake::handoffJobs.front()();uncertain->result.reclaimed=false;
 focusLightHandoffCompleteOnUi();focusClearSession();fake::foreground=fake::window(44);publishProof();
 nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true);
 fake::check(g_focusLightHandoff==uncertain&&fake::handoffJobs.size()==1&&!g_focusSession.lightForegroundAcquired,"unconfirmed retirement allowed replacement handoff");
 startLight(false);const auto virtualCancelled=g_focusLightHandoff;
 fake::persona="steamdeck";
 fake::check(!focusLightIsolationCurrent(),"virtual ON retained a queued light handoff ticket");
 focusLightRecheckOnUi();
 fake::check(ymcc::lightforeground::signalled(virtualCancelled->cancellation)&&!g_focusSession.active,"virtual ON did not cancel the old focus owner");
 fake::handoffJobs.front()();focusLightHandoffCompleteOnUi();
 fake::check(!g_focusLightHandoff&&!g_focusSession.lightForegroundAcquired&&fake::foreground!=g_hwnd,"late handoff acquired focus after virtual ON");
 reset();fake::integrated=true;fake::refocusSucceeds=false;fake::throwHandoffQueue=true;publishProof();
 nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true);
 fake::check(g_focusLightHandoff&&g_focusLightHandoff->completed.load()&&g_focusLightHandoff->result.reclaimed&&g_focusSession.active,"queue exception escaped or lost unstarted task retirement");
 focusLightHandoffCompleteOnUi();fake::check(!g_focusLightHandoff,"queue exception retained an unstarted handoff");
}
static void lightHandoffAdmission(){
 for(unsigned invalidation=0;invalidation<5;++invalidation){
  startLight(false);const auto pending=g_focusLightHandoff;
  switch(invalidation){case 0:++g_msiMainSourceRevision;break;case 1:g_exitRequested=true;break;case 2:++fixturePower;break;case 3:++g_inputHostEpochMirror;break;case 4:g_controllerFrontendIsolationEnabled=false;break;}
  fake::handoffJobs.front()();
  fake::check(fake::handoffExecutions==0&&fake::foreground!=g_hwnd&&pending->completed.load(),"queued stale handoff reached foreground worker");
 }
 startLight(false);const auto pending=g_focusLightHandoff;g_controllerFrontendIsolationEnabled=false;
 focusLightRecheckOnUi();
 fake::check(ymcc::lightforeground::signalled(pending->cancellation)&&g_focusLightHandoff==pending,"settings OFF failed to cancel active handoff");
 startLight(false);fake::handoffJobs.front()();g_controllerFrontendIsolationEnabled=false;const auto attempts=fake::refocusAttempts.size();
 focusLightRecheckOnUi();
 fake::check(fake::refocusAttempts.size()==attempts&&!g_focusSession.lightForegroundAcquired,"settings OFF accepted completed handoff");
 for(const bool resumeBeforeAction:{false,true}){
  startLight(false);const auto inFlight=g_focusLightHandoff;
  const auto sequence=g_focusSessionSequence;
  auto dispatch=ymcc::sleeppower::Dispatch([](void*) noexcept {fixturePowerRevocations.revoke();},nullptr);
  ymcc::lightforeground::beforeForegroundAction=[&]{
   const auto forward=[](const auto&){return true;};
   fake::check(dispatch.callback(PBT_APMSUSPEND,fake::now,forward)&&dispatch.pendingSuspend(),"fixture did not receive suspend before UI consume");
   if(resumeBeforeAction)fake::check(dispatch.callback(PBT_APMRESUMEAUTOMATIC,fake::now+1,forward),"fixture did not receive later resume");
   fake::check(g_focusSessionSequence==sequence&&g_focusSession.active&&!inFlight->completed.load(),"fixture consumed UI power before foreground action");
  };
  fake::handoffJobs.front()();
  fake::check(fake::handoffExecutions==1&&inFlight->completed.load()&&!inFlight->result.foreground&&
   fake::foreground!=g_hwnd&&ymcc::lightforeground::signalled(inFlight->cancellation),"notification ingress did not cancel an already running light handoff");
 }
}
static void lightSettingsOwner(){
 startLight(false);const auto pending=g_focusLightHandoff;const auto changes=fake::steamUiChanges,posts=fake::uiRecheckPosts;
 controllerFrontendIsolationRefreshSettings({{"enabled",false}});
 fake::check(!g_controllerFrontendIsolationEnabled&&fake::nativeSettingDrops==1,"settings OFF lost immediate atomic cancellation");
 fake::check(fake::steamUiChanges==changes&&fake::uiRecheckPosts==posts+1,"settings worker entered UI lease owner");
 focusLightRecheckOnUi();
 fake::check(ymcc::lightforeground::signalled(pending->cancellation)&&fake::steamUiChanges==changes+1,"original UI lane failed to apply settings revocation");
 controllerFrontendIsolationRefreshSettings({{"enabled",false}});
 fake::check(fake::uiRecheckPosts==posts+1,"unchanged settings queued duplicate UI work");
 controllerFrontendIsolationRefreshSettings({{"enabled",true}});
 fake::check(g_controllerFrontendIsolationEnabled&&fake::steamUiChanges==changes+1&&fake::uiRecheckPosts==posts+2,"settings ON bypassed UI owner");
}
static void lateReady(){
 startLight();fake::foreground=fake::window(55);const auto attempts=fake::refocusAttempts.size();
 actualLateReadyFocus();fake::check(fake::refocusAttempts.size()==attempts,"late Ready stole user choice");
 const auto oldSequence=g_focusSessionSequence;g_deferFirstShow=true;fake::visibleWindow=false;focusLightForegroundChanged();
 fake::check(!g_deferFirstShow,"acquired cancellation retained deferred first show");const auto shows=fake::showCalls;
 actualLateReadyFocus();fake::check(fake::showCalls==shows&&!fake::visibleWindow&&fake::refocusAttempts.size()==attempts&&!g_focusSession.active,"late Ready displayed or focused cancelled acquired session");
 fake::foreground=fake::window(44);nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true);const auto newAttempts=fake::refocusAttempts.size();
 actualLateReadyFocus(oldSequence);fake::check(fake::refocusAttempts.size()==newAttempts&&g_focusSession.active,"old Ready generation focused or cleared replacement session");
}
static void uiRecheck(){
 startLight(false);g_deferFirstShow=true;const auto leaseUpdates=fake::steamUiChanges;++g_inputHostEpochMirror;focusLightRecheckOnUi();
 fake::check(!g_focusSession.active&&!g_focusLightIsolationTicket.load()&&fake::timers.empty()&&!g_deferFirstShow,"UI recheck failed to revoke stale session and deferred show");
 fake::check(fake::ownerEvents==1&&fake::deviceWorkRequests==1&&fake::ownerEventAfterClear&&g_summonRegistrationGeneration==0,"UI revocation failed to notify original owner after clearing session");
 fake::check(fake::steamUiChanges==leaseUpdates+2&&!fake::steamUiLastTicketPresent,"UI revocation missed lease update or released before ticket clear");
 focusLightRecheckOnUi();fake::check(fake::ownerEvents==1&&fake::deviceWorkRequests==1&&fake::steamUiChanges==leaseUpdates+3,"duplicate UI recheck queued physical owner work or missed lease update");
}
static void lightUiFallbackReentry(){
 startLight();useActualUiInputGate=true;fake::recordNativeEligibility=true;
 fake::check(fake::foreground==g_hwnd&&gamepadUiInputEligible(),"fixture did not acquire actual YMCC foreground");
 ++g_inputHostEpochMirror;
 focusLightRecheckOnUi();
 fake::check(!g_focusSession.active&&fake::nativeDesired&&fake::deviceWorkRequests==1,"revoked light did not offer ordinary fallback once");
 focusLightRecheckOnUi();
 actualMainWindowOwnerEvent(WM_ACTIVATE,WA_ACTIVE);
 fake::check(!g_focusLightIsolationTicket.load()&&fake::nativeDesired&&fake::deviceWorkRequests==1,
  "unchanged no-game observation reopened light after ordinary fallback");
 auto republished=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());
 republished->generation=42;
 nativeValvePublish(*republished);
 focusLightRecheckOnUi();
 fake::check(!g_focusLightIsolationTicket.load()&&fake::nativeDesired&&fake::deviceWorkRequests==1,
  "republished no-game proof reopened light without a fresh scan");
 fixtureNativeVisibilityHistoryPending=true;
 ++fake::now;publishProof();focusLightRecheckOnUi();
 fake::check(!g_focusLightIsolationTicket.load()&&fake::nativeDesired&&fake::deviceWorkRequests==1,
  "fresh proof bypassed native visibility return debt");
 fixtureNativeVisibilityHistoryPending=false;
 focusLightRecheckOnUi();
 fake::check(focusLightIsolationCurrent()&&!fake::nativeDesired&&fake::deviceWorkRequests==2,
  "fresh eligible proof did not restore light after native visibility return");
 focusLightRecheckOnUi();
 fake::check(fake::deviceWorkRequests==2,"fresh light reentry repeated physical owner work");
 startLight();useActualUiInputGate=true;fake::exerciseSteamLease=true;fake::recordNativeEligibility=true;
 focusLightRecheckOnUi();
 fake::check(steamLightGamepadAttemptActive()&&g_steamLightGamepadIntent.load()->enabled,
  "fixture did not start the original Steam lease");
 ++g_inputHostVirtualIntentRevision;
 focusLightRecheckOnUi();focusLightRecheckOnUi();
 fake::check(fake::nativeDesired&&fake::deviceWorkRequests==1&&!g_focusLightIsolationTicket.load()&&
  !g_steamLightGamepadIntent.load()->enabled,"old proof reopened Steam after the virtual intent changed");
 fake::visibleWindow=false;
 actualMainWindowOwnerEvent(WM_SHOWWINDOW,FALSE);
 fake::visibleWindow=true;
 actualMainWindowOwnerEvent(WM_SHOWWINDOW,TRUE);
 fake::check(focusLightIsolationCurrent()&&!fake::nativeDesired,
  "new presentation could not independently admit a no-game session");
}
static void latePowerReady(){
 startLight(false);g_powerLifecycle=PowerLifecycle::Resuming;++fixturePower;
 fake::check(!focusLightIsolationCurrent(),"sleep retained ticket");g_powerLifecycle=PowerLifecycle::Ready;publishProof();
 const auto attempts=fake::refocusAttempts.size();actualLateReadyFocus();actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::check(fake::refocusAttempts.size()==attempts&&!g_focusSession.active&&!g_focusLightIsolationTicket.load(),"late power Ready revived old ticket");
}
static void isolationSharedPowerOrder(){
 using namespace ymcc::sleeppower;
 for(const bool light:{false,true}){
  if(light)startLight(false);
  else{
   reset();fake::integrated=true;publishProof();
   auto game=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());
   game->game.pid=777;g_gameValveObservation.store(game);
   fake::check(nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true)&&g_focusIsolationGameOpen,"game route did not latch before show");
  }
  const auto sequence=g_focusSessionSequence;
  const auto ticket=g_focusLightIsolationTicket.load();
  const auto cutoff=g_focusLightProofNotBefore.load();
  SharedPowerV1 original;
  fake::check(captureSharedPowerLifecycle(original),"fixture could not read the original shared power projection");
  auto previous=original;
  previous.phase=2;previous.flags=kKnown;
  const bool staleSettled=focusIsolationSharedPowerChanged(previous);
  fake::check(g_focusIsolationPrepared&&g_focusSession.active&&g_focusSessionSequence==sequence&&
   g_focusLightIsolationTicket.load()==ticket&&g_focusLightProofNotBefore.load()==cutoff,
   "superseded power observation cleared current isolation");
  fake::check(!staleSettled,"superseded power observation did not retain projection debt");
  auto current=original;
  ++current.suspendSerial;++current.observedNotificationSequence;current.phase=1;current.flags=kKnown;
  fixtureSharedPower::value=current;g_powerLifecycle=PowerLifecycle::Suspending;
  fake::check(focusIsolationSharedPowerChanged(current),"current suspend did not settle projection debt");
  fake::check(!g_focusIsolationPrepared&&!g_focusSession.active&&!g_focusLightIsolationTicket.load()&&
   g_focusSessionSequence==sequence+1&&g_focusLightProofNotBefore.load()==fake::now,
   "current suspend failed to revoke isolation");
  fake::check(focusIsolationSharedPowerChanged(current),"duplicate suspend recreated projection debt");
  fake::check(g_focusSessionSequence==sequence+1,"duplicate suspend cleared isolation twice");
 }
}
static void lightNoDemandPowerRead(){
 reset();
 const auto noProofReads=fake::uiGateReads;
 for(unsigned i=0;i<1024;++i) actualSteamLightGamepadUiChanged();
 fake::check(fake::uiGateReads==noProofReads&&!g_steamLightGamepadIntent.load()&&fake::steamLeaseJobs.empty(),
  "idle legacy lease queried output or queued work");
 fixtureUiProbeCanIdle=false;
 actualSteamLightGamepadUiChanged();
 fixtureUiProbeCanIdle=true;
 fake::check(fake::uiGateReads==noProofReads+1,"handoff cleanup was skipped");
 auto release=std::make_shared<SteamLightGamepadIntent>();
 release->serial=++g_steamLightGamepadSerial;release->session=g_focusSessionSequence;
 g_steamLightGamepadIntent.store(release);
 g_steamLightGamepadSettledSerial.store(release->serial);
 actualSteamLightGamepadUiChanged();
 fake::check(fake::uiGateReads==noProofReads+1&&g_steamLightGamepadIntent.load()==release,"settled OFF delivery was queried or republished");
 g_steamLightGamepadSettledSerial.store(0);
 actualSteamLightGamepadUiChanged();
 fake::check(fake::uiGateReads==noProofReads+2,"unsettled OFF delivery skipped the original probe");
 startLight();fake::exerciseSteamLease=true;focusLightRecheckOnUi();
 const auto lease=g_steamLightGamepadLeaseState.load();
 fake::check(lease&&steamLightGamepadAttemptActive(),"light attempt not active with current foreground");
 fake::uiAuthority=false;
 const auto reads=fixtureSharedPower::reads.load(std::memory_order_relaxed);
 fake::check(!steamLightGamepadAttemptActive()&&fixtureSharedPower::reads.load(std::memory_order_relaxed)==reads,
  "inactive output ran light qualification");
 fake::uiAuthority=true;
 fake::check(steamLightGamepadAttemptActive()&&g_steamLightGamepadLeaseState.load()==lease,
  "no-demand check changed the original lease");
}
static void isolationCoalescedReady(){
 using namespace ymcc::sleeppower;
 for(const bool newGeneration:{false,true})for(const unsigned route:{0u,1u,2u,3u}){
  reset();fake::integrated=true;publishProof();
  fixtureSharedPower::value.suspendSerial=20;g_sharedPowerSuspendSerial=20;
  fixtureSharedPower::value.publicationRevision=4;
  const bool gameOpen=route==0||route==3;
  if(gameOpen){
   auto game=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());
   game->game.pid=777;g_gameValveObservation.store(game);
  }
  if(route>=2)fixtureSharedPower::unavailableReads=1;
  fake::check(nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true)&&g_focusSession.active&&
   g_focusSession.lightNoGame==!gameOpen&&g_focusIsolationGameOpen==gameOpen,"fixture did not acquire its isolation route");
  fake::exerciseSteamLease=true;focusLightRecheckOnUi();
  if(!gameOpen)fake::check(g_steamLightGamepadIntent.load()&&g_steamLightGamepadIntent.load()->enabled,
   "fixture did not request Steam navigation isolation");
  SharedPowerV1 ready;
  fake::check(captureSharedPowerLifecycle(ready),"fixture failed to observe original Ready");
  const auto sequence=g_focusSessionSequence;
  const auto ticket=g_focusLightIsolationTicket.load();
  auto delayed=ready;--delayed.suspendSerial;
  focusIsolationSharedPowerChanged(delayed);
  ++ready.publicationRevision;fixtureSharedPower::value=ready;
  focusIsolationSharedPowerChanged(ready);
  fake::check(g_focusSessionSequence==sequence&&g_focusSession.active&&g_focusIsolationPrepared&&
   g_focusLightIsolationTicket.load()==ticket,"late or same-period Ready cleared active isolation");
  if(newGeneration)ready.generation=++fixturePower;
  else ready.suspendSerial=++g_sharedPowerSuspendSerial;
  ++ready.publicationRevision;++ready.observedNotificationSequence;
  fixtureSharedPower::value=ready;++fake::now;
  focusIsolationSharedPowerChanged(ready);
  fake::check(!g_focusIsolationPrepared&&!g_focusIsolationGameOpen&&!g_focusSession.active&&
   !g_focusLightIsolationTicket.load()&&g_focusSessionSequence==sequence+1&&g_focusLightProofNotBefore==fake::now,
   "coalesced Ready failed to retire isolation after unknown preflight or a new period");
  if(!gameOpen)fake::check(g_steamLightGamepadIntent.load()&&!g_steamLightGamepadIntent.load()->enabled,
   "coalesced Ready kept the old Steam ON intent");
  focusIsolationSharedPowerChanged(ready);
  fake::check(g_focusSessionSequence==sequence+1,"duplicate coalesced Ready retired isolation twice");
 }
}
static void lightStaleIntentFastRelease(){
 for(const bool steamAttempt:{false,true}){
  const auto current=steamAttempt?steamLightGamepadAttemptActive:focusLightIsolationCurrent;
  for(const bool changeDuringQualification:{false,true}){
   startLight();fake::exerciseSteamLease=true;focusLightRecheckOnUi();
   const auto lease=g_steamLightGamepadLeaseState.load();
   fake::check(lease&&current(),"current light owner was not admitted");
   const auto reads=fixtureSharedPower::reads.load(std::memory_order_relaxed);
   if(changeDuringQualification)fixtureSharedPower::afterRead=[] {++g_inputHostVirtualIntentRevision;};
   else ++g_inputHostVirtualIntentRevision;
   fake::check(!current(),steamAttempt?"stale Steam attempt delayed ordinary isolation":"stale ticket delayed ordinary isolation");
   fake::check(fixtureSharedPower::reads.load(std::memory_order_relaxed)-reads==(changeDuringQualification?1u:0u),
    steamAttempt?"already-stale Steam attempt repeated light qualification":"already-stale ticket repeated light qualification");
   fake::check(msiNativeForegroundUiEligible(),"stale light owner kept ordinary isolation ineligible");
   focusLightRecheckOnUi();
   fake::check(!g_focusLightIsolationTicket.load()&&!g_steamLightGamepadIntent.load()->enabled,
    "stale light owner retained ticket or Steam ON intent");
  }
 }
}
static void returning(){g_focusSession.returning=true;g_focusSession.returnStarted=fake::now;g_focusSession.returnDeadline=fake::now+800;g_focusSession.returnNextAt=0;}
static void returnIdentity(){
 startLight();returning();++fake::created;
 const bool complete=focusLightReturnTick(fake::now);fake::check(complete&&fake::focusAttempts.empty(),"reused process received return focus");
}
static void returnValidation(){
 for(unsigned counterexample=0;counterexample<12;++counterexample){
  startLight();returning();
  switch(counterexample){
   case 0:g_focusSession.target.valid=false;break;
   case 1:g_focusSession.target.hwnd=nullptr;break;
   case 2:g_focusSession.target.pid=0;break;
   case 3:g_focusSession.target.processCreated=0;break;
   case 4:g_focusSession.target.path.clear();break;
   case 5:fake::usable=false;break;
   case 6:fake::blocked=true;break;
   case 7:fake::paused=true;break;
   case 8:fake::identityPid=445;break;
   case 9:fake::identityPath=L"C:\\Apps\\other.exe";break;
   case 10:fake::identityClass=L"ReplacementWindow";break;
   case 11:g_focusSession.active=false;break;
  }
  fake::check(focusLightReturnTick(fake::now)&&fake::focusAttempts.empty(),"invalid return identity or blocked target received focus");
 }
 startLight();returning();fake::foreground=fake::window(55);fake::check(focusLightReturnTick(fake::now)&&fake::focusAttempts.empty(),"return stole third-party foreground");
 startLight();returning();++g_inputHostEpochMirror;fake::check(focusLightReturnTick(fake::now)&&fake::focusAttempts.empty(),"revoked ticket attempted return");
}
static void returnSuccess(){
 startLight();actualFocusBeginReturnToPreviousWindow(fake::now);
 fake::check(fake::focusAttempts==std::vector<HWND>{fake::window(44)}&&!g_focusSession.active&&fake::timers.empty(),"exact target return failed to complete and cancel");
 fake::check(g_focusSession.strictReturnOnly&&fake::legacyReturns==0,"light return entered remembered-game path");
 actualFocusBeginReturnToPreviousWindow(fake::now);fake::check(fake::legacyReturns==0&&fake::focusAttempts.size()==1,"cleared strict session fell through to legacy return");
}
static void returnRateAndOldCallbacks(){
 startLight();fake::returnSucceeds=false;actualFocusBeginReturnToPreviousWindow(fake::now);
 fake::check(g_focusSession.returning&&fake::timers[RETURN_GAME_FOCUS_TIMER_ID]==50&&fake::focusAttempts.size()==1,"failed return did not retain one bounded timer");
 fake::now=1049;actualFocusTimer(RETURN_GAME_FOCUS_TIMER_ID);fake::check(fake::focusAttempts.size()==1,"return retry bypassed 50ms spacing");
 fake::now=1050;actualFocusTimer(RETURN_GAME_FOCUS_TIMER_ID);fake::check(fake::focusAttempts.size()==2,"due return retry did not run");
 fake::now=1800;actualFocusTimer(RETURN_GAME_FOCUS_TIMER_ID);fake::check(!g_focusSession.active&&fake::timers.empty()&&fake::focusAttempts.size()==2,"800ms return deadline failed to cancel");
 const auto attempts=fake::refocusAttempts.size();actualFocusTimer(RETURN_GAME_FOCUS_TIMER_ID);actualFocusTimer(SUMMON_FOCUS_TIMER_ID);actualLateReadyFocus();
 fake::check(!g_focusSession.active&&fake::timers.empty()&&fake::focusAttempts.size()==2&&fake::refocusAttempts.size()==attempts,"old timers or Ready revived cleared session");
}
static void reentrantHide(){
 startLight();const auto sequence=g_focusSessionSequence;
 fake::onHide=[] {fake::onHide={};fake::foreground=fake::window(55);nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true);};
 actualFocusBeginReturnToPreviousWindow(fake::now);
 fake::check(g_focusSessionSequence>sequence&&g_focusSession.active&&!g_focusSession.returning&&g_focusSession.target.hwnd==fake::window(55),"old return completion cleared reentrant summon");
}
static void reentrantClear(){
 startLight();fake::onPosition=[] {fake::onPosition={};fake::foreground=fake::window(55);nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true);};
 focusClearSession();fake::check(g_focusSession.active&&g_focusSession.lightNoGame&&g_focusSession.target.hwnd==fake::window(55),"old clear overwrote reentrant summon");
}
static void reentrantWrapper(){
 reset();fake::integrated=true;publishProof();
 fake::onShow=[] {fake::onShow={};fake::foreground=fake::window(55);nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true);};
 nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true);unsigned events=0;
 for(const auto& event:fake::events)if(event.first=="gamepad.summon"){++events;fake::check(event.second["summonGeneration"]==2,"old wrapper emitted stale generation");}
 fake::check(events==1&&g_focusSession.active&&g_focusSession.target.hwnd==fake::window(55),"reentrant wrapper lost current summon");
}
static void staleRegistration(){
 reset();auto target=focusCaptureTarget(fake::foreground);g_summonRegistrationGeneration=12;
 fake::check(nativeAutoRegisterSummonGame(target,11)["valid"]==false&&fake::ruleWrites==0,"stale worker passed entry generation");
 fake::onMemory=[] {g_summonRegistrationGeneration=0;};nativeAutoRegisterSummonGame(target,12);
 fake::check(fake::ruleWrites==0&&fake::whitelist.empty(),"stale worker wrote whitelist");
 fake::onMemory={};g_summonRegistrationGeneration=13;const auto result=nativeAutoRegisterSummonGame(target,13);
 fake::check(result["autoAdded"]==true&&fake::ruleWrites==1&&g_gameRulesEpoch==2,"current registration failed to use original write path");
}
static void lightForegroundEntry(){
 const auto resetEntry=[] {reset();useActualUiInputGate=true;};
 resetEntry();publishProof();fake::foreground=g_hwnd;
 focusLightRecheckOnUi();
 fake::check(!msiNativeForegroundUiEligible(),"foreground-only entry requested physical isolation");
 fake::check(focusLightIsolationCurrent()&&!g_focusSession.active&&!g_focusSession.returning,"foreground-only entry failed to establish output-only proof");
 fake::check(fake::steamUiLastTicketPresent&&fake::focusAttempts.empty()&&fake::registrationCalls==0&&fake::handoffJobs.empty(),"foreground-only entry changed focus or registered a game");
 const auto ticket=g_focusLightIsolationTicket.load();focusLightRecheckOnUi();
 fake::check(ticket==g_focusLightIsolationTicket.load(),"foreground-only recheck replaced a current proof");
 fake::foreground=fake::window(44);focusLightRecheckOnUi();
 fake::check(!g_focusLightIsolationTicket.load()&&!fake::steamUiLastTicketPresent,"foreground-only focus loss retained proof");
 for(unsigned refusal=0;refusal<13;++refusal){
  resetEntry();publishProof();fake::foreground=g_hwnd;
  if(refusal==0)fake::visibleWindow=false;
  if(refusal==1)fake::isolationSettingsOn=false;
  if(refusal==2)fake::radialOwner=true;
  if(refusal==3)fake::childOwner=true;
  if(refusal==4){auto observation=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());observation->game.pid=123;g_gameValveObservation.store(observation);}
  if(refusal==5)g_focusSession.active=true;
  if(refusal==6)g_powerLifecycle=PowerLifecycle::Resuming;
  if(refusal==7)fake::iconicWindow=true;
  if(refusal==8)g_exitRequested=true;
  if(refusal==9)fake::valid=false;
  if(refusal==10)fake::now=31001;
  if(refusal==11){auto observation=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());observation->discoveryState=NativeGameDiscoveryState::Pending;g_gameValveObservation.store(observation);}
  if(refusal==12)fake::foreground=fake::window(44);
  focusLightRecheckOnUi();fake::check(!g_focusLightIsolationTicket.load(),"ineligible foreground-only entry created proof");
 }
 resetEntry();publishProof();fake::foreground=g_hwnd;focusLightRecheckOnUi();fake::isolationSettingsOn=false;focusLightRecheckOnUi();
 fake::check(!g_focusLightIsolationTicket.load()&&!fake::steamUiLastTicketPresent,"foreground-only settings OFF retained proof");
 resetEntry();publishProof();fake::foreground=g_hwnd;focusLightRecheckOnUi();fake::childOwner=true;focusLightRecheckOnUi();
 fake::check(!g_focusLightIsolationTicket.load()&&msiNativeForegroundUiEligible(),"foreground-only proof overrode independent child owner");
 for(unsigned entry=0;entry<3;++entry){
  resetEntry();publishProof();fake::foreground=g_hwnd;fake::recordNativeEligibility=true;
  if(entry==0)actualMainWindowOwnerEvent(WM_SHOWWINDOW,TRUE);
  if(entry==1)actualMainWindowOwnerEvent(WM_ACTIVATE,WA_ACTIVE);
  if(entry==2)actualForegroundObserved(nullptr,EVENT_SYSTEM_FOREGROUND,g_hwnd,0,0,0,0);
  fake::check(fake::ownerEvents==1,"foreground entry missed common owner notification");
  const char* failure=entry==0?"window show routed physical isolation before light admission":entry==1?"window activation routed physical isolation before light admission":"foreground observer routed physical isolation before light admission";
  fake::check(!fake::lastNativeEligibility&&focusLightIsolationCurrent(),failure);
  fake::check(fake::focusAttempts.empty()&&fake::registrationCalls==0&&fake::handoffJobs.empty(),"foreground event created a focus session or device handoff");
  fake::foreground=fake::window(44);
  if(entry==0){fake::visibleWindow=false;actualMainWindowOwnerEvent(WM_SHOWWINDOW,FALSE);}
  if(entry==1)actualMainWindowOwnerEvent(WM_ACTIVATE,WA_INACTIVE);
  if(entry==2)actualForegroundObserved(nullptr,EVENT_SYSTEM_FOREGROUND,fake::foreground,0,0,0,0);
  fake::check(!g_focusLightIsolationTicket.load()&&!fake::steamUiLastTicketPresent,"foreground event failed to revoke light lease proof");
 }
 for(const auto message:{WM_SHOWWINDOW,WM_ACTIVATE,EVENT_SYSTEM_FOREGROUND}){
  resetEntry();fake::foreground=g_hwnd;fake::recordNativeEligibility=true;
  if(message==EVENT_SYSTEM_FOREGROUND)actualForegroundObserved(nullptr,message,g_hwnd,0,0,0,0);
  else actualMainWindowOwnerEvent(message,message==WM_SHOWWINDOW?TRUE:WA_ACTIVE);
  fake::check(fake::nativeDesired&&fake::deviceWorkRequests==1,"ordinary presentation did not request isolation once");
  fake::check(fake::ownerEvents==1&&fake::uiGateReads==1,"ordinary presentation repeated isolation eligibility");
 }
}
static void runSteamLeaseJob(){
 fake::check(!fake::steamLeaseJobs.empty(),"Steam lease job was not queued");
 auto job=std::move(fake::steamLeaseJobs.front());fake::steamLeaseJobs.erase(fake::steamLeaseJobs.begin());job();
}
static void lightLeaseFallbackLifecycle(){
 for(unsigned entry=0;entry<4;++entry){
  reset();fake::exerciseSteamLease=true;fake::steamInstanceAvailable=false;fake::recordNativeEligibility=true;publishProof();fake::foreground=g_hwnd;
  if(entry==0){fake::integrated=true;nativeYmccShortcutSummon(focusCaptureTarget(fake::window(44)),true);focusLightRecheckOnUi();}
  if(entry==1)actualMainWindowOwnerEvent(WM_SHOWWINDOW,TRUE);
  if(entry==2)actualMainWindowOwnerEvent(WM_ACTIVATE,WA_ACTIVE);
  if(entry==3)actualForegroundObserved(nullptr,EVENT_SYSTEM_FOREGROUND,g_hwnd,0,0,0,0);
  fake::check(focusLightIsolationCurrent()&&!fake::nativeDesired,"unconfirmed first attempt did not prefer light isolation");
  runSteamLeaseJob();
  fake::check(!msiNativeForegroundUiEligible(),"failed Steam lease bypassed initial light attempt window");
  fake::now+=3000;
  fake::check(msiNativeForegroundUiEligible(),"failed Steam lease left native fallback exempt");
  const auto before=fake::deviceWorkRequests;focusLightRecheckOnUi();
  fake::check(fake::nativeDesired&&fake::deviceWorkRequests==before+1,"active light failure did not reach native owner");
  focusLightRecheckOnUi();fake::check(fake::deviceWorkRequests==before+1,"fallback recheck repeated physical work");
  if(!fake::steamLeaseJobs.empty())runSteamLeaseJob();
  fake::now+=1000;fake::steamInstanceAvailable=true;focusLightRecheckOnUi();
  fake::check(fake::nativeDesired&&std::none_of(fake::steamLeaseRequests.begin(),fake::steamLeaseRequests.end(),[](const json& request){return request.at("enabled")==true;}),"late Steam recovery removed current visible fallback");
  ++g_msiMainSourceRevision;focusLightRecheckOnUi();focusLightRecheckOnUi();
  fake::check(fake::nativeDesired&&steamLightGamepadFallbackRequired(),"native source change reopened failed light route");
  fake::visibleWindow=false;fake::uiAuthority=false;actualMainWindowOwnerEvent(WM_SHOWWINDOW,FALSE);
  fake::check(!fake::nativeDesired&&!steamLightGamepadFallbackRequired(),"hide retained native fallback ownership");
  fake::visibleWindow=true;fake::uiAuthority=true;fake::foreground=g_hwnd;publishProof();focusLightRecheckOnUi();runSteamLeaseJob();
  fake::check(!fake::nativeDesired&&!steamLightGamepadFallbackRequired()&&fake::steamLeaseRequests.back().at("enabled")==true,"new visible lifecycle did not retry healthy light route");
 }
 for(unsigned fault=0;fault<5;++fault){
  reset();startLight();fake::exerciseSteamLease=true;fake::recordNativeEligibility=true;
  if(fault==0)fake::acceptSteamLeaseQueue=false;
  focusLightRecheckOnUi();
  if(fault==0)fake::now+=3000;
  if(fault==1){fake::steamLeaseJobs.clear();fake::now+=3000;}
  if(fault>=2){
   runSteamLeaseJob();fake::check(!msiNativeForegroundUiEligible(),"healthy light ACK triggered native fallback");
   fake::now+=750;focusLightRecheckOnUi();
   if(fault==2)fake::steamLeaseOverride={{"ok",false},{"reason","live-busy"}};
   if(fault==3)fake::onSteamLeaseRequest=[] {fake::now=4000;};
   if(fault==4){fake::steamLeaseJobs.clear();fake::now=4000;}
   if(fault!=4)runSteamLeaseJob();
   if(fault==2)fake::now=4000;
  }
  focusLightRecheckOnUi();
  fake::check(fake::nativeDesired&&steamLightGamepadFallbackRequired(),"queue failure or expired renewal failed to fall back");
  fake::isolationSettingsOn=false;focusLightRecheckOnUi();fake::check(!fake::nativeDesired,"settings OFF retained fallback native ownership");
 }
 for(unsigned invalid=0;invalid<3;++invalid){
  reset();startLight();fake::exerciseSteamLease=true;fake::recordNativeEligibility=true;focusLightRecheckOnUi();
  fake::steamLeaseOverride={{"ok",true},{"enabled",true},{"serial",g_steamLightGamepadIntent.load()->serial},{"remainingMs",2500},{"scope","steam-gamepad-navigation"}};
  if(invalid==0)fake::steamLeaseOverride["serial"]=999;
  if(invalid==1)fake::steamLeaseOverride["remainingMs"]=0;
  if(invalid==2)fake::steamLeaseOverride["scope"]="wrong-scope";
  runSteamLeaseJob();fake::now+=3000;focusLightRecheckOnUi();fake::check(fake::nativeDesired,"invalid receipt waived native fallback");
 }
 for(unsigned change=0;change<4;++change){
  reset();startLight();fake::exerciseSteamLease=true;fake::recordNativeEligibility=true;focusLightRecheckOnUi();
  fake::onSteamLeaseRequest=[change] {
   if(change==0){fake::visibleWindow=false;fake::uiAuthority=false;actualMainWindowOwnerEvent(WM_SHOWWINDOW,FALSE);}
   if(change==1)fake::isolationSettingsOn=false;
   if(change==2){fake::foreground=fake::window(55);fake::uiAuthority=false;}
   if(change==3){g_powerLifecycle=PowerLifecycle::Resuming;++fixturePower;}
  };
  runSteamLeaseJob();focusLightRecheckOnUi();
  fake::check(!steamLightGamepadFallbackRequired(),"late obsolete ACK revived fallback state");
  fake::check(!g_steamLightGamepadIntent.load()->enabled,"revoked lifecycle retained Steam ON intent");
 }
 for(const bool replaceOwner:{false,true}){
  startLight();fake::exerciseSteamLease=true;focusLightRecheckOnUi();
  const auto original=g_steamLightGamepadIntent.load();
  fake::now+=600;
  fake::onSteamLeaseRequest=[&] {
   fake::now+=200;
   if(replaceOwner){fake::visibleWindow=false;fake::uiAuthority=false;actualMainWindowOwnerEvent(WM_SHOWWINDOW,FALSE);}
   else focusLightRecheckOnUi();
  };
  runSteamLeaseJob();
  const auto current=g_steamLightGamepadIntent.load();
  if(replaceOwner){
   fake::check(current&&!current->enabled&&current!=original&&!original->lease->acknowledgedDeadline.load()&&
       fake::steamLeaseJobs.size()==1,"in-flight acquisition held off a required release");
   runSteamLeaseJob();
   fake::check(g_steamLightGamepadSettledSerial.load()==current->serial&&fake::steamLeaseJobs.empty(),"queued release did not settle after revoked acquisition");
  }else{
   fake::check(current==original&&original->lease->acknowledgedDeadline.load()==original->deadline&&
       fake::steamLeaseJobs.empty(),"unchanged light renewal cancelled a timely in-flight acknowledgment");
   focusLightRecheckOnUi();
   const auto renewal=g_steamLightGamepadIntent.load();
   fake::check(renewal!=original&&renewal->enabled&&renewal->lease==original->lease&&
       fake::steamLeaseJobs.size()==1,"completed light acquisition did not allow one due renewal");
   runSteamLeaseJob();
  }
 }
}
static void lightLeaseStorm(){
 unsigned episodes=0,steps=0;
 for(unsigned variant=0;variant<8;++variant)for(unsigned cycle=0;cycle<64;++cycle){
  reset();fake::exerciseSteamLease=true;fake::recordNativeEligibility=true;fake::persona="disabled";
  publishProof();
  fake::foreground=g_hwnd;actualMainWindowOwnerEvent(WM_SHOWWINDOW,TRUE);
  fake::check(focusLightIsolationCurrent()&&!fake::nativeDesired,"storm entry did not prefer light route");
  const auto firstDeadline=g_steamLightGamepadLeaseState.load()->initialDeadline;
  for(unsigned burst=0;burst<32;++burst){
   ++steps;focusLightRecheckOnUi();
   fake::check(fake::steamLeaseJobs.size()<=1&&!fake::nativeDesired,"rapid summon storm accumulated work or native borrowing");
  }
  runSteamLeaseJob();
  for(unsigned burst=0;burst<8;++burst){
   const auto previousLease=g_steamLightGamepadLeaseState.load();
   const auto inheritedDeadline=(std::max)(previousLease->initialDeadline,previousLease->acknowledgedDeadline.load());
   ++steps;fake::now+=100;++g_inputHostEpochMirror;++g_xinputTopologyEpoch;
   focusLightRecheckOnUi();focusLightRecheckOnUi();
   fake::check(g_steamLightGamepadLeaseState.load()->initialDeadline==inheritedDeadline,"virtual/source epoch churn extended the original three-second attempt");
   fake::check(!fake::nativeDesired&&fake::steamLeaseJobs.size()<=1,"virtual/source storm escaped light grace or multiplied jobs");
   if(!fake::steamLeaseJobs.empty())runSteamLeaseJob();
  }
  if(variant>=4){
   auto game=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());game->game.pid=777;g_gameValveObservation.store(game);
   focusLightRecheckOnUi();
   fake::check(fake::nativeDesired&&!focusLightIsolationCurrent(),"game launch retained no-game light grace");
   publishProof();focusLightRecheckOnUi();fake::check(fake::nativeDesired&&!focusLightNoGameQualified(fake::now),"foreground handoff erased the game isolation latch");
  }
  if(variant<4&&variant%4==0){
   fake::now=firstDeadline-1;focusLightRecheckOnUi();
   fake::check(!fake::nativeDesired,"native fallback started before the three-second boundary");
   fake::now=firstDeadline;focusLightRecheckOnUi();
  }
  if(variant<4&&variant%4==1){
   for(unsigned renewal=0;renewal<12;++renewal){
    ++steps;fake::now+=750;focusLightRecheckOnUi();if(!fake::steamLeaseJobs.empty())runSteamLeaseJob();
    ++g_inputHostEpochMirror;focusLightRecheckOnUi();if(!fake::steamLeaseJobs.empty())runSteamLeaseJob();
    fake::check(!fake::nativeDesired&&!steamLightGamepadFallbackRequired(),"healthy native/no-game lease degraded to native borrowing");
   }
  }
  if(variant<4&&variant%4==2){
   fake::now+=20;g_focusLightProofNotBefore=fake::now;g_powerLifecycle=PowerLifecycle::Suspending;++fixturePower;
   focusLightRecheckOnUi();
   fake::check(!g_steamLightGamepadLeaseState.load()&&!g_steamLightGamepadIntent.load()->enabled,"sleep retained old light request authority");
   if(!fake::steamLeaseJobs.empty())runSteamLeaseJob();
   g_powerLifecycle=PowerLifecycle::Resuming;focusLightRecheckOnUi();g_powerLifecycle=PowerLifecycle::Ready;focusLightRecheckOnUi();
   fake::check(!focusLightIsolationCurrent(),"Ready replayed pre-sleep game/no-game evidence");
   publishProof();
   focusLightRecheckOnUi();if(!fake::steamLeaseJobs.empty())runSteamLeaseJob();
   fake::check(focusLightIsolationCurrent()&&!fake::nativeDesired,"fresh wake proof failed to restore light route");
  }
  if(variant<4&&variant%4==3){
   fake::onSteamLeaseRequest=[] {fake::isolationSettingsOn=false;};fake::now+=750;focusLightRecheckOnUi();runSteamLeaseJob();focusLightRecheckOnUi();
   fake::check(!g_steamLightGamepadLeaseState.load()&&!g_steamLightGamepadIntent.load()->enabled,"settings OFF accepted a delayed ON result");
   fake::isolationSettingsOn=true;focusLightRecheckOnUi();
  }
  fake::foreground=fake::window(55);fake::uiAuthority=false;focusLightForegroundChanged();focusLightRecheckOnUi();
  for(unsigned drain=0;drain<3&&!fake::steamLeaseJobs.empty();++drain)runSteamLeaseJob();
  fake::check(!g_steamLightGamepadLeaseState.load()&&!g_steamLightGamepadQueued.load()&&fake::steamLeaseJobs.empty()&&!fake::nativeDesired,"storm exit retained light worker or native ownership");
  fake::check(!g_steamLightGamepadIntent.load()->enabled&&g_steamLightGamepadSettledSerial.load()==g_steamLightGamepadIntent.load()->serial,"storm exit did not settle current OFF intent");
  ++episodes;
 }
 fake::check(episodes==512&&steps>=20480,"storm matrix selection incomplete");
 reset();startLight();fake::exerciseSteamLease=true;fake::recordNativeEligibility=true;fake::steamInstanceAvailable=false;
 focusLightRecheckOnUi();const auto originalDeadline=g_steamLightGamepadLeaseState.load()->initialDeadline;
 const auto originalIntent=g_steamLightGamepadIntent.load();runSteamLeaseJob();
 fake::persona="steamdeck";
 fake::check(!focusLightIsolationCurrent()&&!steamLightGamepadIntentCurrent(originalIntent),"virtual ON retained the native-only light authority");
 focusLightRecheckOnUi();
 fake::check(!g_steamLightGamepadIntent.load()->enabled,"virtual ON retained the native-only ON intent");
 fake::persona="disabled";++g_inputHostEpochMirror;fake::now+=100;focusLightRecheckOnUi();focusLightRecheckOnUi();
 fake::check(!steamLightGamepadFallbackRequired()&&!fake::nativeDesired,"virtual OFF failed to resume qualified native light grace");
 const auto offDeadline=g_steamLightGamepadLeaseState.load()->initialDeadline;
 fake::check(offDeadline==originalDeadline,"virtual ON/OFF refunded the original three-second attempt");
 fake::now=offDeadline-1;focusLightRecheckOnUi();fake::check(!fake::nativeDesired,"virtual OFF native grace ended before three seconds");
 fake::now=offDeadline;focusLightRecheckOnUi();fake::check(fake::nativeDesired,"virtual OFF native grace failed to settle at three seconds");
}
static void lightVirtualBoundary(){
 for(const auto persona:{"steamdeck","dualsense-edge","elite","unknown"})for(unsigned phase=0;phase<3;++phase){
  startLight();fake::exerciseSteamLease=true;fake::recordNativeEligibility=true;focusLightRecheckOnUi();
  const auto previousTicket=g_focusLightIsolationTicket.load();
  const auto previousIntent=g_steamLightGamepadIntent.load();
  const auto previousLease=g_steamLightGamepadLeaseState.load();
  if(phase==1)runSteamLeaseJob();
  if(phase==2){fake::onSteamLeaseRequest=[persona]{fake::persona=persona;};runSteamLeaseJob();}
  else fake::persona=persona;
  fake::check(!focusLightNoGameQualified(fake::now)&&!steamLightGamepadIntentCurrent(previousIntent),"virtual ON retained the native-only light authority");
  fake::check(!focusLightIsolationCurrent()&&!g_focusLightIsolationTicket.load(),"virtual ON retained the old light ticket without an epoch change");
  focusLightRecheckOnUi();
  const auto released=g_steamLightGamepadIntent.load();
  fake::check(released&&released!=previousIntent&&!released->enabled&&!g_focusSession.active,"virtual ON failed to release the old light session and ON intent");
  if(phase!=1)fake::check(previousLease->acknowledgedDeadline.load()==0,"late virtual ON accepted an obsolete light ACK");
  for(unsigned drain=0;drain<3&&!fake::steamLeaseJobs.empty();++drain)runSteamLeaseJob();
  fake::check(!g_steamLightGamepadQueued.load()&&fake::steamLeaseJobs.empty()&&g_steamLightGamepadSettledSerial.load()==released->serial,"virtual ON failed to settle the old Steam OFF responsibility");
  fake::check(!steamLightGamepadIntentCurrent(previousIntent)&&g_focusLightIsolationTicket.load()!=previousTicket,"virtual ON revived a superseded light owner");
  fake::isolationSettingsOn=false;focusLightRecheckOnUi();
  fake::check(!g_steamLightGamepadLeaseState.load()&&!fake::nativeDesired,"settings OFF retained revoked light or fallback ownership");
  fake::persona="disabled";++g_inputHostEpochMirror;fake::isolationSettingsOn=true;publishProof();focusLightRecheckOnUi();
  fake::check(focusLightIsolationCurrent()&&g_focusLightIsolationTicket.load()!=previousTicket&&!fake::nativeDesired,"virtual OFF failed to admit a fresh qualified owner after explicit isolation OFF");
  if(!fake::steamLeaseJobs.empty())runSteamLeaseJob();
  fake::visibleWindow=false;fake::uiAuthority=false;actualMainWindowOwnerEvent(WM_SHOWWINDOW,FALSE);
  for(unsigned drain=0;drain<3&&!fake::steamLeaseJobs.empty();++drain)runSteamLeaseJob();
  fake::check(!g_focusLightIsolationTicket.load()&&!g_steamLightGamepadLeaseState.load()&&!g_steamLightGamepadIntent.load()->enabled&&!g_steamLightGamepadQueued.load()&&fake::steamLeaseJobs.empty()&&!fake::nativeDesired,"hide retained light or physical ownership after persona replacement");
 }
}
static void lightVirtualIdleBoundaries(){
 namespace pipe=ymcc::inputHost::pipeTransport;
 namespace targets=ymcc::inputHost::targetReturnRuntime;
 const std::vector<std::pair<const char*,std::function<void()>>> blockers={
  {"writer",[]{g_inputHostVirtualIntentWriters=1;}},
  {"pending",[]{g_virtualStartProtection.request={11,"steamdeck","pending"};}},
  {"verifying",[]{g_virtualStartProtection.request={11,"elite","verifying"};}},
  {"host-lock",[]{g_inputHostMx.busy=true;}},
  {"host-epoch",[]{g_inputHostEpoch=1;}},
  {"host-run",[]{g_inputHostRunId="old-owner";}},
  {"process",[]{g_inputHostProcess=reinterpret_cast<HANDLE>(std::uintptr_t(1));}},
  {"primary-thread",[]{g_inputHostPrimaryThread=reinterpret_cast<HANDLE>(std::uintptr_t(1));}},
  {"server-pipe",[]{g_inputHostServerPipe=reinterpret_cast<HANDLE>(std::uintptr_t(1));}},
  {"unadopted-pipe",[]{g_inputHostUnadoptedPipe=reinterpret_cast<HANDLE>(std::uintptr_t(1));}},
  {"hello",[]{g_inputHostHello=true;}},{"prepared",[]{g_inputHostPrepared=true;}},
  {"prepare-uncertain",[]{g_inputHostPrepareMayHaveReachedHost=true;}},
  {"neutralized",[]{g_inputHostNeutralized=true;}},{"active",[]{g_inputHostActive=true;}},
  {"quarantine",[]{g_inputHostReleaseQuarantined=true;}},{"journal",[]{g_hidHideVisibilityJournal.active=true;}},
  {"deck-debt",[]{g_msiSteamDeckReleaseDebt.owed=true;}},{"endpoint-debt",[]{g_psEndpointOwnership.owed=true;}},
  {"dinput-debt",[]{g_msiDinputReturnOwed=true;}},{"switch-return",[]{g_inputHostSwitchExecutionReturnPending=true;}},
  {"switch-recovery",[]{g_inputHostSwitchRecoveryPending=true;}},{"recovery",[]{g_inputHostRecoveryPending=true;}},
  {"runtime-recovery",[]{g_inputHostRuntimeRecoveryPending=true;}},{"tuple-heal",[]{g_inputHostTupleHealPending=true;}},
  {"native-refresh",[]{g_msiNativeForegroundCyclePending=true;}},{"post-hide",[]{g_postHideCycleOwned=true;}},
  {"pipe-unknown",[]{pipe::fixturePresent=true;pipe::fixtureRuntime.value.available=false;}},
  {"pipe-live",[]{pipe::fixturePresent=true;pipe::fixtureRuntime.value.stopped=false;}},
  {"cold-targets",[]{targets::state.coldHistoryPending=true;}},{"targets",[]{targets::state.targets.push_back(1);}},
  {"physicals",[]{targets::state.physicals.push_back(1);}},{"proven",[]{targets::state.proven.push_back(1);}},
  {"session-unready",[]{targets::state.session=std::make_shared<targets::FixtureSession>();targets::state.session->isReady=false;}},
  {"session-record",[]{targets::state.session=std::make_shared<targets::FixtureSession>();targets::state.session->hasRecord=false;}},
  {"target-obligation",[]{targets::state.session=std::make_shared<targets::FixtureSession>();targets::state.session->value.obligations.push_back(1);}},
  {"visibility-history",[]{fixtureNativeVisibilityHistoryPending=true;}},
  {"late-writer",[]{pipe::fixturePresent=true;pipe::fixtureRuntime.onSnapshot=[] {g_inputHostVirtualIntentWriters=1;};}},
  {"late-persona",[]{pipe::fixturePresent=true;pipe::fixtureRuntime.onSnapshot=[] {fake::persona="elite";};}}
 };
 for(const auto& blocker:blockers){
  startLight();fake::exerciseSteamLease=true;focusLightRecheckOnUi();
  const auto previous=g_steamLightGamepadIntent.load();blocker.second();
  fake::check(!focusLightVirtualIdle(),blocker.first);
  fake::check(!focusLightNoGameQualified(fake::now)&&!focusLightIsolationCurrent()&&!steamLightGamepadAttemptActive(),"virtual debt admitted light qualification");
  fake::check(!g_focusLightIsolationTicket.load()&&!steamLightGamepadIntentCurrent(previous),"virtual debt retained a light ticket or ON intent");
  focusLightRecheckOnUi();
  fake::check(!g_focusSession.active&&!g_steamLightGamepadIntent.load()->enabled,"virtual debt failed to release the light focus owner");
 }
 reset();publishProof();g_inputHostMx.owned=g_inputHostMx.busy=true;
 fake::check(focusLightNoGameQualified(fake::now),"already-owned host mutex blocked pure idle admission");
 g_inputHostMx={};pipe::fixturePresent=true;targets::state.session=std::make_shared<targets::FixtureSession>();
 fake::check(focusLightNoGameQualified(fake::now),"stopped pipe and debt-free ready session blocked idle admission");
 for(const auto phase:{"idle","complete","failed"}){
  g_virtualStartProtection.request={11,"steamdeck",phase};
  fake::check(focusLightNoGameQualified(fake::now),"settled virtual request blocked debt-free disabled admission");
 }
 startLight(false);fake::exerciseSteamLease=true;focusLightRecheckOnUi();
 const auto previous=g_steamLightGamepadIntent.load();
 fake::persona="steamdeck";++g_inputHostVirtualIntentRevision;
 fake::persona="disabled";++g_inputHostVirtualIntentRevision;
 fake::check(!focusLightIsolationCurrent()&&!steamLightGamepadAttemptActive()&&!steamLightGamepadIntentCurrent(previous),"virtual intent ABA revived an old light ticket");
 focusLightRecheckOnUi();fake::check(!g_focusSession.active&&!g_steamLightGamepadIntent.load()->enabled,"virtual intent ABA retained the old focus or lease owner");
 startLight();
 fixtureSharedPower::afterRead=[] {++g_inputHostVirtualIntentRevision;};
 fake::check(!focusLightIsolationCurrent(),"virtual intent ABA revived an old light ticket");
 for(unsigned fault=0;fault<6;++fault){
  startLight();fake::exerciseSteamLease=true;focusLightRecheckOnUi();
  const auto proof=g_focusLightIsolationTicket.load();
  if(fault==0)fixtureSharedPower::available=false;
  if(fault==1)fixtureSharedPower::value.flags|=ymcc::sleeppower::kNotificationPending;
  if(fault==2)fixtureSharedPower::value.phase=3;
  if(fault==3)++fixtureSharedPower::value.observedNotificationSequence;
  if(fault==4)++fixtureSharedPower::value.suspendSerial;
  if(fault==5)fixtureSharedPower::value.generation=fixturePower+1;
  fake::check(!sharedPowerLifecycleCurrent(proof->sharedPower)&&!focusLightIsolationCurrent()&&!steamLightGamepadAttemptActive(),"shared power revocation retained light authority");
  focusLightRecheckOnUi();fake::check(!g_focusSession.active&&!g_steamLightGamepadIntent.load()->enabled,"shared power revocation retained light focus or lease owner");
 }
 startLight();fake::exerciseSteamLease=true;focusLightRecheckOnUi();
 const auto proof=g_focusLightIsolationTicket.load();const auto initialDeadline=g_steamLightGamepadLeaseState.load()->initialDeadline;
 ++fixtureSharedPower::value.publicationRevision;
 const auto reads=fixtureSharedPower::reads.load(std::memory_order_relaxed);
 fake::check(focusLightIsolationCurrent()&&g_focusLightIsolationTicket.load()==proof&&steamLightGamepadAttemptActive(),"publication-only update revoked a healthy light owner");
 fake::check(fixtureSharedPower::reads.load(std::memory_order_relaxed)-reads==2,"light ticket or attempt repeated the current power read");
 focusLightRecheckOnUi();fake::check(g_steamLightGamepadLeaseState.load()->initialDeadline==initialDeadline,"publication-only update refunded the three-second attempt");
}
static void lightTicketPowerCapture(){
 reset();publishProof();
 fixtureSharedPower::afterRead=[] {++fixtureSharedPower::value.publicationRevision;};
 const auto reads=fixtureSharedPower::reads.load(std::memory_order_relaxed);
 fake::check(focusLightIsolationBegin(),"publication-only update rejected a new light ticket");
 fake::check(fixtureSharedPower::reads.load(std::memory_order_relaxed)-reads==2,"light ticket creation repeated the qualifying power capture");
 reset();publishProof();
 fixtureSharedPower::afterRead=[] {++fixtureSharedPower::value.suspendSerial;};
 fake::check(!focusLightIsolationBegin()&&!g_focusLightIsolationTicket.load(),"light ticket accepted a changed suspend serial after qualification");
 reset();publishProof();
 fixtureSharedPower::afterRead=[] {fixtureSharedPower::value.flags|=ymcc::sleeppower::kNotificationPending;};
 fake::check(!focusLightIsolationBegin()&&!g_focusLightIsolationTicket.load(),"light ticket accepted a pending suspend after qualification");
 const auto coalesceReady=[] {
  closeHardwareWriteGate("fixture-coalesced-suspend");
  fixtureSharedPower::value.suspendSerial=g_sharedPowerSuspendSerial.load();
  ++fixtureSharedPower::value.observedNotificationSequence;
  g_hardwareWriteGate=true;
 };
 reset();publishProof();
 coalesceReady();
 fake::check(!focusLightNoGameQualified(fake::now)&&!focusLightIsolationBegin()&&!g_focusLightIsolationTicket.load(),
  "same-generation Ready before UI projection reused pre-suspend no-game proof");
 publishProof();
 fake::check(!focusLightIsolationBegin(),"same-tick no-game observation was treated as certainly post-suspend");
 ++fake::now;publishProof();
 fake::check(focusLightIsolationBegin()&&focusLightIsolationCurrent(),"known post-suspend no-game proof could not start a fresh light ticket");
 reset();publishProof();
 fixtureSharedPower::afterRead=coalesceReady;
 fake::check(!focusLightNoGameQualified(fake::now),"sleep crossing the shared power read retained pre-suspend no-game proof");
}
static void lightLeasePublicationBoundary(){
 for(const bool acknowledged:{false,true})for(unsigned change=0;change<3;++change){
  startLight();fake::exerciseSteamLease=true;
  if(acknowledged){actualSteamLightGamepadUiChanged();runSteamLeaseJob();fake::now+=750;}
  const auto oldProof=g_focusLightIsolationTicket.load();
  const auto oldIntent=g_steamLightGamepadIntent.load();
  fake::onUiOutputEligibility=[change]{
   if(change==0)fixtureSharedPower::value.flags|=ymcc::sleeppower::kNotificationPending;
   if(change==1){++g_inputHostVirtualIntentRevision;fake::persona="elite";}
   if(change!=2)fake::check(!focusLightIsolationCurrent(),"fixture failed to retire the old proof during output gating");
   else fake::check(focusLightIsolationBegin(),"fixture failed to replace the old proof during output gating");
  };
  const auto submitted=fake::steamLeaseRequests.size();
  actualSteamLightGamepadUiChanged();
  const auto currentProof=g_focusLightIsolationTicket.load();
  const auto intent=g_steamLightGamepadIntent.load();
  fake::check(oldProof&&oldProof!=currentProof&&bool(currentProof)==(change==2),"fixture missed the proof transition");
  fake::check(!intent||!intent->enabled,"retired proof published a Steam ON intent");
  if(acknowledged){
   fake::check(intent&&intent!=oldIntent&&fake::steamLeaseJobs.size()==1,"retired proof did not schedule the old lease release");
   runSteamLeaseJob();
   fake::check(g_steamLightGamepadSettledSerial.load()==intent->serial&&fake::steamLeaseRequests.size()==submitted+1&&!fake::steamLeaseRequests.back().at("enabled").get<bool>(),"old lease release did not settle once");
  }else fake::check(!intent&&fake::steamLeaseJobs.empty(),"retired proof started a lease without an earlier ON");
 }
 for(const bool previousAcknowledged:{false,true}){
  startLight();fake::exerciseSteamLease=true;actualSteamLightGamepadUiChanged();
  if(previousAcknowledged){runSteamLeaseJob();fake::now+=750;actualSteamLightGamepadUiChanged();}
  fake::onUiOutputEligibility=[] {
   ++fixtureSharedPower::value.observedNotificationSequence;
   fake::check(!focusLightIsolationCurrent(),"fixture failed to retire the worker proof");
  };
  const auto submitted=fake::steamLeaseRequests.size();
  runSteamLeaseJob();
  fake::check(fake::steamLeaseRequests.size()==submitted,"retired proof reached Steam from the worker gate");
  actualSteamLightGamepadUiChanged();
  const auto release=g_steamLightGamepadIntent.load();
  fake::check(release&&!release->enabled,"worker revocation did not publish OFF");
  runSteamLeaseJob();
  fake::check(!g_steamLightGamepadQueued.load()&&g_steamLightGamepadSettledSerial.load()==release->serial,"worker revocation did not settle OFF");
 }
}
static void nativeUiEligibility(){
 startLight();fake::check(focusLightIsolationCurrent(),"native UI scenario did not establish qualified light proof");
 fake::check(!msiNativeForegroundUiEligible(),"qualified light requested new physical borrowing");
 fake::check(fake::uiGateReads==0&&fake::ownerEvents==0&&fake::deviceWorkRequests==0,"qualified light reached common physical owner gate");
 fake::isolationSettingsOn=false;fake::check(!msiNativeForegroundUiEligible(),"qualified light bypassed isolation settings OFF");
 fake::isolationSettingsOn=true;fake::uiAuthority=false;fake::check(!msiNativeForegroundUiEligible(),"qualified light opened physical owner for foreign context");
 fake::check(fake::uiGateReads==0,"qualified light fell through exemption on main settings or authority change");
 fake::uiAuthority=true;fake::radialOwner=true;const auto radialReads=fake::uiGateReads;
 fake::check(msiNativeForegroundUiEligible()&&fake::uiGateReads==radialReads+1,"common physical gate suppressed independent radial owner");
 fake::isolationSettingsOn=false;fake::check(msiNativeForegroundUiEligible(),"main isolation settings OFF suppressed independent radial owner");
 fake::isolationSettingsOn=true;fake::uiAuthority=false;fake::check(msiNativeForegroundUiEligible(),"main foreign context suppressed independent radial owner");
 fake::check(fake::uiGateReads==radialReads+3,"independent radial owner bypassed common physical gate");
 fake::uiAuthority=true;fake::radialOwner=false;fake::childOwner=true;const auto childReads=fake::uiGateReads;
 fake::check(msiNativeForegroundUiEligible()&&fake::uiGateReads==childReads+1,"common physical gate suppressed independent child owner");
 fake::isolationSettingsOn=false;fake::check(msiNativeForegroundUiEligible(),"main isolation settings OFF suppressed independent child owner");
 fake::isolationSettingsOn=true;fake::uiAuthority=false;fake::check(msiNativeForegroundUiEligible(),"main foreign context suppressed independent child owner");
 fake::check(fake::uiGateReads==childReads+3,"independent child owner bypassed common physical gate");
 fake::uiAuthority=true;fake::childOwner=false;const auto leaseUpdates=fake::steamUiChanges;
 focusLightRecheckOnUi();fake::check(g_focusSession.active&&fake::steamUiChanges==leaseUpdates+1&&fake::ownerEvents==0&&fake::deviceWorkRequests==0,"qualified light recheck missed lease update or notified physical owner");
 ++g_msiMainSourceRevision;
 focusLightRecheckOnUi();
 fake::check(!g_focusSession.active&&!g_focusLightIsolationTicket.load()&&g_focusSession.strictReturnOnly&&g_summonRegistrationGeneration==0,"source recheck retained focus ticket or registration");
 fake::check(fake::ownerEvents==1&&fake::deviceWorkRequests==1&&fake::ownerEventAfterClear,"source recheck failed to restore original physical owner after proof revocation");
 fake::check(fake::steamUiChanges==leaseUpdates+3&&!fake::steamUiLastTicketPresent,"source recheck missed lease release after clearing proof");
 fake::check(msiNativeForegroundUiEligible(),"proof revocation changed original physical owner gate");
 fake::isolationSettingsOn=false;fake::check(!msiNativeForegroundUiEligible(),"proof revocation opened native owner with settings OFF");
 fake::isolationSettingsOn=true;fake::uiAuthority=false;fake::check(!msiNativeForegroundUiEligible(),"proof revocation bypassed original authority");
 startLight(false);g_deferFirstShow=true;const auto expiryLeaseUpdates=fake::steamUiChanges;fake::now=31001;focusLightRecheckOnUi();
 fake::check(!g_focusSession.active&&!g_focusLightIsolationTicket.load()&&fake::timers.empty()&&!g_deferFirstShow,"expired proof recheck retained focus session or deferred show");
 fake::check(fake::ownerEvents==1&&fake::deviceWorkRequests==1&&fake::ownerEventAfterClear,"expired proof recheck failed to restore original physical owner");
 fake::check(fake::steamUiChanges==expiryLeaseUpdates+2&&!fake::steamUiLastTicketPresent,"expired proof recheck missed lease release after clearing ticket");
 fake::check(msiNativeForegroundUiEligible(),"expired focus proof changed original physical owner gate");
 focusLightRecheckOnUi();fake::check(fake::ownerEvents==1&&fake::deviceWorkRequests==1&&fake::steamUiChanges==expiryLeaseUpdates+3,"repeated proof recheck queued physical owner work or missed lease update");
 lightLeaseFallbackLifecycle();
 lightLeaseStorm();
 lightVirtualBoundary();
 lightVirtualIdleBoundaries();
 lightTicketPowerCapture();
 lightLeasePublicationBoundary();
}
static void lightForegroundGameTransition(){
 for(unsigned revoked=0;revoked<4;++revoked){
  reset();publishProof();fake::foreground=g_hwnd;fake::recordNativeEligibility=true;
  actualMainWindowOwnerEvent(WM_SHOWWINDOW,TRUE);
  fake::check(focusLightIsolationCurrent()&&!g_focusSession.active&&!fake::nativeDesired,"foreground-only admission did not retain light policy");
  const auto originalSession=g_focusSessionSequence;
  auto observation=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());
  if(revoked==0||revoked==3)observation->game.pid=777;
  if(revoked==1)observation->discoveryState=NativeGameDiscoveryState::Pending;
  if(revoked==2)fake::now=31001;
  g_gameValveObservation.store(observation);
  if(revoked==3)fake::check(!focusLightIsolationCurrent(),"background consumer retained revoked proof");
  focusLightRecheckOnUi();
  fake::check(!g_focusLightIsolationTicket.load()&&!fake::steamUiLastTicketPresent,"game transition retained light proof or Steam lease");
  fake::check(fake::nativeDesired&&fake::deviceWorkRequests==1,"game transition left original physical isolation owner stale");
  fake::check(!g_focusSession.active&&g_focusSessionSequence==originalSession&&fake::focusAttempts.empty()&&fake::registrationCalls==0,"game transition created a focus session or registered a game");
  focusLightRecheckOnUi();
  fake::check(fake::deviceWorkRequests==1,"unchanged game transition repeated device work");
  fake::isolationSettingsOn=false;focusLightRecheckOnUi();
  fake::check(!fake::nativeDesired&&fake::deviceWorkRequests==2,"settings OFF failed to return physical policy owner");
 }
 reset();publishProof();fake::foreground=g_hwnd;fake::recordNativeEligibility=true;
 auto game=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());game->game.pid=777;g_gameValveObservation.store(game);
 actualMainWindowOwnerEvent(WM_SHOWWINDOW,TRUE);
 fake::check(fake::nativeDesired&&fake::deviceWorkRequests==1&&!g_focusLightIsolationTicket.load(),"known game did not begin original isolation");
 publishProof();focusLightRecheckOnUi();
 fake::check(fake::nativeDesired&&fake::deviceWorkRequests==1&&!focusLightIsolationCurrent(),"game channel downgraded during the same visible lifecycle");
 fake::visibleWindow=false;fake::uiAuthority=false;actualMainWindowOwnerEvent(WM_SHOWWINDOW,FALSE);
 fake::check(!g_focusIsolationPrepared&&!g_focusIsolationGameOpen&&!fake::nativeDesired,"hidden lifecycle retained game channel or physical owner");
 fake::visibleWindow=true;fake::uiAuthority=true;focusLightRecheckOnUi();
 fake::check(!fake::nativeDesired&&focusLightIsolationCurrent(),"new no-game lifecycle failed to admit light policy");
}
static void gameObservationProducerCoverage(){
 namespace producer=gameObservationProducer;
 namespace inventory=gameObservationProducer::ymcc::gameInventory;
 const std::vector<std::wstring> noNames,configured={L"game",L"second"};
 auto fresh=[&]{
  fake::check(producer::liveHandles==0&&producer::opens==producer::closes,"producer retained a process handle between passes");
  producer::processes={producer::ProcessPort{}};producer::clock+=6000;
  producer::fullAvailable=producer::basicAvailable=producer::legacyAvailable=producer::currentSessionKnown=true;
  producer::foregroundWindow=producer::g_hwnd;inventory::invalidate();
 };
 auto unknown=[&](unsigned kind){
  auto& row=producer::processes.front();
  switch(kind){case 0:row.openError=ERROR_ACCESS_DENIED;break;case 1:row.memoryKnown=false;break;case 2:row.sessionKnown=false;break;case 3:row.birthKnown=false;break;case 4:producer::currentSessionKnown=false;break;}
 };
 auto positive=[&]{producer::ProcessPort row;row.pid=1002;row.name=L"second";row.path=L"C:\\Games\\second.exe";row.workingSet/=2;producer::processes.push_back(row);};
 auto noIdle=[&](const NativeDetectedGame& game,NativeGameDiscoveryState state,ULONGLONG observedAt){
  g_focusIsolationGameOpen=false;g_focusIsolationPrepared=false;
  fake::now=producer::clock;publishProof();auto observation=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());
  observation->game=game;observation->discoveryState=state;observation->qualificationObservedAt=observedAt;g_gameValveObservation.store(observation);
  fake::check(!focusLightNoGameQualified(fake::now),"unknown producer observation admitted light no-game proof");
 };
 fake::check(producer::SG_MIN_WS==500ULL*1024*1024&&producer::SG_CAPTURE_MIN_WS==50ULL*1024*1024,"producer thresholds were replaced by fixture policy");
 for(unsigned kind=0;kind<5;++kind){
  fresh();unknown(kind);NativeDetectedGame one;bool changed=false;
  fake::check(producer::nativeScanGameOnePid(1001,L"game",noNames,noNames,noNames,&one,&changed)==producer::NativeOnePidResult::Unavailable&&!one.pid&&!changed,"one-PID unknown became a confirmed rejection");
  NativeGameDiscoveryState state=NativeGameDiscoveryState::Complete;ULONGLONG observedAt=0;
  const auto empty=producer::nativeScanGame(0,noNames,noNames,noNames,&state,&observedAt);
  fake::check(!empty.pid&&state==NativeGameDiscoveryState::Pending,"unknown candidate completed idle discovery");
  noIdle(empty,state,observedAt);
  if(kind!=1){NativeDetectedGame configuredUnknown;fake::check(!producer::nativeScanGameConfiguredBest(noNames,noNames,configured,&configuredUnknown)&&!configuredUnknown.pid,"configured scan confirmed an unknown empty pass");}
  if(kind!=4){
   positive();inventory::invalidate();
   const auto verified=producer::nativeScanGame(0,noNames,noNames,noNames,&state,&observedAt);
   fake::check(verified.pid==1002&&state==NativeGameDiscoveryState::Complete,"unknown candidate revoked an independently verified game");
  }
 }
 fresh();producer::processes.front().memoryKnown=false;NativeDetectedGame zeroMemory;
 fake::check(producer::nativeScanGameOnePid(1001,L"game",noNames,noNames,configured,&zeroMemory)==producer::NativeOnePidResult::Ok&&zeroMemory.pid==1001&&zeroMemory.workingSet==0,"configured zero-WS admission depended on memory success");
 NativeGameDiscoveryState state=NativeGameDiscoveryState::Unavailable;ULONGLONG observedAt=0;
 fake::check(producer::nativeScanGame(0,noNames,noNames,configured,&state,&observedAt).pid==1001,"full scan lost configured zero-WS candidate");
 fake::check(producer::nativeScanGameConfiguredBest(noNames,noNames,configured,&zeroMemory)&&zeroMemory.pid==1001,"configured-best lost zero-WS candidate");
 for(unsigned rejection=0;rejection<5;++rejection){
  fresh();auto& row=producer::processes.front();
  switch(rejection){case 0:row.workingSet=1;break;case 1:row.session=2;break;case 2:row.waitState=WAIT_OBJECT_0;break;case 3:row.openError=ERROR_INVALID_PARAMETER;break;case 4:row.name=L"excluded";row.path=L"C:\\Games\\excluded.exe";break;}
  NativeDetectedGame rejected;bool changed=false;
  fake::check(producer::nativeScanGameOnePid(row.pid,row.name,noNames,noNames,noNames,&rejected,&changed)==producer::NativeOnePidResult::Rejected&&!rejected.pid,"confirmed low-WS, excluded, gone or foreign-session process became unknown");
  fake::check(changed==(rejection==2||rejection==3),"one-PID gone topology responsibility changed");
 }
 fresh();producer::processes.front().workingSet=1;
 const auto confirmedEmpty=producer::nativeScanGame(0,noNames,noNames,noNames,&state,&observedAt);
 fake::check(!confirmedEmpty.pid&&state==NativeGameDiscoveryState::Complete&&observedAt,"confirmed policy rejection lost the no-game observation");
 g_focusIsolationGameOpen=false;g_focusIsolationPrepared=false;fake::now=producer::clock;publishProof();
 auto emptyObservation=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());
 emptyObservation->game=confirmedEmpty;emptyObservation->discoveryState=state;emptyObservation->qualificationObservedAt=observedAt;g_gameValveObservation.store(emptyObservation);
 fake::check(focusLightNoGameQualified(fake::now),"confirmed empty producer observation lost preferred light isolation");
 fresh();unknown(0);
 const auto strictUnknown=producer::nativeScanGame(1001,noNames,noNames,noNames,&state,&observedAt);
 fake::check(!strictUnknown.pid&&state==NativeGameDiscoveryState::Unavailable&&!observedAt,"strict unknown target established an empty qualification");noIdle(strictUnknown,state,observedAt);
 fresh();unknown(0);inventory::Candidates candidates;
 auto queueHint=[&](unsigned long long version,bool clear){candidates.reset(version,clear);candidates.ranked.push_back({1001,L"game",producer::processes.front().workingSet,2,0,{},101,101});candidates.rank();};
 auto validate=[&](const inventory::Hint& hint,NativeDetectedGame& one){
  fake::check(!candidates.candidateUnconfirmed,"next retained previous candidate unknown flag");
  const auto result=producer::nativeScanGameOnePid(hint.pid,hint.name,noNames,noNames,noNames,&one);
  candidates.candidateUnconfirmed=result==producer::NativeOnePidResult::Unavailable;return result==producer::NativeOnePidResult::Ok;
 };
 queueHint(1,true);candidates.candidateUnconfirmed=true;
 fake::check(!inventory::next<NativeDetectedGame>(candidates,true,producer::clock,validate).pid&&!candidates.complete&&candidates.hasUnconfirmed&&candidates.rejected.empty(),"unknown candidate entered the confirmed negative cache");
 producer::processes.front().openError=0;queueHint(2,false);
 fake::check(!candidates.candidateUnconfirmed&&!candidates.hasUnconfirmed&&candidates.ranked.size()==1,"new pass retained unknown candidate suppression");
 fake::check(inventory::next<NativeDetectedGame>(candidates,true,producer::clock,validate).pid==1001,"new bounded pass failed to retry an unknown candidate");
 producer::processes.front().session=2;queueHint(3,true);inventory::next<NativeDetectedGame>(candidates,true,producer::clock,validate);
 fake::check(candidates.complete&&candidates.rejected.size()==1,"confirmed rejection lost original negative-cache responsibility");
 queueHint(4,false);fake::check(candidates.rejected.empty(),"completed pass retained stale confirmed rejection");
 fresh();producer::foregroundWindow=nullptr;
 const inventory::RuleKey rules{noNames,noNames,noNames};
 const auto fullBefore=producer::fullSnapshotReads;
 const auto& initial=inventory::inventory(rules,false,producer::nativeGameDiscoveryTopologyRelevant,producer::nativeGameDiscoveryWorkingSetHint);
 fake::check(initial.available&&initial.rows.size()==1&&producer::fullSnapshotReads==fullBefore+1,"initial inventory bypassed full snapshot provider");
 producer::processes.front().openError=ERROR_ACCESS_DENIED;producer::clock+=30000;
 const auto& fallback=inventory::inventory(rules,false,producer::nativeGameDiscoveryTopologyRelevant,producer::nativeGameDiscoveryWorkingSetHint);
 fake::check(fallback.available&&fallback.rows.size()==1&&producer::fullSnapshotReads==fullBefore+2,"selective access denied skipped full snapshot fallback");
 producer::fullAvailable=false;producer::clock+=30000;
 const auto& unavailable=inventory::inventory(rules,false,producer::nativeGameDiscoveryTopologyRelevant,producer::nativeGameDiscoveryWorkingSetHint);
 fake::check(!unavailable.available&&unavailable.rows.empty()&&producer::fullSnapshotReads==fullBefore+3,"failed full snapshot became an empty available inventory");
 producer::legacyAvailable=false;inventory::invalidate();
 const auto absent=producer::nativeScanGame(0,noNames,noNames,noNames,&state,&observedAt);
 fake::check(!absent.pid&&state==NativeGameDiscoveryState::Unavailable&&!observedAt,"unavailable snapshots created a completed idle proof");noIdle(absent,state,observedAt);
 for(unsigned kind=0;kind<5;++kind){
  fresh();unknown(kind);producer::fullAvailable=false;const auto legacyBefore=producer::legacyReads;
  const auto legacy=producer::nativeScanGame(0,noNames,noNames,noNames,&state,&observedAt);
  fake::check(!legacy.pid&&state==NativeGameDiscoveryState::Unavailable&&!observedAt&&producer::legacyReads==legacyBefore+(kind==4?0u:1u),"legacy unknown rows completed an empty game observation");noIdle(legacy,state,observedAt);
  if(kind!=1){inventory::invalidate();NativeDetectedGame configuredUnknown;fake::check(!producer::nativeScanGameConfiguredBest(noNames,noNames,configured,&configuredUnknown)&&!configuredUnknown.pid,"configured legacy unknown rows confirmed an empty pass");}
  if(kind!=4){positive();inventory::invalidate();const auto winner=producer::nativeScanGame(0,noNames,noNames,noNames,&state,&observedAt);fake::check(winner.pid==1002&&state==NativeGameDiscoveryState::Complete,"legacy unknown row revoked a verified positive");inventory::invalidate();NativeDetectedGame configuredPositive;fake::check(producer::nativeScanGameConfiguredBest(noNames,noNames,configured,&configuredPositive)&&configuredPositive.pid==1002,"configured legacy unknown row revoked a verified positive");}
 }
 fake::check(producer::liveHandles==0&&producer::opens==producer::closes,"producer discovery leaked process handles");reset();
}
static void gameBeforeShowLifecycle(){
 reset();publishProof();fake::integrated=true;fake::visibleWindow=false;
 fake::refocusSucceeds=false;
 auto game=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());game->game.pid=777;g_gameValveObservation.store(game);
 bool observedBeforeShow=false;
 fake::onShow=[&]{observedBeforeShow=g_focusIsolationPrepared.load()&&g_focusIsolationGameOpen.load()&&!g_focusSession.lightNoGame;publishProof();};
 nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true);
 fake::check(observedBeforeShow,"game state was not queried before changing foreground");
 fake::check(g_focusIsolationGameOpen&&!g_focusSession.lightNoGame&&!focusLightNoGameQualified(fake::now),"foreground handoff downgraded the known-game channel");
 fake::check(fake::timers.at(SUMMON_FOCUS_TIMER_ID)==80,"known-game summon changed ordinary retry cadence");
 focusLightRecheckOnUi();focusClearSession();focusLightRecheckOnUi();
 fake::check(g_focusIsolationGameOpen&&!g_focusLightIsolationTicket.load(),"focus-session cleanup erased the visible game channel");
 fake::foreground=fake::window(55);focusLightRecheckOnUi();fake::foreground=g_hwnd;focusLightRecheckOnUi();
 fake::check(g_focusIsolationGameOpen&&!g_focusLightIsolationTicket.load(),"foreground changes reclassified a running game as light");
 fake::iconicWindow=true;actualMainWindowOwnerEvent(WM_SIZE,SIZE_MINIMIZED);focusLightRecheckOnUi();
 fake::check(!g_focusIsolationPrepared&&!g_focusIsolationGameOpen,"minimize did not end the preflight lifecycle");
 fake::iconicWindow=false;fake::visibleWindow=false;fake::onShow={};
 nativeYmccShortcutSummon(focusCaptureTarget(fake::window(44)),true);
 fake::check(g_focusSession.lightNoGame&&!g_focusIsolationGameOpen,"next qualified no-game summon retained old game latch");
 reset();publishProof();fake::visibleWindow=false;
 auto originalGame=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());originalGame->game.pid=777;g_gameValveObservation.store(originalGame);
 actualMainWindowBeforeShow(g_hwnd,SW_RESTORE);publishProof();fake::visibleWindow=true;fake::foreground=g_hwnd;focusLightRecheckOnUi();
 fake::check(g_focusIsolationGameOpen&&!g_focusLightIsolationTicket.load()&&!g_focusSession.active,"tray preflight did not retain game state independently of focus session");
 reset();publishProof();fake::visibleWindow=false;
 actualMainWindowBeforeShow(g_hwnd,SW_SHOW);g_deferFirstShow=true;
 auto lateGame=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());lateGame->game.pid=777;g_gameValveObservation.store(lateGame);
 actualMainWindowBeforeShow(g_hwnd,SW_SHOW);focusLightRecheckOnUi();
 fake::check(g_focusIsolationGameOpen&&g_focusIsolationPrepared,"deferred first frame dropped its game channel");
 actualMainWindowAfterHide(false);
 fake::check(g_focusIsolationGameOpen,"child hide cleared the main game channel");
 fake::visibleWindow=false;g_deferFirstShow=false;actualMainWindowAfterHide(true);
 fake::check(!g_focusIsolationPrepared&&!g_focusIsolationGameOpen&&!g_focusLightIsolationTicket.load(),"main hide retained a deferred game channel");
 reset();publishProof();fake::visibleWindow=false;fake::iconicWindow=true;
 auto beforeRestore=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());beforeRestore->game.pid=777;g_gameValveObservation.store(beforeRestore);
 actualMainWindowBeforeShow(g_hwnd,SW_RESTORE);publishProof();actualMainWindowOwnerEvent(WM_SHOWWINDOW,TRUE);
 fake::check(g_focusIsolationGameOpen,"pre-show visibility state erased the captured game channel");
 reset();publishProof();fake::integrated=true;nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true);
 auto newGame=std::make_shared<NativeGameValveSnapshot>(*g_gameValveObservation.load());newGame->game.pid=777;g_gameValveObservation.store(newGame);
 focusPrepareSummonSession();
 fake::check(g_focusIsolationGameOpen&&!g_focusSession.lightNoGame,"active light fast path bypassed game-open admission");
 for(const auto persona:{"steamdeck","dualsense-edge","elite","unknown"}){
  reset();publishProof();fake::persona=persona;fake::integrated=true;fake::visibleWindow=false;
  bool originalBeforeShow=false;
  fake::onShow=[&]{originalBeforeShow=g_focusIsolationPrepared.load()&&!g_focusSession.lightNoGame&&!g_focusLightIsolationTicket.load();};
  fake::check(nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true),"virtual output changed ordinary summon result");
  fake::check(originalBeforeShow&&!focusLightNoGameQualified(fake::now)&&!focusLightIsolationCurrent(),"enabled or unknown virtual output entered the no-game light route");
  fake::check(fake::registrationCalls==1&&!summonEvent().value("skipGameRegistration",false),"enabled virtual output skipped ordinary summon registration");
 }
 reset();gameObservationProducerCoverage();
}
static void oldReturnTimerNewSession(){
 startLight(false);const auto sequence=g_focusSessionSequence;actualFocusTimer(RETURN_GAME_FOCUS_TIMER_ID);
 fake::check(g_focusSession.active&&!g_focusSession.returning&&g_focusSessionSequence==sequence,"old return timer cancelled new non-returning light session");
 startLight();fake::returnSucceeds=false;actualFocusBeginReturnToPreviousWindow(fake::now);const auto returnSequence=g_focusSessionSequence;
 actualFocusTimer(SUMMON_FOCUS_TIMER_ID);fake::check(g_focusSession.active&&g_focusSession.returning&&g_focusSessionSequence==returnSequence,"old summon timer cancelled current return");
 startLight();fake::now=g_focusSession.summonNextAt;const auto attempts=fake::refocusAttempts.size();actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::check(g_focusSession.active&&g_focusSession.lightForegroundAcquired&&fake::refocusAttempts.size()==attempts,"old summon timer refocused acquired light session");
}
static void valvePublishOrder(){
 reset();publishProof();auto empty=*g_gameValveObservation.load();g_gameValveObservation.store({});empty.generation=18;
 nativeValvePublish(empty);fake::check(g_gameValvePublishedGeneration==18&&focusLightNoGameQualified(fake::now),"initial durable no-game publication failed");
 fake::durableSucceeds=false;auto positive=empty;positive.generation=20;positive.game.pid=901;positive.game.processCreated=777;positive.game.path=L"C:\\Games\\current.exe";
 nativeValvePublish(positive);fake::check(g_gameValvePublishedGeneration==18&&g_gameValveObservation.load()->game.pid==901&&!focusLightNoGameQualified(fake::now),"failed durable write replaced positive memory authority");
 auto lateEmpty=empty;lateEmpty.generation=19;nativeValvePublish(lateEmpty);const auto observed=g_gameValveObservation.load();
 fake::check(observed&&observed->generation==20&&observed->game.pid==901&&!focusLightNoGameQualified(fake::now),"late no-game publication replaced newer positive observation");
 fake::check(fake::durableWrites==2&&g_gameValvePublishedGeneration==18&&fake::downloadCancels==1,"memory generation guard changed original durable responsibility");
 auto unavailable=empty;unavailable.generation=21;nativeValvePublish(unavailable,false);
 fake::check(g_gameValveObservation.load()->qualificationObservedAt==0&&!focusLightNoGameQualified(fake::now),"unavailable rules republished complete no-game proof");
}
int main(int argc,char** argv){
 unsigned scenarios=0,comparisons=0;std::vector<std::string> completed;std::string selected;
 try{
  if(argc!=1){fake::check(argc==3&&std::string(argv[1])=="--case","invalid case selection");selected=argv[2];}
  const std::vector<std::pair<const char*,void(*)()>> cases={
   {"proof-basic",proofBasic},{"proof-expiry",proofExpiry},{"proof-inventory",proofInventory},{"proof-cutoff",proofCutoff},
   {"ticket-source",ticketSource},{"ticket-epochs",ticketEpochs},{"light-wrapper",lightWrapper},{"light-bring-cancel",lightCancelledDuringBring},{"light-foreground-entry",lightForegroundEntry},{"light-foreground-game-transition",lightForegroundGameTransition},{"game-before-show-lifecycle",gameBeforeShowLifecycle},
   {"refocus-user-choice",refocusUserChoice},{"refocus-entering",refocusEntering},{"light-handoff",lightHandoff},{"light-handoff-lifecycle",lightHandoffLifecycle},{"light-handoff-admission",lightHandoffAdmission},{"light-settings-owner",lightSettingsOwner},{"late-ready",lateReady},{"ui-recheck",uiRecheck},{"late-power-ready",latePowerReady},
   {"isolation-shared-power-order",isolationSharedPowerOrder},{"isolation-coalesced-ready",isolationCoalescedReady},{"light-no-demand-power-read",lightNoDemandPowerRead},{"light-ui-fallback-reentry",lightUiFallbackReentry},{"light-stale-intent-fast-release",lightStaleIntentFastRelease},
   {"return-identity",returnIdentity},{"return-validation",returnValidation},{"return-success",returnSuccess},{"return-rate-old-callbacks",returnRateAndOldCallbacks},
   {"reentrant-hide",reentrantHide},{"reentrant-clear",reentrantClear},{"reentrant-wrapper",reentrantWrapper},{"stale-registration",staleRegistration},{"native-ui-eligibility",nativeUiEligibility},{"old-return-timer-new-session",oldReturnTimerNewSession},{"valve-publish-order",valvePublishOrder}
  };
  if(selected.empty())legacyCases(scenarios,comparisons);
  for(const auto& entry:cases)if(selected.empty()||selected==entry.first){entry.second();completed.emplace_back(entry.first);}
  fake::check(!completed.empty(),"unknown or empty case selection");
  std::cout<<json{{"pass",true},{"checks",fake::checks},{"controlScenarios",scenarios},{"baselineComparisons",comparisons},{"lightScenarios",completed},{"selectedCase",selected},
   {"actualOsInput",false},{"realOsSnapshot",false},{"fieldProducerProven",false},{"physicalIsolationProven",false},{"powerPublisherExecuted",false},{"gameValvePublisherExecuted",selected.empty()||selected=="valve-publish-order"},{"nativeRefreshCalls",fake::ownerEvents},{"nativeDeviceWorkRequests",fake::deviceWorkRequests}}.dump()<<'\n';return 0;
 }catch(const std::exception& error){
  std::cout<<json{{"pass",false},{"checks",fake::checks},{"controlScenarios",scenarios},{"baselineComparisons",comparisons},{"lightScenarios",completed},{"selectedCase",selected},{"error",error.what()}}.dump()<<'\n';
  std::cerr<<error.what()<<'\n';return 1;
 }
}

#endif // FOCUS_LIGHT_RETIRED
