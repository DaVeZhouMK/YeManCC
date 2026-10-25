#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
#include "battery_host_process_owner.h"
using Json=nlohmann::json;
using ymcc::batteryhost::Code;
using ymcc::batteryhost::Win32Calls;
namespace {
int fixtureHandles=0; unsigned serial=0;
void require(bool value,const char* name){if(!value)throw std::runtime_error(name);}
struct Handle {
    HANDLE h=nullptr;
    explicit Handle(HANDLE value=nullptr):h(value){if(h&&h!=INVALID_HANDLE_VALUE)++fixtureHandles;}
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
    ~Handle(){if(h&&h!=INVALID_HANDLE_VALUE){CloseHandle(h);--fixtureHandles;}}
};
std::wstring exe(){wchar_t path[32768]{};require(GetModuleFileNameW(nullptr,path,32768)>0,"FIXTURE_EXE_PATH");return path;}
std::wstring quote(const std::wstring& value){require(value.find(L'"')==value.npos,"FIXTURE_ARGUMENT_QUOTE");return L"\""+value+L"\"";}
std::string narrow(const std::wstring& value){auto text=std::filesystem::path(value).u8string();return {text.begin(),text.end()};}
std::wstring eventName(const std::wstring& token,const wchar_t* suffix){return L"Local\\YMCC_BHPO_"+token+suffix;}
struct Ports {
    std::wstring token=std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(++serial);
    Handle ready{CreateEventW(nullptr,TRUE,FALSE,eventName(token,L"_ready").c_str())};
    Handle exit{CreateEventW(nullptr,TRUE,FALSE,eventName(token,L"_exit").c_str())};
    Handle descendantReady{CreateEventW(nullptr,TRUE,FALSE,eventName(token,L"_desc").c_str())};
    explicit Ports(){require(ready.h&&exit.h&&descendantReady.h,"OWNED_EVENTS_REQUIRED");}
};
struct Launch {
    PROCESS_INFORMATION pi{};
    explicit Launch(const wchar_t* mode,const Ports& ports,const std::filesystem::path& root){
        auto image=exe(),command=quote(image)+L" --child "+mode+L" "+ports.token+L" "+quote(root.wstring());
        STARTUPINFOW si{sizeof(si)};
        require(CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE,"OWNED_SUSPENDED_CREATE_FAILED");
    }
    Launch(const Launch&)=delete;Launch& operator=(const Launch&)=delete;
    ~Launch(){cleanup();}
    void cleanup() noexcept {
        if(pi.hProcess){if(WaitForSingleObject(pi.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(pi.hProcess,ERROR_CANCELLED);WaitForSingleObject(pi.hProcess,2000);}CloseHandle(pi.hProcess);pi.hProcess=nullptr;}
        if(pi.hThread){CloseHandle(pi.hThread);pi.hThread=nullptr;}
    }
};
Handle duplicate(HANDLE process){HANDLE copy=nullptr;require(DuplicateHandle(GetCurrentProcess(),process,GetCurrentProcess(),&copy,SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,0)!=FALSE,"DUPLICATE_EXACT_PROCESS_OBSERVER");return Handle(copy);}
int child(int argc,wchar_t** argv){
    if(argc!=5)return 250;
    const std::wstring mode=argv[2],token=argv[3];const std::filesystem::path root=argv[4];
    Handle ready{OpenEventW(EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,eventName(token,L"_ready").c_str())};
    Handle exit{OpenEventW(SYNCHRONIZE,FALSE,eventName(token,L"_exit").c_str())};
    Handle desc{OpenEventW(EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,eventName(token,L"_desc").c_str())};
    if(!ready.h||!exit.h||!desc.h)return 251;
    if(mode==L"leaf"){SetEvent(desc.h);return WaitForSingleObject(exit.h,10000)==WAIT_OBJECT_0?0:124;}
    if(mode==L"tree"||mode==L"exit-with-descendant"){
        auto image=exe(),command=quote(image)+L" --child leaf "+token+L" "+quote(root.wstring());STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
        if(!CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return 252;
        // The descendant inherits the existing Job through real Windows semantics.
        {Handle process{pi.hProcess};Handle thread{pi.hThread};FILETIME birth{},end{},k{},u{};
         GetProcessTimes(process.h,&birth,&end,&k,&u);std::ofstream out(root/(L"desc-"+token+L".json"));out<<Json{{"pid",pi.dwProcessId},{"birth",(std::uint64_t(birth.dwHighDateTime)<<32)|birth.dwLowDateTime},{"sourceBuiltInert",true}}.dump();}
        if(WaitForSingleObject(desc.h,2000)!=WAIT_OBJECT_0)return 253;
    }
    SetEvent(ready.h);
    if(mode==L"exit-with-descendant")return 0;
    return WaitForSingleObject(exit.h,10000)==WAIT_OBJECT_0?0:124;
}
Json state(const ymcc::batteryhost::Snapshot& s){return {{"generation",s.generation},{"launchedGeneration",s.launchedGeneration},{"pid",s.processId},{"birth",s.processBirth},{"image",narrow(s.image)},{"ownedHandles",s.ownedHandles},{"closeFailures",s.closeFailures},{"processExited",s.processExited},{"activeJobProcesses",s.activeJobProcesses},{"jobZeroObserved",s.jobZeroObserved},{"jobLimitFlags",s.jobLimitFlags},{"admissionSealed",s.admissionSealed},{"resourcesDestroyed",s.resourcesDestroyed},{"hardwareRestoreConfirmed",s.hardwareRestoreConfirmed},{"terminationAttempted",s.terminationAttempted},{"cleanupError",s.cleanupError},{"resumed",s.resumed}};}
struct Control {
    HANDLE job=nullptr,port=nullptr,process=nullptr;
    bool failSet=false,failAssign=false,failResume=false,failTerminate=false,failCloseJob=false;
    bool assignmentBeforeResume=false;unsigned resumes=0;std::vector<HANDLE> jobs,ports;
};
struct Calls:Win32Calls {
    Control* c=nullptr;
    explicit Calls(Control* control):c(control){}
    HANDLE createJob()noexcept{auto h=Win32Calls::createJob();if(h){c->job=h;c->jobs.push_back(h);}return h;}
    HANDLE createPort()noexcept{auto h=Win32Calls::createPort();if(h){c->port=h;c->ports.push_back(h);}return h;}
    BOOL setKillOnClose(HANDLE h)noexcept{if(c->failSet){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return Win32Calls::setKillOnClose(h);}
    BOOL assign(HANDLE job,HANDLE process)noexcept{if(c->failAssign){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}auto ok=Win32Calls::assign(job,process);if(ok)c->process=process;return ok;}
    DWORD resume(HANDLE thread)noexcept{
        ++c->resumes;DWORD flags=0;BOOL assigned=FALSE;
        c->assignmentBeforeResume=Win32Calls::jobLimits(c->job,flags)&&(flags&JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)&&IsProcessInJob(c->process,c->job,&assigned)&&assigned;
        if(c->failResume){SetLastError(ERROR_ACCESS_DENIED);return DWORD(-1);}return Win32Calls::resume(thread);
    }
    BOOL terminateJob(HANDLE h)noexcept{if(c->failTerminate){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return Win32Calls::terminateJob(h);}
    BOOL terminateProcess(HANDLE h)noexcept{if(c->failTerminate){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return Win32Calls::terminateProcess(h);}
    BOOL close(HANDLE h)noexcept{if(h==c->job&&c->failCloseJob){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}auto ok=Win32Calls::close(h);if(ok){c->jobs.erase(std::remove(c->jobs.begin(),c->jobs.end(),h),c->jobs.end());c->ports.erase(std::remove(c->ports.begin(),c->ports.end(),h),c->ports.end());}return ok;}
};
using Owner=ymcc::batteryhost::BasicProcessOwner<Calls>;
struct FaultCleanup {
    Control& c;
    ~FaultCleanup(){c.failSet=c.failAssign=c.failResume=c.failTerminate=c.failCloseJob=false;Owner::retryRetained(1500);}
};
void empty(const Control& c){require(c.jobs.empty()&&c.ports.empty(),"REAL_OWNED_JOB_AND_PORT_HANDLE_LEDGER_MUST_BE_ZERO");}
DWORD census(){DWORD count=0;require(GetProcessHandleCount(GetCurrentProcess(),&count)!=FALSE,"REAL_HANDLE_CENSUS");return count;}
Json treeStop(const std::filesystem::path& root){
    Control c;FaultCleanup ensure{c};Json result;Json checkpoints={{"enter",census()}};
    {Ports sentinelPorts;Launch sentinel(L"sentinel",sentinelPorts,root);require(ResumeThread(sentinel.pi.hThread)==1,"SENTINEL_START");require(WaitForSingleObject(sentinelPorts.ready.h,2000)==WAIT_OBJECT_0,"SENTINEL_READY");checkpoints["sentinelReady"]=census();
     Ports ports;Launch launch(L"tree",ports,root);auto rootObserver=duplicate(launch.pi.hProcess);const auto pid=launch.pi.dwProcessId;Owner owner(Calls{&c});auto ticket=owner.admission();require(WaitForSingleObject(ports.ready.h,0)==WAIT_TIMEOUT,"SUSPENDED_CHILD_CANNOT_RUN_BEFORE_ASSIGN");checkpoints["suspendedRoot"]=census();
     const auto start=owner.adoptSuspended(launch.pi,CREATE_SUSPENDED|CREATE_NO_WINDOW,exe(),ticket);require(start.code==Code::ok&&start.transferred&&!launch.pi.hProcess,"EXACT_PI_TAKEOVER");require(c.assignmentBeforeResume,"JOB_KILL_ON_CLOSE_AND_ASSIGN_MUST_PRECEDE_RESUME");require(WaitForSingleObject(ports.ready.h,2000)==WAIT_OBJECT_0,"OWNED_TREE_READY");checkpoints["rootReady"]=census();auto active=owner.observe();std::cerr<<"TREE_CENSUS "<<state(active.state).dump()<<std::endl;require(active.state.processId==pid&&active.state.processBirth!=0&&active.state.activeJobProcesses>=2,"EXACT_ROOT_AND_DESCENDANT_JOB_CENSUS");
     std::ifstream input(root/(L"desc-"+ports.token+L".json"));Json descendant;input>>descendant;
     Handle descObserver{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,descendant.at("pid").get<DWORD>())};require(descObserver.h!=nullptr,"EXACT_INERT_DESCENDANT_OBSERVER");BOOL inJob=FALSE;require(IsProcessInJob(descObserver.h,c.job,&inJob)&&inJob,"EXACT_SOURCE_DESCENDANT_MUST_BELONG_TO_OWN_JOB");FILETIME born{},end{},k{},u{};require(GetProcessTimes(descObserver.h,&born,&end,&k,&u)&&((std::uint64_t(born.dwHighDateTime)<<32)|born.dwLowDateTime)==descendant.at("birth").get<std::uint64_t>(),"DESCENDANT_PID_BIRTH_MATCH");
     checkpoints["descRead"]=census();const auto clock=GetTickCount64();auto stopped=owner.stop(0,2000);require(stopped.code==Code::ok&&stopped.state.resourcesDestroyed&&stopped.state.ownedHandles==0&&stopped.state.jobZeroObserved,"STOP_MUST_REAP_ROOT_DESCENDANT_AND_ALL_HANDLES");require(GetTickCount64()-clock<=2200,"STOP_MUST_REMAIN_WITHIN_ORIGINAL_SMALL_BUDGET");const auto ownerStopElapsedMs=GetTickCount64()-clock;auto remaining=[&]()->DWORD{const auto elapsed=GetTickCount64()-clock;return elapsed>=2000?0:DWORD(2000-elapsed);};require(WaitForSingleObject(rootObserver.h,remaining())==WAIT_OBJECT_0&&WaitForSingleObject(descObserver.h,remaining())==WAIT_OBJECT_0,"REAL_ROOT_AND_DESCENDANT_EXIT_OBSERVED");const auto descendantSignalElapsedMs=GetTickCount64()-clock;require(descendantSignalElapsedMs<=2200,"DESCENDANT_SIGNAL_MUST_USE_SAME_ORIGINAL_STOP_BUDGET");require(WaitForSingleObject(sentinel.pi.hProcess,0)==WAIT_TIMEOUT,"INDEPENDENT_SENTINEL_MUST_SURVIVE_OWNER_STOP");auto repeat=owner.stop(0,2000);require(repeat.state.resourcesDestroyed&&repeat.state.ownedHandles==0,"STOP_IDEMPOTENT");checkpoints["afterStop"]=census();
     result={{"ownerStopElapsedMs",ownerStopElapsedMs},{"rootAndDescendantSignalElapsedMs",descendantSignalElapsedMs},{"sameStopBudgetMs",2000},{"descendant",descendant},{"before",state(active.state)},{"after",state(stopped.state)},{"independentSentinelAliveAfterStop",true},{"actualRootAndDescendantSignaled",true},{"assignmentBeforeResume",c.assignmentBeforeResume}};
     SetEvent(sentinelPorts.exit.h);require(WaitForSingleObject(sentinel.pi.hProcess,2000)==WAIT_OBJECT_0,"SENTINEL_OWN_EXIT");checkpoints["sentinelExited"]=census();}
    empty(c);checkpoints["leave"]=census();result["checkpoints"]=checkpoints;return result;
}
Json naturalExit(const std::filesystem::path& root){
    Ports p;Launch launch(L"normal",p,root);auto process=duplicate(launch.pi.hProcess);ymcc::batteryhost::ProcessOwner owner;auto r=owner.adoptSuspended(launch.pi,CREATE_SUSPENDED,exe(),owner.admission());require(r.code==Code::ok,"DEFAULT_OWNER_START");require(WaitForSingleObject(p.ready.h,2000)==WAIT_OBJECT_0,"NORMAL_CHILD_READY");SetEvent(p.exit.h);require(WaitForSingleObject(process.h,2000)==WAIT_OBJECT_0,"NORMAL_CHILD_NATURAL_EXIT");auto done=owner.observe(2000);std::cerr<<"NATURAL_CENSUS "<<state(done.state).dump()<<std::endl;require(done.state.resourcesDestroyed&&done.state.ownedHandles==0&&!done.state.terminationAttempted,"NATURAL_EXIT_MUST_REAP_WITHOUT_TERMINATE");return state(done.state);
}
Json rootExitNotTreeExit(const std::filesystem::path& root){
    Control c;FaultCleanup ensure{c};Json result;
    {Ports p;Launch launch(L"exit-with-descendant",p,root);auto process=duplicate(launch.pi.hProcess);Owner owner(Calls{&c});require(owner.adoptSuspended(launch.pi,CREATE_SUSPENDED,exe(),owner.admission()).code==Code::ok,"TREE_NATURAL_ROOT_START");require(WaitForSingleObject(p.ready.h,2000)==WAIT_OBJECT_0&&WaitForSingleObject(process.h,2000)==WAIT_OBJECT_0,"ROOT_EXIT_AFTER_DESCENDANT_START");auto observed=owner.observe();std::cerr<<"ROOT_EXIT_CENSUS "<<state(observed.state).dump()<<std::endl;require(observed.state.processExited&&!observed.state.resourcesDestroyed&&observed.state.activeJobProcesses>=1,"ROOT_EXIT_ALONE_IS_NOT_RESOURCE_DESTRUCTION");auto stopped=owner.stop(0,2000);require(stopped.state.resourcesDestroyed&&stopped.state.ownedHandles==0,"LIVE_DESCENDANT_MUST_BE_REAPED_AFTER_ROOT_EXIT");result={{"afterRootExit",state(observed.state)},{"afterStop",state(stopped.state)}};}
    empty(c);return result;
}
Json lateAdopt(const std::filesystem::path& root){
    Control c;FaultCleanup ensure{c};Json result;bool denied=false;
    {Ports p;Launch launch(L"normal",p,root);Owner owner(Calls{&c});const auto ticket=owner.admission();auto stop=owner.stop(0,1000);require(stop.state.resourcesDestroyed,"STOP_BEFORE_START_EMPTY");const auto original=launch.pi.hProcess;auto late=owner.adoptSuspended(launch.pi,CREATE_SUSPENDED,exe(),ticket);
     denied=late.code==Code::stopped&&!late.transferred&&launch.pi.hProcess==original&&owner.admission()==0&&c.resumes==0&&WaitForSingleObject(p.ready.h,0)==WAIT_TIMEOUT;
     result={{"afterStop",state(stop.state)},{"late",state(late.state)},{"lateTransferred",late.transferred},{"callerStillOwnsRejectedPI",launch.pi.hProcess==original},{"resumeCalls",c.resumes}};
     // A mutant may have resumed it. Cleanup exact transferred handles BEFORE reporting the assertion.
     owner.stop(0,2000);launch.cleanup();}
    empty(c);require(denied,"LATE_ADOPT_MUST_BE_REJECTED_WITH_HANDLES_UNTRANSFERRED");return result;
}
Json identityReject(const std::filesystem::path& root){
    Control c;FaultCleanup ensure{c};Json result;
    {Ports p;Launch launch(L"normal",p,root);Owner owner(Calls{&c});const auto pid=launch.pi.dwProcessId;launch.pi.dwProcessId=pid+1;auto wrong=owner.adoptSuspended(launch.pi,CREATE_SUSPENDED,exe(),owner.admission());require(wrong.code==Code::identityMismatch&&!wrong.transferred&&launch.pi.hProcess,"PID_MISMATCH_CANNOT_TRANSFER_OR_RESUME");launch.pi.dwProcessId=pid;auto image=owner.adoptSuspended(launch.pi,CREATE_SUSPENDED,exe()+L".other",owner.admission());require(image.code==Code::identityMismatch&&!image.transferred&&c.resumes==0,"IMAGE_MISMATCH_CANNOT_TRANSFER_OR_RESUME");auto generation=owner.adoptSuspended(launch.pi,CREATE_SUSPENDED,exe(),owner.admission()+1);require(generation.code==Code::stopped&&!generation.transferred,"WRONG_GENERATION_REFUSED");result={{"pidImageGenerationRejected",true},{"callerRetainedExactHandles",true},{"resumeCalls",c.resumes}};}
    empty(c);return result;
}
Json startFailure(const std::filesystem::path& root,int phase){
    Control c;FaultCleanup ensure{c};c.failSet=phase==0;c.failAssign=phase==1;c.failResume=phase==2;Json result;
    {Ports p;Launch launch(L"normal",p,root);auto process=duplicate(launch.pi.hProcess);Owner owner(Calls{&c});auto r=owner.adoptSuspended(launch.pi,CREATE_SUSPENDED,exe(),owner.admission());require(r.code!=Code::ok&&r.transferred&&r.state.resourcesDestroyed&&r.state.ownedHandles==0,"FAILED_START_MUST_KEEP_OR_DESTROY_EXACT_OWNERSHIP");require(WaitForSingleObject(process.h,0)==WAIT_OBJECT_0&&WaitForSingleObject(p.ready.h,0)==WAIT_TIMEOUT,"FAILED_SETUP_MUST_NOT_RUN_CHILD");require(c.resumes==(phase==2?1u:0u),"SETUP_FAILURE_CANNOT_RESUME_BEFORE_ASSIGN");result=state(r.state);}
    empty(c);return result;
}
Json failedStopRetry(const std::filesystem::path& root,bool closeFault){
    Control c;FaultCleanup ensure{c};Json result;
    {Ports p;Launch launch(L"normal",p,root);Owner owner(Calls{&c});require(owner.adoptSuspended(launch.pi,CREATE_SUSPENDED,exe(),owner.admission()).code==Code::ok,"RETRY_CHILD_START");require(WaitForSingleObject(p.ready.h,2000)==WAIT_OBJECT_0,"RETRY_CHILD_READY");c.failTerminate=!closeFault;c.failCloseJob=closeFault;auto failed=owner.stop(0,1000);require(failed.code==Code::cleanupIncomplete&&!failed.state.resourcesDestroyed&&failed.state.ownedHandles>0&&failed.state.admissionSealed&&!c.jobs.empty(),"FAILED_STOP_MUST_RETAIN_EXACT_PROCESS_OR_JOB");if(!closeFault)require(!failed.state.processExited,"FAILED_TERMINATE_MUST_NOT_CLAIM_PROCESS_EXIT");c.failTerminate=false;c.failCloseJob=false;auto recovered=owner.stop(0,2000);require(recovered.state.resourcesDestroyed&&recovered.state.ownedHandles==0,"RETRY_MUST_RELEASE_ORIGINAL_HANDLES");result={{"failed",state(failed.state)},{"retry",state(recovered.state)}};}
    empty(c);return result;
}
Json failedRollbackRetention(const std::filesystem::path& root){
    Control c;FaultCleanup ensure{c};c.failSet=true;c.failTerminate=true;Json result;
    {Ports p;Launch launch(L"normal",p,root);auto process=duplicate(launch.pi.hProcess);Owner owner(Calls{&c});
     const auto failed=owner.adoptSuspended(launch.pi,CREATE_SUSPENDED,exe(),owner.admission(),1000);
     require(failed.code==Code::cleanupIncomplete&&failed.transferred&&!launch.pi.hProcess&&!failed.state.resourcesDestroyed&&failed.state.ownedHandles>=3,"FAILED_PREASSIGN_ROLLBACK_MUST_RETAIN_EXACT_PROCESS_THREAD_JOB");
     require(WaitForSingleObject(process.h,0)==WAIT_TIMEOUT&&WaitForSingleObject(p.ready.h,0)==WAIT_TIMEOUT&&c.resumes==0,"FAILED_ROLLBACK_CHILD_MUST_STAY_SUSPENDED");
     c.failTerminate=false;c.failSet=false;const auto retry=owner.stop(0,2000);
     require(retry.state.resourcesDestroyed&&WaitForSingleObject(process.h,0)==WAIT_OBJECT_0,"PREASSIGN_ROLLBACK_RETRY_MUST_REAP_ORIGINAL_PROCESS");
     result={{"failed",state(failed.state)},{"retry",state(retry.state)}};}
    empty(c);return result;
}
Json naturalCloseFailure(const std::filesystem::path& root){
    Control c;FaultCleanup ensure{c};Json result;
    {Ports p;Launch launch(L"normal",p,root);auto process=duplicate(launch.pi.hProcess);Owner owner(Calls{&c});
     require(owner.adoptSuspended(launch.pi,CREATE_SUSPENDED,exe(),owner.admission()).code==Code::ok,"NATURAL_CLOSE_START");
     require(WaitForSingleObject(p.ready.h,2000)==WAIT_OBJECT_0,"NATURAL_CLOSE_READY");SetEvent(p.exit.h);
     require(WaitForSingleObject(process.h,2000)==WAIT_OBJECT_0,"NATURAL_CLOSE_EXIT");c.failCloseJob=true;
     const auto failed=owner.observe(2000);
     require(failed.code==Code::cleanupIncomplete&&!failed.state.resourcesDestroyed&&failed.state.ownedHandles==1&&!failed.state.terminationAttempted&&c.jobs.size()==1,"NATURAL_CLOSE_FAILURE_MUST_REPORT_AND_RETAIN_EXACT_JOB");
     c.failCloseJob=false;const auto retry=owner.observe(2000);
     require(retry.code==Code::ok&&retry.state.resourcesDestroyed&&!retry.state.terminationAttempted,"NATURAL_CLOSE_RETRY_MUST_RELEASE_WITHOUT_TERMINATE");
     result={{"failed",state(failed.state)},{"retry",state(retry.state)}};}
    empty(c);return result;
}
Json invalidBudget(const std::filesystem::path& root){
    Control c;FaultCleanup ensure{c};Json result;
    {Ports p;Launch launch(L"normal",p,root);Owner owner(Calls{&c});require(owner.adoptSuspended(launch.pi,CREATE_SUSPENDED,exe(),owner.admission()).code==Code::ok,"BUDGET_CHILD_START");require(WaitForSingleObject(p.ready.h,2000)==WAIT_OBJECT_0,"BUDGET_CHILD_READY");auto bad=owner.stop(0,INFINITE);require(bad.code==Code::invalidArgument&&!bad.state.resourcesDestroyed&&bad.state.ownedHandles>0&&bad.state.admissionSealed,"INVALID_BUDGET_CANNOT_CLAIM_DESTROYED_OR_DROP_HANDLES");auto done=owner.stop(0,2000);require(done.state.resourcesDestroyed,"BUDGET_RETRY_REAPS_EXACT_OWNER");result={{"invalid",state(bad.state)},{"retry",state(done.state)}};}
    empty(c);return result;
}
Json destructorRetention(const std::filesystem::path& root){
    Control c;FaultCleanup ensure{c};Json result;
    {Ports p;Launch launch(L"normal",p,root);auto owner=std::make_unique<Owner>(Calls{&c});require(owner->adoptSuspended(launch.pi,CREATE_SUSPENDED,exe(),owner->admission()).code==Code::ok,"DESTRUCTOR_CHILD_START");require(WaitForSingleObject(p.ready.h,2000)==WAIT_OBJECT_0,"DESTRUCTOR_CHILD_READY");c.failCloseJob=true;owner.reset();require(Owner::retainedCount()==1&&c.jobs.size()==1,"DESTRUCTOR_FAILURE_MUST_RETAIN_JOB_LEDGER");c.failCloseJob=false;require(Owner::retryRetained(2000)==0&&c.jobs.empty()&&c.ports.empty(),"EXPLICIT_RETRY_MUST_DRAIN_RETAINED_LEDGER");result={{"retainedAfterCloseFailure",1},{"retainedAfterExplicitRetry",0}};}
    empty(c);return result;
}
}
// First CreateProcess can initialize process-wide Windows/CRT resources. Establish
// that baseline with two inert natural exits BEFORE constructing any owner. The
// second complete launch must add zero real handles; every owner case then uses
// exact equality against that measured, stable process baseline.
Json initializeFixtureRuntime(const std::filesystem::path& root){
    const auto before=census();DWORD afterFirst=0,afterSecond=0;
    for(unsigned i=0;i<2;++i){
        {Ports ports;Launch launch(L"normal",ports,root);
         require(ResumeThread(launch.pi.hThread)==1,"BASELINE_INERT_RESUME");
         require(WaitForSingleObject(ports.ready.h,2000)==WAIT_OBJECT_0,"BASELINE_INERT_READY");
         SetEvent(ports.exit.h);
         require(WaitForSingleObject(launch.pi.hProcess,2000)==WAIT_OBJECT_0,"BASELINE_INERT_NATURAL_EXIT");}
        require(fixtureHandles==0,"BASELINE_EXACT_HANDLES_ZERO");
        if(i==0)afterFirst=census();else afterSecond=census();
    }
    require(afterFirst==afterSecond,"SECOND_BASELINE_LAUNCH_MUST_ADD_ZERO_REAL_HANDLES");
    return {{"beforeFirstCreateProcess",before},{"afterFirstNaturalExit",afterFirst},{"afterSecondNaturalExit",afterSecond},{"fixtureHandles",fixtureHandles},{"ownerConstructed",false},{"secondLaunchHandleDelta",int(afterSecond)-int(afterFirst)}};
}
int wmain(int argc,wchar_t** argv){
    if(argc>1&&std::wstring(argv[1])==L"--child")return child(argc,argv);
    if(argc!=4)return 250;const std::wstring selected=argv[2];const std::filesystem::path root=argv[3];std::filesystem::create_directories(root);Json cases=Json::array();int failed=0;
    std::ofstream(root/L"journal-untouched.json")<<"original-unconfirmed-recovery-marker";
    const auto fixtureRuntimeBaseline=initializeFixtureRuntime(root);
    auto test=[&](const char* name,auto fn){if(selected!=L"all"&&selected!=std::filesystem::path(name).wstring())return;const auto clock=GetTickCount64();DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);Json detail;try{detail=fn();require(fixtureHandles==0,"FIXTURE_EXACT_HANDLE_COUNTER_ZERO");require(Owner::retainedCount()==0,"RETAINED_OWNER_LEDGER_ZERO_AT_CASE_END");GetProcessHandleCount(GetCurrentProcess(),&after);require(before==after,"REAL_PROCESS_HANDLE_COUNT_MUST_RETURN_TO_BASELINE");cases.push_back({{"name",name},{"passed",true},{"detail",detail},{"elapsedMs",GetTickCount64()-clock},{"realHandlesBefore",before},{"realHandlesAfter",after}});}catch(const std::exception& error){++failed;GetProcessHandleCount(GetCurrentProcess(),&after);cases.push_back({{"name",name},{"passed",false},{"detail",detail},{"error",error.what()},{"elapsedMs",GetTickCount64()-clock},{"realHandlesBefore",before},{"realHandlesAfter",after}});}};
    test("start-stop-tree-sentinel",[&]{return treeStop(root);});
    test("natural-exit",[&]{return naturalExit(root);});
    test("root-exit-retains-descendant",[&]{return rootExitNotTreeExit(root);});
    test("late-adopt",[&]{return lateAdopt(root);});
    test("exact-identity",[&]{return identityReject(root);});
    test("job-setup-failure",[&]{return startFailure(root,0);});
    test("assignment-failure",[&]{return startFailure(root,1);});
    test("resume-failure",[&]{return startFailure(root,2);});
    test("terminate-failure-retry",[&]{return failedStopRetry(root,false);});
    test("close-failure-retry",[&]{return failedStopRetry(root,true);});
    test("preassign-rollback-failure-retains-owner",[&]{return failedRollbackRetention(root);});
    test("natural-close-failure-retains-job",[&]{return naturalCloseFailure(root);});
    test("invalid-budget-retains-owner",[&]{return invalidBudget(root);});
    test("destructor-retains-failed-job",[&]{return destructorRetention(root);});
    std::ifstream journal(root/L"journal-untouched.json");std::string marker;std::getline(journal,marker);if(marker!="original-unconfirmed-recovery-marker")++failed;
    Json report={{"fixtureRuntimeBaseline",fixtureRuntimeBaseline},{"status",failed?"FAIL":"PASS"},{"cases",cases},{"failed",failed},{"fixtureHandles",fixtureHandles},{"retainedOwners",Owner::retainedCount()},{"sourceBuiltInertChildAndDescendant",true},{"journalUnchanged",marker=="original-unconfirmed-recovery-marker"},{"hardwareRestoreConfirmed",false},{"productionBatteryHostStarted",false},{"actualPipeWmiEcDevice",false}};
    std::ofstream(root/L"native-result.json")<<report.dump(2)<<'\n';std::cout<<report.dump()<<std::endl;return failed?23:0;
}