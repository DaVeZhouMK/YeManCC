// Standalone calibration: Windows/CRT only; no Runtime, Client, Steam or device code.
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "json.hpp"

namespace calibration {
using Json = nlohmann::json;
constexpr DWORD deadline = 5000;
std::atomic<unsigned> pendingIo{0}, workers{0};
unsigned ownedHandles = 0, joinableThreads = 0, ownedModuleReferences = 0;
using RandomApi = LONG(WINAPI*)(BCRYPT_ALG_HANDLE, PUCHAR, ULONG, ULONG);
#ifdef CALIBRATION_DYNAMIC_BCRYPT
RandomApi randomApi = nullptr;
#else
RandomApi randomApi = &BCryptGenRandom;
#endif
[[noreturn]] void fail(const char* operation) {
    std::fprintf(stderr, "calibration failure: %s, win32=%lu\n", operation, GetLastError());
    ExitProcess(2); // Only this owned probe; no destructor may hide a blocked join.
}
void need(bool ok, const char* operation) { if (!ok) fail(operation); }
void wait(HANDLE h) { need(WaitForSingleObject(h, deadline) == WAIT_OBJECT_0, "bounded wait"); }
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) { set(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { close(); }
    void set(HANDLE h) { need(!value && h && h != INVALID_HANDLE_VALUE, "owned handle"); value = h; ++ownedHandles; }
    void close() { if (value) { need(CloseHandle(value) != FALSE, "CloseHandle"); value = nullptr; --ownedHandles; } }
};
struct Entry { HANDLE value; ULONG_PTR handles, pointers; ULONG access, type, attributes, reserved; };
struct Table { ULONG_PTR count, reserved; Entry entries[1]; };
struct Unicode { USHORT length, capacity; PWSTR value; };
using QueryProcess = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
using QueryObject = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
std::string utf8(const Unicode& text, const std::vector<unsigned char>& buffer) {
    if (!text.length) return {};
    const auto start = reinterpret_cast<std::uintptr_t>(buffer.data());
    const auto ptr = reinterpret_cast<std::uintptr_t>(text.value);
    need(text.length % sizeof(wchar_t) == 0 && ptr >= start && ptr - start <= buffer.size() &&
        text.length <= buffer.size() - (ptr - start), "UNICODE_STRING bounds");
    const int count = text.length / static_cast<int>(sizeof(wchar_t));
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.value, count, nullptr, 0, nullptr, nullptr);
    need(size > 0, "UTF-8 size");
    std::string result(static_cast<size_t>(size), '\0');
    need(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.value, count, result.data(), size, nullptr, nullptr) == size, "UTF-8 conversion");
    return result;
}
struct Row {
    std::uintptr_t handle = 0;
    ULONG access = 0, typeIndex = 0;
    std::string type, nameState = "not-file", name;
    LONG nameStatus = 0;
    auto key() const { return std::to_string(handle) + ":" + type + ":" + std::to_string(access); }
    Json json() const {
        Json row{{"handle", handle}, {"access", access}, {"type", type}, {"typeIndex", typeIndex}, {"nameState", nameState}};
        if (nameState == "queried-quiescent-self" || nameState == "retained-quiescent-self") {
            row["objectName"] = name; row["objectNameStatus"] = static_cast<ULONG>(nameStatus);
        }
        return row;
    }
};
struct Snapshot {
    DWORD countBefore = 0, countAfter = 0;
    std::vector<Row> rows;
    unsigned io = 0, live = 0, owned = 0, joinable = 0, moduleReferences = 0;
    Json modules = Json::object();
    Json json() const {
        Json entries = Json::array(), types = Json::object();
        for (const auto& row : rows) { entries.push_back(row.json()); types[row.type] = types.value(row.type, 0) + 1; }
        return {{"processHandleCountBeforeQuery", countBefore}, {"processHandleCountAfterQuery", countAfter},
            {"tableHandleCount", rows.size()}, {"typeCounts", types}, {"handles", entries},
            {"ownedModuleReferences", moduleReferences}, {"loadedCryptoModules", modules}, {"ownedPendingIo", io}, {"liveOwnedWorkers", live}, {"ownedExplicitHandles", owned}, {"joinableStdThreads", joinable}};
    }
};
class Probe {
    QueryProcess query_ = nullptr;
    QueryObject object_ = nullptr;
    bool initialized_ = false, queryNames_ = true;
    std::map<std::string, Row> previous_;
public:
    Json stages = Json::array(), rounds = Json::array();
    explicit Probe(bool queryNames = true) : queryNames_(queryNames) {
        const auto module = GetModuleHandleW(L"ntdll.dll");
        query_ = reinterpret_cast<QueryProcess>(GetProcAddress(module, "NtQueryInformationProcess"));
        object_ = reinterpret_cast<QueryObject>(GetProcAddress(module, "NtQueryObject"));
        need(query_ && object_, "self snapshot API availability");
    }
    std::pair<LONG, std::string> objectText(HANDLE h, ULONG infoClass) const {
        std::vector<unsigned char> bytes(4096);
        ULONG required = 0;
        LONG status = 0;
        for (unsigned attempt = 0; attempt < 4; ++attempt) {
            status = object_(h, infoClass, bytes.data(), static_cast<ULONG>(bytes.size()), &required);
            if (status >= 0) return {status, utf8(*reinterpret_cast<const Unicode*>(bytes.data()), bytes)};
            if (required <= bytes.size() || required > 1024 * 1024) break;
            bytes.resize(static_cast<size_t>(required) + 4096);
        }
        return {status, {}};
    }
    Snapshot take() {
        Snapshot s;
        s.moduleReferences = ownedModuleReferences;
        s.modules = {{"bcrypt.dll", GetModuleHandleW(L"bcrypt.dll") != nullptr}, {"bcryptprimitives.dll", GetModuleHandleW(L"bcryptprimitives.dll") != nullptr}, {"cryptsp.dll", GetModuleHandleW(L"cryptsp.dll") != nullptr}};
        s.io = pendingIo.load(); s.live = workers.load(); s.owned = ownedHandles; s.joinable = joinableThreads;
        need(GetProcessHandleCount(GetCurrentProcess(), &s.countBefore) != FALSE, "handle count before");
        std::vector<unsigned char> bytes(65536);
        ULONG required = 0; LONG status = 0;
        for (unsigned attempt = 0; attempt < 4; ++attempt) {
            status = query_(GetCurrentProcess(), 51, bytes.data(), static_cast<ULONG>(bytes.size()), &required);
            if (status >= 0) break;
            if (required <= bytes.size() || required > 16 * 1024 * 1024) break;
            bytes.resize(static_cast<size_t>(required) + 4096);
        }
        need(status >= 0, "NtQueryInformationProcess(self,51)");
        const auto* table = reinterpret_cast<const Table*>(bytes.data());
        need(table->count <= (bytes.size() - offsetof(Table, entries)) / sizeof(Entry), "handle table bounds");
        std::map<std::string, Row> next;
        for (ULONG_PTR i = 0; i < table->count; ++i) {
            const auto& e = table->entries[i];
            const auto type = objectText(e.value, 2); need(type.first >= 0, "NtQueryObject(self,type)");
            Row row; row.handle = reinterpret_cast<std::uintptr_t>(e.value); row.access = e.access; row.typeIndex = e.type; row.type = type.second;
            if (row.type == "File") {
                const auto old = previous_.find(row.key());
                if (!queryNames_) row.nameState = "disabled-control";
                else if (!initialized_) row.nameState = "baseline-inherited-file-skipped";
                else if (old != previous_.end() && old->second.nameState == "baseline-inherited-file-skipped") row.nameState = old->second.nameState;
                else if (old != previous_.end() && (old->second.nameState == "queried-quiescent-self" || old->second.nameState == "retained-quiescent-self")) {
                    row.nameState = "retained-quiescent-self"; row.name = old->second.name; row.nameStatus = old->second.nameStatus;
                } else if (s.io != 0 || s.live != 0) row.nameState = "deferred-until-no-owned-io-and-workers";
                else {
                    const auto name = objectText(e.value, 1);
                    row.nameState = "queried-quiescent-self"; row.name = name.second; row.nameStatus = name.first;
                }
            }
            next.emplace(row.key(), row); s.rows.push_back(std::move(row));
        }
        previous_ = std::move(next); initialized_ = true;
        need(GetProcessHandleCount(GetCurrentProcess(), &s.countAfter) != FALSE, "handle count after");
        need(s.countBefore == s.countAfter && s.rows.size() == s.countAfter, "snapshot observer stability");
        return s;
    }
    static Json difference(const Snapshot& before, const Snapshot& after) {
        Json added = Json::array(), removed = Json::array();
        for (const auto& a : after.rows) if (std::none_of(before.rows.begin(), before.rows.end(), [&](const Row& b) {return a.key() == b.key();})) added.push_back(a.json());
        for (const auto& b : before.rows) if (std::none_of(after.rows.begin(), after.rows.end(), [&](const Row& a) {return a.key() == b.key();})) removed.push_back(b.json());
        return {{"delta", static_cast<long long>(after.rows.size()) - static_cast<long long>(before.rows.size())}, {"added", added}, {"removed", removed}};
    }
    void record(const std::string& name, const Snapshot& before, const Snapshot& after, const Json& detail = Json::object()) {
        stages.push_back({{"id", name}, {"before", before.json()}, {"after", after.json()}, {"difference", difference(before, after)}, {"calls", detail}});
    }
    template<class F> void step(const std::string& name, F&& action) {
        const auto before = take(); const Json detail = action(); const auto after = take(); record(name, before, after, detail);
    }
};
void randomCalls(Probe& p, bool once = false, const std::string& prefix = "bcrypt.") {
    const std::vector<unsigned> counts = once ? std::vector<unsigned>{1} : std::vector<unsigned>{1, 1, 32, 32};
    unsigned index = 0;
    for (const unsigned count : counts) {
        const std::string id = prefix + std::to_string(index++);
        const auto before = p.take();
        for (unsigned i = 0; i < count; ++i) {
            unsigned char bytes[32]{};
            need(randomApi && randomApi(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0, "BCryptGenRandom");
        }
        const auto after = p.take();
        p.record(id, before, after, {{"api", "BCryptGenRandom(NULL,32,BCRYPT_USE_SYSTEM_PREFERRED_RNG)"}, {"count", count}});
        p.rounds.push_back({{"id", id}, {"difference", Probe::difference(before, after)}});
    }
}
void threads(Probe& p) {
    p.step("thread.empty.create-exit-join", [] {
        std::thread t([] {}); ++joinableThreads; wait(t.native_handle()); t.join(); --joinableThreads;
        return Json{{"count", 1}, {"joined", true}};
    });
    for (const unsigned width : {1u, 4u, 8u}) for (unsigned round = 0; round < 3; ++round) {
        const auto start = p.take();
        const std::string id = "thread.wait." + std::to_string(width) + "." + std::to_string(round);
        Handle release; std::vector<Handle> entered(width); std::vector<std::thread> group; group.reserve(width);
        std::atomic<unsigned> successful{0};
        p.step(id + ".create-events", [&] {
            release.set(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            for (auto& event : entered) event.set(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            return Json{{"events", width + 1}};
        });
        p.step(id + ".create-and-park", [&] {
            for (unsigned i = 0; i < width; ++i) {
                ++workers;
                group.emplace_back([&, i] {
                    if (!SetEvent(entered[i].value)) fail("worker ready");
                    const DWORD result = WaitForSingleObject(release.value, deadline);
                    if (result == WAIT_OBJECT_0) ++successful;
                    --workers;
                });
                ++joinableThreads;
            }
            for (auto& event : entered) wait(event.value);
            return Json{{"stdThreads", width}, {"api", "WaitForSingleObject(own event,5000)"}};
        });
        p.step(id + ".release-and-exit", [&] {
            need(SetEvent(release.value) != FALSE, "release workers");
            for (auto& t : group) wait(t.native_handle());
            need(workers.load() == 0 && successful.load() == width, "worker exits");
            return Json{{"successfulWaits", successful.load()}, {"terminatedBeforeJoin", true}};
        });
        p.step(id + ".join", [&] {
            for (auto& t : group) { t.join(); --joinableThreads; }
            return Json{{"joined", width}};
        });
        p.step(id + ".close-events", [&] {
            for (auto& event : entered) event.close(); release.close(); return Json{{"closed", width + 1}};
        });
        const auto end = p.take();
        p.rounds.push_back({{"id", id}, {"difference", Probe::difference(start, end)}});
    }
}
void pipes(Probe& p) {
    for (unsigned round = 0; round < 3; ++round) {
        const auto start = p.take(); const std::string id = "pipe." + std::to_string(round);
        Handle server, client, connectEvent, readEvent, writeEvent;
        OVERLAPPED connect{}, read{}, write{};
        unsigned char sent = 0x5a, received = 0;
        const std::wstring name = L"\\\\.\\pipe\\YMCC-Handle-Calibration-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(round);
        p.step(id + ".create", [&] {
            connectEvent.set(CreateEventW(nullptr, TRUE, FALSE, nullptr)); readEvent.set(CreateEventW(nullptr, TRUE, FALSE, nullptr)); writeEvent.set(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            connect.hEvent = connectEvent.value; read.hEvent = readEvent.value; write.hEvent = writeEvent.value;
            server.set(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, nullptr));
            return Json{{"localOwnedPipe", true}};
        });
        p.step(id + ".connect-and-drain", [&] {
            const BOOL connected = ConnectNamedPipe(server.value, &connect); const DWORD error = connected ? ERROR_SUCCESS : GetLastError();
            need(connected || error == ERROR_IO_PENDING || error == ERROR_PIPE_CONNECTED, "ConnectNamedPipe");
            if (error == ERROR_IO_PENDING) ++pendingIo;
            client.set(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
            if (error == ERROR_IO_PENDING) { wait(connect.hEvent); DWORD bytes = 0; need(GetOverlappedResult(server.value, &connect, &bytes, FALSE) != FALSE, "connect drain"); --pendingIo; }
            return Json{{"pendingIoDrained", true}};
        });
        p.step(id + ".read-pending", [&] {
            DWORD bytes = 0; const BOOL done = ReadFile(server.value, &received, 1, &bytes, &read);
            need(!done && GetLastError() == ERROR_IO_PENDING, "own pipe genuinely pending read"); ++pendingIo;
            return Json{{"pendingRead", true}, {"nameQueriesDeferred", true}};
        });
        p.step(id + ".write-and-drain", [&] {
            DWORD bytes = 0; const BOOL done = WriteFile(client.value, &sent, 1, &bytes, &write); const bool pending = !done && GetLastError() == ERROR_IO_PENDING;
            need(done || pending, "own pipe write");
            if (pending) { ++pendingIo; wait(write.hEvent); }
            need(GetOverlappedResult(client.value, &write, &bytes, FALSE) != FALSE && bytes == 1, "write drain"); if (pending) --pendingIo;
            wait(read.hEvent); need(GetOverlappedResult(server.value, &read, &bytes, FALSE) != FALSE && bytes == 1 && received == sent, "read drain"); --pendingIo;
            return Json{{"bytesVerified", bytes}, {"pendingIoDrained", pendingIo.load() == 0}};
        });
        p.step(id + ".close", [&] {
            need(pendingIo.load() == 0, "no pipe IO before close"); client.close();
            need(DisconnectNamedPipe(server.value) != FALSE, "DisconnectNamedPipe"); server.close();
            connectEvent.close(); readEvent.close(); writeEvent.close(); return Json{{"allExplicitPipeHandlesClosed", true}};
        });
        const auto end = p.take(); p.rounds.push_back({{"id", id}, {"difference", Probe::difference(start, end)}});
    }
}
void children(Probe& p) {
    // Additional process-only control: source-built self child, no inherited handles.
    wchar_t executable[32768]{}; need(GetModuleFileNameW(nullptr, executable, 32768) != 0, "own executable");
    for (unsigned round = 0; round < 3; ++round) {
        const auto start = p.take(); const std::string id = "own-child." + std::to_string(round);
        Handle process, primaryThread;
        p.step(id + ".create", [&] {
            std::wstring command = L"\"" + std::wstring(executable) + L"\" --owned-child";
            STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION info{};
            need(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info) != FALSE, "own CreateProcessW");
            process.set(info.hProcess); primaryThread.set(info.hThread); return Json{{"ownedChildPid", info.dwProcessId}, {"handleInheritance", false}};
        });
        p.step(id + ".wait-exit", [&] {
            wait(process.value); DWORD code = STILL_ACTIVE; need(GetExitCodeProcess(process.value, &code) != FALSE && code == 0, "own child natural exit");
            return Json{{"exitCode", code}};
        });
        p.step(id + ".close", [&] { primaryThread.close(); process.close(); return Json{{"closed", 2}}; });
        const auto end = p.take(); p.rounds.push_back({{"id", id}, {"difference", Probe::difference(start, end)}});
    }
}
#ifdef CALIBRATION_DYNAMIC_BCRYPT
void moduleCycles(Probe& p, const std::string& scenario) {
    for (unsigned round = 0; round < 3; ++round) {
        const auto start = p.take(); const std::string id = "library." + std::to_string(round);
        HMODULE module = nullptr;
        p.step(id + ".load-system-bcrypt", [&] {
            module = LoadLibraryExW(L"bcrypt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            need(module != nullptr, "LoadLibraryExW(system bcrypt)"); ++ownedModuleReferences;
            randomApi = reinterpret_cast<RandomApi>(GetProcAddress(module, "BCryptGenRandom")); need(randomApi != nullptr, "BCryptGenRandom resolution");
            return Json{{"library", "system32/bcrypt.dll"}, {"ownLoadReference", true}};
        });
        if (scenario != "dynamic-load-only") randomCalls(p, scenario == "dynamic-rng-once", id + ".bcrypt.");
        p.step(id + ".free-own-reference", [&] {
            randomApi = nullptr; need(FreeLibrary(module) != FALSE, "FreeLibrary(own reference)"); --ownedModuleReferences;
            return Json{{"freedOwnLoadReference", true}, {"noForcedUnmapOrOtherModuleReferences", true}};
        });
        const auto end = p.take(); p.rounds.push_back({{"id", id}, {"difference", Probe::difference(start, end)}});
    }
}
#endif
} // namespace calibration
int main(int argc, char** argv) {
    using namespace calibration;
    if (argc == 2 && std::string(argv[1]) == "--owned-child") return 0;
    try {
        need(argc == 2, "scenario argument"); const std::string scenario = argv[1];
        Probe p(scenario.find("no-names") == std::string::npos); const auto initial = p.take();
        p.step("observer.repeat-1", [] { return Json::object(); });
        p.step("observer.repeat-2", [] { return Json::object(); });
        p.step("event.only.create-set-wait-close", [] {
            Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr)); need(SetEvent(event.value) != FALSE, "event-only set"); wait(event.value); event.close();
            return Json{{"closed", true}};
        });
        if (scenario == "observer-only") { for (unsigned i = 0; i < 16; ++i) p.step("observer.noop-" + std::to_string(i), [] { return Json::object(); }); }
        #ifdef CALIBRATION_DYNAMIC_BCRYPT
        else if (scenario == "dynamic-load-only" || scenario == "dynamic-rng-once" || scenario == "dynamic-rng-repeat") moduleCycles(p, scenario);
#endif
        else if (scenario == "rng-only" || scenario == "rng-only-no-names") randomCalls(p);
        else if (scenario == "rng-once-only" || scenario == "rng-once-no-names") randomCalls(p, true);
        else if (scenario == "rng-once-then-threads") { randomCalls(p, true); threads(p); }
        else if (scenario == "threads-only") threads(p);
        else if (scenario == "pipe-only") pipes(p);
        else if (scenario == "own-child-only") children(p);
        else if (scenario == "rng-then-threads") { randomCalls(p); threads(p); }
        else if (scenario == "threads-then-rng") { threads(p); randomCalls(p); }
        else fail("unknown scenario");
        need(pendingIo.load() == 0 && workers.load() == 0 && ownedHandles == 0 && joinableThreads == 0 && ownedModuleReferences == 0, "final owned resources zero");
        // Quiescent observations without any timer/sleep or extra work between them.
        for (unsigned i = 0; i < 4; ++i) p.step("final.quiescent-" + std::to_string(i), [] { return Json::object(); });
        const auto final = p.take();
        const Json result{{"schema", "ymcc.handle-calibration.probe.v1"}, {"scenario", scenario}, {"pid", GetCurrentProcessId()},
            {"fileNameQueriesEnabled", scenario.find("no-names") == std::string::npos}, {"msvcFullVersion", _MSC_FULL_VER}, {"scope", "self process class 51; self handles only; no global enumeration; baseline inherited File names skipped"},
            {"nameQueryPolicy", "Only newly observed self File handles when ownedPendingIo=0 and liveOwnedWorkers=0; no product I/O or APIs"},
            {"identityLimit", "class 51 has no kernel object address; identity uses handle,type,access; close/create phases are split to expose reuse"},
            {"initial", initial.json()}, {"final", final.json()}, {"net", Probe::difference(initial, final)}, {"stages", p.stages}, {"rounds", p.rounds},
            {"ownedResourcesZero", true}, {"gateChanged", false}, {"productAcceptance", false}};
        const auto text = result.dump(2) + "\n";
        need(std::fwrite(text.data(), 1, text.size(), stdout) == text.size(), "output evidence");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "calibration exception: %s\n", error.what()); return 3; }
}
