#pragma once
#include "json.hpp"
#include <string>
#include <stdexcept>
#include <cstdint>
#include <set>
#include <algorithm>

namespace custom_steam_library {
// Permanent product policy, independent of removable user exclusions. These
// are Steam shared prerequisites, never a game or an identity/artwork target.
inline constexpr int steamCommonRedistributablesAppId = 228980;
inline bool builtinExcludedSteamAppId(const nlohmann::json& value) {
    if (value.is_number_integer()) {
        if (value.is_number_unsigned()) return value.get<uint64_t>() == steamCommonRedistributablesAppId;
        return value.get<int64_t>() == steamCommonRedistributablesAppId;
    }
    if (!value.is_string()) return false;
    const auto text = value.get<std::string>();
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return false;
    const auto end = text.find_last_not_of(" \t\r\n");
    unsigned int id = 0;
    for (size_t i = begin; i <= end; ++i) {
        const auto c = text[i];
        if (c < '0' || c > '9' || id > 228980u / 10u) return false;
        id = id * 10u + static_cast<unsigned int>(c - '0');
        if (id > 228980u) return false;
    }
    return id == steamCommonRedistributablesAppId;
}
inline bool builtinExcludedSteamToolName(const std::string& text) {
    std::string key;
    for (unsigned char c : text) {
        if (c >= 'A' && c <= 'Z') key += static_cast<char>(c + ('a' - 'A'));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 128) key += static_cast<char>(c);
    }
    // Exact normalized names only: do not hide other Steamworks/Redist games.
    static const std::set<std::string> exact = {
        "steamworkscommonredistributables", "steamworksshared",
        "unrealengine", "unrealengine4", "unrealengine5",
        "unityhub", "unityeditor", "godotengine", "godot",
        "gamemaker", "gamemakerstudio", "rpgmaker",
        "steamcmd", "steamvr", "steamlink", "steamworkssdk", "steamsdk",
        "sourcesdk", "sourcesdkbase", "sourcefilmmaker",
        "epicgameslauncher", "proton", "steamlinuxruntime"
    };
    if (exact.contains(key)) return true;
    const auto versioned = [](const std::string& prefix, const std::string& rest) {
        if (rest.size() < 2 || rest.size() > 5) return false;
        return std::all_of(rest.begin(), rest.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
    };
    if (key.starts_with("unrealengine") && versioned("unrealengine", key.substr(12))) return true;
    if (key.starts_with("ue") && versioned("ue", key.substr(2))) return true;
    if (key.starts_with("steamlinuxruntime") && versioned("steamlinuxruntime", key.substr(17))) return true;
    if (key.starts_with("proton") && versioned("proton", key.substr(6))) return true;
    return false;
}
inline bool builtinExcludedSteamToolPath(const std::string& path) {
    size_t begin = 0;
    while (begin < path.size()) {
        const auto end = path.find_first_of("/\\", begin);
        const auto part = path.substr(begin, end == std::string::npos ? end : end - begin);
        if (builtinExcludedSteamToolName(part)) return true;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return false;
}
inline bool builtinExcludedSteamTool(const nlohmann::json& row, unsigned int depth = 0) {
    if (!row.is_object()) return false;
    for (const auto key : {"nativeSteamAppId", "steamStoreAppId", "storefrontAppId", "appId", "steamAppId", "steamId", "steam_appid"}) {
        const auto it = row.find(key);
        if (it != row.end() && builtinExcludedSteamAppId(*it)) return true;
    }
    for (const auto key : {"directoryName", "formalName", "displayName", "name", "primaryProductName", "manifestName", "manualName", "query"}) {
        const auto it = row.find(key);
        if (it != row.end() && it->is_string() && builtinExcludedSteamToolName(it->get<std::string>())) return true;
    }
    for (const auto key : {"gameDirectory", "primaryExecutable", "nativeSteamGameDirectory", "nativeSteamExecutable", "executable"}) {
        const auto it = row.find(key);
        if (it != row.end() && it->is_string() && builtinExcludedSteamToolPath(it->get<std::string>())) return true;
    }
    // Only identity-bearing records, never arbitrary artwork IDs or aliases.
    if (depth < 3) for (const auto key : {"nativeSteam", "steamNativeRecord", "steam", "match", "resolvedIdentity"}) {
        const auto it = row.find(key);
        if (it != row.end() && builtinExcludedSteamTool(*it, depth + 1)) return true;
    }
    return false;
}
inline void rejectBuiltinExcludedSteamTool(const nlohmann::json& row) {
    if (builtinExcludedSteamTool(row))
        throw std::runtime_error("Steamworks Common Redistributables (AppID 228980) is a built-in excluded Steam tool, not a game");
}
} // namespace custom_steam_library
