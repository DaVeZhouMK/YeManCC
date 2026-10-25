#pragma once
#include "steam_game_link_state.h"
#include "steam_game_link_wait_policy.h"
#include "steam_game_link_log_policy.h"
#include "steam_native_focus_feedback.h"
#include "steam_app_lifetime_feedback.h"
#include "steam_overlay_popup_cache_diagnostic.h"
#include "steam_overlay_provider_cache_diagnostic.h"
#include <memory>
#include <utility>
#include "steam_native_current_probe.h"
#include "steam_shortcut_metadata_baseline.h"
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
struct Control {
    std::atomic<bool> stop{false};HANDLE stopEvent=nullptr,pokeEvent=nullptr;
    std::mutex snapshotMx;json snapshot={{"schemaVersion",1},{"phase","idle"},{"linked",false},{"readOnly",true},{"repairAuthorized",false}};
    json history=json::array();
    ~Control(){if(stopEvent)CloseHandle(stopEvent);if(pokeEvent)CloseHandle(pokeEvent);}
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
struct TrackerRead {std::string gameId;Binding binding;json primary=json::object(),log=json::object();bool complete=false;};
static TrackerRead tracking(const Client& client,const Game& game) {
    TrackerRead result;
    const auto tail=SleepRecoveryApi::readTail(client.root+L"\\logs\\gameprocess_log.txt");result.log=tail.meta;
    if(!flag(tail.meta,"available")||flag(tail.meta,"truncated"))return result;
    std::vector<std::string> lines;std::string line;std::istringstream input(tail.bytes);
    while(std::getline(input,line))lines.push_back(line);
    const auto inventory=trackedProcessInventory(lines,TrackingScope::ProcessLifetime);if(!inventory.complete)return result;
    result.binding=track(lines,game.pid,W2U(game.path),SleepSteamRecovery::localSecond(game.created>20000000ULL?game.created-20000000ULL:0),
        SleepSteamRecovery::localSecond(game.created<=ULLONG_MAX-30000000ULL?game.created+30000000ULL:game.created),
        TrackingScope::ProcessLifetime);
    if(result.binding.status=="tracked"&&result.binding.line&&result.binding.line<=lines.size()){
        const auto& exactLine=lines[result.binding.line-1];const auto begin=exactLine.find("AppID ");
        if(begin!=std::string::npos){const auto end=exactLine.find(' ',begin+6);std::uint32_t canonical=0;
            if(end!=std::string::npos){const auto raw=exactLine.substr(begin+6,end-begin-6);if(appId(raw,canonical)&&canonical==result.binding.appid)result.gameId=raw;}}
    }
    result.primary=SleepSteamRecovery::nativePrimaryJoin(SleepSteamRecovery::readNativePrimary(client,sgNowFileTime()),inventory,result.binding.appid,game.pid);
    result.complete=true;return result;
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
static Client discoverClient(unsigned long long epoch) {
    auto before=SleepSteamRecovery::session();
    if(!before.pid||!before.created||before.root.empty()||g_powerLifecycle.load()!=PowerLifecycle::Ready)return {};
    const auto read=SleepSteamRecovery::liveRequest(before,{{"operation","sleep.session.inspect"},{"runningLinkRead",true}});
    if(!g_sleepRecoveryAutoEnabled.load()||g_sleepRecoveryAutoConfigEpoch.load()!=epoch||g_powerLifecycle.load()!=PowerLifecycle::Ready||
        !sameClientProcess(before)||!flag(read,"ok")||!read.contains("mutated")||!read["mutated"].is_boolean()||flag(read,"mutated"))return {};
    const auto account=number(read,"observedAccount");if(!account||account>MAXDWORD)return {};
    before.account=static_cast<DWORD>(account);before.accountPinned=true;before.liveAccountPinned=true;
    before.accountSource="running-link-independent-steam-ui-read";before.reason="running-link-account-bound";
    // This local binding never writes SleepSteamRecovery::liveAccountSession.
    return before;
}
static Game discoverGame(const Client& client,DWORD session,json& evidence) {
    Game selected;unsigned accepted=0,unresolved=0;json candidates=json::array();
    const auto tail=SleepRecoveryApi::readTail(client.root+L"\\logs\\gameprocess_log.txt");
    evidence={{"trackerLog",tail.meta},{"bindingPolicy","read-only-running-instance-join-v1"},{"repairAuthorized",false}};
    if(!flag(tail.meta,"available")||flag(tail.meta,"truncated"))return {};
    std::vector<std::string> lines;std::string line;std::istringstream input(tail.bytes);
    while(std::getline(input,line))lines.push_back(line);
    const auto inventory=trackedProcessInventory(lines);const auto primary=SleepSteamRecovery::readNativePrimary(client,sgNowFileTime());
    if(!inventory.complete||!primary.inventory.complete)return {};
    unsigned scanned=0;
    for(const auto& row:inventory.rows){
        const auto joined=SleepSteamRecovery::nativePrimaryJoin(primary,inventory,row.appid,row.pid);
        if(!flag(joined,"nativePrimaryVerified")||!flag(joined,"originalAllowed"))continue;
        if(++scanned>16){evidence["reason"]="running-link-candidate-limit";return {};}
        Game game;game.pid=row.pid;DWORD gameSession=0;
        Handle candidate(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,game.pid));
        if(candidate.value&&WaitForSingleObject(candidate.value,0)==WAIT_OBJECT_0)continue;
        if(!candidate.value||!focusQueryProcessIdentity(game.pid,&game.path,&game.created)||!game.created||
            !ProcessIdToSessionId(game.pid,&gameSession)){++unresolved;continue;}
        if(gameSession!=session)continue;
        if(pathKey(W2U(game.path))!=pathKey(row.path)||game.created<client.created){++unresolved;continue;}
        const auto ownWindow=focusFindWindowForPid(game.pid);
        if(!ownWindow||!focusIsRestorableExactWindow(ownWindow,game.pid))continue;
        const auto binding=track(lines,game.pid,W2U(game.path),SleepSteamRecovery::localSecond(game.created>20000000ULL?game.created-20000000ULL:0),
            SleepSteamRecovery::localSecond(game.created<=ULLONG_MAX-30000000ULL?game.created+30000000ULL:game.created));
        if(binding.status!="tracked"||binding.appid!=row.appid){++unresolved;continue;}
        json checked;const auto validity=validate(game,checked);
        if(candidates.size()<16)candidates.push_back({{"pid",game.pid},{"created",std::to_string(game.created)},{"appid",row.appid},{"ymccPidValidation",checked}});
        if(validity!=ValidatorStatus::Accepted){++unresolved;continue;}
        ++accepted;selected=game;
    }
    evidence["candidates"]=candidates;evidence["acceptedCandidateCount"]=accepted;evidence["unresolvedCandidateCount"]=unresolved;
    evidence["reason"]=accepted==1&&!unresolved?"running-link-exact-instance-bound":"running-link-no-unique-verified-game";
    return accepted==1&&!unresolved?selected:Game{};
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
    json logged;
    {std::lock_guard<std::mutex> lock(c->snapshotMx);
        const bool previousContactConnected=flag(c->snapshot,"contactConnected");
        const bool previousTransportConnected=flag(c->snapshot,"transportConnected");
        const bool contactChanged=previousContactConnected!=contactConnected;
        const bool transportChanged=previousTransportConnected!=transportConnected;
        if(logChange||contactChanged||transportChanged){c->history.push_back(value);if(c->history.size()>16)c->history.erase(c->history.begin());logged=value;}
        c->snapshot=std::move(value);c->snapshot["history"]=c->history;}
    if(logChange)try{appendNativeLifecycleLog("steam-game-link-state",ymcc::steamlink::logpolicy::compact(logged));}catch(...){}
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
    std::uint32_t account,std::uint32_t appid,const std::string& gameId,std::uint64_t scopeEpoch,std::uint64_t rulesEpoch){
    const auto fullId=(std::uint64_t(appid)<<32)|0x02000000ULL;
    return e.enabled&&e.transportAvailable&&e.configEpoch==scopeEpoch&&scopeEpoch&&
        !g_exitRequested.load()&&g_sleepRecoveryAutoEnabled.load()&&
        g_sleepRecoveryAutoConfigEpoch.load()==scopeEpoch&&rulesEpoch&&g_gameRulesEpoch.load()==rulesEpoch&&
        g_powerLifecycle.load()==PowerLifecycle::Ready&&
        e.game==IdentityStatus::Same&&e.steam==IdentityStatus::Same&&e.account==AccountStatus::Same&&
        e.tracker==TrackerStatus::Exact&&e.primary==PrimaryStatus::Exact&&e.validator==ValidatorStatus::Accepted&&
        e.ui==UiStatus::RunningAndListed&&privateIdentity.available&&account&&appid&&
        privateIdentity.account64&&std::uint32_t(privateIdentity.account64)==account&&privateIdentity.appid==appid&&
        privateIdentity.gameId64==fullId&&gameId==std::to_string(fullId);
}
static void run(const std::shared_ptr<Control>& c) {
    State state;Client client;Game game;Handle clientHandle,gameHandle;
    SleepSteamRecovery::CurrentRecordPre currentRecordPre;
    json currentReadMetadata;
    struct RevokedOriginal {Game game;Handle handle;};std::vector<RevokedOriginal> revokedOriginals;
    HANDLE directory=nullptr;std::wstring watchedRoot;std::string previousGameStamp,previousConsoleStamp,previousConnectionStamp,phase,reason;
    waitpolicy::Policy waitPolicy;bool forceObserve=false;
    DWORD session=0;if(!ProcessIdToSessionId(GetCurrentProcessId(),&session)){publish(c,{{"phase","unavailable"},{"reason","windows-session-unavailable"},{"linked",false},{"active",false}},true);return;}
    bool pending=true;ULONGLONG idleDue=0;std::uint64_t sequence=0,identitySequence=0,lastLifetimeEventSequence=0;std::uint32_t appid=0;std::string gameId;HWND lastWindow=nullptr;
    const auto closeDirectory=[&](){if(directory&&directory!=INVALID_HANDLE_VALUE)FindCloseChangeNotification(directory);directory=nullptr;watchedRoot.clear();waitPolicy.directoryChecked();};
    while(!c->stop.load()){try{
        currentReadMetadata=nullptr;
        const auto now=GetTickCount64(),epoch=g_sleepRecoveryAutoConfigEpoch.load();
        const bool enabled=g_sleepRecoveryAutoEnabled.load(std::memory_order_acquire);
        // Coalesce only after a directory signal. While pending, omit its signalled
        // handle from the wait set; sustained unrelated log writes cannot spin.
        if(waitPolicy.directoryReady(now)){
            waitPolicy.directoryChecked();
            if(directory){
                if(!FindNextChangeNotification(directory))closeDirectory();
                else{
                    const auto gameStamp=stamp(watchedRoot+L"\\logs\\gameprocess_log.txt");
                    const auto consoleStamp=stamp(watchedRoot+L"\\logs\\console_log.txt");
                    const auto connectionStamp=stamp(watchedRoot+L"\\logs\\connection_log.txt");
                    const bool gameChanged=gameStamp!=previousGameStamp,consoleChanged=consoleStamp!=previousConsoleStamp;
                    const bool connection=connectionStamp!=previousConnectionStamp;
                    previousGameStamp=gameStamp;previousConsoleStamp=consoleStamp;previousConnectionStamp=connectionStamp;
                    // connection_log is a wake hint only, never binding evidence.
                    if(gameChanged||consoleChanged||connection)steamOriginalRecoveryObserveHint();
                    if(gameChanged)waitPolicy.relevantChanged(now);
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
                    closeDirectory();directory=FindFirstChangeNotificationW((observed.root+L"\\logs").c_str(),FALSE,FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_SIZE);
                    if(directory==INVALID_HANDLE_VALUE)directory=nullptr;else watchedRoot=observed.root;
                    previousGameStamp=stamp(observed.root+L"\\logs\\gameprocess_log.txt");
                    previousConsoleStamp=stamp(observed.root+L"\\logs\\console_log.txt");
                    previousConnectionStamp=stamp(observed.root+L"\\logs\\connection_log.txt");
                }
                client=discoverClient(epoch);json binding;
                if(SleepSteamRecovery::validSession(client))game=discoverGame(client,session,binding);else game={};
                const bool revokedSameGame=game.pid&&std::any_of(revokedOriginals.begin(),revokedOriginals.end(),[&](const RevokedOriginal& value){return SleepSteamRecovery::sameGameIdentity(value.game,game);});
                if(revokedSameGame||revokedOriginals.size()>=16){binding["reason"]="running-link-revoked-original-not-rebound";binding["revokedOriginalBlocked"]=true;game={};}
                if(game.pid){
                    clientHandle=pin(client.pid,client.created,client.root+L"\\steam.exe",session);gameHandle=pin(game.pid,game.created,game.path,session);
                    const auto tracker=tracking(client,game);
                    steamOriginalRecoveryTrackerObserved(game.pid,game.created,client.pid,client.created,
                        tracker.gameId,tracker.binding.appid,tracker.complete&&tracker.binding.status=="tracked");
                    if(clientHandle.value&&gameHandle.value&&tracker.complete&&tracker.binding.status=="tracked"&&flag(tracker.primary,"nativePrimaryVerified")&&
                        !tracker.gameId.empty()&&flag(tracker.primary,"originalAllowed")&&sameClientProcess(client)&&g_sleepRecoveryAutoEnabled.load()&&g_sleepRecoveryAutoConfigEpoch.load()==epoch&&
                        g_powerLifecycle.load()==PowerLifecycle::Ready){
                        appid=tracker.binding.appid;gameId=tracker.gameId;sequence=0;identitySequence=0;lastLifetimeEventSequence=0;lastWindow=nullptr;const bool begun=state.begin(nextSerial.fetch_add(1),epoch,GetTickCount64());
                        if(begun){publish(c,{{"phase","binding"},{"linked",false},{"active",true},{"binding",binding}},true);phase="binding";}
                        else{clientHandle={};gameHandle={};game={};}
                    }else{clientHandle={};gameHandle={};game={};}
                }
                if(!state.active()){publish(c,{{"phase","idle"},{"reason","waiting-unique-original-instance"},{"linked",false},{"active",false},{"binding",binding}},phase!="idle");phase="idle";reason="waiting-unique-original-instance";}
            }
        }
        if(state.active()&&(forceObserve||waitPolicy.readReady(now)||now>=state.snapshot().dueTick||!enabled||state.snapshot().configEpoch!=epoch||
            WaitForSingleObject(gameHandle.value,0)==WAIT_OBJECT_0||WaitForSingleObject(clientHandle.value,0)==WAIT_OBJECT_0)){
            forceObserve=false;waitPolicy.observed(now);
            Evidence e;e.sequence=++sequence;e.tick=GetTickCount64();e.enabled=enabled;e.configEpoch=epoch;
            e.game=identity(gameHandle.value,game.pid,game.created,game.path,session);e.steam=identity(clientHandle.value,client.pid,client.created,client.root+L"\\steam.exe",session);
            json validation,shared,detail=base();e.sharedCandidate=sharedCandidate(game,shared);
            detail["originalGame"]={{"pid",game.pid},{"created",std::to_string(game.created)},{"path",W2U(game.path)},{"windowsSession",session}};
            detail["steamClient"]=SleepSteamRecovery::clientIdentity(client);detail["appid"]=appid;detail["trackerGameIdExactString"]=gameId;detail["sharedCandidate"]=shared;
            const auto ownWindow=focusFindWindowForPid(game.pid);const bool windowMatched=ownWindow&&focusIsRestorableExactWindow(ownWindow,game.pid);
            detail["osWindow"]={{"source","same-original-pid-window"},{"available",windowMatched},{"hwnd",windowMatched?std::to_string(reinterpret_cast<std::uintptr_t>(ownWindow)):std::string()},{"SteamWindowIdMapping","unknown"}};
            const auto foreground=SleepSteamRecovery::foreground(game,client);detail["osForeground"]={{"role",foreground.role},{"pid",foreground.pid},{"created",std::to_string(foreground.created)},{"readOnly",true}};
            bool lifetimeChanged=false;ymcc::steamrecordidentity::Read privateRecordIdentity;
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
                const auto tracker=tracking(client,game);
                // Revocation hint only; the Running owner still qualifies its
                // own unique PID and full ID before any native transaction.
                steamOriginalRecoveryTrackerObserved(game.pid,game.created,client.pid,client.created,
                    tracker.gameId,tracker.binding.appid,tracker.complete&&tracker.binding.status=="tracked");
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
                const auto original=SleepSteamRecovery::capturedFor(nativeRecordReadGeneration);
                if(original.generation){
                    // Preserve the old captured path, including bound=false refusal.
                    currentRecordPre={};
                    if(original.game.pid==game.pid&&original.game.created==game.created)
                        detail["nativeRecordDiagnostic"]=SleepSteamRecovery::prefetchNativeRecordIdentity(client,game,appid,privateRecordIdentity,nativeRecordReadGeneration,e.configEpoch,gameId,detail.value("steamUI",json::object()));
                }else{
                    auto post=SleepSteamRecovery::currentRecordProof(client,game,appid,privateRecordIdentity,detail.value("steamUI",json::object()),
                        nativeRecordReadGeneration,e.configEpoch,currentRecordRulesEpoch,state.snapshot().serial,uiIdentitySequence,session,gameId,
                        uiStartedTick,uiFinishedTick,uiStartedFileTime,uiFinishedFileTime);
                    json diagnostic;
                    if(SleepSteamRecovery::commitCurrentRecordAfterExistingUi(currentRecordRead,post,detail.value("steamUI",json::object()),currentProbeActive,diagnostic))
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
            // Includes the optional additional identity POST, when one was made.
            const bool originalIdentityBracketMatched=
                e.game==IdentityStatus::Same&&e.steam==IdentityStatus::Same&&e.account==AccountStatus::Same&&
                e.enabled&&e.configEpoch==state.snapshot().configEpoch;
            if(originalRecoveryIdentityReady(e,privateRecordIdentity,client.account,appid,gameId,
                    state.snapshot().configEpoch,currentRecordRulesEpoch)){
                steamOriginalRecoveryIdentityReady(game.pid,game.created,client.pid,client.created,gameId,appid,
                    e.configEpoch,currentRecordRulesEpoch);
                detail["originalRecoveryQualificationReadyHintOffered"]=true;
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
            e.tick=GetTickCount64();state.observe(e);const auto s=state.snapshot();
            detail["phase"]=s.phase;detail["reason"]=s.reason;detail["linked"]=s.linked;detail["contactConnected"]=s.contactConnected;detail["transportConnected"]=s.transportConnected;detail["active"]=s.active;detail["terminal"]=s.terminal;
            detail["linkSerial"]=std::to_string(s.serial);detail["configEpoch"]=std::to_string(s.configEpoch);detail["observationSequence"]=std::to_string(s.lastSequence);
            detail["dueTick"]=s.dueTick;detail["readFailures"]=s.readFailures;
            detail["verifiedReadAvailable"]=e.transportAvailable;detail["gameIdentitySame"]=e.game==IdentityStatus::Same;detail["steamIdentitySame"]=e.steam==IdentityStatus::Same;
            detail["accountMatched"]=e.account==AccountStatus::Same;detail["trackerExact"]=e.tracker==TrackerStatus::Exact;detail["nativePrimaryExact"]=e.primary==PrimaryStatus::Exact;
            detail["ymccSinglePidAccepted"]=e.validator==ValidatorStatus::Accepted;detail["nativeOverviewAssociated"]=e.ui==UiStatus::RunningAndListed;
            publish(c,std::move(detail),phase!=s.phase||reason!=s.reason||lifetimeChanged||(windowMatched&&ownWindow!=lastWindow));phase=s.phase;reason=s.reason;if(windowMatched)lastWindow=ownWindow;
            if(!s.active){currentRecordPre={};if(s.phase=="revoked"&&gameHandle.value&&revokedOriginals.size()<16)revokedOriginals.push_back({game,std::move(gameHandle)});
                gameHandle={};clientHandle={};game={};pending=false;}
        }
        HANDLE waits[5]{c->stopEvent,c->pokeEvent};DWORD count=2,dirIndex=MAXDWORD,gameIndex=MAXDWORD,steamIndex=MAXDWORD;
        if(directory&&!waitPolicy.directoryPending()){dirIndex=count;waits[count++]=directory;}
        if(state.active()){gameIndex=count;waits[count++]=gameHandle.value;steamIndex=count;waits[count++]=clientHandle.value;}
        else if(!revokedOriginals.empty()){gameIndex=count;waits[count++]=revokedOriginals.front().handle.value;}
        const auto nowWait=GetTickCount64();const auto due=state.active()?state.snapshot().dueTick:pending?idleDue:ULLONG_MAX;
        const DWORD timeout=waitpolicy::timeout(nowWait,waitPolicy.nextDue(due),state.active()||pending||waitPolicy.scheduled());
        const auto waited=WaitForMultipleObjects(count,waits,FALSE,timeout);
        if(waited==WAIT_OBJECT_0||waited==WAIT_FAILED)break;
        if(waited==WAIT_OBJECT_0+1){
            steamOriginalRecoveryObserveHint();forceObserve=true;
            if(!state.active()){pending=true;idleDue=GetTickCount64();}
        }else if((gameIndex!=MAXDWORD&&waited==WAIT_OBJECT_0+gameIndex)||
                 (steamIndex!=MAXDWORD&&waited==WAIT_OBJECT_0+steamIndex)){
            steamOriginalRecoveryObserveHint();forceObserve=true;
            if(!state.active()){pending=true;idleDue=GetTickCount64();}
        }else if(dirIndex!=MAXDWORD&&waited==WAIT_OBJECT_0+dirIndex){
            waitPolicy.directoryNotified(GetTickCount64());
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
    std::lock_guard<std::mutex> lock(c->snapshotMx);auto result=c->snapshot;const bool contactConnected=flag(result,"contactConnected");const bool transportConnected=flag(result,"transportConnected");result.update(base());result["contactConnected"]=contactConnected;result["transportConnected"]=transportConnected;if(!result.contains("active"))result["active"]=false;const auto now=GetTickCount64();
    const auto observed=number(result,"observedAtTick");const auto due=number(result,"dueTick");
    const bool observedKnown=result.contains("observedAtTick")&&result["observedAtTick"].is_number_unsigned();
    result["snapshotAtTick"]=now;result["fresh"]=observedKnown&&now>=observed&&(!due||due>ULLONG_MAX-5000||now<=due+5000);
    if(!flag(result,"fresh")){result["contactConnected"]=false;result["transportConnected"]=false;if(flag(result,"linked")){result["linked"]=false;result["phase"]="stale";result["reason"]="current-link-read-not-fresh";}}
    if(!g_sleepRecoveryAutoEnabled.load()){result["phase"]="off";result["reason"]="user-disabled";result["linked"]=false;result["contactConnected"]=false;result["transportConnected"]=false;result["active"]=false;}
    else if(result.contains("configEpoch")&&result["configEpoch"].is_string()&&result["configEpoch"].get<std::string>()!=std::to_string(g_sleepRecoveryAutoConfigEpoch.load())){result["phase"]="cancelled";result["reason"]="config-epoch-changed";result["linked"]=false;result["contactConnected"]=false;result["transportConnected"]=false;result["active"]=false;}
    return result;
}
}
static void steamGameLinkStart(){
    std::lock_guard<std::mutex> lock(SteamGameLink::lifecycleMx);
    if(SteamGameLink::worker.joinable()||g_aiFanMockSession.isolated||g_exitRequested.load())return;
    auto c=std::make_shared<SteamGameLink::Control>();c->stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);c->pokeEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!c->stopEvent||!c->pokeEvent)return;
    try{SteamGameLink::control=c;SteamGameLink::worker=std::thread([c]{SteamGameLink::run(c);});}catch(...){SteamGameLink::control.reset();}
}
static void steamGameLinkNotify(){
    std::lock_guard<std::mutex> lock(SteamGameLink::lifecycleMx);if(SteamGameLink::control&&SteamGameLink::control->pokeEvent)SetEvent(SteamGameLink::control->pokeEvent);
}
static void steamGameLinkShutdown(){
    std::lock_guard<std::mutex> lock(SteamGameLink::lifecycleMx);if(SteamGameLink::control){SteamGameLink::control->stop.store(true);SetEvent(SteamGameLink::control->stopEvent);}
    (void)joinThreadBoundedForExit(SteamGameLink::worker,1000,"steam-game-link");
}
static json steamGameLinkGet(){return SteamGameLink::get();}
static std::wstring steamGameLinkExportSnapshot(){
    const auto file=yemancc_data_dir()+L"\\steam-game-link-current.json";
    try{std::ofstream out(fspath::path(file),std::ios::binary|std::ios::trunc);out<<steamGameLinkGet().dump(2)<<'\n';out.close();if(out)return file;}catch(...){}
    return {};
}


