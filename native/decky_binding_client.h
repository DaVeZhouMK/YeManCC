#pragma once
// Event-driven parent consumer. It owns only its same-image binding Job; Steam
// and CEF handles are query/wait leases and are never termination targets.
#include "decky_binding_worker.h"
#include "decky_binding_process_owner.h"
#include "decky_listener_proxy.h"
#include <array>
#include <set>
#include <new>

namespace ymcc::deckybinding {
struct Packet {
    enum Kind { pending, message, ended, failed };
    Kind kind = pending;
    nlohmann::json value;
    std::string reason;
};
// Extends the existing Owner snapshot. Inherited counters are TOTALS; owner and
// reader counters also remain separate so neither can conceal incomplete cleanup.
struct ClientSnapshot : Snapshot {
    Snapshot owner;
    unsigned readerHandles = 0, readerPendingIo = 0, readerActiveCalls = 0;
    unsigned clientHandles = 0, sessions = 0;
    std::uint64_t epoch = 0;
    bool enabled = false, canceled = false, readerFullyDestroyed = true;
};
namespace clientdetail {
using Json = nlohmann::json;
class EventReader {
    struct State {
        std::mutex mutex;
        detail::Handle pipe, ready;
        OVERLAPPED overlapped{};
        std::vector<std::uint8_t> buffer;
        std::atomic<bool> stopping{false};
        std::size_t offset = 0, target = sizeof(std::uint32_t);
        bool pending = false;
        DWORD error = ERROR_SUCCESS, cleanupError = ERROR_SUCCESS;
        State* retainedNext = nullptr;
    };
    std::unique_ptr<State> state_{std::make_unique<State>()};
    inline static std::mutex retainedMutex_;
    inline static State* retained_ = nullptr;
    inline static State* retainedTail_ = nullptr;
    inline static std::atomic<bool> retainedPending_{false};
    static bool completion(State& s, DWORD& bytes) noexcept {
        if (!s.pending) return true;
        if (GetOverlappedResult(s.pipe.value, &s.overlapped, &bytes, FALSE)) {
            s.pending = false; return true;
        }
        const DWORD error = GetLastError();
        if (error == ERROR_IO_INCOMPLETE) return false;
        s.pending = false; s.error = error; // Real completion, including ABORTED.
        return true;
    }
    static void cancelLocked(State& s) noexcept {
        if (s.pending && !CancelIoEx(s.pipe.value, &s.overlapped)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NOT_FOUND) s.cleanupError = error;
        }
    }
    // Completion is always proved before closing storage shared with the OS.
    static bool closeState(State& s) noexcept {
        bool okay=s.pipe.close(s.cleanupError);
        if(!s.ready.close(s.cleanupError))okay=false;
        return okay;
    }
    static void retain(std::unique_ptr<State>& owner) noexcept {
        std::lock_guard lock(retainedMutex_);
        auto* retired=owner.release();retired->retainedNext=nullptr;
        if(retainedTail_)retainedTail_->retainedNext=retired;else retained_=retired;
        retainedTail_=retired;retainedPending_.store(true,std::memory_order_release);
    }
    // One retired state per existing attach. No timer, healthy watch polling,
    // blocking mutex or IO wait; a refused close stays owned and rotates FIFO.
    static void reapRetained() noexcept {
        if(!retainedPending_.load(std::memory_order_acquire))return;
        struct RestoreError{DWORD value=GetLastError();~RestoreError(){SetLastError(value);}} restore;
        try{
            std::unique_lock list(retainedMutex_,std::try_to_lock);if(!list.owns_lock())return;
            auto* retired=retained_;if(!retired){retainedPending_.store(false,std::memory_order_release);return;}
            bool complete=false;
            {std::unique_lock state(retired->mutex,std::try_to_lock);DWORD bytes=0;
                if(state.owns_lock()&&completion(*retired,bytes))complete=closeState(*retired);}
            // Throwing lock acquisition above cannot orphan a queued state.
            retained_=retired->retainedNext;if(!retained_)retainedTail_=nullptr;retired->retainedNext=nullptr;
            if(complete)delete retired;
            else{if(retainedTail_)retainedTail_->retainedNext=retired;else retained_=retired;retainedTail_=retired;}
            retainedPending_.store(retained_!=nullptr,std::memory_order_release);
        }catch(...){} // Keep the original retirement and the current attach.
    }
    static bool arm(State& s) noexcept {
        if (s.stopping.load(std::memory_order_acquire) || s.error) return false;
        s.overlapped = {}; s.overlapped.hEvent = s.ready.value;
        if (!ResetEvent(s.ready.value)) { s.error = GetLastError(); return false; }
        DWORD bytes = 0;
        const BOOL immediate = ReadFile(s.pipe.value, s.buffer.data() + s.offset,
            static_cast<DWORD>(s.target - s.offset), &bytes, &s.overlapped);
        const DWORD error = immediate ? ERROR_SUCCESS : GetLastError();
        if (!immediate && error != ERROR_IO_PENDING) {
            s.error = error; SetEvent(s.ready.value); return false;
        }
        // Even immediate IO is harvested using GetOverlappedResult before reuse.
        s.pending = true;
        if (s.stopping.load(std::memory_order_acquire)) cancelLocked(s);
        return true;
    }
public:
    struct Status { unsigned handles = 0, pendingIo = 0; DWORD error = 0; bool fullyDestroyed = true; };
    enum class Read { pending, frame, ended, failed };
    EventReader() = default;
    EventReader(const EventReader&) = delete;
    ~EventReader() noexcept {
        if (!stop(0)) {
            // No destructor runs on buffers/OVERLAPPED still referenced by the OS.
            retain(state_);
        }
    }
    // Takes ownership, also on failure. No reading through Owner::readFrame.
    bool attach(HANDLE pipe, DWORD* error = nullptr) {
        if (error) *error = ERROR_SUCCESS;
        reapRetained();
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& s = *state_;
        s.pipe.value = pipe;
        if (!pipe || s.stopping.load(std::memory_order_acquire)) {
            if (error) *error = !pipe ? ERROR_INVALID_HANDLE : ERROR_OPERATION_ABORTED;
            return false;
        }
        s.buffer.resize(maxFrameBytes + sizeof(std::uint32_t));
        s.ready.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!s.ready.value) { s.error = GetLastError(); if (error) *error = s.error; return false; }
        const bool armed = arm(s);
        if (!armed && error) *error = s.error ? s.error : ERROR_OPERATION_ABORTED;
        return armed;
    }
    HANDLE event() const noexcept { // Session wait mutex protects close/duplicate.
        return state_->ready.value;
    }
    Read advance(std::vector<std::uint8_t>& payload, DWORD& error) {
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& s = *state_;
        if (s.stopping.load(std::memory_order_acquire)) { error = ERROR_OPERATION_ABORTED; return Read::ended; }
        if (!s.pipe.value) { error = ERROR_INVALID_STATE; return Read::failed; }
        for (;;) { // At most one bounded frame; never waits or scans when idle.
            if (s.error) {
                error = s.error;
                if (detail::ioCode(error) == Code::brokenPipe) {
                    if (!s.offset && s.target == sizeof(std::uint32_t)) return Read::ended;
                    error = ERROR_HANDLE_EOF;
                }
                return Read::failed;
            }
            DWORD bytes = 0;
            if (!completion(s, bytes)) return Read::pending;
            if (s.error) continue;
            if (!bytes) { s.error = ERROR_BROKEN_PIPE; SetEvent(s.ready.value); continue; }
            if (bytes > s.target - s.offset) { s.error = ERROR_BAD_LENGTH; continue; }
            s.offset += bytes;
            if (s.offset == sizeof(std::uint32_t) && s.target == sizeof(std::uint32_t)) {
                std::uint32_t length = 0;
                std::memcpy(&length, s.buffer.data(), sizeof(length));
                if (!length || length > maxFrameBytes) { s.error = ERROR_BAD_LENGTH; SetEvent(s.ready.value); continue; }
                s.target += length;
            }
            if (s.offset == s.target) {
                payload.assign(s.buffer.begin() + sizeof(std::uint32_t), s.buffer.begin() + s.target);
                s.offset = 0; s.target = sizeof(std::uint32_t);
                arm(s); // Keep the successful watch draining on subsequent events.
                return Read::frame;
            }
            if (!arm(s)) continue;
        }
    }
    void requestCancel() noexcept {
        state_->stopping.store(true, std::memory_order_release);
        std::unique_lock<std::mutex> lock(state_->mutex, std::try_to_lock);
        if (lock.owns_lock()) cancelLocked(*state_);
    }
    bool stop(DWORD waitMs) noexcept {
        requestCancel();
        auto& s = *state_;
        std::unique_lock<std::mutex> lock(s.mutex);
        cancelLocked(s);
        DWORD bytes = 0;
        if (!completion(s, bytes)) {
            const HANDLE ready = s.ready.value;
            lock.unlock();
            const DWORD waited = WaitForSingleObject(ready, waitMs);
            lock.lock();
            if (waited == WAIT_FAILED) s.cleanupError = GetLastError();
            if (!completion(s, bytes)) return false; // Preserve ALL IO storage and leases.
        }
        return closeState(s);
    }
    Status snapshot() const noexcept {
        std::lock_guard<std::mutex> lock(state_->mutex);
        const auto& s = *state_;
        Status out; out.handles = unsigned(s.pipe.value != nullptr) + unsigned(s.ready.value != nullptr);
        out.pendingIo = s.pending ? 1u : 0u; out.error = s.cleanupError;
        out.fullyDestroyed = !out.handles && !out.pendingIo; return out;
    }
};
struct Session {
    // Declared first so the lease outlives Owner destruction and final stop.
    std::shared_ptr<ymcc::steamsession::IdentityDelegation> delegation;
    Owner owner;
    EventReader reader;
    // waitMutex protects publication, observation duplication and final close.
    std::mutex waitMutex, nextMutex, cleanupMutex;
    detail::Handle child, callsDone{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> stopping{false}, destroyed{false};
    unsigned calls = 1; // Published with begin already admitted (manager lock).
    std::uint64_t epoch = 0;
    worker::Request request;
    std::unique_ptr<listenerproxy::Server> listenerServer;
    std::uint64_t nativeGeneration=0;
    std::atomic<std::shared_ptr<listenerproxy::Work>> listenerWork;
    std::string listenerFailure;
    bool listenerFaultReported=false;
    std::atomic<bool> readyForPackets{false};
    bool identified = false, resultSeen = false, succeeded = false, contextTaken = false;
    Json context;
    std::string terminal;
    bool terminalFailed = false;
};
inline Result status(Code code, DWORD error = ERROR_SUCCESS) noexcept {
    Result r; r.code = code; r.error = error; return r;
}
inline bool matchingPath(const std::wstring& root, const std::wstring& path) {
    const auto prefix = root + L"\\";
    return path.starts_with(prefix) && std::filesystem::path(path).filename() == L"steamwebhelper.exe";
}
} // namespace clientdetail

class Client {
public:
    using Snapshot = ClientSnapshot;
    using ListenerReader=std::function<std::vector<steamlive::Endpoint>(const worker::Request&,ULONGLONG,const std::function<bool()>&)>;
private:
    using Session = clientdetail::Session;
    ListenerReader listenerReader_;
    listenerproxy::Post listenerPost_;
    listenerproxy::Wake listenerWake_;
    std::function<listenerproxy::Wake()> listenerWakeFactory_;
    mutable std::mutex mutex_;
    std::shared_ptr<Session> current_, previous_;
    std::uint64_t epoch_ = 0, highestGeneration_ = 0;
    bool everEnabled_ = false, enabled_ = false, canceled_ = false, beginning_ = false, stopping_ = false;
    std::uint64_t identityEpoch_ = 0;
    std::string lastNonce_;
    struct Call {
        Client& client;
        std::shared_ptr<Session> session;
        bool begin = false;
        ~Call() {
            std::lock_guard<std::mutex> lock(client.mutex_);
            if (begin) client.beginning_ = false;
            if (--session->calls == 0) {
                std::lock_guard<std::mutex> waits(session->waitMutex);
                if (session->callsDone.value) SetEvent(session->callsDone.value);
            }
        }
    };
    std::shared_ptr<Session> admit() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_ || canceled_ || !current_ || current_->epoch != epoch_ || current_->stopping.load(std::memory_order_acquire) || !current_->readyForPackets.load(std::memory_order_acquire)) return {};
        auto session = current_;
        std::lock_guard<std::mutex> waits(session->waitMutex);
        if (!session->callsDone.value) return {};
        if (session->calls++ == 0) ResetEvent(session->callsDone.value);
        return session;
    }
    static void requestCancel(const std::shared_ptr<Session>& s) noexcept {
        if (!s) return;
        s->stopping.store(true, std::memory_order_release);
        if(const auto work=s->listenerWork.load(std::memory_order_acquire))work->cancel();
        s->owner.requestCancel(); s->reader.requestCancel();
    }
    Result finishResult(const std::shared_ptr<Session>& s, Code code, DWORD error = ERROR_SUCCESS) const {
        auto result = clientdetail::status(code, error);
        result.generation = s->identified ? s->request.generation : 0;
        // begin itself is still an active call; fullyDestroyed is proved by stop.
        result.ioPending = s->reader.snapshot().pendingIo != 0 || s->owner.snapshot().pendingIo != 0;
        return result;
    }
    static Result stopSession(const std::shared_ptr<Session>& s, detail::Deadline& deadline) noexcept {
        if (!s) { auto r = clientdetail::status(Code::ok); r.fullyDestroyed = true; return r; }
        requestCancel(s);
        std::unique_lock<std::mutex> cleanup(s->cleanupMutex, std::try_to_lock);
        if (!cleanup.owns_lock()) return clientdetail::status(Code::cleanupIncomplete, ERROR_BUSY);
        // Let the existing inherited cancel event unwind WinHTTP and deliver its
        // final HANDLE_CLOSING callbacks before hard-revoking this owned Job.
        // Both phases share the SAME caller deadline; a stalled callback still
        // consumes only that budget and the hard-stop fallback remains mandatory.
        const DWORD stopBudget = deadline.left();
        const DWORD grace = (std::min)(DWORD{150}, stopBudget);
        auto owner = s->owner.stop(grace, stopBudget - grace);
        HANDLE done = nullptr;
        {
            std::lock_guard<std::mutex> waits(s->waitMutex);
            done = s->callsDone.value;
        }
        if (done && WaitForSingleObject(done, deadline.left()) != WAIT_OBJECT_0) {
            auto r = clientdetail::status(Code::cleanupIncomplete, ERROR_TIMEOUT);
            r.ioPending = s->reader.snapshot().pendingIo != 0 || s->owner.snapshot().pendingIo != 0;
            return r;
        }
        std::lock_guard<std::mutex> waits(s->waitMutex);
        const bool reader = s->reader.stop(deadline.left());
        const bool child = s->child.close(owner.error);
        // An initial stop can have raced Owner::start or its bounded write.
        if (!owner.fullyDestroyed) owner = s->owner.stop(0, deadline.left());
        const bool okay = owner.fullyDestroyed && reader && child;
        bool closed = okay;
        if (okay) closed = s->callsDone.close(owner.error);
        s->destroyed.store(okay && closed, std::memory_order_release);
        owner.code = okay && closed ? Code::ok : Code::cleanupIncomplete;
        owner.fullyDestroyed = okay && closed;
        owner.ioPending = s->reader.snapshot().pendingIo != 0 || s->owner.snapshot().pendingIo != 0;
        return owner;
    }
    Packet fail(Session& s, const char* reason) {
        if(const auto work=s.listenerWork.exchange({}))work->cancel();
        s.terminal = reason; s.terminalFailed = true;
        s.owner.requestCancel(); s.reader.requestCancel();
        return {Packet::failed, {}, reason};
    }
public:
    // Keep Client alive until concurrent public method calls have returned.
    explicit Client(ListenerReader reader={},listenerproxy::Post post={},listenerproxy::Wake wake={},
                    std::function<listenerproxy::Wake()> wakeFactory={})
        :listenerReader_(std::move(reader)),listenerPost_(std::move(post)),listenerWake_(std::move(wake)),listenerWakeFactory_(std::move(wakeFactory)){}
    static bool listenerBoundaryFailure(const std::string& reason){
        return reason.rfind("binding-listener-",0)==0||reason.rfind("binding-packet-",0)==0||
            reason=="binding-result-invalid"||reason=="binding-result-out-of-order"||
            reason=="binding-context-identity-invalid"||reason=="binding-frame-length-invalid"||
            reason=="binding-frame-truncated"||reason=="binding-identity-delegation-revoked";
    }
    static bool listenerTransientFailure(const std::string& reason){return reason=="binding-listener-facts-unconfirmed";}
    bool listenerReplyReady()const noexcept{
        try{std::lock_guard lock(mutex_);if(!current_||current_->stopping.load(std::memory_order_acquire))return false;
            const auto work=current_->listenerWork.load(std::memory_order_acquire);return work&&(work->ready()||!listenerproxy::left(work->end()));
        }catch(...){return false;}
    }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    ~Client() noexcept { stop(); } // Owners/readers retain exceptional incomplete cleanup.
    // Lightweight fence only: no Owner, handle allocation, waits or IO cleanup.
    bool enable(std::uint64_t epoch) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (everEnabled_ && epoch < epoch_) return false;
        if (everEnabled_ && epoch == epoch_) return enabled_ && !canceled_;
        if (current_) current_->stopping.store(true, std::memory_order_release);
        if (previous_) previous_->stopping.store(true, std::memory_order_release);
        epoch_ = epoch; everEnabled_ = true; enabled_ = true; canceled_ = false;
        return true;
    }
    Result begin(const nlohmann::json& request, std::uint64_t epoch,
                 std::shared_ptr<ymcc::steamsession::IdentityDelegation> delegation = {}) {
        std::shared_ptr<Session> s, previous;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!enabled_ || canceled_ || epoch != epoch_) return clientdetail::status(Code::stopped, ERROR_OPERATION_ABORTED);
            if (beginning_ || stopping_) return clientdetail::status(Code::busy, ERROR_BUSY);
            if (previous_ && !previous_->destroyed.load(std::memory_order_acquire))
                return clientdetail::status(Code::cleanupIncomplete, ERROR_BUSY);
            try {
                s = std::make_shared<Session>();
            } catch (const std::bad_alloc&) {
                // No new session was published or request written. Preserve the
                // current owner and intent; only this pre-publication failure
                // returns notStarted/memory so Runtime can use its old retry.
                return clientdetail::status(Code::notStarted, ERROR_NOT_ENOUGH_MEMORY);
            }
            s->epoch = epoch; s->delegation = std::move(delegation);
            previous = current_; previous_ = previous; current_ = s; beginning_ = true;
        }
        Call call{*this, s, true};
        bool requestWriteAttempted = false;
        try {
            if (!s->callsDone.value) return finishResult(s, Code::notStarted, ERROR_NOT_ENOUGH_MEMORY);
            const auto bytes = request.dump();
            s->request = worker::parse(bytes); s->identified = true;
            // Bind the in-memory authority to exactly the parsed wire request.
            // An unmarked legacy request never receives a delegated cancel slot.
            if (s->request.delegatedIdentity != static_cast<bool>(s->delegation)) {
                requestCancel(s); return finishResult(s, Code::invalidArgument, ERROR_INVALID_DATA);
            }
            const auto identityCurrent = [&]() noexcept {
                return !s->delegation || (s->delegation->matches(s->request.steam.root,
                    s->request.steam.pid, s->request.steam.created, s->request.steam.account,
                    s->request.windowsSession, s->request.liveUiAccount) && s->delegation->current());
            };
            const HANDLE identityCancel = s->delegation ? s->delegation->cancellationEvent() : nullptr;
            if (s->delegation && !identityCancel) {
                requestCancel(s); return finishResult(s, Code::invalidArgument, ERROR_INVALID_HANDLE);
            }
            if (!identityCurrent()) {
                requestCancel(s); return finishResult(s, Code::cancelled, ERROR_OPERATION_ABORTED);
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!enabled_ || canceled_ || epoch != epoch_ || s->stopping.load(std::memory_order_acquire))
                    return clientdetail::status(Code::stopped, ERROR_OPERATION_ABORTED);
                if (identityEpoch_ != epoch) { identityEpoch_ = epoch; highestGeneration_ = 0; lastNonce_.clear(); }
                if (request.at("generation") != std::to_string(s->request.generation) || s->request.generation <= highestGeneration_ || s->request.id == lastNonce_)
                    return clientdetail::status(Code::invalidArgument, ERROR_INVALID_DATA);
                lastNonce_ = s->request.id; highestGeneration_ = s->request.generation;
            }
            if (previous) {
                detail::Deadline deadline(1500);
                const auto cleaned = stopSession(previous, deadline);
                if (!cleaned.fullyDestroyed) { requestCancel(s); return finishResult(s, Code::cleanupIncomplete, cleaned.error); }
                std::lock_guard<std::mutex> lock(mutex_);
                if (previous_ == previous) previous_.reset();
            }
            if (s->stopping.load(std::memory_order_acquire)) return finishResult(s, Code::stopped, ERROR_OPERATION_ABORTED);
            // Retirement may have waited; never start from the pre-wait check.
            if (!identityCurrent()) {
                requestCancel(s); return finishResult(s, Code::cancelled, ERROR_OPERATION_ABORTED);
            }
            const auto read=listenerReader_;const auto bound=s->request;
            s->listenerServer=std::make_unique<listenerproxy::Server>(bound.id,std::to_string(bound.generation),
                [read,bound](ULONGLONG end,const std::function<bool()>& current){
                    if(!read)throw std::runtime_error("live-listener-proxy-unavailable");
                    return read(bound,end,current);
                });
            auto result = s->owner.start(identityCancel);
            if (!result) return finishResult(s,
                result.code == Code::systemError && result.error == ERROR_NOT_ENOUGH_MEMORY &&
                    result.fullyDestroyed && !result.ioPending ? Code::notStarted : result.code,
                result.error);
            s->nativeGeneration=result.generation;
            {
                std::lock_guard<std::mutex> waits(s->waitMutex);
                if (s->stopping.load(std::memory_order_acquire)) return finishResult(s, Code::stopped, ERROR_OPERATION_ABORTED);
                DWORD handoffError = ERROR_SUCCESS;
                const auto failedHandoff = [&] {
                    requestCancel(s);
                    return finishResult(s, handoffError == ERROR_NOT_ENOUGH_MEMORY ? Code::notStarted : Code::systemError,
                        handoffError ? handoffError : ERROR_INVALID_HANDLE);
                };
                s->child.value = s->owner.copyChildProcess(&handoffError);
                if (!s->child.value) return failedHandoff();
                const auto pipe = s->owner.copyReadPipe(&handoffError);
                if (!pipe) return failedHandoff();
                if (!s->reader.attach(pipe, &handoffError)) return failedHandoff();
            }
            // One finite write, outside the manager lock; no bootstrap/result wait.
            const std::vector<std::uint8_t> payload(bytes.begin(), bytes.end());
            if (!identityCurrent()) {
                requestCancel(s); return finishResult(s, Code::cancelled, ERROR_OPERATION_ABORTED);
            }
            // From entry onward a partial write is possible. A later allocation
            // failure must never authorize replay of an uncertain command.
            requestWriteAttempted = true;
            result = s->owner.writeFrame(payload, 150, result.generation);
            if (!result || s->stopping.load(std::memory_order_acquire) || !identityCurrent()) {
                requestCancel(s);
                return finishResult(s, result ? Code::cancelled : result.code,
                    result ? ERROR_OPERATION_ABORTED : result.error);
            }
            bool identityReady = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!enabled_ || canceled_ || epoch != epoch_ || current_ != s || s->stopping.load(std::memory_order_acquire))
                    return clientdetail::status(Code::cancelled, ERROR_OPERATION_ABORTED);
                identityReady = identityCurrent(); // Pure event/atomic check under the manager lock.
                if (identityReady) s->readyForPackets.store(true, std::memory_order_release);
            }
            // Owner/reader cancellation and snapshots stay outside mutex_.
            if (!identityReady) {
                requestCancel(s); return finishResult(s, Code::cancelled, ERROR_OPERATION_ABORTED);
            }
            return finishResult(s, Code::ok);
        } catch (const std::bad_alloc&) {
            requestCancel(s);
            // The existing caller still requires successful cleanup and current
            // intent/admission before retrying any pre-write preparation failure.
            return finishResult(s, requestWriteAttempted ? Code::systemError : Code::notStarted,
                ERROR_NOT_ENOUGH_MEMORY);
        } catch (...) {
            requestCancel(s); return finishResult(s, Code::invalidArgument, ERROR_INVALID_DATA);
        }
    }
    HANDLE event() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!current_ || current_->stopping.load(std::memory_order_acquire)) return nullptr;
        std::lock_guard<std::mutex> waits(current_->waitMutex);
        return current_->reader.event();
    }
    HANDLE childProcess() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!current_ || current_->stopping.load(std::memory_order_acquire)) return nullptr;
        std::lock_guard<std::mutex> waits(current_->waitMutex);
        return current_->child.value;
    }
    // Runtime owns both duplicates until its wait returns, even across OFF/stop.
    bool duplicateWaitHandles(HANDLE& ready, HANDLE& child) {
        ready = nullptr; child = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_ || canceled_ || !current_ || current_->epoch != epoch_ || current_->stopping.load(std::memory_order_acquire)) return false;
        std::lock_guard<std::mutex> waits(current_->waitMutex);
        if (current_->reader.event() && !DuplicateHandle(GetCurrentProcess(), current_->reader.event(),
            GetCurrentProcess(), &ready, SYNCHRONIZE, FALSE, 0)) return false;
        if (current_->child.value && !DuplicateHandle(GetCurrentProcess(), current_->child.value,
            GetCurrentProcess(), &child, SYNCHRONIZE, FALSE, 0)) {
            if (ready) CloseHandle(ready); ready = nullptr; return false;
        }
        return true;
    }
    Packet next() {
        auto s = admit();
        if (!s) return {Packet::ended, {}, "binding-session-stopped"};
        Call call{*this, s};
        std::lock_guard<std::mutex> next(s->nextMutex);
        if (!s->terminal.empty()) return {s->terminalFailed ? Packet::failed : Packet::ended, {}, s->terminal};
        // A queued success is no authority after the producer revoked its lease.
        // No Client manager lock is held while failing/cancelling this session.
        if (s->delegation && !s->delegation->current())
            return fail(*s,s->listenerWork.load()?"binding-listener-identity-revoked":"binding-identity-delegation-revoked");
        std::vector<std::uint8_t> bytes; DWORD error = ERROR_SUCCESS;
        try {
            if(const auto work=s->listenerWork.load(std::memory_order_acquire)){
                if(!listenerproxy::left(work->end()))return fail(*s,"binding-listener-facts-unconfirmed");
                if(work->ready()){
                    const auto reply=work->take();s->listenerWork.store({});work->cancel();
                    if(s->stopping.load(std::memory_order_acquire)||(s->delegation&&!s->delegation->current()))
                        return fail(*s,"binding-listener-identity-revoked");
                    s->listenerFailure=reply.at("detail").at("ok").get<bool>()?std::string{}:reply.at("detail").at("reason").get<std::string>();
                    if(!s->owner.writeFrame(listenerproxy::wire(reply),listenerproxy::left(work->end(),150),s->nativeGeneration))
                        return fail(*s,"binding-listener-facts-unconfirmed");
                    return {};
                }
            }
            auto read = s->reader.advance(bytes, error);
            if (read == clientdetail::EventReader::Read::pending) {
                bool exited = false;
                {
                    std::lock_guard<std::mutex> waits(s->waitMutex);
                    exited = s->child.value && WaitForSingleObject(s->child.value, 0) == WAIT_OBJECT_0;
                }
                if (!exited) return {};
                // The same-image producer cannot write after its process exits.
                // Drain once more AFTER observing exit so an already committed
                // result/watch frame keeps its normal FIFO and validation path.
                // A descendant retaining the endpoint cannot keep this session
                // pending or repeatedly wake Runtime on the signaled process.
                if (s->delegation && !s->delegation->current())
                    return fail(*s, "binding-identity-delegation-revoked");
                read = s->reader.advance(bytes, error);
                if (read == clientdetail::EventReader::Read::pending)
                    return fail(*s,(s->listenerWork.load()||s->listenerFaultReported)?"binding-listener-facts-unconfirmed":s->resultSeen ? "binding-worker-exited" : "binding-worker-exited-before-result");
            }
            // Corrupt framing is a local protocol failure even when a readonly
            // source lookup is pending. Never turn it into retry authority.
            if(read==clientdetail::EventReader::Read::failed&&(error==ERROR_BAD_LENGTH||error==ERROR_HANDLE_EOF))
                return fail(*s,error==ERROR_BAD_LENGTH?"binding-frame-length-invalid":"binding-frame-truncated");
            if((read==clientdetail::EventReader::Read::ended||read==clientdetail::EventReader::Read::failed)&&
                (s->listenerWork.load()||s->listenerFaultReported))return fail(*s,"binding-listener-facts-unconfirmed");
            if (read == clientdetail::EventReader::Read::ended) {
                if (!s->resultSeen) return fail(*s, "binding-channel-ended-before-result");
                s->terminal = "binding-channel-ended";
                return {Packet::ended, {}, s->terminal};
            }
            if (read == clientdetail::EventReader::Read::failed) return fail(*s,
                error == ERROR_BAD_LENGTH ? "binding-frame-length-invalid" : error == ERROR_HANDLE_EOF ? "binding-frame-truncated" : "binding-read-failed");
            std::vector<std::set<std::string>> keys;
            auto value = nlohmann::json::parse(bytes, [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json& parsed) {
                worker::require(depth <= 32,"binding-packet-depth-invalid");
                if (event == nlohmann::json::parse_event_t::object_start) keys.emplace_back();
                else if (event == nlohmann::json::parse_event_t::object_end) keys.pop_back();
                else if (event == nlohmann::json::parse_event_t::key)
                    worker::require(!keys.empty() && keys.back().insert(parsed.get<std::string>()).second,"binding-duplicate-key");
                return true;
            });
            if(listenerproxy::isQuery(value)){
                try{
                    if(!s->listenerServer||s->listenerWork.load())return fail(*s,"binding-listener-protocol-invalid");
                    const auto delegation=s->delegation;
                    auto work=s->listenerServer->prepare(value,[delegation]{return !delegation||delegation->current();});
                    s->listenerWork.store(work,std::memory_order_release);
                    if(s->stopping.load(std::memory_order_acquire)||!work->current())return fail(*s,"binding-listener-identity-revoked");
                    const auto wake=listenerWakeFactory_?listenerWakeFactory_():listenerWake_;
                    const bool posted=listenerPost_&&listenerPost_([work]{work->run();},[work,wake]{work->publish();if(work->ready()&&wake)try{wake();}catch(...){}});
                    if(!posted){
                        s->listenerWork.store({});work->cancel();s->listenerFailure="live-busy";
                        if(!s->owner.writeFrame(listenerproxy::wire(work->busy()),listenerproxy::left(work->end(),150),s->nativeGeneration))
                            return fail(*s,"binding-listener-facts-unconfirmed");
                    }
                    return {};
                }catch(...){return fail(*s,"binding-listener-protocol-invalid");}
            }
            if (!value.is_object() || value.size() != 5 || value.value("schema", std::string{}) != worker::schema ||
                value.at("id") != s->request.id || value.at("generation") != std::to_string(s->request.generation) ||
                !value.at("detail").is_object()) return fail(*s, "binding-packet-identity-invalid");
            const auto kind = value.at("kind").get<std::string>();
            if (kind == "result") {
                if (s->resultSeen) return fail(*s, "binding-result-out-of-order");
                const auto& detail = value.at("detail");
                if (!detail.at("ok").is_boolean() || !detail.at("mutated").is_boolean())
                    return fail(*s, "binding-result-invalid");
                if (detail.at("ok").get<bool>()) {
                    const auto& context = detail.at("contextLease");
                    if (!context.is_object() || context.size() != 4) return fail(*s, "binding-context-identity-invalid");
                    worker::number(context.at("pid")); worker::decimal(context.at("birth"));
                    worker::wide(context.at("path")); worker::number(context.at("session"), false);
                    s->context = context; s->succeeded = true;
                }
                s->resultSeen = true;
            } else if (kind != "diagnostic") return fail(*s, "binding-packet-kind-invalid");
            else if(listenerproxy::boundarySourceFailure(s->listenerFailure)&&value.at("detail").value("reason",std::string{})==s->listenerFailure)
                s->listenerFaultReported=true;
            if(kind=="result"&&listenerproxy::boundarySourceFailure(s->listenerFailure)&&
                value.at("detail").value("reason",std::string{})==s->listenerFailure){
                value["detail"]["listenerFactsReason"]=s->listenerFailure;
                value["detail"]["reason"]="binding-listener-facts-unconfirmed";
            }
            // Never echo a bootstrap token supplied by a malformed child/fixture.
            const auto token = s->request.bootstrap.at("token").get<std::string>();
            if (value.dump().find(token) != std::string::npos) return fail(*s, "binding-packet-private-data");
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!enabled_ || canceled_ || current_ != s || s->epoch != epoch_ || s->stopping.load(std::memory_order_acquire))
                    return {Packet::ended, {}, "binding-session-stopped"};
            }
            // The original manager/epoch check has released mutex_ here.
            // Failure leaves mutation/dispatch unknown; it never fabricates a no-op.
            if (s->delegation && !s->delegation->current())
                return fail(*s, "binding-identity-delegation-revoked");
            return {Packet::message, std::move(value), {}};
        } catch (...) { return fail(*s, "binding-packet-invalid"); }
    }
    HANDLE takeContextProcess() {
        auto s = admit(); if (!s) return nullptr;
        Call call{*this, s};
        std::lock_guard<std::mutex> next(s->nextMutex);
        if (s->delegation && !s->delegation->current()) {
            requestCancel(s); return nullptr;
        }
        if (!s->succeeded || s->contextTaken || !s->terminal.empty() || s->stopping.load(std::memory_order_acquire)) return nullptr;
        try {
            const auto pid = worker::number(s->context.at("pid"));
            const auto birth = worker::decimal(s->context.at("birth"));
            const auto path = ymcc::steamlive::normalizedPath(worker::wide(s->context.at("path")));
            const auto session = worker::number(s->context.at("session"), false);
            if (session != s->request.windowsSession || !clientdetail::matchingPath(s->request.steam.root, path)) return nullptr;
            ULONGLONG steamBirth = 0;
            detail::Handle steam{ymcc::steamsession::verifiedProcess(s->request.steam.pid, s->request.steam.root, &steamBirth)};
            if (!steam.value || steamBirth != s->request.steam.created) return nullptr;
            detail::Handle context{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid)};
            if (!context.value || WaitForSingleObject(context.value, 0) != WAIT_TIMEOUT) return nullptr;
            DWORD observedSession = 0, length = 32768; wchar_t image[32768]{};
            FILETIME created{}, exited{}, kernel{}, user{};
            if (!GetProcessTimes(context.value, &created, &exited, &kernel, &user) ||
                ((static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime) != birth ||
                !QueryFullProcessImageNameW(context.value, 0, image, &length) ||
                ymcc::steamlive::normalizedPath(std::wstring(image, length)) != path ||
                !ymcc::deckymirror::sameWindowsTokenUser(context.value, &observedSession) || observedSession != session ||
                WaitForSingleObject(context.value, 0) != WAIT_TIMEOUT ||
                s->stopping.load(std::memory_order_acquire)) return nullptr;
            bool identityCurrent = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!enabled_ || canceled_ || current_ != s || s->epoch != epoch_ || s->stopping.load(std::memory_order_acquire)) return nullptr;
                identityCurrent = !s->delegation || s->delegation->current();
                if (identityCurrent) s->contextTaken = true;
            }
            // Keep the temporary handle owned until the last pure fence. All
            // cancellation and its Owner/reader locks occur outside mutex_.
            if (!identityCurrent || (s->delegation && !s->delegation->current())) {
                requestCancel(s); return nullptr;
            }
            if (WaitForSingleObject(steam.value, 0) != WAIT_TIMEOUT || WaitForSingleObject(context.value, 0) != WAIT_TIMEOUT) return nullptr;
            const HANDLE transferred = context.value; context.value = nullptr; return transferred;
        } catch (...) { return nullptr; }
    }
    Result cancel() {
        std::shared_ptr<Session> current, previous;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            enabled_ = false; canceled_ = true;
            current = current_; previous = previous_;
        }
        requestCancel(current); requestCancel(previous);
        // A cancellation receipt is not a claim that resources were destroyed.
        auto result = clientdetail::status(Code::ok);
        result.fullyDestroyed = !current && !previous;
        result.ioPending = bool(current || previous); // Conservative until stop/snapshot harvest completion.
        return result;
    }
    Result stop(DWORD waitMs = 1500) noexcept {
        if (waitMs == INFINITE) return clientdetail::status(Code::invalidArgument, ERROR_INVALID_PARAMETER);
        std::shared_ptr<Session> current, previous;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return clientdetail::status(Code::busy, ERROR_BUSY);
            stopping_ = true; current = current_; previous = previous_;
            if (current) current->stopping.store(true, std::memory_order_release);
            if (previous) previous->stopping.store(true, std::memory_order_release);
        }
        detail::Deadline deadline(waitMs);
        const auto old = stopSession(previous, deadline);
        auto result = stopSession(current, deadline);
        // begin can have been the caller cleaning previous when stop first tried.
        auto previousResult = old;
        if (!previousResult.fullyDestroyed) previousResult = stopSession(previous, deadline);
        result.fullyDestroyed = result.fullyDestroyed && previousResult.fullyDestroyed;
        result.ioPending = result.ioPending || previousResult.ioPending;
        if (!result.fullyDestroyed) {
            result.code = Code::cleanupIncomplete;
            if (!result.error) result.error = previousResult.error ? previousResult.error : ERROR_TIMEOUT;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (current_ == current && (!current || current->destroyed.load(std::memory_order_acquire))) current_.reset();
            if (previous_ == previous && (!previous || previous->destroyed.load(std::memory_order_acquire))) previous_.reset();
            stopping_ = false;
        }
        return result;
    }
    Snapshot snapshot() const {
        std::array<std::shared_ptr<Session>, 2> sessions;
        Snapshot out;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions = {current_, previous_}; out.epoch = epoch_; out.enabled = enabled_; out.canceled = canceled_;
            for (const auto& s : sessions) if (s) out.activeCalls += s->calls;
        }
        for (const auto& s : sessions) if (s) {
            ++out.sessions;
            const auto owner = s->owner.snapshot(); const auto reader = s->reader.snapshot();
            out.owner.handles += owner.handles; out.owner.activeCalls += owner.activeCalls;
            out.owner.pendingIo += owner.pendingIo;
            out.owner.jobActiveProcesses = out.owner.jobActiveProcesses == MAXDWORD || owner.jobActiveProcesses == MAXDWORD ? MAXDWORD : out.owner.jobActiveProcesses + owner.jobActiveProcesses;
            out.owner.childExited = out.owner.childExited && owner.childExited;
            out.owner.started = out.owner.started || owner.started;
            out.owner.stopRequested = out.owner.stopRequested || owner.stopRequested;
            out.owner.jobLimitFlags |= owner.jobLimitFlags;
            out.owner.jobTerminationAttempted = out.owner.jobTerminationAttempted || owner.jobTerminationAttempted;
            if (!out.owner.processId) { out.owner.processId = owner.processId; out.owner.generation = owner.generation; out.owner.exitCode = owner.exitCode; }
            if (owner.cleanupError) out.owner.cleanupError = owner.cleanupError;
            out.owner.fullyDestroyed = out.owner.fullyDestroyed && owner.fullyDestroyed;
            out.readerHandles += reader.handles; out.readerPendingIo += reader.pendingIo;
            out.readerFullyDestroyed = out.readerFullyDestroyed && reader.fullyDestroyed;
            out.handles += owner.handles + reader.handles; out.activeCalls += owner.activeCalls;
            out.pendingIo += owner.pendingIo + reader.pendingIo;
            out.jobActiveProcesses = out.jobActiveProcesses == MAXDWORD || owner.jobActiveProcesses == MAXDWORD ? MAXDWORD : out.jobActiveProcesses + owner.jobActiveProcesses;
            out.started = out.started || owner.started;
            out.childExited = out.childExited && owner.childExited;
            out.stopRequested = out.stopRequested || s->stopping.load(std::memory_order_acquire);
            out.jobTerminationAttempted = out.jobTerminationAttempted || owner.jobTerminationAttempted;
            out.jobLimitFlags |= owner.jobLimitFlags;
            if (!out.processId) { out.processId = owner.processId; out.generation = owner.generation; out.exitCode = owner.exitCode; }
            if (owner.cleanupError) out.cleanupError = owner.cleanupError;
            if (reader.error) out.cleanupError = reader.error;
            std::lock_guard<std::mutex> waits(s->waitMutex);
            out.clientHandles += unsigned(s->child.value != nullptr) + unsigned(s->callsDone.value != nullptr);
        }
        out.handles += out.clientHandles;
        out.fullyDestroyed = out.handles == 0 && out.pendingIo == 0 && out.activeCalls == 0 && out.childExited &&
            out.jobActiveProcesses == 0 && out.owner.fullyDestroyed && out.readerFullyDestroyed;
        return out;
    }
};
} // namespace ymcc::deckybinding





