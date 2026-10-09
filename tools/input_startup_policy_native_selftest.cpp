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
    check("invalid and legacy personas fail closed", [&] {
        for (const json& raw : {json(nullptr), json::array(), json{{"virtualGamepadPersona", "xbox360"}, {"gyroPreset", "fps"}}, json{{"virtualGamepadPersona", true}, {"gyroPreset", 123}}})
            expect(ymcc::normalizeInputStartupPreferences(raw) == json({{"virtualGamepadPersona", "disabled"}, {"gyroPreset", "off"}}));
        expect(ymcc::normalizeInputStartupPreferences({{"virtualGamepadPersona", "elite"}, {"gyroPreset", "invalid"}})["gyroPreset"] == "off");
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
    std::cout << json{{"suite", "Input startup native JSON policy"}, {"passed", cases.size()}, {"cases", cases}, {"hardwareOperations", 0}}.dump(2) << std::endl;
}
