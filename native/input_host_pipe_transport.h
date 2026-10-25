#pragma once
// Native InputHost transport ownership. All I/O is serialized internally.
// The production owner lives until OS process teardown: do not make it a
// destructible global/TLS object while overlapped requests can still exist.
// Contract: an owned FILE_FLAG_OVERLAPPED, PIPE_WAIT message-mode server.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace ymcc::inputHost::pipeTransport {
// Matches InputHost.Program.MaxMessageBytes and the native response capacity.
inline constexpr DWORD kMessageBytes=16*1024;
inline constexpr std::size_t kPendingSlots=4;
inline constexpr std::size_t kPayloadBudget=kPendingSlots*kMessageBytes;
// Four retained generations plus the current lease. Close debt consumes the
// same fixed entries instead of growing an unbounded side list.
inline constexpr std::size_t kLeaseSlots=kPendingSlots+1;
enum class Kind {Read,Write,Connect};
using IssueGuard=bool(*)(void*) noexcept;
struct Result {
    bool ok=false,admitted=false,byteCountKnown=false,terminal=false,retired=false;
    bool apiReturnedTrue=false,queryAttempted=false,queryReturnedTrue=false;
    DWORD error=ERROR_SUCCESS,transferred=0,initialError=ERROR_SUCCESS,queryError=ERROR_SUCCESS;
    DWORD firstWait=WAIT_OBJECT_0,cancelError=ERROR_SUCCESS;
    bool cancelAttempted=false,contextRejected=false;
    std::uint64_t operation=0,lease=0;
};
struct Snapshot {
    bool available=false,sealed=false;
    std::size_t pending=0,payloadBytes=0;
    std::uint64_t admitted=0,reaped=0,lastReapedOperation=0,lastReapedLease=0;
    DWORD lastReapedError=ERROR_SUCCESS;
    unsigned closeFailures=0; // Historical failed attempts, never live debt.
    std::size_t activeLeases=0,retainedLeases=0,cachedOperations=0,retainedOperations=0;
    std::size_t ownedOriginals=0,ownedDuplicates=0,ownedEvents=0,failedCloseDebt=0;
    std::size_t operationSlots=0,residentPayloadBytes=0;
    // Valid only with available=true. Local retirement does not require seal.
    bool stopped=false;
};
class Runtime final {
    class Handle {
        HANDLE h_=nullptr;
        bool failed_=false;
    public:
        Handle()=default;
        Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
        // No handle moves out of its registered lease/operation owner.
        void attach(HANDLE h)noexcept{h_=h;failed_=false;}
        HANDLE get()const noexcept{return h_;}
        bool valid()const noexcept{return h_&&h_!=INVALID_HANDLE_VALUE;}
        bool failed()const noexcept{return valid()&&failed_;}
        bool reset()noexcept{
            if(valid()&&CloseHandle(h_)==FALSE){failed_=true;return false;}
            h_=nullptr;failed_=false;return true;
        }
    };
    struct Lease {
        Handle original,issue;
        std::wstring name;
        std::uint64_t id=0;
        bool occupied=false,retiring=false,identityKnown=false;
    };
    struct Operation {
        Lease* lease=nullptr;
        std::vector<unsigned char> bytes;
        Handle event;
        OVERLAPPED ov{};
        Kind kind=Kind::Read;
        std::uint64_t id=0;
        bool issued=false,terminal=false,accounted=false,apiTrue=false,queryAttempted=false,queryTrue=false,known=false,cancelled=false;
        DWORD initial=ERROR_SUCCESS,completion=ERROR_SUCCESS,queryError=ERROR_SUCCESS,count=0,cancelError=ERROR_SUCCESS;
        Operation(Lease* owner,Kind k,DWORD size,std::uint64_t serial)
            :lease(owner),bytes(size),kind(k),id(serial){}
    };
    std::mutex mutex_;
    std::array<Lease,kLeaseSlots> leases_{};
    Lease* active_=nullptr;
    std::array<std::unique_ptr<Operation>,kPendingSlots> operations_{};
    // One terminal same-lease cache. Failed event close stays owned here but
    // makes the cache ineligible for reuse until maintenance returns it.
    std::unique_ptr<Operation> reusable_;
    std::atomic<bool> sealed_{false};
    std::atomic<unsigned> pending_{0};
    std::size_t payload_=0;
    std::uint64_t nextLease_=0,nextOperation_=0,admitted_=0,reaped_=0,lastReaped_=0,lastReapedLease_=0;
    DWORD lastReapedError_=ERROR_SUCCESS;
    unsigned closeFailures_=0;
    // Process lifetime storage protects outstanding OVERLAPPED pointers.
    // It is not a close receipt: snapshot().stopped proves resource return.
    ~Runtime()=default;
    void closeHandle(Handle& h)noexcept{if(!h.reset())++closeFailures_;}
    static bool pendingOperation(const Operation& op)noexcept{return op.issued&&!op.terminal;}
    bool leaseHasOperations(const Lease& lease,bool pendingOnly=false)const noexcept{
        for(const auto& op:operations_)if(op&&op->lease==&lease&&(!pendingOnly||pendingOperation(*op)))return true;
        return reusable_&&reusable_->lease==&lease&&(!pendingOnly||pendingOperation(*reusable_));
    }
    void releaseOperation(std::unique_ptr<Operation>& op)noexcept{
        if(!op||pendingOperation(*op))return;
        closeHandle(op->event);
        if(!op->event.valid())op.reset();
    }
    void retireLease(Lease& lease)noexcept{
        if(!lease.occupied||!lease.retiring)return;
        closeHandle(lease.original);
        // The issuing duplicate must survive until every issued I/O is proven
        // terminal under query(), regardless of disconnect or cancel failure.
        if(!leaseHasOperations(lease,true))closeHandle(lease.issue);
        if(!lease.original.valid()&&!lease.issue.valid()&&!leaseHasOperations(lease)){
            lease.occupied=false;lease.retiring=false;lease.identityKnown=false;
            lease.name.clear();
        }
    }
    static bool terminalError(DWORD e)noexcept{
        return e==ERROR_BROKEN_PIPE||e==ERROR_PIPE_NOT_CONNECTED||e==ERROR_NO_DATA||e==ERROR_MORE_DATA;
    }
    static void observeBytes(Operation& op,bool success,DWORD error,DWORD n)noexcept{
        op.known=op.kind!=Kind::Connect&&n<=op.bytes.size()&&
            (success||(op.kind==Kind::Read&&error==ERROR_MORE_DATA));
        op.count=op.known?n:0;
    }
    static bool query(Operation& op)noexcept{
        if(op.terminal)return true;
        DWORD n=0;const BOOL ok=GetOverlappedResult(op.lease->issue.get(),&op.ov,&n,FALSE);
        const DWORD e=ok?ERROR_SUCCESS:GetLastError();
        op.queryAttempted=true;op.queryTrue=ok!=FALSE;op.queryError=e;
        const DWORD signal=WaitForSingleObject(op.event.get(),0);
        // Unknown/infrastructure errors never free a genuinely pending op.
        const bool terminal=ok||e==ERROR_OPERATION_ABORTED||(terminalError(e)&&signal==WAIT_OBJECT_0);
        if(terminal){op.terminal=true;op.completion=e;observeBytes(op,ok!=FALSE,e,n);}
        return terminal;
    }
    static void cancel(Operation& op,bool retryFailed=false)noexcept{
        if(!op.issued||op.terminal||(op.cancelled&&(!retryFailed||op.cancelError==ERROR_SUCCESS)))return;
        op.cancelled=true;const BOOL ok=CancelIoEx(op.lease->issue.get(),&op.ov);
        op.cancelError=ok?ERROR_SUCCESS:GetLastError();
    }
    static void issue(Operation& op)noexcept{
        op.issued=true;
        const BOOL ok=op.kind==Kind::Connect?ConnectNamedPipe(op.lease->issue.get(),&op.ov):
            op.kind==Kind::Write?WriteFile(op.lease->issue.get(),op.bytes.data(),static_cast<DWORD>(op.bytes.size()),nullptr,&op.ov):
                ReadFile(op.lease->issue.get(),op.bytes.data(),static_cast<DWORD>(op.bytes.size()),nullptr,&op.ov);
        const DWORD e=ok?ERROR_SUCCESS:GetLastError();op.apiTrue=ok!=FALSE;op.initial=e;
        if(!ok&&e==ERROR_IO_PENDING)return;
        if(op.kind==Kind::Connect&&!ok&&e==ERROR_PIPE_CONNECTED){op.terminal=true;op.completion=ERROR_SUCCESS;return;}
        if(!ok){
            op.terminal=true;op.completion=e;
            if(e==ERROR_MORE_DATA){
                DWORD n=0;const BOOL got=GetOverlappedResult(op.lease->issue.get(),&op.ov,&n,FALSE);
                const DWORD queryError=got?ERROR_SUCCESS:GetLastError();op.queryAttempted=true;op.queryTrue=got!=FALSE;op.queryError=queryError;
                observeBytes(op,got!=FALSE,queryError,n);
            }
            return;
        }
        // API TRUE proves successful completion. An extra count query cannot
        // revoke that fact, even if it reports an unavailable/contradictory result.
        (void)query(op);op.terminal=true;op.completion=ERROR_SUCCESS;
        if(!op.queryTrue){op.known=false;op.count=0;}
    }
    void erase(std::size_t i)noexcept{
        auto& op=operations_[i];if(!op||pendingOperation(*op))return;
        const bool completed=op->accounted;
        if(completed){
            payload_-=op->bytes.size();lastReaped_=op->id;lastReapedLease_=op->lease->id;lastReapedError_=op->completion;++reaped_;
            op->accounted=false;pending_.fetch_sub(1,std::memory_order_release);
        }
        if(completed&&!reusable_&&active_&&op->lease==active_&&!sealed_.load(std::memory_order_seq_cst)){
            reusable_=std::move(op);
            if(sealed_.load(std::memory_order_seq_cst))releaseOperation(reusable_);
        }else releaseOperation(op);
    }
    void retireActiveLocked()noexcept{
        if(!active_)return;
        auto* lease=active_;active_=nullptr;lease->retiring=true;
        for(auto& op:operations_)if(op&&op->lease==lease)cancel(*op);
        // Disconnect affects all duplicates, but supplies no terminal proof.
        (void)DisconnectNamedPipe(lease->original.get());
    }
    void sweepLocked()noexcept{
        if(sealed_.load(std::memory_order_acquire))retireActiveLocked();
        for(std::size_t i=0;i<operations_.size();++i)if(operations_[i]){
            auto& op=*operations_[i];
            if(pendingOperation(op)&&op.lease->retiring)cancel(op,true);
            if(!op.issued||query(op))erase(i);
        }
        if(reusable_&&(reusable_->event.failed()||reusable_->lease->retiring||sealed_.load(std::memory_order_acquire)))releaseOperation(reusable_);
        for(auto& lease:leases_)retireLease(lease);
    }
    bool nameAvailableLocked(const std::wstring& name,DWORD& error,const Lease* skip=nullptr)noexcept{
        if(sealed_.load(std::memory_order_acquire)){error=ERROR_SHUTDOWN_IN_PROGRESS;return false;}
        if(active_){error=ERROR_BUSY;return false;}
        bool room=false;
        for(const auto& lease:leases_){
            if(&lease==skip){room=true;continue;}
            if(!lease.occupied){room=true;continue;}
            if(!lease.identityKnown||CompareStringOrdinal(lease.name.c_str(),-1,name.c_str(),-1,TRUE)==CSTR_EQUAL){error=ERROR_PIPE_BUSY;return false;}
        }
        if(!room){error=ERROR_NOT_ENOUGH_MEMORY;return false;}
        error=ERROR_SUCCESS;return true;
    }
    void closeActiveLocked()noexcept{retireActiveLocked();sweepLocked();}
public:
    Runtime()=default;
    Runtime(const Runtime&)=delete;Runtime& operator=(const Runtime&)=delete;
    // Pending I/O only. False does not prove complete resource retirement.
    bool hasPending()const noexcept{return pending_.load(std::memory_order_acquire)!=0;}
    bool canCreate(const std::wstring& name,DWORD& error)noexcept{
        try {std::lock_guard<std::mutex> lock(mutex_);sweepLocked();return nameAvailableLocked(name,error);}
        catch(...){error=ERROR_GEN_FAILURE;return false;}
    }
    // Three-argument calls still compile. Pass ownershipTaken for explicit
    // transfer: false leaves source with its caller on capacity/identity/lock
    // rejection. Once true, EVERY later path keeps/returns source in Runtime.
    // canCreate + adopt share the caller's single-writer creation contract.
    // Arbitrary incoming handles cannot exceed this fixed ownership budget.
    bool adopt(HANDLE source,const std::wstring& name,DWORD& error)noexcept{
        return adopt(source,name,error,nullptr);
    }
    bool adopt(HANDLE source,const std::wstring& name,DWORD& error,bool* ownershipTaken)noexcept{
        if(ownershipTaken)*ownershipTaken=false;
        if(!source||source==INVALID_HANDLE_VALUE){error=ERROR_INVALID_HANDLE;return false;}
        try {
            std::lock_guard<std::mutex> lock(mutex_);sweepLocked();
            Lease* lease=nullptr;
            for(auto& entry:leases_){
                if(entry.occupied&&(entry.original.get()==source||entry.issue.get()==source)){error=ERROR_INVALID_HANDLE;return false;}
                if(!entry.occupied&&!lease)lease=&entry;
            }
            if(!lease){error=ERROR_NOT_ENOUGH_MEMORY;return false;}
            lease->occupied=true;lease->retiring=true;lease->identityKnown=false;
            lease->original.attach(source);
            if(ownershipTaken)*ownershipTaken=true;
            try {
                lease->name=name;lease->identityKnown=true;
                if(!nameAvailableLocked(name,error,lease)){retireLease(*lease);return false;}
                if(nextLease_==UINT64_MAX){error=ERROR_ARITHMETIC_OVERFLOW;retireLease(*lease);return false;}
                HANDLE duplicate=nullptr;
                if(!DuplicateHandle(GetCurrentProcess(),source,GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS)){
                    error=GetLastError();retireLease(*lease);return false;
                }
                lease->issue.attach(duplicate);lease->id=++nextLease_;
                lease->retiring=false;active_=lease;error=ERROR_SUCCESS;return true;
            }catch(const std::bad_alloc&){error=ERROR_NOT_ENOUGH_MEMORY;}
            catch(...){error=ERROR_GEN_FAILURE;}
            retireLease(*lease);return false;
        }catch(...){error=ERROR_GEN_FAILURE;return false;}
    }
    Result run(HANDLE source,Kind kind,void* data,DWORD size,DWORD timeoutMs,DWORD graceMs=50,
               IssueGuard issueGuard=nullptr,void* issueContext=nullptr)noexcept{
        Result r;
        if((kind!=Kind::Connect&&size&&!data)||size>kMessageBytes){r.error=ERROR_INVALID_PARAMETER;return r;}
        try {
            std::lock_guard<std::mutex> lock(mutex_);sweepLocked();
            if(sealed_.load(std::memory_order_acquire)){r.error=ERROR_SHUTDOWN_IN_PROGRESS;return r;}
            if(!active_||active_->original.get()!=source){r.error=ERROR_INVALID_HANDLE;return r;}
            std::size_t slot=operations_.size();for(std::size_t i=0;i<operations_.size();++i)if(!operations_[i]){slot=i;break;}
            const DWORD payloadSize=kind==Kind::Connect?0:size;
            std::size_t resident=0;
            for(const auto& op:operations_)if(op)resident+=op->bytes.capacity();
            if(reusable_)resident+=reusable_->bytes.capacity();
            const bool reuse=reusable_&&!reusable_->event.failed()&&reusable_->lease==active_;
            const auto retainedCapacity=reuse?reusable_->bytes.capacity():0;
            const auto neededCapacity=retainedCapacity>payloadSize?retainedCapacity:payloadSize;
            if(slot==operations_.size()||payloadSize>kPayloadBudget-payload_||
                neededCapacity>kPayloadBudget-(resident-retainedCapacity)){r.error=ERROR_NOT_ENOUGH_MEMORY;return r;}
            if(nextOperation_==UINT64_MAX){r.error=ERROR_ARITHMETIC_OVERFLOW;return r;}
            // Publish the owner before creating/resetting an event or a
            // throwing resize. Preparation failure is reclaimed in this slot.
            auto& storage=operations_[slot];
            if(reuse)storage=std::move(reusable_);
            try {
                if(storage){
                    if(payloadSize>storage->bytes.capacity()){
                        std::vector<unsigned char> replacement(payloadSize);storage->bytes.swap(replacement);
                    }else storage->bytes.resize(payloadSize);
                    if(!ResetEvent(storage->event.get())){r.error=GetLastError();releaseOperation(storage);return r;}
                }else{
                    storage=std::make_unique<Operation>(active_,kind,payloadSize,nextOperation_+1);
                    HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
                    if(!event){r.error=GetLastError();releaseOperation(storage);return r;}
                    storage->event.attach(event);
                }
            }catch(const std::bad_alloc&){r.error=ERROR_NOT_ENOUGH_MEMORY;releaseOperation(storage);return r;}
            catch(...){r.error=ERROR_GEN_FAILURE;releaseOperation(storage);return r;}
            auto& op=*storage;
            op.ov=OVERLAPPED{};op.kind=kind;op.id=nextOperation_+1;
            op.issued=false;op.terminal=false;op.apiTrue=false;
            op.queryAttempted=false;op.queryTrue=false;op.known=false;op.cancelled=false;
            op.initial=ERROR_SUCCESS;op.completion=ERROR_SUCCESS;op.queryError=ERROR_SUCCESS;
            op.count=0;op.cancelError=ERROR_SUCCESS;
            if(kind==Kind::Write&&size)std::memcpy(op.bytes.data(),data,size);
            HANDLE event=op.event.get();op.ov.hEvent=event;
            if(kind==Kind::Write && issueGuard && !issueGuard(issueContext)) {
                r.contextRejected=true;r.error=ERROR_OPERATION_ABORTED;releaseOperation(storage);return r;
            }
            op.accounted=true;payload_+=payloadSize;pending_.fetch_add(1,std::memory_order_release);++nextOperation_;++admitted_;
            r.admitted=true;r.operation=op.id;r.lease=op.lease->id;
            issue(op);
            if(!op.terminal){
                r.firstWait=WaitForSingleObject(event,timeoutMs);
                const DWORD firstError=r.firstWait==WAIT_OBJECT_0?ERROR_SUCCESS:
                    r.firstWait==WAIT_TIMEOUT?WAIT_TIMEOUT:GetLastError();
                if(r.firstWait==WAIT_OBJECT_0){
                    const bool done=query(op);r.error=done?op.completion:op.queryError;
                    if(!done&&r.error==ERROR_SUCCESS)r.error=ERROR_GEN_FAILURE;
                }else r.error=firstError;
            }else r.error=op.completion;
            r.apiReturnedTrue=op.apiTrue;r.initialError=op.initial;
            r.queryAttempted=op.queryAttempted;r.queryReturnedTrue=op.queryTrue;r.queryError=op.queryError;
            // The original request outcome is fixed before cancellation/cleanup.
            if(op.terminal&&r.error==op.completion&&r.firstWait==WAIT_OBJECT_0&&op.known){
                r.byteCountKnown=true;r.transferred=op.count;
                if(kind==Kind::Read&&op.count)std::memcpy(data,op.bytes.data(),op.count);
            }
            r.ok=op.terminal&&r.error==ERROR_SUCCESS;
            // An inline-success read with no proven count cannot supply a
            // receipt. Reject explicitly instead of passing unknown as empty.
            if(r.ok&&kind==Kind::Read&&!r.byteCountKnown){r.ok=false;r.error=ERROR_INVALID_DATA;}
            if(!op.terminal){cancel(op);(void)WaitForSingleObject(event,graceMs);(void)query(op);}
            r.cancelAttempted=op.cancelled;r.cancelError=op.cancelError;r.terminal=op.terminal;r.retired=!op.terminal;
            if(op.terminal)erase(slot);
            if(sealed_.load(std::memory_order_acquire))sweepLocked();
            return r;
        }catch(const std::bad_alloc&){r.error=ERROR_NOT_ENOUGH_MEMORY;return r;}
        catch(...){r.error=ERROR_GEN_FAILURE;return r;}
    }
    void close(HANDLE source)noexcept{
        try {std::lock_guard<std::mutex> lock(mutex_);if(active_&&active_->original.get()==source)closeActiveLocked();}
        catch(...){} // No other raw handle may be closed by a stale identity.
    }
    void maintenance()noexcept{
        try {std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(lock.owns_lock())sweepLocked();}catch(...){}
    }
    void sealAndReap()noexcept{
        // Nonblocking admission fence. If an in-flight helper owns the mutex,
        // its owner stays registered; run/maintenance continue retirement.
        sealed_.store(true,std::memory_order_seq_cst);
        try {std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(lock.owns_lock()){closeActiveLocked();sweepLocked();}}catch(...){}
    }
    Snapshot snapshot()noexcept{
        Snapshot s;s.sealed=sealed_.load(std::memory_order_acquire);
        try {std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return s;
            s.available=true;s.pending=pending_.load(std::memory_order_acquire);s.payloadBytes=payload_;s.admitted=admitted_;s.reaped=reaped_;
            s.lastReapedOperation=lastReaped_;s.lastReapedLease=lastReapedLease_;s.lastReapedError=lastReapedError_;s.closeFailures=closeFailures_;
            for(const auto& lease:leases_)if(lease.occupied){
                if(&lease==active_)++s.activeLeases;else ++s.retainedLeases;
                s.ownedOriginals+=lease.original.valid();s.ownedDuplicates+=lease.issue.valid();
                s.failedCloseDebt+=lease.original.failed()+lease.issue.failed();
            }
            for(const auto& op:operations_)if(op){
                ++s.operationSlots;s.ownedEvents+=op->event.valid();s.failedCloseDebt+=op->event.failed();
                s.retainedOperations+=!pendingOperation(*op);s.residentPayloadBytes+=op->bytes.capacity();
            }
            if(reusable_){
                s.cachedOperations=1;s.ownedEvents+=reusable_->event.valid();s.failedCloseDebt+=reusable_->event.failed();
                s.residentPayloadBytes+=reusable_->bytes.capacity();
            }
            s.stopped=s.activeLeases==0&&s.retainedLeases==0&&s.pending==0&&s.cachedOperations==0&&s.operationSlots==0&&
                s.ownedOriginals==0&&s.ownedDuplicates==0&&s.ownedEvents==0&&s.failedCloseDebt==0;
        }catch(...){}return s;
    }
    bool stopped()noexcept{const auto s=snapshot();return s.available&&s.stopped;}

};
inline std::atomic<Runtime*> processOwner{nullptr};
inline std::atomic<bool> processSealed{false};
inline Runtime* existingProcessRuntime()noexcept{return processOwner.load(std::memory_order_acquire);}
inline void sealProcess()noexcept{
    processSealed.store(true,std::memory_order_seq_cst);
    if(auto* owner=processOwner.load(std::memory_order_seq_cst))owner->sealAndReap();
}
inline Runtime& processRuntime(){
    static Runtime* value=[](){
        auto* owner=new Runtime();
        // A process seal can precede or race first publication. Sequentially
        // consistent publication/check ensures either sealer observes the owner
        // or the initializer observes the seal before returning it to a caller.
        processOwner.store(owner,std::memory_order_seq_cst);
        if(processSealed.load(std::memory_order_seq_cst))owner->sealAndReap();
        return owner;
    }();
    return *value;
}
} // namespace ymcc::inputHost::pipeTransport
