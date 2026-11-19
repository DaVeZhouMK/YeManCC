#if defined(YMCC_MODE_MALFORMED_ONLY)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "steam_ui_mode_observer.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <crtdbg.h>
using namespace ymcc;
static unsigned passed=0;
static void expect(bool ok,const char* label){if(!ok)throw std::runtime_error(label);++passed;std::cout<<"PASS "<<label<<std::endl;}
template<class F> bool until(F f,DWORD ms=1500){const auto end=GetTickCount64()+ms;while(!f()){if(GetTickCount64()>=end)return false;Sleep(2);}return true;}
int main(int argc,char** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 if(argc!=2||std::string(argv[1])!="--malformed-reader-only")return 2;
 std::set_terminate([]{try{auto e=std::current_exception();if(e)std::rethrow_exception(e);}catch(const std::exception& e){std::cerr<<"ORIGINAL_WORKER_TERMINATE "<<e.what()<<std::endl;}std::_Exit(3);});
 try{
 const steamsession::Session session{L"X:\\InertSteam",123,0,100};
 steamui::ModeObserverConfig config;config.debounceMs=25;config.minimumGapMs=80;config.failureGapMs=180;
#include "fixtures/steam_mode_read/malformed.inc"
 std::cout<<nlohmann::json{{"selection","malformed-reader-only"},{"passed",passed},{"realSteamOperations",0},{"defaultParent",false}}.dump()<<std::endl;return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
#else
#if defined(YMCC_MODE_SOURCE_DIRECTED)
// Directed consumer of the actual ModeObserver and its actual read window.
// Only session/mode headers are included; no Decky Runtime or identity producer starts.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "steam_session_observer.h"
#include <atomic>
// Only the ModeObserver include below sees this single-use OS terminal port.
// The identity producer is already included under pragma once and is never started.
namespace ymcc_mode_source_wait_port {
inline std::atomic<bool> failNext{false};
inline std::atomic<std::uint64_t> forwarded{0}, injected{0};
inline std::atomic<unsigned> inOsWait{0};
inline DWORD WINAPI waitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD timeout) noexcept {
    if (failNext.exchange(false, std::memory_order_acq_rel)) {
        ++injected;
        SetLastError(ERROR_INVALID_HANDLE);
        return WAIT_FAILED;
    }
    ++forwarded; ++inOsWait;
    const auto result = ::WaitForMultipleObjects(count, handles, waitAll, timeout);
    --inOsWait;
    return result;
}
}
#define WaitForMultipleObjects ymcc_mode_source_wait_port::waitForMultipleObjects
#include "steam_ui_mode_observer.h"
#undef WaitForMultipleObjects
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace mode_source_directed {
using namespace ymcc;
using Json = steamui::ModeJson;
using Session = steamsession::Session;
constexpr DWORD waitLimit = 2000, gateLimit = 2500;

static Session syntheticSession() {
    return {L"X:\\YMCC-Mode-Source-Directed-Inert", 424242, 101, 1234567890123ULL};
}
static Json sample(const Session& s, const char* mode = "bigpicture", const char* epoch = "doc-a:1") {
    return {{"known", true}, {"running", true}, {"pid", s.pid},
        {"birth", std::to_string(s.created)}, {"mode", mode}, {"modeEpoch", epoch},
        {"observedAccount", 101}, {"deckyAccountWatchAvailable", true}};
}
static Json sessionJson(const Session& s) {
    return {{"root", std::string(s.root.begin(), s.root.end())}, {"pid", s.pid},
        {"account", s.account}, {"birth", std::to_string(s.created)}};
}
class Event {
    HANDLE handle_;
public:
    explicit Event(bool manual = true) : handle_(CreateEventW(nullptr, manual, FALSE, nullptr)) {
        if (!handle_) throw std::runtime_error("fixture-owned-event-create-failed");
    }
    ~Event() { if (handle_) CloseHandle(handle_); }
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    HANDLE get() const noexcept { return handle_; }
    void signal() noexcept { SetEvent(handle_); }
    DWORD wait(DWORD ms = waitLimit) const noexcept { return WaitForSingleObject(handle_, ms); }
};
struct Gate {
    Event entered, released;
    std::atomic<bool> timedOut{false};
    bool arrive() noexcept {
        entered.signal();
        const bool ready = released.wait(gateLimit) == WAIT_OBJECT_0;
        timedOut.store(!ready);
        return ready;
    }
    void release() noexcept { released.signal(); }
    ~Gate() { release(); }
};
class JoiningThread {
    std::function<void()> release_;
    std::thread thread_;
public:
    template<class F, class R> JoiningThread(F body, R release)
        : release_(std::move(release)), thread_(std::move(body)) {}
    ~JoiningThread() { release_(); if (thread_.joinable()) thread_.join(); }
    void join() { if (thread_.joinable()) thread_.join(); }
};
struct Case {
    std::string name;
    Json assertions = Json::array(), metadata = Json::object();
    bool passed = true;
    bool check(const std::string& assertion, bool ok, Json detail = Json::object()) {
        assertions.push_back({{"name", assertion}, {"passed", ok}, {"detail", std::move(detail)}});
        passed = passed && ok;
        return ok;
    }
    Json json() const {
        return {{"name", name}, {"passed", passed}, {"assertions", assertions}, {"metadata", metadata}};
    }
};

#if !defined(YMCC_MODE_SOURCE_ORIGINAL)
using Snapshot = steamui::ModeSourceSnapshot;
using Lease = steamui::ModeSourceLease;
static_assert(std::is_same_v<decltype(std::declval<const Lease&>().snapshot()), const Snapshot&>);
static_assert(noexcept(std::declval<const Lease&>().snapshot()));
static_assert(noexcept(std::declval<const Lease&>().current()));
static_assert(noexcept(std::declval<const Lease&>().cancellationEvent()));
static Json sourceJson(const Snapshot& s) {
    return {{"session", sessionJson(s.session)}, {"modeFacts", s.modeFacts},
        {"modeRevision", s.modeRevision}, {"mode", s.mode}, {"modeEpoch", s.modeEpoch},
        {"uiAccount", {{"accountID32", s.uiAccount.accountID32}, {"pid", s.uiAccount.pid},
            {"birth", s.uiAccount.birth}, {"samePidAndBirth", s.uiAccount.samePidAndBirth},
            {"currentAccountKnown", s.uiAccount.currentAccountKnown()},
            {"watch", static_cast<int>(s.uiAccount.deckyAccountWatchAvailable)},
            {"documentKnown", s.uiAccount.documentKnown}, {"document", s.uiAccount.document},
            {"documentEntry", s.uiAccount.documentEntry}}},
        {"uiLocal", {{"value", s.uiLocal.value}, {"fromUi", s.uiLocal.fromUi}}}};
}
static Json diagnosticsJson(const steamui::ModeSourceEventDiagnostics& d) {
    return {{"created", d.created}, {"closed", d.closed}, {"createFailed", d.createFailed},
        {"signalFailed", d.signalFailed}, {"closeFailed", d.closeFailed}, {"lastError", d.lastError},
        {"workerWaitFailed", d.workerWaitFailed}};
}
#endif

// Every blocking read/callback gate is registered here before being published.
// Unwinding releases all gates and joins the observer before an exception escapes.
class Harness {
    struct Read { Json facts; std::shared_ptr<Gate> gate; };
    mutable std::mutex planMutex_, traceMutex_;
    Json defaultFacts_;
    std::deque<Read> plan_;
    std::vector<std::shared_ptr<Gate>> gates_;
    Json legacy_ = Json::array();
    Event progress_{false};
    bool closed_ = false;
#if !defined(YMCC_MODE_SOURCE_ORIGINAL)
    std::vector<std::shared_ptr<const Snapshot>> sources_;
    std::vector<std::uint64_t> invalidations_;
#endif
public:
    const Session session = syntheticSession();
    std::atomic<unsigned> reads{0}, stable{0}, legacyReentries{0}, callbackErrors{0};
#if !defined(YMCC_MODE_SOURCE_ORIGINAL)
    std::atomic<unsigned> changedReentries{0}, invalidatedReentries{0};
    std::uint64_t expectedWorkerWaitFailures = 0;
    std::function<void(std::shared_ptr<const Snapshot>)> afterChanged;
    std::function<void(std::uint64_t)> afterInvalidated;
    std::function<void(steamui::ModeObserverConfig&)> beforeInitialize;
#endif
    steamui::ModeObserver observer;
    Harness() : defaultFacts_(sample(syntheticSession())) {}
    ~Harness() { finish(); }
    Harness(const Harness&) = delete;
    template<class F> bool wait(F predicate, DWORD ms = waitLimit) {
        const auto end = GetTickCount64() + ms;
        for (;;) {
            if (predicate()) return true;
            const auto now = GetTickCount64();
            if (now >= end) return predicate();
            progress_.wait(static_cast<DWORD>((std::min)(end - now, ULONGLONG{20})));
        }
    }
    std::shared_ptr<Gate> gate() {
        auto value = std::make_shared<Gate>();
        std::lock_guard lock(planMutex_); gates_.push_back(value); return value;
    }
    std::shared_ptr<Gate> queue(Json value, bool blocked = true) {
        auto block = blocked ? gate() : std::shared_ptr<Gate>{};
        std::lock_guard lock(planMutex_);
        defaultFacts_ = value; plan_.push_back({std::move(value), block}); return block;
    }
    void setDefault(Json value) { std::lock_guard lock(planMutex_); defaultFacts_ = std::move(value); }
    bool start(bool css = false) {
        steamui::ModeObserverConfig config;
        config.debounceMs = 0; config.minimumGapMs = 1; config.failureGapMs = 1;
        config.read = [this](const Session&) {
            Read item;
            {
                std::lock_guard lock(planMutex_);
                if (plan_.empty()) item.facts = defaultFacts_;
                else { item = std::move(plan_.front()); plan_.pop_front(); }
            }
            ++reads; progress_.signal();
            if (item.gate && !item.gate->arrive())
                return Json{{"known", false}, {"mode", "unknown"}, {"reason", "fixture-read-gate-timeout"}};
            return item.facts;
        };
        config.changed = [this](const Session& s, const Json& value) {
            try {
                observer.snapshot(s); observer.setDemand(steamui::ModeConsumer::Decky, true);
                ++legacyReentries;
                { std::lock_guard lock(traceMutex_); legacy_.push_back({{"session", sessionJson(s)}, {"facts", value}}); }
            } catch (...) { ++callbackErrors; }
            progress_.signal();
        };
        config.stableBigPicture = [this](const Session&) { ++stable; progress_.signal(); };
#if !defined(YMCC_MODE_SOURCE_ORIGINAL)
        config.sourceChanged = [this](std::shared_ptr<const Snapshot> value) {
            try {
                observer.snapshot(value->session); observer.observedSource(value->session);
                observer.setDemand(steamui::ModeConsumer::Decky, true);
                ++changedReentries;
                { std::lock_guard lock(traceMutex_); sources_.push_back(value); }
                progress_.signal();
                if (afterChanged) afterChanged(std::move(value));
            } catch (...) { ++callbackErrors; }
            progress_.signal();
        };
        config.sourceInvalidated = [this](std::uint64_t revision) {
            try {
                // Read-only reentry also works after final demand withdrawal/shutdown.
                observer.snapshot(session); observer.observedSource(session);
                ++invalidatedReentries;
                { std::lock_guard lock(traceMutex_); invalidations_.push_back(revision); }
                progress_.signal();
                if (afterInvalidated) afterInvalidated(revision);
            } catch (...) { ++callbackErrors; }
            progress_.signal();
        };
        if (beforeInitialize) beforeInitialize(config);
#endif
        if (!observer.initialize(std::move(config))) return false;
        observer.setDemand(steamui::ModeConsumer::Decky, true);
        if (css) observer.setDemand(steamui::ModeConsumer::Css, true);
        observer.observeSteam(session);
        return true;
    }
    unsigned legacyCount() const { std::lock_guard lock(traceMutex_); return static_cast<unsigned>(legacy_.size()); }
    bool legacyUnknown() const {
        std::lock_guard lock(traceMutex_);
        for (const auto& row : legacy_) if (row["facts"].value("mode", std::string{}) == "unknown") return true;
        return false;
    }
#if !defined(YMCC_MODE_SOURCE_ORIGINAL)
    unsigned sourceCount() const { std::lock_guard lock(traceMutex_); return static_cast<unsigned>(sources_.size()); }
    unsigned invalidationCount() const { std::lock_guard lock(traceMutex_); return static_cast<unsigned>(invalidations_.size()); }
    std::uint64_t lastInvalidation() const {
        std::lock_guard lock(traceMutex_); return invalidations_.empty() ? 0 : invalidations_.back();
    }
    std::shared_ptr<const Snapshot> lastSource() const {
        std::lock_guard lock(traceMutex_); return sources_.empty() ? nullptr : sources_.back();
    }
#endif
    void releaseAll() noexcept {
        std::lock_guard lock(planMutex_);
        for (const auto& value : gates_) value->release();
    }
    void finish() noexcept {
        if (closed_) return;
        releaseAll(); observer.shutdown(); closed_ = true;
    }
    void closeAndRecord(Case& c, std::uint64_t retainedEvents = 0) {
        finish();
        bool gatesOk = true;
        { std::lock_guard lock(planMutex_); for (const auto& value : gates_) gatesOk = gatesOk && !value->timedOut.load(); }
        c.check("all-read-and-callback-gates-released-before-shutdown", gatesOk);
        c.check("callbacks-reenter-without-exception", callbackErrors == 0);
        Json trace{{"readCalls", reads.load()}, {"stableCallbacks", stable.load()},
            {"legacyReentries", legacyReentries.load()}, {"observerShutdownCompleted", closed_}};
        {
            std::lock_guard lock(traceMutex_); trace["legacyChanged"] = legacy_;
#if !defined(YMCC_MODE_SOURCE_ORIGINAL)
            trace["sourceChanged"] = Json::array();
            for (const auto& source : sources_) trace["sourceChanged"].push_back(sourceJson(*source));
            trace["sourceInvalidated"] = invalidations_;
            c.check("source-callbacks-complete-lock-free-reentry",
                changedReentries == sources_.size() && invalidatedReentries == invalidations_.size());
#endif
        }
#if !defined(YMCC_MODE_SOURCE_ORIGINAL)
        const auto d = observer.sourceEventDiagnostics();
        trace["sourceEventDiagnostics"] = diagnosticsJson(d);
        trace["expectedRetainedEvents"] = retainedEvents;
        c.check("source-event-raii-accounting", d.created == d.closed + retainedEvents &&
            d.createFailed == 0 && d.signalFailed == 0 && d.closeFailed == 0 &&
            d.workerWaitFailed == expectedWorkerWaitFailures &&
            d.lastError == (expectedWorkerWaitFailures ? ERROR_INVALID_HANDLE : ERROR_SUCCESS),
            diagnosticsJson(d));
#else
        (void)retainedEvents;
#endif
        if (!c.metadata.contains("harnesses")) c.metadata["harnesses"] = Json::array();
        c.metadata["harnesses"].push_back(std::move(trace));
    }
};
template<class F> static void withHarness(Case& c, F body, std::uint64_t retainedEvents = 0) {
    Harness h;
    try { body(h); }
    catch (...) { h.finish(); throw; }
    h.closeAndRecord(c, retainedEvents);
}

#if defined(YMCC_MODE_SOURCE_ORIGINAL)
static bool warm(Case& c, Harness& h) {
    if (!c.check("original-observer-initializes", h.start())) return false;
    return c.check("original-changed-captures-warm-mode", h.wait([&] {
        return h.legacyCount() == 1 && h.observer.snapshot(h.session).value("mode", std::string{}) == "bigpicture";
    }));
}
#else
static bool currentSource(Case& c, Harness& h, const Session& session, const Json& facts, const std::string& prefix) {
    auto lease = h.observer.observedSource(session);
    if (!c.check(prefix + ":lease-object", static_cast<bool>(lease))) return false;
    const auto& s = lease.snapshot();
    const auto notice = h.lastSource();
    c.check(prefix + ":current-and-unsignaled", lease.current() && lease.cancellationEvent() &&
        WaitForSingleObject(lease.cancellationEvent(), 0) == WAIT_TIMEOUT);
    c.check(prefix + ":snapshot-metadata", s.session == session && s.modeFacts == facts &&
        s.mode == facts.value("mode", std::string{}) && s.modeEpoch == facts.value("modeEpoch", std::string{}) && s.modeRevision > 0,
        sourceJson(s));
    c.check(prefix + ":sourceChanged-metadata", notice && notice->modeRevision == s.modeRevision &&
        notice->session == session && notice->modeFacts == facts && notice->mode == s.mode && notice->modeEpoch == s.modeEpoch);
    return lease.current();
}
static bool warm(Case& c, Harness& h, bool css = false) {
    if (!c.check("candidate-observer-initializes", h.start(css))) return false;
    if (!c.check("warm-source-and-callback-ready", h.wait([&] {
        auto lease = h.observer.observedSource(h.session);
        return lease && lease.current() && h.sourceCount() == 1 && h.legacyCount() == 1;
    }))) return false;
    currentSource(c, h, h.session, sample(h.session), "warm");
    auto lease = h.observer.observedSource(h.session);
    c.check("warm-ui-projection", lease.snapshot().uiAccount.accountID32 == 101 &&
        lease.snapshot().uiAccount.currentAccountKnown() &&
        lease.snapshot().uiAccount.deckyAccountWatchAvailable == steamaccount::Truth::True &&
        lease.snapshot().uiLocal.value == 101 && !lease.snapshot().uiLocal.fromUi);
    return true;
}
static bool rotate(Case& c, Harness& h, const Json& facts, const std::string& label) {
    auto old = h.observer.observedSource(h.session);
    if (!c.check(label + ":previous-source-present", old && old.current())) return false;
    const auto revision = old.snapshot().modeRevision;
    const auto previousFacts = old.snapshot().modeFacts;
    const auto changed = h.sourceCount(), invalidated = h.invalidationCount();
    h.queue(facts, false);
    c.check(label + ":health-refresh-admitted", h.observer.requestRefresh(h.session, false));
    const bool ready = h.wait([&] {
        auto next = h.observer.observedSource(h.session);
        return next && next.current() && next.snapshot().modeRevision > revision && h.sourceCount() == changed + 1;
    });
    c.check(label + ":fresh-revision-published", ready);
    c.check(label + ":previous-lease-revoked", !old.current() &&
        WaitForSingleObject(old.cancellationEvent(), 0) == WAIT_OBJECT_0 && old.snapshot().modeFacts == previousFacts);
    c.check(label + ":invalidation-notification-independent", h.invalidationCount() == invalidated + 1 &&
        h.lastInvalidation() > revision);
    if (ready) currentSource(c, h, h.session, facts, label);
    return ready;
}
class AwaitConsumer {
    Event entered_, resume_;
    std::atomic<DWORD> waited_{WAIT_FAILED};
    std::atomic<unsigned> commits_{0};
    JoiningThread worker_;
public:
    AwaitConsumer(const Lease& lease, bool ignoreCurrent)
        : worker_([this, &lease, ignoreCurrent] {
            entered_.signal();
            const auto result = resume_.wait(gateLimit); waited_.store(result);
            if (result == WAIT_OBJECT_0 && (ignoreCurrent || lease.current())) ++commits_;
        }, [this] { resume_.signal(); }) {}
    bool entered() const { return entered_.wait() == WAIT_OBJECT_0; }
    void resumeAndJoin() { resume_.signal(); worker_.join(); }
    unsigned commits() const noexcept { return commits_.load(); }
    DWORD waited() const noexcept { return waited_.load(); }
};
struct RevisionConsumer {
    std::mutex mutex;
    std::uint64_t fence = 0, applied = 0;
    std::vector<std::uint64_t> accepted, dropped;
    void invalidate(std::uint64_t value) { std::lock_guard lock(mutex); fence = (std::max)(fence, value); }
    void apply(const std::shared_ptr<const Snapshot>& value) {
        std::lock_guard lock(mutex);
        if (value->modeRevision <= fence || value->modeRevision <= applied) dropped.push_back(value->modeRevision);
        else { applied = value->modeRevision; accepted.push_back(applied); }
    }
    Json state() { std::lock_guard lock(mutex); return {{"fence", fence}, {"applied", applied}, {"accepted", accepted}, {"dropped", dropped}}; }
};
struct CallbackCopyState {
    std::atomic<bool> armed{false};
    std::atomic<unsigned> allowedCopies{0}, forbiddenCopies{0}, changedCalls{0}, invalidatedCalls{0};
};
template<class Argument> class ThrowOnCopyCallback {
    std::shared_ptr<CallbackCopyState> state_;
    std::shared_ptr<const std::function<void(Argument)>> invocation_;
public:
    ThrowOnCopyCallback(std::shared_ptr<CallbackCopyState> state, std::function<void(Argument)> invocation)
        : state_(std::move(state)), invocation_(std::make_shared<const std::function<void(Argument)>>(std::move(invocation))) {}
    ThrowOnCopyCallback(const ThrowOnCopyCallback& other) : state_(other.state_) {
        if (state_->armed.load(std::memory_order_acquire)) {
            ++state_->forbiddenCopies;
            throw std::runtime_error("armed-source-callback-copy-constructor");
        }
        ++state_->allowedCopies; invocation_ = other.invocation_;
    }
    ThrowOnCopyCallback(ThrowOnCopyCallback&&) noexcept = default;
    void operator()(Argument argument) const {
        // Invocation has no injected throw: this control targets copying only.
        if constexpr (std::is_same_v<Argument, std::uint64_t>) ++state_->invalidatedCalls;
        else ++state_->changedCalls;
        (*invocation_)(std::move(argument));
    }
};
#endif
} // namespace mode_source_directed
#include "fixtures/steam_mode_read/producer_adapter.inc"
#include "decky_sidebar_diagnostics.h"

#if !defined(YMCC_MODE_SOURCE_ORIGINAL)
namespace mode_source_directed {
// Same in-flight/one-follow-up contract as the health overlap case, with
// exact source attribution and Decky's admission-only diagnostic consumer.
static void supersededModeRead(Case& c, const std::string& kind) {
    const bool health = kind == "health", retired = kind == "retired", off = kind == "off";
    const bool connectionFirst = kind == "connection";
    const bool parked = kind == "parked";
    Json receipts = Json::array();
    bool receivedAtParkedState = false;
    std::atomic<bool> enabled{true};
    Harness h;
    const auto first = h.queue(sample(h.session, "bigpicture", "discard-candidate"));
    auto nextSession = h.session;
    if (retired) ++nextSession.created;
    const auto second = h.queue(sample(nextSession, "bigpicture", "fresh-follow-up"));
    const auto third = parked ? h.queue(sample(h.session, "bigpicture", "terminal-discard")) : nullptr;
#if !defined(YMCC_MODE_WITHOUT_SUPERSEDED_RECEIPT)
    h.beforeInitialize = [&](steamui::ModeObserverConfig& config) {
        config.deckyReadSuperseded = [&](const Session& session, const steamui::ModeReadSuperseded& read) {
            // A callback under the observer mutex would block on this read.
            const auto snapshot = h.observer.snapshot(session);
            const auto detail = parked ? snapshot.value("reason", "shared-mode-pending") : "shared-mode-pending";
            if (!deckydiag::shouldLogSupersededModeRead(enabled, 0, "steam-ui-state-pending", detail)) return;
            receivedAtParkedState |= detail == "shared-mode-events-unsettled";
            receipts.push_back(deckydiag::record("steam-mode-read-superseded",
                deckydiag::supersededModeReadFields(session.pid, session.created, read)));
        };
    };
#endif
    if (!h.start(off)) { c.check("observer-started", false); return; }
    const bool firstEntered = first->entered.wait() == WAIT_OBJECT_0;
    bool accepted = h.observer.requestRefresh(h.session, false);
    auto origin = std::source_location::current();
    if (!health) {
        accepted &= connectionFirst ? h.observer.requestConnectionRefresh(h.session, origin)
                                    : h.observer.requestRefresh(h.session, true, origin);
        for (unsigned i = 0; i < 1000; ++i) {
            accepted &= connectionFirst ? h.observer.requestRefresh(h.session, true)
                                        : h.observer.requestConnectionRefresh(h.session);
        }
    }
    if (retired) h.observer.observeSteam(nextSession);
    if (off) { enabled = false; h.observer.setDemand(steamui::ModeConsumer::Decky, false); }
    first->release();
    const bool secondEntered = second->entered.wait() == WAIT_OBJECT_0;
    const auto during = h.observer.snapshot(nextSession);
    const auto deliveries = h.sourceCount();
    if (parked) {
        accepted &= h.observer.requestRefresh(h.session);
        second->release();
        const bool thirdEntered = third->entered.wait() == WAIT_OBJECT_0;
        const auto terminalOrigin = std::source_location::current();
        accepted &= h.observer.requestRefresh(h.session, true, terminalOrigin);
        third->release();
        const bool exhausted = h.wait([&] {
            return h.observer.snapshot(h.session).value("reason", "") == "shared-mode-events-unsettled";
        });
        h.closeAndRecord(c);
        c.check("original-retry-parks-after-three-without-publishing", firstEntered && secondEntered && thirdEntered &&
            accepted && exhausted && h.reads==3 && deliveries==0 && h.sourceCount()==0 && h.legacyCount()==0);
        c.check("two-original-finite-discard-receipts", receipts.size()>=2 &&
            receipts[0].contains("nextReadMs") && !receipts[0].contains("retryParked") &&
            receipts[1].contains("nextReadMs") && !receipts[1].contains("retryParked"));
        c.check("parked-discard-receipt-without-fabricated-next-read", receivedAtParkedState && receipts.size()==3 && receipts.back().size()==8 &&
            receipts.back().value("retryParked",false) && !receipts.back().contains("nextReadMs") &&
            receipts.back()["hintCause"]=="mode-refresh" && receipts.back()["hintOriginLine"]==terminalOrigin.line() &&
            receipts.back()["hintCount"]==1 && receipts.back()["steamPid"]==h.session.pid &&
            receipts.back()["steamBirth"]==std::to_string(h.session.created));
        c.metadata["discardReceipts"] = receipts;
        return;
    }
    second->release();
    const bool fresh = h.wait([&] { return h.observer.snapshot(nextSession).value("modeEpoch", "") == "fresh-follow-up"; });
    h.closeAndRecord(c);
    c.check("original-in-flight-hints-coalesce-to-one-follow-up", firstEntered && accepted && secondEntered && fresh && h.reads == 2);
    c.check("only-non-invalidating-hint-preserves-first-mode", deliveries == (health ? 1u : 0u) &&
        during.value("mode", "") == (health ? "bigpicture" : "unknown"));
    c.metadata["discardReceipts"] = receipts;
    if (!health && !retired && !off) {
        c.check("exactly-one-finite-discard-receipt-with-first-hint", receipts.size()==1 &&
            receipts[0].size()==8 && receipts[0]["event"]=="steam-mode-read-superseded" &&
            receipts[0]["hintCause"]==(connectionFirst?"connection-refresh":"mode-refresh") &&
            receipts[0]["hintOriginLine"]==origin.line() && receipts[0]["hintCount"]==1001 &&
            receipts[0]["steamPid"]==h.session.pid && receipts[0]["steamBirth"]==std::to_string(h.session.created) &&
            receipts[0]["readStartedTick"].is_number_unsigned() && receipts[0]["readStartedTick"]>0 &&
            receipts[0]["nextReadMs"].is_number_unsigned() && receipts[0]["nextReadMs"]<=500);
    } else {
        c.check("healthy-off-or-retired-read-has-no-discard-receipt", receipts.empty());
    }
    c.check("diagnostic-admission-only", !deckydiag::shouldLogSupersededModeRead(false, 0, "steam-ui-state-pending", "shared-mode-pending") &&
        !deckydiag::shouldLogSupersededModeRead(true, 1, "steam-ui-state-pending", "shared-mode-pending") &&
        !deckydiag::shouldLogSupersededModeRead(true, 0, "connected", "shared-mode-pending") &&
        !deckydiag::shouldLogSupersededModeRead(false, 0, "steam-ui-state-pending", "shared-mode-events-unsettled") &&
        !deckydiag::shouldLogSupersededModeRead(true, 1, "steam-ui-state-pending", "shared-mode-events-unsettled") &&
        !deckydiag::shouldLogSupersededModeRead(true, 0, "steam-ui-state-pending", "live-busy"));
    c.check("diagnostic-rejects-unlisted-fields-and-values", deckydiag::record("steam-mode-read-superseded",
        {{"hintCause", "private"}, {"hintCount", -1}, {"steamBirth", "bad"}, {"account", h.session.account},
         {"retryParked", "true"}}).size()==1);
}
}
#endif


#if !defined(YMCC_MODE_SOURCE_ORIGINAL)
namespace mode_source_directed {
static std::atomic<std::uint64_t> passiveGeneration{1};
static sleeppower::SharedPowerV1 passivePower(std::uint64_t generation) {
    sleeppower::SharedPowerV1 power;
    power.generation = generation;
    power.publicationRevision = generation;
    power.flags = sleeppower::kKnown | sleeppower::kHardwareAllowed;
    return power;
}
static int __cdecl readPassivePower(sleeppower::SharedPowerV1* out, std::uint32_t size) {
    if (!out || size != sizeof(*out)) return 0;
    *out = passivePower(passiveGeneration.load());
    return 1;
}
static void passiveCachePower(Case& c) {
    std::atomic<unsigned> settlements{0};
    withHarness(c, [&](Harness& h) {
        passiveGeneration = 1;
        h.beforeInitialize = [&](steamui::ModeObserverConfig& config) {
            config.admissionPowerReader = readPassivePower;
            config.settled = [&](const Session&) { ++settlements; };
        };
        if (!warm(c, h)) return;
        const auto read = [&]<class Observer>(Observer& observer, const sleeppower::SharedPowerV1& power) {
            if constexpr (requires { observer.passiveSnapshot(h.session, &power); })
                return observer.passiveSnapshot(h.session, &power);
            else return observer.passiveSnapshot(h.session); // Original source control.
        };
        auto published = passivePower(1);
        ++published.publicationRevision;
        c.check("publication-only-keeps-cache-without-new-read",
            read(h.observer, published).value("mode", std::string{}) == "bigpicture" && h.reads == 1);
        auto otherNotification = published;
        ++otherNotification.observedNotificationSequence;
        const auto staleNotification = read(h.observer, otherNotification);
        c.check("new-notification-cannot-read-old-positive-cache", staleNotification.is_object() &&
            !staleNotification.value("known", true) && staleNotification.value("pid", h.session.pid) == 0 && h.reads == 1);
        passiveGeneration = 2;
        const auto current = passivePower(2);
        const auto stale = read(h.observer, current);
        c.check("new-power-cannot-read-old-positive-cache-or-open-private-read", stale.is_object() &&
            !stale.value("known", true) && !stale.value("running", true) &&
            stale.value("birth", std::string{}) == "0" && h.reads == 1, {{"receipt", stale}});
        const auto next = h.queue(sample(h.session));
        const bool refresh = h.observer.requestPublicResumeRefresh(h.session, current);
        const bool entered = next->entered.wait() == WAIT_OBJECT_0;
        next->release();
        const bool completed = h.wait([&] { return h.stable == 1; });
        h.finish();
        c.check("original-ready-refresh-reads-same-mode-once-and-releases-pending-display",
            refresh && entered && completed && h.reads == 2 && settlements == 1);
    });
}
}
#endif

int main(int argc, char** argv) {
    using namespace mode_source_directed;
    const std::string selection = argc > 1 ? argv[1] : "";
    Json report{{"selection", selection}, {"cases", Json::array()}, {"caseIds", Json::array()}, {"failedCases", Json::array()},
        {"metadata", {{"directed", true}, {"defaultSuiteExecuted", false}, {"realSteamOperations", 0},
            {"identityObserverStarted", false}, {"registryReads", 0}, {"processEnumerations", 0},
            {"syntheticSession", sessionJson(syntheticSession())}, {"debounceMs", 0}, {"minimumGapMs", 1},
            {"gateLimitMs", gateLimit}, {"waitLimitMs", waitLimit},
#if defined(YMCC_MODE_SOURCE_ORIGINAL)
            {"originalHeader", true}
#else
            {"originalHeader", false}
#endif
        }}};
    report["metadata"]["unreachedCases"] = Json::array();
    auto run = [&](const char* name, auto body) {
        if (!report["failedCases"].empty()) {
            report["metadata"]["unreachedCases"].push_back(name);
            report["metadata"]["unreachedCaseStatus"] = "NOT_RUN";
            return;
        }
        const auto injectedBefore = ymcc_mode_source_wait_port::injected.load();
        Case c; c.name = name;
        try { body(c); }
        catch (const std::exception& error) { c.check("fixture-exception-after-safe-shutdown", false, {{"what", error.what()}}); }
        catch (...) { c.check("fixture-exception-after-safe-shutdown", false); }
        if (c.assertions.empty()) c.check("selected-case-has-assertions", false);
        const bool pendingWaitFailure = ymcc_mode_source_wait_port::failNext.exchange(false, std::memory_order_acq_rel);
        const auto injectedThisCase = ymcc_mode_source_wait_port::injected.load() - injectedBefore;
        c.check("worker-wait-failure-port-is-disarmed-and-case-scoped", !pendingWaitFailure &&
            injectedThisCase == (c.name == "worker-wait-failure-revokes-source" ? 1u : 0u));
        c.metadata["workerWaitPort"] = {{"failNextAtCaseEnd", pendingWaitFailure},
            {"injectedThisCase", injectedThisCase}, {"realOsForwardsTotal", ymcc_mode_source_wait_port::forwarded.load()}};
        if (!c.passed) report["failedCases"].push_back(c.name);
        report["cases"].push_back(c.json());
    };
    const bool producerOnly=argc==4&&selection=="--mode-source-local-contention"&&std::string(argv[2])=="--case"&&(std::string(argv[3])=="producer-cache"||std::string(argv[3])=="producer-cache-control"||std::string(argv[3])=="producer-owner");
    const bool retryOnly=argc==4&&selection=="--mode-source-local-contention"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="retry-budget";
    bool validSelection = argc == 2 || producerOnly || retryOnly;
#if defined(YMCC_MODE_SOURCE_ORIGINAL)
    validSelection = validSelection && (selection == "--mode-source-before-invalidation" || selection == "--mode-source-before-unknown");
#else
    const bool superseded = argc==4 && selection=="--mode-source-health-overlap" && std::string(argv[2])=="--case" &&
        (std::string(argv[3])=="window" || std::string(argv[3])=="connection" || std::string(argv[3])=="health" ||
         std::string(argv[3])=="retired" || std::string(argv[3])=="off" || std::string(argv[3])=="parked");
    validSelection = superseded || (validSelection && (selection == "--mode-source-only" || selection == "--mode-source-ignore-current-control" || selection == "--mode-source-local-contention" || selection == "--mode-source-local-contention-control" || selection == "--public-cache-power"));
#endif
    report["metadata"]["selectionValid"] = validSelection;
    if (!validSelection) {
        report["metadata"]["error"] = "exactly-one-supported-selection-required";
        report["passed"] = false; report["exitCode"] = 2;
        std::cout << report.dump() << '\n'; return 2;
    }

#if defined(YMCC_MODE_SOURCE_ORIGINAL)
    if (selection == "--mode-source-before-invalidation") {
        run("before-invalidation-unknown-notification", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) { c.check("strong-hint-publishes-unknown-before-return", false); return; }
                const auto next = h.queue(sample(h.session));
                const bool admitted = h.observer.requestRefresh(h.session, true);
                const bool publishedBeforeReturn = h.legacyUnknown();
                const auto snapshotAfterHint = h.observer.snapshot(h.session);
                const bool readerBlocked = next->entered.wait() == WAIT_OBJECT_0;
                h.finish(); // No throwing assertion or JSON construction while IO remains blocked.
                c.check("strong-hint-admitted-with-fresh-read-held", admitted && readerBlocked);
                c.check("strong-hint-publishes-unknown-before-return", publishedBeforeReturn,
                    {{"snapshotAfterHint", snapshotAfterHint}, {"unknownChangedBeforeReturn", publishedBeforeReturn}});
            });
        });
    } else {
        run("before-unknown-unknown-notification", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) { c.check("unknown-read-publishes-unknown-changed", false); return; }
                auto unknown = sample(h.session, "unknown"); unknown["reason"] = "synthetic-unknown";
                const auto read = h.queue(unknown);
                const bool admitted = h.observer.requestRefresh(h.session, false);
                const bool readerBlocked = read->entered.wait() == WAIT_OBJECT_0;
                read->release();
                const bool completed = h.wait([&] {
                    return h.observer.snapshot(h.session).value("mode", std::string{}) == "unknown";
                });
                h.finish();
                c.check("healthy-refresh-reaches-unknown-read", admitted && readerBlocked && completed);
                c.check("unknown-read-publishes-unknown-changed", h.legacyUnknown(),
                    {{"unknownChanged", h.legacyUnknown()}, {"readCalls", h.reads.load()}});
            });
        });
    }
#else
    if (selection == "--public-cache-power") {
        run("public-passive-cache-belongs-to-power-observation", passiveCachePower);
    } else if(producerOnly){
        if(std::string(argv[3])=="producer-owner")run("native-producer-local-lock-tag-and-cancel",[&](Case& c){producerChecks(c);});
        else run("native-producer-cache-return-owner",[&](Case& c){producerCacheChecks(c,std::string(argv[3])=="producer-cache-control");});
    } else if (selection == "--mode-source-health-overlap") {
        report["metadata"]["parameter"] = argv[3];
        run("in-flight-health-overlap-one-follow-up", [&](Case& c) { supersededModeRead(c, argv[3]); });
    } else if (selection == "--mode-source-local-contention" || selection == "--mode-source-local-contention-control") {
#include "fixtures/steam_mode_read/local_contention.inc"
    } else if (selection == "--mode-source-ignore-current-control") {
        run("consumer-rechecks-current-after-await", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) { c.check("consumer-rechecks-current-after-await", false); return; }
                auto lease = h.observer.observedSource(h.session);
                AwaitConsumer consumer(lease, true); // Deliberate mutation: no current() check after await.
                c.check("consumer-await-entered", consumer.entered());
                const auto read = h.queue(sample(h.session));
                const bool admitted = h.observer.requestRefresh(h.session, true);
                c.check("control-starts-from-real-revoked-lease", admitted && !lease.current() &&
                    WaitForSingleObject(lease.cancellationEvent(), 0) == WAIT_OBJECT_0);
                consumer.resumeAndJoin(); read->release(); h.finish();
                c.check("consumer-await-completed", consumer.waited() == WAIT_OBJECT_0);
                c.check("consumer-rechecks-current-after-await", consumer.commits() == 0,
                    {{"ignoreCurrent", true}, {"commits", consumer.commits()}, {"revokedRevision", lease.snapshot().modeRevision}});
            });
        });
    } else {
        run("strong-hint-synchronous-revocation", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) return;
                auto old = h.observer.observedSource(h.session);
                const auto revision = old.snapshot().modeRevision;
                const auto invalidated = h.invalidationCount();
                AwaitConsumer consumer(old, false);
                c.check("consumer-await-entered", consumer.entered());
                const auto read = h.queue(sample(h.session));
                const bool admitted = h.observer.requestRefresh(h.session, true);
                const bool revokedAtReturn = !old.current();
                const auto signaledAtReturn = WaitForSingleObject(old.cancellationEvent(), 0);
                c.check("strong-hint-revokes-before-return", admitted && revokedAtReturn && signaledAtReturn == WAIT_OBJECT_0);
                c.check("revocation-notification-is-separately-delivered", h.invalidationCount() == invalidated + 1 && h.lastInvalidation() > revision);
                c.check("replacement-read-is-controlled", read->entered.wait() == WAIT_OBJECT_0);
                consumer.resumeAndJoin();
                c.check("consumer-rechecks-current-after-await", consumer.waited() == WAIT_OBJECT_0 && consumer.commits() == 0);
                read->release();
                const bool ready = h.wait([&] { auto next = h.observer.observedSource(h.session); return next && next.current() && next.snapshot().modeRevision > revision && h.sourceCount() == 2; });
                c.check("same-facts-after-strong-hint-get-new-revision", ready && !old.current());
                if (ready) currentSource(c, h, h.session, sample(h.session), "reconfirmed");
            });
        });
        run("unknown-read-revokes-source", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) return;
                auto old = h.observer.observedSource(h.session);
                const auto revision = old.snapshot().modeRevision;
                auto facts = sample(h.session, "unknown"); facts["reason"] = "synthetic-unknown";
                const auto read = h.queue(facts);
                c.check("health-unknown-read-admitted", h.observer.requestRefresh(h.session, false));
                c.check("health-keeps-source-until-read-completes", read->entered.wait() == WAIT_OBJECT_0 && old.current() &&
                    WaitForSingleObject(old.cancellationEvent(), 0) == WAIT_TIMEOUT);
                read->release();
                c.check("unknown-result-revokes", h.wait([&] { return !old.current() && h.invalidationCount() == 1; }));
                auto unavailable = h.observer.observedSource(h.session);
                c.check("unknown-has-no-current-source", !unavailable && WaitForSingleObject(old.cancellationEvent(), 0) == WAIT_OBJECT_0);
                c.check("unknown-notification-revision", h.lastInvalidation() > revision && h.sourceCount() == 1 && !h.legacyUnknown());
            });
        });
        run("healthy-identical-refresh-preserves-source", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) return;
                auto old = h.observer.observedSource(h.session);
                const auto* address = &old.snapshot(); const auto event = old.cancellationEvent();
                const auto revision = old.snapshot().modeRevision;
                const auto read = h.queue(sample(h.session));
                c.check("identical-health-refresh-admitted", h.observer.requestRefresh(h.session, false));
                c.check("identical-health-inflight-keeps-event", read->entered.wait() == WAIT_OBJECT_0 && old.current() && WaitForSingleObject(event, 0) == WAIT_TIMEOUT);
                read->release();
                c.check("identical-health-completes-original-stable-callback", h.wait([&] { return h.stable >= 1; }));
                auto same = h.observer.observedSource(h.session);
                c.check("identical-health-preserves-object-event-revision", same && same.current() && &same.snapshot() == address &&
                    same.cancellationEvent() == event && same.snapshot().modeRevision == revision && h.sourceCount() == 1 && h.invalidationCount() == 0);
                auto healthOnly = sample(h.session); healthOnly["deckyBackendConnected"] = true;
                h.queue(healthOnly, false); c.check("decky-health-only-refresh-admitted", h.observer.requestRefresh(h.session, false));
                c.check("decky-health-retains-original-cached-changed-path", h.wait([&] { return h.legacyCount() == 2; }) && h.observer.snapshot(h.session) == healthOnly);
                auto retained = h.observer.observedSource(h.session);
                c.check("decky-health-fields-do-not-rotate-mode-source", retained && retained.current() && &retained.snapshot() == address &&
                    retained.cancellationEvent() == event && retained.snapshot().modeRevision == revision && retained.snapshot().modeFacts == sample(h.session) &&
                    h.sourceCount() == 1 && h.invalidationCount() == 0 && WaitForSingleObject(event, 0) == WAIT_TIMEOUT);
            });
        });
        run("mode-and-document-change-rotate-source", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) return;
                if (!rotate(c, h, sample(h.session, "desktop"), "mode-change")) return;
                if (!rotate(c, h, sample(h.session, "desktop", "doc-b:1"), "document-change")) return;
                rotate(c, h, sample(h.session, "desktop", "doc-b:2"), "same-document-reentry");
            });
        });
        run("ui-account-and-watch-change-rotate-source", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) return;
                auto account = sample(h.session); account["observedAccount"] = 202;
                if (!rotate(c, h, account, "ui-account-change")) return;
                auto lease = h.observer.observedSource(h.session);
                c.check("ui-account-projection-is-not-registry-identity", lease.snapshot().session.account == 101 &&
                    lease.snapshot().uiAccount.accountID32 == 202 && lease.snapshot().uiLocal.value == 202 && lease.snapshot().uiLocal.fromUi);
                account["deckyAccountWatchAvailable"] = false;
                if (!rotate(c, h, account, "ui-watch-change")) return;
                auto unwatch = h.observer.observedSource(h.session);
                c.check("watch-false-keeps-facts-and-legacy-local-fallback", unwatch.snapshot().uiAccount.accountID32 == 202 &&
                    unwatch.snapshot().uiAccount.deckyAccountWatchAvailable == steamaccount::Truth::False &&
                    unwatch.snapshot().uiLocal.value == 101 && !unwatch.snapshot().uiLocal.fromUi);
            });
        });
        run("malformed-ui-facts-revoke-missing-fields-remain-unknown", [&](Case& c) {
            const std::vector<std::pair<std::string, Json>> bad{{"observedAccount", "202"}, {"observedAccount", 0},
                {"deckyAccountWatchAvailable", 1}, {"deckyAccountWatchAvailable", nullptr}};
            for (const auto& [field, value] : bad) withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) return;
                auto old = h.observer.observedSource(h.session); const auto revision = old.snapshot().modeRevision;
                auto malformed = sample(h.session); malformed[field] = value;
                h.queue(malformed, false); c.check("bad-ui-shape-refresh-admitted", h.observer.requestRefresh(h.session, false));
                c.check("bad-ui-shape-completes-original-read", h.wait([&] { return h.stable >= 1; }));
                auto rejected = h.observer.observedSource(h.session);
                c.check("bad-ui-shape-revokes-without-granting-lease", !rejected && !old.current() &&
                    WaitForSingleObject(old.cancellationEvent(), 0) == WAIT_OBJECT_0 && h.invalidationCount() == 1 &&
                    h.lastInvalidation() > revision && h.sourceCount() == 1, {{"field", field}, {"value", value}});
                auto missing = sample(h.session); missing.erase("observedAccount"); missing.erase("deckyAccountWatchAvailable");
                h.queue(missing, false); c.check("missing-ui-fields-refresh-admitted", h.observer.requestRefresh(h.session, false));
                const bool ready = h.wait([&] { auto next = h.observer.observedSource(h.session); return next && next.current() && h.sourceCount() == 2; });
                c.check("missing-ui-fields-admit-mode-only-source", ready);
                if (ready) {
                    currentSource(c, h, h.session, missing, "missing-ui-fields");
                    auto next = h.observer.observedSource(h.session);
                    c.check("missing-ui-evidence-is-unknown", !next.snapshot().uiAccount.currentAccountKnown() &&
                        next.snapshot().uiAccount.accountID32 == 0 && next.snapshot().uiAccount.deckyAccountWatchAvailable == steamaccount::Truth::Unknown &&
                        next.snapshot().modeRevision > revision && !old.current());
                }
            });
        });
        run("session-replacement-discards-inflight-old-read", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) return;
                auto old = h.observer.observedSource(h.session); const auto revision = old.snapshot().modeRevision;
                const auto read = h.queue(sample(h.session));
                c.check("old-session-read-admitted", h.observer.requestRefresh(h.session, false));
                c.check("old-session-read-is-held", read->entered.wait() == WAIT_OBJECT_0);
                auto replacement = h.session; ++replacement.created; // Same PID, different actual supplied birth.
                const auto replacementFacts = sample(replacement); h.setDefault(replacementFacts);
                h.observer.observeSteam(replacement);
                const bool revokedAtReturn = !old.current(); const auto signalAtReturn = WaitForSingleObject(old.cancellationEvent(), 0);
                c.check("session-replacement-synchronously-revokes", revokedAtReturn && signalAtReturn == WAIT_OBJECT_0);
                c.check("old-session-cannot-borrow-new-source", !h.observer.observedSource(h.session));
                read->release();
                const bool ready = h.wait([&] { auto next = h.observer.observedSource(replacement); return next && next.current() && h.sourceCount() == 2; });
                c.check("only-replacement-source-is-published", ready && h.lastInvalidation() > revision);
                if (ready) currentSource(c, h, replacement, replacementFacts, "replacement");
            });
        });
        run("last-demand-withdrawal-revokes-and-reopen-is-fresh", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h, true)) return;
                auto old = h.observer.observedSource(h.session); const auto revision = old.snapshot().modeRevision;
                h.observer.setDemand(steamui::ModeConsumer::Decky, false);
                c.check("remaining-css-demand-preserves-source", old.current() && h.invalidationCount() == 0 &&
                    WaitForSingleObject(old.cancellationEvent(), 0) == WAIT_TIMEOUT);
                const auto read = h.queue(sample(h.session));
                c.check("css-read-admitted", h.observer.requestRefresh(h.session, false));
                c.check("css-read-is-held", read->entered.wait() == WAIT_OBJECT_0);
                h.observer.setDemand(steamui::ModeConsumer::Css, false);
                c.check("last-demand-synchronously-revokes", !old.current() && WaitForSingleObject(old.cancellationEvent(), 0) == WAIT_OBJECT_0);
                c.check("no-demand-admits-no-source-or-refresh", !h.observer.observedSource(h.session) && !h.observer.requestRefresh(h.session, false));
                c.check("last-demand-notification-revision", h.invalidationCount() == 1 && h.lastInvalidation() > revision);
                read->release(); h.observer.setDemand(steamui::ModeConsumer::Css, true);
                const bool ready = h.wait([&] { auto next = h.observer.observedSource(h.session); return next && next.current() && next.snapshot().modeRevision > revision && h.sourceCount() == 2; });
                c.check("reopening-never-renews-old-lease", ready && !old.current() && WaitForSingleObject(old.cancellationEvent(), 0) == WAIT_OBJECT_0);
                if (ready) currentSource(c, h, h.session, sample(h.session), "reopened");
            });
        });
        run("shutdown-revokes-before-blocked-read-joins", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) return;
                auto old = h.observer.observedSource(h.session); const auto revision = old.snapshot().modeRevision;
                const auto read = h.queue(sample(h.session));
                c.check("shutdown-health-read-admitted", h.observer.requestRefresh(h.session, false));
                c.check("shutdown-read-is-held", read->entered.wait() == WAIT_OBJECT_0);
                Event completed;
                JoiningThread stopper([&] { h.observer.shutdown(); completed.signal(); }, [&] { read->release(); });
                const auto cancelled = WaitForSingleObject(old.cancellationEvent(), waitLimit);
                const bool revokedWhileBlocked = !old.current();
                c.check("shutdown-signals-before-read-can-complete", cancelled == WAIT_OBJECT_0 && revokedWhileBlocked && completed.wait(0) == WAIT_TIMEOUT);
                read->release(); stopper.join();
                c.check("shutdown-joins-and-keeps-retained-object-revoked", completed.wait(0) == WAIT_OBJECT_0 && static_cast<bool>(old) && !old.current() &&
                    old.snapshot().modeRevision == revision && !h.observer.observedSource(h.session));
                c.check("shutdown-notification-is-separately-delivered", h.invalidationCount() == 1 && h.lastInvalidation() > revision && h.sourceCount() == 1);
            });
        });
        run("aba-never-renews-old-lease", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) return;
                auto first = h.observer.observedSource(h.session); const auto revision = first.snapshot().modeRevision;
                const auto firstFacts = first.snapshot().modeFacts;
                if (!rotate(c, h, sample(h.session, "desktop"), "aba-middle")) return;
                auto middle = h.observer.observedSource(h.session); const auto middleRevision = middle.snapshot().modeRevision;
                if (!rotate(c, h, sample(h.session), "aba-return")) return;
                auto last = h.observer.observedSource(h.session);
                c.check("same-facts-do-not-revive-old-generation", last && last.current() && last.snapshot().modeFacts == firstFacts &&
                    last.snapshot().modeRevision > middleRevision && middleRevision > revision && !first.current() && !middle.current());
                c.check("aba-events-are-distinct-and-old-signals-sticky", first.cancellationEvent() != middle.cancellationEvent() &&
                    first.cancellationEvent() != last.cancellationEvent() && middle.cancellationEvent() != last.cancellationEvent() &&
                    WaitForSingleObject(first.cancellationEvent(), 0) == WAIT_OBJECT_0 &&
                    WaitForSingleObject(middle.cancellationEvent(), 0) == WAIT_OBJECT_0 &&
                    WaitForSingleObject(last.cancellationEvent(), 0) == WAIT_TIMEOUT);
            });
        });
        run("callbacks-reenter-and-consumer-drops-late-revision", [&](Case& c) {
            RevisionConsumer consumer;
            withHarness(c, [&](Harness& h) {
                const auto callback = h.gate();
                const auto first = std::make_shared<std::atomic<bool>>(true);
                h.afterChanged = [&consumer, callback, first](std::shared_ptr<const Snapshot> source) {
                    if (first->exchange(false)) callback->arrive();
                    consumer.apply(source);
                };
                h.afterInvalidated = [&](std::uint64_t revision) { consumer.invalidate(revision); };
                if (!c.check("callback-order-observer-initializes", h.start())) return;
                c.check("first-source-callback-held-after-lock-free-reentry", callback->entered.wait() == WAIT_OBJECT_0 && h.changedReentries == 1);
                auto old = h.observer.observedSource(h.session);
                if (!c.check("held-callback-source-is-real-and-current", old && old.current())) return;
                const auto revision = old.snapshot().modeRevision;
                c.check("reentrant-strong-hint-admitted", h.observer.requestRefresh(h.session, true));
                const auto beforeRelease = consumer.state();
                c.check("invalidation-consumer-runs-before-late-changed", !old.current() &&
                    WaitForSingleObject(old.cancellationEvent(), 0) == WAIT_OBJECT_0 && h.invalidatedReentries == 1 &&
                    beforeRelease["fence"].get<std::uint64_t>() > revision && beforeRelease["accepted"].empty());
                callback->release();
                const bool ready = h.wait([&] { const auto state = consumer.state(); return state["accepted"].size() == 1 && state["dropped"].size() == 1 && h.sourceCount() == 2; });
                const auto after = consumer.state();
                c.check("late-sourceChanged-is-dropped-by-revision", ready && after["dropped"] == Json::array({revision}) &&
                    after["applied"].get<std::uint64_t>() > beforeRelease["fence"].get<std::uint64_t>());
                c.metadata["revisionConsumer"] = after;
                currentSource(c, h, h.session, sample(h.session), "after-late-callback");
            });
        });
        run("cancellation-event-lives-through-last-lease", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) return;
                auto first = h.observer.observedSource(h.session); auto last = first;
                const auto event = first.cancellationEvent(); h.observer.shutdown();
                c.check("shutdown-retains-event-for-two-lease-copies", h.observer.sourceEventDiagnostics().created == 1 &&
                    h.observer.sourceEventDiagnostics().closed == 0 && WaitForSingleObject(event, 0) == WAIT_OBJECT_0);
                first = Lease{};
                c.check("nonfinal-lease-release-does-not-close-event", h.observer.sourceEventDiagnostics().closed == 0 &&
                    WaitForSingleObject(last.cancellationEvent(), 0) == WAIT_OBJECT_0 && static_cast<bool>(last) && !last.current());
                last = Lease{};
                c.check("last-lease-release-closes-event-once", h.observer.sourceEventDiagnostics().created == 1 && h.observer.sourceEventDiagnostics().closed == 1);
            });
            std::optional<Lease> retained;
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) return;
                retained.emplace(h.observer.observedSource(h.session));
                const auto event = retained->cancellationEvent(); Event entered;
                std::atomic<DWORD> waited{WAIT_FAILED};
                JoiningThread waiter([&] { entered.signal(); waited.store(WaitForSingleObject(event, gateLimit)); }, [&] { h.releaseAll(); h.observer.shutdown(); });
                c.check("retained-event-waiter-entered", entered.wait() == WAIT_OBJECT_0);
                h.observer.shutdown(); waiter.join();
                c.check("retained-event-wakes-real-os-waiter", waited == WAIT_OBJECT_0 && !retained->current());
            }, 1);
            if (c.check("lease-object-survives-observer-destruction", retained && static_cast<bool>(*retained))) {
                DWORD flags = 0;
                c.check("borrowed-event-valid-after-observer-destruction", GetHandleInformation(retained->cancellationEvent(), &flags) != FALSE &&
                    WaitForSingleObject(retained->cancellationEvent(), 0) == WAIT_OBJECT_0 && !retained->current());
                c.check("retained-snapshot-is-readable-after-observer-destruction", retained->snapshot().session == syntheticSession() &&
                    retained->snapshot().modeFacts == sample(syntheticSession()));
            }
            retained.reset(); // No use, reset or CloseHandle of a borrowed HANDLE after final release.
        });
        run("mode-source/callback-copy-exception-safe", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                const auto copyState = std::make_shared<CallbackCopyState>();
                h.beforeInitialize = [copyState](steamui::ModeObserverConfig& config) {
                    const ThrowOnCopyCallback<std::shared_ptr<const Snapshot>> changed(copyState, std::move(config.sourceChanged));
                    const ThrowOnCopyCallback<std::uint64_t> invalidated(copyState, std::move(config.sourceInvalidated));
                    // Lvalue assignment deliberately proves both targets can be copied before arming.
                    config.sourceChanged = changed; config.sourceInvalidated = invalidated;
                };
                const auto initialRead = h.queue(sample(h.session));
                if (!c.check("copy-control-initializes-with-moved-config", h.start())) return;
                c.check("copy-control-first-publication-is-held", initialRead->entered.wait() == WAIT_OBJECT_0);
                c.check("both-callback-targets-copied-before-arming", copyState->allowedCopies >= 2 && copyState->forbiddenCopies == 0);
                copyState->armed.store(true, std::memory_order_release);
                initialRead->release();
                const bool firstReady = h.wait([&] { auto lease = h.observer.observedSource(h.session); return lease && lease.current() && h.sourceCount() == 1 && h.legacyCount() == 1; });
                if (!c.check("armed-callback-publishes-without-copy", firstReady)) return;
                currentSource(c, h, h.session, sample(h.session), "copy-control-warm");
                auto old = h.observer.observedSource(h.session);
                const auto revision = old.snapshot().modeRevision;
                const auto nextRead = h.queue(sample(h.session));
                bool refreshThrew = false, admitted = false;
                try { admitted = h.observer.requestRefresh(h.session, true); }
                catch (...) { refreshThrew = true; }
                c.check("armed-callback-revocation-does-not-throw", !refreshThrew && admitted && !old.current() &&
                    WaitForSingleObject(old.cancellationEvent(), 0) == WAIT_OBJECT_0 && h.invalidationCount() == 1 && h.lastInvalidation() > revision);
                nextRead->release();
                const bool nextReady = h.wait([&] { auto next = h.observer.observedSource(h.session); return next && next.current() && h.sourceCount() == 2; });
                c.check("armed-callback-republication-delivered", nextReady);
                if (nextReady) currentSource(c, h, h.session, sample(h.session), "copy-control-republished");
                bool shutdownThrew = false;
                try { h.observer.shutdown(); }
                catch (...) { shutdownThrew = true; }
                c.check("armed-callback-shutdown-does-not-throw", !shutdownThrew && h.invalidationCount() == 2 && h.sourceCount() == 2);
                c.check("publish-revoke-and-shutdown-never-copy-armed-targets", copyState->forbiddenCopies == 0 &&
                    copyState->changedCalls == 2 && copyState->invalidatedCalls == 2 && h.callbackErrors == 0,
                    {{"allowedCopies", copyState->allowedCopies.load()}, {"forbiddenCopies", copyState->forbiddenCopies.load()},
                        {"changedCalls", copyState->changedCalls.load()}, {"invalidatedCalls", copyState->invalidatedCalls.load()},
                        {"invocationThrowsInjected", false}});
            });
        });
        run("worker-wait-failure-revokes-source", [&](Case& c) {
            withHarness(c, [&](Harness& h) {
                if (!warm(c, h)) return;
                auto old = h.observer.observedSource(h.session);
                const auto revision = old.snapshot().modeRevision;
                const auto event = old.cancellationEvent();
                const auto before = h.observer.sourceEventDiagnostics();
                c.check("worker-wait-port-starts-clean", !ymcc_mode_source_wait_port::failNext.load() &&
                    before.workerWaitFailed == 0 && before.lastError == ERROR_SUCCESS);
                // inOsWait becomes positive only after the port has chosen real OS forwarding.
                // Thus arming cannot replace the currently waiting call; the refresh wakes it.
                if (!c.check("warm-worker-reaches-real-os-wait", h.wait([&] {
                    return ymcc_mode_source_wait_port::inOsWait.load(std::memory_order_acquire) != 0;
                }))) return;
                h.expectedWorkerWaitFailures = 1;
                ymcc_mode_source_wait_port::failNext.store(true, std::memory_order_release);
                c.check("original-health-refresh-wakes-real-wait", h.observer.requestRefresh(h.session, false));
                const bool failed = h.wait([&] {
                    const auto status = h.observer.sourceEventDiagnostics();
                    return status.workerWaitFailed == 1 && h.invalidationCount() == 1;
                });
                const auto status = h.observer.sourceEventDiagnostics();
                c.check("worker-wait-failure-is-diagnosed-and-notified", failed &&
                    status.workerWaitFailed == 1 && status.lastError == ERROR_INVALID_HANDLE &&
                    h.lastInvalidation() > revision && h.invalidatedReentries == 1, diagnosticsJson(status));
                c.check("worker-wait-failure-revokes-retained-lease", static_cast<bool>(old) && !old.current() &&
                    WaitForSingleObject(event, 0) == WAIT_OBJECT_0 && old.snapshot().modeRevision == revision);
                c.check("failed-worker-exposes-no-source-or-cached-mode", !h.observer.observedSource(h.session) &&
                    h.observer.snapshot(h.session).value("mode", std::string{}) == "unknown" &&
                    h.observer.passiveSnapshot(h.session).is_null());
                std::atomic<unsigned> unexpectedRead{0};
                steamui::ModeObserverConfig extraConfig;
                extraConfig.read = [&unexpectedRead](const Session& session) { ++unexpectedRead; return sample(session); };
                bool initialized = false, initializeThrew = false;
                try { initialized = h.observer.initialize(std::move(extraConfig)); }
                catch (...) { initializeThrew = true; }
                c.check("initialize-refuses-joinable-failed-worker", !initializeThrew && !initialized && unexpectedRead == 0);
                h.observer.shutdown();
                c.check("failed-worker-shutdown-joins-without-extra-notification", h.sourceCount() == 1 &&
                    h.invalidationCount() == 1 && h.observer.sourceEventDiagnostics().workerWaitFailed == 1 &&
                    !old.current() && WaitForSingleObject(event, 0) == WAIT_OBJECT_0 &&
                    !ymcc_mode_source_wait_port::failNext.load());
            });
        });
    }
#endif
    Json ids = Json::array();
    for (const auto& row : report["cases"]) ids.push_back(row["name"]);
    report["caseIds"] = std::move(ids);
    report["passed"] = report["failedCases"].empty();
    const int exitCode = report["passed"].get<bool>() ? 0 : 1;
    report["exitCode"] = exitCode;
    std::cout << report.dump() << '\n';
    return exitCode;
}
#else // YMCC_MODE_SOURCE_DIRECTED_ORIGINAL_BODY_BEGIN
// Historical in-process callback regression; not the owned binding production acceptance.
#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1
// Production shared observer + Decky runtime; inert data, rejected preflight.

// No real Steam, plugin, configuration or device is touched.

#define WIN32_LEAN_AND_MEAN

#define NOMINMAX

#include "steam_ui_mode_observer.h"

#include "decky_sidebar_runtime.h"

#include <atomic>

#include <iostream>

#include <stdexcept>

using namespace ymcc;

static unsigned passed=0;

static void expect(bool ok,const char* label){if(!ok)throw std::runtime_error(label);++passed;std::cout<<"PASS "<<label<<'\n';}

template<class F> bool until(F f,DWORD ms=1500){const auto end=GetTickCount64()+ms;while(!f()){if(GetTickCount64()>=end)return false;Sleep(2);}return true;}

static steamui::ModeJson sample(const steamsession::Session& s,const char* mode="desktop",const char* epoch="renderer:1"){

    return {{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},{"mode",mode},{"modeEpoch",epoch}};

}

int main(){try{

    steamui::ModeReadWindow window;const auto base=ULONGLONG{10000};

    expect(window.timeout(base)==INFINITE,"Idle observer has no timer");expect(window.request(base),"First hint owns one wake");

    expect(window.timeout(base)==600,"Window hints coalesce for 600ms");

    unsigned redundantWakes=0;for(unsigned i=0;i<10000;++i)redundantWakes+=window.request(base+500);

    expect(redundantWakes==0,"10000 pending hints do not wake the worker again");

    expect(window.timeout(base+500)==100,"Window storms cannot postpone the first read");

    expect(!window.take(base+599)&&window.take(base+600),"No early read");window.finish(base+600,true);

    expect(window.timeout(base+601)==INFINITE,"Known result returns to event wait");

    window.request(base+601);expect(window.timeout(base+601)==4999,"All consumers share the 5s minimum gap");

    expect(window.take(base+5600),"Only the next eligible read starts");window.finish(base+5600,false);

    expect(window.timeout(base+5600)==5000,"Unknown result has finite 5s retry");

    expect(window.take(base+10600),"Second unknown check");window.finish(base+10600,false);

    expect(window.take(base+15600),"Third unknown check");window.finish(base+15600,false);

    expect(window.timeout(base+15600)==INFINITE,"Three failures park without polling");

    window.request(base+15601);expect(window.timeout(base+15601)==29999,"New hints cannot bypass 30s failure cooldown");

    window.cancel();expect(window.timeout(base+16000)==INFINITE,"Withdrawn demand cancels deadlines");

    window.request(base+16000);expect(window.timeout(base+16000)==29600,"Disable/re-enable preserves cooldown");

    window.reset();window.request(base);expect(window.take(base+900000),"Late IO admits one read, not a catch-up burst");

    window.finish(base+900000,true);expect(window.timeout(base+900001)==INFINITE,"Long stall does not introduce recurring polling");



    steamsession::Session session{L"X:\\InertSteam",123,0,100};

    steamui::ModeObserver observer;std::atomic<unsigned> reads{0},notifications{0};std::atomic<bool> big{false},reentry{false};

    steamui::ModeObserverConfig config;config.debounceMs=25;config.minimumGapMs=80;config.failureGapMs=180;

    config.read=[&](const auto& s){++reads;return sample(s,big?"bigpicture":"desktop",reentry?"renderer:2":"renderer:1");};

    config.changed=[&](const auto& s,const auto&){++notifications;observer.snapshot(s);};

    expect(observer.initialize(config),"Shared worker initializes");observer.observeSteam(session);Sleep(60);

    expect(reads==0,"No enabled consumers means no reads");

    expect(observer.passiveSnapshot(session).is_null(),"No observer demand leaves legacy UI observation available");

    observer.setDemand(steamui::ModeConsumer::Decky,true);observer.setDemand(steamui::ModeConsumer::Css,true);

    expect(until([&]{return observer.snapshot(session).value("mode",std::string{})=="desktop";}),"Both consumers receive a verified initial mode");

    expect(reads==1&&notifications==1,"CSS and Decky demand starts one shared read");

    bool passiveCorrect=true;for(unsigned i=0;i<10000;++i)passiveCorrect&=observer.passiveSnapshot(session).value("mode",std::string{})=="desktop";

    expect(passiveCorrect,"Automatic page consumes shared cached mode");

    expect(reads==1,"10000 passive frontend reads add no CDP reads");

    auto mismatched=session;++mismatched.created;

    expect(observer.passiveSnapshot(mismatched).is_null(),"Old-session cache never replaces current frontend identity");

    for(unsigned i=0;i<10000;++i){observer.snapshot(session);}

    Sleep(120);expect(reads==1,"Snapshot consumers and idle time create no CDP calls");

    auto reused=session;reused.created++;

    expect(observer.snapshot(reused).value("mode",std::string{})=="unknown","PID reuse cannot consume the old birth snapshot");

    auto otherRoot=session;otherRoot.root=L"Y:\\OtherSteam";

    expect(!observer.requestRefresh(otherRoot),"Another root cannot request a refresh for this session");

    big=true;for(unsigned i=0;i<10000;++i)observer.requestRefresh(session);

    expect(until([&]{return observer.snapshot(session).value("mode",std::string{})=="bigpicture";}),"A genuine bigpicture confirmation is delivered");

    Sleep(100);expect(reads==2&&notifications==2,"An event storm starts one read and one mode notification");

    observer.requestRefresh(session);expect(until([&]{return reads==3;}),"Explicit recovery can refresh the shared snapshot");Sleep(30);

    expect(notifications==2,"Unchanged mode/epoch does not wake consumers again");

    reentry=true;observer.requestRefresh(session);expect(until([&]{return reads==4&&notifications==3;}),"Valve modeEpoch re-entry wakes both consumers even when current mode remains bigpicture");

    observer.setDemand(steamui::ModeConsumer::Decky,false);observer.requestRefresh(session);

    expect(until([&]{return reads==5;}),"Disabling Decky retains CSS demand");Sleep(30);

    observer.setDemand(steamui::ModeConsumer::Css,false);const auto disabled=reads.load();

    expect(!observer.requestRefresh(session),"No consumers rejects new hints");

    expect(observer.passiveSnapshot(session).is_null(),"Disabling all consumers restores legacy read admission");Sleep(220);expect(reads==disabled,"Disabled shared worker stays idle");observer.shutdown();



    // Real Decky admission worker wakes on shared completion, ahead of its 1s/30s timers.

    steamui::ModeObserver signals;deckysidebar::Runtime decky;std::atomic<unsigned> preflight{0},signalReads{0};

    deckysidebar::Config deckyConfig;deckyConfig.executable=L"NeverLaunch.exe";

    deckyConfig.preflight=[&]{++preflight;return std::string("inert-preflight-stop");};

    deckyConfig.launchAdmission=[&]{const auto state=signals.snapshot(session);const auto mode=state.value("mode",std::string{});if(mode=="desktop")return deckysidebar::LaunchAdmission{false,"steam-bigpicture-required",false};return deckysidebar::LaunchAdmission{mode=="bigpicture","steam-ui-state-pending",true};};

    decky.initialize(deckyConfig);decky.observeSteam(1,true);decky.setEnabled(true);

    expect(until([&]{return decky.snapshot().phase=="waiting-manual-steam";}),"Decky waits for the shared true mode");

    std::atomic<bool> signalBig{false};
    config.read=[&](const auto& s){++signalReads;return sample(s,signalBig?"bigpicture":"desktop");};

    config.changed=[&](const auto&,const auto&){decky.requestLaunchAdmissionRefresh();};

    signals.initialize(config);signals.setDemand(steamui::ModeConsumer::Decky,true);signals.setDemand(steamui::ModeConsumer::Css,true);signals.observeSteam(session);
    expect(until([&]{return decky.snapshot().reason=="steam-bigpicture-required";}),"Shared confirmed desktop parks the existing Runtime admission");
    const auto parkedReads=signalReads.load();Sleep(120);
    expect(!decky.snapshot().admissionRetry&&preflight==0&&signalReads==parkedReads,"Confirmed desktop waits for events without admission CDP/preflight work");
    const auto started=GetTickCount64();signalBig=true;signals.requestRefresh(session);

    expect(until([&]{return preflight==1;}),"Shared bigpicture signal immediately resumes real Decky admission");

    expect(GetTickCount64()-started<800,"Confirmed mode does not wait for 1s or 30s recovery timer");

    expect(signalReads==2,"Initial desktop and later Big Picture each use one shared read; Decky wake adds no reader");

    expect(until([&]{return decky.snapshot().phase=="unavailable";}),"The original preflight still guards all launch paths");

    expect(decky.snapshot().pid==0,"No loader or real plugin was launched by the fixture");decky.shutdown();signals.shutdown();



    // A session change during blocked IO must reject the old delivery.

    steamui::ModeObserver stale;std::atomic<bool> entered{false},release{false};std::atomic<unsigned> oldNotifies{0};

    config.read=[&](const auto& s){if(s.created==session.created){entered=true;while(!release)Sleep(2);}return sample(s,"bigpicture");};

    config.changed=[&](const auto& s,const auto&){if(s.created==session.created)++oldNotifies;};

    stale.initialize(config);stale.setDemand(steamui::ModeConsumer::Decky,true);stale.observeSteam(session);

    expect(until([&]{return entered.load();}),"Old session fixture is in flight");stale.observeSteam(reused);release=true;

    expect(until([&]{return stale.snapshot(reused).value("mode",std::string{})=="bigpicture";}),"New birth gets its own verified mode");

    expect(oldNotifies==0&&stale.snapshot(session).value("mode",std::string{})=="unknown","Old IO cannot notify or populate a new session");stale.shutdown();



    // An event in flight invalidates that read and schedules just one following read.

    steamui::ModeObserver inFlight;entered=false;release=false;std::atomic<unsigned> flightReads{0},flightNotifies{0};

    config.read=[&](const auto& s){if(++flightReads==1){entered=true;while(!release)Sleep(2);}return sample(s,"bigpicture");};

    config.changed=[&](const auto&,const auto&){++flightNotifies;};

    inFlight.initialize(config);inFlight.setDemand(steamui::ModeConsumer::Css,true);inFlight.observeSteam(session);

    expect(until([&]{return entered.load();}),"Mode event fixture is in flight");

    expect(inFlight.passiveSnapshot(session).value("reason",std::string{})=="shared-mode-pending", "Pending shared read is returned without a competing frontend read");

    for(unsigned i=0;i<1000;++i)inFlight.requestRefresh(session);release=true;

    expect(until([&]{return flightNotifies==1;}),"Event arriving during IO is checked by a following read");Sleep(120);

    expect(flightReads==2&&flightNotifies==1,"In-flight event storm produces exactly one fresh delivery");inFlight.shutdown();



#include "fixtures/steam_mode_read/malformed.inc"
    // Virtual-clock policy tests: event storms/account updates cannot refill
    // startup credit, and absent APIs keep the original slow three-try budget.
    {
        steamui::ModeReadWindow cold(10,5000,30000);ULONGLONG now=10000;
        cold.beginStartup(now);
        expect(cold.timeout(now)==INFINITE,"Opening startup credit alone creates no timer");
        for(unsigned i=0;i<6;++i){
            cold.request(now);now+=cold.timeout(now);
            expect(cold.take(now),"A requested startup read spends one finite credit");cold.finish(now,true);
            expect(cold.timeout(now)==INFINITE,"A known startup result returns to event wait");
            if(i<5){cold.request(now);expect(cold.timeout(now)==500,"Cold startup keeps a 500ms minimum gap");}
        }
        cold.reset(true);cold.request(now);
        expect(cold.timeout(now)==5000,"Account-only observation preserves exhausted credit and its 5s cooldown");
        cold.reset();cold.beginStartup(30000);cold.request(30000);cold.take(30010);cold.finish(30010,false);
        expect(cold.timeout(30010)==5000,"Unknown startup IO does not spend slow-PC retries faster");
        cold.take(35010);cold.finish(35010,false);cold.take(40010);cold.finish(40010,false);
        expect(cold.timeout(40010)==INFINITE,"Unknown startup IO still parks after exactly three attempts");
        cold.request(40011);expect(cold.timeout(40011)>=29999,"New hints cannot bypass failure cooldown");
        cold.reset();cold.beginStartup(10000);cold.request(20001);cold.take(20011);cold.finish(20011,true);cold.request(20012);
        expect(cold.timeout(20012)==4999,"Expired startup allowance restores the ordinary 5s gap");
    }

    {
        steamui::ModeReadWindow debt;debt.beginStartup(10000);debt.request(10000);debt.take(10600);debt.finish(10600,false);
        debt.cancel();debt.request(10601);
        expect(debt.timeout(10601)>=4999,"Unknown slow retry debt survives demand cancellation and reopen");
        debt.reset();debt.beginStartup(10000);debt.request(10000);debt.take(10600);debt.finish(10600,false);
        debt.reset(true);debt.request(10601);
        expect(debt.timeout(10601)>=4999,"Unknown slow retry debt survives same-process account metadata update");
    }

    {
        steamui::ModeReadWindow budget(10,5000,30000);ULONGLONG now=10000;
        budget.beginStartup(now);
        for(unsigned i=0;i<3;++i){
            budget.request(now,true);now+=budget.timeout(now);expect(budget.take(now),"Preserved retry budget admits its scheduled read");budget.finish(now,false);
            if(i<2){if(i==0)budget.cancel(true);else budget.reset(true);budget.request(now+1,true);expect(budget.timeout(now+1)>=4999,"Pause/account update cannot shorten unknown retry spacing");}
        }
        expect(budget.timeout(now)==INFINITE,"Unknown retries still park after three reads across demand/account changes");
        budget.reset(true);budget.request(now+1,true);expect(budget.timeout(now+1)>=29999,"Account update cannot discard exhausted failure cooldown");
    }

    // Cold start: a real confirmed mode invalidated by a startup window hint
    // must get a finite fresh read without waiting for the healthy 5s cadence.
    {
        steamui::ModeObserver startup;std::atomic<unsigned> startupReads{0},startupNotifies{0};
        std::atomic<bool> firstInFlight{false},completeFirst{false};
        steamui::ModeObserverConfig cold;cold.debounceMs=15;
        cold.read=[&](const auto& s){if(++startupReads==1){firstInFlight=true;while(!completeFirst)Sleep(1);}return sample(s,"bigpicture");};
        cold.changed=[&](const auto&,const auto&){++startupNotifies;};
        startup.initialize(cold);startup.setDemand(steamui::ModeConsumer::Decky,true);startup.observeSteam(session);
        const bool coldEntered=until([&]{return firstInFlight.load();});
        for(unsigned i=0;i<10000;++i)startup.requestRefresh(session);
        const auto freshAt=GetTickCount64();completeFirst=true;
        const bool coldReady=until([&]{return startupNotifies==1;},1400);
        const auto coldElapsed=GetTickCount64()-freshAt;
        startup.shutdown();
        expect(coldEntered&&coldReady&&coldElapsed<1400,"Cold startup hint rechecks a verified result before the healthy 5s interval");
        expect(startupReads==2&&startupNotifies==1,"10000 startup hints share two bounded reads and one delivery");
    }

    // A local try-lock miss performs no Steam IO and must not impose the
    // genuine read-failure delay during the finite initial catch-up window.
    {
        steamui::ModeObserver contention;std::atomic<unsigned> reads{0},notifications{0};
        steamui::ModeObserverConfig config;config.debounceMs=15;
        config.read=[&](const auto& s){auto value=sample(s,"bigpicture");if(++reads==1)value={{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},{"mode","unknown"},{"reason","live-busy"},{"localReadBusy",true}};return value;};
        config.changed=[&](const auto&,const auto&){++notifications;};
        const auto at=GetTickCount64();contention.initialize(config);contention.setDemand(steamui::ModeConsumer::Decky,true);contention.observeSteam(session);
        const bool ready=until([&]{return notifications==1;},1400);contention.shutdown();
        expect(ready&&GetTickCount64()-at<1400&&reads==2,"Cold local live-busy contention retries within startup credit without accepting unknown mode");
    }
    {
        steamui::ModeObserver contention;std::atomic<unsigned> reads{0},notifications{0};
        steamui::ModeObserverConfig config;config.debounceMs=15;
        config.read=[&](const auto& s){++reads;return steamui::ModeJson{{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},{"mode","unknown"},{"reason","live-busy"},{"localReadBusy",true}};};
        config.changed=[&](const auto&,const auto&){++notifications;};
        contention.initialize(config);contention.setDemand(steamui::ModeConsumer::Decky,true);contention.observeSteam(session);
        const bool six=until([&]{return reads==6;},3300);Sleep(550);contention.shutdown();
        expect(six&&reads==6&&notifications==0,"Persistent startup live-busy exhausts finite short slots then returns to slow retry without a mode notification");
    }

    {
        steamui::ModeReadWindow local(10,5000,30000);ULONGLONG now=10000;
        local.beginStartup(now);local.request(now);
        for(unsigned i=0;i<6;++i){now+=local.timeout(now);expect(local.take(now),"Busy catch-up consumes only a finite initial read slot");local.finish(now,false,true,true);}
        expect(local.timeout(now)==5000,"Exhausted busy catch-up resumes five-second spacing");
        now+=local.timeout(now);local.take(now);local.finish(now,false,true,true);
        now+=local.timeout(now);local.take(now);local.finish(now,false,true,true);
        expect(local.timeout(now)==INFINITE,"Persistent busy still parks after three ordinary slow attempts");
        local.request(now+1,true);expect(local.timeout(now+1)>=29999,"Persistent busy cannot bypass the ordinary failure cooldown");
    }

    {
        steamui::ModeObserver contention;std::atomic<unsigned> reads{0},notifications{0};
        steamui::ModeObserverConfig config;config.debounceMs=15;
        config.read=[&](const auto& s){auto value=sample(s,"bigpicture");if(++reads<=3)value={{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},{"mode","unknown"},{"reason","live-busy"},{"localReadBusy",true}};return value;};
        config.changed=[&](const auto&,const auto&){++notifications;};
        const auto at=GetTickCount64();contention.initialize(config);contention.setDemand(steamui::ModeConsumer::Decky,true);contention.observeSteam(session);
        const bool ready=until([&]{return notifications==1;},2400);contention.shutdown();
        expect(ready&&GetTickCount64()-at<2400&&reads==4,"Three initial local lock misses recover without entering thirty-second failure cooldown");
    }


    // Health checks request freshness without invalidating an already running
    // verified read. A later real window hint must still invalidate that read,
    // even when health already owns the one queued follow-up.
    for (unsigned order = 0; order < 3; ++order) {
        steamui::ModeObserver overlap;
        std::atomic<unsigned> overlapReads{0}, overlapNotifies{0}, overlapStable{0};
        std::atomic<bool> firstEntered{false}, secondEntered{false}, finishFirst{false}, finishSecond{false};
        steamui::ModeObserverConfig health;
        health.debounceMs=15;health.minimumGapMs=80;health.failureGapMs=180;
        health.read=[&](const auto& s){
            const auto n=++overlapReads;
            if(n==1){firstEntered=true;while(!finishFirst)Sleep(1);}
            if(n==2){secondEntered=true;while(!finishSecond)Sleep(1);}
            return sample(s,order==0||n==1?"bigpicture":"desktop");
        };
        health.changed=[&](const auto&,const auto&){++overlapNotifies;};
        health.stableBigPicture=[&](const auto&){++overlapStable;};
        const bool initialized=overlap.initialize(health);
        overlap.setDemand(steamui::ModeConsumer::Decky,true);
        overlap.setDemand(steamui::ModeConsumer::Css,true);
        overlap.observeSteam(session);
        const bool firstStarted=until([&]{return firstEntered.load();});
        bool requestsAccepted=true;
        if(order!=2)for(unsigned i=0;i<1000;++i)requestsAccepted&=overlap.requestRefresh(session,false);
        if(order!=0)for(unsigned i=0;i<1000;++i)requestsAccepted&=overlap.requestRefresh(session);
        if(order==2)for(unsigned i=0;i<1000;++i)requestsAccepted&=overlap.requestRefresh(session,false);
        finishFirst=true;
        const bool secondStarted=until([&]{return secondEntered.load();});
        const auto beforeFollowup=overlap.snapshot(session);
        const auto earlyNotifies=overlapNotifies.load();
        finishSecond=true;
        const bool ready=until([&]{
            return overlapNotifies==1&&overlap.snapshot(session).value("mode",std::string{})==(order==0?"bigpicture":"desktop")&&
                (order!=0||overlapStable==1);
        });
        Sleep(120);
        overlap.shutdown(); // Release every gate and worker before any throwing assertion.
        std::cout<<"HEALTH_OVERLAP order="<<order<<" earlyMode="<<beforeFollowup.value("mode",std::string{})
            <<" earlyNotifies="<<earlyNotifies<<" reads="<<overlapReads<<" finalNotifies="<<overlapNotifies
            <<" stable="<<overlapStable<<" workerStopped=1\n";
        expect(initialized&&firstStarted&&secondStarted&&requestsAccepted,"Health overlap reaches both controlled reads and releases its worker");
        if(order==0)
            expect(earlyNotifies==1&&beforeFollowup.value("mode",std::string{})=="bigpicture","Health refresh overlapping IO preserves verified delivery before the queued follow-up");
        else
            expect(earlyNotifies==0&&beforeFollowup.value("mode",std::string{})=="unknown","Actual window hints invalidate old IO regardless of health request ordering");
        expect(ready&&overlapReads==2&&overlapNotifies==1,"Health and window storms share one fresh follow-up without a read or notification storm");
        expect(overlapStable==(order==0?1u:0u),"Only a verified unchanged Big Picture follow-up signals stable admission");
    }

    std::cout<<"SHARED_STEAM_MODE_TEST_OK passed="<<passed<<" realSteamOperations=0\n";return 0;

}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

#endif // YMCC_MODE_SOURCE_DIRECTED_ORIGINAL_BODY_END

#endif // YMCC_MODE_MALFORMED_ONLY
