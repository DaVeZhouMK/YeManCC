#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>

// Synchronous CPU ownership only. The caller serializes access, supplies nonthrowing,
// non-reentrant C ABI callbacks, and keeps their library loaded through revoke().
// No HID, DLL loading, threads, Windows sensor state, or calibration persistence.
namespace ymcc::controller_motion {

struct Key {
    std::string physicalIdentity;
    std::uint64_t power = 0;
    std::uint64_t rawGeneration = 0;
    std::uint64_t captureEpoch = 0;
    unsigned sensorIndex = 0;

    bool valid() const {
        return !physicalIdentity.empty() && power != 0 &&
               rawGeneration != 0 && captureEpoch != 0;
    }

    friend bool operator==(const Key& a, const Key& b) {
        return a.physicalIdentity == b.physicalIdentity &&
               a.power == b.power && a.rawGeneration == b.rawGeneration &&
               a.captureEpoch == b.captureEpoch && a.sensorIndex == b.sensorIndex;
    }
    friend bool operator<(const Key& a, const Key& b) {
        return std::tie(a.physicalIdentity, a.power, a.rawGeneration,
                        a.captureEpoch, a.sensorIndex) <
               std::tie(b.physicalIdentity, b.power, b.rawGeneration,
                        b.captureEpoch, b.sensorIndex);
    }
};

// Structured persistence key: exact physical identity + independent sensor index.
// Never append power/raw/capture generations or substitute a Windows gyroDeviceId.
struct CalibrationKey {
    std::string physicalIdentity;
    unsigned sensorIndex = 0;

    friend bool operator==(const CalibrationKey& a, const CalibrationKey& b) {
        return a.physicalIdentity == b.physicalIdentity && a.sensorIndex == b.sensorIndex;
    }
    friend bool operator<(const CalibrationKey& a, const CalibrationKey& b) {
        return std::tie(a.physicalIdentity, a.sensorIndex) <
               std::tie(b.physicalIdentity, b.sensorIndex);
    }
};

inline std::optional<CalibrationKey> calibrationKeyFor(const Key& key) {
    if (!key.valid()) return std::nullopt;
    return CalibrationKey{key.physicalIdentity, key.sensorIndex};
}

struct Calibration {
    float x = 0;
    float y = 0;
    float z = 0;
    int weight = 0;

    bool valid() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && weight >= 0;
    }
};

// On x64 these signatures accept main's actual GamepadMotion C ABI pointers
// directly. Loading/resolving/unloading those pointers remains the caller's job.
struct Ops {
    void* (*create)() = nullptr;
    void (*destroy)(void*) = nullptr;
    void (*setMode)(void*, int) = nullptr;
    void (*setOffset)(void*, float, float, float, int) = nullptr;
    void (*process)(void*, float, float, float, float, float, float, float) = nullptr;

    bool complete() const {
        return create && destroy && setMode && setOffset && process;
    }
    friend bool operator==(const Ops& a, const Ops& b) {
        return a.create == b.create && a.destroy == b.destroy &&
               a.setMode == b.setMode && a.setOffset == b.setOffset &&
               a.process == b.process;
    }
};

struct RawSixAxes {
    float gx = 0;
    float gy = 0;
    float gz = 0;
    float ax = 0;
    float ay = 0;
    float az = 0;

    bool valid() const {
        return std::isfinite(gx) && std::isfinite(gy) && std::isfinite(gz) &&
               std::isfinite(ax) && std::isfinite(ay) && std::isfinite(az);
    }
};

class Owner final {
public:
    Owner() = default;
    ~Owner() noexcept(false) { revoke(); }
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
    Owner(Owner&&) = delete;
    Owner& operator=(Owner&&) = delete;

    // calibration is a caller-supplied value from calibrationKeyFor(key), never a
    // Windows fallback. This value-only API cannot authenticate an external cache.
    // Same key + all callbacks unchanged preserves both handle and applied offset.
    // Any other selection retires the current handle before validation/creation.
    // A retired or failed-create token cannot be selected again; use a fresh epoch.
    // Changing callbacks for the currently active key is explicitly permitted.
    bool select(const Key& key, const Ops& ops, const Calibration& calibration) {
        const bool replacingCurrentKey = handle_ && currentKey_ && *currentKey_ == key;
        if (replacingCurrentKey && ops_ == ops && calibration.valid()) return true;

        revoke();
        if (!key.valid() || !ops.complete() || !calibration.valid()) return false;
        if (!replacingCurrentKey && usedKeys_.find(key) != usedKeys_.end()) return false;
        if (!replacingCurrentKey && reauthorizedKey_ && sameLineage(key,*reauthorizedKey_) &&
            key.captureEpoch<=reauthorizedKey_->captureEpoch) return false;

        // Allocate/copy identity before create, so an allocation failure cannot
        // strand a newly created native handle. Entries survive clear/revoke.
        const auto entry = usedKeys_.insert(key);
        void* created = ops.create();
        if (!created) return false;

        currentKey_ = &*entry.first;
        ops_ = ops;
        handle_ = created;
        ops_.setMode(handle_, 0); // Manual, before applying this controller's offset.
        ops_.setOffset(handle_, calibration.x, calibration.y, calibration.z, calibration.weight);
        return true;
    }

    // Caller proves the same live physical owner; only a strictly newer output
    // capture authorization may replace the token. Keep one high-water key,
    // not a growing entry per user bank switch. No native callback is invoked.
    bool reauthorize(const Key& from,const Key& next) {
        if(!handleFor(from)||!next.valid()||!sameLineage(from,next)||
            next.captureEpoch<=from.captureEpoch||
            (reauthorizedKey_&&sameLineage(next,*reauthorizedKey_)&&next.captureEpoch<=reauthorizedKey_->captureEpoch))return false;
        Key staged=next;
        reauthorizedKey_=std::move(staged);currentKey_=&*reauthorizedKey_;
        return true;
    }

    void revoke() {
        void* retired = handle_;
        const auto destroy = ops_.destroy;
        // Detach before the external callback. Repeated clear/revoke/destruction
        // cannot destroy the same handle twice, even if a callback misbehaves.
        handle_ = nullptr;
        currentKey_ = nullptr;
        ops_ = {};
        if (retired) destroy(retired);
    }

    void clear() { revoke(); }

    // Borrowed handle, valid only until the next selection/clear/revoke/destructor.
    void* handleFor(const Key& key) const {
        return handle_ && currentKey_ && *currentKey_ == key ? handle_ : nullptr;
    }

    // Caller invokes this only when an actual raw sample is ready. All six axes
    // must be finite; zero is a valid stationary sample. dt is forwarded verbatim
    // in the caller's units, including zero/negative/nonfinite values.
    bool process(const Key& key, const RawSixAxes& axes, float dt) {
        void* current = handleFor(key);
        if (!current || !axes.valid()) return false;
        ops_.process(current, axes.gx, axes.gy, axes.gz, axes.ax, axes.ay, axes.az, dt);
        return true;
    }

private:
    static bool sameLineage(const Key& a,const Key& b) noexcept {
        return a.physicalIdentity==b.physicalIdentity&&a.power==b.power&&a.rawGeneration==b.rawGeneration&&a.sensorIndex==b.sensorIndex;
    }
    std::optional<Key> reauthorizedKey_;
    // Exact token history prevents old authorization from reviving after Stop,
    // sleep, selection failure, or a switch back to a previously selected device.
    // Lifetime is one externally owned Owner; destroying it discards this history.
    std::set<Key> usedKeys_;
    const Key* currentKey_ = nullptr;
    void* handle_ = nullptr;
    Ops ops_;
};

} // namespace ymcc::controller_motion
