#include <algorithm>
#include <cmath>
#include "../native/game_feedback_dispatch.h"
#include <iostream>
#include <stdexcept>
static unsigned checks=0;
static void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
// Unlike assert(), these original checks also execute in the Release build.
#define assert(value) check(bool(value),#value)
int main(){try{using ymcc::game_feedback::Dispatch;Dispatch d;ymcc::game_feedback::SourceBoundCommand c;c.valid=true;c.motors.present=c.motors.valid=true;c.motors.generation=1;c.motors.revision=1;c.motors.left=40;c.motors.right=80;c.read.window.readCompletedTick=100;
 assert(d.offer(c,true));auto apply=d.take(100);assert(apply&&apply->kind==Dispatch::Kind::Apply);ymcc::touchhaptic::Receipt r;r.ok=true;r.pulseGeneration=10;d.completeApply(*apply,r);assert(d.snapshot().owner==10);
 d.yieldToJob();auto stop=d.take(101);assert(stop&&stop->kind==Dispatch::Kind::Stop&&stop->pulse==10);d.completeStop(*stop,true);assert(d.jobMayRun());
 d.manualStarted(11);d.resumeAfterJob();assert(!d.take(102));d.ownerFinished(10);assert(!d.take(103));d.ownerFinished(11);auto restored=d.take(104);assert(restored&&restored->command.motors.left==40&&restored->command.motors.right==80);
 r.pulseGeneration=12;d.completeApply(*restored,r);d.ownerFinished(11);assert(d.snapshot().owner==12);d.revoke();stop=d.take(105);assert(stop&&stop->pulse==12);d.completeStop(*stop,true);assert(!d.take(106));
 Dispatch expired;c.read.window.readCompletedTick=1;assert(expired.offer(c,true));expired.manualStarted(20);expired.ownerFinished(20);assert(!expired.take(100000));assert(expired.snapshot().expired==1);
 // The actual writer runs outside the serial mutex. A new accepted command,
 // OFF, or a manual pulse can therefore arrive before its completion returns.
 for(bool returnedPulse:{false,true})for(unsigned change=0;change<4;++change){
  Dispatch pending;c.read.window.readCompletedTick=100;c.ticket={1,1,1,false};
  c.source={ymcc::touchhaptic::Backend::XInput,1,1,1,0,1};c.motors.generation=1;c.motors.revision=1;
  assert(pending.offer(c,true));auto old=pending.take(100);assert(old.has_value());
  auto newer=c;newer.motors.revision=2;
  if(change==0)newer.motors.left=90; // Same source, a later game motor intent.
  if(change==1){++newer.ticket.host;++newer.motors.generation;}
  if(change==2){++newer.ticket.power;++newer.source.power;}
  if(change==3)++newer.ticket.intent;
  assert(pending.offer(newer,true));
  ymcc::touchhaptic::Receipt failed;failed.reason=ymcc::touchhaptic::Reason::WriteFailed;
  failed.attempted=true;failed.pulseGeneration=returnedPulse?30:0;
  pending.completeApply(*old,failed);
  check(pending.snapshot().desired,"obsolete failure must preserve newer accepted intent");
  check(!pending.snapshot().busy,"obsolete failure must release the completed work claim");
  if(returnedPulse){
   auto zero=pending.take(101);check(zero&&zero->kind==Dispatch::Kind::Stop&&zero->pulse==30,"failed old pulse retains exact Stop duty before new Apply");
   pending.completeStop(*zero,false);check(!pending.take(102),"unproven old pulse return must block new physical Apply");
   pending.ownerFinished(30);
  }
  auto next=pending.take(103);check(next&&next->kind==Dispatch::Kind::Apply,"new command must remain schedulable after old failure");
  check(next->command.ticket.host==newer.ticket.host&&next->command.ticket.power==newer.ticket.power&&next->command.ticket.intent==newer.ticket.intent&&next->command.motors.revision==2&&next->command.motors.left==newer.motors.left,"next work uses newest identity and motors");
  failed.pulseGeneration=0;pending.completeApply(*next,failed);
  check(!pending.snapshot().desired&&!pending.take(104),"current failure retires itself without infinite retries");
 }
 for(bool succeeded:{false,true}){
  Dispatch off;c.read.window.readCompletedTick=100;assert(off.offer(c,true));auto old=off.take(100);assert(old.has_value());
  off.revoke();ymcc::touchhaptic::Receipt late;late.ok=succeeded;late.pulseGeneration=40;late.attempted=true;
  off.completeApply(*old,late);check(!off.snapshot().desired,"late completion after OFF cannot restore desired state");
  auto zero=off.take(101);check(zero&&zero->kind==Dispatch::Kind::Stop&&zero->pulse==40,"late owned pulse is cleaned after OFF");
  off.completeStop(*zero,true);check(!off.take(102)&&!off.interested(),"OFF remains terminal after exact pulse cleanup");
 }
 // A later fresh command is allowed; accumulated old failures are never replayed.
 Dispatch repeated;
 for(unsigned i=0;i<1000;++i){
  c.read.window.readCompletedTick=100+i*2;c.motors.revision=i*2+1;
  assert(repeated.offer(c,true));auto old=repeated.take(100+i*2);assert(old.has_value());
  auto newer=c;++newer.motors.revision;newer.motors.right=90;assert(repeated.offer(newer,true));
  ymcc::touchhaptic::Receipt failed;failed.reason=ymcc::touchhaptic::Reason::SourceChanged;
  repeated.completeApply(*old,failed);auto current=repeated.take(101+i*2);
  check(current&&current->command.motors.revision==newer.motors.revision,"repeated old failures cannot discard current revision");
  repeated.completeApply(*current,failed);check(!repeated.interested(),"completed cycle retains no work or pulse owner");
 }
 std::cout<<"{\"pass\":true,\"checks\":"<<checks<<",\"consumer\":\"native/game_feedback_dispatch.h\",\"hardwareValidated\":false}";return 0;
 }catch(const std::exception& e){std::cout<<"{\"pass\":false,\"checks\":"<<checks<<",\"error\":\""<<e.what()<<"\",\"hardwareValidated\":false}";return 2;}
}
