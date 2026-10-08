#pragma once
#include "json.hpp"
#include <string>

namespace ymcc {
// The stored overlay is valid only in its native owner session. A crash/restart
// cannot make last game's transient target become the next run's global target.
inline bool gameInputOverrideActive(const nlohmann::json& input, const std::string& session) {
    if (session.empty() || !input.is_object() || !input.contains("gameOverride")) return false;
    const auto& overlay = input.at("gameOverride");
    return overlay.is_object() && overlay.contains("sessionId") && overlay.at("sessionId").is_string() &&
        overlay.at("sessionId").get<std::string>() == session &&
        overlay.contains("identity") && overlay.at("identity").is_string() &&
        !overlay.at("identity").get<std::string>().empty() &&
        overlay.contains("outputTarget") && overlay.at("outputTarget").is_object() &&
        overlay.contains("gyroMotion") && overlay.at("gyroMotion").is_object();
}
inline nlohmann::json effectiveGameInput(const nlohmann::json& input, const std::string& session) {
    auto effective = input;
    if (gameInputOverrideActive(input, session)) {
        effective["outputTarget"] = input.at("gameOverride").at("outputTarget");
        effective["gyroMotion"] = input.at("gameOverride").at("gyroMotion");
    }
    return effective;
}
}
