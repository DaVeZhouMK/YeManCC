// Offline execution of the production runtime; every external operation is replaced.
#define NOMINMAX
#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <xinput.h>
// Define USB interface GUIDs locally so checked builds need no device library.
#include <initguid.h>
#include <usbioctl.h>
#include <usbiodef.h>
#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <thread>
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include "../native/external_controller_priority_ledger.h"
#include "../native/external_controller_priority_policy.h"
using json = nlohmann::json;
namespace fspath = std::filesystem;
namespace ep = ymcc::externalpad;
namespace offline {
int assertions = 0, passed = 0, total = 0;
void require(bool value, const char* expression, int line) {
    ++assertions;
    if (!value) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
}
#define CHECK(...) offline::require(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __LINE__)
std::string W2U(const std::wstring& s) { std::string out; for (auto c : s) { CHECK(c >= 0 && c < 128); out.push_back(static_cast<char>(c)); } return out; }
std::wstring U2W(const std::string& s) { return {s.begin(), s.end()}; }
std::string hidHideUpper(std::string s) { for (auto& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A'); return s; }
struct HidHidePhysicalIdentity {
    bool resolved = false;
    std::string sourcePath, deviceInstanceId, baseContainerDeviceInstanceId, baseContainerDevicePath;
    USHORT vendorId = 0, productId = 0;
    std::string physicalIdentity;
};
struct HidHideVisibilitySnapshot {
    bool valid = true, cloaking = false;
    std::vector<std::wstring> hidden, applications;
    std::string inverseRaw = "--inv-off";
};
enum class YmccFamily { Unknown, MSI, Other };
struct MachineIdentity { bool resolved = true; YmccFamily family = YmccFamily::MSI; std::string deviceClass = "handheld", inputHidKey = "physical"; };
enum class PowerLifecycle { Ready, Suspended };
static MachineIdentity g_machineIdentity;
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool> g_inputReady{true}, g_externalPriorityVirtualSuspended{false};
static std::atomic<DWORD> g_externalPrioritySourceMask{0};
static std::atomic<ULONGLONG> g_externalPrioritySourceDeadline{0};
static std::atomic<unsigned> g_externalPrioritySourceTopology{0}, g_xinputTopologyEpoch{1};
static std::atomic<unsigned long long> g_externalPrioritySourcePower{0};
static std::atomic<std::uint64_t> g_externalPriorityRevision{0};
static bool g_inputHostReleaseQuarantined = false;
static std::mutex g_inputCaptureBusTickMx;
// Same consumer mutex as production gamepadReadState; only fake operations run.
static std::mutex g_gamepadReadMx;
static std::atomic<std::uint64_t> g_offlinePowerOverride{0};
namespace offline {
struct Device {
    HidHidePhysicalIdentity identity;
    std::string location, parentRoot = "USB\\ROOT_HUB30\\OFFLINE";
    int portKind = -1; unsigned slot = 4;
    bool fresh = true, reverse = true;
};
struct Child { bool completed = false; DWORD code = 0; };
struct World {
    ULONGLONG now = 1000; std::uint64_t power = 1;
    json settings = json::object(); std::vector<Device> devices;
    HidHideVisibilitySnapshot visibility; ep::VisibilityDebt durable;
    bool board = true, writes = true, host = true, stopLeavesHost = false, borrowBusy = false, exitFence = false;
    bool resolveOk = true, loadOk = true, saveOk = true, launchOk = true, snapshotOk = true;
    std::wstring failVerb, realLedgerPath;
    ep::DebtLoadFailure loadFailure = ep::DebtLoadFailure::InvalidRecord;
    int resolves = 0, snapshots = 0, saves = 0, launches = 0, stops = 0, refreshes = 0, resets = 0, reprobes = 0;
    int loads = 0, pnpQueries = 0, settingsReads = 0; bool settingsThrow = false;
    std::string queryRoot; std::vector<std::string> events; std::vector<json> logs;
    std::map<std::uintptr_t, Child> children; std::uintptr_t nextHandle = 100;
    std::function<void()> onStop, onReverse, onSnapshot, onSave, onLoad;
};
World w;
constexpr const char* builtinRoot = "USB\\VID_0DB0&PID_1902\\BUILTIN";
constexpr const char* mode1Root = "USB\\VID_0DB0&PID_1901\\BUILTIN";
constexpr const char* externalRoot = "USB\\VID_045E&PID_028E\\EXTERNAL";
constexpr const char* builtinLocation = "PCIROOT(0)#USBROOT(0)#USB(1)";
constexpr const char* externalLocation = "PCIROOT(0)#USBROOT(0)#USB(3)";
constexpr const wchar_t* modulePath = L"C:\\offline-fixture\\YeManCC.Native.exe";
Device device(std::string root, std::string location, int kind, unsigned slot = 4) {
    Device d; d.identity.resolved = true; d.identity.baseContainerDeviceInstanceId = root;
    d.identity.deviceInstanceId = "HID\\" + root.substr(4); d.identity.baseContainerDevicePath = "\\\\?\\" + root;
    d.identity.vendorId = root.find("VID_0DB0") != std::string::npos ? 0x0DB0 : 0x045E;
    d.identity.productId = root.find("PID_1902") != std::string::npos ? 0x1902 : root.find("PID_1901") != std::string::npos ? 0x1901 : 0x028E;
    d.location = std::move(location); d.portKind = kind; d.slot = slot; return d;
}
Device* byRoot(const std::string& root) {
    for (auto& d : w.devices) if (hidHideUpper(d.identity.baseContainerDeviceInstanceId) == hidHideUpper(root)) return &d;
    return nullptr;
}
Device* byPath(const std::string& path) {
    for (auto& d : w.devices) if (hidHideUpper(d.identity.baseContainerDevicePath) == hidHideUpper(path)) return &d;
    return nullptr;
}
Device* bySlot(unsigned slot) { for (auto& d : w.devices) if (d.slot == slot) return &d; return nullptr; }
void once(std::function<void()>& callback) { auto f = std::move(callback); callback = {}; if (f) f(); }
HANDLE handle(std::uintptr_t value) { return reinterpret_cast<HANDLE>(value); }
ULONGLONG tick() { return w.now; }
DWORD pid() { return 4242; }
BOOL close(HANDLE h) { CHECK(h == handle(17) || h == handle(18) || w.children.count(reinterpret_cast<std::uintptr_t>(h)) != 0); return TRUE; }
DWORD wait(HANDLE h, DWORD) {
    const auto it = w.children.find(reinterpret_cast<std::uintptr_t>(h)); CHECK(it != w.children.end());
    return it->second.completed ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}
BOOL exitCode(HANDLE h, LPDWORD code) { *code = w.children.at(reinterpret_cast<std::uintptr_t>(h)).code; return TRUE; }
BOOL process(LPCWSTR exe, LPWSTR command, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD flags,
             LPVOID, LPCWSTR, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi) {
    ++w.launches; w.events.push_back("cycle:" + W2U(command));
    CHECK(std::wstring(exe) == L"C:\\offline-fixture\\YeManInputHost.exe");
    CHECK((flags & CREATE_NO_WINDOW) != 0); CHECK(si->wShowWindow == SW_HIDE);
    CHECK(std::wstring(command).find(L" --external-controller-priority-cycle ") != std::wstring::npos);
    CHECK(std::wstring(command).find(L" --parent-pid 4242 --once") != std::wstring::npos);
    if (!w.launchOk) return FALSE;
    const auto h = w.nextHandle++; w.children[h] = {}; pi->hProcess = handle(h); pi->hThread = handle(18); return TRUE;
}
// Only production USB classification calls these fakes; no SetupAPI/CM import is linked.
CONFIGRET locate(PDEVINST node, DEVINSTID_W id, ULONG) {
    ++w.pnpQueries; w.queryRoot = W2U(id); if (!byRoot(w.queryRoot)) return CR_NO_SUCH_DEVNODE; *node = 1; return CR_SUCCESS;
}
CONFIGRET parent(PDEVINST out, DEVINST, ULONG) { *out = 2; return CR_SUCCESS; }
CONFIGRET property(DEVINST, ULONG, PULONG, PVOID buffer, PULONG bytes, ULONG) {
    *static_cast<ULONG*>(buffer) = 1; *bytes = sizeof(ULONG); return CR_SUCCESS;
}
CONFIGRET deviceId(DEVINST, PWSTR out, ULONG size, ULONG) {
    const auto d = byRoot(w.queryRoot); CHECK(d != nullptr); const auto id = U2W(d->parentRoot);
    CHECK(size > id.size()); wcscpy_s(out, size, id.c_str()); return CR_SUCCESS;
}
HDEVINFO classDevs(const GUID*, PCWSTR, HWND, DWORD) { return handle(17); }
BOOL enumInterface(HDEVINFO, PSP_DEVINFO_DATA, const GUID*, DWORD index, PSP_DEVICE_INTERFACE_DATA) { return index == 0; }
BOOL interfaceDetail(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail,
                     DWORD, PDWORD size, PSP_DEVINFO_DATA) {
    if (!detail) { if (size) *size = 256; return FALSE; }
    wcscpy_s(detail->DevicePath, 32, L"offline-hub"); return TRUE;
}
BOOL destroySet(HDEVINFO) { return TRUE; }
HANDLE file(LPCWSTR path, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) {
    CHECK(std::wstring(path) == L"offline-hub"); return handle(17);
}
BOOL ioctl(HANDLE h, DWORD code, LPVOID, DWORD, LPVOID output, DWORD, LPDWORD returned, LPOVERLAPPED) {
    CHECK(h == handle(17)); CHECK(code == IOCTL_USB_GET_PORT_CONNECTOR_PROPERTIES);
    const auto d = byRoot(w.queryRoot); CHECK(d != nullptr); if (d->portKind < 0) return FALSE;
    auto* p = static_cast<USB_PORT_CONNECTOR_PROPERTIES*>(output); p->UsbPortProperties.PortIsUserConnectable = d->portKind == 1;
    *returned = sizeof(USB_PORT_CONNECTOR_PROPERTIES); return TRUE;
}
DWORD userIndex(LPCWSTR path, BYTE* index) {
    const auto d = byPath(W2U(path)); if (!d || d->slot >= 4) return ERROR_DEVICE_NOT_CONNECTED;
    *index = static_cast<BYTE>(d->slot); return 0;
}
DWORD devicePath(DWORD slot, wchar_t* out, DWORD* size) {
    const auto d = bySlot(slot); if (!d || !d->reverse) return ERROR_DEVICE_NOT_CONNECTED;
    const auto value = U2W(d->identity.baseContainerDevicePath); CHECK(*size > value.size());
    wcscpy_s(out, *size, value.c_str()); *size = static_cast<DWORD>(value.size()); once(w.onReverse); return 0;
}
}
std::wstring app_data_dir() { return L"C:\\offline-fixture"; }
json ymSettingsSection(const char*) {
    // The real settings reader can throw on file/mutex failures. This is an IO failure,
    // not a malformed value returned by the reader, and must stay inside maintenance.
    ++offline::w.settingsReads;
    if (offline::w.settingsThrow) throw std::runtime_error("Unified settings read failed: 5");
    return offline::w.settings;
}
std::uint64_t currentPowerGeneration() {
    const auto overridePower = g_offlinePowerOverride.load();
    return overridePower ? overridePower : offline::w.power;
}
std::uint64_t inputHostEpochToken() { return 1; }
std::string inputHostConfiguredPersona() { return "xbox360"; }
void appendNativeLifecycleLog(const char*, const json& value) { offline::w.logs.push_back(value); }
bool hardwareWriteAllowed() { return offline::w.writes; }
bool msiClawKnownBoardAuthority() { return offline::w.board; }
bool ymccOpenXInputAvailable() { return true; }
static auto g_ymccOpenXInputGetUserIndex = &offline::userIndex;
static auto g_ymccOpenXInputGetDevicePath = &offline::devicePath;
DWORD ymccXInputGetState14(DWORD slot, XINPUT_STATE* state) {
    const auto d = offline::bySlot(slot); if (!d || !d->fresh) return ERROR_DEVICE_NOT_CONNECTED;
    *state = {}; return ERROR_SUCCESS; // Deliberately neutral; no activity predicate.
}
std::string hidHideDeviceInstanceId(HDEVINFO, const SP_DEVINFO_DATA&) {
    const auto d = offline::byRoot(offline::w.queryRoot); CHECK(d != nullptr); return d->parentRoot;
}
bool hidHideIsVirtualIdentity(const std::string& leaf, const std::string& root) {
    return leaf.find("VIRTUAL") != std::string::npos || root.rfind("ROOT\\", 0) == 0;
}
std::vector<std::wstring> msiForegroundIsolationLocations(const std::string& root) {
    const auto d = offline::byRoot(root); if (!d || d->location.empty()) return {}; return {U2W(d->location)};
}
bool hidHideResolvePhysicalIdentities(std::vector<HidHidePhysicalIdentity>& out) {
    ++offline::w.resolves; if (!offline::w.resolveOk) return false;
    for (const auto& d : offline::w.devices) out.push_back(d.identity); return true;
}
std::vector<std::string> hidHidePhysicalTargets(const HidHidePhysicalIdentity& c) {
    return {c.baseContainerDeviceInstanceId, c.deviceInstanceId};
}
bool hidHideContainsPath(const std::vector<std::wstring>& paths, const std::string& path) {
    for (const auto& p : paths) if (hidHideUpper(W2U(p)) == hidHideUpper(path)) return true; return false;
}
bool hidHideCaptureVisibilitySnapshotViaCli(HidHideVisibilitySnapshot& out) {
    ++offline::w.snapshots; offline::once(offline::w.onSnapshot);
    if (!offline::w.snapshotOk) return false; out = offline::w.visibility; return true;
}
std::wstring hidHideCurrentModulePath() { return offline::modulePath; }
bool hidHideRun(const std::wstring& command, std::string*, DWORD* code) {
    auto& w = offline::w; w.events.push_back("command:" + W2U(command));
    const auto separator = command.find(L' '); const auto verb = command.substr(0, separator);
    std::wstring path;
    if (separator != std::wstring::npos) path = command.substr(separator + 2, command.size() - separator - 3);
    *code = 0; if (w.failVerb == verb) { *code = 5; return true; }
    auto remove = [&path](std::vector<std::wstring>& values) { values.erase(std::remove(values.begin(), values.end(), path), values.end()); };
    if (verb == L"--dev-hide") {
        CHECK(hidHideContainsPath(w.durable.hiddenPaths, W2U(path))); CHECK(w.durable.refreshPending);
        w.visibility.hidden.push_back(path);
    } else if (verb == L"--app-reg") {
        CHECK(hidHideContainsPath(w.durable.applications, W2U(path))); w.visibility.applications.push_back(path);
    } else if (verb == L"--cloak-on") { CHECK(w.durable.cloakChanged); w.visibility.cloaking = true;
    } else if (verb == L"--dev-unhide") { CHECK(hidHideContainsPath(w.durable.hiddenPaths, W2U(path))); remove(w.visibility.hidden);
    } else if (verb == L"--app-unreg") { CHECK(hidHideContainsPath(w.durable.applications, W2U(path))); remove(w.visibility.applications);
    } else if (verb == L"--cloak-off") { CHECK(w.durable.cloakChanged); w.visibility.cloaking = false;
    } else CHECK(false);
    return true;
}
bool msiForegroundIsolationBorrowBusy() { return offline::w.borrowBusy; }
bool inputHostHasLocalLifecycleState() { return offline::w.host; }
bool inputHostExitFenceRaised() { return offline::w.exitFence; }
std::wstring inputHostExecutablePath() { return L"C:\\offline-fixture\\YeManInputHost.exe"; }
void inputHostStop(const char* reason) {
    auto& w = offline::w; ++w.stops; w.events.push_back("host-stop");
    CHECK(std::string(reason) == "coordinator-stop"); CHECK(g_externalPriorityVirtualSuspended.load());
    if (!w.stopLeavesHost) w.host = false; offline::once(w.onStop);
}
void inputCaptureRefreshRealStickSettings(bool force) { CHECK(force); ++offline::w.refreshes; offline::w.events.push_back("refresh-settings"); }
static void gamepadResetNativeState(bool requireRelease) { CHECK(requireRelease); ++offline::w.resets; offline::w.events.push_back("reset-input"); }
void xinputSlotsRequestIdentityReprobe(DWORD mask, const char*) { CHECK(mask == 0xFu); ++offline::w.reprobes; }
void msiNativeForegroundUiEvent(bool force) { CHECK(force); }
namespace ymcc::externalpad {
bool offlineLoadDebt(const std::wstring&, VisibilityDebt& out, std::string& error, DebtLoadFailure& failure) {
    auto& w = offline::w; ++w.loads; offline::once(w.onLoad);
    if (!w.realLedgerPath.empty()) return loadDebt(w.realLedgerPath, out, error, failure);
    if (!w.loadOk) { failure = w.loadFailure; error = "injected-load-failure"; return false; }
    failure = DebtLoadFailure::None; error.clear(); out = w.durable; return true;
}
bool offlineLoadDebt(const std::wstring& path, VisibilityDebt& out, std::string& error) {
    DebtLoadFailure failure;
    return offlineLoadDebt(path, out, error, failure);
}
bool offlineSaveDebt(const std::wstring&, const VisibilityDebt& debt, std::string& error) {
    auto& w = offline::w; ++w.saves; offline::once(w.onSave);
    if (!w.saveOk) { error = "injected-save-failure"; return false; }
    w.durable = debt; w.events.push_back(!debt.hiddenPaths.empty() ? "wal:owned" : debt.refreshPending ? "wal:refresh" : "wal:clean"); return true;
}
}
// Preserve the same inert process terminal after the production wrapper rename.
namespace ymcc::processEfficiency { inline constexpr auto createProcess = &offline::process; }
// Redirect only external operations. Policy and runtime remain the actual production headers.
#define GetTickCount64 offline::tick
#define GetCurrentProcessId offline::pid
#define CloseHandle offline::close
#define WaitForSingleObject offline::wait
#define GetExitCodeProcess offline::exitCode
#define CreateProcessW offline::process
#define CreateFileW offline::file
#define DeviceIoControl offline::ioctl
#define CM_Locate_DevNodeW offline::locate
#define CM_Get_Parent offline::parent
#define CM_Get_DevNode_Registry_PropertyW offline::property
#define CM_Get_Device_IDW offline::deviceId
#define SetupDiGetClassDevsW offline::classDevs
#define SetupDiEnumDeviceInterfaces offline::enumInterface
#define SetupDiGetDeviceInterfaceDetailW offline::interfaceDetail
#define SetupDiDestroyDeviceInfoList offline::destroySet
#define loadDebt offlineLoadDebt
#define saveDebt offlineSaveDebt
#include "../native/external_controller_priority_runtime.h"
#undef GetTickCount64
#undef GetCurrentProcessId
#undef CloseHandle
#undef WaitForSingleObject
#undef GetExitCodeProcess
#undef CreateProcessW
#undef CreateFileW
#undef DeviceIoControl
#undef CM_Locate_DevNodeW
#undef CM_Get_Parent
#undef CM_Get_DevNode_Registry_PropertyW
#undef CM_Get_Device_IDW
#undef SetupDiGetClassDevsW
#undef SetupDiEnumDeviceInterfaces
#undef SetupDiGetDeviceInterfaceDetailW
#undef SetupDiDestroyDeviceInfoList
#undef loadDebt
#undef saveDebt
namespace offline {
void reset(bool enabled = true) {
    w = {}; g_externalPriority = {}; g_machineIdentity = {}; g_offlinePowerOverride = 0;
    g_externalPriorityPolicyEnabled = false;
    g_inputReady = true; g_powerLifecycle = PowerLifecycle::Ready; g_inputHostReleaseQuarantined = false;
    g_externalPriorityVirtualSuspended = false; g_externalPrioritySourceMask = 0; g_externalPrioritySourceDeadline = 0;
    g_externalPrioritySourceTopology = 0; g_externalPrioritySourcePower = 0; g_externalPriorityRevision = 0; g_xinputTopologyEpoch = 1;
    w.settings = {{"ownership", {{"externalControllerPriority", enabled}}}};
    w.devices = {device(builtinRoot, builtinLocation, 0), device(externalRoot, externalLocation, 1, 0)};
}
// A busy UI reader is a different thread from the lifecycle mutex owner.
json snapshotOnReaderThread() {
    std::promise<json> value; auto result = value.get_future();
    std::thread reader([&] {
        try { value.set_value(externalControllerPrioritySnapshot()); }
        catch (...) { value.set_exception(std::current_exception()); }
    });
    reader.join(); return result.get();
}
// These retained files are created only in a unique private-temp fixture directory.
// Actual ledger reads test sharing recovery; all product effects and saves stay faked.
std::wstring realLedgerFixture(const std::string& bytes) {
    wchar_t temp[MAX_PATH + 1]{};
    const DWORD count = GetTempPathW(MAX_PATH, temp); CHECK(count > 0 && count < MAX_PATH);
    static unsigned serial = 0;
    const std::wstring root = std::wstring(temp, count) + L"ymcc-externalpad-runtime-r53-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++serial);
    CHECK(CreateDirectoryW(root.c_str(), nullptr));
    const auto path = root + L"\\ledger.json";
    ep::detail::File file;
    file.handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(file.handle != INVALID_HANDLE_VALUE);
    DWORD written = 0; CHECK(WriteFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr));
    CHECK(written == bytes.size()); std::string error; CHECK(file.close(error));
    std::cout << "R53_PRIVATE_LEDGER " << W2U(path) << '\n';
    return path;
}
std::wstring realLedgerFixture(const ep::VisibilityDebt& debt) {
    std::string bytes, error; CHECK(ep::detail::encode(debt, bytes, error)); return realLedgerFixture(bytes);
}
void step(ULONGLONG time) { w.now = time; externalControllerPriorityMaintenance(); }
void complete(DWORD code = 0) {
    CHECK(g_externalPriority.cycleProcess != nullptr);
    auto& child = w.children.at(reinterpret_cast<std::uintptr_t>(g_externalPriority.cycleProcess)); child.completed = true; child.code = code;
}
void takeover() {
    step(1000); step(1500); step(2000); CHECK(g_externalPriority.cycleProcess != nullptr);
    CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(w.durable.refreshPending); CHECK(!w.durable.hiddenPaths.empty());
    complete(); step(2500); CHECK(g_externalPriority.active); CHECK(g_externalPriority.state == "active");
}
void beginReturn() { w.settings["ownership"]["externalControllerPriority"] = false; step(w.now + 500); CHECK(g_externalPriority.returning); }
void ownDebt() {
    w.durable.builtinRoot = builtinRoot; w.durable.locationPath = builtinLocation; w.durable.refreshPending = true;
    for (const auto& target : hidHidePhysicalTargets(w.devices.front().identity)) w.durable.hiddenPaths.push_back(U2W(target));
    w.durable.applications = {modulePath}; w.durable.cloakChanged = true;
    w.visibility.hidden = w.durable.hiddenPaths; w.visibility.applications = w.durable.applications; w.visibility.cloaking = true;
}
std::size_t eventIndex(const std::string& prefix) {
    for (std::size_t i = 0; i < w.events.size(); ++i) if (w.events[i].rfind(prefix, 0) == 0) return i;
    return w.events.size();
}
bool noMutations() { return w.saves == 0 && w.launches == 0 && w.stops == 0; }
void run(const char* name, const std::function<void()>& test) {
    ++total;
    try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
}
}
int main() {
    using namespace offline;
    run("default false malformed settings do not mutate", [] {
        for (const auto& settings : std::vector<json>{json::object(), {{"ownership", json::object()}},
            {{"ownership", {{"externalControllerPriority", "true"}}}}, {{"ownership", {{"externalControllerPriority", nullptr}}}}}) {
            reset(); w.settings = settings; step(1000); step(2000); CHECK(!g_externalPriority.enabled); CHECK(noMutations());
            CHECK(externalControllerPrioritySnapshot()["state"] == "off");
        }
    });
    run("no external unknown virtual unresolved nonfresh reverse mismatch wait", [] {
        for (unsigned scenario = 0; scenario < 7; ++scenario) {
            reset(); auto& ext = w.devices.back();
            if (scenario == 0) w.devices.pop_back(); else if (scenario == 1) ext.portKind = -1;
            else if (scenario == 2) ext.identity.deviceInstanceId = "VIRTUAL";
            else if (scenario == 3) ext.identity.resolved = false; else if (scenario == 4) ext.fresh = false;
            else if (scenario == 5) ext.reverse = false; else ext.location.clear();
            for (ULONGLONG time = 1000; time < 5000; time += 500) step(time);
            CHECK(!g_externalPriority.active); CHECK(noMutations()); CHECK(!g_externalPriorityVirtualSuspended.load());
        }
    });
    run("unknown board removable MSI and ambiguous builtins never hide", [] {
        for (unsigned scenario = 0; scenario < 3; ++scenario) {
            reset();
            if (scenario == 0) g_machineIdentity.family = YmccFamily::Unknown;
            if (scenario == 1) { w.board = false; w.devices.front().portKind = 1; w.devices.front().slot = 1; }
            if (scenario == 2) w.devices.push_back(device("USB\\VID_1111&PID_2222\\OTHER", "PCIROOT(0)#USBROOT(0)#USB(2)", 0));
            for (ULONGLONG time = 1000; time < 5000; time += 500) step(time);
            CHECK(noMutations()); CHECK(!g_externalPriorityVirtualSuspended.load());
        }
    });
    run("USB internal hub inside removable dock stays external", [] {
        reset(); auto dock = device("USB\\VID_1111&PID_2222\\DOCK", "DOCK-PORT", 1);
        w.devices[1].portKind = 0; w.devices[1].parentRoot = dock.identity.baseContainerDeviceInstanceId; w.devices.push_back(dock);
        CHECK(externalPriorityUsbPortKind(externalRoot) == 1);
        w.devices.back().portKind = -1; CHECK(externalPriorityUsbPortKind(externalRoot) == -1);
    });
    run("neutral takeover canonical stop WAL before hides exact root cycle", [] {
        reset(); takeover(); CHECK(w.stops == 1); CHECK(w.resets >= 2); CHECK(g_externalPrioritySourceMask.load() == 1);
        CHECK(eventIndex("host-stop") < eventIndex("wal:owned")); CHECK(eventIndex("wal:owned") < eventIndex("command:"));
        CHECK(w.durable.builtinRoot == builtinRoot); CHECK(w.durable.locationPath == builtinLocation);
        CHECK(eventIndex("cycle:") < w.events.size()); CHECK(w.events[eventIndex("cycle:")].find(builtinRoot) != std::string::npos);
        CHECK(w.events[eventIndex("cycle:")].find(builtinLocation) != std::string::npos);
        CHECK(g_externalPriority.state == "active");
    });
    run("same location MSI mode2 re-resolves mode1 after stop", [] {
        reset(); w.onStop = [] { w.devices.front() = device(mode1Root, builtinLocation, 0); };
        takeover(); CHECK(w.durable.builtinRoot == mode1Root); CHECK(w.events[eventIndex("cycle:")].find(mode1Root) != std::string::npos);
    });
    run("asynchronous mode1 arrival waits without stopping Host twice", [] {
        reset(); w.onStop = [] { w.devices.erase(w.devices.begin()); };
        step(1000); step(1500); step(2000); CHECK(g_externalPriority.takingOver); CHECK(w.launches == 0); CHECK(w.stops == 1);
        step(2500); step(3000); CHECK(w.stops == 1); CHECK(w.saves == 0);
        w.devices.insert(w.devices.begin(), device(mode1Root, builtinLocation, 0)); ++g_xinputTopologyEpoch;
        step(3500); CHECK(g_externalPriority.cycleProcess != nullptr); CHECK(w.stops == 1); CHECK(w.durable.builtinRoot == mode1Root);
    });
    run("same port arrival timeout holds attempt until actual reconnect", [] {
        reset(); w.onStop = [] { w.devices.erase(w.devices.begin()); };
        step(1000); step(1500); step(2000); step(9500); CHECK(g_externalPriority.takingOver);
        step(10000); CHECK(g_externalPriority.returning); step(10500); CHECK(!g_externalPriorityVirtualSuspended.load());
        w.devices.insert(w.devices.begin(), device(mode1Root, builtinLocation, 0)); ++g_xinputTopologyEpoch;
        for (ULONGLONG time = 11000; time <= 17000; time += 500) step(time);
        CHECK(w.stops == 1); CHECK(w.launches == 0); CHECK(!g_externalPriority.takingOver);
        w.devices[1].fresh = false;
        for (ULONGLONG time = 17500; time <= 20500; time += 500) step(time);
        w.devices[1].fresh = true;
        for (ULONGLONG time = 21000; time <= 23000 && !g_externalPriority.cycleProcess; time += 500) step(time);
        CHECK(g_externalPriority.cycleProcess != nullptr); CHECK(w.launches == 1);
    });
    run("builtin moved port never hidden and times out without further stop", [] {
        reset(); w.onStop = [] { w.devices.front().location = "PCIROOT(0)#USBROOT(0)#USB(9)"; };
        step(1000); step(1500); step(2000); CHECK(w.saves == 0); CHECK(w.launches == 0);
        step(10000); CHECK(g_externalPriority.returning); step(10500); CHECK(!g_externalPriorityVirtualSuspended.load()); CHECK(w.stops == 1);
    });
    run("external moved location or disable during stop prevents hide", [] {
        for (unsigned scenario = 0; scenario < 2; ++scenario) {
            reset(); w.onStop = [scenario] {
                if (scenario == 0) w.devices.back().location = "PCIROOT(0)#USBROOT(0)#USB(8)";
                else w.settings["ownership"]["externalControllerPriority"] = false;
            };
            step(1000); step(1500); step(2000); CHECK(g_externalPriority.returning); CHECK(w.saves == 0); CHECK(w.launches == 0);
        }
    });
    run("release quarantine lifecycle state busy owner block hides", [] {
        for (unsigned scenario = 0; scenario < 3; ++scenario) {
            reset(); if (scenario == 0) w.stopLeavesHost = true; else if (scenario == 1) g_inputHostReleaseQuarantined = true; else w.borrowBusy = true;
            step(1000); step(1500); step(2000); CHECK(w.saves == 0); CHECK(w.launches == 0); CHECK(w.visibility.hidden.empty());
        }
    });
    run("WAL failure cannot hide register or cloak", [] {
        reset(); w.saveOk = false; step(1000); step(1500); step(2000);
        CHECK(g_externalPriority.returning); CHECK(w.visibility.hidden.empty()); CHECK(w.visibility.applications.empty()); CHECK(!w.visibility.cloaking);
        CHECK(eventIndex("command:") == w.events.size()); CHECK(w.launches == 0);
    });
    run("partial hide failure returns owned changes retains foreign cloak", [] {
        reset(); w.visibility.hidden = {L"USB\\VID_9999&PID_0001\\FOREIGN"}; w.visibility.cloaking = true;
        w.failVerb = L"--dev-hide"; step(1000); step(1500); step(2000);
        CHECK(g_externalPriority.returning); CHECK(w.durable.refreshPending); CHECK(!w.durable.hiddenPaths.empty());
        w.failVerb.clear(); step(2500); CHECK(g_externalPriority.cycleProcess != nullptr); CHECK(w.durable.refreshPending);
        CHECK(w.visibility.hidden == std::vector<std::wstring>{L"USB\\VID_9999&PID_0001\\FOREIGN"}); CHECK(w.visibility.cloaking);
    });
    run("preexisting builtin hide cannot be borrowed", [] {
        reset(); w.visibility.hidden = {U2W(builtinRoot)}; step(1000); step(1500); step(2000);
        CHECK(w.saves == 0); CHECK(w.launches == 0); CHECK(w.visibility.hidden == std::vector<std::wstring>{U2W(builtinRoot)});
    });
    run("active stable selection multiple pads slot reorder discovery caching", [] {
        reset(); takeover(); const auto beforeDeadline = w.resolves;
        // Discovery at 1000 has its next deadline at 3000. Refresh there is required.
        step(3000); CHECK(w.resolves == beforeDeadline + 1); const auto cached = w.resolves;
        w.devices.push_back(device("USB\\VID_0001&PID_0001\\EARLIER", "PCIROOT(0)#USBROOT(0)#USB(7)", 1, 1));
        step(3500); CHECK(w.resolves == cached); CHECK(g_externalPriority.externalRoot == externalRoot);
        const auto beforeChangedSlotSnapshots = w.snapshots;
        w.devices[1].slot = 3; ++g_xinputTopologyEpoch; step(4000);
        CHECK(g_externalPriority.externalRoot == externalRoot); CHECK(g_externalPrioritySourceMask.load() == 8);
        // 1 discovery + 1 FreshCandidate on changed slot; exactly one visibility recheck.
        CHECK(w.resolves == cached + 2); CHECK(w.snapshots == beforeChangedSlotSnapshots + 1);
        const auto afterDirty = w.resolves, afterChangedSlotSnapshots = w.snapshots;
        step(4500); CHECK(w.resolves == afterDirty); CHECK(w.snapshots == afterChangedSlotSnapshots);
        CHECK(g_externalPrioritySourceMask.load() == 8);
        step(6000); CHECK(w.resolves == afterDirty + 1);
        CHECK(g_externalPriority.externalRoot == externalRoot); CHECK(w.stops == 1); CHECK(w.launches == 1);
    });
    run("active hotplug flap respects 2sec absence", [] {
        reset(); takeover(); w.devices.back().fresh = false;
        step(3000); step(3500); step(4000); step(4500); CHECK(g_externalPriority.active); CHECK(!g_externalPriority.returning);
        w.devices.back().fresh = true; step(5000); CHECK(g_externalPriority.active); CHECK(g_externalPrioritySourceMask.load() == 1);
        w.devices.back().fresh = false; step(5500); step(6000); step(6500); step(7000); CHECK(!g_externalPriority.returning);
        step(7500); CHECK(g_externalPriority.returning); CHECK(g_externalPrioritySourceMask.load() == 0);
    });
    run("suspend resume keeps preference disable returns automatically", [] {
        reset(); takeover(); g_powerLifecycle = PowerLifecycle::Suspended; g_inputReady = false; ++w.power;
        step(3000); step(10000); CHECK(g_externalPriority.active); CHECK(!g_externalPriority.returning); CHECK(w.launches == 1);
        g_powerLifecycle = PowerLifecycle::Ready; g_inputReady = true; step(10500);
        CHECK(g_externalPriority.active); CHECK(g_externalPrioritySourcePower.load() == w.power);
        beginReturn(); CHECK(g_externalPrioritySourceMask.load() == 0); CHECK(g_externalPriorityVirtualSuspended.load());
    });
    run("durable return duty and suppression persist until success receipt", [] {
        reset(); takeover(); beginReturn(); step(3500);
        CHECK(g_externalPriority.cycleProcess != nullptr); CHECK(w.visibility.hidden.empty()); CHECK(w.visibility.applications.empty());
        CHECK(w.durable.hiddenPaths.empty()); CHECK(w.durable.refreshPending); CHECK(g_externalPriorityVirtualSuspended.load());
        step(4000); CHECK(w.durable.refreshPending); CHECK(g_externalPriorityVirtualSuspended.load());
        complete(); step(4500); CHECK(w.durable.refreshPending); CHECK(g_externalPriorityVirtualSuspended.load());
        step(6500); CHECK(!externalPriorityHasDebt(w.durable)); CHECK(!g_externalPriority.returning);
        CHECK(!g_externalPriorityVirtualSuspended.load()); CHECK(w.reprobes == 1); CHECK(g_externalPriority.state == "off");
    });
    run("return launch failures capped at two per power enable episode", [] {
        reset(); takeover(); beginReturn(); w.launchOk = false; step(3500); step(6500); step(9500); step(12500);
        CHECK(w.launches == 3); CHECK(w.durable.refreshPending); CHECK(g_externalPriority.returning); CHECK(g_externalPriorityVirtualSuspended.load());
        ++w.power; step(15500); CHECK(w.launches == 4); CHECK(w.durable.refreshPending);
    });
    run("nonzero return receipt retains durable duty after attempt cap", [] {
        reset(); takeover(); beginReturn(); step(3500); complete(23); step(4000); CHECK(w.durable.refreshPending);
        step(6500); CHECK(w.launches == 3); complete(23); step(7000); step(9500); CHECK(w.launches == 3);
        CHECK(w.durable.refreshPending); CHECK(g_externalPriorityVirtualSuspended.load());
    });
    run("return location drift retains duty never cycles wrong port", [] {
        reset(); takeover(); beginReturn(); w.devices.front().location = "PCIROOT(0)#USBROOT(0)#USB(9)";
        step(3500); CHECK(w.launches == 1); CHECK(w.durable.refreshPending); CHECK(g_externalPriorityVirtualSuspended.load());
        CHECK(w.durable.locationPath == builtinLocation);
    });
    run("startup inherited refresh only duty paid before unfreeze", [] {
        reset(false); ownDebt(); w.durable.hiddenPaths.clear(); w.durable.applications.clear(); w.durable.cloakChanged = false;
        w.visibility.hidden.clear(); w.visibility.applications.clear(); w.visibility.cloaking = false;
        step(1000); CHECK(g_externalPriority.returning); CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(w.launches == 1);
        complete(); step(1500); CHECK(w.durable.refreshPending); step(4000);
        CHECK(!externalPriorityHasDebt(w.durable)); CHECK(!g_externalPriorityVirtualSuspended.load());
    });
    run("clean WAL failure after verified cycle retains suppression", [] {
        reset(); takeover(); beginReturn(); step(3500); complete(); step(4000); w.saveOk = false; step(6500);
        CHECK(w.durable.refreshPending); CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(g_externalPriority.returning);
        w.saveOk = true; step(9500); CHECK(!externalPriorityHasDebt(w.durable)); CHECK(!g_externalPriorityVirtualSuspended.load());
    });
    run("hardware gate prevents device mutation", [] {
        reset(); w.writes = false; step(1000); step(2000); CHECK(noMutations());
    });
    run("takeover cycle failure unwinds owned WAL", [] {
        reset(); step(1000); step(1500); step(2000); complete(41); step(2500);
        CHECK(g_externalPriority.returning); CHECK(!g_externalPriority.active); CHECK(w.durable.refreshPending);
        CHECK(g_externalPriorityVirtualSuspended.load());
    });
    run("external disappears during takeover cycle returns immediately", [] {
        reset(); step(1000); step(1500); step(2000); w.devices.back().fresh = false; complete(); step(2500);
        CHECK(g_externalPriority.returning); CHECK(!g_externalPriority.active); CHECK(w.durable.refreshPending);
    });
    // Requirement regressions below remain ordinary failures until production satisfies them.
    run("RACE old slot must not receive new topology generation", [] {
        reset(); takeover(); w.onReverse = [] { w.devices[1].slot = 3; ++g_xinputTopologyEpoch; }; step(3000);
        const bool acceptedOldSlot = g_externalPrioritySourceMask.load() == 1 &&
            g_externalPrioritySourceTopology.load() == g_xinputTopologyEpoch.load() &&
            g_externalPrioritySourcePower.load() == w.power && w.now <= g_externalPrioritySourceDeadline.load();
        CHECK(!acceptedOldSlot);
    });
    run("RACE old sample must not receive new power generation", [] {
        reset(); takeover(); w.onReverse = [] { ++w.power; g_powerLifecycle = PowerLifecycle::Suspended; }; step(3000);
        const bool acceptedOldSample = g_externalPrioritySourceMask.load() != 0 &&
            g_externalPrioritySourcePower.load() == w.power && w.now <= g_externalPrioritySourceDeadline.load();
        CHECK(!acceptedOldSample);
    });
    run("failed Host release with no priority debt must stay suppressed", [] {
        reset(); w.stopLeavesHost = true; step(1000); step(1500); step(2000);
        CHECK(g_externalPriority.returning); CHECK(!externalPriorityHasDebt(w.durable)); step(2500);
        CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(g_externalPriority.returning);
    });
    run("quarantined release with no priority debt must stay suppressed", [] {
        reset(); g_inputHostReleaseQuarantined = true; step(1000); step(1500); step(2000); step(2500);
        CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(g_externalPriority.returning);
    });
    run("startup unreadable owned ledger must fail closed for virtual admission", [] {
        reset(); ownDebt(); w.loadOk = false; step(1000); step(2000);
        CHECK(g_externalPriority.storeInvalid); CHECK(noMutations()); CHECK(g_externalPriorityVirtualSuspended.load());
    });
    run("R53 actual shared-file recovery unfreezes OFF through existing return chain", [] {
        reset(false); w.realLedgerPath = realLedgerFixture(ep::VisibilityDebt{});
        {
            ep::detail::File blocker;
            blocker.handle = CreateFileW(w.realLedgerPath.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            CHECK(blocker.handle != INVALID_HANDLE_VALUE);
            step(1000); CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(noMutations());
        }
        ep::VisibilityDebt readable; std::string error;
        CHECK(ep::loadDebt(w.realLedgerPath, readable, error)); // Same unchanged file is now actually readable.
        step(2000);
        CHECK(w.loads == 2); CHECK(g_externalPriority.loaded); CHECK(!g_externalPriority.storeInvalid);
        CHECK(!g_externalPriorityVirtualSuspended.load()); CHECK(g_externalPriority.state == "off");
        CHECK(!externalControllerPrioritySnapshot()["enabled"].get<bool>());
        CHECK(noMutations()); CHECK(w.resolves == 0); CHECK(w.pnpQueries == 0);
        CHECK(w.refreshes == 1); CHECK(w.resets == 1); CHECK(w.reprobes == 1);
        step(10000); CHECK(w.loads == 2); CHECK(w.refreshes == 1);
    });
    run("R53 retries use capped backoff without resetting on policy or power edges", [] {
        reset(false); ownDebt(); const auto debt = w.durable;
        w.loadOk = false; w.loadFailure = ep::DebtLoadFailure::RetryableIo;
        std::vector<ULONGLONG> attempts;
        for (ULONGLONG time = 1000; time <= 100000; time += 500) {
            if (time == 1500 || time == 3500) { w.settings["ownership"]["externalControllerPriority"] = time == 1500; ++w.power; }
            const auto loads = w.loads; step(time); if (loads != w.loads) attempts.push_back(time);
            CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(!g_externalPriority.loaded);
            CHECK(!g_externalPriority.storeInvalid); CHECK(noMutations());
            CHECK(w.durable.hiddenPaths == debt.hiddenPaths); CHECK(w.durable.applications == debt.applications);
            CHECK(w.durable.cloakChanged == debt.cloakChanged); CHECK(w.durable.refreshPending == debt.refreshPending);
            CHECK(g_externalPriority.debt.hiddenPaths.empty()); CHECK(w.snapshots == 0); CHECK(w.resolves == 0);
        }
        CHECK(attempts == std::vector<ULONGLONG>({1000, 2000, 4000, 8000, 16000, 32000, 62000, 92000}));
    });
    run("R53 actual inherited owned record stays suspended until existing refresh receipt", [] {
        reset(false); ownDebt(); w.host = false;
        w.realLedgerPath = realLedgerFixture(w.durable);
        const std::wstring foreign = L"USB\\VID_9999&PID_0001\\FOREIGN";
        w.visibility.hidden.push_back(foreign);
        {
            ep::detail::File blocker;
            blocker.handle = CreateFileW(w.realLedgerPath.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            CHECK(blocker.handle != INVALID_HANDLE_VALUE);
            step(1000); CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(noMutations());
            CHECK(g_externalPriority.debt.hiddenPaths.empty()); // No ownership is inferred from a failed read.
        }
        step(2000); CHECK(w.loads == 2); CHECK(g_externalPriority.loaded);
        CHECK(g_externalPriority.returning); CHECK(g_externalPriorityVirtualSuspended.load());
        CHECK(w.visibility.hidden == std::vector<std::wstring>({foreign})); CHECK(w.visibility.cloaking);
        CHECK(w.durable.hiddenPaths.empty()); CHECK(w.durable.refreshPending);
        CHECK(w.launches == 1); CHECK(w.stops == 0); CHECK(w.refreshes == 0);
        step(2500); CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(w.durable.refreshPending);
        complete(); step(3000); CHECK(g_externalPriorityVirtualSuspended.load());
        step(5000); CHECK(!externalPriorityHasDebt(w.durable)); CHECK(!g_externalPriorityVirtualSuspended.load());
        CHECK(w.visibility.hidden == std::vector<std::wstring>({foreign})); CHECK(w.visibility.cloaking);
        CHECK(w.refreshes == 1); CHECK(w.resets == 1); CHECK(w.reprobes == 1); CHECK(w.stops == 0);
    });
    run("R53 readable record with unverified return never clears suspension or debt", [] {
        reset(false); ownDebt(); const auto debt = w.durable; w.snapshotOk = false;
        w.loadOk = false; w.loadFailure = ep::DebtLoadFailure::RetryableIo; step(1000);
        w.loadOk = true; step(2000); step(5000); externalControllerPriorityShutdown();
        CHECK(w.loads == 2); CHECK(g_externalPriority.loaded); CHECK(g_externalPriority.returning);
        CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(noMutations());
        CHECK(g_externalPriority.debt.hiddenPaths == debt.hiddenPaths); CHECK(w.durable.hiddenPaths == debt.hiddenPaths);
        CHECK(w.durable.applications == debt.applications); CHECK(w.durable.cloakChanged); CHECK(w.durable.refreshPending);
        CHECK(w.refreshes == 0); CHECK(w.resets == 0); CHECK(w.reprobes == 0);
    });
    run("R53 actual malformed file is neither reread automatically nor repaired", [] {
        reset(false); w.realLedgerPath = realLedgerFixture(std::string("{broken-record"));
        step(1000); CHECK(g_externalPriority.storeInvalid); CHECK(g_externalPriorityVirtualSuspended.load());
        for (ULONGLONG time = 2000; time <= 100000; time += 500) step(time);
        externalControllerPriorityShutdown(); CHECK(w.loads == 1); CHECK(noMutations());
        CHECK(w.refreshes == 0); CHECK(w.resolves == 0); CHECK(g_externalPriorityVirtualSuspended.load());
        ep::detail::File file;
        file.handle = CreateFileW(w.realLedgerPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(file.handle != INVALID_HANDLE_VALUE); char bytes[32]{}; DWORD read = 0;
        CHECK(ReadFile(file.handle, bytes, sizeof(bytes), &read, nullptr));
        CHECK(std::string(bytes, read) == "{broken-record");
    });
    run("R53 IO recovery never accepts a subsequently invalid ownership record", [] {
        reset(false); ownDebt(); const auto debt = w.durable;
        w.loadOk = false; w.loadFailure = ep::DebtLoadFailure::RetryableIo; step(1000);
        w.loadFailure = ep::DebtLoadFailure::InvalidRecord; step(2000);
        w.loadOk = true; step(50000); externalControllerPriorityShutdown();
        CHECK(w.loads == 2); CHECK(g_externalPriority.storeInvalid); CHECK(g_externalPriorityVirtualSuspended.load());
        CHECK(g_externalPriority.debt.hiddenPaths.empty()); CHECK(w.durable.hiddenPaths == debt.hiddenPaths); CHECK(noMutations());
    });
    run("R53 readable empty ledger waits for existing power input and write gates", [] {
        for (unsigned gate = 0; gate < 3; ++gate) {
            reset(false); w.loadOk = false; w.loadFailure = ep::DebtLoadFailure::RetryableIo; step(1000);
            if (gate == 0) g_powerLifecycle = PowerLifecycle::Suspended;
            if (gate == 1) g_inputReady = false;
            if (gate == 2) w.writes = false;
            w.loadOk = true; step(2000);
            CHECK(w.loads == 2); CHECK(g_externalPriority.loaded); CHECK(g_externalPriority.returning);
            CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(noMutations()); CHECK(w.refreshes == 0);
            g_powerLifecycle = PowerLifecycle::Ready; g_inputReady = true; w.writes = true;
            step(2500); CHECK(!g_externalPriorityVirtualSuspended.load()); CHECK(g_externalPriority.state == "off");
        }
    });
    run("R53 exit before successful read does not clear or claim unknown durable debt", [] {
        reset(false); ownDebt(); const auto debt = w.durable;
        w.loadOk = false; w.loadFailure = ep::DebtLoadFailure::RetryableIo; step(1000);
        externalControllerPriorityShutdown(); CHECK(w.loads == 1); CHECK(noMutations());
        CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(g_externalPriority.debt.hiddenPaths.empty());
        CHECK(w.durable.hiddenPaths == debt.hiddenPaths); CHECK(w.durable.applications == debt.applications);
        CHECK(w.durable.refreshPending); CHECK(w.durable.cloakChanged);
    });
    run("active audit detects selected external being hidden by foreign owner", [] {
        reset(); takeover(); w.visibility.hidden.push_back(U2W(externalRoot)); step(7500);
        CHECK(g_externalPriority.returning); CHECK(!g_externalPriority.active);
    });
    run("shutdown consumes successful return receipt before cleaning WAL", [] {
        reset(); takeover(); beginReturn(); step(3500); complete(); const auto launches = w.launches;
        externalControllerPriorityShutdown(); CHECK(w.launches == launches); CHECK(!externalPriorityHasDebt(w.durable));
    });
    // R2 requirements exercise the included runtime through bounded fake IO.
    // Keep failures visible until the production owner fixes the actual header.
    run("R2 default OFF clean startup performs zero PnP hide Host or input mutation", [] {
        for (unsigned scenario = 0; scenario < 2; ++scenario) {
            reset(false); if (scenario == 0) w.settings = json::object();
            w.visibility.hidden = {L"USB\\VID_9999&PID_0001\\FOREIGN"};
            w.visibility.applications = {L"C:\\foreign\\owner.exe"}; w.visibility.cloaking = true;
            const auto hidden = w.visibility.hidden, apps = w.visibility.applications;
            for (ULONGLONG time = 1000; time <= 12000; time += 500) {
                if (time == 4000) { ++w.power; ++g_xinputTopologyEpoch; }
                step(time);
            }
            externalControllerPriorityShutdown();
            CHECK(w.loads == 1); CHECK(w.resolves == 0); CHECK(w.pnpQueries == 0); CHECK(w.snapshots == 0);
            CHECK(noMutations()); CHECK(w.refreshes == 0); CHECK(w.resets == 0); CHECK(w.reprobes == 0);
            CHECK(w.events.empty()); CHECK(w.host); CHECK(w.visibility.hidden == hidden);
            CHECK(w.visibility.applications == apps); CHECK(w.visibility.cloaking);
            CHECK(!g_externalPriorityVirtualSuspended.load()); CHECK(g_externalPrioritySourceMask.load() == 0);
            CHECK(!externalControllerPrioritySnapshot()["enabled"].get<bool>());
        }
    });
    run("R2 default OFF snapshot stays truthful during ledger IO", [] {
        reset(false); bool observed = false, enabled = true;
        w.onLoad = [&] { observed = true; enabled = snapshotOnReaderThread()["enabled"].get<bool>(); };
        step(1000); CHECK(observed); CHECK(!enabled); CHECK(noMutations());
    });
    run("R2 unreadable ledger OFF snapshot never claims priority enabled", [] {
        reset(false); ownDebt(); w.loadOk = false; step(1000);
        const auto snapshot = externalControllerPrioritySnapshot();
        CHECK(!snapshot["enabled"].get<bool>()); CHECK(snapshot["state"] == "degraded");
        CHECK(snapshot["virtualSuspended"].get<bool>()); CHECK(noMutations());
    });
    run("R2 disabled policy returns builtin configuration while takeover child exceeds 8sec", [] {
        reset(); step(1000); step(1500); step(2000);
        const auto child = g_externalPriority.cycleProcess;
        CHECK(child != nullptr); CHECK(!w.visibility.hidden.empty());
        w.settings["ownership"]["externalControllerPriority"] = false;
        step(10500); step(11000);
        CHECK(w.visibility.hidden.empty()); CHECK(w.visibility.applications.empty()); CHECK(!w.visibility.cloaking);
        CHECK(w.durable.hiddenPaths.empty()); CHECK(w.durable.applications.empty()); CHECK(!w.durable.cloakChanged);
        CHECK(w.durable.refreshPending); CHECK(g_externalPriority.cycleProcess == child);
        CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(w.launches == 1); CHECK(w.stops == 1);
        CHECK(g_externalPrioritySourceMask.load() == 0);
        complete(); step(14000); step(14500);
        CHECK(g_externalPriority.cycleProcess != nullptr); CHECK(g_externalPriority.cycleProcess != child);
        CHECK(g_externalPriority.cycleRestoring); CHECK(w.launches == 2); CHECK(w.durable.refreshPending);
        complete(); step(15000); step(18000);
        CHECK(!externalPriorityHasDebt(w.durable)); CHECK(!g_externalPriorityVirtualSuspended.load());
        CHECK(g_externalPriority.state == "off"); CHECK(w.stops == 1);
    });
    run("R2 shutdown returns owned configuration while takeover child remains pending", [] {
        reset(); step(1000); step(1500); step(2000); w.now = 10500;
        const auto child = g_externalPriority.cycleProcess; externalControllerPriorityShutdown();
        CHECK(w.visibility.hidden.empty()); CHECK(w.visibility.applications.empty()); CHECK(!w.visibility.cloaking);
        CHECK(w.durable.refreshPending); CHECK(g_externalPriority.cycleProcess == child); CHECK(w.launches == 1);
    });
    run("R2 settings read exception cannot escape idle maintenance", [] {
        reset(false); w.settingsThrow = true; bool escaped = false;
        try { step(1000); } catch (const std::exception&) { escaped = true; }
        CHECK(!escaped); CHECK(noMutations()); CHECK(w.pnpQueries == 0);
        CHECK(!g_externalPriorityVirtualSuspended.load()); CHECK(!externalPriorityHasDebt(w.durable));
        CHECK(g_externalPrioritySourceMask.load() == 0); CHECK(w.settingsReads == 1);
        step(1500); step(2000); step(3500); CHECK(w.settingsReads == 1); CHECK(w.loads == 0);
        w.settingsThrow = false; step(5000); CHECK(!g_externalPriority.enabled); CHECK(noMutations()); CHECK(w.loads == 1);
    });
    run("R2 settings read exception cannot strand active return duty", [] {
        reset(); takeover(); w.settingsThrow = true; bool escaped = false;
        try { step(3000); } catch (const std::exception&) { escaped = true; }
        CHECK(!escaped); CHECK(w.durable.refreshPending); CHECK(g_externalPriorityVirtualSuspended.load());
        CHECK(w.stops == 1); w.settingsThrow = false; w.now = 6000; beginReturn();
        step(w.now + 500); CHECK(w.visibility.hidden.empty()); CHECK(w.durable.refreshPending);
    });
    run("R2 unavailable CLI before WAL preserves selected plane and Host", [] {
        reset(); w.snapshotOk = false; const auto settings = w.settings;
        for (ULONGLONG time = 1000; time <= 11000; time += 500) step(time);
        CHECK(w.snapshots > 0); CHECK(noMutations()); CHECK(w.host); CHECK(w.visibility.hidden.empty());
        CHECK(w.visibility.applications.empty()); CHECK(!w.visibility.cloaking);
        CHECK(w.refreshes == 0); CHECK(w.resets == 0); CHECK(!g_externalPriorityVirtualSuspended.load());
        CHECK(g_externalPrioritySourceMask.load() == 0); CHECK(!externalPriorityHasDebt(w.durable));
        CHECK(w.settings == settings); CHECK(g_externalPriority.admittedExternal.root.empty());
    });
    run("R2 persistent mode1902 to1901 debt survives two launches and exact port return", [] {
        reset(); w.onStop = [] { w.devices.front() = device(mode1Root, builtinLocation, 0); }; takeover();
        CHECK(w.durable.builtinRoot == mode1Root); CHECK(w.durable.locationPath == builtinLocation);
        const auto debt = w.durable; const auto visibility = w.visibility;
        reset(false); w.host = false; w.devices.front() = device(mode1Root, builtinLocation, 0);
        w.durable = debt; w.visibility = visibility; step(1000);
        CHECK(w.visibility.hidden.empty()); CHECK(w.visibility.applications.empty()); CHECK(!w.visibility.cloaking);
        CHECK(w.durable.refreshPending); CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(w.launches == 1);
        CHECK(w.events[eventIndex("cycle:")].find(mode1Root) != std::string::npos);
        CHECK(w.events[eventIndex("cycle:")].find(builtinLocation) != std::string::npos);
        complete(); externalControllerPriorityShutdown(); CHECK(!externalPriorityHasDebt(w.durable));
        const auto clean = w.durable;
        reset(false); w.durable = clean; step(1000); step(10000); externalControllerPriorityShutdown();
        CHECK(noMutations()); CHECK(w.pnpQueries == 0); CHECK(w.snapshots == 0); CHECK(w.host);
        CHECK(!g_externalPriorityVirtualSuspended.load()); CHECK(g_externalPriority.state == "off");
    });
    run("R2 source publication blocks on consumer mutex and rechecks context inside lock", [] {
        for (unsigned scenario = 0; scenario < 4; ++scenario) {
            reset();
            const ep::Candidate candidate{externalRoot, externalLocation, 3, true, true, true,
                g_xinputTopologyEpoch.load(), w.power};
            g_externalPrioritySourceMask = 1; g_externalPrioritySourceDeadline = 2222;
            g_externalPrioritySourceTopology = candidate.topology; g_externalPrioritySourcePower = candidate.power;
            std::unique_lock<std::mutex> consumer(g_gamepadReadMx);
            std::promise<void> entered; auto start = entered.get_future();
            std::promise<bool> published; auto result = published.get_future();
            // Only the helper runs on the second thread. World is immutable until join.
            std::thread worker([&] {
                entered.set_value();
                try { published.set_value(externalPriorityPublishSource(candidate, 4000)); }
                catch (...) { published.set_exception(std::current_exception()); }
            });
            start.wait();
            const bool blocked = result.wait_for(std::chrono::milliseconds(150)) == std::future_status::timeout;
            const bool unchanged = g_externalPrioritySourceMask.load() == 1 &&
                g_externalPrioritySourceDeadline.load() == 2222 &&
                g_externalPrioritySourceTopology.load() == candidate.topology &&
                g_externalPrioritySourcePower.load() == candidate.power;
            if (scenario == 1) ++g_xinputTopologyEpoch;
            if (scenario == 2) g_offlinePowerOverride = candidate.power + 1;
            if (scenario == 3) g_powerLifecycle = PowerLifecycle::Suspended;
            consumer.unlock(); worker.join(); const bool accepted = result.get();
            CHECK(blocked); CHECK(unchanged); CHECK(accepted == (scenario == 0));
            if (scenario == 0) {
                CHECK(g_externalPrioritySourceMask.load() == 8); CHECK(g_externalPrioritySourceDeadline.load() == 5200);
                CHECK(g_externalPrioritySourceTopology.load() == candidate.topology);
                CHECK(g_externalPrioritySourcePower.load() == candidate.power);
            } else {
                CHECK(g_externalPrioritySourceMask.load() == 1); CHECK(g_externalPrioritySourceDeadline.load() == 2222);
                CHECK(g_externalPrioritySourceTopology.load() == candidate.topology);
                CHECK(g_externalPrioritySourcePower.load() == candidate.power);
            }
        }
    });
    run("R2 active maintenance uses consumer mutex for source publication", [] {
        reset(); takeover(); const auto beforeMask = g_externalPrioritySourceMask.load();
        const auto beforeDeadline = g_externalPrioritySourceDeadline.load();
        std::unique_lock<std::mutex> consumer(g_gamepadReadMx);
        std::promise<void> sampled; auto sample = sampled.get_future();
        std::promise<void> finished; auto result = finished.get_future();
        w.onReverse = [&] { sampled.set_value(); };
        // Main thread touches only atomics and synchronization until worker joins.
        std::thread worker([&] {
            try { step(3000); finished.set_value(); }
            catch (...) { finished.set_exception(std::current_exception()); }
        });
        const bool reached = sample.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        const bool blocked = result.wait_for(std::chrono::milliseconds(150)) == std::future_status::timeout;
        const bool unchanged = g_externalPrioritySourceMask.load() == beforeMask &&
            g_externalPrioritySourceDeadline.load() == beforeDeadline;
        ++g_xinputTopologyEpoch; consumer.unlock(); worker.join(); result.get();
        CHECK(reached); CHECK(blocked); CHECK(unchanged);
        CHECK(g_externalPrioritySourceDeadline.load() == beforeDeadline);
        CHECK(g_externalPrioritySourceTopology.load() != g_xinputTopologyEpoch.load());
        CHECK(w.stops == 1); CHECK(w.launches == 1);
    });
    run("R2 stable active replacement candidate vanishes before selection no phantom source", [] {
        reset(); takeover(); constexpr const char* replacement = "USB\\VID_046D&PID_C21D\\REPLACEMENT";
        w.devices.push_back(device(replacement, "PCIROOT(0)#USBROOT(0)#USB(5)", 1, 1));
        w.devices[1].fresh = false; ++g_xinputTopologyEpoch;
        for (ULONGLONG time = 3000; time <= 5500; time += 500) step(time);
        CHECK(g_externalPriority.externalRoot == externalRoot);
        bool vanished = false;
        w.onReverse = [&] { vanished = true; w.devices.pop_back(); };
        step(6000); CHECK(vanished); CHECK(byRoot(replacement) == nullptr);
        CHECK(g_externalPriority.externalRoot != replacement);
        CHECK(g_externalPrioritySourceMask.load() != 2 || g_externalPrioritySourceDeadline.load() < w.now);
        CHECK(w.stops == 1); CHECK(w.launches == 1);
    });
    run("R2 settings fault after WAL cannot strand owned configuration", [] {
        reset(); w.onSave = [] { w.settingsThrow = true; }; bool escaped = false;
        step(1000); step(1500);
        try { step(2000); } catch (const std::exception&) { escaped = true; }
        CHECK(!escaped); CHECK(w.durable.refreshPending); CHECK(!w.durable.hiddenPaths.empty());
        CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(w.stops == 1); CHECK(w.launches == 0);
        w.settingsThrow = false; w.settings["ownership"]["externalControllerPriority"] = false;
        step(6000); step(6500);
        CHECK(w.durable.hiddenPaths.empty()); CHECK(w.durable.applications.empty());
        CHECK(w.durable.refreshPending); CHECK(w.launches == 1); CHECK(g_externalPriority.cycleRestoring);
    });
    run("R2 incomplete current board authority cannot mutate hide or Host", [] {
        for (unsigned scenario = 0; scenario < 4; ++scenario) {
            reset();
            if (scenario == 0) g_machineIdentity.resolved = false;
            if (scenario == 1) g_machineIdentity.inputHidKey.clear();
            if (scenario == 2) g_machineIdentity.deviceClass.clear();
            if (scenario == 3) { w.board = false; w.devices.front().portKind = -1; }
            for (ULONGLONG time = 1000; time <= 11000; time += 500) step(time);
            CHECK(noMutations()); CHECK(w.host); CHECK(!g_externalPriorityVirtualSuspended.load());
            CHECK(w.visibility.hidden.empty()); CHECK(w.visibility.applications.empty()); CHECK(!w.visibility.cloaking);
            if (scenario < 3) CHECK(g_externalPrioritySourceMask.load() == 0);
        }
    });
    run("R2 settings fault during same port arrival starts bounded owned return", [] {
        reset(); w.onStop = [] { w.devices.erase(w.devices.begin()); };
        step(1000); step(1500); step(2000);
        CHECK(g_externalPriority.takingOver); CHECK(g_externalPriorityVirtualSuspended.load());
        CHECK(!externalPriorityHasDebt(w.durable)); CHECK(w.stops == 1); CHECK(w.launches == 0);
        w.settingsThrow = true; bool escaped = false;
        try { step(2500); } catch (const std::exception&) { escaped = true; }
        CHECK(!escaped); CHECK(g_externalPriority.returning); CHECK(!g_externalPriority.takingOver);
        CHECK(g_externalPriorityVirtualSuspended.load()); CHECK(g_externalPrioritySourceMask.load() == 0);
        w.settingsThrow = false; step(6000);
        CHECK(!g_externalPriority.returning); CHECK(!g_externalPriorityVirtualSuspended.load());
        CHECK(!externalPriorityHasDebt(w.durable)); CHECK(w.stops == 1); CHECK(w.launches == 0);
    });
    run("R2 enabled takeover timeout late child success cannot reactivate or overlap cycles", [] {
        reset(); step(1000); step(1500); step(2000);
        const auto child = g_externalPriority.cycleProcess; CHECK(child != nullptr);
        step(10000); CHECK(g_externalPriority.enabled); CHECK(g_externalPriorityPolicyEnabled.load());
        CHECK(g_externalPriority.returning); CHECK(!g_externalPriority.active);
        CHECK(w.visibility.hidden.empty()); CHECK(w.visibility.applications.empty()); CHECK(!w.visibility.cloaking);
        CHECK(w.durable.hiddenPaths.empty()); CHECK(w.durable.refreshPending);
        CHECK(g_externalPriority.cycleProcess == child); CHECK(w.launches == 1);
        CHECK(g_externalPrioritySourceMask.load() == 0); CHECK(g_externalPriorityVirtualSuspended.load());
        step(10500); step(11000); step(12500); step(13000);
        CHECK(g_externalPriority.cycleProcess == child); CHECK(w.launches == 1); CHECK(w.stops == 1);
        complete(); step(13500);
        CHECK(!g_externalPriority.active); CHECK(g_externalPriority.returning);
        CHECK(!g_externalPriority.cycleReturnVerified); CHECK(w.launches == 1);
        CHECK(g_externalPrioritySourceMask.load() == 0); CHECK(w.durable.refreshPending);
        step(16000); CHECK(g_externalPriority.cycleProcess != nullptr);
        CHECK(g_externalPriority.cycleProcess != child); CHECK(g_externalPriority.cycleRestoring);
        CHECK(w.launches == 2); CHECK(!g_externalPriority.active); CHECK(w.durable.refreshPending);
        complete(); step(16500); step(19000);
        CHECK(!externalPriorityHasDebt(w.durable)); CHECK(!g_externalPriorityVirtualSuspended.load());
        CHECK(g_externalPriority.enabled); CHECK(g_externalPriorityPolicyEnabled.load()); CHECK(!g_externalPriority.active);
        step(19500); step(20000); step(20500);
        CHECK(w.stops == 1); CHECK(w.launches == 2); CHECK(!g_externalPriority.active);
    });
    run("R2 policy atomic defaults OFF for absent malformed or unreadable settings", [] {
        for (unsigned scenario = 0; scenario < 4; ++scenario) {
            reset(false);
            if (scenario == 0) w.settings = json::object();
            if (scenario == 1) w.settings = {{"ownership", {{"externalControllerPriority", "true"}}}};
            if (scenario == 2) w.settingsThrow = true;
            if (scenario == 3) w.loadOk = false;
            step(1000); CHECK(!g_externalPriorityPolicyEnabled.load()); CHECK(!g_externalPriority.enabled);
            bool enabled = true;
            { std::lock_guard<std::mutex> owner(g_externalPriorityMx);
              enabled = snapshotOnReaderThread()["enabled"].get<bool>(); }
            CHECK(!enabled); CHECK(noMutations()); CHECK(w.pnpQueries == 0);
            if (scenario != 3) CHECK(!g_externalPriorityVirtualSuspended.load());
            step(1500); step(3500); CHECK(!g_externalPriorityPolicyEnabled.load()); CHECK(noMutations());
        }
    });
    run("R2 policy atomic retains last confirmed preference through settings exception", [] {
        for (const bool enabled : {false, true}) {
            reset(enabled); step(1000); CHECK(g_externalPriorityPolicyEnabled.load() == enabled);
            CHECK(g_externalPriority.enabled == enabled);
            w.settings["ownership"]["externalControllerPriority"] = !enabled; w.settingsThrow = true;
            step(1500); CHECK(g_externalPriorityPolicyEnabled.load() == enabled); CHECK(g_externalPriority.enabled == enabled);
            bool reported = !enabled;
            { std::lock_guard<std::mutex> owner(g_externalPriorityMx);
              reported = snapshotOnReaderThread()["enabled"].get<bool>(); }
            CHECK(reported == enabled); CHECK(noMutations()); CHECK(!g_externalPriorityVirtualSuspended.load());
            w.settingsThrow = false; step(5000);
            CHECK(g_externalPriorityPolicyEnabled.load() == !enabled); CHECK(g_externalPriority.enabled == !enabled);
            { std::lock_guard<std::mutex> owner(g_externalPriorityMx);
              reported = snapshotOnReaderThread()["enabled"].get<bool>(); }
            CHECK(reported == !enabled); CHECK(noMutations()); CHECK(!g_externalPriorityVirtualSuspended.load());
        }
    });
    std::cout << "EXTERNALPAD_RUNTIME_SUMMARY passed=" << passed << " total=" << total << " assertions=" << assertions
              << " hardwareAccess=false productRun=false productionHeader=true operations=faked-external-effects ledgerReads=private-temp-fixtures\n";
    return passed == total ? 0 : 1;
}
