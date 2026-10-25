#include "controller_motion_session.h"
#include "controller_motion_bank_sessions.h"
#include "controller_motion_calibration.h"
#include "legion_motion_source.h"
#include "legion_input_protocol.h"
#include "legion_motion_message_clock.h"
#include "input_frame_submission.h"
#include "windows_motion_submission.h"
#include "json.hpp"
#include "native_bridge.h"
#include <functional>
#include <algorithm>
#include <array>
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

struct XINPUT_GAMEPAD { unsigned short wButtons=0; BYTE bLeftTrigger=0,bRightTrigger=0; short sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0; };
#include "production_extra_types.generated.h"
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
    CHECK(libraryLoaded);void* h=bridge_create();if(!h)return nullptr;++createCalls;const auto id=nextHandle++;
    handles.insert_or_assign(h,NativeHandle{id});effects.push_back({{"case",activeCase},{"event","actual-native-create"},{"id",id}});return h;
}
static void deleteController(void* h){auto* p=handle(h);if(!p)return;p->alive=false;bridge_delete(h);++deleteCalls;effects.push_back({{"case",activeCase},{"event","actual-native-delete"},{"id",p->id}});}
static void setMode(void* h,int m){auto* p=handle(h);if(!p)return;bridge_mode(h,m);p->mode=bridge_mode_read(h);effects.push_back({{"case",activeCase},{"event","mode"},{"requested",m},{"actualReadback",p->mode}});}
static void setOffset(void* h,float x,float y,float z,int w){auto* p=handle(h);if(!p)return;bridge_offset(h,x,y,z,w);p->offset={x,y,z,w};}
static void processController(void* h,float gx,float gy,float gz,float ax,float ay,float az,float dt){auto* p=handle(h);if(!p)return;bridge_process(h,gx,gy,gz,ax,ay,az,dt);++processCalls;p->last={gx,gy,gz,ax,ay,az};p->dt=dt;}
static void getCal(void* h,float* x,float* y,float* z){if(handle(h))bridge_cal(h,x,y,z);}
static void getGravity(void* h,float* x,float* y,float* z){if(handle(h))bridge_gravity(h,x,y,z);}
static void getPlayer(void* h,float* x,float* y,float f){if(handle(h)){++playerCalls;bridge_player(h,x,y,f);}}
static void getWorld(void* h,float* x,float* y,float f){if(handle(h)){++worldCalls;bridge_world(h,x,y,f);}}
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

#include "platform_boundary.h"

static thread_local const InputCaptureGamepadSnapshot* t_commonFrameSource=nullptr;
static thread_local const ymcc::windows_motion::Stamp* t_windowsMotionSubmission=nullptr;
static std::atomic<uint64_t> g_inputHostVirtualIntentRevision{43};
static uint64_t inputHostEpochToken(){return frameCurrent.host;}
#include "production_extra_scopes.generated.h"
#include "production_scopes.generated.h"
#include "production_functions.generated.h"
#include "production_extra_functions.generated.h"

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

