#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <new>
#include <string>
#include <utility>
#include <vector>

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
        bool tracked=false;
        std::wstring name;
        std::vector<HANDLE> inherited; // attribute values stay alive with State
    };
    State* state_=nullptr;
    DWORD error_=ERROR_SUCCESS;

    static void release(State* state) noexcept {
        if(!state)return;
        if(state->attributesInitialized)DeleteProcThreadAttributeList(state->startup.lpAttributeList);
        if(state->startup.lpAttributeList)HeapFree(GetProcessHeap(),0,state->startup.lpAttributeList);
        // Close only this successfully created, never-adopted job. Closing the
        // last handle enforces kill-on-close only for the legacy policy.
        // Tracked helpers retain their inherited owner-file lease independently.
        if(state->job)CloseHandle(state->job);
        state->~State();HeapFree(GetProcessHeap(),0,state);
    }
    bool fail(State* candidate,DWORD error) noexcept {
        release(candidate);error_=error?error:ERROR_GEN_FAILURE;return false;
    }
    bool prepareImpl(const STARTUPINFOW& ordinary,const std::wstring& jobName,
                     const std::vector<HANDLE>& handles,bool tracked) noexcept;
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
        return prepareImpl(ordinary,jobName,{},false);
    }
    // Tracking is named and explicit. It does not kill a helper that may be
    // returning an acknowledged device-disable after its parent has exited.
    // A caller must durably reserve this exact name BEFORE CreateProcess.
    bool prepareTracked(const STARTUPINFOW& ordinary,const std::wstring& jobName,
                        const std::vector<HANDLE>& inheritedHandles={}) noexcept {
        return prepareImpl(ordinary,jobName,inheritedHandles,true);
    }
    bool ready() const noexcept {return state_&&state_->job&&state_->attributesInitialized;}
    STARTUPINFOW* startup() noexcept {return ready()?&state_->startup.StartupInfo:nullptr;}
    DWORD flags() const noexcept {return ready()?EXTENDED_STARTUPINFO_PRESENT:0;}
    DWORD error() const noexcept {return error_;}
    bool tracksExactLease(const std::wstring& name,HANDLE lease) const noexcept {
        if(!ready() || !state_->tracked || state_->name!=name)return false;
        for(const auto handle:state_->inherited)if(handle==lease)return true;
        return false;
    }
    // Acceptance is not exit or idle proof. Never wait or discard ownership.
    bool requestStop(UINT exitCode=1) noexcept {return ready()&&!state_->tracked&&TerminateJobObject(state_->job,exitCode)!=FALSE;}
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

inline bool JobGuard::prepareImpl(const STARTUPINFOW& ordinary,const std::wstring& jobName,
                                  const std::vector<HANDLE>& handles,bool tracked) noexcept {
        // Never silently tear down a possibly live ownership scope on reprepare.
        if(state_){error_=ERROR_BUSY;return false;}
        if(ordinary.cb!=sizeof(STARTUPINFOW)||jobName.size()>=MAX_PATH||jobName.find(L'\0')!=std::wstring::npos||
            (tracked && jobName.empty()) || handles.size()>16){
            error_=ERROR_INVALID_PARAMETER;return false;
        }
        auto* memory=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(State));
        if(!memory){error_=ERROR_NOT_ENOUGH_MEMORY;return false;}
        auto* candidate=new(memory) State{};
        candidate->tracked=tracked;
        try { candidate->inherited=handles; candidate->name=jobName; } catch(...) { return fail(candidate,ERROR_NOT_ENOUGH_MEMORY); }
        for(size_t index=0;index<candidate->inherited.size();++index){
            const auto handle=candidate->inherited[index];DWORD flags=0;
            if(!handle||handle==INVALID_HANDLE_VALUE||!GetHandleInformation(handle,&flags)||!(flags&HANDLE_FLAG_INHERIT))
                return fail(candidate,ERROR_INVALID_HANDLE);
            for(size_t previous=0;previous<index;++previous)
                if(candidate->inherited[previous]==handle)return fail(candidate,ERROR_INVALID_PARAMETER);
        }
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
        if(tracked)limits.BasicLimitInformation.LimitFlags=0; // natural return may need to finish after parent exit
        if(!SetInformationJobObject(candidate->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return fail(candidate,GetLastError());
        SIZE_T bytes=0;SetLastError(ERROR_SUCCESS);
        const DWORD count=candidate->inherited.empty()?1:2;
        const BOOL sized=InitializeProcThreadAttributeList(nullptr,count,0,&bytes);
        const DWORD sizeError=GetLastError();
        if(sized||sizeError!=ERROR_INSUFFICIENT_BUFFER||!bytes)return fail(candidate,sizeError);
        candidate->startup.lpAttributeList=static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(),0,bytes));
        if(!candidate->startup.lpAttributeList)return fail(candidate,ERROR_NOT_ENOUGH_MEMORY);
        if(!InitializeProcThreadAttributeList(candidate->startup.lpAttributeList,count,0,&bytes))return fail(candidate,GetLastError());
        candidate->attributesInitialized=true;
        if(!UpdateProcThreadAttribute(candidate->startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,&candidate->job,sizeof(HANDLE),nullptr,nullptr))return fail(candidate,GetLastError());
        if(!candidate->inherited.empty() && !UpdateProcThreadAttribute(candidate->startup.lpAttributeList,0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,candidate->inherited.data(),candidate->inherited.size()*sizeof(HANDLE),nullptr,nullptr))
            return fail(candidate,GetLastError());
        state_=candidate;error_=ERROR_SUCCESS;return true;
    }

} // namespace ymcc::hidHideCli
