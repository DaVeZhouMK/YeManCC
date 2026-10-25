#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <set>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
using J=nlohmann::json;
namespace privateWriter {
void afterCreate(); void beforeResume(); void beforeExitStatus(); void onClock();
}
namespace fake {
struct Config {
 bool missing=false,pipeFail=false,inheritFail=false,createFail=false;
 bool immediateExit=true,terminateAccepted=true,terminateProvesExit=true,waitError=false,exitQueryFail=false,peekFail=false,readFail=false,readZero=false,readThrows=false,tailAndExitDuringWait=false;
 DWORD exitCode=0;ULONGLONG attributesCost=0,pipeCost=0,createCost=0,readCost=0;
 std::string bytes="ok";
 bool blockMayRun=false,expireAfterCommit=false,changePowerAfterCommit=false,blockSettle=false,probeUnknown=false;
};
struct Child {HANDLE process=nullptr,thread=nullptr,read=nullptr;bool alive=true;DWORD code=STILL_ACTIVE;std::string bytes;std::size_t offset=0;};
inline Config config;
inline ULONGLONG tick=100;
inline std::uintptr_t nextHandle=100;
inline std::map<HANDLE,std::string>handles;
inline std::vector<Child>children;
inline std::vector<J>calls;
inline unsigned creates=0,closeLive=0,doubleClose=0,terminates=0,opCount=0,resumes=0;
inline std::uint64_t power=41;
inline bool suspendedLaunch=false;
inline DWORD error=0;
inline HANDLE readPipe=nullptr;
void note(const char*op,HANDLE h=nullptr,DWORD value=0){if(++opCount>4000)throw std::runtime_error("fixture bounded operation guard");calls.push_back({{"op",op},{"handle",std::uintptr_t(h)},{"value",value},{"tick",tick}});}
HANDLE handle(const char*kind){const auto h=reinterpret_cast<HANDLE>(nextHandle++);handles[h]=kind;return h;}
Child*child(HANDLE h){for(auto&c:children)if(c.process==h)return &c;return nullptr;}
Child*threadChild(HANDLE h){for(auto&c:children)if(c.thread==h)return &c;return nullptr;}
Child*pipeChild(HANDLE h){for(auto&c:children)if(c.read==h)return &c;return nullptr;}
DWORD attributes(LPCWSTR p){note("attributes");if(std::wstring(p)!=L"C:\\Program Files\\Nefarius Software Solutions\\HidHide\\x64\\HidHideCLI.exe")throw std::runtime_error("wrong CLI path");tick+=config.attributesCost;return config.missing?INVALID_FILE_ATTRIBUTES:FILE_ATTRIBUTE_NORMAL;}
BOOL pipe(PHANDLE r,PHANDLE w,LPSECURITY_ATTRIBUTES sa,DWORD bytes){note("create-pipe");tick+=config.pipeCost;if(!sa||!sa->bInheritHandle||bytes!=0)throw std::runtime_error("pipe contract");if(config.pipeFail){error=ERROR_ACCESS_DENIED;return FALSE;}*r=handle("read");*w=handle("write");readPipe=*r;return TRUE;}
BOOL inherit(HANDLE h,DWORD mask,DWORD flags){note("inherit",h);if(mask!=HANDLE_FLAG_INHERIT||flags!=0)throw std::runtime_error("inherit contract");if(config.inheritFail){error=ERROR_ACCESS_DENIED;return FALSE;}return TRUE;}
BOOL create(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL inheritHandles,DWORD flags,LPVOID,LPCWSTR,LPSTARTUPINFOW si,LPPROCESS_INFORMATION pi){
 note("create-process");++creates;tick+=config.createCost;if(!inheritHandles||!(flags&CREATE_NO_WINDOW)||!si||si->hStdOutput!=si->hStdError)throw std::runtime_error("create contract");if(config.createFail){error=ERROR_ACCESS_DENIED;return FALSE;}
 pi->hProcess=handle("process");pi->hThread=handle("thread");pi->dwProcessId=DWORD(creates+1000);
#ifdef CLI_WRITER_FENCE_ENABLED
 suspendedLaunch=(flags&CREATE_SUSPENDED)&&(flags&EXTENDED_STARTUPINFO_PRESENT)&&si->cb==sizeof(STARTUPINFOEXW);
 if(!suspendedLaunch)throw std::runtime_error("atomic extended suspended launch contract");
 children.push_back({pi->hProcess,pi->hThread,readPipe,true,STILL_ACTIVE,config.bytes,0});privateWriter::afterCreate();
#else
 children.push_back({pi->hProcess,pi->hThread,readPipe,!config.immediateExit,config.immediateExit?config.exitCode:STILL_ACTIVE,config.bytes,0});
#endif
 return TRUE;
}
BOOL close(HANDLE h){note("close",h);auto it=handles.find(h);if(it==handles.end()){++doubleClose;error=ERROR_INVALID_HANDLE;return FALSE;}if(auto*c=child(h);c&&c->alive)++closeLive;handles.erase(it);return TRUE;}
BOOL peek(HANDLE h,LPVOID,DWORD,LPDWORD,LPDWORD available,LPDWORD){note("peek",h);if(config.peekFail){error=ERROR_ACCESS_DENIED;return FALSE;}auto*c=pipeChild(h);if(!c){error=ERROR_INVALID_HANDLE;return FALSE;}*available=DWORD(c->bytes.size()-c->offset);calls.back()["available"]=*available;if(!*available&&!c->alive){error=ERROR_BROKEN_PIPE;return FALSE;}return TRUE;}
BOOL read(HANDLE h,LPVOID buffer,DWORD want,LPDWORD got,LPOVERLAPPED){note("read",h,want);tick+=config.readCost;if(config.readThrows)throw std::bad_alloc();if(config.readFail){error=ERROR_READ_FAULT;return FALSE;}auto*c=pipeChild(h);if(!c){error=ERROR_INVALID_HANDLE;return FALSE;}if(config.readZero){*got=0;return TRUE;}*got=DWORD((std::min)(std::size_t(want),c->bytes.size()-c->offset));std::copy_n(c->bytes.data()+c->offset,*got,static_cast<char*>(buffer));c->offset+=*got;return TRUE;}
DWORD wait(HANDLE h,DWORD timeout){note("wait",h,timeout);auto*c=child(h);if(!c||!handles.count(h)){error=ERROR_INVALID_HANDLE;return WAIT_FAILED;}if(config.waitError){error=ERROR_ACCESS_DENIED;return WAIT_FAILED;}if(c->alive&&config.tailAndExitDuringWait){config.tailAndExitDuringWait=false;c->bytes=std::string(9000,'T');c->alive=false;c->code=0;note("external-tail-and-exit",h,DWORD(c->bytes.size()));}if(c->alive){tick+=timeout;return WAIT_TIMEOUT;}note("wait-signaled",h);return WAIT_OBJECT_0;}
ULONGLONG now(){privateWriter::onClock();note("clock");return tick;}
DWORD resume(HANDLE thread){note("resume",thread);++resumes;auto*c=threadChild(thread);if(!c){error=ERROR_INVALID_HANDLE;return DWORD(-1);}privateWriter::beforeResume();if(config.immediateExit){c->alive=false;c->code=config.exitCode;}return 1;}
BOOL terminate(HANDLE h,UINT code){note("terminate",h,code);++terminates;auto*c=child(h);if(!config.terminateAccepted){error=ERROR_ACCESS_DENIED;return FALSE;}if(c&&config.terminateProvesExit){c->alive=false;c->code=code;}return TRUE;}
BOOL exitCode(HANDLE h,LPDWORD code){privateWriter::beforeExitStatus();note("exit-code",h);auto*c=child(h);if(config.exitQueryFail||!c){error=ERROR_ACCESS_DENIED;return FALSE;}*code=c->alive?STILL_ACTIVE:c->code;return TRUE;}
void sleep(DWORD ms){note("sleep",nullptr,ms);tick+=ms;}
DWORD lastError(){return error;}
void externalExit(HANDLE h,DWORD code=0){auto*c=child(h);if(!c)throw std::runtime_error("missing modeled child");note("external-child-exit",h,code);c->alive=false;c->code=code;}
}

#ifdef CLI_WRITER_FENCE_ENABLED
// These headers compile before the fake CLI macros. Session/store/JobGuard are real OS code.
#include "native/hidhide_cli_writer_store.h"
#include "native/virtual_target_return_process.h"
#include "native/hidhide_cli_job_guard.h"
#include "native/virtual_start_protection.h"
static std::wstring U2W(const std::string& value) {std::wstring result;for(unsigned char c:value)result.push_back(wchar_t(c));return result;}
static std::string W2U(const std::wstring& value) {std::string result;for(wchar_t c:value){if(c>127)result.push_back('?');else result.push_back(char(c));}return result;}
namespace privateWriter {
using Writer=ymcc::hidhideCliWriter::Record;
inline std::wstring root;
inline HANDLE fileBlocker=INVALID_HANDLE_VALUE;
inline J audit=J::array();
inline std::set<std::string> ownedNames;
inline bool clockHookUsed=false;
static std::wstring recordPath(){if(root.empty())throw std::runtime_error("explicit private record root required");return root+L"\\writer.json";}
static std::string machineBinding(){return "fixture120-private-machine";}
static void log(const char* op,const J& detail=J::object()){audit.push_back({{"op",op},{"detail",detail},{"fakeTick",fake::tick}});}
static ymcc::hidhideCliWriter::LoadResult disk(){return ymcc::hidhideCliWriter::load(recordPath(),machineBinding());}
static J diskJson(){const auto got=disk();J result={{"state",int(got.state)},{"reason",got.reason}};if(got.record){std::string encoded;if(!ymcc::hidhideCliWriter::encode(*got.record,encoded))throw std::runtime_error("encode actual record");result["record"]=J::parse(encoded);}return result;}
static std::optional<ymcc::targetReturn::ProcessIdentity> capture(HANDLE process){
 if(process==GetCurrentProcess()){auto real=ymcc::targetReturn::process::capture(process);log("real-parent-capture",{{"success",bool(real)},{"pid",real?real->pid:0},{"creationTime",real?real->creationTime:std::string{}}});return real;}
 auto*c=fake::child(process);if(!c){log("unrecognized-process-capture-denied");return std::nullopt;}
 const auto index=std::size_t(c-fake::children.data());ymcc::targetReturn::ProcessIdentity controlled{std::uint32_t(1001+index),std::to_string(9000000+index)};
 log("modeled-child-capture",{{"pid",controlled.pid},{"creationTime",controlled.creationTime},{"realChild",false}});return controlled;
}
static ymcc::targetReturn::process::Probe probe(const ymcc::targetReturn::ProcessIdentity& identity){
 namespace p=ymcc::targetReturn::process;
 for(std::size_t n=0;n<fake::children.size();++n){auto& c=fake::children[n];if(identity.pid==1001+n&&identity.creationTime==std::to_string(9000000+n)){log("modeled-exact-child-probe",{{"pid",identity.pid},{"alive",c.alive},{"forcedUnknown",fake::config.probeUnknown}});return {fake::config.probeUnknown?p::State::Unknown:c.alive?p::State::Alive:p::State::Dead,p::Reason::MatchingRunning,0};}}
 const auto current=p::capture(GetCurrentProcess());if(current&&ymcc::targetReturn::detail::sameProcess(*current,identity))return{p::State::Alive,p::Reason::MatchingRunning,0};
 log("arbitrary-pid-probe-denied",{{"pid",identity.pid}});return {p::State::Unknown,p::Reason::OpenFailed,ERROR_ACCESS_DENIED};
}
// Runtime enum is defined later in the actual bridge: Absent=0,Idle=1,Active=2,Unknown=3.
static int probeJob(const std::string& name){
 if(!ownedNames.count(name)){log("unrecognized-job-query-denied",{{"name",name}});return 3;}
 HANDLE h=OpenJobObjectW(JOB_OBJECT_QUERY,FALSE,U2W(name).c_str());if(!h){const DWORD error=GetLastError();log("real-private-job-query-open",{{"name",name},{"error",error}});return error==ERROR_FILE_NOT_FOUND?0:3;}
 JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};const BOOL ok=QueryInformationJobObject(h,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr);const DWORD error=ok?0:GetLastError();CloseHandle(h);
 log("real-empty-private-job-query",{{"name",name},{"success",ok!=FALSE},{"active",info.ActiveProcesses},{"error",error},{"fakeChildContained",false}});return !ok?3:info.ActiveProcesses?2:1;
}
static void blockRecord(){if(fileBlocker!=INVALID_HANDLE_VALUE)throw std::runtime_error("record already blocked");fileBlocker=CreateFileW(recordPath().c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(fileBlocker==INVALID_HANDLE_VALUE)throw std::runtime_error("real no-share-delete record lock");log("real-record-no-share-delete-lock");}
static void unblock(){if(fileBlocker!=INVALID_HANDLE_VALUE){CloseHandle(fileBlocker);fileBlocker=INVALID_HANDLE_VALUE;log("record-lock-released");}}
void afterCreate(){const auto row=diskJson();const bool reserved=row.contains("record")&&row["record"]["active"].is_object()&&row["record"]["active"]["stage"]=="reserved";log("fake-create-observed-durable-reservation",{{"present",reserved},{"disk",row}});if(reserved)ownedNames.insert(row["record"]["active"]["jobName"].get<std::string>());if(fake::config.blockMayRun)blockRecord();}
void beforeResume(){const auto row=diskJson();const bool published=row.contains("record")&&row["record"]["active"].is_object()&&row["record"]["active"]["stage"]=="may-run"&&!row["record"]["active"]["child"].is_null();log("fake-resume-observed-durable-MayRun",{{"present",published},{"disk",row}});}
void beforeExitStatus(){if(fake::config.blockSettle&&fileBlocker==INVALID_HANDLE_VALUE)blockRecord();}
void onClock(){if(clockHookUsed||(!fake::config.expireAfterCommit&&!fake::config.changePowerAfterCommit))return;const auto row=diskJson();if(row.contains("record")&&row["record"]["active"].is_object()&&row["record"]["active"]["stage"]=="may-run"){clockHookUsed=true;if(fake::config.expireAfterCommit)fake::tick+=1000;if(fake::config.changePowerAfterCommit)++fake::power;log("after-real-MayRun-commit-clock-or-power-change",{{"disk",row},{"power",fake::power}});}}
}
static void appendNativeLifecycleLog(const char* channel,const J& row){privateWriter::log(channel,row);}
#include "native/hidhide_cli_writer_runtime.h"
#include "execution-globals.inc"
static std::uint64_t currentPowerGeneration(){return fake::power;}
static std::string inputHostNewId(){static std::uint64_t serial=0;return "fixture120-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial);}
#else
namespace privateWriter {void afterCreate(){}void beforeResume(){}void beforeExitStatus(){}void onClock(){}}
#endif

namespace ymcc::processEfficiency {inline BOOL createProcess(LPCWSTR a,LPWSTR b,LPSECURITY_ATTRIBUTES c,LPSECURITY_ATTRIBUTES d,BOOL e,DWORD f,LPVOID g,LPCWSTR h,LPSTARTUPINFOW i,LPPROCESS_INFORMATION j){return fake::create(a,b,c,d,e,f,g,h,i,j);}}
#define ResumeThread fake::resume
#define GetFileAttributesW fake::attributes
#define CreatePipe fake::pipe
#define SetHandleInformation fake::inherit
#define CloseHandle fake::close
#define PeekNamedPipe fake::peek
#define ReadFile fake::read
#define WaitForSingleObject fake::wait
#define GetTickCount64 fake::now
#define TerminateProcess fake::terminate
#define GetExitCodeProcess fake::exitCode
#define Sleep fake::sleep
#define GetLastError fake::lastError
#include "cli-path.inc"
#include "mutex.inc"
#include "globals.inc"
#include "consumer.inc"
#undef ResumeThread
#undef GetFileAttributesW
#undef CreatePipe
#undef SetHandleInformation
#undef CloseHandle
#undef PeekNamedPipe
#undef ReadFile
#undef WaitForSingleObject
#undef GetTickCount64
#undef TerminateProcess
#undef GetExitCodeProcess
#undef Sleep
#undef GetLastError
namespace {
J failures=J::array(),observations=J::object();unsigned checks=0;
void check(bool okay,const char*label){++checks;if(!okay)failures.push_back(label);}
unsigned callCount(const char*op){return unsigned(std::count_if(fake::calls.begin(),fake::calls.end(),[&](const J&r){return r["op"]==op;}));}
void ordinary(const std::string&name){
 std::string output="sentinel";DWORD code=99;
 if(name=="expired-deadline"){const bool ok=hidHideRun(L"--dev-list",&output,&code,100);check(!ok,"expired deadline rejects");check(fake::creates==0,"expired deadline never creates process");check(fake::handles.empty(),"expired deadline leaves no handles");}
 else if(name=="deadline-during-setup"){fake::config.attributesCost=20;const bool ok=hidHideRun(L"--dev-list",&output,&code,110);check(!ok,"setup consumes original deadline");check(fake::creates==0,"deadline rechecked before process creation");check(fake::handles.empty(),"setup expiry releases pipe handles");}
 else if(name=="busy"){std::mutex signal;std::condition_variable cv;bool held=false,done=false;std::thread holder([&]{std::unique_lock<std::mutex>owner(g_hidHideMx);{std::lock_guard<std::mutex>s(signal);held=true;}cv.notify_one();std::unique_lock<std::mutex>s(signal);cv.wait(s,[&]{return done;});});{std::unique_lock<std::mutex>s(signal);cv.wait(s,[&]{return held;});}const bool ok=hidHideRun(L"--dev-hide X",&output,&code,200);{std::lock_guard<std::mutex>s(signal);done=true;}cv.notify_one();holder.join();check(!ok,"busy mutex rejects immediately");check(fake::creates==0,"busy does not dispatch a process");check(fake::tick==100,"busy never sleeps or extends budget");}
 else if(name=="unknown-child-retained"||name=="termination-accepted-still-live"||name=="wait-failed-retained"||name=="exception-live-retained"){
  fake::config.immediateExit=false;fake::config.terminateAccepted=name=="termination-accepted-still-live";fake::config.terminateProvesExit=false;fake::config.waitError=name=="wait-failed-retained";fake::config.readThrows=name=="exception-live-retained";
  const auto start=fake::tick;bool first=false,threw=false;try{first=hidHideRun(L"--dev-hide X",&output,&code,start+1);}catch(...){threw=true;}observations["firstThrew"]=threw;check(!threw,"post-create exception is contained without losing ownership");check(!first,"unknown/timed-out child is not success");check(fake::creates==1,"first request dispatched one exact child");const auto old=fake::children[0].process;const auto read=fake::children[0].read;
  check(fake::closeLive==0,"unproven live child handle is retained");check(fake::handles.count(old)==1,"exact old process handle remains owned");observations["firstRetainedExactHandle"]=fake::handles.count(old)==1;observations["firstEndedTick"]=fake::tick;observations["firstLiveHandleCloses"]=fake::closeLive;check(fake::tick<=start+1,"cleanup does not renew original absolute budget");
  fake::config.waitError=false;fake::config.readThrows=false;fake::config.immediateExit=true;fake::config.bytes="next";const auto callBegin=fake::calls.size();const bool second=hidHideRun(L"--dev-unhide X",&output,&code,fake::tick+100);
  observations["secondReturn"]=second;observations["createsAfterSecond"]=fake::creates;check(!second,"new request denied while exact old child still live");check(fake::creates==1,"unproven child blocks overlapping second dispatch");check(std::none_of(fake::calls.begin()+std::ptrdiff_t(callBegin),fake::calls.end(),[](const J&r){return r["op"]=="peek"||r["op"]=="read"||r["op"]=="sleep"||r["op"]=="terminate";}),"existing call checks old child without new I/O/wait/kill loop");
  check(std::any_of(fake::calls.begin()+std::ptrdiff_t(callBegin),fake::calls.end(),[&](const J&r){return r["op"]=="wait"&&r["handle"]==std::uintptr_t(old)&&r["value"]==0;}),"existing call rechecks exact retained process with Wait0");
  fake::externalExit(old);const bool third=hidHideRun(L"--dev-unhide X",&output,&code,fake::tick+100);check(third&&code==0&&output=="next","external OS terminal observation allows next ordinary command");check(fake::creates==2,"re-admission launches only one successor");check(fake::closeLive==0,"no live process closed during handover");check(!fake::handles.count(old)&&!fake::handles.count(read),"old retained process and pipe closed only after terminal proof");check(fake::handles.empty(),"terminal successor releases all owned handles");
 }
 else if(name=="tail-after-empty-peek"){
  fake::config.immediateExit=false;fake::config.bytes.clear();fake::config.tailAndExitDuringWait=true;
  const bool ok=hidHideRun(L"--dev-list",&output,&code,1000);
  observations["capturedBytes"]=output.size();observations["expectedBytes"]=9000;observations["return"]=ok;observations["exitCode"]=code;
  check(ok&&code==0,"late terminal tail still permits ordinary success");check(output==std::string(9000,'T'),"post-exit peek captures tail written after prior empty peek");check(fake::handles.empty(),"late terminal output releases handles");
 }
 else if(name=="tail-output"){fake::config.bytes=std::string(9000,'A');const bool ok=hidHideRun(L"--dev-list",&output,&code,1000);check(ok&&code==0,"ordinary terminal child successful");observations["capturedBytes"]=output.size();observations["expectedBytes"]=fake::config.bytes.size();observations["return"]=ok;observations["exitCode"]=code;check(output==fake::config.bytes,"all buffered tail captured after first terminal wait");check(fake::handles.empty(),"complete output closes owned handles");}
 else if(name=="output-read-failure"||name=="output-peek-failure"||name=="output-zero-read"){
  fake::config.readFail=name=="output-read-failure";fake::config.peekFail=name=="output-peek-failure";fake::config.readZero=name=="output-zero-read";
  const bool ok=hidHideRun(L"--dev-list",&output,&code,200);check(!ok,"incomplete output is not successful list result");check(code!=0,"incomplete output returns nonzero operational failure");check(fake::creates==1,"capture failure never silently retries command");}
 else if(name=="timeout-confirmed"){fake::config.immediateExit=false;const bool ok=hidHideRun(L"--dev-hide X",&output,&code,101);check(!ok&&code!=0,"timed-out operation never promoted to success");check(fake::tick<=101,"timeout cleanup retains original deadline");check(fake::terminates==1,"timeout asks once for own CLI termination");check(fake::handles.empty(),"proven terminated child closes handles");}
 else if(name=="exit-query-failure"){fake::config.exitQueryFail=true;const bool ok=hidHideRun(L"--dev-list",&output,&code,200);check(!ok&&code!=0,"missing exit status cannot become success");check(fake::handles.empty(),"proven terminal child closed on exit query failure");}
 else if(name=="output-budget"){fake::config.bytes=std::string(9000,'B');fake::config.readCost=20;const bool ok=hidHideRun(L"--dev-list",&output,&code,110);check(!ok&&code!=0,"capture crossing original deadline is not complete success");check(fake::creates==1,"no capture budget renewal or relaunch");}
 else if(name=="output-limit"){fake::config.bytes=std::string(1024*1024+1,'C');const bool ok=hidHideRun(L"--dev-list",&output,&code,1000);check(!ok&&code!=0,"bounded output overflow is explicit failure");check(output.size()<=1024*1024,"capture does not publish unbounded text");}
 else if(name=="shared-expired-default"){g_inputHostSwitchRecoveryStartDeadline=100;const bool ok=hidHideRun(L"--dev-list",&output,&code);check(!ok&&code!=0,"legacy caller inherits expired execution deadline");check(fake::creates==0,"expired shared context never creates CLI");check(g_inputHostSwitchRecoveryStartDeadline==100,"CLI never rewrites shared execution budget");check(fake::handles.empty(),"inherited expiry creates no owned handles");}
 else if(name=="shared-shorter-than-explicit"){g_inputHostSwitchRecoveryStartDeadline=105;fake::config.immediateExit=false;const bool ok=hidHideRun(L"--dev-hide X",&output,&code,1000);check(!ok&&code!=0,"active shared context timeout is not success");check(fake::tick<=105,"shorter inherited deadline bounds explicit longer caller");check(g_inputHostSwitchRecoveryStartDeadline==105,"inherited budget never renewed");}
 else if(name=="explicit-shorter-than-shared"){g_inputHostSwitchRecoveryStartDeadline=500;fake::config.immediateExit=false;const bool ok=hidHideRun(L"--dev-hide X",&output,&code,105);check(!ok&&code!=0,"explicit shorter timeout is not success");check(fake::tick<=105,"explicit shorter deadline wins over shared bound");check(g_inputHostSwitchRecoveryStartDeadline==500,"explicit CLI call does not mutate outer deadline");}
 else if(name=="default-arguments"){const bool ok=hidHideRun(L"--dev-list");check(ok,"default null output/code and fallback budget succeed");check(fake::handles.empty(),"default parameters release all handles");}
 else if(name=="empty-output"){fake::config.bytes.clear();const bool ok=hidHideRun(L"--cloak-off",&output,&code,200);check(ok&&code==0&&output.empty(),"terminal empty stdout is complete success");check(fake::handles.empty(),"empty output releases handles");}
 else if(name=="fallback-unknown-no-extra-wait"){fake::config.immediateExit=false;fake::config.terminateAccepted=false;const auto begin=fake::tick;const bool ok=hidHideRun(L"--dev-hide X",&output,&code);check(!ok&&code!=0,"fallback unknown is not success");check(fake::tick<=begin+4000,"unknown cleanup does not add a second fallback wait budget");check(fake::closeLive==0,"fallback still retains unproven process");observations["elapsed"]=fake::tick-begin;}
 else if(name=="success"||name=="error-exit"||name=="missing-cli"||name=="pipe-failure"||name=="create-failure"||name=="inherit-failure"){
  fake::config.exitCode=name=="error-exit"?42:0;fake::config.missing=name=="missing-cli";fake::config.pipeFail=name=="pipe-failure";fake::config.createFail=name=="create-failure";fake::config.inheritFail=name=="inherit-failure";
  const bool ok=hidHideRun(L"--dev-list",&output,&code,1000);check(ok==(name=="success"),"ordinary success/error result preserved");check(fake::handles.empty(),"ordinary path releases all created handles");if(name=="success")check(output=="ok"&&code==0,"ordinary output and exit status exact");if(name=="error-exit")check(code==42,"nonzero process exit preserved");if(name=="inherit-failure")check(fake::creates==0,"unsafe pipe inheritance cannot launch child");
 }else throw std::runtime_error("unknown fixture case");
 check(fake::doubleClose==0,"no double close");
}
}

#ifdef CLI_WRITER_FENCE_ENABLED
namespace {
J pendingSnapshot(){return {{"process",g_hidHidePendingChildProcess!=nullptr},{"pipe",g_hidHidePendingReadPipe!=nullptr},{"jobReady",g_hidHidePendingJob.ready()},{"writerReady",g_hidHidePendingWriter&&g_hidHidePendingWriter->ready()},{"attempt",g_hidHidePendingAttempt},{"processHandle",std::to_string(std::uintptr_t(g_hidHidePendingChildProcess))},{"pipeHandle",std::to_string(std::uintptr_t(g_hidHidePendingReadPipe))}};}
bool auditBoolean(const char* operation,const char* property){for(const auto& r:privateWriter::audit)if(r["op"]==operation&&r["detail"].contains(property)&&r["detail"][property]==true)return true;return false;}
void seed(bool child,bool alive){
 using namespace ymcc::hidhideCliWriter;Record r;r.machineBinding=privateWriter::machineBinding();
 if(child){fake::children.push_back({nullptr,nullptr,nullptr,alive,alive?STILL_ACTIVE:DWORD(0),{},0});const auto parent=ymcc::targetReturn::process::capture(GetCurrentProcess());if(!parent)throw std::runtime_error("actual seed parent capture");Attempt a;a.attemptId="fixture120-seeded-"+std::to_string(GetCurrentProcessId());a.parent=*parent;a.child=ProcessIdentity{1001,"9000000"};a.stage=Stage::MayRun;a.jobName=std::string(jobPrefix)+a.attemptId;privateWriter::ownedNames.insert(a.jobName);r.active=a;}
 if(!store(privateWriter::recordPath(),r).committed())throw std::runtime_error("actual private seed store");
}
DWORD separateOwnerProbe(){
 std::vector<wchar_t> image(32768);const DWORD length=GetModuleFileNameW(nullptr,image.data(),DWORD(image.size()));if(!length||length>=image.size())throw std::runtime_error("probe image");
 const auto quote=[](const std::wstring& v){if(v.find(L'"')!=v.npos)throw std::runtime_error("probe quote");return L"\""+v+L"\"";};
 std::wstring cmd=quote(std::wstring(image.data(),length))+L" --probe-writer-owner "+quote(privateWriter::root);std::vector<wchar_t> bytes(cmd.begin(),cmd.end());bytes.push_back(0);
 STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION pi{};if(!CreateProcessW(nullptr,bytes.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,privateWriter::root.c_str(),&startup,&pi))throw std::runtime_error("private session probe launch");
 const auto identity=ymcc::targetReturn::process::capture(pi.hProcess);CloseHandle(pi.hThread);const DWORD state=WaitForSingleObject(pi.hProcess,3000);if(state!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,90);WaitForSingleObject(pi.hProcess,1000);CloseHandle(pi.hProcess);throw std::runtime_error("private session probe budget");}
 DWORD code=0;const bool known=GetExitCodeProcess(pi.hProcess,&code)!=FALSE;CloseHandle(pi.hProcess);if(!known||!identity)throw std::runtime_error("private probe identity/status");privateWriter::log("actual-independent-session-probe",{{"pid",identity->pid},{"birth",identity->creationTime},{"exit",code},{"realProbeOnly",true}});return code;
}
void retained(){const auto pending=pendingSnapshot();observations["pending"]=pending;check(pending["process"]==true&&pending["pipe"]==true,"unknown keeps exact fake process and pipe");check(pending["jobReady"]==true&&pending["writerReady"]==true&&!pending["attempt"].get<std::string>().empty(),"unknown keeps real owned job and actual Session/attempt");auto other=ymcc::hidhideCliWriter::Session::open(privateWriter::recordPath(),privateWriter::machineBinding());check(!other.ready(),"retained actual Session excludes an independent second Session");check(separateOwnerProbe()==73,"retained actual file owner excludes a separate native process");}
void fenceCase(const std::string& name){
 std::string output="sentinel";DWORD code=99;
 if(name=="writer-open-fails-zero-create"){
  privateWriter::fileBlocker=CreateFileW((privateWriter::recordPath()+L".owner").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(privateWriter::fileBlocker==INVALID_HANDLE_VALUE)throw std::runtime_error("real owner exclusion setup");
  const bool ok=hidHideRun(L"--dev-hide X",&output,&code,1000);check(!ok,"locked actual owner denies launch");check(fake::creates==0&&fake::resumes==0,"writer open failure creates and resumes zero fake child");check(fake::handles.empty(),"owner failure does not allocate fake pipe/process handles");
 }else if(name=="writer-reserve-fails-zero-create"){
  seed(false,false);const auto before=privateWriter::diskJson();privateWriter::blockRecord();const bool ok=hidHideRun(L"--dev-hide X",&output,&code,1000);
  check(!ok,"actual reservation store failure rejects");check(fake::creates==0&&fake::resumes==0,"failed durable reservation dispatches zero child");check(privateWriter::diskJson()==before,"failed atomic reservation preserves original disk state");check(!g_hidHidePendingWriter,"childless unreserved scope releases actual Session");
 }else if(name=="writer-mayrun-fails-zero-resume"){
  fake::config.blockMayRun=true;fake::config.immediateExit=false;fake::config.terminateAccepted=false;
  const bool ok=hidHideRun(L"--dev-hide X",&output,&code,1000);const auto disk=privateWriter::diskJson();
  check(!ok,"actual MayRun store failure rejects");check(fake::creates==1&&fake::resumes==0,"failed MayRun commit never resumes suspended fake child");check(disk.contains("record")&&disk["record"]["active"]["stage"]=="reserved"&&disk["record"]["active"]["child"].is_null(),"failed MayRun leaves durable childless reservation");check(auditBoolean("fake-create-observed-durable-reservation","present"),"real reservation existed before modeled Create");retained();
 }else if(name=="writer-commit-expiry-zero-resume"||name=="writer-postcommit-power-stale"){
  fake::config.immediateExit=false;fake::config.terminateAccepted=false;
  const bool expiry=name=="writer-commit-expiry-zero-resume";fake::config.expireAfterCommit=expiry;fake::config.changePowerAfterCommit=!expiry;
  if(expiry)g_inputHostSwitchRecoveryStartDeadline=200;else {const auto admission=g_virtualStartProtection.admit("steamdeck",fake::tick);check(admission.accepted,"actual lifecycle admission accepted fixture input");g_inputHostSwitchRecoveryStartRequestId=admission.requestId;g_inputHostSwitchRecoveryStartPower=fake::power;}
  const bool ok=hidHideRun(L"--dev-hide X",&output,&code,5000);const auto disk=privateWriter::diskJson();
  check(!ok&&code==(expiry?WAIT_TIMEOUT:ERROR_CANCELLED),"post-commit original deadline/currentness rejects");check(fake::creates==1&&fake::resumes==0,"committed but stale command never resumes child");check(privateWriter::clockHookUsed,"clock or power changed only after actual MayRun commit");check(disk.contains("record")&&disk["record"]["active"]["stage"]=="may-run"&&!disk["record"]["active"]["child"].is_null(),"post-commit rejection preserves exact durable child responsibility");if(expiry)check(g_inputHostSwitchRecoveryStartDeadline==200,"commit does not renew inherited deadline");retained();
 }else if(name=="writer-unknown-session-retained"){
  fake::config.immediateExit=false;fake::config.terminateAccepted=false;fake::config.readThrows=true;
  const bool ok=hidHideRun(L"--dev-hide X",&output,&code,1000);check(!ok,"post-create output exception is contained");retained();const auto before=pendingSnapshot();const unsigned creates=fake::creates;fake::config.readThrows=false;
  check(!hidHideRun(L"--dev-unhide X",&output,&code,1000)&&code==ERROR_BUSY,"same native rejects while unknown child persists");check(fake::creates==creates&&pendingSnapshot()==before,"unknown retry preserves exact process/pipe/job/session/attempt");
  fake::externalExit(fake::children[0].process);check(hidHideCliReapPendingLocked(1000),"actual shared reap clears only after exact fake child exit and real empty job idle");check(!g_hidHidePendingWriter&&!g_hidHidePendingJob.ready()&&!g_hidHidePendingChildProcess&&!g_hidHidePendingReadPipe,"actual reap releases all pending ownership fields");check(privateWriter::diskJson()["record"]["active"].is_null(),"actual reap settles durable writer");check(fake::creates==creates,"reap never starts another CLI");
 }else if(name=="writer-normal-settle"){
  const auto ticket=g_virtualStartProtection.admit("steamdeck",fake::tick);g_inputHostSwitchRecoveryStartRequestId=ticket.requestId;g_inputHostSwitchRecoveryStartPower=fake::power;
  const bool ok=hidHideRun(L"--dev-list",&output,&code,1000);const auto disk=privateWriter::diskJson();check(ok&&code==0&&output=="ok","normal actual consumer completes");check(fake::creates==1&&fake::resumes==1,"normal path creates and resumes once");check(auditBoolean("fake-create-observed-durable-reservation","present"),"normal dispatch follows actual Reserved commit");check(auditBoolean("fake-resume-observed-durable-MayRun","present"),"normal Resume follows actual exact-child MayRun commit");check(disk.contains("record")&&disk["record"]["active"].is_null()&&disk["record"]["revision"]==4,"normal completion actually settles durable attempt");check(!g_hidHidePendingWriter&&!g_hidHidePendingJob.ready()&&fake::handles.empty(),"normal completion releases pending and fake handles");auto next=ymcc::hidhideCliWriter::Session::open(privateWriter::recordPath(),privateWriter::machineBinding());check(next.ready(),"normal completion releases actual file owner");
 }else if(name=="writer-settle-fails-retains-debt"){
  fake::config.blockSettle=true;const bool ok=hidHideRun(L"--dev-list",&output,&code,1000);check(!ok,"actual settle publication failure cannot report complete success");check(fake::creates==1&&fake::resumes==1,"settle failure never repeats dispatch");check(g_hidHidePendingWriter&&g_hidHidePendingWriter->ready(),"settle failure retains actual file lease after proved subtree exit");check(privateWriter::diskJson()["record"]["active"]["stage"]=="may-run","failed settle preserves durable MayRun");privateWriter::unblock();fake::config.blockSettle=false;check(hidHideCliReapPendingLocked(1000),"existing reap retries actual settle once after I/O unblocks");check(privateWriter::diskJson()["record"]["active"].is_null(),"same-record retry really clears debt");check(fake::creates==1,"settle retry starts no child");

 }else if(name=="writer-proven-exit-retains-only-debt"){
  fake::config.immediateExit=false;const bool first=hidHideRun(L"--dev-hide X",&output,&code,101);const auto disk=privateWriter::diskJson();
  check(!first&&code==WAIT_TIMEOUT,"proved exit still reports original operation timeout");check(fake::handles.empty()&&!g_hidHidePendingChildProcess&&!g_hidHidePendingReadPipe,"proved dead child and pipe close despite expired settlement budget");check(g_hidHidePendingWriter&&g_hidHidePendingWriter->ready(),"expired settlement budget retains actual file owner");check(disk.contains("record")&&disk["record"]["active"]["stage"]=="may-run","proved dead command responsibility stays durably recorded until settle");observations["beforeDebtReap"]={{"disk",disk},{"pending",pendingSnapshot()}};
  fake::config.immediateExit=true;check(hidHideRun(L"--dev-unhide X",&output,&code,fake::tick+100)&&fake::creates==2,"next original admission settles old debt before successor");check(privateWriter::diskJson()["record"]["active"].is_null(),"successor finishes with actual old and new debt settled");check(fake::closeLive==0,"debt-only cleanup closes no live fake process");
 }else if(name=="writer-stale-child-alive-blocks"||name=="writer-stale-child-unknown-blocks"||name=="writer-stale-child-dead-admits"){
  const bool dead=name=="writer-stale-child-dead-admits";seed(true,!dead);fake::config.probeUnknown=name=="writer-stale-child-unknown-blocks";const auto before=privateWriter::diskJson();
  const bool ok=hidHideRun(L"--dev-list",&output,&code,1000);
  if(dead){check(ok&&fake::creates==1&&fake::resumes==1,"exact controlled old-child death and absent private job permit new original entry");check(privateWriter::diskJson()["record"]["active"].is_null(),"old and successor actual records both settle");}
  else {check(!ok&&fake::creates==0&&fake::resumes==0,"old recorded child Alive or Unknown prevents new dispatch");check(privateWriter::diskJson()==before,"blocked historical writer record stays unchanged");}
 }else throw std::runtime_error("unknown writer case");
 observations["writerDisk"]=privateWriter::diskJson();observations["writerPending"]=pendingSnapshot();check(fake::doubleClose==0,"writer cases do not double close fake handles");
}
}
#endif
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return 2;
 std::string name;for(const wchar_t c:std::wstring(argv[1])){if(c>127)return 2;name.push_back(char(c));}
#ifdef CLI_WRITER_FENCE_ENABLED
 privateWriter::root=argv[2];if(!std::filesystem::path(privateWriter::root).is_absolute())return 2;
 if(name=="--probe-writer-owner"){auto session=ymcc::hidhideCliWriter::Session::open(privateWriter::recordPath(),privateWriter::machineBinding());return session.ready()?74:73;}
#endif
 try{
#ifdef CLI_WRITER_FENCE_ENABLED
  if(name.rfind("writer-",0)==0)fenceCase(name);else
#endif
  ordinary(name);
 }catch(const std::exception& e){failures.push_back(std::string("exception: ")+e.what());}
 J result={{"case",name},{"pass",failures.empty()},{"checks",checks},{"failures",failures},{"creates",fake::creates},{"resumes",fake::resumes},{"liveProcessCloses",fake::closeLive},{"openHandles",fake::handles.size()},{"tick",fake::tick},{"calls",fake::calls},{"observations",observations},{"deviceCalls",0},{"realCliRuns",0},{"modeledCliProcess",true}};
#ifdef CLI_WRITER_FENCE_ENABLED
 result["realPrivateWriterSession"]=true;result["realEmptyPrivateJobGuard"]=true;result["actualJobContainmentValidatedHere"]=false;result["writerAudit"]=privateWriter::audit;privateWriter::unblock();
#else
 result["modeledOsOnly"]=true;
#endif
 std::cout<<result.dump()<<"\n";return failures.empty()?0:1;
}
