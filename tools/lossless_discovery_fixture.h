#pragma once
// Optional leaf of controller.lossless-toggle-owner. Never model discoverRuntime.
// SDK/scale declarations are parsed before the discovery-only API substitutions.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <shlobj.h>
#include <wrl/implements.h>
#include "lossless_scale.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace ymcc::losslessDiscoveryFixture {
struct Assertion final : std::runtime_error {
    std::string tag;
    Assertion(const char* label, const std::string& detail) : std::runtime_error(detail), tag(label) {}
};
inline unsigned checks = 0;
inline const char* currentCase = "initialization";
inline void require(bool condition, const char* tag) {
    ++checks;
    if (!condition) throw Assertion(tag, std::string(currentCase) + ": " + tag);
}
struct Process {
    DWORD pid = 71, session = 7;
    std::wstring name = L"LosslessScaling.exe";
    std::wstring path = L"C:\\Fixture\\LosslessScaling.exe";
    std::uint64_t birth = 0x0123456789abcdefULL;
    bool sessionOk = true, openOk = true, pathOk = true, birthOk = true;
    bool terminateName = true; // UNICODE_STRING counts bytes; CRT path must not require NUL.
    std::vector<DWORD> waits{WAIT_TIMEOUT};
};
struct Scenario {
    const char* name;
    std::vector<Process> processes;
    bool selfSessionOk = true, snapshotOk = true;
    DWORD enumerationError = ERROR_NO_MORE_FILES;
    bool firstFails = false;
};
enum class QueryMode {
    Complete, Unavailable, NotImplemented, NotSupported, Failed,
    BadOffset, BadLength, BadPointer, CrossRowPointer, BadCapacity, BadPid, Truncated, Oversized,
    GrowOnce, RetryExhausted, OverBudget, ZeroReturned
};
struct Metrics {
    unsigned queries = 0, snapshots = 0, first = 0, next = 0;
    unsigned sessions = 0, self = 0, opens = 0, waits = 0, paths = 0, births = 0;
    unsigned acquired = 0, closed = 0, forbidden = 0, statusConversions = 0;
    std::size_t basicRows = 0, basicBytes = 0, toolhelpRows = 0;
    std::vector<DWORD> openedPids;
};
struct Handle {
    enum Kind { Snapshot, ProcessHandle } kind;
    DWORD pid = 0;
    std::size_t cursor = 0;
};
struct State {
    Scenario scenario{"unset", {}};
    QueryMode mode = QueryMode::Complete;
    Metrics metrics;
    DWORD lastError = ERROR_SUCCESS;
    std::uintptr_t nextHandle = 0x10000;
    std::map<std::uintptr_t, Handle> handles;
    std::map<DWORD, std::size_t> waitIndices;
};
inline State& state() { static State value; return value; }
inline DWORD& lastError() { static thread_local DWORD value=ERROR_SUCCESS; return value; }
inline ULONGLONG clock=1000;
inline constexpr DWORD selfPid = 0xf0000001u;
inline constexpr std::uintptr_t moduleToken = 0x70000;
[[noreturn]] inline void forbidden(const char* name) {
    ++state().metrics.forbidden;
    throw Assertion("FORBIDDEN_API", name);
}
// A healthy process inventory always contains at least the fake test process.
// No-LS means no matching row, rather than a zero-byte or synthetic PID-zero result.
inline std::size_t enumerationRowCount() { return state().scenario.processes.size() + 1; }
inline const Process& enumerationRow(std::size_t index) {
    static const Process self = [] {
        Process value; value.pid = selfPid; value.name = L"YMCCDiscoveryFixture.exe";
        value.path = L"C:\\Fixture\\YMCCDiscoveryFixture.exe"; return value;
    }();
    if (!index) return self;
    if (index > state().scenario.processes.size()) forbidden("fake inventory row out of bounds");
    return state().scenario.processes[index - 1];
}
inline Process& process(DWORD pid) {
    for (auto& item : state().scenario.processes) if (item.pid == pid) return item;
    forbidden("unknown fake PID");
}
inline HANDLE acquire(Handle::Kind kind, DWORD pid = 0) {
    auto& s = state(); const auto token = ++s.nextHandle;
    s.handles.emplace(token, Handle{kind, pid, 0}); ++s.metrics.acquired;
    return reinterpret_cast<HANDLE>(token);
}
inline Handle& handle(HANDLE value, Handle::Kind kind) {
    auto& s = state(); const auto found = s.handles.find(reinterpret_cast<std::uintptr_t>(value));
    if (found == s.handles.end() || found->second.kind != kind) forbidden("unknown/wrong-kind fake handle");
    return found->second;
}
inline DWORD WINAPI fakeGetCurrentProcessId() { ++state().metrics.self; return selfPid; }
inline BOOL WINAPI fakeProcessIdToSessionId(DWORD pid, DWORD* session) {
    auto& s = state(); ++s.metrics.sessions;
    if (!session) forbidden("null session output");
    if (pid == selfPid) {
        if (!s.scenario.selfSessionOk) { lastError() = ERROR_ACCESS_DENIED; return FALSE; }
        *session = 7; return TRUE;
    }
    auto& item = process(pid);
    if (!item.sessionOk) { lastError() = ERROR_ACCESS_DENIED; return FALSE; }
    *session = item.session; return TRUE;
}
inline HANDLE WINAPI fakeOpenProcess(DWORD rights, BOOL inherit, DWORD pid) {
    auto& s = state(); ++s.metrics.opens; s.metrics.openedPids.push_back(pid);
    if (rights != (PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE) || inherit != FALSE)
        forbidden("unexpected OpenProcess rights/inherit");
    auto& item = process(pid);
    if (!item.openOk) { lastError() = ERROR_ACCESS_DENIED; return nullptr; }
    return acquire(Handle::ProcessHandle, pid);
}
inline DWORD WINAPI fakeGetProcessId(HANDLE value) { return handle(value, Handle::ProcessHandle).pid; }
inline DWORD WINAPI fakeWaitForSingleObject(HANDLE value, DWORD milliseconds) {
    auto& s = state(); ++s.metrics.waits;
    if (milliseconds != 0) forbidden("blocking wait in discovery");
    const auto pid = handle(value, Handle::ProcessHandle).pid;
    auto& item = process(pid); if (item.waits.empty()) forbidden("missing fake liveness sequence");
    auto& index = s.waitIndices[pid];
    return item.waits[(std::min)(index++, item.waits.size() - 1)];
}
inline BOOL WINAPI fakeQueryFullProcessImageNameW(HANDLE value, DWORD flags, LPWSTR image, PDWORD count) {
    auto& s = state(); ++s.metrics.paths;
    auto& item = process(handle(value, Handle::ProcessHandle).pid);
    if (flags || !image || !count) forbidden("unexpected QueryFullProcessImageName arguments");
    if (!item.pathOk) { lastError() = ERROR_ACCESS_DENIED; return FALSE; }
    if (*count <= item.path.size()) { lastError() = ERROR_INSUFFICIENT_BUFFER; return FALSE; }
    std::wmemcpy(image, item.path.c_str(), item.path.size() + 1);
    *count = static_cast<DWORD>(item.path.size()); return TRUE;
}
inline BOOL WINAPI fakeGetProcessTimes(HANDLE value, LPFILETIME created, LPFILETIME exited,
    LPFILETIME kernel, LPFILETIME user) {
    auto& s = state(); ++s.metrics.births;
    auto& item = process(handle(value, Handle::ProcessHandle).pid);
    if (!created || !exited || !kernel || !user) forbidden("null FILETIME output");
    if (!item.birthOk) { lastError() = ERROR_ACCESS_DENIED; return FALSE; }
    *created = FILETIME{static_cast<DWORD>(item.birth), static_cast<DWORD>(item.birth >> 32)};
    *exited = *kernel = *user = FILETIME{}; return TRUE;
}
inline BOOL WINAPI fakeCloseHandle(HANDLE value) {
    auto& s = state();
    const auto found = s.handles.find(reinterpret_cast<std::uintptr_t>(value));
    if (found == s.handles.end()) forbidden("close unknown/already closed fake handle");
    s.handles.erase(found); ++s.metrics.closed; return TRUE;
}
inline DWORD WINAPI fakeGetLastError() { return lastError(); }
inline void WINAPI fakeSetLastError(DWORD value) { lastError() = value; }
inline HANDLE WINAPI fakeCreateToolhelp32Snapshot(DWORD flags, DWORD pid) {
    auto& s = state(); ++s.metrics.snapshots;
    if (flags != TH32CS_SNAPPROCESS || pid != 0) forbidden("unexpected Toolhelp flags/PID");
    if (!s.scenario.snapshotOk) { lastError() = ERROR_ACCESS_DENIED; return INVALID_HANDLE_VALUE; }
    return acquire(Handle::Snapshot);
}
inline BOOL fillEntry(Handle& h, LPPROCESSENTRY32W entry) {
    auto& s = state();
    if (!entry || entry->dwSize != sizeof(PROCESSENTRY32W)) forbidden("bad PROCESSENTRY32W size");
    if (h.cursor >= enumerationRowCount()) { lastError() = s.scenario.enumerationError; return FALSE; }
    const auto& item = enumerationRow(h.cursor);
    ++s.metrics.toolhelpRows;
    if (item.name.size() >= MAX_PATH) forbidden("fixture name exceeds Toolhelp capacity");
    entry->th32ProcessID = item.pid;
    std::wmemcpy(entry->szExeFile, item.name.c_str(), item.name.size() + 1);
    return TRUE;
}
inline BOOL WINAPI fakeProcess32FirstW(HANDLE value, LPPROCESSENTRY32W entry) {
    auto& s = state(); ++s.metrics.first;
    auto& h = handle(value, Handle::Snapshot); h.cursor = 0;
    if (s.scenario.firstFails) { lastError() = s.scenario.enumerationError; return FALSE; }
    return fillEntry(h, entry);
}
inline BOOL WINAPI fakeProcess32NextW(HANDLE value, LPPROCESSENTRY32W entry) {
    ++state().metrics.next; auto& h = handle(value, Handle::Snapshot); ++h.cursor;
    return fillEntry(h, entry);
}
inline std::size_t alignRow(std::size_t bytes) {
    constexpr auto alignment = alignof(SYSTEM_BASICPROCESS_INFORMATION);
    return (bytes + alignment - 1) / alignment * alignment;
}
inline std::size_t wireExtent(const Process& p) {
    const auto nameBytes = p.name.size() * sizeof(wchar_t);
    // A nonzero poison wchar follows a non-NUL name, inside the row but outside
    // MaximumLength. An accidental unbounded CRT comparison cannot silently pass.
    return alignRow(sizeof(SYSTEM_BASICPROCESS_INFORMATION) + nameBytes + sizeof(wchar_t));
}
// Encode only OS input records into the caller's actual scratch buffer.
// Every ImageName.Buffer points after its own row struct and stays in its extent.
// The product parser and discovery algorithm stay intact.
inline NTSTATUS NTAPI fakeNtQuerySystemInformation(SYSTEM_INFORMATION_CLASS info, PVOID data,
    ULONG capacity, PULONG returned) {
    auto& s = state(); ++s.metrics.queries;
    if (info != SystemBasicProcessInformation || !data || !returned)
        forbidden("unexpected basic-query arguments/class");
    switch (s.mode) {
    case QueryMode::Unavailable: return static_cast<NTSTATUS>(0xc0000003u);
    case QueryMode::NotImplemented: return static_cast<NTSTATUS>(0xc0000002u);
    case QueryMode::NotSupported: return static_cast<NTSTATUS>(0xc00000bbu);
    case QueryMode::Failed: return static_cast<NTSTATUS>(0xc0000022u);
    case QueryMode::ZeroReturned: *returned = 0; return 0; // malformed success, NOT healthy no-LS
    case QueryMode::RetryExhausted: *returned = capacity; return static_cast<NTSTATUS>(0xc0000004u);
    case QueryMode::OverBudget: *returned = 17u * 1024u * 1024u; return static_cast<NTSTATUS>(0xc0000023u);
    case QueryMode::GrowOnce:
        if (s.metrics.queries == 1) { *returned = capacity + 1024; return static_cast<NTSTATUS>(0xc0000004u); }
        break;
    default: break;
    }
    const auto rowCount = enumerationRowCount();
    std::size_t required = 0;
    for (std::size_t i = 0; i < rowCount; ++i) required += wireExtent(enumerationRow(i));
    if (required > capacity) { *returned = static_cast<ULONG>(required); return static_cast<NTSTATUS>(0xc0000004u); }
    auto* bytes = static_cast<unsigned char*>(data); std::memset(bytes, 0xa5, required);
    s.metrics.basicRows = rowCount; s.metrics.basicBytes = required;
    std::size_t offset = 0;
    for (std::size_t i = 0; i < rowCount; ++i) {
        const auto& p = enumerationRow(i); const auto extent = wireExtent(p);
        const auto nameBytes = p.name.size() * sizeof(wchar_t);
        const auto nameCapacity = nameBytes + (p.terminateName ? sizeof(wchar_t) : 0);
        if (nameCapacity > (std::numeric_limits<USHORT>::max)()) forbidden("fake image name exceeds UNICODE_STRING");
        SYSTEM_BASICPROCESS_INFORMATION row{};
        row.NextEntryOffset = i + 1 < rowCount ? static_cast<ULONG>(extent) : 0;
        row.UniqueProcessId = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(p.pid));
        row.ImageName.Buffer = reinterpret_cast<PWSTR>(bytes + offset + sizeof(row));
        row.ImageName.Length = static_cast<USHORT>(nameBytes);
        row.ImageName.MaximumLength = static_cast<USHORT>(nameCapacity);
        std::memcpy(bytes + offset, &row, sizeof(row));
        std::memcpy(bytes + offset + sizeof(row), p.name.data(), nameBytes);
        if (p.terminateName) {
            const wchar_t nul = 0;
            std::memcpy(bytes + offset + sizeof(row) + nameBytes, &nul, sizeof(nul));
        }
        offset += extent;
    }
    SYSTEM_BASICPROCESS_INFORMATION row{}; std::memcpy(&row, bytes, sizeof(row));
    switch (s.mode) {
    case QueryMode::BadOffset: row.NextEntryOffset = 1; break;
    case QueryMode::BadLength: row.ImageName.Length = 3; break;
    case QueryMode::BadPointer: row.ImageName.Buffer = reinterpret_cast<PWSTR>(reinterpret_cast<std::uintptr_t>(bytes) + capacity + 2); break;
    case QueryMode::CrossRowPointer:
        row.ImageName.Buffer = reinterpret_cast<PWSTR>(bytes + wireExtent(enumerationRow(0)) + sizeof(row)); break;
    case QueryMode::BadCapacity:
        row.ImageName.MaximumLength = static_cast<USHORT>(wireExtent(enumerationRow(0)) - sizeof(row) + sizeof(wchar_t)); break;
    case QueryMode::BadPid: row.UniqueProcessId = reinterpret_cast<HANDLE>(std::uintptr_t{1} << 32); break;
    default: break;
    }
    std::memcpy(bytes, &row, sizeof(row));
    *returned = s.mode == QueryMode::Truncated ? sizeof(row) - 1
        : s.mode == QueryMode::Oversized ? capacity + 1 : static_cast<ULONG>(required);
    return 0;
}
inline ULONG NTAPI fakeRtlNtStatusToDosError(NTSTATUS status) {
    ++state().metrics.statusConversions;
    if (status != static_cast<NTSTATUS>(0xc0000022u)) forbidden("unexpected NTSTATUS conversion");
    return ERROR_ACCESS_DENIED;
}
inline HMODULE WINAPI fakeGetModuleHandleW(LPCWSTR module) {
    if (!module || std::wcscmp(module, L"ntdll.dll")) forbidden("unexpected module resolution");
    return reinterpret_cast<HMODULE>(moduleToken);
}
inline FARPROC WINAPI fakeGetProcAddress(HMODULE module, LPCSTR symbol) {
    if (module != reinterpret_cast<HMODULE>(moduleToken) || !symbol) forbidden("unexpected procedure module");
    if (std::strcmp(symbol, "NtQuerySystemInformation") == 0)
        return reinterpret_cast<FARPROC>(&fakeNtQuerySystemInformation);
    if (std::strcmp(symbol, "RtlNtStatusToDosError") == 0)
        return reinterpret_cast<FARPROC>(&fakeRtlNtStatusToDosError);
    forbidden("unexpected procedure resolution");
}
inline int WINAPI forbiddenCompareStringOrdinal(LPCWCH, int, LPCWCH, int, BOOL) { forbidden("ordinal matching in CrtIgnoreCase Lossless"); }
inline ULONGLONG WINAPI fakeGetTickCount64() { return clock; }
inline HANDLE WINAPI forbiddenCreateEventW(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { forbidden("RuntimeMonitor event creation"); }
inline BOOL WINAPI forbiddenSetEvent(HANDLE) { forbidden("RuntimeMonitor event signal"); }
inline HRESULT WINAPI forbiddenSHGetKnownFolderPath(REFKNOWNFOLDERID, DWORD, HANDLE, PWSTR*) { forbidden("known-folder probe"); }
inline void WINAPI forbiddenCoTaskMemFree(LPVOID) { forbidden("COM memory release"); }
inline HANDLE WINAPI forbiddenFindFirstChangeNotificationW(LPCWSTR, BOOL, DWORD) { forbidden("filesystem watcher"); }
inline BOOL WINAPI forbiddenFindNextChangeNotification(HANDLE) { forbidden("filesystem watcher reset"); }
inline BOOL WINAPI forbiddenFindCloseChangeNotification(HANDLE) { forbidden("filesystem watcher close"); }
inline DWORD WINAPI forbiddenWaitForMultipleObjects(DWORD, const HANDLE*, BOOL, DWORD) { forbidden("monitor multiwait"); }
inline HWND forbiddenScaleMainWindow(DWORD) { forbidden("UIA window discovery"); }
inline ymcc::lossless::ScaleSnapshot forbiddenObserveScaling(const ymcc::lossless::Identity&, bool&) { forbidden("UIA scaling observation"); }
} // namespace ymcc::losslessDiscoveryFixture

#define GetProcessId ::ymcc::losslessDiscoveryFixture::fakeGetProcessId
#define GetCurrentProcessId ::ymcc::losslessDiscoveryFixture::fakeGetCurrentProcessId
#define ProcessIdToSessionId ymcc::losslessDiscoveryFixture::fakeProcessIdToSessionId
#define OpenProcess ::ymcc::losslessDiscoveryFixture::fakeOpenProcess
#define WaitForSingleObject ::ymcc::losslessDiscoveryFixture::fakeWaitForSingleObject
#define QueryFullProcessImageNameW ::ymcc::losslessDiscoveryFixture::fakeQueryFullProcessImageNameW
#define GetProcessTimes ::ymcc::losslessDiscoveryFixture::fakeGetProcessTimes
#define CloseHandle ::ymcc::losslessDiscoveryFixture::fakeCloseHandle
#define GetLastError ::ymcc::losslessDiscoveryFixture::fakeGetLastError
#define SetLastError ::ymcc::losslessDiscoveryFixture::fakeSetLastError
#define CreateToolhelp32Snapshot ::ymcc::losslessDiscoveryFixture::fakeCreateToolhelp32Snapshot
#define Process32FirstW ::ymcc::losslessDiscoveryFixture::fakeProcess32FirstW
#define Process32NextW ::ymcc::losslessDiscoveryFixture::fakeProcess32NextW
#define GetModuleHandleW ::ymcc::losslessDiscoveryFixture::fakeGetModuleHandleW
#define GetProcAddress ::ymcc::losslessDiscoveryFixture::fakeGetProcAddress
#define CompareStringOrdinal ::ymcc::losslessDiscoveryFixture::forbiddenCompareStringOrdinal
#define GetTickCount64 ::ymcc::losslessDiscoveryFixture::fakeGetTickCount64
#define CreateEventW ::ymcc::losslessDiscoveryFixture::forbiddenCreateEventW
#define SetEvent ::ymcc::losslessDiscoveryFixture::forbiddenSetEvent
#define SHGetKnownFolderPath ::ymcc::losslessDiscoveryFixture::forbiddenSHGetKnownFolderPath
#define CoTaskMemFree ::ymcc::losslessDiscoveryFixture::forbiddenCoTaskMemFree
#define FindFirstChangeNotificationW ::ymcc::losslessDiscoveryFixture::forbiddenFindFirstChangeNotificationW
#define FindNextChangeNotification ::ymcc::losslessDiscoveryFixture::forbiddenFindNextChangeNotification
#define FindCloseChangeNotification ::ymcc::losslessDiscoveryFixture::forbiddenFindCloseChangeNotification
#define WaitForMultipleObjects ::ymcc::losslessDiscoveryFixture::forbiddenWaitForMultipleObjects
#define scaleMainWindow ::ymcc::losslessDiscoveryFixture::forbiddenScaleMainWindow
#define observeScaling ::ymcc::losslessDiscoveryFixture::forbiddenObserveScaling
// Include resolution MUST be candidate directory first, followed by shared native.
#include "lossless_runtime.h"
#include "game_process_inventory.h"
#undef GetProcessId
#undef GetCurrentProcessId
#undef ProcessIdToSessionId
#undef OpenProcess
#undef WaitForSingleObject
#undef QueryFullProcessImageNameW
#undef GetProcessTimes
#undef CloseHandle
#undef GetLastError
#undef SetLastError
#undef CreateToolhelp32Snapshot
#undef Process32FirstW
#undef Process32NextW
#undef GetModuleHandleW
#undef GetProcAddress
#undef CompareStringOrdinal
#undef GetTickCount64
#undef CreateEventW
#undef SetEvent
#undef SHGetKnownFolderPath
#undef CoTaskMemFree
#undef FindFirstChangeNotificationW
#undef FindNextChangeNotification
#undef FindCloseChangeNotification
#undef WaitForMultipleObjects
#undef scaleMainWindow
#undef observeScaling

namespace ymcc::losslessDiscoveryFixture {
using ymcc::lossless::RuntimeReceipt;
struct Observation { RuntimeReceipt receipt; Metrics metrics; };
inline std::string quote(const std::string& text) {
    std::ostringstream out; out << '"';
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32 || c >= 127) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else out << static_cast<char>(c);
    }
    out << '"'; return out.str();
}
inline std::string quote(const std::wstring& text) {
    std::ostringstream out; out << '"';
    for (const wchar_t c : text) {
        if (c == L'"' || c == L'\\') out << '\\' << static_cast<char>(c);
        else if (c < 32 || c >= 127) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(static_cast<unsigned short>(c));
        else out << static_cast<char>(c);
    }
    out << '"'; return out.str();
}
inline std::string receiptJson(const RuntimeReceipt& r) {
    std::ostringstream out;
    out << "{\"known\":" << (r.known ? "true" : "false") << ",\"running\":" << (r.running ? "true" : "false")
        << ",\"readable\":" << (r.readable ? "true" : "false") << ",\"pid\":" << r.identity.pid
        << ",\"created\":" << quote(std::to_string(r.identity.created)) << ",\"path\":" << quote(r.identity.path)
        << ",\"button\":" << quote(r.button) << ",\"status\":" << quote(r.status) << ",\"reason\":" << quote(r.reason)
        << ",\"configRevision\":" << r.configRevision << ",\"observedAt\":" << r.observedAt << '}';
    return out.str();
}
inline std::string metricsJson(const Metrics& m) {
    std::ostringstream out;
    out << "{\"queries\":" << m.queries << ",\"toolhelp\":" << m.snapshots << ",\"first\":" << m.first << ",\"next\":" << m.next
        << ",\"sessions\":" << m.sessions << ",\"self\":" << m.self << ",\"opens\":" << m.opens << ",\"waits\":" << m.waits
        << ",\"paths\":" << m.paths << ",\"births\":" << m.births << ",\"acquired\":" << m.acquired << ",\"closed\":" << m.closed
        << ",\"forbidden\":" << m.forbidden << ",\"statusConversions\":" << m.statusConversions
        << ",\"basicRows\":" << m.basicRows << ",\"basicBytes\":" << m.basicBytes << ",\"toolhelpRows\":" << m.toolhelpRows << ",\"openedPids\":[";
    for (std::size_t i = 0; i < m.openedPids.size(); ++i) { if (i) out << ','; out << m.openedPids[i]; }
    out << "]}"; return out.str();
}
inline Observation run(const Scenario& scenario, QueryMode mode, unsigned snapshots) {
    currentCase = scenario.name;
    require(state().handles.empty(), "PREVIOUS_FAKE_HANDLES_EMPTY");
    ymcc::processInventory::invalidate(); lastError()=ERROR_SUCCESS; clock=1000;
    state() = State{}; state().scenario = scenario; state().mode = mode;
    const auto receipt = ymcc::lossless::discoverRuntime(); // actual public product code, no model
    require(state().metrics.forbidden == 0, "NO_FORBIDDEN_API");
    require(state().metrics.snapshots == snapshots, snapshots ? "FALLBACK_TOOLHELP_ONCE" : "NO_TOOLHELP_FAST_PATH");
    require(state().handles.empty() && state().metrics.acquired == state().metrics.closed, "ALL_FAKE_HANDLES_CLOSED");
    require(!receipt.readable && receipt.button.empty() && receipt.status.empty() && !receipt.configRevision && !receipt.observedAt,
        "DISCOVERY_HAS_NO_UIA_OR_CLOCK_SIDE_EFFECTS");
    return {receipt, state().metrics};
}
inline void expect(const Observation& actual, bool known, bool running, const char* reason,
    DWORD pid = 0, std::uint64_t created = 0, const std::wstring& path = {}) {
    const auto& r = actual.receipt;
    require(r.known == known && r.running == running && r.reason == reason && r.identity.pid == pid
        && r.identity.created == created && r.identity.path == path, "EXPECTED_DISCOVERY_RECEIPT");
}
inline std::vector<std::string> rows;
inline unsigned comparisons = 0, executions = 0;
inline void record(const Scenario& scenario, const char* lane, const Observation& observed) {
    ++executions;
    rows.push_back("{\"case\":" + quote(scenario.name) + ",\"lane\":" + quote(std::string(lane))
        + ",\"receipt\":" + receiptJson(observed.receipt) + ",\"metrics\":" + metricsJson(observed.metrics) + '}');
}
inline void compare(const Scenario& scenario, bool known, bool running, const char* reason,
    DWORD pid = 0, std::uint64_t created = 0, const std::wstring& path = {}) {
    const auto basic = run(scenario, QueryMode::Complete, 0);
    expect(basic, known, running, reason, pid, created, path);
    record(scenario, "basic-complete", basic);
    const auto fallback = run(scenario, QueryMode::Unavailable, scenario.selfSessionOk ? 1 : 0);
    expect(fallback, known, running, reason, pid, created, path);
    require(receiptJson(basic.receipt) == receiptJson(fallback.receipt), "BASIC_VS_ACTUAL_PRODUCT_FALLBACK_RECEIPT");
    require(basic.metrics.openedPids == fallback.metrics.openedPids && basic.metrics.opens == fallback.metrics.opens
        && basic.metrics.paths == fallback.metrics.paths && basic.metrics.births == fallback.metrics.births
        && basic.metrics.waits == fallback.metrics.waits && basic.metrics.sessions == fallback.metrics.sessions,
        "BASIC_VS_FALLBACK_IDENTITY_POLICY_CALLS");
    if (scenario.selfSessionOk) {
        require(basic.metrics.queries == 1 && fallback.metrics.queries == 1, "ONE_FRESH_QUERY_PER_DISCOVERY");
        require(basic.metrics.basicRows == scenario.processes.size() + 1
            && fallback.metrics.toolhelpRows == scenario.processes.size() + 1
            && basic.metrics.basicBytes >= sizeof(SYSTEM_BASICPROCESS_INFORMATION), "HEALTHY_INVENTORY_RETAINS_SELF_ROW");
    }
    else require(!basic.metrics.queries && !fallback.metrics.queries, "SELF_SESSION_FAILURE_PRECEDES_ENUMERATION");
    ++comparisons; record(scenario, "actual-product-toolhelp", fallback);
}
inline Process item(DWORD pid = 71, const std::wstring& name = L"LosslessScaling.exe") {
    Process p; p.pid = pid; p.name = name; return p;
}
inline void normalGroups() {
    const auto p = item();
    compare({"empty", {}}, true, false, "");
    compare({"single", {p}}, true, true, "", p.pid, p.birth, p.path);
    auto external = p; external.path = L"E:\\Portable\\LosslessScaling.exe";
    compare({"external-path", {external}}, true, true, "", external.pid, external.birth, external.path);
    auto second = item(72); second.path = L"F:\\Other\\LosslessScaling.exe"; second.birth += 17;
    compare({"multiple", {p, second}}, false, false, "multiple-lossless-instances");
    auto foreign = p; foreign.session = 9;
    compare({"foreign-session", {foreign}}, true, false, "");
    compare({"foreign-plus-local", {foreign, second}}, true, true, "", second.pid, second.birth, second.path);
    auto sessionError = p; sessionError.sessionOk = false;
    compare({"candidate-session-error", {sessionError}}, false, false, "runtime-identity-unavailable");
    compare({"self-session-error", {p}, false}, false, false, "unobserved");
    auto denied = p; denied.openOk = false;
    compare({"open-denied", {denied}}, false, false, "runtime-identity-unavailable");
    auto exited = p; exited.waits = {WAIT_OBJECT_0};
    compare({"exited-before-identity", {exited}}, true, false, "");
    auto pathError = p; pathError.pathOk = false;
    compare({"query-path-error-live", {pathError}}, false, false, "runtime-identity-unavailable");
    auto birthError = p; birthError.birthOk = false;
    compare({"birth-error-live", {birthError}}, false, false, "runtime-identity-unavailable");
    pathError.waits = {WAIT_TIMEOUT, WAIT_OBJECT_0};
    compare({"exits-after-path-failure", {pathError}}, true, false, "");
    birthError.waits = {WAIT_TIMEOUT, WAIT_OBJECT_0};
    compare({"exits-after-birth-failure", {birthError}}, true, false, "");
    compare({"unreadable-plus-readable", {sessionError, second}}, false, false, "runtime-identity-unavailable");
    auto upper = item(73, L"LOSSLESSSCALING.EXE"), mixed = item(74, L"lOsSlEsSsCaLiNg.ExE");
    compare({"ascii-upper", {upper}}, true, true, "", upper.pid, upper.birth, upper.path);
    compare({"ascii-mixed", {mixed}}, true, true, "", mixed.pid, mixed.birth, mixed.path);
    auto counted = mixed; counted.terminateName = false;
    compare({"crt-counted-name-without-nul", {counted}}, true, true, "", counted.pid, counted.birth, counted.path);
    auto countedOther = item(86, L"LosslessScaling.exe.old"); countedOther.terminateName = false;
    compare({"crt-counted-nonmatch-without-nul", {countedOther}}, true, false, "");
    compare({"nonmatching-names", {item(81,L"LosslessScaling.exe.old"),item(82,L"LosslessScaling"),
        item(83,L"xLosslessScaling.exe"),item(84,L"LosslessScaling.ex"),item(85,L"Other.exe")}}, true, false, "");
    compare({"nonmatching-plus-match", {item(81,L"Other.exe"),p,item(82,L"LosslessScaling.exe.old")}}, true, true, "", p.pid, p.birth, p.path);
    // Same cached resolver function + same scratch storage; every call must use new OS records.
    compare({"fresh-empty", {}}, true, false, "");
    compare({"fresh-arrival", {p}}, true, true, "", p.pid, p.birth, p.path);
    auto replacement = p; replacement.birth += 900; replacement.path = L"Z:\\Replacement\\LosslessScaling.exe";
    compare({"fresh-same-pid-new-birth-and-path", {replacement}}, true, true, "", replacement.pid, replacement.birth, replacement.path);
    compare({"fresh-departure", {}}, true, false, "");
    const std::pair<QueryMode,const char*> bad[] = {
        {QueryMode::ZeroReturned,"basic-success-zero-returned-is-malformed"},
        {QueryMode::Failed,"basic-status-failed"},{QueryMode::BadOffset,"basic-invalid-offset"},
        {QueryMode::BadLength,"basic-invalid-length"},{QueryMode::BadPointer,"basic-invalid-pointer"},
        {QueryMode::CrossRowPointer,"basic-pointer-crosses-own-row"},{QueryMode::BadCapacity,"basic-capacity-crosses-own-row"},
        {QueryMode::BadPid,"basic-invalid-pid"},{QueryMode::Truncated,"basic-truncated"},
        {QueryMode::Oversized,"basic-returned-over-capacity"},{QueryMode::RetryExhausted,"basic-retry-exhausted"},
        {QueryMode::OverBudget,"basic-buffer-over-budget"}
    };
    for (const auto& test : bad) {
        Scenario scenario{test.second,{p}}; const auto observed = run(scenario,test.first,0);
        expect(observed,false,false,"unobserved");
        require(observed.metrics.opens == 0 && observed.metrics.first == 0 && observed.metrics.next == 0,
            "BASIC_FAILED_IS_UNKNOWN_WITHOUT_FALLBACK_OR_IDENTITY");
        if (test.first == QueryMode::RetryExhausted) require(observed.metrics.queries == 4,"BASIC_RETRY_BOUND_FOUR");
        record(scenario,"basic-failed-no-fallback",observed);
    }
    for (const auto mode : {QueryMode::NotImplemented,QueryMode::NotSupported}) {
        Scenario scenario{mode==QueryMode::NotImplemented?"basic-not-implemented":"basic-not-supported",{p}};
        const auto observed=run(scenario,mode,1);expect(observed,true,true,"",p.pid,p.birth,p.path);
        record(scenario,"actual-product-toolhelp",observed);
    }
    {Scenario scenario{"basic-retry-then-complete",{p}};const auto observed=run(scenario,QueryMode::GrowOnce,0);
     expect(observed,true,true,"",p.pid,p.birth,p.path);require(observed.metrics.queries==2,"BASIC_RETRY_THEN_FRESH_COMPLETE");record(scenario,"basic-complete",observed);}
    {Scenario scenario{"fallback-snapshot-error",{p}};scenario.snapshotOk=false;
     const auto observed=run(scenario,QueryMode::Unavailable,1);expect(observed,false,false,"unobserved");record(scenario,"actual-product-toolhelp",observed);}
    {Scenario scenario{"fallback-first-error",{p}};scenario.firstFails=true;scenario.enumerationError=ERROR_ACCESS_DENIED;
     const auto observed=run(scenario,QueryMode::Unavailable,1);expect(observed,false,false,"unobserved");record(scenario,"actual-product-toolhelp",observed);}
    {Scenario scenario{"fallback-next-error",{p}};scenario.enumerationError=ERROR_ACCESS_DENIED;
     const auto observed=run(scenario,QueryMode::Unavailable,1);expect(observed,false,false,"unobserved");record(scenario,"actual-product-toolhelp",observed);}
}
inline void sharedInventoryGroups() {
    auto lossless=item(); auto mouse=item(73); mouse.name=L"JoyXoff.exe"; mouse.path=L"C:\\Fixture\\JoyXoff.exe";
    const Scenario scenario{"shared-inventory",{lossless,mouse}};
    const auto observed=run(scenario,QueryMode::Complete,0);
    record(scenario,"shared-inventory",observed);
    expect(observed,true,true,"",lossless.pid,lossless.birth,lossless.path);
    const auto first=ymcc::processInventory::read();
    const ymcc::gameInventory::SharedBasicSnapshot game;
    require(game.valid()&&game.entries().size()==3,"GAME_AND_RUNTIME_CONSUME_ONE_OWNER");
    const auto queries=state().metrics.queries;
    std::vector<DWORD> pids; DWORD error=ERROR_SUCCESS;
    require(ymcc::processNames::candidates(L"joyxoff.EXE",pids,error,
        ymcc::processNames::resolveBasicQuery(),ymcc::processNames::NameMatch::CrtIgnoreCase)==ymcc::processNames::BasicResult::Complete&&pids==std::vector<DWORD>{73},"CROSS_NAME_CONSUMES_SAME_TABLE");
    require(state().metrics.queries==queries&&first==ymcc::processInventory::read(),"CROSS_MODULE_NO_SECOND_QUERY");
    std::shared_ptr<const ymcc::processInventory::Snapshot> concurrent[8];
    std::vector<std::thread> readers;
    ymcc::processInventory::invalidate();
    const auto beforeConcurrent=state().metrics.queries;
    for(unsigned i=0;i<8;++i)readers.emplace_back([&,i]{concurrent[i]=ymcc::processInventory::read();});
    for(auto& reader:readers)reader.join();
    require(state().metrics.queries==beforeConcurrent+1,"CONCURRENT_READERS_ONE_PRODUCER");
    for(const auto& reader:concurrent)require(reader==concurrent[0],"CONCURRENT_IMMUTABLE_PUBLICATION");
    clock+=249;require(ymcc::processInventory::read()==concurrent[0],"CACHE_BEFORE_BOUNDARY");
    state().scenario.processes.clear(); clock+=1;
    const auto replaced=ymcc::processInventory::read();
    require(replaced!=concurrent[0]&&state().metrics.queries==beforeConcurrent+2,"EXACT_BOUNDARY_REFRESHES");
    require(replaced->rows.size()==1&&first->rows.size()==3&&first->rows[1].name==L"LosslessScaling.exe","OLD_READER_OWNS_ITS_ROWS");
    state().mode=QueryMode::Failed; ymcc::processInventory::invalidate();
    const auto failed=ymcc::processInventory::read();const auto failedQueries=state().metrics.queries;
    require(failed->status==ymcc::processInventory::Status::Failed&&failed->rows.empty(),"FAILED_REFRESH_NEVER_RETURNS_OLD_SUCCESS");
    require(ymcc::processInventory::read()==failed&&state().metrics.queries==failedQueries,"FAILED_RESULT_ALSO_COALESCED");
    require(state().metrics.snapshots==0,"QUERY_FAILURE_CANNOT_FALLBACK_TO_TOOLHELP");
    // Same cached PID was replaced with another image: the held handle decides.
    auto recycled=lossless;recycled.path=L"C:\\Fixture\\Unrelated.exe";
    const Scenario wrongScenario{"cached-pid-reused-other-image",{recycled}};
    const auto wrong=run(wrongScenario,QueryMode::Complete,0); record(wrongScenario,"shared-inventory",wrong);
    expect(wrong,false,false,"runtime-identity-unavailable");
}
inline int main(int argc, wchar_t** argv) {
    const bool inventory=argc==2&&std::wcscmp(argv[1],L"--inventory-only")==0;
    const bool negative=argc==2&&std::wcscmp(argv[1],L"--legacy-no-toolhelp")==0;
    try {
        require(argc==1||negative||inventory,"DISCOVERY_ARGUMENTS");
        if(negative){
            const auto p=item();Scenario scenario{"legacy-no-toolhelp-precondition",{p}};
            const auto observed=run(scenario,QueryMode::Unavailable,1);expect(observed,true,true,"",p.pid,p.birth,p.path);
            record(scenario,"actual-product-toolhelp",observed);
            require(observed.metrics.snapshots==0,"NO_TOOLHELP_FAST_PATH");
            throw Assertion("NEGATIVE_DID_NOT_REJECT","legacy fallback unexpectedly passed");
        }
        if(!inventory)normalGroups();
        else {
            const auto p=item();
            for(bool firstFails:{true,false}) {
                Scenario incomplete{firstFails?"fallback-first-error":"fallback-next-error",{p}};
                incomplete.firstFails=firstFails;incomplete.enumerationError=ERROR_ACCESS_DENIED;
                const auto observed=run(incomplete,QueryMode::Unavailable,1);
                expect(observed,false,false,"unobserved");record(incomplete,"actual-product-toolhelp",observed);
            }
        }
        sharedInventoryGroups();
        std::cout << "{\"pass\":true,\"canonicalId\":\"controller.lossless-toggle-owner\",\"selection\":\"discovery\",\"publicDiscoverRuntimeExecuted\":true,\"checks\":"
            << checks << ",\"comparisons\":" << comparisons << ",\"executions\":" << executions
            << ",\"actualOSInput\":false,\"actualLsControl\":false,\"normalBusinessExecuted\":false,\"rows\":[";
        for(std::size_t i=0;i<rows.size();++i){if(i)std::cout<<',';std::cout<<rows[i];}
        std::cout << "]}" << std::endl;return 0;
    } catch(const Assertion& error) {
        std::cout << "{\"pass\":false,\"selection\":" << quote(std::string(negative?"legacy-no-toolhelp":"discovery"))
            << ",\"assertion\":" << quote(error.tag) << ",\"error\":" << quote(std::string(error.what()))
            << ",\"checks\":" << checks << ",\"executions\":" << executions << ",\"comparisons\":" << comparisons
            << ",\"outstandingFakeHandles\":" << state().handles.size() << ",\"metrics\":" << metricsJson(state().metrics) << ",\"rows\":[";
        for(std::size_t i=0;i<rows.size();++i){if(i)std::cout<<',';std::cout<<rows[i];}
        std::cout << "]}" << std::endl;
        return 1;
    } catch(const std::exception& error) {
        std::cout << "{\"pass\":false,\"assertion\":\"UNEXPECTED_EXCEPTION\",\"error\":" << quote(std::string(error.what()))
            << ",\"outstandingFakeHandles\":" << state().handles.size() << "}" << std::endl;return 1;
    }
}
} // namespace ymcc::losslessDiscoveryFixture
