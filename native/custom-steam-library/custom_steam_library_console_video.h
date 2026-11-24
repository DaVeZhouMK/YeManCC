#pragma once
#include "json.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace custom_steam_library::console_video {
using Json = nlohmann::json;
struct Game {
    std::string_view title, platform;
    std::array<std::string_view, 5> aliases;
    std::array<std::string_view, 2> launcherAliases{};
};
inline constexpr Game totk{"The Legend of Zelda: Tears of the Kingdom", "switch",
    {"Zelda: Tears of the Kingdom", "Tears of the Kingdom", "\u585e\u5c14\u8fbe\u4f20\u8bf4\uff1a\u738b\u56fd\u4e4b\u6cea"}, {"totk", "zeldatotk"}};
inline constexpr Game botw{"The Legend of Zelda: Breath of the Wild", "switch-or-wii-u",
    {"Zelda: Breath of the Wild", "Breath of the Wild", "\u585e\u5c14\u8fbe\u4f20\u8bf4\uff1a\u65f7\u91ce\u4e4b\u606f"}, {"botw", "zeldabotw"}};
inline constexpr Game sentinels{"13 Sentinels: Aegis Rim", "switch-or-ps4",
    {"\u5341\u4e09\u673a\u5175\u9632\u536b\u5708", "\u5341\u4e09\u6a5f\u5175\u9632\u885b\u570f", "\u5341\u4e09\u6a5f\u5175\u9632\u885b\u5708", "13\u673a\u5175", "\u5341\u4e09\u673a\u5175"}};
inline constexpr std::array games{&totk,&botw,&sentinels};
struct Offer {
    const Game& game;
    std::string_view id, provider, url, poster, quality, sha256;
    size_t bytes;
    bool segmented = false;
};
inline constexpr std::string_view sentinelsPreviewUrl =
    "https://vulcan.dl.playstation.net/img/rnd/202009/1819/ZfD9JioLLk1ncUtSFDTFSwqV.mp4";
inline constexpr std::string_view botwPreviewUrl =
    "https://www.nintendo.com/jp/zelda/botw/assets/movie/top_trailer.mp4";
// Verified public MP4s, official first. Do not bundle the videos or infer media from a franchise tag.
inline constexpr std::array offers{
    Offer{sentinels, "13-sentinels", "playstation-store",
          sentinelsPreviewUrl, "https://image.api.playstation.com/vulcan/ap/rnd/202007/0917/41X8AtKHHfjwnvg0j4Lx1mb6.png", "MP4",
          "fcf9740d3de5f9a0aa504c53e865f5553427d4f72685ade509171237c21f61c2", 13826506, true},
    Offer{botw, "zelda-botw-nintendo", "nintendo",
          botwPreviewUrl, "https://www.nintendo.com/jp/zelda/botw/assets/movie/top_trailer.jpg", "MP4",
          "c79f11c06c274e93f6b5aa3ed5da01d5ba069a4f8284e4f7b9fcae9ab9fe0cbe", 6406940, true},
    Offer{totk, "zelda-totk", "motionbgs",
          "https://motionbgs.com/dl/hd/6361/", "https://motionbgs.com/media/6361/zelda-tears-of-the-kingdom.jpg", "1080p MP4",
          "ca0761667d3f386d914b5784d85e416b3ce5df2a77216c2afbb750360b97ee1e", 3237371},
    Offer{botw, "zelda-botw", "motionbgs",
          "https://motionbgs.com/dl/hd/502/", "https://motionbgs.com/media/502/the-legend-of-zelda-breath-of-the-wild.jpg", "1080p MP4",
          "be7153806d536322ee25d017c24add2134b84d10b0793f8ddd16243e9be9f953", 6686679},
};
inline const Offer* byId(std::string_view id) {
    for (const auto& offer : offers) if (offer.id == id) return &offer;
    return nullptr;
}
inline std::string titleKey(std::string_view title) {
    if (title.size() > 256) return {};
    std::string key;
    bool space = false;
    while (!title.empty()) {
        const auto ch = static_cast<unsigned char>(title.front());
        if (title.starts_with("\xEF\xBC\x9A")) { space = !key.empty(); title.remove_prefix(3); continue; }
        title.remove_prefix(1);
        if (ch == ':' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { space = !key.empty(); continue; }
        if (space) key += ' ';
        space = false;
        key += ch >= 'A' && ch <= 'Z' ? char(ch + ('a' - 'A')) : char(ch);
    }
    return key;
}
inline const Game* match(std::string_view title) {
    const auto key = titleKey(title);
    if (key.empty()) return nullptr;
    for (const auto* game : games) {
        if (key == titleKey(game->title)) return game;
        for (const auto alias : game->aliases) if (key == titleKey(alias)) return game;
    }
    return nullptr;
}
inline bool gameLauncher(const Game& game, std::string_view stem) {
    // A shared emulator EXE is not a per-game identity. Only accept explicit
    // per-title launcher names until ROM/Title ID is available.
    const auto compact = [](const std::string_view title) {
        std::string result;
        for (const auto ch : titleKey(title)) if (ch != ' ' && ch != '-' && ch != '_' && ch != '.') result += ch;
        return result;
    };
    auto launcher = titleKey(stem);
    for (const auto suffix : {std::string_view("_xlate"),std::string_view("_translator")})
        if (launcher.ends_with(suffix)) { launcher.resize(launcher.size() - suffix.size()); break; }
    const auto key = compact(launcher);
    if (key.empty()) return false;
    if (key == compact(game.title)) return true;
    for (const auto alias : game.aliases) if (key == compact(alias)) return true;
    for (const auto alias : game.launcherAliases) if (key == alias) return true;
    return false;
}
inline Json candidates(std::string_view query) {
    Json result{{"appId", 0}, {"query", std::string(query)}, {"candidates", Json::array()}};
    if (const auto* game = match(query)) for (const auto& offer : offers) {
        if (&offer.game != game) continue;
        result["candidates"].push_back({{"id", offer.id}, {"title", game->title},
            {"platform", game->platform}, {"provider", offer.provider}, {"quality", offer.quality}, {"previewUrl", offer.poster}});
    }
    return result;
}
inline const Offer& select(const Json& entry, const Json& catalog, const Json& request, int64_t now) {
    const auto created = catalog.value("createdAt", int64_t{});
    const auto revision = entry.value("bindingRevision", uint64_t{});
    const auto token = request.value("searchToken", std::string{});
    const auto* game = match(catalog.value("query", std::string{}));
    const auto* offer = byId(request.value("candidateId", std::string{}));
    if (!offer || &offer->game != game || !revision || catalog.value("source", std::string{}) != "console" ||
        entry.value("appId", int64_t{}) != 0 || token.empty() || token != catalog.value("searchToken", std::string{}) ||
        entry.value("key", std::string{}) != catalog.value("entryKey", std::string{}) ||
        request.value("bindingRevision", uint64_t{}) != revision || catalog.value("bindingRevision", uint64_t{}) != revision ||
        created <= 0 || created > now || now - created > 600000)
        throw std::runtime_error("Console video candidate expired or no longer belongs to this game");
    return *offer;
}
} // namespace custom_steam_library::console_video
