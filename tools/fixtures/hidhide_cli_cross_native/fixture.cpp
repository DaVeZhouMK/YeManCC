#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
#include "process_efficiency.h"
using J = nlohmann::json;
static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    require(n > 0, "UTF8 length"); std::string out(static_cast<size_t>(n), '\0');
    require(WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), n, nullptr, nullptr) == n, "UTF8 conversion"); return out;
}
static std::wstring ownImage() {
    std::vector<wchar_t> buf(32768); const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    require(n && n < buf.size(), "own image"); return {buf.data(), n};
}
static J identity(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    require(GetProcessTimes(process, &created, &exited, &kernel, &user) != FALSE, "actual process birth");
    std::vector<wchar_t> image(32768); DWORD n = static_cast<DWORD>(image.size());
    const auto birth = (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    const DWORD pid = GetProcessId(process); const auto key = std::make_pair(pid, birth);
    static std::map<std::pair<DWORD, std::uint64_t>, std::string> capturedImages;
    const BOOL queried = QueryFullProcessImageNameW(process, 0, image.data(), &n); const DWORD queryError = queried ? 0 : GetLastError();
    if (queried) capturedImages[key] = utf8(std::wstring(image.data(), n));
    require(capturedImages.count(key) != 0, "process image requires successful live observation of exact PID+birth");
    return {{"pid", pid}, {"birth", std::to_string(birth)}, {"birthHigh", created.dwHighDateTime}, {"birthLow", created.dwLowDateTime}, {"image", capturedImages[key]},
        {"imageQuerySucceededNow", queried != FALSE}, {"imageQueryError", queryError},
        {"imageEvidence", queried ? "current actual query" : "prior successful query on exact PID+birth; process handle still owned"},
        {"handleLocalValue", std::to_string(reinterpret_cast<std::uintptr_t>(process))}};
}
static DWORD parentPid() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); require(snap != INVALID_HANDLE_VALUE, "process snapshot");
    PROCESSENTRY32W row{}; row.dwSize = sizeof(row); DWORD result = 0;
    if (Process32FirstW(snap, &row)) do { if (row.th32ProcessID == GetCurrentProcessId()) { result = row.th32ParentProcessID; break; } } while (Process32NextW(snap, &row));
    CloseHandle(snap); require(result != 0, "actual parent PID"); return result;
}
static std::wstring file(const std::wstring& root, const std::wstring& leaf) { return root + L"\\" + leaf; }
static std::string readBytes(HANDLE h) {
    LARGE_INTEGER size{}; require(GetFileSizeEx(h, &size) && size.QuadPart >= 0 && size.QuadPart < 1024 * 1024, "private JSON size");
    std::string data(static_cast<size_t>(size.QuadPart), '\0'); DWORD n = 0;
    if (!data.empty()) require(ReadFile(h, data.data(), static_cast<DWORD>(data.size()), &n, nullptr) && n == data.size(), "private JSON read");
    return data;
}
static J load(const std::wstring& p) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(h != INVALID_HANDLE_VALUE, "open private JSON");
    std::string data; try { data = readBytes(h); } catch (...) { CloseHandle(h); throw; } CloseHandle(h); return J::parse(data);
}
static void writeBytes(HANDLE h, const std::string& data) {
    DWORD n = 0; require(WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &n, nullptr) && n == data.size(), "private JSON write");
    require(FlushFileBuffers(h) != FALSE, "private JSON flush");
}
static void save(const std::wstring& p, const J& data) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(h != INVALID_HANDLE_VALUE, "create private JSON");
    try { writeBytes(h, data.dump(2) + "\n"); } catch (...) { CloseHandle(h); throw; } CloseHandle(h);
}
static std::wstring quote(const std::wstring& s) { require(s.find(L'"') == s.npos, "quoted private argument"); return L"\"" + s + L"\""; }
static std::wstring eventName(const std::wstring& prefix, const std::wstring& name) { return L"Local\\" + prefix + L"_" + name; }
static void signal(const std::wstring& prefix, const std::wstring& name) {
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName(prefix, name).c_str()); require(h != nullptr, "open private signal");
    const BOOL ok = SetEvent(h); CloseHandle(h); require(ok != FALSE, "private signal");
}
static void waitEvent(const std::wstring& prefix, const std::wstring& name, DWORD budget = 6000) {
    HANDLE h = OpenEventW(SYNCHRONIZE, FALSE, eventName(prefix, name).c_str()); require(h != nullptr, "open private wait");
    const DWORD rc = WaitForSingleObject(h, budget); CloseHandle(h); require(rc == WAIT_OBJECT_0, "private event budget");
}
static bool g_suppressFakeTermination = false;
static J g_denied = J::array();
static BOOL fixtureTerminate(HANDLE process, UINT code) {
    const auto id = identity(process);
    if (g_suppressFakeTermination && id["pid"] != GetCurrentProcessId() && id["image"] == utf8(ownImage())) {
        g_denied.push_back({{"identity", id}, {"tick", GetTickCount64()}, {"requestedCode", code}, {"controlledLeaf", "return FALSE/ERROR_ACCESS_DENIED; real child untouched"}});
        SetLastError(ERROR_ACCESS_DENIED); return FALSE;
    }
    return TerminateProcess(process, code);
}
// Only the CLI path leaf is mapped; the full actual launcher and its process wrapper remain real.
static std::wstring hidHideCliPath() { return ownImage(); }
#define TerminateProcess fixtureTerminate
#include "actual_consumer.inc"
#undef TerminateProcess

static J pending() {
    J j = {{"processPresent", g_hidHidePendingChildProcess != nullptr}, {"pipePresent", g_hidHidePendingReadPipe != nullptr}};
    if (g_hidHidePendingChildProcess) {
        j["identity"] = identity(g_hidHidePendingChildProcess);
        j["wait0"] = WaitForSingleObject(g_hidHidePendingChildProcess, 0);
    }
    return j;
}
static std::wstring fakeArgs(const std::wstring& root, const std::wstring& prefix, const std::wstring& tag) {
    return L"--fake-cli " + quote(root) + L" " + quote(prefix) + L" " + quote(tag);
}
static J call(const std::wstring& root, const std::wstring& prefix, const std::wstring& tag, ULONGLONG budget) {
    g_inputHostSwitchRecoveryStartDeadline = GetTickCount64() + budget;
    const ULONGLONG begin = GetTickCount64(); std::string output; DWORD code = 0;
    const bool ok = hidHideRun(fakeArgs(root, prefix, tag), &output, &code);
    return {{"ok", ok}, {"code", code}, {"beginTick", begin}, {"endTick", GetTickCount64()}, {"originalDeadline", g_inputHostSwitchRecoveryStartDeadline}, {"output", output}, {"pending", pending()}};
}
static void appendWorld(const std::wstring& root, const J& record) {
    HANDLE h = CreateFileW(file(root, L"world.json").c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(h != INVALID_HANDLE_VALUE, "exclusive private world writer");
    try {
        const auto bytes = readBytes(h); J world = bytes.empty() ? J{{"writes", J::array()}} : J::parse(bytes);
        world["writes"].push_back(record); LARGE_INTEGER zero{};
        require(SetFilePointerEx(h, zero, nullptr, FILE_BEGIN) && SetEndOfFile(h), "private world rewrite"); writeBytes(h, world.dump(2) + "\n");
    } catch (...) { CloseHandle(h); throw; } CloseHandle(h);
}
static int fakeCli(const std::wstring& root, const std::wstring& prefix, const std::wstring& tag) {
    J ready = {{"identity", identity(GetCurrentProcess())}, {"parentPid", parentPid()}, {"tag", utf8(tag)}, {"readyTick", GetTickCount64()}, {"privateFakeOnly", true}};
    save(file(root, tag + L".ready.json"), ready); signal(prefix, tag + L"-ready");
    waitEvent(prefix, tag + L"-write", 12000);
    J record = ready; record["writeTick"] = GetTickCount64(); appendWorld(root, record); signal(prefix, tag + L"-written");
    waitEvent(prefix, tag + L"-exit", 12000);
    const std::string text = "private-fake-cli:" + utf8(tag) + "\n"; DWORD sent = 0;
    // After an old caller exits, its stdout pipe can be broken. This is diagnostic only.
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &sent, nullptr);
    return 0;
}
static int callerOne(const std::wstring& root, const std::wstring& prefix, bool stay) {
    const auto me = identity(GetCurrentProcess()); const auto initial = pending(); g_suppressFakeTermination = true;
    const auto first = call(root, prefix, L"old", 1200);
    const auto beforeSecond = pending(); const auto second = call(root, prefix, L"blocked", 1200);
    save(file(root, L"caller-one.json"), {{"identity", me}, {"entryPending", initial}, {"first", first}, {"beforeSecond", beforeSecond}, {"second", second}, {"controlledTerminationDenials", g_denied}});
    signal(prefix, L"caller-one-report");
    if (!stay) { waitEvent(prefix, L"allow-caller-one-exit"); ExitProcess(0); }
    waitEvent(prefix, L"begin-third");
    const auto beforeThird = pending(); g_suppressFakeTermination = false;
    const auto third = call(root, prefix, L"new", 4000);
    save(file(root, L"caller-three.json"), {{"identity", identity(GetCurrentProcess())}, {"beforeThird", beforeThird}, {"third", third}, {"controlledTerminationDenials", g_denied}});
    return 0;
}
static int callerTwo(const std::wstring& root, const std::wstring& prefix) {
    save(file(root, L"caller-two-entry.json"), {{"identity", identity(GetCurrentProcess())}, {"entryPending", pending()}, {"naturalNewProcess", true}});
    const auto result = call(root, prefix, L"new", 4000);
    save(file(root, L"caller-two.json"), {{"identity", identity(GetCurrentProcess())}, {"result", result}, {"controlledTerminationDenials", g_denied}});
    return 0;
}
struct Observer {
    HANDLE job = nullptr; std::vector<HANDLE> processes; std::vector<HANDLE> events;
    J result = {{"pass", true}, {"checks", 0}, {"failures", J::array()}, {"observations", J::array()}};
    explicit Observer(const std::wstring& prefix) {
        job = CreateJobObjectW(nullptr, nullptr); require(job != nullptr, "private cleanup job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{}; info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        require(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info)) != FALSE, "private cleanup limit");
        std::vector<std::wstring> names = {L"caller-one-report", L"allow-caller-one-exit", L"begin-third"};
        for (const auto* tag : {L"old", L"new", L"blocked"}) for (const auto* suffix : {L"ready", L"write", L"written", L"exit"}) names.push_back(std::wstring(tag) + L"-" + suffix);
        for (const auto& name : names) { HANDLE h = CreateEventW(nullptr, TRUE, FALSE, eventName(prefix, name).c_str()); require(h != nullptr && GetLastError() != ERROR_ALREADY_EXISTS, "fresh private event"); events.push_back(h); }
        observe("observer-start", {{"identity", identity(GetCurrentProcess())}, {"jobCleanupOnly", true}});
    }
    ~Observer() { if (job) CloseHandle(job); for (auto h : processes) CloseHandle(h); for (auto h : events) CloseHandle(h); }
    void expect(bool ok, const std::string& name) {
        result["checks"] = result["checks"].get<unsigned>() + 1;
        if (!ok) { result["pass"] = false; result["failures"].push_back(name); }
    }
    void observe(const char* event, J data) { result["observations"].push_back({{"event", event}, {"tick", GetTickCount64()}, {"data", std::move(data)}}); }
    HANDLE launch(const std::wstring& role, const std::wstring& root, const std::wstring& prefix) {
        std::wstring cmd = quote(ownImage()) + L" " + role + L" " + quote(root) + L" " + quote(prefix);
        std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0); STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
        require(CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, root.c_str(), &si, &pi) != FALSE, "private native launch");
        processes.push_back(pi.hProcess);
        if (!AssignProcessToJobObject(job, pi.hProcess)) { TerminateProcess(pi.hProcess, 90); CloseHandle(pi.hThread); throw std::runtime_error("private caller job assignment"); }
        require(ResumeThread(pi.hThread) != static_cast<DWORD>(-1), "resume private caller"); CloseHandle(pi.hThread);
        observe("native-start", {{"role", utf8(role)}, {"identity", identity(pi.hProcess)}}); return pi.hProcess;
    }
    HANDLE child(const std::wstring& root, const std::wstring& tag, HANDLE parent) {
        const auto record = load(file(root, tag + L".ready.json"));
        HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, record["identity"]["pid"].get<DWORD>());
        require(h != nullptr, "open exact private child"); processes.push_back(h); const auto real = identity(h), parentId = identity(parent);
        expect(real["pid"] == record["identity"]["pid"] && real["birth"] == record["identity"]["birth"], "child PID+birth bound to observer handle");
        expect(real["image"] == utf8(ownImage()), "child image is private fixture");
        expect(record["parentPid"] == parentId["pid"], "snapshot confirms actual native parent");
        expect(WaitForSingleObject(h, 0) == WAIT_TIMEOUT, "observed child really alive");
        observe("child-opened", {{"tag", utf8(tag)}, {"identity", real}, {"ready", record}}); return h;
    }
    void finished(HANDLE h, const char* name) {
        expect(WaitForSingleObject(h, 3000) == WAIT_OBJECT_0, std::string(name) + " actual process exited");
        DWORD code = 999; expect(GetExitCodeProcess(h, &code) && code == 0, std::string(name) + " normal exit code");
        observe("process-exit-observed", {{"name", name}, {"identity", identity(h)}, {"code", code}});
    }
    void clean() {
        J rows = J::array();
        for (auto h : processes) {
            const auto id = identity(h); DWORD state = WaitForSingleObject(h, 0); bool forced = false;
            if (state != WAIT_OBJECT_0) { forced = TerminateProcess(h, 91) != FALSE; state = WaitForSingleObject(h, 3000); }
            rows.push_back({{"identity", id}, {"forced", forced}, {"finalWait", state}}); expect(state == WAIT_OBJECT_0, "exact owned process cleaned");
            expect(!forced, "baseline needs no forced cleanup");
        }
        result["cleanup"] = rows; result["noOwnedProcessesRemain"] = std::all_of(rows.begin(), rows.end(), [](const J& r) { return r["finalWait"] == WAIT_OBJECT_0; });
    }
};
static void verifyOne(Observer& o, const J& row, HANDLE native, HANDLE child) {
    const auto old = identity(child), id = identity(native);
    o.expect(row["identity"]["pid"] == id["pid"] && row["identity"]["birth"] == id["birth"], "caller report exact native identity");
    o.expect(!row["entryPending"]["processPresent"].get<bool>(), "fresh native naturally starts without pending");
    o.expect(!row["first"]["ok"].get<bool>() && row["first"]["code"] == WAIT_TIMEOUT, "actual first call times out");
    o.expect(row["first"]["pending"]["processPresent"].get<bool>() && row["first"]["pending"]["pipePresent"].get<bool>(), "actual RAII retains process and read pipe");
    o.expect(row["first"]["pending"].contains("identity") && row["first"]["pending"]["identity"]["birth"] == old["birth"] && row["first"]["pending"]["identity"]["pid"] == old["pid"], "retained pending is the actual private child");
    o.expect(!row["second"]["ok"].get<bool>() && row["second"]["code"] == ERROR_BUSY, "same-native second call blocked by pending gate");
    o.expect(row["beforeSecond"] == row["second"]["pending"], "same-native refusal preserves exact pending handles");
    o.expect(row["controlledTerminationDenials"].size() == 1 && row["controlledTerminationDenials"][0]["identity"]["birth"] == old["birth"], "one explicitly controlled termination refusal on exact child");
    o.expect(WaitForSingleObject(child, 0) == WAIT_TIMEOUT, "old CLI alive after actual timeout and second call");
    o.result["callerOne"] = row;
}
static int observeCase(const std::wstring& kind, const std::wstring& root) {
    const auto prefix = L"YMCC117_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    Observer o(prefix); o.result["case"] = utf8(kind); o.result["privateRoot"] = utf8(root); o.result["eventPrefix"] = utf8(prefix);
    try {
        const bool cross = kind == L"cross-native"; require(cross || kind == L"same-native", "explicit scenario");
        HANDLE a = o.launch(cross ? L"--caller-one" : L"--caller-same", root, prefix);
        waitEvent(prefix, L"old-ready"); HANDLE old = o.child(root, L"old", a);
        waitEvent(prefix, L"caller-one-report"); verifyOne(o, load(file(root, L"caller-one.json")), a, old);
        o.expect(GetFileAttributesW(file(root, L"blocked.ready.json").c_str()) == INVALID_FILE_ATTRIBUTES, "same-native second call created no fake child");
        if (GetFileAttributesW(file(root, L"blocked.ready.json").c_str()) != INVALID_FILE_ATTRIBUTES) o.child(root, L"blocked", a);
        HANDLE b = nullptr;
        if (cross) {
            signal(prefix, L"allow-caller-one-exit"); o.finished(a, "old-native");
            o.expect(WaitForSingleObject(old, 0) == WAIT_TIMEOUT, "old fake CLI survives old native ExitProcess");
            o.observe("old-native-dead-old-cli-alive", {{"native", identity(a)}, {"child", identity(old)}, {"nativeWait", WaitForSingleObject(a, 0)}, {"childWait", WaitForSingleObject(old, 0)}});
            b = o.launch(L"--caller-two", root, prefix); waitEvent(prefix, L"new-ready");
            o.result["callerTwoEntry"] = load(file(root, L"caller-two-entry.json"));
            o.expect(!o.result["callerTwoEntry"]["entryPending"]["processPresent"].get<bool>(), "new independent native has no knowledge of old pending");
            o.expect(identity(a)["pid"] != identity(b)["pid"] && identity(a)["birth"] != identity(b)["birth"], "two independently created native identities");
        } else {
            signal(prefix, L"old-write"); waitEvent(prefix, L"old-written"); signal(prefix, L"old-exit"); o.finished(old, "old-cli");
            signal(prefix, L"begin-third"); waitEvent(prefix, L"new-ready"); b = a;
        }
        HANDLE next = o.child(root, L"new", b);
        if (cross) {
            const DWORD oldWait = WaitForSingleObject(old, 0), nextWait = WaitForSingleObject(next, 0);
            o.expect(oldWait == WAIT_TIMEOUT && nextWait == WAIT_TIMEOUT, "two exact real fake CLI children simultaneously alive");
            o.observe("two-cli-overlap", {{"old", identity(old)}, {"new", identity(next)}, {"oldWait", oldWait}, {"newWait", nextWait}});
            signal(prefix, L"old-write"); waitEvent(prefix, L"old-written");
        }
        signal(prefix, L"new-write"); waitEvent(prefix, L"new-written");
        const auto world = load(file(root, L"world.json")); o.result["world"] = world;
        o.expect(world["writes"].size() == 2 && world["writes"][0]["tag"] == "old" && world["writes"][1]["tag"] == "new", "both real CLI children wrote the same private JSON world");
        o.expect(world["writes"][0]["identity"]["birth"] == identity(old)["birth"] && world["writes"][1]["identity"]["birth"] == identity(next)["birth"], "private world writes bind exact children");
        if (cross) {
            o.expect(WaitForSingleObject(a, 0) == WAIT_OBJECT_0 && WaitForSingleObject(old, 0) == WAIT_TIMEOUT && WaitForSingleObject(next, 0) == WAIT_TIMEOUT, "late writes while old native dead and both children live");
            o.observe("shared-world-after-old-native-exit", {{"world", world}, {"oldNativeWait", WaitForSingleObject(a, 0)}, {"oldCliWait", WaitForSingleObject(old, 0)}, {"newCliWait", WaitForSingleObject(next, 0)}});
            signal(prefix, L"old-exit"); o.finished(old, "old-cli");
        }
        signal(prefix, L"new-exit"); o.finished(next, "new-cli"); o.finished(b, "current-native");
        if (cross) {
            const auto report = load(file(root, L"caller-two.json")); o.result["callerTwo"] = report;
            o.expect(report["result"]["ok"].get<bool>() && report["result"]["code"] == 0, "new native dispatch completes through actual launcher");
            o.expect(!report["result"]["pending"]["processPresent"].get<bool>() && report["controlledTerminationDenials"].empty(), "new native finishes without pending or termination substitution");
            o.expect(report["result"]["output"] == "private-fake-cli:new\n", "actual pipe output is drained");
        } else {
            const auto report = load(file(root, L"caller-three.json")); o.result["callerThree"] = report;
            o.expect(report["identity"]["birth"] == identity(a)["birth"], "third call remains in exact same native");
            o.expect(report["beforeThird"]["processPresent"].get<bool>() && report["beforeThird"]["wait0"] == WAIT_OBJECT_0 && report["beforeThird"]["identity"]["birth"] == identity(old)["birth"], "third entry still retains exact now-signaled old child");
            o.expect(report["third"]["ok"].get<bool>() && report["third"]["code"] == 0, "original entry rechecks old exit and admits new dispatch");
            o.expect(!report["third"]["pending"]["processPresent"].get<bool>() && !report["third"]["pending"]["pipePresent"].get<bool>(), "proven old pending handle and pipe cleared by actual entry");
            o.expect(report["controlledTerminationDenials"].size() == 1 && report["third"]["output"] == "private-fake-cli:new\n", "third call finishes without another termination substitution");
        }
    } catch (const std::exception& e) { o.result["pass"] = false; o.result["infrastructureException"] = e.what(); }
    o.clean(); save(file(root, L"observer-result.json"), o.result);
    const std::string text = o.result.dump() + "\n"; std::fwrite(text.data(), 1, text.size(), stdout); return o.result["pass"].get<bool>() ? 0 : 1;
}
int wmain(int argc, wchar_t** argv) {
    try {
        require(argc >= 2, "explicit private fixture role"); const std::wstring role = argv[1];
        if (role == L"--observe") { require(argc == 4, "observer arguments"); return observeCase(argv[2], argv[3]); }
        if (role == L"--fake-cli") { require(argc == 5, "fake CLI arguments"); return fakeCli(argv[2], argv[3], argv[4]); }
        require(argc == 4, "caller arguments");
        if (role == L"--caller-one") return callerOne(argv[2], argv[3], false);
        if (role == L"--caller-same") return callerOne(argv[2], argv[3], true);
        if (role == L"--caller-two") return callerTwo(argv[2], argv[3]);
        throw std::runtime_error("unrecognized private role");
    } catch (const std::exception& e) { std::fprintf(stderr, "private fixture failure: %s\n", e.what()); return 92; }
}
