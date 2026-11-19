// Only test seams and observations live here. Product bodies below are exact
// generated slices from the fixed main/Host/typed source snapshot.
using json=Json;
std::atomic<bool> g_exitRequested{false},g_poolCancel{false};
struct NativeFixtureMock {bool isolated=false;} g_aiFanMockSession;
enum class PowerLifecycle:uint8_t {Ready,Suspending,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned long long> g_powerGeneration{7};
static std::atomic<bool> g_hardwareWriteGate{true};
static ymcc::sleeppower::Dispatch g_sleepPowerDispatch;
static std::atomic<uint64_t> g_sharedPowerPublicationRevision{10},g_sharedPowerSuspendSerial{2};
#include "sleep_cancel_ready.h"
static ymcc::sleeppower::CancellationReady g_sgCancellationReady;
static std::atomic<ULONGLONG> g_sgLatestSleepEntryFileTime{0};
#include "power-body.inc"

static Child* nativeOwnedTarget=nullptr;
static HANDLE nativeOwnedTargetStop=nullptr;
static std::uint64_t nativeTargetBirth=0;
static std::wstring nativeTargetRoot;
static unsigned nativeLookups=0,nativeShellAttempts=0,nativeInfoRequests=0,nativeSessionReads=0;
static bool nativeAfterDrain=false,nativeTargetReturned=false,nativeFixedCommand=false;
static std::uint64_t nativeIdentityProcessEpoch=11,nativeIdentityAccountEpoch=13,nativeIdentityRevision=1;
static bool nativeIdentityKnown=true;static int nativeIdentityPresence=1;
static unsigned nativeIdentityReads=0;
static unsigned nativeSourceReads=0;
static bool nativeSourceFaultApplied=false;
static bool nativeSourceMode(){return providerMode.rfind(L"native-compose-source-",0)==0;}
static bool nativeIdentityMode(){return providerMode.rfind(L"native-compose-identity-",0)==0;}
static void nativeObserveIdentityClaim() noexcept {
    if(providerMode==L"native-compose-identity-after-claim")++nativeIdentityProcessEpoch;
}
static Json nativeTargetReceipt;
static std::unique_ptr<Child> nativeOtherTarget;
static si::Handle nativeOtherStop;
static DWORD nativeOtherPid=0;
static bool nativeOtherAliveAtHostEnd=false,nativeOtherJoined=false;
static unsigned nativeExpectedTargetReturns=3;
static Json nativePresenceObservations=Json::array();
static bool nativePresenceMode(){return providerMode.rfind(L"native-compose-presence-",0)==0;}
static bool nativeReplacementMode(){return providerMode==L"native-compose-presence-replacement-preflight"||providerMode==L"native-compose-presence-replacement-after";}
static void nativeStopTarget(){
    need(nativeOwnedTarget&&nativeOwnedTarget->process.value&&nativeOwnedTargetStop,"owned target lifetime");
    need(SetEvent(nativeOwnedTargetStop)!=FALSE,"owned target stop event");
    if(!nativeOwnedTarget->joined)nativeOwnedTarget->join(3000);
    need(nativeOwnedTarget->exit==0,"owned target returned normally");nativeTargetReturned=true;
}
static void revokeNativeAfterClaim() noexcept {g_hardwareWriteGate.store(false,std::memory_order_release);}
static sa::Request nativeExpectedAction;
static bool nativeExpectedActionKnown=false;
static unsigned nativeAcceptedFields=0,nativeClaimedFields=0,nativeUnboundActionCAS=0;
static void nativeObserveActionCAS(volatile LONG* target,LONG exchange,LONG comparand,LONG result) noexcept {
    if(exchange!=1||comparand!=0||result!=0)return;
    if(!nativeExpectedActionKnown){++nativeUnboundActionCAS;return;}
    for(const auto& entry:nativeObservedViews){
        if(entry.second!=sizeof(sa::Wire))continue;
        const auto* wire=static_cast<const sa::Wire*>(entry.first);
        const auto base=reinterpret_cast<UINT_PTR>(entry.first),address=reinterpret_cast<UINT_PTR>(target);
        if(address!=base+offsetof(sa::Wire,accepted)&&address!=base+offsetof(sa::Wire,claimed))continue;
        if(!sa::matches(wire->request,nativeExpectedAction)){++nativeUnboundActionCAS;return;}
        if(address==base+offsetof(sa::Wire,accepted))++nativeAcceptedFields;
        else {++nativeClaimedFields;if(nativeInjectAfterClaim)revokeNativeAfterClaim();nativeObserveIdentityClaim();}
        return;
    }
    ++nativeUnboundActionCAS;
}
static Json nativeResourceReceipt(){
    return {{"recovered",nativeObservedResourcesReturned()},{"errors",nativeResourceErrors},
        {"liveHandles",nativeObservedHandles.size()},{"baselineHandles",nativeBaselineHandles.size()},
        {"liveViews",nativeObservedViews.size()},{"baselineViews",nativeBaselineViews.size()},
        {"liveWindows",nativeObservedWindows.size()},{"baselineWindows",nativeBaselineWindows.size()},
        {"handleAcquires",nativeHandleAcquires},{"handleReleases",nativeHandleReleases},
        {"viewAcquires",nativeViewAcquires},{"viewReleases",nativeViewReleases},
        {"windowAcquires",nativeWindowAcquires},{"windowReleases",nativeWindowReleases}};
}
namespace ymcc::steamsession {
static HANDLE fixtureVerifiedProcess(DWORD pid,const std::wstring& root,ULONGLONG* born){
    ++nativeLookups;
    if(providerMode==L"native-compose-identity-lookup-epoch")++nativeIdentityProcessEpoch;
    if(providerMode==L"native-compose-target-exited")nativeStopTarget();
    // The real verifier performs OpenProcess/path/birth/held-process checks.
    return verifiedProcess(pid,root,born);
}
}
struct NativeFixtureObserver {
    using IdentitySnapshot=ymcc::steamsession::IdentitySnapshot;
    ymcc::steamsession::Session value;
    auto sessionSnapshot(){++nativeSessionReads;return value;}
    int steamPresence(){return 1;}
    auto sourceRead(){++nativeSourceReads;return fixtureSource().read();}
    std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> identitySnapshot() const {
        ++nativeIdentityReads;if(!nativeIdentityKnown)return {};
        auto facts=std::make_shared<ymcc::steamsession::IdentitySnapshot>();facts->session=value;
        facts->presence=nativeIdentityPresence;facts->registeredPid=nativeRegistryActivePid;facts->registeredPidReadError=ERROR_SUCCESS;facts->processEpoch=nativeIdentityProcessEpoch;
        facts->accountEpoch=nativeIdentityAccountEpoch;facts->revision=nativeIdentityRevision;return facts;
    }
#include "identity-comparator.inc"
}g_steamSettingsObserver;
static BOOL nativeFixtureShell(SHELLEXECUTEINFOW* info){
    ++nativeShellAttempts;
    nativeFixedCommand=info&&info->lpFile&&info->lpParameters&&info->lpDirectory&&
        _wcsicmp(info->lpFile,(nativeTargetRoot+L"\\steam.exe").c_str())==0&&
        std::wstring(info->lpParameters)==L"-shutdown"&&info->nShow==SW_HIDE&&
        std::wstring(info->lpDirectory)==nativeTargetRoot;
    need(nativeFixedCommand,"fixed target command escaped inert boundary");
    if(providerMode==L"native-compose-shell-reject")return FALSE;
    ++effects;nativeStopTarget();
    if(providerMode==L"native-compose-identity-post-submit"){nativeIdentityPresence=0;++nativeIdentityProcessEpoch;}
    return TRUE;
}
#define verifiedProcess fixtureVerifiedProcess
#define ShellExecuteExW nativeFixtureShell
#define CloseHandle nativeObserveClose
#define OpenProcessToken nativeObserveOpenProcessToken
namespace ymcc::steamui {
#include "typed-client.inc"
}
#undef ShellExecuteExW
#undef OpenProcessToken
#undef CloseHandle
#undef verifiedProcess
static bool poolSubmit(std::function<void()> work){
    ++submissions;pending.push_back(std::move(work));drainAt=GetTickCount64()+30;return true;
}
// This fixture is not a registered Gaming Home and never dispatches HomeStart.
static LRESULT serveHomeSteamAction(HWND,HWND,const COPYDATASTRUCT*){return FALSE;}
#include "library-handler.inc"

// Actual Host helper uses the real shared information and action clients.
// Only its calls are observed; no outcome or currentness is substituted.
std::atomic<bool> g_shuttingDown{false},g_cancelRequested{false};
static unsigned nativeClientActionCalls=0;
static sa::Outcome nativeClientActionOutcome=sa::Outcome::Unknown;
namespace ymcc::steamaction {
static Outcome fixtureHostShutdown(DWORD pid,std::uint64_t born,ULONGLONG deadline,
        std::uint64_t gen,std::uint64_t seq,std::uint64_t serial,const std::function<bool()>& current){
    ++nativeClientActionCalls;
    nativeClientActionOutcome=shutdown(pid,born,deadline,gen,seq,serial,current);
    return nativeClientActionOutcome;
}
}
#define shutdown fixtureHostShutdown
#include "host-body.inc"
#undef shutdown

static bool nativeCompositionMode(const std::wstring& mode){return mode.rfind(L"native-compose-",0)==0;}
static int nativeTargetMain(const std::wstring& name){
    si::Handle stop(OpenEventW(SYNCHRONIZE,FALSE,name.c_str()));need(stop.value,"owned target stop open");
    need(WaitForSingleObject(stop.value,15000)==WAIT_OBJECT_0,"owned target bounded return");return 0;
}
static int nativeCompositionClient(const std::wstring& mode,DWORD target,std::uint64_t born,const fs::path& output){
    nativeObserveResourceBaseline();
    const auto began=GetTickCount64();bool returned=false;std::string error;
    const json expected{{"pid",target},{"birth",std::to_string(born)},{"powerGeneration","7"},{"powerSequence","5"},{"powerSerial","2"}};
    const json arguments{{"steamClient",expected},{"_nativeSteamDeadlineTick",GetTickCount64()+5000}};
    try{closeSteamForShortcutCommit(arguments);returned=true;}catch(const std::exception& e){error=e.what();}
    writeNew(output,{{"hostReturned",returned},{"hostErrorPresent",!error.empty()},{"pid",GetCurrentProcessId()},
        {"actionCalls",nativeClientActionCalls},{"actionSends",observedActionSends},{"outcome",LONG(nativeClientActionOutcome)},
        {"resources",nativeResourceReceipt()},{"allTransportSends",observedSends},{"elapsedMs",GetTickCount64()-began},{"productExecuted",false}});
    return 0;
}
static void nativeCompositionBegin(const fs::path& root,const Desktop& desktop,Child& target,si::Handle& stop){
    nativeInjectAfterClaim=providerMode==L"native-compose-after-claim-gate";
    nativeIdentityKnown=providerMode!=L"native-compose-identity-unknown";
    for(unsigned i=0;i<5;++i){const auto receipt=g_sleepPowerDispatch.window(PBT_APMRESUMEAUTOMATIC,GetTickCount64());need(bool(receipt),"owned power receipt");g_sleepPowerDispatch.complete(receipt,GetTickCount64());}
    need(g_sleepPowerDispatch.observedSequence()==5&&!g_sleepPowerDispatch.pendingSuspend(),"actual initial power dispatch");
    const auto targetDir=root/L"SteamOwned";need(fs::create_directory(targetDir),"owned target directory");
    need(fs::copy_file(module(),targetDir/L"steam.exe",fs::copy_options::none),"copy inert target PE");
    const auto eventName=L"Local\\YMCC.NativeAction.Target."+std::to_wstring(GetCurrentProcessId());
    stop.value=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());need(stop.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"owned target event create");
    target.start(targetDir/L"steam.exe",{L"--native-target",eventName},desktop.full());
    si::Peer actual;need(actual.load(target.pid),"actual owned target identity");
    nativeTargetBirth=actual.birth;nativeTargetRoot=targetDir.wstring();nativeOwnedTarget=&target;nativeOwnedTargetStop=stop.value;
    g_steamSettingsObserver.value={nativeTargetRoot,target.pid,1001,nativeTargetBirth};
    nativeTargetReceipt={{"pid",target.pid},{"birth",std::to_string(actual.birth)},{"image",si::utf8(actual.path)}};
    if(nativePresenceMode()){
        nativeRegistryActivePid=0;
        if(providerMode==L"native-compose-presence-birth-mismatch")++g_steamSettingsObserver.value.created;
        if(nativeReplacementMode()){
            const auto otherDir=root/L"OtherOwned";need(fs::create_directory(otherDir),"second target directory");
            need(fs::copy_file(module(),otherDir/L"steam.exe",fs::copy_options::none),"second inert Steam PE");
            const auto name=eventName+L".Other";
            nativeOtherStop.value=CreateEventW(nullptr,TRUE,FALSE,name.c_str());need(nativeOtherStop.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"second target event create");
            nativeOtherTarget=std::make_unique<Child>();nativeOtherTarget->start(otherDir/L"steam.exe",{L"--native-target",name},desktop.full());
            si::Peer other;need(other.load(nativeOtherTarget->pid),"second actual process identity");nativeOtherPid=other.pid;
            nativeExpectedTargetReturns=6;
            if(providerMode==L"native-compose-presence-replacement-preflight")nativeRegistryActivePid=other.pid;
        }
    }
    ymcc::sleeppower::SharedPowerV1 captured;need(captureSharedPowerLifecycle(captured)&&captured.observedNotificationSequence==5,"actual initial Native48 capture");
}
static void nativeCompositionBeforeDrain(){
    need(!nativeAfterDrain,"composition action drained twice");nativeAfterDrain=true;
    if(providerMode==L"native-compose-source-queue-rotate"){
        auto& source=fixtureSource();
        const auto old=source.read().view();
        need(old.current(),"original source before queue drain");
        // This also proves that an unexecuted queue holds no counted read.
        source.drain();source.resumeReads();
        nativeSourceFaultApplied=!old.current()&&source.read().current();
    }
    if(providerMode==L"native-compose-queue-pending"){
        need(bool(g_sleepPowerDispatch.window(PBT_APMSUSPEND,GetTickCount64())),"actual pending suspend receipt");
    }else if(providerMode==L"native-compose-queue-aba"){
        const auto suspend=g_sleepPowerDispatch.window(PBT_APMSUSPEND,GetTickCount64());need(bool(suspend),"actual suspend receipt");g_sleepPowerDispatch.complete(suspend,GetTickCount64());
        const auto resume=g_sleepPowerDispatch.window(PBT_APMRESUMEAUTOMATIC,GetTickCount64());need(bool(resume),"actual resume receipt");g_sleepPowerDispatch.complete(resume,GetTickCount64());
        ++g_sharedPowerSuspendSerial;
    }else if(providerMode==L"native-compose-publication")++g_sharedPowerPublicationRevision;
    if(providerMode==L"native-compose-identity-queue-process")++nativeIdentityProcessEpoch;
    else if(providerMode==L"native-compose-identity-queue-account")++nativeIdentityAccountEpoch;
    else if(providerMode==L"native-compose-identity-publication")++nativeIdentityRevision;
}
static LRESULT nativeCompositionServe(HWND window,HWND sender,const COPYDATASTRUCT* data){
    if(!data)return FALSE;
    if(data->dwData==sa::messageTag){
        need(data->cbData==sizeof(sa::Request)&&data->lpData,"native action identity envelope");
        std::memcpy(&nativeExpectedAction,data->lpData,sizeof(nativeExpectedAction));nativeExpectedActionKnown=true;
        if(providerMode==L"native-compose-expected-power-changed")++g_powerGeneration;
        return serveLibrarySteamAction(window,sender,data);
    }
    if(data->dwData==si::messageTag)return si::serve(window,sender,data,[](std::uint32_t mask,const auto& table,const auto& reading){
        ++nativeInfoRequests;si::Facts facts;ymcc::sleeppower::SharedPowerV1 power;
        if(!reading.currentFor(fixtureSource())||!table||table->status!=ymcc::processInventory::Status::Complete||!captureSharedPowerLifecycle(power))return facts;
        facts.available=true;facts.requested=mask;facts.observedAt=GetTickCount64();
        if(nativePresenceMode()){
            // Explicit fixture-only census. Original full table is read once by
            // real ingress; only verified owned PID rows enter this test scope.
            ymcc::processInventory::Snapshot scoped;scoped.status=table->status;
            for(const auto& row:table->rows)if(row.pid==nativeOwnedTarget->pid||(nativeOtherPid&&row.pid==nativeOtherPid))scoped.rows.push_back(row);
            const bool after=WaitForSingleObject(nativeOwnedTarget->process.value,0)==WAIT_OBJECT_0;
            if(after&&providerMode==L"native-compose-presence-stale-after")scoped.rows.push_back({nativeOwnedTarget->pid,L"steam.exe",0,GetCurrentProcessId()});
            if(after&&providerMode==L"native-compose-presence-incomplete-after")scoped.status=ymcc::processInventory::Status::Failed;
            const auto identity=g_steamSettingsObserver.identitySnapshot();
            si::collectPresence(facts,scoped,identity?identity->session:ymcc::steamsession::Session{},
                identity&&identity->registeredPidReadError==ERROR_SUCCESS?identity->registeredPid:0);
            nativePresenceObservations.push_back({{"request",nativeInfoRequests},{"afterOriginalExit",after},
                {"fixtureCensusStatus",int(scoped.status)},{"fixtureRows",scoped.rows.size()},
                {"presence",facts.presence},{"selectedPid",facts.processPid},{"selectedBirth",std::to_string(facts.processBirth)},
                {"registryActivePid",nativeRegistryActivePid},{"targetPid",nativeOwnedTarget->pid},{"otherPid",nativeOtherPid}});
        }else{
            const auto observed=WaitForSingleObject(nativeOwnedTarget->process.value,0);
            if(observed==WAIT_TIMEOUT){
                facts.presence=1;facts.processPid=nativeOwnedTarget->pid;facts.processBirth=nativeTargetBirth;
                facts.processPath=si::utf8(nativeTargetRoot+L"\\steam.exe");
            }else if(observed==WAIT_OBJECT_0)facts.presence=0;else return si::Facts{};
        }
        if(!sharedPowerLifecycleCurrent(power))return si::Facts{};
        facts.actionPowerGeneration=power.generation;facts.actionPowerSequence=power.observedNotificationSequence;facts.actionPowerSerial=power.suspendSerial;
        if(providerMode==L"native-compose-source-info-revoke"){
            fixtureSource().suspendReads();fixtureSource().resumeReads();
            nativeSourceFaultApplied=facts.available&&!reading.current()&&fixtureSource().read().current();
        }
        return facts;
    },[]{return g_steamSettingsObserver.sourceRead();});
    return FALSE;
}
static void nativeCompositionEnd(Child& target,si::Handle& stop){
    if(nativeOtherTarget){
        nativeOtherAliveAtHostEnd=WaitForSingleObject(nativeOtherTarget->process.value,0)==WAIT_TIMEOUT;
        need(SetEvent(nativeOtherStop.value)!=FALSE,"second target stop");nativeOtherTarget->join(3000);
        need(nativeOtherTarget->exit==0,"second target normal return");nativeOtherTarget->close();nativeOtherJoined=true;
        need(CloseHandle(nativeOtherStop.value)!=FALSE,"second target event close");nativeOtherStop.value=nullptr;nativeOtherTarget.reset();
    }
    nativeStopTarget();target.close();
    need(stop.value&&CloseHandle(stop.value),"owned target event close");stop.value=nullptr;
    nativeOwnedTarget=nullptr;nativeOwnedTargetStop=nullptr;
}
static Json nativeCompositionReceipt(const Json& client,DWORD actualClient,DWORD kernelBefore,DWORD kernelAfter){
    if(nativeSourceMode()){
        const bool queued=providerMode==L"native-compose-source-queue-rotate";
        const unsigned action=queued?1:0;
        const bool good=nativeSourceFaultApplied&&nativeInfoRequests==1&&nativeSourceReads==1+action&&
            !client.value("hostReturned",true)&&client.value("hostErrorPresent",false)&&
            client.value("actionCalls",999u)==action&&client.value("actionSends",999u)==action&&
            client.value("outcome",-1)==LONG(queued?sa::Outcome::Rejected:sa::Outcome::Unknown)&&
            nativeAfterDrain==queued&&submissions==action&&observedQueries==action&&(!queued||observedServer==actualClient)&&
            nativeAcceptedFields==action&&!nativeClaimedFields&&!nativeUnboundActionCAS&&!nativeLookups&&!nativeShellAttempts&&!effects&&
            nativeTargetReturned&&nativeObservedResourcesReturned()&&client.at("resources").value("recovered",false)&&
            kernelBefore>=3&&kernelBefore-3==kernelAfter;
        return {{"pass",good},{"client",client},{"target",nativeTargetReceipt},{"targetJoined",nativeTargetReturned},
            {"sourceFaultApplied",nativeSourceFaultApplied},{"sourceReads",nativeSourceReads},{"infoRequests",nativeInfoRequests},
            {"submissions",submissions},{"kernelQueries",observedQueries},{"acceptedFieldCAS",nativeAcceptedFields},
            {"claimedFieldCAS",nativeClaimedFields},{"actualProcessVerifications",nativeLookups},{"shellAttempts",nativeShellAttempts},
            {"effects",effects},{"resources",nativeResourceReceipt()},{"kernelBefore",kernelBefore},{"kernelAfter",kernelAfter},
            {"productExecuted",false}};
    }
    const bool success=providerMode==L"native-compose-current"||providerMode==L"native-compose-publication";
    if(nativeIdentityMode()){
        const bool success=providerMode==L"native-compose-identity-publication"||providerMode==L"native-compose-identity-post-submit";
        const bool unavailable=providerMode==L"native-compose-identity-unknown";
        const bool acquired=success||providerMode==L"native-compose-identity-lookup-epoch"||providerMode==L"native-compose-identity-after-claim";
        const bool claimed=success||providerMode==L"native-compose-identity-after-claim";
        const auto expected=success?sa::Outcome::Submitted:unavailable?sa::Outcome::Unknown:sa::Outcome::Rejected;
        bool good=client.value("hostReturned",false)==success&&client.value("hostErrorPresent",false)!=success&&
            client.value("actionCalls",0)==1&&client.value("actionSends",0)==1&&submissions==(unavailable?0u:1u)&&
            observedQueries==(unavailable?0u:1u)&&(!observedQueries||observedServer==actualClient)&&nativeTargetReturned&&
            nativeObservedResourcesReturned()&&client.at("resources").value("recovered",false)&&
            kernelBefore>=3&&kernelBefore-3==kernelAfter&&nativeAcceptedFields==(unavailable?0u:1u)&&
            nativeClaimedFields==(claimed?1u:0u)&&nativeUnboundActionCAS==0&&
            nativeLookups==(acquired?1u:0u)&&nativeShellAttempts==(success?1u:0u)&&effects==(success?1u:0u)&&
            client.value("outcome",-1)==LONG(expected)&&nativeIdentityReads>0;
        return {{"pass",good},{"client",client},{"target",nativeTargetReceipt},{"targetJoined",nativeTargetReturned},
            {"identityReads",nativeIdentityReads},{"identityProcessEpoch",nativeIdentityProcessEpoch},{"identityAccountEpoch",nativeIdentityAccountEpoch},
            {"actualProcessVerifications",nativeLookups},{"shellAttempts",nativeShellAttempts},{"effects",effects},{"submissions",submissions},
            {"acceptedFieldCAS",nativeAcceptedFields},{"claimedFieldCAS",nativeClaimedFields},{"unboundCAS",nativeUnboundActionCAS},
            {"kernelQueries",observedQueries},{"kernelServerPid",observedServer},{"kernelBefore",kernelBefore},{"kernelAfter",kernelAfter},
            {"expectedTargetHandleReturns",3},{"resources",nativeResourceReceipt()},{"publicIdentityComparatorActual",true},
            {"sessionObserverIsAdapter",true},{"finalShellIsInert",true},{"productExecuted",false}};
    }
    if(nativePresenceMode()){
        const bool preflight=providerMode==L"native-compose-presence-replacement-preflight"||providerMode==L"native-compose-presence-birth-mismatch";
        const bool completed=providerMode==L"native-compose-presence-absence";
        bool good=client.value("hostReturned",false)==completed&&client.value("hostErrorPresent",false)!=completed&&
            client.value("actionCalls",0)==(preflight?0:1)&&client.value("actionSends",0)==(preflight?0:1)&&
            submissions==(preflight?0u:1u)&&observedQueries==(preflight?0u:1u)&&
            (!observedQueries||observedServer==actualClient)&&nativeTargetReturned&&
            nativeObservedResourcesReturned()&&client.at("resources").value("recovered",false)&&
            kernelBefore>=nativeExpectedTargetReturns&&kernelBefore-nativeExpectedTargetReturns==kernelAfter&&
            nativeUnexpectedRegistryReads==0&&nativeRegistryReads==0&&
            nativeShellAttempts==(preflight?0u:1u)&&effects==(preflight?0u:1u)&&nativeUnboundActionCAS==0&&
            nativeAcceptedFields==(preflight?0u:1u)&&nativeClaimedFields==(preflight?0u:1u)&&
            client.value("outcome",-1)==LONG(preflight?sa::Outcome::Unknown:sa::Outcome::Submitted);
        need(!nativePresenceObservations.empty(),"presence algorithm must actually run");
        const auto& last=nativePresenceObservations.back();
        if(completed)good=good&&last.value("presence",-2)==0&&last.value("fixtureRows",999u)==0;
        else if(providerMode==L"native-compose-presence-replacement-after"||providerMode==L"native-compose-presence-replacement-preflight")
            good=good&&last.value("presence",-2)==1&&last.value("selectedPid",0u)==nativeOtherPid&&nativeOtherAliveAtHostEnd&&nativeOtherJoined;
        else if(providerMode==L"native-compose-presence-incomplete-after")good=good&&last.value("presence",-2)==-1;
        else good=good&&last.value("presence",-2)==1&&last.value("selectedPid",0u)==0;
        return {{"pass",good},{"client",client},{"target",nativeTargetReceipt},{"targetJoined",nativeTargetReturned},
            {"otherPid",nativeOtherPid},{"otherAliveAtHostEnd",nativeOtherAliveAtHostEnd},{"otherJoined",nativeOtherJoined},
            {"presenceAlgorithmActual",true},{"presenceObservations",nativePresenceObservations},
            {"registryReads",nativeRegistryReads},{"unexpectedRegistryReads",nativeUnexpectedRegistryReads},
            {"fixtureCensusOnly",true},{"registryPidIsInertReadAdapter",true},
            {"resources",nativeResourceReceipt()},{"kernelBefore",kernelBefore},{"kernelAfter",kernelAfter},
            {"expectedTargetHandleReturns",nativeExpectedTargetReturns},{"acceptedFieldCAS",nativeAcceptedFields},
            {"claimedFieldCAS",nativeClaimedFields},{"unboundCAS",nativeUnboundActionCAS},
            {"shellAttempts",nativeShellAttempts},{"effects",effects},{"submissions",submissions},
            {"kernelQueries",observedQueries},{"kernelServerPid",observedServer},
            {"hostBodyActual",true},{"powerBodiesActual",true},{"typedBodyActual",true},{"finalShellIsInert",true},{"productExecuted",false}};
    }
    bool good=client.value("hostReturned",false)==success&&client.value("hostErrorPresent",false)!=success&&
        client.value("actionCalls",0)==1&&client.value("actionSends",0)==1&&submissions==1&&
        observedQueries==1&&observedServer==actualClient&&nativeTargetReturned&&
        nativeObservedResourcesReturned()&&client.at("resources").value("recovered",false)&&
        kernelBefore>=3&&kernelBefore-3==kernelAfter&&nativeAcceptedFields==1&&nativeUnboundActionCAS==0;
    const auto expectedOutcome=success?sa::Outcome::Submitted:
        (providerMode==L"native-compose-queue-pending"||providerMode==L"native-compose-queue-aba"||providerMode==L"native-compose-after-claim-gate")?sa::Outcome::Rejected:sa::Outcome::Failed;
    good=good&&client.value("outcome",-1)==LONG(expectedOutcome)&&nativeClaimedFields==(nativeSuccessfulCAS==2?1u:0u);
    if(success)good=good&&nativeLookups==1&&nativeShellAttempts==1&&nativeFixedCommand&&effects==1&&nativeSuccessfulCAS==2&&client.value("outcome",-1)==LONG(sa::Outcome::Submitted);
    else if(providerMode==L"native-compose-after-claim-gate")good=good&&nativeLookups==1&&nativeSuccessfulCAS==2&&nativeShellAttempts==0&&effects==0&&!g_hardwareWriteGate.load();
    else if(providerMode==L"native-compose-target-exited")good=good&&nativeLookups==1&&nativeSuccessfulCAS==1&&nativeShellAttempts==0&&effects==0;
    else if(providerMode==L"native-compose-shell-reject")good=good&&nativeLookups==1&&nativeSuccessfulCAS==2&&nativeShellAttempts==1&&effects==0&&client.value("outcome",-1)==LONG(sa::Outcome::Failed);
    else good=good&&nativeLookups==0&&nativeSuccessfulCAS==1&&nativeShellAttempts==0&&effects==0;
    ymcc::sleeppower::SharedPowerV1 power;const bool readable=readSharedSystemPower(&power,sizeof(power))==1;
    return {{"pass",good},{"client",client},{"target",nativeTargetReceipt},{"targetJoined",nativeTargetReturned},
        {"actualNative48Readable",readable},{"nativePhase",power.phase},{"nativeFlags",power.flags},
        {"nativeGeneration",power.generation},{"nativeSequence",power.observedNotificationSequence},{"nativeSerial",power.suspendSerial},
        {"nativePublication",power.publicationRevision},{"actualProcessVerifications",nativeLookups},
        {"sessionAdapterReads",nativeSessionReads},{"infoRequests",nativeInfoRequests},{"submissions",submissions},
        {"resources",nativeResourceReceipt()},{"kernelBefore",kernelBefore},{"kernelAfter",kernelAfter},{"expectedTargetHandleReturns",3},{"acceptedFieldCAS",nativeAcceptedFields},{"claimedFieldCAS",nativeClaimedFields},{"unboundCAS",nativeUnboundActionCAS},{"successfulActionCAS",nativeSuccessfulCAS},{"shellAttempts",nativeShellAttempts},{"effects",effects},
        {"kernelServerPid",observedServer},{"kernelQueries",observedQueries},{"fixedCommand",nativeFixedCommand},
        {"powerBodiesActual",true},{"typedBodyActual",true},{"hostBodyActual",true},
        {"sessionObserverIsAdapter",true},{"finalShellIsInert",true},{"productExecuted",false}};
}
