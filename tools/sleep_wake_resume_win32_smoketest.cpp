// Safe OS smoke test: suspends ONLY a child created by this executable.
// The second child simulates an independent input writer. No games, drivers,
// InputHost instances, or existing user processes are enumerated or modified.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include "wake_game_resume.h"

using NtProcessFn = LONG (NTAPI*)(HANDLE);
struct Counters { volatile LONG value[2]; };
struct Child {
    PROCESS_INFORMATION pi{};
    NtProcessFn resume{};
    ~Child() {
        if (pi.hProcess) {
            // Recover before stopping only this test-owned helper.
            if (resume) resume(pi.hProcess);
            if (WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT)
                TerminateProcess(pi.hProcess, 0);
            WaitForSingleObject(pi.hProcess, 3000);
            CloseHandle(pi.hProcess);
        }
        if (pi.hThread) CloseHandle(pi.hThread);
    }
};
LONG sample(Counters* c, int i) { return InterlockedCompareExchange(&c->value[i], 0, 0); }
int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring(argv[1]) == L"--worker") {
        HANDLE map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, argv[2]);
        if (!map) return 2;
        auto* c = static_cast<Counters*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Counters)));
        if (!c) { CloseHandle(map); return 3; }
        const int i = std::wstring(argv[3]) == L"0" ? 0 : 1;
        const ULONGLONG deadline = GetTickCount64() + 15000;
        while (GetTickCount64() < deadline) { InterlockedIncrement(&c->value[i]); Sleep(5); }
        UnmapViewOfFile(c); CloseHandle(map); return 0;
    }
    auto dll = GetModuleHandleW(L"ntdll.dll");
    auto suspend = reinterpret_cast<NtProcessFn>(GetProcAddress(dll, "NtSuspendProcess"));
    auto resume = reinterpret_cast<NtProcessFn>(GetProcAddress(dll, "NtResumeProcess"));
    if (!suspend || !resume) return 4;
    const std::wstring name = L"Local\\YMCC_SleepPause_Smoke_" + std::to_wstring(GetCurrentProcessId());
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Counters), name.c_str());
    if (!map || GetLastError() == ERROR_ALREADY_EXISTS) return 5;
    auto* c = static_cast<Counters*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Counters)));
    if (!c) { CloseHandle(map); return 6; }
    c->value[0] = c->value[1] = 0;
    int result = 0;
    {
        Child children[2];
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        for (int i = 0; i < 2; ++i) {
            children[i].resume = resume;
            std::wstring command = L"\"" + std::wstring(executable) + L"\" --worker \"" + name + L"\" " + std::to_wstring(i);
            STARTUPINFOW si{}; si.cb = sizeof(si);
            if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
                                CREATE_NO_WINDOW, nullptr, nullptr, &si, &children[i].pi)) { result=7; break; }
        }
        if (result == 0) {
            auto start = GetTickCount64();
            while ((sample(c,0) < 10 || sample(c,1) < 10) && GetTickCount64()-start < 5000) Sleep(10);
            if (sample(c,0) < 10 || sample(c,1) < 10) result=8;
        }
        if (result == 0) {
            const bool sameOwner = WakeGameResume::sameUser(children[0].pi.hProcess);
            const auto running = WakeGameResume::queryThreads(children[0].pi.dwProcessId);
            printf("probe initial known=%d threads=%u suspended=%u sameUser=%d\n",running.known,running.total,running.suspended,sameOwner);
            if (!running.allRunning() || !sameOwner) return 20;
            const LONG status = suspend(children[0].pi.hProcess);
            if (status < 0) result=9;
            else {
                Sleep(30);
                const auto held = WakeGameResume::queryThreads(children[0].pi.dwProcessId);
                printf("probe suspended known=%d threads=%u suspended=%u\n",held.known,held.total,held.suspended);
                if (!held.allSuspended()) return 21;
                const LONG doubleStatus = suspend(children[0].pi.hProcess);
                if (doubleStatus < 0) return 22;
                LONG paused=sample(c,0), other=sample(c,1);
                Sleep(250);
                bool targetStopped=sample(c,0)==paused;
                bool unrelatedContinued=sample(c,1)>other;
                LONG resumeStatus=resume(children[0].pi.hProcess);
                const auto stillHeld=WakeGameResume::queryThreads(children[0].pi.dwProcessId);
                if (!stillHeld.allSuspended()) return 23;
                for (int i=0;i<8;++i) resume(children[0].pi.hProcess);
                Sleep(150);
                const auto released=WakeGameResume::queryThreads(children[0].pi.dwProcessId);
                printf("probe restored known=%d threads=%u suspended=%u\n",released.known,released.total,released.suspended);
                if (!released.allRunning()) return 24;
                bool targetRecovered=sample(c,0)>paused;
                std::printf("target_pid=%lu independent_writer_pid=%lu\n", children[0].pi.dwProcessId, children[1].pi.dwProcessId);
                std::printf("single_pid_stopped=%d independent_writer_continued=%d target_resumed=%d\n", targetStopped, unrelatedContinued, targetRecovered);
                if (!targetStopped || !unrelatedContinued || resumeStatus<0 || !targetRecovered) result=10;
            }
        }
    } // Resume and terminate ONLY the children created above, on every exit path.
    UnmapViewOfFile(c); CloseHandle(map);
    std::printf("sleep PID isolation smoke: %s\n", result==0?"PASS":"FAIL");
    return result;
}
