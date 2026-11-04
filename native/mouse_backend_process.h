#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <algorithm>
#include <new>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <string>
#include <vector>
#include <utility>
#include "process_basic_candidates.h"

// Direct executable control only. A live process is not evidence that its
// controller bindings are enabled; callers expose that verification level.
namespace ymcc::mouseProcess {
struct Result {
    bool ok = false;
    bool running = false;
    bool known = false;
    DWORD error = ERROR_SUCCESS;
    const char* reason = "unknown";
};
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
            value = std::exchange(other.value, nullptr);
        }
        return *this;
    }
};
struct Snapshot {
    bool known = true;
    DWORD error = ERROR_SUCCESS;
    std::vector<Handle> processes;
};
inline bool samePath(HANDLE process, const std::wstring& executable) {
    wchar_t path[32768]{};
    DWORD length = static_cast<DWORD>(std::size(path));
    return QueryFullProcessImageNameW(process, 0, path, &length) &&
        CompareStringOrdinal(path, static_cast<int>(length), executable.c_str(),
            static_cast<int>(executable.size()), TRUE) == CSTR_EQUAL;
}
// Compatibility forwarding: callers retain the original mouse namespace/signatures.
using BasicQuery = ymcc::processNames::BasicQuery;
using BasicResult = ymcc::processNames::BasicResult;
using SessionQuery = ymcc::processNames::SessionQuery;
inline BasicQuery resolveBasicQuery() noexcept { return ymcc::processNames::resolveBasicQuery(); }
inline bool parseBasicCandidates(const void* data, std::size_t bytes,
    std::wstring_view name, std::vector<DWORD>& candidates) {
    return ymcc::processNames::parseBasicCandidates(data, bytes, name, candidates);
}
inline BasicResult basicCandidates(std::wstring_view name, std::vector<DWORD>& candidates,
    DWORD& error, BasicQuery query = resolveBasicQuery()) {
    return ymcc::processNames::basicCandidates(name, candidates, error, query);
}
inline void inspectCandidate(Snapshot& result, DWORD pid, DWORD session,
    const std::wstring& executable, SessionQuery querySession = &::ProcessIdToSessionId) {
    DWORD candidateSession = 0;
    if (!querySession(pid, &candidateSession)) {
        const DWORD error = GetLastError();
        if (error != ERROR_INVALID_PARAMETER) { result.known = false; result.error = error; }
        return;
    }
    if (candidateSession != session) return;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    if (!process.value) {
        const DWORD error = GetLastError();
        if (error != ERROR_INVALID_PARAMETER) { result.known = false; result.error = error; }
    } else {
        const DWORD wait = WaitForSingleObject(process.value, 0);
        if (wait == WAIT_TIMEOUT) {
            // The name/session hint predates OpenProcess and its PID can be
            // recycled. Recheck the opened instance while its handle is held.
            const DWORD heldPid = GetProcessId(process.value);
            DWORD heldSession = 0;
            if (!heldPid || !querySession(heldPid, &heldSession)) {
                result.known = false; result.error = GetLastError(); return;
            }
            if (heldSession != session) return;
            SetLastError(ERROR_SUCCESS);
            if (samePath(process.value, executable)) result.processes.push_back(std::move(process));
            else if (GetLastError() != ERROR_SUCCESS) {
                result.known = false; result.error = GetLastError();
            }
        } else if (wait == WAIT_FAILED) { result.known = false; result.error = GetLastError(); }
    }
}
inline Snapshot inspect(const std::wstring& executable, BasicQuery query = resolveBasicQuery()) {
    Snapshot result;
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
        result.known = false; result.error = GetLastError(); return result;
    }
    const auto name = executable.substr(executable.find_last_of(L"\\/") + 1);
    std::vector<DWORD> candidates;
    // This read becomes action/readback authority; cached absence cannot prove fresh OFF.
    const auto status = ymcc::processNames::candidates(name, candidates, result.error, query,
        ymcc::processNames::NameMatch::OrdinalIgnoreCase, 0);
    if (status != BasicResult::Complete) { result.known = false; return result; }
    for (const auto pid : candidates) inspectCandidate(result, pid, session, executable);
    return result;
}
inline Result observe(const std::wstring& executable) {
    const auto snapshot = inspect(executable);
    return {snapshot.known, !snapshot.processes.empty(), snapshot.known,
        snapshot.error, snapshot.known ? "process-observed" : "process-query-failed"};
}
struct CloseWindowRequest { DWORD pid; };
inline BOOL CALLBACK closeWindow(HWND window, LPARAM parameter) {
    const auto* request = reinterpret_cast<const CloseWindowRequest*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == request->pid) PostMessageW(window, WM_CLOSE, 0, 0);
    return TRUE;
}
using ProcessCreator = decltype(&::CreateProcessW);
inline Result set(const std::wstring& executable, bool enabled,
                  ProcessCreator create = &::CreateProcessW) {
    ymcc::processInventory::invalidate();
    auto before = inspect(executable);
    if (!before.known) return {false, !before.processes.empty(), false, before.error, "process-query-failed"};
    if (enabled && !before.processes.empty()) return {true, true, true, 0, "already-running"};
    if (!enabled && before.processes.empty()) return {true, false, true, 0, "already-stopped"};
    if (enabled) {
        const std::wstring command = L"\"" + executable + L"\"";
        std::vector<wchar_t> buffer(command.begin(), command.end()); buffer.push_back(0);
        const auto directory = executable.substr(0, executable.find_last_of(L"\\/"));
        STARTUPINFOW startup{sizeof(startup)};
        // A tray GUI owns its initial visibility. STARTF_USESHOWWINDOW/SW_HIDE
        // also overrides its first ShowWindow(settings), hiding a user-opened
        // settings dialog. CREATE_NO_WINDOW still avoids a console carrier.
        PROCESS_INFORMATION info{};
        if (!create(executable.c_str(), buffer.data(), nullptr, nullptr, FALSE,
                    CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &info))
            return {false, false, true, GetLastError(), "create-process-failed"};
        ymcc::processInventory::invalidate();
        Handle process(info.hProcess), thread(info.hThread);
        const DWORD wait = WaitForSingleObject(process.value, 300);
        if (wait == WAIT_FAILED) return {false, false, false, GetLastError(), "startup-wait-failed"};
        // A single-instance launcher may exit after handing off. Recheck the
        // exact path in the current session instead of trusting CreateProcess.
        auto actual = observe(executable);
        if (!actual.known || !actual.running) {
            actual.ok = false; actual.reason = "backend-exited-during-start"; return actual;
        }
        actual.reason = "process-started"; return actual;
    }
    for (auto& process : before.processes) {
        if (WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) continue;
        const CloseWindowRequest request{GetProcessId(process.value)};
        EnumWindows(closeWindow, reinterpret_cast<LPARAM>(&request));
    }
    const ULONGLONG deadline = GetTickCount64() + 1000;
    for (auto& process : before.processes) {
        const auto now = GetTickCount64();
        const DWORD remaining = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
        if (WaitForSingleObject(process.value, remaining) == WAIT_OBJECT_0) continue;
        // Request termination on the same process object, then recheck its
        // image and session. Never taskkill an arbitrary matching basename.
        Handle terminate(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
            FALSE, GetProcessId(process.value)));
        FILETIME created{}, exited{}, kernel{}, user{}, originalCreated{};
        const bool sameInstance = terminate.value &&
            GetProcessTimes(process.value, &originalCreated, &exited, &kernel, &user) &&
            GetProcessTimes(terminate.value, &created, &exited, &kernel, &user) &&
            CompareFileTime(&created, &originalCreated) == 0 && samePath(terminate.value, executable);
        if (!sameInstance || !TerminateProcess(terminate.value, 0)) {
            const DWORD error = GetLastError();
            auto actual = observe(executable); actual.ok = false;
            actual.error = error; actual.reason = "stop-process-failed"; return actual;
        }
        if (WaitForSingleObject(terminate.value, 500) != WAIT_OBJECT_0) {
            auto actual = observe(executable); actual.ok = false;
            actual.reason = "stop-process-timeout"; return actual;
        }
    }
    ymcc::processInventory::invalidate();
    auto actual = observe(executable);
    actual.ok = actual.known && !actual.running;
    actual.reason = actual.ok ? "process-stopped" : "stop-not-confirmed";
    return actual;
}
} // namespace ymcc::mouseProcess
