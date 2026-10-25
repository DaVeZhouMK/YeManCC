#include "../native/msi_frontkey_observation.h"
#include <iostream>
#include <cstdlib>
#include <thread>
#include <atomic>
using namespace ymcc::msi::frontObservation;
static unsigned checks=0;static void check(bool ok,const char* name){++checks;if(!ok){std::cerr<<"FAIL "<<name<<"\n";std::exit(1);}}
static Request click(){Request r;r.power=2;r.source=3;r.rule=4;r.code=41;r.sequence=1;r.capturedAt=100;return r;}
int main(){
 const auto r=click();
 for(unsigned flags=0;flags<16;++flags)for(bool src:{false,true})for(bool vendor:{false,true}){
  Context c{bool(flags&1),bool(flags&2),bool(flags&4),bool(flags&8),2,4};
  check(current(r,c,src,vendor)==(flags==3&&src),"click exact MSI/power/exit/recording/source gates");
  auto t=r;t.origin=Origin::Takeover;t.vendorIntent=7;t.code=0;t.sequence=t.source=0;
  check(current(t,c,src,vendor)==((flags==3||flags==11)&&vendor),"takeover read does not invent WMI requirement or ignore lease cancellation");
 }
 Context c{true,true,false,false,2,4};auto wrong=r;wrong.power=3;check(!current(wrong,c,true,true),"old power denied");wrong=r;wrong.rule=5;check(!current(wrong,c,true,true),"old rule denied");wrong=r;wrong.code=99;check(!current(wrong,c,true,true),"unassigned code denied");
 Plan plan;check(plan.offer(r,100),"first Claw episode accepted");for(unsigned i=0;i<100;++i){auto repeated=r;repeated.sequence=i+2;check(!plan.offer(repeated,101+i),"repeated click does not trigger periodic inventory");}
 Request out;check(plan.take(100,out)&&out.phase==Phase::Immediate,"immediate no sleep");check(!plan.take(1599,out),"settled inventory never sleeps worker or runs early");check(plan.take(1600,out)&&out.phase==Phase::Settled,"existing caller clock releases settled phase");check(!plan.take(9999,out),"two snapshots only");
 for(unsigned i=1;i<8;++i){auto next=r;next.rule+=i;check(plan.offer(next,2000+i),"new admitted diagnostic episode within fixed budget");}
 auto overflow=r;overflow.rule=100;check(!plan.offer(overflow,2100)&&plan.stats().episodes==8,"process lifetime cap cannot be reset by rule edits");
 unsigned takeCount=0;while(plan.take(4000,out))++takeCount;check(takeCount==14,"eight episodes allocate at most sixteen snapshots");
 Plan invalid;auto bad=r;bad.origin=Origin(99);check(!invalid.offer(bad,100),"unknown origin does not spend budget");bad=r;bad.power=0;check(!invalid.offer(bad,100),"zero power refused");check(!invalid.offer(r,UINT64_MAX-100),"deadline overflow refused");check(invalid.offer(r,100)&&!invalid.take(6101,out)&&invalid.stats().expired==2,"stale metadata is not inspected forever");
 Plan rewind;check(rewind.offer(r,100)&&!rewind.take(99,out)&&rewind.stats().expired==2,"clock rollback is explicit expiry");
 Plan concurrent;std::atomic<unsigned> accepted{0};std::thread a([&]{for(unsigned i=0;i<1000;++i)accepted+=concurrent.offer(r,100);});std::thread b([&]{for(unsigned i=0;i<1000;++i)accepted+=concurrent.offer(r,100);});a.join();b.join();check(accepted==1&&concurrent.stats().episodes==1,"UI and worker publication never duplicate an episode");
 std::cout<<"MSI_FRONT_OBSERVATION_PLAN_OK checks="<<checks<<" hardware_io=none snapshots_per_process_max=16\n";
}