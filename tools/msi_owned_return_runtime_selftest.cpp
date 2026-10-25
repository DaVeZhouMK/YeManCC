// Offline integration: only the OS/device/store seams below are fake. The six
// owned-return entrypoints are compiled directly from the production header;
// restore/finish and the OFF cleanup predicate are exact source extractions.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
#include "../native/msi_owned_return_recovery.h"
#include "../native/msi_crash_recovery_journal.h"
#include "../native/xinput_identity_refresh_policy.h"
using json = nlohmann::json;
namespace msiRecovery = ymcc::msi::recovery;
using Recovery = ymcc::msi::ownedReturn::Recovery;

// STUB-BEGIN WinAPI scalar layout and deterministic clock/process identity (no Windows SDK)
using BYTE = unsigned char;
using DWORD = unsigned long;
using ULONG = unsigned long;
using USHORT = unsigned short;
using ULONGLONG = unsigned long long;
using HANDLE = std::uintptr_t;
constexpr HANDLE INVALID_HANDLE_VALUE = (std::numeric_limits<HANDLE>::max)();
constexpr DWORD ERROR_NO_SUCH_DEVICE = 433;
constexpr DWORD ERROR_DEVICE_NOT_CONNECTED = 1167;
constexpr DWORD ERROR_INVALID_HANDLE = 6;
constexpr DWORD ERROR_WRITE_FAULT = 29;
constexpr DWORD FILE_FLAG_OVERLAPPED = 0x40000000;
struct XINPUT_GAMEPAD {
    USHORT wButtons = 0;
    unsigned char bLeftTrigger = 0, bRightTrigger = 0;
    short sThumbLX = 0, sThumbLY = 0, sThumbRX = 0, sThumbRY = 0;
};
static ULONGLONG g_tick = 10000;
static unsigned g_clockReads = 0;
static ULONGLONG GetTickCount64() { ++g_clockReads; return g_tick; }
static DWORD GetCurrentProcessId() { return 43100; }
static HANDLE GetCurrentProcess() { return 7; }
// STUB-END

// Exact production declarations extracted from main.cpp; never hand-maintained
// copies of command outcomes or the BUS cached read-window structure.
#include "production_types.h"

struct Counters {
    unsigned hostTryLocks = 0, opens = 0, closes = 0, invalidCloses = 0;
    unsigned writes = 0, physicalWriteAttempts = 0, badWrites = 0;
    unsigned rootQueries = 0, locationQueries = 0, durableDischarges = 0;
    unsigned terminalCancels = 0, disruptiveNotes = 0;
};
static Counters g_count;
static bool g_hostLockBlocked = false;

// STUB-BEGIN Host/power/board context and instrumented lifecycle lock boundary
class InputHostMutex {
    std::mutex mutex_;
public:
    void lock() { mutex_.lock(); }
    bool try_lock() { ++g_count.hostTryLocks; return !g_hostLockBlocked && mutex_.try_lock(); }
    void unlock() { mutex_.unlock(); }
};
static InputHostMutex g_inputHostMx;
static std::mutex g_msiPersonaPumpMx;
struct RealStickTest { bool virtualTargetEnabled = false; };
static RealStickTest g_realStickTest;
static std::string g_board = "msi", g_persona = "disabled";
static bool g_hardwareGate = true, g_powerReady = true, g_exitFence = false;
static bool g_targetAbsent = true;
static unsigned long long g_power = 9;
static HANDLE g_inputHostProcess = 0;
static bool g_inputHostPrepared = false, g_inputHostReleaseQuarantined = false;
static bool g_inputHostActive = false;
static std::atomic<bool> g_msiNativeForegroundCyclePending{false}, g_msiNativeForegroundPowerHold{false};
static bool g_foregroundIsolationBorrowBusy = false;
static bool msiForegroundIsolationBorrowBusy() { return g_foregroundIsolationBorrowBusy; }
static std::atomic<bool> g_msiDinputRouteDinputArmed{false}, g_msiDinputReturnOwed{true};
static std::atomic<unsigned long long> g_msiDinputReturnDebtEpisode{41};
static std::atomic<unsigned long long> g_msiFeedbackModeGeneration{4};
static std::atomic<unsigned> g_xinputTopologyEpoch{8};
static std::atomic<unsigned long long> g_xinputSlotIdentityEpoch{3};
static bool msiClawKnownBoardAuthority() { return g_board == "msi"; }
static bool hardwareWriteAllowed() { return g_hardwareGate && g_powerReady; }
static bool inputHostExitFenceRaised() { return g_exitFence; }
static bool ymStartSeamActive() { return false; }
static std::string inputHostConfiguredPersona() { return g_persona; }
static unsigned long long currentPowerGeneration() { return g_power; }
static bool msiDinputDeviceTargetAbsent() { return g_targetAbsent; }
static std::string inputHostOwnerToken() { return "offline-noInputHost"; }
static unsigned long long g_fakeHostEpoch=19;
static std::atomic<unsigned long long> g_inputHostVirtualIntentRevision{0},g_msiMainSourceRevision{0};
static std::atomic<unsigned> g_inputHostVirtualIntentWriters{0};
static unsigned long long inputHostEpochToken() { return g_fakeHostEpoch; }
static std::atomic<bool> g_msiTerminalReturnPending{false};
static void msiTerminalReturnCancel(const char*) { ++g_count.terminalCancels;g_msiTerminalReturnPending=false; }
static void msiNoteDisruptiveTx(const char*) { ++g_count.disruptiveNotes; }
// STUB-END

// STUB-BEGIN In-memory durable store, cached BUS snapshot and log publication
static std::mutex g_msiCrashMx, g_gamepadReadMx, g_inputCaptureGamepadSnapshotMx;
static msiRecovery::Record g_msiCrashRecord;
static bool g_msiCrashStoreInvalid = false;
static InputCaptureGamepadSnapshot g_inputCaptureGamepadSnapshot;
// Slot itself is production policy, including capture/legacy/generation checks.
static std::array<ymcc::input::identityRefresh::Slot, 4> g_xinputFeedbackIdentity;
static std::array<std::wstring, 4> g_msiOwnedReturnSlotPaths;
static DWORD g_xinputConnectedMask = 0, g_xinputVirtualEndpointMask = 0;
static DWORD g_xinputSlotUnknownMask = 0, g_xinputPhysicalSourceMask = 0;
static std::atomic<bool> g_msiOwnedReturnPending{false}, g_msiOwnedReturnWorking{false}, g_msiOwnedReturnFailed{false};
static std::atomic<unsigned long long> g_msiOwnedReturnMatchedEpisode{0}, g_msiOwnedReturnMatchedPower{0};
static std::atomic<unsigned> g_msiOwnedReturnMatchedTopology{0};
static std::atomic<unsigned long long> g_msiOwnedReturnSourceEvent{0};
static std::atomic<ULONGLONG> g_msiDinputReturnDebtPendingLogTick{0};
static std::atomic<bool> g_externalPriorityVirtualSuspended{false};
static std::atomic<unsigned> g_externalPrioritySourceMask{0};
static std::vector<std::pair<std::string, json>> g_logs;
static std::string msiCrashBoardKey() { return "MSI|Claw-A1M|Claw-A1M|handheld"; }
static std::string msiCrashProcessCreation(HANDLE) { return "133900000000000001"; }
static std::function<void(const json&)> g_logHook;
static void appendNativeLifecycleLog(const char* domain, const json& row) { g_logs.emplace_back(domain, row);if(g_logHook)g_logHook(row); }
static bool msiCrashRecoveryAfterModeDischarge() {
    ++g_count.durableDischarges;
    g_msiCrashRecord.modeReturnPending = false;
    return true;
}
// STUB-END

struct Parent { std::wstring root; std::vector<std::string> locations; };
static std::map<std::wstring, Parent> g_parents;
static std::function<void()> g_signalHook, g_openHook;

// STUB-BEGIN Inert physical-parent-root/location resolver and ASCII path conversions
static std::string W2U(const std::wstring& path) {
    std::string result;
    for (const auto c : path) {
        if (static_cast<unsigned>(c) > 127) throw std::runtime_error("fake paths must be ASCII");
        result.push_back(static_cast<char>(c));
    }
    return result;
}
static std::wstring U2W(const std::string& path) { return std::wstring(path.begin(), path.end()); }
static std::string hidHideUpper(std::string text) {
    for (auto& c : text) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
    return text;
}
static bool deviceInterfaceUsbInstance(const wchar_t* path, std::wstring& root) {
    ++g_count.rootQueries;
    const auto found = g_parents.find(path ? path : L"");
    if (found == g_parents.end()) return false;
    root = found->second.root;
    if (g_signalHook) { auto hook = std::move(g_signalHook); g_signalHook = {}; hook(); }
    return true;
}
static std::vector<std::string> msiCrashLocations(const std::string& root) {
    ++g_count.locationQueries;
    for (const auto& item : g_parents) if (W2U(item.second.root) == root) return item.second.locations;
    return {};
}
// STUB-END

static bool g_commandAvailable = true;
static bool g_commandReady=true;
// Boundary fact only; real bounded-slot reaper and Ready are exercised by the
// separate retired-I/O integration fixture, not replaced there.
static bool msiOwnedReturnCommandReady() { return g_commandReady; }
static MsiClawHidDiag g_commandDiag;
static MsiClawCommandOutcome g_retryOutcome;
static HANDLE g_nextHandle = 1000;
static std::set<HANDLE> g_liveHandles;
static std::vector<HANDLE> g_openedHandles, g_writtenHandles;

// STUB-BEGIN Inert command-open/close/modewriter; synthetic handles, no SDK or device I/O
static HANDLE msiClawOpenModeCommand(MsiClawHidDiag& diag) {
    ++g_count.opens;
    diag = g_commandDiag;
    if (g_openHook) { auto hook = std::move(g_openHook); g_openHook = {}; hook(); }
    if (!g_commandAvailable) return INVALID_HANDLE_VALUE;
    const auto handle = ++g_nextHandle;
    g_liveHandles.insert(handle);
    g_openedHandles.push_back(handle);
    return handle;
}
static void msiClawCloseModeCommand(HANDLE handle) {
    if (handle == INVALID_HANDLE_VALUE) { ++g_count.invalidCloses; return; }
    if (g_liveHandles.erase(handle) != 1) throw std::runtime_error("closing unowned/already closed fake handle");
    ++g_count.closes;
}
static MsiClawCommandOutcome msiClawSwitchModeOnHandle(BYTE mode, HANDLE handle, const MsiClawHidDiag& diag, bool ownedReturnRetry = false, const std::function<bool()>& contextCurrent = {}) {
    if(contextCurrent && !contextCurrent()){MsiClawCommandOutcome rejected;rejected.diag=diag;msiClawCloseModeCommand(handle);return rejected;}
    ++g_count.writes;
    g_writtenHandles.push_back(handle);
    if (mode != 1 || !ownedReturnRetry || g_liveHandles.count(handle) != 1 || !msiClawKnownBoardAuthority() ||
        !hardwareWriteAllowed() || g_exitFence || g_realStickTest.virtualTargetEnabled ||
        g_persona != "disabled" || g_inputHostProcess || g_inputHostActive ||
        (g_inputHostPrepared && !g_inputHostReleaseQuarantined) || !g_targetAbsent ||
        g_msiNativeForegroundCyclePending || g_msiNativeForegroundPowerHold || msiForegroundIsolationBorrowBusy())
        ++g_count.badWrites;
    auto outcome = g_retryOutcome;
    outcome.diag = diag;
    if(contextCurrent && !contextCurrent()){outcome={};outcome.diag=diag;}
    if (outcome.write.attempted) ++g_count.physicalWriteAttempts;
    if (!outcome.write.handleRetained) msiClawCloseModeCommand(handle);
    return outcome;
}
// STUB-END

// STUB-BEGIN Rebound DInput source boundaries
struct HidHideMsiMainSourceRecord {
    bool valid=false;unsigned generation=0,vendorId=0,productId=0;
    std::string interfacePath,usbInstance;
};
static HidHideMsiMainSourceRecord g_reboundSource;
static bool g_reboundAvailable=false,g_reboundReleaseOk=true;
static unsigned g_reboundReleases=0,g_reboundClears=0;
static bool msiDinputMainSourceRecordSnapshot(HidHideMsiMainSourceRecord& source){source=g_reboundSource;return g_reboundAvailable;}
static void msiDinputReleasePad(const char*){++g_reboundReleases;if(g_reboundReleaseOk){g_reboundAvailable=false;g_targetAbsent=true;}}
static void msiDinputClearOwnedState(const char*){++g_reboundClears;}
static void resetMsiReboundBoundary(){g_reboundSource={};g_reboundAvailable=false;g_reboundReleaseOk=true;g_reboundReleases=g_reboundClears=0;}
// STUB-END

// Real discharge policy/sink and physical-mask functions from main.cpp. Only
// the durable post-discharge helper above is replaced with an in-memory store.
#include "production_debt_helpers.h"
// STUB-BEGIN Initial terminal command / legacy schedule / optional trace boundaries
static std::atomic<bool> g_msiMode1TerminalAttempted{false};
static unsigned g_terminalCalls=0;
static MsiClawCommandOutcome g_terminalOutcome;
static void ymccResolveMachineIdentityOnce() {}
static bool msiControllerTraceEnabled() { return false; }
static void msiControllerTraceLog(const char*,const json&,bool) {}
static MsiClawCommandOutcome msiClawSwitchMode(BYTE mode) {
    if(mode!=1)throw std::runtime_error("fixture terminal command must be mode1");
    ++g_terminalCalls;return g_terminalOutcome;
}
static void msiTerminalReturnSchedule(const char*,const MsiClawCommandOutcome&) {}
// Cached Stop facts are fake boundaries; the OFF cleanup predicate below is real.
static bool g_inputHostHello=false,g_inputHostPrepareMayHaveReachedHost=false,g_inputHostNeutralized=false;
static struct {bool active=false;} g_hidHideVisibilityJournal;
static struct {bool required()const{return false;}} g_msiSteamDeckReleaseDebt;
static struct {bool retirementRequired()const{return false;}} g_psEndpointOwnership;
static bool inputHostTargetReturnHistorySettledLocked(){return true;}
// STUB-END
#include "../native/msi_owned_return_runtime.h"
// Byte-for-byte actual gate, readback contract, restore and finish consumers.
#include "production_restore_helpers.h"

static constexpr const char* kOwnedRoot = "USB\\VID_0DB0&PID_1902\\7&OWNED&0&3";
static constexpr const char* kReturnedRoot = "USB\\VID_0DB0&PID_1901\\7&OWNED&0&3";
static const std::vector<std::string> kLocations{"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(3)"};
static const std::wstring kCommandPath = L"\\\\?\\hid#vid_0db0&pid_1902#owned-command";
static const std::wstring kXInputPath = L"\\\\?\\hid#vid_0db0&pid_1901#owned-xinput";

static MsiClawCommandOutcome blocked() { return {}; }
static MsiClawCommandOutcome successful() {
    MsiClawCommandOutcome out;
    out.write.attempted = out.write.ok = true;
    out.write.written = 64;
    out.write.state = MsiClawIoState::Completed;
    return out;
}
static MsiClawCommandOutcome missingTransport() {
    auto out=blocked();out.write.state=MsiClawIoState::NoDevice;out.diag.enumerationComplete=true;return out;
}
static MsiClawCommandOutcome disconnected(DWORD error = ERROR_NO_SUCH_DEVICE) {
    MsiClawCommandOutcome out;
    out.write.attempted = true;
    out.write.state = MsiClawIoState::StartFailed;
    out.write.lastError = error;
    return out;
}
static void reset() {
    resetMsiReboundBoundary();
    g_msiMode1TerminalAttempted=false;g_terminalCalls=0;g_terminalOutcome={};
    g_inputHostHello=g_inputHostPrepareMayHaveReachedHost=g_inputHostNeutralized=false;g_hidHideVisibilityJournal.active=false;
    g_count = {}; g_clockReads = 0; g_tick = 10000;g_msiTerminalReturnPending=false;
    g_board = "msi"; g_persona = "disabled"; g_hardwareGate = g_powerReady = true;
    g_exitFence = g_hostLockBlocked = false; g_targetAbsent = true;
    g_realStickTest = {}; g_inputHostProcess = 0;
    g_inputHostPrepared = g_inputHostReleaseQuarantined = g_inputHostActive = false;
    g_msiNativeForegroundCyclePending = false; g_msiNativeForegroundPowerHold = false; g_foregroundIsolationBorrowBusy = false;
    g_power = 9; g_msiFeedbackModeGeneration = 4; g_xinputTopologyEpoch = 8; g_xinputSlotIdentityEpoch = 3;
    g_msiDinputRouteDinputArmed = false; g_msiDinputReturnOwed = true; g_msiDinputReturnDebtEpisode = 41;
    g_msiOwnedReturnRecovery = Recovery{}; g_msiOwnedReturnExpected = {};
    g_msiOwnedReturnTerminalReceipt={};
    g_msiOwnedReturnRetrySafe = false;
    g_msiOwnedReturnInterruptedByMode2=false;g_msiOwnedReturnFailedMode2Observed=false; g_msiOwnedReturnLastNote.clear();
    g_msiOwnedReturnPending = false; g_msiOwnedReturnWorking = false; g_msiOwnedReturnFailed = false;
    g_msiOwnedReturnMatchedEpisode = 0; g_msiOwnedReturnMatchedPower = 0; g_msiOwnedReturnMatchedTopology = 0;
    g_msiOwnedReturnSourceEvent = 0;
    g_msiDinputReturnDebtPendingLogTick = 0; g_externalPriorityVirtualSuspended = false; g_externalPrioritySourceMask = 0;
    g_msiCrashStoreInvalid = false; g_msiCrashRecord = {};
    g_msiCrashRecord.boardKey = msiCrashBoardKey();
    g_msiCrashRecord.ownerPid = GetCurrentProcessId();
    g_msiCrashRecord.ownerCreationTime = msiCrashProcessCreation(GetCurrentProcess());
    g_msiCrashRecord.transactionId = "offline-owned-return";
    g_msiCrashRecord.modeReturnPending = true;
    g_msiCrashRecord.rootInstance = kOwnedRoot;
    g_msiCrashRecord.locationPaths = kLocations;
    g_msiCrashRecord.hiddenPaths = {kOwnedRoot};
    g_inputCaptureGamepadSnapshot = {}; g_xinputFeedbackIdentity = {}; g_msiOwnedReturnSlotPaths = {};
    g_xinputConnectedMask = g_xinputVirtualEndpointMask = g_xinputSlotUnknownMask = g_xinputPhysicalSourceMask = 0;
    g_fakeHostEpoch=19;g_inputHostVirtualIntentRevision=0;g_msiMainSourceRevision=0;g_inputHostVirtualIntentWriters=0;
    g_parents.clear(); g_logs.clear(); g_logHook={}; g_signalHook = g_openHook = {};
    g_parents[kCommandPath] = {U2W(kOwnedRoot), kLocations};
    g_commandDiag = {}; g_commandDiag.productId = 0x1902; g_commandDiag.interfacePath = W2U(kCommandPath);
    g_commandDiag.deviceFound = g_commandDiag.enumerationComplete = true;
    g_commandAvailable = true; g_commandReady=true; g_retryOutcome = successful(); g_liveHandles.clear();
    g_openedHandles.clear(); g_writtenHandles.clear();
}
static void initial(const MsiClawCommandOutcome& out = blocked(), const char* reason = "input-host-stop") {
    msiOwnedReturnRecordInitial(reason, out);
}
static void maintain(ULONGLONG now) { g_tick = now; msiOwnedReturnMaintenance(now); }
static void sample(unsigned long long sequence, ULONGLONG completed, const std::string& source = "xinput") {
    g_inputCaptureGamepadSnapshot.live = true;
    g_inputCaptureGamepadSnapshot.connectedMask = g_xinputConnectedMask;
    g_inputCaptureGamepadSnapshot.inputSource = source;
    g_inputCaptureGamepadSnapshot.readAttemptSequence = sequence;
    g_inputCaptureGamepadSnapshot.readPowerGeneration = g_power;
    g_inputCaptureGamepadSnapshot.readTopologyGeneration = g_xinputTopologyEpoch.load();
    g_inputCaptureGamepadSnapshot.readIdentityGeneration = static_cast<unsigned>(g_xinputSlotIdentityEpoch.load());
    g_inputCaptureGamepadSnapshot.readStartedTick = completed > 5 ? completed - 5 : 0;
    g_inputCaptureGamepadSnapshot.readCompletedTick = completed;
    g_inputCaptureGamepadSnapshot.readContextStable = true;
}
static void physical(unsigned slot = 0, const std::string& root = kReturnedRoot,
                     const std::vector<std::string>& locations = kLocations,
                     ymcc::input::identityRefresh::Kind kind = ymcc::input::identityRefresh::Kind::Physical,
                     bool provider = true) {
    const auto path = slot == 0 ? kXInputPath : kXInputPath + std::to_wstring(slot);
    g_parents[path] = {U2W(root), locations};
    g_msiOwnedReturnSlotPaths[slot] = path;
    g_xinputConnectedMask |= 1ul << slot;
    g_xinputPhysicalSourceMask = g_xinputConnectedMask;
    if (kind == ymcc::input::identityRefresh::Kind::Virtual) g_xinputVirtualEndpointMask |= 1ul << slot;
    if (kind == ymcc::input::identityRefresh::Kind::Unknown) g_xinputSlotUnknownMask |= 1ul << slot;
    const auto epoch = g_xinputSlotIdentityEpoch.load();
    (void)g_xinputFeedbackIdentity[slot].observe({provider, true, false, false, path, kind, epoch, epoch, g_power, g_power});
}
static bool context() {
    return msiOwnedReturnContext(g_power, g_msiFeedbackModeGeneration.load(), g_msiDinputReturnDebtEpisode.load());
}
static MsiOwnedReturnSignal readSignal(ULONGLONG now) {
    g_tick=now;
    return msiOwnedReturnReadSignal(g_power, g_xinputTopologyEpoch.load(), now);
}
static json counterJson() {
    return {{"hostTryLocks", g_count.hostTryLocks}, {"commandOpens", g_count.opens}, {"commandCloses", g_count.closes},
        {"modewriterCalls", g_count.writes}, {"physicalWriteAttempts", g_count.physicalWriteAttempts}, {"badWrites", g_count.badWrites},
        {"parentRootQueries", g_count.rootQueries}, {"locationQueries", g_count.locationQueries},
        {"durableDischarges", g_count.durableDischarges}, {"terminalCancels", g_count.terminalCancels},
        {"liveSyntheticHandles", g_liveHandles.size()}, {"clockReads", g_clockReads},
        {"initialTerminalCommandCalls",g_terminalCalls},{"reboundSourceReleases",g_reboundReleases}};
}
struct Checks {
    unsigned assertions = 0;
    json cases = json::array(), failures = json::array();
    std::string current;
    void require(bool condition, const char* message) {
        ++assertions;
        if (!condition) failures.push_back({{"case", current}, {"assertion", message}});
    }
    void owed(const char* message = "responsibility and durable mode debt are retained") {
        require(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending && g_count.durableDischarges == 0, message);
    }
    void quiet(const char* message = "invalid evidence never invokes modewriter or durable discharge") {
        require(g_count.writes == 0 && g_count.durableDischarges == 0, message);
        owed();
    }
    void settled() {
        require(!g_msiDinputReturnOwed && !g_msiCrashRecord.modeReturnPending, "real discharge helper clears in-memory and durable debt");
        require(g_msiOwnedReturnRecovery.completed() && !g_msiOwnedReturnPending && !g_msiOwnedReturnWorking && !g_msiOwnedReturnFailed,
            "completion is published through actual runtime mirrors");
        require(g_count.durableDischarges == 1, "qualified stable evidence discharges exactly once");
        require(g_msiOwnedReturnMatchedEpisode == g_msiDinputReturnDebtEpisode && g_msiOwnedReturnMatchedPower == g_power &&
            g_msiOwnedReturnMatchedTopology == g_xinputTopologyEpoch, "matched certificate binds current episode/power/topology");
        unsigned confirms = 0;
        for (const auto& row : g_logs) if (row.second.value("action", "") == "physical-signal-confirmed") {
            ++confirms;
            require(row.second.value("physicalSignalVerified", false) && !row.second.value("steamConsumptionVerified", true) &&
                !row.second.value("modeWriteIssued", true), "receipt asserts physical evidence only and no mode-write proof");
        }
        require(confirms == 1, "one production physical-signal-confirmed receipt");
    }
    template<class F> void run(const std::string& name, F test) {
        reset(); current = name;
        const auto before = failures.size(); const auto beforeAssertions = assertions;
        try { test(); } catch (const std::exception& error) { require(false, error.what()); }
        require(g_count.badWrites == 0, "every synthetic write uses a live new handle under allowed context");
        require(g_liveHandles.empty(), "all temporary fake command handles are closed/consumed");
        json logs = json::array();
        for (const auto& row : g_logs) logs.push_back({{"domain", row.first}, {"row", row.second}});
        cases.push_back({{"name", name}, {"passed", before == failures.size()}, {"assertions", assertions - beforeAssertions},
            {"counters", counterJson()}, {"pending", g_msiOwnedReturnPending.load()}, {"working", g_msiOwnedReturnWorking.load()},
            {"failed", g_msiOwnedReturnFailed.load()}, {"owed", g_msiDinputReturnOwed.load()},
            {"observations", g_msiOwnedReturnRecovery.observations()}, {"retryUsed", g_msiOwnedReturnRecovery.retryUsed()},
            {"logs", logs}});
    }
};

static void classify(Checks& c) {
    struct Row { const char* name; MsiClawCommandOutcome outcome; bool expected; };
    std::vector<Row> rows{
        {"blocked-no-write", blocked(), true}, {"success", successful(), true},
        {"error433-zero-byte", disconnected(), true},
        {"error1167-zero-byte", disconnected(ERROR_DEVICE_NOT_CONNECTED), true},
        {"error6-zero-byte", disconnected(ERROR_INVALID_HANDLE), true},
        {"access-denied", disconnected(5), false}, {"unknown-error", disconnected(0), false},
    };
    auto out=blocked();out.write.state=MsiClawIoState::SlotBusyRetired;rows.push_back({"prior-slot-busy-deferred-until-ready",out,true});
    out=blocked();out.write.state=MsiClawIoState::Retired;rows.push_back({"unattempted-retired-unknown",out,false});
    out=blocked();out.write.state=MsiClawIoState::IoDesignPending;rows.push_back({"unattempted-io-design-unknown",out,false});
    out=blocked();out.write.state=MsiClawIoState::StartFailed;out.write.lastError=ERROR_WRITE_FAULT;rows.push_back({"durable-gate-no-write",out,true});
    out = disconnected(); out.write.written = 1; rows.push_back({"partial-byte", out, false});
    out = disconnected(); out.write.state = MsiClawIoState::ShortWrite; rows.push_back({"short-write", out, false});
    out = disconnected(); out.write.state = MsiClawIoState::LateCompleted; rows.push_back({"late-completed", out, false});
    out = disconnected(); out.write.state = MsiClawIoState::Retired; rows.push_back({"retired", out, false});
    out = disconnected(); out.write.state = MsiClawIoState::TimeoutCancelledReaped; rows.push_back({"timeout-state", out, false});
    out = disconnected(); out.write.timedOut = true; rows.push_back({"timed-out", out, false});
    out = successful(); out.write.timedOut = true; rows.push_back({"success-but-timed-out", out, false});
    out = blocked(); out.write.handleRetained = true; rows.push_back({"blocked-but-retained", out, false});
    out = successful(); out.write.handleRetained = true; rows.push_back({"success-but-retained", out, false});
    for (const auto& row : rows) c.run(std::string("retryable/") + row.name, [&] {
        c.require(msiOwnedReturnRetryable(row.outcome) == row.expected, "real Retryable classifies actual command outcome");
        initial(row.outcome);
        c.require(g_msiOwnedReturnPending && g_msiOwnedReturnRetrySafe == row.expected, "tracking retains debt even when retry is unsafe");
        maintain(12000); maintain(13000); maintain(14000);
        c.require(g_count.writes == (row.expected ? 1u : 0u), "actual Maintenance respects Retryable and one retry maximum");
        c.owed();
    });
}
static void trackingAndReturn(Checks& c) {
    for (const auto& row : std::vector<std::pair<std::string, MsiClawCommandOutcome>>{
        {"blocked", blocked()}, {"error433-zero-byte", disconnected()}, {"initial-success", successful()}})
        c.run("tracking/" + row.first + "-new-handle-single-retry-stable-return", [&] {
            c.require(msiRecovery::validRecord(g_msiCrashRecord), "fixture owns a real valid journal record");
            initial(row.second);
            c.require(g_msiOwnedReturnPending && g_msiOwnedReturnWorking && !g_msiOwnedReturnFailed, "RecordInitial publishes active tracking");
            c.require(g_count.writes == 0 && g_count.terminalCancels == 1, "tracking schedules no write and cancels competing terminal plan");
            c.require(msiOwnedReturnRecordCurrent() && context(), "real RecordCurrent and Context accept current owned episode");
            c.owed("initial receipt, including successful write, does not clear debt");
            maintain(10000); maintain(11000);
            c.require(g_count.writes == 0, "no retry before the 1500ms delay");
            maintain(12000);
            c.require(g_count.writes == 1 && g_msiOwnedReturnRecovery.retryUsed(), "live same-root PID1902 consumes single retry");
            c.require(g_writtenHandles.size() == 1 && g_writtenHandles.front() == g_openedHandles.back() &&
                g_writtenHandles.front() != 0x55, "retry uses its freshly opened synthetic handle, never old initial handle");
            c.require(g_count.disruptiveNotes == 1, "successful retry records one disruptive transaction receipt");
            c.owed("successful retry receipt leaves both debts pending");
            maintain(13000); maintain(14000);
            c.require(g_count.writes == 1, "missing input after successful retry never repeats mode write");
            physical(2); sample(101, 15000, "xinput-any-connected");
            const auto signal = readSignal(15000);
            c.require(signal.matched && signal.mask == 4 && signal.sequence == 101, "ReadSignal consumes actual cached path and connected slot mask");
            maintain(15000); c.owed("first fresh exact physical window cannot discharge");
            sample(102, 16000, "xinput-any-connected"); maintain(16000); c.settled();
            maintain(17000); initial(row.second); maintain(18000);
            c.require(g_count.writes == 1 && g_count.durableDischarges == 1 && !g_msiOwnedReturnPending,
                "completed same-episode OFF reentry cannot renew budget or discharge twice");
        });
    c.run("R120/ordinary-confirmation-binds-read-window",[&]{
        initial();physical();sample(501,12000);maintain(12000);sample(502,13000);maintain(13000);c.settled();
        json proof=json::object();for(const auto& row:g_logs)if(row.second.value("action","")=="physical-signal-confirmed")proof=row.second;
        c.require(proof.value("readStartedTick",0ULL)==12995 && proof.value("readCompletedTick",0ULL)==13000,
            "ordinary return receipt retains actual read interval");
        c.require(proof.value("readIdentityGeneration",0U)==g_xinputSlotIdentityEpoch.load(),"ordinary receipt binds actual identity generation");
        c.require(proof.value("readFreshnessPolicy",std::string{})=="canonical-start-window-1500ms-query-context-rechecked","ordinary receipt identifies strict cached evidence policy");
    });
    c.run("R120/fresh-end-long-read-cannot-discharge", [&] {
        initial();physical();g_commandDiag.productId=0x1901;
        for(unsigned i=0;i<2;++i){const ULONGLONG now=12000+i*1000;sample(201+i,now);g_inputCaptureGamepadSnapshot.readStartedTick=now-2000;maintain(now);}
        c.require(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending && g_count.durableDischarges==0,
            "fresh completion cannot turn a blocked canonical read into exact fresh physical return");
        c.require(g_msiOwnedReturnPending && !g_msiOwnedReturnRecovery.completed(),"unproven physical return retains observer");
    });
    for(const std::string mutation:{"identity","power","topology","elapsed-window"})c.run("R120/identity-query-retires-"+mutation,[&]{
        initial();physical();sample(301,12000);g_tick=12000;
        g_signalHook=[mutation]{if(mutation=="identity")++g_xinputSlotIdentityEpoch;if(mutation=="power")++g_power;if(mutation=="topology")++g_xinputTopologyEpoch;if(mutation=="elapsed-window")g_tick+=1600;};
        const auto result=readSignal(12000);c.require(!result.matched,"blocking root query cannot publish an expired or retired source match");
        c.quiet();
    });
    c.run("R120/matching-read-start-at-freshness-boundary",[&]{initial();physical();sample(401,12000);g_inputCaptureGamepadSnapshot.readStartedTick=10500;g_tick=12000;c.require(readSignal(12000).matched,"original 1500ms canonical age is inclusive");c.quiet();});
    c.run("tracking/blocked-power-readiness-restores-without-losing-debt", [&] {
        g_powerReady = false; initial(); maintain(12000);
        c.require(g_msiOwnedReturnPending && g_msiOwnedReturnRecovery.observations() == 0, "not-ready initial return still tracks responsibility without probing");
        c.quiet(); g_powerReady = true; maintain(13000);
        c.require(g_count.writes == 1, "existing lifecycle can recover after readiness returns"); c.owed();
    });
    for (const bool success : {false, true}) c.run(success ? "tracking/retry-success-budget-never-refunded" : "tracking/retry-error433-budget-never-refunded", [&] {
        initial(); g_retryOutcome = success ? successful() : disconnected(); maintain(12000);
        for (unsigned i = 0; i < 5; ++i) { initial(); maintain(13000 + i * 1000); }
        ++g_xinputTopologyEpoch; maintain(20000); ++g_power; maintain(21000);
        c.require(g_count.writes == 1 && g_msiOwnedReturnRecovery.retryUsed(), "OFF reentry/events never refund a used retry after any outcome"); c.owed();
    });
}
static void invalidSignals(Checks& c) {
    using Kind = ymcc::input::identityRefresh::Kind;
    using Mutation = std::function<void(ULONGLONG)>;
    std::vector<std::pair<std::string, Mutation>> rows{
        {"external-controller", [](ULONGLONG) { physical(0, "USB\\VID_045E&PID_028E\\EXTERNAL", {}); }},
        {"other-root-location", [](ULONGLONG) { physical(0, "USB\\VID_0DB0&PID_1901\\OTHER", {"PCIROOT(0)#USBROOT(0)#USB(4)"}); }},
        {"same-tail-without-location-proof", [](ULONGLONG) { g_msiOwnedReturnExpected.locationPaths.clear(); g_msiCrashRecord.locationPaths.clear(); g_parents[kXInputPath].locations.clear(); }},
        {"legacy-provider-pathless", [](ULONGLONG) { physical(0, kReturnedRoot, kLocations, Kind::Physical, false); }},
        {"empty-cached-path", [](ULONGLONG) { g_msiOwnedReturnSlotPaths[0].clear(); }},
        {"virtual-slot", [](ULONGLONG) { physical(0, kReturnedRoot, kLocations, Kind::Virtual); }},
        {"virtual-parent-spoofed-physical", [](ULONGLONG) { physical(0, "ROOT\\VIGEMBUS\\VIRTUAL", kLocations); }},
        {"unknown-slot", [](ULONGLONG) { physical(0, kReturnedRoot, kLocations, Kind::Unknown); }},
        {"dinput-pid1902", [](ULONGLONG) { physical(0, kOwnedRoot, kLocations); }},
        {"wrong-pid1903", [](ULONGLONG) { physical(0, "USB\\VID_0DB0&PID_1903\\7&OWNED&0&3", kLocations); }},
        {"unresolved-parent", [](ULONGLONG) { g_parents.erase(kXInputPath); }},
        {"duplicate-physical-matches", [](ULONGLONG) { physical(1); g_inputCaptureGamepadSnapshot.connectedMask = g_xinputConnectedMask; }},
        {"not-live", [](ULONGLONG) { g_inputCaptureGamepadSnapshot.live = false; }},
        {"unstable-read-context", [](ULONGLONG) { g_inputCaptureGamepadSnapshot.readContextStable = false; }},
        {"zero-sequence", [](ULONGLONG) { g_inputCaptureGamepadSnapshot.readAttemptSequence = 0; }},
        {"old-power-sample", [](ULONGLONG) { --g_inputCaptureGamepadSnapshot.readPowerGeneration; }},
        {"old-topology-sample", [](ULONGLONG) { --g_inputCaptureGamepadSnapshot.readTopologyGeneration; }},
        {"future-completion", [](ULONGLONG now) { g_inputCaptureGamepadSnapshot.readCompletedTick = now + 1; }},
        {"stale-1501ms", [](ULONGLONG now) { g_inputCaptureGamepadSnapshot.readCompletedTick = now - 1501; }},
        {"missing-read-start", [](ULONGLONG) { g_inputCaptureGamepadSnapshot.readStartedTick = 0; }},
        {"start-after-completion", [](ULONGLONG now) { g_inputCaptureGamepadSnapshot.readStartedTick = now + 1; }},
        {"fresh-end-but-blocked-read", [](ULONGLONG now) { g_inputCaptureGamepadSnapshot.readStartedTick = now - 1501; }},
        {"old-read-identity-with-current-path", [](ULONGLONG) { --g_inputCaptureGamepadSnapshot.readIdentityGeneration; }},
        {"rog-hid-source", [](ULONGLONG) { g_inputCaptureGamepadSnapshot.inputSource = "rog-hid"; }},
        {"none-source", [](ULONGLONG) { g_inputCaptureGamepadSnapshot.inputSource = "none"; }},
        {"sample-slot-disconnected", [](ULONGLONG) { g_inputCaptureGamepadSnapshot.connectedMask = 0; }},
        {"current-slot-disconnected", [](ULONGLONG) { g_xinputConnectedMask = 0; }},
        {"retired-identity-epoch", [](ULONGLONG) { ++g_xinputSlotIdentityEpoch; }},
        {"retired-identity-power", [](ULONGLONG) { ++g_power; g_inputCaptureGamepadSnapshot.readPowerGeneration = g_power; }},
    };
    for (const auto& row : rows) c.run("signal-reject/" + row.first, [&] {
        initial(); physical();
        // A live PID1901 command interface may be observed, but it cannot be
        // switched just because some cached XInput evidence was rejected.
        g_commandDiag.productId = 0x1901;
        for (unsigned i = 0; i < 2; ++i) {
            const ULONGLONG now = 12000 + i * 1000;
            sample(101 + i, now); row.second(now);
            c.require(!readSignal(now).matched, "real ReadSignal rejects invalid path/identity/provenance");
            maintain(now); c.quiet();
        }
        c.require(g_msiOwnedReturnPending && !g_msiOwnedReturnRecovery.completed(), "invalid cached evidence preserves pending responsibility");
    });
    c.run("signal/age1500-boundary-valid", [&] {
        initial(); physical(); sample(1, 10500);g_inputCaptureGamepadSnapshot.readStartedTick=10500; c.require(readSignal(12000).matched, "inclusive 1500ms cache age is valid");
        maintain(12000); sample(2, 11500);g_inputCaptureGamepadSnapshot.readStartedTick=11500; maintain(13000); c.settled();
        c.require(g_count.writes == 0, "valid cached XInput settles without modewriter");
    });
    c.run("signal/exact-root-without-location-legacy-record", [&] {
        g_msiCrashRecord.rootInstance = kReturnedRoot; g_msiCrashRecord.locationPaths.clear();
        initial(); physical(0, kReturnedRoot, {}); sample(1, 12000); maintain(12000);
        sample(2, 13000); maintain(13000); c.settled();
    });
    c.run("signal/same-sequence-and-stationary-packet", [&] {
        initial(); physical(); sample(77, 12000); maintain(12000);
        sample(77, 13000); maintain(13000); c.owed("same fresh-read identity cannot provide second evidence");
        sample(78, 14000); maintain(14000); c.settled();
        c.require(g_inputCaptureGamepadSnapshot.pad.wButtons == 0, "stationary all-zero gamepad data still permits distinct read-window evidence");
    });
    c.run("signal/invalid-middle-window-resets-stability", [&] {
        initial(); physical(); sample(1, 12000); maintain(12000);
        sample(2, 13000); g_inputCaptureGamepadSnapshot.readContextStable = false; g_commandAvailable = false; maintain(13000);
        sample(3, 14000); maintain(14000); c.owed("a missed window requires two new matching fresh windows");
        sample(4, 15000); maintain(15000); c.settled();
    });
    for (const bool power : {false, true}) c.run(power ? "signal/power-change-resets-stability" : "signal/topology-change-resets-stability", [&] {
        initial(); physical(); sample(1, 12000); maintain(12000);
        if (power) ++g_power; else ++g_xinputTopologyEpoch;
        physical(); sample(2, 13000); maintain(13000); c.owed("event generation change retires prior stable window");
        sample(3, 14000); maintain(14000); c.settled();
    });
}

static void contextAndOwnership(Checks& c) {
    using Guard = std::function<void()>;
    const std::vector<std::pair<std::string, Guard>> guards{
        {"Rog", [] { g_board = "rog"; }}, {"Gpd", [] { g_board = "gpd"; }},
        {"write-gate-false", [] { g_hardwareGate = false; }}, {"ready-false", [] { g_powerReady = false; }},
        {"exit-fence", [] { g_exitFence = true; }}, {"Host-process", [] { g_inputHostProcess = 55; }},
        {"Host-prepared", [] { g_inputHostPrepared = true; }}, {"Host-active", [] { g_inputHostActive = true; }},
        {"DInput-route-armed", [] { g_msiDinputRouteDinputArmed = true; }},
    };
    for (const auto& row : guards) c.run("context-block/" + row.first, [&] {
        initial(); row.second();
        c.require(!context(), "real Context rejects unavailable board/power/Host ownership");
        physical(); sample(1, 12000); maintain(12000); sample(2, 13000); maintain(13000);
        c.quiet(); c.require(g_count.opens == 0 && g_msiOwnedReturnRecovery.observations() == 0,
            "blocked context performs no command discovery or observation work");
    });
    c.run("context/parameter-generations-must-match", [&] {
        initial();
        c.require(!msiOwnedReturnContext(g_power + 1, g_msiFeedbackModeGeneration, g_msiDinputReturnDebtEpisode), "stale power parameter rejected");
        c.require(!msiOwnedReturnContext(g_power, g_msiFeedbackModeGeneration + 1, g_msiDinputReturnDebtEpisode), "stale mode parameter rejected");
        c.require(!msiOwnedReturnContext(g_power, g_msiFeedbackModeGeneration, g_msiDinputReturnDebtEpisode + 1), "stale episode parameter rejected");
        g_msiDinputReturnOwed = false;
        c.require(!context(), "no owned return debt grants no context");
    });
    c.run("context/quarantined-prepared-Host-can-return", [&] {
        initial(); g_inputHostPrepared = g_inputHostReleaseQuarantined = true;
        c.require(context(), "fully released quarantined preparation is allowed by actual Context");
        maintain(12000); c.require(g_count.writes == 1, "quarantined preparation permits one exact live DInput retry"); c.owed();
    });
    for (const auto& row : std::vector<std::pair<std::string, Guard>>{
        {"target-enabled", [] { g_realStickTest.virtualTargetEnabled = true; }},
        {"persona-changed", [] { g_persona = "xbox"; }},
        {"episode-changed", [] { ++g_msiDinputReturnDebtEpisode; }}})
        c.run("cancellation/" + row.first, [&] {
            initial(); maintain(10000); row.second(); maintain(12000);
            c.require(!g_msiOwnedReturnPending && !g_msiOwnedReturnWorking && !g_msiOwnedReturnFailed,
                "new target or episode cancels this recovery plan");
            c.quiet();
        });
    c.run("cancellation/explicit-preserves-episode-tombstone-and-debt", [&] {
        initial(); maintain(12000); msiOwnedReturnCancel("offline-new-target");
        c.require(!g_msiOwnedReturnPending && g_msiOwnedReturnMatchedEpisode == 0, "Cancel clears work and certificate");
        initial(); maintain(14000); msiOwnedReturnCancel(nullptr);
        c.require(!g_msiOwnedReturnPending && g_count.writes == 1 && g_msiOwnedReturnRecovery.retryUsed(), "same-episode OFF after cancel cannot rearm consumed retry"); c.owed();
        ++g_msiDinputReturnDebtEpisode; initial();
        c.require(g_msiOwnedReturnPending && !g_msiOwnedReturnRecovery.retryUsed() && g_msiOwnedReturnRecovery.observations() == 0,
            "only an explicit new debt episode starts new tracking");
    });
    c.run("cancellation/lifecycle-host-lock-unavailable", [&] {
        initial(); g_hostLockBlocked = true; maintain(12000);
        c.quiet(); c.require(g_count.hostTryLocks == 1 && g_msiOwnedReturnRecovery.observations() == 0, "nonblocking Host-lock failure performs no observation work");
        g_hostLockBlocked = false; maintain(13000); c.require(g_count.writes == 1, "existing maintenance can resume when serialization lock becomes available");
    });
    const std::vector<std::pair<std::string, Guard>> recordChanges{
        {"invalid-store", [] { g_msiCrashStoreInvalid = true; }},
        {"return-already-cleared", [] { g_msiCrashRecord.modeReturnPending = false; }},
        {"board-key", [] { g_msiCrashRecord.boardKey += "changed"; }},
        {"root", [] { g_msiCrashRecord.rootInstance = "USB\\VID_0DB0&PID_1902\\OTHER"; }},
        {"locations", [] { g_msiCrashRecord.locationPaths = {"PCIROOT(0)#USB(4)"}; }},
        {"owner-pid", [] { ++g_msiCrashRecord.ownerPid; }},
        {"owner-creation", [] { g_msiCrashRecord.ownerCreationTime = "133900000000000002"; }},
    };
    for (const auto& row : recordChanges) c.run("record-current-reject/" + row.first, [&] {
        initial(); row.second();
        c.require(!msiOwnedReturnRecordCurrent(), "actual RecordCurrent rejects changed durable ownership");
        maintain(12000); c.require(g_count.opens == 0 && g_count.writes == 0 && g_count.durableDischarges == 0,
            "changed record performs neither command open nor discharge");
        c.require(g_msiDinputReturnOwed && g_msiOwnedReturnPending, "record mismatch cannot erase in-memory responsibility");
    });
    const std::vector<std::pair<std::string, Guard>> drift{
        {"power", [] { ++g_power; }}, {"mode", [] { ++g_msiFeedbackModeGeneration; }},
        {"topology", [] { ++g_xinputTopologyEpoch; }}, {"episode", [] { ++g_msiDinputReturnDebtEpisode; }},
        {"record", [] { ++g_msiCrashRecord.ownerPid; }}, {"new-target", [] { g_realStickTest.virtualTargetEnabled = true; }},
        {"Host", [] { g_inputHostProcess = 55; }}, {"ready-false", [] { g_powerReady = false; }},
        {"foreground-cycle-pending", [] { g_msiNativeForegroundCyclePending = true; }},
        {"foreground-power-hold", [] { g_msiNativeForegroundPowerHold = true; }},
        {"foreground-borrow-busy", [] { g_foregroundIsolationBorrowBusy = true; }},
    };
    for (const auto& row : drift) c.run("post-query-recheck/" + row.first, [&] {
        initial(); physical(); sample(1, 12000); g_signalHook = row.second; maintain(12000);
        c.quiet(); c.require(g_count.opens == 0 && g_msiOwnedReturnMatchedEpisode == 0,
            "context drift during real signal query cannot publish match or enter modewriter");
    });
    for (const auto& row : drift) c.run("before-retry-recheck/" + row.first, [&] {
        initial(); g_openHook = row.second; maintain(12000);
        c.quiet(); c.require(!g_msiOwnedReturnRecovery.retryUsed(), "context drift during command open cannot consume a retry");
    });
    for (const auto& row : std::vector<std::pair<std::string, Guard>>{
        {"Rog", [] { g_board = "rog"; }}, {"Gpd", [] { g_board = "gpd"; }},
        {"target-enabled", [] { g_realStickTest.virtualTargetEnabled = true; }},
        {"configured-persona", [] { g_persona = "xbox"; }}, {"no-debt", [] { g_msiDinputReturnOwed = false; }},
        {"invalid-schema", [] { g_msiCrashRecord.schema = 99; }},
        {"wrong-board-record", [] { g_msiCrashRecord.boardKey = "ROG|Ally|Ally|handheld"; }},
        {"wrong-owner-pid", [] { ++g_msiCrashRecord.ownerPid; }},
        {"wrong-owner-creation", [] { g_msiCrashRecord.ownerCreationTime = "133900000000000002"; }},
        {"no-pending-durable-return", [] { g_msiCrashRecord.modeReturnPending = false; }}})
        c.run("initial-reject/" + row.first, [&] {
            row.second(); initial(); maintain(12000);
            c.require(!g_msiOwnedReturnPending && g_count.writes == 0 && g_count.opens == 0 && g_count.terminalCancels == 0,
                "RecordInitial never grants tracking or any action without eligible board/owner/reason");
        });
    for (const char* reason : {static_cast<const char*>(nullptr), "off", "input-host-start", "shutdown"})
        c.run(std::string("initial-reject/reason-") + (reason ? reason : "null"), [&] {
            initial(blocked(), reason); maintain(12000);
            c.require(!g_msiOwnedReturnPending && g_count.terminalCancels == 0, "only two production stop reasons admit tracking"); c.quiet();
        });
    c.run("initial/deferred-transport-reason", [&] {
        initial(blocked(), "input-host-stop-deferred-transport"); c.require(g_msiOwnedReturnPending, "deferred transport retains actual return tracking");
        maintain(12000); c.require(g_count.writes == 1, "deferred stop may make one qualified later retry"); c.owed();
    });
}
static void foregroundIsolation(Checks& c) {
    struct Fact {
        const char* name;
        std::function<void(bool)> set;
        std::function<bool()> get;
    };
    const std::vector<Fact> facts{
        {"cycle-pending", [](bool value) { g_msiNativeForegroundCyclePending = value; }, [] { return g_msiNativeForegroundCyclePending.load(); }},
        {"power-hold", [](bool value) { g_msiNativeForegroundPowerHold = value; }, [] { return g_msiNativeForegroundPowerHold.load(); }},
        {"borrow-busy", [](bool value) { g_foregroundIsolationBorrowBusy = value; }, [] { return msiForegroundIsolationBorrowBusy(); }},
    };
    for (const auto& fact : facts) c.run(std::string("foreground-isolation/") + fact.name + "-blocks-and-recovers", [&] {
        fact.set(true); initial();
        c.require(g_msiOwnedReturnPending, "busy physical release window still records the outstanding return duty");
        c.require(!context(), "real Context rejects the existing physical release window guard");
        maintain(12000); maintain(13000);
        c.quiet();
        c.require(!g_msiOwnedReturnRecovery.retryUsed() && g_msiOwnedReturnRecovery.observations() == 0 && g_count.opens == 0,
            "cycle/helper/power hold never consumes the sole retry or opens a command handle");
        c.require(fact.get(), "runtime reads the other lifecycle fact without clearing its ownership");
        fact.set(false);
        c.require(context(), "the same real Context recovers after the other lifecycle owner releases its fact");
        maintain(14000);
        c.require(g_count.writes == 1 && g_msiOwnedReturnRecovery.retryUsed(), "guard recovery retains the original unused single-retry budget");
        c.owed("post-recovery successful write still cannot discharge the return debt");
        fact.set(true); physical(); sample(1, 15000); maintain(15000); sample(2, 16000); maintain(16000);
        c.owed("fresh physical windows cannot settle while the physical release window is busy");
        c.require(fact.get() && g_count.writes == 1, "return tracking never releases another owner's busy state or repeats the retry");
        fact.set(false); sample(3, 17000); maintain(17000); c.owed();
        sample(4, 18000); maintain(18000); c.settled();
        c.require(g_count.writes == 1, "valid physical completion after release requires no additional write");
    });
}

static void commandBoundaries(Checks& c) {
    using Change = std::function<void()>;
    for (const auto& row : std::vector<std::pair<std::string, Change>>{
        {"no-command-handle", [] { g_commandAvailable = false; }},
        {"already-pid1901", [] { g_commandDiag.productId = 0x1901; }},
        {"other-pid", [] { g_commandDiag.productId = 0x9999; }},
        {"no-interface-path", [] { g_commandDiag.interfacePath.clear(); }},
        {"unknown-parent", [] { g_parents.erase(kCommandPath); }},
        {"other-root-location", [] { g_parents[kCommandPath] = {L"USB\\VID_0DB0&PID_1902\\OTHER", {"PCIROOT(0)#USB(4)"}}; }},
        {"virtual-parent", [] { g_parents[kCommandPath] = {L"ROOT\\VIGEMBUS\\VIRTUAL", kLocations}; }},
        {"target-not-absent", [] { g_targetAbsent = false; }},
    }) c.run("command-reject/" + row.first, [&] {
        initial(); row.second(); maintain(12000); maintain(13000);
        c.quiet(); c.require(!g_msiOwnedReturnRecovery.retryUsed(), "unqualified live command identity cannot consume retry");
        c.require(g_count.opens == 2, "only granted existing observation windows inspect a synthetic command handle");
    });
    c.run("command/exact-full-root-without-location-can-retry", [&] {
        g_msiCrashRecord.locationPaths.clear(); g_parents[kCommandPath].locations.clear(); initial(); maintain(12000);
        c.require(g_count.writes == 1, "exact full owned PID1902 root is sufficient even for legacy no-location journal"); c.owed();
    });
}
static void windowsAndDormancy(Checks& c) {
    c.run("context/full64-mode-generation", [&] {
        g_msiFeedbackModeGeneration = (1ull << 40) + 4;
        initial(); c.require(context(), "Context preserves the full 64-bit mode generation");
        maintain(12000); c.require(g_count.writes == 1, "64-bit mode token is preserved through Maintenance and retry rechecks");
        c.owed();
    });
    c.run("budget/unsafe-initial-OFF-reentry-cannot-upgrade-retry", [&] {
        auto unsafe = disconnected(); unsafe.write.handleRetained = true; initial(unsafe);
        for (unsigned i = 0; i < 20; ++i) {
            g_tick = 10000 + i * 1000; initial(successful()); maintain(g_tick);
        }
        c.require(!g_msiOwnedReturnRetrySafe && !g_msiOwnedReturnRecovery.retryUsed() && g_count.opens == 0,
            "a later successful OFF receipt cannot upgrade an ambiguous original retry classification");
        c.require(!g_msiOwnedReturnWorking && g_msiOwnedReturnFailed, "unsafe initial outcome still has a bounded observation window");
        c.quiet(); physical(); ++g_xinputTopologyEpoch; sample(1, 40000); maintain(40000);
        sample(2, 41000); maintain(41000); c.settled();
    });
    c.run("budget/OFF-reentry-and-subsecond-calls-do-not-refresh", [&] {
        initial(); maintain(10000); const auto terminal = g_count.terminalCancels;
        for (unsigned i = 1; i < 1000; ++i) {
            g_tick = 10000 + i; initial(i % 2 ? blocked() : successful()); maintain(g_tick);
        }
        c.require(g_msiOwnedReturnRecovery.observations() == 1 && g_count.hostTryLocks == 1,
            "subsecond repeated OFF/maintenance returns before Host lock and consumes no window");
        c.require(g_count.terminalCancels == terminal && g_count.opens == 1 && g_count.writes == 0,
            "OFF reentry neither restarts another owner nor refreshes retry start time");
        maintain(11000); maintain(12000);
        c.require(g_count.writes == 1 && g_msiOwnedReturnRecovery.observations() == 3,
            "single retry still uses original 1500ms start and existing one-second cadence"); c.owed();
    });
    c.run("budget/twentieth-observation-can-complete", [&] {
        initial();
        for (unsigned i = 0; i < 18; ++i) maintain(10000 + i * 1000);
        physical(); sample(1, 28000); maintain(28000); c.owed();
        sample(2, 29000); maintain(29000); c.settled();
        c.require(g_msiOwnedReturnRecovery.observations() == 20 && g_count.writes == 1,
            "last granted observation may settle after due changes observing to dormant");
    });
    for (const bool powerEvent : {false, true}) c.run(powerEvent ? "dormant/power-event-reobserves-without-refunding-retry" : "dormant/topology-event-reobserves-without-refunding-retry", [&] {
        initial();
        for (unsigned i = 0; i < 20; ++i) maintain(10000 + i * 1000);
        c.require(g_msiOwnedReturnPending && !g_msiOwnedReturnWorking && g_msiOwnedReturnFailed &&
            g_msiOwnedReturnRecovery.observations() == 20, "20 observations publish dormant work but retain responsibility");
        c.require(g_count.writes == 1 && g_msiOwnedReturnRecovery.retryUsed(), "window consumed exactly one retry"); c.owed();
        const auto before = counterJson(); const auto logs = g_logs.size();
        for (unsigned i = 0; i < 1000; ++i) maintain(30000 + i * 1000);
        c.require(counterJson() == before && g_logs.size() == logs,
            "dormant same-event calls do not lock, query roots, open commands, read clock, write or emit logs");
        // A valid but eventless cache arrival cannot wake dormant work. The
        // runtime only resumes on the existing topology/power event tokens.
        physical(); sample(101, 1030000); maintain(1030000); c.owed();
        c.require(counterJson() == before, "cache replacement alone does not create background dormant work");
        initial();
        c.require(g_msiOwnedReturnRecovery.observations() == 20 && !g_msiOwnedReturnWorking,
            "repeated OFF cannot rearm an exhausted observation window");
        if (powerEvent) ++g_power; else ++g_xinputTopologyEpoch;
        physical(); sample(102, 1031000); maintain(1031000); c.owed("first late valid event-bound physical sample cannot settle alone");
        c.require(g_msiOwnedReturnWorking && !g_msiOwnedReturnFailed && g_msiOwnedReturnRecovery.observations() == 1,
            "existing event reopens only bounded observation work");
        sample(103, 1032000); maintain(1032000); c.settled();
        c.require(g_count.writes == 1, "late valid physical signal settles without another retry");
    });
    c.run("dormant/late-source-event-reopens-observation", [&] {
        initial(); for (unsigned i = 0; i < 20; ++i) maintain(10000 + i * 1000);
        c.require(g_msiOwnedReturnPending && !g_msiOwnedReturnWorking && g_msiOwnedReturnFailed,
            "initial bounded observation window is exhausted with debt retained");
        c.require(g_count.writes == 1 && g_msiOwnedReturnRecovery.retryUsed(), "initial retry budget is already consumed");
        const auto topology = g_xinputTopologyEpoch.load(); const auto power = g_power;
        // Fake only the event published by the existing physical identity probe.
        // No device-tree topology change, new device API or observer is involved.
        physical(1); ++g_msiOwnedReturnSourceEvent; sample(101, 40000); maintain(40000);
        c.require(g_xinputTopologyEpoch == topology && g_power == power && g_msiOwnedReturnSourceEvent == 1,
            "physical source event alone wakes observation with topology and power unchanged");
        c.require(g_msiOwnedReturnWorking && !g_msiOwnedReturnFailed && g_msiOwnedReturnRecovery.observations() == 1,
            "real runtime due opens one fresh bounded observation window on the source token");
        c.owed("first late physical source read is evidence but cannot discharge alone");
        sample(102, 41000); maintain(41000); c.settled();
        c.require(g_count.writes == 1 && g_msiOwnedReturnRecovery.retryUsed(),
            "two late distinct physical reads settle without renewing or repeating mode1 retry");
    });
    c.run("dormant/same-source-ticks-no-extra-IO-or-retry-reset", [&] {
        initial(); for (unsigned i = 0; i < 20; ++i) maintain(10000 + i * 1000);
        ++g_msiOwnedReturnSourceEvent; maintain(40000);
        c.require(g_msiOwnedReturnWorking && g_msiOwnedReturnRecovery.observations() == 1,
            "one source event starts one bounded observation window");
        const auto activeCounters = counterJson(); const auto activeLogs = g_logs.size();
        for (unsigned i = 1; i < 1000; ++i) maintain(40000 + i);
        c.require(counterJson() == activeCounters && g_logs.size() == activeLogs && g_msiOwnedReturnRecovery.observations() == 1,
            "same source token and subsecond ticks return before locks, clock reads, command IO or logging");
        for (unsigned i = 1; i < 20; ++i) maintain(40000 + i * 1000);
        c.require(!g_msiOwnedReturnWorking && g_msiOwnedReturnFailed && g_msiOwnedReturnRecovery.observations() == 20,
            "unchanged source token cannot reset the reopened bounded window");
        const auto dormantCounters = counterJson(); const auto dormantLogs = g_logs.size();
        for (unsigned i = 0; i < 1000; ++i) maintain(60000 + i * 1000);
        c.require(counterJson() == dormantCounters && g_logs.size() == dormantLogs,
            "same source token stays dormant indefinitely with no further locks, queries, command IO or log work");
        c.require(g_msiOwnedReturnSourceEvent == 1 && g_count.writes == 1 && g_msiOwnedReturnRecovery.retryUsed(),
            "source-event observation never mutates the event or refunds the episode retry budget");
        c.owed();
    });
    c.run("dormant/events-without-input-never-renew-single-retry", [&] {
        initial(); for (unsigned i = 0; i < 20; ++i) maintain(10000 + i * 1000);
        ++g_xinputTopologyEpoch;
        for (unsigned i = 0; i < 20; ++i) maintain(40000 + i * 1000);
        ++g_power; maintain(70000);
        c.require(g_msiOwnedReturnRecovery.observations() == 1 && g_count.writes == 1 && g_msiOwnedReturnRecovery.retryUsed(),
            "multiple bounded observation windows retain original consumed write budget"); c.owed();
    });
    c.run("dormant/explicit-new-target-cancel-is-inert", [&] {
        initial(); for (unsigned i = 0; i < 20; ++i) maintain(10000 + i * 1000);
        g_realStickTest.virtualTargetEnabled = true; msiOwnedReturnCancel("new-target");
        const auto count = counterJson(); maintain(50000);
        c.require(!g_msiOwnedReturnPending && counterJson() == count, "explicit new-target cancellation stops dormant work before any lock"); c.owed();
    });
}
static void commandReadyBoundary(Checks& c) {
    c.run("prior-command/busy-preserves-budget-before-open",[&]{
        initial();g_commandReady=false;maintain(12000);maintain(13000);
        c.require(g_count.opens==0 && g_count.writes==0 && !g_msiOwnedReturnRecovery.retryUsed(),"older I/O cannot consume this return retry");c.owed();
        g_commandReady=true;maintain(14000);maintain(15000);
        c.require(g_count.writes==1 && g_msiOwnedReturnRecovery.retryUsed(),"settled old I/O permits the preserved retry exactly once");c.owed();
    });
    c.run("prior-command/physical-match-settles-even-when-command-busy",[&]{
        initial();g_commandReady=false;physical();sample(700,12000);maintain(12000);sample(701,13000);maintain(13000);
        c.settled();c.require(g_count.opens==0&&g_count.writes==0,"physical receipt does not need another command transaction");
    });
}
static void failedMode2ReturnDuty(Checks& c) {
    for(bool used:{false,true})for(bool earlySettingsCancel:{false,true})
    c.run(std::string("failed-mode2/new-OFF-resumes-")+(used?"used":"unused")+(earlySettingsCancel?"-settings-first":"-caller-first"),[&]{
        initial();if(used)maintain(12000);
        const auto writes=g_count.writes;const auto episode=g_msiDinputReturnDebtEpisode.load();
        g_realStickTest.virtualTargetEnabled=true;g_persona="steamdeck";
        if(earlySettingsCancel)maintain(14000);
        msiOwnedReturnCancel("new-dinput-request");msiOwnedReturnMode2Outcome(blocked());
        c.require(!g_msiOwnedReturnPending && g_msiDinputReturnOwed,"new mode request remains inactive while old physical debt survives");
        g_realStickTest.virtualTargetEnabled=false;g_persona="disabled";
        msiOwnedReturnObserveOffIntent(); // real explicit-OFF hook, no Host or visibility journal required
        c.require(g_msiOwnedReturnPending && g_msiOwnedReturnRecovery.retryUsed()==used &&
            g_msiDinputReturnDebtEpisode.load()==episode,"new OFF resumes only same debt and preserves consumed retry");
        physical();sample(500,20000);maintain(20000);sample(501,21000);maintain(21000);c.settled();
        c.require(g_count.writes==writes,"matching physical return is settled without another mode write");
    });
    c.run("failed-mode2/unused-retry-still-bounded",[&]{
        initial();g_realStickTest.virtualTargetEnabled=true;g_persona="steamdeck";
        msiOwnedReturnCancel("new-dinput-request");msiOwnedReturnMode2Outcome(blocked());
        g_realStickTest.virtualTargetEnabled=false;g_persona="disabled";
        msiOwnedReturnResumeAfterFailedMode2("input-host-stop");maintain(12000);maintain(13000);
        c.require(g_count.writes==1 && g_msiOwnedReturnRecovery.retryUsed(),"safe unspent original retry can be used exactly once");c.owed();
        msiOwnedReturnResumeAfterFailedMode2("input-host-stop");maintain(14000);
        c.require(g_count.writes==1,"repeated OFF gate cannot refresh a resumed retry");
    });
    c.run("failed-mode2/ambiguous-later-IO-reduces-permission",[&]{
        initial();g_realStickTest.virtualTargetEnabled=true;g_persona="steamdeck";
        msiOwnedReturnCancel("new-dinput-request");auto uncertain=disconnected();uncertain.write.timedOut=true;
        msiOwnedReturnMode2Outcome(uncertain);g_realStickTest.virtualTargetEnabled=false;g_persona="disabled";
        msiOwnedReturnResumeAfterFailedMode2("input-host-stop");maintain(12000);maintain(13000);
        c.require(g_count.writes==0 && !g_msiOwnedReturnRetrySafe,"uncertain takeover cannot retain a formerly safe write permission");
        physical();sample(600,20000);maintain(20000);sample(601,21000);maintain(21000);c.settled();
    });
    for(const char* denied:{"no-mode2-outcome","mode2-success","wrong-owner","wrong-root","different-episode","still-ON","exit-fence","other-terminal"})
    c.run(std::string("failed-mode2/no-resume-")+denied,[&]{
        initial();g_realStickTest.virtualTargetEnabled=true;g_persona="steamdeck";
        msiOwnedReturnCancel("new-dinput-request");
        const std::string why=denied;
        if(why!="no-mode2-outcome")msiOwnedReturnMode2Outcome(why=="mode2-success"?successful():blocked());
        g_realStickTest.virtualTargetEnabled=false;g_persona="disabled";
        if(why=="wrong-owner")++g_msiCrashRecord.ownerPid;
        if(why=="wrong-root"){g_msiCrashRecord.rootInstance="USB\\VID_0DB0&PID_1902\\OTHER";g_msiCrashRecord.locationPaths={"PCIROOT(9)#USBROOT(0)#USB(5)"};}
        if(why=="different-episode")++g_msiDinputReturnDebtEpisode;
        if(why=="still-ON")g_realStickTest.virtualTargetEnabled=true;
        if(why=="exit-fence")g_exitFence=true;
        msiOwnedReturnResumeAfterFailedMode2(why=="other-terminal"?"dinput-disabled":"input-host-stop");
        c.require(!g_msiOwnedReturnPending && g_count.writes==0,"resume requires failed takeover, same ownership and explicit valid OFF");c.owed();
    });
}
static void refreshedActualOutcome(Checks& c) {
    c.run("initial-refresh/late-ambiguous-result-reduces-permission-and-cancels-only-real-legacy-plan",[&]{
        initial();maintain(10000);const auto observations=g_msiOwnedReturnRecovery.observations();
        const auto oldCancels=g_count.terminalCancels;g_msiTerminalReturnPending=true;
        auto late=disconnected();late.write.timedOut=true;g_tick=11000;initial(late);
        c.require(!g_msiOwnedReturnRetrySafe && !g_msiTerminalReturnPending && g_count.terminalCancels==oldCancels+1,
            "late ambiguous result lowers permission and retires a newly pending legacy plan");
        c.require(g_msiOwnedReturnRecovery.observations()==observations && g_msiDinputReturnDebtEpisode==41,
            "same outcome refresh cannot allocate another debt or observation window");
        maintain(12000);c.require(g_count.writes==0 && !g_msiOwnedReturnRecovery.retryUsed(),
            "same duty with ambiguous later I/O cannot consume its formerly safe write retry");
    });
}

// Replay source-derived context around field N93 -> N147. The OS command
// outcome is literal field evidence; settings/Host transitions are fixture input.
static void onTerminal(const MsiClawCommandOutcome& outcome) {
    g_persona="steamdeck";g_realStickTest.virtualTargetEnabled=true;
    g_inputHostProcess=17;g_inputHostPrepared=g_inputHostActive=true;
    g_terminalOutcome=outcome;
    (void)msiClawRestoreXInputMode("dinput-route-window-exhausted");
}
static void explicitOff() {
    g_persona="disabled";g_realStickTest.virtualTargetEnabled=false;
    g_inputHostProcess=0;g_inputHostPrepared=g_inputHostActive=false;
    (void)msiClawRestoreXInputMode("input-host-stop");
    msiOwnedReturnObserveOffIntent();
}
static void offTerminalHandoff(Checks& c) {
    c.run("field-OFF/no-device-ON-terminal-must-handoff-to-existing-owner",[&]{
        auto noDevice=missingTransport();
        onTerminal(noDevice);
        c.require(g_msiMode1TerminalAttempted && g_terminalCalls==1 && !g_msiOwnedReturnPending,
            "actual ON fallback consumes terminal gate but cannot start OFF observation");
        explicitOff();
        c.require(g_terminalCalls==1,"actual OFF restore remains suppressed; terminal gate is never refunded");
        c.require(g_msiOwnedReturnPending && g_msiOwnedReturnWorking,
            "explicit OFF adopts the exact earlier no-device duty into its existing owner");
        maintain(12000);maintain(13000);
        c.require(g_count.writes==1 && g_msiOwnedReturnRecovery.retryUsed(),
            "only a live exact owned PID1902 permits the existing single return retry");
        c.owed();c.require(!inputHostDisabledCleanupProvenLocked(),"actual OFF proof rejects sent=true while physical return debt remains");
        const auto observations=g_msiOwnedReturnRecovery.observations();
        explicitOff();
        c.require(g_msiOwnedReturnRecovery.observations()==observations && g_count.writes==1,
            "repeated OFF cannot renew observations or refund the consumed retry");
        maintain(13500);c.require(g_count.writes==1 && g_msiOwnedReturnRecovery.retryUsed(),"repeated OFF followed by maintenance cannot issue a second retry");
        physical();sample(900,14000);maintain(14000);sample(901,15000);maintain(15000);
        c.settled();c.require(inputHostDisabledCleanupProvenLocked(),"actual OFF cleanup accepts only the discharged physical duty");
    });
    const std::vector<std::pair<std::string,MsiClawCommandOutcome>> unsafe={
        {"unknown-not-attempted",blocked()}, {"completed-write-receipt",successful()},
        {"incomplete-enumeration",[]{auto o=missingTransport();o.diag.enumerationComplete=false;return o;}()},
        {"enumeration-error",[]{auto o=missingTransport();o.diag.enumerationError=5;return o;}()},
        {"command-access-denied",[]{auto o=missingTransport();o.diag.openFailures=1;o.diag.lastError=5;return o;}()},
        {"attempted-disconnect",disconnected()},
        {"partial-write",[]{auto o=successful();o.write.ok=false;o.write.written=1;return o;}()},
        {"retained-no-device",[]{auto o=blocked();o.write.state=MsiClawIoState::NoDevice;o.write.handleRetained=true;return o;}()},
        {"timedout-no-device",[]{auto o=blocked();o.write.state=MsiClawIoState::NoDevice;o.write.timedOut=true;return o;}()},
    };
    for(const auto& row:unsafe)c.run("field-OFF/observe-only-"+row.first,[&]{
        onTerminal(row.second);explicitOff();maintain(12000);maintain(13000);
        c.require(g_msiOwnedReturnPending,"retained exact obligation remains observable after OFF");
        c.require(g_count.writes==0 && !g_msiOwnedReturnRecovery.retryUsed(),
            "handoff grants a write only for explicit settled NoDevice with no write attempt");
        c.owed();physical();sample(902,14000);maintain(14000);sample(903,15000);maintain(15000);
        c.settled();
    });

    for(const auto& row:unsafe)c.run("field-OFF/rebound-source-cannot-upgrade-"+row.first,[&]{
        onTerminal(row.second);
        g_reboundAvailable=true;g_targetAbsent=false;
        g_reboundSource={true,71,0x0db0,0x1902,W2U(kCommandPath),kOwnedRoot};
        explicitOff();maintain(12000);maintain(13000);
        c.require(g_msiOwnedReturnPending && !g_msiOwnedReturnRetrySafe,
            "device-facts-unproven OFF retains the actual earlier terminal outcome instead of upgrading default NotAttempted");
        c.require(g_count.writes==0 && g_reboundReleases==0 && !g_msiOwnedReturnRecovery.retryUsed(),
            "bound source and missing current outcome never upgrade an unknown or attempted old write");
        c.require(!inputHostDisabledCleanupProvenLocked(),"unknown physical duty cannot pass the actual OFF cleanup predicate");c.owed();
    });
    c.run("field-OFF/no-device-rebound-waits-for-retired-command-before-single-retry",[&]{
        auto noDevice=missingTransport();onTerminal(noDevice);
        g_reboundAvailable=true;g_targetAbsent=false;
        g_reboundSource={true,71,0x0db0,0x1902,W2U(kCommandPath),kOwnedRoot};
        g_commandReady=false;explicitOff();maintain(12000);maintain(13000);
        c.require(g_count.writes==0 && g_reboundReleases==0 && !g_msiOwnedReturnRecovery.retryUsed(),
            "prior retired command remains owned and cannot consume retry or close rebound DInput early");
        g_commandReady=true;maintain(14000);maintain(15000);
        c.require(g_count.writes==1 && g_reboundReleases==1 && g_reboundClears==1,
            "after older I/O settles only the exact rebound source is quiesced for one existing retry");c.owed();
    });

    const std::vector<std::pair<std::string,std::function<void()>>> changed={
        {"new-debt-episode",[]{++g_msiDinputReturnDebtEpisode;}},
        {"changed-root",[]{g_msiCrashRecord.rootInstance="USB\\VID_0DB0&PID_1902\\OTHER";}},
        {"changed-location",[]{g_msiCrashRecord.locationPaths={"PCIROOT(0)#USB(9)"};}},
        {"changed-transaction",[]{g_msiCrashRecord.transactionId="other";}},
        {"foreign-owner",[]{++g_msiCrashRecord.ownerPid;}},
        {"invalid-store",[]{g_msiCrashStoreInvalid=true;}},
    };
    for(const auto& row:changed)c.run("field-OFF/reject-handoff-"+row.first,[&]{
        auto noDevice=missingTransport();onTerminal(noDevice);
        row.second();explicitOff();maintain(12000);maintain(13000);
        c.require(!g_msiOwnedReturnPending && g_count.writes==0 && g_terminalCalls==1,
            "changed durable owner/root/location/transaction/episode cannot adopt the old outcome");
        c.owed();
    });
}

int main() {
    Checks c;
    classify(c); trackingAndReturn(c); invalidSignals(c); contextAndOwnership(c); foregroundIsolation(c); commandBoundaries(c); windowsAndDormancy(c); failedMode2ReturnDuty(c); commandReadyBoundary(c);refreshedActualOutcome(c);offTerminalHandoff(c);
    const json report = {{"passed", c.failures.empty()}, {"tests", c.cases.size()}, {"assertions", c.assertions},
        {"failures", c.failures}, {"noInputHost", true}, {"noSdkDeviceIo", true}, {"hardwareFakesOnly", true},
        {"realRuntimeFunctions", {"msiOwnedReturnRecordInitial", "msiOwnedReturnMaintenance", "msiOwnedReturnReadSignal",
            "msiOwnedReturnRetryable", "msiOwnedReturnRecordCurrent", "msiOwnedReturnContext", "msiOwnedReturnCancel",
             "msiOwnedReturnObserveOffIntent", "msiClawRestoreXInputMode", "msiClawFinishXinputRestore",
             "msiTerminalReturnMissingTransport", "inputHostDisabledCleanupProvenLocked"}},
        {"cases", c.cases}};
    std::cout << report.dump(2) << '\n';
    return c.failures.empty() ? 0 : 1;
}






