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