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
// Default placement reserves the existing YMCC summon window, including while
// it is hidden. Saved drag coordinates bypass this policy in the carrier.
inline bool overlaps(const Area& a,const Area& b,int gap=0){
 return a.right>a.left&&a.bottom>a.top&&b.right>b.left&&b.bottom>b.top&&
   (long long)a.left<(long long)b.right+gap&&(long long)a.right>(long long)b.left-gap&&
   (long long)a.top<(long long)b.bottom+gap&&(long long)a.bottom>(long long)b.top-gap;
}
inline Placement avoidInitialOverlap(Placement p,const Area& occupied,int w,int h){
 if(!p.fits||!overlaps({p.x,p.y,p.x+w,p.y+h},occupied))return p;
 // At most eight candidates; no timers, window enumeration or layout resizing.
 for(int gap=12;gap>=0;gap-=12){
   Placement best=p;bool found=false;long long bestDistance=0;
   const long long xs[]={ (long long)occupied.left-w-gap,(long long)occupied.right+gap,p.x,p.x };
   const long long ys[]={ p.y,p.y,(long long)occupied.top-h-gap,(long long)occupied.bottom+gap };
   for(int i=0;i<4;++i){
     const int x=(int)std::clamp(xs[i],(long long)p.area.left,(long long)p.area.right-w);
     const int y=(int)std::clamp(ys[i],(long long)p.area.top,(long long)p.area.bottom-h);
     if(overlaps({x,y,x+w,y+h},occupied,gap))continue;
     const long long dx=(long long)x-p.x,dy=(long long)y-p.y;
     const long long distance=(dx<0?-dx:dx)+(dy<0?-dy:dy);
     if(!found||distance<bestDistance){found=true;bestDistance=distance;best.x=x;best.y=y;}
   }
   if(found)return best;
 }
 // Both windows may exceed the available space. Keep the HUD reachable.
 return p;
}
// An event can precede a borderless game's own late promotion to TOPMOST.
// The existing 1 Hz frame may repair confirmed occlusion, but never raises
// every frame while the HUD is already above the foreground.
inline bool needsRaise(bool global,bool eventRequested,bool foregroundOccludes){return eventRequested||(global&&foregroundOccludes);}
inline bool relevant(bool ownHudEvent,bool foregroundEvent,bool anchorOrForegroundEvent){
 return !ownHudEvent&&(foregroundEvent||anchorOrForegroundEvent); // global is an always-on viewport, not a target PID admission.
}
}
