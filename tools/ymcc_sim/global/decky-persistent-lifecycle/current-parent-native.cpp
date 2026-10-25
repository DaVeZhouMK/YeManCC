// Current parent migration probe. Product Runtime/Client/Owner and the original
// source-built protocol peer are included from a byte-pinned producer preamble.
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
#error Current parent evidence must compile the production typed path.
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
namespace currentprobe {
const char* stage="not-started";thread_local const char* apiStage="outside-probe";wchar_t faultFile[32768]{};
LONG WINAPI faultFilter(EXCEPTION_POINTERS* info){
    char text[16384]{},module[1024]{};const auto* record=info->ExceptionRecord;MEMORY_BASIC_INFORMATION region{};
    if(VirtualQuery(record->ExceptionAddress,&region,sizeof(region)))GetModuleFileNameA(static_cast<HMODULE>(region.AllocationBase),module,1024);
    int length=wsprintfA(text,"stage=%s api=%s tid=%lu code=%lu address=%p base=%p module=%s access=%I64u target=%p\r\n",stage,apiStage,GetCurrentThreadId(),record->ExceptionCode,record->ExceptionAddress,GetModuleHandleW(nullptr),module,record->NumberParameters?record->ExceptionInformation[0]:0,record->NumberParameters>1?reinterpret_cast<void*>(record->ExceptionInformation[1]):nullptr);
    void* frames[32]{};const auto count=CaptureStackBackTrace(0,32,frames,nullptr);
    for(USHORT i=0;i<count&&length<14000;++i){module[0]=0;region={};if(VirtualQuery(frames[i],&region,sizeof(region)))GetModuleFileNameA(static_cast<HMODULE>(region.AllocationBase),module,1024);length+=wsprintfA(text+length,"frame=%p moduleBase=%p module=%s\r\n",frames[i],region.AllocationBase,module);}
    const auto file=CreateFileW(faultFile,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){DWORD written=0;WriteFile(file,text,static_cast<DWORD>(length),&written,nullptr);::CloseHandle(file);}return EXCEPTION_EXECUTE_HANDLER;
}
std::atomic<unsigned> launches{0},loaderLaunches{0},jobSerial{0};
wchar_t image[32768]{};
SRWLOCK ledgerLock=SRWLOCK_INIT;
SRWLOCK processCreateLock=SRWLOCK_INIT;
struct JobHandle {HANDLE handle=nullptr;unsigned key=0;DWORD root=0;bool killOnClose=false;};
JobHandle jobs[256]{};
struct LoaderRecord {HANDLE process=nullptr;DWORD pid=0;ULONGLONG createdAt=0;wchar_t home[512]{};};
LoaderRecord records[64]{};unsigned recordCount=0;
std::atomic<bool> capture{false},overflow{false};
std::atomic<unsigned> deniedJob{0},deniedCalls{0},targetCloses{0},lastCloses{0},descAliveAtLastClose{0},rootTerminateCalls{0},descTerminateCalls{0};
std::atomic<DWORD> targetRoot{0},targetDesc{0};
std::atomic<HANDLE> descendantObserver{nullptr};
void addJob(HANDLE handle,unsigned id,DWORD root=0,bool kill=false){
    AcquireSRWLockExclusive(&ledgerLock);bool added=false;
    for(auto& row:jobs)if(!row.handle){row={handle,id,root,kill};added=true;break;}
    if(!added)overflow=true;ReleaseSRWLockExclusive(&ledgerLock);
}
JobHandle lookup(HANDLE handle){AcquireSRWLockShared(&ledgerLock);JobHandle result{};for(const auto& row:jobs)if(row.handle==handle){result=row;break;}ReleaseSRWLockShared(&ledgerLock);return result;}
unsigned jobHandles(unsigned id=0){AcquireSRWLockShared(&ledgerLock);unsigned count=0;for(const auto& row:jobs)if(row.handle&&(!id||row.key==id))++count;ReleaseSRWLockShared(&ledgerLock);return count;}
JobHandle rootJob(DWORD pid){AcquireSRWLockShared(&ledgerLock);JobHandle result{};for(const auto& row:jobs)if(row.handle&&row.root==pid){result=row;break;}ReleaseSRWLockShared(&ledgerLock);return result;}
bool inertCommand(LPCWSTR executable,LPCWSTR command){const auto n=lstrlenW(image);return executable&&command&&n>0&&lstrcmpiW(executable,image)==0&&lstrlenW(command)==n+2&&command[0]==L'"'&&command[n+1]==L'"'&&CompareStringOrdinal(command+1,n,image,n,TRUE)==CSTR_EQUAL;}
void readOwnerHome(LPVOID environment,DWORD flags,wchar_t* result){
    result[0]=0;if(!environment||!(flags&CREATE_UNICODE_ENVIRONMENT))return;
    const wchar_t* prefix=L"PRIVILEGED_PATH=";const auto n=lstrlenW(prefix);
    for(const wchar_t* row=static_cast<const wchar_t*>(environment);*row;row+=lstrlenW(row)+1)if(lstrlenW(row)>=n&&CompareStringOrdinal(row,n,prefix,n,TRUE)==CSTR_EQUAL){lstrcpynW(result,row+n,512);return;}
}
unsigned ownedCount(LPCWSTR home){AcquireSRWLockShared(&ledgerLock);unsigned count=0;for(unsigned i=0;i<recordCount;++i)if(lstrcmpiW(records[i].home,home)==0)++count;ReleaseSRWLockShared(&ledgerLock);return count;}
void created(bool inert,const PROCESS_INFORMATION* child,LPCWSTR home){
    ++launches;if(!inert)return;++loaderLaunches;if(!capture)return;
    HANDLE observer=nullptr;const auto self=GetCurrentProcess();
    if(!::DuplicateHandle(self,child->hProcess,self,&observer,0,FALSE,DUPLICATE_SAME_ACCESS)){overflow=true;return;}
    AcquireSRWLockExclusive(&ledgerLock);
    if(recordCount<64){auto& row=records[recordCount++];row.process=observer;row.pid=child->dwProcessId;row.createdAt=GetTickCount64();lstrcpynW(row.home,home,512);if(!row.home[0])overflow=true;}else{overflow=true;::CloseHandle(observer);}
    ReleaseSRWLockExclusive(&ledgerLock);
}
BOOL WINAPI create(LPCWSTR a,LPWSTR b,LPSECURITY_ATTRIBUTES c,LPSECURITY_ATTRIBUTES d,BOOL e,DWORD flags,LPVOID env,LPCWSTR dir,LPSTARTUPINFOW si,LPPROCESS_INFORMATION pi){const bool inert=inertCommand(a,b);wchar_t home[512]{};if(inert&&capture){apiStage="read-owner-home";readOwnerHome(env,flags,home);}apiStage="original-create-process";AcquireSRWLockExclusive(&processCreateLock);const auto ok=::CreateProcessW(a,b,c,d,e,flags,env,dir,si,pi);const auto savedError=GetLastError();ReleaseSRWLockExclusive(&processCreateLock);if(ok){apiStage="record-created-process";created(inert,pi,home);}apiStage="outside-probe";SetLastError(savedError);return ok;}
BOOL WINAPI createToken(HANDLE token,DWORD logon,LPCWSTR a,LPWSTR b,DWORD flags,LPVOID env,LPCWSTR dir,LPSTARTUPINFOW si,LPPROCESS_INFORMATION pi){const bool inert=inertCommand(a,b);wchar_t home[512]{};if(inert&&capture){apiStage="read-owner-home";readOwnerHome(env,flags,home);}apiStage="original-create-process";AcquireSRWLockExclusive(&processCreateLock);const auto ok=::CreateProcessWithTokenW(token,logon,a,b,flags,env,dir,si,pi);const auto savedError=GetLastError();ReleaseSRWLockExclusive(&processCreateLock);if(ok){apiStage="record-created-process";created(inert,pi,home);}apiStage="outside-probe";SetLastError(savedError);return ok;}
HANDLE WINAPI createJob(LPSECURITY_ATTRIBUTES attributes,LPCWSTR name){const auto handle=::CreateJobObjectW(attributes,name);if(handle)addJob(handle,++jobSerial);return handle;}
BOOL WINAPI configureJob(HANDLE job,JOBOBJECTINFOCLASS kind,LPVOID value,DWORD bytes){
    const auto ok=::SetInformationJobObject(job,kind,value,bytes);const auto entry=lookup(job);
    if(ok&&entry.key&&kind==JobObjectExtendedLimitInformation&&bytes>=sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)){
        const bool kill=(static_cast<JOBOBJECT_EXTENDED_LIMIT_INFORMATION*>(value)->BasicLimitInformation.LimitFlags&JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)!=0;
        AcquireSRWLockExclusive(&ledgerLock);for(auto& row:jobs)if(row.key==entry.key)row.killOnClose=kill;ReleaseSRWLockExclusive(&ledgerLock);
    }return ok;
}
BOOL WINAPI assignJob(HANDLE job,HANDLE process){
    const auto ok=::AssignProcessToJobObject(job,process);const auto entry=lookup(job);
    if(ok&&entry.key){const auto pid=GetProcessId(process);AcquireSRWLockExclusive(&ledgerLock);for(auto& row:jobs)if(row.key==entry.key)row.root=pid;ReleaseSRWLockExclusive(&ledgerLock);}return ok;
}
BOOL WINAPI duplicate(HANDLE sourceProcess,HANDLE source,HANDLE targetProcess,LPHANDLE target,DWORD access,BOOL inherit,DWORD options){
    const auto entry=GetProcessId(sourceProcess)==GetCurrentProcessId()?lookup(source):JobHandle{};
    const auto ok=::DuplicateHandle(sourceProcess,source,targetProcess,target,access,inherit,options);
    if(ok&&entry.key&&target&&GetProcessId(targetProcess)==GetCurrentProcessId())addJob(*target,entry.key,entry.root,entry.killOnClose);
    if(ok&&(options&DUPLICATE_CLOSE_SOURCE)&&entry.key){AcquireSRWLockExclusive(&ledgerLock);for(auto& row:jobs)if(row.handle==source)row={};ReleaseSRWLockExclusive(&ledgerLock);}return ok;
}
BOOL WINAPI close(HANDLE handle){
    const auto entry=lookup(handle);const bool selected=entry.key&&entry.key==deniedJob.load();
    const bool last=selected&&jobHandles(entry.key)==1;
    if(last){const auto desc=descendantObserver.load();if(desc&&WaitForSingleObject(desc,0)==WAIT_TIMEOUT)++descAliveAtLastClose;}
    const auto ok=::CloseHandle(handle);
    if(ok&&entry.key){AcquireSRWLockExclusive(&ledgerLock);for(auto& row:jobs)if(row.handle==handle)row={};ReleaseSRWLockExclusive(&ledgerLock);if(selected){++targetCloses;if(last&&entry.killOnClose)++lastCloses;}}
    return ok;
}
BOOL WINAPI terminateJob(HANDLE job,UINT code){const auto entry=lookup(job);if(entry.key&&entry.key==deniedJob.load()){++deniedCalls;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::TerminateJobObject(job,code);}
BOOL WINAPI terminateProcess(HANDLE process,UINT code){const auto pid=GetProcessId(process);if(pid&&pid==targetRoot)++rootTerminateCalls;if(pid&&pid==targetDesc)++descTerminateCalls;return ::TerminateProcess(process,code);}
}
#define CreateProcessW currentprobe::create
#define CreateProcessWithTokenW currentprobe::createToken
#define CreateJobObjectW currentprobe::createJob
#define SetInformationJobObject currentprobe::configureJob
#define AssignProcessToJobObject currentprobe::assignJob
#define DuplicateHandle currentprobe::duplicate
#define CloseHandle currentprobe::close
#define TerminateJobObject currentprobe::terminateJob
#define TerminateProcess currentprobe::terminateProcess
#include "current-runtime-helpers.inc"
#undef CreateProcessW
#undef CreateProcessWithTokenW
#undef CreateJobObjectW
#undef SetInformationJobObject
#undef AssignProcessToJobObject
#undef DuplicateHandle
#undef CloseHandle
#undef TerminateJobObject
#undef TerminateProcess
#include <type_traits>
static_assert(std::is_same_v<decltype(ymcc::deckysidebar::Config{}.ownedBinding),std::shared_ptr<ymcc::deckybinding::Client>>);
namespace currentparent {
using namespace rtfixture;
static ymcc::deckysidebar::Runtime* activeRuntime=nullptr;
enum class PowerLifecycle {Ready,Suspending,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static unsigned long long generation=1;
static unsigned watchdogStops=0;
static Json diagnostics=Json::array();
static Json fallbackMeasurements=Json::array();
static unsigned long long currentPowerGeneration(){return generation;}
static void stopPowerResumeWatchdog(){++watchdogStops;}
// CSS resume is a separate product boundary. Keep the exact extracted Decky
// consumer and its assertions; appearance behavior is verified by its own suite.
static void steamAppearanceResumeCommitted(unsigned long long) {}
static void deckySidebarDiagnostic(const char* event,const Json& detail,bool){diagnostics.push_back({{"event",event},{"detail",detail}});}
#define g_deckySidebarRuntime (*activeRuntime)
#include "current-committed-ready.inc"
#undef g_deckySidebarRuntime
inline Json stopAndLate(const std::filesystem::path& output,const std::string& reason,bool late){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Harness harness(output,context);const auto beforeHandles=handles();const auto beforeThreads=threadCount();
    harness.start();need(waitFor([&]{return harness.healthyGeneration==1;}),"initial-current-binding-checkpoint");
    const auto owned=harness.runtime.snapshot();const auto live=harness.client->snapshot();
    detail::Handle loader{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,owned.pid)};
    need(loader.value&&WaitForSingleObject(loader.value,0)==WAIT_TIMEOUT,"current-owned-loader-live");
    need(live.handles>0&&live.processId!=0,"current-owned-client-live");
    if(!reason.empty()){
        const std::string run(32,'a');
        need(ymcc::deckyhealth::matches(Json{{"reason",reason},{"runId",run}},Json{{"enabled",true},{"phase","loader-started"}},run),"original-fatal-policy-admits-scoped-error");
    }
    const auto began=GetTickCount64();
    if(late)harness.runtime.shutdown();else harness.runtime.setEnabled(false,reason);
    const auto elapsed=GetTickCount64()-began;
    const bool exited=WaitForSingleObject(loader.value,1000)==WAIT_OBJECT_0;
    CloseHandle(loader.value);loader.value=nullptr;
    const auto plans=harness.plans.load(),launches=currentprobe::launches.load();unsigned rejected=0,deliveries=0;
    // Replay the same post-stop native demands as the legacy parent probe.
    for(unsigned i=0;i<(late?8u:16u);++i){
        ++deliveries;harness.runtime.observeSteam(1,true);
        rejected+=!harness.runtime.requestTransportRecovery();
        rejected+=!harness.runtime.requestBindingRefresh(i+2);
        rejected+=!harness.runtime.requestLaunchAdmissionRefresh();
        rejected+=!harness.runtime.requestConnectionCheck();
        rejected+=!harness.runtime.requestPreparationRefresh(i+2);
    }
    Sleep(40);
    const auto after=harness.runtime.snapshot();const auto destroyed=harness.client->snapshot();
    const auto afterHandles=handles();const auto afterThreads=threadCount();
    need(elapsed<4500&&exited&&!after.enabled&&after.pid==0,"current-off-bounded-and-loader-exited");
    need(after.reason==reason&&harness.plans==plans&&currentprobe::launches==launches,"current-off-not-revived-by-late-demand");
    need(rejected==deliveries*5,"every-original-and-current-late-demand-rejected");
    need(destroyed.fullyDestroyed&&destroyed.handles==0&&destroyed.pendingIo==0&&destroyed.activeCalls==0&&destroyed.jobActiveProcesses==0,"current-owned-client-destroyed");
    need(beforeHandles==afterHandles&&beforeThreads==afterThreads&&context.alive(),"current-parent-resources-return-to-baseline-and-context-unaffected");
    return {{"bounded",elapsed<4500},{"elapsedMs",elapsed},{"ownerRevoked",!after.enabled},{"disabledReason",after.reason},
        {"ownedLoaderPid",owned.pid},{"ownedLoaderExited",exited},{"atExit",state(destroyed)},{"active",state(live)},
        {"parentHandlesBefore",beforeHandles},{"parentHandlesAfter",afterHandles},{"parentThreadsBefore",beforeThreads},{"parentThreadsAfter",afterThreads},
        {"lateCallbacksDelivered",deliveries},{"rejectedLateRequests",rejected},{"bindingPlansBeforeLate",plans},{"bindingPlansAfterLate",harness.plans.load()},
        {"additionalStarts",currentprobe::launches.load()-launches},{"cefUnaffected",context.alive()}};
}
inline Json committedReady(const std::filesystem::path& output){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Harness harness(output,context);const auto beforeHandles=handles();
    harness.start();need(waitFor([&]{return harness.healthyGeneration==1;}),"current-ready-initial-checkpoint");
    activeRuntime=&harness.runtime;const auto originalLoader=harness.runtime.snapshot().pid;
    const auto originalBinding=harness.client->snapshot().processId;const auto before=harness.plans.load();
    generation=2;g_powerLifecycle=PowerLifecycle::Suspended;consumeCommittedReady();Sleep(40);const auto sleeping=harness.plans.load();
    g_powerLifecycle=PowerLifecycle::Resuming;consumeCommittedReady();Sleep(40);const auto resuming=harness.plans.load();
    g_powerLifecycle=PowerLifecycle::Ready;consumeCommittedReady();
    need(waitFor([&]{return harness.healthyGeneration==2;}),"current-ready-rebound-checkpoint");
    const auto after=harness.plans.load();const auto nextBinding=harness.client->snapshot().processId;
    consumeCommittedReady();Sleep(40);const auto duplicate=harness.plans.load();
    const auto finalLoader=harness.runtime.snapshot().pid;
    harness.runtime.shutdown();activeRuntime=nullptr;const auto destroyed=harness.client->snapshot();
    need(before==sleeping&&before==resuming&&after==before+1&&duplicate==after,"original-ready-only-once-and-no-sleeping-rebind");
    need(originalLoader>0&&finalLoader==originalLoader&&originalBinding>0&&nextBinding>0&&nextBinding!=originalBinding,"current-ready-keeps-owned-loader-and-replaces-binding");
    need(diagnostics.size()==2&&diagnostics[0]["detail"]["bindingRequested"]==true&&diagnostics[1]["detail"]["bindingRequested"]==false,"original-ready-consumer-records-accepted-and-duplicate");
    need(destroyed.fullyDestroyed&&beforeHandles==handles()&&context.alive(),"current-ready-destroyed-with-strict-parent-handle-equality");
    return {{"sleepObserved",true},{"readyGranted",true},{"currentGeneration",generation},
        {"bindingCallsBefore",before},{"bindingCallsSleeping",sleeping},{"bindingCallsResuming",resuming},{"bindingCallsAfterReady",after},{"bindingCallsAfterDuplicate",duplicate},
        {"initialLoaderPid",originalLoader},{"afterLoaderPid",finalLoader},{"initialBindingPid",originalBinding},{"nextBindingPid",nextBinding},
        {"loaderIdentityUnchanged",finalLoader==originalLoader},{"healthObserved",false},{"scope","original native/main.cpp committed-Ready consumer with current typed Runtime; TS health is independently executed"},
        {"atExit",state(destroyed)},{"beforeHandles",beforeHandles},{"afterHandles",handles()},{"cefUnaffected",context.alive()},{"diagnostics",diagnostics}};
}
struct EnvironmentScope {
    const wchar_t* name;std::wstring prior;
    EnvironmentScope(const wchar_t* key,const wchar_t* value):name(key),prior(environment(key)){setEnvironment(name,value);}
    ~EnvironmentScope(){SetEnvironmentVariableW(name,prior.empty()?nullptr:prior.c_str());}
};
struct FaultScope {
    FaultScope(unsigned key,DWORD root,DWORD descendant,HANDLE observer){
        currentprobe::deniedCalls=0;currentprobe::targetCloses=0;currentprobe::lastCloses=0;currentprobe::descAliveAtLastClose=0;
        currentprobe::rootTerminateCalls=0;currentprobe::descTerminateCalls=0;
        currentprobe::targetRoot=root;currentprobe::targetDesc=descendant;currentprobe::descendantObserver=observer;currentprobe::deniedJob=key;
    }
    ~FaultScope(){currentprobe::deniedJob=0;currentprobe::targetRoot=0;currentprobe::targetDesc=0;currentprobe::descendantObserver=nullptr;}
};
inline Json ownedCommandLine(HANDLE process){
    using Query=LONG(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);struct Text {USHORT length,maximum;PWSTR value;};
    const auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationProcess"));
    if(!query)return {{"error","query-unavailable"}};std::vector<unsigned char> buffer(65536);ULONG size=0;
    const auto code=query(process,60,buffer.data(),static_cast<ULONG>(buffer.size()),&size);if(code<0)return {{"status",code}};
    const auto* text=reinterpret_cast<const Text*>(buffer.data());const auto first=reinterpret_cast<std::uintptr_t>(buffer.data()),at=reinterpret_cast<std::uintptr_t>(text->value);
    if(text->length%2||at<first||at+text->length>first+buffer.size())return {{"error","query-bounds"}};
    const auto utf8=std::filesystem::path(std::wstring(text->value,text->length/sizeof(wchar_t))).u8string();return {{"pid",GetProcessId(process)},{"command",std::string(utf8.begin(),utf8.end())}};
}
inline Json jobCloseFallback(const std::filesystem::path& output){
    Controls controls(L"runtime-success-checkpoint");
    // Original source-built CEF context is a separately owned sentinel. It is
    // in ContextJob and is explicitly proved outside the Runtime Loader Job.
    Context sentinel;const auto sentinelPid=worker::number(sentinel.lease.at("pid"));ContextJob sentinelOwner(sentinelPid);
    Harness harness(output,sentinel);EnvironmentScope mode(L"YMCC_CURRENT_PARENT_INERT_MODE",L"job-tree");
    // Job membership alone includes children still being created. A root stop
    // can abort that creation with STATUS_PROCESS_IS_TERMINATING before main.
    // Both real actors must enter their inert bodies within the ORIGINAL3s gate.
    const auto tag=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(currentprobe::jobSerial.load());
    const auto rootReadyName=L"Local\\YMCC-Fallback-RootReady-"+tag,descReadyName=L"Local\\YMCC-Fallback-DescReady-"+tag;
    detail::Handle rootReady{CreateEventW(nullptr,TRUE,FALSE,rootReadyName.c_str())};need(rootReady.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"fallback-private-root-ready-event");
    detail::Handle descReady{CreateEventW(nullptr,TRUE,FALSE,descReadyName.c_str())};need(descReady.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"fallback-private-desc-ready-event");
    EnvironmentScope rootReadyScope(L"YMCC_CURRENT_PARENT_ROOT_READY",rootReadyName.c_str()),descReadyScope(L"YMCC_CURRENT_PARENT_DESC_READY",descReadyName.c_str());
    const auto beforeHandles=handles();const auto beforeThreads=threadCount();const auto jobsBefore=currentprobe::jobHandles();
    harness.start();need(waitFor([&]{return harness.healthyGeneration==1;}),"fallback-current-binding-checkpoint");
    const auto loaderPid=harness.runtime.snapshot().pid;const auto owned=currentprobe::rootJob(loaderPid);
    need(owned.handle&&owned.key&&owned.killOnClose,"fallback-real-owned-kill-on-close-job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    need(QueryInformationJobObject(owned.handle,JobObjectExtendedLimitInformation,&limits,sizeof(limits),nullptr)&&
        (limits.BasicLimitInformation.LimitFlags&JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE),"fallback-query-actual-kill-on-close");
    struct ProcessList {DWORD assigned;DWORD count;ULONG_PTR ids[8];};ProcessList members{};
    need(waitFor([&]{members={};return QueryInformationJobObject(owned.handle,JobObjectBasicProcessIdList,&members,sizeof(members),nullptr)&&members.count==2&&WaitForSingleObject(rootReady.value,0)==WAIT_OBJECT_0&&WaitForSingleObject(descReady.value,0)==WAIT_OBJECT_0;},3000),"fallback-owned-root-and-descendant-both-running");
    DWORD descendantPid=0;for(DWORD i=0;i<members.count;++i)if(members.ids[i]!=loaderPid)descendantPid=static_cast<DWORD>(members.ids[i]);
    detail::Handle loader{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,loaderPid)};
    detail::Handle descendant{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,descendantPid)};
    detail::Handle observer{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,sentinelPid)};
    BOOL sentinelInRuntime=TRUE,sentinelInOwnJob=FALSE,descendantInRuntime=FALSE;
    need(loader.value&&descendant.value&&observer.value&&WaitForSingleObject(loader.value,0)==WAIT_TIMEOUT&&WaitForSingleObject(descendant.value,0)==WAIT_TIMEOUT,"fallback-owned-tree-live-before-stop");
    need(IsProcessInJob(observer.value,owned.handle,&sentinelInRuntime)&&!sentinelInRuntime&&
        IsProcessInJob(observer.value,sentinelOwner.job.value,&sentinelInOwnJob)&&sentinelInOwnJob&&
        IsProcessInJob(descendant.value,owned.handle,&descendantInRuntime)&&descendantInRuntime,"fallback-sentinel-outside-owned-job");
    const auto leasesBefore=currentprobe::jobHandles(owned.key),plansBefore=harness.plans.load();
    const auto live=harness.client->snapshot();need(live.handles>0&&live.processId>0,"fallback-real-typed-client-live");
    fallbackMeasurements.push_back({{"stage","before-stop"},{"rootActorEntered",WaitForSingleObject(rootReady.value,0)==WAIT_OBJECT_0},{"descendantActorEntered",WaitForSingleObject(descReady.value,0)==WAIT_OBJECT_0},{"loader",ownedCommandLine(loader.value)},{"descendant",ownedCommandLine(descendant.value)},{"binding",live.processId},{"root",loaderPid}});
    need(identity(descendant.value).at("path")==identity(loader.value).at("path")&&ownedCommandLine(descendant.value).value("command",std::string{}).find("--current-owned-fallback-descendant")!=std::string::npos,"fallback-owned-descendant-actual-actor");

    ULONGLONG elapsed=0;{
        FaultScope inject(owned.key,loaderPid,descendantPid,descendant.value);
        const auto began=GetTickCount64();harness.runtime.shutdown();elapsed=GetTickCount64()-began;
    }
    const bool loaderExited=WaitForSingleObject(loader.value,1200)==WAIT_OBJECT_0;
    const bool descendantExited=WaitForSingleObject(descendant.value,1200)==WAIT_OBJECT_0;
    const bool sentinelAlive=WaitForSingleObject(observer.value,0)==WAIT_TIMEOUT&&sentinel.alive();
    const auto destroyed=harness.client->snapshot();const auto remaining=harness.runtime.snapshot();
    const auto denied=currentprobe::deniedCalls.load(),closes=currentprobe::targetCloses.load(),last=currentprobe::lastCloses.load(),aliveBefore=currentprobe::descAliveAtLastClose.load();
    const auto exactRoot=currentprobe::rootTerminateCalls.load(),directDesc=currentprobe::descTerminateCalls.load();
    const auto remainingJobHandles=currentprobe::jobHandles(owned.key);
    CloseHandle(loader.value);loader.value=nullptr;CloseHandle(descendant.value);descendant.value=nullptr;CloseHandle(observer.value);observer.value=nullptr;
    const auto afterHandles=handles();const auto afterThreads=threadCount();
    need(elapsed<4500&&loaderExited&&descendantExited&&!remaining.enabled&&remaining.pid==0,"fallback-bounded-real-owned-tree-exit");
    fallbackMeasurements.push_back({{"deniedTerminateJobObjectCalls",denied},{"ownedJobHandleLeasesBefore",leasesBefore},{"closedOwnedJobHandles",closes},{"lastKillOnCloseHandleClosed",last},{"ownedDescendantAliveBeforeLastJobClose",aliveBefore==1},{"directDescendantTerminateProcessCalls",directDesc},{"elapsedMs",elapsed}});
    need(denied>=2&&leasesBefore>=2&&closes==leasesBefore&&last==1&&aliveBefore==1&&directDesc==0,"fallback-final-owned-job-close-proven-on-live-descendant");
    need(sentinelAlive&&!sentinelInRuntime&&sentinelInOwnJob,"fallback-owned-only-sentinel-unaffected");
    need(destroyed.fullyDestroyed&&destroyed.handles==0&&destroyed.pendingIo==0&&destroyed.activeCalls==0&&destroyed.jobActiveProcesses==0,"fallback-client-owner-resources-destroyed");
    need(remainingJobHandles==0&&jobsBefore==currentprobe::jobHandles()&&beforeHandles==afterHandles&&beforeThreads==afterThreads,"fallback-resources-return-to-original-baseline");
    return {{"fallbackExercised",last==1&&aliveBefore==1&&descendantExited},{"fallbackOwnedOnly",sentinelAlive&&!sentinelInRuntime},
        {"bounded",elapsed<4500},{"elapsedMs",elapsed},{"deadlineMs",4500},{"ownerRevoked",!remaining.enabled},
        {"deniedTerminateJobObjectCalls",denied},{"ownedJobHandleLeasesBefore",leasesBefore},{"closedOwnedJobHandles",closes},{"lastKillOnCloseHandleClosed",last},
        {"ownedDescendantAliveBeforeLastJobClose",aliveBefore==1},{"directDescendantTerminateProcessCalls",directDesc},{"preciseRootTerminateProcessCalls",exactRoot},
        {"ownedLoaderPid",loaderPid},{"ownedDescendantPid",descendantPid},{"ownedLoaderExited",loaderExited},{"ownedDescendantExited",descendantExited},
        {"sentinelPid",sentinelPid},{"sentinelAliveAfterStop",sentinelAlive},{"sentinelInRuntimeJob",sentinelInRuntime!=FALSE},{"sentinelInSeparateOwnedJob",sentinelInOwnJob!=FALSE},
        {"atExit",state(destroyed)},{"active",state(live)},{"remainingOwnedJobHandleLeases",remainingJobHandles},
        {"parentHandlesBefore",beforeHandles},{"parentHandlesAfter",afterHandles},{"parentThreadsBefore",beforeThreads},{"parentThreadsAfter",afterThreads},
        {"bindingPlansBeforeStop",plansBefore},{"bindingPlansAfterStop",harness.plans.load()},
        {"injection","ERROR_ACCESS_DENIED only for actual Runtime Loader Job and its retained duplicate; all other Jobs/API calls forward unchanged. Retained exact-root termination is observed, never masked."}};
}
static unsigned policyChecks=0;
static void check(bool value,const char* label){need(value,label);++policyChecks;}
using ymcc::deckysidebar::LoaderCrashBudget;
using ymcc::deckysidebar::ConnectionRetryWindow;
#include "current-crash-policy.inc"
inline Json threadTable(){
    using Query=LONG(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);
    const auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationThread"));
    need(query!=nullptr,"current-thread-query-api");
    detail::Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0)};need(snapshot.value&&snapshot.value!=INVALID_HANDLE_VALUE,"current-thread-table-snapshot");
    THREADENTRY32 entry{sizeof(entry)};need(Thread32First(snapshot.value,&entry)!=FALSE,"current-thread-table-first");Json rows=Json::array();
    do{if(entry.th32OwnerProcessID!=GetCurrentProcessId())continue;
        detail::Handle thread{OpenThread(THREAD_QUERY_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ThreadID)};
        if(!thread.value){rows.push_back({{"id",entry.th32ThreadID},{"queryError",GetLastError()}});continue;}
        FILETIME birth{},exit{},kernel{},user{};need(GetThreadTimes(thread.value,&birth,&exit,&kernel,&user)!=FALSE,"current-thread-identity-time");
        PVOID address=nullptr;const auto status=query(thread.value,9,&address,sizeof(address),nullptr);
        MEMORY_BASIC_INFORMATION memory{};wchar_t module[32768]{};
        if(status>=0&&address&&VirtualQuery(address,&memory,sizeof(memory)))GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase),module,32768);
        const auto utf8=std::filesystem::path(module).u8string();
        rows.push_back({{"id",entry.th32ThreadID},{"created",(static_cast<unsigned long long>(birth.dwHighDateTime)<<32)|birth.dwLowDateTime},
            {"startAddress",reinterpret_cast<unsigned long long>(address)},{"queryStatus",status},{"module",std::string(utf8.begin(),utf8.end())},
            {"signaled",WaitForSingleObject(thread.value,0)==WAIT_OBJECT_0}});
    }while(Thread32Next(snapshot.value,&entry));return rows;
}
static Json cryptoThreadsBefore=Json::array(),cryptoThreadsAfter=Json::array();
struct CrashCapture {
    CrashCapture(){need(currentprobe::recordCount==0,"fresh-owned-crash-capture");currentprobe::capture=true;}
    void close(){currentprobe::capture=false;for(unsigned i=0;i<currentprobe::recordCount;++i)if(currentprobe::records[i].process){CloseHandle(currentprobe::records[i].process);currentprobe::records[i].process=nullptr;}}
    ~CrashCapture(){close();}
};
inline Json crashBudget(const std::filesystem::path& output){
    currentprobe::stage="crash-policy";policy();Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    CrashCapture capture;const auto sessionHome=output/"s",explicitHome=output/"e";
    currentprobe::stage="crash-harness-construct";Harness session(sessionHome,context),explicitReset(explicitHome,context);
    EnvironmentScope mode(L"YMCC_CURRENT_PARENT_INERT_MODE",L"crash");
    const auto threadsBefore=threadTable();const auto beforeHandles=handles();const auto beforeThreads=threadCount();const auto jobsBefore=currentprobe::jobHandles();
    currentprobe::stage="crash-home-identity";const auto sessionKey=std::filesystem::absolute(sessionHome).wstring(),explicitKey=std::filesystem::absolute(explicitHome).wstring();
    const auto max=ymcc::deckysidebar::LoaderCrashBudget::maxUnstableExits;
    currentprobe::stage="crash-two-runtime-enable";const auto began=GetTickCount64();need(session.runtime.setEnabled(true)&&explicitReset.runtime.setEnabled(true),"both-current-runtime-crash-budgets-enabled");
    currentprobe::stage="crash-active-thread-table";const auto threadsActive=threadTable();
    currentprobe::stage="crash-first-budget-wait";need(waitFor([&]{return session.runtime.snapshot().reason=="loader-crash-loop"&&explicitReset.runtime.snapshot().reason=="loader-crash-loop";},24000),"two-current-runtime-five-crash-limits");
    const auto sessionLimit=currentprobe::ownedCount(sessionKey.c_str()),explicitLimit=currentprobe::ownedCount(explicitKey.c_str());
    need(sessionLimit==max&&explicitLimit==max,"each-current-runtime-exactly-five-real-owned-crashes");
    const auto limitMs=GetTickCount64()-began;
    unsigned demands=0;
    for(auto* harness:{&session,&explicitReset})for(unsigned i=0;i<20;++i){harness->runtime.requestTransportRecovery();harness->runtime.requestBindingRefresh(1000+i);harness->runtime.requestLaunchAdmissionRefresh();harness->runtime.requestConnectionCheck();harness->runtime.requestPreparationRefresh(1000+i);harness->runtime.observeSteam(-1);harness->runtime.observeSteam(1);demands+=7;}
    Sleep(400);
    const auto sessionAfterUnknown=currentprobe::ownedCount(sessionKey.c_str()),explicitAfterUnknown=currentprobe::ownedCount(explicitKey.c_str());
    need(sessionAfterUnknown==sessionLimit&&explicitAfterUnknown==explicitLimit&&session.runtime.snapshot().reason=="loader-crash-loop"&&explicitReset.runtime.snapshot().reason=="loader-crash-loop","unknown-and-recovery-cannot-renew-either-current-crash-budget");
    // Two independently exhausted current Runtime owners allow both legitimate
    // reset rules to execute without serial duplicate backoff windows. Neither
    // native budget/algorithm nor failure count is shortened or seeded.
    setEnvironment(L"YMCC_CURRENT_PARENT_INERT_MODE",L"hold");
    session.runtime.observeSteam(1,true);need(explicitReset.runtime.setEnabled(true),"explicit-enable-on-independent-blocked-runtime");
    need(waitFor([&]{return currentprobe::ownedCount(sessionKey.c_str())==sessionLimit+1&&currentprobe::ownedCount(explicitKey.c_str())==explicitLimit+1&&session.runtime.snapshot().pid!=0&&explicitReset.runtime.snapshot().pid!=0;},2500),"both-legitimate-resets-start-real-owned-loader");
    const auto verifiedLoader=session.runtime.snapshot().pid,explicitLoader=explicitReset.runtime.snapshot().pid;
    const auto sessionAfterReset=currentprobe::ownedCount(sessionKey.c_str()),explicitAfterReset=currentprobe::ownedCount(explicitKey.c_str());
    need(verifiedLoader!=explicitLoader,"independent-reset-owned-processes");
    session.runtime.setEnabled(false);explicitReset.runtime.setEnabled(false);
    const auto sessionOff=session.client->snapshot(),explicitOff=explicitReset.client->snapshot();
    for(auto* harness:{&session,&explicitReset})for(unsigned i=0;i<16;++i){harness->runtime.observeSteam(1,true);harness->runtime.observeSteam(-1);harness->runtime.observeSteam(1);harness->runtime.requestTransportRecovery();harness->runtime.requestBindingRefresh(10000+i);harness->runtime.requestLaunchAdmissionRefresh();harness->runtime.requestConnectionCheck();harness->runtime.requestPreparationRefresh(10000+i);}
    Sleep(400);
    const bool offHonored=!session.runtime.snapshot().enabled&&!explicitReset.runtime.snapshot().enabled&&session.runtime.snapshot().pid==0&&explicitReset.runtime.snapshot().pid==0&&currentprobe::ownedCount(sessionKey.c_str())==sessionAfterReset&&currentprobe::ownedCount(explicitKey.c_str())==explicitAfterReset;
    need(offHonored,"off-vetoes-verified-sessions-unknown-and-recovery-on-both-current-runtimes");
    Json processes=Json::array();bool allExited=true,allCrashCodes=true;unsigned crashes=0,resets=0,sessionSequence=0,explicitSequence=0;
    need(currentprobe::recordCount==2*max+2&&!currentprobe::overflow,"actual-owned-crash-and-reset-process-count");
    for(unsigned i=0;i<currentprobe::recordCount;++i){
        const auto& record=currentprobe::records[i];const bool verified=lstrcmpiW(record.home,sessionKey.c_str())==0;
        need(verified||lstrcmpiW(record.home,explicitKey.c_str())==0,"captured-owned-process-has-exact-current-runtime-home");
        const auto sequence=verified?++sessionSequence:++explicitSequence;
        const bool exited=WaitForSingleObject(record.process,0)==WAIT_OBJECT_0;DWORD code=STILL_ACTIVE;
        need(GetExitCodeProcess(record.process,&code)!=FALSE,"owned-crash-process-exit-code");allExited=allExited&&exited;
        if(sequence<=max){++crashes;allCrashCodes=allCrashCodes&&code==37;}else{++resets;need(sequence==max+1,"exactly-one-real-reset-launch-per-owner");}
        processes.push_back({{"pid",record.pid},{"createdAt",record.createdAt},{"exited",exited},{"exitCode",code},{"fixtureInertImage",true},{"owner",verified?"verified-session":"explicit-enable"},{"sequence",sequence}});
    }
    capture.close();const auto afterHandles=handles();const auto afterThreads=threadCount();const auto threadsAfter=threadTable();
    need(allExited&&allCrashCodes&&crashes==2*max&&resets==2&&context.alive(),"ten-real-crashes-two-reset-children-exited-context-unaffected");
    for(const auto* destroyed:{&sessionOff,&explicitOff})need(destroyed->fullyDestroyed&&destroyed->handles==0&&destroyed->pendingIo==0&&destroyed->activeCalls==0&&destroyed->jobActiveProcesses==0,"both-crash-budget-client-owner-resources-destroyed");
    const Json measured{{"schema","ymcc.current-decky-crash-measurement.v2"},{"executionLayout","two independent current Runtime owners, concurrent exact five-crash budgets; verified-session reset on one blocked owner and explicit-enable reset on the other"},
        {"parentHandlesBefore",beforeHandles},{"parentHandlesAfter",afterHandles},{"parentThreadsBefore",beforeThreads},{"parentThreadsAfter",afterThreads},
        {"cryptoThreadsBefore",cryptoThreadsBefore},{"cryptoThreadsAfter",cryptoThreadsAfter},{"threadsBefore",threadsBefore},{"threadsActive",threadsActive},{"threadsAfter",threadsAfter},
        {"jobHandleLeasesBefore",jobsBefore},{"jobHandleLeasesAfter",currentprobe::jobHandles()},{"atExit",state(sessionOff)},{"explicitOwnerAtExit",state(explicitOff)},
        {"firstBudgetAttempts",sessionLimit},{"secondBudgetAttempts",explicitLimit},{"bothLimitMs",limitMs},{"additionalStartsAfterUnknownRecovery",(sessionAfterUnknown-sessionLimit)+(explicitAfterUnknown-explicitLimit)},
        {"verifiedSessionResetExecuted",sessionAfterReset==sessionLimit+1},{"explicitEnableResetExecuted",explicitAfterReset==explicitLimit+1},
        {"explicitOffHonored",offHonored},{"startsAfterOff",(currentprobe::ownedCount(sessionKey.c_str())-sessionAfterReset)+(currentprobe::ownedCount(explicitKey.c_str())-explicitAfterReset)},
        {"ownedInertProcesses",processes},{"everyOwnedChildExited",allExited},{"tenCrashExitCodesAre37",allCrashCodes},{"contextAlive",context.alive()},
        {"originalPolicyChecks",policyChecks},{"resourceAssertionPassed",beforeHandles==afterHandles&&beforeThreads==afterThreads&&jobsBefore==currentprobe::jobHandles()}};
    std::ofstream(output.parent_path()/"crash-budget-measurements.json",std::ios::binary)<<measured.dump(2)<<'\n';
    need(beforeHandles==afterHandles&&beforeThreads==afterThreads&&jobsBefore==currentprobe::jobHandles(),"crash-budget-all-owned-process-observers-jobs-and-worker-cleaned");
    return {{"limitReached",true},{"attempts",sessionLimit},{"maxAttempts",max},{"secondBudgetAttempts",explicitLimit},{"bothLimitMs",limitMs},
        {"additionalStarts",(sessionAfterUnknown-sessionLimit)+(explicitAfterUnknown-explicitLimit)},{"ordinaryDemandDeliveries",demands},
        {"verifiedNewSessionResetExecuted",sessionAfterReset==sessionLimit+1},{"explicitEnableResetExecuted",explicitAfterReset==explicitLimit+1},
        {"verifiedResetLoaderPid",verifiedLoader},{"explicitResetLoaderPid",explicitLoader},{"explicitOffHonored",offHonored},{"startsAfterOff",(currentprobe::ownedCount(sessionKey.c_str())-sessionAfterReset)+(currentprobe::ownedCount(explicitKey.c_str())-explicitAfterReset)},
        {"ownedInertLoaderLaunches",currentprobe::recordCount},{"ownedInertProcesses",processes},{"everyOwnedChildExited",allExited},{"tenRealCrashExitCodesAre37",allCrashCodes},
        {"atExit",state(sessionOff)},{"explicitOwnerAtExit",state(explicitOff)},{"parentHandlesBefore",beforeHandles},{"parentHandlesAfter",afterHandles},
        {"parentThreadsBefore",beforeThreads},{"parentThreadsAfter",afterThreads},{"cefUnaffected",context.alive()},{"originalPurePolicyChecks",policyChecks},
        {"stableUptimeRuleScope","unchanged original policy() exercises exact 30000ms input boundary as pure function; no claim of actual 30s healthy uptime"}};
}
inline bool signalFallbackActorEntered(const wchar_t* key){
    const auto name=environment(key);if(name.empty())return false;
    detail::Handle event{OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str())};
    return event.value&&SetEvent(event.value)!=FALSE;
}
inline int inertLoader(){
    const auto mode=environment(L"YMCC_CURRENT_PARENT_INERT_MODE");if(mode==L"crash")return 37;
    if(mode==L"job-tree"){
        // Runtime has already assigned this Loader to its real kill-on-close
        // Job before resume. Its child inherits that Job; it ignores cancel.
        const auto image=detail::executable();std::wstring command=L"\""+image+L"\" --current-owned-fallback-descendant";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
        if(!CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))return 92;
        CloseHandle(child.hThread);CloseHandle(child.hProcess);
        if(!signalFallbackActorEntered(L"YMCC_CURRENT_PARENT_ROOT_READY"))return 93;
    }
    Sleep(30000);return 0;
}
}
int main(int argc,char** argv){
    int code=0;if(rtfixture::tryStalledChild(code))return code;if(ymcc::deckybinding::fixture::tryRunChild(code))return code;
    if(argc==1)return currentparent::inertLoader();
    if(argc==2&&std::string(argv[1])=="--current-owned-fallback-descendant"){if(!currentparent::signalFallbackActorEntered(L"YMCC_CURRENT_PARENT_DESC_READY"))return 94;Sleep(30000);return 0;}
    if(argc!=2)return 64;
    using namespace currentparent;
    const auto actualImage=detail::executable();lstrcpynW(currentprobe::image,actualImage.c_str(),32768);
    const auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
    const auto faultPath=(output.parent_path()/"native-fault.json").wstring();lstrcpynW(currentprobe::faultFile,faultPath.c_str(),32768);SetUnhandledExceptionFilter(currentprobe::faultFilter);
    cryptoThreadsBefore=threadTable();
    const auto calibration=initializeSharedCryptoForMeasurement();cryptoThreadsAfter=threadTable();Json cases=Json::array();
    const auto test=[&](const char* id,const auto& action){currentprobe::stage=id;try{cases.push_back({{"id",id},{"passed",true},{"measurements",action()}});}catch(const std::exception& e){cases.push_back({{"id",id},{"passed",false},{"error",e.what()}});}std::ofstream(output.parent_path()/"native-progress.json",std::ios::binary)<<cases.dump(2)<<'\n';};
    test("native.exit-clears-owned-process-job-worker-event",[&]{return stopAndLate(output/"e",{},false);});
    test("native.explicit-off-and-fatal-off-veto-late-wakes",[&]{const auto user=stopAndLate(output/"o",{},false),fatal=stopAndLate(output/"f","decky-render-failed",false);return Json{{"userOff",user},{"fatalOff",fatal},{"explicitOffHonored",true},{"fatalOffHonored",true}};});
    test("native.late-actual-recovery-callback-after-exit",[&]{return stopAndLate(output/"l",{},true);});
    test("native.committed-ready-executes-original-rebind-consumer",[&]{return committedReady(output/"p");});
    test("native.job-stop-failure-falls-back-to-owned-job-close",[&]{return jobCloseFallback(output/"j");});
    test("native.crash-budget-cannot-renew-on-recovery-or-unknown",[&]{return crashBudget(output/"b");});
    const auto passed=std::count_if(cases.begin(),cases.end(),[](const Json& row){return row.value("passed",false);});
    const Json report{{"schema","ymcc.current-decky-parent-native.v1"},{"passed",static_cast<std::size_t>(passed)==cases.size()},
        {"cases",cases},{"remainingTrackedJobHandles",currentprobe::jobHandles()},{"resourceLedgerOverflow",currentprobe::overflow.load()},{"sharedCryptoInitialization",calibration},{"productCallbackMode","typed-only; legacy macro absent"},
        {"fallbackMeasurements",fallbackMeasurements},{"fullPersistentAcceptance",false},{"officialLoaderExecuted",false},{"realSteamOrDeviceActions",0},{"operatingSystemSleep",false}};
    std::ofstream(output.parent_path()/"native-result.json",std::ios::binary)<<report.dump(2)<<'\n';
    std::cout<<report.dump(2)<<'\n';return report.at("passed").get<bool>()?0:1;
}