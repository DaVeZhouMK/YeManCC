#pragma once
// Shared, event-driven Steam session observer. No WMI polling, no recurring timer.
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cwctype>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace ymcc::steamsession {
struct Session {
    std::wstring root;
    DWORD pid = 0, account = 0;
    ULONGLONG created = 0; // PID reuse is a new session, too.
    bool operator==(const Session&) const = default;
};
// Only a genuine session change or explicit user action opens a new budget.
// A failed attempt / unrelated registry write must NEVER replenish it.
class RetryWindow {
    static constexpr std::array<ULONGLONG, 4> offsets{1000, 3000, 8000, 20000};
    ULONGLONG opened = 0;
    size_t next = offsets.size();
public:
    void begin(ULONGLONG now) { opened = now; next = 0; }
    DWORD timeout(ULONGLONG now) const {
        if (next == offsets.size()) return INFINITE;
        const auto due = opened + offsets[next];
        return now >= due ? 0 : static_cast<DWORD>(due - now);
    }
    bool take(ULONGLONG now) {
        if (timeout(now) != 0) return false;
        // IO/standby can run past multiple deadlines: one attempt, not a burst.
        do { ++next; } while (next < offsets.size() && opened + offsets[next] <= now);
        return true;
    }
};
inline DWORD regNumber(const std::wstring& keyPath, const wchar_t* name) {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, (keyPath + L"\\ActiveProcess").c_str(), name,
        RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) return 0;
    return value;
}
inline std::wstring steamRoot(const std::wstring& keyPath) {
    wchar_t value[32768]{}; DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, keyPath.c_str(), L"SteamPath",
        RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) return {};
    std::wstring root(value);
    std::replace(root.begin(), root.end(), L'/', L'\\');
    std::transform(root.begin(), root.end(), root.begin(), [](wchar_t c) { return std::towlower(c); });
    while (!root.empty() && root.back() == L'\\') root.pop_back();
    return root;
}
inline HANDLE verifiedProcess(DWORD pid, const std::wstring& root, ULONGLONG* created) {
    if (!pid || root.empty()) return nullptr;
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return nullptr;
    wchar_t image[32768]; DWORD length = static_cast<DWORD>(std::size(image));
    FILETIME birth{}, exit{}, kernel{}, user{};
    bool ok = QueryFullProcessImageNameW(h, 0, image, &length) && GetProcessTimes(h, &birth, &exit, &kernel, &user) &&
        WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    if (ok) {
        std::wstring path(image, length);
        std::replace(path.begin(), path.end(), L'/', L'\\');
        ok = _wcsicmp(path.c_str(), (root + L"\\steam.exe").c_str()) == 0;
    }
    if (!ok) { CloseHandle(h); return nullptr; }
    *created = (static_cast<ULONGLONG>(birth.dwHighDateTime) << 32) | birth.dwLowDateTime;
    return h;
}
// Tri-state process observation for display only: 1=running, 0=confirmed absent,
// -1=unknown. Enumeration failures must never be presented as "Steam stopped".
inline int probeSteamPresence() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return -1;
    int presence = -1;
    PROCESSENTRY32W pe{sizeof(pe)};
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"steam.exe") == 0) { presence = 1; break; }
        } while (Process32NextW(snap, &pe));
        if (presence != 1 && GetLastError() == ERROR_NO_MORE_FILES) presence = 0;
    } else if (GetLastError() == ERROR_NO_MORE_FILES) presence = 0;
    CloseHandle(snap);
    return presence;
}
class Observer {
    const std::wstring registryPath; // Alternate key is used by isolated Win32 tests only.
    std::mutex lifecycle;
    HANDLE stopEvent = nullptr, pokeEvent = nullptr;
    std::thread thread;
    std::atomic<bool> openWindow{false};
    std::atomic<int> presence{-1};
    using Changed = std::function<void(bool)>; // true=session change, false=bounded retry
    // Some Steam starts keep ActiveProcess.pid at zero and never change the
    // account/root values. A verified top-level window event is a startup wake,
    // not consent to write registry state or a recurring process scan.
    inline static thread_local Observer* windowObserver = nullptr;
    static void CALLBACK windowEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) {
        auto* self = windowObserver;
        if (!self || !window || object != OBJID_WINDOW || child != CHILDID_SELF ||
            (event != EVENT_OBJECT_CREATE && event != EVENT_OBJECT_SHOW)) return;
        DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
        ULONGLONG birth = 0;
        const HANDLE candidate = verifiedProcess(pid, steamRoot(self->registryPath), &birth);
        if (!candidate) return;
        CloseHandle(candidate);
        if (self->pokeEvent) SetEvent(self->pokeEvent);
    }
    struct WindowSubscription {
        HWINEVENTHOOK hook = nullptr;
        explicit WindowSubscription(Observer* owner) {
            windowObserver = owner;
            hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr, windowEvent,
                0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        }
        ~WindowSubscription() { if (hook) UnhookWinEvent(hook); windowObserver = nullptr; }
    };
    void run(const Changed& changed, const std::function<bool()>& needsRetry) {
        HANDLE notify = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HKEY key = nullptr;
        HANDLE process = nullptr;
        Session previous;
        bool havePrevious = false, armed = false;
        DWORD previousRegisteredPid = 0;
        RetryWindow retries;
        auto arm = [&] {
            if (!notify) return false;
            constexpr DWORD filter = REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET;
            if (key && RegNotifyChangeKeyValue(key, TRUE, filter, notify, TRUE) == ERROR_SUCCESS) return true;
            if (key) { RegCloseKey(key); key = nullptr; }
            ResetEvent(notify); // Closing a deleted key can signal its outstanding notification.
            std::wstring path = registryPath;
            while (!path.empty()) {
                if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_NOTIFY, &key) == ERROR_SUCCESS) {
                    if (RegNotifyChangeKeyValue(key, TRUE, filter, notify, TRUE) == ERROR_SUCCESS) return true;
                    RegCloseKey(key); key = nullptr;
                }
                const auto slash = path.find_last_of(L'\\');
                if (slash == std::wstring::npos) break;
                path.resize(slash);
            }
            return false; // Do not replace a failed subscription with permanent polling.
        };
        auto inspect = [&](bool force = false) {
            Session next{steamRoot(registryPath)};
            next.account = regNumber(registryPath, L"ActiveUser");
            const DWORD registeredPid = regNumber(registryPath, L"pid");
            if (!force && havePrevious && next.root == previous.root && next.account == previous.account &&
                registeredPid == previousRegisteredPid && (!process || WaitForSingleObject(process, 0) == WAIT_TIMEOUT)) return false;
            previousRegisteredPid = registeredPid;
            if (process && previous.root == next.root && (!registeredPid || registeredPid == previous.pid) &&
                WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
                next.pid = previous.pid; next.created = previous.created;
            } else {
                if (process) { CloseHandle(process); process = nullptr; }
                process = verifiedProcess(registeredPid, next.root, &next.created);
                if (process) next.pid = registeredPid;
                else if (!next.root.empty()) {
                    // Discovery only on startup/notification/user action, never on an idle clock.
                    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                    if (snap != INVALID_HANDLE_VALUE) {
                        PROCESSENTRY32W pe{sizeof(pe)};
                        if (Process32FirstW(snap, &pe)) do {
                            if (_wcsicmp(pe.szExeFile, L"steam.exe")) continue;
                            ULONGLONG birth = 0;
                            HANDLE candidate = verifiedProcess(pe.th32ProcessID, next.root, &birth);
                            if (!candidate) continue;
                            if (process) { // Ambiguous same-install sessions: do not guess.
                                CloseHandle(candidate); CloseHandle(process); process = nullptr;
                                next.pid = 0; next.created = 0; break;
                            }
                            process = candidate; next.pid = pe.th32ProcessID; next.created = birth;
                        } while (Process32NextW(snap, &pe));
                        CloseHandle(snap);
                    }
                }
            }
            // Reuse the verified live handle; otherwise probe only on session events,
            // startup or bounded retries. The input/render path only reads this cache.
            presence.store(process ? 1 : probeSteamPresence(), std::memory_order_release);
            if (!havePrevious || !(next == previous)) {
                previous = next; havePrevious = true;
                retries.begin(GetTickCount64());
                try { changed(true); } catch (...) {}
                return true;
            }
            return false;
        };
        WindowSubscription windows(this); // Its callbacks run only on this existing observer thread.
        armed = arm(); // Subscribe BEFORE reading, so startup changes cannot be lost.
        inspect(true);
        for (;;) {
            HANDLE handles[4]{stopEvent, pokeEvent}; DWORD count = 2;
            const DWORD registryIndex = armed ? count++ : MAXDWORD;
            if (armed) handles[registryIndex] = notify;
            const DWORD processIndex = process ? count++ : MAXDWORD;
            if (process) handles[processIndex] = process;
            const DWORD result = MsgWaitForMultipleObjectsEx(count, handles, retries.timeout(GetTickCount64()), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (result == WAIT_OBJECT_0 + count) {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message); DispatchMessageW(&message);
                }
                continue; // A verified Steam callback signals pokeEvent; inspect on that handle.
            }
            if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) break;
            if (result == WAIT_TIMEOUT) {
                if (retries.take(GetTickCount64()) && needsRetry()) {
                    // Re-probe once: PID/ActiveUser can become valid during bounded startup.
                    if (!inspect(true)) { try { changed(false); } catch (...) {} }
                }
                continue;
            }
            if (result == WAIT_OBJECT_0 + registryIndex) armed = arm(); // One-shot: rearm before reading.
            if (result == WAIT_OBJECT_0 + 1 && openWindow.exchange(false)) retries.begin(GetTickCount64());
            inspect(result == WAIT_OBJECT_0 + 1);
        }
        if (process) CloseHandle(process);
        if (key) RegCloseKey(key);
        if (notify) CloseHandle(notify);
    }
public:
    explicit Observer(std::wstring keyPath = L"Software\\Valve\\Steam") : registryPath(std::move(keyPath)) {}
    ~Observer() { stop(); }
    bool start(Changed changed, std::function<bool()> needsRetry) {
        std::lock_guard<std::mutex> lock(lifecycle);
        if (thread.joinable()) return true;
        presence.store(-1, std::memory_order_release);
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        pokeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!stopEvent || !pokeEvent) {
            if (stopEvent) CloseHandle(stopEvent);
            if (pokeEvent) CloseHandle(pokeEvent);
            stopEvent = pokeEvent = nullptr; return false;
        }
        try { thread = std::thread([this, changed = std::move(changed), needsRetry = std::move(needsRetry)] {
            try { run(changed, needsRetry); } catch (...) {}
            presence.store(-1, std::memory_order_release);
        }); }
        catch (...) { CloseHandle(stopEvent); CloseHandle(pokeEvent); stopEvent = pokeEvent = nullptr; return false; }
        return true;
    }
    // Unknown before startup/after stop or an observer failure. No scan or locks here.
    int steamPresence() const { return presence.load(std::memory_order_acquire); }
    void beginWindow() { // Explicit request, not called from retries.
        std::lock_guard<std::mutex> lock(lifecycle);
        openWindow.store(true);
        if (pokeEvent) SetEvent(pokeEvent);
    }
    void stop() {
        std::lock_guard<std::mutex> lock(lifecycle);
        if (stopEvent) SetEvent(stopEvent);
        if (thread.joinable()) thread.join();
        if (stopEvent) CloseHandle(stopEvent);
        if (pokeEvent) CloseHandle(pokeEvent);
        stopEvent = pokeEvent = nullptr; openWindow.store(false);
        presence.store(-1, std::memory_order_release);
    }
};
}