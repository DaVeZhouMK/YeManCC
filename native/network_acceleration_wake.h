#pragma once
// Included after the update-page accelerator helpers. No timer/thread/polling is
// owned here: the root schedules the request after public Ready.
struct NetworkAccelerationWakeScope {
    std::uint64_t generation = 0, suspendSerial = 0;
    bool matches(const ymcc::sleeppower::SharedPowerV1& power) const noexcept {
        return generation == power.generation && suspendSerial == power.suspendSerial;
    }
};
#ifndef YMCC_NETWORK_ACCEL_WAKE_FIXTURE
static constexpr const wchar_t* kNetworkAccelerationTaskNames[] = {L"Steamcommunity302 V15", L"Steamcommunity_302"};
static constexpr wchar_t kNetworkAccelerationDir[] = L"C:\\SOFT\\steamcommunity";
static constexpr wchar_t kNetworkAccelerationServiceArgs[] = L"--start-service";
struct NetworkAccelerationTasks { unsigned present = 0, enabled = 0; };

static std::wstring networkAccelerationTaskText(BSTR text, bool unquote = false) {
    std::wstring value = text ? std::wstring(text, SysStringLen(text)) : L"";
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    value = value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
    if (unquote && value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
        value = value.substr(1, value.size() - 2);
    return value;
}

static HRESULT networkAccelerationTaskValidate(IRegisteredTask* task, unsigned slot, VARIANT_BOOL& enabled) {
        HRESULT hr = task->get_Enabled(&enabled);
        ComPtr<ITaskDefinition> definition; ComPtr<ITaskSettings> settings;
        TASK_INSTANCES_POLICY instances{};
        if (SUCCEEDED(hr)) hr = task->get_Definition(&definition);
        if (SUCCEEDED(hr)) hr = definition->get_Settings(&settings);
        if (SUCCEEDED(hr)) hr = settings->get_MultipleInstances(&instances);
        if (SUCCEEDED(hr) && instances != TASK_INSTANCES_IGNORE_NEW) hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        VARIANT_BOOL demand{};
        if (SUCCEEDED(hr)) hr = settings->get_AllowDemandStart(&demand);
        if (SUCCEEDED(hr) && demand == VARIANT_FALSE) hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        ComPtr<ITriggerCollection> triggers; LONG triggerCount = 0;
        if (SUCCEEDED(hr)) hr = definition->get_Triggers(&triggers);
        if (SUCCEEDED(hr)) hr = triggers->get_Count(&triggerCount);
        if (SUCCEEDED(hr) && triggerCount != 1) hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        if (SUCCEEDED(hr)) {
            ComPtr<ITrigger> trigger; TASK_TRIGGER_TYPE2 type{}; VARIANT_BOOL triggerEnabled{};
            hr = triggers->get_Item(1, &trigger);
            if (SUCCEEDED(hr)) hr = trigger->get_Type(&type);
            if (SUCCEEDED(hr)) hr = trigger->get_Enabled(&triggerEnabled);
            if (SUCCEEDED(hr) && (type != TASK_TRIGGER_LOGON || triggerEnabled == VARIANT_FALSE))
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        ComPtr<IActionCollection> actions; ComPtr<IAction> action; ComPtr<IExecAction> exec;
        LONG count = 0;
        if (SUCCEEDED(hr)) hr = definition->get_Actions(&actions);
        if (SUCCEEDED(hr)) hr = actions->get_Count(&count);
        if (SUCCEEDED(hr) && count != 1) hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        if (SUCCEEDED(hr)) hr = actions->get_Item(1, &action);
        if (SUCCEEDED(hr)) hr = action.As(&exec);
        BSTR path = nullptr, arguments = nullptr, directory = nullptr;
        if (SUCCEEDED(hr)) hr = exec->get_Path(&path);
        if (SUCCEEDED(hr)) hr = exec->get_Arguments(&arguments);
        if (SUCCEEDED(hr)) hr = exec->get_WorkingDirectory(&directory);
        const auto image = networkAccelerationTaskText(path, true);
        const auto args = networkAccelerationTaskText(arguments);
        const auto cwd = networkAccelerationTaskText(directory);
        SysFreeString(path); SysFreeString(arguments); SysFreeString(directory);
        const auto expectedArgs = slot == 0 ? kNetworkAccelerationServiceArgs : L"";
        if (SUCCEEDED(hr) && (_wcsicmp(image.c_str(), g_updateAccelPath.c_str()) != 0 || args != expectedArgs ||
                _wcsicmp(cwd.c_str(), kNetworkAccelerationDir) != 0)) hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        return hr;
}

static bool readNetworkAccelerationTasks(NetworkAccelerationTasks& facts) {
    facts = {};
    for (unsigned slot = 0; slot < std::size(kNetworkAccelerationTaskNames); ++slot) {
        bool exists = false;
        if (!oemVendorTaskAccess(kNetworkAccelerationTaskNames[slot], [&](IRegisteredTask* task) {
            VARIANT_BOOL enabled{};
            const auto hr = networkAccelerationTaskValidate(task, slot, enabled);
            if (FAILED(hr)) { SetLastError(static_cast<DWORD>(hr)); return false; }
            if (enabled != VARIANT_FALSE) facts.enabled |= 1u << slot;
            return true;
        }, exists)) return false;
        if (exists) facts.present |= 1u << slot;
    }
    return true;
}

static bool setNetworkAccelerationTaskEnabled(unsigned slot, bool enabled, bool expected) {
    bool exists = false;
    const bool ok = oemVendorTaskAccess(kNetworkAccelerationTaskNames[slot], [&](IRegisteredTask* task) {
        VARIANT_BOOL live{};
        HRESULT hr = networkAccelerationTaskValidate(task, slot, live);
        if (SUCCEEDED(hr) && (live != VARIANT_FALSE) != expected) hr = HRESULT_FROM_WIN32(ERROR_RETRY);
        if (SUCCEEDED(hr)) hr = task->put_Enabled(enabled ? VARIANT_TRUE : VARIANT_FALSE);
        if (FAILED(hr)) SetLastError(static_cast<DWORD>(hr));
        return SUCCEEDED(hr);
    }, exists);
    if (ok && !exists) SetLastError(ERROR_FILE_NOT_FOUND);
    return ok && exists;
}

static std::mutex g_networkAccelerationTaskMutex;
static json networkAccelerationBootTask(const json& args) {
    if (!args.is_object() || (args.contains("enabled") && args.contains("restore")))
        return {{"ok", false}, {"win32Error", ERROR_INVALID_PARAMETER}};
    std::lock_guard<std::mutex> lock(g_networkAccelerationTaskMutex);
    NetworkAccelerationTasks before;
    if (!readNetworkAccelerationTasks(before)) return {{"ok", false}, {"win32Error", GetLastError()}};
    const auto result = [&](unsigned enabled) -> json {
        return {{"ok", true}, {"present", before.present}, {"enabled", enabled}};
    };
    if (!args.contains("enabled") && !args.contains("restore")) return result(before.enabled);
    unsigned desired;
    if (args.contains("restore")) {
        if (!args["restore"].is_number_unsigned() || !args.contains("expectedEnabled") ||
            !args["expectedEnabled"].is_number_unsigned() || !args.contains("expectedPresent") ||
            !args["expectedPresent"].is_number_unsigned() ||
            args["expectedEnabled"] != before.enabled || args["expectedPresent"] != before.present ||
            args["restore"].get<unsigned long long>() > 3)
            return {{"ok", false}, {"win32Error", ERROR_RETRY}};
        desired = args["restore"].get<unsigned>();
        if (desired & ~before.present) return {{"ok", false}, {"win32Error", ERROR_FILE_NOT_FOUND}};
    } else {
        if (!args["enabled"].is_boolean()) return {{"ok", false}, {"win32Error", ERROR_INVALID_PARAMETER}};
        desired = args["enabled"].get<bool>() ? (before.present & 1u ? 1u : before.present & 2u) : 0u;
    }
    if (desired == before.enabled) return result(desired);
    unsigned updated = before.enabled;
    DWORD error = ERROR_SUCCESS;
    // Disable the other task before enabling the chosen owner. Never stop a running session.
    for (bool enable : {false, true}) {
        for (unsigned slot = 0; slot < std::size(kNetworkAccelerationTaskNames); ++slot) {
            const unsigned bit = 1u << slot;
            const bool want = (desired & bit) != 0, had = (updated & bit) != 0;
            if (want == had || want != enable) continue;
            if (!setNetworkAccelerationTaskEnabled(slot, want, had)) { error = GetLastError(); break; }
            updated ^= bit;
        }
        if (error) break;
    }
    NetworkAccelerationTasks after;
    if (!error) {
        if (!readNetworkAccelerationTasks(after)) error = GetLastError();
        else if (after.present != before.present || after.enabled != desired) error = ERROR_RETRY;
    }
    if (!error) return result(desired);
    bool rollbackOk = true;
    for (unsigned slot = 0; slot < std::size(kNetworkAccelerationTaskNames); ++slot) {
        const unsigned bit = 1u << slot;
        if ((updated ^ before.enabled) & bit)
            rollbackOk = setNetworkAccelerationTaskEnabled(slot, (before.enabled & bit) != 0, (updated & bit) != 0) && rollbackOk;
    }
    return {{"ok", false}, {"win32Error", error}, {"rollbackOk", rollbackOk}};
}

static bool networkAccelerationWakeTaskEnabled(bool& enabled) {
    NetworkAccelerationTasks facts;
    const bool ok = readNetworkAccelerationTasks(facts);
    enabled = ok && facts.enabled != 0;
    return ok;
}
static bool networkAccelerationWakeSubmit(const ymcc::sleeppower::SharedPowerV1& power, bool& powerDeferred) {
    std::lock_guard<std::mutex> lock(g_networkAccelerationTaskMutex);
    bool runAttempted = false;
    const auto fail = [&] {
        // A task query may fail before the final power check is reached.
        if (!runAttempted && !powerDeferred) {
            const DWORD error = GetLastError();
            powerDeferred = !sharedPowerLifecycleCurrent(power);
            SetLastError(error);
        }
        return false;
    };
    NetworkAccelerationTasks facts;
    if (!readNetworkAccelerationTasks(facts)) return fail();
    if (!facts.enabled) { SetLastError(ERROR_CANCELLED); return fail(); }
    const unsigned slot = facts.enabled & 1u ? 0 : 1;
    bool exists = false;
    const bool ok = oemVendorTaskAccess(kNetworkAccelerationTaskNames[slot], [&](IRegisteredTask* task) {
        VARIANT_BOOL enabled{};
        HRESULT hr = networkAccelerationTaskValidate(task, slot, enabled);
        if (SUCCEEDED(hr) && enabled == VARIANT_FALSE) hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        VARIANT empty{}; VariantInit(&empty); ComPtr<IRunningTask> running;
        if (SUCCEEDED(hr) && !sharedPowerLifecycleCurrent(power)) {
            powerDeferred = true; // Run has not been called.
            hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }
        if (SUCCEEDED(hr)) {
            runAttempted = true;
            hr = task->Run(empty, &running); // IgnoreNew arbitrates a concurrent logon/start request.
            if (SUCCEEDED(hr)) ymcc::processInventory::invalidate();
        }
        if (FAILED(hr)) SetLastError(static_cast<DWORD>(hr));
        return SUCCEEDED(hr);
    }, exists);
    if (ok && exists) return true;
    if (ok) SetLastError(ERROR_FILE_NOT_FOUND);
    return fail();
}
static bool startUpdateAccel(const UpdateAccelProcessSnapshot& before) {
    const auto running = updateAccelRunningIn(before);
    if (!running.has_value()) return false;
    if (*running) return true;
    std::lock_guard<std::mutex> lock(g_networkAccelerationTaskMutex);
    NetworkAccelerationTasks facts;
    if (!readNetworkAccelerationTasks(facts)) return false;
    std::error_code error;
    if (!fspath::is_regular_file(g_updateAccelPath, error) || error) return false;
    const auto args = facts.present & 1u ? kNetworkAccelerationServiceArgs : nullptr;
    const HINSTANCE result = ymcc::processEfficiency::shellExecute(nullptr, L"open", g_updateAccelPath.c_str(), args, kNetworkAccelerationDir, SW_HIDE);
    const bool accepted = reinterpret_cast<intptr_t>(result) > 32;
    if (accepted) ymcc::processInventory::invalidate();
    return accepted;
}
#else
static bool networkAccelerationWakeTaskEnabled(bool&);
static bool networkAccelerationWakeSubmit(const ymcc::sleeppower::SharedPowerV1&, bool&);
#endif
static std::mutex g_networkAccelerationWakeMutex;
static NetworkAccelerationWakeScope g_networkAccelerationWakeChecked;
static json recheckNetworkAccelerationAfterWake(NetworkAccelerationWakeScope scope) {
    using namespace ymcc::sleeppower;
    std::lock_guard<std::mutex> lock(g_networkAccelerationWakeMutex);
    auto result = [&](bool ok, const char* reason, bool submitted = false, DWORD error = ERROR_SUCCESS) -> json {
        const json facts{{"generation", scope.generation}, {"suspendSerial", scope.suspendSerial}, {"ok", ok}, {"reason", reason},
            {"submitted", submitted}, {"runningConfirmed", std::string(reason) == "already-running"}, {"win32Error", error}};
        appendNativeLifecycleLog("network-acceleration-wake", facts);
        return facts;
    };
    const auto statusBeforeLaunch = [&](const SharedPowerV1& current) {
        return g_exitRequested.load(std::memory_order_acquire) || (current.generation && !scope.matches(current))
            ? "stale-or-suspended" : "power-deferred-before-launch";
    };
    SharedPowerV1 power;
    const bool observed = readSharedSystemPower(&power, sizeof(power)) == 1;
    if (!observed || !scope.matches(power) || g_exitRequested.load(std::memory_order_acquire))
        return result(false, statusBeforeLaunch(power));
    if (g_networkAccelerationWakeChecked.matches(power))
        return {{"generation", scope.generation}, {"suspendSerial", scope.suspendSerial}, {"ok", true},
            {"reason", "already-checked"}, {"submitted", false}};
    if (!powerAdmitted(power)) return result(false, statusBeforeLaunch(power));
    const auto previouslyChecked = g_networkAccelerationWakeChecked;
    g_networkAccelerationWakeChecked = scope;
    const auto retryableBeforeLaunch = [&](const char* reason, DWORD error) {
        g_networkAccelerationWakeChecked = previouslyChecked;
        return result(false, reason, false, error);
    };
    const auto deferBeforeLaunch = [&] {
        SharedPowerV1 current;
        readSharedSystemPower(&current, sizeof(current));
        return retryableBeforeLaunch(statusBeforeLaunch(current), ERROR_SUCCESS);
    };
    bool enabled = false;
    const bool taskKnown = networkAccelerationWakeTaskEnabled(enabled);
    const DWORD taskError = taskKnown ? ERROR_SUCCESS : GetLastError();
    if (!sharedPowerLifecycleCurrent(power)) return deferBeforeLaunch();
    if (!taskKnown) return retryableBeforeLaunch("startup-state-unavailable", taskError);
    if (!enabled) return result(true, "startup-disabled");
    // One fresh receipt from the same owner as updateAccel.get/updateAccel.set.
    // UNKNOWN cannot authorize startup; IgnoreNew arbitrates concurrent task runs.
    const auto running = readUpdateAccelRunning(0);
    const DWORD processError = running.has_value() ? ERROR_SUCCESS : GetLastError();
    if (!sharedPowerLifecycleCurrent(power)) return deferBeforeLaunch();
    if (!running.has_value()) return retryableBeforeLaunch("process-state-unavailable", processError);
    if (*running) return result(true, "already-running");
    std::error_code error;
    const bool exists = fspath::is_regular_file(g_updateAccelPath, error);
    if (!sharedPowerLifecycleCurrent(power)) return deferBeforeLaunch();
    if (!exists || error) return result(false, "executable-unavailable", false, error ? static_cast<DWORD>(error.value()) : ERROR_FILE_NOT_FOUND);
    bool blockedBeforeRun = false;
    const bool submitted = networkAccelerationWakeSubmit(power, blockedBeforeRun);
    if (blockedBeforeRun) return deferBeforeLaunch();
    return result(submitted, submitted ? "launch-request-accepted" : "launch-request-failed", submitted, submitted ? ERROR_SUCCESS : GetLastError());
}
