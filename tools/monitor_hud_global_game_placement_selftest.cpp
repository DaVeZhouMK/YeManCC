#include "../native/monitor_hud_placement.h"
#include <iostream>
#include <stdexcept>
using namespace ymcc::hudplacement;
int main(){int n=0;auto check=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);++n;};
 Area screen{0,0,3840,2112},ignoredWindow{506,375,3096,2044};
 auto p=place(ignoredWindow,screen,false,401,470,1);check(p.fits&&!p.foreground&&p.x==12&&p.y==12,"global remains fixed screen corner, never follows foreground client");
 for(int position=1;position<=6;++position){auto q=place(ignoredWindow,screen,false,401,470,position);check(q.fits&&!q.foreground&&q.x>=screen.left&&q.y>=screen.top&&q.x+401<=screen.right&&q.y+470<=screen.bottom,"six global positions remain within configured display");}
 p=place({},screen,false,401,470,1);check(p.fits&&!p.foreground,"no active game/window still visible");
 p=place({},screen,false,3840,2112,1);check(p.fits&&p.x==0&&p.y==0,"exact display size has no extra margin overflow");
 p=place({}, {-1920,0,0,1080},false,401,470,1);check(p.fits&&p.x==-1908&&p.y==12,"configured negative-coordinate display stays fixed");
 const auto clear=[](const Placement& p,const Area& reserved,int w,int h){return !overlaps({p.x,p.y,p.x+w,p.y+h},reserved);};
 for(int height:{720,800})for(int side:{0,1})for(int position=1;position<=6;++position){
   Area work{0,0,1280,height},reserved=side?Area{760,0,1280,height}:Area{0,0,520,height};
   auto initial=place({},work,false,360,180,position),safe=avoidInitialOverlap(initial,reserved,360,180);
   check(safe.fits&&clear(safe,reserved,360,180),"all default corners avoid left/right YMCC at 720p and 800p");
   check(safe.x>=work.left&&safe.y>=work.top&&safe.x+360<=work.right&&safe.y+180<=work.bottom,"avoidance stays on its own display");
   if(clear(initial,reserved,360,180))check(initial.x==safe.x&&initial.y==safe.y,"non-overlapping default does not move");
 }
 {Area work{-1920,-120,0,960},reserved{-1920,-120,-1100,960};auto safe=avoidInitialOverlap(place({},work,false,400,240,1),reserved,400,240);check(safe.fits&&clear(safe,reserved,400,240)&&safe.x>=work.left&&safe.y>=work.top,"negative display coordinates avoid the same-screen summon area");}
 {Area work{0,0,1280,800},other{1280,0,2560,800};auto initial=place({},work,false,360,180,1),safe=avoidInitialOverlap(initial,other,360,180);check(safe.x==initial.x&&safe.y==initial.y,"YMCC on another display does not displace the monitor");}
 {Area work{0,0,1280,800},reserved{0,0,1280,200};auto safe=avoidInitialOverlap(place({},work,false,360,180,1),reserved,360,180);check(safe.y>=212&&clear(safe,reserved,360,180),"wide upper summon window can be avoided vertically");}
 {Area work{0,0,1280,800},reserved{0,0,1000,800};auto safe=avoidInitialOverlap(place({},work,false,280,180,1),reserved,280,180);check(safe.x==1000&&clear(safe,reserved,280,180),"exact remaining space permits edge contact instead of overlap");}
 {Area work{0,0,1280,800};auto initial=place({},work,false,360,180,1),safe=avoidInitialOverlap(initial,work,360,180);check(safe.fits&&safe.x==initial.x&&safe.y==initial.y,"insufficient space keeps the monitor reachable without resize or hiding");}
 {auto initial=place({},screen,false,401,470,1),safe=avoidInitialOverlap(initial,{},401,470);check(safe.x==initial.x&&safe.y==initial.y,"missing summon bounds preserve default placement");auto invalid=place({},screen,false,5000,5000,1);check(!avoidInitialOverlap(invalid,screen,5000,5000).fits,"avoidance cannot admit an oversized monitor");}
 check(relevant(false,true,false),"foreground activation permits only non-activating topmost maintenance");
 check(!relevant(true,true,true),"own HUD cannot create feedback loop");
 check(!relevant(false,false,false),"unrelated window events ignored");
 check(needsRaise(true,true,false),"global foreground event raises once even before game topmost transition");
 check(needsRaise(true,false,true),"late borderless topmost promotion is repaired by existing data frame");
 check(!needsRaise(true,false,false),"steady global frame never continuously reasserts topmost");
 check(!needsRaise(false,false,true),"legacy bound mode does not acquire global recovery policy");
 check(needsRaise(false,true,false),"legacy bound event behavior unchanged");
 check(needsRaise(true,true,true),"event and overlap coalesce into a single raise decision");
 check(!needsRaise(false,false,false),"inactive repair conditions produce no work");
 std::cout<<"FIXED_GLOBAL_PLACEMENT_SELFTEST_OK "<<n<<" cases; policy only, not game acceptance or CPU benchmark\n";
}