#pragma once
#include <windows.h>
#include <atomic>
#include <memory>

namespace ymcc::steamgamefacts {
// Request-local cancellation only. This carries no game, Steam, selection,
// account, power or publication authority. Ordinary observation never creates it.
class RequestCancellation final {
    const HANDLE event_;
    std::atomic<bool> cancelled_{false};
    explicit RequestCancellation(HANDLE event) noexcept:event_(event){}
public:
    RequestCancellation(const RequestCancellation&)=delete;
    RequestCancellation& operator=(const RequestCancellation&)=delete;
    static std::shared_ptr<RequestCancellation> create() noexcept {
        const auto event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!event)return {};
        RequestCancellation* raw=nullptr;
        try{raw=new RequestCancellation(event);}
        catch(...){CloseHandle(event);return {};}
        // shared_ptr construction deletes raw if its control-block allocation fails.
        try{return std::shared_ptr<RequestCancellation>(raw);}
        catch(...){return {};}
    }
    ~RequestCancellation(){cancel();CloseHandle(event_);}
    void cancel() noexcept {
        cancelled_.store(true,std::memory_order_release);
        SetEvent(event_);
    }
    bool current() noexcept {
        if(cancelled_.load(std::memory_order_acquire))return false;
        if(WaitForSingleObject(event_,0)!=WAIT_TIMEOUT){cancel();return false;}
        return !cancelled_.load(std::memory_order_acquire);
    }
    // Borrowed until the retained signal is released. Consumers never reset or
    // close it; remote receivers get independent SYNCHRONIZE-only copies.
    HANDLE event() const noexcept{return event_;}
};

inline std::shared_ptr<RequestCancellation> requestCancellationSignal(
    std::atomic<std::shared_ptr<RequestCancellation>>& slot,
    const std::atomic<bool>& cancelled) noexcept {
    auto held=slot.load(std::memory_order_acquire);
    if(!held){
        auto fresh=RequestCancellation::create();if(!fresh)return {};
        if(slot.compare_exchange_strong(held,fresh,std::memory_order_acq_rel))held=std::move(fresh);
    }
    // A cancellation racing first creation always reaches the published event.
    if(cancelled.load(std::memory_order_acquire))held->cancel();
    return held;
}
}
