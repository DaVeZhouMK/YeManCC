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
#include <cstring>
#include <cstddef>
#include <vector>

// Only the production leaf calls below are intercepted. None means real Win32.
// All fault values are explicitly synthetic and do not claim real PID reuse,
// access denial or device observations. Fixture process management stays real.
namespace leafTest {
enum class Fault { None, OpenDenied, OpenMissing, OpenOther, OpenNoError,
    PidError, PidMismatch, TimesError, ZeroBirth, ChangedBirth,
    WaitError, WaitOther, WaitAbandoned };
inline Fault fault = Fault::None;
enum class CensusMode { Automatic, Real, NoModule, NoExport, Fake };
enum class SelfFault { None, PidError, TimesError, ZeroBirth };
// Independently encoded ABI input: these are bytes for the production parser,
// never a replacement implementation of its liveness decision.
struct CensusUnicodeString { USHORT length, maximumLength; PWSTR buffer; };
struct CensusPrefix {
    ULONG nextEntryOffset, threadCount; BYTE reserved[24];
    LARGE_INTEGER creation, user, kernel; CensusUnicodeString imageName;
    LONG basePriority; HANDLE uniqueProcessId;
};
struct QueryStep { LONG status; ULONG returned; };
inline constexpr LONG lengthMismatch=static_cast<LONG>(0xc0000004u);
inline constexpr LONG bufferTooSmall=static_cast<LONG>(0xc0000023u);
inline constexpr LONG accessDenied=static_cast<LONG>(0xc0000022u);
inline constexpr ULONG initialCapacity=64*1024, maximumCapacity=8*1024*1024;
inline CensusMode censusMode=CensusMode::Automatic;
inline SelfFault selfFault=SelfFault::None;
inline std::vector<BYTE> snapshot;
inline std::vector<QueryStep> querySteps;
inline std::vector<ULONG> capacities;
inline unsigned modules=0,exports=0,selfHandles=0,selfPids=0,selfTimes=0,queries=0;
using QueryFunction=LONG(NTAPI*)(ULONG,PVOID,ULONG,PULONG);
inline QueryFunction realQuery=nullptr;
inline bool correctModule=true,correctExport=true,correctQuery=true;
inline unsigned opens=0, closes=0, pids=0, times=0, waits=0;
inline DWORD lastAccess=0, lastPid=0, lastTimeout=MAXDWORD;
inline BOOL lastInherit=TRUE;
inline std::map<HANDLE,bool> outstanding;
inline void reset(Fault next=Fault::None) {
    if(!outstanding.empty()) throw std::runtime_error("previous probe leaked handles");
    fault=next; opens=closes=pids=times=waits=0;
    censusMode=CensusMode::Automatic;selfFault=SelfFault::None;
    snapshot.clear();querySteps.clear();capacities.clear();
    modules=exports=selfHandles=selfPids=selfTimes=queries=0;
    correctModule=correctExport=correctQuery=true;realQuery=nullptr;
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
    if(handle==::GetCurrentProcess()) {
        ++selfPids;
        if(selfFault==SelfFault::PidError){SetLastError(ERROR_ACCESS_DENIED);return 0;}
        return ::GetProcessId(handle);
    }
    ++pids;
    if(fault==Fault::PidError){SetLastError(ERROR_ACCESS_DENIED);return 0;}
    const DWORD value=::GetProcessId(handle);
    if(value&&fault==Fault::PidMismatch)return value+1;
    return value;
}
inline BOOL getTimes(HANDLE handle,LPFILETIME birth,LPFILETIME exit,LPFILETIME kernel,LPFILETIME user) {
    if(handle==::GetCurrentProcess()) {
        ++selfTimes;
        if(selfFault==SelfFault::TimesError){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
        const BOOL ok=::GetProcessTimes(handle,birth,exit,kernel,user);
        if(ok&&selfFault==SelfFault::ZeroBirth)*birth=FILETIME{};
        return ok;
    }
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
inline bool realCensus() {
    return censusMode==CensusMode::Real || (censusMode==CensusMode::Automatic&&fault==Fault::None);
}
inline LONG NTAPI query(ULONG informationClass,PVOID buffer,ULONG capacity,PULONG returned) {
    ++queries;capacities.push_back(capacity);
    correctQuery=correctQuery&&informationClass==5&&buffer&&returned;
    if(!buffer||!returned)return accessDenied;
    if(realCensus())return realQuery?realQuery(informationClass,buffer,capacity,returned):accessDenied;
    // Fill unused capacity with poison. A zero-filled allocation must not hide
    // a truncated record or a parser that ignores ReturnLength.
    std::memset(buffer,0xa5,capacity);
    if(!snapshot.empty())std::memcpy(buffer,snapshot.data(),(std::min)(snapshot.size(),static_cast<std::size_t>(capacity)));
    if(!querySteps.empty()) {
        const auto index=(std::min)(static_cast<std::size_t>(queries-1),querySteps.size()-1);
        *returned=querySteps[index].returned;return querySteps[index].status;
    }
    *returned=static_cast<ULONG>(snapshot.size());
    return snapshot.size()>capacity?lengthMismatch:0;
}
inline HMODULE module(LPCWSTR name) {
    ++modules;correctModule=correctModule&&name&&std::wstring(name)==L"ntdll.dll";
    if(realCensus())return ::GetModuleHandleW(name);
    if(censusMode==CensusMode::NoModule||censusMode==CensusMode::Automatic)return nullptr;
    return reinterpret_cast<HMODULE>(static_cast<std::uintptr_t>(0x107));
}
inline FARPROC address(HMODULE handle,LPCSTR name) {
    ++exports;correctExport=correctExport&&name&&std::strcmp(name,"NtQuerySystemInformation")==0;
    if(realCensus()) {
        const auto found=::GetProcAddress(handle,name);
        realQuery=reinterpret_cast<QueryFunction>(found);
        return found?reinterpret_cast<FARPROC>(&query):nullptr;
    }
    if(censusMode==CensusMode::NoExport)return nullptr;
    return reinterpret_cast<FARPROC>(&query);
}
inline HANDLE currentProcess(){++selfHandles;return ::GetCurrentProcess();}
inline void reclaimFaultLeaks() {
    for(auto [handle,unused]:outstanding){(void)unused;::CloseHandle(handle);}
    outstanding.clear();
}
}
#define GetModuleHandleW leafTest::module
#define GetProcAddress leafTest::address
#define GetCurrentProcess leafTest::currentProcess
#define OpenProcess leafTest::open
#define GetProcessId leafTest::pid
#define GetProcessTimes leafTest::getTimes
#define CloseHandle leafTest::close
#define WaitForSingleObject leafTest::wait
#include "virtual_target_return_process.h"
#undef GetModuleHandleW
#undef GetProcAddress
#undef GetCurrentProcess
#undef OpenProcess
#undef GetProcessId
#undef GetProcessTimes
#undef CloseHandle
#undef WaitForSingleObject

namespace tr=ymcc::targetReturn;
namespace proc=ymcc::targetReturn::process;
static_assert(sizeof(leafTest::CensusPrefix)==sizeof(proc::leafDetail::NativeProcessPrefix));
static_assert(offsetof(leafTest::CensusPrefix,creation)==32);
static_assert(offsetof(leafTest::CensusPrefix,uniqueProcessId)==(sizeof(void*)==8?80:68));
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
using CensusRow=std::pair<std::uintptr_t,LONGLONG>;
void snapshotRows(const std::vector<CensusRow>& rows) {
    leafTest::censusMode=leafTest::CensusMode::Fake;
    leafTest::snapshot.assign(rows.size()*sizeof(leafTest::CensusPrefix),0);
    for(std::size_t i=0;i<rows.size();++i) {
        leafTest::CensusPrefix row{};
        row.nextEntryOffset=i+1<rows.size()?static_cast<ULONG>(sizeof(row)):0;
        row.creation.QuadPart=rows[i].second;
        row.uniqueProcessId=reinterpret_cast<HANDLE>(rows[i].first);
        std::memcpy(leafTest::snapshot.data()+i*sizeof(row),&row,sizeof(row));
    }
}
CensusRow censusRow(const tr::ProcessIdentity& id) {return{id.pid,static_cast<LONGLONG>(std::stoull(id.creationTime))};}
void fakeSnapshot(const tr::ProcessIdentity& target,bool present=true,LONGLONG birthDelta=0) {
    leafTest::fault=leafTest::Fault::OpenDenied;
    const auto self=censusRow(actualIdentity(::GetCurrentProcess()));
    auto owner=censusRow(target);owner.second+=birthDelta;
    if(present)snapshotRows({owner,self,{0,0}});
    else snapshotRows({{0,0},self});
}
void changeRow(std::size_t index,const std::function<void(leafTest::CensusPrefix&)>& edit) {
    leafTest::CensusPrefix row{};const auto offset=index*sizeof(row);
    if(offset+sizeof(row)>leafTest::snapshot.size())throw std::runtime_error("invalid fixture row edit");
    std::memcpy(&row,leafTest::snapshot.data()+offset,sizeof(row));edit(row);
    std::memcpy(leafTest::snapshot.data()+offset,&row,sizeof(row));
}
unsigned assertions=0;
J failures=J::array(),cases=J::array(),observations=J::array();
std::string current;
void check(bool condition,const char* text){++assertions;if(!condition)failures.push_back({{"case",current},{"assertion",text}});}
void run(const char* name,const char* basis,const std::function<void()>& test){current=name;const auto before=failures.size();try{leafTest::reset();test();}catch(const std::exception& error){failures.push_back({{"case",current},{"exception",error.what()}});}check(leafTest::outstanding.empty(),"probe must close every opened handle on every return path");
    check(leafTest::correctModule&&leafTest::correctExport&&leafTest::correctQuery,"census uses exact ntdll export and SystemProcessInformation class 5");
    check(leafTest::queries<=3,"census query count stays within three attempts");
    check(std::all_of(leafTest::capacities.begin(),leafTest::capacities.end(),[](ULONG capacity){return capacity>=leafTest::initialCapacity&&capacity<=leafTest::maximumCapacity;}),"every query allocation stays within 64KiB and 8MiB");
    leafTest::reclaimFaultLeaks();cases.push_back({{"name",name},{"basis",basis},{"passed",failures.size()==before}});}
void expect(const proc::Probe& got,proc::State state,proc::Reason reason,DWORD error){check(got.state==state,"exact Alive/Dead/Unknown classification");check(got.reason==reason,"exact diagnostic reason");check(got.win32Error==error,"original Win32 error or explicit data error preserved");}
void singleOpen(bool successful){check(leafTest::opens==1,"one exact PID open, no direct retry");check(leafTest::lastAccess==(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE),"exact minimal query plus synchronization access");check(leafTest::lastInherit==FALSE,"probe handle is noninheritable");check(leafTest::closes==(successful?1u:0u),"every successful open closes exactly once");}
void noCensus(const proc::Probe& got) {
    check(leafTest::modules==0&&leafTest::exports==0&&leafTest::selfHandles==0&&leafTest::queries==0,"conclusive or invalid direct result never invokes census APIs");
    check(got.directWin32Error==ERROR_SUCCESS&&got.censusAttempts==0&&got.censusBytes==0&&got.censusRows==0&&got.censusNtStatus==0,"unused census diagnostic fields remain zero");
}
void expectCensus(const proc::Probe& got,proc::State state,proc::Reason reason,DWORD error,DWORD directError=ERROR_ACCESS_DENIED) {
    expect(got,state,reason,error);
    check(got.directWin32Error==directError,"original direct Win32 error survives fallback");
    check(got.censusAttempts==leafTest::queries,"reported attempt count matches actual query invocations");
    if(!leafTest::queries)check(got.censusNtStatus==0,"no query means no fabricated raw NTSTATUS");
    else if(leafTest::querySteps.empty())check(got.censusNtStatus==0,"successful fake query reports zero NTSTATUS");
    else check(got.censusNtStatus==static_cast<DWORD>(leafTest::querySteps[(std::min)(static_cast<std::size_t>(leafTest::queries-1),leafTest::querySteps.size()-1)].status),"raw NTSTATUS matches final actual fake query status");
    check(leafTest::modules==1&&leafTest::exports<=1,"one bounded API resolution per ambiguous direct probe");
    if(leafTest::queries) {
        check(leafTest::selfHandles==1&&leafTest::selfPids==1&&leafTest::selfTimes==1,"self PID and birth captured exactly once before snapshot query");
        check(leafTest::capacities.front()==leafTest::initialCapacity,"first census allocation is exactly 64KiB");
    }
    observations.push_back({{"basis","controlled-census-bytes"},{"case",current},{"state",proc::stateName(got.state)},
        {"reason",proc::reasonName(got.reason)},{"directWin32Error",got.directWin32Error},{"censusAttempts",got.censusAttempts},
        {"censusBytes",got.censusBytes},{"censusRows",got.censusRows},{"censusNtStatus",got.censusNtStatus},{"capacities",leafTest::capacities}});
}
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
        run("real/probe-alive","real-win32",[&]{const auto got=proc::probeRecordedOwner(id);expect(got,proc::State::Alive,proc::Reason::MatchingRunning,ERROR_SUCCESS);noCensus(got);singleOpen(true);check(leafTest::lastPid==id.pid&&leafTest::pids==1&&leafTest::times==1&&leafTest::waits==1,"exact identity queried and waited once");check(leafTest::lastTimeout==0,"probe never blocks or creates a wait budget");});
        run("real/probe-handle-count","real-win32",[&]{(void)proc::probeRecordedOwner(id);DWORD before=0,after=0;if(!::GetProcessHandleCount(::GetCurrentProcess(),&before))throw std::runtime_error("real handle count failed");for(unsigned i=0;i<128;++i)check(proc::probeRecordedOwner(id).state==proc::State::Alive,"repeated one-shot probe sees live private child");check(::GetProcessHandleCount(::GetCurrentProcess(),&after)!=0,"final actual handle count read");check(before==after,"128 probes leave real OS handle count unchanged");check(leafTest::opens==129&&leafTest::closes==129,"all repeated real opens paired with closes");});
        const std::vector<std::pair<const char*,tr::ProcessIdentity>> invalid={
            {"zero-pid",{0,id.creationTime}}, {"empty-birth",{id.pid,""}}, {"zero-birth",{id.pid,"0"}},
            {"leading-zero",{id.pid,"01"}}, {"negative",{id.pid,"-1"}}, {"plus",{id.pid,"+1"}},
            {"space",{id.pid," 1"}}, {"suffix",{id.pid,"1x"}}, {"overflow",{id.pid,"18446744073709551616"}},
            {"too-long",{id.pid,"123456789012345678901"}}, {"embedded-nul",{id.pid,std::string("1\0",2)}}};
        for(const auto& [name,bad]:invalid){const auto title="invalid/"+std::string(name);run(title.c_str(),"controlled-input",[&]{const auto got=proc::probeRecordedOwner(bad);expect(got,proc::State::Unknown,proc::Reason::InvalidIdentity,ERROR_INVALID_DATA);noCensus(got);check(leafTest::opens==0&&leafTest::waits==0,"invalid identity cannot open any process");});}
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
        for(const auto& test:faults)run(test.name,"controlled-win32-leaf-fault",[&]{
            leafTest::fault=test.fault;
            expect(proc::probeRecordedOwnerHandle(id),test.state,test.reason,test.error);
            singleOpen(test.opened);
            check(leafTest::modules==0&&leafTest::queries==0,"original handle helper never invokes census");
            leafTest::reset(test.fault);
            const auto got=proc::probeRecordedOwner(id);
            if(test.state==proc::State::Unknown)
                expectCensus(got,proc::State::Unknown,proc::Reason::CensusUnavailable,ERROR_NOT_SUPPORTED,test.error);
            else{expect(got,test.state,test.reason,test.error);noCensus(got);}
            singleOpen(test.opened);
            if(test.fault==leafTest::Fault::ChangedBirth)check(leafTest::waits==0,"birth mismatch proves only original recorded owner dead, without waiting on replacement");
        });
        run("controlled/census-open-denied-pid-reused","controlled-win32-and-census",[&]{
            fakeSnapshot(id,true,1);const auto got=proc::probeRecordedOwner(id);
            expectCensus(got,proc::State::Dead,proc::Reason::CensusPidReused,ERROR_SUCCESS);singleOpen(false);
            check(got.censusRows==3&&got.censusBytes==leafTest::snapshot.size(),"reused PID accepted only after complete census and self guard");
            check(::WaitForSingleObject(child.process.value,0)==WAIT_TIMEOUT,"synthetic dead result does not terminate even the private target process");
        });
        run("controlled/census-open-denied-pid-absent","controlled-win32-and-census",[&]{
            fakeSnapshot(id,false);const auto got=proc::probeRecordedOwner(id);
            expectCensus(got,proc::State::Dead,proc::Reason::CensusPidNotFound,ERROR_SUCCESS);singleOpen(false);
            check(got.censusRows==2,"absence established after self and idle rows scanned");
        });
        run("controlled/census-same-birth-alive","controlled-win32-and-census",[&]{
            fakeSnapshot(id);const auto got=proc::probeRecordedOwner(id);
            expectCensus(got,proc::State::Alive,proc::Reason::CensusMatchingRunning,ERROR_SUCCESS);singleOpen(false);
            check(got.censusRows==3,"matching first target does not bypass later self guard or final row");
        });
        run("controlled/census-target-after-self","controlled-win32-and-census",[&]{
            fakeSnapshot(id);snapshotRows({{0,0},censusRow(actualIdentity(::GetCurrentProcess())),censusRow(id)});
            const auto got=proc::probeRecordedOwner(id);expectCensus(got,proc::State::Alive,proc::Reason::CensusMatchingRunning,ERROR_SUCCESS);
            check(got.censusRows==3,"self-first ordering still requires entire target search");
        });
        run("controlled/census-recorded-self","controlled-win32-and-census",[&]{
            const auto self=actualIdentity(::GetCurrentProcess());leafTest::fault=leafTest::Fault::OpenDenied;
            snapshotRows({censusRow(self),{0,0}});const auto got=proc::probeRecordedOwner(self);
            expectCensus(got,proc::State::Alive,proc::Reason::CensusMatchingRunning,ERROR_SUCCESS);
            check(got.censusRows==2,"one row may prove self layout and exact recorded identity");
        });
        run("controlled/census-only-idle-zero-birth","controlled-win32-and-census",[&]{
            fakeSnapshot(id,false);const auto self=censusRow(actualIdentity(::GetCurrentProcess()));
            std::uintptr_t unrelated=MAXDWORD;while(unrelated==id.pid||unrelated==self.first)--unrelated;
            snapshotRows({{0,0},{unrelated,0},self});const auto got=proc::probeRecordedOwner(id);
            expectCensus(got,proc::State::Dead,proc::Reason::CensusPidNotFound,ERROR_SUCCESS);
            check(got.censusRows==3,"non-target zero birth does not invalidate verified absence");
        });
        for(const auto mode:{leafTest::CensusMode::NoModule,leafTest::CensusMode::NoExport}) {
            const auto name=mode==leafTest::CensusMode::NoModule?"controlled/census-module-unavailable":"controlled/census-export-unavailable";
            run(name,"controlled-api-resolution",[&]{leafTest::fault=leafTest::Fault::OpenDenied;leafTest::censusMode=mode;
                const auto got=proc::probeRecordedOwner(id);expectCensus(got,proc::State::Unknown,proc::Reason::CensusUnavailable,ERROR_NOT_SUPPORTED);
                check(got.censusRows==0&&got.censusBytes==0&&leafTest::selfHandles==0,"missing API never queries or manufactures census proof");
            });
        }
        for(const auto fault:{leafTest::SelfFault::PidError,leafTest::SelfFault::TimesError,leafTest::SelfFault::ZeroBirth}) {
            const auto name=fault==leafTest::SelfFault::PidError?"controlled/census-self-pid-query-error":fault==leafTest::SelfFault::TimesError?"controlled/census-self-birth-query-error":"controlled/census-self-capture-zero-birth";
            run(name,"controlled-self-capture",[&]{fakeSnapshot(id,false);leafTest::selfFault=fault;
                const auto got=proc::probeRecordedOwner(id);
                expectCensus(got,proc::State::Unknown,proc::Reason::CensusSelfUnproven,fault==leafTest::SelfFault::ZeroBirth?ERROR_INVALID_DATA:ERROR_ACCESS_DENIED);
                check(leafTest::queries==0&&got.censusRows==0,"unproven capture prevents any snapshot authority");
            });
        }
        struct MalformedCase {const char* name;std::function<void()> edit;proc::Reason reason;};
        const auto malformed=proc::Reason::CensusMalformed,selfUnproven=proc::Reason::CensusSelfUnproven;
        const std::vector<MalformedCase> malformedCases={
            {"missing-self",[&]{snapshotRows({censusRow(id),{0,0}});},selfUnproven},
            {"self-birth-mismatch",[]{changeRow(1,[](auto& row){++row.creation.QuadPart;});},selfUnproven},
            {"self-zero-birth",[]{changeRow(1,[](auto& row){row.creation.QuadPart=0;});},selfUnproven},
            {"abi-shifted-self-creation",[]{changeRow(1,[](auto& row){row.user=row.creation;row.creation.QuadPart=0;});},selfUnproven},
            {"target-zero-birth",[]{changeRow(0,[](auto& row){row.creation.QuadPart=0;});},malformed},
            {"target-negative-birth",[]{changeRow(0,[](auto& row){row.creation.QuadPart=-1;});},malformed},
            {"duplicate-self",[&]{const auto self=censusRow(actualIdentity(::GetCurrentProcess()));snapshotRows({censusRow(id),self,self});},malformed},
            {"duplicate-target",[&]{snapshotRows({censusRow(id),censusRow(actualIdentity(::GetCurrentProcess())),censusRow(id)});},malformed},
            {"conflicting-target",[&]{auto other=censusRow(id);++other.second;snapshotRows({censusRow(id),censusRow(actualIdentity(::GetCurrentProcess())),other});},malformed},
            {"duplicate-idle",[&]{snapshotRows({censusRow(id),censusRow(actualIdentity(::GetCurrentProcess())),{0,0},{0,0}});},malformed},
            {"duplicate-unrelated",[&]{const auto self=censusRow(actualIdentity(::GetCurrentProcess()));std::uintptr_t other=MAXDWORD;while(other==id.pid||other==self.first)--other;snapshotRows({censusRow(id),self,{other,0},{other,0}});},malformed},
            {"next-offset-too-small",[]{changeRow(0,[](auto& row){row.nextEntryOffset=static_cast<ULONG>(sizeof(row)-sizeof(void*));});},malformed},
            {"next-offset-misaligned",[]{changeRow(0,[](auto& row){row.nextEntryOffset=static_cast<ULONG>(sizeof(row)+1);});},malformed},
            {"next-offset-outside-buffer",[]{changeRow(1,[](auto& row){row.nextEntryOffset=MAXDWORD;});},malformed},
            {"next-offset-exact-end",[]{changeRow(1,[](auto& row){row.nextEntryOffset=static_cast<ULONG>(2*sizeof(row));});},malformed},
            {"truncated-after-target-and-self",[]{leafTest::snapshot.pop_back();},malformed},
            {"unterminated-chain",[]{changeRow(2,[](auto& row){row.nextEntryOffset=static_cast<ULONG>(sizeof(row));});},malformed},
            {"return-length-shorter-than-payload",[]{leafTest::querySteps={{0,static_cast<ULONG>(2*sizeof(leafTest::CensusPrefix)-1)}};},malformed},
            {"return-length-short-prefix",[]{leafTest::querySteps={{0,static_cast<ULONG>(sizeof(leafTest::CensusPrefix)-1)}};},malformed},
            {"return-length-zero",[]{leafTest::querySteps={{0,0}};},malformed},
            {"return-length-beyond-capacity",[]{leafTest::querySteps={{0,leafTest::initialCapacity+1}};},malformed}
        };
        for(const auto& test:malformedCases) {
            const auto name="controlled/census-"+std::string(test.name);
            run(name.c_str(),"controlled-malformed-census",[&]{fakeSnapshot(id);test.edit();
                const auto got=proc::probeRecordedOwner(id);expectCensus(got,proc::State::Unknown,test.reason,ERROR_INVALID_DATA);
                check(got.censusAttempts==1,"malformed successful query is rejected without retry");
            });
        }
        if constexpr(sizeof(void*)==8)run("controlled/census-pid-overflow","controlled-malformed-census",[&]{
            fakeSnapshot(id);changeRow(2,[](auto& row){row.uniqueProcessId=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(UINT64_C(0x100000000)));});
            expectCensus(proc::probeRecordedOwner(id),proc::State::Unknown,proc::Reason::CensusMalformed,ERROR_INVALID_DATA);
        });
        run("controlled/census-absence-truncated-tail","controlled-malformed-census",[&]{
            fakeSnapshot(id,false);leafTest::snapshot.pop_back();
            expectCensus(proc::probeRecordedOwner(id),proc::State::Unknown,proc::Reason::CensusMalformed,ERROR_INVALID_DATA);
        });
        run("controlled/census-negative-status-with-valid-payload","controlled-query-status",[&]{
            fakeSnapshot(id,false);leafTest::querySteps={{leafTest::accessDenied,static_cast<ULONG>(leafTest::snapshot.size())}};
            const auto got=proc::probeRecordedOwner(id);expectCensus(got,proc::State::Unknown,proc::Reason::CensusQueryFailed,ERROR_GEN_FAILURE);
            check(got.censusNtStatus==static_cast<DWORD>(leafTest::accessDenied),"raw failing NTSTATUS remains separate from Win32 error");
            check(got.censusRows==0,"failed status never authorizes apparently complete payload");
        });
        run("controlled/census-other-negative-status-no-retry","controlled-query-status",[&]{
            fakeSnapshot(id,false);const auto status=static_cast<LONG>(0xc0000001u);leafTest::querySteps={{status,leafTest::initialCapacity*2}};
            const auto got=proc::probeRecordedOwner(id);expectCensus(got,proc::State::Unknown,proc::Reason::CensusQueryFailed,ERROR_GEN_FAILURE);
            check(got.censusNtStatus==static_cast<DWORD>(status),"other NTSTATUS preserved without pretending to be Win32");
            check(got.censusAttempts==1,"only the two documented size statuses may retry");
        });
        for(const auto status:{leafTest::lengthMismatch,leafTest::bufferTooSmall}) {
            const auto name=status==leafTest::lengthMismatch?"controlled/census-length-mismatch-resize":"controlled/census-buffer-too-small-resize";
            run(name,"controlled-query-growth",[&]{fakeSnapshot(id,false);
                leafTest::querySteps={{status,leafTest::initialCapacity*3},{0,static_cast<ULONG>(leafTest::snapshot.size())}};
                const auto got=proc::probeRecordedOwner(id);expectCensus(got,proc::State::Dead,proc::Reason::CensusPidNotFound,ERROR_SUCCESS);
                check(got.censusNtStatus==0,"successful retry resets last raw NTSTATUS to success");
                check(got.censusAttempts==2&&leafTest::capacities[1]>=leafTest::initialCapacity*3,"retry grows to at least actual returned length");
                check(got.censusRows==2,"only final successful payload counted");
            });
        }
        run("controlled/census-growing-until-third-success","controlled-query-growth",[&]{
            fakeSnapshot(id);leafTest::querySteps={{leafTest::lengthMismatch,0},{leafTest::bufferTooSmall,0},{0,static_cast<ULONG>(leafTest::snapshot.size())}};
            const auto got=proc::probeRecordedOwner(id);expectCensus(got,proc::State::Alive,proc::Reason::CensusMatchingRunning,ERROR_SUCCESS);
            check(got.censusAttempts==3&&leafTest::capacities[1]>leafTest::capacities[0]&&leafTest::capacities[2]>leafTest::capacities[1],"zero returned sizes grow with bounded third attempt");
        });
        run("controlled/census-three-retries-exhausted","controlled-query-growth",[&]{
            fakeSnapshot(id,false);leafTest::querySteps={{leafTest::lengthMismatch,0},{leafTest::bufferTooSmall,0},{leafTest::lengthMismatch,0}};
            const auto got=proc::probeRecordedOwner(id);expectCensus(got,proc::State::Unknown,proc::Reason::CensusTooLarge,ERROR_INSUFFICIENT_BUFFER);
            check(got.censusNtStatus==static_cast<DWORD>(leafTest::lengthMismatch),"last exhausted size NTSTATUS retained");
            check(got.censusAttempts==3&&got.censusRows==0,"never parse partial rows or make a fourth query");
        });
        run("controlled/census-returned-size-above-budget","controlled-query-growth",[&]{
            fakeSnapshot(id,false);leafTest::querySteps={{leafTest::lengthMismatch,leafTest::maximumCapacity+1}};
            const auto got=proc::probeRecordedOwner(id);expectCensus(got,proc::State::Unknown,proc::Reason::CensusTooLarge,ERROR_INSUFFICIENT_BUFFER);
            check(got.censusAttempts==1&&got.censusBytes==leafTest::maximumCapacity+1&&got.censusRows==0,"oversized requirement rejected before allocation or parsing");
        });
        run("controlled/census-eight-mib-boundary","controlled-query-growth",[&]{
            fakeSnapshot(id,false);leafTest::querySteps={{leafTest::lengthMismatch,leafTest::maximumCapacity-4096},{0,static_cast<ULONG>(leafTest::snapshot.size())}};
            const auto got=proc::probeRecordedOwner(id);expectCensus(got,proc::State::Dead,proc::Reason::CensusPidNotFound,ERROR_SUCCESS);
            check(got.censusAttempts==2&&leafTest::capacities[1]==leafTest::maximumCapacity,"successful retry may use exactly 8MiB but never exceed it");
        });
        run("controlled/census-query-fails-after-resize","controlled-query-growth",[&]{
            fakeSnapshot(id,false);leafTest::querySteps={{leafTest::lengthMismatch,leafTest::initialCapacity*2},{leafTest::accessDenied,0}};
            const auto got=proc::probeRecordedOwner(id);expectCensus(got,proc::State::Unknown,proc::Reason::CensusQueryFailed,ERROR_GEN_FAILURE);
            check(got.censusNtStatus==static_cast<DWORD>(leafTest::accessDenied),"last query NTSTATUS replaces earlier size failure");
            check(got.censusAttempts==2&&got.censusRows==0,"later failure cannot consume an earlier partial payload");
        });
        run("real/census-open-denied-private-child","real-ntquery-controlled-direct-denial",[&]{
            leafTest::fault=leafTest::Fault::OpenDenied;leafTest::censusMode=leafTest::CensusMode::Real;
            const auto got=proc::probeRecordedOwner(id);
            expect(got,proc::State::Alive,proc::Reason::CensusMatchingRunning,ERROR_SUCCESS);
            check(got.directWin32Error==ERROR_ACCESS_DENIED,"real census retains synthetic direct denial");singleOpen(false);
            check(leafTest::modules==1&&leafTest::exports==1&&leafTest::realQuery!=nullptr,"real ntdll export resolved and delegated without synthetic bytes");
            check(got.censusAttempts==leafTest::queries&&got.censusAttempts>=1&&got.censusAttempts<=3,"actual NtQuerySystemInformation invocations counted");
            check(got.censusRows>=2&&got.censusBytes>=2*sizeof(leafTest::CensusPrefix)&&got.censusBytes<=leafTest::maximumCapacity,"actual complete OS snapshot parsed and self-validated");
            check(::WaitForSingleObject(child.process.value,0)==WAIT_TIMEOUT,"fixture child independently remains alive after real census");
            observations.push_back({{"basis","real-ntquery-system-process-information"},{"role","private-child"},{"pid",id.pid},{"birth",id.creationTime},
                {"directDenialSynthetic",true},{"state",proc::stateName(got.state)},{"reason",proc::reasonName(got.reason)},
                {"censusAttempts",got.censusAttempts},{"censusBytes",got.censusBytes},{"censusRows",got.censusRows},{"capacities",leafTest::capacities}});
        });
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
        run("real/probe-exited","real-win32",[&]{const auto got=proc::probeRecordedOwner(id);expect(got,proc::State::Dead,proc::Reason::MatchingExited,ERROR_SUCCESS);noCensus(got);singleOpen(true);check(leafTest::waits==1&&leafTest::lastTimeout==0,"terminal result comes from nonblocking real process wait");});
        run("diagnostics/names","pure-diagnostic",[]{check(std::string(proc::stateName(proc::State::Alive))=="alive","stable alive diagnostic");check(std::string(proc::stateName(proc::State::Dead))=="dead","stable dead diagnostic");check(std::string(proc::stateName(proc::State::Unknown))=="unknown","stable unknown diagnostic");check(std::string(proc::reasonName(proc::Reason::PidReused))=="pid-reused","distinct reuse reason");check(std::string(proc::reasonName(proc::Reason::OpenFailed))=="open-failed","distinct open reason");});
    }catch(const std::exception& error){failures.push_back({{"case","fixture/setup-or-cleanup"},{"exception",error.what()}});}
    J result={{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",assertions},{"cases",cases},{"failures",failures},{"observations",observations},{"realPrivateChildProcesses",true},{"syntheticCasesExplicitlyMarked",true},{"productRuntimeIntegrated",false},{"deviceOperations",0},{"productOrSteamRun",false}};
    std::ofstream out(resultPath,std::ios::binary);out<<result.dump(2)<<"\n";
    std::cout<<J({{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",assertions},{"failures",failures}}).dump()<<"\n";
    return failures.empty()?0:1;
}
