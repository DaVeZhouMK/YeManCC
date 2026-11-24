// Reusable self-image fixtures. Runtime gates may rename main and include this
// source, then call fixture::tryRunChild, Controls and makeRequest directly.
#ifdef YMCC_BINDING_IDENTITY_DELEGATION_FIXTURE
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
// Count real preparation/launch calls only on the synchronous rejection thread.
// Every wrapper forwards unchanged arguments to the real Win32 operation.
namespace ymcc::deckybinding::fixture::delegationprobe {
inline thread_local bool armed = false;
inline thread_local unsigned preparations = 0, jobs = 0, launches = 0, jobKills = 0, processKills = 0;
inline HANDLE WINAPI createEvent(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCWSTR name) {
    if (armed && !manual) ++preparations; // Owner::start's changed event; Session's callsDone is manual-reset.
    return ::CreateEventW(sa, manual, initial, name);
}
inline HANDLE WINAPI createJob(LPSECURITY_ATTRIBUTES sa, LPCWSTR name) {
    if (armed) ++jobs;
    return ::CreateJobObjectW(sa, name);
}
inline BOOL WINAPI createProcess(LPCWSTR image, LPWSTR command, LPSECURITY_ATTRIBUTES process,
    LPSECURITY_ATTRIBUTES thread, BOOL inherit, DWORD flags, LPVOID env, LPCWSTR cwd,
    LPSTARTUPINFOW startup, LPPROCESS_INFORMATION result) {
    if (armed) ++launches;
    return ::CreateProcessW(image, command, process, thread, inherit, flags, env, cwd, startup, result);
}
struct Scope {
    Scope() { preparations = jobs = launches = jobKills = processKills = 0; armed = true; }
    ~Scope() { armed = false; }
    Scope(const Scope&) = delete;
};
inline BOOL WINAPI terminateJob(HANDLE job, UINT code) {
    if (armed) ++jobKills;
    return ::TerminateJobObject(job, code);
}
inline BOOL WINAPI terminateProcess(HANDLE process, UINT code) {
    if (armed) ++processKills;
    return ::TerminateProcess(process, code);
}
}
#define CreateEventW ymcc::deckybinding::fixture::delegationprobe::createEvent
#define CreateJobObjectW ymcc::deckybinding::fixture::delegationprobe::createJob
#define CreateProcessW ymcc::deckybinding::fixture::delegationprobe::createProcess
#define TerminateJobObject ymcc::deckybinding::fixture::delegationprobe::terminateJob
#define TerminateProcess ymcc::deckybinding::fixture::delegationprobe::terminateProcess
#endif
#include "../native/decky_binding_client.h"
#include "../native/steam_failure_diagnostics.h"
#ifdef YMCC_BINDING_IDENTITY_DELEGATION_FIXTURE
#undef CreateEventW
#undef CreateJobObjectW
#undef CreateProcessW
#undef TerminateJobObject
#undef TerminateProcess
#endif
#include <fstream>
#include <iostream>
#include <thread>

namespace ymcc::deckybinding::fixture {
using Json = nlohmann::json;
inline void need(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
inline std::wstring environment(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size) return {};
    std::wstring value(size, L'\0');
    const auto count = GetEnvironmentVariableW(name, value.data(), size);
    value.resize(count); return value;
}
inline void setEnvironment(const wchar_t* name, const std::wstring& value) {
    need(SetEnvironmentVariableW(name, value.empty() ? nullptr : value.c_str()) != FALSE, "fixture-environment");
}
inline std::wstring widen(const std::string& value) { return worker::wide(Json(value)); }
inline std::vector<std::uint8_t> bytes(const std::string& value) { return {value.begin(), value.end()}; }
inline Json identity(HANDLE process = GetCurrentProcess()) { return worker::contextIdentity(process); }
inline Json makeRequest(std::uint64_t generation) {
    const auto own = identity();
    const auto root = std::filesystem::path(widen(own.at("path").get<std::string>())).parent_path();
    const auto utf8 = root.u8string();
    std::string nonce(32, '0');
    static constexpr char hex[] = "0123456789abcdef";
    auto value = generation;
    for (unsigned i = 0; i < 16; ++i) { nonce[31 - i] = hex[value & 15]; value >>= 4; }
    return {{"schema", worker::schema}, {"id", nonce}, {"generation", std::to_string(generation)},
        {"steam", {{"root", std::string(utf8.begin(), utf8.end())}, {"pid", GetCurrentProcessId()},
            {"created", own.at("birth")}, {"account", 12345678}, {"windowsSession", own.at("session")}}},
        {"bootstrap", {{"endpoint", "ws://127.0.0.1:1/fixture"}, {"token", std::string(64, 'b')}, {"runId", std::string(32, 'c')}}},
        {"requireFreshContext", true}, {"retainedFrontendOnly", false}};
}
class Controls {
    inline static std::atomic<unsigned> serial_{0};
    std::wstring oldMode_, oldReady_, oldRelease_;
public:
    std::wstring readyName, releaseName;
    detail::Handle ready, release;
    explicit Controls(const std::wstring& mode) {
        const auto prefix = L"Local\\YMCC-BindingClient-Fixture-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++serial_);
        readyName = prefix + L"-ready"; releaseName = prefix + L"-release";
        ready.value = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str());
        release.value = CreateEventW(nullptr, TRUE, FALSE, releaseName.c_str());
        need(ready.value && release.value, "fixture-control-events");
        oldMode_ = environment(L"YMCC_BINDING_CLIENT_FIXTURE_MODE");
        oldReady_ = environment(L"YMCC_BINDING_CLIENT_FIXTURE_READY");
        oldRelease_ = environment(L"YMCC_BINDING_CLIENT_FIXTURE_RELEASE");
        setEnvironment(L"YMCC_BINDING_CLIENT_FIXTURE_MODE", mode);
        setEnvironment(L"YMCC_BINDING_CLIENT_FIXTURE_READY", readyName);
        setEnvironment(L"YMCC_BINDING_CLIENT_FIXTURE_RELEASE", releaseName);
    }
    ~Controls() {
        SetEvent(release.value);
        SetEnvironmentVariableW(L"YMCC_BINDING_CLIENT_FIXTURE_MODE", oldMode_.empty() ? nullptr : oldMode_.c_str());
        SetEnvironmentVariableW(L"YMCC_BINDING_CLIENT_FIXTURE_READY", oldReady_.empty() ? nullptr : oldReady_.c_str());
        SetEnvironmentVariableW(L"YMCC_BINDING_CLIENT_FIXTURE_RELEASE", oldRelease_.empty() ? nullptr : oldRelease_.c_str());
    }
    void waitReady(DWORD ms = 5000) const { need(WaitForSingleObject(ready.value, ms) == WAIT_OBJECT_0, "fixture-ready-timeout"); }
    bool releaseUnsent() const { return WaitForSingleObject(release.value, 0) == WAIT_TIMEOUT; }
    void releaseNow() { SetEvent(release.value); }
};
inline PROCESS_INFORMATION spawnFixture(const std::wstring& image, const std::wstring& ready, const std::wstring& release) {
    std::wstring command = L"\"" + image + L"\" --ymcc-client-owned-fixture \"" + ready + L"\" \"" + release + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    need(CreateProcessW(image.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process) != FALSE, "owned-fixture-start");
    CloseHandle(process.hThread); process.hThread = nullptr; return process;
}
class Context {
    detail::Handle process_, ready_, release_;
    std::wstring readyName_, releaseName_;
public:
    Json lease;
    Context() {
        const auto prefix = L"Local\\YMCC-BindingClient-Context-" + std::to_wstring(GetCurrentProcessId());
        readyName_ = prefix + L"-ready"; releaseName_ = prefix + L"-release";
        ready_.value = CreateEventW(nullptr, TRUE, FALSE, readyName_.c_str());
        release_.value = CreateEventW(nullptr, TRUE, FALSE, releaseName_.c_str());
        const auto root = std::filesystem::path(detail::executable()).parent_path();
        const auto child = spawnFixture((root / L"bin" / L"cef" / L"fixture" / L"steamwebhelper.exe").wstring(), readyName_, releaseName_);
        process_.value = child.hProcess;
        need(WaitForSingleObject(ready_.value, 5000) == WAIT_OBJECT_0, "context-fixture-ready");
        lease = identity(process_.value);
    }
    ~Context() { if (release_.value) SetEvent(release_.value); if (process_.value) WaitForSingleObject(process_.value, 3000); }
    bool alive() const { return WaitForSingleObject(process_.value, 0) == WAIT_TIMEOUT; }
};
inline bool writeRaw(ChildChannel& channel, const void* data, DWORD size) {
    detail::Handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    OVERLAPPED overlapped{}; overlapped.hEvent = ready.value; DWORD count = 0;
    const BOOL done = WriteFile(channel.pipeHandle(), data, size, &count, &overlapped);
    if (!done && GetLastError() != ERROR_IO_PENDING) return false;
    return GetOverlappedResult(channel.pipeHandle(), &overlapped, &count, TRUE) && count == size;
}
inline bool tryRunChild(int& exitCode) {
    int argc = 0; auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) { exitCode = 87; return true; }
    if (argc == 4 && std::wstring_view(argv[1]) == L"--ymcc-client-held-endpoint") {
        const auto pipe = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(std::stoull(argv[2])));
        const auto cancel = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(std::stoull(argv[3])));
        LocalFree(argv);
        if (GetFileType(pipe) != FILE_TYPE_PIPE || WaitForSingleObject(cancel, 0) == WAIT_FAILED) { exitCode = 94; return true; }
        Sleep(INFINITE); exitCode = 0; return true; // Only the original owned Job can release this leaf.
    }
    if (argc == 4 && std::wstring_view(argv[1]) == L"--ymcc-client-owned-fixture") {
        detail::Handle ready{OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[2])};
        detail::Handle release{OpenEventW(SYNCHRONIZE, FALSE, argv[3])}; LocalFree(argv);
        if (!ready.value || !release.value) { exitCode = 88; return true; }
        SetEvent(ready.value); exitCode = WaitForSingleObject(release.value, INFINITE) == WAIT_OBJECT_0 ? 0 : 89; return true;
    }
    const auto classified = ChildChannel::classify(argc, argv); LocalFree(argv);
    if (classified == ChildMode::ordinary) return false;
    ChildChannel channel; ChildMode mode{};
    if (!channel.tryRunCommandLine(GetCommandLineW(), exitCode, mode) || mode != ChildMode::worker) { exitCode = 87; return true; }
    try {
        const auto behavior = environment(L"YMCC_BINDING_CLIENT_FIXTURE_MODE");
        detail::Handle ready{OpenEventW(EVENT_MODIFY_STATE, FALSE, environment(L"YMCC_BINDING_CLIENT_FIXTURE_READY").c_str())};
        detail::Handle release{OpenEventW(SYNCHRONIZE, FALSE, environment(L"YMCC_BINDING_CLIENT_FIXTURE_RELEASE").c_str())};
        need(ready.value && release.value, "fixture-worker-controls");
        if (behavior == L"blocked-write") {
            SetEvent(ready.value); WaitForSingleObject(release.value, INFINITE); exitCode = 0; return true;
        }
        const auto incoming = channel.readFrame(7000); need(bool(incoming), "fixture-request-read");
        const std::string text(incoming.payload.begin(), incoming.payload.end());
        const auto request = worker::parse(text);
        const auto envelope = [&](const char* kind, const Json& detail) {
            return Json{{"schema", worker::schema}, {"id", request.id}, {"generation", std::to_string(request.generation)}, {"kind", kind}, {"detail", detail}};
        };
        const auto emit = [&](const Json& packet) { need(bool(channel.writeFrame(bytes(packet.dump()), 5000)), "fixture-emit"); };
#ifdef YMCC_BINDING_IDENTITY_DELEGATION_FIXTURE
        if (behavior == L"delegated-wait-before-result" || behavior == L"delegated-wait-after-result") {
            need(request.delegatedIdentity, "delegated-terminal-requires-marker");
            worker::SteamLease steam(request, L"Software\\Valve\\Steam", nullptr, channel.cancelEvent());
            need(steam.current(), "delegated-terminal-current-at-entry");
            detail::Handle context;
            if (behavior == L"delegated-wait-after-result") {
                context.value = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                    worker::number(request.bootstrap.at("fixtureContextPid")));
                need(context.value != nullptr, "delegated-terminal-context");
                emit(envelope("result", {{"ok", true}, {"mutated", true}, {"contextLease", identity(context.value)}}));
            }
            need(SetEvent(ready.value) != FALSE, "delegated-terminal-ready");
            // This terminal port consumes ONLY the authenticated inherited event.
            // Neither mode waits on or signals the independent fixture release.
            need(channel.waitCancelled(7000) == WAIT_OBJECT_0, "delegated-terminal-cancel-timeout");
            need(!steam.current(), "delegated-terminal-current-after-revoke");
            exitCode = 0; return true;
        }
#endif
        if (behavior.starts_with(L"exited-held-")) {
            detail::Handle pipe, cancel;
            need(DuplicateHandle(GetCurrentProcess(), channel.pipeHandle(), GetCurrentProcess(), &pipe.value, 0, TRUE, DUPLICATE_SAME_ACCESS) != FALSE, "held-binding-pipe");
            need(DuplicateHandle(GetCurrentProcess(), channel.cancelEvent(), GetCurrentProcess(), &cancel.value, 0, TRUE, DUPLICATE_SAME_ACCESS) != FALSE, "held-binding-cancel");
            HANDLE inherited[]{pipe.value, cancel.value}; detail::Attributes attributes;
            need(attributes.initialize(inherited), "held-binding-attributes");
            STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
            startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW; startup.StartupInfo.wShowWindow = SW_HIDE; startup.lpAttributeList = attributes.list;
            const auto image = detail::executable();
            std::wstring command = L"\"" + image + L"\" --ymcc-client-held-endpoint " + std::to_wstring(reinterpret_cast<UINT_PTR>(pipe.value)) + L" " + std::to_wstring(reinterpret_cast<UINT_PTR>(cancel.value));
            PROCESS_INFORMATION process{};
            need(CreateProcessW(image.c_str(), command.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr, &startup.StartupInfo, &process) != FALSE, "held-binding-start");
            detail::Handle leaf{process.hProcess}, thread{process.hThread};
            emit(envelope("diagnostic", {{"fixtureRoot", identity()}, {"fixtureLeaf", identity(leaf.value)}, {"descendantPid", process.dwProcessId}}));
            if (behavior == L"exited-held-result" || behavior == L"exited-held-runtime") {
                detail::Handle context{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, worker::number(request.bootstrap.at("fixtureContextPid")))};
                need(context.value != nullptr, "held-binding-context");
                auto result = envelope("result", {{"ok", true}, {"mutated", true}, {"contextLease", identity(context.value)}});
                if (behavior == L"exited-held-runtime") result["detail"]["fixtureResultGeneration"] = std::to_string(request.generation);
                emit(result);
                for (unsigned i = 0; i < 3; ++i) emit(envelope("diagnostic", {{"fixtureQueuedIndex", i}}));
                if (behavior == L"exited-held-runtime") emit(envelope("diagnostic", {{"fixtureHealthyGeneration", std::to_string(request.generation)}}));
            } else if (behavior == L"exited-held-partial") {
                const std::uint32_t length = 42;
                need(writeRaw(channel, &length, sizeof(length)) && writeRaw(channel, "{", 1), "held-binding-partial");
            }
            SetEvent(ready.value);
            need(WaitForSingleObject(release.value, 1500) == WAIT_OBJECT_0, "held-binding-root-ack");
            exitCode = 0; return true; // Leaf deliberately keeps the pipe open after root exit.
        }
        if (behavior == L"production") {
            SetEvent(ready.value);
            exitCode = worker::run(text, channel.cancelEvent(), [&](const Json& packet) { return bool(channel.writeFrame(bytes(packet.dump()), 2500)); });
            return true;
        }
        if (behavior == L"stubborn" || behavior == L"blocked-read") {
            SetEvent(ready.value);
            WaitForSingleObject(release.value, INFINITE); // Intentionally ignores inherited cancel.
            exitCode = 0; return true;
        }
        if (behavior == L"stubborn-descendant") {
            const auto descendantReadyName = environment(L"YMCC_BINDING_CLIENT_FIXTURE_READY") + L"-descendant";
            detail::Handle descendantReady{CreateEventW(nullptr, TRUE, FALSE, descendantReadyName.c_str())};
            const auto descendant = spawnFixture(detail::executable(), descendantReadyName, environment(L"YMCC_BINDING_CLIENT_FIXTURE_RELEASE"));
            detail::Handle process{descendant.hProcess};
            need(WaitForSingleObject(descendantReady.value, 5000) == WAIT_OBJECT_0, "descendant-ready");
            emit(envelope("diagnostic", {{"descendantPid", descendant.dwProcessId}}));
            SetEvent(ready.value); WaitForSingleObject(release.value, INFINITE); exitCode = 0; return true;
        }
        SetEvent(ready.value);
        if (behavior == L"oversized" || behavior == L"empty" || behavior == L"truncated") {
            const std::uint32_t length = behavior == L"oversized" ? maxFrameBytes + 1 : behavior == L"empty" ? 0 : 42;
            need(writeRaw(channel, &length, sizeof(length)), "fixture-prefix-write");
            if (behavior == L"truncated") { exitCode = 0; return true; }
        } else if (behavior == L"json-invalid" || behavior == L"duplicate-key") {
            const auto body = behavior == L"json-invalid" ? "{" : "{\"schema\":\"first\",\"schema\":\"second\"}";
            need(bool(channel.writeFrame(bytes(body), 5000)), "fixture-invalid-json-write");
        } else {
            Json context = identity();
            if (request.bootstrap.contains("fixtureContextPid")) {
                detail::Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, worker::number(request.bootstrap.at("fixtureContextPid")))};
                context = identity(process.value);
            }
            if (behavior == L"context-birth") context["birth"] = "1";
            if (behavior == L"context-session") context["session"] = 4294967295u;
            if (behavior == L"context-path") context["path"] = "C:/foreign/steamwebhelper.exe";
            auto result = envelope("result", {{"ok", true}, {"mutated", true}, {"contextLease", context}});
            if (behavior == L"wrong-schema") result["schema"] = "untrusted";
            if (behavior == L"wrong-id") result["id"] = std::string(32, 'f');
            if (behavior == L"wrong-generation") result["generation"] = "999999999999999999";
            if (behavior == L"wrong-kind") result["kind"] = "unknown";
            if (behavior == L"token") result["detail"]["reason"] = request.bootstrap.at("token");
            emit(result);
            if (behavior == L"duplicate-result") emit(result);
            if (behavior == L"truncated-after-result") {
                const std::uint32_t length = 42; need(writeRaw(channel, &length, sizeof(length)), "fixture-partial-watch");
                exitCode = 0; return true;
            }
            if (behavior == L"burst") {
                for (unsigned i = 0; i < 192; ++i) {
                    Json facts={{"index",i},{"payload",std::string(8192,'d')}};
                    if(request.bootstrap.value("fixtureFailureTrace",false)&&i<3){
                        facts["bootstrapStage"]="context-replaced";facts["reason"]="mirror-bootstrap-reload-pending";
                        facts["steamFailure"]=ymcc::steamfailure::cdp({{"error",{{"code",-32000},{"message","Cannot find context private-token-account-url"}}}});
                        facts["steamFailure"]["privateSecret"]="secret-untrusted-value";
                        if(i!=2)facts["runId"]=i==0?request.bootstrap.at("runId"):Json(std::string(32,'f'));
                    }
                    emit(envelope("diagnostic",facts));
                }
                emit(envelope("diagnostic", {{"fixtureDone", true}}));
            }
        }
        channel.waitCancelled(15000); exitCode = 0; return true;
    } catch (...) { exitCode = 90; return true; }
}
inline Json state(const ClientSnapshot& s) {
    return {{"handles", s.handles}, {"pendingIo", s.pendingIo}, {"activeCalls", s.activeCalls},
        {"jobActiveProcesses", s.jobActiveProcesses}, {"childExited", s.childExited}, {"fullyDestroyed", s.fullyDestroyed},
        {"ownerHandles", s.owner.handles}, {"ownerPendingIo", s.owner.pendingIo}, {"readerHandles", s.readerHandles},
        {"readerPendingIo", s.readerPendingIo}, {"clientHandles", s.clientHandles}, {"epoch", s.epoch}};
}
inline void needDestroyed(Client& client) {
    const auto s = client.snapshot();
    need(s.fullyDestroyed && s.handles == 0 && s.pendingIo == 0 && s.activeCalls == 0 && s.jobActiveProcesses == 0 && s.childExited && s.owner.fullyDestroyed && s.readerFullyDestroyed, "client-residual-resources");
}
inline Packet receive(Client& client, DWORD waitMs = 5000) {
    detail::Deadline deadline(waitMs);
    for (;;) {
        auto packet = client.next(); if (packet.kind != Packet::pending) return packet;
        HANDLE ready = nullptr, child = nullptr;
        need(client.duplicateWaitHandles(ready, child), "fixture-wait-lease");
        detail::Handle readyLease{ready}, childLease{child};
        HANDLE handles[2]{}; DWORD count = 0;
        if (ready) handles[count++] = ready;
        if (child) handles[count++] = child;
        need(count != 0, "fixture-no-wait-handles");
        const auto waited = WaitForMultipleObjects(count, handles, FALSE, deadline.left());
        need(waited >= WAIT_OBJECT_0 && waited < WAIT_OBJECT_0 + count, "fixture-packet-timeout");
    }
}
inline unsigned threadCount() {
    detail::Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)}; need(snapshot.value != nullptr, "thread-snapshot");
    THREADENTRY32 thread{sizeof(thread)}; unsigned count = 0;
    need(Thread32First(snapshot.value, &thread) != FALSE, "thread-first");
    do { if (thread.th32OwnerProcessID == GetCurrentProcessId()) ++count; } while (Thread32Next(snapshot.value, &thread));
    return count;
}
inline Json exitedPeerMeasurements = Json::array();
inline void checkExitedBindingPeer(const std::wstring& mode) {
    Controls fixture(mode); Context context; Client client;
    auto request = makeRequest(1); request["bootstrap"]["fixtureContextPid"] = context.lease.at("pid");
    need(client.enable(1) && bool(client.begin(request, 1)), "held-binding-begin"); fixture.waitReady();
    const auto proof = receive(client);
    need(proof.kind == Packet::message && proof.value.at("kind") == "diagnostic", "held-binding-proof");
    detail::Handle root{OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, worker::number(proof.value.at("detail").at("fixtureRoot").at("pid")))};
    detail::Handle leaf{OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, worker::number(proof.value.at("detail").at("fixtureLeaf").at("pid")))};
    const bool exactIdentity = root.value && leaf.value && identity(root.value) == proof.value.at("detail").at("fixtureRoot") && identity(leaf.value) == proof.value.at("detail").at("fixtureLeaf");
    need(exactIdentity && WaitForSingleObject(leaf.value, 0) == WAIT_TIMEOUT, "held-binding-exact-process-identities");
    fixture.releaseNow(); need(WaitForSingleObject(root.value, 1500) == WAIT_OBJECT_0, "held-binding-root-did-not-exit");
    const bool endpointHeld = WaitForSingleObject(leaf.value, 0) == WAIT_TIMEOUT;
    FILETIME created{}, exit{}, kernel0{}, user0{}, kernel1{}, user1{};
    need(GetThreadTimes(GetCurrentThread(), &created, &exit, &kernel0, &user0) != FALSE, "held-binding-cpu-before");
    const auto started = GetTickCount64(); unsigned calls = 0, results = 0, watch = 0;
    Packet terminal;
    for (;;) {
        ++calls; auto packet = client.next();
        if (packet.kind == Packet::message) {
            if (packet.value.at("kind") == "result") ++results;
            else if (packet.value.at("detail").contains("fixtureQueuedIndex")) {
                need(packet.value.at("detail").at("fixtureQueuedIndex") == watch, "held-binding-watch-order"); ++watch;
            }
        } else if (packet.kind == Packet::ended || packet.kind == Packet::failed) { terminal = std::move(packet); break; }
        if (GetTickCount64() - started >= 180) break;
        HANDLE ready = nullptr, child = nullptr;
        need(client.duplicateWaitHandles(ready, child), "held-binding-wait-lease");
        detail::Handle readyLease{ready}, childLease{child}; HANDLE waits[2]{}; DWORD count = 0;
        if (ready) waits[count++] = ready; if (child) waits[count++] = child;
        need(count != 0, "held-binding-wait-handles");
        const auto elapsed = GetTickCount64() - started;
        const auto waited = WaitForMultipleObjects(count, waits, FALSE, elapsed >= 180 ? 0u : static_cast<DWORD>(180 - elapsed));
        need(waited != WAIT_FAILED, "held-binding-wait-failed");
    }
    const auto elapsed = GetTickCount64() - started;
    need(GetThreadTimes(GetCurrentThread(), &created, &exit, &kernel1, &user1) != FALSE, "held-binding-cpu-after");
    const auto ticks = [](const FILETIME& value) { return (static_cast<unsigned long long>(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
    const auto stopStarted = GetTickCount64(); const auto stopped = client.stop(1500); const auto stopMs = GetTickCount64() - stopStarted;
    const bool leafExited = WaitForSingleObject(leaf.value, 0) == WAIT_OBJECT_0;
    const auto remaining = client.snapshot();
    std::string modeText; for (const wchar_t c : mode) modeText.push_back(static_cast<char>(c)); // Fixed ASCII fixture modes only.
    const bool queued = mode == L"exited-held-result";
    const auto expected = queued ? "binding-worker-exited" : "binding-worker-exited-before-result";
    exitedPeerMeasurements.push_back({{"mode", modeText}, {"calls", calls}, {"maximumCalls", 128}, {"elapsedMs", elapsed},
        {"threadCpuMs", (ticks(kernel1) - ticks(kernel0) + ticks(user1) - ticks(user0)) / 10000.0},
        {"exactIdentity", exactIdentity}, {"endpointHeldAfterRootExit", endpointHeld}, {"queuedResults", results}, {"queuedWatchFrames", watch},
        {"terminalKind", terminal.kind}, {"terminalReason", terminal.reason}, {"stopMs", stopMs}, {"leafExited", leafExited},
        {"atExit", state(remaining)}, {"externalContextAlive", context.alive()}, {"manualLeafRelease", false}});
    need(stopped.fullyDestroyed && leafExited && stopMs < 1500 && context.alive(), "held-binding-owned-stop"); needDestroyed(client);
    need(endpointHeld && results == (queued ? 1u : 0u) && watch == (queued ? 3u : 0u), "held-binding-committed-fifo-lost");
    need(terminal.kind == Packet::failed && terminal.reason == expected && calls <= 128, "exited binding worker stayed pending behind inherited endpoint");
}
#ifdef YMCC_BINDING_IDENTITY_DELEGATION_FIXTURE
// A real Observer, its real registry subscription and its own publisher thread.
// No fake lease constructor, separate subscriber thread or release-based revoke.
class DelegationPublisher {
    struct PrivateKey {
        std::wstring path;
        ~PrivateKey() { RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()); }
    } key_;
    detail::Handle publication_{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    std::wstring root_;
    ULONGLONG birth_ = 0;
    DWORD windowsSession_ = 0;
    inline static std::atomic<unsigned> serial_{0};
public:
    ymcc::steamsession::Observer observer;
private:
    // Constructed BEFORE start; unwinding a constructor/assertion also stops.
    // It dies before Observer, publication event and private key, in that order.
    struct StopObserver {
        ymcc::steamsession::Observer& value;
        ~StopObserver() { value.stop(); }
    } stop_;
    void number(const wchar_t* name, DWORD value) {
        HKEY key = nullptr;
        const auto path = key_.path + L"\\ActiveProcess";
        need(RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
            nullptr, &key, nullptr) == ERROR_SUCCESS, "delegation-private-active-key");
        const auto written = RegSetValueExW(key, name, 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key); need(written == ERROR_SUCCESS, "delegation-private-active-value");
    }
public:
    static constexpr DWORD account = 12345678;
    DelegationPublisher()
        : key_{L"Software\\YeManCC-BindingClient-DelegationSelftest\\" +
            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) +
            L"-" + std::to_wstring(++serial_)}, observer(key_.path), stop_{observer} {
        const auto image = std::filesystem::path(detail::executable());
        need(_wcsicmp(image.filename().c_str(), L"steam.exe") == 0, "delegation-own-image-must-be-steam-exe");
        need(publication_.value != nullptr, "delegation-publication-event");
        root_ = ymcc::steamlive::normalizedPath(image.parent_path().wstring());
        const auto own = identity(); birth_ = worker::decimal(own.at("birth"));
        windowsSession_ = worker::number(own.at("session"), false);
        HKEY key = nullptr;
        need(RegCreateKeyExW(HKEY_CURRENT_USER, key_.path.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
            nullptr, &key, nullptr) == ERROR_SUCCESS, "delegation-private-root-key");
        const auto written = RegSetValueExW(key, L"SteamPath", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(root_.c_str()), static_cast<DWORD>((root_.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key); need(written == ERROR_SUCCESS, "delegation-private-root-value");
        number(L"pid", GetCurrentProcessId()); number(L"ActiveUser", account);
        need(observer.start([this](bool) { SetEvent(publication_.value); }, [] { return false; }),
            "delegation-real-observer-start");
        waitAccount(account);
    }
    std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> waitAccount(DWORD value, std::uint64_t afterRevision = 0) {
        detail::Deadline deadline(3000);
        for (;;) {
            const auto receipt = observer.identitySnapshot();
            if (receipt && receipt->presence == 1 && receipt->session.root == root_ &&
                receipt->session.pid == GetCurrentProcessId() && receipt->session.created == birth_ &&
                receipt->windowsSessionKnown && receipt->windowsSession == windowsSession_ &&
                receipt->session.account == value && receipt->revision > afterRevision) return receipt;
            need(WaitForSingleObject(publication_.value, deadline.left()) == WAIT_OBJECT_0,
                "delegation-real-publication-timeout");
        }
    }
    void changeAccount(DWORD value) { number(L"ActiveUser", value); }
    std::shared_ptr<ymcc::steamsession::IdentityDelegation> delegate(bool ui = false) {
        const auto receipt = observer.identitySnapshot();
        auto lease = observer.delegateIdentity(receipt, account, ui);
        need(lease && lease->current(), "delegation-real-lease"); return lease;
    }
    Json request(std::uint64_t generation, const std::shared_ptr<ymcc::steamsession::IdentityDelegation>& lease) {
        auto result = makeRequest(generation);
        const auto& expected = lease->expected();
        const auto path = std::filesystem::path(expected.session.root).u8string();
        result["steam"] = {{"root", std::string(path.begin(), path.end())}, {"pid", expected.session.pid},
            {"created", std::to_string(expected.session.created)}, {"account", lease->resolvedAccount()},
            {"windowsSession", expected.windowsSession}, {"identitySource", "shared-steam-observer.v1"}};
        if (lease->liveUiAccount()) result["steam"]["accountSource"] = "verified-steam-ui";
        return result;
    }
};
inline void delegatedRejectionsBeforeOwner() {
    DelegationPublisher publisher;
    std::uint64_t generation = 1000;
    const auto reject = [&](Json request, std::shared_ptr<ymcc::steamsession::IdentityDelegation> lease) {
        Controls fixture(L"delegated-wait-before-result"); Client client;
        need(client.enable(1), "delegation-reject-enable");
        {
            delegationprobe::Scope probe;
            need(!client.begin(request, 1, std::move(lease)), "delegation-mismatch-admitted");
            need(delegationprobe::preparations == 0 && delegationprobe::jobs == 0 && delegationprobe::launches == 0,
                "delegation-rejection-after-owner-preparation-or-worker");
        }
        const auto refused = client.snapshot();
        need(!refused.owner.started && !refused.owner.processId && !refused.owner.handles &&
            !refused.owner.jobLimitFlags && !refused.owner.jobActiveProcesses,
            "delegation-rejection-created-owner-resources");
        need(WaitForSingleObject(fixture.ready.value, 0) == WAIT_TIMEOUT && fixture.releaseUnsent(),
            "delegation-rejection-worker-or-release-observed");
        need(client.stop().fullyDestroyed, "delegation-reject-stop"); needDestroyed(client);
    };
    { auto lease = publisher.delegate(); reject(publisher.request(++generation, lease), {}); }
    { auto lease = publisher.delegate(); auto request = publisher.request(++generation, lease);
      request["steam"].erase("identitySource"); reject(std::move(request), std::move(lease)); }
    for (unsigned field = 0; field != 7; ++field) {
        auto lease = publisher.delegate(); auto request = publisher.request(++generation, lease);
        if (field == 0) request["steam"]["root"] = request["steam"]["root"].get<std::string>() + "/wrong-root";
        if (field == 1) request["steam"]["pid"] = GetCurrentProcessId() == MAXDWORD ? MAXDWORD - 1 : GetCurrentProcessId() + 1;
        if (field == 2) request["steam"]["created"] = std::to_string(lease->expected().session.created + 1);
        if (field == 3) request["steam"]["windowsSession"] = lease->expected().windowsSession == MAXDWORD ? MAXDWORD - 1 : lease->expected().windowsSession + 1;
        if (field == 4) request["steam"]["account"] = DelegationPublisher::account + 1;
        if (field == 5) request["steam"]["accountSource"] = "verified-steam-ui"; // Valid UI marker, wrong policy for this non-UI lease.
        if (field == 6) request["steam"]["identitySource"] = "ordinary-cancel-is-not-delegation";
        reject(std::move(request), std::move(lease));
    }
    { auto lease = publisher.delegate(true); auto request = publisher.request(++generation, lease);
      request["steam"].erase("accountSource"); reject(std::move(request), std::move(lease)); }
}
inline void delegatedBootstrapAndLifetimeCancellation(Context& context) {
    for (const bool afterResult : {false, true}) {
        DelegationPublisher publisher;
        Controls fixture(afterResult ? L"delegated-wait-after-result" : L"delegated-wait-before-result");
        auto lease = publisher.delegate(); auto request = publisher.request(2000 + unsigned(afterResult), lease);
        request["bootstrap"]["fixtureContextPid"] = context.lease.at("pid");
        Client client; need(client.enable(1), "delegation-lifetime-enable");
        need(bool(client.begin(request, 1, lease)), "delegation-lifetime-begin");
        fixture.waitReady(3000);
        HANDLE ready = nullptr, child = nullptr;
        need(client.duplicateWaitHandles(ready, child), "delegation-observe-child");
        detail::Handle readyObservation{ready}, childObservation{child}, retainedContext;
        detail::Handle childQuery{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, client.snapshot().processId)};
        need(childQuery.value && WaitForSingleObject(childQuery.value,0)==WAIT_TIMEOUT,"delegation-query-exact-held-child");
        need(childObservation.value != nullptr, "delegation-child-handle");
        if (afterResult) {
            const auto result = receive(client);
            need(result.kind == Packet::message && result.value.at("kind") == "result" &&
                result.value.at("detail").at("ok") == true, "delegation-correlated-success");
            retainedContext.value = client.takeContextProcess();
            need(retainedContext.value && GetProcessId(retainedContext.value) == worker::number(context.lease.at("pid")),
                "delegation-retained-context-transfer");
        } else need(client.next().kind == Packet::pending, "delegation-bootstrap-terminal-sent-result");
        need(WaitForSingleObject(childObservation.value, 0) == WAIT_TIMEOUT && fixture.releaseUnsent(),
            "delegation-child-exited-before-producer-revoke");
        const auto previous = publisher.observer.identitySnapshot();
        if (afterResult) {
            publisher.changeAccount(DelegationPublisher::account + 1);
            const auto changed = publisher.waitAccount(DelegationPublisher::account + 1, previous->revision);
            need(changed->session.root == previous->session.root && changed->session.pid == previous->session.pid &&
                changed->session.created == previous->session.created && changed->processEpoch == previous->processEpoch &&
                changed->accountEpoch != previous->accountEpoch, "delegation-change-was-not-same-process-account");
        } else publisher.observer.stop();
        need(!lease->current() && WaitForSingleObject(lease->cancellationEvent(), 0) == WAIT_OBJECT_0,
            "delegation-producer-did-not-signal-lease");
        // No Client cancellation/stop has occurred: only the producer could
        // have signalled the event this actual child is now required to exit on.
        need(WaitForSingleObject(childObservation.value, 2500) == WAIT_OBJECT_0, "delegation-child-missed-inherited-revoke");
        DWORD childExit = STILL_ACTIVE;
        need(GetExitCodeProcess(childQuery.value, &childExit) && childExit == 0, "delegation-child-not-cooperative-zero");
        need(fixture.releaseUnsent() && !client.snapshot().owner.jobTerminationAttempted,
            "delegation-exit-used-release-or-hard-kill");
        {
            delegationprobe::Scope probe;
            need(client.stop().fullyDestroyed, "delegation-lifetime-stop");
            need(delegationprobe::jobKills == 0 && delegationprobe::processKills == 0,
                "delegation-stop-hard-killed-owned-process");
        }
        needDestroyed(client);
        need(!client.snapshot().owner.jobTerminationAttempted && fixture.releaseUnsent(),
            "delegation-stop-used-hard-kill-or-release");
        need(context.alive() && (!retainedContext.value || WaitForSingleObject(retainedContext.value, 0) == WAIT_TIMEOUT),
            "delegation-context-was-owned-job-target");
    }
}
inline void delegatedQueuedSuccessRevocation(Context& context) {
    // First: leave the correlated success queued until AFTER producer stop.
    // Second: parse success while current, then revoke before context transfer.
    for (const bool parseBeforeRevoke : {false, true}) {
        DelegationPublisher publisher; Controls fixture(L"delegated-wait-after-result");
        auto lease = publisher.delegate(); auto request = publisher.request(3000 + unsigned(parseBeforeRevoke), lease);
        request["bootstrap"]["fixtureContextPid"] = context.lease.at("pid");
        Client client; need(client.enable(1), "delegation-queued-enable");
        need(bool(client.begin(request, 1, lease)), "delegation-queued-begin");
        fixture.waitReady(3000); // Child has committed the original correlated success to the real pipe.
        HANDLE ready = nullptr, child = nullptr;
        need(client.duplicateWaitHandles(ready, child), "delegation-queued-child-observation");
        detail::Handle readyObservation{ready}, childObservation{child};
        detail::Handle childQuery{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, client.snapshot().processId)};
        need(childQuery.value && WaitForSingleObject(childQuery.value,0)==WAIT_TIMEOUT,"delegation-queued-query-exact-held-child");
        need(childObservation.value != nullptr, "delegation-queued-child-handle");
        if (parseBeforeRevoke) {
            const auto success = receive(client);
            need(success.kind == Packet::message && success.value.at("kind") == "result" &&
                success.value.at("detail").at("ok") == true, "delegation-current-success-not-received");
        }
        need(WaitForSingleObject(childObservation.value, 0) == WAIT_TIMEOUT && fixture.releaseUnsent(),
            "delegation-queued-child-not-waiting");
        publisher.observer.stop();
        need(!lease->current(), "delegation-queued-stop-did-not-revoke");
        // Observe natural exit BEFORE next/takeContext/Client.stop can cancel anything.
        need(WaitForSingleObject(childObservation.value, 2500) == WAIT_OBJECT_0, "delegation-queued-child-missed-stop");
        DWORD code = STILL_ACTIVE;
        need(GetExitCodeProcess(childQuery.value, &code) && code == 0 && fixture.releaseUnsent(),
            "delegation-queued-child-not-natural-zero");
        if (parseBeforeRevoke) {
            detail::Handle forbidden{client.takeContextProcess()};
            need(!forbidden.value, "delegation-revoked-parsed-success-granted-context");
        }
        const auto refused = client.next();
        need(refused.kind == Packet::failed || refused.kind == Packet::ended,
            "delegation-revoked-queued-success-published-message");
        need(!refused.value.is_object() || !refused.value.contains("mutated") ||
            refused.value.at("mutated") != false, "delegation-revoked-success-claimed-no-mutation");
        detail::Handle forbidden{client.takeContextProcess()};
        need(!forbidden.value, "delegation-revoked-queued-success-granted-context");
        {
            delegationprobe::Scope probe;
            need(client.stop().fullyDestroyed, "delegation-queued-stop");
            need(!delegationprobe::jobKills && !delegationprobe::processKills,
                "delegation-queued-stop-hard-killed");
        }
        needDestroyed(client);
        need(fixture.releaseUnsent() && context.alive(), "delegation-queued-release-or-context-target");
    }
}
#endif
} // namespace ymcc::deckybinding::fixture

#ifdef YMCC_RETAINED_READER_FIXTURE
#include "fixtures/decky_reader_retained_cases.inc"
#endif
#ifdef YMCC_SOURCE_NOTIFICATION_DIAGNOSTIC_FIXTURE
#include "../native/decky_sidebar_runtime.h"
#include "../native/decky_sidebar_diagnostics.h"
#include "../native/decky_sidebar_setup.h"
#include "../native/decky_steam_restart_requirement.h"
#include "fixtures/decky_source_notification_diagnostic_cases.inc"
#endif
int main(int argc, char** argv) {
    using namespace ymcc::deckybinding;
    using namespace ymcc::deckybinding::fixture;
    int childExit = 0; if (tryRunChild(childExit)) return childExit;
    if (argc != 2 && !(argc == 4 && std::string_view(argv[2]) == "--case" && *argv[3])) return 3;
    const std::string selection = argc == 4 ? argv[3] : "";
    Json cases = Json::array();
    const auto test = [&](const char* name, const auto& body) {
        if (!selection.empty() && selection != name) return;
        try { auto row = body(); row["name"] = name; row["passed"] = true; cases.push_back(std::move(row)); }
        catch (const std::exception& error) { cases.push_back({{"name", name}, {"passed", false}, {"reason", error.what()}}); }
    };
    test("enable-cancel-are-fences-without-session-handles", [] {
        Client zero; need(zero.enable(0), "zero-first-epoch"); zero.cancel(); need(!zero.enable(0) && zero.enable(1), "zero-epoch-fence");
        Client client; need(!client.begin(makeRequest(1), 1), "begin-without-enable");
        need(client.enable(1), "enable"); needDestroyed(client);
        need(bool(client.cancel()), "cancel"); need(!client.enable(1) && !client.begin(makeRequest(1), 1), "off-epoch-revived");
#ifdef YMCC_BINDING_IDENTITY_DELEGATION_FIXTURE
        delegatedRejectionsBeforeOwner();
#endif
        need(client.enable(2) && !client.enable(1), "new-epoch-fence"); needDestroyed(client); return state(client.snapshot());
    });
    test("actual-production-worker-refusal-is-correlated", [] {
        Controls fixture(L"production"); Client client; need(client.enable(1), "enable");
        need(bool(client.begin(makeRequest(1), 1)), "begin");
        const auto packet = receive(client);
        need(packet.kind == Packet::message && packet.value["kind"] == "result" && packet.value["detail"]["ok"] == false && packet.value["detail"]["mutated"] == false, "production-refusal");
        need(client.stop().fullyDestroyed, "stop"); needDestroyed(client); return state(client.snapshot());
    });
    test("event-drain-success-and-193-watch-frames-no-parent-worker", [] {
        Controls fixture(L"burst"); Context context; Client client; need(client.enable(1), "enable");
        auto request = makeRequest(1); request["bootstrap"]["fixtureContextPid"] = context.lease["pid"];
        const auto before = threadCount(); DWORD handlesBefore = 0; need(GetProcessHandleCount(GetCurrentProcess(), &handlesBefore) != FALSE, "handle-baseline");
        need(bool(client.begin(request, 1)), "begin");
        auto first = receive(client); need(first.kind == Packet::message && first.value["kind"] == "result", "success-frame");
        DiagnosticContextProcess attribution;
        const auto beforeTransfer=GetTickCount64();
        detail::Handle cef{client.takeContextProcess(&attribution)}; need(cef.value && GetProcessId(cef.value) == worker::number(context.lease.at("pid")), "verified-cef-transfer");
        need(attribution.pid==GetProcessId(cef.value)&&attribution.birth==worker::decimal(context.lease.at("birth"))&&
             attribution.steamPid==worker::number(request.at("steam").at("pid"))&&attribution.steamBirth==worker::decimal(request.at("steam").at("created"))&&
             attribution.observedAt>=beforeTransfer&&attribution.observedAt<=GetTickCount64(),"diagnostic-uses-verified-transfer-and-original-steam-birth");
        need(client.takeContextProcess(&attribution) == nullptr, "cef-transferred-twice");
        need(!attribution.pid&&!attribution.birth&&!attribution.steamPid&&!attribution.steamBirth&&!attribution.observedAt,"failed-transfer-clears-attribution");
        unsigned diagnostics = 0;
        for (;;) {
            const auto packet = receive(client); need(packet.kind == Packet::message && packet.value["kind"] == "diagnostic", "watch-frame"); ++diagnostics;
            if (packet.value["detail"].value("fixtureDone", false)) break;
        }
        need(diagnostics == 193 && threadCount() == before, "watch-drain-or-parent-thread");
        need(client.stop().fullyDestroyed, "stop"); needDestroyed(client);
        need(context.alive() && WaitForSingleObject(cef.value, 0) == WAIT_TIMEOUT, "cef-was-job-target");
        DWORD closeError = 0; need(cef.close(closeError), "transferred-cef-close");
        DWORD handlesAfter = 0; need(GetProcessHandleCount(GetCurrentProcess(), &handlesAfter) && handlesAfter == handlesBefore, "parent-handles-not-returned");
#ifdef YMCC_BINDING_IDENTITY_DELEGATION_FIXTURE
        delegatedBootstrapAndLifetimeCancellation(context);
        delegatedQueuedSuccessRevocation(context);
#endif
        return Json{{"atExit", state(client.snapshot())}, {"diagnostics", diagnostics}, {"parentThreadsBefore", before}, {"parentThreadsAfter", threadCount()}, {"externalCefStillAlive", true}, {"parentHandlesBefore", handlesBefore}, {"parentHandlesAfter", handlesAfter}};
    });
    for (const auto mode : {L"wrong-schema", L"wrong-id", L"wrong-generation", L"wrong-kind", L"duplicate-key", L"json-invalid", L"oversized", L"empty", L"truncated", L"token", L"duplicate-result", L"truncated-after-result"}) {
        std::string name = "reject-"; for (const auto* c = mode; *c; ++c) name.push_back(static_cast<char>(*c));
        test(name.c_str(), [mode] {
            Controls fixture(mode); Client client; need(client.enable(1), "enable"); need(bool(client.begin(makeRequest(1), 1)), "begin");
            auto packet = receive(client);
            if (std::wstring_view(mode) == L"duplicate-result" || std::wstring_view(mode) == L"truncated-after-result") { need(packet.kind == Packet::message, "first-result"); packet = receive(client); }
            need(packet.kind == Packet::failed && !packet.reason.empty() && packet.reason.find(std::string(64, 'b')) == std::string::npos, "invalid-frame-accepted");
            need(client.takeContextProcess() == nullptr, "invalid-frame-context-admitted");
            need(client.stop().fullyDestroyed, "stop"); needDestroyed(client); return Json{{"reason", packet.reason}, {"atExit", state(client.snapshot())}};
        });
    }
    for (const auto mode : {L"context-birth", L"context-path", L"context-session"}) {
        std::string name = "reject-"; for (const auto* c = mode; *c; ++c) name.push_back(static_cast<char>(*c));
        test(name.c_str(), [mode] {
            Controls fixture(mode); Context context; Client client; need(client.enable(1), "enable"); auto request = makeRequest(1);
            request["bootstrap"]["fixtureContextPid"] = context.lease["pid"];
            need(bool(client.begin(request, 1)) && receive(client).kind == Packet::message, "context-fixture-start");
            need(!client.takeContextProcess(), "wrong-context-transferred"); need(client.stop().fullyDestroyed, "stop"); needDestroyed(client); return state(client.snapshot());
        });
    }
    test("off-cancels-real-blocked-write-and-read-with-active-begin", [] {
        Controls fixture(L"blocked-write"); Client client; need(client.enable(1), "enable"); auto request = makeRequest(1);
        request["bootstrap"]["padding"] = std::string(110000, 'x');
        Result begun; std::thread caller([&] { begun = client.begin(request, 1); });
        // Always join before throwing, including a failed fixture handshake.
        const auto ready = WaitForSingleObject(fixture.ready.value, 5000);
        const auto before = client.snapshot(); const auto start = GetTickCount64();
        const auto canceled = client.cancel(); const auto cancelMs = GetTickCount64() - start;
        const auto stopped = client.stop(1500); caller.join();
        need(ready == WAIT_OBJECT_0 && before.owner.pendingIo != 0 && before.readerPendingIo != 0 && before.activeCalls != 0, "not-real-blocked-io");
        need(bool(canceled) && cancelMs < 150 && stopped.fullyDestroyed && !begun, "off-write-stop");
        need(fixture.releaseUnsent() && !client.begin(makeRequest(2), 1) && !client.enable(1), "off-late-begin-or-release");
        needDestroyed(client);
        return Json{{"beforeOff", state(before)}, {"cancelMs", cancelMs}, {"stopMs", GetTickCount64() - start}, {"atExit", state(client.snapshot())}, {"fixtureReleaseUnsent", true}};
    });
    test("wait-duplicates-survive-concurrent-off-stop", [] {
        Controls fixture(L"blocked-read"); Client client; need(client.enable(1), "enable"); need(bool(client.begin(makeRequest(1), 1)), "begin"); fixture.waitReady();
        HANDLE ready = nullptr, child = nullptr; need(client.duplicateWaitHandles(ready, child) && ready && child, "duplicate-waits");
        detail::Handle readyLease{ready}, childLease{child}, wake{CreateEventW(nullptr, TRUE, FALSE, nullptr)}, entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        DWORD waited = WAIT_FAILED;
        std::thread waiter([&] { HANDLE waits[]{wake.value, ready, child}; SetEvent(entered.value); waited = WaitForMultipleObjects(3, waits, FALSE, 5000); });
        WaitForSingleObject(entered.value, 5000); SetEvent(wake.value); client.cancel();
        const auto stopped = client.stop(); waiter.join();
        need(waited == WAIT_OBJECT_0 && stopped.fullyDestroyed, "off-wait-failed");
        DWORD flags = 0; need(GetHandleInformation(ready, &flags) && GetHandleInformation(child, &flags) && WaitForSingleObject(child, 0) == WAIT_OBJECT_0, "wait-duplicate-invalidated");
        needDestroyed(client);
        for (const auto mode : {L"exited-held-empty", L"exited-held-result", L"exited-held-partial"}) checkExitedBindingPeer(mode);
        return state(client.snapshot());
    });
    test("uncooperative-child-and-descendant-job-stop-before-release", [] {
        Controls fixture(L"stubborn-descendant"); Client client; need(client.enable(1), "enable"); need(bool(client.begin(makeRequest(1), 1)), "begin"); fixture.waitReady();
        const auto packet = receive(client); need(packet.kind == Packet::message, "descendant-diagnostic");
        detail::Handle descendant{OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, worker::number(packet.value["detail"]["descendantPid"]))};
        need(descendant.value && WaitForSingleObject(descendant.value, 0) == WAIT_TIMEOUT, "descendant-not-alive");
        const auto before = client.snapshot(); const auto start = GetTickCount64(); client.cancel(); const auto stopped = client.stop(); const auto elapsed = GetTickCount64() - start;
        need(stopped.fullyDestroyed && elapsed < 1500 && fixture.releaseUnsent(), "job-stop-missed-deadline");
        const bool signaledAtReturn = WaitForSingleObject(descendant.value, 0) == WAIT_OBJECT_0;
        const auto remaining = elapsed >= 1500 ? 0u : static_cast<DWORD>(1500 - elapsed);
        need(WaitForSingleObject(descendant.value, remaining) == WAIT_OBJECT_0 && GetTickCount64() - start < 1500 && fixture.releaseUnsent(), "descendant-survived-deadline"); needDestroyed(client);
        return Json{{"beforeOff", state(before)}, {"stopMs", elapsed}, {"descendantSignaledAtStopReturn", signaledAtReturn}, {"descendantObservedMs", GetTickCount64() - start}, {"fixtureReleaseUnsent", true}, {"atExit", state(client.snapshot())}};
    });
    test("zero-budget-stop-preserves-incomplete-state-and-is-repeatable", [] {
        Controls fixture(L"blocked-read"); Client client; need(client.enable(1), "enable"); need(bool(client.begin(makeRequest(1), 1)), "begin"); fixture.waitReady();
        const auto zero = client.stop(0); const auto remaining = client.snapshot();
        need(zero.fullyDestroyed || (zero.code == Code::cleanupIncomplete && !remaining.fullyDestroyed), "incomplete-stop-was-cleared");
        need(client.stop().fullyDestroyed && client.stop().fullyDestroyed, "repeat-stop"); needDestroyed(client);
#ifdef YMCC_RETAINED_READER_FIXTURE
        const auto retired=retainedreader::run();
        return Json{{"firstFullyDestroyed",zero.fullyDestroyed},{"remaining",state(remaining)},{"atExit",state(client.snapshot())},{"retainedReader",retired}};
#else
        return Json{{"firstFullyDestroyed", zero.fullyDestroyed}, {"remaining", state(remaining)}, {"atExit", state(client.snapshot())}};
#endif
    });
    test("refresh-and-ordinary-stop-preserve-enabled-epoch-off-does-not", [] {
#ifdef YMCC_SOURCE_NOTIFICATION_DIAGNOSTIC_FIXTURE
        return sourceNotificationFixture::run();
#else
        Controls fixture(L"blocked-read"); Client client; need(client.enable(1), "enable"); need(bool(client.begin(makeRequest(1), 1)), "begin1");
        need(bool(client.begin(makeRequest(2), 1)), "refresh"); need(client.stop().fullyDestroyed, "ordinary-stop");
        need(bool(client.begin(makeRequest(3), 1)), "stop-permanently-disabled-epoch"); client.cancel(); need(client.stop().fullyDestroyed, "off-stop");
        need(!client.begin(makeRequest(4), 1) && !client.enable(1), "old-epoch-reopened");
        need(client.enable(2) && bool(client.begin(makeRequest(1), 2)), "new-epoch-start"); need(client.stop().fullyDestroyed, "new-stop"); needDestroyed(client); return state(client.snapshot());
#endif
    });
    test("request-replay-rejected-and-stop-start-race-does-not-resurrect", [] {
        Controls fixture(L"blocked-read"); Client client; need(client.enable(1), "enable");
        need(bool(client.begin(makeRequest(1), 1)), "begin"); need(!client.begin(makeRequest(1), 1), "request-replay"); need(client.stop().fullyDestroyed, "replay-stop");
        for (std::uint64_t epoch = 2; epoch < 26; ++epoch) {
            need(client.enable(epoch), "race-enable"); Result result;
            std::thread caller([&] { result = client.begin(makeRequest(epoch), epoch); });
            client.cancel(); const auto stopped = client.stop(); caller.join();
            need(stopped.fullyDestroyed && !client.begin(makeRequest(epoch + 1), epoch), "race-late-owner"); needDestroyed(client);
        }
        return state(client.snapshot());
    });
    if (cases.empty()) return 64; // Unknown exact selections execute no bodies.
    const auto passed = std::count_if(cases.begin(), cases.end(), [](const Json& row) { return row.at("passed").get<bool>(); });
    const bool okay = static_cast<std::size_t>(passed) == cases.size();
    const Json result{{"schema", "ymcc.binding-client-component.v1"}, {"passed", okay}, {"tests", cases.size()}, {"passedCount", passed}, {"cases", cases}, {"selection", selection}, {"exitedPeerMeasurements", exitedPeerMeasurements}, 
#ifdef YMCC_RETAINED_READER_FIXTURE
        {"retainedReaderMeasurements",retainedreader::measurements},
#endif

        {"sameImagePrivateChildrenAndOwnedFixturesOnly", true}, {"realSteamOrDeviceActions", 0}, {"sleepActions", 0}, {"runtimeMigrationPassed", false}, {"original3200msGatePassed", false}, {"hostReleasePassed", false}};
    std::ofstream output(std::filesystem::absolute(argv[1]), std::ios::binary); output << result.dump(2) << '\n';
    std::cout << "BINDING_CLIENT cases=" << cases.size() << " passed=" << passed << '\n'; return okay ? 0 : 2;
}




