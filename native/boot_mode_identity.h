#pragma once
#include <windows.h>
#include <objbase.h>
#include <string>

namespace ymcc::bootModeIdentity {
// A loader BootIdentifier/BootId tuple can repeat across an actual reboot (G3E
// field log 2026-10-24). Use kernel-owned volatile-hive lifetime, not PID, wall
// clock, uptime subtraction, logon or app lifetime, for this startup authority.
struct Observation { std::string identity; DWORD error = ERROR_SUCCESS; bool created = false; };
inline Observation observe(HKEY hive, const wchar_t* path, const wchar_t* mutexName) {
    Observation result;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName);
    if (!mutex) { result.error = GetLastError(); return result; }
    const auto wait = WaitForSingleObject(mutex, 5000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        result.error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        CloseHandle(mutex); return result;
    }
    HKEY key = nullptr; DWORD disposition = 0;
    auto status = RegCreateKeyExW(hive, path, 0, nullptr, REG_OPTION_VOLATILE,
        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, &disposition);
    if (status == ERROR_SUCCESS) {
        wchar_t value[40]{}; DWORD type = 0, bytes = sizeof(value);
        status = RegQueryValueExW(key, L"Session", nullptr, &type,
            reinterpret_cast<BYTE*>(value), &bytes);
        if (status == ERROR_FILE_NOT_FOUND) {
            GUID id{};
            if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, value, 40)) status = ERROR_INVALID_DATA;
            else {
                status = RegSetValueExW(key, L"Session", 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(value), 39 * sizeof(wchar_t));
                if (status == ERROR_SUCCESS) { type = REG_SZ; bytes = 39 * sizeof(wchar_t); result.created = true; }
            }
        }
        GUID checked{};
        if (status == ERROR_SUCCESS && (type != REG_SZ || bytes != 39 * sizeof(wchar_t) ||
            value[38] != L'\0' || FAILED(CLSIDFromString(value, &checked)) || IsEqualGUID(checked, GUID{})))
            status = ERROR_INVALID_DATA;
        if (status == ERROR_SUCCESS) {
            result.identity = "windows-volatile-v2|";
            for (unsigned n = 0; n < 38; ++n) result.identity.push_back(static_cast<char>(value[n]));
        }
        RegCloseKey(key);
    }
    result.error = static_cast<DWORD>(status);
    ReleaseMutex(mutex); CloseHandle(mutex);
    return result;
}
inline Observation current() {
    // YeManCC's native manifest requires administrator. This exact machine key
    // survives renderer/app restart and wake; Windows unloads it at full reboot.
    // An unreadable/malformed marker fails closed instead of guessing a boundary.
    return observe(HKEY_LOCAL_MACHINE, L"SOFTWARE\\YeManCC\\Runtime\\BootSessionV2",
        L"Global\\YeManCC-BootSessionV2");
}
}
