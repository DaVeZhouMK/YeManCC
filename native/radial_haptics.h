#pragma once
#include "touch_haptic_dispatch.h"
namespace ymcc::radial {
// The touchpad Motion is the single distance/jitter/cadence implementation.
// This adapter owns only the wheel contact; it never sends device or OS input.
class TouchFeedback {
public:
 enum class Phase {Down,Move,Up,Cancel};
 struct Output {bool contact=false,pulse=false;};
private:
 bool active_=false;std::uint32_t id_=0;int strength_=0;
 touchhaptic::Motion motion_{};
public:
 bool contacting()const noexcept{return active_;}
 void cancel()noexcept{active_=false;id_=0;strength_=0;motion_={};}
 Output event(bool enabled,int strength,Phase phase,std::uint32_t id,float x,float y,int width,int height,std::uint64_t at)noexcept{
  if(!enabled||!touchhaptic::validStrengthPercent(strength)){cancel();return {};}
  if(active_&&strength_!=strength){cancel();if(phase!=Phase::Down)return {};}
  if(active_&&id==id_&&(phase==Phase::Up||phase==Phase::Cancel)){cancel();return {};}
  if(!std::isfinite(x)||!std::isfinite(y)||width<=0||height<=0)return {active_,false};
  if(phase==Phase::Down){
   if(active_)return {true,false};active_=true;id_=id;strength_=strength;
   motion_.begin(std::clamp(x,0.f,float(width)),std::clamp(y,0.f,float(height)),width,height,at);return {true,true};
  }
  if(!active_||id!=id_)return {active_,false};
  if(phase==Phase::Up||phase==Phase::Cancel){cancel();return {};}
  return {true,motion_.move(std::clamp(x,0.f,float(width)),std::clamp(y,0.f,float(height)),at)};
 }
};
}
