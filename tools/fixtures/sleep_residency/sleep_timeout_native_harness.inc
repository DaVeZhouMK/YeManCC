// Actual timeout policy/native include and exact main.cpp sender/retry consumers.
// Every Windows power, timer, privilege, clock and action-reader boundary is inert.
#include <windows.h>
#include <powrprof.h>
#include <atomic>
#include <array>
#include <climits>
#include <functional>
#include <map>
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
static std::atomic<ULONGLONG> g_sgLastPowerButtonSleepIntentFileTime{epoch},g_sgLastPowerButtonWakeEventFileTime{0};
static std::atomic<ULONGLONG> g_sgLatestUserSleepEntryFileTime{epoch},g_sgOriginalPowerButtonWakeFileTime{0},g_sgClosedSleepIntentFileTime{0};
static std::atomic<ULONGLONG> g_sgPauseWorkCompletedGeneration{771};
static std::atomic<ymcc::sleeprequest::Mode> g_sgAutomaticSleepTarget{ymcc::sleeprequest::Mode::None};
static ymcc::sleeprepair::Budget g_sgAutomaticSleepBudget{3};
enum class PowerLifecycle {Ready,Suspending,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Suspending};
static struct {SgRetryKind retryKind=SgRetryKind::None;unsigned retryAttempt=0;ULONGLONG generation=771;SgSleepMode mode=SgSleepMode::S3;bool unexpectedWakeConsumed=false;} g_sgTask;
static struct {SgRetryKind kind=SgRetryKind::None;ULONGLONG dispatchTick=0,dispatchFileTime=0;} g_sgInternalSleepRequest;
static ULONGLONG fixtureFileTime=epoch,tick=100000,fixtureGeneration=771,g_sgRetryDueTick=0,g_sgSleepTriggerTick=1,g_sgModernWakeClassifiedTick=0,g_sgModernStandbyGeneration=0;
static double g_sgSleepTriggerEpoch=0;
static bool timerAvailable=true,activeSchemeKnown=true,statusKnown=true,acKnown=true,dcKnown=true,capabilitiesKnown=true,modern=true,classic=true,apiAccepted=true;
static BYTE acLine=1;
static ULONG acAction=1,dcAction=1;
static DWORD lastError=0,apiError=ERROR_ACCESS_DENIED;
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
static ULONGLONG clockTick(){return tick;}
static void sgSleepTimeoutStop(const char*) noexcept;
static bool sgSleepTimeoutDispatchPermitted();
static ULONGLONG sgSleepTimeoutOriginalIntent();
static void sgStartSleepRetry(SgRetryKind,const char*);
static void sgAdvanceSleepRetry(const char*);
static void sgScheduleNextSleepRetry(const char*);
static bool sgSleepRetryActive(){return g_sgRetryInProgress&&g_sgTask.retryKind!=SgRetryKind::None;}
static bool sgModernStandbyRepairSupported(){return modern;}
#ifdef YMCC_SLEEP_LATE_WAKE_ENABLE
static bool sgNonUserWakeGuardEnabled();
static bool sgExternalDeviceWakeIntentAge(ULONGLONG&);
static bool sgObserveNonUserWake(const char*);
static void sgEvaluateExternalDeviceWake();
static void sgClearUnexpectedWake(const char*,bool);
static bool g_sgNonUserWakeObserved=false;
static std::atomic<ULONGLONG> g_sgLastKernel507EventFileTime{0};
static std::atomic<int> g_sgLastKernel506Reason{1};
static ULONGLONG g_sgIgnoredEarlyWakeFileTime=0,g_sgLastS0WakeTick=0,g_sgLastPowerButtonWakeTick=0;
static int g_sgLastS0WakeReason=-1;
// Legacy fact-monitor preference is extracted from main, not a high-frequency gate.
static unsigned uiRecoveryCalls=0;
static void sgAbnormalStop(const char*){}
static void sgStopMonitorEntryFailureTimer(const char*){}
static void sgHandleModernStandbyWake(bool){++uiRecoveryCalls;}
static void sleepRecoveryAutoArm(ULONGLONG){++uiRecoveryCalls;}
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
static void sgFinishSleepRetryFailure(const char*){++finishes;g_sgRetryInProgress=false;g_sgRetryDueTick=0;timers.erase(retryTimerId);}
static void sgAbortSleepIntent(const char* reason){++aborts;g_sgRetryInProgress=false;sgSleepTimeoutStop(reason);}
static void sgCancelEntryFailureRetry(const char*,SgRetryKind only=SgRetryKind::EntryFailure){if(g_sgTask.retryKind==only){g_sgRetryInProgress=false;timers.erase(retryTimerId);}}
#ifndef YMCC_SLEEP_LATE_WAKE_ENABLE
static void sgClearUnexpectedWake(const char*,bool){}
#endif
static void sgClearInternalSleepRequest(const char*){++markerClears;g_sgInternalSleepRequest={};}
static void sgMarkInternalSleepRequest(SgRetryKind kind){g_sgInternalSleepRequest={kind,tick,fixtureFileTime};}
static bool sgPrepareSleepPrivilege(SgSystemSleepRequestResult& result){result.privilegeRequired=result.shutdownPrivilegeEnabled=true;const auto hook=std::move(afterPrivilege);afterPrivilege=nullptr;if(hook)hook();return true;}
static void stopPowerResumeWatchdog(){}
static void closeHardwareWriteGate(const char*){}
static struct FanBoundaryPort {void retryBoundary(ULONGLONG,ULONGLONG){++boundaryCalls;}} g_fanPhysicalWake;
static BOOL killTimer(HWND,UINT_PTR id){++timerKills;timers.erase(id);return TRUE;}
static UINT_PTR setTimer(HWND,UINT_PTR id,UINT delay,TIMERPROC){++timerWrites;check(id==timeoutTimerId||id==retryTimerId,"timeout uses only original timeout/retry timer IDs");if(!timerAvailable)return 0;timers[id]=tick+delay;return id;}
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
#include "production_timeout_bindings.inc"
#include "sleep_timeout_native.inc"
#ifdef YMCC_SLEEP_LATE_WAKE_ENABLE
#include "production_late_wake_bindings.inc"
static LRESULT s0WakeMessage(WPARAM w,LPARAM l){
#include "production_late_wake_message.inc"
}
static void manualWakeCallback(ULONGLONG eventFileTime,ULONGLONG recordedFileTime,bool leaving=true,bool reason1=true){
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
static void systemWakeCallback(int eventId,int targetState,ULONGLONG eventFileTime,ULONGLONG recordedFileTime){
    const std::string xml=std::to_string(targetState);(void)recordedFileTime;
#include "production_timeout_s4_hook.inc"
}
static void at(ULONGLONG ms){fixtureFileTime=epoch+ms*10000;tick=100000+ms;}
static void wake(){g_sgLastPowerButtonWakeEventFileTime=fixtureFileTime;g_sgOriginalPowerButtonWakeFileTime=fixtureFileTime;}
static void reset(){
    afterLog=nullptr;afterPowerRead=nullptr;afterPrivilege=nullptr;throwLogEvent.clear();g_sgSleepTimeoutWindow={};g_sgSleepTimeoutSuccessfulExit=0;
    g_hwnd=reinterpret_cast<HWND>(1);g_guardEnabled=g_sgRetrySleepTimeout=true;g_sgRetryEntryFailure=g_sgResleepEnabled=false;g_sgRepairEligible=true;g_exitRequested=false;
    g_sgLastPowerButtonSleepIntentFileTime=g_sgLatestUserSleepEntryFileTime=epoch;g_sgLastPowerButtonWakeEventFileTime=g_sgOriginalPowerButtonWakeFileTime=g_sgClosedSleepIntentFileTime=0;
    g_sgTask={};g_sgTask.generation=771;g_sgTask.mode=SgSleepMode::S3;g_sgInternalSleepRequest={};g_sgRetryInProgress=false;fixtureGeneration=771;g_sgPauseWorkCompletedGeneration=771;
    g_sgAutomaticSleepBudget.confirmedUserWake();g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::None;g_sgRetryDueTick=0;g_sgSleepTriggerTick=1;g_sgSleepTriggerEpoch=0;g_powerLifecycle=PowerLifecycle::Suspending;
    timerAvailable=activeSchemeKnown=statusKnown=acKnown=dcKnown=capabilitiesKnown=modern=classic=apiAccepted=true;acLine=1;acAction=dcAction=1;apiError=ERROR_ACCESS_DENIED;lastError=0;
    timerWrites=timerKills=powerReads=capabilityReads=apiCalls=finishes=aborts=markerClears=boundaryCalls=monitorCalls=0;
#ifdef YMCC_SLEEP_LATE_WAKE_ENABLE
    g_sgNonUserWakeObserved=false;g_sgLastKernel507EventFileTime=0;g_sgLastKernel506Reason=1;g_sgIgnoredEarlyWakeFileTime=g_sgLastS0WakeTick=g_sgLastPowerButtonWakeTick=0;g_sgLastS0WakeReason=-1;g_sgFactMonitorEnabled=false;uiRecoveryCalls=0;
#endif
    timers.clear();apiArguments.clear();facts.clear();at(0);
}
static void arm(){sgSleepTimeoutArm(1,g_sgLastPowerButtonSleepIntentFileTime.load());}
static void deadline(){at(240000);sgSleepTimeoutTick();}
static void dispatch(){if(g_sgRetryDueTick>tick){const auto delta=g_sgRetryDueTick-tick;tick+=delta;fixtureFileTime+=delta*10000;}timers.erase(retryTimerId);sgDispatchSameModeRetry();}
static void completed(const std::string& fields,ULONGLONG eventTime){::productionCapture(true,"<Event><EventData>"+fields+"</EventData></Event>",eventTime);::captured.clear();}
static std::string positive(){return "<Data Name=\"DurationInUs\">1000000</Data><Data Name=\"DripsResidencyInUs\">1000</Data><Data Name=\"HwDripsResidencyInUs\">0</Data><Data Name=\"SleepEntered\">true</Data>";}
template<class F> static void scenario(const std::string& name,F body){
    reset();const auto before=sleepEvidenceChecks;
    try{body();for(const auto& values:apiArguments)check(values==std::array<BOOLEAN,3>{FALSE,FALSE,FALSE},name+": actual shared sender arguments are tripleFALSE");for(const auto& fact:facts)if(fact.first=="sleep-request-route-result")check(fact.second.at("arguments")==json::array({false,false,false})&&fact.second.at("hibernate")==false,name+": final shared route receipt explicitly records tripleFALSE and no hibernate");}
    catch(...){std::cerr<<"TIMEOUT_SCENARIO: "<<name<<'\n';throw;}
    check(sleepEvidenceChecks>before,name+": selected scenario executed assertions");
    scenarios.push_back({{"name",name},{"status","passed"},{"checks",sleepEvidenceChecks-before},{"mockSuspendCalls",apiCalls},{"timerWrites",timerWrites},{"powerPlanReads",powerReads},{"sharedBudgetConsumed",g_sgAutomaticSleepBudget.requests()}});
}
} // namespace sleep_timeout_fixture
static void sgSleepTimeoutObserveCompleted(const json& evidence,unsigned long long at) noexcept {sleep_timeout_fixture::sgSleepTimeoutObserveCompleted(evidence,at);}
[[maybe_unused]] static unsigned sleepTimeoutNativeTests(){
    using namespace sleep_timeout_fixture;
    const auto initial=sleepEvidenceChecks;scenarios=json::array();
    check(g_sgRetrySleepTimeout,"actual main.cpp timeout default is true");
    scenario("239999ms early tick preserves original deadline",[]{arm();const auto first=timers.at(timeoutTimerId);at(239999);sgSleepTimeoutTick();check(g_sgSleepTimeoutWindow.waiting&&!apiCalls&&!sgSleepRetryActive()&&timers.at(timeoutTimerId)==first,"early tick rearms only remaining one millisecond");check(timerWrites==2,"early delivery has bounded single remaining timer");});
    scenario("240000ms enters actual shared sender once",[]{arm();deadline();check(!g_sgSleepTimeoutWindow.waiting&&g_sgSleepTimeoutWindow.requested&&sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::SleepTimeout&&!apiCalls,"deadline hands off to existing scheduler");dispatch();check(apiCalls==1&&g_sgAutomaticSleepBudget.requests()==1,"actual dispatcher charges shared budget exactly once");const auto writes=timerWrites;for(unsigned i=0;i<8;++i)sgSleepTimeoutTick();check(apiCalls==1&&timerWrites==writes&&!timers.count(timeoutTimerId),"consumed oneShot never polls or duplicates accepted request");});
    scenario("late original506 uses remaining time",[]{at(230000);arm();check(timers.at(timeoutTimerId)-tick==10000,"delivery latency does not restart240seconds");deadline();dispatch();check(apiCalls==1,"late original reaches original deadline");});
    scenario("already overdue506 waits at most one millisecond",[]{at(300000);arm();check(timers.at(timeoutTimerId)-tick==1,"overdue intent schedules oneShot not fresh240seconds");sgSleepTimeoutTick();dispatch();check(apiCalls==1,"overdue original sends once");});
    scenario("manual wake before arm",[]{wake();arm();check(!g_sgSleepTimeoutWindow.active()&&!timerWrites,"wake at original timestamp blocks arm");});
    scenario("manual wake during arm power read",[]{afterPowerRead=[]{wake();};arm();check(!g_sgSleepTimeoutWindow.active()&&!timerWrites,"callback-side wake after arm read blocks timer");});
    scenario("manual wake before deadline tick",[]{arm();at(240000);wake();sgSleepTimeoutTick();check(!g_sgSleepTimeoutWindow.active()&&!apiCalls&&!sgSleepRetryActive(),"tick respects manual wake");});
    scenario("manual wake after deadline power read",[]{arm();afterPowerRead=[]{wake();};deadline();check(!g_sgSleepTimeoutWindow.active()&&!apiCalls&&!sgSleepRetryActive(),"post-read wake rechecked");});
    scenario("manual wake after deadline fact log",[]{arm();afterLog=[](const char* event){if(std::string(event)=="sleep-timeout-due")wake();};deadline();check(!apiCalls&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active(),"post-log wake vetoes handoff");});
    scenario("manual wake after actual sender dispatch log",[]{arm();deadline();afterLog=[](const char* event){if(std::string(event)=="sleep-request-dispatch")wake();};dispatch();check(!apiCalls,"actual sender final callback-side cancellation wins");});
    scenario("manual wake during actual sender action read",[]{arm();deadline();afterPowerRead=[]{wake();};dispatch();check(!apiCalls,"actual dispatcher rejects wake during final applicable-plan read");});
    scenario("duplicate and old506 cannot reset deadline",[]{arm();const auto due=timers.at(timeoutTimerId);at(90000);arm();sgSleepTimeoutArm(1,epoch-10000);check(timerWrites==1&&timers.at(timeoutTimerId)==due&&g_sgSleepTimeoutWindow.origin==epoch,"same and older event cannot refill timer");sgSleepTimeoutStop("test-retire");arm();check(!g_sgSleepTimeoutWindow.active()&&timerWrites==1,"retired original cannot rearm");});
    scenario("old nonuser guard clears marker without losing240deadline",[]{arm();at(30000);g_sgLastPowerButtonSleepIntentFileTime=0;check(sgSleepTimeoutOriginalIntent()==epoch,"independent window retains original506 identity");deadline();dispatch();check(apiCalls==1&&g_sgAutomaticSleepBudget.requests()==1,"cleared old guard marker still reaches actual shared sender once");});
    scenario("different nonzero new marker vetoes old deadline",[]{arm();at(30000);g_sgLastPowerButtonSleepIntentFileTime=fixtureFileTime;deadline();check(!apiCalls&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active(),"new foreign original marker stops old window");});
    scenario("manual wake after old guard marker cleared still vetoes",[]{arm();at(30000);g_sgLastPowerButtonSleepIntentFileTime=0;wake();deadline();check(!apiCalls&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active(),"independent original manual-wake anchor survives inherited marker retirement");});
    scenario("new original intent owns new deadline",[]{arm();at(10000);g_sgLastPowerButtonSleepIntentFileTime=g_sgLatestUserSleepEntryFileTime=fixtureFileTime;arm();check(g_sgSleepTimeoutWindow.origin==epoch+10000*10000ULL&&timers.at(timeoutTimerId)==tick+240000,"strictly newer original arms new window");at(240000);sgSleepTimeoutTick();check(!sgSleepRetryActive()&&g_sgSleepTimeoutWindow.waiting,"stale old timer delivery cannot accelerate new intent");});
    for(const BYTE supply:{BYTE{1},BYTE{0}})for(const ULONG action:{1UL,2UL,4UL,0UL,99UL})scenario(std::string(supply?"AC":"DC")+" applicable action "+std::to_string(action),[=]{acLine=supply;acAction=supply?action:2;dcAction=supply?2:action;arm();check(g_sgSleepTimeoutWindow.active()==(action==1),"only current supply Sleep may arm; opposite supply cannot authorize");if(action==1){deadline();dispatch();check(apiCalls==1,"selected supply Sleep uses actual sender");}else check(!timerWrites&&!apiCalls,"S4 ScreenOff None unknown never dispatch");});
    for(const ULONG action:{2UL,4UL,0UL,99UL})scenario("action changes before deadline "+std::to_string(action),[=]{arm();acAction=action;deadline();check(!sgSleepRetryActive()&&!apiCalls&&!g_sgSleepTimeoutWindow.active(),"deadline rechecks current action");});
    scenario("AC to DC switch rechecks applicable action",[]{acAction=1;dcAction=4;arm();acLine=0;deadline();check(!apiCalls&&!sgSleepRetryActive(),"new DC screenoff cannot inherit AC Sleep permission");});
    scenario("unknown current plan or action refused",[]{activeSchemeKnown=false;arm();check(!timerWrites,"unknown current scheme refuses");activeSchemeKnown=true;acKnown=false;arm();check(!timerWrites,"unreadable selected action refuses");acKnown=true;statusKnown=false;acAction=1;dcAction=2;arm();check(!timerWrites,"unknown supply with conflicting actions refuses");});
    scenario("missing original506 timestamp highwater refuses collection-time substitute",[]{g_sgLatestUserSleepEntryFileTime=0;arm();check(!g_sgSleepTimeoutWindow.active()&&!timerWrites,"absent raw506 timestamp cannot be replaced by callback collection time");g_sgLatestUserSleepEntryFileTime=epoch-1;arm();check(!g_sgSleepTimeoutWindow.active()&&!timerWrites,"different latest original timestamp cannot authorize current substitute");});
    scenario("nonReason1 and missing original506 do not arm",[]{for(int reason:{0,2,3,4,7})sgSleepTimeoutArm(reason,epoch);sgSleepTimeoutArm(1,0);sgSleepTimeoutArm(1,epoch-1);check(!timerWrites&&!g_sgSleepTimeoutWindow.active(),"idle lid programmatic and noncurrent original are not human button evidence");sgSleepTimeoutPowerBroadcast(PBT_APMSUSPEND);sgSleepTimeoutTick();check(!timerWrites&&!apiCalls,"ordinary PBT without original506 cannot manufacture timeout");});
    for(unsigned veto=0;veto<3;++veto)scenario("OFF guard or exit at arm "+std::to_string(veto),[=]{if(veto==0)g_sgRetrySleepTimeout=false;if(veto==1)g_guardEnabled=false;if(veto==2)g_exitRequested=true;arm();check(!timerWrites&&!apiCalls,"disabled or exiting cannot arm");});
    for(unsigned veto=0;veto<3;++veto)scenario("OFF guard or exit at tick "+std::to_string(veto),[=]{arm();if(veto==0)g_sgRetrySleepTimeout=false;if(veto==1)g_guardEnabled=false;if(veto==2)g_exitRequested=true;deadline();check(!apiCalls&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active(),"disabled or exiting cannot dispatch");});
    scenario("S4 original107 callback vetoes before queued UIwake",[]{arm();at(239999);systemWakeCallback(107,5,fixtureFileTime,fixtureFileTime);check(g_sgSleepTimeoutWindow.waiting&&g_sgSleepTimeoutSuccessfulExit==fixtureFileTime,"callback publishes success while UI Stop has not executed");deadline();check(!apiCalls&&!sgSleepRetryActive()&&!g_sgSleepTimeoutWindow.active(),"timer delivered before S4 UIwake cannot send after callback success");});
    scenario("old S4 timestamp cannot cancel newer original intent",[]{arm();at(239999);systemWakeCallback(107,5,epoch-1,fixtureFileTime);check(g_sgSleepTimeoutSuccessfulExit==epoch-1&&g_sgSleepTimeoutWindow.waiting,"delayed107 retains original old timestamp not collection-time replacement");deadline();dispatch();check(apiCalls==1,"old S4 callback cannot cancel new original506 deadline");});
    scenario("S4 missing original time never uses callback collection time",[]{arm();at(239999);systemWakeCallback(107,5,0,fixtureFileTime);check(g_sgSleepTimeoutSuccessfulExit==0,"missing original107 time cannot create success witness");deadline();dispatch();check(apiCalls==1,"collection-time fallback never vetoes elapsed intent");});
    scenario("nonS4 event107 and other event IDs cannot publish S4success",[]{arm();systemWakeCallback(107,4,epoch+1,epoch+1);systemWakeCallback(507,5,epoch+1,epoch+1);check(g_sgSleepTimeoutSuccessfulExit==0,"only actual107 TargetState5 is system S4success");});
    scenario("positive507 success veto before arm",[]{completed(positive(),epoch);arm();check(!timerWrites&&g_sgSleepTimeoutSuccessfulExit==epoch,"actual capture hook forwards original507 time and positive witness");});
    scenario("positive507 success veto while waiting",[]{arm();completed(positive(),epoch+10000);deadline();check(!apiCalls&&!sgSleepRetryActive(),"Windows positive residency cancels waiting timeout");});
    scenario("positive507 after due log vetoes",[]{arm();afterLog=[](const char* event){if(std::string(event)=="sleep-timeout-due")completed(positive(),fixtureFileTime);};deadline();check(!apiCalls&&!sgSleepRetryActive(),"late positive before handoff wins");});
    scenario("positive507 after actual dispatch log vetoes",[]{arm();deadline();afterLog=[](const char* event){if(std::string(event)=="sleep-request-dispatch")completed(positive(),fixtureFileTime);};dispatch();check(!apiCalls,"real sender repeats positive-success veto at final API boundary");});
    scenario("doublezero507 is neither failure nor success",[]{arm();completed("<Data Name=\"DurationInUs\">1000000</Data><Data Name=\"DripsResidencyInUs\">0</Data><Data Name=\"HwDripsResidencyInUs\">0</Data><Data Name=\"SleepEntered\">true</Data>",epoch+10000);check(!apiCalls&&g_sgSleepTimeoutWindow.waiting&&g_sgSleepTimeoutSuccessfulExit==0,"doublezero neither triggers retry nor fabricates success");deadline();dispatch();check(apiCalls==1,"elapsed policy alone reaches deadline without DRIPS failure classification");});
    scenario("missing malformed and old507 cannot veto new intent",[]{arm();completed("<Data Name=\"SleepEntered\">true</Data>",epoch+10000);completed(positive(),epoch-1);completed("<Data Name=\"DurationInUs\">100</Data><Data Name=\"DripsResidencyInUs\">101</Data><Data Name=\"SleepEntered\">true</Data>",epoch+10000);deadline();dispatch();check(apiCalls==1,"invalid missing or older witness keeps elapsed policy independent");});
    scenario("positive witness highwater cannot roll back",[]{completed(positive(),epoch+20000);completed(positive(),epoch+10000);check(g_sgSleepTimeoutSuccessfulExit==epoch+20000,"out-of-order positive507 retains highest original timestamp");});
    scenario("arm timer registration fails closed",[]{timerAvailable=false;arm();check(!g_sgSleepTimeoutWindow.active()&&!apiCalls,"failed SetTimer retires window");timerAvailable=true;arm();check(!g_sgSleepTimeoutWindow.active()&&timerWrites==1,"same intent never retries failed registration");});
    scenario("early tick rearm timer fails closed",[]{arm();at(239999);timerAvailable=false;sgSleepTimeoutTick();check(!g_sgSleepTimeoutWindow.active()&&!apiCalls,"remaining timer registration failure stops");});
    scenario("missing window fails closed",[]{g_hwnd=nullptr;arm();check(!g_sgSleepTimeoutWindow.active()&&!timerWrites,"no HWND never creates timer or power request");});
    scenario("shared budget exhausted never refills",[]{for(unsigned i=0;i<3;++i)check(g_sgAutomaticSleepBudget.consume(),"preexisting request consumes original budget");arm();deadline();check(!apiCalls&&!sgSleepRetryActive()&&g_sgAutomaticSleepBudget.requests()==3,"timeout cannot refill or fork exhausted budget");});
    scenario("existing retry advances without resetting attempt or budget",[]{g_sgRetryInProgress=true;g_sgTask.retryKind=SgRetryKind::EntryFailure;g_sgTask.retryAttempt=1;check(g_sgAutomaticSleepBudget.consume(),"existing sender already charged budget");arm();deadline();check(g_sgTask.retryKind==SgRetryKind::SleepTimeout&&g_sgTask.retryAttempt==1&&boundaryCalls==0&&markerClears==1,"same active transaction advances without Start or second owner");dispatch();check(apiCalls==1&&g_sgTask.retryAttempt==2&&g_sgAutomaticSleepBudget.requests()==2,"existing attempt and cumulative budget preserved");});
    scenario("existing retry at attempt limit never forks",[]{g_sgRetryInProgress=true;g_sgTask.retryKind=SgRetryKind::EntryFailure;g_sgTask.retryAttempt=SG_MAX_ENTRY_RETRIES;arm();deadline();check(!apiCalls&&!sgSleepRetryActive()&&finishes==1&&!boundaryCalls,"advance at limit finishes original transaction");});
    scenario("rejected shared API uses existing delayed retry budget",[]{arm();deadline();apiAccepted=false;dispatch();check(apiCalls==1&&g_sgAutomaticSleepBudget.requests()==1&&timers.count(retryTimerId),"rejection uses original delayed scheduler");dispatch();dispatch();check(apiCalls==3&&g_sgAutomaticSleepBudget.requests()==3&&!sgSleepRetryActive(),"original three-attempt budget remains bounded");});
    scenario("classic S3 routes through same tripleFALSE sender",[]{modern=false;arm();deadline();dispatch();check(apiCalls==1&&g_sgAutomaticSleepTarget==ymcc::sleeprequest::Mode::Sleep,"modern unavailable classic capability uses actual shared API");});
    scenario("all automatic sleep capabilities unavailable does not S4",[]{modern=classic=false;arm();deadline();dispatch();check(!apiCalls&&g_sgAutomaticSleepBudget.requests()==1,"S4 capability never becomes automatic fallback");});
    scenario("ordinary logs independent of detail logging and IO failure",[]{throwLogEvent="sleep-timeout-armed";arm();check(g_sgSleepTimeoutWindow.waiting,"arm log failure preserves elapsed policy");throwLogEvent="sleep-timeout-due";deadline();dispatch();check(apiCalls==1,"timeout fact IO cannot grant repeat or block power");for(const auto& fact:facts)if(fact.first=="sleep-timeout-armed"||fact.first=="sleep-timeout-due")check(fact.second.at("detailLogsRequired")==false&&fact.second.at("mandatoryEvidence")==true,"ordinary timeout facts explicitly mandatory without detailed logging");});
    scenario("power broadcast success and manual resume cancel",[]{arm();sgSleepTimeoutPowerBroadcast(PBT_APMRESUMESUSPEND);check(!g_sgSleepTimeoutWindow.active(),"interactive resume retires");reset();arm();sgSleepTimeoutPowerBroadcast(PBT_APMRESUMECRITICAL);check(!g_sgSleepTimeoutWindow.active(),"critical resume retires");reset();modern=false;arm();sgSleepTimeoutPowerBroadcast(PBT_APMSUSPEND);check(!g_sgSleepTimeoutWindow.active(),"classic Windows suspend confirmation retires");});
    scenario("AoAc suspend notification alone is not lowpower proof",[]{arm();sgSleepTimeoutPowerBroadcast(PBT_APMSUSPEND);check(g_sgSleepTimeoutWindow.waiting,"AoAc PBT alone keeps original elapsed fallback");deadline();sgSleepTimeoutPowerBroadcast(PBT_APMSUSPEND);check(!g_sgSleepTimeoutWindow.active(),"requested Windows suspend boundary retires");});
    scenario("policy rollback overflow and monotonic original fence",[]{ymcc::sleeptimeout::Window window;check(!window.arm(0,0)&&!window.arm(epoch,epoch-1),"invalid or backwards arm refuses");check(!window.arm((std::numeric_limits<ULONGLONG>::max)()-1,(std::numeric_limits<ULONGLONG>::max)()-1),"deadline overflow refuses");arm();fixtureFileTime=epoch-1;sgSleepTimeoutTick();check(!g_sgSleepTimeoutWindow.active()&&!apiCalls,"clock rollback cancels oneShot");});
    reset();g_sgRetrySleepTimeout=false;::captured.clear();
    return sleepEvidenceChecks-initial;
}
