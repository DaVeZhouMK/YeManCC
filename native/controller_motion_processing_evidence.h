#pragma once
#include <array>
#include <mutex>
#include <optional>
#include "controller_motion_session.h"

namespace ymcc::controller_motion {
struct ProcessedBank {
    Key key;
    std::uint64_t lifetime=0,queuedAt=0,processedAt=0,sequence=0,reportSerial=0;
    bool unselected=false;
    MotionResult result;
};
inline bool finiteResult(const MotionResult& value) noexcept {
    return value.axes.valid()&&std::isfinite(value.deltaSeconds)&&value.deltaSeconds>=0&&
        std::isfinite(value.calibrated.x)&&std::isfinite(value.calibrated.y)&&std::isfinite(value.calibrated.z)&&
        std::isfinite(value.gravity.x)&&std::isfinite(value.gravity.y)&&std::isfinite(value.gravity.z)&&
        value.orientationFinite&&std::isfinite(value.orientation.w)&&std::isfinite(value.orientation.x)&&
        std::isfinite(value.orientation.y)&&std::isfinite(value.orientation.z);
}
// Query/export copies value evidence only; never expose native sessions to UI.
class ProcessingEvidence {
    std::mutex mutex_;
    std::array<std::optional<ProcessedBank>,2> banks_;
    std::uint64_t sequence_=0;
public:
    bool publish(const Key& key,std::uint64_t lifetime,std::uint64_t queuedAt,std::uint64_t processedAt,
        const MotionResult& value,bool unselected,std::uint64_t reportSerial=0) {
        if(!key.valid()||key.sensorIndex>1||!lifetime||!queuedAt||processedAt<queuedAt||!finiteResult(value))return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto& bank=banks_[key.sensorIndex];
        if(!bank||!(bank->key==key)||bank->lifetime!=lifetime){bank.emplace();bank->key=key;}
        if(++sequence_==0)sequence_=1;
        bank->lifetime=lifetime;bank->queuedAt=queuedAt;bank->processedAt=processedAt;bank->sequence=sequence_;
        bank->result=value;bank->unselected=unselected;bank->reportSerial=reportSerial;
        return true;
    }
    void clear() {std::lock_guard<std::mutex> lock(mutex_);banks_={};sequence_=0;}
    std::array<std::optional<ProcessedBank>,2> snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);return banks_;
    }
};
} // namespace ymcc::controller_motion
