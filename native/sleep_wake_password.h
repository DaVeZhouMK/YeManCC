#pragma once
// The wake-password setting is owned by one fixed YMCC scheme, never SCHEME_CURRENT.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <powrprof.h>
#include <mutex>
#pragma comment(lib, "powrprof.lib")

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
    bool rollbackOk = true;
    Snapshot state;
};

struct WindowsPowerApi {
    DWORD read(bool ac, DWORD& value) {
        return ac ? PowerReadACValueIndex(nullptr, &Scheme, &Subgroup, &Setting, &value)
                  : PowerReadDCValueIndex(nullptr, &Scheme, &Subgroup, &Setting, &value);
    }
    DWORD write(bool ac, DWORD value) {
        return ac ? PowerWriteACValueIndex(nullptr, &Scheme, &Subgroup, &Setting, value)
                  : PowerWriteDCValueIndex(nullptr, &Scheme, &Subgroup, &Setting, value);
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

// Used under the domain mutex + main.cpp's HardwareWriteLease. Writes and reads
// only the hard-coded scheme. An inactive YMCC plan is edited without activation.
// Every failed partial write is restored/read back, so the UI never assumes ACK.
template<class Api>
ApplyResult apply(Api& api, bool enabled) {
    ApplyResult result;
    const Snapshot before = read(api);
    result.state = before;
    if (!before.known) { result.error = before.error; return result; }
    const DWORD desired = enabled ? 1 : 0;
    if (before.ac == desired && before.dc == desired) { result.ok = true; result.stage = "unchanged"; return result; }

    auto fail = [&](DWORD error, const char* stage) {
        result.error = error;
        result.stage = stage;
        const DWORD restoreAC = api.write(true, before.ac);
        const DWORD restoreDC = api.write(false, before.dc);
        bool active = false;
        const DWORD activeError = api.activeIsYeman(active);
        DWORD reactivate = ERROR_SUCCESS;
        if (activeError == ERROR_SUCCESS && active) reactivate = api.reactivateYeman();
        result.state = read(api);
        result.rollbackOk = restoreAC == ERROR_SUCCESS && restoreDC == ERROR_SUCCESS &&
            activeError == ERROR_SUCCESS && reactivate == ERROR_SUCCESS && result.state.known &&
            result.state.ac == before.ac && result.state.dc == before.dc;
        return result;
    };

    DWORD error = api.write(true, desired);
    if (error != ERROR_SUCCESS) return fail(error, "write-ac");
    result.changed = true;
    error = api.write(false, desired);
    if (error != ERROR_SUCCESS) return fail(error, "write-dc");
    auto state = read(api);
    if (!state.known || state.ac != desired || state.dc != desired)
        return fail(state.error ? state.error : ERROR_INVALID_DATA, "readback");

    // Never activate YMCC while another plan (especially Balanced) is active.
    // Check immediately before applying the stored values to the current plan.
    bool active = false;
    error = api.activeIsYeman(active);
    if (error != ERROR_SUCCESS) return fail(error, "active-scheme");
    if (active) {
        error = api.reactivateYeman();
        if (error != ERROR_SUCCESS) return fail(error, "apply");
        result.reactivated = true;
    }
    state = read(api);
    if (!state.known || state.ac != desired || state.dc != desired)
        return fail(state.error ? state.error : ERROR_INVALID_DATA, "final-readback");
    result.ok = true;
    result.stage = "complete";
    result.state = state;
    return result;
}
} // namespace ymcc::sleep_wake_password
