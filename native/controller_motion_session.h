#pragma once

#include "controller_motion_processor.h"

// One caller-serialized CPU session. No clock acquisition, DLL loading, HID,
// threads, Windows sensor state, or application globals. Caller owns callback
// library lifetime and supplies nonthrowing, non-reentrant C ABI callbacks.
namespace ymcc::controller_motion {

struct ReadOps {
    void (*getCal)(void*, float*, float*, float*) = nullptr;
    void (*getGravity)(void*, float*, float*, float*) = nullptr;
    void (*getPlayer)(void*, float*, float*, float) = nullptr;
    void (*getWorld)(void*, float*, float*, float) = nullptr;

    bool complete() const { return getCal && getGravity; }
    friend bool operator==(const ReadOps& a, const ReadOps& b) {
        return a.getCal == b.getCal && a.getGravity == b.getGravity &&
               a.getPlayer == b.getPlayer && a.getWorld == b.getWorld;
    }
};

struct ReadRequest {
    bool player = false;
    bool world = false;
};

struct MotionVector3 { float x = 0, y = 0, z = 0; };
struct MotionVector2 { float x = 0, y = 0; };
struct MotionQuaternion { float w = 0, x = 0, y = -1, z = 0; };

struct MotionResult {
    RawSixAxes axes;
    MotionVector3 calibrated;
    MotionVector3 gravity;
    MotionQuaternion orientation;
    bool orientationFinite = false;
    std::optional<MotionVector2> player;
    std::optional<MotionVector2> world;
    double deltaSeconds = 0;
};

namespace session_detail {
// Formula copied from the current main.cpp ymccMadgwickUpdate, with the four
// state slots replaced by an instance quaternion. Existing dt<=0 and zero-gravity
// returns are formula behavior, not admission to native ProcessMotion. No new
// dt clamp or stability threshold is added. Controller-only repair: an exactly
// zero gradient has no correction; retain zero s1..s4 and integrate gyro normally.
// Nonzero gradients retain the original formula. Windows main is not modified.
inline void updateOrientation(MotionQuaternion& q, float gx, float gy, float gz,
                              float ax, float ay, float az, float dtSeconds) {
    if (dtSeconds <= 0.0f) return;
    float q1 = q.w, q2 = q.x, q3 = q.y, q4 = q.z;
    float norm = std::sqrt(ax * ax + ay * ay + az * az);
    if (norm == 0.0f) return;
    norm = 1.0f / norm; ax *= norm; ay *= norm; az *= norm;
    const float _2q1 = 2.0f * q1, _2q2 = 2.0f * q2, _2q3 = 2.0f * q3, _2q4 = 2.0f * q4;
    const float _4q1 = 4.0f * q1, _4q2 = 4.0f * q2, _4q3 = 4.0f * q3;
    const float _8q2 = 8.0f * q2, _8q3 = 8.0f * q3;
    const float q1q1 = q1 * q1, q2q2 = q2 * q2, q3q3 = q3 * q3, q4q4 = q4 * q4;
    float s1 = _4q1 * q3q3 + _2q3 * ax + _4q1 * q2q2 - _2q2 * ay;
    float s2 = _4q2 * q4q4 - _2q4 * ax + 4.0f * q1q1 * q2 - _2q1 * ay - _4q2 + _8q2 * q2q2 + _8q2 * q3q3 + _4q2 * az;
    float s3 = 4.0f * q1q1 * q3 + _2q1 * ax + _4q3 * q4q4 - _2q4 * ay - _4q3 + _8q3 * q2q2 + _8q3 * q3q3 + _4q3 * az;
    float s4 = 4.0f * q2q2 * q4 - _2q2 * ax + 4.0f * q3q3 * q4 - _2q3 * ay;
    const float gradientSquaredNorm = s1 * s1 + s2 * s2 + s3 * s3 + s4 * s4;
    if (gradientSquaredNorm == 0.0f) {
        s1 = s2 = s3 = s4 = 0.0f;
    } else {
        norm = 1.0f / std::sqrt(gradientSquaredNorm);
        s1 *= norm; s2 *= norm; s3 *= norm; s4 *= norm;
    }
    const float beta = 0.1f;
    const float qDot1 = 0.5f * (-q2 * gx - q3 * gy - q4 * gz) - beta * s1;
    const float qDot2 = 0.5f * (q1 * gx + q3 * gz - q4 * gy) - beta * s2;
    const float qDot3 = 0.5f * (q1 * gy - q2 * gz + q4 * gx) - beta * s3;
    const float qDot4 = 0.5f * (q1 * gz + q2 * gy - q3 * gx) - beta * s4;
    q1 += qDot1 * dtSeconds; q2 += qDot2 * dtSeconds;
    q3 += qDot3 * dtSeconds; q4 += qDot4 * dtSeconds;
    norm = 1.0f / std::sqrt(q1 * q1 + q2 * q2 + q3 * q3 + q4 * q4);
    q.w = q1 * norm; q.x = q2 * norm;
    q.y = q3 * norm; q.z = q4 * norm;
}
} // namespace session_detail

class Session final {
public:
    Session() = default;
    ~Session() noexcept(false) { revoke(); }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    // All resource/read callback changes count as a replacement, even when an
    // allocator reuses the same pointer. An unchanged selection does not reset
    // orientation/time or reapply a different valid calibration value.
    bool select(const Key& key, const Ops& ops, const ReadOps& reads,
                const Calibration& calibration, double nowSeconds) {
        const bool sameCurrentKey = owner_ && owner_->handleFor(key);
        const bool unchanged = sameCurrentKey && ops_ == ops && reads_ == reads;
        if (!key.valid() || !ops.complete() || !reads.complete() || !calibration.valid()) {
            revoke();
            return false;
        }
        if(unchanged)return true;
        if(!sameCurrentKey&&reauthorizedKey_&&key.physicalIdentity==reauthorizedKey_->physicalIdentity&&
            key.power==reauthorizedKey_->power&&key.rawGeneration==reauthorizedKey_->rawGeneration&&
            key.sensorIndex==reauthorizedKey_->sensorIndex&&key.captureEpoch<=reauthorizedKey_->captureEpoch){
            revoke();return false;
        }
        if (!sameCurrentKey && attemptedKeys_.find(key) != attemptedKeys_.end()) {
            revoke();
            return false;
        }
        // Allocate/copy token metadata before native creation. This set bridges
        // the private Owner replacement needed for read-only callback changes.
        const auto entry = attemptedKeys_.insert(key);
        if (sameCurrentKey && ops_ == ops && !(reads_ == reads)) {
            // Owner only compares native Ops. Retire before reconstructing that
            // private Owner; Session history still rejects all retired tokens.
            revoke();
            owner_.reset();
        }
        if (!owner_) owner_.emplace();
        if (!owner_->select(key, ops, calibration)) {
            resetState();
            return false;
        }
        currentKey_ = &*entry.first;
        ops_ = ops;
        reads_ = reads;
        if (!unchanged) {
            orientation_ = {};
            previousSeconds_ = nowSeconds;
            calibrationMode_ = 0; // Owner initialized this exact replacement handle as Manual.
        }
        return true;
    }

    // Retain this bank's native calibration and orientation across an explicit
    // selection switch. The caller supplies a separately proven live lifetime.
    // Reset only delta's origin so time spent unselected is never integrated.
    bool reauthorize(const Key& from,const Key& next,double nowSeconds,bool resetTimeOrigin=true) {
        if(!owner_||!currentKey_||!(*currentKey_==from)||!std::isfinite(nowSeconds))return false;
        Key staged=next;
        if(!owner_->reauthorize(from,next))return false;
        reauthorizedKey_=std::move(staged);currentKey_=&*reauthorizedKey_;
        if(resetTimeOrigin)previousSeconds_=nowSeconds;
        return true;
    }
    bool resetTimeOrigin(const Key& key,double now) noexcept {
        if(!handleFor(key)||!std::isfinite(now))return false;
        previousSeconds_ = now;return true;
    }

    // Invoke on every BUS tick with the actual current controller sample; an
    // identical sample may be processed again. Missing requested reads reject
    // the whole tick before ProcessMotion and do not advance previousSeconds_.
    std::optional<MotionResult> process(const Key& key, const RawSixAxes& axes,
                                      double nowSeconds, ReadRequest request = {}) {
        void* handle = handleFor(key);
        if (!handle || !axes.valid() || !reads_.complete() ||
            (request.player && !reads_.getPlayer) || (request.world && !reads_.getWorld))
            return std::nullopt;

        MotionResult result;
        result.axes = axes;
        result.deltaSeconds = nowSeconds - previousSeconds_;
        const float dt = static_cast<float>(result.deltaSeconds);
        if (!owner_->process(key, axes, dt)) return std::nullopt;
        previousSeconds_ = nowSeconds;
        reads_.getCal(handle, &result.calibrated.x, &result.calibrated.y, &result.calibrated.z);
        reads_.getGravity(handle, &result.gravity.x, &result.gravity.y, &result.gravity.z);
        const float kDeg2Rad = 3.14159265358979323846f / 180.0f;
        session_detail::updateOrientation(orientation_,
            -kDeg2Rad * result.calibrated.x, kDeg2Rad * result.calibrated.y, -kDeg2Rad * result.calibrated.z,
            -result.gravity.x, result.gravity.y, -result.gravity.z, dt);
        result.orientation = orientation_;
        result.orientationFinite = std::isfinite(orientation_.w) && std::isfinite(orientation_.x) &&
                                   std::isfinite(orientation_.y) && std::isfinite(orientation_.z);
        if (request.player) {
            MotionVector2 player;
            reads_.getPlayer(handle, &player.x, &player.y, 1.41f);
            result.player = player;
        }
        if (request.world) {
            MotionVector2 world;
            reads_.getWorld(handle, &world.x, &world.y, 0.125f);
            result.world = world;
        }
        return result;
    }

    // The independent Controller handle has one serialized mode writer. Native
    // callbacks keep their existing nonthrowing/non-reentrant C ABI contract.
    // Mode changes do not reset offsets, orientation or the process time origin.
    bool reconcileCalibrationMode(const Key& key, bool automatic) {
        void* handle = handleFor(key);
        if (!handle || !ops_.setMode) return false;
        const int wanted = automatic ? 3 : 0; // HC Stillness|SensorFusion or Manual.
        if (calibrationMode_ && *calibrationMode_ == wanted) return true;
        calibrationMode_.reset();
        ops_.setMode(handle, wanted);
        calibrationMode_ = wanted;
        return true;
    }

    // A borrowed manual transaction may write mode3/mode0 directly. Mark the
    // cache unknown before it can do so; the next idle frame reconciles intent.
    void invalidateCalibrationMode() noexcept { calibrationMode_.reset(); }

    // Callback-completed software intent only, never native mode readback.
    std::optional<int> calibrationModeFor(const Key& key) const {
        return handleFor(key) ? calibrationMode_ : std::nullopt;
    }

    bool active() const { return currentKey_ && handleFor(*currentKey_); }

    bool matchesSelection(const Key& key, const Ops& ops, const ReadOps& reads) const {
        return handleFor(key) && ops_ == ops && reads_ == reads;
    }

    void* handleFor(const Key& key) const {
        return owner_ ? owner_->handleFor(key) : nullptr;
    }

    void revoke() {
        if (owner_) owner_->revoke();
        resetState();
    }
    void clear() { revoke(); }

private:
    void resetState() {
        currentKey_ = nullptr;
        ops_ = {};
        reads_ = {};
        calibrationMode_.reset();
        orientation_ = {};
        previousSeconds_ = 0;
    }
    // Owner's exact key history is unbounded in its instance lifetime. Session
    // also keeps exact token metadata to preserve history across read-only Ops
    // replacement; this grows with distinct attempted tokens. No old handles kept.
    std::set<Key> attemptedKeys_;
    std::optional<Key> reauthorizedKey_;
    const Key* currentKey_ = nullptr;
    std::optional<Owner> owner_{std::in_place};
    Ops ops_;
    ReadOps reads_;
    std::optional<int> calibrationMode_;
    MotionQuaternion orientation_;
    double previousSeconds_ = 0;
};

} // namespace ymcc::controller_motion
