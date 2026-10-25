#include "../native/input_idle_windows_json.h"
#include <iostream>
#include <thread>
#include <limits>
using namespace ymcc::idle;
int main(){
    unsigned cases=0,failed=0;auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    Pad p{},centerNoise{};centerNoise.sticks={-1,2,0,0};
    check("center residue retained and classified without a filter",rawActive(centerNoise)&&!meaningful(centerNoise)&&centerNoise.sticks[0]==-1);
    p.sticks[0]=-32768;check("negative full-scale never overflows classification",meaningful(p));
    p={};p.triggers[0]=30;check("diagnostic trigger threshold boundary",rawActive(p)&&!meaningful(p));p.triggers[0]=31;check("trigger above threshold",meaningful(p));
    p={};p.back=1;check("back is user content",meaningful(p));p={};p.screen=1;check("screen Guide pulse is user content",meaningful(p));
    Accumulator a;a.physical(true,p,1,true,1,1,"test");a.submit(p,true,false,{},1);
    check("gate off has zero counters",a.take(1).attempted.samples==0);
    a.enable(true,100);for(unsigned i=0;i<125;++i){a.physical(true,centerNoise,101+i,true,1,1,"msi-dinput");a.submit(centerNoise,true,false,{},101+i);}
    auto w=a.take(1100);
    check("constant repeated reports are not 125 input changes",w.accepted.samples==125&&w.accepted.changes==0);
    check("small analog noise not mislabeled meaningful",w.physical.smallAnalogOnlyFrames==125&&w.physical.meaningfulFrames==0);
    check("actual ACK and attempts separately counted",w.attempted.samples==125&&w.accepted.samples==125&&w.rejected==0);
    p.buttons=4096;a.physical(true,p,1200,true,1,1,"msi-dinput");a.submit(p,false,true,{1,2,3,0,0,1},1200);
    w=a.take(1300);check("rejected attempt not accepted input",w.attempted.samples==1&&w.accepted.samples==0&&w.rejected==1);
    check("window reset preserves last content for comparison",w.physical.changes==1&&w.physical.meaningfulFrames==1&&w.startTick==1100);
    a.submit(p,true,true,{1,2,3,0,0,1},1400);a.submit(p,true,true,{2,2,3,0,0,1},1401);
    a.submit(p,true,true,{std::numeric_limits<double>::quiet_NaN(),2,3,0,0,1},1402);
    w=a.take(1500);check("IMU counted separately from buttons/report cadence",w.imuFrames==3&&w.imuChanges==1&&w.nonFiniteImu==1);
    a.physical(false,p,1501,false,0,1,"none");w=a.take(1600);check("absent source not mistaken for sampled live buttons",w.physical.samples==0&&w.sourceAbsent==1&&!w.virtualEnabled&&w.contextTransitions==1);
    a.enable(false,1601);a.enable(true,1700);w=a.take(1701);check("gate reenable establishes a fresh observation window",w.physical.samples==0&&!w.physical.haveLast&&w.startTick==1700);
    check("failed idle read cannot prove timeout",!timeoutElapsed(false,999999,true,60));
    check("zero timeout means never, not immediate",!timeoutElapsed(true,999999,true,0));
    check("valid timeout boundary",!timeoutElapsed(true,59999,true,60)&&timeoutElapsed(true,60000,true,60));
    Environment e;DWORD seconds=99;e.acOffStatus=e.dcOffStatus=0;e.acOffSec=60;e.dcOffSec=120;e.activePlanStatus=0;
    check("unknown AC/DC is not guessed DC",!selectedTimeout(e,false,seconds));e.sourceKnown=true;e.acLineStatus=1;
    check("correct actual power source selects its timeout",selectedTimeout(e,false,seconds)&&seconds==60);
    auto j=environmentJson(e);check("unavailable execution status remains null not false success",j["displayRequired"].is_null()&&j["executionStateKnown"]==false);
    a.enable(true,2000);std::thread producer([&]{for(unsigned i=0;i<1000;++i)a.submit(centerNoise,true,false,{},2000+i);});uint64_t total=0;
    for(unsigned i=0;i<20;++i)total+=a.take(3000+i).accepted.samples;producer.join();total+=a.take(4000).accepted.samples;
    check("owner snapshots never lose concurrent producer counts",total==1000);
    std::cout<<"cases="<<cases<<" failed="<<failed<<"\n";return failed?3:0;
}
