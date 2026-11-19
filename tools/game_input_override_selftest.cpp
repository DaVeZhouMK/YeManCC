// Pure configuration/runtime selftest. No IPC, hardware or user settings access.
#include "../native/input_shortcut_toggle.h"
#include "../native/virtual_recovery_intent.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using nlohmann::json;
int main() {
    int passed = 0;
    auto check = [&](bool condition, const std::string& name) {
        if (!condition) throw std::runtime_error(name);
        ++passed; std::cout << "PASS " << name << '\n';
    };
    try {
        const json base = {
            {"revision", 7},
            {"outputTarget", {{"persona", "disabled"}, {"buttonMappingEnabled", false},
                {"gyroEnabled", false}, {"futureTarget", 42}}},
            {"gyroMotion", {{"enabled", false}, {"preset", "custom"}, {"activePreset", "custom"},
                {"virtualPadLink", true}, {"gyroMultiplier", 2}, {"futureMotion", 81},
                {"presets", {{"fps", {{"motionMode", "suppress"}, {"motionTrigger", "LT"}, {"gyroMultiplier", 1.1}}},
                    {"racing", {{"motionMode", "on"}, {"gyroMultiplier", 1.8}}},
                    {"custom", {{"motionMode", "toggle"}, {"motionTrigger", "RT"}, {"gyroMultiplier", 2.3}}},
                    {"steam", {{"motionMode", "on"}, {"gyroMultiplier", 1.4}}}}}}},
            {"futureInput", {{"keep", true}}}
        };
        const json overlay = {
            {"identity", "101:1001:a.exe"}, {"sessionId", "owner-session"},
            {"outputTarget", {{"persona", "steamdeck"}, {"buttonMappingEnabled", true}, {"gyroEnabled", true}}},
            {"gyroMotion", {{"enabled", true}, {"gyroMultiplier", 99},
                {"presets", {{"fps", {{"gyroMultiplier", 99}}}}}}}
        };
        std::vector<json> overlays = {overlay, nullptr, false, 42, "legacy", json::array({overlay}), json::object()};
        auto malformed = overlay; malformed["identity"] = ""; overlays.push_back(malformed);
        malformed = overlay; malformed["gyroMotion"] = false; overlays.push_back(malformed);
        malformed = overlay; malformed["sessionId"] = ""; overlays.push_back(malformed);
        malformed = overlay; malformed["sessionId"] = 42; overlays.push_back(malformed);
        malformed = overlay; malformed["outputTarget"] = json::array(); overlays.push_back(malformed);

        for (const auto& persona : {"disabled", "steamdeck", "dualsense-edge", "elite"}) {
            auto global = base;
            global["outputTarget"]["persona"] = persona;
            global["outputTarget"]["buttonMappingEnabled"] = std::string(persona) != "disabled";
            check(ymcc::effectiveGameInput(global, "owner-session") == global,
                std::string(persona) + " base configuration passes through unchanged");
            ymcc::inputHost::VirtualRecoveryIntent recovery;
            recovery.configure(global, "owner-session", true, std::string(persona) != "disabled", persona);
            const auto revision = recovery.revision();
            unsigned index = 0;
            for (const auto& legacy : overlays) {
                auto input = global; input["gameOverride"] = legacy;
                const auto frozen = input.dump();
                for (const auto& session : {"owner-session", "restarted-owner", ""}) {
                    const auto effective = ymcc::effectiveGameInput(input, session);
                    check(!ymcc::gameInputOverrideActive(input, session) && effective == input && input.dump() == frozen,
                        std::string(persona) + " ignores overlay " + std::to_string(index) + " in session '" + session + "'");
                    check(effective["outputTarget"] == global["outputTarget"] && effective["gyroMotion"] == global["gyroMotion"],
                        "global persona, gyro and preset memory remain exact");
                    check(ymcc::inputShortcutContext(input, session) == ymcc::inputShortcutContext(global, session),
                        "legacy metadata never owns shortcut context");
                    recovery.configure(effective, session, true, std::string(persona) != "disabled", persona);
                    check(recovery.revision() == revision, "legacy metadata never renews recovery intent");
                }
                ++index;
            }
            auto edited = global; edited["gyroMotion"]["gyroMultiplier"] = 3.1;
            recovery.configure(edited, "owner-session", true, std::string(persona) != "disabled", persona);
            check(recovery.revision() != revision, "real global motion edits still renew recovery intent");
        }
        for (const auto& invalid : {json::array(), json(nullptr), json(false), json(42), json("legacy")}) {
            check(!ymcc::gameInputOverrideActive(invalid, "owner-session") &&
                ymcc::effectiveGameInput(invalid, "owner-session") == invalid, "non-object settings pass through without overlay access");
        }

        for (const auto& preset : {"fps", "racing", "custom", "steam"}) {
            auto input = base; input["gameOverride"] = overlay;
            const auto frozen = input.dump(); ymcc::InputShortcutRuntime runtime;
            const auto on = runtime.toggle(input, "owner-session", "input.gyroToggle", preset);
            check(on.ok && on.enabled && on.effective["gyroMotion"]["enabled"] == true &&
                on.effective["outputTarget"]["gyroEnabled"] == true && on.effective["gyroMotion"]["preset"] == preset &&
                on.effective["gyroMotion"]["gyroMultiplier"] == base["gyroMotion"]["presets"][preset]["gyroMultiplier"],
                std::string(preset) + " temporary gyro enable uses the global preset bank");
            auto changed = input; changed["gameOverride"] = {{"identity", "next-game"}, {"sessionId", "owner-session"}};
            check(runtime.effective(changed, "owner-session")["gyroMotion"] == on.effective["gyroMotion"] && runtime.active(),
                "changing game metadata preserves the live temporary toggle");
            changed.erase("gameOverride");
            check(runtime.effective(changed, "owner-session")["gyroMotion"] == on.effective["gyroMotion"],
                "removing game metadata preserves the live temporary toggle");
            const auto off = runtime.toggle(input, "owner-session", "input.gyroToggle", "steam");
            check(off.ok && !off.enabled && off.effective["gyroMotion"]["preset"] == preset &&
                off.effective["gyroMotion"]["enabled"] == false && off.effective["outputTarget"]["gyroEnabled"] == false &&
                off.effective["gyroMotion"]["presets"] == base["gyroMotion"]["presets"] && input.dump() == frozen,
                "temporary gyro disable preserves selected preset, durable settings and definitions");
        }
        for (const auto& persona : {"steamdeck", "dualsense-edge", "elite"}) {
            auto input = base; input["gameOverride"] = overlay;
            const auto frozen = input.dump(); ymcc::InputShortcutRuntime runtime;
            const auto on = runtime.toggle(input, "owner-session", "input.virtualGamepadToggle", persona);
            check(on.ok && on.enabled && on.effective["outputTarget"]["persona"] == persona &&
                on.effective["outputTarget"]["buttonMappingEnabled"] == true && on.effective["gyroMotion"]["enabled"] == true &&
                on.effective["gyroMotion"]["gyroMultiplier"] == 2.3 && on.effective["gyroMotion"]["activePreset"] == "custom",
                std::string(persona) + " temporary target enable links the selected global gyro preset");
            input["gameOverride"]["identity"] = "next-game";
            const auto off = runtime.toggle(input, "owner-session", "input.virtualGamepadToggle", "elite");
            check(off.ok && !off.enabled && off.effective["outputTarget"]["persona"] == "disabled" &&
                off.effective["outputTarget"]["buttonMappingEnabled"] == false && off.effective["gyroMotion"]["enabled"] == false,
                "temporary target disable survives metadata changes and stops linked gyro");
            input["gameOverride"]["identity"] = overlay["identity"];
            check(input.dump() == frozen, "temporary target switches never write durable settings");
        }
        auto input = base; input["gameOverride"] = overlay;
        ymcc::InputShortcutRuntime runtime;
        const auto queued = ymcc::inputShortcutContext(input, "owner-session");
        input["gameOverride"] = false;
        check(runtime.toggle(input, "owner-session", "input.gyroToggle", "fps", queued).ok,
            "queued global shortcut remains valid after legacy metadata changes");
        input["revision"] = 8; input["gyroMotion"]["revision"] = 8; input["outputTarget"]["revision"] = 8;
        check(runtime.effective(input, "owner-session")["gyroMotion"]["enabled"] == true,
            "revision-only acknowledgements preserve transient state");
        check(!runtime.toggle(input, "restarted-owner", "input.gyroToggle", "fps", queued).ok,
            "queued shortcut still rejects a changed native session");
        check(runtime.effective(input, "restarted-owner")["gyroMotion"]["enabled"] == false && !runtime.active(),
            "native session change clears transient state");
        runtime.toggle(input, "owner-session", "input.gyroToggle", "fps");
        input["gyroMotion"]["gyroMultiplier"] = 3.1;
        check(!runtime.toggle(input, "owner-session", "input.gyroToggle", "fps", queued).ok &&
            runtime.effective(input, "owner-session")["gyroMotion"]["gyroMultiplier"] == 3.1 && !runtime.active(),
            "explicit global motion edit rejects stale queue and clears transient state");
        runtime.toggle(input, "owner-session", "input.gyroToggle", "fps");
        check(ymcc::InputShortcutRuntime{}.effective(input, "owner-session") == input,
            "new runtime never restores temporary state from metadata");
        runtime.clear(); input["gyroMotion"]["virtualPadLink"] = false;
        check(runtime.toggle(input, "owner-session", "input.virtualGamepadToggle", "elite").effective["gyroMotion"]["enabled"] == false,
            "temporary target enable retains the global linkage-disabled preference");
        check(!runtime.toggle(input, "owner-session", "input.gyroToggle", "bogus").ok &&
            !runtime.toggle(input, "owner-session", "input.virtualGamepadToggle", "disabled").ok,
            "invalid temporary presets remain rejected");
        std::cout << "passed: " << passed << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
}