#pragma once
#include <mutex>
#include <string>
#include <deque>
#include <algorithm>
#include <stdexcept>
namespace ymcc::update_download {
// The existing update operation mutex still owns all cache IO. This control
// only permits cancellation/recovery of that owner, never a second worker.
class Control {
    mutable std::mutex mutex_;
    std::string operation_;
    bool background_ = false, cancelled_ = false;
    // More than the bounded 32-entry IPC queue; late queued POSTs are fenced.
    std::deque<std::string> retired_;
public:
    struct Status { std::string operation; bool background; };
    class Lease {
        Control& owner_;
    public:
        Lease(Control& owner, const std::string& operation, bool background):owner_(owner) {
            std::lock_guard<std::mutex> lock(owner_.mutex_);
            if(background && std::find(owner_.retired_.begin(),owner_.retired_.end(),operation)!=owner_.retired_.end())
                throw std::runtime_error("Background download operation was retired");
            owner_.operation_=operation; owner_.background_=background; owner_.cancelled_=false;
        }
        Lease(const Lease&)=delete;
        ~Lease() { std::lock_guard<std::mutex> lock(owner_.mutex_); owner_.operation_.clear(); owner_.background_=false; owner_.cancelled_=false; }
    };
    bool cancel(const std::string& operation) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(operation.empty() || operation!=operation_ || !background_) return false;
        cancelled_=true; return true;
    }
    bool cancelled(const std::string& operation) const {
        std::lock_guard<std::mutex> lock(mutex_); return operation==operation_ && cancelled_;
    }
    bool retire(const std::string& operation) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(operation.empty() || operation.size()>128) return false;
        if(operation==operation_ && !background_) return false;
        if(std::find(retired_.begin(),retired_.end(),operation)==retired_.end()) {
            retired_.push_back(operation); if(retired_.size()>256) retired_.pop_front();
        }
        if(operation==operation_) cancelled_=true;
        return true;
    }
    Status status() const { std::lock_guard<std::mutex> lock(mutex_); return {operation_,background_}; }
};
}
