#pragma once
// Test-only isolation: never acquire an installed host/worker's data mutex.
// This header is not included by any shipping translation unit.
#include <winsock2.h>
#include <windows.h>
#include <string>
static HANDLE WINAPI storageSelftestCreateMutex(LPSECURITY_ATTRIBUTES attributes, BOOL owner, LPCWSTR name) {
    if (name && (wcscmp(name, L"Local\\YeManCustomSteamLibraryDataTransaction") == 0 ||
                 wcscmp(name, L"Local\\YeManSteamArtworkLabShortcutWriter") == 0)) {
        const auto isolated = std::wstring(L"Local\\YMCC-Storage-Selftest-") +
            std::to_wstring(GetCurrentProcessId()) +
            (wcsstr(name, L"ShortcutWriter") ? L"-Shortcut" : L"-Data");
        return CreateMutexW(attributes, owner, isolated.c_str());
    }
    return CreateMutexW(attributes, owner, name);
}
#define CreateMutexW storageSelftestCreateMutex
