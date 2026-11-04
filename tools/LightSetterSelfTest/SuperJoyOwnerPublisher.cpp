#include "superjoy_lighting_owner.h"
#include <json.hpp>
#include <filesystem>
#include <iostream>
#include <vector>
#include <thread>
using json=nlohmann::json;
namespace owner=ymcc::superjoy_owner;
static std::wstring quote(const std::wstring& value){
    std::wstring result=L"\"";std::size_t slash=0;
    for(auto c:value){if(c==L'\\'){++slash;continue;}if(c==L'\"'){result.append(slash*2+1,L'\\');result+=c;}else{result.append(slash,L'\\');result+=c;}slash=0;}
    result.append(slash*2,L'\\');return result+L"\"";
}
static std::uint64_t birth(){FILETIME c{},e{},k{},u{};if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))throw std::runtime_error("birth");return (static_cast<std::uint64_t>(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
static PROCESS_INFORMATION spawn(const std::wstring& executable,const std::vector<std::wstring>& args,DWORD flags=0){
    std::wstring text=quote(executable);for(const auto& arg:args)text+=L" "+quote(arg);
    std::vector<wchar_t> command(text.begin(),text.end());command.push_back(0);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|flags,nullptr,nullptr,&startup,&process))throw std::runtime_error("spawn failed "+std::to_string(GetLastError()));
    return process;
}
static void check(bool ok,const char* what,json& checks){checks.push_back({{"name",what},{"ok",ok}});if(!ok)throw std::runtime_error(what);}
static std::wstring self(){std::vector<wchar_t> text(32768);const auto n=GetModuleFileNameW(nullptr,text.data(),static_cast<DWORD>(text.size()));if(!n||n==text.size())throw std::runtime_error("self path");return std::wstring(text.data(),n);}
static owner::HandoffRecord* writable(const owner::Admission& admission,HANDLE& mapping){
    mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,owner::mappingName(admission.parentPid,admission.parentBirth).c_str());if(!mapping)throw std::runtime_error("open own mapping");
    auto* record=static_cast<owner::HandoffRecord*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(owner::HandoffRecord)));
    if(!record){CloseHandle(mapping);throw std::runtime_error("map own record");}return record;
}
static json debtCase(){
    json checks=json::array();owner::Owner native;owner::Context context{7,9,true,"HandheldCompanion.Devices.AYANEO3"};
    const std::wstring path=L"\\\\?\\hid#vid_1c4f&pid_0002#software-only-owner";
    auto admission=native.begin(context,path);check(admission.has_value(),"native begin",checks);
    const auto leaveName=L"Local\\YMCC-SuperJoy-OwnerTest-Leave-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(birth());
    HANDLE leave=CreateEventW(nullptr,TRUE,FALSE,leaveName.c_str());if(!leave)throw std::runtime_error("exit event");
    auto child=spawn(self(),{L"--inert-child",leaveName},CREATE_SUSPENDED);HANDLE control=child.hProcess;
    HANDLE limited=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),control,GetCurrentProcess(),&limited,SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,0))throw std::runtime_error("limited child handle");
    check(native.attachChild(admission->token,limited,context)&&limited==nullptr,"exact limited handle transferred",checks);
    check(ResumeThread(child.hThread)!=static_cast<DWORD>(-1),"resume inert child",checks);CloseHandle(child.hThread);
    const auto wait=native.waitChild(admission->token,40,20,[&]{return context;});
    check(wait.terminationAttempted&&!wait.terminationSucceeded&&!wait.returned&&wait.terminationError==ERROR_ACCESS_DENIED,"termination rights failure keeps real live child",checks);
    check(!native.finish(admission->token),"unsignaled child cannot release lease",checks);
    check(native.observation().childRetained&&native.observation().phase==owner::Phase::Retiring,"retiring handle/token retained",checks);
    check(!native.begin(context,path)&&!native.current(admission->token,context),"debt blocks admission/current authority",checks);
    check(!native.finish("older-token"),"unmatched release refused",checks);
    SetEvent(leave);check(WaitForSingleObject(control,2000)==WAIT_OBJECT_0,"real child observed returned",checks);
    check(native.observeReturn()&&native.observation().observedReturns==1,"exact observed return settles once",checks);
    check(!native.observeReturn()&&native.observation().observedReturns==1,"duplicate observation cannot settle twice",checks);
    auto next=native.begin(context,path);check(next&&next->token!=admission->token,"same endpoint gets unique successor admission",checks);
    check(!native.finish(admission->token)&&native.observation().token==next->token,"old completion cannot release successor",checks);
    check(native.cancelBeforeSpawn(next->token),"unspawned successor cleanup",checks);
    CloseHandle(control);CloseHandle(leave);
    return {{"status","PASS"},{"checks",checks},{"terminationFailed",true},{"terminationError",wait.terminationError},{"realObservedReturns",1},{"hardwareIo",false}};
}
int wmain(int argc,wchar_t** argv){
    try{
        if(argc==3&&std::wstring(argv[1])==L"--inert-child"){
            HANDLE leave=OpenEventW(SYNCHRONIZE,FALSE,argv[2]);if(!leave)return 5;
            const auto status=WaitForSingleObject(leave,15000);CloseHandle(leave);return status==WAIT_OBJECT_0?0:6;
        }
        if(argc==2&&std::wstring(argv[1])==L"--debt"){std::cout<<debtCase().dump()<<std::endl;return 0;}
        if(argc!=6)throw std::runtime_error("scenario dotnet fixture.dll child-result.json ready-event");
        const std::string scenario=std::filesystem::path(argv[1]).string();
        if(scenario=="foreign-helper"){
            json checks=json::array();owner::Owner native;owner::Context context{7,9,true,"HandheldCompanion.Devices.AYANEO3"};
            const std::wstring path=L"\\\\?\\hid#vid_1c4f&pid_0002#software-only-owner";
            const auto admission=native.begin(context,path);check(admission.has_value(),"foreign case begins intended admission",checks);
            const auto exitName=L"Local\\YMCC-SuperJoy-ForeignTest-Exit-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(birth());
            HANDLE leave=CreateEventW(nullptr,TRUE,FALSE,exitName.c_str());HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,argv[5]);
            if(!leave||!ready)throw std::runtime_error("foreign test events");
            auto intended=spawn(self(),{L"--inert-child",exitName},CREATE_SUSPENDED);HANDLE transferred=intended.hProcess;
            check(native.attachChild(admission->token,transferred,context),"only intended native child registered",checks);
            ResumeThread(intended.hThread);CloseHandle(intended.hThread);
            auto probe=spawn(argv[2],{argv[3],L"--lease-child",owner::widen(admission->model),path,owner::widen(admission->token),argv[4],argv[5],L"reject"});
            CloseHandle(probe.hThread);
            DWORD readyStatus=WAIT_TIMEOUT;const auto limit=GetTickCount64()+5000;
            while(GetTickCount64()<limit){native.refresh(context);readyStatus=WaitForSingleObject(ready,25);if(readyStatus==WAIT_OBJECT_0)break;}
            const auto probeWait=WaitForSingleObject(probe.hProcess,2000);DWORD probeExit=99;GetExitCodeProcess(probe.hProcess,&probeExit);CloseHandle(probe.hProcess);
            SetEvent(leave);const auto wait=native.waitChild(admission->token,2000,500,[&]{return context;});const auto released=native.finish(admission->token);
            CloseHandle(leave);CloseHandle(ready);
            check(readyStatus==WAIT_OBJECT_0&&probeWait==WAIT_OBJECT_0,"foreign probe process returned",checks);
            check(wait.returned&&released,"intended child actually returned and released",checks);
            check(probeExit==0,"different helper PID cannot join the intended child admission",checks);
            std::cout<<json{{"status","PASS"},{"scenario",scenario},{"checks",checks},{"hardwareIo",false}}.dump()<<std::endl;return 0;
        }
        json checks=json::array();owner::Owner native;owner::Context context{7,9,true,"HandheldCompanion.Devices.AYANEO3"};
        const std::wstring path=L"\\\\?\\hid#vid_1c4f&pid_0002#software-only-owner";
        auto admission=native.begin(context,path);check(admission.has_value(),"current native admission",checks);
        check(native.observation().phase==owner::Phase::Admitting,"no child means admitting, never Active",checks);
        check(!native.begin(context,path),"concurrent native admission refused",checks);
        HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,argv[5]);if(!ready)throw std::runtime_error("ready event");
        auto expectedModel=admission->model;auto expectedPath=path;auto expectedToken=admission->token;
        const bool rejected=scenario.rfind("reject-",0)==0;
        if(scenario=="reject-model")expectedModel="HandheldCompanion.Devices.AYANEONEXT2";
        if(scenario=="reject-path")expectedPath+=L"-other";
        if(scenario=="reject-token")expectedToken[expectedToken.size()-1]='9';
        const std::vector<std::wstring> arguments{argv[3],L"--lease-child",owner::widen(expectedModel),expectedPath,owner::widen(expectedToken),argv[4],argv[5],rejected?L"reject":L"guard"};
        auto child=spawn(argv[2],arguments,CREATE_SUSPENDED);HANDLE transferred=child.hProcess;
        FILETIME childCreation{},childExit{},childKernel{},childUser{};
        if(!GetProcessTimes(child.hProcess,&childCreation,&childExit,&childKernel,&childUser))throw std::runtime_error("child birth");
        const auto childBirth=(static_cast<std::uint64_t>(childCreation.dwHighDateTime)<<32)|childCreation.dwLowDateTime;
        check(native.attachChild(admission->token,transferred,context)&&transferred==nullptr,"suspended exact process registered before Active",checks);
        if(scenario=="reject-odd"){
            HANDLE mapping=nullptr;auto* record=writable(*admission,mapping);
            std::atomic_ref<std::uint64_t>(record->sequence).fetch_or(1,std::memory_order_release);UnmapViewOfFile(record);CloseHandle(mapping);
        }
        check(ResumeThread(child.hThread)!=static_cast<DWORD>(-1),"resume owned helper",checks);CloseHandle(child.hThread);
        DWORD readyStatus=WAIT_TIMEOUT;const auto readyDeadline=GetTickCount64()+5000;
        while(GetTickCount64()<readyDeadline){native.refresh(context);readyStatus=WaitForSingleObject(ready,50);if(readyStatus==WAIT_OBJECT_0)break;}
        check(readyStatus==WAIT_OBJECT_0,"child joined or explicitly refused native contract",checks);CloseHandle(ready);
        if(scenario=="parent-exit"){
            std::cout<<json{{"status","PARENT_EXIT_ARMED"},{"parentPid",admission->parentPid},{"token",admission->token},{"activeRecordLeftForGuardian",true},{"childPid",child.dwProcessId},{"childBirth",childBirth}}.dump()<<std::endl;
            TerminateProcess(GetCurrentProcess(),0);return 7;
        }
        HANDLE waitEntered=nullptr;std::atomic<bool> prematureFinishRefused{false};std::thread stopper;
        if(scenario=="event"){
            waitEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!waitEntered)throw std::runtime_error("wait barrier");
            stopper=std::thread([&]{
                if(WaitForSingleObject(waitEntered,2000)==WAIT_OBJECT_0){
                    prematureFinishRefused.store(!native.finish(admission->token),std::memory_order_release);
                    native.retire();
                }
            });
        }
        else if(scenario=="power"){context.generation++;context.writesAllowed=false;native.refresh(context);}
        else if(scenario=="topology"){context.topology++;native.refresh(context);}
        else if(scenario=="model"){context.model="HandheldCompanion.Devices.AYANEONEXT2";native.refresh(context);}
        else if(scenario=="tamper-path"||scenario=="future-heartbeat"){
            HANDLE mapping=nullptr;auto* record=writable(*admission,mapping);auto seq=std::atomic_ref<std::uint64_t>(record->sequence);
            const auto before=seq.load(std::memory_order_acquire);seq.store(before+1,std::memory_order_release);
            if(scenario=="tamper-path")record->path[path.size()-1]=L'X';else record->heartbeat=GetTickCount64()+10000;
            seq.store(before+2,std::memory_order_release);UnmapViewOfFile(record);CloseHandle(mapping);
        }
        owner::WaitResult wait;
        if(scenario=="heartbeat"||scenario=="future-heartbeat"||scenario=="tamper-path"||rejected){
            // Intentionally do not refresh a stale/corrupt publication. A lock or
            // an otherwise-live parent alone cannot keep a helper admitted.
            for(int i=0;i<80;i++){
                const auto state=native.observation();(void)state;
                // use the native owner wait only after the guardian has had its
                // bounded chance; the ready child's process is owned by native.
                if(std::filesystem::exists(argv[4]))break;
                Sleep(50);
            }
            wait=native.waitChild(admission->token,1000,500,[&]{return context;});
        }else wait=native.waitChild(admission->token,2000,500,[&]{if(waitEntered)SetEvent(waitEntered);return context;});
        if(stopper.joinable())stopper.join();if(waitEntered)CloseHandle(waitEntered);
        if(scenario=="event")check(prematureFinishRefused.load(std::memory_order_acquire),"concurrent completion cannot close a handle while native is waiting",checks);
        check(wait.returned&&wait.exitCode==0,"real helper exit observed",checks);
        const bool released=native.finish(admission->token);check(released,"exact helper return releases ownership",checks);
        check(native.observation().phase==owner::Phase::Idle&&native.observation().observedReturns==1,"single return accounted",checks);
        check(!native.finish(admission->token),"duplicate completion refused",checks);
        std::cout<<json{{"status","PASS"},{"scenario",scenario},{"checks",checks},{"token",admission->token},{"recordBytes",sizeof(owner::HandoffRecord)},
            {"parentPid",admission->parentPid},{"parentBirth",admission->parentBirth},{"childReturned",wait.returned},{"childExitCode",wait.exitCode},
            {"terminationAttempted",wait.terminationAttempted},{"ownerReleased",released},{"hardwareIo",false}}.dump()<<std::endl;return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
