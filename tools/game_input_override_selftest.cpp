// Pure configuration selftest. No IPC, hardware, processes or user settings access.
#include "../native/game_input_override.h"
#include <iostream>
#include <stdexcept>
using nlohmann::json;
int main() {
    int passed = 0;
    auto check = [&](bool condition, const char* name) {
        if (!condition) throw std::runtime_error(name);
        ++passed; std::cout << "PASS " << name << '\n';
    };
    const json base = {
        {"outputTarget", {{"persona", "disabled"}, {"gyroEnabled", false}}},
        {"gyroMotion", {{"enabled", false}, {"gyroMultiplier", 2}, {"presets", {{"racing", {{"gyroMultiplier", 1.8}}}}}}}
    };
    auto input = base;
    input["gameOverride"] = {
        {"identity", "101:1001:a.exe"}, {"sessionId", "owner-session"},
        {"outputTarget", {{"persona", "steamdeck"}, {"gyroEnabled", true}}},
        {"gyroMotion", {{"enabled", true}, {"gyroMultiplier", 1.7}}}
    };
    check(ymcc::gameInputOverrideActive(input, "owner-session"), "same-session overlay accepted");
    const auto effective = ymcc::effectiveGameInput(input, "owner-session");
    check(effective["outputTarget"]["persona"] == "steamdeck" && effective["gyroMotion"]["enabled"] == true,
        "native consumes transient persona and gyro");
    check(input["outputTarget"] == base["outputTarget"] && input["gyroMotion"] == base["gyroMotion"],
        "effective read never changes base or preset memory");
    check(ymcc::effectiveGameInput(input, "restarted-owner")["outputTarget"] == base["outputTarget"],
        "restart ignores old-owner overlay");
    auto invalid = input; invalid["gameOverride"]["identity"] = "";
    check(!ymcc::gameInputOverrideActive(invalid, "owner-session"), "empty game identity rejected");
    invalid = input; invalid["gameOverride"]["gyroMotion"] = false;
    check(!ymcc::gameInputOverrideActive(invalid, "owner-session"), "malformed overlay rejected");
    invalid = input; invalid["gameOverride"]["sessionId"] = "";
    check(!ymcc::gameInputOverrideActive(invalid, ""), "empty owner session rejected");
    input["gameOverride"] = nullptr;
    check(ymcc::effectiveGameInput(input, "owner-session")["gyroMotion"] == base["gyroMotion"],
        "clear overlay restores unchanged base");
    check(!ymcc::gameInputOverrideActive(json::array(), "owner-session"), "non-object settings fail closed");
    std::cout << "passed: " << passed << '\n';
}
