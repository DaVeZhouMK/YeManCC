// Fixture seams only. The three included product bodies are byte-exact extracts.
#include "sleep_power_state.h"
#include "sleep_power_dispatch.h"
static void chainTypedObserveCAS(volatile LONG*,LONG,LONG,LONG) noexcept;
static LONG chainTypedCAS(volatile LONG* p,LONG x,LONG c){const auto r=::InterlockedCompareExchange(p,x,c);chainTypedObserveCAS(p,x,c,r);return r;}
#pragma push_macro("InterlockedCompareExchange")
#pragma push_macro("CreateFileW")
#pragma push_macro("OpenEventW")
#pragma push_macro("CreateEventW")
#pragma push_macro("CreateNamedPipeW")
#pragma push_macro("OpenProcessToken")
#pragma push_macro("OpenFileMappingW")
#pragma push_macro("CreateFileMappingW")
#pragma push_macro("MapViewOfFile")
#pragma push_macro("UnmapViewOfFile")
#pragma push_macro("CreateWindowExW")
#pragma push_macro("DestroyWindow")
#undef InterlockedCompareExchange
#define InterlockedCompareExchange chainTypedCAS
#define CreateFileW nativeObserveCreateFile
#define OpenEventW nativeObserveOpenEvent
#define CreateEventW nativeObserveCreateEvent
#define CreateNamedPipeW nativeObserveCreatePipe
#define OpenProcessToken nativeObserveOpenProcessToken
#define OpenFileMappingW nativeObserveOpenMapping
#define CreateFileMappingW nativeObserveCreateMapping
#define MapViewOfFile nativeObserveMap
#define UnmapViewOfFile nativeObserveUnmap
#define CreateWindowExW nativeObserveCreateWindow
#define DestroyWindow nativeObserveDestroyWindow
#include "steam_client_action_producer.h"
#pragma pop_macro("DestroyWindow")
#pragma pop_macro("CreateWindowExW")
#pragma pop_macro("UnmapViewOfFile")
#pragma pop_macro("MapViewOfFile")
#pragma pop_macro("CreateFileMappingW")
#pragma pop_macro("OpenFileMappingW")
#pragma pop_macro("OpenProcessToken")
#pragma pop_macro("CreateNamedPipeW")
#pragma pop_macro("CreateEventW")
#pragma pop_macro("OpenEventW")
#pragma pop_macro("CreateFileW")
#pragma pop_macro("InterlockedCompareExchange")

std::atomic<bool> g_exitRequested{false},g_poolCancel{false};
struct ChainTypedMock {bool isolated=false;} g_aiFanMockSession;
enum class PowerLifecycle:uint8_t {Ready,Suspending,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned long long> g_powerGeneration{7};
static std::atomic<bool> g_hardwareWriteGate{true};
static ymcc::sleeppower::Dispatch g_sleepPowerDispatch;
static std::atomic<uint64_t> g_sharedPowerPublicationRevision{10},g_sharedPowerSuspendSerial{2};
#include "typed-shutdown-extracted/power-body.inc"

namespace ChainTyped {
static constexpr UINT drainMessage=WM_APP+0x6d;
static HWND window=nullptr;
static std::string name;
static fs::path targetRoot;
static DWORD targetPid=0;
static uint64_t targetBirth=0;
static bool enabled=false,targetReturned=false,targetAliveAtHostEnd=false,fixedCommand=false,revoked=false;
static unsigned requests=0,submits=0,queueAccepted=0,queueRejected=0,drains=0,accepted=0,claimed=0,shellAttempts=0,effects=0,verifications=0,sessionReads=0,unboundCAS=0;
static ymcc::steamaction::Request expected;
static bool expectedKnown=false;
static std::vector<std::function<void()>> pending;
static std::function<void()> stopTarget;
static void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static bool ackMode(const std::string& value){return value=="chain-typed-ack-cancel-unknown-preserves"||value=="chain-typed-ack-submitted-unknown-preserves";}
static bool ackSubmitted(){return name=="chain-typed-ack-submitted-unknown-preserves";}
static HANDLE returnedEvent=nullptr,borrowedHost=nullptr;
static std::wstring returnedEventName;
static DWORD borrowedHostPid=0,actualHostWaitDuringAck=WAIT_FAILED,actualHostWaitFinal=WAIT_FAILED,ackReturnWait=WAIT_FAILED;
static ULONGLONG ackHandlerWaitMs=0;
static bool borrowedHostReturned=false,postedDrain=false,lateWireRead=false,postDrainWireRead=false;
static unsigned drainPosts=0,drainMessagesConsumed=0,ackWaits=0;
static LONG lateCancelled=-1,lateClaimed=-1,lateCompleted=-1,lateAccepted=-1,lateOutcome=-1;
static LONG finalCancelled=-1,finalClaimed=-1,finalCompleted=-1,finalOutcome=-1;
static std::unique_ptr<ymcc::steamaction::Mapping> lateObservation;
static void drain();
static bool known(const std::string& value){
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
return value=="chain-observer-shutdown-commit"||value=="chain-observer-power-revoked-preserves";
#else
return ackMode(value)|| value=="chain-typed-shutdown-commit"||value=="chain-typed-revoked-preserves"||value=="chain-typed-rejected-preserves";
#endif
}
static json client(){return {{"pid",uint32_t(targetPid)},{"birth",std::to_string(targetBirth)},{"powerGeneration","7"},{"powerSequence","5"},{"powerSerial","2"}};}
}
static void chainTypedObserveCAS(volatile LONG* p,LONG x,LONG c,LONG result) noexcept {
    if(x!=1||c!=0||result!=0)return;
    using namespace ChainTyped;
    if(!expectedKnown){++unboundCAS;return;}
    for(const auto& row:nativeObservedViews){
        if(row.second!=sizeof(ymcc::steamaction::Wire))continue;
        const auto* wire=static_cast<const ymcc::steamaction::Wire*>(row.first);
        const auto address=reinterpret_cast<UINT_PTR>(p),base=reinterpret_cast<UINT_PTR>(row.first);
        if(address!=base+offsetof(ymcc::steamaction::Wire,accepted)&&address!=base+offsetof(ymcc::steamaction::Wire,claimed))continue;
        if(!ymcc::steamaction::matches(wire->request,expected)){++unboundCAS;return;}
        if(address==base+offsetof(ymcc::steamaction::Wire,accepted))++accepted;else ++claimed;
        return;
    }
    ++unboundCAS;
}
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
static ymcc::steamsession::Observer g_steamSettingsObserver(ChainObserver::registryPath);
#else
struct ChainTypedObserver {
    ymcc::steamsession::Session value;
    auto sessionSnapshot(){++ChainTyped::sessionReads;return value;}
    int steamPresence(){return ChainTyped::targetReturned?0:1;}
}g_steamSettingsObserver;
#endif
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
static BOOL WINAPI chainTypedResourceClose(HANDLE value){
#ifdef YMCC_LIBRARY_UNPAIRED_TYPED_CLOSE_CONTROL
    return nativeObserveClose(value); // Explicit original mismatch semantic control.
#else
    return ObserverResources::closeHandleOrForward(value,&nativeObserveClose);
#endif
}
#endif
namespace ymcc::steamsession {
static HANDLE chainTypedVerified(DWORD pid,const std::wstring& root,ULONGLONG* birth){
++ChainTyped::verifications;
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
if(!ChainTyped::enabled||pid!=ChainTyped::targetPid||!birth||_wcsicmp(root.c_str(),ChainTyped::targetRoot.c_str()))return nullptr;
auto process=verifiedProcess(pid,root,birth);if(process&&*birth!=ChainTyped::targetBirth){
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
chainTypedResourceClose(process);
#else
CloseHandle(process);
#endif
return nullptr;}return process;
#else
return verifiedProcess(pid,root,birth);
#endif
}
}
static BOOL chainTypedShell(SHELLEXECUTEINFOW* info){
    using namespace ChainTyped;++shellAttempts;
    fixedCommand=enabled&&info&&info->lpFile&&info->lpParameters&&info->lpDirectory&&
        _wcsicmp(info->lpFile,(targetRoot/L"steam.exe").c_str())==0&&
        std::wstring(info->lpParameters)==L"-shutdown"&&info->nShow==SW_HIDE&&
        _wcsicmp(info->lpDirectory,targetRoot.c_str())==0&&info->fMask==SEE_MASK_FLAG_NO_UI&&
        info->lpVerb&&std::wstring(info->lpVerb)==L"open";
    need(fixedCommand&&effects==0&&stopTarget,"Typed fixed command escaped owned target");
    ymcc::steaminfo::Peer target;need(target.load(targetPid)&&target.birth==targetBirth&&
        ymcc::steaminfo::pathKey(target.path)==ymcc::steaminfo::pathKey((targetRoot/L"steam.exe").wstring()),"Typed target identity changed");
    stopTarget();targetReturned=true;++effects;
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
    ChainObserver::waitRetired();
#endif
    return TRUE;
}
#pragma push_macro("OpenProcessToken")
#define OpenProcessToken nativeObserveOpenProcessToken
#define verifiedProcess chainTypedVerified
#define ShellExecuteExW chainTypedShell
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
#pragma push_macro("CloseHandle")
#undef CloseHandle
#define CloseHandle chainTypedResourceClose
#endif
namespace ymcc::steamui {
#include "typed-shutdown-extracted/typed-client.inc"
}
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
#undef CloseHandle
#pragma pop_macro("CloseHandle")
#endif
#undef ShellExecuteExW
#undef verifiedProcess
#pragma pop_macro("OpenProcessToken")
static bool poolSubmit(std::function<void()> work){
    using namespace ChainTyped;++submits;
    need(enabled&&window&&pending.empty(),"Typed duplicate or unbound queue submit");
    if(name=="chain-typed-rejected-preserves"){++queueRejected;return false;}
    pending.push_back(std::move(work));
    if(!ackMode(name)){
        if(!PostMessageW(window,drainMessage,0,0)){pending.clear();++queueRejected;return false;}
        postedDrain=true;++drainPosts;
    }
    ++queueAccepted;return true;
}
#include "typed-shutdown-extracted/library-handler.inc"
namespace ChainTyped {
static void bind(const std::string& selected,HWND provider,const fs::path& root,DWORD pid,uint64_t birth,std::function<void()> stop,HANDLE returned=nullptr,const std::wstring& eventName={}){
    need(known(selected)&&!enabled&&pid&&birth&&stop,"Typed bind identity");
    name=selected;window=provider;targetRoot=root;targetPid=pid;targetBirth=birth;stopTarget=std::move(stop);enabled=true;
    returnedEvent=returned;returnedEventName=eventName;
    need(!ackMode(name)||(returnedEvent&&!returnedEventName.empty()),"Ack returned event binding");
#ifndef YMCC_LIBRARY_OBSERVER_CHAIN
    g_steamSettingsObserver.value={root.wstring(),pid,1001,birth};
#endif
    for(unsigned i=0;i<5;++i){const auto receipt=g_sleepPowerDispatch.window(PBT_APMRESUMEAUTOMATIC,GetTickCount64());need(bool(receipt),"Typed initial power receipt");g_sleepPowerDispatch.complete(receipt,GetTickCount64());}
    ymcc::sleeppower::SharedPowerV1 power;need(captureSharedPowerLifecycle(power)&&power.generation==7&&power.observedNotificationSequence==5&&power.suspendSerial==2,"Typed initial public power tuple");
}
static void facts(ymcc::steaminfo::Facts& value){
    if(!enabled)return;ymcc::sleeppower::SharedPowerV1 power;
    if(!captureSharedPowerLifecycle(power)){value.available=false;return;}
    value.actionPowerGeneration=power.generation;value.actionPowerSequence=power.observedNotificationSequence;value.actionPowerSerial=power.suspendSerial;
}
static LRESULT serve(HWND provider,HWND sender,const COPYDATASTRUCT* data){
    need(enabled&&requests==0&&data&&data->cbData==sizeof(expected)&&data->lpData,"Typed exact single request");
    std::memcpy(&expected,data->lpData,sizeof(expected));expectedKnown=true;++requests;
    need(expected.targetPid==targetPid&&expected.targetBirth==targetBirth&&expected.powerGeneration==7&&expected.powerSequence==5&&expected.powerSerial==2,"Typed request differs from actual seed");
    const auto response=serveLibrarySteamAction(provider,sender,data);
    if(ackMode(name)){
        need(response==ymcc::steamaction::protocol&&accepted==1&&queueAccepted==1&&pending.size()==1&&borrowedHost&&returnedEvent,"Ack actual accepted request and borrowed Host");
        lateObservation=std::make_unique<ymcc::steamaction::Mapping>();
        lateObservation->handle.value=nativeObserveOpenMapping(FILE_MAP_READ,FALSE,ymcc::steamaction::mappingName(expected).c_str());
        need(lateObservation->handle.value!=nullptr,"Ack read-only actual mapping");
        lateObservation->view.value=nativeObserveMap(lateObservation->handle.value,FILE_MAP_READ,0,0,sizeof(ymcc::steamaction::Wire));
        need(lateObservation->view.value!=nullptr,"Ack read-only actual Wire");
        if(ackSubmitted())drain();
        const auto began=GetTickCount64();++ackWaits;
        ackReturnWait=WaitForSingleObject(returnedEvent,3000);ackHandlerWaitMs=GetTickCount64()-began;
        actualHostWaitDuringAck=WaitForSingleObject(borrowedHost,0);
        need(ackReturnWait==WAIT_OBJECT_0&&(actualHostWaitDuringAck==WAIT_TIMEOUT||actualHostWaitDuringAck==WAIT_OBJECT_0),"Ack actual sender return event/Host observation");
        const auto* wire=static_cast<const ymcc::steamaction::Wire*>(lateObservation->view.value);MemoryBarrier();
        need(ymcc::steamaction::matches(wire->request,expected),"Ack observation nonce/request changed");
        lateAccepted=wire->accepted;lateCancelled=wire->cancelled;lateClaimed=wire->claimed;lateCompleted=wire->completed;lateOutcome=wire->outcome;lateWireRead=true;
        need(lateAccepted==1&&lateCancelled==1&&lateClaimed==(ackSubmitted()?1:0)&&lateCompleted==(ackSubmitted()?1:0),"Ack original sender cancellation/claim/completion");
        if(!ackSubmitted()){
            need(!postedDrain&&pending.size()==1,"Ack cancel retained original pending work");
            need(PostMessageW(window,drainMessage,0,0)!=FALSE,"Ack cancelled queue drain post");postedDrain=true;++drainPosts;
        }
    }
    return response;
}
static void drain(){
    need(enabled&&pending.size()==1&&drains==0,"Typed exact accepted queue");++drains;
    if(postedDrain){postedDrain=false;++drainMessagesConsumed;}
    auto work=std::move(pending.front());pending.clear();
    if(name=="chain-typed-revoked-preserves"){
        const auto receipt=g_sleepPowerDispatch.window(PBT_APMSUSPEND,GetTickCount64());need(bool(receipt)&&g_sleepPowerDispatch.pendingSuspend(),"Typed original Dispatch revocation");revoked=true;
    }
    work();
}
static void borrowActualHost(HANDLE handle,DWORD pid){
    need(enabled&&!borrowedHost&&handle&&GetProcessId(handle)==pid,"Borrow exact created Host process HANDLE");
    borrowedHost=handle;borrowedHostPid=pid;borrowedHostReturned=false;
}
static void finishHostObservation(){
    if(!enabled)return;
    if(ackMode(name)){
        need(borrowedHost&&GetProcessId(borrowedHost)==borrowedHostPid,"Return same borrowed Host HANDLE");
        actualHostWaitFinal=WaitForSingleObject(borrowedHost,0);need(actualHostWaitFinal==WAIT_OBJECT_0,"Actual Host exited before borrowed HANDLE return");
        if(!pending.empty()){
            MSG message{};need(postedDrain&&PeekMessageW(&message,window,drainMessage,drainMessage,PM_REMOVE)!=FALSE,"Exact cancelled drain message settlement");
            drain();
        }
        need(pending.empty()&&!postedDrain&&lateObservation&&lateWireRead,"Ack pending queue/observation settlement");
        const auto* wire=static_cast<const ymcc::steamaction::Wire*>(lateObservation->view.value);MemoryBarrier();
        need(ymcc::steamaction::matches(wire->request,expected),"Final ack observation request identity");
        finalCancelled=wire->cancelled;finalClaimed=wire->claimed;finalCompleted=wire->completed;finalOutcome=wire->outcome;postDrainWireRead=true;
        lateObservation.reset();
    }
    borrowedHost=nullptr;borrowedHostReturned=true;returnedEvent=nullptr;
}
static json receipt(){
    const bool success=name=="chain-typed-shutdown-commit"
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
        ||name=="chain-observer-shutdown-commit"
#endif
        ;
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
    if(name=="chain-observer-power-revoked-preserves"){
        const bool passed=enabled&&requests==0&&accepted==0&&submits==0&&pending.empty()&&unboundCAS==0&&queueAccepted==0&&queueRejected==0&&drains==0&&claimed==0&&shellAttempts==0&&effects==0&&verifications==0&&!targetReturned&&targetAliveAtHostEnd&&ChainObserver::powerRevoked;
        return {{"passed",passed},{"requestCount",requests},{"queueSubmits",submits},{"acceptedFieldCAS",accepted},{"claimedFieldCAS",claimed},{"shellAttempts",shellAttempts},{"effects",effects},{"dispatchProcessVerifications",verifications},{"targetReturnedByAction",targetReturned},{"targetAliveAtHostEnd",targetAliveAtHostEnd},{"powerRevokedDuringInformationCollect",ChainObserver::powerRevoked},{"observerIsExplicitAdapter",false},{"actualIsolatedObserver",true},{"originalMainHandler",true},{"originalTypedDispatch",true},{"originalPowerBodies",true}};
    }
#endif
    const bool reject=name=="chain-typed-rejected-preserves";
    const bool ack=ackMode(name),actionSuccess=success||(ack&&ackSubmitted());
    const bool ackProof=!ack||(ackWaits==1&&ackReturnWait==WAIT_OBJECT_0&&
        (actualHostWaitDuringAck==WAIT_TIMEOUT||actualHostWaitDuringAck==WAIT_OBJECT_0)&&actualHostWaitFinal==WAIT_OBJECT_0&&
        borrowedHostReturned&&!borrowedHost&&!returnedEvent&&!lateObservation&&!postedDrain&&lateWireRead&&postDrainWireRead&&
        lateAccepted==1&&lateCancelled==1&&lateClaimed==(ackSubmitted()?1:0)&&lateCompleted==(ackSubmitted()?1:0)&&
        finalCancelled==1&&finalClaimed==(ackSubmitted()?1:0)&&finalCompleted==1&&
        finalOutcome==LONG(ackSubmitted()?ymcc::steamaction::Outcome::Submitted:ymcc::steamaction::Outcome::Rejected)&&
        drainPosts==(ackSubmitted()?0u:1u)&&drainMessagesConsumed==drainPosts);
    const bool passed=ackProof&&enabled&&requests==1&&accepted==1&&submits==1&&pending.empty()&&unboundCAS==0&&
        queueAccepted==(reject?0u:1u)&&queueRejected==(reject?1u:0u)&&drains==(reject?0u:1u)&&
        claimed==(actionSuccess?1u:0u)&&shellAttempts==(actionSuccess?1u:0u)&&effects==(actionSuccess?1u:0u)&&
        verifications==(actionSuccess?1u:0u)&&targetReturned==actionSuccess&&targetAliveAtHostEnd!=actionSuccess&&
        revoked==(name=="chain-typed-revoked-preserves")&&(!actionSuccess||fixedCommand);
    return {{"passed",passed},{"ackTimeoutMode",ack},{"ackWaits",ackWaits},{"ackWaitTimeoutMs",3000},{"ackHandlerWaitMs",ackHandlerWaitMs},{"ackReturnWait",ackReturnWait},
        {"actualHostPid",borrowedHostPid},{"actualHostWait",actualHostWaitDuringAck},{"actualHostWaitFinal",actualHostWaitFinal},{"borrowedHostReturned",borrowedHostReturned},
        {"lateWireRead",lateWireRead},{"lateAccepted",lateAccepted},{"lateCancelled",lateCancelled},{"lateClaimed",lateClaimed},{"lateCompleted",lateCompleted},{"lateOutcome",lateOutcome},
        {"lateWire",{{"accepted",lateAccepted},{"cancelled",lateCancelled},{"claimed",lateClaimed},{"completed",lateCompleted},{"outcome",lateOutcome}}},
        {"postDrainWireRead",postDrainWireRead},{"finalCancelled",finalCancelled},{"finalClaimed",finalClaimed},{"finalCompleted",finalCompleted},{"finalOutcome",finalOutcome},
        {"drainPosts",drainPosts},{"drainMessagesConsumed",drainMessagesConsumed},{"pendingEmpty",pending.empty()},{"readOnlyObservationReturned",!lateObservation},{"requestCount",requests},{"acceptedFieldCAS",accepted},{"claimedFieldCAS",claimed},{"unboundCAS",unboundCAS},
        {"queueSubmits",submits},{"queueAccepted",queueAccepted},{"queueRejected",queueRejected},{"drains",drains},
        {"dispatchProcessVerifications",verifications},{"shellAttempts",shellAttempts},{"fixedCommand",fixedCommand},{"effects",effects},
        {"targetPid",targetPid},{"targetBirth",std::to_string(targetBirth)},{"targetReturnedByAction",targetReturned},{"targetAliveAtHostEnd",targetAliveAtHostEnd},
        {"publicPowerRevokedBeforeDrain",revoked},{"observerIsExplicitAdapter",
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
false
#else
true
#endif
},{"originalMainHandler",true},{"originalTypedDispatch",true},{"originalPowerBodies",true}};
}
}
