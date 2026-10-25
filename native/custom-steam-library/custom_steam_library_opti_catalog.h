#pragma once
#include <windows.h>
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"

// Metadata only: never infer OPT installation, version, injection or preferences.
// The schema was checked against OptiscalerClient Models/Game.cs and the local
// 1.0.7.2 catalog: Steam=0, Manual=8, local covers use CoverImageUrl.
namespace custom_steam_library::opti_catalog {
using Json = nlohmann::json;
inline constexpr unsigned startupDelayMs = 30000;
inline constexpr unsigned itemDelayMs = 50;
inline constexpr size_t maxCatalogBytes = 16u << 20;
inline constexpr size_t maxGames = 20000;
struct Candidate {
    std::string installPath, executablePath, name, coverPath;
    int steamAppId = 0; // Native Steam ownership only; never a store search ID.
};
inline std::string text(const Json& game, const char* key) {
    const auto it = game.find(key);
    return it != game.end() && it->is_string() ? it->get<std::string>() : std::string{};
}
inline std::string pathKey(std::string value) {
    if (value.empty() || value.size() > 32768) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!count) return {};
    std::wstring wide(count, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), wide.data(), count)) return {};
    for (auto& c : wide) if (c == L'/') c = L'\\';
    const int lowerCount = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, wide.data(), count, nullptr, 0, nullptr, nullptr, 0);
    if (!lowerCount) return {};
    std::wstring lowered(lowerCount, L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, wide.data(), count, lowered.data(), lowerCount, nullptr, nullptr, 0)) return {};
    wide = std::move(lowered);
    while (!wide.empty() && wide.back() == L'\\') wide.pop_back();
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string result(bytes, '\0');
    if (bytes) WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), bytes, nullptr, nullptr);
    return result;
}
inline bool absolutePath(const std::string& path) {
    if (path.size() < 3 || std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32; })) return false;
    const auto key = pathKey(path);
    // UNC/device paths could block this deliberately local, low-priority pass.
    if (key.size() < 3 || key[0] < 'a' || key[0] > 'z' || key[1] != ':' || key[2] != '\\') return false;
    size_t start = 3;
    while (start <= key.size()) {
        const auto end = key.find('\\', start);
        const auto part = key.substr(start, end == std::string::npos ? end : end - start);
        if (part == "." || part == ".." || part.find_first_of(":*?\"<>|") != std::string::npos) return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}
inline bool valid(const Candidate& item) {
    if (!absolutePath(item.installPath) || pathKey(item.installPath).size() <= 3) return false;
    if (item.executablePath.empty()) return item.steamAppId > 0;
    const auto exe = pathKey(item.executablePath), root = pathKey(item.installPath);
    return absolutePath(item.executablePath) && exe.ends_with(".exe") && exe.starts_with(root + "\\");
}
inline bool missing(const Json& game, const char* key) {
    const auto it = game.find(key);
    return it == game.end() || it->is_null() || (it->is_string() && it->get_ref<const std::string&>().empty());
}
struct Result {
    std::string status = "idle", reason;
    size_t candidates = 0, added = 0, supplemented = 0, covers = 0, skipped = 0;
    Json document() const {
        return {{"status", status}, {"reason", reason}, {"candidates", candidates},
            {"added", status == "synced" ? added : 0}, {"supplemented", status == "synced" ? supplemented : 0},
            {"covers", status == "synced" ? covers : 0}, {"coversCopied", covers}, {"skipped", skipped}};
    }
};
struct Ports {
    std::function<std::optional<std::string>()> read;
    std::function<void(const std::string&)> backup, commit;
    std::function<bool()> clientRunning, cancelled;
    std::function<void()> pace;
    std::function<std::string()> newId;
    std::function<std::string(const Candidate&)> copyCover, resolveCover;
};
inline Json parseCatalog(const std::optional<std::string>& bytes) {
    if (!bytes) return Json::array();
    if (bytes->size() > maxCatalogBytes) throw std::runtime_error("OPT catalog exceeds size limit");
    const auto catalog = Json::parse(*bytes);
    if (!catalog.is_array() || catalog.size() > maxGames ||
        !std::all_of(catalog.begin(), catalog.end(), [](const Json& row) { return row.is_object(); }))
        throw std::runtime_error("Invalid OPT catalog; original preserved");
    return catalog;
}
inline Json merge(Json games, const std::vector<Candidate>& candidates, Ports& ports, Result& result) {
    using Index = std::map<std::string, std::set<size_t>>;
    Index byExe, byDirectory, bySteamId;
    auto index = [&](size_t i) {
        const auto& row = games[i];
        const auto exe = pathKey(text(row, "ExecutablePath")), dir = pathKey(text(row, "InstallPath"));
        if (!exe.empty()) byExe[exe].insert(i);
        if (!dir.empty()) byDirectory[dir].insert(i);
        if (row.contains("Platform") && row["Platform"] == 0 && !text(row, "AppId").empty())
            bySteamId[text(row, "AppId")].insert(i);
    };
    for (size_t i = 0; i < games.size(); ++i) index(i);
    result.candidates = candidates.size();
    for (const auto& item : candidates) {
        if (ports.cancelled()) throw std::runtime_error("OPT supplement cancelled");
        if (!valid(item)) { ++result.skipped; continue; }
        const auto exe = pathKey(item.executablePath), dir = pathKey(item.installPath);
        std::set<size_t> matches;
        auto include = [&](const Index& values, const std::string& key) {
            if (const auto found = values.find(key); found != values.end()) matches.insert(found->second.begin(), found->second.end());
        };
        if (!exe.empty()) include(byExe, exe);
        if (item.steamAppId > 0) include(bySteamId, std::to_string(item.steamAppId));
        if (matches.empty()) {
            include(byDirectory, dir);
            // Older manual OPT entries point at the EXE's subdirectory.
            if (!exe.empty()) include(byDirectory, exe.substr(0, exe.find_last_of('\\')));
        }
        if (matches.size() > 1) { ++result.skipped; continue; }
        if (matches.empty() && games.size() >= maxGames) { ++result.skipped; continue; }
        const bool added = matches.empty();
        size_t i = added ? games.size() : *matches.begin();
        if (added) games.push_back({{"Platform", item.steamAppId > 0 ? 0 : 8},
            {"AppId", item.steamAppId > 0 ? std::to_string(item.steamAppId) : "Manual_" + ports.newId()}});
        const bool needsMetadata = (!item.name.empty() && missing(games[i], "Name")) ||
            missing(games[i], "InstallPath") || (!item.executablePath.empty() && missing(games[i], "ExecutablePath"));
        const bool needsCover = missing(games[i], "CoverImageUrl") && (!item.coverPath.empty() || ports.resolveCover);
        if (!added && !needsMetadata && !needsCover) continue;
        // Pace actual missing data only. Complete records need neither artwork IO
        // nor a 50ms sleep, so the once-only worker can exit promptly.
        ports.pace();
        if (ports.cancelled()) throw std::runtime_error("OPT supplement cancelled");
        const auto before = games[i];
        auto fill = [&](const char* key, const std::string& value) {
            if (!value.empty() && missing(games[i], key)) games[i][key] = value;
        };
        fill("Name", item.name);
        fill("InstallPath", item.installPath);
        fill("ExecutablePath", item.executablePath);
        if (needsCover) {
            auto withCover = item;
            if (withCover.coverPath.empty() && ports.resolveCover) withCover.coverPath = ports.resolveCover(item);
            if (!withCover.coverPath.empty()) {
                const auto cover = ports.copyCover(withCover);
                if (!cover.empty()) { fill("CoverImageUrl", cover); ++result.covers; }
            }
        }
        if (added) ++result.added;
        else if (before != games[i]) ++result.supplemented;
        // Add newly filled identities to the index, including earlier candidates.
        index(i);
    }
    return games;
}
inline Result supplement(const std::vector<Candidate>& candidates, Ports& ports) {
    Result result;
    result.candidates = candidates.size();
    if (ports.cancelled()) { result.status = "cancelled"; return result; }
    if (ports.clientRunning()) { result.status = "deferred"; result.reason = "client-running"; return result; }
    const auto original = ports.read();
    const auto catalog = parseCatalog(original);
    const auto merged = merge(catalog, candidates, ports, result);
    if (merged == catalog) return result; // No backup, covers or catalog writes on an unchanged pass.
    const auto content = merged.dump(2) + '\n';
    if (content.size() > maxCatalogBytes) throw std::runtime_error("OPT merged catalog exceeds size limit");
    auto ready = [&] {
        if (ports.cancelled()) { result.status = "cancelled"; return false; }
        if (ports.read() != original) { result.status = "deferred"; result.reason = "catalog-changed"; return false; }
        if (ports.clientRunning()) { result.status = "deferred"; result.reason = "client-running"; return false; }
        return true;
    };
    if (!ready()) return result;
    if (original) ports.backup(*original);
    if (!ready()) return result;
    ports.commit(content);
    if (ports.read() != std::optional<std::string>{content}) throw std::runtime_error("OPT catalog verification failed");
    result.status = "synced";
    return result;
}
} // namespace custom_steam_library::opti_catalog
