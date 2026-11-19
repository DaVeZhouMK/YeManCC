// Private, single-translation-unit observation of actual Observer Win32 APIs.
// Include at GLOBAL scope, with an existing complete ::json alias (nlohmann::json).
// No macro is installed by this file. Parent installs/restores the eight macros
// listed in PREPARATION.json ONLY around steam_session_observer.h's first include.
// All transitive product/STL/SDK dependencies must be included before that window.
// No std::thread/CRT, SDK-internal or NtQueryObject coverage is claimed.
#pragma once
#pragma push_macro("CreateEventW")
#pragma push_macro("OpenEventW")
#pragma push_macro("OpenProcess")
#pragma push_macro("CloseHandle")
#pragma push_macro("RegOpenKeyExW")
#pragma push_macro("RegCloseKey")
#pragma push_macro("SetWinEventHook")
#pragma push_macro("UnhookWinEvent")
#undef CreateEventW
#undef OpenEventW
#undef OpenProcess
#undef CloseHandle
#undef RegOpenKeyExW
#undef RegCloseKey
#undef SetWinEventHook
#undef UnhookWinEvent
#include <windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace ObserverResources {
namespace detail {
enum class Kind : unsigned { Event, Process, RegistryKey, WinEventHook, Count };
static constexpr std::size_t kindCount = static_cast<std::size_t>(Kind::Count);
static constexpr std::size_t maxRows = 16384;
static const char* kindName(Kind kind) noexcept {
    switch (kind) {
    case Kind::Event: return "event";
    case Kind::Process: return "process";
    case Kind::RegistryKey: return "registryKey";
    case Kind::WinEventHook: return "winEventHook";
    default: return "untrackedKernelHandle";
    }
}
struct Counts {
    std::uint64_t acquired = 0, released = 0, acquireFailures = 0;
    std::uint64_t closeFailures = 0, untrackedCloses = 0;
};
struct Row {
    Kind kind = Kind::Count;
    std::uintptr_t value = 0;
    std::uint64_t serial = 0;
    DWORD threadId = 0, lastError = 0;
    bool acquired = false, outstanding = false, duplicateLive = false;
    std::uint64_t closeAttempts = 0, failedCloseAttempts = 0;
    DWORD closeThreadId = 0, closeLastError = 0;
    LSTATUS closeStatus = ERROR_SUCCESS;
    DWORD firstFailedCloseThreadId = 0, firstFailedCloseLastError = 0;
    LSTATUS firstFailedCloseStatus = ERROR_SUCCESS;
    // 0=no attempt, 1=success, 2=failure. WinEvent hooks never use CloseHandle.
    unsigned closeOutcome = 0;
};
using Key = std::pair<Kind, std::uintptr_t>;
struct Ledger {
    std::mutex mutex;
    std::map<Key, std::size_t> live;
    std::vector<Row> rows;
    std::array<Counts, kindCount> counts{};
    std::uint64_t serial = 0, errors = 0, baselineCalls = 0, baselineRejected = 0;
    std::uint64_t untrackedKernelCloses = 0;
    std::uint64_t ownPairedCloseCount = 0, forwardedCloseCount = 0, pairedCloseFailureCount = 0;
};
static std::atomic<std::uint64_t> observerErrors{0};
static Ledger& ledger() {
    static Ledger value;
    return value;
}
static void observationFailed() noexcept {
    observerErrors.fetch_add(1, std::memory_order_relaxed);
}
struct PreserveLastError {
    DWORD value = ::GetLastError();
    ~PreserveLastError() noexcept { ::SetLastError(value); }
};
static std::size_t index(Kind kind) noexcept { return static_cast<std::size_t>(kind); }
template<class T> static std::uintptr_t numeric(T value) noexcept {
    return reinterpret_cast<std::uintptr_t>(value);
}
// Serialize the real acquire/close and its record. Recording only AFTER an
// unlocked CloseHandle would misclassify another thread's immediate value reuse.
// No callback, JSON, logging, wait loop or business substitute runs under this lock.
// An exceptional ledger failure calls the original API once, and fails proof
// closed through observerErrors; the original result and last-error are retained.
template<class Call, class Observe>
static auto observed(Call call, Observe observe) noexcept -> decltype(call()) {
    const DWORD incomingError = ::GetLastError();
    decltype(call()) result{};
    DWORD apiError = incomingError;
    bool called = false;
    try {
        auto& state = ledger();
        std::lock_guard<std::mutex> guard(state.mutex);
        ::SetLastError(incomingError);
        result = call();
        apiError = ::GetLastError(); // Before ANY observation.
        called = true;
        observe(state, result, apiError);
    } catch (...) {
        observationFailed();
        if (!called) {
            ::SetLastError(incomingError);
            result = call();
            apiError = ::GetLastError();
        }
    }
    ::SetLastError(apiError);
    return result;
}
static bool append(Ledger& state, Row row, std::size_t& slot) {
    if (state.rows.size() >= maxRows ||
        state.serial == (std::numeric_limits<std::uint64_t>::max)()) {
        observationFailed();
        return false;
    }
    row.serial = ++state.serial;
    slot = state.rows.size();
    state.rows.push_back(row);
    return true;
}
static void acquired(Ledger& state, Kind kind, std::uintptr_t value, bool success, DWORD error) {
    auto& count = state.counts[index(kind)];
    if (!success) { ++count.acquireFailures; return; }
    ++count.acquired;
    if (!value) { ++state.errors; observationFailed(); return; }
    const Key key{kind, value};
    const bool duplicate = state.live.find(key) != state.live.end();
    if (duplicate) ++state.errors;
    Row row;
    row.kind = kind; row.value = value; row.threadId = ::GetCurrentThreadId();
    row.lastError = error; row.acquired = true; row.outstanding = true;
    row.duplicateLive = duplicate;
    std::size_t slot = 0;
    if (!append(state, row, slot)) return;
    // Keep both historical rows on a live duplicate; do not replace its owner.
    // Its unresolved extra acquisition remains outstanding and errors remain sticky.
    if (!duplicate) state.live.emplace(key, slot);
}
static void closed(Ledger& state, Kind kind, std::uintptr_t value,
                   bool success, DWORD error, LSTATUS status) {
    const auto found = state.live.find(Key{kind, value});
    if (found == state.live.end()) {
        ++state.errors;
        if (kind == Kind::Count) ++state.untrackedKernelCloses;
        else ++state.counts[index(kind)].untrackedCloses;
        if (!success && kind != Kind::Count) ++state.counts[index(kind)].closeFailures;
        Row row;
        row.kind = kind; row.value = value; row.threadId = ::GetCurrentThreadId();
        row.lastError = error; row.closeAttempts = 1; row.closeThreadId = row.threadId;
        row.closeLastError = error; row.closeStatus = status;
        row.closeOutcome = success ? 1u : 2u;
        if (!success) {
            row.failedCloseAttempts = 1; row.firstFailedCloseThreadId = row.threadId;
            row.firstFailedCloseLastError = error; row.firstFailedCloseStatus = status;
        }
        std::size_t slot = 0;
        append(state, row, slot);
        return;
    }
    auto& row = state.rows[found->second];
    ++row.closeAttempts;
    row.closeThreadId = ::GetCurrentThreadId();
    row.closeLastError = error; row.closeStatus = status;
    row.closeOutcome = success ? 1u : 2u;
    if (!success) {
        ++state.errors; ++state.counts[index(kind)].closeFailures;
        if (!row.failedCloseAttempts) {
            row.firstFailedCloseThreadId = row.closeThreadId;
            row.firstFailedCloseLastError = error; row.firstFailedCloseStatus = status;
        }
        ++row.failedCloseAttempts;
        return; // Preserve live map entry and outstanding acquisition.
    }
    row.outstanding = false;
    ++state.counts[index(kind)].released;
    state.live.erase(found);
}
struct Snapshot {
    std::array<Counts, kindCount> counts{};
    std::vector<Row> rows;
    std::array<std::uint64_t, kindCount> outstanding{};
    std::uint64_t errors = 0, baselineCalls = 0, baselineRejected = 0;
    std::uint64_t untrackedKernelCloses = 0, observerErrorCount = 0;
    std::uint64_t ownPairedCloseCount = 0, forwardedCloseCount = 0, pairedCloseFailureCount = 0;
    bool available = false;
};
static Snapshot snapshot() {
    Snapshot copy;
    try {
        auto& state = ledger();
        std::lock_guard<std::mutex> guard(state.mutex);
        copy.counts = state.counts;
        copy.rows = state.rows;
        copy.errors = state.errors;
        copy.baselineCalls = state.baselineCalls;
        copy.baselineRejected = state.baselineRejected;
        copy.untrackedKernelCloses = state.untrackedKernelCloses;
        copy.ownPairedCloseCount = state.ownPairedCloseCount;
        copy.forwardedCloseCount = state.forwardedCloseCount;
        copy.pairedCloseFailureCount = state.pairedCloseFailureCount;
        // Rows include any unresolved duplicate or partially recorded acquisition.
        for (const auto& row : copy.rows)
            if (row.acquired && row.outstanding && row.kind != Kind::Count)
                ++copy.outstanding[index(row.kind)];
        copy.observerErrorCount = observerErrors.load(std::memory_order_relaxed);
        copy.available = true;
    } catch (...) {
        observationFailed();
        copy.observerErrorCount = observerErrors.load(std::memory_order_relaxed);
    }
    return copy; // Lock is gone before any JSON allocation.
}
} // namespace detail

[[maybe_unused]] static HANDLE WINAPI createEventW(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCWSTR name) noexcept {
    return detail::observed([&] { return ::CreateEventW(sa, manual, initial, name); },
        [&](detail::Ledger& s, HANDLE r, DWORD e) {
            detail::acquired(s, detail::Kind::Event, detail::numeric(r), r != nullptr, e);
        });
}
[[maybe_unused]] static HANDLE WINAPI openEventW(DWORD access, BOOL inherit, LPCWSTR name) noexcept {
    return detail::observed([&] { return ::OpenEventW(access, inherit, name); },
        [&](detail::Ledger& s, HANDLE r, DWORD e) {
            detail::acquired(s, detail::Kind::Event, detail::numeric(r), r != nullptr, e);
        });
}
[[maybe_unused]] static HANDLE WINAPI openProcess(DWORD access, BOOL inherit, DWORD pid) noexcept {
    return detail::observed([&] { return ::OpenProcess(access, inherit, pid); },
        [&](detail::Ledger& s, HANDLE r, DWORD e) {
            detail::acquired(s, detail::Kind::Process, detail::numeric(r), r != nullptr, e);
        });
}
[[maybe_unused]] static BOOL WINAPI closeHandle(HANDLE value) noexcept {
    return detail::observed([&] { return ::CloseHandle(value); },
        [&](detail::Ledger& s, BOOL r, DWORD e) {
            const auto v = detail::numeric(value);
            const bool event = s.live.find({detail::Kind::Event, v}) != s.live.end();
            const bool process = s.live.find({detail::Kind::Process, v}) != s.live.end();
            if (event && process) {
                ++s.errors; // Ambiguous kernel kind: do not erase either owner.
                detail::closed(s, detail::Kind::Count, v, r != FALSE, e, r ? ERROR_SUCCESS : static_cast<LSTATUS>(e));
            } else {
                detail::closed(s, event ? detail::Kind::Event : process ? detail::Kind::Process : detail::Kind::Count,
                    v, r != FALSE, e, r ? ERROR_SUCCESS : static_cast<LSTATUS>(e));
            }
        });
}
// Only the parent typed-client CloseHandle include window uses this bridge.
// Lookup, genuine paired close and original row update share the acquire/close
// mutex. A request-ledger callback is never invoked while this mutex is held.
// Token ownership remains entirely in the request ledger.
[[maybe_unused]] static BOOL WINAPI closeHandleOrForward(HANDLE value, BOOL (WINAPI* fallback)(HANDLE)) noexcept {
    const DWORD incomingError = ::GetLastError();
    if (!fallback) {
        detail::observationFailed();
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL result = FALSE;
    DWORD apiError = incomingError;
    bool apiCalled = false, forward = false;
    try {
        auto& state = detail::ledger();
        std::lock_guard<std::mutex> guard(state.mutex);
        const auto v = detail::numeric(value);
        const bool event = state.live.find({detail::Kind::Event, v}) != state.live.end();
        const bool process = state.live.find({detail::Kind::Process, v}) != state.live.end();
        if (event && process) {
            // Ambiguous ownership: reject without closing or retiring either row.
            ++state.errors;
            detail::observationFailed();
            apiError = ERROR_INVALID_HANDLE;
        } else if (event || process) {
            ++state.ownPairedCloseCount;
            ::SetLastError(incomingError);
            apiCalled = true; // Before the side effect: never replay a close.
            result = ::CloseHandle(value);
            apiError = ::GetLastError();
            if (result == FALSE) ++state.pairedCloseFailureCount;
            detail::closed(state, event ? detail::Kind::Event : detail::Kind::Process,
                v, result != FALSE, apiError, result ? ERROR_SUCCESS : static_cast<LSTATUS>(apiError));
        } else {
            ++state.forwardedCloseCount;
            forward = true;
        }
    } catch (...) {
        detail::observationFailed();
        if (!apiCalled) {
            // No safe ownership decision without the lock; do not guess or close.
            forward = false;
            apiError = ERROR_GEN_FAILURE;
        }
        // After a genuine close, preserve its exact outcome even if recording fails.
    }
    if (forward) {
        // The Observer mutex has been released on every path reaching this call.
        ::SetLastError(incomingError);
        try {
            result = fallback(value);
            apiError = ::GetLastError();
        } catch (...) {
            // A throwing callback has no returned outcome. Never invoke it twice.
            detail::observationFailed();
            result = FALSE;
            apiError = ERROR_GEN_FAILURE;
        }
    }
    ::SetLastError(apiError);
    return result;
}
[[maybe_unused]] static LSTATUS WINAPI regOpenKeyExW(HKEY key, LPCWSTR subkey, DWORD options, REGSAM access, PHKEY result) noexcept {
    return detail::observed([&] { return ::RegOpenKeyExW(key, subkey, options, access, result); },
        [&](detail::Ledger& s, LSTATUS r, DWORD e) {
            // A failed call's out parameter is never read.
            const auto value = r == ERROR_SUCCESS && result ? detail::numeric(*result) : 0;
            detail::acquired(s, detail::Kind::RegistryKey, value, r == ERROR_SUCCESS, e);
        });
}
[[maybe_unused]] static LSTATUS WINAPI regCloseKey(HKEY key) noexcept {
    return detail::observed([&] { return ::RegCloseKey(key); },
        [&](detail::Ledger& s, LSTATUS r, DWORD e) {
            detail::closed(s, detail::Kind::RegistryKey, detail::numeric(key), r == ERROR_SUCCESS, e, r);
        });
}
[[maybe_unused]] static HWINEVENTHOOK WINAPI setWinEventHook(DWORD first, DWORD last, HMODULE module,
        WINEVENTPROC callback, DWORD process, DWORD thread, DWORD flags) noexcept {
    return detail::observed([&] { return ::SetWinEventHook(first, last, module, callback, process, thread, flags); },
        [&](detail::Ledger& s, HWINEVENTHOOK r, DWORD e) {
            detail::acquired(s, detail::Kind::WinEventHook, detail::numeric(r), r != nullptr, e);
        });
}
[[maybe_unused]] static BOOL WINAPI unhookWinEvent(HWINEVENTHOOK hook) noexcept {
    return detail::observed([&] { return ::UnhookWinEvent(hook); },
        [&](detail::Ledger& s, BOOL r, DWORD e) {
            detail::closed(s, detail::Kind::WinEventHook, detail::numeric(hook), r != FALSE, e,
                r ? ERROR_SUCCESS : static_cast<LSTATUS>(e));
        });
}
// Optional assertion marker only. It NEVER clears errors, counts or history.
// A nonempty baseline is rejected and remains visible in the receipt.
[[maybe_unused]] static void baseline() noexcept {
    detail::PreserveLastError preserve;
    try {
        auto& state = detail::ledger();
        std::lock_guard<std::mutex> guard(state.mutex);
        ++state.baselineCalls;
        bool outstanding = !state.live.empty();
        for (const auto& row : state.rows) outstanding = outstanding || row.outstanding;
        if (outstanding) { ++state.errors; ++state.baselineRejected; }
    } catch (...) { detail::observationFailed(); }
}
[[maybe_unused]] static json receipt() {
    detail::PreserveLastError preserve;
    try {
        const auto copy = detail::snapshot();
        json entries = json::array(), counts = json::object(), outstanding = json::object();
        json acquiredByKind = json::object(), releasedByKind = json::object();
        std::uint64_t acquired = 0, released = 0, pending = 0;
        std::uint64_t acquiredEntries = 0, failedCloseEntries = 0, duplicateEntries = 0;
        for (std::size_t i = 0; i < detail::kindCount; ++i) {
            const auto& n = copy.counts[i];
            const auto* kind = detail::kindName(static_cast<detail::Kind>(i));
            counts[kind] = {{"acquired", n.acquired}, {"released", n.released},
                {"acquireFailures", n.acquireFailures}, {"closeFailures", n.closeFailures},
                {"untrackedCloses", n.untrackedCloses}};
            outstanding[kind] = copy.outstanding[i];
            acquiredByKind[kind] = n.acquired; releasedByKind[kind] = n.released;
            acquired += n.acquired; released += n.released; pending += copy.outstanding[i];
        }
        for (const auto& row : copy.rows) {
            if (row.acquired) ++acquiredEntries;
            if (row.failedCloseAttempts) ++failedCloseEntries;
            if (row.duplicateLive) ++duplicateEntries;
            entries.push_back({{"kind", detail::kindName(row.kind)}, {"value", row.value},
                {"serial", row.serial}, {"threadId", row.threadId}, {"lastError", row.lastError},
                {"acquired", row.acquired}, {"outstanding", row.outstanding}, {"duplicateLive", row.duplicateLive},
                {"closeAttempts", row.closeAttempts}, {"failedCloseAttempts", row.failedCloseAttempts},
                {"closeThreadId", row.closeThreadId}, {"closeLastError", row.closeLastError},
                {"closeStatus", row.closeStatus},
                {"closeOutcome", row.closeOutcome == 0 ? "notAttempted" : row.closeOutcome == 1 ? "success" : "failed"},
                {"firstFailedCloseThreadId", row.firstFailedCloseThreadId},
                {"firstFailedCloseLastError", row.firstFailedCloseLastError}, {"firstFailedCloseStatus", row.firstFailedCloseStatus}});
        }
        const auto errors = copy.errors + copy.observerErrorCount;
        const bool returned = copy.available && errors == 0 && pending == 0 && acquired == released;
        return {{"schemaVersion", 1}, {"scope", "actual Observer scoped API ledger"},
            {"snapshotAvailable", copy.available}, {"errors", errors}, {"observerErrors", copy.observerErrorCount},
            {"ledgerErrors", copy.errors}, {"outstanding", outstanding}, {"outstandingTotal", pending},
            {"acquired", acquired}, {"released", released}, {"counts", counts},
            {"acquiredByKind", acquiredByKind}, {"releasedByKind", releasedByKind}, {"liveByKind", outstanding},
            {"entries", entries}, {"rows", {{"total", copy.rows.size()}, {"acquired", acquiredEntries},
                {"untrackedClose", copy.rows.size() - acquiredEntries}, {"withFailedClose", failedCloseEntries},
                {"duplicateLive", duplicateEntries}}},
            {"rowCount", copy.rows.size()}, {"rowLimit", detail::maxRows},
            {"baselineCalls", copy.baselineCalls}, {"baselineRejected", copy.baselineRejected},
            {"untrackedKernelCloses", copy.untrackedKernelCloses},
            {"ownPairedCloseCount", copy.ownPairedCloseCount},
            {"forwardedCloseCount", copy.forwardedCloseCount},
            {"pairedCloseFailureCount", copy.pairedCloseFailureCount},
            {"requestCloseScope", "typed-client include window only: live Observer Event/Process paired here; other HANDLEs forwarded outside lock to request ledger; Token excluded"},
            {"allObserverTrackedResourcesReturned", returned},
            {"stdThreadCrtHandlesTracked", false}, {"sdkInternalResourcesTracked", false},
            {"wholeProcessHandleCensus", false}, {"allKernelResourcesReturnedProven", false}};
    } catch (...) {
        detail::observationFailed();
        throw; // Never return an apparently successful receipt after JSON allocation failure.
    }
}
} // namespace ObserverResources
#pragma pop_macro("UnhookWinEvent")
#pragma pop_macro("SetWinEventHook")
#pragma pop_macro("RegCloseKey")
#pragma pop_macro("RegOpenKeyExW")
#pragma pop_macro("CloseHandle")
#pragma pop_macro("OpenProcess")
#pragma pop_macro("OpenEventW")
#pragma pop_macro("CreateEventW")
