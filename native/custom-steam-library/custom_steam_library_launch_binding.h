#pragma once

#include "custom_steam_library_launch_file.h"
#include "custom_steam_library_plan_receipt.h"
#include <fstream>
#include <optional>

namespace custom_steam_library::launch_file {

inline constexpr size_t maxBindingBytes = 128 * 1024;
inline constexpr wchar_t bindingRecoveryFileName[] = L"binding.recovery.json";

struct OrdinaryShortcutFields {
    std::string target;
    std::string startIn;
    std::string options;
};

inline std::optional<OrdinaryShortcutFields> ordinaryShortcutFields(const nlohmann::json& binding) {
    const auto saved = binding.find("previousOrdinaryLaunch");
    if (saved == binding.end() || !saved->is_object()) return std::nullopt;
    for (const auto* key : {"target", "startIn", "options"}) {
        const auto field = saved->find(key);
        if (field == saved->end() || !field->is_string() ||
            field->get_ref<const std::string&>().find('\0') != std::string::npos) return std::nullopt;
    }
    const auto& target = saved->at("target").get_ref<const std::string&>();
    if (target.empty()) return std::nullopt;
    return OrdinaryShortcutFields{target, saved->at("startIn").get<std::string>(), saved->at("options").get<std::string>()};
}

inline fs::path bindingRoot(const fs::path& dataRoot) {
    return dataRoot / L"state" / L"steam-launch";
}

inline std::wstring_view unquoted(std::wstring_view value) {
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') return value.substr(1, value.size() - 2);
    return value;
}

inline bool bindingDirectory(const fs::path& dataRoot, const fs::path& start) {
    const auto leaf = start.filename().native();
    return absolutePath(start) && samePath(start.parent_path(), bindingRoot(dataRoot)) && leaf.size() == 64 &&
        std::all_of(leaf.begin(), leaf.end(), [](wchar_t ch) { return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f'); });
}

inline bool cleanBindingDirectory(const fs::path& dataRoot, const fs::path& start) {
    if (!bindingDirectory(dataRoot, start)) return false;
    const auto attributes = GetFileAttributesW(start.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) || attributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;
    const auto appId = GetFileAttributesW((start / L"steam_appid.txt").c_str());
    return appId == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
}

struct BindingPayload {
    std::string bytes;
    bool fromRecovery = false;
};

inline std::optional<std::string> readBindingBytes(const fs::path& path, bool& missing) {
    missing = false;
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        missing = GetLastError() == ERROR_FILE_NOT_FOUND;
        return std::nullopt;
    }
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return std::nullopt;
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error || size > maxBindingBytes) return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    std::string bytes(static_cast<size_t>(size), '\0');
    if (!file || !file.read(bytes.data(), bytes.size()) || file.peek() != std::char_traits<char>::eof() || file.bad())
        return std::nullopt;
    return bytes;
}

template<class ToWide>
inline fs::path readPrimaryBinding(const fs::path& dataRoot, const fs::path& vdf, const std::string& account,
                                  uint32_t id, std::wstring_view target, std::wstring_view startIn,
                                  std::wstring_view options, ToWide toWide,
                                  std::optional<OrdinaryShortcutFields>* ordinary = nullptr,
                                  BindingPayload* payload = nullptr, bool allowRecovery = true) noexcept {
    if (ordinary) ordinary->reset();
    if (payload) *payload = {};
    try {
        if (dataRoot.empty() || id < 0x80000000u || account.empty()) return {};
        const fs::path start(unquoted(startIn));
        if (!cleanBindingDirectory(dataRoot, start)) return {};
        const auto script = steamScriptOperand(target, options);
        if (script.empty()) return {};
        bool missing = false;
        auto bytes = readBindingBytes(start / L"binding.json", missing);
        if (!bytes && !missing) return {};
        auto binding = bytes ? nlohmann::json::parse(*bytes, nullptr, false) : nlohmann::json{};
        const bool useRecovery = missing || binding.is_discarded();
        if (useRecovery) {
            // Read failure, a link or a different parseable document is not
            // evidence that the primary is missing or truncated.
            if (!allowRecovery) return {};
            bytes = readBindingBytes(start / bindingRecoveryFileName, missing);
            if (!bytes || bytes->empty()) return {};
            binding = nlohmann::json::parse(*bytes, nullptr, false);
        }
        if (!binding.is_object() || binding.value("schemaVersion", 0) != 1 ||
            binding.value("accountId", std::string{}) != account || binding.value("shortcutAppId", 0u) != id ||
            !samePath(fs::path(toWide(binding.value("shortcutsVdf", std::string{}))), vdf) ||
            !samePath(fs::path(toWide(binding.value("script", std::string{}))), script)) return {};
        if (binding.dump() != *bytes || start.filename().native() != toWide(plan_receipt::digest(binding))) return {};
        const fs::path executable(toWide(binding.value("primaryExecutable", std::string{})));
        if (!absolutePath(executable) || !equal(executable.extension().native(), L".exe")) return {};
        if (ordinary) *ordinary = ordinaryShortcutFields(binding);
        if (payload) *payload = {std::move(*bytes), useRecovery};
        // An unmounted drive must not allow a fallback to the shared host.
        return executable;
    } catch (...) { return {}; }
}

} // namespace custom_steam_library::launch_file
