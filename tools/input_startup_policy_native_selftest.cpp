// Pure JSON-only harness: no Windows APIs, devices, services or product runtime.
#include "input_startup_policy.h"
#include <iostream>
#include <functional>
#include <vector>
using nlohmann::json;
static void expect(bool value) { if (!value) throw std::runtime_error("assertion-failed"); }
int main() {
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
        auto fps = ymcc::startupGyroPresetParams("fps", nullptr); expect(fps["outputStick"] == "right" && fps["outputAxis"] == "xy" && fps["aimingSightsTrigger"] == "LT");
        auto racing = ymcc::startupGyroPresetParams("racing", nullptr); expect(racing["outputStick"] == "left" && racing["outputAxis"] == "x" && racing["motionInput"] == "joystick-steering");
        auto steam = ymcc::startupGyroPresetParams("steam", nullptr); expect(steam["motionInput"] == "local-space");
        for (auto key : {"fps", "racing", "steam", "custom"}) {
            auto params = ymcc::startupGyroPresetParams(key, nullptr);
            expect(params["innerDeadzone"] == 1 && params["antiDeadzone"] == 20 && params["autoCalibrate"] == true && params["motionMode"] == "on" && params["motionTrigger"].is_null());
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
    std::cout << json{{"suite", "Input startup native JSON policy"}, {"passed", cases.size()}, {"cases", cases}, {"hardwareOperations", 0}}.dump(2) << std::endl;
}
