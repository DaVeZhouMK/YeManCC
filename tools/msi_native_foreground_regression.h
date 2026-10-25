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
        g_inputReady=true;g_powerGeneration=3;g_powerLifecycle=PowerLifecycle::Ready;
        g_physicalReleaseWindowStage=0;g_physicalReleaseWindowTransition=0;g_physicalReleaseWindowNeutral=false;
        g_msiNativeForegroundLease=MsiNativeForegroundLease{};g_msiNativeForegroundEdge=1;
        g_msiNativeForegroundSourceRetryNeeded=false;g_msiNativeForegroundSourceRetryGeneration=0;
        g_msiNativeForegroundWatchGameSources=false;
        thirdGamePresent=currentInventoryComplete=true;thirdGameUsage=5;gameRefreshResult=0;gameRefreshes.clear();
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
            gameRefreshes.push_back(value.deviceInstanceId);if(value.baseContainerDeviceInstanceId!=familyRoot || position!=location){++failures;return 2;}return gameRefreshResult;};
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

        configureFamily(rog);gameRefreshResult=2;enter();
        check("ROG/GPD failed node refresh returns private config without root-cycle fallback",hidden.empty() && g_msiNativeForegroundLease.configurationReturned && g_msiNativeForegroundLease.refreshFailed && cycles.empty());
        g_realStickTest.virtualTargetPersona="steamdeck";g_realStickTest.virtualTargetEnabled=true;
        check("ROG/GPD terminal failed refresh can hand back proven config without claiming external reset",msiNativeForegroundBeforeVirtualWork("persona-transition") && !g_msiNativeForegroundLease.active && cycles.empty());

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
    reset();g_msiNativeForegroundTestIo={};g_msiForegroundTestIo={};
    std::cout<<"MSI_NATIVE_ONLY cases="<<cases<<" failures="<<failures<<" realHardwareAccess=false\n";
    return {cases,failures};
}
