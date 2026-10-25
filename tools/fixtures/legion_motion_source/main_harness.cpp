// Deterministic interleavings through three exact main.cpp helpers.
// Product globals, power lifecycle and clock inputs are controlled stand-ins.
#include "legion_motion_source.h"
#include "legion_motion_message_clock.h"
#include "virtual_output_status.h"
#include <atomic>
#include <cstdio>
#include <limits>
#include <mutex>

using ymcc::legion_motion::Context;
using ymcc::legion_motion::Sample;
using ymcc::legion_motion::Source;
using ULONGLONG = std::uint64_t;
using std::uint64_t;

enum class PowerLifecycle { Ready, Suspending, Suspended, Resuming };
enum class YmccFamily { Unknown, LenovoLegionGo, RogAlly };
struct MachineIdentity { YmccFamily family = YmccFamily::LenovoLegionGo; };
static std::mutex g_hidHidePhysicalIdentityMx;
static std::mutex g_legionImuMx;
static ymcc::legion_motion::Lease g_legionMotion;
static std::atomic<std::uint64_t> g_legionMotionCaptureEpoch{1};
static std::atomic<std::uint64_t> g_legionMotionEpochStartedAt{0};
static std::uint64_t g_testClockMs = 100;
static std::uint64_t GetTickCount64() { return g_testClockMs; }
static std::atomic<std::uint64_t> g_powerGeneration{7};
static std::atomic<std::uint64_t> g_hidResolveCacheGeneration{11};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static bool g_inputCaptureReady = true;
static std::atomic<bool> g_inputCaptureStop{false};
static std::atomic<bool> g_legionRawInputSubscribed{true};
static MachineIdentity g_machineIdentity;
static unsigned long long currentPowerGeneration() {
    return g_powerGeneration.load(std::memory_order_acquire);
}

// Controlled selected-source stub: no actual selection, identity lookup, HID or
// physical binding is executed. Three production helpers below remain verbatim.
static std::string g_testSelectedPhysicalIdentity = "baseContainer:A|VID_6127|PID_24962";
static unsigned g_testSelectedSensorIndex = 0;
static bool g_testLegionRoute = true;
static bool inputCaptureLegionControllerMotionRoute() {
    return g_testLegionRoute && g_machineIdentity.family == YmccFamily::LenovoLegionGo;
}
static Source legionMotionSourceForSelectedLocked() {
    return {g_testSelectedPhysicalIdentity, g_legionMotionCaptureEpoch.load(std::memory_order_acquire),
            g_testSelectedSensorIndex};
}

#include "main_helpers.generated.h"

namespace {
int checks = 0, failures = 0;
const char* activeCase = "uninitialized";
void check(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL %s line=%d expression=%s\n", activeCase, line, expression);
    }
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)
Sample capture(std::uint64_t tick = 100) {
    return Sample{{1.25, -2.5, 3.75}, {-4.25, 5.5, -6.75}, tick,
                  {currentPowerGeneration(), g_hidResolveCacheGeneration.load()}, legionMotionSourceForSelectedLocked()};
}
std::uint64_t epoch() { return g_legionMotionCaptureEpoch.load(); }
void reset() {
    g_legionMotion.revoke();
    g_testSelectedPhysicalIdentity = "baseContainer:A|VID_6127|PID_24962";
    g_testSelectedSensorIndex = 0;
    g_testLegionRoute = true;
    g_legionMotionCaptureEpoch.store(1);
    g_legionMotionEpochStartedAt.store(0);
    g_testClockMs = 100;
    g_powerGeneration.store(7);
    g_hidResolveCacheGeneration.store(11);
    g_powerLifecycle.store(PowerLifecycle::Ready);
    g_inputCaptureReady = true;
    g_inputCaptureStop.store(false);
    g_legionRawInputSubscribed.store(true);
    g_machineIdentity.family = YmccFamily::LenovoLegionGo;
}
void expect(const Sample& expected, std::uint64_t now) {
    const auto actual = legionMotionSnapshot(now);
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
void fullSixaxis() {
    const auto input = capture();
    CHECK(legionMotionPublish(input, epoch()));
    expect(input, 100);
    auto returned = legionMotionSnapshot(100);
    if (returned) returned->gyro.fill(99.0);
    expect(input, 100);
}
void stationaryZero() {
    auto still = capture();
    still.gyro.fill(0.0);
    still.accel.fill(0.0);
    CHECK(legionMotionPublish(still, epoch()));
    expect(still, 100);
    CHECK(legionMotionPublish(still, epoch()));
    still.capturedAt = 1700;
    CHECK(legionMotionPublish(still, epoch()));
    expect(still, 1700);
}
void readyGate() {
    const auto input = capture();
    CHECK(legionMotionPublish(input, epoch()));
    g_inputCaptureReady = false;
    CHECK(!legionMotionPublish(capture(101), epoch()));
    CHECK(!legionMotionSnapshot(101));
    g_inputCaptureReady = true;
    expect(input, 101); // Predicate-only simulation; no retirement was requested.
}
void stopGate() {
    const auto input = capture();
    CHECK(legionMotionPublish(input, epoch()));
    g_inputCaptureStop.store(true);
    CHECK(!legionMotionPublish(capture(101), epoch()));
    CHECK(!legionMotionSnapshot(101));
    g_inputCaptureStop.store(false);
    g_legionRawInputSubscribed.store(true);
    expect(input, 101);
}
void familyGate() {
    const auto input = capture();
    CHECK(legionMotionPublish(input, epoch()));
    for (const auto family : {YmccFamily::Unknown, YmccFamily::RogAlly}) {
        g_machineIdentity.family = family;
        CHECK(!legionMotionPublish(capture(101), epoch()));
        CHECK(!legionMotionSnapshot(101));
    }
    g_machineIdentity.family = YmccFamily::LenovoLegionGo;
    expect(input, 101);
}
void routeGate() {
    const auto input = capture();
    CHECK(legionMotionPublish(input, epoch()));
    g_testLegionRoute = false;
    CHECK(!legionMotionPublish(capture(101), epoch()));
    CHECK(!legionMotionSnapshot(101));
    g_testLegionRoute = true;
    expect(input, 101);
}
void phaseGate() {
    const auto input = capture();
    CHECK(legionMotionPublish(input, epoch()));
    for (const auto phase : {PowerLifecycle::Suspending, PowerLifecycle::Suspended,
                             PowerLifecycle::Resuming}) {
        g_powerLifecycle.store(phase);
        CHECK(!legionMotionPublish(capture(101), epoch()));
        CHECK(!legionMotionSnapshot(101));
    }
    g_powerLifecycle.store(PowerLifecycle::Ready);
    expect(input, 101);
}
void decoderPowerRace() {
    const auto inFlight = capture();
    const auto capturedEpoch = epoch();
    CHECK(legionMotionPublish(inFlight, capturedEpoch));
    g_powerGeneration.fetch_add(1);
    CHECK(!legionMotionSnapshot(101));
    CHECK(!legionMotionPublish(inFlight, capturedEpoch));
    const auto fresh = capture(102);
    CHECK(legionMotionPublish(fresh, epoch()));
    expect(fresh, 102);
    CHECK(!legionMotionPublish(inFlight, capturedEpoch));
    expect(fresh, 103);
}
void decoderRawRace() {
    const auto inFlight = capture();
    const auto capturedEpoch = epoch();
    CHECK(legionMotionPublish(inFlight, capturedEpoch));
    g_hidResolveCacheGeneration.fetch_add(1);
    CHECK(!legionMotionSnapshot(101));
    CHECK(!legionMotionPublish(inFlight, capturedEpoch));
    const auto fresh = capture(102);
    CHECK(legionMotionPublish(fresh, epoch()));
    expect(fresh, 102);
    CHECK(!legionMotionPublish(inFlight, capturedEpoch));
    expect(fresh, 103);
}
void stopStartSameContext() {
    for (std::uint64_t cycle = 0; cycle < 3; ++cycle) {
        const auto base = 100 + cycle * 10;
        const auto inFlight = capture(base);
        const auto capturedEpoch = epoch();
        CHECK(legionMotionPublish(inFlight, capturedEpoch));
        g_inputCaptureStop.store(true);
        g_inputCaptureReady = false;
        legionMotionRevoke();
        CHECK(epoch() == capturedEpoch + 1);
        CHECK(!legionMotionSnapshot(base + 1));
        CHECK(!legionMotionPublish(inFlight, capturedEpoch));
        g_inputCaptureStop.store(false);
    g_legionRawInputSubscribed.store(true);
        g_inputCaptureReady = true;
        // Same power/raw context, fully Ready again; only the capture epoch
        // distinguishes the late pre-Stop decoder from a new acquisition.
        CHECK(!legionMotionPublish(inFlight, capturedEpoch));
        CHECK(!legionMotionSnapshot(base + 2));
        auto fresh = capture(base + 3);
        fresh.gyro.fill(0.0);
        fresh.accel.fill(0.0);
        CHECK(legionMotionPublish(fresh, epoch()));
        expect(fresh, base + 3);
        CHECK(!legionMotionPublish(inFlight, capturedEpoch));
        expect(fresh, base + 4);
    }
}
void sleepWakeNewContext() {
    const auto inFlight = capture();
    const auto capturedEpoch = epoch();
    CHECK(legionMotionPublish(inFlight, capturedEpoch));
    g_powerLifecycle.store(PowerLifecycle::Suspending);
    legionMotionRevoke();
    g_powerGeneration.fetch_add(1);
    g_hidResolveCacheGeneration.fetch_add(1);
    g_powerLifecycle.store(PowerLifecycle::Suspended);
    CHECK(!legionMotionPublish(inFlight, capturedEpoch));
    CHECK(!legionMotionSnapshot(101));
    g_powerLifecycle.store(PowerLifecycle::Resuming);
    CHECK(!legionMotionPublish(capture(102), epoch()));
    CHECK(!legionMotionSnapshot(102));
    g_powerLifecycle.store(PowerLifecycle::Ready);
    CHECK(!legionMotionPublish(inFlight, capturedEpoch));
    CHECK(!legionMotionPublish(inFlight, epoch())); // Lease context still fences it.
    CHECK(!legionMotionSnapshot(103));
    const auto fresh = capture(104);
    CHECK(legionMotionPublish(fresh, epoch()));
    expect(fresh, 104);
}
void revokeNoResurrection() {
    const auto input = capture();
    const auto oldEpoch = epoch();
    CHECK(legionMotionPublish(input, oldEpoch));
    legionMotionRevoke();
    CHECK(epoch() == oldEpoch + 1);
    CHECK(!legionMotionSnapshot(100));
    CHECK(!legionMotionPublish(input, oldEpoch));
    CHECK(!legionMotionSnapshot(101));
    legionMotionRevoke();
    CHECK(epoch() == oldEpoch + 2);
    CHECK(!legionMotionPublish(input, oldEpoch + 1));
    CHECK(!legionMotionSnapshot(102));
    const auto fresh = capture(103);
    CHECK(legionMotionPublish(fresh, epoch()));
    expect(fresh, 103);
}
void captureEpochEdges() {
    const auto input = capture();
    CHECK(!legionMotionPublish(input, 0));
    CHECK(!legionMotionPublish(input, epoch() + 1));
    CHECK(!legionMotionSnapshot(100));
    g_legionMotionCaptureEpoch.store(0);
    CHECK(!legionMotionPublish(input, 0));
    CHECK(!legionMotionSnapshot(100));
    g_legionMotionCaptureEpoch.store(std::numeric_limits<std::uint64_t>::max());
    const auto oldEpoch = epoch();
    const auto atEnd = capture();
    CHECK(legionMotionPublish(atEnd, oldEpoch));
    legionMotionRevoke();
    CHECK(epoch() == 1);
    CHECK(!legionMotionSnapshot(100));
    CHECK(!legionMotionPublish(input, oldEpoch));
    CHECK(!legionMotionPublish(input, 0));
    CHECK(!legionMotionSnapshot(101));
    const auto fresh = capture(102);
    CHECK(legionMotionPublish(fresh, epoch()));
    expect(fresh, 102);
}
void windowAndInvalid() {
    static_assert(ymcc::kVirtualOutputSourceWindowMaxAgeMs == 1500);
    const auto valid = capture();
    CHECK(legionMotionPublish(valid, epoch()));
    CHECK(!legionMotionSnapshot(99));
    expect(valid, 1600);
    CHECK(!legionMotionSnapshot(1601));
    auto bad = capture(101);
    bad.accel[2] = std::numeric_limits<double>::infinity();
    CHECK(!legionMotionPublish(bad, epoch()));
    expect(valid, 101);
    bad = capture(0);
    CHECK(!legionMotionPublish(bad, epoch()));
    expect(valid, 101);
}

void selectedPhysicalSourceChange() {
    const auto old = capture(); const auto oldEpoch = epoch();
    CHECK(legionMotionPublish(old, oldEpoch));
    g_testSelectedPhysicalIdentity = "baseContainer:B|VID_6127|PID_24962";
    CHECK(!legionMotionSnapshot(101));
    CHECK(!legionMotionPublish(old, oldEpoch));
    const auto fresh = capture(102);
    CHECK(legionMotionPublish(fresh, epoch()));
    expect(fresh, 102);
    CHECK(!legionMotionPublish(old, oldEpoch));
    expect(fresh, 103);
}
void selectedSensorChange() {
    const auto old = capture(); const auto oldEpoch = epoch();
    CHECK(legionMotionPublish(old, oldEpoch));
    g_testSelectedSensorIndex = 1;
    CHECK(!legionMotionSnapshot(101));
    CHECK(!legionMotionPublish(old, oldEpoch));
    auto fresh = capture(102); fresh.gyro.fill(0.0); fresh.accel.fill(0.0);
    CHECK(legionMotionPublish(fresh, epoch()));
    expect(fresh, 102);
    CHECK(!legionMotionPublish(old, oldEpoch));
}
void emptySelectedIdentity() {
    const auto old = capture();
    CHECK(legionMotionPublish(old, epoch()));
    g_testSelectedPhysicalIdentity.clear();
    CHECK(!legionMotionSnapshot(101));
    CHECK(!legionMotionPublish(old, epoch()));
    CHECK(!legionMotionPublish(capture(102), epoch()));
    CHECK(!legionMotionSnapshot(102));
}
void invalidSelectedSource() {
    CHECK(legionMotionPublish(capture(), epoch()));
    g_testSelectedSensorIndex = 2;
    CHECK(!legionMotionSnapshot(101));
    CHECK(!legionMotionPublish(capture(101), epoch()));
    g_testSelectedSensorIndex = 0;
    g_legionMotionCaptureEpoch.store(0);
    CHECK(!legionMotionSnapshot(102));
    CHECK(!legionMotionPublish(capture(102), 0));
}
void forgedEpochArgument() {
    const auto old = capture();
    CHECK(legionMotionPublish(old, epoch()));
    legionMotionRevoke();
    CHECK(!legionMotionPublish(old, epoch())); // current arg cannot relabel old Sample.source
    CHECK(!legionMotionSnapshot(101));
    const auto fresh = capture(102);
    CHECK(legionMotionPublish(fresh, epoch()));
    expect(fresh, 102);
}

void registrationGate() {
    CHECK(legionMotionPublish(capture(), epoch()));
    g_legionRawInputSubscribed.store(false);
    CHECK(!legionMotionSnapshot(101));
    CHECK(!legionMotionPublish(capture(101), epoch()));
    legionMotionRevoke();
    g_legionRawInputSubscribed.store(true);
    const auto fresh=capture(102);
    CHECK(legionMotionPublish(fresh, epoch()));
    expect(fresh,102);
}
void messageEpochCutover() {
    g_testClockMs = 201;
    const auto previousEpoch = epoch();
    legionMotionRevoke();
    CHECK(epoch() == previousEpoch + 1);
    CHECK(g_legionMotionEpochStartedAt.load() == 201);
    CHECK(!legionMotionPublish(capture(200), epoch())); // current label, old queue time
    CHECK(!legionMotionSnapshot(201));
    const auto equality = capture(201);
    CHECK(!legionMotionPublish(equality, epoch())); // same tick cannot prove a post-cutover message
    CHECK(!legionMotionSnapshot(201));
    const auto nextTick = capture(202);
    CHECK(legionMotionPublish(nextTick, epoch()));
    expect(nextTick,202);
    // Exercise the snapshot boundary independently of the publish guard.
    g_legionMotionEpochStartedAt.store(202);
    CHECK(!legionMotionSnapshot(202)); // stored sample now equals cutover and must be rejected
    const auto current = capture(203);
    CHECK(legionMotionPublish(current, epoch()));
    expect(current,203);
}
struct Scenario { const char* name; void (*run)(); };
const Scenario scenarios[] = {
    {"main-queue-time-cutover", messageEpochCutover},
    {"main-registration-gate", registrationGate},
    {"main-selected-physical-source-change", selectedPhysicalSourceChange},
    {"main-selected-sensor-change", selectedSensorChange},
    {"main-empty-selected-identity", emptySelectedIdentity},
    {"main-invalid-selected-source", invalidSelectedSource},
    {"main-forged-epoch-argument", forgedEpochArgument},
    {"main-full-sixaxis", fullSixaxis},
    {"main-stationary-zero", stationaryZero},
    {"main-ready-gate", readyGate},
    {"main-stop-gate", stopGate},
    {"main-family-gate", familyGate},
    {"main-route-gate", routeGate},
    {"main-phase-gate", phaseGate},
    {"main-decoder-power-race", decoderPowerRace},
    {"main-decoder-raw-race", decoderRawRace},
    {"main-stop-start-same-context", stopStartSameContext},
    {"main-sleep-wake-new-context", sleepWakeNewContext},
    {"main-revoke-no-resurrection", revokeNoResurrection},
    {"main-window-and-invalid", windowAndInvalid},
    {"main-capture-epoch-edges", captureEpochEdges},
};
} // namespace
int main() {
    int failedCases = 0;
    for (const auto& scenario : scenarios) {
        reset();
        activeCase = scenario.name;
        const int beforeChecks = checks, beforeFailures = failures;
        scenario.run();
        const int failed = failures - beforeFailures;
        if (failed) ++failedCases;
        std::printf("CASE %s checks=%d failures=%d\n", activeCase, checks - beforeChecks, failed);
    }
    std::printf("SUMMARY cases=%zu checks=%d failures=%d failedCases=%d\n",
                sizeof(scenarios) / sizeof(scenarios[0]), checks, failures, failedCases);
    return failures == 0 ? 0 : 1;
}
