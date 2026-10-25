#pragma once
// One-shot, read-only collection for a user-requested export. Never starts Steam,
// scans libraries/drives, changes a log gate, or follows directory reparse points.
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace ymcc::logexport {
namespace fs = std::filesystem;
struct SteamRootCandidate { fs::path root; std::string source; DWORD pid = 0; };
struct SteamLogIssue { fs::path path; std::string reason; DWORD error = 0; };
struct SteamLogs {
    fs::path root;
    std::string source;
    std::string status = "not-installed-or-unavailable";
    std::vector<fs::path> files;
    std::vector<SteamLogIssue> issues;
};
inline std::wstring winPath(const fs::path& path) {
    const auto text = path.lexically_normal().wstring();
    if (text.rfind(L"\\\\?\\", 0) == 0 || !path.is_absolute()) return text;
    if (text.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + text.substr(2);
    return L"\\\\?\\" + text;
}
inline bool reparsePoint(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(winPath(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
inline fs::path registryString(HKEY hive, const wchar_t* key, const wchar_t* name, DWORD view = 0) {
    std::array<wchar_t, 32768> value{};
    DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    if (RegGetValueW(hive, key, name, RRF_RT_REG_SZ | view, nullptr, value.data(), &bytes) != ERROR_SUCCESS) return {};
    std::wstring text(value.data());
    if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"') text = text.substr(1, text.size() - 2);
    std::replace(text.begin(), text.end(), L'/', L'\\');
    return fs::path(text);
}
inline fs::path processSteamRoot(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) return {};
    std::array<wchar_t, 32768> value{};
    DWORD length = static_cast<DWORD>(value.size());
    const bool ok = QueryFullProcessImageNameW(process, 0, value.data(), &length) &&
        WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    if (!ok) return {};
    const fs::path image(std::wstring(value.data(), length));
    return _wcsicmp(image.filename().c_str(), L"steam.exe") == 0 ? image.parent_path() : fs::path{};
}
inline std::vector<SteamRootCandidate> discoverSteamRoots(DWORD observedPid = 0) {
    std::vector<SteamRootCandidate> roots;
    DWORD activePid = 0, bytes = sizeof(activePid);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"pid", RRF_RT_REG_DWORD,
        nullptr, &activePid, &bytes);
    const auto addProcess = [&](DWORD pid, const char* source) {
        if (!pid) return;
        auto root = processSteamRoot(pid);
        if (!root.empty()) roots.push_back({std::move(root), source, pid});
    };
    addProcess(activePid, "running-active-process");
    addProcess(observedPid, "running-session-observer");
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
        for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
            if (_wcsicmp(entry.szExeFile, L"steam.exe") == 0 && entry.th32ProcessID != activePid && entry.th32ProcessID != observedPid)
                addProcess(entry.th32ProcessID, "running-process-image");
        }
        CloseHandle(snapshot);
    }
    // Running images precede installation evidence; Steam need not be open.
    roots.push_back({registryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"), "hkcu-steam-path"});
    const auto exe = registryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamExe");
    if (!exe.empty()) roots.push_back({exe.parent_path(), "hkcu-steam-exe"});
    for (DWORD view : {RRF_SUBKEY_WOW6432KEY, RRF_SUBKEY_WOW6464KEY})
        roots.push_back({registryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", view),
            view == RRF_SUBKEY_WOW6432KEY ? "hklm-install-32" : "hklm-install-64"});
    for (const auto* variable : {L"ProgramFiles(x86)", L"ProgramFiles"}) {
        std::array<wchar_t, 32768> value{};
        const DWORD length = GetEnvironmentVariableW(variable, value.data(), static_cast<DWORD>(value.size()));
        if (length && length < value.size()) roots.push_back({fs::path(value.data()) / L"Steam", "default-install-directory"});
    }
    return roots;
}
inline SteamLogs collectSteamLogs(const std::vector<SteamRootCandidate>& candidates) {
    SteamLogs result;
    for (const auto& candidate : candidates) {
        if (candidate.root.empty() || !candidate.root.is_absolute()) continue;
        std::error_code ec;
        if (!fs::is_regular_file(candidate.root / L"steam.exe", ec) || ec) continue;
        result.root = candidate.root.lexically_normal(); result.source = candidate.source;
        break; // Do not fall back to a stale installation just because its logs exist.
    }
    if (result.root.empty()) return result;
    const auto logs = result.root / L"logs";
    if (reparsePoint(logs)) {
        result.status = "logs-reparse-point-skipped";
        result.issues.push_back({logs, result.status}); return result;
    }
    std::error_code ec;
    const auto logStatus = fs::status(logs, ec);
    if (logStatus.type() == fs::file_type::not_found) {
        result.status = "logs-directory-missing"; return result;
    }
    if (ec || !fs::is_directory(logStatus)) {
        result.status = ec ? "logs-unavailable" : "logs-not-a-directory";
        if (ec) result.issues.push_back({logs, result.status, static_cast<DWORD>(ec.value())});
        return result;
    }
    result.status = "collected";
    std::vector<fs::path> pending{logs};
    while (!pending.empty()) {
        const auto dir = pending.back(); pending.pop_back();
        fs::directory_iterator it(dir, ec), end;
        if (ec) { result.issues.push_back({dir, "enumeration-failed", static_cast<DWORD>(ec.value())}); ec.clear(); continue; }
        while (it != end) {
            const auto path = it->path();
            const DWORD attributes = GetFileAttributesW(winPath(path).c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) result.issues.push_back({path, "attributes-unavailable", GetLastError()});
            else if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) result.issues.push_back({path, "reparse-point-skipped"});
            else if (attributes & FILE_ATTRIBUTE_DIRECTORY) pending.push_back(path);
            else if (fs::is_regular_file(path, ec)) result.files.push_back(path);
            if (ec) { result.issues.push_back({path, "file-status-failed", static_cast<DWORD>(ec.value())}); ec.clear(); }
            it.increment(ec);
            if (ec) { result.issues.push_back({dir, "enumeration-incomplete", static_cast<DWORD>(ec.value())}); ec.clear(); break; }
        }
    }
    std::sort(result.files.begin(), result.files.end());
    if (!result.issues.empty()) result.status = "collection-partial";
    else if (result.files.empty()) result.status = "logs-directory-empty";
    return result;
}
inline SteamLogs collectSteamLogs(DWORD observedPid = 0) { return collectSteamLogs(discoverSteamRoots(observedPid)); }

// Snapshot exactly the length observed at open, not an indefinitely growing log.
// FILE_SHARE_DELETE permits Steam rotation; failures never modify the source.
inline bool snapshotSharedLog(const fs::path& source, const fs::path& destination, DWORD& error, unsigned long long maxBytes = 0) {
    error = ERROR_SUCCESS;
    HANDLE input = CreateFileW(winPath(source).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (input == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(input, &size) || size.QuadPart < 0) {
        error = GetLastError(); if (!error) error = ERROR_INVALID_DATA; CloseHandle(input); return false;
    }
    if (maxBytes && static_cast<unsigned long long>(size.QuadPart) > maxBytes) { error = ERROR_FILE_TOO_LARGE; CloseHandle(input); return false; }
    HANDLE output = CreateFileW(winPath(destination).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) { error = GetLastError(); CloseHandle(input); return false; }
    std::array<char, 65536> buffer{};
    LONGLONG remaining = size.QuadPart;
    bool ok = true;
    while (remaining > 0) {
        DWORD read = 0;
        const DWORD requested = static_cast<DWORD>((std::min)(remaining, static_cast<LONGLONG>(buffer.size())));
        if (!ReadFile(input, buffer.data(), requested, &read, nullptr) || !read) {
            error = GetLastError(); if (!error) error = ERROR_HANDLE_EOF; ok = false; break;
        }
        DWORD written = 0;
        if (!WriteFile(output, buffer.data(), read, &written, nullptr) || written != read) {
            error = GetLastError(); if (!error) error = ERROR_WRITE_FAULT; ok = false; break;
        }
        remaining -= read;
    }
    if (!CloseHandle(output) && ok) { error = GetLastError(); ok = false; }
    CloseHandle(input);
    if (!ok) DeleteFileW(winPath(destination).c_str()); // only this CREATE_NEW fixture/staging file
    return ok;
}
} // namespace ymcc::logexport
