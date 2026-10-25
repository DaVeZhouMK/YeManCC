// Offline unit of the actual header. Each fixture owns a private real thread.
// Timings cover only this local startup wait; no product Start/Stop/kernel proof.
#include "input_capture_startup_wait.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using ymcc::inputCapture::StartupWait;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using Ms = std::chrono::milliseconds;

static_assert(!std::is_copy_constructible_v<StartupWait>);
static_assert(!std::is_move_constructible_v<StartupWait>);
static_assert(std::is_same_v<decltype(std::declval<StartupWait&>().waitForStop(
    std::declval<const std::atomic<bool>&>(), Ms{})), bool>);
static_assert(std::is_same_v<decltype(std::declval<StartupWait&>().requestStop(
    std::declval<std::atomic<bool>&>())), bool>);
static_assert(std::is_same_v<decltype(std::declval<StartupWait&>().reset(
    std::declval<std::atomic<bool>&>())), bool>);

static_assert(noexcept(std::declval<StartupWait&>().requestStop(
    std::declval<std::atomic<bool>&>())));
static_assert(noexcept(std::declval<StartupWait&>().reset(
    std::declval<std::atomic<bool>&>())));

struct Audit {
    unsigned started = 0, joined = 0, fallbackJoins = 0;
};
struct Observation {
    bool stopped = false, protocolOk = false, threw = false, synchronizationOk = false;
    double elapsedMs = 0;
    Clock::time_point ended{};
};

class Capture final {
public:
    explicit Capture(Audit& audit) : audit_(audit) {}
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    ~Capture() {
        // Exception cleanup also releases the gate, requests stop and joins.
        // Successful and specified-negative runs must report ZERO fallback joins.
        if (worker_.joinable()) {
            open();
            stop();
            worker_.join();
            ++audit_.joined;
            ++audit_.fallbackJoins;
        }
    }
    void start(Ms budget) {
        if (worker_.joinable()) throw std::logic_error("unjoined worker");
        {
            std::lock_guard<std::mutex> lock(observationMutex_);
            atBoundary_ = allowed_ = done_ = false;
            observation_ = {};
        }
        worker_ = std::thread([this, budget] {
            Observation row;
            {
                std::unique_lock<std::mutex> lock(observationMutex_);
                atBoundary_ = true;
                observationCv_.notify_all();
                // Bounded fixture handshake, outside the production class.
                row.protocolOk = observationCv_.wait_for(lock, 5000ms,
                    [this] { return allowed_; });
            }
            const auto begin = Clock::now();
            if (row.protocolOk) {
                try { row.stopped = wait_.waitForStop(stop_, budget); }
                catch (...) { row.threw = true; }
            }
            row.ended = Clock::now();
            row.elapsedMs = std::chrono::duration<double, std::milli>(row.ended - begin).count();
            {
                std::lock_guard<std::mutex> lock(observationMutex_);
                observation_ = row;
                done_ = true;
            }
            observationCv_.notify_all();
        });
        ++audit_.started;
    }
    bool boundary() {
        std::unique_lock<std::mutex> lock(observationMutex_);
        return observationCv_.wait_for(lock, 2000ms, [this] { return atBoundary_; });
    }
    void open() {
        {
            std::lock_guard<std::mutex> lock(observationMutex_);
            allowed_ = true;
        }
        observationCv_.notify_all();
    }
    bool doneWithin(Ms budget) {
        std::unique_lock<std::mutex> lock(observationMutex_);
        return observationCv_.wait_for(lock, budget, [this] { return done_; });
    }
    void stop() noexcept {
        if (!wait_.requestStop(stop_)) synchronizationFailures_.fetch_add(1, std::memory_order_relaxed);
    }
    void reset() {
        if (worker_.joinable()) throw std::logic_error("reset before explicit join");
        if (!wait_.reset(stop_)) throw std::runtime_error("reset synchronization failed");
        // Controller is the only publisher; old worker joined.
    }
    bool stopped() const { return stop_.load(std::memory_order_acquire); }
    Observation join() {
        worker_.join(); // Every assertion is made AFTER explicit cleanup.
        ++audit_.joined;
        std::lock_guard<std::mutex> lock(observationMutex_);
        observation_.synchronizationOk = synchronizationFailures_.load(std::memory_order_relaxed) == 0;
        return observation_;
    }
private:
    Audit& audit_;
    StartupWait wait_;
    std::atomic<bool> stop_{false}; // The ONLY stop state, passed to the actual API.
    std::atomic<unsigned> synchronizationFailures_{0}; // Test result, not stop state.
    std::thread worker_;
    std::mutex observationMutex_;
    std::condition_variable observationCv_;
    bool atBoundary_ = false, allowed_ = false, done_ = false; // Test observations only.
    Observation observation_;
};

struct Check {
    std::string name;
    bool pass;
    double elapsedMs;
};
struct Case {
    std::string name;
    std::vector<Check> checks;
    void check(const std::string& label, bool pass, double elapsedMs = 0) {
        checks.push_back({name + "." + label, pass, elapsedMs});
    }
    void observation(const Observation& row) {
        check("worker-completed", row.protocolOk && !row.threw && row.synchronizationOk, row.elapsedMs);
    }
};

static Case beforeWait(Audit& audit) {
    Case test{"stop-before-wait", {}};
    Capture capture(audit);
    capture.stop(); // No waiter exists when notify occurs.
    capture.start(1200ms);
    const bool boundary = capture.boundary();
    capture.open();
    const bool prompt = capture.doneWithin(400ms);
    const auto row = capture.join(); // Mutant is allowed to finish its finite budget.
    test.check("boundary", boundary);
    test.observation(row);
    test.check("stopped", row.stopped && capture.stopped());
    test.check("prompt", prompt && row.elapsedMs < 400, row.elapsedMs);
    return test;
}

static Case duringWait(Audit& audit) {
    Case test{"stop-during-wait", {}};
    Capture capture(audit);
    capture.start(1600ms);
    const bool boundary = capture.boundary();
    capture.open();
    // Public observation only: about-to-wait handshake then a quiet interval.
    // This is not a hook proving the exact condition-variable enrollment instant.
    const bool quiet = !capture.doneWithin(100ms);
    const auto requested = Clock::now();
    capture.stop();
    const bool prompt = capture.doneWithin(400ms);
    const auto row = capture.join();
    const double releaseMs = std::chrono::duration<double, std::milli>(row.ended - requested).count();
    test.check("boundary", boundary);
    test.check("pending-before-stop", quiet);
    test.observation(row);
    test.check("stopped", row.stopped && capture.stopped());
    test.check("prompt", prompt && releaseMs >= 0 && releaseMs < 400, releaseMs);
    return test;
}

static Case normalTimeout(Audit& audit) {
    Case test{"normal-2500ms-timeout", {}};
    Capture capture(audit);
    capture.start(2500ms); // Exact unchanged production startup delay, no kernel consumer.
    const bool boundary = capture.boundary();
    capture.open();
    const auto row = capture.join();
    test.check("boundary", boundary);
    test.observation(row);
    test.check("not-stopped", !row.stopped && !capture.stopped());
    test.check("full-budget", row.elapsedMs >= 2500 && row.elapsedMs < 4500, row.elapsedMs);
    return test;
}

static Case repeatedStop(Audit& audit) {
    Case test{"repeated-stop", {}};
    Capture capture(audit);
    capture.start(1200ms);
    const bool boundary = capture.boundary();
    for (unsigned i = 0; i < 8; ++i) capture.stop();
    capture.open();
    for (unsigned i = 0; i < 8; ++i) capture.stop();
    const bool prompt = capture.doneWithin(400ms);
    const auto row = capture.join();
    for (unsigned i = 0; i < 8; ++i) capture.stop(); // Even after explicit join.
    test.check("boundary", boundary);
    test.observation(row);
    test.check("idempotent", prompt && row.stopped && capture.stopped(), row.elapsedMs);
    return test;
}

static Case resetThenWait(Audit& audit) {
    Case test{"reset-new-wait", {}};
    Capture capture(audit);
    capture.stop();
    capture.start(1200ms);
    const bool firstBoundary = capture.boundary();
    capture.open();
    const auto first = capture.join();
    test.check("old-boundary", firstBoundary);
    test.observation(first);
    test.check("old-stopped", first.stopped);
    capture.reset(); // Old worker explicitly joined; no new worker published yet.
    test.check("cleared", !capture.stopped());
    capture.start(120ms);
    const bool nextBoundary = capture.boundary();
    capture.open();
    const auto next = capture.join();
    test.check("new-boundary", nextBoundary);
    test.observation(next);
    test.check("new-full-budget", !next.stopped && next.elapsedMs >= 120 && next.elapsedMs < 1000,
               next.elapsedMs);
    capture.reset();
    capture.start(1200ms);
    const bool thirdBoundary = capture.boundary();
    capture.open();
    const bool quiet = !capture.doneWithin(50ms);
    capture.stop();
    const bool prompt = capture.doneWithin(400ms);
    const auto third = capture.join();
    test.check("later-boundary", thirdBoundary);
    test.observation(third);
    test.check("new-cancellable", quiet && prompt && third.stopped, third.elapsedMs);
    return test;
}

static Case interleavings(Audit& audit) {
    Case test{"bounded-interleavings", {}};
    Capture capture(audit);
    for (unsigned i = 0; i < 64; ++i) {
        capture.reset(); // Reuse the SAME synchronization only after the previous join.
        const unsigned order = i % 4;
        if (order == 0) capture.stop();
        capture.start(1200ms);
        const bool boundary = capture.boundary();
        if (order == 1) capture.stop();
        capture.open();
        bool quiet = true;
        if (order == 2) capture.stop(); // Race the public call boundary.
        if (order == 3) {
            quiet = !capture.doneWithin(2ms);
            capture.stop();
        }
        const bool prompt = capture.doneWithin(400ms);
        const auto row = capture.join();
        test.check("round-" + std::to_string(i), boundary && quiet && row.protocolOk && !row.threw && row.synchronizationOk &&
                   prompt && row.stopped && capture.stopped(), row.elapsedMs);
    }
    return test;
}

static Case concurrentNotify(Audit& audit) {
    Case test{"concurrent-notify", {}};
    Capture capture(audit);
    for (unsigned round = 0; round < 8; ++round) {
        capture.reset();
        capture.start(1600ms);
        const bool boundary = capture.boundary();
        // Six real stop senders, released together with the worker on alternating
        // rounds; other rounds let its wait remain pending before releasing them.
        std::mutex gateMutex;
        std::condition_variable gateCv;
        bool go = false;
        unsigned ready = 0;
        std::atomic<unsigned> gateTimeouts{0}; // Test result, never a stop flag.
        std::vector<std::thread> senders;
        senders.reserve(6);
        try {
            for (unsigned i = 0; i < 6; ++i) {
                senders.emplace_back([&] {
                    bool released;
                    {
                        std::unique_lock<std::mutex> lock(gateMutex);
                        ++ready;
                        gateCv.notify_all();
                        released = gateCv.wait_for(lock, 5000ms, [&] { return go; });
                    }
                    if (!released) gateTimeouts.fetch_add(1, std::memory_order_relaxed);
                    for (unsigned n = 0; n < 8; ++n) capture.stop();
                });
                ++audit.started;
            }
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(gateMutex);
                go = true;
            }
            gateCv.notify_all();
            for (auto& sender : senders) { sender.join(); ++audit.joined; }
            throw; // Capture's bounded exception cleanup also joins its worker.
        }
        bool allReady;
        {
            std::unique_lock<std::mutex> lock(gateMutex);
            allReady = gateCv.wait_for(lock, 2000ms, [&] { return ready == 6; });
        }
        capture.open();
        bool quiet = true;
        if (round % 2 == 1) quiet = !capture.doneWithin(40ms);
        {
            std::lock_guard<std::mutex> lock(gateMutex);
            go = true;
        }
        gateCv.notify_all();
        const bool prompt = capture.doneWithin(400ms);
        for (auto& sender : senders) { sender.join(); ++audit.joined; }
        const auto row = capture.join();
        test.check("round-" + std::to_string(round), boundary && allReady && quiet && prompt &&
                   gateTimeouts.load(std::memory_order_relaxed) == 0 && row.protocolOk &&
                   !row.threw && row.synchronizationOk && row.stopped && capture.stopped(), row.elapsedMs);
    }
    return test;
}

static Case zeroBudget(Audit& audit) {
    Case test{"zero-budget", {}};
    Capture capture(audit);
    for (unsigned i = 0; i < 2; ++i) {
        capture.reset();
        if (i == 1) capture.stop();
        capture.start(0ms);
        const bool boundary = capture.boundary();
        capture.open();
        const auto row = capture.join();
        test.check("round-" + std::to_string(i), boundary && row.protocolOk && !row.threw && row.synchronizationOk &&
                   row.stopped == (i == 1) && row.elapsedMs < 400, row.elapsedMs);
    }
    return test;
}

int main(int argc, char** argv) {
    Audit audit;
    std::vector<Case> cases;
    bool unexpected = false;
    try {
        const std::string selected = argc == 2 ? argv[1] : "all";
        if (selected == "all" || selected == "stop-before-wait") cases.push_back(beforeWait(audit));
        if (selected == "all" || selected == "stop-during-wait") cases.push_back(duringWait(audit));
        if (selected == "all") {
            cases.push_back(normalTimeout(audit));
            cases.push_back(repeatedStop(audit));
            cases.push_back(resetThenWait(audit));
            cases.push_back(interleavings(audit));
            cases.push_back(concurrentNotify(audit));
            cases.push_back(zeroBudget(audit));
        }
        if (cases.empty() || argc > 2) throw std::invalid_argument("unknown case");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        unexpected = true;
    }
    unsigned checks = 0, failures = 0;
    for (const auto& test : cases) for (const auto& check : test.checks) {
        ++checks;
        if (!check.pass) ++failures;
    }
    const bool allJoined = audit.started == audit.joined && audit.fallbackJoins == 0;
    const bool pass = !unexpected && allJoined && failures == 0 && !cases.empty();
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    std::cout << "{\"pass\":" << boolean(pass) << ",\"checks\":" << checks
              << ",\"failures\":" << failures << ",\"unexpectedException\":" << boolean(unexpected)
              << ",\"allThreadsExplicitlyJoined\":" << boolean(allJoined)
              << ",\"threadsStarted\":" << audit.started << ",\"threadsJoined\":" << audit.joined
              << ",\"fallbackJoins\":" << audit.fallbackJoins << ",\"cases\":[";
    bool firstCase = true;
    for (const auto& test : cases) {
        if (!firstCase) std::cout << ',';
        firstCase = false;
        std::cout << "{\"name\":\"" << test.name << "\",\"checks\":[";
        bool firstCheck = true;
        for (const auto& check : test.checks) {
            if (!firstCheck) std::cout << ',';
            firstCheck = false;
            std::cout << "{\"name\":\"" << check.name << "\",\"pass\":" << boolean(check.pass)
                      << ",\"elapsedMs\":" << check.elapsedMs << '}';
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
    return pass ? 0 : 1;
}
