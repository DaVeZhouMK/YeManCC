#pragma once
// Actual MSI wrappers and old late-close assignment, no Host/hardware launch.
static bool msiExplicitRearmRegression() {
    using namespace ymcc::msi::rearm;unsigned cases=0,failed=0;std::ostringstream report;
    auto check=[&](const char* n,bool ok){++cases;if(!ok)++failed;report<<(ok?"PASS ":"FAIL ")<<n<<"\n";};
    Facts f;f.enabled=f.knownBoard=f.restored=f.powerReady=f.wanted=true;
    f.quarantined=f.journalActive=f.hostPresent=f.faultHold=f.transportFault=f.exitFence=false;f.power=7;f.persona="steamdeck";
    auto setup=[&](Ticket& t){t.setting(true,"steamdeck",7);t.started(t.snapshot().sequence);t.setting(false,"disabled",7);t.released(true,true,true,true,true,7);t.setting(true,"steamdeck",7);};
    {Ticket t;setup(t);check("newer ON survives old normal release",t.take(f).preserved);check("one setting consumed once",!t.take(f).preserved);}
    for(int i=0;i<15;i++){Ticket t;setup(t);auto v=f;
        switch(i){case 0:v.enabled=false;break;case 1:v.knownBoard=false;break;case 2:v.restored=false;break;case 3:v.quarantined=true;break;
        case 4:v.journalActive=true;break;case 5:v.hostPresent=true;break;case 6:v.faultHold=true;break;case 7:v.transportFault=true;break;
        case 8:v.exitFence=true;break;case 9:v.powerReady=false;break;case 10:v.wanted=false;break;case 11:v.power=8;break;
        case 12:v.persona="elite";break;case 13:v.persona="disabled";break;case 14:v.power=0;break;}
        check("each original safety gate remains closed",!t.take(v).preserved);
    }
    for(int i=0;i<5;i++){Ticket t;t.setting(true,"steamdeck",7);t.started(t.snapshot().sequence);t.setting(false,"disabled",7);
        t.released(i!=0,i!=1,i!=2,i!=3,i!=4,7);t.setting(true,"steamdeck",7);check("normal off and all release receipts required",!t.take(f).preserved);}
    {Ticket t;t.setting(true,"steamdeck",7);t.started(t.snapshot().sequence);t.released(true,true,true,true,true,7);check("no OFF setting means no rearm grant",!t.take(f).preserved);}
    {Ticket t;t.setting(true,"steamdeck",7);t.started(t.snapshot().sequence);t.setting(false,"disabled",7);t.released(true,true,true,true,true,7);check("physical return alone is not an ON setting",!t.take(f).preserved);}
    {Ticket t;setup(t);t.setting(false,"disabled",7);check("newer OFF cancels ON",!t.take(f).preserved);}
    {Ticket t;setup(t);t.started(t.snapshot().sequence);check("later successful publication invalidates old release",!t.take(f).preserved);}
    {Ticket t;setup(t);t.setting(true,"steamdeck",8);check("cross-power setting cannot revive old release",!t.take(f).preserved);}
    for(bool bothBeforeAck:{false,true}){Ticket t;t.setting(true,"steamdeck",7);const auto owner=t.snapshot().sequence;
        t.setting(false,"disabled",7);if(bothBeforeAck)t.setting(true,"steamdeck",7);
        check("ACK accepts only its captured start owner",t.started(owner)&&t.snapshot().startedSequence==owner);
        t.released(true,true,true,true,true,7);if(!bothBeforeAck)t.setting(true,"steamdeck",7);
        check("OFF during PREPARE remains newer than the acknowledged start",t.take(f).preserved);
    }
    {Ticket t;setup(t);const auto before=t.snapshot();check("future fabricated ACK owner is refused",!t.started(before.sequence+1));
        check("invalid ACK does not consume or revoke a legitimate release",t.snapshot().startedSequence==before.startedSequence&&t.take(f).preserved);}
    {Ticket t;t.setting(true,"steamdeck",7);const auto owner=t.snapshot().sequence;t.setting(false,"disabled",7);
        t.started(owner);t.released(true,true,true,true,true,7);check("captured ACK still cannot manufacture a newer ON",!t.take(f).preserved);}
    // Real wrapper state is saved/restored, so historical suites see no state delta.
    const auto oldTicket=g_msiExplicitRearmTicket.snapshot();const auto oldMachine=g_machineIdentity;
    const auto oldConfig=g_realStickTest;const auto oldJournal=g_hidHideVisibilityJournal;
    const auto oldPower=g_powerGeneration.load();const auto oldLifecycle=g_powerLifecycle.load();const bool oldReady=g_inputReady.load();
    const bool oldRevoked=g_inputHostPowerAdmissionRevoked.load(),oldFault=g_inputHostFaultHoldsStart.load(),oldTransport=g_inputHostTransportFault;
    const bool oldQuarantine=g_inputHostReleaseQuarantined,oldBlocked=g_inputHostStartBlocked,oldFence=g_inputHostExitFence.load();
    const auto oldProcess=g_inputHostProcess;const auto oldApp=g_appDataDir;
    const auto logDir=fspath::path(g_yemanccDataDir)/(L"explicit-rearm-native-"+std::to_wstring(GetTickCount64()));fspath::create_directories(logDir);
    g_nativeLifecycleLog.close();g_nativeLifecycleLog.clear();g_appDataDir=logDir.wstring();
    g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_powerGeneration=7;g_inputHostPowerAdmissionRevoked=false;
    g_inputHostFaultHoldsStart=false;g_inputHostTransportFault=false;g_inputHostReleaseQuarantined=false;g_inputHostExitFence=false;
    g_inputHostProcess=nullptr;g_hidHideVisibilityJournal.active=false;
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";
    auto runActual=[&](const char* persona,bool receiptOk,bool newerOn){
        g_msiExplicitRearmTicket.restore(State{});msiExplicitRearmSettingChanged(true,true,"steamdeck");msiExplicitRearmStarted(msiExplicitRearmStartSettingSequence());
        msiExplicitRearmSettingChanged(true,false,"disabled");msiExplicitRearmRelease("virtual-target-disabled",true,receiptOk,true,true,7);
        if(newerOn)msiExplicitRearmSettingChanged(true,true,persona);
        g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=newerOn;g_realStickTest.virtualTargetPersona=newerOn?persona:"disabled";
        g_inputHostStartBlocked=false; // old settings boundary
        g_inputHostStartBlocked=true;  // old successful quarantine completion
        return msiExplicitRearmRestoredLocked();
    };
    for(const char* board:{"MS-1T41","MS-1T42","MS-1T52","MS-1T8K","MS-1T91"}){
        g_machineIdentity.product=board;check("actual known MSI wrapper preserves request only with build flag",runActual("steamdeck",true,true)==buildEnabled);
        check("late close cannot re-block preserved MSI intent",g_inputHostStartBlocked==!buildEnabled);
    }
    g_machineIdentity.product="MS-1T91";check("actual missing release ACK stays closed",!runActual("steamdeck",false,true)&&g_inputHostStartBlocked);
    check("actual physical return without explicit ON stays closed",!runActual("steamdeck",true,false)&&g_inputHostStartBlocked);
    for(const auto family:{YmccFamily::AsusRogAlly,YmccFamily::AsusRogAllyX,YmccFamily::Unknown}){
        g_machineIdentity.family=family;check("actual ROG/common behavior unchanged",!runActual("steamdeck",true,true)&&g_inputHostStartBlocked);
    }
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.product="unregistered";
    check("unregistered MSI not authorized",!runActual("steamdeck",true,true)&&g_inputHostStartBlocked);
    g_machineIdentity.product="MS-1T91";
    const auto fixture=fspath::path(g_yemanccDataDir)/L"msi-e-rearm-real-fixture.json";
    if(fspath::exists(fixture)){
        std::ifstream in(fixture);json data;in>>data;check("fixture is the SHA-identified E return",data.value("sourceZipSha256","")=="C35730E4B0269FDD5A65E24573102244D3CA100C671D3024E9120CDCF5F22310");
        check("both actual new-ON-before-old-clear sequences included",data["releaseRaces"].size()==2);
        for(const auto& r:data["releaseRaces"]){check("actual file ordering reproduces lost intent",r["setting"]["time"].get<std::string>()<r["cleared"]["time"].get<std::string>()&&r["startsBeforeNextSetting"]==0&&r["rearmRequired"]==true);
            check("real E request repaired by actual MSI wrapper only with flag",runActual(r["persona"].get<std::string>().c_str(),r["localReleaseAck"],true)==buildEnabled);
        }
    }
    g_msiExplicitRearmTicket.restore(oldTicket);g_machineIdentity=oldMachine;g_realStickTest=oldConfig;g_hidHideVisibilityJournal=oldJournal;
    g_powerGeneration=oldPower;g_powerLifecycle=oldLifecycle;g_inputReady=oldReady;g_inputHostPowerAdmissionRevoked=oldRevoked;
    g_inputHostFaultHoldsStart=oldFault;g_inputHostTransportFault=oldTransport;g_inputHostReleaseQuarantined=oldQuarantine;
    g_inputHostStartBlocked=oldBlocked;g_inputHostExitFence=oldFence;g_inputHostProcess=oldProcess;
    g_nativeLifecycleLog.close();g_nativeLifecycleLog.clear();g_appDataDir=oldApp;
    report<<"cases="<<cases<<" failed="<<failed<<"\n";std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-explicit-rearm-native-entry.txt");out<<report.str();return failed==0&&out.good();
}
