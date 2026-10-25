// Inert synthetic tests: no Windows headers, APIs, live devices or product code.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>
#include <stdexcept>
#include <type_traits>
#include "keyboard_device_origin_evidence.h"

namespace ko = ymcc::keyboardOrigin;
using Json = ko::json;
static std::atomic<long long> failAfter{-1};
static std::atomic<unsigned long long> allocationAttempts{0};
static std::atomic<bool> persistentFailure{false};
void* operator new(std::size_t size) {
    allocationAttempts.fetch_add(1, std::memory_order_relaxed);
    // MSVC checked iterators allocate bookkeeping proxies inside noexcept
    // container constructors. Exclude proxy-sized allocations from injected
    // payload faults; real allocation is still performed. Release has no such
    // exemption. Path buffers, vector storage and JSON nodes remain faultable.
    bool faultable = true;
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL > 0
    faultable = size != sizeof(std::_Container_proxy);
#endif
    auto remaining = failAfter.load(std::memory_order_relaxed);
    if (faultable && remaining >= 0 && failAfter.fetch_sub(1, std::memory_order_relaxed) == 0) {
        if (persistentFailure.load()) failAfter.store(0);
        throw std::bad_alloc();
    }
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }
static void restoreAllocation() { failAfter.store(-1); persistentFailure.store(false); }

namespace ymcc::keyboardOrigin::detail {
struct CacheTestAccess {
    static void hold(Cache& cache) { cache.lock_.test_and_set(std::memory_order_acquire); }
    static void release(Cache& cache) { cache.lock_.clear(std::memory_order_release); }
    static void saturateObservation(Cache& cache) {
        cache.entries_[0].observations = (std::numeric_limits<std::uint64_t>::max)();
    }
};
}
static unsigned checks = 0, failures = 0, cases = 0;
static void check(bool condition, const char* name) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAILED: %s\n", name); }
}
static std::wstring path(unsigned index = 1) { return L"\\\\?\\HID#TEST#" + std::to_wstring(index); }
static ko::Observation observation(unsigned index = 1) {
    return {path(index), 10, 20, 30, 1, 2, 3, 123, true, true};
}
struct FakeOps {
    ko::Snapshot data{{observation()}, true, 2, 3, 4, 5};
    ko::ResolveResult resolveValue{true, 1, 0};
    ko::StringResult instanceValue{true, 0, "ROOT\\SYNTHETIC"};
    ko::StringResult propertyValue{true, 0, "current synthetic metadata"};
    ko::ParentResult parentValue{false, true, 0, 13};
    std::vector<ko::ParentResult> parents;
    std::vector<std::uint64_t> ticks;
    std::vector<std::string> trace;
    std::size_t tickIndex = 0;
    std::uint64_t tick = 100;
    unsigned snapshots = 0, clocks = 0, conversions = 0, queries = 0;
    unsigned resolveCalls = 0, instanceCalls = 0, propertyCalls = 0, parentCalls = 0;
    std::string throwOperation;
    unsigned throwAt = 1, matchingCalls = 0;
    bool allocationException = false, persistentAllocationException = false;
    bool chain = false, invalidProperty = false;
    std::string encodedOverride;
    bool overrideEncoding = false;
    void hit(const char* operation) {
        trace.emplace_back(operation);
        if (throwOperation == operation && ++matchingCalls == throwAt) {
            if (persistentAllocationException) {
                persistentFailure.store(true); failAfter.store(0); throw std::bad_alloc();
            }
            if (allocationException) throw std::bad_alloc();
            throw std::runtime_error("synthetic operation failure");
        }
    }
    ko::Snapshot snapshot() { ++snapshots; hit("snapshot"); return data; }
    std::uint64_t now() {
        ++clocks; hit("now");
        if (tickIndex < ticks.size()) tick = ticks[tickIndex++];
        return tick;
    }
    std::string utf8(const std::wstring& wide) {
        ++conversions; hit("utf8");
        if (overrideEncoding) return encodedOverride;
        std::string value;
        for (const auto ch : wide) value.push_back(ch < 128 ? static_cast<char>(ch) : '?');
        return value;
    }
    ko::ResolveResult resolve(const std::wstring&) {
        ++queries; ++resolveCalls; hit("resolve"); return resolveValue;
    }
    ko::StringResult instance(std::uint32_t) {
        ++queries; ++instanceCalls; hit("instance"); return instanceValue;
    }
    ko::StringResult property(std::uint32_t, const char* fixedName) {
        ++queries; ++propertyCalls; hit(fixedName);
        bool allowed = false;
        for (const char* name : {"service", "driverInf", "driverProvider", "manufacturer", "description", "enumerator"})
            if (std::string(name) == fixedName) allowed = true;
        invalidProperty = invalidProperty || !allowed;
        return propertyValue;
    }
    ko::ParentResult parent(std::uint32_t node) {
        ++queries; ++parentCalls; hit("parent");
        if (node > 0 && node <= parents.size()) return parents[node - 1];
        if (chain) return {true, false, node + 1, 0};
        return parentValue;
    }
};
static const Json& row(const Json& value, std::size_t index = 0) { return value.at("interfaces").at(index); }
static const Json& node(const Json& value, std::size_t index = 0, std::size_t depth = 0) {
    return row(value, index).at("nodes").at(depth);
}
static void proof(const Json& value) {
    check(value.at("readOnly") == true && value.at("metadataScope") == "current-at-query-time" &&
        value.at("historicalPathIdentity") == false && value.at("eventDriverOwner") == false &&
        value.at("injectorPID") == false, "scope and all non-attribution claims remain explicit");
}
static void bounds(const Json& value, const FakeOps& ops) {
    check(value.at("queryOperations") == ops.queries && ops.queries <= 160 &&
        value.at("interfaces").size() <= 8 && !ops.invalidProperty, "logical operation accounting, interface cap and property allowlist");
    bool capped = true;
    for (const auto& item : value.at("interfaces")) capped = capped && item.at("nodes").size() <= 6;
    check(capped, "each ancestor chain is bounded to six nodes");
}

static void cacheTests() {
    static_assert(noexcept(std::declval<ko::Cache&>().observe(std::declval<const std::wstring&>(), 0, 0, 0, 0)));
    static_assert(noexcept(std::declval<ko::Cache&>().snapshot()));
    static_assert(ko::capacity == 8 && ko::maxPathChars == 1023);
    {
        ++cases; ko::Cache cache;
        auto empty = cache.snapshot();
        check(empty.known && empty.observations.empty() && !empty.rejected && !empty.evicted, "empty cache is a known empty snapshot");
        cache.observe(path(), 10, 20, 30, 40);
        const auto first = cache.snapshot();
        const auto& value = first.observations.at(0);
        check(first.known && value.path == path() && value.firstEventTick == 30 && value.lastEventTick == 30 &&
            value.lastQueryTick == 40 && value.firstGeneration == 20 && value.lastGeneration == 20 &&
            value.observations == 1 && value.lastDevice == 10 && !value.mixedDevice && !value.mixedGeneration,
            "first observation preserves all caller-supplied fields");
        auto upper = path(); upper[4] = L'h';
        cache.observe(upper, 10, 20, 29, 39);
        auto same = cache.snapshot();
        check(same.observations.size() == 1 && same.observations[0].observations == 2 &&
            same.observations[0].path == path() && same.observations[0].lastEventTick == 29 &&
            same.observations[0].firstEventTick == 30 && !same.observations[0].mixedGeneration,
            "ASCII folding deduplicates; clock values are observations, not recency sort keys");
        cache.observe(path(), 11, 21, 50, 60); cache.observe(path(), 10, 20, 70, 80);
        auto mixed = cache.snapshot();
        const auto& m = mixed.observations[0];
        check(m.observations == 4 && m.firstGeneration == 20 && m.lastGeneration == 20 &&
            m.lastDevice == 10 && m.lastQueryTick == 80 && m.mixedDevice && m.mixedGeneration,
            "mixed generation/device flags are sticky even when values return to the first values");
        check(first.observations[0].observations == 1 && cache.snapshot().observations[0].observations == 4,
            "snapshots own their data and never drain the cache");
    }
    {
        ++cases; ko::Cache cache;
        std::vector<std::wstring> invalid{L"", L"\\\\?\\", L"HID#X", L"\\\\.\\X", path() + std::wstring(1024, L'x'),
            path() + std::wstring(1, L'\0'), path() + L"\n", path() + L"\t", path() + wchar_t(0x7f), path() + wchar_t(0x85)};
        for (const auto& item : invalid) cache.observe(item, 0, 0, 0, 0);
        const auto value = cache.snapshot();
        check(value.known && value.observations.empty() && value.rejected == invalid.size(), "invalid prefix, prefix-only, oversized, NUL and control paths are rejected");
        const auto maximum = L"\\\\?\\" + std::wstring(1019, L'x');
        cache.observe(maximum, 0, 0, 0, 0);
        check(cache.snapshot().observations.at(0).path.size() == 1023, "1023-character interface path is accepted including zero handles/ticks");
    }
    {
        ++cases; ko::Cache cache;
        cache.observe(L"\\\\?\\\u00c4", 1, 1, 1, 1); cache.observe(L"\\\\?\\\u00e4", 1, 1, 1, 1);
        check(cache.snapshot().observations.size() == 2, "non-ASCII codepoints are never locale-folded");
    }
    {
        ++cases; ko::Cache cache;
        for (unsigned i = 1; i <= 8; ++i) cache.observe(path(i), i, 1, 100 - i, i);
        cache.observe(path(1), 1, 1, 0, 0); cache.observe(path(9), 9, 1, 0, 0);
        auto value = cache.snapshot();
        check(value.known && value.evicted == 1 && value.observations.size() == 8 &&
            value.observations.front().path == path(3) && value.observations[6].path == path(1) &&
            value.observations.back().path == path(9), "ninth distinct path evicts oldest observation; duplicate touch refreshes recency");
        bool absent = true; for (const auto& item : value.observations) absent = absent && item.path != path(2);
        check(absent && value.observations[6].observations == 2, "eviction preserves other observations and duplicate count");
    }
    {
        ++cases; ko::Cache cache; const auto input = path();
        ko::detail::CacheTestAccess::hold(cache);
        cache.observe(input, 1, 1, 1, 1); const auto blocked = cache.snapshot();
        ko::detail::CacheTestAccess::release(cache);
        check(!blocked.known && blocked.observations.empty() && blocked.droppedContention == 2,
            "observe and snapshot each make one lock attempt and expose contention");
        cache.observe(input, 1, 1, 1, 1);
        check(cache.snapshot().known && cache.snapshot().observations.size() == 1, "lock remains usable after deterministic contention");
    }
    {
        ++cases; ko::Cache cache; const auto input = path();
        failAfter.store(0); cache.observe(input, 1, 1, 1, 1); restoreAllocation();
        const auto failed = cache.snapshot();
        check(failed.known && failed.observations.empty() && failed.rejected == 1 && failed.allocationFailures == 1,
            "new-path allocation failure is rejected and counted without partial insertion");
        cache.observe(input, 1, 1, 1, 1);
        failAfter.store(0); const auto unknown = cache.snapshot(); restoreAllocation();
        check(!unknown.known && unknown.observations.empty() && unknown.allocationFailures == 2,
            "snapshot allocation failure is explicit unknown, not empty success");
        check(cache.snapshot().observations.size() == 1, "failed snapshot preserves stored evidence");
        const auto allocationsBefore = allocationAttempts.load();
        failAfter.store(0); cache.observe(input, 2, 2, 2, 2);
        const auto remaining = failAfter.load();
        const auto allocationsAfter = allocationAttempts.load(); restoreAllocation();
        check(remaining == 0 && allocationsBefore == allocationsAfter && cache.snapshot().observations[0].observations == 2,
            "deduplicated observation does not allocate");
        ko::detail::CacheTestAccess::saturateObservation(cache);
        cache.observe(input, 2, 2, 2, 2);
        check(cache.snapshot().observations[0].observations == (std::numeric_limits<std::uint64_t>::max)(),
            "observation count saturates instead of wrapping");
    }
    {
        ++cases; ko::Cache cache;
        for (unsigned i = 1; i <= 8; ++i) cache.observe(path(i), i, i, i, i);
        const auto input = path(9); failAfter.store(0); cache.observe(input, 9, 9, 9, 9); restoreAllocation();
        const auto value = cache.snapshot();
        check(value.observations.size() == 8 && value.evicted == 0 && value.observations.front().path == path(1) &&
            value.allocationFailures == 1, "failed insertion into a full cache never evicts existing evidence");
    }
}

static void collectorTests() {
    {
        ++cases; FakeOps ops; ops.throwOperation = "now";
        const auto value = ko::collect(false, ops);
        check(value.at("status") == "disabled" && ops.trace.empty() && !ops.clocks && !ops.snapshots &&
            !ops.queries && !ops.conversions, "disabled collector performs ZERO Ops calls"); proof(value);
    }
    {
        ++cases; FakeOps ops;
        const auto value = ko::collect(true, ops);
        check(value.at("status") == "complete" && value.at("complete") == true && value.at("known") == true &&
            ops.queries == 9 && ops.snapshots == 1 && ops.conversions == 1, "single root collects exactly resolve + instance + six properties + parent");
        check(row(value).at("status") == "complete" && row(value).at("ancestryStatus") == "root" &&
            node(value).at("parent").at("known") == false && node(value).at("parent").at("error") == 13,
            "explicit synthetic root marker is accepted without rewriting its raw known/error values");
        check(value.at("snapshot").at("droppedContention") == 2 && value.at("snapshot").at("rejected") == 3 &&
            value.at("snapshot").at("evicted") == 4 && value.at("snapshot").at("allocationFailures") == 5 &&
            row(value).at("firstEventTick") == 10 && row(value).at("lastDevice") == 123 &&
            row(value).at("mixedGeneration") == true, "snapshot losses and observation identity limits survive JSON projection");
        check(Json::parse(value.dump()) == value && value.at("limits").at("hardIoTimeout") == false,
            "complete evidence serializes and checkpoint limit is explicitly not a hard I/O timeout");
        proof(value); bounds(value, ops);
    }
    {
        ++cases; FakeOps ops; ops.data.observations.clear();
        const auto value = ko::collect(true, ops);
        check(value.at("status") == "complete" && value.at("interfaces").empty() && !ops.queries && !ops.conversions,
            "known empty snapshot is complete without device queries");
    }
    {
        ++cases; FakeOps ops; ops.data.known = false;
        const auto value = ko::collect(true, ops);
        check(value.at("status") == "unknown" && value.at("known") == false && value.at("complete") == false &&
            value.at("interfaces").empty() && !ops.queries && !ops.conversions &&
            value.at("snapshot").at("rejected") == 3, "unknown snapshot is explicit and its observations are never queried");
    }
    {
        ++cases; FakeOps ops;
        for (unsigned i = 2; i <= 10; ++i) ops.data.observations.push_back(observation(i));
        const auto value = ko::collect(true, ops);
        check(value.at("status") == "partial" && value.at("interfacesTruncated") == true &&
            value.at("snapshot").at("count") == 10 && ops.resolveCalls == 8,
            "oversized external snapshot is capped at eight interfaces and reports truncation"); bounds(value, ops);
    }
    {
        ++cases; FakeOps ops; ops.data.observations = {observation(1), observation(2), observation(3), observation(4)};
        ops.data.observations[0].path = L"\\\\?\\";
        ops.data.observations[1].path += wchar_t(0);
        ops.data.observations[2].observations = 0;
        const auto value = ko::collect(true, ops);
        check(value.at("status") == "partial" && row(value, 0).at("status") == "malformedObservation" &&
            row(value, 1).at("path").is_null() && row(value, 2).at("status") == "malformedObservation" &&
            ops.resolveCalls == 1 && row(value, 3).at("status") == "complete",
            "malformed roots and impossible observation counts are skipped while later evidence survives");
    }
    {
        ++cases; FakeOps ops; auto duplicate = observation(); duplicate.path[4] = L'h';
        ops.data.observations.push_back(duplicate);
        const auto value = ko::collect(true, ops);
        check(row(value, 1).at("status") == "duplicatePath" && ops.resolveCalls == 1,
            "collector defensively suppresses duplicate roots from external snapshots");
    }
    for (const auto& invalid : {std::string(), std::string(4097, 'x'), std::string("x\0y", 3), std::string("\xc0\xaf", 2)}) {
        ++cases; FakeOps ops; ops.overrideEncoding = true; ops.encodedOverride = invalid;
        const auto value = ko::collect(true, ops);
        check(row(value).at("status") == "malformedPathEncoding" && !ops.queries && row(value).at("path").is_null(),
            "invalid converted paths never reach resolution or JSON strings");
    }
    for (const auto resolved : {ko::ResolveResult{false, 77, 42}, ko::ResolveResult{true, 0, 0}}) {
        ++cases; FakeOps ops; ops.resolveValue = resolved;
        const auto value = ko::collect(true, ops);
        check(row(value).at("known") == false && row(value).at("nodes").empty() && ops.queries == 1 &&
            row(value).at("resolve").at("error") == resolved.error && value.at("status") == "partial",
            "unknown or zero resolved node never becomes trusted ancestry");
    }
    {
        ++cases; FakeOps ops; ops.instanceValue = {false, 55, "discard this untrusted value"};
        ops.propertyValue = {false, 66, "discard this untrusted value"};
        const auto value = ko::collect(true, ops);
        check(node(value).at("instance").at("status") == "unknown" && node(value).at("instance").at("error") == 55 &&
            node(value).at("instance").at("value").is_null() &&
            node(value).at("properties").at("service").at("value").is_null() &&
            ops.parentCalls == 1 && value.at("status") == "partial", "unknown string results retain errors, omit values and permit remaining queries");
    }
    {
        ++cases; FakeOps ops; ops.instanceValue.value.assign(4096, 'i'); ops.propertyValue.value.assign(4096, 'p');
        const auto value = ko::collect(true, ops);
        check(value.at("status") == "complete" && node(value).at("instance").at("value").get<std::string>().size() == 4096,
            "4096-byte metadata boundary is accepted");
    }
    for (const auto& invalid : {std::string(4097, 'x'), std::string("x\0y", 3), std::string("bad\n"),
             std::string("\xed\xa0\x80", 3), std::string("\xf4\x90\x80\x80", 4), std::string("\xe2\x82", 2)}) {
        ++cases; FakeOps ops; ops.instanceValue.value = invalid; ops.propertyValue.value = invalid;
        const auto value = ko::collect(true, ops);
        check(value.at("status") == "partial" && node(value).at("instance").at("status") == "malformedString" &&
            node(value).at("properties").at("description").at("value").is_null() && Json::parse(value.dump()) == value,
            "oversized/control/invalid-UTF8 metadata is explicitly malformed and never corrupts JSON");
    }
    {
        ++cases; FakeOps ops; ops.instanceValue.value.clear(); ops.propertyValue.value.clear();
        const auto value = ko::collect(true, ops);
        check(node(value).at("instance").at("known") == false &&
            node(value).at("properties").at("service").at("known") == true,
            "empty instance ID is malformed while a known empty property remains representable");
    }
    {
        ++cases; FakeOps ops; ops.propertyValue.value = "\xe5\x88\xb6\xe9\x80\xa0\xe5\x95\x86 \xf0\x9f\x8e\xae";
        const auto value = ko::collect(true, ops);
        check(value.at("status") == "complete" && Json::parse(value.dump()) == value,
            "valid multibyte and supplementary UTF-8 metadata is preserved");
    }
    for (const auto parent : {ko::ParentResult{true, true, 9, 0}, ko::ParentResult{false, true, 9, 13},
             ko::ParentResult{true, false, 0, 0}, ko::ParentResult{false, false, 0, 99}}) {
        ++cases; FakeOps ops; ops.parentValue = parent;
        const auto value = ko::collect(true, ops);
        check(value.at("status") == "partial" && row(value).at("nodes").size() == 1 &&
            row(value).at("ancestryStatus") == (parent.known || parent.root ? "malformedRoot" : "unknown") &&
            node(value).at("parent").at("error") == parent.error,
            "malformed root flags/zero nonroot nodes and unknown parents are explicit terminal states");
    }
    {
        ++cases; FakeOps ops; ops.parents = {{true, false, 2, 0}, {true, false, 1, 0}};
        const auto value = ko::collect(true, ops);
        check(row(value).at("ancestryStatus") == "cycle" && row(value).at("nodes").size() == 2 && ops.queries == 17,
            "two-node cycle terminates without querying a repeated node"); bounds(value, ops);
    }
    {
        ++cases; FakeOps ops; ops.parentValue = {true, false, 1, 0};
        const auto value = ko::collect(true, ops);
        check(row(value).at("ancestryStatus") == "cycle" && ops.queries == 9, "self-parent cycle is detected at first node");
    }
    {
        ++cases; FakeOps ops; ops.chain = true;
        const auto value = ko::collect(true, ops);
        check(row(value).at("ancestryStatus") == "depthLimit" && row(value).at("nodes").size() == 6 && ops.queries == 49,
            "deep ancestry stops at six nodes with an explicit depth limit"); bounds(value, ops);
    }
    {
        ++cases; FakeOps ops; ops.chain = true;
        for (unsigned i = 2; i <= 8; ++i) ops.data.observations.push_back(observation(i));
        const auto value = ko::collect(true, ops);
        check(value.at("status") == "partial" && value.at("stopReason") == "operationBudget" &&
            ops.queries == 160 && row(value, 3).at("status") == "operationBudget" &&
            node(value, 3, 1).at("properties").at("manufacturer").at("status") == "operationBudget" &&
            node(value, 3, 1).at("properties").at("description").at("status") == "notQueried",
            "shared 160-operation budget retains prior results and labels the unfinished interface"); bounds(value, ops);
    }
    for (const auto boundary : {std::uint64_t(399), std::uint64_t(400)}) {
        ++cases; FakeOps ops; ops.ticks = {100, 100, boundary};
        const auto value = ko::collect(true, ops);
        check(boundary == 399 ? value.at("status") == "complete" && ops.queries == 9 :
            value.at("stopReason") == "timeBudget" && ops.queries == 0 && ops.conversions == 0,
            "299ms permits work; exact 300ms checkpoint stops before the next operation");
    }
    {
        ++cases; FakeOps ops;
        // Start, before snapshot, before utf8, before resolve, before instance.
        ops.ticks = {100, 100, 100, 100, 401};
        const auto value = ko::collect(true, ops);
        check(value.at("stopReason") == "timeBudget" && ops.resolveCalls == 1 && !ops.instanceCalls &&
            row(value).at("resolve").at("known") == true, "slow completed operation is retained before the next checkpoint stops further I/O");
    }
    {
        ++cases; FakeOps ops; ops.ticks.assign(12, 100); ops.ticks.push_back(450);
        const auto value = ko::collect(true, ops);
        check(value.at("stopReason") == "timeBudget" && ops.queries == 9 &&
            row(value).at("ancestryStatus") == "root", "final checkpoint detects time overrun in the last parent operation without discarding it");
    }
    {
        ++cases; FakeOps ops; ops.ticks = {100, 100, 99};
        const auto value = ko::collect(true, ops);
        check(value.at("stopReason") == "clockRegression" && !ops.queries && value.at("elapsedMs") == 0,
            "regressing clock stops before unsigned elapsed-time underflow");
    }
    {
        ++cases; FakeOps ops; const auto nearMax = (std::numeric_limits<std::uint64_t>::max)() - 10;
        ops.ticks = {nearMax, nearMax, nearMax + 5};
        const auto value = ko::collect(true, ops);
        check(value.at("status") == "complete" && value.at("elapsedMs") == 5,
            "elapsed-time arithmetic remains safe near uint64 maximum");
    }
    for (const char* operation : {"now", "snapshot", "utf8", "resolve", "instance", "service", "driverInf",
             "driverProvider", "manufacturer", "description", "enumerator", "parent"}) {
        ++cases; FakeOps ops; ops.throwOperation = operation;
        const auto value = ko::collect(true, ops);
        check(value.at("status") == "partial" && value.at("hadExceptions") == true && value.at("complete") == false,
            "exception from each Ops method is an explicit partial result");
        if (std::string(operation) == "service")
            check(node(value).at("properties").at("service").at("status") == "exception" &&
                node(value).at("properties").at("enumerator").at("known") == true && ops.parentCalls == 1,
                "one property exception preserves later metadata and the parent query");
        bounds(value, ops);
    }
    {
        ++cases; FakeOps ops; ops.throwOperation = "now"; ops.throwAt = 5;
        const auto value = ko::collect(true, ops);
        check(value.at("stopReason") == "exception" && ops.resolveCalls == 1 && !ops.instanceCalls,
            "checkpoint clock exception retains earlier resolve evidence");
    }
    {
        ++cases; FakeOps ops; ops.throwOperation = "service"; ops.allocationException = true;
        const auto value = ko::collect(true, ops);
        check(value.at("allocationFailure") == true && value.at("stopReason") == "allocationFailure" &&
            node(value).at("instance").at("known") == true && ops.propertyCalls == 1 && !ops.parentCalls,
            "bad_alloc from Ops stops work and preserves earlier fields with an explicit reason");
    }
    {
        ++cases; FakeOps ops; ops.throwOperation = "resolve"; ops.persistentAllocationException = true;
        const auto value = ko::collect(true, ops); restoreAllocation();
        check(value.is_object() && value.at("allocationFailure") == true && value.at("complete") == false &&
            value.at("status") == "allocationFailure" && row(value).at("path").is_string(),
            "persistent allocation failure preserves the prebuilt failure status and preceding evidence");
    }
    {
        ++cases; FakeOps ops; persistentFailure.store(true); failAfter.store(0);
        const auto value = ko::collect(true, ops); restoreAllocation();
        check(value.is_null() && ops.trace.empty(), "failure to allocate the initial status object returns the explicit null unavailable sentinel");
    }
    {
        ++cases; FakeOps ops; ops.propertyValue.value = "Unrelated installed driver provider";
        const auto value = ko::collect(true, ops); proof(value);
        check(node(value).at("properties").at("driverProvider").at("value") == ops.propertyValue.value &&
            !value.contains("sender") && !value.contains("injectorPidGuess") && !value.contains("ownerGuess"),
            "provider is current metadata only and never produces sender/owner heuristics");
    }
}
int main() {
    static_assert(noexcept(ko::collect(false, std::declval<FakeOps&>())));
    std::set_terminate([] {
        std::fprintf(stderr, "unexpected terminate at case %u, failure countdown %lld\n", cases, failAfter.load());
        std::abort();
    });
    cacheTests(); collectorTests(); restoreAllocation();
    std::printf("{\"pass\":%s,\"cases\":%u,\"checks\":%u,\"failures\":%u,"
        "\"allocationFaultPolicy\":\"Release: all; Checked: exclude MSVC iterator-proxy-sized allocations\","
        "\"scope\":\"pure cache and fake Ops; no real CM, User32, product, helper, input or device execution\"}\n",
        failures ? "false" : "true", cases, checks, failures);
    return failures ? 1 : 0;
}
