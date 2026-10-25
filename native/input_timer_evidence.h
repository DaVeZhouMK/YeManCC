#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <json.hpp>

// Passive, bounded observations only. No clock reads, timer/thread/device ownership,
// control decisions, I/O or allocations in Recorder. snapshotJson is COLD PATH ONLY.
namespace ymcc::inputTimerEvidence {
using U64 = std::uint64_t;
enum Kind : unsigned { multimedia = 1, waitable = 2, sleep = 3 };
enum class BoundaryKind : unsigned { ownerBegin = 1, createResult = 2, killResult = 3 };
enum class TraceKind : unsigned { entry = 1, exit = 2 };
inline constexpr unsigned ownerCapacity = 8, groupCapacity = 8, activeCapacity = 64;
inline constexpr unsigned firstCapacity = 16, recentCapacity = 64;
inline constexpr unsigned boundaryFirstCapacity = 8, boundaryRecentCapacity = 16;

// PODs use natural member alignment and at least 8-byte struct alignment, no packing.
// Padding is not a wire format; serialize named fields. Token belongs to its Recorder
// (which must outlive it); it carries entry identity even after trace ring overwrite.
struct alignas(8) Token {
    U64 ordinal, nowMs, eventHandle, powerGeneration;
    unsigned timerId, threadId;
    bool stopObserved;
    const void* recorder;
};
struct alignas(8) Drops {
    U64 ownerBegin, createResult, killResult, enter, leave, snapshot, summary;
};
struct alignas(8) Summary {
    bool known, accountingComplete, inFlightKnown;
    // These claims are deliberately never established by this observer.
    bool ownerBindingKnown, callbacksDrainedProven, busCountProven, steamConsumptionProven;
    U64 ordinal, ownerBegins, createResults, killResults;
    U64 entered, exited, matchedLeaves, unmatchedLeaves;
    U64 busCalled, busySkip, stopReturn, exceptionOrUnknown;
    // Accepted entries minus matched accepted leaves, NOT a drain/quiescence proof.
    // Lost leaves remain unresolved. maxInFlight has the same accounting scope.
    U64 inFlight, maxInFlight, clockRollbacks;
    U64 ownerCapacityDrop, groupCapacityDrop, activeCapacityDrop;
    U64 firstCapacityDrop, recentOverwrite, boundaryFirstCapacityDrop, boundaryRecentOverwrite;
    unsigned ownerCount, groupCount, activeCount;
    Drops drops;
};
struct alignas(8) Owner {
    U64 owner, firstOrdinal, lastOrdinal, begins, creates, kills;
    U64 beginMs, beginEventHandle, createMs, createEventHandle, killMs, killEventHandle;
    unsigned kind, intervalMs, timerId, createErrorOrResult, killTimerId, killResult;
};
struct alignas(8) Group {
    unsigned timerId;
    U64 eventHandle, entered, exited, matchedLeaves, unmatchedLeaves;
    U64 busCalled, busySkip, stopReturn, exceptionOrUnknown, inFlight, maxInFlight;
};
struct alignas(8) Trace {
    U64 ordinal, entryOrdinal, nowMs, entryMs, eventHandle, powerGeneration;
    U64 busOrdinal, durationMs, ownerCandidate;
    unsigned timerId, threadId, outcome, ownerCandidateCount;
    TraceKind type;
    bool stopObserved, matchedEntry, durationKnown, clockRollback;
    // Candidate is only a latest-receipt hint, never a unique callback owner.
    bool ownerBindingKnown, ownerBindingAmbiguous;
};
struct alignas(8) Boundary {
    U64 ordinal, owner, nowMs, eventHandle;
    unsigned kind, intervalMs, timerId, result;
    BoundaryKind type;
    bool ownerSlotKnown, clockRollback;
};
struct alignas(8) Snapshot {
    Summary summary;
    Owner owners[ownerCapacity];
    Group groups[groupCapacity];
    Trace first[firstCapacity], recent[recentCapacity];
    Boundary boundaryFirst[boundaryFirstCapacity], boundaryRecent[boundaryRecentCapacity];
    unsigned firstCount, recentCount, recentStart;
    unsigned boundaryFirstCount, boundaryRecentCount, boundaryRecentStart;
};
static_assert(std::is_trivial_v<Token> && std::is_standard_layout_v<Token>);
static_assert(std::is_trivial_v<Summary> && std::is_standard_layout_v<Summary>);
static_assert(std::is_trivial_v<Snapshot> && std::is_standard_layout_v<Snapshot>);
static_assert(alignof(Snapshot) >= 8 && alignof(Token) >= 8);
static_assert(std::atomic<U64>::is_always_lock_free, "Drop accounting must never take a runtime lock");

struct RecorderTestAccess; // The memory selftest alone defines this friend.
class Recorder {
    friend struct RecorderTestAccess;
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
    std::atomic<U64> ownerBeginDrop_{0}, createDrop_{0}, killDrop_{0};
    std::atomic<U64> enterDrop_{0}, leaveDrop_{0}, snapshotDrop_{0}, summaryDrop_{0};
    // EVERY non-atomic member is accessed only under flag_, including readers.
    Snapshot data_{};
    Token active_[activeCapacity]{};
    U64 lastMs_ = 0;
    bool haveMs_ = false;
    struct Release {
        std::atomic_flag& flag;
        ~Release() noexcept { flag.clear(std::memory_order_release); }
    };
    bool acquire(std::atomic<U64>& drop) noexcept {
        if (!flag_.test_and_set(std::memory_order_acquire)) return true;
        drop.fetch_add(1, std::memory_order_relaxed);
        return false; // exactly one attempt; never wait, retry or spin
    }
    Drops drops() const noexcept {
        return {ownerBeginDrop_.load(std::memory_order_relaxed), createDrop_.load(std::memory_order_relaxed),
            killDrop_.load(std::memory_order_relaxed), enterDrop_.load(std::memory_order_relaxed),
            leaveDrop_.load(std::memory_order_relaxed), snapshotDrop_.load(std::memory_order_relaxed),
            summaryDrop_.load(std::memory_order_relaxed)};
    }
    Summary readSummary() const noexcept {
        Summary s = data_.summary;
        s.known = true;
        s.drops = drops();
        s.inFlightKnown = s.drops.enter == 0 && s.drops.leave == 0 &&
            s.unmatchedLeaves == 0 && s.activeCapacityDrop == 0;
        s.accountingComplete = s.inFlightKnown && s.drops.ownerBegin == 0 &&
            s.drops.createResult == 0 && s.drops.killResult == 0;
        return s;
    }
    bool observeClock(U64 now) noexcept {
        const bool rollback = haveMs_ && now < lastMs_;
        if (rollback) ++data_.summary.clockRollbacks;
        lastMs_ = now;
        haveMs_ = true;
        return rollback; // observation order only; no hardware-time inference
    }
    Owner* ownerSlot(U64 owner, U64 ordinal) noexcept {
        for (unsigned i = 0; i < data_.summary.ownerCount; ++i)
            if (data_.owners[i].owner == owner) return &data_.owners[i];
        if (data_.summary.ownerCount == ownerCapacity) {
            ++data_.summary.ownerCapacityDrop; // per boundary lacking a slot
            return nullptr;
        }
        Owner& row = data_.owners[data_.summary.ownerCount++];
        row.owner = owner;
        row.firstOrdinal = ordinal;
        return &row;
    }
    Group* groupSlot(unsigned timer, U64 event, bool add) noexcept {
        for (unsigned i = 0; i < data_.summary.groupCount; ++i)
            if (data_.groups[i].timerId == timer && data_.groups[i].eventHandle == event)
                return &data_.groups[i];
        if (!add) return nullptr;
        if (data_.summary.groupCount == groupCapacity) {
            ++data_.summary.groupCapacityDrop; // per entry lacking a group, never reject callback data
            return nullptr;
        }
        Group& row = data_.groups[data_.summary.groupCount++];
        row.timerId = timer;
        row.eventHandle = event;
        return &row;
    }
    void appendTrace(const Trace& row) noexcept {
        if (data_.firstCount < firstCapacity) data_.first[data_.firstCount++] = row;
        else ++data_.summary.firstCapacityDrop;
        if (data_.recentCount < recentCapacity) {
            data_.recent[(data_.recentStart + data_.recentCount++) % recentCapacity] = row;
        } else {
            data_.recent[data_.recentStart] = row;
            data_.recentStart = (data_.recentStart + 1) % recentCapacity;
            ++data_.summary.recentOverwrite;
        }
    }
    void appendBoundary(const Boundary& row) noexcept {
        if (data_.boundaryFirstCount < boundaryFirstCapacity)
            data_.boundaryFirst[data_.boundaryFirstCount++] = row;
        else ++data_.summary.boundaryFirstCapacityDrop;
        if (data_.boundaryRecentCount < boundaryRecentCapacity) {
            data_.boundaryRecent[(data_.boundaryRecentStart + data_.boundaryRecentCount++) % boundaryRecentCapacity] = row;
        } else {
            data_.boundaryRecent[data_.boundaryRecentStart] = row;
            data_.boundaryRecentStart = (data_.boundaryRecentStart + 1) % boundaryRecentCapacity;
            ++data_.summary.boundaryRecentOverwrite;
        }
    }
    void candidates(Trace& row) const noexcept {
        for (unsigned i = 0; i < data_.summary.ownerCount; ++i) {
            const Owner& owner = data_.owners[i];
            if (owner.creates && owner.timerId == row.timerId && owner.createEventHandle == row.eventHandle) {
                ++row.ownerCandidateCount;
                row.ownerCandidate = owner.owner;
            }
        }
        if (row.ownerCandidateCount != 1) row.ownerCandidate = 0;
        row.ownerBindingAmbiguous = row.ownerCandidateCount > 1;
        // No owner is supplied by enter(), so even a single matching receipt is NOT proof.
        row.ownerBindingKnown = false;
    }
    void boundary(BoundaryKind type, U64 owner, U64 now, U64 event,
                  unsigned timer, unsigned result, unsigned kind = 0, unsigned interval = 0) noexcept {
        Boundary row{};
        row.ordinal = ++data_.summary.ordinal;
        row.type = type; row.owner = owner; row.nowMs = now; row.eventHandle = event;
        row.timerId = timer; row.result = result; row.kind = kind; row.intervalMs = interval;
        row.clockRollback = observeClock(now);
        Owner* slot = ownerSlot(owner, row.ordinal);
        row.ownerSlotKnown = slot != nullptr;
        if (slot) {
            slot->lastOrdinal = row.ordinal;
            if (type == BoundaryKind::ownerBegin) {
                ++slot->begins; slot->beginMs = now; slot->beginEventHandle = event;
                slot->kind = kind; slot->intervalMs = interval;
            } else if (type == BoundaryKind::createResult) {
                ++slot->creates; slot->createMs = now; slot->createEventHandle = event;
                slot->timerId = timer; slot->createErrorOrResult = result;
            } else {
                ++slot->kills; slot->killMs = now; slot->killEventHandle = event;
                slot->killTimerId = timer; slot->killResult = result;
            }
            row.kind = slot->kind; row.intervalMs = slot->intervalMs;
        }
        appendBoundary(row); // API receipt only: never clears active entries or proves drain
    }
    static bool sameToken(const Token& a, const Token& b) noexcept {
        return a.ordinal == b.ordinal && a.nowMs == b.nowMs && a.eventHandle == b.eventHandle &&
            a.powerGeneration == b.powerGeneration && a.timerId == b.timerId && a.threadId == b.threadId &&
            a.stopObserved == b.stopObserved && a.recorder == b.recorder;
    }
    static void outcome(unsigned value, U64& bus, U64& busy, U64& stop, U64& unknown) noexcept {
        if (value == 1) ++bus;
        else if (value == 2) ++busy;
        else if (value == 3) ++stop;
        else ++unknown;
    }
public:
    Recorder() noexcept = default;
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    void ownerBegin(U64 owner, U64 nowMs, U64 eventHandle, unsigned kind, unsigned intervalMs) noexcept {
        if (!acquire(ownerBeginDrop_)) return;
        Release release{flag_};
        ++data_.summary.ownerBegins;
        boundary(BoundaryKind::ownerBegin, owner, nowMs, eventHandle, 0, 0, kind, intervalMs);
    }
    void createResult(U64 owner, U64 nowMs, U64 eventHandle, unsigned timerId, unsigned errorOrResult = 0) noexcept {
        if (!acquire(createDrop_)) return;
        Release release{flag_};
        ++data_.summary.createResults;
        boundary(BoundaryKind::createResult, owner, nowMs, eventHandle, timerId, errorOrResult);
    }
    void killResult(U64 owner, U64 nowMs, U64 eventHandle, unsigned timerId, unsigned result) noexcept {
        if (!acquire(killDrop_)) return;
        Release release{flag_};
        ++data_.summary.killResults;
        boundary(BoundaryKind::killResult, owner, nowMs, eventHandle, timerId, result);
    }
    Token enter(bool enabled, unsigned timerId, U64 eventHandle, U64 nowMs,
                unsigned threadId, U64 powerGeneration, bool stopObserved) noexcept {
        if (!enabled) return {}; // disabled: no clock, allocation, lock or atomic access
        if (!acquire(enterDrop_)) return {};
        Release release{flag_};
        Token token{++data_.summary.ordinal, nowMs, eventHandle, powerGeneration,
            timerId, threadId, stopObserved, this};
        ++data_.summary.entered;
        if (++data_.summary.inFlight > data_.summary.maxInFlight)
            data_.summary.maxInFlight = data_.summary.inFlight;
        bool tracked = false;
        for (auto& slot : active_) if (slot.ordinal == 0) {
            slot = token; tracked = true; ++data_.summary.activeCount; break;
        }
        if (!tracked) ++data_.summary.activeCapacityDrop;
        if (Group* group = groupSlot(timerId, eventHandle, true)) {
            ++group->entered;
            if (++group->inFlight > group->maxInFlight) group->maxInFlight = group->inFlight;
        }
        Trace row{};
        row.type = TraceKind::entry; row.ordinal = token.ordinal; row.entryOrdinal = token.ordinal;
        row.nowMs = nowMs; row.entryMs = nowMs; row.timerId = timerId; row.eventHandle = eventHandle;
        row.threadId = threadId; row.powerGeneration = powerGeneration; row.stopObserved = stopObserved;
        row.clockRollback = observeClock(nowMs); candidates(row); appendTrace(row);
        return token;
    }
    void leave(Token token, U64 nowMs, unsigned value, U64 busOrdinal = 0) noexcept {
        if (token.ordinal == 0) return; // no accepted entry, including disabled/contended enter
        if (!acquire(leaveDrop_)) return; // unresolved entry is retained; never manufacture zero
        Release release{flag_};
        bool matched = false;
        if (token.recorder == this) for (auto& slot : active_) if (sameToken(slot, token)) {
            slot = {}; matched = true; --data_.summary.activeCount; break;
        }
        ++data_.summary.exited;
        if (matched) { ++data_.summary.matchedLeaves; --data_.summary.inFlight; }
        else ++data_.summary.unmatchedLeaves;
        outcome(value, data_.summary.busCalled, data_.summary.busySkip,
            data_.summary.stopReturn, data_.summary.exceptionOrUnknown);
        if (Group* group = groupSlot(token.timerId, token.eventHandle, false)) {
            ++group->exited;
            if (matched) { ++group->matchedLeaves; --group->inFlight; }
            else ++group->unmatchedLeaves;
            outcome(value, group->busCalled, group->busySkip, group->stopReturn, group->exceptionOrUnknown);
        }
        Trace row{};
        row.ordinal = ++data_.summary.ordinal; row.entryOrdinal = token.ordinal; row.type = TraceKind::exit;
        row.nowMs = nowMs; row.entryMs = token.nowMs; row.timerId = token.timerId;
        row.eventHandle = token.eventHandle; row.threadId = token.threadId;
        row.powerGeneration = token.powerGeneration; row.stopObserved = token.stopObserved;
        row.outcome = value; row.busOrdinal = busOrdinal; row.matchedEntry = matched;
        row.durationKnown = matched && nowMs >= token.nowMs;
        row.durationMs = row.durationKnown ? nowMs - token.nowMs : 0;
        row.clockRollback = observeClock(nowMs); candidates(row); appendTrace(row);
    }
    // One try-lock for the whole plain-data copy. known=false means ALL plain fields
    // are unavailable, NOT zero. No torn fields/arrays in a known snapshot/summary.
    // Drop counters are separately sampled relaxed atomics, not a transaction with
    // the plain fields or each other. Conservation is exact after producers quiesce.
    // Inspection drops never invalidate accepted callback accounting.
    Snapshot snapshot() noexcept {
        Snapshot result{};
        if (!acquire(snapshotDrop_)) { result.summary.drops = drops(); return result; }
        Release release{flag_};
        result = data_;
        result.summary = readSummary();
        return result;
    }
    Summary summary() noexcept {
        if (!acquire(summaryDrop_)) { Summary result{}; result.drops = drops(); return result; }
        Release release{flag_};
        return readSummary();
    }
};

// Cold path only; may allocate/throw via json.hpp. No I/O. Repeated first/recent
// entries are the same observation by ordinal, not additional callback executions.
inline nlohmann::json snapshotJson(const Snapshot& snapshot) {
    using Json = nlohmann::json;
    const auto& s = snapshot.summary;
    Json j{{"schema",1},{"known",s.known},{"scope","passive caller-supplied timer API receipts and callback observations; cumulative recorder lifetime"},
        {"clockScope","caller tick milliseconds in recorder acceptance order; not hardware time; concurrent samples may arrive out of order"},
        {"atomicity","known plain fields copied under one try-lock; drop atomics sampled individually, not transactionally; exact conservation only after producers quiesce"},
        {"inFlightScope","accepted entries minus matched accepted leaves; missing leaves stay unresolved; never proof of physical concurrency or callback drain"},
        {"ownerScope","owner table stores explicit lifecycle receipts; callback candidate hints use latest matching receipts only, never prove ownership or uniqueness"},
        {"retentionScope","first and recent windows overlap by ordinal; ring arrays exported oldest to newest; capacities do not bound callback execution"},
        {"dropScope","operation drops count failed single try-lock attempts; ownerCapacityDrop per boundary without owner slot; groupCapacityDrop per entry without group; activeCapacityDrop per entry without matching slot; window drops count per-window retention loss"},
        {"ownerBindingKnown",false},{"callbacksDrainedProven",false},{"busCountProven",false},{"steamConsumptionProven",false},
        {"caps",{{"owners",ownerCapacity},{"groups",groupCapacity},{"active",activeCapacity},{"first",firstCapacity},
            {"recent",recentCapacity},{"boundaryFirst",boundaryFirstCapacity},{"boundaryRecent",boundaryRecentCapacity}}},
        {"drops",{{"ownerBegin",s.drops.ownerBegin},{"createResult",s.drops.createResult},{"killResult",s.drops.killResult},
            {"enter",s.drops.enter},{"leave",s.drops.leave},{"snapshot",s.drops.snapshot},{"summary",s.drops.summary}}}};
    if (!s.known) { j["counts"] = nullptr; j["owners"] = nullptr; j["groups"] = nullptr;
        j["trace"] = nullptr; j["boundaries"] = nullptr; j["inFlightKnown"] = false; return j; }
    j["accountingComplete"] = s.accountingComplete; j["inFlightKnown"] = s.inFlightKnown;
    j["counts"] = {{"ordinal",s.ordinal},{"ownerBegins",s.ownerBegins},{"createResults",s.createResults},{"killResults",s.killResults},
        {"entered",s.entered},{"exited",s.exited},{"matchedLeaves",s.matchedLeaves},{"unmatchedLeaves",s.unmatchedLeaves},
        {"busCalled",s.busCalled},{"busySkip",s.busySkip},{"stopReturn",s.stopReturn},{"exceptionOrUnknown",s.exceptionOrUnknown},
        {"inFlight",s.inFlight},{"maxInFlight",s.maxInFlight},{"clockRollbacks",s.clockRollbacks},
        {"activeCount",s.activeCount},{"ownerCount",s.ownerCount},{"groupCount",s.groupCount},
        {"ownerCapacityDrop",s.ownerCapacityDrop},{"groupCapacityDrop",s.groupCapacityDrop},{"activeCapacityDrop",s.activeCapacityDrop},
        {"firstCapacityDrop",s.firstCapacityDrop},{"recentOverwrite",s.recentOverwrite},
        {"boundaryFirstCapacityDrop",s.boundaryFirstCapacityDrop},{"boundaryRecentOverwrite",s.boundaryRecentOverwrite}};
    j["owners"] = Json::array();
    for (unsigned i=0; i<s.ownerCount; ++i) { const auto& o=snapshot.owners[i];
        j["owners"].push_back({{"owner",o.owner},{"firstOrdinal",o.firstOrdinal},{"lastOrdinal",o.lastOrdinal},
            {"begins",o.begins},{"creates",o.creates},{"kills",o.kills},{"kind",o.kind},{"intervalMs",o.intervalMs},
            {"beginMs",o.beginMs},{"beginEventHandle",o.beginEventHandle},{"createMs",o.createMs},{"createEventHandle",o.createEventHandle},
            {"timerId",o.timerId},{"createErrorOrResult",o.createErrorOrResult},{"killMs",o.killMs},
            {"killEventHandle",o.killEventHandle},{"killTimerId",o.killTimerId},{"killResult",o.killResult}});
    }
    j["groups"] = Json::array();
    for (unsigned i=0; i<s.groupCount; ++i) { const auto& g=snapshot.groups[i];
        j["groups"].push_back({{"timerId",g.timerId},{"eventHandle",g.eventHandle},{"entered",g.entered},{"exited",g.exited},
            {"matchedLeaves",g.matchedLeaves},{"unmatchedLeaves",g.unmatchedLeaves},{"busCalled",g.busCalled},
            {"busySkip",g.busySkip},{"stopReturn",g.stopReturn},{"exceptionOrUnknown",g.exceptionOrUnknown},
            {"inFlight",g.inFlight},{"maxInFlight",g.maxInFlight},{"inFlightKnown",s.inFlightKnown}});
    }
    auto traceJson = [](const Trace& t) -> Json {
        return {{"ordinal",t.ordinal},{"entryOrdinal",t.entryOrdinal},{"type",t.type==TraceKind::entry?"entry":"exit"},
            {"nowMs",t.nowMs},{"entryMs",t.entryMs},{"timerId",t.timerId},{"eventHandle",t.eventHandle},
            {"threadId",t.threadId},{"powerGeneration",t.powerGeneration},{"stopObserved",t.stopObserved},
            {"outcome",t.outcome},{"busOrdinal",t.busOrdinal},{"matchedEntry",t.matchedEntry},
            {"durationKnown",t.durationKnown},{"durationMs",t.durationKnown?Json(t.durationMs):Json(nullptr)},
            {"clockRollback",t.clockRollback},{"ownerCandidate",t.ownerCandidate},{"ownerCandidateCount",t.ownerCandidateCount},
            {"ownerBindingKnown",false},{"ownerBindingAmbiguous",t.ownerBindingAmbiguous}};
    };
    j["trace"] = {{"first",Json::array()},{"recent",Json::array()}};
    for (unsigned i=0; i<snapshot.firstCount; ++i) j["trace"]["first"].push_back(traceJson(snapshot.first[i]));
    for (unsigned i=0; i<snapshot.recentCount; ++i)
        j["trace"]["recent"].push_back(traceJson(snapshot.recent[(snapshot.recentStart+i)%recentCapacity]));
    auto boundaryJson = [](const Boundary& b) -> Json {
        return {{"ordinal",b.ordinal},{"type",b.type==BoundaryKind::ownerBegin?"owner-begin":b.type==BoundaryKind::createResult?"create-api-receipt":"kill-api-receipt"},
            {"owner",b.owner},{"nowMs",b.nowMs},{"eventHandle",b.eventHandle},{"kind",b.kind},{"intervalMs",b.intervalMs},
            {"timerId",b.timerId},{"result",b.result},{"ownerSlotKnown",b.ownerSlotKnown},{"clockRollback",b.clockRollback},
            {"callbacksDrainedProven",false}};
    };
    j["boundaries"] = {{"first",Json::array()},{"recent",Json::array()}};
    for (unsigned i=0; i<snapshot.boundaryFirstCount; ++i) j["boundaries"]["first"].push_back(boundaryJson(snapshot.boundaryFirst[i]));
    for (unsigned i=0; i<snapshot.boundaryRecentCount; ++i)
        j["boundaries"]["recent"].push_back(boundaryJson(snapshot.boundaryRecent[(snapshot.boundaryRecentStart+i)%boundaryRecentCapacity]));
    return j;
}
} // namespace ymcc::inputTimerEvidence
