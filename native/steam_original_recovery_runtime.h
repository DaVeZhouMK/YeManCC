#pragma once
#include "steam_original_recovery_handoff.h"
#include "steam_original_recovery_policy.h"
#include "steam_original_recovery_trace.h"
#include "steam_original_recovery_dual_route.h"
#include "steam_original_recovery_config.h"
#include "steam_original_recovery_wake_watch.h"
#include "steam_original_recovery_storage.h"
#include "steam_original_recovery_worker.h"
#include "worker_thread_name.h"
#include "steam_native_read_scope.h"
#include "steam_game_request_cancellation.h"
#include "steam_game_tracker_completion.h"
#include "steam_command_availability.h"
#include <array>
#include <optional>
#include <condition_variable>
#include <chrono>
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
    std::uintptr_t wakeSignal,std::uintptr_t cancelSignal,std::uintptr_t identityCancelSignal,
    std::shared_ptr<ymcc::steamnative::ReadScope> publicationScope,
    std::shared_ptr<ymcc::steamgamefacts::ProcessGroupLease> processGroup,
    std::shared_ptr<ymcc::steamgamefacts::RequestCancellation> businessRequestCancellation,
    std::shared_ptr<ymcc::steamcommand::Intent> mutationIntent,
    std::shared_ptr<ymcc::steamcommand::AvailabilityEvent> mutationAvailability,
    std::shared_ptr<const ymcc::steamgamefacts::StartupHandoffEvidence> startupHandoff,
    const std::function<bool()>& startupHandoffCurrent,
    std::shared_ptr<const ymcc::steamgamefacts::HistoricalStartupHandoffEvidence> historicalStartupHandoff,
    const std::function<bool()>& historicalStartupHandoffCurrent);
static std::shared_ptr<ymcc::steamnative::ReadScope> nativeSteamNativeReadScope(const SleepSteamRecovery::Session&,ymcc::steamnative::ReadPurpose);
static SteamGameLink::TrackerRead nativeSteamNativeObservationTracking(const SleepSteamRecovery::Session&,const SleepSteamRecovery::Game&,const std::function<bool()>&);
namespace SteamOriginalRecovery {
using Trigger=ymcc::steamOriginal::Trigger;
using RecoveryRoute=ymcc::steamOriginal::RecoveryRoute;
using Identity=ymcc::steamOriginal::Identity;
using Scope=ymcc::steamOriginal::Scope;
struct StartupFreshRequest;
struct StartupFreshWait;
struct StartupCommandWait;
struct Control;
struct Pending {
    Trigger kind;unsigned long long generation=0,sequence=0;
    // Only a drained exact-flight hint can populate this request-local suffix.
    // Original enqueues keep the same three-field aggregate and a null suffix.
    std::shared_ptr<StartupFreshWait> startupFresh;
    // Ordinary pending hints get one fixed window. Exact-flight suffixes never wait.
    unsigned long long notBeforeTick=0;
    std::shared_ptr<StartupCommandWait> startupCommand; // Never a tracker completion witness.
};
struct StartupFreshRequest final {
    const unsigned long long sequence,configEpoch,rulesEpoch,powerGeneration;
    const DWORD accountCandidate;
    const NativeGameSelectionReceipt selection;
    const std::shared_ptr<ymcc::steamnative::ReadScope> operationReadScope;
    std::atomic<bool> cancelled{false},terminal{false};
    // Only the existing recovery worker reads/writes these route snapshots.
    // They are diagnostic observations, never facts or admission authority.
    std::array<json,2> routeObservations;
    std::array<bool,2> routeOwns{};
    std::array<bool,2> routeArmed{}; // Protected by original Control::mx.
    StartupFreshRequest(unsigned long long serial,unsigned long long config,unsigned long long rules,
        DWORD account,NativeGameSelectionReceipt selected,std::shared_ptr<ymcc::steamnative::ReadScope> scope)
        :sequence(serial),configEpoch(config),rulesEpoch(rules),powerGeneration(selected.powerGeneration),
         accountCandidate(account),selection(std::move(selected)),operationReadScope(std::move(scope)){}
};
enum class StartupFreshPhase { Arming, Watching, Queued, Running, Finished, Cancelled };
struct StartupFreshWait final {
    const std::shared_ptr<StartupFreshRequest> request;
    const Pending original;
    const RecoveryRoute route;
    const Scope scope;
    const bool mayMutate;
    const std::shared_ptr<ymcc::steamnative::ReadScope> nativeReadScope;
    const ymcc::steamgamefacts::TrackerCompletionWitness witness;
    // Access below is serialized by original Control::mx, never by the sink.
    StartupFreshPhase phase=StartupFreshPhase::Arming;
    ymcc::steamgamefacts::TrackerCompletionSubscription subscription;
    std::optional<ymcc::steamgamefacts::TrackerCompletionHint> earlyHint;
    bool reReadNow=false;
    std::uint64_t consumedReceiptRevision=0;
    StartupFreshWait(std::shared_ptr<StartupFreshRequest> owner,Pending pending,RecoveryRoute selectedRoute,
        Scope originalScope,bool mutate,std::shared_ptr<ymcc::steamnative::ReadScope> native,
        ymcc::steamgamefacts::TrackerCompletionWitness blocked)
        :request(std::move(owner)),original(std::move(pending)),route(selectedRoute),scope(std::move(originalScope)),
         mayMutate(mutate),nativeReadScope(std::move(native)),witness(std::move(blocked)){}
};
// This signal publishes metadata only, AFTER the core State lock is gone.
// The original Control mutex closes predicate-check -> wait registration;
// atomic store + notify alone cannot close that lost-notification window.
struct StartupCommandSignal final:ymcc::steamcommand::AvailabilitySignal {
    const std::weak_ptr<Control> owner;
    std::atomic<bool> notified{false};
    explicit StartupCommandSignal(const std::shared_ptr<Control>& value):owner(value){}
    void notify()noexcept override;
};
enum class StartupCommandPhase { Arming, Watching, Queued, Running, Finished, Cancelled };
struct StartupCommandWait final {
    const std::shared_ptr<StartupFreshRequest> request;
    const Pending original;
    const RecoveryRoute route;
    const Scope scope;
    const std::string gameId;
    const std::uint32_t appid;
    const unsigned long long account64; // Retained value, never upgraded provenance.
    const bool mayMutate;
    const std::shared_ptr<StartupCommandSignal> signal;
    const std::shared_ptr<ymcc::steamcommand::Intent> intent;
    StartupCommandPhase phase=StartupCommandPhase::Arming; // Control::mx only.
    std::atomic<bool> cancelled{false};
    StartupCommandWait(std::shared_ptr<StartupFreshRequest> r,Pending pending,RecoveryRoute selectedRoute,
        Scope s,std::string id,std::uint32_t app,unsigned long long account,bool mutate,
        std::shared_ptr<StartupCommandSignal> wake,std::shared_ptr<ymcc::steamcommand::Intent> ticket)
        :request(std::move(r)),original(std::move(pending)),route(selectedRoute),scope(std::move(s)),
         gameId(std::move(id)),appid(app),account64(account),mayMutate(mutate),
         signal(std::move(wake)),intent(std::move(ticket)){}
};
// Every writer declares this BEFORE acquiring its original consumer locks.
// Only an empty retirement receives a moved old slot; cancel and final capture
// release then occur after those locks, never on a coordinator callback stack.
struct CommandRetirement final {
    std::shared_ptr<StartupCommandWait> value;
    CommandRetirement()=default;
    CommandRetirement(CommandRetirement&&)noexcept=default;
    CommandRetirement& operator=(CommandRetirement&&)noexcept=default;
    CommandRetirement(const CommandRetirement&)=delete;
    ~CommandRetirement(){if(value&&value->intent)value->intent->cancel();}
};
// Business scope only: neither this slot nor cancellation grants game/account/power authority.
enum class GroupRequestTarget { GlobalSelection, SleepOriginal };
struct ActiveGroupRequest {
    std::shared_ptr<ymcc::steamgamefacts::RequestCancellation> cancellation;
    unsigned long long sequence=0,bindingSerial=0,configEpoch=0,rulesEpoch=0;
    unsigned long long selectionGeneration=0,powerGeneration=0;
    RecoveryRoute route=RecoveryRoute::LegacyDynamic;
    GroupRequestTarget targetDomain=GroupRequestTarget::GlobalSelection;
    Identity game{},steam{};
};
// One advisory slot; no native state or action permission is stored here.
struct RunningLossNotice {
    unsigned long long sequence=0,configEpoch=0,rulesEpoch=0,powerGeneration=0;
    Identity game{},steam{};std::string gameId;std::uint32_t appid=0;
    NativeGameSelectionReceipt selection;
};
struct Control {
    std::mutex mx,snapshotMx,logMx;std::condition_variable cv;
    bool stop=false;std::atomic<bool> startupPending{true};std::deque<Pending> pending;
    std::shared_ptr<void> observationEvent;
    std::shared_ptr<ymcc::steamcommand::AvailabilityEvent> mutationAvailability;
    std::shared_ptr<StartupCommandWait> startupCommandWait; // At most one, before admit.
    std::shared_ptr<ActiveGroupRequest> activeGroupRequest;
    std::shared_ptr<StartupFreshRequest> startupFreshRequest;
    std::array<std::shared_ptr<StartupFreshWait>,2> startupFreshWaits;
    unsigned long long sequence=0,observationSequence=0;std::uint64_t latestPower=0,deferredWake=0;
    Identity observedTarget{},observedSteam{};unsigned long long observedRules=0;
    bool targetObserved=false,steamObserved=false,bindingQualified=false;
    std::shared_ptr<SleepSteamRecovery::CaptureRequest> boundSleepRequest;
    unsigned long long bindingSerial=0;Identity boundGame{},boundSteam{};std::string boundGameId;std::uint32_t boundAppId=0;
    std::atomic<bool> active{false},ownsRunning{false},enabled{false};
    std::atomic<DWORD> ownedPid{0};std::atomic<ULONGLONG> ownedBirth{0};
    ymcc::steamOriginal::Admission admission,legacyAdmission;
    std::optional<RunningLossNotice> runningLossNotice; // Protected by mx; exact sequence only.
    json snapshot={{"schema","ymcc.steam-original-recovery.v1"},{"phase","not-started"},
        {"owner","ymcc-native"},{"extraExecutables",0},{"periodicPolling",false},
        {"gamePidSource","ymcc-unique-game-valve"},{"controllerVerified",false},{"continueClickVerified",false}};
};
inline void StartupCommandSignal::notify()noexcept {
    const auto held=owner.lock();if(!held)return;
    {std::lock_guard<std::mutex> lock(held->mx);
        notified.store(true,std::memory_order_release);}
    held->cv.notify_all(); // No Current/IO/join; final owner drops outside mx.
}
static void retireStartupCommandLocked(const std::shared_ptr<Control>& c,CommandRetirement& retired)noexcept {
    if(!c->startupCommandWait)return;
    c->startupCommandWait->cancelled.store(true,std::memory_order_release);
    c->startupCommandWait->phase=StartupCommandPhase::Cancelled;
    retired.value=std::move(c->startupCommandWait); // Caller guarantees retired is empty.
}
struct StartupCommandRunReturn final {
    const std::shared_ptr<Control> owner;
    std::shared_ptr<StartupCommandWait> wait;
    ~StartupCommandRunReturn(){
        if(!wait)return;
        CommandRetirement retired;
        {std::lock_guard<std::mutex> lock(owner->mx);
            if(owner->startupCommandWait==wait)retireStartupCommandLocked(owner,retired);}
    }
};
// Caller holds c->mx. Signal only; no Current callback, IO, join or ownership release.
static void cancelGroupRequestLocked(const std::shared_ptr<Control>& c,bool globalSelectionOnly=false) noexcept {
    if(c->mutationAvailability)c->mutationAvailability->notify(); // Dedicated hint only; no WakeWatch cadence change.
    const auto& request=c->activeGroupRequest;
    if(request&&request->cancellation&&(!globalSelectionOnly||request->targetDomain==GroupRequestTarget::GlobalSelection))
        request->cancellation->cancel();
}
struct GroupRequestRegistration {
    std::shared_ptr<Control> owner;
    std::shared_ptr<ActiveGroupRequest> request;
    GroupRequestRegistration(std::shared_ptr<Control> c,std::shared_ptr<ActiveGroupRequest> r):owner(std::move(c)),request(std::move(r)){}
    GroupRequestRegistration(const GroupRequestRegistration&)=delete;
    GroupRequestRegistration& operator=(const GroupRequestRegistration&)=delete;
    ~GroupRequestRegistration(){
        if(!request)return;
        std::lock_guard<std::mutex> lock(owner->mx);
        // The engine has returned; retained engine/remote owners keep their shared_ptr.
        request->cancellation->cancel();
        if(owner->activeGroupRequest==request)owner->activeGroupRequest.reset();
    }
};
static std::mutex lifecycleMx,shutdownMx;
static std::shared_ptr<Control> control;
static ymcc::steamOriginal::RecoveryWorker worker;
static std::wstring root(){return yemancc_data_dir()+L"\\SteamOriginalRecovery";} // clearable receipts only
static std::wstring runtimeRoot(){return yemancc_data_dir()+L"\\Runtime\\SteamOriginalRecovery";}
struct ConfigRead {json value=nullptr;bool exists=false,lookupError=false;};
static ConfigRead readJson(const std::wstring& path){
    ConfigRead result;
    try{
        const fspath::path file(path);
        ymcc::steamOriginalStorage::detail::ancestry(file);
        result.exists=ymcc::steamOriginalStorage::detail::present(file);
        if(result.exists)result.value=ymcc::steamOriginalStorage::readState(file);
    }catch(...){result.lookupError=true;result.value=nullptr;}
    return result;
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
static json routeAdmissionLogProjection(const json& snapshot){
    json result={{"routeAdmissionLogSchema",1}};
    for(const char* key:{"schema","phase","reason","recoveryRoute","attemptSequence",
            "engineDispatched","eventId","trigger","lastResult","lastResultHistorical","lastResultState",
            "trackerRead","trackerGameId","terminalEvidence","terminalEvidenceHistorical",
            "owner","extraExecutables","periodicPolling","gamePidSource","configurationSource","targetMode","atFileTime"}){
        const auto item=snapshot.find(key);
        if(item!=snapshot.end())result[key]=*item;
    }
    return result;
}
enum class RouteStateLog { admission, noUniqueGame };
static json noUniqueGameRouteLogProjection(const json& snapshot){
    json receipt=json::object();
    // The previous aggregate belongs to its own dual-route-finished receipt.
    for(auto it=snapshot.begin();it!=snapshot.end();++it)
        if(it.key()!="dualRoutes")receipt[it.key()]=it.value();
    receipt["routeNoUniqueGameLogSchema"]=1;
    return receipt;
}
static void publish(const std::shared_ptr<Control>& c,json value,const Pending* routeEvent=nullptr,
                    RouteStateLog routeLog=RouteStateLog::admission){
    value["owner"]="ymcc-native";value["extraExecutables"]=0;value["periodicPolling"]=false;
    value["storageLayout"]={{"schema",2},{"evidenceRoot",W2U(root())},{"runtimeRoot",W2U(runtimeRoot())},
        {"loadedImagesInEvidenceRoot",false},{"logClearResetsRecoveryBudget",false}};
    value["gamePidSource"]="ymcc-unique-game-valve";value["atFileTime"]=std::to_string(sgNowFileTime());
    {std::lock_guard<std::mutex> lock(c->snapshotMx);c->snapshot.update(value);value=c->snapshot;}
    // Route admission is a pre-qualification breadcrumb. Keep it live and in
    // the log; persist status when the route reports an outcome.
    if(!routeEvent||routeLog!=RouteStateLog::admission)saveJson(root()+L"\\status.json",value);
    if(routeEvent){
        try{
            auto receipt=routeLog==RouteStateLog::admission?routeAdmissionLogProjection(value):noUniqueGameRouteLogProjection(value);
            receipt["queuedTrigger"]=ymcc::steamOriginal::name(routeEvent->kind);
            receipt["queuedPowerGeneration"]=routeEvent->generation?json(std::to_string(routeEvent->generation)):json(nullptr);
            receipt["writerProcessCreation100ns"]=ymcc::sleepfactlog::writerCreation100nsJson();
            log(c,"state",std::move(receipt));
            return;
        }catch(...){} // Fall back to the full state if projection cannot be prepared.
    }
    log(c,"state",std::move(value));
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
    const auto nativeWake=SleepRecoveryAuto::sharedWakeFactsLocked();
    const auto& state=SleepRecoveryAuto::state;const auto& observed=nativeWake.kernelWake;
    const auto& admission=state.wakeAdmission;
    if(!admission.is_object())return proof;
    proof.generation=state.generation;proof.epoch=state.configEpoch;
    proof.observedGeneration=observed.generation;proof.observedEpoch=observed.configEpoch;
    proof.requestedGeneration=observed.requestGeneration;proof.requested=observed.uiWakeRequested;
    proof.eventFileTime=observed.eventFileTime;proof.observedBoundaryFileTime=observed.boundaryFileTime;
    proof.eventId=observed.eventId;proof.latestEntryEpoch=nativeWake.kernelSleepEntry.configEpoch;
    proof.latestEntryFileTime=nativeWake.kernelSleepEntry.eventFileTime;
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
    const auto nativeWake=SleepRecoveryAuto::sharedWakeFactsLocked();
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
    }else if(source=="interactive-resume-committed-ready"&&ymcc::sleeprecovery::flag(admission,"interactiveWakeConfirmed")){
        // Dynamic evidence is independent of the strict kernel/profile route.
        // Re-read the native join at every preflight, never trust exported JSON.
        proof.sourceMatched=decimalField("eventFileTime",proof.eventFileTime)&&
            proof.eventFileTime==nativeWake.interactiveResume.eventFileTime&&
            SleepRecoveryAuto::completedInteractiveResumeLocked(generation,epoch,proof.boundaryFileTime,proof.boundaryGeneration);
        if(proof.sourceMatched)proof.completedEventFileTime=nativeWake.completedResume.eventFileTime;
    }else if(ymcc::sleeprecovery::flag(admission,"kernelWakeConfirmed")&&ymcc::sleeprecovery::flag(admission,"proofFresh")){
        const auto& observed=nativeWake.kernelWake;
        proof.sourceMatched=decimalField("eventFileTime",proof.eventFileTime)&&observed.configEpoch==epoch&&
            observed.eventFileTime==proof.eventFileTime&&observed.requestGeneration==generation&&observed.uiWakeRequested&&
            ((observed.eventId==507&&source=="kernel-power-507-reason-1")||
             (observed.eventId==107&&source=="kernel-power-107-target-5"));
    }
    if(nativeWake.kernelSleepEntry.configEpoch==epoch)
        proof.latestEntryFileTime=nativeWake.kernelSleepEntry.eventFileTime;
    return proof.valid(generation,epoch,sgNowFileTime(),GetTickCount64())?proof:ymcc::steamOriginal::LegacyWakeProof{};
}
static bool sameScopeNow(const std::shared_ptr<Control>& c,const Scope& scope,unsigned long long generation,bool recovery,
    unsigned long long configEpoch=0,unsigned long long bindingSerial=ULLONG_MAX,
    const SleepSteamRecovery::Captured& original={}){
    {std::lock_guard<std::mutex> lock(c->mx);if(c->stop||
        (generation&&c->latestPower>generation)||(bindingSerial!=ULLONG_MAX&&
            (!c->bindingQualified||c->bindingSerial!=bindingSerial)))return false;}
    if(configEpoch&&g_sleepRecoveryAutoConfigEpoch.load()!=configEpoch)return false;
    if(g_exitRequested.load()||(original.request?
        !SleepSteamRecovery::originalRulesCurrent(original,scope.rulesEpoch):
        g_gameRulesEpoch.load()!=scope.rulesEpoch))return false;
    if(!sharedSleepWakeClassified(generation?generation:currentPowerGeneration())||
        (recovery&&(!g_sleepRecoveryAutoEnabled.load()||g_powerLifecycle.load()!=PowerLifecycle::Ready)))return false;
    if(recovery&&generation&&!SleepRecoveryAuto::wakeOperationAllows(generation,g_sleepRecoveryAutoConfigEpoch.load()))return false;
    if(original.request){
        if(!SleepSteamRecovery::originalTargetCurrent(original)||
            !(identity(original.game.pid,original.game.created,original.game.path)==scope.game))return false;
    }else{
        NativeDetectedGame target;unsigned long long touch=0;
        if(!nativeValveReadCurrentTarget(&target,&touch)||!(identity(target.pid,target.processCreated,target.path)==scope.game))return false;
    }
    const auto steam=SleepSteamRecovery::session();
    return identity(steam.pid,steam.created,steam.root+L"\\steam.exe")==scope.steam;
}
// This guard serves read-only wake observation. Full disk rules, registry and
// fresh nativeValveReadCurrentTarget checks remain at every mutation boundary.
static bool sameObservationScopeNow(const std::shared_ptr<Control>& c,const Scope& scope,
    unsigned long long generation,unsigned long long configEpoch,unsigned long long bindingSerial,std::uint32_t expectedAccount,
    const SleepSteamRecovery::Captured& original={}){
    {std::lock_guard<std::mutex> lock(c->mx);if(c->stop||!c->bindingQualified||
        c->bindingSerial!=bindingSerial||(generation&&c->latestPower>generation))return false;}
    if(g_exitRequested.load()||(original.request?
        !SleepSteamRecovery::originalRulesCurrent(original,scope.rulesEpoch):
        g_gameRulesEpoch.load()!=scope.rulesEpoch)||
        g_sleepRecoveryAutoConfigEpoch.load()!=configEpoch||!g_sleepRecoveryAutoEnabled.load()||
        g_powerLifecycle.load()!=PowerLifecycle::Ready||!sharedSleepWakeClassified(generation?generation:currentPowerGeneration()))return false;
    if(original.request){
        if(!SleepSteamRecovery::originalTargetCurrent(original)||
            !(identity(original.game.pid,original.game.created,original.game.path)==scope.game))return false;
    }else{
        NativeGameValveSnapshot selected;
        {std::lock_guard<std::mutex> lock(g_gameValveMx);selected=g_gameValveTarget;}
        if(!selected.generation||selected.rulesEpoch!=scope.rulesEpoch||selected.game.pid!=scope.game.pid||
            selected.game.processCreated!=scope.game.birth||_wcsicmp(selected.game.path.c_str(),scope.game.path.c_str())!=0)return false;
    }
    const auto steam=g_steamSettingsObserver.sessionSnapshot();
    return expectedAccount&&steam.account==expectedAccount&&steam.pid==scope.steam.pid&&steam.created==scope.steam.birth&&
        _wcsicmp((steam.root+L"\\steam.exe").c_str(),scope.steam.path.c_str())==0;
}
static std::size_t startupFreshRouteIndex(RecoveryRoute route) noexcept {
    return route==RecoveryRoute::LegacyDynamic?0u:1u;
}
// Caller holds only original c->mx. Cancellation is atomic-only; keep scopes
// and RAII handles until the existing worker/ordinary event retires the objects.
static void cancelStartupFreshLocked(const std::shared_ptr<Control>& c,CommandRetirement& retiredCommand) noexcept {
    retireStartupCommandLocked(c,retiredCommand);
    if(c->startupFreshRequest)c->startupFreshRequest->cancelled.store(true,std::memory_order_release);
    for(const auto& wait:c->startupFreshWaits)if(wait){
        wait->phase=StartupFreshPhase::Cancelled;wait->subscription.cancel();
    }
}
struct RetiredStartupFresh {
    std::shared_ptr<StartupFreshRequest> request;
    std::array<std::shared_ptr<StartupFreshWait>,2> waits;
    CommandRetirement command;
};
static RetiredStartupFresh retireStartupFreshLocked(const std::shared_ptr<Control>& c) noexcept {
    RetiredStartupFresh old;cancelStartupFreshLocked(c,old.command);
    old.request=std::move(c->startupFreshRequest);old.waits=std::move(c->startupFreshWaits);
    return old; // Caller destroys outside c->mx; no lock-dependent sink captures.
}
struct StartupFreshRunReturn final {
    std::shared_ptr<Control> owner;
    std::shared_ptr<StartupFreshRequest> request;
    std::shared_ptr<StartupFreshWait> resumed;
    ~StartupFreshRunReturn(){
        if(!request)return;
        RetiredStartupFresh retired;
        try{
            std::lock_guard<std::mutex> lock(owner->mx);
            bool retained=false;
            if(owner->startupCommandWait&&owner->startupCommandWait->request==request){
                if(request->cancelled.load()||request->terminal.load()||owner->startupCommandWait->cancelled.load())
                    retireStartupCommandLocked(owner,retired.command);
                else retained=true;
            }
            for(std::size_t i=0;i<owner->startupFreshWaits.size();++i){
                auto& wait=owner->startupFreshWaits[i];if(!wait||wait->request!=request)continue;
                if(wait==resumed)wait->phase=StartupFreshPhase::Finished;
                if(request->cancelled.load()||request->terminal.load()||
                    wait->phase==StartupFreshPhase::Finished||wait->phase==StartupFreshPhase::Cancelled){
                    wait->subscription.cancel();retired.waits[i]=std::move(wait);
                }else retained=true;
            }
            if(!retained&&owner->startupFreshRequest==request){
                request->cancelled.store(true,std::memory_order_release);retired.request=std::move(owner->startupFreshRequest);
            }
        }catch(...){request->cancelled.store(true,std::memory_order_release);}
    }
};
static bool startupFreshScopeCurrent(const std::shared_ptr<Control>& c,const std::shared_ptr<StartupFreshRequest>& request,
    const std::shared_ptr<StartupFreshWait>& wait={}) noexcept {
    try{
        if(!c||!request||request->cancelled.load(std::memory_order_acquire))return false;
        {
            std::lock_guard<std::mutex> lock(c->mx);
            if(c->stop||c->startupFreshRequest!=request||c->sequence!=request->sequence)return false;
            if(wait&&(c->startupFreshWaits[startupFreshRouteIndex(wait->route)]!=wait||wait->phase==StartupFreshPhase::Cancelled))return false;
        }
        if(g_exitRequested.load()||!g_sleepRecoveryAutoEnabled.load()||g_powerLifecycle.load()!=PowerLifecycle::Ready||
            g_sleepRecoveryAutoConfigEpoch.load()!=request->configEpoch||g_gameRulesEpoch.load()!=request->rulesEpoch||
            currentPowerGeneration()!=request->powerGeneration||!nativeValveSelectionCurrent(request->selection)||
            !request->operationReadScope||!request->operationReadScope->current())return false;
        // The opaque witness was matched to this exact held ReadScope below.
        // Its public delegation cancels on Steam process/account epoch changes;
        // selection/rules/power are checked above. Do not call a private adapter.
        if(wait&&(!wait->nativeReadScope||!wait->nativeReadScope->current()||!wait->witness))return false;
        return !request->cancelled.load(std::memory_order_acquire);
    }catch(...){return false;}
}
// A terminal budget prevents another continuation, but is not scope loss for
// the original successful ownsRunning projection.
static bool startupFreshCurrent(const std::shared_ptr<Control>& c,const std::shared_ptr<StartupFreshRequest>& request,
    const std::shared_ptr<StartupFreshWait>& wait={}) noexcept {
    return startupFreshScopeCurrent(c,request,wait)&&!request->terminal.load(std::memory_order_acquire);
}
static std::shared_ptr<StartupFreshRequest> beginStartupFreshRequest(const std::shared_ptr<Control>& c,Pending pending,
    unsigned long long configEpoch,unsigned long long rulesEpoch,DWORD account,
    const std::shared_ptr<ymcc::steamnative::ReadScope>& operation) noexcept {
    try{
        if(pending.startupFresh||pending.startupCommand||pending.kind==Trigger::Wake||pending.kind==Trigger::SuspendCapture||!operation)return {};
        NativeGameSelectionReceipt selected;
        // Sole game owner's memory-only generation receipt; no new detector,
        // process query, current-target refresh or account authority.
        if(!nativeValveReadCachedSelection(&selected)||selected.target.rulesEpoch!=rulesEpoch)return {};
        auto request=std::make_shared<StartupFreshRequest>(pending.sequence,configEpoch,rulesEpoch,account,selected,operation);
        RetiredStartupFresh retired;
        {
            std::lock_guard<std::mutex> lock(c->mx);
            if(c->stop||c->sequence!=pending.sequence)return {};
            retired=retireStartupFreshLocked(c);c->startupFreshRequest=request;
        }
        if(startupFreshCurrent(c,request))return request;
        RetiredStartupFresh rejected;
        {std::lock_guard<std::mutex> lock(c->mx);if(c->startupFreshRequest==request)rejected=retireStartupFreshLocked(c);}
        return {};
    }catch(...){return {};}
}
static void drainStartupFreshCompletion(std::weak_ptr<Control> owner,std::weak_ptr<StartupFreshWait> pending,
    std::optional<ymcc::steamgamefacts::TrackerCompletionHint> hint) noexcept {
    const auto c=owner.lock();const auto wait=pending.lock();if(!c||!wait)return;
    bool queued=false;
    try{
        // Runs only after the shared main-window transport released its mutex.
        // This is still metadata handling; the original worker rechecks again.
        if(!startupFreshCurrent(c,wait->request,wait))return;
        {
            std::lock_guard<std::mutex> lock(c->mx);
            if(c->stop||c->startupFreshRequest!=wait->request||wait->request->cancelled.load()||wait->request->terminal.load()||
                c->startupFreshWaits[startupFreshRouteIndex(wait->route)]!=wait)return;
            if(wait->phase!=StartupFreshPhase::Arming&&wait->phase!=StartupFreshPhase::Watching)return;
            if(hint){
                const auto& receipt=hint->receipt();
                if(!hint->subscribed()||hint->key()!=wait->witness.key()||!receipt.terminal()||!receipt.revision||
                    receipt.flightSequence!=wait->witness.sequence()||receipt.factVersion!=wait->witness.factVersion())return;
            }else if(!wait->reReadNow)return;
            if(wait->phase==StartupFreshPhase::Arming){
                // Owner may post before watchCompletion returns. Retain the
                // early value; never consume until its RAII handle is installed.
                if(hint)wait->earlyHint=std::move(hint);
                return;
            }
            if(c->pending.size()>=16){wait->phase=StartupFreshPhase::Cancelled;wait->subscription.cancel();return;}
            // Preserve the exact effective original Pending. Do not call enqueue,
            // increment sequence, publish IdentityReady, or refund any ledger.
            c->pending.push_back({wait->original.kind,wait->original.generation,wait->original.sequence,wait});
            wait->consumedReceiptRevision=hint?hint->receipt().revision:0;
            wait->phase=StartupFreshPhase::Queued;queued=true;
        }
        if(queued)c->cv.notify_all();
    }catch(...){
        try{std::lock_guard<std::mutex> lock(c->mx);wait->phase=StartupFreshPhase::Cancelled;wait->subscription.cancel();}catch(...){}
    }
}
static void armStartupFreshCompletion(const std::shared_ptr<Control>& c,const std::shared_ptr<StartupFreshRequest>& request,
    Pending pending,RecoveryRoute route,const Scope& scope,bool mayMutate,
    const std::shared_ptr<ymcc::steamnative::ReadScope>& nativeReadScope,
    const ymcc::steamgamefacts::TrackerCompletionWitness& witness) noexcept {
    using namespace ymcc::steamgamefacts;
    std::shared_ptr<StartupFreshWait> wait;
    try{
        if(!request||pending.startupFresh||(pending.kind!=Trigger::Startup&&pending.kind!=Trigger::RunningLost)||!mayMutate||!witness||!nativeReadScope||
            !startupFreshCurrent(c,request))return;
        const auto& selected=request->selection.target.game;const auto& facts=witness.key()->facts();
        if(scope.game.pid!=selected.pid||scope.game.birth!=selected.processCreated||
            !samePath(scope.game.path,selected.path)||facts.selectedGame.pid!=scope.game.pid||facts.selectedGame.birth!=scope.game.birth||
            !samePath(facts.selectedGame.exactPath,scope.game.path)||
            facts.steam.pid!=scope.steam.pid||facts.steam.birth!=scope.steam.birth||facts.steam.windowsSession!=scope.steam.session||
            !samePath(facts.steam.root,nativeReadScope->expected().session.root)||
            facts.steam.processEpoch!=nativeReadScope->expected().processEpoch||
            facts.steam.accountEpoch!=nativeReadScope->expected().accountEpoch||facts.rulesEpoch!=request->rulesEpoch||
            facts.powerGeneration!=request->powerGeneration)return;
        const auto index=startupFreshRouteIndex(route);
        wait=std::make_shared<StartupFreshWait>(request,pending,route,scope,mayMutate,nativeReadScope,witness);
        std::shared_ptr<StartupFreshWait> retired;
        {
            std::lock_guard<std::mutex> lock(c->mx);
            if(c->stop||c->startupFreshRequest!=request||request->cancelled.load()||request->terminal.load()||request->routeArmed[index])return;
            request->routeArmed[index]=true;retired=std::move(c->startupFreshWaits[index]);c->startupFreshWaits[index]=wait;
        }
        const std::weak_ptr<Control> weakOwner=c;const std::weak_ptr<StartupFreshWait> weakWait=wait;
        auto issue=trackerOwner().watchCompletion(witness,[weakOwner,weakWait](const TrackerCompletionHint& hint){
            // The source caller may hold refreshMx. Sink/destructor capture only
            // weak tickets and the hint value; no consumer lock or callback here.
            return postSteamTrackerCompletionDispatch([weakOwner,weakWait,hint]{drainStartupFreshCompletion(weakOwner,weakWait,hint);});
        });
        std::optional<TrackerCompletionHint> early;bool postReRead=false;
        {
            std::lock_guard<std::mutex> lock(c->mx);
            if(c->startupFreshRequest!=request||c->startupFreshWaits[index]!=wait||request->cancelled.load()||
                request->terminal.load()||wait->phase!=StartupFreshPhase::Arming)return;
            if(issue.status==TrackerCompletionWatchStatus::Watching){
                wait->subscription=std::move(issue.subscription);wait->phase=StartupFreshPhase::Watching;
                early=std::move(wait->earlyHint);
            }else if(issue.status==TrackerCompletionWatchStatus::ReReadNow){
                wait->reReadNow=true;wait->phase=StartupFreshPhase::Watching;postReRead=true;
            }else wait->phase=StartupFreshPhase::Cancelled;
        }
        if(early||postReRead){
            // The same shared bounded transport handles registration races too;
            // a rejection has no automatic retry or eventual-delivery promise.
            if(!postSteamTrackerCompletionDispatch([weakOwner,weakWait,early]{drainStartupFreshCompletion(weakOwner,weakWait,early);})){
                std::lock_guard<std::mutex> lock(c->mx);wait->phase=StartupFreshPhase::Cancelled;wait->subscription.cancel();
            }
        }
    }catch(...){
        // An optional allocation/registration/post failure must leave a terminal
        // marker so original worker RAII can return retained request resources.
        if(wait)try{
            std::lock_guard<std::mutex> lock(c->mx);
            if(c->startupFreshWaits[startupFreshRouteIndex(route)]==wait){
                wait->phase=StartupFreshPhase::Cancelled;wait->subscription.cancel();
            }
        }catch(...){}
    }
}

static void enqueue(const std::shared_ptr<Control>& c,Trigger kind,unsigned long long generation=0){
    if(!c)return;
    CommandRetirement retiredCommand;
    {std::lock_guard<std::mutex> lock(c->mx);if(c->stop)return;
        if(kind==Trigger::Wake||kind==Trigger::SuspendCapture){
            if(!generation||generation<c->latestPower)return;
            c->latestPower=generation;
        }
        for(const auto& item:c->pending)if(item.kind==kind&&item.generation==generation)return;
        if(c->pending.size()>=16)return; // bounded; never spawn a second worker
        cancelStartupFreshLocked(c,retiredCommand); // Only cancel here; existing worker retires scopes outside consumer locks.
        Pending next{kind,generation,++c->sequence};
        if(kind==Trigger::TargetChanged){
            const auto now=GetTickCount64();
            next.notBeforeTick=now>ULLONG_MAX-100?ULLONG_MAX:now+100;
        }
        c->pending.push_back(std::move(next));
        if(c->observationSequence!=ULLONG_MAX)++c->observationSequence;
    }c->cv.notify_all();if(c->observationEvent)SetEvent(c->observationEvent.get());
}
// Discovery only: ActiveProcess can retain an exited registry PID after a
// Steam restart. An account candidate never admits publication. Each route
// must verify the real selected CUser/account, original full GameID/PID and
// saved healthy pins; the legacy normal IPC roundtrip validates the account too.
static DWORD accountCandidate(DWORD verifiedAccount){
    if(verifiedAccount)return verifiedAccount;
    const auto facts=g_steamSettingsObserver.identitySnapshot();
    return facts&&facts->registryAccountCandidateKnown()?facts->session.account:0;
}
// Diagnostic references only. These two bounded slots never grant a route,
// revive a result, or participate in the original admission/consumption logic.
static json retainedRouteTerminal(const std::shared_ptr<Control>& c,RecoveryRoute route) {
    try{std::lock_guard<std::mutex> lock(c->snapshotMx);
        const auto slots=c->snapshot.find("routeTerminals");
        if(slots!=c->snapshot.end()&&slots->is_object()){
            const auto found=slots->find(ymcc::steamOriginal::routeName(route));
            if(found!=slots->end()&&found->is_object())return *found;
        }
    }catch(...){}return nullptr;
}
static void recordRouteTerminal(const std::shared_ptr<Control>& c,RecoveryRoute route,
    const std::string& eventId,const json& receipt,bool engineObserved,bool summaryPersisted) {
    try{
        if(!receipt.is_object())return;
        const auto name=ymcc::steamOriginal::routeName(route);
        std::lock_guard<std::mutex> lock(c->snapshotMx);
        auto& slots=c->snapshot["routeTerminals"];if(!slots.is_object())slots=json::object();
        const auto prior=slots.value(name,json());
        const bool same=prior.is_object()&&prior.value("eventId",std::string{})==eventId;
        json terminal={{"schema",1},{"route",name},{"eventId",eventId},
            {"resultId",receipt.value("id",std::string{}).substr(0,256)},
            {"status",receipt.value("status",std::string("unknown")).substr(0,128)},
            {"reason",receipt.value("reason",std::string{}).substr(0,256)},
            {"engineTerminalObserved",engineObserved||(same&&prior.value("engineTerminalObserved",false))},
            {"summaryPersisted",summaryPersisted||(same&&prior.value("summaryPersisted",false))},
            {"summaryFile",std::string("last-")+name+"-summary.json"},
            {"logStream","steam-original-recovery.jsonl (including retained rotation)"},
            {"logEvent","engine"},{"engineEvent","job-finished"},
            {"publisherCallsKnown",receipt.value("publisherCallsKnown",false)},
            {"publisherCalls",receipt.value("publisherCallsKnown",false)?receipt.value("publisherCalls",json()):json()},
            {"diagnosticOnly",true},{"actionAuthority",false}};
        slots[name]=terminal;
        if(c->snapshot.value("recoveryRoute",std::string{})==name&&
           c->snapshot.value("eventId",json())==eventId){
            c->snapshot["terminalEvidence"]=terminal;c->snapshot["terminalEvidenceHistorical"]=false;
        }
    }catch(...){} // Evidence projection must not change a route outcome.
}
static bool startupCommandCurrent(const std::shared_ptr<Control>& c,
    const std::shared_ptr<StartupCommandWait>& wait)noexcept {
    if(!wait||wait->cancelled.load(std::memory_order_acquire)||!startupFreshCurrent(c,wait->request))return false;
    std::lock_guard<std::mutex> lock(c->mx);
    return c->startupCommandWait==wait&&!c->stop&&!wait->cancelled.load(std::memory_order_acquire);
}
// Coalesce advisory readiness only when this exact current request already
// has an owned command wake or queued/running read work. A Watching source can
// later reject completion delivery, so it still takes the original fallback.
static bool currentStartupContinuation(const std::shared_ptr<Control>& c) noexcept {
    try {
        std::shared_ptr<StartupCommandWait> command;
        std::array<std::shared_ptr<StartupFreshWait>,2> fresh;
        {std::lock_guard<std::mutex> lock(c->mx);
            if(c->stop)return false;
            const auto& held=c->startupCommandWait;
            if(held&&held->phase==StartupCommandPhase::Watching)command=held;
            for(std::size_t i=0;i<fresh.size();++i){const auto& wait=c->startupFreshWaits[i];
                if(!wait)continue;
                const bool queued=wait->phase==StartupFreshPhase::Queued&&std::any_of(c->pending.begin(),c->pending.end(),[&](const Pending& item){return item.startupFresh==wait;});
                if(queued)fresh[i]=wait;}}
        // Existing Current performs the original account/selection/rules/power
        // and ReadScope checks. Never call it while the consumer mx is held.
        if(command&&command->intent){const auto status=command->intent->status();
            if((status==ymcc::steamcommand::IntentStatus::Waiting||status==ymcc::steamcommand::IntentStatus::Ready)&&
                startupCommandCurrent(c,command))return true;}
        for(const auto& wait:fresh)if(wait&&startupFreshCurrent(c,wait->request,wait)){
            std::lock_guard<std::mutex> lock(c->mx);
            if(!c->stop&&c->startupFreshRequest==wait->request&&c->sequence==wait->request->sequence&&
                !wait->request->cancelled.load(std::memory_order_acquire)&&!wait->request->terminal.load(std::memory_order_acquire)&&
                c->startupFreshWaits[startupFreshRouteIndex(wait->route)]==wait&&wait->phase==StartupFreshPhase::Queued&&
                std::any_of(c->pending.begin(),c->pending.end(),[&](const Pending& item){return item.startupFresh==wait;}))return true;
        }
    }catch(...){} // Unproven progress retains the original enqueue path.
    return false;
}
static bool startupCommandNotifiedLocked(const std::shared_ptr<Control>& c)noexcept {
    return c->startupCommandWait&&c->startupCommandWait->signal->notified.load(std::memory_order_acquire);
}
static bool takeReadyStartupCommand(const std::shared_ptr<Control>& c,Pending& next){
    std::shared_ptr<StartupCommandWait> wait;
    {std::lock_guard<std::mutex> lock(c->mx);
        wait=c->startupCommandWait;
        if(!wait||!wait->signal->notified.exchange(false,std::memory_order_acq_rel))return false;}
    const bool current=startupCommandCurrent(c,wait);
    const auto status=wait->intent->status(); // No Control lock under Coordinator.
    CommandRetirement retired;
    {std::lock_guard<std::mutex> lock(c->mx);
        if(c->startupCommandWait!=wait)return false;
        if(!current||status==ymcc::steamcommand::IntentStatus::Revoked){retireStartupCommandLocked(c,retired);return false;}
        if(status!=ymcc::steamcommand::IntentStatus::Ready||wait->phase!=StartupCommandPhase::Watching)return false;
        next=wait->original;next.startupFresh.reset();next.startupCommand=wait;
        wait->phase=StartupCommandPhase::Queued;
    }
    return true;
}
// This is before routeAdmission.admit and before any engine/native role stack.
// A second route never overwrites the single waiting route or waits on Busy.
static std::shared_ptr<StartupCommandWait> prepareStartupCommand(const std::shared_ptr<Control>& c,
    const std::shared_ptr<StartupFreshRequest>& request,Pending pending,RecoveryRoute route,const Scope& scope,
    const std::string& gameId,std::uint32_t appid,unsigned long long account64,bool mayMutate,
    const ymcc::steamcommand::Target& target){
    if(!startupFreshCurrent(c,request))return {};
    std::shared_ptr<StartupCommandWait> wait;
    {std::lock_guard<std::mutex> lock(c->mx);wait=c->startupCommandWait;}
    if(wait){
        if(wait->request!=request||wait->route!=route||!(wait->scope==scope)||wait->gameId!=gameId||
            wait->appid!=appid||wait->account64!=account64||wait->mayMutate!=mayMutate||
            !wait->intent->matches(target)||!startupCommandCurrent(c,wait))return {};
        if(wait->intent->status()!=ymcc::steamcommand::IntentStatus::Ready)return {};
        {std::lock_guard<std::mutex> lock(c->mx);
            if(c->startupCommandWait!=wait||wait->cancelled.load())return {};
            wait->phase=StartupCommandPhase::Running;}
        return wait;
    }
    auto signal=std::make_shared<StartupCommandSignal>(c);
    auto intent=ymcc::steamcommand::prepareIntent(target,signal);
    if(!intent)return {};
    pending.startupFresh.reset();pending.startupCommand.reset();
    wait=std::make_shared<StartupCommandWait>(request,pending,route,scope,gameId,appid,account64,mayMutate,signal,intent);
    {std::lock_guard<std::mutex> lock(c->mx);
        if(c->stop||c->startupCommandWait||c->startupFreshRequest!=request||request->cancelled.load()||request->terminal.load()||
            c->sequence!=request->sequence)return {};
        c->startupCommandWait=wait;
    }
    const auto result=intent->promote(); // No Control lock and no lost-wake gap.
    if(result==ymcc::steamcommand::IntentStatus::Ready||result==ymcc::steamcommand::IntentStatus::Waiting){
        std::lock_guard<std::mutex> lock(c->mx);
        if(c->startupCommandWait!=wait||wait->cancelled.load())return {};
        wait->phase=result==ymcc::steamcommand::IntentStatus::Ready?StartupCommandPhase::Running:StartupCommandPhase::Watching;
        if(result==ymcc::steamcommand::IntentStatus::Ready)return wait;
        return {}; // Registered passive pending. Original worker remains free.
    }
    CommandRetirement retired;
    {std::lock_guard<std::mutex> lock(c->mx);if(c->startupCommandWait==wait)retireStartupCommandLocked(c,retired);}
    return {}; // Busy is not registered, and is never awaited as Waiting.
}
// Consume the existing unique-game receipt, never elect or refresh a target.
static std::optional<RunningLossNotice> runningLossCurrent(const std::shared_ptr<Control>& c,const Pending& pending){
    if(pending.kind!=Trigger::RunningLost)return {};
    std::optional<RunningLossNotice> notice;
    {std::lock_guard<std::mutex> lock(c->mx);
        if(c->stop||!c->runningLossNotice||c->runningLossNotice->sequence!=pending.sequence)return {};
        notice=c->runningLossNotice;
        if(!c->bindingQualified||!(c->boundGame==notice->game)||!(c->boundSteam==notice->steam)||
            c->boundGameId!=notice->gameId||c->boundAppId!=notice->appid)return {};
    }
    if(g_exitRequested.load()||!g_sleepRecoveryAutoEnabled.load()||g_powerLifecycle.load()!=PowerLifecycle::Ready||
        g_sleepRecoveryAutoConfigEpoch.load()!=notice->configEpoch||g_gameRulesEpoch.load()!=notice->rulesEpoch||
        currentPowerGeneration()!=notice->powerGeneration||!nativeValveSelectionCurrent(notice->selection))return {};
    return notice;
}
static void runRoute(const std::shared_ptr<Control>& c,Pending pending,const json& config,RecoveryRoute route,bool mayMutate,
    const std::shared_ptr<ymcc::steamnative::ReadScope>& operationReadScope=nullptr,
    const std::shared_ptr<StartupFreshRequest>& startupRequest={}){
    const auto lossNotice=runningLossCurrent(c,pending);
    if(pending.kind==Trigger::RunningLost&&!lossNotice)return;
    const bool legacy=route==RecoveryRoute::LegacyDynamic;
    const auto continuation=pending.startupFresh;
    StartupCommandRunReturn commandReturn{c,pending.startupCommand};
    if(pending.startupCommand&&(!startupRequest||pending.startupCommand->request!=startupRequest||
        pending.startupCommand->route!=route||!startupCommandCurrent(c,pending.startupCommand)))return;
    if(continuation&&(!startupRequest||continuation->request!=startupRequest||continuation->route!=route||
        continuation->original.sequence!=pending.sequence||!startupFreshCurrent(c,startupRequest,continuation)))return;
    auto& routeAdmission=legacy?c->legacyAdmission:c->admission;
    const auto retainedTerminal=retainedRouteTerminal(c,route);
    publish(c,{{"recoveryRoute",ymcc::steamOriginal::routeName(route)},{"phase","route-started"},{"reason","independent-route-admission"},
        {"engineDispatched",false},{"eventId",nullptr},{"trigger",nullptr},{"lastResult",nullptr},{"lastResultHistorical",true},
        {"trackerRead",nullptr},{"trackerGameId",""},
        {"attemptSequence",std::to_string(pending.sequence)},{"terminalEvidence",retainedTerminal},
        {"terminalEvidenceHistorical",retainedTerminal.is_object()},
        {"lastResultState",retainedTerminal.is_object()?"no-current-result-retained-terminal":"no-current-result"}},&pending);
    const auto configEpoch=g_sleepRecoveryAutoConfigEpoch.load();
    const bool enabled=config.is_object()&&config.value("enabled",false)&&g_sleepRecoveryAutoEnabled.load();
    if(!enabled){c->ownsRunning=false;publish(c,{{"phase","disabled"},{"reason","automatic-recovery-disabled"},{"target",nullptr},{"steam",nullptr},{"currentBindingValid",false},{"nativeRunning",nullptr},{"lastResultHistorical",true}});return;}
    const bool sleepOriginal=pending.kind==Trigger::Wake||pending.kind==Trigger::SuspendCapture;
    const auto original=sleepOriginal?SleepSteamRecovery::capturedFor(pending.generation):SleepSteamRecovery::Captured{};
    if(sleepOriginal&&(!SleepSteamRecovery::originalTargetCurrent(original)||
        (pending.kind==Trigger::SuspendCapture&&(g_powerLifecycle.load()!=PowerLifecycle::Suspending||
            currentPowerGeneration()!=pending.generation)))){
        c->ownsRunning=false;publish(c,{{"phase","waiting"},{"reason","original-sleep-request-unavailable"}});return;
    }
    NativeDetectedGame game;unsigned long long touch=0;
    if(sleepOriginal){game.pid=original.game.pid;game.processCreated=original.game.created;game.path=original.game.path;}
    if(!sleepOriginal&&!nativeValveReadCurrentTarget(&game,&touch)){
        const auto session=SleepSteamRecovery::session();
        c->ownsRunning=false;publish(c,{{"phase","waiting"},{"reason","ymcc-unique-game-unavailable"},
            {"target",nullptr},{"steam",asJson(identity(session.pid,session.created,session.root+L"\\steam.exe"))},
            {"currentBindingValid",false},{"nativeRunning",nullptr},{"lastResultHistorical",true}},
            &pending,RouteStateLog::noUniqueGame);return;
    }
    const auto client=sleepOriginal?original.client:SleepSteamRecovery::session();
    std::wstring parentPath;ULONGLONG parentBirth=0;
    if(!focusQueryProcessIdentity(GetCurrentProcessId(),&parentPath,&parentBirth))throw std::runtime_error("ymcc-process-identity-unavailable");
    const Scope scope{identity(GetCurrentProcessId(),parentBirth,parentPath),
        identity(game.pid,game.processCreated,game.path),identity(client.pid,client.created,client.root+L"\\steam.exe"),
        original.request?original.request->rulesEpoch:g_gameRulesEpoch.load()};
    if(continuation&&!(scope==continuation->scope))return;
    if(pending.startupCommand&&!(scope==pending.startupCommand->scope))return;
    if(!scope.valid()){c->ownsRunning=false;publish(c,{{"phase","waiting"},{"reason","unique-game-or-steam-unavailable"},{"currentBindingValid",false},
        {"nativeRunning",nullptr},{"lastResultHistorical",true},{"target",asJson(scope.game)},{"steam",asJson(scope.steam)}});return;}
    // A route owns only its own dispatch. Keep the original Wake available
    // while the other route has not qualified yet; no consumed job is refunded.
    const auto retireDeferredWakeLocked=[&]{
        const ymcc::steamOriginal::Event event{pending.kind,pending.generation,pending.sequence};
        if(pending.kind==Trigger::Wake&&c->deferredWake==pending.generation&&
            !c->legacyAdmission.preview(scope,event,enabled).accepted&&!c->admission.preview(scope,event,enabled).accepted)
            c->deferredWake=0;
    };
    if(lossNotice&&(!(scope.game==lossNotice->game)||!(scope.steam==lossNotice->steam)||
        scope.rulesEpoch!=lossNotice->rulesEpoch))return;
    // Reject already consumed notifications before any log parsing or CDP read.
    {std::lock_guard<std::mutex> lock(c->mx);
        // A terminal receipt for the preceding pair cannot consume a new pair.
        // Its already queued identity event performs its own original admission.
        if(!mayMutate&&(!(c->boundGame==scope.game)||!(c->boundSteam==scope.steam)))return;
        // Startup belongs to the exact Steam/game pair in each original ledger.
        // Ordinary events may discover a new pair after the first YMCC startup.
        if(pending.kind!=Trigger::Wake&&pending.kind!=Trigger::SuspendCapture&&pending.kind!=Trigger::RunningLost){
            const bool startupCovered=c->legacyAdmission.observedPair(scope,Trigger::Wake)||
                c->admission.observedPair(scope,Trigger::Wake);
            const ymcc::steamOriginal::Event startup{Trigger::Startup,0,pending.sequence};
            if(startupCovered&&pending.kind==Trigger::Startup)pending.kind=Trigger::TargetChanged;
            if(!startupCovered&&routeAdmission.preview(scope,startup,enabled).accepted)pending.kind=Trigger::Startup;
            c->startupPending=!startupCovered&&(c->legacyAdmission.preview(scope,startup,enabled).accepted||
                c->admission.preview(scope,startup,enabled).accepted);
        }
        const auto preview=routeAdmission.preview(scope,{pending.kind,pending.generation,pending.sequence},enabled);
        if(!preview.accepted){retireDeferredWakeLocked();return;}}
    publish(c,{{"phase","qualifying"},{"reason","binding-current-original-instance"},
        {"target",asJson(scope.game)},{"steam",asJson(scope.steam)},
        {"currentBindingValid",false},{"nativeRunning",nullptr},{"lastResultHistorical",true}});
    // This log lookup maps only the valve's already admitted PID. It cannot
    // pick a game from the Steam library or enumerate an executable name.
    const auto purpose=mayMutate&&(pending.kind==Trigger::Startup||pending.kind==Trigger::RunningLost||pending.kind==Trigger::Wake)
        ?ymcc::steamnative::ReadPurpose::PrepareMutation:ymcc::steamnative::ReadPurpose::ObserveOriginal;
    const auto nativeReadScope=continuation?continuation->nativeReadScope:nativeSteamNativeReadScope(client,purpose);
    if(!nativeReadScope||!nativeReadScope->current()||(operationReadScope&&!operationReadScope->current())){
        if(pending.kind!=Trigger::Wake&&pending.kind!=Trigger::SuspendCapture&&pending.kind!=Trigger::RunningLost){
            std::lock_guard<std::mutex> lock(c->mx);if(!c->stop)c->startupPending=true;
        }
        c->ownsRunning=false;publish(c,{{"phase","waiting"},{"reason","native-read-scope-unavailable"}});return;
    }
    const auto trackerAllowed=[&]{return (!startupRequest||startupFreshScopeCurrent(c,startupRequest,continuation))&&
        nativeReadScope->current()&&(!operationReadScope||operationReadScope->current())&&
        sameScopeNow(c,scope,pending.generation,false,configEpoch,ULLONG_MAX,original)&&nativeReadScope->current();};
    unsigned long long bindingBefore=0;{std::lock_guard<std::mutex> lock(c->mx);bindingBefore=c->bindingSerial;}
    NativeSteamTrackerFreshCompletion freshCompletion;
    const bool mayWatchStartup=startupRequest&&!continuation&&!sleepOriginal&&(pending.kind==Trigger::Startup||pending.kind==Trigger::RunningLost)&&mayMutate&&
        startupFreshCurrent(c,startupRequest);
    const auto tracked=sleepOriginal
        ?SteamGameLink::trackingOriginal(client,{game.pid,game.processCreated,game.path},original.selectedProcess,trackerAllowed)
        :purpose==ymcc::steamnative::ReadPurpose::ObserveOriginal
        ?nativeSteamNativeObservationTracking(client,{game.pid,game.processCreated,game.path},trackerAllowed)
        :SteamGameLink::tracking(client,{game.pid,game.processCreated,game.path},
            ymcc::sleepsteam::TrackingScope::ProcessLifetime,
            ymcc::steamgamefacts::TrackerGroupCapturePolicy::Capture,trackerAllowed,mayWatchStartup?&freshCompletion:nullptr);
    // This reference belongs to this route/request and is held through Engine return.
    // Scope selection below is unchanged until the original sleep-target binding
    // and final native/reference/Packet contracts are delivered together.
    const auto processGroup=tracked.processGroup;
    // Direct tracking and historical startup handoff remain distinct sources.
    // Only the shared owner can mint the latter and keep its account/source current.
    const auto candidateHandoff=tracked.startupHandoff;
    const auto candidateHistoricalHandoff=tracked.historicalStartupHandoff;
    const bool handoffAmbiguous=candidateHandoff&&candidateHistoricalHandoff;
    const bool handoffDirectGap=tracked.complete&&tracked.binding.status=="unknown"&&tracked.gameId.empty()&&
        !tracked.binding.appid;
    bool handoffSelected=false;
    if(!handoffAmbiguous&&handoffDirectGap&&candidateHandoff&&candidateHandoff->launch()&&
        ymcc::steamOriginalRecovery::startupHandoffOwnerCurrent(candidateHandoff,tracked.startupHandoffCurrent)){
        const auto& launch=*candidateHandoff->launch();
        handoffSelected=ymcc::steamOriginalRecovery::startupHandoffMatches(candidateHandoff,
            {{client.pid,client.created,client.root+L"\\steam.exe"},
             {game.pid,game.processCreated,game.path},scope.game.session,launch.fullGameId(),launch.appid()});
    }
    bool historicalHandoffSelected=false;
    if(!handoffAmbiguous&&handoffDirectGap&&candidateHistoricalHandoff&&
        ymcc::steamOriginalRecovery::historicalStartupHandoffOwnerCurrent(candidateHistoricalHandoff,tracked.historicalStartupHandoffCurrent)){
        historicalHandoffSelected=ymcc::steamOriginalRecovery::historicalStartupHandoffMatches(candidateHistoricalHandoff,
            {{client.pid,client.created,client.root+L"\\steam.exe"},
             {game.pid,game.processCreated,game.path},scope.game.session,
             candidateHistoricalHandoff->fullGameId(),candidateHistoricalHandoff->appid()});
    }
    const auto startupHandoff=handoffSelected?candidateHandoff:nullptr;
    const auto startupHandoffCurrent=handoffSelected?tracked.startupHandoffCurrent:ymcc::steamgamefacts::TrackerOwnerCurrent{};
    const auto historicalStartupHandoff=historicalHandoffSelected?candidateHistoricalHandoff:nullptr;
    const auto historicalStartupHandoffCurrent=historicalHandoffSelected?tracked.historicalStartupHandoffCurrent:ymcc::steamgamefacts::TrackerOwnerCurrent{};
    const auto boundGameId=handoffSelected?std::to_string(startupHandoff->launch()->fullGameId()):
        historicalHandoffSelected?std::to_string(historicalStartupHandoff->fullGameId()):tracked.gameId;
    const auto expected=boundGameId;
    const auto expectedApp=handoffSelected?startupHandoff->launch()->appid():
        historicalHandoffSelected?historicalStartupHandoff->appid():tracked.binding.appid;
    const bool fixedTarget=config.value("targetMode",std::string("generic"))=="fixed";
    if(handoffAmbiguous||!tracked.complete||(!handoffSelected&&!historicalHandoffSelected&&tracked.binding.status!="tracked")||boundGameId.empty()||!expectedApp||
        (fixedTarget&&(boundGameId!=config.value("targetGameId",std::string{})||
            expectedApp!=config.value("targetAppId",std::uint32_t{})))){
        if(mayWatchStartup&&freshCompletion.freshSourceBusy&&freshCompletion.witness)
            armStartupFreshCompletion(c,startupRequest,pending,route,scope,mayMutate,nativeReadScope,freshCompletion.witness);
        c->ownsRunning=false;json refusal={{"phase","unqualified"},{"reason",handoffAmbiguous?"ambiguous-startup-handoff":tracked.complete?"exact-type2-gameid-not-bound":"shared-tracker-read-unavailable"},
            {"target",asJson(scope.game)},{"trackerGameId",tracked.gameId}};
        try{refusal["trackerRead"]={{"complete",tracked.complete},{"bindingStatus",tracked.binding.status},{"appid",tracked.binding.appid},
            {"log",tracked.log},{"nativePrimary",tracked.primary},
            {"startupHandoff",{{"present",static_cast<bool>(candidateHandoff)},{"directGapEligible",handoffDirectGap},
                {"selected",handoffSelected},{"ownerCallbackPresent",static_cast<bool>(tracked.startupHandoffCurrent)},
                {"actionAuthority",false}}},
            {"historicalStartupHandoff",{{"present",static_cast<bool>(candidateHistoricalHandoff)},{"directGapEligible",handoffDirectGap},
                {"selected",historicalHandoffSelected},{"ownerCallbackPresent",static_cast<bool>(tracked.historicalStartupHandoffCurrent)},
                {"ambiguous",handoffAmbiguous},{"actionAuthority",false}}}};}catch(...){}
        publish(c,std::move(refusal));return;
    }
    if(lossNotice&&(boundGameId!=lossNotice->gameId||expectedApp!=lossNotice->appid))return;
    std::uint64_t fullGameId=0;
    const bool idMatches=ymcc::sleepsteam::unsignedDecimal(boundGameId,fullGameId)&&
        ymcc::steamappstatewin::gameIdMatches(fullGameId,expectedApp);
    if(!idMatches||((fullGameId>>24)&255)!=2){
        c->ownsRunning=false;
        // A shortcut launch can still be tracked under a native AppID.
        publish(c,{{"phase","unqualified"},{"reason","non-type2-gameid"},{"target",asJson(scope.game)},
            {"trackerGameId",tracked.gameId},{"trackerRead",{{"complete",tracked.complete},
                {"bindingStatus",tracked.binding.status},{"appid",tracked.binding.appid},
                {"gameIdKind",idMatches?"native-app":"unknown"},
                {"log",tracked.log},{"nativePrimary",tracked.primary},{"actionAuthority",false}}}});
        return;
    }
    if(pending.kind==Trigger::Wake&&(g_powerLifecycle.load()!=PowerLifecycle::Ready||!sharedSleepWakeClassified(pending.generation))){
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
    // Existing route currently depends on the global selection. SleepOriginal
    // needs its own capture/target contract before it can register here.
    NativeGameSelectionReceipt requestSelection;
    if(processGroup&&!sleepOriginal&&(!nativeValveReadCurrentSelection(&requestSelection)||
        !(identity(requestSelection.target.game.pid,requestSelection.target.game.processCreated,
            requestSelection.target.game.path)==scope.game)||requestSelection.target.rulesEpoch!=scope.rulesEpoch))return;
    if(startupRequest&&processGroup&&
        (requestSelection.target.identityGeneration!=startupRequest->selection.target.identityGeneration||
         requestSelection.powerGeneration!=startupRequest->powerGeneration||
         !startupFreshScopeCurrent(c,startupRequest,continuation)))return;
    const auto businessRequestCancellation=processGroup?ymcc::steamgamefacts::RequestCancellation::create():nullptr;
    if(processGroup&&!businessRequestCancellation)return;
    auto activeGroupRequest=processGroup?std::make_shared<ActiveGroupRequest>():nullptr;
    if(activeGroupRequest){
        activeGroupRequest->cancellation=businessRequestCancellation;
        activeGroupRequest->sequence=pending.sequence;activeGroupRequest->configEpoch=configEpoch;
        activeGroupRequest->rulesEpoch=scope.rulesEpoch;activeGroupRequest->route=route;
        activeGroupRequest->game=scope.game;activeGroupRequest->steam=scope.steam;
        activeGroupRequest->targetDomain=sleepOriginal?GroupRequestTarget::SleepOriginal:GroupRequestTarget::GlobalSelection;
        activeGroupRequest->selectionGeneration=requestSelection.target.identityGeneration;
        activeGroupRequest->powerGeneration=sleepOriginal?pending.generation:requestSelection.powerGeneration;
    }
    GroupRequestRegistration requestRegistration(c,activeGroupRequest);
    unsigned long long bindingSerial=0;
    const auto registerBinding=[&]{std::lock_guard<std::mutex> lock(c->mx);
        if(c->stop||c->bindingSerial!=bindingBefore||c->bindingSerial==ULLONG_MAX||
            (pending.generation&&c->latestPower>pending.generation))return;
        if(startupRequest&&(c->startupFreshRequest!=startupRequest||c->sequence!=startupRequest->sequence||
            startupRequest->cancelled.load(std::memory_order_acquire)))return;
        if(activeGroupRequest){
            cancelGroupRequestLocked(c);
            activeGroupRequest->bindingSerial=c->bindingSerial+1;
            c->activeGroupRequest=activeGroupRequest;
        }
        bindingSerial=++c->bindingSerial;c->bindingQualified=true;c->boundSleepRequest=original.request;c->boundGame=scope.game;c->boundSteam=scope.steam;
        c->boundGameId=boundGameId;c->boundAppId=expectedApp;
    };
    if(sleepOriginal){
        if(businessRequestCancellation){
            if(!SleepSteamRecovery::withOriginalTarget(original,businessRequestCancellation,registerBinding))return;
        }else if(!SleepSteamRecovery::withOriginalTargetPublication(original,registerBinding))return;
    }else registerBinding();
    if(!bindingSerial)return;
    const auto businessCurrent=[c,activeGroupRequest,businessRequestCancellation,requestSelection,original,startupRequest,continuation,startupHandoff,startupHandoffCurrent,historicalStartupHandoff,historicalStartupHandoffCurrent]{
        if(!ymcc::steamOriginalRecovery::startupHandoffOwnerCurrent(startupHandoff,startupHandoffCurrent))return false;
        if(!ymcc::steamOriginalRecovery::historicalStartupHandoffOwnerCurrent(historicalStartupHandoff,historicalStartupHandoffCurrent))return false;
        if(startupRequest&&!startupFreshScopeCurrent(c,startupRequest,continuation))return false;
        if(!activeGroupRequest)return true;
        if(!businessRequestCancellation->current())return false;
        {std::lock_guard<std::mutex> lock(c->mx);if(c->stop||c->activeGroupRequest!=activeGroupRequest||
            !c->bindingQualified||c->bindingSerial!=activeGroupRequest->bindingSerial)return false;}
        return (original.request?SleepSteamRecovery::originalTargetCurrent(original):nativeValveSelectionCurrent(requestSelection))&&businessRequestCancellation->current();
    };
    const auto scopeNow=[c,scope,pending,configEpoch,bindingSerial,proof,legacyProof,powerOwnerWitness,nativeReadScope,operationReadScope,businessCurrent,original](bool recovery){
        return businessCurrent()&&(!operationReadScope||operationReadScope->current())&&nativeReadScope->current()&&sameScopeNow(c,scope,pending.generation,recovery,configEpoch,bindingSerial,original)&&
            (pending.kind!=Trigger::Wake||(powerOwnerWitness?legacyWakeProof(pending.generation,configEpoch)==legacyProof:
                wakeProof(pending.generation,configEpoch)==proof))&&nativeReadScope->current()&&(!operationReadScope||operationReadScope->current())&&
            businessCurrent();
    };
    // Registration precedes this check; writer commits use the same mx.
    if(!scopeNow(false)){if(businessRequestCancellation)businessRequestCancellation->cancel();return;}
    // Prepare the same module-fact cache before private account/role reads.
    // Result never changes admission; sleep capture remains CacheOnlyTryLock.
    auto profilePreparation=ymcc::steamrecordwin::ProfilePreparationStatus::NotAttempted;
    if(!sleepOriginal&&purpose==ymcc::steamnative::ReadPurpose::PrepareMutation&&
        processGroup&&processGroup->graph().count==2){
        try {
            profilePreparation=nativeSteamPrepareModuleProfile(client,processGroup,nativeReadScope,
                operationReadScope,[scopeNow]{return scopeNow(false);});
        }catch(...){profilePreparation=ymcc::steamrecordwin::ProfilePreparationStatus::Exception;}
        if(profilePreparation==ymcc::steamrecordwin::ProfilePreparationStatus::CurrentRejected||
            !scopeNow(false)||!processGroup->processCurrent()){
            if(businessRequestCancellation)businessRequestCancellation->cancel();return;
        }
    }
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
            publish(c,{{"phase","waiting"},{"reason","legacy-active-account-unavailable"},{"moduleProfilePreparation",ymcc::steamrecordwin::profilePreparationLabel(profilePreparation)}});return;
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
    if(!privateIdentity.available||privateIdentity.gameId64!=fullGameId||privateIdentity.appid!=expectedApp||
        !sessionRead.is_object()||!sessionRead.value("ok",false)||sessionRead.value("mutated",json())!=json(false)||!readAllowed()){
        publish(c,{{"phase","waiting"},{"reason",privateIdentity.reason},{"moduleProfilePreparation",ymcc::steamrecordwin::profilePreparationLabel(profilePreparation)}});return;
    }
    }
    // A valid timestamp is not the current shared wake operation permit.
    // Keep this original Wake unconsumed while Native retry/classification
    // still denies recovery; a later final proof can use its original route.
    if(pending.kind==Trigger::Wake&&!SleepRecoveryAuto::wakeOperationAllows(pending.generation,configEpoch)){
        publish(c,{{"phase","waiting-confirmed-wake"},{"reason","shared-wake-operation-not-current"}});return;
    }
    std::shared_ptr<ymcc::steamcommand::Intent> mutationIntent;
    struct IntentReturn {std::shared_ptr<ymcc::steamcommand::Intent>& value;~IntentReturn(){if(value)value->cancel();}}intentReturn{mutationIntent};
    ymcc::steamOriginal::Decision commandPreview;
    {std::lock_guard<std::mutex> lock(c->mx);commandPreview=routeAdmission.preview(scope,{pending.kind,pending.generation,pending.sequence},enabled);}
    if(!commandPreview.accepted)return;
    if(commandPreview.recoveryAllowed&&mayMutate&&(pending.kind==Trigger::Startup||pending.kind==Trigger::RunningLost||pending.kind==Trigger::Wake)){
        const ymcc::steamcommand::Target commandTarget{client.pid,client.created,client.root};
        if(pending.kind==Trigger::Startup||pending.kind==Trigger::RunningLost){
            auto ready=prepareStartupCommand(c,startupRequest,pending,route,scope,boundGameId,expectedApp,privateIdentity.account64,mayMutate,commandTarget);
            if(!ready){publish(c,{{"phase","waiting-command"},{"reason","public-steam-command-unavailable-before-admit"},{"engineDispatched",false}});return;}
            commandReturn.wait=ready;mutationIntent=ready->intent;
        }else{
            if(!c->mutationAvailability||!c->mutationAvailability->event()){
                publish(c,{{"phase","waiting-command"},{"reason","public-steam-command-signal-unavailable-before-admit"},{"engineDispatched",false}});return;
            }
            mutationIntent=ymcc::steamcommand::prepareIntent(commandTarget,c->mutationAvailability);
            if(!mutationIntent)return;
        }
        if(!scopeNow(false))return;
    }
    ymcc::steamOriginal::Decision decision;
    {std::lock_guard<std::mutex> lock(c->mx);decision=routeAdmission.admit(scope,{pending.kind,pending.generation,pending.sequence},enabled);
        if(decision.accepted&&pending.kind==Trigger::Wake)c->startupPending=false;
        if(pending.kind!=Trigger::Wake&&pending.kind!=Trigger::SuspendCapture&&pending.kind!=Trigger::RunningLost){
            const bool startupCovered=c->legacyAdmission.observedPair(scope,Trigger::Wake)||
                c->admission.observedPair(scope,Trigger::Wake);
            const ymcc::steamOriginal::Event startup{Trigger::Startup,0,pending.sequence};
            c->startupPending=!startupCovered&&(c->legacyAdmission.preview(scope,startup,enabled).accepted||
                c->admission.preview(scope,startup,enabled).accepted);
        }}
    {std::lock_guard<std::mutex> lock(c->mx);retireDeferredWakeLocked();}
    if(!decision.accepted)return;
    c->ownedPid=scope.game.pid;c->ownedBirth=scope.game.birth;c->ownsRunning=true;
    const auto stillCurrent=[scopeNow,mayRecover=decision.recoveryAllowed&&mayMutate]{return scopeNow(mayRecover);};
    const auto eventId=std::to_string(scope.parent.pid)+"-"+std::to_string(scope.parent.birth)+"-"+std::to_string(pending.sequence);
    json request={{"schema","ymcc.steam-original-recovery-event.v1"},{"parent",asJson(scope.parent)},
        {"target",asJson(scope.game)},{"steam",asJson(scope.steam)},
        {"trigger",{{"kind",ymcc::steamOriginal::name(pending.kind)},{"generation",std::to_string(pending.generation)},
            {"id",eventId},{"eventId",eventId},{"rulesEpoch",std::to_string(scope.rulesEpoch)},{"timestamp",nativeLifecycleTimestamp()}}},
        {"rulesEpoch",std::to_string(scope.rulesEpoch)},{"configEpoch",std::to_string(configEpoch)},
        {"bindingSerial",std::to_string(bindingSerial)},{"recoveryAllowed",decision.recoveryAllowed&&mayMutate},
        {"recoveryRoute",ymcc::steamOriginal::routeName(route)},
        {"moduleProfilePreparation",ymcc::steamrecordwin::profilePreparationLabel(profilePreparation)},
        {"accountSelection",client.account?"verified-session":"candidate-requires-original-native-reference"},
        {"targetStatePath",W2U(MONITOR_GAME_TARGET_JSON)},{"dataRoot",W2U(runtimeRoot())},{"evidenceRoot",W2U(root())},{"logRoot",W2U(yemancc_data_dir())},
        {"config",config},{"extraExecutables",0}};
    request["target"]["gameId"]=boundGameId;request["target"]["appid"]=expectedApp;
    request["game"]=request["target"];
    request["identitySource"]=handoffSelected?"public-startup-handoff":
        historicalHandoffSelected?"public-historical-startup-handoff":"direct-steam-process-row";
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
        {"moduleProfilePreparation",ymcc::steamrecordwin::profilePreparationLabel(profilePreparation)},
        {"targetMode",fixedTarget?"fixed":"generic"},{"currentBindingValid",true},{"nativeRunning",nullptr},{"lastResultHistorical",true}});
    request["account64"]=std::to_string(privateIdentity.account64); // private; added AFTER durable public event
    const auto emit=[c,eventId,route,captureBinding=ymcc::steamOriginalTrace::CaptureBinding{}](const json& row) mutable {
        if(ymcc::steamOriginalTrace::CaptureBinding::isBegin(row))captureBinding.begin(row,currentPowerGeneration());
        if(row.value("event",std::string{})=="job-finished"&&row.contains("result"))
            recordRouteTerminal(c,route,eventId,row["result"],true,false);
        auto tagged=captureBinding.tag(row);tagged["engineEvent"]=row.value("event",std::string("unknown"));tagged["eventId"]=eventId;log(c,"engine",std::move(tagged));
    };
    const auto observationGuard=[c,scope,pending,configEpoch,bindingSerial,proof,legacyProof,powerOwnerWitness,nativeReadScope,operationReadScope,scopeNow,businessCurrent,original,expectedAccount=static_cast<std::uint32_t>(privateIdentity.account64)]{
        if(!businessCurrent())return false;
        // The cached observer can lag behind the already-qualified UI account
        // or process after wake/FSE. A cache miss cannot cancel this read-only
        // watch while the full original-instance scope is still current.
        // Both observer paths share the sticky process/account/power lifetime.
        // Mutation still requires stillCurrent plus a fresh native snapshot,
        // original reference/pins and the unchanged durable consume budget.
        if(!nativeReadScope->current()||(operationReadScope&&!operationReadScope->current()))return false;
        if(pending.kind==Trigger::Wake&&(powerOwnerWitness?legacyWakeProof(pending.generation,configEpoch)!=legacyProof:
            wakeProof(pending.generation,configEpoch)!=proof))return false;
        return (sameObservationScopeNow(c,scope,pending.generation,configEpoch,bindingSerial,expectedAccount,original)||
            scopeNow(false))&&nativeReadScope->current()&&(!operationReadScope||operationReadScope->current());
    };
    auto receipt=steamOriginalRecoveryEngineHandle(request,stillCurrent,emit,observationGuard,
        reinterpret_cast<std::uintptr_t>(c->observationEvent.get()),
        nativeReadScope?reinterpret_cast<std::uintptr_t>(nativeReadScope->powerCancellationEvent()):0,
        nativeReadScope?reinterpret_cast<std::uintptr_t>(nativeReadScope->identityCancellationEvent()):0,nativeReadScope,processGroup,businessRequestCancellation,mutationIntent,c->mutationAvailability,startupHandoff,startupHandoffCurrent,historicalStartupHandoff,historicalStartupHandoffCurrent);
    if(mutationIntent)mutationIntent->cancel(); // Original engine has returned through real cleanup.
    const auto receiptStatus=receipt.value("status",std::string{});
    const bool scopeCurrent=scopeNow(false);
    const bool observationCurrent=scopeCurrent&&(receiptStatus=="healthy"||receiptStatus=="recovered-native");
    if(!observationCurrent)c->ownsRunning=false;
    saveJson(root()+L"\\last-recovery-summary.json",receipt);
    saveJson(root()+L"\\last-"+U2W(ymcc::steamOriginal::routeName(route))+L"-summary.json",receipt);
    recordRouteTerminal(c,route,eventId,receipt,false,true);
    publish(c,{{"phase",receipt.value("phase",receipt.value("status",std::string("unqualified")))},
        {"reason",receipt.value("reason",std::string{})},{"eventId",eventId},{"lastResult",receipt},{"lastResultState","returned-terminal"},
        {"currentBindingValid",scopeCurrent},
        {"nativeRunning",observationCurrent?receipt.value("after",receipt.value("before",json::object())).value("running",json()):json()},
        {"lastResultHistorical",!observationCurrent}});
}
// Each route has its own admission, qualification and source acquisition.
// Both use the engine's existing original-instance publisher and durable ledger.
// A route rejection before publication never rejects the other route.
// Caller holds mx. A notice belongs to queued work or a retained original
// continuation; failed pre-admission reads never consume a permanent pair slot.
static void retireRunningLossLocked(const std::shared_ptr<Control>& c,unsigned long long sequence){
    if(!c->runningLossNotice||c->runningLossNotice->sequence!=sequence)return;
    for(const auto& pending:c->pending)
        if(pending.kind==Trigger::RunningLost&&pending.sequence==sequence)return;
    const auto& request=c->startupFreshRequest;
    if(request&&request->sequence==sequence&&!request->cancelled.load()&&!request->terminal.load())return;
    c->runningLossNotice.reset();
}
struct RunningLossRunReturn final {
    std::shared_ptr<Control> owner;Pending pending;
    ~RunningLossRunReturn(){
        if(pending.kind!=Trigger::RunningLost)return;
        try{std::lock_guard<std::mutex> lock(owner->mx);retireRunningLossLocked(owner,pending.sequence);}catch(...){}
    }
};
static void runOne(const std::shared_ptr<Control>& c,Pending pending,const json& config){
    RunningLossRunReturn returnRunningLoss{c,pending};
    if(pending.kind==Trigger::RunningLost&&!runningLossCurrent(c,pending))return;
    const auto sleepOriginal=(pending.kind==Trigger::Wake||pending.kind==Trigger::SuspendCapture)?SleepSteamRecovery::capturedFor(pending.generation):SleepSteamRecovery::Captured{};
    const auto resumed=pending.startupFresh;
    const auto commandResumed=pending.startupCommand;
    StartupCommandRunReturn commandSuffixReturn{c,commandResumed};
    if(resumed&&commandResumed)return;
    auto startupRequest=resumed?resumed->request:commandResumed?commandResumed->request:std::shared_ptr<StartupFreshRequest>{};
    StartupFreshRunReturn returnStartupFresh{c,startupRequest,resumed};
    if(resumed){
        if(!startupFreshCurrent(c,startupRequest,resumed))return;
        std::lock_guard<std::mutex> lock(c->mx);
        if(resumed->phase!=StartupFreshPhase::Queued)return;
        resumed->phase=StartupFreshPhase::Running;
    }
    if(commandResumed){
        if(!startupCommandCurrent(c,commandResumed))return;
        std::lock_guard<std::mutex> lock(c->mx);
        if(commandResumed->phase!=StartupCommandPhase::Queued)return;
        commandResumed->phase=StartupCommandPhase::Running;
    }
    const auto startingConfigEpoch=startupRequest?startupRequest->configEpoch:g_sleepRecoveryAutoConfigEpoch.load();
    const auto startingRulesEpoch=sleepOriginal.request?sleepOriginal.request->rulesEpoch:
        startupRequest?startupRequest->rulesEpoch:g_gameRulesEpoch.load();
    const auto startingAccount=startupRequest?startupRequest->accountCandidate:accountCandidate(SleepSteamRecovery::session().account);
    // Each route selects Startup only after binding the exact current pair.
    const auto operationPurpose=pending.kind==Trigger::Startup||pending.kind==Trigger::RunningLost||pending.kind==Trigger::Wake
        ?ymcc::steamnative::ReadPurpose::PrepareMutation:ymcc::steamnative::ReadPurpose::ObserveOriginal;
    const auto operationReadScope=startupRequest?startupRequest->operationReadScope:nativeSteamNativeReadScope(SleepSteamRecovery::session(),operationPurpose);
    if(!operationReadScope||!operationReadScope->current()){
        if(pending.kind!=Trigger::Wake&&pending.kind!=Trigger::SuspendCapture&&pending.kind!=Trigger::RunningLost){
            std::lock_guard<std::mutex> lock(c->mx);if(!c->stop)c->startupPending=true;
        }
        c->ownsRunning=false;publish(c,{{"phase","waiting"},{"reason","native-read-scope-unavailable"}});return;
    }
    if(!startupRequest)startupRequest=beginStartupFreshRequest(c,pending,startingConfigEpoch,startingRulesEpoch,startingAccount,operationReadScope);
    returnStartupFresh.request=startupRequest;
    json routes=json::array(),best;bool terminal=startupRequest&&startupRequest->terminal.load(),bestOwns=false;
    for(const auto route:{RecoveryRoute::LegacyDynamic,RecoveryRoute::SemiStatic}){
        const auto routeIndex=startupFreshRouteIndex(route);
        json observed;bool observedOwns=false;
        if(resumed&&route!=resumed->route){
            observed=startupRequest->routeObservations[routeIndex];observedOwns=startupRequest->routeOwns[routeIndex];
            if(!observed.is_object())continue;
        }else{
            Pending routePending=pending;
            if(commandResumed&&route!=commandResumed->route)routePending.startupCommand.reset();
            try{runRoute(c,routePending,config,route,!terminal,operationReadScope,startupRequest);}
            catch(const std::exception& error){publish(c,{{"phase","blocked"},{"reason",std::string(error.what()).substr(0,256)}});}
            {std::lock_guard<std::mutex> lock(c->snapshotMx);observed=c->snapshot;}
            observedOwns=c->ownsRunning.load();
            if(startupRequest){startupRequest->routeObservations[routeIndex]=observed;startupRequest->routeOwns[routeIndex]=observedOwns;}
        }
        json item={{"route",ymcc::steamOriginal::routeName(route)},{"phase",observed.value("phase",std::string{})},
            {"reason",observed.value("reason",std::string{})},{"engineDispatched",observed.value("engineDispatched",false)},
            {"result",observed.value("lastResult",json())},{"eventId",observed.value("eventId",json())},
            {"terminalEvidence",observed.value("terminalEvidence",json())},
            {"terminalEvidenceHistorical",observed.value("terminalEvidenceHistorical",false)},
            {"lastResultState",observed.value("lastResultState",std::string("unknown"))}};
        routes.push_back(item);
        const auto result=item["result"];const bool finished=result.is_object()&&
            (result.value("status",std::string{})=="healthy"||result.value("status",std::string{})=="recovered-native");
        const auto bestResult=best.is_object()?best.value("lastResult",json()):json();
        const auto previousStatus=bestResult.is_object()?bestResult.value("status",std::string{}):std::string{};
        const bool previousFinished=previousStatus=="healthy"||previousStatus=="recovered-native";
        const bool newlyRecovered=result.is_object()&&result.value("status",std::string{})=="recovered-native";
        if((finished&&(!previousFinished||(newlyRecovered&&previousStatus!="recovered-native")))||best.is_null()){
            best=observed;bestOwns=observedOwns;
        }
        // No automatic second publisher after any dispatched mutation, including
        // ambiguous/failed readback. No route can erase or refund the shared keys.
        terminal=terminal||finished||(result.is_object()&&result.value("consumed",false));
        if(terminal&&startupRequest){
            startupRequest->terminal.store(true,std::memory_order_release);
            CommandRetirement retiredCommand;
            std::lock_guard<std::mutex> lock(c->mx);
            for(const auto& wait:c->startupFreshWaits)if(wait&&wait->request==startupRequest)wait->subscription.cancel();
            if(c->startupCommandWait&&c->startupCommandWait->request==startupRequest)retireStartupCommandLocked(c,retiredCommand);
        }
    }
    // Actual native success or an uncertain consumed publication is terminal
    // for automatic writes, even when another route's reader is not ready.
    if(terminal&&pending.kind==Trigger::Wake){std::lock_guard<std::mutex> lock(c->mx);
        if(c->deferredWake==pending.generation)c->deferredWake=0;}
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
        bool finalCurrent=(!startupRequest||startupFreshScopeCurrent(c,startupRequest,resumed))&&
            operationReadScope->current()&&bestOwns&&finalBinding&&!g_exitRequested.load()&&g_sleepRecoveryAutoEnabled.load()&&
            (sleepOriginal.request?SleepSteamRecovery::originalRulesCurrent(sleepOriginal,startingRulesEpoch):
                g_gameRulesEpoch.load()==startingRulesEpoch)&&startingAccount&&accountCandidate(finalSteam.account)==startingAccount&&
            g_sleepRecoveryAutoConfigEpoch.load()==startingConfigEpoch&&g_powerLifecycle.load()==PowerLifecycle::Ready&&
            sharedSleepWakeClassified(pending.generation?pending.generation:currentPowerGeneration())&&
            chosenGame.is_object()&&chosenSteam.is_object()&&
            (sleepOriginal.request?(SleepSteamRecovery::originalTargetCurrent(sleepOriginal)&&
                (finalGame.pid=sleepOriginal.game.pid,finalGame.processCreated=sleepOriginal.game.created,finalGame.path=sleepOriginal.game.path,true)):
                nativeValveReadCurrentTarget(&finalGame,&finalTouch))&&
            matchesIdentity(asJson(identity(finalGame.pid,finalGame.processCreated,finalGame.path)),chosenGame)&&
            matchesIdentity(asJson(identity(finalSteam.pid,finalSteam.created,finalSteam.root+L"\\steam.exe")),chosenSteam)&&operationReadScope->current();
        if(sleepOriginal.request&&finalCurrent){
            const auto finalGameId=chosenGame.value("gameId",std::string{});
            const auto finalAppId=chosenGame.value("appid",std::uint32_t{});
            const bool committed=SleepSteamRecovery::withOriginalTargetPublication(sleepOriginal,[&]{
                std::lock_guard<std::mutex> lock(c->mx);
                finalCurrent=finalCurrent&&!c->stop&&c->bindingQualified&&c->boundSleepRequest==sleepOriginal.request&&
                    c->boundGameId==finalGameId&&c->boundAppId==finalAppId&&
                    (!pending.generation||c->latestPower<=pending.generation)&&
                    g_sleepRecoveryAutoConfigEpoch.load()==startingConfigEpoch&&g_sleepRecoveryAutoEnabled.load()&&
                    !g_exitRequested.load()&&g_powerLifecycle.load()==PowerLifecycle::Ready;
                c->ownsRunning=finalCurrent;
            });
            if(!committed){finalCurrent=false;c->ownsRunning=false;}
        }else c->ownsRunning=finalCurrent;
        if(bestOwns&&!finalCurrent){best["currentBindingValid"]=false;best["nativeRunning"]=nullptr;
            best["lastResultHistorical"]=true;best["phase"]="cancelled";best["reason"]="scope-changed-after-dual-route";}
        if(best.contains("lastResult")&&best["lastResult"].is_object())
            saveJson(root()+L"\\last-recovery-summary.json",best["lastResult"]);
        best.erase("routeTerminals"); // Keep both latest diagnostic slots, not the earlier best snapshot.
        publish(c,best);
    }
}
// Single existing worker only. All queue inspection remains under the original lock.
// Any urgent item releases its FIFO predecessors; no reorder or suffix delay.
static bool takeNext(const std::shared_ptr<Control>& c,Pending& next){
    std::unique_lock<std::mutex> lock(c->mx);
    for(;;){
        c->cv.wait(lock,[&]{return c->stop||!c->pending.empty()||startupCommandNotifiedLocked(c);});
        if(c->stop)return false;
        if(c->pending.empty()){
            lock.unlock();const bool ready=takeReadyStartupCommand(c,next);lock.lock();
            if(ready)return true;
            continue;
        }
        const auto& front=c->pending.front();
        bool urgent=false;
        for(const auto& item:c->pending)if(item.kind!=Trigger::TargetChanged||item.startupFresh){urgent=true;break;}
        if(front.kind==Trigger::TargetChanged&&!front.startupFresh&&front.notBeforeTick&&!urgent){
            const auto now=GetTickCount64();
            if(now<front.notBeforeTick){
                c->cv.wait_for(lock,std::chrono::milliseconds(front.notBeforeTick-now));
                continue; // Spurious/ordinary wakes never refresh the original deadline.
            }
        }
        next=front;c->pending.pop_front();return true;
    }
}
static void run(const std::shared_ptr<Control>& c){
    ymcc::threadRole::describe(L"YMCC.SteamRecovery");
    c->enabled=false;c->ownsRunning=false;c->active=false;json config;
    try{
        const auto storage=ymcc::steamOriginalStorage::prepare(fspath::path(root()),fspath::path(runtimeRoot()));
        log(c,"storage-ready",{{"layout",2},{"legacyImported",storage.legacyImported},
            {"filesCopied",storage.filesCopied},{"filesAlreadyPresent",storage.filesAlreadyPresent},
            {"evidenceRoot",W2U(root())},{"runtimeRoot",W2U(runtimeRoot())}});
        const auto local=readJson(runtimeRoot()+L"\\enabled.json");
        config=ymcc::steamOriginal::configuration(local.value,local.exists,local.lookupError);
        c->enabled=config.at("enabled").get<bool>();
    }catch(const std::exception& error){
        c->enabled=false;c->ownsRunning=false;c->active=false;
        try{publish(c,{{"phase","blocked"},{"reason",std::string(error.what()).substr(0,160)},
            {"currentBindingValid",false},{"nativeRunning",nullptr},{"lastResultHistorical",true}});}catch(...){}
        return; // No recovery or ledger writes after an ambiguous configuration/storage read.
    }catch(...){
        c->enabled=false;c->ownsRunning=false;c->active=false;
        try{publish(c,{{"phase","blocked"},{"reason","configuration-storage-exception"},
            {"currentBindingValid",false},{"nativeRunning",nullptr},{"lastResultHistorical",true}});}catch(...){}
        return;
    }
    try{publish(c,{{"phase","waiting-events"},{"reason","ymcc-event-owner-started"},
        {"configurationSource",config.value("source",std::string("local-override"))},
        {"targetMode",config.value("targetMode",std::string("generic"))}});}catch(...){}
    for(;;){Pending next;
        if(!takeNext(c,next))break;
        if(!next.startupFresh&&!next.startupCommand){
            RetiredStartupFresh retired;
            {std::lock_guard<std::mutex> lock(c->mx);retired=retireStartupFreshLocked(c);}
            // Destruction happens on this existing worker with no consumer lock.
        }
        c->active=true;
        try{runOne(c,next,config);}catch(const std::exception& e){try{publish(c,{{"phase","blocked"},{"reason",std::string(e.what()).substr(0,256)}});}catch(...){} }
        catch(...){try{publish(c,{{"phase","blocked"},{"reason","native-engine-exception"}});}catch(...){} }
        c->active=false;c->cv.notify_all();
    }
    c->ownsRunning=false;try{publish(c,{{"phase","stopped"},{"reason","ymcc-owner-stopped"}});}catch(...){}
}
// Called only after the launched worker has returned through run/catch.
// Close this same owner's admission; original Shutdown/final ownership still
// retires its pending work and resources after the actual thread is joined.
static void finishWorkerOwner(const std::shared_ptr<Control>& c){
    CommandRetirement retiredCommand;
    {std::lock_guard<std::mutex> lock(c->mx);
        cancelGroupRequestLocked(c);
        cancelStartupFreshLocked(c,retiredCommand);
        c->stop=true;c->enabled=false;c->ownsRunning=false;c->active=false;}
    c->cv.notify_all();
    if(c->observationEvent)SetEvent(c->observationEvent.get());
} // Command captures return outside the original consumer lock.
static json get(){const auto c=current();if(!c)return {{"phase","not-started"},{"owner","ymcc-native"}};
    std::lock_guard<std::mutex> lock(c->snapshotMx);return c->snapshot;}
}
// The writer already owns its original rules/config/selection publication lock.
// Registration and the memory commit cannot interleave between cancel and publish.
static void steamOriginalRecoveryBusinessCommit(bool globalSelectionOnly,const std::function<void()>& publication){
    const auto c=SteamOriginalRecovery::current();
    SteamOriginalRecovery::CommandRetirement retiredCommand;
    SleepSteamRecovery::originalTargetCommit(globalSelectionOnly,[&]{
        if(!c){publication();return;}
        std::lock_guard<std::mutex> lock(c->mx);
        SteamOriginalRecovery::cancelGroupRequestLocked(c,globalSelectionOnly);
        SteamOriginalRecovery::cancelStartupFreshLocked(c,retiredCommand);
        publication();
    });
}
// Shared rules already own rulesMx. The existing sleep request decides
// continuity before the runtime memory publication; no owner lookup under
// captureMx/c->mx and no change to event/admission/durable publisher budgets.
static void steamOriginalRecoveryRulesCommit(const NativeGameRulesSnapshot& next,
    unsigned long long previous,unsigned long long following,const std::function<void()>& publication){
    const auto c=SteamOriginalRecovery::current();
    SteamOriginalRecovery::CommandRetirement retiredCommand;
    SleepSteamRecovery::originalRulesCommit(next,previous,following,[&](bool retained){
        if(!c){publication();return;}
        std::lock_guard<std::mutex> lock(c->mx);
        const bool boundOriginalContinues=retained&&c->boundSleepRequest==SleepSteamRecovery::captureRequest;
        SteamOriginalRecovery::cancelGroupRequestLocked(c,boundOriginalContinues);
        if(!boundOriginalContinues){c->bindingQualified=false;c->ownsRunning=false;}
        SteamOriginalRecovery::cancelStartupFreshLocked(c,retiredCommand);
        publication();
    });
}
static void steamOriginalRecoveryStart(){
    std::shared_ptr<SteamOriginalRecovery::Control> c;
    bool failed=false;
    {std::lock_guard<std::mutex> lock(SteamOriginalRecovery::lifecycleMx);
        if(SteamOriginalRecovery::control||g_exitRequested.load(std::memory_order_acquire))return;
        try{
            c=std::make_shared<SteamOriginalRecovery::Control>();SteamOriginalRecovery::control=c;
            try{
                c->mutationAvailability=std::make_shared<ymcc::steamcommand::AvailabilityEvent>();
                (void)c->mutationAvailability->open(); // Retained before OS creation.
            }catch(...){/* This optional signal must not strand the original worker startup. */}
            c->observationEvent=std::shared_ptr<void>(CreateEventW(nullptr,FALSE,FALSE,nullptr),[](void* event){if(event)CloseHandle(event);});
            SteamOriginalRecovery::worker.start([c]{
                try{SteamOriginalRecovery::run(c);}
                catch(...){c->active=false;c->ownsRunning=false;c->enabled=false;
                    try{SteamOriginalRecovery::publish(c,{{"phase","stopped"},{"reason","worker-exception"}});}catch(...){} }
                SteamOriginalRecovery::finishWorkerOwner(c);
            });
        }catch(...){
            // No successful RecoveryWorker launch exists. Keep the published
            // Control and its resources for the original Shutdown/final owner.
            // A stopped owner cannot accept an event into a workerless queue.
            failed=true;
            if(c){std::lock_guard<std::mutex> guard(c->mx);
                c->stop=true;c->enabled=false;c->ownsRunning=false;c->active=false;}
        }}
    if(failed){
        if(c)try{SteamOriginalRecovery::publish(c,{{"phase","blocked"},{"reason","worker-start-failed"},
            {"currentBindingValid",false},{"nativeRunning",nullptr},{"lastResultHistorical",true}});}catch(...){}
        return;
    }
    try{SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::Startup);}
    catch(const std::bad_alloc&){
        // The worker was launched, but its first event was not admitted.
        // Stop this exact owner; retained work/resources still use ordinary
        // worker cleanup and Shutdown. No retry or replacement owner is minted.
        {
            SteamOriginalRecovery::CommandRetirement retiredCommand;
            {std::lock_guard<std::mutex> guard(c->mx);
                SteamOriginalRecovery::cancelGroupRequestLocked(c);
                SteamOriginalRecovery::cancelStartupFreshLocked(c,retiredCommand);
                c->stop=true;c->enabled=false;c->ownsRunning=false;}
            c->cv.notify_all();
            if(c->observationEvent)SetEvent(c->observationEvent.get());
        } // Command captures return outside the original consumer lock.
        try{SteamOriginalRecovery::publish(c,{{"phase","blocked"},{"reason","worker-initial-event-failed"},
            {"initialEventEnqueueFailed",true},{"currentBindingValid",false},
            {"nativeRunning",nullptr},{"lastResultHistorical",true}});}catch(...){}
    }
}
static void steamOriginalRecoveryTargetChanged(DWORD pid,ULONGLONG birth,const std::wstring& path,unsigned long long rules){
    const auto c=SteamOriginalRecovery::current();if(!c)return;
    bool sameObserved=false,resumeDeferred=false;
    {std::lock_guard<std::mutex> lock(c->mx);
        if(c->targetObserved&&c->observedTarget.pid==pid&&c->observedTarget.birth==birth&&
            _wcsicmp(c->observedTarget.path.c_str(),path.c_str())==0){
            sameObserved=true;resumeDeferred=!c->stop&&pid&&birth&&c->observedRules!=rules&&c->deferredWake!=0;
            c->observedRules=rules;}}
    if(sameObserved){if(resumeDeferred)steamOriginalRecoveryPowerReady(currentPowerGeneration());return;}
    const auto next=SteamOriginalRecovery::identity(pid,birth,path);
    {std::lock_guard<std::mutex> lock(c->mx);if(c->stop)return;
        if(c->targetObserved&&c->observedTarget==next){sameObserved=true;resumeDeferred=c->observedRules!=rules&&c->deferredWake!=0;c->observedRules=rules;}
        else{c->targetObserved=true;c->observedTarget=next;c->observedRules=rules;if(!c->boundSleepRequest)c->ownsRunning=false;}}
    if(sameObserved){if(resumeDeferred)steamOriginalRecoveryPowerReady(currentPowerGeneration());return;}
    // Discovery can finish after the admitted wake first found no target.
    // Resume only that deferred event, before the ordinary observation.
    if(next.valid())steamOriginalRecoveryPowerReady(currentPowerGeneration());
    SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::TargetChanged);
}
static void steamOriginalRecoverySteamChanged(){
    const auto c=SteamOriginalRecovery::current();if(!c)return;
    const auto session=g_steamSettingsObserver.sessionSnapshot();
    const auto next=SteamOriginalRecovery::identity(session.pid,session.created,session.root+L"\\steam.exe");
    {std::lock_guard<std::mutex> lock(c->mx);if(c->steamObserved&&c->observedSteam==next)return;
        // Advisory session notice: the shared identity event revokes old scopes.
        c->steamObserved=true;c->observedSteam=next;if(!(c->boundSteam==next))c->ownsRunning=false;}
    SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::SteamChanged);
}
static void steamOriginalRecoverySuspend(unsigned long long generation){SteamOriginalRecovery::enqueue(SteamOriginalRecovery::current(),ymcc::steamOriginal::Trigger::SuspendCapture,generation);}
static void steamOriginalRecoveryWake(unsigned long long generation){
    const auto c=SteamOriginalRecovery::current();if(!c||!generation)return;
    {std::lock_guard<std::mutex> lock(c->mx);if(c->stop||generation<c->latestPower)return;
        c->latestPower=generation;c->deferredWake=generation;}
    if(g_powerLifecycle.load()==PowerLifecycle::Ready&&sharedSleepWakeClassified(generation))SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::Wake,generation);
}
static void steamOriginalRecoveryPowerReady(unsigned long long generation){
    const auto c=SteamOriginalRecovery::current();if(!c||!generation||g_powerLifecycle.load()!=PowerLifecycle::Ready||!sharedSleepWakeClassified(generation))return;
    unsigned long long pending=0;bool startup=false;
    {std::lock_guard<std::mutex> lock(c->mx);if(!c->stop){
        if(c->deferredWake==generation&&c->latestPower==generation)pending=generation;
        else startup=c->startupPending.load(std::memory_order_acquire)&&c->enabled.load(std::memory_order_acquire)&&generation==currentPowerGeneration();
    }}
    if(pending)SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::Wake,pending);
    // Existing runOne alone converts this ordinary event to Startup. A consumed
    // Startup keeps its original ledger entry; Ready grants no new attempt.
    else if(startup&&!SteamOriginalRecovery::currentStartupContinuation(c))
        SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::TargetChanged);
}
static void steamOriginalRecoveryTrackerObserved(DWORD gamePid,ULONGLONG gameBirth,DWORD steamPid,ULONGLONG steamBirth,
    const std::string& gameId,std::uint32_t appid,bool exact,bool definitivelyAbsent=false){
    // Unknown/non-tracked reads alone cannot contradict a held original.
    // A complete explicit removal still revokes through the existing path.
    if(!exact&&!definitivelyAbsent)return;
    const auto c=SteamOriginalRecovery::current();if(!c)return;
    SteamOriginalRecovery::CommandRetirement retiredCommand;
    std::lock_guard<std::mutex> lock(c->mx);
    const bool same=c->boundGame.pid==gamePid&&c->boundGame.birth==gameBirth&&
        c->boundSteam.pid==steamPid&&c->boundSteam.birth==steamBirth;
    if(same&&c->bindingQualified&&exact&&c->boundGameId==gameId&&c->boundAppId==appid)return;
    // Another global selection cannot revoke the original sleep target.
    // Its own exact binding failures and public process/account events still revoke.
    if(!same&&c->boundSleepRequest)return;
    if(!same&&!c->active.load())return;
    // This event can revoke only. It cannot elect a PID, replace GameID, queue
    // a mutation, or refund an already consumed observation/dispatch.
    SteamOriginalRecovery::cancelGroupRequestLocked(c);
    SteamOriginalRecovery::retireStartupCommandLocked(c,retiredCommand);
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
// Advisory from one already completed ordinary Steam UI read. Native flags,
// reference/record/roles, account and action rights are re-read by the worker.
static void steamOriginalRecoveryRunningLost(DWORD gamePid,ULONGLONG gameBirth,DWORD steamPid,ULONGLONG steamBirth,
    const std::string& gameId,std::uint32_t appid,ULONGLONG configEpoch,ULONGLONG rulesEpoch){
    try{
        const auto c=SteamOriginalRecovery::current();if(!c||!c->enabled.load())return;
        if(c->active.load()){steamOriginalRecoveryObserveHint();return;}
        if(SteamOriginalRecovery::currentStartupContinuation(c))return;
        if(!appid||gameId!=std::to_string((std::uint64_t(appid)<<32)|0x02000000ULL)||
            g_exitRequested.load()||!g_sleepRecoveryAutoEnabled.load()||g_powerLifecycle.load()!=PowerLifecycle::Ready||
            !configEpoch||g_sleepRecoveryAutoConfigEpoch.load()!=configEpoch||!rulesEpoch||g_gameRulesEpoch.load()!=rulesEpoch)return;
        NativeGameSelectionReceipt selected;
        if(!nativeValveReadCachedSelection(&selected)||!nativeValveSelectionCurrent(selected)||
            selected.target.game.pid!=gamePid||selected.target.game.processCreated!=gameBirth||
            selected.target.rulesEpoch!=rulesEpoch)return;
        SteamOriginalRecovery::CommandRetirement retired;
        {std::lock_guard<std::mutex> lock(c->mx);
            if(c->stop||c->active.load()||!c->bindingQualified||c->sequence==ULLONG_MAX||
                c->boundGame.pid!=gamePid||c->boundGame.birth!=gameBirth||
                c->boundSteam.pid!=steamPid||c->boundSteam.birth!=steamBirth||
                _wcsicmp(c->boundGame.path.c_str(),selected.target.game.path.c_str())!=0||
                c->boundGameId!=gameId||c->boundAppId!=appid||
                c->pending.size()>=16)return;
            // observedPair reads only the exact Steam/game keys; no parent is invented.
            const SteamOriginalRecovery::Scope scope{{},c->boundGame,c->boundSteam,rulesEpoch};
            const ymcc::steamOriginal::Event event{ymcc::steamOriginal::Trigger::RunningLost,0,c->sequence+1};
            if(c->legacyAdmission.observedPair(scope,event.trigger)&&c->admission.observedPair(scope,event.trigger))return;
            if(c->runningLossNotice)SteamOriginalRecovery::retireRunningLossLocked(c,c->runningLossNotice->sequence);
            if(c->runningLossNotice&&c->runningLossNotice->game==scope.game&&
                c->runningLossNotice->steam==scope.steam&&c->runningLossNotice->gameId==gameId)return;
            for(const auto& pending:c->pending)if(pending.kind==ymcc::steamOriginal::Trigger::RunningLost)return;
            SteamOriginalRecovery::RunningLossNotice notice{event.sequence,configEpoch,rulesEpoch,selected.powerGeneration,
                c->boundGame,c->boundSteam,gameId,appid,std::move(selected)};
            c->pending.push_back({ymcc::steamOriginal::Trigger::RunningLost,0,event.sequence});
            c->runningLossNotice=std::move(notice);c->sequence=event.sequence;
            SteamOriginalRecovery::cancelStartupFreshLocked(c,retired);
            if(c->observationSequence!=ULLONG_MAX)++c->observationSequence;
        }
        c->cv.notify_all();if(c->observationEvent)SetEvent(c->observationEvent.get());
    }catch(...){ /* Optional hint cannot escape into the ordinary reader. */ }
}
static void steamOriginalRecoveryBindingReady(){
    const auto c=SteamOriginalRecovery::current();if(!c)return;
    steamOriginalRecoveryPowerReady(currentPowerGeneration());
    // Existing Steam window/session/binding changes can complete a first
    // healthy observation, but cannot grant another consumed recovery action.
    if(c->enabled.load()&&!c->ownsRunning.load()&&!SteamOriginalRecovery::currentStartupContinuation(c))
        SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::TargetChanged);
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
    if(SteamOriginalRecovery::currentStartupContinuation(c))return;
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
    SleepSteamRecovery::originalTargetCommit(false,[]{});
    const auto c=SteamOriginalRecovery::current();if(!c)return;
    std::deque<SteamOriginalRecovery::Pending> abandoned;
    SteamOriginalRecovery::CommandRetirement retiredCommand;
    {std::lock_guard<std::mutex> lock(c->mx);SteamOriginalRecovery::cancelGroupRequestLocked(c);SteamOriginalRecovery::cancelStartupFreshLocked(c,retiredCommand);c->stop=true;abandoned.swap(c->pending);}c->cv.notify_all();
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
    const auto c=SteamOriginalRecovery::current();
    SteamOriginalRecovery::RetiredStartupFresh retired;
    if(c){std::lock_guard<std::mutex> lock(c->mx);retired=SteamOriginalRecovery::retireStartupFreshLocked(c);}
    if(c&&c->mutationAvailability)(void)c->mutationAvailability->close(); // After join; refused close stays in original Control.
}
static std::wstring steamOriginalRecoveryExportSnapshot(){
    const auto file=SteamOriginalRecovery::root()+L"\\current-export.json";
    try{SteamOriginalRecovery::saveJson(file,SteamOriginalRecovery::get());return file;}catch(...){return {};}
}
