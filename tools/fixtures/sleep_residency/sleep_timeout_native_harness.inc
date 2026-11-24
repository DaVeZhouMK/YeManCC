// Actual timeout policy/native include and exact main.cpp sender/retry consumers.
// Every Windows power, timer, privilege, clock and action-reader boundary is inert.
#include <windows.h>
#include <powrprof.h>
#include <atomic>
#include <array>
#include <climits>
#include <functional>
#include <map>
#include <mutex>
#include <memory>
#include <winevt.h>
#include "sleep_kernel_cycle.h"
#include "sleep_power_observation.h"
#include "sleep_power_dispatch.h"
#ifdef YMCC_SLEEP_LATE_WAKE_ENABLE
#include "sleep_cancel_ready.h"
#endif
#include "sleep_timeout_policy.h"
#include "sleep_request_fallback.h"
#include "sleep_retry_budget.h"
#ifdef YMCC_SLEEP_LATE_WAKE_ENABLE
#include "sleep_abnormal_resleep.h"
#endif
namespace sleep_timeout_fixture {
#include "production_timeout_types.inc"
#ifdef YMCC_SLEEP_LATE_WAKE_ENABLE
#include "production_late_wake_types.inc"
#endif
static constexpr UINT timeoutTimerId=0xA214,retryTimerId=0xA20C;
static constexpr ULONGLONG epoch=134372000000000000ULL;
static HWND g_hwnd=reinterpret_cast<HWND>(1);
static bool g_guardEnabled=true,g_sgRetryEntryFailure=false,g_sgResleepEnabled=false,g_sgRepairEligible=true;
static bool g_sgRetryInProgress=false,g_sgModernWakeClassified=false,g_sgModernStandbyActive=false;
static std::atomic<bool> g_exitRequested{false};
static bool g_sgNonUserWakeObserved=false;
static std::mutex g_sgOpMtx;
static ymcc::sleeprequest::CompletionWitness g_sgRetryWindowsSuccessAtAdmission;
// This companion dispatches no Native callbacks; use the real empty dispatch.
static ymcc::sleeppower::Dispatch g_sleepPowerDispatch;
#ifdef YMCC_SLEEP_LATE_WAKE_ENABLE
static ymcc::sleeppower::CancellationReady g_sgCancellationReady;
#endif
static std::atomic<ULONGLONG> g_sgLastPowerButtonSleepIntentFileTime{epoch},g_sgLastPowerButtonWakeEventFileTime{0};
static std::atomic<ULONGLONG> g_sgLatestUserSleepEntryFileTime{epoch},g_sgLatestSleepEntryFileTime{0},g_sgOriginalPowerButtonWakeFileTime{0},g_sgClosedSleepIntentFileTime{0};
static std::atomic<ULONGLONG> g_sgLastKernel506EventFileTime{0};
static std::atomic<ULONGLONG> g_sgPauseWorkCompletedGeneration{771};
static std::atomic<ymcc::sleeprequest::Mode> g_sgAutomaticSleepTarget{ymcc::sleeprequest::Mode::None};
static ymcc::sleeprepair::Budget g_sgAutomaticSleepBudget{3};
enum class PowerLifecycle {Ready,Suspending,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Suspending};
static struct {SgRetryKind retryKind=SgRetryKind::None;unsigned retryAttempt=0;ULONGLONG retryEntryFileTime=0,generation=771;SgSleepMode mode=SgSleepMode::S3;bool unexpectedWakeConsumed=false;} g_sgTask;
static struct {SgRetryKind kind=SgRetryKind::None;ULONGLONG dispatchTick=0,dispatchFileTime=0;} g_sgInternalSleepRequest;
static ULONGLONG fixtureFileTime=epoch,fixtureTick=100000,fixtureGeneration=771,g_sgRetryDueTick=0,g_sgSleepTriggerTick=1,g_sgModernWakeClassifiedTick=0,g_sgModernStandbyGeneration=0;
static ULONGLONG g_entryFailureInterruptedAt=0;
static ULONGLONG g_entryFailureDisplayOffFileTime=0,g_entryFailureNewOffFileTime=0;
static ULONGLONG g_entryFailureQuickDeadline=0;
static bool g_entryFailureExpanded=false;
static int g_sgLastS0WakeReason=-1;
static double g_sgSleepTriggerEpoch=0;
static bool timerAvailable=true,activeSchemeKnown=true,statusKnown=true,acKnown=true,dcKnown=true,capabilitiesKnown=true,modern=true,classic=true,apiAccepted=true;
static BYTE acLine=1;
static ULONG acAction=1,dcAction=1;
static DWORD lastError=0,apiError=ERROR_ACCESS_DENIED;
static bool privilegeDenied=false;
static unsigned timerWrites=0,timerKills=0,powerReads=0,capabilityReads=0,apiCalls=0,finishes=0,aborts=0,markerClears=0,boundaryCalls=0,monitorCalls=0;
static std::map<UINT_PTR,ULONGLONG> timers;
static std::vector<std::array<BOOLEAN,3>> apiArguments;
static std::vector<std::pair<std::string,json>> facts;
static std::string throwLogEvent;
static std::function<void()> afterPowerRead,afterPrivilege;
static std::function<void(const char*)> afterLog;
static json scenarios=json::array();
static GUID buttonGroup{1,0,0,{}},buttonAction{2,0,0,{}},activeScheme{3,0,0,{}};
static ULONGLONG currentPowerGeneration(){return fixtureGeneration;}
static ULONGLONG sgNowFileTime(){return fixtureFileTime;}
static double sgNowEpoch(){return static_cast<double>(fixtureFileTime-epoch)/1e7;}
static ULONGLONG clockTick(){return fixtureTick;}
static void sgSleepTimeoutStop(const char*) noexcept;
static bool sgSleepTimeoutDispatchPermitted();
static bool sgSleepTimeoutCurrent();
static void sgSleepTimeoutArm(int,ULONGLONG);
// Shared publication/classification effects are outside this confirmed506 companion.
static void revokeSharedPowerMirrors() {}
static void sgBlockSharedWakeClassification(ULONGLONG) {}
static void sgStopResumeClassificationWait(const char*) {}
#ifdef YMCC_SLEEP_LATE_WAKE_ENABLE
// No classification owner is created by these late507/sender cases.
// Entry observation/discovery and the complete classifier run in the joined consumer.
static void sgCompleteSharedWakeClassification(ULONGLONG) {}
static bool sgResumeClassificationOwned(ULONGLONG) {return false;}
static void sgFinishResumeClassification(const char*,ULONGLONG) {throw std::runtime_error("unselected classification owner");}
static bool sgEntryFailureLifecycleAllowed() {throw std::runtime_error("unselected entry lifecycle");}
#endif
// No recovery worker is created by this confirmed506 companion.
static void sgCancelRetryAdmission(SgRetryKind) {}
static void sgDeferRetryAdmission(SgRetryKind,ULONGLONG,ymcc::sleeprequest::CompletionWitness) {throw std::runtime_error("unselected worker admission deferral");}
static bool sgResumeClassificationHeld(ULONGLONG) {return false;}
// This companion never creates a raw/classified entry-observation episode.
// Existing explicit EntryFailure sender tests do not grant such a ticket.
// Full observation predicates/handoff execute only in the actual joined leaf.
[[maybe_unused]] static bool sgResumeEntryObservationPresent(ULONGLONG) {return false;}
[[maybe_unused]] static bool sgResumeEntryObservationHeld(ULONGLONG) {return false;}
[[maybe_unused]] static bool sgJoinResumeEntryObservation() {return false;}
[[maybe_unused]] static void sgResumeEntryObservationRememberExit(int,ULONGLONG) {}
[[maybe_unused]] static void sgResumeEntryObservationInterrupt(int,ULONGLONG,ULONGLONG) {throw std::runtime_error("unselected entry-observation interruption");}
static bool sgSleepTimeoutDeferClassification();
static bool sgSleepTimeoutSuccessAfter(ULONGLONG);
static ULONGLONG sgSleepTimeoutSuccessfulExitTime();
static ULONGLONG sgSleepTimeoutOriginalIntent();
static void sgStartSleepRetry(SgRetryKind,const char*,const ymcc::sleeprequest::CompletionWitness* originalSuccess=nullptr);
static bool sgKernelPowerWakePredatesLatestEntry(int,ULONGLONG,ULONGLONG*);
static void sgObserveUnattributedSleepRetryFailure(const char*,int=-1,ULONGLONG=0);
static void sgScheduleNextSleepRetry(const char*);
static bool sgSleepRetryActive(){return g_sgRetryInProgress&&g_sgTask.retryKind!=SgRetryKind::None;}
static bool sgModernStandbyRepairSupported(){return modern;}
#ifdef YMCC_SLEEP_LATE_WAKE_ENABLE
static bool sgNonUserWakeGuardEnabled();
static bool sgExternalDeviceWakeIntentAge(ULONGLONG&);
static bool sgObserveNonUserWake(const char*);
static void sgEvaluateExternalDeviceWake();
static void sgClearUnexpectedWake(const char*,bool);

static std::atomic<ULONGLONG> g_sgLastKernel507EventFileTime{0};
static std::atomic<int> g_sgLastKernel506Reason{1};
static ULONGLONG g_sgIgnoredEarlyWakeFileTime=0,g_sgLastS0WakeTick=0,g_sgLastPowerButtonWakeTick=0;
// Legacy fact-monitor preference is extracted from main, not a high-frequency gate.
static unsigned uiRecoveryCalls=0;
static void sgAbnormalStop(const char*){}
static void sgStopMonitorEntryFailureTimer(const char*){}
static void sgHandleModernStandbyWake(bool,ULONGLONG=0){++uiRecoveryCalls;}
static void sleepRecoveryAutoArm(ULONGLONG){++uiRecoveryCalls;}
static void sgReportSleepDisplayRelight() noexcept {} // UI diagnostics outside this fixture.
static const char* sgKernelPowerReasonName(int){return "inert-reason-label";}
#else
static bool sgNonUserWakeGuardEnabled(){return false;}
static bool sgExternalDeviceWakeIntentAge(ULONGLONG& age){age=0;return false;}
#endif
static bool sgAbnormalPermitted(){return false;}
static bool sgAbnormalDispatchReady(){return false;}
static bool sgAbnormalObserveOwnedRetry(ULONGLONG,ULONGLONG){return false;}
static void sgAbnormalOwnedRetryResult(ULONGLONG,ULONGLONG,bool){}
static json sgSleepEnvironmentSnapshot(){return {{"port","inert-environment"}};}
static void traceLog(const char*,...){}
static void sgRecordFact(const char* event,const json& details){
    facts.emplace_back(event,details);if(afterLog)afterLog(event);
    if(throwLogEvent==event)throw std::runtime_error("inert-log-write-failure");
}
static void sgFinishSleepRetryFailure(const char*,const SgSleepRetryFailureScope* = nullptr){++finishes;g_sgRetryInProgress=false;g_sgRetryDueTick=0;timers.erase(retryTimerId);}
static void sgAbortSleepIntent(const char* reason, ULONGLONG){++aborts;g_sgRetryInProgress=false;sgSleepTimeoutStop(reason);}
static void sgCancelEntryFailureRetry(const char*,SgRetryKind only=SgRetryKind::EntryFailure,bool=false){if(g_sgTask.retryKind==only){g_sgRetryInProgress=false;timers.erase(retryTimerId);}}
#ifndef YMCC_SLEEP_LATE_WAKE_ENABLE
static void sgClearUnexpectedWake(const char*,bool){}
#endif
static void sgClearInternalSleepRequest(const char*){++markerClears;g_sgInternalSleepRequest={};}
static std::string sgFormatFactFileTime(ULONGLONG time){return std::to_string(time);}
static bool sgPrepareSleepPrivilege(SgSystemSleepRequestResult& result){result.privilegeRequired=true;result.shutdownPrivilegeEnabled=!privilegeDenied;result.shutdownPrivilegeError=privilegeDenied?ERROR_NOT_ALL_ASSIGNED:ERROR_SUCCESS;const auto hook=std::move(afterPrivilege);afterPrivilege=nullptr;if(hook)hook();return result.shutdownPrivilegeEnabled;}
static void stopPowerResumeWatchdog(){}
static void closeHardwareWriteGate(const char*){}
static struct FanBoundaryPort {void retryBoundary(ULONGLONG,ULONGLONG){++boundaryCalls;}} g_fanPhysicalWake;
static BOOL killTimer(HWND,UINT_PTR id){++timerKills;timers.erase(id);return TRUE;}
static UINT_PTR setTimer(HWND,UINT_PTR id,UINT delay,TIMERPROC){++timerWrites;check(id==timeoutTimerId||id==retryTimerId,"timeout uses only original timeout/retry timer IDs");if(!timerAvailable)return 0;timers[id]=fixtureTick+delay;return id;}
static DWORD activePowerScheme(void*,GUID** value){++powerReads;if(!activeSchemeKnown)return ERROR_ACCESS_DENIED;*value=&activeScheme;return ERROR_SUCCESS;}
static DWORD readAction(void*,const GUID* scheme,const GUID* subgroup,const GUID* setting,ULONG* value,bool ac){
    check(scheme==&activeScheme&&subgroup==&buttonGroup&&setting==&buttonAction,"actual action reader uses system button subgroup/current plan");
    *value=ac?acAction:dcAction;return (ac?acKnown:dcKnown)?ERROR_SUCCESS:ERROR_ACCESS_DENIED;
}
static DWORD readAc(void* h,const GUID* p,const GUID* g,const GUID* s,ULONG* v){return readAction(h,p,g,s,v,true);}
static DWORD readDc(void* h,const GUID* p,const GUID* g,const GUID* s,ULONG* v){return readAction(h,p,g,s,v,false);}
static HLOCAL freeScheme(HLOCAL p){check(p==&activeScheme,"action reader frees only inert active scheme");return nullptr;}
static BOOL powerStatus(SYSTEM_POWER_STATUS* value){value->ACLineStatus=acLine;const auto hook=std::move(afterPowerRead);afterPowerRead=nullptr;if(hook)hook();return statusKnown?TRUE:FALSE;}
static BOOLEAN powerCapabilities(SYSTEM_POWER_CAPABILITIES* value){++capabilityReads;*value={};value->AoAc=modern?TRUE:FALSE;value->SystemS3=classic?TRUE:FALSE;value->SystemS4=TRUE;value->HiberFilePresent=TRUE;return capabilitiesKnown?TRUE:FALSE;}
static void setError(DWORD value){lastError=value;}
static DWORD getError(){return lastError;}
static BOOLEAN suspend(BOOLEAN hibernate,BOOLEAN force,BOOLEAN disableWake){++apiCalls;apiArguments.push_back({hibernate,force,disableWake});lastError=apiError;return apiAccepted?TRUE:FALSE;}
[[maybe_unused]] static LRESULT monitorCommand(HWND,UINT message,WPARAM command,LPARAM value,UINT,UINT,PDWORD_PTR){check(message==WM_SYSCOMMAND&&command==SC_MONITORPOWER&&value==2,"negative sender uses only inert monitor-off boundary");++monitorCalls;return 1;}
#define SendMessageTimeoutW monitorCommand
#define SG_SLEEP_TIMEOUT_TIMER_ID timeoutTimerId
#define SG_RETRY_TIMER_ID retryTimerId
#define GUID_SYSTEM_BUTTON_SUBGROUP buttonGroup
#define GUID_POWERBUTTON_ACTION buttonAction
#define PowerGetActiveScheme activePowerScheme
#define PowerReadACValueIndex readAc
#define PowerReadDCValueIndex readDc
#define LocalFree freeScheme
#define GetSystemPowerStatus powerStatus
#define GetPwrCapabilities powerCapabilities
#define SetSuspendState suspend
#define GetTickCount64 clockTick
#define SetLastError setError
#define GetLastError getError
#define SetTimer setTimer
#define KillTimer killTimer
}
namespace ymcc::powerSource {
static BOOL read(SYSTEM_POWER_STATUS& status){return sleep_timeout_fixture::powerStatus(&status);}
}
namespace sleep_timeout_fixture {
// This companion selects the original confirmed506/action-policy cases only.
// PBT/query/worker execution is covered by the joined non-user consumer. An
// accidental discovery here is a hard refusal, never synthetic Reason1.
static unsigned unselectedTimeoutSourceCalls=0;
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* out,uint32_t) noexcept {
 *out={};out->generation=currentPowerGeneration();out->suspendSerial=1;
 out->observedNotificationSequence=g_sleepPowerDispatch.observedSequence();
 out->phase=static_cast<uint32_t>(g_powerLifecycle.load());out->flags=ymcc::sleeppower::kKnown;
 if(g_sleepPowerDispatch.pendingSuspend())out->flags|=ymcc::sleeppower::kNotificationPending;
 return 1;
}
static bool poolTrySubmitExisting(std::function<void()>) noexcept {++unselectedTimeoutSourceCalls;return false;}
static void sgRememberKernelSleepEntry(int,ULONGLONG){throw std::runtime_error("unselected original-source publication");}
#ifdef YMCC_SLEEP_LATE_WAKE_ENABLE
static void sgRefreshSleepTriggerOrigin();
#else
static void sgRefreshSleepTriggerOrigin(){throw std::runtime_error("unselected source refinement");}
#endif
static std::string W2U(const wchar_t*){throw std::runtime_error("unselected source render");}
static ULONGLONG sgKernelPowerEventFileTime(const std::string&){throw std::runtime_error("unselected source parse");}
static int sgKernelPowerEventId(const std::string&){throw std::runtime_error("unselected source parse");}
static int sgKernelPowerReason(const std::string&){throw std::runtime_error("unselected source parse");}
static EVT_HANDLE WINAPI unselectedEvtQuery(EVT_HANDLE,LPCWSTR,LPCWSTR,DWORD){++unselectedTimeoutSourceCalls;return nullptr;}
static BOOL WINAPI unselectedEvtNext(EVT_HANDLE,DWORD,PEVT_HANDLE,DWORD,DWORD,PDWORD){throw std::runtime_error("unselected query");}
static BOOL WINAPI unselectedEvtRender(EVT_HANDLE,EVT_HANDLE,DWORD,DWORD,PVOID,PDWORD,PDWORD){throw std::runtime_error("unselected render");}
static BOOL WINAPI unselectedEvtClose(EVT_HANDLE){throw std::runtime_error("unselected event handle");}
static BOOL unselectedTimeoutPost(HWND,UINT,WPARAM,LPARAM){throw std::runtime_error("unselected worker post");}

#include "production_timeout_bindings.inc"
#define EvtQuery unselectedEvtQuery
#define EvtNext unselectedEvtNext
#define EvtRender unselectedEvtRender
#define EvtClose unselectedEvtClose
#pragma push_macro("PostMessageW")
#undef PostMessageW
#define PostMessageW unselectedTimeoutPost
#include "sleep_timeout_native.inc"
#pragma pop_macro("PostMessageW")
#undef EvtQuery
#undef EvtNext
#undef EvtRender
#undef EvtClose
[[maybe_unused]] static const auto timeoutObservationCompileReference=&sgSleepTimeoutObserveSuspend;
[[maybe_unused]] static const auto timeoutHeldCompileReference=&sgSleepTimeoutObservationHeld;
[[maybe_unused]] static const auto timeoutClassificationCompileReference=&sgSleepTimeoutClassificationComplete;
[[maybe_unused]] static const auto timeoutSettleCompileReference=&sgSleepTimeoutSettleClassification;
[[maybe_unused]] static const auto timeoutReentryCompileReference=&sgSleepTimeoutStageReentry;
[[maybe_unused]] static const auto timeoutWakeOrderCompileReference=&sgKernelPowerWakePredatesLatestEntry;
[[maybe_unused]] static const auto timeoutReentryResumeCompileReference=&sgSleepTimeoutResumeDeferredReentry;
#ifdef YMCC_SLEEP_LATE_WAKE_ENABLE
#include "production_late_wake_bindings.inc"
static LRESULT s0WakeMessageNative(WPARAM w,LPARAM l){
#include "production_late_wake_message.inc"
}
static LRESULT s0WakeMessage(WPARAM,LPARAM reason){
    const auto time=reason==1?g_sgLastPowerButtonWakeEventFileTime.load():g_sgLastKernel507EventFileTime.load();
    return s0WakeMessageNative(static_cast<WPARAM>(reason),static_cast<LPARAM>(time?time:fixtureFileTime));
}
static void manualWakeCallback(ULONGLONG eventFileTime,[[maybe_unused]] ULONGLONG recordedFileTime,bool leaving=true,bool reason1=true){
#include "production_late_wake_manual_callback.inc"
}
#endif
#undef SendMessageTimeoutW
#undef SG_SLEEP_TIMEOUT_TIMER_ID
#undef SG_RETRY_TIMER_ID
#undef GUID_SYSTEM_BUTTON_SUBGROUP
#undef GUID_POWERBUTTON_ACTION
#undef PowerGetActiveScheme
#undef PowerReadACValueIndex
#undef PowerReadDCValueIndex
#undef LocalFree
#undef GetSystemPowerStatus
#undef GetPwrCapabilities
#undef SetSuspendState
#undef GetTickCount64
#undef SetLastError
#undef GetLastError
#undef SetTimer
#undef KillTimer
// Execute the exact current107 classification and original-time success call.
// The real callback ordering is independently bound before its first log/UI post.
static int sgKernelPowerEventDataInt(const std::string& xml,const char* field){check(std::string(field)=="TargetState","S4 callback reads actual TargetState field");return std::stoi(xml);}
static void systemWakeCallback(int eventId,int targetState,ULONGLONG eventFileTime,ULONGLONG recordedFileTime,int reason=-1){
    const bool leaving=eventId==507;
    const std::string xml=std::to_string(targetState);(void)recordedFileTime;
#include "production_timeout_s4_hook.inc"
}
static void at(ULONGLONG ms){fixtureFileTime=epoch+ms*10000;fixtureTick=100000+ms;}
static void wake(){g_sgLastPowerButtonWakeEventFileTime=fixtureFileTime;g_sgOriginalPowerButtonWakeFileTime=fixtureFileTime;}
static void reset(){
    g_entryFailureInterruptedAt=0;g_entryFailureDisplayOffFileTime=g_entryFailureNewOffFileTime=0;g_entryFailureQuickDeadline=0;g_entryFailureExpanded=false;g_sgLastS0WakeReason=-1;g_sgLastKernel506EventFileTime=0;
    afterLog=nullptr;afterPowerRead=nullptr;afterPrivilege=nullptr;throwLogEvent.clear();g_sgSleepTimeoutWindow={};g_sgKernelSystemWake=0;sgResetKernelPowerIntervalObservations();g_sgSleepTimeoutObservation={};g_sgSleepTimeoutGeneration=0;unselectedTimeoutSourceCalls=0;
    g_hwnd=reinterpret_cast<HWND>(1);g_guardEnabled=g_sgRetrySleepTimeout=true;g_sgRetryEntryFailure=g_sgResleepEnabled=false;g_sgRepairEligible=true;g_exitRequested=false;
    g_sgLastPowerButtonSleepIntentFileTime=g_sgLatestUserSleepEntryFileTime=epoch;g_sgLatestSleepEntryFileTime=0;g_sgLastPowerButtonWakeEventFileTime=g_sgOriginalPowerButtonWakeFileTime=g_sgClosedSleepIntentFileTime=0;
    g_sgTask={};g_sgTask.generation=771;g_sgTask.mode=SgSleepMode::S3;g_sgInternalSleepRequest={};g_sgRetryInProgress=false;fixtureGeneration=771;g_sgPauseWorkCompletedGeneration=771;
    g_sgAutomaticSleepBudget.confirmedUserWake();g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::None;g_sgRetryDueTick=0;g_sgSleepTriggerTick=1;g_sgSleepTriggerEpoch=0;g_powerLifecycle=PowerLifecycle::Suspending;
    timerAvailable=activeSchemeKnown=statusKnown=acKnown=dcKnown=capabilitiesKnown=modern=classic=apiAccepted=true;acLine=1;acAction=dcAction=1;apiError=ERROR_ACCESS_DENIED;lastError=0;
    timerWrites=timerKills=powerReads=capabilityReads=apiCalls=finishes=aborts=markerClears=boundaryCalls=monitorCalls=0;
#ifdef YMCC_SLEEP_LATE_WAKE_ENABLE
    g_sgNonUserWakeObserved=false;g_sgLastKernel507EventFileTime=0;g_sgLastKernel506Reason=1;g_sgIgnoredEarlyWakeFileTime=g_sgLastS0WakeTick=g_sgLastPowerButtonWakeTick=0;g_sgLastS0WakeReason=-1;g_sgFactMonitorEnabled=false;uiRecoveryCalls=0;
#endif
    timers.clear();apiArguments.clear();facts.clear();at(0);
    privilegeDenied=false;
}
static void arm(){sgSleepTimeoutArm(1,g_sgLastPowerButtonSleepIntentFileTime.load());}
static void deadline(){at(240000);sgSleepTimeoutTick();}
static void dispatch(){if(g_sgRetryDueTick>fixtureTick){const auto delta=g_sgRetryDueTick-fixtureTick;fixtureTick+=delta;fixtureFileTime+=delta*10000;}timers.erase(retryTimerId);sgDispatchSameModeRetry();}
static void completed(const std::string& fields,ULONGLONG eventTime,int reason=5,ULONGLONG generation=771){sgSleepTimeoutRecordKernel(507,reason,eventTime,generation);::productionCapture(true,"<Event><EventData>"+fields+"</EventData></Event>",eventTime,generation,reason);::captured.clear();}
static std::string windowsSession(ULONGLONG span,ULONGLONG slept,const char* status="true"){
    return "<Data Name=\"DurationInUs\">"+std::to_string(span)+"</Data><Data Name=\"SleepDurationInUs\">"+std::to_string(slept)+"</Data><Data Name=\"SleepEntered\">"+status+"</Data>";
}
static void originalEntry(ULONGLONG generation=771){sgSleepTimeoutRecordKernel(506,1,epoch,generation);}
template<class F> static void scenario(const std::string& name,F body){
    reset();const auto before=sleepEvidenceChecks;
    try{body();for(const auto& values:apiArguments)check(values==std::array<BOOLEAN,3>{FALSE,FALSE,FALSE},name+": actual shared sender arguments are tripleFALSE");for(const auto& fact:facts)if(fact.first=="sleep-request-route-result")check(fact.second.at("arguments")==json::array({false,false,false})&&fact.second.at("hibernate")==false,name+": final shared route receipt explicitly records tripleFALSE and no hibernate");}
    catch(...){std::cerr<<"TIMEOUT_SCENARIO: "<<name<<'\n';throw;}
    check(unselectedTimeoutSourceCalls==0,name+": confirmed timeout companion did not execute source discovery");
    check(sleepEvidenceChecks>before,name+": selected scenario executed assertions");
    scenarios.push_back({{"name",name},{"status","passed"},{"checks",sleepEvidenceChecks-before},{"mockSuspendCalls",apiCalls},{"timerWrites",timerWrites},{"powerPlanReads",powerReads},{"sharedBudgetConsumed",g_sgAutomaticSleepBudget.requests()}});
}
} // namespace sleep_timeout_fixture
static void sgSleepTimeoutObserveCompleted(const json& evidence,unsigned long long at,unsigned long long generation,int reason) noexcept {sleep_timeout_fixture::sgSleepTimeoutObserveCompleted(evidence,at,generation,reason);}
namespace sleep_timeout_fixture {
static unsigned runTests(){
    const auto initial=sleepEvidenceChecks;scenarios=json::array();
    check(g_sgRetrySleepTimeout,"actual main.cpp timeout default is true");
    scenario("239999ms early tick preserves original deadline",[]{arm();const auto first=timers.at(timeoutTimerId);at(239999);sgSleepTimeoutTick();check(g_sgSleepTimeoutWindow.waiting&&!apiCalls&&!sgSleepRetryActive()&&timers.at(timeoutTimerId)==first,"early tick rearms only remaining one millisecond");check(timerWrites==2,"early delivery has bounded single remaining timer");});
    scenario("240000ms enters actual shared sender once",[]{arm();deadline();check(!g_sgSleepTimeoutWindow.waiting&&g_sgSleepTimeoutWindow.requested&&sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::SleepTimeout&&!apiCalls,"deadline hands off to existing scheduler");dispatch();check(apiCalls==1&&g_sgAutomaticSleepBudget.requests()==1,"actual dispatcher charges shared budget exactly once");const auto writes=timerWrites;for(unsigned i=0;i<8;++i)sgSleepTimeoutTick();check(apiCalls==1&&timerWrites==writes&&!timers.count(timeoutTimerId),"consumed oneShot never polls or duplicates accepted request");});
    scenario("late original506 uses remaining time",[]{at(230000);arm();check(timers.at(timeoutTimerId)-fixtureTick==10000,"delivery latency does not restart240seconds");deadline();dispatch();check(apiCalls==1,"late original reaches original deadline");});
    scenario("already overdue506 waits at most one millisecond",[]{at(300000);arm();check(timers.at(timeoutTimerId)-fixtureTick==1,"overdue intent schedules oneShot not fresh240seconds");sgSleepTimeoutTick();dispatch();check(apiCalls==1,"overdue original sends once");});
    scenario("manual wake before arm",[]{wake();arm();check(!g_sgSleepTimeoutWindow.active()&&!timerWrites,"wake at original timestamp blocks arm");});
    scenario("manual wake during arm power read",[]{afterPowerRead=[]{wake();};arm();check(!g_sgSleepTimeoutWindow.active()&&!timerWrites,"callback-side wake after arm read blocks timer");});
    scenario("manual wake before deadline tick",[]{arm();at(240000);wake();sgSleepTimeoutTick();check(!g_sgSleepTimeoutWindow.active()&&!apiCalls&&!sgSleepRetryActive(),"tick respects manual wake");});
    scenario("manual wake after deadline power read",[]{arm();afterPowerRead=[]{wake();};deadline();check(!g_sgSleepTimeoutWindow.active()&&!apiCalls&&!sgSleepRetryActive(),"post-read wake rechecked");});
    scenario("manual wake after deadline fact log",[]{arm();afterLog=[](const char* event){if(std::string(event)=="sleep-timeout-due")wake();};deadline();check(!apiCalls&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active(),"post-log wake vetoes handoff");});
    const auto routeAttempt=[]() -> const json& {
        for (const auto& [event,details]:facts)
            if(event=="sleep-request-route-result")return details.at("attempts").at(0);
        throw std::runtime_error("missing sleep route result");
    };
    scenario("manual wake after actual sender dispatch log",[&]{
        arm();deadline();afterLog=[](const char* event){if(std::string(event)=="sleep-request-dispatch")wake();};dispatch();
        check(!apiCalls,"actual sender final callback-side cancellation wins");
        const auto& attempt=routeAttempt();
        check(attempt.at("invoked")==true && attempt.at("error")==ERROR_CANCELLED,"existing invoked field reports adapter entry even after final veto");
        check(attempt.contains("apiCall") && attempt.at("apiCall").is_null(),"post-log cancellation has no Windows API receipt");
    });
    scenario("privilege rejection has no Windows API receipt",[&]{
        arm();deadline();privilegeDenied=true;dispatch();
        const auto& attempt=routeAttempt();
        check(!apiCalls && attempt.at("invoked")==true && attempt.at("error")==ERROR_NOT_ALL_ASSIGNED && attempt.at("apiCall").is_null(),"privilege rejection is an adapter result only");
    });
    for (const bool accepted:{false,true}) scenario(accepted?"Windows API acceptance receipt remains pending confirmation":"Windows API failure receipt preserves raw error",[&]{
        arm();deadline();apiAccepted=accepted;apiError=ERROR_ACCESS_DENIED;dispatch();
        const auto& attempt=routeAttempt();const auto& call=attempt.at("apiCall");
        check(apiCalls==1 && call.at("accepted")==accepted && call.at("win32Error")== (accepted?ERROR_SUCCESS:ERROR_ACCESS_DENIED),"receipt describes the call and its Windows return");
        check(call.at("preflightFileTime")==fixtureFileTime && call.at("returnedFileTime")==fixtureFileTime,"receipt preserves the call time bounds");
        check(attempt.at("outcome")== (accepted?"accepted-pending-confirmation":"failed"),"Windows return is not sleep confirmation");
    });
    scenario("manual wake during actual sender action read",[]{arm();deadline();afterPowerRead=[]{wake();};dispatch();check(!apiCalls,"actual dispatcher rejects wake during final applicable-plan read");});
    scenario("duplicate and old506 cannot reset deadline",[]{arm();const auto due=timers.at(timeoutTimerId);at(90000);arm();sgSleepTimeoutArm(1,epoch-10000);check(timerWrites==1&&timers.at(timeoutTimerId)==due&&g_sgSleepTimeoutWindow.origin==epoch,"same and older event cannot refill timer");sgSleepTimeoutStop("test-retire");arm();check(!g_sgSleepTimeoutWindow.active()&&timerWrites==1,"retired original cannot rearm");});
    scenario("old nonuser guard clears marker without losing240deadline",[]{arm();at(30000);g_sgLastPowerButtonSleepIntentFileTime=0;check(sgSleepTimeoutOriginalIntent()==epoch,"independent window retains original506 identity");deadline();dispatch();check(apiCalls==1&&g_sgAutomaticSleepBudget.requests()==1,"cleared old guard marker still reaches actual shared sender once");});
    scenario("different nonzero new marker vetoes old deadline",[]{arm();at(30000);g_sgLastPowerButtonSleepIntentFileTime=fixtureFileTime;deadline();check(!apiCalls&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active(),"new foreign original marker stops old window");});
    scenario("manual wake after old guard marker cleared still vetoes",[]{arm();at(30000);g_sgLastPowerButtonSleepIntentFileTime=0;wake();deadline();check(!apiCalls&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active(),"independent original manual-wake anchor survives inherited marker retirement");});
    scenario("new original intent owns new deadline",[]{arm();at(10000);g_sgLastPowerButtonSleepIntentFileTime=g_sgLatestUserSleepEntryFileTime=fixtureFileTime;arm();check(g_sgSleepTimeoutWindow.origin==epoch+10000*10000ULL&&timers.at(timeoutTimerId)==fixtureTick+240000,"strictly newer original arms new window");at(240000);sgSleepTimeoutTick();check(!sgSleepRetryActive()&&g_sgSleepTimeoutWindow.waiting,"stale old timer delivery cannot accelerate new intent");});
    for(const BYTE supply:{BYTE{1},BYTE{0}})for(const ULONG action:{1UL,2UL,4UL,0UL,99UL})scenario(std::string(supply?"AC":"DC")+" applicable action "+std::to_string(action),[=]{acLine=supply;acAction=supply?action:2;dcAction=supply?2:action;arm();check(g_sgSleepTimeoutWindow.active()==(action==1),"only current supply Sleep may arm; opposite supply cannot authorize");if(action==1){deadline();dispatch();check(apiCalls==1,"selected supply Sleep uses actual sender");}else check(!timerWrites&&!apiCalls,"S4 ScreenOff None unknown never dispatch");});
    for(const ULONG action:{2UL,4UL,0UL,99UL})scenario("action changes before deadline "+std::to_string(action),[=]{arm();acAction=action;deadline();check(!sgSleepRetryActive()&&!apiCalls&&!g_sgSleepTimeoutWindow.active(),"deadline rechecks current action");});
    scenario("AC to DC switch rechecks applicable action",[]{acAction=1;dcAction=4;arm();acLine=0;deadline();check(!apiCalls&&!sgSleepRetryActive(),"new DC screenoff cannot inherit AC Sleep permission");});
    scenario("unknown current plan or action refused",[]{activeSchemeKnown=false;arm();check(!timerWrites,"unknown current scheme refuses");activeSchemeKnown=true;acKnown=false;arm();check(!timerWrites,"unreadable selected action refuses");acKnown=true;statusKnown=false;acAction=1;dcAction=2;arm();check(!timerWrites,"unknown supply with conflicting actions refuses");});
    scenario("missing original506 timestamp highwater refuses collection-time substitute",[]{g_sgLatestUserSleepEntryFileTime=0;arm();check(!g_sgSleepTimeoutWindow.active()&&!timerWrites,"absent raw506 timestamp cannot be replaced by callback collection time");g_sgLatestUserSleepEntryFileTime=epoch-1;arm();check(!g_sgSleepTimeoutWindow.active()&&!timerWrites,"different latest original timestamp cannot authorize current substitute");});
    scenario("nonReason1 and missing original506 do not arm",[]{for(int reason:{0,2,3,4,7})sgSleepTimeoutArm(reason,epoch);sgSleepTimeoutArm(1,0);sgSleepTimeoutArm(1,epoch-1);check(!timerWrites&&!g_sgSleepTimeoutWindow.active(),"idle lid programmatic and noncurrent original are not human button evidence");sgSleepTimeoutPowerBroadcast(PBT_APMSUSPEND);sgSleepTimeoutTick();check(!timerWrites&&!apiCalls,"ordinary PBT without original506 cannot manufacture timeout");});
    for(unsigned veto=0;veto<3;++veto)scenario("OFF guard or exit at arm "+std::to_string(veto),[=]{if(veto==0)g_sgRetrySleepTimeout=false;if(veto==1)g_guardEnabled=false;if(veto==2)g_exitRequested=true;arm();check(!timerWrites&&!apiCalls,"disabled or exiting cannot arm");});
    for(unsigned veto=0;veto<3;++veto)scenario("OFF guard or exit at tick "+std::to_string(veto),[=]{arm();if(veto==0)g_sgRetrySleepTimeout=false;if(veto==1)g_guardEnabled=false;if(veto==2)g_exitRequested=true;deadline();check(!apiCalls&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active(),"disabled or exiting cannot dispatch");});
    scenario("S4 original107 callback vetoes before queued UIwake",[]{arm();at(239999);systemWakeCallback(107,5,fixtureFileTime,fixtureFileTime);check(g_sgSleepTimeoutWindow.waiting&&sgSleepTimeoutSuccessfulExitTime()==fixtureFileTime,"callback publishes success while UI Stop has not executed");deadline();check(!apiCalls&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active(),"timer delivered before S4 UIwake cannot send after callback success");});
    scenario("old S4 timestamp cannot cancel newer original intent",[]{arm();at(239999);systemWakeCallback(107,5,epoch-1,fixtureFileTime);check(sgSleepTimeoutSuccessfulExitTime()==epoch-1&&g_sgSleepTimeoutWindow.waiting,"delayed107 retains original old timestamp not collection-time replacement");deadline();dispatch();check(apiCalls==1,"old S4 callback cannot cancel new original506 deadline");});
    scenario("S4 missing original time never uses callback collection time",[]{arm();at(239999);systemWakeCallback(107,5,0,fixtureFileTime);check(sgSleepTimeoutSuccessfulExitTime()==0,"missing original107 time cannot create success witness");deadline();dispatch();check(apiCalls==1,"collection-time fallback never vetoes elapsed intent");});
    scenario("nonS4 event107 and other event IDs cannot publish S4success",[]{arm();systemWakeCallback(107,4,epoch+1,epoch+1);systemWakeCallback(507,5,epoch+1,epoch+1);check(sgSleepTimeoutSuccessfulExitTime()==0,"only actual107 TargetState5 is system S4success");});
    scenario("positive507 success veto before arm",[]{at(1000);completed(windowsSession(1000000,990000),fixtureFileTime);check(!sgSleepTimeoutSuccessAfter(epoch),"507 alone does not settle the button intent");originalEntry(770);arm();check(!timerWrites&&sgSleepTimeoutSuccessAfter(epoch),"late 506 joins its original 507 across the adjacent generation before first arm");});
    scenario("positive507 success veto while waiting",[]{originalEntry(770);arm();at(3288620);completed(windowsSession(3286870421ULL,3286825339ULL)+"<Data Name=\"DripsResidencyInUs\">3219233990</Data>",epoch+32868694796ULL);sgSleepTimeoutTick();check(!apiCalls&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active(),"Win5 completed 507 retires the exact overdue fallback");});
    scenario("positive507 after due log vetoes",[]{originalEntry();arm();afterLog=[](const char* event){if(std::string(event)=="sleep-timeout-due"){const auto us=(fixtureFileTime-epoch)/10;completed(windowsSession(us,us-1),fixtureFileTime);}};deadline();check(!apiCalls&&!sgSleepRetryActive(),"late completion before handoff wins");});
    scenario("positive507 after actual dispatch log vetoes",[]{originalEntry();arm();deadline();afterLog=[](const char* event){if(std::string(event)=="sleep-request-dispatch"){const auto us=(fixtureFileTime-epoch)/10;completed(windowsSession(us,us-1),fixtureFileTime);}};dispatch();check(!apiCalls,"real sender repeats the completed-session veto at the final API boundary");});
    scenario("doublezero507 is neither failure nor success",[]{originalEntry();arm();at(1000);completed("<Data Name=\"DurationInUs\">1000000</Data><Data Name=\"DripsResidencyInUs\">0</Data><Data Name=\"HwDripsResidencyInUs\">0</Data><Data Name=\"SleepEntered\">true</Data>",fixtureFileTime);check(!apiCalls&&g_sgSleepTimeoutWindow.waiting&&!sgSleepTimeoutSuccessAfter(epoch),"DRIPS and SleepEntered without sleep duration are insufficient");deadline();dispatch();check(apiCalls==1,"elapsed policy remains independent of DRIPS");});
    scenario("Windows completed session with zero DRIPS",[]{originalEntry();arm();at(1000);completed(windowsSession(1000000,990000)+"<Data Name=\"DripsResidencyInUs\">0</Data><Data Name=\"HwDripsResidencyInUs\">0</Data>",fixtureFileTime);deadline();dispatch();check(!apiCalls&&!g_sgSleepTimeoutWindow.active(),"explicit Windows completed sleep is independent of zero DRIPS");});
    scenario("Intel G3E continuing sleep then separate screen-off failure",[]{
        // 2026-11-22 G3E export, sleep-facts.log lines 818, 848, 849, 870.
        originalEntry();arm();at(7220392);
        const auto continuingAt=fixtureFileTime;
        const auto continuing=windowsSession(7220391475ULL,7220340182ULL)+
            "<Data Name=\"IsCsSessionInProgressOnExit\">true</Data>";
        const auto continuingXml="<Event><EventData>"+continuing+"</EventData></Event>";
        sgSleepTimeoutRecordKernel(507,55,continuingAt,771);
        check(!::productionCapture(true,continuingXml,continuingAt,771,55),
            "G3E continuing 507 cannot be forwarded as an exit");
        check(::captured.size()==1&&::captured.front().second.at("windowsEntryOutcome")=="entered",
            "G3E continuing 507 preserves the Windows sleep evidence");
        ::captured.clear();
        check(sgSleepTimeoutSuccessAfter(epoch),"G3E continuing 507 denies retry of the matched original intent");
        check(!g_sgKernelCycles.latestCompletedAfter(epoch,continuingAt,55,771,SG_S0_ENTRY_FAILURE_WINDOW_MS),
            "G3E continuing 507 is not a completed exit for the in-flight retry branch");
        at(7220393);const auto laterEntry=fixtureFileTime;
        sgSleepTimeoutRecordKernel(506,55,laterEntry,771);
        at(7225476);
        const auto noEntry=windowsSession(5083507,0,"false")+
            "<Data Name=\"IsCsSessionInProgressOnExit\">false</Data>";
        sgSleepTimeoutRecordKernel(507,32,fixtureFileTime,771);
        check(::productionCapture(true,"<Event><EventData>"+noEntry+"</EventData></Event>",fixtureFileTime,771,32),
            "G3E second 507 is forwarded as a real exit from the screen-off phase");
        check(::captured.size()==1&&::captured.front().second.at("windowsEntryOutcome")=="not-entered",
            "G3E screen-off-only 507 explicitly reports no sleep in the ordinary export");
        ::captured.clear();
        check(sgSleepTimeoutSuccessAfter(epoch)&&!sgSleepTimeoutSuccessAfter(laterEntry),
            "G3E later no-entry does not erase original entry or borrow its success");
        sgSleepTimeoutTick();check(!apiCalls&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active(),
            "G3E evidence does not authorize a new request for a non-button entry");
    });
    scenario("Continuing bit preserves manual and hibernate arbitration",[]{
        const auto xml="<Event><EventData>"+windowsSession(1000000,990000)+
            "<Data Name=\"IsCsSessionInProgressOnExit\">true</Data></EventData></Event>";
        at(1000);
        for(const int reason:{1,20}) {
            check(::productionCapture(true,xml,fixtureFileTime,771,reason),
                "explicit human wake and Windows hibernate handoff still reach the original classifier");
            ::captured.clear();
        }
    });
    scenario("Windows entry accounting stays unknown for contradictory or missing fields",[]{
        for(const auto& fields:{windowsSession(5000000,0),windowsSession(5000000,10,"false"),
            windowsSession(5000000,5000001),windowsSession(5000000,0,"false")+"<Data Name=\"SleepEntered\">true</Data>",
            std::string("<Data Name=\"SleepEntered\">false</Data>")}) {
            const auto evidence=ymcc::sleep_residency::makeEvidence("<Event><EventData>"+fields+"</EventData></Event>");
            check(evidence.at("windowsEntryOutcome")=="unknown","missing or contradictory Windows accounting is unknown");
        }
    });
    scenario("missing malformed and old507 cannot veto new intent",[]{originalEntry();arm();at(1000);completed(windowsSession(1000000,0,"false"),fixtureFileTime);at(2000);completed(windowsSession(100,101),fixtureFileTime);at(3000);completed(windowsSession(1000000,990000),epoch-1);at(4000);completed(windowsSession(4000000,3900000)+"<Data Name=\"SleepEntered\">false</Data>",fixtureFileTime);deadline();dispatch();check(apiCalls==1,"explicit no-entry, incompatible clocks, duplicate flag and old timestamps cannot fabricate completion");});
    scenario("positive witness highwater cannot roll back",[]{originalEntry();at(4000);completed(windowsSession(4000000,3950000),fixtureFileTime);completed(windowsSession(2000000,1900000),epoch+20000000);check(sgSleepTimeoutSuccessAfter(epoch)&&!sgSleepTimeoutSuccessAfter(epoch+1),"old callback cannot erase or broaden the exact settled intent");});
    scenario("unrelated later session cannot settle old intent",[]{originalEntry();arm();at(230000);sgSleepTimeoutRecordKernel(506,3,fixtureFileTime,772);at(239000);completed(windowsSession(9000000,8900000),fixtureFileTime,5,772);check(!sgSleepTimeoutSuccessAfter(epoch),"future completion is not yet current");fixtureGeneration=772;check(!sgSleepTimeoutSuccessAfter(epoch)&&sgSleepTimeoutSuccessAfter(epoch+2300000000ULL),"later entry and completion cannot be borrowed by the old intent");g_sgLatestSleepEntryFileTime=epoch+2300000000ULL;deadline();dispatch();check(!apiCalls,"original sender cancels when it sees a newer entry");});
    scenario("other generation and unrelated duration are not completion",[]{originalEntry();arm();at(2000);completed(windowsSession(2000000,1900000),fixtureFileTime,5,773);at(10000);completed(windowsSession(1000000,990000),fixtureFileTime,5,771);check(!sgSleepTimeoutSuccessAfter(epoch),"different generation or duration cannot settle the original button intent");deadline();dispatch();check(apiCalls==1,"unknown evidence preserves the four-minute path");});
    scenario("arm timer registration fails closed",[]{timerAvailable=false;arm();check(!g_sgSleepTimeoutWindow.active()&&!apiCalls,"failed SetTimer retires window");timerAvailable=true;arm();check(!g_sgSleepTimeoutWindow.active()&&timerWrites==1,"same intent never retries failed registration");});
    scenario("early tick rearm timer fails closed",[]{arm();at(239999);timerAvailable=false;sgSleepTimeoutTick();check(!g_sgSleepTimeoutWindow.active()&&!apiCalls,"remaining timer registration failure stops");});
    scenario("missing window fails closed",[]{g_hwnd=nullptr;arm();check(!g_sgSleepTimeoutWindow.active()&&!timerWrites,"no HWND never creates timer or power request");});
    scenario("shared budget exhausted never refills",[]{for(unsigned i=0;i<3;++i)check(g_sgAutomaticSleepBudget.consume(),"preexisting request consumes original budget");arm();deadline();check(!apiCalls&&!sgSleepRetryActive()&&g_sgAutomaticSleepBudget.requests()==3,"timeout cannot refill or fork exhausted budget");});
    const auto startEntryRetry=[](ULONGLONG ms){
        originalEntry();arm();at(ms);g_sgRetryEntryFailure=true;
        sgStartSleepRetry(SgRetryKind::EntryFailure,"test-original-entry-retry");
        check(sgSleepRetryActive()&&g_sgRetryDueTick&&g_sgAutomaticSleepBudget.requests()==0,
            "entry retry starts through production admission without charging until dispatch");
    };
    const auto sendAcceptedEntryRetry=[&]{
        startEntryRetry(10);dispatch();
        check(apiCalls==1&&g_sgTask.retryAttempt==1&&g_sgAutomaticSleepBudget.requests()==1&&
            !g_sgRetryDueTick&&g_sgInternalSleepRequest.kind==SgRetryKind::EntryFailure,
            "accepted Windows return leaves the real retry in flight without a retry timer");
        const auto route=std::find_if(facts.rbegin(),facts.rend(),[](const auto& fact){return fact.first=="sleep-request-route-result";});
        check(route!=facts.rend()&&route->second.at("attempts").at(0).at("apiCall").at("accepted")==true,
            "Windows mock acceptance is recorded by the real sender");
    };
    scenario("elapsed deadline joins the unissued first retry",[&]{
        startEntryRetry(239900);
        const auto due=g_sgRetryDueTick;const auto boundaries=boundaryCalls;
        check(due&&!apiCalls,"original retry has a deadline but has not sent");
        deadline();check(g_sgTask.retryKind==SgRetryKind::SleepTimeout&&g_sgRetryDueTick==due&&boundaryCalls==boundaries&&
            !g_sgTask.retryAttempt&&!g_sgAutomaticSleepBudget.requests(),"elapsed joins the original timer without dispatch or new owner");
        dispatch();check(apiCalls==1&&g_sgTask.retryAttempt==1&&g_sgAutomaticSleepBudget.requests()==1,"joined timer still owns the first send");
    });
    scenario("accepted entry request at the original elapsed deadline",[&]{
        sendAcceptedEntryRetry();const auto previousClears=markerClears;
        const auto marker=g_sgInternalSleepRequest.dispatchFileTime;deadline();
        check(apiCalls==1&&g_sgTask.retryAttempt==1&&g_sgAutomaticSleepBudget.requests()==1&&
            g_sgTask.retryKind==SgRetryKind::EntryFailure&&!g_sgRetryDueTick&&markerClears==previousClears&&
            g_sgInternalSleepRequest.dispatchFileTime==marker&&g_sgSleepTimeoutWindow.waiting&&!g_sgSleepTimeoutWindow.requested,
            "original elapsed fallback cannot replace the accepted marker or consume its window");
        const auto fact=std::find_if(facts.rbegin(),facts.rend(),[](const auto& row){return row.first=="sleep-retry-failure-attribution";});
        check(fact!=facts.rend()&&fact->second.at("attribution")=="unknown","elapsed policy records unknown attribution");
        sgDispatchSameModeRetry();check(apiCalls==1&&g_sgAutomaticSleepBudget.requests()==1&&!g_sgRetryDueTick,
            "elapsed callback and stale retry dispatch cannot charge a second slot");
    });
    scenario("manual wake or newer entry or confirmed sleep vetoes elapsed dispatch after acceptance",[&]{
        for(const int veto:{0,1,2}){
            reset();sendAcceptedEntryRetry();at(239999);
            if(veto==0)wake();
            if(veto==1){++fixtureGeneration;g_sgLatestSleepEntryFileTime=fixtureFileTime;sgSleepTimeoutRecordKernel(506,3,fixtureFileTime,fixtureGeneration);}
            if(veto==2){const auto span=(fixtureFileTime-epoch)/10;completed(windowsSession(span,span-1),fixtureFileTime);}
            deadline();dispatch();
            check(apiCalls==1&&!g_sgSleepTimeoutWindow.active()&&!g_sgRetryDueTick&&
                g_sgAutomaticSleepBudget.requests()==1,"veto stops the elapsed dispatch without refunding the charged slot");
        }
    });
    scenario("public suspend ingress during elapsed logging preserves the accepted entry owner",[&]{
        sendAcceptedEntryRetry();const auto previousClears=markerClears;
        const auto markerTime=g_sgInternalSleepRequest.dispatchFileTime;
        ULONGLONG sequence=0;
        afterLog=[&](const char* name){
            if(std::string(name)!="sleep-retry-failure-attribution")return;
            check(g_sleepPowerDispatch.callback(PBT_APMSUSPEND,fixtureTick,[&](const auto& receipt){sequence=receipt.sequence;return true;},fixtureFileTime),
                "public ingress accepts new Suspend before UI delivery");
        };
        deadline();
        const auto pending=g_sleepPowerDispatch.pendingSuspend();
        const auto consumed=g_sleepPowerDispatch.forwarded(sequence,PBT_APMSUSPEND,fixtureTick);
        g_sleepPowerDispatch.complete(consumed,fixtureTick);
        check(pending&&consumed.sequence==sequence&&sequence,"newer public Suspend is still available to its original handler");
        check(!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.requested&&
            g_sgInternalSleepRequest.kind==SgRetryKind::EntryFailure&&g_sgInternalSleepRequest.dispatchFileTime==markerTime&&
            markerClears==previousClears&&!g_sgRetryDueTick&&apiCalls==1&&g_sgAutomaticSleepBudget.requests()==1,
            "superseded elapsed handoff must not reclassify or retire the existing accepted owner");
    });
    scenario("queued or rejected entry request keeps its first scheduled deadline",[&]{
        for(const bool rejected:{false,true}){
            // First send at 239900, synchronous rejection schedules after 240000.
            reset();startEntryRetry(rejected?239400:239900);
            if(rejected){apiAccepted=false;dispatch();check(apiCalls==1,"original entry request reached Windows and failed");}
            const auto due=g_sgRetryDueTick;const auto writes=timerWrites;
            check(due>100000+240000,"existing retry is due after the original elapsed deadline");
            deadline();check(g_sgRetryDueTick==due&&timerWrites==writes&&apiCalls==static_cast<unsigned>(rejected),
                "elapsed callback neither advances nor re-registers an already scheduled retry");
            apiAccepted=true;dispatch();check(apiCalls==1+static_cast<unsigned>(rejected)&&g_sgAutomaticSleepBudget.requests()==apiCalls,
                "the retained deadline charges exactly one additional slot");
        }
    });
    scenario("existing retry at attempt limit never forks",[&]{startEntryRetry(10);g_sgTask.retryAttempt=SG_MAX_ENTRY_RETRIES-1;
        while(g_sgAutomaticSleepBudget.requests()<SG_MAX_ENTRY_RETRIES-1)check(g_sgAutomaticSleepBudget.consume(),"prepare remaining slot");
        dispatch();deadline();sgDispatchSameModeRetry();check(apiCalls==1&&sgSleepRetryActive()&&!g_sgRetryDueTick&&
            g_sgAutomaticSleepBudget.requests()==SG_MAX_ENTRY_RETRIES&&!g_sgSleepTimeoutWindow.requested,
            "elapsed deadline cannot retire or send over a last accepted call");});
    scenario("rejected shared API uses existing delayed retry budget",[]{arm();deadline();apiAccepted=false;dispatch();check(apiCalls==1&&g_sgAutomaticSleepBudget.requests()==1&&timers.count(retryTimerId),"rejection uses original delayed scheduler");dispatch();dispatch();check(apiCalls==3&&g_sgAutomaticSleepBudget.requests()==3&&!sgSleepRetryActive(),"original three-attempt budget remains bounded");});
    for(const char* event:{"internal-sleep-request-armed","sleep-request-route","sleep-request-dispatch",
            "sleep-request-route-result","sleep-retry-request"}) {
        scenario(event,[&]{
            arm();deadline();apiAccepted=false;throwLogEvent=event;
            bool escaped=false;
            try {dispatch();} catch(const std::runtime_error&) {escaped=true;}
            check(!escaped,"ordinary retry fact failure must not interrupt the shared sender");
            check(apiCalls==1&&g_sgAutomaticSleepBudget.requests()==1&&timers.count(retryTimerId)&&
                g_sgInternalSleepRequest.kind==SgRetryKind::None,"one rejected call clears its marker and schedules the original next attempt");
            dispatch();dispatch();
            check(apiCalls==3&&g_sgAutomaticSleepBudget.requests()==3&&!sgSleepRetryActive()&&!timers.count(retryTimerId),
                "repeated journal failures exhaust only the original budget and retire the retry");
        });
    }
    scenario("classic S3 routes through same tripleFALSE sender",[]{modern=false;arm();deadline();dispatch();check(apiCalls==1&&g_sgAutomaticSleepTarget==ymcc::sleeprequest::Mode::Sleep,"modern unavailable classic capability uses actual shared API");});
    scenario("all automatic sleep capabilities unavailable does not S4",[]{modern=classic=false;arm();deadline();dispatch();check(!apiCalls&&g_sgAutomaticSleepBudget.requests()==1&&!g_sgRetryDueTick&&!sgSleepRetryActive(),"unsupported route retires without forging a Windows failure or S4 fallback");});
    scenario("ordinary logs independent of detail logging and IO failure",[]{throwLogEvent="sleep-timeout-armed";arm();check(g_sgSleepTimeoutWindow.waiting,"arm log failure preserves elapsed policy");throwLogEvent="sleep-timeout-due";deadline();dispatch();check(apiCalls==1,"timeout fact IO cannot grant repeat or block power");for(const auto& fact:facts)if(fact.first=="sleep-timeout-armed"||fact.first=="sleep-timeout-due")check(fact.second.at("detailLogsRequired")==false&&fact.second.at("mandatoryEvidence")==true,"ordinary timeout facts explicitly mandatory without detailed logging");});
    scenario("power broadcast success and manual resume cancel",[]{arm();wake();sgSleepTimeoutPowerBroadcast(PBT_APMRESUMESUSPEND);check(!g_sgSleepTimeoutWindow.active(),"interactive resume retires");reset();arm();sgSleepTimeoutPowerBroadcast(PBT_APMRESUMECRITICAL);check(!g_sgSleepTimeoutWindow.active(),"critical resume retires");reset();modern=false;arm();sgSleepTimeoutPowerBroadcast(PBT_APMSUSPEND);check(!g_sgSleepTimeoutWindow.active(),"classic Windows suspend confirmation retires");});
    scenario("AoAc suspend notification alone is not lowpower proof",[]{arm();sgSleepTimeoutPowerBroadcast(PBT_APMSUSPEND);check(g_sgSleepTimeoutWindow.waiting,"AoAc PBT alone keeps original elapsed fallback");deadline();sgSleepTimeoutPowerBroadcast(PBT_APMSUSPEND);check(!g_sgSleepTimeoutWindow.active(),"requested Windows suspend boundary retires");});
    scenario("policy rollback overflow and monotonic original fence",[]{ymcc::sleeptimeout::Window window;check(!window.arm(0,0)&&!window.arm(epoch,epoch-1),"invalid or backwards arm refuses");check(!window.arm((std::numeric_limits<ULONGLONG>::max)()-1,(std::numeric_limits<ULONGLONG>::max)()-1),"deadline overflow refuses");arm();fixtureFileTime=epoch-1;sgSleepTimeoutTick();check(!g_sgSleepTimeoutWindow.active()&&!apiCalls,"clock rollback cancels oneShot");});
    reset();g_sgRetrySleepTimeout=false;::captured.clear();
    return sleepEvidenceChecks-initial;
}
} // namespace sleep_timeout_fixture
[[maybe_unused]] static unsigned sleepTimeoutNativeTests(){return sleep_timeout_fixture::runTests();}
