#pragma once
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Process topology hints only. A cached PID/name/sequence does not authorize
// an action or replace a consumer's live handle, birth, path or owner checks.
namespace ymcc::processInventory {
using Query = NTSTATUS (NTAPI*)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
inline constexpr ULONGLONG currentTime = (std::numeric_limits<ULONGLONG>::max)();
enum class Status { Complete, Unavailable, Failed };
struct Entry {
    DWORD pid = 0;
    std::wstring name;
    ULONG64 sequence = 0;
    DWORD parentPid = 0;
};
// Positive name facts from a partial Toolhelp prefix are rejection-only.
// They never establish absence, identity, parent topology or action authority.
struct RejectionHint { DWORD pid=0; std::wstring name; };
struct Snapshot {
    Status status = Status::Failed, basicStatus = Status::Unavailable;
    DWORD error = ERROR_SUCCESS;
    ULONGLONG observedAt = 0;
    std::uint64_t generation = 0;
    std::vector<Entry> rows;
    std::vector<RejectionHint> rejectionHints;
};

// Detailed hints have their own receipt and cache. Names here own their bytes;
// neither working set nor a verified birth hint authorizes a process action.
struct DetailEntry {
    DWORD pid = 0;
    std::wstring name;
    SIZE_T workingSet = 0;
    ULONGLONG processCreated = 0;
};
struct DetailSnapshot {
    Status status = Status::Failed;
    DWORD error = ERROR_SUCCESS;
    ULONGLONG observedAt = 0;
    std::uint64_t generation = 0; // Detail receipts only; never a process sequence.
    bool creationTimesVerified = false;
    std::vector<DetailEntry> rows;
};
// Borrowed views exist only for the pure parser and compatibility adapters.
// They require the input buffer (or an owning detail receipt) to remain alive.
struct DetailView {
    DWORD pid = 0;
    std::wstring_view name;
    SIZE_T workingSet = 0;
    ULONGLONG processCreated = 0;
};

namespace detail {
inline constexpr std::size_t maxRows = 65536;
class LastErrorScope {
    DWORD saved_ = GetLastError();
public:
    ~LastErrorScope() { SetLastError(saved_); }
    LastErrorScope() = default;
    LastErrorScope(const LastErrorScope&) = delete;
    LastErrorScope& operator=(const LastErrorScope&) = delete;
};

// Keep recursive detail requests from waiting on this Store's nonrecursive
// mutex. The game wrapper additionally leases its views for its full lifetime.
class DetailReadScope {
    inline static thread_local bool active_ = false;
    bool entered_ = false;
public:
    DetailReadScope() noexcept : entered_(!active_) { if (entered_) active_ = true; }
    ~DetailReadScope() { if (entered_) active_ = false; }
    bool entered() const noexcept { return entered_; }
    DetailReadScope(const DetailReadScope&) = delete;
    DetailReadScope& operator=(const DetailReadScope&) = delete;
};
inline void failDetails(DetailSnapshot& snapshot, DWORD error) {
    snapshot.status = Status::Failed;
    snapshot.error = error ? error : ERROR_GEN_FAILURE;
    snapshot.creationTimesVerified = false;
    snapshot.rows.clear();
}
inline std::shared_ptr<const DetailSnapshot> failedDetailsReceipt(
    Query query, ULONGLONG now, DWORD error) {
    // Allocation failure of the failure receipt itself may escape, as in read().
    auto receipt = std::make_shared<DetailSnapshot>();
    receipt->observedAt = now == currentTime ? GetTickCount64() : now;
    failDetails(*receipt, error);
    if (!query) receipt->status = Status::Unavailable;
    return receipt;
}

inline bool parseDetailRows(const void* data, std::size_t bytes, std::vector<DetailView>& rows) {
    rows.clear();
    if (!data || bytes < sizeof(SYSTEM_PROCESS_INFORMATION)) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(data);
    if (bytes > (std::numeric_limits<std::uintptr_t>::max)() - base) return false;
    std::size_t offset = 0;
    for (;;) {
        if (bytes - offset < sizeof(SYSTEM_PROCESS_INFORMATION)) { rows.clear(); return false; }
        SYSTEM_PROCESS_INFORMATION record{};
        std::memcpy(&record, static_cast<const unsigned char*>(data) + offset, sizeof(record));
        const std::size_t extent = record.NextEntryOffset ? record.NextEntryOffset : bytes - offset;
        if (extent < sizeof(record) || extent > bytes - offset ||
            (record.NextEntryOffset && extent % alignof(SYSTEM_PROCESS_INFORMATION)) ||
            record.NumberOfThreads > (extent - sizeof(record)) / sizeof(SYSTEM_THREAD_INFORMATION)) {
            rows.clear(); return false;
        }
        const auto pid = reinterpret_cast<std::uintptr_t>(record.UniqueProcessId);
        const auto name = reinterpret_cast<std::uintptr_t>(record.ImageName.Buffer);
        const auto length = static_cast<std::size_t>(record.ImageName.Length);
        const auto capacity = static_cast<std::size_t>(record.ImageName.MaximumLength);
        const auto low = base + offset + sizeof(record), high = base + offset + extent;
        if (pid > (std::numeric_limits<DWORD>::max)() || length % sizeof(wchar_t) ||
            capacity % sizeof(wchar_t) || length > capacity ||
            (name && (name < low || name > high || capacity > high - name || name % alignof(wchar_t))) ||
            (!name && (length || capacity))) {
            rows.clear(); return false;
        }
        // The SPI time prefix is a compatibility hint, never an unchecked
        // identity authority. The owner verifies this layout against the current
        // process GetProcessTimes value before retaining any creation times.
        constexpr std::size_t creationOffset = 32;
        static_assert(offsetof(SYSTEM_PROCESS_INFORMATION, ImageName) >= creationOffset + sizeof(LARGE_INTEGER));
        LARGE_INTEGER created{};
        std::memcpy(&created, reinterpret_cast<const unsigned char*>(&record) + creationOffset, sizeof(created));
        rows.push_back({static_cast<DWORD>(pid), name ? std::wstring_view(
            record.ImageName.Buffer, length / sizeof(wchar_t)) : std::wstring_view{}, record.WorkingSetSize,
            created.QuadPart > 0 ? static_cast<ULONGLONG>(created.QuadPart) : 0});
        if (!record.NextEntryOffset) return true;
        offset += extent;
    }
}

// Use memcpy for the fixed prefix; validate record offsets and the actual
// WCHAR pointer alignment before dereferencing any borrowed string bytes.
inline bool parseBasicRows(const void* data, std::size_t bytes,
    std::vector<Entry>& rows, DWORD& error) {
    rows.clear();
    error = ERROR_INVALID_DATA;
    const auto fail = [&] { rows.clear(); return false; };
    try {
        if (!data || bytes < sizeof(SYSTEM_BASICPROCESS_INFORMATION)) return fail();
        const auto base = reinterpret_cast<std::uintptr_t>(data);
        if (bytes > (std::numeric_limits<std::uintptr_t>::max)() - base) return fail();
        std::size_t offset = 0;
        for (;;) {
            const auto remaining = bytes - offset;
            if (remaining < sizeof(SYSTEM_BASICPROCESS_INFORMATION) || rows.size() >= maxRows)
                return fail();
            SYSTEM_BASICPROCESS_INFORMATION record{};
            std::memcpy(&record, static_cast<const unsigned char*>(data) + offset, sizeof(record));
            const std::size_t extent = record.NextEntryOffset ? record.NextEntryOffset : remaining;
            if (extent < sizeof(record) || extent > remaining ||
                (record.NextEntryOffset && extent % alignof(SYSTEM_BASICPROCESS_INFORMATION)))
                return fail();
            const auto pid = reinterpret_cast<std::uintptr_t>(record.UniqueProcessId);
            const auto parentPid = reinterpret_cast<std::uintptr_t>(record.InheritedFromUniqueProcessId);
            const auto name = reinterpret_cast<std::uintptr_t>(record.ImageName.Buffer);
            const auto length = static_cast<std::size_t>(record.ImageName.Length);
            const auto capacity = static_cast<std::size_t>(record.ImageName.MaximumLength);
            const auto low = base + offset + sizeof(record), high = base + offset + extent;
            if (pid > (std::numeric_limits<DWORD>::max)() ||
                parentPid > (std::numeric_limits<DWORD>::max)() ||
                length % sizeof(wchar_t) || capacity % sizeof(wchar_t) || length > capacity ||
                (name && (name < low || name > high || capacity > high - name || name % alignof(wchar_t))) ||
                (!name && (length || capacity)))
                return fail();
            Entry entry;
            entry.pid = static_cast<DWORD>(pid);
            entry.parentPid = static_cast<DWORD>(parentPid);
            entry.sequence = record.SequenceNumber;
            if (length) entry.name.assign(record.ImageName.Buffer, length / sizeof(wchar_t));
            rows.push_back(std::move(entry));
            if (!record.NextEntryOffset) { error = ERROR_SUCCESS; return true; }
            offset += extent;
        }
    } catch (const std::bad_alloc&) {
        error = ERROR_NOT_ENOUGH_MEMORY;
    } catch (...) {
        error = ERROR_GEN_FAILURE;
    }
    return fail();
}

inline void fail(Snapshot& snapshot, DWORD error) {
    snapshot.status = Status::Failed;
    snapshot.error = error ? error : ERROR_GEN_FAILURE;
    snapshot.rows.clear();
    snapshot.rejectionHints.clear();
}
class SnapshotHandle {
    HANDLE value_;
public:
    explicit SnapshotHandle(HANDLE value) : value_(value) {}
    ~SnapshotHandle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    SnapshotHandle(const SnapshotHandle&) = delete;
    SnapshotHandle& operator=(const SnapshotHandle&) = delete;
    HANDLE get() const { return value_; }
};
inline std::shared_ptr<const Snapshot> failedReceipt(Query query, ULONGLONG now, DWORD error) {
    // If allocation of the failure receipt itself fails, bad_alloc may escape.
    auto receipt = std::make_shared<Snapshot>();
    receipt->basicStatus = query ? Status::Failed : Status::Unavailable;
    receipt->observedAt = now == currentTime ? GetTickCount64() : now;
    fail(*receipt, error);
    return receipt;
}
} // namespace detail

inline Query resolveQuery() {
    detail::LastErrorScope restore;
    static const auto query = reinterpret_cast<Query>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    return query;
}
inline bool parseBasic(const void* data, std::size_t bytes, std::vector<Entry>& rows) {
    DWORD error = ERROR_SUCCESS;
    return detail::parseBasicRows(data, bytes, rows, error);
}

inline bool parseDetails(const void* data, std::size_t bytes, std::vector<DetailView>& rows) {
    return detail::parseDetailRows(data, bytes, rows);
}

class Store {
    std::mutex mutex_;
    std::shared_ptr<const Snapshot> cached_;
    Query cachedQuery_ = nullptr;
    std::atomic<std::uint64_t> epoch_{0};
    std::uint64_t cachedEpoch_ = 0, generation_ = 0;
    std::vector<unsigned char> scratch_;
    // The topology fields and read() below keep their original behavior.
    // Both channels share mutex_/epoch_; detailed failure never writes topology.
    std::shared_ptr<const DetailSnapshot> detailsCached_;
    Query detailsCachedQuery_ = nullptr;
    std::uint64_t detailsCachedEpoch_ = 0, detailsGeneration_ = 0;
    std::vector<unsigned char> detailsScratch_;
    std::vector<DetailView> detailRows_;

    static void toolhelpFailure(Snapshot& snapshot, DWORD error) {
        // Original name-only rejection consumers may already have seen this
        // valid Toolhelp prefix. Keep those facts separately, never as a census.
        auto prefix=std::move(snapshot.rows);
        detail::fail(snapshot,error);
        snapshot.rejectionHints.reserve(prefix.size());
        for(auto& row:prefix)snapshot.rejectionHints.push_back({row.pid,std::move(row.name)});
    }
    static void toolhelp(Snapshot& snapshot) {
        snapshot.basicStatus = Status::Unavailable;
        detail::SnapshotHandle handle(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (!handle.get() || handle.get() == INVALID_HANDLE_VALUE) {
            detail::fail(snapshot, GetLastError()); return;
        }
        PROCESSENTRY32W row{};
        row.dwSize = sizeof(row);
        SetLastError(ERROR_SUCCESS);
        BOOL more = Process32FirstW(handle.get(), &row);
        for (;;) {
            if (!more) {
                const DWORD error = GetLastError();
                if (error != ERROR_NO_MORE_FILES) { toolhelpFailure(snapshot, error); return; }
                snapshot.status = Status::Complete;
                snapshot.error = ERROR_SUCCESS;
                return;
            }
            if (snapshot.rows.size() >= detail::maxRows) {
                toolhelpFailure(snapshot, ERROR_MORE_DATA); return;
            }
            const auto end = row.szExeFile + sizeof(row.szExeFile) / sizeof(row.szExeFile[0]);
            const auto terminator = std::find(row.szExeFile, end, L'\0');
            if (terminator == end) { toolhelpFailure(snapshot, ERROR_INVALID_DATA); return; }
            Entry entry;
            entry.pid = row.th32ProcessID; // PID 0 is a valid topology row.
            entry.parentPid = row.th32ParentProcessID;
            entry.name.assign(row.szExeFile, terminator);
            snapshot.rows.push_back(std::move(entry)); // Toolhelp sequence stays 0.
            row.dwSize = sizeof(row);
            SetLastError(ERROR_SUCCESS);
            more = Process32NextW(handle.get(), &row);
        }
    }
    void produce(Snapshot& snapshot, Query query, bool allowToolhelp) {
        if (!query) {
            if (allowToolhelp) toolhelp(snapshot);
            else { snapshot.status = snapshot.basicStatus = Status::Unavailable; snapshot.error = ERROR_NOT_SUPPORTED; }
            return;
        }
        snapshot.basicStatus = Status::Failed;
        constexpr std::size_t initialBytes = 64 * 1024, maximumBytes = 16 * 1024 * 1024;
        if (scratch_.empty()) scratch_.resize(initialBytes);
        for (unsigned attempt = 0; attempt < 4; ++attempt) {
            ULONG returned = 0;
            const NTSTATUS status = query(static_cast<SYSTEM_INFORMATION_CLASS>(252),
                scratch_.data(), static_cast<ULONG>(scratch_.size()), &returned);
            if (status >= 0) {
                if (!returned || returned > scratch_.size()) {
                    detail::fail(snapshot, ERROR_INVALID_DATA); return;
                }
                DWORD error = ERROR_SUCCESS;
                if (!detail::parseBasicRows(scratch_.data(), returned, snapshot.rows, error)) {
                    detail::fail(snapshot, error); return;
                }
                snapshot.status = snapshot.basicStatus = Status::Complete;
                snapshot.error = ERROR_SUCCESS;
                return;
            }
            if (status == static_cast<NTSTATUS>(0xC0000003L) || // INVALID_INFO_CLASS
                status == static_cast<NTSTATUS>(0xC0000002L) || // NOT_IMPLEMENTED
                status == static_cast<NTSTATUS>(0xC00000BBL)) { // NOT_SUPPORTED
                snapshot.basicStatus = Status::Unavailable;
                if (allowToolhelp) toolhelp(snapshot);
                else { snapshot.status = Status::Unavailable; snapshot.error = ERROR_NOT_SUPPORTED; }
                return;
            }
            if (status != static_cast<NTSTATUS>(0xC0000004L) && // INFO_LENGTH_MISMATCH
                status != static_cast<NTSTATUS>(0xC0000023L)) { // BUFFER_TOO_SMALL
                using ToDos = ULONG (NTAPI*)(NTSTATUS);
                static const auto toDos = reinterpret_cast<ToDos>(
                    GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlNtStatusToDosError"));
                detail::fail(snapshot, toDos ? toDos(status) : ERROR_GEN_FAILURE);
                return;
            }
            const auto desired = (std::max)(scratch_.size() * 2, static_cast<std::size_t>(returned));
            if (desired > maximumBytes || attempt == 3) break;
            scratch_.resize(desired);
        }
        detail::fail(snapshot, ERROR_INSUFFICIENT_BUFFER);
    }
    void produceDetails(DetailSnapshot& snapshot, Query query) {
        detailRows_.clear();
        if (!query) {
            snapshot.status = Status::Unavailable;
            snapshot.error = ERROR_NOT_SUPPORTED;
            return;
        }
        // Original class5 limits and growth: 1 MiB, 64 MiB, four attempts,
        // max(double capacity, returned + 64 KiB); only the two size statuses retry.
        constexpr std::size_t initialBytes = 1024 * 1024, maxBytes = 64 * 1024 * 1024;
        if (detailsScratch_.empty()) detailsScratch_.resize(initialBytes);
        for (unsigned attempt = 0; attempt != 4; ++attempt) {
            ULONG returned = 0;
            const NTSTATUS status = query(SystemProcessInformation, detailsScratch_.data(),
                static_cast<ULONG>(detailsScratch_.size()), &returned);
            if (status >= 0) {
                if (!returned || returned > detailsScratch_.size() ||
                    !detail::parseDetailRows(detailsScratch_.data(), returned, detailRows_)) {
                    detail::failDetails(snapshot, ERROR_INVALID_DATA);
                    return;
                }
                FILETIME born{}, exited{}, kernel{}, user{};
                if (GetProcessTimes(GetCurrentProcess(), &born, &exited, &kernel, &user)) {
                    const ULONGLONG selfCreated = (static_cast<ULONGLONG>(born.dwHighDateTime) << 32) | born.dwLowDateTime;
                    const DWORD selfPid = GetCurrentProcessId();
                    const auto self = std::find_if(detailRows_.begin(), detailRows_.end(),
                        [&](const DetailView& entry) { return entry.pid == selfPid; });
                    snapshot.creationTimesVerified = selfCreated && self != detailRows_.end() && self->processCreated == selfCreated;
                }
                if (!snapshot.creationTimesVerified)
                    for (auto& entry : detailRows_) entry.processCreated = 0;
                snapshot.rows.reserve(detailRows_.size());
                for (const auto& entry : detailRows_)
                    snapshot.rows.push_back({entry.pid, std::wstring(entry.name), entry.workingSet, entry.processCreated});
                detailRows_.clear();
                snapshot.status = Status::Complete;
                snapshot.error = ERROR_SUCCESS;
                return;
            }
            if (status != static_cast<NTSTATUS>(0xC0000004u) &&
                status != static_cast<NTSTATUS>(0xC0000023u)) {
                detail::failDetails(snapshot, ERROR_GEN_FAILURE);
                return;
            }
            const auto desired = (std::max)(detailsScratch_.size() * 2,
                static_cast<std::size_t>(returned) + 65536);
            if (desired > maxBytes || attempt == 3) {
                detail::failDetails(snapshot, ERROR_INSUFFICIENT_BUFFER);
                return;
            }
            detailsScratch_.resize(desired);
        }
        detail::failDetails(snapshot, ERROR_INSUFFICIENT_BUFFER);
    }
public:
    Store() = default;
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    std::shared_ptr<const Snapshot> read(Query query = resolveQuery(),
        ULONGLONG now = currentTime, ULONGLONG maxAge = 250, bool allowToolhelp = true) {
        detail::LastErrorScope restore;
        std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
        try { lock.lock(); }
        catch (const std::bad_alloc&) { return detail::failedReceipt(query, now, ERROR_NOT_ENOUGH_MEMORY); }
        catch (...) { return detail::failedReceipt(query, now, ERROR_GEN_FAILURE); }
        if (now == currentTime) now = GetTickCount64();
        const auto ticket = epoch_.load(std::memory_order_acquire);
        const auto lifetime = (std::min)(maxAge, ULONGLONG{250});
        if (cached_ && cachedQuery_ == query && cachedEpoch_ == ticket &&
            now >= cached_->observedAt && now - cached_->observedAt < lifetime &&
            (!allowToolhelp || cached_->status != Status::Unavailable))
            return cached_;

        // A failed fresh request must never fall back to an older success.
        // Existing readers keep their own immutable, owning shared_ptr.
        cached_.reset();
        auto snapshot = std::make_shared<Snapshot>();
        snapshot->observedAt = now;
        snapshot->generation = ++generation_;
        snapshot->basicStatus = query ? Status::Failed : Status::Unavailable;
        try { produce(*snapshot, query, allowToolhelp); }
        catch (const std::bad_alloc&) { detail::fail(*snapshot, ERROR_NOT_ENOUGH_MEMORY); }
        catch (...) { detail::fail(*snapshot, ERROR_GEN_FAILURE); }
        cachedQuery_ = query;
        cachedEpoch_ = ticket;
        cached_ = std::move(snapshot);
        return cached_;
    }
    std::shared_ptr<const DetailSnapshot> readDetails(Query query = resolveQuery(),
        ULONGLONG now = currentTime, ULONGLONG maxAge = 250) {
        detail::LastErrorScope restore;
        detail::DetailReadScope active;
        if (!active.entered()) return detail::failedDetailsReceipt(query, now, ERROR_BUSY);
        std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
        try { lock.lock(); }
        catch (const std::bad_alloc&) { return detail::failedDetailsReceipt(query, now, ERROR_NOT_ENOUGH_MEMORY); }
        catch (...) { return detail::failedDetailsReceipt(query, now, ERROR_GEN_FAILURE); }
        if (now == currentTime) now = GetTickCount64();
        const auto ticket = epoch_.load(std::memory_order_acquire);
        const auto lifetime = (std::min)(maxAge, ULONGLONG{250});
        if (detailsCached_ && detailsCachedQuery_ == query && detailsCachedEpoch_ == ticket &&
            now >= detailsCached_->observedAt && now - detailsCached_->observedAt < lifetime)
            return detailsCached_;

        // A fresh failure returns only this failed epoch, never an older success.
        // Each receipt owns strings, independently of either channel's scratch.
        detailsCached_.reset();
        auto snapshot = std::make_shared<DetailSnapshot>();
        snapshot->observedAt = now;
        snapshot->generation = ++detailsGeneration_;
        try { produceDetails(*snapshot, query); }
        catch (const std::bad_alloc&) { detailRows_.clear(); detail::failDetails(*snapshot, ERROR_NOT_ENOUGH_MEMORY); }
        catch (...) { detailRows_.clear(); detail::failDetails(*snapshot, ERROR_GEN_FAILURE); }
        detailsCachedQuery_ = query;
        detailsCachedEpoch_ = ticket;
        detailsCached_ = std::move(snapshot);
        return detailsCached_;
    }
    void invalidate() noexcept { epoch_.fetch_add(1, std::memory_order_release); }
};
inline Store& owner() { static Store store; return store; }
inline std::shared_ptr<const Snapshot> read(Query query = resolveQuery(),
    ULONGLONG now = currentTime, ULONGLONG maxAge = 250, bool allowToolhelp = true) {
    detail::LastErrorScope restore;
    return owner().read(query, now, maxAge, allowToolhelp);
}
inline std::shared_ptr<const DetailSnapshot> readDetails(Query query = resolveQuery(),
    ULONGLONG now = currentTime, ULONGLONG maxAge = 250) {
    detail::LastErrorScope restore;
    return owner().readDetails(query, now, maxAge);
}
inline void invalidate() noexcept { owner().invalidate(); }
} // namespace ymcc::processInventory
