inline Json initialUpdateBackend(const std::filesystem::path& output,const std::string& variant){
 namespace actual=offlineConsumer;
 using Window=ymcc::deckysidebar::ConnectionRecoveryWindow;
 using Action=Window::Action;
 Controls controls(L"runtime-success-checkpoint");Context context;
 ContextJob contextOwner(worker::number(context.lease.at("pid")));
 const auto beforeHandles=handles();
 const auto installed=std::string(64,'d'),oldBuild=std::string(64,'a');
 const auto loader=std::string(ymcc::deckyidentity::version);
 const auto plugin=std::filesystem::path(actual::POWER_CONTROL_DIR)/L"decky/plugins/ymcc-sidebar/dist/index.js";
 std::filesystem::create_directories(plugin.parent_path());
 {std::ofstream file(plugin,std::ios::binary);file<<"// YMCC_DECKY_FRONTEND_BUILD_ID="<<installed<<'\n';}
 need(ymcc::deckymirror::frontendBuildId(plugin.wstring())==installed,"initial-update-real-frontend-id-read");
 actual::observedSession={};actual::observedSession.pid=worker::number(context.lease.at("pid"));
 actual::observedSession.created=worker::decimal(context.lease.at("birth"));actual::observedSession.root=output.wstring();actual::observedSession.account=12345678;
 actual::observedMode={{"known",true},{"running",true},{"mode","bigpicture"},{"pid",actual::observedSession.pid},
  {"birth",std::to_string(actual::observedSession.created)},{"deckyFrontendLoaded",true},
  {"deckyBackendConnected",false},{"deckyBackendState","socket-connecting"},{"deckyHooksInstalled",true},
  {"deckyClientConnected",false},{"deckyClientChecking",false},{"deckyClientVerified",false},
  {"deckyUpdateResidue",{{"protocol",1},{"active",true},{"bridgePresent",true},{"frontendBuildId",oldBuild},{"loaderVersion",loader}}}};
 ymcc::deckyupdate::Recovery recovery;
 const auto grant=recovery.consider(actual::observedSession,actual::observedMode,installed,true,loader);
 need(grant=="updated-decky-frontend-pending","initial-update-original-grant-required");
 actual::initialUpdateRecovery=&recovery;actual::retainedConsent=false;actual::g_deckyMirrorBroker.connected=false;
 actual::fixedTick=0;actual::realModeObserver=nullptr;
 std::mutex gate;std::atomic<unsigned> admissions{0},rebuilds{0},reopens{0};
 std::atomic<bool> entered{false};Json admissionTrace=Json::array();
 auto admission=actual::actualAdmission();auto health=actual::actualConnectionHealth();
 auto readAdmission=[&]{std::lock_guard lock(gate);auto result=admission();++admissions;
  if(admissionTrace.size()<12)admissionTrace.push_back({{"allowed",result.allowed},{"retryable",result.retryable},{"bindingOnly",result.bindingOnly},{"reason",result.reason}});
  entered=true;return result;};
 auto readHealth=[&]{std::lock_guard lock(gate);return health();};
 const auto step=[&](Action action){if(action==Action::rebuild)++rebuilds;if(action==Action::reopen)++reopens;
  return Json{{"ok",false},{"mutated",false},{"uncertain",false},{"reason","inert-step-refused"}};};
 Harness h(output,context,true,readAdmission,[&](Json&,unsigned){std::lock_guard lock(gate);
  actual::g_deckyMirrorBroker.connected=true;actual::observedMode["deckyClientConnected"]=true;
  actual::observedMode["deckyClientVerified"]=true;actual::observedMode["deckyUpdateResidue"]["frontendBuildId"]=installed;
 },{},{},{},readHealth,step);
 actual::runtime=&h.runtime;
 const auto began=GetTickCount64();need(h.runtime.setEnabled(true),"initial-update-enable");
 need(waitFor([&]{return entered.load();},1000),"initial-update-original-admission-entered");
 Json boundaries=Json::array();bool recovered=false,offAccepted=false,deadlineObserved=false;
 if(variant=="ready"){
  Sleep(500);
  {std::lock_guard lock(gate);actual::observedMode["deckyBackendConnected"]=true;actual::observedMode["deckyBackendState"]="socket-open";}
  h.runtime.requestLaunchAdmissionRefresh();h.runtime.requestConnectionCheck();
  recovered=waitFor([&]{return h.healthyGeneration==1&&h.runtime.snapshot().phase=="frontend-attached";},2000);
  h.runtime.requestConnectionCheck();waitFor([&]{return h.globalConnected.load()>0;},1000);
 }else if(variant=="deadline"){
  deadlineObserved=waitFor([&]{return rebuilds.load()>0;},Window::transportGraceMs+2000);
 }else if(variant=="off"){
  Sleep(100);offAccepted=h.runtime.setEnabled(false);
  offAccepted=offAccepted&&waitFor([&]{return h.runtime.snapshot().phase=="disabled";},1000)&&!h.runtime.requestConnectionCheck();
 }else{
  // Actual main consumer and actual Recovery; mutate only supplied boundary facts.
  std::lock_guard lock(gate);const auto originalMode=actual::observedMode;const auto originalSession=actual::observedSession;
  const auto check=[&](const char* name,bool expectedPending,bool expectedAllowed=false,bool expectedBindingOnly=false,
   const char* expectedReason=nullptr,unsigned expectedReads=1,unsigned repeat=1){
   const auto before=ymcc::deckymirror::fixtureFrontendBuildReads;
   bool verdict=true;ymcc::deckysidebar::LaunchAdmission result;
   for(unsigned i=0;i<repeat;++i){result=admission();
    const bool pending=!result.allowed&&result.retryable&&result.reason=="decky-frontend-update-backend-pending";
    verdict&=pending==expectedPending&&result.allowed==expectedAllowed&&result.bindingOnly==expectedBindingOnly&&
     (!expectedReason||result.reason==expectedReason);
   }
   const auto reads=ymcc::deckymirror::fixtureFrontendBuildReads-before;
   boundaries.push_back({{"name",name},{"passed",verdict&&reads==expectedReads},{"originalVerdictPreserved",verdict},
    {"reads",reads},{"expectedReads",expectedReads},{"admissions",repeat},{"allowed",result.allowed},{"retryable",result.retryable},{"reason",result.reason}});};
  check("known-update-socket-opening",true);
  actual::observedMode["deckyBackendState"]="socket-closed";check("closed-is-not-opening",false);actual::observedMode=originalMode;
  actual::observedMode.erase("deckyBackendState");check("unknown-is-not-opening",false);actual::observedMode=originalMode;
  constexpr auto blocked="decky-context-already-loaded";
  actual::observedMode["deckyUpdateResidue"]["loaderVersion"]="v3.0.0";check("different-loader",false,false,false,blocked,0,32);actual::observedMode=originalMode;
  actual::observedMode["deckyUpdateResidue"]["active"]=false;check("invalid-residue",false,false,false,blocked,0,32);actual::observedMode=originalMode;
  actual::observedMode.erase("deckyUpdateResidue");check("missing-residue",false,false,false,blocked,0);actual::observedMode=originalMode;
  actual::observedMode["deckyUpdateResidue"].erase("loaderVersion");check("legacy-residue-without-loader",false,false,false,blocked,0);actual::observedMode=originalMode;
  actual::observedMode["deckyUpdateResidue"]["loaderVersion"]=1;check("nonstring-loader-version",false,false,false,blocked,0);actual::observedMode=originalMode;
  actual::observedMode["deckyUpdateResidue"]["frontendBuildId"]="invalid-id";check("malformed-build-proof",false,false,false,blocked,0);actual::observedMode=originalMode;
  ++actual::observedSession.created;actual::observedMode["birth"]=std::to_string(actual::observedSession.created);check("new-owner-without-update-grant",false);actual::observedSession=originalSession;actual::observedMode=originalMode;
  actual::observedMode["deckyUpdateResidue"]["frontendBuildId"]=installed;check("same-build-retains-original-attachment",false,true,true);actual::observedMode=originalMode;
  {std::ofstream file(plugin,std::ios::binary|std::ios::trunc);file<<"// YMCC_DECKY_FRONTEND_BUILD_ID="<<oldBuild<<'\n';}
  check("rewritten-valid-build-still-fresh",false,true,true,"retained-frontend-rebind");
  {std::ofstream file(plugin,std::ios::binary|std::ios::trunc);file<<"// YMCC_DECKY_FRONTEND_BUILD_ID="<<installed<<'\n';}
  check("restored-build-still-fresh",true,false,false,"decky-frontend-update-backend-pending");
  actual::observedMode["deckyBackendConnected"]=true;check("open-backend-retains-original-update-grant",false,true,true);actual::observedMode=originalMode;
  actual::observedMode["deckyFrontendLoaded"]=false;check("fresh-document-no-version-reads",false,true,false,"",0,64);actual::observedMode=originalMode;
  actual::observedMode.erase("deckyFrontendLoaded");check("absent-load-state-no-version-read",false,false,false,"decky-context-state-pending",0);actual::observedMode=originalMode;
  actual::observedMode["deckyFrontendLoaded"]="true";check("string-load-state-no-version-read",false,false,false,"decky-context-state-pending",0);actual::observedMode=originalMode;
  actual::observedMode["deckyFrontendLoaded"]=1;check("numeric-load-state-no-version-read",false,false,false,"decky-context-state-pending",0);actual::observedMode=originalMode;
  recovery.veto();check("explicit-veto",false);
 }
 const auto elapsed=GetTickCount64()-began;
 h.runtime.shutdown();const auto returned=h.client->snapshot();
 actual::runtime=nullptr;actual::initialUpdateRecovery=nullptr;
 const auto afterHandles=handles();
 const bool resources=returned.fullyDestroyed&&returned.handles==0&&returned.pendingIo==0&&beforeHandles==afterHandles&&context.alive();
 const bool behavior=variant=="ready"?recovered&&h.plans==1&&rebuilds==0&&reopens==0:
  variant=="deadline"?deadlineObserved&&elapsed>=Window::transportGraceMs-500&&elapsed<=Window::transportGraceMs+2000:
  variant=="off"?offAccepted&&h.plans==0&&rebuilds==0&&reopens==0:
  boundaries.size()==19&&std::all_of(boundaries.begin(),boundaries.end(),[](const Json& row){return row.value("passed",false);});
 Json result{{"passed",resources&&behavior},{"variant",variant},{"beforeHandles",beforeHandles},{"afterHandles",afterHandles},
  {"actualRuntime",true},{"actualMainAdmissionAndHealth",true},{"actualRecovery",true},{"existingUpdateGrant",grant},
  {"elapsedMs",elapsed},{"recovered",recovered},{"offAccepted",offAccepted},{"deadlineObserved",deadlineObserved},
  {"plans",h.plans.load()},{"rebuilds",rebuilds.load()},{"reopens",reopens.load()},
  {"atExit",state(returned)},{"contextAlive",context.alive()},{"admissionTrace",admissionTrace},{"globalRecoveryLog",h.globalRecoveryLog},
  {"boundaries",boundaries},{"realSteamOrDeviceActions",0},{"originalTransportGraceMs",Window::transportGraceMs}};
 if(!behavior)result["error"]="initial-backend-opening-must-not-rebuild-before-original-window";
 std::cout<<"INITIAL_UPDATE_FACTS "<<result.dump()<<'\n'<<std::flush;
 return result;
}
inline Json retainedOfficialBackend(const std::filesystem::path& output,const std::string& variant){
 namespace actual=offlineConsumer;
 using Window=ymcc::deckysidebar::ConnectionRecoveryWindow;using Action=Window::Action;
 Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
 const auto baseline=handles();const auto installed=std::string(64,'d');
 const auto plugin=std::filesystem::path(actual::POWER_CONTROL_DIR)/L"decky/plugins/ymcc-sidebar/dist/index.js";
 std::filesystem::create_directories(plugin.parent_path());{std::ofstream file(plugin,std::ios::binary);file<<"// YMCC_DECKY_FRONTEND_BUILD_ID="<<installed<<'\n';}
 actual::observedSession={};actual::observedSession.pid=worker::number(context.lease.at("pid"));
 actual::observedSession.created=worker::decimal(context.lease.at("birth"));actual::observedSession.root=output.wstring();actual::observedSession.account=12345678;
 actual::observedMode={{"known",true},{"running",true},{"mode","bigpicture"},{"modeEpoch","retained-backend:1"},
  {"pid",actual::observedSession.pid},{"birth",std::to_string(actual::observedSession.created)},
  {"deckyFrontendLoaded",true},{"deckyBackendConnected",false},{"deckyBackendState","socket-connecting"},
  {"deckyHooksInstalled",true},{"deckyClientConnected",false},{"deckyClientChecking",false},{"deckyClientVerified",false},
  {"deckyUpdateResidue",{{"protocol",1},{"active",true},{"bridgePresent",true},{"frontendBuildId",installed},{"loaderVersion",ymcc::deckyidentity::version}}}};
 ymcc::deckyupdate::Recovery recovery;actual::initialUpdateRecovery=&recovery;actual::retainedConsent=false;
 actual::fixedTick=0;actual::fixturePower=1;actual::realModeObserver=nullptr;actual::g_deckyMirrorBroker.connected=false;
 need(!recovery.allows(actual::observedSession),"same-build-fixture-must-not-invent-update-or-managed-grant");
 std::mutex gate;std::atomic<unsigned> rebuilds{0},reopens{0},healthReads{0};std::atomic<bool> pendingSeen{false},lastHealthConnected{false};
 auto admission=actual::actualAdmission();auto health=actual::actualConnectionHealth();Json healthRows=Json::array();
 const auto readHealth=[&]{std::lock_guard lock(gate);auto v=health();++healthReads;lastHealthConnected=v.connected&&!v.paused;
  if(v.reason=="official-backend-reconnect-pending")pendingSeen=true;
  if(healthRows.size()<40)healthRows.push_back({{"connected",v.connected},{"paused",v.paused},{"reason",v.reason},{"backendState",actual::observedMode.value("deckyBackendState",std::string{})}});
  return v;};
 Harness* currentHarness=nullptr;std::atomic<unsigned> plansAtFirstPage{0};
 const bool sidebarOnly=variant=="backend-outage-sidebar-only";
 const auto step=[&](Action a){
   if(a==Action::rebuild){if(currentHarness&&rebuilds==0)plansAtFirstPage=currentHarness->plans.load();++rebuilds;std::lock_guard lock(gate);actual::observedMode["deckyBackendConnected"]=!sidebarOnly;actual::observedMode["deckyBackendState"]=sidebarOnly?"socket-connecting":"socket-open";
    if(sidebarOnly)actual::observedMode["modeEpoch"]="retained-backend-rebuilt:1";
   return Json{{"ok",true},{"mutated",true},{"uncertain",false},{"reason","fixture-original-page-dispatch-ack"}};}
  if(a==Action::reopen)++reopens;
  return Json{{"ok",false},{"mutated",false},{"uncertain",false},{"reason","fixture-unexpected-step-refused"}};
 };
 const bool boundaries=variant=="backend-boundaries";
 std::function<ymcc::deckysidebar::ConnectionHealth()> healthCallback=boundaries?std::function<ymcc::deckysidebar::ConnectionHealth()>{}:readHealth;
 Harness h(output,context,true,[&]{std::lock_guard lock(gate);return admission();},[&](Json&,unsigned){std::lock_guard lock(gate);
  actual::g_deckyMirrorBroker.connected=true;actual::observedMode["deckyClientConnected"]=true;actual::observedMode["deckyClientVerified"]=true;
 },{},{},{},healthCallback,step);
 currentHarness=&h;actual::runtime=&h.runtime;
 const auto began=GetTickCount64();need(h.runtime.setEnabled(true),"backend-retained-enable");
 need(waitFor([&]{return h.healthyGeneration.load()>=1&&h.runtime.snapshot().phase=="frontend-attached";},3000),"backend-retained-real-binding");
 const auto publish=[] {++actual::g_steamModeObserver.controlledPublication;
  actual::g_steamModeObserver.controlledObservationTick=actual::fixedTick?actual::fixedTick.load():GetTickCount64();};
 Json checks=Json::array();bool behavior=false,settled=false;ULONGLONG faultAt=0;
 if(boundaries){
  std::lock_guard lock(gate);actual::fixedTick=100000;
  const auto check=[&](const char* name,bool connected,bool paused){const auto v=health();
   checks.push_back({{"name",name},{"passed",v.connected==connected&&v.paused==paused},{"connected",v.connected},{"paused",v.paused},{"reason",v.reason}});};
  check("initial-connecting-preserves-verified-mirror",true,false);
  actual::observedMode["deckyBackendState"]="socket-closed";publish();check("explicit-closed-starts-original-grace",false,true);
  for(unsigned n=0;n<1000;++n)(void)health();
  actual::fixedTick=106999;check("duplicates-never-postpone-original-grace",false,true);
  actual::fixedTick=107000;check("same-cached-close-cannot-certify-persistent-backend-fault",false,true);
  const auto requests=actual::refreshes;
  const auto duplicate=health();Window duplicateWindow;
  checks.push_back({{"name","cached-close-waits-for-existing-read-without-spending-step"},
   {"passed",duplicateWindow.observe(actual::fixedTick,duplicate)==Action::none&&duplicate.pausedReadDue>actual::fixedTick&&
    duplicateWindow.timeout(actual::fixedTick)==duplicate.pausedReadDue-actual::fixedTick&&actual::refreshes==requests}});
  actual::g_deckyMirrorBroker.connected=false;
  check("mirror-loss-still-enters-normal-recovery",false,false);
  actual::g_deckyMirrorBroker.connected=true;actual::observedMode["deckyClientConnected"]=false;
  check("client-loss-still-enters-normal-recovery",false,false);
  actual::observedMode["deckyClientConnected"]=true;
  actual::observedMode["deckyBackendState"]="socket-connecting";publish();
  actual::observedMode["modeEpoch"]="retained-backend:2";actual::fixedTick=107000;check("same-document-entry-keeps-failure",false,false);
  actual::observedMode["deckyBackendConnected"]=true;actual::observedMode["deckyBackendState"]="socket-open";check("actual-backend-connection-clears",true,false);
  actual::observedMode["deckyBackendConnected"]=false;actual::observedMode["deckyBackendState"]="socket-connecting";check("post-success-connecting-is-not-new-fault",true,false);
  actual::observedMode["deckyBackendState"]="socket-closed";check("second-fault-gets-original-grace",false,true);
  actual::observedMode["modeEpoch"]="new-backend-document:1";actual::observedMode["deckyBackendState"]="socket-connecting";check("new-document-does-not-inherit-fault",true,false);
  actual::observedMode.erase("deckyBackendConnected");actual::observedMode["deckyBackendState"]="socket-closed";check("unknown-closed-does-not-create-fault",true,false);
  actual::observedMode["deckyBackendConnected"]=false;check("fresh-known-closed-observed",false,true);
  actual::g_powerLifecycle=actual::PowerLifecycle::Suspended;const auto suspended=health();
  checks.push_back({{"name","suspend-never-grants-action"},{"passed",!suspended.required&&!suspended.connected}});
  actual::g_powerLifecycle=actual::PowerLifecycle::Ready;actual::observedMode["deckyBackendState"]="socket-connecting";check("resume-starts-without-old-fault",true,false);
  ++actual::fixturePower;actual::observedMode["deckyBackendState"]="socket-closed";check("new-power-generation-is-fresh",false,true);
  actual::fixedTick=106000;check("clock-regression-does-not-underflow",false,true);
  actual::fixedTick=114001;actual::observedMode["modeEpoch"]="";actual::observedMode.erase("deckyBackendConnected");
  const auto emptyExpired=health();const auto readsBefore=actual::refreshes;
  for(unsigned n=0;n<1000;++n)(void)health();
  checks.push_back({{"name","empty-expired-is-unknown-and-no-fast-spin"},{"passed",emptyExpired.paused&&!emptyExpired.connected&&emptyExpired.reason=="official-backend-observation-unconfirmed"&&actual::refreshes==readsBefore}});
  actual::fixedTick+=30000;const auto emptyLate=health();
  checks.push_back({{"name","unknown-uses-original-idle-cadence"},{"passed",emptyLate.paused&&!emptyLate.connected&&actual::refreshes==readsBefore+1}});
  const auto idleAt=actual::fixedTick.load();
  const auto idleReads=actual::refreshes;
  Window openingWatch;openingWatch.observe(idleAt,emptyLate);
  actual::fixedTick=idleAt+1;actual::observedMode["deckyClientChecking"]=true;
  const auto openingHealth=health();openingWatch.observe(actual::fixedTick,openingHealth);
  checks.push_back({{"name","opening-event-keeps-fast-check"},{"passed",openingHealth.paused&&openingWatch.timeout(actual::fixedTick)<=Window::evidenceIntervalMs}});
  actual::observedMode["deckyClientChecking"]=false;
  Window idleWatch;idleWatch.observe(idleAt,emptyLate);unsigned idleWakes=0;
  for(DWORD offset=Window::evidenceIntervalMs;offset<Window::intervalMs;offset+=Window::evidenceIntervalMs){
   actual::fixedTick=idleAt+offset;
   if(idleWatch.timeout(actual::fixedTick)==0){++idleWakes;idleWatch.observe(actual::fixedTick,health());}
  }
  actual::fixedTick=idleAt+Window::intervalMs;
  checks.push_back({{"name","expired-unknown-skips-cache-only-wakes"},{"passed",!idleWakes&&idleWatch.timeout(actual::fixedTick)==0&&actual::refreshes==idleReads},{"cacheOnlyWakes",idleWakes}});
  actual::g_steamModeObserver.refusePassiveRefresh=true;
  const auto refused=health();idleWatch.observe(actual::fixedTick,refused);
  actual::g_steamModeObserver.refusePassiveRefresh=false;
  checks.push_back({{"name","refused-shared-renewal-rechecks-fast"},{"passed",refused.paused&&idleWatch.timeout(actual::fixedTick)==Window::evidenceIntervalMs}});
  // Backend visibility may independently disappear while the authenticated
  // broker peer survives and the actual frontend reports it is disconnected.
  {
   const auto savedMode=actual::observedMode;const auto savedTick=actual::fixedTick.load();
   auto lossHealth=actual::actualConnectionHealth();
   actual::fixedTick=500000;actual::observedMode["modeEpoch"]="independent-loss:1";
   actual::observedMode["deckyBackendConnected"]=false;actual::observedMode["deckyBackendState"]="socket-closed";
   publish();const auto grace=lossHealth();
   actual::fixedTick+=Window::transportGraceMs;actual::observedMode.erase("deckyBackendConnected");
   actual::observedMode["deckyBackendState"]="socket-unavailable";publish();const auto uncertain=lossHealth();
   actual::observedMode["deckyClientConnected"]=false;actual::observedMode["deckyClientVerified"]=false;
   publish();const auto lost=lossHealth();Window lossWindow;
   const auto action=lossWindow.observe(actual::fixedTick,lost);
   checks.push_back({{"name","known-client-loss-escapes-expired-backend-uncertainty"},
     {"passed",grace.paused&&uncertain.paused&&lost.required&&!lost.connected&&!lost.paused&&lost.transportConnected&&
       lost.reason=="frontend-client-disconnected"&&action==Action::recycle},
     {"paused",lost.paused},{"recycles",action==Action::recycle},{"reason",lost.reason}});
   actual::observedMode["deckyClientChecking"]=true;publish();const auto checking=lossHealth();
   actual::observedMode["deckyClientChecking"]=false;actual::observedMode.erase("deckyClientConnected");
   publish();const auto unknownClient=lossHealth();
   checks.push_back({{"name","opening-and-missing-client-do-not-become-definite-loss"},
     {"passed",checking.paused&&!checking.connected&&unknownClient.paused&&!unknownClient.connected}});
   actual::observedMode=savedMode;actual::fixedTick=savedTick;
  }
  actual::observedMode["modeEpoch"]="new-backend-document:1";actual::observedMode["deckyBackendConnected"]=false;publish();
  check("proof-return-does-not-refill-expired-window",false,false);
  actual::observedMode["modeEpoch"]="";actual::observedMode["deckyBackendConnected"]=true;check("positive-backend-clears-even-with-empty-doc",true,false);
  actual::observedMode["deckyBackendConnected"]=false;actual::observedMode["deckyBackendState"]="socket-closed";actual::observedMode["modeEpoch"]="new-backend-document:1";
  check("new-known-fault-after-success",false,true);++actual::fixturePower;actual::observedMode["modeEpoch"]="";actual::observedMode["deckyBackendState"]="socket-connecting";
  check("empty-document-new-epoch-does-not-inherit-fault",true,false);
  // The completed-unknown cache below is the public observer's failure
  // projection, not an in-flight cache miss or a pending opening handshake.
  auto modeHealth=actual::actualConnectionHealth();
  const auto usable=actual::observedMode;
  constexpr ULONGLONG unconfirmedAt=200000;
  actual::fixedTick=unconfirmedAt;
  actual::observedMode={{"known",true},{"running",true},{"mode","unknown"},
    {"pid",actual::observedSession.pid},{"birth",std::to_string(actual::observedSession.created)},{"reason","live-busy"}};
  const auto firstUnknown=modeHealth();
  Window modeWindow;unsigned unconfirmedWakes=0,unsafeActions=0;
  const auto observeMode=[&](const auto& value){
   if(modeWindow.observe(actual::fixedTick,value)!=Action::none)++unsafeActions;
  };
  const auto acceptedUnknown=actual::refreshes;
  observeMode(firstUnknown);
  for(DWORD offset=Window::evidenceIntervalMs;offset<Window::intervalMs;offset+=Window::evidenceIntervalMs){
   actual::fixedTick=unconfirmedAt+offset;
   if(modeWindow.timeout(actual::fixedTick)==0){++unconfirmedWakes;observeMode(modeHealth());}
  }
  actual::fixedTick=unconfirmedAt+Window::intervalMs;
  checks.push_back({{"name","completed-unknown-mode-skips-cache-only-wakes"},{"passed",firstUnknown.paused&&!firstUnknown.required&&
   !unsafeActions&&!unconfirmedWakes&&actual::refreshes==acceptedUnknown&&modeWindow.timeout(actual::fixedTick)==0},{"cacheOnlyWakes",unconfirmedWakes}});
  actual::g_steamModeObserver.refusePassiveRefresh=true;
  observeMode(modeHealth());actual::g_steamModeObserver.refusePassiveRefresh=false;
  checks.push_back({{"name","completed-unknown-refusal-keeps-fast-check"},{"passed",!unsafeActions&&modeWindow.timeout(actual::fixedTick)==Window::evidenceIntervalMs}});
  const auto seed=actual::observedMode;
  const auto scopeCheck=[&](const char* name,auto change){
   auto sameScopeHealth=actual::actualConnectionHealth();actual::observedMode=seed;
   const auto session=actual::observedSession;const auto power=actual::fixturePower.load();
   (void)sameScopeHealth();change();++actual::fixedTick;
   const auto next=sameScopeHealth();Window window;window.observe(actual::fixedTick,next);
   checks.push_back({{"name",name},{"passed",next.paused&&!next.required&&window.timeout(actual::fixedTick)==Window::evidenceIntervalMs}});
   actual::observedSession=session;actual::fixturePower=power;
  };
  scopeCheck("inflight-mode-keeps-fast-check",[&]{actual::observedMode["reason"]="shared-mode-pending";});
  scopeCheck("other-completed-mode-failures-keep-fast-check",[&]{actual::observedMode["reason"]="steam-session-unconfirmed";});
  scopeCheck("new-power-rejects-old-mode-refresh",[&]{++actual::fixturePower;});
  scopeCheck("new-steam-account-rejects-old-mode-refresh",[&]{++actual::observedSession.account;});
  actual::observedMode=usable;actual::observedMode["deckyBackendConnected"]=true;
  actual::observedMode["deckyBackendState"]="socket-open";
  const auto returned=modeHealth();
  checks.push_back({{"name","fresh-mode-health-eligible-before-timer"},{"passed",returned.required&&returned.connected&&!returned.paused}});
  actual::observedMode["modeEpoch"]="distinct-boundary-doc:1";
  actual::observedMode["deckyBackendConnected"]=false;actual::observedMode["deckyBackendState"]="socket-closed";publish();
  (void)health();actual::fixedTick+=Window::transportGraceMs;publish();
  check("distinct-identical-close-confirms-outage",false,false);
  ++actual::observedSession.account;publish();
  check("new-steam-account-does-not-inherit-confirmed-outage",false,true);
  actual::observedMode["modeEpoch"]="grace-observation:1";actual::fixedTick=900000;publish();
  auto agedHealth=actual::actualConnectionHealth();(void)agedHealth();
  actual::observedMode["deckyBackendState"]="socket-connecting";
  actual::fixedTick+=Window::transportGraceMs-1;publish();
  const auto early=agedHealth();++actual::fixedTick;const auto expired=agedHealth();
  Window held;const auto action=held.observe(actual::fixedTick,expired);
  checks.push_back({{"name","pre-grace-observation-cannot-certify-persistent-fault"},
    {"passed",early.paused&&expired.paused&&!expired.connected&&action==Action::none},
    {"reason",expired.reason},{"wouldRecycle",action==Action::recycle}});
  publish();const auto fresh=agedHealth();Window confirmed;
  checks.push_back({{"name","post-grace-observation-confirms-fault"},
    {"passed",!fresh.paused&&!fresh.connected&&confirmed.observe(actual::fixedTick,fresh)==Action::recycle}});
  behavior=checks.size()==40&&std::all_of(checks.begin(),checks.end(),[](const Json& r){return r.value("passed",false);});
 }else{
  h.runtime.requestConnectionCheck();need(waitFor([&]{return lastHealthConnected.load();},1500),"initial-connecting-native-ready-must-be-usable");
   const bool checkTerminal=variant=="backend-outage"||sidebarOnly;
   if(checkTerminal)need(waitFor([&]{return h.globalConnected.load()>0;},1500),"initial-recovery-terminal-observed");
   const auto beforeFaultTerminals=h.globalConnected.load();
  {std::lock_guard lock(gate);actual::observedMode["deckyBackendState"]="socket-closed";publish();}
  const auto oldReads=healthReads.load();faultAt=GetTickCount64();h.runtime.requestConnectionCheck();
  need(waitFor([&]{return healthReads.load()>oldReads;},1000),"backend-closed-check-consumed");
  {std::lock_guard lock(gate);actual::observedMode["deckyBackendState"]="socket-connecting";publish();}
  if(variant=="backend-recovers"){
   {std::lock_guard lock(gate);actual::observedMode["deckyBackendConnected"]=true;actual::observedMode["deckyBackendState"]="socket-open";}
   const auto prior=healthReads.load();h.runtime.requestConnectionCheck();settled=waitFor([&]{return healthReads.load()>prior;},1000);
   behavior=settled&&pendingSeen.load()&&rebuilds==0&&reopens==0;
  }else if(variant=="backend-off"){
   const bool off=h.runtime.setEnabled(false);settled=waitFor([&]{return h.client->snapshot().fullyDestroyed;},1500);
   const bool enabled=h.runtime.setEnabled(true);
   const bool rebound=waitFor([&]{return h.healthyGeneration.load()>=2&&h.runtime.snapshot().phase=="frontend-attached";},2000);
   const auto resumed=readHealth();
   behavior=off&&settled&&enabled&&rebound&&resumed.connected&&!resumed.paused&&pendingSeen.load()&&rebuilds==0&&reopens==0;
  }else{
   need(variant=="backend-outage"||sidebarOnly,"unknown-backend-parameter");
   const auto graceObserved=GetTickCount64();
   need(waitFor([&]{return GetTickCount64()-graceObserved>=Window::transportGraceMs;},Window::transportGraceMs+1000),"backend-grace-elapsed");
   {std::lock_guard lock(gate);publish();}h.runtime.requestConnectionCheck();
   settled=waitFor([&]{return rebuilds.load()>0;},Window::transportGraceMs+Window::evidenceIntervalMs+2000);
   if(settled){h.runtime.requestLaunchAdmissionRefresh();h.runtime.requestConnectionCheck();
    settled=waitFor([&]{return h.healthyGeneration.load()>=2&&h.runtime.snapshot().phase=="frontend-attached";},2000);}
   behavior=settled&&pendingSeen.load()&&rebuilds==1&&reopens==0&&plansAtFirstPage==1;
   if(behavior){h.runtime.requestConnectionCheck();
    behavior=waitFor([&]{return h.globalConnected.load()>beforeFaultTerminals;},1500);}
  }
 }
 h.runtime.shutdown();const auto returned=h.client->snapshot();actual::runtime=nullptr;actual::initialUpdateRecovery=nullptr;actual::fixedTick=0;
 const auto after=handles();const bool resources=returned.fullyDestroyed&&returned.handles==0&&returned.pendingIo==0&&after==baseline&&context.alive();
 Json terminalRows=Json::array();bool recoveryStagePassed=true;
 if(variant=="backend-outage"||sidebarOnly){
  unsigned long long pageEpisode=0;
  for(const auto& row:h.globalRecoveryLog)if(row.value("stage",std::string{})=="rebuild-ui-document")
   pageEpisode=row.value("recoveryEpisode",0ULL);
  for(const auto& row:h.globalRecoveryLog)if(pageEpisode&&row.value("recoveryEpisode",0ULL)==pageEpisode&&row.value("recoveryPhase",std::string{})=="connected")
   terminalRows.push_back(ymcc::deckydiag::record("connection-recovery",row));
  recoveryStagePassed=terminalRows.size()==1&&terminalRows[0].value("connected",false)&&
   terminalRows[0].value("stage",std::string{})==(sidebarOnly?"decky-sidebar-only":"decky-channels")&&
   terminalRows[0].value("reason",std::string{})==(sidebarOnly?"sidebar-native-confirmed-official-backend-unavailable":"decky-channels-connected");
 }
 Json result{{"passed",behavior&&resources&&recoveryStagePassed},{"variant",variant},{"actualRuntime",true},{"actualMainAdmissionAndHealth",true},{"actualRecovery",true},
  {"managedContinuationGranted",false},{"updateGrantGranted",false},{"sameInstalledFrontend",true},{"elapsedMs",GetTickCount64()-began},
  {"faultObservationMs",faultAt?GetTickCount64()-faultAt:0},{"pendingSeen",pendingSeen.load()},{"settled",settled},
  {"plansBeforeFirstPageOperation",plansAtFirstPage.load()},{"rebuilds",rebuilds.load()},{"reopens",reopens.load()},{"plans",h.plans.load()},{"beforeHandles",baseline},{"afterHandles",after},
  {"atExit",state(returned)},{"contextAlive",context.alive()},{"globalRecoveryLog",h.globalRecoveryLog},{"healthObservations",healthRows},
  {"boundaries",checks},{"recoveryStagePassed",recoveryStagePassed},{"recoveryTerminalRows",terminalRows},
  {"uiAndOfficialBackendFactsAreControlledBoundaryPorts",true},{"pageOperationIsControlledDispatchReceipt",true},{"realSteamOrDeviceActions",0}};
 if(!behavior)result["error"]=variant=="backend-outage"&&plansAtFirstPage!=1?"official-only-outage-rebound-healthy-retained-transport":"retained-mirror-must-not-hide-confirmed-official-backend-outage";
 else if(!recoveryStagePassed)result["error"]="recovery-log-must-distinguish-sidebar-only-from-both-channels";
 return result;
}

// Parameter of the original typed-ready/rebind case. The shared reader and
// Native stable callback are real; UI facts and the page dispatch stay inert.
inline Json retainedBackendNotification(const std::filesystem::path& output){
 namespace actual=offlineConsumer;
 using Window=ymcc::deckysidebar::ConnectionRecoveryWindow;
 using Action=Window::Action;
 Controls controls(L"runtime-success-checkpoint");Context context;
 ContextJob contextOwner(worker::number(context.lease.at("pid")));
 const auto baseline=handles();
 actual::observedSession={output.wstring(),worker::number(context.lease.at("pid")),12345678,
     worker::decimal(context.lease.at("birth"))};
 actual::observedMode={{"known",true},{"running",true},{"mode","bigpicture"},{"modeEpoch","notification-doc:1"},
  {"pid",actual::observedSession.pid},{"birth",std::to_string(actual::observedSession.created)},
  {"deckyFrontendLoaded",true},{"deckyBackendConnected",false},{"deckyBackendState","socket-connecting"},
  {"deckyHooksInstalled",true},{"deckyClientConnected",false},{"deckyClientChecking",false},{"deckyClientVerified",false},
  {"deckyUpdateResidue",{{"protocol",1},{"active",true},{"bridgePresent",true},
     {"frontendBuildId",std::string(64,'d')},{"loaderVersion",ymcc::deckyidentity::version}}}};
 actual::initialUpdateRecovery=nullptr;actual::retainedConsent=true;actual::realModeObserver=nullptr;
 actual::fixedTick=0;actual::fixturePower=1;actual::g_exitRequested=false;
 actual::g_powerLifecycle=actual::PowerLifecycle::Ready;actual::g_deckyMirrorBroker.connected=false;
 std::mutex gate;
 std::atomic<unsigned> healthReads{0},admissionReads{0},modeReads{0},changes{0},acceptedChanges{0},stable{0},rebuilds{0},reopens{0};
 std::atomic<bool> connected{false},pending{false},modePaused{false};
 std::atomic<ULONGLONG> firstPending{0};
 Json healthRows=Json::array(),checks=Json::array();
 std::uint64_t firstClosedPublication=0;
 Json firstClosedMode;
 ymcc::steamui::ModeObserver shared;
 auto health=actual::actualConnectionHealth();auto admission=actual::actualAdmission();
 const auto readHealth=[&]{std::lock_guard lock(gate);auto value=health();++healthReads;
  connected=value.connected&&!value.paused;
  modePaused=value.paused&&value.reason=="steam-bigpicture-unconfirmed";
  if(value.reason=="official-backend-reconnect-pending"){
   pending=true;ULONGLONG zero=0;firstPending.compare_exchange_strong(zero,GetTickCount64());
  }
  if(healthRows.size()<64)healthRows.push_back({{"at",GetTickCount64()},{"connected",value.connected},
   {"paused",value.paused},{"reason",value.reason}});
  return value;
 };
 const auto step=[&](Action action){
  if(action==Action::rebuild){++rebuilds;
   {std::lock_guard lock(gate);actual::observedMode["deckyBackendConnected"]=true;
    actual::observedMode["deckyBackendState"]="socket-open";
    actual::observedMode["modeEpoch"]="notification-doc:2";
    actual::g_deckyMirrorBroker.connected=false;
    actual::observedMode["deckyClientConnected"]=false;actual::observedMode["deckyClientVerified"]=false;}
   shared.requestRefresh(actual::observedSession,false);
   return Json{{"ok",true},{"mutated",true},{"uncertain",false},{"reason","fixture-original-page-dispatch-ack"}};
  }
  if(action==Action::reopen)++reopens;
  return Json{{"ok",false},{"mutated",false},{"reason","fixture-unexpected-step"}};
 };
 Harness h(output,context,true,[&]{std::lock_guard lock(gate);++admissionReads;return admission();},[&](Json&,unsigned){
  std::lock_guard lock(gate);actual::g_deckyMirrorBroker.connected=true;
  actual::observedMode["deckyClientConnected"]=true;actual::observedMode["deckyClientVerified"]=true;
  if(actual::realModeObserver)shared.requestRefresh(actual::observedSession,false);
 },{},{},{},readHealth,step);
 actual::runtime=&h.runtime;
 struct Clear{~Clear(){actual::runtime=nullptr;actual::realModeObserver=nullptr;actual::retainedConsent=false;}}clear;
 const auto began=GetTickCount64();need(h.runtime.setEnabled(true),"notification-enable-original-runtime");
 need(waitFor([&]{return h.healthyGeneration>=1&&h.runtime.snapshot().phase=="frontend-attached";},3000),"notification-original-retained-binding");
 auto stableCallback=actual::actualStableBigPicture();
 ymcc::steamui::ModeObserverConfig config;config.debounceMs=1;config.minimumGapMs=1;config.failureGapMs=80;
 config.read=[&](const auto&){std::lock_guard lock(gate);++modeReads;return actual::observedMode;};
 config.changed=[&](const auto&,const auto&){++changes;if(actual::runtime&&actual::runtime->requestConnectionCheck())++acceptedChanges;};
 config.cssBigPicturePublished=[](const auto& session,const auto& mode,const auto& timing,bool stable){
  ymcc::steamcss::ModeSample sample{ymcc::steamcss::UiMode::BigPicture,mode.value("modeEpoch",std::string{})};
  sample.timing=timing;actual::g_steamCssGuard.observeMode(session,sample,stable);
 };
 config.stableBigPicture=[&](const auto& session){std::lock_guard lock(gate);
  std::uint64_t publication=0;ULONGLONG observedAt=0;
  auto mode=actual::Modes::readSnapshot(&shared,session,nullptr,&publication,&observedAt);
  if(mode.value("deckyBackendState",std::string{})=="socket-closed"){
   if(!firstClosedPublication){
    firstClosedPublication=publication;firstClosedMode=mode;
    std::uint64_t copied=0;ULONGLONG copiedAt=0;const auto same=actual::Modes::readSnapshot(&shared,session,nullptr,&copied,&copiedAt);
    checks.push_back({{"name","cache-copy-keeps-one-publication"},{"passed",publication&&observedAt&&copied==publication&&copiedAt==observedAt&&same==mode}});
   }else if(checks.size()==1){
    checks.push_back({{"name","identical-shared-read-publishes-new-receipt"},{"passed",publication>firstClosedPublication&&mode==firstClosedMode}});
   }
  }
  stableCallback(session);++stable;
 };
 need(shared.initialize(std::move(config)),"notification-original-shared-reader-start");
 shared.setDemand(ymcc::steamui::ModeConsumer::Decky,true);shared.observeSteam(actual::observedSession);
 need(waitFor([&]{return changes==1&&shared.snapshot(actual::observedSession).value("deckyClientVerified",false);},1500),"notification-original-opening-read");
 {std::lock_guard lock(gate);actual::realModeObserver=&shared;}
 const auto initialHealthReads=healthReads.load();h.runtime.requestConnectionCheck();
 need(waitFor([&]{return healthReads>initialHealthReads&&connected.load();},1000),"notification-original-connecting-mirror-usable");
 Sleep(40);
 auto lease=shared.observedSource(actual::observedSession);
 need(lease.current(),"notification-original-source-lease-current");
 const auto beforeChanges=changes.load(),beforeHealth=healthReads.load();
 {std::lock_guard lock(gate);actual::observedMode["deckyBackendState"]="socket-closed";}
 const auto faultAt=GetTickCount64();need(shared.requestRefresh(actual::observedSession,false),"notification-existing-read-request");
 need(waitFor([&]{return stable>=1;},1500),"notification-definite-closed-read-settled");
 const bool sameBooleanSuppressedChanged=changes==beforeChanges;
 const bool originalOwnerObserved=waitFor([&]{return healthReads>beforeHealth&&pending.load();},1000);
 const bool preservedGrace=originalOwnerObserved&&rebuilds==0&&reopens==0;
 bool recovered=false;Json controlRows=Json::array();
 if(originalOwnerObserved){
  // Full original seven-second health grace and Runtime ladder, no fake clock.
  recovered=waitFor([&]{return rebuilds==1&&h.healthyGeneration>=2&&connected.load();},
      Window::transportGraceMs+2*Window::evidenceIntervalMs+5000);
  if(recovered){
   // Restore the identical valid facts after a completed failure. The real
   // observer must emit changed, not stable; only that callback wakes health.
   Json goodMode;
   {std::lock_guard lock(gate);goodMode=actual::observedMode;
    actual::observedMode={{"known",true},{"running",true},{"mode","unknown"},{"reason","live-busy"},
     {"pid",actual::observedSession.pid},{"birth",std::to_string(actual::observedSession.created)}};}
   const bool completed=shared.requestRefresh(actual::observedSession,false)&&waitFor([&]{
    std::uint64_t publication=0;ULONGLONG observedAt=1;
    return actual::Modes::readSnapshot(&shared,actual::observedSession,nullptr,&publication,&observedAt).value("reason",std::string{})=="live-busy"&&publication==0&&observedAt==0;
   },1500);
   h.runtime.requestConnectionCheck();
   const bool paused=completed&&waitFor([&]{return modePaused.load();},1000);
   const auto priorChanges=acceptedChanges.load(),priorHealth=healthReads.load(),priorCss=actual::stableCssHints.load();
   shared.setDemand(ymcc::steamui::ModeConsumer::Css,true);
   {std::lock_guard lock(gate);actual::observedMode=std::move(goodMode);}
   const bool notified=shared.requestRefresh(actual::observedSession,false)&&waitFor([&]{
    return acceptedChanges>priorChanges&&healthReads>priorHealth&&connected.load()&&!modePaused.load();
   },1500);
   controlRows.push_back({{"name","completed-unknown-to-identical-mode-notifies-runtime"},
    {"passed",paused&&notified&&actual::stableCssHints>priorCss&&rebuilds==1&&reopens==0},
    {"paused",paused},{"notified",notified},{"independentCssPublication",actual::stableCssHints>priorCss}});
   shared.shutdown();
   {std::lock_guard lock(gate);actual::realModeObserver=nullptr;}
   const auto row=[&](const char* label,Json mode,bool expectHealth){
    const auto prior=healthReads.load();
    {std::lock_guard lock(gate);auto saved=actual::observedMode;actual::observedMode=std::move(mode);
     stableCallback(actual::observedSession);actual::observedMode=std::move(saved);}
    if(expectHealth)waitFor([&]{return healthReads>prior;},1000);else Sleep(25);
    controlRows.push_back({{"name",label},{"passed",(healthReads>prior)==expectHealth}});
   };
   const auto good=actual::observedMode;
   // Only this observation is synthetic. The normal production callback and
   // production Runtime still decide whether their admission owner wakes.
   const auto stableAdmission=[&](const char* name,Json mode,bool expect){
    Sleep(125); // Drain the prior controlled signal, before taking the count.
    const auto prior=admissionReads.load(), healthBefore=healthReads.load();
    const auto cssBefore=actual::stableCssHints.load(),wallBefore=actual::stableWallpaperHints.load(),managedBefore=actual::stableManagedHints.load();
    {std::lock_guard lock(gate);auto saved=actual::observedMode;actual::observedMode=std::move(mode);
     stableCallback(actual::observedSession);actual::observedMode=std::move(saved);}
    if(expect)waitFor([&]{return admissionReads>prior;},1000);else Sleep(150);
    const auto reads=admissionReads.load()-prior;
    // CSS now receives the cache publication above, not duplicate stable-callback hints.
    const bool otherOwners=actual::stableCssHints==cssBefore&&actual::stableWallpaperHints>wallBefore&&actual::stableManagedHints>managedBefore;
    controlRows.push_back({{"name",name},{"passed",(reads>0)==expect&&healthReads==healthBefore&&otherOwners},
     {"admissionReads",reads},{"otherOwnersCalled",otherOwners}});
   };
   {auto opening=good;opening["deckyBackendConnected"]=false;opening["deckyBackendState"]="socket-connecting";
    stableAdmission("verified-retained-connecting-does-not-reread-admission",opening,false);
    auto unverified=opening;unverified["deckyClientVerified"]=false;
    stableAdmission("unverified-mirror-still-wakes-admission",unverified,true);
    auto absent=opening;absent["deckyFrontendLoaded"]=false;
    stableAdmission("missing-frontend-still-wakes-admission",absent,true);
    auto missingEpoch=opening;missingEpoch.erase("modeEpoch");
    stableAdmission("unbound-document-still-wakes-admission",missingEpoch,true);
   }
   row("healthy-open-keeps-existing-health-cadence",good,false);
   for(const auto& value:Json::array({nullptr,Json("false"),Json(0),Json::object()})){
    auto malformed=good;malformed["deckyBackendConnected"]=value;malformed["deckyBackendState"]="socket-closed";
    row("malformed-backend-does-not-request-health",malformed,false);
   }
   {auto opening=good;opening["deckyBackendConnected"]=false;opening["deckyBackendState"]="socket-connecting";
    row("connecting-does-not-request-health",opening,false);}
   {auto unknown=good;unknown.erase("deckyBackendState");unknown["deckyBackendConnected"]=false;
    row("missing-socket-state-does-not-request-health",unknown,false);}
   {auto malformed=good;malformed["deckyBackendConnected"]=false;malformed["deckyBackendState"]=Json::object();
    row("malformed-socket-state-does-not-request-health",malformed,false);}
   const auto healthBeforeExit=healthReads.load();actual::g_exitRequested=true;
   {std::lock_guard lock(gate);stableCallback(actual::observedSession);}actual::g_exitRequested=false;Sleep(25);
   controlRows.push_back({{"name","exit-original-guard-keeps-no-health-request"},{"passed",healthReads==healthBeforeExit}});
   auto foreign=actual::observedSession;++foreign.created;
   {std::lock_guard lock(gate);stableCallback(foreign);}Sleep(25);
   controlRows.push_back({{"name","foreign-session-original-guard-keeps-no-health-request"},{"passed",healthReads==healthBeforeExit}});
  }
 }
 h.runtime.shutdown();shared.shutdown();lease={};
 const auto returned=h.client->snapshot();
 actual::runtime=nullptr;actual::realModeObserver=nullptr;actual::retainedConsent=false;
 const auto after=handles();
 const bool resources=returned.fullyDestroyed&&returned.handles==0&&returned.pendingIo==0&&baseline==after&&context.alive();
 const bool controlsOkay=std::all_of(controlRows.begin(),controlRows.end(),[](const Json& row){return row.value("passed",false);});
 const bool distinct=checks.size()==2&&std::all_of(checks.begin(),checks.end(),[](const Json& row){return row.value("passed",false);});
 const bool passed=sameBooleanSuppressedChanged&&preservedGrace&&recovered&&controlsOkay&&resources&&distinct;
 Json result{{"passed",passed},{"variant","backend-notify"},{"actualRuntime",true},{"actualSharedModeObserver",true},
  {"actualMainStableCallback",true},{"actualMainHealth",true},{"actualRecovery",true},
  {"sameBooleanSuppressedChanged",sameBooleanSuppressedChanged},{"originalOwnerObserved",originalOwnerObserved},
  {"publicationChecks",checks},
  {"preservedOriginalGrace",preservedGrace},{"recovered",recovered},{"rebuilds",rebuilds.load()},{"reopens",reopens.load()},
  {"faultAtMs",faultAt},{"firstPendingAtMs",firstPending.load()},
  {"readRequestToHealthMs",firstPending.load()>=faultAt?firstPending.load()-faultAt:0},
  {"modeReads",modeReads.load()},{"stableCallbacks",stable.load()},{"healthReads",healthReads.load()},
  {"elapsedMs",GetTickCount64()-began},{"beforeHandles",baseline},{"afterHandles",after},{"atExit",state(returned)},
  {"contextAlive",context.alive()},{"controls",controlRows},{"healthObservations",healthRows},
  {"globalRecoveryLog",h.globalRecoveryLog},{"realSteamOrDeviceActions",0},
  {"scope","Actual unchanged-source notification, Native stable callback, Runtime and original real-time grace. UI read facts, normal mode-changed notification and page dispatch are controlled leaves; not installed Steam, input or CPU acceptance."}};
 if(!passed)result["error"]=!originalOwnerObserved?"fresh-closed-mode-did-not-wake-original-health-owner":"backend-notification-regression";
 return result;
}
