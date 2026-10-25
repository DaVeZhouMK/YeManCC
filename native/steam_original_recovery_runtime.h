#pragma once
#include "steam_original_recovery_policy.h"
#include "steam_original_recovery_dual_route.h"
#include "steam_original_recovery_config.h"
#include "steam_original_recovery_wake_watch.h"
#include "steam_original_recovery_storage.h"
#include "steam_original_recovery_worker.h"
#include <condition_variable>
#include <deque>
#include <fstream>
#include <memory>

// The only executable owner is this YMCC instance. Power/window/game callbacks
// only queue identity changes; no callback performs disk/CDP/debugger work.
// The engine adapter is defined in main.cpp, next to its ordinary live bridge.
static json steamOriginalRecoveryEngineHandle(const json& request,
    const std::function<bool()>& stillCurrent,
    const std::function<void(const json&)>& emit,
    const std::function<bool()>& observationCurrent,
    std::uintptr_t wakeSignal);
namespace SteamOriginalRecovery {
using Trigger=ymcc::steamOriginal::Trigger;
using RecoveryRoute=ymcc::steamOriginal::RecoveryRoute;
using Identity=ymcc::steamOriginal::Identity;
using Scope=ymcc::steamOriginal::Scope;
struct Pending {Trigger kind;unsigned long long generation=0,sequence=0;};
struct Control {
    std::mutex mx,snapshotMx,logMx;std::condition_variable cv;
    bool stop=false,startupPending=true;std::deque<Pending> pending;
    std::shared_ptr<void> observationEvent;
    unsigned long long sequence=0,observationSequence=0;std::uint64_t latestPower=0,deferredWake=0;
    Identity observedTarget{},observedSteam{};unsigned long long observedRules=0;
    bool targetObserved=false,steamObserved=false,bindingQualified=false;
    unsigned long long bindingSerial=0;Identity boundGame{},boundSteam{};std::string boundGameId;std::uint32_t boundAppId=0;
    std::atomic<bool> active{false},ownsRunning{false},enabled{false};
    std::atomic<DWORD> ownedPid{0};std::atomic<ULONGLONG> ownedBirth{0};
    ymcc::steamOriginal::Admission admission,legacyAdmission;
    json snapshot={{"schema","ymcc.steam-original-recovery.v1"},{"phase","not-started"},
        {"owner","ymcc-native"},{"extraExecutables",0},{"periodicPolling",false},
        {"gamePidSource","ymcc-unique-game-valve"},{"controllerVerified",false},{"continueClickVerified",false}};
};
static std::mutex lifecycleMx,shutdownMx;
static std::shared_ptr<Control> control;
static ymcc::steamOriginal::RecoveryWorker worker;
static std::wstring root(){return yemancc_data_dir()+L"\\SteamOriginalRecovery";} // clearable receipts only
static std::wstring runtimeRoot(){return yemancc_data_dir()+L"\\Runtime\\SteamOriginalRecovery";}
static json readJson(const std::wstring& path){
    try{std::ifstream f(fspath::path(path),std::ios::binary);if(!f)return {};
        f.seekg(0,std::ios::end);const auto n=f.tellg();if(n<0||n>2*1024*1024)return {};
        f.seekg(0);return json::parse(f,nullptr,false);}catch(...){return {};}
}
static void saveJson(const std::wstring& path,const json& value){
    std::error_code ec;fspath::create_directories(fspath::path(path).parent_path(),ec);
    if(ec||!sgWriteMonitorSnapshotDurable(path,value.dump()))throw std::runtime_error("original-recovery-evidence-write-failed");
}
static void log(const std::shared_ptr<Control>& c,const char* event,json value){
    try{std::lock_guard<std::mutex> lock(c->logMx);
        const auto path=yemancc_data_dir()+L"\\steam-original-recovery.jsonl";
        rotateOneLogFile(path,1024*1024);
        value["event"]=event;value["time"]=nativeLifecycleTimestamp();value["writerPid"]=GetCurrentProcessId();
        std::ofstream f(fspath::path(path),std::ios::binary|std::ios::app);if(f)f<<value.dump()<<'\n';
    }catch(...){}
}
static void publish(const std::shared_ptr<Control>& c,json value,bool logged=true){
    value["owner"]="ymcc-native";value["extraExecutables"]=0;value["periodicPolling"]=false;
    value["storageLayout"]={{"schema",2},{"evidenceRoot",W2U(root())},{"runtimeRoot",W2U(runtimeRoot())},
        {"loadedImagesInEvidenceRoot",false},{"logClearResetsRecoveryBudget",false}};
    value["gamePidSource"]="ymcc-unique-game-valve";value["atFileTime"]=std::to_string(sgNowFileTime());
    {std::lock_guard<std::mutex> lock(c->snapshotMx);c->snapshot.update(value);value=c->snapshot;}
    saveJson(root()+L"\\status.json",value);
    if(logged)log(c,"state",value);
}
static std::shared_ptr<Control> current(){std::lock_guard<std::mutex> lock(lifecycleMx);return control;}
static Identity identity(DWORD pid,ULONGLONG birth,const std::wstring& path){
    DWORD session=MAXDWORD;if(!pid||!birth||path.empty()||!ProcessIdToSessionId(pid,&session))return {};
    auto canonical=path;std::replace(canonical.begin(),canonical.end(),L'/',L'\\');
    std::transform(canonical.begin(),canonical.end(),canonical.begin(),[](wchar_t ch){return std::towlower(ch);});
    return {pid,session,birth,std::move(canonical)};
}
static json asJson(const Identity& p){return {{"pid",p.pid},{"birth",std::to_string(p.birth)},
    {"birthFileTime",std::to_string(p.birth)},{"path",W2U(p.path)},{"session",p.session}};}
static ymcc::steamOriginal::WakeProof wakeProof(unsigned long long generation,unsigned long long epoch){
    ymcc::steamOriginal::WakeProof proof;
    std::lock_guard<std::mutex> lock(SleepRecoveryAuto::mx);
    const auto& state=SleepRecoveryAuto::state;const auto& observed=SleepRecoveryAuto::kernelWake;
    const auto& admission=state.wakeAdmission;
    if(!admission.is_object())return proof;
    proof.generation=state.generation;proof.epoch=state.configEpoch;
    proof.observedGeneration=observed.generation;proof.observedEpoch=observed.configEpoch;
    proof.requestedGeneration=observed.requestGeneration;proof.requested=observed.uiWakeRequested;
    proof.eventFileTime=observed.eventFileTime;proof.observedBoundaryFileTime=observed.boundaryFileTime;
    proof.eventId=observed.eventId;proof.latestEntryEpoch=SleepRecoveryAuto::kernelSleepEntry.configEpoch;
    proof.latestEntryFileTime=SleepRecoveryAuto::kernelSleepEntry.eventFileTime;
    proof.admitted=admission.value("status",std::string{})=="admitted";
    proof.confirmed=ymcc::sleeprecovery::flag(admission,"kernelWakeConfirmed");
    proof.fresh=ymcc::sleeprecovery::flag(admission,"proofFresh");
    proof.transferred=ymcc::sleeprecovery::flag(admission,"proofFollowedPreparedBoundary");
    proof.boundaryGeneration=ymcc::sleeprecovery::number(admission,"boundaryGeneration");
    proof.admissionObservedGeneration=ymcc::sleeprecovery::number(admission,"proofObservedGeneration");
    const auto decimalField=[&](const char* key,std::uint64_t& value){const auto it=admission.find(key);
        return it!=admission.end()&&it->is_string()&&ymcc::sleepsteam::unsignedDecimal(it->get<std::string>(),value);};
    if(!decimalField("eventFileTime",proof.admissionFileTime)||!decimalField("boundaryFileTime",proof.boundaryFileTime))return {};
    const auto source=admission.value("source",std::string{});
    proof.sourceMatched=(proof.eventId==507&&source=="kernel-power-507-reason-1")||
        (proof.eventId==107&&source=="kernel-power-107-target-5");
    return g_sleepRecoveryAutoConfigEpoch.load()==epoch&&proof.valid(generation,epoch,sgNowFileTime())?
        proof:ymcc::steamOriginal::WakeProof{};
}
// Read from the ordinary power owner, independently of the semi-static
// Kernel witness. No UI/native compatibility reader participates in this gate.
static ymcc::steamOriginal::LegacyWakeProof legacyWakeProof(unsigned long long generation,unsigned long long epoch){
    ymcc::steamOriginal::LegacyWakeProof proof;
    std::lock_guard<std::mutex> lock(SleepRecoveryAuto::mx);
    const auto& state=SleepRecoveryAuto::state;const auto& admission=state.wakeAdmission;
    if(!admission.is_object()||g_powerLifecycle.load()!=PowerLifecycle::Ready||
        g_sleepRecoveryAutoConfigEpoch.load()!=epoch)return proof;
    proof.generation=state.generation;proof.epoch=state.configEpoch;proof.armedTick=state.armed;
    proof.admitted=admission.value("status",std::string{})=="admitted";
    proof.operationAllowed=SleepRecoveryAuto::wakeOperationAllowedLocked(generation,epoch);
    proof.boundaryGeneration=ymcc::sleeprecovery::number(admission,"boundaryGeneration");
    const auto decimalField=[&](const char* key,std::uint64_t& value){const auto it=admission.find(key);
        return it!=admission.end()&&it->is_string()&&ymcc::sleepsteam::unsignedDecimal(it->get<std::string>(),value);};
    if(!decimalField("boundaryFileTime",proof.boundaryFileTime))return {};
    const auto source=admission.value("source",std::string{});
    if(source=="pbt-resume-suspend-s3"&&!ymcc::sleeprecovery::flag(admission,"modernStandby")){
        proof.sourceMatched=decimalField("pbtObservedFileTime",proof.eventFileTime);
    }else if(ymcc::sleeprecovery::flag(admission,"kernelWakeConfirmed")&&ymcc::sleeprecovery::flag(admission,"proofFresh")){
        const auto& observed=SleepRecoveryAuto::kernelWake;
        proof.sourceMatched=decimalField("eventFileTime",proof.eventFileTime)&&observed.configEpoch==epoch&&
            observed.eventFileTime==proof.eventFileTime&&observed.requestGeneration==generation&&observed.uiWakeRequested&&
            ((observed.eventId==507&&source=="kernel-power-507-reason-1")||
             (observed.eventId==107&&source=="kernel-power-107-target-5"));
    }
    if(SleepRecoveryAuto::kernelSleepEntry.configEpoch==epoch)
        proof.latestEntryFileTime=SleepRecoveryAuto::kernelSleepEntry.eventFileTime;
    return proof.valid(generation,epoch,sgNowFileTime(),GetTickCount64())?proof:ymcc::steamOriginal::LegacyWakeProof{};
}
static bool sameScopeNow(const std::shared_ptr<Control>& c,const Scope& scope,unsigned long long generation,bool recovery,
    unsigned long long configEpoch=0,unsigned long long bindingSerial=ULLONG_MAX){
    {std::lock_guard<std::mutex> lock(c->mx);if(c->stop||
        (generation&&c->latestPower>generation)||(bindingSerial!=ULLONG_MAX&&
            (!c->bindingQualified||c->bindingSerial!=bindingSerial)))return false;}
    if(configEpoch&&g_sleepRecoveryAutoConfigEpoch.load()!=configEpoch)return false;
    if(g_exitRequested.load()||g_gameRulesEpoch.load()!=scope.rulesEpoch)return false;
    if(recovery&&(!g_sleepRecoveryAutoEnabled.load()||g_powerLifecycle.load()!=PowerLifecycle::Ready))return false;
    if(recovery&&generation&&!SleepRecoveryAuto::wakeOperationAllows(generation,g_sleepRecoveryAutoConfigEpoch.load()))return false;
    NativeDetectedGame target;unsigned long long touch=0;
    if(!nativeValveReadCurrentTarget(&target,&touch))return false;
    if(!(identity(target.pid,target.processCreated,target.path)==scope.game))return false;
    const auto steam=SleepSteamRecovery::session();
    return identity(steam.pid,steam.created,steam.root+L"\\steam.exe")==scope.steam;
}
// This guard serves read-only wake observation. Full disk rules, registry and
// fresh nativeValveReadCurrentTarget checks remain at every mutation boundary.
static bool sameObservationScopeNow(const std::shared_ptr<Control>& c,const Scope& scope,
    unsigned long long generation,unsigned long long configEpoch,unsigned long long bindingSerial,std::uint32_t expectedAccount){
    {std::lock_guard<std::mutex> lock(c->mx);if(c->stop||!c->bindingQualified||
        c->bindingSerial!=bindingSerial||(generation&&c->latestPower>generation))return false;}
    if(g_exitRequested.load()||g_gameRulesEpoch.load()!=scope.rulesEpoch||
        g_sleepRecoveryAutoConfigEpoch.load()!=configEpoch||!g_sleepRecoveryAutoEnabled.load()||
        g_powerLifecycle.load()!=PowerLifecycle::Ready)return false;
    NativeGameValveSnapshot selected;
    {std::lock_guard<std::mutex> lock(g_gameValveMx);selected=g_gameValveTarget;}
    if(!selected.generation||selected.rulesEpoch!=scope.rulesEpoch||selected.game.pid!=scope.game.pid||
        selected.game.processCreated!=scope.game.birth||_wcsicmp(selected.game.path.c_str(),scope.game.path.c_str())!=0)return false;
    const auto steam=g_steamSettingsObserver.sessionSnapshot();
    return expectedAccount&&steam.account==expectedAccount&&steam.pid==scope.steam.pid&&steam.created==scope.steam.birth&&
        _wcsicmp((steam.root+L"\\steam.exe").c_str(),scope.steam.path.c_str())==0;
}
static void enqueue(const std::shared_ptr<Control>& c,Trigger kind,unsigned long long generation=0){
    if(!c)return;
    {std::lock_guard<std::mutex> lock(c->mx);if(c->stop)return;
        if(kind==Trigger::Wake||kind==Trigger::SuspendCapture){
            if(!generation||generation<c->latestPower)return;c->latestPower=generation;
        }
        for(const auto& item:c->pending)if(item.kind==kind&&item.generation==generation)return;
        if(c->pending.size()>=16)return; // bounded; never spawn a second worker
        c->pending.push_back({kind,generation,++c->sequence});
        if(c->observationSequence!=ULLONG_MAX)++c->observationSequence;
    }c->cv.notify_all();if(c->observationEvent)SetEvent(c->observationEvent.get());
}
// Discovery only: ActiveProcess can retain an exited registry PID after a
// Steam restart. An account candidate never admits publication. Each route
// must verify the real selected CUser/account, original full GameID/PID and
// saved healthy pins; the legacy normal IPC roundtrip validates the account too.
static DWORD accountCandidate(DWORD verifiedAccount){
    if(verifiedAccount)return verifiedAccount;
    DWORD candidate=0;
    if(!sofRegDWORD(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"ActiveUser",&candidate))return 0;
    return candidate;
}
static void runRoute(const std::shared_ptr<Control>& c,Pending pending,const json& config,RecoveryRoute route,bool mayMutate){
    const bool legacy=route==RecoveryRoute::LegacyDynamic;
    auto& routeAdmission=legacy?c->legacyAdmission:c->admission;
    publish(c,{{"recoveryRoute",ymcc::steamOriginal::routeName(route)},{"phase","route-started"},{"reason","independent-route-admission"},{"engineDispatched",false},{"lastResult",nullptr}});
    const auto configEpoch=g_sleepRecoveryAutoConfigEpoch.load();
    const bool enabled=config.is_object()&&config.value("enabled",false)&&g_sleepRecoveryAutoEnabled.load();
    if(!enabled){c->ownsRunning=false;publish(c,{{"phase","disabled"},{"reason","automatic-recovery-disabled"},{"target",nullptr},{"steam",nullptr},{"currentBindingValid",false},{"nativeRunning",nullptr},{"lastResultHistorical",true}});return;}
    NativeDetectedGame game;unsigned long long touch=0;
    if(!nativeValveReadCurrentTarget(&game,&touch)){
        const auto session=SleepSteamRecovery::session();
        c->ownsRunning=false;publish(c,{{"phase","waiting"},{"reason","ymcc-unique-game-unavailable"},
            {"target",nullptr},{"steam",asJson(identity(session.pid,session.created,session.root+L"\\steam.exe"))},
            {"currentBindingValid",false},{"nativeRunning",nullptr},{"lastResultHistorical",true}});return;
    }
    const auto client=SleepSteamRecovery::session();
    std::wstring parentPath;ULONGLONG parentBirth=0;
    if(!focusQueryProcessIdentity(GetCurrentProcessId(),&parentPath,&parentBirth))throw std::runtime_error("ymcc-process-identity-unavailable");
    const Scope scope{identity(GetCurrentProcessId(),parentBirth,parentPath),
        identity(game.pid,game.processCreated,game.path),identity(client.pid,client.created,client.root+L"\\steam.exe"),
        g_gameRulesEpoch.load()};
    if(!scope.valid()){c->ownsRunning=false;publish(c,{{"phase","waiting"},{"reason","unique-game-or-steam-unavailable"},{"currentBindingValid",false},
        {"nativeRunning",nullptr},{"lastResultHistorical",true},{"target",asJson(scope.game)},{"steam",asJson(scope.steam)}});return;}
    // Reject already consumed notifications before any log parsing or CDP read.
    {std::lock_guard<std::mutex> lock(c->mx);
        const auto preview=routeAdmission.preview(scope,{pending.kind,pending.generation,pending.sequence},enabled);
        if(!preview.accepted){if(pending.kind==Trigger::Wake&&c->deferredWake==pending.generation)c->deferredWake=0;return;}}
    publish(c,{{"phase","qualifying"},{"reason","binding-current-original-instance"},
        {"target",asJson(scope.game)},{"steam",asJson(scope.steam)},
        {"currentBindingValid",false},{"nativeRunning",nullptr},{"lastResultHistorical",true}});
    // This log lookup maps only the valve's already admitted PID. It cannot
    // pick a game from the Steam library or enumerate an executable name.
    unsigned long long bindingBefore=0;{std::lock_guard<std::mutex> lock(c->mx);bindingBefore=c->bindingSerial;}
    const auto tracked=SteamGameLink::tracking(client,{game.pid,game.processCreated,game.path});
    const auto expected=tracked.gameId;
    const auto expectedApp=tracked.binding.appid;
    const bool fixedTarget=config.value("targetMode",std::string("generic"))=="fixed";
    if(!tracked.complete||tracked.binding.status!="tracked"||tracked.gameId.empty()||!expectedApp||
        (fixedTarget&&(tracked.gameId!=config.value("targetGameId",std::string{})||
            expectedApp!=config.value("targetAppId",std::uint32_t{})))){
        c->ownsRunning=false;publish(c,{{"phase","unqualified"},{"reason","exact-type2-gameid-not-bound"},
            {"target",asJson(scope.game)},{"trackerGameId",tracked.gameId}});return;
    }
    if(!ymcc::steamappstatewin::gameIdMatches(std::stoull(tracked.gameId),expectedApp)||
        ((std::stoull(tracked.gameId)>>24)&255)!=2){
        c->ownsRunning=false;publish(c,{{"phase","unqualified"},{"reason","non-type2-gameid"}});return;
    }
    if(pending.kind==Trigger::Wake&&g_powerLifecycle.load()!=PowerLifecycle::Ready){
        {std::lock_guard<std::mutex> lock(c->mx);c->deferredWake=pending.generation;}
        publish(c,{{"phase","waiting-power-ready"},{"reason","wake-observed-before-power-commit"}});return;
    }
    const auto proof=pending.kind==Trigger::Wake&&!legacy?wakeProof(pending.generation,configEpoch):ymcc::steamOriginal::WakeProof{};
    const bool powerOwnerWitness=legacy||!proof.eventFileTime;
    const auto legacyProof=pending.kind==Trigger::Wake&&powerOwnerWitness?legacyWakeProof(pending.generation,configEpoch):ymcc::steamOriginal::LegacyWakeProof{};
    const ULONGLONG powerEventFileTime=powerOwnerWitness?legacyProof.eventFileTime:proof.eventFileTime;
    if(pending.kind==Trigger::Wake&&!powerEventFileTime){
        publish(c,{{"phase","waiting-confirmed-wake"},{"reason",legacy?"legacy-admitted-resume-not-current":"kernel-wake-witness-or-config-not-current"}});return;
    }
    unsigned long long bindingSerial=0;
    {std::lock_guard<std::mutex> lock(c->mx);
        if(c->bindingSerial!=bindingBefore||c->bindingSerial==ULLONG_MAX)return;
        bindingSerial=++c->bindingSerial;c->bindingQualified=true;c->boundGame=scope.game;c->boundSteam=scope.steam;
        c->boundGameId=tracked.gameId;c->boundAppId=expectedApp;
    }
    const auto scopeNow=[c,scope,pending,configEpoch,bindingSerial,proof,legacyProof,powerOwnerWitness](bool recovery){
        return sameScopeNow(c,scope,pending.generation,recovery,configEpoch,bindingSerial)&&
            (pending.kind!=Trigger::Wake||(powerOwnerWitness?legacyWakeProof(pending.generation,configEpoch)==legacyProof:
                wakeProof(pending.generation,configEpoch)==proof));
    };
    ymcc::steamrecordidentity::Read privateIdentity;
    auto accountClient=client;
    const auto candidateAccount=accountCandidate(client.account);
    accountClient.account=candidateAccount; // Still untrusted until the route native identity checks.
    const auto readAllowed=[scopeNow]{return scopeNow(false);};
    if(legacy){
        // The old route qualifies its account candidate with its own typed native
        // snapshot, complete GameID, record, primary and saved healthy pins.
        // A rejected CDP/UI compatibility reader cannot suppress this route.
        if(!candidateAccount||!readAllowed()){
            publish(c,{{"phase","waiting"},{"reason","legacy-active-account-unavailable"}});return;
        }
        privateIdentity.account64=76561197960265728ULL+candidateAccount;
    }else{
    auto sessionRead=SleepSteamRecovery::liveRequest(accountClient,
        {{"operation","sleep.session.inspect"},{"runningLinkRead",true},{"appids",json::array({expectedApp})}},readAllowed,&privateIdentity);
    if(!privateIdentity.available&&sessionRead.is_object()&&sessionRead.value("ok",false)&&
        sessionRead.value("mutated",json())==json(false)&&!accountClient.account){
        const auto observed=sessionRead.value("observedAccount",std::uint64_t{});
        if(observed&&observed<=MAXDWORD&&readAllowed()){
            accountClient.account=static_cast<DWORD>(observed);
            sessionRead=SleepSteamRecovery::liveRequest(accountClient,
                {{"operation","sleep.session.inspect"},{"runningLinkRead",true},{"appids",json::array({expectedApp})}},readAllowed,&privateIdentity);
        }
    }
    if(!privateIdentity.available||privateIdentity.gameId64!=std::stoull(tracked.gameId)||privateIdentity.appid!=expectedApp||
        !sessionRead.is_object()||!sessionRead.value("ok",false)||sessionRead.value("mutated",json())!=json(false)||!readAllowed()){
        publish(c,{{"phase","waiting"},{"reason",privateIdentity.reason}});return;
    }
    }
    ymcc::steamOriginal::Decision decision;
    {std::lock_guard<std::mutex> lock(c->mx);decision=routeAdmission.admit(scope,{pending.kind,pending.generation,pending.sequence},enabled);}
    if(!decision.accepted){if(pending.kind==Trigger::Wake){std::lock_guard<std::mutex> lock(c->mx);if(c->deferredWake==pending.generation)c->deferredWake=0;}return;}
    if(pending.kind==Trigger::Wake){std::lock_guard<std::mutex> lock(c->mx);if(c->deferredWake==pending.generation)c->deferredWake=0;}
    c->startupPending=false;c->ownedPid=scope.game.pid;c->ownedBirth=scope.game.birth;c->ownsRunning=true;
    const auto stillCurrent=[scopeNow,mayRecover=decision.recoveryAllowed&&mayMutate]{return scopeNow(mayRecover);};
    const auto eventId=std::to_string(scope.parent.pid)+"-"+std::to_string(scope.parent.birth)+"-"+std::to_string(pending.sequence);
    json request={{"schema","ymcc.steam-original-recovery-event.v1"},{"parent",asJson(scope.parent)},
        {"target",asJson(scope.game)},{"steam",asJson(scope.steam)},
        {"trigger",{{"kind",ymcc::steamOriginal::name(pending.kind)},{"generation",std::to_string(pending.generation)},
            {"id",eventId},{"eventId",eventId},{"rulesEpoch",std::to_string(scope.rulesEpoch)},{"timestamp",nativeLifecycleTimestamp()}}},
        {"rulesEpoch",std::to_string(scope.rulesEpoch)},{"configEpoch",std::to_string(configEpoch)},
        {"bindingSerial",std::to_string(bindingSerial)},{"recoveryAllowed",decision.recoveryAllowed&&mayMutate},
        {"recoveryRoute",ymcc::steamOriginal::routeName(route)},
        {"accountSelection",client.account?"verified-session":"candidate-requires-original-native-reference"},
        {"targetStatePath",W2U(MONITOR_GAME_TARGET_JSON)},{"dataRoot",W2U(runtimeRoot())},{"evidenceRoot",W2U(root())},{"logRoot",W2U(yemancc_data_dir())},
        {"config",config},{"extraExecutables",0}};
    request["target"]["gameId"]=tracked.gameId;request["target"]["appid"]=expectedApp;
    request["game"]=request["target"];
    if(powerEventFileTime){
        request["trigger"]["powerEventFileTime"]=std::to_string(powerEventFileTime);
        request["trigger"]["wakeWitness"]={{"source",powerOwnerWitness?"admitted-power-owner":"kernel-witness"},{"configEpoch",std::to_string(configEpoch)},
            {"observedGeneration",std::to_string(powerOwnerWitness?legacyProof.generation:proof.observedGeneration)},
            {"boundaryFileTime",std::to_string(powerOwnerWitness?legacyProof.boundaryFileTime:proof.boundaryFileTime)},{"eventId",powerOwnerWitness?0:proof.eventId}};
    }
    if(!scopeNow(false)){publish(c,{{"phase","cancelled"},{"reason","scope-changed-before-observation"}});return;}
    saveJson(root()+L"\\last-event.json",request);
    saveJson(root()+L"\\last-"+U2W(ymcc::steamOriginal::routeName(route))+L"-event.json",request);
    publish(c,{{"phase","checking"},{"engineDispatched",true},{"reason",decision.reason},{"eventId",eventId},
        {"trigger",request["trigger"]},{"target",request["target"]},{"steam",request["steam"]},
        {"targetMode",fixedTarget?"fixed":"generic"},{"currentBindingValid",true},{"nativeRunning",nullptr},{"lastResultHistorical",true}});
    request["account64"]=std::to_string(privateIdentity.account64); // private; added AFTER durable public event
    const auto emit=[c,eventId](const json& row){
        auto tagged=row;tagged["engineEvent"]=row.value("event",std::string("unknown"));tagged["eventId"]=eventId;log(c,"engine",std::move(tagged));
    };
    const auto observationGuard=[c,scope,pending,configEpoch,bindingSerial,proof,legacyProof,powerOwnerWitness,scopeNow,expectedAccount=static_cast<std::uint32_t>(privateIdentity.account64)]{
        // The cached observer can lag behind the already-qualified UI account
        // or process after wake/FSE. A cache miss cannot cancel this read-only
        // watch while the full original-instance scope is still current.
        // Mutation still requires stillCurrent plus a fresh native snapshot,
        // original reference/pins and the unchanged durable consume budget.
        if(pending.kind==Trigger::Wake&&(powerOwnerWitness?legacyWakeProof(pending.generation,configEpoch)!=legacyProof:
            wakeProof(pending.generation,configEpoch)!=proof))return false;
        return sameObservationScopeNow(c,scope,pending.generation,configEpoch,bindingSerial,expectedAccount)||
            scopeNow(false);
    };
    auto receipt=steamOriginalRecoveryEngineHandle(request,stillCurrent,emit,observationGuard,
        reinterpret_cast<std::uintptr_t>(c->observationEvent.get()));
    const auto receiptStatus=receipt.value("status",std::string{});
    const bool scopeCurrent=scopeNow(false);
    const bool observationCurrent=scopeCurrent&&(receiptStatus=="healthy"||receiptStatus=="recovered-native");
    if(!observationCurrent)c->ownsRunning=false;
    saveJson(root()+L"\\last-recovery-summary.json",receipt);
    saveJson(root()+L"\\last-"+U2W(ymcc::steamOriginal::routeName(route))+L"-summary.json",receipt);
    publish(c,{{"phase",receipt.value("phase",receipt.value("status",std::string("unqualified")))},
        {"reason",receipt.value("reason",std::string{})},{"eventId",eventId},{"lastResult",receipt},
        {"currentBindingValid",scopeCurrent},
        {"nativeRunning",observationCurrent?receipt.value("after",receipt.value("before",json::object())).value("running",json()):json()},
        {"lastResultHistorical",!observationCurrent}});
}
// Each route has its own admission, qualification and source acquisition.
// Both use the engine's existing original-instance publisher and durable ledger.
// A route rejection before publication never rejects the other route.
static void runOne(const std::shared_ptr<Control>& c,Pending pending,const json& config){
    const auto startingConfigEpoch=g_sleepRecoveryAutoConfigEpoch.load();
    const auto startingRulesEpoch=g_gameRulesEpoch.load();
    const auto startingAccount=accountCandidate(SleepSteamRecovery::session().account);
    if(c->startupPending&&pending.kind!=Trigger::SuspendCapture&&pending.kind!=Trigger::Wake)pending.kind=Trigger::Startup;
    json routes=json::array(),best;bool terminal=false,bestOwns=false;
    for(const auto route:{RecoveryRoute::LegacyDynamic,RecoveryRoute::SemiStatic}){
        try{runRoute(c,pending,config,route,!terminal);}
        catch(const std::exception& error){publish(c,{{"phase","blocked"},{"reason",std::string(error.what()).substr(0,256)}});}
        json observed;{std::lock_guard<std::mutex> lock(c->snapshotMx);observed=c->snapshot;}
        json item={{"route",ymcc::steamOriginal::routeName(route)},{"phase",observed.value("phase",std::string{})},
            {"reason",observed.value("reason",std::string{})},{"engineDispatched",observed.value("engineDispatched",false)},
            {"result",observed.value("lastResult",json())}};
        routes.push_back(item);
        const auto result=item["result"];const bool finished=result.is_object()&&
            (result.value("status",std::string{})=="healthy"||result.value("status",std::string{})=="recovered-native");
        const auto bestResult=best.is_object()?best.value("lastResult",json()):json();
        const auto previousStatus=bestResult.is_object()?bestResult.value("status",std::string{}):std::string{};
        const bool previousFinished=previousStatus=="healthy"||previousStatus=="recovered-native";
        const bool newlyRecovered=result.is_object()&&result.value("status",std::string{})=="recovered-native";
        if((finished&&(!previousFinished||(newlyRecovered&&previousStatus!="recovered-native")))||best.is_null()){
            best=observed;bestOwns=c->ownsRunning.load();
        }
        // No automatic second publisher after any dispatched mutation, including
        // ambiguous/failed readback. No route can erase or refund the shared keys.
        terminal=terminal||finished||(result.is_object()&&result.value("consumed",false));
    }
    const json dual={{"schema","ymcc.steam-original-dual-route.v1"},{"routes",routes},
        {"legacyDynamicPreserved",true},{"independentQualification",true},{"sharedPublicationBudget",true},
        {"atFileTime",std::to_string(sgNowFileTime())}};
    saveJson(root()+L"\\last-dual-route-summary.json",dual);log(c,"dual-route-finished",dual);
    if(best.is_object()){
        best["dualRoutes"]=routes;
        // The second route may observe OFF, an account/epoch switch or a new
        // target. A saved successful receipt cannot resurrect old ownership.
        NativeDetectedGame finalGame;unsigned long long finalTouch=0;
        const auto finalSteam=SleepSteamRecovery::session();
        const auto chosenGame=best.value("target",json()),chosenSteam=best.value("steam",json());
        const auto matchesIdentity=[](const json& actual,const json& expected){
            if(!actual.value("pid",DWORD{})||!expected.is_object())return false;
            for(const auto& item:actual.items())if(!expected.contains(item.key())||expected.at(item.key())!=item.value())return false;
            return true;
        };
        bool finalBinding=false;
        {std::lock_guard<std::mutex> lock(c->mx);finalBinding=!c->stop&&c->bindingQualified&&chosenGame.is_object()&&
            c->boundGameId==chosenGame.value("gameId",std::string{})&&c->boundAppId==chosenGame.value("appid",std::uint32_t{});}
        const bool finalCurrent=bestOwns&&finalBinding&&!g_exitRequested.load()&&g_sleepRecoveryAutoEnabled.load()&&
            g_gameRulesEpoch.load()==startingRulesEpoch&&startingAccount&&accountCandidate(finalSteam.account)==startingAccount&&
            g_sleepRecoveryAutoConfigEpoch.load()==startingConfigEpoch&&g_powerLifecycle.load()==PowerLifecycle::Ready&&
            chosenGame.is_object()&&chosenSteam.is_object()&&nativeValveReadCurrentTarget(&finalGame,&finalTouch)&&
            matchesIdentity(asJson(identity(finalGame.pid,finalGame.processCreated,finalGame.path)),chosenGame)&&
            matchesIdentity(asJson(identity(finalSteam.pid,finalSteam.created,finalSteam.root+L"\\steam.exe")),chosenSteam);
        c->ownsRunning=finalCurrent;
        if(bestOwns&&!finalCurrent){best["currentBindingValid"]=false;best["nativeRunning"]=nullptr;
            best["lastResultHistorical"]=true;best["phase"]="cancelled";best["reason"]="scope-changed-after-dual-route";}
        if(best.contains("lastResult")&&best["lastResult"].is_object())
            saveJson(root()+L"\\last-recovery-summary.json",best["lastResult"]);
        publish(c,best);
    }
}
static void run(const std::shared_ptr<Control>& c){
    try{
        const auto storage=ymcc::steamOriginalStorage::prepare(fspath::path(root()),fspath::path(runtimeRoot()));
        log(c,"storage-ready",{{"layout",2},{"legacyImported",storage.legacyImported},
            {"filesCopied",storage.filesCopied},{"filesAlreadyPresent",storage.filesAlreadyPresent},
            {"evidenceRoot",W2U(root())},{"runtimeRoot",W2U(runtimeRoot())}});
    }catch(const std::exception& error){
        c->enabled=false;c->ownsRunning=false;
        try{publish(c,{{"phase","blocked"},{"reason",std::string(error.what()).substr(0,160)},
            {"currentBindingValid",false},{"nativeRunning",nullptr},{"lastResultHistorical",true}});}catch(...){}
        return; // No recovery or ledger writes after an incomplete/ambiguous migration.
    }
    const auto configPath=runtimeRoot()+L"\\enabled.json";
    auto localConfig=readJson(configPath);
    std::error_code configError;const bool configExists=fspath::exists(configPath,configError);
    const auto config=ymcc::steamOriginal::configuration(std::move(localConfig),configExists,!!configError);
    c->enabled=config["enabled"].get<bool>();
    try{publish(c,{{"phase","waiting-events"},{"reason","ymcc-event-owner-started"},
        {"configurationSource",config.value("source",std::string("local-override"))},
        {"targetMode",config.value("targetMode",std::string("generic"))}});}catch(...){}
    for(;;){Pending next;
        {std::unique_lock<std::mutex> lock(c->mx);c->cv.wait(lock,[&]{return c->stop||!c->pending.empty();});
            if(c->stop)break;next=c->pending.front();c->pending.pop_front();}
        c->active=true;
        try{runOne(c,next,config);}catch(const std::exception& e){try{publish(c,{{"phase","blocked"},{"reason",std::string(e.what()).substr(0,256)}});}catch(...){} }
        catch(...){try{publish(c,{{"phase","blocked"},{"reason","native-engine-exception"}});}catch(...){} }
        c->active=false;c->cv.notify_all();
    }
    c->ownsRunning=false;try{publish(c,{{"phase","stopped"},{"reason","ymcc-owner-stopped"}});}catch(...){}
}
static json get(){const auto c=current();if(!c)return {{"phase","not-started"},{"owner","ymcc-native"}};
    std::lock_guard<std::mutex> lock(c->snapshotMx);return c->snapshot;}
}
static void steamOriginalRecoveryStart(){
    std::shared_ptr<SteamOriginalRecovery::Control> c;
    {std::lock_guard<std::mutex> lock(SteamOriginalRecovery::lifecycleMx);
        if(SteamOriginalRecovery::control||g_exitRequested.load(std::memory_order_acquire))return;
        c=std::make_shared<SteamOriginalRecovery::Control>();SteamOriginalRecovery::control=c;
        c->observationEvent=std::shared_ptr<void>(CreateEventW(nullptr,FALSE,FALSE,nullptr),[](void* event){if(event)CloseHandle(event);});
        SteamOriginalRecovery::worker.start([c]{
            try{SteamOriginalRecovery::run(c);}
            catch(...){c->active=false;c->ownsRunning=false;
                try{SteamOriginalRecovery::publish(c,{{"phase","stopped"},{"reason","worker-exception"}});}catch(...){} }
        });}
    SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::Startup);
}
static void steamOriginalRecoveryTargetChanged(DWORD pid,ULONGLONG birth,const std::wstring& path,unsigned long long rules){
    const auto c=SteamOriginalRecovery::current();if(!c)return;
    {std::lock_guard<std::mutex> lock(c->mx);
        if(c->targetObserved&&c->observedTarget.pid==pid&&c->observedTarget.birth==birth&&
            _wcsicmp(c->observedTarget.path.c_str(),path.c_str())==0){c->observedRules=rules;return;}}
    const auto next=SteamOriginalRecovery::identity(pid,birth,path);
    {std::lock_guard<std::mutex> lock(c->mx);if(c->stop)return;
        if(c->targetObserved&&c->observedTarget==next){c->observedRules=rules;return;}
        c->targetObserved=true;c->observedTarget=next;c->observedRules=rules;
        c->ownsRunning=false;}
    SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::TargetChanged);
}
static void steamOriginalRecoverySteamChanged(){
    const auto c=SteamOriginalRecovery::current();if(!c)return;
    const auto session=g_steamSettingsObserver.sessionSnapshot();
    const auto next=SteamOriginalRecovery::identity(session.pid,session.created,session.root+L"\\steam.exe");
    {std::lock_guard<std::mutex> lock(c->mx);if(c->steamObserved&&c->observedSteam==next)return;
        c->steamObserved=true;c->observedSteam=next;c->ownsRunning=false;}
    SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::SteamChanged);
}
static void steamOriginalRecoverySuspend(unsigned long long generation){SteamOriginalRecovery::enqueue(SteamOriginalRecovery::current(),ymcc::steamOriginal::Trigger::SuspendCapture,generation);}
static void steamOriginalRecoveryWake(unsigned long long generation){
    const auto c=SteamOriginalRecovery::current();if(!c||!generation)return;
    {std::lock_guard<std::mutex> lock(c->mx);if(c->stop||generation<c->latestPower)return;
        c->latestPower=generation;c->deferredWake=generation;}
    if(g_powerLifecycle.load()==PowerLifecycle::Ready)SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::Wake,generation);
}
static void steamOriginalRecoveryPowerReady(unsigned long long generation){
    const auto c=SteamOriginalRecovery::current();if(!c||!generation||g_powerLifecycle.load()!=PowerLifecycle::Ready)return;
    unsigned long long pending=0;
    {std::lock_guard<std::mutex> lock(c->mx);if(!c->stop&&c->deferredWake==generation&&c->latestPower==generation)pending=generation;}
    if(pending)SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::Wake,pending);
}
static void steamOriginalRecoveryTrackerObserved(DWORD gamePid,ULONGLONG gameBirth,DWORD steamPid,ULONGLONG steamBirth,
    const std::string& gameId,std::uint32_t appid,bool exact){
    const auto c=SteamOriginalRecovery::current();if(!c)return;
    std::lock_guard<std::mutex> lock(c->mx);
    const bool same=c->boundGame.pid==gamePid&&c->boundGame.birth==gameBirth&&
        c->boundSteam.pid==steamPid&&c->boundSteam.birth==steamBirth;
    if(same&&c->bindingQualified&&exact&&c->boundGameId==gameId&&c->boundAppId==appid)return;
    if(!same&&!c->active.load())return;
    // This event can revoke only. It cannot elect a PID, replace GameID, queue
    // a mutation, or refund an already consumed observation/dispatch.
    c->bindingQualified=false;c->ownsRunning=false;
    if(c->bindingSerial!=ULLONG_MAX)++c->bindingSerial;
}
// Existing tracker/lifetime events only wake a currently active original job.
// There is no new queue entry, target election, mutation ticket or budget refund.
static void steamOriginalRecoveryObserveHint(){
    const auto c=SteamOriginalRecovery::current();if(!c||!c->active.load())return;
    {std::lock_guard<std::mutex> lock(c->mx);if(c->stop)return;
        if(c->observationSequence!=ULLONG_MAX)++c->observationSequence;}
    if(c->observationEvent)SetEvent(c->observationEvent.get());
}
static void steamOriginalRecoveryBindingReady(){
    const auto c=SteamOriginalRecovery::current();if(!c)return;
    steamOriginalRecoveryPowerReady(currentPowerGeneration());
    // Existing Steam window/session/binding changes can complete a first
    // healthy observation, but cannot grant another consumed recovery action.
    if(c->enabled.load()&&!c->ownsRunning.load())SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::TargetChanged);
}
// Advisory notification from an already completed ordinary read. It never
// elects an identity, publishes Running, admits a mutation or refunds a ticket.
static void steamOriginalRecoveryIdentityReady(DWORD gamePid,ULONGLONG gameBirth,DWORD steamPid,ULONGLONG steamBirth,
    const std::string& gameId,std::uint32_t appid,ULONGLONG configEpoch,ULONGLONG rulesEpoch){
    const auto c=SteamOriginalRecovery::current();if(!c||!c->enabled.load()||c->ownsRunning.load())return;
    if(g_exitRequested.load()||!g_sleepRecoveryAutoEnabled.load()||g_powerLifecycle.load()!=PowerLifecycle::Ready||
        !configEpoch||g_sleepRecoveryAutoConfigEpoch.load()!=configEpoch||
        !rulesEpoch||g_gameRulesEpoch.load()!=rulesEpoch)return;
    {std::lock_guard<std::mutex> lock(c->mx);
        if(c->stop||c->boundGame.pid!=gamePid||c->boundGame.birth!=gameBirth||
            c->boundSteam.pid!=steamPid||c->boundSteam.birth!=steamBirth||
            c->boundGameId!=gameId||c->boundAppId!=appid||gameId.empty()||!appid||
            (c->targetObserved&&!(c->observedTarget==c->boundGame))||
            (c->steamObserved&&!(c->observedSteam==c->boundSteam))||
            (c->targetObserved&&c->observedRules!=rulesEpoch))return;
    }
    // The existing queue coalesces identical TargetChanged notifications. Its
    // consumer re-reads the unique valve, Steam session and private identity.
    SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::TargetChanged);
}
static bool steamOriginalRecoveryOwnsRunning(DWORD pid,ULONGLONG birth){
    const auto c=SteamOriginalRecovery::current();return c&&c->ownsRunning.load()&&c->ownedPid.load()==pid&&c->ownedBirth.load()==birth;
}
// Pure cancellation: safe in the UI exit callback before device cleanup starts.
// Waking an active observer also revokes its original-cohort mutation guard.
static void steamOriginalRecoveryRequestStop(){
    const auto c=SteamOriginalRecovery::current();if(!c)return;
    {std::lock_guard<std::mutex> lock(c->mx);c->stop=true;c->pending.clear();}c->cv.notify_all();
    if(c->observationEvent)SetEvent(c->observationEvent.get());
    c->ownsRunning=false;
}
static bool steamOriginalRecoveryStopped(){return SteamOriginalRecovery::worker.finished();}
static void steamOriginalRecoveryShutdown(){
    steamOriginalRecoveryRequestStop();
    std::lock_guard<std::mutex> shutdownLock(SteamOriginalRecovery::shutdownMx);
    // No TerminateThread/process, kill-on-close job, or timeout-based detach.
    // A live debugger owner must finish cleanup (or observe Steam's natural
    // exit) before YMCC's ordinary exit can release its dependencies.
    SteamOriginalRecovery::worker.join();
}
static std::wstring steamOriginalRecoveryExportSnapshot(){
    const auto file=SteamOriginalRecovery::root()+L"\\current-export.json";
    try{SteamOriginalRecovery::saveJson(file,SteamOriginalRecovery::get());return file;}catch(...){return {};}
}
