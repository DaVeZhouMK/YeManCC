#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include "json.hpp"

// Unresolved tripwires: the actual WindowsOps adapter is parsed, never called.
// An accidental call to it fails linking instead of launching/terminating a process.
namespace forbidden_os {
ULONGLONG tick(); DWORD error(); DWORD attributes(const wchar_t*);
HANDLE job(LPSECURITY_ATTRIBUTES, LPCWSTR);
BOOL configure(HANDLE, JOBOBJECTINFOCLASS, LPVOID, DWORD);
BOOL start(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD,
           LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
BOOL assign(HANDLE, HANDLE); BOOL foreground(DWORD); DWORD resume(HANDLE); DWORD wait(HANDLE, DWORD);
BOOL exitCode(HANDLE, LPDWORD); BOOL terminate(HANDLE, UINT); BOOL close(HANDLE);
DWORD currentPid();void setError(DWORD);HANDLE event(LPSECURITY_ATTRIBUTES,BOOL,BOOL,LPCWSTR);
DWORD waitMany(DWORD,const HANDLE*,BOOL,DWORD);BOOL setEvent(HANDLE);
DWORD currentThread();DWORD windowThread(HWND,LPDWORD);HWND foregroundWindow();
LRESULT sendMessage(HWND,UINT,WPARAM,LPARAM,UINT,UINT,PDWORD_PTR);
}
#define GetTickCount64 forbidden_os::tick
#define GetLastError forbidden_os::error
#define GetFileAttributesW forbidden_os::attributes
#define CreateJobObjectW forbidden_os::job
#define SetInformationJobObject forbidden_os::configure
#define CreateProcessW forbidden_os::start
#define AssignProcessToJobObject forbidden_os::assign
#define AllowSetForegroundWindow forbidden_os::foreground
#define ResumeThread forbidden_os::resume
#define WaitForSingleObject forbidden_os::wait
#define GetExitCodeProcess forbidden_os::exitCode
#define TerminateProcess forbidden_os::terminate
#define CloseHandle forbidden_os::close
#define GetCurrentProcessId forbidden_os::currentPid
#define SetLastError forbidden_os::setError
#define CreateEventW forbidden_os::event
#define WaitForMultipleObjects forbidden_os::waitMany
#define SetEvent forbidden_os::setEvent
#define GetCurrentThreadId forbidden_os::currentThread
#define GetWindowThreadProcessId forbidden_os::windowThread
#define GetForegroundWindow forbidden_os::foregroundWindow
#define SendMessageTimeoutW forbidden_os::sendMessage
#include "../native/task_view_dispatch.h"

using json = nlohmann::json;
namespace tv = ymcc::taskView;
namespace fixture {
static unsigned checks = 0;
static json cases = json::array();
static void check(bool condition, const std::string& message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
static std::uintptr_t id(HANDLE h) { return reinterpret_cast<std::uintptr_t>(h); }
static HANDLE handle(std::uintptr_t value) { return reinterpret_cast<HANDLE>(value); }
enum class Kind { Job, Process, Thread };
struct Node {
    Kind kind; bool owned = true, closed = false, configured = false;
    bool resumed = false, ready = false, released = false, exited = false;
    HANDLE process = nullptr, job = nullptr;
    DWORD code = 0, pid = 0;
};
struct Event { std::string op; HANDLE h = nullptr; DWORD budget = 0; ULONGLONG at = 0; };
struct WaitStep { DWORD result; ULONGLONG advance = 0; };
struct FakeOps {
    ULONGLONG clock = 100;
    DWORD lastError = 700, nextExit = 0;
    std::string fail;
    bool directory = false, autoComplete = true, terminateAccepted = true, terminateExits = true;
    std::map<std::string, ULONGLONG> cost;
    std::map<std::uintptr_t, Node> nodes;
    std::vector<Event> events;
    std::deque<WaitStep> waits;
    std::vector<std::string> violations;
    std::vector<HANDLE> processes;
    std::vector<ULONGLONG> startTimes;
    std::vector<DWORD> waitBudgets, foregroundPids;
    std::function<void(DWORD)> waitHook;
    std::uintptr_t next = 0x100;
    unsigned peakLiveProcesses = 0;
    static constexpr DWORD attrError = 301, jobError = 302, configError = 303,
        startError = 304, assignError = 305, resumeError = 306, exitError = 307,
        waitError = 308, terminateError = 309, foregroundError = 310, readyError = 311, releaseError = 312;
    FakeOps() {
        nodes.emplace(1, Node{Kind::Process, false});
        nodes.emplace(2, Node{Kind::Job, false});
        nodes.emplace(3, Node{Kind::Thread, false});
    }
    void record(const char* name, HANDLE h = nullptr, DWORD budget = 0) {
        events.push_back({name, h, budget, clock});
        const auto found = cost.find(name); if (found != cost.end()) clock += found->second;
    }
    unsigned count(const std::string& op) const {
        return static_cast<unsigned>(std::count_if(events.begin(), events.end(), [&](const Event& event) { return event.op == op; }));
    }
    unsigned liveHandles() const {
        unsigned count = 0; for (const auto& pair : nodes) if (pair.second.owned && !pair.second.closed) ++count; return count;
    }
    unsigned runningProcesses() const {
        unsigned count = 0; for (const auto& pair : nodes) if (pair.second.owned && pair.second.kind == Kind::Process && !pair.second.exited) ++count; return count;
    }
    Node* owned(HANDLE h, Kind kind) {
        const auto found = nodes.find(id(h));
        if (found == nodes.end() || !found->second.owned || found->second.closed || found->second.kind != kind) {
            violations.push_back("foreign/closed/wrong-kind handle"); return nullptr;
        }
        return &found->second;
    }
    HANDLE make(Kind kind) {
        const auto value = next++; nodes.emplace(value, Node{kind}); return handle(value);
    }
    ULONGLONG now() noexcept { record("now"); return clock; }
    DWORD error() noexcept { record("error"); return lastError; }
    DWORD attributes(const wchar_t* path) noexcept {
        record("attributes");
        if (!path || std::wstring(path) != L"fixture helper.exe") violations.push_back("helper path altered");
        if (fail == "attributes") { lastError = attrError; return INVALID_FILE_ATTRIBUTES; }
        return directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    }
    HANDLE createJob() noexcept {
        record("createJob"); if (fail == "createJob") { lastError = jobError; return nullptr; }
        return make(Kind::Job);
    }
    bool configureJob(HANDLE job) noexcept {
        record("configureJob", job); auto* node = owned(job, Kind::Job); if (!node) return false;
        if (fail == "configureJob") { lastError = configError; return false; }
        node->configured = true; return true;
    }
    bool start(const wchar_t* path, wchar_t* command, PROCESS_INFORMATION& pi) noexcept {
        startTimes.push_back(clock); record("start");
        if (!path || std::wstring(path) != L"fixture helper.exe" || !command || std::wstring(command) != L"\"fixture helper.exe\" --task-view") violations.push_back("producer arguments altered");
        if (fail == "start") { lastError = startError; return false; }
        if (runningProcesses() != 0) violations.push_back("second helper created while owned helper still running");
        pi.hProcess = make(Kind::Process); pi.hThread = make(Kind::Thread);
        nodes.at(id(pi.hThread)).process = pi.hProcess; nodes.at(id(pi.hProcess)).code = nextExit;
        pi.dwProcessId = static_cast<DWORD>(id(pi.hProcess) + 10000);
        nodes.at(id(pi.hProcess)).pid = pi.dwProcessId;
        processes.push_back(pi.hProcess); peakLiveProcesses = (std::max)(peakLiveProcesses, runningProcesses());
        return true;
    }
    bool assign(HANDLE job, HANDLE process) noexcept {
        record("assign", process); auto* j = owned(job, Kind::Job); auto* p = owned(process, Kind::Process);
        if (!j || !p) return false;
        if (!j->configured || p->resumed) violations.push_back("assign outside configured/suspended phase");
        if (fail == "assign") { lastError = assignError; return false; }
        p->job = job; return true;
    }
    bool allowForeground(DWORD pid) noexcept {
        record("allowForeground"); foregroundPids.push_back(pid);
        const auto found = std::find_if(nodes.begin(),nodes.end(),[&](const auto& pair) { return pair.second.owned && pair.second.kind == Kind::Process && pair.second.pid == pid && !pair.second.closed; });
        if (pid == ASFW_ANY || found == nodes.end() || !found->second.job || !found->second.resumed || !found->second.ready || found->second.released) violations.push_back("foreground grant targeted non-owned or uninitialized/already-released process");
        if (fail == "allowForeground") { lastError = foregroundError; return false; }
        return true;
    }
    bool resume(HANDLE thread) noexcept {
        record("resume", thread); auto* t = owned(thread, Kind::Thread); if (!t) return false;
        auto* p = owned(t->process, Kind::Process); if (!p) return false;
        if (!p->job || !nodes.at(id(p->job)).configured || p->resumed) violations.push_back("resume before job assignment or duplicate resume");
        if (fail == "resume") { lastError = resumeError; return false; }
        p->resumed = true; return true;
    }
    void finishHandshake() noexcept {}
    DWORD ready(HANDLE process,DWORD ms) noexcept {
        record("ready",process,ms);auto* p=owned(process,Kind::Process);if(!p)return WAIT_FAILED;
        if(!p->resumed || !p->job)violations.push_back("ready before resume/job assignment");
        if(ms>tv::callBudgetMs)violations.push_back("unbounded initialization wait");
        if(fail=="ready"){lastError=readyError;return WAIT_FAILED;}
        if(fail=="readyTimeout"){clock+=ms;return WAIT_TIMEOUT;}
        if(fail=="earlyExit"){p->exited=true;return WAIT_OBJECT_0+1;}
        p->ready=true;return WAIT_OBJECT_0;
    }
    bool release() noexcept {
        record("release");
        auto* p=owned(processes.back(),Kind::Process);if(!p)return false;
        if(!p->ready || !count("allowForeground"))violations.push_back("Shell released before ready and grant");
        if(fail=="release"){lastError=releaseError;return false;}
        p->released=true;return true;
    }
    DWORD wait(HANDLE process, DWORD ms) noexcept {
        record("wait", process, ms); waitBudgets.push_back(ms); auto* p = owned(process, Kind::Process);
        if (!p) return WAIT_FAILED;
        if (ms > tv::callBudgetMs || ms == INFINITE) violations.push_back("unbounded wait");
        if (waitHook) waitHook(ms);
        if (!waits.empty()) {
            const auto step = waits.front(); waits.pop_front(); clock += step.advance;
            if (step.result == WAIT_OBJECT_0) p->exited = true;
            if (step.result == WAIT_FAILED || step.result == WAIT_ABANDONED_0) lastError = waitError;
            return step.result;
        }
        if (p->exited || (autoComplete && p->released)) { p->exited = true; return WAIT_OBJECT_0; }
        clock += ms; return WAIT_TIMEOUT;
    }
    bool exitCode(HANDLE process, DWORD& code) noexcept {
        record("exitCode", process); auto* p = owned(process, Kind::Process); if (!p) return false;
        if (!p->exited) violations.push_back("exit queried before signalled");
        if (fail == "exitCode") { lastError = exitError; return false; }
        code = p->code; return true;
    }
    bool terminate(HANDLE process) noexcept {
        record("terminate", process); auto* p = owned(process, Kind::Process); if (!p) return false;
        if (!terminateAccepted) { lastError = terminateError; return false; }
        if (terminateExits) p->exited = true;
        return true;
    }
    void close(HANDLE h) noexcept {
        record("close", h); if (!h) return;
        const auto found = nodes.find(id(h));
        if (found == nodes.end() || !found->second.owned || found->second.closed) { violations.push_back("foreign/double handle close"); return; }
        auto& node = found->second;
        if (node.kind == Kind::Process && !node.exited) violations.push_back("live process handle discarded before proven cleanup");
        if (node.kind == Kind::Job) {
            for (auto& pair : nodes) if (pair.second.owned && pair.second.kind == Kind::Process && pair.second.job == h && !pair.second.exited) {
                violations.push_back("job discarded while owned process still running"); pair.second.exited = true;
            }
        }
        node.closed = true;
    }
    void markExited(HANDLE process) { nodes.at(id(process)).exited = true; }
    void sound(bool allClosed = true) const {
        check(violations.empty(), violations.empty() ? "handle model sound" : violations.front());
        check(!nodes.at(1).closed && !nodes.at(1).exited && !nodes.at(2).closed && !nodes.at(3).closed, "foreign sentinel process/job/thread affected");
        check(peakLiveProcesses <= 1, "more than one live owned helper");
        if (allClosed) check(liveHandles() == 0 && runningProcesses() == 0, "owned handles/process leaked");
    }
};
static tv::Result request(tv::Dispatcher& dispatcher, FakeOps& ops, ULONGLONG deadline) {
    wchar_t command[] = L"\"fixture helper.exe\" --task-view";
    return dispatcher.request(ops, L"fixture helper.exe", command, deadline);
}
static std::size_t position(const FakeOps& ops, const std::string& name, HANDLE h = nullptr) {
    for (std::size_t i = 0; i < ops.events.size(); ++i) if (ops.events[i].op == name && (!h || ops.events[i].h == h)) return i;
    return ops.events.size();
}
static bool nodesClosedBeforeNewStart(const FakeOps& ops, HANDLE process, std::size_t start) {
    return position(ops,"close",process) < start && position(ops,"close",ops.nodes.at(id(process)).job) < start;
}
static void assertSuccess(const tv::Result& result) {
    check(result.status == tv::Status::Requested && result.helperStarted && result.helperExitKnown && result.helperExitCode == 0, "successful request/result contract");
    check(result.cleanupVerified && !result.cleanupRetained && !result.terminationAttempted && !result.terminationAccepted && !result.waitTimedOut, "success cleanup/termination flags");
}
static void constantsAndNames() {
    static_assert(!std::is_copy_constructible_v<tv::Dispatcher>);
    static_assert(!std::is_copy_assignable_v<tv::Dispatcher>);
    check(tv::callBudgetMs == 3000 && tv::cleanupBudgetMs == 500 && tv::requestBudgetMs == 4500, "budget constants changed");
    const std::array<std::pair<tv::Status, const char*>, 14> names{{
        {tv::Status::Requested,"requested"},{tv::Status::Busy,"busy"},{tv::Status::Expired,"request-expired"},
        {tv::Status::MissingHelper,"helper-missing"},{tv::Status::JobFailed,"job-failed"},{tv::Status::StartFailed,"start-failed"},
        {tv::Status::AssignFailed,"job-assignment-failed"},{tv::Status::ResumeFailed,"resume-failed"},{tv::Status::WaitFailed,"wait-failed"},
        {tv::Status::TimedOut,"timed-out-outcome-unknown"},{tv::Status::ExitQueryFailed,"exit-query-failed"},{tv::Status::ApiFailed,"api-rejected"},{tv::Status::CleanupUnproven,"cleanup-unproven"},
        {static_cast<tv::Status>(999),"unknown"}}};
    for (const auto& pair : names) check(std::string(tv::statusName(pair.first)) == pair.second, "status name ambiguity");
    tv::Dispatcher dispatcher; FakeOps ops; check(ops.events.empty(), "construction polled or started background work");
    assertSuccess(request(dispatcher, ops, ops.clock + 4500)); ops.sound();
    check(ops.count("wait") == 1 && ops.count("terminate") == 0, "success does extra polling/termination");
}
static void successAndComExit() {
    for (const DWORD code : {DWORD(0), DWORD(1), DWORD(0x80004005u), DWORD(0x80040154u)}) {
        tv::Dispatcher dispatcher; FakeOps ops; ops.nextExit = code;
        const auto result = request(dispatcher, ops, ops.clock + 4500);
        check(result.status == (code == 0 ? tv::Status::Requested : tv::Status::ApiFailed) && result.helperExitKnown && result.helperExitCode == code, "helper exit code/COM rejection changed");
        check(result.cleanupVerified && !result.cleanupRetained && !result.terminationAttempted, "completed child terminated or retained");
        check(ops.count("start") == 1 && ops.count("assign") == 1 && ops.count("resume") == 1 && ops.count("exitCode") == 1 && ops.count("close") == 3, "success operation count");
        check(ops.count("allowForeground") == 1 && result.foregroundGrantAccepted && ops.foregroundPids == std::vector<DWORD>({ops.nodes.at(id(ops.processes.front())).pid}), "foreground grant did not use exactly owned helper PID");
        check(position(ops,"resume") < position(ops,"ready") && position(ops,"ready") < position(ops,"allowForeground") && position(ops,"allowForeground") < position(ops,"release"), "foreground grant ordering");
        check(position(ops,"configureJob") < position(ops,"start") && position(ops,"start") < position(ops,"assign") && position(ops,"assign") < position(ops,"resume") && position(ops,"resume") < position(ops,"wait"), "suspend/configure/assign/resume ordering");
        ops.sound();
    }
}
static void beforeChildApiFailures() {
    struct Failure { const char* op; tv::Status status; DWORD error; };
    for (const auto& failure : {Failure{"attributes",tv::Status::MissingHelper,FakeOps::attrError}, Failure{"createJob",tv::Status::JobFailed,FakeOps::jobError}, Failure{"configureJob",tv::Status::JobFailed,FakeOps::configError}, Failure{"start",tv::Status::StartFailed,FakeOps::startError}}) {
        tv::Dispatcher dispatcher; FakeOps ops; ops.fail = failure.op;
        const auto result = request(dispatcher, ops, ops.clock + 4500);
        check(result.status == failure.status && result.errorHint == failure.error && !result.helperStarted && !result.helperExitKnown, "pre-child API failure result/error");
        check(ops.count("resume") == 0 && ops.count("wait") == 0 && ops.count("terminate") == 0, "pre-child failure touched a process");
        ops.sound(); ops.fail.clear(); assertSuccess(request(dispatcher, ops, ops.clock + 4500)); ops.sound();
    }
    tv::Dispatcher dispatcher; FakeOps directory; directory.directory = true;
    const auto result = request(dispatcher, directory, directory.clock + 4500);
    check(result.status == tv::Status::MissingHelper && directory.count("createJob") == 0 && directory.count("start") == 0, "directory admitted as helper"); directory.sound();
}
static void childApiFailures() {
    struct Failure { const char* op; tv::Status status; DWORD error; };
    for (const auto& failure : {Failure{"assign",tv::Status::AssignFailed,FakeOps::assignError}, Failure{"resume",tv::Status::ResumeFailed,FakeOps::resumeError}, Failure{"exitCode",tv::Status::ExitQueryFailed,FakeOps::exitError}}) {
        tv::Dispatcher dispatcher; FakeOps ops; ops.fail = failure.op;
        const auto result = request(dispatcher, ops, ops.clock + 4500);
        check(result.status == failure.status && result.errorHint == failure.error && result.helperStarted && !result.helperExitKnown, "owned-child API failure result/error");
        check(result.cleanupVerified && !result.cleanupRetained && result.terminationAttempted == (ops.fail != "exitCode"), "owned-child failure cleanup");
        check(ops.count("close") == 3 && (ops.fail != "assign" || ops.count("resume") == 0), "child failure leaked handles or resumed unassigned child");
        ops.sound(); ops.fail.clear(); assertSuccess(request(dispatcher, ops, ops.clock + 4500)); ops.sound();
    }
}
static void waitsAndTermination() {
    for (const DWORD wait : {DWORD(WAIT_TIMEOUT), DWORD(WAIT_FAILED), DWORD(WAIT_ABANDONED_0)}) {
        tv::Dispatcher dispatcher; FakeOps ops; ops.autoComplete = false;
        ops.waits.push_back({wait, wait == WAIT_TIMEOUT ? ULONGLONG(3000) : ULONGLONG(0)});
        const auto result = request(dispatcher, ops, ops.clock + 4500);
        check(result.status == (wait == WAIT_TIMEOUT ? tv::Status::TimedOut : tv::Status::WaitFailed), "wait status classification");
        check(result.waitTimedOut == (wait == WAIT_TIMEOUT) && result.errorHint == (wait == WAIT_TIMEOUT ? DWORD(ERROR_TIMEOUT) : FakeOps::waitError), "wait error/timeout flags");
        check(result.terminationAttempted && result.terminationAccepted && result.cleanupVerified && !result.cleanupRetained && ops.count("terminate") == 1, "failed wait did not retire exact child");
        check(ops.waitBudgets == std::vector<DWORD>({3000,0,500}), "wait/cancel cleanup budget sequence"); ops.sound();
    }
    tv::Dispatcher dispatcher; FakeOps ops; ops.autoComplete = false;
    ops.waits = {{WAIT_TIMEOUT,3000},{WAIT_OBJECT_0,0}};
    const auto raced = request(dispatcher, ops, ops.clock + 4500);
    check(raced.status == tv::Status::TimedOut && raced.cleanupVerified && !raced.terminationAttempted && ops.count("terminate") == 0, "naturally exited child unnecessarily terminated"); ops.sound();
}
static void timeoutThenRetry() {
    tv::Dispatcher dispatcher; FakeOps ops; ops.autoComplete = false;
    ops.waits = {{WAIT_TIMEOUT,3000},{WAIT_TIMEOUT,0},{WAIT_OBJECT_0,500}};
    const auto result = request(dispatcher, ops, ops.clock + 4500);
    check(result.status == tv::Status::TimedOut && result.waitTimedOut && result.cleanupVerified && result.elapsedMs == 3500, "call+cleanup budget or timeout outcome");
    check(result.terminationAttempted && result.terminationAccepted && ops.processes.size() == 1, "timeout termination ownership"); ops.sound();
    ops.autoComplete = true; const auto retry = request(dispatcher, ops, ops.clock + 4500);
    assertSuccess(retry); check(ops.processes.size() == 2 && ops.count("terminate") == 1, "verified timeout blocked retry or cancelled a new child"); ops.sound();
}
static void retainedCleanup() {
    for (const bool accepted : {false,true}) for (const DWORD cleanupWait : {DWORD(WAIT_TIMEOUT),DWORD(WAIT_FAILED)}) {
        tv::Dispatcher dispatcher; FakeOps ops; ops.autoComplete = false; ops.terminateAccepted = accepted; ops.terminateExits = false;
        ops.waits = {{WAIT_TIMEOUT,3000},{WAIT_TIMEOUT,0},{cleanupWait,500}};
        const auto first = request(dispatcher, ops, ops.clock + 4500);
        check(first.status == tv::Status::CleanupUnproven && first.cleanupRetained && !first.cleanupVerified && first.waitTimedOut && first.errorHint == ERROR_TIMEOUT, "unproven timeout cleanup discarded primary evidence/retention");
        check(first.terminationAttempted && first.terminationAccepted == accepted && ops.liveHandles() == 2 && ops.count("close") == 1, "pending process/job not retained with thread closed"); ops.sound(false);
        const auto original = ops.processes.front();
        for (unsigned retry = 0; retry < 16; ++retry) {
            const auto attributesBefore = ops.count("attributes"), jobsBefore = ops.count("createJob");
            const auto pending = request(dispatcher, ops, ops.clock + 4500);
            check(pending.status == tv::Status::CleanupUnproven && pending.cleanupRetained && !pending.helperStarted && ops.processes.size() == 1 && ops.count("start") == 1, "pending cleanup allowed a second helper");
            check(ops.count("attributes") == attributesBefore && ops.count("createJob") == jobsBefore && ops.liveHandles() == 2, "pending cleanup started new preparation or lost handles");
        }
        ops.markExited(original); ops.autoComplete = true;
        const auto retry = request(dispatcher, ops, ops.clock + 4500);
        assertSuccess(retry); check(ops.processes.size() == 2, "exited pending child did not recover");
        const auto secondStart = std::find_if(ops.events.begin() + static_cast<std::ptrdiff_t>(position(ops,"close",original)), ops.events.end(), [](const Event& event) { return event.op == "start"; });
        check(secondStart != ops.events.end() && nodesClosedBeforeNewStart(ops, original, static_cast<std::size_t>(secondStart - ops.events.begin())), "new helper preceded old process/job closure");
        ops.sound();
    }
}
static void failureCleanupRetention() {
    for (const char* failure : {"assign","resume","wait"}) {
        tv::Dispatcher dispatcher; FakeOps ops; ops.autoComplete = false; ops.terminateAccepted = false;
        if (std::string(failure) == "wait") ops.waits.push_back({WAIT_FAILED,0}); else ops.fail = failure;
        const auto first = request(dispatcher, ops, ops.clock + 4500);
        check(first.status == tv::Status::CleanupUnproven && first.cleanupRetained && first.helperStarted && first.terminationAttempted && !first.terminationAccepted, "child failure discarded an unproven process/job");
        check(ops.liveHandles() == 2 && ops.count("close") == 1 && ops.processes.size() == 1, "failed child retention handle count"); ops.sound(false);
        const auto pending = request(dispatcher, ops, ops.clock + 4500);
        check(pending.status == tv::Status::CleanupUnproven && !pending.helperStarted && ops.count("start") == 1, "failed-child pending admitted a second helper");
        ops.markExited(ops.processes.front()); ops.fail.clear(); ops.autoComplete = true;
        assertSuccess(request(dispatcher, ops, ops.clock + 4500)); ops.sound();
    }
}
static void expiredAtEntry() {
    for (const ULONGLONG deadline : {ULONGLONG(0),ULONGLONG(99),ULONGLONG(100)}) {
        tv::Dispatcher dispatcher; FakeOps ops;
        const auto result = request(dispatcher, ops, deadline);
        check(result.status == tv::Status::Expired && !result.helperStarted && !result.terminationAttempted, "expired request admitted");
        check(ops.count("attributes") == 0 && ops.count("createJob") == 0 && ops.count("start") == 0 && ops.count("wait") == 0, "expired entry performed helper work");
        ops.sound(); assertSuccess(request(dispatcher, ops, ops.clock + 4500)); ops.sound();
    }
}
static void expiresInPreparation(const char* phase) {
    tv::Dispatcher dispatcher; FakeOps ops; ops.cost[phase] = 5;
    const auto deadline = ops.clock + 5;
    const auto result = request(dispatcher, ops, deadline);
    // Keep cleanup assertions first so a failure still verifies that no owned
    // process survived. No suspended-helper creation is accepted after expiry.
    ops.sound();
    check(result.status == tv::Status::Expired && !result.helperStarted && ops.count("start") == 0 && ops.count("resume") == 0, std::string("deadline crossed in ") + phase + " still created a helper; add deadline check immediately before ops.start");
    check(ops.count("wait") == 0 && ops.count("terminate") == 0, "already expired preparation touched a process");
    ops.cost.clear(); assertSuccess(request(dispatcher, ops, ops.clock + 4500)); ops.sound();
}
static void expiredAttributes() { expiresInPreparation("attributes"); }
static void expiredCreateJob() { expiresInPreparation("createJob"); }
static void expiredConfigureJob() { expiresInPreparation("configureJob"); }
static void expiresBeforeResume() {
    for (const char* phase : {"start","assign"}) {
        tv::Dispatcher dispatcher; FakeOps ops; ops.cost[phase] = 5;
        const auto result = request(dispatcher, ops, ops.clock + 5);
        check(result.status == tv::Status::Expired && result.helperStarted && result.cleanupVerified && !result.cleanupRetained, "created suspended child expiry result");
        check(ops.count("assign") == 1 && ops.count("allowForeground") == 0 && ops.count("resume") == 0 && ops.count("terminate") == 1 && result.terminationAttempted && result.terminationAccepted, "expired suspended child was resumed or not retired");
        check(ops.startTimes.front() == 100 && ops.waitBudgets == std::vector<DWORD>({0,500}), "pre-resume expiry issued command wait"); ops.sound();
        ops.cost.clear(); assertSuccess(request(dispatcher, ops, ops.clock + 4500)); ops.sound();
    }
}
static void waitBudgetBoundaries() {
    for (const ULONGLONG remaining : {ULONGLONG(1),ULONGLONG(2),ULONGLONG(499),ULONGLONG(500),ULONGLONG(501),ULONGLONG(2999),ULONGLONG(3000),ULONGLONG(3001),ULONGLONG(4500),ULONGLONG(MAXDWORD) + 123}) {
        tv::Dispatcher dispatcher; FakeOps ops;
        assertSuccess(request(dispatcher, ops, ops.clock + remaining));
        check(ops.waitBudgets.size() == 1 && ops.waitBudgets[0] == static_cast<DWORD>((std::min)(remaining,ULONGLONG(3000))), "request remainder not clamped before narrowing to DWORD"); ops.sound();
    }
    tv::Dispatcher dispatcher; FakeOps ops;
    for (const char* phase : {"attributes","createJob","configureJob","start","assign","resume"}) ops.cost[phase] = 2;
    assertSuccess(request(dispatcher, ops, ops.clock + 20));
    check(ops.waitBudgets == std::vector<DWORD>({8}), "preparation time not subtracted from request deadline"); ops.sound();
}
static void grantFailureKeepsUi() {
    tv::Dispatcher dispatcher; FakeOps ops; ops.fail = "allowForeground";
    const auto result = request(dispatcher,ops,ops.clock + 4500);
    check(result.status==tv::Status::ForegroundGrantFailed&&result.helperResumed&&result.helperReady&&!result.executionReleased&&!result.helperExitKnown,"grant failure treated as successful COM execution");
    check(result.helperStarted&&result.cleanupVerified&&!result.cleanupRetained,"denied grant helper ownership lost");
    check(result.foregroundGrantError==FakeOps::foregroundError&&result.errorHint==FakeOps::foregroundError,"grant error replaced by cleanup");
    check(result.foregroundGrantAttempted&&!result.foregroundGrantAccepted&&ops.count("allowForeground")==1&&ops.count("resume")==1&&ops.count("release")==0&&ops.count("terminate")==1,"denied grant resumed/retried/fell back");
    check(result.helperPid==ops.nodes.at(id(ops.processes.front())).pid,"owned helper PID lost");
    check(ops.foregroundPids==std::vector<DWORD>({result.helperPid}),"failed grant targeted a foreign PID");ops.sound();
    ops.fail.clear();assertSuccess(request(dispatcher,ops,ops.clock+4500));ops.sound();
}
static void grantFailureCleanupDebt() {
    tv::Dispatcher dispatcher;FakeOps ops;ops.fail="allowForeground";ops.terminateAccepted=false;ops.terminateExits=false;
    auto first=request(dispatcher,ops,ops.clock+4500);
    check(first.status==tv::Status::CleanupUnproven&&first.cleanupRetained&&!first.cleanupVerified&&first.helperResumed&&!first.executionReleased,"grant cleanup uncertainty discarded");
    check(first.foregroundGrantError==FakeOps::foregroundError&&first.errorHint==FakeOps::foregroundError,"cleanup error replaced denied grant evidence");
    check(ops.count("release")==0&&ops.count("start")==1,"grant cleanup failure executed API");ops.sound(false);
    const auto second=request(dispatcher,ops,ops.clock+4500);
    check(second.status==tv::Status::CleanupUnproven&&ops.count("start")==1&&ops.count("release")==0,"pending denied helper allowed accumulation");
    ops.markExited(ops.processes.front());ops.fail.clear();ops.terminateAccepted=true;ops.terminateExits=true;
    assertSuccess(request(dispatcher,ops,ops.clock+4500));ops.sound();
}
static void grantConsumesDeadline() {
    tv::Dispatcher dispatcher; FakeOps ops; ops.cost["allowForeground"] = 5;
    const auto result = request(dispatcher,ops,ops.clock + 5);
    ops.sound();
    check(result.status == tv::Status::Expired && result.cleanupVerified && result.helperStarted && ops.count("allowForeground") == 1 && ops.count("resume") == 1 && ops.count("release") == 0, "foreground grant exhausted deadline but helper still resumed; recheck deadline after grant before resume");
}
static void resumeConsumesRemainder() {
    tv::Dispatcher dispatcher;FakeOps ops;ops.cost["resume"]=1;
    const auto result=request(dispatcher,ops,ops.clock+1);
    check(result.status==tv::Status::Expired && !result.executionReleased && ops.count("release")==0,"resume consumed deadline but Shell was released");
    ops.sound();
}
static void readinessFailures() {
    for(const std::string failure:{"ready","readyTimeout","earlyExit","release"}){
        tv::Dispatcher dispatcher;FakeOps ops;ops.fail=failure;
        const auto result=request(dispatcher,ops,ops.clock+4500);
        const auto expected=failure=="readyTimeout"?tv::Status::TimedOut:failure=="release"?tv::Status::ReleaseFailed:tv::Status::ReadyFailed;
        check(result.status==expected && result.cleanupVerified && !result.executionReleased,"readiness/release failure reached Shell or leaked");
        check(ops.count("allowForeground")==unsigned(failure=="release"),"grant attempted before ready");
        ops.sound();ops.fail.clear();assertSuccess(request(dispatcher,ops,ops.clock+4500));ops.sound();
    }
}
static void readinessConsumesCallBudget() {
    tv::Dispatcher dispatcher;FakeOps ops;ops.cost["ready"]=500;
    const auto result=request(dispatcher,ops,ops.clock+4500);
    assertSuccess(result);check(ops.waitBudgets.front()==2500,"initialization did not share the existing 3000ms helper budget");ops.sound();
}

static void cleanupUsesRemainder() {
    tv::Dispatcher dispatcher; FakeOps ops; ops.autoComplete = false;
    ops.waits = {{WAIT_TIMEOUT,1},{WAIT_TIMEOUT,0},{WAIT_OBJECT_0,500}};
    const auto result = request(dispatcher, ops, ops.clock + 1);
    check(result.status == tv::Status::TimedOut && result.cleanupVerified && result.elapsedMs == 501, "one-ms request did not retain separate bounded cleanup allowance");
    check(ops.waitBudgets == std::vector<DWORD>({1,0,500}), "request/cleanup budget mixed"); ops.sound();
}
static void pendingRecoveryConsumesDeadline() {
    tv::Dispatcher dispatcher; FakeOps ops; ops.autoComplete = false; ops.terminateAccepted = false;
    const auto first = request(dispatcher, ops, ops.clock + 4500);
    check(first.cleanupRetained && ops.processes.size() == 1, "pending setup");
    ops.terminateAccepted = true; ops.terminateExits = false;
    ops.waits = {{WAIT_TIMEOUT,0},{WAIT_OBJECT_0,500}};
    const auto result = request(dispatcher, ops, ops.clock + 500);
    check(result.status == tv::Status::Expired && !result.helperStarted && !result.cleanupRetained && ops.count("start") == 1, "cleanup consumed deadline but dispatched another helper"); ops.sound();
    ops.autoComplete = true; assertSuccess(request(dispatcher, ops, ops.clock + 4500)); ops.sound();
}
static void cleanupWaitFailedThenRecovery() {
    tv::Dispatcher dispatcher; FakeOps ops; ops.autoComplete = false;
    ops.terminateExits = false; ops.waits = {{WAIT_FAILED,0},{WAIT_FAILED,0},{WAIT_FAILED,0}};
    const auto first = request(dispatcher, ops, ops.clock + 4500);
    check(first.status == tv::Status::CleanupUnproven && first.errorHint == FakeOps::waitError && first.cleanupRetained && !first.waitTimedOut, "WAIT_FAILED cleanup evidence lost"); ops.sound(false);
    ops.markExited(ops.processes.front()); ops.autoComplete = true;
    assertSuccess(request(dispatcher, ops, ops.clock + 4500)); ops.sound();
}
static void terminationRejectedButChildExited() {
    tv::Dispatcher dispatcher; FakeOps ops; ops.autoComplete = false; ops.terminateAccepted = false;
    ops.waits = {{WAIT_TIMEOUT,3000},{WAIT_TIMEOUT,0},{WAIT_OBJECT_0,0}};
    const auto result = request(dispatcher, ops, ops.clock + 4500);
    check(result.status == tv::Status::TimedOut && result.terminationAttempted && !result.terminationAccepted && result.cleanupVerified && !result.cleanupRetained, "termination failure treated as proof child is still running"); ops.sound();
    ops.autoComplete = true; assertSuccess(request(dispatcher, ops, ops.clock + 4500)); ops.sound();
}
static void elapsedSaturation() {
    tv::Dispatcher dispatcher; FakeOps ops;
    ops.waits = {{WAIT_OBJECT_0,ULONGLONG(MAXDWORD) + 123}};
    const auto result = request(dispatcher, ops, ops.clock + 4500);
    assertSuccess(result); check(result.elapsedMs == MAXDWORD, "elapsed DWORD overflow"); ops.sound();
}
struct Rendezvous {
    std::mutex mutex; std::condition_variable cv;
    bool entered = false, released = false, secondDone = false, timedOut = false;
    void block() {
        std::unique_lock<std::mutex> lock(mutex); entered = true; cv.notify_all();
        if (!cv.wait_for(lock,std::chrono::seconds(5),[&] { return released; })) timedOut = true;
    }
    bool waitEntered() {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock,std::chrono::seconds(3),[&] { return entered; });
    }
    void done() { std::lock_guard<std::mutex> lock(mutex); secondDone = true; cv.notify_all(); }
    bool waitDone() {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock,std::chrono::seconds(3),[&] { return secondDone; });
    }
    void release() { std::lock_guard<std::mutex> lock(mutex); released = true; cv.notify_all(); }
};
static void busyConcurrency() {
    tv::Dispatcher dispatcher; FakeOps firstOps, busyOps; Rendezvous rendezvous;
    tv::Result first, busy;
    firstOps.waitHook = [&](DWORD) { rendezvous.block(); };
    std::thread firstThread([&] { first = request(dispatcher,firstOps,firstOps.clock + 4500); });
    const bool entered = rendezvous.waitEntered();
    std::thread busyThread([&] { busy = request(dispatcher,busyOps,busyOps.clock + 4500); rendezvous.done(); });
    const bool returnedBeforeRelease = rendezvous.waitDone();
    rendezvous.release(); busyThread.join(); firstThread.join();
    check(entered && returnedBeforeRelease && !rendezvous.timedOut, "Busy waited for active request instead of returning immediately");
    check(busy.status == tv::Status::Busy && busy.elapsedMs == 0 && !busy.helperStarted && !busy.cleanupRetained && busyOps.events.empty(), "Busy touched Ops or misstated result");
    assertSuccess(first); firstOps.sound();
    assertSuccess(request(dispatcher,busyOps,busyOps.clock + 4500)); busyOps.sound();
}
static void synchronousReentry() {
    tv::Dispatcher dispatcher; FakeOps ops, nestedOps; tv::Result nested; bool called = false;
    ops.waitHook = [&](DWORD) { if (!called) { called = true; nested = request(dispatcher,nestedOps,nestedOps.clock + 4500); } };
    assertSuccess(request(dispatcher,ops,ops.clock + 4500));
    check(called && nested.status == tv::Status::Busy && nestedOps.events.empty(), "synchronous reentry blocked or performed Ops"); ops.sound();
    assertSuccess(request(dispatcher,nestedOps,nestedOps.clock + 4500)); nestedOps.sound();
}
static void twoIndependentDispatchers() {
    tv::Dispatcher one,two; FakeOps firstOps,secondOps; bool called = false; tv::Result inner;
    firstOps.waitHook = [&](DWORD) { if (!called) { called = true; inner = request(two,secondOps,secondOps.clock + 4500); } };
    assertSuccess(request(one,firstOps,firstOps.clock + 4500)); assertSuccess(inner);
    check(called, "independent dispatcher request not exercised"); firstOps.sound(); secondOps.sound();
}
static void adversarialMatrix() {
    const std::array<const char*,12> failures{"","attributes","createJob","configureJob","start","assign","resume","ready","readyTimeout","allowForeground","release","exitCode"};
    const std::array<const char*,8> delays{"attributes","createJob","configureJob","start","assign","resume","ready","allowForeground"};
    const std::array<ULONGLONG,7> budgets{0,1,5,100,600,3000,4500};
    const std::array<ULONGLONG,5> costs{0,1,50,3000,60000};
    unsigned scenarios=0;
    for(const auto failure:failures)for(const auto delay:delays)for(const auto budget:budgets)for(const auto cost:costs){
        tv::Dispatcher dispatcher;FakeOps ops;ops.fail=failure;ops.cost[delay]=cost;
        const auto result=request(dispatcher,ops,ops.clock+budget);
        check(!result.cleanupRetained,"bounded matrix unexpectedly retained an owned child");
        check(!result.executionReleased||(result.helperReady&&result.foregroundGrantAccepted),"Shell released without initialized/granted child");
        check(result.status!=tv::Status::Requested||(result.executionReleased&&result.helperExitKnown),"false success without execute receipt");
        if(budget==0)check(!result.helperStarted,"stale queued request created worker");
        if(result.foregroundGrantAttempted)check(result.helperReady,"authorization before initialization");
        if(cost==60000&&(std::string(delay)=="ready"||std::string(delay)=="allowForeground"))check(!result.executionReleased,"wake/slow-init deadline crossing still executed Shell");
        ops.sound();
        ops.fail.clear();ops.cost.clear();assertSuccess(request(dispatcher,ops,ops.clock+4500));ops.sound();
        ++scenarios;
    }
    check(scenarios==3360,"adversarial combination coverage changed");
}
static void burstConcurrency() {
    tv::Dispatcher dispatcher;FakeOps leader;
    std::mutex mx;std::condition_variable cv;bool entered=false,released=false;
    leader.waitHook=[&](DWORD){std::unique_lock<std::mutex> lock(mx);entered=true;cv.notify_all();cv.wait(lock,[&]{return released;});};
    tv::Result first;
    std::thread primary([&]{first=request(dispatcher,leader,leader.clock+4500);});
    {std::unique_lock<std::mutex> lock(mx);cv.wait(lock,[&]{return entered;});}
    std::array<FakeOps,64> contenders;std::array<tv::Result,64> results;std::vector<std::thread> threads;
    for(unsigned i=0;i<contenders.size();++i)threads.emplace_back([&,i]{results[i]=request(dispatcher,contenders[i],contenders[i].clock+4500);});
    for(auto& thread:threads)thread.join();
    {std::lock_guard<std::mutex> lock(mx);released=true;}cv.notify_all();primary.join();
    assertSuccess(first);leader.sound();
    for(unsigned i=0;i<contenders.size();++i){check(results[i].status==tv::Status::Busy&&contenders[i].events.empty(),"rapid repeat created another child or touched OS");}
    FakeOps retry;assertSuccess(request(dispatcher,retry,retry.clock+4500));retry.sound();
}
static void runCase(const char* name, void (*test)()) {
    const auto before = checks;
    try { test(); cases.push_back({{"case",name},{"pass",true},{"checks",checks-before}}); }
    catch (const std::exception& error) { cases.push_back({{"case",name},{"pass",false},{"checks",checks-before},{"error",error.what()}}); }
    catch (...) { cases.push_back({{"case",name},{"pass",false},{"checks",checks-before},{"error","non-standard fixture exception"}}); }
}
}
int main() {
    using namespace fixture;
    runCase("constants-status-names-no-constructor-poll",constantsAndNames);
    runCase("success-and-nonzero-com-helper-exit",successAndComExit);
    runCase("attributes-job-config-start-api-failures",beforeChildApiFailures);
    runCase("assign-resume-exit-query-api-failures",childApiFailures);
    runCase("wait-timeout-failed-unexpected-and-exit-race",waitsAndTermination);
    runCase("timeout-3500-budget-and-successful-retry",timeoutThenRetry);
    runCase("retained-pair-no-second-helper-and-natural-exit-recovery",retainedCleanup);
    runCase("assign-resume-wait-failed-retained-cleanup",failureCleanupRetention);
    runCase("deadline-expired-at-entry",expiredAtEntry);
    runCase("deadline-crossed-during-attributes-no-start",expiredAttributes);
    runCase("deadline-crossed-during-job-create-no-start",expiredCreateJob);
    runCase("deadline-crossed-during-job-config-no-start",expiredConfigureJob);
    runCase("deadline-expired-before-resume",expiresBeforeResume);
    runCase("remaining-deadline-and-3000-call-budget-boundaries",waitBudgetBoundaries);
    runCase("ready-timeout-exit-and-release-failure",readinessFailures);
    runCase("ready-shares-helper-call-budget",readinessConsumesCallBudget);
    runCase("foreground-grant-failure-never-releases-shell-and-retry",grantFailureKeepsUi);
    runCase("foreground-grant-failed-cleanup-debt-blocks-next-helper",grantFailureCleanupDebt);
    runCase("foreground-grant-consumes-deadline-no-shell-release",grantConsumesDeadline);
    runCase("resume-crosses-deadline-zero-call-wait",resumeConsumesRemainder);
    runCase("one-ms-request-plus-500-cleanup-budget",cleanupUsesRemainder);
    runCase("pending-recovery-consumes-request-deadline",pendingRecoveryConsumesDeadline);
    runCase("all-cleanup-waits-failed-then-recovered",cleanupWaitFailedThenRecovery);
    runCase("termination-rejected-but-child-exit-proven",terminationRejectedButChildExited);
    runCase("elapsed-saturates-instead-of-wrapping",elapsedSaturation);
    runCase("concurrent-busy-zero-ops-and-retry",busyConcurrency);
    runCase("synchronous-busy-reentry-and-retry",synchronousReentry);
    runCase("dispatcher-lease-is-instance-local",twoIndependentDispatchers);
    runCase("3360-failure-delay-deadline-combinations-and-recovery",adversarialMatrix);
    runCase("64-simultaneous-repeat-actions-create-only-one-worker",burstConcurrency);
    bool pass = true; for (const auto& row : cases) if (!row["pass"].get<bool>()) pass = false;
    std::cout << json({{"pass",pass},{"checks",checks},{"cases",cases},{"actualOSInput",false},{"productExecuted",false},
        {"actualHelperExecuted",false},{"windowsOpsInvoked",false},{"comInvoked",false},{"allOpsInMemory",true},
        {"threadScope","fixture harness only; dispatcher background work prohibited by source audit"},
        {"modelScope","close calls and process ownership modeled; OS API/COM integration not executed"}}).dump(2) << '\n';
    return pass ? 0 : 1;
}
