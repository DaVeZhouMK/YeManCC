#pragma once
#include <windows.h>
#include <string>

namespace ymcc::steamsession {
// One event and key handle per registration keep one-shot filters independent.
class RegistryWatch {
    const std::wstring path_;
    const DWORD exactFilter_;
    const bool exactOnly_;
    HANDLE event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HKEY key_ = nullptr;
    bool exact_ = false;
    bool subtree_ = false;
    bool fallbackNames_ = false;

    void closeKey() noexcept {
        if (key_) { RegCloseKey(key_); key_ = nullptr; }
        if (event_) ResetEvent(event_);
    }
    bool subscribe(HKEY key, bool exact) const noexcept {
        const bool recursive = !exact || subtree_;
        const DWORD filter = recursive ? REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_CHANGE_NAME :
            exactFilter_ | (fallbackNames_ ? REG_NOTIFY_CHANGE_NAME : 0);
        return RegNotifyChangeKeyValue(key, recursive, filter, event_, TRUE) == ERROR_SUCCESS;
    }
public:
    explicit RegistryWatch(const std::wstring& path, DWORD exactFilter = REG_NOTIFY_CHANGE_LAST_SET,
                           bool exactOnly = false)
        : path_(path), exactFilter_(exactFilter), exactOnly_(exactOnly) {}
    RegistryWatch(const RegistryWatch&) = delete;
    RegistryWatch& operator=(const RegistryWatch&) = delete;
    ~RegistryWatch() { closeKey(); if (event_) CloseHandle(event_); }
    HANDLE waitHandle() const noexcept { return key_ ? event_ : nullptr; }
    bool exactArmed() const noexcept { return key_ && exact_; }
    void rebind() { closeKey(); arm(); }
    void armWithChildFallback(bool subtree, bool names) {
        if (subtree_ != subtree || fallbackNames_ != names) {
            subtree_ = subtree; fallbackNames_ = names; closeKey();
        }
        if (!key_) arm();
    }

    // Call at startup, after consuming this watch's signal, or when unarmed.
    void arm() {
        if (!event_) return;
        if (exact_ && key_ && subscribe(key_, true)) return;
        closeKey();
        std::wstring candidate = path_;
        bool exact = true, missingExact = false;
        while (!candidate.empty() && (exact || !exactOnly_)) {
            HKEY opened = nullptr;
            const auto error = RegOpenKeyExW(HKEY_CURRENT_USER, candidate.c_str(), 0, KEY_NOTIFY, &opened);
            if (error == ERROR_SUCCESS) {
                if (subscribe(opened, exact)) { key_ = opened; exact_ = exact; break; }
                RegCloseKey(opened);
                ResetEvent(event_);
            } else if (exact) {
                missingExact = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
            }
            const auto slash = candidate.find_last_of(L'\\');
            if (slash == std::wstring::npos) break;
            candidate.resize(slash);
            exact = false;
        }
        // Cover creation between the first open and the ancestor registration.
        // The already armed ancestor covers changes after this check.
        HKEY created = nullptr;
        if (key_ && !exact_ && missingExact &&
            RegOpenKeyExW(HKEY_CURRENT_USER, path_.c_str(), 0, KEY_NOTIFY, &created) == ERROR_SUCCESS) {
            RegCloseKey(created);
            SetEvent(event_);
        }
    }
};
}
