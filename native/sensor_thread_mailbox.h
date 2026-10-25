#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
namespace ymcc::sensors {
enum class ThreadRequest : uint32_t { None=0, Start, Stop, Suspended, Resume, Resubscribe, Rebind, Shutdown };
// One acknowledgement belongs to one request, never to the most recent poster.
// All queued terminal work remains visible to bounded STA discovery.
class ThreadMailbox {
public:
    enum class Result { Pending, Completed, Failed, Exited, TimedOut, Unavailable };
    struct Ticket { uint64_t id; ThreadRequest request; Result result=Result::Pending; };
    using Handle=std::shared_ptr<Ticket>;
private:
    mutable std::mutex mx_;
    std::condition_variable cv_;
    std::deque<Handle> queued_;
    Handle active_;
    uint64_t next_=0;
    bool open_=false, closing_=false;
public:
    bool open() {
        std::lock_guard<std::mutex> lock(mx_);
        if(open_) return false;
        open_=true;closing_=false;return true;
    }
    bool acceptsWork() const {std::lock_guard<std::mutex> lock(mx_);return open_ && !closing_;}
    Handle submit(ThreadRequest request) {
        std::lock_guard<std::mutex> lock(mx_);
        if(!open_ || request==ThreadRequest::None ||
            (closing_ && request!=ThreadRequest::Stop && request!=ThreadRequest::Shutdown)) return {};
        auto ticket=std::make_shared<Ticket>(Ticket{++next_,request});
        queued_.push_back(ticket);
        if(request==ThreadRequest::Shutdown) closing_=true;
        return ticket;
    }
    Handle take() {
        std::lock_guard<std::mutex> lock(mx_);
        if(active_ || queued_.empty()) return {};
        active_=queued_.front();queued_.pop_front();return active_;
    }
    void complete(const Handle& ticket,bool success=true) {
        {
            std::lock_guard<std::mutex> lock(mx_);
            if(!ticket || ticket!=active_) return;
            if(ticket->result==Result::Pending) ticket->result=success ? Result::Completed : Result::Failed;
            active_.reset();
        }
        cv_.notify_all();
    }
    bool hasQueuedWork() const {std::lock_guard<std::mutex> lock(mx_);return !queued_.empty();}
    bool stopRequested() const {
        std::lock_guard<std::mutex> lock(mx_);
        if(closing_) return true;
        for(const auto& ticket:queued_) if(ticket->request==ThreadRequest::Stop ||
            ticket->request==ThreadRequest::Suspended || ticket->request==ThreadRequest::Shutdown) return true;
        return false;
    }
    Result wait(const Handle& ticket,std::chrono::milliseconds budget) {
        if(!ticket) return Result::Unavailable;
        std::unique_lock<std::mutex> lock(mx_);
        if(!cv_.wait_for(lock,budget,[&]{return ticket->result!=Result::Pending;})) {
            // Timeout is NOT an acknowledgement. Keep the work and worker owned;
            // reject fresh Start/Resume until shutdown has actually settled.
            closing_=true;return Result::TimedOut;
        }
        return ticket->result;
    }
    void close() {
        {
            std::lock_guard<std::mutex> lock(mx_);
            open_=false;closing_=true;
            if(active_ && active_->result==Result::Pending) active_->result=Result::Exited;
            active_.reset();
            for(auto& ticket:queued_) ticket->result=Result::Exited;
            queued_.clear();
        }
        cv_.notify_all();
    }
};
} // namespace ymcc::sensors
