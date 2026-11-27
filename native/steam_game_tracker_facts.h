#pragma once
#include "steam_game_tracker_observation.h"
#include "steam_game_tracker_completion.h"
#include "steam_game_process_facts.h"
#include "steam_game_group_lease.h"
#include "steam_startup_handoff.h"
#include "steam_historical_startup_handoff.h"
#include <algorithm>
#include "sleep_steam_tracking.h"
#include "sleep_steam_capture.h"
#include <array>
#include <limits>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>

// PRIVATE / UNBUILT. Unique EXE and Steam layers remain separate. This owner
// owns bounded fact/request/flight lifetimes, never selection or action rights.
namespace ymcc::steamgamefacts {
inline TrackerSteamReceipt trackerSteamReceipt(const std::shared_ptr<const IdentitySnapshot>& snapshot) {
    TrackerSteamReceipt out;
    if (!snapshot) return out;
    out.present = snapshot->presence == 1;
    out.rootKnown = snapshot->rootReadError == ERROR_SUCCESS && !snapshot->session.root.empty();
    out.windowsSessionKnown = snapshot->windowsSessionKnown && snapshot->windowsSessionError == ERROR_SUCCESS;
    out.pid = snapshot->session.pid; out.birth = snapshot->session.created;
    out.root = snapshot->session.root; out.windowsSession = snapshot->windowsSession;
    out.processEpoch = snapshot->processEpoch; out.accountEpoch = snapshot->accountEpoch;
    return out;
}
// Defined only in main beside the actual shared Observer and game-owner scope.
// Public callers cannot construct a raw key, replace a Reader or supply IDs.
class MainTrackerFactsAdapter final {
    friend class TrackerOwner;
    static bool current(const TrackerFactKey& key) noexcept;
public:
    static SteamLogSourceIdentity sourceKey(const SteamLogIdentity& expected);
    static bool sourceCurrent(const SteamLogSourceKey&)noexcept;
    static TrackerObservationKey factKey(const ::NativeGameFactScope& alreadyQualified,
        TrackerObservationPolicy policy = TrackerObservationPolicy::ProcessOnlyObservation,
        TrackerAccountDomain account = {});
};
struct TrackerTailRead {
    std::string bytes;
    bool availableKnown = false, available = false, truncatedKnown = false, truncated = false;
    bool originalBytesKnown = false, copiedBytesKnown = false, limitBytesKnown = false;
    std::uint64_t originalBytes = 0, copiedBytes = 0, limitBytes = 0;
    bool win32ErrorKnown = false;
    std::uint32_t win32Error = 0;
    TrackerSourceObservation::ReadReason reason = TrackerSourceObservation::ReadReason::Unknown;
};
class MainReadTailPort final {
    friend class TrackerOwner;
    friend MainReadTailPort& mainReadTailPort() noexcept;
    MainReadTailPort() = default;
    TrackerTailRead readTail(const std::wstring&,SteamLogKind) const;
    std::string selectedPathUtf8(const std::wstring&) const;
    std::string localSecond(std::uint64_t) const;
    std::uint64_t nowTick() const noexcept;
    std::uint64_t nowFileTime() const noexcept;
    std::shared_ptr<const IdentitySnapshot> identitySnapshot() const;
public:
    MainReadTailPort(const MainReadTailPort&) = delete;
    MainReadTailPort& operator=(const MainReadTailPort&) = delete;
};
inline MainReadTailPort& mainReadTailPort() noexcept { static MainReadTailPort value; return value; }
namespace tracker_detail {
inline bool ownerCurrent(const TrackerOwnerCurrent& callback) noexcept {
    try { return callback && callback(); } catch (...) { return false; }
}
inline bool validFact(const TrackerFactTuple& key) noexcept {
    if (!key.selectedGame.pid || !key.selectedGame.birth || key.selectedGame.exactPath.empty() ||
        key.selectedGame.exactPath.size() > 32768 || key.selectedGame.exactPath.find(L'\0') != std::wstring::npos ||
        key.canonicalGamePath.empty() || key.canonicalSteamRoot.empty() ||
        !key.steam.present || !key.steam.rootKnown || !key.steam.windowsSessionKnown ||
        !key.steam.pid || !key.steam.birth || !key.steam.processEpoch ||
        key.steam.pid == key.selectedGame.pid || key.steam.root.empty() || key.steam.root.size() > 32768 ||
        key.steam.root.find(L'\0') != std::wstring::npos || !key.rulesEpoch || !key.powerGeneration) return false;
    // AccountBound is unsupported until the main adapter consumes real typed
    // UI/CM proof. Caller enum/account/document values are never such proof.
    return key.policy == TrackerObservationPolicy::ProcessOnlyObservation &&
        key.selectedGame.birth >= key.steam.birth && !key.account.resolvedAccount32 &&
        key.account.provenance == AccountProvenance::Unknown && key.account.source == AccountFactSource::Unknown;
}
inline bool validRequest(const TrackerFactTuple& key, const TrackerRequestContext& request) noexcept {
    if (request.document.size() > 256 || request.documentOwner.size() > 256 ||
        request.document.find('\0') != std::string::npos || request.documentOwner.find('\0') != std::string::npos) return false;
    return key.policy == TrackerObservationPolicy::ProcessOnlyObservation;
}
inline TrackerProcessIdentity processIdentity(const ProcessWitness& process) {
    return {process.pid,process.created,process.image};
}
inline TrackerStatus parserStatus(const std::string& status) noexcept {
    if (status == "removed") return TrackerStatus::ParserRemoved;
    if (status == "ambiguous") return TrackerStatus::ParserAmbiguous;
    if (status == "incomplete") return TrackerStatus::ParserIncomplete;
    return TrackerStatus::ParserUnknown;
}
inline TrackerGameIdKind groupGameIdKind(std::uint32_t appid,std::uint64_t full) noexcept {
    if(!exactGameId(appid,full))return TrackerGameIdKind::Unknown;
    return full==appid?TrackerGameIdKind::NativeApp:TrackerGameIdKind::ShortcutType2;
}
// No source read, lease/current callback or action gate: only one bounded
// inventory and its exact selected binding from the same parsed revision.
inline TrackerGroupObservation projectGroup(const TrackerInventoryProjection& inventory,
    const TrackerBindingProjection& binding,std::uint32_t pid,const std::string& path) {
    TrackerGroupObservation out;out.sourceRevision=inventory.sourceRevision;out.scope=inventory.scope;
    out.appid=binding.appid;out.selectedPid=pid;out.selectedSourceLine=binding.sourceLine;
    out.fullGameIdKnown=binding.fullGameIdKnown;
    out.gameIdRaw=binding.gameIdRaw;
    if(out.fullGameIdKnown){out.fullGameId=binding.fullGameId;out.fullGameIdDecimal=std::to_string(binding.fullGameId);}
    if(!inventory.complete){out.status=TrackerGroupStatus::Incomplete;
        out.reason=inventory.reason.empty()?"steam-tracker-group-inventory-incomplete":inventory.reason;return out;}
    if(!inventory.sourceRevision||inventory.sourceRevision!=binding.sourceRevision||inventory.scope!=binding.scope){
        out.status=TrackerGroupStatus::Incomplete;out.reason="steam-tracker-group-source-mismatch";return out;}
    if(inventory.members.size()>32){out.status=TrackerGroupStatus::Incomplete;
        out.reason="steam-tracker-group-member-limit";return out;}
    unsigned selectedPidRows=0;
    for(const auto& row:inventory.members)if(row.pid==pid)++selectedPidRows;
    if(pid&&selectedPidRows>1){
        out.status=TrackerGroupStatus::Incomplete;out.pidConflict=true;
        out.reason="steam-tracker-group-pid-conflict";return out;
    }
    if(!pid||path.empty()||binding.status!="tracked"||!binding.exactPath||!binding.sourceLine){
        out.reason="steam-tracker-group-selected-binding-unknown";return out;}
    if(!binding.fullGameIdKnown){out.reason="steam-tracker-group-full-gameid-unknown";return out;}
    out.gameIdKind=groupGameIdKind(binding.appid,binding.fullGameId);
    if(out.gameIdKind==TrackerGameIdKind::Unknown){out.reason="steam-tracker-group-full-gameid-unknown";return out;}
    const auto selectedPath=ymcc::sleepsteam::pathKey(path);
    for(const auto& row:inventory.members){
        const bool related=row.appid==binding.appid||row.pid==pid;
        if(related&&!row.fullGameIdKnown)out.relatedFullGameIdUnknown=true;
        if(related&&row.identityConflict)out.pidConflict=true;
        // Normalized AppID alone never admits a member of another full ID.
        if(!row.fullGameIdKnown||row.fullGameId!=binding.fullGameId)continue;
        if(row.appid!=binding.appid||row.gameIdKind!=out.gameIdKind){out.pidConflict=true;continue;}
        out.members.push_back(row);
        if(row.pid==pid&&row.sourceLine==binding.sourceLine&&row.at==binding.at&&
            row.gameIdRaw==binding.gameIdRaw&&ymcc::sleepsteam::pathKey(row.path)==selectedPath)
            out.selectedRowObserved=true;
        if(row.identityConflict)out.pidConflict=true;
        // A PID also present under any other inventory key is unresolved.
        // Iterate <=32 rows; no extra source or process observation is made.
        unsigned pidRows=0;
        for(const auto& other:inventory.members)if(other.pid==row.pid){
            ++pidRows;if(!other.fullGameIdKnown)out.relatedFullGameIdUnknown=true;
        }
        if(pidRows!=1)out.pidConflict=true;
    }
    if(out.relatedFullGameIdUnknown||out.pidConflict||!out.selectedRowObserved){
        out.status=TrackerGroupStatus::Incomplete;
        out.reason=out.pidConflict?"steam-tracker-group-pid-conflict":out.relatedFullGameIdUnknown?
            "steam-tracker-group-related-gameid-unknown":"steam-tracker-group-selected-row-missing";
        return out;
    }
    out.status=TrackerGroupStatus::Observed;out.reason="steam-tracker-group-observed";return out;
}
using TrackerSourceInventories=std::array<ymcc::sleepsteam::TrackedProcessInventory,2>;
struct FreshStartupProjection {
    std::vector<std::string_view> lines;
    TrackerSourceInventories inventories;
};
template <typename Line>
inline std::shared_ptr<const TrackerParsedSource> projectSourceImpl(std::uint64_t revision,
    const std::vector<Line>& lines,std::uint32_t pid,const std::string& path,
    const std::string& earliest,const std::string& latest,
    const ymcc::sleepsteam::TrackedProcessInventory* lifetime=nullptr,
    const ymcc::sleepsteam::TrackedProcessInventory* running=nullptr,
    TrackerSourceInventories* inventories=nullptr) {
    if(!revision)return {};
    auto parsed=std::make_shared<TrackerParsedSource>();parsed->sourceRevision=revision;
    std::optional<ymcc::sleepsteam::TrackedSourceScopes> scanned;
    std::array<ymcc::sleepsteam::Binding,2> bindings;
    if(!lifetime&&!running)scanned.emplace(ymcc::sleepsteam::trackedSourceScopesImpl(lines,pid,path,earliest,latest));
    else if constexpr(std::is_same_v<Line,std::string>)bindings=ymcc::sleepsteam::trackScopes(lines,pid,path,earliest,latest);
    else return {};
    const auto project=[&](ymcc::sleepsteam::TrackingScope scope,TrackerLogScope declared){
        TrackerScopeProjection out;out.inventory.sourceRevision=out.binding.sourceRevision=revision;
        out.inventory.scope=out.binding.scope=declared;
        const auto* supplied=scope==ymcc::sleepsteam::TrackingScope::ProcessLifetime?lifetime:running;
        ymcc::sleepsteam::TrackedProcessInventory generated;
        if constexpr(std::is_same_v<Line,std::string>)
            if(!supplied&&!scanned)generated=ymcc::sleepsteam::trackedProcessInventory(lines,scope);
        const auto& inventory=supplied?*supplied:scanned?
            scanned->inventories[scope==ymcc::sleepsteam::TrackingScope::ProcessLifetime?1:0]:generated;
        out.inventory.complete=inventory.complete;out.inventory.reason=inventory.reason;
        for(const auto& row:inventory.rows){
            TrackerInventoryMember member{row.appid,row.pid};
            member.gameIdRaw=row.gameIdRaw;member.fullGameId=row.fullGameId;
            member.fullGameIdKnown=row.fullGameIdKnown;member.path=row.path;member.at=row.at;
            member.sourceLine=row.sourceLine;member.identityConflict=row.identityConflict;
            if(member.fullGameIdKnown)member.gameIdKind=groupGameIdKind(member.appid,member.fullGameId);
            out.inventory.members.push_back(std::move(member));
        }
        const auto& binding=scanned?
            scanned->bindings[scope==ymcc::sleepsteam::TrackingScope::ProcessLifetime?1:0]:
            bindings[scope==ymcc::sleepsteam::TrackingScope::ProcessLifetime?1:0];
        out.binding.status=binding.status;out.binding.at=binding.at;out.binding.appid=binding.appid;
        out.binding.sourceLine=binding.line;out.binding.exactPath=binding.exactPath;
        if(binding.status=="tracked"&&binding.exactPath&&binding.line&&binding.line<=lines.size()){
            const auto& row=lines[binding.line-1];const auto begin=row.find("AppID ");
            const auto end=begin==std::string::npos?std::string::npos:row.find(' ',begin+6);
            if(end!=std::string::npos){
                out.binding.gameIdRaw=row.substr(begin+6,end-begin-6);std::uint64_t full=0;
                if(ymcc::sleepsteam::unsignedDecimal(out.binding.gameIdRaw,full)&&exactGameId(binding.appid,full)){
                    out.binding.fullGameId=full;out.binding.fullGameIdKnown=true;
                }
            }
        }
        out.group=projectGroup(out.inventory,out.binding,pid,path);
        return out;
    };
    parsed->processLifetime=project(ymcc::sleepsteam::TrackingScope::ProcessLifetime,TrackerLogScope::ProcessLifetime);
    parsed->runningList=project(ymcc::sleepsteam::TrackingScope::RunningListAndProcess,TrackerLogScope::RunningListAndProcess);
    if(inventories&&scanned)*inventories=std::move(scanned->inventories);
    return parsed;
}
inline std::shared_ptr<const TrackerParsedSource> projectSource(std::uint64_t revision,
    const std::vector<std::string>& lines,std::uint32_t pid,const std::string& path,
    const std::string& earliest,const std::string& latest,
    const ymcc::sleepsteam::TrackedProcessInventory* lifetime=nullptr,
    const ymcc::sleepsteam::TrackedProcessInventory* running=nullptr) {
    return projectSourceImpl(revision,lines,pid,path,earliest,latest,lifetime,running);
}
// Match getline: keep interior empty rows and CR bytes, with no extra row at EOF.
template <typename Line=std::string_view>
inline std::vector<Line> sourceLines(std::string_view bytes) {
    std::vector<Line> lines;
    while(!bytes.empty()){
        const auto end=bytes.find('\n');
        lines.emplace_back(bytes.substr(0,end));
        if(end==std::string_view::npos)break;
        bytes.remove_prefix(end+1);
    }
    return lines;
}
struct FactSlot;
struct TrackerFlight final {
    const TrackerObservationKey key;
    const std::weak_ptr<FactSlot> slot;
    const std::uint64_t version, sequence;
    TrackerFlightReceipt receipt;
    bool running = false, publishable = true, committed = false;
    bool physicallyReleased = false; // Original mutex; no source/cache authority.
    const bool freshSource;
    TrackerFlight(TrackerObservationKey identity, const std::shared_ptr<FactSlot>& owner,
        std::uint64_t epoch, std::uint64_t serial, bool fresh = false) : key(std::move(identity)), slot(owner), version(epoch), sequence(serial), freshSource(fresh) {
        receipt.factVersion = version; receipt.flightSequence = sequence;
    }
};
struct FactSlot final {
    const TrackerObservationKey key;
    std::uint64_t version = 0;
    std::shared_ptr<const TrackerSnapshot> cached;
    // Success aliases cached; an actual negative replaces it and clears cached.
    // Thus each slot retains at most one original <=2MiB source byte buffer.
    std::shared_ptr<const TrackerSnapshot> lastObservation;
    std::shared_ptr<TrackerFlight> active;
    TrackerFlightReceipt lastSource;
    explicit FactSlot(TrackerObservationKey identity) : key(std::move(identity)) {}
};
struct RequestState final {
    const TrackerObservationKey key;
    const std::weak_ptr<FactSlot> slot;
    const TrackerRequestContext context;
    const std::uint64_t id;
    TrackerFlightReceipt receipt;
    std::weak_ptr<TrackerFlight> flight;
    bool claimed = false, closed = false;
    RequestState(TrackerObservationKey identity, const std::shared_ptr<FactSlot>& owner,
        TrackerRequestContext caller, std::uint64_t serial)
        : key(std::move(identity)), slot(owner), context(std::move(caller)), id(serial) { receipt.demandId = id; }
};
}
// Opaque request identity: only TrackerOwner constructs it. Numeric IDs never
// enter a public production API. Destruction/cancel affects THIS request only.
class TrackerDemand final {
    friend class TrackerOwner;
    TrackerOwner* owner_ = nullptr;
    std::shared_ptr<tracker_detail::RequestState> request_;
    TrackerDemand(TrackerOwner* owner, std::shared_ptr<tracker_detail::RequestState> request)
        : owner_(owner), request_(std::move(request)) {}
    void release() noexcept;
public:
    TrackerDemand() = default;
    TrackerDemand(const TrackerDemand&) = delete;
    TrackerDemand& operator=(const TrackerDemand&) = delete;
    TrackerDemand(TrackerDemand&& other) noexcept
        : owner_(std::exchange(other.owner_,nullptr)), request_(std::move(other.request_)) {}
    TrackerDemand& operator=(TrackerDemand&& other) noexcept {
        if (this != &other) { release(); owner_ = std::exchange(other.owner_,nullptr); request_ = std::move(other.request_); }
        return *this;
    }
    ~TrackerDemand() { release(); }
    explicit operator bool() const noexcept { return owner_ && static_cast<bool>(request_); }
    std::uint64_t id() const noexcept { return request_ ? request_->id : 0; }
    TrackerObservationKey factKey() const noexcept { return request_ ? request_->key : nullptr; }
};
class TrackerFlightToken final {
    friend class TrackerOwner;
    TrackerOwner* owner_ = nullptr;
    std::shared_ptr<tracker_detail::TrackerFlight> flight_;
    TrackerFlightToken(TrackerOwner* owner, std::shared_ptr<tracker_detail::TrackerFlight> flight)
        : owner_(owner), flight_(std::move(flight)) {}
    void release() noexcept;
public:
    TrackerFlightToken() = default;
    TrackerFlightToken(const TrackerFlightToken&) = delete;
    TrackerFlightToken& operator=(const TrackerFlightToken&) = delete;
    TrackerFlightToken(TrackerFlightToken&& other) noexcept
        : owner_(std::exchange(other.owner_,nullptr)), flight_(std::move(other.flight_)) {}
    TrackerFlightToken& operator=(TrackerFlightToken&& other) noexcept {
        if (this != &other) { release(); owner_ = std::exchange(other.owner_,nullptr); flight_ = std::move(other.flight_); }
        return *this;
    }
    ~TrackerFlightToken() { release(); }
    explicit operator bool() const noexcept { return owner_ && static_cast<bool>(flight_); }
    TrackerObservationKey factKey() const noexcept { return flight_ ? flight_->key : nullptr; }
    std::uint64_t sequence() const noexcept { return flight_ ? flight_->sequence : 0; }
};
enum class TrackerDemandStatus { Issued, InvalidContext, OwnerChanged, Capacity, SequenceExhausted };
struct TrackerDemandIssue {
    TrackerDemandStatus status;
    TrackerDemand demand;
    explicit TrackerDemandIssue(TrackerDemandStatus value, TrackerDemand ticket = {})
        : status(value), demand(std::move(ticket)) {}
};
enum class TrackerClaimStatus { Claimed, Pending, FreshSourceBusy, HistoricalAvailable, InvalidDemand, OwnerChanged, AlreadyClaimed, SequenceExhausted };
struct TrackerClaim {
    TrackerClaimStatus status;
    TrackerFlightReceipt receipt;
    TrackerDemand request; // Keep with the worker/subscriber until completion.
    TrackerFlightToken token; // Only the SINGLE executor receives this.
    TrackerCompletionWitness completion; // Original busy/claimed flight, never caller IDs.
    TrackerClaim(TrackerClaimStatus state, TrackerFlightReceipt metadata = {}, TrackerDemand ticket = {}, TrackerFlightToken work = {},
        TrackerCompletionWitness completedFlight = {})
        : status(state), receipt(metadata), request(std::move(ticket)), token(std::move(work)), completion(std::move(completedFlight)) {}
};
enum class TrackerRequestCloseReason { Cancelled, QueueRejected, ScopeChanged, Expired };
enum class TrackerExecutionCloseReason { QueueRejected, WorkerExpired };

class TrackerOwner final {
    friend TrackerOwner& trackerOwner() noexcept;
    friend class MainTrackerFactsAdapter;
    friend class TrackerDemand;
    friend class TrackerFlightToken;
    using Slot = tracker_detail::FactSlot;
    using Flight = tracker_detail::TrackerFlight;
    using Request = tracker_detail::RequestState;
    std::mutex mutex_;
    std::array<std::shared_ptr<Slot>,8> facts_{};
    std::array<std::weak_ptr<Request>,32> requests_{};
    std::array<std::weak_ptr<tracker_detail::CompletionWatch>,32> completionWatches_{};
    std::uint64_t nextRequest_ = 0, nextFlight_ = 0, revision_ = 0, nextSourceRead_ = 0;
#include "steam_historical_startup_handoff_owner.inc"
#include "steam_startup_handoff_owner.inc"
    TrackerOwner() = default;
    void stamp(TrackerFlightReceipt& receipt) noexcept {
        if (revision_ != (std::numeric_limits<std::uint64_t>::max)()) ++revision_;
        receipt.revision = revision_;
    }
    std::shared_ptr<Slot> slotFor(const TrackerObservationKey& key) const noexcept {
        for (const auto& slot : facts_) if (slot && slot->key == key) return slot;
        return {};
    }
    bool hasRequests(const std::shared_ptr<Slot>& slot, const std::shared_ptr<Flight>& flight = {}) const {
        for (const auto& weak : requests_) if (const auto request = weak.lock())
            if (!request->closed && !request->receipt.terminal() && request->slot.lock() == slot &&
                (!flight || request->flight.lock() == flight)) return true;
        return false;
    }
    // Only the common adapter calls this. Intern canonical tuples; never replace
    // a referenced A by B. Idle unreferenced cache entries alone are recyclable.
    TrackerObservationKey adoptFact(TrackerObservationKey key) {
        std::lock_guard lock(mutex_);
        for (const auto& slot : facts_) if (slot && slot->key->facts() == key->facts()) return slot->key;
        for (auto& slot : facts_) if (!slot) {
            slot = std::make_shared<Slot>(std::move(key)); return slot->key;
        }
        for (auto& slot : facts_) {
            if (!slot->active && !hasRequests(slot) && slot->key.use_count() == 1) {
                slot = std::make_shared<Slot>(std::move(key)); return slot->key;
            }
        }
        return {}; // Capacity is Unknown, never selection replacement/no-game.
    }
    void closeRequest(const std::shared_ptr<Request>& request, TrackerRequestCloseReason reason) noexcept {
        try {
            std::lock_guard lock(mutex_);
            if (request->closed) return;
            request->closed = true;
            if (!request->receipt.terminal()) {
                request->receipt.phase = reason == TrackerRequestCloseReason::Expired ? TrackerFlightPhase::Expired :
                    reason == TrackerRequestCloseReason::ScopeChanged ? TrackerFlightPhase::Superseded :
                    reason == TrackerRequestCloseReason::QueueRejected ? TrackerFlightPhase::Failed : TrackerFlightPhase::Cancelled;
                request->receipt.lastStatus = reason == TrackerRequestCloseReason::Expired ? TrackerStatus::DeadlineExpired : TrackerStatus::ContextChanged;
                stamp(request->receipt);
            }
            // NO cache invalidation, flight revocation or other-request change.
        } catch (...) {}
    }
    void updateRequests(const std::shared_ptr<Flight>& flight) {
        for (const auto& weak : requests_) if (const auto request = weak.lock()) {
            if (request->closed || request->flight.lock() != flight) continue;
            request->receipt = flight->receipt; request->receipt.demandId = request->id;
        }
    }
    std::shared_ptr<const TrackerSnapshot> finish(const std::shared_ptr<Flight>& flight, TrackerStatus status,
        TrackerFlightPhase phase, bool sourceNegative,
        const std::shared_ptr<const TrackerSnapshot>& source = {}) {
        std::lock_guard lock(mutex_);
        const auto slot = flight->slot.lock();
        if (!slot || slot->active != flight || slot->version != flight->version) return {};
        if (sourceNegative && (!source || !source->observation.readAttempted ||
            !flight->running || flight->receipt.readsStarted != 1)) return {};
        flight->publishable = false;
        flight->receipt.phase = phase; flight->receipt.lastStatus = status; stamp(flight->receipt);
        std::shared_ptr<const TrackerSnapshot> sealed;
        if (sourceNegative) {
            auto provenance = source->provenance; provenance.flight = flight->receipt;
            sealed = std::make_shared<const TrackerSnapshot>(status,source->key,source->observation,
                provenance,false,0,0,false,0,std::string{},source->earliestLocalSecond,source->latestLocalSecond,source->parsedSource,source->groupObservation);
            slot->cached.reset(); slot->lastObservation = sealed; slot->lastSource = flight->receipt;
        }
        updateRequests(flight);
        return sealed;
    }
    // Sink contract: bounded queue/PostMessage ONLY. The caller may still hold
    // refreshMx or other consumer locks even though mutex_ is released here.
    static bool offerCompletion(const std::shared_ptr<tracker_detail::CompletionWatch>& watch) noexcept {
        if (!watch || !watch->released.load(std::memory_order_acquire) || watch->cancelled.load(std::memory_order_acquire)) return false;
        auto expected = watch->delivery.load(std::memory_order_acquire);
        if (expected != TrackerCompletionDelivery::Ready && expected != TrackerCompletionDelivery::Rejected &&
            expected != TrackerCompletionDelivery::SinkFailed) return false;
        if (!watch->delivery.compare_exchange_strong(expected,TrackerCompletionDelivery::Offering,std::memory_order_acq_rel)) return false;
        if (watch->cancelled.load(std::memory_order_acquire)) {
            watch->delivery.store(TrackerCompletionDelivery::Cancelled,std::memory_order_release); return false;
        }
        try {
            const TrackerCompletionHint hint(watch);
            const bool accepted = watch->sink(hint);
            watch->delivery.store(watch->cancelled.load(std::memory_order_acquire) ? TrackerCompletionDelivery::Cancelled :
                accepted ? TrackerCompletionDelivery::Enqueued : TrackerCompletionDelivery::Rejected,std::memory_order_release);
            return accepted;
        } catch (...) {
            watch->delivery.store(watch->cancelled.load(std::memory_order_acquire) ? TrackerCompletionDelivery::Cancelled :
                TrackerCompletionDelivery::SinkFailed,std::memory_order_release); return false;
        }
    }
    void releaseFlight(const std::shared_ptr<Flight>& flight) noexcept {
        // Retain every promoted weak subscription until AFTER mutex_ is gone:
        // even a nonmatching last-reference sink destructor must be lock-free.
        std::array<std::shared_ptr<tracker_detail::CompletionWatch>,32> retained{};
        std::array<bool,32> matching{};
        try {
            {
                std::lock_guard lock(mutex_);
                const auto slot = flight->slot.lock();
                if (!slot || slot->active != flight || flight->physicallyReleased) return;
                if (!flight->receipt.terminal()) {
                    flight->receipt.phase = TrackerFlightPhase::Cancelled;
                    flight->receipt.lastStatus = TrackerStatus::NoExecutionToken; stamp(flight->receipt);
                    updateRequests(flight);
                }
                slot->active.reset(); // Console/other borrowed work has returned.
                flight->physicallyReleased = true;
                for (std::size_t i=0;i<completionWatches_.size();++i) {
                    retained[i]=completionWatches_[i].lock();
                    const auto& watch=retained[i];
                    if (!watch || watch->cancelled.load(std::memory_order_acquire) || watch->key!=flight->key ||
                        watch->sequence!=flight->sequence || watch->factVersion!=flight->version) continue;
                    watch->receipt=flight->receipt; // Preserves failure/cancel/no-read, never a success synthesis.
                    watch->delivery.store(TrackerCompletionDelivery::Ready,std::memory_order_relaxed);
                    watch->released.store(true,std::memory_order_release);
                    matching[i]=true;
                }
            }
            for (std::size_t i=0;i<retained.size();++i) if (matching[i]) (void)offerCompletion(retained[i]);
        } catch (...) {}
    }
    TrackerFlightReceipt receipt(const std::shared_ptr<Flight>& flight) {
        std::lock_guard lock(mutex_); return flight ? flight->receipt : TrackerFlightReceipt{};
    }
    bool begin(const std::shared_ptr<Flight>& flight) {
        std::lock_guard lock(mutex_);
        const auto slot = flight->slot.lock();
        if (!slot || slot->active != flight || slot->version != flight->version || flight->running ||
            !flight->publishable || !hasRequests(slot,flight)) return false;
        flight->running = true; flight->receipt.phase = TrackerFlightPhase::Reading;
        stamp(flight->receipt); updateRequests(flight); return true;
    }
    bool admitRead(const std::shared_ptr<Flight>& flight) {
        std::lock_guard lock(mutex_);
        const auto slot = flight->slot.lock();
        if (!slot || slot->active != flight || slot->version != flight->version || !flight->running ||
            !flight->publishable || flight->receipt.readsStarted || !hasRequests(slot,flight)) return false;
        flight->receipt.readsStarted = 1; stamp(flight->receipt); updateRequests(flight); return true;
    }
    static bool contextCurrent(const std::shared_ptr<ProcessLease>& lease, const TrackerObservationKey& key) noexcept {
        try {
            if (!lease || !key) return false;
            const auto& fact = key->facts(); const auto& selected = lease->selectedGameWitness();
            return selected.pid == fact.selectedGame.pid && selected.created == fact.selectedGame.birth &&
                ymcc::sleepsteam::pathKey(mainReadTailPort().selectedPathUtf8(selected.image)) == fact.canonicalGamePath &&
                lease->steamWitness().pid == fact.steam.pid && lease->steamWitness().created == fact.steam.birth &&
                ymcc::sleepsteam::pathKey(mainReadTailPort().selectedPathUtf8(lease->steamRoot())) == fact.canonicalSteamRoot &&
                lease->windowsSession() == fact.steam.windowsSession && lease->processEpoch() == fact.steam.processEpoch &&
                lease->accountEpoch() == fact.steam.accountEpoch && lease->current() &&
                MainTrackerFactsAdapter::current(*key) && lease->current();
        } catch (...) { return false; }
    }
    static TrackerKey makeKey(const TrackerObservationKey& fact, const std::shared_ptr<ProcessLease>& lease) {
        TrackerKey key; key.observation = fact->facts();
        key.sourceKey = fact->facts().steam.root + L"\\logs\\gameprocess_log.txt";
        if (lease) {
            key.observedGame = tracker_detail::processIdentity(lease->gameWitness());
            key.heldSteam = tracker_detail::processIdentity(lease->steamWitness());
        }
        return key;
    }
    std::shared_ptr<const TrackerSnapshot> commit(const std::shared_ptr<Flight>& flight,
        const std::shared_ptr<const TrackerSnapshot>& snapshot) {
        std::lock_guard lock(mutex_);
        const auto slot = flight->slot.lock();
        if (!slot || slot->active != flight || slot->version != flight->version || !flight->publishable ||
            !flight->running || flight->receipt.readsStarted != 1) return {};
        flight->receipt.phase = TrackerFlightPhase::Succeeded; flight->receipt.lastStatus = snapshot->status;
        stamp(flight->receipt);
        auto provenance = snapshot->provenance; provenance.flight = flight->receipt;
        auto sealed = std::make_shared<const TrackerSnapshot>(snapshot->status,snapshot->key,snapshot->observation,
            provenance,snapshot->processValidated,snapshot->trackerAppId,snapshot->fullGameId,snapshot->exactPath,
            snapshot->sourceLine,snapshot->trackerAtLocalSecond,snapshot->earliestLocalSecond,snapshot->latestLocalSecond,snapshot->parsedSource,snapshot->groupObservation);
        slot->cached = sealed; slot->lastObservation = sealed; slot->lastSource = flight->receipt;
        flight->committed = true; updateRequests(flight); return sealed;
    }
    std::shared_ptr<const ProcessGroupCapture> captureProcessGroup(
        const std::shared_ptr<ProcessLease>& selected,const TrackerGroupObservation& group,
        const ymcc::sleepsteam::NativePrimaryProcessInventory& primary,std::uint64_t consoleRevision,
        const TrackerOwnerCurrent& current){
        if(!tracker_detail::ownerCurrent(current)||!selected||!selected->processCurrent()||
            !primary.complete||!consoleRevision||group.status!=TrackerGroupStatus::Observed||
            !group.fullGameIdKnown||group.selectedPid!=selected->gameWitness().pid)return {};
        const auto native=primary.rows.find(group.appid);
        if(native==primary.rows.end()||native->second.removed||!native->second.local)return {};
        ProcessGroupSeed seed;seed.appid=group.appid;seed.selectedPid=group.selectedPid;seed.fullGameId=group.fullGameId;
        seed.sourceRevision=group.sourceRevision;seed.consoleRevision=consoleRevision;
        for(const auto& member:group.members)seed.members.push_back({member.pid,member.path,member.at,member.sourceLine});
        auto captured=processOwner().acquireGroup(selected,seed,native->second.pid);
        if(!tracker_detail::ownerCurrent(current)||!selected->processCurrent()){
            if(captured.lease)captured.lease->cancel();captured.lease.reset();
            captured.rejection=ProcessGroupRejection::ContextChanged;
        }else if(captured.lease){
            const auto roles=captured.lease->inspectNativeRoles(native->second.pid,group.selectedPid);
            if(!roles||(roles->state!=ymcc::steamgameroles::State::ExactSelected&&
                roles->state!=ymcc::steamgameroles::State::VerifiedGroupRoles)){
                captured.lease->cancel();captured.lease.reset();captured.rejection=ProcessGroupRejection::InvalidTopology;
            }
        }
        return std::make_shared<const ProcessGroupCapture>(std::move(captured));
    }
public:
    // One Steam source observation. No game selection, Ready-only game scope,
    // cache, retry or new worker belongs here. Existing game leases are reused.
    std::shared_ptr<const SteamLogSourceView> readLogSource(const SteamLogSourceIdentity& key,SteamLogKind kind,
        SteamLogReadPolicy policy,const SteamLogReadAdmission& admit,const std::shared_ptr<ProcessLease>& reuse={}){
        auto view=std::make_shared<SteamLogSourceView>();view->kind=kind;view->policy=policy;
        auto& observation=view->observation;observation.availableKnown=true;observation.limitBytesKnown=true;observation.limitBytes=steamLogMaximumBytes(kind);
        using Reason=TrackerSourceObservation::ReadReason;
        const auto allowed=[&](){try{return admit&&admit();}catch(...){return false;}};
        const auto refuse=[&](Reason reason){observation.readReason=reason;return std::shared_ptr<const SteamLogSourceView>(view);};
        if((kind!=SteamLogKind::GameProcess&&kind!=SteamLogKind::Console)||
            (policy!=SteamLogReadPolicy::Raw&&policy!=SteamLogReadPolicy::CompleteOnly&&policy!=SteamLogReadPolicy::TruncatedDiagnostic))return refuse(Reason::InvalidRequest);
        if(!key||!MainTrackerFactsAdapter::sourceCurrent(*key))return refuse(Reason::IdentityUnavailable);
        view->steam=key->facts();if(!allowed())return refuse(Reason::ContextChanged);
        std::optional<detail::PinnedProcess> pinned;
        if(reuse){
            if(reuse->steamWitness().pid!=view->steam.pid||reuse->steamWitness().created!=view->steam.birth||
                !samePath(reuse->steamRoot(),view->steam.root)||reuse->windowsSession()!=view->steam.windowsSession||!reuse->current())return refuse(Reason::IdentityUnavailable);
        }else{
            pinned=detail::pin({view->steam.pid,view->steam.birth,view->steam.root+L"\\steam.exe"});
            if(!pinned||pinned->windowsSession!=view->steam.windowsSession)return refuse(Reason::IdentityUnavailable);
        }
        const auto held=[&](){return reuse?reuse->current():pinned&&detail::alive(pinned->handle.value);};
        if(!held()||!MainTrackerFactsAdapter::sourceCurrent(*key)||!allowed())return refuse(Reason::ContextChanged);
        {std::lock_guard lock(mutex_);if(nextSourceRead_==(std::numeric_limits<std::uint64_t>::max)())return refuse(Reason::SequenceExhausted);observation.sourceRevision=++nextSourceRead_;}
        observation.readAttempted=true;observation.startedAtTick=mainReadTailPort().nowTick();TrackerTailRead tail;
        try{tail=mainReadTailPort().readTail(view->steam.root+(kind==SteamLogKind::GameProcess?L"\\logs\\gameprocess_log.txt":L"\\logs\\console_log.txt"),kind);}
        catch(...){observation.finishedAtTick=mainReadTailPort().nowTick();return refuse(Reason::ReadException);}
        observation.finishedAtTick=mainReadTailPort().nowTick();
        observation.availableKnown=tail.availableKnown;observation.available=tail.available;observation.truncatedKnown=tail.truncatedKnown;observation.truncated=tail.truncated;
        observation.originalBytesKnown=tail.originalBytesKnown;observation.originalBytes=tail.originalBytes;observation.copiedBytesKnown=tail.copiedBytesKnown;observation.copiedBytes=tail.copiedBytes;
        observation.limitBytesKnown=tail.limitBytesKnown;observation.limitBytes=tail.limitBytes;observation.returnedBytes=tail.bytes.size();observation.readReason=tail.reason;
        observation.win32ErrorKnown=tail.win32ErrorKnown;observation.win32Error=tail.win32Error;
        if(tail.bytes.size()<=steamLogMaximumBytes(kind))observation.bytes=std::make_shared<const std::string>(std::move(tail.bytes));
        if(!held()||!MainTrackerFactsAdapter::sourceCurrent(*key)||!allowed())return refuse(Reason::ContextChanged);
        observation.metadataComplete=observation.availableKnown&&observation.available&&observation.truncatedKnown&&!observation.truncated&&observation.bytes&&
            observation.originalBytesKnown&&observation.copiedBytesKnown&&observation.limitBytesKnown&&observation.limitBytes==steamLogMaximumBytes(kind)&&
            observation.originalBytes==observation.copiedBytes&&observation.copiedBytes==observation.returnedBytes;
        view->complete=observation.metadataComplete;
        view->diagnosticOnly=kind==SteamLogKind::GameProcess&&policy==SteamLogReadPolicy::TruncatedDiagnostic&&observation.availableKnown&&observation.available&&observation.truncatedKnown&&observation.truncated&&observation.bytes;
        if(kind==SteamLogKind::GameProcess&&policy!=SteamLogReadPolicy::Raw&&(view->complete||view->diagnosticOnly)){
            std::string_view bytes=*observation.bytes;
            if(view->diagnosticOnly){
                const auto end=bytes.find('\n');
                bytes.remove_prefix(end==std::string_view::npos?bytes.size():end+1);
            }
            view->lines=tracker_detail::sourceLines<std::string>(bytes);
            auto inventories=ymcc::sleepsteam::trackedProcessInventories(view->lines);
            view->runningInventory=std::move(inventories[0]);
            view->lifetimeInventory=std::move(inventories[1]);
        }
        return std::shared_ptr<const SteamLogSourceView>(view);
    }
    TrackerOwner(const TrackerOwner&) = delete;
    TrackerOwner& operator=(const TrackerOwner&) = delete;
    // Explicit group demand over the same sole source reader. This API never
    // elects a game, adds a worker/timer, reads another account or grants a
    // mutation. Native observation may use its original non-Ready read scope;
    // every caller still owns its exact game/rules/power/operation admission.
    SteamProcessGroupObservation observeProcessGroup(const SteamLogSourceIdentity& key,
        const std::shared_ptr<ProcessLease>& selected,const TrackerOwnerCurrent& admit){
        SteamProcessGroupObservation out;
        const auto current=[&](){return tracker_detail::ownerCurrent(admit)&&selected&&
            selected->processCurrent()&&key&&MainTrackerFactsAdapter::sourceCurrent(*key);};
        if(!current())return out;
        const auto& receipt=key->facts();
        if(selected->steamWitness().pid!=receipt.pid||selected->steamWitness().created!=receipt.birth||
            !samePath(selected->steamRoot(),receipt.root)||selected->windowsSession()!=receipt.windowsSession)return out;
        out.gameSource_=readLogSource(key,SteamLogKind::GameProcess,SteamLogReadPolicy::CompleteOnly,current,selected);
        if(!current()||!out.gameSource_||!out.gameSource_->complete||out.gameSource_->diagnosticOnly)return out;
        const auto& game=selected->selectedGameWitness();
        const auto earliest=mainReadTailPort().localSecond(game.created>20000000ULL?game.created-20000000ULL:0);
        const auto latest=mainReadTailPort().localSecond(game.created<=(std::numeric_limits<std::uint64_t>::max)()-30000000ULL?game.created+30000000ULL:game.created);
        out.parsed_=tracker_detail::projectSource(out.gameSource_->observation.sourceRevision,
            out.gameSource_->lines,game.pid,mainReadTailPort().selectedPathUtf8(game.image),earliest,latest,
            &out.gameSource_->lifetimeInventory,&out.gameSource_->runningInventory);
        if(!current()||!out.parsed_)return out;
        out.consoleSource_=readLogSource(key,SteamLogKind::Console,SteamLogReadPolicy::Raw,current,selected);
        if(!current()||!out.consoleSource_||!out.consoleSource_->complete||!out.consoleSource_->observation.bytes)return out;
        // Only this actual source owner chooses the current read's bound.
        // A historical capture deadline cannot hide a newer removal/update.
        out.boundary_=mainReadTailPort().nowFileTime();
        if(!out.boundary_||out.boundary_<receipt.birth)return out;
        auto primary=std::make_shared<ymcc::sleepsteam::NativePrimaryProcessInventory>(
            ymcc::sleepsteam::nativePrimaryProcessInventoryText(*out.consoleSource_->observation.bytes,
                mainReadTailPort().localSecond(receipt.birth),mainReadTailPort().localSecond(out.boundary_)));
        out.primary_=primary;
        if(!current()||!primary->complete)return out;
        out.sourcesComplete_=true;
        out.capture_=captureProcessGroup(selected,out.parsed_->processLifetime.group,*primary,
            out.consoleSource_->observation.sourceRevision,current);
        if(!current()){
            ProcessGroupCapture cancelled;cancelled.rejection=ProcessGroupRejection::ContextChanged;
            if(out.capture_&&out.capture_->lease)out.capture_->lease->cancel();
            out.capture_=std::make_shared<const ProcessGroupCapture>(std::move(cancelled));out.sourcesComplete_=false;
        }
        return out;
    }
    static constexpr std::size_t factCapacity = 8, requestCapacity = 32;
    static constexpr std::size_t completionCapacity = 32;
    // Atomic registration against the exact opaque flight returned by claim.
    // ReReadNow is a result, never an inline callback or an implicit refresh.
    TrackerCompletionIssue watchCompletion(const TrackerCompletionWitness& witness,TrackerCompletionSink sink) {
        if (!witness || witness.owner_!=this || !sink) return TrackerCompletionIssue(TrackerCompletionWatchStatus::InvalidWitness);
        std::shared_ptr<Flight> heldFlight;
        std::shared_ptr<tracker_detail::CompletionWatch> watch;
        {
            std::lock_guard lock(mutex_);
            const auto slot=slotFor(witness.key_);
            if (!slot) return TrackerCompletionIssue(TrackerCompletionWatchStatus::ReReadNow);
            heldFlight=witness.flight_.lock();
            if (!heldFlight || heldFlight->physicallyReleased || slot->active!=heldFlight ||
                heldFlight->key!=witness.key_ || heldFlight->sequence!=witness.sequence_ || heldFlight->version!=witness.factVersion_)
                return TrackerCompletionIssue(TrackerCompletionWatchStatus::ReReadNow);
            for (auto& weak:completionWatches_) if (weak.expired()) {
                watch=std::make_shared<tracker_detail::CompletionWatch>(witness.key_,witness.sequence_,witness.factVersion_,std::move(sink));
                weak=watch; break;
            }
        }
        if (!watch) return TrackerCompletionIssue(TrackerCompletionWatchStatus::Capacity);
        return TrackerCompletionIssue(TrackerCompletionWatchStatus::Watching,TrackerCompletionSubscription(std::move(watch)));
    }
    // Pure get observers can watch the active canonical flight without a demand.
    // Completion between capture and registration returns ReReadNow, not a lost event.
    TrackerCompletionIssue watchCompletion(const TrackerObservationKey& key,TrackerCompletionSink sink) {
        if (!key || !sink) return TrackerCompletionIssue(TrackerCompletionWatchStatus::InvalidWitness);
        TrackerCompletionWitness witness;
        {
            std::lock_guard lock(mutex_);
            const auto slot=slotFor(key);
            if (!slot || !slot->active) return TrackerCompletionIssue(TrackerCompletionWatchStatus::ReReadNow);
            const auto& flight=slot->active;
            witness=TrackerCompletionWitness(this,flight->key,flight,flight->sequence,flight->version);
        }
        return watchCompletion(witness,std::move(sink));
    }
    // Explicit existing-event retry only; never schedules itself or source IO.
    bool retryCompletion(const TrackerCompletionSubscription& subscription) noexcept {
        return offerCompletion(subscription.watch_);
    }
    // MEMORY ONLY, including the caller's pure memory guard. No actionCurrent,
    // GetForegroundWindow, ProcessLease, Reader, clock, scheduling or IDs.
    TrackerPeekResult peek(const TrackerObservationKey& key, const TrackerOwnerCurrent& pureOwnerCurrent) {
        if (!key) return {key,TrackerPeekUnknown::InvalidKey};
        if (!tracker_detail::ownerCurrent(pureOwnerCurrent) || !MainTrackerFactsAdapter::current(*key))
            return {key,TrackerPeekUnknown::OwnerChanged};
        std::shared_ptr<Slot> slot; std::shared_ptr<const TrackerSnapshot> historical, lastObservation;
        TrackerFlightReceipt progress; TrackerPeekUnknown why = TrackerPeekUnknown::Missing;
        TrackerCompletionWitness completion;
        {
            std::unique_lock lock(mutex_,std::try_to_lock);
            if (!lock.owns_lock()) return {key,TrackerPeekUnknown::OwnerBusy};
            slot = slotFor(key); if (!slot) return {key,TrackerPeekUnknown::InvalidKey};
            historical = slot->cached; lastObservation = slot->lastObservation;
            if (slot->active && !slot->active->receipt.terminal()) {
                why = TrackerPeekUnknown::Pending; progress = slot->active->receipt;
                // Bind the same flight observed by this get. A later
                // watch must never substitute a newer same-key flight.
                const auto& pending = slot->active;
                completion = TrackerCompletionWitness(this,pending->key,pending,pending->sequence,pending->version);
            } else if (lastObservation) {
                why = TrackerPeekUnknown::SourceUnknown; progress = slot->lastSource;
            }
        }
        if (!tracker_detail::ownerCurrent(pureOwnerCurrent) || !MainTrackerFactsAdapter::current(*key))
            return {key,TrackerPeekUnknown::OwnerChanged};
        if (!historical) {
            {
                std::unique_lock lock(mutex_,std::try_to_lock);
                if (!lock.owns_lock()) return {key,TrackerPeekUnknown::OwnerBusy};
                if (slotFor(key) != slot || slot->cached || slot->lastObservation != lastObservation)
                    return {key,TrackerPeekUnknown::CacheChanged};
            }
            if (!tracker_detail::ownerCurrent(pureOwnerCurrent) || !MainTrackerFactsAdapter::current(*key))
                return {key,TrackerPeekUnknown::OwnerChanged};
            // Retain actual negative source diagnostics without handing a cached
            // RunningList projection to this later memory-only request.
            if(lastObservation)lastObservation=std::make_shared<const TrackerSnapshot>(
                lastObservation->status,lastObservation->key,lastObservation->observation,lastObservation->provenance,
                false,lastObservation->trackerAppId,lastObservation->fullGameId,lastObservation->exactPath,
                lastObservation->sourceLine,lastObservation->trackerAtLocalSecond,
                lastObservation->earliestLocalSecond,lastObservation->latestLocalSecond,
                std::shared_ptr<const TrackerParsedSource>{},lastObservation->groupObservation);
            return {key,why,{},progress,std::move(lastObservation),std::move(completion)};
        }
        TrackerCacheProvenance provenance;
        provenance.cacheHit = true; provenance.source = "historical-verified-process-lifetime-binding";
        provenance.sourceCheckedAtTick = historical->provenance.sourceCheckedAtTick;
        provenance.observationSequence = historical->provenance.observationSequence;
        provenance.flight = historical->provenance.flight;
        auto view = std::make_shared<const TrackerSnapshot>(historical->status,historical->key,historical->observation,
            provenance,false,historical->trackerAppId,historical->fullGameId,historical->exactPath,historical->sourceLine,
            historical->trackerAtLocalSecond,historical->earliestLocalSecond,historical->latestLocalSecond,
            std::shared_ptr<const TrackerParsedSource>{},historical->groupObservation);
        {
            std::unique_lock lock(mutex_,std::try_to_lock);
            if (!lock.owns_lock()) return {key,TrackerPeekUnknown::OwnerBusy};
            if (slotFor(key) != slot || slot->cached != historical) return {key,TrackerPeekUnknown::CacheChanged};
        }
        if (!tracker_detail::ownerCurrent(pureOwnerCurrent) || !MainTrackerFactsAdapter::current(*key))
            return {key,TrackerPeekUnknown::OwnerChanged};
        return {key,TrackerPeekUnknown::None,std::move(view)};
    }
    TrackerDemandIssue beginDemand(const TrackerObservationKey& key, TrackerRequestContext context,
        const TrackerOwnerCurrent& pureOwnerCurrent) {
        if (!key || !tracker_detail::validRequest(key->facts(),context)) return TrackerDemandIssue(TrackerDemandStatus::InvalidContext);
        if (!tracker_detail::ownerCurrent(pureOwnerCurrent) || !MainTrackerFactsAdapter::current(*key))
            return TrackerDemandIssue(TrackerDemandStatus::OwnerChanged);
        TrackerDemand demand;
        {
            std::lock_guard lock(mutex_); const auto slot = slotFor(key);
            if (!slot) return TrackerDemandIssue(TrackerDemandStatus::InvalidContext);
            if (nextRequest_ == (std::numeric_limits<std::uint64_t>::max)() || revision_ == (std::numeric_limits<std::uint64_t>::max)())
                return TrackerDemandIssue(TrackerDemandStatus::SequenceExhausted);
            for (auto& weak : requests_) {
                const auto old = weak.lock(); if (old && !old->closed) continue;
                auto request = std::make_shared<Request>(key,slot,std::move(context),++nextRequest_);
                request->receipt.phase = TrackerFlightPhase::Created; stamp(request->receipt);
                weak = request; demand = TrackerDemand(this,std::move(request)); break;
            }
        }
        if (!demand) return TrackerDemandIssue(TrackerDemandStatus::Capacity);
        if (!tracker_detail::ownerCurrent(pureOwnerCurrent)) return TrackerDemandIssue(TrackerDemandStatus::OwnerChanged);
        return TrackerDemandIssue(TrackerDemandStatus::Issued,std::move(demand));
    }
    TrackerClaim claim(TrackerDemand demand, const TrackerOwnerCurrent& pureOwnerCurrent, TrackerReadMode mode) {
        const auto request = demand.owner_ == this ? demand.request_ : nullptr;
        if (!request || (mode != TrackerReadMode::AllowHistoricalBinding && mode != TrackerReadMode::RequireCurrentSource && mode != TrackerReadMode::RequireNewSource))
            return TrackerClaim(TrackerClaimStatus::InvalidDemand,{},std::move(demand));
        if (!tracker_detail::ownerCurrent(pureOwnerCurrent) || !MainTrackerFactsAdapter::current(*request->key)) {
            closeRequest(request,TrackerRequestCloseReason::ScopeChanged);
            const auto progress = requestReceipt(demand);
            return TrackerClaim(TrackerClaimStatus::OwnerChanged,progress,std::move(demand));
        }
        TrackerFlightToken execution; TrackerFlightReceipt progress;
        TrackerCompletionWitness completion;
        TrackerClaimStatus status = TrackerClaimStatus::Pending;
        {
            std::lock_guard lock(mutex_); const auto slot = request->slot.lock();
            if (!slot || request->closed) return TrackerClaim(TrackerClaimStatus::OwnerChanged,request->receipt,std::move(demand));
            if (request->claimed) return TrackerClaim(TrackerClaimStatus::AlreadyClaimed,request->receipt,std::move(demand));
            request->claimed = true;
            if (slot->cached && mode == TrackerReadMode::AllowHistoricalBinding) {
                request->receipt.phase = TrackerFlightPhase::HistoricalAvailable;
                request->receipt.lastStatus = slot->cached->status; stamp(request->receipt);
                return TrackerClaim(TrackerClaimStatus::HistoricalAvailable,request->receipt,std::move(demand));
            }
            auto flight = slot->active;
            // A post-pin/final admission must not attach to an observation
            // that began before this demand. Never wait/retry or steal it.
            if(flight&&mode==TrackerReadMode::RequireNewSource)
                return TrackerClaim(TrackerClaimStatus::FreshSourceBusy,request->receipt,std::move(demand),{},
                    TrackerCompletionWitness(this,flight->key,flight,flight->sequence,flight->version));
            if (!flight) {
                if (nextFlight_ == (std::numeric_limits<std::uint64_t>::max)() || slot->version == (std::numeric_limits<std::uint64_t>::max)() ||
                    revision_ == (std::numeric_limits<std::uint64_t>::max)())
                    return TrackerClaim(TrackerClaimStatus::SequenceExhausted,request->receipt,std::move(demand));
                flight = std::make_shared<Flight>(slot->key,slot,slot->version + 1,nextFlight_ + 1,mode==TrackerReadMode::RequireNewSource);
                ++slot->version; ++nextFlight_; flight->receipt.phase = TrackerFlightPhase::Queued; stamp(flight->receipt);
                slot->active = flight; execution = TrackerFlightToken(this,flight); status = TrackerClaimStatus::Claimed;
            }
            completion=TrackerCompletionWitness(this,flight->key,flight,flight->sequence,flight->version);
            request->flight = flight; request->receipt = flight->receipt; request->receipt.demandId = request->id;
            progress = request->receipt;
        }
        if (!tracker_detail::ownerCurrent(pureOwnerCurrent)) closeRequest(request,TrackerRequestCloseReason::ScopeChanged);
        // A shared executor still belongs to the worker, even if THIS subscriber
        // cancels. observe checks whether any subscriber remains before IO.
        progress = requestReceipt(demand);
        return TrackerClaim(status,progress,std::move(demand),std::move(execution),std::move(completion));
    }
    // MEMORY ONLY pre-acquire gate for the shared worker. No originating
    // consumer callback: any remaining subscriber may keep this flight alive.
    // This observes eligibility; cancellation after return can still race IO.
    bool executionEligible(const TrackerFlightToken& token) noexcept {
        try {
            const auto flight = token.owner_ == this ? token.flight_ : nullptr;
            if (!flight || !flight->key || !MainTrackerFactsAdapter::current(*flight->key)) return false;
            std::lock_guard lock(mutex_);
            const auto slot = flight->slot.lock();
            return slot && slotFor(flight->key) == slot && slot->active == flight &&
                slot->version == flight->version && flight->publishable && !flight->running &&
                !flight->receipt.readsStarted && !flight->receipt.terminal() &&
                hasRequests(slot,flight) && MainTrackerFactsAdapter::current(*flight->key);
        } catch (...) { return false; }
    }
    TrackerFlightReceipt requestReceipt(const TrackerDemand& demand) {
        std::unique_lock lock(mutex_,std::try_to_lock);
        return lock.owns_lock() && demand.owner_ == this && demand.request_ ? demand.request_->receipt : TrackerFlightReceipt{};
    }
    void close(TrackerDemand demand, TrackerRequestCloseReason reason) noexcept {
        if (demand.owner_ == this && demand.request_) closeRequest(demand.request_,reason);
    }
    void cancel(TrackerDemand demand) noexcept { close(std::move(demand),TrackerRequestCloseReason::Cancelled); }
    // This is worker/job ownership, not menu OFF. It never deletes old facts.
    void releaseExecution(TrackerFlightToken token, TrackerExecutionCloseReason reason) noexcept {
        try {
            if (token.owner_ == this && token.flight_)
                finish(token.flight_,reason == TrackerExecutionCloseReason::WorkerExpired ? TrackerStatus::DeadlineExpired : TrackerStatus::NoExecutionToken,
                    reason == TrackerExecutionCloseReason::WorkerExpired ? TrackerFlightPhase::Expired : TrackerFlightPhase::Failed,false);
        } catch (...) {}
    }
private:
    // Borrow the token so a fresh two-source consumer retains its physical
    // slot until the console observation and final held-context check return.
    std::shared_ptr<const TrackerSnapshot> observeImpl(TrackerFlightToken& token, const std::shared_ptr<ProcessLease>& lease,
        tracker_detail::FreshStartupProjection* startup=nullptr) {
        const auto flight = token.owner_ == this ? token.flight_ : nullptr;
        TrackerKey key; if (flight) key.observation = flight->key->facts();
        TrackerSourceObservation observation; std::string earliest,latest;
        std::shared_ptr<const TrackerParsedSource> parsedSource;
        std::shared_ptr<const TrackerGroupObservation> groupObservation;
        const auto unknown = [&](TrackerStatus status) {
            TrackerCacheProvenance provenance; provenance.flight = receipt(flight);
            provenance.sourceReadThisFlight = observation.readAttempted;
            provenance.checkedAtTick = provenance.sourceCheckedAtTick = observation.finishedAtTick;
            provenance.observationSequence = flight ? flight->sequence : 0;
            if (observation.readAttempted) provenance.source = "bounded-gameprocess-log-unknown";
            return std::make_shared<const TrackerSnapshot>(status,key,observation,provenance,false,
                0,0,false,0,std::string{},earliest,latest,parsedSource,groupObservation);
        };
        const auto refuse = [&](TrackerStatus status, bool sourceNegative = false) {
            if (sourceNegative && (!flight || !contextCurrent(lease,flight->key))) {
                sourceNegative = false; status = TrackerStatus::ContextChanged;
            }
            if (flight) {
                const auto source = sourceNegative ? unknown(status) : nullptr;
                const auto sealed = finish(flight,status,status == TrackerStatus::ContextChanged || status == TrackerStatus::Superseded ?
                    TrackerFlightPhase::Superseded : TrackerFlightPhase::Failed,sourceNegative,source);
                if (sealed) return sealed;
            }
            return unknown(status);
        };
        if (!flight) return unknown(TrackerStatus::NoExecutionToken);
        if (!begin(flight)) return refuse(TrackerStatus::Superseded);
        try {
            key = makeKey(flight->key,lease);
            if (!contextCurrent(lease,flight->key)) return refuse(TrackerStatus::ContextChanged);
            if (!admitRead(flight)) return refuse(TrackerStatus::Superseded);
            const auto sourceKey=MainTrackerFactsAdapter::sourceKey({key.observation.steam.pid,key.observation.steam.birth,key.observation.steam.root});
            const auto source=readLogSource(sourceKey,SteamLogKind::GameProcess,SteamLogReadPolicy::Raw,
                [&](){return contextCurrent(lease,flight->key);},lease);
            observation=source->observation;
            if(observation.readReason==TrackerSourceObservation::ReadReason::ReadException)return refuse(TrackerStatus::SourceReadException,true);
            if (!contextCurrent(lease,flight->key)) return refuse(TrackerStatus::ContextChanged);

            if (!observation.availableKnown || !observation.available) return refuse(TrackerStatus::SourceUnavailable,true);
            if (!observation.truncatedKnown) return refuse(TrackerStatus::SourceIncomplete,true);
            if (observation.truncated) return refuse(TrackerStatus::SourceTruncated,true);
            observation.metadataComplete = observation.bytes && observation.originalBytesKnown &&
                observation.copiedBytesKnown && observation.limitBytesKnown &&
                observation.limitBytes == ymcc::sleepsteam::trackerMaximumBytes &&
                observation.originalBytes == observation.copiedBytes && observation.copiedBytes == observation.returnedBytes;
            if (!observation.metadataComplete) return refuse(TrackerStatus::SourceIncomplete,true);
            // Exact old tracked() birth window and helper; no new time/path parser.
            const auto& game = key.observation.selectedGame;
            earliest = mainReadTailPort().localSecond(game.birth > 20000000ULL ? game.birth - 20000000ULL : 0);
            latest = mainReadTailPort().localSecond(game.birth <= (std::numeric_limits<std::uint64_t>::max)() - 30000000ULL ? game.birth + 30000000ULL : game.birth);
            auto lines=tracker_detail::sourceLines(*observation.bytes);
            tracker_detail::TrackerSourceInventories inventories;
            parsedSource=tracker_detail::projectSourceImpl(observation.sourceRevision,lines,game.pid,
                mainReadTailPort().selectedPathUtf8(game.exactPath),earliest,latest,nullptr,nullptr,startup?&inventories:nullptr);
            if(!parsedSource)return refuse(TrackerStatus::SourceIncomplete,true);
            groupObservation=std::make_shared<const TrackerGroupObservation>(parsedSource->processLifetime.group);
            const auto& projected=parsedSource->processLifetime.binding;
            const auto binding=ymcc::sleepsteam::Binding{projected.status,projected.at,projected.appid,projected.sourceLine,projected.exactPath};
            if (binding.status != "tracked" || !binding.exactPath || !binding.line || binding.line > lines.size()) {
                const auto status=tracker_detail::parserStatus(binding.status);
                if(startup&&status==TrackerStatus::ParserUnknown)*startup={std::move(lines),std::move(inventories)};
                return refuse(status,true);
            }
            if (!projected.fullGameIdKnown) return refuse(TrackerStatus::FullGameIdUnknown,true);
            const auto full=projected.fullGameId;
            if (!contextCurrent(lease,flight->key)) return refuse(TrackerStatus::ContextChanged);
            TrackerCacheProvenance provenance;
            provenance.sourceReadThisFlight = true; // ProcessOnly is the only supported source route.
            provenance.source = "historical-verified-process-lifetime-binding";
            provenance.checkedAtTick = provenance.sourceCheckedAtTick = mainReadTailPort().nowTick();
            provenance.observationSequence = flight->sequence;
            auto snapshot = std::make_shared<const TrackerSnapshot>(TrackerStatus::CachedHistoricalAssociation,key,observation,provenance,false,
                binding.appid,full,binding.exactPath,binding.line,binding.at,earliest,latest,parsedSource,groupObservation);
            if (!contextCurrent(lease,flight->key)) return refuse(TrackerStatus::ContextChanged);
            const auto sealed = commit(flight,snapshot); if (!sealed) return refuse(TrackerStatus::Superseded);
            if (!contextCurrent(lease,flight->key)) return unknown(TrackerStatus::ContextChanged);
            return sealed;
        } catch (...) { return refuse(TrackerStatus::PreparationException,observation.readAttempted); }
    }
public:
    std::shared_ptr<const TrackerSnapshot> observe(TrackerFlightToken token,std::shared_ptr<ProcessLease> lease){
        return observeImpl(token,lease);
    }
    TrackerFreshObservation observeFresh(TrackerFlightToken token,std::shared_ptr<ProcessLease> lease,const TrackerOwnerCurrent& pureOwnerCurrent,TrackerGroupCapturePolicy groupPolicy=TrackerGroupCapturePolicy::None){
        TrackerFreshObservation result;
        const auto flight=token.owner_==this?token.flight_:nullptr;
        if(!flight||!flight->freshSource||!tracker_detail::ownerCurrent(pureOwnerCurrent)||!contextCurrent(lease,flight->key))return result;
        const auto startupGeneration=startupReadGeneration();
        tracker_detail::FreshStartupProjection startup;
        result.tracker=observeImpl(token,lease,&startup);
        // Producer-only handoff: never place process-object evidence in the
        // original fact cache or a memory peek. Exact-child binding stays intact.
        if(result.tracker&&result.tracker->status==TrackerStatus::ParserUnknown&&
            result.tracker->parsedSource&&result.tracker->observation.metadataComplete&&
            contextCurrent(lease,flight->key)&&tracker_detail::ownerCurrent(pureOwnerCurrent)) {
            const auto key=MainTrackerFactsAdapter::sourceKey({flight->key->facts().steam.pid,flight->key->facts().steam.birth,flight->key->facts().steam.root});
            auto source=std::make_shared<SteamLogSourceView>();source->steam=flight->key->facts().steam;
            source->observation=result.tracker->observation;source->complete=true;source->kind=SteamLogKind::GameProcess;source->policy=SteamLogReadPolicy::CompleteOnly;
            // Same immutable bytes and inventories as the tracker projection.
            // The handoff still owns its strings and rebinds any parent PID.
            source->lines.reserve(startup.lines.size());
            for(const auto line:startup.lines)source->lines.emplace_back(line);
            source->runningInventory=std::move(startup.inventories[0]);source->lifetimeInventory=std::move(startup.inventories[1]);
            const auto current=[&]{return contextCurrent(lease,flight->key)&&tracker_detail::ownerCurrent(pureOwnerCurrent);};
            (void)captureStartupFromSource(key,current,source,startupGeneration);
            result.startupHandoff=startupHandoffFromSource(key,lease,current,source,startupGeneration);
            result.historicalStartupHandoff=historicalStartupHandoffFromSource(key,lease,current,source,startupGeneration);
            if(result.startupHandoff) {
                const auto& original=*result.tracker;
                result.tracker=std::make_shared<const TrackerSnapshot>(original.status,original.key,original.observation,original.provenance,
                    original.processValidated,original.trackerAppId,original.fullGameId,original.exactPath,original.sourceLine,
                    original.trackerAtLocalSecond,original.earliestLocalSecond,original.latestLocalSecond,
                    original.parsedSource,original.groupObservation,result.startupHandoff);
            }
        }
        if(!flight||!result.tracker||!result.tracker->parsedSource||
            !result.tracker->observation.metadataComplete||!contextCurrent(lease,flight->key)||!tracker_detail::ownerCurrent(pureOwnerCurrent))return result;
        // The caller keeps the original request and source token. A new source
        // claimant stays Busy even after tracker commit while console is read.
        auto& observation=result.consoleObservation;
        const auto sourceKey=MainTrackerFactsAdapter::sourceKey({flight->key->facts().steam.pid,flight->key->facts().steam.birth,flight->key->facts().steam.root});
        const auto source=readLogSource(sourceKey,SteamLogKind::Console,SteamLogReadPolicy::Raw,pureOwnerCurrent,lease);
        observation=source->observation;
        if(!contextCurrent(lease,flight->key)||!tracker_detail::ownerCurrent(pureOwnerCurrent))return result;
        result.absoluteBoundary=mainReadTailPort().nowFileTime();
        if(!result.absoluteBoundary||result.absoluteBoundary<flight->key->facts().steam.birth)return result;
        auto primary=std::make_shared<ymcc::sleepsteam::NativePrimaryProcessInventory>();
        if(observation.metadataComplete){
            *primary=ymcc::sleepsteam::nativePrimaryProcessInventoryText(*observation.bytes,
                mainReadTailPort().localSecond(flight->key->facts().steam.birth),mainReadTailPort().localSecond(result.absoluteBoundary));
        }else{
            // Original unavailable-console single-member fallback remains
            // distinct from truncated/malformed primary strong-proof refusal.
            primary->complete=observation.readAttempted&&observation.availableKnown&&!observation.available&&
                (observation.readReason==TrackerSourceObservation::ReadReason::MissingOrNotRegular||observation.readReason==TrackerSourceObservation::ReadReason::ReadDenied||observation.readReason==TrackerSourceObservation::ReadReason::SizeUnavailable||observation.readReason==TrackerSourceObservation::ReadReason::ReadFailed);
            primary->reason=observation.truncated?"steam-native-primary-log-truncated":"steam-native-primary-log-unavailable";
        }
        result.nativePrimary=std::move(primary);result.consoleRevision=observation.sourceRevision;
        if(!contextCurrent(lease,flight->key)||!tracker_detail::ownerCurrent(pureOwnerCurrent))return result;
        // Ordinary consumers do not pin unused group members.
        if(groupPolicy==TrackerGroupCapturePolicy::Capture&&result.nativePrimary&&result.nativePrimary->complete&&observation.metadataComplete)
            result.processGroupCapture=captureProcessGroup(lease,result.tracker->parsedSource->processLifetime.group,
                *result.nativePrimary,result.consoleRevision,[&](){return contextCurrent(lease,flight->key)&&tracker_detail::ownerCurrent(pureOwnerCurrent);});
        if(!contextCurrent(lease,flight->key)||!tracker_detail::ownerCurrent(pureOwnerCurrent)){
            if(result.processGroupCapture&&result.processGroupCapture->lease)result.processGroupCapture->lease->cancel();
            result.processGroupCapture.reset();return result;
        }
        result.complete=true;return result;
    }
};
inline void TrackerDemand::release() noexcept {
    if (owner_ && request_) owner_->closeRequest(request_,TrackerRequestCloseReason::Cancelled);
    request_.reset(); owner_ = nullptr;
}
inline void TrackerFlightToken::release() noexcept {
    if (owner_ && flight_) owner_->releaseFlight(flight_);
    flight_.reset(); owner_ = nullptr;
}
inline TrackerOwner& trackerOwner() noexcept { static TrackerOwner value; return value; }
}
