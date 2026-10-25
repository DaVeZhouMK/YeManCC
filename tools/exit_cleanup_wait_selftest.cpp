// Standalone MSVC x64 Release test. Link user32.lib. No application dependencies.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

#include "../native/exit_cleanup_wait.h"

namespace {
using ymcc::exitLifecycle::waitForCleanupOrMessages;
constexpr DWORD timeoutMs = 5000;
constexpr UINT testMessage = WM_APP + 0x5e1;
static_assert(sizeof(void*) == 8, "MSVC x64 test required");
static_assert(noexcept(waitForCleanupOrMessages(nullptr)));

void check(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

class Event {
public:
    Event() : handle_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        check(handle_ != nullptr, "CreateEventW failed");
    }
    ~Event() noexcept { CloseHandle(handle_); }
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    void signal() const noexcept { SetEvent(handle_); }
    bool waitFor(DWORD timeout) const noexcept {
        return WaitForSingleObject(handle_, timeout) == WAIT_OBJECT_0;
    }
    void wait() const { check(waitFor(timeoutMs), "event wait timed out"); }
private:
    HANDLE handle_;
};

struct SignalOnExit {
    const Event& event;
    ~SignalOnExit() noexcept { event.signal(); }
};

class Thread {
public:
    template<class Fn>
    explicit Thread(Fn fn) : thread_([this, task = std::move(fn)]() mutable {
        try { task(); } catch (...) { failure_ = std::current_exception(); }
    }) {}
    ~Thread() noexcept {
        if (thread_.joinable()) { thread_.join(); }
    }
    HANDLE handle() { return thread_.native_handle(); }
    void join() {
        if (thread_.joinable()) { thread_.join(); }
        if (failure_) { std::rethrow_exception(failure_); }
    }
private:
    std::exception_ptr failure_;
    std::thread thread_;
};

void createQueue() {
    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
}

void checkEmptyQueue() {
    MSG message{};
    check(!PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE),
        "unexpected queued message in isolated test");
}

void completionWithoutAnyMessageOrTimer() {
    checkEmptyQueue();
    Event entered;
    Event beginWait;
    Event release;
    Thread cleanup([&] {
        entered.signal();
        beginWait.wait();
        // A controlled 200ms kernel wait, no Sleep/PostMessage/SetTimer.
        check(!release.waitFor(200), "cleanup was released unexpectedly");
    });
    SignalOnExit unblockBegin{beginWait};
    SignalOnExit unblockCleanup{release};
    entered.wait();
    check(WaitForSingleObject(cleanup.handle(), 0) == WAIT_TIMEOUT,
        "cleanup was already complete before the wait");
    beginWait.signal();
    const DWORD result = waitForCleanupOrMessages(cleanup.handle());
    check(result == WAIT_OBJECT_0, "kernel completion did not wake message wait");
    check(WaitForSingleObject(cleanup.handle(), 0) == WAIT_OBJECT_0,
        "object wake did not correspond to actual thread exit");
    checkEmptyQueue();
    cleanup.join();
}

void messagesWhileCleanupUnfinished() {
    checkEmptyQueue();
    const DWORD owner = GetCurrentThreadId();
    Event entered;
    Event release;
    Event beginWait;
    Thread cleanup([&] { entered.signal(); release.wait(); });
    Thread poster([&] {
        beginWait.wait();
        check(PostThreadMessageW(owner, testMessage, 0x1234, 0x5678) != FALSE,
            "PostThreadMessageW failed");
    });
    SignalOnExit unblockCleanup{release};
    SignalOnExit unblockPoster{beginWait};
    entered.wait();
    beginWait.signal();
    const DWORD result = waitForCleanupOrMessages(cleanup.handle());
    check(result == WAIT_OBJECT_0 + 1, "queued input did not wake unfinished cleanup wait");
    check(WaitForSingleObject(cleanup.handle(), 0) == WAIT_TIMEOUT,
        "message wake was confused with cleanup completion");
    MSG message{};
    check(PeekMessageW(&message, nullptr, testMessage, testMessage, PM_NOREMOVE) != FALSE,
        "thread message missing from queue");
    // PM_NOREMOVE marks input as seen. INPUTAVAILABLE must still wake for it.
    check(waitForCleanupOrMessages(cleanup.handle()) == WAIT_OBJECT_0 + 1,
        "already-seen input no longer woke INPUTAVAILABLE wait");
    check(PeekMessageW(&message, nullptr, testMessage, testMessage, PM_REMOVE) != FALSE,
        "thread message could not be pumped");
    check(message.hwnd == nullptr && message.message == testMessage &&
        message.wParam == 0x1234 && message.lParam == 0x5678,
        "wrong thread message payload");
    checkEmptyQueue();
    release.signal();
    check(waitForCleanupOrMessages(cleanup.handle()) == WAIT_OBJECT_0,
        "cleanup completion missing after pumping thread message");
    poster.join();
    cleanup.join();
}

void noHandleTimeoutAllowsCreationRetry() {
    checkEmptyQueue();
    const auto before = std::chrono::steady_clock::now();
    const DWORD result = waitForCleanupOrMessages(nullptr);
    const auto elapsed = std::chrono::steady_clock::now() - before;
    check(result == WAIT_TIMEOUT, "no-handle wait must time out");
    check(elapsed >= std::chrono::milliseconds(150) &&
        elapsed < std::chrono::seconds(2), "no-handle timeout was not approximately 200ms");
    // The exit owner can retry creating cleanup immediately after the timeout.
    Thread cleanup([] {});
    check(waitForCleanupOrMessages(cleanup.handle()) == WAIT_OBJECT_0,
        "cleanup creation retry could not be observed");
    cleanup.join();
    checkEmptyQueue();
}

void noPrematureObjectSignal() {
    Event cleanupEntered;
    Event cleanupRelease;
    Event queueReady;
    Event beginWait;
    Event waitReturned;
    DWORD result = WAIT_FAILED;
    Thread cleanup([&] { cleanupEntered.signal(); cleanupRelease.wait(); });
    Thread waiter([&] {
        SignalOnExit reportReturn{waitReturned};
        createQueue();
        checkEmptyQueue();
        queueReady.signal();
        beginWait.wait();
        result = waitForCleanupOrMessages(cleanup.handle());
        checkEmptyQueue();
    });
    SignalOnExit unblockCleanup{cleanupRelease};
    SignalOnExit unblockWaiter{beginWait};
    cleanupEntered.wait();
    queueReady.wait();
    beginWait.signal();
    check(!waitReturned.waitFor(200), "wait returned before cleanup completion without input");
    check(WaitForSingleObject(cleanup.handle(), 0) == WAIT_TIMEOUT,
        "controlled cleanup finished before release");
    cleanupRelease.signal();
    waitReturned.wait();
    waiter.join();
    check(result == WAIT_OBJECT_0, "released cleanup did not produce object signal");
    check(WaitForSingleObject(cleanup.handle(), 0) == WAIT_OBJECT_0,
        "wait reported object signal before thread handle was signaled");
    cleanup.join();
}

} // namespace

int main() {
    // Bounded failure escape for a broken implementation that waits forever;
    // it never posts a rescue message that could mask the missing-handle bug.
    Event suiteDone;
    Thread watchdog([&] {
        if (!suiteDone.waitFor(timeoutMs)) {
            std::fprintf(stderr, "FAIL suite watchdog: completion wait did not wake\n");
            std::fflush(stderr);
            std::abort();
        }
    });
    SignalOnExit stopWatchdog{suiteDone};
    try {
        createQueue();
        struct Test { const char* name; void (*run)(); };
        const Test tests[] = {
            {"kernel thread completion wakes without messages or timers", completionWithoutAnyMessageOrTimer},
            {"pump thread message while cleanup runs / already-seen input", messagesWhileCleanupUnfinished},
            {"null handle 200ms timeout / cleanup creation retry", noHandleTimeoutAllowsCreationRetry},
            {"no premature object signal / event-controlled release", noPrematureObjectSignal}
        };
        for (const auto& test : tests) {
            std::printf("RUN  %s\n", test.name);
            std::fflush(stdout);
            test.run();
            std::printf("PASS %s\n", test.name);
        }
        checkEmptyQueue();
        std::printf("PASS 4/4 tests (MSVC x64; real queues, threads, handles)\n");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
    } catch (...) {
        std::fprintf(stderr, "FAIL unexpected exception\n");
    }
    // Remove only this test's message if an assertion interrupted its pump.
    MSG message{};
    while (PeekMessageW(&message, nullptr, testMessage, testMessage, PM_REMOVE)) {}
    return 1;
}
