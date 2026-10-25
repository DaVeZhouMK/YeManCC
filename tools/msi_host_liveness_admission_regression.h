#pragma once
// Actual production getter + MSI gate, with a real contended mutex and a live
// self-process handle. Queue/dirty effects below exercise the real downstream
// primitives; full hardware BUS polling is deliberately NOT executed.
static bool msiHostLivenessAdmissionRegression() {
    using ymcc::msi::liveness::admissionAllowed;
    unsigned cases=0,failed=0;std::ostringstream report;
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;report<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    if(g_inputCaptureThread){report<<"FAIL unrelated live capture owner\n";return false;}
    const auto machine=g_machineIdentity;const auto oldProcess=g_inputHostProcess;
    const auto oldAppDir=g_appDataDir;const bool oldDetail=g_domainLogEnabled[1].load();
    const auto oldCounter=g_msiHostLivenessDeferredTicks.load();const auto oldTopology=g_xinputTopologyEpoch.load();
    const bool oldDirty=g_xinputDiscoveryDirty.load();const auto oldDirtyTick=g_xinputDiscoveryDirtyTick.load();
    const auto oldEpoch=g_inputHostEpochMirror.load();const bool oldFence=g_inputHostExitFence.load();
    const bool oldRequested=g_inputHostLifecycleWorkRequested.load(),oldInFlight=g_inputHostLifecycleWorkInFlight.load();
    decltype(g_inputHostLifecycleWorkQueue) oldQueue;{
        std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);oldQueue=g_inputHostLifecycleWorkQueue;g_inputHostLifecycleWorkQueue.clear();}
    ULONGLONG oldEnqueuedTick=0;long oldEnqueuedCount=0;{
        std::lock_guard<std::mutex> lock(g_switchAudit.mx);oldEnqueuedTick=g_switchAudit.enqueuedTick;oldEnqueuedCount=g_switchAudit.enqueuedCount;}
    const auto logDir=fspath::path(g_yemanccDataDir)/(L"liveness-native-"+std::to_wstring(GetTickCount64()));
    fspath::create_directories(logDir);g_nativeLifecycleLog.close();g_nativeDetailLog.close();g_nativeLifecycleLog.clear();g_nativeDetailLog.clear();
    g_appDataDir=logDir.wstring();g_domainLogEnabled[1]=false;g_inputHostExitFence=false;
    g_inputHostLifecycleWorkRequested=false;g_inputHostLifecycleWorkInFlight=false;g_msiHostLivenessDeferredTicks=0;
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";
    check("registered G3E authority is actually recognized",msiClawKnownBoardAuthority());
    for(bool enabled:{false,true})for(bool known:{false,true})for(int state:{-1,0,1})
        check("pure tri-state matrix preserves scope and known outcomes",admissionAllowed(enabled,known,state)==!(enabled&&known&&state<0));
    HANDLE live=nullptr;const bool duplicated=DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&live,0,FALSE,DUPLICATE_SAME_ACCESS)!=FALSE;
    check("real live process observation fixture available without launching Host",duplicated);
    g_inputHostProcess=live;
    const HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    check("bounded mutex fixture events available",entered&&release);
    int unknown=0;
    if(duplicated&&entered&&release) {
        std::thread owner([&](){std::lock_guard<std::mutex> lock(g_inputHostMx);SetEvent(entered);WaitForSingleObject(release,3000);});
        const bool held=WaitForSingleObject(entered,1500)==WAIT_OBJECT_0;
        unknown=inputHostProcessLivenessTry();
        check("actual nonblocking getter returns unknown under another thread's lock",held&&unknown==-1);
        check("legacy bool reduction falsely classifies this unknown as missing",unknown==-1&&!(unknown==1));
        for(const char* board:{"MS-1T41","MS-1T42","MS-1T52","MS-1T8K","MS-1T91"}) {
            g_machineIdentity.product=board;
            check("all registered MSI boards defer only when fix is enabled",msiHostLifecycleAdmissionAllowed(unknown)==!kMsiHostLivenessUnknownFixBuild);
        }
        g_machineIdentity.product="MS-1T91";const auto topology=g_xinputTopologyEpoch.load();
        const bool allowed=msiHostLifecycleAdmissionAllowed(unknown);
        // All other admission facts held true for this substep. These are the
        // actual queue/dirty primitives, not a fabricated target or mode write.
        if(allowed){inputHostQueueLifecycleWork("initial-target-admission");xinputSlotsMarkDiscoveryDirty();}
        check("actual downstream queue is suppressed for unknown MSI in E; legacy D remains reproduced",g_inputHostLifecycleWorkRequested.load()==!kMsiHostLivenessUnknownFixBuild);
        check("unknown MSI cannot manufacture a topology generation in E",g_xinputTopologyEpoch.load()==topology+(kMsiHostLivenessUnknownFixBuild?0:1));
        {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);g_inputHostLifecycleWorkQueue.clear();}g_inputHostLifecycleWorkRequested=false;
        const auto count=g_msiHostLivenessDeferredTicks.load();
        for(const auto family:{YmccFamily::AsusRogAlly,YmccFamily::AsusRogAllyX,YmccFamily::AsusRogAllyClassic,YmccFamily::AsusRogAllyXClassic,YmccFamily::Unknown}) {
            g_machineIdentity.family=family;check("ROG/common unknown behavior is explicitly left unchanged",msiHostLifecycleAdmissionAllowed(unknown));
        }
        g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.product="MS-unknown";
        check("unregistered MSI model does not gain a control-policy change",msiHostLifecycleAdmissionAllowed(unknown));
        g_machineIdentity.product="MS-1T91";g_machineIdentity.manufacturer="ASUSTeK COMPUTER INC.";
        check("spoofed MSI board text does not gain a control-policy change",msiHostLifecycleAdmissionAllowed(unknown));
        check("non-MSI/unknown/spoofed identities produce no MSI counter",g_msiHostLivenessDeferredTicks.load()==count);
        g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";
        SetEvent(release);owner.join();
        const int alive=inputHostProcessLivenessTry();
        check("same real process is actually alive after mutex release",alive==1);
        check("known alive resumes eligibility; unknown never overwrites a Host handle",msiHostLifecycleAdmissionAllowed(alive)&&g_inputHostProcess==live);
        g_inputHostProcess=nullptr;const int absent=inputHostProcessLivenessTry();
        check("absent handle remains known missing and admission eligible",absent==0&&msiHostLifecycleAdmissionAllowed(absent));
        check("negative observation itself never advances Host epoch",g_inputHostEpochMirror.load()==oldEpoch);
        const auto before=g_xinputTopologyEpoch.load();if(msiHostLifecycleAdmissionAllowed(absent)){inputHostQueueLifecycleWork("initial-target-admission");xinputSlotsMarkDiscoveryDirty();}
        check("actual known-missing startup primitives are not blocked",g_inputHostLifecycleWorkRequested.load()&&g_xinputTopologyEpoch.load()==before+1);
        {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);g_inputHostLifecycleWorkQueue.clear();}g_inputHostLifecycleWorkRequested=false;
        // Explicit persona intent is retried normally once an observation is
        // known. The gate neither mutates nor consumes that intent.
        const auto persona=g_realStickTest.virtualTargetPersona;g_realStickTest.virtualTargetPersona="steamdeck";
        (void)msiHostLifecycleAdmissionAllowed(unknown);
        check("busy MSI observation cannot erase explicit persona intent",g_realStickTest.virtualTargetPersona=="steamdeck");
        if(msiHostLifecycleAdmissionAllowed(alive))inputHostQueueLifecycleWork("persona-transition");
        check("known observation still admits explicit persona transition",g_inputHostLifecycleWorkRequested.load());g_realStickTest.virtualTargetPersona=persona;
    }
    if(entered)CloseHandle(entered);if(release)CloseHandle(release);if(live)CloseHandle(live);g_inputHostProcess=oldProcess;
    const auto deferred=g_msiHostLivenessDeferredTicks.load();g_domainLogEnabled[1]=true;
    msiHostLivenessFlushDeferredReceipt();g_nativeDetailLog.flush();g_nativeDetailLog.close();
    std::ifstream in(logDir/L"native-detail.log");unsigned receipts=0;bool truthful=true;std::string line;
    while(std::getline(in,line))try{const auto row=json::parse(line);if(row.value("event",std::string{})=="msi-host-liveness-admission"){++receipts;truthful=truthful&&row.at("deferredTicksSinceLastReceipt")==deferred&&row.at("unknownMeansDead")==false&&row.at("newTargetRequestedByReceipt")==false&&row.at("perOccurrenceEpochProven")==false;}}catch(...){truthful=false;}
    check("actual periodic receipt is detailed only and preserves uncertainty",kMsiHostLivenessUnknownFixBuild?(receipts==1&&truthful):(receipts==0));
    check("new receipt is not promoted into the common base ledger",!nativeLifecycleEventIsBase("msi-host-liveness-admission"));
    g_nativeLifecycleLog.close();g_nativeLifecycleLog.clear();g_nativeDetailLog.clear();g_appDataDir=oldAppDir;g_domainLogEnabled[1]=oldDetail;
    g_machineIdentity=machine;g_inputHostExitFence=oldFence;g_inputHostEpochMirror=oldEpoch;g_msiHostLivenessDeferredTicks=oldCounter;
    g_xinputTopologyEpoch=oldTopology;g_xinputDiscoveryDirty=oldDirty;g_xinputDiscoveryDirtyTick=oldDirtyTick;
    {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);g_inputHostLifecycleWorkQueue=oldQueue;}
    g_inputHostLifecycleWorkRequested=oldRequested;g_inputHostLifecycleWorkInFlight=oldInFlight;
    {std::lock_guard<std::mutex> lock(g_switchAudit.mx);g_switchAudit.enqueuedTick=oldEnqueuedTick;g_switchAudit.enqueuedCount=oldEnqueuedCount;}
    report<<"fullHardwareBusPolled=false productionHostLaunched=false\nfixBuild="<<kMsiHostLivenessUnknownFixBuild<<" cases="<<cases<<" failed="<<failed<<"\n";
    std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-host-liveness-admission-regression.txt");out<<report.str();return failed==0&&out.good();
}
