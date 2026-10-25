#pragma once

#include <cmath>
#include <cstdint>

// Pure numerical production helpers: no I/O, allocation, JSON, threads, timers,
// device access, persistent state, or lifecycle ownership.
// Numerical authority: ControllerCalibration-20261025/contract/
// axisCalibrationContract.ts (center -> inner/outer -> anti -> output shape).
// Shape names follow that contract; the two conversions are not inverses.
//
// CALLER CONTRACT:
// - Retain the untouched source sample and its original source/capability evidence.
//   Never replace evidence or center-sampling input with a calibrated value.
// - The caller admits source identity, independent stick/analog-trigger capability,
//   ownership, focus, power, and each requested virtual/local route. Config flags
//   request routing; they do not establish any of those permissions. Capability
//   checks follow effective enabled items (master, group, stick side, settings),
//   so disabled retained parameters do not request unsupported capabilities.
// - Compute calibration once from the original sample, before gyro composition;
//   share that result with admitted routes. Enforce final neutral AFTER calibration
//   and gyro. Never recalibrate the final neutral frame (center offsets could move it).
// - These are software output values, NEVER hardware/firmware calibration writes.
// - On error, value retains raw for evidence. The caller must inspect error/ok()
//   and apply its existing failure/neutral policy; an error is not route admission.

namespace ymcc::axisCalibration {

enum class Shape : std::uint8_t {
    Original,
    CircleToSquare,
    SquareToCircle
};

struct Stick {
    double centerX = 0.0;
    double centerY = 0.0;
    int innerDeadzonePct = 0;
    int outerDeadzonePct = 0;
    int antiDeadzonePct = 0;
    // Missing shape in the caller's older configuration means Original.
    Shape outputShape = Shape::Original;
    bool enabled = true;
};

struct Trigger {
    int startDeadzonePct = 0;
    int fullScalePct = 100;
};

struct Config {
    bool enabled = false;
    bool sticksEnabled = true;
    bool triggersEnabled = true;
    bool applyToVirtual = true;
    bool applyToLocal = false;
    Stick left{};
    Stick right{};
    Trigger lt{};
    Trigger rt{};
};

struct NormalizedStick {
    double x = 0.0;
    double y = 0.0;
};

struct NormalizedPad {
    NormalizedStick left{};
    NormalizedStick right{};
    double lt = 0.0;
    double rt = 0.0;
};

struct RawPad {
    std::int16_t lx = 0;
    std::int16_t ly = 0;
    std::int16_t rx = 0;
    std::int16_t ry = 0;
    std::uint8_t lt = 0;
    std::uint8_t rt = 0;
};

enum class Error : std::uint8_t {
    None,
    InvalidConfig,
    InvalidSample
};

struct NormalizedResult {
    NormalizedPad value{};
    Error error = Error::None;
    [[nodiscard]] bool ok() const noexcept { return error == Error::None; }
};

struct RawResult {
    RawPad value{};
    Error error = Error::None;
    [[nodiscard]] bool ok() const noexcept { return error == Error::None; }
};

namespace detail {

inline bool finiteIn(double value, double lower, double upper) noexcept {
    return std::isfinite(value) && value >= lower && value <= upper;
}

inline bool validShape(Shape shape) noexcept {
    return shape == Shape::Original || shape == Shape::CircleToSquare ||
        shape == Shape::SquareToCircle;
}

inline bool validStick(const Stick& stick) noexcept {
    return finiteIn(stick.centerX, -0.15, 0.15) &&
        finiteIn(stick.centerY, -0.15, 0.15) &&
        stick.innerDeadzonePct >= 0 && stick.innerDeadzonePct <= 25 &&
        stick.outerDeadzonePct >= 0 && stick.outerDeadzonePct <= 25 &&
        stick.antiDeadzonePct >= 0 && stick.antiDeadzonePct <= 25 &&
        validShape(stick.outputShape);
}

inline bool validTrigger(const Trigger& trigger) noexcept {
    return trigger.startDeadzonePct >= 0 && trigger.startDeadzonePct <= 99 &&
        trigger.fullScalePct >= 1 && trigger.fullScalePct <= 100 &&
        trigger.fullScalePct > trigger.startDeadzonePct;
}

inline bool identityStick(const Stick& stick) noexcept {
    return !stick.enabled || (stick.centerX == 0.0 && stick.centerY == 0.0 &&
        stick.innerDeadzonePct == 0 && stick.outerDeadzonePct == 0 &&
        stick.antiDeadzonePct == 0 && stick.outputShape == Shape::Original);
}

inline bool identityTrigger(const Trigger& trigger) noexcept {
    return trigger.startDeadzonePct == 0 && trigger.fullScalePct == 100;
}

inline bool bypass(const Config& config) noexcept {
    return !config.enabled || (!config.applyToVirtual && !config.applyToLocal) ||
        (!config.sticksEnabled && !config.triggersEnabled);
}

inline bool identitySettings(const Config& config) noexcept {
    return (!config.sticksEnabled ||
            (identityStick(config.left) && identityStick(config.right))) &&
        (!config.triggersEnabled ||
            (identityTrigger(config.lt) && identityTrigger(config.rt)));
}

inline bool finitePad(const NormalizedPad& pad) noexcept {
    return std::isfinite(pad.left.x) && std::isfinite(pad.left.y) &&
        std::isfinite(pad.right.x) && std::isfinite(pad.right.y) &&
        std::isfinite(pad.lt) && std::isfinite(pad.rt);
}

inline bool validPad(const NormalizedPad& pad) noexcept {
    return finiteIn(pad.left.x, -1.0, 1.0) && finiteIn(pad.left.y, -1.0, 1.0) &&
        finiteIn(pad.right.x, -1.0, 1.0) && finiteIn(pad.right.y, -1.0, 1.0) &&
        finiteIn(pad.lt, 0.0, 1.0) && finiteIn(pad.rt, 0.0, 1.0);
}

inline double clamp(double value, double lower, double upper) noexcept {
    return value < lower ? lower : (value > upper ? upper : value);
}

inline double centerAxis(double value, double center) noexcept {
    if (center == 0.0) return value;
    const double denominator = value >= center ? 1.0 - center : 1.0 + center;
    return clamp((value - center) / denominator, -1.0, 1.0);
}

inline NormalizedStick squareToCircle(const NormalizedStick& value) noexcept {
    const double radius = std::hypot(value.x, value.y);
    // HC ImproveCircularity: the entire unit-circle interior stays untouched.
    if (radius <= 1.0) return value;
    return {value.x / radius, value.y / radius};
}

inline NormalizedStick circleToSquare(const NormalizedStick& value) noexcept {
    const double radius = std::hypot(value.x, value.y);
    if (radius < 1e-5) return {};
    const double absX = std::abs(value.x);
    const double absY = std::abs(value.y);
    const double edge = absX > absY ? absX : absY;
    // Preserve both guards and their order from the shared HC-style contract.
    if (edge <= 1e-5) return value;
    const double scale = (radius < 1.0 ? radius : 1.0) / edge;
    return {value.x * scale, value.y * scale};
}

inline NormalizedStick mapStick(const NormalizedStick& value, const Stick& stick) noexcept {
    if (identityStick(stick)) return value;
    NormalizedStick output{centerAxis(value.x, stick.centerX),
        centerAxis(value.y, stick.centerY)};
    // Do not impose a radial clamp for zero inner/outer settings: Original must
    // preserve square diagonals, including raw (32767, 32767).
    if (stick.innerDeadzonePct != 0 || stick.outerDeadzonePct != 0) {
        const double radius = std::hypot(output.x, output.y);
        const double inner = stick.innerDeadzonePct / 100.0;
        if (radius <= inner || radius == 0.0) return {};
        const double mapped = clamp((radius - inner) /
            (1.0 - stick.outerDeadzonePct / 100.0 - inner), 0.0, 1.0);
        output = {output.x / radius * mapped, output.y / radius * mapped};
    }
    if (stick.antiDeadzonePct != 0) {
        const double radius = std::hypot(output.x, output.y);
        // Anti-deadzone never lifts a true zero or a value removed above.
        if (radius == 0.0) return {};
        const double anti = stick.antiDeadzonePct / 100.0;
        const double scale = ((1.0 - anti) * radius + anti) / radius;
        output = {output.x * scale, output.y * scale};
    }
    if (stick.outputShape == Shape::CircleToSquare) return circleToSquare(output);
    if (stick.outputShape == Shape::SquareToCircle) return squareToCircle(output);
    return output;
}

inline double mapTrigger(double value, const Trigger& trigger) noexcept {
    if (identityTrigger(trigger)) return value;
    return clamp((value * 100.0 - trigger.startDeadzonePct) /
        (trigger.fullScalePct - trigger.startDeadzonePct), 0.0, 1.0);
}

// Public entry points validate all metadata before this helper. Disabled
// parameters remain available for re-enable but never enter numerical mapping.
inline NormalizedPad mapEnabled(const NormalizedPad& raw, const Config& config) noexcept {
    NormalizedPad output = raw;
    if (config.sticksEnabled) {
        if (!identityStick(config.left)) output.left = mapStick(raw.left, config.left);
        if (!identityStick(config.right)) output.right = mapStick(raw.right, config.right);
    }
    if (config.triggersEnabled) {
        if (!identityTrigger(config.lt)) output.lt = mapTrigger(raw.lt, config.lt);
        if (!identityTrigger(config.rt)) output.rt = mapTrigger(raw.rt, config.rt);
    }
    return output;
}

inline double decodeAxis(std::int16_t value) noexcept {
    // Independent denominators retain both signed endpoints without bias.
    return static_cast<double>(value) / (value < 0 ? 32768.0 : 32767.0);
}

// Encoding is private and reached only after finite validation. Clamp in
// floating point before any integer cast. floor(x + .5) rounds ties toward +inf,
// matching JS Math.round rather than C++ round's negative half-away behavior.
inline std::int16_t encodeAxis(double value) noexcept {
    const double bounded = clamp(value, -1.0, 1.0);
    const double scaled = bounded * (bounded < 0.0 ? 32768.0 : 32767.0);
    const double rounded = std::floor(scaled + 0.5);
    return static_cast<std::int16_t>(clamp(rounded, -32768.0, 32767.0));
}

inline std::uint8_t encodeTrigger(double value) noexcept {
    const double rounded = std::floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5);
    return static_cast<std::uint8_t>(clamp(rounded, 0.0, 255.0));
}

} // namespace detail

// All stored metadata is validated even when its item is disabled. Native bool
// fields and integer percentages have fixed types; centers must remain finite.
[[nodiscard]] inline bool valid(const Config& config) noexcept {
    return detail::validStick(config.left) && detail::validStick(config.right) &&
        detail::validTrigger(config.lt) && detail::validTrigger(config.rt);
}

// Effective numerical identity, independent of source/route admission. Invalid
// configs are never identity, even with master/group/side OFF or route bypass.
[[nodiscard]] inline bool identity(const Config& config) noexcept {
    return valid(config) && (detail::bypass(config) || detail::identitySettings(config));
}

// Returns one calibrated value. The caller selects raw or value for EACH route
// using its matching flag and admission gates, without applying calibration again.
[[nodiscard]] inline NormalizedResult applyNormalized(
    const NormalizedPad& raw, const Config& config) noexcept {
    NormalizedResult result{raw, Error::None};
    if (!valid(config)) {
        result.error = Error::InvalidConfig;
        return result;
    }
    if (!detail::validPad(raw)) {
        result.error = Error::InvalidSample;
        return result;
    }
    // Validation performs no mapping. OFF/identity copies the original doubles,
    // including signed zero, before any center/deadzone/shape arithmetic.
    if (detail::bypass(config) || detail::identitySettings(config)) return result;
    result.value = detail::mapEnabled(raw, config);
    // Also reject non-finite intermediates (e.g. a subnormal anti scale). Never
    // let invalid floating-point output proceed to raw integer quantization.
    // Keep finite TS results unchanged, including last-bit boundary roundoff;
    // raw encoders saturate safely before their integer casts.
    if (!detail::finitePad(result.value)) {
        result.value = raw;
        result.error = Error::InvalidSample;
    }
    return result;
}

[[nodiscard]] inline RawResult applyRaw(const RawPad& raw, const Config& config) noexcept {
    RawResult result{raw, Error::None};
    if (!valid(config)) {
        result.error = Error::InvalidConfig;
        return result;
    }
    // Master OFF, route bypass and effective identity return before ANY
    // normalization, mapping, shape conversion, or integer quantization.
    if (detail::bypass(config) || detail::identitySettings(config)) return result;
    const bool leftEdited = config.sticksEnabled && !detail::identityStick(config.left);
    const bool rightEdited = config.sticksEnabled && !detail::identityStick(config.right);
    const bool ltEdited = config.triggersEnabled && !detail::identityTrigger(config.lt);
    const bool rtEdited = config.triggersEnabled && !detail::identityTrigger(config.rt);
    // Only active, nonidentity components enter this numerical working copy.
    // Disabled/default components keep their original SHORT/BYTE in result;
    // their zero placeholders below are never mapped or written back.
    NormalizedPad normalized{};
    if (leftEdited)
        normalized.left = {detail::decodeAxis(raw.lx), detail::decodeAxis(raw.ly)};
    if (rightEdited)
        normalized.right = {detail::decodeAxis(raw.rx), detail::decodeAxis(raw.ry)};
    if (ltEdited) normalized.lt = static_cast<double>(raw.lt) / 255.0;
    if (rtEdited) normalized.rt = static_cast<double>(raw.rt) / 255.0;
    const NormalizedPad mapped = detail::mapEnabled(normalized, config);
    if (!detail::finitePad(mapped)) {
        result.error = Error::InvalidSample;
        return result;
    }
    // Quantize only active coordinates that actually changed. A center-only
    // edit also preserves the other coordinate of the same stick exactly.
    if (leftEdited && mapped.left.x != normalized.left.x)
        result.value.lx = detail::encodeAxis(mapped.left.x);
    if (leftEdited && mapped.left.y != normalized.left.y)
        result.value.ly = detail::encodeAxis(mapped.left.y);
    if (rightEdited && mapped.right.x != normalized.right.x)
        result.value.rx = detail::encodeAxis(mapped.right.x);
    if (rightEdited && mapped.right.y != normalized.right.y)
        result.value.ry = detail::encodeAxis(mapped.right.y);
    if (ltEdited && mapped.lt != normalized.lt)
        result.value.lt = detail::encodeTrigger(mapped.lt);
    if (rtEdited && mapped.rt != normalized.rt)
        result.value.rt = detail::encodeTrigger(mapped.rt);
    return result;
}

} // namespace ymcc::axisCalibration
