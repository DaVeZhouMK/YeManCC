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
 const ymcc::fan_resume::PowerStamp s4{75,5,11,21,0,3}, s0{75,5,13,24,0,3};
 ymcc::fan_resume::RequestOwner mixed;
 const auto first=mixed.offer(75,s4,100,0,9,4);
 const auto post=mixed.reservePost(first,101);
 check(bool(post)&&mixed.completePost(post,false),"s4-host-accepts-original-resume-once");
 check(!mixed.offer(75,s0,110,0,9,5)&&mixed.pending()==75,"late-s0-retains-successor-without-overlapping-owner");
 const auto next=mixed.finishRequest(first,111,false);
 auto transferred=mixed.snapshot(next);
 check(transferred&&next.request==first.request&&next.run!=first.run&&transferred->power.same(s0)&&transferred->writeRevision==5,"later-wake-transfers-the-fresh-permit-with-the-original-request");
 check(transferred&&transferred->deadlineTick==15100&&transferred->attempts==1&&transferred->posts==1&&!mixed.reservePost(next,112),"handoff-keeps-first-deadline-and-never-reposts-accepted-command");
 check(!mixed.finishRequest(first,113,true)&&mixed.snapshot(next).has_value(),"late-original-cannot-settle-the-successor");
 mixed.finishRequest(next,114,false);
 check(!mixed.offer(75,s0,115,0,9,6),"write-revision-alone-is-not-a-new-wake");
 const ymcc::fan_resume::PowerStamp ready{75,5,14,25,0,3};
 const auto idle=mixed.offer(75,ready,116,0,9,7);
 const auto idleState=mixed.snapshot(idle);
 check(idleState&&idleState->writeRevision==7&&idle.request==first.request&&idleState->attempts==1,"idle-handoff-uses-the-same-fresh-permit-rule");
 std::cout<<"checks="<<checks<<" failed="<<failed<<"\n";return failed?1:0;
}
