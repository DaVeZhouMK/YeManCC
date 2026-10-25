#pragma once
#include "sleep_steam_settle.h"
// Steam-only event-driven automatic owner. No InputHost/fan gates or actions.
// Protected UI is positively observed before spending a RETURN; no focus override.
// Only an explicit deduplicated wake arms it; bounded waits reuse sgWorkLoop.
namespace SleepRecoveryAuto {
using namespace ymcc::sleeprecovery;
struct State {
    unsigned long long generation=0,configEpoch=0,seenGeneration=0,seenBeforeAdmission=0;
    ULONGLONG armed=0,due=0,stageSince=0,wakeCandidateAt=0;
    std::string phase="idle",reason;
    json before,last,receipt,steamStatus,inputStatus,wakeAdmission,wakeReadiness,associationHistory,secondaryRejection,timeline=json::array();
    bool listAttempted=false,inputAttempted=false;unsigned inputDispatches=0;json preflightRejections=json::array();
    ymcc::sleepsteam::SettleWindow settling;
    unsigned postReturnForegroundSamples=0,initialWindowReadbackSamples=0;bool postReturnForegroundWait=false,postReturnDelayedWindowObserved=false,initialWindowReadbackWait=false;
    json postReturnObservationSummary() const {return {{"foregroundDeferredSamples",postReturnForegroundSamples},
        {"waitingOriginalForeground",postReturnForegroundWait},{"delayedOriginalWindowObserved",postReturnDelayedWindowObserved},
        {"waitingInitialOriginalWindow",initialWindowReadbackWait},{"initialWindowReadbackSamples",initialWindowReadbackSamples},{"returnReplayAllowed",false},
        {"gameInputVerified",false},{"continueButtonVerified",false}};}
};
static std::mutex mx,setMx,reportMx;
static State state;
struct KernelWake {unsigned long long generation=0,configEpoch=0;ULONGLONG boundaryFileTime=0,eventFileTime=0;int eventId=0;bool awaitedCapture=false,uiWakeRequested=false;unsigned long long requestGeneration=0;};
static KernelWake kernelWake;
struct KernelSleepEntry {unsigned long long configEpoch=0,generation=0;ULONGLONG eventFileTime=0;};
static KernelSleepEntry kernelSleepEntry;
static bool proofSupersededLocked() {
    const auto event=state.wakeAdmission.value("eventFileTime",json());std::uint64_t at=0;
    return flag(state.wakeAdmission,"kernelWakeConfirmed")&&event.is_string()&&ymcc::sleepsteam::unsignedDecimal(event.get<std::string>(),at)&&
        kernelSleepEntry.configEpoch==state.configEpoch&&kernelSleepEntry.eventFileTime&&at<=kernelSleepEntry.eventFileTime;
}

// Shared Resuming is NOT OS sleep. ROG can receive a valid final exit after
// the PBT Ready commit and be left Resuming by a duplicate shared wake. Steam
// owns no hardware writes and must not reopen that shared transaction. Allow
// its existing bounded operation only under its EXACT already-admitted final
// Kernel witness; no raw/PBT/intermediate/old proof grants this alternative.
static bool wakeOperationAllowedLocked(unsigned long long gen,unsigned long long epoch) {
    if(!sleepSteamRecoveryAllows(gen,epoch)||sgSleepRetryActive())return false;
    const auto phase=g_powerLifecycle.load();
    if(phase==PowerLifecycle::Ready)return true; // unchanged S3 / direct prechecks
    if(phase!=PowerLifecycle::Resuming||!g_sleepRecoveryAutoPending.load()||state.generation!=gen||state.configEpoch!=epoch||
        !flag(state.wakeAdmission,"kernelWakeConfirmed")||!flag(state.wakeAdmission,"proofFresh")||
        state.wakeAdmission.value("status",std::string())!="admitted"||proofSupersededLocked())return false;
    const auto source=state.wakeAdmission.value("source",std::string());
    if(source!="kernel-power-507-reason-1"&&source!="kernel-power-107-target-5")return false;
    std::uint64_t event=0,boundary=0;const auto at=state.wakeAdmission.value("eventFileTime",json()),sleep=state.wakeAdmission.value("boundaryFileTime",json());
    if(!at.is_string()||!sleep.is_string()||!ymcc::sleepsteam::unsignedDecimal(at.get<std::string>(),event)||
        !ymcc::sleepsteam::unsignedDecimal(sleep.get<std::string>(),boundary)||!boundary||event<=boundary)return false;
    // Keep the native witness in the join, not only exported admission JSON.
    if(kernelWake.configEpoch!=epoch||kernelWake.eventFileTime!=event||!kernelWake.uiWakeRequested||kernelWake.requestGeneration!=gen||
        kernelWake.generation!=number(state.wakeAdmission,"proofObservedGeneration")||
        (kernelWake.boundaryFileTime&&kernelWake.boundaryFileTime!=boundary)||
        (source=="kernel-power-507-reason-1"?kernelWake.eventId!=507:kernelWake.eventId!=107))return false;
    const auto now=GetTickCount64(),utc=sgNowFileTime();
    return now>=state.armed&&now-state.armed<ymcc::sleepsteam::kWakeObserveLimitMs&&event<=utc&&
        utc-event<=ymcc::sleepsteam::kWakeObserveLimitMs*10000ULL;
}
static bool wakeOperationAllows(unsigned long long gen,unsigned long long epoch) {
    std::lock_guard<std::mutex> lock(mx);return wakeOperationAllowedLocked(gen,epoch);
}
static json wakeReadinessLocked(unsigned long long gen,unsigned long long epoch) {
    const auto phase=g_powerLifecycle.load();const bool allowed=wakeOperationAllowedLocked(gen,epoch),sharedReady=phase==PowerLifecycle::Ready;
    return {{"operationAllowed",allowed},{"basis",allowed?sharedReady?"shared-ready":"bound-final-kernel-wake-during-resuming":"waiting-valid-steam-wake"},
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
        {"generation",state.generation},{"reason",state.reason},{"wakeAdmission",state.wakeAdmission},{"wakeReadiness",state.wakeReadiness},{"scope","steam"},{"steamRecovery",state.steamStatus},{"steamInputRecovery",state.inputStatus},{"healthVerified",false},{"logPath",W2U(reportPath())}};
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
static void notify() {ipc_emit("sleep.recovery-auto",get());}
static bool current(unsigned long long gen,unsigned long long epoch,const char* phase) {
    std::lock_guard<std::mutex> lock(mx);
    return sleepSteamRecoveryAllows(gen,epoch)&&state.generation==gen&&state.configEpoch==epoch&&state.phase==phase&&!proofSupersededLocked()&&wakeOperationAllowedLocked(gen,epoch)&&GetTickCount64()>=state.armed&&GetTickCount64()-state.armed<ymcc::sleepsteam::kWakeObserveLimitMs;
}
static void finish(unsigned long long gen,unsigned long long epoch,const std::string& phase,const std::string& reason,json evidence) {
    {
        std::lock_guard<std::mutex> lock(mx);
        if(state.generation!=gen||state.configEpoch!=epoch||!g_sleepRecoveryAutoPending.load())return;
        if(!evidence.contains("before")&&state.before.is_object())evidence["before"]=state.before;
        if(!evidence.contains("after")&&state.last.is_object())evidence["after"]=state.last;
        if(!evidence.contains("receipt")&&state.receipt.is_object())evidence["receipt"]=state.receipt;
        evidence["postReturnObservation"]=state.postReturnObservationSummary();
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
        {"feedbackSaved",saved},{"healthVerified",false},{"uploaded",false},{"wakeReadiness",state.wakeReadiness},
        {"attempts",{{"list",state.listAttempted?1:0},{"input",state.inputAttempted?1:0},{"inputDispatches",state.inputDispatches}}}});
    {
        std::lock_guard<std::mutex> lock(mx);
        if(state.generation!=gen||state.configEpoch!=epoch)return;
        if(!saved&&sleepSteamRecoveryAllows(gen,epoch)){state.phase="blocked";state.reason="log-write-failed";}
    }
    notify();
}
static json set(const json& a) {
    const auto it=a.find("enabled");if(it==a.end()||!it->is_boolean())throw std::runtime_error("automatic-recovery-enabled-must-be-boolean");
    const bool enabled=it->get<bool>();std::lock_guard<std::mutex> serial(setMx);
    if(enabled!=g_sleepRecoveryAutoEnabled.load()) {
        // Persist before publishing admission. Enabling NEVER schedules a repair.
        if(!ymSettingsPatchSection("sleep",{{"autoWakeRecovery",enabled}}))
            return {{"ok",false},{"reason","automatic-recovery-preference-save-failed"}};
        g_sleepRecoveryAutoConfigEpoch.fetch_add(1,std::memory_order_acq_rel);
        g_sleepRecoveryAutoEnabled.store(enabled,std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(mx);
            state.phase=enabled?"idle":"off";state.reason.clear();state.steamStatus=json::object();state.inputStatus=json::object();
            g_sleepRecoveryAutoPending.store(false,std::memory_order_release);
        }
        sgRecordFact("sleep-recovery-automatic-preference",{{"enabled",enabled},{"trigger","future-explicit-wake-only"}});
        g_sgWorkCv.notify_one();notify();
    }
    return get();
}
// Kernel subscriber only records a bounded Steam-specific witness. It never
// arms a worker, touches a game/device or changes shared power classification.
static void recordKernelWake(unsigned long long gen,int eventId,int reason,int targetState,ULONGLONG eventFileTime) {
    const auto epochNow=g_sleepRecoveryAutoConfigEpoch.load();
    if(eventId==506){
        // A later TRUE entry invalidates an earlier exit even if shared capture
        // generation has not caught up. Original event time wins over receipt order.
        if(!sleepSteamRecoveryAllows(gen,epochNow)||!eventFileTime||eventFileTime>sgNowFileTime())return;
        bool changed=false;
        {std::lock_guard<std::mutex> lock(mx);if(sleepSteamRecoveryAllows(gen,epochNow)&&
            (kernelSleepEntry.configEpoch!=epochNow||eventFileTime>kernelSleepEntry.eventFileTime)){
            kernelSleepEntry={epochNow,gen,eventFileTime};changed=true;}}
        if(changed)sgRecordFact("sleep-steam-kernel-entry-witness",{{"generation",gen},{"eventFileTime",std::to_string(eventFileTime)},
            {"candidateOnly",true},{"repairTriggered",false},{"healthVerified",false}});
        return;
    }
    if(!((eventId==507&&reason==1)||(eventId==107&&targetState==5)))return;
    const auto original=SleepSteamRecovery::wakeBoundaryFor(gen);
    const auto actualCapture=SleepSteamRecovery::capturedFor(gen);
    const auto epoch=g_sleepRecoveryAutoConfigEpoch.load();
    if(!sleepSteamRecoveryAllows(gen,epoch))return;
    const auto nowFileTime=sgNowFileTime();
    const char* source=eventId==507?"kernel-power-507-reason-1":"kernel-power-107-target-5";
    const bool captured=actualCapture.generation==gen&&actualCapture.configEpoch==epoch&&original.powerBoundaryFileTime!=0&&actualCapture.powerBoundaryFileTime==original.powerBoundaryFileTime;
    if(!eventFileTime||eventFileTime>nowFileTime||(original.powerBoundaryFileTime&&eventFileTime<=original.powerBoundaryFileTime)) {
        sgRecordFact("sleep-steam-wake-proof-rejected",{{"generation",gen},{"source",source},{"reason","kernel-event-outside-original-sleep-boundary"},
            {"boundaryFileTime",std::to_string(original.powerBoundaryFileTime)},{"eventFileTime",std::to_string(eventFileTime)},{"healthVerified",false}});return;
    }
    {
        std::lock_guard<std::mutex> lock(mx);
        if(!sleepSteamRecoveryAllows(gen,epoch))return;
        if(nowFileTime-eventFileTime>ymcc::sleepsteam::kWakeObserveLimitMs*10000ULL||
            (kernelSleepEntry.configEpoch==epoch&&eventFileTime<=kernelSleepEntry.eventFileTime)){
            sgRecordFact("sleep-steam-wake-proof-rejected",{{"generation",gen},{"reason","kernel-exit-stale-or-before-new-entry"},
                {"eventFileTime",std::to_string(eventFileTime)},{"latestEntryFileTime",std::to_string(kernelSleepEntry.eventFileTime)},{"healthVerified",false}});return;
        }
        if(kernelWake.generation==gen&&kernelWake.configEpoch==epoch&&kernelWake.eventFileTime>=eventFileTime)return;
        // A final kernel event may precede the capture worker. Retain ONE raw
        // candidate, not authority; only arm() can bind it to a real boundary.
        kernelWake={gen,epoch,original.powerBoundaryFileTime,eventFileTime,eventId,!captured,false,0};
    }
    sgRecordFact(captured?"sleep-steam-wake-proof-recorded":"sleep-steam-wake-proof-await-capture",{{"generation",gen},{"source",source},
        {"boundaryFileTime",std::to_string(original.powerBoundaryFileTime)},{"eventFileTime",std::to_string(eventFileTime)},
        {"captureMatched",captured},{"candidateOnly",!captured},{"repairTriggered",false},{"healthVerified",false}});
}
static void arm(unsigned long long gen) {
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
        if(kernelWake.eventFileTime&&kernelWake.configEpoch==epoch&&
            (kernelWake.generation==gen||(kernelWake.generation!=ULLONG_MAX&&kernelWake.generation+1==gen&&original.boundaryGeneration==kernelWake.generation))){
            kernelWake.uiWakeRequested=true;kernelWake.requestGeneration=gen;
        }
        if(state.seenGeneration==gen)return;
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
        admission={{"status",modernStandby&&!confirmed?"waiting-confirmed-kernel-wake":"admitted"},
            {"source",confirmed?(kernelWake.eventId==507?"kernel-power-507-reason-1":"kernel-power-107-target-5"):modernStandby?"pbt-intermediate-resume-not-proof":"pbt-resume-suspend-s3"},
            {"modernStandby",modernStandby},{"kernelWakeConfirmed",confirmed},{"boundaryFileTime",std::to_string(original.powerBoundaryFileTime)},
            {"boundaryGeneration",original.boundaryGeneration},{"proofObservedGeneration",kernelWake.generation},
            {"proofOriginallyAwaitedCapture",confirmed&&kernelWake.awaitedCapture},{"proofFollowedPreparedBoundary",confirmed&&transferredBoundary},
            {"eventFileTime",confirmed?json(std::to_string(kernelWake.eventFileTime)):json()},
            {"pbtObservedFileTime",!modernStandby&&!confirmed?json(std::to_string(proofNow)):json()},
            {"proofFresh",freshProof},{"proofMaximumAgeMs",ymcc::sleepsteam::kWakeObserveLimitMs},
            {"latestEntryFileTime",kernelSleepEntry.configEpoch==epoch?json(std::to_string(kernelSleepEntry.eventFileTime)):json()},
            {"gameInputVerified",false},{"continueButtonVerified",false}};
        // S0 emits RESUMESUSPEND even while entering standby. Do not start the
        // 60s budget or consume the generation/RETURN on that intermediate PBT.
        // S3 lacks 506/507; its documented PBT path is intentionally retained.
        deferred=modernStandby&&!confirmed;
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
            state={};state.seenBeforeAdmission=previousSeen;state.seenGeneration=gen;state.generation=gen;state.configEpoch=epoch;
            state.armed=state.stageSince=GetTickCount64();state.wakeAdmission=admission;
            state.phase="waiting";g_sleepRecoveryAutoPending.store(true,std::memory_order_release);changed=true;
        }
    }
    if(!changed)return;
    sgRecordFact(deferred?"sleep-steam-wake-await-kernel":"sleep-recovery-automatic-armed",
        {{"generation",gen},{"trigger",admission["source"]},{"wakeAdmission",admission},{"repairTriggered",!deferred},{"healthVerified",false}});
    if(!deferred)g_sgWorkCv.notify_one();notify();
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
        const auto epoch=g_sleepRecoveryAutoConfigEpoch.load();
        const bool requestedCurrent=state.generation==gen;
        const bool requestedPrepared=state.generation!=0&&state.generation!=ULLONG_MAX&&state.generation+1==gen&&original.boundaryGeneration==state.generation;
        const bool witnessedRequest=kernelWake.uiWakeRequested&&kernelWake.configEpoch==epoch&&
            (kernelWake.requestGeneration==gen||(kernelWake.requestGeneration!=ULLONG_MAX&&kernelWake.requestGeneration+1==gen&&original.boundaryGeneration==kernelWake.requestGeneration));
        requested=sleepSteamRecoveryAllows(gen,epoch)&&original.generation==gen&&original.configEpoch==epoch&&kernelWake.eventFileTime!=0&&
            (witnessedRequest||(state.phase=="wake-proof-wait"&&state.configEpoch==epoch&&(requestedCurrent||requestedPrepared)));
    }
    if(requested)arm(gen);
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
            {std::lock_guard<std::mutex> lock(mx);if(state.generation!=gen||state.configEpoch!=epoch||state.phase!=expected)return;state.listAttempted=true;}
            receipt=SleepSteamRecovery::reconcile(report,gen,epoch,stillCurrent);
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
            {std::lock_guard<std::mutex> lock(mx);if(state.generation!=gen||state.configEpoch!=epoch||state.phase!=expected)return;state.inputAttempted=true;++state.inputDispatches;}
            auto inputReceipt=SleepSteamRecovery::resumeInputContext(report,gen,epoch,stillCurrent);
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
static void tick() {
    if(!g_sleepRecoveryAutoPending.load(std::memory_order_acquire))return;
    unsigned long long gen=0,epoch=0;bool verify=false,submit=false,revokedProofOnly=false;json readinessNotice;std::string failure;
    {
        std::lock_guard<std::mutex> lock(mx);gen=state.generation;epoch=state.configEpoch;
        const auto now=GetTickCount64();
        const auto readiness=wakeReadinessLocked(gen,epoch);
        if(flag(readiness,"operationAllowed")&&(!state.wakeReadiness.is_object()||
            state.wakeReadiness.value("basis",std::string())!=readiness.value("basis",std::string())))readinessNotice=readiness;
        state.wakeReadiness=readiness;
        const bool steamWakeUsable=flag(readiness,"operationAllowed");
        const bool awaitingWakeCommit=state.phase=="waiting"&&flag(state.wakeAdmission,"kernelWakeConfirmed");
        if(proofSupersededLocked()&&sleepSteamRecoveryAllows(gen,epoch)){
            // Only a cycle with ZERO reservations/dispatches may surrender its
            // seen-generation on newer entry proof. Any attempted mutation,
            // timeout or uncertain receipt can never obtain another RETURN.
            if(!state.listAttempted&&!state.inputAttempted&&state.inputDispatches==0){
                state.seenGeneration=state.seenBeforeAdmission;state.phase="wake-proof-wait";
                state.reason="steam-new-entry-invalidated-wake-proof";state.armed=state.stageSince=state.due=0;
                state.wakeAdmission["status"]="waiting-confirmed-kernel-wake";
                state.wakeAdmission["kernelWakeConfirmed"]=false;
                state.wakeAdmission["supersededByEntryFileTime"]=std::to_string(kernelSleepEntry.eventFileTime);
                g_sleepRecoveryAutoPending.store(false,std::memory_order_release);revokedProofOnly=true;
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
                }
            }
        } else if(state.phase=="settle-wait"&&now-state.armed>=ymcc::sleepsteam::kWakeObserveLimitMs) {
            failure=state.settling.reason.empty()?"steam-wake-stability-not-confirmed":state.settling.reason;
        } else if((state.phase=="confirm-wait"||state.phase=="interaction-wait"||state.phase=="settle-wait"||state.phase=="session-wait")&&now>=state.due) {
            if(!steamWakeUsable)failure="steam-power-not-ready";
            else {state.phase="verifying";state.stageSince=now;submit=true;verify=true;}
        }
    }
    if(readinessNotice.is_object())sgRecordFact("sleep-steam-wake-readiness",{{"generation",gen},{"readiness",readinessNotice},{"repairTriggered",false},{"healthVerified",false}});
    if(revokedProofOnly){sgRecordFact("sleep-steam-wake-proof-revoked-before-action",{{"generation",gen},
        {"repairTriggered",false},{"actionReplayAllowed",false},{"awaitingFreshFinalWake",true},{"healthVerified",false}});notify();return;}
    if(!failure.empty()){finish(gen,epoch,"blocked",failure,json::object());return;}
    if(submit) {
        if(!poolSubmit([gen,epoch,verify]{run(gen,epoch,verify);}))finish(gen,epoch,"blocked","automatic-worker-queue-full",json::object());
        else notify();
    }
}
}
static bool sleepSteamWakeOperationAllows(unsigned long long generation,unsigned long long configEpoch){return SleepRecoveryAuto::wakeOperationAllows(generation,configEpoch);}
static void sleepRecoveryAutoCloseSuspendEpisode(){SleepSteamRecovery::closeSuspendEpisode();}
static void sleepRecoveryAutoPrepareSuspend(){SleepSteamRecovery::prepareSuspend();}
static void sleepRecoveryAutoCancelPreparedSuspend(){SleepSteamRecovery::cancelPreparedSuspend();}
static void sleepRecoveryAutoCaptureSuspend(unsigned long long generation){SleepSteamRecovery::captureSuspend(generation);SleepRecoveryAuto::captureReady(generation);steamOriginalRecoverySuspend(generation);}
static void sleepRecoveryAutoArm(unsigned long long generation){SleepSteamRecovery::closeSuspendEpisode();SleepRecoveryAuto::arm(generation);steamOriginalRecoveryWake(generation);}
static void sleepRecoveryAutoRecordKernelWake(unsigned long long generation,int eventId,int reason,int targetState,ULONGLONG eventFileTime){SleepRecoveryAuto::recordKernelWake(generation,eventId,reason,targetState,eventFileTime);steamOriginalRecoveryPowerReady(generation);}
static void sleepRecoveryAutoGameReleased(unsigned long long generation){
    std::lock_guard<std::mutex> lock(SleepRecoveryAuto::mx);
    auto& state=SleepRecoveryAuto::state;
    if(g_sleepRecoveryAutoPending.load(std::memory_order_acquire)&&state.generation==generation&&
        state.phase=="game-wait"&&sleepRecoveryAutoAllows(generation,state.configEpoch))
        state.due=GetTickCount64(); // Scheduling hint only; tick rereads the original lease.
}
static void sleepRecoveryAutoTick(){SleepRecoveryAuto::tick();}
