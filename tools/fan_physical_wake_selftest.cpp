#include "../native/fan_physical_wake.h"
#include <iostream>
#include <thread>
#include <atomic>
using ymcc::fan_wake::Observation;
int checks=0,failures=0;
void check(const char* name,bool ok){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";}
int main(){
 Observation o;
 check("initial awake generation unchanged",o.generation(1)==1 && o.ready(1));
 check("wake without sleep rejected",!o.recordWake(1,100,false,true));
 const auto g=o.confirmedSuspend(2);
 check("normal shared F4 binds Fan generation",g==2);
 check("suspend blocks native Fan writes",!o.ready(g) && o.awaitingWake(g));
 check("only one Close owner",o.beginClose(g) && !o.beginClose(g));
 check("duplicate confirmed F4 is stable",o.confirmedSuspend(2)==g);
 check("paired raw 506 does not mint duplicate generation",o.recordSleep(2,100)==g);
 check("equal/earlier timestamp wake rejected",!o.recordWake(2,100,false,true) && !o.recordWake(2,99,false,true));
 check("wrong shared generation wake rejected",!o.recordWake(1,101,false,true));
 check("filtered real wake is retained",o.recordWake(2,196,false,true));
 check("real wake cannot overtake unfinished Close",o.physical(g) && !o.ready(g) && !o.claimReady(g));
 check("serialized Close finishes",o.completeClose(g)==0);
 check("filtered wake is now admitted for Fan",o.ready(g) && o.claimReady(g));
 check("publication is once per cycle",!o.claimReady(g));
 // Real repeated sleep while shared policy is STILL Suspended/same generation.
 const auto g2=o.recordSleep(2,300);
 check("same shared generation real new sleep gets newer Fan generation",g2>g && o.generation(2)==g2);
 check("new physical sleep immediately fences prior write",!o.ready(g) && !o.ready(g2));
 check("new private Close is accepted instead of Host stale same-generation",o.beginClose(g2));
 check("old/duplicate raw facts cannot undo new sleep",!o.recordWake(2,196,false,true) && !o.recordSleep(2,100));
 check("old shared F5 cannot acknowledge new private cycle",o.sharedResume(2)==0);
 check("new paired wake retained",o.recordWake(2,400,false,true));
 check("new cycle waits for its own Close",!o.ready(g2) && !o.claimReady(g2));
 check("second serialized Close completed",o.completeClose(g2)==0 && o.ready(g2));
 check("new cycle can publish",o.claimReady(g2));
 // Overlap: first Close is still running when another sleep arrives.
 const auto g3=o.recordSleep(2,500);check("third Close starts",o.beginClose(g3));
 check("third wake retained",o.recordWake(2,600,false,true));
 const auto g4=o.recordSleep(2,700);
 check("newer Close coalesced while owner busy",g4>g3 && !o.beginClose(g4));
 check("newest wake retained",o.recordWake(2,800,false,true));
 check("older Close cannot release latest write gate",o.completeClose(g3)==g4 && !o.ready(g4));
 check("only latest Close completion releases new gate",o.completeClose(g4)==0 && o.ready(g4));
 check("old completion cannot drain twice",o.completeClose(g3)==0 && o.ready(g4));
 o.retryBoundary(2,900);
 check("accepted re-sleep boundary invalidates physical wake",!o.ready(g4) && !o.physical(g4));
 check("late old wake below retry cutoff rejected",!o.recordWake(2,850,false,true));
 check("active retry cannot admit new wake",!o.recordWake(2,1000,true,true) && !o.ready(g4));
 const auto g5=o.recordSleep(2,1100);o.beginClose(g5);o.completeClose(g5);
 check("new cycle after retry needs fresh wake",!o.ready(g5));
 check("new matched wake after retry accepted",o.recordWake(2,1200,false,true) && o.ready(g5));
 const auto g6=o.confirmedSuspend(3);
 check("next public generation preserves monotonic Fan generation",g6>g5 && o.generation(3)==g6);
 check("stale shared suspend cannot roll back Fan",o.confirmedSuspend(2)==0 && o.generation(3)==g6);
 o.beginClose(g6);o.abandonClose(g6);
 check("failed thread schedule does not manufacture Close proof",o.needsClose(g6) && !o.ready(g6));
 check("manual retry may schedule Close again",o.explicitResume(g6) && o.beginClose(g6));
 check("manual still waits for Close worker boundary",!o.ready(g6));
 check("manual completion can resume",o.completeClose(g6)==0 && o.ready(g6));
 Observation concurrent;const auto cg=concurrent.confirmedSuspend(2);std::atomic<int> owners{0};std::thread threads[8];
 for(auto& t:threads)t=std::thread([&]{if(concurrent.beginClose(cg))++owners;});for(auto& t:threads)t.join();
 check("concurrent callbacks still have one Close owner",owners==1);
 std::cout<<"checks="<<checks<<" failures="<<failures<<" scope=production-Fan-cycle-metadata-and-Close-owner-no-hardware\n";
 return failures?1:0;
}
