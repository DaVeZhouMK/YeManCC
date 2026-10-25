// Virtual-controller wake only. No dedicated-key remapping, WMI, or fan work.
static bool msiVirtualWakeNativeRegression(const wchar_t* replayPath) {
    unsigned cases=0,failed=0;std::ostringstream report;
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;report<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    using namespace ymcc::msi;
    VirtualWakeRearmFacts f;f.knownBoard=f.targetEnabled=f.powerReady=f.startBlocked=f.releaseRestored=true;
    f.generation=f.currentGeneration=2;f.armTick=100;f.now=1883;
    check("recorded restored-release wake may lift only its explicit rearm gate",virtualWakeMayRearm(f));
    auto refuse=[&](auto mutate){auto x=f;mutate(x);return !virtualWakeMayRearm(x);};
    check("ROG/common cannot use MSI rearm",refuse([](auto& x){x.knownBoard=false;}));
    check("Off cancels wake",refuse([](auto& x){x.targetEnabled=false;}));
    check("stale power generation is refused",refuse([](auto& x){++x.currentGeneration;}));
    check("generation zero is not authority",refuse([](auto& x){x.generation=x.currentGeneration=0;}));
    check("source insert without a wake is refused",refuse([](auto& x){x.armTick=0;}));
    check("suspending/resuming never lifts safety gate",refuse([](auto& x){x.powerReady=false;}));
    check("unresolved quarantine cannot be bypassed",refuse([](auto& x){x.quarantined=true;}));
    check("unrestored owned release cannot be bypassed",refuse([](auto& x){x.releaseRestored=false;}));
    check("active visibility journal cannot be bypassed",refuse([](auto& x){x.visibilityJournalActive=true;}));
    check("Host present cannot start a second writer",refuse([](auto& x){x.hostPresent=true;}));
    check("transport fault is not cleared by this repair",refuse([](auto& x){x.transportFault=true;}));
    check("explicit backend fault hold is not cleared",refuse([](auto& x){x.faultHold=true;}));
    check("exit fence cannot be bypassed",refuse([](auto& x){x.exitFence=true;}));
    check("same wake rearm is single-use",refuse([](auto& x){x.rearmedGeneration=x.generation;}));
    check("MSI budget is absolute and finite",refuse([](auto& x){x.now=x.armTick+virtualWakeMaxAgeMs;}));
    check("backward tick cannot extend wake",refuse([](auto& x){x.now=x.armTick-1;}));
    f.now=f.armTick+45000;
    check("synthetic late source can wait past old global ceiling",virtualWakeMayWaitSource(f,true,22));
    check("zero Host epoch still refuses extended source wait",!virtualWakeMayWaitSource(f,true,0));
    check("unarmed route cannot wait",!virtualWakeMayWaitSource(f,false,22));
    f.now=f.armTick+virtualWakeMaxAgeMs;
    check("source wait stops at absolute ceiling",!virtualWakeMayWaitSource(f,true,22));

    const auto machine=g_machineIdentity;const auto powerGen=g_powerGeneration.load();const auto power=g_powerLifecycle.load();
    const bool ready=g_inputReady.load(),revoked=g_inputHostPowerAdmissionRevoked.load(),exit=g_inputHostExitFence.load();
    const bool blocked=g_inputHostStartBlocked,quarantined=g_inputHostReleaseQuarantined,journal=g_hidHideVisibilityJournal.active;
    const auto process=g_inputHostProcess;const bool transport=g_inputHostTransportFault,hold=g_inputHostFaultHoldsStart.load();
    const auto gen=g_msiVirtualWakeGeneration.load(),arm=g_msiVirtualWakeArmTick.load(),rearmed=g_msiVirtualWakeRearmedGeneration.load(),logged=g_msiVirtualWakeSourceWaitLoggedGeneration.load();
    const bool eligible=g_msiVirtualReleaseRearmEligible.load(),route=g_msiDinputRouteDinputArmed.load();
    const auto attempts=g_msiVirtualWakeStartAttempts.load();const auto epoch=g_inputHostEpochMirror.load();
    const bool enabled=g_realStickTest.enabled,target=g_realStickTest.virtualTargetEnabled;
    const auto genericArm=g_wakeRescanArmTick.load(),genericGen=g_wakeRescanGeneration.load(),truth=g_wakeTruthSnapshotUntilTick.load(),kept=g_wakeKeptTargetGeneration.load();
    const bool requested=g_wakeRescanRequested.load();
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";
    g_powerGeneration=2;g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_inputHostExitFence=false;
    g_inputHostStartBlocked=true;g_inputHostReleaseQuarantined=false;g_hidHideVisibilityJournal.active=false;
    g_inputHostProcess=nullptr;g_inputHostTransportFault=false;g_inputHostFaultHoldsStart=false;
    g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=true;g_msiVirtualWakeGeneration=2;g_msiVirtualWakeArmTick=100;
    g_msiVirtualWakeRearmedGeneration=0;g_msiVirtualReleaseRearmEligible=true;g_msiDinputRouteDinputArmed=true;g_inputHostEpochMirror=22;
    for(const char* board:{"MS-1T41","MS-1T42","MS-1T52","MS-1T8K","MS-1T91"}) {
        g_machineIdentity.product=board;g_inputHostStartBlocked=true;g_msiVirtualReleaseRearmEligible=true;g_msiVirtualWakeRearmedGeneration=0;
        check("authorized MSI family gets separate 60s wake policy",inputHostWakeMaxAgeMs()==virtualWakeMaxAgeMs);
        check("actual generation-tagged work reason carries MSI power intent",inputHostWakeWorkReason(22,2)=="wake-hot-replace:22:msi-power:2");
        check("actual owned release rearm lifts only startBlocked",msiVirtualWakeTryRearmLocked(1883)&&!g_inputHostStartBlocked&&!g_msiVirtualReleaseRearmEligible.load()&&g_msiVirtualWakeRearmedGeneration.load()==2);
        check("actual rearm grants no Host source or publication",g_inputHostProcess==nullptr&&!g_inputHostReleaseQuarantined&&g_inputHostEpochMirror.load()==22);
        g_inputHostStartBlocked=true;g_msiVirtualReleaseRearmEligible=true;
        check("actual duplicate wake cannot clear a later block",!msiVirtualWakeTryRearmLocked(1884)&&g_inputHostStartBlocked);
        check("actual source decision keeps existing single resend",msiDinputRouteWindowDecideForWake(true,false,false,5000,false,5100)==MsiDinputRouteWindowAction::Resend);
        check("actual pending source waits after 10s only in current MSI wake",msiDinputRouteWindowDecideForWake(true,false,true,45000,false,45100)==MsiDinputRouteWindowAction::Poll);
        check("actual extended source wait stops at 60s",msiDinputRouteWindowDecideForWake(true,false,true,60000,false,60100)==MsiDinputRouteWindowAction::Revert);
    }
    g_machineIdentity.product="MS-1T91";g_msiVirtualWakeGeneration=0;g_wakeKeptTargetGeneration=0;
    inputHostRequestWakeRescan(2);const auto stableArm=g_msiVirtualWakeArmTick.load();g_msiVirtualWakeStartAttempts=3;
    inputHostRequestWakeRescan(2);
    check("actual duplicate PBT keeps fixed MSI deadline",stableArm!=0&&g_msiVirtualWakeArmTick.load()==stableArm);
    check("actual duplicate PBT cannot refresh start budget",g_msiVirtualWakeStartAttempts.load()==3);
    g_msiVirtualWakeArmTick=100;g_msiVirtualWakeGeneration=2;g_msiVirtualWakeRearmedGeneration=0;
    for(const auto family:{YmccFamily::AsusRogAlly,YmccFamily::AsusRogAllyX,YmccFamily::AsusRogAllyClassic,YmccFamily::AsusRogAllyXClassic,YmccFamily::Unknown}) {
        g_machineIdentity.family=family;g_inputHostStartBlocked=true;g_msiVirtualReleaseRearmEligible=true;
        check("protected family retains global 20s ceiling",inputHostWakeMaxAgeMs()==kWakeRescanMaxAgeMs);
        check("protected family keeps original lifecycle reason schema",inputHostWakeWorkReason(22,2)=="wake-hot-replace:22");
        check("protected family cannot consume MSI owned release",!msiVirtualWakeTryRearmLocked(1883)&&g_inputHostStartBlocked&&g_msiVirtualReleaseRearmEligible.load());
        check("protected family retains old source timeout",msiDinputRouteWindowDecideForWake(true,false,true,10000,false,10100)==MsiDinputRouteWindowAction::Revert);
    }
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.product="MS-UNKNOWN";
    check("unknown MSI board keeps default policy",inputHostWakeMaxAgeMs()==kWakeRescanMaxAgeMs&&!msiVirtualWakeTryRearmLocked(1883));
    g_machineIdentity.product="MS-1T91";g_machineIdentity.manufacturer="ASUSTeK COMPUTER INC.";
    check("board text with wrong manufacturer is not MSI authority",inputHostWakeMaxAgeMs()==kWakeRescanMaxAgeMs);
    g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";
    g_inputHostEpochMirror=0;
    check("actual epoch zero never gets extended source wait",msiDinputRouteWindowDecideForWake(true,false,true,10000,false,10100)==MsiDinputRouteWindowAction::Revert);
    check("actual zero-generation mode write refusal stays closed",!msiDinputSubmitModeWriteIntent(true,"virtual-wake-offline-replay"));
    if(replayPath) {
        std::ifstream in{fspath::path(replayPath)};json data;in>>data;
        check("new real log shows source already returned before timeout",data.at("sourceReturnAfterWakeMs")==1783&&data.at("sourceReturned").at("hostEpoch")==0);
        check("new real log shows ten blocked starts not ten healthy handshakes",data.at("blockedStarts").size()==10&&data.at("postWakePreparedWindows")==0&&data.at("postWakeHostAcks")==0);
        check("new real log has completed owned release before wake",data.at("quarantineClearedBeforeWake").at("status")=="physical-restore-readback-confirmed");
        for(const auto& r:data.at("blockedStarts")) {
            g_inputHostStartBlocked=true;g_msiVirtualReleaseRearmEligible=true;g_msiVirtualWakeRearmedGeneration=0;
            g_msiVirtualWakeGeneration=data.at("wake").at("generation");g_powerGeneration=g_msiVirtualWakeGeneration.load();g_msiVirtualWakeArmTick=100;
            const auto elapsed=uint64_t(data.at("sourceReturnAfterWakeMs").get<unsigned>());
            check("recorded explicit-rearm shape can consume a real owned power intent",msiVirtualWakeTryRearmLocked(100+elapsed)&&!g_inputHostStartBlocked);
            check("recorded physical return does not authorize Host submit by itself",r.at("reason")=="explicit-rearm-required"&&g_inputHostProcess==nullptr&&g_inputHostEpochMirror.load()==0);
        }
        report<<"realBlockedStarts="<<data.at("blockedStarts").size()<<" realSourceReturnAfterWakeMs="<<data.at("sourceReturnAfterWakeMs")<<" realPostWakeAcks="<<data.at("postWakeHostAcks")<<"\n";
    }
    g_machineIdentity=machine;g_powerGeneration=powerGen;g_powerLifecycle=power;g_inputReady=ready;g_inputHostPowerAdmissionRevoked=revoked;g_inputHostExitFence=exit;
    g_inputHostStartBlocked=blocked;g_inputHostReleaseQuarantined=quarantined;g_hidHideVisibilityJournal.active=journal;g_inputHostProcess=process;g_inputHostTransportFault=transport;g_inputHostFaultHoldsStart=hold;
    g_msiVirtualWakeGeneration=gen;g_msiVirtualWakeArmTick=arm;g_msiVirtualWakeRearmedGeneration=rearmed;g_msiVirtualWakeSourceWaitLoggedGeneration=logged;g_msiVirtualReleaseRearmEligible=eligible;g_msiDinputRouteDinputArmed=route;g_msiVirtualWakeStartAttempts=attempts;g_inputHostEpochMirror=epoch;
    g_realStickTest.enabled=enabled;g_realStickTest.virtualTargetEnabled=target;g_wakeRescanArmTick=genericArm;g_wakeRescanGeneration=genericGen;g_wakeTruthSnapshotUntilTick=truth;g_wakeKeptTargetGeneration=kept;g_wakeRescanRequested=requested;
    report<<"cases="<<cases<<" failed="<<failed<<"\n";std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-virtual-wake-native-entry.txt");out<<report.str();return failed==0&&out.good();
}

