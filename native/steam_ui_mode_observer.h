#pragma once
// One event-driven UI-mode reader shared by CSS and Decky. No idle timer.
#include "steam_session_observer.h"
#include <json.hpp>
#include <functional>
#include <mutex>
#include <thread>

namespace ymcc::steamui {
using ModeJson = nlohmann::json;
enum class ModeConsumer { Decky, Css };
class ModeReadWindow {
    ULONGLONG due_ = 0, next_ = 0;
    unsigned attempts_ = 0;
    bool armed_ = false;
    DWORD debounce_, gap_, failureGap_, lastGap_ = 0;
    ULONGLONG startupOpened_ = 0;
    unsigned startupReads_ = 0;
    bool startup_ = false;
    // Event-driven catch-up only for a new verified Steam process. Up to six
    // initial read slots can shorten following gaps during the first ten seconds;
    // later slots use the normal cadence. Opening this allowance arms no timer.
    static constexpr unsigned startupLimit = 6;
    static constexpr DWORD startupGapMs = 500, startupWindowMs = 10000;
    bool startupAvailable(ULONGLONG now) const {
        return startup_ && now >= startupOpened_ && now-startupOpened_ < startupWindowMs && startupReads_ < startupLimit;
    }
public:
    ModeReadWindow(DWORD debounce = 600, DWORD gap = 5000, DWORD failureGap = 30000)
        : debounce_(debounce), gap_(gap), failureGap_(failureGap) {}
    void cancel(bool preserveAttempts = false) { armed_ = false; if (!preserveAttempts) attempts_ = 0; } // Keep rate-limit across disable/re-enable.
    void reset(bool preserveStartup = false) {
        cancel(preserveStartup); due_ = 0;
        if (!preserveStartup) { next_ = 0; lastGap_ = gap_; startup_ = false; startupReads_ = 0; }
    }
    void beginStartup(ULONGLONG now) { startup_ = true; startupOpened_ = now; startupReads_ = 0; }
    bool request(ULONGLONG now, bool preserveAttempts = false) {
        if (armed_) return false;
        armed_ = true; if (!preserveAttempts) attempts_ = 0; due_ = (std::max)(now + debounce_, next_); return true;
    }
    DWORD timeout(ULONGLONG now) const {
        return !armed_ ? INFINITE : now >= due_ ? 0 : static_cast<DWORD>((std::min)(due_ - now, ULONGLONG{MAXDWORD - 1}));
    }
    bool take(ULONGLONG now) {
        if (timeout(now)) return false;
        armed_ = false; ++attempts_;
        if (startupAvailable(now)) ++startupReads_;
        lastGap_ = startupAvailable(now) ? (std::min)(gap_, startupGapMs) : gap_;
        next_ = now + lastGap_; return true;
    }
    void finish(ULONGLONG now, bool known, bool transientStartupRetry = false, bool localBusy = false) {
        if (known) { attempts_ = 0; return; }
        // A try-lock miss performs no external IO. Within the finite short
        // startup slots, refund only this attempt (never prior IO failures).
        // Exhausted/expired startup uses the ordinary three slow attempts.
        if (localBusy && lastGap_ < gap_ && attempts_) --attempts_;
        // Keep genuinely unknown IO's slow retry debt in the persistent rate
        // limit too: account updates or demand pause must not discard it.
        if (!transientStartupRetry) next_ = (std::max)(next_, now + gap_);
        // A hint can invalidate verified IO, or a local lock can skip IO. Recheck
        // only these in the finite startup allowance; unknown/failed IO keeps the
        // old slow retries so a slow PC does not spend its budget too early.
        if (attempts_ < 3) { armed_ = true; due_ = (std::max)(now + (transientStartupRetry ? lastGap_ : gap_), next_); }
        else { armed_ = false; next_ = (std::max)(next_, now + failureGap_); }
    }
};
struct ModeObserverConfig {
    // The reader must retain the original verified-process and session checks.
    std::function<ModeJson(const steamsession::Session&)> read;
    std::function<void(const steamsession::Session&, const ModeJson&)> changed;
    DWORD debounceMs = 600, minimumGapMs = 5000, failureGapMs = 30000;
    // Read completion for a page that actually consumed pending; no mode action.
    std::function<void(const steamsession::Session&)> settled;
    // A completed verified Big Picture read may match the last delivered mode
    // after a window hint made admission pending. Only waiting Decky consumes it.
    std::function<void(const steamsession::Session&)> stableBigPicture;
};
class ModeObserver {
    std::mutex lifecycle_, mutex_;
    HANDLE wake_ = nullptr, stop_ = nullptr;
    std::thread worker_;
    ModeObserverConfig config_;
    steamsession::Session session_;
    ModeJson cached_, delivered_;
    ModeReadWindow window_;
    bool stopping_ = false, decky_ = false, css_ = false;
    bool inFlight_ = false, passivePending_ = false;
    unsigned long long generation_ = 0, hintGeneration_ = 0;
    static bool validSession(const steamsession::Session& s) { return s.pid && s.created && !s.root.empty(); }
    bool demand() const { return decky_ || css_; }
    static bool known(const ModeJson& state, const steamsession::Session& session) {
        // JSON numeric conversion to DWORD truncates floats and wraps wide or
        // negative integers. Validate the identity before narrowing anything.
        const auto pid = state.find("pid");
        if (pid == state.end() || !pid->is_number_integer() ||
            (pid->is_number_unsigned() ? pid->get<uint64_t>() != session.pid : pid->get<int64_t>() != session.pid)) return false;
        const auto mode = state.value("mode", std::string{});
        if(state.contains("modeEpoch") && (!state["modeEpoch"].is_string() || state["modeEpoch"].get<std::string>().size()>96))return false;
        return state.value("known", false) && state.value("running", false) &&
            state.value("pid", DWORD{0}) == session.pid && state.value("birth", std::string{}) == std::to_string(session.created) &&
            (mode == "desktop" || mode == "bigpicture");
    }
    void run() {
        for (;;) {
            steamsession::Session session; unsigned long long generation = 0, hints = 0; DWORD timeout = INFINITE;
            bool read = false;
            {
                std::lock_guard lock(mutex_);
                if (stopping_) return;
                if (demand() && validSession(session_) && window_.take(GetTickCount64())) {
                    read = inFlight_ = true; session = session_; generation = generation_; hints = hintGeneration_;
                } else timeout = window_.timeout(GetTickCount64());
            }
            if (!read) {
                HANDLE events[]{stop_, wake_};
                const auto waited = WaitForMultipleObjects(2, events, FALSE, timeout);
                if (waited == WAIT_OBJECT_0 || waited == WAIT_FAILED) return;
                continue;
            }
            ModeJson result;
            try { result = config_.read(session); }
            catch (...) { result = {{"known", false}, {"mode", "unknown"}, {"reason", "shared-mode-read-failed"}}; }
            bool notify = false, settled = false, stableBigPicture = false;
            {
                std::lock_guard lock(mutex_); inFlight_ = false;
                if (stopping_) return;
                // Lifecycle invalidation cannot spend the replacement session/demand budget.
                if (generation != generation_ || !(session == session_) || !demand()) continue;
                const bool invalidated = hints != hintGeneration_;
                bool confirmed = false;
                try { confirmed = result.is_object() && known(result, session); } catch (...) {}
                const bool valid = !invalidated && confirmed;
                // An in-flight hint spends an attempt, without resetting the
                // lifecycle budget. Preserve pending display until its finite
                // follow-up settles or the failure window is exhausted.
                // live-busy is the local try-lock miss in steamui::read: no
                // Steam IO was attempted. It may use remaining startup slots,
                // but never confirms a mode or refills startup credit.
                bool localBusy = false;
                try { localBusy = result.is_object() && result.value("reason", std::string{}) == "live-busy"; } catch (...) {}
                window_.finish(GetTickCount64(), valid, (invalidated && confirmed) || localBusy, localBusy);
                if (invalidated && window_.timeout(GetTickCount64()) != INFINITE) continue;
                if (valid) {
                    notify = !delivered_.is_object() || delivered_.value("mode", std::string{}) != result.value("mode", std::string{}) ||
                        delivered_.value("modeEpoch", std::string{}) != result.value("modeEpoch", std::string{}) ||
                        delivered_.value("deckyUpdateResidue",ModeJson()) != result.value("deckyUpdateResidue",ModeJson()) ||
                        delivered_.value("deckyEmptyFrontend",ModeJson()) != result.value("deckyEmptyFrontend",ModeJson());
                    cached_ = delivered_ = result;
                    stableBigPicture = !notify && result.value("mode", std::string{}) == "bigpicture";
                } else {
                    delivered_ = ModeJson{};
                    std::string reason = invalidated ? "shared-mode-events-unsettled" : "shared-mode-invalid";
                    try { if(!invalidated && result.is_object())reason=result.value("reason",std::string("shared-mode-unconfirmed")); } catch (...) {}
                    cached_ = {{"known", true}, {"running", true}, {"mode", "unknown"}, {"pid", session.pid},
                        {"birth", std::to_string(session.created)}, {"reason", reason}};
                }
                settled = passivePending_ && !notify; passivePending_ = false;
            }
            // No callback under the shared mutex: consumers may call snapshot/setDemand.
            if (notify && config_.changed) { try { config_.changed(session, result); } catch (...) {} }
            if (stableBigPicture && config_.stableBigPicture) { try { config_.stableBigPicture(session); } catch (...) {} }
            if (settled && config_.settled) { try { config_.settled(session); } catch (...) {} }
        }
    }
public:
    ~ModeObserver() { shutdown(); }
    bool initialize(ModeObserverConfig config) {
        std::lock_guard lifecycle(lifecycle_);
        if (worker_.joinable()) return true;
        if (!config.read) return false;
        HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr), stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!wake || !stop) { if (wake) CloseHandle(wake); if (stop) CloseHandle(stop); return false; }
        {
            std::lock_guard lock(mutex_); config_ = std::move(config); stopping_ = false;
            wake_ = wake; stop_ = stop; window_ = ModeReadWindow(config_.debounceMs, config_.minimumGapMs, config_.failureGapMs);
            if (demand() && validSession(session_)) window_.request(GetTickCount64());
        }
        try { worker_ = std::thread([this] { run(); }); }
        catch (...) { std::lock_guard lock(mutex_); CloseHandle(wake_); CloseHandle(stop_); wake_ = stop_ = nullptr; return false; }
        return true;
    }
    void setDemand(ModeConsumer consumer, bool enabled) {
        std::lock_guard lock(mutex_);
        auto& flag = consumer == ModeConsumer::Decky ? decky_ : css_;
        if (stopping_ || flag == enabled) return;
        const bool hadDemand = demand(); flag = enabled;
        if (!demand()) { window_.cancel(true); cached_ = delivered_ = ModeJson{}; passivePending_ = false; ++generation_; }
        else if (!hadDemand && validSession(session_)) window_.request(GetTickCount64(),true);
        if (wake_) SetEvent(wake_);
    }
    void observeSteam(const steamsession::Session& session) {
        std::lock_guard lock(mutex_);
        if (stopping_ || session_ == session) return;
        const bool sameProcess = validSession(session_) && validSession(session) &&
            session_.pid == session.pid && session_.created == session.created && session_.root == session.root;
        session_ = session; cached_ = delivered_ = ModeJson{}; passivePending_ = false; ++generation_;
        // Learning/changing the account in the same process must not refill
        // the short-read allowance during repeated registry notifications.
        window_.reset(sameProcess);
        if (validSession(session_) && !sameProcess) window_.beginStartup(GetTickCount64());
        if (demand() && validSession(session_)) window_.request(GetTickCount64(),sameProcess);
        if (wake_) SetEvent(wake_);
    }
    // Hints from existing Windows events or the Decky 30s recovery timer. No CDP here.
    bool requestRefresh(const steamsession::Session& session) {
        std::lock_guard lock(mutex_);
        if (stopping_ || !demand() || !validSession(session_) || !(session_ == session)) return false;
        if (inFlight_ && window_.timeout(GetTickCount64()) == INFINITE) ++hintGeneration_;
        if (cached_.is_object()) cached_ = ModeJson{}; // Invalidate once; no repeated JSON construction under an event storm.
        // A pending deadline already owns the next wake. Repeated window/name
        // hints must not cause one kernel wake and loop iteration per event.
        const bool newlyArmed = window_.request(GetTickCount64(), inFlight_);
        if (newlyArmed && wake_) SetEvent(wake_); return true;
    }
    ModeJson snapshot(const steamsession::Session& session) {
        std::lock_guard lock(mutex_);
        if (!stopping_ && session_ == session && cached_.is_object()) return cached_;
        return {{"known", validSession(session)}, {"running", validSession(session)}, {"mode", "unknown"},
            {"pid", session.pid}, {"birth", std::to_string(session.created)}, {"reason", "shared-mode-pending"}};
    }
    // Automatic frontend reads join the existing CSS/Decky observation. A
    // pending result must not cause a second CDP reader before our deadline.
    // Null means no enabled observer owns this session; legacy read may proceed.
    ModeJson passiveSnapshot(const steamsession::Session& session) {
        std::lock_guard lock(mutex_);
        if (stopping_ || !demand() || !validSession(session_) || !(session_ == session)) return ModeJson{};
        if (cached_.is_object()) return cached_;
        passivePending_ = true;
        return {{"known", true}, {"running", true}, {"mode", "unknown"},
            {"pid", session.pid}, {"birth", std::to_string(session.created)}, {"reason", "shared-mode-pending"}};
    }
    void shutdown() {
        std::lock_guard lifecycle(lifecycle_);
        { std::lock_guard lock(mutex_); stopping_ = true; if (stop_) SetEvent(stop_); }
        if (worker_.joinable()) worker_.join();
        { std::lock_guard lock(mutex_); if (wake_) CloseHandle(wake_); if (stop_) CloseHandle(stop_);
            wake_ = stop_ = nullptr; session_ = {}; cached_ = delivered_ = ModeJson{}; window_.reset(); passivePending_ = false; ++generation_; }
    }
};
} // namespace ymcc::steamui
