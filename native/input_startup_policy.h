#pragma once
#include "controller_persona_settings.h"
#include <stdexcept>
#include <string>

namespace ymcc {
// Cold-start intent is independent of the input buttons' persisted live state.
inline nlohmann::json normalizeInputStartupPreferences(const nlohmann::json& raw) {
    std::string persona = "disabled", preset = "off";
    if (raw.is_object() && raw.contains("virtualGamepadPersona") && raw.at("virtualGamepadPersona").is_string()) {
        const auto value = raw.at("virtualGamepadPersona").get<std::string>();
        persona = controllerPersona::saved(value);
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
                                             const std::string& session,
                                             bool applyPreferences = true) {
    using nlohmann::json;
    if (session.empty()) throw std::runtime_error("startup-input-session-empty");
    auto input = savedInput.is_object() ? savedInput : json::object();
    controllerPersona::migrateSavedInput(input);
    if (input.contains("startupAppliedSession") && input.at("startupAppliedSession") == session) return input;
    long long revision = 0;
    if (input.contains("revision")) {
        const auto& value = input.at("revision");
        if (!value.is_number_integer() || value < 0 || value >= 9007199254740991LL)
            throw std::runtime_error("startup-input-revision-invalid");
        revision = value.get<long long>();
    }
    if (applyPreferences) {
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
        if (!target.contains("persona") || target["persona"] != persona) target["descriptorHash"] = nullptr;
        target["persona"] = persona;
        target["buttonMappingEnabled"] = padEnabled;
        target["gyroEnabled"] = gyroEnabled;
        input["outputTarget"] = target;
        input["gyroMotion"] = motion;
    }
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

// The process identity still retires dead game/Host owners. Only a new confirmed
// Windows boot may consume the next scene's input preferences. Never infer that
// boundary from a PID, wall clock, renderer reload, wake, or unknown identity.
inline nlohmann::json initializeBootModeInputStartup(nlohmann::json document,
                                                    const std::string& session,
                                                    const std::string& boot) {
    using nlohmann::json;
    if (!document.is_object()) throw std::runtime_error("startup-settings-invalid");
    const auto input = document.value("input", json::object());
    const auto startup = document.value("startupDesired", json::object());
    auto extensions = document.value("extensions", json::object());
    if (!extensions.is_object()) extensions = json::object();
    const auto modes = extensions.value("bootModes", json::object());
    const bool configured = modes.is_object() && modes.value("schema", json(nullptr)) == 1;
    const auto previousBoot = input.is_object() ? input.value("startupAppliedBoot", json(nullptr)) : json(nullptr);
    const auto previousSession = input.is_object() ? input.value("startupAppliedSession", json(nullptr)) : json(nullptr);
    // An explicit scene save records the Windows boot where it was selected.
    // That receipt is already a boundary: the next boot must consume its input
    // even if an older native build never wrote input.startupAppliedBoot.
    auto selectionBoot = extensions.value("bootModesAppliedBoot", json(nullptr));
    // A v1 loader tuple cannot prove which boot an old draft belongs to. Anchor
    // once on upgrade, preserving live input. Explicit selection in this build
    // records v2; the very next reboot must consume that configured preset.
    const bool identityUpgrade = configured && boot.rfind("windows-volatile-v2|", 0) == 0 &&
        selectionBoot.is_string() && selectionBoot.get<std::string>().rfind("windows-loader-v1|", 0) == 0;
    if (identityUpgrade) {
        extensions["bootModesIdentityMigration"] = {{"from", selectionBoot}, {"to", boot}, {"policy", "preserve-current-until-next-boot"}};
        extensions["bootModesAppliedBoot"] = boot;
        selectionBoot = boot;
    }
    const bool haveSelectionBoot = selectionBoot.is_string() && !selectionBoot.get<std::string>().empty();
    // Anchor only truly unobserved legacy installs, not a known next-boot choice.
    const bool migration = configured && !haveSelectionBoot && (!previousBoot.is_string() || previousBoot.get<std::string>().empty()) && previousSession.is_string() &&
        !previousSession.get<std::string>().empty();
    const bool claimed = !boot.empty() && extensions.value("bootModesAppliedBoot", json(nullptr)) == boot;
    const bool preserve = configured && (boot.empty() || migration || previousBoot == boot || claimed);
    auto next = initializeInputStartup(input, startup, session, !preserve);
    if (!boot.empty()) next["startupAppliedBoot"] = boot;
    // Unknown observations must not make a previous boot's result look current.
    next["startupObservedBoot"] = boot.empty() ? json(nullptr) : json(boot);
    document["input"] = std::move(next);
    if ((migration || identityUpgrade) && !boot.empty()) {
        extensions["bootModesAppliedBoot"] = boot;
        document["extensions"] = std::move(extensions);
    }
    return document;
}
}
