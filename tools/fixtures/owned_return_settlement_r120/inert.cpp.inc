// All OS/device/persistence leaves are inert. Production settlement definitions
// are inserted verbatim by the runner; no copied settlement implementation.
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
#include "msi_owned_return_recovery.h"
#include "msi_crash_recovery_journal.h"
using json = nlohmann::json;
namespace msiRecovery = ymcc::msi::recovery;
using ULONGLONG = unsigned long long;
using InputHostMutex = std::mutex;
using HANDLE = long long;
constexpr HANDLE INVALID_HANDLE_VALUE = -1;
struct MsiClawHidDiag { unsigned productId = 0; std::string interfacePath; };
struct MsiClawCommandOutcome { struct { bool ok = false; } write; };
struct { bool virtualTargetEnabled = false; } g_realStickTest;
static std::mutex g_inputHostMx, g_msiPersonaPumpMx, g_msiOwnedReturnMx, g_msiCrashMx;
static std::atomic<bool> g_msiOwnedReturnPending{false}, g_msiOwnedReturnWorking{false}, g_msiOwnedReturnFailed{false};
static std::atomic<bool> g_msiDinputReturnOwed{false}, g_msiDinputRouteDinputArmed{false};
static std::atomic<bool> g_msiNativeForegroundCyclePending{false}, g_msiNativeForegroundPowerHold{false};
static std::atomic<bool> g_inputHostExitFence{false}, g_externalPriorityVirtualSuspended{false};
static std::atomic<unsigned long long> g_msiDinputReturnDebtEpisode{0}, g_msiFeedbackModeGeneration{0};
static std::atomic<unsigned long long> g_inputHostVirtualIntentRevision{0}, g_msiMainSourceRevision{0};
static std::atomic<unsigned> g_inputHostVirtualIntentWriters{0}, g_xinputTopologyEpoch{0}, g_externalPrioritySourceMask{0};
static std::atomic<unsigned long long> g_msiOwnedReturnSourceEvent{0}, g_power{0}, g_hostEpoch{0};
static std::atomic<unsigned long long> g_msiOwnedReturnMatchedPower{0}, g_msiOwnedReturnMatchedEpisode{0};
static std::atomic<unsigned> g_msiOwnedReturnMatchedTopology{0};
static std::atomic<ULONGLONG> g_msiOwnedReturnObserveAfter{0}, g_msiDinputReturnDebtPendingLogTick{0};
static std::atomic<unsigned> g_msiOwnedReturnSeenTopology{0};
static std::atomic<unsigned long long> g_msiOwnedReturnSeenPower{0}, g_msiOwnedReturnSeenSourceEvent{0};
static bool g_inputHostProcess=false, g_inputHostPrepared=false, g_inputHostReleaseQuarantined=false, g_inputHostActive=false;
static bool g_msiOwnedReturnRetrySafe=false, g_msiCrashLoaded=false, g_msiCrashStoreInvalid=false;
static bool g_authorized=true, g_seam=false, g_hardware=true, g_foregroundBusy=false;
static std::string g_persona="disabled";
static unsigned long g_xinputConnectedMask=1, g_xinputPhysicalSourceMask=1;
static ymcc::msi::ownedReturn::Recovery g_msiOwnedReturnRecovery;
static msiRecovery::Record g_msiCrashRecord, g_msiOwnedReturnExpected;
static ULONGLONG g_now=0;
static json g_rows=json::array();
static std::string g_durableBytes;
static unsigned g_storeCalls=0, g_signalCalls=0, g_retryCalls=0;
static bool g_storeSuccess=true, g_stableAtStore=false;
static json g_lockProbe;
static std::function<void()> g_storeHook;
static std::function<void()> g_signalHook;
static bool msiClawKnownBoardAuthority() { return g_authorized; }
static bool ymStartSeamActive() { return g_seam; }
static bool hardwareWriteAllowed() { return g_hardware; }
static bool msiForegroundIsolationBorrowBusy() { return g_foregroundBusy; }
static std::string inputHostConfiguredPersona() { return g_persona; }
static unsigned long long currentPowerGeneration() { return g_power.load(); }
static unsigned long long inputHostEpochToken() { return g_hostEpoch.load(); }
static std::string inputHostOwnerToken() { return "fixture-owner"; }
static ULONGLONG GetTickCount64() { return g_now; }
static unsigned GetCurrentProcessId() { return 42; }
static bool msiDinputDeviceTargetAbsent() { return true; }
static void appendNativeLifecycleLog(const char* type, json row) { row["event"]=type; g_rows.push_back(std::move(row)); }
static void msiOwnedReturnNote(const char* action, const char* reason, const MsiClawCommandOutcome* =nullptr) {
    appendNativeLifecycleLog("fixture-note", {{"action",action},{"reason",reason}});
}
static std::wstring msiCrashRecoveryPath() { return L"fixture-memory-journal"; }
static void msiCrashNote(const char* phase, const char* reason, bool persisted=false) {
    appendNativeLifecycleLog("fixture-crash-note", {{"phase",phase},{"reason",reason},{"persisted",persisted}});
}
static bool fixtureCapturedContextCurrent();
static bool sgWriteFileAtomic(const std::wstring&, const std::string& content) {
    ++g_storeCalls;
    g_stableAtStore=fixtureCapturedContextCurrent();
    // This actor uses ONLY independently mutable atomics. It cannot take any
    // lifecycle/durable lock or overwrite the journal while its mutex is held.
    std::exception_ptr error;
    std::thread actor([&] {
        try {
            auto held=[](std::mutex& mx) { if(mx.try_lock()) {mx.unlock();return false;} return true; };
            g_lockProbe={{"hostHeld",held(g_inputHostMx)}, {"pumpHeld",held(g_msiPersonaPumpMx)},
                {"ownedHeld",held(g_msiOwnedReturnMx)}, {"durableHeld",held(g_msiCrashMx)}};
            if(g_storeHook)g_storeHook();
        } catch(...) { error=std::current_exception(); }
    });
    actor.join();
    if(error)std::rethrow_exception(error);
    if(!g_storeSuccess)return false;
    g_durableBytes=content; // sole simulated commit; no filesystem/device call
    return true;
}
static bool msiOwnedReturnCommandReady() { throw std::runtime_error("out-of-scope command gate reached"); }
static HANDLE msiClawOpenModeCommand(MsiClawHidDiag&) { throw std::runtime_error("out-of-scope device open reached"); }
static void msiClawCloseModeCommand(HANDLE) { throw std::runtime_error("out-of-scope close reached"); }
static bool deviceInterfaceUsbInstance(const wchar_t*, std::wstring&) { throw std::runtime_error("out-of-scope identity query reached"); }
static std::wstring U2W(const std::string&) { throw std::runtime_error("out-of-scope conversion reached"); }
static std::string W2U(const std::wstring&) { throw std::runtime_error("out-of-scope conversion reached"); }
static std::vector<std::string> msiCrashLocations(const std::string&) { throw std::runtime_error("out-of-scope locations reached"); }
static bool msiOwnedReturnQuiesceReboundDinput(unsigned long long,unsigned long long,unsigned,unsigned long long,unsigned long long) {
    throw std::runtime_error("out-of-scope source release reached");
}
template<class Guard> static MsiClawCommandOutcome msiClawSwitchModeOnHandle(unsigned,HANDLE,const MsiClawHidDiag&,bool,const Guard&) {
    ++g_retryCalls;throw std::runtime_error("out-of-scope mode write reached");
}
static void msiNoteDisruptiveTx(const char*) { throw std::runtime_error("out-of-scope disruptive tx reached"); }

