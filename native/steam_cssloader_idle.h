#pragma once
#include "steam_session_observer.h"
#include "decky_sidebar_launch.h"
#include "json.hpp"
#include <filesystem>
// Receipt checks have no scans, timers or launch permission. A copied/stale
// receipt cannot match another Steam process birth or another user's directory.
namespace ymcc::steamcssidle {
inline std::wstring receiptPath() {
    wchar_t base[32768]{};
    const auto n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, static_cast<DWORD>(std::size(base)));
    return n && n < std::size(base) ? std::wstring(base, n) + L"\\YeManCC\\SteamAppearance\\idle.json" : L"";
}
inline bool receiptMatches(const std::wstring& path, const steamsession::Session& session) {
    if (path.empty() || !session.pid) return false;
    for (auto cursor = std::filesystem::path(path); !cursor.empty();) {
        const auto attributes = GetFileAttributesW(cursor.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        const auto parent = cursor.parent_path(); if (parent == cursor) break; cursor = parent;
    }
    deckylaunch::Handle file;
    file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) { file.value = nullptr; return false; }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size) || size.QuadPart < 2 || size.QuadPart > 8192) return false;
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0'); DWORD read = 0;
    if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()) return false;
    try {
        const auto value = nlohmann::json::parse(bytes);
        const auto root = value.value("root", std::string{});
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, root.data(), static_cast<int>(root.size()), nullptr, 0);
        if (count <= 0) return false;
        std::wstring wide(count, L'\0'); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, root.data(), static_cast<int>(root.size()), wide.data(), count);
        return value.value("version", 0) == 1 && value.value("pid", uint64_t{}) == session.pid &&
            value.value("birth", std::string{}) == std::to_string(session.created) && _wcsicmp(wide.c_str(), session.root.c_str()) == 0;
    } catch (...) { return false; }
}
}
