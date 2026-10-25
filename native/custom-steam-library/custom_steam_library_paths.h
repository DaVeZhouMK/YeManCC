#pragma once

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <cwctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace custom_steam_library {

namespace fs = std::filesystem;

// Use the extended Win32 spelling only at IO boundaries. Persisted paths,
// executable identities and UI URLs must retain their ordinary portable form.
// Staging suffixes can push an otherwise valid destination beyond MAX_PATH.
inline fs::path ioPath(const fs::path& path) {
    auto value = path.wstring();
    if (value.empty() || value.size() < 248 || value.starts_with(L"\\\\?\\") ||
        value.starts_with(L"\\\\.\\")) return path;
    std::error_code error;
    auto absolute = fs::absolute(path, error).lexically_normal();
    if (error) return path;
    value = absolute.wstring();
    std::replace(value.begin(), value.end(), L'/', L'\\');
    if (value.starts_with(L"\\\\")) return fs::path(L"\\\\?\\UNC\\" + value.substr(2));
    return fs::path(L"\\\\?\\" + value);
}

inline std::wstring lower(std::wstring value) {
    for (auto& character : value) character = static_cast<wchar_t>(std::towlower(character));
    return value;
}

// The development build keeps the two EXEs under build\. A released green
// package keeps them directly beside the package root. Both layouts resolve
// to the same appRoot so moving the package does not change its data rules.
inline fs::path applicationRootFromExecutable(const fs::path& executablePath) {
    const auto absolute = fs::absolute(executablePath);
    const auto executableDirectory = absolute.parent_path();
    if (lower(executableDirectory.filename().wstring()) == L"build") {
        return executableDirectory.parent_path();
    }
    return executableDirectory;
}

inline fs::path workerPath(const fs::path& appRoot) {
    const auto packaged = appRoot / L"SteamArtworkLab.exe";
    if (fs::is_regular_file(packaged)) return packaged;
    return appRoot / L"build" / L"SteamArtworkLab.exe";
}

inline fs::path workspaceUiPath(const fs::path& appRoot) {
    return appRoot / L"workspace-ui";
}

inline fs::path configuredDataRoot() {
    const DWORD needed = GetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT", nullptr, 0);
    if (needed <= 1) return {};
    std::wstring value(needed, L'\0');
    const DWORD length = GetEnvironmentVariableW(
        L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT", value.data(), needed);
    if (length == 0 || length >= needed) return {};
    value.resize(length);
    return fs::absolute(fs::path(value));
}

// Probe only when choosing a fresh root. Existing data is never silently
// abandoned after a permission/storage fault; its normal health/retry path owns
// recovery. The file is uniquely created and deleted by its owning handle.
inline bool canCreateDataRoot(const fs::path& root) {
    if(root.empty()) return false;
    std::error_code error; auto existing = root;
    for(size_t n = 0; n < 64 && !existing.empty(); ++n) {
        if(fs::exists(existing,error)) break;
        if(error) return false;
        const auto parent = existing.parent_path();
        if(parent == existing) break;
        existing = parent;
    }
    if(existing.empty() || !fs::is_directory(existing,error) || error) return false;
    static std::atomic<unsigned long> sequence{0};
    const auto file = existing / (L".ymcc-data-health-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence) + L".tmp");
    const auto handle = CreateFileW(ioPath(file).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if(handle == INVALID_HANDLE_VALUE) return false;
    const char value = 0; DWORD written = 0;
    const bool ok = WriteFile(handle,&value,1,&written,nullptr) && written == 1;
    CloseHandle(handle); return ok;
}
inline fs::path userLibraryDataRoot() {
    const auto needed = GetEnvironmentVariableW(L"LOCALAPPDATA",nullptr,0);
    if(needed <= 1) return {};
    std::wstring value(needed,L'\0');
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA",value.data(),needed);
    if(!length || length >= needed) return {};
    value.resize(length);
    return fs::absolute(fs::path(value) / L"YeMan" / L"CustomSteamLibrary" / L"data");
}
// Explicit locations make single-volume/absent/blocked-drive policy testable
// without changing any actual drive or publishing a runtime test option.
inline fs::path selectDefaultDataRoot(const fs::path& appRoot, const fs::path& durableVolume,
    const fs::path& dLibraryRoot, const fs::path& dLegacyRoot, const fs::path& userDataRoot) {
    std::error_code error;
    const bool dDriveExists = fs::exists(durableVolume, error);
    error.clear();
    const auto localDataRoot = appRoot / L"data";
    const bool dDataExists = fs::is_directory(dLibraryRoot / L"data", error);
    error.clear();
    const bool dLegacyExists = fs::is_directory(dLegacyRoot, error);
    error.clear();
    const bool localDataExists = fs::is_directory(localDataRoot, error);

    // A stale empty durable directory must not hide a moved package that has
    // a real library snapshot beside the executable. Prefer a root that has
    // usable state, and when both do, keep the most recently updated scan.
    auto hasUsableLibrary = [](const fs::path& root) {
        std::error_code ec;
        const auto scan = root / L"state" / L"library-scan.json";
        if (!fs::is_regular_file(scan, ec)) return false;
        ec.clear();
        return fs::file_size(scan, ec) > 64 && !ec;
    };
    const bool dHasUsableLibrary = dDataExists && hasUsableLibrary(dLibraryRoot / L"data");
    const bool localHasUsableLibrary = localDataExists && hasUsableLibrary(localDataRoot);
    if (dHasUsableLibrary && !localHasUsableLibrary) return dLibraryRoot / L"data";
    if (localHasUsableLibrary && !dHasUsableLibrary) return localDataRoot;
    if (dHasUsableLibrary && localHasUsableLibrary) {
        std::error_code dTimeError;
        std::error_code localTimeError;
        const auto dTime = fs::last_write_time(
            dLibraryRoot / L"data" / L"state" / L"library-scan.json", dTimeError);
        const auto localTime = fs::last_write_time(
            localDataRoot / L"state" / L"library-scan.json", localTimeError);
        if (!dTimeError && !localTimeError && localTime > dTime) return localDataRoot;
        return dLibraryRoot / L"data";
    }

    // Retain existing selections. Do not create a second library when an
    // established root is temporarily full, disconnected or read-only.
    if(dDriveExists && (dDataExists || dLegacyExists || !localDataExists)) {
        if(dDataExists) return dLibraryRoot / L"data";
        if(!localDataExists && !userDataRoot.empty() && fs::is_directory(userDataRoot,error)) return userDataRoot;
        if(canCreateDataRoot(dLibraryRoot / L"data")) return dLibraryRoot / L"data";
    }
    if(localDataExists) return localDataRoot;
    error.clear();
    if(!userDataRoot.empty() && fs::is_directory(userDataRoot,error)) return userDataRoot;
    if(canCreateDataRoot(localDataRoot)) return localDataRoot;
    if(canCreateDataRoot(userDataRoot)) return userDataRoot;
    throw std::runtime_error("library-data-root-unwritable");
}
inline fs::path defaultDataRoot(const fs::path& appRoot) {
    const auto configured = configuredDataRoot();
    if(!configured.empty()) return configured;
    return selectDefaultDataRoot(appRoot,L"D:\\",L"D:\\YeMan\\CustomSteamLibrary",
        L"D:\\YeMan\\Steam大屏",userLibraryDataRoot());
}

inline std::vector<fs::path> legacyDataRoots(const fs::path& appRoot) {
    return {
        fs::path(L"D:\\YeMan\\Steam大屏"),
        appRoot / L"data",
        appRoot / L"data" / L"Steam大屏",
        appRoot / L"Steam大屏"
    };
}

inline fs::path jobsRoot(const fs::path& dataRoot) {
    return dataRoot / L"cache" / L"jobs";
}

inline fs::path webviewUserDataRoot(const fs::path& dataRoot) {
    return dataRoot / L"cache" / L"webview";
}

} // namespace custom_steam_library
