#pragma once
#include <array>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include "json.hpp"

namespace ymcc::logging {
inline constexpr char touchpadScopeDefaultsField[] = "touchpadScopeDefaultsV1";
inline constexpr char touchpadScopeEvent[] = "screen-touchpads-state";
inline constexpr char touchpadScopeCallSite[] = "screenTouchpads.get";
struct TouchpadScopeDefault {
    unsigned bit;
    const char* key;
    const char* child;
    const char* scope;
};
inline constexpr std::array<TouchpadScopeDefault, 4> touchpadScopeDefaults{{
    {1, "cursorVisibilityContext", nullptr, "Sequential current-window reads at first active Deck SHOWING/hidden pair only; WindowFromPoint is context, not hardware hit delivery, cursor owner/API caller, historical process identity, or an atomic snapshot"},
    {2, "cursorVisibilityObserver", "eventTrace", "WinEvent payload thread only; not hidden-API caller identity or a binding to later GetCursorInfo samples"},
    {4, "cursorVisibilityObserver", "sourceOwnerObservations", "First source-TID delivery per hook generation, owner queried on existing dispatch. TID reuse/delivery delay remain possible; later events referencing this observation do not prove unchanged ownership."},
    {8, "exitInputRelease", nullptr, "Touchpad sender-owned signals, controlled sources and original window handles only"},
}};

inline void compactTouchpadScopeDefaults(const char* event, nlohmann::json& detail) {
    if (!event || event[0] != 's' || std::string_view(event) != touchpadScopeEvent || !detail.is_object() ||
        detail.contains(touchpadScopeDefaultsField)) return;
    const auto site = detail.find("callSite"), state = detail.find("state");
    if (site == detail.end() || !site->is_string() || site->get_ref<const std::string&>() != touchpadScopeCallSite ||
        state == detail.end() || !state->is_object()) return;

    std::array<nlohmann::json*, touchpadScopeDefaults.size()> found{};
    unsigned mask = 0;
    for (std::size_t i = 0; i < touchpadScopeDefaults.size(); ++i) {
        const auto& field = touchpadScopeDefaults[i];
        const auto node = state->find(field.key);
        if (node == state->end() || !node->is_object()) continue;
        auto* target = &*node;
        if (field.child) {
            const auto child = target->find(field.child);
            if (child == target->end() || !child->is_object()) continue;
            target = &*child;
        }
        const auto scope = target->find("scope");
        if (scope == target->end() || !scope->is_string() || scope->get_ref<const std::string&>() != field.scope) continue;
        mask |= field.bit;
        found[i] = target;
    }
    if (!mask) return;
    try { detail.emplace(touchpadScopeDefaultsField, mask); }
    catch (const std::bad_alloc&) { return; }
    for (auto* target : found) if (target) target->erase(target->find("scope"));
}

inline nlohmann::json touchpadScopeDefaultsGuide() {
    auto defaults = nlohmann::json::array();
    for (const auto& field : touchpadScopeDefaults) {
        auto path = nlohmann::json::array({"state", field.key});
        if (field.child) path.push_back(field.child);
        path.push_back("scope");
        defaults.push_back({{"bit", field.bit}, {"path", std::move(path)}, {"value", field.scope}});
    }
    return {{"event", touchpadScopeEvent}, {"callSite", touchpadScopeCallSite}, {"field", touchpadScopeDefaultsField},
            {"defaults", std::move(defaults)},
            {"read", "Integer mask 1..15. Validate all selected parent objects and absence of each selected scope. Restore the exact selected text and remove the mask. On conflict keep the original row. No defaults on unmarked rows."}};
}

inline constexpr char touchpadCursorPolicyField[] = "touchpadCursorPolicyV1";
inline constexpr unsigned touchpadCursorPolicyVersion = 19;
inline constexpr const char* touchpadCursorFalseDefaults[] = {
    "cursorDeckSoftwareCursorEnabled", "cursorDeckForceVisibleEnabled", "cursorDeckContactShapeBridgeEnabled"
};
inline constexpr std::pair<const char*, unsigned> touchpadCursorIntegerDefaults[] = {
    {"cursorDeckForceVisibleVersion", 3}, {"cursorDeckContactShapeBridgeVersion", 2},
    {"cursorDeckMoveSubmitOrderVersion", 1}, {"cursorDeckMotionGateVersion", 1},
    {"cursorHiddenQueryBudgetVersion", 1}, {"cursorSubmitOrderVersion", 1}, {"cursorDeckHandoffGraceMs", 0}
};

inline void compactTouchpadCursorPolicy(const char* event, nlohmann::json& detail) {
    if (!event || event[0] != 's' || std::string_view(event) != touchpadScopeEvent || !detail.is_object() ||
        detail.contains(touchpadCursorPolicyField)) return;
    const auto site = detail.find("callSite"), state = detail.find("state");
    if (site == detail.end() || !site->is_string() || site->get_ref<const std::string&>() != touchpadScopeCallSite ||
        state == detail.end() || !state->is_object()) return;
    const auto integerMatches = [&](const char* key, unsigned value) {
        const auto it = state->find(key);
        return it != state->end() && it->is_number_integer() && *it == value;
    };
    if (!integerMatches("cursorPolicyVersion", touchpadCursorPolicyVersion)) return;
    for (const auto* key : touchpadCursorFalseDefaults) {
        const auto it = state->find(key);
        if (it == state->end() || !it->is_boolean() || *it != false) return;
    }
    for (const auto& [key, value] : touchpadCursorIntegerDefaults) if (!integerMatches(key, value)) return;
    try { detail.emplace(touchpadCursorPolicyField, 1); }
    catch (const std::bad_alloc&) { return; }
    for (const auto* key : touchpadCursorFalseDefaults) state->erase(state->find(key));
    for (const auto& [key, value] : touchpadCursorIntegerDefaults) state->erase(state->find(key));
}

inline nlohmann::json touchpadCursorPolicyGuide() {
    auto defaults = nlohmann::json::object();
    for (const auto* key : touchpadCursorFalseDefaults) defaults[key] = false;
    for (const auto& [key, value] : touchpadCursorIntegerDefaults) defaults[key] = value;
    return {{"event", touchpadScopeEvent}, {"callSite", touchpadScopeCallSite}, {"field", touchpadCursorPolicyField},
            {"versionInState", touchpadCursorPolicyVersion}, {"defaultsInState", std::move(defaults)},
            {"read", "For integer marker 1 and state.cursorPolicyVersion integer 19, restore all defaults only if all omitted field names are absent in state. Missing marker or conflict leaves the original state untouched."}};
}
} // namespace ymcc::logging
