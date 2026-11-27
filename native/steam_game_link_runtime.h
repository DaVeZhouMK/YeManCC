#pragma once
#include "worker_thread_name.h"
#include "steam_game_link_state.h"
#include "steam_game_link_wait_policy.h"
#include "steam_game_link_window_observation.h"
#include "steam_game_link_log_policy.h"
#include "steam_game_link_log_watch.h"
#include "steam_native_focus_feedback.h"
#include "steam_app_lifetime_feedback.h"
#include "steam_overlay_popup_cache_diagnostic.h"
#include "steam_overlay_provider_cache_diagnostic.h"
#include <deque>
#include <functional>
#include <memory>
#include <utility>
#include "steam_native_current_probe.h"
#include "steam_shortcut_metadata_baseline.h"
#include "steam_startup_handoff.h"
#include "steam_startup_capture_diagnostic.h"
#include "steam_running_group_role_observation.h"
// Independent read-only running-game association. Never consumes a sleep capture,
// changes the shared game valve, writes Steam state, or authorizes recovery.
namespace SteamGameLink {
using namespace ymcc::steamlink;
using namespace ymcc::sleepsteam;
using Game = SleepSteamRecovery::Game;
using Client = SleepSteamRecovery::Session;
struct Handle {
    HANDLE value=nullptr;
    Handle()=default; explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other)noexcept:value(std::exchange(other.value,nullptr)){}
    Handle& operator=(Handle&& other)noexcept{if(this!=&other){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=std::exchange(other.value,nullptr);}return *this;}
};
struct StartupCaptureDiagnostic {
    bool known=false,retired=false;
    const char* retirementReason="";
    ymcc::steamgamefacts::StartupLaunchCapture capture;
    unsigned attemptCount=0,diagnosticReportsRemaining=16;
    ULONGLONG firstAttemptAtTick=0,lastAttemptAtTick=0,lastDurationMs=0,lastDirectoryAtTick=0;
    ULONGLONG discoveryStartedAtTick=0,discoveryFinishedAtTick=0,discoveryDurationMs=0;
    bool logPending=false;
    json clientDiscovery; // Bounded original reply diagnostics; never account or action authority.
};
struct Control {
    std::atomic<bool> stop{false};HANDLE stopEvent=nullptr,pokeEvent=nullptr;
    std::mutex observationMx;
    std::deque<std::function<void()>> observationJobs;
    std::atomic<bool> observationsClosed{false};
    std::atomic<bool> observationWorkerExited{false};
    std::atomic<unsigned> hintsPending{0};
    bool hintWakeSucceeded=false; // Accessed only by hint producers under lifecycleMx.
    std::mutex snapshotMx;json snapshot={{"schemaVersion",1},{"phase","idle"},{"linked",false},{"readOnly",true},{"repairAuthorized",false}};
    json history=json::array();
    StartupCaptureDiagnostic startupCaptureDiagnostic; // Protected by snapshotMx; diagnostics only.
    ~Control(){if(stopEvent)CloseHandle(stopEvent);if(pokeEvent)CloseHandle(pokeEvent);}
};
static json startupCaptureValue(const StartupCaptureDiagnostic& value) {
    using Status=ymcc::steamgamefacts::StartupLaunchCapture::Status;
    const char* status="unavailable";
    switch(value.capture.status){
    case Status::Unavailable:break;
    case Status::Busy:status="busy";break;
    case Status::Observed:status="observed";break;
    case Status::ContextChanged:status="context-changed";break;
    case Status::SourceIncomplete:status="source-incomplete";break;
    case Status::Capacity:status="capacity";break;
    }
    json out={{"known",value.known},{"retired",value.retired},{"retirementReason",value.retirementReason},
        {"status",status},{"sourceRevision",value.capture.sourceRevision},
        {"considered",value.capture.considered},{"captured",value.capture.captured},
        {"reused",value.capture.reused},{"retained",value.capture.retained},
        {"actionAuthority",false},{"currentEvidenceProven",false},
        {"attemptCount",value.attemptCount},{"firstAttemptAtTick",value.firstAttemptAtTick},
        {"lastAttemptAtTick",value.lastAttemptAtTick},{"lastDurationMs",value.lastDurationMs},
        {"lastDirectoryAtTick",value.lastDirectoryAtTick},{"discoveryStartedAtTick",value.discoveryStartedAtTick},
        {"discoveryFinishedAtTick",value.discoveryFinishedAtTick},{"discoveryDurationMs",value.discoveryDurationMs},
        {"diagnosticReportsRemaining",value.diagnosticReportsRemaining}};
    if(value.clientDiscovery.is_object())out.update(value.clientDiscovery);
    return out;
}
static void noteStartupCapture(const std::shared_ptr<Control>& control,
    const ymcc::steamgamefacts::StartupLaunchCapture& value,ULONGLONG startedAtTick) noexcept {
    try{const auto finishedAtTick=GetTickCount64();std::lock_guard<std::mutex> lock(control->snapshotMx);
        auto& previous=control->startupCaptureDiagnostic;
        const bool changed=!previous.known||previous.retired||previous.capture.status!=value.status||
            previous.capture.considered!=value.considered||previous.capture.captured!=value.captured||
            previous.capture.reused!=value.reused||previous.capture.retained!=value.retained;
        previous.logPending=previous.logPending||changed;previous.known=true;previous.retired=false;previous.retirementReason="";previous.capture=value;
        if(previous.attemptCount!=MAXDWORD)++previous.attemptCount;
        if(!previous.firstAttemptAtTick)previous.firstAttemptAtTick=startedAtTick;
        previous.lastAttemptAtTick=startedAtTick;previous.lastDurationMs=finishedAtTick>=startedAtTick?finishedAtTick-startedAtTick:0;
    }catch(...){} // Optional diagnostics never affect capture or admission.
}
static void retireStartupCaptureDiagnostic(const std::shared_ptr<Control>& control,const char* reason) noexcept {
    try{std::lock_guard<std::mutex> lock(control->snapshotMx);
        auto& value=control->startupCaptureDiagnostic;
        value.logPending=value.logPending||!value.retired||std::strcmp(value.retirementReason,reason)!=0;
        value.retired=true;value.retirementReason=reason;
    }catch(...){}
}
// Same existing worker only. These timestamps describe observed call boundaries,
// not OS event arrival, CPU cost or permission; optional failures remain inert.
static void noteStartupDirectory(const std::shared_ptr<Control>& control,ULONGLONG at) noexcept {
    try{std::lock_guard<std::mutex> lock(control->snapshotMx);control->startupCaptureDiagnostic.lastDirectoryAtTick=at;}catch(...){}
}
static void noteStartupDiscovery(const std::shared_ptr<Control>& control,ULONGLONG started,ULONGLONG finished) noexcept {
    try{std::lock_guard<std::mutex> lock(control->snapshotMx);auto& value=control->startupCaptureDiagnostic;
        value.discoveryStartedAtTick=started;value.discoveryFinishedAtTick=finished;
        value.discoveryDurationMs=finished>=started?finished-started:0;}catch(...){}
}
struct StartupDiscoveryDiagnosticScope final {
    const std::shared_ptr<Control>& control;const ULONGLONG started=GetTickCount64();
    explicit StartupDiscoveryDiagnosticScope(const std::shared_ptr<Control>& value):control(value){noteStartupDiscovery(control,started,0);}
    ~StartupDiscoveryDiagnosticScope(){noteStartupDiscovery(control,started,GetTickCount64());}
};
static std::mutex lifecycleMx;
static std::shared_ptr<Control> control;
static std::thread worker;
static std::atomic<unsigned long long> nextSerial{1};
static IdentityStatus identity(HANDLE h,DWORD pid,ULONGLONG birth,const std::wstring& path,DWORD session) {
    if(!h)return IdentityStatus::Unknown;
    const auto wait=WaitForSingleObject(h,0);
    if(wait==WAIT_OBJECT_0)return IdentityStatus::Exited;
    if(wait!=WAIT_TIMEOUT)return IdentityStatus::Unknown;
    const auto observedBirth=nativeProcessCreatedFromHandle(h);
    std::vector<wchar_t> image(32768);DWORD length=static_cast<DWORD>(image.size()),observedSession=0;
    if(!observedBirth||!QueryFullProcessImageNameW(h,0,image.data(),&length)||!ProcessIdToSessionId(pid,&observedSession))return IdentityStatus::Unknown;
    return observedBirth==birth&&pathKey(W2U(std::wstring(image.data(),length)))==pathKey(W2U(path))&&observedSession==session?IdentityStatus::Same:IdentityStatus::Changed;
}
static Handle pin(DWORD pid,ULONGLONG birth,const std::wstring& path,DWORD session) {
    Handle result(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));
    if(identity(result.value,pid,birth,path,session)!=IdentityStatus::Same)return {};
    return result;
}
static std::string stamp(const std::wstring& file) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if(!GetFileAttributesExW(file.c_str(),GetFileExInfoStandard,&data))return "unavailable:"+std::to_string(GetLastError());
    return std::to_string(data.ftLastWriteTime.dwHighDateTime)+":"+std::to_string(data.ftLastWriteTime.dwLowDateTime)+":"+
        std::to_string(data.nFileSizeHigh)+":"+std::to_string(data.nFileSizeLow);
}
struct TrackerRead {std::string gameId;Binding binding;json primary=json::object(),log=json::object();bool complete=false;
    // Process/topology evidence only. Never serialized or reconstructed from JSON.
    std::shared_ptr<ymcc::steamgamefacts::ProcessGroupLease> processGroup;
    std::shared_ptr<const ymcc::steamgamefacts::StartupHandoffEvidence> startupHandoff;
    ymcc::steamgamefacts::TrackerOwnerCurrent startupHandoffCurrent;
    std::shared_ptr<const ymcc::steamgamefacts::HistoricalStartupHandoffEvidence> historicalStartupHandoff;
    ymcc::steamgamefacts::TrackerOwnerCurrent historicalStartupHandoffCurrent;
};
static TrackerRead tracking(const Client& client,const Game& game,TrackingScope scope=TrackingScope::ProcessLifetime,
    ymcc::steamgamefacts::TrackerGroupCapturePolicy groupPolicy=ymcc::steamgamefacts::TrackerGroupCapturePolicy::None,
    const ymcc::steamgamefacts::TrackerOwnerCurrent& requestCurrent={},NativeSteamTrackerFreshCompletion* completion=nullptr) {
    TrackerRead result;
    const auto logScope=scope==TrackingScope::RunningListAndProcess
        ?ymcc::steamgamefacts::TrackerLogScope::RunningListAndProcess
        :ymcc::steamgamefacts::TrackerLogScope::ProcessLifetime;
    json freshDiagnostic;
    const auto fresh=nativeSteamGameTrackerReadFresh(client,game,logScope,&freshDiagnostic,groupPolicy,requestCurrent,completion);

    // Keep the original gameprocess tail metadata, including rejected reads.
    if(fresh.tracker)
        result.log=ymcc::steamgamefacts::trackerSourceMetadata(fresh.tracker->observation);
    try{if(freshDiagnostic.is_object())result.log["sharedFresh"]=std::move(freshDiagnostic);}catch(...){}
    // Diagnostic memory only, sampled by this existing selected-target read.
    // PID slots never supply a process birth, source identity or action right.
    try {
        const auto observed=ymcc::steamgamefacts::startupdiagnostic::ledger().snapshot(game.pid);
        json stages=json::object();
        constexpr std::array<const char*,6> names{"observer","cache","main","parent","retain","join"};
        static_assert(names.size()==static_cast<unsigned>(ymcc::steamgamefacts::startupdiagnostic::Stage::Count));
        for(std::size_t index=0;index<names.size();++index){const auto& item=observed.stages[index];
            stages[names[index]]={{"count",item.count},{"first",item.first?json(item.first):json(nullptr)},
                {"last",item.last?json(item.last):json(nullptr)},{"firstTick",item.firstTick},{"lastTick",item.lastTick}};
        }
        result.log["startupCapture"]={{"schema","ymcc.startup-capture-memory-diagnostic.v1"},
            {"pid",observed.pid},{"present",observed.present},{"readBusy",observed.readBusy},
            {"evictions",observed.evictions},{"dropped",observed.dropped},{"stages",std::move(stages)},
            {"processBirthProven",false},{"actionAuthority",false}};
    }catch(...){}
    if(!fresh.tracker||!fresh.tracker->parsedSource)return result;

    const auto& parsed=*fresh.tracker->parsedSource;
    const auto& selected=logScope==ymcc::steamgamefacts::TrackerLogScope::RunningListAndProcess
        ?parsed.runningList:parsed.processLifetime;
    if(!selected.inventory.complete)return result;

    // Preserve every member: unique-primary depends on the full AppID/PID set.
    // nativePrimaryJoin does not consume the inventory's path or timestamp.
    ymcc::sleepsteam::TrackedProcessInventory inventory;
    inventory.complete=selected.inventory.complete;
    inventory.reason=selected.inventory.reason;
    inventory.rows.reserve(selected.inventory.members.size());
    for(const auto& member:selected.inventory.members)
        inventory.rows.push_back({member.appid,member.pid,{},{}});

    result.binding.status=selected.binding.status;
    result.binding.at=selected.binding.at;
    result.binding.appid=selected.binding.appid;
    result.binding.line=selected.binding.sourceLine;
    result.binding.exactPath=selected.binding.exactPath;
    if(result.binding.status=="tracked")result.gameId=selected.binding.gameIdRaw;

    // Parsed binding is an observation even when the shared transaction did
    // not finish. All consumers retain complete before any association/action.
    if(!fresh.complete||!fresh.nativePrimary)return result;
    // Retain the real owner-issued source separately; no synthetic child log row.
    result.startupHandoff=fresh.startupHandoff;
    result.startupHandoffCurrent=fresh.startupHandoffCurrent;
    result.historicalStartupHandoff=fresh.historicalStartupHandoff;
    result.historicalStartupHandoffCurrent=fresh.historicalStartupHandoffCurrent;

    // Reuse the original unique-tracker fallback and native verified verdicts.
    // Freshness and the console boundary belong to the shared facade.
    result.primary=SleepSteamRecovery::nativePrimaryJoin(
        {*fresh.nativePrimary,ymcc::steamgamefacts::trackerSourceMetadata(fresh.consoleObservation)},
        inventory,result.binding.appid,game.pid);
    result.complete=fresh.complete&&inventory.complete&&static_cast<bool>(fresh.nativePrimary);
    // Keep the original primary booleans untouched. Only explicitly demanded
    // one- or two-object public evidence is retained for original cancellation and native-role checks.
    const auto group=fresh.processGroupCapture?fresh.processGroupCapture->lease:nullptr;
    if(result.complete&&group&&(group->graph().count==1||group->graph().count==2)&&group->source().appid==result.binding.appid&&
        std::to_string(group->source().fullGameId)==result.gameId&&
        group->selectedGame().pid==game.pid&&group->selectedGame().created==game.created&&
        ymcc::steamgamefacts::samePath(group->selectedGame().image,game.path)&&
        group->steam().pid==client.pid&&group->steam().created==client.created&&
        ymcc::steamgamefacts::samePath(group->steam().image,client.root+L"\\steam.exe")&&group->processCurrent())
        result.processGroup=group;
    return result;
}
// Exact already-captured sleep target. Reuse the public owner's two observations;
// this path never enters a global fact/selection receipt or elects a replacement.
static TrackerRead trackingOriginal(const Client& client,const Game& game,
    const std::shared_ptr<ymcc::steamgamefacts::ProcessLease>& selected,
    const ymcc::steamgamefacts::TrackerOwnerCurrent& requestCurrent){
    TrackerRead result;
    const auto observed=nativeSteamOriginalProcessGroup({client.pid,client.created,client.root},selected,requestCurrent);
    if(!observed||!observed->sourcesComplete()||!observed->parsedSource()||!observed->nativePrimary()||
        !observed->gameSource()||!observed->consoleSource()||!ymcc::steamgamefacts::tracker_detail::ownerCurrent(requestCurrent))return result;
    const auto& facts=observed->parsedSource()->processLifetime;
    result.log=observed->gameSource()->meta();
    result.binding={facts.binding.status,facts.binding.at,facts.binding.appid,facts.binding.sourceLine,facts.binding.exactPath};
    if(result.binding.status=="tracked")result.gameId=facts.binding.gameIdRaw;
    ymcc::sleepsteam::TrackedProcessInventory inventory;inventory.complete=facts.inventory.complete;inventory.reason=facts.inventory.reason;
    for(const auto& member:facts.inventory.members)inventory.rows.push_back({member.appid,member.pid,{},{}});
    result.primary=SleepSteamRecovery::nativePrimaryJoin({*observed->nativePrimary(),observed->consoleSource()->meta()},inventory,result.binding.appid,game.pid);
    result.complete=facts.inventory.complete&&result.binding.status=="tracked"&&facts.binding.fullGameIdKnown;
    const auto capture=observed->processCapture();
    const auto group=capture?capture->lease:nullptr;
    // Never downgrade a multi-member group to empty merely because it is not count2.
    // The final engine keeps its original strict path outside the new count2 route.
    if(group&&group->graph().count>=1&&group->source().appid==result.binding.appid&&
        std::to_string(group->source().fullGameId)==result.gameId&&group->selectedGame().pid==game.pid&&
        group->selectedGame().created==game.created&&ymcc::steamgamefacts::samePath(group->selectedGame().image,game.path)&&
        group->steam().pid==client.pid&&group->steam().created==client.created&&group->processCurrent())result.processGroup=group;
    if(!ymcc::steamgamefacts::tracker_detail::ownerCurrent(requestCurrent)){
        if(result.processGroup)result.processGroup->cancel();result.complete=false;
    }
    return result;
}
static ValidatorStatus validate(const Game& game,json& receipt) {
    const auto rules=SleepSteamRecovery::captureGameRules();NativeDetectedGame found;
    const auto verdict=nativeScanGameOnePid(game.pid,L"",rules.excludes,rules.whitelist,rules.configured,&found);
    const bool accepted=verdict==NativeOnePidResult::Ok&&SleepSteamRecovery::sameGameIdentity(game,{found.pid,found.processCreated,found.path})&&
        g_gameRulesEpoch.load(std::memory_order_acquire)==rules.epoch;
    receipt={{"source","nativeScanGameOnePid"},{"pid",game.pid},{"created",std::to_string(game.created)},
        {"rulesEpoch",std::to_string(rules.epoch)},{"accepted",accepted},{"exactProcessIdentity",accepted}};
    if(verdict==NativeOnePidResult::NameUnavailable||g_gameRulesEpoch.load(std::memory_order_acquire)!=rules.epoch)return ValidatorStatus::Unknown;
    return accepted?ValidatorStatus::Accepted:ValidatorStatus::Rejected;
}
static bool sameClientProcess(const Client& client) {
    const auto current=SleepSteamRecovery::session();
    return SleepSteamRecovery::sameClientProcess(client,current);
}
// Optional code-only diagnostics from the same original call. Never export the
// account, raw reply, endpoint or request. Malformed/oversized codes are omitted.
static void clientDiscoveryDiagnostic(json* output,const char* outcome,const std::string& sessionReason,const json* reply=nullptr) noexcept {
    if(!output)return;
    try{
        const auto code=[](const json& source,const char* key)->std::string{
            const auto it=source.find(key);if(it==source.end()||!it->is_string())return {};
            const auto& text=it->get_ref<const std::string&>();
            static constexpr const char* codes[]={"isolated-session","invalid-live-operation","steam-process-unverified",
                "steam-observer-unavailable","steam-process-ambiguous","steam-observer-process-not-current",
                "steam-session-starting","steam-session-changed","steam-account-changed","steam-account-not-ready",
                "steam-native-session-current","steam-session-process-witness-required","steam-command-busy",
                "live-action-cancelled","live-busy","live-read-unavailable","live-context-unavailable",
                "live-api-unavailable","live-api-error","live-timeout","live-discovery-timeout","live-debug-unavailable",
                "live-debug-owner-mismatch","live-ui-read-timeout","live-transport-uncertain","live-transport-failed",
                "live-response-invalid","live-readback-failed","preflight","sleep-session-read","native-live-admission"};
            for(const auto* known:codes)if(text==known)return text;
            return "unknown";
        };
        const auto boolean=[](const json& source,const char* key)->const char*{
            const auto it=source.find(key);return it==source.end()?"missing":!it->is_boolean()?"invalid":it->get<bool>()?"true":"false";
        };
        json value={{"clientDiscoveryOutcome",outcome}};
        value["clientDiscoverySessionReason"]=code(json{{"reason",sessionReason}},"reason");
        if(reply){
            value["clientDiscoveryReplyReason"]=code(*reply,"reason");
            value["clientDiscoveryReplyStage"]=code(*reply,"stage");
            value["clientDiscoveryReplyStatus"]=boolean(*reply,"ok");
            value["clientDiscoveryMutationStatus"]=boolean(*reply,"mutated");
        }
        *output=std::move(value);
    }catch(...){} // Cannot reject or promote the original discovery result.
}
static void noteStartupClientDiscovery(const std::shared_ptr<Control>& control,const json& diagnostic) noexcept {
    try{
        if(!diagnostic.is_object())return;
        json bounded=json::object();
        for(const auto* key:{"clientDiscoveryOutcome","clientDiscoverySessionReason","clientDiscoveryReplyReason",
            "clientDiscoveryReplyStage","clientDiscoveryReplyStatus","clientDiscoveryMutationStatus"}){
            const auto it=diagnostic.find(key);if(it==diagnostic.end()||!it->is_string())continue;
            const auto& value=it->get_ref<const std::string&>();
            if(value.size()<=96&&value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")==std::string::npos)bounded[key]=value;
        }
        std::lock_guard<std::mutex> lock(control->snapshotMx);auto& value=control->startupCaptureDiagnostic;
        if(value.clientDiscovery!=bounded){value.clientDiscovery=std::move(bounded);value.logPending=true;}
    }catch(...){} // Reuses the original per-Control16 transition budget.
}
static Client discoverClient(unsigned long long epoch,json* diagnostic=nullptr) {
    auto before=SleepSteamRecovery::session();
    if(!before.pid||!before.created||before.root.empty()||g_powerLifecycle.load()!=PowerLifecycle::Ready){
        clientDiscoveryDiagnostic(diagnostic,"session-unavailable",before.reason);return {};
    }
    const auto read=SleepSteamRecovery::liveRequest(before,{{"operation","sleep.session.inspect"},{"runningLinkRead",true}});
    if(!g_sleepRecoveryAutoEnabled.load()||g_sleepRecoveryAutoConfigEpoch.load()!=epoch||g_powerLifecycle.load()!=PowerLifecycle::Ready||
        !sameClientProcess(before)||!flag(read,"ok")||!read.contains("mutated")||!read["mutated"].is_boolean()||flag(read,"mutated")){
        clientDiscoveryDiagnostic(diagnostic,"read-or-current-refused",before.reason,&read);return {};
    }
    const auto account=number(read,"observedAccount");if(!account||account>MAXDWORD){
        clientDiscoveryDiagnostic(diagnostic,"account-unavailable",before.reason,&read);return {};
    }
    clientDiscoveryDiagnostic(diagnostic,"bound",before.reason,&read);
    before.account=static_cast<DWORD>(account);before.accountPinned=true;before.liveAccountPinned=true;
    before.accountSource="running-link-independent-steam-ui-read";before.reason="running-link-account-bound";
    // This local binding never writes SleepSteamRecovery::liveAccountSession.
    return before;
}
// A held source group is only an observation candidate. The typed public
// native proof below is required before it can mean current original contact.
static bool originalGroupCandidate(const TrackerRead& tracker,const Client& client,const Game& game){
    const auto& group=tracker.processGroup;
    if(!tracker.complete||tracker.binding.status!="tracked"||!group||
        group->graph().count!=2||!group->processCurrent())return false;
    const auto appid=tracker.binding.appid;
    if(!appid||tracker.gameId!=std::to_string((std::uint64_t(appid)<<32)|0x02000000ULL)||
        group->source().appid!=appid||std::to_string(group->source().fullGameId)!=tracker.gameId)return false;
    return group->selectedGame().pid==game.pid&&group->selectedGame().created==game.created&&
        ymcc::steamgamefacts::samePath(group->selectedGame().image,game.path)&&
        group->steam().pid==client.pid&&group->steam().created==client.created&&
        ymcc::steamgamefacts::samePath(group->steam().image,client.root+L"\\steam.exe");
}
static bool originalGroupRoleReady(
    const std::shared_ptr<const ymcc::steamgamefacts::RunningLinkNativeRoleObservation>& proof,
    const ymcc::steamrecordidentity::Read& ui,std::uint32_t account,std::uint32_t appid,const std::string& gameId){
    return proof&&proof->current()&&ui.available&&account&&appid&&
        proof->account64()==ui.account64&&static_cast<std::uint32_t>(ui.account64)==account&&
        proof->appid()==appid&&ui.appid==appid&&proof->fullGameId()==ui.gameId64&&
        gameId==std::to_string(ui.gameId64)&&
        gameId==std::to_string((std::uint64_t(appid)<<32)|0x02000000ULL);
}
static json discoveryEvidence() {
    return {{"bindingPolicy","ymcc-selected-instance-steam-join-v2"},{"repairAuthorized",false},
        {"gameIdentitySource","ymcc-unique-game-valve"},{"selectionRead","cached-observation"},
        {"processValidatedAtSelectionRead",false},{"acceptedCandidateCount",0},{"unresolvedCandidateCount",0}};
}
static Game discoverGame(const Client& client,DWORD session,json& evidence,NativeGameSelectionReceipt* admittedSelection=nullptr) {
    if(admittedSelection)*admittedSelection={};
    evidence=discoveryEvidence();
    NativeGameSelectionReceipt selection;
    if(!nativeValveReadCachedSelection(&selection)){
        evidence["reason"]="running-link-selected-target-unavailable";return {};
    }
    const Game game{selection.target.game.pid,selection.target.game.processCreated,selection.target.game.path};
    evidence["selection"]={{"pid",game.pid},{"created",std::to_string(game.created)},{"path",W2U(game.path)},
        {"rulesEpoch",std::to_string(selection.target.rulesEpoch)},{"identityGeneration",std::to_string(selection.target.identityGeneration)},
        {"generation",std::to_string(selection.target.generation)}};
    DWORD gameSession=0;
    if(game.created<client.created||!ProcessIdToSessionId(game.pid,&gameSession)||gameSession!=session){
        evidence["reason"]="running-link-selected-session-unavailable";return {};
    }
    const auto ownWindow=focusFindWindowForPid(game.pid);
    if(!ownWindow||!focusIsRestorableExactWindow(ownWindow,game.pid)){
        evidence["reason"]="running-link-selected-window-unavailable";return {};
    }
    // Steam supplies association evidence for this exact YMCC process only.
    // Another tracked/native-primary game must never replace the selected EXE.
    const auto tracker=tracking(client,game,TrackingScope::RunningListAndProcess,ymcc::steamgamefacts::TrackerGroupCapturePolicy::Capture);
    evidence["trackerLog"]=tracker.log;evidence["nativePrimary"]=tracker.primary;
    evidence["trackerStatus"]=tracker.binding.status;
    if(!tracker.complete||tracker.binding.status!="tracked"||tracker.gameId.empty()||
        ((!flag(tracker.primary,"nativePrimaryVerified")||!flag(tracker.primary,"originalAllowed"))&&!originalGroupCandidate(tracker,client,game))){
        evidence["reason"]="running-link-selected-steam-association-unavailable";return {};
    }
    if(!nativeValveSelectionCurrent(selection)){
        evidence["reason"]="running-link-selected-identity-changed";return {};
    }
    evidence["acceptedCandidateCount"]=1;evidence["reason"]="running-link-exact-instance-bound";
    if(admittedSelection)*admittedSelection=selection;
    return game;
}
static json base() {
    return {{"schemaVersion",1},{"implementation","steam-running-link-v1"},{"readOnly",true},{"evidenceScope","same-original-game-and-steam-running-link"},
        {"repairAuthorized",false},{"postWakeRetargetAllowed",false},{"contactConnected",false},{"transportConnected",false},{"gameInputVerified",false},{"continueButtonVerified",false},
        {"steamWrites",0},{"gameLaunches",0},{"focusWrites",0},{"deviceWrites",0},{"sleepWrites",0}};
}
static void publish(const std::shared_ptr<Control>& c,json value,bool logChange) {
    const bool contactConnected=value.contains("contactConnected")&&value["contactConnected"].is_boolean()&&value["contactConnected"].get<bool>();
    const bool transportConnected=value.contains("transportConnected")&&value["transportConnected"].is_boolean()&&value["transportConnected"].get<bool>();
    value.update(base());value["contactConnected"]=contactConnected;value["transportConnected"]=transportConnected;value["observedAtTick"]=GetTickCount64();
    json logged;bool logPrepared=false,captureSelected=false;
    {std::lock_guard<std::mutex> lock(c->snapshotMx);
        try{value["startupLaunchCapture"]=startupCaptureValue(c->startupCaptureDiagnostic);}catch(...){}
        const bool previousContactConnected=flag(c->snapshot,"contactConnected");
        const bool previousTransportConnected=flag(c->snapshot,"transportConnected");
        const bool contactChanged=previousContactConnected!=contactConnected;
        const bool transportChanged=previousTransportConnected!=transportConnected;
        if(logChange||contactChanged||transportChanged){c->history.push_back(value);if(c->history.size()>16)c->history.erase(c->history.begin());
            // Prepare only the ordinary log projection while this value is owned.
            // Projection failures keep their original best-effort logging scope.
            if(logChange)try{logged=ymcc::steamlink::logpolicy::compact(value);logPrepared=true;}catch(...){} }
        // Reuse the original state event and sink, with a hard per-Control cap.
        // Only capture status/result or retirement changes select an extra row;
        // sourceRevision and timing changes alone never produce a heartbeat.
        auto& captureDiagnostic=c->startupCaptureDiagnostic;
        if(captureDiagnostic.logPending&&captureDiagnostic.diagnosticReportsRemaining){
            if(!logPrepared)try{logged=ymcc::steamlink::logpolicy::compact(value);logPrepared=true;}catch(...){}
            if(logPrepared){captureDiagnostic.logPending=false;--captureDiagnostic.diagnosticReportsRemaining;captureSelected=true;}
        }
        c->snapshot=std::move(value);c->snapshot["history"]=json::array();}
    if(logPrepared){
        // Preserve the original receipt even if optional provenance cannot be added.
        try{logged.emplace("publicationCauseV1",(logChange?1u:0u)|(captureSelected?2u:0u));}catch(...){}
        try{appendNativeLifecycleLog("steam-game-link-state",std::move(logged));}catch(...){}
    }
}
static CandidateStatus sharedCandidate(const Game& original,json& receipt) {
    NativeDetectedGame found;
    if(!nativeValveReadCurrentTarget(&found)){receipt={{"status","not-available"},{"modified",false}};return CandidateStatus::None;}
    const bool same=SleepSteamRecovery::sameGameIdentity(original,{found.pid,found.processCreated,found.path});
    receipt={{"status",same?"same-original-instance":"different-instance"},{"pid",found.pid},{"created",std::to_string(found.processCreated)},{"modified",false}};
    return same?CandidateStatus::Same:CandidateStatus::Different;
}
// Fresh private identity from this round's existing UI read is an advisory
// readiness signal only. It cannot replace the Running owner's admission.
static bool originalRecoveryIdentityReady(const Evidence& e,const ymcc::steamrecordidentity::Read& privateIdentity,
    std::uint32_t account,std::uint32_t appid,const std::string& gameId,std::uint64_t scopeEpoch,std::uint64_t rulesEpoch,
    UiStatus expectedUi=UiStatus::RunningAndListed,
    const std::shared_ptr<const ymcc::steamgamefacts::RunningLinkNativeRoleObservation>& groupRole=nullptr){
    const auto fullId=(std::uint64_t(appid)<<32)|0x02000000ULL;
    return e.enabled&&e.transportAvailable&&e.configEpoch==scopeEpoch&&scopeEpoch&&
        !g_exitRequested.load()&&g_sleepRecoveryAutoEnabled.load()&&
        g_sleepRecoveryAutoConfigEpoch.load()==scopeEpoch&&rulesEpoch&&g_gameRulesEpoch.load()==rulesEpoch&&
        g_powerLifecycle.load()==PowerLifecycle::Ready&&
        e.game==IdentityStatus::Same&&e.steam==IdentityStatus::Same&&e.account==AccountStatus::Same&&
        e.tracker==TrackerStatus::Exact&&
        (e.primary==PrimaryStatus::Exact||(expectedUi==UiStatus::NotRunning&&e.primary==PrimaryStatus::Unknown)||
            (e.primary==PrimaryStatus::VerifiedOriginalGroup&&originalGroupRoleReady(groupRole,privateIdentity,account,appid,gameId)))&&
        e.validator==ValidatorStatus::Accepted&&
        (expectedUi==UiStatus::RunningAndListed||expectedUi==UiStatus::NotRunning)&&
        e.ui==expectedUi&&privateIdentity.available&&account&&appid&&
        privateIdentity.account64&&std::uint32_t(privateIdentity.account64)==account&&privateIdentity.appid==appid&&
        privateIdentity.gameId64==fullId&&gameId==std::to_string(fullId);
}
// Bounded token jobs only; TrackerOwner owns all IDs, deduplication, and facts.
static constexpr std::size_t observationQueueLimit=4;
static void closeObservations(const std::shared_ptr<Control>& c) {
    std::deque<std::function<void()>> cancelled;
    {std::lock_guard lock(c->observationMx);
        c->observationsClosed.store(true,std::memory_order_release);
        cancelled.swap(c->observationJobs);}
    // Queued RAII is destroyed outside observationMx and lifecycleMx.
    // A dequeued job remains owned by drainObservation until its call returns.
}
static void drainObservation(const std::shared_ptr<Control>& c) {
    std::deque<std::function<void()>> batch;
    {std::lock_guard lock(c->observationMx);
        if(c->stop.load(std::memory_order_acquire)||c->observationsClosed.load(std::memory_order_acquire))return;
        batch.swap(c->observationJobs);}
    for(auto& queued:batch){
        if(c->stop.load(std::memory_order_acquire)||c->observationsClosed.load(std::memory_order_acquire))break;
        std::function<void()> job;job.swap(queued);
        if(job)try{job();}catch(...){ /* TrackerOwner token RAII owns failure cleanup. */ }
    }
    // Remaining unstarted batch jobs are cancelled outside both locks after an active call returns.
}
struct ObservationScope {
    std::shared_ptr<Control> control;
    ~ObservationScope(){closeObservations(control);ymcc::steamgamefacts::trackerOwner().retireStartupLaunches();retireStartupCaptureDiagnostic(control,"owner-retired");control->observationWorkerExited.store(true,std::memory_order_release);}
};

static void discoverIdleGame(const std::shared_ptr<Control>& c,unsigned long long epoch,DWORD session,
    Client& client,Game& game,json& binding,NativeGameSelectionReceipt& selectedIdentity) {
    NativeGameNoGameReceipt noGame;
    NativeGameSelectionReceipt targetHint;
    const bool qualifiedEmpty=nativeValveCaptureNoGame(&noGame);
    if(qualifiedEmpty||!nativeValveReadCachedSelection(&targetHint)){
        // A missing selection is unknown. The owner posts an immediate hint
        // when it publishes a target we can try to associate.
        client={};game={};
        binding=discoveryEvidence();
        binding["reason"]="running-link-selected-target-unavailable";
        noteStartupClientDiscovery(c,{{"clientDiscoveryOutcome",qualifiedEmpty?
            "skipped-qualified-no-game":"skipped-selection-unavailable"}});
        return;
    }
    json clientDiagnostic;client=discoverClient(epoch,&clientDiagnostic);
    noteStartupClientDiscovery(c,clientDiagnostic);
    // This hint only gates work. Re-read the selection after the Steam call.
    if(SleepSteamRecovery::validSession(client))game=discoverGame(client,session,binding,&selectedIdentity);else game={};
}
static void run(const std::shared_ptr<Control>& c) {
    ymcc::threadRole::describe(L"YMCC.SteamGameLink");
    ObservationScope observations{c};
    State state;Client client;Game game;Handle clientHandle,gameHandle;
    SleepSteamRecovery::CurrentRecordPre currentRecordPre;
    json currentReadMetadata;
    struct RevokedOriginal {Game game;Handle handle;};std::vector<RevokedOriginal> revokedOriginals;
    logs::Watch directory;std::wstring watchedRoot,watchedLogs;std::string previousGameStamp,previousConsoleStamp,previousConnectionStamp,phase,reason;
    waitpolicy::Policy waitPolicy;bool forceObserve=false;
    bool startupCapturePending=true;
    std::optional<std::array<unsigned long long,2>> startupCaptureScope;
    unsigned long long startupCapturePower=0;
    DWORD session=0;if(!ProcessIdToSessionId(GetCurrentProcessId(),&session)){publish(c,{{"phase","unavailable"},{"reason","windows-session-unavailable"},{"linked",false},{"active",false}},true);return;}
    std::optional<NativeGameSelectionReceipt> originalSelection;
    DiagnosticWindowObservation<HWND> diagnosticWindow;
    bool pending=true;ULONGLONG idleDue=0;std::uint64_t sequence=0,identitySequence=0,lastLifetimeEventSequence=0;std::uint32_t appid=0;std::string gameId;HWND lastWindow=nullptr;
    const auto closeDirectory=[&](){directory.close();watchedRoot.clear();watchedLogs.clear();waitPolicy.directoryChecked();ymcc::steamgamefacts::trackerOwner().retireStartupLaunches();retireStartupCaptureDiagnostic(c,"watch-retired");startupCapturePending=true;};
    while(!c->stop.load()){try{
        drainObservation(c);
        currentReadMetadata=nullptr;
        const auto now=GetTickCount64(),epoch=g_sleepRecoveryAutoConfigEpoch.load();
        const bool enabled=g_sleepRecoveryAutoEnabled.load(std::memory_order_acquire);
        const std::array<unsigned long long,2> captureScope{g_gameRulesEpoch.load(std::memory_order_acquire),epoch};
        // Historical launch identity survives power changes. Every new request
        // independently requires the shared current power scope.
        const auto capturePower=currentPowerGeneration();
        if(capturePower!=startupCapturePower){startupCapturePower=capturePower;startupCapturePending=true;}
        // The window hook may already have retained evidence in the first scope.
        if(startupCaptureScope&&captureScope!=*startupCaptureScope){ymcc::steamgamefacts::trackerOwner().retireStartupLaunches();retireStartupCaptureDiagnostic(c,"scope-retired");startupCapturePending=true;}
        startupCaptureScope=captureScope;
        // Coalesce only after a directory signal. While pending, omit its signalled
        // handle from the wait set; sustained unrelated log writes cannot spin.
        if(waitPolicy.directoryReady(now)){
            waitPolicy.directoryChecked();
            if(directory.handle()){
                unsigned changes=0;
                if(!directory.consume(watchedLogs,changes))closeDirectory();
                else{
                    const auto updateStamp=[&](unsigned bit,std::wstring_view name,std::string& previous){
                        if(!(changes&bit))return false;
                        auto next=stamp(watchedLogs+L"\\"+std::wstring(name));
                        const bool changed=next!=previous;
                        previous=std::move(next);
                        return changed;
                    };
                    const bool gameChanged=updateStamp(logs::Game,logs::names[0],previousGameStamp);
                    const bool consoleChanged=updateStamp(logs::Console,logs::names[1],previousConsoleStamp);
                    const bool connection=updateStamp(logs::Connection,logs::names[2],previousConnectionStamp);
                    // connection_log is a wake hint only, never binding evidence.
                    if(gameChanged||consoleChanged||connection)steamOriginalRecoveryObserveHint();
                    if(gameChanged){startupCapturePending=true;waitPolicy.relevantChanged(now);}
                    else if(consoleChanged)waitPolicy.backgroundChanged(now);
                }
            }
        }
        if(!state.active()&&waitPolicy.readReady(now)){pending=true;idleDue=now;}
        revokedOriginals.erase(std::remove_if(revokedOriginals.begin(),revokedOriginals.end(),[](const RevokedOriginal& value){return value.handle.value&&WaitForSingleObject(value.handle.value,0)==WAIT_OBJECT_0;}),revokedOriginals.end());
        if(!state.active()){
            currentRecordPre={};
            if(!enabled){closeDirectory();waitPolicy.observed(now);forceObserve=false;publish(c,{{"phase","off"},{"reason","user-disabled"},{"linked",false},{"active",false}},phase!="off");phase="off";pending=false;}
            else if(pending&&now>=idleDue){
                pending=false;forceObserve=false;waitPolicy.observed(now);idleDue=waitpolicy::add(now,3000);
                const auto observed=SleepSteamRecovery::session();
                if(observed.pid&&observed.created&&!observed.root.empty()&&observed.root!=watchedRoot){
                    // A first enrollment is not the retirement of a prior watch.
                    if(directory.handle())closeDirectory();
                    startupCapturePending=true;
                    watchedLogs=observed.root+L"\\logs";
                    if(directory.open(watchedLogs))watchedRoot=observed.root;
                    previousGameStamp=stamp(observed.root+L"\\logs\\gameprocess_log.txt");
                    previousConsoleStamp=stamp(observed.root+L"\\logs\\console_log.txt");
                    previousConnectionStamp=stamp(observed.root+L"\\logs\\connection_log.txt");
                }
                if(startupCapturePending&&g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready){
                    const auto captureStartedAtTick=GetTickCount64();startupCapturePending=false;
                    noteStartupCapture(c,nativeSteamCaptureStartupLaunches(observed),captureStartedAtTick);
                }
                json binding;NativeGameSelectionReceipt selectedIdentity;
                {StartupDiscoveryDiagnosticScope discoveryDiagnostic(c);
                    discoverIdleGame(c,epoch,session,client,game,binding,selectedIdentity);
                }
                const bool revokedSameGame=game.pid&&std::any_of(revokedOriginals.begin(),revokedOriginals.end(),[&](const RevokedOriginal& value){return SleepSteamRecovery::sameGameIdentity(value.game,game);});
                if(revokedSameGame||revokedOriginals.size()>=16){binding["reason"]="running-link-revoked-original-not-rebound";binding["revokedOriginalBlocked"]=true;game={};}
                if(game.pid){
                    clientHandle=pin(client.pid,client.created,client.root+L"\\steam.exe",session);gameHandle=pin(game.pid,game.created,game.path,session);
                    const auto tracker=tracking(client,game,TrackingScope::RunningListAndProcess,ymcc::steamgamefacts::TrackerGroupCapturePolicy::Capture);
                    steamOriginalRecoveryTrackerObserved(game.pid,game.created,client.pid,client.created,
                        tracker.gameId,tracker.binding.appid,tracker.complete&&tracker.binding.status=="tracked",
                        tracker.complete&&tracker.binding.status=="removed");
                    if(clientHandle.value&&gameHandle.value&&tracker.complete&&tracker.binding.status=="tracked"&&
                        !tracker.gameId.empty()&&((flag(tracker.primary,"nativePrimaryVerified")&&flag(tracker.primary,"originalAllowed"))||
                            originalGroupCandidate(tracker,client,game))&&sameClientProcess(client)&&g_sleepRecoveryAutoEnabled.load()&&g_sleepRecoveryAutoConfigEpoch.load()==epoch&&
                        g_powerLifecycle.load()==PowerLifecycle::Ready&&nativeValveSelectionCurrent(selectedIdentity)){
                        diagnosticWindow.invalidate();
                        appid=tracker.binding.appid;gameId=tracker.gameId;sequence=0;identitySequence=0;lastLifetimeEventSequence=0;lastWindow=nullptr;const bool begun=state.begin(nextSerial.fetch_add(1),epoch,GetTickCount64());
                        if(begun){originalSelection=selectedIdentity;publish(c,{{"phase","binding"},{"linked",false},{"active",true},{"binding",binding}},true);phase="binding";}
                        else{clientHandle={};gameHandle={};game={};}
                    }else{clientHandle={};gameHandle={};game={};}
                }
                if(!state.active()){publish(c,{{"phase","idle"},{"reason","waiting-unique-original-instance"},{"linked",false},{"active",false},{"binding",binding}},phase!="idle");phase="idle";reason="waiting-unique-original-instance";}
            }
        }
        const bool notified=forceObserve;
        if(state.active()&&(forceObserve||waitPolicy.readReady(now)||now>=state.snapshot().dueTick||!enabled||state.snapshot().configEpoch!=epoch)){
            forceObserve=false;waitPolicy.observed(now);
            Evidence e;e.sequence=++sequence;e.tick=GetTickCount64();e.enabled=enabled;e.configEpoch=epoch;
            e.game=identity(gameHandle.value,game.pid,game.created,game.path,session);e.steam=identity(clientHandle.value,client.pid,client.created,client.root+L"\\steam.exe",session);
            json validation,shared,detail=base();e.sharedCandidate=sharedCandidate(game,shared);
            detail["originalGame"]={{"pid",game.pid},{"created",std::to_string(game.created)},{"path",W2U(game.path)},{"windowsSession",session}};
            detail["steamClient"]=SleepSteamRecovery::clientIdentity(client);detail["appid"]=appid;detail["trackerGameIdExactString"]=gameId;detail["sharedCandidate"]=shared;
            HWND ownWindow=nullptr;
            if(e.game==IdentityStatus::Same){
                ownWindow=diagnosticWindow.observe(e.tick,capturePower,notified,
                    [&]{return focusFindWindowForPid(game.pid);},
                    [&](HWND window){return isUsableFocusWindow(window,game.pid,true);});
            }else{
                diagnosticWindow.invalidate();
                const auto found=focusFindWindowForPid(game.pid);
                if(found&&focusIsRestorableExactWindow(found,game.pid))ownWindow=found;
            }
            const bool windowMatched=ownWindow!=nullptr;
            detail["osWindow"]={{"source","same-original-pid-window"},{"available",windowMatched},{"hwnd",windowMatched?std::to_string(reinterpret_cast<std::uintptr_t>(ownWindow)):std::string()},{"SteamWindowIdMapping","unknown"}};
            const auto foreground=SleepSteamRecovery::foreground(game,client);detail["osForeground"]={{"role",foreground.role},{"pid",foreground.pid},{"created",std::to_string(foreground.created)},{"readOnly",true}};
            bool lifetimeChanged=false;ymcc::steamrecordidentity::Read privateRecordIdentity;
            std::shared_ptr<ymcc::steamgamefacts::ProcessGroupLease> observedGroup;
            std::shared_ptr<const ymcc::steamgamefacts::RunningLinkNativeRoleObservation> nativeGroupRole;
            unsigned long long nativeRecordReadGeneration=0;
            std::uint64_t uiStartedTick=0,uiFinishedTick=0,uiStartedFileTime=0,uiFinishedFileTime=0,currentRecordRulesEpoch=0,uiIdentitySequence=0;
            SleepSteamRecovery::CurrentRecordRead currentRecordRead;
            const auto currentProbeActive=[&]{return !c->stop.load()&&state.active()&&
                identity(gameHandle.value,game.pid,game.created,game.path,session)==IdentityStatus::Same&&
                identity(clientHandle.value,client.pid,client.created,client.root+L"\\steam.exe",session)==IdentityStatus::Same;};
            const auto finishReadIdentity=[&]{
                const auto mergeIdentity=[](IdentityStatus a,IdentityStatus b){
                    if(a==IdentityStatus::Exited||b==IdentityStatus::Exited)return IdentityStatus::Exited;
                    if(a==IdentityStatus::Changed||b==IdentityStatus::Changed)return IdentityStatus::Changed;
                    return a==IdentityStatus::Unknown||b==IdentityStatus::Unknown?IdentityStatus::Unknown:IdentityStatus::Same;
                };
                e.game=mergeIdentity(e.game,identity(gameHandle.value,game.pid,game.created,game.path,session));
                e.steam=mergeIdentity(e.steam,identity(clientHandle.value,client.pid,client.created,client.root+L"\\steam.exe",session));
                e.enabled=g_sleepRecoveryAutoEnabled.load();e.configEpoch=g_sleepRecoveryAutoConfigEpoch.load();
                if(e.steam!=IdentityStatus::Exited&&e.steam!=IdentityStatus::Changed){const auto after=SleepSteamRecovery::session();
                    if(!after.pid||!after.created||after.root.empty())e.steam=IdentityStatus::Unknown;
                    else if(!SleepSteamRecovery::sameClientProcess(client,after))e.steam=IdentityStatus::Changed;
                    else if(after.account&&after.account!=client.account){
                        e.account=AccountStatus::Changed;e.transportAvailable=false;e.ui=UiStatus::Unknown;
                    }}
            };
            if(e.game==IdentityStatus::Same&&e.steam==IdentityStatus::Same&&enabled&&state.snapshot().configEpoch==epoch){
                const auto tracker=tracking(client,game,TrackingScope::ProcessLifetime,ymcc::steamgamefacts::TrackerGroupCapturePolicy::Capture);
                if(originalGroupCandidate(tracker,client,game))observedGroup=tracker.processGroup;
                // Revocation hint only; the Running owner still qualifies its
                // own unique PID and full ID before any native transaction.
                steamOriginalRecoveryTrackerObserved(game.pid,game.created,client.pid,client.created,
                    tracker.gameId,tracker.binding.appid,tracker.complete&&tracker.binding.status=="tracked",
                        tracker.complete&&tracker.binding.status=="removed");
                detail["tracker"]={{"complete",tracker.complete},{"status",tracker.binding.status},{"appid",tracker.binding.appid},{"gameIdExactString",tracker.gameId},{"log",tracker.log},{"nativePrimary",tracker.primary}};
                e.tracker=tracker.complete&&tracker.binding.status=="tracked"&&tracker.binding.appid==appid&&tracker.gameId==gameId?TrackerStatus::Exact:
                    tracker.complete&&tracker.binding.status=="removed"?TrackerStatus::Absent:TrackerStatus::Unknown;
                e.primary=flag(tracker.primary,"nativePrimaryVerified")?(flag(tracker.primary,"originalAllowed")?PrimaryStatus::Exact:PrimaryStatus::Different):PrimaryStatus::Unknown;
                e.validator=validate(game,validation);detail["ymccPidValidation"]=validation;
                if(!ymcc::sleepsteam::unsignedDecimal(validation.value("rulesEpoch",std::string()),currentRecordRulesEpoch))currentRecordRulesEpoch=0;
                if(g_powerLifecycle.load()==PowerLifecycle::Ready){
                    nativeRecordReadGeneration=currentPowerGeneration();
                    // Previous successful UI sample is PRE. Read before this round's
                    // one existing request; that request supplies POST, not a new RPC.
                    if(e.tracker==TrackerStatus::Exact&&e.primary==PrimaryStatus::Exact&&e.validator==ValidatorStatus::Accepted&&
                        currentRecordRulesEpoch==currentRecordPre.proof.rulesEpoch)
                        currentRecordRead=SleepSteamRecovery::readCurrentRecordBeforeExistingUi(currentRecordPre,currentProbeActive);
                    currentReadMetadata=SleepSteamRecovery::currentRecordReadMetadata(currentRecordRead,false);
                    currentRecordPre={}; // consume once, including failed POSTs
                    uiStartedTick=GetTickCount64();uiStartedFileTime=sgNowFileTime();uiIdentitySequence=++identitySequence;
                    const auto read=SleepSteamRecovery::liveRequest(client,{{"operation","sleep.inspect"},{"appids",json::array({appid})},{"runningLinkRead",true},{"nativeFocusFeedbackRead",true},{"nativeAppLifetimeFeedbackRead",true},{"overlayProviderCacheTarget",{{"pid",game.pid},{"gameid64",gameId}}}},{},&privateRecordIdentity);
                    uiFinishedTick=GetTickCount64();uiFinishedFileTime=sgNowFileTime();
                    detail["steamUI"]=read;detail["overviewSource"]="steam-ui-native-overview-store";
                    // Sanitize only this diagnostic on the log copy; the original
                    // read and every running/recovery admission stay unchanged.
                    if(detail["steamUI"].is_object()){
                        detail["steamUI"].erase("overlayPopupCacheDiagnostic");
                        detail["steamUI"].erase("overlayProviderCacheDiagnostic");
                    }
                    try{
                        detail["overlayPopupCacheDiagnostic"]=ymcc::steamoverlaycache::summarize(read,appid,game.pid);
                    }catch(...){} // Diagnostic failure cannot reject the existing read.
                    try{
                        detail["overlayProviderCacheDiagnostic"]=ymcc::steamoverlayprovidercache::summarize(read,appid,game.pid,gameId);
                    }catch(...){} // Cache membership is never recovery admission.
                    detail["nativeFocusFeedback"]=compareNativeFocus(read,appid,game.pid);
                    const auto lifetime=compareNativeAppLifetime(read,appid);detail["nativeAppLifetimeFeedback"]=lifetime;
                    const auto lifetimeSequence=number(lifetime,"latestEventSequence");
                    lifetimeChanged=lifetimeSequence>lastLifetimeEventSequence;
                    if(lifetimeSequence>lastLifetimeEventSequence)lastLifetimeEventSequence=lifetimeSequence;
                    const auto readReason=read.value("reason",std::string());
                    if(readReason=="steam-account-changed")e.account=AccountStatus::Changed;
                    else if(readReason=="steam-session-changed"){e.steam=IdentityStatus::Unknown;detail["steamReadFailureInterpretation"]="unavailable-unless-independent-identity-change-proven";}
                    if(flag(read,"ok")&&read.contains("mutated")&&read["mutated"].is_boolean()&&!flag(read,"mutated")){
                        e.transportAvailable=true;e.account=AccountStatus::Same;
                        if(read.contains("overviews")&&read["overviews"].is_array())for(const auto& row:read["overviews"]){
                            if(number(row,"appid")!=appid||!row.contains("gameidString")||!row["gameidString"].is_string()||row["gameidString"].get<std::string>()!=gameId||!flag(row,"overviewAvailable")||!row.contains("inRunningList")||!row["inRunningList"].is_boolean())continue;
                            const auto status=number(row,"localDisplayStatus");
                            if(status==4||status==1)e.ui=flag(row,"inRunningList")?UiStatus::RunningAndListed:UiStatus::RunningMissingList;
                            else if(status==11)e.ui=UiStatus::NotRunning;
                        }
                    }
                }else detail["steamUI"]={{"ok",false},{"mutated",false},{"reason","power-transition-read-deferred"}};
                // The original pinned handles and enabled epoch bracket every UI read.
                finishReadIdentity();
            }
            if(e.game==IdentityStatus::Same&&e.steam==IdentityStatus::Same&&e.account==AccountStatus::Same&&
                e.tracker==TrackerStatus::Exact&&e.primary==PrimaryStatus::Exact&&e.validator==ValidatorStatus::Accepted&&e.enabled&&e.configEpoch==state.snapshot().configEpoch){
                // These synchronous consumers only read the sanitized reply.
                // Borrow it for this scope instead of deep-copying it per call.
                const auto ui=detail.find("steamUI");
                const json absentUi=ui==detail.end()?json::object():json();
                const json& steamUi=ui==detail.end()?absentUi:*ui;
                const auto original=SleepSteamRecovery::capturedFor(nativeRecordReadGeneration);
                if(original.generation){
                    // Preserve the old captured path, including bound=false refusal.
                    currentRecordPre={};
                    if(original.game.pid==game.pid&&original.game.created==game.created)
                        detail["nativeRecordDiagnostic"]=SleepSteamRecovery::prefetchNativeRecordIdentity(client,game,appid,privateRecordIdentity,nativeRecordReadGeneration,e.configEpoch,gameId,steamUi);
                }else{
                    auto post=SleepSteamRecovery::currentRecordProof(client,game,appid,privateRecordIdentity,steamUi,
                        nativeRecordReadGeneration,e.configEpoch,currentRecordRulesEpoch,state.snapshot().serial,uiIdentitySequence,session,gameId,
                        uiStartedTick,uiFinishedTick,uiStartedFileTime,uiFinishedFileTime);
                    json diagnostic;
                    if(SleepSteamRecovery::commitCurrentRecordAfterExistingUi(currentRecordRead,post,steamUi,currentProbeActive,diagnostic))
                    {
                        detail["nativeRecordDiagnostic"]=std::move(diagnostic);
                        currentReadMetadata=SleepSteamRecovery::currentRecordReadMetadata(currentRecordRead,true);
                    }
                    // If normal cadence missed the strict window, one throttled
                    // existing read-only identity postflight can complete it now.
                    // Evidence sequence stays independent of identity-request sequence.
                    const auto baseline=SleepSteamRecovery::boundedShortcutBaseline(post,identitySequence,currentProbeActive);
                    if(baseline.attempted){
                        detail["shortcutBaselineIdentityPostflightInvoked"]=baseline.identityPostflightInvoked;
                        if(baseline.readMetadata.is_object())currentReadMetadata=baseline.readMetadata;
                        if(baseline.committed)detail["nativeRecordDiagnostic"]=baseline.diagnostic;
                        currentRecordPre=baseline.nextPre;
                        // A native read can outlive its owner before any POST is sent.
                        // Every attempted helper read needs the same final checks.
                        finishReadIdentity();
                        if(baseline.identityPostflightInvoked){
                            detail["shortcutBaselineIdentityPostflightAccepted"]=baseline.postflightIdentityAccepted;
                            if(!baseline.postflightIdentityAccepted){
                                e.transportAvailable=false;e.ui=UiStatus::Unknown;
                                if(baseline.postflightAccountChanged)e.account=AccountStatus::Changed;
                                else if(e.account!=AccountStatus::Changed)e.account=AccountStatus::Unknown;
                                if(baseline.postflightSessionChanged&&e.steam==IdentityStatus::Same)e.steam=IdentityStatus::Unknown;
                            }
                        }
                        if((baseline.identityPostflightInvoked&&!baseline.postflightIdentityAccepted)||
                            e.game!=IdentityStatus::Same||e.steam!=IdentityStatus::Same||e.account!=AccountStatus::Same||
                            !e.enabled||e.configEpoch!=state.snapshot().configEpoch||!SleepSteamRecovery::currentRecordOwner(currentRecordPre)){
                            currentRecordPre={};detail.erase("nativeRecordDiagnostic");
                            if(currentReadMetadata.is_object()){currentReadMetadata["nativeDiagnosticCommitAccepted"]=false;currentReadMetadata["discarded"]=true;}
                        }
                    }else currentRecordPre=std::move(post);
                }
            }else currentRecordPre={};
            // The public issuer performs the existing typed read once, after
            // this round's real UI identity. Only this round owns its proof.
            if(e.primary!=PrimaryStatus::Exact&&observedGroup&&originalSelection&&e.game==IdentityStatus::Same&&e.steam==IdentityStatus::Same&&
                e.account==AccountStatus::Same&&e.tracker==TrackerStatus::Exact&&e.validator==ValidatorStatus::Accepted&&
                e.enabled&&e.configEpoch==epoch&&privateRecordIdentity.available){
                const auto selection=*originalSelection;
                const auto rules=currentRecordRulesEpoch,power=currentPowerGeneration();
                const auto current=[owner=std::weak_ptr<Control>(c),selection,client,game,epoch,rules,power]{
                    const auto control=owner.lock();
                    return control&&!control->stop.load()&&g_sleepRecoveryAutoEnabled.load()&&
                        g_sleepRecoveryAutoConfigEpoch.load()==epoch&&g_gameRulesEpoch.load()==rules&&
                        currentPowerGeneration()==power&&g_powerLifecycle.load()==PowerLifecycle::Ready&&
                        selection.target.game.pid==game.pid&&selection.target.game.processCreated==game.created&&
                        ymcc::steamgamefacts::samePath(selection.target.game.path,game.path)&&
                        nativeValveSelectionCurrent(selection)&&sameClientProcess(client);
                };
                NativeSteamOriginalRoleReadDiagnostic roleDiagnostic;
                nativeGroupRole=nativeSteamRunningLinkReadGroupRoles(client,game,observedGroup,privateRecordIdentity,current,&roleDiagnostic);
                detail["qualifiedOriginalGroupRoleRead"]=roleDiagnostic.projection();
                if(originalGroupRoleReady(nativeGroupRole,privateRecordIdentity,client.account,appid,gameId)){
                    if(e.primary!=PrimaryStatus::Exact)e.primary=PrimaryStatus::VerifiedOriginalGroup;
                    detail["qualifiedOriginalGroupRoles"]={{"source","public-typed-native-group-read"},{"available",true},
                        {"primaryPid",nativeGroupRole->primaryPid()},{"foregroundPid",nativeGroupRole->foregroundPid()},
                        {"nativePrimaryExact",nativeGroupRole->nativePrimaryExact()},
                        {"nativeForegroundExact",nativeGroupRole->nativeForegroundExact()},{"actionAuthority",false}};
                }
            }
            // Includes the optional additional identity POST, when one was made.
            const bool originalIdentityBracketMatched=
                e.game==IdentityStatus::Same&&e.steam==IdentityStatus::Same&&e.account==AccountStatus::Same&&
                e.enabled&&e.configEpoch==state.snapshot().configEpoch;
            if(originalRecoveryIdentityReady(e,privateRecordIdentity,client.account,appid,gameId,
                    state.snapshot().configEpoch,currentRecordRulesEpoch,UiStatus::RunningAndListed,nativeGroupRole)){
                steamOriginalRecoveryIdentityReady(game.pid,game.created,client.pid,client.created,gameId,appid,
                    e.configEpoch,currentRecordRulesEpoch);
                detail["originalRecoveryQualificationReadyHintOffered"]=true;
            }
            if(originalRecoveryIdentityReady(e,privateRecordIdentity,client.account,appid,gameId,
                    state.snapshot().configEpoch,currentRecordRulesEpoch,UiStatus::NotRunning,nativeGroupRole)){
                steamOriginalRecoveryRunningLost(game.pid,game.created,client.pid,client.created,gameId,appid,
                    e.configEpoch,currentRecordRulesEpoch);
                detail["originalRecoveryRunningLossHintOffered"]=true;
            }
            if(detail.contains("nativeFocusFeedback"))detail["nativeFocusFeedback"]["originalIdentityBracketMatched"]=originalIdentityBracketMatched;
            if(detail.contains("overlayProviderCacheDiagnostic")){
                auto& provider=detail["overlayProviderCacheDiagnostic"];
                provider["originalIdentityBracketMatched"]=originalIdentityBracketMatched;
                if(!originalIdentityBracketMatched){
                    provider["available"]=false;provider["targetMembership"]="unknown";
                    provider["targetFieldsTagged"]=nullptr;provider["reason"]="original-identity-bracket-unqualified";
                    provider["sameGameIdCandidateCount"]=nullptr;provider["sameGameIdCandidatesComplete"]=false;
                    provider["sameGameIdCandidatesTagged"]=nullptr;
                }
            }
            if(currentReadMetadata.is_object())detail["nativeRecordReadMetadata"]=currentReadMetadata;
            if(e.primary==PrimaryStatus::VerifiedOriginalGroup&&!originalGroupRoleReady(nativeGroupRole,privateRecordIdentity,client.account,appid,gameId))e.primary=PrimaryStatus::Unknown;
            if(detail.contains("qualifiedOriginalGroupRoles"))detail["qualifiedOriginalGroupRoles"]["current"]=originalGroupRoleReady(nativeGroupRole,privateRecordIdentity,client.account,appid,gameId);
            e.tick=GetTickCount64();state.observe(e);const auto s=state.snapshot();
            detail["phase"]=s.phase;detail["reason"]=s.reason;detail["linked"]=s.linked;detail["contactConnected"]=s.contactConnected;detail["transportConnected"]=s.transportConnected;detail["active"]=s.active;detail["terminal"]=s.terminal;
            detail["linkSerial"]=std::to_string(s.serial);detail["configEpoch"]=std::to_string(s.configEpoch);detail["observationSequence"]=std::to_string(s.lastSequence);
            detail["dueTick"]=s.dueTick;detail["readFailures"]=s.readFailures;
            detail["verifiedReadAvailable"]=e.transportAvailable;detail["gameIdentitySame"]=e.game==IdentityStatus::Same;detail["steamIdentitySame"]=e.steam==IdentityStatus::Same;
            detail["accountMatched"]=e.account==AccountStatus::Same;detail["trackerExact"]=e.tracker==TrackerStatus::Exact;detail["nativePrimaryExact"]=e.primary==PrimaryStatus::Exact;
            detail["ymccSinglePidAccepted"]=e.validator==ValidatorStatus::Accepted;detail["nativeOverviewAssociated"]=e.ui==UiStatus::RunningAndListed;
            publish(c,std::move(detail),phase!=s.phase||reason!=s.reason||lifetimeChanged||(windowMatched&&ownWindow!=lastWindow));phase=s.phase;reason=s.reason;if(windowMatched)lastWindow=ownWindow;
            if(!s.active){currentRecordPre={};if(s.phase=="revoked"&&gameHandle.value&&revokedOriginals.size()<16)revokedOriginals.push_back({game,std::move(gameHandle)});
                gameHandle={};clientHandle={};game={};
                // The successor's selection hint may have woken the old link.
                // Recheck once through normal discovery, never retarget its identity.
                pending=e.enabled;idleDue=e.tick;}
        }
        HANDLE waits[5]{c->stopEvent};DWORD count=1,dirIndex=MAXDWORD,gameIndex=MAXDWORD,steamIndex=MAXDWORD;
        // Ready process exits win over recurring hints and directory traffic.
        if(state.active()){gameIndex=count;waits[count++]=gameHandle.value;steamIndex=count;waits[count++]=clientHandle.value;}
        const DWORD pokeIndex=count;waits[count++]=c->pokeEvent;
        if(directory.handle()&&!waitPolicy.directoryPending()){dirIndex=count;waits[count++]=directory.handle();}
        if(!state.active()&&!revokedOriginals.empty()){gameIndex=count;waits[count++]=revokedOriginals.front().handle.value;}
        const auto nowWait=GetTickCount64();const auto due=state.active()?state.snapshot().dueTick:pending?idleDue:ULLONG_MAX;
        const DWORD timeout=waitpolicy::timeout(nowWait,waitPolicy.nextDue(due),state.active()||pending||waitPolicy.scheduled());
        const auto waited=WaitForMultipleObjects(count,waits,FALSE,timeout);
        if(waited==WAIT_OBJECT_0||waited==WAIT_FAILED)break;
        if(waited==WAIT_OBJECT_0+pokeIndex){
            if(const auto pendingHints=c->hintsPending.exchange(0,std::memory_order_acq_rel)){
                steamOriginalRecoveryObserveHint();
                NativeGameNoGameReceipt noGame;
                if(pendingHints==waitpolicy::Window&&!state.active()&&!pending&&
                    directory.handle()&&!startupCapturePending&&nativeValveCaptureNoGame(&noGame)){
                    // Target changes and game logs still request their own reads.
                    // Window churn alone needs only one bounded audit.
                    waitPolicy.backgroundChanged(GetTickCount64());
                }else if((pendingHints&waitpolicy::Immediate)||!state.linked()){
                    forceObserve=true;
                    if(!state.active()){pending=true;idleDue=GetTickCount64();}
                }
                // A healthy link keeps its existing deadline for window-only
                // notifications. Recovery still receives the hint above.
            }
        }else if((gameIndex!=MAXDWORD&&waited==WAIT_OBJECT_0+gameIndex)||
                 (steamIndex!=MAXDWORD&&waited==WAIT_OBJECT_0+steamIndex)){
            steamOriginalRecoveryObserveHint();forceObserve=true;
            if(!state.active()){pending=true;idleDue=GetTickCount64();}
        }else if(dirIndex!=MAXDWORD&&waited==WAIT_OBJECT_0+dirIndex){
            const auto directoryObservedAtTick=GetTickCount64();noteStartupDirectory(c,directoryObservedAtTick);
            waitPolicy.directoryNotified(directoryObservedAtTick);
        }
        (void)gameIndex;(void)steamIndex;
    }catch(...){
        currentRecordPre={};
        if(state.active()){
            Evidence failed;failed.sequence=++sequence;failed.tick=GetTickCount64();failed.enabled=g_sleepRecoveryAutoEnabled.load();failed.configEpoch=g_sleepRecoveryAutoConfigEpoch.load();
            failed.game=identity(gameHandle.value,game.pid,game.created,game.path,session);failed.steam=identity(clientHandle.value,client.pid,client.created,client.root+L"\\steam.exe",session);
            state.observe(failed);const auto current=state.snapshot();publish(c,{{"phase",current.phase},{"reason","sample-unavailable"},{"linked",false},{"active",current.active},{"dueTick",current.dueTick},
                {"nativeRecordReadMetadata",currentReadMetadata},{"linkSerial",std::to_string(current.serial)},{"originalPid",game.pid},{"originalCreated",std::to_string(game.created)}},reason!="sample-unavailable");reason="sample-unavailable";
            if(!current.active){if(current.phase=="revoked"&&gameHandle.value&&revokedOriginals.size()<16)revokedOriginals.push_back({game,std::move(gameHandle)});gameHandle={};clientHandle={};game={};}
        }else publish(c,{{"phase","idle"},{"reason","capture-read-unavailable"},{"linked",false},{"active",false},{"nativeRecordReadMetadata",currentReadMetadata}},phase!="idle");
        if(WaitForSingleObject(c->stopEvent,3000)==WAIT_OBJECT_0)break;
        pending=true;idleDue=waitpolicy::add(GetTickCount64(),30000);
    }}

    closeDirectory();
    if(c->stop.load())publish(c,{{"phase","stopped"},{"reason","observer-stopped"},{"linked",false},{"active",false}},true);
}
static json get() {
    std::shared_ptr<Control> c;{std::lock_guard<std::mutex> lock(lifecycleMx);c=control;}
    if(!c){auto result=base();result.update({{"phase","not-started"},{"linked",false},{"active",false}});return result;}
    std::lock_guard<std::mutex> lock(c->snapshotMx);auto result=c->snapshot;try{result["startupLaunchCapture"]=startupCaptureValue(c->startupCaptureDiagnostic);}catch(...){}if(result.contains("history"))result["history"]=c->history;const bool contactConnected=flag(result,"contactConnected");const bool transportConnected=flag(result,"transportConnected");result.update(base());result["contactConnected"]=contactConnected;result["transportConnected"]=transportConnected;if(!result.contains("active"))result["active"]=false;const auto now=GetTickCount64();
    const auto observed=number(result,"observedAtTick");const auto due=number(result,"dueTick");
    const bool observedKnown=result.contains("observedAtTick")&&result["observedAtTick"].is_number_unsigned();
    result["snapshotAtTick"]=now;result["fresh"]=observedKnown&&now>=observed&&(!due||due>ULLONG_MAX-5000||now<=due+5000);
    if(!flag(result,"fresh")){result["contactConnected"]=false;result["transportConnected"]=false;if(flag(result,"linked")){result["linked"]=false;result["phase"]="stale";result["reason"]="current-link-read-not-fresh";}}
    if(!g_sleepRecoveryAutoEnabled.load()){result["phase"]="off";result["reason"]="user-disabled";result["linked"]=false;result["contactConnected"]=false;result["transportConnected"]=false;result["active"]=false;}
    else if(result.contains("configEpoch")&&result["configEpoch"].is_string()&&result["configEpoch"].get<std::string>()!=std::to_string(g_sleepRecoveryAutoConfigEpoch.load())){result["phase"]="cancelled";result["reason"]="config-epoch-changed";result["linked"]=false;result["contactConnected"]=false;result["transportConnected"]=false;result["active"]=false;}
    return result;
}
}
// Transport only: no demand IDs, implicit refresh, retries, or new worker.
static bool steamGameLinkSubmitObservation(std::function<void()> job) {
    if(!job||g_exitRequested.load(std::memory_order_acquire))return false;
    std::shared_ptr<SteamGameLink::Control> c;
    {std::lock_guard lock(SteamGameLink::lifecycleMx);c=SteamGameLink::control;}
    if(!c)return false;
    bool submitted=false;
    {std::lock_guard lock(c->observationMx);
        if(g_exitRequested.load(std::memory_order_acquire)||c->stop.load(std::memory_order_acquire)||
            c->observationsClosed.load(std::memory_order_acquire)||
            c->observationJobs.size()>=SteamGameLink::observationQueueLimit||!c->pokeEvent)return false;
        try{c->observationJobs.emplace_back();}catch(...){return false;}
        c->observationJobs.back().swap(job);
        submitted=SetEvent(c->pokeEvent)!=FALSE;
        // Still under the dequeue lock: failure cannot leave the token runnable.
        if(!submitted){c->observationJobs.back().swap(job);c->observationJobs.pop_back();}}
    // A rejected closure is destroyed only after both locks have been released.
    return submitted;
}
static void steamGameLinkStart(){
    std::lock_guard<std::mutex> lock(SteamGameLink::lifecycleMx);
    if(SteamGameLink::worker.joinable()||(SteamGameLink::control&&SteamGameLink::control->stop.load(std::memory_order_acquire)&&!SteamGameLink::control->observationWorkerExited.load(std::memory_order_acquire))||g_aiFanMockSession.isolated||g_exitRequested.load())return;
    auto c=std::make_shared<SteamGameLink::Control>();c->stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);c->pokeEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!c->stopEvent||!c->pokeEvent)return;
    try{SteamGameLink::control=c;SteamGameLink::worker=std::thread([c]{SteamGameLink::run(c);});}catch(...){SteamGameLink::control.reset();}
}
static void steamGameLinkNotify(bool windowHint){
    std::lock_guard<std::mutex> lock(SteamGameLink::lifecycleMx);
    if(const auto& c=SteamGameLink::control;c&&c->pokeEvent){
        using ymcc::steamlink::waitpolicy::Hint;
        const auto pending=c->hintsPending.fetch_or(windowHint ? Hint::Window : Hint::Immediate, std::memory_order_release);
        // The waiter exchanges the whole batch, including urgency upgrades posted
        // after it has received the auto-reset signal but before it takes the hints.
        if(!pending||!c->hintWakeSucceeded)c->hintWakeSucceeded=SetEvent(c->pokeEvent)!=FALSE;
    }
}
static void steamGameLinkShutdown(){
    std::shared_ptr<SteamGameLink::Control> c;
    std::thread stoppedWorker;
    {std::lock_guard<std::mutex> lock(SteamGameLink::lifecycleMx);
        c=SteamGameLink::control;
        if(c){c->stop.store(true,std::memory_order_release);SetEvent(c->stopEvent);}
        stoppedWorker=std::move(SteamGameLink::worker);}
    // Token destructors may reenter submission. Never cancel or join under either queue lock.
    if(c)SteamGameLink::closeObservations(c);
    (void)joinThreadBoundedForExit(stoppedWorker,1000,"steam-game-link");
}
static json steamGameLinkGet(){return SteamGameLink::get();}
static std::wstring steamGameLinkExportSnapshot(){
    const auto file=yemancc_data_dir()+L"\\steam-game-link-current.json";
    try{std::ofstream out(fspath::path(file),std::ios::binary|std::ios::trunc);out<<steamGameLinkGet().dump(2)<<'\n';out.close();if(out)return file;}catch(...){}
    return {};
}


