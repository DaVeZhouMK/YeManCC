#pragma once
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "steam_game_tracker_facts.h"
#include "game_steam_group_view.h"

// PRIVATE CONSUMER CANDIDATE. Include after the existing game layer, shared
// Steam tracker wrappers and GameLink observation queue. No fact owner/cache,
// detector, private reader, thread, timer, Steam command or action permission.
// Supplied by the existing main mailbox integration. Never execute dispatch inline.
static bool postSteamTrackerCompletionDispatch(std::function<void()> dispatch);
namespace GameSteamContext {
using namespace ymcc::steamgamefacts;
constexpr unsigned long long maxWireGeneration = 9007199254740991ULL;

static std::optional<unsigned long long> positiveNumber(const json& value,unsigned long long maximum) {
    if(!value.is_number_integer())return {};
    if(value.is_number_unsigned()){
        const auto number=value.get<unsigned long long>();
        return number&&number<=maximum?std::optional(number):std::nullopt;
    }
    const auto number=value.get<long long>();
    return number>0&&static_cast<unsigned long long>(number)<=maximum?std::optional(static_cast<unsigned long long>(number)):std::nullopt;
}
static std::optional<unsigned long long> positiveDecimal(const json& value) {
    if(!value.is_string())return {};
    const auto& text=value.get_ref<const std::string&>();
    if(text.empty()||text.size()>20||text.front()=='0')return {};
    for(const char c:text)if(c<'0'||c>'9')return {};
    unsigned long long result=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!result)return {};
    return result;
}
struct Expected {DWORD pid=0;unsigned long long birth=0,selection=0;std::wstring path;};
static std::optional<Expected> expected(const json& args) {
    if(!args.is_object()||args.size()!=1||!args.contains("expectedGame"))return {};
    const auto& game=args["expectedGame"];
    if(!game.is_object()||game.size()!=4||!game.contains("pid")||!game.contains("processCreated")||
        !game.contains("path")||!game.contains("identityGeneration")||!game["path"].is_string())return {};
    const auto pid=positiveNumber(game["pid"],MAXDWORD),birth=positiveDecimal(game["processCreated"]),
        selection=positiveNumber(game["identityGeneration"],maxWireGeneration);
    const auto& path=game["path"].get_ref<const std::string&>();
    if(!pid||!birth||!selection||path.empty()||path.size()>131072||path.find('\0')!=std::string::npos)return {};
    auto wide=U2W(path);if(wide.empty()||wide.size()>32768||wide.find(L'\0')!=std::wstring::npos)return {};
    return Expected{static_cast<DWORD>(*pid),*birth,*selection,std::move(wide)};
}
static json unknownSteam(const char* reason,const char* status="unknown") {
    return {{"status",status},{"reason",reason},{"processValidated",false},{"currentSource",false}};
}
static json response(const NativeGameSelectionReceipt* game,const char* reason) {
    json result={{"schemaVersion",1},{"game",nullptr},{"steam",unknownSteam(reason)}};
    if(game){
        result["game"]={{"pid",game->target.game.pid},{"processCreated",std::to_string(game->target.game.processCreated)},
            {"path",W2U(game->target.game.path)},{"identityGeneration",game->target.identityGeneration},
            {"rulesEpoch",std::to_string(game->target.rulesEpoch)},{"powerGeneration",std::to_string(game->powerGeneration)},
            {"processValidated",false}};
    }
    return result;
}
struct Capture {
    NativeGameSelectionReceipt game;
    std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> steam;
    TrackerObservationKey key;
};
static bool current(const Capture& capture) {
    return !g_exitRequested.load(std::memory_order_acquire)&&
        g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready&&
        nativeValveSelectionCurrent(capture.game)&&capture.steam&&
        g_steamSettingsObserver.sameIdentityEpoch(capture.steam);
}
static std::optional<Capture> capture(const json& args,json& result) {
    const auto wanted=expected(args);
    if(!wanted){result=response(nullptr,"invalid-expected-game");return {};}
    Capture found;
    if(!nativeValveReadCachedSelection(&found.game)){
        result=response(nullptr,"game-selection-unavailable");return {};
    }
    result=response(&found.game,"steam-association-unavailable");
    const auto& selected=found.game.target;
    if(selected.game.pid!=wanted->pid||selected.game.processCreated!=wanted->birth||
        selected.identityGeneration!=wanted->selection||
        _wcsicmp(selected.game.path.c_str(),wanted->path.c_str())!=0){
        result["steam"]=unknownSteam("game-selection-changed");return {};
    }
    const auto scope=nativeGameFactScope(selected.game,selected.rulesEpoch,found.game.powerGeneration);
    if(!scope){result["steam"]=unknownSteam("game-fact-scope-unavailable");return {};}
    // The sole shared builder owns canonical path/policy/Steam identity. Global
    // selection/config/document metadata cannot split the common facts cache.
    found.key=nativeSteamGameTrackerFactKey(*scope);
    found.steam=g_steamSettingsObserver.identitySnapshot();
    if(!found.key||!found.steam||trackerSteamReceipt(found.steam)!=found.key->facts().steam){
        result["steam"]=unknownSteam("steam-identity-unavailable");return {};
    }
    if(!current(found)){result["steam"]=unknownSteam("joint-scope-changed");return {};}
    return found;
}
// One optional get observer, not a fact cache or an execution/source owner.
// completionNoticeMx protects the pointer, consumed/cancelled and RAII handoff.
// Its only nested scope checks are the existing memory-only current(Capture).
// No owner watch call, sink, mailbox call or event emission runs under this lock.
struct CompletionNotice final {
    const Capture captured;
    const std::uint64_t pendingSequence,pendingFactVersion;
    TrackerCompletionSubscription subscription;
    bool consumed=false,cancelled=false;
    // Metadata dispatch only: original attempt plus at most two existing-event retries.
    bool rereadPending=false,rereadPosting=false,retrying=false;
    bool notificationPending=false,notifying=false;
    std::optional<TrackerCompletionHint> notificationHint;
    unsigned dispatchRetries=0;
    CompletionNotice(const Capture& value,const TrackerFlightReceipt& pending)
        :captured(value),pendingSequence(pending.flightSequence),pendingFactVersion(pending.factVersion){}
};
static std::mutex completionNoticeMx;
static std::shared_ptr<CompletionNotice> completionNotice;
static constexpr unsigned completionDispatchRetryLimit=2;
// Local dispatch bookkeeping only. Pointer/state transitions serialize under
// completionNoticeMx; main-event retries with no live work avoid that mutex.
// It never establishes source/current/Ready or restores a cancelled notice.
static std::atomic<bool> hasLiveCompletionNotice{false};
static bool sameCompletionCapture(const Capture& a,const Capture& b) noexcept {
    // The interned canonical key already includes exact game/Steam/rules/power.
    // Selection belongs to the subscriber, not the common canonical fact key.
    return a.key==b.key&&a.game.target.identityGeneration==b.game.target.identityGeneration&&
        a.game.target.rulesEpoch==b.game.target.rulesEpoch&&a.game.powerGeneration==b.game.powerGeneration;
}
// Caller holds completionNoticeMx; retired objects are destroyed after unlocking.
static void retireCompletionNoticeLocked(std::shared_ptr<CompletionNotice>& retired,
    TrackerCompletionSubscription& retiredSubscription) noexcept {
    retired=std::move(completionNotice);
    hasLiveCompletionNotice.store(false,std::memory_order_release);
    if(retired){retired->cancelled=true;retiredSubscription=std::move(retired->subscription);}
}
// Future original main power/exit events may call this AFTER releasing source locks.
// No implicit hook, observer, timer, refresh cancellation or original-action change.
[[maybe_unused]] static void cancelCompletionNotice() noexcept {
    std::shared_ptr<CompletionNotice> retired;
    TrackerCompletionSubscription retiredSubscription;
    try{std::lock_guard lock(completionNoticeMx);retireCompletionNoticeLocked(retired,retiredSubscription);}catch(...){}
}
static void pruneCompletionNotice(const Capture* observed=nullptr) noexcept {
    std::shared_ptr<CompletionNotice> retired;
    TrackerCompletionSubscription retiredSubscription;
    try{
        std::lock_guard lock(completionNoticeMx);
        if(!completionNotice)return;
        if(observed&&sameCompletionCapture(completionNotice->captured,*observed))return;
        // An invalid/late get cannot cancel a newer still-current notice.
        if(current(completionNotice->captured)&&(!observed||!current(*observed)))return;
        retireCompletionNoticeLocked(retired,retiredSubscription);
    }catch(...){}
}
// Called ONLY by the later main-window mailbox drain, never by the owner sink.
static void drainCompletionNotice(const std::weak_ptr<CompletionNotice>& weak,
    const std::optional<TrackerCompletionHint>& hint,bool retryNativeEvent=false) noexcept {
    const auto notice=weak.lock();if(!notice)return;
    std::shared_ptr<CompletionNotice> retired;
    TrackerCompletionSubscription finishedSubscription,retiredSubscription;
    bool notify=false,scopeCurrent=false,accepted=false;
    try{
        {
            std::lock_guard lock(completionNoticeMx);
            if(completionNotice!=notice||notice->cancelled||notice->consumed||notice->notifying||
+                (notice->notificationPending&&!retryNativeEvent))return;
            if(hint&&(!hint->subscribed()||hint->key()!=notice->captured.key||
                !hint->receipt().terminal()||!hint->receipt().flightSequence||!hint->receipt().revision||
                hint->receipt().flightSequence!=notice->pendingSequence))return;
            if(!current(notice->captured)){
                retireCompletionNoticeLocked(retired,finishedSubscription);
            }else{
                // Claim one native event offer, not successful UI delivery.
                // Keep the original watch alive through both memory guards.
                notice->notifying=true;notice->notificationPending=false;
                notice->notificationHint.reset();
                finishedSubscription=std::move(notice->subscription);
                notify=true;
            }
        }
        if(!notify)return;
        try{
            scopeCurrent=current(notice->captured);
            if(scopeCurrent){
                accepted=gamepadSerialTryPostEvent("gameSteam.context.changed",{
                    {"pid",notice->captured.game.target.game.pid},
                    {"identityGeneration",notice->captured.game.target.identityGeneration}});
            }
        }catch(...){ /* Construction/admission failure is still undelivered metadata. */ }
        {
            std::lock_guard lock(completionNoticeMx);
            notice->notifying=false;
            if(completionNotice!=notice||notice->cancelled||notice->consumed)return;
            if(!scopeCurrent){
                retireCompletionNoticeLocked(retired,retiredSubscription);
            }else if(accepted){
                // Actual original native mailbox + PostMessage admission only;
                // this is not a renderer ACK, Ready or source/action authority.
                notice->consumed=true;
                retiredSubscription=std::move(notice->subscription); // A late initial/retry installation.
                hasLiveCompletionNotice.store(false,std::memory_order_release);
            }else{
                notice->notificationPending=true;notice->notificationHint=hint;
                if(finishedSubscription)notice->subscription=std::move(finishedSubscription);
                hasLiveCompletionNotice.store(notice->dispatchRetries<completionDispatchRetryLimit,std::memory_order_release);
            }
        }
    }catch(...){ /* Optional delivery cannot change the original get/action result. */ }
    // Retired/detached RAII is returned outside the notice mutex. Initial/Retry
    // arming that drained early keeps its local issue alive until installation.
}
// One message attempt, including the original ReReadNow offer. Never inline
// consume or source-read; rejected admission remains in this notice, not a timer.
static void postCompletionReread(const std::shared_ptr<CompletionNotice>& notice) noexcept {
    bool accepted=false;
    try{
        {
            std::lock_guard lock(completionNoticeMx);
            if(completionNotice!=notice||notice->cancelled||notice->consumed||notice->rereadPosting)return;
            notice->rereadPending=true;notice->rereadPosting=true;
        }
        try{
            const std::weak_ptr<CompletionNotice> weak=notice;
            accepted=postSteamTrackerCompletionDispatch([weak]{drainCompletionNotice(weak,std::nullopt);});
        }catch(...){ /* Queue construction/admission failure keeps this pending. */ }
        {
            std::lock_guard lock(completionNoticeMx);
            notice->rereadPosting=false;
            notice->rereadPending=completionNotice==notice&&!notice->cancelled&&!notice->consumed&&!accepted;
        }
    }catch(...){ /* No synchronous consumer fallback or source execution. */ }
}
// Main owner calls ONCE from an already-existing main-window event, with the
// mailbox/source/consumer locks released. This never posts a retry-only wakeup.
// Both paths share two retries per original notice; repeated get/events do not
// refill them. Healthy Waiting/Ready/Offering/Enqueued notices do no current read.
[[maybe_unused]] static void retryCompletionNoticeOnMainEvent() noexcept {
    static constexpr unsigned dispatchRetryLimit=completionDispatchRetryLimit;
    if(!hasLiveCompletionNotice.load(std::memory_order_acquire))return;
    if(g_exitRequested.load(std::memory_order_acquire)){cancelCompletionNotice();return;}
    std::shared_ptr<CompletionNotice> notice,retired;
    TrackerCompletionSubscription retrySubscription,retiredSubscription;
    bool retryReread=false,retryOwner=false,retryNotification=false;
    std::optional<TrackerCompletionHint> notificationHint;
    try{
        {
            std::lock_guard lock(completionNoticeMx);
            notice=completionNotice;
            if(!notice||notice->cancelled||notice->consumed||notice->retrying||notice->rereadPosting||notice->notifying||
                notice->dispatchRetries>=dispatchRetryLimit)return;
            retryNotification=notice->notificationPending;
            if(retryNotification)notificationHint=notice->notificationHint;
            retryReread=!retryNotification&&notice->rereadPending;
            if(!retryNotification&&!retryReread){
                const auto delivery=notice->subscription.delivery();
                retryOwner=delivery==TrackerCompletionDelivery::Rejected||delivery==TrackerCompletionDelivery::SinkFailed;
            }
            if(!retryNotification&&!retryReread&&!retryOwner)return;
            if(!current(notice->captured)){
                retireCompletionNoticeLocked(retired,retiredSubscription);return;
            }
            notice->retrying=true;++notice->dispatchRetries;
            // The final authorized offer still runs below. Later existing
            // events cannot wait on this mutex or refund the spent allowance.
            if(notice->dispatchRetries>=dispatchRetryLimit)
                hasLiveCompletionNotice.store(false,std::memory_order_release);
            if(retryOwner)retrySubscription=std::move(notice->subscription);
        }
        // Preserve the SAME subscription while detached. A cancelled/replaced
        // state or early drain cannot be restored by the later handoff below.
        if(retryNotification)drainCompletionNotice(std::weak_ptr<CompletionNotice>(notice),notificationHint,true);
        else if(retryReread)postCompletionReread(notice);
        else (void)trackerOwner().retryCompletion(retrySubscription);
        {
            std::lock_guard lock(completionNoticeMx);
            notice->retrying=false;
            if(completionNotice==notice&&!notice->cancelled&&!notice->consumed&&retryOwner)
                notice->subscription=std::move(retrySubscription);
        }
        // Detached RAII destruction, including cancellation, stays outside the lock.
    }catch(...){ /* Optional bounded dispatch never falls back to source work. */ }
}
// Failed registration leaves the same-flight tombstone for get coalescing,
// but has no watch or pending dispatch for an existing main event to resume.
// An old failure must never clear a replacement or an active handoff's gate.
static void clearFailedCompletionRegistrationGate(const std::shared_ptr<CompletionNotice>& notice) noexcept {
    if(!notice)return;
    try{
        std::lock_guard lock(completionNoticeMx);
        if(completionNotice==notice&&!notice->cancelled&&!notice->consumed&&
            !notice->subscription&&!notice->rereadPending&&!notice->rereadPosting&&!notice->retrying&&!notice->notificationPending&&!notice->notifying)
            hasLiveCompletionNotice.store(false,std::memory_order_release);
    }catch(...){ /* No registration retry, source work or replacement change. */ }
}
static void watchPendingCompletion(const Capture& captured,const TrackerPeekResult& cached) noexcept {
    if(cached.status!=TrackerPeekStatus::Pending||cached.reason!=TrackerPeekUnknown::Pending||
        cached.key!=captured.key||cached.flight.terminal()||!cached.flight.flightSequence||!cached.flight.factVersion||
        !cached.completion||cached.completion.key()!=captured.key||
        cached.completion.sequence()!=cached.flight.flightSequence||
        cached.completion.factVersion()!=cached.flight.factVersion)return;
    std::shared_ptr<CompletionNotice> notice,retired;
    TrackerCompletionSubscription retiredSubscription;
    try{
        {
            std::lock_guard lock(completionNoticeMx);
            if(completionNotice&&sameCompletionCapture(completionNotice->captured,captured)&&
                completionNotice->pendingSequence==cached.flight.flightSequence&&
                completionNotice->pendingFactVersion==cached.flight.factVersion)return;
            // Serialized memory-only current check prevents a late A get from
            // replacing a newer B notice after A lost its original Capture.
            if(!current(captured))return;
            notice=std::make_shared<CompletionNotice>(captured,cached.flight);
            retireCompletionNoticeLocked(retired,retiredSubscription);
            completionNotice=notice; // Publish Arming before watch can enqueue.
            hasLiveCompletionNotice.store(true,std::memory_order_release);
        }
        retiredSubscription.reset();
        const std::weak_ptr<CompletionNotice> weak=notice;
        auto issue=trackerOwner().watchCompletion(cached.completion,[weak](const TrackerCompletionHint& hint){
            // The owner caller may hold refreshMx. ONLY queue a value callback;
            // do not lock notice state, check current, re-read or call consumers.
            return postSteamTrackerCompletionDispatch([weak,hint]{drainCompletionNotice(weak,hint);});
        });
        if(issue.status==TrackerCompletionWatchStatus::Watching){
            std::lock_guard lock(completionNoticeMx);
            if(completionNotice==notice&&!notice->cancelled&&!notice->consumed)
                notice->subscription=std::move(issue.subscription);
            // Otherwise local issue destruction cancels. NEVER resurrect a
            // consumed/replaced state after an early main-window drain.
        }else if(issue.status==TrackerCompletionWatchStatus::ReReadNow){
            // Same one-shot main drain, no inline consumption or source work.
            postCompletionReread(notice);
        }else{
            // Capacity/InvalidWitness is terminal for this registration only.
            clearFailedCompletionRegistrationGate(notice);
        }
        // Registration/capacity failure does not retry. Failed DISPATCH only
        // can resume through retryCompletionNoticeOnMainEvent, never this get.
        // The same-flight notice/tombstone still coalesces repeated gets.
    }catch(...){
        // Allocation/construction may fail after publishing Arming. The local
        // issue has already unwound; keep its tombstone, never a live retry gate.
        clearFailedCompletionRegistrationGate(notice);
    }
}
static const char* peekReason(TrackerPeekUnknown reason) {
    switch(reason){
    case TrackerPeekUnknown::Missing:return "association-not-observed";
    case TrackerPeekUnknown::Pending:return "association-read-pending";
    case TrackerPeekUnknown::OwnerBusy:return "association-owner-busy";
    case TrackerPeekUnknown::InvalidKey:return "association-key-invalid";
    case TrackerPeekUnknown::OwnerChanged:return "association-owner-changed";
    case TrackerPeekUnknown::CacheChanged:return "association-cache-changed";
    case TrackerPeekUnknown::SourceUnknown:return "association-source-unavailable";
    default:return "association-unavailable";
    }
}
static const char* sourceReason(TrackerStatus status) {
    switch(status){
    case TrackerStatus::SourceUnavailable:return "association-source-unavailable";
    case TrackerStatus::SourceTruncated:return "association-source-truncated";
    case TrackerStatus::SourceIncomplete:return "association-source-incomplete";
    case TrackerStatus::SourceReadException:return "association-source-read-exception";
    case TrackerStatus::ParserRemoved:return "association-source-removed";
    case TrackerStatus::ParserAmbiguous:return "association-source-ambiguous";
    case TrackerStatus::ParserIncomplete:return "association-source-incomplete";
    case TrackerStatus::FullGameIdUnknown:return "association-full-game-id-unknown";
    default:return "association-source-unconfirmed";
    }
}
static json read(const json& args) {
    json result;const auto captured=capture(args,result);
    if(!captured){pruneCompletionNotice();return result;}
    pruneCompletionNotice(&*captured);
    const auto owned=[&]{return current(*captured);};
    const auto cached=nativeSteamGameTrackerPeek(captured->key,owned);
    if(!owned()){pruneCompletionNotice();result["steam"]=unknownSteam("joint-scope-changed");return result;}
    watchPendingCompletion(*captured,cached);
    const auto& association=cached.association;
    if(cached.status!=TrackerPeekStatus::CachedHistoricalAssociation||!association){
        const auto reason=cached.reason==TrackerPeekUnknown::SourceUnknown&&cached.observation?
            sourceReason(cached.observation->status):peekReason(cached.reason);
        result["steam"]=unknownSteam(reason,cached.status==TrackerPeekStatus::Pending?"pending":"unknown");
        return result;
    }
    const auto& key=captured->key->facts();
    const auto& game=key.selectedGame;
    const auto& actual=association->key;
    if(!cached.key||cached.key->facts()!=key||actual.observation!=key||!association->hasHistoricalBinding()||!association->exactPath||
        cached.processValidated||cached.currentSource||association->processValidated||association->provenance.currentSource||
        !association->provenance.cacheHit||!exactGameId(association->trackerAppId,association->fullGameId)||
        association->fullGameIdDecimal!=std::to_string(association->fullGameId)||
        actual.observedGame.pid!=game.pid||actual.observedGame.birth!=game.birth||
        !samePath(actual.observedGame.exactPath,game.exactPath)||
        actual.heldSteam.pid!=key.steam.pid||actual.heldSteam.birth!=key.steam.birth||
        !samePath(actual.heldSteam.exactPath,key.steam.root+L"\\steam.exe")){
        result["steam"]=unknownSteam("association-game-mismatch");return result;
    }
    result["steam"]={{"status","cached-association"},{"reason","historical-process-lifetime-binding"},
        {"appid",association->trackerAppId},{"gameId",association->fullGameIdDecimal},
        {"gamePid",game.pid},{"gameCreated",std::to_string(game.birth)},{"gamePath",W2U(captured->game.target.game.path)},
        {"gameIdentityGeneration",captured->game.target.identityGeneration},
        {"steamPid",key.steam.pid},{"steamCreated",std::to_string(key.steam.birth)},
        {"processEpoch",std::to_string(key.steam.processEpoch)},{"accountEpoch",std::to_string(key.steam.accountEpoch)},
        {"processValidated",false},{"currentSource",false}};
    if(association->groupObservation)result["steam"]["processGroup"]=cachedGroupView(*association);
    // Cross-owner reads are a checked join, not an atomic simultaneous sample.
    if(!owned())result["steam"]=unknownSteam("joint-scope-changed");
    return result;
}
// Transient consumer outcomes, never action authority or another fact cache.
enum class ConsumerStatus { CachedAssociation, ObservedAssociation, ObservedNegative, Unavailable, Timeout, Cancelled };
struct ConsumerResult {
    ConsumerStatus kind=ConsumerStatus::Unavailable;
    TrackerObservationKey key;
    std::shared_ptr<const TrackerSnapshot> association,observation;
    TrackerFlightReceipt requestReceipt;
    const char* reason="association-unavailable";
    ConsumerResult()=default;
    ConsumerResult(ConsumerStatus state,std::shared_ptr<const TrackerSnapshot> source,
        TrackerFlightReceipt receipt,const char* why,TrackerObservationKey fact={})
        :kind(state),key(std::move(fact)),requestReceipt(receipt),reason(why){
        if(state==ConsumerStatus::CachedAssociation||state==ConsumerStatus::ObservedAssociation)association=source;
        if(state==ConsumerStatus::ObservedAssociation||state==ConsumerStatus::ObservedNegative)observation=std::move(source);
    }
};
struct ConsumerCompletion {
    std::condition_variable changed;
    bool completed=false; // All state below is protected by refreshMx.
    ConsumerResult result;
};
struct RefreshSubscriber {
    Capture captured; // Original global-selection subscriber only.
    std::shared_ptr<const NativeGameFactScope> explicitGame;
    std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> explicitSteam;
    std::shared_ptr<ConsumerCompletion> completion;
    unsigned long long deadline=0;
    TrackerDemand demand;
    RefreshSubscriber(Capture value,TrackerDemand request):captured(std::move(value)),demand(std::move(request)){}
    RefreshSubscriber(const NativeGameFactScope& scope,
        std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> steam,
        std::shared_ptr<ConsumerCompletion> done,unsigned long long until)
        :explicitGame(std::make_shared<const NativeGameFactScope>(scope)),explicitSteam(std::move(steam)),
         completion(std::move(done)),deadline(until){}
};
struct RefreshWork {
    const TrackerObservationKey key;
    const std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> steam;
    const unsigned long long sequence;
    TrackerFlightToken token;
    std::vector<std::shared_ptr<RefreshSubscriber>> subscribers;
    std::atomic<bool> taken{false};
    bool completed=false; // Under refreshMx; no facts/results retained here.
    RefreshWork(TrackerObservationKey fact,
        std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> identity,TrackerFlightToken flight)
        :key(std::move(fact)),steam(std::move(identity)),sequence(flight.sequence()),token(std::move(flight)){}
};
// In-flight transport/request lifetimes only. Actual facts, IDs, cache and
// single physical observation continue to belong solely to TrackerOwner.
static std::mutex refreshMx;
static std::vector<std::weak_ptr<RefreshWork>> refreshWorks;
static std::shared_ptr<RefreshWork> findWork(const TrackerObservationKey& key,unsigned long long sequence=0) {
    std::shared_ptr<RefreshWork> found;
    for(auto it=refreshWorks.begin();it!=refreshWorks.end();){
        const auto work=it->lock();
        if(!work||work->completed){it=refreshWorks.erase(it);continue;}
        if(work->key->facts()==key->facts()&&(!sequence||work->sequence==sequence))found=work;
        ++it;
    }
    return found;
}
static bool explicitCurrent(const RefreshSubscriber& subscriber) {
    return subscriber.explicitGame&&nativeGameFactScopeCurrent(*subscriber.explicitGame)&&
        subscriber.explicitSteam&&g_steamSettingsObserver.sameIdentityEpoch(subscriber.explicitSteam);
}
static bool subscriberCurrent(const RefreshSubscriber& subscriber) {
    if(!subscriber.completion)return current(subscriber.captured);
    return !subscriber.completion->completed&&explicitCurrent(subscriber)&&GetTickCount64()<subscriber.deadline;
}
static void finishConsumerLocked(RefreshSubscriber& subscriber,ConsumerResult result,
    TrackerRequestCloseReason reason=TrackerRequestCloseReason::Cancelled) {
    if(!subscriber.completion||subscriber.completion->completed)return;
    if(!result.key)result.key=subscriber.demand.factKey();
    if(!result.requestReceipt.demandId&&subscriber.demand)
        result.requestReceipt=trackerOwner().requestReceipt(subscriber.demand);
    subscriber.completion->result=std::move(result);
    subscriber.completion->completed=true;
    nativeSteamGameTrackerClose(std::move(subscriber.demand),reason);
    subscriber.completion->changed.notify_all();
}
static bool sourceNegative(const TrackerSnapshot& snapshot) noexcept {
    if(!snapshot.observation.readAttempted||!snapshot.provenance.sourceReadThisFlight||
        snapshot.provenance.flight.phase!=TrackerFlightPhase::Failed)return false;
    switch(snapshot.status){
    case TrackerStatus::SourceUnavailable:case TrackerStatus::SourceTruncated:
    case TrackerStatus::SourceIncomplete:case TrackerStatus::SourceReadException:
    case TrackerStatus::ParserUnknown:case TrackerStatus::ParserRemoved:
    case TrackerStatus::ParserAmbiguous:case TrackerStatus::ParserIncomplete:
    case TrackerStatus::FullGameIdUnknown:return true;
    default:return false; // Preparation/scope/cancellation is never a negative.
    }
}
static ConsumerResult consumerResult(const TrackerObservationKey& key,
    const std::shared_ptr<const TrackerSnapshot>& snapshot,TrackerFlightReceipt receipt) {
    if(!snapshot||snapshot->key.observation!=key->facts()||!receipt.terminal()||
        receipt.flightSequence!=snapshot->provenance.flight.flightSequence)
        return {ConsumerStatus::Unavailable,{},receipt,"association-completion-unavailable",key};
    if(receipt.phase==TrackerFlightPhase::Succeeded&&snapshot->hasHistoricalBinding()&&
        snapshot->exactPath&&exactGameId(snapshot->trackerAppId,snapshot->fullGameId))
        return {ConsumerStatus::ObservedAssociation,snapshot,receipt,"historical-process-lifetime-binding",key};
    if(receipt.phase==TrackerFlightPhase::Failed&&sourceNegative(*snapshot))
        return {ConsumerStatus::ObservedNegative,snapshot,receipt,sourceReason(snapshot->status),key};
    return {ConsumerStatus::Unavailable,{},receipt,"association-observation-unconfirmed",key};
}
static void releaseSubscribers(std::vector<std::shared_ptr<RefreshSubscriber>> subscribers) {
    std::shared_ptr<RefreshSubscriber> notify;
    for(auto& subscriber:subscribers){
        if(subscriber->completion)continue; // Completed/cancelled under refreshMx.
        if(current(subscriber->captured))notify=subscriber;
        subscriber->demand=TrackerDemand{};
    }
    if(notify&&current(notify->captured))gamepadSerialPostEvent("gameSteam.context.changed",{
        {"pid",notify->captured.game.target.game.pid},
        {"identityGeneration",notify->captured.game.target.identityGeneration}});
}
static void runRefresh(const std::shared_ptr<RefreshWork>& work) {
    if(work->taken.exchange(true,std::memory_order_acq_rel))return;
    std::vector<std::shared_ptr<RefreshSubscriber>> stale;
    {
        std::lock_guard lock(refreshMx);
        for(auto it=work->subscribers.begin();it!=work->subscribers.end();){
            auto& subscriber=**it;
            if(!subscriberCurrent(subscriber)){
                if(subscriber.completion){
                    const bool expired=GetTickCount64()>=subscriber.deadline;
                    finishConsumerLocked(subscriber,{expired?ConsumerStatus::Timeout:ConsumerStatus::Cancelled,{},
                        {},expired?"association-deadline-expired":"association-consumer-cancelled"},
                        expired?TrackerRequestCloseReason::Expired:TrackerRequestCloseReason::ScopeChanged);
                }
                stale.push_back(std::move(*it));it=work->subscribers.erase(it);
            }else ++it;
        }
    }
    for(auto& subscriber:stale)if(!subscriber->completion)
        nativeSteamGameTrackerClose(std::move(subscriber->demand),TrackerRequestCloseReason::ScopeChanged);
    bool eligible=false;
    {
        std::lock_guard lock(refreshMx);
        eligible=nativeSteamGameTrackerExecutionEligible(work->token);
        if(!eligible){
            work->completed=true;
            nativeSteamGameTrackerReleaseExecution(std::move(work->token),TrackerExecutionCloseReason::QueueRejected);
        }
    }
    std::shared_ptr<const TrackerSnapshot> observed;
    if(eligible)try{
        const auto& selected=work->key->facts().selectedGame;
        auto lease=processOwner().acquire(g_steamSettingsObserver,work->steam,
            ProcessWitness{selected.pid,selected.birth,selected.exactPath});
        observed=nativeSteamGameTrackerObserve(std::move(work->token),std::move(lease));
    }catch(...){ /* No retry. The original owner/RAII retains failure. */ }
    std::vector<std::shared_ptr<RefreshSubscriber>> completed;
    {
        std::lock_guard lock(refreshMx);
        work->completed=true;work->token=TrackerFlightToken{};
        for(auto& subscriber:work->subscribers)if(subscriber->completion){
            if(!subscriberCurrent(*subscriber)){
                const bool expired=GetTickCount64()>=subscriber->deadline;
                finishConsumerLocked(*subscriber,{expired?ConsumerStatus::Timeout:ConsumerStatus::Cancelled,{},
                    {},expired?"association-deadline-expired":"association-consumer-cancelled"},
                    expired?TrackerRequestCloseReason::Expired:TrackerRequestCloseReason::ScopeChanged);
            }else finishConsumerLocked(*subscriber,consumerResult(work->key,observed,
                trackerOwner().requestReceipt(subscriber->demand)));
        }
        completed.swap(work->subscribers);
    }
    releaseSubscribers(std::move(completed));
}
// Caller owns refreshMx. No caller callback survives this synchronous claim.
static const char* enqueueSubscriber(const TrackerObservationKey& key,
    const std::shared_ptr<const ymcc::steamsession::IdentitySnapshot>& steam,
    const std::shared_ptr<RefreshSubscriber>& subscriber,TrackerClaim claim) {
    if(claim.status==TrackerClaimStatus::Pending){
        if(const auto work=findWork(key,claim.receipt.flightSequence)){
            if(work->subscribers.size()<TrackerOwner::requestCapacity){
                subscriber->demand=std::move(claim.request);work->subscribers.push_back(subscriber);return nullptr;
            }
        }
        return "association-executor-not-owned";
    }
    if(claim.status!=TrackerClaimStatus::Claimed||!claim.token)return "association-refresh-not-claimed";
    // Fresh explicit-only flights must also retire completed weak transports.
    (void)findWork(key);
    auto work=std::make_shared<RefreshWork>(key,steam,std::move(claim.token));
    subscriber->demand=std::move(claim.request);work->subscribers.push_back(subscriber);
    refreshWorks.emplace_back(work);
    if(steamGameLinkSubmitObservation([work]{runRefresh(work);}))return nullptr;
    work->completed=true;
    nativeSteamGameTrackerClose(std::move(subscriber->demand),TrackerRequestCloseReason::QueueRejected);
    nativeSteamGameTrackerReleaseExecution(std::move(work->token),TrackerExecutionCloseReason::QueueRejected);
    return "association-refresh-queue-unavailable";
}
static json refresh(const json& args) {
    json result;auto captured=capture(args,result);
    if(!captured)return result;
    const auto owned=[&]{return current(*captured);};
    std::unique_lock lock(refreshMx);
    if(const auto pending=findWork(captured->key)){
        for(const auto& subscriber:pending->subscribers)
            if(!subscriber->completion&&subscriber->captured.game.target.identityGeneration==captured->game.target.identityGeneration&&
                current(subscriber->captured)){
                result["steam"]=unknownSteam("association-refresh-queued","pending");return result;
            }
    }
    TrackerRequestContext context;context.selectionRevision=captured->game.target.identityGeneration;
    auto issued=nativeSteamGameTrackerBeginDemand(captured->key,std::move(context),owned);
    if(!issued.demand){result["steam"]=unknownSteam("association-demand-unavailable");return result;}
    auto claim=nativeSteamGameTrackerClaim(std::move(issued.demand),owned,TrackerReadMode::AllowHistoricalBinding);
    if(claim.status==TrackerClaimStatus::HistoricalAvailable){lock.unlock();return read(args);}
    auto subscriber=std::make_shared<RefreshSubscriber>(std::move(*captured),TrackerDemand{});
    const auto failed=enqueueSubscriber(subscriber->captured.key,subscriber->captured.steam,subscriber,std::move(claim));
    result["steam"]=failed?unknownSteam(failed):unknownSteam("association-refresh-queued","pending");return result;
}
constexpr unsigned long long consumerWaitCapMs=750;
static bool callerAllowed(const std::function<bool()>& allowed) noexcept {
    try{return allowed&&allowed();}catch(...){return false;}
}
struct ConsumerTicket final {
    const std::shared_ptr<RefreshSubscriber> subscriber;
    explicit ConsumerTicket(std::shared_ptr<RefreshSubscriber> value):subscriber(std::move(value)){}
    ConsumerTicket(const ConsumerTicket&)=delete;
    ConsumerTicket& operator=(const ConsumerTicket&)=delete;
    ~ConsumerTicket(){
        try{std::lock_guard lock(refreshMx);finishConsumerLocked(*subscriber,
            {ConsumerStatus::Cancelled,{}, {},"association-consumer-cancelled"});}catch(...){}
    }
};
// Explicit foreground consumers retain their own closed game scope. The
// synchronous allowed callback stays on THIS stack, never in a queued job.
// A result is association metadata only; fresh action/UI/Host gates stay with
// the caller. No original menu dispatch, fallback or replay happens here.
static ConsumerResult observeForConsumer(const NativeGameFactScope& scope,TrackerRequestContext requestContext,unsigned long long deadline,
    const std::function<bool()>& allowed) {
    if(!callerAllowed(allowed)||!nativeGameFactScopeCurrent(scope))
        return {ConsumerStatus::Cancelled,{}, {},"association-consumer-cancelled"};
    const auto key=nativeSteamGameTrackerFactKey(scope);
    const auto steam=g_steamSettingsObserver.identitySnapshot();
    if(!key||!steam||trackerSteamReceipt(steam)!=key->facts().steam)
        return {ConsumerStatus::Unavailable,{}, {},"steam-identity-unavailable",key};
    const auto owned=[&]{return nativeGameFactScopeCurrent(scope)&&
        g_steamSettingsObserver.sameIdentityEpoch(steam)&&callerAllowed(allowed);};
    const auto cached=nativeSteamGameTrackerPeek(key,owned);
    // This click has no demand/read. Source provenance stays on association;
    // an old source flight's readsStarted must not become this click's receipt.
    const auto historical=[&](const TrackerPeekResult& value){
        TrackerFlightReceipt receipt;receipt.phase=TrackerFlightPhase::HistoricalAvailable;
        receipt.lastStatus=value.association->status;
        return ConsumerResult{ConsumerStatus::CachedAssociation,value.association,receipt,"historical-process-lifetime-binding",key};
    };
    if(cached.status==TrackerPeekStatus::CachedHistoricalAssociation&&cached.association&&owned())return historical(cached);
    const auto started=GetTickCount64();
    if(!deadline||deadline<=started||deadline-started>consumerWaitCapMs)
        return {ConsumerStatus::Timeout,{}, {},"association-deadline-invalid",key};
    auto completion=std::make_shared<ConsumerCompletion>();
    auto subscriber=std::make_shared<RefreshSubscriber>(scope,steam,completion,deadline);
    ConsumerTicket ticket(subscriber);
    {
        std::unique_lock lock(refreshMx);
        auto issued=nativeSteamGameTrackerBeginDemand(key,std::move(requestContext),owned);
        if(!issued.demand)return {ConsumerStatus::Unavailable,{}, {},"association-demand-unavailable",key};
        auto claim=nativeSteamGameTrackerClaim(std::move(issued.demand),owned,TrackerReadMode::AllowHistoricalBinding);
        if(claim.status==TrackerClaimStatus::HistoricalAvailable){
            lock.unlock();const auto available=nativeSteamGameTrackerPeek(key,owned);
            if(available.association&&owned())return historical(available);
            return {ConsumerStatus::Unavailable,{}, {},"association-cache-changed",key};
        }
        if(const auto failure=enqueueSubscriber(key,steam,subscriber,std::move(claim)))
            return {ConsumerStatus::Unavailable,{}, {},failure,key};
    }
    std::unique_lock lock(refreshMx);
    // A single predicate wait consumes the existing caller's absolute budget.
    // No repeated guard/foreground/Steam checks or additional worker/timer.
    const auto now=GetTickCount64();
    if(now<deadline)completion->changed.wait_for(lock,std::chrono::milliseconds(deadline-now),[&]{return completion->completed;});
    if(!completion->completed)finishConsumerLocked(*subscriber,{ConsumerStatus::Timeout,{}, {},"association-deadline-expired"},TrackerRequestCloseReason::Expired);
    auto result=completion->result;lock.unlock();
    if(!owned())return {ConsumerStatus::Cancelled,{},result.requestReceipt,"association-consumer-cancelled",key};
    if(GetTickCount64()>=deadline)return {ConsumerStatus::Timeout,{},result.requestReceipt,"association-deadline-expired",key};
    return result;
}

}
static json nativeGameSteamContextGet(const json& args){return GameSteamContext::read(args);}
static json nativeGameSteamContextRefresh(const json& args){return GameSteamContext::refresh(args);}
