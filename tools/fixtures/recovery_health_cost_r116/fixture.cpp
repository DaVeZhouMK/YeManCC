// R116 inert source-exact adapter/canonical-commit fixture. No Host/device calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#include "json.hpp"
#include "input_host_recovery_health.h"
#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <new>
#include <string>
#include <vector>
using json = nlohmann::json;
static bool measureAllocations = false;
static unsigned long long allocationCount = 0;
void* operator new(std::size_t size) {
    if (measureAllocations) ++allocationCount;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
struct Counts {
    unsigned long long board = 0, repair = 0, eligible = 0, armed = 0;
    unsigned long long configuredCopies = 0, boundCopies = 0, snapshotLocks = 0;
    unsigned long long source = 0, context = 0, tracking = 0;
} counts;
#if R116_COUNT
struct ArmedProbe {
    std::atomic<bool> value{true};
    unsigned reads = 0, revokeOnRead = 0;
    bool load(std::memory_order order) {
        ++counts.armed;
        if (revokeOnRead && ++reads == revokeOnRead) value.store(false, std::memory_order_release);
        return value.load(order);
    }
    void store(bool v, std::memory_order order = std::memory_order_seq_cst) { value.store(v, order); }
} g_msiDinputRouteDinputArmed;
#else
static std::atomic<bool> g_msiDinputRouteDinputArmed{true};
#endif
static ULONGLONG fakeNow = 100;
static ULONGLONG r116Tick() { return fakeNow; }
#define GetTickCount64 r116Tick
static std::atomic<unsigned long long> g_powerGeneration{1}, g_inputHostPresentationWorkGeneration{1};
static std::atomic<unsigned> g_xinputTopologyEpoch{1}, g_xinputSlotIdentityEpoch{1};
static std::atomic<unsigned long long> g_msiMainSourceRevision{1};
static std::atomic<bool> g_msiDinputRouteConfirmed{true}, g_msiDinputSourceValid{true};
struct { std::atomic<bool> enabled{true}, mainInterfaceMatched{true}; std::atomic<unsigned> productId{0x1902}; } g_msiDinput;
static constexpr struct { unsigned pidDInput = 0x1902; } kMsiClawCapability;
enum class YmccFamily { Unknown, MsiClaw, Other };
struct { YmccFamily family = YmccFamily::MsiClaw; std::string manufacturer, product; } g_machineIdentity;
static std::mutex g_inputHostPersonaMx, g_inputHostMx, g_inputCaptureGamepadSnapshotMx;
struct { bool enabled = true, virtualTargetEnabled = true; std::string virtualTargetPersona = "elite"; } g_realStickTest;
static std::string g_inputHostBoundPersona = "elite";
static std::atomic<bool> g_inputHostExitFence{false}, g_inputHostPowerAdmissionRevoked{false};
static std::atomic<bool> g_externalPriorityVirtualSuspended{false}, g_inputReady{true};
static std::atomic<bool> g_inputHostFaultHoldsStart{false}, g_inputHostRecoveryIntentReset{false};
static std::atomic<bool> g_inputHostRecoveryTerminalLogged{false}, g_inputHostRecoveryPending{false}, g_inputHostTupleHealPending{false};
static std::atomic<int> g_inputHostRecoveryEpisodeAttempts{0};
static std::atomic<ULONGLONG> g_inputHostHealthySinceTick{0};
static unsigned long long g_inputHostEpoch = 7, g_inputHostBoundPowerGeneration = 1;
static bool g_inputHostProcess = true, g_inputHostPrepared = true, g_inputHostNeutralized = true, g_inputHostTransportFault = false;
static std::atomic<bool> g_physicalReleaseWindowNeutral{false};
enum class PowerLifecycle { Ready, Suspended, Resuming };
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
// Only the non-Ready platform/environment leaf is inert. Ready uses actual code.
static bool keepInputStandby = false;
static bool sgShouldKeepInputDuringStandby() { return keepInputStandby; }
static ymcc::input::recoveryHealth::Window g_inputHostRecoveryHealth;
static bool g_inputHostRecoveryHealthWasTracking = false;
@@SNAPSHOT@@
static InputCaptureGamepadSnapshot g_inputCaptureGamepadSnapshot;
static const InputCaptureGamepadSnapshot* t_commonFrameSource = nullptr;
@@FUNCTIONS@@

static DWORD g_xinputPrimarySlot = 0;
static void producer(const std::string& route, bool live = true) {
    const XINPUT_GAMEPAD pad{};
    const DWORD admittedMask = route == "msi-dinput" ? 0u : 1u;
    const std::string& inputSource = route;
    const auto probeNow = fakeNow;
    const auto diagnosticReadPower = currentPowerGeneration();
    const auto diagnosticReadTopology = g_xinputTopologyEpoch.load();
    const auto diagnosticReadIdentity = g_xinputSlotIdentityEpoch.load();
    const auto diagnosticReadMsiSourceRevision = g_msiMainSourceRevision.load();
    std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);
@@PRODUCER@@
}
static void frame(bool ack = true, bool eligible = true, bool neutral = false,
                  const std::function<void()>& afterCapture = {}) {
    const bool physicalSourceAdmitted = eligible, semanticNeutral = neutral;
    t_commonFrameSource = &g_inputCaptureGamepadSnapshot;
    {
        std::lock_guard<std::mutex> lock(g_inputHostMx);
@@OBSERVE@@
    }
    std::lock_guard<std::mutex> lock(g_inputHostMx);
@@REQUEST@@
    if (afterCapture) afterCapture();
    if (ack) {
@@ACCEPTED@@
    }
    t_commonFrameSource = nullptr;
}
static std::string activeRoute = "msi-dinput";
static void reset(bool tracking = true, const std::string& board = "MS-1T91") {
    fakeNow = 100;
    g_powerGeneration = 1; g_inputHostPresentationWorkGeneration = 1;
    g_xinputTopologyEpoch = 1; g_xinputSlotIdentityEpoch = 1; g_msiMainSourceRevision = 1;
    g_msiDinputRouteDinputArmed.store(true); g_msiDinputRouteConfirmed = true; g_msiDinputSourceValid = true;
#if R116_COUNT
    g_msiDinputRouteDinputArmed.reads = 0; g_msiDinputRouteDinputArmed.revokeOnRead = 0;
#endif
    g_msiDinput.enabled = true; g_msiDinput.mainInterfaceMatched = true; g_msiDinput.productId = 0x1902;
    g_machineIdentity.family = YmccFamily::MsiClaw;
    g_machineIdentity.manufacturer = "MICRO-STAR INTERNATIONAL CO., LTD."; g_machineIdentity.product = board;
    g_realStickTest.enabled = true; g_realStickTest.virtualTargetEnabled = true;
    g_realStickTest.virtualTargetPersona = "elite"; g_inputHostBoundPersona = "elite";
    g_inputHostExitFence = false; g_inputHostPowerAdmissionRevoked = false;
    g_externalPriorityVirtualSuspended = false; g_inputReady = true; g_inputHostFaultHoldsStart = false;
    g_inputHostRecoveryIntentReset = false; g_inputHostRecoveryTerminalLogged = false;
    g_inputHostRecoveryPending = false; g_inputHostTupleHealPending = false;
    g_inputHostRecoveryEpisodeAttempts = tracking ? 1 : 0; g_inputHostHealthySinceTick = 0;
    g_inputHostEpoch = 7; g_inputHostBoundPowerGeneration = 1; g_inputHostProcess = true;
    g_inputHostPrepared = true; g_inputHostNeutralized = true; g_inputHostTransportFault = false;
    g_powerLifecycle = PowerLifecycle::Ready; keepInputStandby = false; g_physicalReleaseWindowNeutral = false;
    g_inputHostRecoveryHealth.clear(); g_inputHostRecoveryHealthWasTracking = false;
    g_inputCaptureGamepadSnapshot = {}; producer(activeRoute);
    counts = {}; allocationCount = 0;
}
__declspec(noinline) static void oneCycle() {
    fakeNow += 8;
    producer(activeRoute);
    frame();
}
static unsigned checks = 0;
static void expect(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
[[maybe_unused]] static json countsJson() {
    return {{"boardQueries", counts.board}, {"repairScopeQueries", counts.repair}, {"eligibleQueries", counts.eligible},
        {"armedLoads", counts.armed}, {"configuredPersonaCopies", counts.configuredCopies},
        {"boundPersonaCopies", counts.boundCopies}, {"snapshotLocks", counts.snapshotLocks},
        {"sourceProjections", counts.source}, {"contexts", counts.context}, {"trackingChecks", counts.tracking},
        {"allocations", allocationCount}};
}
static json test() {
    json cases = json::array(); unsigned matrixRows = 0;
    unsigned long long digest = 14695981039346656037ULL;
    auto hash = [&](unsigned long long x) { digest ^= x; digest *= 1099511628211ULL; };
    reset();
    expect(!inputHostRecoveryHealthSource(nullptr, true).live, "null source must be rejected");
    for (const std::string route : {"xinput", "xinput-any-connected", "msi-dinput", "none", "virtual", "rog-hid"})
    for (unsigned bits = 0; bits < 1024; ++bits) {
        g_inputCaptureGamepadSnapshot.inputSource = route;
        g_inputCaptureGamepadSnapshot.connectedMask = bits & 1;
        g_inputCaptureGamepadSnapshot.live = (bits & 2) != 0;
        const bool eligible = (bits & 4) != 0;
        g_msiDinputRouteDinputArmed.store((bits & 8) != 0);
        g_msiDinputRouteConfirmed = (bits & 16) != 0;
        g_machineIdentity.product = (bits & 32) ? "MS-1T91" : "MS-7B89";
        g_msiDinput.enabled = (bits & 64) != 0; g_msiDinput.mainInterfaceMatched = (bits & 128) != 0;
        g_msiDinput.productId = (bits & 256) ? 0x1902 : 0x1901; g_msiDinputSourceValid = (bits & 512) != 0;
        const auto s = inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot, eligible);
        const bool xinput = (route == "xinput" || route == "xinput-any-connected") && (bits & 1);
        const bool msi = route == "msi-dinput" && (bits & 8) && (bits & 16) && (bits & 32) &&
            (bits & 64) && (bits & 128) && (bits & 256) && (bits & 512);
        expect(s.live == (eligible && (bits & 2) && (xinput || msi)), "source admission matrix");
        expect(s.sequence == g_inputCaptureGamepadSnapshot.readAttemptSequence && s.started == 100 && s.completed == 100 &&
            s.power == 1 && s.topology == 1 && s.identity == 1 && s.stable && s.continuity != 0, "source metadata must survive failed admission");
        hash(s.live); hash(s.sequence); hash(s.started); hash(s.completed); hash(s.power);
        hash(s.topology); hash(s.identity); hash(s.stable); hash(s.continuity); ++matrixRows;
    }
    cases.push_back("6144 source classifications and unchanged scalar metadata");
    for (const std::string board : {"MS-1T41", "MS-1T42", "MS-1T52", "MS-1T8K", "MS-1T91", "MS-7B89"}) {
        reset(true, board);
        expect(inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot, true).live == (board != "MS-7B89"), "board allowlist");
    }
    reset(); g_machineIdentity.family = YmccFamily::Other;
    expect(!inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot, true).live, "wrong family");
    reset(); g_machineIdentity.manufacturer = "ASUSTEK";
    expect(!inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot, true).live, "wrong manufacturer");
    cases.push_back("all five known boards; unknown board/family/manufacturer reject");
#if R116_COUNT
    for (unsigned revoke : {1u, 2u}) {
        reset(); g_msiDinputRouteDinputArmed.revokeOnRead = revoke;
        expect(!inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot, true).live, "armed revocation retained");
        expect(counts.armed == revoke, "both armed load positions remain observable");
    }
    cases.push_back("revocation at first and second armed reads rejects");
#endif
    const std::vector<std::function<void()>> cancel = {
        [] { g_realStickTest.enabled = false; }, [] { g_realStickTest.virtualTargetEnabled = false; },
        [] { g_externalPriorityVirtualSuspended = true; }, [] { g_inputHostExitFence = true; },
        [] { g_inputReady = false; }, [] { g_inputHostPowerAdmissionRevoked = true; },
        [] { g_powerLifecycle = PowerLifecycle::Suspended; }, [] { g_inputHostBoundPowerGeneration = 2; },
        [] { g_inputHostProcess = false; }, [] { g_inputHostPrepared = false; }, [] { g_inputHostNeutralized = false; },
        [] { g_inputHostTransportFault = true; }, [] { g_inputHostFaultHoldsStart = true; },
        [] { g_inputHostRecoveryIntentReset = true; }, [] { g_inputHostBoundPersona = "steamdeck"; },
        [] { g_physicalReleaseWindowNeutral = true; }
    };
    for (const auto& f : cancel) {
        reset(); f(); expect(!inputHostRecoveryHealthContextLocked().allowed, "context cancellation remains authoritative");
        reset(); frame(); fakeNow += 8; producer(activeRoute); frame(true, true, false, f);
        expect(!g_inputHostRecoveryHealth.since(), "cancellation during ACK clears health");
    }
    cases.push_back("16 context cancellation gates, including changes during ACK");
    for (const auto& f : std::vector<std::function<void()>>{
        [] { ++g_powerGeneration; }, [] { ++g_xinputTopologyEpoch; }, [] { ++g_xinputSlotIdentityEpoch; },
        [] { ++g_inputHostEpoch; }, [] { ++g_inputHostPresentationWorkGeneration; }}) {
        reset(); frame(); fakeNow += 8; producer(activeRoute); frame(true, true, false, f);
        expect(!g_inputHostRecoveryHealth.since(), "generation change during ACK");
    }
    cases.push_back("power/topology/identity/Host/intent generation changes during ACK");
    reset(false); frame();
    expect(g_inputHostHealthySinceTick == 100 && !g_inputHostRecoveryHealthWasTracking, "R112 lightweight T1");
    expect(counts.configuredCopies == 0 && counts.boundCopies == 0 && counts.snapshotLocks == 0, "R112 no-recovery fast path");
    g_inputHostRecoveryEpisodeAttempts = 1; fakeNow = 108; producer(activeRoute); frame();
    expect(g_inputHostRecoveryHealth.since() == 108, "tracking transition cannot inherit normal-path health");
    cases.push_back("R112 fast path and obligation transition preserved");
    for (int flag = 0; flag < 4; ++flag) {
        reset(false);
        if (flag == 0) g_inputHostRecoveryEpisodeAttempts = 1;
        if (flag == 1) g_inputHostRecoveryTerminalLogged = true;
        if (flag == 2) g_inputHostRecoveryPending = true;
        if (flag == 3) g_inputHostTupleHealPending = true;
        expect(inputHostRecoveryHealthTrackingLocked(), "all four tracking obligations");
    }
    reset(); frame(); const auto through = g_inputHostRecoveryHealth.through();
    fakeNow = 200; frame(); expect(g_inputHostRecoveryHealth.through() == through, "repeated ACK cannot extend coverage");
    fakeNow = 1601; inputHostRecoveryHealthObserveLocked(); expect(!g_inputHostRecoveryHealth.since(), "1500ms source freshness bound");
    cases.push_back("same-read ACK dedupe and canonical source freshness");
    reset(); frame(); fakeNow = 3100; producer(activeRoute); frame();
    expect(!g_inputHostRecoveryHealth.complete(2000), "reader stall must break continuity");
    reset(); frame();
    for (const ULONGLONG tick : {1100ULL, 2100ULL}) { fakeNow = tick; producer(activeRoute); frame(false); }
    fakeNow = 2200; producer(activeRoute); frame();
    expect(g_inputHostRecoveryHealth.complete(2000), "fresh intermediate reads with dedupe preserve coverage");
    cases.push_back("reader stall resets, healthy deduped intermediate reads preserve continuity");
    reset(); frame(); fakeNow = 108; producer(activeRoute); frame(true, true, true);
    expect(!g_inputHostRecoveryHealth.since(), "semantic neutral cannot accrue health");
    reset(); frame(false); expect(!g_inputHostRecoveryHealth.since(), "no ACK cannot start health");
    reset(); frame(); fakeNow = 108; producer(activeRoute); frame(true, false);
    expect(!g_inputHostRecoveryHealth.since(), "ineligible source cannot accrue health");
    reset(); frame(); g_inputCaptureGamepadSnapshot.readContextStable = false;
    inputHostRecoveryHealthObserveLocked(); expect(!g_inputHostRecoveryHealth.since(), "unstable read");
    cases.push_back("neutral/no ACK/ineligible/unstable read do not accrue health");
    json profiles = json::array();
#if R116_COUNT
    for (const std::string route : {"xinput", "xinput-any-connected", "msi-dinput"})
    for (const bool tracking : {false, true}) {
        activeRoute = route; reset(tracking); oneCycle(); counts = {}; allocationCount = 0;
        measureAllocations = true; oneCycle(); measureAllocations = false;
        auto row = countsJson(); row["route"] = route; row["tracking"] = tracking;
        expect(counts.source == (tracking ? 5u : 3u), "actual projection call count");
        expect(counts.context == (tracking ? 3u : 2u), "actual context call count");
        expect(counts.snapshotLocks == (tracking ? 2u : 0u), "actual additional snapshot lock count");
        expect(counts.configuredCopies + counts.boundCopies == (tracking ? 4u : 0u), "actual persona snapshot count");
        if (route == "msi-dinput") {
            expect(counts.armed == counts.source * 2, "candidate preserves two armed reads per projection");
            expect(counts.board == counts.source * (R116_CANDIDATE ? 1u : 2u), "board query count witness");
        }
        profiles.push_back(row);
    }
#endif
    return {{"passed", true}, {"checks", checks}, {"matrixRows", matrixRows}, {"matrixDigest", std::to_string(digest)},
        {"cases", cases}, {"countsPerFrameIncludingCanonicalCommit", profiles}};
}
static json benchmark(const std::string& route, bool tracking, unsigned loops) {
    activeRoute = route; reset(tracking);
    for (unsigned i = 0; i < 10000; ++i) oneCycle();
    ULONG64 before = 0, after = 0;
    if (!QueryThreadCycleTime(GetCurrentThread(), &before)) throw std::runtime_error("QueryThreadCycleTime failed");
    measureAllocations = true;
    for (unsigned i = 0; i < loops; ++i) oneCycle();
    measureAllocations = false;
    if (!QueryThreadCycleTime(GetCurrentThread(), &after)) throw std::runtime_error("QueryThreadCycleTime failed");
    return {{"route", route}, {"tracking", tracking}, {"iterations", loops}, {"threadCycles", after - before},
        {"cyclesPerFrame", static_cast<double>(after - before) / loops}, {"allocations", allocationCount},
        {"healthSince", g_inputHostRecoveryHealth.since()}, {"healthThrough", g_inputHostRecoveryHealth.through()}};
}
int main(int argc, char** argv) {
    try {
        if (argc == 5 && std::string(argv[1]) == "--bench")
            std::cout << benchmark(argv[2], std::string(argv[3]) == "1", static_cast<unsigned>(std::stoul(argv[4]))).dump() << '\n';
        else std::cout << test().dump(2) << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
