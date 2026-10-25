// Actual queue-empty worker, including production MSI metadata publication.
// Only this explicit suite selects the production metadata compile branch;
// all controller/SDK/device ports remain the existing isolated fixture ports.
#if defined(YMCC_WORKER_COST_PRODUCTION_METADATA)
static_assert(ymcc::msi::visibility::wake::buildEnabled,"worker measurement needs the production metadata-maintenance caller");
namespace fixtureWorkerCost {
static bool noDuty(){
 return !g_inputHostLifecycleWorkRequested.load()&&!g_inputHostLifecycleWorkInFlight.load()&&
  !g_inputHostSwitchVerificationPending.load()&&!g_inputHostSwitchExecutionReturnPending.load()&&
  !g_inputHostSwitchRecoveryPending.load()&&!g_inputHostRecoveryPending.load()&&!g_inputHostTupleHealPending.load()&&
  !g_msiCrashInheritedPending.load()&&!g_msiCrashReadRetryPending.load()&&!g_msiOwnedReturnPending.load()&&
  !g_msiTerminalReturnPending.load()&&!g_msiVisibilityWakePending.load()&&
  !g_msiNativeForegroundResumePending.load()&&!g_msiNativeForegroundCyclePending.load()&&
  !g_gamepadDriverSetup.active();
}
static bool prepare(const std::string& state){
 using Publisher=ymcc::msi::usbName::ContextPublisher;
 // Each isolated input owns its mapping; no observer or other process holds it.
 // The real publisher destructor closes only this fixture's previous handles.
 g_domainLogEnabled[1]=false;g_msiUsbNameContext.~Publisher();new(&g_msiUsbNameContext)Publisher();
 if(!fixtureRecoveryCost::prepare("all-clear")||!noDuty())return false;
 const bool generic=state=="loop-baseline"||state=="recovery-control"||state=="worker-generic-idle";
 if(generic){g_machineIdentity.family=YmccFamily::Unknown;g_machineIdentity.manufacturer="offline";g_machineIdentity.product="worker-cost-generic";}
 g_powerGeneration=17;g_domainLogEnabled[1]=false;
 {std::lock_guard<std::mutex> source(g_msiMainSourceMx);g_msiMainSource=HidHideMsiMainSourceRecord{};
  g_msiMainSource.valid=!generic;g_msiMainSource.generation=41;g_msiMainSource.vendorId=0x0db0;g_msiMainSource.productId=0x1902;
  g_msiMainSource.interfacePath="offline-worker-cost-msi-interface";g_msiMainSource.usbInstance="USB\\VID_0DB0&PID_1902\\OFFLINE-WORKER-COST";
  g_msiUsbNameSourcePowerGeneration=currentPowerGeneration();
 }
 {std::lock_guard<std::mutex> lock(g_externalPriorityMx);
  if(g_externalPriority.active||g_externalPriority.returning||g_externalPriority.takingOver||g_externalPriority.cycleProcess||externalPriorityHasDebt(g_externalPriority.debt))return false;
  g_externalPriority.nextTick=0; // due before warmup; the real consumer establishes empty-ledger state
 }
 if(state=="worker-msi-detail-on"||state=="worker-msi-after-detail-off")g_domainLogEnabled[1]=true;
 inputHostRunQueuedLifecycleWork();
 if(state=="worker-msi-after-detail-off"){if(!g_msiUsbNameContext.opened())return false;g_domainLogEnabled[1]=false;inputHostRunQueuedLifecycleWork();}
 if(state=="worker-msi-terminal")g_inputHostRecoveryTerminalLogged=true;
 const bool expectedMap=state=="worker-msi-detail-on"||state=="worker-msi-after-detail-off";
 {std::lock_guard<std::mutex> lock(g_externalPriorityMx);
  if(!g_externalPriority.loaded||g_externalPriority.storeInvalid||g_externalPriority.active||g_externalPriority.returning||externalPriorityHasDebt(g_externalPriority.debt)||g_externalPriority.cycleProcess)return false;
 }
 return noDuty()&&g_msiUsbNameContext.opened()==expectedMap&&!g_externalPriorityVirtualSuspended.load();
}
static json flags(){
 auto out=fixtureRecoveryCost::flags();out["metadataMappingOpened"]=g_msiUsbNameContext.opened();
 out["diagnosticsEnabled"]=g_domainLogEnabled[1].load();out["msiAuthorized"]=msiClawKnownBoardAuthority();
 out["noActiveDuty"]=noDuty();
 {std::lock_guard<std::mutex> lock(g_externalPriorityMx);out["externalLoaded"]=g_externalPriority.loaded;out["externalActive"]=g_externalPriority.active;out["externalReturning"]=g_externalPriority.returning;out["externalDebt"]=externalPriorityHasDebt(g_externalPriority.debt);out["externalState"]=g_externalPriority.state;out["externalReason"]=g_externalPriority.reason;}
 return out;
}
static ULONGLONG nextExternalTick(){std::lock_guard<std::mutex> lock(g_externalPriorityMx);return g_externalPriority.nextTick;}
}
static void workerMaintenanceCostCases(){
 using namespace fixtureRecoveryCost;
 const std::vector<std::string> states={"loop-baseline","recovery-control","worker-generic-idle","worker-msi-detail-off","worker-msi-detail-on","worker-msi-after-detail-off","worker-msi-terminal","worker-msi-maintenance-due"};
 for(unsigned trial=0;trial<trials;++trial)for(unsigned step=0;step<states.size();++step){
  const auto& selected=states[(trial*3+step)%states.size()];const unsigned count=selected=="worker-msi-maintenance-due"?1:calls;
  cases.push_back({{"name","worker-cost/"+selected+"/trial-"+std::to_string(trial)},{"kind","controlled-worker-cpu-sample"},
   {"state",selected},{"trial",trial},{"calls",count},{"warmupCalls",warmupCalls},{"productionMetadataBranchCompiled",true},
   {"timedFunction",selected=="loop-baseline"?"emptyLoopTarget":selected=="recovery-control"?"inputHostRecoveryMaintenanceTick":"inputHostRunQueuedLifecycleWork"},
   {"realUninstrumentedTransactionMutex",true},{"lockWaitIndividuallyMeasured",false},{"syntheticSourceMetadata",true},
   {"privateOwnProcessSharedMemoryOnly",true},{"settingsOrigin","existing-isolated-settings-endpoint"}});
  const bool established=fixtureWorkerCost::prepare(selected);check(established,"worker CPU input established without active return, process, device or queued lifecycle duty");
  if(!established)throw std::runtime_error("worker-cost input establishment failed");
  const Function target=selected=="loop-baseline"?emptyLoopTarget:selected=="recovery-control"?inputHostRecoveryMaintenanceTick:inputHostRunQueuedLifecycleWork;
  for(unsigned n=0;n<warmupCalls;++n)target();
  if(selected=="worker-msi-maintenance-due")fixturePowerClockOffset+=600; // one real periodic maintenance becomes due; no player deadline exists
  const auto before=fixtureWorkerCost::flags();const auto nextBefore=fixtureWorkerCost::nextExternalTick();
  const auto eventsBefore=fixtureEvents.size(),traceBefore=g_startEntrySeam.trace.size();const auto ioBefore=forbiddenIo,xinputBefore=inertXinputReads;
  const auto inspectionsBefore=ymcc::inputHost::switchVerificationRuntime::observation.checks;
  const auto measured=sample(target,count);
  const auto inspectionDelta=ymcc::inputHost::switchVerificationRuntime::observation.checks-inspectionsBefore;
  const auto nextAfter=fixtureWorkerCost::nextExternalTick();const auto after=fixtureWorkerCost::flags();
  const bool noIo=forbiddenIo==ioBefore&&inertXinputReads==xinputBefore&&fixtureEvents.size()==eventsBefore&&g_startEntrySeam.trace.size()==traceBefore;
  const bool stable=before==after;const bool duePollObserved=nextAfter!=nextBefore;
  check(measured.wallTicks>0&&measured.qpcFrequency>0&&measured.wallNs>0,"worker CPU counters cover the complete original worker or declared control");
  check(noIo,"worker CPU steady sample performs no device, log, new queue, Stop or Start transaction");
  check(stable&&inspectionDelta==0&&(selected!="worker-msi-maintenance-due"||duePollObserved),"worker CPU sample preserves declared source duty and observes the required periodic maintenance");
  cases.back()["sample"]={{"wallTicks",measured.wallTicks},{"qpcFrequency",measured.qpcFrequency},{"wallNs",measured.wallNs},{"threadCycles",measured.threadCycles},{"threadCpu100ns",measured.threadCpu100ns}};
  cases.back()["before"]=before;cases.back()["after"]=after;cases.back()["noDeviceOrTransactionIo"]=noIo;cases.back()["stateStable"]=stable;cases.back()["inspectionDelta"]=inspectionDelta;
  cases.back()["mappingOpenedBefore"]=before["metadataMappingOpened"];cases.back()["mappingOpenedAfter"]=after["metadataMappingOpened"];
  cases.back()["duePollObserved"]=duePollObserved;cases.back()["externalNextTickBefore"]=nextBefore;cases.back()["externalNextTickAfter"]=nextAfter;
 }
 g_domainLogEnabled[1]=false;g_externalPriorityVirtualSuspended=false;
}
#else
static void workerMaintenanceCostCases(){throw std::runtime_error("worker-cost needs the explicit production-metadata measurement build");}
#endif