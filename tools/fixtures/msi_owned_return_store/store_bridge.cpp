#include "store_bridge.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cwctype>
#include <functional>
#include <mutex>
#include <new>
#include <utility>

// Adapted from tools/msi_crash_record_io_selftest.cpp's real OS leaves.
// Source identity and every intentional delta are in BRIDGE-SOURCE-INTAKE.json.
// No fault selector, event, child, clock override or fabricated successful leaf.
namespace {
struct State {
    std::mutex mutex;
    bool attempted = false, initialized = false;
    std::wstring target;
    HANDLE lease = INVALID_HANDLE_VALUE;
    std::array<HANDLE, 32> files{};
    std::array<HANDLE, 128> directories{};
    unsigned long long writes = 0, flushes = 0, moves = 0, replaces = 0;
    unsigned long long reads = 0, infos = 0, closes = 0, closeFailures = 0;
    DWORD lastMoveError = 0, lastReplaceError = 0, lastCloseError = 0;

    // Process-exit fallback only. Failed closes remain recorded and occupied;
    // destruction never emits a successful cleanup receipt.
    ~State() {
        for (auto& h : files) closeAtExit(h);
        for (auto& h : directories) closeAtExit(h);
    }
    void closeAtExit(HANDLE& h) noexcept {
        if (!h) return;
        ++closes;
        const BOOL ok = ::CloseHandle(h);
        const DWORD error = ::GetLastError();
        if (ok) {
            if (lease == h) lease = INVALID_HANDLE_VALUE;
            h = nullptr;
        } else {
            ++closeFailures;
            lastCloseError = error;
        }
        ::SetLastError(error);
    }
};
State state;

template <size_t N> HANDLE* emptySlot(std::array<HANDLE, N>& handles) {
    for (auto& h : handles) if (!h) return &h;
    return nullptr;
}
template <size_t N> size_t live(const std::array<HANDLE, N>& handles) {
    return static_cast<size_t>(std::count_if(handles.begin(), handles.end(),
        [](HANDLE h) { return h != nullptr; }));
}
bool fail(DWORD value, unsigned long& error) {
    error = value;
    ::SetLastError(value);
    return false;
}

namespace bridgeLeaf {
static HANDLE create(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sec,
    DWORD d, DWORD f, HANDLE t) {
    HANDLE* slot = emptySlot(state.files);
    if (!slot) { ::SetLastError(ERROR_TOO_MANY_OPEN_FILES); return INVALID_HANDLE_VALUE; }
    const HANDLE h = ::CreateFileW(p, a, s, sec, d, f, t);
    const DWORD error = ::GetLastError();
    if (h != INVALID_HANDLE_VALUE) *slot = h;
    ::SetLastError(error);
    return h;
}
static BOOL close(HANDLE h) {
    auto slot = std::find(state.files.begin(), state.files.end(), h);
    if (!h || h == INVALID_HANDLE_VALUE || slot == state.files.end()) {
        ::SetLastError(ERROR_INVALID_HANDLE);
        return FALSE; // Never close a borrowed or untracked handle.
    }
    ++state.closes;
    const BOOL ok = ::CloseHandle(h);
    const DWORD error = ::GetLastError();
    if (ok) {
        *slot = nullptr;
        if (state.lease == h) state.lease = INVALID_HANDLE_VALUE;
    } else {
        ++state.closeFailures;
        state.lastCloseError = error;
    }
    ::SetLastError(error);
    return ok;
}
static BOOL write(HANDLE h, LPCVOID p, DWORD n, LPDWORD done, LPOVERLAPPED o) {
    ++state.writes;
    return ::WriteFile(h, p, n, done, o);
}
static BOOL flush(HANDLE h) {
    ++state.flushes;
    return ::FlushFileBuffers(h);
}
static BOOL move(LPCWSTR from, LPCWSTR to, DWORD flags) {
    ++state.moves;
    const BOOL ok = ::MoveFileExW(from, to, flags);
    const DWORD error = ::GetLastError();
    if (!ok) state.lastMoveError = error;
    ::SetLastError(error);
    return ok;
}
static BOOL replace(LPCWSTR to, LPCWSTR from, LPCWSTR backup, DWORD flags,
    LPVOID a, LPVOID b) {
    ++state.replaces;
    const BOOL ok = ::ReplaceFileW(to, from, backup, flags, a, b);
    const DWORD error = ::GetLastError();
    if (!ok) state.lastReplaceError = error;
    ::SetLastError(error);
    return ok;
}
static BOOL read(HANDLE h, LPVOID p, DWORD n, LPDWORD done, LPOVERLAPPED o) {
    ++state.reads;
    return ::ReadFile(h, p, n, done, o);
}
static BOOL info(HANDLE h, LPBY_HANDLE_FILE_INFORMATION p) {
    ++state.infos;
    return ::GetFileInformationByHandle(h, p);
}
}

// The caller generates exactly the three unchanged production definitions.
// SDK types and function-local locks stay wholly within this second TU.
#define CreateFileW bridgeLeaf::create
#define CloseHandle bridgeLeaf::close
#define WriteFile bridgeLeaf::write
#define FlushFileBuffers bridgeLeaf::flush
#define MoveFileExW bridgeLeaf::move
#define ReplaceFileW bridgeLeaf::replace
#define ReadFile bridgeLeaf::read
#define GetFileInformationByHandle bridgeLeaf::info
#include "actual-store-writer.inc"
#undef CreateFileW
#undef CloseHandle
#undef WriteFile
#undef FlushFileBuffers
#undef MoveFileExW
#undef ReplaceFileW
#undef ReadFile
#undef GetFileInformationByHandle

// Ordinary drive-absolute and UNC paths; device namespaces and ADS are rejected.
bool fullPath(const std::wstring& input, std::wstring& output, unsigned long& error) {
    if (input.empty() || input.find(L'\0') != std::wstring::npos)
        return fail(ERROR_INVALID_NAME, error);
    std::wstring p = input;
    std::replace(p.begin(), p.end(), L'/', L'\\');
    const bool drive = p.size() >= 3 &&
        ((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z')) &&
        p[1] == L':' && p[2] == L'\\';
    const bool unc = p.size() > 4 && p[0] == L'\\' && p[1] == L'\\' &&
        p[2] != L'?' && p[2] != L'.';
    if ((!drive && !unc) || p.find(L':', drive ? 2 : 0) != std::wstring::npos)
        return fail(ERROR_INVALID_NAME, error);
    const DWORD needed = ::GetFullPathNameW(p.c_str(), 0, nullptr, nullptr);
    if (!needed) return fail(::GetLastError(), error);
    if (needed > 32700) return fail(ERROR_FILENAME_EXCED_RANGE, error);
    output.assign(needed, L'\0');
    const DWORD copied = ::GetFullPathNameW(p.c_str(), needed, output.data(), nullptr);
    if (!copied) return fail(::GetLastError(), error);
    if (copied >= needed) return fail(ERROR_INSUFFICIENT_BUFFER, error);
    output.resize(copied);
    while (output.size() > 3 && output.back() == L'\\') output.pop_back();
    return true;
}
bool pinDirectory(const std::wstring& path, unsigned long& error) {
    HANDLE* slot = emptySlot(state.directories);
    if (!slot) return fail(ERROR_TOO_MANY_OPEN_FILES, error);
    const HANDLE h = ::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return fail(::GetLastError(), error);
    *slot = h; // Retain before inspecting; even inspection/close failure is owned.
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(h, &info)) return fail(::GetLastError(), error);
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return fail(ERROR_ACCESS_DENIED, error);
    return true;
}
void cleanupDirectories() noexcept {
    for (auto& h : state.directories) state.closeAtExit(h);
}
bool allowed(const std::wstring& path, unsigned long& error) {
    if (!state.initialized) return fail(ERROR_NOT_READY, error);
    std::wstring resolved;
    if (!fullPath(path, resolved, error)) return false;
    // The initialized canonical spelling is the capability. Separators may vary.
    if (resolved != state.target) return fail(ERROR_ACCESS_DENIED, error);
    const DWORD attrs = ::GetFileAttributesW(state.target.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        const DWORD value = ::GetLastError();
        if (value != ERROR_FILE_NOT_FOUND) return fail(value, error);
    } else if (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
        return fail(ERROR_ACCESS_DENIED, error);
    }
    return true;
}
}

namespace msiStoreFixture {
bool initialize(const std::wstring& root, unsigned long& error) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.attempted) return fail(ERROR_ALREADY_INITIALIZED, error);
    state.attempted = true;
    try {
        std::wstring resolved;
        if (!fullPath(root, resolved, error)) return false;
        size_t prefixEnd = 3;
        if (resolved[0] == L'\\') {
            const auto serverEnd = resolved.find(L'\\', 2);
            if (serverEnd == std::wstring::npos || serverEnd == 2)
                return fail(ERROR_INVALID_NAME, error);
            prefixEnd = resolved.find(L'\\', serverEnd + 1);
            if (prefixEnd == serverEnd + 1) return fail(ERROR_INVALID_NAME, error);
            if (prefixEnd == std::wstring::npos) prefixEnd = resolved.size();
        }
        // Pin each ancestor without delete sharing, rejecting reparse points on
        // the opened objects. This also prevents root/ancestor rename while live.
        for (;;) {
            if (!pinDirectory(resolved.substr(0, prefixEnd), error)) {
                const DWORD first = error;
                cleanupDirectories();
                return fail(first, error);
            }
            if (prefixEnd == resolved.size()) break;
            const size_t next = resolved.find(L'\\', prefixEnd + (resolved[prefixEnd] == L'\\' ? 1 : 0));
            prefixEnd = next == std::wstring::npos ? resolved.size() : next;
        }
        state.target = resolved + (resolved.back() == L'\\' ? L"" : L"\\") + L"msi-controller-return.json";
        state.initialized = true;
        error = ERROR_SUCCESS;
        return true;
    } catch (const std::bad_alloc&) {
        cleanupDirectories();
        return fail(ERROR_NOT_ENOUGH_MEMORY, error);
    }
}
bool atomicWrite(const std::wstring& path, const std::string& text) {
    std::lock_guard<std::mutex> lock(state.mutex);
    unsigned long error = ERROR_SUCCESS;
    if (!allowed(path, error)) return false;
    return sgWriteFileAtomic(state.target, text);
}
bool readOwned(const std::wstring& path, std::string& text, unsigned long& error) {
    std::lock_guard<std::mutex> lock(state.mutex);
    text.clear();
    if (!allowed(path, error)) return false;
    const auto failures = state.closeFailures;
    error = msiCrashReadOwnedRecord(state.target, text);
    // Original reader's RAII ignores close's result. Preserve its business
    // definition, but never present a failed close as a completed bridge read.
    if (error == ERROR_SUCCESS && state.closeFailures != failures) error = state.lastCloseError;
    if (error != ERROR_SUCCESS) { text.clear(); ::SetLastError(error); return false; }
    return true;
}
bool acquireNoDeleteLease(const std::wstring& path, unsigned long& error) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!allowed(path, error)) return false;
    if (state.lease != INVALID_HANDLE_VALUE) return fail(ERROR_ALREADY_EXISTS, error);
    const HANDLE h = bridgeLeaf::create(state.target.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return fail(::GetLastError(), error);
    state.lease = h;
    BY_HANDLE_FILE_INFORMATION info{};
    DWORD failure = ERROR_SUCCESS;
    if (!::GetFileInformationByHandle(h, &info)) failure = ::GetLastError();
    else if (::GetFileType(h) != FILE_TYPE_DISK ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        failure = ERROR_ACCESS_DENIED;
    if (failure != ERROR_SUCCESS) {
        bridgeLeaf::close(h); // Failure stays held and visible; never clear blindly.
        return fail(failure, error);
    }
    error = ERROR_SUCCESS;
    return true;
}
bool releaseNoDeleteLease(unsigned long& error) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.lease != INVALID_HANDLE_VALUE && !bridgeLeaf::close(state.lease))
        return fail(::GetLastError(), error);
    error = ERROR_SUCCESS;
    return true;
}
bool shutdown(unsigned long& error) {
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto failures=state.closeFailures;
    for(auto& h:state.files)if(h)bridgeLeaf::close(h);
    cleanupDirectories();
    if(state.closeFailures!=failures || live(state.files)!=0 || live(state.directories)!=0 || state.lease!=INVALID_HANDLE_VALUE)
        return fail(state.lastCloseError?state.lastCloseError:ERROR_BUSY,error);
    state.initialized=false;error=ERROR_SUCCESS;return true;
}
std::string traceJson() {
    std::lock_guard<std::mutex> lock(state.mutex);
    return "{\"writes\":" + std::to_string(state.writes) +
        ",\"flushes\":" + std::to_string(state.flushes) +
        ",\"moves\":" + std::to_string(state.moves) +
        ",\"replaces\":" + std::to_string(state.replaces) +
        ",\"reads\":" + std::to_string(state.reads) +
        ",\"infos\":" + std::to_string(state.infos) +
        ",\"lastMoveError\":" + std::to_string(state.lastMoveError) +
        ",\"lastReplaceError\":" + std::to_string(state.lastReplaceError) +
        ",\"fileHandlesLive\":" + std::to_string(live(state.files)) +
        ",\"leaseHeld\":" + (state.lease != INVALID_HANDLE_VALUE ? "true" : "false") +
        ",\"closes\":" + std::to_string(state.closes) +
        ",\"closeFailures\":" + std::to_string(state.closeFailures) +
        ",\"lastCloseError\":" + std::to_string(state.lastCloseError) +
        ",\"directoryHandlesLive\":" + std::to_string(live(state.directories)) +
        ",\"initialized\":" + (state.initialized ? "true" : "false") + "}";
}
unsigned long processId() { return ::GetCurrentProcessId(); }
std::string processCreation() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!::GetProcessTimes(::GetCurrentProcess(), &creation, &exit, &kernel, &user)) return {};
    const unsigned long long ticks =
        (static_cast<unsigned long long>(creation.dwHighDateTime) << 32) |
        static_cast<unsigned long long>(creation.dwLowDateTime);
    return std::to_string(ticks);
}
}
