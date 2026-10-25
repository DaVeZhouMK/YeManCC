#pragma once
#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "process_efficiency.h"

// Exactly one attempt per admitted YMCC process. Independent of wallpaper/UI
// sessions. No retry, notification, IPC, focus, controller or device ownership.
namespace ymcc::optiStartup {
namespace fs = std::filesystem;
inline constexpr DWORD delayMs=30000, workerBudgetMs=180000;
struct Handle {
    HANDLE value=nullptr;
    explicit Handle(HANDLE h=nullptr):value(h){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
inline std::wstring birth(HANDLE process){
    FILETIME c{},e{},k{},u{};if(!GetProcessTimes(process,&c,&e,&k,&u))return {};
    ULARGE_INTEGER stamp{};stamp.LowPart=c.dwLowDateTime;stamp.HighPart=c.dwHighDateTime;
    return std::to_wstring(stamp.QuadPart);
}
inline fs::path image(HANDLE process){
    wchar_t buffer[32768]{};DWORD n=static_cast<DWORD>(std::size(buffer));
    if(!QueryFullProcessImageNameW(process,0,buffer,&n)||!n)return {};
    return fs::path(std::wstring(buffer,n)).lexically_normal();
}
inline std::wstring quote(const fs::path& path){return L"\""+path.wstring()+L"\"";}
struct Limits {DWORD delay=delayMs,budget=workerBudgetMs;};
class Owner {
    struct State {
        Handle stop{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
        std::atomic<unsigned> launches{0};
        std::atomic<bool> finished{false};
        std::atomic<DWORD> childPid{0};
    };
    std::mutex gate_;
    bool attempted_=false;
    std::shared_ptr<State> state_;
    std::thread thread_;
    static void run(const std::shared_ptr<State>& state,const fs::path& executable,ULONGLONG started,Limits limits) noexcept {
        struct Finish{State& state;~Finish(){state.finished.store(true);state.childPid.store(0);}}finish{*state};
        try {
            SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN);
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_IDLE);
            const auto elapsed=GetTickCount64()-started;
            if(WaitForSingleObject(state->stop.value,elapsed>=limits.delay?0:static_cast<DWORD>(limits.delay-elapsed))!=WAIT_TIMEOUT)return;
            const auto identity=birth(GetCurrentProcess());if(identity.empty())return;
            Handle parent,signal;
            if(!DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&parent.value,
                SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,TRUE,0))return;
            if(!DuplicateHandle(GetCurrentProcess(),state->stop.value,GetCurrentProcess(),&signal.value,SYNCHRONIZE,TRUE,0))return;
            SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
            std::vector<unsigned char> storage(bytes);
            auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
            if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes))return;
            struct DeleteAttributes{LPPROC_THREAD_ATTRIBUTE_LIST list;~DeleteAttributes(){DeleteProcThreadAttributeList(list);}}cleanup{attributes};
            HANDLE inherited[]={parent.value,signal.value};
            if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof inherited,nullptr,nullptr))return;
            Handle job{CreateJobObjectW(nullptr,nullptr)};if(!job.value)return;
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION bounds{};bounds.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if(!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&bounds,sizeof bounds))return;
            std::wstring command=quote(executable)+L" --opti-startup "+
                std::to_wstring(reinterpret_cast<ULONG_PTR>(parent.value))+L" "+
                std::to_wstring(reinterpret_cast<ULONG_PTR>(signal.value))+L" "+identity;
            STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof startup;startup.StartupInfo.dwFlags=STARTF_USESHOWWINDOW;
            startup.StartupInfo.wShowWindow=SW_HIDE;startup.lpAttributeList=attributes;PROCESS_INFORMATION child{};
            if(WaitForSingleObject(state->stop.value,0)!=WAIT_TIMEOUT)return;
            // This is the sole launch attempt, including a missing/broken child.
            state->launches.fetch_add(1);
            if(!ymcc::processEfficiency::createProcess(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
                CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT|IDLE_PRIORITY_CLASS,nullptr,
                executable.parent_path().c_str(),&startup.StartupInfo,&child))return;
            Handle process{child.hProcess},thread{child.hThread};
            struct KillOwned{HANDLE process;~KillOwned(){if(WaitForSingleObject(process,0)==WAIT_TIMEOUT){TerminateProcess(process,16);WaitForSingleObject(process,2000);}}}owned{child.hProcess};
            state->childPid.store(child.dwProcessId);
            if(!AssignProcessToJobObject(job.value,process.value)||WaitForSingleObject(state->stop.value,0)!=WAIT_TIMEOUT)return;
            if(ResumeThread(thread.value)==DWORD(-1))return;
            HANDLE events[]={state->stop.value,process.value};
            if(WaitForMultipleObjects(2,events,FALSE,limits.budget)!=WAIT_OBJECT_0+1){
                SetEvent(state->stop.value);
                if(WaitForSingleObject(process.value,250)!=WAIT_OBJECT_0){TerminateJobObject(job.value,16);WaitForSingleObject(process.value,2000);}
            }
        }catch(...){/* Fail once and silently; never reschedule or notify. */}
    }
public:
    ~Owner(){stop();}
    bool start(const fs::path& executable,ULONGLONG started,Limits limits={}) noexcept {
        std::lock_guard lock(gate_);if(attempted_)return false;attempted_=true;
        try{state_=std::make_shared<State>();if(!state_->stop.value)return false;
            thread_=std::thread([state=state_,executable,started,limits]{run(state,executable,started,limits);});return true;
        }catch(...){if(state_)state_->finished.store(true);return false;}
    }
    void cancel() noexcept {std::lock_guard lock(gate_);attempted_=true;if(state_&&state_->stop.value)SetEvent(state_->stop.value);}
    void stop() noexcept {
        std::lock_guard lock(gate_);attempted_=true;if(state_&&state_->stop.value)SetEvent(state_->stop.value);
        if(thread_.joinable())thread_.join();
    }
    bool finished()const noexcept{return state_&&state_->finished.load();}
    unsigned launches()const noexcept{return state_?state_->launches.load():0;}
    DWORD childPid()const noexcept{return state_?state_->childPid.load():0;}
};
inline std::optional<int> entrypoint(const fs::path& appRoot,const std::function<void(const std::function<bool()>&)>& operation){
    int count=0;auto* args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return 10;
    struct Free{wchar_t** value;~Free(){LocalFree(value);}}free{args};
    if(count<2||std::wstring_view(args[1])!=L"--opti-startup")return std::nullopt;
    // A child failure is a silent process exit, including crash/error reporting.
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(count!=5)return 10;
    auto parse=[](const wchar_t* value)->HANDLE{
        if(!value||!*value||wcsspn(value,L"0123456789")!=wcslen(value))return nullptr;
        wchar_t* end=nullptr;const auto n=_wcstoui64(value,&end,10);
        return end&&!*end&&n&&n<=static_cast<unsigned long long>(MAXULONG_PTR)?reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(n)):nullptr;
    };
    Handle parent{parse(args[2])},cancel{parse(args[3])};
    if(!parent.value||!cancel.value||parent.value==cancel.value||GetProcessId(parent.value)==GetCurrentProcessId())return 11;
    if(WaitForSingleObject(parent.value,0)!=WAIT_TIMEOUT||WaitForSingleObject(cancel.value,0)!=WAIT_TIMEOUT||birth(parent.value)!=args[4])return 11;
    const auto expected=(appRoot.parent_path()/L"YeManCC.exe").lexically_normal(),actual=image(parent.value);
    if(actual.empty()||_wcsicmp(actual.c_str(),expected.c_str())!=0)return 11;
    const std::function<bool()> stopped=[&]{return WaitForSingleObject(parent.value,0)!=WAIT_TIMEOUT||WaitForSingleObject(cancel.value,0)!=WAIT_TIMEOUT;};
    try{if(!stopped())operation(stopped);return stopped()?12:0;}catch(...){return 15;}
}
} // namespace ymcc::optiStartup
