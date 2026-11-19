#pragma once
#ifndef YMCC_MSI_FOREGROUND_ISOLATION_TEST
#error This private consumer requires the existing production-TU offline effect ports.
#endif
// Supplied BEFORE this header by the owner as a pinned exact baseline function slice.
static int msiPostHideCycleStepLegacy(uint64_t);
static std::pair<unsigned,unsigned> gpdNativeHandoffCycleChecks(bool legacySkip=false,bool partialBefore=false){
    namespace rt=ymcc::inputHost::targetReturnRuntime;
    namespace duty=ymcc::targetReturn;
    namespace nw=ymcc::nativeRefreshWriter;
    unsigned checks=0,failures=0,refreshes=0,writes=0;
    auto check=[&](const char* name,bool ok){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<"gpd-handoff-cycle: "<<name<<"\n";return ok;};
    const auto root=fspath::path(app_data_dir())/(legacySkip?L"handoff-cycle-before":L"handoff-cycle-current");
    // Main task must already bind app_data_dir to its short, owned offline root.
    if(!check("short-fresh-private-data-root",root.is_absolute()&&root.wstring().size()<=120&&!fspath::exists(root)))return {checks,failures};
    if(!check("no-incoming-host-or-native-writer",!g_inputHostProcess&&!g_msiNativeForegroundCyclePending&&!g_msiNativeForegroundLease.cycleProcess&&!g_msiNativeForegroundLease.nativeRefreshWriter))return {checks,failures};
    fspath::create_directories(root);
    auto savedTargetState=std::move(rt::state);
    const std::string physicalRoot="USB\\VID_045E&PID_028E\\PRIVATE-HANDOFF";
    const std::string hid="HID\\VID_045E&PID_028E&IG_01\\PRIVATE-HID";
    const std::string oldHid="HID\\VID_045E&PID_028E&IG_00\\PRIVATE-OLD-HID";
    const std::wstring location=L"PCIROOT(0)#PCI(0100)#USBROOT(0)#USB(1)";
    std::vector<std::wstring> hidden;
    auto cleanup=[&](){
        // Borrowed self process pseudo handle is identity input, NEVER Stop/CloseHandle.
        g_inputHostProcess=nullptr;
        g_msiNativeForegroundLease=MsiNativeForegroundLease{};
        g_msiNativeForegroundCyclePending=false;g_postHideCycleOwned=false;
        g_hidHideVisibilityJournal=HidHideVisibilityJournal{};
        rt::state=rt::State{};
    };
    auto setup=[&](unsigned index,bool donor,bool replacement=false){
        cleanup();refreshes=writes=0;
        g_machineIdentity.family=YmccFamily::Gpd;g_machineIdentity.manufacturer="GPD";g_machineIdentity.product="G1617-01";g_machineIdentity.resolved=true;
        g_inputReady=true;g_hardwareWriteGate=true;g_powerGeneration=3;g_powerLifecycle=PowerLifecycle::Ready;
        g_inputCaptureStop=false;g_exitRequested=false;g_inputHostExitFence=false;g_inputHostPowerAdmissionRevoked=false;
        g_inputHostTransportFault=false;g_inputHostPipeRetired=false;g_inputHostReleaseQuarantined=false;
        g_inputHostPrepared=g_inputHostPrepareMayHaveReachedHost=g_inputHostActive=g_inputHostNeutralized=false;
        g_inputHostHello=true;g_inputHostEpoch=81;g_inputHostHostInstanceId="private-self-identity";
        g_inputHostRunId="handoff-cycle-"+std::to_string(index);g_inputHostBoundPersona="elite";
        g_inputHostStartBlocked=false;g_inputHostFaultHoldsStart=false;g_inputHostSwitchRecoveryStartDeadline=0;
        g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="elite";
        g_msiNativeForegroundPowerHold=false;
        g_virtualStartProtection.~VirtualStartProtection();new (&g_virtualStartProtection) ymcc::VirtualStartProtection();
        const auto ticket=g_virtualStartProtection.admit("elite",inputHostStartProtectionNow()).requestId;
        g_inputHostStartProtectionBoundTicket=ticket;
        HidHidePhysicalIdentity source;source.resolved=true;source.deviceInstanceId=hid;source.baseContainerDeviceInstanceId=physicalRoot;
        source.vendorId=0x045e;source.productId=0x028e;source.usagePage=1;source.usage=5;
        hidden={U2W(physicalRoot),U2W(hid)};
        if(replacement)hidden.push_back(U2W(oldHid));
        g_msiForegroundTestIo.locations=[&](const std::string& r){return r==physicalRoot?std::vector<std::wstring>{location}:std::vector<std::wstring>{};};
        g_msiNativeForegroundTestIo={};g_msiNativeForegroundTestIo.nativeVisibilityPersistence=true;
        g_msiNativeForegroundTestIo.snapshot=[&](HidHideVisibilitySnapshot& s){s.valid=true;s.cloaking=true;s.inverseRaw="off";s.applications={hidHideCurrentModulePath()};s.hidden=hidden;return true;};
        g_msiNativeForegroundTestIo.visibility=[&](const std::wstring&,bool){++writes;return false;};
        g_msiNativeForegroundTestIo.xusbInventory=[&](const HidHidePhysicalIdentity&){MsiNativeForegroundXusbInventory x;x.complete=true;ymcc::msi::NativeForegroundXusbInterface e;
            e.activeInterface=e.ancestorMatched=true;e.instanceId=e.ancestorRoot=physicalRoot;x.interfaces={e};return x;};
        g_msiNativeForegroundTestIo.postHideRootStarted=[&](const std::string& r){return r==physicalRoot;};
        g_msiNativeForegroundTestIo.xusbRefresh=[&](const HidHidePhysicalIdentity& s,const std::wstring& p){
            check("effect-port-exact-root-location",s.deviceInstanceId==physicalRoot&&s.baseContainerDeviceInstanceId==physicalRoot&&p==location);++refreshes;return 1;};
        g_msiNativeForegroundTestIo.gameRefresh=[&](const HidHidePhysicalIdentity&,const std::wstring&){check("no-replacement-game-leaf-refresh",false);return 2;};
        g_msiNativeForegroundTestIo.poll=[](){return -1;};
        g_msiNativeForegroundTestIo.postHideSource=[](){return ymcc::input::recoveryHealth::Source{};};
        g_msiNativeForegroundTestIo.virtualQueue=[](const std::string&){};
        auto& journal=g_hidHideVisibilityJournal;journal.active=true;journal.transactionId=g_inputHostRunId;
        journal.physical=source;journal.physicalSources={source};journal.hiddenByUs=hidden;
        journal.before.valid=true;journal.before.cloaking=true;journal.before.inverseRaw="off";
        journal.before.applications={hidHideCurrentModulePath()};journal.before.hidden=hidden;
        const auto dir=root/std::to_wstring(index);fspath::create_directories(dir);
        rt::state.session.reset(new duty::Session(duty::Session::open((dir/L"target.json").wstring(),"fixture-owned-machine")));
        if(!check("real-target-session-open",rt::state.session->ready()))return uint64_t(0);
        if(donor){
            auto& s=g_msiNativeForegroundLease;s.active=true;s.profile=ymcc::nativeForeground::Profile::Gpd;s.root=physicalRoot;
            s.locations={location};s.power=currentPowerGeneration();s.owned=hidden;s.diagnosticSelectedPathsComplete=true;
            s.virtualHandoffHeld=true;s.virtualHandoffRequest=ticket;s.before.valid=true;s.before.cloaking=true;
            s.nativeVisibilitySession=std::shared_ptr<nw::Session>(new nw::Session(nw::Session::open((dir/L"native.json").wstring(),"fixture-owned-machine")));
            auto ns=s.nativeVisibilitySession;const auto parent=duty::process::capture(GetCurrentProcess());
            if(!check("real-native-session-and-parent-identity",ns&&ns->ready()&&parent.has_value()))return uint64_t(0);
            nw::Visibility v;v.transactionId="private-native-"+std::to_string(index);v.parent=*parent;v.physicalRoot=physicalRoot;v.locationPaths={W2U(location)};
            v.paths={{physicalRoot,nw::VisibilityPathPhase::Reserved},{hid,nw::VisibilityPathPhase::Reserved}};
            if(replacement)v.paths.push_back({oldHid,nw::VisibilityPathPhase::Reserved});
            if(!check("real-native-reserve",ns->reserveVisibility(ns->record()->revision,v)))return uint64_t(0);
            bool marked=true;for(const auto& path:v.paths)marked=ns->markVisibilityMayHaveIssued(ns->record()->revision,v.transactionId,path.instanceId)&&marked;
            check("real-native-both-paths-marked",marked);s.nativeVisibilityTransaction=v.transactionId;
        }
        g_inputHostProcess=GetCurrentProcess(); // actual capture in rt::currentOwner; no SDK/target created
        return ticket;
    };
    auto reserve=[&](bool tail,bool foreignTail){
        auto owner=rt::currentOwner();if(!check("real-current-owner-parent-and-host-birth",owner.has_value()))return false;
        owner->physicalRoot=physicalRoot;owner->transactionId="private-hosted";
        const auto paths=tail?std::vector<std::string>{physicalRoot,hid}:std::vector<std::string>{physicalRoot};
        const bool committed=rt::state.session->reserveNativeOwnedSet(rt::state.session->record()->revision,*owner,paths,{true,true,true});
        check("direct-ledger-reservation-is-fixture-precondition",committed);
        if(foreignTail){owner->runId+="-foreign";owner->transactionId+="-foreign";
            check("foreign-flow-tail-really-persisted",rt::state.session->reserveNativeOwnedSet(rt::state.session->record()->revision,*owner,{hid},{true,true,true}));}
        return committed;
    };
    auto accept=[&](){HidHideVisibilitySnapshot after;(void)msiNativeForegroundSnapshot(after);return msiNativeForegroundAcceptVirtualJournal(after);};
    auto step=[&](uint64_t ticket){return partialBefore?msiPostHideCycleStepPartialBefore(ticket):legacySkip?msiPostHideCycleStepLegacy(ticket):msiPostHideCycleStep(ticket);};
    if(!partialBefore){
        std::lock_guard<InputHostMutex> host(g_inputHostMx);
        const auto ticket=setup(0,true);auto native=g_msiNativeForegroundLease.nativeVisibilitySession;
        if(ticket&&reserve(true,false)){
            check("true-Accept-validates-all-paths-and-transfers-native-session",accept()&&native&&!native->record()->nativeVisibility);
            const auto& j=g_hidHideVisibilityJournal;
            check("stamp-created-only-after-durable-transfer",j.nativeHandoffCycleRequest==ticket&&j.nativeHandoffCyclePower==currentPowerGeneration()&&j.nativeHandoffCycleEpoch==g_inputHostEpoch);
            const int result=step(ticket);
            // REQUIRED historical negative: baseline returns 0 due to before.hidden.
            check("authenticated-native-handoff-must-start-original-posthide-cycle",result==1&&refreshes==1&&g_msiNativeForegroundCyclePending&&g_msiNativeForegroundLease.postHideCycle);
            if(!legacySkip)check("stamp-consumed-once-without-extra-hide",!j.nativeHandoffCycleRequest&&!j.nativeHandoffCyclePower&&!j.nativeHandoffCycleEpoch&&writes==0);
        }
        cleanup();
    }
    if(!legacySkip&&!partialBefore){
        // Missing/partial/foreign Hosted responsibility cannot mint a refresh stamp.
        for(unsigned mode=0;mode<4;++mode){std::lock_guard<InputHostMutex> host(g_inputHostMx);
            const auto ticket=setup(mode+1,true);auto native=g_msiNativeForegroundLease.nativeVisibilitySession;
            if(mode==1||mode==2)(void)reserve(false,mode==2);if(mode==3)(void)reserve(true,false);
            if(mode==3&&native){nw::Attempt a;a.attemptId="fixture-reserved-helper";a.parent=*duty::process::capture(GetCurrentProcess());a.jobName=std::string(nw::jobPrefix)+a.attemptId;
                a.physicalRoot=physicalRoot;a.deviceInstanceId=physicalRoot;a.locationPaths={W2U(location)};check("real-active-native-helper-reservation-blocks-transfer",native->reserve(native->record()->revision,a));}
            if(ticket)check("negative-Accept-retains-native-duty-and-mints-no-stamp",!accept()&&native&&native->record()->nativeVisibility&&
                !g_hidHideVisibilityJournal.nativeHandoffCycleRequest&&!g_hidHideVisibilityJournal.nativeHandoffCyclePower&&!g_hidHideVisibilityJournal.nativeHandoffCycleEpoch&&refreshes==0&&writes==0);
            cleanup();
        }
    }
    if(!legacySkip){
        for(unsigned mode=(partialBefore?6u:0u);mode<(partialBefore?7u:8u);++mode){std::lock_guard<InputHostMutex> host(g_inputHostMx);
            const auto ticket=setup(mode+10,false);auto& j=g_hidHideVisibilityJournal;
            if(mode>=2){j.nativeHandoffCycleRequest=ticket;j.nativeHandoffCyclePower=currentPowerGeneration();j.nativeHandoffCycleEpoch=g_inputHostEpoch;
                if(mode==2)++j.nativeHandoffCycleRequest;if(mode==3)++j.nativeHandoffCyclePower;if(mode==4)++j.nativeHandoffCycleEpoch;if(mode==5)j.transactionId+="-wrong-run";}
            if(mode==6)j.hiddenByUs.erase(std::remove(j.hiddenByUs.begin(),j.hiddenByUs.end(),U2W(physicalRoot)),j.hiddenByUs.end());
            if(mode==7)j.hiddenByUs.erase(std::remove(j.hiddenByUs.begin(),j.hiddenByUs.end(),U2W(hid)),j.hiddenByUs.end());
            // mode6/7 = valid current handoff stamp but mixed foreign/own paths.
            // mode0 = foreign before.hidden; mode1 = ordinary carried/self-owned before.hidden.
            if(mode==0)j.hiddenByUs.clear();
            if(ticket)check("foreign-carry-wrong-ticket-power-epoch-run-still-skip",step(ticket)==0&&refreshes==0&&writes==0&&!g_msiNativeForegroundCyclePending&&!g_msiNativeForegroundLease.postHideCycle);
            cleanup();
        }
    }
    if(!legacySkip&&!partialBefore){
        // The 134326 donor contains an old game leaf and its newer same-root replacement.
        const std::wstring foreign=L"HID\\FOREIGN-PREHIDDEN\\OTHER-PORT";
        for(unsigned mode=0;mode<6;++mode){
            const auto ticket=setup(30+mode,true,true);if(!ticket)continue;
            g_inputHostProcess=nullptr;g_hidHideVisibilityJournal={};
            auto& s=g_msiNativeForegroundLease;auto native=s.nativeVisibilitySession;
            s.gameSourceCandidate=hid;s.cycleIssued=s.cycleAccepted=true;s.helperExitCode=0;s.helperGameTarget=hid;
            hidden.push_back(foreign);s.before.hidden={foreign};
            unsigned reads=0;
            g_msiNativeForegroundTestIo.handoffPathProven=[&](const std::string& rootId,const std::string& path){
                ++reads;
                if(mode==3 && path==oldHid)++g_xinputTopologyEpoch;
                return rootId==physicalRoot && (path==hid?mode!=2:path==physicalRoot||mode==1);
            };
            g_msiNativeForegroundTestIo.visibility=[&](const std::wstring& path,bool hide){
                ++writes;if(hide || path==foreign)return false;
                hidden.erase(std::remove(hidden.begin(),hidden.end(),path),hidden.end());return true;
            };
            g_msiNativeForegroundTestIo.inventory=[&](const HidHidePhysicalIdentity&){
                HidHideInterfaceRecord item;item.instanceId=hid;item.baseContainerDeviceInstanceId=physicalRoot;
                item.vendorId=0x045e;item.productId=0x028e;item.usagePage=1;item.usage=5;
                return std::vector<HidHideInterfaceRecord>{item};
            };
            g_msiNativeForegroundTestIo.gameRefresh=[&](const HidHidePhysicalIdentity& item,const std::wstring&){
                check("pre-start-return-refreshes-current-game-leaf-only",item.deviceInstanceId==hid);
                ++refreshes;return mode==5?1:0;
            };
            g_msiNativeForegroundTestIo.xusbRefresh=[&](const HidHidePhysicalIdentity& item,const std::wstring&){
                check("pre-start-return-refreshes-same-XUSB-root",item.deviceInstanceId==physicalRoot);++refreshes;return 0;
            };
            if(mode==4)g_msiNativeForegroundCyclePending=true;
            const auto work=ymcc::inputHost::queuedPlayerStart::bind("persona-transition");
            const bool allowed=msiNativeForegroundBeforeVirtualWork(work);
            if(mode==0){
                check("untransferable-old-path-is-returned-before-any-Host",allowed&&!s.active&&writes==3&&refreshes==2&&
                    reads==3&&hidden==std::vector<std::wstring>{foreign}&&native&&!native->record()->nativeVisibility&&
                    !g_inputHostProcess&&!g_hidHideVisibilityJournal.active&&g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId==ticket);
            }else if(mode==5){
                check("unfinished-original-return-parks-same-request-before-Host",!allowed&&s.active&&s.configurationReturned&&
                    s.restorePending&&writes==3&&refreshes==1&&hidden==std::vector<std::wstring>{foreign}&&
                    s.deferredVirtualWork==std::vector<std::string>{work}&&!g_inputHostProcess&&!g_hidHideVisibilityJournal.active);
            }else{
                check("proven-set-unproven-replacement-raced-topology-or-live-helper-no-early-return",allowed==(mode!=4)&&
                    s.active&&s.virtualHandoffHeld&&writes==0&&refreshes==0&&(!g_msiNativeForegroundCyclePending||reads==0)&&
                    native&&native->record()->nativeVisibility.has_value()&&hidden.size()==4);
            }
            cleanup();
        }
    }
    cleanup();rt::state=std::move(savedTargetState);
    g_msiNativeForegroundTestIo={};g_msiForegroundTestIo.locations={};
    g_inputHostHello=false;g_inputHostEpoch=0;g_inputHostBoundPersona.clear();
    return {checks,failures};
}
