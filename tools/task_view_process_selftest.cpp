// Real WindowsOps sidecar. Only this executable's fixed inert child modes run.
// This is process/job lifetime evidence, never Task View/API/input acceptance.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <processsnapshot.h>
#include <array>
#include <cstdio>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
#include "../native/task_view_dispatch.h"

namespace tv = ymcc::taskView;
using json = nlohmann::json;
namespace {
constexpr DWORD inertError = 0x80070005u;
constexpr DWORD inertSleepMs = 10000; // Independent natural-exit bound; <= 30 seconds.
static_assert(inertSleepMs <= 30000);
unsigned checks = 0;
void require(bool okay, const char* why) {
    ++checks;
    if (!okay) throw std::runtime_error(why);
}
std::wstring selfPath() {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    require(length && length < path.size(), "cannot obtain exact fixture path");
    return std::wstring(path.data(), length);
}
std::string utf8(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    require(size > 0, "UTF-8 length failed");
    std::string buffer(static_cast<size_t>(size), '\0');
    require(WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, buffer.data(), size, nullptr, nullptr) == size,
            "UTF-8 conversion failed");
    buffer.pop_back();
    return buffer;
}
DWORD handleCount() {
    DWORD count = 0;
    require(GetProcessHandleCount(GetCurrentProcess(), &count) != FALSE, "handle count failed");
    return count;
}
struct Handle {
    HANDLE value = nullptr;
    ~Handle() noexcept { if (value) CloseHandle(value); }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void reset() noexcept { if (value) CloseHandle(value); value = nullptr; }
};

// Read only this parent process's handle metadata. No object names or paths are
// recorded. PSS does not clone a process (no PSS_CAPTURE_VA_CLONE flag).
json handleMetadata() {
    struct Snapshot {
        HPSS snapshot = nullptr;
        HPSSWALK marker = nullptr;
        ~Snapshot() noexcept {
            if (marker) PssWalkMarkerFree(marker);
            if (snapshot) PssFreeSnapshot(GetCurrentProcess(), snapshot);
        }
    } owner;
    require(PssCaptureSnapshot(GetCurrentProcess(), PSS_CAPTURE_HANDLES |
        PSS_CAPTURE_HANDLE_BASIC_INFORMATION | PSS_CAPTURE_HANDLE_TYPE_SPECIFIC_INFORMATION,
        0, &owner.snapshot) == ERROR_SUCCESS, "self handle snapshot failed");
    require(PssWalkMarkerCreate(nullptr, &owner.marker) == ERROR_SUCCESS, "self handle marker failed");
    json entries = json::object();
    PSS_HANDLE_ENTRY entry{};
    DWORD status = ERROR_SUCCESS;
    while ((status = PssWalkSnapshot(owner.snapshot, PSS_WALK_HANDLES, owner.marker,
                                     &entry, sizeof(entry))) == ERROR_SUCCESS) {
        json row = {{"type", static_cast<unsigned>(entry.ObjectType)}};
        if (entry.TypeName && entry.TypeNameLength)
            row["typeName"] = utf8(std::wstring(entry.TypeName, entry.TypeNameLength / sizeof(wchar_t)));
        if (entry.ObjectType == PSS_OBJECT_TYPE_PROCESS)
            row["processId"] = entry.TypeSpecificInformation.Process.ProcessId;
        if (entry.ObjectType == PSS_OBJECT_TYPE_THREAD)
            row["threadProcessId"] = entry.TypeSpecificInformation.Thread.ProcessId;
        entries[std::to_string(reinterpret_cast<std::uintptr_t>(entry.Handle))] = std::move(row);
    }
    require(status == ERROR_NO_MORE_ITEMS, "self handle walk failed");
    return entries;
}
// Tracking wrappers forward all production operations to WindowsOps. The only
// behavioral substitution is allowForeground: no call to the real User32 API.
// Fixed arrays avoid allocations/throws in the production Ops noexcept contract.
struct GuardOps : tv::WindowsOps {
    enum class Kind { Job, Process, Thread, Observer };
    struct Entry { HANDLE handle = nullptr; Kind kind = Kind::Job; bool live = false; };
    std::array<Entry, 8> handles{};
    std::wstring self, expectedCommand;
    DWORD pid = 0;
    HANDLE observer = nullptr;
    bool sound = true, assigned = false, resumed = false, cleaned = false;
    bool independentExited = false, exitKnown = false, cleanupOkay = true;
    DWORD independentExit = STILL_ACTIVE, observedJobFlags = 0;
    unsigned jobs = 0, starts = 0, assigns = 0, resumes = 0, grantsSuppressed = 0;
    unsigned terminations = 0, emergencyTerminations = 0, productionCloses = 0;
    unsigned liveOriginalsBeforeGuard = 0, waits = 0;
    DWORD maxProductionWait = 0;
    bool observerDuplicate = false, singleProcessInJob = false;
    DWORD handlesBeforeCreate = 0, handlesAfterCreate = 0, handlesAfterAssign = 0;
    DWORD handlesAfterResume = 0;
    std::array<ULONG_PTR, 16> guardianPids{};
    DWORD guardianPidCount = 0;
    explicit GuardOps(const std::wstring& path, const wchar_t* mode)
        : self(path), expectedCommand(L"\"" + path + L"\" " + mode) {}
    ~GuardOps() noexcept { cleanup(); }
    bool owns(HANDLE handle, Kind kind) const noexcept {
        for (const auto& entry : handles)
            if (entry.live && entry.handle == handle && entry.kind == kind) return true;
        return false;
    }
    void adopt(HANDLE handle, Kind kind) noexcept {
        if (!handle) { sound = false; return; }
        for (auto& entry : handles) {
            if (!entry.handle) { entry = {handle, kind, true}; return; }
        }
        // Fixed test creates at most four handles; overflow is an invariant error.
        sound = false;
        CloseHandle(handle);
    }
    HANDLE createJob() noexcept {
        ++jobs;
        HANDLE job = tv::WindowsOps::createJob();
        if (job) adopt(job, Kind::Job);
        return job;
    }
    bool configureJob(HANDLE job) noexcept {
        if (!owns(job, Kind::Job)) { sound = false; SetLastError(ERROR_INVALID_HANDLE); return false; }
        const bool okay = tv::WindowsOps::configureJob(job);
        if (okay) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            if (!QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info), nullptr))
                sound = false;
            observedJobFlags = info.BasicLimitInformation.LimitFlags;
        }
        return okay;
    }
    bool start(const wchar_t* helper, wchar_t* command, PROCESS_INFORMATION& pi) noexcept {
        ++starts;
        if (pid || std::wcscmp(helper, self.c_str()) || std::wcscmp(command, expectedCommand.c_str())) {
            sound = false; SetLastError(ERROR_ACCESS_DENIED); return false;
        }
        GetProcessHandleCount(GetCurrentProcess(), &handlesBeforeCreate);
        if (!tv::WindowsOps::start(helper, command, pi)) return false;
        GetProcessHandleCount(GetCurrentProcess(), &handlesAfterCreate);
        pid = pi.dwProcessId;
        struct Pids { DWORD assigned = 0, count = 0; ULONG_PTR values[16]{}; } pids;
        if (!QueryInformationJobObject(nullptr, JobObjectBasicProcessIdList, &pids, sizeof(pids), nullptr)) {
            sound = false;
        } else {
            guardianPidCount = pids.count;
            bool parentSeen = false, childSeen = false;
            for (DWORD i = 0; i < pids.count && i < guardianPids.size(); ++i) {
                guardianPids[i] = pids.values[i];
                parentSeen = parentSeen || pids.values[i] == GetCurrentProcessId();
                childSeen = childSeen || pids.values[i] == pid;
            }
            sound = sound && pids.count == 2 && parentSeen && childSeen;
        }
        adopt(pi.hProcess, Kind::Process);
        adopt(pi.hThread, Kind::Thread);
        std::fprintf(stdout, "{\"event\":\"child-created\",\"parentPid\":%lu,\"pid\":%lu}\n",
                     GetCurrentProcessId(), pid);
        std::fflush(stdout);
        observerDuplicate = DuplicateHandle(GetCurrentProcess(), pi.hProcess, GetCurrentProcess(),
                                             &observer, 0, FALSE, DUPLICATE_SAME_ACCESS) != FALSE;
        if (observerDuplicate) adopt(observer, Kind::Observer);
        else sound = false;
        if (GetProcessId(pi.hProcess) != pid) sound = false;
        // The thread is not signaled; CREATE_SUSPENDED is separately source-audited.
        if (WaitForSingleObject(pi.hThread, 0) != WAIT_TIMEOUT) sound = false;
        return true;
    }
    bool assign(HANDLE job, HANDLE process) noexcept {
        ++assigns;
        if (!owns(job, Kind::Job) || !owns(process, Kind::Process) || GetProcessId(process) != pid) {
            sound = false; SetLastError(ERROR_INVALID_HANDLE); return false;
        }
        const bool okay = tv::WindowsOps::assign(job, process);
        GetProcessHandleCount(GetCurrentProcess(), &handlesAfterAssign);
        if (okay) {
            BOOL member = FALSE;
            assigned = IsProcessInJob(process, job, &member) && member;
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
            singleProcessInJob = QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                &accounting, sizeof(accounting), nullptr) && accounting.ActiveProcesses == 1;
            sound = sound && assigned && singleProcessInJob;
        }
        return okay;
    }
    bool allowForeground(DWORD childPid) noexcept {
        ++grantsSuppressed;
        sound = sound && childPid == pid && assigned;
        return true; // In-memory grant acceptance; never tv::WindowsOps::allowForeground().
    }
    bool resume(HANDLE thread) noexcept {
        ++resumes;
        if (!assigned || !owns(thread, Kind::Thread)) {
            sound = false; SetLastError(ERROR_INVALID_HANDLE); return false;
        }
        resumed = tv::WindowsOps::resume(thread);
        GetProcessHandleCount(GetCurrentProcess(), &handlesAfterResume);
        return resumed;
    }
    DWORD wait(HANDLE process, DWORD ms) noexcept {
        ++waits;
        if (!owns(process, Kind::Process) || ms == INFINITE || ms > tv::callBudgetMs) {
            sound = false; SetLastError(ERROR_INVALID_PARAMETER); return WAIT_FAILED;
        }
        maxProductionWait = (std::max)(maxProductionWait, ms);
        return tv::WindowsOps::wait(process, ms);
    }
    bool exitCode(HANDLE process, DWORD& code) noexcept {
        if (!owns(process, Kind::Process)) { sound = false; SetLastError(ERROR_INVALID_HANDLE); return false; }
        return tv::WindowsOps::exitCode(process, code);
    }
    bool terminate(HANDLE process) noexcept {
        if (!owns(process, Kind::Process) || GetProcessId(process) != pid) {
            sound = false; SetLastError(ERROR_INVALID_HANDLE); return false;
        }
        ++terminations;
        return tv::WindowsOps::terminate(process);
    }
    void close(HANDLE handle) noexcept {
        if (!handle) return;
        const DWORD previousError = GetLastError();
        for (auto& entry : handles) {
            if (entry.live && entry.handle == handle && entry.kind != Kind::Observer) {
                tv::WindowsOps::close(handle);
                ++productionCloses;
                entry.live = false;
                DWORD flags = 0;
                if (GetHandleInformation(handle, &flags) || GetLastError() != ERROR_INVALID_HANDLE) sound = false;
                SetLastError(previousError);
                return;
            }
        }
        sound = false;
        SetLastError(previousError);
    }
    void observeExit() noexcept {
        if (!observer) { sound = false; return; }
        independentExited = WaitForSingleObject(observer, 0) == WAIT_OBJECT_0;
        exitKnown = GetExitCodeProcess(observer, &independentExit) != FALSE;
    }
    void cleanup() noexcept {
        if (cleaned) return;
        cleaned = true;
        const auto record = [&](const char* operation, DWORD result, DWORD error) noexcept {
            if (!pid) return;
            std::fprintf(stdout,
                "{\"event\":\"guard-cleanup-step\",\"pid\":%lu,\"operation\":\"%s\",\"result\":%lu,\"error\":%lu}\n",
                pid, operation, result, error);
            std::fflush(stdout);
        };
        for (const auto& entry : handles)
            if (entry.live && entry.kind != Kind::Observer) ++liveOriginalsBeforeGuard;
        // An exception before assignment still has a tracked process observer.
        // Closing a remaining owned job also kills any assigned inert child.
        for (auto& entry : handles) {
            if (entry.live && entry.kind == Kind::Job) {
                const BOOL closed = CloseHandle(entry.handle);
                record("close-job", closed, closed ? ERROR_SUCCESS : GetLastError());
                cleanupOkay = closed && cleanupOkay;
                entry.live = false;
            }
        }
        HANDLE proof = observer;
        if (!proof)
            for (const auto& entry : handles)
                if (entry.live && entry.kind == Kind::Process) proof = entry.handle;
        if (proof) {
            const DWORD before = WaitForSingleObject(proof, 0);
            record("wait-before-terminate", before, before == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS);
            if (before != WAIT_OBJECT_0) {
                ++emergencyTerminations;
                const BOOL terminated = TerminateProcess(proof, tv::cancelledExit);
                record("terminate-owned-process", terminated, terminated ? ERROR_SUCCESS : GetLastError());
                if (!terminated) {
                    const DWORD after = WaitForSingleObject(proof, 0);
                    record("wait-after-failed-terminate", after, after == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS);
                    // Job close can already be terminating the child: the
                    // zero-time wait is observation only. The bounded wait and
                    // independent exit query below decide whether cleanup worked.
                }
                const DWORD bounded = WaitForSingleObject(proof, 2000);
                record("wait-bounded-exit", bounded, bounded == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS);
                if (bounded != WAIT_OBJECT_0) cleanupOkay = false;
            }
            const DWORD verified = WaitForSingleObject(proof, 0);
            record("verify-exit", verified, verified == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS);
            independentExited = verified == WAIT_OBJECT_0;
            exitKnown = GetExitCodeProcess(proof, &independentExit) != FALSE;
            record("query-exit-code", exitKnown ? 1 : 0, exitKnown ? ERROR_SUCCESS : GetLastError());
            cleanupOkay = cleanupOkay && independentExited && exitKnown;
        }
        for (auto& entry : handles) {
            if (entry.live) {
                const BOOL closed = CloseHandle(entry.handle);
                record(entry.kind == Kind::Process ? "close-process" :
                       entry.kind == Kind::Thread ? "close-thread" : "close-observer",
                       closed, closed ? ERROR_SUCCESS : GetLastError());
                cleanupOkay = closed && cleanupOkay;
                entry.live = false;
            }
        }
        if (pid) {
            std::fprintf(stdout,
                "{\"event\":\"guard-cleanup\",\"pid\":%lu,\"exitVerified\":%s,\"exitCode\":%lu,\"clean\":%s}\n",
                pid, independentExited ? "true" : "false", independentExit, cleanupOkay ? "true" : "false");
            std::fflush(stdout);
        }
    }
    size_t liveHandles() const noexcept {
        size_t count = 0;
        for (const auto& entry : handles) if (entry.live) ++count;
        return count;
    }
};

json requestCase(tv::Dispatcher& dispatcher, const std::wstring& path, const char* label,
                 const wchar_t* mode, DWORD budget, tv::Status expected, DWORD expectedExit, bool initialization = false) {
    const json metadataBefore = initialization ? handleMetadata() : json::object();
    const DWORD before = handleCount();
    GuardOps ops(path, mode);
    std::wstring command = ops.expectedCommand;
    const auto result = dispatcher.request(ops, path.c_str(), command.data(), GetTickCount64() + budget);
    ops.observeExit();
    json row = {{"case", label}, {"pid", ops.pid}, {"status", tv::statusName(result.status)},
        {"elapsedMs", result.elapsedMs}, {"requestDeadlineMs", budget},
        {"cleanupVerified", result.cleanupVerified}, {"cleanupRetained", result.cleanupRetained},
        {"terminationAttempted", result.terminationAttempted}, {"terminationAccepted", result.terminationAccepted},
        {"helperExitKnown", result.helperExitKnown}, {"helperExitCode", result.helperExitCode},
        {"independentExitCode", ops.independentExit}, {"independentExitVerified", ops.independentExited},
        {"foregroundGrantAccepted", result.foregroundGrantAccepted}, {"jobFlags", ops.observedJobFlags},
        {"jobHasOnlyThisChild", ops.singleProcessInJob}, {"realWaitCalls", ops.waits},
        {"maxProductionWaitMs", ops.maxProductionWait},
        {"handlesBeforeCreate", ops.handlesBeforeCreate}, {"handlesAfterCreate", ops.handlesAfterCreate},
        {"handlesAfterAssign", ops.handlesAfterAssign}, {"handlesAfterResume", ops.handlesAfterResume},
        {"initialCreateProcessObservation", initialization}, {"guardianActivePidCount", ops.guardianPidCount},
        {"guardianActivePidsAfterCreate", json::array()}};
    for (DWORD i = 0; i < ops.guardianPidCount && i < ops.guardianPids.size(); ++i)
        row["guardianActivePidsAfterCreate"].push_back(ops.guardianPids[i]);
    const bool normalProof = ops.sound && ops.observerDuplicate && ops.independentExited && ops.exitKnown;
    ops.cleanup();
    const DWORD after = handleCount();
    row.update({{"handlesBefore", before}, {"handlesAfter", after},
        {"openTrackedHandles", ops.liveHandles()}, {"productionHandlesClosed", ops.productionCloses},
        {"productionHandlesRemainingBeforeGuard", ops.liveOriginalsBeforeGuard},
        {"guardEmergencyTerminations", ops.emergencyTerminations}});
    if (initialization) {
        const json metadataAfter = handleMetadata();
        row["initialHandleMetadataAdded"] = json::object();
        for (const auto& entry : metadataAfter.items())
            if (!metadataBefore.contains(entry.key())) row["initialHandleMetadataAdded"][entry.key()] = entry.value();
    }
    row["event"] = "case-observation";
    std::printf("%s\n", row.dump().c_str());
    std::fflush(stdout);
    require(normalProof, "independent child/job/operation observation failed");
    require(result.helperStarted && result.status == expected, "unexpected dispatcher result");
    require(result.cleanupVerified && !result.cleanupRetained, "production cleanup was not verified");
    require(ops.cleanupOkay && !ops.liveHandles(), "owned handle leak or cleanup failure");
    if (!initialization) require(before == after, "handle count grew across a real process request");
    require(!ops.liveOriginalsBeforeGuard && !ops.emergencyTerminations && ops.productionCloses == 3,
            "RAII guard had to repair normal dispatcher cleanup");
    require(ops.jobs == 1 && ops.starts == 1 && ops.assigns == 1 && ops.resumes == 1 && ops.grantsSuppressed == 1,
            "unexpected process/job call counts");
    require(ops.assigned && ops.resumed && result.foregroundGrantAccepted, "foreground substitution or job assignment failed");
    require((ops.observedJobFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) != 0, "job has no kill-on-close limit");
    require(ops.independentExit == expectedExit, "child DWORD exit code changed");
    if (expected == tv::Status::TimedOut) {
        require(result.waitTimedOut && result.terminationAttempted && result.terminationAccepted && ops.terminations == 1,
                "timeout did not terminate the exact owned child");
        const DWORD bounded = (std::min)(budget, tv::callBudgetMs);
        require(result.elapsedMs >= bounded - (std::min)(bounded, DWORD(40)), "timeout returned too early");
        require(result.elapsedMs <= bounded + tv::cleanupBudgetMs + 1500, "timeout exceeded scheduling allowance");
    } else {
        require(result.helperExitKnown && result.helperExitCode == expectedExit,
                "dispatcher did not preserve nonzero DWORD exit code");
        require(!result.waitTimedOut && !result.terminationAttempted && ops.terminations == 0,
                "already completed child was terminated");
    }
    row["pass"] = true;
    return row;
}

json unwindCase(const std::wstring& path) {
    const DWORD before = handleCount();
    DWORD pid = 0;
    Handle externalObserver;
    bool caught = false;
    try {
        GuardOps ops(path, L"--inert-wait");
        HANDLE job = ops.createJob();
        require(job && ops.configureJob(job), "unwind job setup failed");
        PROCESS_INFORMATION pi{};
        std::wstring command = ops.expectedCommand;
        require(ops.start(path.c_str(), command.data(), pi), "unwind child creation failed");
        pid = pi.dwProcessId;
        require(DuplicateHandle(GetCurrentProcess(), pi.hProcess, GetCurrentProcess(),
            &externalObserver.value, 0, FALSE, DUPLICATE_SAME_ACCESS) != FALSE, "external unwind observer failed");
        require(ops.assign(job, pi.hProcess) && ops.resume(pi.hThread), "unwind child setup failed");
        throw 42; // Deliberate parent-scope unwind; Ops destructor must retire child.
    } catch (int sentinel) {
        caught = sentinel == 42;
    }
    require(caught && externalObserver.value, "unwind sentinel not caught");
    require(WaitForSingleObject(externalObserver.value, 2000) == WAIT_OBJECT_0, "unwind left live child");
    DWORD exit = STILL_ACTIVE;
    require(GetExitCodeProcess(externalObserver.value, &exit) && exit != STILL_ACTIVE,
            "unwind child exit not verified");
    externalObserver.reset();
    const DWORD after = handleCount();
    require(before == after, "unwind handle leak");
    return {{"case", "parent-scope-exception-raii"}, {"pass", true}, {"pid", pid},
        {"independentExitCode", exit}, {"independentExitVerified", true},
        {"handlesBefore", before}, {"handlesAfter", after}};
}
} // namespace

// GUI subsystem prevents implicit console-host descendants; the runner supplies
// file-backed standard handles for JSON evidence. The raw argument tail accepts
// only a single fixed mode, so no Shell command-line parser/import is needed.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int) {
    if (*arguments) {
        const std::wstring tail=arguments;
        const auto first=tail.find(L' '),second=first==std::wstring::npos?first:tail.find(L' ',first+1);
        if(first==std::wstring::npos||second==std::wstring::npos)return static_cast<int>(ERROR_INVALID_PARAMETER);
        const auto mode=tail.substr(0,first),readyName=tail.substr(first+1,second-first-1),executeName=tail.substr(second+1);
        if(mode!=L"--inert-wait"&&mode!=L"--inert-success"&&mode!=L"--inert-error")return static_cast<int>(ERROR_INVALID_PARAMETER);
        HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,readyName.c_str());
        HANDLE execute=OpenEventW(SYNCHRONIZE,FALSE,executeName.c_str());
        if(!ready||!execute){if(ready)CloseHandle(ready);if(execute)CloseHandle(execute);return static_cast<int>(ERROR_INVALID_HANDLE);}
        // Kernel event handshake only. No User32/COM/Shell/input in this inert child.
        const bool signalled=SetEvent(ready)!=FALSE;
        const DWORD wait=signalled?WaitForSingleObject(execute,4500):WAIT_FAILED;
        CloseHandle(ready);CloseHandle(execute);
        if(wait!=WAIT_OBJECT_0)return static_cast<int>(ERROR_TIMEOUT);
        if(mode==L"--inert-wait"){Sleep(inertSleepMs);return 0;}
        return mode==L"--inert-error"?static_cast<int>(inertError):0;
    }
    json report = {{"event", "result"}, {"pass", false}, {"parentPid", GetCurrentProcessId()},
        {"scope", "Only this fixture executable's fixed inert children; actual WindowsOps process/job cleanup"},
        {"productionHelperExecuted", false}, {"comInvoked", false}, {"inputInvoked", false},
        {"foregroundApiInvoked", false}, {"cases", json::array()}};
    std::fputs("{\"event\":\"scope\",\"actualProcessOperations\":true,\"onlySelfInertChildren\":true}\n", stdout);
    std::fflush(stdout); // Warm CRT output before handle-count baselines.
    try {
        const auto path = selfPath();
        report["fixturePath"] = utf8(path);
        tv::Dispatcher sameDispatcher;
        report["cases"].push_back(requestCase(sameDispatcher, path, "first-createprocess-observation", L"--inert-success", 3000,
                                             tv::Status::Requested, 0, true));
        const DWORD before = handleCount();
        report["cases"].push_back(requestCase(sameDispatcher, path, "short-timeout", L"--inert-wait", 200,
                                             tv::Status::TimedOut, tv::cancelledExit));
        report["cases"].push_back(requestCase(sameDispatcher, path, "same-dispatcher-success-after-timeout", L"--inert-success", 3000,
                                             tv::Status::Requested, 0));
        report["cases"].push_back(requestCase(sameDispatcher, path, "hresult-dword-exit-preserved", L"--inert-error", 3000,
                                             tv::Status::ApiFailed, inertError));
#ifndef _DEBUG
        report["cases"].push_back(requestCase(sameDispatcher, path, "production-three-second-call-cap", L"--inert-wait", 4500,
                                             tv::Status::TimedOut, tv::cancelledExit));
#endif
        report["cases"].push_back(unwindCase(path));
        const DWORD after = handleCount();
        report["handlesBeforeSuite"] = before;
        report["handlesAfterSuite"] = after;
        require(before == after, "suite handle count grew");
        report["pass"] = true;
    } catch (const std::exception& error) {
        report["error"] = error.what();
    } catch (...) {
        report["error"] = "unexpected parent-scope exception";
    }
    report["checks"] = checks;
    std::printf("%s\n", report.dump().c_str());
    std::fflush(stdout);
    return report["pass"].get<bool>() ? 0 : 1;
}