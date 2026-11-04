#pragma once
#include "json.hpp"
#include "custom_steam_library_exclusions.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

namespace custom_steam_library::ymcc_whitelist {
using Json = nlohmann::json;
inline constexpr const char* beginMarker = "# BEGIN YMCC custom-steam-library managed rules";
inline constexpr const char* endMarker = "# END YMCC custom-steam-library managed rules";
inline std::string text(const Json& row, const char* key) {
    const auto i = row.find(key);
    return i != row.end() && i->is_string() ? i->get<std::string>() : std::string{};
}
inline bool flag(const Json& row, const char* key) {
    const auto i = row.find(key);
    return i != row.end() && i->is_boolean() && i->get<bool>();
}
inline std::string executableKey(std::string value) {
    if (value.empty() || value.find_first_of("\"*?[]<>|\r\n\t") != std::string::npos) return {};
    for (auto& c : value) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        else if (static_cast<unsigned char>(c) < 32) return {};
    }
    const bool drive = value.size() > 3 && value[0] >= 'a' && value[0] <= 'z' && value[1] == ':' && value[2] == '/';
    if (!drive && !value.starts_with("//")) return {};
    if (!value.ends_with(".exe")) return {};
    if (value.find("/../") != std::string::npos || value.find("/./") != std::string::npos) return {};
    return value;
}
inline bool validRule(const std::string& rule) {
    if (rule.empty() || rule == "." || rule == ".." || rule.front() == '#' || rule.front() == ';' ||
        rule.front() == ' ' || rule.back() == ' ' || rule.ends_with(".exe") ||
        rule.find_first_of("\\/:*?[]<>|#\"\r\n\t") != std::string::npos) return false;
    return std::none_of(rule.begin(), rule.end(), [](unsigned char c) { return c < 32 || (c >= 'A' && c <= 'Z'); });
}
inline Json emptyMembership() {
    return {{"schemaVersion", 1}, {"owner", "custom-steam-library"}, {"accounts", Json::object()}};
}
inline bool accountKey(const std::string& account) {
    return !account.empty() && std::all_of(account.begin(), account.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
}
inline bool revisionNumber(const Json& value) {
    return value.is_number_unsigned() || (value.is_number_integer() && value.get<std::int64_t>() >= 0);
}
inline bool bindingPath(const std::string& raw, const std::string& account) {
    if (!accountKey(account) || raw.empty() || raw.find_first_of("\"*?[]<>|\r\n\t") != std::string::npos) return false;
    std::string path = raw;
    for (auto& c : path) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        else if (static_cast<unsigned char>(c) < 32) return false;
    }
    const bool drive = path.size() > 3 && path[0] >= 'a' && path[0] <= 'z' && path[1] == ':' && path[2] == '/';
    if (!drive && !path.starts_with("//")) return false;
    if (path.find(':', drive ? 2 : 0) != std::string::npos || path.find("/../") != std::string::npos || path.find("/./") != std::string::npos) return false;
    // This is a structural account/path binding only. The publisher still
    // verifies the actual installation and current Steam bytes before use.
    const auto suffix = "/userdata/" + account + "/config/shortcuts.vdf";
    return path.size() > suffix.size() && path.ends_with(suffix);
}
inline void validateMembership(const Json& state) {
    if (!state.is_object() || !state.contains("schemaVersion") || !state["schemaVersion"].is_number_integer() ||
        state["schemaVersion"] != 1 || text(state, "owner") != "custom-steam-library" ||
        !state.contains("accounts") || !state["accounts"].is_object()) throw std::runtime_error("Invalid library-owned YMCC membership snapshot; preserved existing files");
    for (const auto& [account, row] : state["accounts"].items()) {
        if (!accountKey(account) || !row.is_object() || !row.contains("revision") || !revisionNumber(row["revision"]) ||
            !row.contains("entries") || !row["entries"].is_object()) throw std::runtime_error("Invalid account membership snapshot");
        for (const auto& [key, rule] : row["entries"].items())
            if (key.empty() || executableKey(key) != key || !rule.is_string() || !validRule(rule.get<std::string>())) throw std::runtime_error("Invalid managed executable membership");
    }
    // Missing bindings are retained as legacy unknown debt, never defaulted to
    // empty ownership. Even unused binding rows must not hide malformed data.
    if (state.contains("bindings")) {
        if (!state["bindings"].is_object()) throw std::runtime_error("Invalid membership installation bindings");
        for (const auto& [account, row] : state["bindings"].items())
            if (!accountKey(account) || !row.is_object() || !row.contains("shortcutsVdf") || !row["shortcutsVdf"].is_string() ||
                !bindingPath(row["shortcutsVdf"].get<std::string>(), account)) throw std::runtime_error("Invalid account installation binding; ownership remains unknown");
    }
}
inline std::uint64_t revision(const Json& state, const std::string& account) {
    validateMembership(state);
    if (!accountKey(account)) throw std::runtime_error("Unknown membership account");
    const auto i = state["accounts"].find(account);
    return i == state["accounts"].end() ? 0 : (*i)["revision"].get<std::uint64_t>();
}
inline bool authoritativeEvent(const Json& event) {
    if (!event.is_object() || !flag(event, "realSteamTarget") || text(event, "accountId").empty() ||
        !event.contains("items") || !event["items"].is_array()) return false;
    const auto mode = text(event, "mode");
    return mode == "steam-library-add-plan" ||
        ((mode == "steam-library-add-commit" || mode == "steam-library-delete-commit") && flag(event, "committed"));
}
inline bool currentEventSchema(const Json& event) {
    return event.is_object() && event.contains("schemaVersion") && event["schemaVersion"].is_number_integer() && event["schemaVersion"] == 1;
}
inline bool completeInventory(const Json& state, const Json& items) {
    if (!state.is_object() || !state.contains("games") || !state["games"].is_array() ||
        !items.is_array() || state["games"].size() != items.size() ||
        !state.contains("roots") || !state["roots"].is_array()) return false;
    for (const auto& root : state["roots"])
        if (!root.is_object() || !flag(root, "scanComplete")) return false;
    for (const auto* section : {"unscannedGames", "missingGames"})
        if (state.contains(section) && (!state[section].is_array() || !state[section].empty())) return false;
    for (const auto& game : state["games"])
        if (!game.is_object() || (game.contains("scanComplete") && !flag(game, "scanComplete"))) return false;
    if (state.contains("skippedDirectories") && state["skippedDirectories"].is_array())
        for (const auto& row : state["skippedDirectories"])
            if (text(row, "reason").find("unavailable") != std::string::npos || text(row, "reason").find("incomplete") != std::string::npos) return false;
    return true;
}
// Only freshly constructed/read Steam plans and successful commits call this.
// Persist the consumed revision in result JSON so replaying historical last-*
// payloads after a later deletion cannot resurrect their membership.
inline void prepareLiveEvent(Json& event, const Json& state, bool completeAccount) {
    if (!authoritativeEvent(event) || !currentEventSchema(event)) return;
    event["ymccMembershipAuthority"] = {{"baseRevision", revision(state, text(event, "accountId"))},
        {"completeAccount", completeAccount}, {"source", "live-steam-shortcuts"}};
}
struct Update { Json membership; bool accepted = false; std::string reason; };
template<class NormalizeRule>
Update applyPrepared(const Json& state, const Json& event, NormalizeRule normalizeRule) {
    validateMembership(state);
    if (!authoritativeEvent(event) || !currentEventSchema(event)) return {state, false, "not-a-live-membership-event"};
    const auto authority = event.find("ymccMembershipAuthority");
    if (authority == event.end() || !authority->is_object() || text(*authority, "source") != "live-steam-shortcuts" ||
        !authority->contains("baseRevision") || !revisionNumber((*authority)["baseRevision"]) ||
        !authority->contains("completeAccount") || !(*authority)["completeAccount"].is_boolean()) return {state, false, "missing-live-authority"};
    const auto account = text(event, "accountId");
    const auto previousRevision = revision(state, account);
    if ((*authority)["baseRevision"].get<std::uint64_t>() != previousRevision) return {state, false, "stale-account-revision"};
    Json next = state;
    Json before = state["accounts"].contains(account) ? state["accounts"][account]["entries"] : Json::object();
    Json entries = before;
    const auto mode = text(event, "mode");
    const bool deleting = mode == "steam-library-delete-commit";
    if (mode == "steam-library-add-plan" && flag(*authority, "completeAccount")) entries = Json::object();
    for (const auto& row : event["items"]) {
        if (!row.is_object()) continue;
        const auto executable = text(row, "primaryExecutable");
        const auto key = executableKey(executable);
        if (key.empty()) continue;
        if (deleting) {
            if (text(row, "status") == "ready-to-delete") entries.erase(key);
            continue;
        }
        if (mode == "steam-library-add-plan" && text(row, "ymccSteamMembership") == "absent" &&
            text(row, "primarySelectionStatus") == "ready" && text(row, "primaryRole") == "main-game") {
            entries.erase(key);
            continue;
        }
        const auto status = text(row, "status");
        if (status != "added-to-steam" && status != "already-in-steam" && status != "steam-native") continue;
        if (text(row, "primarySelectionStatus") != "ready" || text(row, "primaryRole") != "main-game" ||
            flag(row, "unrecognizedTool") || flag(row, "isTool") || flag(row, "isLauncher") ||
            custom_steam_library::builtinExcludedSteamTool(row)) continue;
        const auto content = text(row, "contentType");
        if (content == "tool" || content == "launcher" || content == "emulator-launcher" || content == "non-game") continue;
        const auto rule = normalizeRule(executable);
        if (validRule(rule)) entries[key] = rule;
    }
    if (entries != before || !next["accounts"].contains(account)) {
        if (previousRevision == (std::numeric_limits<std::uint64_t>::max)()) throw std::runtime_error("Membership revision overflow");
        auto& row = next["accounts"][account];
        if (!row.is_object()) row = Json::object();
        row["revision"] = previousRevision + 1;
        row["entries"] = std::move(entries);
    }
    return {std::move(next), true, "authoritative-membership"};
}
inline std::set<std::string> unionRules(const Json& state) {
    validateMembership(state);
    std::set<std::string> rules;
    for (const auto& [account, row] : state["accounts"].items()) {
        if (!state.contains("bindings") || !state["bindings"].contains(account))
            throw std::runtime_error("Unbound legacy membership remains unknown; refresh that account explicitly");
        for (const auto& [path, value] : row["entries"].items()) rules.insert(value.get<std::string>());
    }
    return rules;
}
// Only exact complete marker pairs grant deletion authority. Text outside the
// block is byte-preserved. If a manual UI rewrite removed both markers, all
// naked rules are manual for this purpose; never infer ownership from history.
inline std::string renderManagedBlock(const std::string& original, const std::set<std::string>& rules) {
    for (const auto& rule : rules) if (!validRule(rule)) throw std::runtime_error("Invalid managed rule");
    std::size_t begin = std::string::npos, end = std::string::npos;
    for (std::size_t offset = 0; offset < original.size();) {
        const auto newline = original.find('\n', offset);
        const auto next = newline == std::string::npos ? original.size() : newline + 1;
        auto line = original.substr(offset, (newline == std::string::npos ? original.size() : newline) - offset);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == beginMarker) {
            if (begin != std::string::npos || end != std::string::npos) throw std::runtime_error("Ambiguous managed whitelist markers; preserved file");
            begin = offset;
        } else if (line == endMarker) {
            if (begin == std::string::npos || end != std::string::npos) throw std::runtime_error("Unpaired managed whitelist markers; preserved file");
            end = next;
        }
        offset = next;
    }
    if ((begin == std::string::npos) != (end == std::string::npos)) throw std::runtime_error("Incomplete managed whitelist block; preserved file");
    const std::string newline = original.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string block;
    if (!rules.empty()) {
        block = beginMarker + newline;
        for (const auto& rule : rules) block += rule + newline;
        block += endMarker + newline;
    }
    if (begin != std::string::npos) return original.substr(0, begin) + block + original.substr(end);
    if (block.empty()) return original;
    return original + (original.empty() || original.back() == '\n' ? "" : newline) + block;
}
} // namespace custom_steam_library::ymcc_whitelist
