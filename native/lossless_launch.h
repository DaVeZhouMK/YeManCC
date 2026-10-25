#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <functional>
#include <vector>
#include <cwctype>
#include "lossless_launch_contract.h"
namespace ymcc::lossless {
class NativePorts {
public:
    using Ready = std::function<bool()>;
    using Creator = std::function<BOOL(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
        BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION)>;
    NativePorts(std::wstring executable, Ready ready, Creator creator = ::CreateProcessW)
        : executable_(std::move(executable)), ready_(std::move(ready)), creator_(std::move(creator)) {}
    ~NativePorts() { if (mutex_) CloseHandle(mutex_); }
    bool ready() const { return ready_(); }
    DWORD error() const { return error_; }
    bool validate() {
        if (executable_.size() < 4 || executable_.find(L'\0') != std::wstring::npos ||
            executable_.find(L'"') != std::wstring::npos || !iswalpha(executable_[0]) ||
            executable_[1] != L':' || (executable_[2] != L'\\' && executable_[2] != L'/')) return false;
        for (auto& c : executable_) if (c == L'/') c = L'\\';
        const auto slash = executable_.find_last_of(L'\\');
        if (slash == std::wstring::npos || _wcsicmp(executable_.c_str() + slash + 1, L"LosslessScaling.exe") != 0) return false;
        std::vector<wchar_t> absolute(32768);
        const DWORD count = GetFullPathNameW(executable_.c_str(), static_cast<DWORD>(absolute.size()), absolute.data(), nullptr);
        if (!count || count >= absolute.size()) { error_ = GetLastError(); return false; }
        executable_.assign(absolute.data(), count);
        const DWORD attributes = GetFileAttributesW(executable_.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) { error_ = GetLastError(); return false; }
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_)) { error_ = GetLastError(); return false; }
        return true;
    }
    bool lock() {
        const auto name = L"Local\\YMCC.LosslessLaunch." + std::to_wstring(session_);
        mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!mutex_) { error_ = GetLastError(); return false; }
        const DWORD wait = WaitForSingleObject(mutex_, 0);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) return true;
        error_ = wait == WAIT_TIMEOUT ? ERROR_BUSY : GetLastError(); return false;
    }
    void unlock() { if (mutex_) ReleaseMutex(mutex_); }
    Scan scan() {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return {ScanState::Unknown, {}, GetLastError()};
        PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
        bool found = false; Identity exact;
        if (!Process32FirstW(snapshot, &entry)) {
            const DWORD failure = GetLastError(); CloseHandle(snapshot);
            return {ScanState::Unknown, {}, failure};
        }
        do {
            if (_wcsicmp(entry.szExeFile, L"LosslessScaling.exe") != 0) continue;
            DWORD session = 0;
            if (!ProcessIdToSessionId(entry.th32ProcessID, &session)) {
                const DWORD failure = GetLastError(); CloseHandle(snapshot);
                return {ScanState::Unknown, {}, failure};
            }
            if (session != session_) continue;
            Identity identity;
            if (!readIdentity(entry.th32ProcessID, identity)) {
                const DWORD failure = error_; CloseHandle(snapshot);
                return {ScanState::Unknown, {}, failure};
            }
            if (found || _wcsicmp(identity.path.c_str(), executable_.c_str()) != 0) {
                CloseHandle(snapshot); return {ScanState::Conflict, {}, 0};
            }
            found = true; exact = std::move(identity);
        } while (Process32NextW(snapshot, &entry));
        const DWORD finished = GetLastError(); CloseHandle(snapshot);
        if (finished != ERROR_NO_MORE_FILES) return {ScanState::Unknown, {}, finished};
        return {found ? ScanState::Exact : ScanState::Absent, std::move(exact), 0};
    }
    bool matches(const Identity& before) {
        Identity now;
        return readIdentity(before.pid, now) && now.created == before.created &&
            _wcsicmp(now.path.c_str(), before.path.c_str()) == 0 &&
            _wcsicmp(now.path.c_str(), executable_.c_str()) == 0;
    }
    Result start() {
        Result result;
        // No game path, toggling switch, shell, cmd, UAC elevation or window
        // message is sent to an existing LS instance. AutoScale stays the owner.
        std::wstring command = L"\"" + executable_ + L"\" -StartMinimized";
        std::vector<wchar_t> writable(command.begin(), command.end()); writable.push_back(0);
        const auto directory = executable_.substr(0, executable_.find_last_of(L'\\'));
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_SHOWMINNOACTIVE;
        PROCESS_INFORMATION process{};
        if (!creator_(executable_.c_str(), writable.data(), nullptr, nullptr, FALSE,
                NORMAL_PRIORITY_CLASS, nullptr, directory.c_str(), &startup, &process)) {
            result.reason = "create-process-failed"; result.error = GetLastError(); return result;
        }
        result.started = true;
        result.identity.pid = process.dwProcessId;
        result.identity.path = executable_;
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessTimes(process.hProcess, &created, &exited, &kernel, &user)) {
            result.identity.created = ticks(created);
            result.ok = result.identity.created != 0;
        } else { result.error = GetLastError(); }
        if (!result.ok) result.reason = "started-identity-unconfirmed";
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        return result;
    }
private:
    static std::uint64_t ticks(const FILETIME& value) { return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime; }
    bool readIdentity(DWORD pid, Identity& identity) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
        if (!process) { error_ = GetLastError(); return false; }
        std::vector<wchar_t> image(32768); DWORD size = static_cast<DWORD>(image.size());
        FILETIME created{}, exited{}, kernel{}, user{};
        const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
        const bool queried = alive && QueryFullProcessImageNameW(process, 0, image.data(), &size) &&
            GetProcessTimes(process, &created, &exited, &kernel, &user) &&
            WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
        if (!queried) error_ = GetLastError();
        CloseHandle(process);
        if (!queried) return false;
        identity = {pid, ticks(created), std::wstring(image.data(), size)};
        return identity.created != 0;
    }
    std::wstring executable_;
    Ready ready_;
    Creator creator_;
    HANDLE mutex_ = nullptr;
    DWORD session_ = 0, error_ = 0;
};
} // namespace ymcc::lossless
