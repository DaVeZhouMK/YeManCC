#pragma once
// R41 isolated Win32 adapter. Not included by Native or InputHost production.
// Contract: asynchronous I/O handles; Connect uses a PIPE_WAIT instance.
// PIPE_NOWAIT Connect TRUE is not connection-completion proof.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <exception>
#include <utility>
#include "input_pipe_owned_operation_model.h"
namespace ymcc::testing::pipeWin32 {
namespace own=ymcc::testing::pipeOwnership;
// Preserve a pre-issue Win32 failure before exception construction/cleanup.
class ApiError:public std::runtime_error {
    DWORD code_;
public:
    ApiError(const char* api,DWORD code):std::runtime_error(std::string(api)+":"+std::to_string(code)),code_(code){}
    DWORD code()const noexcept{return code_;}
};
struct HandleStats {unsigned live=0,created=0,closed=0,closeFailures=0;};
class Handle {
    HANDLE value_=nullptr;
    std::shared_ptr<HandleStats> stats_;
public:
    Handle()=default;
    explicit Handle(HANDLE h,std::shared_ptr<HandleStats> stats={}):value_(h),stats_(std::move(stats)){
        if(valid()&&stats_){++stats_->live;++stats_->created;}
    }
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other)noexcept:value_(std::exchange(other.value_,nullptr)),stats_(std::move(other.stats_)){}
    Handle& operator=(Handle&& other)noexcept{if(this!=&other){reset();value_=std::exchange(other.value_,nullptr);stats_=std::move(other.stats_);}return *this;}
    ~Handle(){reset();}
    bool valid()const noexcept{return value_&&value_!=INVALID_HANDLE_VALUE;}
    HANDLE get()const noexcept{return value_;}
    void reset()noexcept{
        if(valid()){
            const BOOL closed=CloseHandle(value_);
            if(stats_){if(closed){--stats_->live;++stats_->closed;}else ++stats_->closeFailures;}
        }
        value_=nullptr;stats_.reset();
    }
};
inline Handle duplicatePipe(HANDLE source,const std::shared_ptr<HandleStats>& stats){
    HANDLE copy=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),source,GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_SAME_ACCESS))
        throw ApiError("DuplicateHandle",GetLastError());
    return Handle(copy,stats);
}
// Raw query failure is not necessarily an operation failure. Infrastructure
// errors and unknown errors remain Unavailable, retaining the operation pin.
inline own::Poll classify(bool querySuccess,DWORD queryError,DWORD privateEventWait)noexcept{
    if(querySuccess)return own::Poll::Terminal;
    if(queryError==ERROR_IO_INCOMPLETE)return own::Poll::Pending;
    if(queryError==ERROR_OPERATION_ABORTED)return own::Poll::Terminal;
    const bool knownOperationError=queryError==ERROR_BROKEN_PIPE||queryError==ERROR_PIPE_NOT_CONNECTED||
        queryError==ERROR_NO_DATA||queryError==ERROR_MORE_DATA;
    return knownOperationError&&privateEventWait==WAIT_OBJECT_0?own::Poll::Terminal:own::Poll::Unavailable;
}
struct ByteCount {
    bool applicable=false,known=false;DWORD count=0;
};
inline ByteCount classifyBytes(own::Kind kind,bool querySuccess,DWORD queryError,DWORD transferred,DWORD capacity)noexcept{
    if(kind==own::Kind::Connect)return {};
    const bool valid=transferred<=capacity&&(querySuccess||(kind==own::Kind::Read&&queryError==ERROR_MORE_DATA));
    return {true,valid,valid?transferred:0};
}
struct Summary {
    bool issued=false,apiReturnedTrue=false,pendingAtIssue=false,terminal=false,destroyed=false;
    DWORD initialError=ERROR_SUCCESS,completionError=ERROR_SUCCESS,bytes=0;
    bool byteCountApplicable=false,byteCountKnown=false,byteQueryAttempted=false,byteQueryReturnedTrue=false;
    DWORD byteQueryError=ERROR_SUCCESS;
    DWORD lastPollError=ERROR_SUCCESS,lastEventWait=WAIT_TIMEOUT;
    bool lastQueryReturnedTrue=false,terminalFromApiReturn=false;
    BOOL cancelReturned=FALSE;DWORD cancelError=ERROR_SUCCESS;unsigned polls=0,cancels=0;
    std::uintptr_t pipeIdentity=0,overlappedIdentity=0,bufferIdentity=0,eventIdentity=0;
};
class Backend {
    struct PipeLease {Handle handle;explicit PipeLease(Handle value):handle(std::move(value)){};};
    struct Record {
        own::Kind kind=own::Kind::Read;
        const own::OverlappedStorage* ownerIdentity=nullptr;
        unsigned char* buffer=nullptr;DWORD length=0;
        std::shared_ptr<PipeLease> pipe;
        Handle event;
        OVERLAPPED ov{};
        Summary summary;
    };
    std::shared_ptr<HandleStats> stats_;
    std::shared_ptr<PipeLease> pipe_;
    std::map<std::uint64_t,std::unique_ptr<Record>> records_;
    std::array<Summary,128> history_{};
    std::uint64_t nextId_=0;
    void observeBytes(Record& r,bool success,DWORD error,DWORD transferred)noexcept{
        r.summary.byteQueryAttempted=true;r.summary.byteQueryReturnedTrue=success;r.summary.byteQueryError=error;
        const auto bytes=classifyBytes(r.kind,success,error,transferred,r.length);
        r.summary.byteCountApplicable=bytes.applicable;r.summary.byteCountKnown=bytes.known;r.summary.bytes=bytes.count;
    }
    own::Poll query(Record& r)noexcept{
        if(r.summary.terminal)return own::Poll::Terminal;
        if(!r.summary.issued)return own::Poll::Unavailable;
        DWORD transferred=0;const BOOL ok=GetOverlappedResult(r.pipe->handle.get(),&r.ov,&transferred,FALSE);
        const DWORD error=ok?ERROR_SUCCESS:GetLastError();
        const DWORD observed=WaitForSingleObject(r.event.get(),0);
        r.summary.lastPollError=error;r.summary.lastEventWait=observed;r.summary.lastQueryReturnedTrue=ok!=FALSE;++r.summary.polls;
        const auto classification=classify(ok!=FALSE,error,observed);
        if(classification==own::Poll::Terminal){r.summary.terminal=true;r.summary.completionError=error;observeBytes(r,ok!=FALSE,error,transferred);}
        return classification;
    }
public:
    explicit Backend(HANDLE source,std::shared_ptr<HandleStats> stats):stats_(std::move(stats)),
        pipe_(std::make_shared<PipeLease>(duplicatePipe(source,stats_))){}
    Backend(const Backend&)=delete;Backend& operator=(const Backend&)=delete;
    std::uint64_t create(own::Kind kind,const own::OverlappedStorage* identity,unsigned char* bytes,std::size_t size){
        if(size>std::numeric_limits<DWORD>::max()||nextId_>=history_.size())throw std::length_error("R41 fixture operation limit");
        auto record=std::make_unique<Record>();record->kind=kind;record->ownerIdentity=identity;record->buffer=bytes;record->length=static_cast<DWORD>(size);record->pipe=pipe_;
        record->summary.byteCountApplicable=kind!=own::Kind::Connect;
        record->event=Handle(CreateEventW(nullptr,TRUE,FALSE,nullptr),stats_);
        if(!record->event.valid())throw ApiError("CreateEventW",GetLastError());
        record->ov.hEvent=record->event.get();
        record->summary.pipeIdentity=reinterpret_cast<std::uintptr_t>(record->pipe->handle.get());
        record->summary.overlappedIdentity=reinterpret_cast<std::uintptr_t>(&record->ov);
        record->summary.bufferIdentity=reinterpret_cast<std::uintptr_t>(bytes);
        record->summary.eventIdentity=reinterpret_cast<std::uintptr_t>(record->event.get());
        const auto id=nextId_+1;records_.emplace(id,std::move(record));nextId_=id;return id;
    }
    own::Issue issue(std::uint64_t id)noexcept{
        auto it=records_.find(id);if(it==records_.end())return own::Issue::Rejected;auto& r=*it->second;r.summary.issued=true;
        const BOOL ok=r.kind==own::Kind::Connect?ConnectNamedPipe(r.pipe->handle.get(),&r.ov):
            r.kind==own::Kind::Write?WriteFile(r.pipe->handle.get(),r.buffer,r.length,nullptr,&r.ov):
                ReadFile(r.pipe->handle.get(),r.buffer,r.length,nullptr,&r.ov);
        const DWORD error=ok?ERROR_SUCCESS:GetLastError();r.summary.apiReturnedTrue=ok!=FALSE;r.summary.initialError=error;
        if(!ok&&error==ERROR_IO_PENDING){r.summary.pendingAtIssue=true;return own::Issue::Pending;}
        if(r.kind==own::Kind::Connect&&!ok&&error==ERROR_PIPE_CONNECTED){
            r.summary.terminal=true;r.summary.completionError=ERROR_SUCCESS;return own::Issue::Completed;
        }
        if(!ok){
            // Not ERROR_IO_PENDING: no outstanding operation from this issue.
            // ERROR_MORE_DATA can be a completed partial message, not "no I/O".
            r.summary.terminal=true;r.summary.completionError=error;
            if(error==ERROR_MORE_DATA){
                DWORD n=0;const BOOL queryOk=GetOverlappedResult(r.pipe->handle.get(),&r.ov,&n,FALSE);
                const DWORD queryError=queryOk?ERROR_SUCCESS:GetLastError();observeBytes(r,queryOk!=FALSE,queryError,n);
            }
            return own::Issue::Rejected;
        }
        // TRUE from the issuing API is completion proof in its own right.
        // An additional nonblocking byte query must not revoke that fact.
        // Keep query status separately; a failed/contradictory query supplies
        // no successful byte count for this inline-success operation.
        (void)query(r);
        r.summary.terminal=true;r.summary.terminalFromApiReturn=true;
        r.summary.completionError=ERROR_SUCCESS;
        if(!r.summary.lastQueryReturnedTrue){r.summary.byteCountKnown=false;r.summary.bytes=0;}
        return own::Issue::Completed;
    }
    void cancel(std::uint64_t id)noexcept{
        auto it=records_.find(id);if(it==records_.end())return;auto& r=*it->second;
        const BOOL ok=CancelIoEx(r.pipe->handle.get(),&r.ov);const DWORD error=ok?ERROR_SUCCESS:GetLastError();
        r.summary.cancelReturned=ok;r.summary.cancelError=error;++r.summary.cancels;
        // Never mark terminal from a cancellation request's return value.
    }
    own::Poll poll(std::uint64_t id)noexcept{auto it=records_.find(id);return it==records_.end()?own::Poll::Unavailable:query(*it->second);}
    DWORD waitEvent(std::uint64_t id,DWORD timeoutMs)const noexcept{
        auto it=records_.find(id);return it==records_.end()?WAIT_FAILED:WaitForSingleObject(it->second->event.get(),timeoutMs);
    }
    Summary summary(std::uint64_t id)const{
        const auto it=records_.find(id);if(it!=records_.end())return it->second->summary;
        if(!id||id>history_.size())throw std::out_of_range("unknown operation");return history_[static_cast<std::size_t>(id-1)];
    }
    void destroy(std::uint64_t id,const own::OverlappedStorage* identity,const unsigned char* bytes,std::size_t size)noexcept{
        const auto it=records_.find(id);if(it==records_.end())return;auto& r=*it->second;
        // Destruction is allowed only unissued or terminal. A violation is an
        // adapter bug; fail fast in this isolated harness before releasing I/O memory.
        if((r.summary.issued&&!r.summary.terminal)||identity!=r.ownerIdentity||bytes!=r.buffer||size!=r.length)std::terminate();
        r.summary.destroyed=true;history_[static_cast<std::size_t>(id-1)]=r.summary;records_.erase(it);
    }
};
} // namespace ymcc::testing::pipeWin32
