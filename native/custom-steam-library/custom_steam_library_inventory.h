#pragma once
#include "json.hpp"
#include "custom_steam_library_exclusions.h"
#include <algorithm>
#include <map>
#include <set>
#include <string>

namespace custom_steam_library {
// Built-in excluded Steam tools are never library items.
// One executable is one library item, even with overlapping scan roots or a
// manually added bin/x64 directory. No filename/title-only deduplication.
template<class PathKey>
nlohmann::json uniqueExecutableInventory(const nlohmann::json& rows, PathKey pathKey) {
    using Json = nlohmann::json;
    Json result = Json::array();
    if (!rows.is_array()) return result;
    std::map<std::string, size_t> positions;
    auto text = [](const Json& row, const char* field) -> std::string {
        const auto it = row.find(field);
        return it != row.end() && it->is_string() ? it->template get<std::string>() : std::string{};
    };
    auto flag = [](const Json& row, const char* field) {
        const auto it = row.find(field); return it != row.end() && it->is_boolean() && it->template get<bool>();
    };
    auto rank = [&](const Json& row) {
        const auto status = text(row, "status");
        if (flag(row, "steamNative") || text(row, "contentType") == "steam-native") return 100;
        if (status == "already-in-steam" || status == "added-to-steam") return 90;
        if (flag(row, "manualGameDirectoryLocked") || text(row, "discoverySource") == "manual") return 80;
        if (text(row, "steamVerificationStatus") == "steam-verified") return 70;
        return status == "ready" || status == "ready-to-add" ? 60 : 0;
    };
    for (const auto& row : rows) {
        if (!row.is_object() || builtinExcludedSteamTool(row)) continue;
        const auto exe = text(row, "primaryExecutable");
        const auto key = exe.empty() ? std::string{} : pathKey(exe);
        if (key.empty()) { result.push_back(row); continue; }
        auto [found, inserted] = positions.emplace(key, result.size());
        if (inserted) { result.push_back(row); continue; }
        auto& previous = result[found->second];
        const auto previousDir = pathKey(text(previous, "gameDirectory"));
        const auto directory = pathKey(text(row, "gameDirectory"));
        const bool replace = rank(row) > rank(previous) || (rank(row) == rank(previous) &&
            (directory.size() < previousDir.size() || (directory.size() == previousDir.size() && directory < previousDir)));
        std::map<std::string, std::string> aliases;
        auto collect = [&](const Json& value) {
            const auto dir = text(value, "gameDirectory");
            if (!dir.empty()) aliases.emplace(pathKey(dir), dir);
            const auto it = value.find("alternateGameDirectories");
            if (it != value.end() && it->is_array()) for (const auto& alias : *it)
                if (alias.is_string()) { const auto value = alias.template get<std::string>(); aliases.emplace(pathKey(value), value); }
        };
        collect(previous); collect(row);
        if (replace) previous = row;
        aliases.erase(pathKey(text(previous, "gameDirectory")));
        if (!aliases.empty()) {
            previous["alternateGameDirectories"] = Json::array();
            for (const auto& [_, alias] : aliases) previous["alternateGameDirectories"].push_back(alias);
        }
    }
    return result;
}
} // namespace custom_steam_library
