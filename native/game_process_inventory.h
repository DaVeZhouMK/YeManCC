#pragma once
#include <windows.h>
#include "process_inventory.h"
#include <winternl.h>
#include <algorithm>
#include <atomic>
#include <clocale>
#include <string>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

// A fresh process snapshot for the game scanner. Memory here is a shortlist
// hint; all admitted candidates still go through the live one-PID validator.
// The snapshot wrapper holds no process handles. Discovery below retains only
// hints for 5 seconds, with per-candidate live identity validation, a shared
// read-rate limit and an advancing background cursor; it adds no timer/thread.
namespace ymcc::gameInventory {
inline constexpr ULONGLONG topologyIntervalMs = 5000;
using Query = NTSTATUS (NTAPI*)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
using Entry = ymcc::processInventory::DetailView;

inline Query resolveQuery() noexcept {
    return ymcc::processInventory::resolveQuery();
}

// Pure compatibility entry: parsing itself performs no discovery or OS query.
inline bool parse(const void* data, std::size_t bytes, std::vector<Entry>& rows) {
    return ymcc::processInventory::parseDetails(data, bytes, rows);
}

// The owner alone owns the OS buffer and executes class5. This wrapper leases
// owning immutable detail rows, then exposes only stable compatibility views.
class Snapshot {
    static bool& inUse() { static thread_local bool value = false; return value; }
    bool ownsLease_ = false;
    bool valid_ = false;
    std::shared_ptr<const ymcc::processInventory::DetailSnapshot> snapshot_;
    std::vector<Entry> rows_;
public:
    explicit Snapshot(Query query = resolveQuery()) noexcept {
        ymcc::processInventory::detail::LastErrorScope restore;
        if (!query || inUse()) return;
        inUse() = true; ownsLease_ = true;
        try {
            snapshot_ = ymcc::processInventory::readDetails(query,
                ymcc::processInventory::currentTime, 0);
            if (!snapshot_ || snapshot_->status != ymcc::processInventory::Status::Complete) return;
            rows_.reserve(snapshot_->rows.size());
            for (const auto& row : snapshot_->rows)
                rows_.push_back({row.pid, row.name, row.workingSet, row.processCreated});
            valid_ = true;
        } catch (...) { valid_ = false; rows_.clear(); snapshot_.reset(); }
    }
    ~Snapshot() { if (ownsLease_) inUse() = false; }
    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;
    bool valid() const noexcept { return valid_; }
    const std::vector<Entry>& entries() const noexcept { return rows_; }
    bool creationTimesVerified() const noexcept {
        return valid_ && snapshot_ && snapshot_->creationTimesVerified;
    }
};

// Class 252 omits thread/timing/working-set collection. Its sequence is only
// a topology hint, never an admission/lease substitute for a live handle.
struct BasicEntry { DWORD pid = 0; std::wstring_view name; ULONG64 sequence = 0; DWORD parentPid = 0; };
inline bool parseBasic(const void* data, std::size_t bytes, std::vector<BasicEntry>& rows) {
    rows.clear();
    if (!data || bytes < sizeof(SYSTEM_BASICPROCESS_INFORMATION)) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(data);
    if (bytes > (std::numeric_limits<std::uintptr_t>::max)() - base) return false;
    std::size_t offset = 0;
    for (;;) {
        if (bytes - offset < sizeof(SYSTEM_BASICPROCESS_INFORMATION)) { rows.clear(); return false; }
        SYSTEM_BASICPROCESS_INFORMATION record{};
        std::memcpy(&record, static_cast<const unsigned char*>(data) + offset, sizeof(record));
        const std::size_t extent = record.NextEntryOffset ? record.NextEntryOffset : bytes - offset;
        if (extent < sizeof(record) || extent > bytes - offset ||
            (record.NextEntryOffset && extent % alignof(SYSTEM_BASICPROCESS_INFORMATION))) {
            rows.clear(); return false;
        }
        const auto pid = reinterpret_cast<std::uintptr_t>(record.UniqueProcessId);
        const auto parentPid = reinterpret_cast<std::uintptr_t>(record.InheritedFromUniqueProcessId);
        const auto name = reinterpret_cast<std::uintptr_t>(record.ImageName.Buffer);
        const auto length = static_cast<std::size_t>(record.ImageName.Length);
        const auto capacity = static_cast<std::size_t>(record.ImageName.MaximumLength);
        const auto low = base + offset + sizeof(record), high = base + offset + extent;
        if (pid > (std::numeric_limits<DWORD>::max)() || parentPid > (std::numeric_limits<DWORD>::max)() || length % sizeof(wchar_t) ||
            capacity % sizeof(wchar_t) || length > capacity ||
            (name && (name < low || name > high || capacity > high - name || name % alignof(wchar_t))) ||
            (!name && (length || capacity))) {
            rows.clear(); return false;
        }
        rows.push_back({static_cast<DWORD>(pid), name ? std::wstring_view(record.ImageName.Buffer,
            length / sizeof(wchar_t)) : std::wstring_view{}, record.SequenceNumber, static_cast<DWORD>(parentPid)});
        if (!record.NextEntryOffset) return true;
        offset += extent;
    }
}
// The one-shot wrapper only consumes the native owner. Its per-thread lease
// protects existing string_view consumers across reentrant fallback requests.
class BasicSnapshot {
    struct Storage {
        std::shared_ptr<const ymcc::processInventory::Snapshot> snapshot;
        std::vector<BasicEntry> rows;
        std::vector<ymcc::processInventory::ParentEntry> parents;
        bool inUse = false;
    };
    static Storage& storage() { static thread_local Storage value; return value; }
    Storage* owned_ = nullptr;
    bool valid_ = false;
public:
    enum class Projection { CompatibilityRows, TopologyOnly };
    explicit BasicSnapshot(Query query = resolveQuery(), Projection projection = Projection::CompatibilityRows) noexcept {
        auto& scratch = storage();
        if (!query || scratch.inUse) return;
        owned_ = &scratch; scratch.inUse = true;
        try {
            scratch.rows.clear();
            if (projection == Projection::TopologyOnly) {
                scratch.snapshot.reset();
                valid_ = ymcc::processInventory::owner().readFreshParents(query, scratch.parents);
                return;
            }
            scratch.snapshot = ymcc::processInventory::read(query,
                ymcc::processInventory::currentTime, 0, false);
            if (!scratch.snapshot || scratch.snapshot->basicStatus != ymcc::processInventory::Status::Complete) return;
            scratch.rows.reserve(scratch.snapshot->rows.size());
            for (const auto& row : scratch.snapshot->rows)
                scratch.rows.push_back({row.pid, row.name, row.sequence, row.parentPid});
            valid_ = true;
        } catch (...) { valid_ = false; scratch.rows.clear(); scratch.snapshot.reset(); }
    }
    ~BasicSnapshot() { if (owned_) owned_->inUse = false; }
    BasicSnapshot(const BasicSnapshot&) = delete;
    BasicSnapshot& operator=(const BasicSnapshot&) = delete;
    bool valid() const noexcept { return valid_; }
    const std::vector<BasicEntry>& entries() const noexcept { return owned_->rows; }
    // Borrowed only while this lease is alive. No per-row name views are needed.
    const std::vector<ymcc::processInventory::ParentEntry>* topology() const noexcept {
        return valid_ && !owned_->snapshot ? &owned_->parents : nullptr;
    }
};

// Recurring game discovery consumes the native owner's immutable Basic rows
// directly. One-shot Steam listeners above always request fresh0.
inline auto readSharedBasicSnapshot() {
    return ymcc::processInventory::read(ymcc::processInventory::resolveQuery(),
        ymcc::processInventory::currentTime, 250, false);
}

// These state objects are accessed under the existing g_gameValveMx. The
// atomic is only an invalidation ticket published by explicit IPC requests.
inline std::atomic<unsigned long long> invalidation{1};
inline thread_local unsigned manualDepth = 0;
inline void invalidate() noexcept { invalidation.fetch_add(1, std::memory_order_release); ymcc::processInventory::invalidate(); }
class ManualScope {
    bool active_;
public:
    explicit ManualScope(bool active) noexcept : active_(active) { if (active_) ++manualDepth; }
    ~ManualScope() { if (active_) --manualDepth; }
};
inline bool foreground(HWND window) noexcept {
    if (!window || !IsWindowVisible(window) || IsIconic(window)) return false;
    const HWND active = GetForegroundWindow();
    if (!active) return false;
    const HWND owner = GetAncestor(window, GA_ROOTOWNER);
    return active == window || (owner && GetAncestor(active, GA_ROOTOWNER) == owner);
}
struct RuleKey {
    std::vector<std::wstring> excludes, whitelist, configured;
    bool operator==(const RuleKey&) const = default;
};
struct OwnedEntry { DWORD pid; std::wstring name; SIZE_T workingSet; ULONGLONG processCreated = 0; ULONG64 sequence = 0; };
struct TopologyEntry {
    DWORD pid; std::wstring name; ULONG64 sequence;
    bool operator==(const TopologyEntry&) const = default;
};
using TopologyFilter = bool (*)(const BasicEntry&, const RuleKey&);
// Read-only working-set hints. AccessDenied is the same current inability to
// open a process required by the live validator, not a persistent negative.
enum class WorkingSetRead { Read, AccessDenied, Unavailable };
using WorkingSetReader = WorkingSetRead (*)(const TopologyEntry&, OwnedEntry&);
inline bool readWorkingSetHints(const std::vector<TopologyEntry>& topology,
    WorkingSetReader reader, std::vector<OwnedEntry>& rows) {
    std::vector<OwnedEntry> staged; staged.reserve(topology.size());
    for (const auto& entry : topology) {
        OwnedEntry row{};
        const auto status = reader(entry, row);
        if (status == WorkingSetRead::AccessDenied) return false;
        if (status != WorkingSetRead::Read || entry.name.empty() ||
            row.pid != entry.pid || row.name != entry.name)
            return false; // Unknown/partial data falls back to the original full receipt.
        staged.push_back(std::move(row));
    }
    rows = std::move(staged);
    return true;
}
struct Memo {
    TopologyFilter topologyFilter = nullptr;
    WorkingSetReader workingSetReader = nullptr;
    bool ready = false, available = false, wasForeground = false, resetProgress = true;
    ULONGLONG at = 0, fullAt = 0;
    bool topologyAvailable = false;
    std::vector<TopologyEntry> topology, rawTopology;
    std::vector<unsigned char> rawRelevant;
    std::string filterLocale;
    unsigned long long invalidationTicket = 0, version = 0;
    RuleKey rules;
    std::vector<OwnedEntry> rows;
};
inline const Memo& inventory(const RuleKey& rules, bool actualForeground, TopologyFilter topologyFilter = nullptr, WorkingSetReader workingSetReader = nullptr) {
    static Memo memo;
    const auto ticket = invalidation.load(std::memory_order_acquire);
    const auto now = GetTickCount64();
    const bool becameForeground = actualForeground && !memo.wasForeground;
    memo.wasForeground = actualForeground;
    const bool mandatory = !memo.ready || !(memo.rules == rules) || memo.invalidationTicket != ticket || becameForeground ||
        memo.topologyFilter != topologyFilter || memo.workingSetReader != workingSetReader;
    if (mandatory || now - memo.at >= topologyIntervalMs) {
        const auto basic = readSharedBasicSnapshot();
        const bool basicValid = basic->basicStatus == ymcc::processInventory::Status::Complete;
        using BasicRow = ymcc::processInventory::Entry;
        std::vector<const BasicRow*> order;
        if (basicValid) {
            order.reserve(basic->rows.size());
            for (const auto& row : basic->rows) order.push_back(&row);
            std::sort(order.begin(), order.end(), [](const BasicRow* a, const BasicRow* b) { return a->pid < b->pid; });
        }
        const bool rawUnchanged = basicValid && memo.topologyAvailable &&
            std::equal(order.begin(), order.end(), memo.rawTopology.begin(), memo.rawTopology.end(),
                [](const BasicRow* row, const TopologyEntry& old) {
                    return row->pid == old.pid && row->name == old.name && row->sequence == old.sequence;
                });
        std::vector<TopologyEntry> rawTopology;
        if (basicValid && !rawUnchanged) {
            rawTopology.reserve(order.size());
            for (const auto* row : order) rawTopology.push_back({row->pid, row->name, row->sequence});
        }
        // Stable raw topology needs no repeated name normalization or rule
        // matching. Mandatory requests must still recompute the projection.
        const bool reuseProjection = !mandatory && rawUnchanged;
        std::vector<TopologyEntry> topology;
        std::vector<unsigned char> relevant;
        std::string filterLocale;
        if (!reuseProjection) {
            const auto& raw = rawUnchanged ? memo.rawTopology : rawTopology;
            topology.reserve(raw.size());
            relevant.reserve(raw.size());
            if (const auto* locale = std::setlocale(LC_CTYPE, nullptr)) filterLocale = locale;
            // This is a name hint only, never a cached live process decision.
            const bool reuseRows = !mandatory && memo.topologyAvailable &&
                !filterLocale.empty() && filterLocale == memo.filterLocale &&
                memo.rawRelevant.size() == memo.rawTopology.size();
            const auto project = [&](bool reuse) {
                size_t prior = 0;
                for (const auto& entry : raw) {
                    while (reuse && prior < memo.rawTopology.size() && memo.rawTopology[prior].pid < entry.pid) ++prior;
                    const bool unchangedRow = reuse && prior < memo.rawTopology.size() && memo.rawTopology[prior] == entry;
                    const bool selected = unchangedRow ? memo.rawRelevant[prior++] != 0 :
                        !topologyFilter || topologyFilter({entry.pid, entry.name, entry.sequence, 0}, rules);
                    relevant.push_back(selected);
                    if (selected) topology.push_back(entry);
                }
            };
            project(reuseRows);
            const auto* localeAfter = std::setlocale(LC_CTYPE, nullptr);
            if (!localeAfter || filterLocale != localeAfter) {
                filterLocale.clear();
                if (reuseRows) {
                    relevant.clear(); topology.clear();
                    project(false);
                }
            }
        }
        const bool unchanged = basicValid && memo.topologyAvailable && (reuseProjection || topology == memo.topology);
        memo.at = now;
        if (!rawUnchanged) memo.rawTopology = std::move(rawTopology);
        if (!reuseProjection) {
            memo.rawRelevant = std::move(relevant);
            memo.filterLocale = std::move(filterLocale);
        }
        // Unknown Basic data is never a confirmed empty topology. Foreground
        // uses the original 5s full cadence; background has a 30s WS backstop.
        if (!mandatory && !actualForeground && unchanged && memo.available && now - memo.fullAt < 30000)
            return memo;
        const bool periodicWorkingSetRefresh = memo.available && now >= memo.fullAt && now - memo.fullAt >= 30000;
        const bool topologyWorkingSetRefresh = memo.available && memo.topologyAvailable && basicValid && !unchanged;
        memo.resetProgress = mandatory;
        memo.ready = true; memo.available = false; memo.fullAt = now;
        memo.invalidationTicket = ticket; memo.rules = rules;
        memo.topologyFilter = topologyFilter; memo.workingSetReader = workingSetReader; ++memo.version;
        memo.topologyAvailable = basicValid;
        if (!reuseProjection) memo.topology = std::move(topology);
        memo.rows.clear();
        // Existing complete receipts may refresh working-set hints from a
        // complete Basic topology, including relevant background topology changes.
        // Startup, rules/invalidation, foreground/manual, unavailable Basic and
        // failed-source recovery retain the original complete-snapshot path.
        const bool selective = workingSetReader && !mandatory && !actualForeground &&
            manualDepth == 0 && basicValid &&
            ((periodicWorkingSetRefresh && unchanged) || topologyWorkingSetRefresh);
        if (selective && readWorkingSetHints(memo.topology, workingSetReader, memo.rows)) {
            // A reused Basic table cannot acquire a later absence-proof age.
            memo.fullAt = (std::min)(now, basic->observedAt);
            memo.available = true;
        } else {
        Snapshot snapshot;
        if (snapshot.valid() && (snapshot.creationTimesVerified() || basicValid)) {
            memo.rows.reserve(snapshot.entries().size());
            for (const auto& entry : snapshot.entries()) {
                const auto hint = std::lower_bound(memo.topology.begin(), memo.topology.end(), entry.pid,
                    [](const TopologyEntry& value, DWORD pid) { return value.pid < pid; });
                const ULONG64 sequence = hint != memo.topology.end() && hint->pid == entry.pid && hint->name == entry.name ? hint->sequence : 0;
                memo.rows.push_back({entry.pid, std::wstring(entry.name), entry.workingSet, entry.processCreated, sequence});
            }
            memo.available = true;
        }
        }
        // Full-snapshot failure remains unavailable. Each scanner's exact
        // Toolhelp fallback is retried only in a new discovery epoch.
    }
    return memo;
}

struct Hint {
    DWORD pid = 0; std::wstring name; SIZE_T workingSet = 0; unsigned group = 0;
    ULONGLONG expectedBirth = 0; std::wstring expectedPath;
    ULONGLONG snapshotBirth = 0; ULONG64 snapshotSequence = 0;
};
struct RejectedHint {
    DWORD pid; std::wstring name; SIZE_T workingSet; unsigned group;
    ULONGLONG snapshotBirth; ULONG64 snapshotSequence;
    bool matches(const Hint& hint) const noexcept {
        // No durable negative proof exists without a stable process instance.
        return (snapshotBirth || snapshotSequence) && pid == hint.pid && name == hint.name &&
            workingSet == hint.workingSet && group == hint.group &&
            snapshotBirth == hint.snapshotBirth && snapshotSequence == hint.snapshotSequence;
    }
};
struct Candidates {
    unsigned long long version = 0;
    // Cached qualification keeps its original age across discovery heartbeats.
    ULONGLONG qualificationObservedAt = 0;
    std::vector<Hint> ranked;
    std::size_t cursor = 0, windowEnd = 0;
    bool complete = false;
    bool sourceAvailable = true;
    bool topologyChanged = false;
    bool candidateUnconfirmed = false;
    bool hasUnconfirmed = false;
    std::vector<RejectedHint> rejected;
    void reset(unsigned long long value, bool clearProgress) {
        if (clearProgress || complete) rejected.clear();
        version = value; qualificationObservedAt = 0;
        ranked.clear(); cursor = windowEnd = 0; complete = false;
        sourceAvailable = true; topologyChanged = false;
        candidateUnconfirmed = false; hasUnconfirmed = false;
    }
    void rank() {
        // Keep rejected hints across ordinary TTL refreshes until this pass
        // completes; otherwise a slow cursor would restart forever at rank 1.
        ranked.erase(std::remove_if(ranked.begin(), ranked.end(), [&](const Hint& hint) {
            return std::any_of(rejected.begin(), rejected.end(), [&](const RejectedHint& value) { return value.matches(hint); });
        }), ranked.end());
        std::stable_sort(ranked.begin(), ranked.end(), [](const Hint& a, const Hint& b) {
            if (a.group != b.group) return a.group < b.group;
            return a.workingSet > b.workingSet;
        });
        windowEnd = (std::min)(ranked.size(), std::size_t(8));
    }
};
inline ULONGLONG backgroundNextReadAt = 0; // shared by both serialized scanners

template<class Game, class Validate>
Game next(Candidates& candidates, bool fast, ULONGLONG started, Validate validate) {
    unsigned checked = 0;
    while (candidates.cursor < candidates.ranked.size()) {
        // One synchronous Windows API cannot be preempted. The 2ms budget is
        // checked between candidates and also after an indivisible snapshot.
        if (!fast && (checked >= 1 || GetTickCount64() - started >= 2)) return {};
        if (candidates.cursor == candidates.windowEnd)
            candidates.windowEnd = (std::min)(candidates.ranked.size(), candidates.windowEnd + 8);
        const auto now = GetTickCount64();
        if (!fast && now < backgroundNextReadAt) return {};
        if (!fast) backgroundNextReadAt = now + 250;
        auto& hint = candidates.ranked[candidates.cursor];
        ++checked;
        Game live;
        candidates.candidateUnconfirmed = false;
        const bool accepted = validate(hint, live);
        const bool changedIdentity = accepted && ((hint.expectedBirth && hint.expectedBirth != live.processCreated) ||
            (!hint.expectedPath.empty() && !live.path.empty() && _wcsicmp(hint.expectedPath.c_str(), live.path.c_str()) != 0));
        if (candidates.topologyChanged || changedIdentity) {
            candidates.topologyChanged = true;
            invalidate();
            return {}; // A stale inventory is pending, never a completed idle proof.
        }
        if (!accepted) {
            if (candidates.candidateUnconfirmed) {
                candidates.hasUnconfirmed = true;
                ++candidates.cursor;
                continue;
            }
            candidates.rejected.push_back({hint.pid, hint.name, hint.workingSet, hint.group, hint.snapshotBirth, hint.snapshotSequence});
            ++candidates.cursor;
            continue;
        }
        // The pool was only a hint. Only this fresh same-handle validation can
        // establish identity; subsequent checks must keep that birth/path.
        hint.expectedBirth = live.processCreated;
        hint.expectedPath = live.path;
        return live;
    }
    candidates.complete = !candidates.hasUnconfirmed;
    return {};
}
} // namespace ymcc::gameInventory
