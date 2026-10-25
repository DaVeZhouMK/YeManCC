#pragma once
#include "controller_persona.h"
#include "json.hpp"

namespace ymcc::controllerPersona {
using Json = nlohmann::json;
// Migrate existing fields only; never turn on output or replace unrelated data.
inline bool migrateSavedField(Json& object, const char* key, bool allowFollow = false) {
    if (!object.is_object()) return false;
    auto field = object.find(key);
    if (field == object.end() || (allowFollow && *field == "follow")) return false;
    const std::string value = field->is_string() ? field->get<std::string>() : std::string{};
    const char* next = allowFollow ? savedOverride(value) : saved(value);
    if (*field == next) return false;
    *field = next;
    return true;
}
inline bool migrateSavedTarget(Json& target) {
    if (!migrateSavedField(target, "persona")) return false;
    // The old descriptor hash cannot prove the replacement identity.
    target["descriptorHash"] = nullptr;
    target["closure"] = "UNENCLOSED";
    return true;
}
inline bool migrateSavedShortcuts(Json& rules) {
    bool changed = false;
    if (!rules.is_array()) return changed;
    for (auto& rule : rules) {
        if (!rule.is_object() || !rule.contains("actionId") || rule["actionId"] != "input.virtualGamepadToggle" ||
            !rule.contains("params")) continue;
        changed = migrateSavedField(rule["params"], "preset") || changed;
    }
    return changed;
}
inline bool migrateSavedInput(Json& input) {
    if (!input.is_object()) return false;
    bool identityChanged = input.contains("outputTarget") && migrateSavedTarget(input["outputTarget"]);
    if (input.contains("gameOverride") && input["gameOverride"].is_object()) {
        auto& overlay = input["gameOverride"];
        if (overlay.contains("outputTarget")) identityChanged = migrateSavedTarget(overlay["outputTarget"]) || identityChanged;
        if (overlay.contains("baseline") && overlay["baseline"].is_object() && overlay["baseline"].contains("outputTarget"))
            identityChanged = migrateSavedTarget(overlay["baseline"]["outputTarget"]) || identityChanged;
    }
    // Receipts describe the effective target identity, including overlays and
    // their saved baseline. Preset memory alone cannot invalidate that identity.
    if (identityChanged) {
        for (const char* field : {"appliedRevision", "appliedConfigHash", "pendingConfigHash",
                "hostAcknowledgedRevision", "hostAcknowledgedConfigHash"}) input[field] = nullptr;
        input["applyStatus"] = "saved-pending";
    }
    bool changed = identityChanged;
    if (input.contains("buttonMapping") && input["buttonMapping"].is_object()) {
        auto& mapping = input["buttonMapping"];
        if (mapping.contains("rules") && mapping["rules"].is_object() && mapping["rules"].contains("shortcutRules"))
            changed = migrateSavedShortcuts(mapping["rules"]["shortcutRules"]) || changed;
    }
    return changed;
}
inline bool migrateSavedGameCustom(Json& custom) {
    if (!custom.is_object()) return false;
    bool changed = false;
    if (custom.contains("inputRestore")) changed = migrateSavedTarget(custom["inputRestore"]) || changed;
    if (custom.contains("entries") && custom["entries"].is_object()) {
        for (auto& entry : custom["entries"]) {
            changed = migrateSavedField(entry, "padPersona", true) || changed;
            if (entry.is_object() && entry.contains("inputOverrides") && entry["inputOverrides"].is_object() &&
                entry["inputOverrides"].contains("outputTarget"))
                changed = migrateSavedTarget(entry["inputOverrides"]["outputTarget"]) || changed;
        }
    }
    return changed;
}
// Current saved values win. Copy all missing fields before retiring old keys.
inline bool fillMissingSavedFields(Json& next, const Json& old) {
    if (!next.is_object() || !old.is_object()) return false;
    bool changed = false;
    for (auto it = old.begin(); it != old.end(); ++it) {
        if (!next.contains(it.key())) { next[it.key()] = it.value(); changed = true; }
        else if (next[it.key()].is_object() && it.value().is_object())
            changed = fillMissingSavedFields(next[it.key()], it.value()) || changed;
    }
    return changed;
}
inline bool migrateSavedScreenProfiles(Json& section) {
    if (!section.is_object() || !section.contains("profiles") || !section["profiles"].is_object()) return false;
    auto& profiles = section["profiles"];
    bool changed = false;
    for (const char* retired : {"xbox360", "dualsense", "dualshock4"}) {
        if (!profiles.contains(retired) || !profiles[retired].is_object()) continue;
        const char* next = saved(retired);
        if (!profiles.contains(next) || !profiles[next].is_object()) profiles[next] = profiles[retired];
        else fillMissingSavedFields(profiles[next], profiles[retired]);
        profiles.erase(retired);
        changed = true;
    }
    return changed;
}
inline bool migrateSavedDocument(Json& document) {
    if (!document.is_object()) return false;
    bool changed = false;
    if (document.contains("input")) changed = migrateSavedInput(document["input"]) || changed;
    if (document.contains("startupDesired")) changed = migrateSavedField(document["startupDesired"], "virtualGamepadPersona") || changed;
    if (document.contains("gameCustom")) changed = migrateSavedGameCustom(document["gameCustom"]) || changed;
    if (document.contains("screenTouchpads")) changed = migrateSavedScreenProfiles(document["screenTouchpads"]) || changed;
    return changed;
}
} // namespace ymcc::controllerPersona
