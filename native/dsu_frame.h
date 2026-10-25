#pragma once
#include "windows_motion_submission.h"
#include "legion_motion_source.h"
#include "input_frame_submission.h"
#include <cmath>
#include <cstdint>
#include <optional>

namespace ymcc::dsu {
struct Axes {
    float gx=0, gy=0, gz=0, ax=0, ay=0, az=0; // dps and g; ProcessMotion input, not calibrated output.
    bool finite() const noexcept {
        return std::isfinite(gx)&&std::isfinite(gy)&&std::isfinite(gz)&&
            std::isfinite(ax)&&std::isfinite(ay)&&std::isfinite(az);
    }
};
// Exact fixed HC MotionManager.SetupMotion DSU transform; multipliers apply
// only to its Default plane, not to DSU/raw. Wire sign changes happen later.
inline Axes profile(Axes a,bool yaw,bool invertHorizontal,bool invertVertical) noexcept {
    if(yaw){const auto gy=a.gy,ay=a.ay;a.gy=-a.gz;a.gz=-gy;a.ay=-a.az;a.az=-ay;}
    if(invertHorizontal){a.gy=-a.gy;a.gz=-a.gz;a.ay=-a.ay;a.az=-a.az;}
    if(invertVertical){a.gx=-a.gx;a.gy=-a.gy;a.ax=-a.ax;a.ay=-a.ay;}
    return a;
}
struct Source {
    std::uint64_t power=0;
    input::frameSubmission::Sample physical;
    windows_motion::Stamp windows;
    std::optional<legion_motion::Sample> controller;
};
struct Admission {bool pad=false,motion=false;};
struct Frame;
using Validate=Admission(*)(const Frame&,std::uint64_t) noexcept;
struct Frame {
    bool live=false,motionReady=false,semanticNeutral=false;
    Axes raw,transformed;
    std::uint16_t buttons=0;
    std::int16_t lx=0,ly=0,rx=0,ry=0;
    std::uint8_t lt=0,rt=0;
    std::uint64_t timestampMs=0;
    Source source;
    Validate validate=nullptr; // Process-lifetime native function, never a temporary closure.
};
inline Admission admit(const Frame& frame,std::uint64_t now) noexcept {
    if(frame.semanticNeutral)return {true,false};
    if(!frame.validate)return {};
    auto state=frame.validate(frame,now);
    state.motion=state.pad&&state.motion&&frame.motionReady&&frame.raw.finite()&&frame.transformed.finite();
    return state;
}
} // namespace ymcc::dsu
