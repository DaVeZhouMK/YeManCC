#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string_view>

namespace ymcc::monitorGameRules {
// A notification is only a wake hint. The monitor cannot mint a new empty
// observation while it borrows a previous one, even with a quiet watch.
struct IdleState {
    ULONGLONG generation = 0, observedAt = 0, topologyDue = 0;
    ULONGLONG ruleEpoch = 0, power = 0, inventory = 0;
    HWND foreground = nullptr;
    bool completedEmpty = false;
    bool canReuse(const IdleState& current, ULONGLONG now) const noexcept {
        return completedEmpty && current.completedEmpty && generation && generation == current.generation &&
            observedAt && observedAt == current.observedAt && now >= observedAt && now - observedAt < 30000 &&
            topologyDue && now < topologyDue && ruleEpoch && ruleEpoch == current.ruleEpoch &&
            power == current.power && inventory == current.inventory && foreground == current.foreground;
    }
};

enum class Notice { Quiet, Dirty, Unavailable };
class Watch {
    HANDLE dir_ = INVALID_HANDLE_VALUE, event_ = nullptr;
    OVERLAPPED operation_{};
    alignas(DWORD) std::array<std::byte, 8192> buffer_{};
    ULONGLONG retryAt_ = 0;
    bool pending_ = false;

    bool arm() noexcept {
        if (!ResetEvent(event_)) return false;
        operation_ = {};
        operation_.hEvent = event_;
        pending_ = ReadDirectoryChangesW(dir_, buffer_.data(), static_cast<DWORD>(buffer_.size()), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SECURITY,
            nullptr, &operation_, nullptr) != FALSE;
        return pending_;
    }
    void close() noexcept {
        if (pending_) {
            CancelIoEx(dir_, &operation_);
            DWORD ignored = 0;
            GetOverlappedResult(dir_, &operation_, &ignored, TRUE);
            pending_ = false;
        }
        if (dir_ != INVALID_HANDLE_VALUE) CloseHandle(dir_);
        if (event_) CloseHandle(event_);
        dir_ = INVALID_HANDLE_VALUE;
        event_ = nullptr;
    }
    Notice failed(ULONGLONG now) noexcept {
        close();
        retryAt_ = now + 5000;
        return Notice::Unavailable;
    }
public:
    Watch() = default;
    ~Watch() { close(); }
    Watch(const Watch&) = delete;
    Watch& operator=(const Watch&) = delete;
    void reset() noexcept { close(); retryAt_ = 0; }

    static bool relevant(const std::byte* data, DWORD bytes,
        std::initializer_list<std::wstring_view> names) noexcept {
        constexpr auto header = offsetof(FILE_NOTIFY_INFORMATION, FileName);
        if (!bytes) return true; // Overflow or unobservable changes.
        for (std::size_t offset = 0;;) {
            const auto left = bytes - offset;
            if (left < header) return true;
            DWORD next = 0, action = 0, length = 0;
            std::memcpy(&next, data + offset, sizeof(next));
            std::memcpy(&action, data + offset + sizeof(DWORD), sizeof(action));
            std::memcpy(&length, data + offset + 2 * sizeof(DWORD), sizeof(length));
            if (action < FILE_ACTION_ADDED || action > FILE_ACTION_RENAMED_NEW_NAME || !length ||
                length % sizeof(wchar_t) || length > left - header) return true;
            auto value = reinterpret_cast<const wchar_t*>(data + offset + header);
            for (const auto name : names) {
                const auto ordering = CompareStringOrdinal(value, static_cast<int>(length / sizeof(wchar_t)),
                    name.data(), static_cast<int>(name.size()), TRUE);
                if (!ordering || ordering == CSTR_EQUAL) return true;
            }
            // A short-name notification cannot prove it refers to a sibling.
            for (std::size_t i = 0; i < length / sizeof(wchar_t); ++i)
                if (value[i] == L'~') return true;
            if (!next) return false;
            if (next % alignof(DWORD) || next < header + length || next >= left) return true;
            offset += next;
        }
    }

    Notice check(const wchar_t* root, ULONGLONG now,
        std::initializer_list<std::wstring_view> names) noexcept {
        if (dir_ == INVALID_HANDLE_VALUE) {
            if (now < retryAt_) return Notice::Unavailable;
            const DWORD attrs = GetFileAttributesW(root);
            if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) ||
                (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return failed(now);
            dir_ = CreateFileW(root, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (dir_ == INVALID_HANDLE_VALUE) return failed(now);
            FILE_ATTRIBUTE_TAG_INFO info{};
            if (!GetFileInformationByHandleEx(dir_, FileAttributeTagInfo, &info, sizeof(info)) ||
                !(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return failed(now);
            event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!event_ || !arm()) return failed(now);
            return Notice::Dirty;
        }
        const auto state = WaitForSingleObject(event_, 0);
        if (state == WAIT_TIMEOUT) return Notice::Quiet;
        if (state != WAIT_OBJECT_0) return failed(now);
        DWORD bytes = 0;
        if (!GetOverlappedResult(dir_, &operation_, &bytes, FALSE)) {
            if (GetLastError() == ERROR_IO_INCOMPLETE) return Notice::Unavailable;
            pending_ = false;
            return failed(now);
        }
        pending_ = false;
        const bool dirty = bytes > buffer_.size() || relevant(buffer_.data(), bytes, names);
        if (!arm()) return failed(now);
        return dirty ? Notice::Dirty : Notice::Quiet;
    }
};
}
