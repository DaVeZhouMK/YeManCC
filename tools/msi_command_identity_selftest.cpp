// Source-exact command selection and durable mode admission fixture.
// The runner supplies unchanged production declarations/bodies. OS/HID/storage
// boundaries below are in-memory fakes; this executable never opens a device.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
#include "msi_crash_recovery_journal.h"
#ifdef YMCC_FAULT_INJECT
#error This fixture must exercise production gates, never fault-injection bypasses.
#endif
using json = nlohmann::json;
namespace msiRecovery = ymcc::msi::recovery;
#include "msi_command_identity_types.h"

enum class YmccFamily { Unknown, MsiClaw };
struct MachineIdentity {
    YmccFamily family = YmccFamily::MsiClaw;
    std::string manufacturer = "MICRO-STAR INTERNATIONAL CO., LTD.";
    std::string product = "MS-1T8K";
    std::string systemProduct = "Fixture G3E";
    std::string deviceClass = "FixtureClaw";
};
static MachineIdentity g_machineIdentity;
struct Endpoint {
    std::wstring path;
    USHORT vid = 0x0db0, pid = 0x1902, page = 0xfff0, usage = 0x0040;
    bool open = true, attributes = true, preparsed = true, caps = true;
};
struct Fixture {
    bool trace = false, rootQueryOk = true, locationsKnown = true;
    bool persistenceOk = true, storeInvalid = false, writable = true;
    bool exitFence = false, targetAbsent = true, oldOwnerDead = true;
    bool changeEpochOnPersist = false, changePowerOnPersist = false;
    bool fenceOnPersist = false;
    bool changeTopologyOnPersist=false,changeSourceEventOnPersist=false;
    unsigned long long power = 7, epoch = 11;
    DWORD lastError = 0;
    unsigned setupCalls = 0, destroyCalls = 0, rootQueries = 0;
    unsigned opens = 0, closes = 0, writeCalls = 0, persistCalls = 0;
    unsigned commits = 0, borrowedHandles = 0;
    std::vector<Endpoint> endpoints;
    std::vector<std::string> events, queryPaths, selectedPaths;
    std::vector<json> writes;
    std::string root = "USB\\VID_0DB0&PID_1902\\FIXTURE_ROOT";
    std::string location = "PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4)";
    std::string decision;
    msiRecovery::Record durable;
    json modeLog;
};
static Fixture f;
static std::mutex g_msiCrashMx;
static msiRecovery::Record g_msiCrashRecord;
static bool g_msiCrashStoreInvalid = false;
static bool g_msiCrashLoaded = false;
static std::atomic<bool> g_msiCrashInheritedPending{false};
static bool g_msiCrashStartupAttempted = false;
static std::atomic<bool> g_msiDinputReturnOwed{false};
static std::atomic<bool> g_msiDinputRouteDinputArmed{true};
static std::atomic<bool> g_msiMode1TerminalAttempted{false};
// Current production evidence fields; no controller I/O or admission authority.
static std::atomic<unsigned long long> g_msiDinputReturnDebtEpisode{0},g_msiDinputReturnDebtPendingLogTick{0};
static DWORD g_xinputConnectedMask=0,g_xinputPhysicalSourceMask=0;
static std::atomic<bool> g_externalPriorityVirtualSuspended{false};
static std::atomic<DWORD> g_externalPrioritySourceMask{0};
static std::atomic<unsigned long long> g_msiFeedbackModeGeneration{1};
static std::atomic<unsigned> g_xinputTopologyEpoch{1};
static std::atomic<unsigned long long> g_msiOwnedReturnSourceEvent{0};
struct ReturnIntentFixture { bool virtualTargetEnabled=false; };
static ReturnIntentFixture g_realStickTest;
static std::atomic<bool> g_msiNativeForegroundCyclePending{false},g_msiNativeForegroundPowerHold{false};
static bool g_returnBorrowBusy=false;
static bool msiForegroundIsolationBorrowBusy() { return g_returnBorrowBusy; }
static std::string inputHostConfiguredPersona() { return g_realStickTest.virtualTargetEnabled?"steamdeck":"disabled"; }
static constexpr USHORT kMsiClawVendorId = 0x0db0;
static constexpr USHORT kMsiClawPidXInput = 0x1901;
static constexpr USHORT kMsiClawPidDInput = 0x1902;
static const GUID kYeManGuidDeviceInterfaceHid{};
static std::string W2U(const std::wstring& s) {
    std::string out;for(wchar_t c:s){if(static_cast<unsigned>(c)>127)throw std::runtime_error("fixture requires ASCII identity");out.push_back(static_cast<char>(c));}return out;
}
static std::wstring U2W(const std::string& s) { return {s.begin(), s.end()}; }
static HANDLE endpointHandle(size_t i) { return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(i + 0x100)); }
static Endpoint& endpoint(HANDLE h) {
    const auto i = reinterpret_cast<uintptr_t>(h) - 0x100;
    if (i >= f.endpoints.size()) throw std::runtime_error("non-fixture HID handle");
    return f.endpoints[i];
}
static HDEVINFO fixtureGetClassDevs(const GUID*, PCWSTR, HWND, DWORD) {
    ++f.setupCalls; return reinterpret_cast<HDEVINFO>(static_cast<uintptr_t>(0x55));
}
static BOOL fixtureEnumInterfaces(HDEVINFO, PSP_DEVINFO_DATA, const GUID*, DWORD index, PSP_DEVICE_INTERFACE_DATA out) {
    if (index >= f.endpoints.size()) { f.lastError = ERROR_NO_MORE_ITEMS; return FALSE; }
    out->Reserved = index; return TRUE;
}
static BOOL fixtureGetDetail(HDEVINFO, PSP_DEVICE_INTERFACE_DATA iface, PSP_DEVICE_INTERFACE_DETAIL_DATA_W out,
                             DWORD bytes, PDWORD required, PSP_DEVINFO_DATA data) {
    const size_t i = static_cast<size_t>(iface->Reserved);
    if (i >= f.endpoints.size()) throw std::runtime_error("bad fixture interface");
    const auto& path = f.endpoints[i].path;
    const auto needed = static_cast<DWORD>(offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath) + (path.size()+1)*sizeof(wchar_t));
    if (required) *required = needed;
    if (!out || bytes < needed) { f.lastError = ERROR_INSUFFICIENT_BUFFER; return FALSE; }
    std::memcpy(out->DevicePath, path.c_str(), (path.size()+1)*sizeof(wchar_t));
    if (data) data->DevInst = static_cast<DWORD>(i);
    return TRUE;
}
static BOOL fixtureDestroySet(HDEVINFO) { ++f.destroyCalls; return TRUE; }
static HANDLE fixtureCreateFile(PCWSTR path, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) {
    ++f.opens;
    for (size_t i=0;i<f.endpoints.size();++i) if (f.endpoints[i].path == path) {
        if (!f.endpoints[i].open) { f.lastError=ERROR_ACCESS_DENIED; return INVALID_HANDLE_VALUE; }
        ++f.borrowedHandles; return endpointHandle(i);
    }
    throw std::runtime_error("real or unknown CreateFile path refused");
}
static BOOLEAN fixtureAttributes(HANDLE h, PHIDD_ATTRIBUTES out) {
    auto& e=endpoint(h); if (!e.attributes) return FALSE;
    out->VendorID=e.vid;out->ProductID=e.pid;out->VersionNumber=0x101;return TRUE;
}
static BOOLEAN fixturePreparsed(HANDLE h, PHIDP_PREPARSED_DATA* out) {
    if (!endpoint(h).preparsed) return FALSE;
    *out=reinterpret_cast<PHIDP_PREPARSED_DATA>(h);return TRUE;
}
static NTSTATUS fixtureCaps(PHIDP_PREPARSED_DATA prep, PHIDP_CAPS out) {
    auto& e=endpoint(reinterpret_cast<HANDLE>(prep));
    if (!e.caps) return HIDP_STATUS_INVALID_PREPARSED_DATA;
    out->UsagePage=e.page;out->Usage=e.usage;out->OutputReportByteLength=64;return HIDP_STATUS_SUCCESS;
}
static BOOLEAN fixtureFreePreparsed(PHIDP_PREPARSED_DATA) { return TRUE; }
static BOOL fixtureClose(HANDLE h) {
    (void)endpoint(h);++f.closes;
    if (!f.borrowedHandles) throw std::runtime_error("double close");
    --f.borrowedHandles;return TRUE;
}
static DWORD fixtureLastError() { return f.lastError; }
static DWORD fixtureProcessId() { return 4242; }
static HANDLE fixtureCurrentProcess() { return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x22)); }
static BOOL fixtureProcessTimes(HANDLE, LPFILETIME creation, LPFILETIME, LPFILETIME, LPFILETIME) {
    creation->dwLowDateTime=123456;creation->dwHighDateTime=0;return TRUE;
}
#define SetupDiGetClassDevsW fixtureGetClassDevs
#define SetupDiEnumDeviceInterfaces fixtureEnumInterfaces
#define SetupDiGetDeviceInterfaceDetailW fixtureGetDetail
#define SetupDiDestroyDeviceInfoList fixtureDestroySet
#define CreateFileW fixtureCreateFile
#define HidD_GetAttributes fixtureAttributes
#define HidD_GetPreparsedData fixturePreparsed
#define HidP_GetCaps fixtureCaps
#define HidD_FreePreparsedData fixtureFreePreparsed
#define CloseHandle fixtureClose
#define GetLastError fixtureLastError
#define GetCurrentProcessId fixtureProcessId
#define GetCurrentProcess fixtureCurrentProcess
#define GetProcessTimes fixtureProcessTimes

// Explicit environmental / OS / transport / durable-storage boundaries.
static bool msiControllerTraceEnabled() { return f.trace; }
static void msiControllerTraceLog(const char*,const json&,bool) {}
static void appendNativeLifecycleLog(const char* event,const json& row) {
    if (std::string(event)=="msi-claw-gamepad-mode") f.modeLog=row;
}
static unsigned long long currentPowerGeneration() { return f.power; }
static unsigned long long inputHostEpochToken() { return f.epoch; }
static std::string inputHostOwnerToken() { return "fixture-owner"; }
static bool inputHostExitFenceRaised() { return f.exitFence; }
static bool hardwareWriteAllowed() { return f.writable; }
static bool msiDinputDeviceTargetAbsent() { return f.targetAbsent; }
static void ymccResolveMachineIdentityOnce() {}
static void msiTerminalReturnCancel(const char*) {}
static void msiTerminalReturnSchedule(const char*,const MsiClawCommandOutcome&) {}
// Independent command-admission fixture; the real return runtime has its own integration suite.
static void msiOwnedReturnCancel(const char*) {}
static void msiOwnedReturnRecordInitial(const char*,const MsiClawCommandOutcome&) {}
static void msiOwnedReturnMode2Outcome(const MsiClawCommandOutcome&) {}
static void msiOwnedReturnResumeAfterFailedMode2(const char*) {}
static void msiControllerProbeBeforeRearm(const char*) {}
static void msiNoteDisruptiveTx(const char*) {}
static void msiCrashNote(const char*,const char* reason,bool = false) { f.decision=reason; }
static void msiCrashDecision(const char* reason) { f.decision=reason; }
static void msiCrashLoadLocked() { g_msiCrashLoaded=true;g_msiCrashStoreInvalid=f.storeInvalid; }
static bool msiCrashOldOwnerDead(const msiRecovery::Record&) { return f.oldOwnerDead; }
static std::wstring app_data_dir() { return L"FIXTURE_ONLY"; }
static bool deviceInterfaceUsbInstance(PCWSTR path,std::wstring& root) {
    ++f.rootQueries;f.queryPaths.push_back(W2U(path));
    if (!f.rootQueryOk || !path || !*path) return false;
    const auto it=std::find_if(f.endpoints.begin(),f.endpoints.end(),[&](const auto& e){return e.path==path;});
    if (it==f.endpoints.end()) return false;
    root=U2W(f.root);return true;
}
static std::vector<std::wstring> msiForegroundIsolationLocations(const std::string& root) {
    return f.locationsKnown && root==f.root ? std::vector<std::wstring>{U2W(f.location)} : std::vector<std::wstring>{};
}
static bool sgWriteFileAtomic(const std::wstring& path,const std::string& text) {
    if(path!=L"FIXTURE_ONLY\\msi-controller-return.json")throw std::runtime_error("unexpected persistent path");
    ++f.persistCalls;f.events.push_back("persist-attempt");
    if(!f.persistenceOk)return false;
    const auto j=json::parse(text);f.durable.modeReturnPending=j.at("modeReturnPending").get<bool>();
    f.events.push_back("persist-commit");++f.commits;
    if(f.changeEpochOnPersist)++f.epoch;
    if(f.changePowerOnPersist)++f.power;
    if(f.fenceOnPersist)f.exitFence=true;
    if(f.changeTopologyOnPersist)++g_xinputTopologyEpoch;
    if(f.changeSourceEventOnPersist)++g_msiOwnedReturnSourceEvent;
    return true;
}
static MsiClawWriteResult msiClawCommandWriteBounded(HANDLE h,const BYTE* bytes,size_t size,const std::function<bool()>& contextCurrent=std::function<bool()>()) {
    MsiClawWriteResult result;
    if(contextCurrent && !contextCurrent()){result.state=MsiClawIoState::StartFailed;result.lastError=ERROR_WRITE_FAULT;return result;}
    if(h==INVALID_HANDLE_VALUE){result.state=MsiClawIoState::NoDevice;return result;}
    (void)endpoint(h);
    if(size!=64 || bytes[0]!=15 || bytes[3]!=60 || bytes[4]!=36 || (bytes[5]!=1 && bytes[5]!=2))
        throw std::runtime_error("wrong actual production SwitchMode frame");
    if(!f.commits)throw std::runtime_error("write without durable commit");
    ++f.writeCalls;f.events.push_back("hid-mode"+std::to_string(bytes[5]));
    f.writes.push_back({{"mode",bytes[5]},{"path",W2U(endpoint(h).path)},{"bytes",std::vector<BYTE>(bytes,bytes+size)}});
    result.attempted=true;result.ok=true;result.written=static_cast<DWORD>(size);
    result.state=MsiClawIoState::Completed;result.writeFileReturned=true;result.writeFileSucceeded=true;
    return result;
}
#include "msi_command_identity_body.h"

static json checks=json::array();
static json observations=json::array();
static void check(const std::string& name,bool pass) { checks.push_back({{"name",name},{"pass",pass}}); }
static Endpoint goodEndpoint() { return {L"\\\\?\\hid#vid_0db0&pid_1902&mi_01&col03#FIXTURE_COMMAND"}; }
static void reset(bool trace) {
    f=Fixture{};f.trace=trace;f.endpoints.push_back(goodEndpoint());g_machineIdentity={};
    g_msiCrashRecord={};g_msiCrashStoreInvalid=false;g_msiCrashLoaded=false;g_msiCrashInheritedPending=false;
    g_msiCrashStartupAttempted=false;g_msiDinputReturnOwed=false;
    g_msiDinputRouteDinputArmed=true;g_msiMode1TerminalAttempted=false;
    g_xinputTopologyEpoch=1;g_msiOwnedReturnSourceEvent=0;
}
static void seededDuty() {
    g_msiCrashRecord.boardKey=msiCrashBoardKey();g_msiCrashRecord.ownerPid=4242;
    g_msiCrashRecord.ownerCreationTime="123456";g_msiCrashRecord.transactionId="fixture-transaction";
    g_msiCrashRecord.rootInstance=f.root;g_msiCrashRecord.locationPaths={f.location};
    g_msiCrashRecord.modeReturnPending=true;g_msiDinputReturnOwed=true;
}
static json semanticSnapshot() {
    return {{"writes",f.writes},{"queryPaths",f.queryPaths},{"events",f.events},
        {"record",msiRecovery::encodeRecord(g_msiCrashRecord)},{"returnOwed",g_msiDinputReturnOwed.load()},
        {"pending",g_msiCrashRecord.modeReturnPending},{"terminalAttempted",g_msiMode1TerminalAttempted.load()}};
}
static json runRoundTrip(bool trace) {
    reset(trace);
    // Include rejected HID collections before the selected command endpoint.
    auto wrong=goodEndpoint();wrong.path=L"\\\\?\\hid#vid_0db0&pid_1902#WRONG_COLLECTION";wrong.usage=1;
    f.endpoints.insert(f.endpoints.begin(),wrong);
    const bool sent=msiClawRequestDInputMode("fixture-mode2");
    const std::string tag=trace?"trace-on":"trace-off";
    check(tag+"-mode2-receipt",sent);
    check(tag+"-mode2-actual-write-receipt",f.modeLog.value("writeAttempted",false) && f.modeLog.value("ioState",std::string{})=="completed");
    check(tag+"-selected-command-path-used-for-root",f.queryPaths.size()==1 && f.queryPaths[0]==W2U(goodEndpoint().path));
    check(tag+"-persist-precedes-mode2",f.events==std::vector<std::string>{"persist-attempt","persist-commit","hid-mode2"});
    check(tag+"-mode2-owns-duty",g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending);
    // The fixture supplies local target absence only, never real device acceptance.
    g_msiDinputRouteDinputArmed=false;f.exitFence=true;
    const bool returned=msiClawRestoreXInputMode("input-host-stop");
    check(tag+"-owned-mode1-sent",returned && f.writeCalls==2);
    check(tag+"-mode1-receipt-does-not-discharge",g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending);
    check(tag+"-one-durable-mode1-claim",g_msiCrashRecord.modeAttemptOwner==msiCrashCurrentOwner());
    const auto writes=f.writeCalls;
    check(tag+"-repeat-mode1-denied",!msiClawRestoreXInputMode("input-host-stop") && f.writeCalls==writes);
    check(tag+"-all-selected-handles-closed",f.borrowedHandles==0 && f.destroyCalls==f.setupCalls);
    const auto before=semanticSnapshot();
    check(tag+"-no-device-proof-keeps-debt",!msiDinputTryDischargeReturnDebt(false,false,0,"fixture-no-proof"));
    check(tag+"-explicit-qualified-fact-discharges-owned-only",msiDinputTryDischargeReturnDebt(false,true,1,"fixture-qualified-fact") && !g_msiDinputReturnOwed && !g_msiCrashRecord.modeReturnPending);
    check(tag+"-second-discharge-without-duty-denied",!msiDinputTryDischargeReturnDebt(true,true,1,"fixture-no-duty"));
    return before;
}
// Only the return reconciler owns the single-use budget. These checks cover
// the real lower durable gate after that caller has consumed its retry ticket.
static void ownedRetryAdmissionCases() {
    reset(false);seededDuty();g_msiDinputRouteDinputArmed=false;
    MsiClawHidDiag diag;diag.interfacePath=W2U(goodEndpoint().path);
    check("retry-admission/first-owned-mode1-claim",msiCrashRecoveryBeforeModeWrite(1,diag));
    check("retry-admission/default-caller-still-cannot-repeat",!msiCrashRecoveryBeforeModeWrite(1,diag));
    check("retry-admission/explicit-same-owner-return-ticket-accepted",msiCrashRecoveryBeforeModeWrite(1,diag,true));
    check("retry-admission/claim-and-send-never-clear-mode-duty",g_msiCrashRecord.modeReturnPending&&g_msiDinputReturnOwed&&f.writeCalls==0);
    reset(false);seededDuty();g_msiDinputRouteDinputArmed=false;
    g_msiCrashRecord.rootInstance="USB\\VID_0DB0&PID_1902\\OTHER_ROOT";
    g_msiCrashRecord.locationPaths={"PCIROOT(9)#USBROOT(1)#USB(2)"};
    check("retry-admission/foreign-root-cannot-use-ticket",!msiCrashRecoveryBeforeModeWrite(1,diag,true)&&f.writeCalls==0);
    reset(false);seededDuty();g_msiDinputRouteDinputArmed=false;
    g_msiCrashRecord.ownerPid=8888;f.oldOwnerDead=false;
    check("retry-admission/live-other-owner-cannot-be-bypassed",!msiCrashRecoveryBeforeModeWrite(1,diag,true)&&f.writeCalls==0);
    reset(false);seededDuty();g_msiDinputRouteDinputArmed=false;f.storeInvalid=true;
    check("retry-admission/invalid-durable-store-still-denies",!msiCrashRecoveryBeforeModeWrite(1,diag,true)&&f.writeCalls==0);
    reset(false);seededDuty();g_msiDinputRouteDinputArmed=false;g_realStickTest.virtualTargetEnabled=true;
    check("retry-admission/new-ON-intent-still-denies",!msiCrashRecoveryBeforeModeWrite(1,diag,true)&&f.writeCalls==0);
    g_realStickTest.virtualTargetEnabled=false;
    for(int guard=0;guard<3;++guard){
        reset(false);seededDuty();g_msiDinputRouteDinputArmed=false;
        g_msiNativeForegroundCyclePending=guard==0;g_msiNativeForegroundPowerHold=guard==1;g_returnBorrowBusy=guard==2;
        check("retry-admission/shield-cycle-or-power-or-borrow-blocks-"+std::to_string(guard),
            !msiCrashRecoveryBeforeModeWrite(1,diag,true)&&f.writeCalls==0);
    }
    g_msiNativeForegroundCyclePending=false;g_msiNativeForegroundPowerHold=false;g_returnBorrowBusy=false;
    for(int edge=0;edge<2;++edge){
        reset(false);seededDuty();g_msiDinputRouteDinputArmed=false;
        f.changeTopologyOnPersist=edge==0;f.changeSourceEventOnPersist=edge==1;
        check("retry-admission/topology-or-source-event-after-durable-denies-"+std::to_string(edge),
            !msiCrashRecoveryBeforeModeWrite(1,diag,true) && f.commits==1 && f.writeCalls==0 &&
            g_msiCrashRecord.modeReturnPending && g_msiDinputReturnOwed);
    }
}
static void negativeCases(bool trace) {
    const std::string t=trace?"trace-on/":"trace-off/";
    auto zero=[&](const char* name){check(t+name,f.writeCalls==0 && f.borrowedHandles==0);};
    reset(trace);f.endpoints.clear();check(t+"no-device-result",!msiClawRequestDInputMode("fixture"));zero("no-device-zero-write");
    reset(trace);f.endpoints[0].usage=1;check(t+"wrong-collection-result",!msiClawRequestDInputMode("fixture"));zero("wrong-collection-zero-write");
    reset(trace);f.endpoints[0].vid=0x1234;check(t+"wrong-vendor-result",!msiClawRequestDInputMode("fixture"));zero("wrong-vendor-zero-write");
    reset(trace);f.endpoints[0].pid=0x1903;check(t+"testing-pid-result",!msiClawRequestDInputMode("fixture"));zero("testing-pid-zero-write");
    reset(trace);f.endpoints[0].open=false;check(t+"access-denied-result",!msiClawRequestDInputMode("fixture"));zero("access-denied-zero-write");
    reset(trace);g_machineIdentity.product="UNKNOWN-BOARD";(void)msiClawRequestDInputMode("fixture");(void)msiClawRestoreXInputMode("input-host-stop");zero("unknown-board-zero-write");check(t+"unknown-board-real-authority-gate",!msiClawKnownBoardAuthority() && f.setupCalls==0);
    reset(trace);f.rootQueryOk=false;check(t+"root-query-result",!msiClawRequestDInputMode("fixture"));zero("root-query-failure-zero-write");check(t+"root-query-specific-refusal",f.decision=="mode-write-interface-root-query-failed");
    reset(trace);f.root="USB\\VID_1234&PID_5678\\ALIEN";(void)msiClawRequestDInputMode("fixture");zero("invalid-root-zero-write");
    reset(trace);f.locationsKnown=false;(void)msiClawRequestDInputMode("fixture");zero("missing-location-zero-write");
    reset(trace);seededDuty();g_msiCrashRecord.rootInstance="USB\\VID_0DB0&PID_1902\\OTHER_ROOT";g_msiCrashRecord.locationPaths={"PCIROOT(0)#USBROOT(1)#USB(2)"};(void)msiClawRequestDInputMode("fixture");zero("owned-root-and-location-mismatch-zero-write");
    reset(trace);seededDuty();g_msiCrashRecord.ownerPid=8888;f.oldOwnerDead=false;(void)msiClawRequestDInputMode("fixture");zero("old-owner-not-dead-zero-write");
    reset(trace);f.persistenceOk=false;(void)msiClawRequestDInputMode("fixture");zero("persistence-failure-zero-write");check(t+"failed-store-does-not-claim",!g_msiCrashRecord.modeReturnPending && !g_msiDinputReturnOwed);
    reset(trace);f.storeInvalid=true;(void)msiClawRequestDInputMode("fixture");zero("invalid-store-zero-write");
    reset(trace);f.changeEpochOnPersist=true;(void)msiClawRequestDInputMode("fixture");zero("epoch-changed-after-store-zero-write");
    reset(trace);f.changePowerOnPersist=true;(void)msiClawRequestDInputMode("fixture");zero("power-changed-after-store-zero-write");
    reset(trace);f.fenceOnPersist=true;(void)msiClawRequestDInputMode("fixture");zero("exit-fence-after-store-zero-write");
    reset(trace);f.writable=false;(void)msiClawRequestDInputMode("fixture");zero("hardware-write-fact-false-zero-write");
    reset(trace);f.exitFence=true;g_msiDinputRouteDinputArmed=false;check(t+"mode1-no-duty-denied",!msiClawRestoreXInputMode("input-host-stop"));zero("mode1-no-duty-zero-write");check(t+"no-duty-no-discharge",!msiDinputTryDischargeReturnDebt(true,true,1,"fixture") && f.persistCalls==0);
    reset(trace);seededDuty();f.exitFence=true;check(t+"mode1-route-armed-denied",!msiClawRestoreXInputMode("input-host-stop"));zero("mode1-route-armed-zero-write");
    reset(trace);seededDuty();g_msiDinputRouteDinputArmed=false;f.targetAbsent=false;check(t+"mode1-device-fact-unproven-denied",!msiClawRestoreXInputMode("input-host-stop"));zero("mode1-device-fact-unproven-zero-write");
    reset(trace);seededDuty();g_msiDinputRouteDinputArmed=false;check(t+"mode1-nonterminal-denied",!msiClawRestoreXInputMode("fixture-nonterminal"));zero("mode1-nonterminal-zero-write");
    reset(trace);seededDuty();g_msiDinputRouteDinputArmed=false;f.exitFence=true;f.rootQueryOk=false;check(t+"mode1-root-query-failure-denied",!msiClawRestoreXInputMode("input-host-stop"));zero("mode1-root-query-failure-zero-write");check(t+"mode1-root-failure-no-claim",f.persistCalls==0);
    reset(trace);seededDuty();g_msiDinputRouteDinputArmed=false;f.exitFence=true;f.persistenceOk=false;check(t+"mode1-persist-failure-denied",!msiClawRestoreXInputMode("input-host-stop"));zero("mode1-persist-failure-zero-write");
}
int main() {
    try {
        check("no-fault-seam",!ymStartSeamActive());
        if (R53_EXPECT_OLD) {
            for(int repeat=0;repeat<3;++repeat)for(bool trace:{false,true}) {
                reset(trace);const bool sent=msiClawRequestDInputMode("fixture-old-regression");
                const auto tag=std::string("old/repeat-")+std::to_string(repeat)+(trace?"/on":"/off");
                observations.push_back({{"repeat",repeat},{"trace",trace},{"sent",sent},
                    {"consumerPath",f.queryPaths.empty()?std::string{}:f.queryPaths.back()},
                    {"writeAttempted",f.modeLog.value("writeAttempted",false)},
                    {"ioState",f.modeLog.value("ioState",std::string{})},{"hidWrites",f.writeCalls},{"durableCommits",f.commits}});
                check(tag+"-old-outcome",sent==trace);
                check(tag+"-actual-writeAttempted",f.modeLog.value("writeAttempted",!trace)==trace);
                check(tag+"-actual-io-state",f.modeLog.value("ioState",std::string{})==(trace?"completed":"start-failed"));
                check(tag+"-actual-path-consumer",f.queryPaths.size()==1 && f.queryPaths[0]==(trace?W2U(goodEndpoint().path):""));
                check(tag+"-write-count",f.writeCalls==(trace?1u:0u));
                check(tag+"-durable-count",f.commits==(trace?1u:0u));
                check(tag+"-specific-old-refusal",trace || f.decision=="mode-write-interface-root-query-failed");
                check(tag+"-closed",f.borrowedHandles==0);
            }
            // Old trace-on follows the same production gate negatives.
            negativeCases(true);
        } else {
            const auto off=runRoundTrip(false),on=runRoundTrip(true);
            check("trace-toggle-identical-mode-and-return-behavior",off==on);
            negativeCases(false);negativeCases(true);
            ownedRetryAdmissionCases();
        }
    } catch(const std::exception& e) {check(std::string("unexpected-exception:")+e.what(),false);}
    unsigned failed=0;for(const auto& c:checks)if(!c.at("pass").get<bool>())++failed;
    std::cout<<json({{"variant",R53_EXPECT_OLD?"frozen-old":"current-fixed"},{"checks",checks},{"observations",observations},{"failures",failed},
        {"realDeviceAccess",false},{"realPersistenceWrites",false},{"faultInjectionDefined",false},
        {"realDeviceAcceptanceProven",false},{"qualifiedReturnFact","synthetic input only; not OS/game/Steam evidence"}}).dump(2)<<'\n';
    return failed?1:0;
}
