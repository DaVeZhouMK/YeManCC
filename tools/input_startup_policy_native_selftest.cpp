// Pure JSON-only harness: no Windows APIs, devices, services or product runtime.
#include "input_startup_policy.h"
#include <iostream>
#include <functional>
#include <vector>
#include <fstream>
using nlohmann::json;
static void expect(bool value) { if (!value) throw std::runtime_error("assertion-failed"); }
int main(int argc, char** argv) {
    if (argc == 5 && std::string(argv[1]) == "--restart-document") {
        std::ifstream stream(argv[2]); if (!stream) return 2;
        json document; stream >> document; json decision;
        const auto next = ymcc::initializeBootModeInputStartup(document, argv[3], argv[4], &decision);
        std::cout << json{{"document",next},{"decision",decision}}.dump() << std::endl; return 0;
    }
    std::vector<std::string> cases;
    auto check = [&](const char* label, std::function<void()> fn) { fn(); cases.emplace_back(label); };
    const json saved = {{"revision", 15}, {"schemaVersion", 1}, {"unknownRoot", {{"keep", true}}},
        {"outputTarget", {{"persona", "elite"}, {"buttonMappingEnabled", true}, {"gyroEnabled", true}, {"oemRearMap", {{"m1", "left"}}}, {"backMapping", true}}},
        {"buttonMapping", {{"rules", {{"custom", "keep"}}}}},
        {"gyroMotion", {{"enabled", true}, {"outputMode", "virtual-stick"}, {"preset", "custom"}, {"calibrationId", "keep-calibration"}, {"gyroMultiplier", 2.5},
            {"presets", {{"fps", {{"motionMode", "suppress"}, {"motionTrigger", " LT "}, {"gyroMultiplier", 1.7}, {"gyroMode", "legacy"}}}}}}},
        {"gameOverride", {{"sessionId", "old"}}}, {"pendingConfigHash", "old-hash"}, {"appliedRevision", 15}, {"applyStatus", "active"}};
    check("new install and last-session on both default closed", [&] {
        auto next = ymcc::initializeInputStartup(saved, json::object(), "session-1");
        expect(next["outputTarget"]["persona"] == "disabled"); expect(next["outputTarget"]["gyroEnabled"] == false);
        expect(next["gyroMotion"]["enabled"] == false); expect(next["revision"] == 16);
        auto fresh = ymcc::initializeInputStartup(nullptr, nullptr, "fresh"); expect(fresh["revision"] == 1); expect(fresh["gyroMotion"]["enabled"] == false);
    });
    for (const auto* pad : {"disabled", "steamdeck", "dualsense-edge", "elite"}) {
        for (const auto* gyro : {"off", "fps", "racing", "custom", "steam"}) {
            auto prefs = json{{"virtualGamepadPersona", pad}, {"gyroPreset", gyro}};
            auto next = ymcc::initializeInputStartup(saved, prefs, "matrix");
            bool on = std::string(pad) != "disabled", gyroOn = on && std::string(gyro) != "off";
            expect(next["outputTarget"]["persona"] == pad); expect(next["outputTarget"]["buttonMappingEnabled"] == on);
            expect(next["outputTarget"]["gyroEnabled"] == gyroOn); expect(next["gyroMotion"]["enabled"] == gyroOn);
            if (gyroOn) { expect(next["gyroMotion"]["preset"] == gyro); expect(next["gyroMotion"]["activePreset"] == gyro); expect(next["gyroMotion"]["outputMode"] == "virtual-stick"); }
            cases.emplace_back(std::string("persona/preset matrix: ") + pad + "/" + gyro);
        }
    }
    check("invalid saved personas fail closed", [&] {
        for (const json& raw : {json(nullptr), json::array(), json{{"virtualGamepadPersona", "unknown"}, {"gyroPreset", "fps"}}, json{{"virtualGamepadPersona", true}, {"gyroPreset", 123}}})
            expect(ymcc::normalizeInputStartupPreferences(raw) == json({{"virtualGamepadPersona", "disabled"}, {"gyroPreset", "off"}}));
        expect(ymcc::normalizeInputStartupPreferences({{"virtualGamepadPersona", "elite"}, {"gyroPreset", "invalid"}})["gyroPreset"] == "off");
    });
    check("saved legacy personas migrate once without admitting legacy runtime identities", [&] {
        for (const auto& row : std::vector<std::pair<std::string, std::string>>{{"xbox360", "elite"}, {"dualshock4", "dualsense-edge"}, {"dualsense", "dualsense-edge"}}) {
            const auto prefs = ymcc::normalizeInputStartupPreferences({{"virtualGamepadPersona", row.first}, {"gyroPreset", "off"}});
            expect(prefs["virtualGamepadPersona"] == row.second && prefs["gyroPreset"] == "off");
            expect(!ymcc::controllerPersona::currentOutput(row.first) && ymcc::controllerPersona::currentOutput(row.second));
            for (bool buttons : {false, true}) for (bool gyro : {false, true}) {
                auto legacy = saved; legacy["startupAppliedSession"] = "already-applied";
                legacy["outputTarget"].update(json{{"persona", row.first}, {"buttonMappingEnabled", buttons}, {"gyroEnabled", gyro},
                    {"descriptorHash", "old-identity"}, {"closure", "CLOSED"}});
                const auto next = ymcc::initializeInputStartup(legacy, {}, "already-applied");
                expect(next["outputTarget"]["persona"] == row.second && next["outputTarget"]["descriptorHash"].is_null() && next["outputTarget"]["closure"] == "UNENCLOSED");
                expect(next["outputTarget"]["buttonMappingEnabled"] == buttons && next["outputTarget"]["gyroEnabled"] == gyro);
                expect(next["revision"] == legacy["revision"] && next["applyStatus"] == "saved-pending");
                for (const char* field : {"appliedRevision", "appliedConfigHash", "pendingConfigHash", "hostAcknowledgedRevision", "hostAcknowledgedConfigHash"}) expect(next[field].is_null());
                expect(ymcc::initializeInputStartup(next, {}, "already-applied") == next);
                expect(next["unknownRoot"] == legacy["unknownRoot"] && next["gyroMotion"] == legacy["gyroMotion"]);
            }
        }
    });
    const json on = {{"virtualGamepadPersona", "steamdeck"}, {"gyroPreset", "fps"}};
    check("stored preset tuning and suppress encoding retained", [&] {
        const auto next = ymcc::initializeInputStartup(saved, on, "tuning");
        expect(next["gyroMotion"]["gyroMultiplier"] == 1.7); expect(next["gyroMotion"]["motionMode"] == "on");
        expect(next["gyroMotion"]["motionTrigger"] == "LT"); expect(!next["gyroMotion"].contains("gyroMode"));
        expect(next["gyroMotion"]["presets"] == saved["gyroMotion"]["presets"]); expect(next["gyroMotion"]["calibrationId"] == "keep-calibration");
    });
    check("all new preset defaults match UI and game override", [&] {
        auto fps = ymcc::startupGyroPresetParams("fps", nullptr); expect(fps["outputStick"] == "right" && fps["outputAxis"] == "xy" && fps["aimingSightsTrigger"] == "" && fps["gyroWeight"] == 1.3);
        auto racing = ymcc::startupGyroPresetParams("racing", nullptr); expect(racing["outputStick"] == "left" && racing["outputAxis"] == "x" && racing["motionInput"] == "joystick-steering");
        auto steam = ymcc::startupGyroPresetParams("steam", nullptr); expect(steam["motionInput"] == "local-space");
        for (auto key : {"fps", "racing", "steam", "custom"}) {
            auto params = ymcc::startupGyroPresetParams(key, nullptr);
            expect(params["innerDeadzone"] == 1 && params["antiDeadzone"] == 20 && params["autoCalibrate"] == true);
            if (std::string(key) == "fps") expect(params["motionMode"] == "off" && params["motionTrigger"] == "LT");
            else expect(params["motionMode"] == "on" && params["motionTrigger"].is_null());
        }
    });
    check("on/trigger normalization and inactive preset memory", [&] {
        auto p = ymcc::startupGyroPresetParams("fps", {{"fps", {{"motionMode", "on"}, {"motionTrigger", "RB"}}}}); expect(p["motionTrigger"].is_null());
        auto off = ymcc::initializeInputStartup(saved, {}, "off"); expect(off["gyroMotion"]["presets"] == saved["gyroMotion"]["presets"]); expect(off["gyroMotion"]["gyroMultiplier"] == 2.5);
    });
    check("mapping calibration and unknown fields not erased", [&] {
        auto next = ymcc::initializeInputStartup(saved, on, "retain");
        expect(next["buttonMapping"] == saved["buttonMapping"]); expect(next["unknownRoot"] == saved["unknownRoot"]);
        expect(next["outputTarget"]["oemRearMap"] == saved["outputTarget"]["oemRearMap"]); expect(next["outputTarget"]["backMapping"] == true);
    });
    check("stale game overlay and T10 claims removed without pretending ACK", [&] {
        auto next = ymcc::initializeInputStartup(saved, on, "ack"); expect(next["gameOverride"].is_null()); expect(next["startupAppliedSession"] == "ack");
        expect(next["applyStatus"] == "unknown");
        for (auto key : {"appliedRevision", "appliedConfigHash", "pendingConfigHash", "hostAcknowledgedRevision", "hostAcknowledgedConfigHash"}) expect(next[key].is_null());
    });
    check("same session wake/reload is no-op even after manual changes", [&] {
        auto next = ymcc::initializeInputStartup(saved, on, "same"); next["outputTarget"]["persona"] = "elite"; next["gyroMotion"]["enabled"] = false;
        expect(ymcc::initializeInputStartup(next, {}, "same") == next);
        auto cold = ymcc::initializeInputStartup(next, {}, "different"); expect(cold["outputTarget"]["persona"] == "disabled"); expect(cold["revision"] == 17);
    });
    check("unsafe revision or missing owner session rejected", [&] {
        for (const json& rev : {json(-1), json("15"), json(nullptr), json(9007199254740991LL), json(1.5)}) {
            auto bad = saved; bad["revision"] = rev; bool rejected = false;
            try { ymcc::initializeInputStartup(bad, on, "bad"); } catch (...) { rejected = true; } expect(rejected);
        }
        bool rejected = false; try { ymcc::initializeInputStartup(saved, on, ""); } catch (...) { rejected = true; } expect(rejected);
    });
    const auto bootDocument = [&] {
        auto input = saved;
        input["startupAppliedSession"] = "old-process";
        input["startupAppliedBoot"] = "windows-boot-1";
        return json{{"input", input}, {"startupDesired", on},
            {"extensions", {{"bootModes", {{"schema", 1}, {"current", "global"}, {"boot", "steam"}}}}},
            {"gameProfiles", {{"fixture-game", {{"dedicated", true}}}}}};
    };
    check("same Windows boot retires dead owners without consuming next-boot input or tuning", [&] {
        const auto doc = bootDocument();
        const auto next = ymcc::initializeBootModeInputStartup(doc, "new-process", "windows-boot-1");
        expect(next["input"]["outputTarget"] == doc["input"]["outputTarget"]);
        expect(next["input"]["gyroMotion"] == doc["input"]["gyroMotion"]);
        expect(next["input"]["buttonMapping"] == doc["input"]["buttonMapping"]);
        expect(next["input"]["gameOverride"].is_null());
        expect(next["input"]["startupAppliedSession"] == "new-process");
        expect(next["input"]["revision"] == 16 && next["input"]["applyStatus"] == "unknown");
        expect(next["input"]["appliedRevision"].is_null() && next["input"]["pendingConfigHash"].is_null());
        expect(next["startupDesired"] == doc["startupDesired"] && next["gameProfiles"] == doc["gameProfiles"]);
        expect(ymcc::initializeBootModeInputStartup(next, "new-process", "windows-boot-1") == next);
    });
    check("confirmed next Windows boot consumes the selected input once", [&] {
        const auto doc = bootDocument();
        auto next = ymcc::initializeBootModeInputStartup(doc, "new-boot-process", "windows-boot-2");
        expect(next["input"]["outputTarget"]["persona"] == "steamdeck");
        expect(next["input"]["gyroMotion"]["preset"] == "fps" && next["input"]["gyroMotion"]["enabled"] == true);
        expect(next["input"]["startupAppliedBoot"] == "windows-boot-2");
        next["input"]["gyroMotion"]["gyroMultiplier"] = 7.25;
        const auto restart = ymcc::initializeBootModeInputStartup(next, "second-process", "windows-boot-2");
        expect(restart["input"]["gyroMotion"]["gyroMultiplier"] == 7.25);
        expect(restart["gameProfiles"] == doc["gameProfiles"]);
    });
    check("first upgraded launch anchors the observed boot and retains current input", [&] {
        auto doc = bootDocument(); doc["input"].erase("startupAppliedBoot");
        const auto next = ymcc::initializeBootModeInputStartup(doc, "upgraded-process", "windows-boot-1");
        expect(next["input"]["outputTarget"] == doc["input"]["outputTarget"]);
        expect(next["input"]["gyroMotion"] == doc["input"]["gyroMotion"]);
        expect(next["extensions"]["bootModesAppliedBoot"] == "windows-boot-1");
        const auto reboot = ymcc::initializeBootModeInputStartup(next, "reboot-process", "windows-boot-2");
        expect(reboot["input"]["outputTarget"]["persona"] == "steamdeck");
    });
    check("first selected scene consumes input on the very next boot even when legacy input has no boot marker", [&] {
        auto doc = bootDocument(); doc["input"].erase("startupAppliedBoot");
        doc["extensions"]["bootModes"]["boot"] = "xbox-steam";
        doc["extensions"]["bootModesAppliedBoot"] = "windows-boot-1";
        const auto sameBoot = ymcc::initializeBootModeInputStartup(doc, "same-boot-process", "windows-boot-1");
        expect(sameBoot["input"]["outputTarget"] == doc["input"]["outputTarget"]);
        const auto reboot = ymcc::initializeBootModeInputStartup(doc, "first-reboot-process", "windows-boot-2");
        expect(reboot["input"]["outputTarget"]["persona"] == "steamdeck");
        expect(reboot["input"]["startupAppliedBoot"] == "windows-boot-2");
        expect(reboot["extensions"]["bootModesAppliedBoot"] == "windows-boot-1");
        expect(reboot["gameProfiles"] == doc["gameProfiles"]);
    });
    check("unknown boot identity cannot consume a selected scene or fabricate a boot claim", [&] {
        const auto doc = bootDocument();
        const auto next = ymcc::initializeBootModeInputStartup(doc, "unknown-boot-process", "");
        expect(next["input"]["outputTarget"] == doc["input"]["outputTarget"]);
        expect(next["input"]["gyroMotion"] == doc["input"]["gyroMotion"]);
        expect(next["input"]["gameOverride"].is_null());
        expect(next["input"]["startupAppliedBoot"] == "windows-boot-1");
        expect(!next["extensions"].contains("bootModesAppliedBoot"));
    });
    check("a scene selection claim suppresses premature preferences before the initial restore", [&] {
        auto doc = bootDocument();
        doc["extensions"]["bootModesAppliedBoot"] = "windows-boot-2";
        const auto next = ymcc::initializeBootModeInputStartup(doc, "selection-process", "windows-boot-2");
        expect(next["input"]["outputTarget"] == doc["input"]["outputTarget"]);
        expect(next["input"]["gyroMotion"] == doc["input"]["gyroMotion"]);
    });
    check("applications without a boot-mode document retain the existing process startup policy", [&] {
        auto doc = bootDocument();doc["extensions"].erase("bootModes");
        const auto next = ymcc::initializeBootModeInputStartup(doc, "legacy-process", "windows-boot-1");
        expect(next["input"]["outputTarget"]["persona"] == "steamdeck");
        expect(next["input"]["gyroMotion"]["preset"] == "fps");
        expect(next["input"]["startupAppliedBoot"] == "windows-boot-1");
    });
    check("startup result observations are current only for the boot actually read", [&] {
        auto doc = bootDocument();
        doc["extensions"]["bootModesStartupResult"] = {{"boot", "windows-boot-1"}, {"status", "failed"}};
        const auto known = ymcc::initializeBootModeInputStartup(doc, "new-process", "windows-boot-1");
        expect(known["input"]["startupObservedBoot"] == "windows-boot-1");
        const auto unknown = ymcc::initializeBootModeInputStartup(known, "unknown-process", "");
        expect(unknown["input"]["startupObservedBoot"].is_null());
        expect(unknown["input"]["startupAppliedBoot"] == "windows-boot-1");
        expect(unknown["extensions"]["bootModesStartupResult"] == doc["extensions"]["bootModesStartupResult"]);
        const auto changed = ymcc::initializeBootModeInputStartup(unknown, "later-process", "windows-boot-2");
        expect(changed["input"]["startupObservedBoot"] == "windows-boot-2");
    });
    check("v1 to kernel volatile identity anchors live state once without hot-applying an old draft", [&] {
        auto doc = bootDocument();doc["extensions"]["bootModesAppliedBoot"]="windows-loader-v1|repeated|1";
        const auto anchor=ymcc::initializeBootModeInputStartup(doc,"upgrade-process","windows-volatile-v2|session-1");
        expect(anchor["input"]["outputTarget"]==doc["input"]["outputTarget"]);
        expect(anchor["extensions"]["bootModesAppliedBoot"]=="windows-volatile-v2|session-1");
        expect(anchor["extensions"]["bootModesIdentityMigration"]["policy"]=="preserve-current-until-next-boot");
        const auto reopened=ymcc::initializeBootModeInputStartup(anchor,"same-boot-process","windows-volatile-v2|session-1");
        expect(reopened["input"]["outputTarget"]==doc["input"]["outputTarget"]);
        const auto reboot=ymcc::initializeBootModeInputStartup(anchor,"next-boot-process","windows-volatile-v2|session-2");
        expect(reboot["input"]["outputTarget"]["persona"]=="steamdeck");
    });
    check("repeated G3E loader tuple fails old boundary while distinct kernel session consumes the selected SD", [&] {
        auto doc=bootDocument();doc["input"]["startupAppliedBoot"]="windows-loader-v1|{1E8F80C1-C49D-11F1-8755-801316155698}|1";
        doc["extensions"]["bootModesAppliedBoot"]=doc["input"]["startupAppliedBoot"];
        const auto old=ymcc::initializeBootModeInputStartup(doc,"actual-next-boot",doc["input"]["startupAppliedBoot"]);
        expect(old["input"]["outputTarget"]["persona"]==doc["input"]["outputTarget"]["persona"]);
        doc["input"]["startupAppliedBoot"]="windows-volatile-v2|before";
        doc["extensions"]["bootModesAppliedBoot"]="windows-volatile-v2|before";
        const auto next=ymcc::initializeBootModeInputStartup(doc,"actual-next-boot","windows-volatile-v2|after");
        expect(next["input"]["outputTarget"]["persona"]=="steamdeck");
    });
    check("Win5 boot marker without a mode document identifies legacy OFF, not a selected Steam scene", [&] {
        auto doc = bootDocument();
        doc["extensions"] = {{"bootModesAppliedBoot", "windows-boot-1"}};
        doc["startupDesired"] = {{"virtualGamepadPersona", "disabled"}, {"gyroPreset", "off"}};
        doc["input"]["outputTarget"]["persona"] = "steamdeck";
        json facts;
        const auto next = ymcc::initializeBootModeInputStartup(doc, "win5-next-process", "windows-boot-2", &facts);
        expect(next == ymcc::initializeBootModeInputStartup(doc, "win5-next-process", "windows-boot-2"));
        expect(facts["configured"] == false && facts["selectedMode"].is_null());
        expect(facts["source"] == "legacy-startupDesired" && facts["requestedPersona"] == "disabled");
        expect(facts["action"] == "apply-startup-preferences");
        expect(next["input"]["outputTarget"]["persona"] == "disabled");
    });
    for (const auto* mode : {"global", "xbox", "steam", "xbox-steam"}) {
        const std::string caseName = std::string("mode startup decision follows exact prepared boundary: ") + mode;
        check(caseName.c_str(), [&] {
            auto doc = bootDocument();
            doc["extensions"]["bootModes"]["boot"] = mode;
            doc["extensions"]["bootModes"]["bootEnabled"] = true;
            doc["extensions"]["bootModes"]["revision"] = 4;
            doc["extensions"]["bootModesAppliedBoot"] = "windows-boot-1";
            doc["input"]["startupAppliedBoot"] = "windows-boot-1";
            doc["startupDesired"]["virtualGamepadPersona"] = std::string(mode) == "steam" || std::string(mode) == "xbox-steam" ? "steamdeck" : "elite";
            json sameFacts, nextFacts, repeatedFacts;
            const auto same = ymcc::initializeBootModeInputStartup(doc, "same-boot-other-process", "windows-boot-1", &sameFacts);
            expect(same["input"]["outputTarget"] == doc["input"]["outputTarget"]);
            expect(sameFacts["selectedMode"] == mode && sameFacts["source"] == "four-mode-startup");
            expect(sameFacts["action"] == "preserve-current-input" && sameFacts["selectionBootMatchesObserved"] == true);
            const auto next = ymcc::initializeBootModeInputStartup(doc, "next-boot-process", "windows-boot-2", &nextFacts);
            expect(next == ymcc::initializeBootModeInputStartup(doc, "next-boot-process", "windows-boot-2"));
            expect(nextFacts["action"] == "apply-startup-preferences" && nextFacts["revision"] == 4);
            expect(nextFacts["requestedPersona"] == next["input"]["outputTarget"]["persona"]);
            expect(nextFacts["selectionBootMatchesObserved"] == false && nextFacts["inputBootMatchesObserved"] == false);
            const auto repeated = ymcc::initializeBootModeInputStartup(next, "next-boot-process", "windows-boot-2", &repeatedFacts);
            expect(repeated == next && repeatedFacts["action"] == "preserve-same-process");
        });
    }
    check("missing Windows boot identity is reported as preserve for a selected scene", [&] {
        auto doc = bootDocument(); json facts;
        const auto next = ymcc::initializeBootModeInputStartup(doc, "unknown-boot-process", "", &facts);
        expect(next == ymcc::initializeBootModeInputStartup(doc, "unknown-boot-process", ""));
        expect(facts["configured"] == true && facts["action"] == "preserve-current-input");
        expect(facts["selectionBootMatchesObserved"] == false && facts["inputBootMatchesObserved"] == false);
    });
    // 2026-11-11 supersedes only bootEnabled=false => forced disabled/off.
    // A disabled mode selection now consumes GLOBAL input on the next boot.
    // Explicit global/scene OFF, and missing-document startupDesired OFF (the
    // original Win5 case above), remain authoritative. All original cases stay.
    // These additions call the production JSON policy only; synthetic boot and
    // process strings never query boot_mode_identity::current or start a Host.
    const char* modeIds[] = {"global", "xbox", "steam", "xbox-steam"};
    const char* padIds[] = {"disabled", "steamdeck", "dualsense-edge", "elite"};
    const char* gyroIds[] = {"off", "fps", "racing", "custom", "steam"};
    const auto expectedPreferences = [](const char* pad, const char* gyro) {
        return json{{"virtualGamepadPersona", pad},
            {"gyroPreset", std::string(pad) == "disabled" ? "off" : gyro}};
    };
    const auto expectAppliedPreferences = [](const json& document, const char* pad, const char* gyro) {
        const auto& input = document.at("input");
        const bool padEnabled = std::string(pad) != "disabled";
        const bool gyroEnabled = padEnabled && std::string(gyro) != "off";
        expect(input.at("outputTarget").at("persona") == pad);
        expect(input.at("outputTarget").at("buttonMappingEnabled") == padEnabled);
        expect(input.at("outputTarget").at("gyroEnabled") == gyroEnabled);
        expect(input.at("gyroMotion").at("enabled") == gyroEnabled);
        if (gyroEnabled) {
            expect(input.at("gyroMotion").at("preset") == gyro);
            expect(input.at("gyroMotion").at("activePreset") == gyro);
            expect(input.at("gyroMotion").at("outputMode") == "virtual-stick");
        }
    };
    const auto globalDocument = [&](const char* mode, bool enabled, const char* pad, const char* gyro) {
        auto document = bootDocument();
        const char* rememberedPreset = std::string(gyro) == "off" ? "custom" : gyro;
        document["extensions"]["bootModes"] = {{"schema", 1}, {"current", "global"},
            {"boot", mode}, {"bootEnabled", enabled}, {"revision", 12}, {"controllerDefaultsVersion", 2},
            {"global", {{"input", {{"persona", pad}, {"unknownGlobalInput", "keep"}}},
                {"gyro", {{"motion", {{"enabled", std::string(gyro) != "off"},
                    {"preset", rememberedPreset}, {"activePreset", rememberedPreset}, {"unknownGlobalMotion", "keep"}}}}}}},
            {"scenes", json::object()}, {"unknownModes", "keep"}};
        document["extensions"]["bootModesAppliedBoot"] = "windows-boot-1";
        // Deliberately contradict global so a stale materialized cache cannot
        // accidentally satisfy the global-inheritance assertions.
        document["startupDesired"] = {{"virtualGamepadPersona", std::string(pad) == "elite" ? "steamdeck" : "elite"},
            {"gyroPreset", std::string(gyro) == "fps" ? "racing" : "fps"},
            {"fanControl", true}, {"unknownStartup", "keep"}};
        return document;
    };
    // 4 modes x 2 selection states x 4 global personas x 5 gyro choices.
    // Every row also covers a same-process repeat, a manual runtime change,
    // a different native process in the same boot, and the following real boot.
    for (const char* mode : modeIds) for (bool enabled : {false, true})
        for (const char* pad : padIds) for (const char* gyro : gyroIds) {
            const std::string label = std::string("global inheritance and reopen matrix: ") + mode +
                (enabled ? "/enabled/" : "/disabled/") + pad + "/" + gyro;
            check(label.c_str(), [&] {
                auto document = globalDocument(mode, enabled, pad, gyro);
                for (const char* scene : {"xbox", "steam", "xbox-steam"})
                    document["extensions"]["bootModes"]["scenes"][scene] = {{"name", "partial-scene"},
                        {"overrides", {{"input", {{"mouse", "joyxoff"}}},
                            {"gyro", {{"motion", {{"unknownSceneMotion", "keep"}}}}}}}};
                if (!enabled || std::string(mode) == "global") {
                    const char* ignoredScene = std::string(mode) == "global" ? "steam" : mode;
                    document["extensions"]["bootModes"]["scenes"][ignoredScene]["overrides"] = {
                        {"input", {{"persona", std::string(pad) == "steamdeck" ? "elite" : "steamdeck"}}},
                        {"gyro", {{"motion", {{"enabled", std::string(gyro) == "off"},
                            {"preset", "fps"}, {"activePreset", "fps"}}}}}};
                }
                const auto preferences = expectedPreferences(pad, gyro);
                expect(ymcc::bootModeInputStartupPreferences(document["extensions"]["bootModes"], document["startupDesired"]) == preferences);
                json facts;
                const auto next = ymcc::initializeBootModeInputStartup(document, "matrix-first-process", "windows-boot-2", &facts);
                expectAppliedPreferences(next, pad, gyro);
                expect(facts["configured"] == true && facts["source"] == "four-mode-startup");
                expect(facts["selectedMode"] == mode && facts["effectiveMode"] == (enabled ? mode : "global"));
                expect(facts["bootEnabled"] == enabled && facts["revision"] == 12);
                expect(facts["requestedPersona"] == preferences["virtualGamepadPersona"] && facts["requestedGyroPreset"] == preferences["gyroPreset"]);
                expect(facts["action"] == "apply-startup-preferences");
                expect(facts["selectionBootMatchesObserved"] == false && facts["inputBootMatchesObserved"] == false);
                expect(next["extensions"] == document["extensions"] && next["gameProfiles"] == document["gameProfiles"]);
                expect(next["startupDesired"]["virtualGamepadPersona"] == preferences["virtualGamepadPersona"]);
                expect(next["startupDesired"]["gyroPreset"] == preferences["gyroPreset"]);
                expect(next["startupDesired"]["fanControl"] == true && next["startupDesired"]["unknownStartup"] == "keep");
                expect(next["input"]["buttonMapping"] == saved["buttonMapping"] && next["input"]["unknownRoot"] == saved["unknownRoot"]);
                expect(next["input"]["outputTarget"]["oemRearMap"] == saved["outputTarget"]["oemRearMap"]);
                expect(next["input"]["gyroMotion"]["calibrationId"] == "keep-calibration");
                expect(next["input"]["gyroMotion"]["presets"] == saved["gyroMotion"]["presets"]);
                expect(next["input"]["revision"] == 16 && next["input"]["gameOverride"].is_null());
                expect(next["input"]["startupAppliedBoot"] == "windows-boot-2" && next["input"]["startupObservedBoot"] == "windows-boot-2");
                expect(next["input"]["applyStatus"] == "unknown");
                for (const char* field : {"appliedRevision", "appliedConfigHash", "pendingConfigHash", "hostAcknowledgedRevision", "hostAcknowledgedConfigHash"})
                    expect(next["input"][field].is_null());
                json repeatFacts;
                expect(ymcc::initializeBootModeInputStartup(next, "matrix-first-process", "windows-boot-2", &repeatFacts) == next);
                expect(repeatFacts["action"] == "preserve-same-process");

                auto manual = next;
                manual["input"]["outputTarget"].update(json{{"persona", std::string(pad) == "elite" ? "steamdeck" : "elite"},
                    {"buttonMappingEnabled", true}, {"gyroEnabled", false}});
                manual["input"]["gyroMotion"].update(json{{"enabled", false}, {"gyroMultiplier", 7.25}});
                auto& draft = manual["extensions"]["bootModes"];
                draft["boot"] = std::string(mode) == "global" ? "xbox" : "global";
                draft["bootEnabled"] = !enabled;
                draft["revision"] = 13;
                draft["scenes"] = json::object();
                json reopenFacts;
                const auto reopened = ymcc::initializeBootModeInputStartup(manual, "matrix-reopen-process", "windows-boot-2", &reopenFacts);
                expect(reopenFacts["action"] == "preserve-current-input" && reopenFacts["inputBootMatchesObserved"] == true);
                expect(reopenFacts["selectedMode"] == draft["boot"]);
                expect(reopenFacts["effectiveMode"] == (enabled ? json("global") : draft["boot"]));
                expect(reopened["input"]["outputTarget"] == manual["input"]["outputTarget"]);
                expect(reopened["input"]["gyroMotion"] == manual["input"]["gyroMotion"]);
                expect(reopened["input"]["buttonMapping"] == manual["input"]["buttonMapping"]);
                expect(reopened["input"]["startupAppliedSession"] == "matrix-reopen-process" && reopened["input"]["revision"] == 17);
                expect(reopened["extensions"] == manual["extensions"] && reopened["gameProfiles"] == manual["gameProfiles"]);
                expect(ymcc::initializeBootModeInputStartup(reopened, "matrix-reopen-process", "windows-boot-2") == reopened);
                json rebootFacts;
                const auto rebooted = ymcc::initializeBootModeInputStartup(reopened, "matrix-next-boot-process", "windows-boot-3", &rebootFacts);
                expectAppliedPreferences(rebooted, pad, gyro);
                expect(rebootFacts["action"] == "apply-startup-preferences" && rebooted["input"]["revision"] == 18);
                expect(rebooted["extensions"] == manual["extensions"]);
            });
        }
    for (int shape = 0; shape < 3; ++shape) {
        const std::string label = std::string("missing mode configuration uses explicit startupDesired: ") +
            (shape == 0 ? "absent" : shape == 1 ? "null" : "empty");
        check(label.c_str(), [&] {
            for (const char* pad : padIds) for (const char* gyro : gyroIds) {
                auto document = bootDocument();
                if (shape == 0) document["extensions"].erase("bootModes");
                else document["extensions"]["bootModes"] = shape == 1 ? json(nullptr) : json::object();
                // A marker without a mode document is not a configured scene.
                document["extensions"]["bootModesAppliedBoot"] = "windows-boot-2";
                document["startupDesired"] = {{"virtualGamepadPersona", pad}, {"gyroPreset", gyro}, {"unknownStartup", "keep"}};
                json facts;
                const auto next = ymcc::initializeBootModeInputStartup(document, "fallback-process", "windows-boot-2", &facts);
                expectAppliedPreferences(next, pad, gyro);
                const auto preferences = expectedPreferences(pad, gyro);
                expect(facts["configured"] == false && facts["selectedMode"].is_null() && facts["effectiveMode"] == "global");
                expect(facts["source"] == "legacy-startupDesired" && facts["bootEnabled"].is_null() && facts["revision"].is_null());
                expect(facts["requestedPersona"] == preferences["virtualGamepadPersona"] && facts["requestedGyroPreset"] == preferences["gyroPreset"]);
                expect(facts["action"] == "apply-startup-preferences");
                expect(next["extensions"] == document["extensions"] && next["startupDesired"] == document["startupDesired"]);
                expect(next["gameProfiles"] == document["gameProfiles"] && next["input"]["buttonMapping"] == document["input"]["buttonMapping"]);
                json repeatedFacts;
                expect(ymcc::initializeBootModeInputStartup(next, "fallback-process", "windows-boot-2", &repeatedFacts) == next);
                expect(repeatedFacts["action"] == "preserve-same-process");
                // Missing-document cross-process behavior remains the original
                // legacy policy; do not claim configured-mode deduplication here.
            }
        });
    }
    for (const char* mode : modeIds) for (bool enabled : {false, true}) {
        const std::string label = std::string("missing scene fields inherit known global: ") + mode + (enabled ? "/enabled" : "/disabled");
        check(label.c_str(), [&] {
            for (int shape = 0; shape < 6; ++shape) {
                auto document = globalDocument(mode, enabled, "dualsense-edge", "racing");
                auto& modes = document["extensions"]["bootModes"];
                const char* scene = std::string(mode) == "global" ? "steam" : mode;
                if (shape == 0) modes.erase("scenes");
                if (shape == 1) modes["scenes"] = json::object();
                if (shape == 2) modes["scenes"][scene] = {{"name", "no-overrides"}};
                if (shape == 3) modes["scenes"][scene] = {{"overrides", json::object()}};
                if (shape == 4) modes["scenes"][scene] = {{"overrides", {{"input", json::object()}, {"gyro", {{"motion", json::object()}}}}}};
                if (shape == 5) modes["scenes"][scene] = {{"overrides", {{"input", {{"mouse", "off"}}},
                    {"gyro", {{"motion", {{"gyroMultiplier", 1.25}}}}}}}};
                const auto next = ymcc::initializeBootModeInputStartup(document, "inherit-process", "windows-boot-2");
                expectAppliedPreferences(next, "dualsense-edge", "racing");
                expect(next["extensions"] == document["extensions"]);
            }
        });
        const std::string ownLabel = std::string("explicit scene OFF and partial own fields respect effective mode: ") + mode + (enabled ? "/enabled" : "/disabled");
        check(ownLabel.c_str(), [&] {
            for (int variant = 0; variant < 7; ++variant) {
                auto document = globalDocument(mode, enabled, "elite", "racing");
                auto& modes = document["extensions"]["bootModes"];
                const char* scene = std::string(mode) == "global" ? "steam" : mode;
                json own;
                const char* expectedPad = "elite";
                const char* expectedGyro = "racing";
                const bool consumesScene = enabled && std::string(mode) != "global";
                if (variant == 0) {
                    own = {{"input", {{"persona", "disabled"}}}};
                    if (consumesScene) { expectedPad = "disabled"; expectedGyro = "off"; }
                } else if (variant == 1) {
                    own = {{"gyro", {{"motion", {{"enabled", false}}}}}};
                    if (consumesScene) expectedGyro = "off";
                } else if (variant == 2) {
                    own = {{"input", {{"persona", "steamdeck"}}}};
                    if (consumesScene) expectedPad = "steamdeck";
                } else if (variant == 3) {
                    own = {{"gyro", {{"motion", {{"activePreset", "fps"}}}}}};
                    if (consumesScene) expectedGyro = "fps";
                } else if (variant == 4) {
                    own = {{"input", {{"persona", "disabled"}}}, {"gyro", {{"motion", {{"enabled", false}}}}}};
                    if (consumesScene) { expectedPad = "disabled"; expectedGyro = "off"; }
                } else if (variant == 5) {
                    modes["global"]["input"]["persona"] = "disabled";
                    own = {{"input", {{"persona", "steamdeck"}}}};
                    expectedPad = consumesScene ? "steamdeck" : "disabled";
                    if (!consumesScene) expectedGyro = "off";
                } else {
                    modes["global"]["gyro"]["motion"]["enabled"] = false;
                    own = {{"gyro", {{"motion", {{"enabled", true}, {"activePreset", "steam"}}}}}};
                    expectedGyro = consumesScene ? "steam" : "off";
                }
                modes["scenes"][scene] = {{"name", "explicit-partial"}, {"overrides", own}};
                json facts;
                const auto next = ymcc::initializeBootModeInputStartup(document, "own-process", "windows-boot-2", &facts);
                expectAppliedPreferences(next, expectedPad, expectedGyro);
                expect(facts["effectiveMode"] == (enabled ? mode : "global") && facts["selectedMode"] == mode);
                expect(next["extensions"] == document["extensions"]);
            }
        });
    }
    check("absent explicit startup input never adopts an ordinary runtime ON toggle", [&] {
        auto document = bootDocument();
        document["extensions"].erase("bootModes");
        document.erase("startupDesired");
        const auto next = ymcc::initializeBootModeInputStartup(document, "no-explicit-startup", "windows-boot-2");
        expectAppliedPreferences(next, "disabled", "off");
        expect(!next.contains("startupDesired") && next["input"]["buttonMapping"] == document["input"]["buttonMapping"]);
    });
    check("materialized global document gains same-boot ownership without replaying legacy startup input", [&] {
        auto document = bootDocument();
        document["extensions"].erase("bootModes");
        auto next = ymcc::initializeBootModeInputStartup(document, "before-materialization", "windows-boot-2");
        // Model an externally committed document, not native/frontend creation.
        // The consumer must preserve a later manual OFF in this observed boot.
        next["extensions"]["bootModes"] = globalDocument("steam", false, "steamdeck", "fps")["extensions"]["bootModes"];
        next["extensions"]["bootModesAppliedBoot"] = "windows-boot-2";
        next["input"]["outputTarget"].update(json{{"persona", "disabled"}, {"buttonMappingEnabled", false}, {"gyroEnabled", false}});
        next["input"]["gyroMotion"]["enabled"] = false;
        json facts;
        const auto reopened = ymcc::initializeBootModeInputStartup(next, "after-materialization", "windows-boot-2", &facts);
        expect(reopened["input"]["outputTarget"] == next["input"]["outputTarget"] && reopened["input"]["gyroMotion"] == next["input"]["gyroMotion"]);
        expect(facts["configured"] == true && facts["selectedMode"] == "steam" && facts["effectiveMode"] == "global");
        expect(facts["action"] == "preserve-current-input" && facts["selectionBootMatchesObserved"] == true);
        const auto rebooted = ymcc::initializeBootModeInputStartup(reopened, "materialized-next-boot", "windows-boot-3");
        expectAppliedPreferences(rebooted, "steamdeck", "fps");
        expect(rebooted["extensions"] == next["extensions"]);
    });
    check("disabled-to-global fallback still rejects corrupt mode input instead of treating it as missing", [&] {
        for (bool enabled : {false, true}) for (int corruption = 0; corruption < 6; ++corruption) {
            auto document = globalDocument("steam", enabled, "elite", "racing");
            auto& modes = document["extensions"]["bootModes"];
            if (corruption == 0) modes["schema"] = 2;
            if (corruption == 1) modes["bootEnabled"] = "false";
            if (corruption == 2) modes["global"]["input"]["persona"] = "unknown";
            if (corruption == 3) modes["global"]["gyro"]["motion"]["enabled"] = "true";
            if (corruption == 4) modes["scenes"]["steam"] = {{"overrides", {{"input", {{"persona", true}}}}}};
            if (corruption == 5) modes["boot"] = "unknown";
            bool rejected = false;
            try { ymcc::initializeBootModeInputStartup(document, "invalid-fallback", "windows-boot-2"); }
            catch (...) { rejected = true; }
            expect(rejected);
        }
    });
    std::cout << json{{"suite", "Input startup native JSON policy"}, {"passed", cases.size()}, {"cases", cases}, {"hardwareOperations", 0}}.dump(2) << std::endl;
}
