#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <powrprof.h>
#include "json.hpp"
#include "sleep_request_fallback.h"
#include <atomic>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using json=nlohmann::json;
using Mode=ymcc::sleeprequest::Mode;
static unsigned checks=0,scenarios=0,totalSuspendCalls=0,totalMonitorCalls=0,totalWindowQueries=0;
static void check(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}
enum class SgRetryKind {None,EntryFailure,UnexpectedWake,SleepTimeout};
struct {SgRetryKind retryKind=SgRetryKind::EntryFailure;unsigned retryAttempt=0;} g_sgTask;
struct {SgRetryKind kind=SgRetryKind::None;ULONGLONG dispatchTick=0,dispatchFileTime=0;} g_sgInternalSleepRequest;
static constexpr ULONGLONG SG_INTERNAL_SLEEP_506_WINDOW_MS=60000,SG_INTERNAL_SLEEP_506_CLOCK_SKEW_MS=2000;
static ULONGLONG testTick=10000;
static ULONGLONG fakeGetTickCount64(){return testTick;}
enum class PowerLifecycle {Ready,Suspending,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Suspending};
static bool g_sgSleepQueryGateClosed=true,g_sgSleepQueryOwnsGeneration=true;
static ULONGLONG g_sgSleepQueryGeneration=7,g_sgRetryDueTick=1;
static unsigned guardArms=0;
static constexpr UINT_PTR SG_RETRY_TIMER_ID=1;
static BOOL fakeKillTimer(HWND,UINT_PTR){return TRUE;}
static void sgArmNonUserWakeGuard(const char*){++guardArms;}
[[maybe_unused]] static void sgClearInternalSleepRequest(const char*){g_sgInternalSleepRequest={};}
static bool g_guardEnabled=true,g_sgRetryInProgress=true,g_sgRetryEntryFailure=true,g_sgResleepEnabled=true;
static bool g_sgRetrySleepTimeout=false,timeoutDispatchPermitted=true;
static unsigned timeoutPermissionQueries=0;
static ULONGLONG timeoutOriginalIntent=980000;
static ULONGLONG sgSleepTimeoutOriginalIntent(){return timeoutOriginalIntent;}
// Mock only the timeout policy boundary. The production timeout-policy fixture
// owns its 240-second observation and shared-budget implementation.
static bool sgSleepTimeoutDispatchPermitted(){++timeoutPermissionQueries;return g_sgRetrySleepTimeout && timeoutDispatchPermitted;}
static std::atomic<ULONGLONG> g_sgLastPowerButtonWakeEventFileTime{0},g_sgLastPowerButtonSleepIntentFileTime{0};
static std::atomic<ULONGLONG> g_sgLatestUserSleepEntryFileTime{0},g_sgClosedSleepIntentFileTime{0};
static std::atomic<ULONGLONG> g_sgOriginalPowerButtonWakeFileTime{0};
static std::atomic<Mode> g_sgAutomaticSleepTarget{Mode::None};
static ULONGLONG testGeneration=7;
static ULONGLONG currentPowerGeneration(){return testGeneration;}
static ULONGLONG testClock=1000000;
static ULONGLONG sgNowFileTime(){return testClock;}
static std::vector<std::pair<std::string,json>> facts;
static std::string injectWakeAt;
static ULONGLONG injectedWakeTime=0,stagedOrigin=0;
static Mode dispatchMode=Mode::None;
static void injectConfirmedWake(const char* point){if(injectWakeAt==point)g_sgLastPowerButtonWakeEventFileTime=injectedWakeTime;}
static void sgRecordFact(const char* key,const json& value){
    facts.emplace_back(key,value);injectConfirmedWake(key);
    if(std::string(key)=="sleep-request-dispatch"){
        const auto target=value.at("target").get<std::string>();
        dispatchMode=target=="modern-standby" ? Mode::ModernStandby : target=="sleep" ? Mode::Sleep : Mode::Hibernate;
    }
}
struct TestReply {bool accepted=true;DWORD error=0;};
static SYSTEM_POWER_CAPABILITIES capability{};
static bool testCapsKnown=true,validWindow=true,wakeAfterModern=false,generationAfterModern=false;
static bool privilegeOk=true,tokenOpenOk=true,privilegeLookupOk=true,stagedPermission=true,cancelDuringPrivilege=false;
static bool wakeDuringCapabilities=false,wakeDuringPrivilege=false,disableDuringPrivilege=false;
static bool disableTimeoutDuringPrivilege=false,disableUnexpectedDuringPrivilege=false,loseTimeoutPermissionDuringPrivilege=false;
static bool sgAbnormalPermitted(){return stagedPermission && (!stagedOrigin || g_sgLastPowerButtonWakeEventFileTime.load()<stagedOrigin);}
static unsigned finalSourceQueries=0,finalSourceFailAt=0;
static bool sgAbnormalDispatchReady(){++finalSourceQueries;injectConfirmedWake("source-final-query");return finalSourceQueries!=finalSourceFailAt && sgAbnormalPermitted();}
static DWORD privilegeError=ERROR_NOT_ALL_ASSIGNED,lastError=0;
static int privilegeCalls=0,tokenOpenCalls=0;
static HWND g_hwnd=reinterpret_cast<HWND>(1);
static TestReply modern{},sleepReply{};
static std::vector<Mode> calls;
static json suspendArguments=json::array();
static DWORD fakeGetLastError(){return lastError;}
static void fakeSetLastError(DWORD error){lastError=error;}
static BOOL fakeGetPwrCapabilities(PSYSTEM_POWER_CAPABILITIES out){injectConfirmedWake("capabilities");if(wakeDuringCapabilities)g_sgLastPowerButtonWakeEventFileTime=sgNowFileTime()+1;*out=capability;if(!testCapsKnown)lastError=ERROR_ACCESS_DENIED;return testCapsKnown;}
static HANDLE fakeGetCurrentProcess(){return reinterpret_cast<HANDLE>(2);}
static BOOL fakeOpenProcessToken(HANDLE,DWORD access,PHANDLE token){
    ++tokenOpenCalls;check(access==(TOKEN_QUERY|TOKEN_ADJUST_PRIVILEGES),"shutdown privilege requests token query and adjustment");
    if(!tokenOpenOk){lastError=privilegeError;return FALSE;}*token=reinterpret_cast<HANDLE>(3);return TRUE;
}
static BOOL fakeLookupPrivilegeValueW(LPCWSTR,LPCWSTR name,PLUID){
    check(std::wcscmp(name,SE_SHUTDOWN_NAME)==0,"both direct modes prepare SE_SHUTDOWN privilege");
    if(!privilegeLookupOk)lastError=privilegeError;return privilegeLookupOk;
}
static BOOL fakeAdjustTokenPrivileges(HANDLE,BOOL,PTOKEN_PRIVILEGES,DWORD,PTOKEN_PRIVILEGES,PDWORD){
    injectConfirmedWake("privilege");++privilegeCalls;
    if(cancelDuringPrivilege)stagedPermission=false;
    if(wakeDuringPrivilege)g_sgLastPowerButtonWakeEventFileTime=sgNowFileTime()+1;
    if(disableDuringPrivilege)g_sgRetryEntryFailure=false;
    if(disableTimeoutDuringPrivilege)g_sgRetrySleepTimeout=false;
    if(disableUnexpectedDuringPrivilege)g_sgResleepEnabled=false;
    if(loseTimeoutPermissionDuringPrivilege)timeoutDispatchPermitted=false;
    lastError=privilegeOk ? 0 : privilegeError;return privilegeOk || privilegeError==ERROR_NOT_ALL_ASSIGNED;
}
static BOOL fakeCloseHandle(HANDLE){return TRUE;}
[[maybe_unused]] static BOOL fakeIsWindow(HWND h){++totalWindowQueries;injectConfirmedWake("window-validation");return h==g_hwnd && validWindow;}
[[maybe_unused]] static LRESULT fakeSendMessageTimeoutW(HWND,UINT,WPARAM,LPARAM,UINT,UINT,PDWORD_PTR){
    ++totalMonitorCalls;check(false,"monitor-off substitution is forbidden");return 0;
}
static BOOLEAN fakeSetSuspendState(BOOLEAN hibernate,BOOLEAN critical,BOOLEAN disableWake){
    ++totalSuspendCalls;
    check(hibernate==FALSE && critical==FALSE && disableWake==FALSE,"every actual request uses SetSuspendState(FALSE,FALSE,FALSE)");
    check(dispatchMode==Mode::ModernStandby || dispatchMode==Mode::Sleep,"production Hibernate invoke is forbidden");
    check(privilegeCalls==1,"both direct modes require one successful shutdown privilege preparation");
    calls.push_back(dispatchMode);suspendArguments.push_back(json::array({hibernate!=FALSE,critical!=FALSE,disableWake!=FALSE}));
    const auto reply=dispatchMode==Mode::ModernStandby ? modern : sleepReply;lastError=reply.error;
    if(dispatchMode==Mode::ModernStandby){
        if(wakeAfterModern)g_sgLastPowerButtonWakeEventFileTime=sgNowFileTime()+1;
        if(generationAfterModern)++testGeneration;
    }
    return reply.accepted;
}
#define GetTickCount64 fakeGetTickCount64
#define KillTimer fakeKillTimer
#define GetLastError fakeGetLastError
#define SetLastError fakeSetLastError
#define GetPwrCapabilities fakeGetPwrCapabilities
#define GetCurrentProcess fakeGetCurrentProcess
#define OpenProcessToken fakeOpenProcessToken
#define LookupPrivilegeValueW fakeLookupPrivilegeValueW
#define AdjustTokenPrivileges fakeAdjustTokenPrivileges
#define CloseHandle fakeCloseHandle
#define IsWindow fakeIsWindow
#define SendMessageTimeoutW fakeSendMessageTimeoutW
#define SetSuspendState fakeSetSuspendState
#include "production_request.inc"
static bool productionRetrySuspendConfirmation(){
#include "production_suspend_confirmation.inc"
    return false;
}
static void reset(bool ms=true,bool ordinary=true,bool hibernate=true){
    g_sgInternalSleepRequest={};testTick=10000;g_sgTask.retryAttempt=0;guardArms=0;
    g_powerLifecycle=PowerLifecycle::Suspending;g_sgSleepQueryGateClosed=g_sgSleepQueryOwnsGeneration=true;g_sgSleepQueryGeneration=7;g_sgRetryDueTick=1;
    capability={};capability.AoAc=ms;capability.SystemS3=ordinary;capability.SystemS4=hibernate;capability.HiberFilePresent=hibernate;
    testCapsKnown=validWindow=privilegeOk=tokenOpenOk=privilegeLookupOk=true;finalSourceQueries=finalSourceFailAt=0;wakeAfterModern=generationAfterModern=false;
    stagedPermission=timeoutDispatchPermitted=true;timeoutPermissionQueries=0;timeoutOriginalIntent=980000;
    cancelDuringPrivilege=wakeDuringCapabilities=wakeDuringPrivilege=disableDuringPrivilege=false;
    disableTimeoutDuringPrivilege=disableUnexpectedDuringPrivilege=loseTimeoutPermissionDuringPrivilege=false;
    privilegeError=ERROR_NOT_ALL_ASSIGNED;lastError=0;privilegeCalls=tokenOpenCalls=0;
    g_guardEnabled=g_sgRetryInProgress=g_sgRetryEntryFailure=g_sgResleepEnabled=true;g_sgRetrySleepTimeout=false;
    g_sgLastPowerButtonSleepIntentFileTime=1000000-20000;g_sgTask.retryKind=SgRetryKind::EntryFailure;
    g_sgLastPowerButtonWakeEventFileTime=0;g_sgAutomaticSleepTarget=Mode::None;testGeneration=7;
    g_sgLatestUserSleepEntryFileTime=0;g_sgClosedSleepIntentFileTime=0;g_sgOriginalPowerButtonWakeFileTime=0;
    modern=sleepReply={};calls.clear();suspendArguments=json::array();facts.clear();injectWakeAt.clear();injectedWakeTime=stagedOrigin=0;testClock=1000000;dispatchMode=Mode::None;
}
static void selectRetryKind(SgRetryKind kind){g_sgTask.retryKind=kind;g_sgRetrySleepTimeout=kind==SgRetryKind::SleepTimeout;}
static void setRetryIntent(ULONGLONG intent){g_sgLastPowerButtonSleepIntentFileTime=intent;timeoutOriginalIntent=intent;}
static json outcomes=json::array(),selectedCases=json::array();
static void run(const std::string& label,const std::vector<Mode>& expected,bool accepted,Mode target,DWORD error=0,
    Mode first=Mode::ModernStandby,Mode last=Mode::Sleep,bool staged=false){
    ++scenarios;selectedCases.push_back(label);
    // Exercise the production default arguments for the ordinary entry point.
    const auto result=first==Mode::ModernStandby && last==Mode::Sleep && !staged
        ? sgRequestSystemSleep() : sgRequestSystemSleep(first,last,staged);
    check(calls==expected,label.c_str());check(result.accepted==accepted,label.c_str());check(result.target==target,label.c_str());
    check(result.requestError==error,label.c_str());
    check(!facts.empty() && facts.back().first=="sleep-request-route-result","route result always recorded");
    check(facts.back().second.contains("arguments") && facts.back().second.at("arguments")==json::array({false,false,false}),"final request result records all three FALSE arguments");
    check(facts.back().second.contains("hibernate") && facts.back().second.at("hibernate")==false,"final request result explicitly records hibernate false");
    check(result.accepted || g_sgAutomaticSleepTarget.load()==Mode::None,"failed request never retains a pending target");
    check(sgAutomaticClassicSleepPending()==(accepted && target==Mode::Sleep),"modern routing never creates a classic receipt");
    check(std::string(result.api)==(target==Mode::None ? "none" : "SetSuspendState"),"both routed modes identify the actual suspend API");
    check(totalMonitorCalls==0 && totalWindowQueries==0,"direct sleep does not depend on monitor messages or window validation");
    if(!calls.empty())check(result.privilegeRequired && result.shutdownPrivilegeEnabled && tokenOpenCalls==1,"direct API requests require shutdown privilege");
    for(const auto& [key,value]:facts){
        if(key=="sleep-request-route"){
            check(value.at("hibernateAllowed")==false,"hardware S4 availability never authorizes Hibernate");
            if(testCapsKnown)check(value.at("hibernateAvailable")==json(capability.SystemS4!=FALSE && capability.HiberFilePresent!=FALSE),"hardware S4 availability is diagnostic only");
            check(value.at("order")==json::array({"modern-standby","sleep"}),"automatic route excludes Hibernate");
        }
        if(key=="sleep-request-route" || key=="sleep-request-dispatch"){
            check(value.at("api")=="SetSuspendState" && value.at("arguments")==json::array({false,false,false}),"request facts preserve the actual three FALSE arguments");
        }
        if(key=="sleep-request-dispatch")check(value.at("hibernate")==false && value.at("shutdownPrivilegeRequired")==true,"dispatch records direct sleep with privilege for both modes");
    }
    if(result.accepted)check(facts.back().second.at("outcome")=="accepted-pending-confirmation","API acceptance is pending physical sleep confirmation");
    json modes=json::array();for(auto mode:calls)modes.push_back(ymcc::sleeprequest::name(mode));
    outcomes.push_back({{"name",label},{"calls",modes},{"arguments",suspendArguments},{"accepted",result.accepted},{"target",ymcc::sleeprequest::name(result.target)},
        {"error",result.requestError},{"privilegeCalls",privilegeCalls},{"timeoutPermissionQueries",timeoutPermissionQueries},{ "finalArguments",facts.back().second.at("arguments")},{"finalHibernate",facts.back().second.at("hibernate")}});
}
// Original request cases for the independent timeout intent boundary. The
// directed selection calls this same group once and excludes the default group.
static void runTimeoutIntentCases(){
    for(const auto mode:{Mode::ModernStandby,Mode::Sleep}){
        const auto suffix=" / "+std::string(ymcc::sleeprequest::name(mode));
        reset(mode==Mode::ModernStandby);selectRetryKind(SgRetryKind::SleepTimeout);g_sgLastPowerButtonSleepIntentFileTime=0;
        run("timeout retains independent intent after old guard clears global"+suffix,{mode},true,mode,0,mode,mode);
        reset(mode==Mode::ModernStandby);selectRetryKind(SgRetryKind::SleepTimeout);g_sgLastPowerButtonSleepIntentFileTime=sgNowFileTime()-10000;timeoutOriginalIntent=0;
        run("old global cannot invent missing timeout original intent"+suffix,{},false,mode,ERROR_CANCELLED,mode,mode);
        reset(mode==Mode::ModernStandby);selectRetryKind(SgRetryKind::SleepTimeout);g_sgLastPowerButtonSleepIntentFileTime=0;g_sgLastPowerButtonWakeEventFileTime=timeoutOriginalIntent-1;
        run("wake older than independent timeout intent permits request"+suffix,{mode},true,mode,0,mode,mode);
        for(const auto* point:{"before-helper","capabilities","sleep-request-route","privilege","sleep-request-dispatch"}){
            reset(mode==Mode::ModernStandby);selectRetryKind(SgRetryKind::SleepTimeout);g_sgLastPowerButtonSleepIntentFileTime=0;
            injectedWakeTime=timeoutOriginalIntent+10000;injectWakeAt=point;
            if(injectWakeAt=="before-helper")g_sgLastPowerButtonWakeEventFileTime=injectedWakeTime;
            run("manual wake vetoes independent timeout intent"+suffix+" / "+point,{},false,mode,ERROR_CANCELLED,mode,mode);
        }
    }
}
static void runLate506Cases(){
    constexpr ULONGLONG dispatched=1000000000000ULL,tick=1000,late=tick+600000;
    for(const auto kind:{SgRetryKind::EntryFailure,SgRetryKind::UnexpectedWake,SgRetryKind::SleepTimeout})
     for(const auto mode:{Mode::ModernStandby,Mode::Sleep}){
        const auto suffix=" / "+std::to_string(static_cast<unsigned>(kind))+" / "+ymcc::sleeprequest::name(mode);
        const auto probe=[&](const char* label,int reason,ULONGLONG event,ULONGLONG dispatch,ULONGLONG dispatchTick,ULONGLONG now,bool expected,SgRetryKind markerKind){
            reset();g_sgTask.retryKind=SgRetryKind::None;g_sgRetryInProgress=false;g_sgAutomaticSleepTarget=mode;
            g_sgInternalSleepRequest={markerKind,dispatchTick,dispatch};testTick=now;
            const std::string name="internal506 / "+std::string(label)+suffix;
            ++scenarios;selectedCases.push_back(name);
            check(sgInternalSleepRequestMatches506(reason,event)==expected,name.c_str());
            check(calls.empty(),"506 matching never calls a power API");
            outcomes.push_back({{"name",name},{"matches",expected},{"callbackAgeMs",now>=dispatchTick ? json(now-dispatchTick) : json(nullptr)}});
        };
        probe("late callback retains original match",1,dispatched+10000,dispatched,tick,late,true,kind);
        probe("exact early skew boundary",1,dispatched-2000*10000ULL,dispatched,tick,late,true,kind);
        probe("before early skew boundary",1,dispatched-2000*10000ULL-1,dispatched,tick,late,false,kind);
        probe("exact original late boundary",3,dispatched+60000*10000ULL,dispatched,tick,late,true,kind);
        probe("beyond original late boundary",3,dispatched+60000*10000ULL+1,dispatched,tick,late,false,kind);
        probe("missing event time cannot survive callback timeout",1,0,dispatched,tick,late,false,kind);
        probe("missing dispatch time cannot survive callback timeout",1,dispatched+10000,0,tick,late,false,kind);
        probe("missing both times cannot survive callback timeout",1,0,0,tick,late,false,kind);
        probe("missing time keeps original bounded callback fallback",1,0,dispatched,tick,tick+60000,true,kind);
        probe("non-user reason needs modern route",28,dispatched+10000,dispatched,tick,late,mode==Mode::ModernStandby,kind);
        probe("callback tick before dispatch is refused",1,dispatched+10000,dispatched,tick,tick-1,false,kind);
        probe("retired marker is refused",1,dispatched+10000,dispatched,tick,late,false,SgRetryKind::None);
        probe("missing dispatch tick is refused",1,dispatched+10000,dispatched,0,late,false,kind);
     }
    for(const auto kind:{SgRetryKind::EntryFailure,SgRetryKind::UnexpectedWake,SgRetryKind::SleepTimeout}){
        reset();selectRetryKind(kind);g_sgTask.retryAttempt=2;g_sgInternalSleepRequest={kind,tick,dispatched};g_sgAutomaticSleepTarget=Mode::ModernStandby;
        const std::string name="suspend-confirmation retains internal506 marker / "+std::to_string(static_cast<unsigned>(kind));
        ++scenarios;selectedCases.push_back(name);
        check(productionRetrySuspendConfirmation(),name.c_str());
        check(g_sgInternalSleepRequest.kind==kind && g_sgInternalSleepRequest.dispatchTick==tick && g_sgInternalSleepRequest.dispatchFileTime==dispatched,"all completed retry kinds retain their original internal506 marker");
        check(guardArms==(kind==SgRetryKind::UnexpectedWake ? 1U : 0U),"only unexpected wake rearms the separate non-user guard");
        check(!g_sgRetryInProgress && g_sgTask.retryKind==SgRetryKind::None && g_sgTask.retryAttempt==0 && g_sgRetryDueTick==0,"confirmed suspend retires retry scheduling");
        check(g_powerLifecycle==PowerLifecycle::Suspended && !g_sgSleepQueryGateClosed && !g_sgSleepQueryOwnsGeneration && g_sgSleepQueryGeneration==0,"confirmed suspend retains its production ownership cleanup");
        testTick=late;check(sgInternalSleepRequestMatches506(1,dispatched+10000),"late internal506 still matches after actual extracted suspend-confirmation branch");
        check(calls.empty(),"suspend confirmation does not issue another power call");outcomes.push_back({{"name",name},{"markerRetained",true}});
    }
}
static void writeSuccess(const char* path){
    const json result={{"status","passed"},{"scenarios",scenarios},{"checks",checks},{"results",outcomes},{"selectedCases",selectedCases},{"realSleepCalls",0},
        {"mockSuspendCalls",totalSuspendCalls},{"monitorCalls",totalMonitorCalls},{"windowQueries",totalWindowQueries},
        {"physicalSleepConfirmed",false},{"timeoutPolicyPort","mocked; no 240-second/shared-budget implementation acceptance"},
        {"productionFunctions","sgRequestSystemSleep;sgPrepareSleepPrivilege;sgAutomaticClassicSleepPending;sgUserWakeCancelsSleepIntent;sgInternalSleepRequestMatches506;PBT_APMSUSPEND/retry-confirmation"}};
    std::ofstream out(path);out<<result.dump(2)<<'\n';
    std::cout<<json{{"status","passed"},{"scenarios",scenarios},{"checks",checks},{"mockSuspendCalls",totalSuspendCalls},{"monitorCalls",totalMonitorCalls}}.dump()<<'\n';
}
int main(int argc,char**argv){try{
    if(argc!=5)throw std::runtime_error("result.json, archived wake fixture, original mutation and case selections required");
    const std::string mutation=argv[3],caseFilter=argv[4];
    check(caseFilter=="all" || caseFilter=="timeout-intent" || caseFilter=="intent-and-late-506","unknown case selection is refused");
    check(caseFilter=="all" || mutation=="none","directed case selection cannot also mutate");
    if(caseFilter!="all"){runTimeoutIntentCases();if(caseFilter=="intent-and-late-506")runLate506Cases();writeSuccess(argv[1]);return 0;}
    // Existing mutations run only their original decisive case; no full-suite replay.
    if(mutation=="downgrade-on-any-error"){
        reset();modern={false,ERROR_ACCESS_DENIED};run("generic modern failure stops downgrade",{Mode::ModernStandby},false,Mode::ModernStandby,ERROR_ACCESS_DENIED);
        throw std::runtime_error("mutation failed to activate");
    }
    if(mutation=="hibernate-first"){
        reset();run("capability priority matrix",{Mode::ModernStandby},true,Mode::ModernStandby);
        throw std::runtime_error("mutation failed to activate");
    }
    check(mutation=="none","unknown mutation is refused");
    runTimeoutIntentCases();runLate506Cases();
    check(std::string(ymcc::sleeprequest::api(Mode::ModernStandby))=="SetSuspendState","generic modern API is direct suspend");
    {unsigned invoked=0;const auto result=ymcc::sleeprequest::request(ymcc::sleeprequest::Capabilities{true,false,false,true,0},[&](Mode){++invoked;return ymcc::sleeprequest::Reply{ymcc::sleeprequest::Outcome::Accepted,0};});
     ++scenarios;selectedCases.push_back("generic default excludes Hibernate");check(invoked==0 && result.target==Mode::None,"generic default excludes Hibernate even when hardware supports it");}
    for(unsigned mask=0;mask<16;++mask){
        const bool ms=(mask&1)!=0,ordinary=(mask&2)!=0,hib=(mask&4)!=0,isKnown=(mask&8)!=0;
        reset(ms,ordinary,hib);testCapsKnown=isKnown;
        const auto expected=!isKnown ? Mode::None : ms ? Mode::ModernStandby : ordinary ? Mode::Sleep : Mode::None;
        run("capability priority matrix / "+std::to_string(mask),expected==Mode::None ? std::vector<Mode>{} : std::vector<Mode>{expected},expected!=Mode::None,expected,
            !isKnown ? ERROR_ACCESS_DENIED : expected==Mode::None ? ERROR_NOT_SUPPORTED : 0);
    }
    for(unsigned state=1;state<=3;++state){
        reset(false,false);if(state==1)capability.SystemS1=TRUE;else if(state==2)capability.SystemS2=TRUE;else capability.SystemS3=TRUE;
        run("legacy S"+std::to_string(state)+" directly requests triple FALSE",{Mode::Sleep},true,Mode::Sleep);
    }
    reset(true,false,false);run("AoAc-only platform directly requests triple FALSE",{Mode::ModernStandby},true,Mode::ModernStandby);
    reset();validWindow=false;g_hwnd=nullptr;run("modern direct request needs no window or monitor-off substitute",{Mode::ModernStandby},true,Mode::ModernStandby);g_hwnd=reinterpret_cast<HWND>(1);
    reset();testCapsKnown=false;g_sgAutomaticSleepTarget=Mode::Hibernate;run("new failed selection clears stale receipt",{},false,Mode::None,ERROR_ACCESS_DENIED);
    reset();run("modern accepted stops all downgrade",{Mode::ModernStandby},true,Mode::ModernStandby);check(privilegeCalls==1,"modern requires shutdown privilege");
    reset();modern={false,ERROR_NOT_SUPPORTED};run("explicit modern unsupported falls to sleep",{Mode::ModernStandby,Mode::Sleep},true,Mode::Sleep);check(privilegeCalls==1,"fallback shares one successful privilege preparation");
    reset(true,false,true);modern={false,ERROR_NOT_SUPPORTED};run("absent ordinary capability never falls through to S4",{Mode::ModernStandby},false,Mode::None,ERROR_NOT_SUPPORTED);
    for(const auto code:{ERROR_INVALID_FUNCTION,ERROR_CALL_NOT_IMPLEMENTED}){
        reset();modern={false,static_cast<DWORD>(code)};run("operation unsupported allows downgrade / "+std::to_string(code),{Mode::ModernStandby,Mode::Sleep},true,Mode::Sleep);
    }
    for(const auto mode:{Mode::ModernStandby,Mode::Sleep}){
        for(const auto code:{ERROR_ACCESS_DENIED,ERROR_TIMEOUT,ERROR_INVALID_PARAMETER,ERROR_GEN_FAILURE,ERROR_PRIVILEGE_NOT_HELD,ERROR_CANCELLED,ERROR_SUCCESS}){
            reset(mode==Mode::ModernStandby);(mode==Mode::ModernStandby ? modern : sleepReply)={false,static_cast<DWORD>(code)};
            run("non-unsupported error stops downgrade / "+std::string(ymcc::sleeprequest::name(mode))+" / "+std::to_string(code),{mode},false,mode,code ? code : ERROR_GEN_FAILURE);
        }
        for(const auto code:{ERROR_NOT_ALL_ASSIGNED,ERROR_ACCESS_DENIED,ERROR_CALL_NOT_IMPLEMENTED}){
            reset(mode==Mode::ModernStandby);privilegeOk=false;privilegeError=code;
            run("privilege failure never downgrades / "+std::string(ymcc::sleeprequest::name(mode))+" / "+std::to_string(code),{},false,mode,code);
        }
        for(const bool openFailure:{false,true}){
            reset(mode==Mode::ModernStandby);tokenOpenOk=!openFailure;privilegeLookupOk=openFailure;privilegeError=ERROR_CALL_NOT_IMPLEMENTED;
            run("unsupported prerequisite never downgrades / "+std::string(ymcc::sleeprequest::name(mode))+(openFailure ? " / token" : " / lookup"),{},false,mode,ERROR_CALL_NOT_IMPLEMENTED);
        }
    }
    reset();g_sgLastPowerButtonWakeEventFileTime=sgNowFileTime();run("confirmed wake cancels before first call",{},false,Mode::ModernStandby,ERROR_CANCELLED);
    reset();modern={false,ERROR_NOT_SUPPORTED};wakeAfterModern=true;run("confirmed wake cancels between modes",{Mode::ModernStandby},false,Mode::Sleep,ERROR_CANCELLED);
    reset();modern={false,ERROR_NOT_SUPPORTED};generationAfterModern=true;run("new power generation cancels between modes",{Mode::ModernStandby},false,Mode::Sleep,ERROR_CANCELLED);
    reset(false,false,true);capability.HiberFilePresent=FALSE;run("missing hiberfile never enables it",{},false,Mode::None,ERROR_NOT_SUPPORTED);
    reset();modern=sleepReply={false,ERROR_NOT_SUPPORTED};run("all permitted calls explicitly unsupported",{Mode::ModernStandby,Mode::Sleep},false,Mode::None,ERROR_NOT_SUPPORTED);
    reset();modern=sleepReply={false,ERROR_NOT_SUPPORTED};run("staged unsupported cannot fall through to S4",{Mode::ModernStandby,Mode::Sleep},false,Mode::None,ERROR_NOT_SUPPORTED,Mode::ModernStandby,Mode::Sleep,true);
    for(const bool staged:{false,true}){
        reset();run(staged ? "staged explicit Hibernate is refused" : "explicit Hibernate is refused",{},false,Mode::None,ERROR_NOT_SUPPORTED,Mode::Hibernate,Mode::Hibernate,staged);
        check(privilegeCalls==0,"rejected Hibernate never prepares a real dispatch");
        reset();modern=sleepReply={false,ERROR_NOT_SUPPORTED};run(staged ? "staged widened range cannot invoke Hibernate" : "widened range cannot invoke Hibernate",{Mode::ModernStandby,Mode::Sleep},false,Mode::None,ERROR_NOT_SUPPORTED,Mode::ModernStandby,Mode::Hibernate,staged);
    }
    reset();stagedPermission=false;run("staged cancellation before helper start wins",{},false,Mode::ModernStandby,ERROR_CANCELLED,Mode::ModernStandby,Mode::Sleep,true);
    for(const auto mode:{Mode::ModernStandby,Mode::Sleep}){
        reset();cancelDuringPrivilege=true;run("staged cancellation during privilege / "+std::string(ymcc::sleeprequest::name(mode)),{},false,mode,ERROR_CANCELLED,mode,mode,true);
    }
    reset();g_sgLastPowerButtonSleepIntentFileTime=sgNowFileTime()-20000;g_sgLastPowerButtonWakeEventFileTime=sgNowFileTime()-10000;
    run("confirmed current-cycle wake before helper start cancels",{},false,Mode::ModernStandby,ERROR_CANCELLED);
    reset();g_sgRetryEntryFailure=false;run("disabled entry feature cannot invoke request",{},false,Mode::ModernStandby,ERROR_CANCELLED);
    reset();g_sgRetryEntryFailure=false;selectRetryKind(SgRetryKind::UnexpectedWake);run("entry feature off preserves independent unexpected wake",{Mode::ModernStandby},true,Mode::ModernStandby);
    reset();g_sgRetryEntryFailure=false;run("staged permission retains its own owner checks",{Mode::ModernStandby},true,Mode::ModernStandby,0,Mode::ModernStandby,Mode::Sleep,true);
    reset();wakeDuringCapabilities=true;run("wake during capability read vetoes first call",{},false,Mode::ModernStandby,ERROR_CANCELLED);
    for(const auto mode:{Mode::ModernStandby,Mode::Sleep}){
        reset(mode==Mode::ModernStandby);wakeDuringPrivilege=true;run("wake during privilege vetoes / "+std::string(ymcc::sleeprequest::name(mode)),{},false,mode,ERROR_CANCELLED);
        reset(mode==Mode::ModernStandby);disableDuringPrivilege=true;run("entry option off during privilege vetoes / "+std::string(ymcc::sleeprequest::name(mode)),{},false,mode,ERROR_CANCELLED);
        reset(mode==Mode::ModernStandby);disableDuringPrivilege=true;selectRetryKind(SgRetryKind::UnexpectedWake);run("entry option off during privilege preserves unexpected wake / "+std::string(ymcc::sleeprequest::name(mode)),{mode},true,mode);
        reset(mode==Mode::ModernStandby);selectRetryKind(SgRetryKind::SleepTimeout);g_sgRetrySleepTimeout=false;
        run("other features cannot authorize disabled timeout / "+std::string(ymcc::sleeprequest::name(mode)),{},false,mode,ERROR_CANCELLED);check(timeoutPermissionQueries!=0,"timeout permission boundary is consumed");
        reset(mode==Mode::ModernStandby);selectRetryKind(SgRetryKind::SleepTimeout);g_sgRetryEntryFailure=g_sgResleepEnabled=false;
        run("timeout has independent authorization / "+std::string(ymcc::sleeprequest::name(mode)),{mode},true,mode);check(timeoutPermissionQueries>=2,"timeout permission is rechecked before API");
        reset(mode==Mode::ModernStandby);selectRetryKind(SgRetryKind::SleepTimeout);timeoutDispatchPermitted=false;
        run("timeout policy veto survives enabled option / "+std::string(ymcc::sleeprequest::name(mode)),{},false,mode,ERROR_CANCELLED);
        reset(mode==Mode::ModernStandby);selectRetryKind(SgRetryKind::SleepTimeout);disableTimeoutDuringPrivilege=true;
        run("timeout option off during privilege cancels / "+std::string(ymcc::sleeprequest::name(mode)),{},false,mode,ERROR_CANCELLED);
        reset(mode==Mode::ModernStandby);selectRetryKind(SgRetryKind::SleepTimeout);loseTimeoutPermissionDuringPrivilege=true;
        run("timeout policy withdrawal during privilege cancels / "+std::string(ymcc::sleeprequest::name(mode)),{},false,mode,ERROR_CANCELLED);
        reset(mode==Mode::ModernStandby);disableTimeoutDuringPrivilege=true;
        run("timeout option off preserves entry repair / "+std::string(ymcc::sleeprequest::name(mode)),{mode},true,mode);
        reset(mode==Mode::ModernStandby);selectRetryKind(SgRetryKind::UnexpectedWake);disableUnexpectedDuringPrivilege=true;
        run("unexpected wake option off during privilege cancels / "+std::string(ymcc::sleeprequest::name(mode)),{},false,mode,ERROR_CANCELLED);
    }
    for(const auto kind:{SgRetryKind::EntryFailure,SgRetryKind::UnexpectedWake,SgRetryKind::SleepTimeout}){
        for(const bool guardOff:{false,true}){
            reset();selectRetryKind(kind);if(guardOff)g_guardEnabled=false;else g_sgRetryInProgress=false;
            run("retry owner withdrawal / "+std::to_string(static_cast<unsigned>(kind))+(guardOff ? " / guard" : " / in-progress"),{},false,Mode::ModernStandby,ERROR_CANCELLED);
        }
    }
    json archived;{std::ifstream input(argv[2]);input>>archived;}
    check(archived.at("kind")=="archived-manual-wake-dispatch-race","manual wake fixture identity");
    reset();testClock=std::stoull(archived.at("requestPreparationFileTime").get<std::string>());selectRetryKind(SgRetryKind::UnexpectedWake);
    g_sgLastPowerButtonSleepIntentFileTime=std::stoull(archived.at("originalIntentFileTime").get<std::string>());
    injectedWakeTime=std::stoull(archived.at("kernelWakeFileTime").get<std::string>());injectWakeAt="capabilities";
    check(g_sgLastPowerButtonSleepIntentFileTime.load()<injectedWakeTime && injectedWakeTime<testClock,"archive original chronology is retained");
    run("G3E archived manual wake before helper original time and callback during preparation",{},false,Mode::ModernStandby,ERROR_CANCELLED);
    check(calls.size()==archived.at("expectedPowerCallsAfterFix").get<unsigned>(),"archived callback evidence cancels automatic sleep");
    // Keep the archived callback ordering, testing every remaining pre-API phase.
    // Window validation and S4 dispatch are explicitly forbidden above.
    for(const auto kind:{SgRetryKind::EntryFailure,SgRetryKind::UnexpectedWake,SgRetryKind::SleepTimeout})
     for(const auto mode:{Mode::ModernStandby,Mode::Sleep})
      for(const auto* point:{"before-helper","capabilities","sleep-request-route","privilege","sleep-request-dispatch"}){
        reset(mode==Mode::ModernStandby);selectRetryKind(kind);g_sgLastPowerButtonSleepIntentFileTime=sgNowFileTime()-20000;
        injectedWakeTime=sgNowFileTime()-10000;injectWakeAt=point;if(injectWakeAt=="before-helper")g_sgLastPowerButtonWakeEventFileTime=injectedWakeTime;
        if(kind==SgRetryKind::SleepTimeout)g_sgLastPowerButtonSleepIntentFileTime=0;
        run("manual wake veto / "+std::to_string(static_cast<unsigned>(kind))+" / "+ymcc::sleeprequest::name(mode)+" / "+point,{},false,mode,ERROR_CANCELLED,mode,mode);
        check(calls.size()==archived.at("expectedPowerCallsAfterFix").get<unsigned>(),"archived manual wake never reaches a power terminal");
      }
    for(const auto mode:{Mode::ModernStandby,Mode::Sleep})
     for(const auto* point:{"before-helper","capabilities","sleep-request-route","privilege","sleep-request-dispatch","source-final-query"}){
        reset(mode==Mode::ModernStandby);stagedOrigin=sgNowFileTime()-20000;injectedWakeTime=sgNowFileTime()-10000;injectWakeAt=point;
        if(injectWakeAt=="before-helper")g_sgLastPowerButtonWakeEventFileTime=injectedWakeTime;
        run("staged manual wake veto / "+std::string(ymcc::sleeprequest::name(mode))+" / "+point,{},false,mode,ERROR_CANCELLED,mode,mode,true);
     }
    for(const auto kind:{SgRetryKind::EntryFailure,SgRetryKind::UnexpectedWake,SgRetryKind::SleepTimeout}){
      reset();selectRetryKind(kind);setRetryIntent(sgNowFileTime()-10000);g_sgLastPowerButtonWakeEventFileTime=sgNowFileTime()-20000;
      run("earlier cycle wake permits fresh user sleep / "+std::to_string(static_cast<unsigned>(kind)),{Mode::ModernStandby},true,Mode::ModernStandby);
      for(const auto mode:{Mode::ModernStandby,Mode::Sleep}){
        const auto suffix=" / "+std::to_string(static_cast<unsigned>(kind))+" / "+ymcc::sleeprequest::name(mode);
        reset(mode==Mode::ModernStandby);selectRetryKind(kind);setRetryIntent(0);
        run("no original intent cannot reach power terminal"+suffix,{},false,mode,ERROR_CANCELLED,mode,mode);
        reset(mode==Mode::ModernStandby);selectRetryKind(kind);setRetryIntent(testClock-10000);
        g_sgLatestUserSleepEntryFileTime=testClock-30000;g_sgLastPowerButtonWakeEventFileTime=testClock-20000;g_sgOriginalPowerButtonWakeFileTime=g_sgLastPowerButtonWakeEventFileTime.load();
        run("collection-time intent cannot hide unretired manually ended sleep"+suffix,{},false,mode,ERROR_CANCELLED,mode,mode);
        reset(mode==Mode::ModernStandby);selectRetryKind(kind);setRetryIntent(testClock-10000);
        g_sgLatestUserSleepEntryFileTime=testClock-30000;g_sgLastPowerButtonWakeEventFileTime=testClock-20000;g_sgOriginalPowerButtonWakeFileTime=g_sgLastPowerButtonWakeEventFileTime.load();g_sgClosedSleepIntentFileTime=testClock-15000;
        run("retired older entry cannot veto fresh user sleep"+suffix,{mode},true,mode,0,mode,mode);
        reset(mode==Mode::ModernStandby);selectRetryKind(kind);setRetryIntent(testClock-10000);
        g_sgLatestUserSleepEntryFileTime=testClock-10000;g_sgLastPowerButtonWakeEventFileTime=testClock-20000;g_sgOriginalPowerButtonWakeFileTime=g_sgLastPowerButtonWakeEventFileTime.load();
        run("newer original entry proves fresh user sleep after prior wake"+suffix,{mode},true,mode,0,mode,mode);
      }
    }
    for(const auto mode:{Mode::ModernStandby,Mode::Sleep}){
        reset(mode==Mode::ModernStandby);stagedOrigin=sgNowFileTime()-20000;finalSourceFailAt=1;
        run("lost source at final boundary vetoes staged / "+std::string(ymcc::sleeprequest::name(mode)),{},false,mode,ERROR_CANCELLED,mode,mode,true);
    }
    reset();modern={false,ERROR_NOT_SUPPORTED};finalSourceFailAt=2;stagedOrigin=sgNowFileTime()-20000;
    run("final source is rechecked before every staged fallback",{Mode::ModernStandby},false,Mode::Sleep,ERROR_CANCELLED,Mode::ModernStandby,Mode::Sleep,true);
    reset();finalSourceFailAt=1;run("independent primary request does not query abnormal observation source",{Mode::ModernStandby},true,Mode::ModernStandby);check(finalSourceQueries==0,"non-staged request never queries abnormal source");
    writeSuccess(argv[1]);return 0;
}catch(const std::exception&e){
    const json failed={{"status","failed"},{"scenarios",scenarios},{"checks",checks},{"selectedCases",selectedCases},{"failure",e.what()},
        {"realSleepCalls",0},{"mockSuspendCalls",totalSuspendCalls},{"monitorCalls",totalMonitorCalls},{"windowQueries",totalWindowQueries},{"physicalSleepConfirmed",false}};
    if(argc>1){std::ofstream out(argv[1]);out<<failed.dump(2)<<'\n';}
    std::cerr<<"CHECK_FAILED: "<<e.what()<<'\n';return 1;
}}
