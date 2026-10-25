#include "../native/monitor_cpu_deltas.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ymcc::cpusample;
int main(){int n=0;auto check=[&](bool condition,const char* name){if(!condition)throw std::runtime_error(name);++n;};
 Deltas d;auto r=d.accept({{100,100,100},{100,100,100}},1000);
 check(r.cores.size()==2&&!r.cores[0]&&!r.cores[1],"first sample has unknown slots, not fake load");
 r=d.accept({{180,200,100},{100,200,100}},2000);
 check(r.cores[0]&&std::abs(*r.cores[0]-20)<.0001,"per-processor delta 20 percent");
 check(r.cores[1]&&*r.cores[1]==100,"per-processor delta 100 percent");
 check(std::abs(r.total-60)<.0001,"aggregate derived from SAME snapshot, never copied into cores");
 r=d.accept({{280,300,100},{200,300,100}},3000);check(r.cores[0]&&*r.cores[0]==0&&r.total==0,"true zero is not unavailable");
 r=d.accept({{280,300,100},{200,300,100}},4000);check(!r.cores[0]&&!r.cores[1],"no tick delta stays unknown");
 r=d.accept({{1,1,1},{1,1,1}},5000);check(!r.cores[0]&&!r.cores[1],"counter regression rebaselines without wraparound");
 r=d.accept({{100,10,1},{1,10,1}},6000);check(!r.cores[0]&&r.cores[1],"idle exceeding total invalidates only that processor");
 r=d.accept({{100,100,100}},7000);check(r.cores.size()==1&&!r.cores[0],"topology/cardinality change starts unknown, never remaps old data");
 r=d.accept({{100,200,100}},14001);check(!r.cores[0],"idle/resume gap longer than freshness window rebaselines");
 r=d.accept({{100,300,100}},15001);check(r.cores[0]&&*r.cores[0]==100,"second fresh post-resume sample is valid");
 d.reset();r=d.accept(std::vector<Times>(128,Times{0,100,100}),1000);check(r.cores.size()==128,"unified layer retains actual processor count up to packet bound");
 r=d.accept(std::vector<Times>(129),2000);check(r.cores.empty(),"unbounded/malformed count rejected");
 r=d.accept({},3000);check(r.cores.empty(),"missing sampler data stays absent");
 d.reset();r=d.accept({{0,0,0}},1000);r=d.accept({{0,UINT64_MAX,1}},2000);check(!r.cores[0],"unsigned total overflow rejected");
 std::cout<<"MONITOR_CPU_DELTAS_SELFTEST_OK "<<n<<" cases; pure synthetic counters, no CPU benchmark or sensor reads\n";
}