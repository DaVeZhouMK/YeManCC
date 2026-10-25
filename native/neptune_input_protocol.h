#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Pure logical NCInput decoder. NOT a transport, report-ID stripper or device binding.
// Oracle: shipped steam-hidapi.net.dll SHA256
// 2d1d03847ff4f90b6ca7059d55cf7f9cae40eafd31f01cc9974d514de76c52f9
// and Valkirie/HandheldCompanion commit 06c0b9544db2b1f39abf9cd3796225ccfb096103:
// steam-hidapi.net/{Hid/HidEnums.cs,NeptuneControllerEnums.cs,
// NeptuneControllerInputState.cs,NeptuneController.cs}; hidapi.net/HidDevice.cs.
// See tools/fixtures/neptune_input_protocol/README.md and source-bindings.json
// in the selftest evidence for exact source bytes, Git blobs and layout oracles.
namespace ymcc::neptune_input {

inline constexpr std::size_t kNCInputSize = 60;
inline constexpr std::size_t kButtonCount = 28;
inline constexpr std::size_t kAxisCount = 22;

// Numeric values preserve the shipped NeptuneControllerButton enum, not wire bits.
enum class Button : std::uint8_t {
    BtnX = 1, BtnY, BtnA, BtnB, BtnMenu, BtnOptions, BtnSteam, BtnQuickAccess,
    BtnDpadUp, BtnDpadLeft, BtnDpadRight, BtnDpadDown,
    BtnL1, BtnR1, BtnL2, BtnR2, BtnL4, BtnR4, BtnL5, BtnR5,
    BtnRPadPress, BtnLPadPress, BtnRPadTouch, BtnLPadTouch,
    BtnRStickPress, BtnLStickPress, BtnRStickTouch, BtnLStickTouch
};

enum class Axis : std::uint8_t {
    LeftStickX = 1, LeftStickY, RightStickX, RightStickY,
    LeftPadX, LeftPadY, RightPadX, RightPadY, LeftPadPressure, RightPadPressure,
    L2, R2, GyroAccelX, GyroAccelY, GyroAccelZ, GyroRoll, GyroPitch, GyroYaw,
    Q1, Q2, Q3, Q4
};

// Selection is an explicit caller assertion about already identified logical data.
// Default/unverified data is refused; byte[0] is NEVER interpreted as a Win32 prefix.
enum class BufferKind { Unverified, LogicalNCInput };
enum class Error { None, UnverifiedBuffer, NullBuffer, InvalidActualBytes,
                   ShortPacket, InvalidHeader, OtherEventType };

struct Pad { bool touch; bool press; std::int16_t x; std::int16_t y; std::int16_t pressure; };
struct Stick { bool touch; bool press; std::int16_t x; std::int16_t y; };
struct RawGyro { std::int16_t pitch; std::int16_t yaw; std::int16_t roll; };
struct RawAccel { std::int16_t x; std::int16_t y; std::int16_t z; };

struct Input {
    std::uint32_t seq = 0;
    // bit (canonical Button value - 1); reserved wire bits never become buttons.
    std::uint32_t allButtons = 0;
    // index (canonical Axis value - 1), raw signed short units, including triggers.
    std::array<std::int16_t, kAxisCount> axes{};

    bool button(Button b) const noexcept {
        const auto id = static_cast<unsigned>(b);
        return id >= 1 && id <= kButtonCount &&
            (allButtons & (std::uint32_t{1} << (id - 1))) != 0;
    }
    std::int16_t axis(Axis a) const noexcept {
        const auto id = static_cast<unsigned>(a);
        return id >= 1 && id <= kAxisCount ? axes[id - 1] : 0;
    }
    Pad leftPad() const noexcept {
        return {button(Button::BtnLPadTouch), button(Button::BtnLPadPress),
            axis(Axis::LeftPadX), axis(Axis::LeftPadY), axis(Axis::LeftPadPressure)};
    }
    Pad rightPad() const noexcept {
        return {button(Button::BtnRPadTouch), button(Button::BtnRPadPress),
            axis(Axis::RightPadX), axis(Axis::RightPadY), axis(Axis::RightPadPressure)};
    }
    Stick leftStick() const noexcept {
        return {button(Button::BtnLStickTouch), button(Button::BtnLStickPress),
            axis(Axis::LeftStickX), axis(Axis::LeftStickY)};
    }
    Stick rightStick() const noexcept {
        return {button(Button::BtnRStickTouch), button(Button::BtnRStickPress),
            axis(Axis::RightStickX), axis(Axis::RightStickY)};
    }
    std::int16_t leftTrigger() const noexcept { return axis(Axis::L2); }
    std::int16_t rightTrigger() const noexcept { return axis(Axis::R2); }
    RawGyro rawGyro() const noexcept {
        return {axis(Axis::GyroPitch), axis(Axis::GyroYaw), axis(Axis::GyroRoll)};
    }
    RawAccel rawAccel() const noexcept {
        return {axis(Axis::GyroAccelX), axis(Axis::GyroAccelY), axis(Axis::GyroAccelZ)};
    }
};

struct Result {
    Error error = Error::UnverifiedBuffer;
    Input input{}; // Failure always has empty output, with no previous-packet tail.
    explicit operator bool() const noexcept { return error == Error::None; }
};

namespace detail {
inline std::int16_t readI16LE(const std::uint8_t* p) noexcept {
    const std::uint32_t u = std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8);
    const std::int32_t value = u >= 0x8000u ? static_cast<std::int32_t>(u) - 65536
                                           : static_cast<std::int32_t>(u);
    return static_cast<std::int16_t>(value); // in range; no unsigned-to-signed wrap assumption
}
inline std::uint32_t readU32LE(const std::uint8_t* p) noexcept {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) |
        (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
}
struct ButtonBit { Button button; std::size_t offset; std::uint8_t mask; };
inline constexpr std::array<ButtonBit, kButtonCount> buttonBits{{
    {Button::BtnX, 8, 0x40}, {Button::BtnY, 8, 0x10},
    {Button::BtnA, 8, 0x80}, {Button::BtnB, 8, 0x20},
    {Button::BtnMenu, 9, 0x10}, {Button::BtnOptions, 9, 0x40},
    {Button::BtnSteam, 9, 0x20}, {Button::BtnQuickAccess, 14, 0x04},
    {Button::BtnDpadUp, 9, 0x01}, {Button::BtnDpadLeft, 9, 0x04},
    {Button::BtnDpadRight, 9, 0x02}, {Button::BtnDpadDown, 9, 0x08},
    {Button::BtnL1, 8, 0x08}, {Button::BtnR1, 8, 0x04},
    {Button::BtnL2, 8, 0x02}, {Button::BtnR2, 8, 0x01},
    {Button::BtnL4, 13, 0x02}, {Button::BtnR4, 13, 0x04},
    {Button::BtnL5, 9, 0x80}, {Button::BtnR5, 10, 0x01},
    {Button::BtnRPadPress, 10, 0x04}, {Button::BtnLPadPress, 10, 0x02},
    {Button::BtnRPadTouch, 10, 0x10}, {Button::BtnLPadTouch, 10, 0x08},
    {Button::BtnRStickPress, 11, 0x04}, {Button::BtnLStickPress, 10, 0x40},
    {Button::BtnRStickTouch, 13, 0x80}, {Button::BtnLStickTouch, 13, 0x40}
}};
// NCInput Marshal offsets, in canonical Axis order. rpad_x is 20 (0x14),
// despite the stale 0x13 comment in the historical C# declaration.
inline constexpr std::array<std::size_t, kAxisCount> axisOffsets{{
    48, 50, 52, 54, 16, 18, 20, 22, 56, 58, 44, 46,
    24, 26, 28, 34, 30, 32, 36, 38, 40, 42
}};
} // namespace detail

// capacity bounds accessible memory; actualBytes is the actual logical byte count.
// The complete packed NCInput core is 60 bytes. Any actualBytes >= 60 within
// capacity is accepted after the logical header gate; all trailing bytes are
// opaque and never read. The sources impose no per-packet size whitelist.
// Known wrapper capacities do not constrain logical actualBytes or prove any
// Windows/native hidapi framing, native report length, physical support or bind.
inline Result decode(const std::uint8_t* logical, std::size_t capacity,
                     std::size_t actualBytes,
                     BufferKind kind = BufferKind::Unverified) noexcept {
    if (kind != BufferKind::LogicalNCInput) return {Error::UnverifiedBuffer, {}};
    if (logical == nullptr) return {Error::NullBuffer, {}};
    if (actualBytes > capacity) return {Error::InvalidActualBytes, {}};
    if (actualBytes < kNCInputSize) return {Error::ShortPacket, {}};
    if (logical[0] != 1 || logical[1] != 0) return {Error::InvalidHeader, {}};
    if (logical[2] != 9) return {Error::OtherEventType, {}};
    Result result{Error::None, {}};
    result.input.seq = detail::readU32LE(logical + 4);
    for (const auto& bit : detail::buttonBits) {
        if ((logical[bit.offset] & bit.mask) != 0)
            result.input.allButtons |= std::uint32_t{1} << (static_cast<unsigned>(bit.button) - 1);
    }
    for (std::size_t i = 0; i < kAxisCount; ++i)
        result.input.axes[i] = detail::readI16LE(logical + detail::axisOffsets[i]);
    return result;
}

} // namespace ymcc::neptune_input
