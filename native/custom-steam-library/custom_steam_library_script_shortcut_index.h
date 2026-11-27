#pragma once

#include "custom_steam_library_launch_binding.h"
#include <map>
#include <set>

namespace custom_steam_library::launch_file {

// Account-scoped, read-only shortcut membership. The row pointers must outlive the index.
template<class Row, auto pathKey, auto toWide>
class SteamScriptShortcutIndex {
    using Key = decltype(pathKey(std::declval<const fs::path&>()));

    static std::string text(const nlohmann::json& row, const char* key) {
        const auto field = row.find(key);
        return field != row.end() && field->is_string() ? field->get<std::string>() : std::string{};
    }

    static bool containsPath(const fs::path& child, const fs::path& parent) {
        auto prefix = pathKey(parent);
        const auto full = pathKey(child);
        if (prefix.empty()) return false;
        if (full == prefix) return true;
        if (prefix.back() != '\\') prefix.push_back('\\');
        return full.starts_with(prefix);
    }
    struct Entry {
        const Row* shortcut;
        bool managed;
        Key primaryKey;
    };
    std::map<Key, std::vector<Entry>> byWrapper;
    std::map<Key, std::vector<const Row*>> byPrimary;
    std::map<Key, std::set<Key>> aliasWrappersByPrimary;
    std::map<Key, std::set<Key>> wrapperOwners;
    bool completeAliasScan = false;

    static bool managedStart(const fs::path& dataRoot, const fs::path& start) {
        namespace launch = custom_steam_library::launch_file;
        const auto root = launch::bindingRoot(dataRoot);
        for (auto path = start.lexically_normal(); !path.empty(); path = path.parent_path()) {
            if (launch::samePath(path, root)) return true;
            if (path == path.root_path()) break;
        }
        return false;
    }

public:
    struct Match {
        const Row* shortcut = nullptr;
        bool bindingConflict = false;
        bool ambiguous = false;
        bool conflicted() const { return bindingConflict || ambiguous; }
    };

    // The index borrows the observed shortcut rows. It never changes a card or
    // supplies runtime Steam identity.
    bool observe(const fs::path& dataRoot, const fs::path& vdf, const std::string& account, uint32_t id,
                 const Row& shortcut, std::wstring_view exe, std::wstring_view startIn, std::wstring_view options) {
        const auto script = steamScriptOperand(exe, options);
        if (script.empty()) return false;
        const bool managed = managedStart(dataRoot, fs::path(unquoted(startIn)));
        const auto primary = managed ? readPrimaryBinding(dataRoot, vdf, account, id, exe, startIn, options, toWide) : fs::path{};
        const auto key = primary.empty() ? Key{} : pathKey(primary);
        if (!key.empty()) byPrimary[key].push_back(&shortcut);
        byWrapper[pathKey(script)].push_back({&shortcut, managed, key});
        return true;
    }

    void observeScanAliases(const nlohmann::json& state, bool complete,
                            const std::vector<fs::path>& uncertainDirectories = {}, bool includeUnreferenced = false) {
        if (byWrapper.empty() && !includeUnreferenced) return;
        namespace launch = custom_steam_library::launch_file;
        const auto& games = state.at("games");
        completeAliasScan = complete;
        for (const auto& game : games) {
            const auto aliases = game.find("alternateGameDirectories");
            if (aliases == game.end() || !aliases->is_array() || aliases->empty()) continue;
            const fs::path primary(toWide(text(game, "primaryExecutable")));
            if (primary.empty()) continue;
            const auto primaryKey = pathKey(primary);
            for (const auto& value : *aliases) {
                if (!value.is_string()) continue;
                const fs::path alias(toWide(value.get<std::string>()));
                if (!alias.is_absolute() || !containsPath(primary, alias)) continue;
                const auto wrapper = pathKey(launch::wrapperPath(alias));
                const bool referenced = byWrapper.count(wrapper) != 0;
                if (!referenced && !includeUnreferenced) continue;
                if (referenced) aliasWrappersByPrimary[primaryKey].insert(wrapper);
                wrapperOwners[wrapper].insert(primaryKey);
            }
        }
        // Exact-directory claims count even when no alias was recorded. An
        // unresolved second row also prevents reusing a legacy wrapper.
        for (const auto& game : games) {
            const fs::path directory(toWide(text(game, "gameDirectory")));
            if (directory.empty()) continue;
            const auto wrapper = pathKey(launch::wrapperPath(directory));
            if (!includeUnreferenced && !byWrapper.count(wrapper)) continue;
            const fs::path primary(toWide(text(game, "primaryExecutable")));
            wrapperOwners[wrapper].insert(primary.empty() ? Key{} : pathKey(primary));
        }
        for (const auto& directory : uncertainDirectories) {
            const auto wrapper = pathKey(launch::wrapperPath(directory));
            if (includeUnreferenced || byWrapper.count(wrapper)) wrapperOwners[wrapper].insert(Key{});
        }
    }

    using WrapperOwners = std::map<Key, std::set<Key>>;

    WrapperOwners takeWrapperOwners() && { return std::move(wrapperOwners); }

    static void requireUniqueWrapperOwner(const WrapperOwners& owners, const fs::path& wrapper, const Key& primaryKey) {
        const auto found = owners.find(pathKey(wrapper));
        if (primaryKey.empty() || found == owners.end() || found->second.size() != 1 || !found->second.contains(primaryKey))
            throw std::runtime_error("此目录有多个或待确认的游戏主程序，无法确认 VBS 归属。请先确认主程序，或使用普通启动。");
    }

    Match find(const fs::path& gameDirectory, const Key& primaryKey) const {
        Match result;
        if (primaryKey.empty() || byWrapper.empty()) return result;
        const auto accept = [&](const Row* shortcut) {
            if (result.shortcut && result.shortcut != shortcut) result.ambiguous = true;
            else result.shortcut = shortcut;
        };
        if (const auto matches = byPrimary.find(primaryKey); matches != byPrimary.end())
            for (const auto* shortcut : matches->second) accept(shortcut);
        const auto observeWrapper = [&](const Key& key, bool viaAlias) {
            const auto wrapper = byWrapper.find(key);
            const auto owners = wrapperOwners.find(key);
            if (wrapper != byWrapper.end()) for (const auto& entry : wrapper->second) {
                if (entry.managed && entry.primaryKey != primaryKey) result.bindingConflict = true;
                else if (!entry.managed && ((viaAlias && !completeAliasScan) ||
                    (owners != wrapperOwners.end() && (owners->second.size() != 1 || !owners->second.contains(primaryKey)))))
                    result.ambiguous = true;
                else accept(entry.shortcut);
            }
        };
        namespace launch = custom_steam_library::launch_file;
        const auto currentWrapper = gameDirectory.empty() ? Key{} : pathKey(launch::wrapperPath(gameDirectory));
        if (!currentWrapper.empty()) observeWrapper(currentWrapper, false);
        if (const auto aliases = aliasWrappersByPrimary.find(primaryKey); aliases != aliasWrappersByPrimary.end()) {
            for (const auto& key : aliases->second) if (key != currentWrapper) observeWrapper(key, true);
        }
        return result;
    }
};

} // namespace custom_steam_library::launch_file
