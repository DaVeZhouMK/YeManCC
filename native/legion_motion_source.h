#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace ymcc::legion_motion {

struct Context {
    std::uint64_t power = 0;
    std::uint64_t rawGeneration = 0;
};

// Caller supplies its selected physical identity and capture lifecycle epoch.
// No identity discovery or device binding is performed by this pure module.
struct Source {
    std::string physicalIdentity;
    std::uint64_t captureEpoch = 0;
    unsigned sensorIndex = 0;
    // Immutable total-library provider/model token. Zero remains unbound generic data.
    std::uint16_t capabilityToken = 0;
};

struct Sample {
    std::array<double, 3> gyro{};
    std::array<double, 3> accel{};
    std::uint64_t capturedAt = 0;
    Context context{};
    Source source{};
};

// Export/query-only evidence. lastAccepted is historical when sampleCurrent is
// false. Neither this snapshot nor its reason authorizes a consumer or mode write.
struct Observation {
    std::optional<Sample> lastAccepted;
    bool sampleCurrent = false;
    const char* reason = "no-admitted-sample";
};

// The caller serializes all access with its existing mutex. Context and Ready
// and selected Source must be current at each call; revoke on retirement before allowing new work.
// Ticks and maxAgeMs use the caller's same monotonic millisecond time domain.
class Lease {
public:
    // Source owns its identity string; copying a Sample can allocate, so publish
    // and current deliberately allow allocation exceptions to propagate.
    bool publish(const Sample& sample, Context current, const Source& currentSource, bool ready) {
        if (!ready || !sameContext(sample.context, current) ||
            !sameSource(sample.source, currentSource) || sample.capturedAt == 0 || !finiteAxes(sample)) {
            return false;
        }
        sample_ = sample;
        return true;
    }

    std::optional<Sample> current(Context current, const Source& currentSource, bool ready,
                                  std::uint64_t now,
                                  std::uint64_t maxAgeMs) const {
        if (!ready || !sample_ || !sameContext(sample_->context, current) ||
            !sameSource(sample_->source, currentSource)) {
            return std::nullopt;
        }
        if (sample_->capturedAt > now) return std::nullopt;
        if (now - sample_->capturedAt > maxAgeMs) return std::nullopt;
        return sample_;
    }

    // No new work in publish/current: diagnostics copy only on explicit query.
    Observation inspect(Context context, const Source& source, bool ready,
                        std::uint64_t now, std::uint64_t maxAgeMs) const {
        auto admitted = current(context, source, ready, now, maxAgeMs);
        if (admitted) return {std::move(admitted), true, "fresh-current-sample"};
        const char* reason = !ready ? "source-not-ready"
            : !sample_ ? "no-admitted-sample"
            : context.power == 0 ? "power-generation-unavailable"
            : sample_->context.power != context.power ? "power-generation-mismatch"
            : context.rawGeneration == 0 ? "raw-generation-unavailable"
            : sample_->context.rawGeneration != context.rawGeneration ? "raw-generation-mismatch"
            : source.physicalIdentity.empty() || source.captureEpoch == 0 || source.sensorIndex > 1
                ? "selected-source-not-bound"
            : sample_->source.physicalIdentity != source.physicalIdentity ? "physical-identity-mismatch"
            : sample_->source.captureEpoch != source.captureEpoch ? "capture-epoch-mismatch"
            : sample_->source.sensorIndex != source.sensorIndex ? "sensor-index-mismatch"
            : sample_->source.capabilityToken != source.capabilityToken ? "capability-binding-mismatch"
            : sample_->capturedAt > now ? "sample-from-future"
            : "sample-expired";
        return {sample_, false, reason};
    }

    void revoke() noexcept { sample_.reset(); }

private:
    static bool sameContext(Context captured, Context current) noexcept {
        return current.power != 0 && captured.power == current.power
            && current.rawGeneration != 0 && captured.rawGeneration == current.rawGeneration;
    }

    static bool sameSource(const Source& captured, const Source& currentSource) noexcept {
        return !currentSource.physicalIdentity.empty() &&
            captured.physicalIdentity == currentSource.physicalIdentity &&
            currentSource.captureEpoch != 0 && captured.captureEpoch == currentSource.captureEpoch &&
            currentSource.sensorIndex <= 1 && captured.sensorIndex == currentSource.sensorIndex &&
            captured.capabilityToken == currentSource.capabilityToken;
    }

    static bool finiteAxes(const Sample& sample) noexcept {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(sample.gyro[axis]) ||
                !std::isfinite(sample.accel[axis])) return false;
        }
        return true;
    }

    std::optional<Sample> sample_;
};

} // namespace ymcc::legion_motion
