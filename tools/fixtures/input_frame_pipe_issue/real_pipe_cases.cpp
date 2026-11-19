// real-pipe-r2 successor: r1 preserved; only delay-60s + limit-derived boundary added.
// Private, manual-only extension of input_frame_pipe_issue. NOT a new test ID.
// Real Windows SDK + the byte-pinned production transport header; no API fakes.
// Preparation only: this source has not been compiled or executed.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <input_host_pipe_transport.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef YMCC_REAL_PIPE_WITH_SLOW_READ_ALLOWANCE
#define YMCC_REAL_PIPE_WITH_SLOW_READ_ALLOWANCE 1
#endif
#if YMCC_REAL_PIPE_WITH_SLOW_READ_ALLOWANCE != 0 && YMCC_REAL_PIPE_WITH_SLOW_READ_ALLOWANCE != 1
#error YMCC_REAL_PIPE_WITH_SLOW_READ_ALLOWANCE must be 0 or 1
#endif

namespace test {
namespace transport = ymcc::inputHost::pipeTransport;
constexpr DWORD kSoftReadMs = 1000, kTotalReadMs = 15000;
constexpr DWORD kLongReadTotalMs = 65000; // r2: actual in-flight Read, not idle.
constexpr LONG kMagic = 0x52504950;
unsigned closeFailures = 0;

void need(bool value, const char* detail) {
    if (!value) throw std::runtime_error(detail);
}
std::string escape(const std::string& value) {
    std::string out;
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c >= 32 && c < 127) out += static_cast<char>(c);
        else out += '?';
    }
    return out;
}
class Handle {
    HANDLE value_ = nullptr;
public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { close(); }
    HANDLE get() const noexcept { return value_; }
    HANDLE releaseOwnership() noexcept { const auto value = value_; value_ = nullptr; return value; }
    bool valid() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void attach(HANDLE value) { need(!valid(), "handle-owner-already-set"); value_ = value; }
    void close() noexcept {
        if (valid() && !CloseHandle(value_)) { ++closeFailures; return; }
        value_ = nullptr;
    }
};
struct Deadline {
    ULONGLONG start = GetTickCount64(), end;
    explicit Deadline(DWORD milliseconds) : end(start + milliseconds) {}
    DWORD left(DWORD maximum = MAXDWORD - 1) const noexcept {
        const auto now = GetTickCount64();
        return now >= end ? 0 : static_cast<DWORD>(std::min<ULONGLONG>(maximum, end - now));
    }
};
struct Shared {
    LONG magic = 0;
    DWORD parentPid = 0, peerOrdinal = 0, delayMs = 0;
    BOOL breakFrame = FALSE;
    ULONGLONG deadline = 0;
    volatile LONG helloReads = 0, helloAcks = 0, prepareReads = 0, prepareAcks = 0;
    volatile LONG frameReads = 0, frameAckAttempts = 0, frameAcks = 0;
    volatile LONG childError = 0, stopped = 0, residualBytes = 0;
    ULONGLONG frameReadTick = 0, ackBeginTick = 0, ackEndTick = 0;
};
LONG count(volatile LONG& value) noexcept { return InterlockedCompareExchange(&value, 0, 0); }
DWORD left(const Shared& state, DWORD maximum) noexcept {
    const auto now = GetTickCount64();
    return now >= state.deadline ? 0 : static_cast<DWORD>(std::min<ULONGLONG>(maximum, state.deadline - now));
}

// Deliberately controlled protocol endpoint, not the managed InputHost binary.
// Field names/persona match the real envelope; exact full-message comparison
// rejects another peer/run, old request, changed persona or duplicate command.
std::string request(DWORD peer, unsigned step) {
    static const char* commands[] = {"HELLO", "PREPARE_TARGET", "SUBMIT_FRAME"};
    need(step < 3, "invalid-protocol-step");
    return std::string("{\"protocolVersion\":1,\"command\":\"") + commands[step] +
        "\",\"requestId\":\"private-" + std::to_string(peer) + "-" + std::to_string(step + 1) +
        "\",\"sequence\":" + std::to_string(step + 1) +
        ",\"runId\":\"private-run-" + std::to_string(peer) + "\",\"epoch\":" + std::to_string(peer) +
        ",\"powerGeneration\":1,\"targetId\":\"ymcc-hidmaestro-gamepad\","
        "\"ownerIdentity\":\"ymcc.inputhost.virtual-target\",\"persona\":\"steamdeck\","
        "\"profileIdentity\":\"steam-deck-composite\",\"startupNonce\":\"private-fixture-" +
        std::to_string(peer) + "\",\"configRevision\":1,\"configHash\":\"" + std::string(64, 'a') +
        "\",\"sampleSequence\":" + std::to_string(step == 2 ? 1 : 0) + "}";
}
std::string ack(DWORD peer, unsigned step) {
    static const char* statuses[] = {"host-hello", "target-prepared", "frame-accepted"};
    return std::string("{\"ok\":true,\"status\":\"") + statuses[step] +
        "\",\"requestId\":\"private-" + std::to_string(peer) + "-" + std::to_string(step + 1) +
        "\",\"runId\":\"private-run-" + std::to_string(peer) + "\",\"epoch\":" +
        std::to_string(peer) + ",\"persona\":\"steamdeck\",\"profileIdentity\":\"steam-deck-composite\"}";
}

// Child-only real overlapped I/O. A still-pending stack OVERLAPPED must never
// unwind: an unreturned cancellation terminates this owned private child with
// a nonzero code instead. Parent Job/exit evidence makes that a failed case.
bool childIo(HANDLE pipe, HANDLE stop, Shared& state, bool writing,
             void* buffer, DWORD size, DWORD& transferred) {
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.valid()) { InterlockedExchange(&state.childError, static_cast<LONG>(GetLastError())); return false; }
    OVERLAPPED ov{}; ov.hEvent = event.get();
    const BOOL issued = writing ? WriteFile(pipe, buffer, size, nullptr, &ov) : ReadFile(pipe, buffer, size, nullptr, &ov);
    DWORD error = issued ? ERROR_SUCCESS : GetLastError();
    if (!issued && error != ERROR_IO_PENDING) { InterlockedExchange(&state.childError, static_cast<LONG>(error)); return false; }
    if (!issued) {
        const HANDLE waits[] = {event.get(), stop};
        const DWORD waited = WaitForMultipleObjects(2, waits, FALSE, left(state, MAXDWORD - 1));
        if (waited != WAIT_OBJECT_0) {
            if (waited == WAIT_OBJECT_0 + 1) InterlockedExchange(&state.stopped, 1);
            else InterlockedExchange(&state.childError, static_cast<LONG>(waited == WAIT_TIMEOUT ? WAIT_TIMEOUT : GetLastError()));
            (void)CancelIoEx(pipe, &ov);
            if (WaitForSingleObject(event.get(), left(state, 1000)) != WAIT_OBJECT_0) {
                InterlockedExchange(&state.childError, ERROR_IO_INCOMPLETE);
                ExitProcess(71); // Do not destroy memory still lent to the kernel.
            }
            DWORD ignored = 0;
            if (!GetOverlappedResult(pipe, &ov, &ignored, FALSE) && GetLastError() == ERROR_IO_INCOMPLETE) ExitProcess(71);
            return false;
        }
    }
    if (!GetOverlappedResult(pipe, &ov, &transferred, FALSE)) {
        error = GetLastError();
        InterlockedExchange(&state.childError, static_cast<LONG>(error));
        if (error == ERROR_IO_INCOMPLETE) ExitProcess(71);
        return false;
    }
    return true;
}
std::uint64_t number(const wchar_t* value) {
    need(value && *value && *value != L'-', "empty-or-negative-number");
    wchar_t* end = nullptr;
    const auto parsed = _wcstoui64(value, &end, 10);
    need(end && *end == 0 && end != value, "invalid-number");
    return parsed;
}
HANDLE inherited(const wchar_t* value) {
    const auto parsed = number(value);
    need(parsed != 0 && parsed <= static_cast<std::uint64_t>(UINTPTR_MAX), "invalid-inherited-handle");
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(parsed));
}
int child(int argc, wchar_t** argv) {
    need(argc == 9, "invalid-private-peer-arguments");
    const DWORD parent = static_cast<DWORD>(number(argv[8]));
    const std::wstring prefix = L"\\\\.\\pipe\\ymcc-input-frame-private-" + std::to_wstring(parent) + L"-";
    need(std::wstring(argv[2]).find(prefix) == 0, "foreign-pipe-name");
    Handle mapping(inherited(argv[3])), seen(inherited(argv[4])), gate(inherited(argv[5])), stop(inherited(argv[6])), done(inherited(argv[7]));
    auto* state = static_cast<Shared*>(MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    need(state && state->magic == kMagic && state->parentPid == parent, "invalid-shared-owner");
    int exitCode = 0;
    {
        Handle pipe(CreateFileW(argv[2], GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
        if (!pipe.valid()) { InterlockedExchange(&state->childError, static_cast<LONG>(GetLastError())); exitCode = 72; }
        else {
            ULONG serverPid = 0;
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (!GetNamedPipeServerProcessId(pipe.get(), &serverPid) || serverPid != parent ||
                !SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr)) {
                InterlockedExchange(&state->childError, ERROR_ACCESS_DENIED); exitCode = 73;
            } else for (unsigned step = 0; step < 3; ++step) {
                char input[4096]{}; DWORD bytes = 0;
                if (!childIo(pipe.get(), stop.get(), *state, false, input, static_cast<DWORD>(sizeof(input)), bytes)) {
                    if (!count(state->stopped)) exitCode = 74;
                    break;
                }
                if (std::string(input, bytes) != request(state->peerOrdinal, step)) {
                    InterlockedExchange(&state->childError, ERROR_INVALID_DATA); exitCode = 75; break;
                }
                if (step == 0) InterlockedIncrement(&state->helloReads);
                if (step == 1) InterlockedIncrement(&state->prepareReads);
                if (step == 2) {
                    state->frameReadTick = GetTickCount64();
                    InterlockedIncrement(&state->frameReads);
                    if (!SetEvent(seen.get())) { exitCode = 76; break; }
                    if (state->breakFrame) break; // Actual CloseHandle(pipe), no ACK.
                    const HANDLE waits[] = {stop.get(), gate.get()};
                    const DWORD gated = WaitForMultipleObjects(2, waits, FALSE, left(*state, MAXDWORD - 1));
                    if (gated == WAIT_OBJECT_0) { InterlockedExchange(&state->stopped, 1); break; }
                    if (gated != WAIT_OBJECT_0 + 1) { InterlockedExchange(&state->childError, WAIT_TIMEOUT); exitCode = 77; break; }
                    if (state->delayMs) {
                        const DWORD delayed = WaitForSingleObject(stop.get(), std::min(state->delayMs, left(*state, state->delayMs)));
                        if (delayed == WAIT_OBJECT_0) { InterlockedExchange(&state->stopped, 1); break; }
                        if (delayed != WAIT_TIMEOUT) { exitCode = 78; break; }
                    }
                    state->ackBeginTick = GetTickCount64();
                    InterlockedIncrement(&state->frameAckAttempts);
                }
                auto response = ack(state->peerOrdinal, step);
                if (!childIo(pipe.get(), stop.get(), *state, true, response.data(), static_cast<DWORD>(response.size()), bytes) || bytes != response.size()) {
                    if (!count(state->stopped)) exitCode = 79;
                    break;
                }
                if (step == 0) InterlockedIncrement(&state->helloAcks);
                if (step == 1) InterlockedIncrement(&state->prepareAcks);
                if (step == 2) {
                    state->ackEndTick = GetTickCount64();
                    InterlockedIncrement(&state->frameAcks);
                    // Do not close a pipe containing an ACK that the parent
                    // has not yet consumed. This is a control event, no second
                    // command/write/ACK and no FlushFileBuffers deadlock.
                    const HANDLE finishWaits[] = {stop.get(), done.get()};
                    const DWORD finished = WaitForMultipleObjects(2, finishWaits, FALSE, left(*state, MAXDWORD - 1));
                    if (finished == WAIT_OBJECT_0) { InterlockedExchange(&state->stopped, 1); break; }
                    if (finished != WAIT_OBJECT_0 + 1) { exitCode = 84; break; }
                    DWORD available = 0;
                    if (!PeekNamedPipe(pipe.get(), nullptr, 0, nullptr, &available, nullptr)) { exitCode = 80; break; }
                    InterlockedExchange(&state->residualBytes, static_cast<LONG>(available));
                    if (available) { InterlockedExchange(&state->childError, ERROR_INVALID_DATA); exitCode = 81; }
                }
            }
        }
    }
    if (!UnmapViewOfFile(state)) { ++closeFailures; exitCode = 82; }
    mapping.close(); seen.close(); gate.close(); stop.close(); done.close();
    return closeFailures ? 83 : exitCode;
}

std::wstring quote(const std::wstring& value) {
    std::wstring out = L"\""; unsigned slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') { out.append(slashes * 2 + 1, L'\\'); out += c; }
        else { out.append(slashes, L'\\'); out += c; }
        slashes = 0;
    }
    out.append(slashes * 2, L'\\'); return out + L"\"";
}
std::wstring handleText(HANDLE value) { return std::to_wstring(reinterpret_cast<std::uintptr_t>(value)); }
struct Probe {
    HANDLE child = nullptr;
    DWORD verdict = ERROR_SUCCESS;
    std::atomic<unsigned> calls{0};
    static DWORD current(void* context) noexcept {
        auto& self = *static_cast<Probe*>(context);
        self.calls.fetch_add(1, std::memory_order_relaxed);
        if (self.verdict != ERROR_SUCCESS) return self.verdict;
        const DWORD alive = WaitForSingleObject(self.child, 0);
        return alive == WAIT_TIMEOUT ? ERROR_SUCCESS : alive == WAIT_OBJECT_0 ? ERROR_BROKEN_PIPE : ERROR_GEN_FAILURE;
    }
};
bool graceEntered(const transport::Result& result) noexcept {
#if YMCC_REAL_PIPE_WITH_SLOW_READ_ALLOWANCE
    return result.schedulingGraceEntered;
#else
    (void)result; return false;
#endif
}
struct Observation {
    transport::Result result{};
    ULONGLONG elapsed = 0;
    unsigned packets = 0;
};

class Peer {
    transport::Runtime& runtime_;
    Deadline& deadline_;
    Handle mapping_, seen_, gate_, stop_, done_, process_, thread_, job_, port_;
    Shared* shared_ = nullptr;
    HANDLE server_ = nullptr; // Adopted by Runtime, never closed by this class.
    bool created_ = false, assigned_ = false, resumed_ = false, settled_ = false, settledGood_ = false;
    DWORD pid_ = 0;
public:
    Probe probe;
    unsigned frameWrites = 0, frameReads = 0;
    LONG finalFrameReads = 0, finalFrameAcks = 0, finalAckAttempts = 0;
    Peer(transport::Runtime& runtime, Deadline& deadline) : runtime_(runtime), deadline_(deadline) {}
    ~Peer() { if (!settled_) (void)settle(); }
    void start(DWORD ordinal, DWORD delay, bool broken) {
        SECURITY_ATTRIBUTES inherit{static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), nullptr, TRUE};
        mapping_.attach(CreateFileMappingW(INVALID_HANDLE_VALUE, &inherit, PAGE_READWRITE, 0, static_cast<DWORD>(sizeof(Shared)), nullptr));
        seen_.attach(CreateEventW(&inherit, TRUE, FALSE, nullptr));
        gate_.attach(CreateEventW(&inherit, TRUE, FALSE, nullptr));
        stop_.attach(CreateEventW(&inherit, TRUE, FALSE, nullptr));
        done_.attach(CreateEventW(&inherit, TRUE, FALSE, nullptr));
        need(mapping_.valid() && seen_.valid() && gate_.valid() && stop_.valid() && done_.valid(), "private-control-handles-create-failed");
        shared_ = static_cast<Shared*>(MapViewOfFile(mapping_.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        need(shared_ != nullptr, "private-map-view-failed");
        // Mapped storage is initialized as a Shared object before publication.
        new (shared_) Shared();
        shared_->magic = kMagic; shared_->parentPid = GetCurrentProcessId(); shared_->peerOrdinal = ordinal;
        shared_->delayMs = delay; shared_->breakFrame = broken; shared_->deadline = deadline_.end;
        FILETIME creation{}, exit{}, kernel{}, user{};
        need(GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) != FALSE, "parent-start-key-failed");
        const auto birth = (static_cast<ULONGLONG>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
        const std::wstring name = L"\\\\.\\pipe\\ymcc-input-frame-private-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(birth) + L"-" + std::to_wstring(deadline_.start) + L"-" + std::to_wstring(ordinal);
        DWORD error = 0;
        need(runtime_.canCreate(name, error), "private-pipe-name-retained");
        Handle raw(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, nullptr));
        need(raw.valid(), "private-real-pipe-create-failed");
        port_.attach(CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1));
        need(port_.valid() && CreateIoCompletionPort(raw.get(), port_.get(), 1, 1) == port_.get(), "real-iocp-association-failed");
        // Transfer without a second CloseHandle, including an adopt failure
        // that already took ownership. Handle relinquishment is explicit below.
        HANDLE owned = raw.get();
        bool taken = false;
        const bool adopted = runtime_.adopt(owned, name, error, &taken);
        if (taken) raw.releaseOwnership();
        need(adopted && taken, "real-transport-adopt-failed");
        server_ = owned;
        job_.attach(CreateJobObjectW(nullptr, nullptr));
        need(job_.valid(), "private-job-create-failed");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        need(SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limits, static_cast<DWORD>(sizeof(limits))) != FALSE, "private-job-limits-failed");
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        need(length && length < std::size(executable), "self-image-path-failed");
        std::wstring command = quote(executable) + L" --private-peer " + quote(name) + L" " + handleText(mapping_.get()) +
            L" " + handleText(seen_.get()) + L" " + handleText(gate_.get()) + L" " + handleText(stop_.get()) +
            L" " + handleText(done_.get()) +
            L" " + std::to_wstring(GetCurrentProcessId());
        SIZE_T attributesSize = 0;
        (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &attributesSize);
        need(attributesSize != 0, "attribute-size-failed");
        std::vector<unsigned char> storage(attributesSize);
        auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        need(InitializeProcThreadAttributeList(attributes, 1, 0, &attributesSize) != FALSE, "attribute-init-failed");
        HANDLE inheritedHandles[] = {mapping_.get(), seen_.get(), gate_.get(), stop_.get(), done_.get()};
        const BOOL updated = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inheritedHandles, sizeof(inheritedHandles), nullptr, nullptr);
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = static_cast<DWORD>(sizeof(startup)); startup.lpAttributeList = attributes;
        PROCESS_INFORMATION process{};
        const BOOL spawned = updated && CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &process);
        const DWORD spawnError = spawned ? ERROR_SUCCESS : GetLastError();
        DeleteProcThreadAttributeList(attributes);
        need(spawned != FALSE, ("private-child-create-failed:" + std::to_string(spawnError)).c_str());
        process_.attach(process.hProcess); thread_.attach(process.hThread); pid_ = process.dwProcessId; created_ = true;
        assigned_ = AssignProcessToJobObject(job_.get(), process_.get()) != FALSE;
        need(assigned_, "private-child-job-assign-failed");
        BOOL inJob = FALSE;
        need(IsProcessInJob(process_.get(), job_.get(), &inJob) && inJob, "private-child-job-membership-unproven");
        FILETIME childCreation{}, childExit{}, childKernel{}, childUser{};
        need(GetProcessTimes(process_.get(), &childCreation, &childExit, &childKernel, &childUser) != FALSE, "child-start-key-failed");
        const auto childBirth = (static_cast<ULONGLONG>(childCreation.dwHighDateTime) << 32) | childCreation.dwLowDateTime;
        resumed_ = ResumeThread(thread_.get()) == 1;
        need(resumed_, "private-child-first-resume-failed");
        std::printf("{\"kind\":\"owned-child-start\",\"parentPid\":%lu,\"parentStartKey\":\"%llu\",\"childPid\":%lu,\"childStartKey\":\"%llu\","
            "\"assignedBeforeResume\":true,\"verifiedJobMembership\":true,\"handleListCount\":5}\n",
            GetCurrentProcessId(), static_cast<unsigned long long>(birth), pid_, static_cast<unsigned long long>(childBirth));
        auto connect = io("connect", transport::Kind::Connect, nullptr, 0, 3000, 0);
        need(connect.result.ok, "private-real-connect-failed");
        ULONG client = 0;
        need(GetNamedPipeClientProcessId(server_, &client) && client == pid_, "private-peer-pid-mismatch");
    }
    Observation io(const char* label, transport::Kind kind, void* data, DWORD size,
                   DWORD soft, DWORD total, transport::Cancellation cancel = {}) {
        const auto before = runtime_.snapshot();
        need(before.available, "transport-snapshot-unavailable-before");
        const auto begin = GetTickCount64();
        transport::Result result;
#if YMCC_REAL_PIPE_WITH_SLOW_READ_ALLOWANCE
        const transport::SlowReadAllowance allowance{total, total ? &Probe::current : nullptr, total ? &probe : nullptr};
        result = runtime_.run(server_, kind, data, size, soft, 50, nullptr, nullptr, std::move(cancel), allowance);
#else
        (void)total;
        result = runtime_.run(server_, kind, data, size, soft, 50, nullptr, nullptr, std::move(cancel));
#endif
        const auto elapsed = GetTickCount64() - begin;
        const auto after = runtime_.snapshot();
        need(after.available && after.admitted == before.admitted + (result.admitted ? 1 : 0), "operation-admission-count-mismatch");
        // Completion packets are from the REAL file object, including the
        // Runtime's DuplicateHandle. No skip-completion flags are installed.
        unsigned packets = 0; DWORD packetBytes = 0, packetError = 0;
        const bool packetExpected = result.admitted && (result.initialError == ERROR_IO_PENDING || result.apiReturnedTrue);
        for (;;) {
            ULONG_PTR key = 0; OVERLAPPED* completed = nullptr; DWORD bytes = 0;
            const BOOL ok = GetQueuedCompletionStatus(port_.get(), &bytes, &key, &completed,
                packets == 0 && packetExpected ? deadline_.left(1000) : 0);
            const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
            if (!completed) { need(!ok && error == WAIT_TIMEOUT, "unexpected-iocp-wait-result"); break; }
            need(key == 1, "foreign-iocp-key");
            ++packets; packetBytes = bytes; packetError = error;
            std::printf("{\"kind\":\"real-iocp-completion\",\"op\":\"%s\",\"pointer\":\"%llu\",\"bytes\":%lu,\"error\":%lu}\n",
                label, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(completed)), bytes, error);
        }
        std::printf("{\"kind\":\"transport-operation\",\"op\":\"%s\",\"operation\":%llu,\"lease\":%llu,\"ok\":%s,\"error\":%lu,"
            "\"softMs\":%lu,\"totalMs\":%lu,\"elapsedMs\":%llu,\"schedulingGraceEntered\":%s,\"currentCalls\":%u,"
            "\"admitted\":%s,\"terminal\":%s,\"retired\":%s,\"cancelAttempted\":%s,\"scopeRevoked\":%s,"
            "\"initialError\":%lu,\"transferred\":%lu,\"iocpPackets\":%u,\"packetBytes\":%lu,\"packetError\":%lu}\n",
            label, static_cast<unsigned long long>(result.operation), static_cast<unsigned long long>(result.lease), result.ok ? "true" : "false", result.error,
            soft, total, static_cast<unsigned long long>(elapsed), graceEntered(result) ? "true" : "false", probe.calls.load(),
            result.admitted ? "true" : "false", result.terminal ? "true" : "false", result.retired ? "true" : "false",
            result.cancelAttempted ? "true" : "false", result.scopeRevoked ? "true" : "false", result.initialError, result.transferred, packets, packetBytes, packetError);
        need(packets == (packetExpected ? 1u : 0u), "real-io-completion-count-not-exactly-one-or-zero");
        if (kind == transport::Kind::Connect) probe.child = process_.get();
        return {result, elapsed, packets};
    }
    void exchange(unsigned step) {
        auto body = request(shared_->peerOrdinal, step);
        auto sent = io(step == 0 ? "hello-write" : "prepare-write", transport::Kind::Write, body.data(), static_cast<DWORD>(body.size()), 1000, 0);
        need(sent.result.ok && sent.result.transferred == body.size(), "protocol-write-failed");
        char input[4096]{};
        auto received = io(step == 0 ? "hello-read" : "prepare-read", transport::Kind::Read, input, static_cast<DWORD>(sizeof(input)), 1000, 0);
        need(received.result.ok && received.result.byteCountKnown && std::string(input, received.result.transferred) == ack(shared_->peerOrdinal, step), "protocol-ack-mismatch");
    }
    void frame() {
        auto body = request(shared_->peerOrdinal, 2);
        auto sent = io("frame-write", transport::Kind::Write, body.data(), static_cast<DWORD>(body.size()), 1000, 0);
        ++frameWrites;
        need(sent.result.ok && sent.result.transferred == body.size(), "frame-write-failed");
        need(WaitForSingleObject(seen_.get(), deadline_.left(2000)) == WAIT_OBJECT_0 && count(shared_->frameReads) == 1, "child-frame-not-read-exactly-once");
    }
    Observation readFrame(char* buffer, DWORD size, DWORD total, transport::Cancellation cancel = {}) {
        need(SetEvent(gate_.get()) != FALSE, "private-ack-gate-failed");
        ++frameReads;
        return io("frame-read", transport::Kind::Read, buffer, size, kSoftReadMs, total, std::move(cancel));
    }
    void expectAck(const Observation& observed, const char* buffer) {
        need(observed.result.ok && observed.result.byteCountKnown && observed.result.terminal, "frame-ack-did-not-complete");
        need(std::string(buffer, observed.result.transferred) == ack(shared_->peerOrdinal, 2), "frame-ack-full-binding-mismatch");
        need(observed.packets == 1 && frameWrites == 1 && frameReads == 1, "frame-write-read-ack-not-one-each");
        need(SetEvent(done_.get()) != FALSE, "private-ack-consumed-event-failed");
        need(WaitForSingleObject(process_.get(), deadline_.left(2000)) == WAIT_OBJECT_0, "successful-private-child-did-not-exit");
        need(count(shared_->frameReads) == 1 && count(shared_->frameAckAttempts) == 1 && count(shared_->frameAcks) == 1 &&
            count(shared_->childError) == 0 && count(shared_->residualBytes) == 0, "child-did-not-observe-one-request-and-one-ack");
    }
    void idleMinute() {
        const auto before = runtime_.snapshot();
        const auto start = GetTickCount64();
        // No operation is pending, no signal is sent, no liveness/axis poller.
        need(WaitForSingleObject(process_.get(), deadline_.left(60000)) == WAIT_TIMEOUT, "private-peer-exited-during-idle");
        const auto elapsed = GetTickCount64() - start;
        const auto after = runtime_.snapshot();
        need(elapsed >= 60000 && before.available && after.available && before.admitted == after.admitted &&
            after.activeLeases == 1 && after.pending == 0 && probe.calls.load() == 0 && count(shared_->helloReads) == 0, "idle-created-io-or-retired-the-lease");
        std::printf("{\"kind\":\"idle-observation\",\"elapsedMs\":%llu,\"pending\":%zu,\"newOperations\":0,\"currentCalls\":0}\n",
            static_cast<unsigned long long>(elapsed), after.pending);
    }
    bool settle() noexcept {
        if (settled_) return settledGood_;
        settled_ = true;
        bool good = true, forced = false, stopSignalled = false;
        DWORD exitCode = STILL_ACTIVE, active = MAXDWORD;
        if (process_.valid()) {
            if (WaitForSingleObject(process_.get(), 0) == WAIT_TIMEOUT && stop_.valid()) {
                stopSignalled = SetEvent(stop_.get()) != FALSE;
                if (!stopSignalled) good = false;
            }
            if (WaitForSingleObject(process_.get(), deadline_.left(2500)) != WAIT_OBJECT_0) {
                forced = true; good = false;
                if (assigned_) (void)TerminateJobObject(job_.get(), 91);
                else (void)TerminateProcess(process_.get(), 91); // Only this still-owned suspended child.
                (void)WaitForSingleObject(process_.get(), deadline_.left(1500));
            }
            if (!GetExitCodeProcess(process_.get(), &exitCode) || exitCode != 0) good = false;
        }
        if (job_.valid()) {
            const auto observeEnd = GetTickCount64() + deadline_.left(1000);
            do {
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
                if (!QueryInformationJobObject(job_.get(), JobObjectBasicAccountingInformation, &info, static_cast<DWORD>(sizeof(info)), nullptr)) { good = false; break; }
                active = info.ActiveProcesses;
                if (active == 0 || GetTickCount64() >= observeEnd) break;
                Sleep(1); // Bounded retirement observation, never a health poller.
            } while (deadline_.left());
            if (active != 0 || !created_ || !assigned_ || !resumed_) good = false;
        } else good = false;
        if (server_) { runtime_.close(server_); server_ = nullptr; }
        auto snapshot = runtime_.snapshot();
        while ((!snapshot.available || !snapshot.stopped) && deadline_.left()) {
            runtime_.maintenance(); snapshot = runtime_.snapshot();
            if (!snapshot.available || !snapshot.stopped) Sleep(1);
        }
        if (!snapshot.available || !snapshot.stopped || snapshot.closeFailures != 0) good = false;
        if (shared_) {
            finalFrameReads = count(shared_->frameReads); finalFrameAcks = count(shared_->frameAcks); finalAckAttempts = count(shared_->frameAckAttempts);
            if (count(shared_->childError) != 0 || count(shared_->residualBytes) != 0) good = false;
            std::printf("{\"kind\":\"private-peer\",\"pid\":%lu,\"peerOrdinal\":%lu,\"helloReads\":%ld,\"helloAcks\":%ld,\"prepareReads\":%ld,\"prepareAcks\":%ld,"
                "\"frameReads\":%ld,\"frameAckAttempts\":%ld,\"frameAcks\":%ld,\"childError\":%ld,\"residualBytes\":%ld,\"stoppedByControlEvent\":%ld,"
                "\"frameReadTick\":%llu,\"ackBeginTick\":%llu,\"ackEndTick\":%llu}\n",
                pid_, shared_->peerOrdinal, count(shared_->helloReads), count(shared_->helloAcks), count(shared_->prepareReads), count(shared_->prepareAcks),
                finalFrameReads, finalAckAttempts, finalFrameAcks, count(shared_->childError), count(shared_->residualBytes), count(shared_->stopped),
                static_cast<unsigned long long>(shared_->frameReadTick), static_cast<unsigned long long>(shared_->ackBeginTick), static_cast<unsigned long long>(shared_->ackEndTick));
        }
        std::printf("{\"kind\":\"owned-resource-closing\",\"childPid\":%lu,\"created\":%s,\"assigned\":%s,\"resumed\":%s,\"childExitCode\":%lu,"
            "\"stopSignalled\":%s,\"forcedTermination\":%s,\"jobActiveBeforeClose\":%lu,\"runtimeAvailable\":%s,\"runtimeStopped\":%s,"
            "\"pending\":%zu,\"operationSlots\":%zu,\"cachedOperations\":%zu,\"originals\":%zu,\"duplicates\":%zu,\"events\":%zu,\"closeDebt\":%zu,"
            "\"activeLeases\":%zu,\"retainedLeases\":%zu,\"payloadBytes\":%zu,\"residentPayloadBytes\":%zu}\n",
            pid_, created_ ? "true" : "false", assigned_ ? "true" : "false", resumed_ ? "true" : "false", exitCode,
            stopSignalled ? "true" : "false", forced ? "true" : "false", active, snapshot.available ? "true" : "false", snapshot.stopped ? "true" : "false",
            snapshot.pending, snapshot.operationSlots, snapshot.cachedOperations, snapshot.ownedOriginals, snapshot.ownedDuplicates, snapshot.ownedEvents, snapshot.failedCloseDebt,
            snapshot.activeLeases, snapshot.retainedLeases, snapshot.payloadBytes, snapshot.residentPayloadBytes);
        if (shared_) { if (!UnmapViewOfFile(shared_)) { ++closeFailures; good = false; } shared_ = nullptr; }
        thread_.close(); process_.close(); job_.close(); port_.close(); seen_.close(); gate_.close(); stop_.close(); done_.close(); mapping_.close();
        settledGood_ = good && closeFailures == 0;
        return settledGood_;
    }
};

struct EventOwner {
    Handle event;
    explicit EventOwner(bool signalled) : event(CreateEventW(nullptr, TRUE, signalled ? TRUE : FALSE, nullptr)) {}
};
class CancelDuring {
    Handle stopped_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread worker_;
public:
    std::atomic<DWORD> error{ERROR_SUCCESS};
    explicit CancelDuring(std::shared_ptr<EventOwner> owner) {
        need(stopped_.valid(), "cancel-worker-stop-create-failed");
        worker_ = std::thread([this, owner = std::move(owner)] {
            const DWORD waited = WaitForSingleObject(stopped_.get(), 1500);
            if (waited == WAIT_TIMEOUT) {
                if (!SetEvent(owner->event.get())) error.store(GetLastError());
            } else if (waited != WAIT_OBJECT_0) error.store(GetLastError());
        });
    }
    ~CancelDuring() {
        if (!SetEvent(stopped_.get())) ++closeFailures;
        if (worker_.joinable()) worker_.join();
    }
};
int parent(const std::wstring& selected) {
    const bool idle = selected == L"idle-60s", five = selected == L"delay-5s", ten = selected == L"delay-10s";
    const bool sixty = selected == L"delay-60s";
    const bool bounded = selected == L"bounded-timeout", excessive = selected == L"allowance-over-limit";
    const bool revoked = selected == L"allowance-revoked", cancelled = selected == L"cancel-before-read";
    const bool cancelDuring = selected == L"cancel-during-grace";
    const bool broken = selected == L"broken-reconnect";
    need(idle || five || ten || sixty || bounded || excessive || revoked || cancelled || cancelDuring || broken, "unknown-or-empty-case");
#if YMCC_REAL_PIPE_WITH_SLOW_READ_ALLOWANCE
    if (sixty) need(transport::kMaxSchedulingReadMs == kLongReadTotalMs, "delay-60s-requires-candidate-max-65000-before-any-child");
#endif
    std::string label;
    for (wchar_t c : selected) label.push_back(static_cast<char>(c)); // Validated ASCII selector above.
    std::printf("{\"kind\":\"selection\",\"canonicalEntry\":\"input_frame_pipe_issue\",\"selectedCase\":\"%s\",\"selectedCount\":1,"
        "\"allowanceMacro\":%d,\"scope\":\"real-Win32-private-peer-transport-only\",\"productionRuntimeSelfHealProven\":false}\n",
        label.c_str(), YMCC_REAL_PIPE_WITH_SLOW_READ_ALLOWANCE);
    Deadline deadline(idle ? 80000 : sixty ? 90000 : 30000);
    auto& runtime = transport::processRuntime(); // Production's process-lifetime owner, not a stack imitation.
    std::vector<std::unique_ptr<Peer>> peers;
    std::string failure;
    try {
        peers.push_back(std::make_unique<Peer>(runtime, deadline));
        auto& peer = *peers.back();
        const DWORD delay = five ? 5000 : ten ? 10000 : sixty ? 60000 : (bounded || excessive || revoked || cancelled || cancelDuring) ? 5000 : 0;
        peer.start(1, delay, broken);
        if (idle) peer.idleMinute();
        peer.exchange(0); peer.exchange(1); peer.frame();
        char input[4096]{};
        std::shared_ptr<EventOwner> cancelOwner;
        std::unique_ptr<CancelDuring> cancelWorker;
        transport::Cancellation cancellation;
        if (cancelled || cancelDuring) {
            cancelOwner = std::make_shared<EventOwner>(cancelled);
            need(cancelOwner->event.valid(), "cancel-event-create-failed");
            cancellation.event = cancelOwner->event.get(); cancellation.lifetime = cancelOwner;
            if (cancelDuring) cancelWorker = std::make_unique<CancelDuring>(cancelOwner);
        }
        if (revoked) peer.probe.verdict = ERROR_OPERATION_ABORTED;
        #if YMCC_REAL_PIPE_WITH_SLOW_READ_ALLOWANCE
        static_assert(transport::kMaxSchedulingReadMs < MAXDWORD, "upper-bound-plus-one-must-not-overflow");
        const DWORD overLimitMs = transport::kMaxSchedulingReadMs + DWORD{1};
#else
        const DWORD overLimitMs = 0; // Old header has no limit symbol; allowance is not passed at all.
#endif
        const DWORD total = bounded ? 2000 : excessive ? overLimitMs : sixty ? kLongReadTotalMs : kTotalReadMs;
        const auto observed = peer.readFrame(input, static_cast<DWORD>(sizeof(input)), total, std::move(cancellation));
        if (broken) {
            need(!observed.result.ok && observed.result.error == ERROR_BROKEN_PIPE && !graceEntered(observed.result), "real-close-did-not-return-broken-pipe");
            need(peer.settle(), "broken-peer-resources-not-returned");
            need(peer.finalFrameReads == 1 && peer.finalFrameAcks == 0 && peer.finalAckAttempts == 0, "broken-peer-wrote-an-ack");
            // Explicit fixture reconstruction after old resources are proven
            // returned. This is NOT the production runtime's recovery policy.
            peers.push_back(std::make_unique<Peer>(runtime, deadline));
            auto& successor = *peers.back(); successor.start(2, 0, false);
            successor.exchange(0); successor.exchange(1); successor.frame();
            char replacement[4096]{};
            const auto recovered = successor.readFrame(replacement, static_cast<DWORD>(sizeof(replacement)), kTotalReadMs);
            successor.expectAck(recovered, replacement);
        } else if (cancelled) {
            need(!observed.result.ok && observed.result.error == ERROR_OPERATION_ABORTED && observed.result.contextRejected &&
                observed.result.scopeRevoked && !observed.result.admitted && observed.packets == 0 && peer.probe.calls.load() == 0,
                "pre-signaled-public-cancel-issued-or-extended-read");
        } else if (excessive) {
            // The actual candidate rejects kMaxSchedulingReadMs + 1 BEFORE issue;
            // it does not silently fall back to the old timeout.
            need(!observed.result.ok && observed.result.error == ERROR_INVALID_PARAMETER && !observed.result.admitted &&
                observed.packets == 0 && !graceEntered(observed.result) && peer.probe.calls.load() == 0,
                "over-max-allowance-was-not-rejected-before-issue");
        } else if (revoked || cancelDuring) {
            need(!observed.result.ok && observed.result.error == ERROR_OPERATION_ABORTED && observed.result.cancelAttempted &&
                graceEntered(observed.result) && peer.probe.calls.load() > 0 && observed.elapsed >= (cancelDuring ? 1400u : 900u) && observed.elapsed <= 3000,
                "revocation-did-not-stop-the-same-read-without-reissue");
            if (cancelDuring) need(observed.result.scopeRevoked && cancelWorker->error.load() == ERROR_SUCCESS,
                "public-cancel-not-observed-in-scheduling-grace");
        } else if (bounded) {
            need(!observed.result.ok && observed.result.error == WAIT_TIMEOUT && observed.result.cancelAttempted,
                "overdue-read-did-not-use-original-timeout-cancel-flow");
            need(graceEntered(observed.result) && peer.probe.calls.load() > 0 && observed.elapsed >= 1900 && observed.elapsed <= 3500,
                "short-total-budget-not-bounded-or-grace-not-entered");
        } else {
            peer.expectAck(observed, input);
            if (five || ten || sixty) need(graceEntered(observed.result) && peer.probe.calls.load() > 0 &&
                observed.result.firstWait == WAIT_TIMEOUT && observed.elapsed >= delay - 250 &&
                observed.elapsed < (sixty ? kLongReadTotalMs : kTotalReadMs),
                "delayed-ack-not-one-read-across-soft-timeout");
        }
    } catch (const std::exception& error) { failure = error.what(); }
    bool cleaned = true;
    for (auto& peer : peers) if (!peer->settle()) cleaned = false;
    if (failure.empty() && (bounded || excessive || revoked || cancelled || cancelDuring) &&
        (peers.size() != 1 || peers[0]->frameWrites != 1 || peers[0]->frameReads != 1 ||
         peers[0]->finalFrameReads != 1 || peers[0]->finalFrameAcks != 0 || peers[0]->finalAckAttempts != 0))
        failure = "negative-case-replayed-request-or-wrote-late-ack";
    runtime.sealAndReap(); runtime.maintenance();
    const auto closing = runtime.snapshot();
    cleaned = cleaned && closing.available && closing.stopped && closing.closeFailures == 0 && closeFailures == 0;
    if (failure.empty() && !cleaned) failure = "owned-resource-return-not-proven";
    const bool passed = failure.empty();
    std::printf("{\"kind\":\"case-result\",\"selectedCase\":\"%s\",\"executedCases\":[\"%s\"],\"executedCount\":1,\"passed\":%s,"
        "\"detail\":\"%s\",\"elapsedMs\":%llu,\"ownedResourcesReturned\":%s,\"handleCloseFailures\":%u,"
        "\"productionRuntimeSelfHealProven\":false,\"physicalCpuPressureExecuted\":false}\n",
        label.c_str(), label.c_str(), passed ? "true" : "false", escape(failure).c_str(), static_cast<unsigned long long>(GetTickCount64() - deadline.start),
        cleaned ? "true" : "false", closeFailures);
    std::fflush(stdout);
    return passed ? 0 : 1;
}
} // namespace test

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc > 1 && std::wstring(argv[1]) == L"--private-peer") return test::child(argc, argv);
        test::need(argc == 3 && std::wstring(argv[1]) == L"--case" && argv[2][0] != 0, "requires-one-explicit-case-no-default-no-all");
        return test::parent(argv[2]);
    } catch (const std::exception& error) {
        std::printf("{\"kind\":\"selection-or-preparation-error\",\"executedCount\":0,\"detail\":\"%s\"}\n", test::escape(error.what()).c_str());
        return 64;
    }
}
