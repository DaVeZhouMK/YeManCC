#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#pragma comment(lib, "shell32.lib")

// Pending integration component. No Loader, Runtime, Steam, settings or device calls.
// Owner is one-shot: stop seals its generation, including stop before start.
// Keep the C++ object alive until concurrent public method calls have returned.
namespace ymcc::deckybinding {
inline constexpr wchar_t childSwitch[] = L"--ymcc-decky-binding-worker";
inline constexpr std::uint32_t maxFrameBytes = 128u * 1024u;
enum class Code { ok, notStarted, stopped, busy, invalidArgument, tooLarge,
    cancelled, timeout, childExited, brokenPipe, systemError, cleanupIncomplete };
enum class StopPolicy { terminateAfterGrace, cooperativeOnly };
enum class ChildMode { ordinary, worker, invalid };
struct Result {
    Code code = Code::ok;
    DWORD error = ERROR_SUCCESS;
    bool fullyDestroyed = false;
    bool ioPending = false;
    std::uint64_t generation = 0;
    std::vector<std::uint8_t> payload;
    explicit operator bool() const noexcept { return code == Code::ok; }
};
struct Snapshot {
    std::uint64_t generation = 0;
    DWORD processId = 0, exitCode = STILL_ACTIVE, cleanupError = ERROR_SUCCESS;
    DWORD jobActiveProcesses = 0, jobLimitFlags = 0;
    unsigned handles = 0, activeCalls = 0, pendingIo = 0;
    bool started = false, stopRequested = false, childExited = true;
    bool jobTerminationAttempted = false, fullyDestroyed = true;
};
namespace detail {
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) noexcept : value(h == INVALID_HANDLE_VALUE ? nullptr : h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (value) CloseHandle(value); }
    bool close(DWORD& error) noexcept {
        if (!value) return true;
        if (!CloseHandle(value)) { error = GetLastError(); return false; }
        value = nullptr;
        return true;
    }
};
struct Deadline {
    ULONGLONG end;
    explicit Deadline(DWORD ms) noexcept : end(GetTickCount64() + ms) {}
    DWORD left() const noexcept {
        const auto now = GetTickCount64();
        return now >= end ? 0 : static_cast<DWORD>(end - now);
    }
};
struct Io {
    Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    OVERLAPPED overlapped{};
    std::vector<std::uint8_t> buffer;
    bool writing = false, active = true, pending = false;
    explicit Io(bool write) : writing(write) { overlapped.hEvent = event.value; }
};
struct State {
    std::mutex mutex;
    std::atomic<bool> cancelRequested{false};
    std::mutex cancelMutex; // Only event publication/signal/close; never held across IO or waits.
    Handle pipe, peer, cancel, job, port, process, thread, changed;
    std::vector<std::shared_ptr<Io>> operations;
    // Stop-only read leases: Job zero may precede final process signalling.
    std::array<Handle,64> stopMembers;
    bool stopMembersCaptured = false;
    std::uint64_t generation = 0;
    DWORD pid = 0, exitCode = STILL_ACTIVE, cleanupError = ERROR_SUCCESS;
    unsigned diagnosticWaiters = 0;
    bool started = false, attempted = false, stopping = false, stopBusy = false;
    bool assigned = false, childExited = true, jobTerminated = false, childSide = false;
    bool drainWaitActive = false;
    State* retainedNext = nullptr;
    State() { operations.reserve(2); } // At most one frame in each direction; allocate before owning handles.
};
inline std::atomic<std::uint64_t> serial{0};
// Exceptional OS cleanup failures remain observable and retryable for process lifetime.
// The list itself is never destroyed while a kernel request may reference its buffers.
inline std::mutex retainedMutex;
inline State* retained = nullptr;
inline void retain(State* state) noexcept {
    std::lock_guard<std::mutex> lock(retainedMutex);
    state->retainedNext = retained;
    retained = state;
}
inline bool complete(State& s, Io& io, DWORD& bytes, DWORD& error) noexcept {
    if (!io.pending) return true;
    if (GetOverlappedResult(s.pipe.value, &io.overlapped, &bytes, FALSE)) {
        io.pending = false;
        return true;
    }
    error = GetLastError();
    if (error == ERROR_IO_INCOMPLETE) return false;
    io.pending = false; // Includes ERROR_OPERATION_ABORTED: real completion observed.
    return true;
}
inline void reap(State& s) noexcept {
    if (s.drainWaitActive) return; // A concurrent snapshot must not close a handle being waited on.
    for (auto it = s.operations.begin(); it != s.operations.end();) {
        DWORD bytes = 0, error = 0;
        if (!(*it)->active && complete(s, **it, bytes, error)) {
            if ((*it)->event.close(s.cleanupError)) it = s.operations.erase(it);
            else ++it;
        } else ++it;
    }
}
inline DWORD jobCount(State& s) noexcept {
    if (!s.job.value || !s.assigned) return 0;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
    if (!QueryInformationJobObject(s.job.value, JobObjectBasicAccountingInformation,
                                  &info, sizeof(info), nullptr)) {
        s.cleanupError = GetLastError();
        return MAXDWORD; // Unknown is never a proof of zero processes.
    }
    return info.ActiveProcesses;
}
inline Snapshot snapshotLocked(State& s) noexcept {
    reap(s);
    Snapshot out;
    out.generation = s.generation; out.processId = s.pid;
    out.started = s.started; out.stopRequested = s.stopping;
    if (s.process.value && WaitForSingleObject(s.process.value, 0) == WAIT_OBJECT_0) {
        s.childExited = true;
        if (!GetExitCodeProcess(s.process.value, &s.exitCode)) s.cleanupError = GetLastError();
    }
    out.childExited = s.childExited; out.exitCode = s.exitCode;
    out.jobTerminationAttempted = s.jobTerminated;
    out.jobActiveProcesses = jobCount(s);
    if (s.job.value) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        if (QueryInformationJobObject(s.job.value, JobObjectExtendedLimitInformation,
                                      &info, sizeof(info), nullptr))
            out.jobLimitFlags = info.BasicLimitInformation.LimitFlags;
        else s.cleanupError = GetLastError();
    }
    const HANDLE handles[] = {s.pipe.value,s.peer.value,s.cancel.value,s.job.value,
        s.port.value,s.process.value,s.thread.value,s.changed.value};
    for (HANDLE h : handles) if (h) ++out.handles;
    for(const auto& member:s.stopMembers)if(member.value)++out.handles;
    for (const auto& io : s.operations) {
        if (io->event.value) ++out.handles;
        if (io->active) ++out.activeCalls;
        if (io->pending) ++out.pendingIo;
    }
    out.activeCalls += s.diagnosticWaiters;
    out.cleanupError = s.cleanupError;
    out.fullyDestroyed = out.handles == 0 && s.operations.empty() && !s.diagnosticWaiters && s.childExited;
    return out;
}
inline Result resultLocked(State& s, Code code, DWORD error = ERROR_SUCCESS) noexcept {
    const auto snap = snapshotLocked(s);
    Result r; r.code = code; r.error = error; r.generation = s.generation;
    r.fullyDestroyed = snap.fullyDestroyed; r.ioPending = snap.pendingIo != 0;
    return r;
}
inline void requestCancel(State& s) noexcept {
    s.cancelRequested.store(true, std::memory_order_release);
    s.stopping = true;
    if (!s.childSide) {
        std::lock_guard<std::mutex> signal(s.cancelMutex);
        if (s.cancel.value && !SetEvent(s.cancel.value)) s.cleanupError = GetLastError();
    }
    if (s.pipe.value && !CancelIoEx(s.pipe.value, nullptr)) {
        const DWORD error = GetLastError();
        if (error != ERROR_NOT_FOUND) s.cleanupError = error;
    }
    if (s.changed.value) SetEvent(s.changed.value);
}
inline Code ioCode(DWORD error) noexcept {
    if (error == ERROR_OPERATION_ABORTED) return Code::cancelled;
    if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_NO_DATA)
        return Code::brokenPipe;
    return Code::systemError;
}
inline Result frame(State& s, bool writing, const std::vector<std::uint8_t>& payload,
                    DWORD timeoutMs, std::uint64_t expectedGeneration) {
    std::shared_ptr<Io> io;
    std::unique_lock<std::mutex> lock(s.mutex);
    if (timeoutMs == INFINITE) return resultLocked(s, Code::invalidArgument, ERROR_INVALID_PARAMETER);
    if (expectedGeneration && expectedGeneration != s.generation)
        return resultLocked(s, Code::stopped, ERROR_INVALID_STATE);
    if (s.stopping || s.cancelRequested.load(std::memory_order_acquire))
        return resultLocked(s, Code::stopped, ERROR_OPERATION_ABORTED);
    if (!s.started) return resultLocked(s, Code::notStarted, ERROR_INVALID_STATE);
    if (writing && payload.size() > maxFrameBytes)
        return resultLocked(s, Code::tooLarge, ERROR_BAD_LENGTH);
    reap(s);
    for (const auto& current : s.operations)
        if (current->writing == writing) return resultLocked(s, Code::busy, ERROR_BUSY);
    try {
        io = std::make_shared<Io>(writing);
        if (!io->event.value) return resultLocked(s, Code::systemError, GetLastError());
        s.operations.push_back(io); // Reserved State storage; admission already limits this to two.
        io->buffer.resize(writing ? payload.size() + sizeof(std::uint32_t) : maxFrameBytes + sizeof(std::uint32_t));
        if (writing) {
            const auto size = static_cast<std::uint32_t>(payload.size());
            std::memcpy(io->buffer.data(), &size, sizeof(size)); // Win32 little endian wire format.
            if (size) std::memcpy(io->buffer.data() + sizeof(size), payload.data(), size);
        }
    } catch (...) {
        if (io) io->active = false;
        return resultLocked(s, Code::systemError, ERROR_NOT_ENOUGH_MEMORY);
    }
    Deadline deadline(timeoutMs);
    Code code = Code::ok;
    DWORD error = ERROR_SUCCESS;
    std::size_t transferred = 0, target = writing ? io->buffer.size() : sizeof(std::uint32_t);
    while (transferred < target) {
        if (s.stopping || s.cancelRequested.load(std::memory_order_acquire) || WaitForSingleObject(s.cancel.value, 0) == WAIT_OBJECT_0) {
            code = Code::cancelled; error = ERROR_OPERATION_ABORTED; break;
        }
        io->overlapped = {}; io->overlapped.hEvent = io->event.value;
        if (!ResetEvent(io->event.value)) { code = Code::systemError; error = GetLastError(); break; }
        DWORD bytes = 0;
        const auto amount = static_cast<DWORD>(target - transferred);
        const BOOL immediate = writing ? WriteFile(s.pipe.value, io->buffer.data() + transferred,
            amount, &bytes, &io->overlapped) : ReadFile(s.pipe.value, io->buffer.data() + transferred,
            amount, &bytes, &io->overlapped);
        const DWORD issuedError = immediate ? ERROR_SUCCESS : GetLastError();
        if (!immediate && issuedError != ERROR_IO_PENDING) {
            code = ioCode(issuedError); error = issuedError; break;
        }
        io->pending = true;
        if (s.changed.value) SetEvent(s.changed.value);
        if (!immediate) {
            if (s.cancelRequested.load(std::memory_order_acquire)) requestCancel(s);
            HANDLE waits[] = {s.cancel.value, io->event.value, s.process.value};
            const DWORD count = s.process.value ? 3u : 2u;
            lock.unlock();
            const DWORD waited = WaitForMultipleObjects(count, waits, FALSE, deadline.left());
            lock.lock();
            if (waited != WAIT_OBJECT_0 + 1) {
                if (waited == WAIT_OBJECT_0) { code = Code::cancelled; error = ERROR_OPERATION_ABORTED; }
                else if (waited == WAIT_OBJECT_0 + 2) { code = Code::childExited; error = ERROR_PROCESS_ABORTED; }
                else if (waited == WAIT_TIMEOUT) { code = Code::timeout; error = ERROR_TIMEOUT; }
                else { code = Code::systemError; error = GetLastError(); }
                break;
            }
        }
        error = ERROR_SUCCESS;
        if (!complete(s, *io, bytes, error)) { code = Code::systemError; break; }
        if (error != ERROR_SUCCESS) { code = ioCode(error); break; }
        if (bytes == 0) { code = Code::brokenPipe; error = ERROR_BROKEN_PIPE; break; }
        transferred += bytes;
        if (!writing && transferred == sizeof(std::uint32_t) && target == sizeof(std::uint32_t)) {
            std::uint32_t length = 0;
            std::memcpy(&length, io->buffer.data(), sizeof(length));
            if (length > maxFrameBytes) { code = Code::tooLarge; error = ERROR_BAD_LENGTH; break; }
            target += length;
        }
    }
    if (io->pending) {
        if (!CancelIoEx(s.pipe.value, &io->overlapped)) {
            const DWORD cancelError = GetLastError();
            if (cancelError != ERROR_NOT_FOUND) s.cleanupError = cancelError;
        }
        DWORD bytes = 0, completionError = 0;
        complete(s, *io, bytes, completionError); // Never waits; state survives an incomplete cancel.
    }
    // A partial/aborted frame cannot safely be reused or have a write replayed.
    if (code != Code::ok) requestCancel(s);
    Result r;
    if (code == Code::ok && !writing) {
        // Reuse the already allocated heap buffer; no throwing allocation after registration.
        io->buffer.resize(target);
        io->buffer.erase(io->buffer.begin(), io->buffer.begin() + sizeof(std::uint32_t));
        r.payload = std::move(io->buffer);
    }
    io->active = false;
    if (s.changed.value) SetEvent(s.changed.value);
    const auto status = resultLocked(s, code, error);
    r.code = status.code; r.error = status.error; r.generation = status.generation;
    r.fullyDestroyed = status.fullyDestroyed; r.ioPending = status.ioPending;
    return r;
}
inline bool closeAll(State& s) noexcept {
    bool okay = true;
    Handle* handles[] = {&s.peer,&s.thread,&s.pipe,&s.cancel,&s.process,&s.job,&s.port,&s.changed};
    for (auto* h : handles) {
        if (h == &s.cancel) {
            std::lock_guard<std::mutex> signal(s.cancelMutex);
            if (!h->close(s.cleanupError)) okay = false;
        } else if (!h->close(s.cleanupError)) okay = false;
    }
    for(auto& member:s.stopMembers)if(!member.close(s.cleanupError))okay=false;
    return okay;
}
// Stop observes existing owned members before cancellation completes them.
// A recycled PID/foreign Job never becomes a target; no healthy-path scan.
inline bool captureStopMembers(State& s) noexcept {
    if(s.stopMembersCaptured||!s.assigned)return true;
    struct Members {DWORD assigned=0,count=0;ULONG_PTR ids[64]{};} members;
    if(!QueryInformationJobObject(s.job.value,JobObjectBasicProcessIdList,&members,sizeof(members),nullptr)){
        s.cleanupError=GetLastError();return false;
    }
    if(members.count>64||members.assigned>64){s.cleanupError=ERROR_MORE_DATA;return false;}
    size_t kept=0;
    for(DWORD i=0;i<members.count;++i){
        const auto pid=members.ids[i];if(!pid||pid>MAXDWORD)continue;
        Handle process(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(pid)));
        if(!process.value){const auto error=GetLastError();if(error!=ERROR_INVALID_PARAMETER){s.cleanupError=error;return false;}continue;}
        BOOL member=FALSE;
        if(!IsProcessInJob(process.value,s.job.value,&member)){s.cleanupError=GetLastError();return false;}
        if(!member)continue;
        if(std::any_of(s.stopMembers.begin(),s.stopMembers.end(),[&](const Handle& known){return known.value&&GetProcessId(known.value)==pid;}))continue;
        while(kept<s.stopMembers.size()&&s.stopMembers[kept].value)++kept;
        if(kept==s.stopMembers.size()){s.cleanupError=ERROR_MORE_DATA;return false;}
        s.stopMembers[kept++].value=process.value;process.value=nullptr;
    }
    s.stopMembersCaptured=true;return true;
}
inline Result stop(State& s, DWORD graceMs, DWORD terminateWaitMs, StopPolicy policy) noexcept {
    std::unique_lock<std::mutex> lock(s.mutex);
    if (graceMs == INFINITE || terminateWaitMs == INFINITE)
        return resultLocked(s, Code::invalidArgument, ERROR_INVALID_PARAMETER);
    requestCancel(s); // Seal admission and signal the inherited event before any wait.
    if (s.stopBusy) return resultLocked(s, Code::busy, ERROR_BUSY);
    s.stopBusy = true;
    const bool memberLeasesKnown=captureStopMembers(s);
    const auto finish = [&](Code code, DWORD error = ERROR_SUCCESS) {
        s.stopBusy = false;
        return resultLocked(s, code, error);
    };
    Deadline grace(graceMs);
    if (s.process.value && !s.childExited) {
        const HANDLE process = s.process.value;
        lock.unlock();
        const DWORD waited = WaitForSingleObject(process, grace.left());
        lock.lock();
        if (waited == WAIT_OBJECT_0) s.childExited = true;
        else if (waited == WAIT_FAILED) s.cleanupError = GetLastError();
    }
    // Process signalling can precede the Job's ACTIVE_PROCESS_ZERO notification.
    // Spend the remaining cooperative grace on that notification before using force.
    while (s.childExited && s.assigned && jobCount(s) != 0 && grace.left()) {
        const HANDLE port = s.port.value;
        lock.unlock();
        DWORD message = 0; ULONG_PTR key = 0; LPOVERLAPPED process = nullptr;
        const BOOL received = GetQueuedCompletionStatus(port, &message, &key, &process, grace.left());
        const DWORD receiveError = received ? ERROR_SUCCESS : GetLastError();
        lock.lock();
        if (!received) { if (receiveError != WAIT_TIMEOUT) s.cleanupError = receiveError; break; }
    }
    const DWORD active = jobCount(s);
    if ((!s.childExited || active != 0) && policy == StopPolicy::terminateAfterGrace) {
        s.jobTerminated = true;
        BOOL killed = FALSE;
        if (s.assigned) killed = TerminateJobObject(s.job.value, ERROR_OPERATION_ABORTED);
        // Only a freshly created, still suspended child can take this startup-failure path.
        else if (s.process.value) killed = TerminateProcess(s.process.value, ERROR_OPERATION_ABORTED);
        if (!killed) s.cleanupError = GetLastError();
    }
    Deadline deadline(terminateWaitMs);
    if (s.process.value && !s.childExited) {
        const HANDLE process = s.process.value;
        lock.unlock();
        const DWORD waited = WaitForSingleObject(process, deadline.left());
        lock.lock();
        if (waited == WAIT_OBJECT_0) s.childExited = true;
        else if (waited == WAIT_FAILED) s.cleanupError = GetLastError();
    }
    // Job notifications wake this bounded drain; no polling or helper thread.
    while (s.assigned && jobCount(s) != 0 && deadline.left()) {
        const HANDLE port = s.port.value;
        lock.unlock();
        DWORD message = 0; ULONG_PTR key = 0; LPOVERLAPPED process = nullptr;
        const BOOL received = GetQueuedCompletionStatus(port, &message, &key, &process, deadline.left());
        const DWORD receiveError = received ? ERROR_SUCCESS : GetLastError();
        lock.lock();
        if (!received) { if (receiveError != WAIT_TIMEOUT) s.cleanupError = receiveError; break; }
    }
    bool memberExitsConfirmed=memberLeasesKnown;
    for(auto& member:s.stopMembers){
        if(!member.value)continue;
        const HANDLE process=member.value;
        lock.unlock();const auto waited=WaitForSingleObject(process,deadline.left());lock.lock();
        if(waited!=WAIT_OBJECT_0){memberExitsConfirmed=false;if(waited==WAIT_FAILED)s.cleanupError=GetLastError();}
    }
    if(!memberExitsConfirmed)return finish(Code::cleanupIncomplete,s.cleanupError?s.cleanupError:WAIT_TIMEOUT);
    // Operations own OVERLAPPED, buffer and event until GetOverlappedResult proves completion.
    for (;;) {
        reap(s);
        if (s.operations.empty() && !s.diagnosticWaiters) break;
        HANDLE waits[3] = {s.changed.value, nullptr, nullptr};
        DWORD count = s.changed.value ? 1u : 0u;
        for (const auto& io : s.operations)
            if (!io->active && io->pending && count < 3) waits[count++] = io->event.value;
        if (!count || !deadline.left()) break;
        s.drainWaitActive = true;
        lock.unlock();
        const DWORD waited = WaitForMultipleObjects(count, waits, FALSE, deadline.left());
        lock.lock();
        s.drainWaitActive = false;
        if (waited == WAIT_FAILED || waited == WAIT_TIMEOUT) {
            if (waited == WAIT_FAILED) s.cleanupError = GetLastError();
            break;
        }
    }
    if (s.process.value && s.childExited && !GetExitCodeProcess(s.process.value, &s.exitCode))
        s.cleanupError = GetLastError();
    if (!s.childExited || jobCount(s) != 0 || !s.operations.empty() || s.diagnosticWaiters)
        return finish(Code::cleanupIncomplete, s.cleanupError ? s.cleanupError : ERROR_TIMEOUT);
    if (!closeAll(s)) return finish(Code::cleanupIncomplete, s.cleanupError);
    return finish(Code::ok);
}
struct Attributes {
    std::vector<std::uint8_t> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    ~Attributes() { if (list) DeleteProcThreadAttributeList(list); }
    bool initialize(HANDLE* handles) {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        storage.resize(size);
        auto* candidate = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(candidate, 1, 0, &size)) return false;
        list = candidate;
        return UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            handles, 2 * sizeof(HANDLE), nullptr, nullptr) != FALSE;
    }
};
inline std::wstring executable() {
    std::wstring path(512, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length) return {};
        if (length < path.size()) { path.resize(length); return path; }
        if (path.size() >= 32768) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return {}; }
        path.resize((std::min)(path.size() * 2, std::size_t{32768}));
    }
}
} // namespace detail

class Owner {
    std::unique_ptr<detail::State> state_{std::make_unique<detail::State>()};
public:
    Owner() { state_->generation = detail::serial.fetch_add(1) + 1; }
    Owner(const Owner&) = delete; Owner& operator=(const Owner&) = delete;
    ~Owner() noexcept {
        if (!stop().fullyDestroyed) detail::retain(state_.release());
    }
    Result start() {
        auto& s = *state_;
        std::unique_lock<std::mutex> lock(s.mutex);
        if (s.stopping || s.cancelRequested.load(std::memory_order_acquire))
            return detail::resultLocked(s, Code::stopped, ERROR_OPERATION_ABORTED);
        if (s.attempted) return detail::resultLocked(s, Code::busy, ERROR_ALREADY_INITIALIZED);
        s.attempted = true;
        DWORD error = ERROR_SUCCESS;
        try {
            const auto prepare = [&]() -> bool {
                s.changed.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
                const HANDLE cancelEvent = CreateEventW(&inherit, TRUE, FALSE, nullptr);
                {
                    std::lock_guard<std::mutex> signal(s.cancelMutex);
                    s.cancel.value = cancelEvent;
                    if (s.cancel.value && s.cancelRequested.load(std::memory_order_acquire)) SetEvent(s.cancel.value);
                }
                s.job.value = CreateJobObjectW(nullptr, nullptr);
                s.port.value = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
                if (!s.changed.value || !s.cancel.value || !s.job.value || !s.port.value) return false;
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
                limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                if (!SetInformationJobObject(s.job.value, JobObjectExtendedLimitInformation,
                                              &limits, sizeof(limits))) return false;
                JOBOBJECT_ASSOCIATE_COMPLETION_PORT completion{};
                completion.CompletionKey = &s; completion.CompletionPort = s.port.value;
                if (!SetInformationJobObject(s.job.value, JobObjectAssociateCompletionPortInformation,
                                              &completion, sizeof(completion))) return false;
                const auto pipeName = L"\\\\.\\pipe\\ymcc-decky-binding-" +
                    std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(s.generation);
                s.pipe.value = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX |
                    FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                    1, 4096, 4096, 0, nullptr);
                if (s.pipe.value == INVALID_HANDLE_VALUE) s.pipe.value = nullptr;
                if (!s.pipe.value) return false;
                s.peer.value = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                    &inherit, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
                if (s.peer.value == INVALID_HANDLE_VALUE) s.peer.value = nullptr;
                if (!s.peer.value) return false;
                // Use real heap OVERLAPPED storage even for the already-connected local peer.
                // An unexpected pending connect follows the same retained-completion rules.
                auto connection = std::make_shared<detail::Io>(false);
                if (!connection->event.value) return false;
                s.operations.push_back(connection);
                const BOOL connected = ConnectNamedPipe(s.pipe.value, &connection->overlapped);
                const DWORD connectError = connected ? ERROR_SUCCESS : GetLastError();
                connection->active = false;
                connection->pending = !connected && connectError == ERROR_IO_PENDING;
                if (!connected && connectError != ERROR_PIPE_CONNECTED) {
                    SetLastError(connectError); return false;
                }
                detail::reap(s);
                const auto image = detail::executable();
                if (image.empty()) return false;
                std::wstring command = L"\"" + image + L"\" " + childSwitch + L" " +
                    std::to_wstring(reinterpret_cast<std::uintptr_t>(s.peer.value)) + L" " +
                    std::to_wstring(reinterpret_cast<std::uintptr_t>(s.cancel.value));
                HANDLE inherited[] = {s.peer.value, s.cancel.value};
                detail::Attributes attributes;
                if (!attributes.initialize(inherited)) return false;
                STARTUPINFOEXW startup{};
                startup.StartupInfo.cb = sizeof(startup);
                startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
                startup.StartupInfo.wShowWindow = SW_HIDE;
                startup.StartupInfo.hStdInput = INVALID_HANDLE_VALUE;
                startup.StartupInfo.hStdOutput = INVALID_HANDLE_VALUE;
                startup.StartupInfo.hStdError = INVALID_HANDLE_VALUE;
                startup.lpAttributeList = attributes.list;
                PROCESS_INFORMATION info{};
                if (!CreateProcessW(image.c_str(), command.data(), nullptr, nullptr, TRUE,
                    CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                    nullptr, nullptr, &startup.StartupInfo, &info)) return false;
                s.process.value = info.hProcess; s.thread.value = info.hThread;
                s.pid = info.dwProcessId; s.childExited = false;
                if (!SetHandleInformation(s.cancel.value, HANDLE_FLAG_INHERIT, 0)) return false;
                if (!AssignProcessToJobObject(s.job.value, s.process.value)) return false;
                s.assigned = true;
                // Close the parent's copy before resuming, so child death really closes the peer.
                if (!s.peer.close(s.cleanupError)) { SetLastError(s.cleanupError); return false; }
                if (s.cancelRequested.load(std::memory_order_acquire)) { SetLastError(ERROR_OPERATION_ABORTED); return false; }
                if (ResumeThread(s.thread.value) == MAXDWORD) return false;
                if (!s.thread.close(s.cleanupError)) { SetLastError(s.cleanupError); return false; }
                s.started = true;
                return true;
            };
            if (prepare()) return detail::resultLocked(s, Code::ok);
            error = GetLastError();
        } catch (...) { error = ERROR_NOT_ENOUGH_MEMORY; }
        lock.unlock();
        auto cleanup = stop();
        cleanup.code = cleanup.fullyDestroyed ? Code::systemError : Code::cleanupIncomplete;
        cleanup.error = error ? error : ERROR_GEN_FAILURE;
        return cleanup;
    }
    // Latch first, then try the state lock: safe inside Runtime's stateMutex even
    // while start owns the state or a frame is being issued. stop performs drain.
    Result requestCancel() noexcept {
        auto& s = *state_;
        s.cancelRequested.store(true, std::memory_order_release);
        DWORD signalError = ERROR_SUCCESS;
        {
            std::lock_guard<std::mutex> signal(s.cancelMutex);
            if (s.cancel.value && !SetEvent(s.cancel.value)) signalError = GetLastError();
        }
        std::unique_lock<std::mutex> lock(s.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            detail::requestCancel(s);
            if (signalError) s.cleanupError = signalError;
            return detail::resultLocked(s, signalError ? Code::systemError : Code::ok, signalError);
        }
        Result r; r.generation = s.generation; r.ioPending = true;
        if (signalError) { r.code = Code::systemError; r.error = signalError; }
        return r;
    }
    // Caller owns these non-inheritable observation leases. No job/kill access.
    HANDLE copyReadPipe() const noexcept {
        std::lock_guard<std::mutex> lock(state_->mutex);
        HANDLE copy = nullptr;
        if (state_->stopping || state_->cancelRequested.load(std::memory_order_acquire) || !state_->started) return nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), state_->pipe.value, GetCurrentProcess(),
                             &copy, GENERIC_READ | SYNCHRONIZE, FALSE, 0)) return nullptr;
        return copy;
    }
    HANDLE copyChildProcess() const noexcept {
        std::lock_guard<std::mutex> lock(state_->mutex);
        HANDLE copy = nullptr;
        if (state_->stopping || state_->cancelRequested.load(std::memory_order_acquire) || !state_->started) return nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), state_->process.value, GetCurrentProcess(),
                             &copy, PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, 0)) return nullptr;
        return copy;
    }
    Result writeFrame(const std::vector<std::uint8_t>& payload, DWORD timeoutMs,
                      std::uint64_t expectedGeneration = 0) {
        return detail::frame(*state_, true, payload, timeoutMs, expectedGeneration);
    }
    Result readFrame(DWORD timeoutMs, std::uint64_t expectedGeneration = 0) {
        return detail::frame(*state_, false, {}, timeoutMs, expectedGeneration);
    }
    Result stop(DWORD graceMs = 150, DWORD terminateWaitMs = 1500,
                StopPolicy policy = StopPolicy::terminateAfterGrace) noexcept {
        return detail::stop(*state_, graceMs, terminateWaitMs, policy);
    }
    Snapshot snapshot() const noexcept {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return detail::snapshotLocked(*state_);
    }
    // Bounded diagnostic wait. No callback, polling loop, or internal worker thread.
    bool waitForPendingIo(DWORD timeoutMs) const noexcept {
        if (timeoutMs == INFINITE) return false;
        detail::Deadline deadline(timeoutMs);
        std::unique_lock<std::mutex> lock(state_->mutex);
        ++state_->diagnosticWaiters;
        const auto finish = [&](bool pending) {
            --state_->diagnosticWaiters;
            if (state_->changed.value) SetEvent(state_->changed.value);
            return pending;
        };
        for (;;) {
            const auto snap = detail::snapshotLocked(*state_);
            if (snap.pendingIo) return finish(true);
            if (state_->stopping || !state_->changed.value || !deadline.left()) return finish(false);
            const HANDLE changed = state_->changed.value;
            lock.unlock();
            const DWORD waited = WaitForSingleObject(changed, deadline.left());
            lock.lock();
            if (waited != WAIT_OBJECT_0) return finish(false);
        }
    }
    static std::vector<Snapshot> retainedFailures() {
        std::lock_guard<std::mutex> retainedLock(detail::retainedMutex);
        std::vector<Snapshot> result;
        for (auto* s = detail::retained; s; s = s->retainedNext) {
            std::lock_guard<std::mutex> lock(s->mutex);
            result.push_back(detail::snapshotLocked(*s));
        }
        return result;
    }
    static void retryRetainedCleanup(DWORD waitMs = 1500) noexcept {
        std::lock_guard<std::mutex> retainedLock(detail::retainedMutex);
        auto** link = &detail::retained;
        while (*link) {
            auto* s = *link;
            if (detail::stop(*s, 0, waitMs, StopPolicy::terminateAfterGrace).fullyDestroyed) {
                *link = s->retainedNext; delete s;
            } else link = &s->retainedNext;
        }
    }
};

class ChildChannel {
    std::unique_ptr<detail::State> state_{std::make_unique<detail::State>()};
    static bool parseHandle(std::wstring_view text, HANDLE& handle) noexcept {
        if (text.empty() || text.size() > 20) return false;
        std::uintptr_t value = 0;
        for (const wchar_t c : text) {
            if (c < L'0' || c > L'9') return false;
            const auto digit = static_cast<std::uintptr_t>(c - L'0');
            if (value > ((std::numeric_limits<std::uintptr_t>::max)() - digit) / 10) return false;
            value = value * 10 + digit;
        }
        if (!value || value == (std::numeric_limits<std::uintptr_t>::max)()) return false;
        handle = reinterpret_cast<HANDLE>(value);
        return true;
    }
    // Handle syntax is not authority. Authenticate the live pipe server before adopting
    // inherited resources or allowing any production worker operation to execute.
    static bool qualifiedSource(HANDLE pipe, DWORD& error) noexcept {
        const auto reject = [&](DWORD why) noexcept { error = why ? why : ERROR_ACCESS_DENIED; return false; };
        try {
            DWORD serverPid = 0, pipeSession = 0, currentSession = 0, serverSession = 0;
            BOOL inJob = FALSE;
            if (!GetNamedPipeServerProcessId(pipe, &serverPid) ||
                !GetNamedPipeServerSessionId(pipe, &pipeSession) ||
                !ProcessIdToSessionId(GetCurrentProcessId(), &currentSession) ||
                !IsProcessInJob(GetCurrentProcess(), nullptr, &inJob))
                return reject(GetLastError());
            if (!serverPid || serverPid == GetCurrentProcessId() || !inJob || pipeSession != currentSession)
                return reject(ERROR_ACCESS_DENIED);
            detail::Handle server{OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, serverPid)};
            if (!server.value) return reject(GetLastError());
            if (!ProcessIdToSessionId(serverPid, &serverSession)) return reject(GetLastError());
            if (serverSession != currentSession) return reject(ERROR_ACCESS_DENIED);
            const auto checkAlive = [&]() noexcept {
                const DWORD waited = WaitForSingleObject(server.value, 0);
                if (waited == WAIT_TIMEOUT) return true;
                error = waited == WAIT_FAILED ? GetLastError() : ERROR_PROCESS_ABORTED;
                return false;
            };
            if (!checkAlive()) return false;
            std::wstring serverImage(32768, L'\0');
            DWORD length = static_cast<DWORD>(serverImage.size());
            if (!QueryFullProcessImageNameW(server.value, 0, serverImage.data(), &length))
                return reject(GetLastError());
            const auto image = detail::executable();
            if (image.empty()) return reject(GetLastError());
            const int compared = CompareStringOrdinal(serverImage.data(), static_cast<int>(length),
                image.data(), static_cast<int>(image.size()), TRUE);
            if (compared != CSTR_EQUAL) return reject(compared ? ERROR_ACCESS_DENIED : GetLastError());
            DWORD confirmedPid = 0;
            if (!GetNamedPipeServerProcessId(pipe, &confirmedPid)) return reject(GetLastError());
            if (confirmedPid != serverPid) return reject(ERROR_ACCESS_DENIED);
            if (!checkAlive()) return false;
            if (!server.close(error)) return false;
            error = ERROR_SUCCESS;
            return true;
        } catch (...) { return reject(ERROR_NOT_ENOUGH_MEMORY); }
    }
public:
    ChildChannel() { state_->childSide = true; }
    ChildChannel(const ChildChannel&) = delete; ChildChannel& operator=(const ChildChannel&) = delete;
    ~ChildChannel() noexcept {
        if (!detail::stop(*state_, 0, 1500, StopPolicy::cooperativeOnly).fullyDestroyed)
            detail::retain(state_.release());
    }
    static ChildMode classify(int argc, const wchar_t* const* argv) noexcept {
        bool candidate = false;
        if (!argv || argc < 1) return ChildMode::invalid;
        for (int i = 1; i < argc; ++i) {
            if (!argv[i]) return ChildMode::invalid;
            if (std::wstring_view(argv[i]).find(L"--ymcc-decky-binding") == 0) candidate = true;
        }
        if (!candidate) return ChildMode::ordinary;
        if (argc != 4 || !argv[1] || std::wstring_view(argv[1]) != childSwitch)
            return ChildMode::invalid;
        HANDLE pipe = nullptr, cancel = nullptr;
        if (!parseHandle(argv[2], pipe) || !parseHandle(argv[3], cancel) || pipe == cancel)
            return ChildMode::invalid;
        return ChildMode::worker;
    }
    ChildMode openArguments(int argc, const wchar_t* const* argv) noexcept {
        const auto mode = classify(argc, argv);
        if (mode != ChildMode::worker) return mode;
        HANDLE pipe = nullptr, cancel = nullptr;
        if (!parseHandle(argv[2], pipe) || !parseHandle(argv[3], cancel)) return ChildMode::invalid;
        DWORD pipeFlags = 0, cancelFlags = 0, pipeInfo = 0;
        if (!GetHandleInformation(pipe, &pipeFlags) || !GetHandleInformation(cancel, &cancelFlags) ||
            !(pipeFlags & HANDLE_FLAG_INHERIT) || !(cancelFlags & HANDLE_FLAG_INHERIT) ||
            GetFileType(pipe) != FILE_TYPE_PIPE ||
            !GetNamedPipeInfo(pipe, &pipeInfo, nullptr, nullptr, nullptr) ||
            (pipeInfo & PIPE_SERVER_END) || WaitForSingleObject(cancel, 0) == WAIT_FAILED)
            return ChildMode::invalid;
        DWORD sourceError = ERROR_SUCCESS;
        if (!qualifiedSource(pipe, sourceError)) {
            SetLastError(sourceError);
            return ChildMode::invalid; // Reserved worker mode never falls back to ordinary startup.
        }
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->attempted || state_->stopping) return ChildMode::invalid;
        state_->attempted = true;
        state_->pipe.value = pipe; state_->cancel.value = cancel;
        state_->changed.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!SetHandleInformation(pipe, HANDLE_FLAG_INHERIT, 0) ||
            !SetHandleInformation(cancel, HANDLE_FLAG_INHERIT, 0) || !state_->changed.value)
            return ChildMode::invalid;
        state_->started = true;
        return ChildMode::worker;
    }
    // Call before singleton/settings/devices. Invalid mode is handled and must exit.
    // true => caller must run the worker or return exitCode; false => ordinary startup.
    bool tryRunCommandLine(const wchar_t* commandLine, int& exitCode, ChildMode& mode) noexcept {
        int argc = 0;
        auto** argv = CommandLineToArgvW(commandLine, &argc);
        if (!argv) { mode = ChildMode::invalid; exitCode = ERROR_INVALID_PARAMETER; return true; }
        mode = openArguments(argc, argv);
        LocalFree(argv);
        if (mode == ChildMode::invalid) exitCode = ERROR_INVALID_PARAMETER;
        return mode != ChildMode::ordinary;
    }
    Result readFrame(DWORD timeoutMs) { return detail::frame(*state_, false, {}, timeoutMs, 0); }
    Result writeFrame(const std::vector<std::uint8_t>& payload, DWORD timeoutMs) {
        return detail::frame(*state_, true, payload, timeoutMs, 0);
    }
    // Borrowed handle. DuplicateHandle it while the channel is alive; never close this handle.
    HANDLE cancelEvent() const noexcept { return state_->cancel.value; }
    // Borrowed client endpoint for independent source inspection; do not close it.
    HANDLE pipeHandle() const noexcept { return state_->pipe.value; }
    bool cancelled() const noexcept {
        return state_->cancel.value && WaitForSingleObject(state_->cancel.value, 0) == WAIT_OBJECT_0;
    }
    DWORD waitCancelled(DWORD timeoutMs) const noexcept {
        if (!state_->cancel.value || timeoutMs == INFINITE) { SetLastError(ERROR_INVALID_PARAMETER); return WAIT_FAILED; }
        return WaitForSingleObject(state_->cancel.value, timeoutMs);
    }
};
} // namespace ymcc::deckybinding
