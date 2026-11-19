#pragma once
#include "json.hpp"
#include "custom_steam_library_exclusions.h"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

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
// Static library membership only. A scanned launcher can name a unique selected
// primary, but this relationship proves no live PID or native Steam association.
template<class PathKey, class WithinDirectory>
std::map<std::string, std::string> confirmedLauncherPrimaryLinks(
    const nlohmann::json& scan, PathKey pathKey, WithinDirectory withinDirectory) {
    using Json = nlohmann::json;
    const auto text = [](const Json& row, const char* field) -> std::string {
        if (!row.is_object()) return {};
        const auto value = row.find(field);
        return value != row.end() && value->is_string() ? value->template get<std::string>() : std::string{};
    };
    const auto flag = [](const Json& row, const char* field, bool expected) {
        if (!row.is_object()) return false;
        const auto value = row.find(field);
        return value != row.end() && value->is_boolean() && value->template get<bool>() == expected;
    };
    std::map<std::string, std::string> result;
    if (!scan.is_object() || !scan.contains("schemaVersion") || !scan["schemaVersion"].is_number_integer() ||
        scan["schemaVersion"] != 1 || scan.contains("libraryScanRecovery") ||
        !scan.contains("games") || !scan["games"].is_array()) return result;
    std::map<std::string, size_t> owners;
    for (const auto& game : scan["games"]) {
        if (!game.is_object() || !game.contains("candidates") || !game["candidates"].is_array()) continue;
        std::set<std::string> paths;
        for (const auto& candidate : game["candidates"]) {
            const auto key = pathKey(text(candidate, "path"));
            if (!key.empty()) paths.insert(key);
        }
        for (const auto& key : paths) ++owners[key];
    }
    for (const auto& game : scan["games"]) {
        if (!game.is_object() || text(game, "status") != "ready" || text(game, "primaryRole") != "main-game" ||
            text(game, "contentType") != "game" || !flag(game, "scanComplete", true) ||
            !flag(game, "candidateLimitReached", false) || !flag(game, "uniquePrimaryInvariant", true) ||
            !flag(game, "unrecognizedTool", false) || builtinExcludedSteamTool(game) ||
            !game.contains("candidates") || !game["candidates"].is_array()) continue;
        const auto directory = text(game, "gameDirectory"), primary = text(game, "primaryExecutable");
        const auto primaryKey = pathKey(primary);
        if (primaryKey.empty() || !withinDirectory(primary, directory) || owners[primaryKey] != 1) continue;
        bool valid = true, selectedPrimary = false;
        size_t selectedCount = 0;
        std::set<std::string> unique;
        std::vector<std::string> launchers;
        for (const auto& candidate : game["candidates"]) {
            const auto executable = text(candidate, "path"), key = pathKey(executable);
            if (key.empty() || !unique.insert(key).second || !withinDirectory(executable, directory) ||
                (!flag(candidate, "selected", true) && !flag(candidate, "selected", false))) { valid = false; break; }
            if (flag(candidate, "selected", true)) {
                ++selectedCount;
                selectedPrimary = key == primaryKey && text(candidate, "role") == "main-game";
            }
            if (key != primaryKey && text(candidate, "role") == "launcher" && flag(candidate, "selected", false) && owners[key] == 1)
                launchers.push_back(key);
        }
        if (!valid || !selectedPrimary || selectedCount != 1) continue;
        for (const auto& launcher : launchers) result.emplace(launcher, primary);
    }
    return result;
}

} // namespace custom_steam_library
