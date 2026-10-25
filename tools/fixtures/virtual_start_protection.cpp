#include "virtual_start_protection.h"
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
static int checks=0,failed=0;
static void check(bool condition,const char* assertion){++checks;if(!condition){++failed;std::cout<<"FAIL "<<assertion<<'\n';}}
int main(){using Gate=ymcc::VirtualStartProtection;
 for(const auto* terminal:{"success","failure"}){
  Gate g;auto a=g.admit("elite",0);check(a.accepted&&a.requestId&&g.view(0).locked,"first request at zero tick locks");
  check(!g.admit("elite",1).accepted,"same persona external repeat rejected");check(!g.admit("steamdeck",1).accepted,"other persona rejected");check(!g.admit("disabled",1).accepted,"OFF rejected while a player transition owns admission");
  check(g.consume("elite",2).accepted&&!g.consume("steamdeck",2).accepted,"consumer inherits only admitted target");
  check(g.finish(a.requestId,terminal,1000),"terminal recorded once");check(!g.finish(a.requestId,std::string(terminal)=="success"?"failure":"success",1001),"opposite terminal cannot overwrite the one final outcome");check(g.view(1001).phase==terminal,"request cannot be both success and failure");check(g.view(10999).locked,"terminal plus9999 remains locked");check(!g.consume("elite",10999).accepted,"internal consumer cannot restart inside terminal settle");
  check(!g.finish(a.requestId,terminal,10999),"duplicate completion cannot extend settle");check(!g.view(11000).locked,"terminal plus10000 releases");
 }
 {Gate g;auto a=g.admit("elite",100);check(g.view(30099).locked,"pending29999 remains locked");check(!g.view(30100).locked,"pending maximum30000 releases");auto b=g.admit("steamdeck",30100);check(b.accepted&&a.requestId!=b.requestId,"new request at maximum accepted");check(!g.finish(a.requestId,"success",30101)&&g.view(30101).phase=="pending","stale ticket cannot settle new request");}
 for(const auto* terminal:{"success","failure"}){Gate g;auto a=g.admit("elite",0);check(g.finish(a.requestId,terminal,29000),"late terminal recorded");check(g.view(29999).locked,"late terminal capped remains locked29999");check(!g.view(30000).locked,"late terminal capped at30000");}
 {Gate g;auto a=g.admit("elite",500);check(g.cancel(700),"pending cancel is accepted");check(!g.cancel(1000),"repeated cancel cannot extend settle");check(!g.admit("elite",10699).accepted,"cancel plus9999 rejects reopen");check(g.admit("elite",10700).accepted,"cancel plus10000 permits reopen");check(!g.finish(a.requestId,"failure",10701),"cancelled old request cannot finish new request");}
 {Gate g;auto a=g.admit("elite",0);check(!g.view(30000).locked,"uncompleted request expires");auto inherited=g.consume("elite",30000);check(inherited.accepted&&!inherited.created&&!g.view(30000).locked,"delayed consumer cannot renew expired30s request");check(!g.finish(a.requestId,"success",30001),"late terminal after expiry cannot relock");}
 {Gate g;auto a=g.admit("elite",10);check(g.view(9).locked&&g.view(9).remainingMs<=Gate::maxMs,"clock rollback conservatively bounded");}
 {Gate g;std::atomic<int> ready{0},accepted{0};std::atomic<bool> start{false};std::vector<std::thread> workers;for(int i=0;i<32;++i)workers.emplace_back([&,i]{++ready;while(!start.load())std::this_thread::yield();for(int j=0;j<1000;++j)if(g.admit(i%3==0?"disabled":i%3==1?"elite":"steamdeck",1000).accepted)++accepted;});while(ready.load()!=32)std::this_thread::yield();start=true;for(auto& w:workers)w.join();check(accepted==1,"32000 concurrent external requests admit exactly one");}
 {Gate g;auto a=g.admit("elite",100);check(g.existingTicket("elite",100)==a.requestId,"pending operational recovery inherits existing request");check(!g.existingTicket("steamdeck",100),"different persona cannot inherit request");g.finish(a.requestId,"success",200);check(g.existingTicket("elite",201)==a.requestId,"successful intent can recover during user settle");check(!g.admit("elite",201).accepted&&g.view(201).locked,"inherited recovery does not unlock external requests");check(g.existingTicket("elite",30200)==a.requestId&&!g.view(30200).locked,"late internal recovery never renews30second deadline");auto b=g.admit("elite",30201);g.cancel(30202);check(!g.existingTicket("elite",30203),"explicitly cancelled intent cannot be inherited");check(b.requestId!=a.requestId,"new admitted request has distinct ownership");}
 {Gate g;const auto request=g.admit("steamdeck",100).requestId;std::atomic<int> winners{0};std::vector<std::thread> workers;for(int i=0;i<32;++i)workers.emplace_back([&,i]{if(g.finish(request,i%2?"success":"failure",200))++winners;});for(auto& w:workers)w.join();check(winners==1,"concurrent terminal receipts commit exactly one outcome");const auto view=g.view(201);check(view.phase=="success"||view.phase=="failure","single immutable terminal has one defined outcome");}
 {Gate g;const auto id=g.admit("steamdeck",0).requestId;check(g.beginVerification(id,"success",1000),"creation candidate enters verification without a final outcome");check(!g.finishVerification(id,"success",10999),"verification cannot commit before the full ten-second check window");auto v=g.view(11000);check(v.locked&&v.verificationPending&&v.phase=="verifying"&&v.verificationRemainingMs==0,"ten seconds alone cannot bypass required recovery verification");check(!g.admit("disabled",11000).accepted,"player OFF cannot bypass unfinished verification");check(g.finishVerification(id,"success",11000),"only completed verification commits final success");check(!g.view(11000).locked&&g.view(11000).phase=="success","single verified exit releases all player switching");check(!g.finishVerification(id,"failure",11001)&&!g.beginVerification(id,"failure",11001),"verified success cannot later also become failure");}
 {Gate g;const auto id=g.admit("steamdeck",0).requestId;g.beginVerification(id,"success",1000);check(!g.beginVerification(id,"failure",9000)&&g.view(9000).candidateOutcome=="failure","checking can invalidate a candidate without publishing or renewing a final");check(g.view(11000).verificationRemainingMs==0,"candidate change does not renew first verification window");check(g.finishVerification(id,"failure",11000)&&g.view(11000).phase=="failure"&&!g.view(11000).locked,"failed verification has one final failure exit");}
 {Gate g;const auto id=g.admit("steamdeck",0).requestId;g.beginVerification(id,"success",27000);check(g.view(29999).locked,"late verification stays locked below hard cap");check(!g.view(30000).locked&&g.view(30000).phase=="expired","incomplete verification exits only as timeout at original thirty-second cap");check(!g.finishVerification(id,"success",30001),"timeout cannot later also report verified success");}
 {Gate g;const auto id=g.admit("steamdeck",100).requestId;
  g.beginVerification(id,"failure",1100);
  check(!g.executionInspectionComplete(11099)&&!g.fallbackAllowed(id,11099)&&!g.reserveFallback(id,11099),"fallback cannot reserve inside execution inspection");
  check(g.view(11099).fallbackAttempts==0,"execution failure and waiting spend no fallback attempt");
  check(g.executionInspectionComplete(11100)&&g.fallbackAllowed(id,11100),"fallback is eligible only after original execution inspection");
  const auto before=g.view(11100);check(before.inspectionStartedElapsedMs==1000&&before.inspectionDeadlineElapsedMs==11000,"timeline is anchored to first execution receipt not admission or retry");check(g.reserveFallback(id,11100),"one post-inspection fallback reservation is accepted");
  const auto reserved=g.view(11100);check(reserved.requestId==id&&reserved.remainingMs==before.remainingMs&&reserved.fallbackAttempts==1,"fallback reservation preserves request and hard deadline");
  check(!g.reserveFallback(id,11100)&&!g.fallbackAllowed(id,11100),"same request cannot reserve a second fallback");
  check(!g.admit("disabled",11100).accepted&&!g.admit("elite",11100).accepted,"fallback cannot be interrupted by player OFF or persona change");
  check(g.finishVerification(id,"success",11100)&&!g.reserveFallback(id,11101),"verified final cannot restart fallback");
 }
 {Gate g;const auto id=g.admit("steamdeck",0).requestId;g.beginVerification(id,"failure",29000);
  check(!g.reserveFallback(id,29999),"late inspection cannot borrow time beyond hard cap");
  check(!g.reserveFallback(id,30000)&&g.view(30000).phase=="expired"&&g.view(30000).fallbackAttempts==0,"hard cap wins over unused fallback budget");
  const auto next=g.admit("steamdeck",30000).requestId;g.beginVerification(next,"failure",30000);
  check(!g.reserveFallback(id,40000)&&g.reserveFallback(next,40000),"stale request cannot spend successor fallback budget");
 }
 {Gate g;const auto id=g.admit("steamdeck",0).requestId;g.beginVerification(id,"failure",0);
  std::atomic<int> winners{0};std::vector<std::thread> workers;
  for(int i=0;i<32;++i)workers.emplace_back([&]{if(g.reserveFallback(id,10000))++winners;});for(auto& w:workers)w.join();
  check(winners==1&&g.view(10000).fallbackAttempts==1,"concurrent fallback dispatch reserves exactly one attempt");
 }
 {Gate g;const auto id=g.admit("steamdeck",100).requestId;
  check(!g.executionInspectionComplete(10100)&&!g.reserveFallback(id,10100),"admission elapsed without execution receipt cannot authorize fallback");
  check(g.view(10100).locked&&g.view(10100).phase=="pending"&&g.view(10100).fallbackAttempts==0&&g.view(10100).inspectionDeadlineElapsedMs==0,"pending execution cannot invent an inspection deadline or spend fallback");
 }
 std::cout<<"CHECKS="<<checks<<" FAILED="<<failed<<'\n';return failed?1:0;
}
