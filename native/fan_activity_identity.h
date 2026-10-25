#pragma once
#include <string>
#include "json.hpp"
namespace ymcc::fan_activity {
// Called only at the existing Fan response observation boundary. A response
// declares deviceClass, or the actual Host state declares its factoryType.
// Missing/malformed/conflicting identities retain the current refusal rule.
// The returned pointer is owned by reply and must not outlive it.
inline const std::string* deviceClassReferenceFromReply(const nlohmann::json& reply) {
    if (!reply.is_object()) return nullptr;
    const std::string* selected = nullptr;
    auto read = [&](const nlohmann::json& value, const char* key) {
        const auto it = value.find(key);
        if (it == value.end() || it->is_null()) return true;
        if (!it->is_string()) return false;
        const auto& candidate = it->get_ref<const std::string&>();
        if (candidate.empty()) return true;
        if (candidate.size() > 256 || candidate.rfind("HandheldCompanion.Devices.", 0) != 0
            || (selected && candidate != *selected)) return false;
        selected = &candidate;
        return true;
    };
    if (!read(reply, "deviceClass") || !read(reply, "factoryType")) return nullptr;
    const auto state = reply.find("state");
    if (state != reply.end() && state->is_object()) {
        if (!read(*state, "deviceClass") || !read(*state, "factoryType")) return nullptr;
    }
    return selected;
}
// Compatibility callers receive their own independent copy.
inline std::string deviceClassFromReply(const nlohmann::json& reply) {
    const auto* selected = deviceClassReferenceFromReply(reply);
    return selected ? *selected : std::string{};
}
}
