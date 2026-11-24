#pragma once
#include "json.hpp"
#include <string>

namespace ymcc {
// Input configuration belongs to global settings and the four boot modes.
// Keep these entry points for existing callers; stored per-game overlays are inert.
inline bool gameInputOverrideActive(const nlohmann::json&, const std::string&) {
    return false;
}
inline const nlohmann::json& effectiveGameInputView(const nlohmann::json& input, const std::string&) {
    return input;
}
inline nlohmann::json effectiveGameInput(const nlohmann::json& input, const std::string& session) {
    return effectiveGameInputView(input, session);
}
}
