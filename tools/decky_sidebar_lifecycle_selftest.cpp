// Source-built Windows process fixture only. No Steam, third-party loader, settings,
// network, or hardware IO. Production Runtime owns this inert child's Job.
#include "../native/decky_sidebar_runtime.h"
#include <condition_variable>
#include <iostream>
using ymcc::deckysidebar::Runtime;
static int checks=0;
static void check(bool value,const char* name){if(!value)throw std::runtime_error(name);++checks;}
struct OwnedHandle{HANDLE h=nullptr;explicit OwnedHandle(HANDLE value):h(value){}~OwnedHandle(){if(h)CloseHandle(h);}OwnedHandle(const OwnedHandle&)=delete;};
static HANDLE fixtureProcess(DWORD pid,const std::wstring& executable){
    HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE,FALSE,pid);
    if(!process)throw std::runtime_error("fixture process unavailable");
    wchar_t image[32768];DWORD size=32768;
    if(!QueryFullProcessImageNameW(process,0,image,&size)||_wcsicmp(image,executable.c_str())){CloseHandle(process);throw std::runtime_error("not our source-built fixture");}
    return process;
}
int main(){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    try{
        wchar_t module[32768];const DWORD length=GetModuleFileNameW(nullptr,module,32768);
        if(!length||length>=32768)throw std::runtime_error("fixture executable path");
        const std::wstring executable(module,length);
        const std::filesystem::path build=std::filesystem::path(executable).parent_path();
        if(build!=L"G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Decky-Sidebar\\Build" && build!=L"G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Decky-Console20\\Build" && build!=L"G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Decky-Console22\\NativeRegression\\Build" && build!=L"G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Decky-Console23\\Build" && build!=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Launch25\Build)" && build!=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Bootstrap26\Build)" && build!=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Input27\Build)")throw std::runtime_error("fixture outside explicit task Build roots");
        if(std::filesystem::path(executable).parent_path()!=build)throw std::runtime_error("fixture must run within explicit task Build");
        const auto home=(build/L"YMCC-Decky-Lifecycle-Fixture-Home").wstring();
        std::filesystem::create_directories(home); // Task-local inert home; no settings files.
        wchar_t childHome[32768];const DWORD homeLength=GetEnvironmentVariableW(L"UNPRIVILEGED_PATH",childHome,32768);
        if(homeLength&&homeLength<32768&&home==std::wstring(childHome,homeLength)){
            Sleep(15000);return 0; // Inert owned-loader stand-in; Job cleanup ends it early.
        }
        check(true,"source-built fixture located within explicit task Build");
        std::mutex mutex;std::condition_variable changed;
        std::atomic<int> preflights{0},refreshes{0},beginnings{0},mode{0};
        OwnedHandle entered(CreateEventW(nullptr,TRUE,FALSE,nullptr)),cancel(CreateEventW(nullptr,TRUE,FALSE,nullptr));
        if(!entered.h||!cancel.h)throw std::runtime_error("fixture event creation failed");
        Runtime runtime; // Destroy worker before captured events/counters/mutex on exceptions.
        runtime.initialize({executable,home,[&]{++preflights;return std::string{};},
            [&](const auto&){changed.notify_all();},
            [&]{const int requestedMode=mode.load();{std::lock_guard lock(mutex);++refreshes;}changed.notify_all();if(requestedMode==2){SetEvent(entered.h);WaitForSingleObject(cancel.h,2000);}return requestedMode==1?std::string("mirror-bootstrap-context-unavailable"):std::string{};},
            [&]{++beginnings;ResetEvent(cancel.h);},[&]{SetEvent(cancel.h);}});
        auto phase=[&](const char* name){std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::seconds(3),[&]{return runtime.snapshot().phase==name;});};
        auto calls=[&](int value){std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::seconds(2),[&]{return refreshes>=value;});};
        check(!runtime.requestBindingRefresh(1),"off cannot start rebind or loader");
        runtime.observeSteam(1);check(preflights==0&&refreshes==0,"observed running Steam with opt-in off performs no work");
        check(runtime.setEnabled(true)&&phase("loader-started"),"production Runtime launches only source-built inert child");
        const DWORD firstPid=runtime.snapshot().pid;OwnedHandle first(fixtureProcess(firstPid,executable));
        check(firstPid&&WaitForSingleObject(first.h,0)==WAIT_TIMEOUT,"owned loader fixture really alive");
        check(preflights==1&&beginnings==1,"one admitted launch/preflight");
        check(!runtime.requestBindingRefresh(0),"generation zero is not a committed resume");
        check(runtime.requestBindingRefresh(10),"committed resume admits transport refresh for live owner");
        check(!runtime.requestBindingRefresh(10),"duplicate generation coalesced");
        check(calls(1),"refresh callback actually executes on worker");
        check(!runtime.requestBindingRefresh(9),"older generation rejected");
        check(runtime.snapshot().pid==firstPid&&preflights==1,"refresh reuses owned loader, never launches a second one");
        mode=1;check(runtime.requestBindingRefresh(11)&&calls(2),"missing context is an explicit refresh failure");
        {std::unique_lock lock(mutex);check(changed.wait_for(lock,std::chrono::seconds(2),[&]{return runtime.snapshot().reason=="mirror-bootstrap-context-unavailable";}),"failed refresh reason is actually published");}
        check(phase("loader-started")&&runtime.snapshot().pid==firstPid&&preflights==1,"failed refresh preserves loader ownership and no restart");
        mode=2;ResetEvent(entered.h);check(runtime.requestBindingRefresh(12),"next physical generation may try once");
        check(WaitForSingleObject(entered.h,1000)==WAIT_OBJECT_0,"pending refresh fixture actually entered cancellation wait");
        runtime.observeSteam(0);
        check(phase("waiting-steam"),"Steam exit cancels pending refresh and returns to wait");
        check(WaitForSingleObject(first.h,1000)==WAIT_OBJECT_0,"Steam exit closes exactly the owned fixture Job");
        check(!runtime.requestBindingRefresh(13)&&preflights==1,"resume while Steam absent cannot reopen launch budget");
        runtime.observeSteam(-1);check(!runtime.requestBindingRefresh(13),"unknown Steam state fails closed");
        mode=0;runtime.observeSteam(1,true);check(phase("loader-started")&&preflights==2,"genuine new Steam session retains original linked-launch behavior");
        const DWORD secondPid=runtime.snapshot().pid;OwnedHandle second(fixtureProcess(secondPid,executable));
        check(runtime.requestBindingRefresh(13)&&calls(4),"rejected off/absent request did not consume generation");
        runtime.setEnabled(false);
        check(WaitForSingleObject(second.h,1000)==WAIT_OBJECT_0&&runtime.snapshot().pid==0,"off reaps owned fixture and releases process handles");
        check(!runtime.requestBindingRefresh(14),"resume never reenables user's switch");
        runtime.setEnabled(true);check(phase("loader-started")&&preflights==3,"explicit user enable remains the only non-session launch demand");
        const DWORD thirdPid=runtime.snapshot().pid;OwnedHandle third(fixtureProcess(thirdPid,executable));
        check(runtime.requestBindingRefresh(14)&&calls(5),"fresh live owner can use next generation");
        check(TerminateProcess(third.h,17)!=FALSE,"test terminates only its verified source-built owned fixture");
        const bool queued=runtime.requestBindingRefresh(15);(void)queued; // Exit/wake ordering may vary.
        check(phase("failed")&&runtime.snapshot().reason=="loader-exited","loader exit is reported, not silently repaired");
        check(!runtime.requestBindingRefresh(16),"failed loader cannot be restarted by rebind event");
        Sleep(40);check(preflights==3&&runtime.snapshot().pid==0,"exit/rebind overlap cannot turn refresh into a new launch");
        runtime.shutdown();check(runtime.snapshot().phase=="disabled"&&runtime.snapshot().pid==0,"final shutdown leaves no owned process");
        std::cout<<"Lifecycle owned-fixture tests passed: "<<checks<<"; realSteam=0; hardware=0; userConfig=0\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}



