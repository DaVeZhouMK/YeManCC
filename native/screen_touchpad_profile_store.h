#pragma once
#include "screen_touchpads.h"

namespace ymcc::screenpads {
// Staging is pure: inactive persona settings are not a request to create an
// output device. The caller commits this entire bank once, then applies only
// the current persona through the existing runtime capability gates.
struct ProfileSavePlan {
    ProfileBank bank;
    bool ok = false;
    unsigned changedMask = 0;
    std::string persona, reason;
    explicit ProfileSavePlan(const Json& saved) : bank(saved) {}
};
inline ProfileSavePlan stageProfileSave(const Json& saved, const Json& patches, const Json& expected) {
    ProfileSavePlan plan(saved);
    if (!patches.is_object() || patches.empty() || patches.size() > 4 || !expected.is_object()) {
        plan.reason = "invalid-profile-batch"; return plan;
    }
    for (const auto& [name, patch] : patches.items()) {
        plan.persona = name;
        const int index = profileIndex(name);
        if (index < 0 || index >= 4) { plan.reason = "invalid-persona"; return plan; }
        if (!patch.is_object() || patch.empty() || patch.size() > 32 || !expected.contains(name) || !expected[name].is_object() || expected[name].size() > 40) {
            plan.reason = "profile-baseline-missing"; return plan;
        }
        const auto current = configJson(plan.bank.slots[index]);
        for (const auto& [key, value] : patch.items()) {
            if (!current.contains(key) || !expected[name].contains(key) || expected[name][key] != current[key]) {
                plan.reason = "profile-settings-conflict"; return plan;
            }
        }
        Config next;
        if (!parseConfig(patch, plan.bank.slots[index], next)) { plan.reason = "invalid-settings"; return plan; }
        const bool deck = (patch.contains("leftMode") && next.left == Mode::Deck) ||
            (patch.contains("rightMode") && next.right == Mode::Deck);
        const bool ps = (patch.contains("singleMode") && next.single == Mode::DualSense) ||
            (patch.contains("leftMode") && next.left == Mode::DualSense) ||
            (patch.contains("rightMode") && next.right == Mode::DualSense);
        if ((deck && index != 1) || (ps && index != 2) ||
            (effectiveSpecialMask(next) != 0 && index == 0) ||
            (effectiveRearMask(next) != 0 && index != 1 && index != 2)) {
            plan.reason = "persona-mapping-not-supported"; return plan;
        }
        if (configJson(next) != current) plan.changedMask |= 1u << index;
        plan.bank.slots[index] = next;
    }
    plan.ok = true; plan.persona.clear(); return plan;
}
}
