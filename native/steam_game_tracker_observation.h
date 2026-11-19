#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "sleep_steam_tracking.h"
#include "sleep_steam_capture.h"
#include <sstream>

class NativeGameFactScope; // Defined by the independent unique game owner.
namespace ymcc::steamgamefacts {
// PRIVATE SOURCE ONLY. No Windows, Reader, discovery or UI APIs in these DTOs.
enum class TrackerReadMode { AllowHistoricalBinding, RequireCurrentSource, RequireNewSource };
// AccountBound is reserved/unsupported: no actual typed UI/CM proof port yet.
enum class TrackerObservationPolicy { AccountBound, ProcessOnlyObservation };
enum class AccountProvenance { Unknown, StrictUiAccountProof, StrictCmAccountProof };
// SharedMenuInspect is an observation label, NOT strict account/document proof.
enum class AccountFactSource { Unknown, SharedMenuInspect, SharedUiReceipt, SharedCmProof };
using TrackerOwnerCurrent = std::function<bool()>;
struct TrackerProcessIdentity {
    std::uint32_t pid = 0;
    std::uint64_t birth = 0;
    std::wstring exactPath;
};
struct TrackerSteamReceipt {
    bool present = false, rootKnown = false, windowsSessionKnown = false;
    std::uint32_t pid = 0, windowsSession = 0;
    std::uint64_t birth = 0, processEpoch = 0, accountEpoch = 0;
    std::wstring root;
    bool operator==(const TrackerSteamReceipt&) const = default;
};
struct TrackerAccountDomain {
    std::uint32_t resolvedAccount32 = 0;
    AccountProvenance provenance = AccountProvenance::Unknown;
    AccountFactSource source = AccountFactSource::Unknown;
    bool operator==(const TrackerAccountDomain&) const = default;
};
struct TrackerFactTuple {
    TrackerObservationPolicy policy = TrackerObservationPolicy::ProcessOnlyObservation;
    TrackerProcessIdentity selectedGame; // Original selected spelling as provenance.
    std::string canonicalGamePath, canonicalSteamRoot;
    TrackerSteamReceipt steam;
    TrackerAccountDomain account;
    std::uint64_t rulesEpoch = 0, powerGeneration = 0;
    bool operator==(const TrackerFactTuple& b) const noexcept {
        return policy == b.policy && selectedGame.pid == b.selectedGame.pid &&
            selectedGame.birth == b.selectedGame.birth && canonicalGamePath == b.canonicalGamePath &&
            canonicalSteamRoot == b.canonicalSteamRoot && steam.pid == b.steam.pid &&
            steam.birth == b.steam.birth && steam.processEpoch == b.steam.processEpoch &&
            steam.accountEpoch == b.steam.accountEpoch && steam.windowsSession == b.steam.windowsSession &&
            account == b.account && rulesEpoch == b.rulesEpoch && powerGeneration == b.powerGeneration;
    }
};
class TrackerOwner;
class ProcessGroupLease;
struct ProcessGroupCapture;
class MainTrackerFactsAdapter;
class TrackerFactKey final {
    friend class TrackerOwner;
    friend class MainTrackerFactsAdapter;
    const TrackerFactTuple facts_;
    const std::shared_ptr<const ::NativeGameFactScope> gameScope_;
    TrackerFactKey(TrackerFactTuple facts, std::shared_ptr<const ::NativeGameFactScope> gameScope)
        : facts_(std::move(facts)), gameScope_(std::move(gameScope)) {}
public:
    TrackerFactKey(const TrackerFactKey&) = delete;
    TrackerFactKey& operator=(const TrackerFactKey&) = delete;
    const TrackerFactTuple& facts() const noexcept { return facts_; }
};
using TrackerObservationKey = std::shared_ptr<const TrackerFactKey>;
// Exact original flight metadata. The owner alone constructs it; no
// allocation, source bytes, action authority or numeric reconstruction.
namespace tracker_detail { struct TrackerFlight; }
class TrackerCompletionWitness final {
    friend class TrackerOwner;
    TrackerOwner* owner_ = nullptr;
    TrackerObservationKey key_;
    std::weak_ptr<tracker_detail::TrackerFlight> flight_;
    std::uint64_t sequence_ = 0, factVersion_ = 0;
    TrackerCompletionWitness(TrackerOwner* owner,TrackerObservationKey key,
        const std::shared_ptr<tracker_detail::TrackerFlight>& flight,std::uint64_t sequence,std::uint64_t version)
        :owner_(owner),key_(std::move(key)),flight_(flight),sequence_(sequence),factVersion_(version){}
public:
    TrackerCompletionWitness()=default;
    explicit operator bool()const noexcept{return owner_&&key_&&sequence_&&factVersion_;}
    TrackerObservationKey key()const noexcept{return key_;}
    std::uint64_t sequence()const noexcept{return sequence_;}
    std::uint64_t factVersion()const noexcept{return factVersion_;}
};
// These are request/action metadata only. NEVER a fact/cache key component.
struct TrackerRequestContext {
    std::uint64_t selectionRevision = 0, configRevision = 0, requestRevision = 0;
    std::string document, documentOwner;
    std::uint64_t documentEntry = 0;
};
struct TrackerKey {
    TrackerFactTuple observation;
    TrackerProcessIdentity observedGame, heldSteam;
    std::wstring sourceKey;
};
enum class TrackerStatus {
    BoundHistoricalFact, CachedHistoricalAssociation, InvalidContext, ContextChanged, Superseded,
    SourceUnavailable, SourceTruncated, SourceIncomplete, SourceReadException,
    ParserUnknown, ParserRemoved, ParserAmbiguous, ParserIncomplete,
    FullGameIdUnknown, NoExecutionToken, PreparationException, DeadlineExpired
};
enum class TrackerFlightPhase {
    None, Created, Queued, Reading, Succeeded, Failed, Cancelled, Expired, Superseded, HistoricalAvailable
};
inline bool trackerFlightTerminal(TrackerFlightPhase phase) noexcept {
    return phase == TrackerFlightPhase::Succeeded || phase == TrackerFlightPhase::Failed ||
        phase == TrackerFlightPhase::Cancelled || phase == TrackerFlightPhase::Expired ||
        phase == TrackerFlightPhase::Superseded || phase == TrackerFlightPhase::HistoricalAvailable;
}
struct TrackerFlightReceipt {
    // Numeric IDs are diagnostic only; no public API accepts caller-issued IDs.
    TrackerFlightPhase phase = TrackerFlightPhase::None;
    TrackerStatus lastStatus = TrackerStatus::NoExecutionToken;
    std::uint64_t demandId = 0, revision = 0, flightSequence = 0, factVersion = 0;
    std::uint32_t readsStarted = 0;
    bool terminal() const noexcept { return trackerFlightTerminal(phase); }
};
struct TrackerSourceObservation {
    std::uint64_t sourceRevision=0; // Actual one-source read sequence, independent of game flights.
    bool readAttempted = false, availableKnown = false, available = false;
    bool truncatedKnown = false, truncated = false;
    bool originalBytesKnown = false, copiedBytesKnown = false, limitBytesKnown = false;
    std::uint64_t originalBytes = 0, copiedBytes = 0, limitBytes = 0, returnedBytes = 0;
    bool win32ErrorKnown = false;
    std::uint32_t win32Error = 0;
    enum class ReadReason { None, MissingOrNotRegular, ReadDenied, SizeUnavailable, ReadFailed, Unknown, ContextChanged, IdentityUnavailable, InvalidRequest, SequenceExhausted, ReadException };
    ReadReason readReason = ReadReason::Unknown;
    std::uint64_t startedAtTick = 0, finishedAtTick = 0;
    bool metadataComplete = false;
    // Bounded by the source kind; may contain other paths/rows. Never dump to normal logs.
    std::shared_ptr<const std::string> bytes;
};
struct TrackerCacheProvenance {
    bool cacheHit = false, currentSource = false, sourceReadThisFlight = false;
    std::uint64_t checkedAtTick = 0, sourceCheckedAtTick = 0;
    std::uint64_t requestSequence = 0, observationSequence = 0;
    TrackerFlightReceipt flight;
    const char* source = "no-complete-source-observation";
};
// These are log observations from one physical source revision. They never
// mean native Running, current process liveness, UI state or action authority.
enum class TrackerLogScope { RunningListAndProcess, ProcessLifetime };
enum class TrackerGameIdKind { Unknown, NativeApp, ShortcutType2 };
struct TrackerInventoryMember {
    std::uint32_t appid=0,pid=0;
    std::string gameIdRaw{};
    std::uint64_t fullGameId=0;
    bool fullGameIdKnown=false;
    std::string path{},at{};
    unsigned sourceLine=0;
    bool identityConflict=false;
    TrackerGameIdKind gameIdKind=TrackerGameIdKind::Unknown;
};
struct TrackerInventoryProjection {
    std::uint64_t sourceRevision=0;
    TrackerLogScope scope=TrackerLogScope::RunningListAndProcess;
    bool complete=false;
    std::string reason;
    // The entire AppID/PID membership, not only the selected game's row.
    // Exact row metadata is bounded by the same source; it never elects a game.
    std::vector<TrackerInventoryMember> members;
};
struct TrackerBindingProjection {
    std::uint64_t sourceRevision=0;
    TrackerLogScope scope=TrackerLogScope::RunningListAndProcess;
    std::string status="unknown",at,gameIdRaw;
    std::uint32_t appid=0;
    std::uint64_t fullGameId=0;
    unsigned sourceLine=0;
    bool exactPath=false,fullGameIdKnown=false;
};
// Pure historical log metadata. Observed describes only this source revision;
// no status proves liveness, account ownership, primary/foreground equivalence,
// overlay registration, current lease validity or permission to perform work.
enum class TrackerGroupStatus { Unknown, Incomplete, Observed };
struct TrackerGroupObservation {
    std::uint64_t sourceRevision=0;
    TrackerLogScope scope=TrackerLogScope::RunningListAndProcess;
    TrackerGroupStatus status=TrackerGroupStatus::Unknown;
    std::string reason="steam-tracker-group-unknown";
    std::uint32_t appid=0,selectedPid=0;
    std::uint64_t fullGameId=0;
    // Canonical decimal for consumers that cannot represent every uint64.
    // gameIdRaw separately retains the exact selected log token.
    std::string fullGameIdDecimal{},gameIdRaw{};
    bool fullGameIdKnown=false,selectedRowObserved=false;
    bool relatedFullGameIdUnknown=false,pidConflict=false;
    TrackerGameIdKind gameIdKind=TrackerGameIdKind::Unknown;
    unsigned selectedSourceLine=0;
    // At most the original 32 live rows, only with exactly this complete ID.
    std::vector<TrackerInventoryMember> members;
};
struct TrackerScopeProjection {
    TrackerInventoryProjection inventory;
    TrackerBindingProjection binding;
    TrackerGroupObservation group;
};
struct TrackerParsedSource {
    std::uint64_t sourceRevision=0;
    TrackerScopeProjection processLifetime,runningList;
};
class StartupHandoffEvidence;
class HistoricalStartupHandoffEvidence;
class TrackerSnapshot final {
public:
    const TrackerStatus status;
    const TrackerObservationPolicy scope;
    const bool accountUnknown;
    const TrackerKey key;
    const TrackerSourceObservation observation;
    const TrackerCacheProvenance provenance;
    // Present only on an actual producer result, never a memory peek view.
    const std::shared_ptr<const TrackerParsedSource> parsedSource;
    // The selected ProcessLifetime binding's historical group only. Memory
    // peeks may retain this metadata, never parsedSource/current-source proof.
    const std::shared_ptr<const TrackerGroupObservation> groupObservation;
    const std::shared_ptr<const StartupHandoffEvidence> startupHandoff;
    const bool processValidated;
    const std::uint32_t trackerAppId;
    const std::uint64_t fullGameId;
    const std::string fullGameIdDecimal;
    const bool exactPath;
    const unsigned sourceLine;
    const std::string trackerAtLocalSecond, earliestLocalSecond, latestLocalSecond;
    TrackerSnapshot(TrackerStatus state, TrackerKey sourceKey, TrackerSourceObservation source,
        TrackerCacheProvenance cache, bool checked = false, std::uint32_t app = 0,
        std::uint64_t full = 0, bool exact = false, unsigned line = 0, std::string at = {},
        std::string earliest = {}, std::string latest = {},
        std::shared_ptr<const TrackerParsedSource> parsed = {},
        std::shared_ptr<const TrackerGroupObservation> group = {},
        std::shared_ptr<const StartupHandoffEvidence> handoff = {})
        : status(state), scope(sourceKey.observation.policy),
          accountUnknown(scope == TrackerObservationPolicy::ProcessOnlyObservation || !sourceKey.observation.account.resolvedAccount32),
          key(std::move(sourceKey)), observation(std::move(source)), provenance(cache), parsedSource(std::move(parsed)),
          groupObservation(std::move(group)), startupHandoff(std::move(handoff)), processValidated(checked),
          trackerAppId(app), fullGameId(full), fullGameIdDecimal(full ? std::to_string(full) : std::string{}),
          exactPath(exact), sourceLine(line), trackerAtLocalSecond(std::move(at)),
          earliestLocalSecond(std::move(earliest)), latestLocalSecond(std::move(latest)) {}
    bool hasHistoricalBinding() const noexcept {
        return status == TrackerStatus::BoundHistoricalFact || status == TrackerStatus::CachedHistoricalAssociation;
    }
};
// Two independent bounded source observations, never an atomic Steam/UI
// snapshot and never a cached authority. Only the producer mints this result.
enum class TrackerGroupCapturePolicy { None, Capture };
struct TrackerFreshObservation {
    bool complete=false;
    std::shared_ptr<const TrackerSnapshot> tracker;
    TrackerSourceObservation consoleObservation;
    std::shared_ptr<const ymcc::sleepsteam::NativePrimaryProcessInventory> nativePrimary;
    std::uint64_t consoleRevision=0,absoluteBoundary=0;
    // Explicit opt-in only, using the same two reads from this fresh flight.
    // No memory peek or caller-supplied historical DTO can mint the lease.
    std::shared_ptr<const ProcessGroupCapture> processGroupCapture;
    std::shared_ptr<const StartupHandoffEvidence> startupHandoff;
    TrackerOwnerCurrent startupHandoffCurrent;
    std::shared_ptr<const HistoricalStartupHandoffEvidence> historicalStartupHandoff;
    TrackerOwnerCurrent historicalStartupHandoffCurrent;
};
inline ymcc::sleepsteam::Json trackerSourceMetadata(const TrackerSourceObservation& observation){
    using Json=ymcc::sleepsteam::Json;Json result=Json::object();
    if(observation.availableKnown)result["available"]=observation.available;
    if(observation.truncatedKnown)result["truncated"]=observation.truncated;
    if(observation.originalBytesKnown)result["originalBytes"]=observation.originalBytes;
    if(observation.copiedBytesKnown)result["copiedBytes"]=observation.copiedBytes;
    if(observation.limitBytesKnown)result["limitBytes"]=observation.limitBytes;
    if(observation.win32ErrorKnown)result["win32Error"]=observation.win32Error;
    using Reason=TrackerSourceObservation::ReadReason;
    const auto reason=observation.readReason==Reason::MissingOrNotRegular?"missing-or-not-regular-file":
        observation.readReason==Reason::ReadDenied?"read-denied":observation.readReason==Reason::SizeUnavailable?"size-unavailable":
        observation.readReason==Reason::ReadFailed?"read-failed":observation.readReason==Reason::ContextChanged?"steam-source-context-changed":
        observation.readReason==Reason::IdentityUnavailable?"steam-source-identity-unavailable":observation.readReason==Reason::InvalidRequest?"steam-source-request-invalid":
        observation.readReason==Reason::SequenceExhausted?"steam-source-sequence-exhausted":observation.readReason==Reason::ReadException?"read-exception":"";
    if(*reason)result["reason"]=reason;
    if(observation.sourceRevision)result["sourceRevision"]=observation.sourceRevision;return result;
}
struct SteamLogIdentity {std::uint32_t pid=0;std::uint64_t birth=0;std::wstring root;};
enum class SteamLogKind { GameProcess, Console };
inline constexpr std::size_t steamLogMaximumBytes(SteamLogKind kind) noexcept {
    return kind==SteamLogKind::Console?ymcc::sleepsteam::nativePrimaryMaximumBytes:ymcc::sleepsteam::trackerMaximumBytes;
}
enum class SteamLogReadPolicy { Raw, CompleteOnly, TruncatedDiagnostic };
// Request admission may include the original cumulative active-time budget.
// It is invoked outside the common owner lock; no new deadline is created.
using SteamLogReadAdmission=std::function<bool()>;
class SteamLogSourceKey final {
    friend class MainTrackerFactsAdapter;
    const TrackerSteamReceipt steam_;
    explicit SteamLogSourceKey(TrackerSteamReceipt steam):steam_(std::move(steam)){}
public:
    const TrackerSteamReceipt& facts()const noexcept{return steam_;}
};
using SteamLogSourceIdentity=std::shared_ptr<const SteamLogSourceKey>;
struct SteamLogSourceView {
    TrackerSourceObservation observation;
    TrackerSteamReceipt steam;
    SteamLogKind kind=SteamLogKind::GameProcess;
    SteamLogReadPolicy policy=SteamLogReadPolicy::Raw;
    bool complete=false,diagnosticOnly=false;
    std::vector<std::string> lines;
    ymcc::sleepsteam::TrackedProcessInventory runningInventory{false,"steam-tracker-not-parsed",{}},lifetimeInventory{false,"steam-tracker-not-parsed",{}};
    ymcc::sleepsteam::Json meta()const{return trackerSourceMetadata(observation);}
    ymcc::sleepsteam::Binding binding(std::uint32_t pid,const std::string& path,const std::string& earliest,const std::string& latest,
        ymcc::sleepsteam::TrackingScope scope=ymcc::sleepsteam::TrackingScope::RunningListAndProcess)const{
        if(policy==SteamLogReadPolicy::Raw||(!complete&&!diagnosticOnly))return {};
        return ymcc::sleepsteam::track(lines,pid,path,earliest,latest,scope);
    }
    ymcc::sleepsteam::NativePrimaryProcessInventory primary(const std::string& earliest,const std::string& latest)const{
        if(!complete||!observation.bytes){ymcc::sleepsteam::NativePrimaryProcessInventory out;out.complete=false;out.reason="steam-native-primary-log-unavailable";return out;}
        return ymcc::sleepsteam::nativePrimaryProcessInventoryText(*observation.bytes,earliest,latest);
    }
};
// Minted only while the one source owner performs an explicit request. The
// exposed views remain observations; only the private capture holds objects.
// No caller-provided DTO can be promoted to this request-owned capture.
class SteamProcessGroupObservation final {
    friend class TrackerOwner;
    bool sourcesComplete_=false;
    std::uint64_t boundary_=0;
    std::shared_ptr<const SteamLogSourceView> gameSource_,consoleSource_;
    std::shared_ptr<const TrackerParsedSource> parsed_;
    std::shared_ptr<const ymcc::sleepsteam::NativePrimaryProcessInventory> primary_;
    std::shared_ptr<const ProcessGroupCapture> capture_;
    SteamProcessGroupObservation()=default;
public:
    SteamProcessGroupObservation(const SteamProcessGroupObservation&)=default;
    SteamProcessGroupObservation(SteamProcessGroupObservation&&)=default;
    SteamProcessGroupObservation& operator=(const SteamProcessGroupObservation&)=default;
    SteamProcessGroupObservation& operator=(SteamProcessGroupObservation&&)=default;
    bool sourcesComplete()const noexcept{return sourcesComplete_;}
    std::uint64_t absoluteBoundary()const noexcept{return boundary_;}
    const std::shared_ptr<const SteamLogSourceView>& gameSource()const noexcept{return gameSource_;}
    const std::shared_ptr<const SteamLogSourceView>& consoleSource()const noexcept{return consoleSource_;}
    const std::shared_ptr<const TrackerParsedSource>& parsedSource()const noexcept{return parsed_;}
    const std::shared_ptr<const ymcc::sleepsteam::NativePrimaryProcessInventory>& nativePrimary()const noexcept{return primary_;}
    const std::shared_ptr<const ProcessGroupCapture>& processCapture()const noexcept{return capture_;}
};
enum class TrackerPeekStatus { CachedHistoricalAssociation, Pending, Unknown };
enum class TrackerPeekUnknown { None, Missing, Pending, OwnerBusy, InvalidKey, OwnerChanged, CacheChanged, SourceUnknown };
class TrackerPeekResult final {
public:
    const TrackerPeekStatus status;
    const TrackerPeekUnknown reason;
    const TrackerObservationPolicy scope;
    const bool accountUnknown;
    const TrackerObservationKey key;
    const bool processValidated = false, currentSource = false;
    const TrackerFlightReceipt flight;
    const TrackerCompletionWitness completion; // Same-lock Pending flight only.
    const std::shared_ptr<const TrackerSnapshot> association;
    // Latest real source receipt, including Unknown/removed/truncated. Never
    // treat its presence as an association or a reason to auto-refresh.
    const std::shared_ptr<const TrackerSnapshot> observation;
    TrackerPeekResult(TrackerObservationKey input, TrackerPeekUnknown why,
        std::shared_ptr<const TrackerSnapshot> value = {}, TrackerFlightReceipt progress = {},
        std::shared_ptr<const TrackerSnapshot> lastObservation = {},
        TrackerCompletionWitness pendingCompletion = {})
        : status(value && value->hasHistoricalBinding() ? TrackerPeekStatus::CachedHistoricalAssociation :
            ((why == TrackerPeekUnknown::Missing || why == TrackerPeekUnknown::Pending ||
              why == TrackerPeekUnknown::OwnerBusy || why == TrackerPeekUnknown::CacheChanged) ?
                TrackerPeekStatus::Pending : TrackerPeekStatus::Unknown)),
          reason(value && value->hasHistoricalBinding() ? TrackerPeekUnknown::None : why),
          scope(input ? input->facts().policy : TrackerObservationPolicy::ProcessOnlyObservation),
          accountUnknown(!input || scope == TrackerObservationPolicy::ProcessOnlyObservation || !input->facts().account.resolvedAccount32),
          key(std::move(input)),
          flight(value && value->hasHistoricalBinding() ? value->provenance.flight : progress),
          completion(status == TrackerPeekStatus::Pending && reason == TrackerPeekUnknown::Pending
              ? std::move(pendingCompletion) : TrackerCompletionWitness{}),
          association(value && value->hasHistoricalBinding() ? value : nullptr),
          observation(lastObservation ? std::move(lastObservation) : std::move(value)) {}
};
}
