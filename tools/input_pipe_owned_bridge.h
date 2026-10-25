#pragma once
// R42 isolated legacy-result/retirement bridge. Never included by production.
// Serialized access only. The caller must keep one reachable process-wide Hub
// and one shared logical Budget across all endpoint generations. This fixture
// does not solve final process teardown or turn the R41 history cap into policy.
#include "input_pipe_owned_win32_backend.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace ymcc::testing::pipeBridge {
namespace own=ymcc::testing::pipeOwnership;
struct Result {
    bool ok=false,admitted=false,terminalAtReturn=false,retired=false;
    DWORD error=ERROR_SUCCESS,transferred=0;
    bool byteCountKnown=false;
    std::uint64_t operationId=0,ticket=0;
};
struct Sweep {std::size_t terminal=0,pending=0,unavailable=0,expired=0;};
template<class Backend,std::size_t Slots=4> class RetirementHub {
public:
    using Registry=own::Registry<Backend,Slots>;
    using Operation=typename Registry::Operation;
private:
    struct Slot {std::uint64_t ticket=0;std::weak_ptr<Operation> weak;};
    std::array<Slot,Slots> slots_{};
    std::uint64_t nextTicket_=0;
public:
    // Admission occurs BEFORE issue, so retirement cannot allocate or fail
    // after an asynchronous kernel operation has started.
    std::uint64_t admit(const std::shared_ptr<Operation>& op)noexcept{
        if(nextTicket_==std::numeric_limits<std::uint64_t>::max())return 0;
        for(auto& slot:slots_){
            auto previous=slot.weak.lock();
            if(previous&&previous->phase()!=own::Phase::Terminal)continue;
            slot.ticket=++nextTicket_;slot.weak=op;return slot.ticket;
        }
        return 0;
    }
    void releaseCompleted(std::uint64_t ticket)noexcept{
        for(auto& slot:slots_)if(slot.ticket==ticket){
            auto op=slot.weak.lock();
            if(!op||op->phase()==own::Phase::Terminal)slot=Slot{};
            return;
        }
    }
    Sweep sweep()noexcept{
        Sweep result;
        for(auto& slot:slots_){
            if(!slot.ticket)continue;
            auto op=slot.weak.lock();
            if(!op){slot=Slot{};++result.expired;continue;}
            const auto state=op->poll();
            if(state==own::Poll::Terminal){slot=Slot{};++result.terminal;}
            else if(state==own::Poll::Pending)++result.pending;
            else ++result.unavailable;
        }
        return result;
    }
    std::size_t tracked()const noexcept{
        std::size_t count=0;
        for(const auto& slot:slots_)if(!slot.weak.expired())++count;
        return count;
    }
};
template<class Backend,std::size_t Slots=4> class Endpoint {
    using Hub=RetirementHub<Backend,Slots>;
    std::shared_ptr<Backend> backend_;
    std::shared_ptr<Hub> hub_;
    typename Hub::Registry registry_;
public:
    Endpoint(std::shared_ptr<Backend> backend,std::shared_ptr<own::Budget> budget,
             std::shared_ptr<Hub> hub):backend_(std::move(backend)),hub_(std::move(hub)),registry_(backend_,std::move(budget)){}
    Result run(own::Kind kind,void* data,DWORD size,DWORD timeoutMs,DWORD graceMs=50){
        Result result;
        if(kind!=own::Kind::Connect&&size&&!data){result.error=ERROR_INVALID_PARAMETER;return result;}
        // Extra copies are intentional correctness scaffolding, not a proposed
        // CPU optimization. Caller storage never becomes an asynchronous buffer.
        std::shared_ptr<typename Hub::Operation> op;
        try {
            std::vector<unsigned char> payload(kind==own::Kind::Connect?0:size);
            if(kind==own::Kind::Write&&size)std::memcpy(payload.data(),data,size);
            auto acquisition=registry_.acquire(kind,payload);
            op=std::move(acquisition.first);
        }catch(const ymcc::testing::pipeWin32::ApiError& failure){result.error=failure.code();return result;}
        catch(const std::bad_alloc&){result.error=ERROR_NOT_ENOUGH_MEMORY;return result;}
        // Other construction exceptions propagate before issue; their partially
        // acquired budget has already been rolled back by Registry::acquire.
        if(!op){result.error=ERROR_NOT_ENOUGH_MEMORY;return result;}
        result.ticket=hub_->admit(op);
        if(!result.ticket){result.error=ERROR_NOT_ENOUGH_MEMORY;return result;}
        result.admitted=true;result.operationId=op->id();
        const auto issued=op->issue();
        bool originalTerminal=op->phase()==own::Phase::Terminal;
        if(issued==own::Issue::Pending){
            const DWORD wait=backend_->waitEvent(op->id(),timeoutMs);
            // Capture immediately; cancellation/grace/query may alter last-error.
            const DWORD firstWaitError=wait==WAIT_OBJECT_0?ERROR_SUCCESS:
                wait==WAIT_TIMEOUT?WAIT_TIMEOUT:GetLastError();
            if(wait==WAIT_OBJECT_0){
                const auto polled=op->poll();originalTerminal=polled==own::Poll::Terminal;
                const auto observed=backend_->summary(op->id());
                result.error=originalTerminal?observed.completionError:observed.lastPollError;
                if(!originalTerminal&&result.error==ERROR_SUCCESS)result.error=ERROR_GEN_FAILURE;
            }else result.error=firstWaitError;
        }else result.error=backend_->summary(op->id()).completionError;
        // Freeze the request result and byte eligibility BEFORE cleanup. Late
        // completion is separate evidence; it cannot make an expired read write
        // into caller storage or convert a timeout to success.
        if(originalTerminal&&kind!=own::Kind::Connect){
            const auto observed=backend_->summary(op->id());
            if(observed.byteCountKnown&&observed.bytes<=op->size()){
                result.transferred=observed.bytes;result.byteCountKnown=true;
                if(kind==own::Kind::Read&&observed.bytes)
                    std::memcpy(data,op->data(),observed.bytes);
            }
        }
        result.ok=originalTerminal&&result.error==ERROR_SUCCESS;
        if(op->phase()!=own::Phase::Terminal){
            op->retire();op->requestCancel();
            (void)backend_->waitEvent(op->id(),graceMs);
            (void)op->poll();
        }
        result.terminalAtReturn=op->phase()==own::Phase::Terminal;
        result.retired=!result.terminalAtReturn;
        hub_->releaseCompleted(result.ticket);
        return result;
    }
    bool io(bool write,void* data,DWORD size,DWORD timeoutMs,DWORD* transferred,DWORD* error){
        if(transferred)*transferred=0;if(error)*error=ERROR_SUCCESS;
        const auto result=run(write?own::Kind::Write:own::Kind::Read,data,size,timeoutMs);
        if(transferred)*transferred=result.transferred;if(error)*error=result.error;return result.ok;
    }
    bool connect(DWORD timeoutMs,DWORD* error){
        if(error)*error=ERROR_SUCCESS;
        const auto result=run(own::Kind::Connect,nullptr,0,timeoutMs);
        if(error)*error=result.error;return result.ok;
    }
};
} // namespace ymcc::testing::pipeBridge
