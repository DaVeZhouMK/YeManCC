#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "json.hpp"
namespace apiTest {
enum class Fault {None,StateAlloc,AttributeAlloc,CreateJob,HandleInfo,InheritableJob,SetLimits,SizeList,InitList,UpdateList,QueryJob,StopJob};
inline Fault fault=Fault::None;inline unsigned allocations=0,updates=0,queries=0,stops=0,jobCreates=0;
inline std::set<void*> allocationsHeld,lists;
inline std::set<HANDLE> jobs;
inline std::vector<std::string> releases;
inline DWORD configuredLimits=0;inline DWORD observedInheritFlags=0;
void reset(Fault f=Fault::None){fault=f;allocations=updates=queries=stops=jobCreates=0;configuredLimits=observedInheritFlags=0;releases.clear();}
LPVOID alloc(HANDLE heap,DWORD flags,SIZE_T size){++allocations;if((fault==Fault::StateAlloc&&allocations==1)||(fault==Fault::AttributeAlloc&&allocations==2)){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}const auto p=::HeapAlloc(heap,flags,size);if(p)allocationsHeld.insert(p);return p;}
BOOL free(HANDLE heap,DWORD flags,LPVOID p){const BOOL ok=::HeapFree(heap,flags,p);if(ok)allocationsHeld.erase(p);return ok;}
HANDLE create(LPSECURITY_ATTRIBUTES security,LPCWSTR name){++jobCreates;if(fault==Fault::CreateJob){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}const auto h=::CreateJobObjectW(security,name);if(h)jobs.insert(h);return h;}
BOOL close(HANDLE h){if(jobs.count(h))releases.push_back("close-job");const BOOL ok=::CloseHandle(h);if(ok)jobs.erase(h);return ok;}
BOOL handleInfo(HANDLE h,LPDWORD flags){if(fault==Fault::HandleInfo){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}const auto ok=::GetHandleInformation(h,flags);if(ok){if(fault==Fault::InheritableJob)*flags|=HANDLE_FLAG_INHERIT;observedInheritFlags=*flags;}return ok;}
BOOL limits(HANDLE h,JOBOBJECTINFOCLASS c,LPVOID value,DWORD size){configuredLimits=static_cast<JOBOBJECT_EXTENDED_LIMIT_INFORMATION*>(value)->BasicLimitInformation.LimitFlags;if(fault==Fault::SetLimits){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::SetInformationJobObject(h,c,value,size);}
BOOL init(LPPROC_THREAD_ATTRIBUTE_LIST list,DWORD count,DWORD flags,PSIZE_T size){if((!list&&fault==Fault::SizeList)||(list&&fault==Fault::InitList)){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}const auto ok=::InitializeProcThreadAttributeList(list,count,flags,size);if(ok&&list)lists.insert(list);return ok;}
void remove(LPPROC_THREAD_ATTRIBUTE_LIST list){releases.push_back("delete-attributes");::DeleteProcThreadAttributeList(list);lists.erase(list);}
BOOL update(LPPROC_THREAD_ATTRIBUTE_LIST list,DWORD flags,DWORD_PTR attr,PVOID value,SIZE_T size,PVOID old,PSIZE_T returned){++updates;if(fault==Fault::UpdateList){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}return ::UpdateProcThreadAttribute(list,flags,attr,value,size,old,returned);}
BOOL query(HANDLE h,JOBOBJECTINFOCLASS c,LPVOID value,DWORD size,LPDWORD returned){++queries;if(c!=JobObjectBasicAccountingInformation)throw std::logic_error("idle must use basic accounting only");if(fault==Fault::QueryJob){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::QueryInformationJobObject(h,c,value,size,returned);}
BOOL stop(HANDLE h,UINT code){++stops;if(fault==Fault::StopJob){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::TerminateJobObject(h,code);}
HANDLE onlyJob(){if(jobs.size()!=1)throw std::runtime_error("exactly one owned guard job required in local test process");return *jobs.begin();}
}
#define HeapAlloc apiTest::alloc
#define HeapFree apiTest::free
#define CreateJobObjectW apiTest::create
#define CloseHandle apiTest::close
#define GetHandleInformation apiTest::handleInfo
#define SetInformationJobObject apiTest::limits
#define InitializeProcThreadAttributeList apiTest::init
#define DeleteProcThreadAttributeList apiTest::remove
#define UpdateProcThreadAttribute apiTest::update
#define QueryInformationJobObject apiTest::query
#define TerminateJobObject apiTest::stop
#include "hidhide_cli_job_guard.h"
#undef HeapAlloc
#undef HeapFree
#undef CreateJobObjectW
#undef CloseHandle
#undef GetHandleInformation
#undef SetInformationJobObject
#undef InitializeProcThreadAttributeList
#undef DeleteProcThreadAttributeList
#undef UpdateProcThreadAttribute
#undef QueryInformationJobObject
#undef TerminateJobObject
namespace cli=ymcc::hidHideCli;using J=nlohmann::json;
static_assert(!std::is_copy_constructible_v<cli::JobGuard>&&!std::is_copy_assignable_v<cli::JobGuard>);
static_assert(std::is_nothrow_move_constructible_v<cli::JobGuard>&&std::is_nothrow_move_assignable_v<cli::JobGuard>);
namespace {
J cases=J::array(),failures=J::array(),observations=J::array();unsigned assertions=0,spawned=0;std::string active;
void check(bool value,const std::string& text){++assertions;if(!value)failures.push_back({{"case",active},{"assertion",text}});}
void need(bool value,const char* text){check(value,text);if(!value)throw std::runtime_error(text);}
void run(const std::string& name,const std::function<void()>& body){active=name;const auto before=assertions;const auto failed=failures.size();try{body();}catch(const std::exception& e){check(false,std::string("fixture-exception:")+e.what());}check(apiTest::jobs.empty()&&apiTest::lists.empty()&&apiTest::allocationsHeld.empty(),"guard releases every owned job/list/allocation after case");apiTest::reset();cases.push_back({{"name",name},{"assertions",assertions-before},{"failures",failures.size()-failed}});}
struct Handle {
 HANDLE h=nullptr;explicit Handle(HANDLE value=nullptr):h(value){}~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;Handle(Handle&& v)noexcept:h(std::exchange(v.h,nullptr)){};Handle&operator=(Handle&& v)noexcept{if(this!=&v){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=std::exchange(v.h,nullptr);}return *this;}bool valid()const{return h&&h!=INVALID_HANDLE_VALUE;}
};
std::wstring exe(){std::wstring s(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,s.data(),DWORD(s.size()));if(!n||n==s.size())throw std::runtime_error("private executable path unavailable");s.resize(n);return s;}
std::wstring quote(const std::wstring& value){if(value.find(L'"')!=std::wstring::npos||(!value.empty()&&value.back()==L'\\'))throw std::runtime_error("unsafe controlled argument");return L"\""+value+L"\"";}
std::wstring command(const std::vector<std::wstring>& args){auto s=quote(exe());for(const auto& arg:args)s+=L" "+quote(arg);return s;}
std::string text(const std::wstring& p){std::ifstream f(std::filesystem::path(p),std::ios::binary);if(!f)throw std::runtime_error("private report missing");return std::string(std::istreambuf_iterator<char>(f),{});}
void write(const std::wstring& p,const std::string& s){std::ofstream f(std::filesystem::path(p),std::ios::binary);f.write(s.data(),std::streamsize(s.size()));if(!f)throw std::runtime_error("private report write failed");}
void report(const std::wstring& p,const J& r){write(p,r.dump(2)+"\n");}
bool exists(const std::wstring& p){return GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES;}
std::string birth(HANDLE p){FILETIME a{},b{},c{},d{};if(!GetProcessTimes(p,&a,&b,&c,&d))throw std::runtime_error("exact owned process birth unavailable");return std::to_string((std::uint64_t(a.dwHighDateTime)<<32)|a.dwLowDateTime);}
std::wstring unique(const wchar_t* suffix){return L"Local\\YMCC_CliJob118_"+std::to_wstring(GetCurrentProcessId())+L"_"+suffix;}
STARTUPINFOW ordinary(){STARTUPINFOW s{};s.cb=sizeof(s);return s;}
struct Events {
 std::wstring prefix;Handle ready,writeNow,wrote,finish,parentReady,parentExit;
 explicit Events(const std::wstring& key):prefix(key),ready(make(L"_ready")),writeNow(make(L"_write")),wrote(make(L"_wrote")),finish(make(L"_finish")),parentReady(make(L"_parentReady")),parentExit(make(L"_parentExit")){}
 HANDLE make(const wchar_t* suffix){const auto h=CreateEventW(nullptr,TRUE,FALSE,(prefix+suffix).c_str());if(!h)throw std::runtime_error("private event create failed");return h;}
};
struct Output {
 Handle input,output;std::wstring path;
 Output(const std::wstring& dir):path(dir+L"\\stdout.txt"){
  SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
  input=Handle(CreateFileW((dir+L"\\stdin.txt").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
  output=Handle(CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));if(!input.valid()||!output.valid())throw std::runtime_error("private inherited standard handles unavailable");
 }
 STARTUPINFOW startup(){auto s=ordinary();s.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;s.wShowWindow=SW_HIDE;s.hStdInput=input.h;s.hStdOutput=s.hStdError=output.h;return s;}
};
struct Process {
 Handle process,thread;DWORD pid=0;Process()=default;Process(const Process&)=delete;Process&operator=(const Process&)=delete;Process(Process&&)=default;
 bool launch(const std::vector<std::wstring>& args,STARTUPINFOW* si,DWORD flags,BOOL inherit){auto cmd=command(args);PROCESS_INFORMATION pi{};const BOOL ok=CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,inherit,flags,nullptr,nullptr,si,&pi);if(ok){process=Handle(pi.hProcess);thread=Handle(pi.hThread);pid=pi.dwProcessId;++spawned;}return ok!=FALSE;}
 DWORD wait(DWORD budget=6000){if(!process.valid()||WaitForSingleObject(process.h,budget)!=WAIT_OBJECT_0)throw std::runtime_error("owned fixture process did not finish in test budget");DWORD code=0;if(!GetExitCodeProcess(process.h,&code))throw std::runtime_error("owned process exit unavailable");return code;}
 void resume(){if(ResumeThread(thread.h)==DWORD(-1))throw std::runtime_error("resume failed");thread=Handle();}
};
std::wstring makeDir(const std::wstring& root,const wchar_t* name){auto p=root+L"\\"+name;if(!CreateDirectoryW(p.c_str(),nullptr))throw std::runtime_error("fresh private case directory required");return p;}
bool boundedIdle(const cli::JobGuard& g){const auto limit=GetTickCount64()+4000;do{if(g.idle())return true;Sleep(10);}while(GetTickCount64()<limit);return false;}
void stdoutToken(const char* value){DWORD done=0;const DWORD n=DWORD(std::char_traits<char>::length(value));if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),value,n,&done,nullptr)||done!=n)throw std::runtime_error("inherited stdout write failed");}
int worker(const std::wstring& dir,const std::wstring& prefix){
 Handle ready(OpenEventW(EVENT_MODIFY_STATE,FALSE,(prefix+L"_ready").c_str())),writeNow(OpenEventW(SYNCHRONIZE,FALSE,(prefix+L"_write").c_str())),wrote(OpenEventW(EVENT_MODIFY_STATE,FALSE,(prefix+L"_wrote").c_str())),finish(OpenEventW(SYNCHRONIZE,FALSE,(prefix+L"_finish").c_str()));if(!ready.valid()||!writeNow.valid()||!wrote.valid()||!finish.valid())return 51;
 stdoutToken("guard118-worker-ready\n");if(!SetEvent(ready.h))return 52;const HANDLE waits[]={writeNow.h,finish.h};const auto event=WaitForMultipleObjects(2,waits,FALSE,15000);if(event==WAIT_OBJECT_0){write(dir+L"\\child-write.txt","private-child-write\n");stdoutToken("guard118-worker-write\n");if(!SetEvent(wrote.h))return 53;if(WaitForSingleObject(finish.h,15000)!=WAIT_OBJECT_0)return 54;return 0;}return event==WAIT_OBJECT_0+1?0:55;
}
int forkRoot(const std::wstring& dir,const std::wstring& prefix){auto si=ordinary();si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);si.hStdError=GetStdHandle(STD_ERROR_HANDLE);Process leaf;if(!leaf.launch({L"--worker",dir,prefix},&si,CREATE_NO_WINDOW,TRUE))return 61;report(dir+L"\\descendant.json",{{"pid",leaf.pid},{"birth",birth(leaf.process.h)}});return 0;}
int crashParent(const std::wstring& dir,const std::wstring& prefix){
 Handle ready(OpenEventW(EVENT_MODIFY_STATE,FALSE,(prefix+L"_parentReady").c_str())),exitNow(OpenEventW(SYNCHRONIZE,FALSE,(prefix+L"_parentExit").c_str()));if(!ready.valid()||!exitNow.valid())return 70;
 Output out(dir);auto si=out.startup();cli::JobGuard original;const bool prepared=original.prepare(si,prefix+L"_ownedJob");if(!prepared){report(dir+L"\\parent.json",{{"prepared",false},{"error",original.error()},{"unsupportedNoFallback",true}});SetEvent(ready.h);return 71;}
 const auto* startupAddress=original.startup();const bool jobInheritable=(apiTest::observedInheritFlags&HANDLE_FLAG_INHERIT)!=0;apiTest::reset();cli::JobGuard pending(std::move(original));const bool movedAddress=pending.startup()==startupAddress;Process child;
 const bool created=child.launch({L"--worker",dir,prefix},pending.startup(),CREATE_SUSPENDED|CREATE_NO_WINDOW|pending.flags(),TRUE);
 if(!created){report(dir+L"\\parent.json",{{"prepared",true},{"created",false},{"error",GetLastError()}});SetEvent(ready.h);return 72;}
 BOOL inside=FALSE;const bool queried=IsProcessInJob(child.process.h,apiTest::onlyJob(),&inside)!=FALSE;
 const auto childBirth=birth(child.process.h);child.resume();report(dir+L"\\parent.json",{{"prepared",true},{"created",true},{"childPid",child.pid},{"childBirth",childBirth},{"atomicMembershipQueryBeforeResume",queried},{"inOwnedJobBeforeResume",inside!=FALSE},{"movePreservedStartupAddress",movedAddress},{"moveAllocations",apiTest::allocations},{"ownedJobHandleInheritable",jobInheritable},{"observerReceivesJobHandle",false},{"startupStdoutPreserved",pending.startup()->hStdOutput==si.hStdOutput}});
 if(!SetEvent(ready.h))return 73;if(WaitForSingleObject(exitNow.h,15000)!=WAIT_OBJECT_0)return 74;
 ExitProcess(67); // Real native death: no JobGuard/Process/Output destructors.
}
}
int wmain(int argc,wchar_t** argv){
 if(argc==4){const std::wstring mode=argv[1];try{if(mode==L"--worker")return worker(argv[2],argv[3]);if(mode==L"--fork-root")return forkRoot(argv[2],argv[3]);if(mode==L"--crash-parent")return crashParent(argv[2],argv[3]);}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 90;}}
 if(argc!=3)return 2;const std::wstring root=argv[1],resultFile=argv[2];
 run("prepare/ordinary-startup-and-owned-zero-process-job",[]{cli::JobGuard g;auto si=ordinary();si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;check(!g.ready()&&!g.startup()&&!g.flags()&&!g.idle()&&!g.requestStop(),"unprepared guard supplies no startup, stop or idle proof");need(g.prepare(si),"OS supports atomic JOB_LIST prepare without fallback");check(g.ready()&&g.startup()&&g.startup()->cb==sizeof(STARTUPINFOEXW),"real extended startup is ready");check(g.startup()->dwFlags==si.dwFlags&&g.startup()->wShowWindow==si.wShowWindow,"ordinary startup fields retained");check(g.flags()==EXTENDED_STARTUPINFO_PRESENT,"only extended startup flag supplied");check(apiTest::configuredLimits==JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,"only KILL_ON_JOB_CLOSE limit configured, no ActiveProcessLimit");check((apiTest::observedInheritFlags&HANDLE_FLAG_INHERIT)==0,"owned job handle is noninheritable");check(g.idle(),"new owned job active process count is zero");check(!g.prepare(si)&&g.ready()&&g.error()==ERROR_BUSY,"reprepare cannot reset possibly active ownership");});
 run("prepare/fresh-name-and-collision-refusal",[]{const auto name=unique(L"collision");Handle external(CreateJobObjectW(nullptr,name.c_str()));need(external.valid(),"private collision job exists");JOBOBJECT_EXTENDED_LIMIT_INFORMATION before{},after{};need(QueryInformationJobObject(external.h,JobObjectExtendedLimitInformation,&before,sizeof(before),nullptr)!=FALSE,"private collision limits observable");cli::JobGuard g;check(!g.prepare(ordinary(),name)&&!g.ready()&&g.error()==ERROR_ALREADY_EXISTS,"existing name is refused rather than adopted");need(QueryInformationJobObject(external.h,JobObjectExtendedLimitInformation,&after,sizeof(after),nullptr)!=FALSE,"collision job remains queryable");check(before.BasicLimitInformation.LimitFlags==after.BasicLimitInformation.LimitFlags,"refusal leaves existing job limits untouched");check(!g.requestStop()&&!g.idle(),"refused job cannot be stopped or treated idle through guard");});
 run("prepare/invalid-input-no-create",[]{auto si=ordinary();si.cb=sizeof(STARTUPINFOEXW);cli::JobGuard g;check(!g.prepare(si)&&g.error()==ERROR_INVALID_PARAMETER,"extended caller startup is not silently merged");si=ordinary();const std::wstring bad(L"private\0alias",13);check(!g.prepare(si,bad),"embedded NUL job name rejected");check(apiTest::jobCreates==0,"invalid inputs never reach job creation");});
 for(const auto& [name,fault]:std::vector<std::pair<std::string,apiTest::Fault>>{{"state-alloc",apiTest::Fault::StateAlloc},{"attributes-alloc",apiTest::Fault::AttributeAlloc},{"create-job",apiTest::Fault::CreateJob},{"handle-info",apiTest::Fault::HandleInfo},{"inheritable-job",apiTest::Fault::InheritableJob},{"set-limits",apiTest::Fault::SetLimits},{"size-attributes",apiTest::Fault::SizeList},{"init-attributes",apiTest::Fault::InitList},{"update-job-list",apiTest::Fault::UpdateList}})run("prepare-fault/"+name,[&]{apiTest::reset(fault);const auto before=spawned;cli::JobGuard g;check(!g.prepare(ordinary()),"failed kernel/allocation leaf refuses prepare");check(!g.ready()&&!g.startup()&&!g.flags()&&g.error()!=ERROR_SUCCESS,"failure never exposes unowned startup");check(apiTest::jobs.empty()&&apiTest::lists.empty()&&apiTest::allocationsHeld.empty(),"partial prepare releases all owned resources");check(spawned==before,"failed preparation does not create or fallback to child");});
 run("idle/query-failure-is-not-idle",[]{cli::JobGuard g;need(g.prepare(ordinary()),"guard ready for query failure control");apiTest::reset(apiTest::Fault::QueryJob);check(!g.idle()&&apiTest::queries==1,"one failed basic accounting query is unknown, no retry");apiTest::reset(apiTest::Fault::StopJob);check(!g.requestStop(88)&&g.ready()&&apiTest::stops==1,"failed stop request retains ownership and readiness");apiTest::reset();check(g.idle(),"separate explicit successful query observes truly empty job");});
 run("move/no-allocation-stable-attributes-and-reset-order",[]{cli::JobGuard a;need(a.prepare(ordinary()),"source guard prepared");const auto* address=a.startup();apiTest::reset(apiTest::Fault::StateAlloc);cli::JobGuard b(std::move(a));check(!a.ready()&&b.startup()==address,"move constructor transfers exact stable startup block");cli::JobGuard c;c=std::move(b);check(!b.ready()&&c.startup()==address,"move assignment transfers without rebinding list value pointers");check(apiTest::allocations==0,"moves never allocate");c=std::move(c);check(c.ready()&&c.startup()==address,"self move keeps same ownership");c.reset();check(apiTest::releases==std::vector<std::string>{"delete-attributes","close-job"},"reset deletes attribute list before closing owned job");check(!c.ready()&&!c.idle()&&!c.requestStop(),"reset releases ready and idle authority");c.reset();check(apiTest::releases.size()==2,"reset idempotent without double close");});
 run("child/parent-alive-atomic-membership-stdout-natural-exit",[&]{const auto dir=makeDir(root,L"alive");Events events(unique(L"alive"));Output output(dir);auto si=output.startup();cli::JobGuard source;need(source.prepare(si),"anonymous guard prepared for live parent");const auto* pointer=source.startup();apiTest::reset();cli::JobGuard guard(std::move(source));check(apiTest::allocations==0&&guard.startup()==pointer,"real launch follows allocation-free pending-style move");Process child;need(child.launch({L"--worker",dir,events.prefix},guard.startup(),CREATE_SUSPENDED|CREATE_NO_WINDOW|guard.flags(),TRUE),"real child created suspended with atomic JOB_LIST");BOOL inside=FALSE;need(IsProcessInJob(child.process.h,apiTest::onlyJob(),&inside)!=FALSE,"query exact owned membership before first instruction");check(inside!=FALSE,"child belongs to owned job at CreateProcess return before ResumeThread");child.resume();need(WaitForSingleObject(events.ready.h,5000)==WAIT_OBJECT_0,"live parent child executed inherited stdout");check(!guard.idle(),"active child makes job non-idle");need(SetEvent(events.writeNow.h)!=FALSE,"live parent permits private child write");need(WaitForSingleObject(events.wrote.h,5000)==WAIT_OBJECT_0,"child can write while parent is alive");check(exists(dir+L"\\child-write.txt"),"actual private write occurred under healthy parent");need(SetEvent(events.finish.h)!=FALSE,"natural child finish signaled");check(child.wait()==0,"child exits naturally, not by job stop");check(boundedIdle(guard),"confirmed root exit plus job accounting establishes idle");check(text(output.path).find("guard118-worker-ready")!=std::string::npos&&text(output.path).find("guard118-worker-write")!=std::string::npos,"ordinary inherited stdout preserved by extended startup");observations.push_back({{"case",active},{"atomicOwnedMembership",inside!=FALSE},{"stdoutInherited",true},{"naturalExit",true},{"noCleanupJob",true}});});
 for(const bool stopRequest:{false,true})run(stopRequest?"descendant/request-stop-is-not-root-only-proof":"descendant/root-exit-is-not-idle",[&]{const auto dir=makeDir(root,stopRequest?L"desc-stop":L"desc-normal");Events events(unique(stopRequest?L"descstop":L"descnormal"));Output output(dir);cli::JobGuard guard;need(guard.prepare(output.startup()),"descendant owner prepared");Process rootChild;need(rootChild.launch({L"--fork-root",dir,events.prefix},guard.startup(),CREATE_SUSPENDED|CREATE_NO_WINDOW|guard.flags(),TRUE),"guarded root launches only private descendant");rootChild.resume();check(rootChild.wait()==0,"root exits naturally while private descendant remains alive");need(WaitForSingleObject(events.ready.h,5000)==WAIT_OBJECT_0,"descendant reports live and ready");const auto identity=J::parse(text(dir+L"\\descendant.json"));Handle child(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,identity.at("pid").get<DWORD>()));need(child.valid()&&birth(child.h)==identity.at("birth").get<std::string>(),"exact descendant PID and birth observed without job duplication");BOOL inside=FALSE;need(IsProcessInJob(child.h,apiTest::onlyJob(),&inside)!=FALSE,"descendant query uses existing local owned job handle");check(inside!=FALSE,"descendant inherits actual owned job membership");check(!guard.idle(),"root exit is not subtree idle while descendant executes");if(stopRequest){check(guard.requestStop(79),"owned job accepts one termination request");check(guard.ready(),"accepted request keeps guard ownership and ready state");const bool ended=WaitForSingleObject(child.h,3000)==WAIT_OBJECT_0;check(ended,"owned descendant exit separately confirmed after stop request");check(boundedIdle(guard),"accounting query, not request acceptance, confirms subtree idle");if(!ended){SetEvent(events.finish.h);WaitForSingleObject(child.h,5000);}}else{SetEvent(events.writeNow.h);need(WaitForSingleObject(events.wrote.h,5000)==WAIT_OBJECT_0,"descendant writes before permitted natural finish");SetEvent(events.finish.h);need(WaitForSingleObject(child.h,5000)==WAIT_OBJECT_0,"natural descendant exit confirmed");check(boundedIdle(guard),"job becomes idle only after descendant leaves");}observations.push_back({{"case",active},{"rootExitedBeforeLeaf",true},{"descendantInsideOwnedJob",inside!=FALSE},{"requestStopUsed",stopRequest},{"noCleanupJob",true}});});
 run("crash/ExitProcess-kills-child-before-late-write",[&]{const auto dir=makeDir(root,L"parent-crash");Events events(unique(L"parentcrash"));Process parent;auto si=ordinary();need(parent.launch({L"--crash-parent",dir,events.prefix},&si,CREATE_NO_WINDOW,FALSE),"private native-parent fixture launched");need(WaitForSingleObject(events.parentReady.h,7000)==WAIT_OBJECT_0,"parent publishes prepare and child identity receipt");const auto receipt=J::parse(text(dir+L"\\parent.json"));need(receipt.value("prepared",false),"OS JOB_LIST support required; unsupported never downgraded");need(receipt.value("created",false),"parent created actual extended-startup child");check(receipt.value("inOwnedJobBeforeResume",false),"crash-parent child atomically assigned before ResumeThread");check(receipt.value("movePreservedStartupAddress",false)&&receipt.value("moveAllocations",99)==0,"parent moved guard without allocation or stale attribute pointers");check(!receipt.value("ownedJobHandleInheritable",true)&&!receipt.value("observerReceivesJobHandle",true),"observer and child never inherit or duplicate job ownership");Handle child(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,receipt.at("childPid").get<DWORD>()));need(child.valid()&&birth(child.h)==receipt.at("childBirth").get<std::string>(),"observer holds exact child PID/birth query+synchronize handle only");need(WaitForSingleObject(events.ready.h,5000)==WAIT_OBJECT_0,"child ran before parent death; no late-write request sent yet");check(!exists(dir+L"\\child-write.txt"),"no write requested before crash");need(SetEvent(events.parentExit.h)!=FALSE,"request only owned parent to ExitProcess");check(parent.wait()==67,"actual parent ExitProcess bypassed all C++ destructors");const bool stopped=WaitForSingleObject(child.h,3000)==WAIT_OBJECT_0;check(stopped,"child exit independently confirmed after parent death without cleanup job");SetEvent(events.writeNow.h);const bool wrote=WaitForSingleObject(events.wrote.h,700)==WAIT_OBJECT_0;check(!wrote&&!exists(dir+L"\\child-write.txt"),"child cannot execute any requested private write after parent died");SetEvent(events.finish.h);need(WaitForSingleObject(child.h,5000)==WAIT_OBJECT_0,"any deliberately uncontained mutant child finishes naturally for cleanup");observations.push_back({{"case",active},{"parentExitCode",67},{"childExitConfirmedBeforeLateWriteRequest",stopped},{"lateWriteExecuted",wrote},{"observerHoldsJobHandle",false},{"observerProcessRights","SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION"},{"cleanupJobUsed",false},{"parentReceipt",receipt}});});
 J result={{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",assertions},{"cases",cases},{"failures",failures},{"processObservations",observations},{"ownFixtureProcessesSpawnedByObserver",spawned},{"realPrivateProcesses",true},{"cleanupJobUsed",false},{"deviceApiCalls",0},{"realCliOrHostLaunched",false},{"productMainRun",false}};report(resultFile,result);std::cout<<J({{"pass",failures.empty()},{"cases",cases.size()},{"assertions",assertions},{"failures",failures}}).dump()<<"\n";return failures.empty()?0:1;
}
