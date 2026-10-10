// Event-driven context-process watch + bounded windows; ONLY source-built inert
// processes. No Steam discovery, CDP, third-party loader, settings, or hardware IO.
#include "../native/decky_sidebar_bootstrap.h"
#include "../native/decky_sidebar_runtime.h"
#include <condition_variable>
#include <iostream>
#include <fstream>
using ymcc::deckysidebar::Runtime;
static int checks=0;
static void check(bool value,const char* label){if(!value)throw std::runtime_error(label);++checks;}
struct Handle{HANDLE h=nullptr;explicit Handle(HANDLE value):h(value){}~Handle(){if(h)CloseHandle(h);}Handle(const Handle&)=delete;};
struct InertContext {
    HANDLE process=nullptr;DWORD pid=0;
    explicit InertContext(const std::wstring& executable){
        std::wstring line=L"\""+executable+L"\" --context-standin";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION child{};
        if(!CreateProcessW(executable.c_str(),line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))throw std::runtime_error("cannot create own inert context");
        process=child.hProcess;pid=child.dwProcessId;CloseHandle(child.hThread);
    }
    ~InertContext(){stop();if(process)CloseHandle(process);}
    void stop(){if(process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT){TerminateProcess(process,31);WaitForSingleObject(process,1500);}}
    bool alive()const{return process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;}
    InertContext(const InertContext&)=delete;
};
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    try{
        wchar_t buffer[32768];const DWORD size=GetModuleFileNameW(nullptr,buffer,32768);
        if(!size||size>=32768)throw std::runtime_error("fixture path");
        const std::wstring executable(buffer,size);
        const std::filesystem::path build=std::filesystem::path(executable).parent_path();
        if(build!=L"G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Decky-Sidebar\\Build" && build!=L"G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Decky-Console20\\Build" && build!=L"G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Decky-Console22\\NativeRegression\\Build" && build!=L"G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Decky-Console23\\Build" && build!=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Launch25\Build)" && build!=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Bootstrap26\Build)" && build!=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Input27\Build)")throw std::runtime_error("fixture outside explicit task Build roots");
        if(std::filesystem::path(executable).parent_path()!=build)throw std::runtime_error("fixture must stay in task Build");
        const auto home=(build/L"YMCC-Decky-Context-Fixture-Home").wstring();
        std::filesystem::create_directories(home); // Task-local inert home; no settings files.
        wchar_t inherited[32768];const auto inheritedSize=GetEnvironmentVariableW(L"UNPRIVILEGED_PATH",inherited,32768);
        if((argc==2&&std::string(argv[1])=="--context-standin")||(inheritedSize&&inheritedSize<32768&&home==std::wstring(inherited,inheritedSize))){Sleep(40000);return 0;}
        InertContext first(executable),second(executable),third(executable);
        std::mutex mutex;std::condition_variable changed;
        std::atomic<int> validations{0},preparations{0},refreshes{0},renewals{0};
        std::atomic<DWORD> selected{first.pid};
        ymcc::deckymirror::ContextHandleSlot slot;
        Runtime runtime; // Joins before captured state, slot, and manual children are destroyed.
        auto lease=[&]{const DWORD pid=selected.load();if(!pid)return std::string("mirror-bootstrap-context-unavailable");ymcc::deckymirror::ObservedProcess observed(pid);slot.replace(observed.release());return std::string{};};
        runtime.initialize({executable,home,[&]{++validations;return std::string{};},
            [&](const auto&){changed.notify_all();},
            [&]{{std::lock_guard lock(mutex);++refreshes;}changed.notify_all();return lease();},
            [&]{++renewals;},[]{},
            [&]{int attempt;{std::lock_guard lock(mutex);attempt=++preparations;}changed.notify_all();return attempt<3?std::string("mirror-bootstrap-context-unavailable"):lease();},
            [&]{return slot.take();}});
        auto wait=[&](const auto& predicate,int milliseconds){std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::milliseconds(milliseconds),predicate);};
        check(!runtime.requestBindingRefresh(1),"default opt-in off rejects demand");
        runtime.observeSteam(1);runtime.setEnabled(true);
        check(wait([&]{return preparations>=1&&runtime.snapshot().bindingRetry;},2000),"cold context failure actually opens finite startup window");
        check(runtime.snapshot().pid==0&&validations==1,"not-ready context never launches fixture or rehashes on each wait");
        check(wait([&]{return runtime.snapshot().phase=="loader-started";},5500),"late readiness succeeds through existing 1/3 second offsets");
        check(preparations==3&&validations==2,"late startup retries only binding and revalidates resource once before launch");
        const DWORD loaderPid=runtime.snapshot().pid;Handle loader(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,loaderPid));
        check(loader.h&&WaitForSingleObject(loader.h,0)==WAIT_TIMEOUT,"inert owned loader really running");
        check(first.alive()&&!runtime.snapshot().bindingRetry&&runtime.snapshot().bindingAttempts==3,"successful binding installs process lease and stops retry clock");
        Sleep(250);check(preparations==3&&refreshes==0,"successful startup has no idle retry/scan");
        selected=second.pid;first.stop();
        check(wait([&]{return refreshes>=1&&!runtime.snapshot().bindingRetry;},2500),"context process exit causes event-driven rebind without new Steam session");
        check(runtime.snapshot().pid==loaderPid&&validations==2&&preparations==3,"context exit cannot reopen loader launch/resource-validation budget");
        {
            ymcc::deckymirror::ObservedProcess observed(second.pid);Handle readOnly(observed.release());
            SetLastError(ERROR_SUCCESS);const bool denied=!TerminateProcess(readOnly.h,99)&&GetLastError()==ERROR_ACCESS_DENIED;
            check(denied&&second.alive(),"observer lease has no terminate right; test target is only our inert context");
        }
        selected=0;second.stop();
        const ULONGLONG windowStarted=GetTickCount64();
        check(wait([&]{return refreshes>=2&&runtime.snapshot().bindingRetry;},2500),"second genuine context exit opens a new finite window");
        check(wait([&]{return refreshes>=6&&!runtime.snapshot().bindingRetry&&runtime.snapshot().bindingAttempts==5;},23000),"persistent unavailability exhausts initial plus four retries");
        check(GetTickCount64()-windowStarted>=19000,"full window follows production 20-second final offset, not fake accelerated time");
        check(refreshes==6&&runtime.snapshot().reason=="mirror-bootstrap-context-unavailable","failure remains visible, no success fabricated");
        Sleep(1200);check(refreshes==6&&runtime.snapshot().pid==loaderPid,"exhausted window remains idle and does not restart loader");
        check(validations==2&&renewals>=8,"network retries do not repeat resource validation and use fresh operation tokens");
        selected=third.pid;
        check(runtime.requestBindingRefresh(42),"new committed resume can open next transport-only window");
        check(!runtime.requestBindingRefresh(42),"duplicate committed event cannot replenish window");
        check(wait([&]{return refreshes>=7&&!runtime.snapshot().bindingRetry&&runtime.snapshot().reason=="mirror-handshake-pending";},2500),"new event recovers after exhaustion");
        runtime.setEnabled(false);
        check(WaitForSingleObject(loader.h,1000)==WAIT_OBJECT_0&&runtime.snapshot().pid==0,"off terminates only own inert loader Job");
        check(third.alive(),"off closes watch lease without terminating watched context");
        check(!runtime.requestBindingRefresh(43)&&runtime.snapshot().phase=="disabled","off cannot start retries or a loader");
        runtime.shutdown();
        {
            std::atomic<int> prepares{0},validates{0};
            Runtime rejected;
            rejected.initialize({executable,home,[&]{++validates;return std::string{};},[&](const auto&){changed.notify_all();},[]{return std::string{};},[]{},[]{},
                [&]{++prepares;return std::string("mirror-bootstrap-owner-mismatch");},[]{return static_cast<HANDLE>(nullptr);}});
            rejected.observeSteam(1);rejected.setEnabled(true);
            check(wait([&]{return rejected.snapshot().phase=="unavailable";},2000),"owner mismatch is a terminal startup failure");
            Sleep(1200);check(prepares==1&&validates==1&&!rejected.snapshot().bindingRetry&&!rejected.snapshot().pid,"security failure cannot retry or create loader");
            rejected.shutdown();
        }
        {
            std::atomic<int> prepares{0},validates{0};
            Runtime replacedResource;
            replacedResource.initialize({executable,home,[&]{return ++validates==1?std::string{}:std::string("resource-hash-mismatch");},[&](const auto&){changed.notify_all();},[]{return std::string{};},[]{},[]{},
                [&]{return ++prepares==1?std::string("mirror-bootstrap-context-unavailable"):std::string{};},[]{return static_cast<HANDLE>(nullptr);}});
            replacedResource.observeSteam(1);replacedResource.setEnabled(true);
            check(wait([&]{return replacedResource.snapshot().reason=="resource-hash-mismatch";},3500),"delayed ready binding cannot launch a changed resource");
            Sleep(1200);check(prepares==2&&validates==2&&!replacedResource.snapshot().pid&&!replacedResource.snapshot().bindingRetry,"final resource check fails closed without another retry");
            replacedResource.shutdown();
        }
        check(third.alive(),"terminal startup failures never terminate independent watched-context fixture");
        const auto out=build.parent_path()/L"validation"/L"CONTEXT-LIFECYCLE-IMPLEMENTATION06.json";
        std::ofstream evidence(out,std::ios::binary);
        evidence<<"{\"project\":\"YMCC Decky 侧边栏\",\"checks\":"<<checks<<",\"preparations\":"<<preparations.load()<<",\"validations\":"<<validations.load()<<",\"refreshes\":"<<refreshes.load()<<",\"actualSteam\":false,\"thirdPartyLoader\":false,\"hardware\":false,\"userConfigWrites\":0,\"actualProductionRetryOffsets\":true,\"watchedContextSurvivesDisable\":true}";
        std::cout<<"Context lifecycle fixture passed: "<<checks<<"; full production retry window; realSteam=0; hardware=0; userConfig=0\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}



