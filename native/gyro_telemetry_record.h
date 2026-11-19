#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include "json.hpp"

// Owned gyro.telemetry transport between the BUS and UI threads.
// Each record owns its strings/trees or retains an immutable complete Host value.
namespace ymcc::gyro_telemetry {
using json = nlohmann::json;

// Construct a default only when absent; every result still owns its subtree.
inline json valueOrEmptyObject(const json& object, const char* key) {
    if (object.is_object()) {
        const auto found = object.find(key);
        if (found != object.end()) return *found;
        return json::object();
    }
    return object.value(key, json::object());
}

struct Record {
    std::uint64_t sequence = 0;
    std::string timestampUtc;
    bool sensorPresent = false;
    std::array<double, 3> gyro{}, accel{}, rawAccel{}, gravity{}, processedAcceleration{};
    std::array<double, 2> playerSpace{}, worldSpace{};
    std::array<double, 4> orientation{}; // w,x,y,z, including the captured w value.
    json gamepadMotionPlane;
    json output;
    std::string targetStick;
    json leftStick, rightStick;
    std::string motionAdmission;
    bool motionTriggerResolved = false, aimingSightsTriggerResolved = false;
    json motionContribution, layoutContribution, motionManager, layoutManager, latency;
    json hostFrame;
    std::shared_ptr<const json> immutableHostFrame;
    const json& hostFrameValue() const noexcept { return immutableHostFrame ? *immutableHostFrame : hostFrame; }
private:
    bool immutableHostFields_ = false, immutableOutputLeft_ = false;
    struct HostDefaults { json axes = {{"x", 0.0}, {"y", 0.0}}; json object = json::object(); };
    static const HostDefaults& hostDefaults() { static const HostDefaults defaults; return defaults; }
    const json& immutableHostValue(const char* name, const json& fallback) const {
        const json& host = *immutableHostFrame;
        if (!host.is_object()) (void)host.value(name, fallback); // preserve value() type errors
        const auto found = host.find(name);
        return found != host.end() ? *found : fallback;
    }
public:
    // Only the producer of the known complete immutable inactive Host enables
    // these views. Initialize literal defaults at the original construction
    // point, so no first-use allocation is deferred onto UI delivery.
    void retainImmutableHostFields(bool outputLeft) {
        if (!immutableHostFrame) return;
        (void)hostDefaults();
        immutableOutputLeft_ = outputLeft;
        immutableHostFields_ = true;
    }
    bool hasImmutableHostFields() const noexcept { return immutableHostFields_ && static_cast<bool>(immutableHostFrame); }
    const json& outputValue() const { return hasImmutableHostFields() ? immutableHostValue(immutableOutputLeft_ ? "leftStick" : "rightStick", hostDefaults().axes) : output; }
    const json& leftStickValue() const { return hasImmutableHostFields() ? immutableHostValue("leftStick", hostDefaults().axes) : leftStick; }
    const json& rightStickValue() const { return hasImmutableHostFields() ? immutableHostValue("rightStick", hostDefaults().axes) : rightStick; }
    const json& motionContributionValue() const { return hasImmutableHostFields() ? immutableHostValue("motionContribution", hostDefaults().axes) : motionContribution; }
    const json& layoutContributionValue() const { return hasImmutableHostFields() ? immutableHostValue("layoutContribution", hostDefaults().axes) : layoutContribution; }
    const json& motionManagerValue() const { return hasImmutableHostFields() ? immutableHostValue("motionManager", hostDefaults().object) : motionManager; }
    const json& layoutManagerValue() const { return hasImmutableHostFields() ? immutableHostValue("layoutManager", hostDefaults().object) : layoutManager; }
    const json& latencyValue() const { return hasImmutableHostFields() ? immutableHostValue("latency", hostDefaults().object) : latency; }


    // For diagnostics and the original value/type comparator only. Production
    // integration must preserve the existing diagnostic gate observation order.
    json tree() const {
        return {
            {"schemaVersion", 1}, {"sequence", static_cast<unsigned long long>(sequence)},
            {"timestampUtc", timestampUtc}, {"sensorPresent", sensorPresent},
            {"gyro", {{"x", gyro[0]}, {"y", gyro[1]}, {"z", gyro[2]}}},
            {"playerSpace", {{"x", playerSpace[0]}, {"y", playerSpace[1]}}},
            {"worldSpace", {{"x", worldSpace[0]}, {"y", worldSpace[1]}}},
            {"accel", {{"x", accel[0]}, {"y", accel[1]}, {"z", accel[2]}}},
            {"rawAccel", {{"x", rawAccel[0]}, {"y", rawAccel[1]}, {"z", rawAccel[2]}}},
            {"gravity", {{"x", gravity[0]}, {"y", gravity[1]}, {"z", gravity[2]}}},
            {"processedAcceleration", {{"x", processedAcceleration[0]}, {"y", processedAcceleration[1]}, {"z", processedAcceleration[2]}}},
            {"orientation", {{"w", orientation[0]}, {"x", orientation[1]}, {"y", orientation[2]}, {"z", orientation[3]}}},
            {"gamepadMotionPlane", gamepadMotionPlane}, {"output", outputValue()}, {"targetStick", targetStick},
            {"leftStick", leftStickValue()}, {"rightStick", rightStickValue()}, {"motionAdmission", motionAdmission},
            {"motionTriggerResolved", motionTriggerResolved}, {"aimingSightsTriggerResolved", aimingSightsTriggerResolved},
            {"motionContribution", motionContributionValue()}, {"layoutContribution", layoutContributionValue()},
            {"motionManager", motionManagerValue()}, {"layoutManager", layoutManagerValue()}, {"latency", latencyValue()},
            {"outputKind", "native-canonical-pipe-frame"}, {"hostFrame", hostFrameValue()}
        };
    }

    std::string envelope(json::error_handler_t handler = json::error_handler_t::strict) const {
        struct Adapter final : nlohmann::detail::output_adapter_protocol<char> {
            std::string& text;
            explicit Adapter(std::string& value) : text(value) {}
            void write_character(char value) override { text.push_back(value); }
            void write_characters(const char* value, std::size_t length) override { text.append(value, length); }
        };
        std::string text;
        auto adapter = std::make_shared<Adapter>(text);
        nlohmann::detail::serializer<json> serializer(
            nlohmann::detail::output_adapter_t<char>(adapter), ' ', handler);
        const auto value = [&](const json& item) { serializer.dump(item, false, false, 0); };
        const auto key = [&](std::string_view literal, bool first = false) {
            if (!first) text.push_back(',');
            text.append(literal); text.push_back(':');
        };
        const auto axes = [&](const auto& values, bool quaternion = false) {
            text.push_back('{');
            std::size_t index = 0;
            if (quaternion) { text.append("\"w\":"); value(json(values[index++])); }
            for (const char axis : {'x', 'y', 'z'}) {
                if (index == values.size()) break;
                if (index) text.push_back(',');
                text.push_back('"'); text.push_back(axis); text.append("\":");
                value(json(values[index++]));
            }
            text.push_back('}');
        };
        // Match the actual ordered-json dump order. Strings, scalars and
        // dynamic trees still use the exact pinned library serializer.
        text.append("{\"data\":{");
        key("\"accel\"", true); axes(accel);
        key("\"aimingSightsTriggerResolved\""); value(json(aimingSightsTriggerResolved));
        key("\"gamepadMotionPlane\""); value(gamepadMotionPlane);
        key("\"gravity\""); axes(gravity);
        key("\"gyro\""); axes(gyro);
        key("\"hostFrame\""); value(hostFrameValue());
        key("\"latency\""); value(latencyValue());
        key("\"layoutContribution\""); value(layoutContributionValue());
        key("\"layoutManager\""); value(layoutManagerValue());
        key("\"leftStick\""); value(leftStickValue());
        key("\"motionAdmission\""); value(json(motionAdmission));
        key("\"motionContribution\""); value(motionContributionValue());
        key("\"motionManager\""); value(motionManagerValue());
        key("\"motionTriggerResolved\""); value(json(motionTriggerResolved));
        key("\"orientation\""); axes(orientation, true);
        key("\"output\""); value(outputValue());
        key("\"outputKind\""); text.append("\"native-canonical-pipe-frame\"");
        key("\"playerSpace\""); axes(playerSpace);
        key("\"processedAcceleration\""); axes(processedAcceleration);
        key("\"rawAccel\""); axes(rawAccel);
        key("\"rightStick\""); value(rightStickValue());
        key("\"schemaVersion\""); value(json(1));
        key("\"sensorPresent\""); value(json(sensorPresent));
        key("\"sequence\""); value(json(static_cast<unsigned long long>(sequence)));
        key("\"targetStick\""); value(json(targetStick));
        key("\"timestampUtc\""); value(json(timestampUtc));
        key("\"worldSpace\""); axes(worldSpace);
        text.append("},\"event\":\"gyro.telemetry\"}");
        return text;
    }
};
}
