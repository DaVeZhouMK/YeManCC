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
#include <thread>
#include <vector>
#include "input_idle_diagnostics.h"
#include "json.hpp"
using json=nlohmann::json;
namespace fake {
unsigned checks=0,reads=0;DWORD error=12345,foregroundPid=444;bool valid=true,hide=false,throwRead=false,throwSink=false;
ULONGLONG now=1000,created=101;DWORD identityPid=444;unsigned registrationCalls=0,ruleWrites=0,legacyReturns=0,ownerEvents=0;
 unsigned showCalls=0,steamUiChanges=0;bool steamUiLastTicketPresent=false,ownerEventAfterClear=false;
bool durableSucceeds=true;unsigned durableWrites=0,downloadCancels=0,publicationNotifications=0;
bool integrated=false,visibleWindow=true,refocusSucceeds=true,returnSucceeds=true,usable=true,identityKnown=true,blocked=false,paused=false;
std::atomic<bool> isolationSettingsOn{true};bool uiAuthority=true,radialOwner=false,childOwner=false;unsigned uiGateReads=0,deviceWorkRequests=0;
HWND window(std::uintptr_t value){return reinterpret_cast<HWND>(value);}
HWND foreground=window(44);
std::wstring identityPath=L"C:\\Apps\\desktop.exe",identityClass=L"DesktopApplication";
std::vector<HWND> focusAttempts;std::vector<bool> refocusAttempts;
std::map<UINT_PTR,UINT> timers;std::vector<std::pair<std::string,json>> events;
std::function<void()> onHide,onShow,onMemory,onPosition;
std::vector<std::wstring> whitelist,excludes;
std::vector<std::string> controls;std::vector<json>rows;std::vector<std::string>domains;
void check(bool okay,const char* label){++checks;if(!okay)throw std::runtime_error(label);}
DWORD getError()noexcept{return error;}void setError(DWORD e)noexcept{error=e;}
ULONGLONG tick()noexcept{error=777;return now;}
BOOL isWindow(HWND){controls.push_back("IsWindow");error=222;return valid;}
BOOL visible(HWND){controls.push_back("IsWindowVisible");return visibleWindow;}
BOOL iconic(HWND){controls.push_back("IsIconic");return FALSE;}
BOOL zoomed(HWND){controls.push_back("IsZoomed");return FALSE;}
HWND getForeground(){return foreground;}
HWND ancestor(HWND hwnd,UINT){return hwnd;}
DWORD processId(){return 9848;}
LONG_PTR windowLong(HWND,int){return 0;}
BOOL position(HWND,HWND order,int,int,int,int,UINT){if(order==HWND_NOTOPMOST&&onPosition){auto action=onPosition;action();}return TRUE;}
DWORD windowPid(HWND,DWORD* pid){*pid=identityPid;return 1;}
BOOL setForeground(HWND hwnd){focusAttempts.push_back(hwnd);if(returnSucceeds)foreground=hwnd;return returnSucceeds;}
BOOL killTimer(HWND,UINT_PTR timer){timers.erase(timer);return TRUE;}
UINT_PTR setTimer(HWND,UINT_PTR timer,UINT interval,TIMERPROC){timers[timer]=interval;return timer;}
HANDLE openProcess(DWORD,BOOL,DWORD){return reinterpret_cast<HANDLE>(std::uintptr_t(1));}
BOOL memoryInfo(HANDLE,PROCESS_MEMORY_COUNTERS* memory,DWORD){memory->WorkingSetSize=512ULL*1024*1024;if(onMemory)onMemory();return TRUE;}
BOOL closeHandle(HANDLE){return TRUE;}
BOOL updateWindow(HWND){return TRUE;}
BOOL postMessage(HWND,UINT,WPARAM,LPARAM){return TRUE;}
}
namespace ymcc::keyboardEvidence {
struct Snapshot {unsigned mask=0;DWORD pid=0;};
Snapshot read(){++fake::reads;fake::error=888;if(fake::throwRead)throw std::runtime_error("fake snapshot failure");return {17,fake::foregroundPid};}
json toJson(const Snapshot& s){return {{"asyncDownMask",s.mask},{"foregroundPidBefore",s.pid},{"foregroundPidAfter",s.pid},{"captureBeginTick",1000},{"captureEndTick",1000},{"sourceAttribution","unobserved"}};}
}
namespace ymcc {static bool summonPressHides(bool,bool,bool,bool,bool){fake::controls.push_back("summon-policy");return fake::hide;}}
#include "actual_focus_types.inc"
static HWND g_hwnd=reinterpret_cast<HWND>(std::uintptr_t(42));
static std::atomic<unsigned long long>g_summonGeneration{0};
static std::atomic<unsigned long long>g_summonRegistrationGeneration{0},g_gameRulesEpoch{1},g_inputHostEpochMirror{2},g_msiMainSourceRevision{3};
static std::atomic<ULONGLONG>g_focusLightProofNotBefore{0};
static std::atomic<unsigned>g_xinputTopologyEpoch{4};
static std::atomic<bool>g_exitRequested{false};
static std::atomic<bool>& g_controllerFrontendIsolationEnabled=fake::isolationSettingsOn;
enum class PowerLifecycle {Ready,Suspending,Resuming};
static std::atomic<PowerLifecycle>g_powerLifecycle{PowerLifecycle::Ready};
static unsigned long long fixturePower=7;
static unsigned long long currentPowerGeneration(){return fixturePower;}
namespace ymcc::gameInventory {static std::atomic<unsigned long long>invalidation{9};}
static std::atomic<std::shared_ptr<const NativeGameValveSnapshot>>g_gameValveObservation;
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
static int g_firstShowCmd=SW_SHOWNORMAL;
static ULONGLONG g_bClosePendingSince=0,g_bCloseReadyAt=0;
static constexpr UINT_PTR SUMMON_FOCUS_TIMER_ID=1,RETURN_GAME_FOCUS_TIMER_ID=2;
static std::atomic<bool>g_fullHeight{false},g_keyboardModifierEventGate{false};
static std::atomic<std::uint64_t>g_focusShortcutEvidenceFailures{0};
static FocusTargetSnapshot g_pendingSummonTarget;
static ymcc::idle::Accumulator g_inputIdleActivity;
static void actualFocusBeginReturnToPreviousWindow(ULONGLONG);
static void actualBringToFront(HWND,bool);
static void focusClearSession();
static bool focusLightIsolationCurrent();
static void focusBeginReturnToPreviousWindow(ULONGLONG now){fake::controls.push_back("return-request");if(fake::integrated)actualFocusBeginReturnToPreviousWindow(now);fake::foregroundPid=444;fake::error=333;}
static void bringToFront(HWND hwnd,bool maximize){fake::controls.push_back("bringToFront");if(fake::integrated)actualBringToFront(hwnd,maximize);fake::foregroundPid=9848;fake::error=444;}
static void ipc_emit(const char* name,const json& value){fake::controls.push_back("ipc_emit");fake::events.emplace_back(name,value);fake::error=555;}
static void queueSummonGameRegistration(const FocusTargetSnapshot&,unsigned long long){++fake::registrationCalls;fake::controls.push_back("queue-registration");fake::error=666;}
static void appendNativeLifecycleLog(const char*,const json&){fake::controls.push_back("lifecycle-log");fake::error=999;}
static void appendDomainLog(const char* domain,const char* event,const json& row){if(std::string(event)!="focus-shortcut-input")throw std::runtime_error("wrong event");fake::error=123;if(fake::throwSink)throw std::runtime_error("fake sink failure");fake::domains.emplace_back(domain);fake::rows.push_back(row);}
static void focusLosslessReturnCancel(){}
static bool focusLosslessReturnBegin(ULONGLONG){++fake::legacyReturns;return false;}
static bool focusLosslessReturnTimerTick(ULONGLONG){return false;}
static bool focusForegroundIsTransientOrShell(){return false;}
static bool refocusPreviousWindow(){++fake::legacyReturns;return false;}
inline void steamLightGamepadUiChanged(){++fake::steamUiChanges;fake::steamUiLastTicketPresent=bool(g_focusLightIsolationTicket.load());}
inline void msiNativeForegroundUiEvent(bool force){++fake::ownerEvents;++fake::deviceWorkRequests;fake::ownerEventAfterClear=force&&!g_focusSession.active&&!g_focusLightIsolationTicket.load()&&g_summonRegistrationGeneration==0;}
static bool actualGamepadUiOutputIsolationEligible();
static bool gamepadUiInputEligible(){return fake::uiAuthority;}
static bool gamepadUiOutputIsolationEligible(){++fake::uiGateReads;return actualGamepadUiOutputIsolationEligible();}
inline bool radialHasInputOwner(){return fake::radialOwner;}
inline bool customSteamLibraryInputSuppressed(){return fake::childOwner;}
static bool focusGameControlBlocked(){return fake::blocked;}
static bool focusPidIsManuallyPaused(DWORD){return fake::paused;}
static bool isUsableFocusWindow(HWND,DWORD){return fake::usable;}
static bool focusQueryProcessIdentity(DWORD,std::wstring* path,ULONGLONG* created){*path=fake::identityPath;*created=fake::created;return fake::identityKnown;}
static std::wstring focusWindowClass(HWND){return fake::identityClass;}
static FocusTargetSnapshot focusCaptureTarget(HWND hwnd){FocusTargetSnapshot target;target.hwnd=hwnd;target.pid=fake::identityPid;target.processCreated=fake::created;target.path=fake::identityPath;target.className=fake::identityClass;target.valid=hwnd!=nullptr;return target;}
static void focusReflowMainWindow(){}
static void showWindowAnimated(HWND,int,bool){++fake::showCalls;fake::visibleWindow=true;if(fake::onShow){auto action=fake::onShow;action();}}
static void hideWindowAnimated(HWND){fake::visibleWindow=false;if(fake::onHide){auto action=fake::onHide;action();}}
static bool refocusWebView(bool useAlt){fake::refocusAttempts.push_back(useAlt);if(fake::refocusSucceeds)fake::foreground=g_hwnd;return fake::refocusSucceeds;}
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
struct FixtureDownloadControl {struct Status {unsigned operation=1;};Status status(){return {};}void cancel(unsigned){++fake::downloadCancels;}};
static FixtureDownloadControl g_updateDownloadControl;
static const std::wstring MONITOR_GAME_TARGET_JSON=L"fake-monitor-snapshot";
static bool sgWriteMonitorSnapshotDurable(const std::wstring&,const std::string&){++fake::durableWrites;return fake::durableSucceeds;}
static json nativeGameTargetJson(const NativeGameValveSnapshot& target){return {{"generation",target.generation},{"pid",target.game.pid}};}
static void steamOriginalRecoveryTargetChanged(DWORD,ULONGLONG,const std::wstring&,unsigned long long){}
static void gamepadSerialPostEvent(const char*,const json&){++fake::publicationNotifications;}
static void mousePolicyGameContextChanged(){}
#define GetLastError fake::getError
#define SetLastError fake::setError
#define GetTickCount64 fake::tick
#define IsWindow fake::isWindow
#define IsWindowVisible fake::visible
#define IsIconic fake::iconic
#define IsZoomed fake::zoomed
#define GetForegroundWindow fake::getForeground
#define GetAncestor fake::ancestor
#define GetCurrentProcessId fake::processId
#define GetWindowLongPtrW fake::windowLong
#define SetWindowPos fake::position
#define GetWindowThreadProcessId fake::windowPid
#define SetForegroundWindow fake::setForeground
#define KillTimer fake::killTimer
#define SetTimer fake::setTimer
#define OpenProcess fake::openProcess
#undef GetProcessMemoryInfo
#define GetProcessMemoryInfo fake::memoryInfo
#define CloseHandle fake::closeHandle
#define UpdateWindow fake::updateWindow
#define PostMessageW fake::postMessage
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
#undef GetAncestor
#undef GetCurrentProcessId
#undef GetWindowLongPtrW
#undef SetWindowPos
#undef GetWindowThreadProcessId
#undef SetForegroundWindow
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
 fake::controls.clear();fake::rows.clear();fake::domains.clear();fake::reads=0;fake::error=12345;fake::foregroundPid=444;fake::valid=true;fake::hide=false;fake::throwRead=fake::throwSink=false;
 fake::now=1000;fake::created=101;fake::identityPid=444;fake::registrationCalls=fake::ruleWrites=fake::legacyReturns=fake::ownerEvents=0;
 fake::showCalls=fake::steamUiChanges=0;fake::steamUiLastTicketPresent=fake::ownerEventAfterClear=false;
 fake::durableSucceeds=true;fake::durableWrites=fake::downloadCancels=fake::publicationNotifications=0;g_gameValvePublishedGeneration=0;g_updateGameDetected=true;g_updateGameObservationTick=0;
 fake::integrated=false;fake::visibleWindow=fake::refocusSucceeds=fake::returnSucceeds=fake::usable=fake::identityKnown=true;fake::blocked=fake::paused=false;
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
 observation->qualificationRulesEpoch=g_gameRulesEpoch.load();observation->qualificationPower=fixturePower;observation->qualificationInventory=ymcc::gameInventory::invalidation.load();g_gameValveObservation.store(observation);
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
 for(unsigned counterexample=0;counterexample<6;++counterexample){
  reset();publishProof();fake::check(focusLightIsolationBegin(),"epoch setup ticket rejected");
  switch(counterexample){
   case 0:++fixturePower;break;
   case 1:++g_gameRulesEpoch;break;
   case 2:++g_inputHostEpochMirror;break;
   case 3:++g_xinputTopologyEpoch;break;
   case 4:++ymcc::gameInventory::invalidation;break;
   case 5:fake::now=31001;break;
  }
  fake::check(!focusLightIsolationCurrent()&&!g_focusLightIsolationTicket.load(),"changed epoch or expired proof retained ticket");
  publishProof();fake::check(!focusLightIsolationCurrent(),"fresh proof resurrected revoked ticket without begin");
 }
}
static void lightWrapper(){
 startLight();const auto candidate=summonEvent();
 fake::check(candidate["isolationMode"]=="light-no-game"&&candidate["skipGameRegistration"]==true&&candidate["valid"]==true&&candidate["pid"]==444,"light candidate lost mode or captured identity");
 fake::check(fake::registrationCalls==0,"light summon queued registration");
 fake::check(fake::refocusAttempts==std::vector<bool>{true}&&g_focusSession.lightForegroundAcquired,"explicit light summon lost original foreground compatibility");
 fake::check(g_focusSession.target.hwnd==fake::window(44)&&g_focusSession.strictReturnOnly,"light session lost exact return origin");
 const auto sequence=g_focusSessionSequence;fake::check(focusPrepareSummonSession()&&g_focusSessionSequence==sequence,"repeated focused prepare replaced light session");
}
static void lightCancelledDuringBring(){
 reset();fake::integrated=true;publishProof();fake::onShow=[] {++g_inputHostEpochMirror;};
 nativeYmccShortcutSummon(focusCaptureTarget(fake::foreground),true);
 fake::check(!g_focusSession.active&&g_focusSession.strictReturnOnly&&fake::refocusAttempts.empty(),"cancelled bring kept or focused stale light session");
 fake::check(summonEvent()["skipGameRegistration"]==true&&fake::registrationCalls==0,"cancelled light selection queued registration");
}
static void refocusUserChoice(){
 startLight();fake::foreground=fake::window(55);fake::check(!focusLightMayRefocus(fake::now),"acquired focus stole user choice");
 focusLightForegroundChanged();fake::check(!g_focusSession.active&&!g_focusLightIsolationTicket.load()&&g_focusSession.strictReturnOnly,"foreground departure failed to cancel light session");
}
static void refocusEntering(){
 startLight(false);fake::check(focusLightMayRefocus(fake::now),"origin denied bounded entering retry");
 fake::now=1079;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);fake::check(fake::refocusAttempts.size()==1,"entering retry bypassed 80ms spacing");
 fake::now=1080;actualFocusTimer(SUMMON_FOCUS_TIMER_ID);fake::check(fake::refocusAttempts==std::vector<bool>{true,false},"light timer repeated explicit-entry Alt fallback");
 fake::foreground=nullptr;fake::check(focusLightMayRefocus(fake::now),"empty transient foreground denied entering retry");
 fake::foreground=fake::window(55);fake::check(!focusLightMayRefocus(fake::now),"entering retry stole third-party focus");
 focusLightForegroundChanged();fake::check(!g_focusSession.active,"entering foreground departure failed to cancel");
 startLight(false);fake::now=g_focusSession.summonDeadline;fake::check(!focusLightMayRefocus(fake::now),"summon deadline admitted retry");
 actualFocusTimer(SUMMON_FOCUS_TIMER_ID);fake::check(!g_focusSession.active&&fake::timers.empty(),"expired summon retained retry timer");
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
static void latePowerReady(){
 startLight(false);g_powerLifecycle=PowerLifecycle::Resuming;++fixturePower;
 fake::check(!focusLightIsolationCurrent(),"sleep retained ticket");g_powerLifecycle=PowerLifecycle::Ready;publishProof();
 const auto attempts=fake::refocusAttempts.size();actualLateReadyFocus();actualFocusTimer(SUMMON_FOCUS_TIMER_ID);
 fake::check(fake::refocusAttempts.size()==attempts&&!g_focusSession.active&&!g_focusLightIsolationTicket.load(),"late power Ready revived old ticket");
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
   {"ticket-source",ticketSource},{"ticket-epochs",ticketEpochs},{"light-wrapper",lightWrapper},{"light-bring-cancel",lightCancelledDuringBring},
   {"refocus-user-choice",refocusUserChoice},{"refocus-entering",refocusEntering},{"late-ready",lateReady},{"ui-recheck",uiRecheck},{"late-power-ready",latePowerReady},
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
