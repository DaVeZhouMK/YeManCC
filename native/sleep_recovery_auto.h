#pragma once
#include "sleep_steam_settle.h"
#include "sleep_steam_command_attempt.h"
#include "sleep_wake_admission.h"
#include <functional>
// Steam-only event-driven automatic owner. No InputHost/fan gates or actions.
// Protected UI is positively observed before spending a RETURN; no focus override.
// Only an explicit deduplicated wake arms it; bounded waits reuse sgWorkLoop.
namespace SleepRecoveryAuto {
using namespace ymcc::sleeprecovery;
struct State {
    unsigned long long generation=0,configEpoch=0,seenGeneration=0,seenBeforeAdmission=0;
    ULONGLONG armed=0,due=0,stageSince=0,wakeCandidateAt=0;
    ymcc::sleeppower::WakeContinuation wakeContinuation;
    std::string phase="idle",reason;
    json before,last,receipt,steamStatus,inputStatus,wakeAdmission,wakeReadiness,associationHistory,secondaryRejection,timeline=json::array();
    bool listAttempted=false,inputAttempted=false;unsigned inputDispatches=0;json preflightRejections=json::array();
    bool commandInFlight=false;json commandWait;
    ymcc::sleepsteam::SettleWindow settling;
    unsigned postReturnForegroundSamples=0,initialWindowReadbackSamples=0;bool postReturnForegroundWait=false,postReturnDelayedWindowObserved=false,initialWindowReadbackWait=false;
    json postReturnObservationSummary() const {return {{"foregroundDeferredSamples",postReturnForegroundSamples},
        {"waitingOriginalForeground",postReturnForegroundWait},{"delayedOriginalWindowObserved",postReturnDelayedWindowObserved},
        {"waitingInitialOriginalWindow",initialWindowReadbackWait},{"initialWindowReadbackSamples",initialWindowReadbackSamples},{"returnReplayAllowed",false},
        {"gameInputVerified",false},{"continueButtonVerified",false}};}
};
static std::mutex& mx=ymcc::sleeppower::nativeWakeFacts.mutex;
static std::mutex reportMx;
static std::timed_mutex setMx;
static State state;
static ymcc::sleeppower::WakeProjection sharedWakeFactsLocked(){
    const auto generation=currentPowerGeneration(),epoch=g_sleepRecoveryAutoConfigEpoch.load();
    const auto original=SleepSteamRecovery::capturedFor(generation);
    return ymcc::sleeppower::nativeWakeFacts.snapshotForLocked(generation,epoch,original.powerBoundaryFileTime,original.boundaryGeneration);
}
static bool followsWakeBoundary(unsigned long long observed,unsigned long long gen,unsigned long long boundaryGeneration) {
    return ymcc::sleeppower::followsWakeBoundary(observed,gen,boundaryGeneration);
}
// Diagnostic output only: tags name evaluated branches, never unvisited subconditions.
// Optional callers retain the same guards, short circuit order, clocks and result.
struct WakeOperationObservation {
    const char* rejectedGuard=nullptr;
    unsigned long long generation=0,configEpoch=0;
    ULONGLONG proofNowFileTime=0,interactiveFileTime=0,exitFileTime=0,proofLimitMs=0;
    ULONGLONG nowTick=0,armedTick=0,kernelEventFileTime=0,kernelProofNowFileTime=0;
    int sharedPhase=0;
    bool interactiveProofEvaluated=false,kernelProofEvaluated=false,sharedPhaseEvaluated=false;
};
static bool completedInteractiveResumeLocked(unsigned long long gen,unsigned long long epoch,ULONGLONG boundary,unsigned long long boundaryGeneration,WakeOperationObservation* observation=nullptr) {
        const auto native=sharedWakeFactsLocked();
        const auto& kernelWake=native.kernelWake;const auto& kernelSleepEntry=native.kernelSleepEntry;
        const auto& interactiveResume=native.interactiveResume;const auto& completedResume=native.completedResume;const auto& readyCommit=native.readyCommit;

    if(observation){observation->generation=gen;observation->configEpoch=epoch;observation->rejectedGuard="interactive-identity-ready-or-retry";}
    const auto utc=sgNowFileTime();
    const ymcc::sleeppower::CompletedWake joinedNative{gen,epoch,boundary,boundaryGeneration,
        readyCommit.generation,readyCommit.configEpoch,readyCommit.boundaryFileTime,
        interactiveResume.generation,interactiveResume.configEpoch,interactiveResume.boundaryFileTime,interactiveResume.eventFileTime,
        completedResume.generation,completedResume.configEpoch,completedResume.boundaryFileTime,completedResume.eventFileTime,
        kernelSleepEntry.configEpoch==epoch?kernelSleepEntry.eventFileTime:0,utc,ymcc::sleepsteam::kWakeObserveLimitMs,completedResume.eventId,
        sleepSteamRecoveryAllows(gen,epoch),sgSleepRetryActive(),g_powerLifecycle.load()==PowerLifecycle::Ready};
    const auto result=ymcc::sleeppower::completedWake(joinedNative);
    if(observation&&result!=ymcc::sleeppower::CompletedWakeResult::Context){
        observation->interactiveProofEvaluated=true;observation->proofNowFileTime=utc;
        observation->interactiveFileTime=joinedNative.interactiveAt;observation->exitFileTime=joinedNative.completedAt;
        observation->proofLimitMs=joinedNative.limitMs;
        observation->rejectedGuard=result==ymcc::sleeppower::CompletedWakeResult::Window?"interactive-proof-window":
            result==ymcc::sleeppower::CompletedWakeResult::Kind?"completed-exit-kind-or-window":
            result==ymcc::sleeppower::CompletedWakeResult::Gap?"interactive-exit-gap":nullptr;
    }
    return result==ymcc::sleeppower::CompletedWakeResult::Allowed;
}
static void recordInteractiveResume(unsigned long long gen,int code,ULONGLONG receiptTick) {
    // Native ordered PBT producer owns recording. This consumer only receives
    // its existing notification; it never grants another sleep interpretation.
    (void)gen;(void)code;(void)receiptTick;
}
static void recordCompletedResume(unsigned long long gen,int eventId,int reason,int targetState,ULONGLONG at) {
    (void)gen;(void)eventId;(void)reason;(void)targetState;(void)at;
}
static bool proofSupersededLocked() {
        const auto native=sharedWakeFactsLocked();
        const auto& kernelWake=native.kernelWake;const auto& kernelSleepEntry=native.kernelSleepEntry;
        const auto& interactiveResume=native.interactiveResume;const auto& completedResume=native.completedResume;const auto& readyCommit=native.readyCommit;

    const auto event=state.wakeAdmission.value("eventFileTime",json());std::uint64_t at=0;
    const bool timed=event.is_string()&&ymcc::sleepsteam::unsignedDecimal(event.get<std::string>(),at);
    if(timed&&flag(state.wakeAdmission,"interactiveWakeConfirmed")&&
        state.wakeAdmission.value("source",std::string{})=="interactive-resume-committed-ready"){
        std::uint64_t completed=0,boundary=0;
        const auto end=state.wakeAdmission.value("completedEventFileTime",json()),begin=state.wakeAdmission.value("boundaryFileTime",json());
        // Only the original admitted completion and its current native tuple
        // can close an entry. JSON, a fresh unrelated exit or Ready alone cannot.
        if(end.is_string()&&begin.is_string()&&ymcc::sleepsteam::unsignedDecimal(end.get<std::string>(),completed)&&
            ymcc::sleepsteam::unsignedDecimal(begin.get<std::string>(),boundary)&&completed>at&&
            completedResume.eventFileTime==completed&&completedResume.configEpoch==state.configEpoch&&
            completedResume.boundaryFileTime==boundary&&
            followsWakeBoundary(completedResume.generation,state.generation,number(state.wakeAdmission,"boundaryGeneration"))&&
            (completedResume.eventId==507||completedResume.eventId==107))at=completed;
    }
    return (flag(state.wakeAdmission,"kernelWakeConfirmed")||flag(state.wakeAdmission,"interactiveWakeConfirmed"))&&timed&&
        kernelSleepEntry.configEpoch==state.configEpoch&&kernelSleepEntry.eventFileTime&&at<=kernelSleepEntry.eventFileTime;
}

// Shared Resuming is NOT OS sleep. ROG can receive a valid final exit after
// the PBT Ready commit and be left Resuming by a duplicate shared wake. Steam
// owns no hardware writes and must not reopen that shared transaction. Allow
// its existing bounded operation only under its EXACT already-admitted final
// Kernel witness; no raw/PBT/intermediate/old proof grants this alternative.
static bool wakeOperationAllowedLocked(unsigned long long gen,unsigned long long epoch,WakeOperationObservation* observation=nullptr) {
        const auto native=sharedWakeFactsLocked();
        const auto& kernelWake=native.kernelWake;const auto& kernelSleepEntry=native.kernelSleepEntry;
        const auto& interactiveResume=native.interactiveResume;const auto& completedResume=native.completedResume;const auto& readyCommit=native.readyCommit;

    if(observation){*observation=WakeOperationObservation{};observation->generation=gen;observation->configEpoch=epoch;
        observation->rejectedGuard="wake-classification-consumer-or-retry";}
    if(!sharedSleepWakeClassified(gen)||!sleepSteamRecoveryAllows(gen,epoch)||sgSleepRetryActive())return false;
    // A successor proof never extends the retired proof's original UTC limit.
    if(state.wakeContinuation.retiredEvent()&&!state.wakeContinuation.operationCurrent(sgNowFileTime()))return false;
    if(state.wakeAdmission.is_object()&&state.wakeAdmission.value("source",std::string())=="interactive-resume-committed-ready"){
        std::uint64_t boundary=0;const auto value=state.wakeAdmission.value("boundaryFileTime",json());
        if(observation)observation->rejectedGuard="interactive-admission-identity-or-boundary";
        if(state.generation!=gen||state.configEpoch!=epoch||proofSupersededLocked()||!value.is_string()||
            !ymcc::sleepsteam::unsignedDecimal(value.get<std::string>(),boundary)||
            !completedInteractiveResumeLocked(gen,epoch,boundary,number(state.wakeAdmission,"boundaryGeneration"),observation))return false;
    }
    const auto phase=g_powerLifecycle.load();
    if(observation){observation->sharedPhaseEvaluated=true;observation->sharedPhase=static_cast<int>(phase);}
    if(phase==PowerLifecycle::Ready){
        // An original 506 can reach the public owner before the window leaves Ready.
        if(state.generation==gen&&state.configEpoch==epoch&&flag(state.wakeAdmission,"kernelWakeConfirmed")&&proofSupersededLocked()){
            if(observation)observation->rejectedGuard="ready-kernel-proof-superseded";
            return false;
        }
        if(observation)observation->rejectedGuard=nullptr;
        return true;
    }
    if(observation)observation->rejectedGuard="resuming-admission-or-superseded";
    if(phase!=PowerLifecycle::Resuming||!g_sleepRecoveryAutoPending.load()||state.generation!=gen||state.configEpoch!=epoch||
        !flag(state.wakeAdmission,"kernelWakeConfirmed")||!flag(state.wakeAdmission,"proofFresh")||
        state.wakeAdmission.value("status",std::string())!="admitted"||proofSupersededLocked())return false;
    const auto source=state.wakeAdmission.value("source",std::string());
    if(observation)observation->rejectedGuard="kernel-source";
    if(source!="kernel-power-507-reason-1"&&source!="kernel-power-107-target-5")return false;
    std::uint64_t event=0,boundary=0;const auto at=state.wakeAdmission.value("eventFileTime",json()),sleep=state.wakeAdmission.value("boundaryFileTime",json());
    if(observation)observation->rejectedGuard="kernel-admission-boundary";
    if(!at.is_string()||!sleep.is_string()||!ymcc::sleepsteam::unsignedDecimal(at.get<std::string>(),event)||
        !ymcc::sleepsteam::unsignedDecimal(sleep.get<std::string>(),boundary)||!boundary||event<=boundary)return false;
    // Keep the native witness in the join, not only exported admission JSON.
    if(observation)observation->rejectedGuard="kernel-native-witness";
    if(kernelWake.configEpoch!=epoch||kernelWake.eventFileTime!=event||!kernelWake.uiWakeRequested||kernelWake.requestGeneration!=gen||
        kernelWake.generation!=number(state.wakeAdmission,"proofObservedGeneration")||
        (kernelWake.boundaryFileTime&&kernelWake.boundaryFileTime!=boundary)||
        (source=="kernel-power-507-reason-1"?kernelWake.eventId!=507:kernelWake.eventId!=107))return false;
    const auto now=GetTickCount64(),utc=sgNowFileTime();
    if(observation){observation->kernelProofEvaluated=true;observation->nowTick=now;observation->armedTick=state.armed;
        observation->kernelProofNowFileTime=utc;observation->kernelEventFileTime=event;observation->proofLimitMs=ymcc::sleepsteam::kWakeObserveLimitMs;}
    const bool allowed=now>=state.armed&&now-state.armed<ymcc::sleepsteam::kWakeObserveLimitMs&&event<=utc&&
        utc-event<=ymcc::sleepsteam::kWakeObserveLimitMs*10000ULL;
    if(observation)observation->rejectedGuard=allowed?nullptr:"kernel-operation-or-proof-window";
    return allowed;
}
static bool wakeOperationAllows(unsigned long long gen,unsigned long long epoch) {
    std::lock_guard<std::mutex> lock(mx);return wakeOperationAllowedLocked(gen,epoch);
}
static json wakeReadinessLocked(unsigned long long gen,unsigned long long epoch) {
    WakeOperationObservation observation;
    const auto phase=g_powerLifecycle.load();const bool allowed=wakeOperationAllowedLocked(gen,epoch,&observation),sharedReady=phase==PowerLifecycle::Ready;
    return {{"operationAllowed",allowed},{"rejectedGuard",observation.rejectedGuard?json(observation.rejectedGuard):json()},
        {"consumerObservation",{{"diagnosticOnly",true},{"authority",false},
            {"generation",observation.generation},{"configEpoch",observation.configEpoch},
            {"sharedPhaseEvaluated",observation.sharedPhaseEvaluated},
            {"sharedPhase",observation.sharedPhaseEvaluated?json(observation.sharedPhase):json()},
            {"interactiveProofEvaluated",observation.interactiveProofEvaluated},{"kernelProofEvaluated",observation.kernelProofEvaluated},
            {"proofNowFileTime",observation.interactiveProofEvaluated?json(std::to_string(observation.proofNowFileTime)):observation.kernelProofEvaluated?json(std::to_string(observation.kernelProofNowFileTime)):json()},
            {"kernelProofNowFileTime",observation.kernelProofEvaluated?json(std::to_string(observation.kernelProofNowFileTime)):json()},
            {"interactiveFileTime",observation.interactiveProofEvaluated?json(std::to_string(observation.interactiveFileTime)):json()},
            {"exitFileTime",observation.interactiveProofEvaluated?json(std::to_string(observation.exitFileTime)):json()},
            {"kernelEventFileTime",observation.kernelProofEvaluated?json(std::to_string(observation.kernelEventFileTime)):json()},
            {"nowTick",observation.kernelProofEvaluated?json(observation.nowTick):json()},
            {"armedTick",observation.kernelProofEvaluated?json(observation.armedTick):json()},
            {"proofLimitMs",observation.interactiveProofEvaluated||observation.kernelProofEvaluated?json(observation.proofLimitMs):json()}}},
        {"basis",allowed?sharedReady?"shared-ready":"bound-final-kernel-wake-during-resuming":"waiting-valid-steam-wake"},
        {"sharedPowerLifecycle",sharedReady?"ready":phase==PowerLifecycle::Resuming?"resuming":phase==PowerLifecycle::Suspending?"suspending":"suspended"},
        {"sharedReady",sharedReady},{"finalKernelWakeAdmitted",flag(state.wakeAdmission,"kernelWakeConfirmed")},
        {"generation",gen},{"configEpoch",epoch},{"sharedStateModified",false},{"inputDeviceGateUsed",false},
        {"gameInputVerified",false},{"continueButtonVerified",false}};
}
static unsigned pidWindowAttempts(const json& receipt){
    if(!receipt.is_object())return 0;
    const auto input=receipt.value("inputResume",json::object());return input.is_object()&&flag(input.value("pidWindowActivation",json::object()),"attempted")?1:0;
}
static std::wstring reportPath(){return SG_DIR+L"\\wake-recovery-last.json";}
static json get() {
    std::lock_guard<std::mutex> lock(mx);
    return {{"ok",true},{"enabled",g_sleepRecoveryAutoEnabled.load()},{"phase",state.phase},
        {"generation",state.generation},{"reason",state.reason},{"commandInFlight",state.commandInFlight},{"commandWait",state.commandWait},{"wakeAdmission",state.wakeAdmission},{"wakeReadiness",state.wakeReadiness},{"scope","steam"},{"steamRecovery",state.steamStatus},{"steamInputRecovery",state.inputStatus},{"healthVerified",false},{"logPath",W2U(reportPath())}};
}
static std::wstring exportCurrentSnapshot() {
    json evidence;
    {
        std::lock_guard<std::mutex> lock(mx);
        const bool pending=g_sleepRecoveryAutoPending.load();
        const bool active=pending&&sleepSteamRecoveryAllows(state.generation,state.configEpoch);
        evidence={{"schema",4},{"implementation","steam-wake-round50-native-record-diagnostic"},{"scope","steam"},
            {"automatic",true},{"exportOnly",true},{"readOnly",true},{"repairTriggered",false},
            {"generation",state.generation},{"currentPowerGeneration",currentPowerGeneration()},{"configEpoch",state.configEpoch},
            {"enabled",g_sleepRecoveryAutoEnabled.load()},{"pending",pending},{"active",active},
            {"reportState",active?"pending":pending?"superseded":state.phase=="wake-proof-wait"?"waiting-confirmed-wake":(state.phase=="recorded"||state.phase=="blocked")?"terminal":"idle"},
            {"phase",state.phase},{"reason",state.reason},{"wakeAdmission",state.wakeAdmission},{"wakeReadiness",state.wakeReadiness},{"associationHistory",state.associationHistory},{"elapsedMs",state.armed&&GetTickCount64()>=state.armed?GetTickCount64()-state.armed:0},
            {"before",state.before},{"after",state.last},{"receipt",state.receipt},{"secondaryRejection",state.secondaryRejection},{"preflightRejections",state.preflightRejections},{"observations",state.timeline},
            {"steamRecovery",state.steamStatus},{"steamInputRecovery",state.inputStatus},
            {"attempts",{{"list",state.listAttempted?1:0},{"input",state.inputAttempted?1:0},{"inputDispatches",state.inputDispatches},{"pidWindow",pidWindowAttempts(state.receipt)}}},
            {"settling",state.settling.summary()},{"postReturnObservation",state.postReturnObservationSummary()},{"recordEpoch",sgNowEpoch()},{"healthVerified",false},
            {"gameInput","not-tested"},{"uploaded",false}};
    }
    for(const auto key:{"before","after"})if(evidence[key].is_object()){
        evidence[key]["inspectionToken"]=nullptr;evidence[key]["repairTicketReusable"]=false;
    }
    std::lock_guard<std::mutex> io(reportMx);
    std::error_code ec;const auto size=fspath::file_size(fspath::path(reportPath()),ec);
    json previous;
    if(!ec&&size<=512*1024){std::ifstream f{fspath::path(reportPath()),std::ios::binary};if(f)previous=json::parse(f,nullptr,false);}
    if(previous.is_object())evidence["lastCompletedReport"]={{"available",true},{"generation",previous.value("generation",json())},
        {"phase",previous.value("phase",json())},{"reason",previous.value("reason",json())},{"recordEpoch",previous.value("recordEpoch",json())},
        {"sameGenerationAsCurrent",number(evidence,"generation")!=0&&number(previous,"generation")==number(evidence,"generation")&&previous.value("configEpoch",json())==evidence["configEpoch"]}};
    else evidence["lastCompletedReport"]={{"available",false}};
    auto bytes=evidence.dump(2,' ',false,json::error_handler_t::replace);
    if(bytes.size()>512*1024){evidence.erase("observations");evidence.erase("before");evidence["truncated"]=true;bytes=evidence.dump(2,' ',false,json::error_handler_t::replace);}
    if(bytes.size()>512*1024)return {};
    const auto path=SG_DIR+L"\\wake-recovery-current.json";
    return sgEnsureMarkerDir(SG_DIR)&&sgWriteFileAtomic(path,bytes)?path:std::wstring();
}
static void notify(const std::function<void(const std::function<void()>&)>& withOwner = nullptr) {
    if(!withOwner){sgNotifyWork();postSleepRecoveryAutoStatus();return;}
    withOwner([]{});sgNotifyWork();withOwner(postSleepRecoveryAutoStatus);
}
static bool current(unsigned long long gen,unsigned long long epoch,const char* phase) {
    std::lock_guard<std::mutex> lock(mx);
    return sleepSteamRecoveryAllows(gen,epoch)&&state.generation==gen&&state.configEpoch==epoch&&state.phase==phase&&!proofSupersededLocked()&&wakeOperationAllowedLocked(gen,epoch)&&GetTickCount64()>=state.armed&&GetTickCount64()-state.armed<ymcc::sleepsteam::kWakeObserveLimitMs;
}
static void finish(unsigned long long gen,unsigned long long epoch,const std::string& phase,const std::string& reason,json evidence) {
    {
        std::lock_guard<std::mutex> lock(mx);
        if(state.generation!=gen||state.configEpoch!=epoch||!g_sleepRecoveryAutoPending.load())return;
        // A newer public entry can retire the verification while Ready still matches.
        // Leave the episode pending for the existing cancellation or continuation path.
        if(phase=="recorded"&&(proofSupersededLocked()||!wakeOperationAllowedLocked(gen,epoch)))return;
        if(!evidence.contains("before")&&state.before.is_object())evidence["before"]=state.before;
        if(!evidence.contains("after")&&state.last.is_object())evidence["after"]=state.last;
        if(!evidence.contains("receipt")&&state.receipt.is_object())evidence["receipt"]=state.receipt;
        evidence["postReturnObservation"]=state.postReturnObservationSummary();evidence["commandInFlight"]=state.commandInFlight;evidence["commandWait"]=state.commandWait;
        evidence["settling"]=state.settling.summary();evidence["observations"]=state.timeline;evidence["wakeAdmission"]=state.wakeAdmission;evidence["wakeReadiness"]=state.wakeReadiness;evidence["associationHistory"]=state.associationHistory;evidence["preflightRejections"]=state.preflightRejections;evidence["secondaryRejection"]=state.secondaryRejection;
        evidence["attempts"]={{"list",state.listAttempted?1:0},{"input",state.inputAttempted?1:0},{"inputDispatches",state.inputDispatches},{"pidWindow",pidWindowAttempts(evidence.value("receipt",json::object()))}};
        state.phase=phase;state.reason=reason;
        if(evidence.contains("before"))state.before=evidence["before"];
        if(evidence.contains("after"))state.last=evidence["after"];
        if(evidence.contains("receipt"))state.receipt=evidence["receipt"];
        if(evidence.contains("after")&&evidence["after"].contains("steamRecovery"))state.steamStatus=evidence["after"]["steamRecovery"];
        else if(evidence.contains("before")&&evidence["before"].contains("steamRecovery"))state.steamStatus=evidence["before"]["steamRecovery"];
        if(evidence.contains("after"))state.inputStatus=evidence["after"].value("steamInputRecovery",json::object());
        else if(evidence.contains("before"))state.inputStatus=evidence["before"].value("steamInputRecovery",json::object());
        g_sleepRecoveryAutoPending.store(false,std::memory_order_release);
    }
    for(const auto key:{"before","after"})if(evidence.contains(key)&&evidence[key].is_object()){
        evidence[key]["inspectionToken"]=nullptr;evidence[key]["repairTicketReusable"]=false;
    }
    evidence["schema"]=4;evidence["implementation"]="steam-wake-round50-native-record-diagnostic";evidence["scope"]="steam";evidence["automatic"]=true;evidence["generation"]=gen;evidence["configEpoch"]=epoch;
    evidence["phase"]=phase;evidence["reason"]=reason;evidence["healthVerified"]=false;evidence["uploaded"]=false;
    evidence["gameInput"]="unknown";evidence["recordEpoch"]=sgNowEpoch();
    bool saved=false;
    {
        std::lock_guard<std::mutex> io(reportMx);
        if(sleepSteamRecoveryAllows(gen,epoch)) {
            auto bytes=evidence.dump(2,' ',false,json::error_handler_t::replace);
            if(bytes.size()>512*1024)bytes=json{{"automatic",true},{"generation",gen},{"phase",phase},
                {"reason",reason},{"truncated",true},{"healthVerified",false},{"uploaded",false}}.dump(2);
            saved=sgEnsureMarkerDir(SG_DIR)&&sgWriteFileAtomic(reportPath(),bytes);
        }
    }
    sgRecordFact("sleep-recovery-automatic-result",{{"generation",gen},{"phase",phase},{"reason",reason},
        {"feedbackSaved",saved},{"healthVerified",false},{"uploaded",false},{"wakeReadiness",evidence["wakeReadiness"]},
        {"attempts",{{"list",evidence["attempts"]["list"]},{"input",evidence["attempts"]["input"]},{"inputDispatches",evidence["attempts"]["inputDispatches"]}}}});
    {
        std::lock_guard<std::mutex> lock(mx);
        if(state.generation!=gen||state.configEpoch!=epoch)return;
        if(!saved&&sleepSteamRecoveryAllows(gen,epoch)){state.phase="blocked";state.reason="log-write-failed";}
    }
    notify();
}
static json set(const json& a, const std::function<bool(bool)>& savePreference = nullptr,
    const std::function<void(const std::function<void()>&)>& withOwner = nullptr) {
    const auto it=a.find("enabled");if(it==a.end()||!it->is_boolean())throw std::runtime_error("automatic-recovery-enabled-must-be-boolean");
    const auto admitted=[&](const std::function<void()>& action){if(withOwner)withOwner(action);else action();};
    const auto requireOwner=[&]{if(withOwner)withOwner([]{});};
    const bool enabled=it->get<bool>();requireOwner();
    std::unique_lock<std::timed_mutex> serial(setMx,std::defer_lock);
    if(!serial.try_lock_for(std::chrono::seconds(2)))throw std::runtime_error("Automatic recovery configuration is busy");
    requireOwner();
    const bool changed=enabled!=g_sleepRecoveryAutoEnabled.load();
    // The IPC supplies its original locked settings writer and provenance.
    // An explicit same-state save can update global intent without rearming.
    const bool saved=!(changed||savePreference)||(savePreference?savePreference(enabled):ymSettingsPatchSection("sleep",{{"autoWakeRecovery",enabled}}));
    requireOwner();
    if(!saved)return {{"ok",false},{"reason","automatic-recovery-preference-save-failed"}};
    if(changed) {
        // Revoke the old worker before waiting for its state lock.
        admitted([&]{
            steamOriginalRecoveryBusinessCommit(false,[&]{
                g_sleepRecoveryAutoConfigEpoch.fetch_add(1,std::memory_order_acq_rel);
                g_sleepRecoveryAutoEnabled.store(enabled,std::memory_order_release);
            });
        });
        {
            std::lock_guard<std::mutex> lock(mx);
            admitted([&]{
                state.phase=enabled?"idle":"off";state.reason.clear();state.steamStatus=json::object();state.inputStatus=json::object();
                g_sleepRecoveryAutoPending.store(false,std::memory_order_release);
            });
        }
        requireOwner();
        sgRecordFact("sleep-recovery-automatic-preference",{{"enabled",enabled},{"trigger","future-explicit-wake-only"}});
        requireOwner();notify(withOwner);requireOwner();
    }
    const auto result=get();requireOwner();return result;
}
// Kernel subscriber only records a bounded Steam-specific witness. It never
// arms a worker, touches a game/device or changes shared power classification.
static void recordKernelWake(unsigned long long gen,int eventId,int reason,int targetState,ULONGLONG eventFileTime) {
    // Public Native subscriber already recorded this original event once.
    (void)gen;(void)eventId;(void)reason;(void)targetState;(void)eventFileTime;
    sgNotifyWork();
}
// Adapter supplies original consumer ownership; shared policy owns continuation.
static ymcc::sleeppower::WakeIdentity originalWakeIdentity(const SleepSteamRecovery::Captured& original) {
    return {original.generation,original.configEpoch,original.request?original.request->serial:0,
        original.powerBoundaryFileTime,original.game.pid,original.game.created};
}
static bool zeroWakeActionsLocked() {
    return !state.commandInFlight&&!state.listAttempted&&!state.inputAttempted&&state.inputDispatches==0&&
        (state.receipt.is_null()||(state.receipt.is_object()&&state.receipt.empty()));
}
static bool zeroActionOriginalCurrentLocked(const SleepSteamRecovery::Captured& original,ULONGLONG now) {
    return (state.phase=="wake-proof-wait"||state.phase=="waiting")&&
        sleepSteamRecoveryAllows(state.generation,state.configEpoch)&&
        state.wakeContinuation.current(originalWakeIdentity(original),now,sgNowFileTime(),
            SleepSteamRecovery::originalTargetCurrent(original),zeroWakeActionsLocked());
}
static void arm(unsigned long long gen,bool relayOriginal=false) {
    if(!gen||gen!=currentPowerGeneration())return;
    const auto original=SleepSteamRecovery::capturedFor(gen);
    const bool modernStandby=sgModernStandbyRepairSupported();
    bool deferred=false,changed=false;json admission;
    {
        std::lock_guard<std::mutex> lock(mx);
        const auto epoch=g_sleepRecoveryAutoConfigEpoch.load();
        if(!sleepSteamRecoveryAllows(gen,epoch))return;
        // Preserve the explicit request even if the previous cycle still owns
        // this pre-increment generation. It cannot replay that cycle, but may
        // accompany the exact prepared boundary into its next capture.
        ymcc::sleeppower::nativeWakeFacts.requestedLocked(gen,original.boundaryGeneration);
        const auto native=sharedWakeFactsLocked();
        const auto& kernelWake=native.kernelWake;const auto& kernelSleepEntry=native.kernelSleepEntry;
        const auto& completedResume=native.completedResume;const auto& interactiveResume=native.interactiveResume;
        const bool continuing=state.wakeContinuation.awaiting();
        if(state.seenGeneration==gen&&!continuing)return;
        if(continuing&&(!zeroActionOriginalCurrentLocked(original,GetTickCount64())||
            !state.wakeContinuation.retiredEvent()||sgNowFileTime()<state.wakeContinuation.retiredEvent()||
            sgNowFileTime()-state.wakeContinuation.retiredEvent()>=ymcc::sleepsteam::kWakeObserveLimitMs*10000ULL))return;
        const bool sameGeneration=kernelWake.generation==gen;
        // The existing capture contract can transfer a synchronous prepared
        // boundary into ONLY its immediately following power generation.
        // A raw proof may follow that exact boundary, never an arbitrary gen+1.
        const bool transferredBoundary=kernelWake.generation!=0&&kernelWake.generation!=ULLONG_MAX&&kernelWake.generation+1==gen&&
            original.boundaryGeneration==kernelWake.generation;
        const auto proofNow=sgNowFileTime();
        const bool freshProof=kernelWake.eventFileTime&&kernelWake.eventFileTime<=proofNow&&
            proofNow-kernelWake.eventFileTime<=ymcc::sleepsteam::kWakeObserveLimitMs*10000ULL&&
            (kernelSleepEntry.configEpoch!=epoch||kernelWake.eventFileTime>kernelSleepEntry.eventFileTime);
        const bool confirmed=freshProof&&(sameGeneration||transferredBoundary)&&kernelWake.configEpoch==epoch&&
            original.generation==gen&&original.configEpoch==epoch&&original.powerBoundaryFileTime!=0&&
            (kernelWake.boundaryFileTime==0||kernelWake.boundaryFileTime==original.powerBoundaryFileTime)&&
            kernelWake.eventFileTime>original.powerBoundaryFileTime&&kernelWake.eventFileTime<=sgNowFileTime();
        const bool interactiveConfirmed=!confirmed&&completedInteractiveResumeLocked(gen,epoch,original.powerBoundaryFileTime,original.boundaryGeneration);
        if(continuing&&!state.wakeContinuation.canContinue(originalWakeIdentity(original),GetTickCount64(),sgNowFileTime(),
            SleepSteamRecovery::originalTargetCurrent(original),zeroWakeActionsLocked(),sharedSleepWakeClassified(gen),sgSleepRetryActive(),
            completedInteractiveResumeLocked(gen,epoch,original.powerBoundaryFileTime,original.boundaryGeneration),
            completedResume.eventId,completedResume.eventFileTime,confirmed?kernelWake.eventId:0,confirmed?kernelWake.eventFileTime:0))return;
        admission={{"admissionRevision","completed-interactive-ready-20261111"},{"status",modernStandby&&!confirmed&&!interactiveConfirmed?"waiting-confirmed-kernel-wake":"admitted"},
            {"source",confirmed?(kernelWake.eventId==507?"kernel-power-507-reason-1":"kernel-power-107-target-5"):interactiveConfirmed?"interactive-resume-committed-ready":modernStandby?"pbt-intermediate-resume-not-proof":"pbt-resume-suspend-s3"},
            {"interactiveWakeConfirmed",interactiveConfirmed},{"readyCommitMatched",interactiveConfirmed},
            {"completedEventId",interactiveConfirmed?completedResume.eventId:0},{"completedReason",interactiveConfirmed?completedResume.reason:0},
            {"completedEventFileTime",interactiveConfirmed?json(std::to_string(completedResume.eventFileTime)):json()},
            {"modernStandby",modernStandby},{"kernelWakeConfirmed",confirmed},{"boundaryFileTime",std::to_string(original.powerBoundaryFileTime)},
            {"boundaryGeneration",original.boundaryGeneration},{"proofObservedGeneration",kernelWake.generation},
            {"proofOriginallyAwaitedCapture",confirmed&&kernelWake.awaitedCapture},{"proofFollowedPreparedBoundary",confirmed&&transferredBoundary},
            {"eventFileTime",confirmed?json(std::to_string(kernelWake.eventFileTime)):interactiveConfirmed?json(std::to_string(interactiveResume.eventFileTime)):json()},
            {"pbtObservedFileTime",!modernStandby&&!confirmed?json(std::to_string(proofNow)):json()},
            {"proofFresh",freshProof||interactiveConfirmed},{"proofMaximumAgeMs",ymcc::sleepsteam::kWakeObserveLimitMs},
            {"latestEntryFileTime",kernelSleepEntry.configEpoch==epoch?json(std::to_string(kernelSleepEntry.eventFileTime)):json()},
            {"gameInputVerified",false},{"continueButtonVerified",false}};
        // S0 emits RESUMESUSPEND even while entering standby. Do not start the
        // 60s budget or consume the generation/RETURN on that intermediate PBT.
        // S3 lacks 506/507; its documented PBT path is intentionally retained.
        deferred=modernStandby&&!confirmed&&!interactiveConfirmed;
        if(deferred){
            if(state.generation!=gen||state.configEpoch!=epoch||state.phase!="wake-proof-wait"){
                const auto seen=state.seenGeneration;state={};state.seenGeneration=seen;
                state.generation=gen;state.configEpoch=epoch;state.wakeCandidateAt=GetTickCount64();
                state.phase="wake-proof-wait";state.reason="steam-confirmed-user-wake-not-observed";state.wakeAdmission=admission;
                g_sleepRecoveryAutoPending.store(false,std::memory_order_release);changed=true;
            }
        }else{
            const auto candidateAt=state.generation==gen&&state.configEpoch==epoch?state.wakeCandidateAt:0;
            admission["deferredCandidateObserved"]=state.generation==gen&&state.configEpoch==epoch&&state.phase=="wake-proof-wait";
            admission["candidateWaitMs"]=flag(admission,"deferredCandidateObserved")&&GetTickCount64()>=candidateAt?GetTickCount64()-candidateAt:0;
            const auto previousSeen=state.seenGeneration;
            const auto firstContinuation=state.wakeContinuation;
            state={};state.seenBeforeAdmission=previousSeen;state.seenGeneration=gen;state.generation=gen;state.configEpoch=epoch;
            state.armed=continuing?firstContinuation.armed():GetTickCount64();state.stageSince=continuing?firstContinuation.stage():state.armed;state.wakeAdmission=admission;
            if(continuing){state.wakeContinuation=firstContinuation;state.wakeContinuation.resumed();}
            else state.wakeContinuation.begin(originalWakeIdentity(original),state.armed,state.stageSince);
            state.phase="waiting";g_sleepRecoveryAutoPending.store(true,std::memory_order_release);changed=true;
        }
    }
    if(!changed)return;
    sgRecordFact(deferred?"sleep-steam-wake-await-kernel":"sleep-recovery-automatic-armed",
        {{"generation",gen},{"trigger",admission["source"]},{"wakeAdmission",admission},{"repairTriggered",!deferred},{"healthVerified",false}});
    notify();
    if(relayOriginal&&!deferred)steamOriginalRecoveryWake(gen);
}
// Record RELIABLE native overview transitions, including while the original
// sleep lease or a protected UI delays RETURN. This is historical evidence,
// not a status writer, button proof or an instruction to replay an action.
static void observeAssociation(unsigned long long gen,unsigned long long epoch,const json& report) {
    if(!flag(report.value("game",json::object()),"identityMatches")||!flag(report.value("suspendCapture",json::object()),"available")||
        !flag(report.value("steamSessionState",json::object()),"ready"))return;
    const auto steam=report.value("steam",json::object());if(!flag(steam,"ok"))return;
    const auto rows=steam.find("overviews");if(rows==steam.end()||!rows->is_array())return;
    const auto appid=number(report.value("tracker",json::object()),"appid");if(!appid)return;
    json stamp;for(const auto& row:*rows)if(number(row,"appid")==appid&&flag(row,"overviewAvailable")&&
        row.contains("localDisplayStatus")&&row["localDisplayStatus"].is_number_integer()&&row.contains("inRunningList")&&row["inRunningList"].is_boolean()){
        stamp={{"appid",appid},{"localDisplayStatus",row["localDisplayStatus"]},{"selectedDisplayStatus",row.value("selectedDisplayStatus",json())},
            {"inRunningList",row["inRunningList"]},{"associationVerified",flag(report.value("steamRecovery",json::object()),"associationVerified")}};break;
    }
    if(!stamp.is_object())return;
    json transition;
    {
        std::lock_guard<std::mutex> lock(mx);
        if(!sleepSteamRecoveryAllows(gen,epoch)||state.generation!=gen||state.configEpoch!=epoch||!g_sleepRecoveryAutoPending.load())return;
        if(!state.associationHistory.is_object())state.associationHistory={{"evidence","reliable-native-overview-observations-not-button-proof"},
            {"firstCurrentAssociationElapsedMs",nullptr},{"firstLossAfterCurrentElapsedMs",nullptr},{"healthVerified",false},{"continueButtonVerified",false}};
        const auto now=GetTickCount64(),elapsed=now>=state.armed?now-state.armed:0;
        const auto previous=state.associationHistory.value("lastReliableState",json());
        if(flag(stamp,"associationVerified")&&state.associationHistory["firstCurrentAssociationElapsedMs"].is_null())state.associationHistory["firstCurrentAssociationElapsedMs"]=elapsed;
        const bool loss=previous.is_object()&&flag(previous,"associationVerified")&&!flag(stamp,"associationVerified");
        if(loss&&state.associationHistory["firstLossAfterCurrentElapsedMs"].is_null())state.associationHistory["firstLossAfterCurrentElapsedMs"]=elapsed;
        state.associationHistory["lastReliableState"]=stamp;state.associationHistory["lastReliableElapsedMs"]=elapsed;
        state.associationHistory["lastReliableRecordEpoch"]=report.value("recordEpoch",json());
        if(previous!=stamp){
            const auto inputReceipt=state.receipt.is_object()?state.receipt.value("inputResume",json::object()):json::object();
            transition={{"generation",gen},{"elapsedMs",elapsed},{"from",previous},{"to",stamp},{"lostAfterCurrent",loss},
                {"originalGame",report["game"]},{"recordEpoch",report.value("recordEpoch",json())},
                {"returnMutationPreviouslyMarked",flag(inputReceipt,"mutated")},{"healthVerified",false},{"continueButtonVerified",false}};
        }
    }
    if(transition.is_object())sgRecordFact("sleep-steam-running-association-transition",transition);
}
static void captureReady(unsigned long long gen) {
    if(!gen||gen!=currentPowerGeneration()||!sgModernStandbyRepairSupported())return;
    const auto original=SleepSteamRecovery::capturedFor(gen);
    bool requested=false;
    {
        std::lock_guard<std::mutex> lock(mx);
        const auto native=sharedWakeFactsLocked();
        const auto& kernelWake=native.kernelWake;const auto& kernelSleepEntry=native.kernelSleepEntry;
        const auto& interactiveResume=native.interactiveResume;const auto& completedResume=native.completedResume;const auto& readyCommit=native.readyCommit;

        const auto epoch=g_sleepRecoveryAutoConfigEpoch.load();
        const bool requestedCurrent=state.generation==gen;
        const bool requestedPrepared=state.generation!=0&&state.generation!=ULLONG_MAX&&state.generation+1==gen&&original.boundaryGeneration==state.generation;
        const bool witnessedRequest=kernelWake.uiWakeRequested&&kernelWake.configEpoch==epoch&&
            (kernelWake.requestGeneration==gen||(kernelWake.requestGeneration!=ULLONG_MAX&&kernelWake.requestGeneration+1==gen&&original.boundaryGeneration==kernelWake.requestGeneration));
        const bool completedInteractive=completedInteractiveResumeLocked(gen,epoch,original.powerBoundaryFileTime,original.boundaryGeneration);
        requested=sleepSteamRecoveryAllows(gen,epoch)&&original.generation==gen&&original.configEpoch==epoch&&
            ((kernelWake.eventFileTime!=0&&(witnessedRequest||(state.phase=="wake-proof-wait"&&state.configEpoch==epoch&&(requestedCurrent||requestedPrepared))))||completedInteractive);
    }
    if(requested)arm(gen,true);
}
static void powerReady(unsigned long long gen) {
    const auto epoch=g_sleepRecoveryAutoConfigEpoch.load();
    if(!sleepSteamRecoveryAllows(gen,epoch)||g_powerLifecycle.load()!=PowerLifecycle::Ready)return;
    const auto original=SleepSteamRecovery::wakeBoundaryFor(gen);
    if(!original.powerBoundaryFileTime)return;
    {std::lock_guard<std::mutex> lock(mx);
        if(!sleepSteamRecoveryAllows(gen,epoch)||g_powerLifecycle.load()!=PowerLifecycle::Ready)return;
        // The current Ready fact is published by the shared projection before this callback.
    }
    captureReady(gen); // order-independent join; no timestamp/budget reset.
}
// An acknowledged action can miss the window in its immediate postflight,
// then a NEW native inspection can already see the exact captured window.
// This is permission to keep bounded observations, not an input/action ACK,
// reservation refund, a target change or authority to replay the RETURN.
static bool sameOriginalWindowReadbackTransaction(const json& inputReceipt,const json& before,const json& after) {
    if(inputReceipt.value("accepted",json())!=json(false)||inputReceipt.value("originalWindowFocused",json())!=json(false)||
        !flag(inputReceipt,"mutated")||!flag(inputReceipt,"actionAcknowledged")||!flag(inputReceipt,"originalIdentityStable")||
        text(inputReceipt,"reason")!="steam-original-window-not-confirmed"||text(inputReceipt,"stage")!="sleep-input-resume"||
        text(inputReceipt,"nativeAdmissionReason")!="admitted")return false;
    const auto beforeGame=before.value("game",json::object()),afterGame=after.value("game",json::object());
    const auto pause=after.value("gamePause",json::object()),original=after.value("originalGameRecovery",json::object());
    const auto pid=number(inputReceipt,"originalPid");const auto created=text(inputReceipt,"originalCreated");
    return pid!=0&&!created.empty()&&number(beforeGame,"pid")==pid&&number(afterGame,"pid")==pid&&
        text(beforeGame,"created")==created&&text(afterGame,"created")==created&&flag(beforeGame,"identityMatches")&&
        flag(afterGame,"identityMatches")&&flag(afterGame,"recoveryBindingMatches")&&flag(afterGame,"powerGenerationMatches")&&
        flag(original,"jointBindingVerified")&&flag(original,"originalInstanceMatched")&&
        flag(after.value("suspendCapture",json::object()),"available")&&flag(after.value("steamSessionState",json::object()),"ready")&&
        flag(pause,"available")&&!flag(pause,"paused")&&!flag(pause,"waitForSleepRelease")&&
        SleepSteamRecovery::sameClientEvidence(before.value("steamClient",json::object()),after.value("steamClient",json::object()));
}
static bool delayedOriginalWindowReadback(const json& inputReceipt,const json& before,const json& after) {
    if(!sameOriginalWindowReadbackTransaction(inputReceipt,before,after))return false;
    const auto fg=after.value("foreground",json::object());
    return flag(after.value("originalGameRecovery",json::object()),"originalWindowFocused")&&
        text(fg,"role")=="original-game"&&number(fg,"pid")==number(inputReceipt,"originalPid")&&text(fg,"created")==text(inputReceipt,"originalCreated");
}
static bool pendingOriginalWindowReadback(const json& inputReceipt,const json& before,const json& after) {
    // Steam can return while its own window worker is still running. Preserve
    // only the remaining READS for this exact dispatched transaction. Neither
    // a result code nor a missing window can refund or replay its action.
    return sameOriginalWindowReadbackTransaction(inputReceipt,before,after)&&
        inputReceipt.value("windowReturnObserved",json())==json(false)&&
        !flag(after.value("originalGameRecovery",json::object()),"originalWindowFocused");
}

// A call owns a tentative slot while its native admission is unresolved. This
// blocks zero-action handoff. Native BEFORE-send commits the original budget;
// non-Busy early returns and exceptions conservatively keep the original charge.
struct CommandReservation {
    unsigned long long generation,epoch;const char* expected;bool input,charged=false,settled=false;
    ymcc::sleepsteam::CommandAttempt command;
    CommandReservation(unsigned long long gen,unsigned long long cfg,const char* phase,bool forInput)
        :generation(gen),epoch(cfg),expected(phase),input(forInput){
        command.admit=[this]{std::lock_guard<std::mutex> lock(mx);
            if(state.generation!=generation||state.configEpoch!=epoch||state.phase!=expected||
                !g_sleepRecoveryAutoPending.load()||!sleepSteamRecoveryAllows(generation,epoch))return false;
            if(input&&!charged&&state.inputDispatches>=3)return false;
            chargeLocked();return true;};
        std::lock_guard<std::mutex> lock(mx);
        if(state.generation!=generation||state.configEpoch!=epoch||state.phase!=expected||state.commandInFlight)
            throw std::runtime_error("automatic-command-slot-not-current");
        state.commandInFlight=true;
    }
    void chargeLocked(){if(charged)return;if(input){state.inputAttempted=true;++state.inputDispatches;}else state.listAttempted=true;charged=true;}
    ~CommandReservation(){if(settled)return;std::lock_guard<std::mutex> lock(mx);
        if(state.generation==generation&&state.configEpoch==epoch){chargeLocked();state.commandInFlight=false;}}
    bool resolve(const json& receipt,const json& before){
        const bool busy=!charged&&command.disposition==ymcc::steamcommand::Disposition::Busy&&receipt.is_object()&&
            receipt.value("reason",json())==json("steam-command-busy")&&receipt.value("ok",json())==json(false)&&
            receipt.value("accepted",json())==json(false)&&receipt.value("mutated",json())==json(false)&&
            receipt.value("requestDispatched",json())==json(false)&&!flag(receipt,"actionAcknowledged");
        std::lock_guard<std::mutex> lock(mx);
        if(state.generation!=generation||state.configEpoch!=epoch){settled=true;return false;}
        if(!busy)chargeLocked();
        bool waiting=false;
        if(busy&&state.phase==expected&&g_sleepRecoveryAutoPending.load()&&sleepSteamRecoveryAllows(generation,epoch)){
            // Separate observed native rejection from an action receipt. No
            // counter, proof, nonce, stage/armed clock or deadline is refunded.
            state.commandWait=receipt;if(!state.before.is_object())state.before=before;state.last=before;
            state.phase="command-wait";state.reason="steam-command-busy";
            state.due=GetTickCount64()+ymcc::sleepsteam::kWakePollMs;waiting=true;
        }
        state.commandInFlight=false;settled=true;return waiting;
    }
};

static void run(unsigned long long gen,unsigned long long epoch,bool verify) {
    const char* expected=verify?"verifying":"inspecting";
    if(!current(gen,epoch,expected))return;
    try {
        auto report=SleepSteamRecovery::inspect(gen);
        if(!current(gen,epoch,expected)||number(report,"generation")!=gen)return;
        observeAssociation(gen,epoch,report);
        const auto pause=report.value("gamePause",json::object());
        if(!flag(report.value("game",json::object()),"identityMatches")||!flag(report.value("suspendCapture",json::object()),"available")) {
            const auto selected=report.value("suspendCapture",json::object()).value("targetSelection",json::object());
            const auto session=report.value("steamSessionState",json::object());
            const auto failure=text(session,"status")=="unbound"?text(session,"reason"):
                text(selected,"status")=="unbound"?text(selected,"reason"):
                flag(report.value("game",json::object()),"identityMatches")?"steam-original-target-not-captured-at-suspend":"original-game-identity-unavailable";
            finish(gen,epoch,"blocked",failure,{{"after",report}});return;
        }
        const auto clientState=report.value("steamSessionState",json::object());
        if(text(clientState,"status")=="changed"||text(clientState,"status")=="unbound"){
            finish(gen,epoch,"blocked",text(clientState,"reason"),{{"after",report}});return;
        }
        if(flag(pause,"waitForSleepRelease")){
            {
                std::lock_guard<std::mutex> lock(mx);
                if(!sleepSteamRecoveryAllows(gen,epoch)||state.generation!=gen||state.configEpoch!=epoch||state.phase!=expected)return;
                if(!state.before.is_object())state.before=report;state.last=report;state.phase="game-wait";state.reason="steam-original-game-sleep-lease-held";
                state.steamStatus=report.value("steamRecovery",json::object());state.inputStatus=report.value("steamInputRecovery",json::object());
                state.due=GetTickCount64()+750;state.stageSince=GetTickCount64();
            }
            sgRecordFact("sleep-steam-input-wait-original-game",{{"generation",gen},{"pid",number(report["game"],"pid")},{"reason","sleep-lease-held"},{"maxWaitMs",30000},{"gameInputVerified",false}});notify();return;
        }
        if(flag(pause,"paused")){
            finish(gen,epoch,"blocked",text(report.value("steamInputRecovery",json::object()),"reason"),{{"after",report}});return;
        }
        if(!flag(clientState,"ready")){
            {
                std::lock_guard<std::mutex> lock(mx);
                if(!sleepSteamRecoveryAllows(gen,epoch)||state.generation!=gen||state.configEpoch!=epoch||state.phase!=expected)return;
                const auto now=GetTickCount64(),elapsed=now>=state.armed?now-state.armed:0;
                if(!state.before.is_object())state.before=report;state.last=report;
                state.steamStatus=report.value("steamRecovery",json::object());state.inputStatus=report.value("steamInputRecovery",json::object());
                (void)state.settling.observe(report,elapsed);state.settling.reason=text(clientState,"reason");
                state.phase="session-wait";state.reason=text(clientState,"reason");state.stageSince=now;state.due=now+ymcc::sleepsteam::kWakePollMs;
                if(state.timeline.size()<48)state.timeline.push_back({{"elapsedMs",elapsed},{"stage","session-wait"},{"session",clientState},
                    {"game",report.value("game",json::object())},{"client",report.value("steamClient",json::object())},{"gameInputVerified",false}});
            }
            notify();return;
        }
        bool listAttempted=false,inputAttempted=false;
        {std::lock_guard<std::mutex> lock(mx);listAttempted=state.listAttempted;inputAttempted=state.inputAttempted;}
        // Foreground is an ACTION/readiness guard, not the captured game's identity.
        // A completed RETURN may be followed by a transient Steam/other foreground;
        // keep bounded PID/session reads, but do not mutate or steal that foreground.
        const bool postReturnForegroundWait=verify&&inputAttempted&&
            text(report.value("foreground",json::object()),"role")!="original-game";
        const auto before=report;json receipt;bool inputDeferred=false;
        const auto stillCurrent=[gen,epoch,expected]{return current(gen,epoch,expected);};
        const auto steamRecovery=report.value("steamRecovery",json::object());
        const auto originalGame=report.value("game",json::object());
        const bool nativeOriginalOwner=steamOriginalRecoveryOwnsRunning(static_cast<DWORD>(number(originalGame,"pid")),
            [] (const json& game){try{return std::stoull(game.value("created",std::string("0")));}catch(...){return 0ULL;}}(originalGame));
        if(!nativeOriginalOwner&&!postReturnForegroundWait&&(flag(steamRecovery,"repairable")||flag(steamRecovery,"contactRepairable"))&&!listAttempted){
            CommandReservation command(gen,epoch,expected,false);
            receipt=SleepSteamRecovery::reconcile(report,gen,epoch,stillCurrent,&command.command);
            if(command.resolve(receipt,before)){notify();return;}
            if(!current(gen,epoch,expected))return;
            // Always retain a NEW post-action snapshot, including rejection.
            // The old report is the pre-action inspection, never an "after".
            report=SleepSteamRecovery::inspect(gen,"reconcile-postflight");
            if(!current(gen,epoch,expected))return;
            if(!flag(receipt,"accepted")){
                const auto reason=text(receipt,"reason");
                const bool listOnly=reason=="steam-rescope-unavailable"||reason=="steam-rescope-schema-changed"||reason=="steam-rescope-not-confirmed"||reason=="steam-contact-rescope-not-confirmed";
                const bool contactOnly=reason=="steam-contact-rescope-not-confirmed";
                const auto beforeGame=before.value("game",json::object()),afterGame=report.value("game",json::object());
                const auto input=report.value("steamInputRecovery",json::object()),pause=report.value("gamePause",json::object());
                const auto beforeSteam=before.value("steamRecovery",json::object()),afterSteam=report.value("steamRecovery",json::object());
                const auto basis=text(input,"returnBasis");
                const bool contactSafe=!contactOnly||(flag(beforeSteam,"contactConnected")&&flag(afterSteam,"contactConnected")&&
                    flag(beforeSteam,"contactRepairable")&&flag(beforeSteam.value("contactProof",json::object()),"originalInstanceMatched")&&
                    flag(afterSteam.value("contactProof",json::object()),"originalInstanceMatched"));
                const bool originalSafe=listOnly&&contactSafe&&flag(afterGame,"identityMatches")&&number(beforeGame,"pid")==number(afterGame,"pid")&&
                    text(beforeGame,"created")==text(afterGame,"created")&&SleepSteamRecovery::sameClientEvidence(before.value("steamClient",json::object()),report.value("steamClient",json::object()))&&
                    flag(report.value("suspendCapture",json::object()),"available")&&flag(report.value("steamSessionState",json::object()),"ready")&&
                    flag(pause,"available")&&!flag(pause,"paused")&&basis!="none"&&
                    (basis=="current-association"||basis=="captured-original-local-running"||basis=="captured-original-tracker");
                if(!originalSafe){finish(gen,epoch,"blocked",reason,{{"before",before},{"receipt",receipt},{"after",report}});return;}
                {std::lock_guard<std::mutex> lock(mx);if(state.generation!=gen||state.configEpoch!=epoch||state.phase!=expected)return;state.secondaryRejection=receipt;}
            }
        }
        if((flag(report.value("steamInputRecovery",json::object()),"repairable")||flag(report.value("steamInputRecovery",json::object()),"windowPreparationRepairable"))&&!inputAttempted){
            CommandReservation command(gen,epoch,expected,true);
            auto inputReceipt=SleepSteamRecovery::resumeInputContext(report,gen,epoch,stillCurrent,&command.command);
            if(command.resolve(inputReceipt,before)){notify();return;}
            if(!current(gen,epoch,expected))return;
            receipt["inputResume"]=inputReceipt;
            report=SleepSteamRecovery::inspect(gen,"input-postflight");
            if(!current(gen,epoch,expected))return;
            // A completed native RETURN + exact original OS PID/window is NOT
            // an input ACK. UI-cache-only uncertainty gets remaining bounded
            // READS, never another action or a promoted accepted=true receipt.
            const auto afterGame=report.value("game",json::object()),afterPause=report.value("gamePause",json::object());
            const bool delayedWindow=delayedOriginalWindowReadback(inputReceipt,before,report);
            const bool initialWindowPending=pendingOriginalWindowReadback(inputReceipt,before,report);
            if(initialWindowPending)receipt["nativeWindowReadbackWait"]={{"source","acknowledged-return-window-not-yet-observed"},{"readOnly",true},
                {"originalPid",number(afterGame,"pid")},{"originalCreated",text(afterGame,"created")},{"windowObservedAtReceipt",false},
                {"recordEpoch",report.value("recordEpoch",json())},{"receiptAcceptanceUnchanged",true},{"actionReplayAllowed",false},
                {"gameInputVerified",false},{"continueButtonVerified",false}};
            if(delayedWindow)receipt["nativePostReturnReadback"]={{"source","fresh-native-original-window"},{"readOnly",true},
                {"delayedOriginalWindowObserved",true},{"originalPid",number(afterGame,"pid")},{"originalCreated",text(afterGame,"created")},
                {"recordEpoch",report.value("recordEpoch",json())},{"receiptAcceptanceUnchanged",true},{"actionReplayAllowed",false},
                {"gameInputVerified",false},{"continueButtonVerified",false}};
            const bool pending=delayedWindow||initialWindowPending||(flag(inputReceipt,"verificationPending")&&flag(inputReceipt,"actionAcknowledged")&&flag(inputReceipt,"windowReturnObserved")&&
                number(inputReceipt,"originalPid")==number(afterGame,"pid")&&text(inputReceipt,"originalCreated")==text(afterGame,"created")&&
                flag(afterGame,"identityMatches")&&flag(report.value("suspendCapture",json::object()),"available")&&flag(report.value("steamSessionState",json::object()),"ready")&&
                flag(afterPause,"available")&&!flag(afterPause,"paused")&&
                SleepSteamRecovery::sameClientEvidence(before.value("steamClient",json::object()),report.value("steamClient",json::object())));
            // The delayed path independently observes the exact original OS window;
            // it preserves the earlier rejected receipt and the single action budget.
            // windowReturnObserved already certifies the native exact-PID postflight.
            // A NEW read losing foreground after that receipt only resets readiness;
            // it cannot promote acceptance or authorize a replay/PID fallback.
            if(!flag(inputReceipt,"accepted")&&!pending){
                const auto beforeGame=before.value("game",json::object());
                const bool nativeNoSend=flag(inputReceipt,"nativePreDispatchCancelled")&&
                    inputReceipt.value("nativeAdmissionReason",std::string())=="steam-input-foreground-changed"&&
                    inputReceipt.value("stage",std::string())=="native-live-pre-dispatch"&&
                    inputReceipt.value("requestDispatched",json())==json(false);
                const bool preflightOnly=flag(inputReceipt,"preflightDeferred")&&flag(inputReceipt,"preflightOnly")&&
                    inputReceipt.value("mutated",json())==json(false)&&!flag(inputReceipt,"actionAcknowledged")&&
                    number(beforeGame,"pid")==number(afterGame,"pid")&&text(beforeGame,"created")==text(afterGame,"created")&&flag(afterGame,"identityMatches")&&
                    SleepSteamRecovery::sameClientEvidence(before.value("steamClient",json::object()),report.value("steamClient",json::object()))&&
                    flag(report.value("suspendCapture",json::object()),"available")&&flag(report.value("steamSessionState",json::object()),"ready")&&
                    flag(afterPause,"available")&&!flag(afterPause,"paused")&&
                    (flag(report.value("foreground",json::object()),"allowedForOriginalGameReturn")||nativeNoSend);
                if(preflightOnly){
                    std::lock_guard<std::mutex> lock(mx);
                    if(state.generation!=gen||state.configEpoch!=epoch||state.phase!=expected)return;
                    if(state.preflightRejections.size()<3)state.preflightRejections.push_back(inputReceipt);
                    if(state.inputDispatches<3){state.inputAttempted=false;inputDeferred=true;}
                    // Three reservations remain the cap. After a proven zero-send
                    // foreground veto, keep READS until the existing wake deadline
                    // without authorizing a fourth reservation or stealing focus.
                    else if(nativeNoSend)inputDeferred=true;
                }
                if(!inputDeferred){finish(gen,epoch,"blocked",text(inputReceipt,"reason"),{{"before",before},{"receipt",receipt},{"after",report}});return;}
            }
        }
        bool complete=false,expired=false,foregroundDeferred=false,foregroundTransition=false,initialWindowPendingNow=false,initialWindowTransition=false;std::string pendingReason;
        {
            std::lock_guard<std::mutex> lock(mx);
            if(!sleepSteamRecoveryAllows(gen,epoch)||state.generation!=gen||state.configEpoch!=epoch||state.phase!=expected)return;
            if(!state.before.is_object())state.before=before;
            if(receipt.is_object()){
                if(!state.receipt.is_object())state.receipt=json::object();
                for(auto i=receipt.begin();i!=receipt.end();++i)state.receipt[i.key()]=i.value();
                if(flag(receipt.value("nativePostReturnReadback",json::object()),"delayedOriginalWindowObserved"))state.postReturnDelayedWindowObserved=true;
            }
            state.last=report;state.steamStatus=report.value("steamRecovery",json::object());state.inputStatus=report.value("steamInputRecovery",json::object());
            const auto now=GetTickCount64(),elapsed=now>=state.armed?now-state.armed:0;
            const auto priorReturn=state.receipt.is_object()?state.receipt.value("inputResume",json::object()):json::object();
            if(delayedOriginalWindowReadback(priorReturn,state.before,report)){
                state.postReturnDelayedWindowObserved=true;
                state.receipt["nativePostReturnReadback"]={{"source","fresh-native-original-window"},{"readOnly",true},
                    {"delayedOriginalWindowObserved",true},{"originalPid",number(report.value("game",json::object()),"pid")},
                    {"originalCreated",text(report.value("game",json::object()),"created")},{"recordEpoch",report.value("recordEpoch",json())},
                    {"receiptAcceptanceUnchanged",true},{"actionReplayAllowed",false},{"gameInputVerified",false},{"continueButtonVerified",false}};
            }
            initialWindowPendingNow=!state.postReturnDelayedWindowObserved&&pendingOriginalWindowReadback(priorReturn,state.before,report);
            initialWindowTransition=initialWindowPendingNow!=state.initialWindowReadbackWait;state.initialWindowReadbackWait=initialWindowPendingNow;
            if(initialWindowPendingNow)++state.initialWindowReadbackSamples;
            foregroundDeferred=flag(priorReturn,"mutated")&&flag(priorReturn,"actionAcknowledged")&&(flag(priorReturn,"windowReturnObserved")||state.postReturnDelayedWindowObserved)&&
                text(report.value("foreground",json::object()),"role")!="original-game";
            foregroundTransition=foregroundDeferred!=state.postReturnForegroundWait;state.postReturnForegroundWait=foregroundDeferred;
            if(foregroundDeferred)++state.postReturnForegroundSamples;
            const bool stable=state.settling.observe(report,elapsed);
            complete=stable&&elapsed>=ymcc::sleepsteam::kWakeFinalizeMinimumMs;expired=elapsed>=ymcc::sleepsteam::kWakeObserveLimitMs;
            pendingReason=state.settling.reason;
            if(state.timeline.size()<48)state.timeline.push_back({{"elapsedMs",elapsed},{"pid",number(report["game"],"pid")},
                {"stage",state.initialWindowReadbackWait?"post-return-window-readback-wait":foregroundDeferred?"post-return-foreground-wait":"settling"},{"wakeReadiness",state.wakeReadiness},{"postReturnForegroundWait",foregroundDeferred},
                {"game",report.value("game",json::object())},{"steamClient",report.value("steamClient",json::object())},
                {"tracker",report.value("tracker",json::object())},
                {"inputControllers",report.value("steam",json::object()).value("inputContext",json::object()).value("controllers",json::array())},
                {"reason",pendingReason},{"association",state.steamStatus},{"input",state.inputStatus},{"continue",report.value("continueRecovery",json::object())},
                {"overviews",report.value("steam",json::object()).value("overviews",json::array())},{"inputFocus",report.value("steam",json::object()).value("inputContext",json::object()).value("focus",json::object())},
                {"controllerCount",report.value("steam",json::object()).value("inputContext",json::object()).value("controllers",json::array()).size()},
                {"interactive",report.value("steam",json::object()).value("inputContext",json::object()).value("interactive",json::object())},
                {"foreground",report.value("foreground",json::object())},{"originalGameRecovery",report.value("originalGameRecovery",json::object())}});
            if(!complete&&!expired){state.phase=inputDeferred?"interaction-wait":receipt.is_object()?"confirm-wait":"settle-wait";state.reason=pendingReason.empty()?(stable?"steam-wake-late-disconnect-observation":"steam-wake-stability-window"):pendingReason;state.stageSince=now;state.due=now+ymcc::sleepsteam::kWakePollMs;}
        }
        if(initialWindowTransition)sgRecordFact("sleep-steam-post-return-window-readback",{{"generation",gen},
            {"waitingInitialOriginalWindow",initialWindowPendingNow},{"readOnly",true},{"actionReplayAllowed",false},
            {"game",report.value("game",json::object())},{"originalWindowFocused",flag(report.value("originalGameRecovery",json::object()),"originalWindowFocused")},
            {"receiptAcceptanceUnchanged",true},{"gameInputVerified",false},{"continueButtonVerified",false}});
        if(foregroundTransition)sgRecordFact("sleep-steam-post-return-foreground-observation",{{"generation",gen},
            {"foregroundDeferred",foregroundDeferred},{"foregroundWaitReadOnly",foregroundDeferred},{"returnReplayAllowed",false},
            {"game",report.value("game",json::object())},{"steamClient",report.value("steamClient",json::object())},
            {"foreground",report.value("foreground",json::object())},{"gameInputVerified",false},{"continueButtonVerified",false}});
        if(complete){finish(gen,epoch,"recorded","steam-original-context-stable-gameplay-unverified",json::object());return;}
        if(expired){finish(gen,epoch,"blocked",pendingReason.empty()?"steam-wake-stability-not-confirmed":pendingReason,json::object());return;}
        if(receipt.is_object()){
            const bool inputPending=flag(receipt.value("inputResume",json::object()),"verificationPending")||flag(receipt.value("nativePostReturnReadback",json::object()),"readOnly")||flag(receipt.value("nativeWindowReadbackWait",json::object()),"readOnly");
            bool secondaryRejected=false;{std::lock_guard<std::mutex> lock(mx);secondaryRejected=state.secondaryRejection.is_object()&&!flag(state.secondaryRejection,"accepted");}
            sgRecordFact(inputDeferred?"sleep-recovery-automatic-preflight-wait":secondaryRejected?"sleep-recovery-automatic-partial-observing":inputPending?"sleep-recovery-automatic-observing":"sleep-recovery-automatic-accepted",
                {{"generation",gen},{"receipt",receipt},{"scope","steam"},{"observationOnly",inputPending||inputDeferred||secondaryRejected},{"secondaryReceiptRejected",secondaryRejected},{"associationObserved",flag(report.value("steamRecovery",json::object()),"associationVerified")},{"continueButtonVerified",false},{"preflightOnly",inputDeferred},{"actionReplayAllowed",false},{"healthVerified",false}});
        }
        notify();
    } catch(const std::exception& e) {
        finish(gen,epoch,"blocked","automatic-check-exception",{{"error",std::string(e.what()).substr(0,256)}});
    } catch(...) {finish(gen,epoch,"blocked","automatic-check-exception",json::object());}
}
// Worker completion publishes notify(); only a real next observation or the
// unchanged watchdog deadline wakes an in-flight operation. No 50ms heartbeat.
static ULONGLONG waitMs(ULONGLONG now) {
    if(!g_sleepRecoveryAutoPending.load(std::memory_order_acquire))return ~ULONGLONG{0};
    std::lock_guard<std::mutex> lock(mx);
    if(!g_sleepRecoveryAutoPending.load(std::memory_order_acquire))return ~ULONGLONG{0};
    if(now<state.armed||now<state.stageSince)return 0;
    const auto total=now-state.armed,stage=now-state.stageSince;
    if(total>=ymcc::sleepsteam::kWakeObserveLimitMs||stage>45000)return 0;
    auto delay=(std::min)(ULONGLONG{ymcc::sleepsteam::kWakeObserveLimitMs}-total,ULONGLONG{45001}-stage);
    if(state.phase=="waiting") {
        if(total>=30000||wakeOperationAllowedLocked(state.generation,state.configEpoch))return 0;
        delay=(std::min)(delay,(std::min)(ULONGLONG{250},ULONGLONG{30000}-total));
    } else if(state.phase=="game-wait"||state.phase=="command-wait"||state.phase=="confirm-wait"||state.phase=="interaction-wait"||state.phase=="settle-wait"||state.phase=="session-wait") {
        if(state.phase=="game-wait") {
            if(total>=30000)return 0;
            delay=(std::min)(delay,ULONGLONG{30000}-total);
        }
        if(now>=state.due)return 0;
        delay=(std::min)(delay,state.due-now);
    }
    return delay;
}
static void tick() {
    if(!g_sleepRecoveryAutoPending.load(std::memory_order_acquire))return;
    const auto original=SleepSteamRecovery::capturedFor(currentPowerGeneration());
    unsigned long long gen=0,epoch=0;bool verify=false,submit=false,revokedProofOnly=false,continueFromCurrentFacts=false;json readinessNotice;std::string failure;
    {
        std::lock_guard<std::mutex> lock(mx);
        const auto native=sharedWakeFactsLocked();
        const auto& kernelWake=native.kernelWake;const auto& kernelSleepEntry=native.kernelSleepEntry;
        const auto& interactiveResume=native.interactiveResume;const auto& completedResume=native.completedResume;const auto& readyCommit=native.readyCommit;
gen=state.generation;epoch=state.configEpoch;
        const auto now=GetTickCount64();
        const auto readiness=wakeReadinessLocked(gen,epoch);
        // Compare decision identity, never moving proofNow/age: repeated timers stay quiet.
        if(!state.wakeReadiness.is_object()||state.wakeReadiness.value("basis",std::string())!=readiness.value("basis",std::string())||
            state.wakeReadiness.value("sharedPowerLifecycle",std::string())!=readiness.value("sharedPowerLifecycle",std::string())||
            state.wakeReadiness.value("operationAllowed",false)!=readiness.value("operationAllowed",false)||
            state.wakeReadiness.value("rejectedGuard",json())!=readiness.value("rejectedGuard",json())||
            state.wakeReadiness.value("generation",0ULL)!=gen||state.wakeReadiness.value("configEpoch",0ULL)!=epoch)
            readinessNotice=readiness;
        state.wakeReadiness=readiness;
        const bool steamWakeUsable=flag(readiness,"operationAllowed");
        const bool awaitingWakeCommit=(state.phase=="waiting"||state.phase=="command-wait")&&flag(state.wakeAdmission,"kernelWakeConfirmed");
        std::uint64_t retiringEvent=0;
        const auto retiring=state.wakeAdmission.value("eventFileTime",json());
        std::uint64_t admittedCompleted=0;
        const auto completedAtAdmission=state.wakeAdmission.value("completedEventFileTime",json());
        if((state.phase=="waiting"||state.phase=="command-wait")&&flag(state.wakeAdmission,"interactiveWakeConfirmed")&&
            state.wakeAdmission.value("source",std::string{})=="interactive-resume-committed-ready"&&
            retiring.is_string()&&ymcc::sleepsteam::unsignedDecimal(retiring.get<std::string>(),retiringEvent)&&
            completedAtAdmission.is_string()&&ymcc::sleepsteam::unsignedDecimal(completedAtAdmission.get<std::string>(),admittedCompleted)&&
            kernelSleepEntry.configEpoch==epoch&&
            state.wakeContinuation.parkInteractiveEntry(originalWakeIdentity(original),now,sgNowFileTime(),
                retiringEvent,admittedCompleted,kernelSleepEntry.eventFileTime,
                SleepSteamRecovery::originalTargetCurrent(original),zeroWakeActionsLocked())){
            state.phase="wake-proof-wait";state.reason="steam-entry-awaiting-current-interactive-wake";
            state.wakeAdmission["status"]="waiting-confirmed-kernel-wake";
            state.wakeAdmission["kernelWakeConfirmed"]=false;state.wakeAdmission["interactiveWakeConfirmed"]=false;
            g_sleepRecoveryAutoPending.store(false,std::memory_order_release);revokedProofOnly=true;continueFromCurrentFacts=true;
        }else if(sgSleepRetryActive()&&(state.phase=="waiting"||state.phase=="command-wait")&&flag(state.wakeAdmission,"kernelWakeConfirmed")&&
            state.wakeAdmission.value("source",std::string{})=="kernel-power-107-target-5"&&
            retiring.is_string()&&ymcc::sleepsteam::unsignedDecimal(retiring.get<std::string>(),retiringEvent)&&
            state.wakeContinuation.park(originalWakeIdentity(original),now,sgNowFileTime(),107,retiringEvent,
                SleepSteamRecovery::originalTargetCurrent(original),zeroWakeActionsLocked())){
            // Keep seen-generation and the first deadlines while retry settles.
            state.phase="wake-proof-wait";state.reason="steam-retry-awaiting-new-final-wake";
            state.wakeAdmission["status"]="waiting-confirmed-kernel-wake";
            state.wakeAdmission["kernelWakeConfirmed"]=false;state.wakeAdmission["interactiveWakeConfirmed"]=false;
            g_sleepRecoveryAutoPending.store(false,std::memory_order_release);revokedProofOnly=true;
        }else if(proofSupersededLocked()&&sleepSteamRecoveryAllows(gen,epoch)){
            // The public owner retains only this exact unused episode. Neither
            // a newer entry nor an uncertain receipt refunds its first clocks.
            if((state.phase=="waiting"||state.phase=="command-wait")&&retiring.is_string()&&
                ymcc::sleepsteam::unsignedDecimal(retiring.get<std::string>(),retiringEvent)&&
                state.wakeContinuation.parkSupersededEntry(originalWakeIdentity(original),now,sgNowFileTime(),
                    retiringEvent,kernelSleepEntry.eventFileTime,
                    SleepSteamRecovery::originalTargetCurrent(original),zeroWakeActionsLocked())){
                state.phase="wake-proof-wait";state.reason="steam-new-entry-invalidated-wake-proof";
                state.wakeAdmission["status"]="waiting-confirmed-kernel-wake";
                state.wakeAdmission["kernelWakeConfirmed"]=false;state.wakeAdmission["interactiveWakeConfirmed"]=false;
                state.wakeAdmission["supersededByEntryFileTime"]=std::to_string(kernelSleepEntry.eventFileTime);
                g_sleepRecoveryAutoPending.store(false,std::memory_order_release);revokedProofOnly=true;continueFromCurrentFacts=true;
            }else failure="steam-new-entry-after-action-no-replay";
        }
        if(revokedProofOnly){}else if(!failure.empty()){}else if(!sleepSteamRecoveryAllows(gen,epoch)||sgSleepRetryActive()||(!awaitingWakeCommit&&(g_powerLifecycle.load()==PowerLifecycle::Suspending||g_powerLifecycle.load()==PowerLifecycle::Suspended)))failure="cancelled-new-sleep-or-disabled";
        else if(now<state.armed||now-state.stageSince>45000)failure="automatic-worker-timeout";
        else if(now-state.armed>=ymcc::sleepsteam::kWakeObserveLimitMs)failure=state.settling.reason.empty()?"steam-wake-stability-not-confirmed":state.settling.reason;
        else if(state.phase=="waiting") {
            if(now-state.armed>=30000)failure="steam-power-not-ready";
            else if(steamWakeUsable){
                state.phase="inspecting";state.stageSince=now;submit=true;
            }
        } else if(state.phase=="game-wait") {
            if(now-state.armed>=30000)failure="steam-original-game-sleep-release-timeout";
            else if(now>=state.due){
                const auto pause=SleepSteamRecovery::capturedPauseState(gen);
                if(!flag(pause,"available")||flag(pause,"manualPauseHeld"))failure="steam-original-game-pause-state-changed";
                else if(flag(pause,"waitForSleepRelease"))state.due=now+750;
                else if(steamWakeUsable){
                    state.phase="inspecting";state.stageSince=now;state.reason.clear();submit=true;
                } else state.due=now+250; // Short readiness wait, never an overdue busy loop.
            }
        } else if(state.phase=="settle-wait"&&now-state.armed>=ymcc::sleepsteam::kWakeObserveLimitMs) {
            failure=state.settling.reason.empty()?"steam-wake-stability-not-confirmed":state.settling.reason;
        } else if((state.phase=="command-wait"||state.phase=="confirm-wait"||state.phase=="interaction-wait"||state.phase=="settle-wait"||state.phase=="session-wait")&&now>=state.due) {
            if(!steamWakeUsable)failure="steam-power-not-ready";
            else {state.phase="verifying";state.stageSince=now;submit=true;verify=true;}
        }
    }
    if(readinessNotice.is_object())sgRecordFact("sleep-steam-wake-readiness",{{"generation",gen},{"readiness",readinessNotice},{"repairTriggered",false},{"healthVerified",false}});
    if(revokedProofOnly){sgRecordFact("sleep-steam-wake-proof-revoked-before-action",{{"generation",gen},
        {"repairTriggered",false},{"actionReplayAllowed",false},{"awaitingFreshFinalWake",true},{"healthVerified",false}});notify();if(continueFromCurrentFacts)captureReady(gen);return;}
    if(!failure.empty()){finish(gen,epoch,"blocked",failure,json::object());return;}
    if(submit) {
        if(!poolSubmit([gen,epoch,verify]{run(gen,epoch,verify);}))finish(gen,epoch,"blocked","automatic-worker-queue-full",json::object());
        else notify();
    }
}
}
static bool sleepSteamWakeOperationAllows(unsigned long long generation,unsigned long long configEpoch){return SleepRecoveryAuto::wakeOperationAllows(generation,configEpoch);}
static void sleepRecoveryAutoCloseSuspendEpisode(){SleepSteamRecovery::closeSuspendEpisode();}
static void sleepRecoveryAutoPrepareSuspend(ULONGLONG originalBoundary=0){SleepSteamRecovery::prepareSuspend(originalBoundary);}
static void sleepRecoveryAutoCancelPreparedSuspend(){SleepSteamRecovery::cancelPreparedSuspend();}
static void sleepRecoveryAutoCaptureSuspend(unsigned long long generation,ULONGLONG originalBoundary=0) noexcept {
    // Steam capture may already have accepted work when it fails. Let the
    // remaining consumers and the shared Suspend queue continue without replay.
    const auto consume=[generation](const char* name,auto&& run) noexcept {
        try{run();}catch(...){try{
            sgRecordFact("sleep-suspend-consumer-failed",{{"consumer",name},{"generation",generation},
                {"businessCompletionProved",false},{"automaticReplay",false}});
        }catch(...){} }
    };
    consume("steam-original-capture",[&]{SleepSteamRecovery::captureSuspend(generation,{},originalBoundary);});
    consume("steam-wake-fact-join",[&]{SleepRecoveryAuto::captureReady(generation);});
    consume("steam-original-recovery",[&]{steamOriginalRecoverySuspend(generation);});
}
static void sleepRecoveryAutoArm(unsigned long long generation){SleepSteamRecovery::closeSuspendEpisode();SleepRecoveryAuto::arm(generation);steamOriginalRecoveryWake(generation);}
static void sleepRecoveryAutoRecordKernelWake(unsigned long long generation,int eventId,int reason,int targetState,ULONGLONG eventFileTime){SleepRecoveryAuto::recordKernelWake(generation,eventId,reason,targetState,eventFileTime);SleepRecoveryAuto::captureReady(generation);steamOriginalRecoveryPowerReady(generation);}
static void sleepRecoveryAutoInteractiveResume(unsigned long long generation,int code,ULONGLONG receiptTick){SleepRecoveryAuto::recordInteractiveResume(generation,code,receiptTick);SleepRecoveryAuto::captureReady(generation);steamOriginalRecoveryPowerReady(generation);}
static void sleepRecoveryAutoPowerReady(unsigned long long generation){SleepRecoveryAuto::powerReady(generation);steamOriginalRecoveryPowerReady(generation);}
static void sleepRecoveryAutoGameReleased(unsigned long long generation){
    std::lock_guard<std::mutex> lock(SleepRecoveryAuto::mx);
    auto& state=SleepRecoveryAuto::state;
    if(g_sleepRecoveryAutoPending.load(std::memory_order_acquire)&&state.generation==generation&&
        state.phase=="game-wait"&&sleepRecoveryAutoAllows(generation,state.configEpoch))
        state.due=GetTickCount64(); // Scheduling hint only; tick rereads the original lease.
}
static void sleepRecoveryAutoTick(){SleepRecoveryAuto::tick();}
static ULONGLONG sleepRecoveryAutoWaitMs(ULONGLONG now){return SleepRecoveryAuto::waitMs(now);}
