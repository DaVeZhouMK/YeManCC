#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// Pure protocol data only. Reference: HC a250b19a4b7e29dac3b383f2d1ac517d1af43e4e,
// OneXPlayerX1 / X2 / X2MiniPro / 3. No discovery, handles, timers or I/O.
namespace ymcc::onex_x2 {

enum class Model : std::uint8_t { Unsupported, X2, X2MiniPro, OneXPlayer3 };
enum class Button : std::uint8_t { None, Turbo, Keyboard, Home, L4, R4 };

constexpr bool supported(Model model) noexcept {
    return model == Model::X2 || model == Model::X2MiniPro || model == Model::OneXPlayer3;
}

// Exact short HC class names and canonical machineIdentity.deviceClass values.
// This is an explicit allowlist, not namespace stripping or fuzzy SMBIOS matching.
constexpr Model modelForClass(std::string_view cls) noexcept {
    if (cls == "OneXPlayerX2" || cls == "HandheldCompanion.Devices.OneXPlayerX2") return Model::X2;
    if (cls == "OneXPlayerX2MiniPro" || cls == "HandheldCompanion.Devices.OneXPlayerX2MiniPro") return Model::X2MiniPro;
    if (cls == "OneXPlayer3" || cls == "HandheldCompanion.Devices.OneXPlayer3") return Model::OneXPlayer3;
    return Model::Unsupported;
}

constexpr bool matchesCollection(Model model, std::uint16_t vid, std::uint16_t pid,
                                 std::uint16_t usagePage, std::uint16_t usage) noexcept {
    if (!supported(model) || vid != 0x1A86 || usagePage != 0xFF00 || usage != 0x0001)
        return false;
    return pid == 0xFE00 || (pid == 0x1305 && model != Model::OneXPlayer3);
}

constexpr Button mapButton(Model model, std::uint8_t id) noexcept {
    if (!supported(model)) return Button::None;
    switch (id) {
    case 0x20: return Button::Turbo;
    case 0x21: return model == Model::X2MiniPro ? Button::Keyboard : Button::Home;
    case 0x22: return Button::L4;
    case 0x23: return Button::R4;
    // OXP3 inherits X2's 0x21 Home and overrides 0x24 to Home as well.
    case 0x24: return model == Model::X2 ? Button::Keyboard : Button::Home;
    default: return Button::None;
    }
}

constexpr bool clickOnly(Button button) noexcept {
    return button == Button::Turbo || button == Button::Keyboard || button == Button::Home;
}
inline constexpr unsigned clickPulseMs = 100; // Reference pulse duration; no timer is started.

enum class FrameKind : std::uint8_t { Button, Status };
struct LogicalFrame {
    FrameKind kind = FrameKind::Button;
    std::uint8_t buttonId = 0;
    Button button = Button::None;
    bool pressed = false;
    std::uint8_t status = 0;
};

// data MUST be HidReport.Data, with the report ID already removed by the caller.
// This function does not accept/detect/strip raw HID report IDs or guess a 65-byte
// input offset. HC accepts logical data >=14 bytes, markers at [1] and [size-2],
// and treats only [12]==1 as pressed. Its final byte is not a validated command.
// Unknown button IDs remain decoded Button::None, so callers can retain evidence
// without manufacturing input. Status [5]==0x20 requests reconfiguration in HC;
// this decoder only returns the status and performs no lifecycle action.
inline std::optional<LogicalFrame> decodeLogicalFrame(Model model, const std::uint8_t* data,
                                                      std::size_t size) noexcept {
    if (!supported(model) || data == nullptr || size < 14) return std::nullopt;
    if (data[1] != 0x3F || data[size - 2] != 0x3F) return std::nullopt;
    LogicalFrame result;
    if (data[0] == 0xB2) {
        result.kind = FrameKind::Button;
        result.buttonId = data[6];
        result.button = mapButton(model, result.buttonId);
        result.pressed = data[12] == 0x01;
    } else if (data[0] == 0xB8) {
        result.kind = FrameKind::Status;
        result.status = data[5];
    } else {
        return std::nullopt;
    }
    return result;
}

struct InitStep {
    std::uint8_t command = 0;
    std::array<std::uint8_t, 59> payload{};
    std::size_t payloadSize = 0;
    unsigned delayAfterMs = 0;
};

// Zero-based sequence of controller remap messages, preset 0x01. Delays describe
// the fixed source sequence only. Neither the 4000ms insertion wait nor device
// bootstrap/WMI takeover, scheduling, retries or shutdown are part of this API.
constexpr std::optional<InitStep> initStep(Model model, std::size_t index) noexcept {
    if (!supported(model)) return std::nullopt;
    const bool mini = model == Model::X2MiniPro;
    if (index >= (mini ? 4u : 3u)) return std::nullopt;
    InitStep result;
    if (index == 3) {
        result.command = 0xB2;
        result.payload = {0x00, 0x01, 0x02}; // BuildIntercept(false), Mini only.
        result.payloadSize = 3;
        return result;
    }
    result.command = 0xB4;
    result.delayAfterMs = 50;
    if (index == 0) {
        result.payload = {
            0x02, 0x38, 0x20, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
            0x02, 0x01, 0x02, 0x00, 0x00, 0x00,
            0x03, 0x01, 0x03, 0x00, 0x00, 0x00,
            0x04, 0x01, 0x04, 0x00, 0x00, 0x00,
            0x05, 0x01, 0x05, 0x00, 0x00, 0x00,
            0x06, 0x01, 0x06, 0x00, 0x00, 0x00,
            0x07, 0x01, 0x07, 0x00, 0x00, 0x00,
            0x08, 0x01, 0x08, 0x00, 0x00, 0x00,
            0x09, 0x01, 0x09, 0x00, 0x00, 0x00
        };
        result.payloadSize = 59;
    } else if (index == 1) {
        result.payload = {
            0x02, 0x38, 0x20, 0x02, 0x01,
            0x0A, 0x01, 0x0A, 0x00, 0x00, 0x00,
            0x0B, 0x01, 0x0B, 0x00, 0x00, 0x00,
            0x0C, 0x01, 0x0C, 0x00, 0x00, 0x00,
            0x0D, 0x01, 0x0D, 0x00, 0x00, 0x00,
            0x0E, 0x01, 0x0E, 0x00, 0x00, 0x00,
            0x0F, 0x01, 0x0F, 0x00, 0x00, 0x00,
            0x10, 0x01, 0x10, 0x00, 0x00, 0x00,
            0x22, 0x02, 0x01, 0x67, 0x00, 0x00,
            0x23, 0x02, 0x01, 0x66, 0x00, 0x00
        };
        if (mini) {
            result.payload[50] = 0x68; // M1 -> F15
            result.payload[56] = 0x69; // M2 -> F16
        }
        result.payloadSize = 59;
    } else {
        // X2 and OXP3 inherit this third B4 page verbatim, including 0x25 -> 0x21.
        result.payload = {
            0x02, 0x38, 0x20, 0x03, 0x01,
            0x24, 0x02, 0x02, 0x05, 0x00, 0x00,
            0x25, 0x01, 0x21, 0x00, 0x00, 0x00
        };
        result.payloadSize = 17;
    }
    if (mini) result.payload[2] = 0x02;
    return result;
}

struct EncodedReport {
    std::array<std::uint8_t, 65> bytes{};
    std::size_t size = 0;
};

// Only the two source-documented output lengths are admitted. A 64-byte report
// is the frame itself; 65 is report ID 0x00 plus the same 64-byte frame. This says
// nothing about the input-report length/layout. Payload is limited to 59 bytes.
inline std::optional<EncodedReport> encodeReport(std::uint8_t command,
                                                const std::uint8_t* payload,
                                                std::size_t payloadSize,
                                                std::size_t reportLength) noexcept {
    if ((reportLength != 64 && reportLength != 65) || payloadSize > 59 ||
        (payloadSize != 0 && payload == nullptr)) return std::nullopt;
    EncodedReport result;
    result.size = reportLength;
    const std::size_t offset = reportLength == 65 ? 1u : 0u;
    result.bytes[offset] = command;
    result.bytes[offset + 1] = 0x3F;
    result.bytes[offset + 2] = 0x01;
    for (std::size_t i = 0; i < payloadSize; ++i) result.bytes[offset + 3 + i] = payload[i];
    result.bytes[offset + 62] = 0x3F;
    result.bytes[offset + 63] = command;
    return result;
}

inline std::optional<EncodedReport> encodeReport(const InitStep& step,
                                                std::size_t reportLength) noexcept {
    return encodeReport(step.command, step.payload.data(), step.payloadSize, reportLength);
}

} // namespace ymcc::onex_x2