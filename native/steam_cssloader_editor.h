#pragma once
#include "steam_cssloader_guard.h"

// One process snapshot only at an explicit use/cleanup boundary. No watcher or
// polling. A live same-user/session Desktop means the backend belongs to an edit.
namespace ymcc::steamcsseditor {
enum class State { Absent, Active, Unknown };
using Probe = std::function<State()>;
inline bool editorName(const wchar_t* name) {
    return !_wcsicmp(name, L"CSSLoader Desktop.exe") || !_wcsicmp(name, L"CSSLoader-Desktop.exe");
}
inline State probe() {
    deckylaunch::Handle snapshot;
    snapshot.value = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot.value == INVALID_HANDLE_VALUE) { snapshot.value = nullptr; return State::Unknown; }
    PROCESSENTRY32W entry{sizeof(entry)};
    if (!Process32FirstW(snapshot.value, &entry)) return State::Unknown;
    DWORD ourSession = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &ourSession)) return State::Unknown;
    do {
        if (!editorName(entry.szExeFile)) continue;
        DWORD targetSession = 0;
        if (!ProcessIdToSessionId(entry.th32ProcessID, &targetSession)) return State::Unknown;
        if (targetSession != ourSession) continue;
        deckylaunch::Handle process, callerToken, targetToken;
        process.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process.value) { if (GetLastError() == ERROR_INVALID_PARAMETER) continue; return State::Unknown; }
        if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) continue;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &callerToken.value) ||
            !OpenProcessToken(process.value, TOKEN_QUERY, &targetToken.value)) return State::Unknown;
        const auto caller = deckylaunch::tokenData(callerToken.value, TokenUser);
        const auto target = deckylaunch::tokenData(targetToken.value, TokenUser);
        if (caller.empty() || target.empty()) return State::Unknown;
        if (!EqualSid(reinterpret_cast<const TOKEN_USER*>(caller.data())->User.Sid,
                      reinterpret_cast<const TOKEN_USER*>(target.data())->User.Sid)) continue;
        wchar_t image[32768]{}; DWORD size = static_cast<DWORD>(std::size(image));
        if (!QueryFullProcessImageNameW(process.value, 0, image, &size)) return State::Unknown;
        if (editorName(std::filesystem::path(image).filename().c_str()) &&
            WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT) return State::Active;
    } while (Process32NextW(snapshot.value, &entry));
    return GetLastError() == ERROR_NO_MORE_FILES ? State::Absent : State::Unknown;
}
}
