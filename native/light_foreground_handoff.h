#pragma once

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "sleep_power_revocation.h"

namespace ymcc::lightforeground {

struct Request {
    HWND target;
    HWND origin;
    DWORD parentPid;
    ULONGLONG parentCreated;
    DWORD originPid;
    ULONGLONG originCreated;
    ULONGLONG deadline;
};

struct Cancellation {
private:
    sleeppower::RevocationMirrors::Registration power_;
public:
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Cancellation() = default;
    explicit Cancellation(sleeppower::RevocationMirrors::Registration&& power) noexcept
        : power_(std::move(power)), event(power_.event()) {}
    Cancellation(const Cancellation&) = delete;
    Cancellation& operator=(const Cancellation&) = delete;
    ~Cancellation() { if (!power_ && event) CloseHandle(event); }
    void cancel() noexcept {
        if (power_) power_.cancel();
        else if (event) SetEvent(event);
    }
    bool valid() const noexcept { return event != nullptr; }
};

struct Result {
    bool attempted;
    bool foreground;
    bool reclaimed;
    DWORD error;
    DWORD exitCode;
    const char* reason;
};

namespace detail {

struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

inline ULONGLONG created(HANDLE process) {
    FILETIME birth{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &birth, &exit, &kernel, &user)) return 0;
    return (static_cast<ULONGLONG>(birth.dwHighDateTime) << 32) | birth.dwLowDateTime;
}

template<class Function> inline Function nativeFunction(const char* name) {
    const auto address = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

inline bool manualEvent(HANDLE event) {
    struct EventInfo { LONG type; LONG state; } info{};
    using Query = LONG (NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    const auto query = nativeFunction<Query>("NtQueryEvent");
    return query && query(event, 0, &info, sizeof(info), nullptr) >= 0 && info.type == 0;
}

inline DWORD actualParent() {
    struct BasicInfo {
        LONG status;
        PVOID peb;
        ULONG_PTR affinity;
        LONG priority;
        ULONG_PTR pid;
        ULONG_PTR parent;
    } info{};
    using Query = LONG (NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    const auto query = nativeFunction<Query>("NtQueryInformationProcess");
    if (!query || query(GetCurrentProcess(), 0, &info, sizeof(info), nullptr) < 0 ||
        info.parent > (std::numeric_limits<DWORD>::max)()) return 0;
    return static_cast<DWORD>(info.parent);
}

inline std::wstring image(HANDLE process) {
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) return {};
    path.resize(length);
    return path;
}

inline std::wstring executable() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    path.resize(length);
    return path;
}

enum class Code : DWORD {
    ready = 0x474c1000, foreground, alreadyForeground, arguments, cancelled,
    expired, parentIdentity, targetIdentity, originIdentity, foregroundChanged,
    originIsParent, attachFailed, foregroundDenied, detachFailed, invalidCancellation, testClean,
    foregroundApiRejected, foregroundQueueUnconfirmed
};

inline const char* reason(Code code) {
    switch (code) {
    case Code::foreground: return "foreground";
    case Code::alreadyForeground: return "already-foreground";
    case Code::arguments: return "worker-arguments";
    case Code::cancelled: return "cancelled";
    case Code::expired: return "deadline";
    case Code::parentIdentity: return "parent-identity";
    case Code::targetIdentity: return "target-identity";
    case Code::originIdentity: return "origin-identity";
    case Code::foregroundChanged: return "foreground-changed";
    case Code::originIsParent: return "origin-is-parent";
    case Code::attachFailed: return "attach-failed";
    case Code::foregroundDenied: return "foreground-not-observed";
    case Code::detachFailed: return "detach-failed";
    case Code::invalidCancellation: return "invalid-cancellation";
    case Code::testClean: return "test-clean";
    case Code::foregroundApiRejected: return "foreground-api-not-accepted";
    case Code::foregroundQueueUnconfirmed: return "foreground-queue-unconfirmed";
    default: return "worker-exit";
    }
}

inline DWORD error(Code code) {
    switch (code) {
    case Code::foreground: case Code::alreadyForeground: case Code::testClean: return ERROR_SUCCESS;
    case Code::cancelled: return ERROR_OPERATION_ABORTED;
    case Code::expired: return ERROR_TIMEOUT;
    case Code::arguments: return ERROR_INVALID_PARAMETER;
    case Code::invalidCancellation: return ERROR_INVALID_HANDLE;
    case Code::attachFailed: case Code::detachFailed: return ERROR_GEN_FAILURE;
    case Code::foregroundDenied: return ERROR_NOT_READY;
    case Code::foregroundApiRejected: return ERROR_ACCESS_DENIED;
    case Code::foregroundQueueUnconfirmed: return ERROR_TIMEOUT;
    default: return ERROR_ACCESS_DENIED;
    }
}

template<class Operations> inline Code gate(Operations& operations) {
    const auto state = operations.cancelState();
    if (state != WAIT_TIMEOUT) return state == WAIT_OBJECT_0 ? Code::cancelled : Code::invalidCancellation;
    const auto now = operations.now();
    if (operations.request.deadline <= now || operations.request.deadline - now > 1200) return Code::expired;
    if (!operations.parentMatches()) return Code::parentIdentity;
    if (!operations.targetMatches()) return Code::targetIdentity;
    if (!operations.originMatches()) return Code::originIdentity;
    const HWND foreground = operations.foreground();
    const DWORD latestCancellation = operations.cancelState();
    if (latestCancellation != WAIT_TIMEOUT) return latestCancellation == WAIT_OBJECT_0 ? Code::cancelled : Code::invalidCancellation;
    const auto latestTime = operations.now();
    if (operations.request.deadline <= latestTime || operations.request.deadline - latestTime > 1200) return Code::expired;
    if (foreground == operations.request.target) return Code::alreadyForeground;
    if (foreground != operations.request.origin) return Code::foregroundChanged;
    if (operations.request.originPid == operations.request.parentPid) return Code::originIsParent;
    return Code::ready;
}

template<class Operations> inline Code handoff(Operations& operations) {
    Code state = gate(operations);
    if (state != Code::ready) return state;
    operations.queue();
    state = gate(operations);
    if (state != Code::ready) return state;
    if (!operations.attach(true)) return Code::attachFailed;
    state = gate(operations);
    const bool accepted = state == Code::ready && operations.setForeground();
    if (!operations.attach(false)) return Code::detachFailed;
    if (state != Code::ready) return state;
    state = gate(operations);
    if (state != Code::ready) return state == Code::alreadyForeground ? Code::foreground : state;
    if (!accepted) return Code::foregroundApiRejected;
    const bool settled = operations.settleForeground();
    state = gate(operations);
    return state == Code::alreadyForeground ? Code::foreground :
        state == Code::ready ? (settled ? Code::foregroundDenied : Code::foregroundQueueUnconfirmed) : state;
}

struct WindowsOperations {
    const Request& request;
    HANDLE cancel;
    HANDLE parent;
    HANDLE origin;
    DWORD originThread;
    DWORD targetThread;
    ULONGLONG now() const { return GetTickCount64(); }
    DWORD cancelState() const { return WaitForSingleObject(cancel, 0); }
    bool parentMatches() const {
        return WaitForSingleObject(parent, 0) == WAIT_TIMEOUT && created(parent) == request.parentCreated;
    }
    bool targetMatches() const {
        DWORD pid = 0;
        const DWORD thread = GetWindowThreadProcessId(request.target, &pid);
        return thread && thread == targetThread && pid == request.parentPid &&
            IsWindowVisible(request.target) && !IsIconic(request.target);
    }
    bool originMatches() const {
        DWORD pid = 0;
        const DWORD thread = GetWindowThreadProcessId(request.origin, &pid);
        return thread && thread == originThread && pid == request.originPid &&
            WaitForSingleObject(origin, 0) == WAIT_TIMEOUT && created(origin) == request.originCreated;
    }
    HWND foreground() const { return GetForegroundWindow(); }
    void queue() const { MSG message{}; PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE); }
    bool attach(bool connect) const {
        return AttachThreadInput(GetCurrentThreadId(), originThread, connect ? TRUE : FALSE) != FALSE;
    }
    bool setForeground() const { return SetForegroundWindow(request.target) != FALSE; }
    bool settleForeground() const {
        const auto started = now();
        if (started >= request.deadline) return false;
        DWORD_PTR reply = 0;
        return SendMessageTimeoutW(request.target, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
            static_cast<UINT>((std::min)(request.deadline - started, ULONGLONG{50})), &reply) != 0;
    }
};

inline Code worker(const Request& request, HANDLE cancel, DWORD fault = 0) {
    Handle cancelOwner(cancel);
    if (!manualEvent(cancel) || !SetHandleInformation(cancel, HANDLE_FLAG_INHERIT, 0)) return Code::invalidCancellation;
    if (!request.parentPid || !request.parentCreated || request.parentPid != actualParent()) return Code::parentIdentity;
    Handle parent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, request.parentPid));
    if (!parent.value || created(parent.value) != request.parentCreated ||
        WaitForSingleObject(parent.value, 0) != WAIT_TIMEOUT) return Code::parentIdentity;
    const auto parentImage = image(parent.value);
    const auto ownImage = image(GetCurrentProcess());
    if (parentImage.empty() || ownImage.empty() ||
        CompareStringOrdinal(parentImage.c_str(), -1, ownImage.c_str(), -1, TRUE) != CSTR_EQUAL) return Code::parentIdentity;
    const auto now = GetTickCount64();
    if (request.deadline <= now || request.deadline - now > 1200) return Code::expired;
    const DWORD cancellation = WaitForSingleObject(cancel, 0);
    if (cancellation != WAIT_TIMEOUT) return cancellation == WAIT_OBJECT_0 ? Code::cancelled : Code::invalidCancellation;
#ifdef YMCC_LIGHT_FOREGROUND_TESTING
    if (fault == 1) return Code::testClean;
    if (fault == 2) { Sleep(INFINITE); return Code::testClean; }
    if (fault == 3) {
        DWORD flags = 0;
        return GetHandleInformation(reinterpret_cast<HANDLE>(request.target), &flags) ? Code::arguments : Code::testClean;
    }
#else
    if (fault) return Code::arguments;
#endif
    if (!request.target || !request.origin || !request.originPid || !request.originCreated) return Code::arguments;
    DWORD targetPid = 0, originPid = 0;
    const DWORD targetThread = GetWindowThreadProcessId(request.target, &targetPid);
    const DWORD originThread = GetWindowThreadProcessId(request.origin, &originPid);
    if (!targetThread || targetPid != request.parentPid) return Code::targetIdentity;
    if (!originThread || originPid != request.originPid) return Code::originIdentity;
    Handle origin(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, request.originPid));
    if (!origin.value) return Code::originIdentity;
    WindowsOperations operations{request, cancel, parent.value, origin.value, originThread, targetThread};
    return handoff(operations);
}

inline constexpr wchar_t command[] = L"--ymcc-light-foreground-worker-v1";

inline bool number(const wchar_t*& cursor, ULONGLONG& value) {
    if (*cursor < L'0' || *cursor > L'9') return false;
    value = 0;
    do {
        const auto digit = static_cast<ULONGLONG>(*cursor - L'0');
        if (value > ((std::numeric_limits<ULONGLONG>::max)() - digit) / 10) return false;
        value = value * 10 + digit;
        ++cursor;
    } while (*cursor >= L'0' && *cursor <= L'9');
    return *cursor == L'\0' || *cursor == L' ' || *cursor == L'\t';
}

inline bool parse(LPCWSTR text, Request& request, HANDLE& cancel, DWORD& fault) {
    const wchar_t* cursor = text;
    while (*cursor == L' ' || *cursor == L'\t') ++cursor;
    constexpr auto length = (sizeof(command) / sizeof(wchar_t)) - 1;
    if (std::wcsncmp(cursor, command, length) || (cursor[length] != L' ' && cursor[length] != L'\t')) return false;
    cursor += length;
    ULONGLONG values[8]{};
    for (auto& value : values) {
        if (*cursor != L' ' && *cursor != L'\t') return false;
        while (*cursor == L' ' || *cursor == L'\t') ++cursor;
        if (!number(cursor, value)) return false;
    }
    while (*cursor == L' ' || *cursor == L'\t') ++cursor;
    fault = 0;
#ifdef YMCC_LIGHT_FOREGROUND_TESTING
    if (*cursor) {
        ULONGLONG selected = 0;
        if (!number(cursor, selected) || selected > 3) return false;
        fault = static_cast<DWORD>(selected);
        while (*cursor == L' ' || *cursor == L'\t') ++cursor;
    }
#endif
    if (*cursor || !values[0] || !values[1] || !values[2] || !values[3] ||
        !values[4] || !values[5] || !values[6] || !values[7] ||
        values[0] > (std::numeric_limits<ULONG_PTR>::max)() ||
        values[1] > (std::numeric_limits<ULONG_PTR>::max)() ||
        values[7] > (std::numeric_limits<ULONG_PTR>::max)() ||
        values[2] > (std::numeric_limits<DWORD>::max)() ||
        values[4] > (std::numeric_limits<DWORD>::max)()) return false;
    request = {reinterpret_cast<HWND>(static_cast<ULONG_PTR>(values[0])),
        reinterpret_cast<HWND>(static_cast<ULONG_PTR>(values[1])), static_cast<DWORD>(values[2]), values[3],
        static_cast<DWORD>(values[4]), values[5], values[6]};
    cancel = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(values[7]));
    return true;
}

struct Attributes {
    std::vector<unsigned char> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    ~Attributes() { if (list) DeleteProcThreadAttributeList(list); }
    bool initialize(HANDLE* cancel, HANDLE* job) {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 2, 0, &bytes);
        if (!bytes) return false;
        storage.resize(bytes);
        auto* candidate = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(candidate, 2, 0, &bytes)) return false;
        list = candidate;
        return UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            cancel, sizeof(HANDLE), nullptr, nullptr) &&
            UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                job, sizeof(HANDLE), nullptr, nullptr);
    }
};

inline DWORD left(ULONGLONG deadline) {
    const ULONGLONG now = GetTickCount64();
    return now >= deadline ? 0 : static_cast<DWORD>((std::min)(deadline - now, ULONGLONG{1200}));
}

struct ProcessGuard {
    HANDLE process;
    HANDLE job;
    ULONGLONG deadline;
    Result& result;
    Result* observation;
    bool assigned = false;
    bool finished = false;
    void finish(bool terminate = true) noexcept {
        if (finished) return;
        if (terminate) {
            if (!assigned || !TerminateJobObject(job, ERROR_OPERATION_ABORTED))
                TerminateProcess(process, ERROR_OPERATION_ABORTED);
        }
        if (WaitForSingleObject(process, left(deadline)) == WAIT_OBJECT_0) {
            GetExitCodeProcess(process, &result.exitCode);
            for (;;) {
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
                result.reclaimed = !assigned || (QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                    &accounting, sizeof(accounting), nullptr) && accounting.ActiveProcesses == 0);
                if (result.reclaimed || !left(deadline)) break;
                Sleep(1);
            }
        }
        if (!result.reclaimed) {
            result.foreground = false;
            result.error = ERROR_TIMEOUT;
            result.reason = "reclaim-unconfirmed";
        }
        if (observation) *observation = result;
        finished = true;
    }
    ~ProcessGuard() noexcept { finish(); }
};

#ifdef YMCC_LIGHT_FOREGROUND_TESTING
struct TestHooks {
    DWORD fault = 0;
    bool rejectAssignment = false;
    bool rejectResume = false;
    void (*observe)(DWORD, HANDLE, HANDLE) = nullptr;
};
inline const TestHooks* activeTestHooks = nullptr;
#endif

inline Result launch(const Request& requested, const std::shared_ptr<Cancellation>& cancellation, Result* observation = nullptr
#ifdef YMCC_LIGHT_FOREGROUND_TESTING
    , const TestHooks* hooks = nullptr
#endif
) {
#ifdef YMCC_LIGHT_FOREGROUND_TESTING
    if (!hooks) hooks = activeTestHooks;
#endif
    Result result{false, false, true, ERROR_INVALID_PARAMETER, 0, "request"};
    const ULONGLONG start = GetTickCount64();
    if (!cancellation || !cancellation->valid()) { result.reason = "invalid-cancellation"; return result; }
    if (WaitForSingleObject(cancellation->event, 0) != WAIT_TIMEOUT) {
        result.error = ERROR_OPERATION_ABORTED; result.reason = "cancelled"; return result;
    }
    if (requested.deadline <= start || requested.deadline - start > 1200) {
        result.error = ERROR_TIMEOUT; result.reason = "deadline"; return result;
    }
    if (requested.parentPid != GetCurrentProcessId() || requested.parentCreated != created(GetCurrentProcess())) return result;
    if (!requested.target || !requested.origin || !requested.originPid || !requested.originCreated) return result;
    DWORD targetPid = 0;
    const DWORD targetThread = GetWindowThreadProcessId(requested.target, &targetPid);
#ifdef YMCC_LIGHT_FOREGROUND_TESTING
    if (!hooks || !hooks->fault) {
#endif
        if (!targetThread || targetPid != requested.parentPid) return result;
        if (targetThread == GetCurrentThreadId()) { result.reason = "ui-thread"; return result; }
#ifdef YMCC_LIGHT_FOREGROUND_TESTING
    }
#endif
    const ULONGLONG end = (std::min)(requested.deadline, start + 250);
    if (left(end) <= 75) { result.error = ERROR_TIMEOUT; result.reason = "insufficient-budget"; return result; }
    Request request = requested;
    request.deadline = end - 75;
    const auto path = executable();
    if (path.empty()) { result.error = GetLastError(); result.reason = "image"; return result; }
    Handle inherited;
    if (!DuplicateHandle(GetCurrentProcess(), cancellation->event, GetCurrentProcess(), &inherited.value,
                         SYNCHRONIZE | 0x0001, TRUE, 0)) {
        result.error = GetLastError(); result.reason = "cancel-duplicate"; return result;
    }
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        result.error = GetLastError(); result.reason = "job"; return result;
    }
    Attributes attributes;
    if (!attributes.initialize(&inherited.value, &job.value)) { result.error = GetLastError(); result.reason = "process-attributes"; return result; }
    auto line = L"\"" + path + L"\" " + command + L" " +
        std::to_wstring(reinterpret_cast<ULONG_PTR>(request.target)) + L" " +
        std::to_wstring(reinterpret_cast<ULONG_PTR>(request.origin)) + L" " +
        std::to_wstring(request.parentPid) + L" " + std::to_wstring(request.parentCreated) + L" " +
        std::to_wstring(request.originPid) + L" " + std::to_wstring(request.originCreated) + L" " +
        std::to_wstring(request.deadline) + L" " + std::to_wstring(reinterpret_cast<ULONG_PTR>(inherited.value));
#ifdef YMCC_LIGHT_FOREGROUND_TESTING
    line += L" " + std::to_wstring(hooks ? hooks->fault : 0);
#endif
    if (!left(request.deadline) || WaitForSingleObject(cancellation->event, 0) != WAIT_TIMEOUT) {
        result.error = ERROR_OPERATION_ABORTED; result.reason = "cancelled-before-create"; return result;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.lpAttributeList = attributes.list;
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(path.c_str(), line.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
        nullptr, nullptr, &startup.StartupInfo, &processInfo)) {
        result.error = GetLastError(); result.reason = "create-process"; return result;
    }
    Handle process(processInfo.hProcess), thread(processInfo.hThread);
    CloseHandle(inherited.value);
    inherited.value = nullptr;
    result.reclaimed = false;
    ProcessGuard guard{process.value, job.value, end, result, observation, true};
    bool assigned = false;
    bool force = true;
#ifdef YMCC_LIGHT_FOREGROUND_TESTING
    if (hooks && hooks->observe) hooks->observe(0, process.value, job.value);
    if (hooks && hooks->rejectAssignment) SetLastError(ERROR_ACCESS_DENIED);
    if (!(hooks && hooks->rejectAssignment))
#endif
    {
        BOOL inside = FALSE;
        assigned = IsProcessInJob(process.value, job.value, &inside) && inside;
    }
    if (!assigned) { result.error = GetLastError(); result.reason = "assign-job"; }
    else if (!left(request.deadline) || WaitForSingleObject(cancellation->event, 0) != WAIT_TIMEOUT) {
        result.error = ERROR_OPERATION_ABORTED; result.reason = "cancelled-before-resume";
    } else {
#ifdef YMCC_LIGHT_FOREGROUND_TESTING
        if (hooks && hooks->observe) hooks->observe(1, process.value, job.value);
        if (hooks && hooks->rejectResume) SetLastError(ERROR_GEN_FAILURE);
        const DWORD resumed = hooks && hooks->rejectResume ? static_cast<DWORD>(-1) : ResumeThread(thread.value);
#else
        const DWORD resumed = ResumeThread(thread.value);
#endif
        if (resumed == static_cast<DWORD>(-1)) { result.error = GetLastError(); result.reason = "resume"; }
        else {
            result.attempted = true;
#ifdef YMCC_LIGHT_FOREGROUND_TESTING
            if (hooks && hooks->observe) hooks->observe(2, process.value, job.value);
#endif
            HANDLE waits[] = {process.value, cancellation->event};
            const DWORD waited = WaitForMultipleObjects(2, waits, FALSE, left(request.deadline));
            if (waited == WAIT_OBJECT_0) {
                force = false;
                if (GetExitCodeProcess(process.value, &result.exitCode)) {
                    const auto code = static_cast<Code>(result.exitCode);
                    result.foreground = (code == Code::foreground || code == Code::alreadyForeground) &&
                        left(request.deadline) && WaitForSingleObject(cancellation->event, 0) == WAIT_TIMEOUT;
                    result.error = error(code); result.reason = reason(code);
                    if (!result.foreground && (code == Code::foreground || code == Code::alreadyForeground)) {
                        result.error = ERROR_OPERATION_ABORTED; result.reason = "late-receipt";
                    }
                } else { result.error = GetLastError(); result.reason = "exit-code"; }
            } else if (waited == WAIT_OBJECT_0 + 1) { result.error = ERROR_OPERATION_ABORTED; result.reason = "cancelled"; }
            else if (waited == WAIT_TIMEOUT) { result.error = ERROR_TIMEOUT; result.reason = "deadline"; }
            else { result.error = GetLastError(); result.reason = "wait"; }
        }
    }
    if (force) result.foreground = false;
    guard.finish(force);
    if (result.foreground && (WaitForSingleObject(cancellation->event, 0) != WAIT_TIMEOUT || !left(end))) {
        result.foreground = false;
        result.error = ERROR_OPERATION_ABORTED;
        result.reason = "late-receipt";
    }
#ifdef YMCC_LIGHT_FOREGROUND_TESTING
    if (hooks && hooks->observe) hooks->observe(3, process.value, job.value);
#endif
    return result;
}

}

inline ULONGLONG processCreated(DWORD pid) {
    detail::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    return process.value ? detail::created(process.value) : 0;
}

inline Result execute(const Request& request, const std::shared_ptr<Cancellation>& cancellation) {
    Result observation{false, false, true, ERROR_NOT_ENOUGH_MEMORY, 0, "allocation"};
    try { return detail::launch(request, cancellation, &observation); }
    catch (...) {
        observation.foreground = false;
        observation.error = ERROR_NOT_ENOUGH_MEMORY;
        observation.reason = observation.reclaimed ? "exception-reclaimed" : "reclaim-unconfirmed";
        return observation;
    }
}

inline bool tryCommandLine(LPCWSTR text, int& exitCode) {
    if (!text) return false;
    const wchar_t* arguments = text;
    while (*arguments == L' ' || *arguments == L'\t') ++arguments;
    constexpr wchar_t reserved[] = L"--ymcc-light-foreground";
    constexpr auto reservedLength = (sizeof(reserved) / sizeof(wchar_t)) - 1;
    if (std::wcsncmp(arguments, reserved, reservedLength)) {
        if (*arguments == L'"') {
            ++arguments;
            while (*arguments && *arguments != L'"') ++arguments;
            if (*arguments != L'"') return false;
            ++arguments;
        } else {
            while (*arguments && *arguments != L' ' && *arguments != L'\t') ++arguments;
        }
    }
    bool recognized = false;
    const wchar_t* token = arguments;
    while (*token) {
        while (*token == L' ' || *token == L'\t') ++token;
        const wchar_t* content = *token == L'"' ? token + 1 : token;
        if (!std::wcsncmp(content, reserved, reservedLength)) { recognized = true; break; }
        bool quoted = false;
        std::size_t backslashes = 0;
        while (*token && (quoted || (*token != L' ' && *token != L'\t'))) {
            if (*token == L'\\') { ++backslashes; ++token; continue; }
            if (*token == L'"' && backslashes % 2 == 0) {
                if (quoted && token[1] == L'"') { token += 2; backslashes = 0; continue; }
                quoted = !quoted;
            }
            backslashes = 0;
            ++token;
        }
    }
    if (!recognized) return false;
    exitCode = static_cast<int>(detail::Code::arguments);
    try {
        Request request{};
        HANDLE cancel = nullptr;
        DWORD fault = 0;
        if (detail::parse(arguments, request, cancel, fault)) exitCode = static_cast<int>(detail::worker(request, cancel, fault));
    } catch (...) { exitCode = static_cast<int>(detail::Code::arguments); }
    return true;
}

}
