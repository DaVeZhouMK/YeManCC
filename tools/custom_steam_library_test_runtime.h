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
// Public types are included before the worker-only namespace substitution.
#ifdef YMCC_LIBRARY_FACTS_IPC
#define OpenProcessToken nativeObserveOpenProcessToken
#define OpenFileMappingW nativeObserveOpenMapping
#define CreateFileMappingW nativeObserveCreateMapping
#define MapViewOfFile nativeObserveMap
#define UnmapViewOfFile nativeObserveUnmap
#define CreateWindowExW nativeObserveCreateWindow
#define DestroyWindow nativeObserveDestroyWindow
#endif
#include "../native/steam_information_client.h"
#ifdef YMCC_LIBRARY_FACTS_IPC
#undef OpenProcessToken
#undef OpenFileMappingW
#undef CreateFileMappingW
#undef MapViewOfFile
#undef UnmapViewOfFile
#undef CreateWindowExW
#undef DestroyWindow
#endif
static bool selftestSteamRunning = false;
static std::function<DWORD(const std::wstring&, const std::wstring&)> selftestMoveFault;
// Local controls never discover an installed Steam or grant process/action proof.
static bool selftestSteamFactsAvailable = true;
static int selftestSteamPresenceOverride = -2; // -2: old bool; -1: unknown; 0/1: absent/present.
static std::wstring selftestSteamInstallationRoot;
static std::uint64_t selftestSteamFactsReads = 0, selftestSteamPresenceReads = 0;
static std::function<void(std::uint32_t)> selftestSteamFactsBeforeRead;
namespace ymcc::steaminfo_selftest {
using ::ymcc::steaminfo::Facts;
using ::ymcc::steaminfo::Mask;
using ::ymcc::steaminfo::Installation;
using ::ymcc::steaminfo::Installations;
using ::ymcc::steaminfo::Session;
using ::ymcc::steaminfo::Presence;
using ::ymcc::steaminfo::All;
static Facts read(std::uint32_t mask) noexcept {
    ++selftestSteamFactsReads;
    if (mask & Presence) ++selftestSteamPresenceReads;
    try {
        if (selftestSteamFactsBeforeRead) selftestSteamFactsBeforeRead(mask);
        Facts facts;
        facts.requested = mask;
        if (!selftestSteamFactsAvailable) return facts;
        facts.available = true;
        if (mask & Presence)
            facts.presence = selftestSteamPresenceOverride == -2
                ? (selftestSteamRunning ? 1 : 0) : selftestSteamPresenceOverride;
        if ((mask & Installations) && !selftestSteamInstallationRoot.empty())
            facts.installations.push_back({::ymcc::steaminfo::utf8(selftestSteamInstallationRoot), "selftest-owned-root", 0});
        return facts;
    } catch (...) {
        return {}; // Hook/conversion/allocation failure leaves every fact unavailable.
    }
}
} // namespace ymcc::steaminfo_selftest
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
#ifndef YMCC_LIBRARY_FACTS_IPC
#define steaminfo steaminfo_selftest
#endif
#include "../native/custom-steam-library/steam_artwork_lab.cpp"
#ifndef YMCC_LIBRARY_FACTS_IPC
#undef steaminfo
#endif
#undef wmain
#undef Sleep
#undef MoveFileExW
#undef Process32FirstW
#undef CreateToolhelp32Snapshot
