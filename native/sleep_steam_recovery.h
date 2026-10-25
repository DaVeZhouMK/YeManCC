#pragma once
#include "sleep_steam_tracking.h"
#include "sleep_steam_capture.h"
#include "sleep_steam_input_context.h"
#include "sleep_steam_game_probe.h"
#include "sleep_steam_settle.h"
#include "steam_native_record_identity.h"
#include "steam_native_record_win32.h"
#include "steam_shortcut_metadata_win32.h"
#include "steam_native_app_state_win32.h"
#include "resume_focus.h"
// Steam-only wake observer/reconciler. It NEVER reads InputHost admission or
// writes controller/fan state. Only an exact original game -> Steam tracker ->
// local backend overview chain can authorize Valve's native UI rescope.
// Defined by the Steam automatic owner, never by a controller/fan gate.
static bool sleepSteamWakeOperationAllows(unsigned long long generation,unsigned long long configEpoch);
namespace SleepSteamRecovery {
using namespace ymcc::sleepsteam;
struct Game {DWORD pid=0;ULONGLONG created=0;std::wstring path;};
static bool sameGame(const Game& g) {
    if(!g.pid||!g.created||g.path.empty())return false;
    ULONGLONG created=0;std::wstring path;
    if(!focusQueryProcessIdentity(g.pid,&path,&created)||created!=g.created||pathKey(W2U(path))!=pathKey(W2U(g.path)))return false;
    HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,g.pid);
    const bool alive=h&&nativeProcessCreatedFromHandle(h)==g.created&&WaitForSingleObject(h,0)==WAIT_TIMEOUT;
    if(h)CloseHandle(h);return alive;
}
static Game currentGame() {
    Game g;SgSleepTarget owner;
    if(sgReadSleepTarget(owner)&&owner.markerOwned){g.pid=owner.pid;g.created=owner.processCreated;}
    else {NativeDetectedGame target;if(nativeValveReadCurrentTarget(&target)){g.pid=target.pid;g.created=target.processCreated;g.path=target.path;}}
    ULONGLONG created=0;std::wstring path;
    if(!g.pid||!g.created||!focusQueryProcessIdentity(g.pid,&path,&created)||created!=g.created)return {};
    g.path=path;return g;
}
static std::string localSecond(ULONGLONG created) {
    ULARGE_INTEGER raw{};raw.QuadPart=created;FILETIME utc{raw.LowPart,raw.HighPart},local{};SYSTEMTIME at{};
    if(!FileTimeToLocalFileTime(&utc,&local)||!FileTimeToSystemTime(&local,&at))return {};
    char value[32]{};snprintf(value,sizeof(value),"%04u-%02u-%02u %02u:%02u:%02u",at.wYear,at.wMonth,at.wDay,at.wHour,at.wMinute,at.wSecond);return value;
}
struct Session {
    DWORD account=0,pid=0,registeredPid=0;ULONGLONG created=0;std::wstring root;
    std::string source="unverified",reason="steam-process-unverified",accountSource="unverified";
    bool accountPinned=false,liveAccountPinned=false,foreignRegistryAccountIgnored=false;
};
static std::mutex liveAccountMx;
static Session liveAccountSession;
static bool sameClientProcess(const Session& a,const Session& b) {
    return a.pid&&a.created&&!a.root.empty()&&a.pid==b.pid&&a.created==b.created&&pathKey(W2U(a.root))==pathKey(W2U(b.root));
}
static json liveRequest(const Session& client,json request,const std::function<bool()>& admit={},ymcc::steamrecordidentity::Read* privateIdentity=nullptr) {
    request["sleepSessionProcess"]={{"pid",client.pid},{"created",std::to_string(client.created)},{"root",W2U(client.root)}};
    if(privateIdentity)request["nativeRecordIdentityRead"]=true;
    auto reply=steamLiveRequest(client.account,std::move(request),admit);
    auto observed=ymcc::steamrecordidentity::consume(reply); // strip complete account before any export
    if(privateIdentity)*privateIdentity=std::move(observed);
    return reply;
}
static bool verifiedClientProcess(DWORD pid,const std::wstring& root,ULONGLONG& created) {
    if(!pid||root.empty())return false;
    std::vector<wchar_t> image(32768);DWORD length=static_cast<DWORD>(image.size());
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
    if(!process)return false;
    const bool imageKnown=QueryFullProcessImageNameW(process,0,image.data(),&length)!=FALSE;
    const auto birth=nativeProcessCreatedFromHandle(process);
    const bool alive=WaitForSingleObject(process,0)==WAIT_TIMEOUT;
    CloseHandle(process);
    if(!imageKnown||!birth||!alive||pathKey(W2U(std::wstring(image.data(),length)))!=pathKey(W2U(root+L"\\steam.exe")))return false;
    created=birth;return true;
}
static bool validSession(const Session& s){return s.account&&s.pid&&s.created&&!s.root.empty();}
static Session session() {
    Session s;DWORD registeredAccount=0;
    sofRegDWORD(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"ActiveUser",&registeredAccount);
    sofRegDWORD(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"pid",&s.registeredPid);
    s.root=sofRegSZ(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath");
    while(!s.root.empty()&&(s.root.back()==L'\\'||s.root.back()==L'/'))s.root.pop_back();
    const auto observed=g_steamSettingsObserver.sessionSnapshot();
    // Reuse the event-driven verified process cache. A registry PID is merely
    // discovery evidence; it may point at an exited/reused non-Steam process.
    const bool rootMatches=!s.root.empty()&&pathKey(W2U(s.root))==pathKey(W2U(observed.root));
    ULONGLONG registeredBirth=0,observedBirth=0;
    const bool registeredValid=verifiedClientProcess(s.registeredPid,s.root,registeredBirth);
    // In this exact branch either outcome of a second query selects the same
    // registered instance; a successful observation cannot make it ambiguous.
    const bool reuseRegistered=registeredValid&&rootMatches&&observed.pid==s.registeredPid&&observed.created==registeredBirth;
    if(reuseRegistered)observedBirth=registeredBirth;
    const bool observedValid=reuseRegistered||(rootMatches&&verifiedClientProcess(observed.pid,s.root,observedBirth)&&observedBirth==observed.created);
    if(registeredValid&&observedValid&&(s.registeredPid!=observed.pid||registeredBirth!=observedBirth)){
        s.reason="steam-process-ambiguous";return s;
    }
    if(registeredValid){s.pid=s.registeredPid;s.created=registeredBirth;s.source="verified-active-process";}
    else if(observedValid){s.pid=observed.pid;s.created=observedBirth;s.source="verified-session-observer";}
    if(registeredValid){s.account=registeredAccount;s.accountSource="verified-active-process";}
    else if(observedValid){
        // The observer also samples ActiveUser. Neither its account nor the
        // registry account can be paired with another verified steam.exe.
        s.foreignRegistryAccountIgnored=true;std::lock_guard<std::mutex> lock(liveAccountMx);
        if(sameClientProcess(s,liveAccountSession)&&validSession(liveAccountSession)&&liveAccountSession.accountPinned){
            s.account=liveAccountSession.account;s.accountPinned=true;s.liveAccountPinned=liveAccountSession.liveAccountPinned;s.accountSource=liveAccountSession.accountSource;
        }
    }
    if(!s.pid)s.reason="steam-process-unverified";
    else if(!s.account)s.reason="steam-account-not-ready";
    else s.reason="steam-native-session-current";
    return s;
}
// Only capture preparation may discover a UI account. A resume inspection
// uses the pinned account and the original PID/birth/root; it never rebinds.
static Session prepareSession() {
    auto before=session();
    const auto lifecycle=g_powerLifecycle.load();
    const auto captureGeneration=currentPowerGeneration(),captureEpoch=g_sleepRecoveryAutoConfigEpoch.load();
    if(before.source!="verified-session-observer"){
        if(validSession(before)&&(lifecycle==PowerLifecycle::Ready||lifecycle==PowerLifecycle::Suspending)){
            before.accountPinned=true;before.accountSource="verified-active-process-capture";
            std::lock_guard<std::mutex> lock(liveAccountMx);liveAccountSession=before;
        }
        return before;
    }
    if(!before.pid||!before.created||before.root.empty())return before;
    if(lifecycle!=PowerLifecycle::Ready&&lifecycle!=PowerLifecycle::Suspending){
        if(!before.accountPinned){before.account=0;before.reason="steam-ui-account-not-bound-before-wake";}
        return before;
    }
    {std::lock_guard<std::mutex> lock(liveAccountMx);liveAccountSession={};}
    before.account=0;
    const auto read=liveRequest(before,{{"operation","sleep.session.inspect"}});
    auto now=session();
    const auto afterLifecycle=g_powerLifecycle.load();
    if(currentPowerGeneration()!=captureGeneration||g_sleepRecoveryAutoConfigEpoch.load()!=captureEpoch||
        (afterLifecycle!=PowerLifecycle::Ready&&afterLifecycle!=PowerLifecycle::Suspending)){
        before.reason="steam-ui-account-capture-cancelled";return before;
    }
    if(!sameClientProcess(before,now)){before.reason="steam-session-changed";return before;}
    const auto account=read.contains("observedAccount")&&read["observedAccount"].is_number_unsigned()?read["observedAccount"].get<unsigned long long>():0;
    if(!flag(read,"ok")||!read.contains("mutated")||!read["mutated"].is_boolean()||flag(read,"mutated")||!account||account>MAXDWORD){
        before.reason=read.value("reason",std::string("steam-ui-account-read-unavailable"));return before;
    }
    now.account=static_cast<DWORD>(account);now.accountPinned=true;now.liveAccountPinned=true;now.accountSource="verified-steam-ui-capture";
    now.reason="steam-native-session-current";
    {std::lock_guard<std::mutex> lock(liveAccountMx);liveAccountSession=now;}
    return now;
}
static bool sameSession(const Session& before) {
    const auto now=session();return validSession(before)&&validSession(now)&&before.account==now.account&&before.pid==now.pid&&before.created==now.created&&pathKey(W2U(before.root))==pathKey(W2U(now.root));
}
static json sessionState(const Session& before,const Session& now,bool capturedAtSuspend) {
    json state={{"status","waiting"},{"ready",false},{"retryable",true},{"reason",now.reason}};
    if(capturedAtSuspend&&!validSession(before)){state["status"]="unbound";state["retryable"]=false;state["reason"]="steam-original-session-not-captured";return state;}
    const bool changed=capturedAtSuspend&&
        ((!now.root.empty()&&pathKey(W2U(before.root))!=pathKey(W2U(now.root)))||
         (now.account&&before.account!=now.account)||(now.pid&&(before.pid!=now.pid||before.created!=now.created)));
    if(changed){state["status"]="changed";state["retryable"]=false;state["reason"]="steam-original-session-changed";return state;}
    if(validSession(now)){state["status"]="current";state["ready"]=true;state["retryable"]=false;state["reason"]="steam-native-session-current";}
    return state;
}
static json clientIdentity(const Session& s) {
    // Matching evidence, not an authorization token; never export raw account IDs.
    const auto key=pathKey(W2U(s.root))+"|"+std::to_string(s.account)+"|"+std::to_string(s.pid)+"|"+std::to_string(s.created);
    return {{"pid",s.pid},{"created",std::to_string(s.created)},{"identityToken",std::to_string(std::hash<std::string>{}(key))},
        {"integrityRid",boundIntegrityRid(s.pid,s.created)},{"processVerified",s.pid!=0&&s.created!=0},
        {"accountKnown",s.account!=0},{"accountPinned",s.accountPinned},{"accountSource",s.accountSource},{"liveAccountPinned",s.liveAccountPinned},{"foreignRegistryAccountIgnored",s.foreignRegistryAccountIgnored},{"source",s.source},{"reason",s.reason},{"registeredPid",s.registeredPid},{"registeredPidMatchesVerified",s.pid!=0&&s.registeredPid==s.pid}};
}
// Discovery provenance / registry PID / access to a diagnostic integrity
// probe may settle independently. They are NOT the verified Steam session.
// The native admit/postflight still checks the real account/PID/birth/root.
static bool sameClientEvidence(const json& a,const json& b) {
    if(!a.is_object()||!b.is_object()||!flag(a,"processVerified")||!flag(b,"processVerified")||!flag(a,"accountKnown")||!flag(b,"accountKnown"))return false;
    const auto pid=number(a,"pid");if(!pid||pid>0xffffffffULL||pid!=number(b,"pid"))return false;
    for(const auto key:{"created","identityToken"}) {
        const auto x=a.find(key),y=b.find(key);std::uint64_t value=0;
        if(x==a.end()||y==b.end()||!x->is_string()||!y->is_string()||*x!=*y)return false;
        const auto raw=x->get<std::string>();
        if(!unsignedDecimal(raw,value)||std::to_string(value)!=raw||(!value&&std::string(key)=="created"))return false;
    }
    return true;
}
struct TargetSelection {Game game;Binding binding;json evidence;};
// User-authorized grace for slow read-only capture; one cumulative budget.
// It never extends the captured sleep boundary or permits a later-born target.
static constexpr ULONGLONG captureReadBudgetMs=1000;
// Read-only join against the SAME one-PID detector used by YMCC's game valve.
// This does not elect/publish a new shared target or acquire its sleep lease.
struct CaptureGameRules {
    std::vector<std::wstring> excludes,whitelist,configured;
    unsigned long long epoch=0;
};
static CaptureGameRules captureGameRules() {
    CaptureGameRules rules;std::lock_guard<std::mutex> lock(g_gameRulesMx);
    nativeRefreshGameRulesEpochFromDiskLocked();
    rules.excludes=sgExcludes();rules.whitelist=sgGameWhitelist();rules.configured=nativeConfiguredGameExes();
    rules.epoch=g_gameRulesEpoch.load(std::memory_order_acquire);return rules;
}
static bool sameGameIdentity(const Game& a,const Game& b) {
    return a.pid&&a.created&&!a.path.empty()&&a.pid==b.pid&&a.created==b.created&&pathKey(W2U(a.path))==pathKey(W2U(b.path));
}
// The shared detector intentionally clears its output on rejection. This
// follow-up sample explains a refused Steam candidate without changing that
// verdict. It is a separate read, so its reason is observational, never an
// authoritative account of the earlier handle or permission to repair.
static json observeRejectedYmccGame(const Game& game,const CaptureGameRules& rules) {
    json sample={{"source","steam-read-only-followup"},{"authoritative",false},{"sampledAfterValidation",true},
        {"readOnly",true},{"admissionGranted",false},{"retargetAllowed",false},{"reason","not-sampled"},
        {"observedAtTick",GetTickCount64()},{"identityMatched",false},{"workingSetKnown",false},{"workingSetBytes",nullptr}};
    const auto name=sgBaseName(game.path);
    const bool whitelisted=sgNameExcludedBy(name,rules.whitelist);
    const bool configured=nativeIsConfiguredGameExe(name,rules.configured);
    const auto minimum=static_cast<SIZE_T>(configured?0:whitelisted?SG_CAPTURE_MIN_WS:SG_MIN_WS);
    sample["exe"]=W2U(name);sample["whitelisted"]=whitelisted;sample["customConfigured"]=configured;
    sample["minimumWorkingSetBytes"]=minimum;sample["rulesEpoch"]=std::to_string(rules.epoch);
    sample["nativeExcluded"]=nativeMonitorExcluded(name);sample["rulesExcluded"]=sgNameExcludedBy(name,rules.excludes);
    const auto finish=[&](const char* reason){sample["reason"]=reason;return sample;};
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,FALSE,game.pid);
    if(!process){sample["openProcessError"]=GetLastError();return finish("process-query-denied-or-unavailable");}
    std::vector<wchar_t> image(32768);DWORD length=static_cast<DWORD>(image.size());
    const bool imageKnown=QueryFullProcessImageNameW(process,0,image.data(),&length)!=FALSE;
    const DWORD imageError=imageKnown?0:GetLastError();
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
    const bool memoryKnown=GetProcessMemoryInfo(process,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))!=FALSE;
    const DWORD memoryError=memoryKnown?0:GetLastError();
    const auto birth=nativeProcessCreatedFromHandle(process);CloseHandle(process);
    sample["imageKnown"]=imageKnown;sample["imageQueryError"]=imageError;
    sample["createdKnown"]=birth!=0;sample["workingSetKnown"]=memoryKnown;sample["memoryQueryError"]=memoryError;
    const Game observed{game.pid,birth,imageKnown?std::wstring(image.data(),length):std::wstring{}};
    const bool identity=sameGameIdentity(game,observed)&&sameGame(game);
    sample["identityMatched"]=identity;
    // Do not attribute a reused PID's memory or rules to the original game.
    if(!imageKnown||!birth)return finish("process-identity-unavailable");
    if(!identity)return finish("process-instance-changed-or-unavailable");
    if(memoryKnown)sample["workingSetBytes"]=memory.WorkingSetSize;
    DWORD ours=0,theirs=0;
    const bool sessionKnown=ProcessIdToSessionId(GetCurrentProcessId(),&ours)&&ProcessIdToSessionId(game.pid,&theirs);
    sample["windowsSessionKnown"]=sessionKnown;sample["windowsSessionMatched"]=sessionKnown&&ours==theirs;
    if(!whitelisted&&(nativeMonitorExcluded(name)||sgNameExcludedBy(name,rules.excludes)))return finish("game-name-excluded");
    if(!sessionKnown)return finish("windows-session-unavailable");
    if(ours!=theirs)return finish("different-windows-session");
    if(!memoryKnown)return finish("working-set-query-unavailable");
    if(memory.WorkingSetSize<minimum)return finish("working-set-below-current-rule-minimum");
    return finish("no-blocker-observed-in-followup");
}
struct NativePrimaryRead {NativePrimaryProcessInventory inventory;json log;};
static NativePrimaryRead readNativePrimary(const Session& client,ULONGLONG boundary) {
    NativePrimaryRead result;const auto tail=SleepRecoveryApi::readTail(client.root+L"\\logs\\console_log.txt");result.log=tail.meta;
    if(!flag(tail.meta,"available")){result.inventory.reason="steam-native-primary-log-unavailable";return result;}
    if(flag(tail.meta,"truncated")){result.inventory.complete=false;result.inventory.reason="steam-native-primary-log-truncated";return result;}
    result.inventory=nativePrimaryProcessInventoryText(tail.bytes,localSecond(client.created),localSecond(boundary));return result;
}
static json nativePrimaryJoin(const NativePrimaryRead& primary,const TrackedProcessInventory& tracker,std::uint32_t app,DWORD pid) {
    json result={{"source","steam-local-native-procid-diagnostic"},{"status","unknown"},{"originalAllowed",false},{"nativePrimaryVerified",false},{"expectedPid",pid},{"appid",app},{"log",primary.log}};
    unsigned members=0;bool originalTracked=false;for(const auto& row:tracker.rows)if(row.appid==app){++members;if(row.pid==pid)originalTracked=true;}
    result["trackedMemberCount"]=members;
    if(!tracker.complete||!primary.inventory.complete){result["status"]="unverifiable";result["reason"]=primary.inventory.reason;return result;}
    const auto found=primary.inventory.rows.find(app);
    if(found==primary.inventory.rows.end()){
        // Existing exact single-member authority is retained, explicitly without
        // claiming native-primary proof. Multiple members never use this fallback.
        result["status"]=members==1&&originalTracked?"unique-tracker-native-primary-unobserved":"unknown";
        result["reason"]=primary.inventory.reason.empty()?"steam-native-primary-not-observed":primary.inventory.reason;
        result["originalAllowed"]=members==1&&originalTracked;return result;
    }
    const auto& row=found->second;result["nativePrimaryPid"]=row.pid;result["at"]=row.at;result["lineInTail"]=row.line;result["label"]=row.label;
    if(row.removed){result["status"]="removed";return result;}
    if(!row.local){result["status"]="non-local-record";return result;}
    result["nativePrimaryVerified"]=true;result["status"]=row.pid==pid?"matched":"different-instance";result["originalAllowed"]=row.pid==pid&&originalTracked;return result;
}
static bool nativePrimaryAllowsOriginal(const Session& client,const Game& game,std::uint32_t app,bool requireVerified=false) {
    if(!app||!game.pid)return false;const auto tail=SleepRecoveryApi::readTail(client.root+L"\\logs\\gameprocess_log.txt");
    if(!flag(tail.meta,"available")||flag(tail.meta,"truncated"))return false;
    std::vector<std::string> lines;std::istringstream input(tail.bytes);std::string line;while(std::getline(input,line))lines.push_back(line);
    const auto binding=track(lines,game.pid,W2U(game.path),localSecond(game.created>20000000ULL?game.created-20000000ULL:0),localSecond(game.created<=ULLONG_MAX-30000000ULL?game.created+30000000ULL:game.created));
    const auto proof=nativePrimaryJoin(readNativePrimary(client,sgNowFileTime()),trackedProcessInventory(lines),app,game.pid);
    return binding.status=="tracked"&&binding.appid==app&&flag(proof,"originalAllowed")&&(!requireVerified||flag(proof,"nativePrimaryVerified"));
}
static TargetSelection selectPreSleepSteamTarget(const Session& client,ULONGLONG boundary,const Game& preferred) {
    TargetSelection result;result.evidence={{"status","unbound"},{"reason","steam-original-target-not-bound"},
        {"source","none"},{"preferredPid",preferred.pid},{"preferredCreated",std::to_string(preferred.created)},
        {"preferredExe",W2U(sgBaseName(preferred.path))},{"selectedPid",0},{"selectedAppid",0},
        {"bindingPolicy","ymcc-steam-exact-instance-join-v1"},{"jointBindingVerified",false},
        {"sharedCandidateMatched",false},{"sharedCandidateModified",false},{"sharedCandidateStatus",preferred.pid?"not-yet-joined":"not-available"},
        {"readOnly",true},{"postWakeRetargetAllowed",false},{"physicalPreSleepVerified",false}};
    if(!boundary||!validSession(client)||client.created>boundary||!sameSession(client)){result.evidence["reason"]="steam-capture-session-unavailable";return result;}
    const auto started=GetTickCount64();auto checkpointTick=started;
    const auto expired=[&](const char* stage=nullptr){
        const auto now=GetTickCount64();auto& budget=result.evidence["captureReadBudget"];
        if(!budget.is_object())budget={{"clock","GetTickCount64"},{"maximumMs",captureReadBudgetMs},{"stage","started"},{"checkpoints",json::array()}};
        if(stage){budget["stage"]=stage;budget["stageElapsedMs"]=now>=checkpointTick?now-checkpointTick:0;
            budget["checkpoints"].push_back({{"stage",stage},{"stageElapsedMs",budget["stageElapsedMs"]},{"elapsedMs",now>=started?now-started:0}});checkpointTick=now;}
        budget["elapsedMs"]=now>=started?now-started:0;budget["clockRegressed"]=now<started;
        return now<started||now-started>captureReadBudgetMs;
    };
    const auto rules=captureGameRules();
    if(expired("ymcc-rules-snapshot")){result.evidence["reason"]="steam-capture-read-budget-exceeded";return result;}
    result.evidence["ymccRulesEpoch"]=std::to_string(rules.epoch);
    const auto tail=SleepRecoveryApi::readTail(client.root+L"\\logs\\gameprocess_log.txt");
    result.evidence["trackerLog"]=tail.meta;
    if(expired("tracker-read")){result.evidence["reason"]="steam-capture-read-budget-exceeded";return result;}
    if(!flag(tail.meta,"available")||flag(tail.meta,"truncated")){result.evidence["reason"]="steam-capture-tracker-not-complete";return result;}
    std::vector<std::string> lines;std::istringstream input(tail.bytes);std::string line;
    while(std::getline(input,line))lines.push_back(line);
    const auto inventory=trackedProcessInventory(lines);
    if(expired("tracker-parse")){result.evidence["reason"]="steam-capture-read-budget-exceeded";return result;}
    const auto primary=readNativePrimary(client,boundary);result.evidence["nativePrimaryLog"]=primary.log;
    if(expired("native-primary-read-and-parse")){result.evidence["reason"]="steam-capture-read-budget-exceeded";return result;}
    if(!inventory.complete){result.evidence["reason"]=inventory.reason;return result;}
    const auto bound=[&](const Game& game){
        if(!game.created||game.created>boundary||!sameGame(game))return Binding{};
        DWORD ours=0,theirs=0;if(!ProcessIdToSessionId(GetCurrentProcessId(),&ours)||!ProcessIdToSessionId(game.pid,&theirs)||ours!=theirs)return Binding{};
        return track(lines,game.pid,W2U(game.path),localSecond(game.created>20000000ULL?game.created-20000000ULL:0),
            localSecond(game.created<=ULLONG_MAX-30000000ULL?game.created+30000000ULL:game.created));
    };
    const auto ymccValidated=[&](const Game& game,json& evidence){
        NativeDetectedGame detected;
        const auto verdict=nativeScanGameOnePid(game.pid,L"",rules.excludes,rules.whitelist,rules.configured,&detected);
        const Game identity{detected.pid,detected.processCreated,detected.path};
        const bool exact=verdict==NativeOnePidResult::Ok&&sameGameIdentity(game,identity)&&sameGame(game);
        evidence={{"source","nativeScanGameOnePid"},{"pid",game.pid},{"created",std::to_string(game.created)},
            {"status",exact?"accepted":verdict==NativeOnePidResult::Ok?"identity-mismatch":verdict==NativeOnePidResult::NameUnavailable?"name-unavailable":"rejected"},
            {"exactProcessIdentity",exact},{"rulesEpoch",std::to_string(rules.epoch)}};
        evidence["validatorReasonAvailable"]=verdict==NativeOnePidResult::Ok;
        evidence["sharedCandidateIdentityMatched"]=sameGameIdentity(preferred,game);
        evidence["sharedCandidateStatus"]=!preferred.pid?"not-available":sameGameIdentity(preferred,game)?"same-instance":"different-instance";
        if(exact){evidence["workingSetBytes"]=detected.workingSet;evidence["whitelisted"]=detected.whitelisted;evidence["customConfigured"]=detected.customConfigured;}
        else if(verdict!=NativeOnePidResult::Ok&&!expired())evidence["rejectionObservation"]=observeRejectedYmccGame(game,rules);
        return exact;
    };
    if(expired()){result.evidence["reason"]="steam-capture-read-budget-exceeded";return result;}
    const auto preferredBinding=bound(preferred);result.evidence["preferredTrackerStatus"]=preferredBinding.status;
    Game chosen;Binding binding;std::string source;json validation;
    const auto preferredPrimary=nativePrimaryJoin(primary,inventory,preferredBinding.appid,preferred.pid);result.evidence["preferredNativePrimary"]=preferredPrimary;
    if(preferredBinding.status=="tracked"&&flag(preferredPrimary,"originalAllowed")){
        if(!ymccValidated(preferred,validation)){
            result.evidence["ymccPidValidation"]=validation;result.evidence["reason"]="steam-original-target-ymcc-pid-not-admitted";return result;
        }
        chosen=preferred;binding=preferredBinding;source="ymcc-candidate-exact-steam-binding";
    } else {
        // Reverse join: Steam discovers candidate IDs, then YMCC's existing
        // full one-PID rules (including working-set/configuration exceptions)
        // and exact OS identity must admit the SAME process. A window, Steam
        // row or shared YMCC current candidate alone never grants permission.
        unsigned eligible=0,unresolved=0,primaryRejected=0;json candidates=json::array(),rejected=json::array(),primaryRejections=json::array();
        for(const auto& row:inventory.rows){
            if(expired()){result.evidence["reason"]="steam-capture-read-budget-exceeded";return result;}
            const auto rowPrimary=nativePrimaryJoin(primary,inventory,row.appid,row.pid);
            if(!flag(rowPrimary,"originalAllowed")){++primaryRejected;if(primaryRejections.size()<16)primaryRejections.push_back(rowPrimary);continue;}
            const auto logged=pathKey(row.path);
            if(!(logged.size()>2&&logged[1]==':'&&logged[2]=='\\')&&logged.rfind("\\\\",0)!=0)continue;
            Game game;game.pid=row.pid;
            const bool queried=focusQueryProcessIdentity(game.pid,&game.path,&game.created);
            const auto window=focusFindWindowForPid(game.pid);
            if(!window||!focusIsRestorableExactWindow(window,game.pid))continue;
            if(!queried||pathKey(W2U(game.path))!=logged){++unresolved;continue;}
            if(game.pid==preferred.pid&&preferred.created&&!sameGameIdentity(game,preferred)){++unresolved;continue;}
            const auto name=sgBaseName(game.path);if(name.empty()||(!sgNameExcludedBy(name,rules.whitelist)&&
                (nativeMonitorExcluded(name)||sgNameExcludedBy(name,rules.excludes))))continue;
            const auto proof=bound(game);if(proof.status!="tracked"||proof.appid!=row.appid||!sameGame(game)){++unresolved;continue;}
            json checked;
            if(!ymccValidated(game,checked)){
                checked["appid"]=proof.appid;checked["steamTrackerStatus"]=proof.status;checked["exactTrackerPath"]=proof.exactPath;
                checked["ownWindowObserved"]=true;
                ++unresolved;if(rejected.size()<16)rejected.push_back(checked);continue;
            }
            ++eligible;if(candidates.size()<16)candidates.push_back({{"pid",game.pid},{"created",std::to_string(game.created)},
                {"exe",W2U(name)},{"appid",proof.appid},{"exactPath",proof.exactPath},{"ownWindowObserved",true},{"ymccPidValidation",checked}});
            chosen=game;binding=proof;validation=checked;
        }
        result.evidence["eligibleCandidates"]=candidates;result.evidence["eligibleCandidateCount"]=eligible;
        result.evidence["nativePrimaryRejectedCandidates"]=primaryRejections;result.evidence["nativePrimaryRejectedCount"]=primaryRejected;
        result.evidence["unresolvedOwnWindowCount"]=unresolved;result.evidence["ymccRejectedCandidates"]=rejected;
        if(unresolved){result.evidence["reason"]=rejected.empty()?"steam-original-target-own-window-unverifiable":"steam-original-target-ymcc-pid-not-admitted";return result;}
        if(eligible!=1){result.evidence["reason"]=eligible?"steam-original-target-ambiguous":primaryRejected?"steam-original-target-native-primary-unverifiable":"steam-original-target-not-bound";return result;}
        source="unique-exact-tracked-own-window-before-suspend";
    }
    if(expired("candidate-identity-and-ymcc-validation")){result.evidence["reason"]="steam-capture-read-budget-exceeded";return result;}
    if(g_gameRulesEpoch.load(std::memory_order_acquire)!=rules.epoch){result.evidence["reason"]="steam-capture-ymcc-rules-changed";return result;}
    if(!sameSession(client)||!sameGame(chosen)||!nativePrimaryAllowsOriginal(client,chosen,binding.appid,flag(nativePrimaryJoin(primary,inventory,binding.appid,chosen.pid),"nativePrimaryVerified"))){result.evidence["reason"]="steam-capture-binding-changed";return result;}
    if(expired("final-tracker-and-primary-validation")){result.evidence["reason"]="steam-capture-read-budget-exceeded";return result;}
    const bool agrees=sameGameIdentity(preferred,chosen);
    result.game=chosen;result.binding=binding;
    result.evidence["status"]="bound";result.evidence["reason"]="steam-original-target-bound";result.evidence["source"]=source;
    result.evidence["selectedPid"]=chosen.pid;result.evidence["selectedCreated"]=std::to_string(chosen.created);
    result.evidence["selectedExe"]=W2U(sgBaseName(chosen.path));result.evidence["selectedAppid"]=binding.appid;
    const auto selectedPrimary=nativePrimaryJoin(primary,inventory,binding.appid,chosen.pid);
    if(!flag(selectedPrimary,"originalAllowed")){result.evidence["reason"]="steam-capture-native-primary-unverifiable";return TargetSelection{Game{},result.evidence};}
    result.evidence["nativePrimary"]=selectedPrimary;
    result.evidence["ymccPidValidation"]=validation;result.evidence["jointBindingVerified"]=true;
    result.evidence["sharedCandidateMatched"]=agrees;
    result.evidence["sharedCandidateStatus"]=agrees?"same-original-instance":preferred.pid?"different-instance":"not-available";
    result.evidence["ymccSteamJoin"]={{"status","exact-instance-matched"},{"ymccValidatedPid",chosen.pid},{"steamTrackedPid",chosen.pid},
        {"appid",binding.appid},{"birthMatched",true},{"pathMatched",true},{"windowsSessionMatched",true},
        {"preSleepOnly",true},{"sharedCandidateMatched",agrees},{"gameInputVerified",false},{"continueButtonVerified",false}};
    result.evidence["exactTrackerPath"]=binding.exactPath;result.evidence["observedAtTick"]=GetTickCount64();return result;
}
struct NativeRecordBinding {
    std::uint64_t account64=0,gameId64=0;std::uint32_t appid=0;DWORD windowsSession=0;
    bool bound=false;std::string reason="native-record-pre-capture-identity-unavailable";
    ymcc::shortcutmetadatawin::Witness shortcutMetadata; // private only; never JSON/logged
};
struct Captured {
    unsigned long long generation=0,configEpoch=0,boundaryGeneration=0;
    ULONGLONG at=0,powerBoundaryFileTime=0;Game game;Session client;json inputBefore,targetSelection;
    NativeRecordBinding nativeRecord; // private in-memory identity, never serialized
    std::string boundary="queued-suspend";
};
static std::mutex captureMx;
static Captured captured,prepared;
static bool preparedPending=false;
// A repeated notification can advance the shared power generation while the
// same suspend episode still owns its original. This Steam-only marker lets
// captureSuspend retain that original without binding a new account or game.
static unsigned long long repeatedSuspendGeneration=0,repeatedSuspendEpoch=0;
// Read-only baseline fence; process/power/account ownership remains with owned.
static bool shortcutMetadataBaselineAllowed(unsigned long long generation) {
    std::lock_guard<std::mutex> lock(captureMx);
    return !preparedPending&&captured.generation!=generation;
}
static Captured capturedFor(unsigned long long generation) {
    std::lock_guard<std::mutex> lock(captureMx);
    return captured.generation==generation&&captured.configEpoch==g_sleepRecoveryAutoConfigEpoch.load()?captured:Captured{};
}
static Captured wakeBoundaryFor(unsigned long long generation) {
    std::lock_guard<std::mutex> lock(captureMx);
    // A resume observation closes the repeated-suspend handoff, even when
    // the shared coalescer ignored its last duplicate notification.
    repeatedSuspendGeneration=0;repeatedSuspendEpoch=0;
    const auto epoch=g_sleepRecoveryAutoConfigEpoch.load();
    if(preparedPending&&prepared.configEpoch==epoch&&
        (prepared.generation==generation||(prepared.generation!=ULLONG_MAX&&prepared.generation+1==generation)))return prepared;
    return captured.generation==generation&&captured.configEpoch==epoch?captured:Captured{};
}
struct NativeRecordPrefetch {
    Session client;Game game;NativeRecordBinding identity;
    ymcc::steamrecordidentity::PrefetchWitness witness;json nativeBefore;
};
static std::mutex nativeRecordPrefetchMx;
static NativeRecordPrefetch nativeRecordPrefetch;
// Piggyback only on the already-running exact-game read. No new polling, RPC,
// listener, capture-thread wait, or post-wake identity adoption is introduced.
static json nativeRecordDiagnostic(const Captured& original,const Game& game,const Session& client,const ymcc::steamrecordidentity::Read& ui,unsigned long long generation,bool stable);
static json prefetchNativeRecordIdentity(const Session& client,const Game& game,DWORD appid,const ymcc::steamrecordidentity::Read& identity,unsigned long long generation,unsigned long long epoch,const std::string& exactTrackerGameId,const json& ui) {
    try {
    std::uint64_t fullGameId=0;
    if(!identity.available||identity.appid!=appid||!ymcc::sleepsteam::unsignedDecimal(exactTrackerGameId,fullGameId)||identity.gameId64!=fullGameId||
        !generation||!epoch||!g_sleepRecoveryAutoEnabled.load()||g_exitRequested.load()||g_powerLifecycle.load()!=PowerLifecycle::Ready||
        currentPowerGeneration()!=generation||g_sleepRecoveryAutoConfigEpoch.load()!=epoch||!sameSession(client)||!sameGame(game))return ymcc::steamrecordwin::unavailable("native-record-running-link-proof-unavailable");
    DWORD steamSession=0,gameSession=0;
    if(!ProcessIdToSessionId(client.pid,&steamSession)||!ProcessIdToSessionId(game.pid,&gameSession)||steamSession!=gameSession)return ymcc::steamrecordwin::unavailable("native-record-original-session-unavailable");
    const auto owned=[&]{return g_powerLifecycle.load()==PowerLifecycle::Ready&&currentPowerGeneration()==generation&&g_sleepRecoveryAutoConfigEpoch.load()==epoch&&
        g_sleepRecoveryAutoEnabled.load()&&!g_exitRequested.load()&&sameSession(client)&&sameGame(game);};
    const auto original=capturedFor(generation);json observed;
    ymcc::shortcutmetadatawin::Witness shortcutMetadata;
    if(original.generation&&original.game.pid==game.pid&&original.game.created==game.created) {
        observed=nativeRecordDiagnostic(original,game,client,identity,generation,owned());
        // Only a completed same-owner Follow, including its UI postflight,
        // may retain this private witness. available means snapshot completion,
        // including allocation transitions, not baseline allocation equality.
        // Preserve the original witness unchanged; never adopt current node/record.
        // Missing/failed Follow never baselines.
        if(flag(observed,"available")&&original.nativeRecord.shortcutMetadata.valid&&
            observed.contains("shortcutMetadataDiagnostic")&&flag(observed["shortcutMetadataDiagnostic"],"available"))
            shortcutMetadata=original.nativeRecord.shortcutMetadata;
    } else {
        ymcc::steamrecordwin::Context context;context.steamPid=client.pid;context.steamBirth=client.created;context.steamRoot=client.root;
        context.gamePid=game.pid;context.gameBirth=game.created;context.gamePath=game.path;context.account64=identity.account64;context.gameId64=identity.gameId64;
        context.windowsSession=steamSession;context.sessionBound=true;
        observed=ymcc::steamrecordwin::observe(context,[](const std::wstring& path){return sha256File(path);},owned);
        auto metadata=ymcc::shortcutmetadatawin::unavailable("native-record-original-primary-unavailable");
        if(flag(observed,"available")&&flag(observed,"originalPrimaryMatched")) {
            ymcc::shortcutmetadatawin::Request request;
            request.original=context;request.appid=appid;request.generation=generation;request.epoch=epoch;
            request.phase=ymcc::shortcutmetadatawin::Phase::Baseline;
            request.beforeCaptureAdmitted=shortcutMetadataBaselineAllowed(generation);
            const auto metadataOwned=[&]{return owned()&&shortcutMetadataBaselineAllowed(generation);};
            metadata=ymcc::shortcutmetadatawin::observe(request,[](const std::wstring& path){return sha256File(path);},metadataOwned);
        }
        if(flag(metadata.diagnostic,"available")&&metadata.witness.valid)shortcutMetadata=std::move(metadata.witness);
        observed["shortcutMetadataDiagnostic"]=std::move(metadata.diagnostic);
        observed["captureIdentityBound"]=false;observed["identityBasis"]="current-exact-running-link-before-suspend";
    }
    observed["observationFileTime"]=std::to_string(sgNowFileTime());observed["observerScope"]="independent-read-only-running-link";observed["uiAndNativeAtomic"]=false;
    if(ui.contains("overviews")&&ui["overviews"].is_array())for(const auto& row:ui["overviews"]){
        if(row.is_object()&&number(row,"appid")==appid&&row.value("gameidString",json())==exactTrackerGameId){
            observed["uiOverview"]={{"localDisplayStatus",row.value("localDisplayStatus",json())},{"inRunningList",row.value("inRunningList",json())},{"source","steam-ui-native-overview-store"}};break;
        }
    }
    if(!owned())return ymcc::steamrecordwin::unavailable("native-record-running-link-owner-changed");
    NativeRecordPrefetch value;value.client=client;value.game=game;
    value.identity.account64=identity.account64;value.identity.gameId64=identity.gameId64;value.identity.appid=appid;value.identity.windowsSession=steamSession;
    value.identity.bound=true;value.identity.reason="native-record-pre-capture-ui-bound";
    value.identity.shortcutMetadata=std::move(shortcutMetadata);
    // Completion follows both providers; the existing beforeCapture fence must
    // reject any proof that finished after its original capture boundary.
    value.witness={generation,epoch,GetTickCount64(),sgNowFileTime()};value.nativeBefore=observed;
    {std::lock_guard<std::mutex> lock(nativeRecordPrefetchMx);nativeRecordPrefetch=std::move(value);}
    return observed;
    } catch(...) {return ymcc::steamrecordwin::unavailable("native-record-running-link-read-unavailable");}
}
static void bindNativeRecordIdentity(Captured& value) {
    try {
    if(!flag(value.targetSelection,"jointBindingVerified")||!validSession(value.client)||!value.game.pid||!g_sleepRecoveryAutoEnabled.load())return;
    NativeRecordPrefetch prior;{std::lock_guard<std::mutex> lock(nativeRecordPrefetchMx);prior=nativeRecordPrefetch;}
    const auto app=number(value.targetSelection,"selectedAppid");
    if(!prior.identity.bound||app<=0||app>MAXDWORD||prior.identity.appid!=static_cast<DWORD>(app)||
        !sameClientProcess(prior.client,value.client)||prior.client.account!=value.client.account||prior.game.pid!=value.game.pid||prior.game.created!=value.game.created||
        pathKey(W2U(prior.game.path))!=pathKey(W2U(value.game.path))||!sameSession(value.client)||!sameGame(value.game))return;
    if(!ymcc::steamrecordidentity::beforeCapture(prior.witness,value.boundaryGeneration,value.configEpoch,value.at,value.powerBoundaryFileTime)){
        value.nativeRecord.reason="native-record-pre-capture-identity-not-fresh";return;
    }
    auto before=value.inputBefore.is_object()?value.inputBefore:json::object();
    before["nativeRecordPrefetchBefore"]=prior.nativeBefore;
    before["nativeRecordPrefetchWitness"]={{"observedAtTick",prior.witness.observedTick},{"observedFileTime",std::to_string(prior.witness.observedFileTime)},
        {"beforeCaptureTick",value.at},{"maximumAgeMs",60000},{"physicalPreSleepIdentityVerified",false}};
    value.inputBefore=std::move(before);value.nativeRecord=prior.identity;
    } catch(...) {value.nativeRecord=NativeRecordBinding{};}
}
static json nativeRecordDiagnostic(const Captured& original,const Game& game,const Session& client,const ymcc::steamrecordidentity::Read& ui,unsigned long long generation,bool stable) {
    using ymcc::steamrecordwin::unavailable;
    try {
    if(!original.generation||!original.nativeRecord.bound)return unavailable(original.nativeRecord.reason.c_str());
    if(!stable||!sameClientProcess(original.client,client)||client.account!=original.client.account||game.pid!=original.game.pid||game.created!=original.game.created)return unavailable("native-record-original-binding-changed");
    if(!ui.available)return unavailable(ui.reason.c_str());
    if(ui.account64!=original.nativeRecord.account64||ui.gameId64!=original.nativeRecord.gameId64||ui.appid!=original.nativeRecord.appid)return unavailable("native-record-ui-original-identity-changed");
    ymcc::steamrecordwin::Context context;
    context.steamPid=original.client.pid;context.steamBirth=original.client.created;context.steamRoot=original.client.root;
    context.gamePid=original.game.pid;context.gameBirth=original.game.created;context.gamePath=original.game.path;
    context.account64=original.nativeRecord.account64;context.gameId64=original.nativeRecord.gameId64;
    context.windowsSession=original.nativeRecord.windowsSession;context.sessionBound=true;
    const auto owned=[&]{return generation==currentPowerGeneration()&&original.configEpoch==g_sleepRecoveryAutoConfigEpoch.load()&&
        g_sleepRecoveryAutoEnabled.load()&&!g_exitRequested.load()&&sameGame(original.game)&&sameSession(original.client);};
    const auto recordReadStartedTick=GetTickCount64();
    const auto recordReadStartedFileTime=sgNowFileTime();
    auto result=ymcc::steamrecordwin::observe(context,[](const std::wstring& path){return sha256File(path);},owned);
    auto metadata=ymcc::shortcutmetadatawin::unavailable("native-record-original-primary-unavailable");
    if(flag(result,"available")&&flag(result,"originalPrimaryMatched")) {
        const auto& witness=original.nativeRecord.shortcutMetadata;
        if(witness.valid) {
            ymcc::shortcutmetadatawin::Request request;
            request.original=context;request.appid=original.nativeRecord.appid;
            // The witness belongs to the baseline generation, which can precede
            // the current wake power generation. Epoch remains the original one.
            request.generation=witness.scope.generation;request.epoch=original.configEpoch;
            request.phase=ymcc::shortcutmetadatawin::Phase::FollowOriginal;request.witness=witness;
            metadata=ymcc::shortcutmetadatawin::observe(request,[](const std::wstring& path){return sha256File(path);},owned);
        } else metadata=ymcc::shortcutmetadatawin::unavailable("original-witness-missing");
    }
    // Preserve metadataAllocationMatchesBaseline/metadataTransition verbatim.
    // Allocation changes remain evidence; these fields never gate native/UI/input.
    result["shortcutMetadataDiagnostic"]=std::move(metadata.diagnostic);
    const auto recordReadFinishedTick=GetTickCount64();
    const auto recordReadFinishedFileTime=sgNowFileTime();
    result["nativeRecordReadStartedTick"]=recordReadStartedTick;result["nativeRecordReadFinishedTick"]=recordReadFinishedTick;
    result["nativeRecordReadStartedFileTime"]=std::to_string(recordReadStartedFileTime);
    result["nativeRecordReadFinishedFileTime"]=std::to_string(recordReadFinishedFileTime);
    // This is a second, explicitly non-atomic observation of the native UI app
    // object. Its flags must never be consumed by verdict/input/Continue gates.
    const auto appReadStartedTick=GetTickCount64();
    const auto appReadStartedFileTime=sgNowFileTime();
    auto appState=flag(result,"available")
        ?ymcc::steamappstatewin::observe(context,original.nativeRecord.appid,[](const std::wstring& path){return sha256File(path);},owned)
        :ymcc::steamappstatewin::unavailable("native-ui-native-record-bracket-unavailable");
    appState["readStartedTick"]=appReadStartedTick;appState["readFinishedTick"]=GetTickCount64();
    appState["readStartedFileTime"]=std::to_string(appReadStartedFileTime);
    appState["observationFileTime"]=std::to_string(sgNowFileTime());
    appState["uiAndNativeAtomic"]=false;appState["captureIdentityBound"]=true;
    const auto identityPostflightStartedTick=GetTickCount64();
    ymcc::steamrecordidentity::Read postflightIdentity;
    const auto postflightReply=liveRequest(original.client,{{"operation","sleep.session.inspect"},{"appids",json::array({original.nativeRecord.appid})}},owned,&postflightIdentity);
    if(!flag(postflightReply,"ok")||!postflightReply.contains("mutated")||!postflightReply["mutated"].is_boolean()||flag(postflightReply,"mutated")||
        !postflightIdentity.available||postflightIdentity.account64!=original.nativeRecord.account64||postflightIdentity.gameId64!=original.nativeRecord.gameId64||
        postflightIdentity.appid!=original.nativeRecord.appid||postflightIdentity.account64!=ui.account64||postflightIdentity.gameId64!=ui.gameId64||postflightIdentity.appid!=ui.appid||!owned())
        return unavailable("native-record-ui-postflight-identity-unavailable");
    appState["privateUiIdentityMatchedBeforeAfter"]=true;
    appState["identityPostflightStartedTick"]=identityPostflightStartedTick;appState["identityPostflightFinishedTick"]=GetTickCount64();
    appState["identityPostflightFileTime"]=std::to_string(sgNowFileTime());
    result["nativeUiAppState"]=std::move(appState);
    result["uiOriginalIdentityMatched"]=true;result["captureIdentityBound"]=true;result["physicalPreSleepIdentityVerified"]=false;
    result["observationFileTime"]=std::to_string(sgNowFileTime());return result;
    } catch(...) {return unavailable("native-record-original-read-unavailable");}
}
struct Foreground {HWND window=nullptr;DWORD pid=0;ULONGLONG created=0;std::string role="unknown";};
static Foreground foreground(const Game& game,const Session& client) {
    Foreground f;f.window=GetForegroundWindow();if(!f.window)return f;
    GetWindowThreadProcessId(f.window,&f.pid);
    if(f.pid==game.pid&&sameGame(game)){f.created=game.created;f.role="original-game";return f;}
    if(f.pid==GetCurrentProcessId()){f.role="yeman-ui";return f;}
    std::wstring path;if(!focusQueryProcessIdentity(f.pid,&path,&f.created)||!f.created)return f;
    // sgBaseName is a game-name stem: production deliberately strips .exe.
    // Steam UI admission needs the EXACT executable leaf in the verified root,
    // not that stem, a UI title/class, or the currently focused game candidate.
    const auto normalized=pathKey(W2U(path)),root=pathKey(W2U(client.root))+"\\";
    const auto separator=normalized.find_last_of('\\');
    const auto base=separator==std::string::npos?normalized:normalized.substr(separator+1);
    if(f.pid==client.pid&&f.created==client.created)f.role="steam-client";
    else if(normalized.rfind(root,0)==0&&(base=="steamwebhelper.exe"||base=="gameoverlayui.exe"))f.role="steam-ui";
    else {
        wchar_t windows[MAX_PATH]{};if(GetWindowsDirectoryW(windows,MAX_PATH)&&normalized==pathKey(W2U(std::wstring(windows)+L"\\explorer.exe")))f.role="windows-shell";
    }
    return f;
}
static bool allowedForeground(const Foreground& f){return f.role!="unknown";}
static bool sameForeground(const Foreground& old,const Game& game,const Session& client) {
    const auto now=foreground(game,client);return allowedForeground(now)&&now.window==old.window&&now.pid==old.pid&&now.created==old.created&&now.role==old.role;
}
static json gamePauseState(const Game& game) {
    json result={{"available",false},{"paused",true},{"sleepLeaseHeld",false},{"manualPauseHeld",false},{"waitForSleepRelease",false}};
    if(!game.pid||!game.created||!sameGame(game))return result;
    SgSleepTarget owner;const bool memory=sgReadSleepTarget(owner)&&owner.markerOwned&&owner.pid==game.pid&&owner.processCreated==game.created;
    const bool manual=focusPidIsManuallyPaused(game.pid);
    std::error_code error;const bool disk=fspath::exists(SG_SLEEP_LEASE_DIR+L"\\"+std::to_wstring(game.pid)+L".txt",error);
    result["available"]=!error;result["manualPauseHeld"]=manual;result["sleepLeaseHeld"]=memory||disk;
    result["paused"]=manual||memory||disk||bool(error);result["waitForSleepRelease"]=!error&&!manual&&(memory||disk);return result;
}
static json capturedPauseState(unsigned long long generation) {
    const auto original=capturedFor(generation);return gamePauseState(original.game);
}
static bool gameNotPaused(const Game& game) {
    const auto state=gamePauseState(game);return flag(state,"available")&&!flag(state,"paused");
}
// Diagnostic classification only. A caller labels an existing read; the label
// never authorizes/retries an action or changes this inspection's return value.
static void recordInspection(unsigned long long generation, const char* purpose, const json& observed) noexcept {
    try {
        static std::mutex logMx;
        static ymcc::logging::JsonSnapshotLogState previous;
        bool changed;
        { std::lock_guard<std::mutex> lock(logMx);
          changed = previous.observe(observed, generation, ymcc::logging::SnapshotKind::sleepInspection); }
        const std::string_view label = purpose ? purpose : "observation";
        const bool actionBoundary = label != "observation";
        if (!changed && !actionBoundary && !domainLogEnabled("virtual")) return;
        auto facts = observed;
        facts["snapshotSchemaVersion"] = 2;
        facts["snapshotPurpose"] = label;
        facts["snapshotReason"] = actionBoundary ? "action-boundary" : changed ? "state-change" : "periodic";
        if (changed || actionBoundary) sgRecordFact("sleep-steam-inspected", facts);
        else appendNativeLifecycleLog("sleep-steam-inspected", facts);
    } catch (...) { /* Logging is never an inspection or recovery prerequisite. */ }
}
static json inspect(unsigned long long generation, const char* diagnosticPurpose = "observation") {
    const auto original=capturedFor(generation);
    // A captured original that exited is never substituted by a newly detected game.
    const Game game=original.generation?original.game:currentGame();auto client=session();const auto filename=client.root+L"\\logs\\gameprocess_log.txt";
    SleepRecoveryApi::Tail tail;
    if(!client.root.empty()&&client.account&&client.pid&&client.created)tail=SleepRecoveryApi::readTail(filename);
    else tail.meta={{"available",false},{"reason","steam-session-unavailable"}};std::vector<std::string> lines;std::istringstream stream(tail.bytes);std::string line;
    if(ymcc::sleeprecovery::flag(tail.meta,"truncated"))std::getline(stream,line);
    while(std::getline(stream,line))lines.push_back(line);
    const auto earliest=localSecond(game.created>20000000ULL?game.created-20000000ULL:0);
    const auto latest=localSecond(game.created<=ULLONG_MAX-30000000ULL?game.created+30000000ULL:game.created);
    const auto binding=track(lines,game.pid,W2U(game.path),earliest,latest);
    json appids=json::array();if(binding.status=="tracked")appids.push_back(binding.appid);
    json sessionRetry={{"attempted",false},{"attempts",0},{"recovered",false},{"boundedReadOnly",true},{"maxAttempts",2}};
    ymcc::steamrecordidentity::Read privateRecordIdentity;
    const bool wantsNativeRecord=original.generation&&original.nativeRecord.bound;
    const auto uiReadStarted=GetTickCount64();
    auto steam=validSession(client)?liveRequest(client,{{"operation","sleep.inspect"},{"appids",appids}},{},wantsNativeRecord?&privateRecordIdentity:nullptr):json{{"ok",false},{"mutated",false},{"reason",client.reason},{"stage","native-session-preflight"}};
    // Steam may publish ActiveUser/transport state between the first registry
    // read and the first native snapshot while waking. Retry ONLY this
    // read-only inspect twice; never retry a setter or treat a different
    // account/session as the original. A persistent mismatch still fails
    // closed with the current/original session identities in the report.
    const auto initialReadReason=steam.value("reason",std::string());
    if(!flag(steam,"ok")&&(initialReadReason=="steam-account-changed"||initialReadReason=="steam-session-changed")){
        sessionRetry["attempted"]=true;
        for(unsigned attempt=1;attempt<=2&&!flag(steam,"ok");++attempt){
            Sleep(75);
            const auto retryClient=session();
            if(!validSession(retryClient))break;
            client=retryClient;sessionRetry["attempts"]=attempt;
            steam=liveRequest(client,{{"operation","sleep.inspect"},{"appids",appids}},{},wantsNativeRecord?&privateRecordIdentity:nullptr);
        }
        sessionRetry["recovered"]=flag(steam,"ok")&&(!original.generation?sameSession(client):sameSession(original.client));
    }
    const auto uiReadFinished=GetTickCount64();
    const auto gameApiProbe=processInputProbe(game.pid,game.created,client.root);
    const bool processMatches=sameGame(game);
    const bool powerMatches=generation&&generation==currentPowerGeneration()&&!g_exitRequested.load();
    const bool clientReadStable=sameSession(client);
    auto clientState=sessionState(original.client,client,original.generation!=0);
    if(flag(clientState,"ready")&&!clientReadStable){clientState={{"status","waiting"},{"ready",false},{"retryable",true},{"reason","steam-session-read-not-stable"}};}
    const auto readReason=steam.value("reason",std::string());
    if(flag(clientState,"ready")&&!flag(steam,"ok")){
        if(readReason=="steam-account-changed")clientState={{"status","changed"},{"ready",false},{"retryable",false},{"reason","steam-live-account-changed"}};
        else clientState={{"status","waiting-read"},{"ready",false},{"retryable",true},{"reason",readReason.empty()?"steam-read-unavailable":readReason}};
    }
    const bool stable=powerMatches&&processMatches&&flag(clientState,"ready");
    auto nativeRecord=nativeRecordDiagnostic(original,game,client,privateRecordIdentity,generation,stable);
    nativeRecord["uiReadStartedTick"]=uiReadStarted;nativeRecord["uiReadFinishedTick"]=uiReadFinished;
    nativeRecord["uiAndNativeAtomic"]=false; // separate observations; never an action proof
    auto primary=nativePrimaryJoin(readNativePrimary(client,sgNowFileTime()),trackedProcessInventory(lines),binding.appid,game.pid);
    const bool primaryWasVerified=original.generation&&flag(original.targetSelection.value("nativePrimary",json::object()),"nativePrimaryVerified");
    if(primaryWasVerified&&!flag(primary,"nativePrimaryVerified")){primary["originalAllowed"]=false;primary["status"]="unverifiable";primary["reason"]="steam-native-primary-proof-lost";}
    const bool originalMatches=original.generation&&original.game.pid&&original.game.created&&!original.game.path.empty()&&processMatches&&original.game.pid==game.pid&&original.game.created==game.created&&pathKey(W2U(original.game.path))==pathKey(W2U(game.path));
    const bool contactProof=stable&&binding.status=="tracked"&&originalMatches&&flag(primary,"originalAllowed")&&
        flag(original.targetSelection,"jointBindingVerified")&&flag(original.targetSelection,"sharedCandidateMatched");
    auto conclusion=verdict(steam,binding,stable,contactProof);
    conclusion["contactProof"]={{"originalInstanceMatched",originalMatches},{"ymccOnePidVerified",flag(primary,"originalAllowed")},
        {"jointBindingVerified",flag(original.targetSelection,"jointBindingVerified")},{"sharedCandidateMatched",flag(original.targetSelection,"sharedCandidateMatched")},
        {"steamSessionMatched",flag(clientState,"ready")},{"readOnlyContact",true}};
    if(stable&&binding.status=="tracked"&&!flag(primary,"originalAllowed")){conclusion["status"]="blocked";conclusion["reason"]=primary.value("status",std::string())=="different-instance"?"steam-native-primary-game-changed":"steam-native-primary-unverifiable";conclusion["repairable"]=false;conclusion["associationVerified"]=false;}
    if(!flag(clientState,"ready")){conclusion["status"]=clientState.value("status",std::string())=="changed"||clientState.value("status",std::string())=="unbound"?"blocked":"unknown";conclusion["reason"]=clientState.value("reason",std::string());conclusion["repairable"]=false;conclusion["contactRepairable"]=false;conclusion["contactConnected"]=false;conclusion["associationVerified"]=false;}
    if(!powerMatches){conclusion["status"]="unknown";conclusion["reason"]="power-generation-changed";conclusion["repairable"]=false;conclusion["contactRepairable"]=false;conclusion["contactConnected"]=false;conclusion["associationVerified"]=false;}
    const auto fg=foreground(game,client);
    const auto pause=gamePauseState(game);
    const bool originalForeground=fg.role=="original-game"&&fg.pid==game.pid&&fg.created==game.created;
    const bool cachedOriginalFocus=number(steam.value("inputContext",json::object()).value("focus",json::object()),"appid")==binding.appid;
    // Do not turn a cached original focus into permission to steal an actively
    // focused YeManCC settings window. Steam/shell return paths retain UI guards.
    const bool inputForegroundAllowed=allowedForeground(fg)&&(!cachedOriginalFocus||fg.role!="yeman-ui");
    auto inputConclusion=inputVerdict(steam,binding,conclusion,stable,originalMatches,inputForegroundAllowed,originalForeground);
    if(flag(pause,"paused")){
        inputConclusion["status"]="blocked";inputConclusion["repairable"]=false;inputConclusion["windowPreparationRepairable"]=false;
        inputConclusion["reason"]=flag(pause,"waitForSleepRelease")?"steam-original-game-sleep-lease-held":flag(pause,"manualPauseHeld")?"steam-original-game-manually-paused":"steam-original-game-pause-state-unknown";
    }
    json report={{"scope","steam"},{"implementation","steam-wake-round50-native-record-diagnostic"},{"generation",generation},{"healthVerified",false},{"gameInput","not-tested"},
        {"game",{{"pid",game.pid},{"created",std::to_string(game.created)},{"exe",W2U(sgBaseName(game.path))},{"identityMatches",processMatches},{"powerGenerationMatches",powerMatches},{"recoveryBindingMatches",stable}}},
        {"tracker",{{"status",binding.status},{"appid",binding.appid},{"at",binding.at},{"lineInTail",binding.line},{"exactPath",binding.exactPath},{"log",tail.meta}}},
        {"nativePrimary",primary},{"nativeRecordDiagnostic",nativeRecord},{"steamClient",clientIdentity(client)},{"steamSessionState",clientState},{"steamSessionRetry",sessionRetry},{"steam",steam},{"steamRecovery",conclusion},{"steamInputRecovery",inputConclusion},{"continueRecovery",continueVerdict(steam,conclusion)},
        {"suspendCapture",{{"available",originalMatches},{"clientMatched",flag(clientState,"ready")},{"originalClient",clientIdentity(original.client)},{"boundary",original.boundary},{"physicalPreSleepVerified",false},{"atTick",original.at},{"boundaryGeneration",original.boundaryGeneration},{"powerBoundaryFileTime",std::to_string(original.powerBoundaryFileTime)},{"inputBefore",original.inputBefore},{"targetSelection",original.targetSelection}}},
        {"gameApiProbe",gameApiProbe},{"gamePause",pause},
        {"originalGameRecovery",{{"verificationBasis","ymcc-one-pid-steam-tracker-os-identity"},{"originalInstanceMatched",originalMatches},
            {"jointBindingVerified",originalMatches&&flag(original.targetSelection,"jointBindingVerified")&&flag(primary,"originalAllowed")},{"nativePrimaryAllowsOriginal",flag(primary,"originalAllowed")},{"sharedCandidateMatched",flag(original.targetSelection,"sharedCandidateMatched")},
            {"steamSessionMatched",flag(clientState,"ready")},{"sleepPauseReleased",flag(pause,"available")&&!flag(pause,"paused")},
            {"originalWindowFocused",originalForeground},{"steamUiInputFocusMatched",flag(inputConclusion,"contextMatched")},
            {"uiFocusAuthority","auxiliary-not-active-input-proof"},{"gameInputVerified",false},{"continueButtonVerified",false}}},
        {"foreground",{{"pid",fg.pid},{"created",std::to_string(fg.created)},{"role",fg.role},{"originalInstanceMatched",originalForeground},{"allowedForOriginalGameReturn",allowedForeground(fg)}}},{"recordEpoch",sgNowEpoch()}};
    json targetOverview=json::object();
    const auto rows=steam.find("overviews");if(rows!=steam.end()&&rows->is_array())
        for(const auto& row:*rows)if(number(row,"appid")==binding.appid){targetOverview=row;break;}
    try { recordInspection(generation,diagnosticPurpose,{{"generation",generation},{"pid",game.pid},{"appid",binding.appid},
        {"gameCreated",std::to_string(game.created)},{"steamPid",client.pid},{"steamCreated",std::to_string(client.created)},
        {"status",conclusion["status"]},{"reason",conclusion["reason"]},{"steamSessionRetry",sessionRetry},{"steamSessionState",clientState},{"scope","steam"},{"healthVerified",false},
        {"overview",targetOverview},{"inputFocus",steam.value("inputContext",json::object()).value("focus",json::object())},
        {"inputReturnBasis",inputConclusion.value("returnBasis",std::string("none"))},{"foregroundRole",fg.role},
        {"originalWindowFocused",originalForeground},{"nativePrimary",primary},{"requireOriginalWindowReturn",flag(inputConclusion,"requireOriginalWindowReturn")}}); }
    catch (...) { /* The diagnostic projection must not change the returned report. */ }
    return report;
}
// Capture BEFORE unrelated suspend handlers or a delayed Kernel-Power 506 can
// run after wake. Only an unbound fallback account permits a fixed bounded
// read from the verified Steam process; this never suspends or touches devices.
static void prepareSuspend() {
    if(!g_sleepRecoveryAutoEnabled.load()||g_exitRequested.load())return;
    const auto gen=currentPowerGeneration(),epoch=g_sleepRecoveryAutoConfigEpoch.load();
    {std::lock_guard<std::mutex> lock(captureMx);
        repeatedSuspendGeneration=0;repeatedSuspendEpoch=0;
        if(preparedPending&&prepared.generation==gen&&prepared.configEpoch==epoch)return;
        const auto lifecycle=g_powerLifecycle.load();
        if(captured.generation==gen&&captured.configEpoch==epoch&&lifecycle!=PowerLifecycle::Ready){
            if(lifecycle==PowerLifecycle::Suspending||lifecycle==PowerLifecycle::Suspended){
                repeatedSuspendGeneration=gen;repeatedSuspendEpoch=epoch;
            }
            return;
        }}
    Captured value;value.generation=gen;value.configEpoch=epoch;
    value.boundaryGeneration=value.generation;value.at=GetTickCount64();value.powerBoundaryFileTime=sgNowFileTime();value.client=prepareSession();value.boundary="power-suspend-notification";
    NativeDetectedGame target;Game preferred;if(nativeValveReadCurrentTarget(&target))preferred={target.pid,target.processCreated,target.path};
    const auto selected=selectPreSleepSteamTarget(value.client,value.powerBoundaryFileTime,preferred);
    value.game=selected.game;value.targetSelection=selected.evidence;
    bindNativeRecordIdentity(value);
    {
        std::lock_guard<std::mutex> lock(captureMx);
        // Duplicate suspend broadcasts must not replace an earlier original
        // snapshot with a process first seen near/after the delayed wake.
        if(preparedPending&&prepared.generation==value.generation&&prepared.configEpoch==value.configEpoch)return;
        if(captured.generation==value.generation&&captured.configEpoch==value.configEpoch&&g_powerLifecycle.load()!=PowerLifecycle::Ready)return;
        prepared=value;preparedPending=true;
    }
    sgRecordFact("sleep-steam-boundary-prepared",{{"generation",value.generation},{"pid",value.game.pid},{"created",std::to_string(value.game.created)},
        {"boundary",value.boundary},{"identityAvailable",value.game.pid!=0},{"steamClient",clientIdentity(value.client)},{"targetSelection",value.targetSelection},{"nativeRecordIdentityBound",value.nativeRecord.bound},{"nativeRecordIdentityReason",value.nativeRecord.reason},{"physicalPreSleepVerified",false}});
}
static void closeSuspendEpisode(){
    std::lock_guard<std::mutex> lock(captureMx);
    repeatedSuspendGeneration=0;repeatedSuspendEpoch=0;
}
static void cancelPreparedSuspend(){std::lock_guard<std::mutex> lock(captureMx);preparedPending=false;prepared={};repeatedSuspendGeneration=0;repeatedSuspendEpoch=0;}
static void captureSuspend(unsigned long long generation) {
    const auto epoch=g_sleepRecoveryAutoConfigEpoch.load();if(!sleepSteamRecoveryAllows(generation,epoch))return;
    Captured value;value.generation=generation;value.configEpoch=epoch;value.boundaryGeneration=generation;value.at=GetTickCount64();value.powerBoundaryFileTime=sgNowFileTime();
    bool early=false,repeatedOriginal=false;
    {
        std::lock_guard<std::mutex> lock(captureMx);if(captured.generation==generation)return;
        if(preparedPending&&prepared.configEpoch==epoch&&
            (prepared.generation==generation||(prepared.generation!=ULLONG_MAX&&prepared.generation+1==generation))){
            value=prepared;value.generation=generation;early=true;
        }else if(repeatedSuspendGeneration&&repeatedSuspendEpoch==epoch&&
            (g_powerLifecycle.load()==PowerLifecycle::Suspending||g_powerLifecycle.load()==PowerLifecycle::Suspended)&&
            captured.generation==repeatedSuspendGeneration&&captured.configEpoch==epoch&&
            repeatedSuspendGeneration!=ULLONG_MAX&&repeatedSuspendGeneration+1==generation){
            // Only the generation used to route this episode changes. Keep the
            // original boundary, identity, time, metadata and any bound=false
            // refusal exactly as captured; a wake read must never repair them.
            value=captured;value.generation=generation;early=true;repeatedOriginal=true;
        }
        preparedPending=false;prepared={};repeatedSuspendGeneration=0;repeatedSuspendEpoch=0;
    }
    if(!early){value.client=prepareSession();NativeDetectedGame target;Game preferred;if(nativeValveReadCurrentTarget(&target))preferred={target.pid,target.processCreated,target.path};
        const auto selected=selectPreSleepSteamTarget(value.client,value.powerBoundaryFileTime,preferred);value.game=selected.game;value.targetSelection=selected.evidence;bindNativeRecordIdentity(value);}
    {std::lock_guard<std::mutex> lock(captureMx);if(captured.generation==generation)return;captured=value;}
    sgRecordFact("sleep-steam-original-captured",{{"generation",generation},{"pid",value.game.pid},{"created",std::to_string(value.game.created)},
        {"identityAvailable",value.game.pid!=0},{"boundary",value.boundary},{"originalRetainedForRepeatedSuspend",repeatedOriginal},{"steamClient",clientIdentity(value.client)},{"targetSelection",value.targetSelection},{"nativeRecordIdentityBound",value.nativeRecord.bound},{"nativeRecordIdentityReason",value.nativeRecord.reason},{"healthVerified",false}});
    if(value.game.pid&&!poolSubmit([value]{
        if(!sleepSteamRecoveryAllows(value.generation,value.configEpoch)||g_powerLifecycle.load()!=PowerLifecycle::Suspending)return;
        const auto before=inspect(value.generation,"suspend-baseline");
        if(!sleepSteamRecoveryAllows(value.generation,value.configEpoch)||g_powerLifecycle.load()!=PowerLifecycle::Suspending||!sameGame(value.game)||!sameSession(value.client))return;
        std::lock_guard<std::mutex> lock(captureMx);
        if(captured.generation==value.generation&&captured.configEpoch==value.configEpoch){
            const auto priorInput=captured.inputBefore.is_object()?captured.inputBefore:json::object();
            const auto priorRecord=priorInput.value("nativeRecordPrefetchBefore",json::object()),priorWitness=priorInput.value("nativeRecordPrefetchWitness",json::object());
            captured.inputBefore={{"steam",before.value("steam",json::object())},{"nativeRecordDiagnostic",before.value("nativeRecordDiagnostic",json::object())},{"nativeRecordPrefetchBefore",priorRecord},{"nativeRecordPrefetchWitness",priorWitness},
                {"gameApiProbe",before.value("gameApiProbe",json::object())},{"recordEpoch",before.value("recordEpoch",0.0)},{"boundary","suspend-notification-read"},{"physicalPreSleepVerified",false}};
        }
    }))sgRecordFact("sleep-steam-pre-input-capture-skipped",{{"generation",generation},{"reason","worker-queue-full"}});
}
static std::vector<std::wstring> exportLogTails() {
    // Existing user-requested "export all logs" only: bounded raw evidence, not
    // a repair, upload, credential/config copy, or extra screen/confirmation.
    std::vector<std::wstring> files;const auto client=session();const auto folder=SG_DIR+L"\\steam-input-log-tails";
    if(!sgEnsureMarkerDir(folder))return files;
    json manifest={{"schema",1},{"scope","steam"},{"recordEpoch",sgNowEpoch()},{"automaticUpload",false},
        {"gameInputVerified",false},{"isHistoricalLogEvidence",true},{"maxBytesPerFile",2*1024*1024},{"files",json::array()},
        {"privacy","Steam logs can contain game names, executable paths and account-related identifiers; review before sharing. No Steam config, cookie or login files are copied."}};
    const std::vector<std::pair<std::wstring,std::wstring>> names={
        {L"steam-gameprocess.log",L"gameprocess_log.txt"},{L"steam-controller.log",L"controller.txt"},
        {L"steam-controller-ui.log",L"controller_ui.txt"},{L"steam-gameoverlay-renderer.log",L"gameoverlay_renderer.txt"},
        {L"steam-gameoverlay-ui.log",L"gameoverlay_ui.txt"},
        {L"steam-gameoverlay-renderer.previous.log",L"gameoverlay_renderer.previous.txt"},
        {L"steam-gameoverlay-ui.previous.log",L"gameoverlay_ui.previous.txt"}};
    for(const auto& name:names){
        SleepRecoveryApi::Tail tail;
        if(!client.root.empty())tail=SleepRecoveryApi::readTail(client.root+L"\\logs\\"+name.second);
        else tail.meta={{"available",false},{"reason","steam-root-unavailable"}};
        tail.meta["file"]=W2U(name.first);tail.meta["sourceFile"]=W2U(name.second);
        if(flag(tail.meta,"available")){
            const auto output=folder+L"\\"+name.first;
            const bool saved=sgWriteFileAtomic(output,tail.bytes);tail.meta["saved"]=saved;if(saved)files.push_back(output);
        }
        manifest["files"].push_back(tail.meta);
    }
    manifest["steamSessionStableDuringExport"]=sameSession(client);
    const auto output=folder+L"\\steam-input-export-manifest.json";
    if(sgWriteFileAtomic(output,manifest.dump(2,' ',false,json::error_handler_t::replace)))files.push_back(output);
    return files;
}
// Reuse the existing YMCC original-PID window helpers, not its input-device
// admission/mailbox or process-thaw owner. At most one handoff in this RETURN
// transaction; all later wake polls are read-only because inputAttempted stays set.
static json returnOriginalPidWindow(const Captured& original,unsigned long long generation,const json& receipt,const Foreground& expectedForeground,const std::function<bool()>& stable) {
    json result={{"attempted",false},{"apiAccepted",false},{"scope","original-pid-window"},{"gameInputVerified",false},{"reason","steam-return-not-acknowledged"}};
    if(!flag(receipt,"mutated")||!flag(receipt,"actionAcknowledged"))return result;
    auto oldForeground=foreground(original.game,original.client);
    if(oldForeground.role=="original-game"){result["reason"]="original-window-already-current";return result;}
    if(!stable()||!allowedForeground(oldForeground)||!sameForeground(expectedForeground,original.game,original.client)){result["reason"]="original-pid-return-state-changed";return result;}
    HWND target=focusFindWindowForPid(original.game.pid);
    if(!focusIsRestorableExactWindow(target,original.game.pid)){result["reason"]="original-pid-return-no-window";return result;}
    if(!ymcc::resumeWindowResponds(target)){result["reason"]="original-pid-return-window-unresponsive";return result;}
    if(!stable()){result["reason"]="original-pid-return-state-changed-after-probe";return result;}
    if(foreground(original.game,original.client).role=="original-game"){result["reason"]="original-window-already-current";return result;}
    if(!sameForeground(oldForeground,original.game,original.client)){result["reason"]="original-pid-return-foreground-changed-during-probe";return result;}
    // Probe BEFORE this fresh Steam read: a menu/editor/overlay opened during
    // WM_NULL probing cannot borrow the earlier JS guard for a native action.
    const auto now=inspect(generation,"native-return-preflight"),input=now.value("steamInputRecovery",json::object()),steam=now.value("steam",json::object());
    const auto reason=pidWindowReturnBlock(steam,static_cast<std::uint32_t>(number(now.value("tracker",json::object()),"appid")));
    if(!reason.empty()){result["reason"]=reason;return result;}
    const auto context=steam.value("inputContext",json::object()),priorContext=receipt.value("inputContext",json::object());
    const auto rows=steam.value("overviews",json::array()),priorRows=receipt.value("after",json::object()).value("overviews",json::array());
    const bool binding=flag(input,"repairable")&&number(now.value("game",json::object()),"pid")==original.game.pid&&now.value("game",json::object()).value("created",std::string())==std::to_string(original.game.created)&&
        flag(now.value("suspendCapture",json::object()),"available")&&flag(now.value("steamSessionState",json::object()),"ready")&&
        (input.value("returnBasis",std::string())=="current-association"||input.value("returnBasis",std::string())=="captured-original-tracker"||input.value("returnBasis",std::string())=="captured-original-local-running")&&
        context.value("controllers",json())==priorContext.value("controllers",json())&&rows.is_array()&&priorRows.is_array()&&rows.size()==1&&priorRows.size()==1&&
        rows[0].value("gameidString",json())==priorRows[0].value("gameidString",json())&&rows[0].value("gameidString",json()).is_string()&&
        rows[0].value("appid",json())==priorRows[0].value("appid",json())&&rows[0].value("appType",json())==priorRows[0].value("appType",json());
    if(!binding||!stable()){result["reason"]="original-pid-return-binding-changed";return result;}
    if(foreground(original.game,original.client).role=="original-game"){result["reason"]="original-window-already-current";return result;}
    if(!sameForeground(oldForeground,original.game,original.client)||!focusIsRestorableExactWindow(target,original.game.pid)||!stable()){
        result["reason"]="original-pid-return-foreground-or-window-changed";return result;
    }
    result["attempted"]=true;result["apiAccepted"]=ymcc::requestWindowForegroundAsync(target);
    result["reason"]=flag(result,"apiAccepted")?"original-pid-window-return-requested":"original-pid-window-return-not-accepted";
    return result;
}
static json resumeInputContext(const json& inspected,unsigned long long generation,unsigned long long configEpoch,const std::function<bool()>& runStillCurrent={}) {
    const auto reject=[](const char* reason){return json{{"ok",false},{"accepted",false},{"mutated",false},{"scope","steam-input"},{"reason",reason},{"healthVerified",false}};};
    if(!sleepSteamWakeOperationAllows(generation,configEpoch))return reject("steam-input-resume-cancelled");
    if(runStillCurrent&&!runStillCurrent())return reject("steam-input-worker-expired");
    if(number(inspected,"generation")!=generation||!inspected.contains("steamInputRecovery")||(!flag(inspected["steamInputRecovery"],"repairable")&&!flag(inspected["steamInputRecovery"],"windowPreparationRepairable")))return reject("steam-input-context-not-repairable");
    const auto original=capturedFor(generation);if(!original.generation||!sameGame(original.game)||!sameSession(original.client)||!gameNotPaused(original.game))return reject("steam-original-target-changed-or-paused");
    const auto fresh=inspect(generation,"input-preflight");
    if(!fresh.contains("steamInputRecovery")||fresh["game"]!=inspected["game"]||!sameClientEvidence(fresh["steamClient"],inspected["steamClient"])||number(fresh["tracker"],"appid")!=number(inspected["tracker"],"appid"))return reject("steam-input-binding-changed");
    if(!flag(fresh["steamInputRecovery"],"repairable")&&!flag(fresh["steamInputRecovery"],"windowPreparationRepairable")){
        const auto reason=fresh["steamInputRecovery"].value("reason",std::string());
        auto result=reject(reason.empty()?"steam-input-context-not-repairable":reason.c_str());
        const auto status=fresh["steamInputRecovery"].value("status",std::string());
        if(status=="waiting-interaction"||status=="waiting-controller"){
            result["preflightDeferred"]=true;result["preflightOnly"]=true;result["stage"]="sleep-input-preflight";
        }
        return result;
    }
    const auto fg=foreground(original.game,original.client);
    const auto originalAppid=static_cast<std::uint32_t>(number(fresh["tracker"],"appid"));
    std::string nativeAdmissionReason;
    const auto admit=[generation,configEpoch,original,fg,runStillCurrent,originalAppid,&nativeAdmissionReason]{
        nativeAdmissionReason.clear();
        if(runStillCurrent&&!runStillCurrent()){nativeAdmissionReason="steam-input-worker-expired";return false;}
        if(!sleepSteamRecoveryAllows(generation,configEpoch)){nativeAdmissionReason="steam-input-recovery-cancelled";return false;}
        if(!sleepSteamWakeOperationAllows(generation,configEpoch)){nativeAdmissionReason="steam-input-wake-admission-revoked";return false;}
        if(!sameGame(original.game)){nativeAdmissionReason="steam-original-game-identity-changed";return false;}
        if(!sameSession(original.client)){nativeAdmissionReason="steam-native-session-changed";return false;}
        if(!nativePrimaryAllowsOriginal(original.client,original.game,originalAppid,flag(original.targetSelection.value("nativePrimary",json::object()),"nativePrimaryVerified"))){nativeAdmissionReason="steam-native-primary-game-changed";return false;}
        if(!gameNotPaused(original.game)){nativeAdmissionReason="steam-original-game-paused";return false;}
        if(!sameForeground(fg,original.game,original.client)){nativeAdmissionReason="steam-input-foreground-changed";return false;}
        return true;
    };
    if(!admit())return reject(nativeAdmissionReason.c_str());
    json request={{"operation","sleep.input-resume"},{"appids",json::array({number(fresh["tracker"],"appid")})}};
    const bool originalWindowReturn=flag(fresh["steamInputRecovery"],"requireOriginalWindowReturn");
    // Reuse the immutable YMCC-one-PID + Steam-tracker joint identity in BOTH paths.
    // A divergent shared sleep-lease/current candidate is NOT this Steam target.
    // This internal credential is reissued after live native prechecks; the UI
    // cache cannot authorize a different/new game or establish OS identity.
    request["originalTarget"]={{"basis",fresh["steamInputRecovery"].value("returnBasis",std::string())},{"appid",number(fresh["tracker"],"appid")},
        {"pid",original.game.pid},{"created",std::to_string(original.game.created)}};
    if(originalWindowReturn)request["originalWindowReturn"]=true;
    const bool windowPreparation=flag(fresh["steamInputRecovery"],"controllerReconnectPending");
    if(windowPreparation){
        if(fg.role!="original-game"||fg.pid!=original.game.pid||fg.created!=original.game.created)return reject("steam-pre-controller-original-window-not-current");
        request["preControllerOriginalWindow"]=true;
    }
    auto result=liveRequest(original.client,request,admit);
    // Only the native transport's explicit BEFORE-send receipt plus our own
    // foreground-only veto can release a reservation. Never infer no-send
    // from a generic mutated=false result, timeout, or a Steam JS error.
    const bool nativeForegroundCancelled=result.value("reason",std::string())=="live-action-cancelled"&&
        result.value("stage",std::string())=="native-live-pre-dispatch"&&
        result.value("requestDispatched",json())==json(false)&&result.value("mutated",json())==json(false)&&
        !flag(result,"actionAcknowledged")&&nativeAdmissionReason=="steam-input-foreground-changed";
    result["nativeAdmissionReason"]=nativeAdmissionReason.empty()?"admitted":nativeAdmissionReason;
    result["nativePreDispatchCancelled"]=nativeForegroundCancelled;
    // A cached Steam AppID can satisfy the JS focus readback before Windows has
    // actually activated the window. Wait at most 750ms (30 reads), with NO
    // action replay and immediate cancellation on identity/power/user changes.
    const auto postStable=[&]{return (!runStillCurrent||runStillCurrent())&&sleepSteamRecoveryAllows(generation,configEpoch)&&
        sleepSteamWakeOperationAllows(generation,configEpoch)&&sameGame(original.game)&&sameSession(original.client)&&gameNotPaused(original.game)&&nativePrimaryAllowsOriginal(original.client,original.game,originalAppid,flag(original.targetSelection.value("nativePrimary",json::object()),"nativePrimaryVerified"));};
    auto actualForeground=foreground(original.game,original.client);
    const auto focused=[&]{return actualForeground.role=="original-game"&&actualForeground.pid==original.game.pid&&actualForeground.created==original.game.created;};
    const bool acknowledged=flag(result,"mutated")&&flag(result,"actionAcknowledged");
    const bool preparingWindow=windowPreparation&&acknowledged&&flag(result,"windowPreparation")&&flag(result,"verificationPending")&&
        result.value("reason",std::string())=="steam-original-window-prepared-input-unverified";
    const bool uiPending=acknowledged&&flag(result,"verificationPending")&&(result.value("reason",std::string())=="steam-input-ui-focus-readback-pending"||preparingWindow);
    const auto waitStart=GetTickCount64();unsigned foregroundReads=0;
    while(((flag(result,"ok")&&flag(result,"runtimeAccepted"))||uiPending)&&!focused()&&allowedForeground(actualForeground)&&postStable()&&foregroundReads<30){
        const auto now=GetTickCount64();if(now<waitStart||now-waitStart>=750)break;
        Sleep(static_cast<DWORD>((std::min<ULONGLONG>)(25,750-(now-waitStart))));++foregroundReads;
        actualForeground=foreground(original.game,original.client);
    }
    const auto valveWaitEnd=GetTickCount64();unsigned pidForegroundReads=0;ULONGLONG pidWaitStart=valveWaitEnd;
    json pidReturn={{"attempted",false},{"apiAccepted",false},{"reason",focused()?"original-window-already-current":"steam-return-not-eligible"},{"gameInputVerified",false}};
    if(!windowPreparation&&acknowledged&&((flag(result,"ok")&&flag(result,"runtimeAccepted"))||uiPending)&&!focused()&&allowedForeground(actualForeground)&&postStable()){
        pidReturn=returnOriginalPidWindow(original,generation,result,fg,postStable);
        actualForeground=foreground(original.game,original.client);pidWaitStart=GetTickCount64();
        while(flag(pidReturn,"attempted")&&!focused()&&allowedForeground(actualForeground)&&postStable()&&pidForegroundReads<30){
            const auto now=GetTickCount64();if(now<pidWaitStart||now-pidWaitStart>=750)break;
            Sleep(static_cast<DWORD>((std::min<ULONGLONG>)(25,750-(now-pidWaitStart))));++pidForegroundReads;actualForeground=foreground(original.game,original.client);
        }
    }
    const bool stable=postStable();const bool originalWindowFocused=focused();
    result["pidWindowActivation"]=pidReturn;
    result["pidWindowActivation"]["foregroundWaitReads"]=pidForegroundReads;
    result["pidWindowActivation"]["foregroundWaitMs"]=GetTickCount64()>=pidWaitStart?GetTickCount64()-pidWaitStart:0;
    result["originalWindowReturn"]=originalWindowReturn;
    result["foregroundWaitMs"]=valveWaitEnd>=waitStart?valveWaitEnd-waitStart:0;
    result["foregroundWaitReads"]=foregroundReads;
    result["originalWindowFocused"]=originalWindowFocused;
    result["originalIdentityStable"]=stable;
    result["windowReturnObserved"]=acknowledged&&stable&&originalWindowFocused;
    result["verificationPending"]=uiPending&&stable&&originalWindowFocused;
    result["originalPid"]=original.game.pid;result["originalCreated"]=std::to_string(original.game.created);
    result["verificationBasis"]="ymcc-captured-pid-created-path";
    result["returnBasis"]=fresh["steamInputRecovery"].value("returnBasis",std::string("none"));
    // Only our known PRE-mutation guard failures can release a reservation.
    // A post-RETURN UI change (mutated=true), timeout/unknown stage or identity
    // change can NEVER authorize a second RETURN.
    const auto preflightReason=result.value("reason",std::string());
    const bool softPreflight=preflightReason=="steam-interactive-ui-active"||preflightReason=="steam-interactive-ui-state-unavailable"||
        preflightReason=="steam-input-editor-busy-or-unknown"||preflightReason=="steam-controller-not-connected";
    result["preflightDeferred"]=stable&&(nativeForegroundCancelled||(softPreflight&&result.value("stage",std::string())=="sleep-inspect"))&&
        result.value("mutated",json())==json(false)&&!flag(result,"actionAcknowledged");
    if(flag(result,"preflightDeferred"))result["preflightOnly"]=true;
    result["accepted"]=flag(result,"ok")&&flag(result,"runtimeAccepted")&&stable&&originalWindowFocused;result["scope"]="steam-input";result["healthVerified"]=false;result["gameInput"]="not-tested";
    if(!stable){result["ok"]=false;result["reason"]="steam-input-state-changed-after-request";}
    else if((flag(result,"ok")||uiPending)&&!originalWindowFocused){result["ok"]=false;result["reason"]="steam-original-window-not-confirmed";}
    sgRecordFact("sleep-steam-input-resume-result",{{"generation",generation},{"pid",original.game.pid},{"created",std::to_string(original.game.created)},
        {"accepted",result["accepted"]},{"mutated",result.value("mutated",false)},{"reason",result.value("reason",std::string())},
        {"windowReturnObserved",result["windowReturnObserved"]},{"windowPreparation",result.value("windowPreparation",false)},{"controllerReconnectPending",result.value("controllerReconnectPending",false)},{"pidWindowActivation",result["pidWindowActivation"]},{"verificationPending",result["verificationPending"]},{"uiFocusMatched",result.value("uiFocusMatched",false)},
        {"nativeReturn",result.value("nativeReturn",json::object())},{"nativeAdmissionReason",result["nativeAdmissionReason"]},{"nativePreDispatchCancelled",nativeForegroundCancelled},{"requestDispatched",result.value("requestDispatched",json())},{"preflightDeferred",result["preflightDeferred"]},{"gameInputVerified",false}});
    return result;
}
static json reconcile(const json& inspected,unsigned long long generation,unsigned long long configEpoch,const std::function<bool()>& runStillCurrent={}) {
    const auto reject=[](const char* reason){return json{{"ok",false},{"accepted",false},{"mutated",false},{"scope","steam"},{"reason",reason},{"healthVerified",false}};};
    if(!sleepSteamWakeOperationAllows(generation,configEpoch))return reject("steam-recovery-cancelled");
    if(runStillCurrent&&!runStillCurrent())return reject("steam-worker-expired");
    const bool inspectedRepairable=inspected.contains("steamRecovery")&&(flag(inspected["steamRecovery"],"repairable")||flag(inspected["steamRecovery"],"contactRepairable"));
    if(!inspectedRepairable||number(inspected,"generation")!=generation)return reject("steam-target-not-repairable");
    // Rebind from fresh local tracker + process/Steam identities before admitting ANY UI write.
    const auto fresh=inspect(generation,"reconcile-preflight");
    const bool freshRepairable=fresh.contains("steamRecovery")&&(flag(fresh["steamRecovery"],"repairable")||flag(fresh["steamRecovery"],"contactRepairable"));
    if(!freshRepairable||
        fresh["game"]!=inspected["game"]||!sameClientEvidence(fresh["steamClient"],inspected["steamClient"])||number(fresh["tracker"],"appid")!=number(inspected["tracker"],"appid"))return reject("steam-binding-changed");
    const auto client=session();Game game;game.pid=static_cast<DWORD>(number(fresh["game"],"pid"));
    const auto created=fresh["game"].value("created",std::string());std::uint64_t raw=0;
    if(!unsignedDecimal(created,raw))return reject("original-game-identity-unavailable");game.created=raw;
    ULONGLONG nowCreated=0;if(!focusQueryProcessIdentity(game.pid,&game.path,&nowCreated)||nowCreated!=game.created)return reject("original-game-identity-unavailable");
    const auto originalAppid=static_cast<std::uint32_t>(number(fresh["tracker"],"appid"));
    const bool primaryRequired=flag(fresh.value("nativePrimary",json::object()),"nativePrimaryVerified");
    const auto admit=[generation,configEpoch,client,game,runStillCurrent,originalAppid,primaryRequired]{return (!runStillCurrent||runStillCurrent())&&sleepSteamRecoveryAllows(generation,configEpoch)&&
        sleepSteamWakeOperationAllows(generation,configEpoch)&&sameSession(client)&&sameGame(game)&&nativePrimaryAllowsOriginal(client,game,originalAppid,primaryRequired);};
    if(!admit())return reject("steam-recovery-cancelled");
    const bool contactRecovery=flag(fresh["steamRecovery"],"contactRepairable")&&flag(fresh["steamRecovery"],"contactConnected");
    const auto contactProof=fresh["steamRecovery"].value("contactProof",json::object());
    if(contactRecovery&&(!contactProof.is_object()||!flag(contactProof,"originalInstanceMatched")||!flag(contactProof,"ymccOnePidVerified")||
        !flag(contactProof,"jointBindingVerified")||!flag(contactProof,"sharedCandidateMatched")||!flag(contactProof,"steamSessionMatched")))return reject("steam-contact-proof-unavailable");
    json request={{"operation","sleep.reconcile"},{"appids",json::array({number(fresh["tracker"],"appid")})}};
    if(contactRecovery)request["contactRecovery"]=true,request["contactProof"]={{"connected",true},{"appid",number(fresh["tracker"],"appid")},{"pid",number(fresh["game"],"pid")},{"created",fresh["game"].value("created",std::string())}};
    auto result=liveRequest(client,std::move(request),admit);
    result["scope"]="steam";result["healthVerified"]=false;result["gameInput"]="not-tested";
    const auto contactReceipt=result.value("contactRecovery",json::object());
    const bool contactConfirmed=!contactRecovery||(contactReceipt.is_object()&&flag(contactReceipt,"requested")&&
        flag(contactReceipt,"proofAccepted")&&flag(contactReceipt,"accepted"));
    const bool stable=admit();result["accepted"]=flag(result,"ok")&&flag(result,"runtimeAccepted")&&stable&&contactConfirmed;
    if(contactRecovery&&flag(result,"ok")&&!contactConfirmed){result["ok"]=false;result["reason"]="steam-contact-rescope-not-confirmed";}
    if(!stable){result["ok"]=false;result["reason"]="steam-recovery-state-changed-after-request";}
    sgRecordFact("sleep-steam-reconcile-result",{{"generation",generation},{"appid",number(fresh["tracker"],"appid")},
        {"accepted",result["accepted"]},{"mutated",result.value("mutated",false)},{"reason",result.value("reason",std::string())},{"scope","steam"},{"healthVerified",false}});
    return result;
}
}
