// Compiles the actual runtime include; every event/timer/power boundary is inert.
#include <windows.h>
#include <winevt.h>
#include <atomic>
#include <memory>
#include <deque>
#include "sleep_abnormal_resleep.h"
#include "sleep_request_fallback.h"
#include "sleep_retry_budget.h"
constexpr UINT SG_ABNORMAL_TIMER_ID=0xA212,WM_SG_ABNORMAL_EVIDENCE=WM_APP+825;
static HWND g_hwnd=reinterpret_cast<HWND>(1);
static bool g_guardEnabled=true,g_sgRetryEntryFailure=true,g_sgResleepEnabled=true;
static std::atomic<bool> g_exitRequested{false};
static std::atomic<ULONGLONG> g_sgLastPowerButtonWakeEventFileTime{0};
enum class PowerLifecycle{Ready,Suspending,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Suspended};
enum class SgRetryKind{None,EntryFailure,UnexpectedWake};
struct {SgRetryKind kind=SgRetryKind::None;ULONGLONG dispatchTick=0,dispatchFileTime=0;} g_sgInternalSleepRequest;
static std::atomic<ymcc::sleeprequest::Mode> g_sgAutomaticSleepTarget{ymcc::sleeprequest::Mode::None};
static ymcc::sleeprepair::Budget g_sgAutomaticSleepBudget{3};
static constexpr ULONGLONG testEpoch=134372000000000000ULL;
static ULONGLONG testTick=100000,testFileTime=testEpoch,testDue=0;
static unsigned testSubscriptions=0,testCloses=0,testSleepCalls=0,testPowerOffCalls=0,testTimerWrites=0;
static bool testChannel=true,testAccepted=true,testPowerOffAccepted=true,testRetryBusy=false,testPrivilege=true,testWakeDuringPrivilege=false;
static bool testSuspendOnHibernate=false,testTimerAvailable=true,testSuccessDuringPrivilege=false,testReplaceMarkerDuringRequest=false;
static DWORD testError=0;
static std::deque<MSG> testMessages;
static std::string testXml;
struct SgSystemSleepRequestResult{bool accepted=false;DWORD requestError=0,shutdownPrivilegeError=0;ymcc::sleeprequest::Mode target=ymcc::sleeprequest::Mode::None;};
static void sgAbnormalSuspendBoundary();
static void sgAbnormalPostCompleted(const std::string&,ULONGLONG);
static bool sgPrepareSleepPrivilege(SgSystemSleepRequestResult& result){
    if(testWakeDuringPrivilege)g_sgLastPowerButtonWakeEventFileTime=testFileTime;
    if(testSuccessDuringPrivilege)sgAbnormalPostCompleted("<Event><EventData><Data Name=\"DurationInUs\">20000000</Data><Data Name=\"DripsResidencyInUs\">1000000</Data><Data Name=\"HwDripsResidencyInUs\">900000</Data><Data Name=\"SleepEntered\">true</Data></EventData></Event>",testFileTime);
    if(!testPrivilege)result.shutdownPrivilegeError=ERROR_ACCESS_DENIED;
    return testPrivilege;
}
static SgSystemSleepRequestResult sgRequestSystemSleep(ymcc::sleeprequest::Mode first,ymcc::sleeprequest::Mode,bool staged){
    check(staged,"stage uses shared power adapter");++testSleepCalls;g_sgAutomaticSleepTarget=first;
    if(testSuspendOnHibernate&&first==ymcc::sleeprequest::Mode::Hibernate)sgAbnormalSuspendBoundary();
    if(testReplaceMarkerDuringRequest)g_sgInternalSleepRequest={SgRetryKind::UnexpectedWake,testTick+1,testFileTime+1};
    return {testAccepted,testAccepted?0UL:ERROR_NOT_SUPPORTED,0,first};
}
struct TestFanBoundary{void retryBoundary(ULONGLONG,ULONGLONG){}};
static TestFanBoundary g_fanPhysicalWake;
static void sgMarkInternalSleepRequest(SgRetryKind kind){g_sgInternalSleepRequest={kind,testTick,testFileTime};}
static void sgClearInternalSleepRequest(const char*){g_sgInternalSleepRequest={};}
static bool sgSleepRetryActive(){return testRetryBusy;}
static bool sgModernStandbyRepairSupported(){return true;}
static ULONGLONG sgNowFileTime(){return testFileTime;}
static bool sgRenderKernelPowerEvent(EVT_HANDLE,std::string& xml){xml=testXml;return true;}
static int sgKernelPowerEventId(const std::string& xml){return std::stoi(xml.substr(xml.find("<EventID>")+9));}
static ULONGLONG sgKernelPowerEventFileTime(const std::string&){return testFileTime;}
static ULONGLONG testGetTick(){return testTick;}
static BOOL testKillTimer(HWND,UINT_PTR){testDue=0;return TRUE;}
static UINT_PTR testSetTimer(HWND,UINT_PTR id,UINT ms,TIMERPROC){++testTimerWrites;if(!testTimerAvailable)return 0;testDue=testTick+ms;return id;}
static EVT_HANDLE testOpenChannel(EVT_HANDLE,LPCWSTR,DWORD){return reinterpret_cast<EVT_HANDLE>(2);}
static BOOL testGetChannel(EVT_HANDLE,EVT_CHANNEL_CONFIG_PROPERTY_ID,DWORD,DWORD,PEVT_VARIANT v,PDWORD used){v->Type=EvtVarTypeBoolean;v->BooleanVal=testChannel;*used=sizeof(*v);return TRUE;}
static EVT_HANDLE testSubscribe(EVT_HANDLE,HANDLE,LPCWSTR,LPCWSTR,EVT_HANDLE,PVOID,EVT_SUBSCRIBE_CALLBACK,DWORD flags){
    check(flags==EvtSubscribeToFutureEvents,"future events only; no replay");++testSubscriptions;return reinterpret_cast<EVT_HANDLE>(3);
}
static BOOL testClose(EVT_HANDLE){++testCloses;return TRUE;}
static BOOL testPost(HWND h,UINT id,WPARAM w,LPARAM l){MSG m{};m.hwnd=h;m.message=id;m.wParam=w;m.lParam=l;testMessages.push_back(m);return TRUE;}
static BOOL testPeek(LPMSG m,HWND,UINT,UINT,UINT){if(testMessages.empty())return FALSE;*m=testMessages.front();testMessages.pop_front();return TRUE;}
static BOOL testPowerOff(UINT flags,DWORD){check(flags==EWX_POWEROFF,"normal shutdown never forces applications");++testPowerOffCalls;return testPowerOffAccepted;}
static void testSetError(DWORD v){testError=v;}
static DWORD testGetError(){return testError;}
#define GetTickCount64 testGetTick
#define KillTimer testKillTimer
#define SetTimer testSetTimer
#define EvtOpenChannelConfig testOpenChannel
#define EvtGetChannelConfigProperty testGetChannel
#define EvtSubscribe testSubscribe
#define EvtClose testClose
#define PostMessageW testPost
#define PeekMessageW testPeek
#define ExitWindowsEx testPowerOff
#define SetLastError testSetError
#define GetLastError testGetError
#include "sleep_abnormal_resleep_native.inc"
#undef GetTickCount64
#undef KillTimer
#undef SetTimer
#undef EvtOpenChannelConfig
#undef EvtGetChannelConfigProperty
#undef EvtSubscribe
#undef EvtClose
#undef PostMessageW
#undef PeekMessageW
#undef ExitWindowsEx
#undef SetLastError
#undef GetLastError
static void testAt(ULONGLONG ms){testTick=100000+ms;testFileTime=testEpoch+ms*10000;}
static void testPump(){while(!testMessages.empty()){const auto m=testMessages.front();testMessages.pop_front();sgAbnormalHandleEvent(m.lParam);}}
static void testReset(){
    runtimeFactObserver=nullptr;
    sgAbnormalStop("fixture-reset");g_sgAbnormalLastHumanIntent=g_sgAbnormalLastOwnedRequest=0;g_sgAbnormalHumanIntent=0;g_sgResleepEnabled=true;g_sgAbnormalSuccessOrigin=0;g_sgAbnormalDropped=false;testAt(0);testDue=0;testSubscriptions=testCloses=testSleepCalls=testPowerOffCalls=testTimerWrites=0;
    testTimerAvailable=testChannel=testAccepted=testPowerOffAccepted=testPrivilege=true;testRetryBusy=testWakeDuringPrivilege=testSuspendOnHibernate=testSuccessDuringPrivilege=testReplaceMarkerDuringRequest=false;g_sgInternalSleepRequest={};
    g_sgLastPowerButtonWakeEventFileTime=0;g_guardEnabled=g_sgRetryEntryFailure=true;g_exitRequested=false;
    g_powerLifecycle=PowerLifecycle::Suspended;g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::None;
    g_sgAutomaticSleepBudget.confirmedUserWake();testError=0;captured.clear();sgAbnormalDisplay(0);
}
static std::string testEvent(unsigned id,const std::string& fields){return "<Event><System><EventID>"+std::to_string(id)+"</EventID></System><EventData>"+fields+"</EventData></Event>";}
static void testDeliver(unsigned id,const std::string& fields){testXml=testEvent(id,fields);sgAbnormalEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(4));testPump();}
static void testBad(ULONGLONG end,ULONGLONG period){
    testAt(end);testDeliver(161,"<Data Name=\"DurationThisPeriodMs\">"+std::to_string(period)+"</Data><Data Name=\"ResiliencyPhaseNonActivatedNoDripsMs\">"+std::to_string(period)+"</Data><Data Name=\"NonActivatedCpuTimeMs\">0</Data>");
}
static unsigned abnormalRuntimeTests(const json& fixture){
    const auto initialChecks=checks;
    testReset();sgAbnormalArm(1,testEpoch);testAt(30000);
    ymcc::abnormalsleep::publishConfirmedWake(g_sgLastPowerButtonWakeEventFileTime,testFileTime);
    ymcc::abnormalsleep::publishConfirmedWake(g_sgLastPowerButtonWakeEventFileTime,testEpoch-1);
    sgAbnormalTick();check(g_sgLastPowerButtonWakeEventFileTime==testFileTime&&!g_sgAbnormalEpisode.active()&&!testSleepCalls,
        "out-of-order old button wake cannot overwrite a newer manual veto");
    testReset();check(!g_sgAbnormalEpisode.active()&&!testDue&&!testSubscriptions,"untriggered flow has no timer or subscription");
    sgAbnormalArm(3,testEpoch);check(!testSubscriptions&&!testDue,"menu sleep cannot start power-button-only flow");
    g_sgRetryEntryFailure=false;sgAbnormalArm(1,testEpoch);check(!testSubscriptions,"disabled option cannot arm");
    testReset();testChannel=false;sgAbnormalArm(1,testEpoch);check(!g_sgAbnormalEpisode.active()&&!testDue&&!testSubscriptions,"unavailable source stays unknown without enabling channel");
    testReset();sgAbnormalArm(1,testEpoch);const auto due=testDue;sgAbnormalArm(1,testEpoch);
    check(testSubscriptions==1&&testDue==due,"duplicate button506 never resets origin or subscribes twice");
    testAt(30000);sgAbnormalTick();check(!testSleepCalls&&!testPowerOffCalls,"missing evidence at30seconds does not authorize sleep");
    testAt(45000);sgAbnormalTick();check(!testSleepCalls&&g_sgAbnormalEpisode.next==1,"missed30second stage is not replayed");
    testAt(900000);sgAbnormalTick();check(!g_sgAbnormalEpisode.active()&&!testPowerOffCalls&&!testDue,"late resume cannot execute expired shutdown");
    testReset();sgAbnormalArm(1,testEpoch);testBad(30000,30000);
    check(testSleepCalls==1&&g_sgAutomaticSleepBudget.requests()==1,"known bad original period dispatches one30second sleep");
    sgAbnormalTick();check(testSleepCalls==1,"same stage cannot dispatch twice");
    testAt(31000);g_sgLastPowerButtonWakeEventFileTime=testFileTime;sgAbnormalTick();
    check(!g_sgAbnormalEpisode.active()&&!testDue&&testCloses==2,"callback-side power button cancels and closes live source");
    testAt(360000);sgAbnormalTick();check(testSleepCalls==1&&!testPowerOffCalls,"old timers after user wake cannot sleep or power off");
    // A new physical intent is allowed; no interception of the user's Windows request.
    testAt(370000);sgAbnormalArm(1,testFileTime);check(g_sgAbnormalEpisode.active()&&g_sgAbnormalEpisode.origin==testFileTime,"fresh manual sleep is not blocked by old episode");
    testReset();sgAbnormalArm(1,testEpoch);testAt(20000);
    const auto originalTick=g_sgAbnormalEpisode.startTick;sgAbnormalInternalEntry();
    check(g_sgAbnormalEpisode.origin==testEpoch&&g_sgAbnormalEpisode.startTick==originalTick,"internal suspend retains original stage origin");
    sgAbnormalPostCompleted(testEvent(507,"<Data Name=\"DurationInUs\">20000000</Data><Data Name=\"DripsResidencyInUs\">1000000</Data><Data Name=\"HwDripsResidencyInUs\">900000</Data><Data Name=\"SleepEntered\">true</Data>"),testFileTime);testPump();
    check(!g_sgAbnormalEpisode.active()&&!testDue,"completed Windows low-power success cancels all future stages");
    testReset();sgAbnormalArm(1,testEpoch);testAt(20000);
    testDeliver(554,"<Data Name=\"DfxTransitionCount\">1</Data><Data Name=\"Ps4TransitionCount\">1</Data>");
    check(g_sgAbnormalEpisode.active(),"native action counts alone do not claim success");
    testDeliver(522,"<Data Name=\"HwDripsTotalTimeValid\">true</Data><Data Name=\"DripsTotalTimeThisPeriodUs\">9000</Data><Data Name=\"HwDripsTotalTimeThisPeriodUs\">8000</Data>");
    check(!g_sgAbnormalEpisode.active()&&!testDue&&!testSleepCalls,"valid Windows effective-low-power evidence ends every remaining action");
    testReset();sgAbnormalArm(1,testEpoch);testAt(20000);sgAbnormalDisplay(1);sgAbnormalTick();
    check(g_sgAbnormalEpisode.active()&&!testSleepCalls,"display change is not classified as manual power-button wake");
    sgAbnormalStopAsync();check(!sgAbnormalPermitted(),"OFF revokes dispatch immediately before UI cancellation");testPump();check(!testDue&&!g_sgAbnormalSubscription,"OFF releases source and timer");
    for(const bool accepted:{false,true}){
        testReset();sgAbnormalArm(1,testEpoch);testBad(30000,30000);testBad(230000,30000);
        testAccepted=false;testBad(330000,30000);check(testSleepCalls==3&&g_sgAutomaticSleepBudget.requests()==3,"two resleeps and S4 share existing three-call budget");
        testPowerOffAccepted=accepted;testBad(340000,10000);testBad(350000,10000);testBad(360000,10000);
        check(testPowerOffCalls==1&&!g_sgAbnormalOrigin.load()&&!testDue&&!g_sgAbnormalSubscription,"shutdown success or failure is terminal with no source or timer");
        sgAbnormalTick();testBad(361000,1000);check(testPowerOffCalls==1,"terminal shutdown never attempts twice");
    }
    testReset();sgAbnormalArm(1,testEpoch);testBad(30000,30000);testBad(230000,30000);testBad(330000,30000);
    testBad(340000,10000);testBad(350000,10000);testBad(360000,10000);
    check(!testPowerOffCalls,"accepted-but-unconfirmed S4 never authorizes shutdown");
    testReset();sgAbnormalArm(1,testEpoch);testBad(30000,30000);testBad(230000,30000);testSuspendOnHibernate=true;testBad(330000,30000);
    check(!g_sgAbnormalEpisode.active()&&!testDue,"Windows hibernate suspend boundary ends pending shutdown");
    testReset();sgAbnormalArm(1,testEpoch);g_sgAutomaticSleepBudget.consume();g_sgAutomaticSleepBudget.consume();g_sgAutomaticSleepBudget.consume();testBad(30000,30000);
    check(!testSleepCalls&&!g_sgAbnormalEpisode.active(),"other repairs consume the same budget without refill");
    testReset();sgAbnormalArm(1,testEpoch);testBad(30000,30000);testBad(230000,30000);testAccepted=false;testBad(330000,30000);
    testWakeDuringPrivilege=true;testBad(340000,10000);testBad(350000,10000);testBad(360000,10000);
    check(!testPowerOffCalls&&!g_sgAbnormalOrigin.load(),"wake racing shutdown privilege wins with no second attempt");
    testReset();sgAbnormalArm(1,testEpoch);testAt(30000);
    testDeliver(161,"<Data Name=\"DurationThisPeriodMs\">1000</Data><Data Name=\"ResiliencyPhaseNonActivatedNoDripsMs\">9000</Data><Data Name=\"NonActivatedCpuTimeMs\">0</Data>");
    check(!testSleepCalls,"cumulative or contradictory watchdog values never become a live failure period");
    testReset();sgAbnormalArm(1,testEpoch);testAt(30000);g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Bad,testEpoch,testEpoch+30000*10000ULL,161},testFileTime);
    testAt(36000);sgAbnormalTick();check(!testSleepCalls,"stale positive evidence cannot authorize action");
    testReset();sgAbnormalArm(1,testEpoch);testAt(30000);testXml=testEvent(161,"<Data Name=\"DurationThisPeriodMs\">30000</Data>");
    for(int i=0;i<20;++i)sgAbnormalEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(4));
    check(g_sgAbnormalPending<=8&&testMessages.size()<=8,"source burst keeps a bounded event queue");sgAbnormalStop("test");check(g_sgAbnormalPending==0,"cancellation drains all owned queued observations");
    testReset();sgAbnormalArm(1,testEpoch);testAt(230000);
    g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Bad,testEpoch+200000*10000ULL,testFileTime,161},testFileTime);
    sgAbnormalPostCompleted(testEvent(507,"<Data Name=\"DurationInUs\">20000000</Data><Data Name=\"DripsResidencyInUs\">1000000</Data><Data Name=\"HwDripsResidencyInUs\">900000</Data><Data Name=\"SleepEntered\">true</Data>"),testEpoch+210000*10000ULL);
    sgAbnormalTick();testPump();check(!testSleepCalls&&!testDue,"late success within original intent cancels newer bad evidence and due stage");
    testReset();sgAbnormalArm(1,testEpoch);testBad(30000,30000);testBad(230000,30000);testAccepted=false;testBad(330000,30000);
    testSuccessDuringPrivilege=true;testBad(340000,10000);testBad(350000,10000);testBad(360000,10000);
    check(!testPowerOffCalls&&!testDue&&!g_sgAbnormalOrigin.load(),"Windows success during shutdown preparation vetoes final API and ends flow");
    testReset();sgAbnormalArm(1,testEpoch);testAccepted=false;testBad(30000,30000);
    check(g_sgInternalSleepRequest.kind==SgRetryKind::None,"rejected automatic request clears only its own marker before next manual sleep");
    testReset();sgAbnormalArm(1,testEpoch);testAccepted=false;testReplaceMarkerDuringRequest=true;testBad(30000,30000);
    check(g_sgInternalSleepRequest.kind==SgRetryKind::UnexpectedWake,"rejected staged request preserves a newer repair owner's marker");
    testReset();testTimerAvailable=false;sgAbnormalArm(1,testEpoch);
    check(!g_sgAbnormalEpisode.active()&&!g_sgAbnormalSubscription,"timer allocation failure releases subscription and episode");
    testReset();sgAbnormalArm(1,testEpoch);testBad(30000,30000);testBad(230000,30000);testAccepted=false;testBad(330000,30000);
    testPrivilege=false;testBad(340000,10000);testBad(350000,10000);testBad(360000,10000);
    check(!testPowerOffCalls&&!testDue&&!g_sgAbnormalOrigin.load()&&!g_sgAbnormalSubscription,"shutdown privilege denial ends flow with no second attempt");
    // A successful OS event must veto a due timer before the UI queue is pumped.
    testReset();sgAbnormalArm(1,testEpoch);testAt(30000);
    g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Bad,testEpoch,testFileTime,161},testFileTime);
    testXml=testEvent(522,"<Data Name=\"HwDripsTotalTimeValid\">true</Data><Data Name=\"DripsTotalTimeThisPeriodUs\">9</Data><Data Name=\"HwDripsTotalTimeThisPeriodUs\">8</Data>");
    sgAbnormalEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(4));
    check(!sgAbnormalPermitted(),"callback-side Windows success immediately revokes dispatch before UI queue");
    sgAbnormalTick();testPump();check(!testSleepCalls&&!testPowerOffCalls&&!testDue,"queued Windows success beats due timer");
    testReset();sgAbnormalArm(1,testEpoch);testAt(30000);
    g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Bad,testEpoch,testFileTime,161},testFileTime);
    sgAbnormalEventCallback(EvtSubscribeActionError,nullptr,reinterpret_cast<EVT_HANDLE>(ERROR_EVT_QUERY_RESULT_STALE));
    sgAbnormalTick();testPump();check(!testSleepCalls&&!testDue,"subscription error immediately vetoes due timer");
    testReset();sgAbnormalArm(1,testEpoch);testBad(30000,1);
    check(!testSleepCalls,"momentary no-DRIPS sample cannot authorize a thirty-second retry");
    testReset();sgAbnormalArm(1,testEpoch);testAt(30000);
    g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Bad,testEpoch,testFileTime,161},testFileTime);
    g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Unknown,0,testFileTime,161},testFileTime);
    sgAbnormalTick();check(!testSleepCalls,"contradictory observation at identical timestamp erases bad evidence");
    testReset();sgAbnormalArm(1,testEpoch);testAt(30000);
    g_sgAbnormalEpisode.observe({ymcc::abnormalsleep::Quality::Bad,testEpoch,testFileTime,161},testFileTime);
    g_sgAbnormalDropped=true;sgAbnormalTick();check(!testSleepCalls&&!testDue,"queue gap vetoes due action before event handling");
    testReset();sgAbnormalArm(1,testEpoch);testAt(30000);testFileTime+=6000*10000ULL;
    sgAbnormalTick();check(!g_sgAbnormalEpisode.active()&&!testDue,"wall-clock drift terminates rather than replaying deadlines");
    for(const auto& c:fixture.at("cases"))if(c.contains("fieldClassification")){
        testReset();sgAbnormalArm(1,testEpoch);testAt(30000);sgAbnormalTick();
        check(!testSleepCalls,"ROG completed507 never becomes live30second failure evidence");
        testAt(static_cast<ULONGLONG>(c.at("historicalReplay").at("powerButtonWakeElapsedMs").get<double>()+1));g_sgLastPowerButtonWakeEventFileTime=testFileTime;sgAbnormalTick();
        testAt(360000);sgAbnormalTick();check(!testPowerOffCalls&&!testSleepCalls,"ROG historical active wake cancels230/330/360 without invented live data");
    }
    for(const unsigned stage:{0U,1U,2U,3U}){
        testReset();sgAbnormalArm(1,testEpoch);
        if(stage>0)testBad(30000,30000);
        if(stage>1)testBad(230000,30000);
        if(stage>2){testAccepted=false;testBad(330000,30000);testBad(340000,10000);testBad(350000,10000);}
        const auto previousSleepCalls=testSleepCalls,previousPowerOffCalls=testPowerOffCalls;
        const auto end=ymcc::abnormalsleep::Episode::deadlines[stage];testAt(end);
        ymcc::abnormalsleep::publishConfirmedWake(g_sgLastPowerButtonWakeEventFileTime,testFileTime);
        testBad(end,stage==3?10000:30000);
        check(!g_sgAbnormalEpisode.active()&&!testDue&&!g_sgAbnormalSubscription,"every30/230/330/360stage terminates on callback-side human wake");
        testAt(900000);sgAbnormalTick();
        check(testSleepCalls==previousSleepCalls&&testPowerOffCalls==previousPowerOffCalls,"stale timers cannot dispatch any power action after human wake");
    }
    testReset();sgAbnormalArm(1,testEpoch);testBad(30000,30000);testBad(230000,30000);testAccepted=false;testBad(330000,30000);
    testBad(340000,10000);testBad(350000,10000);
    runtimeFactObserver=[](const char* event,const json& details){
        if(std::string(event)=="abnormal-sleep-finished"&&details.value("trigger",std::string{})=="normal-poweroff-attempt")
            ymcc::abnormalsleep::publishConfirmedWake(g_sgLastPowerButtonWakeEventFileTime,testFileTime);
    };
    testBad(360000,10000);
    check(!testPowerOffCalls&&!g_sgAbnormalEpisode.active()&&!testDue,"human wake during final shutdown cleanup logging vetoes the only shutdown attempt");
    sgAbnormalTick();check(!testPowerOffCalls,"shutdown veto stays terminal without retry");

    // An authorized parent request already owns one call in the shared budget.
    const auto ownRetry=[](ULONGLONG offset){
        testAt(offset);check(g_sgAutomaticSleepBudget.consume(),"parent owned retry consumes shared budget before observation");
        sgMarkInternalSleepRequest(SgRetryKind::UnexpectedWake);
        return sgAbnormalObserveOwnedRetry(testEpoch,testFileTime);
    };
    const auto health507=[](ULONGLONG duration){return testEvent(507,"<Data Name=\"DurationInUs\">"+std::to_string(duration)+"</Data><Data Name=\"DripsResidencyInUs\">1000</Data><Data Name=\"HwDripsResidencyInUs\">900</Data><Data Name=\"SleepEntered\">true</Data>");};
    testReset();testChannel=false;
    check(!sgAbnormalSourceReady()&&!g_sgAbnormalEpisode.active()&&!testDue&&!testSubscriptions,"read-only disabled source probe changes no episode or listener");
    testChannel=true;check(sgAbnormalSourceReady()&&!testDue&&!testSubscriptions,"parent preparation exposes ready source without persistent subscription");
    sgAbnormalArm(1,testEpoch);testBad(30000,30000);testBad(230000,30000);
    check(testSleepCalls==2&&g_sgAbnormalEpisode.next==2,"log2 prepared source preserves original thirty and230second stages");
    testReset();testChannel=false;sgAbnormalArm(1,testEpoch);testChannel=true;
    check(ownRetry(2038),"log1 authorized UnexpectedWake establishes a fresh bounded observation");
    const auto requestTime=testFileTime,phaseDue=testDue;
    sgAbnormalOwnedRetryResult(testEpoch,requestTime,true);sgAbnormalInternalEntry();
    check(g_sgAbnormalEpisode.active()&&g_sgAbnormalEpisode.originalHumanIntent==testEpoch&&g_sgAbnormalEpisode.origin==requestTime&&testDue==phaseDue,
        "owned accepted request and PBT retain observation without claiming low-power success");
    check(sgAbnormalObserveOwnedRetry(testEpoch,requestTime)&&testDue==phaseDue&&testSubscriptions==1,"duplicate owned request cannot reset phase deadlines or subscribe twice");
    testAt(5000);sgAbnormalPostCompleted(health507(10000000),testFileTime);
    check(sgAbnormalPermitted(),"old507 spanning prior session cannot veto a new owned retry before UI queue");testPump();
    check(g_sgAbnormalEpisode.active(),"old507 delivered after new request cannot terminate the new phase");
    testBad(32038,30000);
    check(testSleepCalls==1&&g_sgAutomaticSleepBudget.requests()==2,"log1 second black-screen activity needs no second507 to reach original thirtysecond retry");
    testBad(232038,30000);check(testSleepCalls==2&&g_sgAutomaticSleepBudget.requests()==3,"owned retry and two staged sleeps share total budget three");
    testBad(332038,30000);check(testSleepCalls==2&&!g_sgAbnormalEpisode.active()&&!testPowerOffCalls,"new owned phase never replenishes shared budget for S4");
    testReset();check(ownRetry(2038),"owned retry missing-evidence control arms");testAt(32038);sgAbnormalTick();
    check(!testSleepCalls&&g_sgAutomaticSleepBudget.requests()==1,"owned retry with missing161 cannot blindly sleep at thirtyseconds");
    testAt(442038);sgAbnormalTick();check(!testDue&&!g_sgAbnormalSubscription,"owned observation expires with no resident timer or source");
    check(!sgAbnormalObserveOwnedRetry(testEpoch,testEpoch+2038*10000ULL),"expired owned request cannot resurrect on duplicate late delivery");
    testReset();testChannel=false;check(!ownRetry(2038)&&!testDue&&!g_sgAbnormalEpisode.active(),"owned retry without reliable source remains unknown and releases observation");
    testReset();testAt(2038);sgMarkInternalSleepRequest(SgRetryKind::UnexpectedWake);
    check(!sgAbnormalObserveOwnedRetry(testEpoch,testFileTime)&&!testDue,"unconsumed parent budget cannot establish an owned observation");
    g_sgAutomaticSleepBudget.consume();sgMarkInternalSleepRequest(SgRetryKind::EntryFailure);
    check(!sgAbnormalObserveOwnedRetry(testEpoch,testFileTime),"wrong retry owner cannot establish a fresh phase");
    for(const bool oldWake:{false,true}){
        testReset();check(ownRetry(2038),"owned manual veto control arms");
        testAt(32038);g_sgLastPowerButtonWakeEventFileTime=oldWake?testEpoch+1000*10000ULL:testFileTime;
        testBad(32038,30000);check(!testSleepCalls&&!testDue&&!g_sgAbnormalEpisode.active(),"manual wake after original intent wins even when timestamp precedes owned request");
    }
    testReset();check(ownRetry(2038),"owned health control arms");testAt(30000);
    sgAbnormalPostCompleted(health507(10000000),testFileTime);
    check(!sgAbnormalPermitted(),"new phase507 health immediately revokes dispatch before queue");testPump();
    check(!testDue&&!g_sgAbnormalSubscription&&!g_sgAbnormalEpisode.active(),"new phase507 health closes all observation resources");
    sgAbnormalOwnedRetryResult(testEpoch,testEpoch+2038*10000ULL,true);check(!testDue,"late accepted result cannot resurrect a healthy terminal phase");
    testReset();check(ownRetry(2038),"owned rejected result control arms");
    sgAbnormalOwnedRetryResult(testEpoch,testFileTime,false);check(!testDue&&!g_sgAbnormalEpisode.active()&&g_sgAutomaticSleepBudget.requests()==1,"owned API rejection terminates observation without refunding budget");
    testReset();check(ownRetry(2038),"owned OFF control arms");g_sgRetryEntryFailure=false;sgAbnormalStopAsync();
    sgAbnormalOwnedRetryResult(testEpoch,testEpoch+2038*10000ULL,true);testPump();check(!testDue&&!testSleepCalls&&!g_sgAbnormalSubscription,"OFF and late API result cannot reactivate owned observation");
    testReset();check(ownRetry(2038),"owned resleep setting control arms");g_sgResleepEnabled=false;testBad(32038,30000);
    check(!testSleepCalls&&!testDue,"owned resleep OFF immediately vetoes staged actions");
    testReset();check(ownRetry(2038),"owned stale result control arms");const auto previousRequest=testFileTime;
    check(ownRetry(4000),"second authorized retry starts new phase using the same shared budget");const auto newerDue=testDue;
    sgAbnormalOwnedRetryResult(testEpoch,previousRequest,false);
    check(g_sgAbnormalEpisode.active()&&g_sgAbnormalEpisode.origin==testFileTime&&testDue==newerDue,"late prior result cannot cancel newer owned phase");
    testReset();check(ownRetry(2038),"owned late data control arms");testAt(100000);
    sgAbnormalPostCompleted(health507(50000000),testEpoch+31038*10000ULL);testPump();
    check(g_sgAbnormalEpisode.active(),"late prior-session507 remains ignored even when delivered long after phase start");
    sgAbnormalPostCompleted(health507(1000000),testEpoch+31038*10000ULL);
    check(!sgAbnormalPermitted(),"late but phase-contained507 healthy result is terminal");testPump();
    testReset();sgAbnormalArm(1,testEpoch);testAt(30000);
    sgAbnormalPostCompleted(testEvent(507,"<Data Name=\"DurationInUs\">352691540</Data><Data Name=\"ActiveResidencyInUs\">352691540</Data><Data Name=\"DripsResidencyInUs\">0</Data><Data Name=\"HwDripsResidencyInUs\">0</Data><Data Name=\"SleepEntered\">true</Data>"),testFileTime);
    testPump();check(!testSleepCalls,"completed zero507 never promotes missing current interval to an action");

    testReset();return checks-initialChecks;
}
