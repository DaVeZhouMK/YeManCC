#include <json.hpp>
#include <mutex>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include "../../native/sleep_power_dispatch.h"
#include "../../native/sleep_power_revocation.h"

// Existing production reader/guard are included by the original Win32 fixture.
// Only the main owner's terminal services and clock are controlled here.
#include "recovery-power-enum.generated.h"
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned long long> g_powerGeneration{7},g_resumeReadyGeneration{0};
static ymcc::sleeppower::RecoveryResumeCommit g_recoveryResumeCommit;
static std::atomic<ULONGLONG> g_lastPowerResumeCommitTick{0};
static std::atomic<bool> g_exitRequested{false},g_inputReady{false};
static std::atomic<uint64_t> g_sharedPowerPublicationRevision{1},g_sharedPowerSuspendSerial{1};
// The watchdog fixture does not start the separate remote Host transport.
static struct { void requestStop() noexcept {} } g_sharedNativeRemotePowerRuntimeGate;
#include "recovery-boundary-wiring.generated.h"
static std::atomic<bool> g_hardwareWriteGate{true};
static auto& nativeGate=g_hardwareWriteGate;
static bool moveNativeAuthority=false,revokeDuringCapture=false;
static std::atomic<ULONGLONG> g_sgLatestSleepEntryFileTime{0};
static unsigned terminalPosts=0;
static bool hardwareWriteAllowed(){if(revokeDuringCapture){revokeDuringCapture=false;revokeSharedPowerMirrors();}if(moveNativeAuthority)g_powerGeneration.fetch_add(1);return nativeGate;}
static unsigned long long currentPowerGeneration(){return g_powerGeneration.load();}
static bool sgShouldKeepInputDuringStandby(){return false;}
[[maybe_unused]] static void sleepRecoveryAutoPowerReady(unsigned long long){}
static void sleepRecoveryAutoCloseSuspendEpisode(){}
static void sgStaticScanSchedule(bool){}
static void sgNotifyWork(){}
template<class... Args> static void traceLog(const char*,Args&&...){}
template<class... Args> static void traceLogTry(const char*,Args&&...) noexcept {}
static void publishSharedPowerLifecycle(const char*){g_sharedPowerPublicationRevision.fetch_add(1);}
static HWND g_hwnd=nullptr;
static void sleepPowerPostResumeCommit(HWND,unsigned long long){++terminalPosts;}
static void steamAppearancePublicReady(unsigned long long) noexcept {}
[[maybe_unused]] static void deckySidebarRecheckRepeatedCommit(unsigned long long,uint64_t,uint64_t) noexcept {}
static RecoveryStateSnapshot* g_recoveryStateView=nullptr;
static std::mutex g_recoveryStateMx;
static std::atomic<int> g_recoveryStateBusyDepth{0};
static std::atomic<bool> g_recoveryStateExitIntent{false};
static std::atomic<unsigned int> g_recoveryUiDispatchPulse{42};
static std::string g_recoveryStatePhase="idle",g_recoveryStateLastCommand,g_recoveryStatePersona;
static unsigned long long g_recoveryStateBusyStartedAt=0,g_recoveryStateStartKey=0;
static constexpr unsigned kRecoveryStateMagic=kSnapshotMagic,kRecoveryStateSchemaVersion=kSnapshotSchemaVersion;
static void publisherFixtureGate();
static unsigned recoveryStateFanOwnerValue(){publisherFixtureGate();return 0;}
#include "recovery-main-publisher.generated.h"
static void recoveryStatePublishOnce(){recoveryStatePublishInto(g_recoveryStateView);}
#include <set>
#include <memory>
static void recoveryCheck(bool,const char*);
static std::mutex publisherResourceMx;
static std::set<HANDLE> publisherMappings,publisherEvents;
static std::set<const void*> publisherViews;
static std::atomic<unsigned> publisherMappingsCreated{0},publisherMappingsClosed{0},publisherEventsCreated{0},publisherEventsClosed{0},publisherViewsMapped{0},publisherViewsUnmapped{0},publisherPublicationCalls{0};
enum class PublisherFault {None,Mapping,View,Event,Wait};
static std::atomic<PublisherFault> publisherFault{PublisherFault::None};
static bool publisherTakeFault(PublisherFault value){auto expected=value;return publisherFault.compare_exchange_strong(expected,PublisherFault::None);}
static HANDLE WINAPI publisherCreateMapping(HANDLE file,LPSECURITY_ATTRIBUTES sa,DWORD protect,DWORD high,DWORD low,LPCWSTR name){
    if(publisherTakeFault(PublisherFault::Mapping)){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}
    HANDLE h=::CreateFileMappingW(file,sa,protect,high,low,name);if(h){std::lock_guard<std::mutex> lock(publisherResourceMx);publisherMappings.insert(h);++publisherMappingsCreated;}return h;
}
static LPVOID WINAPI publisherMapView(HANDLE h,DWORD access,DWORD high,DWORD low,SIZE_T bytes){
    if(publisherTakeFault(PublisherFault::View)){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}
    void* view=::MapViewOfFile(h,access,high,low,bytes);if(view){std::lock_guard<std::mutex> lock(publisherResourceMx);publisherViews.insert(view);++publisherViewsMapped;}return view;
}
static HANDLE WINAPI publisherCreateEvent(LPSECURITY_ATTRIBUTES sa,BOOL manual,BOOL state,LPCWSTR name){
    if(publisherTakeFault(PublisherFault::Event)){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}
    HANDLE h=::CreateEventW(sa,manual,state,name);if(h){std::lock_guard<std::mutex> lock(publisherResourceMx);publisherEvents.insert(h);++publisherEventsCreated;}return h;
}
static BOOL WINAPI publisherCloseHandle(HANDLE h){std::lock_guard<std::mutex> lock(publisherResourceMx);const BOOL ok=::CloseHandle(h);if(ok){if(publisherMappings.erase(h))++publisherMappingsClosed;if(publisherEvents.erase(h))++publisherEventsClosed;}return ok;}
static BOOL WINAPI publisherUnmapView(LPCVOID view){std::lock_guard<std::mutex> lock(publisherResourceMx);const BOOL ok=::UnmapViewOfFile(view);if(ok&&publisherViews.erase(view))++publisherViewsUnmapped;return ok;}
static DWORD WINAPI publisherWait(HANDLE h,DWORD ms){if(ms==3000&&publisherTakeFault(PublisherFault::Wait)){SetLastError(ERROR_INVALID_HANDLE);return WAIT_FAILED;}return ::WaitForSingleObject(h,ms);}
#define CreateFileMappingW publisherCreateMapping
#define MapViewOfFile publisherMapView
#define CreateEventW publisherCreateEvent
#define CloseHandle publisherCloseHandle
#define UnmapViewOfFile publisherUnmapView
#define WaitForSingleObject publisherWait
#include "../../native/recovery_state_publisher.h"
static std::mutex g_recoveryStateLifecycleMx;
static ymcc::recovery::StatePublisher g_recoveryStatePublisher;
static std::atomic<bool> g_recoveryStateStarted{false};
static constexpr DWORD kRecoveryStateHeartbeatMs=1000;
#if defined(YMCC_RECOVERY_OLD_PUBLISHER_FIXTURE)
static HANDLE g_recoveryStateMapping=nullptr,g_recoveryStateStopEvent=nullptr;
static std::atomic<bool> g_recoveryStateStop{false};
static std::thread g_recoveryStateThread;
#endif
#include "recovery-publisher-lifecycle.generated.h"
#undef CreateFileMappingW
#undef MapViewOfFile
#undef CreateEventW
#undef CloseHandle
#undef UnmapViewOfFile
#undef WaitForSingleObject
static const DWORD publisherMainThread=GetCurrentThreadId();
static std::atomic<bool> publisherStall{false},publisherThrowMain{false},publisherThrowWorker{false};
static HANDLE publisherEntered=nullptr,publisherRelease=nullptr,publisherWorker=nullptr,publisherHealed=nullptr;
static std::atomic<bool> publisherHadFailure{false};
static void publisherFixtureGate(){
    ++publisherPublicationCalls;
    if(GetCurrentThreadId()==publisherMainThread){if(publisherThrowMain.exchange(false))throw std::runtime_error("private initial publisher fault");return;}
    if(!publisherEntered)return;
    if(!publisherWorker)publisherWorker=OpenThread(SYNCHRONIZE,FALSE,GetCurrentThreadId());
    SetEvent(publisherEntered);
    if(publisherStall.load()&&WaitForSingleObject(publisherRelease,15000)!=WAIT_OBJECT_0)throw std::runtime_error("private publisher stall was not returned");
    if(publisherThrowWorker.exchange(false)){publisherHadFailure=true;throw std::runtime_error("private publisher worker fault");}
    if(publisherHadFailure.load())SetEvent(publisherHealed);
}
static bool publisherResourcesBalanced(){std::lock_guard<std::mutex> lock(publisherResourceMx);return publisherMappings.empty()&&publisherEvents.empty()&&publisherViews.empty()&&publisherMappingsCreated==publisherMappingsClosed&&publisherEventsCreated==publisherEventsClosed&&publisherViewsMapped==publisherViewsUnmapped;}
struct PublisherFixtureScope {
    SnapshotChannel channel;
    PublisherFixtureScope(){
        recoveryCheck(publisherResourcesBalanced(),"publisher fixture begins without retained resources");
        publisherFault=PublisherFault::None;publisherStall=false;publisherThrowMain=false;publisherThrowWorker=false;
        g_exitRequested=false;g_recoveryStateExitIntent=false;g_powerLifecycle=PowerLifecycle::Ready;g_powerGeneration=7;nativeGate=true;moveNativeAuthority=false;
        publisherHadFailure=false;publisherEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);publisherRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);publisherHealed=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        recoveryCheck(publisherEntered&&publisherRelease&&publisherHealed,"publisher private thread gates created");
    }
    void enter(){recoveryStatePublisherStart();recoveryCheck(g_recoveryStateStarted.load()&&g_recoveryStateView,"actual main publisher start succeeds");recoveryCheck(WaitForSingleObject(publisherEntered,5000)==WAIT_OBJECT_0&&publisherWorker,"actual publisher worker reached owned gate");}
    void returned(){recoveryCheck(publisherWorker&&WaitForSingleObject(publisherWorker,5000)==WAIT_OBJECT_0,"actual publisher worker returned");CloseHandle(publisherWorker);publisherWorker=nullptr;ResetEvent(publisherEntered);}
    void stop(){recoveryStatePublisherStop();if(publisherWorker)returned();recoveryCheck(!g_recoveryStateStarted.load()&&!g_recoveryStateView&&publisherResourcesBalanced(),"main stop returns all owned mapping view event and thread resources");}
    ~PublisherFixtureScope(){
        publisherFault=PublisherFault::None;publisherStall=false;publisherThrowMain=false;publisherThrowWorker=false;SetEvent(publisherRelease);
        recoveryStatePublisherStop();if(publisherWorker){if(WaitForSingleObject(publisherWorker,5000)!=WAIT_OBJECT_0)std::terminate();CloseHandle(publisherWorker);publisherWorker=nullptr;}
#if defined(YMCC_RECOVERY_OLD_PUBLISHER_FIXTURE)
        if(g_recoveryStateThread.joinable())g_recoveryStateThread.join();
        for(const auto view:std::vector<const void*>(publisherViews.begin(),publisherViews.end()))publisherUnmapView(view);
        for(const auto h:std::vector<HANDLE>(publisherEvents.begin(),publisherEvents.end()))publisherCloseHandle(h);
        for(const auto h:std::vector<HANDLE>(publisherMappings.begin(),publisherMappings.end()))publisherCloseHandle(h);
        g_recoveryStateView=nullptr;g_recoveryStateMapping=nullptr;g_recoveryStateStopEvent=nullptr;g_recoveryStateStarted=false;
#endif
        snapshotChannelClose(channel);CloseHandle(publisherEntered);CloseHandle(publisherRelease);CloseHandle(publisherHealed);publisherEntered=publisherRelease=publisherHealed=nullptr;
        if(!publisherResourcesBalanced())std::terminate();
    }
};


static unsigned recoveryChecks=0,recoveryReturned=0;
static void recoveryCheck(bool condition,const char* message){++recoveryChecks;if(!condition)throw std::runtime_error(message);}
struct RecoveryMappingScope {
    HANDLE mapping=nullptr;RecoveryStateWithPower* full=nullptr;SnapshotChannel channel;bool legacy=false,prefixOnly=false;
    explicit RecoveryMappingScope(bool old=false,bool powerPrefix=false):legacy(old),prefixOnly(powerPrefix){
        recoveryFixtureNow=100000;nativeGate=true;moveNativeAuthority=false;revokeDuringCapture=false;terminalPosts=0;recoveryDispatchStorage=configuredRecoveryDispatch();
        g_systemSleeping=false;g_lastWakeTick=0;g_lastSuspendTick=0;g_recoveryPowerObservationSerial=0;g_recoveryPowerAppliedSerial=0;
        g_powerLifecycle=PowerLifecycle::Ready;g_powerGeneration=7;clearPowerResumeReady();g_sgLatestSleepEntryFileTime=0;
        g_sgResumeClassification.generation=0;g_sgWakeClassificationBlockedGeneration=0;
        g_exitRequested=false;g_inputReady=false;g_recoveryResumeCommit.record({6,1,90000});g_lastPowerResumeCommitTick=90000;
        g_recoveryStateBusyDepth=0;g_recoveryStateExitIntent=false;g_recoveryUiDispatchPulse=42;
        g_recoveryStateStartKey=processStartKeyOf(GetCurrentProcess());g_sharedPowerPublicationRevision=1;g_sharedPowerSuspendSerial=1;
        mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,legacy?sizeof(RecoveryStateSnapshot):prefixOnly?offsetof(RecoveryStateWithPower,boundary):sizeof(RecoveryStateWithPower),snapshotName(GetCurrentProcessId()).c_str());
        recoveryCheck(mapping&&GetLastError()!=ERROR_ALREADY_EXISTS,"private original recovery mapping has unique ownership");
        full=static_cast<RecoveryStateWithPower*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,legacy?sizeof(RecoveryStateSnapshot):prefixOnly?offsetof(RecoveryStateWithPower,boundary):sizeof(RecoveryStateWithPower)));
        recoveryCheck(full!=nullptr,"real recovery mapping opened");
        ZeroMemory(full,legacy?sizeof(RecoveryStateSnapshot):prefixOnly?offsetof(RecoveryStateWithPower,boundary):sizeof(RecoveryStateWithPower));g_recoveryStateView=&full->recovery;if(!legacy&&!prefixOnly)recoveryCheck(g_recoveryPowerBoundary.attach(&full->boundary),"Native boundary attaches to actual owned mapping");
    }
    void publish(){
        if(!legacy&&!prefixOnly){recoveryStatePublishOnce();return;}
        auto& p=full->recovery;++p.sequence;p.magic=kSnapshotMagic;p.schemaVersion=1;p.pid=GetCurrentProcessId();p.processStartKey=g_recoveryStateStartKey;p.lastNativeHeartbeat=recoveryFixtureNow;p.reserved0=ymcc::encodeRecoveryUiPulse(42);strcpy_s(p.lifecyclePhase,"idle");++p.sequence;
    }
    SnapshotObservation read(HANDLE process=GetCurrentProcess()){SnapshotObservation value;(void)readRecoverySnapshot(channel,process,GetCurrentProcessId(),value);return value;}
    void suspend(){powerNotifyCallback(nullptr,PBT_APMSUSPEND,nullptr);}
    void commit(){recoveryFixtureNow+=1000;g_powerLifecycle=PowerLifecycle::Resuming;nativeGate=false;const auto generation=g_powerGeneration.load();const auto ticket=recordPowerResumeReady(generation,capturePowerResumeBoundary(),true);recoveryCheck(commitPowerResume(generation,"fixture-native-owner",ticket),"complete original Native Ready commit accepted");publish();}
    ~RecoveryMappingScope(){if(!legacy&&!prefixOnly)g_recoveryPowerBoundary.detach(&full->boundary);snapshotChannelClose(channel);g_recoveryStateView=nullptr;const bool unmapped=UnmapViewOfFile(full)!=FALSE,closed=CloseHandle(mapping)!=FALSE;if(unmapped&&closed)++recoveryReturned;else std::terminate();}
};
static bool guarded(const SnapshotObservation& value){return powerDeferKill(L"",&value.powerObservation);}

#include <array>
#include <memory>
#include <cstdlib>
struct RecoveryFixtureChildIdentity {DWORD pid=0;unsigned long long birth=0;};
static std::vector<RecoveryFixtureChildIdentity> recoveryFixtureChildren;
static unsigned guardChildrenStarted=0,guardChildrenReturned=0,guardForcedCleanup=0,guardHandleCloseFailures=0;
static unsigned guardKernelCalls=0,guardKernelWithoutLock=0,guardExternalTerminateAttempts=0;
static BOOL recoveryFixtureTerminateProcess(HANDLE process,UINT code){
    const DWORD pid=GetProcessId(process);const auto birth=processStartKeyOf(process);
    const bool owned=std::any_of(recoveryFixtureChildren.begin(),recoveryFixtureChildren.end(),[&](const auto& c){return c.pid==pid&&c.birth==birth;});
    if(!owned){++guardExternalTerminateAttempts;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    if(TryAcquireSRWLockExclusive(&g_recoveryPowerObservationLock)){++guardKernelWithoutLock;ReleaseSRWLockExclusive(&g_recoveryPowerObservationLock);}
    ++guardKernelCalls;return ::TerminateProcess(process,code);
}
static void guardClose(HANDLE& h){if(h){if(!::CloseHandle(h))++guardHandleCloseFailures;h=nullptr;}}
struct GuardFixtureHandle {HANDLE value=nullptr;~GuardFixtureHandle(){guardClose(value);}};
struct OwnRecoveryGuardChild {
    HANDLE process=nullptr,stop=nullptr,ready=nullptr,command=nullptr,ack=nullptr;DWORD pid=0;unsigned long long birth=0;
    explicit OwnRecoveryGuardChild(bool publish=true,bool liveControl=false){
        stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        GuardFixtureHandle childStop,childReady,parentLife,childCommand,childAck;PROCESS_INFORMATION pi{};std::vector<unsigned char> attributes;
        try{
            if(!stop||!ready||!DuplicateHandle(GetCurrentProcess(),stop,GetCurrentProcess(),&childStop.value,SYNCHRONIZE,TRUE,0)||
               !DuplicateHandle(GetCurrentProcess(),ready,GetCurrentProcess(),&childReady.value,EVENT_MODIFY_STATE,TRUE,0)||
               !DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&parentLife.value,SYNCHRONIZE,TRUE,0))throw std::runtime_error("owned child handle preparation failed");
            if(liveControl){command=CreateEventW(nullptr,FALSE,FALSE,nullptr);ack=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!command||!ack||!DuplicateHandle(GetCurrentProcess(),command,GetCurrentProcess(),&childCommand.value,SYNCHRONIZE,TRUE,0)||!DuplicateHandle(GetCurrentProcess(),ack,GetCurrentProcess(),&childAck.value,EVENT_MODIFY_STATE,TRUE,0))throw std::runtime_error("private boundary command handles failed");}
            SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);if(!bytes)throw std::runtime_error("owned child attribute size absent");attributes.resize(bytes);
            auto* list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());if(!InitializeProcThreadAttributeList(list,1,0,&bytes))throw std::runtime_error("owned child attributes failed");
            std::vector<HANDLE> inherited{childStop.value,childReady.value,parentLife.value};if(liveControl){inherited.push_back(childCommand.value);inherited.push_back(childAck.value);}
            const bool bound=UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,const_cast<HANDLE*>(inherited.data()),inherited.size()*sizeof(HANDLE),nullptr,nullptr)!=FALSE;
            std::wstring module(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));if(!n||n>=module.size()){DeleteProcThreadAttributeList(list);throw std::runtime_error("owned child image unavailable");}module.resize(n);
            std::wstring commandLine=quoteArg(module)+(liveControl?L" --recovery-power-owned-child-live ":publish?L" --recovery-power-owned-child ":L" --recovery-power-owned-child-nomap ")+std::to_wstring(reinterpret_cast<uintptr_t>(childStop.value))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(childReady.value))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(parentLife.value));if(liveControl)commandLine+=L" "+std::to_wstring(reinterpret_cast<uintptr_t>(childCommand.value))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(childAck.value));
            STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=list;
            const bool created=bound&&CreateProcessW(module.c_str(),commandLine.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&pi)!=FALSE;
            DeleteProcThreadAttributeList(list);if(!created)throw std::runtime_error("owned child creation failed");
            process=pi.hProcess;pid=pi.dwProcessId;guardClose(pi.hThread);birth=processStartKeyOf(process);++guardChildrenStarted;recoveryFixtureChildren.push_back({pid,birth});
            if(!birth||WaitForSingleObject(ready,5000)!=WAIT_OBJECT_0||!processAlive(process))throw std::runtime_error("owned child did not reach its private ready event");
        }catch(...){cleanup();throw;}
    }
    void step(){if(!command||!ack||!SetEvent(command)||WaitForSingleObject(ack,5000)!=WAIT_OBJECT_0||!processAlive(process))throw std::runtime_error("private Native command did not complete");}
    void cleanup()noexcept{
        if(process){
            if(WaitForSingleObject(process,0)==WAIT_TIMEOUT){if(stop)SetEvent(stop);if(WaitForSingleObject(process,5000)!=WAIT_OBJECT_0){++guardForcedCleanup;::TerminateProcess(process,0xE102);WaitForSingleObject(process,5000);}}
            if(WaitForSingleObject(process,0)==WAIT_OBJECT_0)++guardChildrenReturned;
            recoveryFixtureChildren.erase(std::remove_if(recoveryFixtureChildren.begin(),recoveryFixtureChildren.end(),[&](const auto& c){return c.pid==pid&&c.birth==birth;}),recoveryFixtureChildren.end());
        }
        guardClose(process);guardClose(stop);guardClose(ready);guardClose(command);guardClose(ack);
    }
    ~OwnRecoveryGuardChild(){cleanup();}
};
static int runRecoveryGuardOwnedChild(int argc,char** argv){
    if(argc!=5&&argc!=7)return 2;auto handle=[](const char* text){char* end=nullptr;const auto value=std::strtoull(text,&end,10);return value&&end&&!*end?reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value)):nullptr;};
    HANDLE stop=handle(argv[2]),ready=handle(argv[3]),parent=handle(argv[4]);if(!stop||!ready||!parent)return 2;
    HANDLE command=argc==7?handle(argv[5]):nullptr,ack=argc==7?handle(argv[6]):nullptr;
    try{std::unique_ptr<RecoveryMappingScope> native;if(std::string(argv[1])!="--recovery-power-owned-child-nomap"){native=std::make_unique<RecoveryMappingScope>();native->publish();}const HANDLE waits[]{stop,parent,command};if(!SetEvent(ready))throw std::runtime_error("private child ready failed");unsigned step=0;for(;;){const auto result=WaitForMultipleObjects(command?3:2,waits,FALSE,INFINITE);if(result==WAIT_OBJECT_0||result==WAIT_OBJECT_0+1)break;if(result!=WAIT_OBJECT_0+2||!native||!ack)throw std::runtime_error("private Native command wait failed");recoveryFixtureNow+=1000;++step;
        if(step==1)g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,step,[](auto){return false;});
        else if(step==2){recoveryActualNonReady(2);nativeGate=false;native->publish();}
        else if(step==3){auto receipt=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPENDFAILED,step);g_sleepPowerDispatch.complete(receipt,step+1);recoveryActualNonReady(3);native->publish();}
        else if(step==4){const auto generation=g_powerGeneration.load();const auto ticket=recordPowerResumeReady(generation,capturePowerResumeBoundary(),true);if(!commitPowerResume(generation,"private-native-command",ticket))throw std::runtime_error("private Native Ready commit failed");native->publish();}
        else if(step==5)g_recoveryPowerBoundary.detach(&native->full->boundary);
        else if(step==6){if(!g_recoveryPowerBoundary.attach(&native->full->boundary))throw std::runtime_error("private Native reattach failed");native->publish();}
        else if(step==7){g_exitRequested=true;closeSharedPowerMirrors();}
        else throw std::runtime_error("unknown private Native command");
        if(!SetEvent(ack))throw std::runtime_error("private Native command ack failed");
    }guardClose(stop);guardClose(ready);guardClose(parent);guardClose(command);guardClose(ack);return 0;}catch(...){guardClose(stop);guardClose(ready);guardClose(parent);guardClose(command);guardClose(ack);return 3;}
}
struct RecoveryGuardConsumer {
    Options options;HANDLE process=nullptr;SnapshotChannel channel;const std::wstring emptyLog;
    std::wstring log;ymcc::sleeppower::RecoveryTerminationPermit terminationPermit;RecoveryOps ops;unsigned spawns=0;
    explicit RecoveryGuardConsumer(OwnRecoveryGuardChild& child){
        recoveryFixtureNow=100000;g_systemSleeping=false;g_lastWakeTick=0;g_lastSuspendTick=0;g_recoveryPowerObservationSerial=0;g_recoveryPowerAppliedSerial=0;g_recoveryLastSuspendSerial=0;
        options.pid=child.pid;process=child.process;
        ops.processAlive=[&]{return processAlive(process);};ops.waitTargetExit=[&]{return targetTerminationSettled(process);};
        ops.ownerProbe=[] {return OwnerProbe{};};ops.spawnTarget=[&]{++spawns;return true;};ops.now=ops.uiNow=[]{return recoveryFixtureNow;};ops.sleep=[](DWORD ms){recoveryFixtureNow+=ms;};
        ops.restartPower=[&](const std::function<void()>& launch){return withRecoveryRestartPower(terminationPermit,launch);};
#include "recovery-final-gate.generated.h"
#include "recovery-terminate-target.generated.h"
    }
    SnapshotObservation observation(){SnapshotObservation result;(void)readRecoverySnapshot(channel,process,options.pid,result);return result;}
    bool capture(){return !ops.powerDeferred();}
    ~RecoveryGuardConsumer(){snapshotChannelClose(channel);}
};


int runRecoverySharedPowerCases(int argc,char** argv){
    if((argc==5||argc==7)&&(std::string(argv[1])=="--recovery-power-owned-child"||std::string(argv[1])=="--recovery-power-owned-child-nomap"||std::string(argv[1])=="--recovery-power-owned-child-live"))return runRecoveryGuardOwnedChild(argc,argv);
    using nlohmann::json;
    std::vector<std::pair<std::string,std::function<void()>>> cases;
    cases.emplace_back("missed-resume-uses-current-native-commit",[]{RecoveryMappingScope owner;owner.suspend();owner.commit();auto value=owner.read();recoveryCheck(value.state==SnapshotRead::Ok&&value.powerObservation.processBound,"original reader binds native projection to actual held process");recoveryCheck(guarded(value)&&!g_systemSleeping,"new native commit reconciles missed Resume and preserves wake grace");const auto wake=g_lastWakeTick.load();recoveryFixtureNow=wake+kWakeGraceMs;owner.publish();recoveryCheck(!guarded(owner.read())&&g_lastWakeTick==wake,"same projection cannot refresh the original fifteen-second grace");});
    cases.emplace_back("old-commit-cannot-release-current-suspend",[]{RecoveryMappingScope owner;owner.suspend();g_recoveryResumeCommit.record({7,1,90000});owner.publish();recoveryFixtureNow+=300000;owner.publish();recoveryCheck(guarded(owner.read())&&g_systemSleeping,"old native commit cannot clear a newer real Suspend even after a long wait");});
    cases.emplace_back("native-ready-without-commit-is-not-wake",[]{RecoveryMappingScope owner;owner.suspend();recoveryFixtureNow+=1000;owner.publish();recoveryCheck(guarded(owner.read())&&g_systemSleeping,"Ready and fresh publisher heartbeat alone cannot prove wake");});
    cases.emplace_back("stale-projection-cannot-clear-sleep",[]{RecoveryMappingScope owner;owner.suspend();owner.commit();recoveryFixtureNow+=kHeartbeatStaleMs+1;recoveryCheck(guarded(owner.read())&&g_systemSleeping,"expired original publisher observation cannot reconcile sleep");});
    cases.emplace_back("future-publication-is-not-fresh",[]{RecoveryMappingScope owner;owner.suspend();owner.commit();owner.full->recovery.lastNativeHeartbeat=recoveryFixtureNow+1;recoveryCheck(guarded(owner.read())&&g_systemSleeping,"future publisher time is not fresh wake proof");});
    cases.emplace_back("commit-after-publication-is-not-coherent",[]{RecoveryMappingScope owner;owner.suspend();owner.commit();owner.full->power.committedTick=recoveryFixtureNow+1;recoveryCheck(guarded(owner.read())&&g_systemSleeping,"commit newer than its published observation is rejected");});
    cases.emplace_back("old-generation-cannot-borrow-new-ready",[]{RecoveryMappingScope owner;owner.suspend();owner.commit();owner.full->power.committedGeneration=6;recoveryCheck(guarded(owner.read())&&g_systemSleeping,"old generation commit cannot be paired with newer Ready");});
    cases.emplace_back("nonready-owner-cannot-release-sleep",[]{RecoveryMappingScope owner;owner.suspend();owner.commit();for(unsigned phase=1;phase!=4;++phase){owner.full->power.power.phase=phase;recoveryCheck(guarded(owner.read())&&g_systemSleeping,"all original non-Ready phases retain sleep protection");}});
    cases.emplace_back("pending-native-notification-retains-sleep",[]{RecoveryMappingScope owner;owner.suspend();owner.commit();owner.full->power.power.flags|=ymcc::sleeppower::kNotificationPending;recoveryCheck(guarded(owner.read())&&g_systemSleeping,"pending native notification cannot publish an admitted wake");});
    cases.emplace_back("projection-abi-unknown-retains-sleep",[]{RecoveryMappingScope owner;owner.suspend();owner.commit();owner.full->power.version=2;recoveryCheck(guarded(owner.read())&&g_systemSleeping,"unknown projection ABI retains original sleep protection");owner.full->power.version=1;owner.full->power.size=0;recoveryCheck(guarded(owner.read())&&g_systemSleeping,"short projection cannot serve as proof");});
    cases.emplace_back("legacy-152-byte-publisher-remains-compatible",[]{RecoveryMappingScope owner(true);owner.suspend();owner.publish();const auto value=owner.read();recoveryCheck(value.state==SnapshotRead::Ok&&value.uiPulseKnown,"original legacy recovery snapshot remains readable");recoveryCheck(guarded(value)&&g_systemSleeping,"legacy mapping never manufactures a shared wake witness");powerNotifyCallback(nullptr,PBT_APMRESUMEAUTOMATIC,nullptr);recoveryCheck(powerDeferKill(L""),"legacy direct OS Resume preserves wake grace");});
    cases.emplace_back("wrong-process-start-key-cannot-reconcile",[]{RecoveryMappingScope owner;owner.suspend();owner.commit();++owner.full->recovery.processStartKey;const auto value=owner.read();recoveryCheck(value.state==SnapshotRead::Unavailable&&guarded(value)&&g_systemSleeping,"PID reuse or old process mapping cannot clear current sleep");});
    cases.emplace_back("unbound-process-cannot-borrow-projection",[]{RecoveryMappingScope owner;owner.suspend();owner.commit();const auto value=owner.read(nullptr);recoveryCheck(value.state==SnapshotRead::Ok&&!value.powerObservation.processBound&&guarded(value),"legacy unknown process identity may be read but never grants new wake proof");});
    cases.emplace_back("torn-publication-keeps-sleep-protection",[]{RecoveryMappingScope owner;owner.suspend();owner.commit();++owner.full->recovery.sequence;const auto value=owner.read();recoveryCheck(value.state!=SnapshotRead::Ok&&value.unavailableReason=="torn-read"&&guarded(value)&&g_systemSleeping,"original seqlock refuses a partially published power tail");});
    cases.emplace_back("late-suspend-cannot-reuse-prior-wake",[]{RecoveryMappingScope owner;owner.suspend();owner.commit();recoveryCheck(guarded(owner.read())&&!g_systemSleeping,"first current wake is admitted under grace");++recoveryFixtureNow;owner.suspend();owner.publish();recoveryCheck(guarded(owner.read())&&g_systemSleeping,"later Suspend conservatively retires the older commit proof");});
    cases.emplace_back("duplicate-ready-cannot-renew-commit-witness",[]{RecoveryMappingScope owner;owner.suspend();owner.commit();const auto tick=g_lastPowerResumeCommitTick.load();const auto stamp=g_recoveryResumeCommit.read();recoveryFixtureNow+=10000;recoveryCheck(commitPowerResume(7,"fixture-duplicate",0)&&g_lastPowerResumeCommitTick==tick&&stamp.generation==7&&g_recoveryResumeCommit.read()==stamp,"original already-Ready branch never mints a fresh wake witness");});
    cases.emplace_back("invalid-commit-cannot-mint-wake-proof",[]{RecoveryMappingScope owner;owner.suspend();const auto stamp=g_recoveryResumeCommit.read();g_powerLifecycle=PowerLifecycle::Resuming;recoveryCheck(!commitPowerResume(7,"fixture-invalid",1)&&g_recoveryResumeCommit.read()==stamp,"unready generation cannot publish a successful commit witness");const auto ticket=recordPowerResumeReady(7,capturePowerResumeBoundary(),true);g_exitRequested=true;recoveryCheck(ticket&&!commitPowerResume(7,"fixture-exit",ticket)&&g_recoveryResumeCommit.read()==stamp,"exit forbids a new commit witness");});
    cases.emplace_back("moving-native-capture-stays-unknown",[]{RecoveryMappingScope owner;moveNativeAuthority=true;const auto value=captureRecoveryPowerProjection();recoveryCheck(value.power.generation==0&&value.committedTick==0,"complete original native read rejects moving capture rather than inventing Ready");});
    cases.emplace_back("new-generation-does-not-relabel-old-tick",[]{RecoveryMappingScope owner;owner.suspend();g_powerGeneration=8;g_powerLifecycle=PowerLifecycle::Ready;const auto value=captureRecoveryPowerProjection();recoveryCheck(value.power.generation==8&&value.committedGeneration==0&&value.committedTick==0,"old committed tick cannot be relabelled with a newer Ready generation");});
    cases.emplace_back("same-generation-ready-cannot-relabel-revoked-commit",[]{
        RecoveryMappingScope owner;
        owner.suspend();owner.commit();
        const auto old=owner.read();
        const auto generation=old.powerObservation.projection.power.generation;
        const auto serial=old.powerObservation.projection.power.suspendSerial;
        recoveryCheck(old.powerObservation.projection.committedTick!=0,"original Ready carried a wake commit before revocation");
        // The native query can close and reopen the write gate without a new generation or Ready commit.
        nativeGate=false;g_sharedPowerSuspendSerial.fetch_add(1);revokeSharedPowerMirrors();nativeGate=true;owner.publish();
        const auto coalesced=owner.read();
        const auto& power=coalesced.powerObservation.projection;
        recoveryCheck(power.power.generation==generation&&power.power.suspendSerial==serial+1&&ymcc::sleeppower::powerAdmitted(power.power),"a current Ready can carry a newer suspend serial in the same generation");
        recoveryCheck(!old.powerObservation.boundary.current()&&power.committedTick==0&&power.committedGeneration==0,"a current mapping must not relabel the old commit with the new suspend serial");
        recoveryCheck(guarded(coalesced)&&g_systemSleeping,"a retired wake commit must not clear the local suspend latch");
        owner.commit();const auto current=owner.read();
        recoveryCheck(guarded(current)&&!g_systemSleeping,"a genuine commit in the current suspend serial clears the latch under grace");
        recoveryFixtureNow+=kWakeGraceMs;owner.publish();
        recoveryCheck(!guarded(owner.read()),"the current commit restores the watchdog after the original grace");
    });
    cases.emplace_back("current-wake-restores-original-watchdog-decision",[]{RecoveryMappingScope owner;owner.suspend();owner.commit();recoveryCheck(guarded(owner.read())&&!g_systemSleeping,"missed resume settles before original watchdog checks");recoveryFixtureNow+=kWakeGraceMs;RecoveryOps ops;unsigned kills=0,spawns=0;ops.now=ops.uiNow=[]{return recoveryFixtureNow;};ops.processAlive=[]{return true;};ops.terminateTarget=[&]{++kills;return true;};ops.waitTargetExit=[]{return true;};ops.ownerProbe=[]{return OwnerProbe{};};ops.spawnTarget=[&]{++spawns;return true;};ops.sleep=[](DWORD ms){recoveryFixtureNow+=ms;};ops.powerDeferred=[&]{owner.publish();return guarded(owner.read());};RecoveryEpisodeState episode;for(unsigned i=0;i!=11;++i){owner.publish();auto value=owner.read();const auto result=recoveryDecisionRound(value,episode,ops,L"");recoveryCheck(result==(i==10?RoundResult::Stop:RoundResult::Continue),"original thirty-second UI observation remains unchanged");recoveryFixtureNow+=3000;}recoveryCheck(kills==1&&spawns==1,"fresh wake permits exactly one original mocked watchdog recovery");});
    cases.emplace_back("final-power-fence-defers-and-keeps-observing",[]{RecoveryMappingScope owner;owner.publish();auto value=owner.read();RecoveryOps ops;unsigned kills=0;Options options;options.pid=GetCurrentProcessId();HANDLE process=GetCurrentProcess();SnapshotChannel& channel=owner.channel;const std::wstring log;ymcc::sleeppower::RecoveryTerminationPermit terminationPermit;
    ops.processAlive=[&]{owner.suspend();return true;};ops.terminateTarget=[&]{++kills;return false;};
#include "recovery-final-gate.generated.h"
    RecoveryEpisodeState episode;recoveryCheck(recoveryPerformRestart(value,episode,ops,L"")==RoundResult::Continue&&kills==0,"new Suspend at the original final termination boundary cancels kill without stopping observer");});
    cases.emplace_back("termination-suspend-after-check-before-real-api",[]{OwnRecoveryGuardChild child;RecoveryGuardConsumer service(child);const auto observed=service.observation();const auto original=service.ops.powerDeferred;service.ops.powerDeferred=[&]{const bool blocked=original();powerNotifyCallback(nullptr,PBT_APMSUSPEND,nullptr);return blocked;};RecoveryEpisodeState episode;const auto outcome=recoveryPerformRestart(observed,episode,service.ops,L"");recoveryCheck(outcome==RoundResult::Continue&&processAlive(child.process)&&service.spawns==0,"completed Suspend in final API gap must preserve owned child");recoveryCheck(!service.terminationPermit.accepted,"superseded termination request is consumed");});
    cases.emplace_back("termination-resume-invalidates-old-permit-after-grace",[]{OwnRecoveryGuardChild child;RecoveryGuardConsumer service(child);recoveryCheck(service.capture(),"original final reader captures current process-bound request");powerNotifyCallback(nullptr,PBT_APMRESUMEAUTOMATIC,nullptr);recoveryFixtureNow+=kWakeGraceMs+1;recoveryCheck(!service.ops.terminateTarget()&&processAlive(child.process),"old request remains rejected after new Resume grace expires");});
    cases.emplace_back("termination-current-permit-reaches-real-owned-api",[]{OwnRecoveryGuardChild child;RecoveryGuardConsumer service(child);RecoveryEpisodeState episode;const auto before=guardKernelCalls;const auto result=recoveryPerformRestart(service.observation(),episode,service.ops,L"");DWORD code=0;recoveryCheck(result==RoundResult::Stop&&WaitForSingleObject(child.process,0)==WAIT_OBJECT_0&&GetExitCodeProcess(child.process,&code)&&code==0xE001,"current request reaches exactly the original real termination and return");recoveryCheck(guardKernelCalls==before+1&&!service.terminationPermit.accepted&&service.spawns==1,"one real owned termination consumes request before original mocked restart");});
    cases.emplace_back("termination-kernel-failure-consumes-and-next-request-heals",[]{OwnRecoveryGuardChild child;RecoveryGuardConsumer service(child);GuardFixtureHandle limited;recoveryCheck(DuplicateHandle(GetCurrentProcess(),child.process,GetCurrentProcess(),&limited.value,SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,0)!=FALSE,"owned query-only target handle prepared");service.process=limited.value;service.log=L"fixture-termination-errors.log";recoveryCheck(GetFileAttributesW(service.log.c_str())==INVALID_FILE_ATTRIBUTES,"private diagnostic output is fresh");recoveryCheck(service.capture()&&!service.ops.terminateTarget()&&!service.terminationPermit.accepted&&processAlive(child.process),"real access denial consumes only this request without killing child");{std::ifstream input(service.log);nlohmann::json entry;input>>entry;recoveryCheck(entry.at("error")==ERROR_ACCESS_DENIED&&entry.at("reason")=="target-terminate-failed","original failure diagnostic preserves the kernel error");}service.process=child.process;recoveryCheck(!service.ops.terminateTarget()&&processAlive(child.process),"consumed failed request cannot replay on a stronger handle");recoveryCheck(service.capture()&&service.ops.terminateTarget()&&targetTerminationSettled(child.process),"new current observation heals original real failure without self-lock");});
    cases.emplace_back("termination-permit-binds-exact-held-process",[]{OwnRecoveryGuardChild first,second;RecoveryGuardConsumer service(first);recoveryCheck(service.capture(),"first owned process request captured");service.process=second.process;recoveryCheck(!service.ops.terminateTarget()&&processAlive(first.process)&&processAlive(second.process),"request cannot terminate a different held process");service.process=first.process;recoveryCheck(service.capture()&&service.ops.terminateTarget()&&targetTerminationSettled(first.process)&&processAlive(second.process),"fresh request heals while preserving independent second child");});
    cases.emplace_back("termination-missing-final-mapping-stays-observing",[]{OwnRecoveryGuardChild child(false);RecoveryGuardConsumer service(child);recoveryCheck(!service.capture()&&!service.terminationPermit.accepted,"missing final production mapping cannot grant a termination request");recoveryCheck(!service.ops.terminateTarget()&&processAlive(child.process),"unavailable final observation never reaches actual kernel termination");});
    cases.emplace_back("termination-callback-on-another-thread-revokes-request",[]{OwnRecoveryGuardChild child;RecoveryGuardConsumer service(child);recoveryCheck(service.capture(),"async boundary current request captured");std::thread callback([]{powerNotifyCallback(nullptr,PBT_APMSUSPEND,nullptr);});callback.join();recoveryCheck(!service.ops.terminateTarget()&&processAlive(child.process),"completed concurrent callback fences real termination");});
    cases.emplace_back("termination-counter-exhaustion-is-fail-closed",[]{OwnRecoveryGuardChild child;RecoveryGuardConsumer service(child);g_recoveryPowerObservationSerial=(std::numeric_limits<std::uint64_t>::max)();powerNotifyCallback(nullptr,PBT_APMRESUMEAUTOMATIC,nullptr);recoveryFixtureNow+=kWakeGraceMs+1;recoveryCheck(!service.capture()&&!service.terminationPermit.accepted&&!service.ops.terminateTarget()&&processAlive(child.process),"notification serial cannot wrap into reusable termination authority");});

    cases.emplace_back("publisher-start-stop-restart-original-reader",[]{PublisherFixtureScope owner;owner.enter();const auto maps=publisherMappingsCreated.load();recoveryStatePublisherStart();recoveryCheck(publisherMappingsCreated==maps,"duplicate start does not allocate another mapping or worker");SnapshotObservation observed;recoveryCheck(readRecoverySnapshot(owner.channel,GetCurrentProcess(),GetCurrentProcessId(),observed)==SnapshotRead::Ok,"actual service reader accepts actual publisher snapshot");owner.stop();recoveryStatePublisherStop();owner.enter();owner.stop();});
    cases.emplace_back("publisher-timeout-retains-event-and-view",[]{PublisherFixtureScope owner;publisherStall=true;owner.enter();const auto events=publisherEventsClosed.load(),views=publisherViewsUnmapped.load(),maps=publisherMappingsCreated.load();recoveryStatePublisherStop();recoveryCheck(publisherEventsClosed==events&&publisherViewsUnmapped==views,"timed-out worker retains its live stop event and mapped view");recoveryCheck(!g_recoveryStateStarted.load()&&!g_recoveryStateView,"timed-out run is retired from new observation");recoveryStatePublisherStart();recoveryCheck(!g_recoveryStateStarted.load()&&publisherMappingsCreated==maps,"restart is refused while the original worker still owns resources");publisherStall=false;SetEvent(publisherRelease);owner.returned();owner.enter();owner.stop();});
    cases.emplace_back("publisher-wait-failure-retains-until-return",[]{PublisherFixtureScope owner;publisherStall=true;owner.enter();const auto events=publisherEventsClosed.load();publisherFault=PublisherFault::Wait;recoveryStatePublisherStop();recoveryCheck(publisherEventsClosed==events,"failed thread wait cannot free a live worker stop event");publisherStall=false;SetEvent(publisherRelease);owner.returned();owner.stop();owner.enter();owner.stop();});
    cases.emplace_back("publisher-partial-resource-failure-heals",[]{PublisherFixtureScope owner;for(const auto fault:{PublisherFault::Mapping,PublisherFault::View,PublisherFault::Event}){publisherFault=fault;recoveryStatePublisherStart();recoveryCheck(!g_recoveryStateStarted.load()&&!g_recoveryStateView&&publisherResourcesBalanced(),"partial startup failure returns every acquired resource");owner.enter();owner.stop();}});
    cases.emplace_back("publisher-initial-publication-exception-heals",[]{PublisherFixtureScope owner;publisherThrowMain=true;recoveryStatePublisherStart();recoveryCheck(!g_recoveryStateStarted.load()&&!g_recoveryStateView&&publisherResourcesBalanced(),"initial publication exception returns mapping view event without a worker");owner.enter();owner.stop();});
    cases.emplace_back("publisher-worker-publication-exception-heals",[]{PublisherFixtureScope owner;publisherThrowWorker=true;const auto before=publisherMappingsCreated.load();owner.enter();recoveryCheck(WaitForSingleObject(publisherHealed,5000)==WAIT_OBJECT_0,"original heartbeat retries a transient worker publication exception");recoveryCheck(g_recoveryStatePublisher.running()&&publisherMappingsCreated==before+1,"self-heal keeps the original worker mapping and heartbeat");SnapshotObservation observed;recoveryCheck(readRecoverySnapshot(owner.channel,GetCurrentProcess(),GetCurrentProcessId(),observed)==SnapshotRead::Ok,"self-heal commits an even coherent snapshot after interrupted publication");owner.stop();publisherHadFailure=false;ResetEvent(publisherHealed);ResetEvent(publisherRelease);publisherStall=true;auto local=std::make_unique<ymcc::recovery::StatePublisher>();const auto name=snapshotName(GetCurrentProcessId())+L".owned-exit";recoveryCheck(local->start(name.c_str(),sizeof(RecoveryStateWithPower),kRecoveryStateHeartbeatMs,[](void* view){recoveryStatePublishInto(static_cast<RecoveryStateSnapshot*>(view));}),"private exit owner starts actual publisher body");recoveryCheck(WaitForSingleObject(publisherEntered,5000)==WAIT_OBJECT_0&&publisherWorker,"private exit worker is blocked at owned gate");const auto events=publisherEventsClosed.load(),views=publisherViewsUnmapped.load();local.reset();recoveryCheck(publisherEventsClosed==events&&publisherViewsUnmapped==views,"destroyed owner leaves every in-use resource with its detached worker");publisherStall=false;SetEvent(publisherRelease);owner.returned();recoveryCheck(publisherResourcesBalanced(),"detached worker naturally returns its mapping view and stop event after owner destruction");});
    cases.emplace_back("publisher-exit-intent-refuses-new-start",[]{PublisherFixtureScope owner;const auto maps=publisherMappingsCreated.load();g_exitRequested=true;recoveryStatePublisherStart();recoveryCheck(!g_recoveryStateStarted.load()&&publisherMappingsCreated==maps,"process exit refuses a new publisher");g_exitRequested=false;g_recoveryStateExitIntent=true;recoveryStatePublisherStart();recoveryCheck(!g_recoveryStateStarted.load()&&publisherMappingsCreated==maps,"recovery exit intent refuses a new publisher");g_recoveryStateExitIntent=false;owner.enter();owner.stop();});
    cases.emplace_back("publisher-queued-publication-cannot-revive-retired-run",[]{PublisherFixtureScope owner;publisherStall=true;owner.enter();const auto calls=publisherPublicationCalls.load();std::thread late([]{g_recoveryStatePublisher.publishOnce();});recoveryStatePublisherStop();publisherStall=false;SetEvent(publisherRelease);late.join();owner.returned();recoveryCheck(publisherPublicationCalls==calls,"queued publication cannot enter the retired run after stop");owner.enter();owner.stop();});

    cases.emplace_back("native-live-revoke-before-next-publication",[]{OwnRecoveryGuardChild child(true,true);RecoveryGuardConsumer service(child);recoveryCheck(service.capture(),"live Native Ready permits the original request");const auto calls=guardKernelCalls;child.step();recoveryCheck(!service.ops.terminateTarget()&&processAlive(child.process),"completed Native revoke after capture rejects the original final termination");recoveryCheck(guardKernelCalls==calls&&!service.capture(),"old Ready cannot mint a new request before a fresh publication");});
    cases.emplace_back("native-live-late-resume-defers-suspended-and-recovers-resuming",[]{OwnRecoveryGuardChild child(true,true);RecoveryGuardConsumer service(child);child.step();child.step();powerNotifyCallback(nullptr,PBT_APMRESUMEAUTOMATIC,nullptr);recoveryFixtureNow+=kWakeGraceMs+1;recoveryCheck(!service.capture()&&!g_systemSleeping,"late local Resume cannot override current Native Suspended");child.step();auto observed=service.observation();recoveryCheck(observed.powerObservation.projection.power.phase==3&&!(observed.powerObservation.projection.power.flags&ymcc::sleeppower::kHardwareAllowed)&&service.capture(),"current Native Resuming may recover watchdog without granting hardware Ready");child.step();auto ready=service.observation();const auto nativeWake=ready.powerObservation.projection.committedTick;recoveryCheck(!service.capture()&&g_lastWakeTick==nativeWake,"newer Native Ready commit starts the original grace after an older local Resume");recoveryFixtureNow=nativeWake+kWakeGraceMs-1;recoveryCheck(!service.capture()&&g_lastWakeTick==nativeWake,"duplicate observation cannot extend or shorten Native wake grace");recoveryFixtureNow=nativeWake+kWakeGraceMs;recoveryCheck(service.capture(),"fresh Native Ready heals after the unchanged fifteen-second grace");{OwnRecoveryGuardChild staleChild(true,true);RecoveryGuardConsumer staleService(staleChild);powerNotifyCallback(nullptr,PBT_APMSUSPEND,nullptr);for(unsigned i=0;i!=4;++i)staleChild.step();const auto stale=staleService.observation();recoveryFixtureNow=stale.powerObservation.publishedTick+kHeartbeatStaleMs+1;recoveryCheck(!staleService.capture()&&g_systemSleeping,"aging publication may add protection through its live Native fence but never clears actual sleep");}});
    cases.emplace_back("native-live-missed-resume-heals-only-from-fresh-commit",[]{OwnRecoveryGuardChild child(true,true);RecoveryGuardConsumer service(child);powerNotifyCallback(nullptr,PBT_APMSUSPEND,nullptr);for(unsigned i=0;i!=4;++i)child.step();auto observed=service.observation();recoveryFixtureNow=observed.powerObservation.publishedTick+1;recoveryCheck(!service.capture()&&!g_systemSleeping,"fresh Native commit reconciles missed local Resume while retaining grace");recoveryFixtureNow=observed.powerObservation.projection.committedTick+kWakeGraceMs;recoveryCheck(service.capture(),"the original grace ends without reusing a revoked request");});
    cases.emplace_back("native-live-readonly-channel-close-retains-and-retires-permit",[]{OwnRecoveryGuardChild child(true,true);RecoveryGuardConsumer service(child);recoveryCheck(service.capture(),"held read-only mapping captured");auto ticket=service.terminationPermit.boundary;std::weak_ptr<const std::atomic<bool>> weak=ticket.retired;const auto* address=const_cast<const LONG64*>(ticket.liveSerial);MEMORY_BASIC_INFORMATION info{};recoveryCheck(VirtualQuery(address,&info,sizeof(info))==sizeof(info)&&info.Protect==PAGE_READONLY,"service boundary uses a real read-only page");snapshotChannelClose(service.channel);recoveryCheck(!weak.expired()&&ticket.retired->load()&&VirtualQuery(address,&info,sizeof(info))==sizeof(info)&&info.State==MEM_COMMIT,"closed channel keeps the retired view alive through the permit");recoveryCheck(!service.ops.terminateTarget()&&processAlive(child.process),"retired channel cannot authorize a final kernel call");service.terminationPermit={};ticket={};recoveryCheck(weak.expired()&&VirtualQuery(address,&info,sizeof(info))==sizeof(info)&&info.State==MEM_FREE,"final permit release naturally unmaps the retired channel");recoveryCheck(service.capture(),"a newly opened read-only channel heals without reviving the old lease");});
    cases.emplace_back("native-live-detach-reattach-never-revives-old-request",[]{OwnRecoveryGuardChild child(true,true);RecoveryGuardConsumer service(child);for(unsigned i=0;i!=4;++i)child.step();recoveryCheck(service.capture(),"current Ready boundary captured");const auto old=service.terminationPermit.boundary;child.step();recoveryCheck(!old.current()&&!service.capture(),"detached Native boundary retires old Ready before publication");child.step();recoveryCheck(service.capture()&&!old.current(),"reattached same mapping uses a new monotonically increasing stamp");child.step();recoveryCheck(!service.ops.terminateTarget()&&processAlive(child.process),"actual Native exit wrapper revokes the last request");});
    cases.emplace_back("native-live-unknown-boundary-fails-closed-legacy-prefixes-read",[]{for(unsigned mode=0;mode!=2;++mode){RecoveryMappingScope legacy(mode==0,mode==1);legacy.publish();auto observation=legacy.read();ymcc::sleeppower::RecoveryTerminationPermit permit;recoveryCheck(observation.state==SnapshotRead::Ok&&!observation.powerObservation.boundary.required&&!captureRecoveryTerminationPermit(L"",observation,permit),"original 152/224 prefix stays readable without inventing a live boundary");}RecoveryMappingScope owner;owner.publish();auto& record=owner.full->boundary;const auto saved=record.version;record.version=2;ymcc::sleeppower::RecoveryTerminationPermit permit;recoveryCheck(captureRecoveryTerminationPermit(L"",owner.read(),permit)&&!permit.accepted,"unsupported nonzero boundary cannot silently become legacy");record.version=saved;record.size=0;recoveryCheck(captureRecoveryTerminationPermit(L"",owner.read(),permit),"short known boundary is rejected");record.size=sizeof(record);record.magic=0;recoveryCheck(captureRecoveryTerminationPermit(L"",owner.read(),permit),"malformed boundary magic is rejected even with a current serial");record.magic=ymcc::sleeppower::kRecoveryBoundaryMagic;const auto serial=ReadAcquire64(&record.liveSerial);const auto stamp=record.publicationStamp;InterlockedExchange64(&record.liveSerial,-1);record.publicationStamp=(std::numeric_limits<std::uint64_t>::max)();recoveryCheck(captureRecoveryTerminationPermit(L"",owner.read(),permit),"negative wire serial cannot become an unsigned valid permit");InterlockedExchange64(&record.liveSerial,serial);record.publicationStamp=stamp;owner.publish();recoveryCheck(!captureRecoveryTerminationPermit(L"",owner.read(),permit),"fresh valid metadata heals without rewriting old tickets");});
    cases.emplace_back("native-live-counter-exhaustion-cannot-wrap",[]{using namespace ymcc::sleeppower;const auto maximum=static_cast<std::uint64_t>((std::numeric_limits<LONG64>::max)());RecoveryBoundaryMirror mirror(maximum-1);RecoveryPowerBoundary record;recoveryCheck(mirror.attach(&record)&&mirror.sample()==maximum,"actual Native mirror may reach its last positive serial");mirror.revoke();recoveryCheck(ReadAcquire64(&record.liveSerial)==0&&mirror.sample()==0,"exhausted mirror publishes only fail-closed zero");mirror.detach(&record);recoveryCheck(!mirror.attach(&record),"exhaustion cannot reset or revive an old serial");});
    cases.emplace_back("native-live-revoke-during-original-capture-mints-no-stamp",[]{RecoveryMappingScope owner;revokeDuringCapture=true;owner.publish();ymcc::sleeppower::RecoveryTerminationPermit permit;recoveryCheck(owner.full->boundary.publicationStamp==0&&captureRecoveryTerminationPermit(L"",owner.read(),permit),"actual revocation racing original coherent capture cannot mint a boundary stamp");owner.publish();recoveryCheck(!captureRecoveryTerminationPermit(L"",owner.read(),permit),"a stable original publication heals the failed capture");});
    cases.emplace_back("native-live-query-cancel-keeps-old-request-retired",[]{RecoveryMappingScope owner;owner.publish();ymcc::sleeppower::RecoveryTerminationPermit permit;recoveryCheck(!captureRecoveryTerminationPermit(L"",owner.read(),permit),"initial Native authority captured");const auto old=permit.boundary;g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,1,[](auto){return false;});owner.publish();recoveryCheck(!old.current()&&captureRecoveryTerminationPermit(L"",owner.read(),permit),"actual Dispatch ingress revokes before failed post and pending publication");auto cancelled=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPENDFAILED,2);g_sleepPowerDispatch.complete(cancelled,3);owner.publish();recoveryCheck(!captureRecoveryTerminationPermit(L"",owner.read(),permit)&&!old.current(),"same-generation query cancellation creates fresh permission without reviving the old one");});
    cases.emplace_back("native-live-resuming-reaches-real-owned-watchdog-api",[]{OwnRecoveryGuardChild child(true,true);RecoveryGuardConsumer service(child);for(unsigned i=0;i!=3;++i)child.step();const auto observed=service.observation();RecoveryEpisodeState episode;const auto calls=guardKernelCalls;recoveryCheck(observed.powerObservation.projection.power.phase==3&&recoveryPerformRestart(observed,episode,service.ops,L"")==RoundResult::Stop&&guardKernelCalls==calls+1&&service.spawns==1&&targetTerminationSettled(child.process),"current Resuming recovers the real owned target through the original watchdog");});
    cases.emplace_back("native-live-publisher-stop-retires-inflight-stamp-before-unmap",[]{PublisherFixtureScope owner;owner.enter();SnapshotObservation observed;recoveryCheck(readRecoverySnapshot(owner.channel,GetCurrentProcess(),GetCurrentProcessId(),observed)==SnapshotRead::Ok&&observed.powerObservation.boundary.current(),"actual main lifecycle attaches the live Native record");const auto old=observed.powerObservation.boundary;ResetEvent(publisherEntered);publisherStall=true;recoveryCheck(WaitForSingleObject(publisherEntered,5000)==WAIT_OBJECT_0,"actual publisher reached the inflight publication gate");recoveryStatePublisherStop();recoveryCheck(!old.current(),"stop invalidates the Native boundary before bounded worker return");publisherStall=false;SetEvent(publisherRelease);owner.returned();recoveryCheck(!old.current(),"late completion of the retired publish cannot remint its old stamp");owner.enter();recoveryCheck(readRecoverySnapshot(owner.channel,GetCurrentProcess(),GetCurrentProcessId(),observed)==SnapshotRead::Ok&&observed.powerObservation.boundary.current()&&!old.current(),"fresh main start heals while old tickets remain revoked");owner.stop();});

    cases.emplace_back("native-live-resuming-request-cannot-cross-new-ready-commit",[]{OwnRecoveryGuardChild child(true,true);RecoveryGuardConsumer service(child);for(unsigned i=0;i!=3;++i)child.step();recoveryCheck(service.capture(),"current Resuming request captured before Ready");const auto old=service.terminationPermit.boundary;child.step();const auto calls=guardKernelCalls;recoveryCheck(!service.ops.terminateTarget()&&processAlive(child.process)&&guardKernelCalls==calls,"completed new Native Ready commit retires the prior Resuming request before its final API");const auto ready=service.observation();const auto tick=ready.powerObservation.projection.committedTick;recoveryFixtureNow=ready.powerObservation.publishedTick+1;recoveryCheck(!service.capture()&&g_lastWakeTick==tick,"fresh Ready uses its original fifteen-second grace");recoveryFixtureNow=tick+kWakeGraceMs;recoveryCheck(service.capture()&&!old.current(),"new Ready request heals after grace while the old Resuming request stays revoked");});
    cases.emplace_back("native-live-ready-closed-gate-keeps-grace-without-clearing-sleep",[]{RecoveryMappingScope owner;const auto commitTick=recoveryFixtureNow;g_recoveryResumeCommit.record({7,1,commitTick});nativeGate=false;owner.publish();ymcc::sleeppower::RecoveryTerminationPermit permit;recoveryCheck(captureRecoveryTerminationPermit(L"",owner.read(),permit)&&g_lastWakeTick==commitTick,"a recorded Ready commit protects its original grace before the hardware gate opens");recoveryFixtureNow=commitTick+kWakeGraceMs;recoveryCheck(!captureRecoveryTerminationPermit(L"",owner.read(),permit)&&!(owner.full->power.power.flags&ymcc::sleeppower::kHardwareAllowed),"watchdog may heal a stuck closed gate after grace without granting device permission");powerNotifyCallback(nullptr,PBT_APMSUSPEND,nullptr);recoveryFixtureNow+=1000;g_recoveryResumeCommit.record({7,1,recoveryFixtureNow});owner.publish();recoveryCheck(captureRecoveryTerminationPermit(L"",owner.read(),permit)&&g_systemSleeping,"fresh Ready tick with a closed hardware gate cannot clear actual sleeping");});
    cases.emplace_back("local-delayed-suspend-uses-notification-ingress-time",[]{
        RecoveryMappingScope owner;const auto deliveredAt=recoveryFixtureNow;
        const std::function<void()> commitBeforeApply=[&]{owner.commit();};
        recoveryBeforeLocalPowerLock=&commitBeforeApply;
        owner.suspend();
        recoveryCheck(g_lastSuspendTick==deliveredAt,"delayed local Suspend keeps its ingress time before the shared commit");
        recoveryCheck(guarded(owner.read())&&!g_systemSleeping&&g_lastWakeTick==g_lastPowerResumeCommitTick,"a current commit observed after local ingress settles the delayed Suspend under original grace");
    });
    cases.emplace_back("local-earlier-suspend-cannot-overwrite-newer-resume",[]{
        RecoveryMappingScope owner;owner.publish();
        const std::function<void()> resumeBeforeApply=[] {++recoveryFixtureNow;powerNotifyCallback(nullptr,PBT_APMRESUMEAUTOMATIC,nullptr);};
        recoveryBeforeLocalPowerLock=&resumeBeforeApply;
        owner.suspend();
        recoveryCheck(!g_systemSleeping&&guarded(owner.read()),"receipt order preserves the newer local Resume and its grace");
        recoveryFixtureNow+=kWakeGraceMs;owner.publish();
        recoveryCheck(!guarded(owner.read()),"processing the old Suspend last cannot leave the original watchdog stuck");
    });
    cases.emplace_back("local-ingress-revokes-before-notification-gets-lock",[]{
        OwnRecoveryGuardChild child;RecoveryGuardConsumer service(child);
        recoveryCheck(service.capture(),"initial watchdog permit is current");
        const auto calls=guardKernelCalls;bool stopped=false;
        ymcc::sleeppower::RecoveryTerminationPermit pendingPermit;
        bool pendingDeferred=false;
        const std::function<void()> tryWhilePending=[&]{
            pendingDeferred=captureRecoveryTerminationPermit(L"",service.observation(),pendingPermit);
            stopped=service.ops.terminateTarget();
        };
        recoveryBeforeLocalPowerLock=&tryWhilePending;
        powerNotifyCallback(nullptr,PBT_APMSUSPEND,nullptr);
        recoveryCheck(pendingDeferred&&!pendingPermit.accepted,"an unprocessed power receipt cannot mint a new watchdog permit");
        recoveryCheck(!stopped&&guardKernelCalls==calls&&processAlive(child.process),"local Suspend at ingress retires an in-flight watchdog permit before the callback obtains its lock");
    });
    cases.emplace_back("local-notification-during-identity-check-retires-permit",[]{
        OwnRecoveryGuardChild child;RecoveryGuardConsumer service(child);
        recoveryCheck(service.capture(),"initial watchdog permit is current");
        GuardFixtureHandle arrived{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
        recoveryCheck(arrived.value!=nullptr,"owned callback arrival event created");
        std::jthread callback;bool entered=false;
        const std::function<void()> interruptIdentity=[&]{
            callback=std::jthread([&]{
                const std::function<void()> signal=[&]{SetEvent(arrived.value);};
                recoveryBeforeLocalPowerLock=&signal;
                powerNotifyCallback(nullptr,PBT_APMSUSPEND,nullptr);
            });
            entered=WaitForSingleObject(arrived.value,3000)==WAIT_OBJECT_0;
        };
        recoveryBeforeProcessTimes=&interruptIdentity;
        const auto calls=guardKernelCalls;
        const bool stopped=service.ops.terminateTarget();
        recoveryBeforeProcessTimes=nullptr;
        if(callback.joinable())callback.join();
        recoveryCheck(entered&&!stopped&&guardKernelCalls==calls&&processAlive(child.process),"local ingress during exact process identity recheck retires the permit before the kernel call");
    });
    cases.emplace_back("classification-holds-watchdog-after-early-local-resume",[]{
        RecoveryMappingScope owner;
        recoveryActualNonReady(3);
        nativeGate=false;
        powerNotifyCallback(nullptr,PBT_APMRESUMEAUTOMATIC,nullptr);
        recoveryFixtureNow+=kWakeGraceMs+1;
        sgBlockSharedWakeClassification(7);
        owner.publish();
        auto early=owner.read();
        ymcc::sleeppower::RecoveryTerminationPermit permit;
        recoveryCheck(!sharedSleepWakeClassified(7)&&early.powerObservation.projection.power.phase==3&&
            !early.powerObservation.projection.committedTick&&!g_systemSleeping,
            "early local Resume left an unresolved shared Resuming episode");
        recoveryCheck(captureRecoveryTerminationPermit(L"",early,permit)&&!permit.accepted,
            "early Resume must not allow a watchdog request after grace while classification is unresolved");
        sgCompleteSharedWakeClassification(7);
        owner.publish();
        recoveryCheck(sharedSleepWakeClassified(7)&&!captureRecoveryTerminationPermit(L"",owner.read(),permit)&&permit.boundary.current(),
            "settled classification allows recovery of a still stuck Resuming owner");
        const auto old=permit.boundary;
        sgBlockSharedWakeClassification(7);
        recoveryCheck(!old.current()&&captureRecoveryTerminationPermit(L"",owner.read(),permit),
            "new classification revokes an outstanding permit before the next heartbeat");
        sgCompleteSharedWakeClassification(7);
        owner.publish();
        recoveryCheck(!old.current()&&!captureRecoveryTerminationPermit(L"",owner.read(),permit),
            "the fresh permit heals without reviving its predecessor");
        const auto beforeHold=permit.boundary;
        sgHoldSharedWakeClassification(7);
        owner.publish();
        recoveryCheck(!beforeHold.current()&&captureRecoveryTerminationPermit(L"",owner.read(),permit),
            "the original pending classification timer also revokes the watchdog permit");
        g_sgResumeClassification.generation.exchange(0,std::memory_order_acq_rel);
        owner.publish();
        recoveryCheck(!beforeHold.current()&&!captureRecoveryTerminationPermit(L"",owner.read(),permit),
            "returning the classification timer allows a newly published permit");
    });
    cases.emplace_back("responsive-probe-observes-wake-before-publisher-stalls",[]{
        RecoveryMappingScope owner;
        RecoveryEpisodeState episode;
        const auto responsive=[&]{recoveryProbeResponsive(owner.channel,GetCurrentProcess(),GetCurrentProcessId(),episode,L"");};
        owner.publish();responsive();
        recoveryCheck(!owner.channel.view,"ordinary responsive rounds have no extra power mapping read");
        owner.suspend();owner.publish();responsive();
        recoveryCheck(g_systemSleeping,"a responsive probe and fresh heartbeat alone must not clear Suspend");
        owner.commit();responsive();
        const auto wake=g_lastPowerResumeCommitTick.load();
        recoveryCheck(!g_systemSleeping&&g_lastWakeTick==wake,"responsive round must consume a current shared commit while it is fresh");
        ymcc::sleeppower::RecoveryTerminationPermit permit;
        ++recoveryFixtureNow;
        recoveryCheck(captureRecoveryTerminationPermit(L"",owner.read(),permit)&&!permit.accepted,"responsive observation leaves the original wake grace in force");
        recoveryFixtureNow=wake+kWakeGraceMs;
        auto stalled=owner.read();
        recoveryCheck(!stalled.heartbeatFresh&&!captureRecoveryTerminationPermit(L"",stalled,permit)&&permit.accepted,"a later publisher/UI stall can use the original watchdog after grace without borrowing a stale wake");
    });
    cases.emplace_back("late-local-suspend-missing-resume-keeps-ordered-proof",[]{
        RecoveryMappingScope owner;
        owner.commit();
        const auto oldWake=g_lastPowerResumeCommitTick.load();
        recoveryFixtureNow=oldWake+1;
        owner.suspend(); // Also matches a real new Suspend not yet received by Native.
        const auto localSuspend=g_lastSuspendTick.load();
        owner.publish();
        RecoveryEpisodeState episode;
        recoveryProbeResponsive(owner.channel,GetCurrentProcess(),GetCurrentProcessId(),episode,L"");
        recoveryCheck(g_systemSleeping,"a successful probe cannot assign a late local Suspend to the committed cycle");
        recoveryFixtureNow=oldWake+kWakeGraceMs+1;
        owner.publish();
        const auto before=owner.read();
        const std::wstring log=L"fixture-late-local-suspend.log";
        recoveryCheck(GetFileAttributesW(log.c_str())==INVALID_FILE_ATTRIBUTES,"private sleep diagnostic output is fresh");
        ymcc::sleeppower::RecoveryTerminationPermit permit;
        recoveryCheck(captureRecoveryTerminationPermit(log,before,permit)&&!permit.accepted&&g_systemSleeping,
            "expiration of grace and a fresh publication must not promote an older wake or arm termination");
        ++g_powerGeneration;
        ++g_sharedPowerSuspendSerial;
        owner.commit();
        const auto currentWake=g_lastPowerResumeCommitTick.load();
        recoveryCheck(captureRecoveryTerminationPermit(L"",owner.read(),permit)&&!permit.accepted&&!g_systemSleeping&&g_lastWakeTick==currentWake,
            "a newer public cycle settles the missing Resume but must preserve its original grace");
        recoveryFixtureNow=currentWake+kWakeGraceMs;
        recoveryCheck(!captureRecoveryTerminationPermit(L"",owner.read(),permit)&&permit.accepted,
            "the local latch cannot survive a later proved commit and its grace");
        std::ifstream input(log);
        json entry;input>>entry;
        const auto& submitted=before.powerObservation;
        recoveryCheck(entry.value("reason","")=="system-sleeping"&&
            entry.value("localNotificationSequence",0ULL)==1&&entry.value("localSuspendTick",0ULL)==localSuspend&&
            entry.value("nativeProcessBound",false)&&entry.value("nativeGeneration",0ULL)==submitted.projection.power.generation&&
            entry.value("nativeSuspendSerial",0ULL)==submitted.projection.power.suspendSerial&&
            entry.value("nativeNotificationSequence",0ULL)==submitted.projection.power.observedNotificationSequence&&
            entry.value("nativeCommittedTick",0ULL)==oldWake&&entry.value("nativePublishedTick",0ULL)==submitted.publishedTick,
            "the original defer line must preserve the local receipt and older Native commit separately");
    });
    cases.emplace_back("restart-owner-release-suspend-defers-relaunch",[]{
        OwnRecoveryGuardChild child(true,true);
        RecoveryGuardConsumer service(child);
        RecoveryEpisodeState episode;
        auto observation=service.observation();
        observation.heartbeatFresh=false;
        const auto calls=guardKernelCalls;
        bool suspended=false,resumed=false,probedDuringHold=false,launchedDuringHold=false;
        service.ops.ownerProbe=[&]{
            if(suspended&&!resumed)probedDuringHold=true;
            if(!suspended){
                recoveryCheck(WaitForSingleObject(child.process,0)==WAIT_OBJECT_0,"the original target is confirmed exited before checking the old owner");
                ++recoveryFixtureNow;
                powerNotifyCallback(nullptr,PBT_APMSUSPEND,nullptr);
                suspended=true;
            }
            return OwnerProbe{};
        };
        service.ops.sleep=[&](DWORD ms){
            recoveryFixtureNow+=ms;
            if(suspended&&!resumed){powerNotifyCallback(nullptr,PBT_APMRESUMEAUTOMATIC,nullptr);resumed=true;}
        };
        service.ops.spawnTarget=[&]{
            launchedDuringHold=!resumed||recoveryFixtureNow-g_lastWakeTick.load()<kWakeGraceMs;
            ++service.spawns;
            return true;
        };
        const auto result=recoveryDecisionRound(observation,episode,service.ops,L"");
        recoveryCheck(!launchedDuringHold,"new Suspend during owner probe must veto launch until its Resume and original grace");
        recoveryCheck(result==RoundResult::Stop&&suspended&&resumed&&service.spawns==1&&guardKernelCalls==calls+1,
                      "one proved restart resumes after wake, without a second kill");
        recoveryCheck(!probedDuringHold,"no owner probe is repeated while the replacement is suspended");
    });
    cases.emplace_back("restart-unmatched-or-revoked-power-cannot-launch",[]{
        for(const bool revoke:{false,true}){
            OwnRecoveryGuardChild child(true,true);
            RecoveryGuardConsumer service(child);
            RecoveryEpisodeState episode;
            auto observation=service.observation();
            observation.heartbeatFresh=false;
            const auto calls=guardKernelCalls;
            unsigned probes=0;
            const auto start=recoveryFixtureNow;
            service.ops.ownerProbe=[&]{
                ++probes;
                if(revoke) snapshotChannelClose(service.channel);
                else {++recoveryFixtureNow;powerNotifyCallback(nullptr,PBT_APMRESUMEAUTOMATIC,nullptr);}
                return OwnerProbe{};
            };
            const auto result=recoveryDecisionRound(observation,episode,service.ops,L"");
            recoveryCheck(result==RoundResult::Stop&&probes==1&&service.spawns==0&&guardKernelCalls==calls+1,
                        "retired Native authority or a lone Resume cannot launch or repeat owner work");
            recoveryCheck(revoke?recoveryFixtureNow==start:recoveryFixtureNow-start>=kOwnerReleaseWaitMs&&
                recoveryFixtureNow-start<kOwnerReleaseWaitMs+kOwnerReleasePollMs+1,
                "retired authority stops immediately and missing pairing uses one bounded window");
        }
    });
    if(argc>2)return 2;const std::string selected=argc==2?argv[1]:"";const bool suffix=selected.starts_with("from:");const auto name=suffix?selected.substr(5):selected;if(argc==2&&std::none_of(cases.begin(),cases.end(),[&](const auto& c){return c.first==name;}))return 2;
    json executed=json::array(),failures=json::array();bool reached=false;for(const auto& c:cases){if(c.first==name)reached=true;if(argc==2&&!(suffix?reached:c.first==name))continue;executed.push_back(c.first);try{c.second();}catch(const std::exception& e){failures.push_back({{"case",c.first},{"message",e.what()}});break;}}
    std::cout<<json({{"cases",executed},{"assertions",recoveryChecks},{"actualMappingsReturned",recoveryReturned},{"actualGuardChildrenStarted",guardChildrenStarted},{"actualGuardChildrenReturned",guardChildrenReturned},{"forcedGuardChildCleanup",guardForcedCleanup},{"guardHandleCloseFailures",guardHandleCloseFailures},{"guardKernelCalls",guardKernelCalls},{"guardKernelWithoutLock",guardKernelWithoutLock},{"externalTerminateAttempts",guardExternalTerminateAttempts},{"publisherMappingsCreated",publisherMappingsCreated.load()},{"publisherMappingsClosed",publisherMappingsClosed.load()},{"publisherEventsCreated",publisherEventsCreated.load()},{"publisherEventsClosed",publisherEventsClosed.load()},{"publisherViewsMapped",publisherViewsMapped.load()},{"publisherViewsUnmapped",publisherViewsUnmapped.load()},{"publisherPublicationCalls",publisherPublicationCalls.load()},{"failures",failures},{"physicalSleep",false},{"killAndSpawnMocked",true}}).dump()<<std::endl;return failures.empty()?0:1;
}
