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
    const auto appId=number("appId");
    if(appId<=0||text("primaryProvider")!="steam"||text("identityStatus")!="verified"||text("steamVerificationStatus")!="steam-verified")return false;
    const auto version=text("resolverVersion");
    if(version!="steam-official-fuzzy-global-v4"&&version!="steam-official-fuzzy-global-v2")return false;
    if(match.contains("storefrontAppId")&&!match["storefrontAppId"].is_null()&&number("storefrontAppId")!=appId)return false;
    return number("matchedContentAppId")<=0 || (number("baseGameAppId")>0&&!text("contentRelation").empty()&&text("identityKind").find("with-explicit-content")!=std::string::npos);
}
} // namespace custom_steam_library
