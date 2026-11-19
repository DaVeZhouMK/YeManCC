#pragma once
#include "steam_source_lease_transfer.h"
#include "sleep_power_remote_provider.h"
#include "sleep_power_native_task.h"
#include <mutex>
#include <new>

// This owns only the original media launch's exact Job and its source copies.
// The same ledger survives a failed drain. No polling thread or retry producer.
namespace ymcc::media::downloads {
class SourceJob final {
    struct State {
        HANDLE job=nullptr,port=nullptr,process=nullptr,thread=nullptr;
        bool assigned=false,returned=false;
        std::vector<steaminfo::transfer::Handle> members;
        std::unique_ptr<steaminfo::transfer::Export> source;
        sleeppower::remote::NativeOwnedTaskBorrow power;
        sleeppower::task::Transfer transfer;
        State* next=nullptr;
        ~State(){members.clear();if(thread)CloseHandle(thread);if(process)CloseHandle(process);if(port)CloseHandle(port);if(job)CloseHandle(job);source.reset();}
    };
    State* state_=nullptr;
    inline static std::mutex retainedMutex_;
    inline static State* retained_=nullptr;
    static bool zero(State& s)noexcept{
        if(!s.assigned)return true;
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION value{};
        if(QueryInformationJobObject(s.job,JobObjectBasicAccountingInformation,&value,sizeof value,nullptr))
            return value.ActiveProcesses==0;
        JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION alternate{};
        return QueryInformationJobObject(s.job,JobObjectBasicAndIoAccountingInformation,&alternate,sizeof alternate,nullptr)&&
            alternate.BasicInfo.ActiveProcesses==0;
    }
    static bool captureMembers(State& s)noexcept{
        if(!s.assigned)return true;
        try{
            std::array<unsigned char,sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST)+64*sizeof(ULONG_PTR)> bytes{};
            auto* list=reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(bytes.data());
            if(!QueryInformationJobObject(s.job,JobObjectBasicProcessIdList,list,static_cast<DWORD>(bytes.size()),nullptr)||
               list->NumberOfAssignedProcesses>64||list->NumberOfProcessIdsInList!=list->NumberOfAssignedProcesses)return false;
            s.members.reserve(s.members.size()+list->NumberOfProcessIdsInList);
            for(DWORD i=0;i<list->NumberOfProcessIdsInList;++i){
                steaminfo::transfer::Handle member(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(list->ProcessIdList[i])));
                if(!member.value){if(GetLastError()==ERROR_INVALID_PARAMETER)continue;return false;}
                BOOL belongs=FALSE;if(!IsProcessInJob(member.value,s.job,&belongs))return false;
                // A returned PID may already have been reused by an unrelated task.
                if(!belongs)continue;
                bool duplicate=false;for(const auto& prior:s.members)if(CompareObjectHandles(prior.value,member.value))duplicate=true;
                if(!duplicate)s.members.push_back(std::move(member));
            }return true;
        }catch(...){return false;}
    }
    static bool drain(State& s,DWORD budget)noexcept{
        const auto until=GetTickCount64()+budget;
        bool captured=captureMembers(s);
        const auto returned=[&]{if(!captured||!zero(s)||(s.process&&WaitForSingleObject(s.process,0)!=WAIT_OBJECT_0))return false;
            for(const auto& member:s.members)if(WaitForSingleObject(member.value,0)!=WAIT_OBJECT_0)return false;return true;};
        if(returned())return true;
        // Observation failure cannot veto stopping this exact owned Job.
        // Keep the same source/ledger until observation and waits do return.
        if(s.assigned){if(!TerminateJobObject(s.job,ERROR_CANCELLED))return false;}
        else if(s.process&&!TerminateProcess(s.process,ERROR_CANCELLED)&&WaitForSingleObject(s.process,0)!=WAIT_OBJECT_0)return false;
        captured=captureMembers(s);
        do{
            if(returned())return true;
            const auto now=GetTickCount64();if(now>=until)return false;
            HANDLE waiting=nullptr;
            if(!s.assigned||zero(s)){
                for(const auto& member:s.members)if(WaitForSingleObject(member.value,0)==WAIT_TIMEOUT){waiting=member.value;break;}
                if(!waiting&&s.process&&WaitForSingleObject(s.process,0)==WAIT_TIMEOUT)waiting=s.process;
            }
            if(waiting)WaitForSingleObject(waiting,static_cast<DWORD>(until-now));
            else{DWORD bytes=0;ULONG_PTR key=0;LPOVERLAPPED item=nullptr;
                GetQueuedCompletionStatus(s.port,&bytes,&key,&item,static_cast<DWORD>(until-now));}
            if(!captured)captured=captureMembers(s);
        }while(true);
    }
    static bool closeOwned(State& s)noexcept{
        // A signalled process and Job zero do not prove that our handles were
        // returned. Keep each exact failed close and the same source ledger.
        const auto close=[](HANDLE& handle)noexcept{
            if(!handle)return true;
            if(!CloseHandle(handle))return false;
            handle=nullptr;return true;
        };
        bool closed=true;
        for(auto& member:s.members)if(!close(member.value))closed=false;
        if(!close(s.thread))closed=false;
        if(!close(s.process))closed=false;
        if(!close(s.port))closed=false;
        if(!close(s.job))closed=false;
        return closed;
    }
    static bool finalize(State& s,DWORD budget)noexcept{
        s.power.cancel();
        s.transfer.beginClose();if(s.source)s.source->beginClose();
        // After observation succeeds, a partial close can remove the Job or
        // completion port. Reaping then retries only outstanding handle return.
        if(!s.returned){if(!drain(s,budget))return false;s.returned=true;}
        if(s.source)s.source->beginClose();
        if(!closeOwned(s))return false;
        if(s.source&&!s.source->closeOwned())return false;
        if(!s.transfer.closeOwned())return false;
        return s.power.closeOwned();
    }
    static void retain(State* s)noexcept{std::lock_guard lock(retainedMutex_);s->next=retained_;retained_=s;}
    static void abandon(State* s)noexcept{if(finalize(*s,0))delete s;else retain(s);}
public:
    explicit SourceJob(std::unique_ptr<steaminfo::transfer::Export> source):state_(new State){
        state_->source=std::move(source);
        state_->job=CreateJobObjectW(nullptr,nullptr);
        state_->port=CreateIoCompletionPort(INVALID_HANDLE_VALUE,nullptr,0,1);
        if(!state_->job||!state_->port){abandon(std::exchange(state_,nullptr));throw std::runtime_error("download-job-unavailable");}
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT port{};port.CompletionKey=state_;port.CompletionPort=state_->port;
        if(!SetInformationJobObject(state_->job,JobObjectExtendedLimitInformation,&limits,sizeof limits)||
           !SetInformationJobObject(state_->job,JobObjectAssociateCompletionPortInformation,&port,sizeof port)){
            abandon(std::exchange(state_,nullptr));throw std::runtime_error("download-job-unavailable");
        }
    }
    ~SourceJob(){if(!state_)return;if(finalize(*state_,2000))delete state_;else retain(state_);}
    SourceJob(const SourceJob&)=delete;SourceJob& operator=(const SourceJob&)=delete;
    HANDLE get()const noexcept{return state_->job;}
    sleeppower::remote::NativeOwnedTaskBorrow& power()noexcept{return state_->power;}
    sleeppower::task::Transfer& transfer()noexcept{return state_->transfer;}
    steaminfo::transfer::Export* source()const noexcept{return state_->source.get();}
    // Transfer this call's fresh suspended PI immediately, including assignment
    // failure. The caller may retain the numeric PI as a borrowed query view.
    bool assign(const PROCESS_INFORMATION& child)noexcept{if(state_->process)return false;state_->process=child.hProcess;state_->thread=child.hThread;
        if(!AssignProcessToJobObject(state_->job,child.hProcess))return false;state_->assigned=true;return true;}
    static unsigned reap(DWORD budget=0)noexcept{
        State* list=nullptr;{std::lock_guard lock(retainedMutex_);std::swap(list,retained_);}
        const auto until=GetTickCount64()+budget;
        while(list){State* current=list;list=list->next;current->next=nullptr;
            const auto now=GetTickCount64();if(finalize(*current,now<until?static_cast<DWORD>(until-now):0))delete current;else retain(current);}
        std::lock_guard lock(retainedMutex_);unsigned count=0;for(auto* s=retained_;s;s=s->next)++count;return count;
    }
};
} // namespace ymcc::media::downloads
