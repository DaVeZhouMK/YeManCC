// Owned, hidden Win32 fixture. No real YMCC, game, service, sleep or hardware is touched.
// Include the production reader, probes and decision core; only kill/spawn/time are mocked.
#define wWinMain unusedRecoveryServiceEntry
#include "../native/recovery_service.cpp"
#undef wWinMain
#include <thread>
#include <cassert>

static HANDLE fixtureReady, fixtureStop, fixtureBlocked, fixturePong;
static std::atomic<HWND> fixtureWindow{nullptr};
static std::atomic<unsigned> fixturePulse{0};
static std::atomic<bool> fixtureScriptPong{true};
constexpr UINT WM_FIXTURE_BLOCK = WM_APP + 41;

static LRESULT CALLBACK fixtureWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_RECOVERY_WEBVIEW_PING) {
        fixturePulse.fetch_add(1);
        if (fixtureScriptPong.load()) SetEvent(fixturePong);
        return 0;
    }
    if (m == WM_FIXTURE_BLOCK) {
        SetEvent(fixtureBlocked);
        WaitForSingleObject(fixtureStop, INFINITE); // Only this owned thread blocks.
        return 0;
    }
    if (m == WM_CLOSE) { DestroyWindow(h); return 0; }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int main() {
    fixtureReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    fixtureStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    fixtureBlocked = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    fixturePong = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::thread ui([] {
        WNDCLASSW wc{}; wc.lpfnWndProc = fixtureWndProc;
        wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"YmccOwnedRecoveryFixture";
        assert(RegisterClassW(&wc));
        fixtureWindow = CreateWindowW(wc.lpszClassName, L"", WS_POPUP,
            0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
        assert(fixtureWindow.load()); SetEvent(fixtureReady);
        MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    });
    assert(WaitForSingleObject(fixtureReady, 5000) == WAIT_OBJECT_0);
    Options options; options.pid = GetCurrentProcessId(); options.hwnd = fixtureWindow;
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(RecoveryStateSnapshot), snapshotName(options.pid).c_str());
    auto* view = static_cast<RecoveryStateSnapshot*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS,
        0, 0, sizeof(RecoveryStateSnapshot)));
    assert(mapping && view);
    memset(view, 0, sizeof(*view));
    auto publish = [&] {
        ++view->sequence;
        view->magic = kSnapshotMagic; view->schemaVersion = kSnapshotSchemaVersion;
        view->pid = options.pid; view->processStartKey = processStartKeyOf(GetCurrentProcess());
        view->lastNativeHeartbeat = GetTickCount64();
        view->reserved0 = ymcc::encodeRecoveryUiPulse(fixturePulse.load());
        strcpy_s(view->lifecyclePhase, "idle");
        ++view->sequence;
    };
    SnapshotChannel channel;
    auto read = [&] {
        publish(); SnapshotObservation obs;
        assert(readRecoverySnapshot(channel, GetCurrentProcess(), options.pid, obs) == SnapshotRead::Ok);
        assert(obs.uiPulseKnown && obs.heartbeatFresh); return obs;
    };
    DWORD serial = 1;
    assert(firstProbe(options, fixturePong, serial++));
    assert(webviewResponds(options.hwnd, fixturePong, serial++));
    assert(read().uiPulse != 0);
    puts("PASS real posted UI ping crosses the production snapshot reader");

    ULONGLONG simulatedNow = 100000;
    int kills = 0, spawns = 0;
    RecoveryOps ops;
    ops.now = ops.uiNow = [&] { return simulatedNow; };
    ops.processAlive = [] { return true; };
    ops.terminateTarget = [&] { ++kills; return true; }; // No actual process termination.
    ops.waitTargetExit = [] { return true; };
    ops.ownerProbe = [] { return OwnerProbe{}; };
    ops.spawnTarget = [&] { ++spawns; return true; }; // No actual child process.
    ops.sleep = [&](DWORD ms) { simulatedNow += ms; };
    RecoveryEpisodeState liveEpisode;
    fixtureScriptPong = false;
    for (unsigned i = 0; i < 12; ++i) {
        assert(!webviewResponds(options.hwnd, fixturePong, serial++));
        simulatedNow += 3000;
        assert(recoveryDecisionRound(read(), liveEpisode, ops, L"") == RoundResult::Continue);
    }
    assert(kills == 0 && spawns == 0);
    puts("PASS script pong failures with an advancing UI never kill the fixture");

    assert(PostMessageW(options.hwnd, WM_FIXTURE_BLOCK, 0, 0));
    assert(WaitForSingleObject(fixtureBlocked, 5000) == WAIT_OBJECT_0);
    assert(!firstProbe(options, fixturePong, serial++));
    assert(!confirmationProbe(options, fixturePong, serial));
    const unsigned stalledPulse = fixturePulse.load();
    RecoveryEpisodeState stalledEpisode;
    for (unsigned i = 0; i < 11; ++i) {
        simulatedNow += 3000;
        const auto obs = read();
        assert(obs.uiPulse == stalledPulse && obs.heartbeatFresh && !obs.lifecycleBusy);
        const auto result = recoveryDecisionRound(obs, stalledEpisode, ops, L"");
        assert(result == (i == 10 ? RoundResult::Stop : RoundResult::Continue));
    }
    assert(kills == 1 && spawns == 1);
    puts("PASS blocked UI + advancing publisher invokes exactly one mocked recovery after 30s");

    powerNotifyCallback(nullptr, PBT_APMSUSPEND, nullptr);
    assert(powerDeferKill(L""));
    powerNotifyCallback(nullptr, PBT_APMRESUMEAUTOMATIC, nullptr);
    assert(powerDeferKill(L""));
    puts("PASS production sleep/wake-grace gate blocks termination");
    SetEvent(fixtureStop); PostMessageW(options.hwnd, WM_CLOSE, 0, 0); ui.join();
    snapshotChannelClose(channel); UnmapViewOfFile(view); CloseHandle(mapping);
    for (HANDLE h : {fixtureReady, fixtureStop, fixtureBlocked, fixturePong}) CloseHandle(h);
    puts("owned Win32 UI recovery fixture: PASS (no real app/game/sleep touched)");
}

