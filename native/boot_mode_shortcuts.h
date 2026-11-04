#pragma once
#include "json.hpp"

namespace ymcc::bootModeShortcuts {
using json = nlohmann::json;
// Controller rules and calibration belong to ordinary global input only.
// Legacy mode overrides are deliberately ignored, including same-boot values.
inline const json* effectiveRules(const json& input) {
    const auto mapping = input.find("buttonMapping");
    if (mapping == input.end() || !mapping->is_object()) return nullptr;
    const auto container = mapping->find("rules");
    if (container == mapping->end() || !container->is_object()) return nullptr;
    const auto rules = container->find("shortcutRules");
    const auto initialized = container->find("shortcutRulesInitialized");
    const bool userOwned = initialized != container->end() && *initialized == true;
    if (rules == container->end() || !rules->is_array()) {
        // Damaged user-owned tables fail closed; explicit restore can repair
        // them, while first-run defaults remain reserved for unowned data.
        static const json empty = json::array();
        return userOwned ? &empty : nullptr;
    }
    // Match the editor: an old uninitialized empty table is first-run state.
    // Saving an intentionally empty table records the explicit marker.
    return !rules->empty() || userOwned ? &*rules : nullptr;
}
inline void stripControllerSnapshot(json& snapshot) {
    if (!snapshot.is_object()) return;
    for (const auto* key : {"controller", "buttonMapping", "axisCalibration", "controllerCalibration", "calibration"}) snapshot.erase(key);
    auto input = snapshot.find("input");
    if (input == snapshot.end() || !input->is_object()) return;
    for (const auto* key : {"controller", "buttonMapping", "axisCalibration", "controllerCalibration", "calibration", "bootModeShortcuts", "shortcutRules"}) input->erase(key);
    auto target = input->find("outputTarget");
    if (target != input->end() && target->is_object())
        for (const auto* key : {"controller", "buttonMapping", "axisCalibration", "controllerCalibration", "calibration", "shortcutRules"}) target->erase(key);
}
inline void retireGlobalSnapshot(json& document) {
    auto input = document.find("input");
    if (input != document.end() && input->is_object() && input->contains("bootModeShortcuts")) (*input)["bootModeShortcuts"] = nullptr;
    auto extensions = document.find("extensions");
    if (extensions == document.end() || !extensions->is_object()) return;
    extensions->erase("bootModeGlobalShortcutsBackup");
    auto modes = extensions->find("bootModes");
    if (modes == extensions->end() || !modes->is_object() || modes->value("schema", 0) != 1) return;
    stripControllerSnapshot(*modes);
    for (const auto* key : {"global", "activeSnapshot"}) {
        auto snapshot = modes->find(key); if (snapshot != modes->end()) stripControllerSnapshot(*snapshot);
    }
    auto stripScene = [](json& scene) {
        if (!scene.is_object()) return;
        for (const auto* key : {"overrides", "rememberedOverrides"}) {
            auto snapshot = scene.find(key); if (snapshot != scene.end()) stripControllerSnapshot(*snapshot);
        }
    };
    auto scenes = modes->find("scenes");
    if (scenes != modes->end() && scenes->is_object()) for (auto& scene : *scenes) stripScene(scene);
    auto presets = modes->find("presets");
    if (presets != modes->end() && presets->is_object()) for (auto& group : *presets)
        if (group.is_object()) for (auto& scene : group) stripScene(scene);
    modes->erase("controllerTemplates");
}
}
