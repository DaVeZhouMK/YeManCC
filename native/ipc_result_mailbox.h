#pragma once
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>

// A posted HWND message is a wakeup, not an owner of a heap pointer. Windows
// can discard successfully posted messages when their window is destroyed.
// Keep responses here until the UI takes them or the root owner retires.
namespace ymcc::ipc {
template<class Response> class ResultMailbox {
    std::mutex mutex_;
    std::map<std::uintptr_t,std::unique_ptr<Response>> pending_;
    std::uintptr_t next_=0;
    bool accepting_=true;
public:
    // send must be a nonblocking asynchronous post; it must not reenter take.
    template<class Post> bool post(std::unique_ptr<Response> response,Post send) noexcept {
        if(!response)return false;
        try {
            std::lock_guard lock(mutex_);
            if(!accepting_||next_==(std::numeric_limits<std::uintptr_t>::max)())return false;
            const auto token=++next_;
            const auto entry=pending_.emplace(token,std::move(response)).first;
            bool posted=false;try{posted=send(token);}catch(...){ }
            if(!posted)pending_.erase(entry);
            return posted;
        }catch(...){return false;}
    }
    std::unique_ptr<Response> take(std::uintptr_t token) noexcept {
        try {
            std::lock_guard lock(mutex_);
            const auto it=pending_.find(token);if(it==pending_.end())return {};
            auto response=std::move(it->second);pending_.erase(it);return response;
        }catch(...){return {};}
    }
    void retire() noexcept {
        try {
            std::map<std::uintptr_t,std::unique_ptr<Response>> retired;
            {std::lock_guard lock(mutex_);accepting_=false;retired.swap(pending_);}
            // Destruct responses outside the lock; callbacks cannot reactivate it.
        }catch(...){ }
    }
};
}
