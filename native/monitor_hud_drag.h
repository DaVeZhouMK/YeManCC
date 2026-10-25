#pragma once
// Bounded gestures shared by mouse, touch and pen. The carrier owns input and
// radial hold timer. Drag/tap gestures never change the explicit input lock.
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace ymcc::huddrag {
inline constexpr std::uint64_t maxSafeRevision=9007199254740991ULL;
// Device event stamps are a separate clock from a persisted wall-time version.
// A future/mismatched 32-bit stamp must not wrap an unsigned boot-time subtraction.
inline std::uint64_t sourceEventTick(std::uint64_t received,std::uint32_t source,bool known){
 const auto age=static_cast<std::uint32_t>(static_cast<std::uint32_t>(received)-source);
 return known&&age<=60000&&age<=received?received-age:received;
}
inline std::uint64_t nextRevision(std::uint64_t previous,std::uint64_t now){
 const auto current=std::min(now,maxSafeRevision);
 return previous<maxSafeRevision?std::max(previous+1,current):current;
}
struct Position {double x=0,y=0;bool placed=false,locked=false;std::uint64_t revision=0;};
struct Result {bool moved=false,started=false,ended=false,switchTemplate=false;};
class Gesture {
 bool down_=false,moving_=false,touch_=false,tapped_=false,tapTouch_=false;unsigned id_=0;
 double downX_=0,downY_=0,offsetX_=0,offsetY_=0,tapX_=0,tapY_=0;
 std::uint64_t downAt_=0,tapAt_=0;
public:
 Position position;
 bool dragging()const{return moving_;}bool contact()const{return down_;}
 void cancel(){down_=moving_=tapped_=false;}
 bool adopt(Position p){if(p.revision>maxSafeRevision||p.revision<position.revision||down_)return false;position=p;return true;}
 bool begin(unsigned id,bool touch,double x,double y,int left,int top,std::uint64_t at){
  if(position.locked||down_)return false;
  if(tapped_&&touch!=tapTouch_)tapped_=false;
  down_=true;moving_=false;id_=id;touch_=touch;downX_=x;downY_=y;offsetX_=x-left;offsetY_=y-top;downAt_=at;return true;
 }
 Result move(unsigned id,double x,double y,int left,int top,int freeX,int freeY,int panelWidth=0,int panelHeight=0){
  Result r;if(!down_||id!=id_||position.locked)return r;
  if(!moving_&&std::hypot(x-downX_,y-downY_)<7)return r;
  r.started=!moving_;moving_=true;tapped_=false;
  const double nx=freeX>0?std::clamp(x-offsetX_-left,0.0,(double)freeX)/std::max(1,freeX+panelWidth):0;
  const double ny=freeY>0?std::clamp(y-offsetY_-top,0.0,(double)freeY)/std::max(1,freeY+panelHeight):0;
  r.moved=!position.placed||position.x!=nx||position.y!=ny;
  position.x=nx;position.y=ny;position.placed=true;return r;
 }
 Result end(unsigned id,double x,double y,std::uint64_t at,bool cancelled,unsigned doubleMs=500){
  Result r;if(!down_||id!=id_)return r;r.ended=moving_;
  // Long stationary contacts belong to radial summon; release cannot lock.
  const bool tap=!cancelled&&!moving_&&at>=downAt_&&at-downAt_<=350&&std::hypot(x-downX_,y-downY_)<7;
  if(tap&&tapped_&&touch_==tapTouch_&&at>=tapAt_&&at-tapAt_<=std::clamp(doubleMs,200u,900u)&&std::hypot(x-tapX_,y-tapY_)<=24){r.switchTemplate=true;tapped_=false;}
  else {tapped_=tap;tapAt_=at;tapX_=x;tapY_=y;tapTouch_=touch_;}
  down_=moving_=false;
  if(r.ended)position.revision=std::max(position.revision+1,at);
  return r;
 }
};
}
