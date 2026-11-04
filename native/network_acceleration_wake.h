#pragma once
// Included after the update-page accelerator helpers. No timer/thread/polling is
// owned here: the root schedules one request five seconds after a committed wake.
static bool networkAccelerationWakeCurrent(unsigned long long generation) {
    return generation && generation == currentPowerGeneration() &&
        !g_exitRequested.load(std::memory_order_acquire) && hardwareWriteAllowed();
}
#ifndef YMCC_NETWORK_ACCEL_WAKE_FIXTURE
static bool networkAccelerationWakeTaskEnabled(bool& enabled) {
    OemVendorTaskFacts facts;
    const bool ok = oemVendorTaskQuery(L"Steamcommunity_302", facts);
    enabled = ok && facts.exists && facts.enabled;
    return ok;
}
static bool networkAccelerationWakeSubmit(unsigned long long generation) {
    bool exists = false;
    const bool ok = oemVendorTaskAccess(L"Steamcommunity_302", [&](IRegisteredTask* task) {
        VARIANT_BOOL enabled{};
        HRESULT hr = task->get_Enabled(&enabled);
        if (SUCCEEDED(hr) && enabled == VARIANT_FALSE) hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        ComPtr<ITaskDefinition> definition; ComPtr<ITaskSettings> settings;
        TASK_INSTANCES_POLICY instances{};
        if (SUCCEEDED(hr)) hr = task->get_Definition(&definition);
        if (SUCCEEDED(hr)) hr = definition->get_Settings(&settings);
        if (SUCCEEDED(hr)) hr = settings->get_MultipleInstances(&instances);
        if (SUCCEEDED(hr) && instances != TASK_INSTANCES_IGNORE_NEW) hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        // Use the same registered 302 executable as the update page; never run
        // a task that has been replaced by an unrelated command.
        ComPtr<IActionCollection> actions; ComPtr<IAction> action; ComPtr<IExecAction> exec;
        LONG count = 0;
        if (SUCCEEDED(hr)) hr = definition->get_Actions(&actions);
        if (SUCCEEDED(hr)) hr = actions->get_Count(&count);
        if (SUCCEEDED(hr) && count != 1) hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        if (SUCCEEDED(hr)) hr = actions->get_Item(1, &action);
        if (SUCCEEDED(hr)) hr = action.As(&exec);
        BSTR path = nullptr, arguments = nullptr;
        if (SUCCEEDED(hr)) hr = exec->get_Path(&path);
        if (SUCCEEDED(hr)) hr = exec->get_Arguments(&arguments);
        std::wstring image = path ? std::wstring(path, SysStringLen(path)) : L"";
        const auto first = image.find_first_not_of(L" \t\r\n\"");
        const auto last = image.find_last_not_of(L" \t\r\n\"");
        image = first == std::wstring::npos ? L"" : image.substr(first, last - first + 1);
        const bool hasArguments = arguments && std::wstring(arguments, SysStringLen(arguments)).find_first_not_of(L" \t\r\n") != std::wstring::npos;
        SysFreeString(path); SysFreeString(arguments);
        if (SUCCEEDED(hr) && (_wcsicmp(image.c_str(), g_updateAccelPath.c_str()) != 0 || hasArguments)) hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        if (SUCCEEDED(hr) && !networkAccelerationWakeCurrent(generation)) hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        if (SUCCEEDED(hr)) {
            VARIANT empty{}; VariantInit(&empty); ComPtr<IRunningTask> running;
            hr = task->Run(empty, &running); // IgnoreNew arbitrates a concurrent logon/start request.
            if (SUCCEEDED(hr)) ymcc::processInventory::invalidate();
        }
        if (FAILED(hr)) SetLastError(static_cast<DWORD>(hr));
        return SUCCEEDED(hr);
    }, exists);
    if (ok && !exists) SetLastError(ERROR_FILE_NOT_FOUND);
    return ok && exists;
}
#else
static bool networkAccelerationWakeTaskEnabled(bool&);
static bool networkAccelerationWakeSubmit(unsigned long long);
#endif
static std::mutex g_networkAccelerationWakeMutex;
static unsigned long long g_networkAccelerationWakeChecked = 0;
static json recheckNetworkAccelerationAfterWake(unsigned long long generation) {
    std::lock_guard<std::mutex> lock(g_networkAccelerationWakeMutex);
    if (!networkAccelerationWakeCurrent(generation)) return {{"ok", false}, {"reason", "stale-or-suspended"}};
    if (generation <= g_networkAccelerationWakeChecked) return {{"ok", true}, {"reason", "already-checked"}, {"submitted", false}};
    g_networkAccelerationWakeChecked = generation;
    auto result = [&](bool ok, const char* reason, bool submitted = false, DWORD error = ERROR_SUCCESS) -> json {
        const json facts{{"generation", generation}, {"ok", ok}, {"reason", reason},
            {"submitted", submitted}, {"runningConfirmed", std::string(reason) == "already-running"}, {"win32Error", error}};
        appendNativeLifecycleLog("network-acceleration-wake", facts);
        return facts;
    };
    bool enabled = false;
    if (!networkAccelerationWakeTaskEnabled(enabled)) return result(false, "startup-state-unavailable", false, GetLastError());
    if (!enabled) return result(true, "startup-disabled");
    // One fresh receipt from the same owner as updateAccel.get/updateAccel.set.
    // UNKNOWN cannot authorize startup; IgnoreNew arbitrates concurrent task runs.
    const auto running = readUpdateAccelRunning(0);
    if (!running.has_value()) return result(false, "process-state-unavailable", false, GetLastError());
    if (*running) return result(true, "already-running");
    std::error_code error;
    const bool exists = fspath::is_regular_file(g_updateAccelPath, error);
    if (!exists || error) return result(false, "executable-unavailable", false, error ? static_cast<DWORD>(error.value()) : ERROR_FILE_NOT_FOUND);
    if (!networkAccelerationWakeCurrent(generation)) return result(false, "stale-or-suspended");
    const bool submitted = networkAccelerationWakeSubmit(generation);
    return result(submitted, submitted ? "launch-request-accepted" : "launch-request-failed", submitted, submitted ? ERROR_SUCCESS : GetLastError());
}
