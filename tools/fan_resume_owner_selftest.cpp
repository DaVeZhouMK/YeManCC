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
 const auto attach=[](auto& owner,const auto& ticket){
  auto binding=std::make_shared<ymcc::fan_resume::RequestAttachment>();
  return std::weak_ptr<ymcc::fan_resume::RequestAttachment>{owner.retainAttachment(ticket,binding)};
 };
 ymcc::fan_resume::RequestOwner returning;
 const auto running=returning.offer(75,s4,100,1,9,4,true);
 const auto runningBinding=attach(returning,running);
 const auto borrower=runningBinding.lock();
 check(bool(borrower),"real-worker-retains-its-exact-host-binding");
 returning.finishRequest(running,101,true);
 check(borrower&&borrower.use_count()==1,"terminal-return-drops-ledger-reference-without-taking-worker-borrow");
 check(returning.registered(75,1,9,4)&&!returning.offer(75,s0,102,1,9,4,true),"terminal-receipt-survives-resource-release-and-cannot-reopen");
 ymcc::fan_resume::RequestOwner handoff;
 const auto previous=handoff.offer(75,s4,100,1,9,4,true);
 const auto previousBinding=attach(handoff,previous);
 check(handoff.checkpoint(previous,{26,2,0,119,12}),"checkpoint-records-both-observation-quotas");
 check(!handoff.checkpoint(previous,{27,2,0,120,11}),"checkpoint-refuses-detail-quota-regression");
 handoff.offer(75,s0,120,1,9,4,true);
 const auto successor=handoff.finishRequest(previous,121,false);
 check(successor&&successor.request==previous.request&&!previousBinding.expired(),"same-request-public-handoff-keeps-exact-host-binding");
 {
  const auto continued=handoff.snapshot(successor);
  check(continued&&continued->observations.loggedProbes==2&&continued->observations.detailedProbeLogs==12&&
        continued->observations.lastLogTick==119,"handoff-retains-independent-observation-quotas");
 }
 handoff.close();
 check(!previousBinding.expired(),"exit-does-not-discard-an-active-original-binding-before-return");
 check(!handoff.finishRequest(successor,122,false)&&previousBinding.expired(),"return-after-exit-drops-binding-and-starts-no-successor");
 ymcc::fan_resume::RequestOwner parked;
 const auto parkedRun=parked.offer(75,s4,100,1,9,4,true);
 const auto parkedBinding=attach(parked,parkedRun);
 parked.finishRequest(parkedRun,101,false);
 check(!parkedBinding.expired(),"parked-original-request-keeps-binding-for-a-current-ready");
 parked.close();
 check(parkedBinding.expired(),"exit-releases-parked-binding-with-no-worker-left-to-return");
 ymcc::fan_resume::RequestOwner deadline;
 const auto timed=deadline.offer(75,s4,100,1,9,4,true);
 const auto timedBinding=attach(deadline,timed);
 deadline.finishRequest(timed,101,false);
 check(!deadline.offer(75,s0,15100,1,9,4,true)&&timedBinding.expired(),"ready-at-original-deadline-releases-parked-binding-without-restarting");
 ymcc::fan_resume::RequestOwner passiveDeadline;
 const auto passive=passiveDeadline.offer(75,s4,100,1,9,4,true);
 const auto passiveBinding=attach(passiveDeadline,passive);
 passiveDeadline.finishRequest(passive,101,false);
 check(passiveDeadline.retainedSessionManual(75,75,9,4,15099)==1&&!passiveBinding.expired(),"public-ready-lookup-keeps-live-manual-binding");
 check(!passiveDeadline.retainedSessionManual(75,75,9,4,15100)&&passiveBinding.expired(),"public-ready-lookup-releases-expired-manual-binding-without-offering-work");
 ymcc::fan_resume::RequestOwner launch;
 const auto failedStart=launch.offer(75,s4,100,0,9,4);
 const auto retryStart=launch.finishUnstartedRequest(failedStart,101);
 const auto launchState=launch.snapshot(retryStart);
 check(launchState&&retryStart.request==failedStart.request&&retryStart.run!=failedStart.run&&
     launchState->deadlineTick==15100&&launchState->attempts==0,"failed-thread-creation-retries-once-with-original-deadline");
 check(!launch.finishUnstartedRequest(failedStart,102)&&launch.snapshot(retryStart).has_value(),"stale-launch-return-cannot-release-the-retry");
 check(!launch.finishUnstartedRequest(retryStart,103)&&!launch.offer(75,s0,104,0,9,5),"persistent-launch-failure-does-not-restart-same-business");
 const auto afterLaunchFailure=launch.offer(75,s0,105,1,9,5,true);
 check(afterLaunchFailure&&afterLaunchFailure.request!=failedStart.request,"later-manual-request-is-admitted-after-launch-budget-ends");
 ymcc::fan_resume::RequestOwner launchBoundary;
 const auto oldLaunch=launchBoundary.offer(75,s4,100,0,9,4);
 launchBoundary.offer(75,s0,101,1,9,5,true);
 const auto newLaunch=launchBoundary.finishUnstartedRequest(oldLaunch,102);
 const auto newLaunchState=launchBoundary.snapshot(newLaunch);
 check(newLaunchState&&newLaunch.request!=oldLaunch.request&&newLaunchState->manualOperation==1,"unstarted-old-run-hands-off-queued-manual-request-first");
 check(!launchBoundary.finishUnstartedRequest(newLaunch,15101),"failed-creation-cannot-renew-an-expired-request");
 ymcc::fan_resume::RequestOwner launchRefresh;
 const auto earlierLaunch=launchRefresh.offer(75,s4,100,0,9,4);
 launchRefresh.offer(75,s0,101,0,9,5);
 const auto refreshedLaunch=launchRefresh.finishUnstartedRequest(earlierLaunch,102);
 const auto refreshedState=launchRefresh.snapshot(refreshedLaunch);
 check(refreshedState&&refreshedLaunch.request==earlierLaunch.request&&refreshedState->power.same(s0)&&
     refreshedState->writeRevision==5&&refreshedState->deadlineTick==15100,"launch-retry-prefers-current-public-power-without-refunding-the-deadline");
 std::cout<<"checks="<<checks<<" failed="<<failed<<"\n";return failed?1:0;
}
