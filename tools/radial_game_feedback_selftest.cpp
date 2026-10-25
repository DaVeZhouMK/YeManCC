#include "../native/game_feedback_dispatch.h"
#include <cassert>
#include <iostream>
int main(){using ymcc::game_feedback::Dispatch;Dispatch d;ymcc::game_feedback::SourceBoundCommand c;c.valid=true;c.motors.present=c.motors.valid=true;c.motors.generation=1;c.motors.revision=1;c.motors.left=40;c.motors.right=80;c.read.window.readCompletedTick=100;
 assert(d.offer(c,true));auto apply=d.take(100);assert(apply&&apply->kind==Dispatch::Kind::Apply);ymcc::touchhaptic::Receipt r;r.ok=true;r.pulseGeneration=10;d.completeApply(*apply,r);assert(d.snapshot().owner==10);
 d.yieldToJob();auto stop=d.take(101);assert(stop&&stop->kind==Dispatch::Kind::Stop&&stop->pulse==10);d.completeStop(*stop,true);assert(d.jobMayRun());
 d.manualStarted(11);d.resumeAfterJob();assert(!d.take(102));d.ownerFinished(10);assert(!d.take(103));d.ownerFinished(11);auto restored=d.take(104);assert(restored&&restored->command.motors.left==40&&restored->command.motors.right==80);
 r.pulseGeneration=12;d.completeApply(*restored,r);d.ownerFinished(11);assert(d.snapshot().owner==12);d.revoke();stop=d.take(105);assert(stop&&stop->pulse==12);d.completeStop(*stop,true);assert(!d.take(106));
 Dispatch expired;c.read.window.readCompletedTick=1;assert(expired.offer(c,true));expired.manualStarted(20);expired.ownerFinished(20);assert(!expired.take(100000));assert(expired.snapshot().expired==1);
 std::cout<<"{\"pass\":true,\"checks\":17,\"consumer\":\"native/game_feedback_dispatch.h\",\"hardwareValidated\":false}";
}
