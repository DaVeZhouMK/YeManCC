#pragma once
#include "json.hpp"

namespace ymcc::bootModeShortcuts {
using json = nlohmann::json;
// A mode never replaces the ordinary global array. Only its applied override
// is eligible, and only in the Windows boot in which it was applied.
inline const json* effectiveRules(const json& input) {
    const auto own = input.find("bootModeShortcuts");
    if (own != input.end() && own->is_object() && own->value("schema", 0) == 1) {
        const auto mode = own->find("mode"), rules = own->find("rules"), boot = own->find("boot");
        const auto observed = input.find("startupObservedBoot");
        const std::string currentBoot = observed != input.end() && observed->is_string() ? observed->get<std::string>() : "";
        if (mode != own->end() && mode->is_string() &&
            (*mode == "xbox" || *mode == "steam" || *mode == "xbox-steam") &&
            rules != own->end() && rules->is_array() && boot != own->end() && boot->is_string() && *boot == currentBoot)
            return &*rules;
    }
    const auto mapping = input.find("buttonMapping");
    if (mapping == input.end() || !mapping->is_object()) return nullptr;
    const auto container = mapping->find("rules");
    if (container == mapping->end() || !container->is_object()) return nullptr;
    const auto rules = container->find("shortcutRules");
    return rules != container->end() && rules->is_array() ? &*rules : nullptr;
}
inline void retireGlobalSnapshot(json& document) {
    auto extensions = document.find("extensions");
    if (extensions == document.end() || !extensions->is_object()) return;
    auto modes = extensions->find("bootModes");
    if (modes == extensions->end() || !modes->is_object() || modes->value("schema", 0) != 1) return;
    auto global = modes->find("global");
    if (global == modes->end() || !global->is_object()) return;
    const auto controller = global->find("controller");
    if (controller == global->end()) return;
    if (!extensions->contains("bootModeGlobalShortcutsBackup"))
        (*extensions)["bootModeGlobalShortcutsBackup"] = {{"schema", 1}, {"revision", modes->value("revision", 0LL)}, {"controller", *controller}};
    global->erase("controller");
}
}
