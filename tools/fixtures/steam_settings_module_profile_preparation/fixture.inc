#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "deps/json/json.hpp"
using json=nlohmann::json;
#include "native/log_frequency_policy.h"
#include "native/log_snapshot_state.h"
static std::atomic<unsigned long long> g_powerGeneration{1};
#include "module-profile-log-classifier.inc"
// These are controlled leaf ports, not a native scope/provider implementation.
namespace ymcc::steamnative { enum class ReadPurpose { PrepareMutation }; }
namespace SleepSteamRecovery { struct Session {DWORD pid=0;unsigned long long created=0;std::wstring root;}; }
struct Identity {int presence=1;SleepSteamRecovery::Session session{900,100,L"Q:/Steam"};bool windowsSessionKnown=true;DWORD windowsSession=7;unsigned epoch=1,account=42;};
struct ObserverPort {
    std::shared_ptr<Identity> identity=std::make_shared<Identity>();
    auto identitySnapshot(){return std::shared_ptr<const Identity>(identity);}
    bool sameIdentityEpoch(const std::shared_ptr<const Identity>& old){return identity&&old&&identity->epoch==old->epoch&&identity->account==old->account;}
} g_steamSettingsObserver;
static std::atomic<bool> g_exitRequested{false},g_sleepRecoveryAutoEnabled{true};
static std::atomic<unsigned long long> g_sleepRecoveryAutoConfigEpoch{1};
static struct {bool isolated=false;} g_aiFanMockSession;
static bool powerReady=true,scopeAvailable=true,scopeThrows=false,prepareThrows=false,poolOk=true,settingsThrow=false,mouseThrow=false;
static unsigned scopeCalls=0,prepareCalls=0,moduleFacts=0,settingsCalls=0,mouseCalls=0,healthGrants=0;
static std::function<void()> prepareHook,scopeHook,settingsHook;
static json contexts=json::array(),logs=json::array();
static json residentLogs=json::array(),detailLogs=json::array();
static bool fixtureDetail=false,sparseLogThrows=false;
static std::mutex g_sdmMx;
static std::atomic<bool> g_sofQueued{false},g_sofPending{false},g_sdmRefreshRequested{false},g_sdmApplyRequested{false},g_sdmEventPending{false};
static std::vector<std::function<void()>> jobs;
#include "module-profile-state.inc"
struct ScopePort {bool current()const{return powerReady;}};
static std::shared_ptr<ScopePort> nativeSteamNativeReadScope(const SleepSteamRecovery::Session&,ymcc::steamnative::ReadPurpose){
    ++scopeCalls;if(scopeHook)scopeHook();if(scopeThrows)throw std::runtime_error("scope-leaf");
    return scopeAvailable?std::make_shared<ScopePort>():nullptr;
}
namespace ymcc::steamrecordwin {
enum class ProfilePreparationStatus { BindingUnavailable,CurrentRejected,Exception,Prepared };
static const char* profilePreparationLabel(ProfilePreparationStatus value){
    switch(value){case ProfilePreparationStatus::BindingUnavailable:return "binding-unavailable";case ProfilePreparationStatus::CurrentRejected:return "current-rejected";case ProfilePreparationStatus::Exception:return "exception";default:return "prepared";}
}
struct Context {DWORD steamPid=0;unsigned long long steamBirth=0;std::wstring steamRoot;DWORD windowsSession=0;bool sessionBound=false;};
static ProfilePreparationStatus prepareModuleProfile(const Context& c,const std::function<bool()>& current){
    ++prepareCalls;contexts.push_back({{"pid",c.steamPid},{"birth",c.steamBirth},{"session",c.windowsSession},{"bound",c.sessionBound}});
    if(!current())return ProfilePreparationStatus::CurrentRejected;
    if(prepareHook)prepareHook();
    if(prepareThrows)throw std::runtime_error("prepare-leaf");
    ++moduleFacts; // Permitted old-key disk facts completion after cancellation.
    return ProfilePreparationStatus::Prepared;
}
}
#include "module-profile-helper.inc"
#if defined(YMCC_MODULE_PROFILE_BEFORE)
static void steamSettingsQueue(const char*);
#else
static void steamSettingsQueue(const char*,bool=false);
#endif
static bool poolSubmit(std::function<void()> f){if(!poolOk)return false;jobs.push_back(std::move(f));return true;}
static void steamClientSettingsAttemptLocked(){++settingsCalls;if(settingsHook)settingsHook();if(settingsThrow)throw std::runtime_error("settings-leaf");g_steamClientPending=false;}
static void sdmApplyLocked(){++mouseCalls;if(mouseThrow)throw std::runtime_error("mouse-leaf");g_sdmEventPending=false;}
static json sdmStateLocked(bool){return {{"pending",g_sdmEventPending.load()}};}
static void ipc_emit(const char*,const json&){}
static void appendNativeLifecycleLog(const char* event,const json& value){
    if(sparseLogThrows&&std::strcmp(event,"steam-module-profile-preparation")==0)throw std::runtime_error("sparse-log-leaf");
    const auto lane=ymcc::logging::nativeLane(nativeLifecycleEventIsBase(event),nativeLifecycleRecordIsStreaming(event,value),fixtureDetail);
    if(lane==ymcc::logging::NativeLane::event)residentLogs.push_back({{"event",event},{"value",value}});
    if(lane==ymcc::logging::NativeLane::detail)detailLogs.push_back({{"event",event},{"value",value}});
    // Preserve original raw batch capture for its existing semantic assertions.
    if(std::strcmp(event,"steam-settings-batch")==0)logs.push_back(value);
}
#include "module-profile-queue.inc"
namespace module_profile_fixture {
static json cases=json::array();static json* active=nullptr;
static void require(bool ok,const char* name){(*active)["assertions"].push_back({{"name",name},{"passed",ok}});if(!ok)throw std::runtime_error(name);}
static void reset(){
    jobs.clear();logs=json::array();contexts=json::array();residentLogs=json::array();detailLogs=json::array();fixtureDetail=sparseLogThrows=false;prepareHook={};scopeHook={};settingsHook={};
    g_steamSettingsObserver.identity=std::make_shared<Identity>();g_exitRequested=false;g_sleepRecoveryAutoEnabled=true;g_sleepRecoveryAutoConfigEpoch=1;g_aiFanMockSession.isolated=false;
    powerReady=scopeAvailable=poolOk=true;scopeThrows=prepareThrows=settingsThrow=mouseThrow=false;
    scopeCalls=prepareCalls=moduleFacts=settingsCalls=mouseCalls=healthGrants=0;
    g_sofQueued=g_sofPending=g_sdmRefreshRequested=g_sdmApplyRequested=g_sdmEventPending=false;
    g_steamSettingsQueued=g_steamClientPending=false;g_steamSettingsSerial=0;
    g_steamModuleProfilePreparationPending=false;
}
static void enqueue(bool prepare){
#if defined(YMCC_MODULE_PROFILE_BEFORE)
    // Before has no preparation flag: execute its original one-argument queue.
    (void)prepare;steamSettingsQueue("steam-session");
#else
    steamSettingsQueue("steam-session",prepare);
#endif
}
static void next(){require(!jobs.empty(),"one-owned-settings-pool-task-present");auto f=std::move(jobs.front());jobs.erase(jobs.begin());f();}
static std::string result(){return logs.empty()?"missing":logs.back().value("moduleProfilePreparation",std::string("missing"));}
static void replaceIdentity(){auto copy=std::make_shared<Identity>(*g_steamSettingsObserver.identity);++copy->epoch;copy->account=43;g_steamSettingsObserver.identity=copy;}
static void run(const std::string& name,const std::function<void()>& body){
    json c={{"name",name},{"assertions",json::array()}};active=&c;reset();try{body();}catch(const std::exception& e){c["error"]=e.what();}
    require(healthGrants==0,"leaf-ports-and-real-helper-never-produce-health-grant");
    c["observed"]={{"prepareCalls",prepareCalls},{"moduleFacts",moduleFacts},{"settingsCalls",settingsCalls},{"mouseCalls",mouseCalls},{"queueRemaining",jobs.size()},{"logs",logs},{"residentLogs",residentLogs},{"detailLogs",detailLogs}};
    c["passed"]=!c.contains("error");cases.push_back(c);active=nullptr;
}
static void burst(){for(int i=0;i<8;++i)enqueue(true);enqueue(false);require(jobs.size()==1,"session-true-burst-coalesced-to-one-task");next();require(prepareCalls==1,"original-queue-must-forward-session-preparation-flag");require(moduleFacts==1&&result()=="prepared","one-current-result-adopted");}
static void positives(){
    run("module-profile/session-true-burst-coalesced",burst);
    run("module-profile/same-value-false-no-preparation",[]{enqueue(false);enqueue(false);next();require(prepareCalls==0&&result()=="not-attempted","false-same-value-notification-does-not-prepare");});
    run("module-profile/locks-lane-released-next-settings-accepted",[]{
        g_sdmEventPending=true;g_steamClientPending=true;
        prepareHook=[]{
            bool a=false,b=false;std::thread probe([&]{a=g_steamSettingsMx.try_lock();if(a)g_steamSettingsMx.unlock();b=g_sdmMx.try_lock();if(b)g_sdmMx.unlock();});probe.join();
            require(a&&b,"actual-settings-and-mouse-locks-released-before-prepare");
            require(!g_steamSettingsQueued.load()&&!g_sofQueued.load(),"queued-and-overlay-lane-false-before-prepare");
            g_steamClientPending=true;steamSettingsQueue("real-user-followup");require(jobs.size()==1,"later-real-settings-accepted-during-preparation");
        };
        enqueue(true);next();prepareHook={};next();require(settingsCalls==2&&mouseCalls==1&&prepareCalls==1,"later-settings-executed-without-preparation-replay");
    });
    for(const std::string cause:{"off","exit","nonready","account","epoch"}){
        const auto cancel=[cause]{if(cause=="off")g_sleepRecoveryAutoEnabled=false;else if(cause=="exit")g_exitRequested=true;else if(cause=="nonready")powerReady=false;else if(cause=="account")replaceIdentity();else ++g_sleepRecoveryAutoConfigEpoch;};
        run("module-profile/before-prepare-"+cause,[=]{scopeHook=cancel;enqueue(true);next();require(prepareCalls==0,"revoked-before-prepare-no-leaf-call");require(result()=="current-rejected","revoked-scope-not-adopted");});
        run("module-profile/during-prepare-"+cause,[=]{prepareHook=cancel;enqueue(true);next();require(prepareCalls==1&&moduleFacts==1,"old-key-pure-facts-may-finish-after-cancel");require(result()=="current-rejected","late-prepared-result-not-adopted");});
    }
    run("module-profile/exception-cleanup-later-settings",[]{prepareThrows=true;enqueue(true);next();require(result()=="exception"&&!g_steamSettingsQueued.load(),"exception-return-clears-lane-and-does-not-grant-health");prepareThrows=false;enqueue(true);next();require(result()=="prepared"&&prepareCalls==2,"helper-mutex-released-after-catch");});
    run("module-profile/settings-and-mouse-catch-still-release-lane",[]{settingsThrow=mouseThrow=true;g_steamClientPending=g_sdmEventPending=true;enqueue(true);next();require(!g_steamClientPending&&!g_sdmEventPending&&!g_steamSettingsQueued,"existing-settings-catches-clear-only-own-pending");require(result()=="prepared","optional-profile-remains-pure-facts");});
    run("module-profile/no-public-scope",[]{scopeAvailable=false;enqueue(true);next();require(prepareCalls==0&&result()=="current-rejected","no-public-read-scope-no-prepare");});
    run("module-profile/no-bound-identity",[]{g_steamSettingsObserver.identity.reset();enqueue(true);next();require(prepareCalls==0&&result()=="binding-unavailable","no-bound-public-identity-no-prepare");});
}

static json terminalRows(){json rows=json::array();for(const auto& r:residentLogs)if(r.value("event",std::string())=="steam-module-profile-preparation")rows.push_back(r);return rows;}
static void logBurst(){
    for(unsigned i=0;i<1000;++i)enqueue(true);enqueue(false);require(jobs.size()==1,"logging-burst-reuses-one-original-task");next();
    const auto rows=terminalRows();require(rows.size()==1,"one-sparse-terminal-for-one-consumed-profile-request");
    require(rows[0]["value"]==json{{"source","steam-session"},{"status","prepared"},{"scope","session-module-profile-facts-only"}},"fixed-terminal-payload-only-original-status-and-source");
    require(prepareCalls==1&&scopeCalls==1&&moduleFacts==1,"logging-does-not-prepare-or-read-scope-twice");
    require(!nativeLifecycleEventIsBase("steam-settings-batch")&&!nativeLifecycleEventIsBase("steam-module-profile-preparation-typo"),"batch-and-lookalike-not-promoted-to-base");
}
static void logCases(){
    run("module-profile/log-terminal-detail-off",logBurst);
    run("module-profile/log-false-and-isolated-no-terminal",[]{for(unsigned i=0;i<1000;++i)enqueue(false);next();require(terminalRows().empty()&&prepareCalls==0,"false-only-burst-does-not-create-sparse-terminal");g_aiFanMockSession.isolated=true;enqueue(true);require(jobs.empty()&&terminalRows().empty(),"isolated-queue-does-not-produce-terminal");});
    run("module-profile/log-logger-exception-does-not-replay",[]{sparseLogThrows=true;enqueue(true);next();require(prepareCalls==1&&moduleFacts==1&&!g_steamModuleProfilePreparationPending.load()&&!g_steamSettingsQueued.load()&&jobs.empty(),"throwing-logger-cannot-refund-consumed-request-or-hold-queue");sparseLogThrows=false;enqueue(false);next();require(prepareCalls==1&&terminalRows().empty(),"false-followup-does-not-replay-failed-log");enqueue(true);next();require(prepareCalls==2&&terminalRows().size()==1,"next-real-session-request-can-log-once");});
    run("module-profile/log-pool-rejection-waits-real-next-request",[]{poolOk=false;enqueue(true);require(jobs.empty()&&terminalRows().empty()&&g_steamModuleProfilePreparationPending.load(),"unsubmitted-task-is-not-consumed-or-terminal");poolOk=true;enqueue(false);next();require(prepareCalls==1&&terminalRows().size()==1,"next-original-queue-consumes-retained-pending-once");});
    run("module-profile/log-revocation-and-error-status-not-health",[]{scopeAvailable=false;enqueue(true);next();require(terminalRows().size()==1&&terminalRows()[0]["value"]["status"]=="current-rejected"&&prepareCalls==0,"rejected-profile-consumption-keeps-original-label-without-leaf-call");scopeAvailable=true;prepareThrows=true;enqueue(true);next();require(terminalRows().size()==2&&terminalRows()[1]["value"]["status"]=="exception"&&moduleFacts==0,"caught-leaf-exception-is-not-ready-or-health");});
    run("module-profile/log-detail-on-still-one-resident-receipt",[]{fixtureDetail=true;enqueue(true);next();require(terminalRows().size()==1&&residentLogs.size()==1&&detailLogs.size()==1,"detail-on-does-not-duplicate-sparse-terminal-and-batch-stays-detailed");enqueue(false);next();require(terminalRows().size()==1&&detailLogs.size()==2,"same-value-false-only-adds-original-detailed-batch");});
}
static int main(int argc,char** argv){
    const std::string selection=argc==2?argv[1]:"";
    if(selection=="--module-profile-preparation-before-control")run("module-profile/session-true-burst-coalesced",burst);
    else if(selection=="--module-profile-preparation"){positives();logCases();}else return 2;
    bool passed=true;unsigned checks=0;for(const auto& c:cases){passed=passed&&c.at("passed").get<bool>();checks+=static_cast<unsigned>(c.at("assertions").size());}
    std::cout<<"MODULE_PROFILE_PREPARATION_RESULT "<<json{{"selection",selection},{"cases",cases},{"checks",checks},{"passed",passed},{"defaultGroupExecuted",false},{"completeNativeOrActualProvider",false}}.dump()<<'\n';return passed?0:1;
}
}
int main(int argc,char** argv){return module_profile_fixture::main(argc,argv);}
