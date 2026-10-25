#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cwchar>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "../native/msi_crash_recovery_journal.h"
using json = nlohmann::json;
namespace journal = ymcc::msi::recovery;
namespace io {
enum class Fault { None, Write, ShortWrite, Flush, Rename, MoveOnly, Read, ShortRead, ChangedInfo };
struct Trace {
    Fault fault = Fault::None;
    unsigned writes=0, flushes=0, moves=0, replaces=0, leaves=0, reads=0, infos=0;
    DWORD createFlags=0, moveFlags=0, replaceFlags=0, lastMoveError=0, lastReplaceError=0;
};
static Trace trace;
static HANDLE pauseEvent = nullptr;
static HANDLE create(LPCWSTR p,DWORD a,DWORD s,LPSECURITY_ATTRIBUTES sec,DWORD d,DWORD f,HANDLE t) {
    if(a==GENERIC_WRITE)trace.createFlags=f;
    return ::CreateFileW(p,a,s,sec,d,f,t);
}
static BOOL write(HANDLE f,LPCVOID p,DWORD n,LPDWORD done,LPOVERLAPPED o) {
    ++trace.writes;
    if(trace.fault==Fault::Write){++trace.leaves;*done=0;SetLastError(ERROR_WRITE_FAULT);return FALSE;}
    if(trace.fault==Fault::ShortWrite){++trace.leaves;return ::WriteFile(f,p,n/2,done,o);}
    return ::WriteFile(f,p,n,done,o);
}
static BOOL flush(HANDLE f) {
    ++trace.flushes;
    if(trace.fault==Fault::Flush){++trace.leaves;SetLastError(ERROR_WRITE_FAULT);return FALSE;}
    return ::FlushFileBuffers(f);
}
static BOOL move(LPCWSTR from,LPCWSTR to,DWORD flags) {
    ++trace.moves;trace.moveFlags=flags;
    if(pauseEvent){SetEvent(pauseEvent);::Sleep(15000);ExitProcess(74);}
    if(trace.fault==Fault::Rename||trace.fault==Fault::MoveOnly){++trace.leaves;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    const BOOL ok=::MoveFileExW(from,to,flags);if(!ok)trace.lastMoveError=GetLastError();return ok;
}
static BOOL replace(LPCWSTR to,LPCWSTR from,LPCWSTR backup,DWORD flags,LPVOID a,LPVOID b) {
    ++trace.replaces;trace.replaceFlags=flags;
    if(trace.fault==Fault::Rename){++trace.leaves;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    const BOOL ok=::ReplaceFileW(to,from,backup,flags,a,b);if(!ok)trace.lastReplaceError=GetLastError();return ok;
}
static BOOL read(HANDLE f,LPVOID p,DWORD n,LPDWORD done,LPOVERLAPPED o) {
    ++trace.reads;
    if(trace.fault==Fault::Read){++trace.leaves;SetLastError(ERROR_READ_FAULT);return FALSE;}
    if(trace.fault==Fault::ShortRead){++trace.leaves;return ::ReadFile(f,p,n-1,done,o);}
    return ::ReadFile(f,p,n,done,o);
}
static BOOL info(HANDLE f,LPBY_HANDLE_FILE_INFORMATION p) {
    ++trace.infos;const BOOL ok=::GetFileInformationByHandle(f,p);
    if(ok&&trace.fault==Fault::ChangedInfo&&trace.infos==2){++trace.leaves;p->ftLastWriteTime.dwLowDateTime^=1;}
    return ok;
}
}
// Only OS leaves are routed. The included original contains all business branches.
#define CreateFileW io::create
#define WriteFile io::write
#define FlushFileBuffers io::flush
#define MoveFileExW io::move
#define ReplaceFileW io::replace
#define ReadFile io::read
#define GetFileInformationByHandle io::info
#include "actual-record-functions.inc"
#undef CreateFileW
#undef WriteFile
#undef FlushFileBuffers
#undef MoveFileExW
#undef ReplaceFileW
#undef ReadFile
#undef GetFileInformationByHandle
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE value):h(value){}
    ~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static journal::Record record(bool next) {
    journal::Record r;r.boardKey="MSI|private-io-fixture|Claw|handheld";
    r.ownerPid=next?202u:101u;r.ownerCreationTime=next?"134000000000000002":"134000000000000001";
    r.transactionId=next?"private-next":"private-old";r.modeReturnPending=true;
    r.modeAttemptOwner=std::to_string(r.ownerPid)+":"+r.ownerCreationTime;
    r.rootInstance="USB\\VID_0DB0&PID_1901\\PRIVATE_IO";
    r.locationPaths={"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4)"};
    r.hiddenPaths={r.rootInstance,"HID\\VID_0DB0&PID_1901&MI_00&COL01\\PRIVATE_IO"};
    return r;
}
static void seed(const std::wstring& p,const std::string& bytes) {
    Handle f(CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    require(f.h!=INVALID_HANDLE_VALUE,"seed create");DWORD done=0;
    require(WriteFile(f.h,bytes.data(),static_cast<DWORD>(bytes.size()),&done,nullptr)&&done==bytes.size(),"seed write");
    require(FlushFileBuffers(f.h)!=FALSE,"seed flush");
}
static bool matches(const std::wstring& p,const journal::Record& expected) {
    std::string text;const DWORD error=msiCrashReadOwnedRecord(p,text);const auto d=journal::decodeRecord(text);
    return error==ERROR_SUCCESS&&text==journal::encodeRecord(expected)&&d.state==journal::DecodeState::Valid&&
        journal::encodeRecord(d.record)==text&&d.record.boardKey==expected.boardKey&&d.record.ownerPid==expected.ownerPid&&
        d.record.ownerCreationTime==expected.ownerCreationTime&&d.record.transactionId==expected.transactionId&&
        d.record.modeAttemptOwner==expected.modeAttemptOwner&&d.record.modeReturnPending==expected.modeReturnPending&&
        d.record.hiddenPaths==expected.hiddenPaths&&d.record.rootInstance==expected.rootInstance&&d.record.locationPaths==expected.locationPaths;
}
static std::vector<std::filesystem::path> temps(const std::wstring& p) {
    std::vector<std::filesystem::path> out;const std::filesystem::path file(p);
    for(const auto& e:std::filesystem::directory_iterator(file.parent_path()))
        if(e.path().filename().wstring().find(file.filename().wstring()+L".ymcc.")==0)out.push_back(e.path());
    return out;
}
struct Checks {
    json cases=json::array(),failures=json::array();unsigned count=0,start=0;std::string name,boundary;
    void begin(const char* n,const char* b="private-real-Win32"){name=n;boundary=b;start=count;io::trace={};}
    void expect(bool ok,const char* assertion){++count;if(!ok)failures.push_back({{"case",name},{"assertion",assertion}});}
    void end(){bool passed=true;for(const auto& f:failures)if(f.at("case")==name)passed=false;
        cases.push_back({{"name",name},{"assertions",count-start},{"passed",passed},{"boundary",boundary},
            {"explicitLeafCalls",io::trace.leaves},{"writes",io::trace.writes},{"flushes",io::trace.flushes},
            {"moves",io::trace.moves},{"replaces",io::trace.replaces},{"reads",io::trace.reads},{"lastMoveError",io::trace.lastMoveError},
            {"lastReplaceError",io::trace.lastReplaceError},{"createFlags",io::trace.createFlags},
            {"moveFlags",io::trace.moveFlags},{"replaceFlags",io::trace.replaceFlags}});}
};
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc==4&&std::wstring(argv[1])==L"--child") {
            Handle event(OpenEventW(EVENT_MODIFY_STATE,FALSE,argv[3]));require(event.h!=nullptr,"child event");
            io::pauseEvent=event.h;return sgWriteFileAtomic(argv[2],journal::encodeRecord(record(true)))?0:75;
        }
        require(argc==1,"unexpected arguments");Checks c;const auto old=record(false),next=record(true);
        const auto bytes=journal::encodeRecord(next);const auto root=std::filesystem::current_path()/L"files";
        require(std::filesystem::create_directory(root),"fresh private files directory required");
        const auto p=(root/L"record.json").wstring();
        c.begin("journal/new-durable");
        c.expect(journal::validRecord(old)&&journal::validRecord(next)&&!bytes.empty(),"valid journal encoding");
        c.expect(sgWriteFileAtomic(p,bytes),"durable new write succeeds");
        c.expect(matches(p,next),"new bytes decode with exact content and ownership");
        c.expect((io::trace.createFlags&FILE_FLAG_WRITE_THROUGH)!=0&&io::trace.flushes==1,"durable create and explicit flush");
        c.expect(io::trace.moveFlags==(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"durable atomic publish flags");
        c.expect(temps(p).empty(),"new write leaves no temp");c.end();
        seed(p,journal::encodeRecord(old));c.begin("journal/replace-old");
        c.expect(sgWriteFileAtomic(p,bytes),"replacement succeeds");
        c.expect(matches(p,next),"replacement preserves all new content and ownership");
        c.expect(io::trace.flushes==1&&io::trace.moves==1,"replacement flush precedes single publish");
        c.expect(temps(p).empty(),"replacement consumes temp");c.end();
        seed(p,journal::encodeRecord(old));c.begin("journal/real-sharing-and-release");
        {
            Handle held(CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
            c.expect(held.h!=INVALID_HANDLE_VALUE,"real sharing handle opened");
            c.expect(!sgWriteFileAtomic(p,bytes),"real sharing conflict rejects replacement");
            c.expect(io::trace.moves==6&&io::trace.replaces==1&&(io::trace.lastMoveError==ERROR_SHARING_VIOLATION||io::trace.lastMoveError==ERROR_ACCESS_DENIED)&&io::trace.lastReplaceError==ERROR_SHARING_VIOLATION,"real sharing conflict reaches bounded rename and fallback failures");
            c.expect(matches(p,old),"real sharing conflict retains exact old ownership");
            c.expect(temps(p).empty()&&io::trace.leaves==0,"real sharing failure cleans own temp without injection");
        }
        c.expect(sgWriteFileAtomic(p,bytes)&&matches(p,next),"released sharing handle permits durable retry");c.end();
        const auto failure=[&](const char* name,io::Fault fault) {
            seed(p,journal::encodeRecord(old));c.begin(name,"explicit-error-leaf");io::trace.fault=fault;
            c.expect(!sgWriteFileAtomic(p,bytes),"failed IO returns false");
            c.expect(io::trace.leaves>0,"declared leaf was reached");
            c.expect(matches(p,old),"failed IO retains exact old content and ownership");
            c.expect(io::trace.moves==(fault==io::Fault::Rename?6u:0u)&&io::trace.replaces==(fault==io::Fault::Rename?1u:0u),"failed IO never publishes a new record");
            c.expect(temps(p).empty(),"failed IO cleans only its own temp");
            c.expect(io::trace.flushes==((fault==io::Fault::Write||fault==io::Fault::ShortWrite)?0u:1u),"failed IO preserves flush ordering");c.end();
        };
        failure("leaf/write-failure",io::Fault::Write);failure("leaf/short-write",io::Fault::ShortWrite);
        failure("leaf/flush-failure",io::Fault::Flush);failure("leaf/rename-failure",io::Fault::Rename);
        seed(p,journal::encodeRecord(old));c.begin("leaf/replace-fallback","explicit-error-leaf");io::trace.fault=io::Fault::MoveOnly;
        c.expect(sgWriteFileAtomic(p,bytes),"real ReplaceFile fallback succeeds");
        c.expect(io::trace.leaves==6&&io::trace.moves==6&&io::trace.replaces==1&&io::trace.replaceFlags==REPLACEFILE_WRITE_THROUGH,"fallback preserves durable flags and bounded retries");
        c.expect(matches(p,next)&&temps(p).empty(),"real fallback publishes complete new journal");c.end();
        const auto readCase=[&](const char* name,const std::string& data,DWORD expected,bool invalid) {
            seed(p,data);c.begin(name);std::string text="stale";
            c.expect(msiCrashReadOwnedRecord(p,text)==expected,"bounded reader returns expected real result");
            c.expect(expected==ERROR_SUCCESS?text==data:text.empty(),"reader publishes only a complete bounded snapshot");
            c.expect(!invalid||journal::decodeRecord(text).state==journal::DecodeState::Invalid,"invalid bytes never decode authority");c.end();
        };
        readCase("reader/exact-64KiB",std::string(65536,'x'),ERROR_SUCCESS,true);
        readCase("reader/oversize",std::string(65537,'x'),ERROR_INVALID_DATA,true);
        readCase("reader/empty",{},ERROR_SUCCESS,true);
        c.begin("reader/missing-and-sharing");std::string text="stale";
        c.expect(msiCrashReadOwnedRecord((root/L"missing.json").wstring(),text)==ERROR_FILE_NOT_FOUND&&text.empty(),"missing real file clears output");
        {
            Handle held(CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr));require(held.h!=INVALID_HANDLE_VALUE,"reader sharing handle");text="stale";
            c.expect(msiCrashReadOwnedRecord(p,text)==ERROR_SHARING_VIOLATION&&text.empty(),"real reader sharing conflict clears output");
        }c.end();
        for(const auto fault:{io::Fault::Read,io::Fault::ShortRead,io::Fault::ChangedInfo}) {
            seed(p,bytes);const char* name=fault==io::Fault::Read?"leaf/read-failure":fault==io::Fault::ShortRead?"leaf/short-read":"leaf/changing-info";
            c.begin(name,"explicit-error-leaf");io::trace.fault=fault;text="stale";
            c.expect(msiCrashReadOwnedRecord(p,text)==static_cast<DWORD>(fault==io::Fault::Read?ERROR_READ_FAULT:ERROR_RETRY),"uncertain reader returns typed failure");
            c.expect(text.empty()&&io::trace.leaves==1,"uncertain reader publishes no bytes");c.end();
        }
        seed(p,journal::encodeRecord(old));c.begin("writer/owned-child-before-rename");
        const auto eventName=L"Local\\YMCC-MSI-private-IO-"+std::to_wstring(GetCurrentProcessId());
        Handle ready(CreateEventW(nullptr,TRUE,FALSE,eventName.c_str()));require(ready.h&&GetLastError()!=ERROR_ALREADY_EXISTS,"fresh child ready event");
        std::wstring exe(32768,L'\0');const DWORD length=GetModuleFileNameW(nullptr,exe.data(),static_cast<DWORD>(exe.size()));require(length>0&&length<exe.size(),"own executable");exe.resize(length);
        std::wstring command=L"\""+exe+L"\" --child \""+p+L"\" \""+eventName+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        require(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"create own writer child");
        Handle child(process.hProcess),thread(process.hThread);
        const DWORD readyWait=WaitForSingleObject(ready.h,10000);DWORD exit=0;GetExitCodeProcess(child.h,&exit);
        const bool atBoundary=readyWait==WAIT_OBJECT_0&&exit==STILL_ACTIVE;
        c.expect(atBoundary,"owned child acknowledges flushed closed temp before rename");
        c.expect(matches(p,old),"old journal complete while own writer is interrupted");
        const auto orphan=temps(p);c.expect(orphan.size()==1&&matches(orphan.front().wstring(),next),"child temp already contains complete flushed new journal");
        if(exit==STILL_ACTIVE)require(TerminateProcess(child.h,73)!=FALSE,"terminate only own child handle");
        require(WaitForSingleObject(child.h,5000)==WAIT_OBJECT_0,"own child bounded exit");GetExitCodeProcess(child.h,&exit);
        c.expect(atBoundary&&exit==73,"only acknowledged owned child interrupted with expected exit");
        c.expect(matches(p,old),"interrupted writer leaves complete old ownership");
        c.expect(sgWriteFileAtomic(p,bytes)&&matches(p,next),"fresh writer succeeds after owned child interruption");
        c.expect(temps(p)==orphan,"fresh writer preserves other invocation orphan temp");c.end();
        const json report={{"schemaVersion",1},{"passed",c.failures.empty()},{"tests",c.cases.size()},{"assertions",c.count},
            {"cases",c.cases},{"failures",c.failures},{"productRun",false},{"hardwareAccess",false},
            {"filesystem","private-real-Win32"},{"ownedChildPid",process.dwProcessId},{"childExitCode",exit},
            {"interruptionBoundary","after-flush-and-close-before-first-rename"},{"rebootOrPowerLossTested",false}};
        std::cout<<report.dump(2)<<'\n';return c.failures.empty()?0:1;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
