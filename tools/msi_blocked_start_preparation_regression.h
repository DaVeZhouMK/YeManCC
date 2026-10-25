#pragma once
static bool msiBlockedStartPreparationRegression(){
    using namespace ymcc::msi::preparation;unsigned cases=0,failed=0;std::ostringstream report;
    auto check=[&](const char* n,bool ok){++cases;if(!ok)++failed;report<<(ok?"PASS ":"FAIL ")<<n<<"\n";};
    for(bool enabled:{false,true})for(bool board:{false,true})for(bool quarantine:{false,true})
        check("pure scope rejects only registered MSI quarantine",allowed(enabled,board,quarantine)==!(enabled&&board&&quarantine));
    ymcc::msi::quarantine::Budget budget;
    check("budget no eligibility no take",budget.take(false,1000)==0);
    check("budget first take",budget.take(true,1000)==1);
    check("budget sub-five-second reattempt denied",budget.take(true,5999)==0);
    check("budget second take",budget.take(true,6000)==1);
    check("budget third take",budget.take(true,11000)==1);
    check("budget maximum three",budget.take(true,16000)==0);
    budget.begin();check("new stop resets with new episode",budget.take(true,16001)==2);
    if(g_inputCaptureThread||g_inputHostProcess){report<<"FAIL not offline entry\n";return false;}
    const auto oldMachine=g_machineIdentity;const auto oldConfig=g_realStickTest;
    const bool oldDone=g_msiM12RemapDone,oldQuarantine=g_inputHostReleaseQuarantined,oldBlocked=g_inputHostStartBlocked;
    const auto oldCount=g_msiStartPreparationEntriesForTest;const auto oldSeam=g_startEntrySeam;
    const bool oldFence=g_inputHostExitFence.load(),oldDetail=g_domainLogEnabled[1].load();const auto oldSlotHook=g_msiQuarantineSlotEvidenceForTest;
    const auto oldMask=g_xinputConnectedMask,oldPhysical=g_xinputPhysicalSourceMask,oldVirtual=g_xinputVirtualEndpointMask;const bool oldLocked=g_xinputPhysicalSourceLocked;
    const auto oldApp=g_appDataDir;const auto logDir=fspath::path(g_yemanccDataDir)/(L"blocked-preparation-native-"+std::to_wstring(GetTickCount64()));fspath::create_directories(logDir);
    g_nativeLifecycleLog.close();g_nativeDetailLog.close();g_nativeLifecycleLog.clear();g_nativeDetailLog.clear();g_appDataDir=logDir.wstring();
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";
    g_msiM12RemapDone=true; // Existing idempotence gate: never enumerates or writes hardware, even flag-off.
    g_inputHostReleaseQuarantined=true;g_inputHostStartBlocked=false;g_inputHostExitFence=false;g_domainLogEnabled[1]=true;
    unsigned evidenceCalls=0;g_msiQuarantineSlotEvidenceForTest=[&](){++evidenceCalls;return json::array({{{"slot",0},{"pathFromOpenXInput","offline-msi1901-path"},{"reverseUserIndex",0},{"reverseMatches",true}}});};
    check("actual registered MSI recognized",msiClawKnownBoardAuthority());
    for(const char* board:{"MS-1T41","MS-1T42","MS-1T52","MS-1T8K","MS-1T91"}){
        g_machineIdentity.product=board;const auto count=g_msiStartPreparationEntriesForTest;
        msiClawPrepareForVirtualStart();check("real wrapper enters old preparation only without MSI fix",g_msiStartPreparationEntriesForTest==count);
    }
    g_machineIdentity.product="MS-1T91";g_inputHostReleaseQuarantined=false;const auto n=g_msiStartPreparationEntriesForTest;
    check("unblocked preparation retains existing success behavior",msiClawPrepareForVirtualStart()&&g_msiStartPreparationEntriesForTest==n+1);
    g_inputHostReleaseQuarantined=true;
    for(const auto family:{YmccFamily::AsusRogAlly,YmccFamily::AsusRogAllyX,YmccFamily::Unknown}){
        g_machineIdentity.family=family;const auto n=g_msiStartPreparationEntriesForTest;msiClawPrepareForVirtualStart();check("actual ROG/common preparation call preserved",g_msiStartPreparationEntriesForTest==n+1);
    }
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.product="unregistered";const auto n2=g_msiStartPreparationEntriesForTest;msiClawPrepareForVirtualStart();check("unregistered MSI not changed",g_msiStartPreparationEntriesForTest==n2+1);
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";g_startEntrySeam=YmccStartEntrySeam{};g_startEntrySeam.active=true;g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="steamdeck";
    msiQuarantineDiagnosticBeginStop();const auto countBefore=g_msiStartPreparationEntriesForTest;
    check("actual inputHostStart retains existing quarantine refusal",!inputHostStart());
    check("actual start entry skips before forbidden preparation with fix",g_msiStartPreparationEntriesForTest==countBefore);
    check("actual refusal does not arm physical mode route",std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"ensure-route")==0);
    // Evidence hook supplies synthetic API values, no GetDevicePath/device I/O.
    g_xinputConnectedMask=1;g_xinputPhysicalSourceMask=0;g_xinputPhysicalSourceLocked=true;g_xinputVirtualEndpointMask=0;
    const auto state=std::tuple{g_xinputConnectedMask,g_xinputPhysicalSourceMask,g_xinputPhysicalSourceLocked,g_inputHostReleaseQuarantined,g_inputHostStartBlocked};
    msiQuarantineDiagnosticBeginStop();const auto callsBefore=evidenceCalls;msiQuarantineDiagnosticReceipt();msiQuarantineDiagnosticReceipt();
    check("receipt is opt-in and bounded across repeated denial",evidenceCalls==callsBefore+(ymcc::msi::quarantine::buildEnabled?1:0));
    check("receipt never grants control or changes masks",state==std::tuple{g_xinputConnectedMask,g_xinputPhysicalSourceMask,g_xinputPhysicalSourceLocked,g_inputHostReleaseQuarantined,g_inputHostStartBlocked});
    // Null-hook counterexample: detail on, connected slot 0, registered MSI.
    // Actual receipt must be empty and explicit rather than reaching platform APIs.
    g_msiQuarantineSlotEvidenceForTest={};msiQuarantineDiagnosticBeginStop();msiQuarantineDiagnosticReceipt();
    g_nativeDetailLog.flush();std::ifstream missingHookLog(logDir/L"native-detail.log");std::string line;bool sawMissingHook=false;
    while(std::getline(missingHookLog,line)){auto row=json::parse(line,nullptr,false);
        if(!row.is_discarded()&&row.value("event","")=="msi-quarantine-source-evidence"&&row.value("slotEvidenceSource","")=="fault-hook-missing")
            sawMissingHook=row.at("slots").is_array()&&row.at("slots").empty()&&row.at("connectedMask")==1&&row.at("physicalSourceMask")==0;
    }
    check("fault null hook emits explicit empty slot evidence only when opted in",sawMissingHook==ymcc::msi::quarantine::buildEnabled);
    check("fault null hook does not call configured evidence hook",evidenceCalls==callsBefore+(ymcc::msi::quarantine::buildEnabled?1:0));
    check("fault null hook never grants control or changes masks",state==std::tuple{g_xinputConnectedMask,g_xinputPhysicalSourceMask,g_xinputPhysicalSourceLocked,g_inputHostReleaseQuarantined,g_inputHostStartBlocked});
    g_machineIdentity.family=YmccFamily::AsusRogAlly;const auto c=evidenceCalls;msiQuarantineDiagnosticBeginStop();msiQuarantineDiagnosticReceipt();check("ROG gets no new evidence queries",evidenceCalls==c);
    g_nativeLifecycleLog.close();g_nativeDetailLog.close();g_nativeLifecycleLog.clear();g_nativeDetailLog.clear();g_appDataDir=oldApp;
    g_machineIdentity=oldMachine;g_realStickTest=oldConfig;g_msiM12RemapDone=oldDone;g_inputHostReleaseQuarantined=oldQuarantine;g_inputHostStartBlocked=oldBlocked;
    g_msiStartPreparationEntriesForTest=oldCount;g_startEntrySeam=oldSeam;g_inputHostExitFence=oldFence;g_domainLogEnabled[1]=oldDetail;g_msiQuarantineSlotEvidenceForTest=oldSlotHook;
    g_xinputConnectedMask=oldMask;g_xinputPhysicalSourceMask=oldPhysical;g_xinputVirtualEndpointMask=oldVirtual;g_xinputPhysicalSourceLocked=oldLocked;
    report<<"cases="<<cases<<" failed="<<failed<<"\n";std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-blocked-start-preparation-native-entry.txt");out<<report.str();return failed==0&&out.good();
}
