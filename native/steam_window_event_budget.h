#pragma once
// Windows UI events are hints, never permission to launch or switch Steam.
#include <windows.h>
#include <algorithm>
#include <map>
#include <string>
#include <utility>

namespace ymcc::steamsession {
// One finite trailing delivery for the entire fan-out (WebView/mode/game link).
// Subsequent hints do not postpone it. No timer exists while nothing is pending.
class WindowHintWindow {
    ULONGLONG due_ = 0;
    bool pending_ = false;
public:
    void request(ULONGLONG now) { if (!pending_) { pending_ = true; due_ = now + 200; } }
    DWORD timeout(ULONGLONG now) const {
        return !pending_ ? INFINITE : now >= due_ ? 0 : static_cast<DWORD>(due_ - now);
    }
    bool take(ULONGLONG now) { if (timeout(now)) return false; pending_ = false; return true; }
    void clear() { pending_ = false; }
};
// Pin the process lifetime instead of querying its image for every CREATE.
// A signaled handle invalidates the entry immediately, even if its PID is reused.
// Unknown/denied processes have only a short negative entry; later events retry.
// This cache filters notifications ONLY. Action paths retain full verification.
class WindowProcessCache {
    struct Entry {
        HANDLE process = nullptr;
        ULONGLONG until = 0, order = 0;
        unsigned kind = 0;
        Entry() = default;
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
        ~Entry() { if (process) CloseHandle(process); }
    };
    std::map<DWORD, Entry> entries_;
    ULONGLONG serial_ = 0;
public:
    static constexpr size_t capacity = 256;
    void clear() { entries_.clear(); serial_ = 0; }
    template<class Classify> unsigned classify(DWORD pid, ULONGLONG now, Classify classifyPath) {
        if (auto it = entries_.find(pid); it != entries_.end()) {
            if (it->second.process ? WaitForSingleObject(it->second.process, 0) == WAIT_TIMEOUT : now < it->second.until)
                return it->second.kind;
            entries_.erase(it);
        }
        if (entries_.size() >= capacity) {
            const auto oldest = std::min_element(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
                return a.second.order < b.second.order;
            });
            entries_.erase(oldest); // Evict one entry, not every known PID.
        }
        auto& entry = entries_.try_emplace(pid).first->second;
        entry.until = now + 1000; entry.order = ++serial_;
        entry.process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
        if (!entry.process) return 0;
        wchar_t image[32768]; DWORD length = static_cast<DWORD>(std::size(image));
        if (!QueryFullProcessImageNameW(entry.process, 0, image, &length) || WaitForSingleObject(entry.process, 0) != WAIT_TIMEOUT) {
            CloseHandle(entry.process); entry.process = nullptr; return 0;
        }
        entry.kind = classifyPath(std::wstring(image, length));
        return entry.kind;
    }
};
}
