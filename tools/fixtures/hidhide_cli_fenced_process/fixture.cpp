#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "json.hpp"
#include "process_efficiency.h"
#include "virtual_start_protection.h"
#include "hidhide_cli_job_guard.h"
#include "hidhide_cli_writer_store.h"
#include "virtual_target_return_process.h"
using J=nlohmann::json;
namespace writer=ymcc::hidhideCliWriter;
namespace process=ymcc::targetReturn::process;
namespace fspath=std::filesystem;
static std::wstring privateRoot;
static J runtimeNotes=J::array();
static void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Handle {
    HANDLE value=nullptr;
    Handle()=default;explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(valid())CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& h)noexcept:value(std::exchange(h.value,nullptr)){}
    Handle& operator=(Handle&& h)noexcept{if(this!=&h){if(valid())CloseHandle(value);value=std::exchange(h.value,nullptr);}return *this;}
    bool valid()const{return value&&value!=INVALID_HANDLE_VALUE;}
};
#include "actual_context.inc"
static std::wstring file(const std::wstring& leaf){return privateRoot+L"\\"+leaf;}
static std::wstring ownImage(){std::vector<wchar_t> b(32768);const auto n=GetModuleFileNameW(nullptr,b.data(),DWORD(b.size()));need(n&&n<b.size(),"own fixture image");return {b.data(),n};}
static std::wstring quote(const std::wstring& s){need(s.find(L'"')==s.npos,"private argument contains quote");return L"\""+s+L"\"";}
static std::wstring command(const std::vector<std::wstring>& args){auto value=quote(ownImage());for(const auto& a:args)value+=L" "+quote(a);return value;}
static std::string readBytes(const std::wstring& path){
    Handle h(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));need(h.valid(),"private receipt open");
    LARGE_INTEGER size{};need(GetFileSizeEx(h.value,&size)&&size.QuadPart>=0&&size.QuadPart<1024*1024,"private receipt bounded size");std::string value(size_t(size.QuadPart),'\0');DWORD n=0;
    if(!value.empty())need(ReadFile(h.value,value.data(),DWORD(value.size()),&n,nullptr)&&n==value.size(),"private receipt complete read");return value;
}
static J load(const std::wstring& path){return J::parse(readBytes(path));}
static void writeBytes(HANDLE h,const std::string& bytes){size_t offset=0;while(offset<bytes.size()){DWORD n=0;need(WriteFile(h,bytes.data()+offset,DWORD(bytes.size()-offset),&n,nullptr)&&n,"private write");offset+=n;}}
static void save(const std::wstring& path,const J& data){Handle h(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));need(h.valid(),"private receipt create");writeBytes(h.value,data.dump(2)+"\n");need(FlushFileBuffers(h.value)!=FALSE,"private receipt flush");}
static bool exists(const std::wstring& path){return GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES;}
static J identity(HANDLE h){const auto id=process::capture(h);need(bool(id),"capture exact owned process PID/birth");return {{"pid",id->pid},{"creationTime",id->creationTime}};}
static std::wstring eventName(const std::wstring& prefix,const std::wstring& tag,const std::wstring& suffix){return L"Local\\"+prefix+L"_"+tag+L"_"+suffix;}
static void signal(const std::wstring& prefix,const std::wstring& tag,const std::wstring& suffix){Handle h(OpenEventW(EVENT_MODIFY_STATE,FALSE,eventName(prefix,tag,suffix).c_str()));need(h.valid()&&SetEvent(h.value),"signal private event");}
static bool eventReady(const std::wstring& prefix,const std::wstring& tag,const std::wstring& suffix,DWORD budget){Handle h(OpenEventW(SYNCHRONIZE,FALSE,eventName(prefix,tag,suffix).c_str()));need(h.valid(),"open private wait event");return WaitForSingleObject(h.value,budget)==WAIT_OBJECT_0;}
static void await(const std::wstring& prefix,const std::wstring& tag,const std::wstring& suffix,DWORD budget=9000){need(eventReady(prefix,tag,suffix,budget),"private event test budget expired");}
static std::string binding(){return "private122:"+W2U(privateRoot);}
static void appendNativeLifecycleLog(const char* domain,const J& data){runtimeNotes.push_back({{"domain",domain},{"data",data}});}
#include "hidhide_cli_writer_runtime.h"
static std::wstring hidHideCliPath(){return ownImage();}
#include "actual_consumer.inc"

static J pending(){
    J p={{"process",g_hidHidePendingChildProcess!=nullptr},{"pipe",g_hidHidePendingReadPipe!=nullptr},{"job",g_hidHidePendingJob.ready()},{"writer",bool(g_hidHidePendingWriter)},{"attemptId",g_hidHidePendingAttempt}};
    if(g_hidHidePendingChildProcess){p["identity"]=identity(g_hidHidePendingChildProcess);p["rootExited"]=WaitForSingleObject(g_hidHidePendingChildProcess,0)==WAIT_OBJECT_0;}
    if(g_hidHidePendingJob.ready())p["jobIdle"]=g_hidHidePendingJob.idle();
    if(g_hidHidePendingWriter)p["writerActive"]=g_hidHidePendingWriter->record()&&g_hidHidePendingWriter->record()->active.has_value();return p;
}
static J disk(){return exists(file(L"writer.json"))?load(file(L"writer.json")):J(nullptr);}
static J coldObservation(){
    J result={{"disk",disk()},{"self",identity(GetCurrentProcess())},{"pending",pending()}};
    const auto loaded=writer::load(file(L"writer.json"),binding());
    if(loaded.state==writer::LoadState::Valid&&loaded.record->active){const auto& a=*loaded.record->active;
        result["parentProbe"]=process::stateName(hidHideCliWriterProbe(a.parent).state);
        result["childProbe"]=a.child?process::stateName(hidHideCliWriterProbe(*a.child).state):"none";
        result["jobProbe"]=int(hidHideCliWriterProbeJob(a.jobName));
    }return result;
}
static std::string expectedOutput(){std::string out="OUT:";for(size_t n=0;n<12289;++n)out+=char('A'+n%26);return out+"\nERR:"+std::string(6145,'e')+"\n";}
static std::wstring fakeArguments(const std::wstring& prefix,const std::wstring& tag,const std::wstring& mode){
    const auto me=identity(GetCurrentProcess());return L"--fake-cli "+quote(privateRoot)+L" "+quote(prefix)+L" "+quote(tag)+L" "+quote(mode)+L" "+std::to_wstring(me["pid"].get<DWORD>())+L" "+quote(U2W(me["creationTime"].get<std::string>()))+L" "+quote(std::wstring(12289,L'I'));
}
static J invoke(const std::wstring& prefix,const std::wstring& tag,const std::wstring& mode,bool shortDeadline=false){
    const auto begin=GetTickCount64();if(shortDeadline)g_inputHostSwitchRecoveryStartDeadline=begin+1500;
    std::string output;DWORD code=0;const auto absolute=shortDeadline?begin+3000:0;
    const bool ok=hidHideRun(fakeArguments(prefix,tag,mode),&output,&code,absolute);
    J result={{"ok",ok},{"code",code},{"output",output},{"begin",begin},{"end",GetTickCount64()},{"explicitDeadline",absolute},{"threadDeadline",g_inputHostSwitchRecoveryStartDeadline},{"requestId",g_inputHostSwitchRecoveryStartRequestId},{"power",currentPowerGeneration()},{"pending",pending()},{"disk",disk()},{"notes",runtimeNotes}};
    g_inputHostSwitchRecoveryStartDeadline=0;return result;
}
static int native(const std::wstring& prefix,const std::wstring& tag,const std::wstring& action){
    save(file(tag+L".entry.json"),coldObservation());
    if(action==L"crash"){
        std::thread([prefix,tag]{await(prefix,tag,L"die",12000);ExitProcess(67);}).detach();
        const auto result=invoke(prefix,tag,L"hold-tree");save(file(tag+L".native.json"),result);await(prefix,tag,L"die");ExitProcess(68);
    }
    const auto result=invoke(prefix,tag,action==L"reap"?L"fork-exit":action==L"hold"?L"hold":L"normal",action==L"reap");
    save(file(tag+L".native.json"),result);signal(prefix,tag,L"returned");
    if(action==L"reap"){
        await(prefix,tag,L"reap");const auto second=invoke(prefix,L"reaped",L"normal");save(file(L"reaped.native.json"),second);
    }return 0;
}
static J firstInstruction(DWORD parent,const std::string& parentBirth,const std::wstring& payload){
    // First intentional fake-CLI action: read the durable slot, before receipts,
    // events, stdout, descendant creation, or a private-world write.
    const auto loaded=writer::load(file(L"writer.json"),binding());
    const auto self=identity(GetCurrentProcess());J result={{"self",self},{"expectedParent",{{"pid",parent},{"creationTime",parentBirth}}},{"validLoad",loaded.state==writer::LoadState::Valid},{"argumentChars",payload.size()},{"argumentComplete",payload==std::wstring(12289,L'I')},{"stdoutIsPipe",GetFileType(GetStdHandle(STD_OUTPUT_HANDLE))==FILE_TYPE_PIPE},{"stdinSupplied",GetStdHandle(STD_INPUT_HANDLE)!=nullptr&&GetStdHandle(STD_INPUT_HANDLE)!=INVALID_HANDLE_VALUE}};
    result["mayRunBeforeFirstAction"]=false;
    if(loaded.record){std::string bytes;need(writer::encode(*loaded.record,bytes),"encode actual first-action read");result["slot"]=J::parse(bytes);const auto& active=loaded.record->active;
        if(active){result["mayRunBeforeFirstAction"]=active->stage==writer::Stage::MayRun&&active->child&&active->child->pid==self["pid"].get<DWORD>()&&active->child->creationTime==self["creationTime"].get<std::string>()&&active->parent.pid==parent&&active->parent.creationTime==parentBirth&&active->jobName==std::string(writer::jobPrefix)+active->attemptId;}
    }return result;
}
static int leaf(const std::wstring& prefix,const std::wstring& tag){
    BOOL inJob=FALSE;need(IsProcessInJob(GetCurrentProcess(),nullptr,&inJob)!=FALSE,"private descendant membership query");save(file(tag+L".leaf.json"),{{"self",identity(GetCurrentProcess())},{"inJob",inJob!=FALSE}});signal(prefix,tag,L"desc-ready");
    Handle write(OpenEventW(SYNCHRONIZE,FALSE,eventName(prefix,tag,L"write").c_str())),finish(OpenEventW(SYNCHRONIZE,FALSE,eventName(prefix,tag,L"finish").c_str()));need(write.valid()&&finish.valid(),"private leaf events");HANDLE waits[]={write.value,finish.value};const auto event=WaitForMultipleObjects(2,waits,FALSE,12000);
    if(event==WAIT_OBJECT_0){save(file(tag+L".leaf-world.json"),{{"writer",identity(GetCurrentProcess())},{"lateWrite",true}});await(prefix,tag,L"finish");return 0;}return event==WAIT_OBJECT_0+1?0:72;
}
static int fakeCli(const std::wstring& prefix,const std::wstring& tag,const std::wstring& mode,DWORD parent,const std::string& parentBirth,const std::wstring& payload){
    const auto first=firstInstruction(parent,parentBirth,payload);save(file(tag+L".first.json"),first);
    if(mode==L"hold-tree"||mode==L"fork-exit"){
        STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);si.hStdError=GetStdHandle(STD_ERROR_HANDLE);
        auto cmd=command({L"--leaf",privateRoot,prefix,tag});PROCESS_INFORMATION pi{};need(CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE,"own descendant launch");Handle processHandle(pi.hProcess),threadHandle(pi.hThread);await(prefix,tag,L"desc-ready");
    }
    signal(prefix,tag,L"ready");if(mode==L"fork-exit")return 0;
    if(mode==L"normal"){
        await(prefix,tag,L"finish");const auto output=expectedOutput();const auto split=output.find("ERR:");writeBytes(GetStdHandle(STD_OUTPUT_HANDLE),output.substr(0,split));writeBytes(GetStdHandle(STD_ERROR_HANDLE),output.substr(split));save(file(tag+L".world.json"),{{"self",identity(GetCurrentProcess())},{"normalWrite",true}});return 0;
    }
    Handle write(OpenEventW(SYNCHRONIZE,FALSE,eventName(prefix,tag,L"write").c_str())),finish(OpenEventW(SYNCHRONIZE,FALSE,eventName(prefix,tag,L"finish").c_str()));need(write.valid()&&finish.valid(),"private child events");HANDLE waits[]={write.value,finish.value};const auto event=WaitForMultipleObjects(2,waits,FALSE,12000);
    if(event==WAIT_OBJECT_0){save(file(tag+L".world.json"),{{"self",identity(GetCurrentProcess())},{"lateWrite",true}});await(prefix,tag,L"finish");return 0;}return event==WAIT_OBJECT_0+1?0:73;
}
struct Observer {
    struct Process {Handle handle;J id;std::string label;};
    std::wstring prefix;std::vector<Handle> events;std::vector<Process> processes;
    J result={{"pass",true},{"checks",0},{"failures",J::array()},{"observations",J::array()},{"observerHoldsJobHandle",false},{"cleanupJobUsed",false}};
    explicit Observer(std::wstring key):prefix(std::move(key)){
        for(const auto* tag:{L"normal",L"old",L"new",L"blocked",L"root",L"reaped"})for(const auto* suffix:{L"ready",L"write",L"finish",L"desc-ready",L"returned",L"die",L"reap"}){
            const auto h=CreateEventW(nullptr,TRUE,FALSE,eventName(prefix,tag,suffix).c_str());need(h&&GetLastError()!=ERROR_ALREADY_EXISTS,"fresh explicit private events");events.emplace_back(h);
        }
    }
    ~Observer(){cleanup();}
    void expect(bool ok,const char* assertion){result["checks"]=result["checks"].get<unsigned>()+1;if(!ok){result["pass"]=false;result["failures"].push_back(assertion);}}
    HANDLE launch(const std::wstring& tag,const std::wstring& action){auto cmd=command({L"--native",privateRoot,prefix,tag,action});STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};need(CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE,"private native process launch");Handle thread(pi.hThread);const auto h=pi.hProcess;processes.push_back({Handle(h),identity(h),W2U(tag)+" native"});return h;}
    HANDLE observe(const J& id,const std::string& label){Handle h(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE,FALSE,id.at("pid").get<DWORD>()));need(h.valid()&&identity(h.value)==id,"observe only exact self-created process");const auto value=h.value;processes.push_back({std::move(h),id,label});return value;}
    void ended(HANDLE h,const char* label,DWORD budget=6000){need(WaitForSingleObject(h,budget)==WAIT_OBJECT_0,label);}
    void ready(const std::wstring& tag){await(prefix,tag,L"ready");}
    J first(const std::wstring& tag,HANDLE nativeHandle){
        auto row=load(file(tag+L".first.json"));expect(row["validLoad"].get<bool>(),"first instruction reads valid private durable record");expect(row["mayRunBeforeFirstAction"].get<bool>(),"first instruction sees durable MayRun with exact parent child birth and named job");expect(row["expectedParent"]==identity(nativeHandle),"first instruction binds exact actual native parent");expect(row["argumentComplete"].get<bool>()&&row["argumentChars"].get<unsigned>()>4096,"long command input is complete");return row;
    }
    void normalComplete(const std::wstring& tag,HANDLE nativeHandle){
        ready(tag);const auto row=first(tag,nativeHandle);const auto child=observe(row["self"],W2U(tag)+" CLI");signal(prefix,tag,L"finish");ended(child,"normal exact child exit");ended(nativeHandle,"normal native exit");const auto receipt=load(file(tag+L".native.json"));
        expect(receipt["ok"].get<bool>()&&receipt["code"]==ERROR_SUCCESS,"actual hidHideRun normal command succeeds");expect(receipt["output"]==expectedOutput(),"stdout and stderr greater than 4096 bytes captured completely");expect(row["stdoutIsPipe"].get<bool>(),"actual stdout pipe inherited");expect(receipt["disk"]["active"].is_null(),"normal completed command durably settles writer empty");expect(!receipt["pending"]["process"].get<bool>()&&!receipt["pending"]["writer"].get<bool>(),"normal completed command leaves no pending owner");expect(exists(file(tag+L".world.json")),"alive child can perform its allowed private world write");result["observations"].push_back({{"tag",W2U(tag)},{"first",row},{"native",receipt}});
    }
    bool cleanup()noexcept{
        bool all=true;
        for(const auto& e:events)SetEvent(e.value);
        for(auto& p:processes)if(WaitForSingleObject(p.handle.value,2000)!=WAIT_OBJECT_0){if(identitySafe(p)){TerminateProcess(p.handle.value,99);if(WaitForSingleObject(p.handle.value,3000)!=WAIT_OBJECT_0)all=false;}else all=false;}
        return all;
    }
    static bool identitySafe(const Process& p)noexcept{try{return identity(p.handle.value)==p.id;}catch(...){return false;}}
    bool allExited()const{for(const auto& p:processes)if(WaitForSingleObject(p.handle.value,0)!=WAIT_OBJECT_0)return false;return true;}
};
static int observeCase(const std::wstring& scenario){
    const auto prefix=L"YMCC122_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());Observer o(prefix);o.result["case"]=W2U(scenario);o.result["privateRoot"]=W2U(privateRoot);
    try{
        if(scenario==L"normal-long-io"){
            const auto nativeHandle=o.launch(L"normal",L"normal");o.normalComplete(L"normal",nativeHandle);
        }else if(scenario==L"crash-cold-restart"){
            const auto parent=o.launch(L"old",L"crash");o.ready(L"old");await(prefix,L"old",L"desc-ready");const auto first=o.first(L"old",parent);const auto child=o.observe(first["self"],"old CLI");const auto descendantRow=load(file(L"old.leaf.json"));const auto descendant=o.observe(descendantRow["self"],"old descendant");const auto oldSlot=disk();
            o.expect(WaitForSingleObject(child,0)==WAIT_TIMEOUT&&WaitForSingleObject(descendant,0)==WAIT_TIMEOUT,"child and descendant alive while owning native lives");o.expect(!exists(file(L"old.native.json")),"crash occurs while real hidHideRun is still executing");signal(prefix,L"old",L"die");o.ended(parent,"native ExitProcess completion");DWORD exitCode=0;need(GetExitCodeProcess(parent,&exitCode)!=FALSE,"parent exit code");o.expect(exitCode==67,"real ExitProcess bypasses RAII and pending destruction");
            const bool rootDead=WaitForSingleObject(child,3000)==WAIT_OBJECT_0,descendantDead=WaitForSingleObject(descendant,3000)==WAIT_OBJECT_0;o.expect(rootDead&&descendantDead,"owned job close independently proves child and descendant exit");signal(prefix,L"old",L"write");Sleep(100);o.expect(!exists(file(L"old.world.json"))&&!exists(file(L"old.leaf-world.json")),"no late private world write after native death");o.expect(disk()==oldSlot&&!disk()["active"].is_null(),"native crash preserves exact durable MayRun slot");
            const auto fresh=o.launch(L"new",L"normal");o.normalComplete(L"new",fresh);const auto entry=load(file(L"new.entry.json"));const auto finish=load(file(L"new.native.json"));o.expect(entry["self"]!=identity(parent)&&!entry["pending"]["process"].get<bool>()&&!entry["pending"]["writer"].get<bool>(),"fresh native has independent identity and naturally empty pending memory");o.expect(entry["disk"]==oldSlot&&entry["parentProbe"]=="dead"&&entry["childProbe"]=="dead","fresh native reads preserved slot and real probes prove exact parent and child dead");o.expect(entry["jobProbe"]==int(ymcc::hidHideCliWriterRuntime::JobState::Absent)||entry["jobProbe"]==int(ymcc::hidHideCliWriterRuntime::JobState::Idle),"fresh native real job probe proves absent or idle");
            bool settled=false;for(const auto& note:finish["notes"]){const auto& n=note["data"];if(n["phase"]=="settled"&&n["reason"]=="exact-native-child-and-job-writer-quiescent"&&n["attemptId"]==oldSlot["active"]["attemptId"])settled=true;}o.expect(settled,"actual writerRuntime settles old attempt before new child may run");o.result["coldEntry"]=entry;o.result["oldFirst"]=first;o.result["oldSlot"]=oldSlot;
        }else if(scenario==L"live-owner-exclusion"){
            const auto parent=o.launch(L"old",L"hold");o.ready(L"old");const auto row=o.first(L"old",parent);const auto child=o.observe(row["self"],"live old CLI");const auto before=disk();const auto contender=o.launch(L"blocked",L"normal");o.ended(contender,"fresh contender refused by owner lock");const auto denied=load(file(L"blocked.native.json"));o.expect(!denied["ok"].get<bool>()&&denied["code"]==ERROR_BUSY,"fresh native is refused by actual writer file owner exclusion");o.expect(!exists(file(L"blocked.first.json")),"owner refusal launches no new CLI");o.expect(disk()==before,"refused fresh native preserves old MayRun record");o.expect(WaitForSingleObject(parent,0)==WAIT_TIMEOUT&&WaitForSingleObject(child,0)==WAIT_TIMEOUT,"old native and child remain alive throughout refusal");bool lockRefused=false;for(const auto& note:denied["notes"]){const auto reason=note["data"]["reason"].get<std::string>();if(reason=="owner-open-or-lock-failed"||reason=="owner-create-or-lock-failed")lockRefused=true;}o.expect(lockRefused,"runtime reports the real Session owner lock error");signal(prefix,L"old",L"finish");o.ended(child,"old child normal finish");o.ended(parent,"old native normal finish");o.expect(disk()["active"].is_null(),"old owner alone settles on its natural completion");o.result["denied"]=denied;
        }else if(scenario==L"root-exit-job-active-reap"){
            const auto parent=o.launch(L"root",L"reap");o.ready(L"root");const auto first=o.first(L"root",parent);const auto root=o.observe(first["self"],"early-exit root");await(prefix,L"root",L"desc-ready");const auto descendant=o.observe(load(file(L"root.leaf.json"))["self"],"active subtree descendant");o.ended(root,"root exits naturally before descendant",1000);o.expect(WaitForSingleObject(descendant,0)==WAIT_TIMEOUT,"root exited while exact descendant is still active");o.expect(!exists(file(L"root.native.json"))&&!disk()["active"].is_null(),"root exit with active job neither succeeds nor clears durable writer");
            await(prefix,L"root",L"returned");const auto timed=load(file(L"root.native.json"));o.expect(!timed["ok"].get<bool>()&&timed["code"]==WAIT_TIMEOUT,"active subtree reaches original deadline and returns timeout");o.expect(timed["end"].get<ULONGLONG>()-timed["begin"].get<ULONGLONG>()>=1400&&timed["end"].get<ULONGLONG>()-timed["begin"].get<ULONGLONG>()<2500,"real earlier thread deadline wins without renewed 4s or 30s budget");o.expect(timed["pending"]["writer"].get<bool>()&&timed["pending"]["writerActive"].get<bool>()&&!timed["disk"]["active"].is_null(),"deadline retains durable writer lease in real pending RAII state");o.ended(descendant,"actual owned requestStop ends descendant");signal(prefix,L"root",L"reap");o.normalComplete(L"reaped",parent);o.expect(disk()["active"].is_null(),"actual later reaper settles pending writer then allows normal CLI");o.result["timeout"]=timed;
        }else throw std::runtime_error("unknown private scenario");
        o.expect(o.allExited(),"all exact owned processes exited before cleanup");
    }catch(const std::exception& e){o.result["pass"]=false;o.result["infrastructureException"]=e.what();}
    const bool cleaned=o.cleanup();o.result["noOwnedProcessesRemain"]=cleaned&&o.allExited();o.result["actualDeviceCalls"]=0;o.result["realCliOrHostRuns"]=0;o.result["stdinTested"]=false;save(file(L"observer-result.json"),o.result);std::cout<<o.result.dump()<<"\n";return o.result["pass"].get<bool>()?0:1;
}
int wmain(int argc,wchar_t** argv){
    try{
        need(argc>=4,"explicit private role and root");const std::wstring role=argv[1];privateRoot=argv[role==L"--observe"?3:2];need(fspath::path(privateRoot).is_absolute(),"private absolute root only");
        if(role==L"--observe"&&argc==4)return observeCase(argv[2]);
        if(role==L"--native"&&argc==6)return native(argv[3],argv[4],argv[5]);
        if(role==L"--fake-cli"&&argc==9)return fakeCli(argv[3],argv[4],argv[5],std::stoul(argv[6]),W2U(argv[7]),argv[8]);
        if(role==L"--leaf"&&argc==5)return leaf(argv[3],argv[4]);
        throw std::runtime_error("invalid private fixture role");
    }catch(const std::exception& e){std::cerr<<"fixture exception: "<<e.what()<<"\n";return 90;}
}