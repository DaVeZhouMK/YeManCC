#pragma once
#include "decky_binding_client.h"
#include <atomic>
#include <memory>
#include <mutex>

namespace ymcc::steamdocuments::finite {
struct ListenerReady final {
private:
    std::atomic<HANDLE> value_{nullptr};
    std::atomic<DWORD> error_{0};
    std::mutex mutex_;
    void remember(DWORD value) noexcept {
        DWORD expected=0;
        error_.compare_exchange_strong(expected,value?value:ERROR_GEN_FAILURE,std::memory_order_relaxed);
    }
public:
    ListenerReady(){
        const auto value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!value)throw std::runtime_error("document-listener-unavailable");
        value_.store(value,std::memory_order_release);
    }
    ListenerReady(const ListenerReady&)=delete;
    ListenerReady& operator=(const ListenerReady&)=delete;
    ~ListenerReady(){(void)close();}
    HANDLE handle() const noexcept{return value_.load(std::memory_order_acquire);}
    DWORD handles() const noexcept{return handle()?DWORD{1}:DWORD{0};}
    DWORD error() const noexcept{return error_.load(std::memory_order_relaxed);}
    bool signal() noexcept {
        try{
            std::lock_guard guard(mutex_);
            const auto value=handle();
            if(!value)return false;
            if(SetEvent(value))return true;
            remember(GetLastError());
        }catch(...){remember(ERROR_GEN_FAILURE);}
        return false;
    }
    bool close() noexcept {
        try{
            std::unique_lock guard(mutex_,std::try_to_lock);
            if(!guard.owns_lock()){remember(ERROR_BUSY);return false;}
            const auto value=handle();
            if(!value)return true;
            if(!CloseHandle(value)){remember(GetLastError());return false;}
            value_.store(nullptr,std::memory_order_release);
            return true;
        }catch(...){remember(ERROR_GEN_FAILURE);return false;}
    }
};
// Request resources remain owned by this caller after a refused close. There
// is one slot, no executor or timer, and no scan of other consumers' debt.
class Parent final {
    struct Resources {
        deckybinding::Owner owner;
        deckybinding::clientdetail::EventReader reader;
        std::shared_ptr<ListenerReady> ready;
    };
    std::mutex mutex_,admission_;
    std::unique_ptr<Resources> resources_;
    std::atomic<bool> retirementRequested_{false},retired_{false};

    bool reclaimLocked(DWORD budgetMs) noexcept {
        if(!resources_)return true;
        const auto end=GetTickCount64()+budgetMs;
        const auto left=[&]() noexcept {
            const auto now=GetTickCount64();
            return now>=end?DWORD{0}:static_cast<DWORD>(end-now);
        };
        resources_->owner.requestCancel();
        resources_->reader.requestCancel();
        const auto grace=(std::min)(DWORD{150},left());
        const auto available=left();
        const auto stopped=resources_->owner.stop((std::min)(grace,available),available-(std::min)(grace,available));
        const bool reader=resources_->reader.stop(left());
        const bool ready=!resources_->ready||resources_->ready->close();
        if(!stopped.fullyDestroyed||!reader||!ready)return false;
        resources_.reset();
        return true;
    }
public:
    struct Status {
        bool known=false,retiring=false,retired=false,resources=false;
        DWORD readyHandles=0,readyError=0;
        deckybinding::Snapshot owner;
        deckybinding::clientdetail::EventReader::Status reader;
    };
    class Use final {
        Parent& parent_;
        std::unique_lock<std::mutex> guard_;
        Resources* value_=nullptr;
        bool clean_=false,completed_=false;
        friend class Parent;
        explicit Use(Parent& parent):parent_(parent),guard_(parent.mutex_,std::try_to_lock){
            if(!guard_.owns_lock())return;
            std::unique_lock admission(parent_.admission_,std::try_to_lock);
            if(!admission.owns_lock()||parent_.retirementRequested_.load(std::memory_order_acquire))return;
            // Only a nonwaiting cleanup attempt on the existing slot. Refusal
            // prevents a new Owner/reader/child allocation and preserves debt.
            if(!parent_.reclaimLocked(0))return;
            parent_.resources_=std::make_unique<Resources>();
            value_=parent_.resources_.get();
        }
    public:
        Use(const Use&)=delete;
        Use& operator=(const Use&)=delete;
        ~Use(){
            if(!value_)return;
            if(!completed_){(void)parent_.reclaimLocked(0);return;}
            if(clean_)parent_.resources_.reset();
        }
        explicit operator bool()const noexcept{return value_!=nullptr;}
        deckybinding::Result start(){
            std::unique_lock admission(parent_.admission_,std::try_to_lock);
            if(!admission.owns_lock()||parent_.retirementRequested_.load(std::memory_order_acquire)){
                deckybinding::Result result;result.code=deckybinding::Code::cancelled;result.error=ERROR_OPERATION_ABORTED;return result;
            }
            return value_->owner.start();
        }
        deckybinding::Owner& owner() noexcept{return value_->owner;}
        deckybinding::clientdetail::EventReader& reader() noexcept{return value_->reader;}
        std::shared_ptr<ListenerReady> ready(){
            if(!value_->ready)value_->ready=std::make_shared<ListenerReady>();
            return value_->ready;
        }
        bool closeReady() noexcept{return !value_->ready||value_->ready->close();}
        DWORD readyHandles() const noexcept{return value_->ready?value_->ready->handles():DWORD{0};}
        DWORD readyError() const noexcept{return value_->ready?value_->ready->error():DWORD{0};}
        void completed(bool destroyed)noexcept{completed_=true;clean_=destroyed;}
    };
    Parent()=default;
    Parent(const Parent&)=delete;
    Parent& operator=(const Parent&)=delete;
    Use acquire(){return Use(*this);}
    bool current()const noexcept{return !retirementRequested_.load(std::memory_order_acquire);}
    Status snapshot()noexcept{
        Status value;value.retired=retired_.load(std::memory_order_acquire);
        value.retiring=retirementRequested_.load(std::memory_order_acquire)&&!value.retired;
        std::unique_lock guard(mutex_,std::try_to_lock);
        if(!guard.owns_lock())return value;
        value.known=true;value.resources=bool(resources_);
        if(resources_){
            value.owner=resources_->owner.snapshot();value.reader=resources_->reader.snapshot();
            if(resources_->ready){value.readyHandles=resources_->ready->handles();value.readyError=resources_->ready->error();}
        }
        return value;
    }
    bool reclaim(DWORD budgetMs)noexcept{
        if(budgetMs==INFINITE)return false;
        std::unique_lock guard(mutex_,std::try_to_lock);
        return guard.owns_lock()&&reclaimLocked(budgetMs);
    }
    bool finishForExit(DWORD budgetMs)noexcept{
        retirementRequested_.store(true,std::memory_order_release);
        std::unique_lock admission(admission_,std::try_to_lock);
        if(!admission.owns_lock())return false; // A running start has not committed retirement yet.
        retired_.store(true,std::memory_order_release);
        return reclaim(budgetMs);
    }
};
inline Parent& documentParent(){static Parent value;return value;}
} // namespace ymcc::steamdocuments::finite
