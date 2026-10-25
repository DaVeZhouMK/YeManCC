#include "../native/fan_resume_owner.h"
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
int main() {
 unsigned checks=0,failed=0;
 auto check=[&](bool ok,const char*name){++checks;if(!ok)++failed;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
 ymcc::fan_resume::RequestOwner q;
 check(!q.begin(0),"zero-generation-refused");
 check(q.begin(5),"older-wake-acquires-one-owner");
 check(!q.begin(5)&&q.pending()==0,"duplicate-wake-does-not-queue-another-attempt");
 check(!q.begin(4)&&q.pending()==0,"older-wake-cannot-replace-owner");
 check(!q.begin(6)&&q.pending()==6,"newer-wake-is-retained-while-old-rebuild-is-in-flight");
 check(!q.begin(7)&&!q.begin(6)&&q.pending()==7,"only-latest-newer-wake-survives");
 check(q.finish(4)==0&&!q.begin(7),"wrong-owner-completion-cannot-release-current-owner");
 check(q.finish(5)==7,"superseded-owner-hands-off-latest-wake");
 check(q.pending()==0&&q.begin(7),"handoff-can-enter-without-two-owners");
 check(q.finish(5)==0&&!q.begin(7),"late-old-completion-cannot-release-successor");
 check(q.finish(7)==0,"same-cycle-duplicates-cannot-create-an-endless-loop");
 check(q.begin(100),"concurrent-queue-control-owner-entered");
 std::atomic<unsigned> extraOwners{0};std::vector<std::thread> writers;
 for(unsigned i=101;i<=124;++i)writers.emplace_back([&,i]{if(q.begin(i))++extraOwners;});
 for(auto& t:writers)t.join();
 check(extraOwners==0&&q.pending()==124,"concurrent-new-wakes-coalesce-to-highest-and-create-no-second-owner");
 check(q.finish(100)==124&&q.begin(124),"concurrent-latest-wake-drains-once");q.finish(124);
 ymcc::fan_resume::RequestOwner racing;std::atomic<int> active{0},maxActive{0};writers.clear();
 for(unsigned i=0;i<8;++i)writers.emplace_back([&]{for(unsigned j=0;j<200;++j){if(!racing.begin(200))continue;
  const int n=++active;int prior=maxActive.load();while(n>prior&&!maxActive.compare_exchange_weak(prior,n)){}
  std::this_thread::yield();--active;racing.finish(200);}});
 for(auto& t:writers)t.join();
 check(maxActive==1&&active==0,"racing-entry-and-completion-never-overlap-writers");
 std::cout<<"checks="<<checks<<" failed="<<failed<<"\n";return failed?1:0;
}
