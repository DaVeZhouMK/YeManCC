// Explicit additional parameters of the existing family-lifecycle GPD consumer.
namespace preHide183409 {
static void checkpoint(const char* text){std::cerr<<"PREHIDE "<<text<<std::endl;}
static void diagnosticTerminate() noexcept {try{if(auto p=std::current_exception())std::rethrow_exception(p);}catch(const std::exception& e){std::cerr<<"TERMINATE "<<e.what()<<std::endl;}catch(...){std::cerr<<"TERMINATE unknown"<<std::endl;}::ExitProcess(95);}
static std::string shape;
static bool enabled=false,published=false,prepared=false,hidden=false;
static unsigned yields=0,cycles=0;
static bool getterCross=false;static uint64_t originalDeadline=0;
static json order=json::array();
static void publish(){
 if(published)return;published=true;fixtureXinputSourceLive=true;
 checkpoint("before-source-discovery");cases.back()["discovery"]=fullPipeReadPhysicalSourceForElite();
 checkpoint("after-source-discovery");order.push_back("source-admitted-by-original-discovery");
}
static void pipe(const char* command){
 if(!enabled||!command)return;
 const std::string cmd=command;
 if(cmd=="HELLO"){order.push_back("HELLO");if(shape=="hello-return")publish();}
 if(cmd=="PREPARE_TARGET"){
  checkpoint("prepare");prepared=true;order.push_back("PREPARE_TARGET");
  check(hidden&&g_hidHideVisibilityJournal.active,"GPD pending source must be hidden before target preparation");
  check(cycles==1,"GPD returned source must complete existing post-hide cycle before target preparation");
 }
}
static bool hide(const std::string& transaction,bool powerCycle){
 if(!enabled)return false;
 order.push_back("hide");hidden=true;
 const bool prior=fixtureFullPipe;fixtureFullPipe=true;
 const bool ok=fixtureGpdPostHideApply(transaction,powerCycle);fixtureFullPipe=prior;
 const auto refresh=g_msiNativeForegroundTestIo.xusbRefresh;
 g_msiNativeForegroundTestIo.xusbRefresh=[refresh](const HidHidePhysicalIdentity& source,const std::wstring& location){
  ++cycles;order.push_back("post-hide-cycle");return refresh(source,location);
 };
 return ok;
}
static bool sleep(DWORD ms){
 if(!enabled||!g_inputHostHello||g_inputHostPrepared||g_inputHostPrepareMayHaveReachedHost||g_inputHostMx.ownedByCurrentThread())return false;
 startProtectionClock+=ms;fixturePowerClockOffset+=ms;++yields;
 if(!hidden){
  if((shape=="late-return"||shape=="posthide-replacement"||shape=="publication-only")&&yields==3)publish();
  if(shape=="scope-revoked"){g_sharedPowerRevocations.revoke();publish();}
  if(shape=="publication-only")g_sharedPowerPublicationRevision.fetch_add(1);
  if(shape=="getter-deadline"||shape=="getter-inherited"||shape=="getter-scope"){publish();getterCross=true;}
  if(shape=="off"){g_realStickTest.virtualTargetEnabled=false;g_inputHostVirtualIntentRevision.fetch_add(1);}
  if(shape=="intent"){g_inputHostVirtualIntentRevision.fetch_add(1);}
  if(shape=="persona"){g_realStickTest.virtualTargetPersona="ps5";g_inputHostVirtualIntentRevision.fetch_add(1);}
  if(shape=="power"){g_powerGeneration.fetch_add(1);}
  if(shape=="capture-stop"){g_inputCaptureStop=true;}
  if(shape=="replacement"){g_inputHostEpoch+=100;g_inputHostRunId="successor-tuple";}
  if(shape=="deadline"){startProtectionClock=originalDeadline+1;publish();}
 }
 if(hidden&&shape=="posthide-replacement"){g_inputHostEpoch+=100;g_inputHostRunId="successor-tuple";}
 return true;
}
static void run(const std::string& selected){
 const std::set<std::string> supported={"hello-return","late-return","absent","off","intent","persona","power","capture-stop","replacement","deadline","posthide-replacement","scope-revoked","publication-only","getter-deadline","getter-inherited","getter-scope"};
 if(!supported.contains(selected))throw std::logic_error("unknown pre-hide parameter");
 checkpoint("before-seed");seed();checkpoint("after-seed");shape=selected;enabled=true;published=prepared=hidden=false;yields=cycles=0;order=json::array();
 fixtureFullPipe=false;fixtureGpdPostHideApplied=false;getterCross=false;
 g_machineIdentity.family=YmccFamily::Gpd;g_machineIdentity.manufacturer="GPD";g_machineIdentity.product="G1618-05";
 g_msiDinput.enabled=false;g_msiDinputSourceValid=false;
 g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;
 g_inputCaptureStop=false;g_sleepRecoveryOutputWanted=true;g_externalPriorityVirtualSuspended=false;g_shortcutRecordingActive=false;
 fixtureXinputSourceLive=false;g_xinputAdmittedMask=0;g_xinputConnectedMask=0;g_xinputPreTargetSlots=0;
 g_xinputPreTargetBoundaryScanned=false;g_xinputBoundaryScanEpoch=0;g_xinputSourceReconfirmMask=0;
 g_xinputSlotFullScanOnce=false;g_xinputSlotFullScanEpochStart=0;g_xinputDiscoveryDirty=true;g_xinputDiscoveryLastTick=0;
 g_realStickTest.virtualTargetPersona="steamdeck";g_realStickTest.virtualTargetEnabled=true;
 g_inputHostVirtualIntentWriters=0;g_inputHostRecoveryPending=false;g_inputHostSwitchRecoveryPending=false;
 cases.push_back({{"name","family-lifecycle/GPD/pending-before-hide/"+shape}});
 checkpoint("before-protection-admit");const auto beforeFailures=failures.size();const auto ticket=g_virtualStartProtection.admit("steamdeck",startProtectionClock).requestId;
 const auto original=g_virtualStartProtection.view(startProtectionClock);const auto began=startProtectionClock;
 originalDeadline=began+original.remainingMs;
 check(nativeForegroundProfile()==ymcc::nativeForeground::Profile::Gpd,"controlled actual Win5 DMI selects the real GPD route");
 if(shape=="getter-inherited"){g_inputHostSwitchRecoveryStartDeadline=began+500;originalDeadline=began+500;}
 checkpoint("before-Start");bool started=false;try{started=inputHostStart();checkpoint("after-Start");}catch(const std::exception& e){cases.back()["exception"]=e.what();throw;}
 const auto result=state();const auto after=g_virtualStartProtection.view(startProtectionClock);
 const bool positive=shape=="hello-return"||shape=="late-return"||shape=="publication-only";
 check(positive?started:!started,positive?"actual Start accepts returned source through original GPD path":"retired or unproven GPD source never prepares a target");
 if(positive){
  check(prepared&&hidden&&cycles==1,"one target prepare follows one hide and one original GPD post-hide cycle");
  check(g_inputHostPrepared&&g_inputHostNeutralized,"actual Start publishes prepared and neutralized target");
 }else{
  if(shape=="posthide-replacement")check(!prepared&&hidden&&cycles==1,"post-hide retired Start never prepares or stops replacement");
  else check(!prepared&&!hidden&&cycles==0,"pending cancellation or expiry performs no hide cycle or target prepare");
  if(shape=="replacement"||shape=="posthide-replacement")check(g_inputHostProcess&&g_inputHostRunId=="successor-tuple","retired Start does not stop replacement Host tuple");
  else check(!g_inputHostProcess,"unproven HELLO-only Host is returned through original Stop");
 }
 check(after.requestId==ticket,"wait retains original request identity");
 check(after.remainingMs<=original.remainingMs,"wait does not renew original player deadline");
 if(shape=="scope-revoked"||shape=="getter-scope")check(g_powerLifecycle==PowerLifecycle::Ready,"revoked original Scope remains revoked with same generation Ready");
 cases.back()["order"]=order;cases.back()["stateAfterStart"]=result;cases.back()["started"]=started;
 cases.back()["yields"]=yields;cases.back()["cycleCalls"]=cycles;cases.back()["elapsedMs"]=startProtectionClock-began;
 cases.back()["events"]=fixtureEvents;cases.back()["trace"]=g_startEntrySeam.trace;
 checkpoint("before-cleanup");enabled=false;g_inputCaptureStop=false;g_realStickTest.virtualTargetEnabled=false;cleanup();
 checkpoint("after-cleanup");check(!g_inputHostProcess&&!g_inputHostPrepared,"test returns its controlled Host handle through actual Stop");
 cases.back()["passed"]=beforeFailures==failures.size();
}
}
static void fixturePreHidePipe(const char* command){preHide183409::pipe(command);}
static bool fixturePreHideSleep(DWORD millis){return preHide183409::sleep(millis);}
static bool fixturePreHideApply(const std::string& transaction,bool powerCycle){return preHide183409::hide(transaction,powerCycle);}

static void fixturePreHideGetterReturn(){
 using namespace preHide183409;
 if(!enabled||!getterCross)return;getterCross=false;
 if(shape=="getter-scope"){if(t_inputHostRequestPower)t_inputHostRequestPower->cancel();}
 else startProtectionClock=originalDeadline+1;
 order.push_back("getter-boundary-crossed");
}

static bool fixturePreHideActive(){return preHide183409::enabled;}
