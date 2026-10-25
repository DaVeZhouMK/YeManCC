#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace ymcc::autoclose {
// Preserve the scan deadline while allowing every exit path to interrupt the wait.
// The stop predicate is changed under the wait mutex so a notification cannot be lost.
class ScanWait {
    std::atomic<bool>& stopped_;
    std::mutex mutex_;
    std::condition_variable wake_;
public:
    explicit ScanWait(std::atomic<bool>& stopped) noexcept : stopped_(stopped) {}
    bool waitForNextScan(std::chrono::milliseconds interval) {
        std::unique_lock<std::mutex> lock(mutex_);
        return !wake_.wait_for(lock, interval, [this] { return stopped_.load(); });
    }
    void requestStop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_.store(true);
        }
        wake_.notify_all();
    }
};
} // namespace ymcc::autoclose
