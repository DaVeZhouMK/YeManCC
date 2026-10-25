#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <new>
#include <string>
#include <utility>

// Launch leaf only. No process creation, path selection, device/Host work,
// waits, retries or authority over another owner's job. Caller serializes use.
namespace ymcc::hidHideCli {
class JobGuard final {
    // Attribute values must remain at the same address until the list dies.
    // This allocation stays put across moves, including a pending-child handoff.
    struct State {
        HANDLE job=nullptr;
        STARTUPINFOEXW startup{};
        bool attributesInitialized=false;
    };
    State* state_=nullptr;
    DWORD error_=ERROR_SUCCESS;

    static void release(State* state) noexcept {
        if(!state)return;
        if(state->attributesInitialized)DeleteProcThreadAttributeList(state->startup.lpAttributeList);
        if(state->startup.lpAttributeList)HeapFree(GetProcessHeap(),0,state->startup.lpAttributeList);
        // Close only this successfully created, never-adopted job. Closing the
        // last handle enforces the configured child/subtree lifetime boundary.
        if(state->job)CloseHandle(state->job);
        state->~State();HeapFree(GetProcessHeap(),0,state);
    }
    bool fail(State* candidate,DWORD error) noexcept {
        release(candidate);error_=error?error:ERROR_GEN_FAILURE;return false;
    }
public:
    JobGuard() noexcept=default;
    ~JobGuard() noexcept {reset();}
    JobGuard(const JobGuard&)=delete;
    JobGuard& operator=(const JobGuard&)=delete;
    JobGuard(JobGuard&& other) noexcept:
        state_(std::exchange(other.state_,nullptr)),error_(std::exchange(other.error_,ERROR_SUCCESS)){}
    JobGuard& operator=(JobGuard&& other) noexcept {
        if(this!=&other){reset();state_=std::exchange(other.state_,nullptr);error_=std::exchange(other.error_,ERROR_SUCCESS);}
        return *this;
    }

    bool prepare(const STARTUPINFOW& ordinary,const std::wstring& jobName={}) noexcept {
        // Never silently tear down a possibly live ownership scope on reprepare.
        if(state_){error_=ERROR_BUSY;return false;}
        if(ordinary.cb!=sizeof(STARTUPINFOW)||jobName.size()>=MAX_PATH||jobName.find(L'\0')!=std::wstring::npos){
            error_=ERROR_INVALID_PARAMETER;return false;
        }
        auto* memory=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(State));
        if(!memory){error_=ERROR_NOT_ENOUGH_MEMORY;return false;}
        auto* candidate=new(memory) State{};
        candidate->startup.StartupInfo=ordinary;
        candidate->startup.StartupInfo.cb=sizeof(STARTUPINFOEXW);
        SetLastError(ERROR_SUCCESS);
        candidate->job=CreateJobObjectW(nullptr,jobName.empty()?nullptr:jobName.c_str());
        const DWORD createdError=GetLastError();
        if(!candidate->job)return fail(candidate,createdError);
        if(createdError==ERROR_ALREADY_EXISTS)return fail(candidate,ERROR_ALREADY_EXISTS);
        DWORD handleFlags=0;
        if(!GetHandleInformation(candidate->job,&handleFlags))return fail(candidate,GetLastError());
        if(handleFlags&HANDLE_FLAG_INHERIT)return fail(candidate,ERROR_INVALID_HANDLE);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(candidate->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return fail(candidate,GetLastError());
        SIZE_T bytes=0;SetLastError(ERROR_SUCCESS);
        const BOOL sized=InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
        const DWORD sizeError=GetLastError();
        if(sized||sizeError!=ERROR_INSUFFICIENT_BUFFER||!bytes)return fail(candidate,sizeError);
        candidate->startup.lpAttributeList=static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(),0,bytes));
        if(!candidate->startup.lpAttributeList)return fail(candidate,ERROR_NOT_ENOUGH_MEMORY);
        if(!InitializeProcThreadAttributeList(candidate->startup.lpAttributeList,1,0,&bytes))return fail(candidate,GetLastError());
        candidate->attributesInitialized=true;
        if(!UpdateProcThreadAttribute(candidate->startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,&candidate->job,sizeof(HANDLE),nullptr,nullptr))return fail(candidate,GetLastError());
        state_=candidate;error_=ERROR_SUCCESS;return true;
    }
    bool ready() const noexcept {return state_&&state_->job&&state_->attributesInitialized;}
    STARTUPINFOW* startup() noexcept {return ready()?&state_->startup.StartupInfo:nullptr;}
    DWORD flags() const noexcept {return ready()?EXTENDED_STARTUPINFO_PRESENT:0;}
    DWORD error() const noexcept {return error_;}
    // Acceptance is not exit or idle proof. Never wait or discard ownership.
    bool requestStop(UINT exitCode=1) noexcept {return ready()&&TerminateJobObject(state_->job,exitCode)!=FALSE;}
    // A root Wait(0) cannot establish subtree quiescence. Query failure and an
    // absent/unprepared job are unknown, never an idle proof. No waiting here.
    bool idle() const noexcept {
        if(!ready())return false;
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        return QueryInformationJobObject(state_->job,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr)!=FALSE&&accounting.ActiveProcesses==0;
    }
    // Release attributes before the owned job. Caller keeps this guard through
    // unknown child lifetime; moving it does not duplicate or lose the handle.
    void reset() noexcept {auto* state=std::exchange(state_,nullptr);release(state);error_=ERROR_SUCCESS;}
};
} // namespace ymcc::hidHideCli
