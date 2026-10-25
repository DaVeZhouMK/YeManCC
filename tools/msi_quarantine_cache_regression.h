#pragma once
// Actual cache-copy function, real mutexes and state. No enumeration or
// device/process APIs. Contention is on separate OS threads, not try_lock on
// a std::mutex already owned by this thread.
static bool msiQuarantineCacheRegression() {
    unsigned cases=0,failed=0;std::ostringstream report;const bool enabled=ymcc::msi::quarantine::buildEnabled;
    auto check=[&](const char* label,bool ok){++cases;failed+=!ok;report<<(ok?"PASS ":"FAIL ")<<label<<"\n";};
    if(g_inputCaptureThread||g_inputHostProcess)return false;
    const auto oldMachine=g_machineIdentity;const bool oldDetail=g_domainLogEnabled[1].load(),oldQuarantine=g_inputHostReleaseQuarantined;
    const auto oldRaw=g_hidHidePhysicalIdentity;const auto oldMain=g_msiMainSource;const auto oldExpected=g_xinputReconfirmIdentity;
    const auto oldMasks=std::tuple{g_xinputConnectedMask,g_xinputPhysicalSourceMask,g_xinputVirtualEndpointMask,g_xinputSlotUnknownMask,g_xinputPrimarySlot,g_xinputPhysicalSourceLocked};
    const auto oldPending=g_xinputSourceReconfirmMask.load();const auto oldGeneration=g_xinputReconfirmMsiGeneration;const bool oldReject=g_xinputReconfirmRejectLogged;
    const auto oldTopo=g_xinputTopologyEpoch.load(),oldBoundary=g_xinputBoundaryScanEpoch.load(),oldStart=g_xinputSlotFullScanEpochStart.load(),oldIdentity=g_xinputSlotIdentityEpoch.load();
    const bool oldScanned=g_xinputPreTargetBoundaryScanned.load();std::array<DWORD,4> oldResolved{};for(unsigned i=0;i<4;++i)oldResolved[i]=g_xinputSlotIdentityResolvedEpoch[i];
    const auto oldHook=g_msiQuarantineSlotEvidenceForTest;const auto oldApp=g_appDataDir;
    const auto logDir=fspath::path(g_yemanccDataDir)/(L"quarantine-cache-native-"+std::to_wstring(GetTickCount64()));fspath::create_directories(logDir);
    g_nativeLifecycleLog.close();g_nativeDetailLog.close();g_nativeLifecycleLog.clear();g_nativeDetailLog.clear();g_appDataDir=logDir.wstring();
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";g_machineIdentity.attempted=g_machineIdentity.resolved=true;
    g_domainLogEnabled[1]=true;g_inputHostReleaseQuarantined=true;
    auto seed=[&](){std::scoped_lock locks(g_gamepadReadMx,g_hidHidePhysicalIdentityMx,g_msiMainSourceMx);
        g_xinputConnectedMask=1;g_xinputPhysicalSourceMask=0;g_xinputVirtualEndpointMask=0;g_xinputSlotUnknownMask=0;g_xinputPrimarySlot=0;g_xinputPhysicalSourceLocked=true;
        g_xinputSourceReconfirmMask=1;g_xinputReconfirmMsiGeneration=0;g_xinputReconfirmRejectLogged=true;
        g_xinputReconfirmIdentity="hid\\vid_0db0&pid_1901&ig_03\\fixture|usb\\vid_0db0&pid_1901\\serial|vid_3504|pid_6401";
        g_hidHidePhysicalIdentity={};g_hidHidePhysicalIdentity.resolved=true;g_hidHidePhysicalIdentity.physicalIdentity="HID\\VID_0DB0&PID_1901&IG_02\\fixture|USB\\VID_0DB0&PID_1901\\serial|VID_3504|PID_6401";
        g_hidHidePhysicalIdentity.deviceInstanceId="HID\\VID_0DB0&PID_1901&IG_02\\fixture";g_hidHidePhysicalIdentity.baseContainerDeviceInstanceId="USB\\VID_0DB0&PID_1901\\serial";
        g_hidHidePhysicalIdentity.vendorId=0x0db0;g_hidHidePhysicalIdentity.productId=0x1901;g_msiMainSource={};
        g_xinputTopologyEpoch=15;g_xinputBoundaryScanEpoch=15;g_xinputSlotFullScanEpochStart=15;g_xinputPreTargetBoundaryScanned=true;g_xinputSlotIdentityEpoch=21;
        for(unsigned i=0;i<4;++i)g_xinputSlotIdentityResolvedEpoch[i]=i==0?21:0;
    };
    auto snapshot=[&](){std::lock_guard<std::mutex> host(g_inputHostMx);return msiQuarantineCachedEvidence();};
    seed();auto r=snapshot();check("cache snapshot opt-in only",r.at("available")==enabled);
    if(enabled){check("same USB root with changed IG is logged, not matched/granted",r.at("expectedIdentity").get<std::string>().find("ig_03")!=std::string::npos&&r.at("currentRawIdentity").get<std::string>().find("ig_02")!=std::string::npos&&!r.at("cachedNonemptyIdentityEqualsExpected").get<bool>()&&!r.at("admissionProof").get<bool>());
        check("cached primary label is separated from real mask membership",r.at("cachedSlots")[0].at("cachedPrimaryOnly")==true&&r.at("cachedSlots")[0].at("physicalMaskMember")==false&&r.at("cachedSlots")[0].at("pendingReconfirmMember")==true);
        check("zero main record and muted prior rejection remain visible",r.at("msiMainRecordValid")==false&&r.at("msiMainRecordGeneration")==0&&r.at("rejectReceiptAlreadyLogged")==true);
        check("scan and slot epochs are explicit clauses, not proof",r.at("boundaryTopologyEqualityOnly")==true&&r.at("slotIdentityEpoch")==21&&r.at("cachedSlots")[0].at("identityEpochEqualityOnly")==true&&r.at("cachedSlots")[1].at("identityEpochEqualityOnly")==false);
    }
    g_xinputBoundaryScanEpoch=14;r=snapshot();check("old full scan cannot be presented as epoch equality",!enabled||r.at("boundaryTopologyEqualityOnly")==false);
    {std::lock_guard<std::mutex> l(g_hidHidePhysicalIdentityMx);g_hidHidePhysicalIdentity.physicalIdentity=g_xinputReconfirmIdentity;}
    r=snapshot();check("exact string equality still leaves physical admission mask zero",!enabled||(r.at("cachedNonemptyIdentityEqualsExpected")==true&&r.at("physicalSourceMask")==0));
    {std::lock_guard<std::mutex> l(g_msiMainSourceMx);g_msiMainSource.valid=true;g_msiMainSource.generation=10;}g_xinputReconfirmMsiGeneration=9;
    r=snapshot();check("mismatching main record generation is explicit",!enabled||r.at("mainGenerationEqualityClauseOnly")==false);
    for(auto* mutex:{&g_gamepadReadMx,&g_hidHidePhysicalIdentityMx,&g_msiMainSourceMx}){
        std::atomic<bool> held{false},release{false};std::thread owner([&](){std::lock_guard<std::mutex> l(*mutex);held=true;while(!release.load())std::this_thread::yield();});
        while(!held.load())std::this_thread::yield();const auto before=GetTickCount64();r=snapshot();const auto elapsed=GetTickCount64()-before;
        release=true;owner.join();check("busy cache returns promptly instead of Host/sampling deadlock",elapsed<1000&&r.at("available")==false&&(!enabled||r.at("reason")=="cache-lock-busy"));
    }
    seed();unsigned queries=0;bool allReleased=false;
    g_msiQuarantineSlotEvidenceForTest=[&](){++queries;std::thread peer([&](){std::unique_lock<std::mutex> a(g_gamepadReadMx,std::defer_lock),b(g_hidHidePhysicalIdentityMx,std::defer_lock),c(g_msiMainSourceMx,std::defer_lock);allReleased=std::try_lock(a,b,c)==-1;});peer.join();return json::array();};
    msiQuarantineDiagnosticBeginStop();{std::lock_guard<std::mutex> host(g_inputHostMx);msiQuarantineDiagnosticReceipt();}
    check("all cache locks released before mapping callback",queries==unsigned(enabled)&&(!enabled||allReleased));
    check("snapshot and mapping callback never clear real admission/quarantine",g_xinputPhysicalSourceMask==0&&g_xinputSourceReconfirmMask.load()==1&&g_inputHostReleaseQuarantined&&g_xinputPhysicalSourceLocked);
    std::atomic<bool> held{false},release{false};std::thread owner([&](){std::lock_guard<std::mutex> l(g_gamepadReadMx);held=true;while(!release.load())std::this_thread::yield();});while(!held.load())std::this_thread::yield();
    msiQuarantineDiagnosticBeginStop();{std::lock_guard<std::mutex> host(g_inputHostMx);msiQuarantineDiagnosticReceipt();}release=true;owner.join();
    check("unavailable sample never queries slot mapping",queries==unsigned(enabled));
    g_nativeDetailLog.flush();std::ifstream input(logDir/L"native-detail.log");std::string line;bool sawUnknown=false;
    while(std::getline(input,line)){auto row=json::parse(line,nullptr,false);if(row.is_discarded()||row.value("event","")!="msi-quarantine-source-evidence")continue;
        if(!row.at("cachedEvidence").value("available",false))sawUnknown=row.at("connectedMask").is_null()&&row.at("physicalSourceMask").is_null()&&row.at("mappingQueriesSkippedForUnavailableCache")==true;}
    check("unavailable cache is unknown/null in actual durable receipt, never zero",sawUnknown==enabled);
    for(int off=0;off<4;++off){seed();switch(off){case 0:g_machineIdentity.family=YmccFamily::AsusRogAlly;break;case 1:g_machineIdentity.product="unregistered";break;case 2:g_domainLogEnabled[1]=false;break;case 3:g_inputHostReleaseQuarantined=false;break;}
        check("actual ROG/unregistered/detailoff/notquarantine yields no cache sample",snapshot().at("available")==false);
        g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.product="MS-1T91";g_domainLogEnabled[1]=true;g_inputHostReleaseQuarantined=true;
    }
    {std::scoped_lock locks(g_gamepadReadMx,g_hidHidePhysicalIdentityMx,g_msiMainSourceMx);
        g_hidHidePhysicalIdentity=oldRaw;g_msiMainSource=oldMain;g_xinputReconfirmIdentity=oldExpected;
        std::tie(g_xinputConnectedMask,g_xinputPhysicalSourceMask,g_xinputVirtualEndpointMask,g_xinputSlotUnknownMask,g_xinputPrimarySlot,g_xinputPhysicalSourceLocked)=oldMasks;
        g_xinputSourceReconfirmMask=oldPending;g_xinputReconfirmMsiGeneration=oldGeneration;g_xinputReconfirmRejectLogged=oldReject;
        g_xinputTopologyEpoch=oldTopo;g_xinputBoundaryScanEpoch=oldBoundary;g_xinputSlotFullScanEpochStart=oldStart;g_xinputSlotIdentityEpoch=oldIdentity;g_xinputPreTargetBoundaryScanned=oldScanned;
        for(unsigned i=0;i<4;++i)g_xinputSlotIdentityResolvedEpoch[i]=oldResolved[i];
    }
    g_machineIdentity=oldMachine;g_domainLogEnabled[1]=oldDetail;g_inputHostReleaseQuarantined=oldQuarantine;g_msiQuarantineSlotEvidenceForTest=oldHook;
    g_nativeLifecycleLog.close();g_nativeDetailLog.close();g_nativeLifecycleLog.clear();g_nativeDetailLog.clear();g_appDataDir=oldApp;
    report<<"diagnosticEnabled="<<enabled<<" cases="<<cases<<" failed="<<failed<<"\n";std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-quarantine-cache-native-entry.txt");out<<report.str();return failed==0&&out.good();
}
