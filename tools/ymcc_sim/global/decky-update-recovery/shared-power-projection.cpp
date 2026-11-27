// Actual Native current-power publisher/message/timer and MSI scalar consumer.
// Process/device/sensor/Steam/window/transport leaves are explicit inert ports.
#define NOMINMAX
#include <winsock2.h>
#include "steam_cssloader_guard.h"
#include <Windows.h>
#include <fstream>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
#include "sleep_power_state.h"
#include "sleep_cancel_ready.h"
#include "sleep_wake_admission.h"
#include "sleep_power_dispatch.h"
#include "sleep_power_revocation.h"
#include "sleep_power_observation.h"
#include "fan_physical_wake.h"
#include "fan_resume_owner.h"
#include "recovery_power_boundary.h"
#include "recovery_resume_commit.h"
#include "steam_original_recovery_policy.h"
#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>
namespace decky_read_probe {
static std::atomic<ULONGLONG> fixtureTick{10000};
static std::atomic<bool> holdFinite{false}, watchIdle{false}, waitFailed{false};
static HANDLE entered=nullptr,release=nullptr,idle=nullptr;
static ULONGLONG WINAPI now() noexcept{return fixtureTick.load();}
static DWORD WINAPI wait(DWORD n,const HANDLE* hs,BOOL all,DWORD ms) noexcept{
 if(ms!=INFINITE&&holdFinite.exchange(false)){
  SetEvent(entered);
  if(WaitForSingleObject(release,4000)!=WAIT_OBJECT_0){waitFailed=true;return WAIT_FAILED;}
  return WAIT_TIMEOUT;
 }
 if(ms==INFINITE&&watchIdle.load())SetEvent(idle);
 return ::WaitForMultipleObjects(n,hs,all,ms);
}
}
#define GetTickCount64 decky_read_probe::now
#define WaitForMultipleObjects decky_read_probe::wait
#include "steam_ui_mode_observer.h"
#undef WaitForMultipleObjects
#undef GetTickCount64
namespace ymcc::steamcommand {class AvailabilityEvent;}
using json=nlohmann::json;
static ymcc::steamcss::Runtime g_steamCssGuard;
static struct {ymcc::steamsession::Session value; auto sessionSnapshot(){return value;}} g_steamSettingsObserver;
static std::atomic<unsigned> cssReadyDiagnostics{0};
static bool cssDiagnosticThrow=false;
static void deckySidebarDiagnostic(const char*,const json&,bool){++cssReadyDiagnostics;if(cssDiagnosticThrow)throw std::runtime_error("inert CSS diagnostic fault");}
static unsigned deckyFirstOffers=0,deckyFirstBindings=0,deckyFirstAdmissions=0,deckyFirstModeRequests=0;
static std::function<bool()> deckyFirstAdmission;
static bool deckyFirstRequestThrows=false;
[[maybe_unused]] static struct {
 struct View{bool enabled=false;};
 View state;
 bool fenced=false;
 unsigned connectionChecks=0;
 bool exitFenced()const{return fenced;}
 View snapshot()const{if(deckyFirstAdmission){auto admit=std::exchange(deckyFirstAdmission,{});(void)admit();}return state;}
 bool requestPreparationRefresh(unsigned long long){++deckyFirstOffers;if(deckyFirstRequestThrows)throw std::runtime_error("accepted Decky preparation fault");return true;}
 bool requestBindingRefresh(unsigned long long){++deckyFirstBindings;return true;}
 bool requestLaunchAdmissionRefresh(){++deckyFirstAdmissions;return true;}
 bool requestConnectionCheck(){if(fenced||!state.enabled)return false;++connectionChecks;return true;}
} g_deckySidebarRuntime;
[[maybe_unused]] static struct {
 std::vector<ymcc::sleeppower::SharedPowerV1> requests;
 ymcc::steamui::ModeObserver* actual=nullptr;
 std::function<void()> beforeDeckyAdmission;
 bool forwardOrdinary=false;
 void requestRefresh(const ymcc::steamsession::Session& session){++deckyFirstModeRequests;if(actual&&forwardOrdinary)actual->requestRefresh(session);}
 bool requestDeckyResumeRefresh(const ymcc::steamsession::Session& session,const ymcc::sleeppower::SharedPowerV1& power,std::string* reusedEpoch=nullptr){
  if(beforeDeckyAdmission){auto before=std::move(beforeDeckyAdmission);beforeDeckyAdmission={};before();return false;}
  requests.push_back(power);
  if(!actual)return true;
  return []<class Observer>(Observer& observer,const auto& target,const auto& scope,std::string* reused){
   if constexpr(requires{observer.requestDeckyResumeRefresh(target,scope,reused);})return observer.requestDeckyResumeRefresh(target,scope,reused);
   else return observer.requestDeckyResumeRefresh(target,scope);
  }(*actual,session,power,reusedEpoch);
 }
} g_steamModeObserver;
@@POWER_ENUM@@
@@MESSAGES@@
static std::atomic<bool> g_exitRequested{false},g_hardwareWriteGate{true},g_inputReady{true};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned long long> g_powerGeneration{10},g_resumeReadyGeneration{10};
static std::atomic<uint64_t> g_sharedPowerPublicationRevision{10},g_sharedPowerSuspendSerial{10};
static std::atomic<bool> g_sharedPowerProjectionPending{false},g_sleepPowerRedeliveryArmed{false},g_sensorSuspendCallbackActive{false};
static struct {bool isolated=false;}g_aiFanMockSession;
static ymcc::sleeppower::RevocationMirrors g_sharedPowerRevocations;
static ymcc::sleeppower::RecoveryBoundaryMirror g_recoveryPowerBoundary;
static unsigned readerIngress=0;
static bool publishIngressCompletion=false;
static uint64_t serial=100;
struct ProjectionFixtureDispatch:ymcc::sleeppower::Dispatch {
 using Dispatch::Dispatch;
 auto sampledSequence(){
  const auto observed=observedSequence();
  if(readerIngress){--readerIngress;callback(PBT_APMRESUMEAUTOMATIC,++serial,[](const auto&){return true;});}
  return observed;
 }
};
static ProjectionFixtureDispatch g_sleepPowerDispatch{[](void*)noexcept{g_sharedPowerRevocations.revoke();g_recoveryPowerBoundary.revoke();},nullptr};
static HWND g_hwnd=reinterpret_cast<HWND>(static_cast<UINT_PTR>(1));
static void projectSharedPowerLifecycle(HWND);
static LRESULT productionProjectionWindow(HWND,WPARAM);
static std::optional<WPARAM> projectionQueue;
static std::vector<std::pair<WPARAM,LPARAM>> rawReceiptQueue;
static std::vector<ymcc::sleeppower::Dispatch::Receipt>* rawReceiptClaims=nullptr;
static bool failProjectionPost=false,failProjectionTimer=false;
static void publishSharedPowerLifecycle(const char*);
static void sleepPowerArmRedelivery(HWND);
struct Call{std::string name;uint64_t generation,publication;};
static std::vector<Call> calls;
static std::vector<json> emitted,cssObservations;
static std::vector<std::string> diagnostics;
static std::string faultStage,edge;
static bool injected=false,failPost=false,throwDiagnostic=false;
static unsigned ayaAttempts=0;
static bool refuseAya=false;
static unsigned arms=0,posts=0,msiAttempts=0;
static void sgCompleteSharedWakeClassification(unsigned long long);
static void stage(const char* name,uint64_t generation){
 calls.push_back({name,generation,g_sharedPowerPublicationRevision.load()});
 if(injected||faultStage!=name)return;
 injected=true;
 if(edge=="throw")throw std::runtime_error("accepted leaf failed");
 if(edge=="classification")sgCompleteSharedWakeClassification(g_powerGeneration.load());
 else if(edge=="pending")g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});
 else if(edge=="reader-ingress")readerIngress=6;
 else if(edge=="exit")g_exitRequested=true;
 else if(edge=="advance"){
  ++g_powerGeneration;++g_sharedPowerSuspendSerial;g_resumeReadyGeneration=g_powerGeneration.load();
  publishSharedPowerLifecycle("reentrant-new-native-generation");
  if(faultStage=="radial-create")projectSharedPowerLifecycle(g_hwnd);
 }else if(edge=="phase"){
  g_powerLifecycle=PowerLifecycle::Suspended;g_hardwareWriteGate=false;
  publishSharedPowerLifecycle("reentrant-suspend");
 }else if(edge=="reentrant"){
  g_sharedPowerProjectionPending=true;projectSharedPowerLifecycle(g_hwnd);
 }
}
static unsigned count(const char* name){return static_cast<unsigned>(std::count_if(calls.begin(),calls.end(),[&](const auto& c){return c.name==name;}));}
// Shared UI presentation is an explicit inert leaf in this power fixture.
static struct {void sharedPowerChanged(const ymcc::sleeppower::SharedPowerV1& power){stage("ui-presentation",power.generation);}}g_gameOverlay;
static ymcc::sleeppower::CancellationReady g_sgCancellationReady;
static std::atomic<ULONGLONG> g_sgLatestSleepEntryFileTime{0};
static unsigned long long currentPowerGeneration();
@@READY_RECEIPT@@
// Inject actual Dispatch ingress between the unmodified reader's scalar samples.
#define observedSequence sampledSequence
@@READER@@
#undef observedSequence
@@PHASE_NAME@@
@@ACCESSORS@@
// Unrelated media-download and startup-resume business are inert in this fixture.
namespace ymcc::sleeppower::task {
static bool allowed() noexcept {SharedPowerV1 power;return captureSharedPowerLifecycle(power);}
}
static void notifySharedMediaDownloadsPower() noexcept {}
static void sgOfferStartupResume(const ymcc::sleeppower::SharedPowerV1&) {}
static void sgNotifyParkedWakeGameRecovery() {stage("game-recovery-power",g_powerGeneration.load());}
static std::atomic<unsigned long long> g_lastPowerResumeCommitTick{0};
static ymcc::sleeppower::RecoveryResumeCommit g_recoveryResumeCommit;
static ULONGLONG readyFixtureNow=100000;
static bool readyReenter=false,readyNestedResult=true;
static unsigned readySignals=0,readyCloseCalls=0;
static std::vector<std::string> readyCommitScenarios;
static bool commitPowerResume(unsigned long long,const char*);
static ULONGLONG readyFixtureTick(){if(readyReenter){readyReenter=false;const bool open=g_hardwareWriteGate.exchange(false);readyNestedResult=commitPowerResume(g_powerGeneration.load(),"fixture-reentrant-already-ready");g_hardwareWriteGate.store(open);}return readyFixtureNow;}
#define GetTickCount64 readyFixtureTick

static bool fixtureFanActive=false;
static bool fixtureFanIngress=false;
static unsigned fixtureFanReadySignals=0;
static unsigned fixtureFanStartupCloseStarts=0;
static std::uint64_t fixtureFanIntent=1;
static std::atomic<std::uint64_t> g_fanWritePowerRevision{1},g_fanResumeManualOperation{0};
static auto fixtureFanObservation=std::make_unique<ymcc::fan_wake::Observation>();
static auto fixtureFanRequests=std::make_unique<ymcc::fan_resume::RequestOwner>();
#define g_fanPhysicalWake (*fixtureFanObservation)
#define g_fanResumeRequestOwner (*fixtureFanRequests)
static std::vector<ymcc::fan_resume::RequestTicket> fixtureFanStarts;
static json fixtureMergedFanSnapshots=json::array();
static json fixtureColdFanReplay;
static json fixtureRawReceiptFanReplay;
static json fixtureColdFanOffReplay;
[[maybe_unused]] static std::atomic<unsigned long long> g_webviewGeneration{1};
[[maybe_unused]] static ULONGLONG g_webviewNavigationId=1;
[[maybe_unused]] static ULONGLONG g_gyroTelemetryDocument=1;
static bool fanActivityIntentPresent(){
 if(fixtureFanIngress){fixtureFanIngress=false;readerIngress=6;}
 return fixtureFanActive;
}
static bool fanResumeActivityRevision(std::uint64_t& value){value=fixtureFanIntent;return fixtureFanActive;}
static bool fanManualActivityRevision(std::uint64_t& value){value=fixtureFanIntent;return true;}
static void fanHostStartResumeTicket(ymcc::fan_resume::RequestTicket);
// Fan's own fixture exercises the Close worker. This projection fixture has no Host Close port.
[[maybe_unused]] static void fanHostContinueUnstartedClose(unsigned long long) {}
[[maybe_unused]] static void fanHostQueueSuspend(const char* reason,unsigned long long generation) {
 if(!reason||std::strcmp(reason,"startup-fan-wake-close")!=0||generation!=g_fanPhysicalWake.generation(currentPowerGeneration()))
  throw std::logic_error("unexpected private Fan startup Close request");
 if(g_fanPhysicalWake.admitClose(generation)==ymcc::fan_wake::Observation::CloseAdmission::StartWorker)
  ++fixtureFanStartupCloseStarts;
}
static void appendFanLifecycleLog(const char*,const json&){}
static void ipc_emit(const char*,const json&);
static bool sharedSleepWakeClassified(unsigned long long generation) noexcept;
@@FAN_RESUME@@
static std::vector<std::unique_ptr<FanHttpPowerAdmission>> fixtureFanScopes;
static void fanHostStartResumeTicket(ymcc::fan_resume::RequestTicket ticket){
 auto power=std::make_unique<FanHttpPowerAdmission>();power->resume=true;
 if(!power->captureCurrent())throw std::runtime_error("Fan starter lacks original Native admission");
 fixtureFanStarts.push_back(ticket);fixtureFanScopes.push_back(std::move(power));
 stage("fan-original-owner",currentPowerGeneration());
}
static void fixtureFanReset(){
 fixtureFanRequests->close();fixtureFanScopes.clear();fixtureFanStarts.clear();
 fixtureFanRequests=std::make_unique<ymcc::fan_resume::RequestOwner>();
 fixtureFanObservation=std::make_unique<ymcc::fan_wake::Observation>();
 g_sgLatestSleepEntryFileTime=0;
 fixtureFanActive=true;fixtureFanIngress=false;fixtureFanReadySignals=0;fixtureFanStartupCloseStarts=0;fixtureFanIntent=1;g_fanWritePowerRevision=1;g_fanResumeManualOperation=0;
}
static void fixtureFanReturnOld(){
 const auto old=fixtureFanStarts.back();
 const auto next=g_fanResumeRequestOwner.finishRequest(old,readyFixtureNow,false);
 if(next)fanHostStartResumeTicket(next);
}

static bool sgShouldKeepInputDuringStandby(){return false;}
static void sleepRecoveryAutoCloseSuspendEpisode(){++readyCloseCalls;stage("close-episode",g_powerGeneration.load());}
static void sgStaticScanSchedule(bool){}
static void sgNotifyWork(){}
template<class... Args>static void traceLog(const char* format,Args...){if(std::string(format).starts_with("power gate opened"))stage("gate-open",g_powerGeneration.load());}
template<class... Args>static void traceLogTry(const char* format,Args... args){traceLog(format,args...);}
static bool sgResumeClassificationOwned(unsigned long long generation);
static void sgCompleteSharedWakeClassification(unsigned long long generation);
namespace SleepSteamRecovery {struct CaptureRequest;}
namespace SteamOriginalRecovery {
using Trigger=ymcc::steamOriginal::Trigger;
using Identity=ymcc::steamOriginal::Identity;
using Scope=ymcc::steamOriginal::Scope;
struct StartupFreshWait;struct StartupCommandWait;
@@RECOVERY_PENDING@@
struct ActiveGroupRequest;struct StartupFreshRequest;struct StartupFreshWait;
struct Control;
// Running-loss observation and Startup continuations are inert external ports here.
// No resource-bearing value may enter this projection fixture.
struct RunningLossNotice {};
template<class T>static bool currentStartupContinuation(const std::shared_ptr<T>& c){
 if(c->startupCommandWait||c->mutationAvailability||c->startupFreshRequest||c->activeGroupRequest||c->runningLossNotice||
    std::any_of(c->startupFreshWaits.begin(),c->startupFreshWaits.end(),[](const auto& wait){return static_cast<bool>(wait);}))
  throw std::runtime_error("INERT_STARTUP_RESOURCE_PORT_REQUIRES_EMPTY_INPUT");
 return false;
}
[[maybe_unused]] static void cancelStartupFreshLocked(const std::shared_ptr<Control>&){}
// The theme projection fixture owns no game startup command/fresh request.
// Reject accidental resource-bearing input rather than pretending to retire it.
struct CommandRetirement {};
template<class T>static void cancelStartupFreshLocked(const std::shared_ptr<T>& c,CommandRetirement&){
 if(c->startupCommandWait||c->mutationAvailability||c->startupFreshRequest||c->activeGroupRequest||
    std::any_of(c->startupFreshWaits.begin(),c->startupFreshWaits.end(),[](const auto& wait){return static_cast<bool>(wait);}))
  throw std::runtime_error("INERT_STARTUP_RESOURCE_PORT_REQUIRES_EMPTY_INPUT");
}
static std::shared_ptr<Control> readyFixtureControl;
static std::shared_ptr<Control> current(){return readyFixtureControl;}
}
namespace SleepRecoveryAuto {static void powerReady(unsigned long long generation){++readySignals;stage("sleep-recovery",generation);}}
static void sleepRecoveryAutoPowerReady(unsigned long long);
static void steamOriginalRecoveryPowerReady(unsigned long long);
static BOOL readyFixtureSetEvent(HANDLE);

static bool sgHibernateAvailable(){return false;}
@@SNAPSHOT@@
static json sharedPowerLifecycleSnapshot(){stage("snapshot",g_powerGeneration.load());return productionSharedPowerLifecycleSnapshot();}
namespace ymcc::batterycharge {static void notifySystemPower(){stage("battery",g_powerGeneration.load());}}
static void inputCaptureResumeSensorsForPower(){stage("sensors",g_powerGeneration.load());}
static std::mutex g_msiNativeForegroundPowerMx;
static std::atomic<bool> g_msiNativeForegroundPowerHold{true},g_msiNativeForegroundResumePending{true},g_msiNativeForegroundPowerTransitionObserved{true};
static std::atomic<bool> g_msiNativeForegroundWorkPowerDeferred{false};
static uint64_t g_msiNativeForegroundLastReadySuspendSerial=0;
static std::atomic<uint64_t> g_msiNativeForegroundLastResumeGeneration{0};
static bool nativeForegroundFamilyAllowed(){return true;}
[[maybe_unused]] static void msiNativeForegroundUiEvent(bool,bool=false,const ymcc::sleeppower::SharedPowerV1* power=nullptr){
 if(power&&!sharedPowerLifecycleCurrent(*power))std::abort();
 stage("msi",g_powerGeneration.load());
}
[[maybe_unused]] static void msiNativeForegroundQueue(){stage("msi",g_powerGeneration.load());}
@@MSI_SHARED_READY_PORTS@@
@@MSI_SHARED_READY@@
[[maybe_unused]] static std::atomic<uint64_t> g_msiNativeForegroundPowerHoldCommitTick{0},g_msiNativeForegroundSuspendedGeneration{0};
@@MSI_RAW_WAKE@@
static bool msiNativeForegroundSharedPowerReady(const ymcc::sleeppower::SharedPowerV1& power){
 ++msiAttempts;
 if(faultStage=="msi-ready-admission"){
  if(edge=="serial")++g_sharedPowerSuspendSerial;
  if(edge=="notification"){
   const auto receipt=g_sleepPowerDispatch.window(PBT_APMRESUMESUSPEND,++serial);
   g_sleepPowerDispatch.complete(receipt,++serial);
  }
 }
  return productionMsiNativeForegroundSharedPowerReady(power);
}
@@FOCUS_TICKET@@
static ymcc::sleeppower::SharedPowerV1 g_radialOverlayPower;
static bool g_radialOverlayCreating=false,g_radialOverlayRetiring=false,g_radialEnabled=true;
static std::atomic<bool> g_radialReturning{false};
namespace ymcc::radial {
 static bool userAvailable(){return true;}
 template<class Json>static Json unavailableResponse(){std::abort();}
}
static bool customSteamLibraryInputSuppressed(){return false;}
struct RadialProjectionPort {
 bool open=false;
 bool active()const{return open;}
 json create(const std::string& nonce,const json&,const std::string&,int,int){
  open=true;stage("radial-create",g_powerGeneration.load());return {{"id",1},{"nonce",nonce}};
 }
 void shutdown(){open=false;stage("radial-close",g_powerGeneration.load());}
};
static RadialProjectionPort g_radialOverlay;
[[maybe_unused]] static void radialInputIsolationInvalidate(){g_radialReturning=false;stage("radial-invalidate",g_powerGeneration.load());}
@@RADIAL_PUBLIC_POWER@@
static std::atomic<bool> g_focusIsolationPrepared{false},g_focusIsolationGameOpen{false};
static std::pair<std::uint64_t,std::uint64_t> g_focusIsolationPowerPeriod;
static std::pair<std::uint64_t,std::uint64_t> g_focusSessionPowerPeriod;
static struct FocusSessionPort{bool active=false,lightNoGame=false,returning=false;ULONGLONG returnStarted=0,returnDeadline=0;} g_focusSession;
static std::uint64_t g_focusSessionSequence=0;
static bool focusTimerArmed=false;
static constexpr WPARAM RETURN_GAME_FOCUS_TIMER_ID=1;
static std::atomic<std::shared_ptr<const FocusLightIsolationTicket>> g_focusLightIsolationTicket;
static std::shared_ptr<const bool> g_focusLightFallbackObservation;
static std::atomic<ULONGLONG> g_focusLightProofNotBefore{0};
static void steamLightGamepadEndIsolation(){stage("focus-end",g_powerGeneration.load());}
static void focusClearSession(){g_focusSessionPowerPeriod={};g_focusSession={};++g_focusSessionSequence;focusTimerArmed=false;stage("focus-clear",g_powerGeneration.load());}
static bool focusLosslessSharedPowerChanged(const ymcc::sleeppower::SharedPowerV1&){return true;} // Exercised by the original Lossless fixture.
static bool taskViewSharedPowerChanged(const ymcc::sleeppower::SharedPowerV1&){return true;} // Exercised by the Task View wiring fixture.
@@FOCUS_ISOLATION@@
static auto focusIsolationSharedPowerChanged(const ymcc::sleeppower::SharedPowerV1& power){
 stage("focus-isolation",power.generation);
 return productionFocusIsolationSharedPowerChanged(power);
}
static bool focusLightReturnTick(ULONGLONG){return false;}
static bool focusLosslessReturnTimerTick(ULONGLONG){return false;}
static bool focusForegroundIsTransientOrShell(){return true;}
static bool refocusPreviousWindow(){stage("focus-return-action",g_powerGeneration.load());return false;}
@@FOCUS_RETURN_TIMER@@
namespace ymcc::lossless {struct ProjectionMonitor {void powerChanged() noexcept {}};static ProjectionMonitor& runtimeMonitor(){static ProjectionMonitor monitor;return monitor;}}
namespace SteamControllerCursor {static void power(const ymcc::sleeppower::SharedPowerV1& power){stage("cursor",power.generation);}}
static bool dropInputReadyReceipt=false;
static std::vector<ymcc::sleeppower::SharedPowerV1> inputReadyReceipts;
static json jointReadyObservation;
namespace ymcc::inputHost::msiPowerWork {
 static void readyDelivery(const sleeppower::SharedPowerV1& power){
  if(!dropInputReadyReceipt)inputReadyReceipts.push_back(power);
  stage("input-host-rearm",power.generation);
 }
}
namespace ymcc::screenpads {static void ownerSharedPowerChanged(const ymcc::sleeppower::SharedPowerV1&){}}
[[maybe_unused]] static void appendNativeLifecycleLog(const char* name,const json& detail){
 if(throwDiagnostic)throw std::runtime_error("diagnostic leaf failed");
 if(std::string(name)!="power-projection-consumer-failed")throw std::runtime_error("unexpected diagnostic");
 diagnostics.push_back(detail.at("consumer").get<std::string>());
}
static void monitorSharedPowerChanged(){} // Independent monitor consumer is executed by its original monitor fixtures.
static void ipc_emit(const char* name,const json& value){if(std::string(name)=="fan.resume-ready"){++fixtureFanReadySignals;return;}if(std::string(name)=="cpu.undervolt.state"){stage("cpu-undervolt-state",g_powerGeneration.load());return;}if(std::string(name)!="power.lifecycleChanged")throw std::runtime_error("unexpected event");emitted.push_back(value);stage("ipc",g_powerGeneration.load());}
struct UiCostIpcScope{explicit UiCostIpcScope(const char*){}};
struct FixtureIpcChild{bool view=true;};
static FixtureIpcChild fixtureIpcChild;
static bool g_view=true;
static std::vector<std::pair<int,FixtureIpcChild*>> g_children;
static unsigned sharedFactMainPosts=0,sharedFactChildPosts=0,sharedFactRefusals=0;
static json sharedFactChildLast;
[[maybe_unused]] static bool ipc_post_json(int child,const json& message,const std::string&,std::optional<std::wstring>*){
 if(child){++sharedFactChildPosts;sharedFactChildLast=message.at("data");return true;}
 ++sharedFactMainPosts;
 if(sharedFactRefusals){--sharedFactRefusals;return false;}
 ipc_emit(message.at("event").get_ref<const std::string&>().c_str(),message.at("data"));return true;
}
static constexpr UINT_PTR acdcFixtureTimerId=0xA104;
static constexpr UINT_PTR cpuFixtureTimerId=0xA130;
static bool cpuTrialTimer=false;
static bool refuseAcDcTimer=false;
static unsigned acdcTimerAttempts=0;
static UINT_PTR fixtureSetTimer(HWND,UINT_PTR id,UINT,TIMERPROC){
 if(id==acdcFixtureTimerId){++acdcTimerAttempts;return refuseAcDcTimer?0:id;}
 if(id==cpuFixtureTimerId){cpuTrialTimer=true;return id;}
 ++arms;return failProjectionTimer?0:1;
}
static BOOL fixtureKillTimer(HWND,UINT_PTR id){if(id==cpuFixtureTimerId)cpuTrialTimer=false;return TRUE;}
static constexpr UINT WM_STEAM_WALLPAPER_FOREGROUND=WM_APP+827;
static std::atomic<bool> g_steamWallpaperForegroundPending{false};
static std::mutex g_steamWallpaperForegroundPostMx;
static bool refuseWallpaperPost=false;
static unsigned wallpaperAttempts=0,wallpaperPosts=0;
static BOOL fixturePostMessage(HWND window,UINT message,WPARAM w,LPARAM l){
 if(message==WM_STEAM_WALLPAPER_FOREGROUND){++wallpaperAttempts;if(refuseWallpaperPost)return FALSE;++wallpaperPosts;return TRUE;}
 if(message==WM_POWER_RESUME_COMMIT)return failPost?FALSE:TRUE;
 if(message==kSleepPowerForwardMessage){
  if(window!=g_hwnd||!l)throw std::runtime_error("raw receipt post lost window or sequence");
  rawReceiptQueue.emplace_back(w,l);return TRUE;
 }
 if(message!=kSharedPowerStateMessage)throw std::runtime_error("unexpected post");
 ++posts;if(failPost||failProjectionPost)return FALSE;projectionQueue=w;return TRUE;
}
static LRESULT fixtureSendMessage(HWND h,UINT message,WPARAM w,LPARAM l){
 if(message==WM_POWER_RESUME_COMMIT)return 0;
 if(message==kSharedPowerStateMessage)return productionProjectionWindow(h,w);
 if(message==kSleepPowerForwardMessage){
  const auto receipt=g_sleepPowerDispatch.forwarded(static_cast<uint64_t>(l),static_cast<ULONG>(w),++serial);
  g_sleepPowerDispatch.complete(receipt,++serial);
  if(rawReceiptClaims){
   if(receipt)rawReceiptClaims->push_back(receipt);
   if(g_sleepPowerDispatch.hasPending())sleepPowerArmRedelivery(h);
  }
  if(receipt&&publishIngressCompletion)publishSharedPowerLifecycle("ordered-power-delivery-complete");return 0;
 }
 throw std::runtime_error("unexpected retry business message");
}
#define SetTimer fixtureSetTimer
#define KillTimer fixtureKillTimer
#define PostMessageW fixturePostMessage
#define SendMessageW fixtureSendMessage
enum class YmccFamily {Unknown,AsusRogAlly,AsusRogAllyX,AsusRogAllyClassic,AsusRogAllyXClassic};
static struct {YmccFamily family=YmccFamily::Unknown;std::string deviceClass;} g_machineIdentity;
static json cpuTrialDisk=json::object();
static unsigned cpuTrialWrites=0;
static std::mutex cpuTrialSettingsMutex;
struct SettingsFileGuard {std::lock_guard<std::mutex> lock{cpuTrialSettingsMutex};};
static const std::wstring YM_SETTINGS_FILE=L"memory-only";
static json ymSettingsRead(){return cpuTrialDisk;}
static json ymSettingsReadUnlocked(){return cpuTrialDisk;}
static std::string ymSettingsReadRaw(const std::wstring&){return cpuTrialDisk.dump();}
static bool ymSettingsHasCompatibleWriteBase(const std::string&,const json&){return true;}
static bool ymSettingsWriteDocumentUnlocked(const std::string& document,void*,const json*){
 cpuTrialDisk=json::parse(document);++cpuTrialWrites;return true;
}
#include "cpu_undervolt_trial_native.h"
static_assert(cpuFixtureTimerId==CPU_UV_TRIAL_TIMER);
@@FAN_WAKE_DELIVERY@@
static void inputCaptureSensorThreadRetryPowerWake(HWND){} // Sensor owner is exercised by the original sensor-power-only selector.
static void inputCaptureSensorThreadRecoverReturned(HWND){} // Actual STA recovery executes in sleep.gyro-power power-wake.
@@DELIVERY@@
#define SetEvent readyFixtureSetEvent
static constexpr UINT_PTR SG_RESUME_CLASSIFY_TIMER_ID=0x7A71;
static std::atomic<ULONGLONG> g_sgLastPowerButtonSleepIntentFileTime{0},g_sgLastPowerButtonWakeEventFileTime{0};
// Diagnostic transport is unobserved in this projection scope; zero is not wake proof.
static std::atomic<ULONGLONG> g_sgOriginalPowerButtonWakeFileTime{0};
static ULONGLONG fixtureKernelNow=0;
[[maybe_unused]] static ULONGLONG sgNowFileTime(){return fixtureKernelNow;}
static bool classificationRetry=false,classificationManual=false,classificationSuccess=false;
static unsigned classificationFacts=0,classificationHandlerCalls=0,classificationInteractive=0;
static bool g_sgModernStandbyActive=false,g_sgModernWakeClassified=false;
static ULONGLONG g_sgModernWakeClassifiedTick=0;
enum class SgWork {WakeSuspend};
static bool sgUserWakeCancelsSleepIntent(ULONGLONG,ULONGLONG){return classificationManual;}
static bool sgSleepRetryActive(){return classificationRetry;}
static bool sgSleepTimeoutSuccessAfter(ULONGLONG){return classificationSuccess;}
static void sgAbortSleepIntent(const char*, ULONGLONG){}
static struct {void confirmedUserWake(){}} g_sgAutomaticSleepBudget;
static void sgSleepTimeoutStop(const char*){}
static void sgClearInternalSleepRequest(const char*){}
static void sgClearUnexpectedWake(const char*,bool){}
static void sleepRecoveryAutoInteractiveResume(unsigned long long,ULONG,ULONGLONG){++classificationInteractive;}
static void sgRecordFact(const char*,const json&){++classificationFacts;stage("classification-record",g_powerGeneration.load());}
static void handlePowerResumeNotification(SgWork,const char*){++classificationHandlerCalls;stage("classification-handler",g_powerGeneration.load());sgCompleteSharedWakeClassification(g_powerGeneration.load());}

static void sgRevokeResumeEntryObservation(){}
static void sgSleepTimeoutClassificationComplete(unsigned long long){}
static bool sgSleepTimeoutSettleClassification(unsigned long long){return false;}
static bool sgResumeEntryObservationPresent(unsigned long long){return false;}
// Pre-existing FinalDispatch retention is outside this projection consumer scope.
enum class SgRetryKind : uint8_t { None, EntryFailure, UnexpectedWake, SleepTimeout };
[[maybe_unused]] static bool sgNonUserWakeGuardEnabled(){return false;}
[[maybe_unused]] static bool sgRetryAdmissionHeld(SgRetryKind,unsigned long long,ULONGLONG){return false;}
@@CLASSIFICATION@@
static bool& g_sgRetryInProgress=classificationRetry;
static ULONGLONG g_sgSleepTriggerTick=0,g_sgRetryDueTick=0,g_sgRetryWindowsSuccessAtAdmission=0;
static ULONGLONG g_entryFailureInterruptedAt=0,g_entryFailureQuickDeadline=0;
static bool g_entryFailureExpanded=false;
static constexpr UINT_PTR SG_RETRY_TIMER_ID=0x7A72;
static struct {ULONGLONG generation=0,retryEntryFileTime=0;SgRetryKind retryKind=SgRetryKind::None;unsigned retryAttempt=0;} g_sgTask;
static void sgCancelRetryAdmission(SgRetryKind){}
static void sgStopMonitorEntryFailureTimer(const char*){}
@@RETRY_STOP@@
@@RECOVERY_CONSUMER@@
namespace SteamOriginalRecovery {
static void readyFixtureSelectPending(const std::shared_ptr<Control>& c,Pending& pending,[[maybe_unused]] const Scope& scope,[[maybe_unused]] bool enabled,[[maybe_unused]] bool legacy){
 [[maybe_unused]] auto& routeAdmission=legacy?c->legacyAdmission:c->admission;
 std::lock_guard<std::mutex> lock(c->mx);
@@RECOVERY_STARTUP_SELECTOR@@
}
static bool readyFixtureAdmitPending(const std::shared_ptr<Control>& c,Pending& pending,const Scope& scope,bool enabled,bool legacy){
 auto& routeAdmission=legacy?c->legacyAdmission:c->admission;
 ymcc::steamOriginal::Decision decision;
@@RECOVERY_FINAL_ADMISSION@@
 return decision.accepted;
}
}
#undef SetEvent
static HANDLE readyWorkerHandled=nullptr;
static std::atomic<unsigned> readyWorkerAttempts{0},readyWorkerUnknown{0},readyWorkerAdmissions{0};
static std::atomic<int> readyWorkerQueueKind{-1},readyWorkerSelectedKind{-1};
static BOOL readyFixtureSetEvent(HANDLE event){const BOOL signaled=::SetEvent(event);if(event&&SteamOriginalRecovery::readyFixtureControl&&event==SteamOriginalRecovery::readyFixtureControl->observationEvent.get()&&WaitForSingleObject(readyWorkerHandled,5000)!=WAIT_OBJECT_0)throw std::runtime_error("private original queue worker did not return");return signaled;}
struct ReadyConsumerScope {
    std::shared_ptr<SteamOriginalRecovery::Control> c;
    std::thread worker;
    ReadyConsumerScope(){
        readyWorkerAttempts=0;readyWorkerUnknown=0;readyWorkerAdmissions=0;readyWorkerQueueKind=-1;readyWorkerSelectedKind=-1;readySignals=0;readyCloseCalls=0;readyReenter=false;readyNestedResult=true;
        readyWorkerHandled=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!readyWorkerHandled)throw std::runtime_error("private worker completion event missing");
        c=std::make_shared<SteamOriginalRecovery::Control>();c->latestPower=c->deferredWake=g_powerGeneration.load();c->observationEvent=std::shared_ptr<void>(CreateEventW(nullptr,TRUE,FALSE,nullptr),[](void* handle){if(handle)CloseHandle(handle);});if(!c->observationEvent)throw std::runtime_error("private original observation event missing");SteamOriginalRecovery::readyFixtureControl=c;
        worker=std::thread([owner=c]{for(;;){SteamOriginalRecovery::Pending pending;{std::unique_lock<std::mutex> lock(owner->mx);owner->cv.wait(lock,[&]{return owner->stop||!owner->pending.empty();});if(owner->stop&&owner->pending.empty())break;pending=owner->pending.front();owner->pending.pop_front();}
            const ymcc::steamOriginal::Scope scope{{1,1,11,L"parent"},{2,1,22,L"game"},{3,1,33,L"steam"},9};
             readyWorkerQueueKind=static_cast<int>(pending.kind);SteamOriginalRecovery::readyFixtureSelectPending(owner,pending,scope,true,false);
            readyWorkerSelectedKind=static_cast<int>(pending.kind);
            ++readyWorkerAttempts;ymcc::sleeppower::SharedPowerV1 power;const bool captured=captureSharedPowerLifecycle(power)&&(pending.kind!=SteamOriginalRecovery::Trigger::Wake||power.generation==pending.generation);auto registration=captured?g_sharedPowerRevocations.observe(&readSharedSystemPower,power):ymcc::sleeppower::RevocationMirrors::Registration{};
            if(!registration||registration.cancelled()||!sharedPowerLifecycleCurrent(power))++readyWorkerUnknown;
            else if(SteamOriginalRecovery::readyFixtureAdmitPending(owner,pending,scope,true,false))++readyWorkerAdmissions;
            ::SetEvent(readyWorkerHandled);
        }});
    }
    ~ReadyConsumerScope(){SteamOriginalRecovery::readyFixtureControl.reset();{std::lock_guard<std::mutex> lock(c->mx);c->stop=true;}c->cv.notify_all();if(worker.joinable())worker.join();c.reset();CloseHandle(readyWorkerHandled);readyWorkerHandled=nullptr;}
};

@@CSS_RESUME@@
@@DECKY_FIRST_READY@@
@@DECKY_REPEATED_COMMIT@@
@@READY_COMMIT@@
static bool commitPowerResume(unsigned long long generation,const char* source){
    if(g_powerLifecycle.load()!=PowerLifecycle::Resuming)return commitPowerResume(generation,source,0);
    const auto ticket=recordPowerResumeReady(generation,capturePowerResumeBoundary(),true);
    return commitPowerResume(generation,source,ticket);
}
@@FAILURE_LOG@@
@@PUBLISHER@@
#ifdef YMCC_FIXTURE_FAN_STARTUP_WAKE
static ULONGLONG g_sgAcceptedS4WakeFileTime=0;
static void fanWritePowerCancel() noexcept {++g_fanWritePowerRevision;}
static constexpr ULONGLONG kFanResumeObserveWindowMs=2000;
@@FAN_KERNEL_LEAF@@
#endif
static bool nativeAyaLightingPublicReady(const ymcc::sleeppower::SharedPowerV1& power){
 ++ayaAttempts;
 if(faultStage=="aya-lighting")stage("aya-lighting",power.generation);
 return !refuseAya;
}
#include "power_source_state.h"
static unsigned acdcReceipts=0,acdcMonitorCalls=0,acdcReceiptAttempts=0;
static bool acdcMonitorFails=false,acdcReceiptFails=false;
static void ensureNativePowerMonitor(){++acdcMonitorCalls;if(acdcMonitorFails)throw std::runtime_error("fixture monitor admission");}
static void publishPowerSourceReceipt(HWND,const char*){++acdcReceiptAttempts;if(acdcReceiptFails)throw std::runtime_error("fixture receipt publication");++acdcReceipts;}
static ymcc::powerSource::Snapshot acdcFixtureSource() noexcept {
    ymcc::powerSource::Snapshot s;s.known=s.batteryKnown=s.hasBattery=true;s.ac=1;return s;
}
@@POWER_SOURCE_RECOVERY@@
static_assert(acdcFixtureTimerId==TIMER_ID_POWER_SOURCE_RECOVERY);
#include "steam_wallpaper_foreground.h"
[[maybe_unused]] static ymcc::steamwallpaperForeground::Authority& steamWallpaperForegroundOwner(){
 static ymcc::steamwallpaperForeground::Authority owner("fixture-wallpaper");return owner;
}
@@WALLPAPER_POWER_READY@@
@@PROJECTION@@
@@PROJECTION_WINDOW@@
static LRESULT productionTimer(){HWND h=g_hwnd;WPARAM w=kSleepPowerRedeliveryTimer;
@@TIMER@@
return 0;
}
#undef SetTimer
#undef KillTimer
#undef PostMessageW
#undef SendMessageW
static std::vector<std::string> cases,failures;
static unsigned assertions=0;
static void require(bool condition,const char* text){++assertions;if(!condition)failures.push_back(cases.back()+": "+text);}
static void deliverProjection(){const auto receipt=projectionQueue.value_or(0);projectionQueue.reset();productionProjectionWindow(g_hwnd,receipt);}
static void settle(){
 const auto receipt=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPENDFAILED,++serial);g_sleepPowerDispatch.complete(receipt,++serial);
 for(unsigned i=0;i<4&&g_sleepPowerDispatch.hasPending();++i){
  const auto pending=g_sleepPowerDispatch.pendingReplay();
  if(!pending)throw std::runtime_error("active power delivery crossed fixture reset");
  const auto delivered=g_sleepPowerDispatch.forwarded(pending.sequence,pending.type,++serial);
  if(!delivered)throw std::runtime_error("fixture cannot retire its retained power receipt");
  g_sleepPowerDispatch.complete(delivered,++serial);
 }
 if(g_sleepPowerDispatch.hasPending()||g_sleepPowerDispatch.pendingSuspend())throw std::runtime_error("power debt crossed fixture reset");
}
static void deliverDeckySuspend(){
    using Receipt=ymcc::sleeppower::Dispatch::Receipt;
    Receipt ingress;
    g_sleepPowerDispatch.callback(PBT_APMSUSPEND,++serial,[&](const auto& receipt){ingress=receipt;return true;});
    const auto delivered=g_sleepPowerDispatch.forwarded(ingress.sequence,ingress.type,++serial);
    require(delivered&&delivered.sequence==ingress.sequence,"ordered Dispatch consumes the simulated suspend before Ready");
    g_sleepPowerDispatch.complete(delivered,++serial);
}
static void reset(){
 if(projectionQueue){g_exitRequested=true;deliverProjection();} // Retire the real receipt between independent fixture cases.
 rawReceiptQueue.clear();
 failProjectionPost=failProjectionTimer=false;
 g_view=true;g_children.clear();sharedFactMainPosts=sharedFactChildPosts=sharedFactRefusals=0;
 sharedFactChildLast=nullptr;
 g_cpuUvTrial={};cpuTrialTimer=false;cpuTrialDisk=json::object();cpuTrialWrites=0;
 g_steamWallpaperForegroundPending=false;refuseWallpaperPost=false;wallpaperAttempts=wallpaperPosts=0;
 g_sgResumeClassification.generation=0;g_sgResumeClassification.observedGeneration=0;g_sgWakeClassificationBlockedGeneration=0;
 settle();g_exitRequested=false;g_hardwareWriteGate=true;g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;clearPowerResumeReady();
 g_powerGeneration.fetch_add(10);g_resumeReadyGeneration=g_powerGeneration.load();g_sharedPowerPublicationRevision.fetch_add(10);g_sharedPowerSuspendSerial.fetch_add(10);
 g_sharedPowerProjectionPending=false;g_sleepPowerRedeliveryArmed=false;g_sensorSuspendCallbackActive=false;g_aiFanMockSession.isolated=false;
 g_msiNativeForegroundPowerHold=true;g_msiNativeForegroundResumePending=true;g_msiNativeForegroundPowerTransitionObserved=true;g_msiNativeForegroundLastResumeGeneration=0;
 g_focusIsolationPrepared=false;g_focusIsolationGameOpen=false;g_focusLightProofNotBefore=0;
 g_focusIsolationPowerPeriod={};g_focusSessionPowerPeriod={};g_focusSession={};g_focusSessionSequence=0;focusTimerArmed=false;
 g_focusLightIsolationTicket.store({});g_focusLightFallbackObservation.reset();
 g_radialOverlayPower={};g_radialOverlay.open=false;g_radialReturning=false;
 calls.clear();inputReadyReceipts.clear();emitted.clear();diagnostics.clear();faultStage.clear();edge.clear();injected=failPost=throwDiagnostic=publishIngressCompletion=refuseAya=false;arms=posts=msiAttempts=readerIngress=ayaAttempts=0;
}
static void start(){publishSharedPowerLifecycle("fixture");deliverProjection();}
static void drain(){for(unsigned i=0;i<4&&g_sleepPowerRedeliveryArmed.load();++i)productionTimer();require(!g_sleepPowerRedeliveryArmed&&!g_sharedPowerProjectionPending,"original delivery debt and timer eventually settle");}
static void currentReady(){require(!emitted.empty()&&emitted.back().value("known",false)&&emitted.back().value("generation",0ull)==g_powerGeneration.load()&&emitted.back().value("hardwareWritesAllowed",false),"final event carries current admitted Native facts");}
int main(int argc,char** argv){
 const std::string selected=argc>=2?argv[1]:"";
 const bool dropNormalInputReceipt=argc==3&&selected=="fan-post-gate-resume-original-owner"&&std::string(argv[2])=="--drop-input-ready-receipt";
 if(argc>2&&!dropNormalInputReceipt){std::cerr<<"UNKNOWN_SHARED_POWER_PROJECTION_ARGUMENT\n";return 2;}
 const auto run=[&](const std::string& name,const std::function<void()>& test){if(!selected.empty()&&selected!=name)return;if(selected!="css-public-ready-boundaries")reset();cases.push_back(name);try{test();}catch(const std::exception& e){failures.push_back(name+": escaped exception "+e.what());}};
 const auto cssScopedReady=[&](bool queryCancel){
    using namespace ymcc::steamcss;
    const auto waitFor=[](const auto& predicate){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(std::chrono::steady_clock::now()<until){if(predicate())return true;Sleep(2);}return predicate();};
    std::atomic<unsigned> refreshes{0},helpers{0},ensures{0},probes{0};
    std::atomic<bool> confirmed{false},refreshReturn{true};
    g_steamSettingsObserver.value={};g_steamSettingsObserver.value.pid=800;g_steamSettingsObserver.value.created=900;g_steamSettingsObserver.value.root=L"G:\\private-steam";
    const auto session=g_steamSettingsObserver.sessionSnapshot();
    auto directory=std::filesystem::current_path()/("css-private-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directory(directory);
    const auto link=directory/"CSSLoaderLink.txt";
    {std::ofstream file(link);file<<"G:\\private-css\\CssLoader-Standalone-Headless.exe";}
    Config config;config.linkFile=link.wstring();config.delays={60000,60000,60000};config.modeDelays={60000,60000,60000};
    config.powerGeneration=[]{return currentPowerGeneration();};
    config.mayLaunch=[]{ymcc::sleeppower::SharedPowerV1 power;return !g_exitRequested&&captureSharedPowerLifecycle(power);};
    const auto bind=[]<class T>(T& value){if constexpr(requires{value.powerReader;})value.powerReader=&readSharedSystemPower;};bind(config);
    config.refreshMode=[&](const auto&){++refreshes;confirmed=false;return refreshReturn.load();};
    config.readMode=[](const auto&){return ModeSample{UiMode::Unknown,{}};};
    config.ensure=[&](const auto&,const auto&,const auto& allowed){++ensures;if(!allowed())return Result{};return Result{CreateEventW(nullptr,TRUE,FALSE,nullptr),880,"running","inert-backend"};};
    config.confirmIdle=[&](const auto&,bool,bool){++helpers;confirmed=true;return CreateEventW(nullptr,TRUE,TRUE,nullptr);};
    config.isResidentConfirmed=[&](const auto&){return confirmed.load();};
    config.probeExisting=[&]{++probes;return Result{CreateEventW(nullptr,TRUE,FALSE,nullptr),880,"running","inert-backend"};};
    struct GuardCleanup{~GuardCleanup(){g_steamCssGuard.shutdown();}} cleanup;
    require(g_steamCssGuard.initialize(std::move(config)),"actual CSS guard initializes");g_steamCssGuard.observeSteam(session);
    require(waitFor([]{return g_steamCssGuard.snapshot().linked;}),"actual private link enabled before Ready");
    const auto generation=g_powerGeneration.load();
    g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
    require(commitPowerResume(generation,"fixture-css-first"),"actual first Ready succeeds");
    (void)steamAppearanceResumeCommitted(generation); // Existing WM stage 2 follows commit.
    require(waitFor([&]{return refreshes==1;}),"first public Ready causes exactly one CSS shared-mode refresh");
    g_steamCssGuard.observeMode(session,{UiMode::BigPicture,"same-document"});
    require(waitFor([&]{return helpers==1&&g_steamCssGuard.snapshot().phase=="ready";}),"first actual guard use confirms resident");
    const auto oldScope=[&]{ymcc::sleeppower::SharedPowerV1 p;captureSharedPowerLifecycle(p);return p;}();
    g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});
    if(queryCancel){
        publishSharedPowerLifecycle("fixture-css-query-pending");deliverProjection();
        require(refreshes==1,"pending suspend cannot authorize CSS work");
        settle();publishSharedPowerLifecycle("fixture-css-query-cancel");deliverProjection();
    }else{
    ++g_sharedPowerSuspendSerial;g_powerLifecycle=PowerLifecycle::Suspended;g_hardwareWriteGate=false;settle();
    g_powerLifecycle=PowerLifecycle::Resuming;g_resumeReadyGeneration=generation;
    require(commitPowerResume(generation,"fixture-css-second-same-generation"),"actual second Ready in same generation succeeds");
    }
    const bool second=waitFor([&]{return refreshes==2;});
    cssObservations.push_back(json{{"cssObservation",{{"generation",generation},{"refreshes",refreshes.load()},{"helpers",helpers.load()},{"serial",g_sharedPowerSuspendSerial.load()},{"notification",g_sleepPowerDispatch.observedSequence()},{"secondReadyDelivered",second}}}});
    require(second,"second same-generation public Ready must deliver CSS without WM replay or mode fallback");
    if(!second)return;
    g_steamCssGuard.observeMode(session,{UiMode::BigPicture,"same-document"});
    require(waitFor([&]{return helpers==2&&g_steamCssGuard.snapshot().phase=="ready";}),"second actual guard use completes for unchanged Steam/document");
    for(unsigned i=0;i<16;++i)require(commitPowerResume(generation,"fixture-css-metadata-only"),"alreadyReady metadata publication succeeds");
    Sleep(30);require(refreshes==2&&helpers==2,"metadata and duplicate Ready never replay CSS use");
    const auto request=[]<class T>(T& runtime,const auto& s,const auto& p){if constexpr(requires{runtime.requestPublicResumeRecovery(s,p);})return runtime.requestPublicResumeRecovery(s,p);else return false;};
    require(!request(g_steamCssGuard,session,oldScope),"old public scope cannot request current CSS work");
    auto current=oldScope;require(captureSharedPowerLifecycle(current),"current public Ready scope captured");
    current.publicationRevision=1;require(!request(g_steamCssGuard,session,current),"older metadata revision cannot refund the same public boundary");
    g_exitRequested=true;require(!commitPowerResume(generation,"fixture-css-exit"),"exit refuses public Ready");g_exitRequested=false;
    g_hardwareWriteGate=false;require(!commitPowerResume(generation,"fixture-css-unknown"),"closed gate cannot mint a CSS Ready request");g_hardwareWriteGate=true;
    g_steamCssGuard.observeSteam({});require(waitFor([]{return !g_steamCssGuard.snapshot().steamRunning;}),"Steam absent worker state settles");
    ++g_sharedPowerSuspendSerial;require(commitPowerResume(generation,"fixture-css-absent"),"public Ready independent from absent Steam");
    Sleep(30);require(refreshes==2&&helpers==2,"absent Steam creates no CSS work");
    g_steamCssGuard.shutdown();require(DeleteFileW(link.c_str())&&RemoveDirectoryW(directory.c_str()),"private configuration and directory returned");
 };
 run("css-scoped-ready",[&]{cssScopedReady(false);});
 run("css-query-cancel-ready",[&]{cssScopedReady(true);});
 const auto deckyReadOwnership=[](bool inFlight){
    const auto choices=inFlight?std::vector<std::string>{"active-no-demand","active-old-hint","active-new-hint","active-new-refresh","active-next-ready","active-retired-hint",
        "ordinary-next-ready","ordinary-late-return","ordinary-retired-ready","ordinary-no-successor"}:
        std::vector<std::string>{"current","unconfirmed","cache-reuse","joined-inflight","retired","independent","decky-off","theme-joined","successor"};
    for(const auto& control:choices){
        reset();
        std::clog<<"Decky mode ownership: "<<control<<std::endl;
        using namespace decky_read_probe;
        struct Event{
            HANDLE handle=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            Event(){if(!handle)throw std::runtime_error("fixture event");}
            ~Event(){CloseHandle(handle);}
        } parked, go, settled, warm, callback, returnRead, healthSeen, stableSeen;
        entered=parked.handle;release=go.handle;idle=settled.handle;
        fixtureTick.store(10000);holdFinite=false;watchIdle=false;waitFailed=false;
        std::atomic<unsigned> reads{0}, delivers{0}, changes{0}, started{0}, cancelled{0}, healthWakes{0};
        std::atomic<std::uint64_t> lastReadSerial{0}, healthSerial{0};
        ymcc::steamui::ModeObserver observer;
        struct Cleanup{
            ymcc::steamui::ModeObserver& observer;
            HANDLE returnRead;
            ~Cleanup(){SetEvent(release);SetEvent(returnRead);observer.shutdown();g_steamModeObserver.actual=nullptr;g_steamModeObserver.forwardOrdinary=false;g_deckySidebarRuntime.state.enabled=false;}
        } cleanup{observer,returnRead.handle};
        g_steamModeObserver.actual=&observer;g_steamModeObserver.requests.clear();g_deckySidebarRuntime.state.enabled=true;
        g_deckySidebarRuntime.connectionChecks=0;
        g_steamSettingsObserver.value={};g_steamSettingsObserver.value.pid=802;
        g_steamSettingsObserver.value.created=902;g_steamSettingsObserver.value.root=L"X:\\FixtureSteam";
        const auto session=g_steamSettingsObserver.sessionSnapshot();
        ymcc::steamui::ModeObserverConfig cfg;
        cfg.debounceMs=0;cfg.minimumGapMs=500;cfg.failureGapMs=1500;
        cfg.admissionPowerReader=&readSharedSystemPower;
        const bool ordinary=control.starts_with("ordinary-");
        cfg.readOwned=[&](const auto& s,const auto& current)->json{
            ymcc::sleeppower::SharedPowerV1 atStart;
            if(ordinary&&!captureSharedPowerLifecycle(atStart)){++cancelled;return {{"reason","shared-mode-read-cancelled"}};}
            if(++started==2&&(inFlight||control=="joined-inflight")){
                SetEvent(callback.handle);
                if(WaitForSingleObject(returnRead.handle,4000)!=WAIT_OBJECT_0)waitFailed=true;
            }
            ymcc::sleeppower::SharedPowerV1 p;
            if(ordinary){
                p=atStart;
                // The read can return after passing its final power check.
                if(!current()||(!(control=="ordinary-late-return"&&started==2)&&!sharedPowerLifecycleCurrent(p))){
                    ++cancelled;return {{"reason","shared-mode-read-cancelled"}};
                }
            }else if(!current()||!captureSharedPowerLifecycle(p)){++cancelled;return {{"reason","shared-mode-read-cancelled"}};}
            lastReadSerial=p.suspendSerial;++reads;
            if(control=="unconfirmed"&&started==2)return {{"known",false},{"reason","live-busy"}};
            return {{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},
                {"mode","bigpicture"},{"modeEpoch","fixture:1"}};
        };
        cfg.changed=[&](const auto&,const auto&){++delivers;++changes;SetEvent(warm.handle);};
        cfg.stableBigPicture=[&](const auto&){++delivers;SetEvent(stableSeen.handle);};
        const auto healthSettled=[&](const auto& s,const auto& power){
            const auto mode=observer.snapshot(s,power);
            if(mode.value("mode",std::string{})==(control=="unconfirmed"?"unknown":"bigpicture"))healthSerial=power.suspendSerial;
            ++healthWakes;SetEvent(healthSeen.handle);
        };
        []<class Config>(Config& config,const auto& callbackFn){
            if constexpr(requires{config.deckyResumeSettled=callbackFn;})config.deckyResumeSettled=callbackFn;
        }(cfg,healthSettled);
        require(observer.initialize(std::move(cfg)),"actual ModeObserver starts");
        observer.setDemand(ymcc::steamui::ModeConsumer::Decky,true);observer.observeSteam(session);
        require(WaitForSingleObject(warm.handle,2000)==WAIT_OBJECT_0&&reads==1&&delivers==1,"original startup read settles");
        auto commit=[&]{g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
            require(commitPowerResume(g_powerGeneration.load(),"fixture-decky-queued-owner"),"original Ready commit succeeds");
            deliverProjection();};
        auto boundary=[&]{++g_sharedPowerSuspendSerial;deliverDeckySuspend();settle();};
        commit();require(g_steamModeObserver.requests.empty(),"first commit does not use the supplementary port");
        g_deckySidebarRuntime.connectionChecks=0; // Measure the supplementary scope only.
        if(ordinary){
            fixtureTick.fetch_add(1000);
            require(observer.requestRefresh(session),"ordinary old-scope read is admitted");
            const bool active=WaitForSingleObject(callback.handle,2000)==WAIT_OBJECT_0;
            require(active&&started==2&&reads==1,"ordinary read is in flight before suspension");
            if(!active)continue;
            const auto retire=[&]{g_powerLifecycle=PowerLifecycle::Suspending;g_hardwareWriteGate=false;boundary();};
            retire();
            if(control!="ordinary-no-successor"){
                const auto wave=deckyFirstModeRequests;
                g_deckySidebarRuntime.state.enabled=true;
                g_steamModeObserver.forwardOrdinary=true;
                ++g_powerGeneration;
                commit();
                require(deckyFirstModeRequests==wave+1&&g_steamModeObserver.requests.empty(),"new Ready admits only its original ordinary mode request");
                if(control=="ordinary-retired-ready")retire();
            }
            watchIdle=true;fixtureTick.fetch_add(1000);SetEvent(returnRead.handle);
            require(WaitForSingleObject(settled.handle,2500)==WAIT_OBJECT_0&&!waitFailed,"ordinary callback and successor return to idle");
            const bool preserve=control=="ordinary-next-ready"||control=="ordinary-late-return";
            if(preserve)require(started==3&&delivers==2&&changes==2&&lastReadSerial==g_sharedPowerSuspendSerial,
                "new first-wave mode request survives the old ordinary completion and delivers once");
            else require(started==2&&delivers==1&&cancelled==1,"absent or retired Ready cannot transfer an ordinary mode request");
            continue;
        }
        boundary();const auto acceptedSerial=g_sharedPowerSuspendSerial.load();
        if(control=="cache-reuse"||control=="joined-inflight"){
            publishSharedPowerLifecycle("fixture-independent-current-ready");
            fixtureTick.fetch_add(500);
            require(observer.requestRefresh(session,false),"independent reader can already own the current wake");
            const bool reuse=control=="cache-reuse";
            const auto reached=WaitForSingleObject(reuse?stableSeen.handle:callback.handle,2000)==WAIT_OBJECT_0;
            require(reached,"current power read is already published or in flight");
            if(!reached)continue;
            commit();
            if(!reuse){
                require(healthWakes==0&&g_deckySidebarRuntime.connectionChecks==0,"an in-flight join cannot publish or notify early");
                watchIdle=true;fixtureTick.fetch_add(500);SetEvent(returnRead.handle);
                require(WaitForSingleObject(settled.handle,2500)==WAIT_OBJECT_0&&!waitFailed,"joined read returns to idle");
            }
            readyCommitScenarios.push_back(control+" reads="+std::to_string(reads.load())+" health="+std::to_string(healthWakes.load()+g_deckySidebarRuntime.connectionChecks));
            require(started==2&&reads==2&&delivers==2&&lastReadSerial==acceptedSerial,"current read shared without another IO");
            if(reuse)require(healthWakes==0&&g_deckySidebarRuntime.connectionChecks==1,"current cached publication wakes health once");
            else require(healthWakes==1&&healthSerial==acceptedSerial&&g_deckySidebarRuntime.connectionChecks==0,"joined read wakes health once after publication");
            continue;
        }
        holdFinite=true;commit();
        require(g_steamModeObserver.requests.size()==1,"repeated same-generation Ready is forwarded to the production observer");
        const bool held=WaitForSingleObject(parked.handle,2000)==WAIT_OBJECT_0;
        require(held&&reads==1&&healthWakes==0,"production observer holds the pending read without notifying health or doing IO before its slot");
        if(!held)continue;
        if(inFlight){
            fixtureTick.fetch_add(1000);SetEvent(go.handle);
            const bool active=WaitForSingleObject(callback.handle,2000)==WAIT_OBJECT_0;
            require(active&&started==2&&reads==1,"Decky-only read took its slot and is paused before the owned fence");
            if(!active)continue;
            if(control=="active-old-hint")require(observer.requestConnectionRefresh(session),"same-scope hint is accepted before revocation");
            auto retireScope=[&]{g_powerLifecycle=PowerLifecycle::Suspending;g_hardwareWriteGate=false;boundary();
                g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;};
            retireScope();
            if(control=="active-new-hint"||control=="active-retired-hint")
                require(observer.requestConnectionRefresh(session),"successor connection hint takes the original follow-up slot");
            if(control=="active-new-refresh")require(observer.requestRefresh(session,false),"non-invalidating successor request takes a slot without a hint revision");
            if(control=="active-next-ready"){commit();require(g_steamModeObserver.requests.size()==2,"fresh Decky Ready takes over after the in-flight read");}
            if(control=="active-retired-hint")retireScope();
            watchIdle=true;fixtureTick.fetch_add(1000);SetEvent(returnRead.handle);
            require(WaitForSingleObject(settled.handle,2500)==WAIT_OBJECT_0&&!waitFailed,"follow-up resolves back to idle");
            const bool preserve=control=="active-new-hint"||control=="active-new-refresh"||control=="active-next-ready";
            require(healthWakes==0&&changes==(preserve?2u:1u),"a replacement publication uses ordinary changed delivery once; a cancelled read does not wake health");
            require(cancelled==1,"the old scoped read cannot observe the new Ready");
            readyCommitScenarios.push_back(control+" entered="+std::to_string(started.load())+" reads="+std::to_string(reads.load())+
                " deliveries="+std::to_string(delivers.load())+" cancelled="+std::to_string(cancelled.load())+
                " admitted="+std::to_string(acceptedSerial)+" read="+std::to_string(lastReadSerial.load())+
                " changedWake="+std::to_string(changes.load()-1)+" scopedWake="+std::to_string(healthWakes.load()));
            if(preserve){
                require(started==3&&reads==2&&delivers==2&&lastReadSerial==g_sharedPowerSuspendSerial,
                    "fresh successor request survives old cancellation and delivers once");
            }else require(started==2&&reads==1&&delivers==1,"stale or absent follow-up cannot trigger a read");
            continue;
        }
        const bool retire=control=="retired"||control=="independent"||control=="successor";
        if(retire){g_powerLifecycle=PowerLifecycle::Suspending;g_hardwareWriteGate=false;boundary();
            g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;
            require(acceptedSerial<g_sharedPowerSuspendSerial&&g_steamModeObserver.requests.size()==1,
                "a successor scope is current without new Decky or CSS demand");}
        if(control=="independent")require(observer.requestRefresh(session,false),"independent Steam read adopts the original slot");
        if(control=="successor"){commit();require(g_steamModeObserver.requests.size()==2,"fresh Ready replaces the old Decky scope, not its deadline");}
        if(control=="decky-off"||control=="theme-joined"){
            observer.setDemand(ymcc::steamui::ModeConsumer::Css,true);
            if(control=="theme-joined"){
                ymcc::sleeppower::SharedPowerV1 scope;
                require(captureSharedPowerLifecycle(scope)&&observer.requestPublicResumeRefresh(session,scope),"theme can attach independent demand");
            }
            observer.setDemand(ymcc::steamui::ModeConsumer::Decky,false);
        }
        watchIdle=true;fixtureTick.fetch_add(1000);SetEvent(go.handle);
        if(control=="unconfirmed"){
            const bool delivered=WaitForSingleObject(healthSeen.handle,500)==WAIT_OBJECT_0;
            readyCommitScenarios.push_back(control+" reads="+std::to_string(reads.load())+" health="+std::to_string(healthWakes.load()));
            require(delivered&&reads==2&&delivers==1&&healthWakes==1&&healthSerial==acceptedSerial,
                "unconfirmed shared result notifies health while the original retry is still pending");
            continue;
        }
        require(WaitForSingleObject(settled.handle,2500)==WAIT_OBJECT_0&&!waitFailed,"observer reached idle after processing the single pending slot");
        readyCommitScenarios.push_back(control+" reads="+std::to_string(reads.load())+" deliveries="+std::to_string(delivers.load())+
            " admitted="+std::to_string(acceptedSerial)+" read="+std::to_string(lastReadSerial.load())+" health="+std::to_string(healthWakes.load()));
        const bool notified=control=="current"||control=="successor";
        if(notified)require(healthWakes==1&&healthSerial==g_sharedPowerSuspendSerial,"unchanged current publication wakes health once");
        else require(healthWakes==0,"no stale or disabled scoped health notification");
        if(control=="retired"||control=="decky-off"){
            require(reads==1&&delivers==1,"retired Decky-only queued wake must not borrow a successor Ready or consume a mode read slot");
        }else{
            require(reads==2&&delivers==2&&lastReadSerial==g_sharedPowerSuspendSerial,"valid current or independent request reads once and delivers ordinarily");
        }
    }
 };
 run("decky-repeated-commit-queued-owner",[&]{deckyReadOwnership(false);});
 run("decky-repeated-commit-inflight-owner",[&]{deckyReadOwnership(true);});
 run("decky-same-generation-repeated-commit",[]{
    const auto generation=g_powerGeneration.load();
    g_steamModeObserver.requests.clear();g_deckySidebarRuntime.state.enabled=true;
    g_steamSettingsObserver.value={};g_steamSettingsObserver.value.pid=800;
    g_steamSettingsObserver.value.created=900;g_steamSettingsObserver.value.root=L"G:\\private-steam";
    const auto recommit=[&]{g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
        require(commitPowerResume(generation,"fixture-decky-repeated-ready"),"original Ready commit succeeds");
        deliverProjection();};
    recommit();
    require(g_steamModeObserver.requests.empty(),"first commit leaves original per-generation Decky wave alone");
    recommit();
    require(g_steamModeObserver.requests.empty(),"same revocation recommit does not refresh");
    g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});
    settle();recommit();
    require(g_steamModeObserver.requests.empty(),"notification-only change does not refresh");
    ++g_sharedPowerSuspendSerial;
    deliverDeckySuspend();
    settle();recommit();
    const auto& observed=g_steamModeObserver.requests;
    require(observed.size()==1&&observed.front().generation==generation&&
        observed.front().suspendSerial==g_sharedPowerSuspendSerial&&
        observed.front().observedNotificationSequence==g_sleepPowerDispatch.observedSequence(),
        "second real commit in the same generation refreshes the current shared read even with CSS off");
    const auto requestsAfterRetry=observed.size();
    require(commitPowerResume(generation,"fixture-decky-metadata"),"already-Ready publication remains successful");
    recommit();
    require(observed.size()==requestsAfterRetry,"duplicate Ready and recommit cannot reissue the same read");
    g_deckySidebarRuntime.state.enabled=false;++g_sharedPowerSuspendSerial;recommit();
    g_deckySidebarRuntime.state.enabled=true;recommit();
    require(observed.size()==requestsAfterRetry,"OFF consumes the scope without work and re-enable cannot replay it");
    g_deckySidebarRuntime.fenced=true;++g_sharedPowerSuspendSerial;recommit();g_deckySidebarRuntime.fenced=false;
    require(observed.size()==requestsAfterRetry,"a fenced runtime cannot request a read");
    g_steamSettingsObserver.value={};++g_sharedPowerSuspendSerial;recommit();
    require(observed.size()==requestsAfterRetry,"no bound Steam session cannot request a read");
    g_deckySidebarRuntime.state.enabled=false;
 });
 run("decky-cancel-without-commit-ready",[]{
    const auto generation=g_powerGeneration.load();
    g_steamModeObserver.requests.clear();g_deckySidebarRuntime.state.enabled=true;
    struct ResetDecky {~ResetDecky(){g_deckySidebarRuntime.state.enabled=false;classificationRetry=false;g_steamModeObserver.beforeDeckyAdmission={};}} resetDecky;
    g_steamSettingsObserver.value={};g_steamSettingsObserver.value.pid=800;
    g_steamSettingsObserver.value.created=900;g_steamSettingsObserver.value.root=L"G:\\private-steam";
    g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
    require(commitPowerResume(generation,"fixture-first-ready"),"first real commit succeeds");
    require(g_steamModeObserver.requests.empty(),"first commit does not issue supplementary mode refresh");
    deliverProjection();
    // The next Ready is a cancellation;
    // there is no second commit or WM_POWER_RESUME_COMMIT driver for it.
    ++g_sharedPowerSuspendSerial;deliverDeckySuspend();settle();
    classificationRetry=true;start();
    require(g_steamModeObserver.requests.empty(),"a guarded retry does not spend the Decky wake read");
    classificationRetry=false;
    bool refused=false;
    g_steamModeObserver.beforeDeckyAdmission=[&]{
        const auto later=g_sleepPowerDispatch.window(PBT_APMRESUMESUSPEND,++serial);
        g_sleepPowerDispatch.complete(later,++serial);
        refused=true;
    };
    start();
    require(refused&&g_steamModeObserver.requests.empty()&&g_sleepPowerRedeliveryArmed,
        "definite refusal after notification movement retains delivery");
    drain();
    const auto& requests=g_steamModeObserver.requests;
    require(requests.size()==1&&requests.back().generation==generation&&
        requests.back().suspendSerial==g_sharedPowerSuspendSerial,"cancellation Ready refreshes the current scope with no second commit");
    const auto accepted=requests.size();start();
    require(requests.size()==accepted,"metadata-only Ready cannot request another Decky read");
 });
 run("decky-first-public-ready",[]{
    const auto generation=g_powerGeneration.load();
    const auto first=deckyFirstOffers;
    g_steamModeObserver.requests.clear();g_deckySidebarRuntime.state.enabled=true;
    const auto bindings=deckyFirstBindings,admissions=deckyFirstAdmissions,mode=deckyFirstModeRequests;
    const auto connections=g_deckySidebarRuntime.connectionChecks;
    struct ResetDecky {~ResetDecky(){g_deckySidebarRuntime.state.enabled=false;classificationRetry=false;deckyFirstAdmission={};deckyFirstRequestThrows=false;}} resetDecky;
    g_steamSettingsObserver.value={};g_steamSettingsObserver.value.pid=800;
    g_steamSettingsObserver.value.created=900;g_steamSettingsObserver.value.root=L"G:\\private-steam";
    const auto initialSerial=g_sharedPowerSuspendSerial.load();g_sharedPowerSuspendSerial=0;
    start();require(deckyFirstOffers==first,"cold Ready never starts Decky sleep recovery");
    g_sharedPowerSuspendSerial=initialSerial;
    g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
    require(commitPowerResume(generation,"fixture-decky-first-ready"),"first native commit succeeds");
    require(deckyFirstOffers==first,"commit only publishes the first wave");
    ++g_sharedPowerSuspendSerial;deliverDeckySuspend();settle();
    classificationRetry=true;start();
    require(deckyFirstOffers==first,"guarded retry cannot spend first Decky recovery");
    classificationRetry=false;
    deckyFirstAdmission=[&]{const auto later=g_sleepPowerDispatch.window(PBT_APMRESUMESUSPEND,++serial);
        g_sleepPowerDispatch.complete(later,++serial);return false;};
    start();
    require(deckyFirstOffers==first&&g_sleepPowerRedeliveryArmed,"known first refusal after notification movement retains delivery");
    drain();
    require(deckyFirstOffers==first+1&&g_steamModeObserver.requests.empty(),"coalesced first Ready runs the original wave once");
    require(deckyFirstBindings==bindings+1&&deckyFirstAdmissions==admissions+1&&deckyFirstModeRequests==mode+1&&
        g_deckySidebarRuntime.connectionChecks==connections+1,"first Ready requests preparation, binding, admission, connection and mode");
    start();require(deckyFirstOffers==first+1&&g_steamModeObserver.requests.empty(),"metadata does not repeat the first wave");
    ++g_sharedPowerSuspendSerial;deliverDeckySuspend();settle();start();
    require(deckyFirstOffers==first+1&&g_steamModeObserver.requests.size()==1,"later same-generation Ready only requests a shared read");
    ++g_powerGeneration;start();
    require(deckyFirstOffers==first+2&&g_steamModeObserver.requests.size()==1,"a new generation still receives its original wave");
    ++g_powerGeneration;deckyFirstRequestThrows=true;start();
    require(deckyFirstOffers==first+3&&diagnostics==std::vector<std::string>{"decky-ready"},"public projection records a failed Decky request after acceptance");
    currentReady();
    deckyFirstRequestThrows=false;start();
    require(deckyFirstOffers==first+3&&deckyFirstBindings==bindings+2,"fault does not replay accepted first Ready or strand independent consumers");
 });

 run("css-public-ready-boundaries",[&]{
    using namespace ymcc::steamcss;
    const auto waitFor=[](const auto& predicate){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(std::chrono::steady_clock::now()<until){if(predicate())return true;Sleep(2);}return predicate();};

    if(selected=="css-public-ready-boundaries") {
     const auto coldInitialReady=[&]{
      DWORD before=0,after=0;require(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"cold initial HANDLE count");
      std::atomic<unsigned> refreshes{0},helpers{0};std::atomic<bool> confirmed{false};
      HANDLE completed=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!completed)throw std::runtime_error("owned cold completion missing");
      struct Cleanup{HANDLE event;~Cleanup(){if(event)SetEvent(event);g_steamCssGuard.shutdown();if(event)CloseHandle(event);}} cleanup{completed};
      g_powerGeneration=8157;g_resumeReadyGeneration=8157;g_sharedPowerSuspendSerial=0;g_sharedPowerPublicationRevision=1;
      g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;
      require(g_sleepPowerDispatch.observedSequence()==0,"real cold Dispatch has no observed notification");
      publishSharedPowerLifecycle("fixture-cold-baseline");
      g_steamSettingsObserver.value={};g_steamSettingsObserver.value.pid=800;g_steamSettingsObserver.value.created=900;g_steamSettingsObserver.value.root=L"G:\\private-steam";
      const auto session=g_steamSettingsObserver.sessionSnapshot();
      const auto directory=std::filesystem::current_path()/("css-cold-"+std::to_string(GetCurrentProcessId()));std::filesystem::create_directory(directory);const auto link=directory/"CSSLoaderLink.txt";
      {std::ofstream file(link);file<<"G:\\private-css\\CssLoader-Standalone-Headless.exe";}
      Config config;config.linkFile=link.wstring();config.delays={60000,60000,60000};config.modeDelays={60000,60000,60000};config.resumeDelays={0,80,80};
      config.powerReader=&readSharedSystemPower;config.powerGeneration=[]{return currentPowerGeneration();};
      config.mayLaunch=[]{ymcc::sleeppower::SharedPowerV1 p;return !g_exitRequested&&captureSharedPowerLifecycle(p);};
      config.refreshMode=[&](const auto&){++refreshes;return true;};
      config.readMode=[](const auto&){return ModeSample{UiMode::Unknown,{}};};
      config.ensure=[](const auto&,const auto&,const auto& allowed){return allowed()?Result{CreateEventW(nullptr,TRUE,FALSE,nullptr),880,"running","inert-backend"}:Result{};};
      config.confirmIdle=[&](const auto&,bool,bool){const auto n=++helpers;if(n!=1){confirmed=true;return CreateEventW(nullptr,TRUE,TRUE,nullptr);}HANDLE duplicate=nullptr;if(!DuplicateHandle(GetCurrentProcess(),completed,GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS))throw std::runtime_error("duplicate cold completion");return duplicate;};
      config.isResidentConfirmed=[&](const auto&){return confirmed.load();};
      config.probeExisting=[]{return Result{CreateEventW(nullptr,TRUE,FALSE,nullptr),880,"running","inert-backend"};};
      require(g_steamCssGuard.initialize(std::move(config)),"cold actual guard initializes");g_steamCssGuard.observeSteam(session);
      require(waitFor([]{return g_steamCssGuard.snapshot().linked;}),"cold private link enabled");
      g_steamCssGuard.observeMode(session,{UiMode::BigPicture,"startup-document"});
      require(waitFor([&]{return helpers==1&&g_steamCssGuard.snapshot().phase=="applying";}),"ordinary startup use is already in flight");
      const auto diagnosticBefore=cssReadyDiagnostics.load();
      require(commitPowerResume(8157,"fixture-cold-already-ready"),"actual already-ready Native commit succeeds");
      Sleep(60);const bool uninterrupted=refreshes==0&&helpers==1&&cssReadyDiagnostics==diagnosticBefore;
      require(uninterrupted,"initial zero-boundary Ready cannot cancel ordinary startup use or grant recovery");
      if(uninterrupted){
       for(unsigned i=0;i<16;++i)require(commitPowerResume(8157,"fixture-cold-metadata"),"initial metadata Ready remains valid");
       Sleep(30);require(refreshes==0&&helpers==1,"initial metadata and duplicate Ready remain inert");
       confirmed=true;SetEvent(completed);require(waitFor([]{return g_steamCssGuard.snapshot().phase=="ready";}),"original startup helper completion reaches Ready");
       g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});settle();
       publishSharedPowerLifecycle("fixture-cold-query-cancel");deliverProjection();
       require(g_sharedPowerSuspendSerial==0&&g_sleepPowerDispatch.observedSequence()>0,"real notification-only boundary retains zero suspend serial");
       const bool resumed=waitFor([&]{return refreshes==1;});require(resumed,"notification-only boundary still grants one original recovery");
       if(resumed){g_steamCssGuard.observeMode(session,{UiMode::BigPicture,"startup-document"});require(waitFor([&]{return helpers==2&&g_steamCssGuard.snapshot().phase=="ready";}),"notification-only recovery confirms one bounded use");}
      }
      g_steamCssGuard.shutdown();require(DeleteFileW(link.c_str())&&RemoveDirectoryW(directory.c_str()),"cold private configuration returned");
      CloseHandle(completed);cleanup.event=nullptr;
      require(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&before==after,"cold guard worker and owned HANDLEs returned");
      cssObservations.push_back(json{{"coldInitialReady",{{"uninterrupted",uninterrupted},{"refreshes",refreshes.load()},{"helpers",helpers.load()},{"diagnosticsBefore",diagnosticBefore},{"diagnosticsAfter",cssReadyDiagnostics.load()},{"handlesBefore",before},{"handlesAfter",after}}}});
      return uninterrupted;
     };
     if(!coldInitialReady())return;
     reset();
    }
    DWORD handlesBefore=0,handlesAfter=0;require(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=FALSE,"initial owned HANDLE count");
    std::atomic<unsigned> refreshes{0},helpers{0},unknownSlot{0};std::atomic<bool> confirmed{false};
    g_steamSettingsObserver.value={};g_steamSettingsObserver.value.pid=800;g_steamSettingsObserver.value.created=900;g_steamSettingsObserver.value.root=L"G:\\private-steam";
    const auto session=g_steamSettingsObserver.sessionSnapshot();
    auto directory=std::filesystem::current_path()/("css-boundaries-"+std::to_string(GetCurrentProcessId()));std::filesystem::create_directory(directory);const auto link=directory/"CSSLoaderLink.txt";
    const auto enable=[&]{std::ofstream file(link);file<<"G:\\private-css\\CssLoader-Standalone-Headless.exe";};enable();
    Config cssGuard;cssGuard.linkFile=link.wstring();cssGuard.delays={60000,60000,60000};cssGuard.modeDelays={60000,60000,60000};cssGuard.resumeDelays={0,80,80};
@@CSS_GUARD_CONFIG@@
    cssGuard.powerGeneration=[]{return currentPowerGeneration();};
    cssGuard.mayLaunch=[]{ymcc::sleeppower::SharedPowerV1 power;return !g_exitRequested&&captureSharedPowerLifecycle(power);};
    cssGuard.refreshMode=[&](const auto&){const auto n=++refreshes;if(n==1){g_hardwareWriteGate=false;return false;}return true;};
    cssGuard.changed=[&](const auto& state){if(state.reason=="resume-public-power-unavailable")unknownSlot=state.attempts;};
    cssGuard.readMode=[](const auto&){return ModeSample{UiMode::Unknown,{}};};
    cssGuard.ensure=[](const auto&,const auto&,const auto& allowed){if(!allowed())return Result{};return Result{CreateEventW(nullptr,TRUE,FALSE,nullptr),880,"running","inert-backend"};};
    cssGuard.confirmIdle=[&](const auto&,bool,bool){++helpers;confirmed=true;return CreateEventW(nullptr,TRUE,TRUE,nullptr);};
    cssGuard.isResidentConfirmed=[&](const auto&){return confirmed.load();};
    cssGuard.probeExisting=[]{return Result{CreateEventW(nullptr,TRUE,FALSE,nullptr),880,"running","inert-backend"};};
    struct Cleanup{~Cleanup(){g_steamCssGuard.shutdown();}} cleanup;
    require(g_steamCssGuard.initialize(std::move(cssGuard)),"actual guard initializes with exact main Config binding");g_steamCssGuard.observeSteam(session);require(waitFor([]{const auto state=g_steamCssGuard.snapshot();return state.linked&&state.phase=="grace";}),"fresh private guard startup publishes enabled link");
    const auto generation=g_powerGeneration.load();g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
    require(commitPowerResume(generation,"fixture-css-unknown-slot"),"public Ready commit succeeds independently from later worker Unknown");
    const bool unknown=waitFor([&]{return unknownSlot==2;});require(unknown,"UNKNOWN consumes second existing retry slot without erasing the remaining slot");
    if(!unknown){g_hardwareWriteGate=true;return;}
    require(refreshes==1&&helpers==0,"UNKNOWN slot performs no shared-mode refresh or helper work");
    g_hardwareWriteGate=true;
    require(waitFor([&]{return refreshes==2;}),"third original retry slot survives temporary UNKNOWN with no new Ready event");
    g_steamCssGuard.observeMode(session,{UiMode::BigPicture,"unchanged-doc"});require(waitFor([&]{return helpers==1&&g_steamCssGuard.snapshot().phase=="ready";}),"remaining original slot completes one bounded CSS use");
    require(!steamAppearanceResumeCommitted(generation),"old WM compatibility callback cannot duplicate public accepted scope");
    require(DeleteFileW(link.c_str())!=FALSE,"disable private link");require(waitFor([]{return !g_steamCssGuard.snapshot().linked;}),"OFF reaches actual worker");
    ++g_sharedPowerSuspendSerial;require(commitPowerResume(generation,"fixture-css-off"),"public Ready continues when appearance OFF");Sleep(30);require(refreshes==2&&helpers==1,"OFF Ready does not create CSS work");
    enable();require(waitFor([]{return g_steamCssGuard.snapshot().linked;}),"enable private link by existing directory event");require(commitPowerResume(generation,"fixture-css-off-consumed"),"duplicate consumed OFF scope remains a valid public Ready");Sleep(30);require(refreshes==2&&helpers==1,"re-enabling cannot refund a consumed Ready scope");
    cssDiagnosticThrow=true;++g_sharedPowerSuspendSerial;require(commitPowerResume(generation,"fixture-css-diagnostic-failure"),"CSS diagnostic exception cannot fail public Ready or close hardware gate");cssDiagnosticThrow=false;
    require(g_hardwareWriteGate&&g_powerLifecycle==PowerLifecycle::Ready,"public Ready survives CSS listener error");
    require(waitFor([&]{return refreshes==3;}),"accepted CSS request survives its diagnostic failure exactly once");
    g_exitRequested=true;require(!commitPowerResume(generation,"fixture-css-exit"),"exit cannot publish Ready");
    g_steamCssGuard.shutdown();g_exitRequested=false;
    require(DeleteFileW(link.c_str())&&RemoveDirectoryW(directory.c_str()),"private config and directory returned");
    require(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)!=FALSE&&handlesBefore==handlesAfter,"actual guard worker and all event/directory/backend HANDLEs returned");
    cssObservations.push_back(json{{"boundaryRefreshes",refreshes.load()},{"helpers",helpers.load()},{"unknownSlot",unknownSlot.load()},{"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter}});
 });

 run("css-manual-ready-mailbox",[]{
    using namespace ymcc::steamcss;
    const auto waitFor=[](const auto& predicate){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(std::chrono::steady_clock::now()<until){if(predicate())return true;Sleep(2);}return predicate();};
    const auto scenario=[&](bool confirmationAvailable){
     std::atomic<unsigned> refreshes{0},confirmations{0},automaticUses{0},ensures{0};std::atomic<bool> confirmed{false},pausedOnce{false};
     HANDLE paused=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
     if(!paused||!release)throw std::runtime_error("owned synchronization event missing");
     struct Cleanup{HANDLE a,b;~Cleanup(){SetEvent(b);g_steamCssGuard.shutdown();CloseHandle(a);CloseHandle(b);}} cleanup{paused,release};
     g_steamSettingsObserver.value={};g_steamSettingsObserver.value.pid=800;g_steamSettingsObserver.value.created=900;g_steamSettingsObserver.value.root=L"G:\\private-steam";
     const auto session=g_steamSettingsObserver.sessionSnapshot();
     const auto directory=std::filesystem::current_path()/("css-manual-ready-"+std::to_string(GetCurrentProcessId())+(confirmationAvailable?"-confirm":"-refuse"));std::filesystem::create_directory(directory);const auto link=directory/"backend.path";
     {std::ofstream file(link);file<<"G:\\private-css\\CssLoader-Standalone-Headless.exe";}
     Config cssGuard;cssGuard.linkFile=link.wstring();cssGuard.delays={60000,60000,60000};cssGuard.modeDelays={60000,60000,60000};cssGuard.resumeDelays={0,60000,60000};
@@CSS_MANUAL_GUARD_CONFIG@@
     cssGuard.powerGeneration=[]{return currentPowerGeneration();};cssGuard.mayLaunch=[]{ymcc::sleeppower::SharedPowerV1 power;return !g_exitRequested&&captureSharedPowerLifecycle(power);};
     cssGuard.changed=[&](const auto& state){if(state.phase=="grace"&&!pausedOnce.exchange(true)){SetEvent(paused);if(WaitForSingleObject(release,3000)!=WAIT_OBJECT_0)throw std::runtime_error("owned mailbox barrier expired");}};
     cssGuard.refreshMode=[&](const auto&){++refreshes;return !confirmationAvailable;};
     cssGuard.readMode=[](const auto&){return ModeSample{UiMode::Unknown,{}};};
     cssGuard.confirmExisting=[&](const auto&){++confirmations;if(!confirmationAvailable)return static_cast<HANDLE>(nullptr);confirmed=true;return CreateEventW(nullptr,TRUE,TRUE,nullptr);};
     cssGuard.ensure=[&](const auto&,const auto&,const auto& allowed){++ensures;if(!allowed())return Result{};return Result{CreateEventW(nullptr,TRUE,FALSE,nullptr),880,"running","inert-backend"};};
     cssGuard.confirmIdle=[&](const auto&,bool,bool){++automaticUses;confirmed=true;return CreateEventW(nullptr,TRUE,TRUE,nullptr);};
     cssGuard.isResidentConfirmed=[&](const auto&){return confirmed.load();};
     cssGuard.probeExisting=[]{return Result{CreateEventW(nullptr,TRUE,FALSE,nullptr),880,"running","inert-backend"};};
     require(g_steamCssGuard.initialize(std::move(cssGuard)),"actual guard initializes");g_steamCssGuard.observeSteam(session);
     require(WaitForSingleObject(paused,3000)==WAIT_OBJECT_0,"actual guard is paused before its original event wait");
     require(g_steamCssGuard.requestReadOnlyRebind(session,1),"actual completed manual-use handoff admitted");
     require(commitPowerResume(g_powerGeneration.load(),"fixture-coalesced-manual-ready"),"actual public Ready coalesces into the same mailbox read");SetEvent(release);
     const bool handed=waitFor([&]{return confirmations==1;});
     cssObservations.push_back(json{{"manualMailbox",{{"confirmationAvailable",confirmationAvailable},{"manualConfirmations",confirmations.load()},{"refreshes",refreshes.load()},{"automaticUses",automaticUses.load()},{"handoffDelivered",handed}}}});
     require(handed,"coalesced manual read-only handoff must survive resume mode-refresh refusal");
     if(!handed)return false;
     if(confirmationAvailable){
      require(waitFor([]{return g_steamCssGuard.snapshot().phase=="ready";}),"manual read-only helper completion reaches original resident Ready");
      Sleep(30);require(refreshes==0&&automaticUses==0&&ensures==0,"manual completed edit performs no automatic refresh/Ensure/reapply");
     }else{
      require(waitFor([&]{return refreshes==1;}),"refused read-only handoff leaves the original immediate resume slot available");
      g_steamCssGuard.observeMode(session,{UiMode::BigPicture,"owned-doc"});
      require(waitFor([&]{return automaticUses==1&&g_steamCssGuard.snapshot().phase=="ready";}),"original resume fallback still confirms one bounded automatic use");
      require(confirmations==1&&refreshes==1&&automaticUses==1,"refused manual handoff neither loops nor refunds the resume budget");
     }
     g_steamCssGuard.shutdown();require(DeleteFileW(link.c_str())&&RemoveDirectoryW(directory.c_str()),"owned manual mailbox profile returned");return true;
    };
    if(scenario(true))scenario(false);
 });

 run("normal-ready-and-late-message",[]{start();currentReady();require(count("sensors")==1&&count("msi")==1&&count("cursor")==1&&count("ipc")==1&&count("game-recovery-power")==1,"one current publication reaches every independent consumer");require(!g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&g_msiNativeForegroundLastResumeGeneration==g_powerGeneration.load(),"actual MSI scalar consumer releases private power hold");deliverProjection();require(count("sensors")==1&&count("ipc")==1&&count("game-recovery-power")==1,"late message without debt cannot replay consumers");require(!g_sleepPowerRedeliveryArmed,"healthy delivery creates no retry polling");});
 run("sensor-exception-isolated",[]{faultStage="sensors";edge="throw";start();require(count("sensors")==1&&count("msi")==1&&count("cursor")==1&&count("ipc")==1,"a failed sensor consumer cannot strand MSI/cursor/current facts");require(diagnostics==std::vector<std::string>{"sensors"},"sensor failure remains visible");currentReady();});
 run("msi-exception-isolated",[]{faultStage="msi";edge="throw";start();require(count("cursor")==1&&count("ipc")==1,"accepted MSI leaf exception cannot block later projections");require(!g_msiNativeForegroundPowerHold,"MSI acceptance remains owned despite a later UI exception");currentReady();});
 run("cursor-exception-isolated",[]{faultStage="cursor";edge="throw";start();require(count("ipc")==1,"cursor failure cannot suppress common facts");currentReady();});
 run("diagnostic-exception-isolated",[]{faultStage="sensors";edge="throw";throwDiagnostic=true;start();require(count("msi")==1&&count("cursor")==1&&count("ipc")==1,"diagnostic exception cannot break independent consumers");});
 run("battery-exception-still-queues",[]{faultStage="battery";edge="throw";start();require(posts==1&&count("sensors")==1&&count("ipc")==1,"publisher retains common projection after battery observer failure");require(diagnostics==std::vector<std::string>{"battery"},"battery observer failure is separate");});
 run("snapshot-failure-retries-readonly",[]{faultStage="snapshot";edge="throw";start();require(emitted.empty()&&g_sharedPowerProjectionPending&&arms==1,"read-only snapshot failure retains bounded delivery debt");drain();currentReady();require(count("sensors")==1&&count("msi")==1&&count("cursor")==1&&count("snapshot")==2,"snapshot retry never replays already dispatched module work");});
 run("aya-lighting-public-delivery",[]{
   g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;start();
   require(ayaAttempts==0,"nonReady cannot offer AYA restoration");
   g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;refuseAya=true;start();
   const auto sensors=count("sensors"),msi=count("msi");
   require(ayaAttempts==1&&g_sleepPowerRedeliveryArmed,"known refusal retains the AYA delivery");
   require(emitted.size()==2&&emitted.back().value("hardwareWritesAllowed",false)&&
       emitted.back().value("suspendSerial",0ull)==g_sharedPowerSuspendSerial.load(),
       "an unqueued AYA restore cannot block publication of the admitted power receipt");
   refuseAya=false;drain();currentReady();
   require(ayaAttempts==2&&count("sensors")==sensors&&count("msi")==msi,"AYA retry does not replay independently dispatched consumers");
   ++g_sharedPowerSuspendSerial;faultStage="aya-lighting";edge="throw";start();
   require(ayaAttempts==3&&diagnostics==std::vector<std::string>{"aya-lighting"}&&!g_sleepPowerRedeliveryArmed,"uncertain consumer outcome is isolated without retry");
   currentReady();
   reset();faultStage="aya-lighting";edge="pending";refuseAya=true;start();
   require(ayaAttempts==1&&!emitted.empty()&&emitted.back().value("notificationPending",false)&&
       !emitted.back().value("hardwareWritesAllowed",true),
       "a new suspend during the AYA offer still denies the old public Ready");
   settle();refuseAya=false;start();currentReady();
   require(ayaAttempts==2&&!g_sleepPowerRedeliveryArmed,"current power reoffers the unaccepted AYA restore once");
 });
 run("cpu-trial-public-delivery",[]{
   const auto initialTick=readyFixtureNow;
   struct ClockReset{ULONGLONG tick;~ClockReset(){readyFixtureNow=tick;}} clockReset{initialTick};
   const auto arm=[](const json& token,int value){
      return cpuUvTrialArm({{"token",token},{"preference",{{"schema",1},{"enabled",true},{"value",value},
          {"vendor","amd"},{"unit","co_step"},{"identity",json::object()}}},
          {"receipt",{{"accepted",true},{"vendor","amd"},{"unit","co_step"},{"requested",{{"value",value}}}}}});
   };
   const auto original=cpuUvTrialBegin().at("token");
   require(arm(original,-5).value("pending",false)&&cpuTrialTimer,"original trial is armed by the Native owner");
   const auto originalNotices=count("cpu-undervolt-state");
   start();
   require(g_cpuUvTrial.pending&&cpuTrialTimer&&count("cpu-undervolt-state")==originalNotices,
       "a metadata publication cannot cancel a current trial");
   const auto generation=g_powerGeneration.load();
   deliverDeckySuspend();
   ++g_sharedPowerSuspendSerial;g_powerLifecycle=PowerLifecycle::Suspended;g_hardwareWriteGate=false;
   publishSharedPowerLifecycle("fixture-suspend");
   g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;
   publishSharedPowerLifecycle("fixture-resume-before-window-delivery");
   faultStage="cpu-undervolt-state";edge="throw";deliverProjection();
   require(g_powerGeneration.load()==generation&&!g_cpuUvTrial.pending&&!cpuTrialTimer&&
       g_cpuUvTrial.stage=="suspended"&&count("cpu-undervolt-state")==originalNotices+1,
       "coalesced Ready retires the old trial once even when its notification fails");
   currentReady();
   start();
   require(count("cpu-undervolt-state")==originalNotices+1,"later publication cannot cancel the old trial twice");
   readyFixtureNow=initialTick+30000;cpuUvTrialTick();
   require(cpuTrialWrites==0&&!arm(original,-5).value("ok",true),"stale timer and apply receipt cannot save the old trial");
   const auto next=cpuUvTrialBegin().at("token");
   require(arm(next,-8).value("pending",false)&&cpuTrialTimer,"new trial arms after Ready without manual reset");
   readyFixtureNow+=30000;cpuUvTrialTick();
   require(cpuTrialWrites==1&&!cpuTrialTimer&&cpuTrialDisk.at("cpu").at("undervolt").at("value")==-8,
       "only the fresh post-wake trial can save its settings");
 });
 run("power-source-public-delivery",[]{
   ymcc::powerSource::setExternalReader(acdcFixtureSource);
   struct ResetSource{~ResetSource(){ymcc::powerSource::setExternalReader(nullptr);}} resetSource;
   const auto first=acdcReceipts;
   beginPowerSourceRecovery(g_hwnd,false);
   require(g_powerSourceRecoveryRunning&&acdcReceipts==first+1,"startup arms original heartbeat");
   start();require(acdcReceipts==first+1,"matching public Ready does not repeat the startup sample");
   const auto pending=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPEND,++serial);
   auto before=acdcReceipts;
   reconcileAcDcSourceHeartbeat(g_hwnd);
   require(g_powerSourceRecoveryRunning&&acdcReceipts==before,"late pending notification cannot sample or retire live heartbeat");
   g_sleepPowerDispatch.complete(pending,++serial);settle();
   ++g_sharedPowerSuspendSerial;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
   before=acdcReceipts;
   start();
   require(!g_powerSourceRecoveryRunning&&acdcReceipts==before,"public NonReady retires prior heartbeat without a raw suspend or a timer tick");
   reconcileAcDcSourceHeartbeat(g_hwnd);
   require(!g_powerSourceRecoveryRunning&&acdcReceipts==before,"a queued stale timer stays retired");
   g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;start();
   require(g_powerSourceRecoveryRunning&&acdcReceipts==before+1,"new same-generation public Ready rearms original sampler");
   before=acdcReceipts;start();require(acdcReceipts==before,"metadata publication does not replay the sampler");
   reconcileAcDcSourceHeartbeat(g_hwnd);
   require(acdcReceipts==before+(g_powerSourceRecoveryRunning?1u:0u),"timer only samples if recovery remains armed");
   before=acdcReceipts;beginPowerSourceRecovery(g_hwnd,true);
   require(g_powerSourceRecoveryRunning&&acdcReceipts==before+1,"explicit AC/DC source edge still refreshes");
   const auto monitorCalls=acdcMonitorCalls;
   ++g_sharedPowerSuspendSerial;acdcMonitorFails=true;start();acdcMonitorFails=false;
   require(g_powerSourceRecoveryRunning&&acdcReceipts==before+2,"monitor admission failure must still deliver the first receipt and arm the heartbeat");
   require(diagnostics==std::vector<std::string>{"power-source-monitor"}&&acdcMonitorCalls==monitorCalls+1,"uncertain monitor admission is isolated");
   start();require(acdcReceipts==before+2&&acdcMonitorCalls==monitorCalls+1,"metadata cannot replay uncertain source admission");
   currentReady();
 });
 run("power-source-refused-timer-continues-current-scope",[]{
   ymcc::powerSource::setExternalReader(acdcFixtureSource);
   struct ResetSource{~ResetSource(){refuseAcDcTimer=false;ymcc::powerSource::setExternalReader(nullptr);}} resetSource;
   const auto before=acdcReceipts,attempts=acdcTimerAttempts;
   const auto serialBefore=g_powerSourceRecoverySerial;
   refuseAcDcTimer=true;start();
   require(acdcReceipts==before+1&&acdcTimerAttempts==attempts+1&&!g_powerSourceRecoveryRunning,
       "the first current scope samples once and retains the refused timer");
   require(g_sleepPowerRedeliveryArmed&&g_sharedPowerProjectionPending&&emitted.size()==1&&emitted[0].value("hardwareWritesAllowed",false),
       "refused AC/DC timer owes a retry without withholding Ready facts from other consumers");
   refuseAcDcTimer=false;drain();
   require(g_powerSourceRecoveryRunning&&acdcTimerAttempts==attempts+2&&acdcReceipts==before+1&&g_powerSourceRecoverySerial==serialBefore+1,
       "public redelivery arms the same recovery window without repeating its sample or refunding its deadline");
   start();
   require(acdcTimerAttempts==attempts+2&&acdcReceipts==before+1,"metadata cannot restart repaired heartbeat");
   currentReady();
   refuseAcDcTimer=true;beginPowerSourceRecovery(g_hwnd,true);
   require(projectionQueue&&g_sharedPowerProjectionPending&&g_powerSourceRecoveryTimerPending&&
       acdcReceipts==before+2&&acdcTimerAttempts==attempts+3,"explicit source edge hands refused timer back to public delivery");
   ++g_sharedPowerSuspendSerial;g_powerLifecycle=PowerLifecycle::Suspended;g_hardwareWriteGate=false;
   refuseAcDcTimer=false;start();
   require(!g_powerSourceRecoveryTimerPending&&!g_powerSourceRecoveryRunning&&acdcTimerAttempts==attempts+3&&acdcReceipts==before+2,
       "new sleep discards the old refused timer without sampling or attempting its registration");
   g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;start();
   require(g_powerSourceRecoveryRunning&&acdcTimerAttempts==attempts+4&&acdcReceipts==before+3&&g_powerSourceRecoverySerial==serialBefore+3,
       "the next Ready creates exactly one new owned window");
   const auto receiptAttempts=acdcReceiptAttempts,monitorCalls=acdcMonitorCalls;
   ++g_sharedPowerSuspendSerial;acdcReceiptFails=true;refuseAcDcTimer=true;start();
   acdcReceiptFails=false;refuseAcDcTimer=false;
   require(g_powerSourceRecoveryTimerPending&&g_sleepPowerRedeliveryArmed&&!g_powerSourceRecoveryRunning&&
       diagnostics==std::vector<std::string>{"power-source-receipt"},"first receipt failure cannot erase refused timer delivery or poison the Ready projection");
   drain();require(g_powerSourceRecoveryRunning&&acdcTimerAttempts==attempts+6&&acdcReceipts==before+3&&
       acdcReceiptAttempts==receiptAttempts+1&&acdcMonitorCalls==monitorCalls+1&&g_powerSourceRecoverySerial==serialBefore+4,
       "public redelivery repairs only the retained timer and preserves the source scope");
   reconcileAcDcSourceHeartbeat(g_hwnd);
   require(acdcReceipts==before+4&&acdcReceiptAttempts==receiptAttempts+2,"live heartbeat publishes the next sample after the initial receipt failed");
   currentReady();
 });
 run("ipc-failure-retries-facts-only",[]{faultStage="ipc";edge="throw";start();require(g_sharedPowerProjectionPending&&arms==1,"uncertain read-only event delivery is retained");drain();currentReady();require(count("sensors")==1&&count("msi")==1&&count("cursor")==1&&count("ipc")==2,"duplicate current-fact delivery cannot replay accepted business consumers");});
 run("failed-post-original-timer",[]{failPost=true;publishSharedPowerLifecycle("failed-post");require(g_sharedPowerProjectionPending&&g_sleepPowerRedeliveryArmed&&arms==1&&calls.size()==1,"failed original window post retains current projection debt");failPost=false;drain();currentReady();});
 run("game-wake-terminal-retries-without-replaying-power-consumers",[]{
  g_wakeGameRecoveryStatusWindow=g_hwnd;
  g_children.emplace_back(1,&fixtureIpcChild);
  start();currentReady();
  const auto powerRevision=g_sharedPowerPublicationRevision.load(),oldTerminal=g_gameWakeRecoveryRevision.load();
  const auto sensors=count("sensors"),battery=count("battery"),msi=count("msi"),cursor=count("cursor");
  const auto mainPosts=sharedFactMainPosts,childPosts=sharedFactChildPosts;
  sharedFactRefusals=1;
  postWakeGameRecoveryStatus();
  require(productionSharedPowerLifecycleSnapshot().at("gameWakeRecoveryRevision")==oldTerminal+1&&projectionQueue,
      "the settled game observation is immediately readable and queues the common projection");
  deliverProjection();
  require(g_sharedPowerProjectionPending&&g_sleepPowerRedeliveryArmed&&sharedFactMainPosts==mainPosts+1,
      "a rejected game completion uses the common fact-only retry");
  drain();currentReady();
  require(emitted.back().at("gameWakeRecoveryRevision")==oldTerminal+1&&sharedFactMainPosts==mainPosts+2&&sharedFactChildPosts==childPosts+1,
      "only the main WebView retries the retained completion fact");
  require(g_sharedPowerPublicationRevision==powerRevision&&count("sensors")==sensors&&count("battery")==battery&&count("msi")==msi&&count("cursor")==cursor,
      "game completion does not advance the power revision or replay dispatched consumers");
  postWakeGameRecoveryStatus();
  g_powerLifecycle=PowerLifecycle::Suspending;g_hardwareWriteGate=false;++g_sharedPowerSuspendSerial;
  publishSharedPowerLifecycle("new-suspend");deliverProjection();
  require(!emitted.back().value("hardwareWritesAllowed",true)&&emitted.back().at("gameWakeRecoveryRevision")==oldTerminal+2,
      "terminal metadata carries no admission across a newer suspend");
  g_wakeGameRecoveryStatusWindow=nullptr;
 });
 run("main-webview-refusal-replays-only-current-read-only-fact",[]{
  g_children.emplace_back(1,&fixtureIpcChild);sharedFactRefusals=1;
  start();const auto firstCalls=calls.size();const auto firstSnapshots=count("snapshot");
  require(sharedFactMainPosts==1&&sharedFactChildPosts==1&&emitted.empty()&&g_sharedPowerProjectionPending&&g_sleepPowerRedeliveryArmed,
      "a refused current fact owes retry even when the window message and child delivery succeeded");
  drain();currentReady();
  require(sharedFactMainPosts==2&&sharedFactChildPosts==1&&emitted.size()==1&&count("snapshot")==firstSnapshots+1&&calls.size()==firstCalls+2,
      "only the main fact retries; Native business consumers and child posts are not repeated");
  reset();g_children.emplace_back(1,&fixtureIpcChild);sharedFactRefusals=1;
  start();const auto beforeIngress=g_sharedPowerPublicationRevision.load();
  require(sharedFactChildLast.value("hardwareWritesAllowed",false),"the child initially received the Ready refused by the main view");
  g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return true;});
  deliverProjection();
  require(g_sharedPowerPublicationRevision==beforeIngress&&sharedFactMainPosts==2&&emitted.size()==1&&
      !emitted.back().value("hardwareWritesAllowed",true)&&emitted.back().value("notificationPending",false),
      "ordered suspend ingress replaces the refused Ready even before the publication revision moves");
  require(sharedFactChildPosts==2&&!sharedFactChildLast.value("hardwareWritesAllowed",true)&&
      sharedFactChildLast.value("notificationPending",false),
      "the changed pending-suspend fact cannot inherit the old Ready's main-only retry route");
  reset();g_children.emplace_back(1,&fixtureIpcChild);sharedFactRefusals=3;refuseWallpaperPost=true;
  start();productionTimer();productionTimer();
  const auto exhaustedRevision=g_sharedPowerPublicationRevision.load();
  require(sharedFactMainPosts==3&&sharedFactChildPosts==1&&g_sharedPowerProjectionPending&&g_sleepPowerRedeliveryArmed,
      "wallpaper retains only its own debt after the old Ready has spent three main attempts");
  g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return true;});
  deliverProjection();
  require(g_sharedPowerPublicationRevision==exhaustedRevision&&sharedFactMainPosts==4&&emitted.size()==1&&
      !emitted.back().value("hardwareWritesAllowed",true)&&emitted.back().value("notificationPending",false)&&
      sharedFactChildPosts==2&&sharedFactChildLast.value("notificationPending",false),
      "an already exhausted Ready cannot suppress a strictly newer suspend observation on either view");
  for(const bool persistent:{false,true}){
   reset();failProjectionTimer=true;sharedFactRefusals=persistent?100:1;
   if(!g_sleepPowerDispatch.openReceiptWake())throw std::runtime_error("projection fallback requires the original owner event");
   const auto wake=g_sleepPowerDispatch.receiptWakeHandle();
   start();const auto callsBeforeRetry=calls.size();const auto snapshotsBeforeRetry=count("snapshot");
   const unsigned retries=persistent?2:1;
   for(unsigned i=0;i<retries;++i){
    const bool signaled=WaitForSingleObject(wake,0)==WAIT_OBJECT_0;
    const bool pending=g_sleepPowerDispatch.takeProjectionWake();
    require(signaled&&pending&&g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed,
        "a refused retry timer hands the pending read-only projection to the original owner event");
    if(signaled&&pending)fixtureSendMessage(g_hwnd,kSharedPowerStateMessage,0,0);
   }
   require(sharedFactMainPosts==1+retries&&emitted.size()==(persistent?0u:1u)&&
       count("snapshot")==snapshotsBeforeRetry+retries,"the fallback reads current facts within the same retry budget");
   if(!persistent)currentReady();
   require(calls.size()==callsBeforeRetry+retries+(persistent?0u:1u)&&!g_sharedPowerProjectionPending&&
       WaitForSingleObject(wake,0)==WAIT_TIMEOUT&&!g_sleepPowerDispatch.takeProjectionWake(),
       "fallback reaches idle without replaying business or signaling beyond the budget");
   g_sleepPowerDispatch.closeReceiptWake();
  }
  reset();g_view=false;g_children.emplace_back(1,&fixtureIpcChild);start();
  require(sharedFactMainPosts==0&&sharedFactChildPosts==1&&!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed,
      "an absent main view leaves no delivery debt and does not withhold the child fact");
  reset();sharedFactRefusals=100;start();drain();
  require(sharedFactMainPosts==3&&emitted.empty(),"persistent refusal stops after three total attempts");
  sharedFactRefusals=0;start();currentReady();
  require(sharedFactMainPosts==4,"a new publication can deliver after the older budget stops");
  reset();sharedFactRefusals=1;start();
  g_hardwareWriteGate=false;g_powerLifecycle=PowerLifecycle::Suspended;
  ++g_powerGeneration;++g_sharedPowerSuspendSerial;
  publishSharedPowerLifecycle("new-suspend-after-refused-fact");drain();deliverProjection();
  require(emitted.size()==1&&emitted.back().value("phase",std::string{})=="suspended"&&
      !emitted.back().value("hardwareWritesAllowed",true),"a later suspend replaces the rejected Ready fact before transport retries");
  reset();failProjectionTimer=true;sharedFactRefusals=100;
  g_children.emplace_back(1,&fixtureIpcChild);
  start();
  const auto beforeRetry=count("snapshot");
  require(!g_sleepPowerDispatch.receiptWakeHandle()&&projectionQueue.has_value(),
      "an unavailable owner event and refused timer queue a tagged retry on the existing window");
  for(unsigned i=0;i<4&&projectionQueue;++i)deliverProjection();
  require(sharedFactMainPosts==3&&sharedFactChildPosts==1&&count("snapshot")==beforeRetry+2&&
      count("sensors")==1&&count("msi")==1&&!projectionQueue&&!g_sharedPowerProjectionPending,
      "window fallback observes the same budget and does not repeat other consumers or child delivery");
  reset();failProjectionTimer=true;sharedFactRefusals=1;
  publishSharedPowerLifecycle("fixture-before-endpoints-refuse");
  failProjectionPost=true;deliverProjection();
  require(posts==2&&arms==1&&!projectionQueue&&!g_sleepPowerRedeliveryArmed&&g_sharedPowerProjectionPending&&
      !g_sleepPowerDispatch.takeProjectionWake()&&sharedFactMainPosts==1,
      "all retry endpoints refusing leaves pending debt without another scheduled attempt");
  const auto publication=g_sharedPowerPublicationRevision.load();
  failProjectionPost=false;publishSharedPowerLifecycle("fixture-next-real-publication");
  deliverProjection();currentReady();
  require(g_sharedPowerPublicationRevision==publication+1&&sharedFactMainPosts==2&&!g_sharedPowerProjectionPending,
      "a subsequent real publication can settle debt parked after refusals");
 });
 run("burst-coalesces-current-facts",[]{for(unsigned i=0;i<50;++i)publishSharedPowerLifecycle("burst");require(posts==1&&!g_sleepPowerRedeliveryArmed,"publication burst owns one original window message");deliverProjection();require(emitted.size()==1&&emitted[0].at("publicationRevision")==g_sharedPowerPublicationRevision.load(),"coalesced event uses latest publication");});
 run("pending-before-ui-consume",[]{
    const auto generation=g_powerGeneration.load();
    g_sensorSuspendCallbackActive=true;
    g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});start();
    require(count("sensors")==0&&g_sensorSuspendCallbackActive&&msiAttempts==0&&g_msiNativeForegroundPowerHold,
        "actual Dispatch pending refuses Ready-only consumers and preserves the sensor suspend latch");
    require(count("game-recovery-power")==1,"pending suspend signals the parked game recovery worker without granting Ready");
    require(count("cursor")==1&&emitted.back().value("notificationPending",false)&&!emitted.back().value("hardwareWritesAllowed",true),
        "pending is still published as current diagnostic facts");
    g_hardwareWriteGate=false;settle();start();
    require(count("sensors")==0&&g_sensorSuspendCallbackActive,"completion alone cannot resume sensors while the query gate is closed");
    g_hardwareWriteGate=true;start();currentReady();deliverProjection();
    require(count("sensors")==1&&!g_sensorSuspendCallbackActive&&g_powerGeneration.load()==generation&&
        !g_msiNativeForegroundPowerHold&&count("game-recovery-power")==3,
        "gate reopening delivers same-generation Ready once without a raw resume notification");
 });
 run("wallpaper-retires-lease-on-public-suspend",[]{
  auto& owner=steamWallpaperForegroundOwner();
  ymcc::steamwallpaperForeground::Facts facts;
  facts.known=facts.physical=facts.enabled=facts.ready=true;
  facts.pid=facts.steamPid=7;facts.birth=facts.steamBirth=8;facts.window=9;
  facts.modeEpoch="fixture-document";facts.modeRevision=1;
  facts.power=g_powerGeneration;facts.suspendSerial=g_sharedPowerSuspendSerial;
  ymcc::sleeppower::SharedPowerV1 firstPower;
  require(captureSharedPowerLifecycle(firstPower),"initial read captures admitted power");
  const auto first=owner.observeForPower(facts,owner.ticket(),firstPower,readSharedSystemPower);
  const auto oldTicket=owner.ticket();
  require(first.value("current",false),"initial foreground proof is current");
  g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});
  auto stale=owner.observeForPower(facts,oldTicket,firstPower,readSharedSystemPower);
  require(!stale.value("current",true)&&stale.value("reason","")=="foreground-changed-during-read"&&owner.ticket()==oldTicket,
          "pending ingress refuses an in-flight proof before the window can project");
  start();
  require(owner.ticket()==oldTicket&&wallpaperAttempts==0,"temporary pending admission without a new scope does not revoke the current owner");
  settle();
  ++g_sharedPowerSuspendSerial;g_powerLifecycle=PowerLifecycle::Suspended;g_hardwareWriteGate=false;
  start(); // No WM_POWERBROADCAST and no foreground read takes place.
  require(owner.ticket()!=oldTicket&&wallpaperAttempts==0,"public NonReady retires the previous lease without a raw suspend or foreground read");
  require(!owner.observeForPower(facts,oldTicket,firstPower,readSharedSystemPower).value("current",true),"an old in-flight read cannot return a positive proof");
  g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;start();
  ymcc::sleeppower::SharedPowerV1 recoveredPower;
  require(captureSharedPowerLifecycle(recoveredPower),"successor read captures current Ready");
  const auto recovered=owner.observeForPower(facts,owner.ticket(),recoveredPower,readSharedSystemPower);
  require(recovered.value("current",false)&&recovered.at("ownerEpoch")!=first.at("ownerEpoch")&&wallpaperPosts==1,
          "current public Ready queues the existing read and admits the new lease");
  ++g_sharedPowerSuspendSerial; // Coalesced Ready, with a direct read before UI delivery.
  const auto beforeCoalesced=owner.ticket();
  stale=owner.observeForPower(facts,beforeCoalesced,recoveredPower,readSharedSystemPower);
  require(!stale.value("current",true)&&owner.ticket()==beforeCoalesced,"coalesced Ready rejects the old scope before projection");
  ymcc::sleeppower::SharedPowerV1 nextPower;
  require(captureSharedPowerLifecycle(nextPower),"coalesced Ready admits the direct read");
  const auto next=owner.observeForPower(facts,owner.ticket(),nextPower,readSharedSystemPower);
  const auto nextTicket=owner.ticket();
  require(next.value("current",false)&&nextTicket!=beforeCoalesced,"direct read commits the successor scope");
  g_steamWallpaperForegroundPending=false;
  start();
  require(owner.ticket()==nextTicket&&wallpaperPosts==2,"delayed Ready offers one read without revoking an accepted successor proof");
  owner.retirePreviousPower(recoveredPower);
  require(owner.observeForPower(facts,nextTicket,nextPower,readSharedSystemPower)==next,"an older projection cannot revoke the successor lease");
 });
 run("wallpaper-current-public-ready",[]{
  const auto suspendSerial=g_sharedPowerSuspendSerial.load();
  const auto generation=g_powerGeneration.load();
  g_sharedPowerSuspendSerial=0;start();
  require(wallpaperAttempts==0,"ordinary startup Ready keeps original startup observers");
  g_sharedPowerSuspendSerial=suspendSerial;
  g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});
  start();require(wallpaperAttempts==0,"pending suspend cannot read wallpaper foreground");
  settle();refuseWallpaperPost=true;start();
  require(wallpaperAttempts==1&&wallpaperPosts==0&&!g_steamWallpaperForegroundPending&&g_sleepPowerRedeliveryArmed,
          "refused wallpaper read leaves retry debt with the original projection");
  require(!emitted.empty()&&emitted.back().value("hardwareWritesAllowed",false)&&
          emitted.back().value("suspendSerial",0ull)==suspendSerial,
          "a refused wallpaper read cannot withhold current public Ready from other consumers");
  const auto sensorAdmissions=count("sensors"),msiAdmissions=count("msi");
  refuseWallpaperPost=false;drain();
  require(wallpaperPosts==1&&wallpaperAttempts==2&&g_steamWallpaperForegroundPending&&g_powerGeneration==generation,
          "the original retry delivers one read on same-generation Ready");
  require(count("sensors")==sensorAdmissions&&count("msi")==msiAdmissions,
          "retrying the refused wallpaper read does not replay independent consumers");
  g_steamWallpaperForegroundPending=false; // Existing foreground message consumed.
  for(const auto event:{PBT_APMRESUMEAUTOMATIC,PBT_APMRESUMESUSPEND,PBT_APMRESUMECRITICAL}){
   const auto receipt=g_sleepPowerDispatch.window(event,++serial);g_sleepPowerDispatch.complete(receipt,++serial);
   start();
  }
  require(wallpaperAttempts==2,"late duplicate receipt and publication cannot re-read the same sleep scope");
  ++g_sharedPowerSuspendSerial;start();
  require(wallpaperPosts==2&&g_powerGeneration==generation,"a coalesced new suspend serial re-reads without a generation change");
  ++g_sharedPowerSuspendSerial;start(); // Already queued; its handler samples live facts.
  require(wallpaperPosts==2&&wallpaperAttempts==3,"current Ready coalesces with an accepted read");
  g_steamWallpaperForegroundPending=false;start();
  require(wallpaperAttempts==3&&!g_sleepPowerRedeliveryArmed,"the delivered scope leaves no retry owner");

  reset();g_children.emplace_back(1,&fixtureIpcChild);refuseWallpaperPost=true;start();currentReady();
  const auto independentAdmissions=count("sensors");
  drain();currentReady();
  require(wallpaperAttempts==4&&wallpaperPosts==0&&count("sensors")==independentAdmissions,
          "persistent refusal parks wallpaper after the original three retries without replaying sensors");
  require(sharedFactMainPosts==1&&sharedFactChildPosts==1,
          "wallpaper-only retries do not repeat an unchanged fact already sent to both views");
  const auto expiredAttempts=wallpaperAttempts;
  ++g_sharedPowerSuspendSerial;g_powerLifecycle=PowerLifecycle::Suspended;g_hardwareWriteGate=false;start();
  require(wallpaperAttempts==expiredAttempts&&!emitted.back().value("hardwareWritesAllowed",true)&&!g_sleepPowerRedeliveryArmed,
          "new suspend replaces parked wallpaper debt without admitting any read");
  refuseWallpaperPost=false;g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;start();currentReady();
  require(wallpaperAttempts==expiredAttempts+1&&wallpaperPosts==1&&!g_sleepPowerRedeliveryArmed,
          "the new Ready offers only the current scope's wallpaper read");
 });
 run("sensor-pending-retires-msi",[]{faultStage="sensors";edge="pending";start();require(msiAttempts==0&&g_msiNativeForegroundPowerHold,"notification ingress during sensor callback cannot reach old MSI Ready release");require(emitted.back().value("notificationPending",false),"snapshot sees ingress after the callback");settle();start();require(!g_msiNativeForegroundPowerHold,"current Ready later releases original private hold");});
 run("msi-raw-wake-public-continuation",[]{
  reset();
  const auto generation=g_powerGeneration.load();
  g_msiNativeForegroundResumePending=false;
  g_lastPowerResumeCommitTick=100;
  msiNativeForegroundPowerEvent(PBT_APMSUSPEND);
  require(g_msiNativeForegroundPowerHold&&count("msi")==1,"admitted suspend still queues original fence work");
  ++g_sharedPowerSuspendSerial;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
  for(const auto code:{PBT_APMRESUMEAUTOMATIC,PBT_APMRESUMESUSPEND,PBT_APMRESUMECRITICAL})
   msiNativeForegroundPowerEvent(code);
  require(!g_msiNativeForegroundResumePending&&count("msi")==1,"raw resume must not mutate or enqueue foreground work");
  g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;
  require(!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold,
      "notification and unchanged shared commit cannot release a suspended owner");
  start();currentReady();
  require(!g_msiNativeForegroundPowerHold&&count("msi")==2,"same-generation public Ready releases exactly once without a new commit");
  msiNativeForegroundPowerEvent(PBT_APMRESUMESUSPEND);start();
  require(count("msi")==2,"late raw resume and metadata never reopen current foreground work");
  msiNativeForegroundPowerEvent(PBT_APMSUSPEND);++g_sharedPowerSuspendSerial;
  g_lastPowerResumeCommitTick=101;g_resumeReadyGeneration=generation-1;
  require(!msiNativeForegroundTryReleasePowerHold(),"maintenance refuses an unrelated commit");
  g_resumeReadyGeneration=generation;g_inputReady=false;
  require(!msiNativeForegroundTryReleasePowerHold(),"maintenance waits for input restoration");
  g_inputReady=true;g_hardwareWriteGate=false;
  require(!msiNativeForegroundTryReleasePowerHold(),"maintenance waits for a current public power receipt");
  g_hardwareWriteGate=true;
  require(msiNativeForegroundTryReleasePowerHold()&&!g_msiNativeForegroundPowerHold&&count("msi")==4,
      "new current commit can catch up before the window delivery");
  start();require(count("msi")==4,"window delivery cannot replay maintenance catch-up");
  g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});
  start();require(!g_msiNativeForegroundPowerHold&&count("msi")==4,
      "an unclassified notification must not latch the foreground owner");
  settle();
  ++g_sharedPowerSuspendSerial;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
  faultStage="focus-isolation";edge="pending";
  const auto published=emitted.size();
  start();require(!g_msiNativeForegroundPowerHold&&count("msi")==4&&emitted.size()==published,
      "changed public observation cannot latch stale foreground work or finish the projection");
  const auto replay=g_sleepPowerDispatch.pendingReplay();
  require(static_cast<bool>(replay),"the replacing notification is still owed by Dispatch");
  if(replay){
   publishIngressCompletion=true;
   fixtureSendMessage(g_hwnd,kSleepPowerForwardMessage,replay.type,replay.sequence);
   deliverProjection();
  }
  require(g_msiNativeForegroundPowerHold&&count("msi")==5,
      "public Resuming without raw suspend queues the original foreground fence");
  start();require(g_msiNativeForegroundPowerHold&&count("msi")==5,
      "duplicate public non-Ready does not queue the foreground fence again");
  g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;
  start();require(!g_msiNativeForegroundPowerHold&&count("msi")==6,
      "same-generation public Ready releases the public-only foreground fence once");
 });
 run("msi-ready-revalidates-at-private-hold-release",[]{
  for(const auto* boundary:{"serial","notification"}){
   reset();faultStage="msi-ready-admission";edge=boundary;const auto generation=g_powerGeneration.load();start();
   require(g_powerGeneration==generation&&hardwareWriteAllowed()&&msiAttempts==1,"injection reaches private release with the same generation still Ready");
   require(g_msiNativeForegroundPowerHold&&g_msiNativeForegroundResumePending&&g_msiNativeForegroundPowerTransitionObserved&&count("msi")==0,"same-generation stale Ready must leave the private hold and its UI action untouched");
   faultStage.clear();start();
   require(!g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&count("msi")==1,"fresh common Ready clears the hold once without a raw resume entering the private consumer");
   start();require(count("msi")==1,"repeated current projection cannot replay the private release");
  }
  for(const auto* boundary:{"serial","notification"}){
   reset();faultStage="msi-ready-admission";edge=boundary;start();
   const auto publication=g_sharedPowerPublicationRevision.load();
   require(msiAttempts==1&&g_msiNativeForegroundPowerHold&&count("msi")==0,"stale private callback makes no isolation change");
   require(g_sharedPowerProjectionPending&&g_sleepPowerRedeliveryArmed,"the original timer owns the interrupted Ready delivery");
   faultStage.clear();drain();currentReady();
   require(g_sharedPowerPublicationRevision==publication&&msiAttempts==2&&count("msi")==1&&
    !g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending,"the same publication must retry unaccepted private Ready and release the original hold once");
   require(count("ui-presentation")==1&&count("focus-isolation")==1&&count("sensors")==1&&
    count("input-host-rearm")==1,"same-publication retry does not replay accepted isolation siblings or virtual readiness");
   deliverProjection();
   require(count("msi")==1&&count("input-host-rearm")==1,"late posted delivery cannot replay either accepted Ready effect");
  }
 });
 run("controller-isolation-shared-power-order",[]{
  const auto prepareFocus=[] {
   const auto ticket=std::make_shared<FocusLightIsolationTicket>();
   require(captureSharedPowerLifecycle(ticket->sharedPower),"light ticket starts in a current Ready period");
   g_focusIsolationPowerPeriod={ticket->sharedPower.generation,ticket->sharedPower.suspendSerial};
   g_focusLightIsolationTicket.store(ticket);g_focusLightFallbackObservation=std::make_shared<const bool>(true);
   g_focusIsolationPrepared=true;
   return ticket;
  };
  const auto requireRevoked=[] {
   require(!g_focusIsolationPrepared&&!g_focusLightIsolationTicket.load()&&!g_focusLightFallbackObservation&&count("focus-end")==1&&count("focus-clear")==1,"old ticket and fallback retire exactly once through the actual focus consumer");
  };
  prepareFocus();
  publishSharedPowerLifecycle("old-ready");
  ++g_sharedPowerSuspendSerial;g_powerLifecycle=PowerLifecycle::Suspended;g_hardwareWriteGate=false;
  start();requireRevoked();
  require(posts==1&&msiAttempts==0&&g_msiNativeForegroundPowerHold,"coalesced older Ready cannot release the ordinary isolation hold during suspend");
  g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;
  start();requireRevoked();
  require(count("msi")==1&&!g_msiNativeForegroundPowerHold,"only the current Ready releases ordinary isolation");
  const auto newTicket=prepareFocus();
  deliverProjection();start();
  require(count("msi")==1&&count("focus-clear")==1&&g_focusLightIsolationTicket.load()==newTicket,"late and duplicate Ready do not clear the new ticket or replay ordinary isolation");
  for(const auto* fault:{"ui-presentation","focus-isolation"}){
   reset();prepareFocus();
   ++g_sharedPowerSuspendSerial;
   faultStage=fault;edge="reader-ingress";
   start();
   require(count("focus-clear")==0&&count("msi")==0&&g_msiNativeForegroundPowerHold&&g_sharedPowerProjectionPending,"moving Reader at focus revalidation defers both actions");
   drain();requireRevoked();
   std::vector<std::string> isolationOrder;
   for(const auto& call:calls)if(call.name=="focus-end"||call.name=="focus-clear"||call.name=="msi")isolationOrder.push_back(call.name);
   require(isolationOrder==std::vector<std::string>{"focus-end","focus-clear","msi"},"current same-generation Ready must retire old focus before ordinary isolation schedules once");
   require(count("input-host-rearm")==1,"independent input-host Ready is still delivered once");
  }
  reset();
  const auto beforeHidden=prepareFocus();
  g_focusSessionPowerPeriod={beforeHidden->sharedPower.generation,beforeHidden->sharedPower.suspendSerial};
  start();
  focusEndIsolationOnUi(); // WM_SHOWWINDOW ended the lease; the ordinary return still owns its timer.
  require(!g_focusIsolationPrepared&&g_focusSessionPowerPeriod.first&&count("focus-clear")==0,
      "hiding the window leaves its returning focus session alive");
  ++g_sharedPowerSuspendSerial;
  start();
  require(!g_focusSessionPowerPeriod.first&&count("focus-clear")==1&&count("focus-end")==1,
      "coalesced Ready must retire hidden ordinary return without ending isolation twice");
  g_focusSessionPowerPeriod={g_powerGeneration.load(),g_sharedPowerSuspendSerial.load()};
  const auto currentSession=g_focusSessionPowerPeriod;
  require(focusIsolationSharedPowerChanged(beforeHidden->sharedPower)&&g_focusSessionPowerPeriod==currentSession,
      "old Ready does not clear the current ordinary session");
  start();
  require(g_focusSessionPowerPeriod==currentSession&&count("focus-clear")==1,
      "duplicate current Ready does not clear the new ordinary session");
  reset();
  g_focusSessionPowerPeriod={g_powerGeneration.load(),g_sharedPowerSuspendSerial.load()};
  g_focusSession={true,false,true,GetTickCount64(),GetTickCount64()+2200};focusTimerArmed=true;
  productionFocusReturnTimer(RETURN_GAME_FOCUS_TIMER_ID);
  require(focusTimerArmed&&count("focus-return-action")==1,"current ordinary return timer can still make progress");
  const auto pendingFocus=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPEND,++serial);
  productionFocusReturnTimer(RETURN_GAME_FOCUS_TIMER_ID);
  require(focusTimerArmed&&count("focus-return-action")==1,"pending notification blocks the return timer and retains cleanup ownership");
  g_sleepPowerDispatch.complete(pendingFocus,++serial);
  ++g_sharedPowerSuspendSerial;
  productionFocusReturnTimer(RETURN_GAME_FOCUS_TIMER_ID);
  require(!g_focusSession.active&&!g_focusSession.returning&&!focusTimerArmed&&count("focus-return-action")==1&&count("focus-clear")==1,
      "actual queued return timer retires before public delivery and never refocuses its old window");
  g_focusSessionPowerPeriod={g_powerGeneration.load(),g_sharedPowerSuspendSerial.load()};
  g_focusSession.active=true;
  productionFocusReturnTimer(RETURN_GAME_FOCUS_TIMER_ID);
  require(g_focusSession.active&&count("focus-return-action")==1&&count("focus-clear")==1,"queued old return leaves a new non-returning session alone");
  reset();
  require(captureSharedPowerLifecycle(g_radialOverlayPower),"wheel starts with a current public period");
  g_radialOverlay.open=true;
  publishSharedPowerLifecycle("wheel-before-sleep");
  ++g_sharedPowerSuspendSerial;
  start();
  require(!g_radialOverlay.active()&&count("radial-close")==1&&count("radial-invalidate")==1,
      "coalesced same-generation Ready retires the old wheel and input ownership without a raw callback");
  require(captureSharedPowerLifecycle(g_radialOverlayPower),"new wheel uses the successor period");
  g_radialOverlay.open=true;
  start();
  require(g_radialOverlay.active()&&count("radial-close")==1,"late duplicate Ready cannot close the new wheel");
  g_radialOverlay.open=false;g_radialReturning=true;
  ++g_sharedPowerSuspendSerial;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
  start();
  require(!g_radialReturning&&count("radial-invalidate")==2&&count("radial-close")==1,
      "public boundary also retires a normally closed wheel's outstanding input return");
  reset();
  json request={{"nonce","wheel"},{"frame",json::object()},{"powerGeneration",g_powerGeneration.load()},{"powerSuspendSerial",g_sharedPowerSuspendSerial.load()}};
  const auto initialize=[](const ymcc::sleeppower::SharedPowerV1& power){stage("radial-configure",power.generation);};
  const auto rejected=[&]{try{(void)radialCreateForPower(request,initialize);}catch(const std::runtime_error&){return true;}return false;};
  ++g_sharedPowerSuspendSerial;
  require(rejected()&&count("radial-configure")==0&&count("radial-create")==0,"old queued create cannot configure a wheel in the successor period");
  request["powerSuspendSerial"]=g_sharedPowerSuspendSerial.load();
  faultStage="radial-create";edge="advance";
  require(rejected()&&!g_radialOverlay.active()&&count("radial-close")==1&&!g_radialOverlayCreating,"power moves during create: return the window and do not leave creation locked");
  require(emitted.empty(),"reentrant successor projection waits for the in-progress create to return its window");
  faultStage="radial-create";edge="throw";injected=false;
  request["powerGeneration"]=g_powerGeneration.load();request["powerSuspendSerial"]=g_sharedPowerSuspendSerial.load();
  require(rejected()&&!g_radialOverlay.active()&&count("radial-close")==2&&!g_radialOverlayCreating,"creation exception returns a partially acquired window");
  faultStage.clear();
  const auto receipt=radialCreateForPower(request,initialize);
  require(g_radialOverlay.active()&&receipt.value("nonce",std::string{})=="wheel","first new request after invalidated create can acquire the wheel");
 });
 run("sensor-new-publication-stops-old-suffix",[]{faultStage="sensors";edge="advance";const auto old=g_powerGeneration.load();start();require(msiAttempts==0&&emitted.empty(),"new publication during sensor callback retires old suffix");drain();currentReady();require(std::none_of(calls.begin(),calls.end(),[&](const auto& call){return call.name=="msi"&&call.generation==old;}),"old callback never relabels MSI work as successor generation");});
 run("cursor-new-publication-never-emits-old-ready",[]{faultStage="cursor";edge="advance";start();require(std::all_of(emitted.begin(),emitted.end(),[](const auto& value){return value.value("generation",0ull)==g_powerGeneration.load();}),"cursor callback cannot emit its previously captured older Ready payload");drain();currentReady();});
 run("same-publication-reentrancy-is-coalesced",[]{faultStage="sensors";edge="reentrant";start();drain();require(count("sensors")==1&&count("msi")==1&&count("cursor")==1,"nested same-publication delivery cannot duplicate accepted module callbacks");require(arms==1,"reentrant debt uses one original50ms timer");});
 run("exit-during-sensors-stops-projection",[]{faultStage="sensors";edge="exit";start();require(msiAttempts==0&&count("cursor")==0&&emitted.empty(),"exit prevents every remaining module and fact publication");require(!g_sleepPowerRedeliveryArmed,"exit does not create a recovery loop");});
 run("suspending-diagnostics-only",[]{g_powerLifecycle=PowerLifecycle::Suspending;g_hardwareWriteGate=false;start();require(count("sensors")==0&&msiAttempts==0&&count("cursor")==1&&emitted.back().at("phase")=="suspending","nonReady facts do not grant module admission");require(!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed,"stable nonReady diagnostics do not poll");});
 run("resuming-diagnostics-only",[]{g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;start();require(count("sensors")==0&&msiAttempts==0&&count("cursor")==1&&emitted.back().at("phase")=="resuming","projection cannot turn resume progress into Ready permission");});
 run("unknown-source-diagnostics-only",[]{g_powerGeneration=0;start();require(count("sensors")==0&&msiAttempts==0&&count("cursor")==1&&!emitted.back().value("known",true),"UNKNOWN source never opens private consumers");require(!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed,"stable UNKNOWN diagnostics do not poll");});
 run("new-publication-after-truncation-recovers",[]{faultStage="sensors";edge="phase";start();drain();require(msiAttempts==0&&emitted.back().at("phase")=="suspended","interruption publishes current suspended fact");g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;start();currentReady();require(!g_msiNativeForegroundPowerHold&&!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed,"later same-generation Ready releases scalar hold and original delivery resources");});
 run("queued-successor-current-facts",[]{faultStage="cursor";edge="advance";start();require(posts==2,"new publication retains its actual queued successor window message");deliverProjection();if(g_sleepPowerRedeliveryArmed.load())productionTimer();currentReady();require(std::all_of(emitted.begin(),emitted.end(),[](const auto& value){return value.value("generation",0ull)==g_powerGeneration.load();}),"even with successor window delivery, no older Ready payload may escape");require(!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed,"actual successor window delivery and timer both settle");});
 run("refused-post-and-timer-recover-on-next-publication",[]{
  failProjectionPost=failProjectionTimer=true;publishSharedPowerLifecycle("fixture-refused-delivery");
  require(posts==1&&arms==1&&!projectionQueue&&!g_sleepPowerRedeliveryArmed&&g_sharedPowerProjectionPending,"refused Post and timer leave unscheduled debt");
  failProjectionPost=failProjectionTimer=false;++g_powerGeneration;++g_sharedPowerSuspendSerial;g_resumeReadyGeneration=g_powerGeneration.load();
  publishSharedPowerLifecycle("fixture-next-current-ready");publishSharedPowerLifecycle("fixture-coalesced-ready");
  require(posts==2&&arms==1&&projectionQueue,"next publication must post exactly one accepted current receipt");
  if(!projectionQueue)return;
  deliverProjection();currentReady();
  require(count("sensors")==1&&count("input-host-rearm")==1&&count("cursor")==1&&count("ipc")==1&&!g_sharedPowerProjectionPending,"current consumers and frontend receive one projection");
  productionProjectionWindow(g_hwnd,0);
  require(count("sensors")==1&&count("input-host-rearm")==1&&count("ipc")==1&&!g_sleepPowerRedeliveryArmed,"late untagged receipt does not redispatch or arm a timer");
 });
 run("refused-projection-requeue-recovers-on-next-publication",[]{
  failProjectionTimer=true;faultStage="snapshot";edge="throw";start();
  require(arms==1&&!g_sleepPowerRedeliveryArmed&&g_sharedPowerProjectionPending&&!projectionQueue&&emitted.empty(),"read-only projection requeue keeps debt after timer refusal");
  failProjectionTimer=false;publishSharedPowerLifecycle("fixture-after-requeue-refusal");
  require(posts==2&&projectionQueue,"next publication must post after requeue timer refusal");
  if(!projectionQueue)return;
  deliverProjection();currentReady();
  require(count("ipc")==1&&!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed,"current facts settle once without a residual timer");
 });
 run("ready-commit-original-recovery-postgate",[]{
    const auto scenario=[&](const char* name,const std::function<void()>& body){reset();g_lastPowerResumeCommitTick=0;g_recoveryResumeCommit.record({});readyFixtureNow+=100000;readyCommitScenarios.push_back(name);body();};
    scenario("normal-commit-queues-original-worker-after-gate",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(g_powerGeneration.load(),"fixture-normal"),"actual native Ready commit succeeds");deliverProjection();require(readyWorkerAttempts==1&&readyWorkerUnknown==0&&readyWorkerAdmissions==1,"one actual post-gate projection automatically queues and admits original Wake without a second commit call");const auto signals=readySignals;const auto tick=g_lastPowerResumeCommitTick.load();for(unsigned i=0;i!=5;++i){publishSharedPowerLifecycle("diagnostic-only-publication");deliverProjection();}require(readySignals==signals&&readyWorkerAttempts==1&&g_lastPowerResumeCommitTick==tick,"same Native notification identity never repeats Ready consumer or resets its commit tick");});
    scenario("first-recovery-listener-failure-is-isolated",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;faultStage="sleep-recovery";edge="throw";require(commitPowerResume(g_powerGeneration.load(),"fixture-listener-failed"),"commit does not enter consumer early");deliverProjection();require(readyWorkerAdmissions==1&&readyWorkerUnknown==0&&diagnostics==std::vector<std::string>{"sleep-recovery"},"failed first recovery listener cannot strand the original Steam recovery queue");const auto attempts=readyWorkerAttempts.load();publishSharedPowerLifecycle("after-listener-error");deliverProjection();require(readyWorkerAttempts==attempts,"uncertain listener work and successful sibling are not replayed by diagnostics");});
    scenario("already-ready-second-call-while-gate-closed",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;readyReenter=true;require(commitPowerResume(g_powerGeneration.load(),"fixture-outer"),"outer commit continues after nested alreadyReady call");deliverProjection();require(!readyNestedResult&&readyWorkerUnknown==0&&readyWorkerAdmissions==1&&readySignals==1,"nested Ready-before-gate call cannot consume or publish early recovery");});
    scenario("nonready-during-precommit-cannot-be-overwritten",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;faultStage="close-episode";edge="phase";require(!commitPowerResume(g_powerGeneration.load(),"fixture-interrupted"),"new NonReady cancels the original commit");deliverProjection();require(g_powerLifecycle==PowerLifecycle::Suspended&&!g_hardwareWriteGate&&readyWorkerAttempts==0,"old commit never overwrites newer NonReady or wakes its consumer");});
    scenario("exit-during-precommit-cannot-publish",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;faultStage="close-episode";edge="exit";require(!commitPowerResume(g_powerGeneration.load(),"fixture-exit")&&readyWorkerAttempts==0&&!g_hardwareWriteGate,"exit before Ready suppresses notification and gate open");});
    scenario("pending-arrives-after-gate-before-projection",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(g_powerGeneration.load(),"fixture-before-pending"),"actual initial commit completed");g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](auto){return false;});deliverProjection();require(readyWorkerAttempts==0&&readySignals==0,"pending original ingress blocks Ready consumer before its queue");settle();publishSharedPowerLifecycle("original-query-cancel");deliverProjection();require(readyWorkerAdmissions==1&&readyWorkerUnknown==0,"existing post-cancel publication automatically releases the original Wake");});
    scenario("failed-window-post-original-timer-delivers",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;failPost=true;require(commitPowerResume(g_powerGeneration.load(),"fixture-post-failed"),"commit survives original message post failure");require(readyWorkerAttempts==0&&g_sleepPowerRedeliveryArmed,"failed post retains original delivery debt");failPost=false;drain();require(readyWorkerAttempts==1&&readyWorkerAdmissions==1&&readyWorkerUnknown==0,"original retry timer automatically delivers post-gate Ready without repeating commit");});
    scenario("already-ready-closed-gate-alone-is-not-ready",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=false;require(!commitPowerResume(g_powerGeneration.load(),"fixture-closed-ready")&&readySignals==0&&readyWorkerAttempts==0,"alreadyReady phase without the common hardware gate cannot issue recovery notification");});
    scenario("same-generation-query-cancel-keeps-admission-consumed",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(g_powerGeneration.load(),"fixture-once"),"first commit succeeds");deliverProjection();require(readyWorkerAdmissions==1,"first original Wake admission consumed");ResetEvent(readyWorkerHandled);g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](auto){return false;});publishSharedPowerLifecycle("query");deliverProjection();settle();publishSharedPowerLifecycle("cancel");deliverProjection();require(readyWorkerAttempts==2&&readyWorkerAdmissions==1&&readyWorkerUnknown==0,"new notification identity can recheck original Wake but never refunds its Admission");});
 });

 run("ready-classification-original-recovery",[]{
    const auto scenario=[&](const char* name,const std::function<void()>& body){reset();classificationRetry=classificationManual=classificationSuccess=false;classificationFacts=classificationHandlerCalls=classificationInteractive=0;readyFixtureNow+=100000;readyCommitScenarios.push_back(name);body();};
    scenario("retry-zero-action-cannot-claim-the-final-ready-delivery",[]{
        ReadyConsumerScope owner;
        const auto generation=g_powerGeneration.load();
        ymcc::sleeppower::nativeWakeFacts.ready(generation);
        const auto wakeRevision=ymcc::sleeppower::nativeWakeFacts.snapshot().revision;
        classificationRetry=true;
        start();
        require(readySignals==0&&readyWorkerAttempts==0,
            "classified Ready during sleep retry cannot claim either recovery listener");
        require(owner.c->deferredWake==generation,"sleep retry cannot consume the original pending Wake");
        const auto beforeRelease=readyWorkerAttempts.load();
        const auto autoBeforeRelease=readySignals;
        g_sgTask.generation=generation;
        g_sgTask.retryKind=SgRetryKind::SleepTimeout;
        sgStopSleepRetry(false);
        require(!classificationRetry&&g_sharedPowerProjectionPending,
            "original retry stop republishes classified Ready even without a new classification edge");
        if(g_sharedPowerProjectionPending)deliverProjection();
        require(ymcc::sleeppower::nativeWakeFacts.snapshot().revision==wakeRevision,
            "retry release uses the same Ready fact without minting a new wake");
        require(readySignals==autoBeforeRelease+1&&readyWorkerAttempts==beforeRelease+1&&
            readyWorkerAdmissions==1&&readyWorkerUnknown==0,
            "retry release delivers both listeners and admits the one original Wake");
        sgStopSleepRetry(false);
        require(!g_sharedPowerProjectionPending,"an inactive retry cannot republish Ready");
        start();
        require(readySignals==1&&readyWorkerAttempts==1&&readyWorkerAdmissions==1,
            "repeated Ready cannot replay the original Wake after retry release");
    });
    scenario("blocked-classification-before-ready-retains-original-wake",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();sgBlockSharedWakeClassification(generation);g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(generation,"fixture-classification-pending"),"Native Ready may finish before classification");deliverProjection();require(readySignals==0&&readyWorkerAttempts==0,"pending original classification cannot claim or invoke either recovery listener");sgCompleteSharedWakeClassification(generation);require(g_sharedPowerProjectionPending,"actual original classification completion publishes existing common delivery debt");deliverProjection();require(readySignals==1&&readyWorkerAdmissions==1&&readyWorkerUnknown==0,"classification completion automatically releases the original Wake through common projection");const auto revision=g_sharedPowerPublicationRevision.load();sgCompleteSharedWakeClassification(generation);require(g_sharedPowerPublicationRevision==revision&&!g_sharedPowerProjectionPending,"duplicate completion never publishes another readiness edge");publishSharedPowerLifecycle("diagnostic");deliverProjection();require(readyWorkerAttempts==1&&readySignals==1,"ordinary publication after classification never refunds or redelivers original work");});
    scenario("actual-finish-without-blocked-flag-notifies-both-listeners",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();g_sgResumeClassification.generation=generation;g_sgResumeClassification.interactiveReceiptTick=123;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(generation,"fixture-owned-classification"),"commit remains independent of classification state");deliverProjection();require(readyWorkerAttempts==0&&readySignals==0,"owned classification prevents premature listener claims");sgFinishResumeClassification("fixture-original-finish");require(classificationHandlerCalls==1&&classificationInteractive==1&&g_sharedPowerProjectionPending,"actual original Finish preserves its handler and publishes common completion");deliverProjection();require(readyWorkerAdmissions==1&&readySignals==1,"both independent listeners receive actual finish even without a blockedGeneration flag");});
    scenario("stale-active-and-exit-completions-do-not-publish",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();sgBlockSharedWakeClassification(generation);auto revision=g_sharedPowerPublicationRevision.load();sgCompleteSharedWakeClassification(generation-1);require(g_sgWakeClassificationBlockedGeneration==generation&&g_sharedPowerPublicationRevision==revision,"old generation cannot complete or notify current classification");g_sgResumeClassification.generation=generation;sgCompleteSharedWakeClassification(generation);require(g_sgWakeClassificationBlockedGeneration==generation&&g_sharedPowerPublicationRevision==revision,"still-owned classification cannot be completed by an unrelated callback");g_sgResumeClassification.generation=0;g_exitRequested=true;sgCompleteSharedWakeClassification(generation);require(g_sgWakeClassificationBlockedGeneration==generation&&g_sharedPowerPublicationRevision==revision&&readyWorkerAttempts==0,"exit cannot mint classification completion or enqueue recovery");});
    scenario("complete-before-ready-uses-original-commit-post",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();sgBlockSharedWakeClassification(generation);g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;sgCompleteSharedWakeClassification(generation);deliverProjection();require(readySignals==0&&readyWorkerAttempts==0,"classification completion alone cannot grant Native Ready");require(commitPowerResume(generation,"fixture-ready-after-classification"),"the original later Ready commit remains available");deliverProjection();require(readyWorkerAdmissions==1&&readySignals==1,"later original commit automatically joins the already completed classification");});
    scenario("completion-during-current-projection-uses-original-redelivery",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();sgBlockSharedWakeClassification(generation);g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(generation,"fixture-reentrant-classification"),"original commit succeeds");faultStage="sensors";edge="classification";deliverProjection();require(readyWorkerAttempts==0&&g_sharedPowerProjectionPending&&g_sleepPowerRedeliveryArmed,"new classification publication retires the old projection suffix");drain();require(readyWorkerAdmissions==1&&readySignals==1&&!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed,"existing delivery timer joins current classification without extra polling");});
    scenario("stop-observation-alone-does-not-mint-classification-ready",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();sgBlockSharedWakeClassification(generation);g_sgResumeClassification.generation=generation;const auto revision=g_sharedPowerPublicationRevision.load();sgStopResumeClassificationWait("fixture-cancel-observation");require(g_sgWakeClassificationBlockedGeneration==generation&&g_sharedPowerPublicationRevision==revision&&readySignals==0,"cancelling observation does not turn Unknown into a classified wake");});
    scenario("exit-during-finish-keeps-cancelled-consumer-retired",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();g_sgResumeClassification.generation=generation;g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;faultStage="classification-record";edge="exit";sgFinishResumeClassification("fixture-exit-in-stop");require(classificationHandlerCalls==0&&readyWorkerAttempts==0&&!g_sharedPowerProjectionPending,"actual Finish rechecks exit after original stop/diagnostic before common publication");});
 });

 run("ready-classification-startup-current-consumer",[]{
    const auto scenario=[&](const char* name,const std::function<void()>& body){reset();classificationRetry=classificationManual=classificationSuccess=false;classificationFacts=classificationHandlerCalls=classificationInteractive=0;readyFixtureNow+=100000;readyCommitScenarios.push_back(name);body();};
    scenario("late-classification-joins-original-pending-startup",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();{std::lock_guard<std::mutex> lock(owner.c->mx);owner.c->deferredWake=0;owner.c->enabled=true;owner.c->startupPending=true;}sgBlockSharedWakeClassification(generation);g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(generation,"fixture-pending-startup"),"original Ready commit may precede classification");deliverProjection();require(readyWorkerAttempts==0&&readySignals==0,"pending classification cannot manufacture a Startup or consume its delivery");sgCompleteSharedWakeClassification(generation);deliverProjection();require(readyWorkerAttempts==1&&readyWorkerAdmissions==1&&readyWorkerUnknown==0&&readyWorkerQueueKind==static_cast<int>(SteamOriginalRecovery::Trigger::TargetChanged)&&readyWorkerSelectedKind==static_cast<int>(SteamOriginalRecovery::Trigger::Startup),"actual current PowerReady automatically queues ordinary TargetChanged and original selector admits the pending Startup after classification");const auto attempts=readyWorkerAttempts.load();publishSharedPowerLifecycle("diagnostic-after-startup");deliverProjection();require(readyWorkerAttempts==attempts&&readyWorkerAdmissions==1,"pure diagnostic publication neither creates a new Startup request nor refunds its Admission");});
    scenario("valid-deferred-wake-keeps-priority-over-pending-startup",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();owner.c->enabled=true;owner.c->startupPending=true;sgBlockSharedWakeClassification(generation);g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(generation,"fixture-deferred-wake"),"original Ready commit succeeds with both pending intents");deliverProjection();sgCompleteSharedWakeClassification(generation);deliverProjection();require(readyWorkerAttempts==1&&readyWorkerAdmissions==1&&readyWorkerUnknown==0&&readyWorkerQueueKind==static_cast<int>(SteamOriginalRecovery::Trigger::Wake)&&readyWorkerSelectedKind==static_cast<int>(SteamOriginalRecovery::Trigger::Wake)&&!owner.c->startupPending.load(),"actual current callback keeps Wake priority and accepted Wake covers Startup for this pair");
     const ymcc::steamOriginal::Scope samePair{{1,1,11,L"parent"},{2,1,22,L"game"},{3,1,33,L"steam"},9};
     SteamOriginalRecovery::Pending ordinary{SteamOriginalRecovery::Trigger::TargetChanged,0,100};
     SteamOriginalRecovery::readyFixtureSelectPending(owner.c,ordinary,samePair,true,false);
     require(ordinary.kind==SteamOriginalRecovery::Trigger::TargetChanged&&!owner.c->startupPending.load(),"the same pair cannot manufacture Startup after an accepted Wake");
     auto newPair=samePair;++newPair.game.birth;SteamOriginalRecovery::Pending newGame{SteamOriginalRecovery::Trigger::TargetChanged,0,101};
     SteamOriginalRecovery::readyFixtureSelectPending(owner.c,newGame,newPair,true,false);
     require(newGame.kind==SteamOriginalRecovery::Trigger::Startup&&owner.c->startupPending.load(),"the exact current selector admits a new game pair even after the old pair cleared its Startup marker");
     require(owner.c->admission.preview(newPair,{SteamOriginalRecovery::Trigger::Startup,0,102},true).accepted,"selection preview itself does not consume the new pair Startup");});
 });


#ifdef YMCC_FIXTURE_FAN_STARTUP_WAKE
 run("fan-cold-after-raw-receipt-window-fallback",[]{
  using Receipt=ymcc::sleeppower::Dispatch::Receipt;
  struct RawClaimLog {
   std::vector<Receipt> claims;
   RawClaimLog(){rawReceiptClaims=&claims;}
   ~RawClaimLog(){rawReceiptClaims=nullptr;}
  };
  for(const bool replaced:{false,true}){
   reset();fixtureFanReset();fixtureFanActive=false;g_sleepPowerDispatch.closeReceiptWake();
   start();const auto prior=emitted.back();
   const auto base=g_sleepPowerDispatch.latestSleepBoundary().fileTime100ns;
   failProjectionTimer=true;RawClaimLog trace;
   Receipt oldSuspend,suspend,resume;
   const auto ingress=[](ULONG code,ULONGLONG when,bool posted,Receipt& receipt){
    return g_sleepPowerDispatch.callback(code,++serial,[&](auto value){receipt=value;return posted;},when);
   };
   require(!ingress(PBT_APMSUSPEND,base+50,false,replaced?oldSuspend:suspend),"original raw Suspend post fails");
   sleepPowerArmRedelivery(g_hwnd);
   require(rawReceiptQueue.size()==1,"original raw receipt is queued by the Native fallback");
   if(replaced)require(ingress(PBT_APMSUSPEND,base+60,true,suspend),"new Suspend arrives before the old fallback is consumed");
   require(ingress(PBT_APMRESUMESUSPEND,base+150,true,resume),"the new authentic interactive notification is registered");
   fixtureSendMessage(g_hwnd,kSleepPowerForwardMessage,resume.type,static_cast<LPARAM>(resume.sequence));
   require(trace.claims.empty()&&g_sleepPowerDispatch.pendingSuspend(),"an early Resume cannot consume the pending Suspend");
   const auto next=[&]{
    if(rawReceiptQueue.empty())return;
    const auto [type,sequence]=rawReceiptQueue.front();rawReceiptQueue.erase(rawReceiptQueue.begin());
    fixtureSendMessage(g_hwnd,kSleepPowerForwardMessage,type,sequence);
   };
   if(replaced){next();require(trace.claims.empty()&&g_sleepPowerDispatch.pendingSuspend(),"stale posted fallback cannot consume the old Suspend");}
   for(unsigned i=0;i<4&&trace.claims.empty()&&!rawReceiptQueue.empty();++i)next();
   require(trace.claims.size()==1&&trace.claims[0].sequence==suspend.sequence&&trace.claims[0].fileTime100ns==suspend.fileTime100ns,
       "accepted raw Suspend keeps its receipt sequence and original timestamp");
   if(trace.claims.size()!=1)continue;

   // Phase and ETW input are controlled here. The actual broadcast-claim
   // fixture separately extracts the WndProc admission; permission below
   // still comes from the original Ready gate and Fan projection.
   const auto shared=++g_powerGeneration;++g_sharedPowerSuspendSerial;
   g_powerLifecycle=PowerLifecycle::Suspended;g_hardwareWriteGate=false;
   const auto entry=base+100,exit=base+200;
   g_sgLatestSleepEntryFileTime=entry;fanHostObserveKernelEntry(shared,entry);
   publishSharedPowerLifecycle("ordered-power-delivery-complete");deliverProjection();
   const auto suspended=emitted.back();
   require(suspended.at("phase")=="suspended"&&!suspended.at("hardwareWritesAllowed").get<bool>()&&
       !suspended.value("fanStartupWakeGeneration",0ULL)&&!suspended.value("fanWakeGeneration",0ULL),"wire Suspend publishes no Fan or hardware permission");

   for(unsigned i=0;i<6&&!rawReceiptQueue.empty();++i)next();
   require(trace.claims.size()==2&&trace.claims[1].sequence==resume.sequence&&trace.claims[1].fileTime100ns==resume.fileTime100ns&&
       !g_sleepPowerDispatch.pendingSuspend()&&!g_sleepPowerDispatch.hasPending(),"the real Resume follows Suspend exactly once and clears notification debt");
   if(trace.claims.size()!=2)continue;
   g_powerLifecycle=PowerLifecycle::Resuming;g_resumeReadyGeneration=shared;
   publishSharedPowerLifecycle("ordered-power-delivery-complete");deliverProjection();const auto resuming=emitted.back();
   require(resuming.at("phase")=="resuming"&&!resuming.at("hardwareWritesAllowed").get<bool>(),"Resume receipt alone still exposes no hardware grant");
   require(commitPowerResume(shared,"fixture-raw-receipt-current-commit"),"same-scope Native commit opens the common gate");
   fixtureKernelNow=exit;fanHostObserveKernelExit(shared,exit,true);fixtureKernelNow=0;
   deliverProjection();const auto candidate=emitted.back();
   require(candidate.at("phase")=="ready"&&candidate.value("fanStartupWakeGeneration",0ULL)>shared&&
       !candidate.value("fanWakeGeneration",0ULL)&&fixtureFanStarts.empty(),"current S0 offer is read-only until claimed and Close returns");
   if(!candidate.value("fanStartupWakeGeneration",0ULL))continue;
   const json claim={{"generation",candidate.at("fanStartupWakeGeneration")},{"boundary",candidate.at("fanStartupWakeBoundary")},
       {"sharedGeneration",candidate.at("generation")},{"suspendSerial",candidate.at("suspendSerial")},{"notificationSequence",candidate.at("observedNotificationSequence")}};
   const auto accepted=fanClaimStartupWake(claim);deliverProjection();const auto inClose=emitted.back();
   require(accepted.value("ok",false)&&fixtureFanStartupCloseStarts==1&&!inClose.value("fanWakeGeneration",0ULL)&&fixtureFanStarts.empty(),"Close in flight grants no Fan permission");
   g_fanPhysicalWake.completeClose(currentFanPowerGeneration());
   publishSharedPowerLifecycle("fan-close-returned");deliverProjection();const auto permit=emitted.back();
   require(permit.value("fanWakeGeneration",0ULL)==currentFanPowerGeneration()&&fixtureFanStarts.size()==1&&rawReceiptQueue.empty(),
       "only the current Fan permit is published once after Close");
   const auto receiptJson=[](const Receipt& receipt){return json{{"sequence",receipt.sequence},{"type",receipt.type},
       {"receiptTickMs",receipt.tick},{"fileTime100ns",receipt.fileTime100ns}};};
   fixtureRawReceiptFanReplay[replaced?"replacementSuspend":"ordinary"]={{"delivery","kSleepPowerForwardMessage"},
       {"classificationInput","controlled-final-s0-507"},{"kernelEntryFileTime100ns",entry},{"kernelExitFileTime100ns",exit},
       {"oldSuspendReceipt",replaced?receiptJson(oldSuspend):json()},{"suspendReceipt",receiptJson(trace.claims[0])},
       {"resumeReceipt",receiptJson(trace.claims[1])},{"priorReady",prior},{"suspended",suspended},{"resuming",resuming},
       {"candidate",candidate},{"claim",claim},{"accepted",accepted},{"closeInFlight",inClose},{"finalPermit",permit}};
  }
 });
 run("fan-cold-507-with-coalesced-suspend",[]{
  ULONGLONG fixtureColdClockBase=0;
  const auto at=[&](ULONGLONG value){return fixtureColdClockBase+value;};
  enum class OfferRetry { None, Timer, Window, NewSuspend };
  const auto offer=[&](ULONGLONG offAt=0,bool late506=false,OfferRetry retry=OfferRetry::None,bool offBeforeRetry=false){
  reset();fixtureFanReset();fixtureFanActive=false;
  fixtureColdClockBase=g_sleepPowerDispatch.latestSleepBoundary().fileTime100ns;
  start();
  const auto before=emitted.back();
  const auto beforePosts=posts;
  g_powerLifecycle=PowerLifecycle::Suspending;
  g_hardwareWriteGate=false;
  ++g_sharedPowerSuspendSerial;
  const auto shared=++g_powerGeneration;
  // Retire any synthetic undated suspend inherited from another scenario.
  const auto ingress=g_sleepPowerDispatch.window(PBT_APMSUSPEND,++serial,at(50));
  g_sleepPowerDispatch.complete(ingress,++serial);
  if(offAt&&late506){fixtureKernelNow=at(offAt);fanCancelStartupWake({});}
  g_sgLatestSleepEntryFileTime=at(100);
  fanHostObserveKernelEntry(shared,at(100));
  if(offAt&&!late506){fixtureKernelNow=at(offAt);fanCancelStartupWake({});}
  publishSharedPowerLifecycle("fixture-merged-fan-suspend");
  g_powerLifecycle=PowerLifecycle::Resuming;
  g_resumeReadyGeneration=shared;
  require(commitPowerResume(shared,"fixture-merged-fan-ready"),"Native Ready commits before the posted projection runs");
  fixtureKernelNow=at(200);
  fanHostObserveKernelExit(shared,at(200),true);
  fixtureKernelNow=0;
  require(posts==beforePosts+1&&emitted.size()==1,"suspend, Ready, and the classified 507 share one queued projection");
  if(retry!=OfferRetry::None){sharedFactRefusals=1;failProjectionTimer=retry!=OfferRetry::Timer;}
  deliverProjection();
  if(retry!=OfferRetry::None){
   const auto retained=productionSharedPowerLifecycleSnapshot();
   const auto offered=retained.value("fanStartupWakeGeneration",0ULL);
   require(sharedFactMainPosts==2&&sharedFactRefusals==0&&emitted.size()==1&&retained.value("fanStartupWakeGeneration",0ULL)>shared,
       "one refused WebView post leaves the current Native cold offer available without starting Fan");
   require(fixtureFanStartupCloseStarts==0&&fixtureFanStarts.empty(),"refused read-only offer starts no Fan work");
   if(offBeforeRetry){fixtureKernelNow=at(250);fanCancelStartupWake({});fixtureKernelNow=0;}
   if(retry==OfferRetry::Timer){drain();}
   else {
    const auto revision=g_sharedPowerPublicationRevision.load();
    require(projectionQueue.has_value()&&!g_sleepPowerDispatch.receiptWakeHandle()&&!g_sleepPowerRedeliveryArmed,
        "cold offer has one Native window retry even without the coordinator event and timer");
    if(retry==OfferRetry::NewSuspend){
     const auto beforeNew=posts;
     ++g_sharedPowerSuspendSerial;++g_powerGeneration;g_hardwareWriteGate=false;g_powerLifecycle=PowerLifecycle::Suspended;
     const auto successor=g_sleepPowerDispatch.window(PBT_APMSUSPEND,++serial,at(300));
     g_sleepPowerDispatch.complete(successor,++serial);
     g_sgLatestSleepEntryFileTime=at(310);fanHostObserveKernelEntry(g_powerGeneration.load(),at(310));
     publishSharedPowerLifecycle("fixture-new-suspend-coalesces-with-fan-fallback");
     require(posts==beforeNew,"a newer publication coalesces with the queued retry");
    }
    if(projectionQueue)deliverProjection();
    require(g_sharedPowerPublicationRevision==revision+(retry==OfferRetry::NewSuspend?1:0)&&
        !projectionQueue&&!g_sharedPowerProjectionPending,"window retry uses the current revision and settles without extra messages");
    require(fixtureFanStartupCloseStarts==0&&fixtureFanStarts.empty(),"a resent offer still cannot start Fan work without a claim");
   }
   const bool retired=offBeforeRetry||retry==OfferRetry::NewSuspend;
   require(offered&&emitted.size()==2&&emitted.back().value("fanStartupWakeGeneration",0ULL)==(retired?0ULL:offered),
       "coordinator redelivers the current fact, never an offer retired by OFF or another suspend");
   if(emitted.size()!=2)return json::object();
   return emitted.back();
  }
  const auto after=emitted.back();
  require(emitted.size()==2&&after.at("phase")=="ready"&&after.at("generation")==shared&&
      after.at("suspendSerial").get<uint64_t>()==before.at("suspendSerial").get<uint64_t>()+1,
      "the renderer receives Ready directly across a public suspend boundary");
  require(!after.value("fanWakeGeneration",0ULL)&&fixtureFanStartupCloseStarts==0&&fixtureFanStarts.empty(),
      "the merged projection starts no Fan work and grants no Fan permission");
  return after;
  };
  const auto claim=[](const json& state){return json{{"generation",state.at("fanStartupWakeGeneration")},
      {"boundary",state.at("fanStartupWakeBoundary")},{"sharedGeneration",state.at("generation")},
      {"suspendSerial",state.at("suspendSerial")},{"notificationSequence",state.at("observedNotificationSequence")}};};
  (void)offer(0,false,OfferRetry::Timer);
  (void)offer(0,false,OfferRetry::Timer,true);
  const auto windowOff=offer(0,false,OfferRetry::Window,true);
  const auto windowNewSuspend=offer(0,false,OfferRetry::NewSuspend);
  const auto after=offer(90,true);
  fixtureMergedFanSnapshots={emitted.front(),after};
  require(after.value("fanStartupWakeGeneration",0ULL)>after.value("generation",0ULL)&&
      after.value("fanStartupWakeBoundary",std::string{})==std::to_string(at(100)),"pre-boundary OFF does not suppress the next cold offer; its original identity is replayable");
  const auto original=claim(after);
  auto stale=original;stale["boundary"]=std::to_string(at(99));
  require(!fanClaimStartupWake(stale).value("ok",true),"an old boundary with current projection ordinals cannot claim startup");
  require(fanClaimStartupWake(original).value("ok",false)&&!fanClaimStartupWake(original).value("ok",true),"current cold offer can be claimed exactly once");
  deliverProjection();
  require(fixtureFanStartupCloseStarts==1&&fixtureFanStarts.empty(),"accepted claim admits one Close and no Fan session before Close returns");
  fixtureKernelNow=at(250);fanCancelStartupWake({});fixtureKernelNow=0;
  g_fanPhysicalWake.completeClose(currentFanPowerGeneration());
  publishSharedPowerLifecycle("fixture-cancelled-close-returned");deliverProjection();
  require(fixtureFanStarts.empty()&&!emitted.back().value("fanWakeGeneration",0ULL),"OFF during Close prevents startup publication and work");

  const auto grant=offer(0,false,OfferRetry::Window);
  const auto prior=emitted.front();
  const auto request=grant.contains("fanStartupWakeBoundary")?claim(grant):json::object();
  const auto accepted=fanClaimStartupWake(request);
  require(accepted.value("ok",false),"a fresh cold offer can reach the Close return path");
  deliverProjection();
  const auto closeInFlight=emitted.back();
  g_fanPhysicalWake.completeClose(currentFanPowerGeneration());
  publishSharedPowerLifecycle("fixture-successful-close-returned");deliverProjection();
  const auto permit=emitted.back();
  require(fixtureFanStarts.size()==1&&permit.value("fanWakeGeneration",0ULL)==currentFanPowerGeneration(),
      "one final Fan permit and one session follow Close return");
  const auto writeBeforeOff=g_fanWritePowerRevision.load();
  fixtureKernelNow=at(250);fanCancelStartupWake({});fixtureKernelNow=0;
  const auto afterOff=productionSharedPowerLifecycleSnapshot();
  require(!afterOff.value("fanWakeGeneration",0ULL)&&
      g_fanWritePowerRevision.load()==writeBeforeOff+1&&!fixtureFanScopes.back()->current(),
      "OFF after the final permit withdraws the projection and cancels its captured write revision");
  fixtureColdFanReplay={{"priorReady",prior},{"candidate",grant},{"claim",request},{"accepted",accepted},
      {"closeInFlight",closeInFlight},{"finalPermit",permit},{"afterOffReadback",afterOff}};
  fixtureColdFanReplay["nativeWindowRetry"]={{"priorReady",prior},{"candidate",grant},{"claim",request},{"accepted",accepted},
      {"closeInFlight",closeInFlight},{"finalPermit",permit},{"afterOff",windowOff},{"newSuspend",windowNewSuspend}};

  for(bool late:{false,true}){
   const auto off=offer(150,late);
   fixtureMergedFanSnapshots.push_back(off);
   require(!off.value("fanStartupWakeGeneration",0ULL)&&off.value("fanStartupWakeBoundary",std::string{}).empty(),
       late?"OFF after original entry but before delayed 506 blocks replay":"OFF after observed 506 blocks replay");
  }
  const auto superseded=offer();
  ++g_sharedPowerSuspendSerial;
  auto rebound=claim(superseded);rebound["suspendSerial"]=g_sharedPowerSuspendSerial.load();
  require(!productionSharedPowerLifecycleSnapshot().value("fanStartupWakeGeneration",0ULL)&&
      !fanClaimStartupWake(rebound).value("ok",true),"a newer public suspend cannot rebind the older offer even with fresh ordinals");
  const auto beforeEntry=offer();
  g_sgLatestSleepEntryFileTime=at(150);
  require(!productionSharedPowerLifecycleSnapshot().value("fanStartupWakeGeneration",0ULL)&&
      !fanClaimStartupWake(claim(beforeEntry)).value("ok",true),"a new raw 506 withdraws the older offer before the Fan window consumes it");
  const auto beforePbt=offer();
  const auto receipt=g_sleepPowerDispatch.window(PBT_APMSUSPEND,++serial,at(250));
  g_sleepPowerDispatch.complete(receipt,++serial);
  auto pbtClaim=claim(beforePbt);pbtClaim["notificationSequence"]=g_sleepPowerDispatch.observedSequence();
  require(!productionSharedPowerLifecycleSnapshot().value("fanStartupWakeGeneration",0ULL)&&
      !fanClaimStartupWake(pbtClaim).value("ok",true),"new PBT suspend ingress invalidates the old offer even after pending clears");

  reset();fixtureFanReset();fixtureFanActive=false;start();
  const auto beforeCommitPrior=emitted.back();
  g_powerLifecycle=PowerLifecycle::Suspending;g_hardwareWriteGate=false;
  ++g_sharedPowerSuspendSerial;
  const auto shared=++g_powerGeneration;
  const auto entry=(std::max)(300ULL,g_sleepPowerDispatch.latestSleepBoundary().fileTime100ns+1);
  const auto wake=entry+100;
  g_sgLatestSleepEntryFileTime=entry;
  fanHostObserveKernelEntry(shared,entry);
  g_powerLifecycle=PowerLifecycle::Resuming;
  g_resumeReadyGeneration=shared;
  fixtureKernelNow=wake;fanHostObserveKernelExit(shared,wake,true);fixtureKernelNow=0;
  deliverProjection();
  const auto resumingCandidate=emitted.back();
  require(resumingCandidate.value("phase",std::string{})=="resuming"&&
      !resumingCandidate.value("hardwareWritesAllowed",true)&&resumingCandidate.value("fanStartupWakeGeneration",0ULL)>shared,
      "the current cold offer can precede public Ready");
  require(commitPowerResume(shared,"fixture-fan-ready-without-new-notification"),"same observation can commit public Ready");
  deliverProjection();
  const auto readyCandidate=emitted.back();
  require(readyCandidate.value("phase",std::string{})=="ready"&&readyCandidate.value("hardwareWritesAllowed",false)&&
      claim(resumingCandidate)==claim(readyCandidate)&&
      readyCandidate.at("publicationRevision").get<uint64_t>()>resumingCandidate.at("publicationRevision").get<uint64_t>(),
      "Ready republishes the same cold offer without advancing the notification sequence");
  require(fixtureFanStartupCloseStarts==0&&fixtureFanStarts.empty(),"publishing Ready alone starts no cold Fan work");
  const auto readyClaim=claim(readyCandidate);
  const auto readyAccepted=fanClaimStartupWake(readyClaim);
  require(readyAccepted.value("ok",false),"current offer remains claimable after public Ready");
  deliverProjection();
  g_fanPhysicalWake.completeClose(currentFanPowerGeneration());
  publishSharedPowerLifecycle("fixture-fan-public-ready-close-returned");deliverProjection();
  const auto readyPermit=emitted.back();
  require(fixtureFanStartupCloseStarts==1&&fixtureFanStarts.size()==1&&
      readyPermit.value("fanWakeGeneration",0ULL)==currentFanPowerGeneration(),"one Fan permit follows the original Close");
  fixtureColdFanReplay["commitWithoutNewNotification"]={{"priorReady",beforeCommitPrior},{"resumingCandidate",resumingCandidate},
      {"candidate",readyCandidate},{"claim",readyClaim},{"accepted",readyAccepted},{"finalPermit",readyPermit}};

   // C196: 506 and 507 occurred before the matching retry suspend was collected.
   // The public classification fixture covers that admission. Run its cold Fan handoff here.
   enum class LateRetry { Confirmed, NoProof, WrongEntry, NewSuspend, Off };
   for(const auto control:{LateRetry::Confirmed,LateRetry::NoProof,LateRetry::WrongEntry,LateRetry::NewSuspend,LateRetry::Off}){
    reset();fixtureFanReset();fixtureFanActive=false;start();
    const auto latePrior=emitted.back();
    const auto lateEntry=(std::max)(1000ULL,g_sleepPowerDispatch.latestSleepBoundary().fileTime100ns+1);
    const auto lateExit=lateEntry+100;
    const auto lateSuspend=g_sleepPowerDispatch.window(PBT_APMSUSPEND,++serial,lateExit+10);
    g_sleepPowerDispatch.complete(lateSuspend,++serial);
    ++g_sharedPowerSuspendSerial;
    const auto lateShared=++g_powerGeneration;
    g_sgLatestSleepEntryFileTime=lateEntry;
    fanHostObserveKernelEntry(lateShared,lateEntry);
    g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
    g_resumeReadyGeneration=lateShared;
    require(commitPowerResume(lateShared,"fixture-confirmed-late-retry-suspend"),"public wake from confirmed retry is committed");
    fixtureKernelNow=lateExit+20;
    if(control==LateRetry::NewSuspend){
     const auto replacement=g_sleepPowerDispatch.window(PBT_APMSUSPEND,++serial,lateSuspend.fileTime100ns);
     g_sleepPowerDispatch.complete(replacement,++serial);
    }
    if(control==LateRetry::Off)fanCancelStartupWake({});
    fanHostObserveKernelExit(lateShared,lateExit,true,
        control==LateRetry::NoProof?0:lateSuspend.sequence,
        control==LateRetry::WrongEntry?lateEntry-1:lateEntry);
    deliverProjection();
    const auto lateCandidate=productionSharedPowerLifecycleSnapshot();
    require(fixtureFanStartupCloseStarts==0&&fixtureFanStarts.empty(),"observing the late 507 starts no Fan work");
    if(control!=LateRetry::Confirmed){
     require(!lateCandidate.value("fanStartupWakeGeneration",0ULL),"missing or retired retry proof and explicit OFF grant no cold Fan offer");
     continue;
    }
    require(lateCandidate.value("fanStartupWakeBoundary",std::string{})==std::to_string(lateEntry),
        "a proven late retry suspend must not hide the current cold Fan offer");
    const auto lateClaim=claim(lateCandidate);
    const auto lateAccepted=fanClaimStartupWake(lateClaim);
    require(lateAccepted.value("ok",false),"cold Fan claims the original confirmed retry boundary");
    deliverProjection();
    const auto lateClose=emitted.back();
    g_fanPhysicalWake.completeClose(currentFanPowerGeneration());
    publishSharedPowerLifecycle("fixture-late-retry-fan-close-returned");deliverProjection();
    require(fixtureFanStartupCloseStarts==1&&fixtureFanStarts.size()==1&&
        emitted.back().value("fanWakeGeneration",0ULL)==currentFanPowerGeneration(),"one Fan permit follows the original late retry Close");
    fixtureColdFanReplay["lateRetry"]={{"priorReady",latePrior},{"candidate",lateCandidate},
        {"claim",lateClaim},{"accepted",lateAccepted},{"closeInFlight",lateClose},{"finalPermit",emitted.back()}};
   }
   fixtureKernelNow=0;
 });
 run("fan-cold-off-receipt-after-timeout",[]{
  const auto at=[base=g_sleepPowerDispatch.latestSleepBoundary().fileTime100ns](ULONGLONG value){return base+value;};
  const json id={{"offSequence",1ULL},{"offDocument",{{"generation","1"},{"navigationId","1"}}}};
  const auto state=[](const json& reply){return reply.value("state",std::string{});};
  const auto setup=[] {reset();fixtureFanReset();fixtureFanActive=false;fixtureKernelNow=0;
   g_webviewGeneration=1;g_webviewNavigationId=1;g_gyroTelemetryDocument=1;};
  const auto loadNewDocument=[] {++g_gyroTelemetryDocument;};
  const auto cycle=[&](ULONGLONG entry,ULONGLONG wake){
   entry=at(entry);wake=at(wake);
   g_sgLatestSleepEntryFileTime=entry;
   fanHostObserveKernelEntry(currentPowerGeneration(),entry);
   fixtureKernelNow=(std::max)(fixtureKernelNow,wake);
   fanHostObserveKernelExit(currentPowerGeneration(),wake,true);
   if(projectionQueue)deliverProjection();
   return productionSharedPowerLifecycleSnapshot();
  };
  const auto claim=[&](const json& s){auto body=id;body.update({
   {"generation",s.at("fanStartupWakeGeneration")},{"sharedGeneration",s.at("generation")},
   {"suspendSerial",s.at("suspendSerial")},{"notificationSequence",s.at("observedNotificationSequence")},
   {"boundary",s.at("fanStartupWakeBoundary")}});return body;};
  const auto withoutReceipt=[&](const json& s){auto body=claim(s);body.erase("offSequence");body.erase("offDocument");return body;};

  setup();fixtureKernelNow=at(90);
  const auto lostAck=fanCancelStartupWake(id);
  require(state(lostAck)=="applied","original OFF was applied even if the renderer received no ACK");
  const auto afterAckLost=cycle(100,200);
  const auto readApplied=fanStartupOffStatus(id);
  require(state(readApplied)=="applied","status returns the receipt for the original OFF after a newer boundary");
  const auto duplicateApplied=fanCancelStartupWake(id);
  require(state(duplicateApplied)=="applied"&&productionSharedPowerLifecycleSnapshot()==afterAckLost,
    "duplicate OFF does not cancel the newer candidate using a new clock stamp");
  require(fanClaimStartupWake(claim(afterAckLost)).value("ok",false),"the newer candidate can redeem the read-back receipt");

  setup();const auto current=cycle(100,180);
  require(!fanClaimStartupWake(claim(current)).value("ok",true),"an unverified OFF number cannot redeem a current candidate");
  const auto writeRevision=g_fanWritePowerRevision.load();
  fixtureKernelNow=at(200);
  const auto missing=fanStartupOffStatus(id);
  require(state(missing)=="waiting_for_new_boundary"&&g_fanWritePowerRevision==writeRevision&&fixtureFanStarts.empty(),
    "missing request is sealed with no Fan start or write revocation");
  fixtureKernelNow=at(63100);
  require(state(fanStartupOffStatus(id))=="waiting_for_new_boundary"&&!fanClaimStartupWake(claim(current)).value("ok",true),
    "elapsed wall time never admits the held boundary");
  const auto equal=cycle(200,300);
  require(state(fanStartupOffStatus(id))=="waiting_for_new_boundary"&&!fanClaimStartupWake(claim(equal)).value("ok",true),
    "entry equal to the first Native observation does not prove ordering");
  const auto next=cycle(301,400);
  const auto retired=fanStartupOffStatus(id);
  require(state(retired)=="retired","strictly newer original entry retires the missing request without moving the first fence");
  const auto late=fanCancelStartupWake(id);
  require(state(late)=="waiting_for_new_boundary"&&next==productionSharedPowerLifecycleSnapshot(),
    "the delayed original OFF does not acquire a new timestamp");
  auto wrongDoc=claim(next);wrongDoc["offDocument"]["navigationId"]="2";
  require(!fanClaimStartupWake(wrongDoc).value("ok",true),"another navigation cannot borrow this OFF receipt");
  require(fanClaimStartupWake(claim(next)).value("ok",false)&&fixtureFanStarts.empty(),
    "retry verifies the successor boundary at claim and only queues the consumer");

  setup();fixtureKernelNow=at(80);
  const auto firstOff=fanCancelStartupWake(id);
  auto newId=id;newId["offSequence"]=2ULL;
  fixtureKernelNow=at(90);
  const auto secondOff=fanCancelStartupWake(newId);
  const auto afterSecondOff=cycle(100,200);
  const auto superseded=fanStartupOffStatus(id);
  require(state(firstOff)=="applied"&&state(secondOff)=="applied"&&state(superseded)=="superseded" &&
      state(fanCancelStartupWake(id))=="superseded"&&!fanClaimStartupWake(claim(afterSecondOff)).value("ok",true),
    "a newer OFF number retires the old receipt for reads, delivery and claim");
  auto currentClaim=claim(afterSecondOff);currentClaim.update(newId);
  require(fanClaimStartupWake(currentClaim).value("ok",false)&&fixtureFanStarts.empty(),
    "only the current OFF number can redeem a later candidate, still without Fan work");

  setup();const auto old=cycle(100,180);
  fixtureKernelNow=at(200);fanStartupOffStatus(id);
  loadNewDocument();
  const auto reloaded=productionSharedPowerLifecycleSnapshot();
  require(!reloaded.value("fanStartupWakeGeneration",0ULL)&&reloaded.value("fanStartupWakeBoundary",std::string{}).empty(),
    "the sealed OFF hides the old offer on a fresh document read");
  require(!fanClaimStartupWake(withoutReceipt(old)).value("ok",true)&&!projectionQueue&&fixtureFanStarts.empty(),
    "a reloaded WebView without the OFF identity cannot claim the sealed boundary");
  require(g_fanPhysicalWake.physical(currentFanPowerGeneration(),at(100)),
    "cold OFF leaves the same physical evidence available for explicit manual control");
  const auto successor=cycle(201,300);
  require(fanClaimStartupWake(withoutReceipt(successor)).value("ok",false)&&fixtureFanStarts.empty(),
    "the new document can still claim a strictly later entry without resending the old OFF");

  setup();const auto closing=cycle(100,180);
  require(fanClaimStartupWake(withoutReceipt(closing)).value("ok",false),"the control round claims before OFF is sealed");
  deliverProjection();
  require(fixtureFanStartupCloseStarts==1&&fixtureFanStarts.empty(),"the existing claim is waiting for Close");
  fixtureKernelNow=at(200);fanStartupOffStatus(id);loadNewDocument();
  g_fanPhysicalWake.completeClose(currentFanPowerGeneration());
  publishSharedPowerLifecycle("fixture-reloaded-sealed-close-returned");deliverProjection();
  const auto afterSealedClose=emitted.back();
  require(!afterSealedClose.value("fanWakeGeneration",0ULL)&&fixtureFanStarts.empty(),
    "a sealed OFF prevents an already claimed wake from granting permission after Close and reload");

  setup();const auto beforePublished=cycle(100,180);
  const auto claimPublished=fanClaimStartupWake(withoutReceipt(beforePublished));
  deliverProjection();g_fanPhysicalWake.completeClose(currentFanPowerGeneration());
  publishSharedPowerLifecycle("fixture-published-before-seal");deliverProjection();
  require(claimPublished.value("ok",false)&&emitted.back().value("fanWakeGeneration",0ULL),
    "the control round has already issued a cold permit before the status query");
  const auto revisionBeforeSeal=g_fanWritePowerRevision.load();
  const auto startsBeforeSeal=fixtureFanStarts.size();
  fixtureKernelNow=at(200);fanStartupOffStatus(id);fanStartupOffStatus(id);
  loadNewDocument();
  const auto afterPublishedSeal=productionSharedPowerLifecycleSnapshot();
  require(g_fanWritePowerRevision==revisionBeforeSeal+1,"sealing withdraws an already published write scope exactly once");
  require(!afterPublishedSeal.value("fanWakeGeneration",0ULL)&&fixtureFanStarts.size()==startsBeforeSeal,
    "the withdrawn cold permit is absent from a reloaded readback and does not restart the Fan");
  setup();const auto beforeReload=cycle(100,180);
  require(fanClaimStartupWake(withoutReceipt(beforeReload)).value("ok",false),"the old cold offer has been claimed before reload");
  deliverProjection();g_fanPhysicalWake.completeClose(currentFanPowerGeneration());
  publishSharedPowerLifecycle("fixture-permit-before-reload");deliverProjection();
  const auto permitBeforeReload=productionSharedPowerLifecycleSnapshot();
  require(permitBeforeReload.value("fanWakeGeneration",0ULL),"the cold write permit was published before reload");
  const auto revisionBeforeReload=g_fanWritePowerRevision.load();
  const auto startsBeforeReload=fixtureFanStarts.size();
  loadNewDocument(); // Healthy reload advances the document without rearming the first-frame gate.
  fixtureKernelNow=at(200);
  const auto staleStatus=fanStartupOffStatus(id);
  require(staleStatus==json{{"ok",false}},"reload first rejects the queued old document status before sealing OFF");
  require(g_fanWritePowerRevision==revisionBeforeReload&&fixtureFanStarts.size()==startsBeforeReload&&
    productionSharedPowerLifecycleSnapshot()==permitBeforeReload,"old status cannot revoke the published permit or cause a Fan start");
  auto currentId=id;currentId["offDocument"]["navigationId"]="2";
  fixtureKernelNow=at(210);
  require(state(fanCancelStartupWake(currentId))=="applied","the new document can submit its own sequence starting at one");
  const auto newDocumentBoundary=cycle(211,300);
  auto staleHigh=id;staleHigh["offSequence"]=99ULL;
  fixtureKernelNow=at(320);
  require(fanStartupOffStatus(staleHigh)==json{{"ok",false}}&&fanCancelStartupWake(staleHigh)==json{{"ok",false}},
    "a higher old sequence cannot replace the new document receipt");
  require(state(fanStartupOffStatus(currentId))=="applied"&&productionSharedPowerLifecycleSnapshot()==newDocumentBoundary,
    "the new receipt and entry survive the stale status and cancel");
  auto newDocumentClaim=claim(newDocumentBoundary);newDocumentClaim.update(currentId);
  require(fanClaimStartupWake(newDocumentClaim).value("ok",false),"the new document can redeem its own receipt after the stale request");
  ++g_webviewGeneration;
  require(fanStartupOffStatus(currentId)==json{{"ok",false}},"controller rebuild rejects the previous generation even with a matching navigation number");
  g_webviewGeneration=1;g_gyroTelemetryDocument=1;
  fixtureColdFanOffReplay={{"off",id},{"appliedReadback",readApplied},{"appliedCandidate",afterAckLost},
      {"missingReadback",missing},{"blockedCandidate",current},{"retiredReadback",retired},{"retiredCandidate",next},
      {"lateCancel",late},{"supersededReadback",superseded},{"reloadedAfterSeal",reloaded},
      {"reloadSuccessor",successor},{"afterSealedClose",afterSealedClose},{"afterPublishedSeal",afterPublishedSeal},
      {"staleStatusAfterReload",staleStatus},{"newDocumentBoundary",newDocumentBoundary}};
 });
#endif
 run("fan-post-gate-resume-original-owner",[dropNormalInputReceipt]{
  const auto scenario=[&](const char* name,const auto& body){reset();fixtureFanReset();readyCommitScenarios.push_back(name);body();};
  const auto early=[&]{
   g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
   g_resumeReadyGeneration=g_powerGeneration.load();
   auto result=fanHostScheduleFanResume(currentFanPowerGeneration(),false,0);
   require(result.registered&&fixtureFanStarts.size()==1,"original pre-gate F5 request is registered once");
  };
  const auto recoverInterruptedProjection=[&](bool afterSensors,bool publishCompletion){
   early();const auto old=fixtureFanStarts.back();const auto before=g_fanResumeRequestOwner.snapshot(old).value();
   require(commitPowerResume(g_powerGeneration.load(),"fan-reader-ingress"),"actual commit opens Native admission before the interrupted read");
   const auto publication=g_sharedPowerPublicationRevision.load();
   publishIngressCompletion=publishCompletion;
   if(afterSensors){faultStage="sensors";edge="reader-ingress";}else readerIngress=6;
   deliverProjection();
   require(readerIngress==0&&fixtureFanStarts.size()==1&&g_fanResumeRequestOwner.pending()==0,"moving notification authority refuses old Fan admission");
   require(g_sharedPowerProjectionPending&&g_sleepPowerRedeliveryArmed&&arms==1&&emitted.empty(),"missed Native Ready cannot become a completed frontend Ready publication");
   if(g_sleepPowerRedeliveryArmed.load())productionTimer();
   require(g_sharedPowerPublicationRevision==publication+(publishCompletion?1u:0u)&&count("sensors")==1&&msiAttempts==1&&count("ipc")==1,"original timer completes the current publication without duplicate sensor callbacks");
   currentReady();
   require(!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed&&g_fanResumeRequestOwner.pending()==currentFanPowerGeneration(),"timer settles once and continues the original Fan owner");
   fixtureFanReturnOld();
   require(fixtureFanStarts.size()==2,"returning old IO starts one continuation without a manual click");
   if(fixtureFanStarts.size()==2){const auto next=g_fanResumeRequestOwner.snapshot(fixtureFanStarts.back()).value();
    require(next.ticket.request==old.request&&next.deadlineTick==before.deadlineTick&&fixtureFanScopes.back()->current(),"continuation uses current admission and keeps the original request and deadline");}
  };
  scenario("unstable-first-reader-preserves-public-ready",[&]{recoverInterruptedProjection(false,false);});
  scenario("interleaved-ingress-preserves-unattempted-ready-suffix",[&]{recoverInterruptedProjection(true,false);});
  scenario("ordered-completion-supersedes-interrupted-projection",[&]{recoverInterruptedProjection(false,true);});
  scenario("refused-ready-delivery-continues-original-fan-owner",[&]{
   early();if(fixtureFanStarts.empty())return;
   const auto old=fixtureFanStarts.back();const auto before=g_fanResumeRequestOwner.snapshot(old).value();
   failProjectionPost=failProjectionTimer=true;
   require(commitPowerResume(g_powerGeneration.load(),"fan-refused-delivery"),"actual Ready commit still opens the public gate");
   require(!projectionQueue&&!g_sleepPowerRedeliveryArmed&&g_sharedPowerProjectionPending&&g_fanResumeRequestOwner.pending()==0,"rejected endpoints have not admitted the original Fan successor");
   failProjectionPost=failProjectionTimer=false;publishSharedPowerLifecycle("fan-next-valid-publication");
   require(projectionQueue.has_value(),"next valid Ready publication posts after both endpoints rejected");
   if(!projectionQueue)return;
   deliverProjection();require(fixtureFanStarts.size()==1&&g_fanResumeRequestOwner.pending()==currentFanPowerGeneration(),"current projection retains a successor without replacing live old IO");
   fixtureFanReturnOld();require(fixtureFanStarts.size()==2,"old IO return starts the successor without a manual click");
   if(fixtureFanStarts.size()==2){const auto next=g_fanResumeRequestOwner.snapshot(fixtureFanStarts.back()).value();
    require(next.ticket.request==old.request&&next.deadlineTick==before.deadlineTick&&fixtureFanScopes.back()->current(),"resumed Fan uses current admission with the same request and deadline");}
  });
  const auto recoverParkedProjection=[&](bool newPublication){
   early();require(commitPowerResume(g_powerGeneration.load(),"fan-persistent-reader"),"original commit publishes Ready");
   const auto publication=g_sharedPowerPublicationRevision.load();const auto startedPosts=posts;
   readerIngress=256;deliverProjection();
   for(unsigned i=0;i<4&&g_sleepPowerRedeliveryArmed.load();++i)productionTimer();
   require(arms==3&&!g_sleepPowerRedeliveryArmed&&readerIngress>0,"persistent moving Reader parks after the original three shared timer reservations");
   require(emitted.empty()&&fixtureFanStarts.size()==1&&g_fanResumeRequestOwner.pending()==0,"parked projection cannot publish Ready or authorize Fan");
   readerIngress=0;
   if(newPublication){publishSharedPowerLifecycle("fixture-real-publication-after-parking");
    require(posts==startedPosts+1,"new publication after the original message is consumed must post a new window delivery");
    if(posts==startedPosts+1)deliverProjection();
   }else deliverProjection();
   currentReady();
   require(g_sharedPowerPublicationRevision==publication+(newPublication?1u:0u)&&!g_sharedPowerProjectionPending&&g_fanResumeRequestOwner.pending()==currentFanPowerGeneration(),"a real window delivery settles parked work without minting another generation");
   fixtureFanReturnOld();require(fixtureFanStarts.size()==2,"parked Fan recovers through the original request owner");
  };
  scenario("persistent-moving-reader-parks-current-ready",[&]{recoverParkedProjection(false);});
  scenario("new-publication-wakes-parked-projection",[&]{recoverParkedProjection(true);});
  scenario("exit-retires-interrupted-projection",[&]{
   early();require(commitPowerResume(g_powerGeneration.load(),"fan-exit-debt"),"original commit publishes Ready");
   readerIngress=6;deliverProjection();
   require(g_sharedPowerProjectionPending&&g_sleepPowerRedeliveryArmed,"interrupted Ready has outstanding projection delivery");
   g_exitRequested=true;if(g_sleepPowerRedeliveryArmed.load())productionTimer();
   require(!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed&&fixtureFanStarts.size()==1&&emitted.empty(),"exit retires projection debt without admitting Fan or publishing Ready");
   require(inputReadyReceipts.empty(),"exit cannot deliver the interrupted Ready to the InputHost port");
  });
  scenario("normal-commit-automatically-continues-same-owner",[&]{
   early();if(fixtureFanStarts.size()!=1)return;
   const auto old=fixtureFanStarts.back();const auto before=g_fanResumeRequestOwner.snapshot(old).value();
   const auto post=g_fanResumeRequestOwner.reservePost(old,readyFixtureNow);require(bool(post),"original request reserves its first cumulative slot");
   require(g_fanResumeRequestOwner.completePost(post,false),"original authenticated acceptance consumes its POST slot");
   const auto key=g_fanResumeRequestOwner.requestKey(old).value();
   require(g_fanResumeRequestOwner.confirmAwaitingGrant(old,key.first,key.second),"original accepted business retains awaiting-grant evidence");
   readyFixtureNow+=50;
   require(commitPowerResume(g_powerGeneration.load(),"fan-normal"),"original commit opens Native hardware admission");
   dropInputReadyReceipt=dropNormalInputReceipt;
   deliverProjection();
   dropInputReadyReceipt=false;
   const auto inputAtCommit=inputReadyReceipts.size();
   require(fixtureFanStarts.size()==1,"post-gate listener cannot replace still-borrowed old IO");
   require(!fixtureFanScopes.front()->current(),"old Resuming registration cannot authorize Ready work");
   require(g_fanResumeRequestOwner.pending()==currentFanPowerGeneration(),"current Ready retains a successor on the original owner");
   fixtureFanReturnOld();
   require(fixtureFanStarts.size()==2,"returning old IO automatically starts retained work without another click");
   if(fixtureFanStarts.size()!=2)return;
   const auto now=fixtureFanStarts.back();const auto after=g_fanResumeRequestOwner.snapshot(now).value();
   require(now.request==old.request&&now.run!=old.run&&before.startedTick==after.startedTick&&before.deadlineTick==after.deadlineTick,"continuation preserves original request and first deadline");
   require(after.attempts==1&&after.posts==1&&fixtureFanScopes.back()->current(),"fresh Native registration keeps already consumed slot");
   const auto tuple=[](const ymcc::fan_resume::PowerStamp& power){return json{
    {"generation",power.generation},{"suspendSerial",power.suspendSerial},{"notificationSequence",power.notification},
    {"publicationRevision",power.publication},{"phase",power.phase},{"flags",power.flags}};};
   const auto fanReady=tuple(after.power);
   json inputReady;
   if(inputAtCommit==1){
    const auto& power=inputReadyReceipts.front();
    inputReady={{"generation",power.generation},{"suspendSerial",power.suspendSerial},{"notificationSequence",power.observedNotificationSequence},
     {"publicationRevision",power.publicationRevision},{"phase",power.phase},{"flags",power.flags}};
   }
   require(inputAtCommit==1&&ymcc::sleeppower::powerAdmitted(inputReadyReceipts.front())&&inputReady==fanReady,
    "one InputHost receipt must match the original Fan owner's exact public Ready tuple");
   jointReadyObservation={{"inputReady",inputReady},{"fanReady",fanReady},{"fanWakeGeneration",now.generation},
    {"previousFanPower",tuple(before.power)},{"originalDeadline",before.deadlineTick},{"continuedDeadline",after.deadlineTick},
    {"priorPosts",after.posts},{"inputReceiptsAtCommit",inputAtCommit}};
   const auto continuation=g_fanResumeRequestOwner.reserveContinuation(now,readyFixtureNow);
   require(bool(continuation)&&continuation.slot==2,"new grant spends the same cumulative second slot");
   g_fanResumeRequestOwner.finishRequest(now,readyFixtureNow,true);
   const auto starts=fixtureFanStarts.size();for(unsigned i=0;i<5;++i){publishSharedPowerLifecycle("fan-diagnostic");deliverProjection();}
   require(fixtureFanStarts.size()==starts,"publication-only changes cannot reopen completed Fan business");
  });
  scenario("already-ready-publication-continues-original-request",[&]{
   fanHostScheduleFanResume(currentFanPowerGeneration(),false,0);const auto old=fixtureFanStarts.back();const auto before=g_fanResumeRequestOwner.snapshot(old).value();
   settle();readyFixtureNow+=25;require(commitPowerResume(g_powerGeneration.load(),"fan-already-ready"),"actual already_ready publishes qualified current Native facts");
   deliverProjection();fixtureFanReturnOld();
   require(fixtureFanStarts.size()==2,"already_ready automatically joins same Fan owner without second manualWake");
   const auto next=g_fanResumeRequestOwner.snapshot(fixtureFanStarts.back()).value();
   require(next.ticket.request==old.request&&next.startedTick==before.startedTick&&next.deadlineTick==before.deadlineTick,"already_ready does not refund original budget");
  });
  scenario("pending-notification-cannot-admit-fan-work",[&]{
   early();const auto receipt=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPEND,++serial);
   g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;publishSharedPowerLifecycle("fan-pending");deliverProjection();
   require(fixtureFanStarts.size()==1&&g_fanResumeRequestOwner.pending()==0,"unclaimed Native notification cannot continue Fan work");
   require(inputReadyReceipts.empty(),"pending notification cannot send admitted Ready to the InputHost port");
   // A lease rollback can retire Fan's own write admission at the same edge.
   ++g_fanWritePowerRevision;
   g_sleepPowerDispatch.complete(receipt,++serial);publishSharedPowerLifecycle("fan-pending-completed");deliverProjection();fixtureFanReturnOld();
   require(fixtureFanStarts.size()==2,"existing publication after receipt completion resumes the original work");
   const auto continued=g_fanResumeRequestOwner.snapshot(fixtureFanStarts.back());
   require(continued&&continued->writeRevision==g_fanWritePowerRevision&&fixtureFanScopes.back()->current(),"continuation carries both current Native admission and current Fan write revision");
  });
  scenario("off-does-not-resurrect-control",[&]{
   early();fixtureFanActive=false;require(commitPowerResume(g_powerGeneration.load(),"fan-off"),"Native may recover independently of Fan OFF");deliverProjection();fixtureFanReturnOld();
   require(fixtureFanStarts.size()==1,"OFF prevents automatic Fan continuation");
   require(inputReadyReceipts.size()==1&&ymcc::sleeppower::powerAdmitted(inputReadyReceipts.front()),"Fan OFF does not suppress the independent InputHost Ready port");
  });
  const auto privateSleep=[&]{
   const auto shared=currentPowerGeneration();
   const auto bound=currentFanPowerGeneration();
   const auto physical=g_fanPhysicalWake.recordSleep(shared,100);
   require(physical>bound&&g_fanPhysicalWake.beginClose(physical),"next 506 creates a Fan cycle distinct from the public S4 wake");
   require(g_fanPhysicalWake.completeClose(physical)==0,"original Close worker has returned");
   return physical;
  };
  const auto continueFirstManual=[&](bool returnBeforeReady,bool s4,bool off,bool expired){
   fixtureFanActive=false;
   const auto shared=currentPowerGeneration();
   const auto generation=s4?privateSleep():currentFanPowerGeneration();
   if(s4){
    require(g_fanPhysicalWake.recordSharedHibernateWake(shared,200),"authoritative S4 wake closes the private Fan sleep boundary");
    require(g_fanPhysicalWake.sharedResume(shared)==generation,"same S4 establishes the Fan wake");
   }
   g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;g_resumeReadyGeneration=shared;
   const auto manual=fanHostScheduleFanResume(generation,true,0);
   require(manual.registered&&manual.manualOperation&&fixtureFanStarts.size()==1,"first click registers a session-only request before shared Ready");
   if(fixtureFanStarts.size()!=1)return;
   const auto old=fixtureFanStarts.front();const auto before=g_fanResumeRequestOwner.snapshot(old).value();
   const auto post=g_fanResumeRequestOwner.reservePost(old,readyFixtureNow);
   require(before.sessionOnly&&bool(post)&&g_fanResumeRequestOwner.completePost(post,false),"original manual acceptance already spent its first slot");
   if(returnBeforeReady)fixtureFanReturnOld();
   if(off)++g_fanWritePowerRevision; // synchronous Native revocation, before the stop reply
   readyFixtureNow=expired?before.deadlineTick:readyFixtureNow+50;
   require(commitPowerResume(shared,"fan-first-manual"),"original resume commits independently of Fan");
   deliverProjection();
   if(!returnBeforeReady){
    require(fixtureFanStarts.size()==1,"public Ready cannot overlap the previous owner");
    fixtureFanReturnOld();
   }
   if(off||expired){
    require(fixtureFanStarts.size()==1&&g_fanResumeRequestOwner.pending()==0,"revoked or expired manual work cannot be revived");
    return;
   }
   require(fixtureFanStarts.size()==2,"public Ready continues a registered first click without an active curve or another click");
   if(fixtureFanStarts.size()!=2)return;
   const auto next=fixtureFanStarts.back();const auto after=g_fanResumeRequestOwner.snapshot(next);
   require(after&&next.request==old.request&&next.run!=old.run&&after->sessionOnly&&after->manualOperation==manual.manualOperation&&
       after->deadlineTick==before.deadlineTick&&after->startedTick==before.startedTick&&after->attempts==1&&after->posts==1&&
       fixtureFanScopes.back()->current()&&g_fanResumeManualOperation.load()==manual.manualOperation,"handoff reuses the original click, deadline, and spent attempt under current power");
   publishSharedPowerLifecycle("fan-first-manual-repeat");deliverProjection();
   require(fixtureFanStarts.size()==2,"duplicate current Ready cannot start another manual recovery");
  };
  scenario("first-manual-ready-before-return",[&]{continueFirstManual(false,false,false,false);});
  scenario("first-manual-ready-after-return",[&]{continueFirstManual(true,false,false,false);});
  scenario("first-manual-continues-on-shared-s4-ready",[&]{continueFirstManual(false,true,false,false);});
  scenario("first-manual-stop-during-power-handoff",[&]{continueFirstManual(false,false,true,false);});
  scenario("first-manual-original-deadline-still-applies",[&]{continueFirstManual(true,false,false,true);});
  const auto recoverInterruptedFanAdmission=[&](bool physicalWake){
   auto generation=currentFanPowerGeneration();
   if(physicalWake){
    generation=privateSleep();
    require(g_fanPhysicalWake.recordWake(currentPowerGeneration(),200,false),"the newer physical cycle has a genuine matching wake");
   }
   fixtureFanIngress=true;
   start();
   require(!fixtureFanIngress&&readerIngress==0,"ingress crosses the Fan leaf admission read after the public precheck");
   require(fixtureFanStarts.empty()&&fixtureFanReadySignals==0&&emitted.empty()&&g_sleepPowerRedeliveryArmed,"a Fan leaf that registered no work leaves the public Ready delivery pending");
   if(g_sleepPowerRedeliveryArmed)productionTimer();
   require(fixtureFanStarts.size()==1&&fixtureFanStarts.back().generation==generation&&fixtureFanReadySignals==1,"the same publication starts one original Fan request under a fresh observation and sends one Fan Ready");
   require(!g_sleepPowerRedeliveryArmed&&!g_sharedPowerProjectionPending&&msiAttempts==1&&count("sensors")==1,"completion does not repeat earlier consumers or leave a live timer");
   currentReady();
   publishSharedPowerLifecycle("fan-admission-metadata");deliverProjection();
   require(fixtureFanStarts.size()<=1,"duplicate Ready does not start a second Fan request");
  };
  scenario("shared-wake-zero-admission-does-not-claim-delivery",[&]{recoverInterruptedFanAdmission(false);});
  scenario("paired-physical-wake-zero-admission-does-not-claim-delivery",[&]{recoverInterruptedFanAdmission(true);});
  scenario("s4-then-physical-wake-waits-for-public-admission",[&]{
   const auto physical=privateSleep();const auto receipt=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPEND,++serial);
   require(g_fanPhysicalWake.recordWake(currentPowerGeneration(),200,false),"matching 507 is retained despite pending public notification");
   fanHostPublishPhysicalWake(physical);publishSharedPowerLifecycle("fan-physical-pending");deliverProjection();
   require(fixtureFanStarts.empty(),"neither Fan fact nor public projection admit work while notification is pending");
   g_sleepPowerDispatch.complete(receipt,++serial);publishSharedPowerLifecycle("fan-physical-admitted");deliverProjection();
   require(fixtureFanStarts.size()==1&&fixtureFanStarts.back().generation==physical,"public Ready delivers the retained newer physical Fan wake without a second 507 or click");
   publishSharedPowerLifecycle("fan-physical-metadata");deliverProjection();
   require(fixtureFanStarts.size()<=1,"metadata publication does not duplicate the physical wake request");
  });
  scenario("physical-wake-close-fallback-while-publicly-suspended",[&]{
   g_powerLifecycle=PowerLifecycle::Suspended;g_hardwareWriteGate=false;
   const auto shared=currentPowerGeneration();
   const auto physical=g_fanPhysicalWake.recordSleep(shared,100);
   require(physical>shared&&g_fanPhysicalWake.beginClose(physical),"the Fan cycle has an outstanding Close");
   require(g_fanPhysicalWake.recordWake(shared,200,false),"matching 507 arrives before Close returns");
   fanHostPublishPhysicalWake(physical);
   require(fixtureFanStarts.empty(),"physical wake cannot overtake Close");
   require(g_fanPhysicalWake.completeClose(physical)==0,"the original Close returns");
   const auto receipt=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPEND,++serial);
   publishSharedPowerLifecycle("fan-close-wake-post-failed");deliverProjection();
   require(fixtureFanStarts.empty()&&fixtureFanReadySignals==0,"Close fallback respects a pending public notification");
   g_sleepPowerDispatch.complete(receipt,++serial);
   publishSharedPowerLifecycle("fan-close-wake-notification-completed");deliverProjection();
   require(fixtureFanStarts.size()==1&&fixtureFanStarts.back().generation==physical&&fixtureFanScopes.back()->current()&&
       fixtureFanReadySignals==1,"the public fallback delivers the retained Fan wake without a second 507, global Ready or click");
   publishSharedPowerLifecycle("fan-close-wake-duplicate");deliverProjection();
   require(fixtureFanStarts.size()<=1&&fixtureFanReadySignals<=1,"duplicate projection does not start a second Fan wake");
   require(currentPowerGeneration()==shared&&g_powerLifecycle.load()==PowerLifecycle::Suspended&&!g_hardwareWriteGate.load()&&
       count("sensors")==0&&msiAttempts==0,"Fan-only fallback leaves shared admission and other Ready consumers parked");
  });
#ifdef YMCC_FIXTURE_FAN_STARTUP_WAKE
  scenario("startup-fan-wake-close-before-session-only-offer",[&]{
   fixtureFanActive=false;g_powerLifecycle=PowerLifecycle::Suspended;g_hardwareWriteGate=false;
   const auto shared=currentPowerGeneration(),entry=100ULL;
   g_sgLatestSleepEntryFileTime=entry;
   const auto physical=g_fanPhysicalWake.recordSleep(shared,entry);
   require(physical>shared&&g_fanPhysicalWake.recordWake(shared,200,false,readyFixtureNow+1000,
       g_sharedPowerSuspendSerial.load(),g_sleepPowerDispatch.observedSleepBoundarySequence()),"startup wake pairs the current private sleep boundary");
   const auto offer=productionSharedPowerLifecycleSnapshot();
   require(offer.value("fanStartupWakeGeneration",0ULL)==physical&&offer.value("fanWakeGeneration",0ULL)==0,"startup offer is not a ready Fan session");
   json claim={{"generation",physical},{"sharedGeneration",shared},{"suspendSerial",offer.at("suspendSerial")},{"notificationSequence",offer.at("observedNotificationSequence")}};
   auto stale=claim;stale["notificationSequence"]=claim.at("notificationSequence").get<unsigned long long>()+1;
   require(!fanClaimStartupWake(stale).value("ok",true)&&!projectionQueue&&fixtureFanStartupCloseStarts==0,"a different notification sequence cannot claim startup or post new work");
   require(fanClaimStartupWake(claim).value("ok",false)&&projectionQueue.has_value(),"the original claim accepts the current offer and posts the public projection");
   deliverProjection();
   require(fixtureFanStartupCloseStarts==1&&fixtureFanStarts.empty()&&g_fanPhysicalWake.needsClose(physical),"startup waits for the single current Close before offering Host work");
   g_fanPhysicalWake.completeClose(physical);
   publishSharedPowerLifecycle("fixture-startup-close-returned");deliverProjection();
   const auto ready=productionSharedPowerLifecycleSnapshot();
   require(fixtureFanStarts.size()==1&&fixtureFanStarts.front().generation==physical&&ready.value("fanWakeGeneration",0ULL)==physical,"completed Close admits the current startup Fan session once");
   require(!fixtureFanStarts.empty()&&g_fanResumeRequestOwner.snapshot(fixtureFanStarts.front()).value().sessionOnly,"startup does not restore a previously confirmed curve");
   require(!fanClaimStartupWake(claim).value("ok",true)&&fixtureFanStartupCloseStarts==1,"the consumed offer cannot admit another Close");
   require(currentPowerGeneration()==shared&&g_powerLifecycle.load()==PowerLifecycle::Suspended&&!g_hardwareWriteGate.load()&&count("sensors")==0&&msiAttempts==0,"Fan startup leaves public Ready and other consumers parked");
  });
#endif
  scenario("s4-then-physical-wake-rebinds-already-published-owner",[&]{
   const auto physical=privateSleep();
   require(g_fanPhysicalWake.recordWake(currentPowerGeneration(),200,false),"matching 507 initially admits the physical wake");
   fanHostPublishPhysicalWake(physical);
   require(fixtureFanStarts.size()==1,"one physical Fan owner starts before public revocation");
   const auto old=fixtureFanStarts.back();const auto before=g_fanResumeRequestOwner.snapshot(old).value();
   const auto receipt=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPEND,++serial);
   publishSharedPowerLifecycle("fan-physical-revoked");deliverProjection();
   require(!fixtureFanScopes.front()->current()&&g_fanResumeRequestOwner.pending()==0,"pending notification revokes old scope and cannot grant a successor");
   readyFixtureNow+=50;
   g_sleepPowerDispatch.complete(receipt,++serial);publishSharedPowerLifecycle("fan-physical-regranted");deliverProjection();
   require(fixtureFanStarts.size()==1,"new authority waits for original worker to return");
   fixtureFanReturnOld();
   require(fixtureFanStarts.size()==2,"new public Ready rebinds already published physical wake work");
   const auto after=g_fanResumeRequestOwner.snapshot(fixtureFanStarts.back());
   require(after&&after->ticket.request==old.request&&after->ticket.generation==physical&&after->deadlineTick==before.deadlineTick&&fixtureFanScopes.back()->current(),"rebinding keeps the original request and deadline under current authority");
  });
  scenario("physical-wake-rebinds-while-publicly-suspended",[&]{
   g_powerLifecycle=PowerLifecycle::Suspended;g_hardwareWriteGate=false;
   const auto physical=privateSleep();
   require(g_fanPhysicalWake.recordWake(currentPowerGeneration(),200,false),"matching 507 admits only the Fan owner");
   fanHostPublishPhysicalWake(physical);
   require(fixtureFanStarts.size()==1,"the original physical wake starts before revocation");
   if(fixtureFanStarts.size()!=1)return;
   const auto old=fixtureFanStarts.front();const auto before=g_fanResumeRequestOwner.snapshot(old).value();
   const auto receipt=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPEND,++serial);
   start();
   require(!fixtureFanScopes.front()->current()&&g_fanResumeRequestOwner.pending()==0,"pending notification revokes the physical-wake scope");
   g_sleepPowerDispatch.complete(receipt,++serial);
   start();
   require(fixtureFanStarts.size()==1,"no replacement while original IO is in flight");
   fixtureFanReturnOld();
   require(fixtureFanStarts.size()==2,"clearing the notification continues the original physical-wake request while shared power remains parked");
   if(fixtureFanStarts.size()==2){
    const auto after=g_fanResumeRequestOwner.snapshot(fixtureFanStarts.back());
    require(after&&after->ticket.request==old.request&&after->deadlineTick==before.deadlineTick&&fixtureFanScopes.back()->current(),"the resumed owner keeps the original deadline and request");
   }
   start();
   require(fixtureFanStarts.size()<=2&&fixtureFanReadySignals==1&&count("sensors")==0,"duplicate projection cannot redeliver Fan Ready or start other consumers");
  });
  scenario("s4-then-unpaired-sleep-does-not-recover",[&]{
   privateSleep();publishSharedPowerLifecycle("fan-physical-no-wake");deliverProjection();
   require(fixtureFanStarts.empty(),"public Ready cannot fabricate a 507 for a newer 506");
  });
  scenario("accepted-critical-broadcast-waits-for-fan-close",[&]{
   const auto shared=currentPowerGeneration(),bound=currentFanPowerGeneration();
   const auto physical=g_fanPhysicalWake.recordSleep(shared,100);
   require(physical>bound&&g_fanPhysicalWake.beginClose(physical),"unpaired 506 owns the original Fan Close");
   publishSharedPowerLifecycle("fan-wake-absent");deliverProjection();
   require(fixtureFanStarts.empty(),"existing public Ready does not reopen the Fan Close");
   require(g_fanPhysicalWake.recordSharedFinalWake(shared,200,ymcc::fan_wake::Observation::SharedWake::CriticalBroadcast),
       "the accepted public critical broadcast supplies the only wake");
   publishSharedPowerLifecycle("fan-shared-critical-wake");deliverProjection();
   require(fixtureFanStarts.empty()&&fixtureFanReadySignals==0,"public Ready leaves the old Fan Close in charge");
   require(g_fanPhysicalWake.completeClose(physical)==0,"the original Close returns before Host work");
   publishSharedPowerLifecycle("fan-close-wake-post-failed");deliverProjection();
   require(fixtureFanStarts.size()==1&&fixtureFanStarts.front().generation==physical&&fixtureFanReadySignals==1,
       "the original projection starts the critical Fan wake once after Close returns");
   if(fixtureFanStarts.size()!=1)return;
   require(fixtureFanScopes.front()->current(),"the accepted Fan request carries current public admission");
   g_sgLatestSleepEntryFileTime=300;
   require(!fixtureFanScopes.front()->current(),"newer original 506 revokes the critical Fan request before UI delivery");
   fixtureFanReturnOld();
   publishSharedPowerLifecycle("fan-newer-kernel-entry");deliverProjection();
   require(fixtureFanStarts.size()==1&&fixtureFanReadySignals==1,"later Ready cannot reuse stale critical evidence");
  });
  scenario("never-enabled-is-zero-fan-work",[&]{
   fixtureFanActive=false;require(commitPowerResume(g_powerGeneration.load(),"fan-never-enabled"),"original already_ready is independent of Fan intent");deliverProjection();
   require(fixtureFanStarts.empty(),"no Fan intent creates no F5 owner or Host operation");
  });
  scenario("closed-owner-is-not-reopened",[&]{
   early();g_fanResumeRequestOwner.close();require(commitPowerResume(g_powerGeneration.load(),"fan-closed"),"Native commit does not depend on closed Fan owner");deliverProjection();fixtureFanReturnOld();
   require(fixtureFanStarts.size()==1,"closed Fan ledger cannot be revived by Native Ready");
  });
  scenario("expired-first-deadline-is-not-refunded",[&]{
   early();const auto first=g_fanResumeRequestOwner.snapshot(fixtureFanStarts.back()).value();readyFixtureNow=first.deadlineTick;
   require(commitPowerResume(g_powerGeneration.load(),"fan-expired"),"Native Ready still publishes after Fan deadline");deliverProjection();fixtureFanReturnOld();
   require(fixtureFanStarts.size()==1,"post-gate continuation cannot reset the first deadline");
  });
  scenario("failed-original-post-uses-original-delivery-timer",[&]{
   early();failPost=true;require(commitPowerResume(g_powerGeneration.load(),"fan-failed-post"),"original Native commit retains failed post debt");
   require(fixtureFanStarts.size()==1&&g_sleepPowerRedeliveryArmed,"failed post does not start another Fan request");failPost=false;drain();fixtureFanReturnOld();
   require(fixtureFanStarts.size()==2,"original delivery timer automatically continues Fan after real old return");
  });
 });

 if(cases.empty()){std::cerr<<"UNKNOWN_SHARED_POWER_PROJECTION_CASE\n";return 2;}
 json result={{"cases",cases},{"cssObservations",cssObservations},{"assertions",assertions},{"failures",failures},{"readyCommitScenarios",readyCommitScenarios},{"queueKind",readyWorkerQueueKind.load()},{"selectedKind",readyWorkerSelectedKind.load()},{"actualNativePublisherMessageReaderTimer",true},{"actualMsiSharedReadyScalarConsumer",true},{"sensorAndCursorAndTransportLeavesInert",true},{"deviceOrSteamOperations",0}};
 if(!jointReadyObservation.is_null())result["jointReadyObservation"]=jointReadyObservation;
 if(!fixtureMergedFanSnapshots.empty())result["mergedFanSnapshots"]=fixtureMergedFanSnapshots;
 if(!fixtureColdFanReplay.is_null())result["coldFanReplay"]=fixtureColdFanReplay;
 if(!fixtureRawReceiptFanReplay.is_null())result["rawReceiptFanReplay"]=fixtureRawReceiptFanReplay;
 if(!fixtureColdFanOffReplay.is_null())result["coldFanOffReplay"]=fixtureColdFanOffReplay;
 std::cout<<result.dump(2)<<'\n';return failures.empty()?0:1;
}
