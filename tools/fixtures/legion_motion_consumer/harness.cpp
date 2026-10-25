#include "controller_motion_session.h"
#include "controller_motion_bank_sessions.h"
#include "controller_motion_calibration.h"
#include "legion_motion_source.h"
#include "legion_input_protocol.h"
#include "legion_motion_message_clock.h"
#include "input_frame_submission.h"
#include "windows_motion_submission.h"
#include "json.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <vector>

using json = nlohmann::json;
using ULONGLONG = std::uint64_t;
using USHORT = unsigned short;
using USAGE = unsigned short;
using BYTE = unsigned char;
using DWORD = std::uint32_t;
using UINT = unsigned;
using UINT_PTR = std::uintptr_t;
using HWND = void*;
using BOOL = int;
static constexpr BOOL FALSE = 0;
static constexpr DWORD ERROR_SUCCESS = 0;
static constexpr DWORD ERROR_INVALID_WINDOW_HANDLE = 1400;
static constexpr DWORD RIDEV_INPUTSINK = 0x00000100;
static constexpr DWORD RIDEV_DEVNOTIFY = 0x00002000;
struct RAWINPUTDEVICE { USHORT usUsagePage = 0, usUsage = 0; DWORD dwFlags = 0; HWND hwndTarget = nullptr; };
using std::uint64_t;
using std::int64_t;
namespace cm = ymcc::controller_motion;
namespace fsb = ymcc::input::frameSubmission;
using MotionSample = ymcc::legion_motion::Sample;

#include "production_types.generated.h"

static int checks = 0, failures = 0;
static const char* activeCase = "setup";
static void check(bool ok, const char* expression, int line) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s line=%d expression=%s\n", activeCase, line, expression); }
}
#define CHECK(x) check(static_cast<bool>(x), #x, __LINE__)
static bool near(double a, double b) { return std::abs(a-b) < 0.00001; }
static std::vector<json> effects;
static std::vector<json> trace;
static std::filesystem::path calibrationPath;
static unsigned calibrationPathCalls = 0;
static std::uint64_t clockMs = 100;
static std::uint32_t queuedMessageTime32 = 100;
static unsigned messageTimeCalls = 0, uptimeCalls = 0;
static std::vector<std::string> queueClockCalls;
static ULONGLONG GetTickCount64() noexcept { ++uptimeCalls; queueClockCalls.emplace_back("now"); return clockMs; }
static std::int32_t GetMessageTime() noexcept { ++messageTimeCalls; queueClockCalls.emplace_back("message"); std::int32_t value; std::memcpy(&value,&queuedMessageTime32,sizeof(value)); return value; }
static std::string inputCaptureCalibrationJsonPath() { ++calibrationPathCalls; return calibrationPath.string(); }
static void inputCaptureQueueSample(const json& row) { trace.push_back(row); }

enum class PowerLifecycle { Ready, Suspending, Suspended, Resuming };
enum class YmccFamily { Unknown, LenovoLegionGo, RogAlly, MsiClaw, Gpd };
struct Machine { YmccFamily family = YmccFamily::LenovoLegionGo;
    std::string deviceClass = "HandheldCompanion.Devices.LegionGoTablet"; };
static Machine g_machineIdentity;
static std::atomic<std::uint64_t> g_powerGeneration{7}, g_hidResolveCacheGeneration{11}, g_legionMotionCaptureEpoch{1};
static std::atomic<unsigned> g_legionMotionSensorIndex{0};
static std::uint64_t g_legionMotionSelectionRevision = 0;
static json familyTopology;
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool> g_inputCaptureStop{false}, g_gmSessionResetRequested{false};
static bool g_inputCaptureReady = true;
static std::atomic<bool> g_legionRawInputSubscribed{true};
static bool g_rawInputRegistered = true;
static std::atomic<bool> g_keyboardModifierRawSubscribed{true};
static HWND g_hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x33330000));
static bool vendorRegistrationResult = true, baseRegistrationResult = true;
static DWORD rawInputLastError = 0;
static unsigned getLastErrorCalls = 0, existingTimerCalls = 0;
struct RegistrationEffect {
    std::vector<RAWINPUTDEVICE> devices;
    unsigned recordSize = 0;
    std::uint64_t captureEpoch = 0;
    bool subscribedAtCall = false;
};
static std::vector<RegistrationEffect> registrations;
static std::vector<json> lifecycle;
static BOOL RegisterRawInputDevices(const RAWINPUTDEVICE* devices, UINT count, UINT size) {
    CHECK(devices != nullptr && size == sizeof(RAWINPUTDEVICE));
    const bool vendor = count == 1 && devices && devices[0].usUsagePage == 0xFFA0;
    CHECK(vendor || count == 5);
    registrations.push_back({std::vector<RAWINPUTDEVICE>(devices,devices+count),size,
        g_legionMotionCaptureEpoch.load(),g_legionRawInputSubscribed.load()});
    const bool ok = vendor ? vendorRegistrationResult : baseRegistrationResult;
    rawInputLastError = ok ? ERROR_SUCCESS : vendor ? DWORD{87} : DWORD{55};
    return ok ? 1 : 0;
}
static DWORD GetLastError() noexcept { ++getLastErrorCalls; return rawInputLastError; }
static UINT_PTR SetTimer(HWND target, UINT_PTR id, UINT delay, void* callback) {
    CHECK(target == g_hwnd && delay == 50 && callback == nullptr);
    ++existingTimerCalls; return id;
}
static bool throwCalibrationLog=false;
static void appendNativeLifecycleLog(const char* event, const json& value) {
    if(throwCalibrationLog)throw std::runtime_error("controlled log sink failure");
    lifecycle.push_back({{"event",event},{"value",value}});
}
static std::mutex g_hidHidePhysicalIdentityMx, g_legionImuMx;
static HidHidePhysicalIdentity g_hidHidePhysicalIdentity;
static ymcc::legion_motion::Lease g_legionMotion;
static std::uint64_t currentPowerGeneration() noexcept { return g_powerGeneration.load(std::memory_order_acquire); }
// Fixture setting assignments publish to the same atomic state read by production.
static std::atomic<bool> g_controllerAutoCalibrationRequested{true};
struct MotionSettings { std::string motionInput = "local-space"; std::atomic<bool>& autoCalibrate = g_controllerAutoCalibrationRequested; };
static MotionSettings g_realStickTest;

struct NativeHandle { unsigned id = 0; bool alive = true; int mode = -1; cm::Calibration offset{};
    cm::RawSixAxes last{}; float dt = 0; };
static std::map<void*, NativeHandle> handles;
static unsigned nextHandle = 1, createCalls = 0, deleteCalls = 0, processCalls = 0, playerCalls = 0, worldCalls = 0;
static bool failCreate = false, libraryLoaded = true;
static cm::MotionVector3 gravityReadback{0.12f,0.8f,0.4f};
static void* const windowsSentinel = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x77770000));
static void* g_gm = windowsSentinel;
static NativeHandle* handle(void* h) {
    CHECK(libraryLoaded); CHECK(h != windowsSentinel); CHECK(h != g_gm);
    const auto at = handles.find(h);
    CHECK(at != handles.end()); if (at == handles.end()) return nullptr;
    CHECK(at->second.alive); return at->second.alive ? &at->second : nullptr;
}
static void* createController() {
    CHECK(libraryLoaded); ++createCalls;
    if (failCreate) { effects.push_back({{"case",activeCase},{"event","create-failed"}}); return nullptr; }
    const unsigned id = nextHandle++;
    void* h = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000 + id * 16));
    handles.emplace(h, NativeHandle{id});
    effects.push_back({{"case",activeCase},{"event","create"},{"id",id}}); return h;
}
static void deleteController(void* h) {
    auto* p = handle(h); if (!p) return;
    p->alive = false; ++deleteCalls;
    effects.push_back({{"case",activeCase},{"event","delete"},{"id",p->id}});
}
static void setMode(void* h, int mode) {
    auto* p = handle(h); if (!p) return; p->mode = mode;
    effects.push_back({{"case",activeCase},{"event","mode"},{"id",p->id},{"mode",mode}});
}
static void setOffset(void* h, float x, float y, float z, int weight) {
    auto* p = handle(h); if (!p) return; CHECK(p->mode == 0 || p->mode == 3); p->offset = {x,y,z,weight};
    effects.push_back({{"case",activeCase},{"event","offset"},{"id",p->id},
        {"x",x},{"y",y},{"z",z},{"weight",weight}});
}
static void processController(void* h, float gx, float gy, float gz, float ax, float ay, float az, float dt) {
    auto* p = handle(h); if (!p) return; CHECK(p->mode == 0 || p->mode == 3);
    ++processCalls; p->last = {gx,gy,gz,ax,ay,az}; p->dt = dt;
    effects.push_back({{"case",activeCase},{"event","process"},{"id",p->id},
        {"axes",{gx,gy,gz,ax,ay,az}},{"dt",dt}});
}
static void getCal(void* h, float* x, float* y, float* z) {
    auto* p = handle(h); if (!p) return;
    *x = p->last.gx - p->offset.x; *y = p->last.gy - p->offset.y; *z = p->last.gz - p->offset.z;
}
static void getGravity(void* h, float* x, float* y, float* z) {
    if (!handle(h)) return; *x = gravityReadback.x; *y = gravityReadback.y; *z = gravityReadback.z;
}
static void getPlayer(void* h, float* x, float* y, float limit) {
    if (!handle(h)) return; ++playerCalls; CHECK(near(limit, 1.41)); *x = 17; *y = -23;
}
static void getWorld(void* h, float* x, float* y, float limit) {
    if (!handle(h)) return; ++worldCalls; CHECK(near(limit, 0.125)); *x = -31; *y = 47;
}
static cm::BankSessions g_legionMotionSession;
static std::atomic<uint64_t> g_legionMotionProcessorLifetime{1};
static std::optional<cm::Key> g_legionMotionSessionKey;
static std::optional<cm::Calibration> g_legionMotionCalibration;
static auto g_controllerGmCreate = &createController;
static auto g_controllerGmDelete = &deleteController;
static auto g_gmSetMode = &setMode;
static auto g_gmSetOffset = &setOffset;
static auto g_gmProcess = &processController;
static auto g_gmGetCal = &getCal;
static auto g_gmGetGravity = &getGravity;
static auto g_gmPlayer = &getPlayer;
static auto g_gmWorld = &getWorld;

// Complete real frame decide() remains in input_frame_submission.h. Only its
// external current globals are replaced; no source/admission verdict is stubbed.
static fsb::Current frameCurrent;
static unsigned frameNotes = 0;
static thread_local InputFrameSubmission* t_inputFrameSubmission = nullptr;
static thread_local const MotionSample* t_legionMotionSubmission = nullptr;
static fsb::Current inputHostFrameSubmissionCurrent(bool admitted, bool) noexcept {
    auto current = frameCurrent; current.sourceEligible = current.sourceEligible && admitted; return current;
}
static void inputHostFrameSubmissionNote(fsb::Verdict, const char*, bool) { ++frameNotes; }

#include "../legion_motion_calibration_consumer/platform_boundary.h"

#include "production_scopes.generated.h"
#include "production_functions.generated.h"

static void* sessionHandleFor(const cm::Key& key) { return g_legionMotionSession ? g_legionMotionSession->handleFor(key) : nullptr; }
static std::uint64_t epoch() { return g_legionMotionCaptureEpoch.load(); }
static std::string identity() { return legionMotionStableIdentity(g_hidHidePhysicalIdentity); }
static HidHidePhysicalIdentity physical(const std::string& base) {
    HidHidePhysicalIdentity p; p.resolved = true; p.baseContainerDeviceInstanceId = base;
    p.vendorId = 0x17EF; p.productId = 0x6182; p.usagePage = 0xFFA0; p.usage = 1;
    p.physicalIdentity = "interface-" + base; return p;
}
// Setter assignment is fixture orchestration around the actual selected-change
// function. Real setters are saved as source-only anchors, never executed here.
static void selectPhysical(const HidHidePhysicalIdentity& next) {
    std::lock_guard<std::mutex> lock(g_hidHidePhysicalIdentityMx);
    legionMotionSelectedIdentityChanging(g_hidHidePhysicalIdentity, next);
    g_hidHidePhysicalIdentity = next;
}
static MotionSample sample(std::uint64_t tick = 100) {
    return {{11,-22,33},{-4,5,-6},tick,
        {currentPowerGeneration(),g_hidResolveCacheGeneration.load()},legionMotionSourceForSelectedLocked()};
}
static void selectSensor(unsigned index) {
    const bool changed = g_legionMotionSensorIndex.load() != index;
    CHECK(legionMotionSelectSensorIndex(index,g_legionMotionSelectionRevision+1) == changed);
}
static bool publish(const MotionSample& in) { return legionMotionPublish(in, in.source.captureEpoch); }
static cm::RawSixAxes axes(const MotionSample& in) {
    return {static_cast<float>(in.gyro[0]),static_cast<float>(in.gyro[1]),static_cast<float>(in.gyro[2]),
        static_cast<float>(in.accel[0]),static_cast<float>(in.accel[1]),static_cast<float>(in.accel[2])};
}
static std::optional<cm::MotionResult> consume(const MotionSample& in, double seconds = 1.0) {
    CHECK(publish(in)); const auto snap = legionMotionSnapshot(clockMs); CHECK(snap.has_value());
    if (!snap) return std::nullopt;
    return inputCaptureLegionMotion(snap, axes(*snap), seconds);
}
// Explicit fixture scheduling: reject an uncertain sample at an actual cutover,
// then advance the controlled clock. The sample factory never adjusts time.
static void rejectSameTickThenAdvance() {
    const auto cutover=g_legionMotionEpochStartedAt.load();
    if (clockMs==cutover) {
        const auto uncertain=sample(clockMs);
        CHECK(!ymcc::legion_motion_clock::belongsToEpoch(uncertain.capturedAt,cutover));
        CHECK(!publish(uncertain)); CHECK(!legionMotionSubmissionCurrent(uncertain,clockMs));
        ++clockMs;
    }
    CHECK(clockMs>cutover);
}
static void writeCalibration(const json& value) {
    std::ofstream file(calibrationPath, std::ios::trunc); file << value.dump(); CHECK(file.good());
}
static json record(float x, float y, float z, int weight) {
    return {{"xOffset",x},{"yOffset",y},{"zOffset",z},{"weight",weight}};
}
static NativeHandle* currentHandle() {
    CHECK(g_legionMotionSessionKey.has_value()); if (!g_legionMotionSessionKey) return nullptr;
    void* h = sessionHandleFor(*g_legionMotionSessionKey); CHECK(h != nullptr);
    const auto at = handles.find(h); return at == handles.end() ? nullptr : &at->second;
}
static unsigned liveHandles() {
    unsigned n = 0; for (const auto& [key,value] : handles) { (void)key; if (value.alive) ++n; } return n;
}
static void reset(unsigned n) {
    libraryLoaded = true; inputCaptureLegionMotionSessionRevoke(); g_legionMotion.revoke();
    CHECK(liveHandles() == 0);
    handles.clear(); createCalls = deleteCalls = processCalls = playerCalls = worldCalls = 0;
    calibrationPathCalls = frameNotes = 0; failCreate = false; clockMs = 100;
    throwCalibrationLog=false;calibrationBoundaryReset();g_controllerCalibrationRequest.reset();g_controllerCalibrationTrustedKey.reset();g_controllerCalibrationHasRequest.store(false);g_controllerCalibrationActiveRequest.reset();g_controllerCalibrationTransaction.reset();
    queuedMessageTime32=100;messageTimeCalls=uptimeCalls=0;queueClockCalls.clear();g_legionMotionEpochStartedAt.store(0); gravityReadback = {0.12f,0.8f,0.4f};
    g_machineIdentity = {}; g_powerGeneration.store(1000 + n); g_hidResolveCacheGeneration.store(2000 + n);
    g_legionMotionSensorIndex.store(0); g_legionMotionSelectionRevision = 1000 * n;
    g_legionMotionCaptureEpoch.store(3000 + 100 * n); g_powerLifecycle.store(PowerLifecycle::Ready);
    g_inputCaptureStop.store(false); g_inputCaptureReady = true;
    g_legionRawInputSubscribed.store(true); g_rawInputRegistered = true; g_keyboardModifierRawSubscribed.store(true);
    vendorRegistrationResult = baseRegistrationResult = true; rawInputLastError = 0;
    getLastErrorCalls = existingTimerCalls = 0; registrations.clear(); lifecycle.clear(); g_gmSessionResetRequested.store(false);
    g_hidHidePhysicalIdentity = physical("USB\\ROOT-A"); g_realStickTest.motionInput = "local-space"; g_realStickTest.autoCalibrate = true;
    g_gmPlayer = &getPlayer; g_gmWorld = &getWorld; g_gm = windowsSentinel;
    t_inputFrameSubmission = nullptr; t_legionMotionSubmission = nullptr;
    frameCurrent = {41,currentPowerGeneration(),43,0,47,53,true,false,true,0};
    calibrationPath = calibrationPath.parent_path() / (std::string(activeCase) + ".json");
    writeCalibration(json::object());
}

static void sourceIdentityAndRealRoute() {
    CHECK(identity() == "controller/lenovo/6127/24962/usb\\root-a");
    const auto before = epoch(); auto p = g_hidHidePhysicalIdentity;
    p.physicalIdentity = "other-interface-same-container"; selectPhysical(p); CHECK(epoch() == before);
    p.baseContainerDeviceInstanceId = "usb\\root-a"; selectPhysical(p); CHECK(epoch() == before);
    p.baseContainerDeviceInstanceId = "USB\\ROOT-B"; selectPhysical(p); CHECK(epoch() == before + 1);
    CHECK(inputCaptureLegionControllerMotionRoute());
    g_machineIdentity.deviceClass = "HandheldCompanion.Devices.LegionGoTablet2";
    CHECK(inputCaptureLegionControllerMotionRoute());
    for (const auto* name : {"HandheldCompanion.Devices.LegionGoSZ1-suffix", "HandheldCompanion.Devices.SteamDeck"}) {
        g_machineIdentity.deviceClass = name; CHECK(!inputCaptureLegionControllerMotionRoute());
        CHECK(!publish(sample())); CHECK(!legionMotionSnapshot(clockMs));
        CHECK(!inputCaptureLegionMotion(sample(), axes(sample()),1));
    }
    g_machineIdentity.deviceClass = "HandheldCompanion.Devices.LegionGoTablet";
    for (int mode = 0; mode < 4; ++mode) {
        auto bad = physical("USB\\BAD");
        if (mode == 0) bad.resolved = false;
        if (mode == 1) bad.baseContainerDeviceInstanceId.clear();
        if (mode == 2) bad.vendorId = 0x1234;
        if (mode == 3) bad.productId = 0x9999;
        CHECK(legionMotionStableIdentity(bad).empty()); selectPhysical(bad);
        CHECK(!publish(sample())); CHECK(!legionMotionSnapshot(clockMs));
    }
    CHECK(createCalls == 0 && processCalls == 0 && calibrationPathCalls == 0);
}
static void actualSampleAndIndependentCalibration() {
    g_realStickTest.autoCalibrate = false; // This scenario explicitly verifies Manual offset restoration.
    json root = {{"gyroDeviceId", identity()}, {"xOffset",999}, {"yOffset",998}, {"zOffset",997}, {"weight",99}};
    root["controllerMotionCalibration"][identity()]["0"] = record(1,2,3,4);
    root["controllerMotionCalibration"][identity()]["1"] = record(101,102,103,104);
    writeCalibration(root);
    const auto in = sample(); const auto output = consume(in); CHECK(output.has_value());
    CHECK(createCalls == 1 && liveHandles() == 1 && processCalls == 1 && calibrationPathCalls == 1);
    auto* p = currentHandle(); CHECK(p && p->mode == 0); if (!p || !output) return;
    CHECK(p->offset.x == 1 && p->offset.y == 2 && p->offset.z == 3 && p->offset.weight == 4);
    CHECK(p->last.gx == 11 && p->last.gy == -22 && p->last.gz == 33);
    CHECK(p->last.ax == -4 && p->last.ay == 5 && p->last.az == -6);
    CHECK(output->calibrated.x == 10 && output->calibrated.y == -24 && output->calibrated.z == 30);
    CHECK(near(output->gravity.x,0.12) && near(output->gravity.y,0.8) && near(output->gravity.z,0.4));
    CHECK(p->dt == 0 && output->deltaSeconds == 0); CHECK(!output->player && !output->world);
    CHECK(!trace.empty() && trace.back().at("calibrationOwner") == "controller-physical-identity-and-sensor-index");
    CHECK(trace.back().at("physicalAcceptanceVerified") == false);
}
static void windowsRecordCannotHitAndStructuredSensor() {
    writeCalibration({{"gyroDeviceId",identity()},{"xOffset",900},{"yOffset",901},{"zOffset",902},{"weight",9},
        {identity(),record(77,88,99,2)}});
    CHECK(consume(sample()).has_value()); auto* h = currentHandle(); CHECK(h && h->offset.weight == 0 && h->offset.x == 0);
    json root; root["controllerMotionCalibration"][identity()]["1"] = record(10,20,30,40); writeCalibration(root);
    const auto index1 = inputCaptureControllerCalibration({identity(),1});
    const auto index0 = inputCaptureControllerCalibration({identity(),0});
    const auto foreign = inputCaptureControllerCalibration({identity()+"/suffix",1});
    CHECK(index1 && index1->x == 10 && index1->y == 20 && index1->z == 30 && index1->weight == 40);
    CHECK(index0 && index0->weight == 0 && index0->x == 0);
    CHECK(foreign && foreign->weight == 0 && foreign->x == 0);
    root["controllerMotionCalibration"][identity()]["1"]["weight"] = -1; writeCalibration(root);
    CHECK(!inputCaptureControllerCalibration({identity(),1}));
    root["controllerMotionCalibration"][identity()]["0"] = "invalid"; writeCalibration(root);
    CHECK(!inputCaptureControllerCalibration({identity(),0}));
    std::ofstream file(calibrationPath,std::ios::trunc); file << "{broken"; file.close();
    CHECK(!inputCaptureControllerCalibration({identity(),0}));
}
static void repeatTickHasNoExtraCalibrationOrCreate() {
    json root; root["controllerMotionCalibration"][identity()]["0"] = record(1,2,3,4); writeCalibration(root);
    const auto in = sample(); CHECK(consume(in,1).has_value());
    void* old = sessionHandleFor(*g_legionMotionSessionKey);
    root["controllerMotionCalibration"][identity()]["0"] = record(99,98,97,96); writeCalibration(root);
    for (int i = 0; i < 8; ++i) CHECK(inputCaptureLegionMotion(legionMotionSnapshot(clockMs),axes(in),1).has_value());
    CHECK(createCalls == 1 && deleteCalls == 0 && processCalls == 9 && calibrationPathCalls == 1);
    CHECK(sessionHandleFor(*g_legionMotionSessionKey) == old);
    auto* h = currentHandle(); CHECK(h && h->offset.x == 1 && h->dt == 0);
    clockMs = 110; CHECK(consume(sample(110),1.25).has_value());
    CHECK(h && near(h->dt,0.25) && createCalls == 1 && calibrationPathCalls == 1);
}
static void sourceStampRejectionAndSwitchBack() {
    const auto old = sample(); CHECK(consume(old).has_value()); const auto oldEpoch = epoch();
    for (int field = 0; field < 4; ++field) {
        auto forged = old;
        if (field == 0) forged.source.physicalIdentity += "/other";
        if (field == 1) ++forged.source.captureEpoch;
        if (field == 2) forged.source.sensorIndex = 1;
        if (field == 3) forged.source.physicalIdentity.clear();
        CHECK(!publish(forged));
    }
    CHECK(processCalls == 1);
    selectPhysical(physical("USB\\ROOT-B")); CHECK(epoch() == oldEpoch + 1);
    CHECK(!publish(old)); CHECK(!legionMotionSnapshot(clockMs));
    json root; root["controllerMotionCalibration"][identity()]["0"] = record(8,9,10,11); writeCalibration(root);
    rejectSameTickThenAdvance(); auto b = sample(clockMs); CHECK(consume(b).has_value());
    auto* h = currentHandle(); CHECK(h && h->offset.x == 8 && createCalls == 2 && deleteCalls == 1);
    selectPhysical(physical("USB\\ROOT-A")); CHECK(epoch() == oldEpoch + 2);
    CHECK(!publish(old)); CHECK(!inputCaptureLegionMotion(old,axes(old),1));
    CHECK((!g_legionMotionSession || !g_legionMotionSession->active()));
    rejectSameTickThenAdvance(); const auto fresh = sample(clockMs); CHECK(consume(fresh).has_value());
    CHECK(createCalls == 3 && deleteCalls == 2 && liveHandles() == 1);
}
static void powerReadyAndRawRetirement() {
    auto old = sample(); CHECK(consume(old).has_value());
    g_inputCaptureReady = false;
    CHECK(!publish(old)); CHECK(!legionMotionSnapshot(clockMs));
    CHECK(!inputCaptureLegionMotion(legionMotionSnapshot(clockMs),axes(old),1));
    CHECK(!g_legionMotionSession);
    g_inputCaptureReady = true;
    CHECK(!publish(old)); rejectSameTickThenAdvance(); CHECK(consume(sample(clockMs)).has_value()); old = sample(clockMs);
    g_powerLifecycle.store(PowerLifecycle::Suspended);
    CHECK(!publish(old)); CHECK(!legionMotionSnapshot(clockMs));
    CHECK(!inputCaptureLegionMotion(old,axes(old),1)); CHECK((!g_legionMotionSession || !g_legionMotionSession->active()));
    ++g_powerGeneration; g_powerLifecycle.store(PowerLifecycle::Ready);
    CHECK(!publish(old)); CHECK(!inputCaptureLegionMotion(old,axes(old),1));
    rejectSameTickThenAdvance(); CHECK(consume(sample(clockMs)).has_value()); const auto current = sample(clockMs);
    ++g_hidResolveCacheGeneration;
    CHECK(!legionMotionSnapshot(clockMs)); CHECK(!publish(current));
    CHECK(!inputCaptureLegionMotion(current,axes(current),1)); CHECK((!g_legionMotionSession || !g_legionMotionSession->active()));
    rejectSameTickThenAdvance(); CHECK(consume(sample(clockMs)).has_value());
}
static void futureExpiryAbsentAndRecovery() {
    const auto old = sample(); CHECK(consume(old).has_value());
    clockMs = 99; CHECK(!legionMotionSnapshot(clockMs)); CHECK(!inputCaptureLegionMotion(old,axes(old),1));
    clockMs = 100; CHECK(consume(sample()).has_value()); const auto timed = sample();
    clockMs = 1600; CHECK(legionMotionSnapshot(clockMs).has_value());
    clockMs = 1601; CHECK(!legionMotionSnapshot(clockMs));
    CHECK(!inputCaptureLegionMotion(timed,axes(timed),2.5));
    const auto afterExpiry = epoch();
    CHECK(!inputCaptureLegionMotion(std::nullopt,{},2.5)); CHECK(epoch() == afterExpiry);
    CHECK(!publish(timed));
    rejectSameTickThenAdvance(); CHECK(consume(sample(clockMs),2.6).has_value());
    const auto beforeAbsent = epoch(); CHECK(!inputCaptureLegionMotion(std::nullopt,{},2.7));
    CHECK(epoch() == beforeAbsent + 1 && (!g_legionMotionSession || !g_legionMotionSession->active()));
    rejectSameTickThenAdvance(); CHECK(consume(sample(clockMs),2.8).has_value());
}
static void resetAndCreateFailureDoNotReviveHandle() {
    const auto old = sample(); CHECK(consume(old).has_value());
    const auto first = *g_legionMotionSessionKey; void* oldHandle = sessionHandleFor(first);
    g_gmSessionResetRequested.store(true);
    CHECK(!inputCaptureLegionMotion(old,axes(old),1.1)); CHECK(!g_gmSessionResetRequested.load());
    CHECK(sessionHandleFor(first) == nullptr && !handles.at(oldHandle).alive);
    CHECK(!publish(old)); rejectSameTickThenAdvance(); CHECK(consume(sample(clockMs),1.2).has_value());
    selectPhysical(physical("USB\\CREATE-FAIL")); failCreate = true;
    rejectSameTickThenAdvance(); const auto failed = sample(clockMs); CHECK(!consume(failed,1.3));
    CHECK((!g_legionMotionSession || !g_legionMotionSession->active()) && liveHandles() == 0);
    const auto creates = createCalls; failCreate = false;
    CHECK(!inputCaptureLegionMotion(failed,axes(failed),1.4)); CHECK(createCalls == creates);
    CHECK(sessionHandleFor(first) == nullptr);
    legionMotionRevoke(); rejectSameTickThenAdvance(); CHECK(consume(sample(clockMs),1.5).has_value()); CHECK(liveHandles() == 1);
}
static void nonfiniteAndStationaryAxes() {
    CHECK(consume(sample()).has_value()); const auto calls = processCalls;
    for (unsigned axis = 0; axis < 6; ++axis) {
        auto bad = sample(); (axis < 3 ? bad.gyro[axis] : bad.accel[axis-3]) = std::numeric_limits<double>::infinity();
        CHECK(!publish(bad));
        auto raw = axes(sample()); float* values[] = {&raw.gx,&raw.gy,&raw.gz,&raw.ax,&raw.ay,&raw.az};
        *values[axis] = std::numeric_limits<float>::quiet_NaN();
        CHECK(!inputCaptureLegionMotion(legionMotionSnapshot(clockMs),raw,1.25));
        CHECK(processCalls == calls);
    }
    auto zero = sample(); zero.gyro.fill(0); zero.accel.fill(0);
    // Default Q and gravity along +Z give an exactly zero Madgwick gradient.
    // This exercises the real fixed session path, with zero gyro/accel input.
    gravityReadback = {0,0,1};
    const auto stationary = consume(zero,1.25);
    CHECK(stationary && stationary->orientationFinite && std::isfinite(stationary->orientation.w) &&
        std::isfinite(stationary->orientation.x) && std::isfinite(stationary->orientation.y) && std::isfinite(stationary->orientation.z));
    auto* p = currentHandle(); CHECK(p && p->last.gx == 0 && p->last.gy == 0 && p->last.gz == 0 &&
        p->last.ax == 0 && p->last.ay == 0 && p->last.az == 0);
}
static void playerWorldRequests() {
    const auto in = sample();
    g_realStickTest.motionInput = "player-space"; const auto p = consume(in,1);
    CHECK(p && p->player && !p->world && p->player->x == 17 && p->player->y == -23);
    g_realStickTest.motionInput = "world-space"; const auto w = consume(in,1.1);
    CHECK(w && !w->player && w->world && w->world->x == -31 && w->world->y == 47);
    g_realStickTest.motionInput = "local-space"; const auto local = consume(in,1.2);
    CHECK(local && !local->player && !local->world);
    CHECK(playerCalls == 1 && worldCalls == 1 && createCalls == 1 && calibrationPathCalls == 1);
}
static InputFrameSubmission frame(const MotionSample* motion) {
    InputFrameSubmission f; f.ticket = {frameCurrent.host,frameCurrent.power,frameCurrent.intent,false};
    f.sample.window = {1,clockMs,clockMs,frameCurrent.power,frameCurrent.topology,true};
    f.sample.identity = frameCurrent.identity; f.sample.live = f.sample.routeKnown = true;
    f.physicalSourceAdmitted = true; f.controllerMotion = motion; return f;
}
static void guardsRejectFrame(InputFrameSubmission& f) {
    InputFrameSubmissionScope scope(&f); json response;
    CHECK(!inputHostFrameSubmissionMayIssue(&f)); CHECK(f.rejected);
    CHECK(!inputHostFrameSubmissionRequestCurrent("SUBMIT_FRAME",&response,"fixture-request"));
    CHECK(response.is_object() && response.value("deviceWriteAttempted", true) == false);
    CHECK(f.reason == fsb::Verdict::SourceRetired);
}
static void guardsReject(const MotionSample& in) {
    auto f = frame(&in); guardsRejectFrame(f);
}
static void issueRequestGuardRetirement() {
    const auto in = sample(); CHECK(consume(in).has_value());
    { auto f = frame(&in); InputFrameSubmissionScope scope(&f); json response;
      CHECK(inputHostFrameSubmissionMayIssue(&f));
      CHECK(inputHostFrameSubmissionRequestCurrent("SUBMIT_FRAME",&response,"fixture")); }
    for (unsigned cause = 0; cause < 7; ++cause) {
        const auto power = currentPowerGeneration(), raw = g_hidResolveCacheGeneration.load(), cap = epoch();
        // Form the complete ticket/sample/controller pointer while all contexts
        // are still valid; then retire exactly one controller dimension.
        clockMs = cause == 3 ? 1600 : 100;
        auto pending = frame(&in);
        if (cause == 4) pending.sample.window.readStartedTick = pending.sample.window.readCompletedTick = 99;
        CHECK(inputHostFrameSubmissionMayIssue(&pending));
        if (cause == 0) ++g_powerGeneration;
        if (cause == 1) ++g_hidResolveCacheGeneration;
        if (cause == 2) ++g_legionMotionCaptureEpoch;
        if (cause == 3) clockMs = 1601;
        if (cause == 4) clockMs = 99;
        if (cause == 5) g_inputCaptureStop.store(true);
        if (cause == 6) g_powerLifecycle.store(PowerLifecycle::Suspended);
        guardsRejectFrame(pending);
        g_powerGeneration.store(power); g_hidResolveCacheGeneration.store(raw); g_legionMotionCaptureEpoch.store(cap);
        g_inputCaptureStop.store(false); g_powerLifecycle.store(PowerLifecycle::Ready);
    }
    clockMs = 100; auto altered = in; altered.source.sensorIndex = 1; guardsReject(altered);
    altered = in; altered.capturedAt = 0; guardsReject(altered);
    auto f = frame(&in); CHECK(!inputHostFrameSubmissionMayIssue(nullptr));
    { InputFrameSubmissionScope scope(&f); CHECK(inputHostFrameSubmissionRequestCurrent("PING",nullptr,"fixture")); }
    CHECK(inputHostFrameSubmissionRequestCurrent("SUBMIT_FRAME",nullptr,"no-frame-scope"));
}
static void fullNeutralReleaseAndNonLegionNullPointer() {
    auto in = sample(); CHECK(consume(in).has_value());
    ++g_hidResolveCacheGeneration; ++g_legionMotionCaptureEpoch;
    g_powerLifecycle.store(PowerLifecycle::Suspended); clockMs = 10000;
    { auto f = frame(&in); f.ticket.neutral = true; f.sample.live = false; f.physicalSourceAdmitted = false;
      InputFrameSubmissionScope scope(&f); json response;
      CHECK(inputHostFrameSubmissionMayIssue(&f));
      CHECK(inputHostFrameSubmissionRequestCurrent("SUBMIT_FRAME",&response,"neutral"));
      CHECK(!f.rejected); }
    // Null controller pointer leaves existing canonical admission unchanged.
    clockMs = 100; g_machineIdentity.family = YmccFamily::RogAlly;
    { auto f = frame(nullptr); InputFrameSubmissionScope scope(&f); json response;
      CHECK(inputHostFrameSubmissionMayIssue(&f));
      CHECK(inputHostFrameSubmissionRequestCurrent("SUBMIT_FRAME",&response,"non-legion"));
      frameCurrent.host++; CHECK(!inputHostFrameSubmissionMayIssue(&f)); }
    { auto f = frame(&in); f.ticket.neutral = true; frameCurrent.ready = false;
      CHECK(!inputHostFrameSubmissionMayIssue(&f)); }
}
static void actualScopesAndExitRevokeBeforeUnload() {
    const auto in = sample(); CHECK(consume(in).has_value());
    CHECK(t_legionMotionSubmission == nullptr);
    { LegionMotionSubmissionScope first(&in); CHECK(t_legionMotionSubmission == &in);
      { LegionMotionSubmissionScope second(nullptr); CHECK(t_legionMotionSubmission == nullptr); }
      CHECK(t_legionMotionSubmission == &in); }
    CHECK(t_legionMotionSubmission == nullptr);
    CHECK(liveHandles() == 1); const auto deleted = deleteCalls;
    inputCaptureLegionMotionSessionRevoke();
    CHECK(deleteCalls == deleted + 1 && liveHandles() == 0 && !g_legionMotionSessionKey && !g_legionMotionCalibration);
    libraryLoaded = false; effects.push_back({{"case",activeCase},{"event","simulated-unload"}});
    inputCaptureLegionMotionSessionRevoke(); CHECK(deleteCalls == deleted + 1);
    libraryLoaded = true;
}


// Independent model/device table is loaded from family-topology.json. These
// tests execute real admission/identity helpers; no raw report decoder is copied.
static json sensorSettings(const json& value) {
    return {{"gyroMotion",{{"controllerSensorIndex",value}}}};
}
static void familyTopologyAdmission() {
    const auto originalPower = currentPowerGeneration();
    const auto originalRaw = g_hidResolveCacheGeneration.load();
    for (const auto& model : familyTopology.at("models")) {
        g_machineIdentity.family = YmccFamily::LenovoLegionGo;
        g_machineIdentity.deviceClass = model.at("name").get<std::string>();
        CHECK(inputCaptureLegionControllerMotionRoute());
        for (const auto& product : model.at("productIds")) {
            auto p = physical("USB\\TOPOLOGY-A");
            p.vendorId = model.at("vendorId").get<USHORT>(); p.productId = product.get<USHORT>();
            selectPhysical(p); const auto stable = identity();
            CHECK(legionMotionHardwareAdmitted(p.vendorId,p.productId));
            CHECK(legionMotionCollectionAdmitted(p.vendorId,p.productId,0xFFA0,1));
            CHECK(!legionMotionCollectionAdmitted(p.vendorId,p.productId,1,1));
            CHECK(!legionMotionCollectionAdmitted(p.vendorId,p.productId,0xFFA0,2));
            CHECK(!stable.empty());
            CHECK(stable.find("usb\\topology-a") != std::string::npos);
            CHECK(stable.find(std::to_string(p.vendorId)) != std::string::npos);
            CHECK(stable.find(std::to_string(p.productId)) != std::string::npos);
            CHECK(legionMotionSelectedSensorIndex() == 0);
            rejectSameTickThenAdvance(); const auto in = sample(clockMs); CHECK(in.source.physicalIdentity == stable && in.source.sensorIndex == 0);
            CHECK(publish(in)); CHECK(legionMotionSnapshot(clockMs).has_value());
            auto same = p; same.sourcePath = "new-interface"; same.physicalIdentity = "interface-only-change";
            selectPhysical(same); CHECK(identity() == stable);
            auto different = p; different.baseContainerDeviceInstanceId = "USB\\TOPOLOGY-B";
            selectPhysical(different); CHECK(!identity().empty() && identity() != stable);
            CHECK(!publish(in)); CHECK(!legionMotionSubmissionCurrent(in,clockMs));
        }
        for (const auto& device : familyTopology.at("wrongDevices")) {
            auto p = physical("USB\\WRONG-DEVICE");
            p.vendorId = device.at("vendorId").get<USHORT>(); p.productId = device.at("productId").get<USHORT>();
            CHECK(legionMotionStableIdentity(p).empty());
            CHECK(!legionMotionHardwareAdmitted(p.vendorId,p.productId));
            CHECK(!legionMotionCollectionAdmitted(p.vendorId,p.productId,0xFFA0,1));
            selectPhysical(p);
            CHECK(!publish(sample(clockMs))); CHECK(!legionMotionSnapshot(clockMs));
        }
        auto crossed = physical("USB\\CROSSED-FAMILY");
        crossed.vendorId = model.at("rightAllowed").get<bool>() ? 0x1A86 : 0x17EF;
        crossed.productId = model.at("rightAllowed").get<bool>() ? 0xE310 : 0x6182;
        CHECK(legionMotionStableIdentity(crossed).empty());
    }
    for (const auto& model : familyTopology.at("rejectedModels")) {
        g_machineIdentity.deviceClass = model.get<std::string>();
        CHECK(!inputCaptureLegionControllerMotionRoute());
        for (const auto vid : {USHORT{0x17EF}, USHORT{0x1A86}}) {
            auto p = physical("USB\\REJECTED-MODEL"); p.vendorId = vid; p.productId = vid == 0x17EF ? 0x6182 : 0xE310;
            CHECK(legionMotionStableIdentity(p).empty());
        }
    }
    CHECK(currentPowerGeneration() == originalPower && g_hidResolveCacheGeneration.load() == originalRaw);
    CHECK(createCalls == 0 && calibrationPathCalls == 0 && processCalls == 0);
}
static void configuredSensorSettingsReadOnly() {
    // A residual Tablet-right selection must never authorize right on Go S or
    // mutate any other family's live setting merely by parsing configuration.
    g_legionMotionSensorIndex.store(1);
    const auto beforeEpoch = epoch(), beforeRevision = g_legionMotionSelectionRevision;
    for (const auto& model : familyTopology.at("models")) {
        g_machineIdentity.deviceClass = model.at("name").get<std::string>();
        const unsigned right = model.at("rightAllowed").get<bool>() ? 1u : 0u;
        CHECK(legionMotionConfiguredSensorIndex(sensorSettings(1)) == right);
        CHECK(legionMotionConfiguredSensorIndex(sensorSettings(json::number_unsigned_t{1})) == right);
        CHECK(legionMotionConfiguredSensorIndex(sensorSettings(0)) == 0);
        for (const auto& invalid : familyTopology.at("invalidSettings")) {
            const auto value = json::parse(invalid.at("json").get<std::string>());
            CHECK(legionMotionConfiguredSensorIndex(sensorSettings(value)) == 0);
            CHECK(g_legionMotionSensorIndex.load() == 1 && epoch() == beforeEpoch &&
                  g_legionMotionSelectionRevision == beforeRevision);
        }
        const json malformed[] = {nullptr, true, "input-string", json::array(), json::object(),
            {{"gyroMotion",nullptr}}, {{"gyroMotion",json::array()}}, {{"gyroMotion","wrong"}}};
        for (const auto& input : malformed) CHECK(legionMotionConfiguredSensorIndex(input) == 0);
    }
    for (const auto family : {YmccFamily::Unknown,YmccFamily::RogAlly,YmccFamily::MsiClaw,YmccFamily::Gpd}) {
        g_machineIdentity.family = family;
        g_machineIdentity.deviceClass = "HandheldCompanion.Devices.LegionGoTablet";
        CHECK(!inputCaptureLegionControllerMotionRoute());
        CHECK(legionMotionConfiguredSensorIndex(sensorSettings(1)) == 0);
        CHECK(g_legionMotionSensorIndex.load() == 1 && epoch() == beforeEpoch &&
              g_legionMotionSelectionRevision == beforeRevision);
    }
}
static void sensorSelectionRevisionFence() {
    const auto start = epoch(), revision = g_legionMotionSelectionRevision;
    CHECK(legionMotionSelectedSensorIndex() == 0);
    CHECK(!legionMotionSelectSensorIndex(0,revision+1));
    CHECK(epoch() == start && g_legionMotionSelectionRevision == revision+1);
    CHECK(legionMotionSelectSensorIndex(1,revision+2));
    CHECK(g_legionMotionSensorIndex.load() == 1 && epoch() == start+1);
    CHECK(!legionMotionSelectSensorIndex(0,revision+1));
    CHECK(g_legionMotionSensorIndex.load() == 1 && epoch() == start+1 &&
          g_legionMotionSelectionRevision == revision+2);
    CHECK(!legionMotionSelectSensorIndex(2,revision+3));
    CHECK(!legionMotionSelectSensorIndex(std::numeric_limits<unsigned>::max(),revision+3));
    CHECK(g_legionMotionSelectionRevision == revision+2 && epoch() == start+1);
    CHECK(!legionMotionSelectSensorIndex(1,revision+3));
    CHECK(g_legionMotionSelectionRevision == revision+3 && epoch() == start+1);
    CHECK(legionMotionSelectSensorIndex(0,revision+3)); // equal revision is admitted by the stated API
    CHECK(g_legionMotionSensorIndex.load() == 0 && epoch() == start+2);
    const auto high = std::uint64_t{1} << 40;
    CHECK(legionMotionSelectSensorIndex(1,high));
    CHECK(!legionMotionSelectSensorIndex(0,high-1));
    CHECK(g_legionMotionSelectionRevision == high && g_legionMotionSensorIndex.load() == 1);
    CHECK(legionMotionSelectSensorIndex(0,std::numeric_limits<std::uint64_t>::max()));
    CHECK(!legionMotionSelectSensorIndex(1,std::numeric_limits<std::uint64_t>::max()-1));
    CHECK(g_legionMotionSensorIndex.load() == 0);
}
static void tabletSideSwitchCalibration() {
    for (const auto* model : {"HandheldCompanion.Devices.LegionGoTablet", "HandheldCompanion.Devices.LegionGoTablet2"}) {
        g_machineIdentity.deviceClass = model;
        // New container separates the two model subcases without reusing tokens.
        selectPhysical(physical(std::string("USB\\SIDES-") + model));
        selectSensor(0);
        json root;
        root["controllerMotionCalibration"][identity()]["0"] = record(1,2,3,4);
        root["controllerMotionCalibration"][identity()]["1"] = record(101,102,103,104);
        writeCalibration(root);
        rejectSameTickThenAdvance(); auto in = sample(clockMs); CHECK(consume(in).has_value());
        std::vector<MotionSample> retired;
        const auto reads = calibrationPathCalls, creates = createCalls;
        for (const unsigned target : {1u,0u,1u,0u,1u,0u}) {
            retired.push_back(in); const auto oldEpoch = epoch();
            const auto configured = legionMotionConfiguredSensorIndex(sensorSettings(target));
            CHECK(configured == target);
            CHECK(legionMotionSelectSensorIndex(configured,g_legionMotionSelectionRevision+1));
            CHECK(epoch() == oldEpoch+1 && legionMotionSelectedSensorIndex() == target);
            CHECK(!publish(in)); CHECK(!legionMotionSnapshot(clockMs));
            CHECK(!legionMotionSubmissionCurrent(in,clockMs));
            rejectSameTickThenAdvance(); in = sample(clockMs); CHECK(in.source.sensorIndex == target);
            auto wrongSide = in; wrongSide.source.sensorIndex = 1-target;
            CHECK(!publish(wrongSide)); CHECK(!legionMotionSubmissionCurrent(wrongSide,clockMs));
            const auto output = consume(in); CHECK(output.has_value());
            CHECK(g_legionMotionSessionKey && g_legionMotionSessionKey->sensorIndex == target);
            auto* native = currentHandle(); CHECK(native != nullptr);
            if (native && output) {
                CHECK(native->offset.x == (target ? 101 : 1));
                CHECK(native->offset.y == (target ? 102 : 2));
                CHECK(native->offset.z == (target ? 103 : 3));
                CHECK(native->offset.weight == (target ? 104 : 4));
                CHECK(output->calibrated.x == (target ? -90 : 10));
                CHECK(output->calibrated.y == (target ? -124 : -24));
                CHECK(output->calibrated.z == (target ? -70 : 30));
            }
            const auto noExtraRead = calibrationPathCalls, noExtraCreate = createCalls;
            CHECK(inputCaptureLegionMotion(legionMotionSnapshot(clockMs),axes(in),1).has_value());
            CHECK(calibrationPathCalls == noExtraRead && createCalls == noExtraCreate);
        }
        CHECK(calibrationPathCalls == reads+6 && createCalls == creates+1 && liveHandles() == 2);
        for (const auto& old : retired) {
            CHECK(!publish(old)); CHECK(!legionMotionSubmissionCurrent(old,clockMs));
        }
    }
}
static void goSSingleSensorConsumer() {
    for (const auto* model : {"HandheldCompanion.Devices.LegionGoSZ1", "HandheldCompanion.Devices.LegionGoSZ2"}) {
        g_machineIdentity.deviceClass = "HandheldCompanion.Devices.LegionGoTablet";
        selectPhysical(physical(std::string("USB\\TABLET-BEFORE-GOS-")+model));
        selectSensor(1);
        const auto tablet = sample();
        g_machineIdentity.deviceClass = model;
        auto p = physical(std::string("USB\\GO-S-")+model); p.vendorId = 0x1A86; p.productId = 0xE310;
        selectPhysical(p);
        CHECK(inputCaptureLegionControllerMotionRoute());
        CHECK(legionMotionSelectedSensorIndex() == 0);
        CHECK(legionMotionSourceForSelectedLocked().sensorIndex == 0);
        CHECK(legionMotionConfiguredSensorIndex(sensorSettings(1)) == 0);
        selectSensor(0);
        CHECK(!publish(tablet)); CHECK(!legionMotionSubmissionCurrent(tablet,clockMs));
        json root; root["controllerMotionCalibration"][identity()]["0"] = record(8,9,10,11);
        root["controllerMotionCalibration"][identity()]["1"] = record(80,90,100,110); writeCalibration(root);
        rejectSameTickThenAdvance(); const auto in = sample(clockMs); CHECK(in.source.sensorIndex == 0);
        const auto output = consume(in); CHECK(output.has_value());
        auto* native = currentHandle(); CHECK(native && native->offset.x == 8 && native->offset.weight == 11);
        if (output) CHECK(output->calibrated.x == 3 && output->calibrated.y == -31 && output->calibrated.z == 23);
        auto wrong = in; wrong.source.sensorIndex = 1;
        CHECK(!publish(wrong)); CHECK(!legionMotionSubmissionCurrent(wrong,clockMs));
        auto wrongPhysical = p; wrongPhysical.vendorId = 0x17EF; wrongPhysical.productId = 0x6182;
        CHECK(legionMotionStableIdentity(wrongPhysical).empty());
    }
}
static void sideSwitchFinalSubmissionGuards() {
    const auto left = sample(); CHECK(consume(left).has_value());
    auto pendingLeft = frame(&left); CHECK(inputHostFrameSubmissionMayIssue(&pendingLeft));
    selectSensor(1);
    guardsRejectFrame(pendingLeft);
    rejectSameTickThenAdvance(); const auto right = sample(clockMs); CHECK(right.source.sensorIndex == 1 && consume(right).has_value());
    auto pendingRight = frame(&right); CHECK(inputHostFrameSubmissionMayIssue(&pendingRight));
    selectSensor(0);
    guardsRejectFrame(pendingRight); guardsRejectFrame(pendingLeft); // returning to left cannot revive old left
    pendingLeft.ticket.neutral = true;
    CHECK(inputHostFrameSubmissionMayIssue(&pendingLeft));
    rejectSameTickThenAdvance(); const auto freshLeft = sample(clockMs); CHECK(consume(freshLeft).has_value());
    const auto priorEpoch = epoch();
    selectSensor(0);
    CHECK(epoch() == priorEpoch && legionMotionSubmissionCurrent(freshLeft,clockMs));
}


static void registrationNonTargetHasNoWinCalls() {
    const auto before = epoch();
    for (const auto family : {YmccFamily::Unknown,YmccFamily::RogAlly,YmccFamily::MsiClaw,YmccFamily::Gpd}) {
        g_machineIdentity.family = family;
        g_machineIdentity.deviceClass = "HandheldCompanion.Devices.LegionGoTablet";
        CHECK(!legionMotionRegisterRawInput(g_hwnd));
        CHECK(registrations.empty() && lifecycle.empty() && existingTimerCalls == 0 && getLastErrorCalls == 0);
        CHECK(epoch() == before && g_legionRawInputSubscribed.load());
    }
    g_machineIdentity.family = YmccFamily::LenovoLegionGo;
    for (const auto& model : familyTopology.at("rejectedModels")) {
        g_machineIdentity.deviceClass = model.get<std::string>();
        CHECK(!legionMotionRegisterRawInput(g_hwnd));
        CHECK(registrations.empty() && epoch() == before && g_legionRawInputSubscribed.load());
    }
}
static void registrationRetirementAndRecovery() {
    const auto target = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x44440000));
    for (const auto& model : familyTopology.at("models")) {
        g_machineIdentity.deviceClass = model.at("name").get<std::string>();
        auto physicalSource = physical("USB\\REGISTRATION");
        physicalSource.vendorId = model.at("vendorId").get<USHORT>();
        physicalSource.productId = model.at("productIds").at(0).get<USHORT>();
        selectPhysical(physicalSource);
        g_legionRawInputSubscribed.store(true);
        rejectSameTickThenAdvance(); const auto old = sample(clockMs); CHECK(consume(old).has_value());
        auto pending = frame(&old); CHECK(inputHostFrameSubmissionMayIssue(&pending));
        const auto before = epoch(), calls = registrations.size();
        vendorRegistrationResult = false;
        const auto errors = getLastErrorCalls;
        CHECK(!legionMotionRegisterRawInput(target));
        CHECK(registrations.size() == calls+1 && epoch() == before+1 && !g_legionRawInputSubscribed.load());
        if (registrations.size() == calls+1) {
            const auto& entry = registrations.back(); CHECK(entry.devices.size() == 1);
            CHECK(!entry.subscribedAtCall && entry.captureEpoch == before+1 && entry.recordSize == sizeof(RAWINPUTDEVICE));
            if (!entry.devices.empty()) {
                const auto& device = entry.devices[0];
                CHECK(device.usUsagePage == 0xFFA0 && device.usUsage == 1);
                CHECK(device.dwFlags == (RIDEV_INPUTSINK|RIDEV_DEVNOTIFY) && device.hwndTarget == target);
            }
        }
        CHECK(getLastErrorCalls > errors);
        CHECK(!publish(old) && !publish(sample(clockMs)) && !legionMotionSnapshot(clockMs));
        CHECK(!legionMotionSubmissionCurrent(old,clockMs) && !legionMotionSubmissionCurrent(sample(clockMs),clockMs));
        CHECK(!g_legionMotion.current(old.context,old.source,true,clockMs,1500));
        guardsRejectFrame(pending);
        CHECK(!inputCaptureLegionMotion(old,axes(old),1));
        CHECK(!g_legionMotionSession);
        vendorRegistrationResult = true;
        const auto recoveryEpoch = epoch();
        CHECK(legionMotionRegisterRawInput(target));
        CHECK(g_legionRawInputSubscribed.load() && epoch() == recoveryEpoch+1);
        CHECK(!publish(old) && !legionMotionSubmissionCurrent(old,clockMs));
        rejectSameTickThenAdvance(); CHECK(consume(sample(clockMs)).has_value());
        CHECK(existingTimerCalls == 0); // API helper adds no timer of its own
    }
}
static void registrationKeepsBaseChannelIndependent() {
    for (const auto baseOk : {true,false}) for (const auto vendorOk : {true,false}) {
        baseRegistrationResult = baseOk; vendorRegistrationResult = vendorOk;
        registrations.clear(); lifecycle.clear(); existingTimerCalls = 0;
        CHECK(gamepadRegisterRawInput() == baseOk);
        CHECK(g_rawInputRegistered == baseOk && g_keyboardModifierRawSubscribed.load() == baseOk);
        CHECK(g_legionRawInputSubscribed.load() == vendorOk);
        CHECK(registrations.size() == 2 && existingTimerCalls == 1);
        if (registrations.size() == 2) {
            const auto& base = registrations[0].devices;
            const auto& vendor = registrations[1].devices;
            CHECK(base.size() == 5 && vendor.size() == 1);
            const unsigned usages[] = {5,4,6,2,8};
            if (base.size() == 5) for (unsigned i=0;i<5;++i)
                CHECK(base[i].usUsagePage == 1 && base[i].usUsage == usages[i] && base[i].hwndTarget == g_hwnd &&
                      base[i].dwFlags == (RIDEV_INPUTSINK|RIDEV_DEVNOTIFY));
            if (vendor.size() == 1) CHECK(vendor[0].usUsagePage == 0xFFA0 && vendor[0].usUsage == 1);
        }
        bool baseReceipt = false;
        for (const auto& row : lifecycle) if (row.at("event") == "gamepad-input-registration") {
            const auto& value = row.at("value");
            CHECK(value.contains("rawInputError") && value.at("rawInputError") == (baseOk ? 0 : 55));
            CHECK(value.contains("rawInputRegistered") && value.at("rawInputRegistered") == baseOk);
            baseReceipt = true;
        }
        CHECK(baseReceipt);
    }
}
static std::vector<BYTE> rawOracleBytes(const std::string& hex) {
    std::vector<BYTE> raw{0}; // exactly one synthetic raw zero ReportID byte
    for (std::size_t i=0;i<hex.size();i+=2)
        raw.push_back(static_cast<BYTE>(std::stoul(hex.substr(i,2),nullptr,16)));
    return raw;
}
static void realModelDecodeFrontDoorToConsumer() {
    for (const auto& model : familyTopology.at("models")) {
        const bool tablet = model.at("rightAllowed").get<bool>();
        g_machineIdentity.deviceClass = model.at("name").get<std::string>();
        auto source = physical("USB\\ACTUAL-DECODE");
        source.vendorId = model.at("vendorId").get<USHORT>();
        source.productId = model.at("productIds").at(0).get<USHORT>();
        selectPhysical(source); CHECK(legionMotionIdentityAdmitted(source));
        const auto& oracle = familyTopology.at("rawHelperOracles").at(tablet ? "Tablet" : "GoS");
        const auto raw = rawOracleBytes(oracle.at("logicalHex").get<std::string>());
        const auto count = raw.size();
        for (unsigned side=0; side<(tablet ? 2u : 1u); ++side) {
            selectSensor(side);
            auto storage = raw; storage.resize(count+16,0x5A);
            const auto decoded = legionMotionDecodeReport(source,storage.data(),storage.size(),count,side);
            CHECK(decoded.has_value()); if (!decoded) continue;
            const float actual[] = {decoded->gx,decoded->gy,decoded->gz,decoded->ax,decoded->ay,decoded->az};
            const auto& expected = oracle.at("expectedNumerators").at(side);
            for (unsigned i=0;i<6;++i) CHECK(near(actual[i],expected.at(i).get<double>()/32767.0));
            const auto shortCount = oracle.at("minimumLogicalBytes").at(side).get<unsigned>();
            CHECK(!legionMotionDecodeReport(source,storage.data(),storage.size(),shortCount,side));
            CHECK(!legionMotionDecodeReport(source,storage.data(),count,count+1,side));
            CHECK(!legionMotionDecodeReport(source,nullptr,count,count,side));
            CHECK(!legionMotionDecodeReport(source,storage.data(),storage.size(),0,side));
            auto wrongUsage = source; wrongUsage.usagePage = 1;
            CHECK(legionMotionIdentityAdmitted(wrongUsage));
            CHECK(!legionMotionCollectionAdmitted(wrongUsage.vendorId,wrongUsage.productId,wrongUsage.usagePage,wrongUsage.usage));
            CHECK(!legionMotionDecodeReport(wrongUsage,storage.data(),storage.size(),count,side));
            auto crossed = source; crossed.vendorId = tablet ? 0x1A86 : 0x17EF; crossed.productId = tablet ? 0xE310 : 0x6182;
            CHECK(!legionMotionIdentityAdmitted(crossed));
            CHECK(!legionMotionDecodeReport(crossed,storage.data(),storage.size(),count,side));
            rejectSameTickThenAdvance(); auto in = sample(clockMs); in.gyro = {decoded->gx,decoded->gy,decoded->gz}; in.accel = {decoded->ax,decoded->ay,decoded->az};
            const auto result = consume(in); CHECK(result.has_value());
            auto* native = currentHandle();
            if (native) {
                const float forwarded[] = {native->last.gx,native->last.gy,native->last.gz,native->last.ax,native->last.ay,native->last.az};
                for (unsigned i=0;i<6;++i) CHECK(near(forwarded[i],expected.at(i).get<double>()/32767.0));
            }
        }
        if (!tablet) CHECK(!legionMotionDecodeReport(source,raw.data(),raw.size(),raw.size(),1));
        // The actual normalization helper skips one zero byte, never a second.
        const BYTE twice[] = {0,0,0xA1}; const BYTE* view = twice; std::size_t length = 3;
        CHECK(legionHcInputReportView(view,length) && view == twice+1 && length == 2 && view[0] == 0);
    }
}

#include "queue_epoch_scenarios.inl"

struct Scenario { const char* name; void (*run)(); };
static const Scenario scenarios[] = {
    {"queue-clock-wrapper-route-scope",queueClockWrapperScope},
    {"queue-clock-age-window",queueClockAgeWindow},
    {"queued-message-publish-cutover",queuedMessagePublishCutover},
    {"cutover-only-snapshot-and-final-guards",cutoverOnlySnapshotAndFinalGuards},
    {"side-switch-queue-cutover",sideSwitchQueueCutover},
    {"registration-queue-cutover",registrationQueueCutover},
    {"queue-cutover-across32-wrap",queueCutoverAcross32Wrap},
    {"other-routes-ignore-legion-cutover",otherRoutesIgnoreLegionCutover},
    {"registration-non-target-zero-win-calls",registrationNonTargetHasNoWinCalls},
    {"registration-retirement-and-recovery",registrationRetirementAndRecovery},
    {"registration-base-channel-independent",registrationKeepsBaseChannelIndependent},
    {"real-model-decode-to-consumer",realModelDecodeFrontDoorToConsumer},
    {"family-topology-admission",familyTopologyAdmission},
    {"configured-sensor-settings-read-only",configuredSensorSettingsReadOnly},
    {"sensor-selection-revision-fence",sensorSelectionRevisionFence},
    {"tablet-side-switch-calibration",tabletSideSwitchCalibration},
    {"go-s-single-sensor-consumer",goSSingleSensorConsumer},
    {"side-switch-final-submission-guards",sideSwitchFinalSubmissionGuards},
    {"source-identity-and-real-route",sourceIdentityAndRealRoute},
    {"actual-sample-and-independent-calibration",actualSampleAndIndependentCalibration},
    {"windows-record-and-structured-sensor",windowsRecordCannotHitAndStructuredSensor},
    {"repeat-tick-no-calibration-or-create",repeatTickHasNoExtraCalibrationOrCreate},
    {"source-stamp-and-a-b-a",sourceStampRejectionAndSwitchBack},
    {"power-ready-and-raw-retirement",powerReadyAndRawRetirement},
    {"future-expiry-absent-recovery",futureExpiryAbsentAndRecovery},
    {"reset-create-failure-no-revival",resetAndCreateFailureDoNotReviveHandle},
    {"nonfinite-and-stationary",nonfiniteAndStationaryAxes},
    {"player-world-requests",playerWorldRequests},
    {"issue-request-guard-retirement",issueRequestGuardRetirement},
    {"neutral-release-and-non-legion-null",fullNeutralReleaseAndNonLegionNullPointer},
    {"scopes-and-exit-revoke-before-unload",actualScopesAndExitRevokeBeforeUnload},
};
int main(int argc, char** argv) {
    (void)&inputCaptureControllerCalibrationRequest;(void)&inputCaptureControllerCalibrationStatus;
    if (argc != 3) return 2;
    { std::ifstream topology(argv[2]); topology >> familyTopology; }
    if (familyTopology.at("schema") != 1) return 3;
    calibrationPath = std::filesystem::path(argv[1]) / "initial.json";
    std::filesystem::create_directories(calibrationPath.parent_path());
    int failedCases = 0; unsigned index = 0;
    for (const auto& scenario : scenarios) {
        activeCase = scenario.name; const auto beforeChecks = checks, beforeFailures = failures;
        reset(++index); scenario.run(); inputCaptureLegionMotionSessionRevoke(); CHECK(liveHandles() == 0);
        const auto failed = failures-beforeFailures; if (failed) ++failedCases;
        std::printf("CASE %s checks=%d failures=%d\n", activeCase,checks-beforeChecks,failed);
    }
    std::ofstream output(calibrationPath.parent_path()/"effects.json"); output << json(effects).dump(2);
    std::ofstream queued(calibrationPath.parent_path()/"queued-samples.json"); queued << json(trace).dump(2);
    std::printf("SUMMARY cases=%zu checks=%d failures=%d failedCases=%d\n",
        sizeof(scenarios)/sizeof(scenarios[0]),checks,failures,failedCases);
    return failures == 0 ? 0 : 1;
}
