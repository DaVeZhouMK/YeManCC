#pragma once
#include <cstdint>

namespace ymcc::gyro_calibration {
struct Policy { float stillMagnitudeDps; std::uint64_t pollWindowMs; };
constexpr Policy policy(bool msi) { return msi ? Policy{4.0f,15000} : Policy{2.5f,5000}; }
// Calibration may use the MSI's own proven SensorAPI pair while the controller
// is unavailable. This evidence never admits a virtual report or game input.
inline bool freshPair(std::uint64_t now, std::uint64_t gyroReceipt,
    std::uint64_t accelReceipt, std::uint64_t gyroSequence, std::uint64_t accelSequence) {
    return gyroSequence && accelSequence && gyroReceipt && accelReceipt &&
        now>=gyroReceipt && now>=accelReceipt && now-gyroReceipt<=350 && now-accelReceipt<=350;
}
inline bool sensorCalibrationAdmitted(bool msi, bool sensorApiPair, bool wanted,
    bool gyro, bool accel, bool sameProvider, bool selectorBound,
    std::uint64_t gyroGeneration, std::uint64_t accelGeneration,
    std::uint64_t activeGeneration, bool fresh) {
    return msi && sensorApiPair && wanted && gyro && accel && sameProvider &&
        selectorBound && gyroGeneration && gyroGeneration==accelGeneration &&
        gyroGeneration==activeGeneration && fresh;
}
}
