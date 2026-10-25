#pragma once
#include <string_view>

// Output personas only. Physical controllers and touchpad modes (including
// "dualsense") have independent contracts.
namespace ymcc::controllerPersona {
inline constexpr bool currentOutput(std::string_view value) noexcept {
    return value == "steamdeck" || value == "dualsense-edge" || value == "elite";
}
inline constexpr bool current(std::string_view value) noexcept {
    return value == "disabled" || currentOutput(value);
}
inline constexpr const char* runtime(std::string_view value) noexcept {
    if (value == "steamdeck") return "steamdeck";
    if (value == "dualsense-edge") return "dualsense-edge";
    if (value == "elite") return "elite";
    return "disabled";
}
// One-way conversion for saved configuration only; never use for wire admission.
inline constexpr const char* saved(std::string_view value) noexcept {
    if (value == "xbox360") return "elite";
    if (value == "dualshock4" || value == "dualsense") return "dualsense-edge";
    return runtime(value);
}
inline constexpr const char* savedOverride(std::string_view value) noexcept {
    const char* next = saved(value);
    return current(value) || std::string_view(next) != "disabled" ? next : "follow";
}
inline constexpr const char* profileIdentity(std::string_view value) noexcept {
    if (value == "steamdeck") return "steam-deck-composite";
    if (value == "dualsense-edge") return "dualsense-edge";
    if (value == "elite") return "xbox-elite-v2";
    return "disabled";
}
inline constexpr int screenProfile(std::string_view value) noexcept {
    if (value == "disabled") return 0;
    if (value == "steamdeck") return 1;
    if (value == "dualsense-edge") return 2;
    if (value == "elite") return 3;
    return -1;
}
inline constexpr const char* screenPersona(int profile) noexcept {
    switch (profile) {
    case 1: return "steamdeck";
    case 2: return "dualsense-edge";
    case 3: return "elite";
    default: return "disabled";
    }
}
inline constexpr bool motionTouchOutput(std::string_view value) noexcept {
    return value == "steamdeck" || value == "dualsense-edge";
}
} // namespace ymcc::controllerPersona
