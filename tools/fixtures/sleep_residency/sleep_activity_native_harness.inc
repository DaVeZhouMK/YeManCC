// Existing residency consumer: compile the formal observer with inert OS ports.
// Clock, generation, wake, configuration and fact storage belong to the shared
// abnormal harness above. No independent lifecycle or evidence model lives here.
#include "sleep_activity_evidence.h"
#include "sleep_notification_evidence.h"
static std::atomic<int> g_idleSessionDisplayState{-1},g_idleLegacyMonitorState{-1};
static ymcc::sleepnotifications::Evidence g_sleepNotificationEvidence;
#include <array>
#include <cstring>
namespace sleep_activity_fixture {
using Sample=ymcc::sleepactivity::Sample;
struct Calls {
    unsigned system=0,process=0,unbiased=0,processors=0,groups=0,current=0,tick=0,error=0;
    auto sampling()const{return std::array<unsigned,6>{system,process,unbiased,processors,groups,current};}
    auto all()const{return std::array<unsigned,8>{system,process,unbiased,processors,groups,current,tick,error};}
};
static Calls calls;
static Sample counters;
static bool systemOk=true,processOk=true,unbiasedOk=true;
static DWORD lastError=0;
static ULONGLONG captureAdvanceMs=0;
static void (*duringCapture)()=nullptr;
static FILETIME fileTime(ULONGLONG value){return {static_cast<DWORD>(value),static_cast<DWORD>(value>>32)};}
static BOOL systemTimes(LPFILETIME idle,LPFILETIME kernel,LPFILETIME user){
    ++calls.system;*idle=fileTime(counters.idle100ns);*kernel=fileTime(counters.kernel100ns);*user=fileTime(counters.user100ns);
    // Deliberately leave nonzero output on failure: it must not become evidence.
    const auto hook=duringCapture;duringCapture=nullptr;
    if(hook)hook();
    testTick+=captureAdvanceMs;testFileTime+=captureAdvanceMs*10000;
    if(!systemOk)lastError=ERROR_ACCESS_DENIED;
    return systemOk?TRUE:FALSE;
}
static HANDLE currentProcess(){++calls.current;return reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-17));}
static BOOL processTimes(HANDLE process,LPFILETIME created,LPFILETIME exited,LPFILETIME kernel,LPFILETIME user){
    ++calls.process;
    check(process==reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-17)),"activity process read uses only its inert current-process handle");
    *created=fileTime(counters.processCreated100ns);*exited={};
    *kernel=fileTime(counters.processKernel100ns);*user=fileTime(counters.processUser100ns);
    if(!processOk)lastError=ERROR_INVALID_HANDLE;
    return processOk?TRUE:FALSE;
}
static BOOL unbiasedTime(PULONGLONG value){++calls.unbiased;*value=counters.unbiased100ns;if(!unbiasedOk)lastError=ERROR_NOT_SUPPORTED;return unbiasedOk?TRUE:FALSE;}
static WORD groupCount(){++calls.groups;return static_cast<WORD>(counters.processorGroups);}
static DWORD processorCount(WORD group){++calls.processors;check(group==ALL_PROCESSOR_GROUPS,"activity requests declared all-group processor count");return counters.logicalProcessors;}
static ULONGLONG tick(){++calls.tick;return testTick;}
static DWORD error(){++calls.error;return lastError;}
}
#define GetSystemTimes sleep_activity_fixture::systemTimes
#define GetProcessTimes sleep_activity_fixture::processTimes
#define QueryUnbiasedInterruptTime sleep_activity_fixture::unbiasedTime
#define GetActiveProcessorCount sleep_activity_fixture::processorCount
#define GetActiveProcessorGroupCount sleep_activity_fixture::groupCount
#define GetTickCount64 sleep_activity_fixture::tick
#define GetLastError sleep_activity_fixture::error
#define GetCurrentProcess sleep_activity_fixture::currentProcess
// Keep accidental future action/resource calls inert and observable as well.
#define SetTimer testSetTimer
#define KillTimer testKillTimer
#define ExitWindowsEx testPowerOff
#define CreateThread testCreateThread
#define StartTraceW testStartTrace
#include "sleep_activity_evidence_native.inc"
#undef GetSystemTimes
#undef GetProcessTimes
#undef QueryUnbiasedInterruptTime
#undef GetActiveProcessorCount
#undef GetActiveProcessorGroupCount
#undef GetTickCount64
#undef GetLastError
#undef GetCurrentProcess
#undef SetTimer
#undef KillTimer
#undef ExitWindowsEx
#undef CreateThread
#undef StartTraceW
namespace sleep_activity_fixture {
static unsigned forwardedFacts=0,forwardDepth=0,maxForwardDepth=0,injectedLogFailures=0;
static void throwActivityFact(const char* event,const json&){
    if(event&&std::strcmp(event,"sleep-activity-evidence")==0){++injectedLogFailures;throw std::runtime_error("injected-activity-log-failure");}
}
static void actualFactHook(const char* event,const json& details){
    ++forwardedFacts;++forwardDepth;maxForwardDepth=(std::max)(maxForwardDepth,forwardDepth);
    check(forwardDepth<=2,"activity own evidence cannot recursively produce a second activity record");
#include "production_activity_fact_hook.inc"
    --forwardDepth;
}
static void reset(){
    g_sleepActivityWindow={};g_sleepActivityPendingIntent=0;calls={};counters={};systemOk=processOk=unbiasedOk=true;
    forwardedFacts=forwardDepth=maxForwardDepth=injectedLogFailures=0;g_idleSessionDisplayState=-1;g_idleLegacyMonitorState=-1;
    lastError=0;captureAdvanceMs=0;duringCapture=nullptr;
    testAt(0);testPowerGeneration=771;g_guardEnabled=true;g_exitRequested=false;
    g_sgLastPowerButtonWakeEventFileTime=0;runtimeFactObserver=nullptr;captured.clear();
    counters.idle100ns=0x100000000ULL;counters.kernel100ns=0x200000000ULL;counters.user100ns=0x300000000ULL;
    counters.processCreated100ns=0x400000001ULL;counters.processKernel100ns=0x500000000ULL;
    counters.processUser100ns=0x600000000ULL;counters.unbiased100ns=0x700000000ULL;
    counters.logicalProcessors=4;counters.processorGroups=1;
}
static void advance(ULONGLONG ms){testTick+=ms;testFileTime+=ms*10000;}
static const json& fact(){check(!captured.empty(),"activity expected a formal native fact");return captured.back().second;}
static void expectSamples(unsigned n){
    check(calls.sampling()==std::array<unsigned,6>{n,n,n,n,n,n},"activity each capture reads each declared OS boundary exactly once");
}
static void audit(bool includesOriginalFacts=false){
    check(testSleepCalls==0&&testPowerOffCalls==0&&testTimerWrites==0&&testDue==0&&
        testEtwStartCalls==0&&testEtwThreads==0&&testSubscriptions==0&&testMessages.empty(),
        "activity never requests power, timers, ETW, worker creation or UI messages");
    check(g_sgAutomaticSleepBudget.requests()==2&&g_sgAutomaticSleepTarget==ymcc::sleeprequest::Mode::None&&
        g_sgInternalSleepRequest.kind==SgRetryKind::UnexpectedWake&&
        g_sgInternalSleepRequest.dispatchTick==100123&&g_sgInternalSleepRequest.dispatchFileTime==testEpoch+1230000&&
        g_powerLifecycle==PowerLifecycle::Suspended,"activity cannot change shared power ownership or refill or spend the existing budget");
    std::size_t cycleBytes=0;unsigned summaries=0;
    for(const auto& row:captured){
        if(includesOriginalFacts&&row.first!="sleep-activity-evidence")continue;
        ++summaries;
        const auto& d=row.second;const auto bytes=d.dump().size();cycleBytes+=bytes;
        check(bytes<=8U*1024U,"activity each serialized summary stays within eight KiB including cached display and notification evidence");
        check(row.first=="sleep-activity-evidence"&&d.at("powerActionAuthorized")==false&&
            d.at("physicalPowerAssessment")=="UNKNOWN"&&d.at("unbiasedTimeIsS0SleepWitness")==false&&
            d.at("observationMayIncludeResumeWork")==true&&d.at("mandatoryEvidence")==true&&d.at("detailLogsRequired")==false,
            "activity ordinary fact remains mandatory observation without a physical power or DRIPS claim");
        check(d.at("cachedDisplay").at("scope")=="cached-Windows-notifications;freshness-unverified;not-a-sleep-witness"&&
            d.at("powerNotifications").at("powerActionAuthorized")==false&&d.at("powerNotifications").at("sleepSuccessProven")==false&&
            d.at("powerNotifications").at("userWakeProven")==false&&d.at("powerNotifications").at("snapshotAtomic")==false&&
            d.at("powerNotifications").at("callbackDiskWrites")==false,
            "activity cached display and notification snapshots never become sleep proof, action authority or a coherent atomic Windows history");
        for(const auto* key:{"sinceBegin","sincePrevious"}){
            const auto& interval=d.at(key);
            check(interval.at("powerActionAuthorized")==false&&interval.at("continuousHighCpuProven")==false&&
                interval.at("processCpuScope")=="current-native-process-only;excludes-child-host-WebView-Steam-processes"&&
                interval.at("processCpuIncludesDiagnosticWork")==true&&interval.at("assessment")=="observation-only;physical-power-UNKNOWN"&&
                interval.at("counterScope")=="observed-scheduler-accounting;not-original-Windows-event-interval"&&
                interval.at("unbiasedScope")=="Windows-unbiased-interrupt-time;not-a-S0-or-DRIPS-state-witness",
                "activity intervals never convert scheduler accounting into event-interval or sleep-state proof");
        }
    }
    check(summaries<=5&&cycleBytes<=40U*1024U,"activity a whole cycle has at most five summaries and forty KiB of serialized summary JSON");
}
static void expectUnknownSystem(const json& interval){
    check(interval.at("systemCpuDeltaAvailable")==false&&interval.at("systemBusyCpu100ns").is_null()&&
        interval.at("systemBusyPercentOfCapacity").is_null(),"activity invalid system interval stays unknown including normalized percent");
}
static void expectUnknownProcess(const json& interval){
    check(interval.at("processCpuDeltaAvailable")==false&&interval.at("processCpu100ns").is_null()&&
        interval.at("processCpuPercentOfMachine").is_null()&&interval.at("processCpuEquivalentCores").is_null(),"activity invalid process interval stays null rather than zero");
}
}
static unsigned sleepActivityNativeTests(){
    using namespace sleep_activity_fixture;
    const auto initialChecks=sleepEvidenceChecks;
    static_assert(noexcept(sgSleepActivityEnd("compile-contract")),"activity End must preserve its noexcept boundary");
    testReset();
    check(g_sgAutomaticSleepBudget.consume()&&g_sgAutomaticSleepBudget.consume(),"activity test preserves a partially spent parent budget");
    g_sgInternalSleepRequest={SgRetryKind::UnexpectedWake,100123,testEpoch+1230000};
    reset();
    for(unsigned i=0;i<1000;++i){sgSleepActivityObserve("idle-notification",testFileTime,1);sgSleepActivityEnd("idle-end");(void)sgSleepActivityStatus();}
    check(calls.all()==Calls{}.all()&&captured.empty(),"activity no flow has zero OS reads and zero records under a thousand notifications and status reads");audit();
    for(const auto reason:{-1,0,2,32})sgSleepActivityBegin(reason,testFileTime);
    g_guardEnabled=false;sgSleepActivityBegin(1,testFileTime);g_guardEnabled=true;
    g_exitRequested=true;sgSleepActivityBegin(1,testFileTime);g_exitRequested=false;
    testPowerGeneration=0;sgSleepActivityBegin(1,testFileTime);testPowerGeneration=771;
    check(calls.all()==Calls{}.all()&&captured.empty(),"activity nonhuman begin, OFF, exit and zero generation cannot sample or start");audit();

    reset();sgSleepActivityBegin(1,testFileTime);expectSamples(1);
    const auto first=g_sleepActivityWindow.first;const auto startCalls=calls.all();
    advance(1000);
    for(unsigned i=0;i<1000;++i)sgSleepActivityBegin(1,testFileTime);
    check(calls.all()==startCalls&&captured.size()==1&&g_sleepActivityWindow.intent==testEpoch&&
        g_sleepActivityWindow.first.tickMs==first.tickMs&&g_sleepActivityWindow.first.fileTime==first.fileTime&&
        g_sleepActivityWindow.checkpoints==0,"activity a thousand repeated begins preserve original intent baseline and checkpoint budget without reading OS counters");
    const auto beforeStorm=calls.sampling();const auto ticksBeforeStorm=calls.tick;
    for(unsigned i=0;i<1000;++i){sgSleepActivityObserve("repeat",testFileTime,1);(void)sgSleepActivityStatus();}
    check(calls.sampling()==beforeStorm&&calls.tick==ticksBeforeStorm+1000&&captured.size()==1&&
        g_sleepActivityWindow.seen==1000&&g_sleepActivityWindow.suppressed==1000,
        "activity a thousand repeat notifications only read the throttle clock and never sample or write");
    advance(28999);sgSleepActivityObserve("too-early",testFileTime,1);expectSamples(1);
    check(captured.size()==1,"activity 29999ms cannot consume a checkpoint");
    advance(1);sgSleepActivityObserve("at-30s",testFileTime,1);expectSamples(2);
    check(captured.size()==2&&fact().at("boundary")=="checkpoint"&&fact().at("checkpoints")==1&&g_sleepActivityWindow.checkpoints==1,"activity exactly 30000ms admits one checkpoint");
    sgSleepActivityBegin(1,testFileTime);expectSamples(2);
    advance(29999);sgSleepActivityObserve("next-too-early",testFileTime,1);expectSamples(2);
    advance(1);sgSleepActivityObserve("at-60s",testFileTime,1);expectSamples(3);
    advance(30000);sgSleepActivityObserve("at-90s",testFileTime,1);expectSamples(4);
    for(unsigned i=0;i<1000;++i){advance(30000);sgSleepActivityObserve("over-cap",testFileTime,1);}
    expectSamples(4);check(captured.size()==4&&fact().at("checkpoints")==3&&g_sleepActivityWindow.checkpoints==3,"activity three intermediate summaries remain capped after a thousand later events");
    const auto statusCalls=calls.all();const auto status=sgSleepActivityStatus();
    check(calls.all()==statusCalls&&status.at("active")==true&&status.at("checkpoints")==3&&
        status.at("maxRecordsPerCycle")==5&&status.at("minimumEventSpacingMs")==30000&&status.at("powerActionAuthorized")==false&&
        status.at("sampling")=="existing-UI-power-events-only;no-polling-or-wake-timer"&&status.at("physicalPowerAssessment")=="UNKNOWN",
        "activity status is a read-only bounded event-driven observation without power authority");
    sgSleepActivityEnd("explicit-end",testFileTime,1);expectSamples(5);
    check(captured.size()==5&&captured.front().second.at("boundary")=="begin"&&fact().at("boundary")=="end"&&
        !g_sleepActivityWindow.active()&&g_sleepActivityWindow.retired==testEpoch,"activity begin plus three summaries plus end is exactly five records and retires the original intent");
    const auto endedCalls=calls.all();
    for(unsigned i=0;i<1000;++i){advance(30000);sgSleepActivityObserve("after-end",testFileTime);sgSleepActivityEnd("repeat-end");sgSleepActivityBegin(1,testEpoch);}
    check(calls.all()==endedCalls&&captured.size()==5,"activity a thousand notifications or replayed begins after end never sample or write");audit();

    // Consume the actual fact dispatcher, including its own appended records.
    // No detail-logging port is supplied; ordinary evidence is mandatory.
    reset();runtimeFactObserver=actualFactHook;
    sgSleepActivityOnFact(nullptr,json::object());sgSleepActivityOnFact("user-initiated-sleep",json::array());
    sgSleepActivityOnFact("unrelated",json::object());sgSleepActivityOnFact("sleep-activity-evidence",json::object());
    sgSleepActivityOnFact("s0-intent-accepted",json::object());
    sgSleepActivityOnFact("user-initiated-sleep",{{"reason",3},{"fileTime100ns",testEpoch}});
    sgSleepActivityOnFact("s0-intent-accepted",json::object());
    check(calls.all()==Calls{}.all()&&captured.empty()&&!g_sleepActivityPendingIntent&&!g_sleepActivityWindow.active(),
        "activity actual dispatcher ignores invalid inputs unrelated facts admission without pending and reason3 without sampling");audit();
    for(const auto* admission:{"s0-intent-accepted","s0-intent-joined-existing-suspend","s0-intent-joined-delayed-resume"}){
        reset();runtimeFactObserver=actualFactHook;
        sgSleepActivityOnFact("user-initiated-sleep",{{"reason",1},{"fileTime100ns",testEpoch}});
        check(g_sleepActivityPendingIntent==testEpoch&&!g_sleepActivityWindow.active()&&calls.all()==Calls{}.all()&&captured.empty(),
            "activity actual reason1 fact is pending only and performs zero sampling before admission");
        sgSleepActivityOnFact(admission,{{"generation",testPowerGeneration}});expectSamples(1);
        check(g_sleepActivityWindow.active()&&g_sleepActivityPendingIntent==0&&captured.size()==1&&fact().at("boundary")=="begin"&&
            fact().at("mandatoryEvidence")==true&&fact().at("detailLogsRequired")==false,
            "activity each real accepted or joined fact consumes pending once and emits mandatory begin without a detail-logging dependency");
        const auto admitted=calls.all();for(unsigned i=0;i<1000;++i)sgSleepActivityOnFact(admission,json::object());
        check(calls.all()==admitted&&captured.size()==1,"activity repeated admission facts cannot reuse consumed pending intent");
        advance(30000);
        sgSleepActivityOnFact("s0-wake-message",{{"acceptedUserWake",false},{"latestKernel507FileTime100ns",testFileTime-1000*10000ULL},{"reason",32}});expectSamples(2);
        check(fact().at("boundary")=="checkpoint"&&fact().at("source")=="kernel-power-507-window-message"&&
            fact().at("eventReason")==32&&fact().at("eventDeliveryAgeMs")==1000&&
            fact().at("eventTimeAssociation")=="507-window checkpoints use latest callback high-water; not exact window-message identity","activity nonhuman wake fact observes using its original507 time and reason");
        const auto beforeWake=calls.all();sgSleepActivityOnFact("s0-wake-message",{{"acceptedUserWake",true}});
        check(calls.all()==beforeWake&&g_sleepActivityWindow.active(),"activity accepted wake label alone cannot end a cycle without the original shared wake high-water mark");
        g_sgLastPowerButtonWakeEventFileTime=testFileTime;
        sgSleepActivityOnFact("s0-wake-message",{{"acceptedUserWake",true}});expectSamples(3);
        check(!g_sleepActivityWindow.active()&&captured.size()==3&&fact().at("boundary")=="end"&&fact().at("source")=="power-button-wake"&&
            forwardedFacts==3&&maxForwardDepth==1,"activity own begin checkpoint and end records reenter the real dispatcher once each without recursive sampling or logging");audit();
    }
    for(const unsigned rejected:{0U,1U,2U,3U}){
        reset();sgSleepActivityOnFact("user-initiated-sleep",{{"reason",1},{"fileTime100ns",testEpoch}});
        if(rejected==0)sgSleepActivityOnFact("s0-intent-ignored",json::object());
        if(rejected==1)sgSleepActivityOnFact("user-initiated-sleep",{{"reason",3},{"fileTime100ns",testEpoch}});
        if(rejected==2)sgSleepActivityOnFact("user-initiated-sleep",{{"reason",1}});
        if(rejected==3)sgSleepActivityOnFact("s0-wake-message",{{"acceptedUserWake",true}});
        sgSleepActivityOnFact("s0-intent-accepted",json::object());
        check(g_sleepActivityPendingIntent==0&&!g_sleepActivityWindow.active()&&calls.all()==Calls{}.all()&&captured.empty(),
            "activity ignored intent replacement reason3 missing timestamp or accepted wake clears pending before later admission");audit();
    }
    for(const unsigned code:{4U,7U,18U}){
        reset();runtimeFactObserver=actualFactHook;
        sgSleepActivityOnFact("user-initiated-sleep",{{"reason",1},{"fileTime100ns",testEpoch}});
        sgSleepActivityOnFact("s0-intent-accepted",json::object());advance(30000);
        const auto beforeBroadcast=calls.all();sgSleepActivityOnFact("power-broadcast",{{"code",10}});
        check(calls.all()==beforeBroadcast&&captured.size()==1,"activity unrelated power broadcast does not even read the throttle clock");
        sgSleepActivityOnFact("power-broadcast",{{"code",code}});expectSamples(2);
        check(captured.size()==2&&fact().at("boundary")=="checkpoint"&&fact().at("source")=="power-broadcast"&&
            fact().at("eventReason")==code&&fact().at("eventFileTime").is_null()&&forwardedFacts==2&&maxForwardDepth==1,
            "activity broadcast4 7 and18 route one bounded observation without inventing a Windows event timestamp or recursive records");audit();
    }

    // Drive fake sgRecordFact itself: it stores the original first and then
    // invokes the exact derived main.cpp try/catch through its existing pointer.
    reset();runtimeFactObserver=actualFactHook;
    const json originalIntent={{"reason",1},{"fileTime100ns",testEpoch},{"originalFactSentinel","retained"}};
    sgRecordFact("user-initiated-sleep",originalIntent);
    check(captured.size()==1&&captured.front().second==originalIntent&&g_sleepActivityPendingIntent==testEpoch&&calls.all()==Calls{}.all(),
        "activity actual main hook keeps the original fact and only records pending before admission");
    const json admission={{"generation",testPowerGeneration}};sgRecordFact("s0-intent-accepted",admission);expectSamples(1);
    check(captured.size()==3&&captured[1].first=="s0-intent-accepted"&&captured[1].second==admission&&
        captured[2].first=="sleep-activity-evidence"&&captured[2].second.at("boundary")=="begin"&&forwardedFacts==3&&maxForwardDepth==2,
        "activity extracted production dispatch appends begin after the admitted original fact and reenters once without recursive sampling");
    const auto beforeBadFacts=calls.all();
    const json unknown={{"unrecognized",true}},badIntent={{"reason","bad-int"}},badWake={{"acceptedUserWake","bad-bool"}};
    sgRecordFact("unknown-original",unknown);sgRecordFact("user-initiated-sleep",badIntent);
    sgRecordFact("s0-wake-message",badWake);sgRecordFact("power-broadcast",json::array({1,2,3}));
    check(calls.all()==beforeBadFacts&&captured.size()==7&&captured[3].second==unknown&&captured[4].second==badIntent&&
        captured[5].second==badWake&&captured[6].second==json::array({1,2,3})&&g_sleepActivityWindow.active(),
        "activity original try/catch isolates malformed detail exceptions and unknown facts after each original fact is preserved");
    advance(30000);
    sgRecordFact("s0-wake-message",{{"acceptedUserWake",false},{"latestKernel507FileTime100ns",testFileTime-1000*10000ULL},{"reason",32}});expectSamples(2);
    check(captured.size()==9&&fact().at("boundary")=="checkpoint"&&fact().at("eventDeliveryAgeMs")==1000,
        "activity original main dispatch consumes the latest callback high-water as an explicitly qualified observation");
    g_sgLastPowerButtonWakeEventFileTime=testFileTime;sgRecordFact("s0-wake-message",{{"acceptedUserWake",true}});expectSamples(3);
    check(captured.size()==11&&fact().at("boundary")=="end"&&!g_sleepActivityWindow.active()&&g_sleepActivityPendingIntent==0,
        "activity original main dispatch emits terminal wake evidence after preserving the wake fact");
    const auto afterHookEnd=calls.all();sgRecordFact("power-broadcast",{{"code",18}});
    check(calls.all()==afterHookEnd&&captured.size()==12&&forwardedFacts==12&&maxForwardDepth==2,
        "activity main dispatch never resamples after end and each original or appended fact reaches the same hook once");audit(true);
    for(const unsigned terminal:{0U,1U,2U,3U}){
        reset();runtimeFactObserver=actualFactHook;
        sgRecordFact("user-initiated-sleep",{{"reason",1},{"fileTime100ns",testEpoch}});
        sgRecordFact("s0-intent-accepted",{{"generation",testPowerGeneration}});advance(1000);
        sgRecordFact("user-initiated-sleep",{{"reason",1},{"fileTime100ns",testFileTime}});
        if(terminal==0)sgRecordFact("s4-wake-message",json::object());
        else sgRecordFact("power-broadcast",{{"code",terminal==1?2:terminal==2?3:6}});
        expectSamples(2);
        check(captured.size()==6&&fact().at("boundary")=="end"&&fact().at("source")== (terminal==0?"kernel-s4-wake":"power-transition-ended")&&
            !g_sleepActivityWindow.active()&&g_sleepActivityPendingIntent==0,
            "activity original fact hook retires S4 wake and broadcast2 3 or6 with one final end and clears pending");
        const auto terminalCalls=calls.all();
        for(unsigned i=0;i<1000;++i){advance(30000);sgSleepActivityObserve("after-terminal",testFileTime);}
        sgRecordFact("s0-intent-accepted",json::object());
        check(calls.all()==terminalCalls&&captured.size()==7&&captured[5].first=="sleep-activity-evidence"&&maxForwardDepth==2,
            "activity all four terminal routes leave the last activity record at end and prevent later sampling or pending resurrection");audit(true);
    }
    for(const auto* ending:{"guard-disabled","application-exit"}){
        reset();sgSleepActivityOnFact("user-initiated-sleep",{{"reason",1},{"fileTime100ns",testEpoch}});
        sgSleepActivityEnd(ending);sgSleepActivityOnFact("s0-intent-accepted",json::object());
        check(g_sleepActivityPendingIntent==0&&!g_sleepActivityWindow.active()&&captured.empty()&&calls.all()==Calls{}.all(),
            "activity OFF or exit end clears a pending unadmitted identity without sampling");audit();
    }

    // Admission uses the original event timestamp, not the delivery clock.
    for(const unsigned mode:{0U,1U,2U,3U,4U}){
        reset();ULONGLONG original=testEpoch;
        if(mode==0)original=0;
        if(mode==1)original=testEpoch+1;
        if(mode==2)advance(10001);
        if(mode==3)g_sgLastPowerButtonWakeEventFileTime=testEpoch;
        if(mode==4)g_sleepActivityWindow.retired=testEpoch;
        sgSleepActivityBegin(1,original);
        check(!g_sleepActivityWindow.active()&&captured.empty()&&calls.all()==Calls{}.all(),
            "activity missing, future, expired, pre-wake and retired original timestamps are rejected before sampling");audit();
    }
    reset();advance(10000);sgSleepActivityBegin(1,testEpoch);expectSamples(1);
    check(g_sleepActivityWindow.intent==testEpoch&&fact().at("eventDeliveryAgeMs")==10000,"activity exact ten-second admission retains the original intent and delivery age");audit();
    reset();sgSleepActivityBegin(1,testEpoch);advance(1);g_sgLastPowerButtonWakeEventFileTime=testFileTime;
    sgSleepActivityObserve("manual-wake",testFileTime,1);expectSamples(2);
    check(fact().at("boundary")=="end"&&fact().at("source")=="power-button-wake"&&!g_sleepActivityWindow.active(),"activity manual wake records one terminal sample and retires immediately");
    const auto wakeCalls=calls.all();
    for(unsigned i=0;i<1000;++i){sgSleepActivityObserve("late-after-wake",testFileTime);sgSleepActivityEnd("late-end");sgSleepActivityBegin(1,testEpoch);}
    check(calls.all()==wakeCalls&&captured.size()==2,"activity after manual wake no event can sample or reopen the prior session");audit();
    advance(1);sgSleepActivityBegin(1,testFileTime);expectSamples(3);
    check(g_sleepActivityWindow.active()&&g_sleepActivityWindow.intent==testFileTime,"activity a strictly newer human intent may start its own cycle after wake");audit();

    for(const unsigned context:{0U,1U,2U}){
        reset();sgSleepActivityBegin(1,testEpoch);
        if(context==0)g_guardEnabled=false;
        if(context==1)g_exitRequested=true;
        if(context==2)++testPowerGeneration;
        advance(30000);sgSleepActivityObserve("context-change",testFileTime);expectSamples(2);
        check(!g_sleepActivityWindow.active()&&captured.size()==2&&fact().at("boundary")=="end"&&fact().at("source")=="context-ended"&&
            fact().at("generation")==771&&fact().at("currentGeneration")==testPowerGeneration,
            "activity OFF, exit or generation replacement ends the original cycle without a checkpoint");
        const auto terminal=calls.all();for(unsigned i=0;i<1000;++i)sgSleepActivityObserve("after-context",testFileTime);
        check(calls.all()==terminal&&captured.size()==2,"activity context termination prevents all subsequent sampling");audit();
    }
    reset();sgSleepActivityBegin(1,testEpoch);advance(20000);++testPowerGeneration;sgSleepActivityBegin(1,testEpoch);expectSamples(2);
    check(!g_sleepActivityWindow.active()&&fact().at("source")=="generation-superseded"&&captured.size()==2,
        "activity begin in a new generation ends old ownership and rejects the old original intent");
    sgSleepActivityBegin(1,testFileTime);expectSamples(3);
    check(g_sleepActivityWindow.generation==772&&g_sleepActivityWindow.intent==testFileTime,"activity replacement generation admits only a fresh human intent");
    advance(30000);sgSleepActivityObserve("previous-session-event",testEpoch,1);expectSamples(3);
    check(captured.size()==3&&g_sleepActivityWindow.checkpoints==0&&g_sleepActivityWindow.suppressed==1,"activity a previous-session event cannot claim a current-cycle sample even after throttle expiry");audit();

    // Inject each revocation inside the real GetSystemTimes call. Recheck after
    // capture must win, with no second capture for the terminal boundary.
    for(const unsigned boundary:{0U,1U})for(const unsigned veto:{0U,1U,2U,3U}){
        reset();if(boundary==1){sgSleepActivityBegin(1,testEpoch);advance(30000);}
        if(veto==0)duringCapture=[](){g_sgLastPowerButtonWakeEventFileTime=testFileTime;};
        if(veto==1)duringCapture=[](){g_guardEnabled=false;};
        if(veto==2)duringCapture=[](){g_exitRequested=true;};
        if(veto==3)duringCapture=[](){++testPowerGeneration;};
        if(boundary==0)sgSleepActivityBegin(1,testEpoch);else sgSleepActivityObserve("veto-during-capture",testFileTime,1);
        expectSamples(boundary==0?1:2);
        check(!g_sleepActivityWindow.active()&&captured.size()==(boundary==0?0U:2U),"activity capture-time human wake, OFF, exit or replacement vetoes admission or a pending checkpoint");
        if(boundary==1)check(fact().at("boundary")=="end"&&fact().at("source")== (veto==0?"power-button-wake":"context-ended"),"activity revoked checkpoint reuses captured sample for exactly one end");
        const auto vetoCalls=calls.all();sgSleepActivityObserve("after-capture-veto",testFileTime);sgSleepActivityEnd("after-veto");
        check(calls.all()==vetoCalls,"activity capture-time veto leaves no active sampler");audit();
    }

    // Logging failure must never reopen a terminal owner or refund sampling slots.
    reset();runtimeFactObserver=throwActivityFact;sgSleepActivityBegin(1,testEpoch);expectSamples(1);
    check(injectedLogFailures==1&&!g_sleepActivityWindow.active()&&g_sleepActivityWindow.retired==testEpoch,
        "activity failed begin record retires its original intent without propagating the logging exception");
    const auto failedBeginCalls=calls.all();
    for(unsigned i=0;i<1000;++i){sgSleepActivityObserve("after-begin-log-failure",testFileTime);sgSleepActivityBegin(1,testEpoch);}
    check(calls.all()==failedBeginCalls&&injectedLogFailures==1&&captured.size()==1,
        "activity begin logging failure cannot resample or retry under a thousand repeated notifications");audit();
    reset();sgSleepActivityBegin(1,testEpoch);g_sleepActivityPendingIntent=testEpoch+1;runtimeFactObserver=throwActivityFact;
    sgSleepActivityEnd("end-log-failure");expectSamples(2);
    check(injectedLogFailures==1&&!g_sleepActivityWindow.active()&&g_sleepActivityWindow.retired==testEpoch&&g_sleepActivityPendingIntent==0,
        "activity failed terminal record still retires ownership clears pending and respects noexcept End");
    const auto failedEndCalls=calls.all();
    for(unsigned i=0;i<1000;++i){sgSleepActivityObserve("after-end-log-failure",testFileTime);sgSleepActivityEnd("repeat-failed-end");}
    check(calls.all()==failedEndCalls&&injectedLogFailures==1&&captured.size()==2,
        "activity end logging failure leaves no active sampler or recurring write attempts");audit();
    reset();sgSleepActivityBegin(1,testEpoch);runtimeFactObserver=throwActivityFact;
    for(unsigned slot=0;slot<3;++slot){
        advance(30000);counters.processKernel100ns+=1000000;
        sgSleepActivityObserve("checkpoint-log-failure",testFileTime);expectSamples(slot+2);
        check(g_sleepActivityWindow.active()&&g_sleepActivityWindow.checkpoints==slot+1&&g_sleepActivityWindow.last.tickMs==testTick&&
            injectedLogFailures==slot+1&&fact().at("checkpoints")==slot+1&&
            fact().at("sincePrevious").at("observationElapsedMs")==30000&&fact().at("sincePrevious").at("processCpu100ns")==1000000&&
            fact().at("sinceBegin").at("observationElapsedMs")==30000ULL*(slot+1),
            "activity failed checkpoint record keeps its reserved slot baseline and previous interval while reporting real checkpoint1 2 or3");
        const auto failedCheckpointSamples=calls.sampling();const auto attempted=captured.size();
        for(unsigned i=0;i<1000;++i)sgSleepActivityObserve("repeat-failed-checkpoint",testFileTime);
        check(calls.sampling()==failedCheckpointSamples&&captured.size()==attempted&&injectedLogFailures==slot+1,
            "activity a thousand notifications after each failed checkpoint cannot refund its throttle or cause another sample or write");
    }
    for(unsigned i=0;i<1000;++i){advance(30000);sgSleepActivityObserve("past-failed-checkpoint-cap",testFileTime);}
    expectSamples(4);check(g_sleepActivityWindow.checkpoints==3&&captured.size()==4&&injectedLogFailures==3,
        "activity three failed checkpoint writes still exhaust all three slots and a thousand arbitrarily late events cannot resample");
    sgSleepActivityEnd("end-after-checkpoint-log-failures");expectSamples(5);
    check(!g_sleepActivityWindow.active()&&injectedLogFailures==4&&captured.size()==5,
        "activity bounded cycle still retires after a failing final write without changing power actions or shared budget");audit();

    // Admission age is rechecked after OS capture, independently of quality.
    reset();captureAdvanceMs=11001;sgSleepActivityBegin(1,testEpoch);expectSamples(1);
    check(testFileTime==testEpoch+11001*10000ULL&&!g_sleepActivityWindow.active()&&captured.empty(),
        "activity capture lasting11001ms expires the original intent before begin and cannot publish a stale start");
    const auto expiredCaptureCalls=calls.all();
    for(unsigned i=0;i<1000;++i){
        sgSleepActivityBegin(1,testEpoch);sgSleepActivityObserve("expired-capture-repeat",testEpoch);sgSleepActivityEnd("expired-capture-end");
    }
    check(calls.all()==expiredCaptureCalls&&captured.empty()&&!g_sleepActivityWindow.active(),
        "activity a thousand repeats after post-capture expiry reject before any additional sample or write");audit();

    // Exercise both interval endpoints through actual native captures. A slow
    // begin is still admissible below10s; its raw data cannot authorize rates.
    check(ymcc::sleepactivity::maximumCaptureSpanMs==1000,"activity diagnostic capture quality boundary remains exactly1000ms");
    const auto expectCaptureRaw=[&](ULONGLONG span){
        const auto& raw=fact().at("observed");
        check(raw.at("captureCostMs")==span&&raw.at("captureSufficientlyBounded")== (span<=1000)&&
            raw.at("systemTimesAvailable")==true&&raw.at("processTimesAvailable")==true&&raw.at("unbiasedTimeAvailable")==true&&
            raw.at("systemIdle100ns")==counters.idle100ns&&raw.at("systemKernelIncludesIdle100ns")==counters.kernel100ns&&
            raw.at("systemUser100ns")==counters.user100ns&&raw.at("processCreated100ns")==counters.processCreated100ns&&
            raw.at("processKernel100ns")==counters.processKernel100ns&&raw.at("processUser100ns")==counters.processUser100ns&&
            raw.at("unbiasedTime100ns")==counters.unbiased100ns,
            "activity bounded or stretched capture preserves all successful raw counters and reports the exact diagnostic capture span");
    };
    const auto expectCaptureInterval=[&](const json& interval,bool bounded){
        check(interval.at("captureSufficientlyBounded")==bounded&&interval.at("observationElapsedMs").get<ULONGLONG>()>0&&
            !interval.at("wallElapsedMs").is_null()&&interval.at("wallClockConsistent")==true,
            "activity capture quality preserves observed elapsed clocks while qualifying the pair of snapshots");
        if(bounded){
            check(interval.at("systemCpuDeltaAvailable")==true&&!interval.at("systemBusyCpu100ns").is_null()&&
                !interval.at("systemBusyPercentOfCapacity").is_null()&&interval.at("processCpuDeltaAvailable")==true&&
                !interval.at("processCpu100ns").is_null()&&!interval.at("processCpuPercentOfMachine").is_null()&&
                !interval.at("processCpuEquivalentCores").is_null()&&!interval.at("unbiasedDelta100ns").is_null(),
                "activity snapshots at the exact1000ms quality boundary still permit known CPU and unbiased interval accounting");
        }else{
            expectUnknownSystem(interval);expectUnknownProcess(interval);
            check(interval.at("unbiasedDelta100ns").is_null(),
                "activity either endpoint taking1001ms suppresses unbiased interval accounting as well as all CPU deltas and percentages");
        }
    };
    const auto advanceCaptureCounters=[&](){
        advance(30000);counters.idle100ns+=1000000;counters.kernel100ns+=2000000;counters.user100ns+=1000000;
        counters.processKernel100ns+=100000;counters.processUser100ns+=100000;counters.unbiased100ns+=300000000;
    };
    for(const ULONGLONG span:{1000ULL,1001ULL})for(const unsigned stretchedAt:{0U,1U,2U}){
        reset();captureAdvanceMs=stretchedAt==0?span:0;sgSleepActivityBegin(1,testEpoch);expectSamples(1);expectCaptureRaw(captureAdvanceMs);
        check(g_sleepActivityWindow.active()&&captured.size()==1,
            "activity1000ms or1001ms capture quality alone never changes a fresh human-intent admission");
        advanceCaptureCounters();captureAdvanceMs=stretchedAt==1?span:0;
        sgSleepActivityObserve("capture-span-checkpoint",testFileTime);expectSamples(2);expectCaptureRaw(captureAdvanceMs);
        const bool firstIntervalBounded=span==1000||stretchedAt==2;
        expectCaptureInterval(fact().at("sinceBegin"),firstIntervalBounded);expectCaptureInterval(fact().at("sincePrevious"),firstIntervalBounded);
        check(g_sleepActivityWindow.active()&&g_sleepActivityWindow.checkpoints==1&&fact().at("checkpoints")==1,
            "activity stretched checkpoint remains a consumed observation slot without changing active ownership");
        advanceCaptureCounters();captureAdvanceMs=0;
        sgSleepActivityObserve("capture-span-next-checkpoint",testFileTime);expectSamples(3);expectCaptureRaw(0);
        expectCaptureInterval(fact().at("sinceBegin"),span==1000||stretchedAt!=0);
        expectCaptureInterval(fact().at("sincePrevious"),span==1000||stretchedAt!=1);
        check(g_sleepActivityWindow.active()&&g_sleepActivityWindow.checkpoints==2,
            "activity next fast checkpoint evaluates the original and previous capture quality independently");
        advanceCaptureCounters();captureAdvanceMs=stretchedAt==2?span:0;
        sgSleepActivityEnd("capture-span-end",testFileTime);expectSamples(4);expectCaptureRaw(captureAdvanceMs);
        expectCaptureInterval(fact().at("sinceBegin"),span==1000||stretchedAt==1);
        expectCaptureInterval(fact().at("sincePrevious"),span==1000||stretchedAt!=2);
        check(!g_sleepActivityWindow.active()&&g_sleepActivityWindow.retired==testEpoch&&g_sleepActivityPendingIntent==0&&
            captured.size()==4&&fact().at("boundary")=="end",
            "activity stretched final capture keeps raw evidence and safely retires the cycle without authorizing power actions");
        const auto afterCaptureEnd=calls.all();
        for(unsigned i=0;i<1000;++i){sgSleepActivityObserve("after-capture-span-end",testFileTime);sgSleepActivityEnd("repeat-capture-span-end");}
        check(calls.all()==afterCaptureEnd&&captured.size()==4,
            "activity capture quality failure never resurrects sampling after terminal retirement");audit();
    }

    // Formal raw and interval code consume high-word FILETIME counters directly.
    reset();captureAdvanceMs=7;advance(1200);sgSleepActivityBegin(1,testEpoch);
    const auto beginObserved=g_sleepActivityWindow.first;
    check(fact().at("originalHumanIntent100ns")==testEpoch&&fact().at("eventFileTime")==sgFormatFactFileTime(testEpoch)&&
        fact().at("observed").at("observedFileTime100ns")==testEpoch+1200*10000ULL&&
        fact().at("observed").at("observedBootTickMs")==101200&&fact().at("observed").at("captureCostMs")==7&&
        fact().at("eventDeliveryAgeMs")==1200,"activity separates original event time, pre-capture observation time and measured capture cost");
    for(const auto* key:{"systemIdle100ns","systemKernelIncludesIdle100ns","systemUser100ns","processCreated100ns","processKernel100ns","processUser100ns","unbiasedTime100ns"})
        check(fact().at("observed").at(key).get<ULONGLONG>()>0xffffffffULL,"activity raw FILETIME joins retain the full high and low 32-bit words");
    captureAdvanceMs=0;advance(29993);
    counters.idle100ns+=20000000;counters.kernel100ns+=40000000;counters.user100ns+=10000000;
    counters.processKernel100ns+=2000000;counters.processUser100ns+=3000000;counters.unbiased100ns+=300000000;
    const auto checkpointEvent=testFileTime-4000*10000ULL;sgSleepActivityObserve("delivered-resume",checkpointEvent,32);
    const auto interval=fact().at("sinceBegin");
    check(fact().at("originalHumanIntent100ns")==testEpoch&&fact().at("eventReason")==32&&fact().at("eventDeliveryAgeMs")==4000&&
        fact().at("observed").at("observedFileTime100ns")==testFileTime&&interval.at("observationElapsedMs")==30000&&interval.at("wallElapsedMs")==30000,
        "activity checkpoint keeps raw observation clocks separate from the delayed Windows event and human-intent interval");
    check(interval.at("systemCpuDeltaAvailable")==true&&interval.at("systemBusyCpu100ns")==30000000&&
        std::abs(interval.at("systemBusyPercentOfCapacity").get<double>()-2.5)<0.000001&&
        interval.at("processCpuDeltaAvailable")==true&&interval.at("processCpu100ns")==5000000&&
        std::abs(interval.at("processCpuEquivalentCores").get<double>()-1.0/60.0)<0.000001&&
        std::abs(interval.at("processCpuPercentOfMachine").get<double>()-5.0/12.0)<0.000001&&interval.at("continuousHighCpuProven")==false&&interval.at("unbiasedDelta100ns")==300000000&&
        interval.at("wallClockConsistent")==true,"activity known single-group CPU arithmetic subtracts idle and normalizes by actual observed interval and capacity");
    advance(30000);counters.processKernel100ns+=1000000;sgSleepActivityEnd("end-without-event");
    check(fact().at("eventFileTime").is_null()&&fact().at("eventDeliveryAgeMs").is_null()&&
        fact().at("sinceBegin").at("observationElapsedMs")==60000&&fact().at("sincePrevious").at("observationElapsedMs")==30000&&
        fact().at("sinceBegin").at("processCpu100ns")==6000000&&fact().at("sincePrevious").at("processCpu100ns")==1000000&&
        g_sleepActivityWindow.first.fileTime==beginObserved.fileTime,"activity end distinguishes whole-cycle and previous-sample deltas without fabricating a missing Windows event");
    check(calls.error==0,"activity successful OS calls never consume a stale GetLastError");audit();

    for(const unsigned failed:{0U,1U,2U,3U}){
        reset();sgSleepActivityBegin(1,testEpoch);advance(30000);
        systemOk=failed!=0&&failed!=3;processOk=failed!=1&&failed!=3;unbiasedOk=failed!=2&&failed!=3;
        sgSleepActivityObserve("OS-failure",testFileTime);
        const auto& raw=fact().at("observed");const auto& span=fact().at("sinceBegin");
        if(!systemOk){
            check(raw.at("systemTimesAvailable")==false&&raw.at("systemTimesError")==ERROR_ACCESS_DENIED&&
                raw.at("systemIdle100ns").is_null()&&raw.at("systemKernelIncludesIdle100ns").is_null()&&raw.at("systemUser100ns").is_null(),"activity failed GetSystemTimes retains error and nulls poisoned outputs");expectUnknownSystem(span);
        }else check(raw.at("systemTimesAvailable")==true&&span.at("systemCpuDeltaAvailable")==true,"activity unrelated OS failure preserves valid system deltas");
        if(!processOk){
            check(raw.at("processTimesAvailable")==false&&raw.at("processTimesError")==ERROR_INVALID_HANDLE&&
                raw.at("processCreated100ns").is_null()&&raw.at("processKernel100ns").is_null()&&raw.at("processUser100ns").is_null(),"activity failed GetProcessTimes retains error and nulls poisoned outputs");expectUnknownProcess(span);
        }else check(raw.at("processTimesAvailable")==true&&span.at("processCpuDeltaAvailable")==true,"activity unrelated OS failure preserves valid process deltas");
        if(!unbiasedOk)check(raw.at("unbiasedTimeAvailable")==false&&raw.at("unbiasedTimeError")==ERROR_NOT_SUPPORTED&&raw.at("unbiasedTime100ns").is_null()&&span.at("unbiasedDelta100ns").is_null(),"activity failed unbiased query is unknown rather than fabricated zero");
        else check(raw.at("unbiasedTimeAvailable")==true&&span.at("unbiasedDelta100ns")==0,"activity a successful unchanged unbiased counter remains a known zero");
        check(calls.error==(failed==3?3U:1U),"activity GetLastError is read exactly once immediately per failed query");expectSamples(2);audit();
    }
    reset();systemOk=processOk=unbiasedOk=false;sgSleepActivityBegin(1,testEpoch);advance(30000);
    systemOk=processOk=unbiasedOk=true;sgSleepActivityObserve("recovered-OS",testFileTime);
    expectUnknownSystem(fact().at("sinceBegin"));expectUnknownProcess(fact().at("sinceBegin"));
    check(fact().at("sinceBegin").at("unbiasedDelta100ns").is_null(),"activity valid endpoint cannot invent a missing baseline counter");audit();

    for(const unsigned invalid:{0U,1U,2U,3U,4U,5U,6U,7U,8U,9U,10U,11U,12U,13U,14U}){
        reset();if(invalid==12)counters.processCreated100ns=0;sgSleepActivityBegin(1,testEpoch);advance(30000);
        if(invalid==0)--counters.idle100ns;
        if(invalid==1)--counters.kernel100ns;
        if(invalid==2)--counters.user100ns;
        if(invalid==3)++counters.idle100ns; // Idle cannot exceed total kernel+user delta.
        if(invalid==4)--counters.processKernel100ns;
        if(invalid==5)--counters.processUser100ns;
        if(invalid==6)++counters.processCreated100ns;
        if(invalid==7)--counters.unbiased100ns;
        if(invalid==8)counters.processorGroups=2;
        if(invalid==9)counters.logicalProcessors=8;
        if(invalid==10)counters.logicalProcessors=0;
        if(invalid==11)counters.processorGroups=0;
        if(invalid==13)counters.kernel100ns=counters.user100ns=(std::numeric_limits<ymcc::sleepactivity::U64>::max)();
        if(invalid==14)counters.processKernel100ns=counters.processUser100ns=(std::numeric_limits<ymcc::sleepactivity::U64>::max)();
        sgSleepActivityObserve("counter-invalid",testFileTime);
        const auto& span=fact().at("sinceBegin");
        if(invalid<=3||(invalid>=8&&invalid<=11)||invalid==13)expectUnknownSystem(span);
        if((invalid>=4&&invalid<=6)||invalid==12||invalid==14)expectUnknownProcess(span);
        if(invalid==7)check(span.at("unbiasedDelta100ns").is_null(),"activity unbiased counter rollback remains unknown");
        if(invalid==8)check(fact().at("observed").at("processorGroups")==2&&span.at("systemCpuScope")=="single-processor-group-only;multi-group-deltas-unverified", "activity multi-group raw topology remains visible without a fabricated system percent");audit();
    }
    reset();counters.processorGroups=2;sgSleepActivityBegin(1,testEpoch);advance(30000);counters.processorGroups=1;
    sgSleepActivityObserve("group-topology-changed",testFileTime);expectUnknownSystem(fact().at("sinceBegin"));audit();

    for(const unsigned clock:{0U,1U,2U,3U}){
        reset();sgSleepActivityBegin(1,testEpoch);advance(30000);
        if(clock==0)testTick=99999;
        if(clock==1)testFileTime=testEpoch-1;
        if(clock==2)testFileTime+=6000*10000ULL;
        // A clock rollback must still allow the terminal sample to retire ownership.
        if(clock==3){testTick=100000;testFileTime=testEpoch;}
        sgSleepActivityEnd("clock-anomaly");
        const auto& span=fact().at("sinceBegin");
        if(clock<2){check(span.at("observationElapsedMs").is_null()&&span.at("wallElapsedMs").is_null()&&span.at("wallClockConsistent")==false,"activity rollback in either observation clock marks the whole interval unknown");expectUnknownSystem(span);expectUnknownProcess(span);}
        if(clock==2)check(span.at("wallClockConsistent")==false&&span.at("systemBusyPercentOfCapacity").is_null()&&span.at("processCpuPercentOfMachine").is_null()&&span.at("processCpuEquivalentCores").is_null(),"activity inconsistent wall versus monotonic clocks cannot yield a normalized percent");
        if(clock==3)check(span.at("observationElapsedMs")==0&&span.at("systemBusyPercentOfCapacity").is_null()&&span.at("processCpuPercentOfMachine").is_null()&&span.at("processCpuEquivalentCores").is_null(),"activity a zero-length interval cannot divide by zero into a percent");
        check(!g_sleepActivityWindow.active(),"activity clock anomalies cannot keep a terminated cycle alive");audit();
    }
    reset();sgSleepActivityBegin(1,testEpoch);testTick=99999;sgSleepActivityObserve("tick-rollback",testFileTime);expectSamples(1);
    check(captured.size()==1&&g_sleepActivityWindow.suppressed==1,"activity throttle rejects a regressed monotonic clock without sampling");audit();
    reset();sgSleepActivityBegin(1,testEpoch);advance(30000);counters.kernel100ns+=2000000000ULL;
    sgSleepActivityObserve("capacity-overflow",testFileTime);
    check(fact().at("sinceBegin").at("systemCpuDeltaAvailable")==true&&fact().at("sinceBegin").at("systemBusyCpu100ns")==2000000000ULL&&
        fact().at("sinceBegin").at("systemBusyPercentOfCapacity").is_null(),"activity counters exceeding observed CPU capacity preserve raw delta without inventing a valid percent");audit();
    reset();counters.processorGroups=2;sgSleepActivityBegin(1,testEpoch);advance(30000);counters.processKernel100ns+=600000000;
    sgSleepActivityObserve("multi-group-process-average",testFileTime);expectUnknownSystem(fact().at("sinceBegin"));
    check(fact().at("sinceBegin").at("processCpuDeltaAvailable")==true&&fact().at("sinceBegin").at("processCpuEquivalentCores")==2.0&&
        fact().at("sinceBegin").at("processCpuPercentOfMachine")==50.0&&fact().at("sinceBegin").at("continuousHighCpuProven")==false&&
        fact().at("sinceBegin").at("powerActionAuthorized")==false,"activity whole-process interval average may span groups while system percent and continuous-high-CPU authority remain unknown or false");audit();
    reset();sgSleepActivityBegin(1,testEpoch);advance(30000);counters.logicalProcessors=8;counters.processKernel100ns+=300000000;
    sgSleepActivityObserve("process-topology-change",testFileTime);
    check(fact().at("sinceBegin").at("processCpuEquivalentCores")==1.0&&fact().at("sinceBegin").at("processCpuPercentOfMachine").is_null(),"activity topology changes preserve process core average without inventing a machine-normalized percentage");audit();
    reset();sgSleepActivityBegin(1,testEpoch);advance(30000);counters.processKernel100ns+=2000000000ULL;
    sgSleepActivityObserve("process-capacity-skew",testFileTime);
    check(fact().at("sinceBegin").at("processCpuDeltaAvailable")==true&&fact().at("sinceBegin").at("processCpuEquivalentCores").get<double>()>4.0&&
        fact().at("sinceBegin").at("processCpuPercentOfMachine").is_null()&&fact().at("sinceBegin").at("continuousHighCpuProven")==false,
        "activity process CPU above capacity retains raw average without a valid percentage or continuous-high-CPU proof");audit();
    reset();sgSleepActivityBegin(1,testEpoch);expectSamples(1);
    check(fact().at("cachedDisplay").at("sessionState")==-1&&fact().at("cachedDisplay").at("legacyMonitorState")==-1&&
        fact().at("physicalPowerAssessment")=="UNKNOWN"&&fact().at("powerNotifications").at("sleepSuccessProven")==false,
        "activity unknown cached display minus one remains unknown and cannot turn into sleep success");
    const auto beforeNotificationReads=calls.all();const auto beforeNotificationFacts=captured.size();
    g_idleSessionDisplayState=0;g_idleLegacyMonitorState=1;
    g_sleepNotificationEvidence.registered(ERROR_NOT_SUPPORTED);
    g_sleepNotificationEvidence.callback(PBT_APMSUSPEND,testTick);
    g_sleepNotificationEvidence.forwarded(false,ERROR_ACCESS_DENIED);g_sleepNotificationEvidence.noWindow();
    g_sleepNotificationEvidence.handled(PBT_APMSUSPEND,testTick+1);
    const auto notification=g_sleepNotificationEvidence.snapshot();
    check(calls.all()==beforeNotificationReads&&captured.size()==beforeNotificationFacts&&
        notification.at("powerActionAuthorized")==false&&notification.at("sleepSuccessProven")==false&&notification.at("userWakeProven")==false&&
        notification.at("registrationError")==ERROR_NOT_SUPPORTED&&notification.at("callbackEntries").at("suspend")==1&&
        notification.at("windowHandlerEntries").at("suspend")==1&&notification.at("forwarding").at("failed")==1&&
        notification.at("forwarding").at("lastFailureWin32Error")==ERROR_ACCESS_DENIED,
        "activity notification fixture uses the formal purely cached snapshot with errors and counts but no registration read, OS sampling or log write");
    advance(30000);sgSleepActivityObserve("cached-notifications",testFileTime);expectSamples(2);
    check(fact().at("cachedDisplay").at("sessionState")==0&&fact().at("cachedDisplay").at("legacyMonitorState")==1&&
        fact().at("powerNotifications")==notification&&captured.front().second.at("cachedDisplay").at("sessionState")==-1,
        "activity summary copies the two current cached display scalars and exact existing notification snapshot while preserving the prior unknown record");
    g_idleSessionDisplayState=-1;g_idleLegacyMonitorState=-1;advance(30000);sgSleepActivityEnd("cached-unknown-end");expectSamples(3);
    check(fact().at("cachedDisplay").at("sessionState")==-1&&fact().at("cachedDisplay").at("legacyMonitorState")==-1&&
        fact().at("powerNotifications")==notification&&!g_sleepActivityWindow.active(),
        "activity display cache returning to unknown does not imply sleep success and cannot prevent end");audit();
    reset();g_sgInternalSleepRequest={};g_sgAutomaticSleepBudget.confirmedUserWake();
    return sleepEvidenceChecks-initialChecks;
}
