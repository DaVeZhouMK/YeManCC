#pragma once
#include "worker_thread_name.h"
// One event-driven UI-mode reader shared by CSS and Decky. No idle timer.
#include "steam_session_observer.h"
#include "steam_account_facts.h"
#include "steam_ui_mode_cache_miss.h"
#include "steam_ui_epoch.h"
#include "sleep_power_state.h"
#include <json.hpp>
#include <functional>
#include <mutex>
#include <source_location>
#include <thread>

namespace ymcc::steamui {
using ModeJson = nlohmann::json;
using ModeReadCurrent = std::function<bool()>;
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
    bool withinReadGap(ULONGLONG now) const { return now < next_; }
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
// Mode-only receipt. Session projection is not an IdentitySnapshot or action gate.
struct ModeSourceSnapshot {
    steamsession::Session session;
    ModeJson modeFacts;
    steamaccount::UiAccount uiAccount;
    steamaccount::Selection uiLocal;
    std::uint64_t modeRevision = 0;
    std::string mode, modeEpoch;
};
struct ModeSourceEventDiagnostics {
    std::uint64_t created = 0, closed = 0, createFailed = 0, signalFailed = 0, closeFailed = 0;
    DWORD lastError = ERROR_SUCCESS;
    std::uint64_t publicationFailed = 0, workerWaitFailed = 0;
};
namespace modesourcedetail {
struct EventDiagnostics {
    std::atomic<std::uint64_t> created{0}, closed{0}, createFailed{0}, signalFailed{0}, closeFailed{0};
    std::atomic<DWORD> lastError{ERROR_SUCCESS};
    std::atomic<std::uint64_t> publicationFailed{0}, workerWaitFailed{0};
};
struct State {
    std::shared_ptr<const ModeSourceSnapshot> snapshot;
    std::shared_ptr<EventDiagnostics> diagnostics;
    std::atomic<bool> revoked{false};
    HANDLE event = nullptr;
    explicit State(std::shared_ptr<EventDiagnostics> status) : diagnostics(std::move(status)) {
        event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event) ++diagnostics->created;
        else { ++diagnostics->createFailed; diagnostics->lastError = GetLastError(); }
    }
    ~State() {
        if (!event) return;
        if (CloseHandle(event)) ++diagnostics->closed;
        else { ++diagnostics->closeFailed; diagnostics->lastError = GetLastError(); }
    }
    void revoke() noexcept {
        if (revoked.exchange(true, std::memory_order_acq_rel)) return;
        if (event && !SetEvent(event)) { ++diagnostics->signalFailed; diagnostics->lastError = GetLastError(); }
    }
};
}
class ModeObserver;
class ModeSourceLease {
    friend class ModeObserver;
    std::shared_ptr<modesourcedetail::State> state_;
    explicit ModeSourceLease(std::shared_ptr<modesourcedetail::State> state) : state_(std::move(state)) {}
public:
    ModeSourceLease() = default;
    explicit operator bool() const noexcept { return state_ && state_->snapshot; }
    const ModeSourceSnapshot& snapshot() const noexcept {
        static const ModeSourceSnapshot unknown;
        return state_ && state_->snapshot ? *state_->snapshot : unknown;
    }
    bool current() const noexcept {
        return state_ && state_->event && !state_->revoked.load(std::memory_order_acquire);
    }
    // Borrowed until the last copy of this lease is released. Never reset/close it.
    HANDLE cancellationEvent() const noexcept { return state_ ? state_->event : nullptr; }
};
struct ModeReadSuperseded {
    const char* hintCause = nullptr;
    std::uint32_t hintOriginLine = 0;
    ULONGLONG readStartedTick = 0, hintCount = 0;
    DWORD nextReadMs = 0;
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
    // Metadata can arrive late/out of order. Consumers reject older revisions.
    std::function<void(std::shared_ptr<const ModeSourceSnapshot>)> sourceChanged;
    std::function<void(std::uint64_t)> sourceInvalidated;
    // Synchronous owned reader. The borrowed predicate is pure and lock-free;
    // generation includes every session change, including a change back.
    // Legacy fixture readers keep the original one-argument callback.
    std::function<ModeJson(const steamsession::Session&, const ModeReadCurrent&)> readOwned;
    // Optional Ready provenance for admission coalescing; not a new read gate.
    sleeppower::Reader admissionPowerReader = nullptr;
    // A confirmed Big Picture read was discarded. INFINITE means no follow-up is armed.
    std::function<void(const steamsession::Session&, const ModeReadSuperseded&)> deckyReadSuperseded;
    // Nonblocking CSS mailbox only, after verified cache publication and before
    // source subscribers. The sample is a hint, never an admission lease.
    std::function<void(const steamsession::Session&, const ModeJson&, const ModeCacheTiming&, bool stable)> cssBigPicturePublished;
};
class ModeObserver {
    std::mutex lifecycle_, mutex_;
    HANDLE wake_ = nullptr, stop_ = nullptr;
    std::thread worker_;
    ModeObserverConfig config_;
    steamsession::Session session_;
    ModeJson cached_, delivered_;
    sleeppower::SharedPowerV1 cachedPower_, queuedResumePower_, queuedDeckyWake_;
    sleeppower::SharedPowerV1 inFlightPower_;
    ModeCacheMiss cacheMiss_;
    ModeCacheTiming cachedTiming_;
    std::uint64_t cacheMissSequence_ = 0;
    std::uint64_t publicationSequence_ = 0; // Only accepted reads, including unchanged facts.
    ULONGLONG publicationObservationTick_ = 0;
    ModeReadWindow window_;
    // Transport calls readCurrent while holding its own mutex. Never take the
    // mode mutex there: these are projections of the original locked state.
    std::atomic<bool> stopping_{false}, decky_{false}, css_{false};
    bool inFlight_ = false, inFlightDeckyWake_ = false, passivePending_ = false;
    std::atomic<unsigned long long> generation_{0};
    unsigned long long hintGeneration_ = 0;
    unsigned long long inFlightGeneration_ = 0, inFlightHints_ = 0;
    ModeReadSuperseded readDiscard_;
    ModeCacheMiss cachedLocalReadMiss_;
    std::shared_ptr<modesourcedetail::EventDiagnostics> sourceDiagnostics_ = std::make_shared<modesourcedetail::EventDiagnostics>();
    std::shared_ptr<modesourcedetail::State> source_;
    std::uint64_t sourceRevision_ = 0;
    struct SourceCallbacks {
        std::function<void(std::shared_ptr<const ModeSourceSnapshot>)> changed;
        std::function<void(std::uint64_t)> invalidated;
    };
    std::shared_ptr<const SourceCallbacks> sourceCallbacks_;
    struct SourceNotice {
        std::uint64_t invalidated = 0;
        std::shared_ptr<const ModeSourceSnapshot> changed;
        std::shared_ptr<const SourceCallbacks> callbacks;
    };
    void noteCacheClearLocked(const char* reason, std::uint32_t originLine = 0) noexcept {
        cacheMiss_ = {reason, originLine, ++cacheMissSequence_};
    }
    void supersedeInFlightLocked(const char* cause, std::uint32_t originLine) noexcept {
        if (!inFlight_) return;
        ++hintGeneration_;
        if (!readDiscard_.hintCause && decky_ && config_.deckyReadSuperseded) {
            readDiscard_.hintCause = cause;
            readDiscard_.hintOriginLine = originLine;
        }
    }
    // A current independent request must survive cancellation of the old
    // Decky-only read, without letting a stale request borrow the new Ready.
    void retainIndependentDeckySuccessorLocked() noexcept {
        if (!inFlight_ || !inFlightDeckyWake_ || inFlightGeneration_ != generation_) return;
        sleeppower::SharedPowerV1 current;
        if (sleeppower::capturePower(config_.admissionPowerReader, current) &&
            !sleeppower::samePowerRevocation(inFlightPower_, current)) queuedResumePower_ = current;
    }
    void revokeSourceLocked(SourceNotice& notice) noexcept {
        if (!source_) return;
        source_->revoke(); source_.reset();
        notice.invalidated = ++sourceRevision_;
        notice.callbacks = sourceCallbacks_;
    }
    static bool sourceShapeKnown(const ModeJson& result) {
        const auto account = result.find("observedAccount"), watch = result.find("deckyAccountWatchAvailable");
        return (account == result.end() || steamaccount::positiveAccount32(*account) != 0) &&
            (watch == result.end() || watch->is_boolean());
    }
    void updateSourceLocked(const steamsession::Session& session, const ModeJson& result, bool valid, SourceNotice& notice) noexcept {
        try {
        if (!valid || !sourceShapeKnown(result)) { revokeSourceLocked(notice); return; }
        // UI account/watch are source association, even when mode/doc are stable.
        const auto mode = result.value("mode",std::string{}), epoch = result.value("modeEpoch",std::string{});
        if (source_ && source_->snapshot->session == session && source_->snapshot->mode == mode &&
            source_->snapshot->modeEpoch == epoch &&
            source_->snapshot->modeFacts.value("observedAccount",ModeJson()) == result.value("observedAccount",ModeJson()) &&
            source_->snapshot->modeFacts.value("deckyAccountWatchAvailable",ModeJson()) == result.value("deckyAccountWatchAvailable",ModeJson())) return;
        revokeSourceLocked(notice);
        auto next = std::make_shared<modesourcedetail::State>(sourceDiagnostics_);
        if (!next->event) return; // Unknown/unavailable, never a provider-ready receipt.
        auto facts = std::make_shared<ModeSourceSnapshot>();
        facts->session = session; facts->modeFacts = result;
        const auto projected = steamaccount::project(session,result);
        facts->uiAccount = projected.ui;
        facts->uiLocal = steamaccount::select(projected,steamaccount::Policy::UiLocal);
        facts->mode = mode; facts->modeEpoch = epoch; facts->modeRevision = ++sourceRevision_;
        next->snapshot = facts; source_ = std::move(next);
        notice.changed = std::move(facts); notice.callbacks = sourceCallbacks_;
        } catch (const std::bad_alloc&) {
            revokeSourceLocked(notice); ++sourceDiagnostics_->publicationFailed;
            sourceDiagnostics_->lastError = ERROR_NOT_ENOUGH_MEMORY;
        } catch (...) {
            revokeSourceLocked(notice); ++sourceDiagnostics_->publicationFailed;
            sourceDiagnostics_->lastError = ERROR_INVALID_DATA;
        }
    }
    static void emitSource(SourceNotice notice) noexcept {
        if (!notice.callbacks) return;
        if (notice.invalidated && notice.callbacks->invalidated) { try { notice.callbacks->invalidated(notice.invalidated); } catch (...) {} }
        if (notice.changed && notice.callbacks->changed) { try { notice.callbacks->changed(std::move(notice.changed)); } catch (...) {} }
    }
    static bool validSession(const steamsession::Session& s) { return s.pid && s.created && !s.root.empty(); }
    bool demand() const { return decky_ || css_; }
    // Only the native read-mutex miss carries this exact pending envelope.
    // A CDP/renderer live-busy reason alone remains a completed invalid read.
    static bool localReadContention(const ModeJson& state, const steamsession::Session& session) noexcept {
        try {
            if (!state.is_object() || state.size() != 7) return false;
            const auto flag = state.find("localReadBusy"), pid = state.find("pid");
            return flag != state.end() && flag->is_boolean() && flag->get<bool>() &&
                pid != state.end() && pid->is_number_integer() &&
                (pid->is_number_unsigned() ? pid->get<uint64_t>() == session.pid : pid->get<int64_t>() == session.pid) &&
                state.at("known").is_boolean() && state.at("known").get<bool>() &&
                state.at("running").is_boolean() && state.at("running").get<bool>() &&
                state.at("birth").is_string() && state.at("birth").get_ref<const std::string&>() == std::to_string(session.created) &&
                state.at("mode").is_string() && state.at("mode").get_ref<const std::string&>() == "unknown" &&
                state.at("reason").is_string() && state.at("reason").get_ref<const std::string&>() == "live-busy";
        } catch (...) { return false; }
    }
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
    // Keep independent owned snapshots. Only identical container shapes reuse nodes;
    // type/shape/length changes retain the original full JSON copy path.
    static void copyModeValue(ModeJson& into,const ModeJson& from) {
        if(&into==&from)return;
        if(into.type()!=from.type()){into=from;return;}
        if(from.is_object()){
            auto& target=into.get_ref<ModeJson::object_t&>();
            const auto& source=from.get_ref<const ModeJson::object_t&>();
            if(target.size()!=source.size()){into=from;return;}
            auto at=target.begin();
            for(const auto& entry:source){if(at->first!=entry.first){into=from;return;}++at;}
            at=target.begin();
            for(const auto& entry:source){copyModeValue(at->second,entry.second);++at;}
            return;
        }
        if(from.is_array()){
            auto& target=into.get_ref<ModeJson::array_t&>();
            const auto& source=from.get_ref<const ModeJson::array_t&>();
            if(target.size()!=source.size()){into=from;return;}
            for(std::size_t i=0;i<source.size();++i)copyModeValue(target[i],source[i]);
            return;
        }
        if(from.is_string()){
            auto& target=into.get_ref<ModeJson::string_t&>();
            const auto& source=from.get_ref<const ModeJson::string_t&>();
            if(target.size()!=source.size()){into=from;return;}
            for(std::size_t i=0;i<source.size();++i)target[i]=source[i];
            return;
        }
        into=from;
    }
    void run() {
        ymcc::threadRole::describe(L"YMCC.SteamMode");
        for (;;) {
            steamsession::Session session; unsigned long long generation = 0, hints = 0; DWORD timeout = INFINITE;
            sleeppower::SharedPowerV1 readPower, deckyWake;
            bool read = false;
            {
                std::lock_guard lock(mutex_);
                if (stopping_) return;
                const auto now = GetTickCount64();
                const auto& queuedPower = queuedDeckyWake_.generation ? queuedDeckyWake_ : queuedResumePower_;
                if (queuedPower.generation && window_.timeout(now) == 0 &&
                    ((queuedDeckyWake_.generation && !decky_) ||
                     !sleeppower::powerCurrent(config_.admissionPowerReader, queuedPower))) {
                    queuedDeckyWake_ = {}; queuedResumePower_ = {}; window_.cancel(true);
                }
                if (demand() && validSession(session_) && window_.take(now)) {
                    queuedResumePower_ = {};
                    deckyWake = queuedDeckyWake_; queuedDeckyWake_ = {};
                    read = inFlight_ = true; session = session_; generation = generation_; hints = hintGeneration_;
                    inFlightDeckyWake_ = deckyWake.generation != 0;
                    if (deckyWake.generation) readPower = deckyWake;
                    else sleeppower::capturePower(config_.admissionPowerReader, readPower);
                    inFlightPower_ = readPower; inFlightGeneration_ = generation; inFlightHints_ = hints;
                    readDiscard_ = {}; readDiscard_.readStartedTick = now;
                } else timeout = window_.timeout(now);
            }
            if (!read) {
                HANDLE events[]{stop_, wake_};
                const auto waited = WaitForMultipleObjects(2, events, FALSE, timeout);
                if (waited == WAIT_OBJECT_0) return;
                if (waited == WAIT_FAILED) {
                    const auto waitError = GetLastError(); SourceNotice notice;
                    {
                        std::lock_guard lock(mutex_); stopping_ = true;
                        revokeSourceLocked(notice); cached_ = delivered_ = ModeJson{};
                        window_.cancel(true); queuedDeckyWake_ = {}; passivePending_ = false; ++generation_;
                        noteCacheClearLocked("worker-wait-failed");
                        ++sourceDiagnostics_->workerWaitFailed; sourceDiagnostics_->lastError = waitError;
                    }
                    emitSource(std::move(notice)); return;
                }
                continue;
            }
            ModeJson result;
            const auto lifecycleCurrent = [this, generation]() noexcept {
                return !stopping_.load(std::memory_order_acquire) &&
                    generation_.load(std::memory_order_acquire) == generation && demand();
            };
            try {
                if (deckyWake.generation) {
                    const ModeReadCurrent current = [this, lifecycleCurrent, deckyWake]() noexcept {
                        return lifecycleCurrent() && sleeppower::powerCurrent(config_.admissionPowerReader, deckyWake);
                    };
                    result = config_.readOwned(session, current);
                } else result = config_.readOwned ? config_.readOwned(session, lifecycleCurrent) : config_.read(session);
            }
            catch (...) { result = {{"known", false}, {"mode", "unknown"}, {"reason", "shared-mode-read-failed"}}; }
            const bool captureCssTiming = css_ && (readPower.suspendSerial || readPower.observedNotificationSequence);
            const auto returnedTick = (captureCssTiming || decky_) ? GetTickCount64() : 0;
            const auto readReason = result.find("reason");
            const bool readCancelled = result.is_object() && readReason != result.end() && readReason->is_string() &&
                readReason->get_ref<const std::string&>() == "shared-mode-read-cancelled";
            bool notify = false, settled = false, stableBigPicture = false;
            bool notifyCss = false;
            ModeCacheTiming cssTiming;
            SourceNotice sourceNotice;
            ModeReadSuperseded discarded;
            {
                std::unique_lock lock(mutex_); inFlight_ = false;
                if (stopping_) return;
                // Lifecycle invalidation cannot spend the replacement session/demand budget.
                if (generation != generation_ || !(session == session_) || !demand()) continue;
                const bool invalidated = hints != hintGeneration_;
                const bool queuedDifferentReady = (invalidated || deckyWake.generation) && queuedResumePower_.generation &&
                    window_.timeout(GetTickCount64()) != INFINITE &&
                    !sleeppower::samePowerRevocation(readPower, queuedResumePower_) &&
                    !sleeppower::samePowerRevocation(queuedResumePower_, readPower);
                // Ready can change after the reader's final fence. Its old
                // result still spends its slot, but must not delay the successor.
                if (readCancelled || queuedDifferentReady) {
                    if (!queuedDifferentReady || !sleeppower::powerCurrent(config_.admissionPowerReader, queuedResumePower_)) {
                        window_.cancel(true); queuedResumePower_ = {}; queuedDeckyWake_ = {};
                    }
                    revokeSourceLocked(sourceNotice);
                    cached_ = delivered_ = ModeJson{}; passivePending_ = false;
                    noteCacheClearLocked("read-cancelled");
                    lock.unlock(); emitSource(std::move(sourceNotice)); continue;
                }
                queuedResumePower_ = {};
                bool confirmed = false;
                try { confirmed = result.is_object() && known(result, session); } catch (...) {}
                const bool valid = !invalidated && confirmed;
                // An in-flight hint spends an attempt, without resetting the
                // lifecycle budget. Preserve pending display until its finite
                // follow-up settles or the failure window is exhausted.
                // Only the exact local no-IO receipt may use finite startup
                // credit. Transport/renderer live-busy still spends a failed IO
                // attempt and keeps the ordinary retry spacing.
                const bool localBusy = localReadContention(result, session);
                window_.finish(GetTickCount64(), valid, (invalidated && confirmed) || localBusy, localBusy);
                if (invalidated) {
                    const auto next = window_.timeout(GetTickCount64());
                    if (confirmed && decky_ && readDiscard_.hintCause && result["mode"] == "bigpicture") {
                        discarded = readDiscard_;
                        discarded.hintCount = hintGeneration_ - hints;
                        discarded.nextReadMs = next;
                    }
                    if (next != INFINITE) {
                        if (discarded.hintCause) {
                            lock.unlock();
                            try { config_.deckyReadSuperseded(session, discarded); } catch (...) {}
                        }
                        continue;
                    }
                }
                // The synthetic unknown snapshot has no mode proof, but the
                // same-session caller may distinguish exact no-IO contention.
                cachedLocalReadMiss_ = !invalidated && localBusy ? ModeCacheMiss{"native-read-lock", 0, ++cacheMissSequence_} : ModeCacheMiss{};
                if (valid) {
                    notify = !delivered_.is_object() || delivered_.value("mode", std::string{}) != result.value("mode", std::string{}) ||
                        delivered_.value("modeEpoch", std::string{}) != result.value("modeEpoch", std::string{}) ||
                        delivered_.value("deckyUpdateResidue",ModeJson()) != result.value("deckyUpdateResidue",ModeJson()) ||
                        delivered_.value("deckyEmptyFrontend",ModeJson()) != result.value("deckyEmptyFrontend",ModeJson()) ||
                        delivered_.value("deckyBackendConnected",ModeJson()) != result.value("deckyBackendConnected",ModeJson()) ||
                        delivered_.value("deckyHooksInstalled",ModeJson()) != result.value("deckyHooksInstalled",ModeJson()) ||
                        delivered_.value("deckyClientConnected",ModeJson()) != result.value("deckyClientConnected",ModeJson()) ||
                        delivered_.value("deckyClientChecking",ModeJson()) != result.value("deckyClientChecking",ModeJson()) ||
                        delivered_.value("deckyClientVerified",ModeJson()) != result.value("deckyClientVerified",ModeJson());
                    copyModeValue(delivered_,result);
                    copyModeValue(cached_,delivered_);
                    ++publicationSequence_;
                    publicationObservationTick_ = returnedTick;
                    stableBigPicture = !notify && result.value("mode", std::string{}) == "bigpicture";
                } else {
                    delivered_ = ModeJson{};
                    std::string reason = invalidated ? "shared-mode-events-unsettled" : "shared-mode-invalid";
                    try { if(!invalidated && result.is_object())reason=result.value("reason",std::string("shared-mode-unconfirmed")); } catch (...) {}
                    cached_ = {{"known", true}, {"running", true}, {"mode", "unknown"}, {"pid", session.pid},
                        {"birth", std::to_string(session.created)}, {"reason", reason}};
                }
                cachedPower_ = readPower;
                // Local mutex contention observes no external change. Preserve
                // only the existing same-session lease; pending display, retry
                // debt and all real invalidation paths remain unchanged.
                const bool retainSource = !invalidated && localBusy &&
                    source_ && source_->snapshot && source_->snapshot->session == session &&
                    !source_->revoked.load(std::memory_order_acquire);
                if (!retainSource) updateSourceLocked(session, result, valid, sourceNotice);
                cachedTiming_ = valid && captureCssTiming && returnedTick ? ModeCacheTiming{readPower, returnedTick, GetTickCount64()} : ModeCacheTiming{};
                if (valid && css_ && config_.cssBigPicturePublished &&
                    (stableBigPicture || (notify && result.at("mode") == "bigpicture"))) {
                    notifyCss = true; cssTiming = cachedTiming_;
                }
                settled = passivePending_ && !notify; passivePending_ = false;
            }
            if (notifyCss && css_ && lifecycleCurrent()) {
                try { config_.cssBigPicturePublished(session, result, cssTiming, stableBigPicture); } catch (...) {}
            }
            emitSource(std::move(sourceNotice));
            // No callback under the shared mutex: consumers may call snapshot/setDemand.
            if (discarded.hintCause) { try { config_.deckyReadSuperseded(session, discarded); } catch (...) {} }
            if (notify && config_.changed) { try { config_.changed(session, result); } catch (...) {} }
            if (stableBigPicture && config_.stableBigPicture) { try { config_.stableBigPicture(session); } catch (...) {} }
            if (settled && config_.settled) { try { config_.settled(session); } catch (...) {} }
        }
    }
public:
    ~ModeObserver() { shutdown(); }
    bool initialize(ModeObserverConfig config) {
        std::lock_guard lifecycle(lifecycle_);
        if (worker_.joinable()) { std::lock_guard lock(mutex_); return !stopping_; }
        if (!config.read && !config.readOwned) return false;
        std::shared_ptr<SourceCallbacks> sourceCallbacks;
        try {
            sourceCallbacks = std::make_shared<SourceCallbacks>();
            sourceCallbacks->changed = std::move(config.sourceChanged);
            sourceCallbacks->invalidated = std::move(config.sourceInvalidated);
        } catch (...) { return false; }
        HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr), stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!wake || !stop) { if (wake) CloseHandle(wake); if (stop) CloseHandle(stop); return false; }
        {
            std::lock_guard lock(mutex_); config_ = std::move(config); sourceCallbacks_ = std::move(sourceCallbacks); stopping_ = false;
            wake_ = wake; stop_ = stop; window_ = ModeReadWindow(config_.debounceMs, config_.minimumGapMs, config_.failureGapMs);
            if (demand() && validSession(session_)) window_.request(GetTickCount64());
        }
        try { worker_ = std::thread([this] { run(); }); }
        catch (...) { std::lock_guard lock(mutex_); CloseHandle(wake_); CloseHandle(stop_); wake_ = stop_ = nullptr; return false; }
        return true;
    }
    void setDemand(ModeConsumer consumer, bool enabled) {
        SourceNotice notice;
        {
            std::lock_guard lock(mutex_);
            auto& flag = consumer == ModeConsumer::Decky ? decky_ : css_;
            if (stopping_ || flag == enabled) return;
            const bool hadDemand = demand(); flag = enabled;
            if (!decky_ && queuedDeckyWake_.generation) {
                queuedDeckyWake_ = {}; queuedResumePower_ = {}; window_.cancel(true);
            }
            if (!demand()) { revokeSourceLocked(notice); window_.cancel(true); queuedResumePower_ = {}; cached_ = delivered_ = ModeJson{}; passivePending_ = false; ++generation_; noteCacheClearLocked("no-demand"); }
            else if (!hadDemand && validSession(session_)) window_.request(GetTickCount64(),true);
            if (wake_) SetEvent(wake_);
        }
        emitSource(std::move(notice));
    }
    void observeSteam(const steamsession::Session& session) {
        SourceNotice notice;
        {
            std::lock_guard lock(mutex_);
            if (stopping_ || session_ == session) return;
            const bool sameProcess = validSession(session_) && validSession(session) &&
                session_.pid == session.pid && session_.created == session.created && session_.root == session.root;
            revokeSourceLocked(notice);
            session_ = session; cached_ = delivered_ = ModeJson{}; queuedResumePower_ = {}; queuedDeckyWake_ = {}; passivePending_ = false; ++generation_;
            noteCacheClearLocked("session-changed");
            // Learning/changing the account in the same process must not refill startup.
            window_.reset(sameProcess);
            if (validSession(session_) && !sameProcess) window_.beginStartup(GetTickCount64());
            if (demand() && validSession(session_)) window_.request(GetTickCount64(),sameProcess);
            if (wake_) SetEvent(wake_);
        }
        emitSource(std::move(notice));
    }
    // Original event-driven read window; no additional reader/timer/poller.
    bool hasAccountObservation(const steamsession::Session& session) {
        std::lock_guard lock(mutex_);
        // observeSteam resets delivery and revokes the source even when only
        // the account changes. Joining must not invalidate that replacement read.
        return !stopping_ && demand() && validSession(session_) && session_ == session &&
            (inFlight_ || window_.timeout(GetTickCount64()) != INFINITE || (cached_.is_object() && known(cached_, session)));
    }
private:
    using RefreshSatisfied = bool (*)(const steamsession::Session&, const ModeJson&, bool);
    bool requestRefreshImpl(const steamsession::Session& session, bool invalidate,
                            std::source_location origin, RefreshSatisfied satisfied,
                            const sleeppower::SharedPowerV1* resumePower = nullptr,
                            std::string* reusedEpoch = nullptr, bool deckyWake = false) {
        if (reusedEpoch) reusedEpoch->clear();
        SourceNotice notice;
        {
            std::lock_guard lock(mutex_);
            if (stopping_ || !demand() || !validSession(session_) || !(session_ == session)) return false;
            if (deckyWake && (!decky_ || !config_.readOwned)) return false;
            // Decide under the invalidation lock, and never substitute a pre-wake
            // receipt or a sample made to look recent by another in-flight read.
            const auto now = GetTickCount64();
            bool joinInFlight = false;
            sleeppower::SharedPowerV1 current;
            if (resumePower) {
                if (!sleeppower::capturePower(config_.admissionPowerReader, current) ||
                    !sleeppower::samePowerRevocation(*resumePower, current)) return false;
                // Another Ready consumer may already have read this exact scope.
                // Never reuse a pending or pre-suspend appearance observation.
                if (!inFlight_ && window_.timeout(now) == INFINITE && window_.withinReadGap(now) && source_ &&
                    !source_->revoked.load(std::memory_order_acquire) && source_->snapshot &&
                    source_->snapshot->session == session && source_->snapshot->mode == "bigpicture" &&
                    parseModeEpoch(source_->snapshot->modeEpoch).valid &&
                    sleeppower::samePowerRevocation(cachedPower_, current) && cached_.is_object() &&
                    cached_.value("mode", std::string{}) == "bigpicture" &&
                    cached_.value("modeEpoch", std::string{}) == source_->snapshot->modeEpoch && known(cached_, session)) {
                    if (reusedEpoch) *reusedEpoch = source_->snapshot->modeEpoch;
                    return true;
                }
                // Join only an owned read already reserved in this exact power
                // and observer scope. An intervening hint can retire its page.
                joinInFlight = config_.readOwned && inFlight_ && inFlightGeneration_ == generation_ &&
                    inFlightHints_ == hintGeneration_ && sleeppower::samePowerRevocation(inFlightPower_, current);
            }
            if (satisfied && cached_.is_object() && satisfied(session, cached_,
                !inFlight_ && window_.withinReadGap(now) && sleeppower::powerCurrent(config_.admissionPowerReader, cachedPower_))) return true;
            if (invalidate && !joinInFlight) supersedeInFlightLocked("mode-refresh", origin.line());
            if (invalidate) {
                revokeSourceLocked(notice);
                if (cached_.is_object()) {
                    cached_ = ModeJson{};
                    noteCacheClearLocked("mode-refresh", origin.line());
                }
            }
            // Stale published facts are still cleared above. Joining neither
            // publishes the in-flight result early nor consumes another slot.
            if (!joinInFlight) {
                const bool newlyArmed = window_.request(GetTickCount64(), inFlight_);
                // Only the requester that armed an otherwise empty slot owns
                // its Decky wake scope. Another valid demand can use the slot.
                if (deckyWake) {
                    // A retired CSS-only slot cannot displace this admitted Decky wake.
                    const bool retiredCssSlot = !inFlight_ && queuedResumePower_.generation &&
                        !sleeppower::samePowerRevocation(queuedResumePower_, current);
                    if (newlyArmed || queuedDeckyWake_.generation || retiredCssSlot) {
                        queuedDeckyWake_ = *resumePower;
                        if (inFlight_) queuedResumePower_ = *resumePower;
                        else if (retiredCssSlot) queuedResumePower_ = {};
                    }
                } else {
                    const bool joinedDeckySlot = queuedDeckyWake_.generation != 0;
                    if (joinedDeckySlot) queuedResumePower_ = {};
                    queuedDeckyWake_ = {};
                    if (resumePower && (newlyArmed || joinedDeckySlot || inFlight_ || queuedResumePower_.generation)) queuedResumePower_ = *resumePower;
                    else if (!resumePower && !inFlight_) queuedResumePower_ = {}; // Independent fresh demand.
                    if (!resumePower) retainIndependentDeckySuccessorLocked();
                }
                if (newlyArmed && wake_) SetEvent(wake_);
            }
        }
        emitSource(std::move(notice)); return true;
    }
public:
    bool requestRefresh(const steamsession::Session& session, bool invalidate = true,
                        std::source_location origin = std::source_location::current()) {
        return requestRefreshImpl(session, invalidate, origin, nullptr);
    }
    bool requestAdmissionRefresh(const steamsession::Session& session, RefreshSatisfied satisfied,
                                 std::source_location origin = std::source_location::current()) {
        return requestRefreshImpl(session, true, origin, satisfied);
    }
    bool requestPublicResumeRefresh(const steamsession::Session& session, const sleeppower::SharedPowerV1& power,
                                    std::string* reusedEpoch = nullptr,
                                    std::source_location origin = std::source_location::current()) {
        return requestRefreshImpl(session, true, origin, nullptr, &power, reusedEpoch);
    }
    bool requestDeckyResumeRefresh(const steamsession::Session& session, const sleeppower::SharedPowerV1& power,
                                   std::source_location origin = std::source_location::current()) {
        return requestRefreshImpl(session, true, origin, nullptr, &power, nullptr, true);
    }
    // A Decky bridge/QAM event changes its connection observation, not the
    // verified Steam document/account association borrowed by CSS/wallpaper.
    // Keep that source lease during this read; actual session/mode/account
    // changes or a completed invalid read still revoke it in the original paths.
    // No new reader, window, deadline, retries or action authority.
    bool requestConnectionRefresh(const steamsession::Session& session,
                                  std::source_location origin = std::source_location::current()) {
        std::lock_guard lock(mutex_);
        if (stopping_ || !decky_ || !validSession(session_) || !(session_ == session)) return false;
        supersedeInFlightLocked("connection-refresh", origin.line());
        if (!inFlight_ || queuedDeckyWake_.generation) queuedResumePower_ = {};
        queuedDeckyWake_ = {};
        if (cached_.is_object()) {
            cached_ = ModeJson{};
            noteCacheClearLocked("connection-refresh", origin.line());
        }
        const bool newlyArmed = window_.request(GetTickCount64(), inFlight_);
        retainIndependentDeckySuccessorLocked();
        if (newlyArmed && wake_) SetEvent(wake_);
        return true;
    }
    ModeSourceLease observedSource(const steamsession::Session& session) {
        std::lock_guard lock(mutex_);
        return !stopping_ && demand() && session_ == session ? ModeSourceLease(source_) : ModeSourceLease{};
    }
    ModeSourceEventDiagnostics sourceEventDiagnostics() const noexcept {
        return {sourceDiagnostics_->created.load(),sourceDiagnostics_->closed.load(),sourceDiagnostics_->createFailed.load(),
            sourceDiagnostics_->signalFailed.load(),sourceDiagnostics_->closeFailed.load(),sourceDiagnostics_->lastError.load(),sourceDiagnostics_->publicationFailed.load(),sourceDiagnostics_->workerWaitFailed.load()};
    }
    ModeJson snapshot(const steamsession::Session& session, ModeCacheMiss* miss = nullptr, ModeCacheTiming* timing = nullptr,
                      std::uint64_t* publication = nullptr, ULONGLONG* observationTick = nullptr) {
        std::lock_guard lock(mutex_);
        if (miss) *miss = {};
        if (timing) *timing = {};
        if (publication) *publication = 0;
        if (observationTick) *observationTick = 0;
        if (!stopping_ && session_ == session && cached_.is_object()) {
            if (miss) *miss = cachedLocalReadMiss_;
            if (timing) *timing = cachedTiming_;
            // A synthetic unknown clears delivered_; cache invalidation clears cached_.
            if (delivered_.is_object()) {
                if (publication) *publication = publicationSequence_;
                if (observationTick) *observationTick = publicationObservationTick_;
            }
            return cached_;
        }
        // Copy under the same lock as the missed snapshot. Later hints cannot
        // replace the reason that this read actually observed.
        if (miss) {
            if (!(session_ == session)) *miss = {"session-mismatch"};
            else if (stopping_) *miss = {"observer-stopping"};
            else *miss = cacheMiss_.reason ? cacheMiss_ : ModeCacheMiss{"no-observation"};
        }
        return {{"known", validSession(session)}, {"running", validSession(session)}, {"mode", "unknown"},
            {"pid", session.pid}, {"birth", std::to_string(session.created)}, {"reason", "shared-mode-pending"}};
    }
    // Borrow only a cached observation from this Ready scope. A miss has no
    // reader or waiter side effect; the existing owner delivers the next read.
    ModeJson snapshot(const steamsession::Session& session, const sleeppower::SharedPowerV1& power, ModeCacheTiming* timing = nullptr) {
        std::lock_guard lock(mutex_);
        if (timing) *timing = {};
        if (!stopping_ && session_ == session && cached_.is_object() &&
            (sleeppower::samePowerRevocation(cachedPower_, power) ||
             sleeppower::samePowerRevocation(power, cachedPower_))) {
            if (timing) *timing = cachedTiming_;
            return cached_;
        }
        return {};
    }
    // Changed callbacks already carry the result. Borrow metadata only when
    // the same document still has a cache entry; do not make delivery wait.
    ModeCacheTiming tryBigPictureTiming(const steamsession::Session& session, const std::string& epoch) noexcept {
        try {
            std::unique_lock lock(mutex_, std::try_to_lock);
            if (lock.owns_lock() && !stopping_ && session_ == session && cached_.is_object() && cachedTiming_.publishedTick) {
                const auto mode = cached_.find("mode"), document = cached_.find("modeEpoch");
                if (mode != cached_.end() && *mode == "bigpicture" && document != cached_.end() && *document == epoch) return cachedTiming_;
            }
        } catch (...) {}
        return {};
    }
    // The UI delivery lane may attach a positive snapshot to an existing
    // notice. A miss does not wait, schedule a read, or mark a waiter pending.
    ModeJson tryMediaNoticeState(const steamsession::Session& session,
                                 const sleeppower::SharedPowerV1& power) noexcept {
        try {
            std::unique_lock lock(mutex_, std::try_to_lock);
            if (!lock.owns_lock() || stopping_ || !demand() || !validSession(session_) ||
                !(session_ == session) || !cached_.is_object() ||
                !sleeppower::samePowerRevocation(cachedPower_, power) || !known(cached_, session)) return {};
            ModeJson state{{"known",true},{"running",true},{"pid",session.pid},
                {"birth",std::to_string(session.created)},{"mode",cached_.at("mode")},
                {"reason","published-media-notice"}};
            for (auto key : {"observedAccount", "modeEpoch"}) {
                const auto value=cached_.find(key);
                if (value!=cached_.end())state[key]=*value;
            }
            return state;
        } catch (...) { return {}; }
    }
    // Automatic frontend reads join the existing CSS/Decky observation. A
    // pending result must not cause a second CDP reader before our deadline.
    // Null means no enabled observer owns this session; legacy read may proceed.
    ModeJson passiveSnapshot(const steamsession::Session& session,
                             const sleeppower::SharedPowerV1* requestPower = nullptr,
                             bool* withoutDemand = nullptr) {
        std::lock_guard lock(mutex_);
        if (withoutDemand) *withoutDemand = false;
        if (stopping_ || !validSession(session_) || !(session_ == session)) return ModeJson{};
        if (!demand()) {
            if (withoutDemand) *withoutDemand = true;
            return ModeJson{};
        }
        if (cached_.is_object()) {
            if (!requestPower || sleeppower::samePowerRevocation(cachedPower_, *requestPower)) return cached_;
            // This observer still owns the read. An expired cache is not an
            // invitation to fall back to CDP or to publish its process identity.
            passivePending_ = true;
            return {{"known", false}, {"running", false}, {"mode", "unknown"},
                {"pid", 0u}, {"birth", "0"}, {"reason", "shared-mode-power-stale"}};
        }
        passivePending_ = true;
        return {{"known", true}, {"running", true}, {"mode", "unknown"},
            {"pid", session.pid}, {"birth", std::to_string(session.created)}, {"reason", "shared-mode-pending"}};
    }
    void shutdown() {
        SourceNotice notice;
        {
            std::lock_guard lifecycle(lifecycle_);
            { std::lock_guard lock(mutex_); stopping_ = true; revokeSourceLocked(notice); if (stop_) SetEvent(stop_); }
            if (worker_.joinable()) worker_.join();
            { std::lock_guard lock(mutex_); if (wake_) CloseHandle(wake_); if (stop_) CloseHandle(stop_);
                wake_ = stop_ = nullptr; session_ = {}; cached_ = delivered_ = ModeJson{}; window_.reset(); queuedResumePower_ = {}; queuedDeckyWake_ = {}; passivePending_ = false; ++generation_; cacheMiss_ = {}; }
        }
        emitSource(std::move(notice));
    }
};
} // namespace ymcc::steamui
