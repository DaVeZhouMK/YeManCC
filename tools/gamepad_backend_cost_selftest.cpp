// Isolated tests of the exact production headers; no product entrypoint/device API.
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include "json.hpp"
#include "gamepad_backend_cost.h"
#include "gamepad_backend_cost_json.h"

namespace cost = ymcc::input::backendCost;
using json = nlohmann::json;
using U64 = std::uint64_t;
using U32 = std::uint32_t;
constexpr U32 kU32Max = std::numeric_limits<U32>::max();
constexpr U32 kHresultFail = 0x80004005u;
static_assert(!std::is_copy_constructible_v<cost::Span<>>);
static_assert(!std::is_copy_assignable_v<cost::Span<>>);
static_assert(!std::is_move_constructible_v<cost::Span<>>);
static_assert(!std::is_move_assignable_v<cost::Span<>>);
static_assert(cost::kResultCodeCapacity == 8 && cost::kXinputRouteCount == 24);

static U64 checks = 0;
static json suites = json::array();
static void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
static std::map<U32, U64> histogram(const cost::Snapshot& s) {
    std::map<U32, U64> result;
    for (const auto& bucket : s.resultCodes) {
        if (!bucket.n) continue; // Unassigned/empty bucket code is not an observation.
        require(!result.contains(bucket.code), "duplicate occupied result-code key");
        result[bucket.code] = bucket.n;
    }
    return result;
}
static void empty(const cost::Snapshot& s) {
    require(s.n == 0 && s.sumUs == 0 && s.maxUs == 0 && s.success == 0 &&
            s.nonSuccess == 0 && s.otherCodeCount == 0, "drained aggregate still has counts");
    for (const auto& bucket : s.resultCodes) require(bucket.n == 0, "histogram did not drain");
}
static void totals(const cost::Snapshot& s, U64 n, U64 sum, U64 mx, U64 good, U64 bad) {
    require(s.n == n, "observation total");
    require(s.sumUs == sum, "elapsed sum");
    require(s.maxUs == mx, "elapsed maximum");
    require(s.success == good && s.nonSuccess == bad, "success/non-success totals");
}
static void suite(const char* name, U64 before, json evidence = json::object()) {
    suites.push_back({{"name", name}, {"checks", checks - before}, {"evidence", std::move(evidence)}});
}

struct FakeClock {
    using rep = std::int64_t;
    using period = std::nano;
    using duration = std::chrono::nanoseconds;
    using time_point = std::chrono::time_point<FakeClock>;
    static constexpr bool is_steady = true;
    static inline rep tick = 0;
    static inline unsigned calls = 0;
    static time_point now() noexcept { ++calls; return time_point(duration(tick)); }
    static void reset(rep value = 0) { tick = value; calls = 0; }
};

static void testTotals() {
    const auto before = checks;
    cost::Aggregate aggregate;
    empty(aggregate.drain());
    aggregate.record(0, 0, false);
    aggregate.record(3, kU32Max, true);
    aggregate.record(11, 0, true);
    aggregate.record(7, kHresultFail, false);
    aggregate.record(11, kU32Max, false);
    aggregate.record(1, 0, true);
    const auto s = aggregate.drain();
    totals(s, 6, 33, 11, 3, 3);
    require(histogram(s) == std::map<U32, U64>{{0, 3}, {kU32Max, 2}, {kHresultFail, 1}},
            "zero, UINT32_MAX or HRESULT result key lost");
    require(s.otherCodeCount == 0, "unexpected overflow");
    empty(aggregate.drain());
    const U64 wide = std::numeric_limits<U64>::max() - 10;
    aggregate.record(wide, kHresultFail, false);
    aggregate.record(10, 0, true);
    const auto wideResult = aggregate.drain();
    totals(wideResult, 2, std::numeric_limits<U64>::max(), wide, 1, 1);
    const auto roundtrip = json::parse(cost::snapshotJson<json>(wideResult).dump());
    require(roundtrip.at("sumUs").get<U64>() == std::numeric_limits<U64>::max(), "64-bit sum JSON narrowed");
    require(roundtrip.at("maxUs").get<U64>() == wide, "64-bit maximum JSON narrowed");
    suite("exact totals, maxima, zero, UINT32_MAX and HRESULT", before);
}

static void testLifetimeAssignment() {
    const auto before = checks;
    constexpr std::array<U32, 8> codes{0, kU32Max, 1, 17, kHresultFail, 0x80000000u, 87, 1167};
    cost::Aggregate aggregate;
    for (std::size_t i = 0; i < codes.size(); ++i)
        for (std::size_t j = 0; j <= i; ++j) aggregate.record(2, codes[i], (i % 2) == 0);
    aggregate.record(11, 300, false);
    aggregate.record(12, 301, false);
    aggregate.record(13, 300, false);
    const auto first = aggregate.drain();
    totals(first, 39, 108, 13, 16, 23);
    for (std::size_t i = 0; i < codes.size(); ++i) {
        require(first.resultCodes[i].code == codes[i], "first eight keys reordered");
        require(first.resultCodes[i].n == i + 1, "first eight histogram count");
    }
    require(first.otherCodeCount == 3 && first.lastOtherCode == 300, "overflow count/sample");
    empty(aggregate.drain());
    aggregate.record(20, 302, false);
    aggregate.record(21, codes[7], true);
    aggregate.record(22, codes[0], true);
    aggregate.record(23, 303, false);
    aggregate.record(24, kU32Max, true); // Assigned codes must not replace last overflow sample.
    const auto second = aggregate.drain();
    totals(second, 5, 110, 24, 3, 2);
    for (std::size_t i = 0; i < codes.size(); ++i)
        require(second.resultCodes[i].code == codes[i], "drain recycled a lifetime key");
    require(histogram(second) == std::map<U32, U64>{{codes[0], 1}, {codes[7], 1}, {kU32Max, 1}},
            "existing lifetime key failed after drain");
    require(second.otherCodeCount == 2 && second.lastOtherCode == 303, "new codes escaped lifetime overflow");
    const auto drained = aggregate.drain();
    empty(drained);
    require(cost::snapshotJson<json>(drained).at("lastOtherCodeU32").is_null(),
            "stale overflow sample presented without count");
    suite("first eight assignments and overflow retained across drains", before);
}

static void testSpans() {
    const auto before = checks;
    cost::Aggregate untouched;
    untouched.record(19, kHresultFail, false);
    FakeClock::reset(100000);
    unsigned gates = 0;
    for (unsigned i = 0; i < 64; ++i) {
        cost::Span<FakeClock> span(false);
        span.finish(untouched, 0, true, [&]() noexcept { ++gates; return true; });
        span.finish(untouched, kU32Max, false, [&]() noexcept { ++gates; return false; });
    }
    require(FakeClock::calls == 0 && gates == 0, "disabled span consulted clock or gate");
    const auto seeded = untouched.drain();
    totals(seeded, 1, 19, 19, 0, 1);
    require(histogram(seeded) == std::map<U32, U64>{{kHresultFail, 1}}, "disabled span mutated aggregate");

    cost::Aggregate enabled;
    FakeClock::reset(100000);
    cost::Span<FakeClock> span(true);
    require(FakeClock::calls == 1, "enabled span omitted start clock");
    bool gateBeforeEnd = false;
    FakeClock::tick = 125000;
    span.finish(enabled, kHresultFail, false, [&]() noexcept {
        ++gates;
        gateBeforeEnd = FakeClock::calls == 1;
        FakeClock::tick = 140999;
        return true;
    });
    require(gateBeforeEnd && FakeClock::calls == 2 && gates == 1, "enabled start/end or gate ordering");
    FakeClock::tick = 999000;
    span.finish(enabled, 0, true, [&]() noexcept { ++gates; return true; });
    require(FakeClock::calls == 2 && gates == 1, "finished span ran again");
    const auto measured = enabled.drain();
    totals(measured, 1, 40, 40, 0, 1);
    require(histogram(measured) == std::map<U32, U64>{{kHresultFail, 1}}, "finish changed HRESULT bits");

    FakeClock::reset(5000);
    gates = 0;
    cost::Span<FakeClock> closedAtEnd(true);
    closedAtEnd.finish(enabled, 0, true, [&]() noexcept { ++gates; return false; });
    require(FakeClock::calls == 1 && gates == 1, "closed end gate read end clock");
    closedAtEnd.finish(enabled, 0, true, [&]() noexcept { ++gates; return true; });
    require(FakeClock::calls == 1 && gates == 1, "closed end gate did not consume span");
    empty(enabled.drain());
    for (const auto delta : std::array<FakeClock::rep, 4>{-1, 0, 999, 12345}) {
        FakeClock::reset(100000);
        cost::Span<FakeClock> tiny(true);
        FakeClock::tick += delta;
        tiny.finish(enabled, 0, true, []() noexcept { return true; });
        const U64 us = delta > 0 ? static_cast<U64>(delta / 1000) : 0;
        totals(enabled.drain(), 1, us, us, 1, 0);
        require(FakeClock::calls == 2, "tiny span clock count");
    }
    FakeClock::reset();
    gates = 0;
    cost::Span<FakeClock> reentrant(true);
    FakeClock::tick = 3000;
    reentrant.finish(enabled, 0, true, [&]() noexcept {
        ++gates;
        reentrant.finish(enabled, 1, false, [&]() noexcept { ++gates; return true; });
        return true;
    });
    require(gates == 1 && FakeClock::calls == 2, "span was reusable inside its gate");
    totals(enabled.drain(), 1, 3, 3, 1, 0);
    suite("disabled/enabled Span, end gate, finish once, truncation and reentrance", before,
          {{"disabledClockCalls", 0}, {"disabledGateCalls", 0}, {"disabledAggregateUnchanged", true},
           {"atomicInstructionProof", "runner verifies Release disabled probe assembly"}});
}

static void testRoutes() {
    const auto before = checks;
    cost::Counters counters;
    std::set<std::size_t> indices;
    for (std::size_t source = 0; source < 4; ++source)
        for (std::size_t purpose = 0; purpose < 3; ++purpose)
            for (unsigned ordinal = 0; ordinal < 2; ++ordinal) {
                const auto route = cost::Counters::route(source, purpose, ordinal != 0);
                require(route == (source * 3 + purpose) * 2 + ordinal, "route mapping");
                require(route < 24 && indices.insert(route).second, "route alias/out-of-bounds");
                for (std::size_t n = 0; n <= route; ++n)
                    counters.xinput[route].record(100 + route, static_cast<U32>(route), ordinal == 0);
            }
    require(indices.size() == 24, "missing route");
    for (std::size_t route = 0; route < 24; ++route) {
        const auto s = counters.xinput[route].drain();
        const auto n = route + 1;
        totals(s, n, n * (100 + route), 100 + route, route % 2 == 0 ? n : 0, route % 2 ? n : 0);
        require(histogram(s) == std::map<U32, U64>{{static_cast<U32>(route), n}}, "route contamination");
    }
    empty(counters.system32.drain()); empty(counters.msiPoll.drain()); empty(counters.msiGetDeviceState.drain());
    suite("all 24 source/purpose/API routes are independent", before, {{"distinctRoutes", indices.size()}});
}

static std::map<U32, U64> jsonHistogram(const json& value) {
    std::map<U32, U64> result;
    for (const auto& bucket : value.at("resultCodes")) {
        require(bucket.size() == 2 && bucket.at("codeU32").is_number_unsigned(), "result JSON fields/type");
        const auto code = bucket.at("codeU32").get<U32>();
        require(result.emplace(code, bucket.at("n").get<U64>()).second, "duplicate JSON code");
    }
    return result;
}
static void emptyJson(const json& value) {
    require(value.size() == 9 && value.at("n") == 0 && value.at("sumUs") == 0 && value.at("maxUs") == 0,
            "empty JSON totals");
    require(value.at("meanUs").is_null() && value.at("lastOtherCodeU32").is_null(), "empty JSON unavailable means/sample");
    require(value.at("success") == 0 && value.at("nonSuccess") == 0 && value.at("otherCodeCount") == 0 &&
            value.at("resultCodes").empty(), "empty JSON histogram");
}
static void testJson() {
    const auto before = checks;
    cost::Snapshot inconsistent;
    inconsistent.sumUs = 17; // Legal adjacent-emission state: n was drained separately.
    const auto noN = cost::snapshotJson<json>(inconsistent);
    require(noN.at("meanUs").is_null() && noN.at("sumUs") == 17, "n=0 must give null mean even with sum");
    cost::Aggregate aggregate;
    const std::array<U32, 8> codes{0, kU32Max, kHresultFail, 0x80070005u, 1, 2, 3, 4};
    for (const auto code : codes) aggregate.record(3, code, code == 0);
    aggregate.record(5, kHresultFail, false);
    aggregate.record(6, 0x80070057u, false);
    aggregate.record(7, 0x887a0005u, false);
    const auto serialized = cost::snapshotJson<json>(aggregate.drain());
    const auto row = json::parse(serialized.dump());
    require(row == serialized, "JSON roundtrip");
    require(row.at("n") == 11 && row.at("sumUs") == 42 && row.at("maxUs") == 7 && row.at("meanUs") == 3,
            "actual adapter totals/integer mean");
    require(row.at("success") == 1 && row.at("nonSuccess") == 10, "actual adapter classification");
    const auto hist = jsonHistogram(row);
    require(hist.size() == 8 && hist.at(0) == 1 && hist.at(kU32Max) == 1 && hist.at(kHresultFail) == 2 &&
            hist.at(0x80070005u) == 1, "JSON histogram U32 bits");
    require(row.at("otherCodeCount") == 2 && row.at("lastOtherCodeU32").get<U32>() == 0x887a0005u &&
            row.at("lastOtherCodeU32").is_number_unsigned(), "overflow HRESULT bits/type");
    emptyJson(cost::snapshotJson<json>(aggregate.drain()));

    cost::Counters counters;
    const std::array<const char*, 4> sources{"bus", "ui-wm-input", "ui-timer", "other"};
    const std::array<const char*, 3> purposes{"realtime", "discovery", "guard"};
    for (std::size_t r = 0; r < 24; ++r)
        counters.xinput[r].record(101 + r, static_cast<U32>(1000 + r), r % 2 == 0);
    counters.system32.record(9, kU32Max, false);
    counters.msiPoll.record(13, 0x80070005u, false);
    counters.msiGetDeviceState.record(17, kHresultFail, false);
    counters.msiGetDeviceState.record(18, 0, true);
    const auto window = json::parse(cost::windowJson<json>(counters).dump());
    require(window.size() == 11 && window.at("schema") == "ymcc-gamepad-backend-cost-v1", "window schema");
    require(window.at("resultCodeCapacityPerRoute") == 8, "window capacity");
    require(window.at("measurement") == "steady_clock elapsed around calls; not thread CPU or pure hardware I/O", "measurement scope");
    require(window.at("windowSemantics").get<std::string>().find("independent atomic counters may straddle emissions") != std::string::npos,
            "window semantics documentation");
    require(window.at("resultCodeAssignment").get<std::string>().find("assignments retained across drains") != std::string::npos,
            "lifetime assignment documentation");
    require(window.at("coverage").get<std::string>().find("lifecycle target probes excluded") != std::string::npos &&
            window.at("msiCanonicalSnapshot").get<std::string>().find("no new device call") != std::string::npos, "coverage fields");
    require(window.at("xinputWrappers").size() == 4, "JSON source count");
    for (std::size_t source = 0; source < 4; ++source) {
        const auto& byPurpose = window.at("xinputWrappers").at(sources[source]);
        require(byPurpose.size() == 3, "JSON purpose count");
        for (std::size_t purpose = 0; purpose < 3; ++purpose) {
            const auto& apis = byPurpose.at(purposes[purpose]);
            require(apis.size() == 2, "JSON API count");
            for (unsigned ordinal = 0; ordinal < 2; ++ordinal) {
                const auto r = cost::Counters::route(source, purpose, ordinal != 0);
                const auto& value = apis.at(ordinal ? "ordinal100" : "canonical");
                require(value.at("n") == 1 && value.at("sumUs") == 101 + r && value.at("meanUs") == 101 + r,
                        "JSON route mapping");
                require(value.at("success") == (ordinal ? 0 : 1), "JSON route success mapping");
                require(jsonHistogram(value) == std::map<U32, U64>{{static_cast<U32>(1000 + r), 1}}, "JSON route key");
            }
        }
    }
    require(jsonHistogram(window.at("system32Diagnostic")) == std::map<U32, U64>{{kU32Max, 1}}, "system32 adapter field");
    require(window.at("system32Diagnostic").at("sumUs") == 9, "system32 elapsed");
    require(jsonHistogram(window.at("msiPoll")) == std::map<U32, U64>{{0x80070005u, 1}}, "MSI Poll HRESULT");
    require(window.at("msiPoll").at("sumUs") == 13, "MSI Poll elapsed");
    require(jsonHistogram(window.at("msiGetDeviceState")) == std::map<U32, U64>{{0, 1}, {kHresultFail, 1}}, "MSI state HRESULT");
    require(window.at("msiGetDeviceState").at("n") == 2 && window.at("msiGetDeviceState").at("sumUs") == 35 &&
            window.at("msiGetDeviceState").at("meanUs") == 17, "MSI state elapsed");
    const auto next = cost::windowJson<json>(counters);
    for (const auto* source : sources)
        for (const auto* purpose : purposes)
            for (const auto* api : {"canonical", "ordinal100"}) emptyJson(next.at("xinputWrappers").at(source).at(purpose).at(api));
    for (const auto* field : {"system32Diagnostic", "msiPoll", "msiGetDeviceState"}) emptyJson(next.at(field));
    suite("actual vendored-JSON adapter, unsigned HRESULTs, all route/system/MSI fields", before,
          {{"histogramSample", row}, {"windowSample", window}});
}

struct Collected {
    U64 n = 0, sum = 0, mx = 0, good = 0, bad = 0, overflow = 0, drains = 0;
    std::map<U32, U64> codes;
    std::array<U32, 8> keys{};
    std::array<bool, 8> seen{};
    bool stableKeys = true;
    void add(const cost::Snapshot& s) {
        ++drains;
        n += s.n; sum += s.sumUs; mx = std::max(mx, s.maxUs);
        good += s.success; bad += s.nonSuccess; overflow += s.otherCodeCount;
        for (std::size_t i = 0; i < s.resultCodes.size(); ++i) {
            const auto& bucket = s.resultCodes[i];
            if (!bucket.n) continue;
            if (seen[i] && keys[i] != bucket.code) stableKeys = false;
            keys[i] = bucket.code; seen[i] = true;
            codes[bucket.code] += bucket.n;
        }
    }
    void merge(const Collected& other) {
        n += other.n; sum += other.sum; mx = std::max(mx, other.mx);
        good += other.good; bad += other.bad; overflow += other.overflow; drains += other.drains;
        stableKeys = stableKeys && other.stableKeys;
        for (const auto& [code, count] : other.codes) codes[code] += count;
        for (std::size_t i = 0; i < 8; ++i) if (other.seen[i]) {
            if (seen[i] && keys[i] != other.keys[i]) stableKeys = false;
            seen[i] = true; keys[i] = other.keys[i];
        }
    }
};
static U64 elapsed(unsigned worker, unsigned i) { return (worker * 7u + i) % 31u; }
static bool successful(unsigned worker, unsigned i) { return (i + worker) % 3u != 0; }
static void testConcurrent(unsigned iterations, int scenario) {
    const auto before = checks;
    cost::Aggregate aggregate;
    if (scenario == 2) {
        for (U32 i = 100; i < 108; ++i) aggregate.record(0, i, true);
        aggregate.drain(); // Fill lifetime keys; seed observations are outside tested workload.
    }
    constexpr std::array<U32, 4> codes{0, kU32Max, kHresultFail, 0x80070005u};
    std::barrier start(5);
    std::array<std::thread, 4> workers;
    for (unsigned worker = 0; worker < 4; ++worker) workers[worker] = std::thread([&, worker] {
        start.arrive_and_wait();
        for (unsigned i = 0; i < iterations; ++i)
            aggregate.record(elapsed(worker, i), scenario == 0 ? 0 : codes[worker], successful(worker, i));
    });
    start.arrive_and_wait();
    for (auto& worker : workers) worker.join();
    U64 sum = 0, mx = 0, good = 0;
    std::map<U32, U64> expected;
    for (unsigned worker = 0; worker < 4; ++worker)
        for (unsigned i = 0; i < iterations; ++i) {
            const auto us = elapsed(worker, i);
            sum += us; mx = std::max(mx, us); good += successful(worker, i);
            ++expected[scenario == 0 ? 0 : codes[worker]];
        }
    const auto s = aggregate.drain();
    const U64 n = static_cast<U64>(iterations) * 4;
    totals(s, n, sum, mx, good, n - good);
    if (scenario == 2) {
        require(histogram(s).empty() && s.otherCodeCount == n, "concurrent overflow lost results");
        require(std::find(codes.begin(), codes.end(), s.lastOtherCode) != codes.end(), "overflow sample not observed");
    } else {
        require(histogram(s) == expected && s.otherCodeCount == 0, "same/distinct concurrent histogram");
    }
    empty(aggregate.drain());
    const char* name = scenario == 0 ? "4-thread same-code insertion contention" :
                       scenario == 1 ? "4-thread distinct-code insertion contention" : "4-thread overflow contention";
    suite(name, before, {{"producers", 4}, {"observations", n}, {"sumUs", sum}, {"maxUs", mx}, {"overflow", s.otherCodeCount}});
}

static void testConcurrentDrains(unsigned iterations, unsigned rounds) {
    const auto before = checks;
    // Include zero, UINT32_MAX and HRESULT bit patterns while racing first assignment.
    constexpr std::array<U32, 12> choices{0, kU32Max, kHresultFail, 0x80070005u, 1, 2, 3, 4, 5, 6, 7, 0x887a0005u};
    U64 allDrains = 0;
    json roundEvidence = json::array();
    for (unsigned round = 0; round < rounds; ++round) {
        cost::Aggregate aggregate;
        std::atomic<unsigned> done{0};
        std::atomic<bool> drainedDuringWrites{false};
        std::array<U64, 2> nonemptyDrainsDuringWrites{};
        std::barrier start(7); // Four producers, two independent drainers, main thread.
        std::array<std::thread, 4> workers;
        std::array<std::thread, 2> drainers;
        std::array<Collected, 2> collected;
        for (unsigned d = 0; d < 2; ++d) drainers[d] = std::thread([&, d] {
            start.arrive_and_wait();
            do {
                const auto snapshot = aggregate.drain();
                collected[d].add(snapshot);
                if (snapshot.n && done.load(std::memory_order_acquire) != 4) {
                    ++nonemptyDrainsDuringWrites[d];
                    drainedDuringWrites.store(true, std::memory_order_release);
                }
                std::this_thread::yield();
            } while (done.load(std::memory_order_acquire) != 4);
        });
        for (unsigned worker = 0; worker < 4; ++worker) workers[worker] = std::thread([&, worker] {
            start.arrive_and_wait();
            for (unsigned i = 0; i < iterations; ++i) {
                const auto code = choices[(i + worker * 3u + round) % choices.size()];
                aggregate.record(elapsed(worker, i), code, successful(worker, i));
                if (i == 63) {
                    // Producers cannot finish before a drainer has consumed live observations.
                    while (!drainedDuringWrites.load(std::memory_order_acquire)) std::this_thread::yield();
                }
                if ((i & 63u) == 0) std::this_thread::yield();
            }
            done.fetch_add(1, std::memory_order_acq_rel);
        });
        start.arrive_and_wait();
        for (auto& worker : workers) worker.join();
        for (auto& drainer : drainers) drainer.join();
        Collected observed;
        observed.merge(collected[0]); observed.merge(collected[1]);
        observed.add(aggregate.drain()); // All producers/drainers joined: collect every remaining field.
        const U64 n = static_cast<U64>(iterations) * 4;
        U64 sum = 0, mx = 0, good = 0;
        std::map<U32, U64> expected;
        for (unsigned worker = 0; worker < 4; ++worker)
            for (unsigned i = 0; i < iterations; ++i) {
                const auto us = elapsed(worker, i);
                sum += us; mx = std::max(mx, us); good += successful(worker, i);
                ++expected[choices[(i + worker * 3u + round) % choices.size()]];
            }
        // No assertion relates fields inside any individual drain during concurrent writes.
        require(observed.n == n && observed.sum == sum && observed.mx == mx, "conservation of total/sum/global maximum across drains");
        require(observed.good == good && observed.bad == n - good, "success conservation across drains");
        require(observed.stableKeys && observed.codes.size() == 8, "first-insertion race relabeled/lost lifetime keys");
        U64 accounted = observed.overflow, expectedOverflow = 0;
        for (const auto& [code, count] : expected) {
            if (observed.codes.contains(code)) {
                require(observed.codes.at(code) == count, "histogram count mislabeled/lost across drains");
                accounted += count;
            } else expectedOverflow += count;
        }
        for (const auto& [code, count] : observed.codes) require(expected.contains(code), "unobserved result bits appeared");
        require(accounted == n && observed.overflow == expectedOverflow, "histogram+overflow conservation across drains");
        require(collected[0].drains > 0 && collected[1].drains > 0, "concurrent drainer did not execute");
        require(nonemptyDrainsDuringWrites[0] + nonemptyDrainsDuringWrites[1] > 0, "drains never overlapped active producers");
        empty(aggregate.drain());
        allDrains += observed.drains;
        roundEvidence.push_back({{"round", round}, {"observations", observed.n}, {"drains", observed.drains},
                                 {"sumUs", observed.sum}, {"maxAcrossDrainsUs", observed.mx}, {"overflow", observed.overflow},
                                 {"nonemptyDrainsDuringWrites", nonemptyDrainsDuringWrites[0] + nonemptyDrainsDuringWrites[1]}});
    }
    suite("4 producers + 2 concurrent drainers: merged conservation only", before,
          {{"rounds", rounds}, {"drains", allDrains}, {"perDrainCoherenceAssumed", false}, {"roundEvidence", roundEvidence}});
}

#ifdef _MSC_VER
#define SELFTEST_NOINLINE __declspec(noinline)
#else
#define SELFTEST_NOINLINE __attribute__((noinline))
#endif
static std::atomic<bool> benchmarkGate{true};
static SELFTEST_NOINLINE void recordProbe(cost::Aggregate& aggregate, U64 us) noexcept {
    aggregate.record(us, 7, true);
}
static SELFTEST_NOINLINE void spanProbe(cost::Aggregate& aggregate, bool on) noexcept {
    cost::Span<> span(on);
    span.finish(aggregate, 7, true, []() noexcept { return benchmarkGate.load(std::memory_order_relaxed); });
}
static json overhead(unsigned iterations, unsigned trials) {
    json results = json::object();
    for (const std::string mode : {"record", "spanDisabled", "spanEnabled"}) {
        std::vector<double> samples;
        for (unsigned trial = 0; trial < trials; ++trial) {
            cost::Aggregate aggregate;
            aggregate.record(0, 7, true); aggregate.drain(); // Same assigned-code route in every trial.
            volatile bool enabled = mode == "spanEnabled";
            for (unsigned i = 0; i < 2048; ++i) {
                if (mode == "record") recordProbe(aggregate, i & 31u);
                else spanProbe(aggregate, enabled);
            }
            aggregate.drain();
            const auto begin = std::chrono::steady_clock::now();
            if (mode == "record") {
                for (unsigned i = 0; i < iterations; ++i) recordProbe(aggregate, i & 31u);
            } else {
                for (unsigned i = 0; i < iterations; ++i) spanProbe(aggregate, enabled);
            }
            const auto end = std::chrono::steady_clock::now();
            const double ns = std::chrono::duration<double, std::nano>(end - begin).count();
            require(ns > 0, "benchmark clock interval");
            const auto result = aggregate.drain();
            if (mode == "spanDisabled") empty(result);
            else {
                require(result.n == iterations && result.success == iterations && result.nonSuccess == 0,
                        "benchmark calls optimized away or lost");
                require(histogram(result) == std::map<U32, U64>{{7, iterations}}, "benchmark histogram");
                if (mode == "record") {
                    U64 expected = 0;
                    for (unsigned i = 0; i < iterations; ++i) expected += i & 31u;
                    require(result.sumUs == expected && result.maxUs == 31, "benchmark record sum/max");
                }
            }
            samples.push_back(ns / iterations);
        }
        auto sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        results[mode] = {{"nsPerOperationSamples", samples}, {"medianNsPerOperation", sorted[sorted.size() / 2]},
                         {"minNsPerOperation", sorted.front()}, {"maxNsPerOperation", sorted.back()},
                         {"operationsPerTrial", iterations}, {"trials", trials}};
    }
    results["scope"] = "Bounded single-thread fixture wall elapsed per operation: clock/gate/counter atomics and noinline call/loop overhead only; not whole-process CPU, device I/O or product performance.";
    results["method"] = "2048 warmup calls; lifetime code 7 preassigned; duration pattern 0..31 for record; runtime volatile bool passed to noinline Span probe; enabled gate uses relaxed atomic load; drain/JSON outside timed region; no threshold or baseline subtraction.";
    return results;
}


static void seedRepresentative(cost::Counters& c) {
    constexpr std::array<U32, 10> codes{0, 1167, kHresultFail, 0x80070005u, kU32Max, 87, 5, 6, 0x80070057u, 0x887a0005u};
    for (std::size_t i = 0; i < codes.size(); ++i)
        c.xinput[cost::Counters::route(0, 0, false)].record(3 + i, codes[i], codes[i] == 0);
    c.xinput[cost::Counters::route(1, 0, true)].record(7, 1167, false);
    c.xinput[cost::Counters::route(2, 2, false)].record(9, 0, true);
    c.xinput[cost::Counters::route(3, 1, true)].record(11, 0, true);
    c.system32.record(13, 0, true);
    c.msiPoll.record(15, 0, true); c.msiPoll.record(17, kHresultFail, false);
    c.msiGetDeviceState.record(19, 0, true); c.msiGetDeviceState.record(21, 0x80070005u, false);
}
static json timingSummary(const std::vector<double>& samples, unsigned operations, unsigned trials) {
    auto sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    return {{"nsPerOperationSamples", samples}, {"medianNsPerOperation", sorted[sorted.size() / 2]},
            {"medianUsPerOperation", sorted[sorted.size() / 2] / 1000.0}, {"minNsPerOperation", sorted.front()},
            {"maxNsPerOperation", sorted.back()}, {"operationsPerTrial", operations}, {"trials", trials}};
}
static json jsonBenchmark(unsigned iterations, unsigned trials) {
    json report = json::object();
    for (const std::string mode : {"allZero", "representativeActiveRoutes"}) {
        std::vector<double> adapterSamples, dumpSamples;
        std::vector<std::size_t> bytes;
        json sample;
        for (unsigned warmup = 0; warmup < 8; ++warmup) {
            cost::Counters counters;
            if (mode != "allZero") seedRepresentative(counters);
            sample = cost::windowJson<json>(counters);
            const auto dumped = sample.dump();
            require(!dumped.empty(), "JSON benchmark warmup");
        }
        for (unsigned trial = 0; trial < trials; ++trial) {
            double totalNs = 0;
            for (unsigned i = 0; i < iterations; ++i) {
                cost::Counters counters;
                if (mode != "allZero") seedRepresentative(counters); // Outside timed region.
                const auto begin = std::chrono::steady_clock::now();
                auto window = cost::windowJson<json>(counters); // Actual full 24-route matrix + system/MSI.
                const auto end = std::chrono::steady_clock::now();
                totalNs += std::chrono::duration<double, std::nano>(end - begin).count();
                require(window == sample, "synthetic matrix varied between adapter measurements");
            }
            adapterSamples.push_back(totalNs / iterations);
            U64 byteCount = 0;
            const auto begin = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) {
                const auto text = sample.dump();
                byteCount += text.size();
            }
            const auto end = std::chrono::steady_clock::now();
            dumpSamples.push_back(std::chrono::duration<double, std::nano>(end - begin).count() / iterations);
            const auto expectedBytes = sample.dump().size();
            require(byteCount == static_cast<U64>(expectedBytes) * iterations, "JSON dump bytes changed");
            bytes.push_back(expectedBytes);
        }
        auto sortedBytes = bytes;
        std::sort(sortedBytes.begin(), sortedBytes.end());
        report[mode] = {{"windowJson", timingSummary(adapterSamples, iterations, trials)},
                        {"dump", timingSummary(dumpSamples, iterations, trials)},
                        {"outputBytesPerEmission", sortedBytes[sortedBytes.size() / 2]}, {"outputByteSamples", bytes},
                        {"emissionsPerSecondForContext", 1}, {"sample", sample}};
    }
    report["scope"] = "Bounded actual windowJson<json> construction/drain and separate compact dump wall elapsed; all 24 routes emitted, including zero routes, plus system32/MSI; no logging, disk, product, device I/O or whole-process CPU measurement.";
    report["method"] = "8 warmup matrices; per-construction clock brackets exclude counter initialization/seeding, equality check and destruction; dump batches reuse a representative immutable JSON value and include string allocation/destruction; output bytes are compact UTF-8 JSON without newline.";
    return report;
}

int main(int argc, char** argv) {
    try {
        unsigned iterations = 6000, rounds = 6, benchIterations = 200000, trials = 5, jsonIterations = 300;
        for (int i = 1; i < argc; i += 2) {
            if (i + 1 == argc) throw std::runtime_error("missing numeric argument");
            const std::string name = argv[i];
            const auto value = static_cast<unsigned>(std::stoul(argv[i + 1]));
            if (name == "--iterations-per-thread") iterations = value;
            else if (name == "--stress-rounds") rounds = value;
            else if (name == "--overhead-iterations") benchIterations = value;
            else if (name == "--overhead-trials") trials = value;
            else if (name == "--json-iterations") jsonIterations = value;
            else throw std::runtime_error("unknown argument: " + name);
        }
        require(iterations >= 64 && iterations <= 100000 && rounds >= 1 && rounds <= 30 &&
                benchIterations >= 64 && benchIterations <= 1000000 && trials >= 1 && trials <= 9 && trials % 2 == 1 && jsonIterations >= 10 && jsonIterations <= 2000,
                "arguments exceed bounded fixture limits");
        testTotals(); testLifetimeAssignment(); testSpans(); testRoutes(); testJson();
        for (int scenario = 0; scenario < 3; ++scenario) testConcurrent(iterations, scenario);
        testConcurrentDrains(iterations, rounds);
        const auto measured = overhead(benchIterations, trials);
        const auto matrixMeasured = jsonBenchmark(jsonIterations, trials);
        std::cout << json{{"pass", true}, {"checks", checks}, {"suites", suites}, {"overhead", measured}, {"jsonBenchmark", matrixMeasured},
                          {"inputHostNativeMainOrHardwareExecuted", false}, {"spanCopyMoveRejectedAtCompileTime", true},
                          {"iterationsPerThread", iterations}, {"stressRounds", rounds}}.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::cout << json{{"pass", false}, {"checks", checks}, {"error", error.what()}, {"suites", suites}}.dump(2) << '\n';
        return 1;
    }
}
