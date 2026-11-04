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
    const auto topology = processInventory::read();
    if (!topology || topology->status != processInventory::Status::Complete) return State::Unknown;
    DWORD ourSession = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &ourSession)) return State::Unknown;
    for (const auto& entry : topology->rows) {
        if (!editorName(entry.name.c_str())) continue;
        DWORD targetSession = 0;
        if (!ProcessIdToSessionId(entry.pid, &targetSession)) return State::Unknown;
        if (targetSession != ourSession) continue;
        deckylaunch::Handle process, callerToken, targetToken;
        process.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.pid);
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
    }
    return State::Absent;
}
}
