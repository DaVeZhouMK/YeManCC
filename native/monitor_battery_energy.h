#pragma once
// Generic fallback in the sole native monitor publisher: no timer, thread or UI reader.
// Capacity intervals and an explicitly approximate package-power fallback; never fake a sensor.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ymcc::monitorbattery {
struct Power {
    bool valid = false, estimated = false;
    double watts = 0;
    std::uint64_t windowMs = 0;
    const char* source = "unavailable";
};
struct Observation {
    std::uint64_t tickMs = 0, identity = 0;
    double capacityMwh = -1;
    bool valid = false, acKnown = false, ac = false, charging = false, discharging = false;
};
inline bool eligible(bool top, bool hasBattery, int percent) {
    return top && hasBattery && percent >= 0 && percent <= 100;
}
inline Power measured(double watts, const char* source) {
    return std::isfinite(watts) && std::abs(watts) <= 250
        ? Power{true, false, watts, 0, source} : Power{};
}
inline Power measuredRate(double watts, const char* source, bool acKnown, bool ac,
                          bool charging, bool discharging) {
    auto power = measured(watts, source);
    if (!power.valid || (charging && discharging) ||
        (watts > 0 && ((acKnown && !ac) || discharging)) || (watts < 0 && charging)) return {};
    // Zero while actively charging/discharging is a missing-rate firmware case.
    // A confirmed AC idle 0 W is real data, not an unavailable sentinel.
    if (watts == 0 && (!acKnown || !ac || charging || discharging)) return {};
    return power;
}
// Observe complete change-to-change energy intervals, not an arbitrary 30 s
// slice. A coarse 1% (800 mWh) update must not become a 96 W / 30 s spike.
class EnergyCounter {
    bool baseline_ = false, anchored_ = false, ac_ = false, charging_ = false, discharging_ = false;
    std::uint64_t identity_ = 0, start_ = 0, last_ = 0, changedAt_ = 0;
    double capacity_ = 0, observed_ = 0;
    Power result_;
    void begin(const Observation& o) {
        reset(); baseline_ = true; identity_ = o.identity; ac_ = o.ac;
        charging_ = o.charging; discharging_ = o.discharging;
        start_ = last_ = changedAt_ = o.tickMs; capacity_ = observed_ = o.capacityMwh;
    }
public:
    void reset() { baseline_ = anchored_ = false; identity_ = start_ = last_ = changedAt_ = 0;
        capacity_ = observed_ = 0; result_ = {}; }
    Power update(const Observation& o) {
        if (!o.valid || !o.acKnown || !o.identity || !std::isfinite(o.capacityMwh) ||
            o.capacityMwh < 0 || o.capacityMwh > 1000000 || (o.charging && o.discharging)) {
            reset(); return {};
        }
        if (!baseline_ || identity_ != o.identity || ac_ != o.ac ||
            charging_ != o.charging || discharging_ != o.discharging ||
            o.tickMs < last_ || o.tickMs - last_ > 6000 || o.tickMs - start_ > 600000) {
            begin(o); return {};
        }
        last_ = o.tickMs;
        const double step = o.capacityMwh - observed_;
        if ((step > 0 && (!o.ac || o.discharging)) || (step < 0 && o.charging)) {
            begin(o); return {}; // Calibration/direction change cannot retain an old estimate.
        }
        if (step == 0) {
            const auto elapsed = o.tickMs - start_;
            if (o.ac && !o.charging && !o.discharging && elapsed >= 30000) {
                result_ = {true, true, 0, elapsed, "energy-delta"}; start_ = o.tickMs;
            } else if (result_.valid && o.tickMs - changedAt_ >
                       (result_.windowMs * 2 > 60000 ? result_.windowMs * 2 : 60000)) result_ = {};
            // Flat capacity while actively discharging/charging is quantization,
            // not evidence of a real 0 W reading.
            return result_;
        }
        observed_ = o.capacityMwh; changedAt_ = o.tickMs;
        if (!anchored_) {
            anchored_ = true; start_ = o.tickMs; capacity_ = o.capacityMwh;
            return {}; // Discard the first partially observed reporting interval.
        }
        const auto elapsed = o.tickMs - start_;
        if (elapsed < 30000) return result_;
        const double watts = (o.capacityMwh - capacity_) * 3600.0 / static_cast<double>(elapsed);
        start_ = o.tickMs; capacity_ = o.capacityMwh;
        if (!std::isfinite(watts) || std::abs(watts) > 250) { result_ = {}; return {}; }
        result_ = {true, true, std::abs(watts) < .05 ? 0 : watts, elapsed, "energy-delta"};
        return result_;
    }
};
struct ModelObservation {
    std::uint64_t tickMs = 0;
    bool eligible = false, acKnown = false, ac = true, charging = false, packageFresh = false;
    double packageWatts = 0;
};
// User-provided handheld heuristic, not CTW, a calibrated meter, or TDP limit.
// Use the already collected live CPU/APU package watts, DC only. Below 10 W
// use 1.5x; 20/30/40/60 W use 1.7/1.9/2.0/2.2x. This does not model charging.
inline double packageMultiplier(double watts) {
    constexpr double points[][2] = {{10,1.5},{20,1.7},{30,1.9},{40,2.0},{60,2.2}};
    if (watts <= points[0][0]) return points[0][1];
    for (std::size_t i = 1; i < 5; ++i) if (watts <= points[i][0]) {
        const auto& a = points[i-1]; const auto& b = points[i];
        return a[1] + (watts-a[0])*(b[1]-a[1])/(b[0]-a[0]);
    }
    return 2.2;
}
class PackagePowerModel {
    bool ready_ = false; std::uint64_t last_ = 0; double average_ = 0;
public:
    void reset() { ready_ = false; last_ = 0; average_ = 0; }
    Power update(const ModelObservation& o) {
        // The heuristic is deliberately bounded to handheld-range package power.
        // Missing/stale/zero CPU data cannot turn a percentage into measured watts.
        if (!o.eligible || !o.acKnown || o.ac || o.charging || !o.packageFresh ||
            !std::isfinite(o.packageWatts) || o.packageWatts <= 0 || o.packageWatts > 65) {
            reset(); return {};
        }
        if (!ready_ || o.tickMs < last_ || o.tickMs - last_ > 6000) {
            average_ = o.packageWatts; ready_ = true;
        } else if (o.tickMs != last_) {
            const double ms = static_cast<double>(o.tickMs - last_);
            average_ += (o.packageWatts - average_) * ms / (4000.0 + ms);
        }
        last_ = o.tickMs;
        return {true, true, -average_ * packageMultiplier(average_), 0, "package-power-model"};
    }
};
inline Power selectFallback(const Power& energy, const Power& model) {
    // Real Windows Rate is never clamped or replaced by a TDP/package heuristic.
    // Reject a suspect *derived* energy spike, rather than clip it into fake truth.
    if (energy.valid && (!energy.estimated || !model.valid || energy.watts >= 0 ||
                         -energy.watts <= -model.watts * 1.25 + 5)) return energy;
    return model;
}
struct EnergyReading { bool valid = false, estimated = false; double remainingMwh = -1; };
struct RemainingTime {
    bool valid = false, estimated = false; double minutes = -1;
    const char* source = "unavailable";
};
inline EnergyReading remainingEnergy(double absoluteMwh, double fullMwh, int percent) {
    const bool fullValid = std::isfinite(fullMwh) && fullMwh > 0 && fullMwh <= 1000000;
    // A driver default 0 while Windows still confirms non-empty charge is not
    // evidence of an empty battery. Fall back to reported full capacity + SOC,
    // or unknown, rather than fabricate 0 minutes from a missing counter.
    const bool zeroConflict = absoluteMwh == 0 && percent > 0 && percent <= 100;
    if (!zeroConflict && std::isfinite(absoluteMwh) && absoluteMwh >= 0 && absoluteMwh <= 1000000 &&
        (!fullValid || absoluteMwh <= fullMwh * 1.05)) return {true, false, absoluteMwh};
    if (fullValid && percent >= 0 && percent <= 100) return {true, true, fullMwh * percent / 100.0};
    return {};
}
inline RemainingTime remainingTime(const Power& power, const EnergyReading& energy,
                                  bool batteryConfirmed, bool acKnown, bool ac, bool charging,
                                  double sensorMinutes = -1) {
    if (!batteryConfirmed || !acKnown || ac || charging) return {};
    if (power.valid && power.watts < -.05 && energy.valid &&
        std::isfinite(energy.remainingMwh) && energy.remainingMwh >= 0 && energy.remainingMwh <= 1000000) {
        const double minutes = energy.remainingMwh * .06 / -power.watts;
        if (std::isfinite(minutes) && minutes >= 0 && minutes <= 10080)
            return {true, true, minutes, "energy-power"}; // Future runtime is always a forecast.
        return {};
    }
    // A model needs real energy/full capacity; never infer battery Wh from TDP.
    // A fresh OS/HWiNFO runtime forecast may be retained for a real discharge rate.
    if (power.valid && !power.estimated && power.watts < -.05 &&
        std::isfinite(sensorMinutes) && sensorMinutes >= 0 && sensorMinutes <= 10080)
        return {true, true, sensorMinutes, "sensor-estimate"};
    return {};
}
struct BatteryReading {
    bool absoluteCapacity = true;
    double capacityMwh = -1, watts = std::numeric_limits<double>::quiet_NaN();
    bool charging = false, discharging = false;
};
struct CombinedBattery {
    bool valid = false, capacityValid = false, charging = false, discharging = false;
    double capacityMwh = -1;
    Power power;
};
// Never mix relative units with mWh/mW or publish a partial multi-battery total.
inline CombinedBattery combine(const BatteryReading* readings, std::size_t count, bool acKnown, bool ac) {
    if (!readings || !count || count > 16) return {};
    CombinedBattery out; out.valid = out.capacityValid = true;
    bool allRates = true, anyCharging = false, anyDischarging = false;
    double watts = 0, capacity = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& r = readings[i];
        if (!r.absoluteCapacity || (r.charging && r.discharging)) return {};
        if (!std::isfinite(r.capacityMwh) || r.capacityMwh < 0 || r.capacityMwh > 1000000) out.capacityValid = false;
        else capacity += r.capacityMwh;
        auto rate = measuredRate(r.watts, "windows-rate", acKnown, ac, r.charging, r.discharging);
        allRates = allRates && rate.valid; if (rate.valid) watts += rate.watts;
        anyCharging = anyCharging || r.charging; anyDischarging = anyDischarging || r.discharging;
    }
    out.charging = anyCharging && !anyDischarging; out.discharging = anyDischarging && !anyCharging;
    out.capacityValid = out.capacityValid && capacity <= 1000000;
    if (out.capacityValid) out.capacityMwh = capacity;
    if (allRates) out.power = measured(watts, "windows-rate");
    return out;
}
} // namespace ymcc::monitorbattery
