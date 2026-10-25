// Reusable self-image fixtures. Runtime gates may rename main and include this
// source, then call fixture::tryRunChild, Controls and makeRequest directly.
#include "../native/decky_binding_client.h"
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
                for (unsigned i = 0; i < 192; ++i) emit(envelope("diagnostic", {{"index", i}, {"payload", std::string(8192, 'd')}}));
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
} // namespace ymcc::deckybinding::fixture

int main(int argc, char** argv) {
    using namespace ymcc::deckybinding;
    using namespace ymcc::deckybinding::fixture;
    int childExit = 0; if (tryRunChild(childExit)) return childExit;
    if (argc != 2) return 3;
    Json cases = Json::array();
    const auto test = [&](const char* name, const auto& body) {
        try { auto row = body(); row["name"] = name; row["passed"] = true; cases.push_back(std::move(row)); }
        catch (const std::exception& error) { cases.push_back({{"name", name}, {"passed", false}, {"reason", error.what()}}); }
    };
    test("enable-cancel-are-fences-without-session-handles", [] {
        Client zero; need(zero.enable(0), "zero-first-epoch"); zero.cancel(); need(!zero.enable(0) && zero.enable(1), "zero-epoch-fence");
        Client client; need(!client.begin(makeRequest(1), 1), "begin-without-enable");
        need(client.enable(1), "enable"); needDestroyed(client);
        need(bool(client.cancel()), "cancel"); need(!client.enable(1) && !client.begin(makeRequest(1), 1), "off-epoch-revived");
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
        detail::Handle cef{client.takeContextProcess()}; need(cef.value && GetProcessId(cef.value) == worker::number(context.lease.at("pid")), "verified-cef-transfer");
        need(client.takeContextProcess() == nullptr, "cef-transferred-twice");
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
        needDestroyed(client); return state(client.snapshot());
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
        return Json{{"firstFullyDestroyed", zero.fullyDestroyed}, {"remaining", state(remaining)}, {"atExit", state(client.snapshot())}};
    });
    test("refresh-and-ordinary-stop-preserve-enabled-epoch-off-does-not", [] {
        Controls fixture(L"blocked-read"); Client client; need(client.enable(1), "enable"); need(bool(client.begin(makeRequest(1), 1)), "begin1");
        need(bool(client.begin(makeRequest(2), 1)), "refresh"); need(client.stop().fullyDestroyed, "ordinary-stop");
        need(bool(client.begin(makeRequest(3), 1)), "stop-permanently-disabled-epoch"); client.cancel(); need(client.stop().fullyDestroyed, "off-stop");
        need(!client.begin(makeRequest(4), 1) && !client.enable(1), "old-epoch-reopened");
        need(client.enable(2) && bool(client.begin(makeRequest(1), 2)), "new-epoch-start"); need(client.stop().fullyDestroyed, "new-stop"); needDestroyed(client); return state(client.snapshot());
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
    const auto passed = std::count_if(cases.begin(), cases.end(), [](const Json& row) { return row.at("passed").get<bool>(); });
    const bool okay = static_cast<std::size_t>(passed) == cases.size();
    const Json result{{"schema", "ymcc.binding-client-component.v1"}, {"passed", okay}, {"tests", cases.size()}, {"passedCount", passed}, {"cases", cases},
        {"sameImagePrivateChildrenAndOwnedFixturesOnly", true}, {"realSteamOrDeviceActions", 0}, {"sleepActions", 0}, {"runtimeMigrationPassed", false}, {"original3200msGatePassed", false}, {"hostReleasePassed", false}};
    std::ofstream output(std::filesystem::absolute(argv[1]), std::ios::binary); output << result.dump(2) << '\n';
    std::cout << "BINDING_CLIENT cases=" << cases.size() << " passed=" << passed << '\n'; return okay ? 0 : 2;
}




