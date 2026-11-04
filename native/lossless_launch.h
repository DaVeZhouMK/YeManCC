#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "process_inventory.h"
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
        // Launch admission needs a fresh complete table; the same native owner
        // performs this explicit read, retaining all live instance checks below.
        const auto snapshot = ymcc::processInventory::read(ymcc::processInventory::resolveQuery(),
            ymcc::processInventory::currentTime, 0);
        if (snapshot->status != ymcc::processInventory::Status::Complete)
            return {ScanState::Unknown, {}, snapshot->error};
        bool found = false; Identity exact;
        for (const auto& entry : snapshot->rows) {
            if (_wcsicmp(entry.name.c_str(), L"LosslessScaling.exe") != 0) continue;
            DWORD session = 0;
            if (!ProcessIdToSessionId(entry.pid, &session)) return {ScanState::Unknown, {}, GetLastError()};
            if (session != session_) continue;
            Identity identity;
            if (!readIdentity(entry.pid, identity)) return {ScanState::Unknown, {}, error_};
            if (found || _wcsicmp(identity.path.c_str(), executable_.c_str()) != 0)
                return {ScanState::Conflict, {}, 0};
            found = true; exact = std::move(identity);
        }
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
        const auto fail = [this](DWORD error) { error_ = error ? error : ERROR_GEN_FAILURE; return false; };
        ymcc::processInventory::detail::SnapshotHandle process(
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
        if (!process.get()) return fail(GetLastError());
        const auto live = [&] {
            const DWORD wait = WaitForSingleObject(process.get(), 0);
            return wait == WAIT_TIMEOUT || fail(wait == WAIT_FAILED ? GetLastError() : ERROR_PROCESS_ABORTED);
        };
        if (!live()) return false;
        const DWORD heldPid = GetProcessId(process.get());
        if (!heldPid) return fail(GetLastError());
        if (heldPid != pid) return fail(ERROR_INVALID_DATA);
        DWORD heldSession = 0;
        if (!ProcessIdToSessionId(heldPid, &heldSession)) return fail(GetLastError());
        if (heldSession != session_) return fail(ERROR_INVALID_DATA);
        std::vector<wchar_t> image(32768); DWORD size = static_cast<DWORD>(image.size());
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!QueryFullProcessImageNameW(process.get(), 0, image.data(), &size) ||
            !GetProcessTimes(process.get(), &created, &exited, &kernel, &user)) return fail(GetLastError());
        if (!live()) return false;
        const auto birth = ticks(created);
        if (!birth) return fail(ERROR_INVALID_DATA);
        identity = {heldPid, birth, std::wstring(image.data(), size)};
        return true;
    }
    std::wstring executable_;
    Ready ready_;
    Creator creator_;
    HANDLE mutex_ = nullptr;
    DWORD session_ = 0, error_ = 0;
};
} // namespace ymcc::lossless
