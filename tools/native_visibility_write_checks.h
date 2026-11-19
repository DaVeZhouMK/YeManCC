#pragma once
#include <cstdlib>
#include <cstring>
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
// Parameter of the existing production-TU consumer; physical effects remain inert.
static std::pair<unsigned,unsigned> nativeColdReturnWriteChecks(){
    namespace rt=ymcc::inputHost::targetReturnRuntime;
    namespace duty=ymcc::targetReturn;
    namespace continuation=ymcc::inputHost::switchExecutionReturnRuntime;
    unsigned checks=0,failures=0,cycles=0,unhides=0,sequence=0;
    auto check=[&](const char* name,bool ok){++checks;if(!ok)++failures;
        std::cout<<(ok?"PASS ":"FAIL ")<<"native-cold-return: "<<name<<"\n";return ok;};
    const auto directory=fspath::path(app_data_dir())/L"cold-return-canonical-r2";
    if(!check("fresh private fixture directory",directory.is_absolute()&&!fspath::exists(directory)))return {checks,failures};
    fspath::create_directories(directory);
    const std::string root="USB\\VID_045E&PID_028E\\PRIVATE-COLD-R2";
    const std::string hid="HID\\VID_045E&PID_028E&IG_00\\PRIVATE-COLD-R2";
    const std::string tail="HID\\VID_045E&PID_028E&IG_01\\PRIVATE-COLD-R2";
    const std::string other="USB\\VID_045E&PID_028E\\OTHER-CURRENT-ROOT";
    const std::wstring location=L"PCIROOT(0)#PCI(0100)#USBROOT(0)#USB(1)";
    std::vector<std::string> hidden;
    bool rootPresent=true,censusKnown=true,effectsProfileValid=true;
    int poll=-1;
    duty::Origin first,second;
    auto oldIo=g_msiNativeForegroundTestIo;auto oldForegroundIo=g_msiForegroundTestIo;
    auto savedTargetState=std::move(rt::state);
    auto seed=[&](bool reservedOnly=false){
        rt::state=rt::State{};cycles=unhides=0;poll=-1;hidden={root,hid,tail};rootPresent=censusKnown=true;
        if(reservedOnly)hidden.erase(std::remove(hidden.begin(),hidden.end(),root),hidden.end()); // tail remains
        g_msiNativeForegroundLease=MsiNativeForegroundLease{};
        {std::lock_guard identity(g_hidHidePhysicalIdentityMx);g_hidHidePhysicalIdentity=HidHidePhysicalIdentity{};}
        g_msiNativeForegroundCyclePending=false;g_postHideCycleOwned=false;g_coldReturnCycleOwned=false;
        g_machineIdentity.family=YmccFamily::Gpd;g_machineIdentity.manufacturer="GPD";
        g_machineIdentity.product="G1618-05";g_machineIdentity.resolved=true;
        g_inputHostProcess=nullptr;g_inputHostHello=g_inputHostPrepared=g_inputHostPrepareMayHaveReachedHost=false;
        g_inputHostNeutralized=g_inputHostActive=false;g_inputHostEpoch=0;g_hidHideVisibilityJournal={};
        g_inputHostTransportFault=g_inputHostPipeRetired=g_inputHostReleaseQuarantined=false;
        g_inputHostExitFence=g_inputCaptureStop=g_exitRequested=false;g_inputHostPowerAdmissionRevoked=false;
        g_inputReady=true;g_hardwareWriteGate=true;g_powerGeneration=3;g_powerLifecycle=PowerLifecycle::Ready;
        g_msiNativeForegroundPowerHold=false;g_inputHostVirtualIntentWriters=0;
        g_externalPriorityVirtualSuspended=g_shortcutRecordingActive=false;
        g_physicalReleaseWindowNeutral=false;g_physicalReleaseWindowStage=0;g_physicalReleaseWindowTransition=0;
        g_msiSteamDeckReleaseDebt.clear();g_psEndpointOwnership.clear();
        g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=false;g_realStickTest.virtualTargetPersona="disabled";
        g_virtualStartProtection.~VirtualStartProtection();new (&g_virtualStartProtection) ymcc::VirtualStartProtection();
        const auto ticket=g_virtualStartProtection.admit("disabled",inputHostStartProtectionNow()).requestId;
        g_inputHostStartProtectionBoundTicket=ticket;
        g_inputHostSwitchRecoveryStartDeadline=inputHostStartProtectionNow()+30000;
        g_inputHostSwitchRecoveryStartRequestId=ticket;g_inputHostSwitchRecoveryStartPower=3;
        continuation::state={};g_inputHostSwitchExecutionReturnPending=false;
        ymcc::inputHost::nativeVisibilityReturn::pending=false;
        g_msiNativeForegroundTestIo={};
        g_msiNativeForegroundTestIo.coldWriterDead=[](const auto&){return true;};
        g_msiNativeForegroundTestIo.coldWritersQuiet=[](){return true;};
        g_msiNativeForegroundTestIo.coldCensus=[&](const std::vector<std::string>& ids){
            if(!censusKnown)return 2;return !ids.empty()&&rootPresent?1:0;};
        g_msiNativeForegroundTestIo.coldHidden=[&](auto& out){out=hidden;return true;};
        g_msiNativeForegroundTestIo.coldUnhide=[&](const auto& id){++unhides;
            hidden.erase(std::remove(hidden.begin(),hidden.end(),id),hidden.end());return true;};
        g_msiNativeForegroundTestIo.coldPathProven=[&](const auto& r,const auto&){return r==root;};
        g_msiForegroundTestIo.locations=[&](const auto& r){return rootPresent&&r==root?std::vector<std::wstring>{location}:std::vector<std::wstring>{};};
        g_msiNativeForegroundTestIo.xusbInventory=[&](const auto&){MsiNativeForegroundXusbInventory x;x.complete=true;
            ymcc::msi::NativeForegroundXusbInterface i;i.activeInterface=i.ancestorMatched=true;
            i.instanceId=i.ancestorRoot=rootPresent?root:other;x.interfaces={i};return x;};
        g_msiNativeForegroundTestIo.postHideRootStarted=[&](const auto& r){return rootPresent&&r==root;};
        g_msiNativeForegroundTestIo.xusbRefresh=[&](const auto& source,const auto& port){++cycles;
            effectsProfileValid=effectsProfileValid &&
                g_msiNativeForegroundLease.profile==ymcc::nativeForeground::Profile::Gpd &&
                g_msiNativeForegroundLease.coldReturnCycle && !g_msiNativeForegroundLease.nativeRootPortCycle &&
                !g_msiNativeForegroundLease.postHideCycle && g_msiNativeForegroundLease.owned.empty() &&
                source.deviceInstanceId==root && port==location && hidden.empty();return 1;};
        g_msiNativeForegroundTestIo.poll=[&](){return poll;};g_msiNativeForegroundTestIo.queue=[](){};
        g_msiNativeForegroundTestIo.virtualQueue=[](const auto&){};
        const auto data=directory/std::to_wstring(++sequence);fspath::create_directories(data);
        rt::state.session.reset(new duty::Session(duty::Session::open((data/L"target.json").wstring(),"windows-machine-guid:private-canonical-r2")));
        if(!check("real private Session ready",rt::state.session->ready()))return uint64_t(0);
        first={};first.transactionId="cold-root";first.parent={710131,"132000000000000131"};first.host={710132,"132000000000000132"};
        first.hostInstanceId="old-host";first.runId="old-run";first.targetId="old-target";first.persona="steamdeck";
        first.epoch=1;first.cliWriterProtocol=1;first.physicalRoot=root;second=first;second.transactionId="reserved-tail";
        auto& session=*rt::state.session;
        bool saved=reservedOnly?session.reservePath(session.record()->revision,first,root,{true,true,true}):
            session.reserveNativeOwnedSet(session.record()->revision,first,{root,hid},{true,true,true});
        saved=session.reservePath(session.record()->revision,second,tail,{true,true,true})&&saved;
        check("real two-origin Reserved record from actual reservation API",saved&&session.record()->obligations.size()==2 &&
            std::all_of(session.record()->obligations.begin(),session.record()->obligations.end(),[](const auto& group){
                return std::all_of(group.paths.begin(),group.paths.end(),[](const auto& path){return path.stage==duty::DispatchStage::Reserved;});}));
        rt::state.coldHistoryPending=true;return ticket;
    };
    auto run=[&](uint64_t ticket){std::lock_guard host(g_inputHostMx);
        return inputHostTargetReturnColdPassLocked(ticket,currentPowerGeneration(),"disabled");};
    auto ticket=seed();
    auto result=run(ticket);
    check("same-root Reserved multiOrigin observed hidden root refreshes once; Reserved tail retained",
        !result.clean()&&cycles==1&&hidden.empty()&&rt::state.session->record()->obligations.size()==2);
    {
        std::lock_guard host(g_inputHostMx);
        rt::ColdEnvironment env{ticket,currentPowerGeneration(),"disabled"};
        check("ownPending current and poll do not block themselves",env.current()&&env.pollOwnColdRefresh()==1);
        check("same ticket pending arms existing continuation",inputHostSwitchExecutionBeginColdLocked(ticket,duty::coldExecution::Reason::ContextRetired));
        continuation::state.queued=true;
    }
    const auto passes=continuation::state.coldPasses;
    inputHostPerformSwitchExecutionReturn(ticket);
    check("ownPending observation charges no failure pass",continuation::state.coldPasses==passes&&cycles==1);
    poll=0;result=run(ticket);
    check("terminal original helper allows original full batch settlement",result.clean()&&cycles==1&&rt::state.session->record()->obligations.empty());
    ticket=seed(true);result=run(ticket);
    check("onlyReserved with root never hidden returns locally without a helper",result.clean()&&cycles==0);
    ticket=seed();rootPresent=false;result=run(ticket);
    check("complete census noPresentRoot clears old exact keys with zero cycle",result.clean()&&hidden.empty()&&cycles==0);
    ticket=seed();
    {std::lock_guard identity(g_hidHidePhysicalIdentityMx);g_hidHidePhysicalIdentity.resolved=true;
        g_hidHidePhysicalIdentity.baseContainerDeviceInstanceId=other;}
    result=run(ticket);
    check("currentDifferentRoot preserves old local return without refreshing either root",result.clean()&&cycles==0);
    ticket=seed();censusKnown=false;result=run(ticket);
    check("unknown census preserves duty and issues no helper",!result.clean()&&cycles==0&&!rt::state.session->record()->obligations.empty());
    ticket=seed();auto& owner=g_msiNativeForegroundLease;owner.active=true;owner.profile=ymcc::nativeForeground::Profile::Msi;owner.root="FOREIGN-OWNER";
    result=run(ticket);check("foreign-owner is not bypassed or replaced",!result.clean()&&cycles==0&&owner.active&&owner.root=="FOREIGN-OWNER");owner.active=false;
    ticket=seed();result=run(ticket);const auto oldIntent=g_inputHostVirtualIntentRevision.fetch_add(1);
    {
        std::lock_guard host(g_inputHostMx);std::lock_guard native(g_msiNativeForegroundMx);
        check("cancel retains original pending helper",!msiColdReturnCleanupLocked("fixture-intent-changed")&&owner.active&&owner.syntheticCycle);
        poll=0;check("cancel terminal clears lease retaining spent receipt",msiColdReturnCleanupLocked("fixture-terminal")&&!owner.active&&owner.coldReturnReceipt.spent);
        const auto refreshed=msiColdReturnCycleStepLocked(ticket,currentPowerGeneration(),oldIntent+1,g_inputHostSwitchRecoveryStartDeadline,root);
        check("same ticket changed intent cannot refund/re-dispatch",refreshed==2&&cycles==1);
    }
    // Aggregate the same original predicate across every actual effect entry;
    // the exact group stays 34 checks even when fixed reachability enters twice.
    check("fresh lease is GPD before effect and has no old native state",effectsProfileValid);
    // Real LaunchScope reservation with deliberately missing creation-return.
    // No helper process is started. It cannot be retired/cleared as Job-complete.
    ticket=seed();
    {
        std::lock_guard host(g_inputHostMx);std::lock_guard native(g_msiNativeForegroundMx);
        owner.active=true;owner.profile=ymcc::nativeForeground::Profile::Gpd;owner.coldReturnCycle=true;
        owner.coldReturnTicket=ticket;owner.power=3;owner.coldReturnIntent=g_inputHostVirtualIntentRevision;
        owner.coldReturnDeadline=g_inputHostSwitchRecoveryStartDeadline;owner.root=root;owner.locations={location};g_coldReturnCycleOwned=true;
        const auto writer=directory/L"job-not-retired.json";
        owner.nativeRefreshWriter=std::make_unique<ymcc::nativeRefreshWriter::LaunchScope>(writer.wstring(),"windows-machine-guid:private-canonical-r2",root,root,std::vector<std::string>{W2U(location)});
        STARTUPINFOW startup{};startup.cb=sizeof(startup);
        auto& scope=*owner.nativeRefreshWriter;
        const bool tracked=scope.ready() && scope.job().prepareTracked(startup,scope.jobName(),{scope.inheritedLease()});
        check("real LaunchScope reserve",tracked && scope.job().idle() &&
            scope.job().tracksExactLease(scope.jobName(),scope.inheritedLease()) && scope.reserve());
        g_msiNativeForegroundCyclePending=true;
        check("JobNotRetired missing creation receipt retains exact writer",!msiColdReturnCleanupLocked("fixture-job-unresolved")&&owner.active&&owner.nativeRefreshWriter);
        // This test owns the known no-create attempt; acknowledge that exact fact.
        owner.nativeRefreshWriter->creationReturned(nullptr);
        // Successful cleanup destroys the original LaunchScope; its original
        // JobGuard destructor/reset closes attribute lists and Job. Never reset
        // a still-unretired writer merely to make the test pass.
        check("known no-create retirement permits cleanup",msiColdReturnCleanupLocked("fixture-no-create-confirmed")&&
            !owner.active&&!owner.nativeRefreshWriter);
    }
    rt::state=rt::State{};rt::state=std::move(savedTargetState);g_msiNativeForegroundLease=MsiNativeForegroundLease{};
    g_msiNativeForegroundCyclePending=false;g_coldReturnCycleOwned=false;g_postHideCycleOwned=false;
    g_msiNativeForegroundTestIo=oldIo;g_msiForegroundTestIo=oldForegroundIo;
    return {checks,failures};
}
#endif

// Additional parameters of the existing native foreground consumer. Real
// private Session/store and actual reserve/issue/return/clear functions execute.
static std::pair<unsigned,unsigned> nativeVisibilityWriteChecks(const char* selector=std::getenv("YMCC_NATIVE_VISIBILITY_CASE")){
    if(selector){
        if(std::strcmp(selector,"cold-return")==0)return nativeColdReturnWriteChecks();
        std::cout<<"FAIL native-write-ahead: unknown exact selector\n";return {0,1};
    }
    unsigned checks=0,failures=0;
    auto check=[&](const char* name,bool ok){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<"native-write-ahead: "<<name<<"\n";};
    using namespace ymcc::nativeRefreshWriter;
    const std::string root="USB\\VID_045E&PID_028E\\PRIVATE-OWNED-ROOT";
    const std::string hid="HID\\VID_045E&PID_028E&IG_01\\PRIVATE-OWNED-HID";
    g_machineIdentity.family=YmccFamily::Gpd;g_machineIdentity.manufacturer="GPD";g_machineIdentity.product="G1617-01";g_machineIdentity.resolved=true;
    g_msiNativeForegroundLease=MsiNativeForegroundLease{};g_msiNativeForegroundCyclePending=false;
    g_msiNativeForegroundTestIo={};g_msiNativeForegroundTestIo.nativeVisibilityPersistence=true;
    auto& lease=g_msiNativeForegroundLease;lease.profile=ymcc::nativeForeground::Profile::Gpd;lease.root=root;
    lease.locations={L"PCIROOT(0)#PCI(0100)#USBROOT(0)#USB(1)"};
    HidHidePhysicalIdentity source;source.resolved=true;source.deviceInstanceId=hid;source.baseContainerDeviceInstanceId=root;
    HidHideVisibilitySnapshot before;before.valid=true;before.cloaking=true;
    check("real production reserve commits both future paths",msiNativeForegroundDurableHide(source,{root,hid},before));
    auto session=lease.nativeVisibilitySession;
    check("native lease retains exact same durable owner",session&&session->ready()&&session->record()->nativeVisibility&&lease.active);
    if(!session||!session->record()->nativeVisibility)return {checks,failures+1};
    check("reserve commits no issued path",std::all_of(session->record()->nativeVisibility->paths.begin(),session->record()->nativeVisibility->paths.end(),[](const auto& p){return p.phase==VisibilityPathPhase::Reserved;}));
    check("actual issue throat commits root mark",msiNativeForegroundBeforeHideDispatch(root));
    check("unknown path cannot get an issue mark",!msiNativeForegroundBeforeHideDispatch("HID\\VID_045E&PID_028E\\UNRESERVED"));
    msiNativeForegroundClearLeaseLocked();check("clear cannot discard unresolved visibility",lease.active&&lease.nativeVisibilitySession==session);
    HidHideVisibilitySnapshot stillHidden=before;stillHidden.hidden={U2W(root),U2W(hid)};
    check("actual readback still hidden cannot settle issued duty",!msiNativeForegroundDurableReturn(stillHidden)&&session->record()->nativeVisibility->paths.size()==2);
    HidHideVisibilitySnapshot returned=before;returned.hidden={U2W(hid)};
    check("issued root returned; unissued reserved leaf never authorizes unhide",msiNativeForegroundDurableReturn(returned)&&!session->record()->nativeVisibility);
    check("schema2 retained to prevent old-log remigration",session->record()->schemaVersion==kVisibilitySchemaVersion);
    session.reset();msiNativeForegroundClearLeaseLocked();check("settled owner clears without a competing Session",!lease.active&&!lease.nativeVisibilitySession);
    lease.profile=ymcc::nativeForeground::Profile::Gpd;lease.root=root;lease.locations={L"PCIROOT(0)#PCI(0100)#USBROOT(0)#USB(1)"};
    check("next native period reserves on the same durable file",msiNativeForegroundDurableHide(source,{root},before));
    session=lease.nativeVisibilitySession;if(!session)return {checks,failures+1};
    check("next actual dispatch mark",msiNativeForegroundBeforeHideDispatch(root));
    const auto parent=ymcc::targetReturn::process::capture(GetCurrentProcess());
    Attempt attempt;attempt.attemptId="fixture-helper-not-created";attempt.parent=*parent;
    attempt.jobName=std::string(jobPrefix)+attempt.attemptId;attempt.physicalRoot=root;attempt.deviceInstanceId=root;
    attempt.locationPaths={"PCIROOT(0)#PCI(0100)#USBROOT(0)#USB(1)"};
    check("same Session records helper reservation without reopening owner",session->reserve(session->record()->revision,attempt));
    check("configuration return succeeds before helper retirement",msiNativeForegroundDurableReturn(before)&&session->record()->nativeVisibility&&session->record()->nativeVisibility->paths.empty());
    msiNativeForegroundClearLeaseLocked();check("clear retains empty visibility envelope while helper duty lives",lease.active&&session->record()->active.has_value());
    // This fixture itself performed no CreateProcess, and still owns the exact
    // pre-create reservation; no unknown/dead external producer is settled here.
    check("actual known pre-create reservation settles",session->settle(session->record()->revision,attempt.attemptId));
    session.reset();msiNativeForegroundClearLeaseLocked();check("helper retirement permits final durable clear",!lease.active&&!lease.nativeVisibilitySession);
    // Actual R3 bridge and common cold dispatcher; only physical IO is inert.
    g_inputHostProcess=nullptr;g_inputHostEpoch=0;g_inputHostHello=false;g_inputHostPrepared=false;
    g_inputHostNeutralized=false;g_inputHostActive=false;g_inputHostReleaseQuarantined=false;
    g_hidHideVisibilityJournal=HidHideVisibilityJournal{};g_inputHostTransportFault=false;
    g_inputCaptureStop=false;g_exitRequested=false;g_inputHostExitFence=false;
    g_inputReady=true;g_powerGeneration=3;g_powerLifecycle=PowerLifecycle::Ready;
    g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="steamdeck";
    g_virtualStartProtection.~VirtualStartProtection();new (&g_virtualStartProtection) ymcc::VirtualStartProtection();
    ymcc::inputHost::switchExecutionReturnRuntime::state={};g_inputHostSwitchExecutionReturnPending=false;
    ymcc::inputHost::nativeVisibilityReturn::pending=false;
    auto ticket=g_virtualStartProtection.admit("steamdeck",inputHostStartProtectionNow()).requestId;
    unsigned deferredCalls=0;
    g_msiNativeForegroundTestIo.coldReturn=[&](const char*){++deferredCalls;ymcc::inputHost::nativeVisibilityReturn::pending=true;return false;};
    {
        std::lock_guard<InputHostMutex> host(g_inputHostMx);std::lock_guard native(g_msiNativeForegroundMx);
        check("failed native return arms original cold R3",!inputHostNativeVisibilityColdReturnOrQueueLocked("fixture-return-pending") &&
            ymcc::inputHost::switchExecutionReturnRuntime::state.pending && ymcc::inputHost::switchExecutionReturnRuntime::state.requestId==ticket);
        const auto deadline=ymcc::inputHost::switchExecutionReturnRuntime::state.deadline;
        const auto passes=ymcc::inputHost::switchExecutionReturnRuntime::state.coldPasses;
        for(unsigned n=0;n<1000;++n)(void)inputHostNativeVisibilityColdReturnOrQueueLocked("fixture-same-return");
        check("same-owner source storm performs no additional cold IO",deferredCalls==1);
        check("repeat native defer renews neither deadline nor passes",deadline==ymcc::inputHost::switchExecutionReturnRuntime::state.deadline &&
            passes==ymcc::inputHost::switchExecutionReturnRuntime::state.coldPasses);
        check("native debt is visible to original history gate",!inputHostTargetReturnHistorySettledLocked());
    }
    for(unsigned n=0;n<10;++n)(void)inputHostPerformColdStartupReturn();
    check("startup storm defers to the same cold owner",deferredCalls==1);
    unsigned continued=0;
    g_msiNativeForegroundTestIo.coldReturn=[&](const char*){++continued;ymcc::inputHost::nativeVisibilityReturn::pending=false;return true;};
    {
        std::lock_guard<InputHostMutex> host(g_inputHostMx);
        const auto result=inputHostTargetReturnColdPassLocked(ticket,currentPowerGeneration(),"steamdeck");
        check("actual common cold pass consumes native responsibility first",result.clean()&&continued==1&&inputHostTargetReturnHistorySettledLocked());
        check("cold configuration return creates no Host",!g_inputHostProcess&&!g_inputHostHello&&!g_hidHideVisibilityJournal.active);
        g_realStickTest.virtualTargetEnabled=false;g_realStickTest.virtualTargetPersona="disabled";g_virtualStartProtection.cancel(inputHostStartProtectionNow());
        check("OFF retires the old continuation instead of reopening ON",!inputHostSwitchExecutionAwaitingLocked(ticket)&&
            !ymcc::inputHost::switchExecutionReturnRuntime::state.pending&&!g_inputHostProcess);
    }
    // Real HandleWork + real cold entry, with device effect leaves inert.
    // This is not actual SDK creation or external game-input acceptance.
    unsigned liveReads=0,liveWrites=0,liveDiscoveries=0;
    g_msiNativeForegroundLease=MsiNativeForegroundLease{};
    g_msiNativeForegroundRetry=MsiNativeForegroundRetry{};
    g_msiNativeForegroundCyclePending=false;g_postHideCycleOwned=false;
    g_msiNativeForegroundObserverActive=true;g_controllerFrontendIsolationEnabled=true;
    g_msiNativeForegroundPowerHold=false;g_msiNativeForegroundEdge=23;
    g_inputHostPowerAdmissionRevoked=false;g_inputHostVirtualIntentWriters=0;
    g_msiNativeForegroundTestIo={};g_msiNativeForegroundTestIo.nativeVisibilityPersistence=true;
    g_msiNativeForegroundTestIo.ui=[](){return true;};
    g_msiNativeForegroundTestIo.snapshot=[&](auto&){++liveReads;return false;};
    g_msiNativeForegroundTestIo.visibility=[&](const auto&,bool){++liveWrites;return false;};
    g_msiNativeForegroundTestIo.cloak=[&](bool){++liveWrites;return false;};
    g_msiNativeForegroundTestIo.candidates=[&](){++liveDiscoveries;return std::vector<HidHidePhysicalIdentity>{};};
    g_msiNativeForegroundTestIo.queue=[](){};
    g_inputHostProcess=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(1));
    g_inputHostEpoch=4;g_inputHostHello=g_inputHostPrepared=g_inputHostActive=g_inputHostNeutralized=true;
    g_hidHideVisibilityJournal.active=true;
    for(const auto* persona:{"steamdeck","dualsense-edge","elite"}){
        g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona=persona;
        g_virtualStartProtection.~VirtualStartProtection();new (&g_virtualStartProtection) ymcc::VirtualStartProtection();
        (void)g_virtualStartProtection.admit(persona,inputHostStartProtectionNow());
        ymcc::inputHost::switchExecutionReturnRuntime::state={};g_inputHostSwitchExecutionReturnPending=false;
        ymcc::inputHost::nativeVisibilityReturn::pending=false;
        for(unsigned n=0;n<1000;++n)msiNativeForegroundHandleWork();
        const std::string label=std::string("live ")+persona+" UI storm creates no native debt or cold ticket";
        check(label.c_str(),!inputHostNativeVisibilityHistoryPending() &&
            !ymcc::inputHost::switchExecutionReturnRuntime::state.pending && !g_inputHostSwitchExecutionReturnPending &&
            g_inputHostProcess && g_inputHostEpoch==4 && g_inputHostActive && g_hidHideVisibilityJournal.active);
    }
    check("live persona UI never reads or writes visibility nor discovers sources",liveReads==0&&liveWrites==0&&liveDiscoveries==0);
    // Drop the inert handle before any cleanup/OS handle consumer can run.
    g_inputHostProcess=nullptr;g_inputHostEpoch=0;
    g_inputHostHello=g_inputHostPrepared=g_inputHostActive=g_inputHostNeutralized=false;
    g_hidHideVisibilityJournal=HidHideVisibilityJournal{};
    g_realStickTest.virtualTargetEnabled=false;g_realStickTest.virtualTargetPersona="disabled";
    g_virtualStartProtection.~VirtualStartProtection();new (&g_virtualStartProtection) ymcc::VirtualStartProtection();
    ymcc::inputHost::switchExecutionReturnRuntime::state={};g_inputHostSwitchExecutionReturnPending=false;
    ymcc::inputHost::nativeVisibilityReturn::pending=true;
    unsigned debtCalls=0;
    g_msiNativeForegroundTestIo.coldReturn=[&](const char*){
        ++debtCalls;ymcc::inputHost::nativeVisibilityReturn::pending=false;return true;};
    msiNativeForegroundHandleWork();
    check("native OFF can enter cold observer despite its own known history",debtCalls==1&&!inputHostNativeVisibilityHistoryPending());
    check("native OFF rechecks facts after return before original source admission",liveDiscoveries==1&&liveWrites==0);
    g_msiNativeForegroundObserverActive=false;
    g_msiNativeForegroundLease=MsiNativeForegroundLease{};
    g_msiNativeForegroundTestIo={};
    return {checks,failures};
}
