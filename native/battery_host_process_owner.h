#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

// Pending battery integration: this owns the ENTIRE existing BatteryHost process,
// including any lazy display and charge domains. A single request timeout is NOT
// authority to call stop(). The main lifecycle owner must decide whether revoking
// the whole shared Host is permitted. No pipe/provider/journal/hardware calls here.
// Process/Job termination proves software resource destruction only. It never
// confirms charging restoration, and this component never removes a journal.
namespace ymcc::batteryhost {
inline constexpr DWORD maxStopBudgetMs = 15000;
enum class Code { ok, stopped, busy, invalidArgument, identityMismatch, systemError, timeout, cleanupIncomplete };
struct Snapshot {
    std::uint64_t generation = 0, launchedGeneration = 0, processBirth = 0;
    DWORD processId = 0, exitCode = STILL_ACTIVE, activeJobProcesses = 0, jobLimitFlags = 0, cleanupError = 0;
    unsigned ownedHandles = 0, closeFailures = 0;
    bool attempted = false, resumed = false, admissionSealed = false, jobAssigned = false;
    // resourcesDestroyed: exact owned handles closed, root signaled, kernel Job
    // ActiveProcesses==0. A separately held descendant process observer may signal
    // slightly later during kernel teardown. This is no hardware-restoration proof.
    bool processExited = true, jobZeroObserved = true, resourcesDestroyed = true, hardwareRestoreConfirmed = false;
    bool snapshotBusy = false, terminationAttempted = false;
    std::wstring image;
};
struct Result {
    Code code = Code::ok;
    DWORD error = ERROR_SUCCESS;
    bool transferred = false; // false => PROCESS_INFORMATION remains exclusively caller-owned.
    Snapshot state;
    explicit operator bool() const noexcept { return code == Code::ok; }
};

// Typed syscall boundary. Tests may substitute explicit failure receipts; the
// default implementation always calls the real kernel on exact owned handles.
struct Win32Calls {
    HANDLE createJob() noexcept { return CreateJobObjectW(nullptr, nullptr); }
    HANDLE createPort() noexcept { return CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1); }
    BOOL setKillOnClose(HANDLE job) noexcept {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        return SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    BOOL jobLimits(HANDLE job, DWORD& flags) noexcept {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        if (!QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr)) return FALSE;
        flags = limits.BasicLimitInformation.LimitFlags; return TRUE;
    }
    BOOL associatePort(HANDLE job, HANDLE port, void* key) noexcept {
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT info{}; info.CompletionKey = key; info.CompletionPort = port;
        return SetInformationJobObject(job, JobObjectAssociateCompletionPortInformation, &info, sizeof(info));
    }
    BOOL assign(HANDLE job, HANDLE process) noexcept { return AssignProcessToJobObject(job, process); }
    BOOL notInherited(HANDLE handle) noexcept { return SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0); }
    DWORD resume(HANDLE thread) noexcept { return ResumeThread(thread); }
    BOOL terminateJob(HANDLE job) noexcept { return TerminateJobObject(job, ERROR_CANCELLED); }
    BOOL terminateProcess(HANDLE process) noexcept { return TerminateProcess(process, ERROR_CANCELLED); }
    BOOL close(HANDLE handle) noexcept { return CloseHandle(handle); }
    DWORD wait(HANDLE handle, DWORD ms) noexcept { return WaitForSingleObject(handle, ms); }
    BOOL exitCode(HANDLE process, DWORD& code) noexcept { return GetExitCodeProcess(process, &code); }
    BOOL jobActive(HANDLE job, DWORD& count) noexcept {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
        if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr)) return FALSE;
        count = info.ActiveProcesses; return TRUE;
    }
    BOOL completion(HANDLE port, DWORD ms) noexcept {
        DWORD bytes = 0; ULONG_PTR key = 0; LPOVERLAPPED value = nullptr;
        return GetQueuedCompletionStatus(port, &bytes, &key, &value, ms);
    }
};
namespace processOwnerDetail {
inline std::atomic<std::uint64_t> serial{1};
struct Deadline {
    ULONGLONG end;
    explicit Deadline(DWORD ms) noexcept : end(GetTickCount64() + ms) {}
    DWORD left() const noexcept { const auto now = GetTickCount64(); return now >= end ? 0 : static_cast<DWORD>(end - now); }
};
inline bool valid(HANDLE handle) noexcept { return handle && handle != INVALID_HANDLE_VALUE; }
inline std::uint64_t birth(FILETIME value) noexcept { return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime; }
inline bool sameImage(const std::wstring& actual, const std::wstring& expected) noexcept {
    if (actual.size() != expected.size()) return false;
    return CompareStringOrdinal(actual.c_str(), static_cast<int>(actual.size()), expected.c_str(), static_cast<int>(expected.size()), TRUE) == CSTR_EQUAL;
}
template<class Calls> struct State {
    std::timed_mutex mutex;
    Calls calls;
    std::atomic<bool> sealed{false};
    std::atomic<std::uint64_t> generation{serial.fetch_add(2)};
    HANDLE process = nullptr, thread = nullptr, job = nullptr, port = nullptr;
    DWORD pid = 0, exitCode = STILL_ACTIVE, active = 0, limits = 0, error = 0;
    unsigned closeFailures = 0;
    std::uint64_t processBirth = 0, launchedGeneration = 0;
    std::wstring image;
    bool attempted = false, resumed = false, assigned = false, exited = true, jobZero = true, termination = false;
    State* retainedNext = nullptr;
    explicit State(Calls c) : calls(std::move(c)) {}
};
template<class Calls> inline std::mutex retainedMutex;
template<class Calls> inline State<Calls>* retainedHead = nullptr;
template<class Calls> void retain(State<Calls>* state) noexcept {
    std::lock_guard<std::mutex> lock(retainedMutex<Calls>);
    state->retainedNext = retainedHead<Calls>; retainedHead<Calls> = state;
}
template<class Calls> unsigned handles(const State<Calls>& s) noexcept {
    return unsigned(valid(s.process)) + unsigned(valid(s.thread)) + unsigned(valid(s.job)) + unsigned(valid(s.port));
}
template<class Calls> bool destroyed(const State<Calls>& s) noexcept { return !handles(s) && s.exited && s.jobZero; }
template<class Calls> bool close(State<Calls>& s, HANDLE& handle) noexcept {
    if (!valid(handle)) return true;
    if (!s.calls.close(handle)) { s.error = GetLastError(); ++s.closeFailures; return false; }
    handle = nullptr; return true;
}
template<class Calls> bool refresh(State<Calls>& s) noexcept {
    if (valid(s.process) && !s.exited) {
        const auto wait = s.calls.wait(s.process, 0);
        if (wait == WAIT_FAILED) { s.error = GetLastError(); return false; }
        if (wait == WAIT_OBJECT_0) {
            s.exited = true;
            if (!s.calls.exitCode(s.process, s.exitCode)) { s.error = GetLastError(); return false; }
        }
    }
    if (valid(s.job) && s.assigned) {
        if (!s.calls.jobActive(s.job, s.active)) { s.error = GetLastError(); s.jobZero = false; return false; }
        s.jobZero = s.active == 0;
    }
    return true;
}
template<class Calls> void closeTerminal(State<Calls>& s) noexcept {
    if (!s.exited || !s.jobZero) return;
    close(s, s.thread); close(s, s.process); close(s, s.job); close(s, s.port);
}
template<class Calls> Snapshot snapshot(const State<Calls>& s) {
    Snapshot value;
    value.generation = s.generation.load(); value.launchedGeneration = s.launchedGeneration;
    value.processBirth = s.processBirth; value.processId = s.pid; value.exitCode = s.exitCode;
    value.activeJobProcesses = s.active; value.jobLimitFlags = s.limits; value.cleanupError = s.error;
    value.ownedHandles = handles(s); value.closeFailures = s.closeFailures;
    value.attempted = s.attempted; value.resumed = s.resumed; value.admissionSealed = s.sealed.load();
    value.jobAssigned = s.assigned; value.processExited = s.exited; value.jobZeroObserved = s.jobZero;
    value.resourcesDestroyed = destroyed(s); value.terminationAttempted = s.termination; value.image = s.image;
    return value;
}
template<class Calls> void sealAdmission(State<Calls>& s) noexcept {
    if (!s.sealed.exchange(true)) s.generation.fetch_add(1);
}
template<class Calls> Code reap(State<Calls>& s, Deadline& deadline, DWORD graceMs) noexcept {
    if (!refresh(s)) return Code::cleanupIncomplete;
    if (!s.exited && graceMs) {
        const auto wait = s.calls.wait(s.process, (std::min)(graceMs, deadline.left()));
        if (wait == WAIT_FAILED) { s.error = GetLastError(); return Code::cleanupIncomplete; }
        if (!refresh(s)) return Code::cleanupIncomplete;
    }
    if ((!s.exited || !s.jobZero) && valid(s.process)) {
        s.termination = true;
        // On failure RETAIN the exact process and Job, including kill-on-close.
        // Do not close the Job early and lose active-descendant accounting.
        const bool killed = s.assigned ? !!s.calls.terminateJob(s.job) : !!s.calls.terminateProcess(s.process);
        if (!killed) { s.error = GetLastError(); return Code::cleanupIncomplete; }
    }
    if (!s.exited && valid(s.process)) {
        const auto wait = s.calls.wait(s.process, deadline.left());
        if (wait == WAIT_FAILED) { s.error = GetLastError(); return Code::cleanupIncomplete; }
        if (wait != WAIT_OBJECT_0) return Code::timeout;
    }
    if (!refresh(s)) return Code::cleanupIncomplete;
    while (!s.jobZero && deadline.left()) {
        // Bounded wait on the kernel Job completion port; no polling thread.
        s.calls.completion(s.port, deadline.left());
        if (!refresh(s)) return Code::cleanupIncomplete;
    }
    if (!s.exited || !s.jobZero) return Code::timeout;
    closeTerminal(s);
    return destroyed(s) ? Code::ok : Code::cleanupIncomplete;
}
}

// One-shot generation: even stop-before-adopt seals this object permanently.
// Caller guarantees fresh PI came from THIS call site's successful CreateProcess
// with CREATE_SUSPENDED, not OpenProcess/attach or a previously running process.
// This API never opens a process by PID/name. Object lifetime must outlive calls.
template<class Calls = Win32Calls> class BasicProcessOwner {
    using State = processOwnerDetail::State<Calls>;
    std::unique_ptr<State> state_;
    static Result reply(State& s, Code code, bool transferred = false) {
        return {code, s.error, transferred, processOwnerDetail::snapshot(s)};
    }
public:
    explicit BasicProcessOwner(Calls calls = {}) : state_(std::make_unique<State>(std::move(calls))) {}
    BasicProcessOwner(const BasicProcessOwner&) = delete;
    BasicProcessOwner& operator=(const BasicProcessOwner&) = delete;
    ~BasicProcessOwner() noexcept {
        try { if (!stop(0, 1500).state.resourcesDestroyed) processOwnerDetail::retain(state_.release()); }
        catch (...) { processOwnerDetail::retain(state_.release()); }
    }
    std::uint64_t admission() const noexcept { return state_->sealed.load() ? 0 : state_->generation.load(); }
    Result adoptSuspended(PROCESS_INFORMATION& fresh, DWORD creationFlags,
                          const std::wstring& expectedImage, std::uint64_t ticket, DWORD rollbackMs = 1500) {
        auto& s = *state_;
        std::unique_lock<std::timed_mutex> lock(s.mutex, std::try_to_lock);
        if (!lock.owns_lock()) { Result r; r.code = Code::busy; r.state.snapshotBusy = true; r.state.resourcesDestroyed = false; return r; }
        if (s.sealed.load() || !ticket || ticket != s.generation.load()) return reply(s, Code::stopped);
        if (s.attempted) return reply(s, Code::busy);
        if (!(creationFlags & CREATE_SUSPENDED) || rollbackMs > maxStopBudgetMs || expectedImage.empty() ||
            !processOwnerDetail::valid(fresh.hProcess) || !processOwnerDetail::valid(fresh.hThread) ||
            fresh.hProcess == fresh.hThread || !fresh.dwProcessId || !fresh.dwThreadId) return reply(s, Code::invalidArgument);
        wchar_t image[32768]{}; DWORD length = 32768; FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessId(fresh.hProcess) != fresh.dwProcessId || GetThreadId(fresh.hThread) != fresh.dwThreadId ||
            GetProcessIdOfThread(fresh.hThread) != fresh.dwProcessId ||
            WaitForSingleObject(fresh.hProcess, 0) != WAIT_TIMEOUT || WaitForSingleObject(fresh.hThread, 0) != WAIT_TIMEOUT ||
            !GetProcessTimes(fresh.hProcess, &created, &exited, &kernel, &user) ||
            !QueryFullProcessImageNameW(fresh.hProcess, 0, image, &length)) {
            s.error = GetLastError(); return reply(s, Code::identityMismatch);
        }
        std::wstring actual(image, length);
        if (!processOwnerDetail::sameImage(actual, expectedImage)) return reply(s, Code::identityMismatch);
        s.image = std::move(actual); s.processBirth = processOwnerDetail::birth(created);
        s.pid = fresh.dwProcessId; s.launchedGeneration = ticket; s.attempted = true; s.exited = false;
        s.process = fresh.hProcess; s.thread = fresh.hThread; fresh = {}; // precise ownership transfer
        auto fail = [&](DWORD error) {
            s.error = error; processOwnerDetail::sealAdmission(s);
            processOwnerDetail::Deadline deadline(rollbackMs);
            const auto cleanup = processOwnerDetail::reap(s, deadline, 0);
            return reply(s, cleanup == Code::ok ? Code::systemError : cleanup, true);
        };
        s.job = s.calls.createJob(); if (!s.job) return fail(GetLastError());
        if (!s.calls.setKillOnClose(s.job) || !s.calls.jobLimits(s.job, s.limits) ||
            !(s.limits & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)) return fail(GetLastError());
        s.port = s.calls.createPort(); if (!s.port) return fail(GetLastError());
        if (!s.calls.associatePort(s.job, s.port, &s)) return fail(GetLastError());
        if (!s.calls.notInherited(s.job) || !s.calls.notInherited(s.port) ||
            !s.calls.notInherited(s.process) || !s.calls.notInherited(s.thread)) return fail(GetLastError());
        if (!s.calls.assign(s.job, s.process)) return fail(GetLastError());
        s.assigned = true; s.jobZero = false;
        if (s.sealed.load() || ticket != s.generation.load()) return fail(ERROR_CANCELLED);
        const auto priorSuspendCount = s.calls.resume(s.thread);
        if (priorSuspendCount != 1) return fail(priorSuspendCount == DWORD(-1) ? GetLastError() : ERROR_INVALID_STATE);
        s.resumed = true;
        if (!processOwnerDetail::close(s, s.thread)) return reply(s, Code::cleanupIncomplete, true);
        return reply(s, Code::ok, true);
    }
    // Optional finite natural-exit drain. It never terminates a process; Job
    // membership may outlive the root (including Windows console descendants).
    // ok may describe a live process; inspect resourcesDestroyed. A failed close
    // after terminal exit reports cleanupIncomplete and keeps its exact handle.
    Result observe(DWORD naturalWaitMs = 0) {
        auto& s = *state_; std::unique_lock<std::timed_mutex> lock(s.mutex, std::try_to_lock);
        if (!lock.owns_lock()) { Result r; r.code = Code::busy; r.state.snapshotBusy = true; r.state.resourcesDestroyed = false; return r; }
        if (naturalWaitMs > maxStopBudgetMs) return reply(s, Code::invalidArgument);
        processOwnerDetail::Deadline deadline(naturalWaitMs);
        if (!processOwnerDetail::refresh(s)) return reply(s, Code::cleanupIncomplete);
        if (!s.exited && naturalWaitMs) {
            s.calls.wait(s.process, deadline.left());
            if (!processOwnerDetail::refresh(s)) return reply(s, Code::cleanupIncomplete);
        }
        while (!s.jobZero && deadline.left()) {
            s.calls.completion(s.port, deadline.left());
            if (!processOwnerDetail::refresh(s)) return reply(s, Code::cleanupIncomplete);
        }
        const bool terminal = s.exited && s.jobZero;
        processOwnerDetail::closeTerminal(s);
        return reply(s, terminal && !processOwnerDetail::destroyed(s) ? Code::cleanupIncomplete : Code::ok);
    }
    // Lifecycle authority for the ENTIRE Host only, NOT an automatic per-request
    // timeout action. graceMs+reapMs is one finite budget, capped at15s. Failure
    // leaves all unclosed exact handles in this owner's ledger for explicit retry.
    Result stop(DWORD graceMs = 0, DWORD reapMs = 1500) {
        auto& s = *state_;
        processOwnerDetail::sealAdmission(s); // exact private negative mutation target
        if (graceMs > maxStopBudgetMs || reapMs > maxStopBudgetMs - graceMs) {
            std::unique_lock<std::timed_mutex> lock(s.mutex, std::try_to_lock);
            if (lock.owns_lock()) return reply(s, Code::invalidArgument);
            Result r; r.code = Code::invalidArgument; r.state.snapshotBusy = true;
            r.state.admissionSealed = true; r.state.resourcesDestroyed = false; return r;
        }
        processOwnerDetail::Deadline deadline(graceMs + reapMs);
        std::unique_lock<std::timed_mutex> lock(s.mutex, std::defer_lock);
        if (!lock.try_lock_for(std::chrono::milliseconds(deadline.left()))) {
            Result r; r.code = Code::timeout; r.state.generation = s.generation.load();
            r.state.admissionSealed = true; r.state.snapshotBusy = true; r.state.resourcesDestroyed = false; return r;
        }
        return reply(s, processOwnerDetail::reap(s, deadline, graceMs));
    }
    static unsigned retainedCount() noexcept {
        std::lock_guard<std::mutex> lock(processOwnerDetail::retainedMutex<Calls>);
        unsigned count = 0; for (auto* s = processOwnerDetail::retainedHead<Calls>; s; s = s->retainedNext) ++count; return count;
    }
    // Explicit caller-driven retry only; no background worker/polling. Exceptional
    // destructor cleanup failures retain the SAME process/Job/port handles here.
    static unsigned retryRetained(DWORD totalMs = 1500) noexcept {
        if (totalMs > maxStopBudgetMs) return retainedCount();
        processOwnerDetail::Deadline deadline(totalMs);
        State* list = nullptr;
        { std::lock_guard<std::mutex> lock(processOwnerDetail::retainedMutex<Calls>); list = processOwnerDetail::retainedHead<Calls>; processOwnerDetail::retainedHead<Calls> = nullptr; }
        while (list) {
            auto* s = list; list = s->retainedNext; s->retainedNext = nullptr;
            std::unique_lock<std::timed_mutex> lock(s->mutex, std::try_to_lock);
            bool done = lock.owns_lock() && processOwnerDetail::reap(*s, deadline, 0) == Code::ok;
            if (lock.owns_lock()) lock.unlock();
            if (done) delete s; else processOwnerDetail::retain(s);
        }
        return retainedCount();
    }
};
using ProcessOwner = BasicProcessOwner<Win32Calls>;
}