// Directed boundaries inside the original shared transport consumer. The mode
// worker/window, production callback/read and steamlive transport are real.
// The original fixture supplies inert process/WinHTTP endpoints only.
static int modeReadCurrentFixture(const std::string& selected) {
 const std::vector<std::string> cases={
  "off-before-discovery","session-before-discovery","stop-before-discovery","native-before-discovery",
  "off-during-discovery","session-during-discovery","stop-during-discovery","native-during-discovery",
  "off-before-send","session-before-send","stop-before-send","native-before-send",
  "off-at-reply","session-at-reply","stop-at-reply","native-at-reply",
  "publication-only","normal-once"};
 if(std::find(cases.begin(),cases.end(),selected)==cases.end())return 2;
 const auto base=listenerChecks;
 processIdentityProbe=dispatchProbe=true;admissionProbe=true;
 processIdentityOpens=processIdentityCloses=processIdentityUnexpected=0;
 processIdentityOpen=processIdentityPath=processIdentityTimes=processIdentityAlive=true;
 dispatchRead=0;dispatchSendFails=dispatchExitOnUpgrade=false;dispatchOwner=true;
 dispatchReplyValue=R"({"ok":true,"mode":"bigpicture","observedAccount":42,"deckyAccountWatchAvailable":true,"modeEpoch":"renderer:7","mutated":false})";
 dispatchReplyHook={};dispatchUpgradeHook={};modeDiscoveryHook={};
 failureDiagnosticsProbe=false;g_exitRequested=false;
 const auto httpBefore=dispatchHttpOpen,closeBefore=dispatchHttpClose,sendsBefore=dispatchSends;
 const auto portsBefore=admissionPortReads.load();
 ymcc::processInventory::invalidate();
 ymcc::steamsession::IdentitySnapshot identity;
 identity.session={L"P:\\YMCC-identity-fixture",processIdentityPid,0,processIdentityBirth};
 identity.presence=1;identity.registeredPid=processIdentityPid;identity.processEpoch=1;identity.accountEpoch=1;
 identity.rootReadError=identity.registeredPidReadError=identity.accountReadError=ERROR_SUCCESS;
 g_steamSettingsObserver.value=identity.session;g_steamSettingsObserver.publish(identity);g_steamSettingsObserver.onRead={};
 {
  std::lock_guard lock(modePowerMutex);modePowerValue={};modePowerValue.generation=17;
  modePowerValue.publicationRevision=5;modePowerValue.observedNotificationSequence=4;
  modePowerValue.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;modePowerValue.suspendSerial=3;
 }
 struct OwnedEvents {
  HANDLE reached=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr),published=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  ~OwnedEvents(){if(reached)CloseHandle(reached);if(release)CloseHandle(release);if(done)CloseHandle(done);if(published)CloseHandle(published);}
 } events;
 expect(events.reached&&events.release&&events.done&&events.published,"directed owned events created");
 ymcc::steamui::ModeObserver observer;
 std::function<bool()> ticketCurrent;
 std::atomic<unsigned> reads{0},changed{0},sources{0},stable{0},settled{0};
 std::atomic<bool> gateTimedOut{false};
 const bool normal=selected=="normal-once",publication=selected=="publication-only";
 const bool reply=selected.ends_with("at-reply");
 const bool entry=selected.ends_with("before-discovery");
 auto gate=[&] {
  SetEvent(events.reached);
  if(WaitForSingleObject(events.release,3000)!=WAIT_OBJECT_0)gateTimedOut=true;
 };
 if(!entry&&!normal){
  if(selected.ends_with("during-discovery"))modeDiscoveryHook=gate;
  else if(reply)dispatchReplyHook=gate;
  else dispatchUpgradeHook=gate;
 }
 ymcc::steamui::ModeObserverConfig config;
 config.debounceMs=0;config.minimumGapMs=50;config.failureGapMs=150;
 config.changed=[&](const auto& s,const Json& facts){
  expect(s==identity.session&&facts.value("mode",std::string{})=="bigpicture","normal publish retains original session and observed mode");
  ++changed;SetEvent(events.published);
 };
 config.sourceChanged=[&](auto){++sources;};
 config.stableBigPicture=[&](const auto&){++stable;};config.settled=[&](const auto&){++settled;};
#ifdef YMCC_MODE_READ_CURRENT_BEFORE
 config.read=[&](const auto& s){++reads;if(entry)gate();const auto result=productionUiModeRead(s);SetEvent(events.done);return result;};
#else
 config.readOwned=[&](const auto& s,const auto& current){
  ticketCurrent=current;++reads;if(entry)gate();
  const auto result=productionUiModeRead(s,current);SetEvent(events.done);return result;
 };
#endif
 observer.observeSteam(identity.session);observer.setDemand(ymcc::steamui::ModeConsumer::Decky,true);
 if(!observer.initialize(std::move(config)))throw std::runtime_error("mode worker failed to initialize");
 std::thread shutdown;
 bool reached=true,done=false,published=false;
 if(!normal){
  reached=WaitForSingleObject(events.reached,3000)==WAIT_OBJECT_0;
  if(reached){
   if(selected.starts_with("off-"))observer.setDemand(ymcc::steamui::ModeConsumer::Decky,false);
   else if(selected.starts_with("session-"))observer.observeSteam({});
   else if(selected.starts_with("stop-")){
    shutdown=std::thread([&]{observer.shutdown();});
#ifndef YMCC_MODE_READ_CURRENT_BEFORE
    const auto until=GetTickCount64()+1000;while(ticketCurrent()&&GetTickCount64()<until)SwitchToThread();
    if(ticketCurrent())gateTimedOut=true;
#else
    // Control has no owned predicate; allow its original shutdown to reach join.
    Sleep(30);
#endif
   }else {
    std::lock_guard lock(modePowerMutex);
    if(publication)++modePowerValue.publicationRevision;
    else {++modePowerValue.generation;++modePowerValue.suspendSerial;if(entry)modePowerValue.phase=2;}
   }
  }
  SetEvent(events.release);
 }
 done=WaitForSingleObject(events.done,3000)==WAIT_OBJECT_0;
 if(normal||publication)published=WaitForSingleObject(events.published,3000)==WAIT_OBJECT_0;
 // More than three configured minimum gaps: a cancelled read cannot silently
 // rearm the old failure retry path. This is an isolated fixture clock setting.
 if(!normal&&!publication&&selected.starts_with("native-"))Sleep(220);
 if(shutdown.joinable())shutdown.join();else observer.shutdown();
 dispatchReplyHook={};dispatchUpgradeHook={};modeDiscoveryHook={};
 expect(reached&&done&&!gateTimedOut,"selected real worker/transport boundary completed within its original gate");
 const auto sent=dispatchSends-sendsBefore;
 const bool success=normal||publication;
 expect(sent==(success||reply?1u:0u),"revoked mode read sends zero before dispatch and never replays after a reply");
 expect(reads==1,"cancellation does not reopen or refund the original read window");
 expect(changed==(success?1u:0u)&&sources==(success?1u:0u)&&stable==0&&settled==0,"only a current fresh observation publishes once; cancellation grants no recovery callback");
 expect(!success||published,"valid read reaches actual snapshot publication");
 expect(!entry||admissionPortReads.load()==portsBefore,"revocation while waiting reaches transport before listener discovery");
 expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0,"original process leases returned without a foreign open");
 expect(dispatchHttpOpen-httpBefore==dispatchHttpClose-closeBefore,"original transport HTTP and websocket owners all returned");
 const auto diagnostics=observer.sourceEventDiagnostics();
 expect(diagnostics.created==diagnostics.closed&&diagnostics.closeFailed==0&&diagnostics.signalFailed==0,"mode snapshot cancellation events returned after shutdown");
 std::cout<<"MODE_READ_CURRENT_RESULT "<<Json{{"case",selected},{"passed",true},{"checks",listenerChecks-base},{"reads",reads.load()},{"sends",sent},{"changed",changed.load()},{"sourcePublications",sources.load()},{"discoveryCalls",admissionPortReads.load()-portsBefore},{"actualSteamOperations",0},{"modeWorkerReal",true},{"transportReal",true},{"defaultParentExecuted",false}}.dump()<<'\n';
 return 0;
}
