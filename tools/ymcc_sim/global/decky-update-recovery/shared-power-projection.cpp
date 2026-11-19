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
[[maybe_unused]] static struct {
 struct View{bool enabled=false;};
 View state;
 bool fenced=false;
 bool exitFenced()const{return fenced;}
 View snapshot()const{return state;}
} g_deckySidebarRuntime;
[[maybe_unused]] static struct {
 std::vector<ymcc::sleeppower::SharedPowerV1> requests;
 ymcc::steamui::ModeObserver* actual=nullptr;
 bool requestDeckyResumeRefresh(const ymcc::steamsession::Session& session,const ymcc::sleeppower::SharedPowerV1& power){
  requests.push_back(power);return !actual||actual->requestDeckyResumeRefresh(session,power);
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
static void publishSharedPowerLifecycle(const char*);
struct Call{std::string name;uint64_t generation,publication;};
static std::vector<Call> calls;
static std::vector<json> emitted,cssObservations;
static std::vector<std::string> diagnostics;
static std::string faultStage,edge;
static bool injected=false,failPost=false,throwDiagnostic=false;
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
static std::atomic<unsigned long long> g_lastPowerResumeCommitGeneration{0},g_lastPowerResumeCommitTick{0};
static ULONGLONG readyFixtureNow=100000;
static bool readyReenter=false,readyNestedResult=true;
static unsigned readySignals=0,readyCloseCalls=0;
static std::vector<std::string> readyCommitScenarios;
static bool commitPowerResume(unsigned long long,const char*);
static ULONGLONG readyFixtureTick(){if(readyReenter){readyReenter=false;readyNestedResult=commitPowerResume(g_powerGeneration.load(),"fixture-reentrant-already-ready");}return readyFixtureNow;}
#define GetTickCount64 readyFixtureTick

static bool fixtureFanActive=false;
static bool fixtureFanIngress=false;
static unsigned fixtureFanReadySignals=0;
static std::uint64_t fixtureFanIntent=1;
static std::atomic<std::uint64_t> g_fanWritePowerRevision{1},g_fanResumeManualOperation{0};
static auto fixtureFanObservation=std::make_unique<ymcc::fan_wake::Observation>();
static auto fixtureFanRequests=std::make_unique<ymcc::fan_resume::RequestOwner>();
#define g_fanPhysicalWake (*fixtureFanObservation)
#define g_fanResumeRequestOwner (*fixtureFanRequests)
static std::vector<ymcc::fan_resume::RequestTicket> fixtureFanStarts;
static bool fanActivityIntentPresent(){
 if(fixtureFanIngress){fixtureFanIngress=false;readerIngress=6;}
 return fixtureFanActive;
}
static bool fanResumeActivityRevision(std::uint64_t& value){value=fixtureFanIntent;return fixtureFanActive;}
static bool fanManualActivityRevision(std::uint64_t& value){value=fixtureFanIntent;return true;}
static void fanHostStartResumeTicket(ymcc::fan_resume::RequestTicket);
// Fan's own fixture exercises the Close worker. This projection fixture has no Host Close port.
[[maybe_unused]] static void fanHostContinueUnstartedClose(unsigned long long) {}
static void appendFanLifecycleLog(const char*,const json&){}
static void ipc_emit(const char*,const json&);
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
 fixtureFanActive=true;fixtureFanIngress=false;fixtureFanReadySignals=0;fixtureFanIntent=1;g_fanWritePowerRevision=1;g_fanResumeManualOperation=0;
}
static void fixtureFanReturnOld(){
 const auto old=fixtureFanStarts.back();
 const auto next=g_fanResumeRequestOwner.finishRequest(old,readyFixtureNow,false);
 if(next)fanHostStartResumeTicket(next);
}

static bool sgShouldKeepInputDuringStandby(){return false;}
static void sleepRecoveryAutoCloseSuspendEpisode(){++readyCloseCalls;stage("close-episode",g_powerGeneration.load());}
static void openHardwareWriteGate();
static void sgStaticScanSchedule(bool){}
static void sgNotifyWork(){}
template<class... Args>static void traceLog(const char* format,Args...){if(std::string(format).starts_with("power gate opened"))stage("gate-open",g_powerGeneration.load());}
static bool sharedSleepWakeClassified(unsigned long long generation) noexcept;
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
static void msiNativeForegroundUiEvent(bool){stage("msi",g_powerGeneration.load());}
static void msiNativeForegroundQueue(){stage("msi",g_powerGeneration.load());}
@@MSI_SHARED_READY@@
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
static std::atomic<bool> g_focusIsolationPrepared{false},g_focusIsolationGameOpen{false};
static std::pair<std::uint64_t,std::uint64_t> g_focusIsolationPowerPeriod;
static std::atomic<std::shared_ptr<const FocusLightIsolationTicket>> g_focusLightIsolationTicket;
static std::shared_ptr<const bool> g_focusLightFallbackObservation;
static std::atomic<ULONGLONG> g_focusLightProofNotBefore{0};
static void steamLightGamepadEndIsolation(){stage("focus-end",g_powerGeneration.load());}
static void focusClearSession(){stage("focus-clear",g_powerGeneration.load());}
@@FOCUS_ISOLATION@@
static auto focusIsolationSharedPowerChanged(const ymcc::sleeppower::SharedPowerV1& power){
 stage("focus-isolation",power.generation);
 return productionFocusIsolationSharedPowerChanged(power);
}
namespace ymcc::lossless {struct ProjectionMonitor {void powerChanged() noexcept {}};static ProjectionMonitor& runtimeMonitor(){static ProjectionMonitor monitor;return monitor;}}
namespace SteamControllerCursor {static void power(const ymcc::sleeppower::SharedPowerV1& power){stage("cursor",power.generation);}}
namespace ymcc::inputHost::msiPowerWork {static void readyDelivery(const sleeppower::SharedPowerV1& power){stage("input-host-rearm",power.generation);} }
namespace ymcc::screenpads {static void ownerSharedPowerChanged(const ymcc::sleeppower::SharedPowerV1&){}}
[[maybe_unused]] static void appendNativeLifecycleLog(const char* name,const json& detail){
 if(throwDiagnostic)throw std::runtime_error("diagnostic leaf failed");
 if(std::string(name)!="power-projection-consumer-failed")throw std::runtime_error("unexpected diagnostic");
 diagnostics.push_back(detail.at("consumer").get<std::string>());
}
static void monitorSharedPowerChanged(){} // Independent monitor consumer is executed by its original monitor fixtures.
static void ipc_emit(const char* name,const json& value){if(std::string(name)=="fan.resume-ready"){++fixtureFanReadySignals;return;}if(std::string(name)!="power.lifecycleChanged")throw std::runtime_error("unexpected event");emitted.push_back(value);stage("ipc",g_powerGeneration.load());}
static UINT_PTR fixtureSetTimer(HWND,UINT_PTR,UINT,TIMERPROC){++arms;return 1;}
static BOOL fixtureKillTimer(HWND,UINT_PTR){return TRUE;}
static BOOL fixturePostMessage(HWND,UINT message,WPARAM,LPARAM){if(message==WM_POWER_RESUME_COMMIT)return failPost?FALSE:TRUE;if(message!=kSharedPowerStateMessage)throw std::runtime_error("unexpected post");++posts;return failPost?FALSE:TRUE;}
static LRESULT fixtureSendMessage(HWND h,UINT message,WPARAM w,LPARAM l){
 if(message==WM_POWER_RESUME_COMMIT)return 0;
 if(message==kSharedPowerStateMessage){projectSharedPowerLifecycle(h);return 0;}
 if(message==kSleepPowerForwardMessage){const auto receipt=g_sleepPowerDispatch.forwarded(static_cast<uint64_t>(l),static_cast<ULONG>(w),++serial);g_sleepPowerDispatch.complete(receipt,++serial);if(receipt&&publishIngressCompletion)publishSharedPowerLifecycle("ordered-power-delivery-complete");return 0;}
 throw std::runtime_error("unexpected retry business message");
}
#define SetTimer fixtureSetTimer
#define KillTimer fixtureKillTimer
#define PostMessageW fixturePostMessage
#define SendMessageW fixtureSendMessage
static void inputCaptureSensorThreadRetryPowerWake(HWND){} // Sensor owner is exercised by the original sensor-power-only selector.
static void inputCaptureSensorThreadRecoverReturned(HWND){} // Actual STA recovery executes in sleep.gyro-power power-wake.
@@DELIVERY@@
#define SetEvent readyFixtureSetEvent
static constexpr UINT_PTR SG_RESUME_CLASSIFY_TIMER_ID=0x7A71;
static std::atomic<ULONGLONG> g_sgLastPowerButtonSleepIntentFileTime{0},g_sgLastPowerButtonWakeEventFileTime{0};
// Diagnostic transport is unobserved in this projection scope; zero is not wake proof.
static std::atomic<ULONGLONG> g_sgOriginalPowerButtonWakeFileTime{0};
[[maybe_unused]] static ULONGLONG sgNowFileTime(){return 0;}
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
@@DECKY_REPEATED_COMMIT@@
@@READY_COMMIT@@
@@FAILURE_LOG@@
@@PUBLISHER@@
@@PROJECTION@@
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
static void settle(){const auto receipt=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPENDFAILED,++serial);g_sleepPowerDispatch.complete(receipt,++serial);}
static void reset(){
 g_sgResumeClassification.generation=0;g_sgResumeClassification.observedGeneration=0;g_sgWakeClassificationBlockedGeneration=0;
 settle();g_exitRequested=false;g_hardwareWriteGate=true;g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;
 g_powerGeneration.fetch_add(10);g_resumeReadyGeneration=g_powerGeneration.load();g_sharedPowerPublicationRevision.fetch_add(10);g_sharedPowerSuspendSerial.fetch_add(10);
 g_sharedPowerProjectionPending=false;g_sleepPowerRedeliveryArmed=false;g_sensorSuspendCallbackActive=false;g_aiFanMockSession.isolated=false;
 g_msiNativeForegroundPowerHold=true;g_msiNativeForegroundResumePending=true;g_msiNativeForegroundPowerTransitionObserved=true;g_msiNativeForegroundLastResumeGeneration=0;
 g_focusIsolationPrepared=false;g_focusIsolationGameOpen=false;g_focusLightProofNotBefore=0;
 g_focusIsolationPowerPeriod={};g_focusLightIsolationTicket.store({});g_focusLightFallbackObservation.reset();
 calls.clear();emitted.clear();diagnostics.clear();faultStage.clear();edge.clear();injected=failPost=throwDiagnostic=publishIngressCompletion=false;arms=posts=msiAttempts=readerIngress=0;
}
static void start(){publishSharedPowerLifecycle("fixture");projectSharedPowerLifecycle(g_hwnd);}
static void drain(){for(unsigned i=0;i<4&&g_sleepPowerRedeliveryArmed.load();++i)productionTimer();require(!g_sleepPowerRedeliveryArmed&&!g_sharedPowerProjectionPending,"original delivery debt and timer eventually settle");}
static void currentReady(){require(!emitted.empty()&&emitted.back().value("known",false)&&emitted.back().value("generation",0ull)==g_powerGeneration.load()&&emitted.back().value("hardwareWritesAllowed",false),"final event carries current admitted Native facts");}
int main(int argc,char** argv){
 const std::string selected=argc==2?argv[1]:"";
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
        publishSharedPowerLifecycle("fixture-css-query-pending");projectSharedPowerLifecycle(g_hwnd);
        require(refreshes==1,"pending suspend cannot authorize CSS work");
        settle();publishSharedPowerLifecycle("fixture-css-query-cancel");projectSharedPowerLifecycle(g_hwnd);
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
    const auto choices=inFlight?std::vector<std::string>{"active-no-demand","active-old-hint","active-new-hint","active-new-refresh","active-next-ready","active-retired-hint"}:
        std::vector<std::string>{"current","retired","independent","decky-off","theme-joined","successor"};
    for(const auto& control:choices){
        reset();
        using namespace decky_read_probe;
        struct Event{
            HANDLE handle=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            Event(){if(!handle)throw std::runtime_error("fixture event");}
            ~Event(){CloseHandle(handle);}
        } parked, go, settled, warm, callback, returnRead;
        entered=parked.handle;release=go.handle;idle=settled.handle;
        fixtureTick.store(10000);holdFinite=false;watchIdle=false;waitFailed=false;
        std::atomic<unsigned> reads{0}, delivers{0}, started{0}, cancelled{0};
        std::atomic<std::uint64_t> lastReadSerial{0};
        ymcc::steamui::ModeObserver observer;
        struct Cleanup{
            ymcc::steamui::ModeObserver& observer;
            HANDLE returnRead;
            ~Cleanup(){SetEvent(release);SetEvent(returnRead);observer.shutdown();g_steamModeObserver.actual=nullptr;g_deckySidebarRuntime.state.enabled=false;}
        } cleanup{observer,returnRead.handle};
        g_steamModeObserver.actual=&observer;g_steamModeObserver.requests.clear();g_deckySidebarRuntime.state.enabled=true;
        g_steamSettingsObserver.value={};g_steamSettingsObserver.value.pid=802;
        g_steamSettingsObserver.value.created=902;g_steamSettingsObserver.value.root=L"X:\\FixtureSteam";
        const auto session=g_steamSettingsObserver.sessionSnapshot();
        ymcc::steamui::ModeObserverConfig cfg;
        cfg.debounceMs=0;cfg.minimumGapMs=500;cfg.failureGapMs=1500;
        cfg.admissionPowerReader=&readSharedSystemPower;
        cfg.readOwned=[&](const auto& s,const auto& current)->json{
            if(++started==2&&inFlight){
                SetEvent(callback.handle);
                if(WaitForSingleObject(returnRead.handle,4000)!=WAIT_OBJECT_0)waitFailed=true;
            }
            ymcc::sleeppower::SharedPowerV1 p;
            if(!current()||!captureSharedPowerLifecycle(p)){++cancelled;return {{"reason","shared-mode-read-cancelled"}};}
            lastReadSerial=p.suspendSerial;++reads;
            return {{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},
                {"mode","bigpicture"},{"modeEpoch","fixture:1"}};
        };
        cfg.changed=[&](const auto&,const auto&){++delivers;SetEvent(warm.handle);};
        cfg.stableBigPicture=[&](const auto&){++delivers;};
        require(observer.initialize(std::move(cfg)),"actual ModeObserver starts");
        observer.setDemand(ymcc::steamui::ModeConsumer::Decky,true);observer.observeSteam(session);
        require(WaitForSingleObject(warm.handle,2000)==WAIT_OBJECT_0&&reads==1&&delivers==1,"original startup read settles");
        const auto generation=g_powerGeneration.load();
        auto commit=[&]{g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
            require(commitPowerResume(generation,"fixture-decky-queued-owner"),"original Ready commit succeeds");};
        auto boundary=[&]{++g_sharedPowerSuspendSerial;
            g_sleepPowerDispatch.callback(PBT_APMSUSPEND,++serial,[](auto){return false;});settle();};
        commit();require(g_steamModeObserver.requests.empty(),"first commit does not use the supplementary port");
        boundary();const auto acceptedSerial=g_sharedPowerSuspendSerial.load();
        holdFinite=true;commit();
        require(g_steamModeObserver.requests.size()==1,"repeated same-generation Ready is forwarded to the production observer");
        const bool held=WaitForSingleObject(parked.handle,2000)==WAIT_OBJECT_0;
        require(held&&reads==1,"production observer holds the pending read without doing IO before its slot");
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
            require(cancelled==1,"the old scoped read cannot observe the new Ready");
            readyCommitScenarios.push_back(control+" entered="+std::to_string(started.load())+" reads="+std::to_string(reads.load())+
                " deliveries="+std::to_string(delivers.load())+" cancelled="+std::to_string(cancelled.load())+
                " admitted="+std::to_string(acceptedSerial)+" read="+std::to_string(lastReadSerial.load()));
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
        require(WaitForSingleObject(settled.handle,2500)==WAIT_OBJECT_0&&!waitFailed,"observer reached idle after processing the single pending slot");
        readyCommitScenarios.push_back(control+" reads="+std::to_string(reads.load())+" deliveries="+std::to_string(delivers.load())+
            " admitted="+std::to_string(acceptedSerial)+" read="+std::to_string(lastReadSerial.load()));
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
        require(commitPowerResume(generation,"fixture-decky-repeated-ready"),"original Ready commit succeeds");};
    recommit();
    require(g_steamModeObserver.requests.empty(),"first commit leaves original per-generation Decky wave alone");
    recommit();
    require(g_steamModeObserver.requests.empty(),"same revocation recommit does not refresh");
    g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});
    settle();recommit();
    require(g_steamModeObserver.requests.empty(),"notification-only change does not refresh");
    ++g_sharedPowerSuspendSerial;
    g_sleepPowerDispatch.callback(PBT_APMSUSPEND,++serial,[](const auto&){return false;});
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
       publishSharedPowerLifecycle("fixture-cold-query-cancel");projectSharedPowerLifecycle(g_hwnd);
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

 run("normal-ready-and-late-message",[]{start();currentReady();require(count("sensors")==1&&count("msi")==1&&count("cursor")==1&&count("ipc")==1,"one current publication reaches every independent consumer");require(!g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&g_msiNativeForegroundLastResumeGeneration==g_powerGeneration.load(),"actual MSI scalar consumer releases private power hold");projectSharedPowerLifecycle(g_hwnd);require(count("sensors")==1&&count("ipc")==1,"late message without debt cannot replay consumers");require(!g_sleepPowerRedeliveryArmed,"healthy delivery creates no retry polling");});
 run("sensor-exception-isolated",[]{faultStage="sensors";edge="throw";start();require(count("sensors")==1&&count("msi")==1&&count("cursor")==1&&count("ipc")==1,"a failed sensor consumer cannot strand MSI/cursor/current facts");require(diagnostics==std::vector<std::string>{"sensors"},"sensor failure remains visible");currentReady();});
 run("msi-exception-isolated",[]{faultStage="msi";edge="throw";start();require(count("cursor")==1&&count("ipc")==1,"accepted MSI leaf exception cannot block later projections");require(!g_msiNativeForegroundPowerHold,"MSI acceptance remains owned despite a later UI exception");currentReady();});
 run("cursor-exception-isolated",[]{faultStage="cursor";edge="throw";start();require(count("ipc")==1,"cursor failure cannot suppress common facts");currentReady();});
 run("diagnostic-exception-isolated",[]{faultStage="sensors";edge="throw";throwDiagnostic=true;start();require(count("msi")==1&&count("cursor")==1&&count("ipc")==1,"diagnostic exception cannot break independent consumers");});
 run("battery-exception-still-queues",[]{faultStage="battery";edge="throw";start();require(posts==1&&count("sensors")==1&&count("ipc")==1,"publisher retains common projection after battery observer failure");require(diagnostics==std::vector<std::string>{"battery"},"battery observer failure is separate");});
 run("snapshot-failure-retries-readonly",[]{faultStage="snapshot";edge="throw";start();require(emitted.empty()&&g_sharedPowerProjectionPending&&arms==1,"read-only snapshot failure retains bounded delivery debt");drain();currentReady();require(count("sensors")==1&&count("msi")==1&&count("cursor")==1&&count("snapshot")==2,"snapshot retry never replays already dispatched module work");});
 run("ipc-failure-retries-facts-only",[]{faultStage="ipc";edge="throw";start();require(g_sharedPowerProjectionPending&&arms==1,"uncertain read-only event delivery is retained");drain();currentReady();require(count("sensors")==1&&count("msi")==1&&count("cursor")==1&&count("ipc")==2,"duplicate current-fact delivery cannot replay accepted business consumers");});
 run("failed-post-original-timer",[]{failPost=true;publishSharedPowerLifecycle("failed-post");require(g_sharedPowerProjectionPending&&g_sleepPowerRedeliveryArmed&&arms==1&&calls.size()==1,"failed original window post retains current projection debt");failPost=false;drain();currentReady();});
 run("burst-coalesces-current-facts",[]{for(unsigned i=0;i<50;++i)publishSharedPowerLifecycle("burst");require(posts==1&&!g_sleepPowerRedeliveryArmed,"publication burst owns one original window message");projectSharedPowerLifecycle(g_hwnd);require(emitted.size()==1&&emitted[0].at("publicationRevision")==g_sharedPowerPublicationRevision.load(),"coalesced event uses latest publication");});
 run("pending-before-ui-consume",[]{g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});start();require(count("sensors")==0&&msiAttempts==0&&g_msiNativeForegroundPowerHold,"actual Dispatch pending refuses every Ready-only consumer");require(count("cursor")==1&&emitted.back().value("notificationPending",false)&&!emitted.back().value("hardwareWritesAllowed",true),"pending is still published as current diagnostic facts");settle();start();currentReady();require(count("sensors")==1&&!g_msiNativeForegroundPowerHold,"same-generation cancellation can recover without raw resume");});
 run("sensor-pending-retires-msi",[]{faultStage="sensors";edge="pending";start();require(msiAttempts==0&&g_msiNativeForegroundPowerHold,"notification ingress during sensor callback cannot reach old MSI Ready release");require(emitted.back().value("notificationPending",false),"snapshot sees ingress after the callback");settle();start();require(!g_msiNativeForegroundPowerHold,"current Ready later releases original private hold");});
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
   projectSharedPowerLifecycle(g_hwnd);
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
  projectSharedPowerLifecycle(g_hwnd);start();
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
 });
 run("sensor-new-publication-stops-old-suffix",[]{faultStage="sensors";edge="advance";const auto old=g_powerGeneration.load();start();require(msiAttempts==0&&emitted.empty(),"new publication during sensor callback retires old suffix");drain();currentReady();require(std::none_of(calls.begin(),calls.end(),[&](const auto& call){return call.name=="msi"&&call.generation==old;}),"old callback never relabels MSI work as successor generation");});
 run("cursor-new-publication-never-emits-old-ready",[]{faultStage="cursor";edge="advance";start();require(std::all_of(emitted.begin(),emitted.end(),[](const auto& value){return value.value("generation",0ull)==g_powerGeneration.load();}),"cursor callback cannot emit its previously captured older Ready payload");drain();currentReady();});
 run("same-publication-reentrancy-is-coalesced",[]{faultStage="sensors";edge="reentrant";start();drain();require(count("sensors")==1&&count("msi")==1&&count("cursor")==1,"nested same-publication delivery cannot duplicate accepted module callbacks");require(arms==1,"reentrant debt uses one original50ms timer");});
 run("exit-during-sensors-stops-projection",[]{faultStage="sensors";edge="exit";start();require(msiAttempts==0&&count("cursor")==0&&emitted.empty(),"exit prevents every remaining module and fact publication");require(!g_sleepPowerRedeliveryArmed,"exit does not create a recovery loop");});
 run("suspending-diagnostics-only",[]{g_powerLifecycle=PowerLifecycle::Suspending;g_hardwareWriteGate=false;start();require(count("sensors")==0&&msiAttempts==0&&count("cursor")==1&&emitted.back().at("phase")=="suspending","nonReady facts do not grant module admission");require(!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed,"stable nonReady diagnostics do not poll");});
 run("resuming-diagnostics-only",[]{g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;start();require(count("sensors")==0&&msiAttempts==0&&count("cursor")==1&&emitted.back().at("phase")=="resuming","projection cannot turn resume progress into Ready permission");});
 run("unknown-source-diagnostics-only",[]{g_powerGeneration=0;start();require(count("sensors")==0&&msiAttempts==0&&count("cursor")==1&&!emitted.back().value("known",true),"UNKNOWN source never opens private consumers");require(!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed,"stable UNKNOWN diagnostics do not poll");});
 run("new-publication-after-truncation-recovers",[]{faultStage="sensors";edge="phase";start();drain();require(msiAttempts==0&&emitted.back().at("phase")=="suspended","interruption publishes current suspended fact");g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;start();currentReady();require(!g_msiNativeForegroundPowerHold&&!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed,"later same-generation Ready releases scalar hold and original delivery resources");});
 run("queued-successor-current-facts",[]{faultStage="cursor";edge="advance";start();require(posts==2,"new publication retains its actual queued successor window message");projectSharedPowerLifecycle(g_hwnd);if(g_sleepPowerRedeliveryArmed.load())productionTimer();currentReady();require(std::all_of(emitted.begin(),emitted.end(),[](const auto& value){return value.value("generation",0ull)==g_powerGeneration.load();}),"even with successor window delivery, no older Ready payload may escape");require(!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed,"actual successor window delivery and timer both settle");});
 run("ready-commit-original-recovery-postgate",[]{
    const auto scenario=[&](const char* name,const std::function<void()>& body){reset();g_lastPowerResumeCommitTick=0;g_lastPowerResumeCommitGeneration=0;readyFixtureNow+=100000;readyCommitScenarios.push_back(name);body();};
    scenario("normal-commit-queues-original-worker-after-gate",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(g_powerGeneration.load(),"fixture-normal"),"actual native Ready commit succeeds");projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAttempts==1&&readyWorkerUnknown==0&&readyWorkerAdmissions==1,"one actual post-gate projection automatically queues and admits original Wake without a second commit call");const auto signals=readySignals;const auto tick=g_lastPowerResumeCommitTick.load();for(unsigned i=0;i!=5;++i){publishSharedPowerLifecycle("diagnostic-only-publication");projectSharedPowerLifecycle(g_hwnd);}require(readySignals==signals&&readyWorkerAttempts==1&&g_lastPowerResumeCommitTick==tick,"same Native notification identity never repeats Ready consumer or resets its commit tick");});
    scenario("first-recovery-listener-failure-is-isolated",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;faultStage="sleep-recovery";edge="throw";require(commitPowerResume(g_powerGeneration.load(),"fixture-listener-failed"),"commit does not enter consumer early");projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAdmissions==1&&readyWorkerUnknown==0&&diagnostics==std::vector<std::string>{"sleep-recovery"},"failed first recovery listener cannot strand the original Steam recovery queue");const auto attempts=readyWorkerAttempts.load();publishSharedPowerLifecycle("after-listener-error");projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAttempts==attempts,"uncertain listener work and successful sibling are not replayed by diagnostics");});
    scenario("already-ready-second-call-while-gate-closed",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;readyReenter=true;require(commitPowerResume(g_powerGeneration.load(),"fixture-outer"),"outer commit continues after nested alreadyReady call");projectSharedPowerLifecycle(g_hwnd);require(!readyNestedResult&&readyWorkerUnknown==0&&readyWorkerAdmissions==1&&readySignals==1,"nested Ready-before-gate call cannot consume or publish early recovery");});
    scenario("nonready-during-precommit-cannot-be-overwritten",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;faultStage="close-episode";edge="phase";require(!commitPowerResume(g_powerGeneration.load(),"fixture-interrupted"),"new NonReady cancels the original commit");projectSharedPowerLifecycle(g_hwnd);require(g_powerLifecycle==PowerLifecycle::Suspended&&!g_hardwareWriteGate&&readyWorkerAttempts==0,"old commit never overwrites newer NonReady or wakes its consumer");});
    scenario("exit-during-precommit-cannot-publish",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;faultStage="close-episode";edge="exit";require(!commitPowerResume(g_powerGeneration.load(),"fixture-exit")&&readyWorkerAttempts==0&&!g_hardwareWriteGate,"exit before Ready suppresses notification and gate open");});
    scenario("pending-arrives-after-gate-before-projection",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(g_powerGeneration.load(),"fixture-before-pending"),"actual initial commit completed");g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](auto){return false;});projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAttempts==0&&readySignals==0,"pending original ingress blocks Ready consumer before its queue");settle();publishSharedPowerLifecycle("original-query-cancel");projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAdmissions==1&&readyWorkerUnknown==0,"existing post-cancel publication automatically releases the original Wake");});
    scenario("failed-window-post-original-timer-delivers",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;failPost=true;require(commitPowerResume(g_powerGeneration.load(),"fixture-post-failed"),"commit survives original message post failure");require(readyWorkerAttempts==0&&g_sleepPowerRedeliveryArmed,"failed post retains original delivery debt");failPost=false;drain();require(readyWorkerAttempts==1&&readyWorkerAdmissions==1&&readyWorkerUnknown==0,"original retry timer automatically delivers post-gate Ready without repeating commit");});
    scenario("already-ready-closed-gate-alone-is-not-ready",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=false;require(!commitPowerResume(g_powerGeneration.load(),"fixture-closed-ready")&&readySignals==0&&readyWorkerAttempts==0,"alreadyReady phase without the common hardware gate cannot issue recovery notification");});
    scenario("same-generation-query-cancel-keeps-admission-consumed",[]{ReadyConsumerScope owner;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(g_powerGeneration.load(),"fixture-once"),"first commit succeeds");projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAdmissions==1,"first original Wake admission consumed");ResetEvent(readyWorkerHandled);g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](auto){return false;});publishSharedPowerLifecycle("query");projectSharedPowerLifecycle(g_hwnd);settle();publishSharedPowerLifecycle("cancel");projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAttempts==2&&readyWorkerAdmissions==1&&readyWorkerUnknown==0,"new notification identity can recheck original Wake but never refunds its Admission");});
 });

 run("ready-classification-original-recovery",[]{
    const auto scenario=[&](const char* name,const std::function<void()>& body){reset();classificationRetry=classificationManual=classificationSuccess=false;classificationFacts=classificationHandlerCalls=classificationInteractive=0;readyFixtureNow+=100000;readyCommitScenarios.push_back(name);body();};
    scenario("blocked-classification-before-ready-retains-original-wake",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();sgBlockSharedWakeClassification(generation);g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(generation,"fixture-classification-pending"),"Native Ready may finish before classification");projectSharedPowerLifecycle(g_hwnd);require(readySignals==0&&readyWorkerAttempts==0,"pending original classification cannot claim or invoke either recovery listener");sgCompleteSharedWakeClassification(generation);require(g_sharedPowerProjectionPending,"actual original classification completion publishes existing common delivery debt");projectSharedPowerLifecycle(g_hwnd);require(readySignals==1&&readyWorkerAdmissions==1&&readyWorkerUnknown==0,"classification completion automatically releases the original Wake through common projection");const auto revision=g_sharedPowerPublicationRevision.load();sgCompleteSharedWakeClassification(generation);require(g_sharedPowerPublicationRevision==revision&&!g_sharedPowerProjectionPending,"duplicate completion never publishes another readiness edge");publishSharedPowerLifecycle("diagnostic");projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAttempts==1&&readySignals==1,"ordinary publication after classification never refunds or redelivers original work");});
    scenario("actual-finish-without-blocked-flag-notifies-both-listeners",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();g_sgResumeClassification.generation=generation;g_sgResumeClassification.interactiveReceiptTick=123;g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(generation,"fixture-owned-classification"),"commit remains independent of classification state");projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAttempts==0&&readySignals==0,"owned classification prevents premature listener claims");sgFinishResumeClassification("fixture-original-finish");require(classificationHandlerCalls==1&&classificationInteractive==1&&g_sharedPowerProjectionPending,"actual original Finish preserves its handler and publishes common completion");projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAdmissions==1&&readySignals==1,"both independent listeners receive actual finish even without a blockedGeneration flag");});
    scenario("stale-active-and-exit-completions-do-not-publish",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();sgBlockSharedWakeClassification(generation);auto revision=g_sharedPowerPublicationRevision.load();sgCompleteSharedWakeClassification(generation-1);require(g_sgWakeClassificationBlockedGeneration==generation&&g_sharedPowerPublicationRevision==revision,"old generation cannot complete or notify current classification");g_sgResumeClassification.generation=generation;sgCompleteSharedWakeClassification(generation);require(g_sgWakeClassificationBlockedGeneration==generation&&g_sharedPowerPublicationRevision==revision,"still-owned classification cannot be completed by an unrelated callback");g_sgResumeClassification.generation=0;g_exitRequested=true;sgCompleteSharedWakeClassification(generation);require(g_sgWakeClassificationBlockedGeneration==generation&&g_sharedPowerPublicationRevision==revision&&readyWorkerAttempts==0,"exit cannot mint classification completion or enqueue recovery");});
    scenario("complete-before-ready-uses-original-commit-post",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();sgBlockSharedWakeClassification(generation);g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;sgCompleteSharedWakeClassification(generation);projectSharedPowerLifecycle(g_hwnd);require(readySignals==0&&readyWorkerAttempts==0,"classification completion alone cannot grant Native Ready");require(commitPowerResume(generation,"fixture-ready-after-classification"),"the original later Ready commit remains available");projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAdmissions==1&&readySignals==1,"later original commit automatically joins the already completed classification");});
    scenario("completion-during-current-projection-uses-original-redelivery",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();sgBlockSharedWakeClassification(generation);g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(generation,"fixture-reentrant-classification"),"original commit succeeds");faultStage="sensors";edge="classification";projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAttempts==0&&g_sharedPowerProjectionPending&&g_sleepPowerRedeliveryArmed,"new classification publication retires the old projection suffix");drain();require(readyWorkerAdmissions==1&&readySignals==1&&!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed,"existing delivery timer joins current classification without extra polling");});
    scenario("stop-observation-alone-does-not-mint-classification-ready",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();sgBlockSharedWakeClassification(generation);g_sgResumeClassification.generation=generation;const auto revision=g_sharedPowerPublicationRevision.load();sgStopResumeClassificationWait("fixture-cancel-observation");require(g_sgWakeClassificationBlockedGeneration==generation&&g_sharedPowerPublicationRevision==revision&&readySignals==0,"cancelling observation does not turn Unknown into a classified wake");});
    scenario("exit-during-finish-keeps-cancelled-consumer-retired",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();g_sgResumeClassification.generation=generation;g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;faultStage="classification-record";edge="exit";sgFinishResumeClassification("fixture-exit-in-stop");require(classificationHandlerCalls==0&&readyWorkerAttempts==0&&!g_sharedPowerProjectionPending,"actual Finish rechecks exit after original stop/diagnostic before common publication");});
 });

 run("ready-classification-startup-current-consumer",[]{
    const auto scenario=[&](const char* name,const std::function<void()>& body){reset();classificationRetry=classificationManual=classificationSuccess=false;classificationFacts=classificationHandlerCalls=classificationInteractive=0;readyFixtureNow+=100000;readyCommitScenarios.push_back(name);body();};
    scenario("late-classification-joins-original-pending-startup",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();{std::lock_guard<std::mutex> lock(owner.c->mx);owner.c->deferredWake=0;owner.c->enabled=true;owner.c->startupPending=true;}sgBlockSharedWakeClassification(generation);g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(generation,"fixture-pending-startup"),"original Ready commit may precede classification");projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAttempts==0&&readySignals==0,"pending classification cannot manufacture a Startup or consume its delivery");sgCompleteSharedWakeClassification(generation);projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAttempts==1&&readyWorkerAdmissions==1&&readyWorkerUnknown==0&&readyWorkerQueueKind==static_cast<int>(SteamOriginalRecovery::Trigger::TargetChanged)&&readyWorkerSelectedKind==static_cast<int>(SteamOriginalRecovery::Trigger::Startup),"actual current PowerReady automatically queues ordinary TargetChanged and original selector admits the pending Startup after classification");const auto attempts=readyWorkerAttempts.load();publishSharedPowerLifecycle("diagnostic-after-startup");projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAttempts==attempts&&readyWorkerAdmissions==1,"pure diagnostic publication neither creates a new Startup request nor refunds its Admission");});
    scenario("valid-deferred-wake-keeps-priority-over-pending-startup",[]{ReadyConsumerScope owner;const auto generation=g_powerGeneration.load();owner.c->enabled=true;owner.c->startupPending=true;sgBlockSharedWakeClassification(generation);g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;require(commitPowerResume(generation,"fixture-deferred-wake"),"original Ready commit succeeds with both pending intents");projectSharedPowerLifecycle(g_hwnd);sgCompleteSharedWakeClassification(generation);projectSharedPowerLifecycle(g_hwnd);require(readyWorkerAttempts==1&&readyWorkerAdmissions==1&&readyWorkerUnknown==0&&readyWorkerQueueKind==static_cast<int>(SteamOriginalRecovery::Trigger::Wake)&&readyWorkerSelectedKind==static_cast<int>(SteamOriginalRecovery::Trigger::Wake)&&!owner.c->startupPending.load(),"actual current callback keeps Wake priority and accepted Wake covers Startup for this pair");
     const ymcc::steamOriginal::Scope samePair{{1,1,11,L"parent"},{2,1,22,L"game"},{3,1,33,L"steam"},9};
     SteamOriginalRecovery::Pending ordinary{SteamOriginalRecovery::Trigger::TargetChanged,0,100};
     SteamOriginalRecovery::readyFixtureSelectPending(owner.c,ordinary,samePair,true,false);
     require(ordinary.kind==SteamOriginalRecovery::Trigger::TargetChanged&&!owner.c->startupPending.load(),"the same pair cannot manufacture Startup after an accepted Wake");
     auto newPair=samePair;++newPair.game.birth;SteamOriginalRecovery::Pending newGame{SteamOriginalRecovery::Trigger::TargetChanged,0,101};
     SteamOriginalRecovery::readyFixtureSelectPending(owner.c,newGame,newPair,true,false);
     require(newGame.kind==SteamOriginalRecovery::Trigger::Startup&&owner.c->startupPending.load(),"the exact current selector admits a new game pair even after the old pair cleared its Startup marker");
     require(owner.c->admission.preview(newPair,{SteamOriginalRecovery::Trigger::Startup,0,102},true).accepted,"selection preview itself does not consume the new pair Startup");});
 });


 run("fan-post-gate-resume-original-owner",[]{
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
   projectSharedPowerLifecycle(g_hwnd);
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
  const auto recoverParkedProjection=[&](bool newPublication){
   early();require(commitPowerResume(g_powerGeneration.load(),"fan-persistent-reader"),"original commit publishes Ready");
   const auto publication=g_sharedPowerPublicationRevision.load();const auto startedPosts=posts;
   readerIngress=256;projectSharedPowerLifecycle(g_hwnd);
   for(unsigned i=0;i<4&&g_sleepPowerRedeliveryArmed.load();++i)productionTimer();
   require(arms==3&&!g_sleepPowerRedeliveryArmed&&readerIngress>0,"persistent moving Reader parks after the original three shared timer reservations");
   require(emitted.empty()&&fixtureFanStarts.size()==1&&g_fanResumeRequestOwner.pending()==0,"parked projection cannot publish Ready or authorize Fan");
   readerIngress=0;
   if(newPublication){publishSharedPowerLifecycle("fixture-real-publication-after-parking");
    require(posts==startedPosts+1,"new publication after the original message is consumed must post a new window delivery");
    if(posts==startedPosts+1)projectSharedPowerLifecycle(g_hwnd);
   }else projectSharedPowerLifecycle(g_hwnd);
   currentReady();
   require(g_sharedPowerPublicationRevision==publication+(newPublication?1u:0u)&&!g_sharedPowerProjectionPending&&g_fanResumeRequestOwner.pending()==currentFanPowerGeneration(),"a real window delivery settles parked work without minting another generation");
   fixtureFanReturnOld();require(fixtureFanStarts.size()==2,"parked Fan recovers through the original request owner");
  };
  scenario("persistent-moving-reader-parks-current-ready",[&]{recoverParkedProjection(false);});
  scenario("new-publication-wakes-parked-projection",[&]{recoverParkedProjection(true);});
  scenario("exit-retires-interrupted-projection",[&]{
   early();require(commitPowerResume(g_powerGeneration.load(),"fan-exit-debt"),"original commit publishes Ready");
   readerIngress=6;projectSharedPowerLifecycle(g_hwnd);
   require(g_sharedPowerProjectionPending&&g_sleepPowerRedeliveryArmed,"interrupted Ready has outstanding projection delivery");
   g_exitRequested=true;if(g_sleepPowerRedeliveryArmed.load())productionTimer();
   require(!g_sharedPowerProjectionPending&&!g_sleepPowerRedeliveryArmed&&fixtureFanStarts.size()==1&&emitted.empty(),"exit retires projection debt without admitting Fan or publishing Ready");
  });
  scenario("normal-commit-automatically-continues-same-owner",[&]{
   early();const auto old=fixtureFanStarts.back();const auto before=g_fanResumeRequestOwner.snapshot(old).value();
   const auto post=g_fanResumeRequestOwner.reservePost(old,readyFixtureNow);require(bool(post),"original request reserves its first cumulative slot");
   require(g_fanResumeRequestOwner.completePost(post,false),"original authenticated acceptance consumes its POST slot");
   const auto key=g_fanResumeRequestOwner.requestKey(old).value();
   require(g_fanResumeRequestOwner.confirmAwaitingGrant(old,key.first,key.second),"original accepted business retains awaiting-grant evidence");
   readyFixtureNow+=50;
   require(commitPowerResume(g_powerGeneration.load(),"fan-normal"),"original commit opens Native hardware admission");
   projectSharedPowerLifecycle(g_hwnd);
   require(fixtureFanStarts.size()==1,"post-gate listener cannot replace still-borrowed old IO");
   require(!fixtureFanScopes.front()->current(),"old Resuming registration cannot authorize Ready work");
   require(g_fanResumeRequestOwner.pending()==currentFanPowerGeneration(),"current Ready retains a successor on the original owner");
   fixtureFanReturnOld();
   require(fixtureFanStarts.size()==2,"returning old IO automatically starts retained work without another click");
   const auto now=fixtureFanStarts.back();const auto after=g_fanResumeRequestOwner.snapshot(now).value();
   require(now.request==old.request&&now.run!=old.run&&before.startedTick==after.startedTick&&before.deadlineTick==after.deadlineTick,"continuation preserves original request and first deadline");
   require(after.attempts==1&&after.posts==1&&fixtureFanScopes.back()->current(),"fresh Native registration keeps already consumed slot");
   const auto continuation=g_fanResumeRequestOwner.reserveContinuation(now,readyFixtureNow);
   require(bool(continuation)&&continuation.slot==2,"new grant spends the same cumulative second slot");
   g_fanResumeRequestOwner.finishRequest(now,readyFixtureNow,true);
   const auto starts=fixtureFanStarts.size();for(unsigned i=0;i<5;++i){publishSharedPowerLifecycle("fan-diagnostic");projectSharedPowerLifecycle(g_hwnd);}
   require(fixtureFanStarts.size()==starts,"publication-only changes cannot reopen completed Fan business");
  });
  scenario("already-ready-publication-continues-original-request",[&]{
   fanHostScheduleFanResume(currentFanPowerGeneration(),false,0);const auto old=fixtureFanStarts.back();const auto before=g_fanResumeRequestOwner.snapshot(old).value();
   settle();readyFixtureNow+=25;require(commitPowerResume(g_powerGeneration.load(),"fan-already-ready"),"actual already_ready publishes qualified current Native facts");
   projectSharedPowerLifecycle(g_hwnd);fixtureFanReturnOld();
   require(fixtureFanStarts.size()==2,"already_ready automatically joins same Fan owner without second manualWake");
   const auto next=g_fanResumeRequestOwner.snapshot(fixtureFanStarts.back()).value();
   require(next.ticket.request==old.request&&next.startedTick==before.startedTick&&next.deadlineTick==before.deadlineTick,"already_ready does not refund original budget");
  });
  scenario("pending-notification-cannot-admit-fan-work",[&]{
   early();const auto receipt=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPEND,++serial);
   g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;publishSharedPowerLifecycle("fan-pending");projectSharedPowerLifecycle(g_hwnd);
   require(fixtureFanStarts.size()==1&&g_fanResumeRequestOwner.pending()==0,"unclaimed Native notification cannot continue Fan work");
   // A lease rollback can retire Fan's own write admission at the same edge.
   ++g_fanWritePowerRevision;
   g_sleepPowerDispatch.complete(receipt,++serial);publishSharedPowerLifecycle("fan-pending-completed");projectSharedPowerLifecycle(g_hwnd);fixtureFanReturnOld();
   require(fixtureFanStarts.size()==2,"existing publication after receipt completion resumes the original work");
   const auto continued=g_fanResumeRequestOwner.snapshot(fixtureFanStarts.back());
   require(continued&&continued->writeRevision==g_fanWritePowerRevision&&fixtureFanScopes.back()->current(),"continuation carries both current Native admission and current Fan write revision");
  });
  scenario("off-does-not-resurrect-control",[&]{
   early();fixtureFanActive=false;require(commitPowerResume(g_powerGeneration.load(),"fan-off"),"Native may recover independently of Fan OFF");projectSharedPowerLifecycle(g_hwnd);fixtureFanReturnOld();
   require(fixtureFanStarts.size()==1,"OFF prevents automatic Fan continuation");
  });
  const auto privateSleep=[&]{
   const auto shared=currentPowerGeneration();
   const auto bound=currentFanPowerGeneration();
   const auto physical=g_fanPhysicalWake.recordSleep(shared,100);
   require(physical>bound&&g_fanPhysicalWake.beginClose(physical),"next 506 creates a Fan cycle distinct from the public S4 wake");
   require(g_fanPhysicalWake.completeClose(physical)==0,"original Close worker has returned");
   return physical;
  };
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
   publishSharedPowerLifecycle("fan-admission-metadata");projectSharedPowerLifecycle(g_hwnd);
   require(fixtureFanStarts.size()<=1,"duplicate Ready does not start a second Fan request");
  };
  scenario("shared-wake-zero-admission-does-not-claim-delivery",[&]{recoverInterruptedFanAdmission(false);});
  scenario("paired-physical-wake-zero-admission-does-not-claim-delivery",[&]{recoverInterruptedFanAdmission(true);});
  scenario("s4-then-physical-wake-waits-for-public-admission",[&]{
   const auto physical=privateSleep();const auto receipt=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPEND,++serial);
   require(g_fanPhysicalWake.recordWake(currentPowerGeneration(),200,false),"matching 507 is retained despite pending public notification");
   fanHostPublishPhysicalWake(physical);publishSharedPowerLifecycle("fan-physical-pending");projectSharedPowerLifecycle(g_hwnd);
   require(fixtureFanStarts.empty(),"neither Fan fact nor public projection admit work while notification is pending");
   g_sleepPowerDispatch.complete(receipt,++serial);publishSharedPowerLifecycle("fan-physical-admitted");projectSharedPowerLifecycle(g_hwnd);
   require(fixtureFanStarts.size()==1&&fixtureFanStarts.back().generation==physical,"public Ready delivers the retained newer physical Fan wake without a second 507 or click");
   publishSharedPowerLifecycle("fan-physical-metadata");projectSharedPowerLifecycle(g_hwnd);
   require(fixtureFanStarts.size()<=1,"metadata publication does not duplicate the physical wake request");
  });
  scenario("s4-then-physical-wake-rebinds-already-published-owner",[&]{
   const auto physical=privateSleep();
   require(g_fanPhysicalWake.recordWake(currentPowerGeneration(),200,false),"matching 507 initially admits the physical wake");
   fanHostPublishPhysicalWake(physical);
   require(fixtureFanStarts.size()==1,"one physical Fan owner starts before public revocation");
   const auto old=fixtureFanStarts.back();const auto before=g_fanResumeRequestOwner.snapshot(old).value();
   const auto receipt=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPEND,++serial);
   publishSharedPowerLifecycle("fan-physical-revoked");projectSharedPowerLifecycle(g_hwnd);
   require(!fixtureFanScopes.front()->current()&&g_fanResumeRequestOwner.pending()==0,"pending notification revokes old scope and cannot grant a successor");
   readyFixtureNow+=50;
   g_sleepPowerDispatch.complete(receipt,++serial);publishSharedPowerLifecycle("fan-physical-regranted");projectSharedPowerLifecycle(g_hwnd);
   require(fixtureFanStarts.size()==1,"new authority waits for original worker to return");
   fixtureFanReturnOld();
   require(fixtureFanStarts.size()==2,"new public Ready rebinds already published physical wake work");
   const auto after=g_fanResumeRequestOwner.snapshot(fixtureFanStarts.back());
   require(after&&after->ticket.request==old.request&&after->ticket.generation==physical&&after->deadlineTick==before.deadlineTick&&fixtureFanScopes.back()->current(),"rebinding keeps the original request and deadline under current authority");
  });
  scenario("s4-then-unpaired-sleep-does-not-recover",[&]{
   privateSleep();publishSharedPowerLifecycle("fan-physical-no-wake");projectSharedPowerLifecycle(g_hwnd);
   require(fixtureFanStarts.empty(),"public Ready cannot fabricate a 507 for a newer 506");
  });
  scenario("never-enabled-is-zero-fan-work",[&]{
   fixtureFanActive=false;require(commitPowerResume(g_powerGeneration.load(),"fan-never-enabled"),"original already_ready is independent of Fan intent");projectSharedPowerLifecycle(g_hwnd);
   require(fixtureFanStarts.empty(),"no Fan intent creates no F5 owner or Host operation");
  });
  scenario("closed-owner-is-not-reopened",[&]{
   early();g_fanResumeRequestOwner.close();require(commitPowerResume(g_powerGeneration.load(),"fan-closed"),"Native commit does not depend on closed Fan owner");projectSharedPowerLifecycle(g_hwnd);fixtureFanReturnOld();
   require(fixtureFanStarts.size()==1,"closed Fan ledger cannot be revived by Native Ready");
  });
  scenario("expired-first-deadline-is-not-refunded",[&]{
   early();const auto first=g_fanResumeRequestOwner.snapshot(fixtureFanStarts.back()).value();readyFixtureNow=first.deadlineTick;
   require(commitPowerResume(g_powerGeneration.load(),"fan-expired"),"Native Ready still publishes after Fan deadline");projectSharedPowerLifecycle(g_hwnd);fixtureFanReturnOld();
   require(fixtureFanStarts.size()==1,"post-gate continuation cannot reset the first deadline");
  });
  scenario("failed-original-post-uses-original-delivery-timer",[&]{
   early();failPost=true;require(commitPowerResume(g_powerGeneration.load(),"fan-failed-post"),"original Native commit retains failed post debt");
   require(fixtureFanStarts.size()==1&&g_sleepPowerRedeliveryArmed,"failed post does not start another Fan request");failPost=false;drain();fixtureFanReturnOld();
   require(fixtureFanStarts.size()==2,"original delivery timer automatically continues Fan after real old return");
  });
 });

 if(cases.empty()){std::cerr<<"UNKNOWN_SHARED_POWER_PROJECTION_CASE\n";return 2;}
 std::cout<<json{{"cases",cases},{"cssObservations",cssObservations},{"assertions",assertions},{"failures",failures},{"readyCommitScenarios",readyCommitScenarios},{"queueKind",readyWorkerQueueKind.load()},{"selectedKind",readyWorkerSelectedKind.load()},{"actualNativePublisherMessageReaderTimer",true},{"actualMsiSharedReadyScalarConsumer",true},{"sensorAndCursorAndTransportLeavesInert",true},{"deviceOrSteamOperations",0}}.dump(2)<<'\n';return failures.empty()?0:1;
}
