#pragma once
// R40 isolated ownership prototype. Never included by production.
// Backend is a deterministic test adapter. Its Terminal classification is a
// required trusted boundary, NOT a proposed mapping of arbitrary Win32 errors.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
namespace ymcc::testing::pipeOwnership {
enum class Kind {Write,Read,Connect};
enum class Issue {Completed,Rejected,Pending};
enum class Poll {Terminal,Pending,Unavailable};
enum class Phase {Ready,Pending,Retired,Terminal};
struct Budget {
    // All access is serialized by the owner, as production exchange calls are.
    // A future adapter must use one process-wide budget across target epochs.
    std::size_t count=0,bytes=0;
    const std::size_t countLimit,byteLimit;
    Budget(std::size_t countCap,std::size_t byteCap):countLimit(countCap),byteLimit(byteCap){}
    Budget(const Budget&)=delete;
    Budget& operator=(const Budget&)=delete;
    bool acquire(std::size_t n) noexcept {
        if(count>=countLimit||n>byteLimit||bytes>byteLimit-n)return false;
        ++count;bytes+=n;return true;
    }
    void release(std::size_t n) noexcept {--count;bytes-=n;}
};
struct OverlappedStorage {std::uint64_t tag=0;};
template<class Backend,std::size_t Slots=4> class Registry {
    class CreationPermit { friend class Registry; CreationPermit()=default; };
    static_assert(noexcept(std::declval<Backend&>().issue(std::uint64_t{})),
                  "Issue adapter must not throw after an operation can start");
public:
    class Operation final:public std::enable_shared_from_this<Operation> {
        std::shared_ptr<Backend> backend_;
        std::shared_ptr<Budget> budget_;
        std::vector<unsigned char> bytes_;
        OverlappedStorage overlapped_{};
        std::uint64_t id_=0;
        Kind kind_;
        Phase phase_=Phase::Ready;
        std::shared_ptr<Operation> pendingPin_;
        void terminal() noexcept {
            phase_=Phase::Terminal;
            // Every caller of this function has an external shared owner.
            // Never destroy this object from within a raw callback.
            pendingPin_.reset();
        }
    public:
        Operation(std::shared_ptr<Backend> backend,std::shared_ptr<Budget> budget,Kind kind,
                  const std::vector<unsigned char>& bytes,CreationPermit)
            :backend_(std::move(backend)),budget_(std::move(budget)),bytes_(bytes),kind_(kind) {
            id_=backend_->create(kind_,&overlapped_,bytes_.data(),bytes_.size());
        }
        Operation(const Operation&)=delete;
        Operation& operator=(const Operation&)=delete;
        ~Operation() noexcept {
            if(id_)backend_->destroy(id_,&overlapped_,bytes_.data(),bytes_.size());
            budget_->release(bytes_.size());
        }
        Issue issue() {
            if(phase_!=Phase::Ready)throw std::logic_error("operation already issued");
            // Establish the pin BEFORE issuing. shared_ptr copying is noexcept;
            // backend issue must not throw after it can start an operation.
            pendingPin_=this->shared_from_this();
            const auto result=backend_->issue(id_);
            if(result==Issue::Pending)phase_=Phase::Pending;
            else terminal();
            return result;
        }
        void requestCancel() noexcept {
            if(phase_==Phase::Pending||phase_==Phase::Retired)backend_->cancel(id_);
            // Accepted/not-found/failed cancellation does not imply completion.
        }
        void retire() noexcept {
            if(phase_==Phase::Pending)phase_=Phase::Retired;
        }
        Poll poll() noexcept {
            if(phase_==Phase::Terminal)return Poll::Terminal;
            if(phase_==Phase::Ready)return Poll::Unavailable;
            const auto observation=backend_->poll(id_);
            if(observation==Poll::Terminal)terminal();
            return observation;
        }
        Phase phase()const noexcept{return phase_;}
        std::uint64_t id()const noexcept{return id_;}
        const OverlappedStorage* overlapped()const noexcept{return &overlapped_;}
        const unsigned char* data()const noexcept{return bytes_.data();}
        std::size_t size()const noexcept{return bytes_.size();}
        const std::vector<unsigned char>& bytes()const noexcept{return bytes_;}
    };
    struct Ticket {
        std::size_t slot=Slots;
        std::uint64_t generation=0;
        std::weak_ptr<Operation> weak;
    };
private:
    struct Slot {std::uint64_t generation=0;std::weak_ptr<Operation> weak;};
    std::shared_ptr<Backend> backend_;
    std::shared_ptr<Budget> budget_;
    std::array<Slot,Slots> slots_{};
public:
    Registry(std::shared_ptr<Backend> backend,std::shared_ptr<Budget> processBudget)
        :backend_(std::move(backend)),budget_(std::move(processBudget)){}
    std::pair<std::shared_ptr<Operation>,Ticket> acquire(Kind kind,const std::vector<unsigned char>& bytes) {
        std::size_t index=Slots;
        for(std::size_t n=0;n<Slots;++n)if(slots_[n].weak.expired()&&slots_[n].generation!=UINT64_MAX){index=n;break;}
        if(index==Slots||!budget_->acquire(bytes.size()))return {};
        std::shared_ptr<Operation> owner;
        try {owner=std::make_shared<Operation>(backend_,budget_,kind,bytes,CreationPermit{});}
        catch(...){budget_->release(bytes.size());throw;}
        auto& slot=slots_[index];++slot.generation;slot.weak=owner;
        return {owner,Ticket{index,slot.generation,owner}};
    }
    bool poll(const Ticket& ticket) noexcept {
        if(ticket.slot>=Slots)return false;
        auto& slot=slots_[ticket.slot];
        if(slot.generation!=ticket.generation)return false;
        auto owner=slot.weak.lock();
        auto expected=ticket.weak.lock();
        if(!owner||owner!=expected)return false;
        (void)owner->poll();return true;
    }
    void pollAll() noexcept {for(auto& slot:slots_)if(auto owner=slot.weak.lock())(void)owner->poll();}
    static bool pollDetached(const Ticket& ticket) noexcept {
        // Retaining this weak ticket lets an existing serialized reaper recover
        // the pin even after a temporary exchange/registry wrapper is gone.
        auto owner=ticket.weak.lock();if(!owner)return false;(void)owner->poll();return true;
    }
};
} // namespace ymcc::testing::pipeOwnership
