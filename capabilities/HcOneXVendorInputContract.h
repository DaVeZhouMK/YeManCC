// Pinned existing OneX vendor-input consumption; no model or brand discovery.
// Fixed HC f2e58468: X1 collection/serializer/initial wait/status; Mini/Apex override plans.
// Frozen HC files remain untouched; all active native input bytes come through this contract.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ymcc::capabilities::oem::onex {
inline constexpr std::string_view kX1SourceSha256 = "3b80d932227c3beddbb6b3a4007c57fc19e0fb55df0db54d389ca8834eae3485";
inline constexpr std::string_view kMiniSourceSha256 = "93feaddb6b0387e110e92265d000d08c9242968939a36f69d1653bab70630121";
inline constexpr std::string_view kApexSourceSha256 = "f16cde1294b01aba99d51542ca9935af25dedf203eafc6638c42338407b74ff8";
inline constexpr std::size_t kFrameBytes = 64;
inline constexpr std::uint32_t kInitialDelayMs = 4000;
inline constexpr std::uint32_t kReinitDelayMs = 200;
enum class InitProfile : int { None = 0, X1, X1Mini, Apex };
struct Descriptor {
    bool valid = false;
    bool independentRearProtocol = false;
    InitProfile initProfile = InitProfile::None;
    bool role21Orange = false;
};
inline constexpr Descriptor descriptorFor(std::string_view cls) {
    if (cls == "HandheldCompanion.Devices.OneXPlayerX1" ||
        cls == "HandheldCompanion.Devices.OneXPlayerX1AMD" ||
        cls == "HandheldCompanion.Devices.OneXPlayerX1Intel" ||
        cls == "HandheldCompanion.Devices.OneXPlayerX1Pro")
        return {true, true, InitProfile::X1, false};
    if (cls == "HandheldCompanion.Devices.OneXPlayerX1Mini")
        return {true, true, InitProfile::X1Mini, false};
    if (cls == "HandheldCompanion.Devices.OneXPlayerApex")
        return {true, true, InitProfile::Apex, true};
    return {};
}
// Consumer-facing source description shares the exact reader descriptor.
// This grants no device write or default-action suppression verification.
inline constexpr const char* captureRoute(std::string_view cls, std::string_view key) {
    const auto desc = descriptorFor(cls);
    if (!desc.valid) return "unsupported";
    if (key == "l4" || key == "r4" || key == "special") return "onex-vendor-held";
    if (key == "keyboard" || (key == "orange" && desc.role21Orange))
        return "keyboard-vk-or-onex-vendor-held";
    return "keyboard-vk";
}
// APEX exposes three physical special keys plus three firmware gestures.
// Preserve the existing action IDs while counting physical keys only once.
// Exact descriptor admission prevents these gestures changing OP3/X2 catalogs.
inline constexpr std::array<std::string_view, 2> physicalKeyIds(std::string_view cls, std::string_view key) {
    if (descriptorFor(cls).initProfile != InitProfile::Apex) return {};
    if (key == "turbo" || key == "keyboard" || key == "orange") return {key, {}};
    if (key == "orangekb") return {"keyboard", "orange"};
    if (key == "orangetb") return {"turbo", "orange"};
    if (key == "orangelp") return {"orange", {}};
    return {};
}
// Input and output lengths are observed separately. Neither implies the other's layout.
inline constexpr bool reportBytesAllowed(std::size_t n) { return n == 64 || n == 65; }
// HC ReadReportAsync exposes Data without its independent report ID. Keep the
// existing bounded512-byte capture ceiling and source logical-frame minimum14.
inline constexpr bool inputReportBytesAllowed(std::size_t n) { return n >= 15 && n <= 512; }
inline constexpr bool capsAllowed(unsigned vid, unsigned pid, unsigned page, unsigned usage,
                                  unsigned inputBytes, unsigned outputBytes) {
    return vid == 0x1A86 && pid == 0xFE00 && page == 0xFF00 && usage == 1 &&
           inputReportBytesAllowed(inputBytes) && reportBytesAllowed(outputBytes);
}
inline constexpr std::array<std::uint8_t, 59> kX1Page1 = {
    0x02,0x38,0x20,0x01,0x01,
    0x01,0x01,0x01,0,0,0, 0x02,0x01,0x02,0,0,0,
    0x03,0x01,0x03,0,0,0, 0x04,0x01,0x04,0,0,0,
    0x05,0x01,0x05,0,0,0, 0x06,0x01,0x06,0,0,0,
    0x07,0x01,0x07,0,0,0, 0x08,0x01,0x08,0,0,0,
    0x09,0x01,0x09,0,0,0};
inline constexpr std::array<std::uint8_t, 59> kX1Page2 = {
    0x02,0x38,0x20,0x02,0x01,
    0x0A,0x01,0x0A,0,0,0, 0x0B,0x01,0x0B,0,0,0,
    0x0C,0x01,0x0C,0,0,0, 0x0D,0x01,0x0D,0,0,0,
    0x0E,0x01,0x0E,0,0,0, 0x0F,0x01,0x0F,0,0,0,
    0x10,0x01,0x10,0,0,0, 0x22,0x02,0x01,0x67,0,0,
    0x23,0x02,0x01,0x66,0,0};
inline constexpr auto kApexPage1 = [] { auto v = kX1Page1; v[2] = 0x02; return v; }();
inline constexpr auto kApexPage2 = [] { auto v = kX1Page2; v[2] = 0x02; return v; }();
inline constexpr std::array<std::uint8_t, 3> kMiniMotor = {0x01,0x05,0x05};
inline constexpr std::array<std::uint8_t, 10> kApexConfig = {0x01,0x1F,0x40,0x03,0x02,0x03,0,0,0,0x01};
inline constexpr std::array<std::uint8_t, 3> kApexInterceptOff = {0,0x01,0x02};
struct Step {
    std::uint8_t command = 0;
    const std::uint8_t* payload = nullptr;
    std::size_t bytes = 0;
    std::uint32_t delayAfterMs = 0;
};
inline constexpr bool stepAt(InitProfile profile, int step, Step& out) {
    out = {};
    if (step < 0) return false;
    if (profile == InitProfile::X1 && step < 2) {
        const auto& page = step == 0 ? kX1Page1 : kX1Page2;
        out = {0xB4, page.data(), page.size(), step == 0 ? 50u : 0u};
        return true;
    }
    if (profile == InitProfile::X1Mini && step == 0) {
        out = {0xB3, kMiniMotor.data(), kMiniMotor.size(), 0};
        return true;
    }
    if (profile == InitProfile::Apex) {
        if (step < 2) {
            const auto& page = step == 0 ? kApexPage1 : kApexPage2;
            out = {0xB4, page.data(), page.size(), 50}; return true;
        }
        if (step == 2) { out = {0xB2, kApexConfig.data(), kApexConfig.size(), 50}; return true; }
        if (step == 3) { out = {0xB2, kApexInterceptOff.data(), kApexInterceptOff.size(), 0}; return true; }
    }
    return false;
}
inline bool encodeOutput(std::uint8_t command, const std::uint8_t* payload, std::size_t n,
                         std::size_t reportBytes, std::uint8_t* out, std::size_t capacity) {
    if (!out || capacity != reportBytes || !reportBytesAllowed(reportBytes) || n > kFrameBytes - 5 ||
        (n && !payload) || (command != 0xB2 && command != 0xB3 && command != 0xB4)) return false;
    for (std::size_t i = 0; i < capacity; ++i) out[i] = 0;
    auto* frame = out + (reportBytes == 65 ? 1 : 0);
    frame[0] = command; frame[1] = 0x3F; frame[2] = 0x01;
    for (std::size_t i = 0; i < n; ++i) frame[3 + i] = payload[i];
    frame[62] = 0x3F; frame[63] = command;
    return true;
}
inline const std::uint8_t* inputFrame(const std::uint8_t* data, std::size_t n) {
    if (!data || !inputReportBytesAllowed(n)) return nullptr;
    const auto logicalFrame = [](const std::uint8_t* frame, std::size_t size) {
        return size >= 14 && (frame[0] == 0xB2 || frame[0] == 0xB8) &&
            frame[1] == 0x3F && frame[size - 2] == 0x3F;
    };
    // Current HC consumes Report.Data: strip any independently observed report ID.
    // Legacy production consumed the raw64 frame. The command/marker combination
    // makes these two shapes disjoint; output's ID0 never selects an input layout.
    const bool reportThenFrame = logicalFrame(data + 1, n - 1);
    const bool legacyRaw64 = n == 64 && logicalFrame(data, n);
    if (reportThenFrame == legacyRaw64) return nullptr;
    return reportThenFrame ? data + 1 : data;
}
inline constexpr bool reinitStatus(InitProfile profile, const std::uint8_t* frame) {
    if (!frame || frame[0] != 0xB8) return false;
    if (profile == InitProfile::X1Mini) return frame[3] == 0xFE;
    return (profile == InitProfile::X1 || profile == InitProfile::Apex) && frame[5] == 0x20;
}
} // namespace ymcc::capabilities::oem::onex
