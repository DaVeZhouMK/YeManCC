#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <optional>
#include <windows.h>
namespace ymcc::media::downloads {
inline const uint64_t processStartedTick = GetTickCount64();
inline std::atomic<double> sharedCpuValue{0};
inline std::atomic<uint64_t> sharedCpuAt{0};
// Reuse the existing monitor's valid sample when present. Downloads never
// start monitoring. The bounded fallback is active only while a backlog exists.
inline void publishCpu(double value, bool valid, uint64_t at) {
  if (valid && std::isfinite(value) && value >= 0 && value <= 100) {
    sharedCpuValue.store(value, std::memory_order_relaxed);
    sharedCpuAt.store(at, std::memory_order_release);
  }
}
class CpuReader {
  uint64_t previousIdle_ = 0, previousTotal_ = 0, previousAt_ = 0;
  static uint64_t ticks(FILETIME time) {
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
  }

public:
  std::optional<double> read(uint64_t at) {
    const auto shared = sharedCpuAt.load(std::memory_order_acquire);
    if (shared && at >= shared && at - shared <= 5000)
      return sharedCpuValue.load(std::memory_order_relaxed);
    FILETIME idle{}, kernel{}, user{};
    if (!GetSystemTimes(&idle, &kernel, &user))
      return std::nullopt;
    const auto idleNow = ticks(idle), total = ticks(kernel) + ticks(user);
    const bool valid = previousAt_ && at > previousAt_ &&
                       at - previousAt_ <= 30000 && total > previousTotal_ &&
                       idleNow >= previousIdle_ &&
                       idleNow - previousIdle_ <= total - previousTotal_;
    const auto result =
        valid
            ? std::optional<double>{100.0 *
                                    (1.0 - double(idleNow - previousIdle_) /
                                               double(total - previousTotal_))}
            : std::nullopt;
    previousAt_ = at;
    previousIdle_ = idleNow;
    previousTotal_ = total;
    return result;
  }
};
class CpuGate {
  bool overloaded_ = false;
  uint64_t calmSince_ = 0;

public:
  const char *observe(std::optional<double> usage, int threshold,
                      uint64_t time) {
    if (!usage || !std::isfinite(*usage) || *usage < 0 || *usage > 100) {
      calmSince_ = 0;
      return "cpu-unknown";
    }
    if (*usage > threshold) {
      overloaded_ = true;
      calmSince_ = 0;
      return "cpu-busy";
    }
    if (!overloaded_)
      return "";
    if (*usage > std::max(0, threshold - (std::clamp)(threshold / 10, 1, 3))) {
      calmSince_ = 0;
      return "cpu-busy";
    }
    if (!calmSince_)
      calmSince_ = time;
    if (time - calmSince_ < 10000)
      return "cpu-recovering";
    overloaded_ = false;
    calmSince_ = 0;
    return "";
  }
};
struct ResourcePorts {
  std::function<int()> ac = []() {
    SYSTEM_POWER_STATUS power{};
    return GetSystemPowerStatus(&power) && power.ACLineStatus <= 1
               ? int(power.ACLineStatus)
               : -1;
  };
  std::function<std::optional<double>(uint64_t)> cpu;
};
} // namespace ymcc::media::downloads
