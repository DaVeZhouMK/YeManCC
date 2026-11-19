// Compiled only for the explicit original private-child budget-wire variant.
// Real entry/channel/Steam lease/ManualRepair/Runtime; settings/UI ports inert.
namespace budgetwirefixture {
using Json=nlohmann::json;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static std::atomic<unsigned long long> g_powerGeneration{11};
static unsigned long long currentPowerGeneration(){return g_powerGeneration.load();}
static std::mutex traceMutex;static std::condition_variable traceChanged;
static Json traceRows=Json::array();static bool finished=false;
static void deckySidebarDiagnostic(const char*,const Json& detail,bool,const ymcc::steamfailure::FailureContext* failure){
 auto line=ymcc::deckydiag::failureRecord(detail,*failure);
 /*YMCC_ACTUAL_WRITER_POWER_STAMP*/
 {std::lock_guard lock(traceMutex);traceRows.push_back(line);if(line.value("actionPhase",std::string{})=="finished"&&line.value("actionOperation",std::string{})=="rebuild-ui-document"&&line.value("actionId",std::string{})!="0")finished=true;}
 traceChanged.notify_all();
}
static const auto actualFailureContext=/*YMCC_ACTUAL_FAILURE_CONTEXT*/;
static const auto actualFailureDiagnostic=/*YMCC_ACTUAL_FAILURE_DIAGNOSTIC*/;
static Json child(const ymcc::deckyrepair::cleanupworker::Request& request,HANDLE cancel,const ymcc::deckybinding::worker::SteamLease* lease){
 const auto mode=request.state.at("fixtureBudgetMode").template get<std::string>();
 need(mode=="reserved-only"||mode=="reserve-refused"||mode=="malformed-metadata"||mode=="uncertain"||mode=="held-power"||mode=="held-off","unknown isolated budget child mode");
 budgetfixture::ports=budgetfixture::Ports{};auto& ports=budgetfixture::ports;
 ports.primary={{"schemaVersion",1},{"businessOpaque","private-memory"},{"extensions",{{"deckySidebar",{{"enabled",true}}}}}};ports.backup=ports.primary;
 if(mode=="reserve-refused")ports.preserve=false;
 const auto current=[&]{return cancel&&WaitForSingleObject(cancel,0)==WAIT_TIMEOUT&&lease&&lease->current();};
 ymcc::deckydiag::RecoveryBudgetFacts facts;
 const bool reserved=budgetfixture::deckySidebarReserveContextCleanup(request.identity.steam,
  ymcc::deckyupdate::managed::document(request.args.at("modeEpoch").template get<std::string>()),request.state.at("cleanupNativeHost").template get<std::string>(),current,request.args.value("reopenBigPicture",false),&facts);
 need(ports.lockDepth==0,"wire child serialized while original settings guard held");
 Json reply={{"ok",reserved},{"mutated",false},{"uncertain",false},{"requestDispatched",false},
  {"fixtureWorkerPid",GetCurrentProcessId()},{"fixtureGuardDepth",ports.lockDepth},{"fixtureSettingsWrites",ports.writes},{"fixtureRegistryReads",budgetWireRegistryReads.load()}};
 if(mode=="uncertain"||mode=="held-power"||mode=="held-off"){reply["ok"]=false;reply["mutated"]=true;reply["uncertain"]=true;}
 ymcc::deckydiag::attachRecoveryBudget(reply,facts);
 if(mode=="malformed-metadata")reply["recoveryBudget"]["source"]=Json::object();
 if(mode=="held-power"||mode=="held-off"){
  const auto name=ymcc::deckybinding::worker::wide(request.state.at("fixtureBudgetEntered"));
  ymcc::deckybinding::detail::Handle entered{OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str())};need(entered.value!=nullptr,"isolated budget child marker absent");SetEvent(entered.value);
  WaitForSingleObject(cancel,5000);reply["fixtureLateAfterCancel"]=true;
 }
 return reply;
}
static Json state(const ymcc::deckysidebar::State& value){return {{"enabled",value.enabled},{"pid",value.pid},{"revision",value.revision},{"phase",value.phase},{"reason",value.reason},{"steamKnown",value.steamKnown},{"steamRunning",value.steamRunning}};}
template<class State,class Stop>static Dependencies dependencies(const ymcc::steamsession::Session& own,State readState,Stop stopped){
 Dependencies deps;deps.session=[own]{return own;};deps.runtime=readState;deps.exiting=stopped;deps.installed=[]{return std::string(64,'a');};deps.loader=ymcc::deckyidentity::version;
 deps.mode=[](const auto&){return g_steamModeObserver.read();};deps.freshMode=deps.mode;deps.cleanupAllowed=[](const auto&){return true;};deps.consent=[](const auto&){return true;};
 deps.preflight=[]{throw std::runtime_error("parent must use original private cleanup path");return std::string{};};
 deps.dispatch=[](const auto&,const auto&,const auto&)->Json{throw std::runtime_error("no real Steam UI dispatch belongs to this fixture");};
 deps.now=[]{return GetTickCount64();};deps.powerGeneration=[]{return currentPowerGeneration();};deps.cleanupWorker=actualMainCleanupDispatch;return deps;
}
static void released(const Json& reply){need(reply.value("cleanupWorkerDestroyed",false)&&reply.value("cleanupWorkerHandles",99u)==0&&reply.value("cleanupWorkerProcesses",99u)==0,"actual private child resources not returned");}
static Json run(ymcc::steamsession::Session own,bool sourceReturnControls=false){
 Json mode={{"known",true},{"running",true},{"mode","bigpicture"},{"modeEpoch","budget-wire:1"},{"observedAccount",own.account},{"deckyAccountWatchAvailable",true}};fixtureOwnSteamContext(own,mode);
 const auto publish=[&]{g_steamSettingsObserver.value=fixtureSteamIdentity(own);g_steamSettingsObserver.identitySnapshot();};publish();
 g_steamModeObserver.read=[&]{return mode;};Json rows=Json::array();
 struct Reset{~Reset(){g_steamModeObserver.read={};g_deckySidebarRuntime.read={};g_steamSettingsObserver.withdraw();}}reset;
 if(!sourceReturnControls){
 for(const auto* scenario:{"reserved-only","reserve-refused","malformed-metadata"}){
  g_powerGeneration=11;Json native={{"enabled",true},{"pid",0u},{"revision",7u},{"phase","waiting-manual-steam"},{"reason","decky-context-already-loaded"},{"fixtureBudgetMode",scenario}};
  g_deckySidebarRuntime.read=[&]{return native;};auto deps=dependencies(own,[&]{return native;},[]{return false;});ManualRepair repair;
  const Json args={{"finalCleanup",true},{"pid",own.pid},{"birth",std::to_string(own.created)},{"revision",native.at("revision")},{"modeEpoch",mode.at("modeEpoch")}};
  const auto result=repair.request(args,deps,Origin::FinalCleanup);released(result);
  need(result.value("cleanupWorkerReplyReceived",false)&&result.value("fixtureWorkerPid",0u)!=0&&result.at("fixtureWorkerPid")!=own.pid,"metadata did not cross an authenticated actual child");
  need(result.value("fixtureGuardDepth",99u)==0&&result.value("fixtureRegistryReads",99u)==0,"child serialized under lock or independently read Steam registry");
  const bool accepted=std::string(scenario)!="reserve-refused";need(result.value("ok",!accepted)==accepted&&!result.value("mutated",true)&&!result.value("uncertain",true)&&!result.value("requestDispatched",true),"metadata changed original business reply");
  ymcc::steamfailure::FailureContext context;context.actionId=901;context.recoveryEpisode=3;context.steamPid=own.pid;context.steamBirth=own.created;context.powerGeneration=11;context.operation=ymcc::steamfailure::Operation::RebuildDocument;
  const auto logged=ymcc::deckydiag::failureRecord(result,context);need(logged.contains("recoveryBudget")==(std::string(scenario)!="malformed-metadata"),"wire malformed metadata changed more than optional projection");
  rows.push_back({{"kind",scenario},{"result",result},{"logged",logged}});
 }
 for(const auto* boundary:{"power","off"}){
  publish();g_powerGeneration=11;std::atomic<bool> stop{false},changed{false};
  const auto name=L"Local\\YMCC-Budget-Wire-"+std::to_wstring(own.pid)+L"-"+std::wstring(boundary,boundary+std::strlen(boundary));
  ymcc::deckybinding::detail::Handle entered{CreateEventW(nullptr,TRUE,FALSE,name.c_str())};need(entered.value!=nullptr,"budget cancellation marker unavailable");
  Json native={{"enabled",true},{"pid",0u},{"revision",7u},{"phase","waiting-manual-steam"},{"reason","decky-context-already-loaded"},{"fixtureBudgetMode",std::string("held-")+boundary},{"fixtureBudgetEntered",std::string(name.begin(),name.end())}};
  g_deckySidebarRuntime.read=[&]{return native;};auto deps=dependencies(own,[&]{return native;},[&]{return stop.load();});ManualRepair repair;
  const Json args={{"finalCleanup",true},{"pid",own.pid},{"birth",std::to_string(own.created)},{"revision",native.at("revision")},{"modeEpoch",mode.at("modeEpoch")}};
  std::thread revoke([&]{if(WaitForSingleObject(entered.value,2500)==WAIT_OBJECT_0){if(std::string(boundary)=="power")g_powerGeneration=12;else stop=true;changed=true;}});
  Json result;try{result=repair.request(args,deps,Origin::FinalCleanup);}catch(...){revoke.join();throw;}revoke.join();released(result);
  need(changed.load()&&!result.value("ok",true)&&result.value("uncertain",false)&&!result.contains("recoveryBudget"),"late revoked child certified recovery or accepted stale metadata");
  rows.push_back({{"kind",std::string("late-")+boundary},{"result",result},{"actualBoundaryChanged",true}});
 }
 }
 // Original Runtime creates the real FailureTrace. Its actual native callbacks
 // below use an in-memory log sink and the exact existing writer-power stamp.
 publish();g_powerGeneration=11;{std::lock_guard lock(traceMutex);traceRows=Json::array();finished=false;}
 ymcc::deckysidebar::Runtime runtime;std::atomic<bool> park{false},positive{false},stop{false};std::atomic<unsigned> steps{0},healthReads{0},loops{0};
 HANDLE worker=nullptr;DWORD workerId=0;Json receipt;
 ymcc::deckysidebar::Config config;config.executable=L"NEVER-LAUNCH.exe";config.home=L"NEVER-CREATE";
 config.preflight=[]{return std::string("fixture-no-loader");};config.launchAdmission=[]{return ymcc::deckysidebar::LaunchAdmission{false,"fixture-no-loader",false};};
 config.connectionHealth=[&]{++healthReads;ymcc::deckysidebar::ConnectionHealth health;health.required=true;health.paused=park.load();health.connected=positive.load();health.transportConnected=true;health.identity="isolated-budget-runtime";health.reason="fixture-official-unavailable";return health;};
 config.failureContext=actualFailureContext;config.failureDiagnostic=actualFailureDiagnostic;config.connectionRecovery=[&]{++loops;traceChanged.notify_all();};
 config.connectionStep=[&](auto action)->Json{
  need(action==ymcc::deckysidebar::ConnectionRecoveryWindow::Action::rebuild&&++steps==1,"metadata validation retried an uncertain UI action");
  need(DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&worker,THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0)!=FALSE,"exact Runtime observation handle unavailable");workerId=GetCurrentThreadId();
  g_deckySidebarRuntime.read=[&]{auto value=state(runtime.snapshot());value["fixtureBudgetMode"]="uncertain";return value;};
  auto deps=dependencies(own,[&]{return g_deckySidebarRuntime.read();},[&]{return stop.load();});ManualRepair repair;
  const auto native=runtime.snapshot();const Json args={{"finalCleanup",true},{"pid",own.pid},{"birth",std::to_string(own.created)},{"revision",native.revision},{"modeEpoch",mode.at("modeEpoch")}};
  receipt=repair.request(args,deps,Origin::FinalCleanup);released(receipt);need(receipt.contains("recoveryBudget")&&!receipt.value("ok",true)&&receipt.value("mutated",false)&&receipt.value("uncertain",false),"actual Runtime child lost uncertain reservation receipt");
  park=true;g_steamSettingsObserver.withdraw();g_powerGeneration=12;runtime.observeSteam(-1);return receipt;
 };
 runtime.initialize(std::move(config));runtime.observeSteam(1,own);need(runtime.setEnabled(true),"actual Runtime budget-wire owner did not start");
 bool complete=false;{std::unique_lock lock(traceMutex);complete=traceChanged.wait_for(lock,std::chrono::seconds(5),[]{return finished;});}
 if(!complete){stop=true;runtime.shutdown();if(worker)CloseHandle(worker);need(false,"actual Runtime budget receipt deadline");}
 const auto loopBefore=loops.load();FILETIME created{},exited{},kernel1{},user1{},kernel2{},user2{};
 need(GetThreadTimes(worker,&created,&exited,&kernel1,&user1)!=FALSE,"Runtime thread CPU observation unavailable");const auto began=GetTickCount64();Sleep(150);
 need(GetThreadTimes(worker,&created,&exited,&kernel2,&user2)!=FALSE,"Runtime thread CPU closing unavailable");
 const auto ticks=[](FILETIME value){return (static_cast<unsigned long long>(value.dwHighDateTime)<<32)|value.dwLowDateTime;};const auto cpu=ticks(kernel2)+ticks(user2)-ticks(kernel1)-ticks(user1);const auto elapsed=GetTickCount64()-began;
 need(steps==1&&loops.load()-loopBefore<=1,"withdrawn source spun or replayed a recovery action");
 const auto parkedLoopDelta=loops.load()-loopBefore;
 const auto beforeReads=healthReads.load();publish();g_powerGeneration=13;positive=true;park=false;runtime.observeSteam(1,own);
 const auto until=GetTickCount64()+ymcc::deckysidebar::ConnectionRecoveryWindow::evidenceIntervalMs;while(healthReads.load()==beforeReads&&GetTickCount64()<until)Sleep(1);
 need(healthReads.load()>beforeReads&&steps==1,"healthy shared source return replayed the old action or never reached original worker");
 unsigned duplicateEvents=0,offEvents=0;unsigned readsBeforeDuplicates=0,readsAfterDuplicates=0,readsAfterOff=0;
 if(sourceReturnControls){
  readsBeforeDuplicates=healthReads.load();
  for(unsigned n=0;n<1000;++n){publish();runtime.observeSteam(1,own);++duplicateEvents;}
  Sleep(150);readsAfterDuplicates=healthReads.load();
  need(readsAfterDuplicates==readsBeforeDuplicates&&steps==1,"same tuple publication revisions caused new health reads or recovery actions");
  runtime.setEnabled(false);const auto stoppedReads=healthReads.load();
  for(unsigned n=0;n<1000;++n){runtime.observeSteam(-1);runtime.observeSteam(1,own);++offEvents;}
  Sleep(150);readsAfterOff=healthReads.load();
  need(readsAfterOff==stoppedReads&&!runtime.snapshot().enabled&&runtime.snapshot().pid==0&&steps==1,"source return revived OFF owner or issued extra health work");
 }
 stop=true;runtime.shutdown();need(WaitForSingleObject(worker,0)==WAIT_OBJECT_0,"exact Runtime worker survived normal shutdown");CloseHandle(worker);
 Json selected=Json::array();{std::lock_guard lock(traceMutex);for(const auto& row:traceRows)if(row.value("actionOperation",std::string{})=="rebuild-ui-document"&&row.value("actionId",std::string{})!="0")selected.push_back(row);}
 need(selected.size()==2&&selected[0].at("actionPhase")=="started"&&selected[1].at("actionPhase")=="finished"&&selected[0].at("actionId")==selected[1].at("actionId"),"Runtime trace detached from its original single action");
 const auto& logged=selected[1];need(logged.at("steamPid")==own.pid&&logged.at("steamBirth")==std::to_string(own.created)&&logged.at("nativePowerGeneration")==11&&logged.at("writerPowerGeneration")==12,"late receipt was stamped with a later Steam/power observation");
 need(logged.at("recoveryBudget")==receipt.at("recoveryBudget")&&logged.at("ok")==false&&logged.at("mutated")==true&&logged.at("uncertain")==true,"reservation was logged as successful UI dispatch");
 need(runtime.snapshot().pid==0&&!runtime.snapshot().enabled&&budgetWireRegistryReads==0,"Runtime stop retained a process or independent registry query");
 rows.push_back({{"kind","actual-runtime-private-pipe-frozen-context"},{"result",receipt},{"trace",selected},{"steps",steps.load()},{"workerThreadId",workerId},{"workerExitObservedOnHeldHandle",true},{"parkObservationMs",elapsed},{"parkWorkerCpu100ns",cpu},{"counterGranularityLimited",true},{"parkLoopDelta",parkedLoopDelta},{"sharedHealthyReturnWithoutReplay",true},{"duplicatePositiveEvents",duplicateEvents},{"offReturnPairs",offEvents},{"readsBeforeDuplicates",readsBeforeDuplicates},{"readsAfterDuplicates",readsAfterDuplicates},{"readsAfterOff",readsAfterOff}});
 return {{"kind",sourceReturnControls?"source-return-controls-only":"recovery-budget-wire-only"},{"rows",rows},{"directProductRegistryReads",budgetWireRegistryReads.load()},{"realSettingsWrites",0},{"realSteamActions",0},{"scope","Actual original child channel/shared lease/ManualRepair/Runtime/trace; settings, image/build store and UI IO ports inert. Source power/health are explicit test facts, not physical sleep/Steam acceptance."}};
}
}
