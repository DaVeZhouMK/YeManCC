#pragma once
#include "json.hpp"
#include "custom_steam_library_exclusions.h"
#include <string>
#include <cstdint>

namespace custom_steam_library {
// An image search, numeric ID, or display name is never verified identity.
inline bool verifiedSteamIdentityMatch(const nlohmann::json& match) {
    if (!match.is_object() || builtinExcludedSteamTool(match)) return false;
    auto text = [&](const char* key) { const auto it=match.find(key); return it!=match.end()&&it->is_string()?it->get<std::string>():std::string{}; };
    auto number = [&](const char* key) -> int64_t {
        const auto it=match.find(key); if(it==match.end()||!it->is_number_integer())return 0;
        try { const auto value=it->get<int64_t>(); return value>0&&value<=2147483647?value:0; } catch(...) { return 0; }
    };
    // Historical builds labeled local INI title comments as official offline
    // verification. Preserve their images, but require a real official lookup
    // before these candidate IDs can promote a new Steam import.
    if (text("source") == "local-install-metadata-offline") return false;
    const auto source = text("source"), query = text("query");
    if (source.starts_with("parent-") || source.starts_with("exe-")) {
        auto asciiKey = [](const std::string& value) {
            std::string result;
            for (unsigned char c : value) {
                if (c >= 128) return std::string(8, 'x');
                if (c >= 'A' && c <= 'Z') result += static_cast<char>(c + ('a' - 'A'));
                else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) result += static_cast<char>(c);
            }
            return result;
        };
        const auto key = asciiKey(query);
        auto name = text("formalName"); if (name.empty()) name = text("name");
        if (name.empty()) name = text("displayName");
        const bool uppercase = query.size() >= 2 && std::all_of(query.begin(), query.end(), [](unsigned char c) { return c >= 'A' && c <= 'Z'; });
        if (!key.empty() && key.size() <= 4 && !name.empty() &&
            (key != asciiKey(name) || (uppercase && query != name))) return false;
        if (key == "game" || key == "launcher" || key == "start" || key == "main") return false;
    }
    const auto appId=number("appId");
    if(appId<=0||text("primaryProvider")!="steam"||text("identityStatus")!="verified"||text("steamVerificationStatus")!="steam-verified")return false;
    const auto version=text("resolverVersion");
    if(version!="steam-official-fuzzy-global-v4"&&version!="steam-official-fuzzy-global-v2")return false;
    if(match.contains("storefrontAppId")&&!match["storefrontAppId"].is_null()&&number("storefrontAppId")!=appId)return false;
    return number("matchedContentAppId")<=0 || (number("baseGameAppId")>0&&!text("contentRelation").empty()&&text("identityKind").find("with-explicit-content")!=std::string::npos);
}
} // namespace custom_steam_library
