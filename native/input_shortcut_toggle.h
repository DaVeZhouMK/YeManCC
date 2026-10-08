#pragma once
#include "game_input_override.h"
#include <string>
#include <utility>

namespace ymcc {
using InputShortcutJson = nlohmann::json;
inline bool inputShortcutGyroPreset(const std::string& preset) {
    return preset == "fps" || preset == "racing" || preset == "custom" || preset == "steam";
}
inline bool inputShortcutPadPreset(const std::string& preset) {
    // Match the reachable controller-page personas; do not re-expose legacy targets.
    return preset == "steamdeck" || preset == "dualsense-edge" || preset == "elite";
}
inline bool inputShortcutAction(const std::string& action) {
    return action == "input.gyroToggle" || action == "input.virtualGamepadToggle";
}
inline bool inputShortcutPreset(const std::string& action, const std::string& preset) {
    return (action == "input.gyroToggle" && inputShortcutGyroPreset(preset)) ||
        (action == "input.virtualGamepadToggle" && inputShortcutPadPreset(preset));
}
inline InputShortcutJson inputShortcutObject(const InputShortcutJson& input, const char* key) {
    const auto item = input.is_object() ? input.find(key) : input.end();
    return item != input.end() && item->is_object() ? *item : InputShortcutJson::object();
}
inline bool inputShortcutBool(const InputShortcutJson& input, const char* key, bool fallback = false) {
    const auto item = input.is_object() ? input.find(key) : input.end();
    return item != input.end() && item->is_boolean() ? item->get<bool>() : fallback;
}
inline std::string inputShortcutString(const InputShortcutJson& input, const char* key, const char* fallback = "") {
    const auto item = input.is_object() ? input.find(key) : input.end();
    return item != input.end() && item->is_string() ? item->get<std::string>() : fallback;
}
// A rule-only revision/ACK is not a new owner. Explicit target/motion edits,
// game A -> B, dedicated-profile changes, or exit to global clear the temporary switch.
inline InputShortcutJson inputShortcutContext(const InputShortcutJson& input, const std::string& session) {
    const auto base = effectiveGameInput(input, session);
    auto target = inputShortcutObject(base, "outputTarget");
    auto motion = inputShortcutObject(base, "gyroMotion");
    target.erase("revision"); motion.erase("revision");
    return {{"session", session}, {"owner", gameInputOverrideActive(input, session)
        ? input.at("gameOverride").at("identity").get<std::string>() : "global"},
        {"outputTarget", target}, {"gyroMotion", motion}};
}
inline InputShortcutJson inputShortcutGyroParams(const InputShortcutJson& motion, const std::string& preset) {
    auto presets = inputShortcutObject(motion, "presets");
    auto stored = presets.find(preset);
    auto params = stored != presets.end() && stored->is_object() ? *stored : InputShortcutJson::object();
    if (stored == presets.end() || !stored->is_object()) {
        // Same defining defaults as the gyro page and dedicated-game overlay.
        params = {{"motionMode", "on"}, {"motionTrigger", ""}, {"innerDeadzone", 1},
            {"antiDeadzone", 20}, {"autoCalibrate", true}};
        if (preset == "fps") {
            params["outputStick"] = "right"; params["outputAxis"] = "xy";
            params["motionInput"] = "local-space"; params["aimingSightsTrigger"] = "LT";
        } else if (preset == "racing") {
            params["outputStick"] = "left"; params["outputAxis"] = "x";
            params["motionInput"] = "joystick-steering";
        } else if (preset == "steam") params["motionInput"] = "local-space";
    }
    const auto mode = inputShortcutString(params, "motionMode", "on");
    const auto trigger = inputShortcutString(params, "motionTrigger");
    params.erase("gyroMode");
    params["motionMode"] = mode == "suppress" ? "on" : mode;
    params["motionTrigger"] = mode == "on" || trigger.empty() ? InputShortcutJson(nullptr) : InputShortcutJson(trigger);
    return params;
}
struct InputShortcutToggleResult {
    bool ok = false;
    bool enabled = false;
    std::string reason;
    InputShortcutJson effective = InputShortcutJson::object();
};
// In-process intent only. Never writes settings, remembered presets, startup
// intent, or gameOverride. The host's existing neutral/persona transition path
// consumes effective(), exactly as it consumes the dedicated-game overlay.
class InputShortcutRuntime {
    InputShortcutJson context_ = nullptr;
    InputShortcutJson target_ = nullptr;
    InputShortcutJson motion_ = nullptr;
public:
    bool active() const { return !context_.is_null(); }
    void clear() { context_ = nullptr; target_ = nullptr; motion_ = nullptr; }
    InputShortcutJson effective(const InputShortcutJson& input, const std::string& session) {
        auto result = effectiveGameInput(input, session);
        if (!context_.is_null()) {
            if (context_ != inputShortcutContext(input, session)) clear();
            else {
                result["outputTarget"] = target_;
                result["gyroMotion"] = motion_;
            }
        }
        return result;
    }
    InputShortcutToggleResult toggle(const InputShortcutJson& input, const std::string& session,
        const std::string& action, const std::string& preset, const InputShortcutJson& expectedContext = nullptr) {
        if (!inputShortcutPreset(action, preset)) return {false, false, "invalid-action-or-preset", {}};
        const auto context = inputShortcutContext(input, session);
        // A queued key from an old game/configuration must not switch the next owner.
        if (!expectedContext.is_null() && context != expectedContext) return {false, false, "owner-or-config-changed", {}};
        auto current = effective(input, session);
        auto target = inputShortcutObject(current, "outputTarget");
        auto motion = inputShortcutObject(current, "gyroMotion");
        bool enable = false;
        if (action == "input.gyroToggle") {
            enable = !(inputShortcutBool(target, "gyroEnabled") && inputShortcutBool(motion, "enabled"));
            if (enable) {
                motion.update(inputShortcutGyroParams(motion, preset));
                motion["preset"] = preset; motion["activePreset"] = preset;
                motion["outputMode"] = "virtual-stick";
            }
            target["gyroEnabled"] = enable; motion["enabled"] = enable;
        } else {
            enable = !(inputShortcutString(target, "persona", "disabled") != "disabled" &&
                (inputShortcutBool(target, "buttonMappingEnabled") || inputShortcutBool(target, "gyroEnabled")));
            target["persona"] = enable ? preset : "disabled";
            target["buttonMappingEnabled"] = enable;
            // Closing the target always stops gyro, matching the controller page.
            // Opening respects the user's existing virtualPadLink preference.
            if (!enable || inputShortcutBool(motion, "virtualPadLink", true)) {
                target["gyroEnabled"] = enable; motion["enabled"] = enable;
                if (enable) motion["outputMode"] = "virtual-stick";
            }
        }
        context_ = context; target_ = std::move(target); motion_ = std::move(motion);
        current["outputTarget"] = target_; current["gyroMotion"] = motion_;
        return {true, enable, "runtime-only", std::move(current)};
    }
};
}
