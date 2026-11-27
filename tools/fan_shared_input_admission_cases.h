// Included only by the explicit Fan fixture selection. The fields are the
// ones read by the verbatim frame gate; unrelated submission state is absent.
struct InputFrameSubmission {
 ::ymcc::input::frameSubmission::Ticket ticket{};
 ::ymcc::sleeppower::SharedPowerV1 sourcePower{};
 ::std::shared_ptr<::ymcc::inputHost::sharedPower::Scope> powerScope;
 bool physicalSourceAdmitted=true,rejected=false;
 ::ymcc::input::frameSubmission::Verdict reason=::ymcc::input::frameSubmission::Verdict::Allowed;
};
#include "fan_shared_input_native_frame.inc"

static void testSharedInputAdmission(bool control){
 using Power=::ymcc::sleeppower::SharedPowerV1;
 using Verdict=::ymcc::input::frameSubmission::Verdict;
 ::ymcc::sleeppower::RevocationMirrors mirrors;
 ::ymcc::inputHost::sharedPower::Cache cache;
 reset();active=true;dispatchFact(506,100);dispatchFact(507,110);drain();
 const auto fanGeneration=currentFanPowerGeneration();
 fanCloseWake(fanGeneration);
 const auto fanOnly=sharedPowerLifecycleSnapshot();
 check("Fan-only wake has an independent nonzero generation",fanGeneration!=currentPowerGeneration()&&
       fanOnly.value("fanWakeGeneration",0ULL)==fanGeneration);
 const auto captureFrame=[&]{
  InputFrameSubmission frame;
  frame.powerScope=cache.acquire(&readSharedSystemPower,mirrors,false);
  if(frame.powerScope)frame.sourcePower=frame.powerScope->power();
  return frame;
 };
 const auto observe=[](const char* stage,const Power& power,const json& fan){
  ::std::cout<<"OBS "<<json{{"stage",stage},{"generation",power.generation},{"suspendSerial",power.suspendSerial},
   {"notificationSequence",power.observedNotificationSequence},{"publicationRevision",power.publicationRevision},
   {"fanWakeGeneration",fan.value("fanWakeGeneration",0ULL)}}.dump()<<"\n";
 };
 const auto same=[&](const Power& power,const json& fan){
  return ::ymcc::sleeppower::powerAdmitted(power)&&fan.value("hardwareWritesAllowed",false)&&
   fan.value("generation",0ULL)==power.generation&&fan.value("suspendSerial",0ULL)==power.suspendSerial&&
   fan.value("observedNotificationSequence",0ULL)==power.observedNotificationSequence&&
   fan.value("publicationRevision",0ULL)==power.publicationRevision;
 };
 // A physical Fan wake is not a commit of shared input readiness.
 g_powerLifecycle=PowerLifecycle::Resuming;hardwareGate=false;
 auto noInput=captureFrame();
 check("Fan-only Ready cannot give an ordinary InputHost frame a scope",fanHostPowerMayWrite()&&
       !noInput.powerScope&&!inputHostFrameSharedPowerCurrent(noInput)&&noInput.reason==Verdict::PowerUnavailable);

 // Drive the one controlled reader. The actual Fan projection and InputHost
 // Scope sample this tuple independently; neither is given a copied Ready.
 g_powerLifecycle=PowerLifecycle::Ready;hardwareGate=true;
 ++sharedNotificationSequence;
 auto old=captureFrame();const auto first=sharedPowerLifecycleSnapshot();
 observe("first-ready",old.sourcePower,first);
 check("the same committed Ready tuple permits both actual consumers",old.powerScope&&same(old.sourcePower,first)&&
       first.value("fanWakeGeneration",0ULL)==fanGeneration&&fanHostPowerMayWrite()&&inputHostFrameSharedPowerCurrent(old));
 check("new input cancellation event starts unsignaled",old.powerScope&&
       ::WaitForSingleObject(old.powerScope->event(),0)==WAIT_TIMEOUT);

 sharedPowerPending=true;++sharedNotificationSequence;mirrors.revoke();
 auto pending=captureFrame();
 check("a later pending notification blocks both new authorizations",!fanHostPowerMayWrite()&&!pending.powerScope&&
       !inputHostFrameSharedPowerCurrent(pending)&&pending.reason==Verdict::PowerUnavailable);
 check("pending signals the exact old frame event",old.powerScope&&::WaitForSingleObject(old.powerScope->event(),0)==WAIT_OBJECT_0);

 // Keep the shared generation unchanged: only the suspend watermark and
 // notification identify this successor. Fan's own physical generation remains separate.
 sharedPowerPending=false;++sharedSuspendSerial;++sharedNotificationSequence;
 auto fresh=captureFrame();const auto successor=sharedPowerLifecycleSnapshot();
 observe("same-generation-successor",fresh.sourcePower,successor);
 check("same-generation successor gives a new input scope and current Fan permission",fresh.powerScope&&
       fresh.powerScope!=old.powerScope&&fresh.sourcePower.generation==old.sourcePower.generation&&
       fresh.sourcePower.suspendSerial==old.sourcePower.suspendSerial+1&&same(fresh.sourcePower,successor)&&
       fanHostPowerMayWrite()&&inputHostFrameSharedPowerCurrent(fresh));
 const bool oldFrameCurrent=inputHostFrameSharedPowerCurrent(old);
 const bool oldFrameAdmitted=oldFrameCurrent||control;
 check("late frame cannot reuse an old scope after both consumers see Ready",!oldFrameAdmitted);
 check("an old physical sample stays retired even with a fresh input scope",[&]{
  auto mixed=fresh;mixed.sourcePower=old.sourcePower;
  return !inputHostFrameSharedPowerCurrent(mixed)&&mixed.reason==Verdict::SourceRetired;
 }());

 const auto posts=projectedPowerEvents.size();
 active=false;++fanWakeIntentRevision;fireFanWakeTimer();
 check("explicit Fan OFF cancels retained Ready delivery without invalidating healthy InputHost",projectedPowerEvents.size()==posts&&
       !fanWakeTimer&&inputHostFrameSharedPowerCurrent(fresh));
 g_exitRequested=true;mirrors.close();
 auto afterExit=captureFrame();
 check("provider exit refuses Fan write and both fresh and retained InputHost scopes",!fanHostPowerMayWrite()&&
       !afterExit.powerScope&&!inputHostFrameSharedPowerCurrent(fresh)&&fresh.reason==Verdict::PowerUnavailable);
 check("exit signals the final owned InputHost event",fresh.powerScope&&
       ::WaitForSingleObject(fresh.powerScope->event(),0)==WAIT_OBJECT_0);
 ::std::weak_ptr<::ymcc::inputHost::sharedPower::Scope> oldLease=old.powerScope,finalLease=fresh.powerScope;
 old.powerScope.reset();fresh.powerScope.reset();cache.reset();
 check("all fixture-owned scopes, Fan workers and timers are returned",oldLease.expired()&&finalLease.expired()&&workers.empty()&&!fanWakeTimer);
}
