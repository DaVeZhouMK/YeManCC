#pragma once
// Pure placement/event/z-order repair policy. No telemetry or timers.
#include <algorithm>
namespace ymcc::hudplacement {
struct Area {int left=0,top=0,right=0,bottom=0;};
struct Placement {bool fits=false,foreground=false;Area area;int x=0,y=0;};
inline bool fits(const Area& a,int w,int h){return w>0&&h>0&&a.right>a.left&&a.bottom>a.top&&w<=a.right-a.left&&h<=a.bottom-a.top;}
inline Placement place(Area preferred,Area monitor,bool hasForeground,int w,int h,int position){
 Placement p;p.foreground=hasForeground&&fits(preferred,w,h);p.area=p.foreground?preferred:monitor;
 p.fits=position>=1&&position<=6&&fits(p.area,w,h);if(!p.fits)return p;
 const int freeX=p.area.right-p.area.left-w,freeY=p.area.bottom-p.area.top-h,margin=12;
 const int col=position==1||position==4?0:position==5||position==6?1:2;
 p.x=p.area.left+(col==0?std::min(margin,freeX):col==1?freeX/2:std::max(0,freeX-margin));
 p.y=p.area.top+((position==3||position==4||position==6)?std::max(0,freeY-margin):std::min(margin,freeY));return p;
}
// An event can precede a borderless game's own late promotion to TOPMOST.
// The existing 1 Hz frame may repair confirmed occlusion, but never raises
// every frame while the HUD is already above the foreground.
inline bool needsRaise(bool global,bool eventRequested,bool foregroundOccludes){return eventRequested||(global&&foregroundOccludes);}
inline bool relevant(bool ownHudEvent,bool foregroundEvent,bool anchorOrForegroundEvent){
 return !ownHudEvent&&(foregroundEvent||anchorOrForegroundEvent); // global is an always-on viewport, not a target PID admission.
}
}
