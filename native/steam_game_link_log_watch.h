#pragma once
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace ymcc::steamlink::logs {
enum Change : unsigned { Game = 1, Console = 2, Connection = 4, All = Game|Console|Connection };
inline constexpr std::array<std::wstring_view,3> names{
    L"gameprocess_log.txt", L"console_log.txt", L"connection_log.txt"};

inline unsigned changed(const std::byte* data, DWORD bytes) noexcept {
    constexpr auto header = offsetof(FILE_NOTIFY_INFORMATION, FileName);
    if (!bytes) return All; // The directory buffer overflowed.
    unsigned mask = 0;
    for (std::size_t offset = 0;;) {
        const auto left = bytes - offset;
        if (left < header) return All;
        DWORD next = 0, action = 0, length = 0;
        std::memcpy(&next, data + offset, sizeof(next));
        std::memcpy(&action, data + offset + sizeof(DWORD), sizeof(action));
        std::memcpy(&length, data + offset + 2 * sizeof(DWORD), sizeof(length));
        if (action < FILE_ACTION_ADDED || action > FILE_ACTION_RENAMED_NEW_NAME ||
            !length || length % sizeof(wchar_t) || length > left - header) return All;
        const std::wstring_view path(reinterpret_cast<const wchar_t*>(data + offset + header), length / sizeof(wchar_t));
        // Windows can report the short spelling of a changed name.
        if (path.find(L'~') != path.npos || path.find(L'\0') != path.npos) return All;
        for (unsigned i = 0; i < names.size(); ++i) {
            if (path.size() != names[i].size()) continue;
            const int result = CompareStringOrdinal(path.data(), static_cast<int>(path.size()),
                names[i].data(), static_cast<int>(names[i].size()), TRUE);
            if (!result) return All;
            if (result == CSTR_EQUAL) mask |= 1u << i;
        }
        if (!next) return left - header - length < alignof(DWORD) ? mask : All;
        if (next % alignof(DWORD) || next < header + length || next >= left) return All;
        offset += next;
    }
}

class Watch {
    inline static constexpr DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;
    HANDLE directory_ = INVALID_HANDLE_VALUE, event_ = nullptr, fallback_ = nullptr;
    OVERLAPPED io_{};
    alignas(DWORD) std::array<std::byte,8192> buffer_{};
    bool pending_ = false;

    bool arm() noexcept {
        if (!ResetEvent(event_)) return false;
        io_ = {};
        io_.hEvent = event_;
        pending_ = ReadDirectoryChangesW(directory_, buffer_.data(), static_cast<DWORD>(buffer_.size()),
            FALSE, filter, nullptr, &io_, nullptr) != FALSE;
        return pending_;
    }
    bool openFallback(const std::wstring& path) noexcept {
        const auto legacy = FindFirstChangeNotificationW(path.c_str(), FALSE, filter);
        if (legacy == INVALID_HANDLE_VALUE) return false;
        fallback_ = legacy;
        return true;
    }
public:
    Watch() = default;
    ~Watch() { close(); }
    Watch(const Watch&) = delete;
    Watch& operator=(const Watch&) = delete;

    void close() noexcept {
        if (pending_) {
            CancelIoEx(directory_, &io_);
            DWORD ignored = 0;
            GetOverlappedResult(directory_, &io_, &ignored, TRUE);
            pending_ = false;
        }
        if (directory_ != INVALID_HANDLE_VALUE) CloseHandle(directory_);
        if (event_) CloseHandle(event_);
        if (fallback_) FindCloseChangeNotification(fallback_);
        directory_ = INVALID_HANDLE_VALUE;
        event_ = fallback_ = nullptr;
    }
    bool open(const std::wstring& path) noexcept {
        close();
        directory_ = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (directory_ != INVALID_HANDLE_VALUE) {
            event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (event_ && arm()) return true;
        }
        close();
        return openFallback(path);
    }
    HANDLE handle() const noexcept { return fallback_ ? fallback_ : event_; }
    // Called after the existing debounce. Rearm before any timestamp reads,
    // so writes arriving during those reads remain pending for the next batch.
    bool consume(const std::wstring& path, unsigned& mask) noexcept {
        mask = All;
        if (fallback_) return FindNextChangeNotification(fallback_) != FALSE;
        if (!pending_) return false;
        DWORD bytes = 0;
        if (GetOverlappedResult(directory_, &io_, &bytes, FALSE)) {
            pending_ = false;
            const auto observed = bytes <= buffer_.size() ? changed(buffer_.data(), bytes) : All;
            if (arm()) { mask = observed; return true; }
        } else {
            const auto error = GetLastError();
            if (error != ERROR_IO_INCOMPLETE) pending_ = false;
            if (error == ERROR_NOTIFY_ENUM_DIR && arm()) return true;
        }
        close();
        return openFallback(path);
    }
};
} // namespace ymcc::steamlink::logs
