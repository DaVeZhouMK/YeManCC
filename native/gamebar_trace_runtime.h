#pragma once
// Single-TU replacement for the old gamebarTrace* block in native/main.cpp.
// Requires the existing g_exitRequested and appendNativeLifecycleLog declarations.
// Observation / R17 policy only: no keyboard, launch, focus or device operations.
// Shutdown(waitMs) budgets only kernel/thread/callback waits. Activation, property,
// add/remove and COM Release may block: their completion is UNKNOWN until returned.
// A failed wait/close/remove retains its owner and may be retried. No restart after stop.
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#ifndef YMCC_GAMEBAR_TRACE_FAKE_WINRT
#include <winrt/Windows.Gaming.UI.h>
#endif

static std::atomic<bool> g_gamebarTraceInitDone{false};
static std::atomic<bool> g_gamebarTraceAvailable{false};
static std::atomic<int> g_gamebarTraceVisible{-1};
static std::atomic<int> g_gamebarTraceInputRedirected{-1};
static std::atomic<ULONGLONG> g_gamebarTraceObservedTick{0};
static std::atomic<unsigned long long> g_gamebarTraceVersion{0};
static std::atomic<ULONGLONG> g_gamebarTraceLastAttemptTick{0};
static std::atomic<int> g_gamebarTraceInitAttempts{0};
static std::atomic<bool> g_gamebarTraceInitThreadStarted{false};
static constexpr int kGamebarTraceInitAttemptMax = 6;
static constexpr ULONGLONG kGamebarTraceInitRetryMs[] = {2000, 5000, 15000, 30000, 60000, 60000};
static constexpr ULONGLONG kGamebarTraceDemandRetryMs = 60000;

namespace gamebar_trace_detail {
using Statics = winrt::Windows::Gaming::UI::IGameBarStatics;
// Neither callbacks nor the init thread acquire cleanupMx. Never join with resourceMx held.
static std::mutex resourceMx, cleanupMx, publishMx, drainMx;
static std::condition_variable drainCv;
static std::atomic<bool> stop{false};
static std::atomic<unsigned> active{0}; // Actual callback entry, including refused late callbacks.
static HANDLE thread = nullptr;
static HANDLE wake = nullptr;
static Statics factory{nullptr};
static winrt::event_token visibilityToken{}, redirectedToken{};
static bool ownsVisibility = false, ownsRedirected = false;
// Stop is lock-free with respect to the resource lock, even during blocked WinRT init.
// Borrow gate prevents SetEvent racing event CloseHandle. Once closed, it stays closed.
static constexpr unsigned wakeClosed = 0x80000000u;
static std::atomic<unsigned> wakeBorrowers{wakeClosed};
static std::atomic<HANDLE> publishedWake{nullptr};

struct Activity {
    Activity() { active.fetch_add(1, std::memory_order_acq_rel); }
    ~Activity() {
        // Coordinate the zero transition with CV wait, avoiding a lost notification.
        std::lock_guard<std::mutex> lock(drainMx);
        active.fetch_sub(1, std::memory_order_acq_rel);
        drainCv.notify_all();
    }
    bool allowed() const { return !stop.load(std::memory_order_acquire); }
    Activity(const Activity&) = delete;
    Activity& operator=(const Activity&) = delete;
};
static void invalidate() {
    g_gamebarTraceAvailable.store(false, std::memory_order_release);
    g_gamebarTraceInitDone.store(false, std::memory_order_release);
}
static void wakeStop() {
    unsigned n = wakeBorrowers.load(std::memory_order_acquire);
    while (!(n & wakeClosed)) {
        if (wakeBorrowers.compare_exchange_weak(n, n + 1, std::memory_order_acq_rel)) {
            if (HANDLE h = publishedWake.load(std::memory_order_acquire)) (void)SetEvent(h);
            wakeBorrowers.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }
    }
}
}

static void gamebarTraceRequestStop() {
    gamebar_trace_detail::stop.store(true, std::memory_order_release);
    gamebar_trace_detail::invalidate(); // R17 existing availability gate now rejects admission.
    gamebar_trace_detail::wakeStop();   // No WinRT, wait, resource lock, join or log.
}
static void gamebarTraceLog(const char* why) {
    using namespace gamebar_trace_detail;
    std::lock_guard<std::mutex> lock(publishMx);
    if (stop.load(std::memory_order_acquire)) return;
    appendNativeLifecycleLog("gamebar-visibility", {
        {"why", why},
        {"available", g_gamebarTraceAvailable.load(std::memory_order_acquire)},
        {"visible", g_gamebarTraceVisible.load(std::memory_order_acquire)},
        {"inputRedirected", g_gamebarTraceInputRedirected.load(std::memory_order_acquire)},
        {"observedTick", g_gamebarTraceObservedTick.load(std::memory_order_acquire)},
        {"version", g_gamebarTraceVersion.load(std::memory_order_acquire)},
        {"attempts", g_gamebarTraceInitAttempts.load(std::memory_order_acquire)}
    });
}
static void gamebarTraceStoreState(int visible, int redirected) {
    using namespace gamebar_trace_detail;
    std::lock_guard<std::mutex> lock(publishMx);
    if (stop.load(std::memory_order_acquire)) return;
    g_gamebarTraceVisible.store(visible, std::memory_order_release);
    g_gamebarTraceInputRedirected.store(redirected, std::memory_order_release);
    g_gamebarTraceObservedTick.store(GetTickCount64(), std::memory_order_release);
    g_gamebarTraceVersion.fetch_add(1, std::memory_order_acq_rel);
}
namespace gamebar_trace_detail {
static bool readState() {
    using winrt::Windows::Gaming::UI::GameBar;
    const int visible = GameBar::Visible() ? 1 : 0;
    if (stop.load(std::memory_order_acquire)) return false;
    const int redirected = GameBar::IsInputRedirected() ? 1 : 0;
    if (stop.load(std::memory_order_acquire)) return false;
    gamebarTraceStoreState(visible, redirected);
    return !stop.load(std::memory_order_acquire);
}
static void callback(const char* why) noexcept {
    Activity activity; // Must precede the stop check; count genuine in-flight callbacks.
    if (!activity.allowed()) return;
    try { (void)readState(); }
    catch (...) { gamebarTraceStoreState(-1, -1); }
    try { gamebarTraceLog(why); } catch (...) {} // No exception crosses the WinRT delegate.
}
}
static bool gamebarTraceRefresh(const char* why) {
    gamebar_trace_detail::Activity activity;
    if (!activity.allowed() || !g_gamebarTraceInitDone.load(std::memory_order_acquire)) return false;
    try { return gamebar_trace_detail::readState(); }
    catch (...) {
        gamebarTraceStoreState(-1, -1);
        if (why) gamebarTraceLog(why);
        return false;
    }
}
static ULONGLONG gamebarTraceRetryDelayMs(int attempts) {
    const int n = (int)(sizeof(kGamebarTraceInitRetryMs) / sizeof(kGamebarTraceInitRetryMs[0]));
    return kGamebarTraceInitRetryMs[attempts >= 0 && attempts < n ? attempts : n - 1];
}
namespace gamebar_trace_detail {
enum class Attempt { immediate, scheduled, demand };
static bool initAttempt(Attempt source) {
    std::unique_lock<std::mutex> lock(resourceMx, std::try_to_lock);
    if (!lock.owns_lock() || stop.load(std::memory_order_acquire)) return false;
    if (g_gamebarTraceInitDone.load(std::memory_order_acquire)) return true;
    const int attempts = g_gamebarTraceInitAttempts.load(std::memory_order_acquire);
    const ULONGLONG last = g_gamebarTraceLastAttemptTick.load(std::memory_order_acquire);
    if (source == Attempt::demand) {
        if (attempts < kGamebarTraceInitAttemptMax) return false;
        if (last != 0 && GetTickCount64() - last < kGamebarTraceDemandRetryMs) return false;
    } else {
        if (attempts >= kGamebarTraceInitAttemptMax) return false;
        if (source == Attempt::scheduled && last != 0 &&
            GetTickCount64() - last < gamebarTraceRetryDelayMs(attempts)) return false;
    }
    g_gamebarTraceLastAttemptTick.store(GetTickCount64(), std::memory_order_release);
    g_gamebarTraceInitAttempts.fetch_add(1, std::memory_order_acq_rel);
    try {
        using winrt::Windows::Gaming::UI::GameBar;
        if (!factory) factory = winrt::get_activation_factory<GameBar, Statics>();
        if (stop.load(std::memory_order_acquire) || !readState()) return false;
        // Persist each token immediately. A failed second add retains the first;
        // retries reuse this exact factory and subscribe ONLY the missing event.
        if (!ownsVisibility) {
            visibilityToken = factory.VisibilityChanged([](auto&&, auto&&) {
                callback("visibility-changed");
            });
            ownsVisibility = true;
        }
        if (stop.load(std::memory_order_acquire)) return false;
        if (!ownsRedirected) {
            redirectedToken = factory.IsInputRedirectedChanged([](auto&&, auto&&) {
                callback("input-redirect-changed");
            });
            ownsRedirected = true;
        }
        if (stop.load(std::memory_order_acquire)) return false;
        g_gamebarTraceAvailable.store(true, std::memory_order_release);
        g_gamebarTraceInitDone.store(true, std::memory_order_release);
        // RequestStop cannot wait for this lock. Resolve a concurrent publication
        // in favour of cancellation before this admitted operation returns.
        if (stop.load(std::memory_order_acquire)) { invalidate(); return false; }
        gamebarTraceLog("init");
        return !stop.load(std::memory_order_acquire);
    } catch (...) {
        if (!stop.load(std::memory_order_acquire)) {
            g_gamebarTraceAvailable.store(false, std::memory_order_release);
            gamebarTraceLog("init-failed");
        }
        return false;
    }
}
}
static bool gamebarTraceInitAttempt() {
    return gamebar_trace_detail::initAttempt(gamebar_trace_detail::Attempt::immediate);
}
static void gamebarTraceEnsureInit() {
    (void)gamebar_trace_detail::initAttempt(gamebar_trace_detail::Attempt::scheduled);
}
static void gamebarTraceEnsureInitIfDue() {
    (void)gamebar_trace_detail::initAttempt(gamebar_trace_detail::Attempt::demand);
}
static void gamebarTraceInitThreadMain() {
    using namespace gamebar_trace_detail;
    struct Apartment {
        HRESULT result;
        ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    };
    const Apartment apartment{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    while (!stop.load(std::memory_order_acquire) && !g_exitRequested.load(std::memory_order_acquire)) {
        const int attempt = g_gamebarTraceInitAttempts.load(std::memory_order_acquire);
        if (attempt >= kGamebarTraceInitAttemptMax || gamebarTraceInitAttempt()) break;
        const ULONGLONG wait = gamebarTraceRetryDelayMs(attempt);
        // Keep the original 250 ms exit check, but stop wakes it immediately.
        for (ULONGLONG elapsed = 0; elapsed < wait; elapsed += 250) {
            if (stop.load(std::memory_order_acquire) || g_exitRequested.load(std::memory_order_acquire)) break;
            const DWORD result = WaitForSingleObject(wake, 250);
            if (result != WAIT_TIMEOUT) break;
        }
    }
}
namespace gamebar_trace_detail {
static DWORD WINAPI threadEntry(void*) noexcept {
    try { gamebarTraceInitThreadMain(); } catch (...) {} // Owner retained until signalled and closed.
    return 0;
}
}
static void gamebarTraceStartInitThread() {
    using namespace gamebar_trace_detail;
    std::unique_lock<std::mutex> lock(resourceMx, std::try_to_lock);
    if (!lock.owns_lock() || stop.load(std::memory_order_acquire) || thread ||
        g_gamebarTraceInitThreadStarted.load(std::memory_order_acquire)) return;
    if (!wake) {
        wake = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!wake) return;
        publishedWake.store(wake, std::memory_order_release);
        wakeBorrowers.store(0, std::memory_order_release);
    }
    if (stop.load(std::memory_order_acquire)) return; // Event remains owned for Shutdown.
    thread = CreateThread(nullptr, 0, threadEntry, nullptr, 0, nullptr);
    if (!thread) return; // Keep the event; caller can retry or shut down.
    g_gamebarTraceInitThreadStarted.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> publish(publishMx);
    if (!stop.load(std::memory_order_acquire)) {
        appendNativeLifecycleLog("gamebar-visibility", {
            {"why", "init-thread-started"},
            {"available", false}, {"visible", -1}, {"inputRedirected", -1},
            {"observedTick", 0ULL}, {"version", 0ULL}, {"attempts", 0},
            {"policy", "independent-init-not-abnormal-sampling"}
        });
    }
}

static bool gamebarTraceStopped() {
    using namespace gamebar_trace_detail;
    std::unique_lock<std::mutex> lock(resourceMx, std::try_to_lock);
    return lock.owns_lock() && !thread && !wake && !factory &&
        !ownsVisibility && !ownsRedirected && active.load(std::memory_order_acquire) == 0 &&
        (wakeBorrowers.load(std::memory_order_acquire) & ~wakeClosed) == 0;
}
// waitMs bounds waits only (one shared budget, not a fresh budget per resource).
// This call can itself block in WinRT removal / COM Release. It must run on a
// COM-initialized caller. A blocked init/property call keeps owner + Stopped=false.
static bool gamebarTraceShutdown(DWORD waitMs = 0) {
    using namespace gamebar_trace_detail;
    gamebarTraceRequestStop();
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(waitMs == INFINITE ? INFINITE - 1 : waitMs);
    const auto remaining = [&]() -> DWORD {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return 0;
        return static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
    };
    std::unique_lock<std::mutex> cleanup(cleanupMx, std::try_to_lock);
    if (!cleanup.owns_lock()) return false;
    HANDLE joinTarget = nullptr;
    {
        std::unique_lock<std::mutex> lock(resourceMx, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        joinTarget = thread;
    }
    // No resource lock while waiting: init needs it to finish (including COM uninit).
    if (joinTarget && WaitForSingleObject(joinTarget, remaining()) != WAIT_OBJECT_0) return false;
    {
        std::unique_lock<std::mutex> lock(resourceMx, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        if (thread) {
            if (!CloseHandle(thread)) return false;
            thread = nullptr;
        }
        // The projection's noexcept remove discards HRESULT. Use the original
        // factory ABI so removal failure retains the exact token for a retry.
        bool removed = true;
        if (ownsVisibility) {
            const HRESULT hr = static_cast<winrt::impl::abi_t<Statics>*>(winrt::get_abi(factory))
                ->remove_VisibilityChanged(visibilityToken);
            if (SUCCEEDED(hr)) { ownsVisibility = false; visibilityToken = {}; }
            else removed = false;
        }
        if (ownsRedirected) {
            const HRESULT hr = static_cast<winrt::impl::abi_t<Statics>*>(winrt::get_abi(factory))
                ->remove_IsInputRedirectedChanged(redirectedToken);
            if (SUCCEEDED(hr)) { ownsRedirected = false; redirectedToken = {}; }
            else removed = false;
        }
        if (!removed) return false;
    }
    {
        std::unique_lock<std::mutex> drain(drainMx, std::try_to_lock);
        if (!drain.owns_lock()) return false;
        if (!drainCv.wait_until(drain, deadline, [] { return active.load(std::memory_order_acquire) == 0; })) return false;
    }
    std::unique_lock<std::mutex> lock(resourceMx, std::try_to_lock);
    if (!lock.owns_lock() || active.load(std::memory_order_acquire) != 0) return false;
    factory = nullptr; // Tokens removed and admitted callbacks/refreshes drained.
    if (wake) {
        unsigned expected = 0;
        if (!wakeBorrowers.compare_exchange_strong(expected, wakeClosed, std::memory_order_acq_rel) &&
            expected != wakeClosed) return false;
        if (!CloseHandle(wake)) return false; // Owner and closed borrow gate retained.
        publishedWake.store(nullptr, std::memory_order_release);
        wake = nullptr;
    }
    return !thread && !wake && !factory && !ownsVisibility && !ownsRedirected &&
        active.load(std::memory_order_acquire) == 0;
}
