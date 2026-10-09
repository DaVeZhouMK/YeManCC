// Exercise the production presence helper using only in-memory Win32 boundaries.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
static std::vector<std::wstring> names;
static bool snapshotFails = false, firstFails = false, nextFails = false;
static size_t cursor = 0;
static DWORD error = ERROR_SUCCESS;
static int opens = 0, closes = 0, checks = 0;
static HANDLE fixtureSnapshot(DWORD, DWORD) {
    ++opens; cursor = 0;
    return snapshotFails ? INVALID_HANDLE_VALUE : reinterpret_cast<HANDLE>(1);
}
static BOOL fixtureFirst(HANDLE, PROCESSENTRY32W* pe) {
    if (firstFails) { error = ERROR_ACCESS_DENIED; return FALSE; }
    if (names.empty()) { error = ERROR_NO_MORE_FILES; return FALSE; }
    wcscpy_s(pe->szExeFile, names[0].c_str()); return TRUE;
}
static BOOL fixtureNext(HANDLE, PROCESSENTRY32W* pe) {
    if (nextFails) { error = ERROR_ACCESS_DENIED; return FALSE; }
    if (++cursor >= names.size()) { error = ERROR_NO_MORE_FILES; return FALSE; }
    wcscpy_s(pe->szExeFile, names[cursor].c_str()); return TRUE;
}
static BOOL fixtureClose(HANDLE) { ++closes; return TRUE; }
static DWORD fixtureError() { return error; }
#define CreateToolhelp32Snapshot fixtureSnapshot
#define Process32FirstW fixtureFirst
#define Process32NextW fixtureNext
#define CloseHandle fixtureClose
#define GetLastError fixtureError
#include "steam_session_observer.h"
#undef CreateToolhelp32Snapshot
#undef Process32FirstW
#undef Process32NextW
#undef CloseHandle
#undef GetLastError
static void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error(message); ++checks;
}
static void probe(std::vector<std::wstring> processes, int expected, const char* message,
    bool snapshotFailure=false, bool firstFailure=false, bool nextFailure=false) {
    names=std::move(processes); snapshotFails=snapshotFailure;
    firstFails=firstFailure; nextFails=nextFailure; error=ERROR_SUCCESS;
    const int opened=opens, closed=closes;
    expect(ymcc::steamsession::probeSteamPresence()==expected, message);
    expect(opens==opened+1 && closes==closed+(snapshotFailure ? 0 : 1), "snapshot handles closed exactly once");
}
int main() {
    try {
        probe({}, -1, "snapshot failure means unknown", true);
        probe({}, -1, "first enumeration failure means unknown", false, true);
        probe({L"explorer.exe"}, -1, "partial enumeration failure means unknown", false, false, true);
        probe({}, 0, "complete empty snapshot confirms absent");
        probe({L"explorer.exe",L"steamwebhelper.exe",L"steamservice.exe"}, 0, "helpers and service are not the Steam client");
        probe({L"steam.exe"}, 1, "Steam client present");
        probe({L"explorer.exe",L"StEaM.ExE"}, 1, "Steam process name is case insensitive");
        probe({L"steam.exe"}, 1, "positive sighting valid even if later enumeration would fail", false, false, true);
        probe({L"mysteam.exe",L"steam.exe.old",L"Steam"}, 0, "partial names do not count as running client");
        std::cout << "STEAM_PRESENCE_PASS checks=" << checks << " hardwareOperations=0 processOperations=0\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << "FAILED " << e.what() << '\n'; return 1; }
}
