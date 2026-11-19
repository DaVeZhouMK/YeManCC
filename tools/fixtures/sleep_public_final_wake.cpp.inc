#include <atomic>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <thread>
#include <vector>
#include <string>
#include <algorithm>
#include "json.hpp"
#include "sleep_wake_admission.h"
using namespace ymcc::sleeppower;using json=nlohmann::json;using U=std::uint64_t;
static constexpr U gen=5013643,boundary=134392907000000000ULL,old107=134392908258301446ULL,entry=134392908514042653ULL,final507=134392908524461397ULL;
static bool entryJoinOnly=false;static std::string entryJoinCase;
static unsigned checks=0,failed=0;static json rows=json::array();
static void require(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
static void test(const char* name,const std::function<void()>& body){if(entryJoinOnly&&std::string(name).find("entry-join-")!=0)return;if(!entryJoinCase.empty()&&entryJoinCase!=name)return;auto n=checks;try{body();rows.push_back({{"name",name},{"pass",true},{"checks",checks-n}});}catch(const std::exception& e){++failed;rows.push_back({{"name",name},{"pass",false},{"reason",e.what()},{"checks",checks-n}});}}
static WakeProjection projection(WakeFactsOwner& owner,U epoch=9){std::lock_guard lock(owner.mutex);return owner.snapshotForLocked(gen,epoch,boundary,gen);}
static CompletedWake tuple(const WakeProjection& p,U epoch=9,U now=final507+10000){return {gen,epoch,boundary,gen,
 p.readyCommit.generation,p.readyCommit.configEpoch,p.readyCommit.boundaryFileTime,
 p.interactiveResume.generation,p.interactiveResume.configEpoch,p.interactiveResume.boundaryFileTime,p.interactiveResume.eventFileTime,
 p.completedResume.generation,p.completedResume.configEpoch,p.completedResume.boundaryFileTime,p.completedResume.eventFileTime,
 p.kernelSleepEntry.eventFileTime,now,60000,p.completedResume.eventId,true,false,true};}
static void join(WakeFactsOwner& owner){owner.kernel(gen,506,1,-1,entry,final507);owner.interactive(gen,7,4000,4000,final507);owner.kernel(gen,507,1,-1,final507,final507);owner.ready(gen);}
int main(int argc,char** argv){
 if(argc==2&&std::string(argv[1])=="--entry-join-only")entryJoinOnly=true;
 else if(argc==3&&std::string(argv[1])=="--entry-join-case"){entryJoinOnly=true;entryJoinCase=argv[2];}
 else if(argc!=1)return 64;

 test("public-native-facts-independent-of-consumer-off",[]{WakeFactsOwner owner;bool steamEnabled=false;(void)steamEnabled;join(owner);auto raw=owner.snapshot();require(raw.completedResume.sourceGeneration==gen&&raw.readyCommit.sourceGeneration==gen,"private-config-used-as-source");require(completedWake(tuple(projection(owner)))==CompletedWakeResult::Allowed,"public-native-tuple-unavailable-with-consumer-off");});
 test("consumer-epoch-is-only-projection",[]{WakeFactsOwner owner;join(owner);auto a=projection(owner,9),b=projection(owner,77);require(a.completedResume.configEpoch==9&&b.completedResume.configEpoch==77,"consumer-tag-missing");require(owner.snapshot().completedResume.sourceGeneration==gen,"projection-mutated-native-source");require(completedWake(tuple(b,77))==CompletedWakeResult::Allowed,"current-owner-projection-unusable");});
 test("copy-cannot-mutate-public-evidence",[]{WakeFactsOwner owner;join(owner);auto copy=owner.snapshot();copy.completedResume.eventFileTime=1;require(owner.snapshot().completedResume.eventFileTime==final507,"borrowed-mutable-source");});
 test("raw-ready-pbt-or-exit-alone-grants-nothing",[]{for(int i=0;i<3;++i){WakeFactsOwner owner;if(i!=0)owner.ready(gen);if(i!=1)owner.interactive(gen,7,4000,4000,final507);if(i!=2)owner.kernel(gen,507,1,-1,final507,final507);require(completedWake(tuple(projection(owner)))!=CompletedWakeResult::Allowed,"incomplete-join-granted");}});
 test("automatic18-never-interactive",[]{WakeFactsOwner owner;owner.interactive(gen,18,4000,4000,final507);owner.kernel(gen,507,1,-1,final507,final507);owner.ready(gen);require(completedWake(tuple(projection(owner)))!=CompletedWakeResult::Allowed,"automatic18-permitted");});
 test("later-native-entry-revokes-retained-completion",[]{WakeFactsOwner owner;join(owner);owner.kernel(gen,506,1,-1,final507+1,final507+1);require(completedWake(tuple(projection(owner),9,final507+2))!=CompletedWakeResult::Allowed,"new-entry-used-old-final");});
 test("handler-completion-is-not-an-exit",[]{WakeFactsOwner owner;owner.kernel(gen,506,1,-1,entry,final507);owner.ready(gen);owner.interactive(gen,7,4000,4000,final507);require(completedWake(tuple(projection(owner)))!=CompletedWakeResult::Allowed,"ready-stands-for-native-exit");});
 test("duplicate-pbt-and-exit-do-not-refresh-final-tuple",[]{WakeFactsOwner owner;join(owner);owner.interactive(gen,7,5000,5000,final507+10000000);owner.kernel(gen,507,1,-1,final507+10000000,final507+10000000);owner.ready(gen);auto p=projection(owner);require(p.interactiveResume.eventFileTime==final507&&p.completedResume.eventFileTime==final507,"duplicate-refunded-native-proof");});
 test("out-of-order-old-entry-never-replaces-new-entry",[]{WakeFactsOwner owner;owner.kernel(gen,506,1,-1,entry,final507);owner.kernel(gen+1,506,1,-1,entry-1,final507);require(owner.snapshot().kernelSleepEntry.eventFileTime==entry,"old-entry-overwrote-new-native-time");});
 test("old107-does-not-borrow-new507",[]{WakeFactsOwner owner;owner.kernel(gen,107,0,5,old107,old107);owner.kernel(gen,506,1,-1,entry,final507);owner.ready(gen);owner.interactive(gen,7,4000,4000,final507);require(completedWake(tuple(projection(owner)))!=CompletedWakeResult::Allowed,"old107-borrowed-new-ready");owner.kernel(gen,507,1,-1,final507,final507);require(completedWake(tuple(projection(owner)))==CompletedWakeResult::Allowed,"new507-did-not-own-its-join");});
 test("native-generation-change-cannot-reuse-old-ready",[]{WakeFactsOwner owner;join(owner);std::lock_guard lock(owner.mutex);auto p=owner.snapshotForLocked(gen+2,9,boundary,gen+2);require(p.readyCommit.generation==0&&p.completedResume.generation==0,"old-generation-revived");});
 test("native-unknown-future-time-refuses",[]{WakeFactsOwner owner;owner.kernel(gen,507,1,-1,final507+1,final507);owner.interactive(gen,7,5001,5000,final507);owner.ready(gen);require(completedWake(tuple(projection(owner)))!=CompletedWakeResult::Allowed,"future-proof-granted");});
 test("continuation-keeps-original-thirty-and-sixty-second-anchors",[]{WakeContinuation c;WakeIdentity id{gen,9,7,boundary,10708,134392903158823114ULL};c.begin(id,1000,1000);require(c.park(id,1001,old107,107,old107,true,true),"zero-action-not-parked");require(c.canContinue(id,30999,final507,true,true,true,false,true,507,final507,507,final507),"remaining-ready-window-refused");require(!c.canContinue(id,31000,final507,true,true,true,false,true,507,final507,507,final507),"thirty-second-equality-refunded");require(c.operationCurrent(old107+59999ULL*10000)&&!c.operationCurrent(old107+60000ULL*10000),"original-utc-window-refunded");});
 test("continuation-reservation-revocation-and-original-identity-veto",[]{WakeIdentity id{gen,9,7,boundary,10708,134392903158823114ULL};for(int i=0;i<4;++i){WakeContinuation c;c.begin(id,1000,1000);auto changed=id;if(i==2)++changed.birth;if(i==3)++changed.request;require(!c.park(changed,1001,old107,107,old107,i!=0,i!=1),"reserved-revoked-or-foreign-episode-parked");}});
 test("shared-snapshot-coherence-during-native-publications",[]{WakeFactsOwner owner;std::atomic<bool> done=false;std::atomic<unsigned> torn=0;std::thread writer([&]{for(U i=1;i<=4096;++i)owner.kernel(i,506,1,-1,entry+i,entry+5000);done=true;});while(!done.load()){auto p=owner.snapshot();if(p.kernelSleepEntry.generation&&p.kernelSleepEntry.generation!=p.kernelSleepEntry.sourceGeneration)++torn;}writer.join();require(torn==0&&owner.snapshot().kernelSleepEntry.generation==4096,"native-snapshot-torn-or-publication-lost");});

 test("source-revision-progress-is-idempotent-for-duplicates",[]{WakeFactsOwner owner;require(owner.snapshot().revision==0,"fresh-owner-known");require(owner.kernel(gen,506,1,-1,entry,final507),"new-native-entry-unreported");auto first=owner.snapshot().revision;require(!owner.kernel(gen,506,1,-1,entry,final507)&&owner.snapshot().revision==first,"duplicate-entry-advanced-source");join(owner);auto ready=owner.snapshot().revision;owner.ready(gen);require(owner.snapshot().revision==ready,"duplicate-ready-advanced-source");require(projection(owner).sourceRevision==ready,"projection-source-revision-not-current");});
 test("new-final-fact-redelivers-same-power-without-business-permit",[]{SharedPowerV1 old;old.generation=gen;old.phase=0;old.flags=kKnown|kHardwareAllowed;old.publicationRevision=7;auto now=old;now.publicationRevision=8;require(wakeDeliveryAlreadyCurrent(true,old,now,4,4),"same-fact-redelivered");require(!wakeDeliveryAlreadyCurrent(true,old,now,4,5),"new-public-proof-swallowed-by-power-dedup");require(!wakeDeliveryAlreadyCurrent(false,old,now,4,4),"first-delivery-swallowed");now.observedNotificationSequence=1;require(!wakeDeliveryAlreadyCurrent(true,old,now,4,4),"new-revocation-swallowed");});

 test("prepared-boundary-transfer-preserves-native-source-and-current-request",[]{WakeFactsOwner owner;owner.kernel(gen,507,1,-1,final507,final507);owner.interactive(gen,7,4000,4000,final507);owner.ready(gen+1);std::lock_guard lock(owner.mutex);owner.requestedLocked(gen+1,gen);auto p=owner.snapshotForLocked(gen+1,9,boundary,gen);require(p.kernelWake.sourceGeneration==gen&&p.kernelWake.requestGeneration==gen+1&&p.kernelWake.uiWakeRequested,"native-source-confused-with-consumer-epoch");auto wrong=owner.snapshotForLocked(gen+2,9,boundary,gen);require(wrong.kernelWake.generation==0,"unrelated-transfer-admitted");});

 // A previous interactive ingress is older than this original entry; it
 // cannot become a completed tuple by borrowing the following exit/Ready.
 const U e=boundary+10000000ULL,c=e+10000ULL,oldInteractive=e-10000ULL,validInteractive=e+20000ULL,now=e+10000000ULL;
 const auto oldPbt=[&](WakeFactsOwner& owner){owner.interactive(gen,7,2999,4000,now);};
 const auto newPbt=[&](WakeFactsOwner& owner){owner.interactive(gen,7,3002,4000,now);};
 const auto enter=[&](WakeFactsOwner& owner){owner.kernel(gen,506,1,-1,e,now);};
 const auto leave=[&](WakeFactsOwner& owner,int id=507){owner.kernel(gen,id,5,id==107?5:-1,c,now);};
 const auto allowed=[&](WakeFactsOwner& owner){return completedWake(tuple(projection(owner),9,now))==CompletedWakeResult::Allowed;};
 test("entry-join-old-pbt-before-current-entry-refuses",[&]{for(int id:{507,107}){WakeFactsOwner owner;oldPbt(owner);enter(owner);leave(owner,id);owner.ready(gen);require(!allowed(owner),"previous-interactive-borrowed-new-exit-ready");}});
 test("entry-join-equal-interactive-entry-refuses",[&]{WakeFactsOwner owner;owner.interactive(gen,7,3000,4000,now);enter(owner);leave(owner);owner.ready(gen);require(!allowed(owner),"equal-entry-interactive-granted");});
 test("entry-join-late-valid-pbt-can-replace-unqualified-tuple",[&]{WakeFactsOwner owner;oldPbt(owner);enter(owner);leave(owner);owner.ready(gen);newPbt(owner);auto p=projection(owner);require(p.interactiveResume.eventFileTime==validInteractive,"unqualified-retained-pbt-blocks-current-proof");require(allowed(owner),"new-current-interactive-did-not-self-heal");});
 test("entry-join-late-entry-invalidates-first-retention",[&]{WakeFactsOwner owner;oldPbt(owner);leave(owner);owner.ready(gen);enter(owner);auto p=projection(owner);require(p.kernelSleepEntry.eventFileTime==e,"retained-copy-hid-late-current-entry");require(!allowed(owner),"late-entry-did-not-revoke-old-pbt");});
 test("entry-join-late-entry-then-valid-pbt-self-heals",[&]{WakeFactsOwner owner;oldPbt(owner);leave(owner);owner.ready(gen);enter(owner);newPbt(owner);require(projection(owner).interactiveResume.eventFileTime==validInteractive&&allowed(owner),"late-join-still-pinned-to-old-pbt");});
 test("entry-join-equal-entry-revokes-retained-interactive",[&]{WakeFactsOwner owner;owner.interactive(gen,7,3000,4000,now);leave(owner);owner.ready(gen);enter(owner);require(!allowed(owner),"equal-late-entry-not-revoked");});
 test("entry-join-entry-before-interactive-keeps-first-valid-tuple",[&]{WakeFactsOwner owner;newPbt(owner);leave(owner);owner.ready(gen);enter(owner);auto first=projection(owner);owner.interactive(gen,7,3100,4100,now+1000000ULL);require(first.interactiveResume.eventFileTime==validInteractive&&projection(owner).interactiveResume.eventFileTime==validInteractive,"qualified-tuple-refreshed-by-late-entry");});
 test("entry-join-completed-validator-refuses-direct-old-interactive",[&]{WakeFactsOwner owner;enter(owner);newPbt(owner);leave(owner);owner.ready(gen);auto v=tuple(projection(owner),9,now);v.interactiveAt=oldInteractive;require(completedWake(v)!=CompletedWakeResult::Allowed,"public-validator-only-checked-exit-after-entry");});
 test("entry-join-all24-old-ingress-orderings-refuse",[&]{std::vector<int> order{0,1,2,3};unsigned count=0;do{WakeFactsOwner owner;for(int op:order){if(op==0)oldPbt(owner);if(op==1)enter(owner);if(op==2)leave(owner);if(op==3)owner.ready(gen);}require(!allowed(owner),"some-old-pbt-permutation-granted");++count;}while(std::next_permutation(order.begin(),order.end()));require(count==24,"permutation-count");});
 test("entry-join-all120-current-ingress-orderings-self-heal",[&]{std::vector<int> order{0,1,2,3,4};unsigned count=0;do{WakeFactsOwner owner;for(int op:order){if(op==0)oldPbt(owner);if(op==1)enter(owner);if(op==2)leave(owner);if(op==3)owner.ready(gen);if(op==4)newPbt(owner);}
 // An early Ready followed by entry before any exit is revoked. The real
 // shared projection republishes its still-current Ready after source change.
 if(projection(owner).readyCommit.generation!=gen)require(!allowed(owner),"revoked-ready-granted");
 owner.ready(gen);require(allowed(owner)&&projection(owner).interactiveResume.eventFileTime==validInteractive,"current-pbt-ordering-stranded");++count;}while(std::next_permutation(order.begin(),order.end()));require(count==120,"permutation-count");});
 test("entry-join-late-entry-freezes-already-current-tuple-without-refund",[&]{WakeFactsOwner owner;oldPbt(owner);leave(owner);owner.ready(gen);newPbt(owner);enter(owner);require(allowed(owner),"late-valid-tuple-unavailable");owner.interactive(gen,7,3100,4100,now+1000000ULL);owner.kernel(gen,507,5,-1,c+1000000ULL,now);owner.ready(gen);auto p=projection(owner);require(p.interactiveResume.eventFileTime==validInteractive&&p.completedResume.eventFileTime==c,"late-entry-valid-tuple-not-frozen");});
 test("entry-join-newer-than-completion-never-borrows-old-facts",[&]{WakeFactsOwner owner;enter(owner);newPbt(owner);leave(owner);owner.ready(gen);owner.kernel(gen,506,1,-1,c+30000ULL,now);require(!allowed(owner),"new-entry-borrowed-completion");});


 test("entry-join-public-continuation-keeps-original-anchors",[&]{WakeIdentity id{gen,9,7,boundary,10708,134392903158823114ULL};WakeContinuation kept;kept.begin(id,1000,1000);
 require(kept.parkInteractiveEntry(id,1001,now,oldInteractive,c,e,true,true),"late-interactive-not-retained");
 require(kept.armed()==1000&&kept.stage()==1000&&kept.retiredEvent()==oldInteractive,"late-interactive-anchor-refunded");
 require(kept.canContinue(id,30999,now,true,true,true,false,true,507,c,0,0),"current-join-not-permitted");
 require(!kept.canContinue(id,31000,now,true,true,true,false,true,507,c,0,0),"original-thirty-second-limit-refunded");
 require(!kept.operationCurrent(oldInteractive+60000ULL*10000),"original-sixty-second-limit-refunded");
 for(int i=0;i<8;++i){WakeContinuation denied;denied.begin(id,1000,1000);auto other=id;if(i==2)++other.request;if(i==3)++other.birth;
  require(!denied.parkInteractiveEntry(other,1001,now,i==5?e+1:oldInteractive,i==6?e:i==7?now+1:c,i==4?0:e,i!=0,i!=1)&&
          !denied.awaiting(),"foreign-reserved-or-invalid-entry-retained");
  if(i==7)break;
 }
 });

 if(rows.empty())return 64;
 std::cout<<"PUBLIC_FINAL_WAKE "<<json{{"tests",rows.size()},{"checks",checks},{"failed",failed},{"cases",rows},{"livePowerActions",0}}.dump()<<"\n";return failed?1:0;
}
