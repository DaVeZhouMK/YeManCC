// Execute actual production MSI foreground helpers with offline-only I/O.
// No wWinMain, product startup, controller/driver access, Steam or installation.
#define YMCC_MSI_FOREGROUND_ISOLATION_TEST 1
#include <iostream>
#include "../native/main.cpp"
#include "msi_native_foreground_regression.h"



// Actual new-attempt prefix and existing injected terminal paths. No helper
// launch/device APIs: each case rejects before launch or uses an existing I/O seam.
static std::pair<unsigned,unsigned> msiHelperDiagnosticAttributionRegression() {
    unsigned cases=0,failures=0;
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<"helper-facts: "<<name<<"\n";};
    auto oldIo=g_msiNativeForegroundTestIo;
    auto seed=[&](){g_msiNativeForegroundLease=MsiNativeForegroundLease{};g_msiNativeForegroundCyclePending=false;
        auto& lease=g_msiNativeForegroundLease;lease.helperDiagnostic={{"state","record-accepted"},{"reportedFacts",{{"Stage","first"},{"Result",8}}}};
        lease.helperGameTarget="OLD-TARGET";lease.helperDllSha256="OLD-DLL";g_msiNativeForegroundTestIo={};};
    seed();g_msiNativeForegroundLease.profile=ymcc::nativeForeground::Profile::Rog;
    check("policy rejection cannot attach old facts to a new root attempt",msiNativeForegroundCycleLocked(false)==2&&g_msiNativeForegroundLease.helperDiagnostic.is_null()&&g_msiNativeForegroundLease.helperGameTarget.empty());
    seed();HidHidePhysicalIdentity target;target.deviceInstanceId="NEW-TARGET";
    check("missing location cannot attach old facts to a new game target",msiNativeForegroundCycleLocked(true,&target)==2&&g_msiNativeForegroundLease.helperDiagnostic.is_null()&&g_msiNativeForegroundLease.helperGameTarget=="NEW-TARGET");
    for(const int result:{0,1,2}){
        seed();g_msiNativeForegroundLease.locations={L"PINNED-PORT"};g_msiNativeForegroundTestIo.gameRefresh=[&](const auto&,const auto&){return result;};
        check("new injected game result never borrows prior facts",msiNativeForegroundCycleLocked(false,&target)==result&&g_msiNativeForegroundLease.helperDiagnostic.is_null());
    }
    seed();g_msiNativeForegroundLease.locations={L"PINNED-PORT"};g_msiNativeForegroundTestIo.xusbRefresh=[](const auto&,const auto&){return 2;};
    check("new XUSB failure never borrows old HID facts",msiNativeForegroundCycleLocked(true,&target,true)==2&&g_msiNativeForegroundLease.helperDiagnostic.is_null()&&g_msiNativeForegroundLease.helperXusbTarget);
    g_msiNativeForegroundLease=MsiNativeForegroundLease{};g_msiNativeForegroundCyclePending=false;g_msiNativeForegroundTestIo=oldIo;
    return {cases,failures};
}

// F preserves intent, never foreground permission. Exercise the real callback
// next to an outstanding native lease, with all helper/device I/O injected.
// This is not a full Start, actual concurrency or hardware acceptance test.
static std::pair<unsigned,unsigned> msiExplicitRearmForegroundBoundaryRegression() {
    unsigned cases=0,failures=0,polls=0,cycles=0,reads=0,writes=0,locations=0,nativeQueues=0;
    int pollResult=-1;
    std::vector<std::string> virtualJobs;
    const std::string root="USB\\VID_0DB0&PID_1901\\OFFLINE-F-BOUNDARY";
    const std::wstring location=L"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4)";
    const std::vector<std::wstring> foreignHidden={L"HID\\FOREIGN-OWNED-ENTRY"};
    auto check=[&](const char* label,bool ok){++cases;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<"F-focus-boundary: "<<label<<"\n";};
    const auto oldTicket=g_msiExplicitRearmTicket.snapshot();const auto oldNativeIo=g_msiNativeForegroundTestIo;const auto oldForegroundIo=g_msiForegroundTestIo;
    auto seed=[&](){
        g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.product="MS-1T91";g_machineIdentity.resolved=true;
        g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="steamdeck";
        g_inputHostProcess=nullptr;g_inputHostReleaseQuarantined=false;g_inputHostFaultHoldsStart=false;
        g_inputHostTransportFault=false;g_inputHostExitFence=false;g_inputHostPowerAdmissionRevoked=false;
        g_inputHostStartBlocked=true;g_inputHostPrepared=false;g_inputHostNeutralized=false;
        g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_powerGeneration=3;g_hidHideVisibilityJournal=HidHideVisibilityJournal{};
        g_msiForegroundUiOwned=true;g_msiForegroundUiEdge=11;g_msiNativeForegroundDesiredUi=true;g_msiNativeForegroundEdge=11;
        g_msiNativeForegroundPowerHold=false;g_msiNativeForegroundResumePending=false;g_msiNativeForegroundSourceRetryNeeded=false;
        g_msiNativeForegroundCyclePending=true;g_msiNativeForegroundLease=MsiNativeForegroundLease{};
        auto& lease=g_msiNativeForegroundLease;lease.active=true;lease.configurationReturned=true;lease.cycleIssued=true;lease.syntheticCycle=true;
        lease.root=root;lease.locations={location};lease.owned={U2W(root)};lease.power=3;lease.edge=11;lease.before.hidden=foreignHidden;
        ymcc::msi::rearm::State t;t.sequence=3;t.offSequence=2;t.startedSequence=1;t.settingPower=t.releasePower=3;t.releaseFloor=2;
        t.releaseEligible=t.wanted=true;t.persona="steamdeck";g_msiExplicitRearmTicket.restore(t);
        polls=cycles=reads=writes=locations=nativeQueues=0;pollResult=-1;virtualJobs.clear();
    };
    g_msiForegroundTestIo={};g_msiNativeForegroundTestIo={};
    g_msiForegroundTestIo.locations=[&](const std::string& r){++locations;return r==root?std::vector<std::wstring>{location}:std::vector<std::wstring>{};};
    g_msiNativeForegroundTestIo.snapshot=[&](HidHideVisibilitySnapshot& s){++reads;s.valid=true;s.hidden=foreignHidden;return true;};
    g_msiNativeForegroundTestIo.visibility=[&](const std::wstring&,bool){++writes;return false;};
    g_msiNativeForegroundTestIo.cloak=[&](bool){++writes;return false;};
    g_msiNativeForegroundTestIo.poll=[&](){++polls;return pollResult;};
    g_msiNativeForegroundTestIo.cycle=[&](const std::string& r,const std::wstring& l,bool restoring){++cycles;return r==root&&l==location&&restoring?0:2;};
    g_msiNativeForegroundTestIo.queue=[&](){++nativeQueues;};
    g_msiNativeForegroundTestIo.virtualQueue=[&](const std::string& reason){virtualJobs.push_back(reason);};
    auto preserve=[&](){std::lock_guard hostLock(g_inputHostMx);return msiExplicitRearmRestoredLocked();};
    auto noIo=[&](){return polls+cycles+reads+writes+locations+nativeQueues==0&&virtualJobs.empty();};
    auto owned=[&](){return g_msiNativeForegroundLease.active&&g_msiNativeForegroundLease.configurationReturned&&
        g_msiNativeForegroundLease.owned==std::vector<std::wstring>{U2W(root)}&&g_msiNativeForegroundCyclePending&&
        g_msiForegroundUiOwned&&g_msiForegroundUiEdge==11&&g_msiNativeForegroundEdge==11;};
    seed();const bool preserved=preserve();
    check("actual callback has F flag differential",preserved==ymcc::msi::rearm::buildEnabled&&g_inputHostStartBlocked==!ymcc::msi::rearm::buildEnabled);
    check("ON preservation leaves ownership, UI edge and same live helper untouched",owned()&&noIo());
    check("intent is not output preparation, neutralization or publication",!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostNeutralized&&!g_hidHideVisibilityJournal.active);
    check("actual native pre-virtual gate rejects SAME live helper",!msiNativeForegroundBeforeVirtualWork("persona-transition")&&owned()&&virtualJobs.empty()&&cycles==0&&reads==0&&writes==0);
    (void)msiNativeForegroundBeforeVirtualWork("persona-transition");
    check("repeated request deduplicates original deferred reason",g_msiNativeForegroundLease.deferredVirtualWork==std::vector<std::string>{"persona-transition"}&&virtualJobs.empty()&&cycles==0);
    msiNativeForegroundMaintenance();
    check("live poll retains ownership without replacement",polls==1&&nativeQueues==0&&owned()&&cycles==0&&virtualJobs.empty());
    pollResult=0;msiNativeForegroundMaintenance();
    check("same helper terminal only queues existing native work",polls==2&&nativeQueues==1&&!g_msiNativeForegroundCyclePending&&g_msiNativeForegroundLease.active&&virtualJobs.empty()&&cycles==0);
    check("terminal native return replays exact original virtual reason",msiNativeForegroundBeforeVirtualWork("persona-transition")&&!g_msiNativeForegroundLease.active&&virtualJobs==std::vector<std::string>{"persona-transition"}&&cycles==1);
    check("handoff never rehides foreign entries or activates virtual output",writes==0&&reads==0&&!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostNeutralized&&!g_hidHideVisibilityJournal.active);
    (void)msiNativeForegroundBeforeVirtualWork("persona-transition");
    check("completed return never cycles or replays again",cycles==1&&virtualJobs.size()==1);
    seed();g_inputHostReleaseQuarantined=true;
    check("still-quarantined release cannot consume ON or lift block",!preserve()&&g_inputHostStartBlocked&&g_msiExplicitRearmTicket.snapshot().consumedSequence==0&&owned()&&noIo());
    seed();g_hidHideVisibilityJournal.active=true;
    check("unfinished physical journal cannot consume ON",!preserve()&&g_inputHostStartBlocked&&g_msiExplicitRearmTicket.snapshot().consumedSequence==0&&owned()&&noIo());
    seed();g_powerGeneration=4;
    check("new power generation rejects old release ticket",!preserve()&&g_inputHostStartBlocked&&g_msiExplicitRearmTicket.snapshot().consumedSequence==0&&owned()&&noIo());
    seed();g_machineIdentity.product="unregistered-msi";
    check("unregistered MSI callback leaves gates and ownership untouched",!preserve()&&g_inputHostStartBlocked&&g_msiExplicitRearmTicket.snapshot().consumedSequence==0&&owned()&&noIo());
    seed();g_machineIdentity.family=YmccFamily::AsusRogAllyClassic;
    check("ROG callback never uses MSI intent escape",!preserve()&&g_inputHostStartBlocked&&g_msiExplicitRearmTicket.snapshot().consumedSequence==0&&owned()&&noIo());
    seed();g_machineIdentity.family=YmccFamily::Unknown;
    check("common/unknown callback never uses MSI intent escape",!preserve()&&g_inputHostStartBlocked&&g_msiExplicitRearmTicket.snapshot().consumedSequence==0&&owned()&&noIo());
    g_msiExplicitRearmTicket.restore(oldTicket);g_msiNativeForegroundTestIo=oldNativeIo;g_msiForegroundTestIo=oldForegroundIo;
    g_msiNativeForegroundLease=MsiNativeForegroundLease{};g_msiNativeForegroundCyclePending=false;g_msiNativeForegroundDesiredUi=false;
    g_msiForegroundUiOwned=false;g_inputHostProcess=nullptr;g_inputHostReleaseQuarantined=false;g_hidHideVisibilityJournal=HidHideVisibilityJournal{};
    std::cout<<"MSI_EXPLICIT_REARM_FOREGROUND_BOUNDARY cases="<<cases<<" failures="<<failures<<" no-real-Host no-controller-IO not-whole-Start-proof\n";
    return {cases,failures};
}

int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    g_yemanccDataDir=argv[1];fspath::create_directories(g_yemanccDataDir);
    // The real logger uses app_data_dir(), not the input-data root. Keep every
    // offline receipt inside this fixture; never initialize a user app-data path.
    g_appDataDir=g_yemanccDataDir;
    unsigned cases=0,failures=0,locationReads=0,inventoryReads=0,snapshotReads=0,discoveryReads=0;
    std::vector<std::string> queue;
    std::vector<std::wstring> hidden,writes;
    ymcc::msi::NativeForegroundXusbInventory xusbInventory;unsigned xusbReads=0;
    bool loseXusbPort=false;
    const std::string xusbId="USB\\VID_0DB0&PID_1901&IG_00\\XUSB-GAME";
    bool ui=true,readOk=true,writeOk=true,cloak=true,inverseOff=true,exempt=true;
    bool samePort=true,includeObserved=true,loseFocusOnHide=false;
    unsigned discoveryCount=1;
    auto check=[&](const char* name,bool passed){++cases;if(!passed)++failures;std::cout<<(passed?"PASS ":"FAIL ")<<name<<"\n";};
    const std::string mainRoot="USB\\VID_0DB0&PID_1902\\5&3AF6909B&0&4";
    const std::string returnRoot="USB\\VID_0DB0&PID_1901\\00006F64096B22E7";
    auto source=[&](){HidHidePhysicalIdentity s;s.resolved=true;s.vendorId=0x0db0;s.productId=0x1901;s.usagePage=1;s.usage=5;
        s.baseContainerDeviceInstanceId=returnRoot;s.deviceInstanceId="HID\\VID_0DB0&PID_1901&IG_01\\PHYSICAL";return s;};
    auto record=[&](const std::string& id,unsigned vid,unsigned pid,unsigned page,unsigned usage,const std::string& root){
        HidHideInterfaceRecord r;r.instanceId=id;r.baseContainerDeviceInstanceId=root;r.vendorId=vid;r.productId=pid;r.usagePage=page;r.usage=usage;return r;};
    auto reset=[&](){
        g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";
        g_machineIdentity.product="MS-1T91";g_machineIdentity.resolved=true;
        g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;
        g_inputHostEpoch=4;g_inputHostEpochMirror=4;g_inputHostBoundPersona="steamdeck";
        g_inputHostProcess=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(1));
        g_inputHostPrepared=true;g_inputHostNeutralized=true;g_inputHostTransportFault=false;
        g_inputHostPowerAdmissionRevoked=false;g_inputHostExitFence=false;g_inputReady=true;
        g_powerGeneration=3;g_powerLifecycle=PowerLifecycle::Ready;
        g_msiDinputRouteDinputArmed=true;g_msiDinputReturnOwed=true;
        g_msiMainSource=HidHideMsiMainSourceRecord{};g_msiMainSource.valid=true;g_msiMainSource.generation=10;g_msiMainSource.usbInstance=mainRoot;g_msiMainSource.vendorId=0x0db0;g_msiMainSource.productId=0x1902;
        g_physicalReleaseWindowStage=0;g_physicalReleaseWindowTransition=0;g_physicalReleaseWindowNeutral=false;
        g_hidHideVisibilityJournal=HidHideVisibilityJournal{};g_hidHideVisibilityJournal.active=true;
        g_hidHideVisibilityJournal.transactionId="offline-owned-msi-session";
        g_msiForegroundIsolation=MsiForegroundIsolationState{};
        g_msiForegroundUiOwned=false;g_msiForegroundUiEdge=0;g_msiForegroundAnchorRevision=0;
        queue.clear();hidden.clear();writes.clear();locationReads=inventoryReads=snapshotReads=discoveryReads=0;discoveryCount=1;
        xusbInventory=ymcc::msi::NativeForegroundXusbInventory{};xusbInventory.complete=true;xusbReads=0;loseXusbPort=false;
        ui=readOk=writeOk=cloak=inverseOff=exempt=samePort=includeObserved=true;loseFocusOnHide=false;
        HidHidePhysicalIdentity main;main.resolved=true;main.vendorId=0x0db0;main.productId=0x1902;main.usagePage=1;main.usage=5;
        main.baseContainerDeviceInstanceId=mainRoot;main.deviceInstanceId="HID\\VID_0DB0&PID_1902&MI_00&COL01\\LEASED";
        g_hidHideVisibilityJournal.physical=main;g_hidHideVisibilityJournal.physicalSources={main};
        g_hidHideVisibilityJournal.hiddenByUs={U2W(mainRoot),U2W(main.deviceInstanceId)};
        hidden=g_hidHideVisibilityJournal.hiddenByUs;
    };
    g_msiForegroundTestIo.locations=[&](const std::string& root){++locationReads;
        return std::vector<std::wstring>{(root==mainRoot || samePort)?L"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4)":L"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(7)"};};
    g_msiForegroundTestIo.inventory=[&](const HidHidePhysicalIdentity&){++inventoryReads;
        std::vector<HidHideInterfaceRecord> rows;
        if(includeObserved) rows.push_back(record(source().deviceInstanceId,0x0db0,0x1901,1,5,returnRoot));
        rows.push_back(record("HID\\VID_0DB0&PID_1901&IG_02\\PHYSICAL",0x0db0,0x1901,1,5,returnRoot));
        rows.push_back(record("HID\\VID_0DB0&PID_1901&IG_03\\PHYSICAL",0x0db0,0x1901,1,5,returnRoot));
        rows.push_back(record("HID\\VID_0DB0&PID_1901&MI_02&COL03\\VENDOR",0x0db0,0x1901,0xff00,0x40,returnRoot));
        rows.push_back(record("HID\\KEYBOARD",0x0db0,0x1901,1,6,returnRoot));
        rows.push_back(record("HID\\MOUSE",0x0db0,0x1901,1,2,returnRoot));
        rows.push_back(record("HID\\CONSUMER",0x0db0,0x1901,0x0c,1,returnRoot));
        rows.push_back(record("HID\\SENSOR",0x0db0,0x1901,0x20,1,returnRoot));
        rows.push_back(record("HID\\OTHER-HANDHELD",0x0b05,0x1901,1,5,returnRoot));
        rows.push_back(record("HID\\SECOND-MSI",0x0db0,0x1901,1,5,"USB\\OTHER-PORT"));
        rows.push_back(record("HID\\VIGEM\\VIRTUAL",0x0db0,0x1901,1,5,returnRoot));
        return rows;};
    g_msiForegroundTestIo.nativeCandidates=[&](){++discoveryReads;
        std::vector<HidHidePhysicalIdentity> found;for(unsigned i=0;i<discoveryCount;++i)found.push_back(source());return found;};
    g_msiForegroundTestIo.xusbInventory=[&](const HidHidePhysicalIdentity&){++xusbReads;if(loseXusbPort)samePort=false;return xusbInventory;};
    g_msiForegroundTestIo.snapshot=[&](HidHideVisibilitySnapshot& s){++snapshotReads;
        if(!readOk) return false;s.valid=true;s.cloaking=cloak;s.inverseRaw=inverseOff?"off":"on";s.hidden=hidden;
        if(exempt)s.applications={hidHideCurrentModulePath()};return true;};
    g_msiForegroundTestIo.hide=[&](const std::wstring& path){writes.push_back(path);if(writeOk)hidden.push_back(path);if(loseFocusOnHide)ui=false;return writeOk;};
    g_msiForegroundTestIo.ui=[&](){return ui;};
    g_msiForegroundTestIo.queue=[&](const std::string& reason){queue.push_back(reason);};
    auto prepare=[&](){msiForegroundIsolationRememberMain(mainRoot);msiForegroundIsolationObserveNative(source());msiForegroundIsolationSetUiOwned(true);};
    auto run=[&](){for(const auto& job:queue)msiForegroundIsolationHandleWork(job);};
    reset();prepare();check("one MSI foreground job is queued",queue.size()==1);run();
    check("same physical port across serial/PID changes hides root plus all three game inputs",writes.size()==4);
    check("all own paths enter existing cleanup journal",g_hidHideVisibilityJournal.hiddenByUs.size()==6);
    check("keyboard mouse vendor consumer sensor foreign and virtual nodes never hidden",writes.size()==4 && std::all_of(writes.begin(),writes.end(),[](const auto& p){return p.find(L"VID_0DB0&PID_1901")!=std::wstring::npos;}));
    const auto savedReads=locationReads+inventoryReads+snapshotReads+xusbReads;
    run();check("a consumed lifecycle token never repeats PnP/query IO",savedReads==locationReads+inventoryReads+snapshotReads+xusbReads);
    for(unsigned i=0;i<1000;++i)msiForegroundIsolationSetUiOwned(true);
    for(unsigned i=0;i<100;++i)msiForegroundIsolationObserveNative(source());
    check("stable foreground has no extra PnP/query jobs",queue.size()==1 && savedReads==locationReads+inventoryReads+snapshotReads+xusbReads);
    check("MSI lease remains one physical source; alias paths do not become extra controllers",g_hidHideVisibilityJournal.physicalSources.size()==1 && g_hidHideVisibilityJournal.physicalSources.front().baseContainerDeviceInstanceId==mainRoot);
    g_msiMainSource.generation++;
    msiForegroundIsolationRememberMain(mainRoot);msiForegroundIsolationObserveNative(source());
    check("reused USB root on a NEW confirmed mode-2 generation has one new bounded attempt",queue.size()==2);
    const auto writesBeforeNewEpisode=writes.size();run();
    check("reconfirmed episode preserves owned paths and never duplicates hide writes",writes.size()==writesBeforeNewEpisode);
    const auto savedOwnership=msiForegroundIsolationCaptureOwnedPaths();
    g_hidHideVisibilityJournal.hiddenByUs.resize(2);
    msiForegroundIsolationRetainOwnedPaths(savedOwnership);
    check("reconnect bookkeeping preserves aliases for normal disable/exit cleanup",g_hidHideVisibilityJournal.hiddenByUs.size()==6);
    msiForegroundIsolationRetainOwnedPaths(savedOwnership);
    check("reconcile carry is idempotent and performs no driver writes",g_hidHideVisibilityJournal.hiddenByUs.size()==6&&writes.size()==writesBeforeNewEpisode);
    g_inputHostEpoch=5;g_hidHideVisibilityJournal.hiddenByUs.resize(2);
    msiForegroundIsolationRetainOwnedPaths(savedOwnership);
    check("reconcile carry cannot transfer old aliases into a new Host epoch",g_hidHideVisibilityJournal.hiddenByUs.size()==2);
    g_inputHostEpoch=4;g_hidHideVisibilityJournal.transactionId="new-owner";
    msiForegroundIsolationRetainOwnedPaths(savedOwnership);
    check("reconcile carry cannot transfer old aliases into another transaction",g_hidHideVisibilityJournal.hiddenByUs.size()==2);
    for(const char* board:{"MS-1T41","MS-1T42","MS-1T52","MS-1T8K","MS-1T91"}) {
        reset();g_machineIdentity.product=board;prepare();run();check("every registered MSI board uses same family policy",writes.size()==4);
    }
    for(const auto family:{YmccFamily::AsusRogAlly,YmccFamily::AsusRogAllyX,YmccFamily::AsusRogAllyClassic,YmccFamily::AsusRogAllyXClassic,YmccFamily::LenovoLegionGo,YmccFamily::Gpd,YmccFamily::ValveSteamDeck,YmccFamily::Unknown}) {
        reset();g_machineIdentity.family=family;prepare();run();check("ROG/generic family causes zero MSI I/O, queue or writes",queue.empty() && locationReads==0 && inventoryReads==0 && snapshotReads==0 && writes.empty());
    }
    reset();g_machineIdentity.manufacturer="ASUSTeK COMPUTER INC.";prepare();run();check("MSI board text on ASUS never grants policy",queue.empty()&&locationReads==0);
    reset();g_machineIdentity.product="UNKNOWN-MSI-BOARD";prepare();run();check("unknown MSI model is untouched",queue.empty()&&locationReads==0);
    reset();g_machineIdentity.resolved=false;prepare();run();check("unresolved identity is untouched",queue.empty()&&locationReads==0);
    reset();g_realStickTest.virtualTargetEnabled=false;prepare();run();check("virtual off default native state gains no MSI I/O",queue.empty()&&locationReads==0&&writes.empty());
    for(const std::string persona:{"steamdeck","dualsense-edge","elite"}) {
        reset();g_inputHostBoundPersona=persona;prepare();run();check("all three supported bound persona names use MSI-only physical policy",writes.size()==4);
    }
    reset();g_inputHostBoundPersona="dualshock4";prepare();run();check("retired DS4 target cannot authorize MSI physical policy",writes.empty()&&inventoryReads==0);
    reset();msiForegroundIsolationRememberMain(mainRoot);msiForegroundIsolationSetUiOwned(true);run();
    check("foreground entry hides current native root before first button without inventing source data",discoveryReads==1&&writes.size()==4&&!g_msiForegroundIsolation.observed.resolved);
    reset();msiForegroundIsolationSetUiOwned(true);check("UI entry without a confirmed physical lease does not scan or hide",queue.empty()&&locationReads==0);
    msiForegroundIsolationRememberMain(mainRoot);run();check("later authoritative main-source publication handles already-foreground YMCC",discoveryReads==1&&writes.size()==4);
    reset();discoveryCount=0;msiForegroundIsolationRememberMain(mainRoot);msiForegroundIsolationSetUiOwned(true);run();
    check("no current native root means one bounded discovery and zero hides",discoveryReads==1&&writes.empty());
    reset();discoveryCount=2;msiForegroundIsolationRememberMain(mainRoot);msiForegroundIsolationSetUiOwned(true);run();
    check("ambiguous foreground native root never selected by VID/PID alone",discoveryReads==1&&writes.empty());
    reset();prepare();g_msiForegroundIsolation.observedTick=GetTickCount64()-3000;run();check("stale RawInput sample refuses PnP scan and hide",writes.empty()&&inventoryReads==0);
    reset();prepare();g_realStickTest.virtualTargetEnabled=false;run();check("virtual disabled while queued cancels hide",writes.empty()&&inventoryReads==0);
    reset();prepare();g_msiDinputRouteDinputArmed=false;run();check("route disarmed while queued cancels hide",writes.empty()&&inventoryReads==0);
    reset();prepare();loseFocusOnHide=true;run();check("foreground loss during transaction stops subsequent writes and owns partial cleanup",writes.size()==1&&g_hidHideVisibilityJournal.hiddenByUs.size()==3);
    reset();prepare();samePort=false;run();check("another physical port never hidden despite same VID/PID",writes.empty());
    reset();prepare();includeObserved=false;run();check("vanished observed PnP interface never grants hide",writes.empty());
    reset();prepare();ui=false;run();check("lost frontend ownership before execution cancels queued hide",writes.empty()&&inventoryReads==0&&snapshotReads==0);
    reset();prepare();g_inputHostEpoch=5;run();check("old Host epoch cannot hide next session",writes.empty()&&inventoryReads==0);
    reset();prepare();g_powerGeneration=4;run();check("old power generation cannot hide after wake",writes.empty()&&inventoryReads==0);
    reset();prepare();g_msiForegroundAnchorRevision++;run();check("new physical lease invalidates old queued root",writes.empty()&&inventoryReads==0);
    reset();prepare();g_inputHostExitFence=true;run();check("exit fence blocks physical hide",writes.empty()&&inventoryReads==0);
    reset();prepare();g_powerLifecycle=PowerLifecycle::Suspended;run();check("suspend blocks physical hide",writes.empty()&&inventoryReads==0);
    reset();prepare();g_inputHostTransportFault=true;run();check("transport fault never bypassed",writes.empty());
    reset();prepare();g_inputHostBoundPersona.clear();run();check("requested-only persona is insufficient",writes.empty());
    reset();prepare();g_msiDinputReturnOwed=false;run();check("unowned firmware route never hidden",writes.empty());
    reset();prepare();g_physicalReleaseWindowStage=2;run();check("physical-release borrow window is not stolen",writes.empty());
    reset();prepare();g_physicalReleaseWindowStage=1;g_physicalReleaseWindowNeutral=true;run();check("open physical borrow window retains ownership",writes.empty());
    reset();prepare();g_physicalReleaseWindowTransition=1;run();check("borrow transition alone blocks new isolation",writes.empty());
    reset();prepare();readOk=false;run();check("unknown hide state fails closed without writes",writes.empty());
    reset();prepare();inverseOff=false;run();check("inverse HidHide contract never admitted",writes.empty());
    reset();prepare();exempt=false;run();check("YMCC native reader must stay exempt",writes.empty());
    reset();prepare();cloak=false;run();check("disabled cloak never claimed as isolation",writes.empty());
    reset();prepare();hidden.push_back(U2W("HID\\VID_0DB0&PID_1901&IG_02\\PHYSICAL"));run();
    check("third-party pre-hidden gaming path is preserved and not claimed",writes.size()==3&&g_hidHideVisibilityJournal.hiddenByUs.size()==5);
    reset();prepare();writeOk=false;run();check("failed hide remains journal-owned for normal rollback, never silently succeeds",writes.size()==4&&hidden.size()==2&&g_hidHideVisibilityJournal.hiddenByUs.size()==6);
    reset();prepare();msiForegroundIsolationSetUiOwned(false);run();check("foreground edge change invalidates queued work",writes.empty());
    check("pure predicate rejects stale source timestamp",!ymcc::msi::foregroundIsolationCurrent({}));

    auto xusbRecord=[&](){ymcc::msi::NativeForegroundXusbInterface item;
        item.activeInterface=true;item.ancestorMatched=true;item.instanceId=xusbId;item.ancestorRoot=returnRoot;return item;};
    reset();xusbInventory.interfaces={xusbRecord()};prepare();run();
    check("managed MSI adds only exact same-root active XUSB game PDO",writes.size()==5 && hidHideContainsPath(writes,xusbId));
    check("managed XUSB self-owned target joins existing cleanup journal",g_hidHideVisibilityJournal.hiddenByUs.size()==7 && hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,xusbId));
    const auto savedXusbReads=xusbReads;run();for(unsigned i=0;i<1000;++i)msiForegroundIsolationSetUiOwned(true);
    check("managed accepted token and unchanged ownership do not rescan XUSB",xusbReads==savedXusbReads && queue.size()==1);
    reset();xusbInventory.interfaces={xusbRecord()};hidden.push_back(U2W(xusbId));prepare();run();
    check("managed preexisting third-party XUSB is not adopted into own journal",writes.size()==4 && !hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,xusbId));
    reset();auto wrongXusb=xusbRecord();wrongXusb.ancestorRoot="USB\\VID_0DB0&PID_1901\\OTHER-PORT";xusbInventory.interfaces={wrongXusb};prepare();run();
    check("managed MSI refuses XUSB on another physical USB root",writes.size()==4 && !hidHideContainsPath(writes,xusbId));
    reset();wrongXusb=xusbRecord();wrongXusb.virtualDevice=true;xusbInventory.interfaces={wrongXusb};prepare();run();
    check("managed MSI never adopts virtual XUSB into physical suppression",writes.size()==4 && !hidHideContainsPath(writes,xusbId));
    reset();xusbInventory.complete=false;xusbInventory.reason="test-managed-inventory-unknown";prepare();run();
    check("managed incomplete XUSB discovery cannot hide a guessed subset",writes.empty() && g_hidHideVisibilityJournal.hiddenByUs.size()==2);
    reset();xusbInventory.interfaces={xusbRecord()};loseXusbPort=true;prepare();run();
    check("managed root move during XUSB enumeration prevents all writes",writes.empty());
    reset();xusbInventory.interfaces={xusbRecord()};ui=false;prepare();run();
    check("background managed MSI performs no XUSB lookup or hide",xusbReads==0 && writes.empty());
    reset();xusbInventory.interfaces={xusbRecord()};g_machineIdentity.family=YmccFamily::AsusRogAllyClassic;prepare();run();
    check("ROG route never reaches managed MSI XUSB collector",xusbReads==0 && writes.empty());


    // Diagnostic additions must decode the real gate without changing policy,
    // performing new I/O, retrying a consumed job, or inventing false values.
    using Facts=ymcc::msi::ForegroundIsolationFacts;
    using HostGuard=ymcc::msi::ForegroundHostGuard;
    const auto readyFacts=[](){Facts f;f.knownBoard=f.targetEnabled=f.boundTarget=f.uiOwned=f.hostReady=f.modeOwned=true;
        f.epoch=f.currentEpoch=f.power=f.currentPower=f.edge=f.currentEdge=1;
        f.observedTick=f.now=100;return f;};
    const auto hasFailure=[](const json& receipt,const char* name){
        const auto& values=receipt.at("failedGuards");
        return std::find(values.begin(),values.end(),json(name))!=values.end();};
    auto facts=readyFacts();
    check("diagnostic decoder has no failure for the unchanged admitted predicate",ymcc::msi::foregroundIsolationFailureBits(facts)==0 && ymcc::msi::foregroundIsolationCurrent(facts));
    const std::array<std::function<void(Facts&)>,13> failOne={
        [](Facts& f){f.knownBoard=false;},[](Facts& f){f.targetEnabled=false;},
        [](Facts& f){f.boundTarget=false;},[](Facts& f){f.uiOwned=false;},
        [](Facts& f){f.hostReady=false;},[](Facts& f){f.modeOwned=false;},
        [](Facts& f){f.exitFence=true;},[](Facts& f){f.releaseWindow=true;},
        [](Facts& f){f.epoch=0;},[](Facts& f){f.power=0;},[](Facts& f){f.edge=0;},
        [](Facts& f){f.observedTick=0;},[](Facts& f){f.now=2101;}};
    static constexpr const char* failNames[]={"unknown-or-unsupported-board","target-disabled","bound-target-not-admitted",
        "ui-not-owned-or-not-eligible","host-not-ready","mode-not-owned","exit-fence","physical-borrow-window",
        "host-epoch-mismatch","power-generation-mismatch","foreground-edge-mismatch","observed-timestamp-invalid","observed-sample-stale"};
    for(unsigned i=0;i<failOne.size();++i){facts=readyFacts();failOne[i](facts);
        const auto receipt=msiForegroundIsolationGateFacts(facts);
        check(failNames[i],!ymcc::msi::foregroundIsolationCurrent(facts) &&
            ymcc::msi::foregroundIsolationFailureBits(facts)==(1u<<i) && hasFailure(receipt,failNames[i]));}
    facts=readyFacts();facts.now=2100;
    check("diagnostic keeps the exact existing 2000ms admission boundary",ymcc::msi::foregroundIsolationCurrent(facts) && ymcc::msi::foregroundIsolationFailureBits(facts)==0);
    facts=readyFacts();facts.observedTick=101;
    check("future timestamp is unknown age not a fake stale age",!ymcc::msi::foregroundIsolationCurrent(facts) &&
        ymcc::msi::foregroundIsolationFailureBits(facts)==(1u<<11) && msiForegroundIsolationGateFacts(facts).at("observedAgeMs").is_null());
    bool allTruthPatterns=true;
    for(unsigned mask=0;mask<(1u<<13);++mask){facts=readyFacts();
        facts.knownBoard=(mask&1)!=0;facts.targetEnabled=(mask&2)!=0;facts.boundTarget=(mask&4)!=0;
        facts.uiOwned=(mask&8)!=0;facts.hostReady=(mask&16)!=0;facts.modeOwned=(mask&32)!=0;
        facts.exitFence=(mask&64)==0;facts.releaseWindow=(mask&128)==0;
        facts.epoch=(mask&256)?1:0;facts.power=(mask&512)?1:0;facts.edge=(mask&1024)?1:0;
        facts.observedTick=(mask&2048)?100:0;facts.now=(mask&4096)?100:2101;
        allTruthPatterns=allTruthPatterns && ((ymcc::msi::foregroundIsolationFailureBits(facts)==0)==ymcc::msi::foregroundIsolationCurrent(facts));}
    check("all 8192 diagnostic truth patterns agree with the unchanged admission predicate",allTruthPatterns);
    bool hostCaptureEquivalent=true;
    for(unsigned mask=0;mask<256;++mask){facts=Facts{};
        const auto step=[&](unsigned i){return ymcc::msi::foregroundObserveHostGuard(facts,static_cast<HostGuard>(i),(mask&(1u<<i))!=0);};
        const bool accepted=step(0)&&step(1)&&step(2)&&step(3)&&step(4)&&step(5)&&step(6)&&step(7);
        unsigned firstFalse=0;while(firstFalse<8 && (mask&(1u<<firstFalse)))++firstFalse;
        const unsigned expectedObserved=firstFalse==8?255:((1u<<(firstFalse+1))-1);
        hostCaptureEquivalent=hostCaptureEquivalent && accepted==(mask==255) &&
            facts.hostGuardObserved==expectedObserved && facts.hostGuardPassed==(mask&expectedObserved);}
    check("all 256 Host capture patterns preserve order and short circuiting",hostCaptureEquivalent);

    const auto currentJob=[&](){std::lock_guard<std::mutex> lock(g_msiForegroundIsolationMx);const auto& s=g_msiForegroundIsolation;
        return MsiForegroundIsolationJob{s.epoch,s.power,s.edge,s.revision,s.pendingToken,s.observed,s.observedTick,s.mainLocations};};
    reset();prepare();auto job=currentJob();facts=Facts{};const bool admitted=msiForegroundIsolationJobCurrent(job,facts);
    auto receipt=msiForegroundIsolationGateFacts(facts);
    check("actual runtime ready snapshot observes all existing Host prerequisites",admitted && facts.hostGuardObserved==255 && facts.hostGuardPassed==255 && receipt.at("failedGuards").empty());
    const auto ioBeforeFacts=locationReads+inventoryReads+snapshotReads+discoveryReads+xusbReads;
    for(unsigned i=0;i<1000;++i)(void)msiForegroundIsolationGateFacts(facts);
    check("serializing captured facts performs no PnP controller or visibility I/O",ioBeforeFacts==locationReads+inventoryReads+snapshotReads+discoveryReads+xusbReads);
    reset();prepare();g_inputReady=false;job=currentJob();facts=Facts{};
    (void)msiForegroundIsolationJobCurrent(job,facts);receipt=msiForegroundIsolationGateFacts(facts);
    check("first closed input gate does not query or mislabel later Host guards",facts.hostGuardObserved==1 && facts.hostGuardPassed==0 &&
        receipt.at("hostGuards").at("inputReady")==false && receipt.at("hostGuards").at("virtualWriterAllowed").is_null() && receipt.at("hostGuards").at("journalActive").is_null());
    reset();prepare();g_msiForegroundUiOwned=false;job=currentJob();facts=Facts{};
    (void)msiForegroundIsolationJobCurrent(job,facts);receipt=msiForegroundIsolationGateFacts(facts);
    check("unowned UI predicate remains unevaluated not falsely observed out of focus",!facts.uiEligibilityEvaluated && receipt.at("uiEligibility").is_null());
    reset();prepare();g_msiDinputRouteDinputArmed=false;job=currentJob();facts=Facts{};
    (void)msiForegroundIsolationJobCurrent(job,facts);receipt=msiForegroundIsolationGateFacts(facts);
    check("unarmed mode does not reread or falsely clear the return debt",!facts.returnOwedEvaluated && receipt.at("returnOwed").is_null());
    reset();prepare();g_hidHideVisibilityJournal.active=false;job=currentJob();facts=Facts{};
    const bool pendingJournalAllowed=msiForegroundIsolationJobCurrent(job,facts);receipt=msiForegroundIsolationGateFacts(facts);
    check("pending physical journal is distinguished from an eligible foreground",!pendingJournalAllowed && facts.uiOwned &&
        hasFailure(receipt,"host-not-ready") && !hasFailure(receipt,"ui-not-owned-or-not-eligible") &&
        receipt.at("hostGuards").at("journalActive")==false && facts.hostGuardObserved==255 && facts.hostGuardPassed==127);
    run();
    check("diagnostic pending-journal rejection still consumes once without inventory or hides",writes.empty() && inventoryReads==0 && xusbReads==0 && snapshotReads==0 && g_msiForegroundIsolation.pendingToken==0);
    json loggedReceipt=json::object();
    {std::ifstream log(fspath::path(g_yemanccDataDir)/L"native-lifecycle.log");std::string line;
        while(std::getline(log,line)){auto row=json::parse(line,nullptr,false);if(!row.is_discarded() && row.value("event",std::string())=="msi-foreground-isolation" && row.value("state",std::string())=="skipped" && row.contains("gateFacts"))loggedReceipt=row;}}
    check("actual skipped receipt preserves old reason and carries the precise evaluated gate",loggedReceipt.value("reason",std::string())=="stale-or-not-foreground" &&
        loggedReceipt.contains("gateFacts") && loggedReceipt.at("gateFacts").at("hostGuards").at("journalActive")==false &&
        loggedReceipt.at("gateFacts").at("uiOwned")==true && loggedReceipt.value("externalPhysicalSuppression",std::string())=="unverified");
    g_hidHideVisibilityJournal.active=true;const auto afterPendingIo=locationReads+inventoryReads+snapshotReads+discoveryReads+xusbReads;run();
    check("diagnostic metadata never silently requeues a now-ready consumed job",writes.empty() && afterPendingIo==locationReads+inventoryReads+snapshotReads+discoveryReads+xusbReads && queue.size()==1);
    msiForegroundIsolationSetUiOwned(false);msiForegroundIsolationSetUiOwned(true);run();
    check("a real new ownership boundary retains the original successful hide behavior",writes.size()==4);
    reset();prepare();g_hidHideVisibilityJournal.active=false;run();
    check("missing journal retains only the current job without controller I/O",g_msiForegroundIsolation.journalWait.has_value()&&writes.empty()&&inventoryReads+xusbReads+snapshotReads==0);
    g_hidHideVisibilityJournal.active=true;msiForegroundIsolationJournalReady();msiForegroundIsolationJournalReady();
    check("successful primary hide queues one continuation on the same token",queue.size()==2&&g_msiForegroundIsolation.pendingToken!=0);
    run();
    check("journal continuation executes the actual sibling hide with original ownership",writes.size()==4&&g_hidHideVisibilityJournal.hiddenByUs.size()==6&&!g_msiForegroundIsolation.journalWait);
    const auto afterJournalIo=locationReads+inventoryReads+snapshotReads+discoveryReads+xusbReads;
    msiForegroundIsolationJournalReady();run();
    check("journal ready duplicates cannot repeat hide or PnP work",queue.size()==2&&writes.size()==4&&afterJournalIo==locationReads+inventoryReads+snapshotReads+discoveryReads+xusbReads);
    for(unsigned cancellation=0;cancellation<9;++cancellation){
        reset();prepare();g_hidHideVisibilityJournal.active=false;run();
        if(cancellation==0)msiForegroundIsolationSetUiOwned(false);
        if(cancellation==1)g_inputHostEpochMirror=5;
        if(cancellation==2)g_powerGeneration=4;
        if(cancellation==3)g_msiForegroundAnchorRevision.fetch_add(1);
        if(cancellation==4)g_inputHostExitFence=true;
        if(cancellation==5 && g_msiForegroundIsolation.journalWait)g_msiForegroundIsolation.journalWait->observedTick=GetTickCount64()-3000;
        if(cancellation==6)g_realStickTest.virtualTargetEnabled=false;
        if(cancellation==7)ui=false;
        if(cancellation==8)g_msiDinputReturnOwed=false;
        g_hidHideVisibilityJournal.active=true;msiForegroundIsolationJournalReady();run();
        check("late journal callback cannot revive OFF, stale, background or exiting work",writes.empty()&&inventoryReads+xusbReads+snapshotReads==0&&!g_msiForegroundIsolation.journalWait);
    }
    reset();prepare();g_hidHideVisibilityJournal.active=false;run();
    g_hidHideVisibilityJournal.active=true;msiForegroundIsolationJournalReady();g_hidHideVisibilityJournal.active=false;run();
    msiForegroundIsolationJournalReady();
    check("journal continuation losing readiness cannot park or retry a second time",queue.size()==2&&writes.empty()&&!g_msiForegroundIsolation.journalWait&&g_msiForegroundIsolation.pendingToken==0);
    reset();prepare();job=currentJob();g_msiForegroundAnchorRevision.fetch_add(1);facts=Facts{};
    (void)msiForegroundIsolationJobCurrent(job,facts);receipt=msiForegroundIsolationGateFacts(facts);
    check("anchor revision mismatch has its own diagnostic not a fabricated foreground loss",hasFailure(receipt,"anchor-revision-mismatch") && !hasFailure(receipt,"ui-not-owned-or-not-eligible"));


    // F intent preservation must not manufacture managed foreground readiness.
    // The ticket fixture is AFTER verified normal release; it does not test
    // old Start ACK attribution or grant a real Host/source/hardware receipt.
    const auto managedTicketBefore=g_msiExplicitRearmTicket.snapshot();
    reset();g_realStickTest.virtualTargetPersona="steamdeck";
    g_inputHostProcess=nullptr;g_inputHostBoundPersona.clear();g_inputHostPrepared=false;g_inputHostNeutralized=false;
    g_inputHostReleaseQuarantined=false;g_inputHostFaultHoldsStart=false;g_hidHideVisibilityJournal.active=false;g_inputHostStartBlocked=true;
    ymcc::msi::rearm::State managedTicket;managedTicket.sequence=3;managedTicket.offSequence=2;managedTicket.startedSequence=1;
    managedTicket.settingPower=managedTicket.releasePower=3;managedTicket.releaseFloor=2;
    managedTicket.releaseEligible=managedTicket.wanted=true;managedTicket.persona="steamdeck";
    g_msiExplicitRearmTicket.restore(managedTicket);
    bool managedPreserved=false;{std::lock_guard lock(g_inputHostMx);managedPreserved=msiExplicitRearmRestoredLocked();}
    check("F managed readiness: actual preservation flag differential",managedPreserved==ymcc::msi::rearm::buildEnabled&&g_inputHostStartBlocked==!ymcc::msi::rearm::buildEnabled);
    check("F managed readiness: intent callback cannot mint UI/main-source/Host identity",queue.empty()&&locationReads+inventoryReads+snapshotReads+discoveryReads+xusbReads==0&&
        !g_msiForegroundUiOwned&&!g_inputHostProcess&&g_inputHostBoundPersona.empty()&&g_msiForegroundAnchorRevision==0&&g_msiForegroundIsolation.mainRoot.empty());
    prepare();run();
    check("F managed readiness: accepted request alone authorizes no inventory or hide",writes.empty()&&inventoryReads+snapshotReads+discoveryReads+xusbReads==0);
    const auto beforeRepeatedIntentQueue=queue.size();{std::lock_guard lock(g_inputHostMx);(void)msiExplicitRearmRestoredLocked();}run();
    check("F managed readiness: repeated ticket callback cannot retry a consumed foreground job",queue.size()==beforeRepeatedIntentQueue&&writes.empty()&&inventoryReads+snapshotReads+discoveryReads+xusbReads==0);
    auto nextManagedEdge=[&](){msiForegroundIsolationSetUiOwned(false);msiForegroundIsolationSetUiOwned(true);run();};
    g_inputHostProcess=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(1));g_inputHostBoundPersona="steamdeck";nextManagedEdge();
    check("F managed readiness: bound handle without PREPARE cannot hide",writes.empty()&&inventoryReads+snapshotReads+discoveryReads+xusbReads==0);
    g_inputHostPrepared=true;nextManagedEdge();
    check("F managed readiness: PREPARE without neutral receipt cannot hide",writes.empty()&&inventoryReads+snapshotReads+discoveryReads+xusbReads==0);
    g_inputHostNeutralized=true;nextManagedEdge();
    check("F managed readiness: neutral receipt without physical journal cannot hide",writes.empty()&&inventoryReads+snapshotReads+discoveryReads+xusbReads==0);
    const auto consumedReadyQueue=queue.size();g_hidHideVisibilityJournal.active=true;run();
    check("F managed readiness: newly-ready Host cannot silently replay already-consumed job",queue.size()==consumedReadyQueue&&writes.empty()&&inventoryReads+snapshotReads+discoveryReads+xusbReads==0);
    nextManagedEdge();
    check("F managed readiness: true fresh foreground edge retains successful managed isolation",writes.size()==4&&inventoryReads==1);
    check("F managed readiness: aliases stay in same transaction and are not extra controllers",g_hidHideVisibilityJournal.hiddenByUs.size()==6&&g_hidHideVisibilityJournal.physicalSources.size()==1&&
        g_hidHideVisibilityJournal.physicalSources.front().baseContainerDeviceInstanceId==mainRoot&&g_hidHideVisibilityJournal.transactionId=="offline-owned-msi-session");
    const auto isolatedWriteCount=writes.size();const auto isolatedIo=locationReads+inventoryReads+snapshotReads+discoveryReads+xusbReads;run();
    check("F managed readiness: repeated ready worker does not rehide or reenumerate",writes.size()==isolatedWriteCount&&locationReads+inventoryReads+snapshotReads+discoveryReads+xusbReads==isolatedIo);
    g_msiMainSource.generation++;msiForegroundIsolationRememberMain(mainRoot);msiForegroundIsolationObserveNative(source());run();
    check("F managed readiness: actual new source generation gets bounded attempt with aliases preserved",writes.size()==isolatedWriteCount&&g_hidHideVisibilityJournal.hiddenByUs.size()==6&&
        g_hidHideVisibilityJournal.physicalSources.size()==1&&queue.size()==consumedReadyQueue+2);
    g_msiExplicitRearmTicket.restore(managedTicketBefore);

    // Active diagnostic startup/wake must not consume foreground permission.
    // Actual staged observer runtime; ONLY observer/platform hooks are fake.
    reset();prepare();run();
    const auto savedObserverPolicy=g_msiVisibilityWakePolicy.snapshot();
    const auto savedObserverPending=g_msiVisibilityWakePending.load();
    const auto savedObserverActive=g_msiVisibilityObserverActiveForTest;
    const auto savedObserverLaunch=g_msiVisibilityObserverLaunchForTest;
    const auto savedObserverMetadata=g_msiVisibilityObserverMetadataForTest;
    const bool savedObserverDetail=g_domainLogEnabled[1].load();g_domainLogEnabled[1]=true;
    auto controlState=[&](){return std::make_tuple(g_inputHostProcess,g_inputHostEpoch,g_inputHostEpochMirror.load(),
        g_inputHostBoundPersona,g_inputHostPrepared,g_inputHostNeutralized,g_inputHostTransportFault,
        g_inputHostReleaseQuarantined,g_inputHostStartBlocked,g_inputHostFaultHoldsStart.load(),
        g_realStickTest.enabled,g_realStickTest.virtualTargetEnabled,g_realStickTest.virtualTargetPersona,
        g_inputReady.load(),g_powerGeneration.load(),g_powerLifecycle.load(),
        g_msiDinputRouteDinputArmed.load(),g_msiDinputReturnOwed.load(),g_msiForegroundUiOwned.load(),
        g_msiForegroundUiEdge.load(),g_msiForegroundAnchorRevision.load(),g_msiMainSource.generation,
        g_hidHideVisibilityJournal.active,g_hidHideVisibilityJournal.transactionId,
        g_hidHideVisibilityJournal.hiddenByUs,g_hidHideVisibilityJournal.physicalSources.size(),
        g_hidHideVisibilityJournal.physical.baseContainerDeviceInstanceId,
        g_xinputConnectedMask,g_xinputPhysicalSourceMask,g_xinputPhysicalSourceLocked);};
    const auto isolatedControl=controlState();const auto isolatedHidden=hidden;
    const auto isolatedWrites=writes.size(),isolatedQueue=queue.size();
    const auto isolatedReads=locationReads+inventoryReads+snapshotReads+discoveryReads+xusbReads;
    unsigned observerQueries=0,observerLaunches=0,observerMetadata=0;
    auto observerState=ymcc::msi::visibility::wake::Active::Absent;
    g_msiVisibilityObserverActiveForTest=[&](){++observerQueries;return observerState;};
    g_msiVisibilityObserverLaunchForTest=[&](const auto&){++observerLaunches;return true;};
    g_msiVisibilityObserverMetadataForTest=[&](){++observerMetadata;};
    const bool observerBuild=ymcc::msi::visibility::wake::buildEnabled;
    auto unchanged=[&](){return controlState()==isolatedControl&&hidden==isolatedHidden&&writes.size()==isolatedWrites&&queue.size()==isolatedQueue&&
        locationReads+inventoryReads+snapshotReads+discoveryReads+xusbReads==isolatedReads;};
    auto resetObserver=[&](){g_msiVisibilityWakePolicy.restore(ymcc::msi::visibility::wake::State{});g_msiVisibilityWakePending=0;
        observerQueries=observerLaunches=observerMetadata=0;observerState=ymcc::msi::visibility::wake::Active::Absent;};
    auto armCurrentWake=[&](){msiVisibilityObserverNoteResume(g_powerGeneration.load());msiVisibilityObserverWakeReady(g_powerGeneration.load());};
    resetObserver();msiVisibilityObserverStartupDiagnostic();msiVisibilityObserverStartupDiagnostic();
    check("observer joint: real diagnostic startup is once with no foreground/control mutation",observerLaunches==unsigned(observerBuild)&&observerQueries==unsigned(observerBuild)&&unchanged());
    resetObserver();armCurrentWake();msiVisibilityObserverWakeMaintenance();
    check("observer joint: actual wake reader launch cannot hide/rearm/change source tuple",observerLaunches==unsigned(observerBuild)&&g_msiVisibilityWakePending.load()==0&&unchanged());
    const auto launchesOnce=observerLaunches,queriesOnce=observerQueries,metadataOnce=observerMetadata;
    armCurrentWake();msiVisibilityObserverWakeMaintenance();
    check("observer joint: repeated same-power notifications cannot respawn or rediscover physical input",observerLaunches==launchesOnce&&observerQueries==queriesOnce&&observerMetadata==metadataOnce&&unchanged());
    resetObserver();observerState=ymcc::msi::visibility::wake::Active::Running;armCurrentWake();msiVisibilityObserverWakeMaintenance();msiVisibilityObserverWakeMaintenance();
    check("observer joint: live old reader only waits without changing isolated aliases",observerLaunches==0&&g_msiVisibilityWakePending.load()==(observerBuild?g_powerGeneration.load():0)&&unchanged());
    observerState=ymcc::msi::visibility::wake::Active::Unknown;msiVisibilityObserverWakeMaintenance();
    check("observer joint: unknown reader ends diagnostic pending without unhide or replacement",observerLaunches==0&&g_msiVisibilityWakePending.load()==0&&unchanged());
    resetObserver();g_msiVisibilityObserverActiveForTest=[&]()->ymcc::msi::visibility::wake::Active{++observerQueries;throw std::runtime_error("offline-reader-query-failure");};
    armCurrentWake();msiVisibilityObserverWakeMaintenance();msiVisibilityObserverWakeMaintenance();
    check("observer joint: reader-query exception is bounded and never rolls back control",observerQueries==unsigned(observerBuild)&&observerLaunches==0&&g_msiVisibilityWakePending.load()==0&&unchanged());
    g_msiVisibilityWakePolicy.restore(savedObserverPolicy);g_msiVisibilityWakePending=savedObserverPending;
    g_msiVisibilityObserverActiveForTest=savedObserverActive;g_msiVisibilityObserverLaunchForTest=savedObserverLaunch;
    g_msiVisibilityObserverMetadataForTest=savedObserverMetadata;g_domainLogEnabled[1]=savedObserverDetail;

    const auto nativeOnly=msiNativeForegroundRegression();cases+=nativeOnly.first;failures+=nativeOnly.second;
    const auto rearmBoundary=msiExplicitRearmForegroundBoundaryRegression();cases+=rearmBoundary.first;failures+=rearmBoundary.second;
    const auto helperFacts=msiHelperDiagnosticAttributionRegression();cases+=helperFacts.first;failures+=helperFacts.second;
    g_inputHostProcess=nullptr;
    std::cout<<"MSI_FOREGROUND_NATIVE cases="<<cases<<" failures="<<failures<<" realHardwareAccess=false\n";
    std::ofstream report(fspath::path(g_yemanccDataDir)/L"msi-foreground-native-summary.json");
    report<<json({{"cases",cases},{"failures",failures},{"actualProductionTu",true},{"realHardwareAccess",false},{"testIoOnly",true}}).dump(2);
    return failures?3:0;
}
