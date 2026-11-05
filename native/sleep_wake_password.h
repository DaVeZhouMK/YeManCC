#pragma once
// The wake-password setting is owned by one fixed YMCC scheme, never SCHEME_CURRENT.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <powrprof.h>
#include <mutex>
#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "advapi32.lib")

namespace ymcc::sleep_wake_password {
inline constexpr GUID Scheme = {0x1cb8b882, 0xa900, 0x4b9f, {0x9b, 0xac, 0x99, 0xd1, 0x51, 0xe6, 0x44, 0x41}};
inline constexpr GUID Subgroup = {0xfea3413e, 0x7e05, 0x4911, {0x9a, 0x71, 0x70, 0x03, 0x31, 0xf1, 0xc2, 0x94}};
inline constexpr GUID Setting = {0x0e796bdb, 0x100d, 0x47d6, {0xa2, 0xd5, 0xf7, 0xd2, 0xda, 0xa5, 0x1f, 0x51}};
inline std::mutex transactionMutex;

struct Snapshot {
    DWORD ac = 0, dc = 0;
    DWORD error = ERROR_SUCCESS;
    bool known = false;
    bool enabled() const { return known && (ac == 1 || dc == 1); }
    bool mixed() const { return known && ac != dc; }
};
struct ApplyResult {
    bool ok = false;
    DWORD error = ERROR_SUCCESS;
    const char* stage = "read";
    bool changed = false;
    bool reactivated = false;
    DWORD reactivationError = ERROR_SUCCESS;
    bool rollbackOk = true;
    Snapshot state;
};

// Store only this scheme's setting. Policy-aware PowerWrite* may reject 1260
// before touching the plan. A GPO can still control sign-in independently of
// these saved indices; it is never edited or overridden here.
inline constexpr wchar_t SchemeKey[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes\\"
    L"1cb8b882-a900-4b9f-9bac-99d151e64441";
inline constexpr wchar_t SettingKey[] =
    L"fea3413e-7e05-4911-9a71-700331f1c294\\0e796bdb-100d-47d6-a2d5-f7d2daa51f51";
struct RegistryKey {
    HKEY value = nullptr;
    RegistryKey() = default;
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
    ~RegistryKey() { if (value) RegCloseKey(value); }
};
struct WindowsPowerApi {
    DWORD read(bool ac, DWORD& value) {
        RegistryKey scheme, setting;
        LONG error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, SchemeKey, 0, KEY_READ, &scheme.value);
        if (error != ERROR_SUCCESS) return static_cast<DWORD>(error);
        error = RegOpenKeyExW(scheme.value, SettingKey, 0, KEY_QUERY_VALUE, &setting.value);
        if (error == ERROR_SUCCESS) {
            DWORD type = 0, size = sizeof(value);
            error = RegQueryValueExW(setting.value, ac ? L"ACSettingIndex" : L"DCSettingIndex",
                nullptr, &type, reinterpret_cast<BYTE*>(&value), &size);
            if (error == ERROR_SUCCESS)
                return type == REG_DWORD && size == sizeof(value) ? ERROR_SUCCESS : ERROR_INVALID_DATA;
        }
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            return static_cast<DWORD>(error);
        // An existing plan can inherit an index without an explicit value yet.
        // Read its default only; a missing scheme above is never created.
        return ac ? PowerReadACValueIndex(nullptr, &Scheme, &Subgroup, &Setting, &value)
                  : PowerReadDCValueIndex(nullptr, &Scheme, &Subgroup, &Setting, &value);
    }
    DWORD write(bool ac, DWORD value) {
        if (value > 1) return ERROR_INVALID_DATA;
        RegistryKey scheme, setting;
        LONG error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, SchemeKey, 0, KEY_READ, &scheme.value);
        if (error != ERROR_SUCCESS) return static_cast<DWORD>(error);
        error = RegOpenKeyExW(scheme.value, SettingKey, 0, KEY_SET_VALUE, &setting.value);
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            // The root scheme must already exist. Only its one subgroup/setting
            // can be created to materialize inherited defaults.
            RegistryKey writableScheme;
            error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, SchemeKey, 0, KEY_CREATE_SUB_KEY, &writableScheme.value);
            if (error == ERROR_SUCCESS)
                error = RegCreateKeyExW(writableScheme.value, SettingKey, 0, nullptr, 0, KEY_SET_VALUE,
                    nullptr, &setting.value, nullptr);
        }
        if (error != ERROR_SUCCESS) return static_cast<DWORD>(error);
        return static_cast<DWORD>(RegSetValueExW(setting.value, ac ? L"ACSettingIndex" : L"DCSettingIndex",
            0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)));
    }
    DWORD activeIsYeman(bool& yeman) {
        GUID* active = nullptr;
        const DWORD error = PowerGetActiveScheme(nullptr, &active);
        yeman = error == ERROR_SUCCESS && active && IsEqualGUID(*active, Scheme);
        const bool found = active != nullptr;
        if (active) LocalFree(active);
        return error != ERROR_SUCCESS ? error : found ? ERROR_SUCCESS : ERROR_INVALID_DATA;
    }
    DWORD reactivateYeman() { return PowerSetActiveScheme(nullptr, &Scheme); }
};

template<class Api>
Snapshot read(Api& api) {
    Snapshot state;
    if ((state.error = api.read(true, state.ac)) != ERROR_SUCCESS) return state;
    if ((state.error = api.read(false, state.dc)) != ERROR_SUCCESS) return state;
    if (state.ac > 1 || state.dc > 1) { state.error = ERROR_INVALID_DATA; return state; }
    state.known = true;
    return state;
}

// Explicit user action only. Store the fixed plan's indices first. Reapply it
// once only when it is already active; another selected scheme is never replaced.
// Reactivation is a separate receipt, not a reason to undo confirmed saved values.
template<class Api>
ApplyResult apply(Api& api, bool enabled) {
    ApplyResult result;
    const Snapshot before = read(api);
    result.state = before;
    if (!before.known) { result.error = before.error; return result; }
    const DWORD desired = enabled ? 1 : 0;

    auto fail = [&](DWORD error, const char* stage) {
        result.error = error;
        result.stage = stage;
        result.state = read(api);
        // A rejected first write normally changes nothing. Do not issue two
        // more denied writes and misreport that as a failed rollback.
        if (result.state.known && result.state.ac == before.ac && result.state.dc == before.dc) return result;
        const DWORD restoreAC = api.write(true, before.ac);
        const DWORD restoreDC = api.write(false, before.dc);
        result.state = read(api);
        result.rollbackOk = restoreAC == ERROR_SUCCESS && restoreDC == ERROR_SUCCESS && result.state.known &&
            result.state.ac == before.ac && result.state.dc == before.dc;
        return result;
    };

    if (before.ac != desired || before.dc != desired) {
        DWORD error = api.write(true, desired);
        if (error != ERROR_SUCCESS) return fail(error, "write-ac");
        result.changed = true;
        error = api.write(false, desired);
        if (error != ERROR_SUCCESS) return fail(error, "write-dc");
        const auto stored = read(api);
        if (!stored.known || stored.ac != desired || stored.dc != desired)
            return fail(stored.error ? stored.error : ERROR_INVALID_DATA, "readback");
    }
    bool active = false;
    result.reactivationError = api.activeIsYeman(active);
    if (result.reactivationError == ERROR_SUCCESS && active) {
        result.reactivationError = api.reactivateYeman();
        result.reactivated = result.reactivationError == ERROR_SUCCESS;
    }
    const auto state = read(api);
    if (!state.known || state.ac != desired || state.dc != desired)
        return fail(state.error ? state.error : ERROR_INVALID_DATA, "final-readback");
    result.ok = true; // Confirms saved AC/DC, not effective Windows sign-in policy.
    result.stage = result.changed ? "complete" : "unchanged";
    result.state = state;
    return result;
}
} // namespace ymcc::sleep_wake_password
