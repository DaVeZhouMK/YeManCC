#include <future>
#include <thread>
#include "review_helpers.h"

int main() {
 try {
  // Bind unrelated definitions without invoking their lifecycle callbacks.
  (void)&msiOwnedReturnMode2Outcome;
  (void)&msiOwnedReturnObserveOffIntent;
  Review c;
  for(const std::string route:{"timeout","signaled","immediate"})c.run("fixed/busy-"+route+"-retirement-keeps-budget-and-observation",[&](json& d){
   const auto old=retireOld(route);const auto busy=directCommand();initial(busy);
   const auto opens=g_count.opens,writes=inert::writes,waits=inert::waits,cancels=inert::cancels;
   const auto queries=inert::queries;const auto oldHandle=inert::oldHandle,oldEvent=inert::oldEvent;const auto buffer=g_msiClawIoSlot.buffer;
   maintain(11500);d["busyOutcome"]=outcomeJson(busy);d["afterGate"]=stateJson();
   c.check(msiOwnedReturnRetryable(busy) && g_msiOwnedReturnRetrySafe,"busy is a deferrable initial outcome, not a permanent permission to write");
   c.check(!g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==0 && inert::writes==writes,"actual CommandReady blocks claim and all additional writes");
   c.check(g_count.opens==opens && inert::queries==queries+1 && inert::waits==waits && inert::cancels==cancels,"real nonblocking reaper runs before command-open; no new wait or cancel");
   c.check(g_msiClawIoSlot.pending && inert::active && g_msiClawIoSlot.handle==oldHandle && g_msiClawIoSlot.event==oldEvent && g_msiClawIoSlot.buffer==buffer,"busy gate preserves old handle/event/buffer ownership");
   c.check(g_msiOwnedReturnRecovery.pending() && g_msiOwnedReturnRecovery.working() && g_msiOwnedReturnRecovery.observations()==1,"observation duty remains active while retry is deferred");
   maintain(12500);maintain(13500);d["afterRepeatedBusy"]=stateJson();
   c.check(!g_msiOwnedReturnRecovery.retryUsed() && g_count.opens==opens && g_count.writes==0 && g_msiOwnedReturnRecovery.observations()==3,"repeated due windows observe without consuming retry or opening handles");
   d["oldOutcome"]=outcomeJson(old);
  });
  for(const std::string terminal:{"success","aborted","zero-byte-success"})c.run("fixed/late-"+terminal+"-reaps-then-one-actual-retry",[&](json& d){
   retireOld();initial(directCommand());const auto episode=g_msiOwnedReturnRecovery.episode();maintain(11500);
   c.check(!g_msiOwnedReturnRecovery.retryUsed() && inert::writes==1,"busy interval preserves retry");
   inert::plan.completed=terminal!="aborted";inert::plan.aborted=terminal=="aborted";inert::terminalZero=terminal=="zero-byte-success";inert::nextWriteImmediateSuccess=true;
   const auto queries=inert::queries;maintain(12500);d["afterTerminal"]=stateJson();d["retryOutcome"]=outcomeJson(g_lastActualOutcome);
   c.check(g_msiOwnedReturnRecovery.episode()==episode && g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==1 && inert::writes==2,"same episode consumes one retry only after terminal old reap and issues a new WriteFile");
   c.check(g_lastActualOutcome.write.ok && g_lastActualOutcome.write.attempted && !g_msiClawIoSlot.pending && !inert::active && inert::queries==queries+2,"real ready reaps old request; real new write obtains independent completion");
   c.check(g_liveHandles.empty() && g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending && g_count.durableDischarges==0,"old resources are freed but write receipt alone leaves debt pending");
   maintain(13500);++g_xinputTopologyEpoch;maintain(14500);d["afterLaterWindows"]=stateJson();
   c.check(inert::writes==2 && g_count.writes==1 && g_msiOwnedReturnRecovery.retryUsed(),"further windows/topology do not refund or repeat the consumed retry");
  });
  c.run("fixed/completed-initial-intervening-retired-slot-also-gated",[&](json& d){
   inert::plan.pendingStart=false;inert::plan.completed=true;initial(directCommand());retireOld();const auto opens=g_count.opens;
   maintain(11500);d["afterGate"]=stateJson();
   c.check(g_msiOwnedReturnRetrySafe && !g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==0 && inert::writes==2 && g_count.opens==opens,"current slot state is checked even when stored initial result was successful");
   inert::plan.completed=true;inert::nextWriteImmediateSuccess=true;maintain(12500);
   c.check(g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==1 && inert::writes==3 && g_lastActualOutcome.write.ok,"later terminal proof releases the originally eligible retry once");
  });
  c.run("fixed/try-lock-contention-keeps-budget-and-does-not-query",[&](json& d){
   retireOld();initial(directCommand());const auto queries=inert::queries,opens=g_count.opens;
   std::promise<void> held,release;auto released=release.get_future();
   std::thread holder([&]{std::lock_guard<std::mutex> lock(g_msiClawIoSlot.mutex);held.set_value();released.wait();});
   held.get_future().wait();const bool ready=msiOwnedReturnCommandReady();maintain(11500);release.set_value();holder.join();
   d["directReadyDuringContention"]=ready;d["afterContendedMaintenance"]=stateJson();
   c.check(!ready && inert::queries==queries && g_count.opens==opens,"actual try_lock rejects contention without touching old I/O");
   c.check(!g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==0 && g_msiOwnedReturnRecovery.pending(),"contended slot retains observation and unique retry");
  });
  c.run("fixed/ready-empty-slot-is-nonblocking-and-query-free",[&](json& d){
   const bool ready=msiOwnedReturnCommandReady();d["ready"]=ready;
   c.check(ready && inert::queries==0 && inert::waits==0 && inert::cancels==0 && inert::writes==0,"empty slot is immediately ready without device I/O");
  });
  for(const std::string kind:{"unknown-unattempted","retained","timeout","partial","unknown-start-error"})c.run("fixed/ambiguous-initial-"+kind+"-does-not-retry",[&](json& d){
   retireOld();auto out=blocked();
   if(kind=="unknown-unattempted")out.write.state=MsiClawIoState::IoDesignPending;
   if(kind=="retained")out.write.handleRetained=true;
   if(kind=="timeout"){out.write.timedOut=true;out.write.state=MsiClawIoState::TimeoutCancelledReaped;}
   if(kind=="partial"){out.write.attempted=true;out.write.written=17;out.write.state=MsiClawIoState::ShortWrite;}
   if(kind=="unknown-start-error"){out.write.attempted=true;out.write.state=MsiClawIoState::StartFailed;out.write.lastError=12345;}
   initial(out);const auto queries=inert::queries,opens=g_count.opens;maintain(11500);d["initialOutcome"]=outcomeJson(out);d["afterMaintenance"]=stateJson();
   c.check(!msiOwnedReturnRetryable(out) && !g_msiOwnedReturnRetrySafe && !g_msiOwnedReturnRecovery.retryUsed(),"ambiguous result never grants retry permission");
   c.check(g_count.writes==0 && g_count.opens==opens && inert::queries==queries && inert::active,"ineligible outcome does not probe slot or open/write commands");
   c.check(g_msiOwnedReturnRecovery.pending() && g_msiDinputReturnOwed && g_count.durableDischarges==0,"ineligibility retains observation and debt");
  });
  c.run("fixed/fresh-physical-windows-settle-despite-busy-slot",[&](json& d){
   retireOld();initial(directCommand());const auto queries=inert::queries,opens=g_count.opens;physical();sample(1,11500);maintain(11500);
   c.check(g_msiOwnedReturnRecovery.pending() && g_count.durableDischarges==0,"first fresh exact-owned sample observes without discharging");
   sample(2,12500);maintain(12500);d["afterQualifiedPhysicalReturn"]=stateJson();
   c.check(g_msiOwnedReturnRecovery.completed() && !g_msiOwnedReturnPending && !g_msiDinputReturnOwed && !g_msiCrashRecord.modeReturnPending && g_count.durableDischarges==1,"second independent fresh same-owned sample uses real discharge helper");
   c.check(inert::active && g_msiClawIoSlot.pending && inert::queries==queries && g_count.opens==opens && !g_msiOwnedReturnRecovery.retryUsed(),"physical observation settles before retry gate and leaves old I/O ownership intact");
   c.check(inert::writes==1 && g_count.writes==0,"no mode command is needed to settle qualified physical evidence");
  });
  c.run("fixed/ambiguous-initial-still-observes-qualified-physical-return",[&](json& d){
   auto out=blocked();out.write.state=MsiClawIoState::IoDesignPending;initial(out);physical();sample(1,11500);maintain(11500);sample(2,12500);maintain(12500);d["afterPhysicalReturn"]=stateJson();
   c.check(!g_msiOwnedReturnRetrySafe && !g_msiOwnedReturnRecovery.retryUsed() && g_msiOwnedReturnRecovery.completed() && g_count.durableDischarges==1,"unknown I/O disallows retry but does not prevent fresh physical evidence settling debt");
   c.check(inert::queries==0 && inert::writes==0 && g_count.opens==0,"observation-only settlement never probes a command slot");
  });
  c.run("fixed/rog-context-does-not-touch-live-slot",[&](json& d){
   retireOld();initial(directCommand());const auto queries=inert::queries,opens=g_count.opens,writes=inert::writes;g_board="rog";maintain(11500);d["afterRogContext"]=stateJson();
   c.check(inert::queries==queries && g_count.opens==opens && inert::writes==writes && g_count.writes==0,"ROG context rejects in Context before touching MSI slot");
   c.check(inert::active && g_msiClawIoSlot.pending && !g_msiOwnedReturnRecovery.retryUsed(),"ROG path preserves old resources and retry budget");
  });
  c.run("fixed/rog-cold-entry-does-not-start-observation-or-touch-slot",[&](json& d){
   g_board="rog";initial();maintain(11500);d["coldRog"]=stateJson();
   c.check(!g_msiOwnedReturnRecovery.pending() && inert::queries==0 && inert::writes==0 && g_count.opens==0,"ROG cold entry has no MSI observation or slot activity");
  });
  c.run("fixed/busy-gate-does-not-create-observation-window-renewals",[&](json& d){
   retireOld();initial(directCommand());for(unsigned i=0;i<20;i++)maintain(11500+i*1000);d["afterWindowExhaustion"]=stateJson();
   const auto queries=inert::queries;maintain(100000);d["afterElapsedTimeOnly"]=stateJson();
   c.check(g_msiOwnedReturnRecovery.pending() && g_msiOwnedReturnRecovery.failed() && !g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==0,"20 windows exhaust without losing debt or consuming retry");
   c.check(inert::queries==queries && g_msiOwnedReturnRecovery.observations()==20,"elapsed time alone does not renew window or poll slot");
   inert::plan.completed=true;inert::nextWriteImmediateSuccess=true;++g_xinputTopologyEpoch;maintain(101000);d["afterExistingTopologyEdge"]=stateJson();
   c.check(g_count.writes==1 && inert::writes==2 && g_msiOwnedReturnRecovery.retryUsed(),"existing topology edge can reap terminal old I/O and use still-preserved unique retry");
  });
  c.run("context-guard/already-invalid-before-durable-entry",[&](json& d){
   MsiClawHidDiag diag;const auto handle=msiClawOpenModeCommand(diag);
   const auto out=actualMsiClawSwitchModeOnHandle(1,handle,diag,true,[]{return false;});d["outcome"]=outcomeJson(out);
   c.check(!out.write.attempted && !out.write.handleRetained && inert::durableGuards==0 && inert::writes==0,"invalid original context is rejected before durable entry and transport");
   c.check(g_liveHandles.empty() && !g_msiClawIoSlot.pending,"entry rejection closes temporary command handle");
  });
  c.run("context-guard/context-changes-while-waiting-for-slot-mutex",[&](json& d){
   MsiClawHidDiag diag;const auto handle=msiClawOpenModeCommand(diag);
   std::atomic<bool> allowed{true};std::atomic<unsigned> checks{0};std::promise<void> entered;
   auto first=entered.get_future();std::unique_lock<std::mutex> held(g_msiClawIoSlot.mutex);
   auto operation=std::async(std::launch::async,[&]{return actualMsiClawSwitchModeOnHandle(1,handle,diag,true,[&]{if(checks.fetch_add(1)==0){entered.set_value();return true;}return allowed.load();});});
   const bool sawEntry=first.wait_for(std::chrono::seconds(2))==std::future_status::ready;
   allowed=false;held.unlock();const auto out=operation.get();d["outcome"]=outcomeJson(out);d["guardChecks"]=checks.load();
   c.check(sawEntry && checks>=2,"original context is checked before entry and after waiting for actual slot mutex");
   c.check(!out.write.attempted && inert::writes==0 && !g_msiClawIoSlot.pending,"changed context never enters WriteFile or leaves a pending request");
  });
  c.run("context-guard/context-changes-during-event-preparation",[&](json& d){
   MsiClawHidDiag diag;const auto handle=msiClawOpenModeCommand(diag);bool allowed=true;
   inert::resetEventHook=[&]{allowed=false;};
   const auto out=actualMsiClawSwitchModeOnHandle(1,handle,diag,true,[&]{return allowed;});d["outcome"]=outcomeJson(out);
   c.check(!allowed && inert::creates==1,"event preparation crossed an explicit context-change boundary");
   c.check(!out.write.attempted && inert::writes==0 && !g_msiClawIoSlot.pending && g_msiClawIoSlot.handle==INVALID_HANDLE_VALUE,"last pre-WriteFile guard rejects without assigning live handle ownership");
   c.check(g_liveHandles.empty(),"post-event rejection closes command handle while reusable event remains owned by slot");
  });
  const json report={{"passed",c.failures.empty()},{"verdict",c.failures.empty()?"CURRENT_READY_GATE_VALIDATED":"VALIDATION_FAILED"},
   {"assertions",c.assertions},{"failures",c.failures},{"cases",c.cases},{"noDeviceIo",true},
   {"realCommandReady",true},{"noNewMode2OrObserveOffCalls",true},
   {"scope","actual current runtime/CommandReady/writer/reaper/slot; original R108 offline OS seams; no current parent suite changes"}};
  std::cout<<report.dump(2)<<'\n';return c.failures.empty()?0:1;
 }catch(const std::exception& e){std::cout<<json({{"passed",false},{"fatal",e.what()}}).dump(2)<<'\n';return 2;}
}