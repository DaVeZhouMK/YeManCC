// The shipping worker never includes this file. Faults/processes are virtualized
// only here so the full production data routines can be tested in workspace fixtures.
#pragma once
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <functional>
#include <string>
static bool selftestSteamRunning = false;
static std::function<DWORD(const std::wstring&, const std::wstring&)> selftestMoveFault;
static HANDLE WINAPI selftestProcessSnapshot(DWORD, DWORD) { return CreateEventW(nullptr, FALSE, FALSE, nullptr); }
static BOOL WINAPI selftestFirstProcess(HANDLE, LPPROCESSENTRY32W entry) {
    if (!selftestSteamRunning) return FALSE;
    wcscpy_s(entry->szExeFile, L"steam.exe");
    return TRUE;
}
static BOOL WINAPI selftestMoveFile(LPCWSTR source, LPCWSTR destination, DWORD flags) {
    if (selftestMoveFault) {
        auto ordinary = [](std::wstring value) {
            if (value.starts_with(L"\\\\?\\UNC\\")) return std::wstring(L"\\\\") + value.substr(8);
            if (value.starts_with(L"\\\\?\\")) return value.substr(4);
            return value;
        };
        const DWORD error = selftestMoveFault(ordinary(source), ordinary(destination));
        if (error) { SetLastError(error); return FALSE; }
    }
    return MoveFileExW(source, destination, flags);
}
static void WINAPI selftestSleep(DWORD) { }
#define CreateToolhelp32Snapshot selftestProcessSnapshot
#define Process32FirstW selftestFirstProcess
#define MoveFileExW selftestMoveFile
#define Sleep selftestSleep
#include "custom_steam_library_storage_isolation.h"
#define CUSTOM_STEAM_LIBRARY_TEST_RUNTIME 1
#define wmain workerMainNotUsedBySelftest
#include "../native/custom-steam-library/steam_artwork_lab.cpp"
#undef wmain
#undef Sleep
#undef MoveFileExW
#undef Process32FirstW
#undef CreateToolhelp32Snapshot
