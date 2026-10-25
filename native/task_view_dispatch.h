#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <algorithm>
#include <string>
#include "task_view_foreground_grant.h"

// One explicit TaskView request at a time. Only the exact process created here
// may be terminated. COM runs in the helper; no COM call is made on the UI/pool
// owner. Pending cleanup is retained instead of accumulating orphan helpers.
namespace ymcc::taskView {
inline constexpr DWORD callBudgetMs=3000,cleanupBudgetMs=500;
inline constexpr ULONGLONG requestBudgetMs=4500;
inline constexpr DWORD cancelledExit=0xE0000042u;
enum class Status { Requested,Busy,Expired,MissingHelper,JobFailed,StartFailed,
    AssignFailed,ForegroundGrantFailed,ResumeFailed,ReadyFailed,ReleaseFailed,WaitFailed,TimedOut,ExitQueryFailed,ApiFailed,CleanupUnproven };
inline const char* statusName(Status s) noexcept {
    switch(s){
    case Status::Requested:return "requested";case Status::Busy:return "busy";
    case Status::Expired:return "request-expired";case Status::MissingHelper:return "helper-missing";
    case Status::JobFailed:return "job-failed";case Status::StartFailed:return "start-failed";
    case Status::AssignFailed:return "job-assignment-failed";
    case Status::ForegroundGrantFailed:return "foreground-grant-failed";case Status::ResumeFailed:return "resume-failed";
    case Status::ReadyFailed:return "helper-ready-failed";case Status::ReleaseFailed:return "helper-release-failed";
    case Status::WaitFailed:return "wait-failed";case Status::TimedOut:return "timed-out-outcome-unknown";
    case Status::ExitQueryFailed:return "exit-query-failed";case Status::ApiFailed:return "api-rejected";
    case Status::CleanupUnproven:return "cleanup-unproven";
    }return "unknown";
}
struct Result {
    Status status=Status::StartFailed;
    DWORD errorHint=0,helperExitCode=0,elapsedMs=0,foregroundGrantError=0;
    DWORD helperPid=0;
    bool helperExitKnown=false,helperStarted=false,helperResumed=false,helperReady=false,executionReleased=false,waitTimedOut=false,foregroundGrantAttempted=false,foregroundGrantAccepted=false;
    bool terminationAttempted=false,terminationAccepted=false,cleanupVerified=false,cleanupRetained=false;
};
struct WindowsOps {
    HWND foregroundOwner = nullptr;
    foreground::DeliveryReceipt foregroundReceipt;
    HANDLE readyEvent=nullptr,executeEvent=nullptr;
    ~WindowsOps()noexcept{finishHandshake();}
    void finishHandshake()noexcept{
        const DWORD savedError=GetLastError();
        if(readyEvent)CloseHandle(readyEvent);
        if(executeEvent)CloseHandle(executeEvent);
        readyEvent=executeEvent=nullptr;
        SetLastError(savedError);
    }
    DWORD ready(HANDLE process,DWORD ms)const noexcept{
        const HANDLE handles[]{readyEvent,process};
        return WaitForMultipleObjects(2,handles,FALSE,ms);
    }
    bool release()const noexcept{return SetEvent(executeEvent)!=FALSE;}
    ULONGLONG now()const noexcept{return GetTickCount64();}
    DWORD error()const noexcept{return GetLastError();}
    DWORD attributes(const wchar_t* p)const noexcept{return GetFileAttributesW(p);}
    HANDLE createJob()const noexcept{return CreateJobObjectW(nullptr,nullptr);}
    bool configureJob(HANDLE job)const noexcept{
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        // Only the helper remains owned; any Shell-created descendant must not
        // become a target of this helper's timeout cleanup.
        info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
        return SetInformationJobObject(job,JobObjectExtendedLimitInformation,&info,sizeof(info))!=FALSE;
    }
    bool start(const wchar_t* helper,wchar_t* command,PROCESS_INFORMATION& pi)noexcept{
        finishHandshake();
        try {
            static std::atomic<ULONGLONG> serial{0};
            const auto prefix=L"Local\\YeManTaskView-"+std::to_wstring(GetCurrentProcessId())+L"-"+
                std::to_wstring(now())+L"-"+std::to_wstring(serial.fetch_add(1,std::memory_order_relaxed));
            const auto readyName=prefix+L"-ready",executeName=prefix+L"-execute";
            // Never reuse a pre-existing named object or inherit unrelated handles.
            readyEvent=CreateEventW(nullptr,TRUE,FALSE,readyName.c_str());
            if(!readyEvent||GetLastError()==ERROR_ALREADY_EXISTS){const DWORD error=GetLastError();finishHandshake();SetLastError(error);return false;}
            executeEvent=CreateEventW(nullptr,TRUE,FALSE,executeName.c_str());
            if(!executeEvent||GetLastError()==ERROR_ALREADY_EXISTS){const DWORD error=GetLastError();finishHandshake();SetLastError(error);return false;}
            const std::wstring guarded=std::wstring(command)+L" "+readyName+L" "+executeName;
            std::wstring mutableCommand=guarded;
            STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
            return CreateProcessW(helper,mutableCommand.data(),nullptr,nullptr,FALSE,
                CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE;
        }catch(...){finishHandshake();SetLastError(ERROR_NOT_ENOUGH_MEMORY);return false;}
    }
    bool assign(HANDLE job,HANDLE process)const noexcept{return AssignProcessToJobObject(job,process)!=FALSE;}
    bool allowForeground(DWORD pid) noexcept {
        if (foregroundOwner) return foreground::request(foregroundOwner,pid,&foregroundReceipt);
        SetLastError(ERROR_SUCCESS);
        const bool accepted = AllowSetForegroundWindow(pid)!=FALSE;
        const auto error = accepted ? ERROR_SUCCESS : GetLastError();
        foregroundReceipt = {accepted ? foreground::DeliveryStage::ApiAccepted : (error ? foreground::DeliveryStage::ApiRejected : foreground::DeliveryStage::ApiRejectedNoError),
            accepted ? ERROR_SUCCESS : (error ? error : ERROR_ACCESS_DENIED)};
        SetLastError(foregroundReceipt.error);
        return accepted;
    }
    bool resume(HANDLE thread)const noexcept{return ResumeThread(thread)!=static_cast<DWORD>(-1);}
    DWORD wait(HANDLE process,DWORD ms)const noexcept{return WaitForSingleObject(process,ms);}
    bool exitCode(HANDLE process,DWORD& code)const noexcept{return GetExitCodeProcess(process,&code)!=FALSE;}
    bool terminate(HANDLE process)const noexcept{return TerminateProcess(process,cancelledExit)!=FALSE;}
    void close(HANDLE h)const noexcept{if(h)CloseHandle(h);}
};
class Dispatcher {
    std::atomic_flag busy_=ATOMIC_FLAG_INIT;
    HANDLE pendingProcess_=nullptr,pendingJob_=nullptr;
    struct Lease {std::atomic_flag& flag;~Lease()noexcept{flag.clear(std::memory_order_release);}};
    template<class Ops> bool retire(Ops& ops,HANDLE process,HANDLE job,bool knownExited,Result& r) noexcept {
        if(!process){ops.close(job);r.cleanupVerified=true;return true;}
        bool exited=knownExited||ops.wait(process,0)==WAIT_OBJECT_0;
        if(!exited){
            r.terminationAttempted=true;r.terminationAccepted=ops.terminate(process);
            exited=ops.wait(process,cleanupBudgetMs)==WAIT_OBJECT_0;
        }
        if(exited){ops.close(process);ops.close(job);r.cleanupVerified=true;return true;}
        pendingProcess_=process;pendingJob_=job;r.cleanupRetained=true;r.cleanupVerified=false;
        r.status=Status::CleanupUnproven;return false;
    }
public:
    Dispatcher()noexcept=default;
    Dispatcher(const Dispatcher&)=delete;Dispatcher& operator=(const Dispatcher&)=delete;
    // Args are prepared before acquiring the one-request lease. Ops must not
    // throw. No wait is made for another caller's lease; Busy is explicit.
    template<class Ops> Result request(Ops& ops,const wchar_t* helper,wchar_t* command,
            ULONGLONG deadline) noexcept {
        Result r;
        if(busy_.test_and_set(std::memory_order_acquire)){r.status=Status::Busy;return r;}
        Lease lease{busy_};const auto started=ops.now();
        auto finish=[&]()noexcept{ops.finishHandshake();const auto now=ops.now();r.elapsedMs=static_cast<DWORD>((std::min)(now>=started?now-started:0ULL,ULONGLONG(MAXDWORD)));return r;};
        if(pendingProcess_){
            const auto process=pendingProcess_,job=pendingJob_;pendingProcess_=pendingJob_=nullptr;
            if(!retire(ops,process,job,false,r))return finish();
            r.terminationAttempted=r.terminationAccepted=false;r.cleanupVerified=false;
        }
        if(deadline<=ops.now()){r.status=Status::Expired;return finish();}
        const auto attributes=ops.attributes(helper);
        if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY)){
            r.status=Status::MissingHelper;r.errorHint=ops.error();return finish();
        }
        const auto job=ops.createJob();
        if(!job){r.status=Status::JobFailed;r.errorHint=ops.error();return finish();}
        if(!ops.configureJob(job)){r.status=Status::JobFailed;r.errorHint=ops.error();ops.close(job);return finish();}
        if(deadline<=ops.now()){r.status=Status::Expired;ops.close(job);return finish();}
        PROCESS_INFORMATION pi{};
        if(!ops.start(helper,command,pi)){r.status=Status::StartFailed;r.errorHint=ops.error();ops.close(job);return finish();}
        r.helperStarted=true;r.helperPid=pi.dwProcessId;
        auto failChild=[&](Status status)noexcept{
            r.status=status;r.errorHint=ops.error();ops.close(pi.hThread);pi.hThread=nullptr;
            retire(ops,pi.hProcess,job,false,r);return finish();
        };
        if(!ops.assign(job,pi.hProcess))return failChild(Status::AssignFailed);
        if(deadline<=ops.now())return failChild(Status::Expired);
        // User32 cannot reliably grant foreground rights to a suspended,
        // uninitialized process. Resume only its bootstrap, then wait for its
        // message queue. The helper cannot call Shell until release() below.
        const auto callDeadline=(std::min)(deadline,ops.now()+ULONGLONG(callBudgetMs));
        if(!ops.resume(pi.hThread))return failChild(Status::ResumeFailed);
        r.helperResumed=true;
        ops.close(pi.hThread);pi.hThread=nullptr;
        auto remainingBudget=[&]()noexcept{
            const auto now=ops.now();
            return now>=callDeadline?DWORD(0):static_cast<DWORD>(callDeadline-now);
        };
        const DWORD ready=ops.ready(pi.hProcess,remainingBudget());
        if(ready!=WAIT_OBJECT_0){
            r.status=ready==WAIT_TIMEOUT?Status::TimedOut:Status::ReadyFailed;
            r.waitTimedOut=ready==WAIT_TIMEOUT;
            r.errorHint=ready==WAIT_TIMEOUT?ERROR_TIMEOUT:ready==WAIT_FAILED?ops.error():ERROR_PROCESS_ABORTED;
            retire(ops,pi.hProcess,job,false,r);return finish();
        }
        r.helperReady=true;
        if(!remainingBudget())return failChild(Status::Expired);
        r.foregroundGrantAttempted=true;
        r.foregroundGrantAccepted=ops.allowForeground(pi.dwProcessId);
        if(!r.foregroundGrantAccepted){
            r.foregroundGrantError=ops.error();
            r.errorHint=r.foregroundGrantError;
            r.status=Status::ForegroundGrantFailed;
            retire(ops,pi.hProcess,job,false,r);
            return finish();
        }
        if(!remainingBudget())return failChild(Status::Expired);
        if(!ops.release())return failChild(Status::ReleaseFailed);
        r.executionReleased=true;
        const DWORD remaining=remainingBudget();
        const DWORD wait=ops.wait(pi.hProcess,remaining);
        if(wait!=WAIT_OBJECT_0){
            r.status=wait==WAIT_TIMEOUT?Status::TimedOut:Status::WaitFailed;
            r.waitTimedOut=wait==WAIT_TIMEOUT;r.errorHint=wait==WAIT_TIMEOUT?ERROR_TIMEOUT:ops.error();
            retire(ops,pi.hProcess,job,false,r);return finish();
        }
        if(!ops.exitCode(pi.hProcess,r.helperExitCode)){
            r.status=Status::ExitQueryFailed;r.errorHint=ops.error();
        }else{
            r.helperExitKnown=true;r.status=r.helperExitCode==0?Status::Requested:Status::ApiFailed;
        }
        retire(ops,pi.hProcess,job,true,r);return finish();
    }
};
} // namespace ymcc::taskView
