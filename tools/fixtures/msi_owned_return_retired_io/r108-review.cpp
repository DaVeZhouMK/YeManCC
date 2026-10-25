// Compile only frozen R108 runtime plus its offline selftest boundary copy.
#include <sstream>
#define main r108BaselineMain
#include "runtime_boundary_copy.cpp"
#undef main

static json outcomeJson(const MsiClawCommandOutcome& out) {
 const auto& w=out.write;
 return {{"state",msiClawIoStateName(w.state)},{"attempted",w.attempted},{"ok",w.ok},{"written",w.written},
  {"timedOut",w.timedOut},{"handleRetained",w.handleRetained},{"txSeq",w.txSeq},
  {"writeFileReturned",w.writeFileReturned},{"completionQueryAttempted",w.completionQueryAttempted},
  {"retryable",msiOwnedReturnRetryable(out)}};
}
static json stateJson() {
 return {{"slotPending",g_msiClawIoSlot.pending},{"inertOperationActive",inert::active},
  {"slotOwnsOldHandle",g_msiClawIoSlot.handle==inert::oldHandle},
  {"oldHandleLive",g_liveHandles.count(inert::oldHandle)==1},{"oldEventLive",inert::events.count(inert::oldEvent)==1},
  {"slotTxSeq",g_msiClawIoSlot.txSeq},{"actualWriteFileCalls",inert::writes},{"completionQueries",inert::queries},
  {"cancelCalls",inert::cancels},{"modeWriterInvocationsByRuntime",g_count.writes},
  {"retryUsed",g_msiOwnedReturnRecovery.retryUsed()},{"storedRetrySafe",g_msiOwnedReturnRetrySafe},
  {"pending",g_msiOwnedReturnRecovery.pending()},{"owed",g_msiDinputReturnOwed.load()},
  {"durableModePending",g_msiCrashRecord.modeReturnPending},{"debtDischarges",g_count.durableDischarges},
  {"observations",g_msiOwnedReturnRecovery.observations()},{"temporaryHandles",g_liveHandles.size()}};
}
static void cleanIo() {
 if(g_msiClawIoSlot.pending){inert::plan.aborted=true;std::lock_guard<std::mutex> lock(g_msiClawIoSlot.mutex);inert::require(msiClawIoSlotReapLocked(),"cleanup terminal reap failed");}
 if(g_msiClawIoSlot.event || g_msiClawIoSlot.handle!=INVALID_HANDLE_VALUE){std::lock_guard<std::mutex> lock(g_msiClawIoSlot.mutex);msiClawIoSlotFreeLocked();}
 inert::require(!inert::active && g_liveHandles.empty() && inert::events.empty(),"synthetic resource leak at fixture end");
}
static MsiClawCommandOutcome directCommand() {
 MsiClawHidDiag diag;const auto handle=msiClawOpenModeCommand(diag);
 return actualMsiClawSwitchModeOnHandle(1,handle,diag,false);
}
static MsiClawCommandOutcome retireOld(const std::string& route="timeout") {
 inert::plan={};inert::plan.pendingStart=route!="immediate";
 inert::plan.firstWait=route=="signaled"?WAIT_OBJECT_0:WAIT_TIMEOUT;
 auto out=directCommand();inert::require(out.write.handleRetained && g_msiClawIoSlot.pending && inert::active,"old command did not retire");return out;
}
struct Review {
 unsigned assertions=0;json cases=json::array(),failures=json::array();std::string current;
 void check(bool value,const char* why){++assertions;if(!value)failures.push_back({{"case",current},{"assertion",why}});}
 template<class F> void run(const std::string& name,F test){
  cleanIo();reset();inert::clear();g_executeActualWriter=true;g_lastActualOutcome={};current=name;
  const auto before=failures.size();const auto beforeAssertions=assertions;json detail=json::object();
  try{test(detail);}catch(const std::exception& e){check(false,e.what());}
  detail["finalBeforeCleanup"]=stateJson();json logs=json::array();for(const auto& row:g_logs)logs.push_back({{"domain",row.first},{"row",row.second}});
  try{cleanIo();check(g_liveHandles.empty()&&inert::events.empty()&&!inert::active,"all synthetic I/O resources settled and freed");}catch(const std::exception& e){check(false,e.what());}
  cases.push_back({{"name",name},{"passed",before==failures.size()},{"assertions",assertions-beforeAssertions},{"detail",detail},{"logs",logs}});
 }
};
int main(){
 try{
  // Re-run all original R108 cases against the same boundary copy, with the
  // real-writer branch off. Their assertions were not edited or replaced.
  std::ostringstream capture;auto* old=std::cout.rdbuf(capture.rdbuf());
  const int baselineStatus=r108BaselineMain();std::cout.rdbuf(old);
  const auto baseline=json::parse(capture.str());Review c;
  for(const std::string route:{"timeout","signaled","immediate"})c.run("classifier/busy-after-"+route+"-retirement",[&](json& d){
   const auto oldOutcome=retireOld(route);const auto oldHandle=inert::oldHandle;const auto oldEvent=inert::oldEvent;
   const auto tx=g_msiClawIoSlot.txSeq;const auto buffer=g_msiClawIoSlot.buffer;const auto count=inert::writes;
   const auto busy=directCommand();d["oldOutcome"]=outcomeJson(oldOutcome);d["busyOutcome"]=outcomeJson(busy);d["busyState"]=stateJson();
   c.check(!msiOwnedReturnRetryable(oldOutcome),"the old retained outcome itself is excluded by classifier");
   c.check(busy.write.state==MsiClawIoState::SlotBusyRetired && !busy.write.attempted && !busy.write.handleRetained && !busy.write.timedOut,"real bounded writer yields current unattempted non-retained busy result");
   c.check(msiOwnedReturnRetryable(busy),"BUG: real R108 classifier permits busy result while old I/O is incomplete");
   c.check(inert::writes==count && !busy.write.writeFileReturned && !busy.write.completionQueryAttempted,"busy call issued no WriteFile or completion query for the current request");
   c.check(g_msiClawIoSlot.pending && inert::active && g_msiClawIoSlot.handle==oldHandle && g_msiClawIoSlot.event==oldEvent && g_msiClawIoSlot.buffer==buffer && busy.write.txSeq==tx,"old I/O resources remain live and owned; busy txSeq belongs to old request");
  });
  c.run("runtime/busy-initial-busy-at-claim-consumes-only-retry",[&](json& d){
   retireOld();const auto busy=directCommand();initial(busy);d["initialOutcome"]=outcomeJson(busy);d["initial"]=stateJson();
   c.check(g_msiOwnedReturnRecovery.pending() && g_msiOwnedReturnRetrySafe && !g_msiOwnedReturnRecovery.retryUsed(),"real initial record accepts busy as retry-safe without consuming yet");
   const auto writes=inert::writes;maintain(11500);d["afterClaim"]=stateJson();d["retryOutcome"]=outcomeJson(g_lastActualOutcome);
   c.check(g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==1,"BUG: real maintenance consumes the unique retry and invokes real mode writer");
   c.check(g_lastActualOutcome.write.state==MsiClawIoState::SlotBusyRetired && !g_lastActualOutcome.write.attempted && inert::writes==writes,"BUG: retry is consumed with zero additional WriteFile calls");
   c.check(g_msiClawIoSlot.pending && inert::active && g_liveHandles.count(inert::oldHandle)==1,"old retired request is still incomplete after retry consumption");
   c.check(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending && g_count.durableDischarges==0,"no physical proof or debt discharge is fabricated");
   maintain(12500);d["stillBusyLater"]=stateJson();c.check(g_count.writes==1 && inert::writes==writes,"same episode offers no second retry while old slot remains busy");
   inert::plan.completed=true;{std::lock_guard<std::mutex> lock(g_msiClawIoSlot.mutex);c.check(msiClawIoSlotReapLocked(),"test later delivers terminal completion to real reaper");}
   ++g_xinputTopologyEpoch;maintain(13500);d["afterLateReapAndExistingTopologyEdge"]=stateJson();
   c.check(!g_msiClawIoSlot.pending && !inert::active && g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==1 && inert::writes==writes,"retry remains spent after eventual old completion and existing topology edge");
  });
  c.run("runtime/safe-initial-then-slot-retires-before-claim",[&](json& d){
   inert::plan.pendingStart=false;inert::plan.completed=true;const auto success=directCommand();initial(success);
   c.check(success.write.ok && g_msiOwnedReturnRetrySafe,"real completed initial command is eligible");
   const auto retired=retireOld();d["interveningRetired"]=outcomeJson(retired);const auto writes=inert::writes;maintain(11500);d["afterClaim"]=stateJson();d["retryOutcome"]=outcomeJson(g_lastActualOutcome);
   c.check(g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==1 && inert::writes==writes && inert::active,"BUG: stored initial safety is not a current old-slot completion check");
   c.check(g_lastActualOutcome.write.state==MsiClawIoState::SlotBusyRetired,"classifier-only tightening cannot cover a slot that retires after initial classification");
  });
  c.run("control/retained-initial-blocks-retry",[&](json& d){
   const auto retained=retireOld();initial(retained);maintain(11500);d["initialOutcome"]=outcomeJson(retained);
   c.check(!g_msiOwnedReturnRetrySafe && !g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==0,"retained initial I/O correctly blocks retry");
   c.check(inert::writes==1 && inert::active,"blocked retry leaves old request untouched");
  });
  c.run("control/timed-out-initial-blocks-retry-even-without-handle",[&](json& d){
   auto out=blocked();out.write.timedOut=true;out.write.state=MsiClawIoState::TimeoutCancelledReaped;initial(out);maintain(11500);d["initialOutcome"]=outcomeJson(out);
   c.check(!g_msiOwnedReturnRetrySafe && !g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==0,"timedOut exclusion is enforced independently");
  });
  c.run("control/busy-initial-old-terminal-before-retry",[&](json& d){
   retireOld();const auto busy=directCommand();initial(busy);const auto writes=inert::writes;
   inert::plan.completed=true;inert::plan.pendingStart=false;maintain(11500);d["afterClaim"]=stateJson();d["retryOutcome"]=outcomeJson(g_lastActualOutcome);
   c.check(g_msiOwnedReturnRecovery.retryUsed() && g_lastActualOutcome.write.ok && inert::writes==writes+1 && !inert::active && !g_msiClawIoSlot.pending,"terminal old completion allows actual bounded writer to reap and issue one real inert retry");
   c.check(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending,"successful retry alone does not discharge debt");
  });
  c.run("control/busy-initial-before-delay-does-not-consume",[&](json& d){
   retireOld();initial(directCommand());maintain(11000);d["beforeRetryDelay"]=stateJson();
   c.check(!g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==0 && inert::writes==1,"1500ms retry delay is a real gate");
  });
  c.run("control/busy-initial-wrong-owned-root-does-not-consume",[&](json& d){
   retireOld();initial(directCommand());g_parents[kCommandPath]={L"USB\\VID_0DB0&PID_1902\\7&OTHER&0&8",{"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(8)"}};maintain(11500);d["wrongRoot"]=stateJson();
   c.check(!g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==0 && inert::active,"live matching DInput root is still required");
  });
  c.run("control/busy-initial-hardware-gate-closed-does-not-consume",[&](json& d){
   retireOld();initial(directCommand());g_hardwareGate=false;maintain(11500);d["hardwareGateClosed"]=stateJson();
   c.check(!g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==0 && inert::active,"existing hardware gate is retained");
  });
  // These are offline proposals only: use the REAL reaper immediately BEFORE
  // dispatching the unmodified runtime. No production guard is patched here.
  c.run("proposal/terminal-reap-gate-preserves-retry-while-incomplete",[&](json& d){
   retireOld();initial(directCommand());bool settled=false;{std::lock_guard<std::mutex> lock(g_msiClawIoSlot.mutex);settled=msiClawIoSlotReapLocked();}
   if(settled)maintain(11500);d["gateReturned"]=settled;d["afterGate"]=stateJson();
   c.check(!settled && inert::active && g_msiClawIoSlot.pending && !g_msiOwnedReturnRecovery.retryUsed() && g_count.writes==0,"proposed pre-consumption gate rejects ERROR_IO_INCOMPLETE and preserves unique retry");
   inert::plan.completed=true;inert::plan.pendingStart=false;{std::lock_guard<std::mutex> lock(g_msiClawIoSlot.mutex);settled=msiClawIoSlotReapLocked();}
   if(settled)maintain(11500);d["afterTerminalReapAndDispatch"]=stateJson();
   c.check(settled && g_msiOwnedReturnRecovery.retryUsed() && g_lastActualOutcome.write.ok && inert::writes==2,"once terminal old completion is actually reaped the preserved retry can issue once");
  });
  c.run("proposal/pending-only-gate-can-defer-an-already-terminal-slot",[&](json& d){
   retireOld();initial(directCommand());inert::plan.completed=true;d["pendingFlagBeforeQuery"]=g_msiClawIoSlot.pending;
   c.check(g_msiClawIoSlot.pending && inert::active,"pending alone is not proof that a terminal completion is unavailable");
   bool settled=false;{std::lock_guard<std::mutex> lock(g_msiClawIoSlot.mutex);settled=msiClawIoSlotReapLocked();}
   d["opportunisticReapReturned"]=settled;c.check(settled && !g_msiClawIoSlot.pending && !inert::active && !g_msiOwnedReturnRecovery.retryUsed(),"nonblocking real reaper distinguishes terminal old completion without consuming retry");
  });
  const bool passed=baselineStatus==0 && baseline.value("passed",false) && c.failures.empty();
  const json report={{"passed",passed},{"verdict",passed?"DETERMINISTIC_BUG_REPRODUCED":"VALIDATION_INCOMPLETE"},
   {"baseline",baseline},{"assertions",c.assertions},{"failures",c.failures},{"cases",c.cases},
   {"scope","frozen R108; real runtime/classifier/recovery/bounded-writer/reaper/modewriter; inert OS/device/durable seams"},
   {"gateProposalIsExternalExperimentOnly",true},{"noDeviceIo",true},{"newR109ResumeBranchCompiled",false}};
  std::cout<<report.dump(2)<<'\n';return passed?0:1;
 }catch(const std::exception& e){std::cout<<json({{"passed",false},{"fatal",e.what()}}).dump(2)<<'\n';return 2;}
}