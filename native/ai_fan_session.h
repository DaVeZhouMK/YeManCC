#pragma once
#include <windows.h>
#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

namespace ymcc::ai_fan {
// Activation belongs to an exact command-line capability, never a flag file,
// saved settings, frontend request or user's Fan switch.
inline bool validSessionId(const std::wstring& id) {
    if (id.size() != 36) return false;
    for (size_t i = 0; i < id.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (id[i] != L'-') return false; }
        else if (!((id[i] >= L'0' && id[i] <= L'9') || (id[i] >= L'a' && id[i] <= L'f'))) return false;
    }
    return true;
}
struct Session {
    bool enabled = false;
    bool isolated = false;
    std::wstring id;
    DWORD parentPid = 0;
};
inline bool parseSession(int argc, const wchar_t* const* argv, DWORD parentPid, Session& out) {
    Session next;
    next.parentPid = parentPid;
    const std::wstring flag = L"--ai-fan-mock-session";
    const std::wstring isolationFlag = L"--ai-cpu-isolated-session";
    std::wstring isolatedId;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i] ? argv[i] : L"";
        if (arg.compare(0, isolationFlag.size(), isolationFlag) == 0) {
            if (arg != isolationFlag || next.isolated || i + 1 >= argc) return false;
            isolatedId = argv[++i] ? argv[i] : L"";
            if (!validSessionId(isolatedId)) return false;
            next.isolated = true;
            continue;
        }
        if (arg.compare(0, flag.size(), flag) != 0) continue;
        if (arg != flag || next.enabled || i + 1 >= argc) return false;
        next.id = argv[++i] ? argv[i] : L"";
        if (!validSessionId(next.id) || parentPid == 0) return false;
        next.enabled = true;
    }
    if (next.isolated && (!next.enabled || isolatedId != next.id)) return false;
    out = next;
    return true;
}
inline bool validMockHostArguments(const Session& session, const std::vector<std::wstring>& args,
                                   const std::wstring& tokenPath) {
    if (!session.enabled || !validSessionId(session.id) || session.parentPid == 0) return false;
    bool mock = false, zeroEvidence = false, port = false, protocol = false, parent = false, token = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == L"--mock-handshake") { if (mock) return false; mock = true; continue; }
        if (arg == L"--mock-zero-hardware-evidence") { if (zeroEvidence) return false; zeroEvidence = true; continue; }
        if (i + 1 >= args.size()) return false;
        const auto& value = args[++i];
        if (arg == L"--port") { if (port || value != L"8765") return false; port = true; }
        else if (arg == L"--protocol-version") { if (protocol || value != L"2") return false; protocol = true; }
        else if (arg == L"--parent-pid") { if (parent || value != std::to_wstring(session.parentPid)) return false; parent = true; }
        else if (arg == L"--session-token-file") { if (token || _wcsicmp(value.c_str(), tokenPath.c_str()) != 0) return false; token = true; }
        else return false; // No real backend, authorization, confirmation or arbitrary mock injections.
    }
    return mock && zeroEvidence && port && protocol && parent && token;
}
// Keeping the actual process handle pins identity across PID reuse. No polling
// thread, process creation or privilege change belongs to this evidence holder.
class OwnedProcess {
    mutable std::mutex mutex_;
    HANDLE handle_ = nullptr;
    DWORD pid_ = 0;
public:
    ~OwnedProcess() { if (handle_) CloseHandle(handle_); }
    void bind(HANDLE handle, DWORD pid) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (handle_) CloseHandle(handle_);
        handle_ = handle;
        pid_ = pid;
    }
    DWORD livePid() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return handle_ && WaitForSingleObject(handle_, 0) == WAIT_TIMEOUT ? pid_ : 0;
    }
    bool owns(DWORD pid) const { return pid != 0 && livePid() == pid; }
    unsigned long long creationTime() const {
        std::lock_guard<std::mutex> lock(mutex_);
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!handle_ || !GetProcessTimes(handle_, &creation, &exit, &kernel, &user)) return 0;
        return (static_cast<unsigned long long>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
    }
};
} // namespace ymcc::ai_fan
