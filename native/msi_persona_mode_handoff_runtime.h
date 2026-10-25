#pragma once
// Lifecycle state is owned by g_inputHostMx. Only explicit MSI candidate builds
// serialize the MSI pump with the handoff; BUS uses try_lock, never a new wait.
struct MsiPersonaModeHandoffState {
    bool active=false,retained=false,resumed=false,aborting=false;
    ymcc::msi::persona::Facts facts;
    HidHideMsiMainSourceRecord source;
    uint64_t newEpoch=0;
};
static MsiPersonaModeHandoffState g_msiPersonaModeHandoff;
static bool g_msiPersonaStartScope=false;
static std::unique_lock<std::mutex> g_msiPersonaPumpHold;
static bool msiPersonaModeReadPaused(){return ymcc::msi::persona::buildEnabled&&g_msiPersonaReadPause.load(std::memory_order_acquire);}
static void msiPersonaModeNote(const char* phase,const char* reason){
    const auto& s=g_msiPersonaModeHandoff;
    appendNativeLifecycleLog("msi-claw-gamepad-mode",{{"action","persona-physical-mode-handoff"},{"handoffVersion",1},{"phase",phase},{"reason",reason?reason:"unspecified"},{"handoffBuild",ymcc::msi::persona::buildEnabled},{"oldHostEpoch",s.facts.epoch},{"newHostEpoch",s.newEpoch},{"powerGenerationAtCapture",s.facts.power},{"sourceGeneration",s.source.generation},{"sourceRoot",s.source.usbInstance},{"from",s.facts.from},{"to",s.facts.to},{"releaseProven",s.facts.releaseProven},{"visibilityHeld",s.facts.visibilityHeld},{"routeArmed",g_msiDinputRouteDinputArmed.load()},{"routeConfirmed",g_msiDinputRouteConfirmed.load()},{"sourceValid",g_msiDinputSourceValid.load()},{"returnDebtRetained",g_msiDinputReturnOwed.load()},{"modeWriteIssued",false},{"deviceModeVerified",false},{"readPaused",g_msiPersonaReadPause.load()},{"scope","MSI virtual persona replacement only; terminal restore unchanged"}});
}
static void msiPersonaModeRejected(const ymcc::msi::persona::Facts& f,const char* reason){
    appendNativeLifecycleLog("msi-claw-gamepad-mode",{{"action","persona-physical-mode-handoff"},{"handoffVersion",1},{"handoffBuild",true},{"phase","ineligible"},{"reason",reason},{"from",f.from},{"to",f.to},{"oldHostEpoch",f.epoch},{"powerGenerationAtCapture",f.power},{"boundPowerGeneration",f.boundPower},{"sourceGeneration",f.sourceGeneration},{"pumpQuiesced",f.pumpQuiesced},{"oldTargetPrepared",f.oldTargetPrepared},{"sourceKnown",f.sourceKnown},{"routeArmed",f.routeArmed},{"routeConfirmed",f.routeConfirmed},{"modeOwned",f.modeOwned},{"powerReady",f.powerReady},{"exitFence",f.exitFence},{"targetWanted",f.targetWanted},{"modeWriteIssued",false},{"deviceModeVerified",false},{"controlAuthority",false},{"scope","MSI persona capture gate only; original lifecycle retained"}});
}
static void msiPersonaModeReset(){
    g_msiPersonaModeHandoff={};
    if(g_msiPersonaPumpHold.owns_lock())g_msiPersonaPumpHold.unlock();
    g_msiPersonaReadPause.store(false,std::memory_order_release);
}
static void msiPersonaModeCapture(const char* reason,bool prepared,const std::string& oldPersona){
    if(!ymcc::msi::persona::buildEnabled||!msiClawKnownBoardAuthority()||!g_msiPersonaStartScope||g_msiPersonaModeHandoff.active)return;
    if(!reason||std::strcmp(reason,"persona-changed")!=0)return;
    std::unique_lock<std::mutex> pump(g_msiPersonaPumpMx,std::try_to_lock);
    if(!pump.owns_lock()){
        ymcc::msi::persona::Facts busy;busy.from=oldPersona;busy.to=inputHostPersona();busy.epoch=inputHostEpochToken();
        msiPersonaModeRejected(busy,"pump-in-flight-no-new-wait");return;
    }
    ymcc::msi::persona::Facts f;HidHideMsiMainSourceRecord source;
    f.enabled=true;f.knownBoard=true;f.startScope=true;f.personaOnly=true;f.pumpQuiesced=true;
    f.targetWanted=g_realStickTest.virtualTargetEnabled;f.oldTargetPrepared=prepared;f.epoch=inputHostEpochToken();f.power=currentPowerGeneration();f.boundPower=g_inputHostBoundPowerGeneration;
    f.routeArmed=g_msiDinputRouteDinputArmed.load();f.routeConfirmed=g_msiDinputRouteConfirmed.load();f.modeOwned=g_msiDinputReturnOwed.load();f.powerReady=hardwareWriteAllowed();f.exitFence=inputHostExitFenceRaised()||g_exitRequested.load();
    f.sourceKnown=msiDinputMainSourceEligible()&&msiDinputMainSourceRecordSnapshot(source)&&source.valid&&source.vendorId==0x0db0&&source.productId==0x1902&&!source.usbInstance.empty();
    f.sourceGeneration=source.generation;f.from=oldPersona;f.to=inputHostPersona();
    if(!ymcc::msi::persona::capture(f)){msiPersonaModeRejected(f,"capture-facts-not-all-proven");return;}
    g_msiPersonaModeHandoff.active=true;g_msiPersonaModeHandoff.facts=f;g_msiPersonaModeHandoff.source=source;
    g_msiPersonaPumpHold=std::move(pump);g_msiPersonaReadPause.store(true,std::memory_order_release);
    msiPersonaModeNote("captured","virtual-target-replacement-not-physical-stop");
}
static bool msiPersonaModeRetain(const char* reason,bool releaseProven,bool visibilityHeld){
    auto& s=g_msiPersonaModeHandoff;
    if(!s.active||s.aborting||!g_msiPersonaStartScope||!reason||std::strcmp(reason,"persona-changed")!=0)return false;
    s.facts.releaseProven=releaseProven;s.facts.visibilityHeld=visibilityHeld;s.facts.quarantined=g_inputHostReleaseQuarantined;
    HidHideMsiMainSourceRecord current;
    s.facts.sourceStillCurrent=msiDinputMainSourceEligible()&&msiDinputMainSourceRecordSnapshot(current)&&current.valid&&current.generation==s.source.generation&&current.interfacePath==s.source.interfacePath&&current.usbInstance==s.source.usbInstance&&inputHostEpochToken()==s.facts.epoch&&currentPowerGeneration()==s.facts.power&&inputHostPersona()==s.facts.to;
    s.facts.targetWanted=g_realStickTest.virtualTargetEnabled;s.facts.powerReady=hardwareWriteAllowed();s.facts.exitFence=inputHostExitFenceRaised()||g_exitRequested.load();
    if(!ymcc::msi::persona::retain(s.facts)){msiPersonaModeNote("not-retained","release-source-or-context-unproven");return false;}
    s.retained=true;msiPersonaModeNote("retained","skip-mode1-and-next-redundant-mode2");return true;
}
static void msiPersonaModeStopDone(bool kept){
    if(!g_msiPersonaModeHandoff.active)return;
    if(!kept){msiPersonaModeNote("cancelled","normal-stop-and-mode1-return");msiPersonaModeReset();}
}
static bool msiPersonaModeResumeOnNewTuple(){
    auto& s=g_msiPersonaModeHandoff;if(!s.active||!s.retained)return true;
    const auto epoch=inputHostEpochToken();
    if(!ymcc::msi::persona::resume(true,g_realStickTest.virtualTargetEnabled&&inputHostPersona()==s.facts.to,hardwareWriteAllowed(),inputHostExitFenceRaised()||g_exitRequested.load(),s.facts.epoch,epoch,s.facts.power,currentPowerGeneration())||g_inputHostBoundPowerGeneration!=s.facts.power||!g_msiDinputRouteDinputArmed.load()||!g_msiDinputReturnOwed.load()||g_msiDinputRouteConfirmed.load()||g_msiDinputSourceValid.load())return false;
    s.newEpoch=epoch;s.resumed=true;
    if(g_msiPersonaPumpHold.owns_lock())g_msiPersonaPumpHold.unlock();
    g_msiPersonaReadPause.store(false,std::memory_order_release);
    msiPersonaModeNote("source-rebind-allowed","new-host-tuple-established-no-old-frame-reuse");return true;
}
static bool msiPersonaModeFinish(){
    auto& s=g_msiPersonaModeHandoff;if(!s.active)return true;
    HidHideMsiMainSourceRecord current;const bool hasSource=msiDinputMainSourceRecordSnapshot(current);
    const bool sourceSafe=!hasSource||(current.valid&&current.vendorId==0x0db0&&current.productId==0x1902&&current.generation!=s.source.generation&&current.usbInstance==s.source.usbInstance);
    const bool ready=s.retained&&s.resumed&&!s.aborting&&s.newEpoch==inputHostEpochToken()&&s.facts.power==currentPowerGeneration()&&s.facts.power==g_inputHostBoundPowerGeneration&&hardwareWriteAllowed()&&!inputHostExitFenceRaised()&&!g_exitRequested.load()&&g_realStickTest.virtualTargetEnabled&&inputHostPersona()==s.facts.to&&g_inputHostPrepared&&g_inputHostNeutralized&&g_msiDinputRouteDinputArmed.load()&&g_msiDinputReturnOwed.load()&&sourceSafe;
    if(!ready)return false;
    msiPersonaModeNote("complete",hasSource?"new-source-generation-and-target-ready":"target-ready-source-pending");msiPersonaModeReset();return true;
}
struct MsiPersonaModeStartScope {
    bool owner=false;
    MsiPersonaModeStartScope(){owner=ymcc::msi::persona::buildEnabled&&msiClawKnownBoardAuthority();if(owner)g_msiPersonaStartScope=true;}
    ~MsiPersonaModeStartScope() noexcept;
};
