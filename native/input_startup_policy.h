#pragma once
#include "json.hpp"
#include <stdexcept>
#include <string>

namespace ymcc {
// Cold-start intent is independent of the input buttons' persisted live state.
inline nlohmann::json normalizeInputStartupPreferences(const nlohmann::json& raw) {
    std::string persona = "disabled", preset = "off";
    if (raw.is_object() && raw.contains("virtualGamepadPersona") && raw.at("virtualGamepadPersona").is_string()) {
        const auto value = raw.at("virtualGamepadPersona").get<std::string>();
        if (value == "steamdeck" || value == "dualsense-edge" || value == "elite") persona = value;
    }
    if (persona != "disabled" && raw.is_object() && raw.contains("gyroPreset") && raw.at("gyroPreset").is_string()) {
        const auto value = raw.at("gyroPreset").get<std::string>();
        if (value == "fps" || value == "racing" || value == "custom" || value == "steam") preset = value;
    }
    return {{"virtualGamepadPersona", persona}, {"gyroPreset", preset}};
}

// Same UI-snapshot -> durable-motion conversion as GyroMotionView.saveNow and
// gameInputOverride.gyroMotionParamsForPreset. Do not delete independent tuning.
inline nlohmann::json startupGyroPresetParams(const std::string& preset, const nlohmann::json& presets) {
    using nlohmann::json;
    json params;
    if (presets.is_object() && presets.contains(preset) && presets.at(preset).is_object()) {
        params = presets.at(preset);
    } else {
        params = {{"motionMode", "on"}, {"motionTrigger", ""}, {"innerDeadzone", 1},
                  {"antiDeadzone", 20}, {"autoCalibrate", true}};
        if (preset == "fps") {
            params.update(json{{"outputStick", "right"}, {"outputAxis", "xy"},
                               {"motionInput", "local-space"}, {"aimingSightsTrigger", "LT"}});
        } else if (preset == "racing") {
            params.update(json{{"outputStick", "left"}, {"outputAxis", "x"}, {"motionInput", "joystick-steering"}});
        } else if (preset == "steam") {
            params["motionInput"] = "local-space";
        }
    }
    const std::string mode = params.contains("motionMode") && params.at("motionMode").is_string()
        ? params.at("motionMode").get<std::string>() : "on";
    std::string trigger = params.contains("motionTrigger") && params.at("motionTrigger").is_string()
        ? params.at("motionTrigger").get<std::string>() : "";
    const auto start = trigger.find_first_not_of(" \t\r\n");
    trigger = start == std::string::npos ? "" : trigger.substr(start, trigger.find_last_not_of(" \t\r\n") - start + 1);
    params.erase("gyroMode");
    params["motionMode"] = mode == "suppress" ? "on" : mode;
    params["motionTrigger"] = mode == "on" || trigger.empty() ? json(nullptr) : json(trigger);
    return params;
}

// Pure transaction preparation: caller owns the settings mutex and durable write.
// Same-session calls are no-ops (renderer reload/wake must not replay boot intent).
inline nlohmann::json initializeInputStartup(const nlohmann::json& savedInput,
                                             const nlohmann::json& startup,
                                             const std::string& session) {
    using nlohmann::json;
    if (session.empty()) throw std::runtime_error("startup-input-session-empty");
    auto input = savedInput.is_object() ? savedInput : json::object();
    if (input.contains("startupAppliedSession") && input.at("startupAppliedSession") == session) return input;
    long long revision = 0;
    if (input.contains("revision")) {
        const auto& value = input.at("revision");
        if (!value.is_number_integer() || value < 0 || value >= 9007199254740991LL)
            throw std::runtime_error("startup-input-revision-invalid");
        revision = value.get<long long>();
    }
    const auto preferences = normalizeInputStartupPreferences(startup);
    const auto persona = preferences.at("virtualGamepadPersona").get<std::string>();
    const auto preset = preferences.at("gyroPreset").get<std::string>();
    const bool padEnabled = persona != "disabled", gyroEnabled = padEnabled && preset != "off";
    auto target = input.contains("outputTarget") && input.at("outputTarget").is_object()
        ? input.at("outputTarget") : json::object();
    auto motion = input.contains("gyroMotion") && input.at("gyroMotion").is_object()
        ? input.at("gyroMotion") : json::object();
    if (gyroEnabled) {
        const auto presets = motion.contains("presets") ? motion.at("presets") : json(nullptr);
        motion.update(startupGyroPresetParams(preset, presets));
        // A stored preset is parameter memory, never a second boot authority.
        if (!presets.is_null()) motion["presets"] = presets;
        motion["preset"] = preset;
        motion["activePreset"] = preset;
        motion["outputMode"] = "virtual-stick";
    }
    motion["enabled"] = gyroEnabled;
    motion.erase("gyroMode");
    target["persona"] = persona;
    target["buttonMappingEnabled"] = padEnabled;
    target["gyroEnabled"] = gyroEnabled;
    input["outputTarget"] = target;
    input["gyroMotion"] = motion;
    input["gameOverride"] = nullptr; // previous native session cannot own this baseline
    input["startupAppliedSession"] = session;
    input["schemaVersion"] = 1;
    input["revision"] = revision + 1;
    // Cold-start initialization is NOT a Host ACK/T10 proof. A later frontend CAS
    // computes its canonical hash; never keep stale applied/acknowledged claims.
    for (const auto* field : {"appliedRevision", "appliedConfigHash", "pendingConfigHash",
                              "hostAcknowledgedRevision", "hostAcknowledgedConfigHash"}) input[field] = nullptr;
    input["applyStatus"] = "unknown";
    return input;
}
}
