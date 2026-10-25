#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "../native/msi_crash_recovery_journal.h"
namespace msiRecovery = ymcc::msi::recovery;
using json = nlohmann::json;

// These guards forward real calls only for this executable and its own child.
// Error leaves are explicit; the extracted product predicates remain untouched.
enum class Leaf { Real, MissingPid, AccessDenied, CreationFailure, WaitFailure };
static Leaf leaf = Leaf::Real;
static DWORD childPid = 0;
static unsigned realOpens = 0, realTimes = 0, realWaits = 0, leafCalls = 0;
static DWORD observedWait = WAIT_FAILED;
static std::string observedCreation;
static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static bool ownedPid(DWORD pid) { return pid == GetCurrentProcessId() || (childPid != 0 && pid == childPid); }
static void requireOwnedHandle(HANDLE handle) {
    require(handle == nullptr || ownedPid(GetProcessId(handle)), "unowned process handle blocked");
}
static HANDLE ownerOpenProcess(DWORD access, BOOL inherit, DWORD pid) {
    require(access == (PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE) && !inherit, "unexpected process access");
    if (leaf == Leaf::MissingPid) {
        require(pid == 0, "missing PID leaf requires zero sentinel");
        ++leafCalls; SetLastError(ERROR_INVALID_PARAMETER); return nullptr;
    }
    require(ownedPid(pid), "unowned PID blocked before OpenProcess");
    if (leaf == Leaf::AccessDenied) { ++leafCalls; SetLastError(ERROR_ACCESS_DENIED); return nullptr; }
    ++realOpens; return ::OpenProcess(access, inherit, pid);
}
static BOOL ownerGetProcessTimes(HANDLE handle, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user) {
    requireOwnedHandle(handle);
    if (leaf == Leaf::CreationFailure) { ++leafCalls; SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    ++realTimes;
    const BOOL result = ::GetProcessTimes(handle, creation, exit, kernel, user);
    if (result) {
        ULARGE_INTEGER value{}; value.LowPart = creation->dwLowDateTime; value.HighPart = creation->dwHighDateTime;
        observedCreation = std::to_string(value.QuadPart);
    }
    return result;
}
static DWORD ownerWaitForSingleObject(HANDLE handle, DWORD timeout) {
    requireOwnedHandle(handle); require(timeout == 0, "product liveness wait must be nonblocking");
    if (leaf == Leaf::WaitFailure) { ++leafCalls; SetLastError(ERROR_ACCESS_DENIED); return observedWait = WAIT_FAILED; }
    ++realWaits; return observedWait = ::WaitForSingleObject(handle, timeout);
}
#define OpenProcess ownerOpenProcess
#define GetProcessTimes ownerGetProcessTimes
#define WaitForSingleObject ownerWaitForSingleObject
#include "actual-owner-functions.inc"
#undef OpenProcess
#undef GetProcessTimes
#undef WaitForSingleObject

static std::string oracleCreation(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    require(::GetProcessTimes(process, &created, &exited, &kernel, &user) != FALSE, "oracle GetProcessTimes failed");
    const auto ticks = (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return std::to_string(ticks);
}
struct PrivateChild {
    HANDLE ready = nullptr, finish = nullptr, process = nullptr;
    DWORD pid = 0;
    ~PrivateChild() {
        if (finish) SetEvent(finish);
        if (process) { ::WaitForSingleObject(process, 25000); CloseHandle(process); }
        if (ready) CloseHandle(ready);
        if (finish) CloseHandle(finish);
    }
    void start() {
        LARGE_INTEGER nonce{}; require(QueryPerformanceCounter(&nonce) != FALSE, "nonce failed");
        const auto prefix = L"Local\\YMCC-MsiOwner-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(nonce.QuadPart);
        const auto readyName = prefix + L"-ready", finishName = prefix + L"-finish";
        ready = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str());
        require(ready && GetLastError() != ERROR_ALREADY_EXISTS, "private ready event collision");
        finish = CreateEventW(nullptr, TRUE, FALSE, finishName.c_str());
        require(finish && GetLastError() != ERROR_ALREADY_EXISTS, "private finish event collision");
        std::vector<wchar_t> executable(32768);
        const DWORD size = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        require(size > 0 && size < executable.size(), "fixture executable path missing");
        std::wstring command = L"\"" + std::wstring(executable.data()) + L"\" --owned-child \"" + readyName + L"\" \"" + finishName + L"\"";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION info{};
        require(CreateProcessW(executable.data(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &startup, &info) != FALSE, "private hidden child creation failed");
        process = info.hProcess; pid = info.dwProcessId; childPid = pid; CloseHandle(info.hThread);
        require(::WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0, "private child ready handshake failed");
    }
};
static int childMain(const wchar_t* readyName, const wchar_t* finishName) {
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, readyName);
    HANDLE finish = OpenEventW(SYNCHRONIZE, FALSE, finishName);
    if (!ready || !finish) return 75;
    const BOOL signalled = SetEvent(ready);
    const DWORD waited = ::WaitForSingleObject(finish, 20000); // Natural bounded exit even if the test parent fails.
    CloseHandle(ready); CloseHandle(finish);
    return signalled && waited == WAIT_OBJECT_0 ? 73 : 74;
}
struct Checks {
    json cases = json::array(), failures = json::array();
    std::string name;
    unsigned assertions = 0, inCase = 0;
    void expect(bool ok, const char* assertion) {
        ++assertions; ++inCase;
        if (!ok) failures.push_back({{"case", name}, {"assertion", assertion}});
    }
    void run(const char* caseName, Leaf mode, const std::function<void()>& body) {
        name = caseName; inCase = 0; leaf = mode;
        const auto failed = failures.size();
        const unsigned opens = realOpens, times = realTimes, waits = realWaits, leaves = leafCalls;
        body();
        cases.push_back({{"name", name}, {"assertions", inCase}, {"passed", failures.size() == failed},
            {"boundary", mode == Leaf::Real ? "private-real-Win32" : "explicit-error-leaf"},
            {"realOpenProcessCalls", realOpens - opens}, {"realGetProcessTimesCalls", realTimes - times},
            {"realWaitCalls", realWaits - waits}, {"explicitLeafCalls", leafCalls - leaves}});
        leaf = Leaf::Real;
    }
};
int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring(argv[1]) == L"--owned-child") return childMain(argv[2], argv[3]);
    try {
        require(argc == 1, "unexpected fixture arguments");
        Checks c;
        msiRecovery::Record current; current.ownerPid = GetCurrentProcessId(); current.ownerCreationTime = oracleCreation(GetCurrentProcess());
        c.run("owner/current-pseudo-handle", Leaf::Real, [&] {
            c.expect(GetCurrentProcess() == reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-1)), "current process uses actual pseudo handle");
            c.expect(!current.ownerCreationTime.empty(), "current creation oracle exists");
            c.expect(msiCrashProcessCreation(GetCurrentProcess()) == current.ownerCreationTime, "original creation equals raw Win32 oracle");
            c.expect(msiCrashCurrentOwner() == std::to_string(current.ownerPid) + ":" + current.ownerCreationTime, "original current owner binds PID and creation");
            c.expect(msiCrashCurrentOwner() == msiCrashCurrentOwner(), "current owner remains stable");
            c.expect(!msiCrashOldOwnerDead(current), "live current owner is not dead");
        });
        PrivateChild child; child.start();
        msiRecovery::Record record; record.ownerPid = child.pid; record.ownerCreationTime = oracleCreation(child.process);
        c.run("owner/live-child", Leaf::Real, [&] {
            c.expect(child.pid != GetCurrentProcessId(), "private child PID differs from current PID");
            c.expect(::WaitForSingleObject(child.process, 0) == WAIT_TIMEOUT, "private child is alive before query");
            c.expect(msiCrashProcessCreation(child.process) == record.ownerCreationTime, "original child creation equals raw Win32 oracle");
            c.expect(!msiCrashOldOwnerDead(record), "live child owner is not dead");
            c.expect(observedWait == WAIT_TIMEOUT && observedCreation == record.ownerCreationTime, "real opened child matches creation and is unsignalled");
        });
        auto mismatch = record; mismatch.ownerCreationTime = std::to_string(std::stoull(record.ownerCreationTime) + 1);
        c.run("owner/live-child-creation-mismatch", Leaf::Real, [&] {
            c.expect(mismatch.ownerCreationTime != record.ownerCreationTime, "record creation mismatch is deliberate");
            c.expect(msiCrashOldOwnerDead(mismatch), "creation mismatch retires old identity while child lives");
            c.expect(observedWait == WAIT_TIMEOUT, "creation mismatch does not depend on process exit");
        });
        auto currentMismatch = current; currentMismatch.ownerCreationTime = std::to_string(std::stoull(current.ownerCreationTime) + 1);
        c.run("owner/current-creation-mismatch", Leaf::Real, [&] {
            c.expect(msiCrashOldOwnerDead(currentMismatch), "current PID with old creation is a different owner");
            c.expect(observedWait == WAIT_TIMEOUT, "current process is alive during mismatch query");
        });
        c.run("owner/exited-child-retained-handle", Leaf::Real, [&] {
            c.expect(SetEvent(child.finish) != FALSE, "only own child receives natural exit event");
            c.expect(::WaitForSingleObject(child.process, 10000) == WAIT_OBJECT_0, "own child exits naturally");
            DWORD exitCode = 0; require(GetExitCodeProcess(child.process, &exitCode) != FALSE, "child exit receipt missing");
            c.expect(exitCode == 73, "own child returns expected natural exit code");
            c.expect(msiCrashProcessCreation(child.process) == record.ownerCreationTime, "retained child handle keeps original creation");
            c.expect(msiCrashOldOwnerDead(record), "naturally exited child owner is dead");
            c.expect(observedWait == WAIT_OBJECT_0 && observedCreation == record.ownerCreationTime, "original function opens retained exited child and observes signal");
        });
        c.run("owner/invalid-handle-real-api", Leaf::Real, [&] {
            c.expect(msiCrashProcessCreation(nullptr).empty(), "real null process handle has no creation");
            c.expect(GetLastError() == ERROR_INVALID_HANDLE, "real invalid handle retains Win32 error");
        });
        c.run("leaf/missing-pid", Leaf::MissingPid, [&] {
            auto missing = current; missing.ownerPid = 0;
            c.expect(msiCrashOldOwnerDead(missing), "explicit no-process leaf reports dead");
            c.expect(GetLastError() == ERROR_INVALID_PARAMETER, "missing PID error is explicit leaf evidence");
        });
        c.run("leaf/access-denied", Leaf::AccessDenied, [&] {
            c.expect(!msiCrashOldOwnerDead(current), "access denied does not prove death");
            c.expect(GetLastError() == ERROR_ACCESS_DENIED, "access denied remains explicit leaf");
        });
        c.run("leaf/process-times-failure", Leaf::CreationFailure, [&] {
            c.expect(!msiCrashOldOwnerDead(currentMismatch), "unreadable creation cannot prove a different dead owner");
            c.expect(observedWait == WAIT_TIMEOUT, "creation failure still uses a real nonblocking own-process wait");
        });
        c.run("leaf/current-creation-failure", Leaf::CreationFailure, [&] {
            c.expect(msiCrashProcessCreation(GetCurrentProcess()).empty(), "creation failure returns empty identity");
            c.expect(msiCrashCurrentOwner().empty(), "current owner cannot publish PID without creation");
        });
        c.run("leaf/wait-failure-matching-creation", Leaf::WaitFailure, [&] {
            c.expect(!msiCrashOldOwnerDead(current), "wait failure alone does not prove death");
            c.expect(observedWait == WAIT_FAILED, "wait failure is explicit leaf");
        });
        c.run("leaf/wait-failure-creation-mismatch", Leaf::WaitFailure, [&] {
            c.expect(msiCrashOldOwnerDead(currentMismatch), "proven creation mismatch retires old identity despite wait failure");
            c.expect(observedWait == WAIT_FAILED, "mismatch wait failure remains explicit leaf");
        });
        const json report = {{"schemaVersion", 1}, {"passed", c.failures.empty()}, {"tests", c.cases.size()},
            {"assertions", c.assertions}, {"cases", c.cases}, {"failures", c.failures},
            {"currentPid", current.ownerPid}, {"currentCreation", current.ownerCreationTime},
            {"childPid", child.pid}, {"childCreation", record.ownerCreationTime}, {"childNaturalExitCode", 73},
            {"scope", "private-real-Win32-process-identity-and-natural-exit;explicit-error-leaves"},
            {"productRun", false}, {"hardwareAccess", false}, {"foreignProcessQueries", 0}};
        std::cout << report.dump(2) << '\n';
        return c.failures.empty() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "MSI_OWNER_INFRASTRUCTURE_FAILURE: " << error.what() << '\n'; return 2;
    }
}
