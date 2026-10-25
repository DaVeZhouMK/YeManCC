#include "monitor_hud_drag.h"
#include "json.hpp"
#include <iostream>
using namespace ymcc::huddrag;
int main(){nlohmann::json cases=nlohmann::json::array();bool ok=true;auto check=[&](const char* n,bool b){cases.push_back({{"name",n},{"ok",b}});ok&=b;};
for(bool touch:{false,true}){
 Gesture g;check("mouse/touch are draggable by default",!g.position.locked&&g.begin(1,touch,25,30,12,12,100));
 check("jitter does not move",!g.move(1,28,33,0,0,500,300).moved&&!g.dragging());
 auto move=g.move(1,125,130,0,0,500,300);check("drag retains offset",move.started&&move.moved&&std::abs(g.position.x-.224)<.0001&&std::abs(g.position.y-112./300)<.0001);
 check("another contact cannot end a drag",!g.end(2,125,130,200,false).ended&&g.dragging());
 check("outside clamps to viewport",g.move(1,9999,-9999,-1000,-500,500,300).moved&&g.position.x==1&&g.position.y==0);
 const auto end=g.end(1,9999,-9999,300,false);check("commit on release",end.ended&&!g.contact()&&g.position.revision==300);
 check("old settings cannot override latest drag",!g.adopt(Position{}));
 g.begin(1,touch,20,20,12,12,1000);g.end(1,20,20,1050,false);g.begin(1,touch,21,21,12,12,1200);auto twice=g.end(1,21,21,1250,false);
 check("double click/tap switches template without locking",twice.switchTemplate&&!g.position.locked);
 for(const auto duration:{350ULL,649ULL,650ULL,800ULL,5000ULL}){
  g.cancel();const auto before=g.position;g.begin(1,touch,20,20,12,12,2000);const auto release=g.end(1,20,20,2000+duration,false);
  check("stationary hold release never locks or commits placement",!g.position.locked&&g.position.revision==before.revision&&!release.ended);
  check("stationary hold release never switches template",!release.switchTemplate);
 }
 g.cancel();auto p=g.position;p.locked=true;++p.revision;check("settings explicitly locks",g.adopt(p)&&g.position.locked);
 check("explicitly locked input cannot begin",!g.begin(1,touch,20,20,12,12,3000));
 p.locked=false;++p.revision;check("settings explicitly unlocks",g.adopt(p)&&g.begin(1,touch,20,20,12,12,4000));
 g.end(1,20,20,4800,false);check("delayed physical release cannot lock",!g.position.locked);g.cancel();
 g.begin(1,touch,20,20,12,12,5000);g.move(1,99,99,0,0,500,300);const auto longDrag=g.end(1,99,99,6100,false);check("long drag commits without locking",longDrag.ended&&!g.position.locked);
 g.cancel();g.begin(1,touch,20,20,12,12,7000);g.end(1,20,20,7800,false);g.begin(1,touch,20,20,12,12,7900);check("hold release cannot arm a double tap",!g.end(1,20,20,7950,false).switchTemplate);
 g.cancel();g.begin(1,touch,20,20,12,12,7000);g.end(1,20,20,7050,true);g.begin(1,touch,20,20,12,12,7200);check("cancel cannot arm double tap",!g.end(1,20,20,7250,false).switchTemplate);
 g.cancel();g.begin(1,touch,20,20,12,12,8000);g.move(1,100,100,0,0,0,0);check("full viewport remains finite",g.position.x==0&&g.position.y==0);g.cancel();
 g.begin(1,touch,20,20,12,12,9000);check("active gesture refuses config adoption",!g.adopt(Position{1,1,true,true,99999}));g.cancel();
 g.begin(1,touch,20,20,12,12,10000);g.end(1,20,20,10050,false);g.begin(1,!touch,20,20,12,12,10200);check("mouse and touch taps cannot combine",!g.end(1,20,20,10250,false).switchTemplate);
}
check("future event stamp falls back without unsigned underflow",sourceEventTick(10000,11000,true)==10000);
check("foreign event clock falls back without unsigned underflow",sourceEventTick(10000,4000000000u,true)==10000);
check("valid delayed contact preserves the physical duration",sourceEventTick(10000,9100,true)==9100);
check("valid 32-bit rollover is preserved",sourceEventTick(0x100000020ULL,0xfffffff0u,true)==0xfffffff0ULL);
check("unknown and stale clocks cannot create a huge timestamp",sourceEventTick(10000,0,false)==10000&&sourceEventTick(100000,1,true)==100000);
check("unsafe old native revision is repaired into JavaScript range",nextRevision(18446744069588658048ULL,1792479739888ULL)==1792479739888ULL);
check("safe saved wall-clock revision remains monotonic",nextRevision(1792479739888ULL,10000)==1792479739889ULL);
check("safe revision cap never overflows",nextRevision(maxSafeRevision,1792479739888ULL)<=maxSafeRevision);
{Gesture g;check("unsafe persisted revisions cannot be adopted",!g.adopt(Position{0,0,false,false,18446744069588658048ULL}));}
std::cout<<nlohmann::json{{"ok",ok},{"cases",cases}}.dump(2)<<std::endl;return ok?0:1;}
