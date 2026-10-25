#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <powrprof.h>
#include "json.hpp"
#include "sleep_request_fallback.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using json=nlohmann::json;
using Mode=ymcc::sleeprequest::Mode;
static unsigned checks=0,scenarios=0;
static void check(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}
enum class SgRetryKind {None,EntryFailure,UnexpectedWake};
struct {SgRetryKind retryKind=SgRetryKind::EntryFailure;} g_sgTask;
static bool g_guardEnabled=true,g_sgRetryInProgress=true,g_sgRetryEntryFailure=true,g_sgResleepEnabled=true;
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
static void injectConfirmedWake(const char* point){if(injectWakeAt==point)g_sgLastPowerButtonWakeEventFileTime=injectedWakeTime;}
static void sgRecordFact(const char* key,const json& value){facts.emplace_back(key,value);injectConfirmedWake(key);}
struct TestReply {bool accepted=true;DWORD error=0;};
static SYSTEM_POWER_CAPABILITIES capability{};
static bool testCapsKnown=true,validWindow=true,wakeAfterModern=false,generationAfterModern=false;
static bool privilegeOk=true,stagedPermission=true,cancelDuringPrivilege=false;
static bool wakeDuringCapabilities=false,wakeDuringPrivilege=false,disableDuringPrivilege=false;
static bool sgAbnormalPermitted(){return stagedPermission && (!stagedOrigin || g_sgLastPowerButtonWakeEventFileTime.load()<stagedOrigin);}
static DWORD privilegeError=ERROR_NOT_ALL_ASSIGNED,lastError=0;
static int privilegeCalls=0;
static HWND g_hwnd=reinterpret_cast<HWND>(1);
static TestReply modern{},sleepReply{},hibernateReply{};
static std::vector<Mode> calls;
static DWORD fakeGetLastError(){return lastError;}
static void fakeSetLastError(DWORD error){lastError=error;}
static BOOL fakeGetPwrCapabilities(PSYSTEM_POWER_CAPABILITIES out){injectConfirmedWake("capabilities");if(wakeDuringCapabilities)g_sgLastPowerButtonWakeEventFileTime=sgNowFileTime()+1;*out=capability;if(!testCapsKnown)lastError=ERROR_ACCESS_DENIED;return testCapsKnown;}
static HANDLE fakeGetCurrentProcess(){return reinterpret_cast<HANDLE>(2);}
static BOOL fakeOpenProcessToken(HANDLE,DWORD,PHANDLE token){*token=reinterpret_cast<HANDLE>(3);return TRUE;}
static BOOL fakeLookupPrivilegeValueW(LPCWSTR,LPCWSTR,PLUID){return TRUE;}
static BOOL fakeAdjustTokenPrivileges(HANDLE,BOOL,PTOKEN_PRIVILEGES,DWORD,PTOKEN_PRIVILEGES,PDWORD){injectConfirmedWake("privilege");++privilegeCalls;if(cancelDuringPrivilege)stagedPermission=false;if(wakeDuringPrivilege)g_sgLastPowerButtonWakeEventFileTime=sgNowFileTime()+1;if(disableDuringPrivilege)g_sgRetryEntryFailure=false;lastError=privilegeOk ? 0 : privilegeError;return privilegeOk || privilegeError==ERROR_NOT_ALL_ASSIGNED;}
static BOOL fakeCloseHandle(HANDLE){return TRUE;}
static BOOL fakeIsWindow(HWND h){injectConfirmedWake("window-validation");return h==g_hwnd && validWindow;}
static LRESULT fakeSendMessageTimeoutW(HWND h,UINT msg,WPARAM w,LPARAM l,UINT flags,UINT timeout,PDWORD_PTR reply){
    check(h==g_hwnd && msg==WM_SYSCOMMAND && w==SC_MONITORPOWER && l==2,"modern public monitor-off request");
    check(flags==SMTO_ABORTIFHUNG && timeout==1000,"modern bounded delivery");
    calls.push_back(Mode::ModernStandby);*reply=0;lastError=modern.error;
    if(wakeAfterModern)g_sgLastPowerButtonWakeEventFileTime=sgNowFileTime()+1;
    if(generationAfterModern)++testGeneration;
    return modern.accepted ? 1 : 0;
}
static BOOLEAN fakeSetSuspendState(BOOLEAN hibernate,BOOLEAN critical,BOOLEAN disableWake){
    check(critical==FALSE && disableWake==FALSE,"no force and no wake-event suppression");
    const auto mode=hibernate ? Mode::Hibernate : Mode::Sleep;calls.push_back(mode);
    const auto reply=hibernate ? hibernateReply : sleepReply;lastError=reply.error;return reply.accepted;
}
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
static void reset(bool ms=true,bool ordinary=true,bool hibernate=true){
    capability={};capability.AoAc=ms;capability.SystemS3=ordinary;capability.SystemS4=hibernate;capability.HiberFilePresent=hibernate;
    testCapsKnown=validWindow=privilegeOk=true;wakeAfterModern=generationAfterModern=false;
    stagedPermission=true;cancelDuringPrivilege=wakeDuringCapabilities=wakeDuringPrivilege=disableDuringPrivilege=false;privilegeError=ERROR_NOT_ALL_ASSIGNED;lastError=0;privilegeCalls=0;
    g_guardEnabled=g_sgRetryInProgress=g_sgRetryEntryFailure=g_sgResleepEnabled=true;g_sgLastPowerButtonSleepIntentFileTime=1000000-20000;g_sgTask.retryKind=SgRetryKind::EntryFailure;
    g_sgLastPowerButtonWakeEventFileTime=0;g_sgAutomaticSleepTarget=Mode::None;testGeneration=7;
    g_sgLatestUserSleepEntryFileTime=0;g_sgClosedSleepIntentFileTime=0;g_sgOriginalPowerButtonWakeFileTime=0;
    modern=sleepReply=hibernateReply={};calls.clear();facts.clear();injectWakeAt.clear();injectedWakeTime=stagedOrigin=0;testClock=1000000;
}
static json outcomes=json::array();
static void run(const char* label,const std::vector<Mode>& expected,bool accepted,Mode target,DWORD error=0){
    ++scenarios;const auto result=sgRequestSystemSleep();
    check(calls==expected,label);check(result.accepted==accepted,label);check(result.target==target,label);
    check(result.requestError==error,label);
    check(!facts.empty() && facts.back().first=="sleep-request-route-result","route result always recorded");
    check(result.accepted || g_sgAutomaticSleepTarget.load()==Mode::None,"failed request never retains a pending target");
    check(sgAutomaticClassicSleepPending()==(accepted && (target==Mode::Sleep || target==Mode::Hibernate)),"classic receipt matches accepted target");
    json modes=json::array();for(auto mode:calls)modes.push_back(ymcc::sleeprequest::name(mode));
    outcomes.push_back({{"name",label},{"calls",modes},{"accepted",result.accepted},{"target",ymcc::sleeprequest::name(result.target)},
        {"error",result.requestError},{"privilegeCalls",privilegeCalls}});
}
int main(int argc,char**argv){try{
    if(argc!=3)throw std::runtime_error("result.json and archived wake fixture required");
    for(unsigned mask=0;mask<16;++mask){
        const bool ms=(mask&1)!=0,ordinary=(mask&2)!=0,hib=(mask&4)!=0,isKnown=(mask&8)!=0;
        reset(ms,ordinary,hib);testCapsKnown=isKnown;
        const auto expected=!isKnown ? Mode::None : ms ? Mode::ModernStandby : ordinary ? Mode::Sleep : hib ? Mode::Hibernate : Mode::None;
        run("capability priority matrix",expected==Mode::None ? std::vector<Mode>{} : std::vector<Mode>{expected},expected!=Mode::None,expected,
            !isKnown ? ERROR_ACCESS_DENIED : expected==Mode::None ? ERROR_NOT_SUPPORTED : 0);
    }
    reset();testCapsKnown=false;g_sgAutomaticSleepTarget=Mode::Hibernate;run("new failed selection clears stale receipt",{},false,Mode::None,ERROR_ACCESS_DENIED);
    reset();run("modern accepted stops all downgrade",{Mode::ModernStandby},true,Mode::ModernStandby);check(privilegeCalls==0,"modern does not require shutdown privilege");
    reset();modern={false,ERROR_NOT_SUPPORTED};run("explicit modern unsupported falls to sleep",{Mode::ModernStandby,Mode::Sleep},true,Mode::Sleep);
    reset();modern=sleepReply={false,ERROR_NOT_SUPPORTED};run("explicit unsupported falls through all modes",{Mode::ModernStandby,Mode::Sleep,Mode::Hibernate},true,Mode::Hibernate);check(privilegeCalls==1,"one privilege preparation across fallback chain");
    reset(true,false,true);modern={false,ERROR_NOT_SUPPORTED};run("skip unsupported ordinary capability",{Mode::ModernStandby,Mode::Hibernate},true,Mode::Hibernate);
    for(const auto code:{ERROR_INVALID_FUNCTION,ERROR_CALL_NOT_IMPLEMENTED}){
        reset();modern={false,static_cast<DWORD>(code)};run("operation unsupported code allows downgrade",{Mode::ModernStandby,Mode::Sleep},true,Mode::Sleep);
    }
    for(const auto code:{ERROR_ACCESS_DENIED,ERROR_TIMEOUT,ERROR_INVALID_PARAMETER,ERROR_GEN_FAILURE,ERROR_PRIVILEGE_NOT_HELD}){
        reset();modern={false,static_cast<DWORD>(code)};run("generic modern failure stops downgrade",{Mode::ModernStandby},false,Mode::ModernStandby,code);
        reset(false);sleepReply={false,static_cast<DWORD>(code)};run("generic sleep failure stops hibernate",{Mode::Sleep},false,Mode::Sleep,code);
    }
    reset();validWindow=false;run("missing window is not unsupported",{},false,Mode::ModernStandby,ERROR_INVALID_WINDOW_HANDLE);
    reset(false);privilegeOk=false;run("privilege missing cannot downgrade",{},false,Mode::Sleep,ERROR_NOT_ALL_ASSIGNED);
    reset(false);privilegeOk=false;privilegeError=ERROR_CALL_NOT_IMPLEMENTED;run("prerequisite unsupported is not operation unsupported",{},false,Mode::Sleep,ERROR_CALL_NOT_IMPLEMENTED);
    reset();g_sgLastPowerButtonWakeEventFileTime=sgNowFileTime();run("confirmed wake cancels before first call",{},false,Mode::ModernStandby,ERROR_CANCELLED);
    reset();modern={false,ERROR_NOT_SUPPORTED};wakeAfterModern=true;run("confirmed wake cancels between modes",{Mode::ModernStandby},false,Mode::Sleep,ERROR_CANCELLED);
    reset();modern={false,ERROR_NOT_SUPPORTED};generationAfterModern=true;run("new testGeneration cancels between modes",{Mode::ModernStandby},false,Mode::Sleep,ERROR_CANCELLED);
    reset(false,false,true);capability.HiberFilePresent=FALSE;run("missing hiberfile never enables it",{},false,Mode::None,ERROR_NOT_SUPPORTED);
    reset();modern=sleepReply=hibernateReply={false,ERROR_NOT_SUPPORTED};run("all calls explicitly unsupported",{Mode::ModernStandby,Mode::Sleep,Mode::Hibernate},false,Mode::None,ERROR_NOT_SUPPORTED);
    reset();modern=sleepReply={false,ERROR_NOT_SUPPORTED};
    auto staged=sgRequestSystemSleep(Mode::ModernStandby,Mode::Sleep,true);++scenarios;
    check(calls==std::vector<Mode>{Mode::ModernStandby,Mode::Sleep}&&!staged.accepted,"early stages cannot fall through to S4");
    reset();staged=sgRequestSystemSleep(Mode::Hibernate,Mode::Hibernate,true);++scenarios;
    check(calls==std::vector<Mode>{Mode::Hibernate}&&staged.accepted,"S4 stage calls S4 only");
    reset();stagedPermission=false;staged=sgRequestSystemSleep(Mode::ModernStandby,Mode::Sleep,true);++scenarios;
    check(calls.empty()&&staged.requestError==ERROR_CANCELLED,"staged cancellation before helper start wins");
    reset(false);cancelDuringPrivilege=true;staged=sgRequestSystemSleep(Mode::Hibernate,Mode::Hibernate,true);++scenarios;
    check(calls.empty()&&staged.requestError==ERROR_CANCELLED,"Windows success or wake during privilege prevents S4");
    reset();g_sgLastPowerButtonSleepIntentFileTime=sgNowFileTime()-20000;g_sgLastPowerButtonWakeEventFileTime=sgNowFileTime()-10000;
    run("confirmed current-cycle wake before helper start cancels",{},false,Mode::ModernStandby,ERROR_CANCELLED);
    reset();g_sgLastPowerButtonSleepIntentFileTime=sgNowFileTime()-10000;g_sgLastPowerButtonWakeEventFileTime=sgNowFileTime()-20000;
    run("old wake before new manual intent cannot veto sleep",{Mode::ModernStandby},true,Mode::ModernStandby);
    reset();g_sgRetryEntryFailure=false;run("disabled entry feature cannot invoke request",{},false,Mode::ModernStandby,ERROR_CANCELLED);
    reset();g_sgRetryEntryFailure=false;g_sgTask.retryKind=SgRetryKind::UnexpectedWake;
    run("entry feature off preserves independent unexpected wake",{Mode::ModernStandby},true,Mode::ModernStandby);
    reset();g_sgRetryEntryFailure=false;auto independentStaged=sgRequestSystemSleep(Mode::ModernStandby,Mode::Sleep,true);++scenarios;
    check(independentStaged.accepted&&calls==std::vector<Mode>{Mode::ModernStandby},"staged permission retains its own owner checks");
    reset();wakeDuringCapabilities=true;run("wake during capability read vetoes first call",{},false,Mode::ModernStandby,ERROR_CANCELLED);
    reset(false);wakeDuringPrivilege=true;run("wake during privilege preparation vetoes sleep",{},false,Mode::Sleep,ERROR_CANCELLED);
    reset(false);disableDuringPrivilege=true;run("entry option off during privilege preparation vetoes sleep",{},false,Mode::Sleep,ERROR_CANCELLED);
    reset(false);disableDuringPrivilege=true;g_sgTask.retryKind=SgRetryKind::UnexpectedWake;
    run("entry option off during privilege preserves unexpected wake",{Mode::Sleep},true,Mode::Sleep);
    json archived;{std::ifstream input(argv[2]);input>>archived;}
    check(archived.at("kind")=="archived-manual-wake-dispatch-race","manual wake fixture identity");
    reset();testClock=std::stoull(archived.at("requestPreparationFileTime").get<std::string>());
    g_sgTask.retryKind=SgRetryKind::UnexpectedWake;
    g_sgLastPowerButtonSleepIntentFileTime=std::stoull(archived.at("originalIntentFileTime").get<std::string>());
    injectedWakeTime=std::stoull(archived.at("kernelWakeFileTime").get<std::string>());injectWakeAt="capabilities";
    check(g_sgLastPowerButtonSleepIntentFileTime.load()<injectedWakeTime&&injectedWakeTime<testClock,"archive original chronology is retained");
    run("G3E archived manual wake before helper original time and callback during preparation",{},false,Mode::ModernStandby,ERROR_CANCELLED);
    check(calls.size()==archived.at("expectedPowerCallsAfterFix").get<unsigned>(),"archived callback evidence cancels automatic sleep");
    // The field record proves callback-before-terminal, not its exact position
    // inside the helper. Replay all relevant positions with the actual adapter.
    for(const auto kind:{SgRetryKind::EntryFailure,SgRetryKind::UnexpectedWake})
     for(const auto mode:{Mode::ModernStandby,Mode::Sleep,Mode::Hibernate})
      for(const auto* point:{"before-helper","capabilities","sleep-request-route","privilege","sleep-request-dispatch","window-validation"}){
        if((std::string(point)=="window-validation"&&mode!=Mode::ModernStandby)||
           (std::string(point)=="privilege"&&mode==Mode::ModernStandby))continue;
        reset(mode==Mode::ModernStandby,mode==Mode::Sleep,mode==Mode::Hibernate);
        g_sgTask.retryKind=kind;g_sgLastPowerButtonSleepIntentFileTime=sgNowFileTime()-20000;
        injectedWakeTime=sgNowFileTime()-10000;injectWakeAt=point;
        if(injectWakeAt=="before-helper")g_sgLastPowerButtonWakeEventFileTime=injectedWakeTime;
        run("original current-cycle manual wake vetoes every retry kind and terminal phase",{},false,mode,ERROR_CANCELLED);
        check(calls.size()==archived.at("expectedPowerCallsAfterFix").get<unsigned>(),"archived manual wake never reaches a power terminal");
      }
    for(const auto mode:{Mode::ModernStandby,Mode::Sleep,Mode::Hibernate})
     for(const auto* point:{"before-helper","capabilities","sleep-request-route","privilege","sleep-request-dispatch","window-validation"}){
        if((std::string(point)=="window-validation"&&mode!=Mode::ModernStandby)||
           (std::string(point)=="privilege"&&mode==Mode::ModernStandby))continue;
        reset(mode==Mode::ModernStandby,mode==Mode::Sleep,mode==Mode::Hibernate);
        stagedOrigin=sgNowFileTime()-20000;injectedWakeTime=sgNowFileTime()-10000;injectWakeAt=point;
        if(injectWakeAt=="before-helper")g_sgLastPowerButtonWakeEventFileTime=injectedWakeTime;
        const auto request=sgRequestSystemSleep(mode,mode,true);++scenarios;
        check(!request.accepted&&request.requestError==ERROR_CANCELLED&&calls.empty(),"staged sleep and S4 respect callback wake at every terminal phase");
     }
    for(const auto kind:{SgRetryKind::EntryFailure,SgRetryKind::UnexpectedWake}){
      reset();g_sgTask.retryKind=kind;g_sgLastPowerButtonSleepIntentFileTime=sgNowFileTime()-10000;
      g_sgLastPowerButtonWakeEventFileTime=sgNowFileTime()-20000;
      run("earlier cycle manual wake permits a fresh user sleep for both retry kinds",{Mode::ModernStandby},true,Mode::ModernStandby);
    }
    for(const auto kind:{SgRetryKind::EntryFailure,SgRetryKind::UnexpectedWake})
     for(const auto mode:{Mode::ModernStandby,Mode::Sleep,Mode::Hibernate}){
        reset(mode==Mode::ModernStandby,mode==Mode::Sleep,mode==Mode::Hibernate);
        g_sgTask.retryKind=kind;g_sgLastPowerButtonSleepIntentFileTime=0;
        run("autonomous retry with no original intent cannot reach any power terminal",{},false,mode,ERROR_CANCELLED);
     }
    for(const auto kind:{SgRetryKind::EntryFailure,SgRetryKind::UnexpectedWake})
     for(const auto mode:{Mode::ModernStandby,Mode::Sleep,Mode::Hibernate}){
        reset(mode==Mode::ModernStandby,mode==Mode::Sleep,mode==Mode::Hibernate);
        g_sgTask.retryKind=kind;g_sgLastPowerButtonSleepIntentFileTime=testClock-10000;
        g_sgLatestUserSleepEntryFileTime=testClock-30000;g_sgLastPowerButtonWakeEventFileTime=testClock-20000;
        g_sgOriginalPowerButtonWakeFileTime=g_sgLastPowerButtonWakeEventFileTime.load();
        run("collection-time intent cannot hide an unretired original sleep ended by a manual wake",{},false,mode,ERROR_CANCELLED);
        reset(mode==Mode::ModernStandby,mode==Mode::Sleep,mode==Mode::Hibernate);
        g_sgTask.retryKind=kind;g_sgLastPowerButtonSleepIntentFileTime=testClock-10000;
        g_sgLatestUserSleepEntryFileTime=testClock-30000;g_sgLastPowerButtonWakeEventFileTime=testClock-20000;
        g_sgOriginalPowerButtonWakeFileTime=g_sgLastPowerButtonWakeEventFileTime.load();
        g_sgClosedSleepIntentFileTime=testClock-15000;
        run("a retired older entry cannot veto a new user sleep",{mode},true,mode);
        reset(mode==Mode::ModernStandby,mode==Mode::Sleep,mode==Mode::Hibernate);
        g_sgTask.retryKind=kind;g_sgLastPowerButtonSleepIntentFileTime=testClock-10000;
        g_sgLatestUserSleepEntryFileTime=testClock-10000;g_sgLastPowerButtonWakeEventFileTime=testClock-20000;
        g_sgOriginalPowerButtonWakeFileTime=g_sgLastPowerButtonWakeEventFileTime.load();
        run("a newer original entry proves a fresh user sleep after a prior wake",{mode},true,mode);
     }
    json result={{"status","passed"},{"scenarios",scenarios},{"checks",checks},{"results",outcomes},{"realSleepCalls",0},
        {"productionFunctions","sgRequestSystemSleep;sgPrepareSleepPrivilege;sgAutomaticClassicSleepPending"}};
    std::ofstream out(argv[1]);out<<result.dump(2)<<'\n';std::cout<<json{{"status","passed"},{"scenarios",scenarios},{"checks",checks}}.dump()<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"CHECK_FAILED: "<<e.what()<<'\n';return 1;}}
