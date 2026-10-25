// Existing lifecycle fixture only: the production tick, serializer, pipe IO,
// inspection and recovery owners execute unchanged. Only the owned private pipe
// is disconnected after one actual SUBMIT_FRAME write. No physical SDK is loaded.
#ifdef YMCC_FOCUS_RECOVERY_PORTS
static bool fixtureFocusRecoveryActive=false,fixtureFocusRecoveryDisconnect=false;
static unsigned fixtureFocusRecoveryDisconnects=0;
static std::string fixtureFocusRecoveryPhase;
static std::vector<nlohmann::json> fixtureFocusRecoveryWire,fixtureFocusRecoveryIo;
static void fixtureFocusRecoveryPort(HANDLE,bool,void*,DWORD,bool,DWORD,DWORD);
#else
static void fixtureFocusRecoveryPort(HANDLE pipe,bool write,void* buffer,DWORD size,
        bool ok,DWORD transferred,DWORD error){
 if(!fixtureFocusRecoveryActive)return;
 if(!fixtureFullPipe||pipe!=g_inputHostServerPipe)
  throw std::logic_error("focus-recovery-port-is-not-the-owned-pipe");
 fixtureFocusRecoveryIo.push_back({{"phase",fixtureFocusRecoveryPhase},{"write",write},
  {"ok",ok},{"error",error},{"transferred",transferred}});
 if(!write||!ok||!transferred)return;
 const auto wire=json::parse(std::string(static_cast<char*>(buffer),transferred),nullptr,false);
 if(!wire.is_object())throw std::logic_error("focus-recovery-wire-is-not-json");
 if(wire.value("command","")!="SUBMIT_FRAME")return;
 fixtureFocusRecoveryWire.push_back({{"phase",fixtureFocusRecoveryPhase},
  {"hostEpoch",g_inputHostEpoch},{"owner",g_radialUiOwnsInput.load()},
  {"wire",wire},{"observation","successful actual owned pipe write"}});
 if(fixtureFocusRecoveryDisconnect){
  fixtureFocusRecoveryDisconnect=false;
  if(!DisconnectNamedPipe(pipe))throw std::runtime_error("owned-pipe-disconnect-failed");
  ++fixtureFocusRecoveryDisconnects;
 }
}
static bool focusRecoveryWireIsNeutral(const json& wire){
 for(const char* key:{"buttons","backButtons","lx","ly","rx","ry","lt","rt","ltRaw","rtRaw"})
  if(!wire.contains(key)||!wire[key].is_number()||wire[key].get<double>()!=0)return false;
 if(!wire.contains("raw")||!wire["raw"].is_object())return false;
 for(const char* key:{"buttons","sThumbLX","sThumbLY","sThumbRX","sThumbRY","bLeftTrigger","bRightTrigger"})
  if(!wire["raw"].contains(key)||wire["raw"][key].get<double>()!=0)return false;
 if(!wire.contains("imu")||wire["imu"].value("admitted",true))return false;
 for(const char* group:{"gyroDps","accelG"})for(const char* axis:{"x","y","z"})
  if(!wire["imu"].contains(group)||!wire["imu"][group].contains(axis)||wire["imu"][group][axis].get<double>()!=0)return false;
 if(!wire["imu"].contains("quaternion"))return false;
 const auto& q=wire["imu"]["quaternion"];
 for(const char* axis:{"w","x","y","z"})
  if(!q.contains(axis)||q[axis].get<double>()!=(std::strcmp(axis,"w")==0?1.0:0.0))return false;
 return !wire.contains("touchpads")&&wire.value("screenButtons",0u)==0;
}
static void focusRecoveryCases(){
 if(fixtureManagedDll.empty())throw std::logic_error("focus-recovery-requires-owned-sdk-free-dll");
 for(const bool lateFocus:{false,true}){
  cases.push_back({{"name",lateFocus?"focus-recovery/focus-after-queue":"focus-recovery/focused-throughout"},
   {"caller","actual inputCaptureOnGamepad"},{"transport","actual private pipe disconnect"},
   {"sdkFree",true},{"physicalSensorCoverage",false}});
  seed();fixtureFullPipe=true;fullPipeFault="none";
  fixtureFocusRecoveryActive=true;fixtureFocusRecoveryDisconnect=false;
  fixtureFocusRecoveryDisconnects=0;fixtureFocusRecoveryWire.clear();fixtureFocusRecoveryIo.clear();
  struct Scope{
   bool owner=g_radialUiOwnsInput.load(),ready=g_inputCaptureReady;
   ~Scope(){g_radialUiOwnsInput=owner;g_inputCaptureReady=ready;fixtureFocusRecoveryActive=false;fixtureFullPipe=false;}
  } scope;
  // The existing fixture has no window, sensor STA/owner, motion DLL or DSU
  // worker. Cached UI ownership avoids any real window manipulation/input.
  check(!g_hwnd&&!g_inputCaptureSink&&!g_gm,"focus recovery tick has no real window sensor owner or motion DLL");
  g_machineIdentity.family=YmccFamily::AsusRogAllyX;
  g_machineIdentity.manufacturer="ASUSTEK COMPUTER INC.";g_machineIdentity.product="ROG Ally X";
  g_msiDinput.enabled=false;g_msiDinputSourceValid=false;
  g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;
  g_externalPriorityVirtualSuspended=false;g_shortcutRecordingActive=false;g_inputCaptureStop=false;
  fixtureXinputSourceLive=true;
  // Process-equivalent discovery initialization precedes target creation. The
  // unchanged four-slot loop and selected-source consumer establish the lock;
  // never inject a return verdict or clear quarantine to authorize recovery.
  g_xinputPreTargetSlots=0;g_xinputPreTargetBoundaryScanned=false;g_xinputSlotFullScanOnce=false;
  cases.back()["sourceDiscovery"]=fullPipeReadPhysicalSourceForElite();
  g_radialUiOwnsInput=!lateFocus;g_inputCaptureReady=true;
  check(sgWriteFileAtomic(YM_SETTINGS_FILE,targetDocument("steamdeck",1).dump()),"focus recovery seeds only private target settings");
  const auto admission=g_virtualStartProtection.admit("steamdeck",startProtectionClock);
  const auto ticket=admission.requestId;
  const auto deadline=startProtectionClock+g_virtualStartProtection.view(startProtectionClock).remainingMs;
  const auto spawns=fullPipeSpawnCount;
  const auto read=[&](bool held){
   auto sample=fullPipeReadNow(1);
   if(held){sample.pad.wButtons=XINPUT_GAMEPAD_A;sample.pad.sThumbLX=12000;sample.pad.sThumbLY=-13000;
    sample.pad.sThumbRX=14000;sample.pad.sThumbRY=-15000;sample.pad.bLeftTrigger=120;sample.pad.bRightTrigger=160;}
   std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);
   const auto prior=inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot,true);
   sample.readContinuityGeneration=ymcc::input::recoveryHealth::nextContinuity(prior,inputHostRecoveryHealthSource(&sample,true),GetTickCount64());
   g_inputCaptureGamepadSnapshot=sample;return sample;
  };
  const auto tick=[&](const char* phase,bool held){
   fixtureFocusRecoveryPhase=phase;auto sample=read(held);CommonFrameSourceScope source(&sample);
   inputCaptureOnGamepad(true,sample.pad,0,1,"xinput");
   return sample.readAttemptSequence;
  };
  read(false);check(inputHostStart(),"focus recovery starts the first real SDK-free Host");
  const auto oldEpoch=g_inputHostEpoch;
  for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n)tick("old-host-admission",false);
  tick("old-host-held",true);
  const auto candidate=g_virtualStartProtection.view(startProtectionClock);
  check(candidate.requestId==ticket&&candidate.phase=="verifying"&&candidate.candidateOutcome=="success"&&candidate.verificationRemainingMs==10000,
   "actual tick and real ACK establish the original R3 inspection candidate");
  const auto tc=startProtectionClock;
  fixtureFocusRecoveryDisconnect=true;tick("faulting-current-frame",true);
  check(fixtureFocusRecoveryDisconnects==1&&g_inputHostTransportFault&&
   ymcc::inputHost::switchRecoveryRuntime::retryable(g_inputHostSwitchRecoveryTransportError),
   "one actually written current SUBMIT_FRAME loses its real pipe transport");
  check(std::any_of(fixtureFocusRecoveryIo.begin(),fixtureFocusRecoveryIo.end(),[](const auto& row){return !row.value("write",true)&&!row.value("ok",true)&&row.value("error",0u)!=0;}),
   "original kernel pipe read returns the transport failure without a fabricated receipt");
  startProtectionClock=tc+9999;read(true);inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
  check(fullPipeSpawnCount==spawns+1&&g_inputHostRecoveryEpisodeAttempts.load()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,
   "real fault cleanup cannot reserve charge or recreate before R3 inspection ends");
  startProtectionClock=tc+10000;read(true);inputHostRecoveryMaintenanceTick();
  if(!g_inputHostSwitchRecovery.queued){fullPipeQuarantineProbe();read(true);inputHostRecoveryMaintenanceTick();}
  check(g_inputHostSwitchRecovery.queued&&g_inputHostSwitchRecoveryPending.load(),"classified fault queues recovery under the original request after inspection");
  // The second variant changes only the cached OS/UI owner AFTER queueing,
  // before the unchanged executor consumes the work. No neutral flag is supplied.
  g_radialUiOwnsInput=true;
  fixtureFocusRecoveryPhase="rebuild";fullPipeDrainQueuedLifecycle();
  for(int n=0;n<4&&fullPipeSpawnCount==spawns+1;++n){fullPipeQuarantineProbe();read(true);inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();}
  const auto newEpoch=g_inputHostEpoch;
  check(fullPipeSpawnCount==spawns+2&&newEpoch&&newEpoch!=oldEpoch&&g_inputHostPrepared&&g_inputHostNeutralized,
   "one classified fault returns the old target and creates exactly one new Host");
  auto repaired=g_virtualStartProtection.view(startProtectionClock);
  check(repaired.requestId==ticket&&repaired.fallbackAttempts==1&&g_inputHostRecoveryEpisodeAttempts.load()==1&&
   startProtectionClock+repaired.remainingMs==deadline,"rebuild preserves one R3 request deadline and one real-fault budget charge");
  for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n)tick("new-host-admission",false);
  for(int n=0;n<5;++n){tick("new-host-focused-held",true);read(true);inputHostRecoveryMaintenanceTick();}
  std::vector<json> replacement;
  for(const auto& observed:fixtureFocusRecoveryWire)if(observed.value("hostEpoch",0ULL)==newEpoch)replacement.push_back(observed);
  check(replacement.size()>=6,"new Host first frame and continuous focused frames reach the real wire");
  check(!replacement.empty()&&focusRecoveryWireIsNeutral(replacement.front()["wire"]),"focus recovery new Host first wire frame is fully neutral");
  check(std::all_of(replacement.begin(),replacement.end(),[](const auto& row){return row.value("owner",false)&&focusRecoveryWireIsNeutral(row["wire"]);}),
   "focus recovery all focused replacement wire reports are fully neutral");
  check(std::all_of(fixtureFocusRecoveryWire.begin(),fixtureFocusRecoveryWire.end(),[](const auto& row){return !row.value("owner",false)||focusRecoveryWireIsNeutral(row["wire"]);}),
   "focus recovery every owner-held real wire report is fully neutral");
  check(g_inputHostRecoveryHealth.since()==0&&g_inputHostRecoveryHealth.through()==0&&g_inputHostHealthySinceTick.load()==0&&
   g_inputHostRecoveryEpisodeAttempts.load()==1&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1,
   "focused real ACKs provide no recovery health credit or budget reset");
  if(!replacement.empty())check(g_inputHostActive&&g_inputHostSequence==replacement.back()["wire"].value("sequence",0ULL),
   "focused replacement wire receives the actual current managed ACK");
  g_radialUiOwnsInput=false;
  const auto beforeStale=fixtureFocusRecoveryWire.size();
  {auto stale=read(true);stale.readPowerGeneration=currentPowerGeneration()+1;
   fixtureFocusRecoveryPhase="owner-released-stale-held";CommonFrameSourceScope source(&stale);
   inputCaptureOnGamepad(true,stale.pad,0,1,"xinput");}
  check(std::all_of(fixtureFocusRecoveryWire.begin()+beforeStale,fixtureFocusRecoveryWire.end(),[](const auto& row){return focusRecoveryWireIsNeutral(row["wire"]);}),
   "owner release cannot publish nonzero input from a retired physical read");
  const auto beforeRelease=fixtureFocusRecoveryWire.size();
  const auto releaseRead=tick("owner-released-current-held",true);
  check(fixtureFocusRecoveryWire.size()==beforeRelease+1,"owner release submits exactly one new current-source report");
  if(fixtureFocusRecoveryWire.size()>beforeRelease){const auto& wire=fixtureFocusRecoveryWire.back()["wire"];
   check((wire.value("buttons",0)&XINPUT_GAMEPAD_A)!=0&&wire["raw"].value("sThumbRX",0)==14000&&
    wire["raw"].value("bLeftTrigger",0)==120&&wire["raw"].value("bRightTrigger",0)==160,
    "only released owner and a fresh nonzero source restore real wire passthrough");}
  const auto final=g_virtualStartProtection.view(startProtectionClock);
  // A successful terminal View intentionally reports remainingMs=0. The
  // recovery owner's retained deadline is still the original request deadline;
  // the live, pre-final remaining-time equation was checked immediately above.
  check(final.requestId==ticket&&g_inputHostSwitchRecovery.deadline==deadline&&final.fallbackAttempts==1&&
   fullPipeSpawnCount==spawns+2,"continuous focus and release neither renew the request nor rebuild again");
  check(final.phase=="success"&&!final.locked,"actual focused replacement verification reaches the one successful R3 final");
  cases.back()["requestId"]=ticket;cases.back()["candidateTick"]=tc;cases.back()["deadline"]=deadline;
  cases.back()["retainedRecoveryDeadline"]=g_inputHostSwitchRecovery.deadline;cases.back()["finalPhase"]=final.phase;
  cases.back()["oldEpoch"]=oldEpoch;cases.back()["newEpoch"]=newEpoch;cases.back()["releaseReadSequence"]=releaseRead;
  cases.back()["wire"]=fixtureFocusRecoveryWire;cases.back()["pipeIo"]=fixtureFocusRecoveryIo;
  cases.back()["nativeEvents"]=fixtureEvents;cases.back()["fallbackAttempts"]=final.fallbackAttempts;
  cases.back()["episodeAttemptsBeforeCleanup"]=g_inputHostRecoveryEpisodeAttempts.load();
  g_inputCaptureReady=false;fixtureFocusRecoveryPhase="cleanup";inputHostStop("focus-recovery-private-cleanup");
  fullPipeQuarantineProbe();
  while(!fullPipeChildObservers.empty())cases.back()["children"].push_back(fullPipeChildOutcome(fullPipeChildObservers.front(),fullPipeReceipts[fullPipeReceipts.size()-fullPipeChildObservers.size()]));
  check(!g_inputHostProcess&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active&&forbiddenIo==0,
   "focus recovery returns only its private children with no real device IO");
 }
}
#endif
