#pragma once
#include <set>
// HC ISpaceWatcher parity, with durable original-state leases and no new worker.
// Included only after MachineIdentity/kOemBit*/json and serial-worker declarations.
#ifndef YMCC_VENDOR_STACK_FIXTURE
#include <taskschd.h>
#pragma comment(lib, "taskschd.lib")
#endif
#ifndef YMCC_VENDOR_STACK_FIXTURE
#include "msi_service_observation.h"
#else
static json msiShieldReadServiceInventory();
static json msiShieldReadProtectedService();
#endif

struct OemVendorServiceLease {
    std::wstring name;
    DWORD originalStartType = SERVICE_DEMAND_START;
    bool wasRunning = false;
    DWORD originalState = 0; // R13 schema1 infers Running/Stopped from wasRunning.
    bool returnPrepared = false; // Durable authority for our partial return steps.
};
struct OemVendorTaskLease { std::wstring name; bool wasEnabled = false, wasRunning = false, returnPrepared = false, removePrepared = false; };
struct OemVendorTaskFacts { bool exists = false, enabled = false, running = false; };
static std::mutex g_oemVendorStackMx, g_oemVendorStackIoMx;
static std::vector<OemVendorServiceLease> g_oemVendorServiceLeases;
static std::vector<OemVendorTaskLease> g_oemVendorTaskLeases;
// Full task paths discovered on an ordinary request; wake verification reuses
// them instead of rescanning the entire scheduler every two seconds.
static std::vector<std::wstring> g_oemVendorTaskPaths;
static bool g_oemVendorTaskDiscoveryComplete = false;
static constexpr size_t kOemVendorMaxTaskPaths = 64;
static bool g_oemVendorStackWanted = false, g_oemVendorStackOpQueued = false;
// Two independent owners feed the same reversible lease. The existing shortcut
// path remains the default owner; the explicit MSI controller-only shield is a
// second owner so a settings refresh cannot accidentally clear it.
static bool g_oemVendorShortcutWanted = false;
static bool g_oemVendorRecordingWanted = false; // Temporary listener lease, independent of saved rules.
static bool g_oemVendorShieldWanted = false;
static bool g_oemVendorMsiTaskShieldPreferenceSeen = false;
static bool g_oemVendorStackIntentSeen = false, g_oemVendorStackStartupRecovered = false;
static YmccFamily g_oemVendorStackIntentFamily = YmccFamily::Unknown;
static unsigned long long g_oemVendorStackRequested = 1, g_oemVendorStackCompleted = 0;
static bool g_oemVendorStackIntentControllerOnly = false;
static bool g_oemVendorStackLastOutcomeKnown = false, g_oemVendorStackLastOutcomeOk = false;
static unsigned long long g_oemVendorStackLastOutcomeEpoch = 0;
static const char* g_oemVendorStackLastOutcome = "none";
static bool g_oemVendorStackLoaded = false;
static std::atomic<bool> g_oemVendorStackJournalValid{true};
static json g_oemVendorMsiServiceInventory = json::object();
static json g_oemVendorMsiProtectedServiceObservation = json::object();
static unsigned g_oemVendorStackAttempts = 0;
static ULONGLONG g_oemVendorStackRetryAt = 0;
static constexpr DWORD kOemVendorServiceBudgetMs = 10000;
static constexpr unsigned kOemVendorMaxAttempts = 3;
static constexpr ULONGLONG kOemVendorShieldWakeWindowMs = 20000;
static constexpr ULONGLONG kOemVendorShieldWakeIntervalMs = 2000;
struct OemVendorShieldWakeGuard {
    unsigned long long generation = 0, epoch = 0;
    ULONGLONG until = 0, next = 0;
};
// Protected by g_oemVendorStackMx. Uses the existing serial worker and 400ms
// settings refresh: no extra thread/timer and no controller-admission changes.
static OemVendorShieldWakeGuard g_oemVendorShieldWake;
static bool oemVendorStackShieldWakeAliveLocked(ULONGLONG now) {
    return g_oemVendorShieldWake.until && now < g_oemVendorShieldWake.until &&
        g_oemVendorShieldWanted && !g_exitRequested.load(std::memory_order_acquire) &&
        msiControllerShieldWakePowerCurrent(g_oemVendorShieldWake.generation);
}
static void oemVendorStackShieldWakeRefreshLocked(ULONGLONG now) {
    if (!g_oemVendorShieldWake.until || oemVendorStackShieldWakeAliveLocked(now)) return;
    const char* reason = !g_oemVendorShieldWanted ? "disabled" :
        g_exitRequested.load(std::memory_order_acquire) ? "exit" :
        !msiControllerShieldWakePowerCurrent(g_oemVendorShieldWake.generation) ? "power-changed" : "elapsed";
    g_oemVendorShieldWake.until = 0;
    // Retire only this guard's pending pass. Never cancel a newer settings
    // request or restore the lease just because the observation window ended.
    if (g_oemVendorShieldWake.epoch == g_oemVendorStackRequested &&
        g_oemVendorStackCompleted != g_oemVendorStackRequested) {
        g_oemVendorStackCompleted = g_oemVendorStackRequested;
        g_oemVendorStackRetryAt = 0;
        g_oemVendorStackLastOutcomeKnown = true;
        g_oemVendorStackLastOutcomeOk = false;
        g_oemVendorStackLastOutcomeEpoch = g_oemVendorStackRequested;
        g_oemVendorStackLastOutcome = "wake-guard-ended-unconfirmed";
    }
    appendNativeLifecycleLog("oem-vendor-stack", {{"state", "wake-guard-ended"},
        {"generation", g_oemVendorShieldWake.generation}, {"reason", reason}});
}
static void oemVendorStackShieldWakeRequestLocked() {
    g_oemVendorShieldWake.epoch = ++g_oemVendorStackRequested;
    g_oemVendorStackAttempts = 0;
    g_oemVendorStackRetryAt = 0;
    g_oemVendorStackLastOutcomeKnown = false;
    g_oemVendorStackLastOutcome = "pending";
}
static std::vector<std::wstring> oemVendorServiceNamesForFamily(YmccFamily family) {
    switch (family) {
    case YmccFamily::AsusRogAllyX: case YmccFamily::AsusRogAlly:
    case YmccFamily::AsusRogAllyClassic: case YmccFamily::AsusRogAllyXClassic:
        return {L"ArmouryCrateSEService", L"AsusAppService", L"ArmouryCrateControlInterface"};
    case YmccFamily::LenovoLegionGo: return {L"DAService"};
    // MSI Foundation Service is conservatively protected: its controller role is not yet proven.
    // The MSI frontend shield must never disable it; MSI task/process targets
    // are handled below without inventing a second service name.
    case YmccFamily::MsiClaw: return {};
    case YmccFamily::ZotacGamingZone: return {L"ZotacHandheldDatabaseService", L"ZotacHandheldService"};
    default: return {};
    }
}
static std::vector<std::wstring> oemVendorTaskNamesForFamily(YmccFamily family) {
    return family == YmccFamily::MsiClaw
        ? std::vector<std::wstring>{L"MSI_Center_M_Server", L"MSI_Center_M_Updater"} : std::vector<std::wstring>{};
}
// Task names are an exact allowlist. A scheduler subfolder is part of the
// leased identity, never a reason to broaden that allowlist to other MSI tasks.
static bool oemVendorTaskPathSafe(const std::wstring& path, bool folder = false) {
    if (path.empty() || path.size() > 1024 || path.find(L'/') != std::wstring::npos) return false;
    if (folder && path == L"\\") return true;
    size_t start = path.front() == L'\\' ? 1 : 0;
    if (folder && start != 1) return false;
    for (wchar_t c : path) if (c < 32) return false;
    while (start < path.size()) {
        const auto end = path.find(L'\\', start);
        const auto segment = path.substr(start, end == std::wstring::npos ? end : end - start);
        if (segment.empty() || segment == L"." || segment == L"..") return false;
        if (end == std::wstring::npos) return true;
        start = end + 1;
    }
    return false;
}
static bool oemVendorTaskPathAllowed(const std::wstring& path, const std::vector<std::wstring>& names) {
    if (!oemVendorTaskPathSafe(path)) return false;
    const auto split = path.find_last_of(L'\\');
    // Legacy journal entries used only the root task's name.
    if (split != std::wstring::npos && path.front() != L'\\') return false;
    const auto leaf = path.substr(split == std::wstring::npos ? 0 : split + 1);
    return std::any_of(names.begin(), names.end(), [&](const auto& n) { return _wcsicmp(leaf.c_str(), n.c_str()) == 0; });
}
static bool oemVendorTaskPathEqual(const std::wstring& a, const std::wstring& b) {
    const wchar_t* left = a.c_str() + (!a.empty() && a.front() == L'\\');
    const wchar_t* right = b.c_str() + (!b.empty() && b.front() == L'\\');
    return _wcsicmp(left, right) == 0;
}
struct OemVendorTaskFolderFacts { std::vector<std::wstring> tasks, folders; };
struct OemVendorTaskDiscovery {
    std::vector<std::wstring> paths;
    size_t foldersRead = 0, tasksRead = 0;
    DWORD error = ERROR_SUCCESS;
    bool complete = true;
};
template<class ReadFolder>
static OemVendorTaskDiscovery oemVendorTaskDiscoverWithReader(const std::vector<std::wstring>& names, ReadFolder readFolder) {
    OemVendorTaskDiscovery result;
    if (names.empty()) return result;
    struct Pending { std::wstring path; unsigned depth; };
    std::vector<Pending> pending{{L"\\", 0}};
    std::vector<std::wstring> visited;
    const auto deadline = GetTickCount64() + 3000; // Soft budget between scheduler calls.
    auto fail = [&](DWORD error) { result.complete = false; if (!result.error) result.error = error; };
    while (!pending.empty()) {
        if (visited.size() >= 256 || GetTickCount64() >= deadline) { fail(ERROR_TIMEOUT); break; }
        auto current = std::move(pending.back()); pending.pop_back();
        if (std::any_of(visited.begin(), visited.end(), [&](const auto& p) { return oemVendorTaskPathEqual(p, current.path); })) continue;
        visited.push_back(current.path);
        OemVendorTaskFolderFacts facts;
        if (!readFolder(current.path, facts)) { const auto error = GetLastError(); fail(error ? error : ERROR_GEN_FAILURE); }
        ++result.foldersRead;
        for (const auto& path : facts.tasks) {
            if (++result.tasksRead > 4096) { fail(ERROR_BUFFER_OVERFLOW); break; }
            const auto split = path.find_last_of(L'\\');
            const auto parent = split == std::wstring::npos ? L"\\" : split == 0 ? L"\\" : path.substr(0, split);
            if (!oemVendorTaskPathAllowed(path, names) || !oemVendorTaskPathEqual(parent, current.path)) continue;
            if (std::any_of(result.paths.begin(), result.paths.end(), [&](const auto& p) { return oemVendorTaskPathEqual(p, path); })) continue;
            if (result.paths.size() >= kOemVendorMaxTaskPaths) { fail(ERROR_BUFFER_OVERFLOW); break; }
            // Keep old root keys stable for journal migration; nested identities
            // retain their absolute path through query, disable and restoration.
            result.paths.push_back(split == 0 ? path.substr(1) : path);
        }
        if (result.tasksRead > 4096 || result.error == ERROR_BUFFER_OVERFLOW) break;
        for (const auto& path : facts.folders) {
            const auto split = path.find_last_of(L'\\');
            const auto parent = split == 0 ? L"\\" : path.substr(0, split);
            if (!oemVendorTaskPathSafe(path, true) || path == L"\\" || !oemVendorTaskPathEqual(parent, current.path)) { fail(ERROR_INVALID_DATA); continue; }
            if (current.depth >= 16 || pending.size() + visited.size() >= 256) { fail(ERROR_BUFFER_OVERFLOW); continue; }
            pending.push_back({path, current.depth + 1});
        }
    }
    // Preserve per-name absence/readback receipts when a target is not installed.
    for (const auto& name : names) {
        const bool found = std::any_of(result.paths.begin(), result.paths.end(), [&](const auto& p) {
            const auto split = p.find_last_of(L'\\');
            return _wcsicmp(p.c_str() + (split == std::wstring::npos ? 0 : split + 1), name.c_str()) == 0;
        });
        if (!found && result.paths.size() < kOemVendorMaxTaskPaths) result.paths.push_back(name);
    }
    return result;
}

static std::vector<std::wstring> oemVendorProcessNamesForFamily(YmccFamily family) {
    switch (family) {
    case YmccFamily::LenovoLegionGo: return {L"LegionGoQuickSettings", L"LegionSpace", L"LSDaemon"};
    // Do not terminate MCMOSDInfo: it is used by fan/temperature telemetry.
    // This MSI process policy also contains official key/control bridges; it
    // is not a proof of preserving every OEM input path. It is shared by OEM-key
    // takeover and the explicit controller shield.
    case YmccFamily::MsiClaw: return {L"MSI_Center_M_Server", L"MSI Center M", L"MSI_Center_M_Launcher", L"MSI_Center_M_Server_ControlMode"};
    case YmccFamily::ZotacGamingZone: return {L"ZotacHandheldQuickSetting"};
    default: return {}; // HC declares no ROG executable names. Never guess one.
    }
}
// The explicit MSI shield uses the same exact task/process lease mechanism.
// MCMOSDInfo and its unverified OSD Info executable alias are
// protected by default; no unverified service is stopped because the only
// known MSI service has no validated controller-only dependency separation.
static std::vector<std::wstring> oemVendorShieldProcessNamesForFamily(YmccFamily family) {
    auto names = oemVendorProcessNamesForFamily(family);
    if (family == YmccFamily::MsiClaw) names.push_back(L"MSI Center M Updater");
    return names;
}
// Missing preference defaults on. A saved false remains authoritative across
// upgrades/restarts; malformed values must neither enable isolation nor throw
// from the shortcut refresh path. Machine/model admission stays in ShieldRequest.
static bool msiControllerShieldRequested(const json& input) {
    if (!input.is_object()) return false;
    const auto setting = input.find("msiControllerShieldEnabled");
    return setting == input.end() || (setting->is_boolean() && setting->get<bool>());
}

static bool oemVendorShieldWantedSnapshot() {
    std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
    return g_oemVendorShieldWanted;
}
static bool oemVendorEffectiveWanted(bool shortcutWanted, bool shieldWanted) {
    return shortcutWanted || shieldWanted;
}
static bool oemVendorStackFamilySupported(YmccFamily family) {
    return !oemVendorServiceNamesForFamily(family).empty() ||
        !oemVendorTaskNamesForFamily(family).empty() ||
        !oemVendorProcessNamesForFamily(family).empty();
}
static bool oemVendorStackMachineSupported() {
    const auto& cls = g_machineIdentity.deviceClass;
    auto is = [&](const char* name) { return cls == std::string("HandheldCompanion.Devices.") + name; };
    switch (g_machineIdentity.family) {
    case YmccFamily::AsusRogAllyX: case YmccFamily::AsusRogAlly:
    case YmccFamily::AsusRogAllyClassic: case YmccFamily::AsusRogAllyXClassic:
        return is("XboxROGAllyX") || is("XboxROGAlly") || is("ROGAllyX") || is("ROGAlly");
    case YmccFamily::LenovoLegionGo:
        return is("LegionGoTablet") || is("LegionGoTablet2") || is("LegionGoSZ1") || is("LegionGoSZ2");
    case YmccFamily::MsiClaw:
        return msiClawKnownBoardAuthority() &&
            (is("ClawA1M") || is("ClawA2VM") || is("ClawBZ2EM") || is("ClawCG3EM"));
    case YmccFamily::ZotacGamingZone: return is("GamingZone");
    default: return false;
    }
}
static uint64_t oemVendorStackFrontMask(YmccFamily family) {
    switch (family) {
    case YmccFamily::AsusRogAllyX: case YmccFamily::AsusRogAlly:
    case YmccFamily::AsusRogAllyClassic: case YmccFamily::AsusRogAllyXClassic:
        return kOemBitCc | kOemBitAc | kOemBitLib;
    case YmccFamily::LenovoLegionGo: return kOemBitLegionFrontL | kOemBitLegionFrontR;
    case YmccFamily::MsiClaw: return kOemBitClaw | kOemBitQs;
    case YmccFamily::ZotacGamingZone: return kOemBitZotac | kOemBitDots;
    default: return 0;
    }
}
static bool oemVendorStackRuleCanTakeover(YmccFamily family, uint64_t mask,
                                          bool noDesktopKeys, const std::string& trigger) {
    if (!noDesktopKeys || !mask || (mask & (mask - 1)) || !(mask & oemVendorStackFrontMask(family))) return false;
    if (family == YmccFamily::AsusRogAllyX || family == YmccFamily::AsusRogAlly ||
        family == YmccFamily::AsusRogAllyClassic || family == YmccFamily::AsusRogAllyXClassic ||
        family == YmccFamily::MsiClaw) return trigger == "press" || trigger == "double";
    return true;
}
static bool oemVendorServiceQueryConfig(SC_HANDLE service, DWORD& type) {
    DWORD bytes = 0;
    QueryServiceConfigW(service, nullptr, 0, &bytes);
    if (!bytes || bytes > 1024 * 1024) return false;
    std::vector<BYTE> buffer(bytes);
    auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
    if (!QueryServiceConfigW(service, config, bytes, &bytes)) return false;
    type = config->dwStartType;
    return true;
}
static bool oemVendorServiceQueryState(SC_HANDLE service, DWORD& state) {
    SERVICE_STATUS_PROCESS status{}; DWORD bytes = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytes)) return false;
    state = status.dwCurrentState;
    return true;
}
static bool oemVendorServicePending(DWORD state) {
    return state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING ||
        state == SERVICE_CONTINUE_PENDING || state == SERVICE_PAUSE_PENDING;
}
static bool oemVendorServiceWait(SC_HANDLE service, DWORD wanted, DWORD budgetMs) {
    const auto deadline = GetTickCount64() + budgetMs;
    for (;;) {
        DWORD state = 0;
        if (!oemVendorServiceQueryState(service, state)) return false;
        if (state == wanted) return true;
        if (GetTickCount64() >= deadline) { SetLastError(ERROR_TIMEOUT); return false; }
        Sleep(50);
    }
}
static bool oemVendorServiceStable(SC_HANDLE service, DWORD& state) {
    const auto deadline = GetTickCount64() + kOemVendorServiceBudgetMs;
    while (oemVendorServiceQueryState(service, state)) {
        if (!oemVendorServicePending(state)) {
            if (state == SERVICE_STOPPED || state == SERVICE_RUNNING || state == SERVICE_PAUSED) return true;
            SetLastError(ERROR_INVALID_DATA); return false;
        }
        if (GetTickCount64() >= deadline) { SetLastError(ERROR_TIMEOUT); return false; }
        Sleep(50);
    }
    return false;
}
static bool oemVendorStackIntentCurrent(unsigned long long epoch) {
    if (g_exitRequested.load(std::memory_order_acquire)) return false;
    std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
    return g_oemVendorStackWanted && epoch == g_oemVendorStackRequested &&
        (epoch != g_oemVendorShieldWake.epoch || oemVendorStackShieldWakeAliveLocked(GetTickCount64()));
}

#ifndef YMCC_VENDOR_STACK_FIXTURE
// A single bounded read-only walk covers hidden tasks and scheduler subfolders.
// Mutation still uses only a discovered allowlisted full path and the old lease.
static OemVendorTaskDiscovery oemVendorTaskDiscover(const std::vector<std::wstring>& names) {
    if (names.empty()) return {};
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return {{}, 0, 0, static_cast<DWORD>(initialized), false};
    struct ComScope { bool own; ~ComScope() { if (own) CoUninitialize(); } } scope{SUCCEEDED(initialized)};
    ComPtr<ITaskService> service;
    VARIANT empty{}; VariantInit(&empty);
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service));
    if (SUCCEEDED(hr)) hr = service->Connect(empty, empty, empty, empty);
    if (FAILED(hr)) return {{}, 0, 0, static_cast<DWORD>(hr), false};
    return oemVendorTaskDiscoverWithReader(names, [&](const std::wstring& path, OemVendorTaskFolderFacts& facts) {
        ComPtr<ITaskFolder> folder;
        BSTR text = SysAllocString(path.c_str());
        if (!text) { SetLastError(ERROR_OUTOFMEMORY); return false; }
        HRESULT result = service->GetFolder(text, &folder); SysFreeString(text);
        ComPtr<IRegisteredTaskCollection> tasks; ComPtr<ITaskFolderCollection> folders;
        if (SUCCEEDED(result)) result = folder->GetTasks(TASK_ENUM_HIDDEN, &tasks);
        LONG count = 0;
        if (SUCCEEDED(result)) result = tasks->get_Count(&count);
        if (SUCCEEDED(result) && (count < 0 || count > 4096)) result = HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        for (LONG index = 1; SUCCEEDED(result) && index <= count; ++index) {
            VARIANT key{}; VariantInit(&key); key.vt = VT_I4; key.lVal = index;
            ComPtr<IRegisteredTask> task; result = tasks->get_Item(key, &task);
            BSTR taskPath = nullptr;
            if (SUCCEEDED(result)) result = task->get_Path(&taskPath);
            if (SUCCEEDED(result) && taskPath) facts.tasks.emplace_back(taskPath, SysStringLen(taskPath));
            SysFreeString(taskPath);
        }
        if (SUCCEEDED(result)) result = folder->GetFolders(0, &folders);
        if (SUCCEEDED(result)) result = folders->get_Count(&count);
        if (SUCCEEDED(result) && (count < 0 || count > 256)) result = HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        for (LONG index = 1; SUCCEEDED(result) && index <= count; ++index) {
            VARIANT key{}; VariantInit(&key); key.vt = VT_I4; key.lVal = index;
            ComPtr<ITaskFolder> child; result = folders->get_Item(key, &child);
            BSTR folderPath = nullptr;
            if (SUCCEEDED(result)) result = child->get_Path(&folderPath);
            if (SUCCEEDED(result) && folderPath) facts.folders.emplace_back(folderPath, SysStringLen(folderPath));
            SysFreeString(folderPath);
        }
        if (FAILED(result)) SetLastError(static_cast<DWORD>(result));
        return SUCCEEDED(result);
    });
}

// COM is scoped to this existing worker job, not retained as a new task thread.
static bool oemVendorTaskAccess(const std::wstring& name,
                                const std::function<bool(IRegisteredTask*)>& action, bool& exists) {
    exists = false;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) { SetLastError(static_cast<DWORD>(initialized)); return false; }
    struct ComScope { bool own; ~ComScope() { if (own) CoUninitialize(); } } scope{SUCCEEDED(initialized)};
    ComPtr<ITaskService> service; ComPtr<ITaskFolder> folder; ComPtr<IRegisteredTask> task;
    VARIANT empty{}; VariantInit(&empty);
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service));
    if (SUCCEEDED(hr)) hr = service->Connect(empty, empty, empty, empty);
    BSTR path = SysAllocString(L"\\");
    if (!path) { SetLastError(ERROR_OUTOFMEMORY); return false; }
    if (SUCCEEDED(hr)) hr = service->GetFolder(path, &folder);
    SysFreeString(path); path = SysAllocString(name.c_str());
    if (!path) { SetLastError(ERROR_OUTOFMEMORY); return false; }
    if (SUCCEEDED(hr)) hr = folder->GetTask(path, &task);
    SysFreeString(path);
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) return true;
    if (FAILED(hr)) { SetLastError(static_cast<DWORD>(hr)); return false; }
    exists = true; return action(task.Get());
}
static bool oemVendorTaskQuery(const std::wstring& name, OemVendorTaskFacts& facts) {
    facts = {};
    return oemVendorTaskAccess(name, [&](IRegisteredTask* task) {
        VARIANT_BOOL enabled{}; TASK_STATE state{};
        HRESULT hr = task->get_Enabled(&enabled);
        if (SUCCEEDED(hr)) hr = task->get_State(&state);
        if (FAILED(hr)) { SetLastError(static_cast<DWORD>(hr)); return false; }
        if (state != TASK_STATE_DISABLED && state != TASK_STATE_READY && state != TASK_STATE_RUNNING && state != TASK_STATE_QUEUED) {
            SetLastError(ERROR_INVALID_DATA); return false;
        }
        facts.enabled = enabled != VARIANT_FALSE;
        facts.running = state == TASK_STATE_RUNNING || state == TASK_STATE_QUEUED;
        return true;
    }, facts.exists);
}
#include "msi_task_xml_control.h"

static bool oemVendorTaskSet(const std::wstring& name, bool enabled, bool stop, bool run) {
    bool exists = false;
    const bool ok = oemVendorTaskAccess(name, [&](IRegisteredTask* task) {
        HRESULT hr = task->put_Enabled(enabled ? VARIANT_TRUE : VARIANT_FALSE);
        if (SUCCEEDED(hr) && stop) hr = task->Stop(0);
        if (SUCCEEDED(hr) && run) { VARIANT empty{}; VariantInit(&empty); ComPtr<IRunningTask> running; hr = task->Run(empty, &running); }
        if (FAILED(hr)) SetLastError(static_cast<DWORD>(hr));
        return SUCCEEDED(hr);
    }, exists);
    if (ok && !exists) SetLastError(ERROR_FILE_NOT_FOUND);
    return ok && exists;
}
static bool oemVendorProcessMatches(const std::wstring& image, const std::vector<std::wstring>& names) {
    const auto pos = image.find_last_of(L"\\/");
    const auto base = image.substr(pos == std::wstring::npos ? 0 : pos + 1);
    return std::any_of(names.begin(), names.end(), [&](const auto& n) { return _wcsicmp(base.c_str(), (n + L".exe").c_str()) == 0; });
}
static bool oemVendorProcessesStop(const std::vector<std::wstring>& names, unsigned long long epoch) {
    if (names.empty()) return true;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        appendNativeLifecycleLog("oem-vendor-stack", {{"state", "process-stop-observation"}, {"requestedEpoch", epoch},
            {"snapshotRead", false}, {"enumerationComplete", false}, {"operationOk", false}, {"win32Error", GetLastError()}});
        return false;
    }
    bool ok = true; unsigned matched = 0, terminationAttempts = 0, terminatedAndExited = 0, alreadyExited = 0;
    bool enumerationComplete = false;
    const auto deadline = GetTickCount64() + 5000;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    BOOL more = Process32FirstW(snapshot, &entry);
    if (!more && GetLastError() != ERROR_NO_MORE_FILES) ok = false;
    for (; more; more = Process32NextW(snapshot, &entry)) {
        if (!oemVendorProcessMatches(entry.szExeFile, names)) continue;
        if (++matched > 32 || GetTickCount64() >= deadline || !oemVendorStackIntentCurrent(epoch)) { ok = false; break; }
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
        DWORD error = ERROR_SUCCESS; bool stopped = false;
        bool runningBeforeKnown = false, runningBefore = false, terminationAttempted = false, terminationAccepted = false;
        bool exitVerified = false, absentAtOpen = false;
        if (process) {
            wchar_t image[32768]; DWORD size = static_cast<DWORD>(std::size(image));
            DWORD code = 0;
            runningBeforeKnown = GetExitCodeProcess(process, &code) != FALSE;
            runningBefore = runningBeforeKnown && code == STILL_ACTIVE;
            if (runningBeforeKnown && !runningBefore) { stopped = true; exitVerified = true; ++alreadyExited; }
            else if (QueryFullProcessImageNameW(process, 0, image, &size) &&
                     oemVendorProcessMatches(std::wstring(image, size), names) && oemVendorStackIntentCurrent(epoch)) {
                terminationAttempted = true; ++terminationAttempts;
                terminationAccepted = TerminateProcess(process, 0) != FALSE;
                stopped = terminationAccepted &&
                    WaitForSingleObject(process, static_cast<DWORD>(std::min<ULONGLONG>(2000, GetTickCount64() < deadline ? deadline - GetTickCount64() : 0))) == WAIT_OBJECT_0;
                exitVerified = stopped;
                if (stopped) ++terminatedAndExited;
            }
            if (!stopped) error = GetLastError() ? GetLastError() : ERROR_TIMEOUT;
            CloseHandle(process);
        } else { error = GetLastError(); absentAtOpen = error == ERROR_INVALID_PARAMETER; stopped = absentAtOpen; }
        appendNativeLifecycleLog("oem-vendor-stack", {{"state", stopped ? "process-stopped" : "process-stop-failed"},
            {"process", W2U(entry.szExeFile)}, {"pid", entry.th32ProcessID}, {"targetPid", entry.th32ProcessID}, {"win32Error", error},
            {"requestedEpoch", epoch}, {"runningBeforeKnown", runningBeforeKnown}, {"runningBefore", runningBefore},
            {"terminationAttempted", terminationAttempted}, {"terminationAccepted", terminationAccepted},
            {"exitVerified", exitVerified}, {"absentAtOpen", absentAtOpen},
            {"runningToStoppedVerified", runningBefore && terminationAccepted && exitVerified},
            {"restorePolicy", "HC-services-and-tasks-only"}});
        ok = ok && stopped;
    }
    // A snapshot enumeration failure is not proof that no vendor process remains.
    // Process32NextW reports normal completion specifically as ERROR_NO_MORE_FILES.
    if (!more) {
        const DWORD error = GetLastError();
        enumerationComplete = error == ERROR_NO_MORE_FILES;
        if (!enumerationComplete) {
            ok = false;
            appendNativeLifecycleLog("oem-vendor-stack", {{"state", "process-enumeration-failed"}, {"win32Error", error}});
        }
    }
    CloseHandle(snapshot);
    appendNativeLifecycleLog("oem-vendor-stack", {{"state", "process-stop-observation"}, {"requestedEpoch", epoch},
        {"snapshotRead", true}, {"enumerationComplete", enumerationComplete}, {"operationOk", ok},
        {"matchedInSnapshot", matched}, {"terminationAttempts", terminationAttempts},
        {"terminatedAndExited", terminatedAndExited}, {"alreadyExited", alreadyExited},
        {"observationScope", "initial-snapshot-and-held-process-exit; later-relaunch-not-checked"},
        {"externalDefaultActionSuppressed", "DEVICE_UNVERIFIED"}});
    return ok;
}
#else
static OemVendorTaskDiscovery oemVendorTaskDiscover(const std::vector<std::wstring>&);
static bool oemVendorTaskQuery(const std::wstring&, OemVendorTaskFacts&);
static bool oemVendorTaskSet(const std::wstring&, bool, bool, bool);
static bool oemVendorTaskRemovalReady(const std::wstring&);
static bool oemVendorTaskRemove(const std::wstring&);
static bool oemVendorTaskImport(const std::wstring&);
static bool oemVendorProcessesStop(const std::vector<std::wstring>&, unsigned long long);
#endif

static std::wstring oemVendorStackJournalPath() { return POWER_CONTROL_DIR + L"\\oem-vendor-stack-lease.json"; }

// This journal is a return obligation, never proof that SCM/tasks are disabled.
// Bind the exact document observed in this process before changing or clearing
// it. Unknown fields remain attached to their root or exact leased identity.
static constexpr size_t kOemVendorJournalMaxBytes = 131072;
static std::string g_oemVendorJournalBytes, g_oemVendorJournalPendingBytes;
static bool g_oemVendorJournalExists = false;
static YmccFamily g_oemVendorJournalFamily = YmccFamily::Unknown;
static json g_oemVendorJournalDocument = json::object();
static bool oemVendorStackReadJournal(std::string& bytes, bool& exists) {
    bytes.clear(); exists = false;
    const auto path = oemVendorStackJournalPath();
    const auto attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    exists = true;
    if (attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    struct ReadLease { HANDLE value = INVALID_HANDLE_VALUE; ~ReadLease() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } file;
    file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(file.value, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !GetFileSizeEx(file.value, &size) || size.QuadPart <= 0 || size.QuadPart > kOemVendorJournalMaxBytes) return false;
    bytes.resize(static_cast<size_t>(size.QuadPart)); size_t used = 0;
    while (used < bytes.size()) {
        DWORD count = 0;
        if (!ReadFile(file.value, bytes.data() + used, static_cast<DWORD>(bytes.size() - used), &count, nullptr) || !count) return false;
        used += count;
    }
    return true;
}
static json oemVendorStackDecodeJournal(const std::string& bytes) {
    std::vector<std::set<std::string>> keys;
    return json::parse(bytes, [&](int depth, json::parse_event_t event, json& value) {
        if (depth > 16) throw 0;
        if (event == json::parse_event_t::object_start) keys.emplace_back();
        else if (event == json::parse_event_t::key) {
            if (keys.empty() || !keys.back().insert(value.get<std::string>()).second) throw 0;
        } else if (event == json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
}
static bool oemVendorStackJournalInteger(const json& value, unsigned long long minimum, unsigned long long maximum) {
    if (!value.is_number_integer()) return false;
    if (!value.is_number_unsigned() && value.get<long long>() < 0) return false;
    const auto number = value.get<unsigned long long>();
    return number >= minimum && number <= maximum;
}
static bool oemVendorStackLoadJournal() {
    try {
        std::string bytes; bool exists = false;
        if (!oemVendorStackReadJournal(bytes, exists)) throw 0;
        if (g_oemVendorStackLoaded) {
            if (g_oemVendorJournalFamily != g_machineIdentity.family) throw 0;
            if (exists == g_oemVendorJournalExists && bytes == g_oemVendorJournalBytes) {
                g_oemVendorStackJournalValid = true; return true;
            }
            // A failed acknowledgement may have published our exact attempted
            // bytes. Recognize only that candidate, retaining all in-memory debt
            // and its extension fields until a later durable acknowledgement.
            if (exists && !g_oemVendorJournalPendingBytes.empty() && bytes == g_oemVendorJournalPendingBytes) {
                g_oemVendorJournalBytes = bytes; g_oemVendorJournalExists = true;
                g_oemVendorJournalPendingBytes.clear(); g_oemVendorStackJournalValid = true; return true;
            }
            throw 0;
        }
        json doc = json::object();
        std::vector<OemVendorServiceLease> services;
        std::vector<OemVendorTaskLease> tasks;
        if (exists) {
            doc = oemVendorStackDecodeJournal(bytes);
            auto names = oemVendorServiceNamesForFamily(g_machineIdentity.family);
            // Migration-only: never admit the protected service to new apply.
            if (g_machineIdentity.family == YmccFamily::MsiClaw) names.push_back(L"MSI Foundation Service");
            const auto taskNames = oemVendorTaskNamesForFamily(g_machineIdentity.family);
            if (!doc.is_object() || !oemVendorStackJournalInteger(doc.at("schema"), 1, 4) ||
                !oemVendorStackJournalInteger(doc.at("family"), 0, 0x7fffffff) || doc.at("family") != static_cast<int>(g_machineIdentity.family) ||
                !doc.at("services").is_array() || doc.at("services").size() > names.size()) throw 0;
            const int schema = doc.at("schema").get<int>();
            for (const auto& item : doc.at("services")) {
                if (!item.is_object() || !item.at("name").is_string() || !item.at("wasRunning").is_boolean() ||
                    !oemVendorStackJournalInteger(item.at("originalStartType"), SERVICE_AUTO_START, SERVICE_DISABLED)) throw 0;
                OemVendorServiceLease lease{U2W(item.at("name").get<std::string>()), item.at("originalStartType").get<DWORD>(), item.at("wasRunning").get<bool>()};
                if (!item.contains("originalState")) {
                    if (schema != 1) throw 0;
                    lease.originalState = lease.wasRunning ? SERVICE_RUNNING : SERVICE_STOPPED;
                } else {
                    if (!oemVendorStackJournalInteger(item.at("originalState"), SERVICE_STOPPED, SERVICE_PAUSED)) throw 0;
                    lease.originalState = item.at("originalState").get<DWORD>();
                }
                if ((lease.originalState != SERVICE_RUNNING && lease.originalState != SERVICE_STOPPED && lease.originalState != SERVICE_PAUSED) ||
                    lease.wasRunning != (lease.originalState != SERVICE_STOPPED) || std::find(names.begin(), names.end(), lease.name) == names.end() ||
                    std::any_of(services.begin(), services.end(), [&](const auto& x) { return x.name == lease.name; })) throw 0;
                if (item.contains("returnPrepared")) {
                    if (!item["returnPrepared"].is_boolean()) throw 0;
                    lease.returnPrepared = item["returnPrepared"].get<bool>();
                }
                services.push_back(std::move(lease));
            }
            if (schema == 1) {
                // A v1 writer had no task debt. Never silently discard a known
                // tasks field that this migration would otherwise not consume.
                if (doc.contains("tasks") && (!doc["tasks"].is_array() || !doc["tasks"].empty())) throw 0;
            } else {
                const size_t taskLimit = schema == 2 ? taskNames.size() : taskNames.empty() ? 0 : kOemVendorMaxTaskPaths;
                if (!doc.at("tasks").is_array() || doc.at("tasks").size() > taskLimit) throw 0;
                for (const auto& item : doc.at("tasks")) {
                    if (!item.is_object() || !item.at("name").is_string() || !item.at("wasEnabled").is_boolean() || !item.at("wasRunning").is_boolean()) throw 0;
                    OemVendorTaskLease lease{U2W(item.at("name").get<std::string>()), item.at("wasEnabled").get<bool>(), item.at("wasRunning").get<bool>()};
                    if (!oemVendorTaskPathAllowed(lease.name, taskNames) || (schema == 2 && std::find(taskNames.begin(), taskNames.end(), lease.name) == taskNames.end()) ||
                        std::any_of(tasks.begin(), tasks.end(), [&](const auto& x) { return oemVendorTaskPathEqual(x.name, lease.name); })) throw 0;
                    if (item.contains("returnPrepared")) {
                        if (!item["returnPrepared"].is_boolean()) throw 0;
                        lease.returnPrepared = item["returnPrepared"].get<bool>();
                    }
                    if (item.contains("removePrepared")) {
                        if (schema < 4 || !item["removePrepared"].is_boolean() || g_machineIdentity.family != YmccFamily::MsiClaw) throw 0;
                        lease.removePrepared = item["removePrepared"].get<bool>();
                    }
                    tasks.push_back(std::move(lease));
                }
            }
        }
        // Decoding is transactional: failures leave existing return obligations.
        if (!g_oemVendorServiceLeases.empty() || !g_oemVendorTaskLeases.empty()) throw 0;
        g_oemVendorServiceLeases = std::move(services); g_oemVendorTaskLeases = std::move(tasks);
        g_oemVendorJournalBytes = std::move(bytes); g_oemVendorJournalExists = exists;
        g_oemVendorJournalFamily = g_machineIdentity.family; g_oemVendorJournalDocument = std::move(doc);
        g_oemVendorJournalPendingBytes.clear(); g_oemVendorStackLoaded = true; g_oemVendorStackJournalValid = true; return true;
    } catch (...) { g_oemVendorStackJournalValid = false; }
    appendNativeLifecycleLog("oem-vendor-stack", {{"state", "recovery-held"}, {"reason", "journal-unreadable-or-unbound"}}); return false;
}
static bool oemVendorStackSaveJournal() {
    try {
        if (!oemVendorStackLoadJournal()) return false;
        json services = json::array(), tasks = json::array();
        for (const auto& lease : g_oemVendorServiceLeases) {
            json item = json::object();
            for (const auto& previous : g_oemVendorJournalDocument.value("services", json::array()))
                if (previous.at("name") == W2U(lease.name)) { item = previous; break; }
            item["name"] = W2U(lease.name); item["originalStartType"] = lease.originalStartType;
            if (lease.returnPrepared) item["returnPrepared"] = true;
            item["wasRunning"] = lease.wasRunning; item["originalState"] = lease.originalState ? lease.originalState : lease.wasRunning ? SERVICE_RUNNING : SERVICE_STOPPED;
            services.push_back(std::move(item));
        }
        for (const auto& lease : g_oemVendorTaskLeases) {
            json item = json::object();
            for (const auto& previous : g_oemVendorJournalDocument.value("tasks", json::array()))
                if (oemVendorTaskPathEqual(U2W(previous.at("name").get<std::string>()), lease.name)) { item = previous; break; }
            if (lease.returnPrepared) item["returnPrepared"] = true;
            if (lease.removePrepared) item["removePrepared"] = true;
            item["name"] = W2U(lease.name); item["wasEnabled"] = lease.wasEnabled; item["wasRunning"] = lease.wasRunning;
            tasks.push_back(std::move(item));
        }
        const bool nested = std::any_of(g_oemVendorTaskLeases.begin(), g_oemVendorTaskLeases.end(),
            [](const auto& lease) { return lease.name.find(L'\\') != std::wstring::npos; });
        auto doc = g_oemVendorJournalDocument;
        const bool removed = std::any_of(g_oemVendorTaskLeases.begin(), g_oemVendorTaskLeases.end(),
            [](const auto& lease) { return lease.removePrepared; });
        doc["schema"] = removed || doc.value("schema", 1) == 4 ? 4 : nested || doc.value("schema", 1) == 3 ? 3 : 2;
        doc["family"] = static_cast<int>(g_machineIdentity.family); doc["services"] = services; doc["tasks"] = tasks;
        const auto bytes = doc.dump(2);
        if (bytes.size() > kOemVendorJournalMaxBytes) return false;
        g_oemVendorJournalPendingBytes = bytes;
        const bool written = sgWriteFileAtomic(oemVendorStackJournalPath(), bytes);
        std::string readback; bool exists = false;
        if (!written || g_oemVendorJournalFamily != g_machineIdentity.family || !oemVendorStackReadJournal(readback, exists) || !exists || readback != bytes) return false;
        g_oemVendorJournalBytes = bytes; g_oemVendorJournalExists = true; g_oemVendorJournalDocument = std::move(doc);
        g_oemVendorJournalPendingBytes.clear(); return true;
    } catch (...) { return false; }
}
static bool oemVendorStackRestoreLocked(const char* reason, bool tasksOnly = false) {
    if (!oemVendorStackLoadJournal()) return false;
    if (g_oemVendorServiceLeases.empty() && g_oemVendorTaskLeases.empty()) return true;
    SC_HANDLE scm = tasksOnly || g_oemVendorServiceLeases.empty() ? nullptr : OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    std::vector<OemVendorServiceLease> pending = tasksOnly ? g_oemVendorServiceLeases : std::vector<OemVendorServiceLease>{};
    if (!tasksOnly) for (auto& lease : g_oemVendorServiceLeases) {
        if (!oemVendorStackLoadJournal()) { pending.push_back(lease); continue; }
        bool ok = false; DWORD type = 0, state = 0, error = ERROR_SUCCESS;
        SC_HANDLE service = scm ? OpenServiceW(scm, lease.name.c_str(), SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_PAUSE_CONTINUE | SERVICE_CHANGE_CONFIG) : nullptr;
        if (service && oemVendorServiceQueryConfig(service, type)) {
            bool runningOk = oemVendorServiceStable(service, state);
            const DWORD originalState = lease.originalState ? lease.originalState : lease.wasRunning ? SERVICE_RUNNING : SERVICE_STOPPED;
            bool configOk = type == lease.originalStartType;
            const bool original = configOk && runningOk && state == originalState;
            bool admitted = runningOk && (original || type == SERVICE_DISABLED || lease.returnPrepared);
            if (!admitted) error = ERROR_REVISION_MISMATCH;
            if (admitted && !original && !lease.returnPrepared) {
                lease.returnPrepared = true;
                if (!oemVendorStackSaveJournal()) { lease.returnPrepared = false; admitted = false; }
            }
            if (admitted && !configOk && type == SERVICE_DISABLED)
                configOk = ChangeServiceConfigW(service, SERVICE_NO_CHANGE, lease.originalStartType, SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != FALSE;
            configOk = admitted && configOk && oemVendorServiceQueryConfig(service, type) && type == lease.originalStartType;
            if (configOk && runningOk && lease.wasRunning && state == SERVICE_STOPPED) {
                runningOk = lease.originalStartType != SERVICE_DISABLED && StartServiceW(service, 0, nullptr) &&
                    oemVendorServiceWait(service, SERVICE_RUNNING, kOemVendorServiceBudgetMs);
                if (runningOk) runningOk = oemVendorServiceQueryState(service, state);
            }
            if (configOk && runningOk && lease.wasRunning && state != originalState) {
                SERVICE_STATUS status{};
                runningOk = ControlService(service, originalState == SERVICE_PAUSED ? SERVICE_CONTROL_PAUSE : SERVICE_CONTROL_CONTINUE, &status) &&
                    oemVendorServiceWait(service, originalState, kOemVendorServiceBudgetMs);
            }
            if (configOk && runningOk) runningOk = oemVendorServiceQueryState(service, state) && state == originalState;
            ok = configOk && runningOk;
            if (!configOk && type != SERVICE_DISABLED && type != lease.originalStartType) error = ERROR_REVISION_MISMATCH;
        }
        if (!ok && !error) error = GetLastError() ? GetLastError() : ERROR_TIMEOUT;
        appendNativeLifecycleLog("oem-vendor-stack", {{"state", ok ? "restored" : "restore-failed"}, {"service", W2U(lease.name)},
            {"reason", reason ? reason : "app-exit"}, {"startTypeReadback", type}, {"serviceStateReadback", state}, {"win32Error", error}});
        if (service) CloseServiceHandle(service);
        if (!ok) pending.push_back(lease);
    }
    if (scm) CloseServiceHandle(scm);
    std::vector<OemVendorTaskLease> pendingTasks;
    for (auto& lease : g_oemVendorTaskLeases) {
        if (!oemVendorStackLoadJournal()) { pendingTasks.push_back(lease); continue; }
        OemVendorTaskFacts facts; bool ok = oemVendorTaskQuery(lease.name, facts);
        bool imported = false;
        if (ok && !facts.exists && lease.removePrepared) {
            // The deletion obligation is durable before removal; retain it on any import/readback failure.
            if (!lease.returnPrepared) {
                lease.returnPrepared = true;
                if (!oemVendorStackSaveJournal()) { lease.returnPrepared = false; ok = false; }
            }
            if (ok) {
                imported = oemVendorTaskImport(lease.name);
                ok = imported && oemVendorTaskQuery(lease.name, facts) && facts.exists;
            }
        }
        if (ok && facts.exists) {
            const bool original = facts.enabled == lease.wasEnabled && facts.running == lease.wasRunning;
            if (!original && (facts.enabled || facts.running) && (!lease.returnPrepared || (facts.running && !lease.wasRunning))) {
                ok = false; SetLastError(ERROR_REVISION_MISMATCH);
            }
            if (ok && !original && !lease.returnPrepared) {
                lease.returnPrepared = true;
                if (!oemVendorStackSaveJournal()) { lease.returnPrepared = false; ok = false; }
            }
            if (ok && (facts.enabled != lease.wasEnabled || (lease.wasRunning && !facts.running))) {
                if (lease.wasRunning && !lease.wasEnabled) {
                    // A disabled-but-running task is unusual but legal. Run it
                    // through a temporary enabled state, then restore the
                    // disabled policy without stopping the running instance.
                    ok = (facts.running || oemVendorTaskSet(lease.name, true, false, true)) &&
                        oemVendorTaskSet(lease.name, false, false, false);
                } else {
                    ok = oemVendorTaskSet(lease.name, lease.wasEnabled, false, lease.wasRunning && !facts.running);
                }
            }
            ok = ok && oemVendorTaskQuery(lease.name, facts) && facts.enabled == lease.wasEnabled &&
                facts.running == lease.wasRunning;
        }
        // An absent task is not mutated; keep its recovery debt for explicit review.
        ok = ok && facts.exists;
        appendNativeLifecycleLog("oem-vendor-stack", {{"state", ok ? "task-restored" : "task-restore-failed"},
            {"task", W2U(lease.name)}, {"imported", imported}, {"definitionPolicy", lease.removePrepared ? "xml-import" : "enable-restore"},
            {"enabledReadback", facts.enabled}, {"runningReadback", facts.running}, {"readbackVerified", ok},
            {"win32Error", ok ? ERROR_SUCCESS : GetLastError()}});
        if (!ok) pendingTasks.push_back(lease);
    }
    // Do not forget restored entries until the updated journal is durable.
    auto oldServices = std::move(g_oemVendorServiceLeases); auto oldTasks = std::move(g_oemVendorTaskLeases);
    g_oemVendorServiceLeases = std::move(pending); g_oemVendorTaskLeases = std::move(pendingTasks);
    if (!oemVendorStackSaveJournal()) {
        g_oemVendorServiceLeases = std::move(oldServices); g_oemVendorTaskLeases = std::move(oldTasks);
        appendNativeLifecycleLog("oem-vendor-stack", {{"state", "restore-held"}, {"reason", "journal-write-failed"}}); return false;
    }
    return (tasksOnly || g_oemVendorServiceLeases.empty()) && g_oemVendorTaskLeases.empty();
}
static bool oemVendorStackApplyIfNeeded(const char* reason, unsigned long long requestedEpoch = 0) {
    if (!oemVendorStackLoadJournal()) return false;
    const bool controllerOnly = oemVendorShieldWantedSnapshot();
    const auto names = controllerOnly ? std::vector<std::wstring>{} : oemVendorServiceNamesForFamily(g_machineIdentity.family);
    const auto processNames = controllerOnly ? oemVendorShieldProcessNamesForFamily(g_machineIdentity.family)
                                             : oemVendorProcessNamesForFamily(g_machineIdentity.family);
    bool taskReleaseWanted = false;
    { std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
      taskReleaseWanted = g_machineIdentity.family == YmccFamily::MsiClaw &&
          g_oemVendorMsiTaskShieldPreferenceSeen && !g_oemVendorShieldWanted; }
    const auto taskNames = taskReleaseWanted ? std::vector<std::wstring>{} : oemVendorTaskNamesForFamily(g_machineIdentity.family);
    if (names.empty() && taskNames.empty() && processNames.empty()) return false;
    if (g_exitRequested.load(std::memory_order_acquire)) return false;
    if (!oemVendorStackMachineSupported()) return false;
    unsigned long long epoch; bool wakePass;
    { std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
      epoch = requestedEpoch ? requestedEpoch : g_oemVendorStackRequested;
      wakePass = epoch == g_oemVendorShieldWake.epoch; }
    if (!oemVendorStackIntentCurrent(epoch)) return false;
    // An explicit OFF owns the return of these two tasks even while shortcut
    // takeover still owns its separate process/key policy. Never disable them again here.
    bool allOk = !taskReleaseWanted || oemVendorStackRestoreLocked("msi-shield-task-release", true);
    size_t existing = 0, confirmed = 0, unavailable = 0;
    // A wake pass needs only task/process readback. Do not repeat the broad
    // read-only service inventory or frontend diagnostics every two seconds.
    if (controllerOnly && !wakePass) {
        json inventory = msiShieldReadServiceInventory();
        inventory["observedRequestedEpoch"] = epoch;
        { std::lock_guard<std::mutex> lock(g_oemVendorStackMx); g_oemVendorMsiServiceInventory = inventory; }
        appendNativeLifecycleLog("oem-vendor-stack", {{"state", "msi-service-inventory"}, {"requestedEpoch", epoch},
            {"inventory", inventory}, {"action", "read-only"}, {"discoveryAuthorizesMutation", false}});
        if (!oemVendorStackIntentCurrent(epoch)) return false;
    }
    // Discover once per ordinary request. The short wake guard reuses exact
    // paths, preserving its original low-cost readback and retry schedule.
    if (!taskNames.empty() && (!wakePass || g_oemVendorTaskPaths.empty())) {
        auto discovery = oemVendorTaskDiscover(taskNames);
        g_oemVendorTaskPaths = std::move(discovery.paths);
        for (const auto& lease : g_oemVendorTaskLeases) {
            if (std::none_of(g_oemVendorTaskPaths.begin(), g_oemVendorTaskPaths.end(), [&](const auto& p){return oemVendorTaskPathEqual(p, lease.name);}))
                g_oemVendorTaskPaths.push_back(lease.name);
        }
        g_oemVendorTaskDiscoveryComplete = discovery.complete;
        appendNativeLifecycleLog("oem-vendor-stack", {{"state", "task-discovery"}, {"requestedEpoch", epoch},
            {"complete", discovery.complete}, {"foldersRead", discovery.foldersRead}, {"tasksRead", discovery.tasksRead},
            {"includeHidden", true}, {"taskCount", g_oemVendorTaskPaths.size()}, {"win32Error", discovery.error}});
    }
    if (!taskNames.empty()) allOk = allOk && g_oemVendorTaskDiscoveryComplete;
    // The explicit MSI shield removes tasks; legacy takeover retains its disable path.
    for (const auto& name : taskNames.empty() ? std::vector<std::wstring>{} : g_oemVendorTaskPaths) {
        if (!oemVendorStackIntentCurrent(epoch)) return false;
        OemVendorTaskFacts facts;
        if (!oemVendorTaskQuery(name, facts)) {
            allOk = false;
            appendNativeLifecycleLog("oem-vendor-stack", {{"state", "task-unavailable"}, {"task", W2U(name)}, {"requestedEpoch", epoch}, {"win32Error", GetLastError()}});
            continue;
        }
        if (!facts.exists) {
            appendNativeLifecycleLog("oem-vendor-stack", {{"state", "task-absent"}, {"task", W2U(name)}, {"requestedEpoch", epoch}});
            continue;
        }
        auto lease = std::find_if(g_oemVendorTaskLeases.begin(), g_oemVendorTaskLeases.end(), [&](const auto& x){return oemVendorTaskPathEqual(x.name, name);});
        if (controllerOnly) {
            // Mirror YMCC task switches: remove while shielded, import the shipped XML on return.
            // A missing/invalid template must never turn a recoverable disable into irreversible removal.
            bool ok = oemVendorTaskRemovalReady(name), mutationAttempted = false;
            if (ok && lease == g_oemVendorTaskLeases.end()) {
                if (g_oemVendorTaskLeases.size() >= kOemVendorMaxTaskPaths) { allOk = false; continue; }
                g_oemVendorTaskLeases.push_back({name, facts.enabled, facts.running, false, true});
                if (!oemVendorStackSaveJournal()) { g_oemVendorTaskLeases.pop_back(); return false; }
            } else if (ok && !lease->removePrepared) {
                lease->removePrepared = true;
                if (!oemVendorStackSaveJournal()) { lease->removePrepared = false; return false; }
            }
            if (ok) {
                mutationAttempted = oemVendorStackIntentCurrent(epoch);
                ok = mutationAttempted && oemVendorTaskRemove(name);
                if (ok) {
                    ok = oemVendorTaskQuery(name, facts);
                    if (ok && facts.exists) { SetLastError(ERROR_REVISION_MISMATCH); ok = false; }
                }
            }
            appendNativeLifecycleLog("oem-vendor-stack", {{"state", ok ? "task-removed" : "task-remove-failed"},
                {"task", W2U(name)}, {"requestedEpoch", epoch}, {"definitionPolicy", "xml-import"},
                {"mutationAttempted", mutationAttempted}, {"readbackVerified", ok},
                {"existsReadback", facts.exists}, {"win32Error", ok ? ERROR_SUCCESS : GetLastError()}});
            allOk = allOk && ok;
            continue;
        }
        if ((facts.enabled || facts.running) && lease == g_oemVendorTaskLeases.end()) {
            if (g_oemVendorTaskLeases.size() >= kOemVendorMaxTaskPaths) { allOk = false; continue; }
            g_oemVendorTaskLeases.push_back({name, facts.enabled, facts.running});
            if (!oemVendorStackSaveJournal()) { g_oemVendorTaskLeases.pop_back(); return false; }
        }
        const OemVendorTaskFacts before = facts;
        bool ok = true, mutationAttempted = false;
        if (facts.enabled || facts.running) {
            mutationAttempted = oemVendorStackIntentCurrent(epoch);
            ok = mutationAttempted && oemVendorTaskSet(name, false, facts.running, false);
        }
        // Already inactive tasks need one read on a periodic pass. Only a
        // mutation needs another readback; keep the original full audit for
        // ordinary settings requests.
        if (!wakePass || mutationAttempted) {
            const auto deadline = GetTickCount64() + 2000;
            while (ok && oemVendorStackIntentCurrent(epoch) && oemVendorTaskQuery(name, facts) && facts.running && GetTickCount64() < deadline) Sleep(50);
            ok = ok && oemVendorTaskQuery(name, facts) && facts.exists && !facts.enabled && !facts.running;
        }
        appendNativeLifecycleLog("oem-vendor-stack", {{"state", ok ? "task-disabled" : "task-disable-failed"},
            {"task", W2U(name)}, {"requestedEpoch", epoch},
            {"enabledBefore", before.enabled}, {"runningBefore", before.running}, {"mutationAttempted", mutationAttempted},
            {"alreadyInactive", !before.enabled && !before.running}, {"readbackVerified", ok},
            {"runningToStoppedVerified", before.running && mutationAttempted && ok},
            {"enabledReadback", facts.enabled}, {"runningReadback", facts.running}, {"win32Error", ok ? ERROR_SUCCESS : GetLastError()}});
        allOk = allOk && ok;
    }
    SC_HANDLE scm = names.empty() ? nullptr : OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!names.empty() && !scm) { appendNativeLifecycleLog("oem-vendor-stack", {{"state", "apply-failed"}, {"reason", "scm-open-failed"}, {"win32Error", GetLastError()}}); return false; }
    for (const auto& name : names) {
        if (!oemVendorStackIntentCurrent(epoch)) { allOk = false; break; }
        SC_HANDLE service = OpenServiceW(scm, name.c_str(), SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_STOP | SERVICE_CHANGE_CONFIG);
        if (!service) {
            const auto error = GetLastError();
            if (error != ERROR_SERVICE_DOES_NOT_EXIST) { ++unavailable; allOk = false; }
            appendNativeLifecycleLog("oem-vendor-stack", {{"state", "service-unavailable"}, {"service", W2U(name)}, {"win32Error", error}}); continue;
        }
        ++existing; DWORD original = 0, before = 0;
        auto lease = std::find_if(g_oemVendorServiceLeases.begin(), g_oemVendorServiceLeases.end(), [&](const auto& x){return x.name == name;});
        bool qualified = oemVendorServiceQueryConfig(service, original) && original >= SERVICE_AUTO_START && original <= SERVICE_DISABLED;
        // Pending state is not a permanent capability failure. Stabilize it before
        // observing the original running fact; already-leased STOP_PENDING may finish.
        qualified = qualified && oemVendorServiceStable(service, before);
        if (lease != g_oemVendorServiceLeases.end() && original != SERVICE_DISABLED && original != lease->originalStartType) {
            qualified = false; SetLastError(ERROR_REVISION_MISMATCH);
        }
        if (lease == g_oemVendorServiceLeases.end() && original == SERVICE_DISABLED && before != SERVICE_STOPPED) qualified = false;
        if (!qualified) {
            allOk = false; appendNativeLifecycleLog("oem-vendor-stack", {{"state", "apply-held"}, {"service", W2U(name)},
                {"reason", "service-facts-unqualified"}, {"serviceStateReadback", before}, {"win32Error", GetLastError()}}); CloseServiceHandle(service); continue;
        }
        if (lease == g_oemVendorServiceLeases.end() && (original != SERVICE_DISABLED || before != SERVICE_STOPPED)) {
            g_oemVendorServiceLeases.push_back({name, original, before == SERVICE_RUNNING || before == SERVICE_PAUSED, before});
            if (!oemVendorStackSaveJournal()) {
                g_oemVendorServiceLeases.pop_back(); allOk = false;
                appendNativeLifecycleLog("oem-vendor-stack", {{"state", "apply-held"}, {"service", W2U(name)}, {"reason", "journal-write-failed"}});
                CloseServiceHandle(service); break;
            }
        }
        bool disabled = original == SERVICE_DISABLED, stopSent = false;
        DWORD disableError = ERROR_SUCCESS, stopError = ERROR_SUCCESS;
        if (!disabled && oemVendorStackIntentCurrent(epoch)) disabled = ChangeServiceConfigW(service, SERVICE_NO_CHANGE, SERVICE_DISABLED,
            SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != FALSE;
        if (!disabled) disableError = GetLastError();
        if (disabled && before != SERVICE_STOPPED && oemVendorStackIntentCurrent(epoch)) {
            SERVICE_STATUS status{}; stopSent = ControlService(service, SERVICE_CONTROL_STOP, &status) != FALSE;
            if (stopSent) { if (!oemVendorServiceWait(service, SERVICE_STOPPED, kOemVendorServiceBudgetMs)) stopError = GetLastError(); }
            else stopError = GetLastError();
        }
        DWORD afterType = 0, afterState = 0;
        const bool readback = oemVendorServiceQueryConfig(service, afterType) && oemVendorServiceQueryState(service, afterState);
        const DWORD readbackError = readback ? ERROR_SUCCESS : GetLastError();
        const bool ok = readback && afterType == SERVICE_DISABLED && afterState == SERVICE_STOPPED;
        const DWORD error = ok ? ERROR_SUCCESS : readbackError ? readbackError : disableError ? disableError : stopError ? stopError : ERROR_TIMEOUT;
        if (ok) ++confirmed; else allOk = false;
        appendNativeLifecycleLog("oem-vendor-stack", {{"state", ok ? "service-stopped" : "apply-failed"}, {"service", W2U(name)},
            {"reason", reason ? reason : "configured-oem-rule"}, {"startTypeBefore", original}, {"serviceStateBefore", before},
            {"disabledSent", disabled}, {"stopSent", stopSent}, {"readbackVerified", readback}, {"startTypeReadback", afterType},
            {"serviceStateReadback", afterState}, {"win32Error", error}});
        CloseServiceHandle(service);
    }
    if (scm) CloseServiceHandle(scm);
    if (oemVendorStackIntentCurrent(epoch)) allOk = oemVendorProcessesStop(processNames, epoch) && allOk;
    else allOk = false;
    if (controllerOnly && !wakePass) {
        json observation = msiShieldReadProtectedService();
        observation["observedRequestedEpoch"] = epoch;
        { std::lock_guard<std::mutex> lock(g_oemVendorStackMx); g_oemVendorMsiProtectedServiceObservation = observation; }
        appendNativeLifecycleLog("oem-vendor-stack", {{"state", "service-preserve-unverified"},
            {"service", "MSI Foundation Service"}, {"reason", "unverified-controller-dependency-protected"},
            {"action", "not-mutated"}, {"verification", observation.value("readbackVerified", false) ? "read-only-scm" : "readback-unavailable"},
            {"observation", observation}, {"requestedEpoch", epoch}, {"mode", "msi-controller-only"}});
    }
    // Missing declared services are explicit, not an assertion of a suppressed key.
    const bool serviceTargetsOk = names.empty() || (existing > 0 && confirmed == existing && unavailable == 0);
    allOk = allOk && serviceTargetsOk && oemVendorStackIntentCurrent(epoch);
    appendNativeLifecycleLog("oem-vendor-stack", {{"state", allOk ? "applied" : "partial-or-unavailable"}, {"existingServices", existing},
        {"unreadableServices", unavailable}, {"confirmedStoppedServices", confirmed},
        {"requestedEpoch", epoch}, {"requiresReboot", false},
        {"mode", controllerOnly ? "msi-controller-only" : "legacy-oem-takeover"},
        {"servicePolicy", controllerOnly ? "preserve-unverified-dependencies" : "configured-oem-rule"},
        {"externalDefaultActionSuppressed", "DEVICE_UNVERIFIED"}});
    // Observation only: no wider stop lists or CTW Appx removal.
#ifndef YMCC_VENDOR_STACK_FIXTURE
    if(!wakePass && g_machineIdentity.family==YmccFamily::MsiClaw)msiOemSuppressionObserveAfterTakeover(epoch,allOk);
#endif
    return allOk;
}
static void oemVendorStackSchedule() {
    { std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
      const auto now = GetTickCount64();
      oemVendorStackShieldWakeRefreshLocked(now);
      if (!g_oemVendorStackOpQueued && g_oemVendorStackRequested == g_oemVendorStackCompleted &&
          oemVendorStackShieldWakeAliveLocked(now) && now >= g_oemVendorShieldWake.next)
          oemVendorStackShieldWakeRequestLocked();
      if (g_oemVendorStackOpQueued || g_oemVendorStackRequested == g_oemVendorStackCompleted ||
          g_exitRequested.load(std::memory_order_acquire) || g_machineIdentity.family == YmccFamily::Unknown ||
          (g_oemVendorStackRetryAt && GetTickCount64() < g_oemVendorStackRetryAt)) return;
      g_oemVendorStackOpQueued = true; }
    const bool accepted = gamepadSerialSubmit([] {
        unsigned long long epoch; bool wanted, wakePass;
        { std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
          oemVendorStackShieldWakeRefreshLocked(GetTickCount64());
          epoch = g_oemVendorStackRequested; wanted = g_oemVendorStackWanted;
          wakePass = epoch == g_oemVendorShieldWake.epoch; }
        const auto started = GetTickCount64();
        bool ok = false;
        { std::lock_guard<std::mutex> io(g_oemVendorStackIoMx);
          // A queued wake job may have waited beyond its window, or the user
          // may have disabled the shield while this worker was busy.
          bool current;
          { std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
            current = epoch == g_oemVendorStackRequested &&
                (!wakePass || oemVendorStackShieldWakeAliveLocked(GetTickCount64())); }
          if (current) {
              if (!g_oemVendorStackStartupRecovered) {
                  // A current MSI ON intent continues validated task-only leases.
                  // Restoring them first would enable/run the official frontend
                  // in between startup recovery and the immediately following apply.
                  // Legacy service debt, OFF, and other families keep full recovery.
                  const bool retainMsiTasks = wanted && g_machineIdentity.family == YmccFamily::MsiClaw &&
                      oemVendorStackMachineSupported() && oemVendorStackLoadJournal() &&
                      g_oemVendorServiceLeases.empty();
                  g_oemVendorStackStartupRecovered = retainMsiTasks || oemVendorStackRestoreLocked("startup-recovery");
                  if (retainMsiTasks && !g_oemVendorTaskLeases.empty())
                      appendNativeLifecycleLog("oem-vendor-stack", {{"state", "startup-task-leases-retained"},
                          {"requestedEpoch", epoch}, {"taskCount", g_oemVendorTaskLeases.size()}, {"restoredBeforeApply", false}});
              }
              if (g_oemVendorStackStartupRecovered) {
                  if (wanted && !g_exitRequested.load(std::memory_order_acquire))
                      ok = oemVendorStackApplyIfNeeded(wakePass ? "msi-wake-guard" : "configured-oem-rule", epoch);
                  else ok = oemVendorStackRestoreLocked("intent-cleared");
              }
          }
        }
        bool exhausted = false;
        { std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
          g_oemVendorStackOpQueued = false;
          oemVendorStackShieldWakeRefreshLocked(GetTickCount64());
          if (epoch == g_oemVendorStackRequested &&
              (!wakePass || oemVendorStackShieldWakeAliveLocked(GetTickCount64()))) {
              if (wakePass) g_oemVendorShieldWake.next = GetTickCount64() + kOemVendorShieldWakeIntervalMs;
              ++g_oemVendorStackAttempts;
              exhausted = !ok && g_oemVendorStackAttempts >= kOemVendorMaxAttempts;
              g_oemVendorStackLastOutcomeKnown = true;
              g_oemVendorStackLastOutcomeOk = ok;
              g_oemVendorStackLastOutcomeEpoch = epoch;
              g_oemVendorStackLastOutcome = exhausted ? "retry-exhausted" : (ok ? "applied-or-restored" : "retrying");
              if (ok || exhausted) { g_oemVendorStackCompleted = epoch; g_oemVendorStackRetryAt = 0; }
              else g_oemVendorStackRetryAt = wakePass ? g_oemVendorShieldWake.next :
                  GetTickCount64() + (g_oemVendorStackAttempts == 1 ? 1000 : 3000);
          }
        }
        if (wakePass) appendNativeLifecycleLog("oem-vendor-stack", {{"state", "wake-guard-pass"},
            {"requestedEpoch", epoch}, {"ok", ok}, {"durationMs", GetTickCount64() - started},
            {"intervalMs", kOemVendorShieldWakeIntervalMs}});
        if (exhausted) appendNativeLifecycleLog("oem-vendor-stack", {{"state", "retry-exhausted"}, {"attempts", kOemVendorMaxAttempts}, {"externalDefaultActionSuppressed", "NOT_CONFIRMED"}});
        oemVendorStackSchedule(); // Newer intent only, or later existing 400ms refresh.
    });
    if (!accepted) { std::lock_guard<std::mutex> lock(g_oemVendorStackMx); g_oemVendorStackOpQueued = false; }
}
static void oemVendorStackShieldWakeBegin(unsigned long long generation, const char* reason) {
    const auto now = GetTickCount64();
    { std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
      if (!g_oemVendorShieldWanted || g_machineIdentity.family != YmccFamily::MsiClaw ||
          !oemVendorStackMachineSupported() || g_exitRequested.load(std::memory_order_acquire) ||
          !msiControllerShieldWakePowerCurrent(generation) || generation <= g_oemVendorShieldWake.generation) return;
      g_oemVendorShieldWake.generation = generation;
      g_oemVendorShieldWake.until = now + kOemVendorShieldWakeWindowMs;
      g_oemVendorShieldWake.next = now;
      oemVendorStackShieldWakeRequestLocked(); }
    appendNativeLifecycleLog("oem-vendor-stack", {{"state", "wake-guard-started"}, {"generation", generation},
        {"reason", reason ? reason : "wake"}, {"windowMs", kOemVendorShieldWakeWindowMs},
        {"intervalMs", kOemVendorShieldWakeIntervalMs}, {"controllerInputBlocked", false}});
    oemVendorStackSchedule();
}
static void oemVendorStackUpdateIntent(const char* source, bool requested) {
    const auto family = g_machineIdentity.family;
    bool effective = false, changed = false, shortcut = false, shield = false;
    { std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
      shortcut = g_oemVendorShortcutWanted;
      shield = g_oemVendorShieldWanted;
      effective = oemVendorEffectiveWanted(shortcut || g_oemVendorRecordingWanted, shield);
      if (!g_oemVendorStackIntentSeen || g_oemVendorStackWanted != effective ||
          g_oemVendorStackIntentFamily != family || g_oemVendorStackIntentControllerOnly != shield) {
          g_oemVendorStackIntentSeen = true; g_oemVendorStackIntentFamily = family;
          g_oemVendorStackWanted = effective; g_oemVendorStackIntentControllerOnly = shield;
          g_oemVendorStackLastOutcomeKnown = false; g_oemVendorStackLastOutcome = "pending";
          ++g_oemVendorStackRequested; g_oemVendorStackAttempts = 0; g_oemVendorStackRetryAt = 0; changed = true;
      }
    }
    if (changed) appendNativeLifecycleLog("oem-vendor-stack", {{"state", "intent"}, {"source", source ? source : "unknown"},
        {"requested", requested}, {"wanted", effective}, {"shortcutWanted", shortcut}, {"shieldWanted", shield},
        {"family", static_cast<int>(family)}, {"familySupported", oemVendorStackFamilySupported(family)},
        {"deviceClass", g_machineIdentity.deviceClass}, {"modelSupported", oemVendorStackMachineSupported()}});
    oemVendorStackSchedule();
}
static void oemVendorStackRequest(bool wanted) {
    const auto family = g_machineIdentity.family;
    const bool accepted = wanted && oemVendorStackFamilySupported(family) && oemVendorStackMachineSupported();
    { std::lock_guard<std::mutex> lock(g_oemVendorStackMx); g_oemVendorShortcutWanted = accepted; }
    oemVendorStackUpdateIntent("shortcut-rule", wanted);
}
static void oemVendorStackRecordingRequest(bool wanted) {
    const auto family = g_machineIdentity.family;
    const bool accepted = wanted && oemVendorStackFamilySupported(family) && oemVendorStackMachineSupported();
    { std::lock_guard<std::mutex> lock(g_oemVendorStackMx); g_oemVendorRecordingWanted = accepted; }
    oemVendorStackUpdateIntent("shortcut-recording", wanted);
}
// Preparation is best-effort. A rejected/failed lease never rejects input recording.
static json oemVendorStackRecordingSnapshot() {
    std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
    const bool completed = g_oemVendorStackLastOutcomeKnown &&
        g_oemVendorStackLastOutcomeEpoch == g_oemVendorStackRequested &&
        g_oemVendorStackRequested == g_oemVendorStackCompleted;
    return {{"requested", g_oemVendorRecordingWanted}, {"pending", g_oemVendorRecordingWanted && !completed},
        {"applied", g_oemVendorRecordingWanted && completed && g_oemVendorStackLastOutcomeOk && g_oemVendorStackJournalValid.load()},
        {"failed", g_oemVendorRecordingWanted && completed && !g_oemVendorStackLastOutcomeOk},
        {"reason", g_oemVendorStackLastOutcome}};
}
static void oemVendorStackShieldRequest(bool wanted) {
    const auto family = g_machineIdentity.family;
    // Identity resolution is fail-closed but not a reason to tear down an
    // already admitted MSI lease transiently. Wait for the authoritative
    // family/model before changing the shield owner.
    if (family == YmccFamily::Unknown) return;
    const bool accepted = wanted && family == YmccFamily::MsiClaw && oemVendorStackMachineSupported();
    { std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
      g_oemVendorShieldWanted = accepted;
      if (family == YmccFamily::MsiClaw && oemVendorStackMachineSupported()) g_oemVendorMsiTaskShieldPreferenceSeen = true; }
    oemVendorStackUpdateIntent("msi-controller-shield", wanted);
}
static bool oemVendorStackShieldIsWanted() {
    std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
    return g_oemVendorShieldWanted;
}
static json oemVendorStackShieldRuntimeSnapshot() {
    std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
    const bool current = g_oemVendorShieldWanted;
    const bool journalValid = g_oemVendorStackJournalValid.load(std::memory_order_acquire);
    const bool completed = journalValid && g_oemVendorStackLastOutcomeKnown &&
        g_oemVendorStackLastOutcomeEpoch == g_oemVendorStackRequested &&
        g_oemVendorStackRequested == g_oemVendorStackCompleted;
    return {{"effective", current},
            {"shortcutOwnerActive", g_oemVendorShortcutWanted},
            {"isolationOwnerActive", g_oemVendorStackWanted},
            {"wakeGuardActive", oemVendorStackShieldWakeAliveLocked(GetTickCount64())},
            {"wakeGuardGeneration", g_oemVendorShieldWake.generation},
            {"wakeGuardWindowMs", kOemVendorShieldWakeWindowMs},
            {"wakeGuardIntervalMs", kOemVendorShieldWakeIntervalMs},
            {"serviceInventory", g_oemVendorMsiServiceInventory},
            {"protectedServiceObservation", g_oemVendorMsiProtectedServiceObservation},
            {"applied", current && completed && g_oemVendorStackLastOutcomeOk},
            {"pending", !completed},
            {"lastOutcomeKnown", journalValid && g_oemVendorStackLastOutcomeKnown},
            {"lastOutcomeOk", journalValid && g_oemVendorStackLastOutcomeOk},
            {"lastOutcome", journalValid ? g_oemVendorStackLastOutcome : "journal-unreadable-or-unbound"},
            {"requestedEpoch", g_oemVendorStackRequested},
            {"completedEpoch", g_oemVendorStackCompleted}};
}
static void oemVendorStackRestore(const char* reason) {
    { std::lock_guard<std::mutex> lock(g_oemVendorStackMx); g_oemVendorStackWanted = false;
      g_oemVendorShortcutWanted = false; g_oemVendorRecordingWanted = false; g_oemVendorShieldWanted = false;
      oemVendorStackShieldWakeRefreshLocked(GetTickCount64());
      g_oemVendorStackIntentControllerOnly = false;
      g_oemVendorStackLastOutcomeKnown = false; g_oemVendorStackLastOutcome = "pending";
      ++g_oemVendorStackRequested; g_oemVendorStackAttempts = 0; g_oemVendorStackRetryAt = 0; }
    std::lock_guard<std::mutex> io(g_oemVendorStackIoMx);
    (void)oemVendorStackRestoreLocked(reason);
}
