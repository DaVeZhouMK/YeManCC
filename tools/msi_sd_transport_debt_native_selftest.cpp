// R6 transport-health versus SD endpoint ownership sidecar.
// Actual Stop/Reconcile via existing fault seams; never starts a real Host.
#ifndef YMCC_FAULT_INJECT
#error This sidecar is forbidden without YMCC_FAULT_INJECT.
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <setupapi.h>
#include <objbase.h>
#include <iostream>
// Zero-expected tripwires are NOT alternate successful API models. Any attempt
// is counted, denied, and fails the test. They make a faulty candidate safe to run
// and explicitly prove no real SetupDi on missing hook/no strong process kill.
namespace debtTripwire {
static unsigned registry=0,pnp=0,deviceOpen=0,spawn=0,kill=0,com=0;
static unsigned total(){return registry+pnp+deviceOpen+spawn+kill+com;}
static LSTATUS WINAPI regOpen(HKEY,LPCWSTR,DWORD,REGSAM,PHKEY){++registry;return ERROR_ACCESS_DENIED;}
static LSTATUS WINAPI regQuery(HKEY,LPCWSTR,LPDWORD,LPDWORD,LPBYTE,LPDWORD){++registry;return ERROR_ACCESS_DENIED;}
static HDEVINFO WINAPI getClass(const GUID*,PCWSTR,HWND,DWORD){++pnp;SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
static HANDLE WINAPI openFile(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE){++deviceOpen;SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
static BOOL WINAPI createProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){++spawn;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static BOOL WINAPI terminate(HANDLE,UINT){++kill;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static HRESULT WINAPI activate(REFCLSID,LPUNKNOWN,DWORD,REFIID,LPVOID*){++com;return E_ACCESSDENIED;}
}
#define RegOpenKeyExW debtTripwire::regOpen
#define RegQueryValueExW debtTripwire::regQuery
#define SetupDiGetClassDevsW debtTripwire::getClass
#define CreateFileW debtTripwire::openFile
#define CreateProcessW debtTripwire::createProcess
#define TerminateProcess debtTripwire::terminate
#define CoCreateInstance debtTripwire::activate
#include "../native/main.cpp"
#undef RegOpenKeyExW
#undef RegQueryValueExW
#undef SetupDiGetClassDevsW
#undef CreateFileW
#undef CreateProcessW
#undef TerminateProcess
#undef CoCreateInstance

namespace transportDebtTest {
static constexpr const char* run="offline-r6-run";
static constexpr uint64_t epoch=1;
static const std::string usb="USB\\VID_28DE&PID_1205\\OFFLINE_R6";
static const std::string hid="HID\\VID_28DE&PID_1205&MI_00\\OFFLINE_R6";
static unsigned checks=0,failed=0,caseIndex=0,metadataCalls=0,pnpHookCalls=0;
static json cases=json::array();
static fspath::path root;
static void check(const std::string& name,bool ok){++checks;if(!ok)++failed;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';}
static unsigned count(const std::string& exact){return unsigned(std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),exact));}
static unsigned restores(){unsigned n=0;for(const auto& step:g_startEntrySeam.trace)n+=step.rfind("hidhide-restore:",0)==0;return n;}
static json pinReceipt(const std::string& owner=run,uint64_t generation=epoch){return json{{"msiSteamDeckReleaseEvidence",json{
    {"version",1},{"required",true},{"runId",owner},{"epoch",generation},{"persona","steamdeck"},
    {"profileIdentity","steam-deck-composite"},{"probeSucceeded",true},{"absentVerified",false},
    {"ownedTargets",json::array({usb,hid})},{"ownedTargetCount",2},{"remainingOwnedTargetCount",2}}}};}
static ymcc::msi::sdRelease1726::Session session(const std::string& owner=run,uint64_t generation=epoch){
    ymcc::msi::sdRelease1726::Session s;s.hello(json{{"msiSteamDeckReleaseFenceVersion",1}});
    check("fixture/begin-current-SD",s.begin(true,"steamdeck",owner,generation));
    check("fixture/strict-two-target-pin",s.acceptPrepare(pinReceipt(owner,generation))&&s.currentPinned(owner,generation));
    return s;
}
static void seedIdentity(bool msi=true){g_machineIdentity={};g_machineIdentity.attempted=g_machineIdentity.resolved=true;
    g_machineIdentity.family=msi?YmccFamily::MsiClaw:YmccFamily::AsusRogAllyX;
    g_machineIdentity.manufacturer=msi?"MICRO-STAR INTERNATIONAL CO., LTD.":"ASUSTEK COMPUTER INC.";
    g_machineIdentity.product=msi?"MS-1T41":"ROG Ally X";}
static fspath::path reset(const char* name,bool msi=true,const char* persona="steamdeck"){
    // Only our own synthetic events are ever disposed between independent cases.
    if(g_inputHostProcess){CloseHandle(g_inputHostProcess);g_inputHostProcess=nullptr;}
    g_nativeLifecycleLog.close();g_nativeDetailLog.close();g_nativeLifecycleLog.clear();g_nativeDetailLog.clear();
    const auto directory=root/(std::to_wstring(++caseIndex)+L"-"+U2W(name));fspath::create_directories(directory);
    g_appDataDir=directory.wstring();g_yemanccDataDir=directory.wstring();
    seedIdentity(msi);g_startEntrySeam=YmccStartEntrySeam{};g_startEntrySeam.active=true;g_startEntrySeam.hostExitWait=WAIT_OBJECT_0;
    {std::lock_guard<std::mutex> lock(g_inputHostMx);inputHostClearLocked();}
    g_inputHostReleaseQuarantined=false;g_inputHostStartBlocked=false;g_inputHostExitFence=false;g_inputHostFaultHoldsStart=false;
    g_inputHostPowerAdmissionRevoked=false;g_inputHostActive=false;g_inputHostPrepared=false;g_inputHostNeutralized=false;
    g_inputHostHello=true;g_inputHostTransportFault=true;g_inputHostPipeRetired=false;
    g_inputHostRunId=run;g_inputHostEpoch=epoch;g_inputHostPipeName=L"offline-r6-no-real-pipe";g_inputHostStartupNonce="offline-r6-nonce";
    g_inputHostBoundPersona=persona;
    {std::lock_guard<std::mutex> lock(g_inputHostPersonaMx);g_realStickTest.virtualTargetPersona=persona;}
    g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=true;
    g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_exitRequested=false;
    g_hidHideVisibilityJournal={};g_wakeHoldPhysicalUnhide=false;g_msiM12RemapDone=true;g_hidHideLastHealTick=GetTickCount64();
    g_msiDinputRouteDinputArmed=false;g_msiDinputRouteConfirmed=false;g_msiDinputReturnOwed=false;g_msiDinputSourceValid=false;
    g_xinputConnectedMask=0;g_xinputPhysicalSourceMask=0;g_xinputVirtualEndpointMask=0;g_xinputSlotUnknownMask=0;
    g_xinputPhysicalSourceLocked=true; // Deliberately makes the old XInput-zero shortcut look absent.
    g_msiSteamDeckReleaseFence.clear();g_msiSteamDeckReleaseDebt.clear();g_msiSdOwnedPnpSnapshotForTest={};
    g_domainLogEnabled[1]=true;
    return directory;
}
static json logs(const fspath::path& directory){g_nativeLifecycleLog.flush();g_nativeDetailLog.flush();json rows=json::array();
    for(const auto* file:{L"native-lifecycle.log",L"native-detail.log"}){std::ifstream in(directory/file);std::string line;
        while(std::getline(in,line)){auto row=json::parse(line,nullptr,false);if(row.is_object())rows.push_back(std::move(row));}}
    return rows;}
static void record(const char* name,const char* scope,unsigned before,const fspath::path& dir,json facts=json::object(),bool negative=false){
    check(std::string(name)+"/no-blocked-real-API-attempt",debtTripwire::total()==0);
    check(std::string(name)+"/no-new-target-or-runtime",count("create-process")==0&&count("pipe:PREPARE_TARGET")==0&&!g_inputCaptureThread&&!g_sensorThread&&!g_msiDinput.di&&!g_msiDinput.pad);
    facts["name"]=name;facts["scope"]=scope;facts["passed"]=failed==before;facts["nativeNegative"]=negative;
    facts["trace"]=g_startEntrySeam.trace;facts["logs"]=logs(dir);facts["quarantined"]=g_inputHostReleaseQuarantined;
    facts["debtRequired"]=g_msiSteamDeckReleaseDebt.required();facts["restoreSeamCalls"]=restores();facts["directory"]=W2U(dir.wstring());
    cases.push_back(std::move(facts));
}
static void faultStop(const char* name,bool noHelloTimeout){
    const unsigned before=failed;const auto dir=reset(name);g_msiSteamDeckReleaseFence=session();
    g_inputHostPrepared=false;g_inputHostTransportFault=true;g_inputHostHello=!noHelloTimeout;
    g_inputHostProcess=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(noHelloTimeout)g_startEntrySeam.hostExitWait=WAIT_TIMEOUT;
    const bool currentPin=g_msiSteamDeckReleaseFence.currentPinned(run,epoch);
    check(std::string(name)+"/health-false-does-not-delete-pin",currentPin&&!g_inputHostPrepared);
    {std::lock_guard<std::mutex> lock(g_inputHostMx);inputHostStopLocked("transport-fault");}
    check(std::string(name)+"/SD-debt-survives-clear-session",g_msiSteamDeckReleaseDebt.required()&&g_msiSteamDeckReleaseDebt.currentPinned(run,epoch)&&!g_msiSteamDeckReleaseFence.required());
    check(std::string(name)+"/quarantine-and-start-block-retained",g_inputHostReleaseQuarantined&&g_inputHostStartBlocked);
    check(std::string(name)+"/no-physical-restore",restores()==0);
    check(std::string(name)+"/no-ordered-RPC-on-fault",count("pipe:QUIESCE")==0&&count("pipe:RELEASE_TARGET")==0&&count("pipe:SHUTDOWN")==0);
    check(std::string(name)+"/no-root-or-child-strong-kill",debtTripwire::kill==0);
    check(std::string(name)+"/bounded-host-wait-ownership",bool(g_inputHostProcess)==noHelloTimeout);
    const auto rows=logs(dir);bool healthRecorded=false;
    for(const auto& row:rows)if(row.value("event","")=="input-host-stop-request")healthRecorded=
        row.contains("hadPreparedTarget")&&row.at("hadPreparedTarget")==false&&row.value("targetMayExist",false)&&row.value("msiSdPinnedOwnership",false)&&row.value("msiSdReleaseRequired",false);
    check(std::string(name)+"/health-flag-not-forged-in-stop-log",healthRecorded);
    record(name,"actual inputHostStopLocked; existing transport/exit/hide seams; no real Host",before,dir,
        {{"preparedHealth",false},{"currentPinBefore",currentPin},{"noHelloTimeout",noHelloTimeout},{"killAttempts",debtTripwire::kill}},true);
}
static void reconcileCase(const char* name,bool complete,std::set<std::string> present,bool missingHook=false,
                          bool wakeHold=false,bool restoreOk=true,bool liveHost=false,bool expectCleared=false){
    const unsigned before=failed;const auto dir=reset(name);g_msiSteamDeckReleaseDebt=session();
    g_inputHostReleaseQuarantined=true;g_inputHostStartBlocked=true;g_wakeHoldPhysicalUnhide=wakeHold;
    g_startEntrySeam.hidHideRestoreResult=restoreOk;
    if(liveHost)g_inputHostProcess=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    const auto hooksBefore=pnpHookCalls;
    if(!missingHook)g_msiSdOwnedPnpSnapshotForTest=[complete,present](){++pnpHookCalls;return std::make_pair(complete,present);};
    inputHostReconcileQuarantineOnLifecycleThread();
    const unsigned hookCalls=pnpHookCalls-hooksBefore;
    const bool allOwnedAbsent=complete&&!present.count(usb)&&!present.count(hid);
    const unsigned expectedRestores=allOwnedAbsent&&!missingHook&&!wakeHold&&!liveHost?1u:0u;
    check(std::string(name)+"/native-hook-admission",hookCalls==unsigned(!missingHook&&!liveHost));
    check(std::string(name)+"/no-real-SetupDi-even-with-missing-hook",debtTripwire::pnp==0);
    check(std::string(name)+"/XInput-zero-never-substitutes-owned-absence",g_xinputConnectedMask==0&&(!expectCleared?g_inputHostReleaseQuarantined:!g_inputHostReleaseQuarantined));
    check(std::string(name)+"/debt-cleared-only-after-exact-absence-and-restore",g_msiSteamDeckReleaseDebt.required()!=expectCleared);
    check(std::string(name)+"/existing-selected-restore-gate",restores()==expectedRestores);
    check(std::string(name)+"/new-writer-remains-blocked",g_inputHostStartBlocked);
    if(!expectCleared)check(std::string(name)+"/exact-old-owned-ID-debt-preserved",g_msiSteamDeckReleaseDebt.ownedTargets()==std::set<std::string>({usb,hid}));
    if(!complete||missingHook||liveHost||present.count(usb)||present.count(hid))check(std::string(name)+"/no-absence-proof-created",!g_msiSteamDeckReleaseDebt.verified(run,epoch));
    record(name,"actual lifecycle quarantine Reconcile + actual owned-removal helper; fake PnP snapshot; selected restore seam only",before,dir,
        {{"complete",complete},{"present",present},{"missingHook",missingHook},{"wakeHold",wakeHold},{"restoreResult",restoreOk},
         {"liveHost",liveHost},{"hookCalls",hookCalls},{"expectedCleared",expectCleared},{"actualHardwareRestores",0}},!expectCleared);
}
static void boundaryStop(const char* name,bool oldRun,bool oldEpoch,bool msi,const char* persona,bool healthy){
    const unsigned before=failed;const auto dir=reset(name,msi,persona);
    const bool stale=oldRun||oldEpoch;
    if(stale)g_msiSteamDeckReleaseFence=session(oldRun?"older-run":run,oldEpoch?2:epoch);
    else {g_msiSteamDeckReleaseFence.hello(json{{"msiSteamDeckReleaseFenceVersion",1}});
        check(std::string(name)+"/ordinary-persona-not-SD-required",g_msiSteamDeckReleaseFence.begin(msi,persona,run,epoch)&&!g_msiSteamDeckReleaseFence.required());}
    g_inputHostPrepared=healthy;g_inputHostTransportFault=!healthy;g_inputHostHello=true;
    g_inputHostProcess=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    check(std::string(name)+"/no-current-SD-pin-promotion",!g_msiSteamDeckReleaseFence.currentPinned(run,epoch));
    {std::lock_guard<std::mutex> lock(g_inputHostMx);inputHostStopLocked(healthy?"coordinator-stop":"transport-fault");}
    check(std::string(name)+"/no-new-MSI-SD-debt",!g_msiSteamDeckReleaseDebt.required());
    check(std::string(name)+"/existing-ordinary-restore-path-retained",restores()>=1&&!g_inputHostReleaseQuarantined);
    if(healthy){auto index=[&](const char* step){const auto& tr=g_startEntrySeam.trace;auto it=std::find(tr.begin(),tr.end(),step);return it==tr.end()?-1:int(it-tr.begin());};
        const int q=index("pipe:QUIESCE"),n=index("pipe:SUBMIT_NEUTRAL"),r=index("pipe:RELEASE_TARGET"),s=index("pipe:SHUTDOWN");
        check(std::string(name)+"/healthy-original-command-order",q>=0&&q<n&&n<r&&r<s);
    }
    record(name,"actual Stop ownership boundary/healthy non-SD order; stale pin is not proof of current ownership",before,dir,
        {{"staleRun",oldRun},{"staleEpoch",oldEpoch},{"knownMsi",msi},{"persona",persona},{"healthy",healthy}});
}
static void healthySdPolicy(){const char* name="healthy-SD-exact-proof-policy-only";const unsigned before=failed;const auto dir=reset(name);
    auto s=session();auto receipt=pinReceipt();auto& e=receipt["msiSteamDeckReleaseEvidence"];e["absentVerified"]=true;e["remainingOwnedTargetCount"]=0;
    check("healthy-SD/current-pin-exact-tuple",s.currentPinned(run,epoch)&&!s.currentPinned("old-run",epoch)&&!s.currentPinned(run,2)&&!s.currentPinned("",epoch)&&!s.currentPinned(run,0));
    check("healthy-SD/matching-absence-remains-accepted",s.acceptRelease(receipt)&&s.verified(run,epoch));
    check("healthy-SD/no-cross-tuple-release-proof",!s.verified("old-run",epoch)&&!s.verified(run,2));
    record(name,"actual Session currentPinned/acceptRelease only; NOT healthy SD whole Stop/RPC handler",before,dir,{{"wholeHealthySdHandlerExecuted",false}});
}
static void tests(){
    faultStop("prepared-false-current-pin-transport-fault",false);
    faultStop("current-pin-no-HELLO-timeout-no-strong-kill",true);
    reconcileCase("XInput-zero-owned-USB-still-present",true,{usb});
    reconcileCase("XInput-zero-owned-HID-child-still-present",true,{hid});
    reconcileCase("PnP-incomplete-empty-is-not-absence",false,{});
    reconcileCase("PnP-hook-missing-fail-closed-no-SetupDi",false,{},true);
    reconcileCase("complete-absence-wake-hold-keeps-debt",true,{},false,true);
    reconcileCase("complete-absence-restore-failure-keeps-debt",true,{},false,false,false);
    reconcileCase("live-Host-prevents-reconcile-probe",true,{},false,false,true,true);
    reconcileCase("all-owned-absent-unrelated-target-present-clears",true,{"USB\\VID_28DE&PID_1205\\UNRELATED"},false,false,true,false,true);
    boundaryStop("stale-run-pin-not-current-ownership",true,false,true,"steamdeck",false);
    boundaryStop("stale-epoch-pin-not-current-ownership",false,true,true,"steamdeck",false);
    boundaryStop("non-MSI-SD-no-expanded-debt",false,false,false,"steamdeck",false);
    boundaryStop("MSI-non-SD-no-expanded-debt",false,false,true,"dualshock4",false);
    boundaryStop("healthy-non-SD-original-command-order",false,false,true,"dualshock4",true);
    healthySdPolicy();
}
}
int wmain(int argc,wchar_t** argv){if(argc!=2)return 2;using namespace transportDebtTest;
    const auto allowed=fspath::path(L"G:\\YeManCC-Work\\Build\\Validation\\MSI-SD-Lifecycle-R6-20261013\\transport-debt-native");
    root=fspath::absolute(argv[1]).lexically_normal();auto value=root.wstring(),base=allowed.wstring();
    for(auto& c:value)c=towlower(c);for(auto& c:base)c=towlower(c);
    if(value!=base&&value.rfind(base+L"\\",0)!=0){std::cerr<<"Output outside authorized R6 root\n";return 2;}
    seedIdentity();fspath::create_directories(root);
    if(g_inputHostProcess||g_inputCaptureThread||g_sensorThread||g_msiDinput.di||g_msiDinput.pad){std::cerr<<"Unrelated runtime owner present\n";return 3;}
    g_msiControllerTraceInventoryForTest=[](){++metadataCalls;return json{{"known",false},{"scope","offline-no-device-query"}};};
    g_msiControllerOfficialStackForTest=[](){++metadataCalls;return json{{"modeWriterProven",false},{"scope","offline-no-service-WMI-query"}};};
    g_msiControllerModeProbeIoForTest=[](){++metadataCalls;return json{{"scope","offline-no-HID-read-write"}};};
    try {tests();}catch(const std::exception& e){check("unexpected-sidecar-exception",false);std::cerr<<e.what()<<'\n';}
    check("all-real-API-tripwires-zero",debtTripwire::total()==0);
    check("no-strong-process-kill-attempt",debtTripwire::kill==0);
    check("no-M12-preparation-entry",g_msiStartPreparationEntriesForTest==0);
    unsigned negatives=0;for(const auto& row:cases)negatives+=row.value("nativeNegative",false);
    check("at-least-eight-actual-native-negatives",negatives>=8);
    json report={{"scope","Actual inputHostStopLocked and lifecycle Reconcile under existing seams/fake PnP; healthy SD positive restricted to actual Session policy"},
        {"caseCount",cases.size()},{"nativeNegativeCaseCount",negatives},{"checkCount",checks},{"failed",failed},{"passed",failed==0},
        {"realHardwareAPICalls",0},{"identityPreseeded",g_machineIdentity.attempted&&g_machineIdentity.resolved},
        {"tripwireAttempts",json{{"registry",debtTripwire::registry},{"SetupDi",debtTripwire::pnp},{"CreateFile",debtTripwire::deviceOpen},
            {"CreateProcess",debtTripwire::spawn},{"TerminateProcess",debtTripwire::kill},{"CoCreateInstance",debtTripwire::com}}},
        {"tripwireScope","Listed source-call entrypoints only; denied attempts fail checks; not global DLL import interception"},
        {"fakePnpHookCalls",pnpHookCalls},{"syntheticMetadataCalls",metadataCalls},{"realHostProcesses",0},{"newTargetCreates",0},
        {"healthySdWholeHandlerExecuted",false},{"realSelectedRestoreExecuted",false},{"globalUnhideAdded",false},{"cases",cases}};
    std::ofstream output(root/L"msi-sd-transport-debt-native-selftest.json");output<<report.dump(2)<<'\n';output.close();
    if(g_inputHostProcess){CloseHandle(g_inputHostProcess);g_inputHostProcess=nullptr;}
    std::cout<<"MSI_SD_TRANSPORT_DEBT_NATIVE_SELFTEST cases="<<cases.size()<<" nativeNegatives="<<negatives<<" checks="<<checks<<" failed="<<failed<<" realHardwareAPICalls=0\n";
    return failed==0&&output.good()?0:3;
}