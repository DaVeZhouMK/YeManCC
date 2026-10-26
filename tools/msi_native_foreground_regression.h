#pragma once
// Invoked only by the production-TU offline runner; all device/process I/O is
// injected. Test the real runtime, not a duplicate implementation of it.
static std::pair<unsigned,unsigned> msiNativeForegroundRegression() {
    unsigned cases=0,failures=0,discoveries=0,inventories=0,xusbInventories=0,reads=0,polls=0,cloakWrites=0;
    bool ui=true,cloak=true,exempt=true,inverseOff=true,readOk=true,hideOk=true,unhideOk=true,cloakOk=true;
    bool mainPresent=true,rootPresent=true,loseFocus=false,stalePower=false,staleEdge=false,cloakReadbackMismatch=false;
    unsigned sourceCount=1;
    unsigned peer1902=0; // 1=physical gaming,2=virtual gaming,3=auxiliary
    bool thirdGamePresent=true,currentInventoryComplete=true;
    unsigned thirdGameUsage=5;
    int gameRefreshResult=0;
    json gameRefreshTerminalDiagnostic=nullptr;
    std::size_t gameRefreshTerminalDiagnosticWrites=0;
    std::vector<std::string> gameRefreshes;
    int cycleResult=0,pollResult=-1;
    std::function<void()> duringHideCycle;
    std::vector<std::wstring> hidden,apps;
    std::vector<std::pair<std::wstring,bool>> writes;
    std::vector<bool> cycles;
    std::vector<std::string> jobs,virtualJobs;
    const std::string root="USB\\VID_0DB0&PID_1901\\00006F64096B22E7";
    std::string candidateRoot=root;
    MsiNativeForegroundXusbInventory xusbInventory;
    bool loseXusbLocation=false,failXusbHide=false;
    const std::string xusbId="USB\\VID_0DB0&PID_1901&IG_00\\XUSB-GAME";
    const std::wstring location=L"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4)";
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<"native-only: "<<name<<"\n";};
    auto source=[&](){HidHidePhysicalIdentity s;s.resolved=true;s.vendorId=0x0db0;s.productId=0x1901;s.usagePage=1;s.usage=5;
        s.deviceInstanceId="HID\\VID_0DB0&PID_1901&IG_01\\PHYSICAL";s.baseContainerDeviceInstanceId=candidateRoot;return s;};
    auto returnedGameSource=[&](){auto value=source();value.deviceInstanceId="HID\\VID_0DB0&PID_1901&IG_03\\PHYSICAL";return value;};
    auto row=[&](const std::string& id,unsigned vid,unsigned pid,unsigned page,unsigned usage,const std::string& base){
        HidHideInterfaceRecord r;r.instanceId=id;r.baseContainerDeviceInstanceId=base;r.vendorId=vid;r.productId=pid;r.usagePage=page;r.usage=usage;return r;};
    auto reset=[&](){
        g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";
        g_machineIdentity.product="MS-1T91";g_machineIdentity.resolved=true;
        g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=false;g_realStickTest.virtualTargetPersona="disabled";
        g_inputHostProcess=nullptr;g_inputHostReleaseQuarantined=false;g_inputHostExitFence=false;
        g_hidHideVisibilityJournal=HidHideVisibilityJournal{};
        {std::lock_guard<std::mutex> carriedLock(g_hidHideCarriedMx);g_hidHideCarriedResponsibility=HidHideCarriedResponsibility{};}
        // R3/journal ownership belongs to one case, including legacy no-ticket cases.
        g_virtualStartProtection.~VirtualStartProtection();
        new (&g_virtualStartProtection) ymcc::VirtualStartProtection();
        g_inputHostStartProtectionBoundTicket=0;
        g_inputReady=true;g_powerGeneration=3;g_powerLifecycle=PowerLifecycle::Ready;
        g_physicalReleaseWindowStage=0;g_physicalReleaseWindowTransition=0;g_physicalReleaseWindowNeutral=false;
        g_msiNativeForegroundLease=MsiNativeForegroundLease{};g_msiNativeForegroundEdge=1;
        g_msiNativeForegroundSourceRetryNeeded=false;g_msiNativeForegroundSourceRetryGeneration=0;
        g_msiNativeForegroundWatchGameSources=false;
        thirdGamePresent=currentInventoryComplete=true;thirdGameUsage=5;gameRefreshResult=0;gameRefreshes.clear();
        gameRefreshTerminalDiagnostic=nullptr;gameRefreshTerminalDiagnosticWrites=0;
        g_msiNativeForegroundTestIo.arrivalSource={};
        g_msiNativeForegroundDesiredUi=false;g_msiNativeForegroundCyclePending=false;g_msiNativeForegroundResumePending=false;
        g_msiNativeForegroundPowerHold=false;g_msiNativeForegroundSuspendedGeneration=0;g_msiNativeForegroundLastResumeGeneration=0;
        g_msiNativeForegroundObserver=nullptr;g_msiNativeForegroundObserverActive=true;g_msiNativeForegroundObserverRequested=false;
        discoveries=inventories=xusbInventories=reads=polls=cloakWrites=0;sourceCount=1;candidateRoot=root;peer1902=0;
        xusbInventory=MsiNativeForegroundXusbInventory{};xusbInventory.complete=true;loseXusbLocation=failXusbHide=false;duringHideCycle={};
        ui=cloak=exempt=inverseOff=readOk=hideOk=unhideOk=cloakOk=mainPresent=rootPresent=true;
        loseFocus=stalePower=staleEdge=cloakReadbackMismatch=false;cycleResult=0;pollResult=-1;
        hidden.clear();writes.clear();cycles.clear();jobs.clear();virtualJobs.clear();apps={hidHideCurrentModulePath()};
    };
    g_msiForegroundTestIo.locations=[&](const std::string& r){return rootPresent && r==root?std::vector<std::wstring>{location}:std::vector<std::wstring>{};};
    g_msiNativeForegroundTestIo.candidates=[&](){++discoveries;std::vector<HidHidePhysicalIdentity> result;
        for(unsigned n=0;n<sourceCount;++n)result.push_back(source());
        if(peer1902){auto peer=source();peer.productId=0x1902;
            peer.deviceInstanceId="HID\\VID_0DB0&PID_1902&MI_00&COL01\\PEER";
            peer.baseContainerDeviceInstanceId=peer1902==2?"ROOT\\HIDMAESTRO\\VIRTUAL":"USB\\VID_0DB0&PID_1902\\PEER";
            if(peer1902==3)peer.usagePage=0xff00;result.push_back(peer);}
        return result;};
    g_msiNativeForegroundTestIo.inventory=[&](const HidHidePhysicalIdentity&){++inventories;
        std::vector<HidHideInterfaceRecord> rows;
        if(mainPresent)rows.push_back(row(source().deviceInstanceId,0x0db0,0x1901,1,5,root));
        rows.push_back(row("HID\\VID_0DB0&PID_1901&IG_02\\PHYSICAL",0x0db0,0x1901,1,5,root));
        if(thirdGamePresent)rows.push_back(row("HID\\VID_0DB0&PID_1901&IG_03\\PHYSICAL",0x0db0,0x1901,1,thirdGameUsage,root));
        rows.push_back(row("HID\\VENDOR",0x0db0,0x1901,0xff00,1,root));
        rows.push_back(row("HID\\KEYBOARD",0x0db0,0x1901,1,6,root));
        rows.push_back(row("HID\\MOUSE",0x0db0,0x1901,1,2,root));
        rows.push_back(row("HID\\CONSUMER",0x0db0,0x1901,0x0c,1,root));
        rows.push_back(row("HID\\SENSOR",0x0db0,0x1901,0x20,1,root));
        rows.push_back(row("HID\\OTHER-MSI",0x0db0,0x1901,1,5,"USB\\OTHER"));
        rows.push_back(row("HID\\OTHER-VENDOR",0x0b05,0x1901,1,5,root));
        rows.push_back(row("HID\\VIGEM\\VIRTUAL",0x0db0,0x1901,1,5,root));
        if(stalePower)g_powerGeneration.fetch_add(1);
        if(staleEdge)g_msiNativeForegroundEdge.fetch_add(1);
        return rows;};
    g_msiNativeForegroundTestIo.xusbInventory=[&](const HidHidePhysicalIdentity&){++xusbInventories;if(loseXusbLocation)rootPresent=false;return xusbInventory;};
    g_msiNativeForegroundTestIo.snapshot=[&](HidHideVisibilitySnapshot& s){++reads;if(!readOk)return false;
        s.valid=true;s.cloaking=cloak;s.inverseRaw=inverseOff?"off":"on";s.hidden=hidden;s.applications=exempt?apps:std::vector<std::wstring>{};return true;};
    g_msiNativeForegroundTestIo.visibility=[&](const std::wstring& path,bool hide){writes.push_back({path,hide});
        if(hide && (!hideOk || (failXusbHide && hidHideContainsPath({path},xusbId))))return false;if(!hide && !unhideOk)return false;
        if(hide){if(!hidHideContainsPath(hidden,W2U(path)))hidden.push_back(path);}
        else hidden.erase(std::remove_if(hidden.begin(),hidden.end(),[&](const auto& x){return hidHideContainsPath({x},W2U(path));}),hidden.end());
        if(loseFocus && hide)ui=false;return true;};
    g_msiNativeForegroundTestIo.cloak=[&](bool enabled){++cloakWrites;if(!cloakOk)return false;if(!cloakReadbackMismatch)cloak=enabled;return true;};
    g_msiNativeForegroundTestIo.cycle=[&](const std::string& r,const std::wstring& l,bool restoring){
        if(r!=root || l!=location){++failures;std::cout<<"FAIL native-only: cycle exact root/location\n";}
        if(!restoring && duringHideCycle)duringHideCycle();
        cycles.push_back(restoring);return cycleResult;};
    g_msiNativeForegroundTestIo.poll=[&](){++polls;return pollResult;};
    g_msiNativeForegroundTestIo.ui=[&](){return ui;};
    g_msiNativeForegroundTestIo.queue=[&](){jobs.push_back("msi-native-foreground");};
    g_msiNativeForegroundTestIo.virtualQueue=[&](const std::string& r){virtualJobs.push_back(r);};
    unsigned observerQueues=0,observerStarts=0,observerStops=0;
    bool observerResult=true;
    g_msiNativeForegroundTestIo.observerQueue=[&](){++observerQueues;};
    g_msiNativeForegroundTestIo.observerStart=[&](){++observerStarts;return observerResult;};
    g_msiNativeForegroundTestIo.observerStop=[&](){++observerStops;};
    g_msiNativeForegroundTestIo.inventoryComplete=[&](){return currentInventoryComplete;};
    g_msiNativeForegroundTestIo.gameRefresh=[&](const HidHidePhysicalIdentity& target,const std::wstring& position){
        gameRefreshes.push_back(target.deviceInstanceId);
        if(target.baseContainerDeviceInstanceId!=root || position!=location){++failures;return 2;}
        return gameRefreshResult;
    };
    auto enter=[&](){msiNativeForegroundUiEvent();msiNativeForegroundHandleWork();};
    auto leave=[&](){ui=false;msiNativeForegroundUiEvent();msiNativeForegroundHandleWork();};
    reset();enter();
    check("virtual-off foreground hides one root and three capabilities-proven game inputs",hidden.size()==4 && writes.size()==4 && cycles==std::vector<bool>{false});
    check("auxiliary HID, keyboard, mouse, consumer, sensor, foreign and virtual are untouched",std::all_of(hidden.begin(),hidden.end(),[](const auto& p){return p.find(L"VID_0DB0&PID_1901")!=std::wstring::npos;}));
    check("no virtual journal, Host, target or firmware mode is created",!g_hidHideVisibilityJournal.active && !g_inputHostProcess && !g_realStickTest.virtualTargetEnabled && g_realStickTest.virtualTargetPersona=="disabled");
    const auto readCount=reads;
    for(unsigned i=0;i<1000;++i){msiNativeForegroundUiEvent();msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();}
    check("stable show/activate/resize does not discover, readback, refresh or queue",discoveries==1 && inventories==1 && reads==readCount && cycles.size()==1 && jobs.size()==1 && polls==0);
    leave();check("loss of focus returns only own configuration then refreshes exactly once",hidden.empty() && !g_msiNativeForegroundLease.active && cycles==std::vector<bool>({false,true}));
    for(unsigned i=0;i<100;++i)msiNativeForegroundHandleWork();
    check("consumed loss-of-focus edge never repeats restore or scan",cycles.size()==2 && discoveries==1);
    for(const char* board:{"MS-1T41","MS-1T42","MS-1T52","MS-1T8K","MS-1T91"}){reset();g_machineIdentity.product=board;enter();check(board,hidden.size()==4);leave();}
    for(unsigned scenario=0;scenario<4;++scenario){reset();
        if(scenario==0){g_machineIdentity.family=YmccFamily::Unknown;g_machineIdentity.manufacturer="ASUSTeK COMPUTER INC.";}
        if(scenario==1)g_machineIdentity.product="UNKNOWN";
        if(scenario==2)g_machineIdentity.resolved=false;
        if(scenario==3)g_machineIdentity.manufacturer="OTHER";
        enter();msiNativeForegroundPowerEvent(PBT_APMSUSPEND);msiNativeForegroundMaintenance();msiNativeForegroundExitCleanup();
        check("ROG/other/unresolved/unknown board has zero new I/O and jobs",discoveries==0 && reads==0 && cycles.empty() && jobs.empty() && writes.empty());}
    for(unsigned scenario=0;scenario<8;++scenario){reset();
        if(scenario==0)g_realStickTest.virtualTargetPersona="ps5";
        if(scenario==1)g_realStickTest.virtualTargetEnabled=true;
        if(scenario==2)g_inputHostProcess=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(1));
        if(scenario==3)g_hidHideVisibilityJournal.active=true;
        if(scenario==4)g_inputHostReleaseQuarantined=true;
        if(scenario==5)g_inputReady=false;
        if(scenario==6)g_inputHostExitFence=true;
        if(scenario==7)g_physicalReleaseWindowTransition=1;
        enter();check("persona/Host/quarantine/power/exit/borrow gate rejects before device I/O",discoveries==0 && writes.empty() && cycles.empty());}
    reset();sourceCount=2;enter();check("multiple current roots are not guessed",writes.empty() && cycles.empty());
    reset();sourceCount=0;enter();check("missing current source is not guessed",writes.empty() && cycles.empty());
    reset();peer1902=1;enter();check("mixed current 1901 and physical 1902 gaming roots are not guessed",inventories==0 && reads==0 && writes.empty() && cycles.empty());
    reset();peer1902=2;enter();check("virtual 1902 peer is not mistaken for a second physical source",hidden.size()==4 && cycles.size()==1);leave();
    reset();peer1902=3;enter();check("auxiliary 1902 collection is not a gaming source or hidden target",hidden.size()==4 && cycles.size()==1);leave();
    for(const auto& invalid:std::vector<std::string>{"USB\\VID_0DB0&PID_1902\\X","HID\\VID_0DB0&PID_1901\\X",
        "USB\\VID_0DB0&PID_1901&IG_02\\X",root+"\\EXTRA",root+"\" --bad"}){
        reset();candidateRoot=invalid;enter();
        check("invalid/alias/foreign USB root rejected BEFORE inventory/query/hide/refresh",inventories==0 && reads==0 && writes.empty() && cycles.empty());
    }
    check("one-shot and native root contract admit actual 1901 device root",ymcc::msi::nativeForegroundRootCandidate(root));
    check("native root contract admits short and lowercase serial form",ymcc::msi::nativeForegroundRootCandidate("usb\\vid_0db0&pid_1901\\x"));
    reset();mainPresent=false;enter();check("selected game node must still be in current inventory",writes.empty());
    reset();rootPresent=false;enter();check("missing current physical location never writes",writes.empty());
    reset();exempt=false;enter();check("native reader exemption required",writes.empty());
    reset();inverseOff=false;enter();check("inverse allow-list mode rejected",writes.empty());
    reset();stalePower=true;enter();check("source discovery cannot cross a power generation",writes.empty());
    reset();staleEdge=true;enter();check("source discovery cannot cross a UI edge",writes.empty());
    reset();readOk=false;enter();msiNativeForegroundHandleWork();check("snapshot failure is bounded to same edge",writes.empty() && discoveries==1);
    reset();cloak=false;enter();check("native foreground can borrow disabled cloak",cloak && cloakWrites==1);leave();check("unchanged third-party state returns original cloak",!cloak && cloakWrites==2 && hidden.empty());
    reset();cloak=false;enter();hidden.push_back(L"HID\\THIRD-PARTY-NEW");apps.push_back(L"C:\\ThirdParty.exe");leave();
    check("new third-party hidden/app registrations preserve acquired cloak",cloak && cloakWrites==1 && hidden==std::vector<std::wstring>{L"HID\\THIRD-PARTY-NEW"} && apps.size()==2);
    reset();hidden.push_back(U2W(source().deviceInstanceId));enter();check("pre-hidden source is not claimed",g_msiNativeForegroundLease.owned.size()==3);leave();
    check("pre-hidden source stays hidden after focus return",hidden==std::vector<std::wstring>{U2W(source().deviceInstanceId)});
    reset();hideOk=false;enter();check("failed hide is rolled back without claiming isolation",hidden.empty() && !g_msiNativeForegroundLease.active && cycles==std::vector<bool>{true});
    reset();loseFocus=true;enter();check("focus loss during hide rolls back all partial writes",hidden.empty() && !g_msiNativeForegroundLease.active && cycles==std::vector<bool>{true});
    reset();enter();unhideOk=false;leave();check("failed unhide retains exact owned-path responsibility",g_msiNativeForegroundLease.active && !g_msiNativeForegroundLease.configurationReturned && hidden.size()==4 && cycles.size()==1);
    unhideOk=true;msiNativeForegroundUiEvent(true);msiNativeForegroundHandleWork();check("new user edge can complete prior configuration return",hidden.empty() && !g_msiNativeForegroundLease.active && cycles.size()==2);
    reset();cloak=false;enter();cloakReadbackMismatch=true;leave();check("cloak command success alone is not restoration proof",g_msiNativeForegroundLease.active && !g_msiNativeForegroundLease.configurationReturned && cycles.size()==1);
    cloakReadbackMismatch=false;msiNativeForegroundUiEvent(true);msiNativeForegroundHandleWork();check("verified cloak readback completes later restore",!g_msiNativeForegroundLease.active && !cloak);
    reset();cycleResult=2;enter();
    check("real-log hide-refresh failure keeps unresolved evidence after returning config",g_msiNativeForegroundLease.active && g_msiNativeForegroundLease.configurationReturned && g_msiNativeForegroundLease.refreshFailed && hidden.empty() && cycles.size()==2);
    const auto failedCycles=cycles.size();for(unsigned i=0;i<100;++i){msiNativeForegroundHandleWork();msiNativeForegroundMaintenance();}
    check("terminal helper failure is not retried every lifecycle tick",cycles.size()==failedCycles && polls==0);
    cycleResult=0;ui=false;msiNativeForegroundUiEvent();msiNativeForegroundHandleWork();
    check("one fresh focus edge permits one restore retry",!g_msiNativeForegroundLease.active && cycles.size()==failedCycles+1);
    reset();enter();cycleResult=2;leave();check("terminal restore-cycle failure is retained, not reported restored",g_msiNativeForegroundLease.active && g_msiNativeForegroundLease.refreshFailed && g_msiNativeForegroundLease.configurationReturned);
    cycleResult=0;msiNativeForegroundUiEvent(true);msiNativeForegroundHandleWork();check("fresh edge resolves terminal restore debt without new hide",!g_msiNativeForegroundLease.active && hidden.empty() && cycles.size()==3);
    reset();cycleResult=1;enter();check("slow hide helper retains one pending identity",g_msiNativeForegroundCyclePending && cycles.size()==1);
    leave();check("focus return during live helper unhides config without launching replacement",g_msiNativeForegroundLease.configurationReturned && hidden.empty() && g_msiNativeForegroundCyclePending && cycles.size()==1);
    for(unsigned i=0;i<25;++i)msiNativeForegroundMaintenance();check("polls only the same live helper, no new attempts",polls==25 && cycles.size()==1);
    pollResult=0;cycleResult=0;msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
    check("finished hide helper is followed by one required restore refresh",!g_msiNativeForegroundCyclePending && !g_msiNativeForegroundLease.active && cycles==std::vector<bool>({false,true}));
    reset();enter();cycleResult=1;leave();pollResult=0;cycleResult=0;msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
    check("restore-helper completion is consumed, never reissued",!g_msiNativeForegroundLease.active && cycles.size()==2);
    reset();cycleResult=1;enter();g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="ps5";
    check("virtual switch waits for live native refresh",!msiNativeForegroundBeforeVirtualWork("persona-transition") && cycles.size()==1 && hidden.empty());
    check("duplicate switch intent is deferred once",!msiNativeForegroundBeforeVirtualWork("persona-transition") && g_msiNativeForegroundLease.deferredVirtualWork.size()==1);
    pollResult=0;cycleResult=0;msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
    check("original virtual command is replayed once after return, not swallowed",virtualJobs==std::vector<std::string>{"persona-transition"} && !g_msiNativeForegroundLease.active && cycles.size()==2);
    reset();cycleResult=2;enter();g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="ps5";
    check("terminal helper failure with proven config return cannot deadlock virtual choice",msiNativeForegroundBeforeVirtualWork("persona-transition") && !g_msiNativeForegroundLease.active && hidden.empty());
    reset();enter();msiNativeForegroundPowerEvent(PBT_APMSUSPEND);msiNativeForegroundHandleWork();
    check("suspend hook closes native gate before shared Ready changes",hidden.empty() && !g_msiNativeForegroundLease.active && cycles.size()==1 && g_msiNativeForegroundPowerHold);
    msiNativeForegroundPowerEvent(PBT_APMRESUMEAUTOMATIC);msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
    check("resume cannot enter on stale ready generation",g_msiNativeForegroundPowerHold && cycles.size()==1);
    g_powerGeneration=4;msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
    check("ready new power generation has one bounded re-entry",!g_msiNativeForegroundPowerHold && g_msiNativeForegroundLease.active && cycles.size()==2);
    const auto cyclesAfterWake=cycles.size();
    msiNativeForegroundPowerEvent(PBT_APMRESUMESUSPEND);msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
    check("duplicate resume notification has no extra hide, unhide or port refresh",!g_msiNativeForegroundPowerHold && cycles.size()==cyclesAfterWake && g_msiNativeForegroundLease.active);
    reset();g_powerLifecycle=PowerLifecycle::Resuming;g_inputReady=false;g_powerGeneration=4;
    msiNativeForegroundPowerEvent(PBT_APMRESUMEAUTOMATIC);msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
    check("cold resume after shared generation publication still waits for reader ready",g_msiNativeForegroundPowerHold && cycles.empty());
    g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
    check("cold resume enters once reader and generation ready",g_msiNativeForegroundLease.active && cycles.size()==1);
    reset();enter();rootPresent=false;leave();check("gone/relocated old source returns config without cycling guessed root",hidden.empty() && !g_msiNativeForegroundLease.active && cycles.size()==1);
    reset();cycleResult=1;enter();g_msiNativeForegroundLease.deferredVirtualWork.push_back("persona-transition");g_inputHostExitFence=true;msiNativeForegroundExitCleanup();
    check("exit returns owned config even when actual helper is still pending",hidden.empty() && g_msiNativeForegroundLease.configurationReturned && g_msiNativeForegroundCyclePending && cycles.size()==1);
    check("exit never replays a deferred target command",virtualJobs.empty() && g_msiNativeForegroundLease.deferredVirtualWork.empty());
    reset();sourceCount=0;enter();check("failed initial source discovery arms only event-based retry",g_msiNativeForegroundSourceRetryNeeded && discoveries==1);
    sourceCount=1;msiNativeForegroundObserveSource(source(),11);msiNativeForegroundHandleWork();
    check("warm resolved source retries without a new focus event",hidden.size()==4 && cycles.size()==1 && discoveries==2 && jobs.size()==2);
    for(unsigned i=0;i<1000;++i)msiNativeForegroundObserveSource(source(),11);
    check("accepted native lease never restarts on repeated RawInput metadata",cycles.size()==1 && discoveries==2 && jobs.size()==2);
    leave();reset();sourceCount=0;enter();
    for(unsigned i=0;i<100;++i){msiNativeForegroundObserveSource(source(),11);msiNativeForegroundHandleWork();}
    check("still-missing source retries once per cache generation, not every report",discoveries==2 && jobs.size()==2 && cycles.empty());
    msiNativeForegroundObserveSource(source(),10);msiNativeForegroundHandleWork();check("older source generation cannot regress retry budget",discoveries==2);
    msiNativeForegroundObserveSource(source(),12);msiNativeForegroundHandleWork();check("new source generation gets one bounded retry",discoveries==3 && jobs.size()==3);
    ui=false;msiNativeForegroundUiEvent();ui=true;msiNativeForegroundUiEvent();msiNativeForegroundHandleWork();
    const auto beforeUiRetry=discoveries;msiNativeForegroundObserveSource(source(),12);msiNativeForegroundHandleWork();
    check("a real new foreground boundary can retry same current source generation",discoveries==beforeUiRetry+1);
    reset();sourceCount=0;enter();ui=false;msiNativeForegroundObserveSource(source(),11);check("background source arrival performs no extra scan or job",discoveries==1 && jobs.size()==1);
    reset();sourceCount=0;enter();g_realStickTest.virtualTargetPersona="steamdeck";msiNativeForegroundObserveSource(source(),11);
    check("configured virtual route never triggers native source-retry work",discoveries==1 && jobs.size()==1);
    check("virtual lifecycle clears stale native retry flag without extra device I/O",msiNativeForegroundBeforeVirtualWork("persona-transition") && !g_msiNativeForegroundSourceRetryNeeded && discoveries==1 && reads==0);
    reset();sourceCount=0;enter();msiNativeForegroundObserveSource(source(),11);msiNativeForegroundHandleWork();
    msiNativeForegroundUiEvent(true);msiNativeForegroundHandleWork();const auto beforeConfigRetry=discoveries;
    msiNativeForegroundObserveSource(source(),11);msiNativeForegroundHandleWork();
    check("explicit config/power-ready boundary can retry same cache generation once",discoveries==beforeConfigRetry+1);
    msiNativeForegroundObserveSource(source(),11);msiNativeForegroundHandleWork();
    check("source-forced work cannot reset and loop its own retry budget",discoveries==beforeConfigRetry+1);

    reset();sourceCount=0;enter();g_msiNativeForegroundPowerHold=true;msiNativeForegroundObserveSource(source(),11);
    check("power hold excludes source warm-return retry",discoveries==1 && jobs.size()==1);
    reset();sourceCount=0;enter();auto virtualSource=source();virtualSource.baseContainerDeviceInstanceId="ROOT\\HIDMAESTRO\\VIRTUAL";
    msiNativeForegroundObserveSource(virtualSource,11);check("virtual/foreign source metadata does not requeue native discovery",discoveries==1 && jobs.size()==1);
    reset();sourceCount=0;enter();g_msiNativeForegroundSourceRetryNeeded=false;readOk=false;
    for(unsigned i=0;i<100;++i)msiNativeForegroundObserveSource(source(),11);
    check("visibility/helper failures are not converted into automatic source retries",discoveries==1 && jobs.size()==1);
    reset();mainPresent=false;readOk=false;enter();msiNativeForegroundObserveSource(source(),11);
    check("real snapshot failure with missing interface does not arm source retry",!g_msiNativeForegroundSourceRetryNeeded && jobs.size()==1 && reads==1);
    reset();mainPresent=false;exempt=false;enter();msiNativeForegroundObserveSource(source(),11);
    check("missing reader exemption never becomes source-retry authority",!g_msiNativeForegroundSourceRetryNeeded && jobs.size()==1);
    reset();mainPresent=false;enter();check("only gaming-inventory gap with valid visibility contract arms retry",g_msiNativeForegroundSourceRetryNeeded && writes.empty());
    mainPresent=true;msiNativeForegroundObserveSource(source(),11);msiNativeForegroundHandleWork();
    check("current gaming interface return completes a bounded native admission",hidden.size()==4 && cycles.size()==1 && jobs.size()==2);leave();

    reset();g_msiNativeForegroundObserverActive=false;observerQueues=observerStarts=observerStops=0;
    enter();check("foreground cannot borrow visibility before observer registration",writes.empty() && cycles.empty());
    check("identity/UI request queues one owner-thread observer registration",observerQueues==1);
    msiNativeForegroundStartObserver();msiNativeForegroundHandleWork();
    check("observer registration triggers initial native owner reconciliation",observerStarts==1 && hidden.size()==4);
    ui=false;msiNativeForegroundObserved(nullptr,EVENT_SYSTEM_FOREGROUND,nullptr,0,0,0,0);msiNativeForegroundHandleWork();
    check("owned-child to external focus returns native lease without main WM_ACTIVATE",hidden.empty() && !g_msiNativeForegroundLease.active);
    msiNativeForegroundStopObserver();check("observer shuts down on owner thread",observerStops==1 && !g_msiNativeForegroundObserverActive);
    reset();g_msiNativeForegroundObserverActive=false;observerResult=false;msiNativeForegroundStartObserver();enter();
    check("failed event hook cannot strand a newly hidden native source",hidden.empty() && cycles.empty());
    reset();g_machineIdentity.product="UNKNOWN";observerQueues=observerStarts=0;g_msiNativeForegroundObserverActive=false;
    msiNativeForegroundIdentityReady();msiNativeForegroundStartObserver();
    check("unknown/non-MSI board registers no hook or owner message",observerQueues==0 && observerStarts==0);

    // Reproduce the missing public XInput game endpoint without any hardware.
    auto xusbRecord=[&](){ymcc::msi::NativeForegroundXusbInterface item;
        item.activeInterface=true;item.ancestorMatched=true;item.instanceId=xusbId;item.ancestorRoot=root;return item;};
    check("XUSB capability and exact physical ancestry admits own game endpoint",ymcc::msi::nativeForegroundXusbTargetAdmitted(root,xusbRecord()));
    auto xusbBad=xusbRecord();xusbBad.activeInterface=false;
    check("IG name without active XUSB capability is not authority",!ymcc::msi::nativeForegroundXusbTargetAdmitted(root,xusbBad));
    xusbBad=xusbRecord();xusbBad.ancestorMatched=false;
    check("matching VID/PID without actual ancestor proof is rejected",!ymcc::msi::nativeForegroundXusbTargetAdmitted(root,xusbBad));
    xusbBad=xusbRecord();xusbBad.ancestorRoot="USB\\VID_0DB0&PID_1901\\OTHER-PORT";
    check("another MSI USB root cannot enter native lease",!ymcc::msi::nativeForegroundXusbTargetAdmitted(root,xusbBad));
    xusbBad=xusbRecord();xusbBad.virtualDevice=true;
    check("virtual XUSB ancestry cannot enter native lease",!ymcc::msi::nativeForegroundXusbTargetAdmitted(root,xusbBad));
    for(const auto& id:std::vector<std::string>{"ROOT\\HIDMAESTRO\\VIRTUAL","HID\\VID_0DB0&PID_1901&IG_00\\GAME",
        "USB\\VID_0B05&PID_1901&IG_00\\ROG","USB\\VID_0DB0&PID_1902&IG_00\\DINPUT",
        "USB\\VID_0DB0&PID_19010&IG_00\\WRONG","USB\\VID_0DB0&PID_1901&IG_00\\A\\EXTRA",
        "USB\\VID_0DB0&PID_1901&IG_00\\\"INJECT","USB\\VID_0DB0&PID_1901&IG_00\\VIGEM"}){
        xusbBad=xusbRecord();xusbBad.instanceId=id;
        check("invalid/foreign/non-XUSB/virtual endpoint is rejected",!ymcc::msi::nativeForegroundXusbTargetAdmitted(root,xusbBad));
    }
    xusbBad=xusbRecord();xusbBad.instanceId=ascii_lower(xusbId);xusbBad.ancestorRoot=ascii_lower(root);
    check("XUSB devnode identity matching is ASCII case-insensitive",ymcc::msi::nativeForegroundXusbTargetAdmitted(root,xusbBad));

    reset();xusbInventory.interfaces={xusbRecord()};enter();
    check("native lease hides exact current XUSB gameplay PDO alongside HID/root",hidden.size()==5 && hidHideContainsPath(hidden,xusbId) && cycles.size()==1);
    check("native XUSB proof does not enable target or issue firmware mode",!g_realStickTest.virtualTargetEnabled && !g_inputHostProcess && g_msiNativeForegroundLease.xusbTargets.size()==1);
    for(unsigned n=0;n<1000;++n){msiNativeForegroundObserveSource(source(),11);msiNativeForegroundHandleWork();}
    check("steady source reports do not scan XUSB or repeat physical cycle",xusbInventories==1 && cycles.size()==1);
    leave();check("foreground release returns self-owned XUSB target",hidden.empty() && cycles.size()==2);
    reset();hidden={U2W(xusbId)};xusbInventory.interfaces={xusbRecord()};enter();leave();
    check("third-party XUSB entry present before lease remains hidden",hidden.size()==1 && hidHideContainsPath(hidden,xusbId));
    reset();xusbInventory.complete=false;xusbInventory.reason="test-incomplete-xusb-inventory";enter();
    check("unknown XUSB inventory cannot hide or cycle guessed endpoints",writes.empty() && cycles.empty() && !g_msiNativeForegroundSourceRetryNeeded);
    reset();xusbBad=xusbRecord();xusbBad.ancestorRoot="USB\\VID_0DB0&PID_1901\\OTHER-PORT";
    xusbInventory.interfaces={xusbBad};enter();
    check("runtime cannot append an injected foreign game endpoint",hidden.size()==4 && !hidHideContainsPath(hidden,xusbId));leave();
    reset();xusbInventory.interfaces={xusbRecord(),xusbRecord()};enter();
    check("duplicate XUSB interfaces do not duplicate hide target or lease ownership",hidden.size()==5 && g_msiNativeForegroundLease.xusbTargets.size()==1 && g_msiNativeForegroundLease.owned.size()==5);leave();
    reset();xusbInventory.interfaces={xusbRecord()};ui=false;enter();
    check("background MSI UI performs no XUSB enumeration or writes",xusbInventories==0 && writes.empty());
    reset();xusbInventory.interfaces={xusbRecord()};g_machineIdentity.family=YmccFamily::AsusRogAllyClassic;enter();
    check("ROG/common route never enters MSI XUSB enumerator",xusbInventories==0 && writes.empty());


    reset();xusbInventory.interfaces={xusbRecord()};loseXusbLocation=true;enter();
    check("root location lost during XUSB enumeration cannot authorize any hide",writes.empty() && cycles.empty() && g_msiNativeForegroundSourceRetryNeeded);
    reset();xusbBad=xusbRecord();xusbBad.instanceId=root;xusbInventory.interfaces={xusbBad};enter();
    check("XUSB root identical to original root deduplicates rather than widens lease",hidden.size()==4 && cycles.size()==1);leave();
    reset();xusbInventory.interfaces={xusbRecord()};failXusbHide=true;enter();
    check("failed XUSB hide returns all partial own configuration",hidden.empty() && !g_msiNativeForegroundLease.active && cycles.size()==1 && cycles[0]);
    reset();xusbInventory.interfaces={xusbRecord()};enter();msiNativeForegroundPowerEvent(PBT_APMSUSPEND);msiNativeForegroundHandleWork();
    check("suspend returns private XUSB entry with no new cycle during power transition",hidden.empty() && cycles.size()==1 && !g_msiNativeForegroundLease.active);
    reset();xusbInventory.interfaces={xusbRecord()};cycleResult=1;enter();
    g_realStickTest.virtualTargetPersona="steamdeck";g_realStickTest.virtualTargetEnabled=true;
    const bool blockedXusbHandoff=!msiNativeForegroundBeforeVirtualWork("persona-transition");
    check("native XUSB config returns but virtual takeover waits for same live helper",blockedXusbHandoff && hidden.empty() && g_msiNativeForegroundCyclePending && cycles.size()==1 && g_msiNativeForegroundLease.deferredVirtualWork.size()==1);
    pollResult=0;cycleResult=0;msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
    check("resolved XUSB handoff replays exact original virtual lifecycle reason",!g_msiNativeForegroundLease.active && !g_msiNativeForegroundCyclePending && virtualJobs.size()==1 && virtualJobs[0]=="persona-transition");
    reset();for(unsigned n=0;n<17;++n){auto item=xusbRecord();item.instanceId+=std::to_string(n);xusbInventory.interfaces.push_back(item);}enter();
    check("XUSB target over-budget cannot hide or cycle a partial subset",writes.empty() && cycles.empty());


    // Integration contract, not a duplicate BUS unit test: the actual private
    // isolation owner holds HostMx during this injected one-shot. A different
    // reader must see unknown, not claim absence/restart, and the private lease
    // must remain correct. This thread exists ONLY in this offline test seam.
    reset();int overlapLiveness=2;bool overlapAdmission=true;
    duringHideCycle=[&](){std::thread reader([&](){
        overlapLiveness=inputHostProcessLivenessTry();
        overlapAdmission=msiHostLifecycleAdmissionAllowed(overlapLiveness);
    });reader.join();};
    enter();
    check("real private isolation Host mutex contention is unknown to BUS reader",overlapLiveness==-1);
    check("MSI tri-state feature defers auto-admission during own isolation transaction",overlapAdmission==!kMsiHostLivenessUnknownFixBuild);
    check("BUS unknown status does not undo accepted native lease or issue another cycle",g_msiNativeForegroundLease.active && hidden.size()==4 && cycles.size()==1);
    leave();const auto afterIsolationLiveness=inputHostProcessLivenessTry();
    check("after actual isolation lock release, absent Host is readable and admission allowed",afterIsolationLiveness==0 && msiHostLifecycleAdmissionAllowed(afterIsolationLiveness));
    check("tri-state integration does not consume native return or pending virtual reasons",hidden.empty() && !g_msiNativeForegroundLease.active && !g_msiNativeForegroundCyclePending);

    reset();thirdGamePresent=false;enter();const auto acceptedEdge=g_msiNativeForegroundLease.edge;
    thirdGamePresent=true;msiNativeForegroundObserveSource(returnedGameSource(),51);msiNativeForegroundHandleWork();
    check("post-cycle same-root game interface is hidden and refreshed without another root cycle",
        hidden.size()==4 && gameRefreshes==std::vector<std::string>{returnedGameSource().deviceInstanceId} && cycles.size()==1);
    check("post-cycle supplement retains the original foreground and power ownership",
        g_msiNativeForegroundLease.active && g_msiNativeForegroundLease.edge==acceptedEdge && g_msiNativeForegroundEdge==acceptedEdge);
    const auto supplementedDiscoveries=discoveries,supplementedReads=reads;
    const auto supplementedJobs=jobs.size();
    for(unsigned packet=0;packet<1000;++packet){msiNativeForegroundObserveSource(returnedGameSource(),51);msiNativeForegroundHandleWork();}
    check("covered returning source adds no steady discovery, reads, helper or jobs",discoveries==supplementedDiscoveries && reads==supplementedReads && jobs.size()==supplementedJobs && gameRefreshes.size()==1);
    leave();check("foreground return releases the late game path with the original private entries",hidden.empty() && !g_msiNativeForegroundLease.active && !g_msiNativeForegroundWatchGameSources && cycles.size()==2);

    reset();thirdGamePresent=false;enter();thirdGamePresent=true;unsigned arrivalReads=0;
    g_msiNativeForegroundTestIo.arrivalSource=[&](HANDLE,HidHidePhysicalIdentity& value,uint64_t& generation){++arrivalReads;value=returnedGameSource();generation=52;return true;};
    msiNativeForegroundSourceArrival(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(12)));msiNativeForegroundHandleWork();
    check("RawInput device arrival covers the new game interface before a user-input packet",arrivalReads==1 && hidden.size()==4 && gameRefreshes.size()==1 && cycles.size()==1);leave();
    msiNativeForegroundSourceArrival(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(12)));
    check("inactive native lease does not resolve arbitrary device arrivals",arrivalReads==1);

    reset();thirdGamePresent=false;enter();thirdGamePresent=true;
    auto foreignGame=returnedGameSource();foreignGame.baseContainerDeviceInstanceId="USB\\VID_0DB0&PID_1901\\OTHER";
    const auto beforeForeignJobs=jobs.size();msiNativeForegroundObserveSource(foreignGame,53);msiNativeForegroundHandleWork();
    check("same vendor different physical root cannot extend a native lease",hidden.size()==3 && jobs.size()==beforeForeignJobs && gameRefreshes.empty());leave();
    reset();thirdGamePresent=false;enter();thirdGamePresent=true;auto auxiliaryGame=returnedGameSource();auxiliaryGame.usagePage=0xff00;
    msiNativeForegroundObserveSource(auxiliaryGame,53);msiNativeForegroundHandleWork();
    check("auxiliary source observation cannot authorize a game-interface refresh",hidden.size()==3 && gameRefreshes.empty());leave();
    reset();thirdGamePresent=false;enter();thirdGamePresent=true;thirdGameUsage=6;
    msiNativeForegroundObserveSource(returnedGameSource(),54);msiNativeForegroundHandleWork();
    check("current keyboard capabilities reject formerly gaming metadata",hidden.size()==3 && gameRefreshes.empty());leave();
    reset();thirdGamePresent=false;enter();thirdGamePresent=true;currentInventoryComplete=false;
    msiNativeForegroundObserveSource(returnedGameSource(),55);msiNativeForegroundHandleWork();
    check("incomplete current inventory cannot extend a native lease",hidden.size()==3 && gameRefreshes.empty());leave();
    reset();thirdGamePresent=false;enter();thirdGamePresent=true;peer1902=1;
    msiNativeForegroundObserveSource(returnedGameSource(),56);msiNativeForegroundHandleWork();
    check("mixed current firmware modes cannot authorize a late game refresh",hidden.size()==3 && gameRefreshes.empty());leave();
    reset();thirdGamePresent=false;enter();thirdGamePresent=true;rootPresent=false;
    msiNativeForegroundObserveSource(returnedGameSource(),57);msiNativeForegroundHandleWork();
    check("late game interface cannot cross a changed physical location",hidden.size()==3 && gameRefreshes.empty());leave();

    reset();thirdGamePresent=false;enter();thirdGamePresent=true;
    const auto inheritedGame=U2W(returnedGameSource().deviceInstanceId);hidden.push_back(inheritedGame);
    msiNativeForegroundObserveSource(returnedGameSource(),58);msiNativeForegroundHandleWork();
    check("already hidden late interface stays inherited and is never refreshed or adopted",hidden.size()==4 && g_msiNativeForegroundLease.owned.size()==3 && gameRefreshes.empty());
    leave();check("native return preserves an inherited late game entry",hidden==std::vector<std::wstring>{inheritedGame});

    for(unsigned transition=0;transition<3;++transition){
        reset();thirdGamePresent=false;enter();thirdGamePresent=true;msiNativeForegroundObserveSource(returnedGameSource(),59);
        if(transition==0){ui=false;msiNativeForegroundUiEvent();}
        if(transition==1)msiNativeForegroundPowerEvent(PBT_APMSUSPEND);
        if(transition==2){g_realStickTest.virtualTargetPersona="steamdeck";g_realStickTest.virtualTargetEnabled=true;}
        msiNativeForegroundHandleWork();
        check("queued late interface cannot outlive focus, power or virtual ownership",hidden.empty() && gameRefreshes.empty() && !g_msiNativeForegroundLease.active);
    }
    reset();thirdGamePresent=false;enter();thirdGamePresent=true;hideOk=false;
    msiNativeForegroundObserveSource(returnedGameSource(),60);msiNativeForegroundHandleWork();
    check("late hide failure returns every partial private entry",hidden.empty() && !g_msiNativeForegroundLease.active && gameRefreshes.empty());
    reset();thirdGamePresent=false;enter();thirdGamePresent=true;gameRefreshResult=2;
    msiNativeForegroundObserveSource(returnedGameSource(),61);msiNativeForegroundHandleWork();
    check("failed late leaf refresh returns configuration through the existing root recovery",hidden.empty() && !g_msiNativeForegroundLease.active && gameRefreshes.size()==1 && cycles.size()==2);
    reset();thirdGamePresent=false;enter();thirdGamePresent=true;gameRefreshResult=1;
    msiNativeForegroundObserveSource(returnedGameSource(),62);msiNativeForegroundHandleWork();
    check("pending late leaf refresh retains its same helper and ownership",g_msiNativeForegroundCyclePending && g_msiNativeForegroundLease.active && gameRefreshes.size()==1 && cycles.size()==1);
    g_realStickTest.virtualTargetPersona="steamdeck";g_realStickTest.virtualTargetEnabled=true;
    check("virtual takeover returns config but waits for the same late-interface helper",!msiNativeForegroundBeforeVirtualWork("persona-transition") && hidden.empty() && g_msiNativeForegroundCyclePending && gameRefreshes.size()==1);
    pollResult=0;msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
    check("terminal late-interface helper permits existing return and exact virtual replay",!g_msiNativeForegroundLease.active && virtualJobs==std::vector<std::string>{"persona-transition"} && gameRefreshes.size()==1 && cycles.size()==2);

    reset();thirdGamePresent=false;enter();
    for(unsigned generation=1;generation<=100;++generation){msiNativeForegroundObserveSource(returnedGameSource(),generation);msiNativeForegroundHandleWork();}
    check("unresolved returning interfaces have a finite per-lease retry budget",g_msiNativeForegroundLease.gameSourceAttempts.size()==16 && discoveries==17 && cycles.size()==1 && gameRefreshes.empty());leave();

    using ymcc::nativeForeground::Profile;
    check("ROG family policy binds known model and ASUS manufacturer",ymcc::nativeForeground::machineProfile("ASUSTEK COMPUTER INC.","RC73XA")==Profile::Rog);
    check("GPD family policy binds Win5 model and GPD manufacturer",ymcc::nativeForeground::machineProfile("GPD","G1618-05")==Profile::Gpd);
    check("model aliases and manufacturer-only evidence cannot authorize native shielding",
        ymcc::nativeForeground::machineProfile("ASUSTEK COMPUTER INC.","RC73XA-UNKNOWN")==Profile::Unsupported &&
        ymcc::nativeForeground::machineProfile("GPD","UNKNOWN")==Profile::Unsupported &&
        ymcc::nativeForeground::machineProfile("OTHER","G1618-05")==Profile::Unsupported);
    std::string familyRoot,familyHid,familyLateHid,familyXusb;
    unsigned familyVendor=0,familyProduct=0;
    bool familyLatePresent=false;
    bool unrelatedMsiSource=false;
    int familyXusbResult=0;
    std::vector<std::string> xusbRefreshes;
    auto familySource=[&](){HidHidePhysicalIdentity value;value.resolved=true;value.baseContainerDeviceInstanceId=familyRoot;
        value.deviceInstanceId=familyHid;value.vendorId=familyVendor;value.productId=familyProduct;value.usagePage=1;value.usage=5;return value;};
    auto configureFamily=[&](bool rog){
        reset();familyLatePresent=false;unrelatedMsiSource=false;familyXusbResult=0;xusbRefreshes.clear();
        familyVendor=rog?0x0b05:0x045e;familyProduct=rog?0x1abe:0x028e;
        familyRoot=rog?"USB\\VID_0B05&PID_1ABE\\6&A61A827&0&4":"USB\\VID_045E&PID_028E\\7&2EA17D0F&0&1";
        familyHid=rog?"HID\\VID_0B05&PID_1ABE&MI_05&IG_00\\8&2BE06C40&0&0000":"HID\\VID_045E&PID_028E&IG_02\\9&12DC4C13&0&0000";
        familyLateHid=rog?"HID\\VID_0B05&PID_1ABE&MI_05&IG_03\\RETURNED":"HID\\VID_045E&PID_028E&IG_03\\RETURNED";
        familyXusb=rog?"USB\\VID_0B05&PID_1ABE&MI_05\\6&PHYSICAL&0&0005":familyRoot;
        g_machineIdentity.family=rog?YmccFamily::AsusRogAllyX:YmccFamily::Gpd;
        g_machineIdentity.manufacturer=rog?"ASUSTEK COMPUTER INC.":"GPD";g_machineIdentity.product=rog?"RC72LA":"G1618-05";
        g_msiForegroundTestIo.locations=[&](const std::string& candidate){return rootPresent && candidate==familyRoot?std::vector<std::wstring>{location}:std::vector<std::wstring>{};};
        g_msiNativeForegroundTestIo.candidates=[&](){++discoveries;std::vector<HidHidePhysicalIdentity> result;
            for(unsigned index=0;index<sourceCount;++index){auto value=familySource();if(index){value.baseContainerDeviceInstanceId+="-OTHER";value.deviceInstanceId+="-OTHER";}result.push_back(value);}
            if(unrelatedMsiSource){auto peer=familySource();peer.vendorId=0x0db0;peer.productId=0x1902;
                peer.baseContainerDeviceInstanceId="USB\\VID_0DB0&PID_1902\\OTHER-PORT";
                peer.deviceInstanceId="HID\\VID_0DB0&PID_1902&MI_00&COL01\\OTHER-PORT";result.push_back(peer);}
            return result;};
        g_msiNativeForegroundTestIo.inventory=[&](const HidHidePhysicalIdentity&){++inventories;
            std::vector<HidHideInterfaceRecord> result;
            if(mainPresent)result.push_back(row(familyHid,familyVendor,familyProduct,1,5,familyRoot));
            if(familyLatePresent)result.push_back(row(familyLateHid,familyVendor,familyProduct,1,thirdGameUsage,familyRoot));
            result.push_back(row("HID\\VENDOR-CONTROL",familyVendor,familyProduct,0xff31,118,familyRoot));
            result.push_back(row("HID\\KEYBOARD",familyVendor,familyProduct,1,6,familyRoot));
            result.push_back(row("HID\\MOUSE",familyVendor,familyProduct,1,2,familyRoot));
            result.push_back(row("HID\\CONSUMER",familyVendor,familyProduct,12,1,familyRoot));return result;};
        g_msiNativeForegroundTestIo.gameRefresh=[&](const HidHidePhysicalIdentity& value,const std::wstring& position){
            gameRefreshes.push_back(value.deviceInstanceId);if(value.baseContainerDeviceInstanceId!=familyRoot || position!=location){++failures;return 2;}
            if(g_msiNativeForegroundLease.profile==Profile::Gpd && gameRefreshResult==2 && !gameRefreshTerminalDiagnostic.is_null()){
                g_msiNativeForegroundLease.helperDiagnostic=gameRefreshTerminalDiagnostic;
                ++gameRefreshTerminalDiagnosticWrites;
            }
            return gameRefreshResult;};
        g_msiNativeForegroundTestIo.xusbRefresh=[&](const HidHidePhysicalIdentity& value,const std::wstring& position){
            xusbRefreshes.push_back(value.deviceInstanceId);if(value.baseContainerDeviceInstanceId!=familyRoot || value.deviceInstanceId!=familyXusb || position!=location){++failures;return 2;}return familyXusbResult;};
        auto endpoint=ymcc::msi::NativeForegroundXusbInterface{};endpoint.activeInterface=endpoint.ancestorMatched=true;
        endpoint.instanceId=familyXusb;endpoint.ancestorRoot=familyRoot;xusbInventory.interfaces={endpoint};
    };
    for(const bool rog:{true,false}){
        configureFamily(rog);enter();
        check(rog?"ROG native lease covers HID game view and active MI_05 XUSB view":"GPD native lease covers HID game view and proven XUSB root",hidden.size()==2 && hidHideContainsPath(hidden,familyHid) && hidHideContainsPath(hidden,familyXusb));
        check("ROG/GPD use exact game-node refreshes and never USB port CyclePort",gameRefreshes.size()==1 && xusbRefreshes.size()==1 && cycles.empty());
        check("ROG composite root and OEM/keyboard/consumer collections remain outside game targets",(!rog || !hidHideContainsPath(hidden,familyRoot)) && !hidHideContainsPath(hidden,"HID\\VENDOR-CONTROL") && !hidHideContainsPath(hidden,"HID\\KEYBOARD") && !hidHideContainsPath(hidden,"HID\\CONSUMER"));
        const auto stableDiscoveries=discoveries,stableReads=reads;
        for(unsigned packet=0;packet<1000;++packet){msiNativeForegroundObserveSource(familySource(),61);msiNativeForegroundHandleWork();}
        check("ROG/GPD stable foreground adds no discovery, snapshot or refresh",discoveries==stableDiscoveries && reads==stableReads && gameRefreshes.size()==1 && xusbRefreshes.size()==1 && cycles.empty());
        familyLatePresent=true;auto returned=familySource();returned.deviceInstanceId=familyLateHid;
        msiNativeForegroundObserveSource(returned,62);msiNativeForegroundHandleWork();
        check("ROG/GPD returned HID collection joins same lease without touching XUSB again",hidden.size()==3 && gameRefreshes.size()==2 && xusbRefreshes.size()==1 && cycles.empty());
        leave();check("ROG/GPD foreground return releases all private paths and refreshes both current game views",hidden.empty() && !g_msiNativeForegroundLease.active && gameRefreshes.size()==4 && xusbRefreshes.size()==2 && cycles.empty());

        configureFamily(rog);xusbInventory.interfaces.clear();enter();
        check("ROG/GPD without current XUSB-class proof cannot hide or refresh guessed nodes",writes.empty() && gameRefreshes.empty() && xusbRefreshes.empty() && cycles.empty());
        configureFamily(rog);xusbInventory.interfaces.front().ancestorRoot+="-OTHER";enter();
        check("ROG/GPD XUSB on another root cannot authorize foreground shielding",writes.empty() && gameRefreshes.empty() && xusbRefreshes.empty());
        configureFamily(rog);xusbInventory.interfaces.front().virtualDevice=true;enter();
        check("ROG/GPD virtual Xbox lookalike never becomes physical shielding authority",writes.empty() && gameRefreshes.empty() && xusbRefreshes.empty());
        configureFamily(rog);sourceCount=2;enter();
        check("ROG/GPD multiple physical roots are deferred without picking a VID/PID match",writes.empty() && gameRefreshes.empty() && xusbRefreshes.empty());
        configureFamily(rog);g_machineIdentity.product="UNKNOWN";enter();
        check("ROG/GPD unknown model has zero new discovery and driver work",discoveries==0 && reads==0 && writes.empty() && cycles.empty());
        configureFamily(rog);g_realStickTest.virtualTargetPersona="steamdeck";g_realStickTest.virtualTargetEnabled=true;enter();
        check("ROG/GPD virtual route keeps its existing owner and gains no native-only refresh",discoveries==0 && writes.empty() && cycles.empty());

        configureFamily(rog);hidden={U2W(familyHid)};enter();
        check("ROG/GPD preserve preexisting hidden game entry without claiming it",g_msiNativeForegroundLease.owned==std::vector<std::wstring>{U2W(familyXusb)});
        leave();check("ROG/GPD return never removes inherited game entry",hidden==std::vector<std::wstring>{U2W(familyHid)} && cycles.empty());
        configureFamily(rog);enter();const auto hidBeforeSuspend=gameRefreshes.size(),xusbBeforeSuspend=xusbRefreshes.size();
        msiNativeForegroundPowerEvent(PBT_APMSUSPEND);msiNativeForegroundHandleWork();
        check("ROG/GPD suspend returns configuration without another game-node refresh",hidden.empty() && !g_msiNativeForegroundLease.active && gameRefreshes.size()==hidBeforeSuspend && xusbRefreshes.size()==xusbBeforeSuspend && cycles.empty());

        configureFamily(rog);familyXusbResult=1;enter();
        check("ROG/GPD pending XUSB node retains same helper and unfinished refresh series",g_msiNativeForegroundCyclePending && g_msiNativeForegroundLease.refreshSeriesActive && gameRefreshes.size()==1 && xusbRefreshes.size()==1 && cycles.empty());
        for(unsigned repeat=0;repeat<20;++repeat)msiNativeForegroundHandleWork();
        check("ROG/GPD live refresh is not replaced or repeated",gameRefreshes.size()==1 && xusbRefreshes.size()==1 && cycles.empty());
        g_realStickTest.virtualTargetPersona="steamdeck";g_realStickTest.virtualTargetEnabled=true;
        check("ROG/GPD virtual handoff first returns config then waits for same node helper",!msiNativeForegroundBeforeVirtualWork("persona-transition") && hidden.empty() && g_msiNativeForegroundCyclePending);
        familyXusbResult=0;pollResult=0;msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
        check("ROG/GPD terminal node helper permits original virtual reason without port reset",!g_msiNativeForegroundLease.active && virtualJobs==std::vector<std::string>{"persona-transition"} && cycles.empty());

        configureFamily(rog);enter();currentInventoryComplete=false;leave();
        const auto failedPlanInventories=inventories;
        for(unsigned repeat=0;repeat<100;++repeat)msiNativeForegroundHandleWork();
        check("ROG/GPD failed return inventory is bounded on the same foreground edge",hidden.empty() && g_msiNativeForegroundLease.configurationReturned && g_msiNativeForegroundLease.refreshFailed && inventories==failedPlanInventories && cycles.empty());
        g_realStickTest.virtualTargetPersona="steamdeck";g_realStickTest.virtualTargetEnabled=true;
        check("ROG/GPD unverified return plan can yield only after own config is proven returned",msiNativeForegroundBeforeVirtualWork("persona-transition") && !g_msiNativeForegroundLease.active);

        configureFamily(rog);gameRefreshResult=1;enter();familyLatePresent=true;
        returned=familySource();returned.deviceInstanceId=familyLateHid;msiNativeForegroundObserveSource(returned,63);msiNativeForegroundHandleWork();
        check("ROG/GPD interface arrival waits behind existing refresh series",gameRefreshes.size()==1 && xusbRefreshes.empty() && g_msiNativeForegroundLease.pendingGameSources.size()==1);
        gameRefreshResult=0;pollResult=0;msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();msiNativeForegroundHandleWork();
        check("ROG/GPD completed series consumes queued late interface instead of losing its event",hidden.size()==3 && gameRefreshes.size()==2 && xusbRefreshes.size()==1 && cycles.empty());leave();

        configureFamily(rog);gameRefreshResult=2;
        // Only this GPD positive supplies terminal facts, from the actual IO leaf
        // on every failed invocation, including an inverse refresh attempt.
        if(!rog)gameRefreshTerminalDiagnostic={{"transport","delete-on-close-stdout"},{"state","record-accepted"},
            {"scope","helper-self-reported-existing-call-facts"},{"hardwareSuccessProven",false},
            {"reportedFacts",{{"Schema",1},{"Kind","ymcc.gameRefresh.failure"},{"Result",6},{"Stage","disable"},{"Xusb",false},
                {"PrepareLocateResult",0U},{"CurrentLocateResult",0U},{"SameNode",true},
                {"DisableCallEntered",true},{"DisableResult",23U},{"EnableCallEntered",false},{"EnableResult",nullptr},
                {"CompensationChecked",false},{"CompensationAttempted",false},{"CompensationEnableResult",nullptr},
                {"ExceptionType","System.InvalidOperationException"},{"ExceptionHResult",-2146233079}}}};
        enter();
        check("ROG/GPD failed node refresh returns private config without root-cycle fallback",hidden.empty() && g_msiNativeForegroundLease.configurationReturned && g_msiNativeForegroundLease.refreshFailed && cycles.empty());
        g_realStickTest.virtualTargetPersona="steamdeck";g_realStickTest.virtualTargetEnabled=true;
        check("ROG/GPD terminal failed refresh can hand back proven config without claiming external reset",msiNativeForegroundBeforeVirtualWork("persona-transition") && !g_msiNativeForegroundLease.active && cycles.empty());
        if(!rog)check("GPD terminal-failure positive consumes explicit failed Disable facts from every game refresh leaf",
            gameRefreshResult==2&&gameRefreshTerminalDiagnosticWrites>0&&
            gameRefreshTerminalDiagnosticWrites==gameRefreshes.size()&&!gameRefreshTerminalDiagnostic.is_null());

        configureFamily(rog);unrelatedMsiSource=true;enter();
        check("ROG/GPD native entry is independent of an unrelated MSI DInput controller",g_msiNativeForegroundLease.active && hidden.size()==2 && gameRefreshes.size()==1 && xusbRefreshes.size()==1);
        check("ROG/GPD native shielding never adopts another family's physical path",std::none_of(hidden.begin(),hidden.end(),[](const auto& path){return path.find(L"VID_0DB0")!=std::wstring::npos;}) && cycles.empty());leave();
        configureFamily(rog);enter();familyLatePresent=true;unrelatedMsiSource=true;
        returned=familySource();returned.deviceInstanceId=familyLateHid;msiNativeForegroundObserveSource(returned,64);msiNativeForegroundHandleWork();
        check("ROG/GPD late-game coverage is independent of unrelated MSI mode",hidden.size()==3 && gameRefreshes.size()==2 && xusbRefreshes.size()==1 && cycles.empty());leave();

        configureFamily(rog);g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="elite";
        HidHideVisibilitySnapshot virtualBefore;virtualBefore.valid=true;virtualBefore.cloaking=true;virtualBefore.inverseRaw="off";
        const auto virtualTargets=hidHideVirtualPhysicalTargets(familySource(),virtualBefore,1,"offline-regression");
        check(rog?"ROG virtual physical journal plan includes independent MI_05 XUSB node":"GPD virtual physical journal plan retains actual XUSB root",
            std::find(virtualTargets.begin(),virtualTargets.end(),familyXusb)!=virtualTargets.end() && std::find(virtualTargets.begin(),virtualTargets.end(),familyHid)!=virtualTargets.end());
        check("ROG/GPD virtual target planning performs no visibility write or physical refresh",writes.empty() && cycles.empty() && gameRefreshes.empty() && xusbRefreshes.empty() && !g_msiNativeForegroundLease.active);
        familyLatePresent=true;const auto expandedVirtual=hidHideVirtualPhysicalTargets(familySource(),virtualBefore,1,"offline-regression");
        check("ROG/GPD virtual shielding includes other current same-root game collection",std::find(expandedVirtual.begin(),expandedVirtual.end(),familyLateHid)!=expandedVirtual.end() && std::find(expandedVirtual.begin(),expandedVirtual.end(),"HID\\KEYBOARD")==expandedVirtual.end());
        currentInventoryComplete=false;
        check("ROG/GPD partial inventory cannot expand existing virtual physical target policy",hidHideVirtualPhysicalTargets(familySource(),virtualBefore,1,"offline-regression")==hidHidePhysicalTargets(familySource()));
        currentInventoryComplete=true;
        check("ROG/GPD multiple sources cannot widen virtual physical targets",hidHideVirtualPhysicalTargets(familySource(),virtualBefore,2,"offline-regression")==hidHidePhysicalTargets(familySource()));
        virtualBefore.inverseRaw="unknown";
        check("ROG/GPD unknown inverse policy cannot widen virtual targets",hidHideVirtualPhysicalTargets(familySource(),virtualBefore,1,"offline-regression")==hidHidePhysicalTargets(familySource()));
    }

    // GPD async series: launch/poll the real leaf via existing IO seams.
    // Modeled ROG and MSI keep every original assertion and execution policy.
    auto beginPendingGpdSeries=[&](bool restoring,bool lastLeaf){
        configureFamily(false);familyLatePresent=true;
        if(restoring)enter();
        const auto gameRefresh=g_msiNativeForegroundTestIo.gameRefresh;
        g_msiNativeForegroundTestIo.gameRefresh=[&,gameRefresh,lastLeaf](const HidHidePhysicalIdentity& value,const std::wstring& position){
            const int result=gameRefresh(value,position);
            return !lastLeaf && value.deviceInstanceId==familyLateHid?1:result;
        };
        familyXusbResult=lastLeaf?1:0;
        if(restoring)leave();else enter();
        g_msiNativeForegroundTestIo.gameRefresh=gameRefresh;familyXusbResult=0;
    };
    for(const bool restoring:{false,true})for(const bool lastLeaf:{false,true})for(const int terminal:{0,2,3}){
        // 3 is an unrecognized terminal result: never an acceptance receipt.
        beginPendingGpdSeries(restoring,lastLeaf);auto& s=g_msiNativeForegroundLease;
        const auto cursor=s.nextRefreshTarget,total=s.refreshTargets.size();
        const auto refreshes=gameRefreshes.size()+xusbRefreshes.size();
        const auto planInventories=inventories,planXusbInventories=xusbInventories;
        const auto visible=hidden,owned=s.owned;const auto writeCount=writes.size();
        const auto label=std::string("GPD async ")+(restoring?"restore ":"hide ")+(lastLeaf?"last ":"middle ")+
            (terminal==0?"accepted":terminal==2?"failed":"unknown-terminal");
        pollResult=-1;const int live=msiNativeForegroundPollCycleLocked();
        const int waiting=nativeForegroundRefreshLocked(restoring);
        check((label+" live/unknown poll cannot complete or replace the leaf").c_str(),
            live==-1&&waiting==1&&s.profile==Profile::Gpd&&s.refreshSeriesActive&&!s.cycleAccepted&&
            s.syntheticCycle&&g_msiNativeForegroundCyclePending&&total==3&&cursor==(lastLeaf?3u:2u)&&
            s.nextRefreshTarget==cursor&&s.helperGameTarget==(lastLeaf?familyXusb:familyLateHid)&&
            gameRefreshes.size()+xusbRefreshes.size()==refreshes&&cycles.empty()&&polls==1);
        pollResult=terminal;const int polled=msiNativeForegroundPollCycleLocked();
        check((label+" terminal receipt belongs to the same launched leaf").c_str(),
            polled==terminal&&!g_msiNativeForegroundCyclePending&&!s.syntheticCycle&&s.refreshSeriesActive&&
            s.cycleAccepted==(terminal==0)&&s.helperExitCode==DWORD(terminal==0?0:6)&&
            s.nextRefreshTarget==cursor&&s.helperGameTarget==(lastLeaf?familyXusb:familyLateHid)&&polls==2);
        if(restoring&&terminal!=0)g_msiNativeForegroundEdge.fetch_add(1);
        const auto edge=g_msiNativeForegroundEdge.load();
        const int continued=nativeForegroundRefreshLocked(restoring);
        check((label+" same direction requires acceptance before advancing or exhausting").c_str(),
            continued==(terminal==0?0:2)&&!s.refreshSeriesActive&&s.cycleAccepted==(terminal==0)&&
            !g_msiNativeForegroundCyclePending&&s.nextRefreshTarget==(terminal==0?total:cursor)&&
            gameRefreshes.size()+xusbRefreshes.size()==refreshes+((terminal==0&&!lastLeaf)?1u:0u)&&
            inventories==planInventories&&xusbInventories==planXusbInventories&&
            hidden==visible&&s.owned==owned&&writes.size()==writeCount&&cycles.empty()&&polls==2&&
            s.refreshPlanAttemptEdge==edge&&s.refreshPlanForRestore==restoring);
    }
    for(const bool lastLeaf:{false,true})for(const int terminal:{2,3}){
        beginPendingGpdSeries(false,lastLeaf);pollResult=terminal;
        const int polled=msiNativeForegroundPollCycleLocked();
        const auto gameCount=gameRefreshes.size(),xusbCount=xusbRefreshes.size();
        const bool restored=msiNativeForegroundRestoreLocked("gpd-async-series-opposite-direction");
        check((std::string("GPD async ")+(lastLeaf?"last ":"middle ")+(terminal==2?"failed":"unknown-terminal")+
            " hide still permits the existing opposite-direction restore").c_str(),
            polled==terminal&&restored&&hidden.empty()&&!g_msiNativeForegroundLease.active&&
            !g_msiNativeForegroundCyclePending&&gameRefreshes.size()==gameCount+2&&
            xusbRefreshes.size()==xusbCount+1&&cycles.empty()&&polls==1);
    }

    // VC-GPD-NATIVE-VIRTUAL-HANDOFF-20261025-R1: extend the same production-TU
    // fixture. The real native entry creates the donor; only the accepted
    // transaction readback/journal is synthesized at its existing IO boundary.
    // CR03 reservations below model the already-completed physical-return
    // consumer boundary; this header does not prove durable writes/re-hide.
    // No carried pre-adoption and no replacement lifecycle implementation.
    const std::wstring handoffForeign=L"HID\\FOREIGN-PREHIDDEN\\OTHER-PORT";
    auto admitHandoff=[&](){
        g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="elite";
        return g_virtualStartProtection.admit("elite",inputHostStartProtectionNow()).requestId;
    };
    auto holdHandoff=[&](){
        std::lock_guard hostLock(g_inputHostMx);std::lock_guard leaseLock(g_msiNativeForegroundMx);
        return msiNativeForegroundHoldForVirtualLocked("offline-original-virtual-work");
    };
    auto acceptHandoff=[&](const HidHideVisibilitySnapshot& after){
        std::lock_guard hostLock(g_inputHostMx);return msiNativeForegroundAcceptVirtualJournal(after);
    };
    auto finishHandoff=[&](uint64_t ticket,bool continuation){
        std::lock_guard hostLock(g_inputHostMx);msiNativeForegroundVirtualStartFinishedLocked(ticket,continuation);
    };
    auto journalReadback=[&](uint64_t ticket,bool reserveOwned=true){
        g_inputHostStartProtectionBoundTicket=ticket;
        auto& journal=g_hidHideVisibilityJournal;journal=HidHideVisibilityJournal{};
        journal.active=true;journal.transactionId="offline-original-virtual-hide";
        journal.physical=familySource();journal.physicalSources={journal.physical};
        // The original apply consumer has already reserved/reasserted these paths.
        if(reserveOwned)journal.hiddenByUs=g_msiNativeForegroundLease.owned;
        journal.admittedTick=inputHostStartProtectionNow();
        HidHideVisibilitySnapshot after;
        (void)msiNativeForegroundSnapshot(after);journal.before=after;
        return after;
    };
    auto noUnhide=[&](){return std::none_of(writes.begin(),writes.end(),[](const auto& write){return !write.second;});};
    auto noCarried=[&](){const auto carried=hidHideCarriedSnapshot();return !carried.active&&carried.ownedTargets.empty();};
    auto refreshCount=[&](){return gameRefreshes.size()+xusbRefreshes.size()+cycles.size();};

    for(const int terminal:{0,2}){
        configureFamily(false);familyLatePresent=true;hidden={handoffForeign,U2W(familyLateHid)};
        gameRefreshResult=1;enter();
        const auto donor=g_msiNativeForegroundLease.owned;
        const auto preservedHidden=hidden;const auto writeCount=writes.size(),refreshes=refreshCount();
        const auto ticket=admitHandoff();
        check("GPD pending handoff starts with actual private donor and a live helper",
            ticket&&g_msiNativeForegroundLease.active&&g_msiNativeForegroundCyclePending&&donor.size()==2&&
            !hidHideContainsPath(donor,familyLateHid)&&!hidHideContainsPath(donor,W2U(handoffForeign)));
        check("GPD pending helper parks original virtual work without unhide",
            !msiNativeForegroundBeforeVirtualWork("persona-transition")&&
            g_msiNativeForegroundLease.virtualHandoffHeld&&msiNativeForegroundVirtualHandoffRequired(ticket)&&
            !msiNativeForegroundVirtualHandoffRequired(ticket+1)&&hidden==preservedHidden&&writes.size()==writeCount&&noUnhide());
        (void)msiNativeForegroundBeforeVirtualWork("persona-transition");
        (void)msiNativeForegroundBeforeVirtualWork("settings-changed");
        check("GPD parked work is deduplicated under the original native owner",
            g_msiNativeForegroundLease.deferredVirtualWork==std::vector<std::string>({"persona-transition","settings-changed"})&&
            virtualJobs.empty()&&noCarried()&&!g_hidHideVisibilityJournal.active);
        auto pendingAfter=journalReadback(ticket,false);
        check("GPD live helper cannot transfer donor into a premature virtual journal",
            !acceptHandoff(pendingAfter)&&g_msiNativeForegroundLease.owned==donor&&
            g_hidHideVisibilityJournal.hiddenByUs.empty()&&hidden==preservedHidden);
        g_hidHideVisibilityJournal=HidHideVisibilityJournal{};
        ui=false;msiNativeForegroundUiEvent();msiNativeForegroundHandleWork();
        msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
        check("GPD pending helper and focus loss retain hidden donor without polling replacement",
            polls==1&&g_msiNativeForegroundCyclePending&&g_msiNativeForegroundLease.active&&
            !g_msiNativeForegroundLease.configurationReturned&&virtualJobs.empty()&&hidden==preservedHidden&&
            writes.size()==writeCount&&refreshCount()==refreshes&&cloakWrites==0);
        gameRefreshResult=0;pollResult=terminal;msiNativeForegroundMaintenance();
        check("GPD same helper terminal receipt queues native owner before virtual replay",
            polls==2&&!g_msiNativeForegroundCyclePending&&virtualJobs.empty()&&
            g_msiNativeForegroundLease.helperExitCode==DWORD(terminal==0?0:6)&&hidden==preservedHidden);
        msiNativeForegroundHandleWork();msiNativeForegroundHandleWork();
        check(terminal==0?"GPD successful terminal helper preserves hide and replays parked work once":
            "GPD failed terminal helper preserves hide and replays parked work once",
            g_msiNativeForegroundLease.active&&g_msiNativeForegroundLease.virtualHandoffHeld&&
            !g_msiNativeForegroundLease.configurationReturned&&g_msiNativeForegroundLease.owned==donor&&
            virtualJobs==std::vector<std::string>({"persona-transition","settings-changed"})&&
            g_msiNativeForegroundLease.deferredVirtualWork.empty()&&hidden==preservedHidden&&
            writes.size()==writeCount&&refreshCount()==refreshes&&noUnhide()&&noCarried());
        check("GPD replay admits same virtual route while source-pending still requires physical hide",
            msiNativeForegroundBeforeVirtualWork("persona-transition")&&msiNativeForegroundVirtualHandoffRequired(ticket)&&
            !g_inputHostProcess&&!g_hidHideVisibilityJournal.active);
        const auto after=journalReadback(ticket);
        // Complete reservations must survive donor consumption and repeated accept.
        check("GPD accepts fully reserved journal and readback before releasing native donor",
            acceptHandoff(after)&&!g_msiNativeForegroundLease.active&&
            g_msiNativeForegroundLease.owned.empty()&&!msiNativeForegroundVirtualHandoffRequired(ticket)&&
            g_hidHideVisibilityJournal.hiddenByUs.size()==donor.size()&&
            std::all_of(donor.begin(),donor.end(),[&](const auto& path){return hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,W2U(path));}));
        const auto accepted=g_hidHideVisibilityJournal.hiddenByUs;
        check("GPD foreign prehidden same-root and other-port paths stay hidden and unclaimed",
            hidden==preservedHidden&&hidHideContainsPath(hidden,familyLateHid)&&hidHideContainsPath(hidden,W2U(handoffForeign))&&
            !hidHideContainsPath(accepted,familyLateHid)&&!hidHideContainsPath(accepted,W2U(handoffForeign))&&noCarried());
        const bool repeated=acceptHandoff(after);finishHandoff(ticket,false);msiNativeForegroundHandleWork();
        check("GPD repeated accept and Start completion cannot claim twice or refresh",
            repeated&&g_hidHideVisibilityJournal.hiddenByUs==accepted&&hidden==preservedHidden&&
            writes.size()==writeCount&&refreshCount()==refreshes&&cloakWrites==0&&noUnhide()&&virtualJobs.size()==2);
    }

    configureFamily(false);familyLatePresent=true;hidden={handoffForeign};enter();
    const auto allOwned=g_msiNativeForegroundLease.owned;const auto allHidden=hidden;
    const auto allWrites=writes.size(),allRefreshes=refreshCount();const auto verifyingTicket=admitHandoff();
    const bool verifying=g_virtualStartProtection.beginVerification(verifyingTicket,"success",inputHostStartProtectionNow());
    check("GPD original verifying R3 request can retain the complete native donor",
        verifying&&allOwned.size()==3&&holdHandoff()&&msiNativeForegroundVirtualHandoffRequired(verifyingTicket));
    const auto verifyingAfter=journalReadback(verifyingTicket);
    check("GPD verifying handoff consumes reservations for root and every owned game collection",
        acceptHandoff(verifyingAfter)&&g_hidHideVisibilityJournal.hiddenByUs==allOwned&&hidden==allHidden&&
        writes.size()==allWrites&&refreshCount()==allRefreshes&&noUnhide()&&noCarried());

    const char* holdRejections[]={"no R3 request","virtual intent disabled","persona mismatch","existing Host",
        "existing virtual journal","power drift","location drift","cloak changed","private config returned",
        "incomplete selected paths","empty owned donor","restore pending","power not ready","terminal R3 request"};
    for(unsigned scenario=0;scenario<std::size(holdRejections);++scenario){
        configureFamily(false);enter();const auto ticket=admitHandoff();
        if(scenario==0){g_virtualStartProtection.~VirtualStartProtection();new (&g_virtualStartProtection) ymcc::VirtualStartProtection();}
        if(scenario==1)g_realStickTest.virtualTargetEnabled=false;
        if(scenario==2)g_realStickTest.virtualTargetPersona="steamdeck";
        if(scenario==3)g_inputHostProcess=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(1));
        if(scenario==4)g_hidHideVisibilityJournal.active=true;
        if(scenario==5)g_powerGeneration.fetch_add(1);
        if(scenario==6)g_msiForegroundTestIo.locations=[&](const std::string&){return std::vector<std::wstring>{location+L"#OTHER-PORT"};};
        if(scenario==7)g_msiNativeForegroundLease.cloakChanged=true;
        if(scenario==8)g_msiNativeForegroundLease.configurationReturned=true;
        if(scenario==9)g_msiNativeForegroundLease.diagnosticSelectedPathsComplete=false;
        if(scenario==10)g_msiNativeForegroundLease.owned.clear();
        if(scenario==11)g_msiNativeForegroundLease.restorePending=true;
        if(scenario==12)g_inputReady=false;
        if(scenario==13)(void)g_virtualStartProtection.finish(ticket,"failure",inputHostStartProtectionNow());
        const auto beforeHidden=hidden;const auto beforeWrites=writes.size(),beforeRefreshes=refreshCount();
        check((std::string("GPD initial hold refuses ")+holdRejections[scenario]).c_str(),
            !holdHandoff()&&!g_msiNativeForegroundLease.virtualHandoffHeld&&hidden==beforeHidden&&
            writes.size()==beforeWrites&&refreshCount()==beforeRefreshes&&noCarried());
        g_inputHostProcess=nullptr; // Sentinel is never passed to real process IO.
    }

    const char* acceptRejections[]={"root drift","multiple physical sources","unresolved source","virtual source",
        "foreign vendor source","location drift","power drift","R3 request drift","Start bound-ticket drift",
        "invalid after snapshot","missing owned readback","cloak off","inverse on","reader exemption missing",
        "inactive journal","missing physical source","R3 request expired","unbound Start ticket","missing owned reservation"};
    for(unsigned scenario=0;scenario<std::size(acceptRejections);++scenario){
        configureFamily(false);hidden={handoffForeign};enter();const auto ticket=admitHandoff();
        const bool held=holdHandoff();const auto donor=g_msiNativeForegroundLease.owned;bool driftEstablished=true;
        auto after=journalReadback(ticket);auto& journal=g_hidHideVisibilityJournal;
        if(scenario==0){journal.physical.baseContainerDeviceInstanceId+="-OTHER";journal.physicalSources={journal.physical};}
        if(scenario==1)journal.physicalSources.push_back(journal.physical);
        if(scenario==2){journal.physical.resolved=false;journal.physicalSources={journal.physical};}
        if(scenario==3){journal.physical.deviceInstanceId="HID\\VIGEM\\VIRTUAL";journal.physicalSources={journal.physical};}
        if(scenario==4){journal.physical.vendorId=0x0db0;journal.physicalSources={journal.physical};}
        if(scenario==5)g_msiForegroundTestIo.locations=[&](const std::string&){return std::vector<std::wstring>{location+L"#OTHER-PORT"};};
        if(scenario==6)g_powerGeneration.fetch_add(1);
        if(scenario==7){const auto later=inputHostStartProtectionNow()+ymcc::VirtualStartProtection::maxMs;
            const auto replacement=g_virtualStartProtection.admit("elite",later);
            driftEstablished=replacement.accepted&&replacement.requestId!=ticket;}
        if(scenario==8)g_inputHostStartProtectionBoundTicket=ticket+1;
        if(scenario==9)after.valid=false;
        if(scenario==10&&!donor.empty())after.hidden.erase(std::remove(after.hidden.begin(),after.hidden.end(),donor.back()),after.hidden.end());
        if(scenario==11)after.cloaking=false;
        if(scenario==12)after.inverseRaw="on";
        if(scenario==13)after.applications.clear();
        if(scenario==14)journal.active=false;
        if(scenario==15)journal.physicalSources.clear();
        if(scenario==16)(void)g_virtualStartProtection.view(inputHostStartProtectionNow()+ymcc::VirtualStartProtection::maxMs);
        if(scenario==17)g_inputHostStartProtectionBoundTicket=0;
        if(scenario==18&&!donor.empty())journal.hiddenByUs.erase(std::remove(journal.hiddenByUs.begin(),journal.hiddenByUs.end(),donor.back()),journal.hiddenByUs.end());
        const auto beforeReservations=journal.hiddenByUs;
        const auto beforeHidden=hidden;const auto beforeWrites=writes.size(),beforeRefreshes=refreshCount();
        check((std::string("GPD journal refuses ")+acceptRejections[scenario]).c_str(),
            held&&driftEstablished&&!acceptHandoff(after)&&g_msiNativeForegroundLease.active&&
            g_msiNativeForegroundLease.owned==donor&&journal.hiddenByUs==beforeReservations&&hidden==beforeHidden&&
            writes.size()==beforeWrites&&refreshCount()==beforeRefreshes&&noUnhide()&&noCarried());
    }

    // VC-WIN5-NATIVE-OWNED-SET-R3: production proof consumer only. The fixture
    // supplies the current recipient journal; durable persistence and interrupted
    // Apply are covered by their original consumers, not asserted by this header.
    auto nativeOwnedSnapshot=[&](std::vector<std::string>& owned,HidHideVisibilitySnapshot& baseline){
        std::lock_guard hostLock(g_inputHostMx);
        return msiNativeForegroundNativeHandoffSnapshot(owned,baseline); // Acquires its own lease lock.
    };
    auto sameProofBaseline=[](const HidHideVisibilitySnapshot& a,const HidHideVisibilitySnapshot& b){
        return a.valid==b.valid&&a.cloaking==b.cloaking&&a.hidden==b.hidden&&a.applications==b.applications&&
            a.cloakRaw==b.cloakRaw&&a.hiddenRaw==b.hiddenRaw&&a.applicationsRaw==b.applicationsRaw&&a.inverseRaw==b.inverseRaw;
    };
    auto proofSentinel=[](){
        HidHideVisibilitySnapshot value;value.valid=true;value.cloaking=false;
        value.hidden={L"HID\\OUTPUT-SENTINEL"};value.applications={L"C:\\OUTPUT-SENTINEL.exe"};
        value.cloakRaw="sentinel-cloak";value.hiddenRaw="sentinel-hidden";
        value.applicationsRaw="sentinel-applications";value.inverseRaw="sentinel-inverse";return value;
    };
    const std::vector<std::string> proofOwnedSentinel={"OUTPUT-SENTINEL-A","OUTPUT-SENTINEL-B"};
    for(const bool verifyingPhase:{false,true})for(const int terminal:{0,2}){
        configureFamily(false);familyLatePresent=true;hidden={handoffForeign};gameRefreshResult=1;enter();
        const auto ticket=admitHandoff();const bool held=holdHandoff();
        (void)journalReadback(ticket,false); // Recipient authority only; no reservation/persistence claim.
        pollResult=terminal;msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
        const bool phaseReady=!verifyingPhase||g_virtualStartProtection.beginVerification(ticket,"success",inputHostStartProtectionNow());
        const auto donor=g_msiNativeForegroundLease.owned;const auto original=g_msiNativeForegroundLease.before;
        std::vector<std::string> expected;for(const auto& path:donor)expected.push_back(W2U(path));
        const auto beforeHidden=hidden;const auto beforeWrites=writes.size(),beforeRefreshes=refreshCount();
        const auto beforeReads=reads,beforeDiscoveries=discoveries;
        auto owned=proofOwnedSentinel;auto baseline=proofSentinel();
        const bool proven=nativeOwnedSnapshot(owned,baseline);
        check("GPD terminal helper proof exports complete original owned set and baseline in pending/verifying R3",
            held&&phaseReady&&proven&&donor.size()==3&&owned==expected&&sameProofBaseline(baseline,original)&&
            original.valid&&std::none_of(donor.begin(),donor.end(),[&](const auto& path){return hidHideContainsPath(original.hidden,W2U(path));})&&
            !hidHideContainsPath(donor,W2U(handoffForeign))&&baseline.hidden==std::vector<std::wstring>{handoffForeign}&&
            !g_msiNativeForegroundCyclePending&&g_msiNativeForegroundLease.helperExitCode==DWORD(terminal==0?0:6));
        const bool repeated=nativeOwnedSnapshot(owned,baseline);
        check("GPD complete-set proof repeats without consuming donor or mutating visibility/journal",
            repeated&&owned==expected&&sameProofBaseline(baseline,original)&&g_msiNativeForegroundLease.active&&
            g_msiNativeForegroundLease.owned==donor&&msiNativeForegroundVirtualHandoffRequired(ticket)&&
            g_hidHideVisibilityJournal.hiddenByUs.empty()&&hidden==beforeHidden&&writes.size()==beforeWrites&&
            refreshCount()==beforeRefreshes&&reads==beforeReads&&discoveries==beforeDiscoveries&&noCarried()&&noUnhide());
    }

    const char* proofRejections[]={"pending original helper","stale bound ticket","missing bound ticket",
        "stale R3 request","recipient root drift","power drift","location drift","own path in original baseline",
        "duplicate owned path","case-alias duplicate owned path","more than sixteen owned paths","virtual owned target",
        "invalid original baseline","source-pending absent journal","source-pending empty source set",
        "nonunique physical source","unresolved physical source","expired original request","unheld native donor",
        "private configuration returned","changed original cloak","empty owned target","empty owned set",
        "terminal original request","disabled virtual intent","incomplete selected paths"};
    for(unsigned scenario=0;scenario<std::size(proofRejections);++scenario){
        configureFamily(false);familyLatePresent=true;hidden={handoffForeign};
        if(scenario==0)gameRefreshResult=1;
        enter();const auto ticket=admitHandoff();const bool held=holdHandoff();
        (void)journalReadback(ticket,false);
        auto& lease=g_msiNativeForegroundLease;auto& journal=g_hidHideVisibilityJournal;
        bool mutationReady=true;
        if(scenario==1)g_inputHostStartProtectionBoundTicket=ticket+1;
        if(scenario==2)g_inputHostStartProtectionBoundTicket=0;
        if(scenario==3){const auto replacement=g_virtualStartProtection.admit("elite",inputHostStartProtectionNow()+ymcc::VirtualStartProtection::maxMs);
            mutationReady=replacement.accepted&&replacement.requestId!=ticket;}
        if(scenario==4){journal.physical.baseContainerDeviceInstanceId+="-OTHER-ROOT";journal.physicalSources={journal.physical};}
        if(scenario==5)g_powerGeneration.fetch_add(1);
        if(scenario==6)g_msiForegroundTestIo.locations=[&](const std::string&){return std::vector<std::wstring>{location+L"#OTHER-PORT"};};
        if(scenario==7){mutationReady=!lease.owned.empty();if(mutationReady)lease.before.hidden.push_back(lease.owned.back());}
        if(scenario==8){mutationReady=!lease.owned.empty();if(mutationReady)lease.owned.push_back(lease.owned.front());}
        if(scenario==9){mutationReady=!lease.owned.empty();if(mutationReady)lease.owned.push_back(U2W(ascii_lower(W2U(lease.owned.front()))));}
        if(scenario==10){for(unsigned index=0;lease.owned.size()<17;++index)lease.owned.push_back(U2W(familyLateHid+"-OWNED-"+std::to_string(index)));}
        if(scenario==11)lease.owned.push_back(L"HID\\VIGEM\\VIRTUAL");
        if(scenario==12)lease.before.valid=false;
        if(scenario==13)journal=HidHideVisibilityJournal{};
        if(scenario==14)journal.physicalSources.clear();
        if(scenario==15)journal.physicalSources.push_back(journal.physical);
        if(scenario==16){journal.physical.resolved=false;journal.physicalSources={journal.physical};}
        if(scenario==17)mutationReady=g_virtualStartProtection.view(inputHostStartProtectionNow()+ymcc::VirtualStartProtection::maxMs).phase=="expired";
        if(scenario==18)lease.virtualHandoffHeld=false;
        if(scenario==19)lease.configurationReturned=true;
        if(scenario==20)lease.cloakChanged=true;
        if(scenario==21)lease.owned.push_back(L"");
        if(scenario==22)lease.owned.clear();
        if(scenario==23)mutationReady=g_virtualStartProtection.finish(ticket,"failure",inputHostStartProtectionNow());
        if(scenario==24)g_realStickTest.virtualTargetEnabled=false;
        if(scenario==25)lease.diagnosticSelectedPathsComplete=false;
        const auto donor=lease.owned;const auto original=lease.before;const auto beforeHidden=hidden;
        const auto beforeWrites=writes.size(),beforeRefreshes=refreshCount();const auto beforeReads=reads;
        auto owned=proofOwnedSentinel;auto baseline=proofSentinel();const auto sentinelBaseline=baseline;
        const bool proven=nativeOwnedSnapshot(owned,baseline);
        check((std::string("GPD complete-set proof refuses ")+proofRejections[scenario]+" without partial outputs").c_str(),
            held&&mutationReady&&!proven&&owned==proofOwnedSentinel&&sameProofBaseline(baseline,sentinelBaseline)&&
            lease.active&&lease.owned==donor&&sameProofBaseline(lease.before,original)&&
            journal.hiddenByUs.empty()&&hidden==beforeHidden&&writes.size()==beforeWrites&&refreshCount()==beforeRefreshes&&
            reads==beforeReads&&noCarried()&&noUnhide());
    }

    for(const bool rog:{true,false}){
        configureFamily(rog);
        if(!rog){ // Reuse family IO with the original MSI native identity and port-cycle leaf.
            g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";
            g_machineIdentity.product="MS-1T91";familyVendor=0x0db0;familyProduct=0x1901;
            familyRoot=root;familyHid="HID\\VID_0DB0&PID_1901&IG_01\\PHYSICAL";familyXusb=xusbId;
            xusbInventory.interfaces.front().instanceId=familyXusb;xusbInventory.interfaces.front().ancestorRoot=familyRoot;
        }
        enter();const auto ticket=admitHandoff();const bool refused=!holdHandoff();
        (void)journalReadback(ticket,false);const auto beforeHidden=hidden;const auto beforeWrites=writes.size(),beforeRefreshes=refreshCount();
        auto owned=proofOwnedSentinel;auto baseline=proofSentinel();const auto sentinelBaseline=baseline;
        const bool proven=nativeOwnedSnapshot(owned,baseline);
        const bool unchanged=!proven&&owned==proofOwnedSentinel&&sameProofBaseline(baseline,sentinelBaseline)&&
            hidden==beforeHidden&&writes.size()==beforeWrites&&refreshCount()==beforeRefreshes;
        g_hidHideVisibilityJournal=HidHideVisibilityJournal{};
        const bool originalReturn=msiNativeForegroundBeforeVirtualWork("persona-transition");
        check(rog?"ROG rejects GPD owned-set proof and keeps original game-node return":
            "MSI rejects GPD owned-set proof and keeps original port-cycle return",
            refused&&unchanged&&originalReturn&&!g_msiNativeForegroundLease.active&&hidden.empty()&&noCarried()&&
            (rog?(gameRefreshes.size()==2&&xusbRefreshes.size()==2&&cycles.empty()):
                (gameRefreshes.empty()&&xusbRefreshes.empty()&&cycles==std::vector<bool>({false,true}))));
    }
    // End VC-WIN5-NATIVE-OWNED-SET-R3 proof consumer cases.
    for(const bool expired:{false,true}){
        configureFamily(false);hidden={handoffForeign};enter();const auto ticket=admitHandoff();
        const bool held=holdHandoff();const auto donor=g_msiNativeForegroundLease.owned;
        const auto beforeWrites=writes.size(),beforeRefreshes=refreshCount();
        finishHandoff(ticket+1,false);finishHandoff(ticket,true);
        check("GPD stale Start completion and original continuation retain donor without mutation",
            held&&g_msiNativeForegroundLease.active&&g_msiNativeForegroundLease.owned==donor&&
            msiNativeForegroundVirtualHandoffRequired(ticket)&&writes.size()==beforeWrites&&
            refreshCount()==beforeRefreshes&&noCarried()&&!g_hidHideVisibilityJournal.active);
        if(expired){
            const auto clock=inputHostStartProtectionNow();
            const auto deadline=clock+g_virtualStartProtection.view(clock).remainingMs;
            const auto ended=g_virtualStartProtection.view(deadline);
            check("GPD original 30-second R3 deadline expires without admitting a replacement",
                ended.requestId==ticket&&ended.phase=="expired"&&!ended.locked);
        }
        finishHandoff(ticket,expired); // An expired continuation cannot keep the donor.
        check(expired?"GPD expired Start returns native donor through original restore":
            "GPD early failed Start returns native donor through original restore",
            !g_msiNativeForegroundLease.active&&!msiNativeForegroundVirtualHandoffRequired(ticket)&&
            hidden==std::vector<std::wstring>{handoffForeign}&&writes.size()==beforeWrites+donor.size()&&
            std::all_of(writes.begin()+beforeWrites,writes.end(),[&](const auto& write){return !write.second&&hidHideContainsPath(donor,W2U(write.first));})&&
            gameRefreshes.size()==2&&xusbRefreshes.size()==2&&cycles.empty()&&noCarried()&&!g_hidHideVisibilityJournal.active);
        const auto returnedWrites=writes.size(),returnedRefreshes=refreshCount();finishHandoff(ticket,false);
        check("GPD completed Start cleanup never restores donor twice",
            writes.size()==returnedWrites&&refreshCount()==returnedRefreshes&&hidden==std::vector<std::wstring>{handoffForeign});
    }

    for(const bool powerDrift:{true,false}){
        configureFamily(false);hidden={handoffForeign};enter();const auto ticket=admitHandoff();
        const bool held=holdHandoff();const auto donor=g_msiNativeForegroundLease.owned;
        const auto beforeWrites=writes.size();bool drifted=false;
        if(powerDrift){g_powerGeneration.fetch_add(1);drifted=true;}
        else {const auto replacement=g_virtualStartProtection.admit("elite",inputHostStartProtectionNow()+ymcc::VirtualStartProtection::maxMs);
            drifted=replacement.accepted&&replacement.requestId!=ticket;}
        finishHandoff(ticket,true);
        check(powerDrift?"GPD continuation cannot retain donor across power drift":
            "GPD continuation cannot retain donor across request drift",
            held&&drifted&&!g_msiNativeForegroundLease.active&&msiNativeForegroundVirtualHandoffTicket()==0&&
            hidden==std::vector<std::wstring>{handoffForeign}&&writes.size()==beforeWrites+donor.size()&&
            std::all_of(writes.begin()+beforeWrites,writes.end(),[&](const auto& write){return !write.second&&hidHideContainsPath(donor,W2U(write.first));})&&
            !g_hidHideVisibilityJournal.active&&noCarried()&&cycles.empty());
    }

    // Replay the real Start's earliest persona guard, before payload/observer/
    // Host IO. Its outer NativeHandoffReturn must run even without RequestOutcome.
    for(const char* rejectedPersona:{"disabled","xbox360"}){
        configureFamily(false);hidden={handoffForeign};enter();const auto ticket=admitHandoff();
        const bool held=holdHandoff();const auto donor=g_msiNativeForegroundLease.owned;
        const auto beforeWrites=writes.size();const auto beforeEpoch=g_inputHostEpoch;
        const bool beforeHello=g_inputHostHello,beforePrepared=g_inputHostPrepared,beforeNeutral=g_inputHostNeutralized;
        const auto beforeObserverStarts=observerStarts;const auto beforeDeadline=g_inputHostSwitchRecoveryStartDeadline;
        const auto captured=msiNativeForegroundVirtualHandoffTicket();
        g_inputHostSwitchRecoveryStartDeadline=0;g_realStickTest.virtualTargetPersona=rejectedPersona;
        // Caller holds neither Host nor native mutex: exercise the production RAII lock order.
        const bool started=inputHostStart();
        g_inputHostSwitchRecoveryStartDeadline=beforeDeadline;
        check((std::string("GPD actual inputHostStart early rejection returns donor for ")+rejectedPersona).c_str(),
            held&&captured==ticket&&!started&&!g_msiNativeForegroundLease.active&&
            msiNativeForegroundVirtualHandoffTicket()==0&&hidden==std::vector<std::wstring>{handoffForeign}&&
            writes.size()==beforeWrites+donor.size()&&
            std::all_of(writes.begin()+beforeWrites,writes.end(),[&](const auto& write){return !write.second&&hidHideContainsPath(donor,W2U(write.first));})&&
            !g_inputHostProcess&&g_inputHostEpoch==beforeEpoch&&g_inputHostHello==beforeHello&&
            g_inputHostPrepared==beforePrepared&&g_inputHostNeutralized==beforeNeutral&&observerStarts==beforeObserverStarts&&
            g_inputHostStartProtectionBoundTicket==0&&!g_hidHideVisibilityJournal.active&&noCarried()&&
            gameRefreshes.size()==2&&xusbRefreshes.size()==2&&cycles.empty()&&virtualJobs.empty());
    }

    configureFamily(false);hidden={handoffForeign};gameRefreshResult=1;enter();
    const auto unfinishedTicket=admitHandoff();const bool unfinishedHeld=holdHandoff();finishHandoff(unfinishedTicket,false);
    check("GPD early Start failure returns config while original helper remains pending",
        unfinishedHeld&&hidden==std::vector<std::wstring>{handoffForeign}&&g_msiNativeForegroundLease.active&&
        g_msiNativeForegroundLease.configurationReturned&&g_msiNativeForegroundCyclePending&&
        gameRefreshes.size()==1&&xusbRefreshes.empty()&&cycles.empty()&&noCarried());
    gameRefreshResult=0;pollResult=0;msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
    check("GPD original helper terminal allows failed-Start restore to finish without new root cycle",
        !g_msiNativeForegroundLease.active&&!g_msiNativeForegroundCyclePending&&
        hidden==std::vector<std::wstring>{handoffForeign}&&xusbRefreshes.size()==1&&cycles.empty()&&noCarried());

    // R11: invoke the existing production owner after an actual configuration
    // return, with the inverse leaf deliberately left pending. The synthetic IO
    // boundary is not proof of a real child/Job retirement or hardware delivery.
    auto admitReturnedVirtual=[&](){
        g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="steamdeck";
        return g_virtualStartProtection.admit("steamdeck",inputHostStartProtectionNow()).requestId;
    };
    auto seedReturnedGameFacts=[&](unsigned terminal){
        auto& lease=g_msiNativeForegroundLease;
        lease.helperDiagnostic=nullptr;
        if(terminal==1 || terminal==2)return; // acknowledged success / unknown failure
        json facts={{"Schema",1},{"Kind","ymcc.gameRefresh.failure"},{"Result",6},
            {"Stage",terminal==3?"enable":"disable"},{"Xusb",lease.helperXusbTarget},
            {"PrepareLocateResult",0U},{"CurrentLocateResult",0U},{"SameNode",true},
            {"DisableCallEntered",true},{"DisableResult",terminal==3?0U:23U},
            {"EnableCallEntered",terminal==3},{"EnableResult",terminal==3?json(23U):json(nullptr)},
            {"CompensationChecked",terminal==3},{"CompensationIdentityMatched",terminal==3?json(true):json(nullptr)},
            {"CompensationLocateResult",terminal==3?json(0U):json(nullptr)},
            {"CompensationSameNode",terminal==3?json(true):json(nullptr)},
            {"CompensationAttempted",terminal==3},{"CompensationEnableResult",terminal==3?json(23U):json(nullptr)},
            {"CompensationExceptionType",nullptr},{"CompensationExceptionHResult",nullptr},
            {"ExceptionType","System.InvalidOperationException"},{"ExceptionHResult",-2146233079}};
        lease.helperDiagnostic={{"transport","delete-on-close-stdout"},{"state","record-accepted"},
            {"scope","helper-self-reported-existing-call-facts"},{"hardwareSuccessProven",false},
            {"reportedFacts",std::move(facts)}};
    };
    for(const bool originalXusb:{false,true})for(const unsigned terminal:{0U,1U}){
        configureFamily(false);hidden={handoffForeign};cloak=false;
        gameRefreshResult=originalXusb?0:1;familyXusbResult=originalXusb?1:0;enter();
        const auto donor=g_msiNativeForegroundLease.owned;
        const auto priorGames=gameRefreshes.size(),priorXusb=xusbRefreshes.size();
        const auto ticket=admitReturnedVirtual();
        const auto admitted=g_virtualStartProtection.view(inputHostStartProtectionNow());
        const bool deferred=!msiNativeForegroundBeforeVirtualWork("initial-target-admission");
        check("GPD current virtual returns exact config but still waits for original helper",
            ticket&&deferred&&g_msiNativeForegroundLease.active&&g_msiNativeForegroundLease.configurationReturned&&
            g_msiNativeForegroundCyclePending&&hidden==std::vector<std::wstring>{handoffForeign}&&!cloak&&
            gameRefreshes.size()==priorGames&&xusbRefreshes.size()==priorXusb&&cycles.empty()&&virtualJobs.empty());
        for(unsigned repeat=0;repeat<3;++repeat)(void)msiNativeForegroundBeforeVirtualWork("initial-target-admission");
        check("GPD live original writer keeps one parked work and cannot create an inverse leaf",
            g_msiNativeForegroundCyclePending&&g_msiNativeForegroundLease.deferredVirtualWork==
                std::vector<std::string>{"initial-target-admission"}&&gameRefreshes.size()==priorGames&&
            xusbRefreshes.size()==priorXusb&&virtualJobs.empty());
        gameRefreshResult=1;familyXusbResult=1;pollResult=terminal==1?0:2;
        msiNativeForegroundMaintenance();seedReturnedGameFacts(terminal);
        check("GPD terminal IO boundary retains current config duty before original work resumes",
            !g_msiNativeForegroundCyclePending&&!g_msiNativeForegroundLease.syntheticCycle&&
            !g_msiNativeForegroundLease.cycleProcess&&!g_msiNativeForegroundLease.nativeRefreshWriter&&
            g_msiNativeForegroundLease.active&&g_msiNativeForegroundLease.configurationReturned&&
            g_msiNativeForegroundLease.helperGameTarget==(originalXusb?familyXusb:familyHid)&&
            g_msiNativeForegroundLease.helperExitCode==(terminal==1?0U:6U));
        msiNativeForegroundHandleWork();
        check("GPD current virtual resumes after returned terminal writer without any new inverse HID or XUSB",
            !g_msiNativeForegroundLease.active&&!g_msiNativeForegroundCyclePending&&
            gameRefreshes.size()==priorGames&&xusbRefreshes.size()==priorXusb&&cycles.empty());
        const auto resumed=g_virtualStartProtection.view(inputHostStartProtectionNow());
        check("GPD returned handoff queues only original work without a new R3 ticket or fallback budget",
            virtualJobs==std::vector<std::string>{"initial-target-admission"}&&resumed.requestId==ticket&&
            resumed.persona=="steamdeck"&&resumed.phase=="pending"&&resumed.remainingMs<=admitted.remainingMs&&
            resumed.fallbackAttempts==admitted.fallbackAttempts);
        check("GPD returned handoff preserves third party and reports no synthetic target ownership",
            hidden==std::vector<std::wstring>{handoffForeign}&&!cloak&&!g_inputHostProcess&&
            !g_hidHideVisibilityJournal.active&&noCarried()&&
            std::all_of(writes.end()-donor.size(),writes.end(),[&](const auto& write){
                return !write.second&&hidHideContainsPath(donor,W2U(write.first));}));
        msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
        check("GPD late native callback cannot repeat returned handoff or create an inverse writer",
            virtualJobs==std::vector<std::string>{"initial-target-admission"}&&
            gameRefreshes.size()==priorGames&&xusbRefreshes.size()==priorXusb&&!g_msiNativeForegroundCyclePending);
    }
    for(const unsigned terminal:{2U,3U}){
        configureFamily(false);hidden={handoffForeign};cloak=false;
        gameRefreshResult=0;familyXusbResult=1;enter();
        const auto priorGames=gameRefreshes.size(),priorXusb=xusbRefreshes.size();
        const auto ticket=admitReturnedVirtual();
        const bool deferred=!msiNativeForegroundBeforeVirtualWork("initial-target-admission");
        check("GPD unknown or unproven Enable case first returns config under the same live helper",
            ticket&&deferred&&g_msiNativeForegroundLease.configurationReturned&&
            g_msiNativeForegroundCyclePending&&hidden==std::vector<std::wstring>{handoffForeign});
        gameRefreshResult=1;familyXusbResult=1;pollResult=2;
        msiNativeForegroundMaintenance();seedReturnedGameFacts(terminal);msiNativeForegroundHandleWork();
        check(terminal==2?"GPD missing terminal facts cannot masquerade as no Disable responsibility":
            "GPD acknowledged Disable with unproven Enable retains original return owner",
            g_msiNativeForegroundLease.active&&g_msiNativeForegroundCyclePending&&virtualJobs.empty()&&
            gameRefreshes.size()==priorGames+1&&xusbRefreshes.size()==priorXusb&&
            g_msiNativeForegroundLease.configurationReturned&&!g_inputHostProcess&&!g_hidHideVisibilityJournal.active);
        (void)msiNativeForegroundBeforeVirtualWork("initial-target-admission");
        check("GPD unproven return never replaces its pending inverse helper or renews the virtual ticket",
            gameRefreshes.size()==priorGames+1&&xusbRefreshes.size()==priorXusb&&virtualJobs.empty()&&
            g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId==ticket&&
            hidden==std::vector<std::wstring>{handoffForeign}&&cycles.empty());
    }
    for(const bool expired:{false,true}){
        configureFamily(false);hidden={handoffForeign};cloak=false;gameRefreshResult=1;enter();
        const auto ticket=admitReturnedVirtual();
        (void)msiNativeForegroundBeforeVirtualWork("initial-target-admission");
        const auto clock=inputHostStartProtectionNow();
        if(expired){const auto view=g_virtualStartProtection.view(clock);
            (void)g_virtualStartProtection.view(clock+view.remainingMs);}
        else (void)g_virtualStartProtection.finish(ticket,"failure",clock);
        const auto retired=g_virtualStartProtection.view(inputHostStartProtectionNow());
        check("GPD returned virtual shortcut cannot borrow an expired or failed R3 intent",
            retired.requestId==ticket&&retired.phase==(expired?"expired":"failure")&&
            !g_virtualStartProtection.canContinueExecution(ticket,"steamdeck",inputHostStartProtectionNow()));
        gameRefreshResult=1;familyXusbResult=1;pollResult=2;
        msiNativeForegroundMaintenance();seedReturnedGameFacts(0);msiNativeForegroundHandleWork();
        check("GPD retired virtual request retains native return instead of replaying stale Start work",
            g_msiNativeForegroundLease.active&&g_msiNativeForegroundCyclePending&&virtualJobs.empty()&&
            gameRefreshes.size()==2&&xusbRefreshes.empty()&&g_msiNativeForegroundLease.configurationReturned);
        const auto after=g_virtualStartProtection.view(inputHostStartProtectionNow());
        check("GPD native return does not rewrite expired or failed outcome or consume fallback",
            after.requestId==ticket&&after.phase==(expired?"expired":"failure")&&after.fallbackAttempts==0&&
            hidden==std::vector<std::wstring>{handoffForeign}&&!g_inputHostProcess&&!g_hidHideVisibilityJournal.active);
    }
    configureFamily(false);hidden={handoffForeign};cloak=false;gameRefreshResult=1;enter();leave();
    check("GPD OFF configuration returns while its original native helper remains pending",
        !g_realStickTest.virtualTargetEnabled&&inputHostConfiguredPersona()=="disabled"&&
        g_msiNativeForegroundLease.configurationReturned&&g_msiNativeForegroundCyclePending&&
        hidden==std::vector<std::wstring>{handoffForeign}&&gameRefreshes.size()==1&&xusbRefreshes.empty());
    auto offWriterVerificationBusy=[&](){std::lock_guard hostLock(g_inputHostMx);
        return ymcc::inputHost::switchVerificationRuntime::busyLocked();};
    check("GPD OFF verification waits while the original native helper can still write",offWriterVerificationBusy());
    gameRefreshResult=1;familyXusbResult=1;pollResult=2;
    msiNativeForegroundMaintenance();seedReturnedGameFacts(0);msiNativeForegroundHandleWork();
    check("GPD OFF retains its inverse native return even for known failed Disable",
        g_msiNativeForegroundLease.active&&g_msiNativeForegroundCyclePending&&
        gameRefreshes.size()==2&&xusbRefreshes.empty()&&virtualJobs.empty()&&cycles.empty());
    check("GPD OFF verification still waits for inverse native return writer",offWriterVerificationBusy());
    gameRefreshResult=0;familyXusbResult=0;pollResult=0;
    msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
    check("GPD OFF completes original native leaf return without a virtual target or third party mutation",
        !g_msiNativeForegroundLease.active&&!g_msiNativeForegroundCyclePending&&
        gameRefreshes.size()==2&&xusbRefreshes.size()==1&&virtualJobs.empty()&&
        hidden==std::vector<std::wstring>{handoffForeign}&&!g_inputHostProcess&&!g_hidHideVisibilityJournal.active&&noCarried());

    check("GPD OFF verification releases its busy fence after the same helper retires",!offWriterVerificationBusy());
    // The new fence observes OFF writer lifetime only; it must not become a
    // new start gate or extend native mutation authority to other families.
    g_msiNativeForegroundCyclePending=true;g_realStickTest.virtualTargetPersona="steamdeck";
    check("GPD enabled verification retains its prior non-OFF busy policy",!offWriterVerificationBusy());
    g_realStickTest.virtualTargetPersona="disabled";g_machineIdentity.family=YmccFamily::OneXPlayer;
    check("OneX OFF does not inherit GPD native-writer verification scope",!offWriterVerificationBusy());
    g_machineIdentity.family=YmccFamily::Ayanero;
    check("AYA OFF does not inherit GPD native-writer verification scope",!offWriterVerificationBusy());
    g_msiNativeForegroundCyclePending=false;

    // R11 legacy fallback: exercise the original production entry, not a
    // duplicate of its terminal predicate. All process/device IO remains inert.
    auto returnedLegacyFallback=[&](){
        std::lock_guard hostLock(g_inputHostMx);std::lock_guard leaseLock(g_msiNativeForegroundMx);
        return msiNativeForegroundVirtualHandoffLocked();
    };
    for(const unsigned terminal:{2U,3U}){
        configureFamily(false);hidden={handoffForeign};cloak=false;gameRefreshResult=1;enter();
        const auto ticket=admitReturnedVirtual();
        const bool deferred=!msiNativeForegroundBeforeVirtualWork("initial-target-admission");
        pollResult=2;msiNativeForegroundMaintenance();seedReturnedGameFacts(terminal);
        auto& lease=g_msiNativeForegroundLease;lease.refreshFailed=true;
        const auto owned=lease.owned;const auto facts=lease.helperDiagnostic;
        const auto parked=lease.deferredVirtualWork;const auto beforeWrites=writes.size(),beforeRefreshes=refreshCount();
        check("GPD legacy fallback fixture retains returned config after its original terminal helper",
            ticket&&deferred&&lease.active&&lease.configurationReturned&&lease.refreshFailed&&
            !g_msiNativeForegroundCyclePending&&!lease.cycleProcess&&!lease.nativeRefreshWriter&&
            lease.helperExitCode==6&&hidden==std::vector<std::wstring>{handoffForeign});
        const bool admitted=returnedLegacyFallback();
        check(terminal==2?"GPD legacy fallback cannot release responsibility from unknown terminal facts":
            "GPD legacy fallback cannot release acknowledged Disable with unproven Enable",
            !admitted&&lease.active&&lease.configurationReturned&&lease.refreshFailed&&
            !g_msiNativeForegroundCyclePending);
        check("GPD rejected legacy fallback retains exact duty and evidence without replay or device work",
            lease.owned==owned&&lease.helperDiagnostic==facts&&lease.deferredVirtualWork==parked&&
            hidden==std::vector<std::wstring>{handoffForeign}&&virtualJobs.empty()&&
            writes.size()==beforeWrites&&refreshCount()==beforeRefreshes&&!g_inputHostProcess&&
            !g_hidHideVisibilityJournal.active&&noCarried());
        const auto after=g_virtualStartProtection.view(inputHostStartProtectionNow());
        check("GPD rejected legacy fallback keeps original virtual ticket and fallback budget",
            after.requestId==ticket&&after.persona=="steamdeck"&&after.phase=="pending"&&after.fallbackAttempts==0);
    }
    configureFamily(false);hidden={handoffForeign};cloak=false;gameRefreshResult=1;enter();
    const auto retainedTicket=admitReturnedVirtual();
    const bool retainedDeferred=!msiNativeForegroundBeforeVirtualWork("initial-target-admission");
    pollResult=2;msiNativeForegroundMaintenance();seedReturnedGameFacts(0);
    g_msiNativeForegroundLease.refreshFailed=true;
    const auto retainedOwned=g_msiNativeForegroundLease.owned;
    const auto retainedFacts=g_msiNativeForegroundLease.helperDiagnostic;
    const auto retainedWork=g_msiNativeForegroundLease.deferredVirtualWork;
    const auto retainedWrites=writes.size(),retainedRefreshes=refreshCount();
    const HANDLE savedCycleProcess=g_msiNativeForegroundLease.cycleProcess;
    check("GPD legacy retained-handle probe starts from known failed Disable with no Enable acknowledgement",
        retainedTicket&&retainedDeferred&&g_msiNativeForegroundLease.active&&
        g_msiNativeForegroundLease.configurationReturned&&!g_msiNativeForegroundCyclePending&&
        !savedCycleProcess&&!g_msiNativeForegroundLease.nativeRefreshWriter&&
        retainedFacts.value("state",std::string{})=="record-accepted");
    bool retainedAdmitted=false;
    {
        std::lock_guard hostLock(g_inputHostMx);std::lock_guard leaseLock(g_msiNativeForegroundMx);
        // Boolean occupancy probe only. Never pass this placeholder to a poll,
        // CloseHandle or any OS operation; restore it before leaving this scope.
        struct RestoreCycleProcess {
            HANDLE saved;
            ~RestoreCycleProcess(){g_msiNativeForegroundLease.cycleProcess=saved;}
        } restore{savedCycleProcess};
        g_msiNativeForegroundLease.cycleProcess=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(1));
        retainedAdmitted=msiNativeForegroundVirtualHandoffLocked();
    }
    check("GPD legacy fallback cannot report admission while retained cycle process prevents clear",
        !retainedAdmitted&&g_msiNativeForegroundLease.active&&
        g_msiNativeForegroundLease.configurationReturned&&!g_msiNativeForegroundCyclePending&&
        g_msiNativeForegroundLease.cycleProcess==savedCycleProcess);
    check("GPD retained-handle boolean probe preserves original duty and never dispatches work",
        g_msiNativeForegroundLease.owned==retainedOwned&&g_msiNativeForegroundLease.helperDiagnostic==retainedFacts&&
        g_msiNativeForegroundLease.deferredVirtualWork==retainedWork&&virtualJobs.empty()&&
        hidden==std::vector<std::wstring>{handoffForeign}&&writes.size()==retainedWrites&&
        refreshCount()==retainedRefreshes&&!g_inputHostProcess&&!g_hidHideVisibilityJournal.active&&noCarried());
    const bool clearedAfterProbe=returnedLegacyFallback();
    check("GPD legacy fallback admits known terminal return only after retained process occupancy is gone",
        clearedAfterProbe&&!g_msiNativeForegroundLease.active&&!g_msiNativeForegroundCyclePending&&
        virtualJobs==std::vector<std::string>{"initial-target-admission"}&&
        hidden==std::vector<std::wstring>{handoffForeign}&&writes.size()==retainedWrites&&
        refreshCount()==retainedRefreshes&&g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId==retainedTicket);

    for(const bool helperPending:{false,true})for(const std::string faultReason:{
        "frame-receipt-fault","fault-stop:frame-receipt-fault:offline","fault-stop:partial-start-exception"}){
        configureFamily(false);hidden={handoffForeign};gameRefreshResult=helperPending?1:0;enter();
        const auto ticket=admitHandoff();
        (void)msiNativeForegroundBeforeVirtualWork("persona-transition");
        (void)msiNativeForegroundBeforeVirtualWork("settings-changed");
        const bool held=msiNativeForegroundVirtualHandoffRequired(ticket);
        const auto donor=g_msiNativeForegroundLease.owned;
        const auto beforeFaultWrites=writes.size();
        const auto beforeFaultRefreshes=refreshCount();
        const bool faultAdmitted=msiNativeForegroundBeforeVirtualWork(faultReason);
        check("GPD mandatory fault Stop returns exact hidden donor before any virtual Start",
            held&&ticket&&hidden==std::vector<std::wstring>{handoffForeign}&&
            (!g_msiNativeForegroundLease.active||g_msiNativeForegroundLease.configurationReturned)&&
            writes.size()==beforeFaultWrites+donor.size()&&
            std::all_of(writes.begin()+beforeFaultWrites,writes.end(),[&](const auto& write){
                return !write.second&&hidHideContainsPath(donor,W2U(write.first));})&&
            !g_inputHostProcess&&!g_hidHideVisibilityJournal.active&&noCarried()&&cycles.empty());
        check("GPD fault Stop keeps only the original live helper and drops parked Start work",
            g_msiNativeForegroundCyclePending==helperPending&&
            (helperPending?!faultAdmitted:faultAdmitted)&&
            std::none_of(g_msiNativeForegroundLease.deferredVirtualWork.begin(),
                g_msiNativeForegroundLease.deferredVirtualWork.end(),[](const auto& reason){
                    return reason=="persona-transition"||reason=="settings-changed";})&&
            std::none_of(virtualJobs.begin(),virtualJobs.end(),[](const auto& reason){
                    return reason=="persona-transition"||reason=="settings-changed";})&&
            (!helperPending||refreshCount()==beforeFaultRefreshes)&&cycles.empty());
        if(helperPending){
            gameRefreshResult=0;pollResult=0;
            msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
            (void)msiNativeForegroundBeforeVirtualWork(faultReason);
        }
        msiNativeForegroundHandleWork();msiNativeForegroundHandleWork();
        check("GPD late native callback after mandatory fault cannot resurrect parked virtual Start",
            !g_msiNativeForegroundLease.active&&!g_msiNativeForegroundCyclePending&&
            msiNativeForegroundVirtualHandoffTicket()==0&&hidden==std::vector<std::wstring>{handoffForeign}&&
            writes.size()==beforeFaultWrites+donor.size()&&
            std::none_of(virtualJobs.begin(),virtualJobs.end(),[](const auto& reason){
                    return reason=="persona-transition"||reason=="settings-changed";})&&
            !g_inputHostProcess&&!g_hidHideVisibilityJournal.active&&noCarried()&&cycles.empty());
    }

    configureFamily(true);enter();const auto rogTicket=admitHandoff();
    check("ROG valid R3 ticket retains original native return protocol",
        !holdHandoff()&&!msiNativeForegroundVirtualHandoffRequired(rogTicket)&&
        msiNativeForegroundBeforeVirtualWork("persona-transition")&&hidden.empty()&&
        !g_msiNativeForegroundLease.active&&gameRefreshes.size()==2&&xusbRefreshes.size()==2&&cycles.empty());
    for(const auto family:{YmccFamily::OneXPlayer,YmccFamily::Ayanero}){
        configureFamily(false);g_machineIdentity.family=family;
        g_machineIdentity.manufacturer=family==YmccFamily::OneXPlayer?"ONE-NETBOOK TECHNOLOGY CO., LTD.":"AYANEO";
        g_machineIdentity.product=family==YmccFamily::OneXPlayer?"ONEXPLAYER":"AYANEO 2";
        const auto ticket=admitHandoff();enter();const bool refused=!holdHandoff();
        const bool allowed=msiNativeForegroundBeforeVirtualWork("persona-transition");
        HidHideVisibilitySnapshot after;after.valid=true;
        const bool unchanged=acceptHandoff(after);finishHandoff(ticket,false);
        msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();msiNativeForegroundExitCleanup();
        check(family==YmccFamily::OneXPlayer?"OneX native family remains Unsupported with zero new mutation":
            "AYA native family remains Unsupported with zero new mutation",
            nativeForegroundProfile()==Profile::Unsupported&&refused&&allowed&&unchanged&&
            !msiNativeForegroundVirtualHandoffRequired(ticket)&&!g_msiNativeForegroundLease.active&&
            !g_hidHideVisibilityJournal.active&&noCarried()&&discoveries==0&&inventories==0&&reads==0&&
            writes.empty()&&cloakWrites==0&&refreshCount()==0&&jobs.empty()&&virtualJobs.empty());
    }
    // Keep shared-node deduplication on an explicitly modeled, unaffected
    // PID1ABE route. The ROGX field identity below must never authorize stop/start.
    configureFamily(true);familyXusb=familyHid;xusbInventory.interfaces.front().instanceId=familyXusb;
    enter();
    check("modeled ROG PID1ABE shared devnode refreshes exactly once through XUSB",hidden.size()==1&&gameRefreshes.empty()&&xusbRefreshes.size()==1&&cycles.empty());
    leave();
    check("modeled ROG PID1ABE shared-node return remains serial",hidden.empty()&&!g_msiNativeForegroundLease.active&&xusbRefreshes.size()==2);
    auto configureFieldRogX=[&](){
        configureFamily(true);familyProduct=0x1b4c;
        familyRoot="USB\\VID_0B05&PID_1B4C\\6&A61A827&0&4";
        familyHid="HID\\VID_0B05&PID_1B4C&MI_05&IG_00\\8&2BE06C40&0&0000";
        familyLateHid="HID\\VID_0B05&PID_1B4C&MI_05&IG_03\\RETURNED";
        familyXusb=familyHid;g_machineIdentity.product="RC73XA";
        xusbInventory.interfaces.front().instanceId=familyXusb;xusbInventory.interfaces.front().ancestorRoot=familyRoot;
    };
    configureFieldRogX();
    check("field ROGX active HID XUSB identity is recognized without mutation authority",ymcc::nativeForeground::xusbTargetAdmitted(Profile::Rog,familyRoot,xusbInventory.interfaces.front()));
    check("field ROGX HID mutation policy is quarantined independently of recognition",!ymcc::nativeForeground::gameNodeRefreshMutationAllowed(Profile::Rog,familyRoot,familyHid));
    enter();
    check("field ROGX native shielding never hides or refreshes quarantined HID game node",writes.empty()&&hidden.empty()&&!g_msiNativeForegroundLease.active&&gameRefreshes.empty()&&xusbRefreshes.empty()&&cycles.empty());
    const auto blocked=familySource();
    check("field ROGX direct refresh leaf also refuses known unsafe mutation",msiNativeForegroundCycleLocked(false,&blocked,true)==2&&gameRefreshes.empty()&&xusbRefreshes.empty()&&cycles.empty());
    leave();
    check("field ROGX deferred return cannot invent success or issue a second refresh",hidden.empty()&&!g_msiNativeForegroundLease.active&&gameRefreshes.empty()&&xusbRefreshes.empty()&&cycles.empty());
    configureFieldRogX();g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="elite";
    HidHideVisibilitySnapshot fieldVirtualBefore;fieldVirtualBefore.valid=true;fieldVirtualBefore.cloaking=true;fieldVirtualBefore.inverseRaw="off";
    const auto fieldVirtualTargets=hidHideVirtualPhysicalTargets(familySource(),fieldVirtualBefore,1,"ROGX-quarantine-regression");
    check("field ROGX virtual HidHide planning retains recognized source without physical stop/start",std::find(fieldVirtualTargets.begin(),fieldVirtualTargets.end(),familyHid)!=fieldVirtualTargets.end()&&writes.empty()&&gameRefreshes.empty()&&xusbRefreshes.empty()&&cycles.empty());
    for(unsigned rejection=0;rejection<6;++rejection){
        configureFieldRogX();
        auto& endpoint=xusbInventory.interfaces.front();
        if(rejection==0)endpoint.activeInterface=false;
        if(rejection==1)endpoint.ancestorMatched=false;
        if(rejection==2)endpoint.ancestorRoot+="-OTHER-PORT";
        if(rejection==3)endpoint.virtualDevice=true;
        if(rejection==4)endpoint.instanceId="HID\\VID_045E&PID_028E&IG_00\\FOREIGN";
        if(rejection==5)endpoint.instanceId+="\\EXTRA";
        enter();
        check("field ROGX HID syntax cannot bypass class, parent, VID/PID or virtual gates",writes.empty()&&gameRefreshes.empty()&&xusbRefreshes.empty()&&cycles.empty());
    }
    // Same production step/await/Stop; the already-applied journal is modeled at
    // its existing visibility I/O seam. No full Start, real Host or SDK process.
    {
        const auto savedEpoch=g_inputHostEpoch,savedDeadline=g_inputHostSwitchRecoveryStartDeadline;
        const auto savedRun=g_inputHostRunId;
        const bool savedGate=g_hardwareWriteGate.load(),savedCaptureStop=g_inputCaptureStop.load();
        const bool savedRevoked=g_inputHostPowerAdmissionRevoked.load();
        const auto savedTopology=g_xinputTopologyEpoch.load(),savedIdentity=g_xinputSlotIdentityEpoch.load();
        const auto savedReprobe=g_xinputSlotReprobeMask.load();
        const auto savedScanStart=g_xinputSlotFullScanEpochStart.load(),savedBoundaryEpoch=g_xinputBoundaryScanEpoch.load();
        const bool savedDirty=g_xinputDiscoveryDirty.load(),savedBoundary=g_xinputPreTargetBoundaryScanned.load(),savedScan=g_xinputSlotFullScanOnce.load();
        uint64_t postSequence=0;unsigned sourceFault=10,sourceReads=0,readmitCalls=0;int readmitResult=0;
        auto readmitIo=[&](){++readmitCalls;return readmitResult;};
        g_msiNativeForegroundTestIo.postHideReadmit=readmitIo;
        g_msiNativeForegroundTestIo.postHideSource=[&](){
            using namespace ymcc::input::recoveryHealth;
            Source value;value.sequence=++postSequence;value.started=value.completed=GetTickCount64();
            value.power=currentPowerGeneration();value.topology=g_xinputTopologyEpoch.load();
            value.identity=g_xinputSlotIdentityEpoch.load();value.live=value.stable=true;
            value.route=Route::XInput;value.incarnation=value.continuity=1;++sourceReads;
            const auto& lease=g_msiNativeForegroundLease;
            if(sourceFault==0)value.sequence=lease.postHideBaselineSequence;
            if(sourceFault==1)value.started=lease.postHideTerminalTick?lease.postHideTerminalTick-1:0;
            if(sourceFault==2)++value.power;
            if(sourceFault==3)++value.identity;
            if(sourceFault==4)++value.topology;
            if(sourceFault==5)value.live=false;
            if(sourceFault==6)value.stable=false;
            if(sourceFault==7)value.route=Route::Unspecified;
            if(sourceFault==8)value.incarnation=0;
            if(sourceFault==9)value.continuity=0;
            if(sourceFault==11)value.started=lease.postHideSourceAfterTick?lease.postHideSourceAfterTick-1:0;
            return value; // Source has no control-value/packet-change requirement.
        };
        g_msiNativeForegroundTestIo.postHideRootStarted=[&](const std::string& candidate){return rootPresent&&candidate==familyRoot;};
        auto postSetup=[&](){
            configureFamily(false);g_hardwareWriteGate=true;g_inputCaptureStop=false;g_inputHostPowerAdmissionRevoked=false;
            g_inputHostPrepared=g_inputHostPrepareMayHaveReachedHost=g_inputHostActive=false;
            g_inputHostStartBlocked=false;g_inputHostFaultHoldsStart=false;
            g_inputHostEpoch=51;g_inputHostRunId="offline-post-hide-original-R3";g_inputHostSwitchRecoveryStartDeadline=0;
            postSequence=sourceReads=readmitCalls=0;sourceFault=10;familyXusbResult=1;readmitResult=0;
            const auto ticket=admitHandoff();g_inputHostStartProtectionBoundTicket=ticket;
            auto& journal=g_hidHideVisibilityJournal;journal=HidHideVisibilityJournal{};
            journal.active=true;journal.transactionId="offline-already-applied-post-hide";
            journal.physical=familySource();journal.physicalSources={journal.physical};
            journal.before.valid=true;journal.before.cloaking=cloak;journal.before.inverseRaw="off";
            journal.before.applications=apps;journal.before.hidden={handoffForeign};
            journal.hiddenByUs={U2W(familyRoot),U2W(familyHid)};hidden=journal.before.hidden;
            for(const auto& owned:journal.hiddenByUs)(void)msiNativeForegroundVisibility(owned,true);
            return ticket;
        };
        auto postStep=[&](uint64_t ticket){std::lock_guard hostLock(g_inputHostMx);return msiPostHideCycleStep(ticket);};
        auto postAwait=[&](uint64_t ticket,bool* cycleCompleted=nullptr){std::unique_lock<InputHostMutex> hostLock(g_inputHostMx);return ymStartAwaitGpdPostHideCycle(ticket,hostLock,cycleCompleted);};
        auto postReadmit=[&](uint64_t ticket){std::lock_guard hostLock(g_inputHostMx);return ymStartReadmitGpdPostHideSource(ticket);};
        auto postRetire=[&](){std::lock_guard hostLock(g_inputHostMx);return msiPostHideCancelForStop("offline-post-hide-retire");};
        auto noPostTarget=[&](){return !g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostPrepareMayHaveReachedHost&&!g_inputHostActive;};
        auto ticket=postSetup();const auto owned=g_hidHideVisibilityJournal.hiddenByUs;
        check("GPD post-hide owns one exact helper under original journal/request/run/epoch/power",
            postStep(ticket)==1&&xusbRefreshes==std::vector<std::string>{familyRoot}&&gameRefreshes.empty()&&cycles.empty()&&
            g_msiNativeForegroundLease.postHideCycle&&g_msiNativeForegroundLease.postHideTicket==ticket&&
            g_msiNativeForegroundLease.postHideRunId==g_inputHostRunId&&g_msiNativeForegroundLease.postHideEpoch==g_inputHostEpoch&&
            g_msiNativeForegroundLease.power==currentPowerGeneration()&&g_hidHideVisibilityJournal.hiddenByUs==owned&&writes.size()==2&&noPostTarget());
        check("GPD readmit cannot run before the same SDK helper terminal",
            postReadmit(ticket)==1&&readmitCalls==0&&sourceReads==0&&g_msiNativeForegroundCyclePending&&
            !g_msiNativeForegroundLease.postHideReadmitAttempted&&xusbRefreshes.size()==1&&noPostTarget());
        bool pending=true;const auto pendingPolls=polls;
        for(unsigned repeat=0;repeat<4;++repeat){const int result=postStep(ticket);pending=(result==1)&&pending;}
        check("GPD post-hide repeated pending polls retain the same writer without resend or source shortcut",
            pending&&polls==pendingPolls+4&&g_msiNativeForegroundCyclePending&&g_msiNativeForegroundLease.syntheticCycle&&
            xusbRefreshes.size()==1&&sourceReads==0&&writes.size()==2&&noUnhide()&&noPostTarget());
        pollResult=0;
        check("GPD post-hide waits for a new source baseline only after actual terminal poll",
            postStep(ticket)==1&&!g_msiNativeForegroundCyclePending&&g_msiNativeForegroundLease.cycleAccepted&&
            g_msiNativeForegroundLease.helperExitCode==0&&g_msiNativeForegroundLease.postHideTerminalTick&&
            g_msiNativeForegroundLease.postHideBaselineSequence==postSequence&&sourceReads==1&&xusbRefreshes.size()==1&&noPostTarget());
        const auto terminalBaseline=g_msiNativeForegroundLease.postHideBaselineSequence;
        readmitResult=1;bool readmitWaiting=true;
        for(unsigned repeat=0;repeat<3;++repeat){const int admitted=postReadmit(ticket),waiting=postStep(ticket);readmitWaiting=(admitted==1&&waiting==1)&&readmitWaiting;}
        check("GPD terminal readmit wait neither reissues cycle nor reads or publishes a premature source",
            readmitWaiting&&readmitCalls==3&&sourceReads==1&&!g_msiNativeForegroundLease.postHideReadmitAttempted&&
            !g_msiNativeForegroundLease.postHideReadmitted&&g_xinputSlotFullScanOnce&&
            !g_xinputPreTargetBoundaryScanned&&xusbRefreshes.size()==1&&writes.size()==2&&noPostTarget());
        readmitResult=0;const int readmitted=postReadmit(ticket);
        const auto bindingBaseline=g_msiNativeForegroundLease.postHideBaselineSequence;
        sourceFault=0;const int cachedAtReadmit=postStep(ticket);
        sourceFault=11;const int startedBeforeReadmit=postStep(ticket);
        check("GPD successful readmit is once only and requires a later sequence and read window",
            readmitted==0&&postReadmit(ticket)==0&&readmitCalls==4&&g_msiNativeForegroundLease.postHideReadmitAttempted&&
            g_msiNativeForegroundLease.postHideReadmitted&&bindingBaseline>terminalBaseline&&
            g_msiNativeForegroundLease.postHideSourceAfterTick>=g_msiNativeForegroundLease.postHideTerminalTick&&
            cachedAtReadmit==1&&startedBeforeReadmit==1&&xusbRefreshes.size()==1&&writes.size()==2&&noPostTarget());
        const char* sourceLabels[]={"GPD post-hide rejects a source sequence at baseline",
            "GPD post-hide rejects a read started before helper terminal",
            "GPD post-hide requires current source power, identity and topology",
            "GPD post-hide requires a live stable source read",
            "GPD post-hide requires XInput route and nonzero incarnation/continuity"};
        const std::vector<std::vector<unsigned>> sourceFaults={{0},{1},{2,3,4},{5,6},{7,8,9}};
        for(unsigned group=0;group<sourceFaults.size();++group){
            bool waiting=true;for(const auto fault:sourceFaults[group]){sourceFault=fault;const int result=postStep(ticket);waiting=(result==1)&&waiting;}
            check(sourceLabels[group],waiting&&g_msiNativeForegroundLease.active&&xusbRefreshes.size()==1&&writes.size()==2&&noPostTarget());
        }
        sourceFault=10;const auto beforeContinue=g_virtualStartProtection.view(inputHostStartProtectionNow());
        bool continuedCycleCompleted=false;const bool continued=postAwait(ticket,&continuedCycleCompleted);const auto afterContinue=g_virtualStartProtection.view(inputHostStartProtectionNow());
        check("GPD post-hide current neutral-capable Source lets only original R3 await continue without target creation",
            continued&&continuedCycleCompleted&&!g_msiNativeForegroundLease.active&&afterContinue.requestId==ticket&&afterContinue.fallbackAttempts==0&&
            afterContinue.remainingMs<=beforeContinue.remainingMs&&g_virtualStartProtection.canContinueExecution(ticket,"elite",inputHostStartProtectionNow())&&
            g_hidHideVisibilityJournal.hiddenByUs==owned&&xusbRefreshes.size()==1&&writes.size()==2&&noUnhide()&&noPostTarget());
        const char* contextLabels[]={"GPD post-hide expired original deadline rejects await and retains live writer",
            "GPD post-hide changed Host epoch/run rejects await and retains live writer",
            "GPD post-hide changed power rejects await and retains live writer",
            "GPD post-hide changed persona intent rejects await and retains live writer"};
        for(unsigned context=0;context<4;++context){
            bool rejected=true;
            for(unsigned variant=0;variant<(context==1?3U:context==3?2U:1U);++variant){
                ticket=postSetup();const int started=postStep(ticket);
                if(context==0){const auto now=inputHostStartProtectionNow();const auto view=g_virtualStartProtection.view(now);(void)g_virtualStartProtection.view(now+view.remainingMs);}
                bool familyRejected=true;
                if(context==1){if(variant==0)++g_inputHostEpoch;else if(variant==1)g_inputHostRunId+="-other-run";
                    else{g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.manufacturer="ASUSTEK COMPUTER INC.";g_machineIdentity.product="RC72LA";
                        familyRejected=postStep(ticket)==2&&g_msiNativeForegroundCyclePending;}}
                if(context==2)g_powerGeneration.fetch_add(1);
                if(context==3){g_realStickTest.virtualTargetPersona="steamdeck";
                    if(variant){const auto now=inputHostStartProtectionNow();(void)g_virtualStartProtection.finish(ticket,"failure",now);(void)g_virtualStartProtection.admit("steamdeck",now);}}
                const bool allowed=postAwait(ticket);
                const bool held=familyRejected&&g_msiNativeForegroundCyclePending&&g_msiNativeForegroundLease.syntheticCycle&&
                    g_msiNativeForegroundLease.postHideAborted&&g_msiNativeForegroundLease.postHideTicket==ticket&&
                    g_hidHideVisibilityJournal.active&&g_hidHideVisibilityJournal.hiddenByUs==owned&&writes.size()==2&&noUnhide()&&noPostTarget();
                pollResult=0;const bool retired=postRetire();
                rejected=(started==1&&!allowed&&held&&retired&&!g_msiNativeForegroundLease.active&&
                    !g_msiNativeForegroundCyclePending&&xusbRefreshes.size()==1&&writes.size()==2&&noUnhide()&&noPostTarget())&&rejected;
            }
            check(contextLabels[context],rejected);
        }
        ticket=postSetup();const int stopStarted=postStep(ticket);
        {std::lock_guard hostLock(g_inputHostMx);inputHostStopLocked("offline-post-hide-stop");inputHostStopLocked("offline-post-hide-stop");}
        check("GPD actual Stop returns before unhide or Host retirement while original helper is pending",
            stopStarted==1&&g_inputHostStartBlocked&&g_inputHostFaultHoldsStart&&g_msiNativeForegroundCyclePending&&
            g_msiNativeForegroundLease.postHideAborted&&g_msiNativeForegroundLease.deferredVirtualWork==std::vector<std::string>{msiPostHideCleanupWork(g_inputHostEpoch,g_inputHostRunId)}&&
            g_hidHideVisibilityJournal.hiddenByUs==owned&&xusbRefreshes.size()==1&&writes.size()==2&&noUnhide()&&noPostTarget());
        pollResult=0;
        check("GPD post-hide cancellation clears only retired helper lease and leaves journal return duty",
            postRetire()&&!g_msiNativeForegroundLease.active&&!g_msiNativeForegroundCyclePending&&
            g_hidHideVisibilityJournal.active&&g_hidHideVisibilityJournal.hiddenByUs==owned&&writes.size()==2&&noUnhide()&&
            virtualJobs==std::vector<std::string>{msiPostHideCleanupWork(g_inputHostEpoch,g_inputHostRunId)}&&noPostTarget());
        for(const bool asynchronous:{false,true}){
            ticket=postSetup();familyXusbResult=asynchronous?1:2;
            const bool started=!asynchronous||postStep(ticket)==1;pollResult=2;
            check(asynchronous?"GPD post-hide async SDK failure rejects original await without new target":
                "GPD post-hide immediate SDK failure rejects original await without new target",
                started&&!postAwait(ticket)&&!g_msiNativeForegroundLease.active&&!g_msiNativeForegroundCyclePending&&
                xusbRefreshes.size()==1&&sourceReads==0&&g_hidHideVisibilityJournal.hiddenByUs==owned&&writes.size()==2&&noUnhide()&&noPostTarget());
        }
        ticket=postSetup();const int readmitFailureStarted=postStep(ticket);pollResult=0;
        const int readmitFailureTerminal=postStep(ticket);readmitResult=2;
        check("GPD failed pre-target readmit rejects original await without publication or a second cycle",
            readmitFailureStarted==1&&readmitFailureTerminal==1&&!postAwait(ticket)&&readmitCalls==1&&sourceReads==1&&
            !g_msiNativeForegroundLease.active&&!g_msiNativeForegroundCyclePending&&xusbRefreshes.size()==1&&
            g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId==ticket&&
            g_hidHideVisibilityJournal.hiddenByUs==owned&&writes.size()==2&&noUnhide()&&noPostTarget());
        for(const bool carried:{true,false}){
            ticket=postSetup();auto& journal=g_hidHideVisibilityJournal;journal.before.hidden=hidden;
            if(!carried)journal.hiddenByUs.clear();const auto duty=journal.hiddenByUs;
            bool skippedCycleCompleted=true;const bool skippedAwait=postAwait(ticket,&skippedCycleCompleted);
            check(carried?"GPD HC carried prehidden journal skips repeat cycle":
                "GPD HC foreign prehidden root grants no new cycle authority",
                postStep(ticket)==0&&skippedAwait&&!skippedCycleCompleted&&xusbRefreshes.empty()&&gameRefreshes.empty()&&cycles.empty()&&
                !g_msiNativeForegroundLease.active&&journal.hiddenByUs==duty&&writes.size()==2&&noUnhide()&&noPostTarget());
        }
        for(const unsigned pathIndex:{0U,1U}){
            ticket=postSetup();auto& journal=g_hidHideVisibilityJournal;
            const auto missing=journal.hiddenByUs[pathIndex];journal.hiddenByUs.erase(journal.hiddenByUs.begin()+pathIndex);
            const bool ownershipRejected=postStep(ticket)==2;journal.hiddenByUs=owned;
            hidden.erase(std::remove(hidden.begin(),hidden.end(),missing),hidden.end());
            check(pathIndex==0?"GPD post-hide requires both journal-owned and current readback root":
                "GPD post-hide requires both journal-owned and current readback HID",
                ownershipRejected&&postStep(ticket)==2&&xusbRefreshes.empty()&&!g_msiNativeForegroundLease.active&&writes.size()==2&&noPostTarget());
        }
        const char* familyLabels[]={"ROG post-hide stage adds no device action","MSI post-hide stage adds no device action",
            "OneX post-hide stage adds no device action","AYA post-hide stage adds no device action"};
        for(unsigned family=0;family<4;++family){
            ticket=postSetup();
            if(family==0){g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.manufacturer="ASUSTEK COMPUTER INC.";g_machineIdentity.product="RC72LA";}
            if(family==1){g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";}
            if(family==2)g_machineIdentity.family=YmccFamily::OneXPlayer;
            if(family==3)g_machineIdentity.family=YmccFamily::Ayanero;
            const auto expected=family==0?Profile::Rog:family==1?Profile::Msi:Profile::Unsupported;
            bool familyCycleCompleted=true;const bool familyAwait=postAwait(ticket,&familyCycleCompleted);
            check(familyLabels[family],nativeForegroundProfile()==expected&&postStep(ticket)==0&&familyAwait&&!familyCycleCompleted&&
                xusbRefreshes.empty()&&gameRefreshes.empty()&&cycles.empty()&&writes.size()==2&&!g_msiNativeForegroundLease.active&&noPostTarget());
        }
        // Empty readmit callback: consume the real pre-admission rejection path
        // only. Never reach admission's OpenXInput resolver/optional device API.
        ticket=postSetup();const int maskStarted=postStep(ticket);pollResult=0;const int maskTerminal=postStep(ticket);
        g_msiNativeForegroundTestIo.postHideReadmit={};
        const auto connectedBefore=g_xinputConnectedMask,virtualBefore=g_xinputVirtualEndpointMask,unknownBefore=g_xinputSlotUnknownMask;
        const auto sourceMaskBefore=g_xinputPhysicalSourceMask;const bool lockedBefore=g_xinputPhysicalSourceLocked;
        g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=0;
        g_xinputPreTargetBoundaryScanned=true;g_xinputBoundaryScanEpoch=g_xinputTopologyEpoch.load();g_xinputSlotFullScanOnce=false;
        bool rejectedMasks=true;
        for(unsigned boundary=0;boundary<4;++boundary){
            g_xinputConnectedMask=boundary==2?3:2;g_xinputVirtualEndpointMask=boundary==1?2:0;
            g_xinputSlotUnknownMask=boundary==0?2:0;sourceCount=boundary==3?2:1;
            const int admitted=postReadmit(ticket);
            rejectedMasks=(admitted==(boundary<2?1:2)&&g_xinputPhysicalSourceLocked&&g_xinputPhysicalSourceMask==0&&
                !g_msiNativeForegroundLease.postHideReadmitAttempted&&!g_msiNativeForegroundLease.postHideReadmitted)&&rejectedMasks;
        }
        check("GPD real readmit rejects unknown, virtual, multiple-slot or nonunique identity before resetting old locked mask",
            maskStarted==1&&maskTerminal==1&&rejectedMasks&&readmitCalls==0&&sourceReads==1&&
            xusbRefreshes.size()==1&&g_hidHideVisibilityJournal.hiddenByUs==owned&&writes.size()==2&&noUnhide()&&noPostTarget());
        pollResult=0;(void)postRetire();g_msiNativeForegroundTestIo.postHideReadmit=readmitIo;
        // Real Readmit metadata/guards/reset, with only its final standard
        // admission leaf replaced. No resolver, device call or second pipeline.
        const auto primaryBefore=g_xinputPrimarySlot,semanticBefore=g_xinputSemanticSlot,preTargetBefore=g_xinputPreTargetSlots;
        const auto reconfirmBefore=g_xinputSourceReconfirmMask.load();const auto reconfirmMsiBefore=g_xinputReconfirmMsiGeneration;
        const auto reconfirmIdentityBefore=g_xinputReconfirmIdentity;const bool rejectedBefore=g_xinputReconfirmRejectLogged;
        const auto admittedBefore=g_xinputAdmittedMask,loggedAdmittedBefore=g_xinputLoggedAdmittedMask;
        ticket=postSetup();const int leafStarted=postStep(ticket);pollResult=0;const int leafTerminal=postStep(ticket);
        const auto leafTerminalBaseline=g_msiNativeForegroundLease.postHideBaselineSequence;
        g_msiNativeForegroundTestIo.postHideReadmit={};unsigned selectedLeafCalls=0;bool selectedSawReset=false;
        g_xinputConnectedMask=2;g_xinputVirtualEndpointMask=g_xinputSlotUnknownMask=0;
        g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=0;g_xinputPrimarySlot=g_xinputSemanticSlot=0;
        g_xinputPreTargetSlots=1;g_xinputSourceReconfirmMask=1;g_xinputReconfirmIdentity="retired-slot-zero";
        g_xinputReconfirmMsiGeneration=7;g_xinputReconfirmRejectLogged=true;g_xinputAdmittedMask=g_xinputLoggedAdmittedMask=1;
        g_xinputPreTargetBoundaryScanned=true;g_xinputBoundaryScanEpoch=g_xinputTopologyEpoch.load();g_xinputSlotFullScanOnce=false;
        g_msiNativeForegroundTestIo.postHideAdmitSelectedSource=[&](){
            ++selectedLeafCalls;const auto& lease=g_msiNativeForegroundLease;
            selectedSawReset=!g_xinputPhysicalSourceLocked&&g_xinputPhysicalSourceMask==0&&g_xinputPreTargetSlots==0&&
                g_xinputSourceReconfirmMask==0&&g_xinputReconfirmIdentity.empty()&&g_xinputReconfirmMsiGeneration==0&&
                !g_xinputReconfirmRejectLogged&&g_xinputAdmittedMask==0&&g_xinputLoggedAdmittedMask==0&&
                g_xinputConnectedMask==2&&g_xinputVirtualEndpointMask==0&&g_xinputSlotUnknownMask==0&&
                g_xinputPreTargetBoundaryScanned&&g_xinputBoundaryScanEpoch==g_xinputTopologyEpoch.load()&&
                lease.root==familyRoot&&lease.postHideTicket==ticket&&lease.postHideEpoch==g_inputHostEpoch&&
                lease.postHideRunId==g_inputHostRunId&&lease.power==currentPowerGeneration()&&
                ymcc::msi::samePhysicalLocation(lease.locations,std::vector<std::wstring>{location})&&noPostTarget();
            if(!selectedSawReset)return false;
            g_xinputPrimarySlot=g_xinputSemanticSlot=1;g_xinputPhysicalSourceMask=g_xinputPreTargetSlots=2;g_xinputPhysicalSourceLocked=true;
            return true; // Only the standard source-admission leaf's result is fake.
        };
        const auto leafDiscoveries=discoveries;const int leafReadmitted=postReadmit(ticket);
        check("GPD real Readmit validates exact root/current slot and resets old locked mask before one selected-source leaf",
            leafStarted==1&&leafTerminal==1&&leafReadmitted==0&&selectedLeafCalls==1&&selectedSawReset&&readmitCalls==0&&
            discoveries==leafDiscoveries+1&&g_msiNativeForegroundLease.postHideReadmitAttempted&&g_msiNativeForegroundLease.postHideReadmitted&&
            g_msiNativeForegroundLease.postHideBaselineSequence>leafTerminalBaseline&&
            g_xinputPhysicalSourceLocked&&g_xinputPhysicalSourceMask==2&&g_xinputPreTargetSlots==2&&
            g_xinputPrimarySlot==1&&g_xinputSemanticSlot==1&&xusbRefreshes.size()==1&&noPostTarget());
        sourceFault=0;const int leafCached=postStep(ticket);sourceFault=11;const int leafEarly=postStep(ticket);sourceFault=10;
        bool leafCycleCompleted=false;const bool leafContinued=postAwait(ticket,&leafCycleCompleted);
        check("GPD real Readmit slot1 binding still waits for newer current source before original await continues",
            leafCached==1&&leafEarly==1&&leafContinued&&leafCycleCompleted&&selectedLeafCalls==1&&readmitCalls==0&&
            !g_msiNativeForegroundLease.active&&g_xinputPhysicalSourceLocked&&g_xinputPhysicalSourceMask==2&&
            g_virtualStartProtection.canContinueExecution(ticket,"elite",inputHostStartProtectionNow())&&
            g_hidHideVisibilityJournal.hiddenByUs==owned&&xusbRefreshes.size()==1&&writes.size()==2&&noUnhide()&&noPostTarget());
        g_msiNativeForegroundTestIo.postHideAdmitSelectedSource={};g_msiNativeForegroundTestIo.postHideReadmit=readmitIo;
        g_xinputPrimarySlot=primaryBefore;g_xinputSemanticSlot=semanticBefore;g_xinputPreTargetSlots=preTargetBefore;
        g_xinputSourceReconfirmMask=reconfirmBefore;g_xinputReconfirmIdentity=reconfirmIdentityBefore;
        g_xinputReconfirmMsiGeneration=reconfirmMsiBefore;g_xinputReconfirmRejectLogged=rejectedBefore;
        g_xinputAdmittedMask=admittedBefore;g_xinputLoggedAdmittedMask=loggedAdmittedBefore;
        g_xinputConnectedMask=connectedBefore;g_xinputVirtualEndpointMask=virtualBefore;g_xinputSlotUnknownMask=unknownBefore;
        g_xinputPhysicalSourceMask=sourceMaskBefore;g_xinputPhysicalSourceLocked=lockedBefore;
        // Bound cleanup is a pre-target return duty, not a later target's Stop.
        ticket=postSetup();const auto validCleanup=msiPostHideCleanupWork(g_inputHostEpoch,g_inputHostRunId);
        bool sameTuple=false,expiredTuple=false;
        {std::lock_guard hostLock(g_inputHostMx);sameTuple=msiPostHideCleanupWorkCurrentLocked(validCleanup);
            const auto now=inputHostStartProtectionNow();const auto view=g_virtualStartProtection.view(now);
            (void)g_virtualStartProtection.view(now+view.remainingMs);expiredTuple=msiPostHideCleanupWorkCurrentLocked(validCleanup);}
        check("GPD bound cleanup keeps the same pre-target return duty after original UI expiry",sameTuple&&expiredTuple&&noPostTarget());
        const std::vector<std::vector<unsigned>> cleanupFaults={{0,1,5},{2,3,4}};
        for(unsigned group=0;group<cleanupFaults.size();++group){
            bool preserved=true;
            for(const auto fault:cleanupFaults[group]){
                ticket=postSetup();std::string cleanup=msiPostHideCleanupWork(g_inputHostEpoch,g_inputHostRunId);
                const bool dispatchWasCurrent=msiNativeForegroundBeforeVirtualWork(cleanup);
                if(fault==0)++g_inputHostEpoch;
                if(fault==1)g_inputHostRunId+="-replacement";
                if(fault==2)g_inputHostPrepared=true;
                if(fault==3)g_inputHostPrepareMayHaveReachedHost=true;
                if(fault==4)g_inputHostActive=true;
                if(fault==5)cleanup="fault-stop:post-hide-cycle-cancelled";
                const auto epoch=g_inputHostEpoch;const auto run=g_inputHostRunId;const auto visible=hidden;
                const bool prepared=g_inputHostPrepared,mayPrepare=g_inputHostPrepareMayHaveReachedHost,active=g_inputHostActive;
                const auto stopPolls=polls;bool stale=false;
                {std::lock_guard hostLock(g_inputHostMx);stale=!msiPostHideCleanupWorkCurrentLocked(cleanup);inputHostStopLocked(cleanup.c_str());}
                const bool dispatchHeld=!msiNativeForegroundBeforeVirtualWork(cleanup);
                preserved=(dispatchWasCurrent&&stale&&dispatchHeld&&g_inputHostEpoch==epoch&&g_inputHostRunId==run&&
                    g_inputHostPrepared==prepared&&g_inputHostPrepareMayHaveReachedHost==mayPrepare&&g_inputHostActive==active&&
                    g_hidHideVisibilityJournal.active&&g_hidHideVisibilityJournal.hiddenByUs==owned&&hidden==visible&&
                    writes.size()==2&&polls==stopPolls&&xusbRefreshes.empty()&&!g_postHideCycleOwned&&
                    !g_inputHostStartBlocked&&!g_inputHostFaultHoldsStart&&noUnhide())&&preserved;
            }
            check(group==0?"GPD stale epoch/run and legacy unbound cleanup retire at dispatch and final Stop throat":
                "GPD same-tuple prepared, possibly-prepared or active target cannot be stopped by old pre-target cleanup",preserved);
        }
        for(const bool unresolved:{true,false}){
            ticket=postSetup();const int drainStarted=postStep(ticket);
            const auto drainCleanup=msiPostHideCleanupWork(g_inputHostEpoch,g_inputHostRunId);
            {std::lock_guard hostLock(g_inputHostMx);inputHostStopLocked("offline-owned-drain-cancel");}
            if(unresolved)g_machineIdentity.resolved=false;
            else{g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.manufacturer="ASUSTEK COMPUTER INC.";g_machineIdentity.product="RC72LA";}
            const bool fenced=!msiNativeForegroundBeforeVirtualWork("persona-transition");
            check(unresolved?"GPD unresolved family retains owned writer BeforeVirtualWork fence":
                "GPD changed family retains old owned writer BeforeVirtualWork fence",
                drainStarted==1&&fenced&&g_postHideCycleOwned&&g_msiNativeForegroundCyclePending&&
                g_msiNativeForegroundLease.postHideCycle&&g_msiNativeForegroundLease.syntheticCycle&&
                g_msiNativeForegroundLease.postHideAborted&&g_msiNativeForegroundLease.postHideTicket==ticket&&
                g_hidHideVisibilityJournal.hiddenByUs==owned&&xusbRefreshes.size()==1&&writes.size()==2&&virtualJobs.empty()&&noUnhide()&&noPostTarget());
            msiNativeForegroundMaintenance();msiNativeForegroundHandleWork();
            check("GPD owned maintainer precedes family activation and cannot drop a still-live writer",
                g_postHideCycleOwned&&g_msiNativeForegroundCyclePending&&g_msiNativeForegroundLease.active&&
                g_msiNativeForegroundLease.syntheticCycle&&g_msiNativeForegroundLease.postHideTicket==ticket&&
                g_hidHideVisibilityJournal.hiddenByUs==owned&&xusbRefreshes.size()==1&&writes.size()==2&&virtualJobs.empty()&&noUnhide()&&noPostTarget());
            pollResult=0;msiNativeForegroundMaintenance();
            check("GPD family-independent maintainer drains only exact terminal writer then releases fence and replays bound cleanup",
                !g_postHideCycleOwned&&!g_msiNativeForegroundCyclePending&&!g_msiNativeForegroundLease.active&&
                virtualJobs==std::vector<std::string>{drainCleanup,"persona-transition"}&&
                msiNativeForegroundBeforeVirtualWork("persona-transition")&&g_hidHideVisibilityJournal.active&&
                g_hidHideVisibilityJournal.hiddenByUs==owned&&xusbRefreshes.size()==1&&writes.size()==2&&noUnhide()&&noPostTarget());
        }
        g_xinputTopologyEpoch=savedTopology;g_xinputSlotIdentityEpoch=savedIdentity;g_xinputSlotReprobeMask=savedReprobe;
        g_xinputSlotFullScanEpochStart=savedScanStart;g_xinputBoundaryScanEpoch=savedBoundaryEpoch;
        g_xinputDiscoveryDirty=savedDirty;g_xinputPreTargetBoundaryScanned=savedBoundary;g_xinputSlotFullScanOnce=savedScan;
        g_inputHostEpoch=savedEpoch;g_inputHostRunId=savedRun;g_inputHostSwitchRecoveryStartDeadline=savedDeadline;
        g_hardwareWriteGate=savedGate;g_inputCaptureStop=savedCaptureStop;g_inputHostPowerAdmissionRevoked=savedRevoked;
        g_inputHostStartBlocked=false;g_inputHostFaultHoldsStart=false;
    }

    reset();g_msiNativeForegroundTestIo={};g_msiForegroundTestIo={};
    std::cout<<"MSI_NATIVE_ONLY cases="<<cases<<" failures="<<failures<<" realHardwareAccess=false\n";
    return {cases,failures};
}
