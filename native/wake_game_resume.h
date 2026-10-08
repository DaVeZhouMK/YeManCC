#pragma once
// Explicit-wake recovery only. ThreadSuspendCount is queried dynamically;
// unsupported/denied queries fail closed, never inferred from API success.
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <vector>

namespace WakeGameResume {
inline constexpr ULONGLONG attemptOffsetsMs[] = {1000, 2500, 4500, 7000, 10000};
inline constexpr size_t attemptCount = sizeof(attemptOffsetsMs) / sizeof(attemptOffsetsMs[0]);
struct ThreadState {
    bool known = false;
    unsigned total = 0;
    unsigned suspended = 0;
    bool allSuspended() const { return known && total && suspended == total; }
    bool allRunning() const { return known && total && suspended == 0; }
};
struct FallbackGate {
    bool explicitWake = false, sameSession = false, sameUser = false;
    bool systemExcluded = true, userExcluded = true, memoryEligible = false;
    bool debugged = true, identityMatches = false, fullySuspended = false;
};
inline bool allowFallback(const FallbackGate& g) {
    return g.explicitWake && g.sameSession && g.sameUser && !g.systemExcluded &&
        !g.userExcluded && g.memoryEligible && !g.debugged &&
        g.identityMatches && g.fullySuspended;
}
inline bool sameUser(HANDLE process) {
    HANDLE ours = nullptr, theirs = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &ours)) return false;
    if (!OpenProcessToken(process, TOKEN_QUERY, &theirs)) { CloseHandle(ours); return false; }
    const auto user = [](HANDLE token, std::vector<BYTE>& data) {
        DWORD bytes = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        if (!bytes) return false;
        data.resize(bytes);
        return GetTokenInformation(token, TokenUser, data.data(), bytes, &bytes) != FALSE;
    };
    std::vector<BYTE> a, b;
    const bool equal = user(ours, a) && user(theirs, b) &&
        EqualSid(reinterpret_cast<TOKEN_USER*>(a.data())->User.Sid,
                 reinterpret_cast<TOKEN_USER*>(b.data())->User.Sid);
    CloseHandle(ours); CloseHandle(theirs);
    return equal;
}
inline ThreadState queryThreads(DWORD pid) {
    using Query = LONG (NTAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);
    static const auto query = reinterpret_cast<Query>(GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    ThreadState result;
    if (!query) return result;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;
    THREADENTRY32 thread{}; thread.dwSize = sizeof(thread);
    bool complete = true;
    std::vector<DWORD> first;
    if (Thread32First(snap, &thread)) {
        do {
            if (thread.th32OwnerProcessID != pid) continue;
            first.push_back(thread.th32ThreadID);
            HANDLE h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, thread.th32ThreadID);
            ULONG count = 0, returned = 0;
            // phnt THREADINFOCLASS::ThreadSuspendCount (35, ULONG, since WINBLUE).
            if (!h || query(h, 35, &count, sizeof(count), &returned) < 0 ||
                returned != sizeof(count) || GetProcessIdOfThread(h) != pid) complete = false;
            else { ++result.total; if (count) ++result.suspended; }
            if (h) CloseHandle(h);
        } while (Thread32Next(snap, &thread));
        if (GetLastError() != ERROR_NO_MORE_FILES) complete = false;
    } else complete = false;
    CloseHandle(snap);
    // Reject a changing thread set rather than claiming a partial snapshot
    // proves that the entire process has resumed/is suspended.
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;
    std::vector<DWORD> second;
    thread.dwSize = sizeof(thread);
    if (Thread32First(snap, &thread)) {
        do { if (thread.th32OwnerProcessID == pid) second.push_back(thread.th32ThreadID); }
        while (Thread32Next(snap, &thread));
        if (GetLastError() != ERROR_NO_MORE_FILES) complete = false;
    } else complete = false;
    CloseHandle(snap);
    std::sort(first.begin(), first.end()); std::sort(second.begin(), second.end());
    result.known = complete && !first.empty() && first == second && result.total == first.size();
    return result;
}
} // namespace WakeGameResume
