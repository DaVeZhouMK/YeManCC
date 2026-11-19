#pragma once
#include "steam_menu_game_identity.h"
#include "steam_game_tracker_observation.h"
#include "steam_game_host_observation.h"
#include "steam_menu_popup_continuation.h"
#include <array>

// PRIVATE ACTION INTEGRATION. The stable public game_steam_context adapter
// precedes this include. Historical association never grants action authority.
// No private tracker parser/cache, demand allocator, process reader or worker.
namespace SteamMenuGame {
using namespace ymcc::menugame;
namespace facts = ymcc::steamgamefacts;

// SOURCE-ONLY CONSUMER DRAFT. Its public projection dependency is not yet
// SOURCE_READY. No action may be derived from these historical diagnostics.
enum class HostRowObservationKind { ShapeRejected, RegistrationFiltered, Inspected };
struct HostRowObservation {
    HostRowObservationKind kind = HostRowObservationKind::ShapeRejected;
    std::optional<DWORD> requestedHostPid, browserId, reportedAppid;
    bool acceptedByOriginal = false;
    facts::HostInspectionObservation inspection;
};
struct HostSelectionObservation {
    bool uiReadAttempted = false, uiReadReturned = false, uiReadQualified = false;
    std::optional<std::size_t> uiRowCount;
    std::array<HostRowObservation,16> rows{};
    std::size_t used = 0;
    template<class T> static json scalar(const std::optional<T>& value) {
        return value ? json(*value) : json(nullptr);
    }
    static json decimal(const std::optional<std::uint64_t>& value) {
        return value ? json(std::to_string(*value)) : json(nullptr);
    }
    static json process(const facts::HostProcessObservation& value) {
        return {{"pid",scalar(value.pid)},{"created",decimal(value.created)}};
    }
    json projection() const {
        json output={{"schema",1},{"authority","diagnostic-only"},
            {"uiReadAttempted",uiReadAttempted},{"uiReadReturned",uiReadReturned},
            {"uiReadQualified",uiReadQualified},{"uiRowCount",scalar(uiRowCount)},
            {"capacity",rows.size()},{"rows",json::array()}};
        for(std::size_t index=0;index<used;++index){
            const auto& row=rows[index];
            const char* kind=row.kind==HostRowObservationKind::Inspected?"host-inspected":
                row.kind==HostRowObservationKind::RegistrationFiltered?"ui-registration-filtered":"ui-row-shape-rejected";
            json item={{"index",index},{"kind",kind},{"requestedHostPid",scalar(row.requestedHostPid)},
                {"browserId",scalar(row.browserId)},{"reportedAppid",scalar(row.reportedAppid)},
                {"acceptedByOriginal",row.acceptedByOriginal}};
            if(row.kind==HostRowObservationKind::Inspected){
                const auto& host=row.inspection;
                json registration;
                if(host.parsedRegistration)registration={{"gamePid",host.parsedRegistration->gamePid},
                    {"steamPid",host.parsedRegistration->steamPid},{"gameid64",std::to_string(host.parsedRegistration->gameId)}};
                item["inspection"]={{"stage",facts::hostInspectionStageName(host.stage)},
                    {"rejectionCode",static_cast<unsigned>(host.rejection)},{"factCode",static_cast<unsigned>(host.fact)},
                    {"actualGame",process(host.actualGame)},{"selectedGame",process(host.selectedGame)},
                    {"steam",process(host.steam)},{"hostWitness",host.hostWitness?process(*host.hostWitness):json(nullptr)},
                    {"hostImageObserved",scalar(host.hostImageObserved)},{"parentPid",scalar(host.parentPid)},
                    {"windowsSession",scalar(host.windowsSession)},{"commandLineAvailable",scalar(host.commandLineAvailable)},
                    {"parsedRegistration",std::move(registration)},{"trackerAppid",scalar(host.trackerAppId)},
                    {"trackerGameid64",decimal(host.trackerFullGameId)},{"reportedGameid64",decimal(host.reportedGameId)},
                    {"registeredGamePidMatchesActual",scalar(host.registeredGamePidMatchesActual)},
                    {"registeredSteamPidMatchesSteam",scalar(host.registeredSteamPidMatchesSteam)},
                    {"registeredGameIdMatchesReported",scalar(host.registeredGameIdMatchesReported)},
                    {"hostLeasePresentAtReturn",host.hostLeasePresentAtReturn},
                    {"checks",{{"identity",facts::hostInspectionCheckName(host.identityCheck)},
                        {"birth",facts::hostInspectionCheckName(host.birthCheck)},
                        {"image",facts::hostInspectionCheckName(host.imageCheck)},
                        {"parent",facts::hostInspectionCheckName(host.parentCheck)},
                        {"session",facts::hostInspectionCheckName(host.sessionCheck)},
                        {"registration",facts::hostInspectionCheckName(host.registrationCheck)}}}};
            }
            output["rows"].push_back(std::move(item));
        }
        return output;
    }
};
static std::atomic<bool> enabled{true};
static std::atomic<unsigned long long> configEpoch{1};
static void configure(const json& document) {
    const auto ui=document.find("ui");
    const bool wanted=ui==document.end()||!ui->is_object()||!ui->contains("steamMenuGameIdRepair")||(*ui)["steamMenuGameIdRepair"]==true;
    if(enabled.exchange(wanted)!=wanted){
        const auto epoch=++configEpoch;
        // Feature OFF affects only this feature's request/action ownership.
        // It never clears the shared Steam fact cache or another subscriber.
        if(!wanted)gamepadSerialSubmit([epoch]{
            const auto allowed=[&]{return !enabled.load()&&configEpoch.load()==epoch;};
            if(allowed())steamLiveRequest(0,{{"operation","menu.qam.dispose"},{"menuGameDisposeOnly",true}},allowed);
        });
    }
}
static DWORD foregroundPid() noexcept {DWORD pid=0;const auto window=GetForegroundWindow();if(window)GetWindowThreadProcessId(window,&pid);return pid;}
static bool registeredPair(std::uint32_t app,std::uint64_t full,std::uint32_t reportedApp,std::uint64_t reported) noexcept {
    if(!exactGameId(app,full)||!reportedApp||!reported)return false;
    if(reportedApp==app&&reported==full)return true;
    if((full>>32)!=app)return false;
    return (reported==app&&(reportedApp==app||reportedApp==(app&0xffffff)))||
        (reportedApp<=0xffffff&&reported==reportedApp);
}
static json run(DWORD account,json request,const std::function<bool()>& allowed,
    ULONGLONG coldDeadline,unsigned long long requestRevision) {
    const auto original=[&]{return steamLiveRequest(account,request,allowed);};
    if(!enabled.load()||g_aiFanMockSession.isolated||!allowed||!allowed())return original();
    const auto foreground=foregroundPid();
    if(!foreground)return original();
    NativeDetectedGame selected;
    std::vector<std::wstring> excludes,whitelist,configured;
    unsigned long long rules=0;
    try {
        std::lock_guard lock(g_gameRulesMx);
        nativeRefreshGameRulesEpochFromDiskLocked();
        sgReadRecognitionGameRules(excludes,whitelist);
        configured=nativeConfiguredGameExes();
        rules=g_gameRulesEpoch.load(std::memory_order_acquire);
    }catch(...){return original();}
    // Preserve the unique game layer's explicit foreground qualification.
    // Foreground B never borrows global A's selection generation or identity.
    if(nativeScanGameOnePid(foreground,L"",excludes,whitelist,configured,&selected)!=NativeOnePidResult::Ok||
       foregroundPid()!=foreground)return original();
    ymcc::sleeppower::SharedPowerV1 powerScope;
    if(!captureSharedPowerLifecycle(powerScope))return original();
    const auto power=powerScope.generation;
    const auto scope=nativeGameFactScope(selected,rules,power);
    if(!scope)return original();
    const Process game{selected.pid,selected.processCreated,selected.path};
    const auto identity=g_steamSettingsObserver.identitySnapshot();
    if(!identity||identity->presence!=1||!identity->session.pid||!identity->session.created||identity->session.root.empty()){
        g_steamSettingsObserver.requestIdentityRefresh();return original();
    }
    const DWORD active=identity->currentRegistryAccountKnown()?identity->session.account:0;
    if(active&&account&&active!=account)return original();
    const auto epoch=configEpoch.load();
    struct RequestScope {
        const NativeGameFactScope game;
        const std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> steam;
        const unsigned long long config,request;
        RequestScope(const NativeGameFactScope& selected,
            std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> identity,
            unsigned long long revision,unsigned long long originalRequest)
            :game(selected),steam(std::move(identity)),config(revision),request(originalRequest){}
    };
    const auto owned=std::make_shared<const RequestScope>(*scope,identity,epoch,requestRevision);
    // Own the values even though the public API only borrows this pure guard
    // synchronously. Actual allowed/foreground stay on this action stack.
    const auto pureCurrent=[owned,powerScope]{
        return enabled.load()&&configEpoch.load()==owned->config&&nativeGameFactScopeCurrent(owned->game)&&
            sharedPowerLifecycleCurrent(powerScope)&&
            g_steamSettingsObserver.sameIdentityEpoch(owned->steam);
    };
    facts::TrackerRequestContext context;
    context.configRevision=owned->config;context.requestRevision=owned->request;
    const auto association=GameSteamContext::observeForConsumer(owned->game,std::move(context),coldDeadline,pureCurrent);
    json qualifiedMenuRequest; // Unknown until this action has one qualified host/browser.
    HostSelectionObservation hostSelection; // Fixed-size values; no reads or leases.
    const auto finish=[&](json result){
        if(result.is_object()){
            const char* kind="Unavailable";
            switch(association.kind){
            case GameSteamContext::ConsumerStatus::CachedAssociation:kind="CachedAssociation";break;
            case GameSteamContext::ConsumerStatus::ObservedAssociation:kind="ObservedAssociation";break;
            case GameSteamContext::ConsumerStatus::ObservedNegative:kind="ObservedNegative";break;
            case GameSteamContext::ConsumerStatus::Timeout:kind="Timeout";break;
            case GameSteamContext::ConsumerStatus::Cancelled:kind="Cancelled";break;
            default:break;
            }
            // Bounded provenance for this original click. Never export raw
            // source bytes or mistake cached source work for this request.
            result["menuGameAssociation"]={{"kind",kind},{"reason",association.reason?association.reason:"association-unavailable"},
                {"requestId",std::to_string(association.requestReceipt.demandId)},
                {"flightSequence",std::to_string(association.requestReceipt.flightSequence)},
                {"readsStarted",association.requestReceipt.readsStarted},{"gamePid",game.pid},
                {"gameCreated",std::to_string(game.created)},{"originalRequestRevision",std::to_string(requestRevision)}};
        }
        return result;
    };
    const auto finishWithContext=[&](json result){
        result=finish(std::move(result));
        try {
            result["menuGameAction"]={{"schema",1},{"identityState",qualifiedMenuRequest.is_object()?"qualified-request":"unknown"},
                {"qualifiedRequest",qualifiedMenuRequest},{"gameScopePowerGeneration",std::to_string(power)},
                {"powerSource","native-game-scope-at-run"},{"rulesEpoch",std::to_string(rules)},
                {"configEpoch",std::to_string(epoch)},{"originalRequestRevision",std::to_string(requestRevision)},
                {"writerProcessCreated100ns",nullptr}};
            if(hostSelection.uiReadAttempted)
                result["menuGameAction"]["hostSelection"]=hostSelection.projection();
        }catch(...){} // Diagnostic failure never changes the original action result.
        return result;
    };
    const auto refused=[&](const char* reason){return finishWithContext(json{{"ok",false},{"mutated",false},
        {"requestDispatched",false},{"reason",reason}});};
    const auto cancelled=[&]{return refused("steam-menu-game-context-changed");};
    if(!pureCurrent()||!allowed()||foregroundPid()!=game.pid)return cancelled();
    using Kind=GameSteamContext::ConsumerStatus;
    if(association.kind==Kind::Unavailable||association.kind==Kind::Timeout||association.kind==Kind::Cancelled)
        return refused(association.reason?association.reason:"steam-menu-game-association-unavailable");
    const auto& key=association.key;
    if(!key||facts::trackerSteamReceipt(identity)!=key->facts().steam||
       key->facts().selectedGame.pid!=game.pid||key->facts().selectedGame.birth!=game.created||
       key->facts().rulesEpoch!=rules||key->facts().powerGeneration!=power)
        return cancelled();
    const bool negative=association.kind==Kind::ObservedNegative;
    const auto trackedGame=association.association;
    if(negative){
        const auto& observed=association.observation;
        if(trackedGame||!observed||!(observed->key.observation==key->facts())||
           !observed->observation.readAttempted||!observed->provenance.sourceReadThisFlight||
           association.requestReceipt.phase!=facts::TrackerFlightPhase::Failed||
           association.requestReceipt.flightSequence!=observed->provenance.flight.flightSequence)
            return refused("steam-menu-game-negative-source-unconfirmed");
    }else if((association.kind!=Kind::CachedAssociation&&association.kind!=Kind::ObservedAssociation)||
       !trackedGame||!trackedGame->hasHistoricalBinding()||!trackedGame->exactPath||
       trackedGame->scope!=facts::TrackerObservationPolicy::ProcessOnlyObservation||
       !trackedGame->accountUnknown||trackedGame->processValidated||trackedGame->provenance.currentSource||
       !(trackedGame->key.observation==key->facts())||!exactGameId(trackedGame->trackerAppId,trackedGame->fullGameId))
        return refused("steam-menu-game-association-unconfirmed");
    auto process=facts::processOwner().acquire(g_steamSettingsObserver,identity,game);
    if(!process)return refused("steam-menu-game-action-process-unavailable");
    const auto current=[&]{
        return pureCurrent()&&allowed()&&foregroundPid()==game.pid&&process->current();
    };
    if(!current())return cancelled();
    hostSelection.uiReadAttempted=true;
    const auto read=steamLiveRequest(account,{{"operation","menu.inspect"},{"menu","quick-access"},{"menuGameRead",true}},current);
    hostSelection.uiReadReturned=true;
    if(read.contains("menuGameBrowsers")&&read["menuGameBrowsers"].is_array())
        hostSelection.uiRowCount=read["menuGameBrowsers"].size();
    if(!current())return cancelled();
    const auto observedAccount=read.value("observedAccount",DWORD{0});
    if(!read.value("ok",false)||read.value("mutated",true)||!observedAccount||
       (account&&observedAccount!=account)||(active&&observedAccount!=active)||
       !read.contains("menuGameBrowsers")||!read["menuGameBrowsers"].is_array()||read["menuGameBrowsers"].size()>16)return refused("steam-menu-game-ui-unavailable");
    // Fresh UI account stays independent from the candidate registry account
    // and the ProcessOnly historical association. No invented document proof.
    hostSelection.uiReadQualified=true;
    account=observedAccount;
    // The original user action, a held current game/Steam pair and fresh UI
    // facts authorize this fallback; negative metadata alone never does.
    const auto fallback=[&]{if(!current())return cancelled();return finishWithContext(steamLiveRequest(account,request,current));};
    if(negative)return fallback();
    const facts::TrackerIdentity tracker{trackedGame->trackerAppId,trackedGame->fullGameId};
    std::optional<facts::HostInspection> host;json chosen;unsigned matches=0;
    for(const auto& row:read["menuGameBrowsers"]){
        auto& hostObservation=hostSelection.rows[hostSelection.used++];
        if(!row.is_object()||row.value("browserType",-1)!=0||row.value("uiMode",-1)!=4||
           !row.contains("hostPid")||!row["hostPid"].is_number_unsigned()||!row.contains("browserId")||!row["browserId"].is_number_unsigned()||
           !row.contains("appid")||!row["appid"].is_number_unsigned()||!row.contains("gameid64")||!row["gameid64"].is_string())continue;
        const auto pid=row["hostPid"].get<unsigned long long>(),browser=row["browserId"].get<unsigned long long>(),app=row["appid"].get<unsigned long long>();
        hostObservation.kind=HostRowObservationKind::RegistrationFiltered;
        if(pid&&pid<=MAXDWORD)hostObservation.requestedHostPid=static_cast<DWORD>(pid);
        if(browser&&browser<=MAXDWORD)hostObservation.browserId=static_cast<DWORD>(browser);
        if(app&&app<=MAXDWORD)hostObservation.reportedAppid=static_cast<DWORD>(app);
        std::uint64_t id=0;if(!pid||pid>MAXDWORD||!browser||browser>MAXDWORD||!app||app>MAXDWORD||
           !ymcc::sleepsteam::unsignedDecimal(row["gameid64"].get<std::string>(),id)||
           !registeredPair(tracker.appid,tracker.fullGameId,static_cast<std::uint32_t>(app),id))continue;
        auto candidate=process->inspectHost(static_cast<DWORD>(pid),tracker,
            facts::ReportedRegistration{static_cast<std::uint32_t>(app),id});
        hostObservation.kind=HostRowObservationKind::Inspected;
        hostObservation.inspection=facts::projectHostInspection(candidate,*process,static_cast<DWORD>(pid));
        if(candidate.rejection==facts::HostRejection::None&&candidate.fact!=facts::RegistrationFact::Unknown&&
           candidate.hostLease&&candidate.hostLease->current()){
            hostObservation.acceptedByOriginal=true;
            ++matches;host=std::move(candidate);chosen=row;
        }
    }
    if(!current())return cancelled();
    if(matches!=1||!host)return fallback();
    const auto& steam=process->steamWitness();
    const auto& heldHost=host->hostLease->hostWitness();
    request["menuGame"]={{"version",1},{"appid",tracker.appid},{"gameid64",std::to_string(tracker.fullGameId)},
        {"account",account},{"gamePid",game.pid},{"gameCreated",std::to_string(game.created)},{"steamPid",steam.pid},
        {"steamCreated",std::to_string(steam.created)},{"hostPid",heldHost.pid},{"hostCreated",std::to_string(heldHost.created)},
        {"browserId",chosen["browserId"]},{"registeredAppid",chosen["appid"]},{"registeredGameid64",chosen["gameid64"]}};
    // Copy only already-qualified scalar identity, never raw source bytes,
    // paths, a fresh process sample or a new Steam query.
    try {
        json projection=json::object();
        for(const auto* key:{"appid","gameid64","gamePid","gameCreated","steamPid","steamCreated", "hostPid","hostCreated","browserId","registeredAppid","registeredGameid64"})
            projection[key]=request["menuGame"][key];
        qualifiedMenuRequest=std::move(projection);
    }catch(...){} // Leave unknown if diagnostic allocation fails.
    const auto admitted=[&]{return current()&&host->hostLease->current();};
    if(!admitted())return cancelled();
    // The existing common transport retains dispatch uncertainty/no replay.
    const auto actionStarted=GetTickCount64();
    auto result=steamLiveRequest(account,request,admitted,admitted);
    return finishWithContext(ymcc::menupopup::continueAfterButton(request,std::move(result),actionStarted,admitted,
        [&](const json& continuation,const std::function<bool()>& current){return steamLiveRequest(account,continuation,current,current);},
        []{return GetTickCount64();}));
}
}
static ULONGLONG nativeSteamMenuColdDeadline(ULONGLONG started) noexcept {
    constexpr auto cap=GameSteamContext::consumerWaitCapMs;
    return started<=(std::numeric_limits<ULONGLONG>::max)()-cap?started+cap:0;
}
static json nativeSteamMenuGameRequest(DWORD account,json request,const std::function<bool()>& allowed,
    ULONGLONG coldDeadline,unsigned long long requestRevision){
    return SteamMenuGame::run(account,std::move(request),allowed,coldDeadline,requestRevision);
}
static void steamMenuGameConfigure(const json& document){SteamMenuGame::configure(document);}
static void steamMenuGameShutdown() noexcept {
    try {
        SteamMenuGame::enabled.store(false);++SteamMenuGame::configEpoch;
        steamLiveRequest(0,{{"operation","menu.qam.dispose"},{"menuGameDisposeOnly",true}});
    }catch(...){}
}
