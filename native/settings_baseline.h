#pragma once
#include "json.hpp"
#include <cmath>

// Constants are checked against PowerControl/settings-baseline.json at build time.
namespace ymcc::settingsBaseline {
using Json = nlohmann::json;
inline constexpr const char* kId = "ymcc-settings";
inline constexpr long long kRevision = 1;
inline bool integer(const Json& value) {
    if (!value.is_number()) return false;
    const auto number = value.get<double>();
    return std::isfinite(number) && std::floor(number) == number;
}
inline bool supported(const Json& document) {
    if (!document.is_object()) return false;
    const auto schema = document.find("schemaVersion");
    if (schema != document.end()) {
        if (!integer(*schema) || *schema < 0 || *schema > 1) return false;
    }
    const auto stamp = document.find("configurationBaseline");
    if (stamp == document.end()) return true;
    if (!stamp->is_object()) return false;
    const auto id = stamp->find("id"), revision = stamp->find("revision"), imported = stamp->find("legacyImported");
    return id != stamp->end() && *id == kId && revision != stamp->end() && integer(*revision) &&
        *revision >= 1 && *revision <= kRevision && imported != stamp->end() && *imported == true;
}
inline void preserveReceipt(Json& next, const Json& current) {
    if (current.is_object() && current.contains("configurationBaseline") && !next.contains("configurationBaseline"))
        next["configurationBaseline"] = current["configurationBaseline"];
}
}
