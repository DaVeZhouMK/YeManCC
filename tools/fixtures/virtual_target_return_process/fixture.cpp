#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "virtual_target_return_record.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <map>

// Only the production leaf calls below are intercepted. None means real Win32.
// All fault values are explicitly synthetic and do not claim real PID reuse,
// access denial or device observations. Fixture process management stays real.
namespace leafTest {
enum class Fault { None, OpenDenied, OpenMissing, OpenOther, OpenNoError,
    PidError, PidMismatch, TimesError, ZeroBirth, ChangedBirth,
    WaitError, WaitOther, WaitAbandoned };
inline Fault fault = Fault::None;
inline unsigned opens=0, closes=0, pids=0, times=0, waits=0;
inline DWORD lastAccess=0, lastPid=0, lastTimeout=MAXDWORD;
inline BOOL lastInherit=TRUE;
inline std::map<HANDLE,bool> outstanding;
inline void reset(Fault next=Fault::None) {
    if(!outstanding.empty()) throw std::runtime_error("previous probe leaked handles");
    fault=next; opens=closes=pids=times=waits=0;
    lastAccess=lastPid=0;lastTimeout=MAXDWORD;lastInherit=TRUE;
}
inline HANDLE open(DWORD access,BOOL inherit,DWORD pid) {
    ++opens;lastAccess=access;lastInherit=inherit;lastPid=pid;
    if(fault==Fault::OpenDenied){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
    if(fault==Fault::OpenMissing){SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
    if(fault==Fault::OpenOther){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}
    if(fault==Fault::OpenNoError){SetLastError(ERROR_SUCCESS);return nullptr;}
    HANDLE handle=::OpenProcess(access,inherit,pid);
    if(handle)outstanding.emplace(handle,true);
    return handle;
}
inline BOOL close(HANDLE handle) {
    ++closes;outstanding.erase(handle);return ::CloseHandle(handle);
}
inline DWORD pid(HANDLE handle) {
    ++pids;
    if(fault==Fault::PidError){SetLastError(ERROR_ACCESS_DENIED);return 0;}
    const DWORD value=::GetProcessId(handle);
    if(value&&fault==Fault::PidMismatch)return value+1;
    return value;
}
inline BOOL getTimes(HANDLE handle,LPFILETIME birth,LPFILETIME exit,LPFILETIME kernel,LPFILETIME user) {
    ++times;
    if(fault==Fault::TimesError){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    const BOOL ok=::GetProcessTimes(handle,birth,exit,kernel,user);
    if(ok&&fault==Fault::ZeroBirth)*birth=FILETIME{};
    if(ok&&fault==Fault::ChangedBirth){if(++birth->dwLowDateTime==0)++birth->dwHighDateTime;}
    return ok;
}
inline DWORD wait(HANDLE handle,DWORD timeout) {
    ++waits;lastTimeout=timeout;
    if(fault==Fault::WaitError){SetLastError(ERROR_ACCESS_DENIED);return WAIT_FAILED;}
    if(fault==Fault::WaitOther)return 77;
    if(fault==Fault::WaitAbandoned)return WAIT_ABANDONED;
    return ::WaitForSingleObject(handle,timeout);
}
inline void reclaimFaultLeaks() {
    for(auto [handle,unused]:outstanding){(void)unused;::CloseHandle(handle);}
    outstanding.clear();
}
}
#define OpenProcess leafTest::open
#define GetProcessId leafTest::pid
#define GetProcessTimes leafTest::getTimes
#define CloseHandle leafTest::close
#define WaitForSingleObject leafTest::wait
#include "virtual_target_return_process.h"
#undef OpenProcess
#undef GetProcessId
#undef GetProcessTimes
#undef CloseHandle
#undef WaitForSingleObject

namespace tr=ymcc::targetReturn;
namespace proc=ymcc::targetReturn::process;
using J=nlohmann::json;
struct Handle {
    HANDLE value=nullptr;
    Handle()=default;
    explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)::CloseHandle(value);}
    Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;
    Handle(Handle&& other)noexcept:value(other.value){other.value=nullptr;}
    Handle&operator=(Handle&& other)noexcept{if(this!=&other){if(value)::CloseHandle(value);value=other.value;other.value=nullptr;}return *this;}
};
struct Child {
    Handle process,thread;
    DWORD pid=0;
    Child()=default;Child(const Child&)=delete;Child&operator=(const Child&)=delete;
    ~Child(){if(process.value&&::WaitForSingleObject(process.value,1000)==WAIT_TIMEOUT){::TerminateProcess(process.value,99);::WaitForSingleObject(process.value,2000);}}
};
std::wstring executablePath(){std::wstring value(32768,L'\0');DWORD count=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));if(!count||count==value.size())throw std::runtime_error("private fixture executable path unavailable");value.resize(count);return value;}
std::wstring handleArg(HANDLE value){return std::to_wstring(reinterpret_cast<std::uintptr_t>(value));}
HANDLE parsedHandle(const wchar_t* value){return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(value)));}
void start(Child& child,const std::wstring& arguments){
    const auto exe=executablePath();std::wstring command=L"\""+exe+L"\" "+arguments;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION pi{};
    if(!::CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&pi))throw std::runtime_error("could not start private fixture child");
    child.process=Handle(pi.hProcess);child.thread=Handle(pi.hThread);child.pid=pi.dwProcessId;
}
Handle event(){SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};Handle result(::CreateEventW(&sa,TRUE,FALSE,nullptr));if(!result.value)throw std::runtime_error("private event creation failed");return result;}
std::uint64_t birthValue(const FILETIME& value){return(std::uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
tr::ProcessIdentity actualIdentity(HANDLE process){FILETIME birth{},exit{},kernel{},user{};const auto pid=::GetProcessId(process);if(!pid||!::GetProcessTimes(process,&birth,&exit,&kernel,&user)||!birthValue(birth))throw std::runtime_error("real process identity query failed");return{pid,std::to_string(birthValue(birth))};}
unsigned assertions=0;
J failures=J::array(),cases=J::array(),observations=J::array();
std::string current;
void check(bool condition,const char* text){++assertions;if(!condition)failures.push_back({{"case",current},{"assertion",text}});}
void run(const char* name,const char* basis,const std::function<void()>& test){current=name;const auto before=failures.size();try{leafTest::reset();test();}catch(const std::exception& error){failures.push_back({{"case",current},{"exception",error.what()}});}check(leafTest::outstanding.empty(),"probe must close every opened handle on every return path");leafTest::reclaimFaultLeaks();cases.push_back({{"name",name},{"basis",basis},{"passed",failures.size()==before}});}
void expect(const proc::Probe& got,proc::State state,proc::Reason reason,DWORD error){check(got.state==state,"exact Alive/Dead/Unknown classification");check(got.reason==reason,"exact diagnostic reason");check(got.win32Error==error,"original Win32 error or explicit data error preserved");}
void singleOpen(bool successful){check(leafTest::opens==1,"one exact PID open, no scan or retry");check(leafTest::lastAccess==(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE),"exact minimal query plus synchronization access");check(leafTest::lastInherit==FALSE,"probe handle is noninheritable");check(leafTest::closes==(successful?1u:0u),"every successful open closes exactly once");}
int wmain(int argc,wchar_t** argv){
    if(argc==4&&std::wstring(argv[1])==L"--child"){
        const HANDLE ready=parsedHandle(argv[2]),stop=parsedHandle(argv[3]);
        if(!::SetEvent(ready))return 81;return ::WaitForSingleObject(stop,20000)==WAIT_OBJECT_0?17:82;
    }
    if(argc==5&&std::wstring(argv[1])==L"--parent"){
        try{const HANDLE ready=parsedHandle(argv[2]),stop=parsedHandle(argv[3]);Child worker;
            start(worker,L"--child "+handleArg(ready)+L" "+handleArg(stop));
            if(::WaitForSingleObject(ready,5000)!=WAIT_OBJECT_0)return 83;
            const auto id=actualIdentity(worker.process.value);
            {std::ofstream info(std::filesystem::path(argv[4]),std::ios::binary);info<<J({{"pid",id.pid},{"creationTime",id.creationTime}}).dump();if(!info)return 84;}
            // This private parent exits without stopping its private worker.
            // Closing our handles does not terminate the still-running process.
            worker.thread=Handle();worker.process=Handle();return 0;
        }catch(...){return 85;}
    }
    if(argc!=2)return 80;
    const std::filesystem::path resultPath(argv[1]);
    try{
        Handle ready=event(),stop=event();Child child;
        start(child,L"--child "+handleArg(ready.value)+L" "+handleArg(stop.value));
        if(::WaitForSingleObject(ready.value,5000)!=WAIT_OBJECT_0)throw std::runtime_error("private child did not become ready");
        const auto id=actualIdentity(child.process.value);
        observations.push_back({{"basis","real-win32"},{"role","private-child"},{"pid",id.pid},{"birth",id.creationTime}});
        run("real/capture-borrowed-process","real-win32",[&]{DWORD error=99;const auto got=proc::capture(child.process.value,&error);check(got&&got->pid==id.pid&&got->creationTime==id.creationTime,"capture matches independent real PID and FILETIME");check(error==ERROR_SUCCESS,"successful capture clears optional error");check(leafTest::opens==0&&leafTest::closes==0&&leafTest::pids==1&&leafTest::times==1,"capture queries once and never consumes borrowed handle");check(::WaitForSingleObject(child.process.value,0)==WAIT_TIMEOUT,"caller process handle remains usable");});
        run("real/capture-self-pseudo-handle","real-win32",[]{const auto idSelf=actualIdentity(::GetCurrentProcess());const auto got=proc::capture(::GetCurrentProcess());check(got&&got->pid==idSelf.pid&&got->creationTime==idSelf.creationTime,"valid current-process pseudo handle accepted");check(leafTest::closes==0,"pseudo handle is never consumed");});
        run("real/capture-event-rejected","real-win32",[&]{DWORD error=0;const auto got=proc::capture(ready.value,&error);check(!got&&error!=0,"event cannot be captured as a process");check(::WaitForSingleObject(ready.value,0)==WAIT_OBJECT_0,"borrowed event remains open");check(leafTest::closes==0,"rejection does not consume borrowed event");});
        run("real/capture-null-rejected","real-win32",[]{DWORD error=0;check(!proc::capture(nullptr,&error),"null process rejected");check(error!=0,"null query reports actual API error");check(leafTest::closes==0,"null handle never closed");});
        run("real/capture-restricted-handle","real-win32",[&]{HANDLE raw=nullptr;if(!::DuplicateHandle(::GetCurrentProcess(),child.process.value,::GetCurrentProcess(),&raw,SYNCHRONIZE,FALSE,0))throw std::runtime_error("restricted handle creation failed");Handle restricted(raw);DWORD error=0;check(!proc::capture(restricted.value,&error),"synchronize-only handle lacks capture query right");check(error==ERROR_ACCESS_DENIED,"actual denied query remains observable");check(::WaitForSingleObject(restricted.value,0)==WAIT_TIMEOUT,"restricted borrowed handle still usable");});
        run("real/probe-alive","real-win32",[&]{expect(proc::probeRecordedOwner(id),proc::State::Alive,proc::Reason::MatchingRunning,ERROR_SUCCESS);singleOpen(true);check(leafTest::lastPid==id.pid&&leafTest::pids==1&&leafTest::times==1&&leafTest::waits==1,"exact identity queried and waited once");check(leafTest::lastTimeout==0,"probe never blocks or creates a wait budget");});
        run("real/probe-handle-count","real-win32",[&]{(void)proc::probeRecordedOwner(id);DWORD before=0,after=0;if(!::GetProcessHandleCount(::GetCurrentProcess(),&before))throw std::runtime_error("real handle count failed");for(unsigned i=0;i<128;++i)check(proc::probeRecordedOwner(id).state==proc::State::Alive,"repeated one-shot probe sees live private child");check(::GetProcessHandleCount(::GetCurrentProcess(),&after)!=0,"final actual handle count read");check(before==after,"128 probes leave real OS handle count unchanged");check(leafTest::opens==129&&leafTest::closes==129,"all repeated real opens paired with closes");});
        const std::vector<std::pair<const char*,tr::ProcessIdentity>> invalid={
            {"zero-pid",{0,id.creationTime}}, {"empty-birth",{id.pid,""}}, {"zero-birth",{id.pid,"0"}},
            {"leading-zero",{id.pid,"01"}}, {"negative",{id.pid,"-1"}}, {"plus",{id.pid,"+1"}},
            {"space",{id.pid," 1"}}, {"suffix",{id.pid,"1x"}}, {"overflow",{id.pid,"18446744073709551616"}},
            {"too-long",{id.pid,"123456789012345678901"}}, {"embedded-nul",{id.pid,std::string("1\0",2)}}};
        for(const auto& [name,bad]:invalid){const auto title="invalid/"+std::string(name);run(title.c_str(),"controlled-input",[&]{expect(proc::probeRecordedOwner(bad),proc::State::Unknown,proc::Reason::InvalidIdentity,ERROR_INVALID_DATA);check(leafTest::opens==0&&leafTest::waits==0,"invalid identity cannot open any process");});}
        struct FaultCase{const char* name;leafTest::Fault fault;proc::State state;proc::Reason reason;DWORD error;bool opened;};
        const FaultCase faults[]={
            {"controlled/open-access-denied",leafTest::Fault::OpenDenied,proc::State::Unknown,proc::Reason::OpenFailed,ERROR_ACCESS_DENIED,false},
            {"controlled/open-valid-pid-not-found",leafTest::Fault::OpenMissing,proc::State::Dead,proc::Reason::PidNotFound,ERROR_INVALID_PARAMETER,false},
            {"controlled/open-other-error",leafTest::Fault::OpenOther,proc::State::Unknown,proc::Reason::OpenFailed,ERROR_NOT_ENOUGH_MEMORY,false},
            {"controlled/open-no-error",leafTest::Fault::OpenNoError,proc::State::Unknown,proc::Reason::OpenFailed,ERROR_SUCCESS,false},
            {"controlled/pid-query-error",leafTest::Fault::PidError,proc::State::Unknown,proc::Reason::PidQueryFailed,ERROR_ACCESS_DENIED,true},
            {"controlled/pid-mismatch",leafTest::Fault::PidMismatch,proc::State::Unknown,proc::Reason::PidMismatch,ERROR_INVALID_DATA,true},
            {"controlled/birth-query-error",leafTest::Fault::TimesError,proc::State::Unknown,proc::Reason::BirthQueryFailed,ERROR_ACCESS_DENIED,true},
            {"controlled/birth-zero",leafTest::Fault::ZeroBirth,proc::State::Unknown,proc::Reason::BirthMissing,ERROR_INVALID_DATA,true},
            {"controlled/pid-reuse",leafTest::Fault::ChangedBirth,proc::State::Dead,proc::Reason::PidReused,ERROR_SUCCESS,true},
            {"controlled/wait-error",leafTest::Fault::WaitError,proc::State::Unknown,proc::Reason::WaitFailed,ERROR_ACCESS_DENIED,true},
            {"controlled/wait-other",leafTest::Fault::WaitOther,proc::State::Unknown,proc::Reason::UnexpectedWait,ERROR_INVALID_DATA,true},
            {"controlled/wait-abandoned",leafTest::Fault::WaitAbandoned,proc::State::Unknown,proc::Reason::UnexpectedWait,ERROR_INVALID_DATA,true}};
        for(const auto& test:faults)run(test.name,"controlled-win32-leaf-fault",[&]{leafTest::fault=test.fault;expect(proc::probeRecordedOwner(id),test.state,test.reason,test.error);singleOpen(test.opened);if(test.fault==leafTest::Fault::ChangedBirth)check(leafTest::waits==0,"birth mismatch proves only original recorded owner dead, without waiting on replacement");});
        run("controlled/capture-birth-error","controlled-win32-leaf-fault",[&]{leafTest::fault=leafTest::Fault::TimesError;DWORD error=0;check(!proc::capture(child.process.value,&error)&&error==ERROR_ACCESS_DENIED,"failed real-time query cannot manufacture a birth");check(leafTest::closes==0,"failed capture still borrows");});
        run("controlled/capture-zero-birth","controlled-win32-leaf-fault",[&]{leafTest::fault=leafTest::Fault::ZeroBirth;DWORD error=0;check(!proc::capture(child.process.value,&error)&&error==ERROR_INVALID_DATA,"zero birth rejected");check(leafTest::closes==0,"zero-birth capture still borrows");});
        run("controlled/capture-high-filetime","controlled-win32-leaf-fault",[&]{const FILETIME time{0xffffffff,0xffffffff};check(proc::leafDetail::fileTimeValue(time)==UINT64_MAX,"FILETIME combines unsigned high and low bits exactly");});
        run("real/parent-exit-worker-alive","real-win32",[&]{
            Handle treeReady=event(),treeStop=event();Child parent;
            const auto infoPath=resultPath.parent_path()/L"private-worker-identity.json";
            start(parent,L"--parent "+handleArg(treeReady.value)+L" "+handleArg(treeStop.value)+L" \""+infoPath.wstring()+L"\"");
            const auto parentId=actualIdentity(parent.process.value);
            if(::WaitForSingleObject(parent.process.value,5000)!=WAIT_OBJECT_0)throw std::runtime_error("private parent did not exit");
            DWORD exitCode=0;check(::GetExitCodeProcess(parent.process.value,&exitCode)&&exitCode==0,"private parent completed normally");
            J saved;{std::ifstream file(infoPath,std::ios::binary);file>>saved;}
            const tr::ProcessIdentity workerId{saved.at("pid").get<DWORD>(),saved.at("creationTime").get<std::string>()};
            Handle worker(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,workerId.pid));
            expect(proc::probeRecordedOwner(parentId),proc::State::Dead,proc::Reason::MatchingExited,ERROR_SUCCESS);
            expect(proc::probeRecordedOwner(workerId),proc::State::Alive,proc::Reason::MatchingRunning,ERROR_SUCCESS);
            observations.push_back({{"basis","real-win32"},{"role","private-worker-outlives-parent"},{"parentPid",parentId.pid},{"workerPid",workerId.pid}});
            check(::SetEvent(treeStop.value)!=0,"private worker asked to exit by its own event");
            check(worker.value&&::WaitForSingleObject(worker.value,5000)==WAIT_OBJECT_0,"private worker exit reaped independently");
            expect(proc::probeRecordedOwner(workerId),proc::State::Dead,proc::Reason::MatchingExited,ERROR_SUCCESS);
        });
        run("real/capture-exited-process","real-win32",[&]{check(::SetEvent(stop.value)!=0,"private child stop signal");check(::WaitForSingleObject(child.process.value,5000)==WAIT_OBJECT_0,"private child exit independently observed");DWORD code=0;check(::GetExitCodeProcess(child.process.value,&code)&&code==17,"child exits with its expected private code");const auto got=proc::capture(child.process.value);check(got&&got->pid==id.pid&&got->creationTime==id.creationTime,"identity can still be captured from held exited process");check(leafTest::closes==0,"exited borrowed handle still not consumed");});
        run("real/probe-exited","real-win32",[&]{expect(proc::probeRecordedOwner(id),proc::State::Dead,proc::Reason::MatchingExited,ERROR_SUCCESS);singleOpen(true);check(leafTest::waits==1&&leafTest::lastTimeout==0,"terminal result comes from nonblocking real process wait");});
        run("diagnostics/names","pure-diagnostic",[]{check(std::string(proc::stateName(proc::State::Alive))=="alive","stable alive diagnostic");check(std::string(proc::stateName(proc::State::Dead))=="dead","stable dead diagnostic");check(std::string(proc::stateName(proc::State::Unknown))=="unknown","stable unknown diagnostic");check(std::string(proc::reasonName(proc::Reason::PidReused))=="pid-reused","distinct reuse reason");check(std::string(proc::reasonName(proc::Reason::OpenFailed))=="open-failed","distinct open reason");});
    }catch(const std::exception& error){failures.push_back({{"case","fixture/setup-or-cleanup"},{"exception",error.what()}});}
    J result={{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",assertions},{"cases",cases},{"failures",failures},{"observations",observations},{"realPrivateChildProcesses",true},{"syntheticCasesExplicitlyMarked",true},{"productRuntimeIntegrated",false},{"deviceOperations",0},{"productOrSteamRun",false}};
    std::ofstream out(resultPath,std::ios::binary);out<<result.dump(2)<<"\n";
    std::cout<<J({{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",assertions},{"failures",failures}}).dump()<<"\n";
    return failures.empty()?0:1;
}
