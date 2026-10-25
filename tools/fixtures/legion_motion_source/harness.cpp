// Includes the production header first, with no product, Win32 or HID linkage.
#include "legion_motion_source.h"

#include <cstdio>
#include <limits>
#include <type_traits>
#include <utility>

using ymcc::legion_motion::Context;
using ymcc::legion_motion::Lease;
using ymcc::legion_motion::Sample;
using ymcc::legion_motion::Source;

static_assert(noexcept(std::declval<Lease&>().revoke()));
static_assert(std::is_same_v<decltype(std::declval<const Lease&>().current(Context{}, Source{}, true, 1, 0)),
                             std::optional<Sample>>);
static_assert(std::is_same_v<decltype(std::declval<Sample>().source), Source>);
static_assert(std::is_same_v<decltype(std::declval<Lease&>().publish(
    std::declval<const Sample&>(), Context{}, Source{}, true)), bool>);

namespace {
constexpr Context initial{7, 11};
const Source initialSource{"baseContainer:physical-A;VID_17EF&PID_6182", 23, 0};
// Mirrors the existing caller window for tests only. The header has no policy.
constexpr std::uint64_t callerWindowMs = 1500;
int checks = 0;
int failures = 0;
const char* activeCase = "uninitialized";

void check(bool value, const char* expression, int line) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s line=%d expression=%s\n", activeCase, line, expression);
    }
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

Sample sample(Context context = initial, std::uint64_t tick = 100) {
    return Sample{{1.25, -2.5, 3.75}, {-4.25, 5.5, -6.75}, tick, context, initialSource};
}

void expectSample(const std::optional<Sample>& actual, const Sample& expected) {
    CHECK(actual.has_value());
    if (!actual) return;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        CHECK(actual->gyro[axis] == expected.gyro[axis]);
        CHECK(actual->accel[axis] == expected.accel[axis]);
    }
    CHECK(actual->capturedAt == expected.capturedAt);
    CHECK(actual->context.power == expected.context.power);
    CHECK(actual->context.rawGeneration == expected.context.rawGeneration);
    CHECK(actual->source.physicalIdentity == expected.source.physicalIdentity);
    CHECK(actual->source.captureEpoch == expected.source.captureEpoch);
    CHECK(actual->source.sensorIndex == expected.source.sensorIndex);
}

void emptyAndRevoke() {
    Lease lease;
    CHECK(!lease.current(initial, initialSource, true, 100, callerWindowMs));
    lease.revoke();
    lease.revoke();
    CHECK(!lease.current(initial, initialSource, true, 100, callerWindowMs));
}

void fullSixaxisAndCopies() {
    Lease lease;
    auto input = sample();
    const auto expected = input;
    CHECK(lease.publish(input, initial, initialSource, true));
    input.gyro.fill(99.0);
    input.accel.fill(88.0);
    input.capturedAt = 500;
    input.context = {30, 40};
    input.source = {"modified-input", 88, 1};
    const Lease& reader = lease;
    expectSample(reader.current(initial, initialSource, true, 100, 0), expected);
    auto returned = reader.current(initial, initialSource, true, 101, 1);
    CHECK(returned.has_value());
    if (returned) {
        returned->gyro.fill(-99.0);
        returned->accel.fill(-88.0);
        returned->context = {};
        returned->capturedAt = 0;
        returned->source = {"modified-copy", 99, 1};
    }
    expectSample(reader.current(initial, initialSource, true, 101, 1), expected);
}

void zeroGenerations() {
    for (const auto context : {Context{0, 11}, Context{7, 0}, Context{0, 0}}) {
        Lease empty;
        CHECK(!empty.publish(sample(context), context, initialSource, true));
        CHECK(!empty.current(context, initialSource, true, 100, callerWindowMs));
        Lease lease;
        const auto valid = sample();
        CHECK(lease.publish(valid, initial, initialSource, true));
        CHECK(!lease.publish(sample(context, 101), context, initialSource, true));
        CHECK(!lease.publish(sample(context, 101), initial, initialSource, true));
        CHECK(!lease.publish(sample(initial, 101), context, initialSource, true));
        CHECK(!lease.current(context, initialSource, true, 101, callerWindowMs));
        expectSample(lease.current(initial, initialSource, true, 101, callerWindowMs), valid);
    }
}

void publishPowerFence() {
    const Context next{8, initial.rawGeneration};
    Lease empty;
    CHECK(!empty.publish(sample(initial), next, initialSource, true));
    CHECK(!empty.current(initial, initialSource, true, 100, callerWindowMs));
    Lease lease;
    const auto valid = sample(next, 101);
    CHECK(lease.publish(valid, next, initialSource, true));
    auto late = sample(initial, 102);
    late.gyro.fill(77.0);
    CHECK(!lease.publish(late, next, initialSource, true));
    expectSample(lease.current(next, initialSource, true, 102, callerWindowMs), valid);
}

void publishRawFence() {
    const Context next{initial.power, 12};
    Lease empty;
    CHECK(!empty.publish(sample(initial), next, initialSource, true));
    CHECK(!empty.current(initial, initialSource, true, 100, callerWindowMs));
    Lease lease;
    const auto valid = sample(next, 101);
    CHECK(lease.publish(valid, next, initialSource, true));
    auto late = sample(initial, 102);
    late.accel.fill(77.0);
    CHECK(!lease.publish(late, next, initialSource, true));
    expectSample(lease.current(next, initialSource, true, 102, callerWindowMs), valid);
}

void currentPowerFence() {
    Lease lease;
    CHECK(lease.publish(sample(), initial, initialSource, true));
    CHECK(!lease.current({8, initial.rawGeneration}, initialSource, true, 100, callerWindowMs));
    CHECK(!lease.current({initial.power + (1ULL << 32), initial.rawGeneration}, initialSource, true, 100, callerWindowMs));
}

void currentRawFence() {
    Lease lease;
    CHECK(lease.publish(sample(), initial, initialSource, true));
    CHECK(!lease.current({initial.power, 12}, initialSource, true, 100, callerWindowMs));
    CHECK(!lease.current({initial.power, initial.rawGeneration + (1ULL << 32)}, initialSource, true, 100, callerWindowMs));
}

void publishReadyFence() {
    Lease empty;
    CHECK(!empty.publish(sample(), initial, initialSource, false));
    CHECK(!empty.current(initial, initialSource, true, 100, callerWindowMs));
    Lease lease;
    const auto valid = sample();
    CHECK(lease.publish(valid, initial, initialSource, true));
    CHECK(!lease.publish(sample(initial, 101), initial, initialSource, false));
    expectSample(lease.current(initial, initialSource, true, 101, callerWindowMs), valid);
}

void currentReadyFence() {
    Lease lease;
    CHECK(lease.publish(sample(), initial, initialSource, true));
    CHECK(!lease.current(initial, initialSource, false, 100, 0));
    CHECK(!lease.current(initial, initialSource, false, 101, callerWindowMs));
}

void zeroTickAndZeroWindow() {
    Lease lease;
    CHECK(!lease.publish(sample(initial, 0), initial, initialSource, true));
    CHECK(!lease.current(initial, initialSource, true, 0, 0));
    const auto first = sample(initial, 1);
    CHECK(lease.publish(first, initial, initialSource, true));
    CHECK(!lease.current(initial, initialSource, true, 0, std::numeric_limits<std::uint64_t>::max()));
    expectSample(lease.current(initial, initialSource, true, 1, 0), first);
    CHECK(!lease.current(initial, initialSource, true, 2, 0));
    CHECK(!lease.publish(sample(initial, 0), initial, initialSource, true));
    expectSample(lease.current(initial, initialSource, true, 1, 0), first);
}

void ageBoundaries() {
    Lease lease;
    const auto valid = sample();
    CHECK(lease.publish(valid, initial, initialSource, true));
    expectSample(lease.current(initial, initialSource, true, 100, callerWindowMs), valid);
    expectSample(lease.current(initial, initialSource, true, 1599, callerWindowMs), valid);
    expectSample(lease.current(initial, initialSource, true, 1600, callerWindowMs), valid);
    CHECK(!lease.current(initial, initialSource, true, 1601, callerWindowMs));
    CHECK(!lease.current(initial, initialSource, true, 10000, callerWindowMs));
    // Caller-selected windows, including zero, are obeyed exactly.
    expectSample(lease.current(initial, initialSource, true, 107, 7), valid);
    CHECK(!lease.current(initial, initialSource, true, 108, 7));
}

void futureTick() {
    Lease lease;
    CHECK(lease.publish(sample(initial, 100), initial, initialSource, true));
    CHECK(!lease.current(initial, initialSource, true, 99, callerWindowMs));
    CHECK(!lease.current(initial, initialSource, true, 99, std::numeric_limits<std::uint64_t>::max()));
    CHECK(!lease.current(initial, initialSource, true, 0, std::numeric_limits<std::uint64_t>::max()));
    const auto lastTick = std::numeric_limits<std::uint64_t>::max();
    CHECK(lease.publish(sample(initial, lastTick), initial, initialSource, true));
    CHECK(!lease.current(initial, initialSource, true, lastTick - 1, lastTick));
}

void finiteAllSixaxes() {
    const double invalid[] = {std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity()};
    for (std::size_t axis = 0; axis < 6; ++axis) {
        for (const auto value : invalid) {
            auto bad = sample(initial, 101);
            (axis < 3 ? bad.gyro[axis] : bad.accel[axis - 3]) = value;
            Lease empty;
            CHECK(!empty.publish(bad, initial, initialSource, true));
            CHECK(!empty.current(initial, initialSource, true, 101, callerWindowMs));
            Lease lease;
            const auto valid = sample();
            CHECK(lease.publish(valid, initial, initialSource, true));
            CHECK(!lease.publish(bad, initial, initialSource, true));
            expectSample(lease.current(initial, initialSource, true, 101, callerWindowMs), valid);
        }
    }
    auto finite = sample();
    finite.gyro = {std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(),
                   std::numeric_limits<double>::denorm_min()};
    finite.accel = {0.0, -0.0, std::numeric_limits<double>::min()};
    Lease lease;
    CHECK(lease.publish(finite, initial, initialSource, true));
    expectSample(lease.current(initial, initialSource, true, 100, 0), finite);
}

void repeatedAndStationary() {
    Lease lease;
    auto unchanged = sample();
    CHECK(lease.publish(unchanged, initial, initialSource, true));
    CHECK(lease.publish(unchanged, initial, initialSource, true));
    unchanged.capturedAt = 1700;
    CHECK(lease.publish(unchanged, initial, initialSource, true));
    expectSample(lease.current(initial, initialSource, true, 1700, 0), unchanged);
    auto still = Sample{{0.0, -0.0, 0.0}, {-0.0, 0.0, -0.0}, 1800, initial, initialSource};
    CHECK(lease.publish(still, initial, initialSource, true));
    expectSample(lease.current(initial, initialSource, true, 1800, 0), still);
    still.capturedAt = 3400;
    CHECK(lease.publish(still, initial, initialSource, true));
    expectSample(lease.current(initial, initialSource, true, 3400, 0), still);
}

void sleepWakePower() {
    const Context awake{8, 11};
    Lease lease;
    CHECK(lease.publish(sample(), initial, initialSource, true));
    CHECK(!lease.current(initial, initialSource, false, 101, callerWindowMs));
    lease.revoke(); // The existing power owner retires under its own mutex.
    CHECK(!lease.publish(sample(initial, 101), awake, initialSource, false));
    CHECK(!lease.publish(sample(awake, 101), awake, initialSource, false));
    CHECK(!lease.current(awake, initialSource, false, 101, callerWindowMs));
    CHECK(!lease.publish(sample(initial, 102), awake, initialSource, true));
    CHECK(!lease.current(awake, initialSource, true, 102, callerWindowMs));
    CHECK(!lease.current(initial, initialSource, true, 102, callerWindowMs));
    const auto fresh = sample(awake, 103);
    CHECK(lease.publish(fresh, awake, initialSource, true));
    expectSample(lease.current(awake, initialSource, true, 103, 0), fresh);
    CHECK(!lease.publish(sample(initial, 104), awake, initialSource, true));
    expectSample(lease.current(awake, initialSource, true, 104, callerWindowMs), fresh);
    CHECK(!lease.current(initial, initialSource, true, 104, callerWindowMs));
}

void rawRebindAndWakeBoth() {
    const Context rebound{7, 12};
    const Context awake{8, 13};
    Lease lease;
    CHECK(lease.publish(sample(), initial, initialSource, true));
    CHECK(!lease.current(rebound, initialSource, true, 101, callerWindowMs));
    lease.revoke();
    CHECK(!lease.publish(sample(initial, 101), rebound, initialSource, true));
    CHECK(!lease.current(rebound, initialSource, true, 101, callerWindowMs));
    CHECK(lease.publish(sample(rebound, 102), rebound, initialSource, true));
    lease.revoke();
    CHECK(!lease.publish(sample(rebound, 103), awake, initialSource, false));
    CHECK(!lease.publish(sample(rebound, 103), awake, initialSource, true));
    CHECK(!lease.publish(sample({awake.power, rebound.rawGeneration}, 103), awake, initialSource, true));
    CHECK(!lease.publish(sample({rebound.power, awake.rawGeneration}, 103), awake, initialSource, true));
    CHECK(!lease.current(awake, initialSource, true, 103, callerWindowMs));
    const auto fresh = sample(awake, 104);
    CHECK(lease.publish(fresh, awake, initialSource, true));
    expectSample(lease.current(awake, initialSource, true, 104, 0), fresh);
}

void revokeNoResurrection() {
    Lease lease;
    CHECK(lease.publish(sample(), initial, initialSource, true));
    lease.revoke();
    CHECK(!lease.current(initial, initialSource, true, 100, callerWindowMs));
    CHECK(!lease.current(initial, initialSource, false, 100, callerWindowMs));
    CHECK(!lease.publish(sample(initial, 101), initial, initialSource, false));
    CHECK(!lease.current(initial, initialSource, true, 101, callerWindowMs));
    CHECK(!lease.publish(sample(initial, 0), initial, initialSource, true));
    CHECK(!lease.current(initial, initialSource, true, 101, callerWindowMs));
    lease.revoke();
    CHECK(!lease.current(initial, initialSource, true, 1000000, 1000000));
    const auto fresh = sample(initial, 102);
    CHECK(lease.publish(fresh, initial, initialSource, true));
    expectSample(lease.current(initial, initialSource, true, 102, 0), fresh);
}

void uint64Edges() {
    const auto max = std::numeric_limits<std::uint64_t>::max();
    const Context high{max, max - 1};
    Lease lease;
    const auto nearEnd = sample(high, max - 1500);
    CHECK(lease.publish(nearEnd, high, initialSource, true));
    expectSample(lease.current(high, initialSource, true, max, 1500), nearEnd);
    CHECK(!lease.current(high, initialSource, true, max, 1499));
    const auto atEnd = sample(high, max);
    CHECK(lease.publish(atEnd, high, initialSource, true));
    expectSample(lease.current(high, initialSource, true, max, 0), atEnd);
    CHECK(!lease.current({max - (1ULL << 32), max - 1}, initialSource, true, max, 0));
    CHECK(!lease.current({max, max - 1 - (1ULL << 32)}, initialSource, true, max, 0));
    const auto earliest = sample(high, 1);
    CHECK(lease.publish(earliest, high, initialSource, true));
    expectSample(lease.current(high, initialSource, true, max, max - 1), earliest);
    CHECK(!lease.current(high, initialSource, true, max, max - 2));
}


// No power/raw transition: source dimensions independently fence cached samples.
void physicalIdentityChange() {
    Lease lease;
    const auto old = sample();
    auto selected = initialSource;
    CHECK(lease.publish(old, initial, initialSource, true));
    selected.physicalIdentity = "baseContainer:physical-B;VID_17EF&PID_6182";
    CHECK(!lease.current(initial, selected, true, 101, callerWindowMs));
    CHECK(!lease.publish(old, initial, selected, true));
    auto fresh = sample(initial, 102); fresh.source = selected;
    CHECK(lease.publish(fresh, initial, selected, true));
    expectSample(lease.current(initial, selected, true, 102, 0), fresh);
    CHECK(!lease.current(initial, initialSource, true, 103, callerWindowMs));
    CHECK(!lease.publish(fresh, initial, initialSource, true));
    CHECK(!lease.publish(old, initial, selected, true));
    expectSample(lease.current(initial, selected, true, 103, callerWindowMs), fresh);
}
void sourceCaptureEpochChange() {
    Lease lease;
    const auto old = sample();
    CHECK(lease.publish(old, initial, initialSource, true));
    auto selected = initialSource; ++selected.captureEpoch;
    // Stop/Start can keep identity, power, raw generation and sensor unchanged.
    CHECK(!lease.current(initial, selected, true, 101, callerWindowMs));
    CHECK(!lease.publish(old, initial, selected, true));
    auto fresh = sample(initial, 102); fresh.source = selected;
    CHECK(lease.publish(fresh, initial, selected, true));
    expectSample(lease.current(initial, selected, true, 102, 0), fresh);
    CHECK(!lease.current(initial, initialSource, true, 103, callerWindowMs));
    CHECK(!lease.publish(fresh, initial, initialSource, true));
    CHECK(!lease.publish(old, initial, selected, true));
}
void sourceSensorChange() {
    Lease lease;
    auto selected = initialSource; selected.sensorIndex = 1;
    const auto old = sample();
    CHECK(lease.publish(old, initial, initialSource, true));
    CHECK(!lease.current(initial, selected, true, 101, callerWindowMs));
    CHECK(!lease.publish(old, initial, selected, true));
    auto fresh = sample(initial, 102); fresh.source = selected;
    fresh.gyro.fill(0.0); fresh.accel.fill(0.0);
    CHECK(lease.publish(fresh, initial, selected, true));
    expectSample(lease.current(initial, selected, true, 102, 0), fresh);
    CHECK(!lease.current(initial, initialSource, true, 103, callerWindowMs));
    CHECK(!lease.publish(fresh, initial, initialSource, true));
    CHECK(!lease.publish(old, initial, selected, true));
}
void invalidSources() {
    const Source invalid[] = {
        {"", initialSource.captureEpoch, 0},
        {initialSource.physicalIdentity, 0, 0},
        {initialSource.physicalIdentity, initialSource.captureEpoch, 2},
        {initialSource.physicalIdentity, initialSource.captureEpoch, std::numeric_limits<unsigned>::max()},
        {}
    };
    for (const auto& source : invalid) {
        Lease lease;
        auto bad = sample(); bad.source = source;
        CHECK(!lease.publish(bad, initial, source, true)); // equally invalid is not acceptable
        CHECK(!lease.publish(bad, initial, initialSource, true));
        CHECK(!lease.publish(sample(), initial, source, true));
        CHECK(!lease.current(initial, source, true, 100, callerWindowMs));
        const auto good = sample();
        CHECK(lease.publish(good, initial, initialSource, true));
        CHECK(!lease.current(initial, source, true, 101, callerWindowMs));
        CHECK(!lease.publish(bad, initial, source, true));
        CHECK(!lease.publish(bad, initial, initialSource, true));
        CHECK(!lease.publish(good, initial, source, true));
        CHECK(!lease.current(initial, source, true, 101, callerWindowMs));
        expectSample(lease.current(initial, initialSource, true, 101, callerWindowMs), good);
    }
    // Exact owned identity comparison; no trimming, case folding or prefix match.
    for (const auto& identity : {initialSource.physicalIdentity + "-suffix",
                               std::string("basecontainer:physical-A;VID_17EF&PID_6182")}) {
        Lease lease; CHECK(lease.publish(sample(), initial, initialSource, true));
        auto other = initialSource; other.physicalIdentity = identity;
        CHECK(!lease.current(initial, other, true, 100, callerWindowMs));
    }
}
void sourceEpochWidthAndOwnership() {
    Lease lease;
    auto origin = initialSource;
    origin.physicalIdentity = std::string(400, 'a') + "A";
    origin.captureEpoch = std::numeric_limits<std::uint64_t>::max();
    auto input = sample(); input.source = origin;
    CHECK(lease.publish(input, initial, origin, true));
    const auto expected = input;
    input.source.physicalIdentity.back() = 'B';
    input.source.captureEpoch = 1;
    expectSample(lease.current(initial, origin, true, 100, 0), expected);
    auto low = origin; low.captureEpoch -= (1ULL << 32);
    CHECK(!lease.current(initial, low, true, 100, 0));
    auto bad = sample(); bad.source = low;
    CHECK(!lease.publish(bad, initial, origin, true));
}

struct Scenario { const char* name; void (*run)(); };
const Scenario scenarios[] = {
    {"source-physical-identity-change", physicalIdentityChange},
    {"source-capture-epoch-change", sourceCaptureEpochChange},
    {"source-sensor-change", sourceSensorChange},
    {"invalid-sources", invalidSources},
    {"source-epoch-width-and-ownership", sourceEpochWidthAndOwnership},
    {"empty-and-revoke", emptyAndRevoke},
    {"full-sixaxis-and-copies", fullSixaxisAndCopies},
    {"zero-generations", zeroGenerations},
    {"publish-power-fence", publishPowerFence},
    {"publish-raw-fence", publishRawFence},
    {"current-power-fence", currentPowerFence},
    {"current-raw-fence", currentRawFence},
    {"publish-ready-fence", publishReadyFence},
    {"current-ready-fence", currentReadyFence},
    {"zero-tick-and-zero-window", zeroTickAndZeroWindow},
    {"age-boundaries", ageBoundaries},
    {"future-tick", futureTick},
    {"finite-all-sixaxes", finiteAllSixaxes},
    {"repeated-and-stationary", repeatedAndStationary},
    {"sleep-wake-power", sleepWakePower},
    {"raw-rebind-and-wake-both", rawRebindAndWakeBoth},
    {"revoke-no-resurrection", revokeNoResurrection},
    {"uint64-edges", uint64Edges},
};
} // namespace

int main() {
    int failedCases = 0;
    for (const auto& scenario : scenarios) {
        activeCase = scenario.name;
        const int beforeChecks = checks;
        const int beforeFailures = failures;
        scenario.run();
        const int failed = failures - beforeFailures;
        if (failed) ++failedCases;
        std::printf("CASE %s checks=%d failures=%d\n", activeCase, checks - beforeChecks, failed);
    }
    std::printf("SUMMARY cases=%zu checks=%d failures=%d failedCases=%d\n",
                sizeof(scenarios) / sizeof(scenarios[0]), checks, failures, failedCases);
    return failures == 0 ? 0 : 1;
}
