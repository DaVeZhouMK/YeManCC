// Included AFTER the complete instrumented engine.cpp, in this single TU.
namespace ymcc::steamOriginalRecovery {
namespace {
struct FixtureSignal final:ymcc::steamcommand::AvailabilitySignal {
  H event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
  void notify() noexcept override {SetEvent(event.h);}
};
Json driverCleanupCase(driver_cleanup_fixture::Boundary boundary,const char* name) {
  using namespace driver_cleanup_fixture;
  Context c(boundary);
  DWORD fixtureBaselineBefore=0,fixtureBaselineAfter=0;
  need(GetProcessHandleCount(GetCurrentProcess(),&fixtureBaselineBefore)!=FALSE,"fixture-overall-baseline-before");
  H target{CreateEventW(nullptr,TRUE,FALSE,nullptr)},entered{CreateEventW(nullptr,TRUE,FALSE,nullptr)},
    fault{CreateEventW(nullptr,TRUE,FALSE,nullptr)},hold{CreateEventW(nullptr,TRUE,FALSE,nullptr)},
    done{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
  need(target.good()&&entered.good()&&fault.good()&&hold.good()&&done.good(),"fixture-event-allocation");
  c.target=target.h;c.entered=entered.h;c.fault=fault.h;c.hold=hold.h;c.done=done.h;
  ymcc::steamcommand::Coordinator coordinator;
  const ymcc::steamcommand::Target commandTarget{GetCurrentProcessId(),1,L"fixture-owned-events-only"};
  auto signal=std::make_shared<FixtureSignal>();need(signal->event.good(),"fixture-hint-event");
  auto intent=coordinator.prepareIntent(commandTarget,signal);need(bool(intent),"fixture-real-intent");
  need(intent->promote()==ymcc::steamcommand::IntentStatus::Ready,"fixture-promote");
  DWORD before=0,after=0;need(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"fixture-handles-before");
  driver_cleanup_fixture::active.store(&c,std::memory_order_release);
  std::atomic<bool> setupOk{false},workerFailed{false};
  std::thread worker([&]{
    try {
      gCurrent=[]{return true;};gOriginalSteam=[&]{return ::WaitForSingleObject(c.target,0)==WAIT_TIMEOUT;};
      gCleanupMode=false;gCancelSignal=0;gIdentityCancelSignal=0;
      {
        // Same nesting as production: exact Intent/Ticket -> Use -> Dispatch
        // -> real QueryActor -> complete driver. No runTrial port or catch clone.
        const auto ticket=intent->ticket();ymcc::steamcommand::Use use(ticket);
        ymcc::steamcommand::Dispatch dispatch(ymcc::steamcommand::Effect::Mutation);
        need(bool(dispatch),"fixture-real-dispatch");
        J querySummary;QueryActor query(1,L"fixture-never-loaded.dll",RecoveryRoute::SemiStatic,"",false,&querySummary);
        query.waitReady();setupOk=true;
        try{(void)runTrial(J::object(),query);}
        catch(const NonStdCleanupUnwind& error){c.escaped=true;c.escapedNonStd=1;c.escapedNonStdTag=error.tag;}
        catch(...){c.escaped=true;}
        c.driverReturned=true;query.finish();dispatch.complete(false);
      }
      intent->cancel();gCurrent={};gOriginalSteam={};
    } catch(...) {failSite=0;workerFailed=true;intent->cancel();}
    SetEvent(c.done);
  });
  const bool began=::WaitForSingleObject(entered.h,3000)==WAIT_OBJECT_0;
  const bool faultSeen=::WaitForSingleObject(fault.h,3000)==WAIT_OBJECT_0;
  const bool mustHold=boundary==Boundary::CleanupDiagnostic||boundary==Boundary::Management||boundary==Boundary::Unknown||boundary==Boundary::WaitFailed||boundary==Boundary::LastGuard;
  bool heldBeforeRelease=false,timelyBeforeRelease=false,busyBeforeRelease=false,queryAlive=false;
  if(mustHold){
    const HANDLE gates[]{hold.h,done.h};
    const bool enteredHold=::WaitForMultipleObjects(2,gates,FALSE,1500)==WAIT_OBJECT_0;
    heldBeforeRelease=enteredHold&&::WaitForSingleObject(done.h,150)==WAIT_TIMEOUT&&!c.driverReturned.load();
  }else timelyBeforeRelease=::WaitForSingleObject(done.h,1500)==WAIT_OBJECT_0;
  queryAlive=c.queryStarted.load()==1&&c.queryStopped.load()==0;
  auto competitor=coordinator.reserve(commandTarget,ymcc::steamcommand::Plan::Single);
  busyBeforeRelease=!bool(competitor);if(competitor)competitor.parentReturned();
  // Freeze the old timely/busy verdict before releasing any fixture object.
  // This release is teardown only and never converts that earlier FAIL to PASS.
  const unsigned closesBeforeRelease=c.targetCloses.load();
  const bool driverReturnedBeforeRelease=c.driverReturned.load();
  const bool nonStdEscapedBeforeRelease=c.escapedNonStd.load()!=0;
  const bool released=SetEvent(target.h)!=FALSE;
  const bool joinedReady=::WaitForSingleObject(done.h,5000)==WAIT_OBJECT_0;
  if(!joinedReady){
    std::fputs("{\"ok\":false,\"status\":\"FIXTURE_JOIN_UNKNOWN\",\"sourceReady\":false}\n",stdout);std::fflush(stdout);
    // The owned producer must retain this abnormal outcome and Job receipt.
    ExitProcess(72);
  }
  worker.join();driver_cleanup_fixture::active.store(nullptr,std::memory_order_release);
  need(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE,"fixture-handles-after");
  auto finalTicket=coordinator.reserve(commandTarget,ymcc::steamcommand::Plan::Single);
  const bool returned=bool(finalTicket);if(finalTicket)finalTicket.parentReturned();
  const unsigned fixtureEventsOpened=6;
  unsigned fixtureCloseAttempts=0,fixtureEventsClosed=0;
  DWORD fixtureCloseErrors[6]{};
  const auto closeFixtureEvent=[&](H& handle) noexcept {
    const unsigned index=fixtureCloseAttempts++;
    if(!handle.good()){fixtureCloseErrors[index]=ERROR_INVALID_HANDLE;return;}
    if(CloseHandle(handle.h)!=FALSE){handle.h=nullptr;++fixtureEventsClosed;}
    else fixtureCloseErrors[index]=GetLastError(); // Retain refused HANDLE.
  };
  closeFixtureEvent(target);closeFixtureEvent(entered);closeFixtureEvent(fault);
  closeFixtureEvent(hold);closeFixtureEvent(done);closeFixtureEvent(signal->event);
  const bool fixtureBaselineKnown=GetProcessHandleCount(GetCurrentProcess(),&fixtureBaselineAfter)!=FALSE;
  const bool attachedCase=boundary==Boundary::FirstDiagnostic||boundary==Boundary::DetachDiagnostic;
  const bool disarmCase=boundary==Boundary::CleanupDisarm||boundary==Boundary::NormalDisarm;
  bool ok=began&&faultSeen&&released&&setupOk&&!workerFailed&&returned&&before==after&&c.faultCount==1&&
    c.queryStarted==1&&c.queryStopped==1&&c.targetCloses==1&&c.unexpected==0&&
    c.sdkQualifications==1&&c.sdkModules==0&&c.sdkUsers==0&&c.sdkPipes==0&&c.sdkClients==0&&
    before==fixtureBaselineBefore+fixtureEventsOpened&&fixtureCloseAttempts==6&&fixtureEventsClosed==6&&
    fixtureBaselineKnown&&fixtureBaselineBefore==fixtureBaselineAfter;
  if(mustHold)ok=ok&&heldBeforeRelease&&busyBeforeRelease&&queryAlive&&closesBeforeRelease==0;
  else ok=ok&&timelyBeforeRelease&&!busyBeforeRelease;
  if(attachedCase)ok=ok&&c.detach==1&&c.confirm==1;
  if(disarmCase)ok=ok&&c.control==1&&c.write==1&&c.read==1;
  if(boundary==Boundary::Retained)ok=ok&&c.control==0&&c.freed==1;
  if(boundary==Boundary::WaitFailed)ok=ok&&c.waitFailures.load()>0;
  if(boundary==Boundary::LastGuard)ok=ok&&c.guardEntries==1&&c.nonStdThrows==1&&
    c.detach==1&&c.confirm==0&&c.escaped&&c.escapedNonStd==1&&c.escapedNonStdTag==nonStdCleanupTag&&
    !driverReturnedBeforeRelease&&!nonStdEscapedBeforeRelease&&c.faults[0].site==1&&
    c.guardThread.load()!=0&&c.guardThread==c.detachFaultThread&&c.guardThread==c.faults[0].tid&&
    c.guardSequence>c.detachFaultSequence&&c.detachFaultSequence>c.faults[0].sequence;
  Json faults=Json::array();
  for(unsigned i=0;i<c.faultCount;++i){const auto& f=c.faults[i];Json stack=Json::array();for(USHORT n=0;n<f.frames;++n)stack.push_back(reinterpret_cast<std::uintptr_t>(f.stack[n]));faults.push_back({{"site",f.site},{"bytes",f.bytes},{"threadId",f.tid},{"sequence",f.sequence},{"returnAddresses",stack}});}
  return {{"name","driver-cleanup-diagnostic-unwind"},{"boundary",name},{"ok",ok},
    {"mustHold",mustHold},{"heldBeforeRelease",heldBeforeRelease},{"timelyBeforeRelease",timelyBeforeRelease},
    {"busyBeforeRelease",busyBeforeRelease},{"queryAliveBeforeRelease",queryAlive},{"targetClosesBeforeRelease",closesBeforeRelease},
    {"targetCloses",c.targetCloses.load()},{"managementCloses",c.managementCloses.load()},
    {"queryStarted",c.queryStarted.load()},{"queryStopped",c.queryStopped.load()},
    {"sdkQualifications",c.sdkQualifications.load()},{"sdkModuleDebt",c.sdkModules.load()},
    {"sdkUserDebt",c.sdkUsers.load()},{"sdkPipeDebt",c.sdkPipes.load()},{"sdkClientDebt",c.sdkClients.load()},
    {"detach",c.detach.load()},{"confirm",c.confirm.load()},{"control",c.control.load()},
    {"read",c.read.load()},{"write",c.write.load()},{"freed",c.freed.load()},
    {"guardEntries",c.guardEntries.load()},{"guardThread",c.guardThread.load()},
    {"guardSequence",c.guardSequence.load()},{"detachFaultSequence",c.detachFaultSequence.load()},
    {"detachFaultThread",c.detachFaultThread.load()},{"nonStdThrows",c.nonStdThrows.load()},
    {"escapedNonStd",c.escapedNonStd.load()},{"escapedNonStdTag",c.escapedNonStdTag.load()},
    {"driverReturnedBeforeRelease",driverReturnedBeforeRelease},{"nonStdEscapedBeforeRelease",nonStdEscapedBeforeRelease},
    {"ownerEntries",c.ownerEntries.load()},{"waitFailureObservations",c.waitFailures.load()},
    {"unexpectedApi",c.unexpected.load()},{"escaped",c.escaped.load()},{"handlesBefore",before},{"handlesAfter",after},
    {"returnedAfterRelease",returned},{"faults",faults},{"allocationImageBase",reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr))},{"oldVerdictFrozenBeforeRelease",true},
    {"fixtureEventsOpened",fixtureEventsOpened},{"fixtureCloseAttempts",fixtureCloseAttempts},
    {"fixtureEventsExplicitlyClosed",fixtureEventsClosed},
    {"fixtureCloseErrors",Json::array({fixtureCloseErrors[0],fixtureCloseErrors[1],fixtureCloseErrors[2],fixtureCloseErrors[3],fixtureCloseErrors[4],fixtureCloseErrors[5]})},
    {"fixtureBaselineBefore",fixtureBaselineBefore},{"fixtureBaselineAfter",fixtureBaselineAfter},
    {"fixtureBaselineKnown",fixtureBaselineKnown},{"driverQueryHandleReturnOnly",before==after},
    {"fixtureCloseRefusalRetained",fixtureEventsClosed!=6}};
}
}
}
int main(int argc,char** argv){
  using B=driver_cleanup_fixture::Boundary;
  struct Selection{const char* name;B boundary;};
  const Selection selections[]{
    {"first-diagnostic-attached",B::FirstDiagnostic},{"cleanup-diagnostic-attached",B::CleanupDiagnostic},
    {"management-handle-debt",B::Management},{"unknown-control-debt",B::Unknown},
    {"cleanup-disarm-confirmed-diagnostic",B::CleanupDisarm},{"detach-confirmed-diagnostic",B::DetachDiagnostic},
    {"idle-diagnostic-no-debt",B::Idle},{"retained-image-no-active-debt",B::Retained},
    {"normal-disarm-confirmed-diagnostic",B::NormalDisarm},{"wait-failed-is-not-exit",B::WaitFailed},
    {"cleanup-nonstd-unwind-last-guard",B::LastGuard}};
  if(argc!=7||std::strcmp(argv[1],"--scope")||std::strcmp(argv[2],"publication-revocation")||
     std::strcmp(argv[3],"--case")||std::strcmp(argv[4],"driver-cleanup-diagnostic-unwind")||
     std::strcmp(argv[5],"--driver-cleanup-boundary"))return 64;
  const Selection* selected=nullptr;for(const auto& s:selections)if(!std::strcmp(argv[6],s.name))selected=&s;
  if(!selected)return 64; // Reject before any fixture event or actor exists.
#if defined(YMCC_STEAM_DRIVER_CLEANUP_NO_LAST_GUARD)
  if(selected->boundary!=B::LastGuard)return 64;
#endif
  try{
    const auto row=ymcc::steamOriginalRecovery::driverCleanupCase(selected->boundary,selected->name);
    const bool ok=row.at("ok").get<bool>();
    const nlohmann::json raw{{"ok",ok},{"scope","publication-revocation"},{"caseCount",1},
      {"cases",nlohmann::json::array({row})},{"defaultCasesExecuted",0},{"sourceReady",false}};
    std::puts(raw.dump().c_str());return ok?0:1;
  }catch(const std::exception& e){std::fprintf(stderr,"fixture failed: %s\n",e.what());return 70;}
}
