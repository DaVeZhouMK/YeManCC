#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// Pure IMU decoding of one already-normalized logical report. The caller proves
// exact model/VID/PID/usage, one actual transport report and framing. This header
// never guesses/removes a physical Report ID, enumerates HID, or writes a mode.
namespace ymcc::legion_input {

enum class Protocol { Tablet, GoS };
struct MotionAxes { float gx = 0, gy = 0, gz = 0, ax = 0, ay = 0, az = 0; };

namespace detail {
inline int signed16(std::uint32_t value) {
    return value >= 0x8000u ? static_cast<int>(value) - 0x10000 : static_cast<int>(value);
}
inline int bigEndian16(const std::uint8_t* logical, std::size_t offset) {
    return signed16((static_cast<std::uint32_t>(logical[offset]) << 8) | logical[offset + 1]);
}
inline int littleEndian16(const std::uint8_t* logical, std::size_t offset) {
    return signed16(static_cast<std::uint32_t>(logical[offset]) |
                    (static_cast<std::uint32_t>(logical[offset + 1]) << 8));
}
inline constexpr float accelScale = 4.0f / 32767.0f;
inline constexpr float gyroScale = 2000.0f / 32767.0f;
} // namespace detail

// capacity describes accessible storage; actualBytes is the delivered logical
// report count, not buffer capacity. Only actualBytes can establish a full core.
// Tablet fixed HC IMU indices end at left 46/right 59 (47/60-byte cores).
// Logical byte1 is not an IMU shift flag: HC AttachDetails adjusts only non-IMU
// fields. GoS's HC 33-byte reader capacity is not an exact report-count gate.
inline std::optional<MotionAxes> decodeLogical(Protocol protocol, const std::uint8_t* logical,
                                               std::size_t capacity, std::size_t actualBytes,
                                               unsigned sensorIndex) {
    if (!logical || actualBytes > capacity) return std::nullopt;
    MotionAxes axes;
    switch (protocol) {
    case Protocol::Tablet: {
        if (sensorIndex > 1) return std::nullopt;
        const std::size_t a = sensorIndex == 0 ? 35u : 48u;
        const std::size_t g = sensorIndex == 0 ? 41u : 54u;
        if (a + 5 >= actualBytes || g + 5 >= actualBytes) return std::nullopt;
        if (sensorIndex == 0) {
            axes.ax = static_cast<float>(detail::bigEndian16(logical, a)) * -detail::accelScale;
            axes.az = static_cast<float>(detail::bigEndian16(logical, a + 2)) * -detail::accelScale;
            axes.ay = static_cast<float>(detail::bigEndian16(logical, a + 4)) * -detail::accelScale;
            axes.gx = static_cast<float>(detail::bigEndian16(logical, g)) * -detail::gyroScale;
            axes.gz = static_cast<float>(detail::bigEndian16(logical, g + 2)) * -detail::gyroScale;
            axes.gy = static_cast<float>(detail::bigEndian16(logical, g + 4)) * -detail::gyroScale;
        } else {
            axes.ax = static_cast<float>(detail::bigEndian16(logical, a + 2)) * -detail::accelScale;
            axes.az = static_cast<float>(detail::bigEndian16(logical, a)) * detail::accelScale;
            axes.ay = static_cast<float>(detail::bigEndian16(logical, a + 4)) * -detail::accelScale;
            axes.gx = static_cast<float>(detail::bigEndian16(logical, g + 2)) * -detail::gyroScale;
            axes.gz = static_cast<float>(detail::bigEndian16(logical, g)) * detail::gyroScale;
            axes.gy = static_cast<float>(detail::bigEndian16(logical, g + 4)) * -detail::gyroScale;
        }
        return axes;
    }
    case Protocol::GoS:
        if (sensorIndex != 0 || actualBytes < 26) return std::nullopt;
        axes.ax = static_cast<float>(detail::littleEndian16(logical, 14)) * -detail::accelScale;
        axes.az = static_cast<float>(detail::littleEndian16(logical, 16)) * -detail::accelScale;
        axes.ay = static_cast<float>(detail::littleEndian16(logical, 18)) * -detail::accelScale;
        axes.gx = static_cast<float>(detail::littleEndian16(logical, 20)) * -detail::gyroScale;
        axes.gz = static_cast<float>(detail::littleEndian16(logical, 22)) * detail::gyroScale;
        axes.gy = static_cast<float>(detail::littleEndian16(logical, 24)) * -detail::gyroScale;
        return axes;
    default:
        return std::nullopt;
    }
}

} // namespace ymcc::legion_input
