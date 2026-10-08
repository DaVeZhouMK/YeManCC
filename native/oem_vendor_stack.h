#pragma once
// HC ISpaceWatcher parity, with durable original-state leases and no new worker.
// Included only after MachineIdentity/kOemBit*/json and serial-worker declarations.
#ifndef YMCC_VENDOR_STACK_FIXTURE
#include <taskschd.h>
#pragma comment(lib, "taskschd.lib")
#endif

struct OemVendorServiceLease {
    std::wstring name;
    DWORD originalStartType = SERVICE_DEMAND_START;
    bool wasRunning = false;
    DWORD originalState = 0; // R13 schema1 infers Running/Stopped from wasRunning.
};
struct OemVendorTaskLease { std::wstring name; bool wasEnabled = false, wasRunning = false; };
struct OemVendorTaskFacts { bool exists = false, enabled = false, running = false; };
static std::mutex g_oemVendorStackMx, g_oemVendorStackIoMx;
static std::vector<OemVendorServiceLease> g_oemVendorServiceLeases;
static std::vector<OemVendorTaskLease> g_oemVendorTaskLeases;
static bool g_oemVendorStackWanted = false, g_oemVendorStackOpQueued = false;
static bool g_oemVendorStackIntentSeen = false, g_oemVendorStackStartupRecovered = false;
static YmccFamily g_oemVendorStackIntentFamily = YmccFamily::Unknown;
static unsigned long long g_oemVendorStackRequested = 1, g_oemVendorStackCompleted = 0;
static bool g_oemVendorStackLoaded = false, g_oemVendorStackJournalValid = true;
static unsigned g_oemVendorStackAttempts = 0;
static ULONGLONG g_oemVendorStackRetryAt = 0;
static constexpr DWORD kOemVendorServiceBudgetMs = 10000;
static constexpr unsigned kOemVendorMaxAttempts = 3;
static std::vector<std::wstring> oemVendorServiceNamesForFamily(YmccFamily family) {
    switch (family) {
    case YmccFamily::AsusRogAllyX: case YmccFamily::AsusRogAlly:
    case YmccFamily::AsusRogAllyClassic: case YmccFamily::AsusRogAllyXClassic:
        return {L"ArmouryCrateSEService", L"AsusAppService", L"ArmouryCrateControlInterface"};
    case YmccFamily::LenovoLegionGo: return {L"DAService"};
    case YmccFamily::MsiClaw: return {L"MSI Foundation Service"};
    case YmccFamily::ZotacGamingZone: return {L"ZotacHandheldDatabaseService", L"ZotacHandheldService"};
    default: return {};
    }
}
static std::vector<std::wstring> oemVendorTaskNamesForFamily(YmccFamily family) {
    return family == YmccFamily::MsiClaw
        ? std::vector<std::wstring>{L"MSI_Center_M_Server", L"MSI_Center_M_Updater"} : std::vector<std::wstring>{};
}
static std::vector<std::wstring> oemVendorProcessNamesForFamily(YmccFamily family) {
    switch (family) {
    case YmccFamily::LenovoLegionGo: return {L"LegionGoQuickSettings", L"LegionSpace", L"LSDaemon"};
    case YmccFamily::MsiClaw: return {L"MSI_Center_M_Server", L"MSI Center M", L"MCMOSDInfo", L"MSI Center OSD Info"};
    case YmccFamily::ZotacGamingZone: return {L"ZotacHandheldQuickSetting"};
    default: return {}; // HC declares no ROG executable names. Never guess one.
    }
}
static bool oemVendorStackFamilySupported(YmccFamily family) {
    return !oemVendorServiceNamesForFamily(family).empty();
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
        return is("ClawA1M") || is("ClawA2VM") || is("ClawBZ2EM") || is("ClawCG3EM");
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
    return g_oemVendorStackWanted && epoch == g_oemVendorStackRequested;
}

#ifndef YMCC_VENDOR_STACK_FIXTURE
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
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    bool ok = true; unsigned matched = 0; const auto deadline = GetTickCount64() + 5000;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    BOOL more = Process32FirstW(snapshot, &entry);
    if (!more && GetLastError() != ERROR_NO_MORE_FILES) ok = false;
    for (; more; more = Process32NextW(snapshot, &entry)) {
        if (!oemVendorProcessMatches(entry.szExeFile, names)) continue;
        if (++matched > 32 || GetTickCount64() >= deadline || !oemVendorStackIntentCurrent(epoch)) { ok = false; break; }
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
        DWORD error = ERROR_SUCCESS; bool stopped = false;
        if (process) {
            wchar_t image[32768]; DWORD size = static_cast<DWORD>(std::size(image));
            DWORD code = 0;
            if (GetExitCodeProcess(process, &code) && code != STILL_ACTIVE) stopped = true;
            else if (QueryFullProcessImageNameW(process, 0, image, &size) &&
                     oemVendorProcessMatches(std::wstring(image, size), names) && oemVendorStackIntentCurrent(epoch)) {
                stopped = TerminateProcess(process, 0) != FALSE &&
                    WaitForSingleObject(process, static_cast<DWORD>(std::min<ULONGLONG>(2000, GetTickCount64() < deadline ? deadline - GetTickCount64() : 0))) == WAIT_OBJECT_0;
            }
            if (!stopped) error = GetLastError() ? GetLastError() : ERROR_TIMEOUT;
            CloseHandle(process);
        } else { error = GetLastError(); stopped = error == ERROR_INVALID_PARAMETER; }
        appendNativeLifecycleLog("oem-vendor-stack", {{"state", stopped ? "process-stopped" : "process-stop-failed"},
            {"process", W2U(entry.szExeFile)}, {"pid", entry.th32ProcessID}, {"win32Error", error},
            {"restorePolicy", "HC-services-and-tasks-only"}});
        ok = ok && stopped;
    }
    // A snapshot enumeration failure is not proof that no vendor process remains.
    // Process32NextW reports normal completion specifically as ERROR_NO_MORE_FILES.
    if (!more) {
        const DWORD error = GetLastError();
        if (error != ERROR_NO_MORE_FILES) {
            ok = false;
            appendNativeLifecycleLog("oem-vendor-stack", {{"state", "process-enumeration-failed"}, {"win32Error", error}});
        }
    }
    CloseHandle(snapshot); return ok;
}
#else
static bool oemVendorTaskQuery(const std::wstring&, OemVendorTaskFacts&);
static bool oemVendorTaskSet(const std::wstring&, bool, bool, bool);
static bool oemVendorProcessesStop(const std::vector<std::wstring>&, unsigned long long);
#endif

static std::wstring oemVendorStackJournalPath() { return POWER_CONTROL_DIR + L"\\oem-vendor-stack-lease.json"; }
static bool oemVendorStackSaveJournal() {
    json services = json::array(), tasks = json::array();
    for (const auto& lease : g_oemVendorServiceLeases)
        services.push_back({{"name", W2U(lease.name)}, {"originalStartType", lease.originalStartType}, {"wasRunning", lease.wasRunning}, {"originalState", lease.originalState ? lease.originalState : lease.wasRunning ? SERVICE_RUNNING : SERVICE_STOPPED}});
    for (const auto& lease : g_oemVendorTaskLeases)
        tasks.push_back({{"name", W2U(lease.name)}, {"wasEnabled", lease.wasEnabled}, {"wasRunning", lease.wasRunning}});
    return sgWriteFileAtomic(oemVendorStackJournalPath(), json({{"schema", 2},
        {"family", static_cast<int>(g_machineIdentity.family)}, {"services", services}, {"tasks", tasks}}).dump(2));
}
static bool oemVendorStackLoadJournal() {
    if (g_oemVendorStackLoaded) return g_oemVendorStackJournalValid;
    g_oemVendorStackLoaded = true;
    const auto attr = GetFileAttributesW(oemVendorStackJournalPath().c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return true;
        g_oemVendorStackJournalValid = false;
    } else try {
        const auto doc = json::parse(sgReadFile(oemVendorStackJournalPath()));
        const auto names = oemVendorServiceNamesForFamily(g_machineIdentity.family);
        const auto taskNames = oemVendorTaskNamesForFamily(g_machineIdentity.family);
        const int schema = doc.at("schema").get<int>();
        if ((schema != 1 && schema != 2) || doc.at("family") != static_cast<int>(g_machineIdentity.family) ||
            !doc.at("services").is_array() || doc.at("services").size() > names.size()) throw 0;
        std::vector<OemVendorServiceLease> services;
        for (const auto& item : doc.at("services")) {
            OemVendorServiceLease lease{U2W(item.at("name").get<std::string>()), item.at("originalStartType").get<DWORD>(), item.at("wasRunning").get<bool>()};
            lease.originalState = item.value("originalState", lease.wasRunning ? SERVICE_RUNNING : SERVICE_STOPPED);
            if ((lease.originalState != SERVICE_RUNNING && lease.originalState != SERVICE_STOPPED && lease.originalState != SERVICE_PAUSED) ||
                lease.wasRunning != (lease.originalState != SERVICE_STOPPED) || std::find(names.begin(), names.end(), lease.name) == names.end() ||
                lease.originalStartType < SERVICE_AUTO_START || lease.originalStartType > SERVICE_DISABLED ||
                std::any_of(services.begin(), services.end(), [&](const auto& x){return x.name == lease.name;})) throw 0;
            services.push_back(std::move(lease));
        }
        std::vector<OemVendorTaskLease> tasks;
        if (schema == 2) {
            if (!doc.at("tasks").is_array() || doc.at("tasks").size() > taskNames.size()) throw 0;
            for (const auto& item : doc.at("tasks")) {
                OemVendorTaskLease lease{U2W(item.at("name").get<std::string>()), item.at("wasEnabled").get<bool>(), item.at("wasRunning").get<bool>()};
                if (std::find(taskNames.begin(), taskNames.end(), lease.name) == taskNames.end() ||
                    std::any_of(tasks.begin(), tasks.end(), [&](const auto& x){return x.name == lease.name;})) throw 0;
                tasks.push_back(std::move(lease));
            }
        }
        g_oemVendorServiceLeases = std::move(services); g_oemVendorTaskLeases = std::move(tasks); return true;
    } catch (...) { g_oemVendorStackJournalValid = false; }
    appendNativeLifecycleLog("oem-vendor-stack", {{"state", "recovery-held"}, {"reason", "journal-unreadable-or-unbound"}}); return false;
}
static bool oemVendorStackRestoreLocked(const char* reason) {
    if (!oemVendorStackLoadJournal()) return false;
    if (g_oemVendorServiceLeases.empty() && g_oemVendorTaskLeases.empty()) return true;
    SC_HANDLE scm = g_oemVendorServiceLeases.empty() ? nullptr : OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    std::vector<OemVendorServiceLease> pending;
    for (const auto& lease : g_oemVendorServiceLeases) {
        bool ok = false; DWORD type = 0, state = 0, error = ERROR_SUCCESS;
        SC_HANDLE service = scm ? OpenServiceW(scm, lease.name.c_str(), SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_PAUSE_CONTINUE | SERVICE_CHANGE_CONFIG) : nullptr;
        if (service && oemVendorServiceQueryConfig(service, type)) {
            bool configOk = type == lease.originalStartType;
            if (!configOk && type == SERVICE_DISABLED)
                configOk = ChangeServiceConfigW(service, SERVICE_NO_CHANGE, lease.originalStartType, SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != FALSE;
            configOk = configOk && oemVendorServiceQueryConfig(service, type) && type == lease.originalStartType;
            bool runningOk = oemVendorServiceStable(service, state);
            const DWORD originalState = lease.originalState ? lease.originalState : lease.wasRunning ? SERVICE_RUNNING : SERVICE_STOPPED;
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
            if (configOk && runningOk && lease.wasRunning) runningOk = oemVendorServiceQueryState(service, state) && state == originalState;
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
    for (const auto& lease : g_oemVendorTaskLeases) {
        OemVendorTaskFacts facts; bool ok = oemVendorTaskQuery(lease.name, facts);
        if (ok && facts.exists) {
            if (facts.enabled != lease.wasEnabled || (lease.wasRunning && lease.wasEnabled && !facts.running))
                ok = oemVendorTaskSet(lease.name, lease.wasEnabled, false, lease.wasRunning && lease.wasEnabled && !facts.running);
            ok = ok && oemVendorTaskQuery(lease.name, facts) && facts.enabled == lease.wasEnabled;
        }
        // An absent task is not mutated; keep its recovery debt for explicit review.
        ok = ok && facts.exists;
        appendNativeLifecycleLog("oem-vendor-stack", {{"state", ok ? "task-restored" : "task-restore-failed"},
            {"task", W2U(lease.name)}, {"enabledReadback", facts.enabled}, {"win32Error", ok ? ERROR_SUCCESS : GetLastError()}});
        if (!ok) pendingTasks.push_back(lease);
    }
    // Do not forget restored entries until the updated journal is durable.
    auto oldServices = std::move(g_oemVendorServiceLeases); auto oldTasks = std::move(g_oemVendorTaskLeases);
    g_oemVendorServiceLeases = std::move(pending); g_oemVendorTaskLeases = std::move(pendingTasks);
    if (!oemVendorStackSaveJournal()) {
        g_oemVendorServiceLeases = std::move(oldServices); g_oemVendorTaskLeases = std::move(oldTasks);
        appendNativeLifecycleLog("oem-vendor-stack", {{"state", "restore-held"}, {"reason", "journal-write-failed"}}); return false;
    }
    return g_oemVendorServiceLeases.empty() && g_oemVendorTaskLeases.empty();
}
static bool oemVendorStackApplyIfNeeded(const char* reason) {
    const auto names = oemVendorServiceNamesForFamily(g_machineIdentity.family);
    if (names.empty() || g_exitRequested.load(std::memory_order_acquire)) return names.empty();
    if (!oemVendorStackMachineSupported()) return false;
    unsigned long long epoch;
    { std::lock_guard<std::mutex> lock(g_oemVendorStackMx); epoch = g_oemVendorStackRequested; }
    if (!oemVendorStackIntentCurrent(epoch)) return false;
    bool allOk = true; size_t existing = 0, confirmed = 0, unavailable = 0;
    // Match HC: disable MSI startup tasks before stopping its service/processes.
    for (const auto& name : oemVendorTaskNamesForFamily(g_machineIdentity.family)) {
        if (!oemVendorStackIntentCurrent(epoch)) return false;
        OemVendorTaskFacts facts;
        if (!oemVendorTaskQuery(name, facts)) {
            allOk = false;
            appendNativeLifecycleLog("oem-vendor-stack", {{"state", "task-unavailable"}, {"task", W2U(name)}, {"win32Error", GetLastError()}});
            continue;
        }
        if (!facts.exists) {
            appendNativeLifecycleLog("oem-vendor-stack", {{"state", "task-absent"}, {"task", W2U(name)}});
            continue;
        }
        auto lease = std::find_if(g_oemVendorTaskLeases.begin(), g_oemVendorTaskLeases.end(), [&](const auto& x){return x.name == name;});
        if ((facts.enabled || facts.running) && lease == g_oemVendorTaskLeases.end()) {
            g_oemVendorTaskLeases.push_back({name, facts.enabled, facts.running});
            if (!oemVendorStackSaveJournal()) { g_oemVendorTaskLeases.pop_back(); return false; }
        }
        bool ok = true;
        if (facts.enabled || facts.running) ok = oemVendorStackIntentCurrent(epoch) && oemVendorTaskSet(name, false, facts.running, false);
        const auto deadline = GetTickCount64() + 2000;
        while (ok && oemVendorTaskQuery(name, facts) && facts.running && GetTickCount64() < deadline) Sleep(50);
        ok = ok && oemVendorTaskQuery(name, facts) && facts.exists && !facts.enabled && !facts.running;
        appendNativeLifecycleLog("oem-vendor-stack", {{"state", ok ? "task-disabled" : "task-disable-failed"},
            {"task", W2U(name)}, {"enabledReadback", facts.enabled}, {"runningReadback", facts.running}, {"win32Error", ok ? ERROR_SUCCESS : GetLastError()}});
        allOk = allOk && ok;
    }
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) { appendNativeLifecycleLog("oem-vendor-stack", {{"state", "apply-failed"}, {"reason", "scm-open-failed"}, {"win32Error", GetLastError()}}); return false; }
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
    CloseServiceHandle(scm);
    if (oemVendorStackIntentCurrent(epoch)) allOk = oemVendorProcessesStop(oemVendorProcessNamesForFamily(g_machineIdentity.family), epoch) && allOk;
    else allOk = false;
    // Missing declared services are explicit, not an assertion of a suppressed key.
    allOk = allOk && existing > 0 && confirmed == existing && unavailable == 0;
    appendNativeLifecycleLog("oem-vendor-stack", {{"state", allOk ? "applied" : "partial-or-unavailable"}, {"existingServices", existing},
        {"unreadableServices", unavailable}, {"confirmedStoppedServices", confirmed}, {"externalDefaultActionSuppressed", "DEVICE_UNVERIFIED"}});
    return allOk;
}
static void oemVendorStackSchedule() {
    { std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
      if (g_oemVendorStackOpQueued || g_oemVendorStackRequested == g_oemVendorStackCompleted ||
          g_exitRequested.load(std::memory_order_acquire) || g_machineIdentity.family == YmccFamily::Unknown ||
          (g_oemVendorStackRetryAt && GetTickCount64() < g_oemVendorStackRetryAt)) return;
      g_oemVendorStackOpQueued = true; }
    const bool accepted = gamepadSerialSubmit([] {
        unsigned long long epoch; bool wanted;
        { std::lock_guard<std::mutex> lock(g_oemVendorStackMx); epoch = g_oemVendorStackRequested; wanted = g_oemVendorStackWanted; }
        bool ok = false;
        { std::lock_guard<std::mutex> io(g_oemVendorStackIoMx);
          if (!g_oemVendorStackStartupRecovered) g_oemVendorStackStartupRecovered = oemVendorStackRestoreLocked("startup-recovery");
          if (g_oemVendorStackStartupRecovered) {
              if (wanted && !g_exitRequested.load(std::memory_order_acquire)) ok = oemVendorStackApplyIfNeeded("configured-oem-rule");
              else ok = oemVendorStackRestoreLocked("intent-cleared");
          }
        }
        bool exhausted = false;
        { std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
          g_oemVendorStackOpQueued = false;
          if (epoch == g_oemVendorStackRequested) {
              ++g_oemVendorStackAttempts;
              exhausted = !ok && g_oemVendorStackAttempts >= kOemVendorMaxAttempts;
              if (ok || exhausted) { g_oemVendorStackCompleted = epoch; g_oemVendorStackRetryAt = 0; }
              else g_oemVendorStackRetryAt = GetTickCount64() + (g_oemVendorStackAttempts == 1 ? 1000 : 3000);
          }
        }
        if (exhausted) appendNativeLifecycleLog("oem-vendor-stack", {{"state", "retry-exhausted"}, {"attempts", kOemVendorMaxAttempts}, {"externalDefaultActionSuppressed", "NOT_CONFIRMED"}});
        oemVendorStackSchedule(); // Newer intent only, or later existing 400ms refresh.
    });
    if (!accepted) { std::lock_guard<std::mutex> lock(g_oemVendorStackMx); g_oemVendorStackOpQueued = false; }
}
static void oemVendorStackRequest(bool wanted) {
    const auto family = g_machineIdentity.family;
    wanted = wanted && oemVendorStackFamilySupported(family) && oemVendorStackMachineSupported();
    bool changed = false;
    { std::lock_guard<std::mutex> lock(g_oemVendorStackMx);
      if (!g_oemVendorStackIntentSeen || g_oemVendorStackWanted != wanted || g_oemVendorStackIntentFamily != family) {
          g_oemVendorStackIntentSeen = true; g_oemVendorStackIntentFamily = family; g_oemVendorStackWanted = wanted;
          ++g_oemVendorStackRequested; g_oemVendorStackAttempts = 0; g_oemVendorStackRetryAt = 0; changed = true;
      } }
    if (changed) appendNativeLifecycleLog("oem-vendor-stack", {{"state", "intent"}, {"wanted", wanted},
        {"family", static_cast<int>(family)}, {"familySupported", oemVendorStackFamilySupported(family)}, {"deviceClass", g_machineIdentity.deviceClass}, {"modelSupported", oemVendorStackMachineSupported()}});
    oemVendorStackSchedule();
}
static void oemVendorStackRestore(const char* reason) {
    { std::lock_guard<std::mutex> lock(g_oemVendorStackMx); g_oemVendorStackWanted = false;
      ++g_oemVendorStackRequested; g_oemVendorStackAttempts = 0; g_oemVendorStackRetryAt = 0; }
    std::lock_guard<std::mutex> io(g_oemVendorStackIoMx);
    (void)oemVendorStackRestoreLocked(reason);
}
