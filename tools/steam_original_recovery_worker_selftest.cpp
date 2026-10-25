// Standalone MSVC x64 selftest; no application/runtime or Steam dependencies.
// Build: cl /nologo /std:c++17 /EHsc /W4 /WX /permissive- /utf-8 /MD this.cpp
// Optional first argument: existing directory for uniquely owned temporary logs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "../native/steam_original_recovery_worker.h"

// Fail one allocation on the launching test thread, inside the real start().
// This exercises rollback from std::thread construction without exhausting OS
// resources or replacing the production thread implementation with a fake.
namespace {
thread_local bool failNextAllocation = false;
}
void* operator new(std::size_t size) {
    if (failNextAllocation) {
        failNextAllocation = false;
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size ? size : 1)) {
        return memory;
    }
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
using ymcc::steamOriginal::RecoveryWorker;
using Clock = std::chrono::steady_clock;
constexpr DWORD timeoutMs = 5000;
constexpr auto immediateBudget = std::chrono::milliseconds(250);

static_assert(sizeof(void*) == 8, "Run this selftest as x64");
static_assert(!std::is_copy_constructible_v<RecoveryWorker>);
static_assert(!std::is_move_constructible_v<RecoveryWorker>);
static_assert(std::is_nothrow_destructible_v<RecoveryWorker>);
static_assert(noexcept(std::declval<const RecoveryWorker&>().finished()));

void check(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template<class Exception, class Fn>
void expectThrow(Fn&& fn, const char* message) {
    bool caught = false;
    try {
        fn();
    } catch (const Exception&) {
        caught = true;
    }
    check(caught, message);
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
    bool waitFor(DWORD ms) const noexcept {
        return WaitForSingleObject(handle_, ms) == WAIT_OBJECT_0;
    }
    void wait() const { check(waitFor(timeoutMs), "event wait timed out"); }
private:
    HANDLE handle_;
};

struct SignalOnExit {
    const Event& event;
    ~SignalOnExit() noexcept { event.signal(); }
};

// Collect thread failures for the test owner; always join every helper thread.
class Threads {
public:
    ~Threads() noexcept { joinAll(); }
    template<class Fn>
    void launch(Fn&& fn) {
        threads_.emplace_back([this, task = std::forward<Fn>(fn)]() mutable {
            try {
                task();
            } catch (...) {
                std::lock_guard<std::mutex> lock(errorMx_);
                if (!error_) {
                    error_ = std::current_exception();
                }
            }
        });
    }
    void finish() {
        joinAll();
        if (error_) {
            std::rethrow_exception(error_);
        }
    }
private:
    void joinAll() noexcept {
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
    std::vector<std::thread> threads_;
    std::mutex errorMx_;
    std::exception_ptr error_;
};

void waitFinished(const RecoveryWorker& worker) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!worker.finished()) {
        check(Clock::now() < deadline, "completion observation timed out");
        std::this_thread::yield();
    }
}

void checkUnfinishedImmediately(RecoveryWorker& worker) {
    const auto before = Clock::now();
    const bool finished = worker.finished();
    const bool joined = worker.tryJoin();
    const auto elapsed = Clock::now() - before;
    check(!finished && !joined, "blocked task must remain unfinished/unjoined");
    check(elapsed < immediateBudget, "finished/tryJoin blocked on a running task");
}

std::wstring tempBase;
std::atomic<unsigned> nextDirectory{0};

class TempLog {
public:
    TempLog() {
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            directory = tempBase + L"\\recovery-worker-selftest-" +
                std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(nextDirectory.fetch_add(1));
            if (CreateDirectoryW(directory.c_str(), nullptr)) {
                file = directory + L"\\last-event.log";
                return;
            }
            check(GetLastError() == ERROR_ALREADY_EXISTS, "CreateDirectoryW failed");
        }
        throw std::runtime_error("cannot allocate unique temporary log directory");
    }
    ~TempLog() noexcept {
        // Only these exact paths created by this test; never recursive deletion.
        DeleteFileW(file.c_str());
        RemoveDirectoryW(directory.c_str());
    }
    void assertLocked() const {
        check(!DeleteFileW(file.c_str()), "live log unexpectedly allowed deletion");
        check(GetLastError() == ERROR_SHARING_VIOLATION,
            "live log did not report sharing violation");
    }
    void verifyAndDelete() const {
        HANDLE reader = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(reader != INVALID_HANDLE_VALUE, "cannot read final log");
        char data[32]{};
        DWORD count = 0;
        const bool read = ReadFile(reader, data, sizeof(data), &count, nullptr) != FALSE;
        const bool closed = CloseHandle(reader) != FALSE;
        check(read && closed && std::string(data, count) == "final log\n",
            "final log was not written before completion");
        check(DeleteFileW(file.c_str()) != FALSE,
            "finished was true while final log handle remained open");
        check(RemoveDirectoryW(directory.c_str()) != FALSE,
            "log directory could not be deleted before join");
    }
    std::wstring directory;
    std::wstring file;
};

// Its destructor deliberately blocks to distinguish fn return from destruction
// of the std::function's captures. The final Win32 write/close occurs last.
struct CapturedLog {
    CapturedLog(const std::wstring& path, const Event& entered,
        const Event& release, std::atomic<bool>& closed, std::atomic<bool>& ok)
        : entered(entered), release(release), closed(closed), ok(ok) {
        handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(handle != INVALID_HANDLE_VALUE, "CreateFileW for capture failed");
    }
    ~CapturedLog() noexcept {
        entered.signal();
        const bool released = release.waitFor(timeoutMs);
        constexpr char last[] = "final log\n";
        DWORD written = 0;
        const bool wrote = WriteFile(handle, last, sizeof(last) - 1,
            &written, nullptr) != FALSE && written == sizeof(last) - 1;
        const bool didClose = CloseHandle(handle) != FALSE;
        ok.store(released && wrote && didClose, std::memory_order_release);
        closed.store(didClose, std::memory_order_release);
    }
    HANDLE handle = INVALID_HANDLE_VALUE;
    const Event& entered;
    const Event& release;
    std::atomic<bool>& closed;
    std::atomic<bool>& ok;
};

struct StackReleased {
    std::atomic<bool>& released;
    ~StackReleased() noexcept { released.store(true, std::memory_order_release); }
};

void neverStarted() {
    {
        RecoveryWorker never;
        check(never.finished(), "never-started worker must be finished");
        never.join();
        never.join();
        check(never.tryJoin(), "never-started tryJoin must succeed");
        never.rethrowIfFailed();
    } // Destruct a worker that has never launched any thread.
    RecoveryWorker worker;
    worker.join();
    // join() before start() must not consume the one successful launch.
    int result = 0;
    worker.start([&] { result = 7; });
    worker.join();
    check(worker.finished() && result == 7, "start after idle joins failed");
}

void failedStartCanRetry() {
    RecoveryWorker worker;
    expectThrow<std::invalid_argument>([&] { worker.start({}); },
        "empty task was accepted");
    check(worker.finished(), "empty task consumed completion state");
    worker.rethrowIfFailed();

    std::atomic<int> calls{0};
    std::function<void()> task = [&] { ++calls; };
    bool allocationFailed = false;
    failNextAllocation = true;
    try {
        worker.start(std::move(task));
    } catch (const std::bad_alloc&) {
        allocationFailed = true;
    } catch (...) {
        failNextAllocation = false;
        throw;
    }
    const bool injectionConsumed = !failNextAllocation;
    failNextAllocation = false;
    check(allocationFailed && injectionConsumed, "thread construction failure not injected");
    check(worker.finished() && calls.load() == 0,
        "failed launch ran task or left finished false");
    check(worker.tryJoin(), "failed launch left a joinable/blocked thread");
    worker.rethrowIfFailed();
    worker.start([&] { ++calls; });
    worker.join();
    check(worker.finished() && calls.load() == 1, "failed launch was not retryable");
}

void startsOnlyOnce() {
    Event entered;
    Event cancel;
    std::atomic<int> calls{0};
    RecoveryWorker worker;
    SignalOnExit unblock{cancel};
    worker.start([&] { ++calls; entered.signal(); cancel.wait(); });
    entered.wait();
    checkUnfinishedImmediately(worker);
    expectThrow<std::logic_error>([&] { worker.start([&] { ++calls; }); },
        "second start while running was accepted");
    expectThrow<std::logic_error>([&] { worker.rethrowIfFailed(); },
        "pending exception diagnostics were not rejected");
    cancel.signal();
    worker.join();
    worker.join();
    check(worker.tryJoin(), "tryJoin after repeated joins failed");
    expectThrow<std::logic_error>([&] { worker.start([&] { ++calls; }); },
        "restart after join was accepted");
    check(calls.load() == 1, "task executed more than once");
}

void logCleanupBeforeCompletion(bool throws) {
    TempLog log;
    Event entered;
    Event cancel;
    Event captureCleanupEntered;
    Event allowCaptureCleanup;
    std::atomic<bool> stackReleased{false};
    std::atomic<bool> handleClosed{false};
    std::atomic<bool> cleanupOk{false};
    RecoveryWorker worker;
    SignalOnExit unblockTask{cancel};
    SignalOnExit unblockCapture{allowCaptureCleanup};
    worker.start([&, resource = std::make_shared<CapturedLog>(log.file,
        captureCleanupEntered, allowCaptureCleanup, handleClosed, cleanupOk)] {
        (void)resource;
        StackReleased local{stackReleased};
        entered.signal();
        cancel.wait();
        if (throws) {
            throw std::runtime_error("task failure with final log");
        }
    });
    entered.wait();
    checkUnfinishedImmediately(worker);
    log.assertLocked();
    cancel.signal(); // Cooperative cancellation of this test task only.
    captureCleanupEntered.wait();
    check(stackReleased.load(std::memory_order_acquire), "fn scope was not unwound");
    checkUnfinishedImmediately(worker);
    check(!handleClosed.load(std::memory_order_acquire), "cleanup gate was bypassed");
    log.assertLocked();
    allowCaptureCleanup.signal();
    waitFinished(worker);
    check(handleClosed.load(std::memory_order_acquire) &&
        cleanupOk.load(std::memory_order_acquire), "final capture log close failed");
    // Crucial regression: remove the final log AND its directory before any join.
    log.verifyAndDelete();
    worker.join();
    worker.join();
    if (throws) {
        for (int i = 0; i < 2; ++i) {
            bool matched = false;
            try {
                worker.rethrowIfFailed();
            } catch (const std::runtime_error& error) {
                matched = std::string(error.what()) == "task failure with final log";
            }
            check(matched, "task exception was not preserved for repeated rethrow");
        }
    } else {
        worker.rethrowIfFailed();
    }
}

void nonstandardException() {
    RecoveryWorker worker;
    worker.start([] { throw 73; });
    waitFinished(worker);
    bool matched = false;
    try {
        worker.rethrowIfFailed();
    } catch (int value) {
        matched = value == 73;
    }
    check(matched, "catch-all did not preserve a non-std exception before join");
    check(worker.tryJoin(), "throwing task could not be joined");
    worker.join();
}

void multipleObserversAndJoiners() {
    Event entered;
    Event cancel;
    Event observeCompletion;
    std::mutex readyMx;
    std::condition_variable readyCv;
    int ready = 0;
    int published = 0; // Read only after the finished() acquire, without a join.
    std::atomic<int> joined{0};
    RecoveryWorker worker;
    Threads helpers;
    SignalOnExit unblockTask{cancel};
    SignalOnExit unblockObservers{observeCompletion};
    worker.start([&] { entered.signal(); cancel.wait(); published = 1234; });
    entered.wait();
    for (int i = 0; i < 8; ++i) {
        helpers.launch([&] {
            checkUnfinishedImmediately(worker);
            {
                std::lock_guard<std::mutex> lock(readyMx);
                ++ready;
            }
            readyCv.notify_one();
            observeCompletion.wait();
            waitFinished(worker);
            check(published == 1234, "finished did not publish task writes");
            for (int n = 0; n < 3; ++n) {
                worker.join();
                ++joined;
            }
        });
    }
    {
        std::unique_lock<std::mutex> lock(readyMx);
        check(readyCv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
            [&] { return ready == 8; }), "observers did not reach barrier");
    }
    for (int i = 0; i < 4; ++i) {
        helpers.launch([&] { worker.join(); ++joined; });
    }
    checkUnfinishedImmediately(worker);
    cancel.signal();
    observeCompletion.signal();
    helpers.finish();
    check(joined.load() == 28, "not all concurrent/repeated joins completed");
    check(worker.finished() && worker.tryJoin(), "joined state is not stable");
    worker.rethrowIfFailed();
}

// A real TLS destructor holds the OS thread alive AFTER fn/capture completion.
// It probes tryJoin from the worker: before the owner locks, self-join throws the
// standard deadlock error; once tryJoin returns false, the owner's join mutex is
// provably held until this destructor's release event. No private test hook.
struct ExitTailState {
    RecoveryWorker* worker = nullptr;
    Event entered;
    Event beginProbe;
    Event lockObserved;
    Event release;
    std::atomic<bool> observed{false};
    std::atomic<bool> ok{true};
};

struct ExitTailProbe {
    ExitTailState* state = nullptr;
    ~ExitTailProbe() noexcept {
        if (!state) {
            return;
        }
        state->entered.signal();
        if (!state->beginProbe.waitFor(timeoutMs)) {
            state->ok.store(false);
        }
        bool observed = false;
        try {
            const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
            while (Clock::now() < deadline) {
                try {
                    if (!state->worker->tryJoin()) {
                        observed = true;
                        break;
                    }
                    state->ok.store(false);
                    break;
                } catch (const std::system_error& error) {
                    if (error.code() != std::errc::resource_deadlock_would_occur) {
                        throw;
                    }
                }
                std::this_thread::yield();
            }
        } catch (...) {
            state->ok.store(false);
        }
        state->observed.store(observed, std::memory_order_release);
        state->lockObserved.signal();
        if (!state->release.waitFor(timeoutMs)) {
            state->ok.store(false);
        }
    }
};

void finishedAndTryJoinWhileJoinMutexHeld() {
    ExitTailState state;
    RecoveryWorker worker;
    Threads inspector;
    SignalOnExit unblockTail{state.release};
    state.worker = &worker;
    Event inspectorReady;
    inspector.launch([&] {
        SignalOnExit release{state.release};
        inspectorReady.signal();
        state.lockObserved.wait();
        check(state.observed.load(std::memory_order_acquire), "join lock not observed");
        const auto before = Clock::now();
        const bool finished = worker.finished();
        const bool joined = worker.tryJoin();
        const auto elapsed = Clock::now() - before;
        check(finished, "finished did not precede TLS/OS exit");
        check(!joined, "tryJoin did not reject busy join mutex");
        check(elapsed < immediateBudget, "UI observation waited behind blocking join");
    });
    // Start the inspector before TLS teardown: Windows executes TLS destructors
    // under the loader lock, so a newly created inspector cannot start there.
    inspectorReady.wait();
    worker.start([&] {
        static thread_local ExitTailProbe tail;
        tail.state = &state;
    });
    state.entered.wait();
    // Holding joinMx_ while waiting for the TLS destructor creates a deterministic
    // contended join, independent of which thread the scheduler runs first.
    state.beginProbe.signal();
    worker.join();
    inspector.finish();
    check(state.ok.load(), "TLS exit probe failed or timed out");
    check(worker.tryJoin(), "tryJoin failed after the contended join");
}

void destructorJoinsWithoutRethrowing(bool throws) {
    Event entered;
    Event cancel;
    Event disposing;
    Event disposed;
    std::atomic<bool> released{false};
    auto worker = std::make_unique<RecoveryWorker>();
    worker->start([&] {
        StackReleased local{released};
        entered.signal();
        cancel.wait();
        if (throws) {
            throw std::runtime_error("destructor must not rethrow task failure");
        }
    });
    entered.wait();
    Threads owner;
    SignalOnExit unblock{cancel};
    owner.launch([&, owned = std::move(worker)]() mutable {
        disposing.signal();
        owned.reset();
        check(released.load(std::memory_order_acquire), "destructor returned before task cleanup");
        disposed.signal();
    });
    disposing.wait();
    check(!disposed.waitFor(0), "destructor returned while task was blocked");
    cancel.signal();
    disposed.wait();
    owner.finish();
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc > 1) {
            tempBase = argv[1];
        } else {
            wchar_t path[MAX_PATH + 1]{};
            const DWORD count = GetTempPathW(MAX_PATH, path);
            check(count != 0 && count < MAX_PATH, "GetTempPathW failed");
            tempBase = path;
        }
        const DWORD attributes = GetFileAttributesW(tempBase.c_str());
        check(attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY), "temporary base must already exist");
        struct Test { const char* name; void (*run)(); };
        const Test tests[] = {
            {"never started / join before start", neverStarted},
            {"launch allocation failure / empty task / retry", failedStartCanRetry},
            {"one successful start / pending diagnostics / repeated join", startsOnlyOnce},
            {"cancel / final Win32 log and capture destruction before finished",
                [] { logCleanupBeforeCompletion(false); }},
            {"throw / final Win32 log and capture destruction before finished",
                [] { logCleanupBeforeCompletion(true); }},
            {"non-std exception / explicit rethrow before join", nonstandardException},
            {"8 observers / 4 blocking joiners / 28 serialized joins",
                multipleObserversAndJoiners},
            {"finished and tryJoin during held join mutex / real TLS exit tail",
                finishedAndTryJoinWhileJoinMutexHeld},
            {"destructor blocks for cooperative task cleanup",
                [] { destructorJoinsWithoutRethrowing(false); }},
            {"destructor joins throwing task without rethrow",
                [] { destructorJoinsWithoutRethrowing(true); }}
        };
        unsigned passed = 0;
        for (const auto& test : tests) {
            std::printf("RUN  %s\n", test.name);
            std::fflush(stdout);
            test.run();
            ++passed;
            std::printf("PASS %s\n", test.name);
        }
        std::printf("PASS %u/%zu tests (MSVC x64; real threads and Win32 handles)\n",
            passed, sizeof(tests) / sizeof(tests[0]));
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
    } catch (...) {
        std::fprintf(stderr, "FAIL unexpected non-std exception\n");
    }
    return 1;
}
