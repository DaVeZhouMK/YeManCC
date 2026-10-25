#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace ymcc::inputCapture {

// One capture worker's startup delay. Use only at startup/stop boundaries, never
// per frame. Owns synchronization only: no worker, timer, second stop flag, clock,
// logging, device/Host control, or authority to change the player's intent.
//
// Lifetime/ownership contract:
// - The caller owns stop; it and this object must outlive every participating call.
//   Use the SAME stop and StartupWait for a worker generation and every stop writer.
//   Direct stores do not provide the notification protocol implemented here.
// - The lifecycle owner must serialize reset against Stop and worker publication.
//   Call reset ONLY after the previous worker has demonstrably left (e.g. joined)
//   and while no new worker can be published. This class cannot prove that gate.
//   The object must outlive ALL waiters/calls; process exit or global lifetime
//   does not demonstrate worker destruction or make a hanging caller safe.
// - requestStop takes a mutex for a short critical section; it is NOT lock-free.
//   The mutex is released throughout the condition-variable wait. Scheduling and
//   lock acquisition have no hard time bound. Joining/destroying the whole worker
//   and arbitrary blocking elsewhere are outside this class's guarantee.
class StartupWait final {
public:
    StartupWait() = default;
    StartupWait(const StartupWait&) = delete;
    StartupWait& operator=(const StartupWait&) = delete;
    StartupWait(StartupWait&&) = delete;
    StartupWait& operator=(StartupWait&&) = delete;

    // true: stop observed; false: budget elapsed without observing stop.
    // Pass the original delay (2500ms) to preserve normal startup timing.
    // The predicate handles stop-before-wait and spurious wakeups. Checking it
    // under the writer's mutex closes the check-to-wait lost-notification gap.
    // May throw on mutex/CV failure; the worker owner handles that failure.
    [[nodiscard]] bool waitForStop(const std::atomic<bool>& stop,
                                   std::chrono::milliseconds budget) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, budget, [&stop] {
            return stop.load(std::memory_order_acquire);
        });
    }

    // Idempotent and non-throwing, including after the waiter has left.
    // Publish first so a locking failure cannot skip the caller's existing Stop
    // cleanup or prevent stop publication. On success, acquiring/releasing the
    // SAME mutex orders notify after any waiter that saw false has enrolled.
    // true: that synchronization completed; false: lock failed, so notification
    // is best-effort ONLY (no reliable wakeup/destruction claim on that path).
    // atomic<bool>::store and condition_variable::notify_all are non-throwing.
    bool requestStop(std::atomic<bool>& stop) noexcept {
        stop.store(true, std::memory_order_release);
        bool synchronized = false;
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            synchronized = true;
        } catch (...) {
            // Keep stop published; still notify below. Do not skip owner cleanup.
        }
        cv_.notify_all();
        return synchronized;
    }

    // PRECONDITION: externally quiescent lifecycle, as described above.
    // Non-throwing. true: reset completed under the mutex; false: lock failed,
    // stop was not changed, and the owner must NOT publish a new worker.
    [[nodiscard]] bool reset(std::atomic<bool>& stop) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            stop.store(false, std::memory_order_release);
            return true;
        } catch (...) {
            return false;
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace ymcc::inputCapture
