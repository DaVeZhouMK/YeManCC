// Deterministic consumer of the production two-snapshot query. No live process,
// thread enumeration, resume or sleep APIs are called by this test.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

namespace ThreadQueryFixture {
struct Ports {
    std::vector<DWORD> first{11, 12}, second{11, 12};
    std::map<DWORD, ULONG> counts;
    unsigned snapshots = 0, queries = 0;
    size_t index = 0;
    DWORD error = ERROR_SUCCESS, deniedThread = 0, failedQuery = 0;
    DWORD shortQuery = 0, wrongOwner = 0;
    unsigned failedSnapshot = 0, failedEnumeration = 0;
    bool available = true;
} ports;
DWORD error() { return ports.error; }
HANDLE snapshot(DWORD flags, DWORD pid) {
    assert(flags == TH32CS_SNAPTHREAD && pid == 0);
    if (++ports.snapshots == ports.failedSnapshot) return INVALID_HANDLE_VALUE;
    return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(ports.snapshots));
}
const std::vector<DWORD>& entries(HANDLE h) {
    return reinterpret_cast<uintptr_t>(h) == 1 ? ports.first : ports.second;
}
BOOL next(HANDLE h, THREADENTRY32* entry) {
    const auto& set = entries(h);
    if (ports.index == set.size()) {
        ports.error = reinterpret_cast<uintptr_t>(h) == ports.failedEnumeration
            ? ERROR_ACCESS_DENIED : ERROR_NO_MORE_FILES;
        return FALSE;
    }
    entry->th32ThreadID = set[ports.index++];
    entry->th32OwnerProcessID = entry->th32ThreadID == 99 ? 200 : 100;
    return TRUE;
}
BOOL first(HANDLE h, THREADENTRY32* entry) {
    assert(entry->dwSize == sizeof(*entry));
    ports.index = 0;
    return next(h, entry);
}
HANDLE open(DWORD access, BOOL inherit, DWORD tid) {
    assert(access == THREAD_QUERY_INFORMATION && !inherit);
    return tid == ports.deniedThread ? nullptr : reinterpret_cast<HANDLE>(static_cast<uintptr_t>(tid));
}
DWORD owner(HANDLE h) {
    return reinterpret_cast<uintptr_t>(h) == ports.wrongOwner ? 200 : 100;
}
BOOL close(HANDLE) { return TRUE; }
LONG NTAPI query(HANDLE h, ULONG type, void* output, ULONG size, ULONG* returned) {
    assert(type == 35 && size == sizeof(ULONG));
    ++ports.queries;
    const auto tid = static_cast<DWORD>(reinterpret_cast<uintptr_t>(h));
    *static_cast<ULONG*>(output) = ports.counts[tid];
    *returned = tid == ports.shortQuery ? 0 : sizeof(ULONG);
    return tid == ports.failedQuery ? -1 : 0;
}
HMODULE module(LPCWSTR) { return reinterpret_cast<HMODULE>(1); }
FARPROC resolve(HMODULE, LPCSTR name) {
    assert(std::strcmp(name, "NtQueryInformationThread") == 0);
    return ports.available ? reinterpret_cast<FARPROC>(query) : nullptr;
}
} // namespace ThreadQueryFixture
#define GetLastError ThreadQueryFixture::error
#define CreateToolhelp32Snapshot ThreadQueryFixture::snapshot
#define Thread32First ThreadQueryFixture::first
#define Thread32Next ThreadQueryFixture::next
#define OpenThread ThreadQueryFixture::open
#define GetProcessIdOfThread ThreadQueryFixture::owner
#define CloseHandle ThreadQueryFixture::close
#define GetModuleHandleW ThreadQueryFixture::module
#define GetProcAddress ThreadQueryFixture::resolve
#include "wake_game_resume.h"

int main(int argc, char** argv) {
    using namespace ThreadQueryFixture;
    if (argc == 2 && std::strcmp(argv[1], "unavailable") == 0) {
        ports.available = false;
        const auto state = WakeGameResume::queryThreads(100);
        assert(!state.known && !state.allRunning() && !state.allSuspended() && ports.snapshots == 0);
        std::cout << "PASS unavailable query fails closed without enumeration\n";
        return 0;
    }
    unsigned passed = 0;
    auto refused = [&] {
        const auto state = WakeGameResume::queryThreads(100);
        assert(!state.known && !state.allRunning() && !state.allSuspended());
        ++passed;
    };
    ports = {}; ports.first.clear();
    for (DWORD tid = 1; tid <= 54; ++tid) ports.first.push_back(tid);
    ports.second = ports.first;
    std::reverse(ports.second.begin(), ports.second.end());
    auto state = WakeGameResume::queryThreads(100);
    assert(state.allRunning() && !state.allSuspended() && state.total == 54 &&
        state.suspended == 0 && ports.snapshots == 2 && ports.queries == 54); ++passed;
    ports = {}; ports.counts[11] = 1;
    state = WakeGameResume::queryThreads(100);
    assert(state.known && state.total == 2 && state.suspended == 1 && !state.allRunning()); ++passed;
    ports = {}; ports.counts[11] = ports.counts[12] = 1;
    assert(WakeGameResume::queryThreads(100).allSuspended()); ++passed;
    ports = {}; ports.second = {11, 13}; refused(); // same size, replaced TID
    ports = {}; ports.second.push_back(13); refused();
    ports = {}; ports.second.pop_back(); refused();
    ports = {}; ports.deniedThread = 12; refused();
    ports = {}; ports.failedQuery = 12; refused();
    ports = {}; ports.shortQuery = 12; refused();
    ports = {}; ports.wrongOwner = 12; refused();
    ports = {}; ports.failedSnapshot = 1; refused();
    ports = {}; ports.failedSnapshot = 2; refused();
    ports = {}; ports.failedEnumeration = 1; refused();
    ports = {}; ports.failedEnumeration = 2; refused();
    ports = {}; ports.first.clear(); ports.second.clear(); refused();
    ports = {}; ports.first.push_back(99); ports.second.push_back(99);
    state = WakeGameResume::queryThreads(100);
    assert(state.allRunning() && state.total == 2 && ports.queries == 2); ++passed;
    std::cout << "wake thread query production-header selftest: " << passed << " groups PASS\n";
}
