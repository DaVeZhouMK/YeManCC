static json msiShieldReadServiceInventory(){json rows=json::array();for(const auto& [name,v]:services)rows.push_back({{"service",W2U(name)},{"startType",v.type},{"serviceState",v.state},{"mutationPolicy","preserve-unverified-dependency"}});return {{"readOnly",true},{"complete",!scmFail},{"services",rows},{"discoveryAuthorizesMutation",false}};}
static json msiShieldReadProtectedService(){auto i=services.find(L"MSI Foundation Service");if(scmFail)return {{"readOnly",true},{"readbackVerified",false},{"existsKnown",false}};if(i==services.end())return {{"readOnly",true},{"readbackVerified",false},{"existsKnown",true},{"exists",false}};return {{"readOnly",true},{"readbackVerified",!i->second.deny&&!i->second.queryFail},{"existsKnown",true},{"exists",true},{"startType",i->second.type},{"serviceState",i->second.state},{"controllerOnlyRoleVerified",false}};}
#ifdef YMCC_VENDOR_R15
static bool oemVendorTaskQuery(const std::wstring& name,OemVendorTaskFacts& facts){++taskQueries;facts={};auto it=tasks.find(name);if(it==tasks.end())return true;if(it->second.deny){SetLastError(ERROR_ACCESS_DENIED);return false;}facts={true,it->second.enabled,it->second.running};return true;}
static bool oemVendorTaskSet(const std::wstring& name,bool enabled,bool stop,bool run){auto it=tasks.find(name);if(it==tasks.end()){SetLastError(ERROR_FILE_NOT_FOUND);return false;}auto& t=it->second;if(t.setFail){SetLastError(ERROR_ACCESS_DENIED);return false;}t.enabled=enabled;++taskMutations;if(stop&&!t.stuck)t.running=false;if(run){t.running=true;++taskStarts;}return true;}
static bool oemVendorProcessesStop(const std::vector<std::wstring>& names,unsigned long long epoch){++processCalls;if(processFail)return false;for(const auto& n:names)if(oemVendorStackIntentCurrent(epoch)&&std::find(processes.begin(),processes.end(),n)!=processes.end())stoppedProcesses.push_back(n);return true;}
#endif
static int cases=0,failed=0;
static void check(bool ok,const char* text){++cases;std::cout<<(ok?"PASS ":"FAIL ")<<text<<"\n";if(!ok)++failed;}
static bool state(const char* text){return std::any_of(logs.begin(),logs.end(),[&](const auto& x){return x.value("state","")==text;});}
static void reset(){g_oemVendorShieldWake = {}; g_powerGeneration = 1; g_powerLifecycle = PowerLifecycle::Ready; wakeInputResumes = 0; taskQueries = 0; services.clear();logs.clear();jobs.clear();journal.clear();tasks.clear();processes.clear();stoppedProcesses.clear();onPersist={};journalExists=false;journalFail=scmFail=queueFail=processFail=false;mutationCount=stops=starts=taskMutations=taskStarts=processCalls=0;g_exitRequested=false;tick=1;g_oemVendorServiceLeases.clear();g_oemVendorStackLoaded=false;g_oemVendorStackJournalValid=true;g_oemVendorStackWanted=false;g_oemVendorShortcutWanted=false;g_oemVendorShieldWanted=false;g_oemVendorStackIntentControllerOnly=false;g_oemVendorStackLastOutcomeKnown=false;g_oemVendorStackLastOutcomeOk=false;g_oemVendorStackLastOutcomeEpoch=0;g_oemVendorStackLastOutcome="none";g_oemVendorMsiServiceInventory=json::object();g_oemVendorMsiProtectedServiceObservation=json::object();g_oemVendorStackOpQueued=false;g_oemVendorStackRequested=1;g_oemVendorStackCompleted=0;g_machineIdentity.family=YmccFamily::AsusRogAlly;g_machineIdentity.deviceClass="HandheldCompanion.Devices.ROGAlly";SetLastError(0);
#ifdef YMCC_VENDOR_R15
 g_oemVendorTaskLeases.clear();g_oemVendorStackIntentSeen=false;g_oemVendorStackStartupRecovered=false;g_oemVendorStackIntentFamily=YmccFamily::Unknown;g_oemVendorStackAttempts=0;g_oemVendorStackRetryAt=0;
#endif
}
static void seed(){
 if(g_machineIdentity.family==YmccFamily::MsiClaw)g_machineIdentity.deviceClass="HandheldCompanion.Devices.ClawA1M";
 else if(g_machineIdentity.family==YmccFamily::LenovoLegionGo)g_machineIdentity.deviceClass="HandheldCompanion.Devices.LegionGoTablet";
 else if(g_machineIdentity.family==YmccFamily::ZotacGamingZone)g_machineIdentity.deviceClass="HandheldCompanion.Devices.GamingZone";
 for(const auto& n:oemVendorServiceNamesForFamily(g_machineIdentity.family))services[n]={};}
static void drain(){size_t n=0;while(!jobs.empty()&&++n<32){auto f=std::move(jobs.front());jobs.erase(jobs.begin());f();}check(n<32,"serial queue remains bounded");}
int main(){
#ifdef YMCC_VENDOR_R15
 // A saved ON request must not restart MSI startup tasks while adopting its
 // exact previous-process task leases. OFF still owes their original state.
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();
 for(const auto& name:oemVendorTaskNamesForFamily(g_machineIdentity.family))tasks[name]={};
 oemVendorStackShieldRequest(true);drain();
 const auto restartJournal=journal;const auto restartTasks=tasks;
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();
 journalExists=true;journal=restartJournal;tasks=restartTasks;
 oemVendorStackShieldRequest(true);drain();
 check(taskStarts==0&&taskMutations==0,"MSI saved ON restart never re-enables or starts already-disabled task leases");
 check(g_oemVendorTaskLeases.size()==2&&json::parse(journal)["tasks"].size()==2,"MSI restart preserves durable task originals until OFF");
 check(state("applied"),"MSI restart confirms current disabled task readbacks");
 oemVendorStackShieldRequest(false);drain();
 check(taskStarts==2&&g_oemVendorTaskLeases.empty(),"MSI OFF after retained restart restores both originally-running tasks");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();
 journalExists=true;journal=restartJournal;tasks=restartTasks;
 for(auto& [name,task]:tasks){task.enabled=true;task.running=true;}
 oemVendorStackShieldRequest(true);drain();
 check(taskStarts==0&&taskMutations==2,"MSI retained restart re-disables externally enabled tasks without first running them");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();
 journalExists=true;journal=restartJournal;tasks=restartTasks;
 oemVendorStackShieldRequest(false);drain();
 check(taskStarts==2&&g_oemVendorTaskLeases.empty(),"MSI saved OFF startup still restores the original task leases");
#endif

 reset();seed();oemVendorStackRequest(true);drain();check(stops==3,"ROG all three HC services stopped");check(state("applied"),"aggregate service readback success");check(g_oemVendorServiceLeases.size()==3,"durable original service facts");
 oemVendorStackRequest(true);drain();check(starts==0,"unchanged takeover never briefly restarts Armoury Crate");check(stops==3,"unchanged takeover performs no redundant stop");oemVendorStackRestore("exit");check(starts==3&&g_oemVendorServiceLeases.empty(),"normal exit restores originals and clears debt");
 reset();seed();services.begin()->second.stopDelay=3000;oemVendorStackRequest(true);drain();check(state("applied"),"realistic 3-second service stop is not lost at old 2-second deadline");
 reset();seed();services.begin()->second.state=SERVICE_START_PENDING;services.begin()->second.settleAt=1001;oemVendorStackRequest(true);drain();check(stops==3&&state("applied"),"START_PENDING stabilizes before takeover instead of permanent skip");
 reset();seed();services.begin()->second.stopFail=true;oemVendorStackRequest(true);drain();check(!state("applied")&&state("apply-failed"),"failed stop never claims applied");check(g_oemVendorServiceLeases.size()==3,"failed stop retains original fact");services.begin()->second.stopFail=false;tick+=1000;oemVendorStackSchedule();drain();check(state("applied"),"transient failed stop retried on existing refresh");check(starts==0,"retry does not restore/restart already acquired services");
 reset();seed();services.begin()->second.stuck=true;oemVendorStackRequest(true);drain();check(!state("applied"),"stuck service never claims suppression");check(tick<=10051,"service wait remains bounded at ten seconds");
 reset();seed();journalFail=true;oemVendorStackRequest(true);drain();check(mutationCount==0&&stops==0,"write-ahead lease failure prevents all SCM mutations");
 reset();seed();services.begin()->second.deny=true;oemVendorStackRequest(true);drain();check(!state("applied"),"unreadable service forbids aggregate success");
 reset();seed();services.begin()->second.state=SERVICE_STOPPED;oemVendorStackRequest(true);drain();oemVendorStackRestore("exit");check(starts==2,"original stopped service is never newly started");
 reset();seed();services.begin()->second.type=SERVICE_DISABLED;services.begin()->second.state=SERVICE_STOPPED;oemVendorStackRequest(true);drain();check(stops==2&&mutationCount==2,"already-disabled/stopped service receives no mutation and no invented lease");oemVendorStackRestore("exit");check(services.begin()->second.type==SERVICE_DISABLED&&services.begin()->second.state==SERVICE_STOPPED,"pre-existing disabled policy survives restoration");
 reset();seed();services.begin()->second.type=SERVICE_DISABLED;oemVendorStackRequest(true);drain();check(stops==2&&!state("applied"),"disabled-but-running unqualified original remains explicit hold");
 reset();seed();oemVendorStackRequest(true);drain();services.begin()->second.configFail=true;oemVendorStackRestore("exit");check(g_oemVendorServiceLeases.size()==1,"restore failure retains debt");check(json::parse(journal)["services"].size()==1,"restore failure remains durable");services.begin()->second.configFail=false;oemVendorStackRestore("retry");check(g_oemVendorServiceLeases.empty(),"later recovery discharges failed restoration");
 reset();seed();oemVendorStackRequest(true);drain();services.begin()->second.type=SERVICE_DEMAND_START;oemVendorStackRestore("exit");check(services.begin()->second.type==SERVICE_DEMAND_START&&!g_oemVendorServiceLeases.empty(),"third-party start policy is not overwritten");
 reset();seed();oemVendorStackRequest(true);drain();journalFail=true;oemVendorStackRestore("exit");check(g_oemVendorServiceLeases.size()==3,"failed journal commit cannot erase in-memory recovery debt");journalFail=false;oemVendorStackRestore("retry");check(g_oemVendorServiceLeases.empty(),"recovery debt commits on later successful write");
 reset();seed();journalExists=true;journal="{broken";oemVendorStackRequest(true);drain();check(stops==0&&state("recovery-held"),"corrupt journal holds all writes");
 reset();seed();journalExists=true;journal=json({{"schema",1},{"family",static_cast<int>(g_machineIdentity.family)},{"services",json::array({{{"name","UnrelatedService"},{"originalStartType",2},{"wasRunning",true}}})}}).dump();oemVendorStackRequest(true);drain();check(mutationCount==0&&state("recovery-held"),"unbound service in recovery journal rejected");
 reset();seed();queueFail=true;oemVendorStackRequest(true);check(jobs.empty()&&!g_oemVendorStackOpQueued,"queue refusal releases publication ownership");queueFail=false;oemVendorStackSchedule();drain();check(stops==3,"rejected publication can use existing refresh retry");
 reset();seed();oemVendorStackRequest(true);g_exitRequested=true;drain();check(mutationCount==0,"queued job after exit never disables");
 reset();seed();oemVendorStackRequest(true);oemVendorStackRequest(false);drain();check(mutationCount==0,"latest rule clear beats queued takeover");
 reset();seed();oemVendorStackRequest(true);drain();oemVendorStackRequest(false);drain();check(starts==3&&g_oemVendorServiceLeases.empty(),"cleared or disabled executable rules return manufacturer control");
 reset();seed();onPersist=[]{oemVendorStackRequest(false);};oemVendorStackRequest(true);drain();check(stops==0,"rule clear during durable write fences remaining stop operations");
 reset();seed();scmFail=true;oemVendorStackRequest(true);drain();check(!state("applied")&&state("apply-failed"),"SCM access failure remains observable");
 reset();g_machineIdentity.family=YmccFamily::Gpd;oemVendorStackRequest(true);drain();check(stops==0,"no services inferred for generic GPD family");
#ifdef YMCC_VENDOR_R15
 reset();seed();g_machineIdentity.deviceClass="HandheldCompanion.Devices.AsusDevice";oemVendorStackRequest(true);drain();check(stops==0&&mutationCount==0,"generic ASUS fallback cannot acquire ROG manufacturer services");
 reset();seed();services.begin()->second.state=SERVICE_PAUSED;oemVendorStackRequest(true);drain();check(state("applied"),"paused service can be stopped under its durable original-state lease");oemVendorStackRestore("exit");check(services.begin()->second.state==SERVICE_PAUSED,"original paused service is restored to Paused not silently promoted to Running");
 reset();seed();services.begin()->second.stopFail=true;oemVendorStackRequest(true);drain();for(int i=0;i<6;++i){tick+=30000;oemVendorStackSchedule();drain();}check(state("retry-exhausted")&&g_oemVendorStackAttempts==3,"per-intent retry budget exhausts with failure not success");const int oldStops=stops;tick+=30000;oemVendorStackRequest(true);drain();check(stops==oldStops,"unchanged failed intent does not become a permanent poller");
 reset();seed();oemVendorStackRequest(true);drain();auto& s=services.begin()->second;s.type=SERVICE_DEMAND_START;s.stopFail=true;g_oemVendorStackCompleted=0;oemVendorStackSchedule();drain();check(s.type==SERVICE_DEMAND_START,"retry respects externally changed start policy");
 reset();seed();oemVendorStackRequest(true);drain();auto oldJournal=json::parse(journal);oldJournal["schema"]=1;oldJournal.erase("tasks");journal=oldJournal.dump();g_oemVendorServiceLeases.clear();g_oemVendorStackLoaded=false;check(oemVendorStackRestoreLocked("schema1-recovery"),"R13 schema1 service journal remains recoverable");
 for(const auto fam:{YmccFamily::LenovoLegionGo,YmccFamily::MsiClaw,YmccFamily::ZotacGamingZone}){
  reset();g_machineIdentity.family=fam;seed();for(const auto& n:oemVendorTaskNamesForFamily(fam))tasks[n]={};processes=oemVendorProcessNamesForFamily(fam);processes.push_back(L"UnrelatedProcess");oemVendorStackRequest(true);drain();check(state("applied"),"HC manufacturer stack service/task/process paths all admitted");check(stoppedProcesses.size()==oemVendorProcessNamesForFamily(fam).size(),"only exact HC process allowlist stopped in memory boundary");for(const auto& [n,t]:tasks)check(!t.enabled&&!t.running,"MSI scheduled task disabled and terminal");oemVendorStackRequest(false);drain();check(g_oemVendorServiceLeases.empty()&&g_oemVendorTaskLeases.empty(),"family restore commits all service/task debt");for(const auto& [n,t]:tasks)check(t.enabled&&t.running,"MSI original enabled/running task restored");
 }
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();for(const auto& n:oemVendorTaskNamesForFamily(g_machineIdentity.family))tasks[n]={};processes=oemVendorShieldProcessNamesForFamily(g_machineIdentity.family);oemVendorStackShieldRequest(true);drain();check(state("service-preserve-unverified"),"MSI shield does not mutate the protected physical-controller service");check(state("applied"),"MSI controller-only shield applies exact task/process policy");check(mutationCount==0,"MSI shield has no Foundation Service mutation");check(stoppedProcesses.size()==processes.size(),"MSI shield stops only its exact front-end process allowlist");for(const auto& [n,t]:tasks)check(!t.enabled&&!t.running,"MSI shield disables exact startup tasks");oemVendorStackShieldRequest(false);drain();check(g_oemVendorTaskLeases.empty(),"MSI shield restore clears task lease");for(const auto& [n,t]:tasks)check(t.enabled&&t.running,"MSI shield restore returns original task state");
reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();tasks[L"MSI_Center_M_Server"]={false,false};oemVendorStackRequest(true);drain();check(taskMutations==0&&g_oemVendorTaskLeases.empty(),"pre-disabled task is not owned or modified");oemVendorStackRestore("exit");check(!tasks.begin()->second.enabled&&taskStarts==0,"pre-disabled task not enabled or started on return");
reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();tasks[L"MSI_Center_M_Server"]={false,true};oemVendorStackShieldRequest(true);drain();oemVendorStackShieldRequest(false);drain();check(!tasks[L"MSI_Center_M_Server"].enabled&&tasks[L"MSI_Center_M_Server"].running,"disabled-but-running MSI task restores both policy and runtime state");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();tasks[L"MSI_Center_M_Server"]={};journalFail=true;oemVendorStackRequest(true);drain();check(taskMutations==0&&mutationCount==0&&stoppedProcesses.empty(),"task write-ahead failure blocks every mutation");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();tasks[L"MSI_Center_M_Server"]={};tasks.begin()->second.stuck=true;oemVendorStackRequest(true);drain();check(!state("applied")&&state("task-disable-failed"),"task stop without terminal state cannot claim success");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();tasks[L"MSI_Center_M_Server"]={};oemVendorStackRequest(true);drain();tasks.begin()->second.setFail=true;oemVendorStackRestore("exit");check(g_oemVendorTaskLeases.size()==1,"failed task restoration remains debt");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();tasks[L"MSI_Center_M_Server"]={};tasks.begin()->second.deny=true;oemVendorStackRequest(true);drain();check(!state("applied"),"task access denied is not capability absence");
 reset();g_machineIdentity.family=YmccFamily::LenovoLegionGo;seed();processFail=true;oemVendorStackRequest(true);drain();check(!state("applied"),"failed manufacturer-process stop blocks aggregate success");
 reset();seed();check(oemVendorTaskNamesForFamily(g_machineIdentity.family).empty()&&oemVendorProcessNamesForFamily(g_machineIdentity.family).empty(),"no ROG task/process names guessed beyond HC source");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();journalExists=true;journal=json({{"schema",2},{"family",static_cast<int>(g_machineIdentity.family)},{"services",json::array()},{"tasks",json::array({{{"name","ArbitraryTask"},{"wasEnabled",true},{"wasRunning",true}}})}}).dump();oemVendorStackRequest(true);drain();check(taskMutations==0&&mutationCount==0&&state("recovery-held"),"foreign task journal cannot direct arbitrary TaskScheduler writes");
#endif
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();services[L"MSI Foundation Service"]={};services[L"MSI_Unclassified_Service"]={};tasks[L"MSI_Center_M_Server"]={};oemVendorStackShieldRequest(true);drain();auto observation=oemVendorStackShieldRuntimeSnapshot();check(observation["serviceInventory"]["readOnly"]==true&&state("msi-service-inventory"),"MSI shield exports a read-only service inventory");check(observation["serviceInventory"]["discoveryAuthorizesMutation"]==false,"discovered service names never authorize mutations");check(observation["protectedServiceObservation"]["readbackVerified"]==true,"Foundation state has an actual read-only observation");check(mutationCount==0&&stops==0&&services[L"MSI_Unclassified_Service"].state==SERVICE_RUNNING,"unknown MSI service and Foundation are not stopped");check(observation["protectedServiceObservation"]["controllerOnlyRoleVerified"]==false,"SCM readback is not proof of a controller-only role");check(observation["serviceInventory"]["observedRequestedEpoch"]==observation["requestedEpoch"],"inventory epoch is bound to its shield intent");oemVendorStackRequest(true);drain();oemVendorStackShieldRequest(false);drain();check(oemVendorStackShieldRuntimeSnapshot()["shortcutOwnerActive"]==true&&oemVendorStackShieldRuntimeSnapshot()["isolationOwnerActive"]==true,"disabling shield exposes remaining shortcut owner instead of promising full restoration");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();services[L"MSI Foundation Service"].queryFail=true;oemVendorStackShieldRequest(true);drain();check(oemVendorStackShieldRuntimeSnapshot()["protectedServiceObservation"]["readbackVerified"]==false,"unreadable preserved service is not reported as verified");
 reset();seed();oemVendorStackRequest(true);drain();check(!state("msi-service-inventory")&&oemVendorStackShieldRuntimeSnapshot()["serviceInventory"].empty(),"ROG legacy takeover does not perform MSI service diagnostics");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();auto telemetryTargets=oemVendorProcessNamesForFamily(g_machineIdentity.family);check(std::find(telemetryTargets.begin(),telemetryTargets.end(),L"MCMOSDInfo")==telemetryTargets.end()&&std::find(telemetryTargets.begin(),telemetryTargets.end(),L"MSI Center OSD Info")==telemetryTargets.end(),"both telemetry executable names excluded from MSI process targets");auto shieldTelemetryTargets=oemVendorShieldProcessNamesForFamily(g_machineIdentity.family);check(std::find(shieldTelemetryTargets.begin(),shieldTelemetryTargets.end(),L"MSI Center OSD Info")==shieldTelemetryTargets.end(),"explicit shield does not stop an unverified telemetry alias");

#ifdef YMCC_VENDOR_R15
 reset(); g_machineIdentity.family = YmccFamily::MsiClaw; seed();
 for (const auto& name : oemVendorTaskNamesForFamily(g_machineIdentity.family)) tasks[name] = {};
 oemVendorStackShieldRequest(true); drain();
 const auto taskProof = std::find_if(logs.begin(), logs.end(), [](const auto& row) {
     return row.value("state", "") == "task-disabled" && row.value("runningBefore", false);
 });
 check(taskProof != logs.end() && (*taskProof)["mutationAttempted"] == true &&
       (*taskProof)["readbackVerified"] == true && (*taskProof)["runningToStoppedVerified"] == true &&
       (*taskProof)["runningReadback"] == false && (*taskProof)["requestedEpoch"] == g_oemVendorStackRequested,
       "task proof records running before mutation and stopped after readback in the same request");
 reset(); g_machineIdentity.family = YmccFamily::MsiClaw; seed();
 for (const auto& name : oemVendorTaskNamesForFamily(g_machineIdentity.family)) tasks[name] = {};
 for (auto& [name, task] : tasks) { task.enabled = false; task.running = false; }
 oemVendorStackShieldRequest(true); drain();
 const auto inactiveProof = std::find_if(logs.begin(), logs.end(), [](const auto& row) {
     return row.value("state", "") == "task-disabled";
 });
 check(inactiveProof != logs.end() && (*inactiveProof)["alreadyInactive"] == true &&
       (*inactiveProof)["mutationAttempted"] == false && (*inactiveProof)["runningToStoppedVerified"] == false,
       "already disabled tasks cannot masquerade as running-to-stopped proof");
 const auto appliedProof = std::find_if(logs.begin(), logs.end(), [](const auto& row) {
     return row.value("state", "") == "applied";
 });
 check(appliedProof != logs.end() && (*appliedProof)["requiresReboot"] == false &&
       (*appliedProof)["externalDefaultActionSuppressed"] == "DEVICE_UNVERIFIED",
       "operation completes without reboot while physical OEM action remains unverified");
#endif

#ifdef YMCC_SHIELD_IPC_FIXTURE
 registerShieldIpcFixture();
 const auto prepareShield = [](const json& input) {
     reset(); g_machineIdentity.family = YmccFamily::MsiClaw; seed();
     g_machineIdentity.deviceClass = "HandheldCompanion.Devices.ClawCG3EM";
     for (const auto& name : oemVendorTaskNamesForFamily(g_machineIdentity.family)) tasks[name] = {};
     services[L"MSI Foundation Service"] = {};
     g_shieldIpcSettings = {{"input", input}};
     g_shieldWriteFails = g_shieldWriteConflict = false; g_shieldWrites = 0;
 };
 check(msiControllerShieldRequested(json::object()), "absent shield preference defaults on");
 check(!msiControllerShieldRequested({{"msiControllerShieldEnabled", false}}), "saved false overrides default across upgrades");
 check(msiControllerShieldRequested({{"msiControllerShieldEnabled", true}}), "saved true stays enabled");
 for (const auto& invalid : json::array({nullptr, "false", "true", 0, 1, json::array(), json::object()})) {
     check(!msiControllerShieldRequested({{"msiControllerShieldEnabled", invalid}}),
           "malformed saved preference cannot enable or throw");
 }
 check(!msiControllerShieldRequested(json::array()), "non-object policy input fails closed");
 prepareShield(json::object());
 const auto firstRead = g_shieldIpc["msiControllerShield.get"](json::object());
 check(firstRead["enabled"] == true && firstRead["supported"] == true && firstRead["applied"] == false &&
       jobs.empty() && g_shieldWrites == 0, "first getter reports default intent without applying or persisting it");
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 const auto defaultApplied = g_shieldIpc["msiControllerShield.get"](json::object());
 check(defaultApplied["enabled"] == true && defaultApplied["applied"] == true &&
       services[L"MSI Foundation Service"].state == SERVICE_RUNNING && mutationCount == 0,
       "default startup applies only the admitted MSI task/process policy and preserves Foundation");
 const auto disabled = g_shieldIpc["msiControllerShield.set"]({{"enabled", false}}); drain();
 check(disabled["ok"] == true && ymSettingsSection("input")["msiControllerShieldEnabled"] == false &&
       ymSettingsSection("input")["revision"] == 1, "manual off is persisted by the real CAS path");
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 check(g_shieldIpc["msiControllerShield.get"](json::object())["enabled"] == false &&
       !oemVendorStackShieldIsWanted() && g_oemVendorTaskLeases.empty(),
       "background refresh does not turn an explicit off back on");
 const auto savedOff = ymSettingsSection("input"); prepareShield(savedOff);
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 check(taskMutations == 0 && processCalls == 0 && !oemVendorStackShieldIsWanted(),
       "persisted off survives a new runtime rather than becoming default on");
 const auto enabledAgain = g_shieldIpc["msiControllerShield.set"]({{"enabled", true}}); drain();
 check(enabledAgain["ok"] == true && ymSettingsSection("input")["revision"] == 2 &&
       g_shieldIpc["msiControllerShield.get"](json::object())["applied"] == true,
       "manual re-enable after explicit off still applies");
 for (const auto* device : {"ClawA1M", "ClawA2VM", "ClawBZ2EM", "ClawCG3EM"}) {
     prepareShield(json::object()); g_machineIdentity.deviceClass = std::string("HandheldCompanion.Devices.") + device;
     oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
     check(oemVendorStackShieldRuntimeSnapshot()["applied"] == true, "default on reaches each admitted Claw model");
 }
 for (const auto family : {YmccFamily::AsusRogAlly, YmccFamily::Gpd, YmccFamily::LenovoLegionGo, YmccFamily::Unknown}) {
     prepareShield(json::object()); g_machineIdentity.family = family; seed();
     oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
     check(!oemVendorStackShieldIsWanted() && taskMutations == 0 && mutationCount == 0 && processCalls == 0,
           "default preference never grants MSI operations to a different or unknown family");
 }
 prepareShield(json::object()); g_machineIdentity.deviceClass = "HandheldCompanion.Devices.UnverifiedClaw";
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 check(!oemVendorStackShieldIsWanted() && taskMutations == 0 && processCalls == 0,
       "unknown MSI model remains outside automatic isolation");
 prepareShield(json::object()); g_machineIdentity.family = YmccFamily::Unknown;
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input")));
 check(jobs.empty(), "unresolved identity does not queue default isolation");
 g_machineIdentity.family = YmccFamily::MsiClaw;
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 check(oemVendorStackShieldRuntimeSnapshot()["applied"] == true,
       "later resolved identity applies default on without a settings revision change");
 for (const auto& request : json::array({nullptr, true, "true", 1, json::array(), json::object(),
                                      json{{"enabled", nullptr}}, json{{"enabled", "false"}}, json{{"enabled", 1}}})) {
     prepareShield(json::object());
     const auto result = g_shieldIpc["msiControllerShield.set"](request);
     check(result["ok"] == false && result["enabled"] == true && result["reason"] == "invalid-enabled" &&
           g_shieldWrites == 0 && jobs.empty(), "malformed toggle is rejected without silently disabling the default");
 }
 for (const auto& input : json::array({json::object(), json{{"msiControllerShieldEnabled", false}}, json{{"msiControllerShieldEnabled", true}}})) {
     prepareShield(input); g_shieldWriteFails = true;
     const auto result = g_shieldIpc["msiControllerShield.set"]({{"enabled", false}});
     check(result["ok"] == false && result["enabled"] == msiControllerShieldRequested(input) &&
           ymSettingsSection("input") == input && jobs.empty(),
           "storage failure reports the current saved or default preference and does not apply");
 }
 prepareShield({{"revision", 7}, {"msiControllerShieldEnabled", false}, {"outputTarget", {{"persona", "steamdeck"}}}});
 g_shieldWriteConflict = true;
 const auto conflicted = g_shieldIpc["msiControllerShield.set"]({{"enabled", true}});
 check(conflicted["ok"] == false && conflicted["reason"] == "settings-revision-conflict" &&
       conflicted["enabled"] == false && ymSettingsSection("input")["revision"] == 7 && jobs.empty(),
       "revision conflict preserves saved off and does not apply an uncommitted request");
 g_shieldWriteConflict = false;
 const auto committed = g_shieldIpc["msiControllerShield.set"]({{"enabled", true}});
 check(committed["ok"] == true && ymSettingsSection("input")["revision"] == 8 &&
       ymSettingsSection("input")["outputTarget"]["persona"] == "steamdeck",
       "successful boolean CAS preserves unrelated controller configuration");
 for (const auto& revision : json::array({nullptr, true, "0", -1, 0.5,
          json((std::numeric_limits<long long>::max)()), json((std::numeric_limits<unsigned long long>::max)())})) {
     prepareShield({{"revision", revision}});
     const auto result = g_shieldIpc["msiControllerShield.set"]({{"enabled", false}});
     check(result["ok"] == false && result["enabled"] == true &&
           ymSettingsSection("input")["revision"] == revision && g_shieldWrites == 0 && jobs.empty(),
           "invalid or overflowing revision is rejected without exception or mutation");
 }
 prepareShield({{"revision", (std::numeric_limits<long long>::max)() - 1}});
 check(g_shieldIpc["msiControllerShield.set"]({{"enabled", false}})["ok"] == true &&
       ymSettingsSection("input")["revision"] == (std::numeric_limits<long long>::max)(),
       "highest incrementable native revision does not overflow");
 prepareShield(json::object());
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input")));
 g_shieldIpc["msiControllerShield.set"]({{"enabled", false}}); drain();
 check(taskMutations == 0 && processCalls == 0 && !oemVendorStackShieldIsWanted(),
       "manual off wins over a queued default startup before any stop is issued");

 prepareShield(json::object());
 onPersist = [] { g_shieldIpc["msiControllerShield.set"]({{"enabled", false}}); };
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 check(taskMutations == 0 && stoppedProcesses.empty() && g_oemVendorTaskLeases.empty() &&
       !msiControllerShieldRequested(ymSettingsSection("input")),
       "manual off during lease persistence fences default isolation and clears recovery debt");
 prepareShield(json::object()); processFail = true;
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 for (int attempt = 0; attempt < 4; ++attempt) { tick += 4000; oemVendorStackSchedule(); drain(); }
 const auto failedDefault = g_shieldIpc["msiControllerShield.get"](json::object());
 check(failedDefault["applied"] == false && failedDefault["lastOutcome"] == "retry-exhausted" && processCalls == 3,
       "default-on stop failure exhausts the bounded retry budget without claiming success");
 const auto completedCalls = processCalls;
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 g_shieldIpc["msiControllerShield.get"](json::object());
 check(processCalls == completedCalls && jobs.empty(), "unchanged default and status reads do not create endless retries");
 g_shieldIpc["msiControllerShield.set"]({{"enabled", false}}); drain(); processFail = false;
 g_shieldIpc["msiControllerShield.set"]({{"enabled", true}}); drain();
 check(g_shieldIpc["msiControllerShield.get"](json::object())["applied"] == true,
       "explicit off-on permits a fresh successful attempt after a failed default startup");
 prepareShield(json::object());
 oemVendorStackShieldRequest(true); drain(); oemVendorStackRequest(true); drain();
 g_shieldIpc["msiControllerShield.set"]({{"enabled", false}}); drain();
 const auto otherOwner = g_shieldIpc["msiControllerShield.get"](json::object());
 check(otherOwner["enabled"] == false && otherOwner["applied"] == false &&
       otherOwner["shortcutOwnerActive"] == true && otherOwner["isolationOwnerActive"] == true,
       "switching off the default retains a separate shortcut owner and exposes it to the UI");
 prepareShield(json::object());
 services[L"MSI Foundation Service"].state = SERVICE_STOPPED;
 services[L"MSI Foundation Service"].type = SERVICE_DEMAND_START;
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 check(services[L"MSI Foundation Service"].state == SERVICE_STOPPED &&
       services[L"MSI Foundation Service"].type == SERVICE_DEMAND_START && mutationCount == 0 && starts == 0,
       "default isolation also preserves a Foundation service the user had already stopped");
 prepareShield(json::object());
 oemVendorStackShieldRequest(true); drain();
 const auto crashJournal = journal;
 prepareShield(json::object()); journal = crashJournal; journalExists = true;
 for (auto& [name, task] : tasks) { task.enabled = false; task.running = false; }
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 check(oemVendorStackShieldRuntimeSnapshot()["applied"] == true && g_oemVendorTaskLeases.size() == 2,
       "default startup recovers a previous lease and reapplies isolation");
 oemVendorStackRestore("fixture-app-exit");
 check(g_oemVendorTaskLeases.empty() && tasks[L"MSI_Center_M_Server"].enabled && tasks[L"MSI_Center_M_Server"].running,
       "exit still restores the original enabled and running task state after crash recovery");
#endif
#include "msi_controller_shield_wake_cases.h"
 std::cout<<"cases="<<cases<<" failures="<<failed<<" hardwareCalls=0 RESULT="<<(failed?"FAIL":"PASS")<<"\n";return failed?1:0;
}
