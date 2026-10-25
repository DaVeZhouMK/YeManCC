#pragma once

#include <atomic>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace ymcc::steamOriginal {

// Owns one successful task launch. The owner must outlive the task, and must
// destroy this object outside its worker thread. Destruction must not race any
// public call. start() is intended for one launching caller; join()/tryJoin()
// serialize all access to the std::thread, including a concurrent start().
class RecoveryWorker {
public:
    RecoveryWorker() = default;
    RecoveryWorker(const RecoveryWorker&) = delete;
    RecoveryWorker& operator=(const RecoveryWorker&) = delete;
    RecoveryWorker(RecoveryWorker&&) = delete;
    RecoveryWorker& operator=(RecoveryWorker&&) = delete;

    // Task exceptions are saved, never rethrown by join() or this destructor.
    // As with std::thread, joining/destroying from the owned thread is invalid.
    ~RecoveryWorker() noexcept { join(); }

    // A successful launch consumes the single start, even after joining.
    // Empty tasks and repeated launches throw invalid_argument/logic_error.
    // Construction/OS launch failures propagate and leave this object retryable.
    void start(std::function<void()> fn) {
        std::lock_guard<std::mutex> lock(joinMx_);
        if (started_) {
            throw std::logic_error("RecoveryWorker has already started");
        }
        if (!fn) {
            throw std::invalid_argument("RecoveryWorker needs a task");
        }

        finished_.store(false, std::memory_order_release);
        try {
            thread_ = std::thread([this, task = std::move(fn)]() mutable noexcept {
                Completion completion(finished_);
                {
                    // Empty the thread closure before invoking the task. Its
                    // captures (including final log handles) must be destroyed
                    // before Completion publishes true, on both return and throw.
                    std::function<void()> localTask;
                    localTask.swap(task);
                    try {
                        localTask();
                    } catch (...) {
                        failure_ = std::current_exception();
                    }
                }
            });
            started_ = true;
        } catch (...) {
            finished_.store(true, std::memory_order_release);
            throw;
        }
    }

    // Never takes joinMx_: safe for UI observation while another caller joins.
    // true means never launched, or task stack AND callable captures destroyed.
    // Thread-local destruction/OS thread return can still follow; use join() or
    // the host's cleanup-thread handle gate when full thread exit is required.
    bool finished() const noexcept {
        return finished_.load(std::memory_order_acquire);
    }

    // Blocking cleanup for background exit. Repeated/concurrent joins are safe;
    // only std::thread::join failures (not task exceptions) can propagate.
    void join() {
        std::lock_guard<std::mutex> lock(joinMx_);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // Never waits for an unfinished task or for joinMx_. Once completion and the
    // lock are available, join may still wait for the OS/thread-local exit tail.
    bool tryJoin() {
        if (!finished()) {
            return false;
        }
        std::unique_lock<std::mutex> lock(joinMx_, std::try_to_lock);
        if (!lock.owns_lock() || !finished()) {
            return false;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        return true;
    }

    // Explicit diagnostics; may wait behind join(). Calling while the task is
    // unfinished throws logic_error. A stored failure can be rethrown repeatedly.
    void rethrowIfFailed() const {
        if (!finished()) {
            throw std::logic_error("RecoveryWorker task is still running");
        }
        std::exception_ptr failure;
        {
            std::lock_guard<std::mutex> lock(joinMx_);
            if (!finished()) {
                throw std::logic_error("RecoveryWorker task is still running");
            }
            failure = failure_;
        }
        if (failure) {
            std::rethrow_exception(failure);
        }
    }

private:
    struct Completion {
        explicit Completion(std::atomic<bool>& finished) noexcept
            : finished_(finished) {}
        ~Completion() noexcept {
            finished_.store(true, std::memory_order_release);
        }
        Completion(const Completion&) = delete;
        Completion& operator=(const Completion&) = delete;
        std::atomic<bool>& finished_;
    };

    std::atomic<bool> finished_{true};
    mutable std::mutex joinMx_;
    std::thread thread_;
    bool started_ = false; // Protected by joinMx_; not reset by join().
    std::exception_ptr failure_; // Published by finished_'s release/acquire.
};

} // namespace ymcc::steamOriginal
