// Pure, bounded diagnostic evidence. Ops supplies all external observations.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "json.hpp"

namespace ymcc::keyboardOrigin {
using json = nlohmann::json;
inline constexpr std::size_t capacity = 8;
inline constexpr std::size_t maxPathChars = 1023;
inline constexpr std::size_t maxNodes = 6; // Per interface, including its resolved node.
inline constexpr std::size_t maxStringBytes = 4096;
inline constexpr std::uint64_t maxQueryOperations = 160;
inline constexpr std::uint64_t checkpointBudgetMs = 300;

struct Observation {
    std::wstring path;
    std::uint64_t firstEventTick = 0, lastEventTick = 0, lastQueryTick = 0;
    std::uint64_t firstGeneration = 0, lastGeneration = 0, observations = 0;
    std::uintptr_t lastDevice = 0;
    bool mixedGeneration = false, mixedDevice = false;
};
struct Snapshot {
    std::vector<Observation> observations;
    bool known = false;
    std::uint64_t droppedContention = 0, rejected = 0, evicted = 0;
    // Additive diagnostic: observe failures also increment rejected. A failed
    // snapshot copy returns known=false and no observations, without draining.
    std::uint64_t allocationFailures = 0;
};
struct ResolveResult { bool known; std::uint32_t node, error; };
struct StringResult { bool known; std::uint32_t error; std::string value; };
struct ParentResult { bool known, root; std::uint32_t node, error; };

namespace detail {
struct CacheTestAccess;
inline void increment(std::atomic<std::uint64_t>& counter) noexcept {
    counter.fetch_add(1, std::memory_order_relaxed);
}
inline void increment(std::uint64_t& value) noexcept {
    if (value != (std::numeric_limits<std::uint64_t>::max)()) ++value;
}
inline bool validPath(const std::wstring& path) noexcept {
    if (path.size() <= 4 || path.size() > maxPathChars ||
        path[0] != L'\\' || path[1] != L'\\' || path[2] != L'?' || path[3] != L'\\') return false;
    for (wchar_t ch : path) {
        const auto value = static_cast<std::uint32_t>(ch);
        if (value < 0x20 || (value >= 0x7f && value <= 0x9f)) return false;
    }
    return true;
}
inline wchar_t fold(wchar_t ch) noexcept {
    return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch + (L'a' - L'A')) : ch;
}
inline bool samePath(const std::wstring& a, const std::wstring& b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (fold(a[i]) != fold(b[i])) return false;
    return true;
}
// Swap existing storage rather than constructing a temporary string; this also
// avoids MSVC checked-iterator proxy allocations on duplicate observations.
inline void swapObservation(Observation& a, Observation& b) noexcept {
    using std::swap;
    a.path.swap(b.path);
    swap(a.firstEventTick, b.firstEventTick); swap(a.lastEventTick, b.lastEventTick);
    swap(a.lastQueryTick, b.lastQueryTick); swap(a.firstGeneration, b.firstGeneration);
    swap(a.lastGeneration, b.lastGeneration); swap(a.observations, b.observations);
    swap(a.lastDevice, b.lastDevice); swap(a.mixedGeneration, b.mixedGeneration);
    swap(a.mixedDevice, b.mixedDevice);
}
struct TryLock {
    std::atomic_flag& flag;
    bool owns;
    explicit TryLock(std::atomic_flag& value) noexcept
        : flag(value), owns(!flag.test_and_set(std::memory_order_acquire)) {}
    ~TryLock() { if (owns) flag.clear(std::memory_order_release); }
    TryLock(const TryLock&) = delete;
    TryLock& operator=(const TryLock&) = delete;
};
} // namespace detail

class Cache {
    friend struct detail::CacheTestAccess;
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::array<Observation, capacity> entries_{};
    std::size_t size_ = 0;
    std::atomic<std::uint64_t> droppedContention_{0}, rejected_{0}, evicted_{0}, allocationFailures_{0};
    void counters(Snapshot& out) const noexcept {
        out.droppedContention = droppedContention_.load(std::memory_order_relaxed);
        out.rejected = rejected_.load(std::memory_order_relaxed);
        out.evicted = evicted_.load(std::memory_order_relaxed);
        out.allocationFailures = allocationFailures_.load(std::memory_order_relaxed);
    }
public:
    // One lock attempt; no waiting for a cache owner. Recency is observation
    // arrival order, independent of caller clocks. Duplicate hits do not allocate.
    void observe(const std::wstring& path, std::uintptr_t device, std::uint64_t generation,
                 std::uint64_t eventTick, std::uint64_t queryTick) noexcept {
        if (!detail::validPath(path)) { detail::increment(rejected_); return; }
        detail::TryLock lock(lock_);
        if (!lock.owns) { detail::increment(droppedContention_); return; }
        std::size_t index = 0;
        for (; index < size_; ++index) if (detail::samePath(entries_[index].path, path)) break;
        if (index < size_) {
            auto& latest = entries_[index];
            latest.mixedGeneration = latest.mixedGeneration || latest.lastGeneration != generation;
            latest.mixedDevice = latest.mixedDevice || latest.lastDevice != device;
            latest.lastEventTick = eventTick; latest.lastQueryTick = queryTick;
            latest.lastGeneration = generation; latest.lastDevice = device;
            detail::increment(latest.observations);
            for (std::size_t i = index; i + 1 < size_; ++i)
                detail::swapObservation(entries_[i], entries_[i + 1]);
            return;
        }
        try {
            // Finish the only potentially throwing write before eviction.
            Observation latest{path, eventTick, eventTick, queryTick, generation, generation, 1, device, false, false};
            if (size_ == capacity) {
                for (std::size_t i = 0; i + 1 < size_; ++i) entries_[i] = std::move(entries_[i + 1]);
                detail::increment(evicted_);
            } else ++size_;
            entries_[size_ - 1] = std::move(latest);
        } catch (...) {
            detail::increment(rejected_); detail::increment(allocationFailures_);
        }
    }
    Snapshot snapshot() noexcept {
        Snapshot out;
        detail::TryLock lock(lock_);
        if (!lock.owns) {
            detail::increment(droppedContention_); counters(out); return out;
        }
        try {
            out.observations.assign(entries_.begin(), entries_.begin() + size_);
            out.known = true;
        } catch (...) {
            out.observations.clear(); detail::increment(allocationFailures_);
        }
        counters(out); return out;
    }
};

namespace detail {
// JSON strings must be bounded, valid UTF-8 and free of NUL/control characters.
// Invalid bytes are never copied into output, even if the adapter claims known.
inline bool validString(const std::string& value, bool nonempty = false) noexcept {
    if (value.size() > maxStringBytes || (nonempty && value.empty())) return false;
    std::size_t i = 0;
    while (i < value.size()) {
        const auto first = static_cast<unsigned char>(value[i++]);
        std::uint32_t cp = first;
        unsigned remaining = 0;
        if (first >= 0xc2 && first <= 0xdf) { cp = first & 0x1f; remaining = 1; }
        else if (first >= 0xe0 && first <= 0xef) { cp = first & 0x0f; remaining = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { cp = first & 7; remaining = 3; }
        else if (first >= 0x80) return false;
        const auto width = remaining;
        if (remaining > value.size() - i) return false;
        while (remaining--) {
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (next & 0x3f);
        }
        if ((width == 1 && cp < 0x80) || (width == 2 && cp < 0x800) ||
            (width == 3 && cp < 0x10000) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff) || cp < 0x20 || (cp >= 0x7f && cp <= 0x9f)) return false;
    }
    return true;
}
inline constexpr std::array<const char*, 6> propertyNames{
    "service", "driverInf", "driverProvider", "manufacturer", "description", "enumerator"
};
inline json stringSlot() {
    return {{"known", false}, {"status", "notQueried"}, {"error", nullptr}, {"value", nullptr}};
}
enum class Outcome { Value, Exception, Stopped };

template<class Ops> struct Collector {
    Ops& ops;
    json& out;
    std::uint64_t start = 0, last = 0, queries = 0;
    const char* stop = nullptr;
    bool partial = false;
    bool checkpoint() {
        if (stop) return false;
        std::uint64_t tick = 0;
        try { tick = ops.now(); }
        catch (const std::bad_alloc&) { stop = "allocationFailure"; return false; }
        catch (...) { stop = "exception"; out.at("hadExceptions") = true; return false; }
        if (tick < last) { stop = "clockRegression"; return false; }
        last = tick;
        out.at("elapsedMs") = tick - start;
        if (tick - start >= checkpointBudgetMs) { stop = "timeBudget"; return false; }
        return true;
    }
    template<class Result, class Fn> Outcome invoke(Result& result, Fn&& fn, bool query = true) {
        if (!checkpoint()) return Outcome::Stopped;
        if (query) {
            if (queries == maxQueryOperations) { stop = "operationBudget"; return Outcome::Stopped; }
            out.at("queryOperations") = ++queries;
        }
        try { result = fn(); return Outcome::Value; }
        catch (const std::bad_alloc&) { stop = "allocationFailure"; return Outcome::Stopped; }
        catch (...) { partial = true; out.at("hadExceptions") = true; return Outcome::Exception; }
    }
    bool readString(json& slot, std::uint32_t node, const char* name) {
        StringResult result{};
        const auto outcome = invoke(result, [&]() {
            return name ? ops.property(node, name) : ops.instance(node);
        });
        if (outcome == Outcome::Stopped) { slot.at("status") = stop; return false; }
        if (outcome == Outcome::Exception) { slot.at("status") = "exception"; return true; }
        slot.at("error") = result.error;
        const bool valid = result.known && validString(result.value, !name);
        slot.at("known") = valid;
        slot.at("status") = !result.known ? "unknown" : !valid ? "malformedString" : "known";
        if (valid) slot.at("value") = std::move(result.value);
        else partial = true;
        return true;
    }
    void run() {
        start = last = ops.now();
        Snapshot snapshot;
        const auto snapshotOutcome = invoke(snapshot, [&]() { return ops.snapshot(); }, false);
        if (snapshotOutcome != Outcome::Value) {
            if (!stop) stop = "exception";
            finish(); return;
        }
        out.at("known") = snapshot.known;
        out.at("snapshot") = {{"known", snapshot.known}, {"status", snapshot.known ? "known" : "unknown"},
            {"count", snapshot.observations.size()}, {"droppedContention", snapshot.droppedContention},
            {"rejected", snapshot.rejected}, {"evicted", snapshot.evicted},
            {"allocationFailures", snapshot.allocationFailures}};
        if (!snapshot.known) {
            out.at("status") = "unknown"; out.at("stopReason") = "snapshotUnknown";
            checkpoint(); if (stop) finish(); return;
        }
        if (snapshot.observations.size() > capacity) {
            partial = true; out.at("interfacesTruncated") = true;
        }
        auto& rows = out.at("interfaces");
        const auto count = (std::min)(snapshot.observations.size(), capacity);
        for (std::size_t i = 0; i < count && !stop; ++i) {
            const auto& observation = snapshot.observations[i];
            rows.push_back({{"path", nullptr}, {"status", "partial"}, {"known", false},
                {"firstEventTick", observation.firstEventTick}, {"lastEventTick", observation.lastEventTick},
                {"lastQueryTick", observation.lastQueryTick}, {"firstGeneration", observation.firstGeneration},
                {"lastGeneration", observation.lastGeneration}, {"observations", observation.observations},
                {"lastDevice", observation.lastDevice}, {"mixedGeneration", observation.mixedGeneration},
                {"mixedDevice", observation.mixedDevice}, {"resolve", {{"status", "notQueried"},
                    {"known", false}, {"node", nullptr}, {"error", nullptr}}},
                {"ancestryStatus", "notQueried"}, {"nodes", json::array()}});
            auto& row = rows.back();
            if (!validPath(observation.path) || observation.observations == 0) {
                partial = true; row.at("status") = "malformedObservation"; continue;
            }
            bool duplicate = false;
            for (std::size_t k = 0; k < i; ++k)
                if (samePath(snapshot.observations[k].path, observation.path)) duplicate = true;
            if (duplicate) { partial = true; row.at("status") = "duplicatePath"; continue; }
            std::string path;
            auto outcome = invoke(path, [&]() { return ops.utf8(observation.path); }, false);
            if (outcome == Outcome::Stopped) { row.at("status") = stop; break; }
            if (outcome == Outcome::Exception) { row.at("status") = "exception"; continue; }
            if (!validString(path, true) || path.size() <= 4 || path.compare(0, 4, "\\\\?\\") != 0) { partial = true; row.at("status") = "malformedPathEncoding"; continue; }
            row.at("path") = std::move(path);
            ResolveResult resolved{};
            outcome = invoke(resolved, [&]() { return ops.resolve(observation.path); });
            if (outcome == Outcome::Stopped) { row.at("status") = stop; break; }
            if (outcome == Outcome::Exception) {
                row.at("resolve").at("status") = "exception"; row.at("status") = "exception"; continue;
            }
            const bool valid = resolved.known && resolved.node != 0;
            row.at("resolve") = {{"known", valid}, {"node", resolved.node}, {"error", resolved.error},
                {"status", !resolved.known ? "unknown" : !valid ? "malformedNode" : "known"}};
            if (!valid) { partial = true; row.at("status") = resolved.known ? "malformedNode" : "unresolved"; continue; }
            row.at("known") = true;
            std::array<std::uint32_t, maxNodes> visited{};
            std::uint32_t node = resolved.node;
            bool rowPartial = false;
            auto& nodes = row.at("nodes");
            for (std::size_t depth = 0; depth < maxNodes && !stop; ++depth) {
                visited[depth] = node;
                json properties = json::object();
                for (const char* name : propertyNames) properties[name] = stringSlot();
                nodes.push_back({{"node", node}, {"status", "partial"}, {"instance", stringSlot()},
                    {"properties", std::move(properties)}, {"parent", {{"known", false}, {"root", false},
                        {"node", nullptr}, {"error", nullptr}, {"status", "notQueried"}}}});
                auto& entry = nodes.back();
                bool nodeComplete = readString(entry.at("instance"), node, nullptr);
                if (!entry.at("instance").at("known").template get<bool>()) rowPartial = true;
                for (const char* name : propertyNames) {
                    if (!nodeComplete) break;
                    nodeComplete = readString(entry.at("properties").at(name), node, name);
                    if (!entry.at("properties").at(name).at("known").template get<bool>()) rowPartial = true;
                }
                if (!nodeComplete) break;
                ParentResult parent{};
                outcome = invoke(parent, [&]() { return ops.parent(node); });
                if (outcome == Outcome::Stopped) { entry.at("parent").at("status") = stop; break; }
                if (outcome == Outcome::Exception) {
                    entry.at("parent").at("status") = "exception";
                    row.at("ancestryStatus") = "exception"; rowPartial = true; break;
                }
                // root is an explicit Ops assertion, never inferred from an
                // unknown parent or an error code. Preserve its raw fields.
                const bool malformed = (parent.root && parent.node != 0) ||
                    (parent.known && !parent.root && parent.node == 0);
                const char* parentStatus = malformed ? "malformedRoot" : parent.root ? "root" :
                    !parent.known ? "unknown" : "known";
                entry.at("parent") = {{"known", parent.known}, {"root", parent.root},
                    {"node", parent.node}, {"error", parent.error}, {"status", parentStatus}};
                bool stringsKnown = entry.at("instance").at("known").template get<bool>();
                for (const char* name : propertyNames)
                    stringsKnown = stringsKnown && entry.at("properties").at(name).at("known").template get<bool>();
                entry.at("status") = stringsKnown && !malformed && (parent.known || parent.root) ? "complete" : "partial";
                if (malformed || (!parent.known && !parent.root)) {
                    row.at("ancestryStatus") = parentStatus; rowPartial = true; break;
                }
                if (parent.root) { row.at("ancestryStatus") = "root"; break; }
                bool cycle = false;
                for (std::size_t k = 0; k <= depth; ++k) if (visited[k] == parent.node) cycle = true;
                if (cycle) { row.at("ancestryStatus") = "cycle"; rowPartial = true; break; }
                if (depth + 1 == maxNodes) { row.at("ancestryStatus") = "depthLimit"; rowPartial = true; break; }
                node = parent.node;
            }
            if (stop) { row.at("ancestryStatus") = stop; row.at("status") = stop; }
            else row.at("status") = rowPartial ? "partial" : "complete";
            partial = partial || rowPartial;
        }
        checkpoint(); finish();
    }
    void finish() {
        if (stop) {
            out.at("status") = "partial"; out.at("stopReason") = stop;
            if (std::string_view(stop) == "allocationFailure") out.at("allocationFailure") = true;
        } else {
            out.at("status") = partial ? "partial" : "complete";
            out.at("complete") = !partial;
        }
    }
};
} // namespace detail

// queryOperations counts only resolve/instance/property/parent calls, including
// calls that throw. Snapshot/clock/UTF-8 conversion are not device queries.
// No operation is preempted: the time limit is checked BETWEEN Ops operations.
// Metadata describes the current query only and never proves event-time identity
// or the sender of an injected event. Complete means this bounded query finished,
// not that the cache retained every event (see its loss/eviction counters).
// If allocation fails before even the status object can be built, null is the
// allocation-free unavailable sentinel; never substitute an empty success.
template<class Ops> json collect(bool enabled, Ops& ops) noexcept {
    json out;
    try {
        out = {{"schema", 1}, {"enabled", enabled}, {"readOnly", true}, {"known", false},
            {"status", "allocationFailure"}, {"complete", false}, {"stopReason", nullptr},
            {"metadataScope", "current-at-query-time"}, {"historicalPathIdentity", false},
            {"eventDriverOwner", false}, {"injectorPID", false}, {"allocationFailure", false},
            {"hadExceptions", false}, {"interfacesTruncated", false}, {"queryOperations", 0}, {"elapsedMs", 0},
            {"limits", {{"interfaces", capacity}, {"nodesPerInterface", maxNodes},
                {"queryOperations", maxQueryOperations}, {"checkpointMs", checkpointBudgetMs},
                {"hardIoTimeout", false}}},
            {"snapshot", {{"known", false}, {"status", "notQueried"}}}, {"interfaces", json::array()}};
        if (!enabled) { out.at("status") = "disabled"; return out; }
        detail::Collector<Ops>{ops, out}.run();
    } catch (const std::bad_alloc&) {
        if (out.is_object()) {
            out.at("allocationFailure") = true; out.at("complete") = false;
            try { out.at("status") = "partial"; out.at("stopReason") = "allocationFailure"; } catch (...) {}
        }
    } catch (...) {
        if (out.is_object()) {
            out.at("complete") = false; out.at("hadExceptions") = true;
            try { out.at("status") = "partial"; out.at("stopReason") = "exception"; } catch (...) {}
        }
    }
    return out;
}
} // namespace ymcc::keyboardOrigin
