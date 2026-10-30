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
#include <map>
#include <string>
#include <thread>
#include "steam_window_event_budget.h"

namespace ymcc::steamsession {
struct Session {
    std::wstring root;
    DWORD pid = 0, account = 0;
    ULONGLONG created = 0; // PID reuse is a new session, too.
    bool operator==(const Session&) const = default;
};
// Non-owning event hint, NOT confirmed BPM or a renderer identity. HWND/PID may
// be reused after capture; callers must revalidate window + PID/birth/path.
struct UiWindowCandidate {
    HWND window = nullptr;
    DWORD pid = 0;
    ULONGLONG tick = 0; // GetTickCount64 at receipt of the existing WinEvent.
};
struct DiagnosticSnapshot {
    bool available = false; // False on lock contention or no verified root session.
    Session session;
    UiWindowCandidate uiWindow;
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
    mutable std::mutex sessionMutex;
    Session cachedSession; // Shared verified PID/birth identity for addon lifecycle consumers.
    UiWindowCandidate cachedUiWindow; // Guarded by sessionMutex, never used to authorize actions.
    WindowProcessCache windowPids; std::wstring windowRoot;
    WindowHintWindow uiHints;
    ULONGLONG windowPokedAt = 0; bool windowPoked = false;
    std::function<void()> uiChanged; // Lightweight notification only; never CDP on this observer thread.
    using Changed = std::function<void(bool)>; // true=session change, false=bounded retry
    // Some Steam starts keep ActiveProcess.pid at zero and never change the
    // account/root values. A verified top-level window event is a startup wake,
    // not consent to write registry state or a recurring process scan.
    inline static thread_local Observer* windowObserver = nullptr;
    static void CALLBACK windowEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) {
        auto* self = windowObserver;
        if (!self || !window || object != OBJID_WINDOW || child != CHILDID_SELF || GetAncestor(window, GA_ROOT) != window) return;
        const auto session = self->sessionSnapshot();
        const auto& root = session.root;
        if (root.empty()) return; // Registry subscription initializes root; no per-window registry search.
        if (root != self->windowRoot) { self->windowRoot = root; self->windowPids.clear(); self->windowPoked = false; }
        DWORD pid = 0; GetWindowThreadProcessId(window, &pid); if (!pid) return;
        const auto now = GetTickCount64(); unsigned kind = pid == session.pid ? 1 : 0;
        if (!kind) kind = self->windowPids.classify(pid, now, [&](std::wstring path) -> unsigned {
            std::replace(path.begin(), path.end(), L'/', L'\\');
            std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) { return std::towlower(c); });
            if (path == root + L"\\steam.exe") {
                ULONGLONG birth = 0; const auto process = verifiedProcess(pid, root, &birth);
                if (process) { CloseHandle(process); return 1; }
            } else if (self->uiChanged && path.starts_with(root + L"\\bin\\cef\\") && path.ends_with(L"\\steamwebhelper.exe")) return 2;
            return 0;
        });
        // This cache admits NOTIFICATIONS only. All actions still verify the
        // session/process birth/owner. No token/image query for every name event.
        if (!kind) return;
        if (kind == 2) {
            std::lock_guard lock(self->sessionMutex);
            if (self->cachedSession == session) {
                if (event == EVENT_OBJECT_DESTROY) {
                    if (self->cachedUiWindow.window == window) self->cachedUiWindow = {};
                } else if (event != EVENT_OBJECT_HIDE) self->cachedUiWindow = {window, pid, now};
                // HIDE retains the background candidate; another hidden window cannot replace it.
            }
        }
        if (kind == 1 && (event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_SHOW) && self->pokeEvent &&
            (!self->windowPoked || now - self->windowPokedAt >= 1000)) {
            self->windowPoked = true; self->windowPokedAt = now; SetEvent(self->pokeEvent);
        }
        if (self->uiChanged) self->uiHints.request(now);
    }
    struct WindowSubscription {
        HWINEVENTHOOK hook = nullptr, names = nullptr;
        Observer* owner;
        explicit WindowSubscription(Observer* value) : owner(value) {
            windowObserver = owner; owner->uiHints.clear(); owner->windowPids.clear();
            hook = SetWinEventHook(EVENT_OBJECT_CREATE, owner->uiChanged ? EVENT_OBJECT_HIDE : EVENT_OBJECT_SHOW, nullptr, windowEvent,
                0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            if (owner->uiChanged) names = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE,
                nullptr, windowEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        }
        ~WindowSubscription() { if (names) UnhookWinEvent(names); if (hook) UnhookWinEvent(hook); owner->uiHints.clear(); owner->windowPids.clear(); windowObserver = nullptr; }
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
                { std::lock_guard lock(sessionMutex); cachedSession = next; cachedUiWindow = {}; }
                windowPids.clear(); // Same-install PID/birth/account changes invalidate event classification too.
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
            // A busy message stream must not starve stop, pending hints or retries.
            if (WaitForSingleObject(stopEvent, 0) != WAIT_TIMEOUT) break;
            if (uiHints.take(GetTickCount64()) && uiChanged) { try { uiChanged(); } catch (...) {} }
            if (retries.take(GetTickCount64()) && needsRetry()) {
                // Re-probe once even if Windows messages keep the queue busy.
                if (!inspect(true)) { try { changed(false); } catch (...) {} }
            }
            HANDLE handles[4]{stopEvent, pokeEvent}; DWORD count = 2;
            const DWORD registryIndex = armed ? count++ : MAXDWORD;
            if (armed) handles[registryIndex] = notify;
            const DWORD processIndex = process ? count++ : MAXDWORD;
            if (process) handles[processIndex] = process;
            const DWORD result = MsgWaitForMultipleObjectsEx(count, handles, (std::min)(retries.timeout(GetTickCount64()), uiHints.timeout(GetTickCount64())), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (result == WAIT_OBJECT_0 + count) {
                MSG message{};
                for (unsigned dispatched = 0; dispatched < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++dispatched) {
                    TranslateMessage(&message); DispatchMessageW(&message);
                }
                continue; // A verified Steam callback signals pokeEvent; inspect on that handle.
            }
            if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) break;
            if (result == WAIT_TIMEOUT) continue;
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
    bool start(Changed changed, std::function<bool()> needsRetry, std::function<void()> onUiChanged = {}) {
        std::lock_guard<std::mutex> lock(lifecycle);
        if (thread.joinable()) return true;
        presence.store(-1, std::memory_order_release);
        uiChanged = std::move(onUiChanged);
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
            { std::lock_guard lock(sessionMutex); cachedSession = {}; cachedUiWindow = {}; }
        }); }
        catch (...) { CloseHandle(stopEvent); CloseHandle(pokeEvent); stopEvent = pokeEvent = nullptr; return false; }
        return true;
    }
    // Unknown before startup/after stop or an observer failure. No scan or locks here.
    int steamPresence() const { return presence.load(std::memory_order_acquire); }
    Session sessionSnapshot() const { std::lock_guard lock(sessionMutex); return cachedSession; }
    DiagnosticSnapshot diagnosticWindowSnapshot() const {
        std::unique_lock lock(sessionMutex, std::try_to_lock);
        if (!lock.owns_lock()) return {};
        return {cachedSession.pid != 0 && cachedSession.created != 0, cachedSession, cachedUiWindow};
    }
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
        { std::lock_guard sessionLock(sessionMutex); cachedSession = {}; cachedUiWindow = {}; }
        presence.store(-1, std::memory_order_release);
    }
};
}