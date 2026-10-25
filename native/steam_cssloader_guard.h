#pragma once
// CSSLoader uses the shared Steam session and finite startup probes.
// A bounded startup use reuses the single resident backend or starts a missing one.
// No Steam launch or recurring discovery. Abnormal verified exits have a capped recovery window.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include "steam_session_observer.h"
#include "steam_cssloader_mode.h"
#include "decky_sidebar_launch.h"
#include <array>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#pragma comment(lib, "iphlpapi.lib")

namespace ymcc::steamcss {
struct Link {
    bool enabled = false;
    std::wstring executable;
    std::string error;
    bool operator==(const Link&) const = default;
};
inline Link readLink(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Link{} : Link{false, {}, "link-unreadable"};
    }
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return {false, {}, "link-invalid"};
    ymcc::deckylaunch::Handle file;
    file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) { file.value = nullptr; return {false, {}, "link-unreadable"}; }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size) || size.QuadPart <= 0 || size.QuadPart > 32768) return {false, {}, "link-invalid"};
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0'); DWORD read = 0;
    if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()) return {false, {}, "link-unreadable"};
    if (bytes.compare(0, 3, "\xef\xbb\xbf") == 0) bytes.erase(0, 3);
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (!length) return {false, {}, "link-invalid-utf8"};
    std::wstring text(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
    const auto first = text.find_first_not_of(L" \t\r\n"), last = text.find_last_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {false, {}, "link-invalid"};
    text = text.substr(first, last - first + 1);
    if (text.find_first_of(L"\r\n\"\0", 0, 4) != std::wstring::npos || text.size() < 4 ||
        text[1] != L':' || (text[2] != L'\\' && text[2] != L'/')) return {false, {}, "link-invalid"};
    std::wstring full(32768, L'\0');
    const DWORD count = GetFullPathNameW(text.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
    if (!count || count >= full.size()) return {false, {}, "link-invalid"};
    full.resize(count);
    if (_wcsicmp(std::filesystem::path(full).filename().c_str(), L"CssLoader-Standalone-Headless.exe")) return {false, {}, "link-wrong-executable"};
    return {true, std::move(full), {}};
}
inline bool sameSession(const ymcc::steamsession::Session& a, const ymcc::steamsession::Session& b) {
    // Account/login writes do not replenish a process-start budget.
    return a.pid == b.pid && a.created == b.created && a.root == b.root;
}
inline bool ownProcess(HANDLE process) {
    ymcc::deckylaunch::Handle caller, target;
    return OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &caller.value) && OpenProcessToken(process, TOKEN_QUERY, &target.value) &&
        ymcc::deckylaunch::equalUser(caller.value, target.value) && ymcc::deckylaunch::sameSession(caller.value, target.value);
}
inline HANDLE steamLease(const ymcc::steamsession::Session& session) {
    ULONGLONG birth = 0;
    HANDLE process = ymcc::steamsession::verifiedProcess(session.pid, session.root, &birth);
    if (process && (birth != session.created || !ownProcess(process))) { CloseHandle(process); process = nullptr; }
    return process;
}
struct Result {
    HANDLE process = nullptr; // Read-only wait lease, transferred to the worker; never an owned kill target.
    DWORD pid = 0;
    std::string phase = "failed", reason;
    bool started = false;
};
inline Result loaderLease(DWORD pid) {
    ymcc::deckylaunch::Handle process;
    process.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process.value) return {nullptr, 0, "blocked", "loader-owner-unverified"};
    wchar_t image[32768]{}; DWORD size = static_cast<DWORD>(std::size(image));
    const bool valid = WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT && QueryFullProcessImageNameW(process.value, 0, image, &size) && ownProcess(process.value) &&
        _wcsicmp(std::filesystem::path(std::wstring(image, size)).filename().c_str(), L"CssLoader-Standalone-Headless.exe") == 0;
    if (!valid) return {nullptr, 0, "blocked", "loader-owner-unverified"};
    HANDLE lease = process.value; process.value = nullptr;
    return {lease, pid, "running", {}, false};
}
inline Result probeBackend(DWORD exclude = 0, bool listenersOnly = false, unsigned short targetPort = 35821) {
    // A foreign listener (including a different Windows user's loader) blocks
    // launch. Check IPv4 AND IPv6 before adopting an existing process.
    ymcc::deckylaunch::Handle found;
    DWORD foundPid = 0;
    for (const ULONG family : {2UL, 23UL}) {
        DWORD size = 0;
        const DWORD initial = GetExtendedTcpTable(nullptr, &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (initial != ERROR_INSUFFICIENT_BUFFER || !size || size > 16 * 1024 * 1024) return {nullptr, 0, "blocked", "loader-port-unverified"};
        std::vector<unsigned char> bytes(size);
        if (GetExtendedTcpTable(bytes.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR) return {nullptr, 0, "blocked", "loader-port-unverified"};
        bool blocked = false;
        const auto accept = [&](DWORD pid, DWORD port) {
            const auto decoded = static_cast<unsigned short>(((port & 0xff) << 8) | ((port >> 8) & 0xff));
            if (decoded != targetPort || pid == exclude) return;
            auto candidate = loaderLease(pid);
            if (!candidate.process) { blocked = true; return; }
            if (found.value) CloseHandle(candidate.process);
            else { found.value = candidate.process; foundPid = candidate.pid; }
        };
        if (family == 2) { const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(bytes.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) accept(table->table[i].dwOwningPid, table->table[i].dwLocalPort); }
        else { const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(bytes.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) accept(table->table[i].dwOwningPid, table->table[i].dwLocalPort); }
        if (blocked) return {nullptr, 0, "blocked", "loader-port-occupied"};
    }
    if (found.value) { HANDLE lease = found.value; found.value = nullptr; return {lease, foundPid, "running", {}, false}; }
    if (listenersOnly) return {nullptr, 0, "missing", {}, false};
    ymcc::deckylaunch::Handle snapshot;
    snapshot.value = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot.value == INVALID_HANDLE_VALUE) { snapshot.value = nullptr; return {nullptr, 0, "blocked", "loader-processes-unverified"}; }
    PROCESSENTRY32W entry{sizeof(entry)};
    if (!Process32FirstW(snapshot.value, &entry)) return {nullptr, 0, "blocked", "loader-processes-unverified"};
    DWORD ourSession = 0; if (!ProcessIdToSessionId(GetCurrentProcessId(), &ourSession)) return {nullptr, 0, "blocked", "loader-owner-unverified"};
    do {
        if (entry.th32ProcessID == exclude || _wcsicmp(entry.szExeFile, L"CssLoader-Standalone-Headless.exe")) continue;
        DWORD targetSession = 0;
        if (!ProcessIdToSessionId(entry.th32ProcessID, &targetSession)) return {nullptr, 0, "blocked", "loader-owner-unverified"};
        if (targetSession != ourSession) continue;
        return loaderLease(entry.th32ProcessID); // Parent/child packaging is one backend, not a second launch.
    } while (Process32NextW(snapshot.value, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) return {nullptr, 0, "blocked", "loader-processes-unverified"};
    return {nullptr, 0, "missing", {}, false};
}
class Budget {
    std::array<DWORD, 3> delays_;
    ULONGLONG due_ = 0; bool armed_ = false; unsigned used_ = 0;
public:
    explicit Budget(std::array<DWORD, 3> delays = {10000, 30000, 35000}) : delays_(delays) {}
    void begin(ULONGLONG now, bool immediate = false) { used_ = 0; resume(now); if (immediate) due_ = now; }
    void pause() { armed_ = false; }
    void resume(ULONGLONG now) { armed_ = used_ < delays_.size(); if (armed_) due_ = now + delays_[used_]; }
    DWORD timeout(ULONGLONG now) const { return !armed_ ? INFINITE : now >= due_ ? 0 : static_cast<DWORD>(due_ - now); }
    bool take(ULONGLONG now) { if (timeout(now) != 0) return false; ++used_; resume(now); return true; }
    unsigned used() const { return used_; }
};
struct State {
    bool linked = false, steamRunning = false, started = false;
    DWORD pid = 0; unsigned attempts = 0;
    std::string phase = "waiting-steam", reason;
    unsigned automaticRecoveries = 0;
};
struct Config {
    std::wstring linkFile;
    std::function<Result(const Link&, const ymcc::steamsession::Session&, const std::function<bool()>&)> ensure;
    std::array<DWORD, 3> delays{10000, 30000, 35000}; // Test fixtures alone shorten these delays.
    std::function<bool(const ymcc::steamsession::Session&)> isIdle;
    std::function<HANDLE(const ymcc::steamsession::Session&, bool, bool)> confirmIdle;
    std::function<bool(const ymcc::steamsession::Session&)> isResidentConfirmed;
    std::function<Result()> probeExisting; // Same read-only backend boundary; isolated fixtures replace IO.
    std::function<bool(const ymcc::steamsession::Session&)> invalidateBackend; // Revoke old helper/results; retain CSS family.
    std::function<HANDLE(const ymcc::steamsession::Session&)> confirmExisting; // Read-only, never Ensure/reapply/start.
    DWORD crashDelayMs = 30000; // Exit-triggered one-shot; isolated fixtures shorten it.
    std::function<bool()> mayLaunch; // Existing application exit fence; never changes user consent.
    std::function<void(const State&)> changed; // Event-only UI recovery; no frontend timer.
    std::function<ModeSample(const ymcc::steamsession::Session&)> readMode;
    std::function<bool(const ymcc::steamsession::Session&)> refreshMode; // Invalidate old cache and schedule the existing shared reader.
    std::array<DWORD, 3> modeDelays{600, 5000, 30000}; // Debounce / read gap / exhausted-failure gap.
};
class Runtime {
    std::mutex lifecycle_, stateMutex_;
    unsigned recoveryCount_ = 0; // Worker-owned, copied into public state under stateMutex_.
    Config config_; State state_; ymcc::steamsession::Session desired_;
    HANDLE wake_ = nullptr, stop_ = nullptr; std::thread worker_; bool stopping_ = false, manualRecheck_ = false, uiRecheck_ = false;
    bool modePending_ = false, resumeRecheck_ = false, rebindPending_ = false;
    ULONGLONG lastRebindGeneration_ = 0;
    ymcc::steamsession::Session rebindSession_;
    ULONGLONG lastResumeGeneration_ = 0;
    ymcc::steamsession::Session resumeSession_;
    ModeSample pendingMode_;
    ymcc::steamsession::Session pendingModeSession_;
    void publish(const Link& link, const ymcc::steamsession::Session& session, const std::string& phase,
                 const std::string& reason, unsigned attempts, DWORD pid = 0, bool started = false) {
        State copy;
        { std::lock_guard lock(stateMutex_);
          state_ = {link.enabled, session.pid != 0, started, pid, attempts, phase, reason, recoveryCount_}; copy = state_; }
        try { if (config_.changed) config_.changed(copy); } catch (...) {} // No observer/UI blockage.
    }
    void run() {
        HANDLE directoryEvent = INVALID_HANDLE_VALUE, backend = nullptr, idleHelper = nullptr;
        bool idleRequested = false, modeCycle = false, linkDirty = true;
        ExitRecovery recovery(config_.crashDelayMs);
        bool exitParked = false, recoveringExit = false, exitAwaitingReady = false, readOnlyHandoff = false, handoffModePending = false;
        ModeSample handoffMode;
        struct Cleanup {
            HANDLE& directory; HANDLE& backend; HANDLE& helper;
            ~Cleanup() { if (helper) CloseHandle(helper); if (backend) CloseHandle(backend); if (directory != INVALID_HANDLE_VALUE) FindCloseChangeNotification(directory); }
        } cleanup{directoryEvent, backend, idleHelper};
        std::wstring watched;
        auto closeBackend = [&] { if (backend) CloseHandle(backend); backend = nullptr; };
        auto watchDirectory = [&] {
            auto directory = std::filesystem::path(config_.linkFile).parent_path();
            while (!directory.empty() && GetFileAttributesW(directory.c_str()) == INVALID_FILE_ATTRIBUTES) {
                const auto parent = directory.parent_path(); if (parent == directory) { directory.clear(); break; } directory = parent;
            }
            if (directory.wstring() == watched && directoryEvent != INVALID_HANDLE_VALUE) return;
            if (directoryEvent != INVALID_HANDLE_VALUE) FindCloseChangeNotification(directoryEvent);
            watched = directory.wstring();
            directoryEvent = watched.empty() ? INVALID_HANDLE_VALUE : FindFirstChangeNotificationW(watched.c_str(), FALSE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
        };
        ModeWindow modeWindow(config_.modeDelays[0], config_.modeDelays[1], config_.modeDelays[2]);
        bool waitingForMode = false;
        Budget budget(config_.delays); Link link; ymcc::steamsession::Session session; bool initialized = false;
        // Subscribe before reading the link so an enable/disable cannot be lost.
        watchDirectory();
        if (directoryEvent == INVALID_HANDLE_VALUE) { publish({}, {}, "failed", "link-notification-unavailable", 0); return; }
        for (;;) {
            ymcc::steamsession::Session next, confirmedSession; ModeSample confirmedMode;
            bool manualRecheck = false, uiRecheck = false, hasConfirmedMode = false, resumeRequested = false;
            ymcc::steamsession::Session resumedSession, reboundSession; bool rebindRequested = false;
            { std::lock_guard lock(stateMutex_); if (stopping_) break; next = desired_; manualRecheck = manualRecheck_; manualRecheck_ = false; uiRecheck = uiRecheck_;
              hasConfirmedMode = modePending_; confirmedMode = pendingMode_; confirmedSession = pendingModeSession_; modePending_ = false;
              resumeRequested = resumeRecheck_; resumedSession = resumeSession_; resumeRecheck_ = false;
              rebindRequested = rebindPending_; reboundSession = rebindSession_; rebindPending_ = false; }
            // Window/Steam/helper wakes cannot change this file. Refresh on its
            // directory notification; revalidate explicitly at action boundaries.
            const auto nextLink = linkDirty ? readLink(config_.linkFile) : link;
            linkDirty = false;
            if (!initialized || !sameSession(next, session) || manualRecheck) {
                recovery.reset(); recoveryCount_ = 0; exitParked = recoveringExit = exitAwaitingReady = false;
            }
            if (!next.pid || !nextLink.enabled || !(nextLink == link)) { recovery.cancel(); recoveringExit = exitAwaitingReady = false; }
            const bool resumeRecheck = resumeRequested && (!exitParked || exitAwaitingReady) && next.pid && nextLink.enabled && sameSession(next, resumedSession);
            if (resumeRecheck && exitAwaitingReady) { exitAwaitingReady = false; exitParked = false; recoveringExit = true; }
            if (resumeRecheck) {
                // The worker invalidates the old shared mode BEFORE any ensure.
                // Window/power callbacks never run CDP or launch a process.
                bool requested = false; try { requested = config_.refreshMode && config_.refreshMode(next); } catch (...) {}
                if (!requested) {
                    budget.pause(); modeWindow.reset();
                    publish(nextLink, next, "waiting-steam-ui", "resume-mode-refresh-unavailable", 0);
                    continue;
                }
                hasConfirmedMode = false; // A pre-resume mailbox cannot confirm this generation.
            }
            if (!initialized || manualRecheck || resumeRecheck || !(nextLink == link) || !sameSession(next, session)) {
                const bool changed = !initialized || !(nextLink == link) || !sameSession(next, session);
                if (manualRecheck || resumeRecheck) { readOnlyHandoff = false; handoffModePending = false; idleRequested = false; if (idleHelper) { CloseHandle(idleHelper); idleHelper = nullptr; } }
                if (changed) { readOnlyHandoff = false; handoffModePending = false; modeWindow.reset(); waitingForMode = false; modeCycle = false; idleRequested = false; if (idleHelper) { CloseHandle(idleHelper); idleHelper = nullptr; } }
                if (initialized && !(nextLink == link) && session.pid && config_.invalidateBackend) {
                    // OFF/path replacement revokes only our temporary helper and
                    // stale confirmation; the independent CSS family survives.
                    try { config_.invalidateBackend(session); } catch (...) {}
                }
                initialized = true; closeBackend(); session = next; link = nextLink;
                budget.pause();
                if (session.pid && link.enabled && !exitParked) {
                    modeCycle = manualRecheck || resumeRecheck;
                    budget.begin(GetTickCount64(), manualRecheck || resumeRecheck);
                    if (config_.readMode) {
                        if (resumeRecheck) modeWindow.resumeEntry(GetTickCount64());
                        else if (manualRecheck) modeWindow.manualEntry(GetTickCount64());
                        else modeWindow.request(GetTickCount64());
                    }
                    publish(link, session, manualRecheck || resumeRecheck ? "applying" : "grace", resumeRecheck ? "resume-css-recheck" : manualRecheck ? "manual-bigpicture-entry" : "", 0);
                }
                else publish(link, session, !link.error.empty() ? "blocked" : !link.enabled ? "disabled" : exitParked ? "waiting-appearance" : "waiting-steam",
                    exitParked ? "loader-exit-requires-manual-or-budgeted-recovery" : link.error, 0);
            }
            const auto allowed = [&] {
                std::lock_guard lock(stateMutex_);
                return !stopping_ && sameSession(desired_, session) && (!config_.mayLaunch || config_.mayLaunch());
            };
            if (rebindRequested && session.pid && link.enabled && sameSession(session, reboundSession) &&
                allowed() && config_.confirmExisting && readLink(config_.linkFile) == link) {
                HANDLE confirmation = nullptr;
                try { confirmation = config_.confirmExisting(session); } catch (...) {}
                // A newer click may already own the use. A refused handoff must
                // not discard the backend HANDLE we were successfully observing.
                if (confirmation) {
                    budget.pause(); modeWindow.park(); recovery.cancel(); waitingForMode = false;
                    exitAwaitingReady = recoveringExit = false; exitParked = true; modeCycle = false;
                    if (idleHelper) CloseHandle(idleHelper);
                    closeBackend(); idleRequested = true; idleHelper = confirmation;
                    readOnlyHandoff = true; handoffModePending = false;
                }
                publish(link, session, confirmation ? "verifying" : "waiting-appearance",
                    confirmation ? "manual-use-read-only-confirmation" : "manual-use-observer-busy", budget.used());
            }
            if (readOnlyHandoff && hasConfirmedMode && sameSession(confirmedSession, session)) {
                // Keep only the latest delivered mode. Replay it after helper
                // completion, when normal document/entry coalescing is active.
                handoffMode = confirmedMode; handoffModePending = true;
            }
            if (recovery.take(GetTickCount64())) {
                bool refreshed = false;
                if (session.pid && link.enabled && allowed() && readLink(config_.linkFile) == link) {
                    try { refreshed = config_.refreshMode && config_.refreshMode(session); } catch (...) {}
                }
                if (refreshed) {
                    hasConfirmedMode = false; // A mailbox captured before refresh cannot authorize recovery.
                    exitParked = false; recoveringExit = true; waitingForMode = true;
                    idleRequested = false; modeCycle = true; budget.pause(); modeWindow.reset();
                    modeWindow.recoveryEntry(GetTickCount64());
                    publish(link, session, "waiting-steam-ui", "loader-crash-fresh-mode", budget.used());
                } else {
                    exitParked = true; exitAwaitingReady = session.pid && link.enabled && !allowed(); budget.pause(); modeWindow.reset();
                    publish(link, session, "waiting-appearance", exitAwaitingReady ? "loader-crash-awaiting-ready" : "loader-crash-mode-refresh-unavailable", budget.used());
                }
            }
            if (session.pid && link.enabled && !exitParked && config_.readMode) {
                const bool delivered = hasConfirmedMode && sameSession(confirmedSession, session);
                if (uiRecheck && !delivered) modeWindow.request(GetTickCount64());
                if (delivered || modeWindow.take(GetTickCount64())) {
                    { std::lock_guard lock(stateMutex_); uiRecheck_ = false; }
                    ModeSample mode;
                    if (allowed()) { if (delivered) mode = confirmedMode; else { try { mode = config_.readMode(session); } catch (...) {} } }
                    // Reading can overlap exit/disable. Never accept a stale read
                    // or let a notification authorize a different Steam process.
                    if (!allowed()) { modeWindow.reset(); continue; }
                    const bool entered = delivered ? modeWindow.confirmed(GetTickCount64(), mode) : modeWindow.finish(GetTickCount64(), mode);
                    // A manual request can arrive before Steam reaches BPM.
                    // Its old-mode/unknown probe parked the budget; the first
                    // confirmed BPM must resume it even when ModeWindow already
                    // coalesced that manual edge. No second use after success.
                    if (entered || (waitingForMode && mode.mode == UiMode::BigPicture)) {
                        waitingForMode = false;
                        if (!(readLink(config_.linkFile) == link)) { linkDirty = true; modeWindow.reset(); continue; }
                        readOnlyHandoff = false; handoffModePending = false; modeCycle = true; idleRequested = false;
                        if (idleHelper) { CloseHandle(idleHelper); idleHelper = nullptr; }
                        closeBackend(); budget.begin(GetTickCount64(), true);
                        publish(link, session, "applying", "bigpicture-entered", 0);
                    }
                }
            }
            if (!exitParked && budget.take(GetTickCount64())) {
                if (!(readLink(config_.linkFile) == link)) { linkDirty = true; continue; }
                Result result;
                try { result = config_.ensure ? config_.ensure(link, session, allowed) : probeBackend(); }
                catch (...) { result = {nullptr, 0, "failed", "guard-check-failed"}; }
                if (!allowed()) {
                    if (result.process) CloseHandle(result.process); budget.pause();
                    publish(link, session, "canceled", "lifecycle-changed", budget.used()); continue;
                }
                backend = result.process;
                waitingForMode = result.phase == "waiting-steam-ui";
                if (!modeCycle && !backend && result.phase == "missing" && config_.isIdle && config_.isIdle(session)) {
                    budget.pause(); result.phase = "idle"; result.reason = "appearance-applied-backend-idle";
                }
                if (backend || result.phase == "waiting-manual-steam" || result.phase == "waiting-steam-ui") budget.pause();
                const bool startupCandidate = backend || result.phase == "missing";
                if (!backend && result.phase != "idle" && result.phase != "waiting-manual-steam" && result.phase != "waiting-steam-ui" && budget.timeout(GetTickCount64()) == INFINITE) result.phase = "exhausted";
                if (recoveringExit && result.phase == "exhausted") { exitParked = true; recoveringExit = false; budget.pause(); }
                publish(link, session, result.phase, result.reason, budget.used(), result.pid, result.started);
                // Exactly one startup use for either external or YMCC Steam.
                // Missing CSS is started only for an enabled verified session;
                // an idle receipt never replenishes it; abnormal exit recovery has its own fixed cap.
                if (startupCandidate && !idleRequested && config_.confirmIdle && allowed() && readLink(config_.linkFile) == link) {
                    idleRequested = true; budget.pause();
                    idleHelper = config_.confirmIdle(session, !backend, modeCycle);
                    modeCycle = false;
                    if (idleHelper) publish(link, session, "applying", "temporary-css-use", budget.used(), result.pid, !backend);
                }
            }
            HANDLE handles[5]{stop_, wake_}; DWORD count = 2;
            const DWORD directoryIndex = directoryEvent != INVALID_HANDLE_VALUE ? count++ : MAXDWORD;
            if (directoryIndex != MAXDWORD) handles[directoryIndex] = directoryEvent;
            const DWORD backendIndex = backend ? count++ : MAXDWORD;
            if (backend) handles[backendIndex] = backend;
            const DWORD helperIndex = idleHelper ? count++ : MAXDWORD;
            if (idleHelper) handles[helperIndex] = idleHelper;
            const auto result = WaitForMultipleObjects(count, handles, FALSE, std::min({budget.timeout(GetTickCount64()), modeWindow.timeout(GetTickCount64()), recovery.timeout(GetTickCount64())}));
            if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) break;
            if (directoryIndex != MAXDWORD && result == WAIT_OBJECT_0 + directoryIndex) {
                linkDirty = true;
                if (!FindNextChangeNotification(directoryEvent)) { FindCloseChangeNotification(directoryEvent); directoryEvent = INVALID_HANDLE_VALUE; watched.clear(); }
                watchDirectory(); // Directory deletion/recreation heals from its existing ancestor, no timer.
            }
            if (backendIndex != MAXDWORD && result == WAIT_OBJECT_0 + backendIndex) {
                DWORD exitCode = 0; const bool exitKnown = GetExitCodeProcess(backend, &exitCode) != FALSE;
                bool confirmed = false; try { confirmed = config_.isResidentConfirmed && config_.isResidentConfirmed(session); } catch (...) {}
                closeBackend(); budget.pause(); modeWindow.reset(); waitingForMode = false;
                if (config_.isIdle && config_.isIdle(session)) {
                    publish(link, session, "idle", "appearance-applied-backend-idle", budget.used());
                } else {
                    readOnlyHandoff = false; handoffModePending = false; exitParked = true; recoveringExit = false; idleRequested = false;
                    if (idleHelper) { CloseHandle(idleHelper); idleHelper = nullptr; }
                    bool invalidated = false; try { invalidated = config_.invalidateBackend && config_.invalidateBackend(session); } catch (...) {}
                    const bool abnormal = ExitRecovery::abnormal(exitKnown, exitCode);
                    bool identityCurrent = false; { std::lock_guard lock(stateMutex_); identityCurrent = !stopping_ && sameSession(desired_, session); }
                    const bool eligible = abnormal && confirmed && invalidated && config_.readMode && config_.refreshMode &&
                        session.pid && link.enabled && identityCurrent && readLink(config_.linkFile) == link;
                    const bool scheduled = eligible && recovery.admit(GetTickCount64());
                    recoveryCount_ = recovery.used();
                    publish(link, session, scheduled ? "recovering" : "waiting-appearance",
                        scheduled ? "loader-crash-delayed-recovery" : !abnormal ? "loader-exit-not-abnormal" : !eligible ? "loader-crash-recovery-unverified" : "loader-crash-budget-exhausted",
                        budget.used());
                }
            }
            if (helperIndex != MAXDWORD && result == WAIT_OBJECT_0 + helperIndex) {
                CloseHandle(idleHelper); idleHelper = nullptr;
                closeBackend(); budget.pause();
                const bool completedReadOnly = readOnlyHandoff;
                if (completedReadOnly) {
                    readOnlyHandoff = false; exitParked = false;
                    if (handoffModePending) observeMode(session, handoffMode);
                    handoffModePending = false;
                }
                if (config_.isResidentConfirmed) {
                    // One rebind at helper completion keeps an exit HANDLE wait,
                    // not a recurring lookup. A failed helper never earns Ready.
                    auto resident = config_.probeExisting ? config_.probeExisting() : probeBackend(); backend = resident.process;
                    if (backend && config_.isResidentConfirmed(session)) {
                        if (completedReadOnly) { recovery.reset(); recoveryCount_ = 0; }
                        recoveringExit = false;
                        publish(link, session, "ready", "appearance-applied-resident", budget.used(), resident.pid);
                    } else {
                        readOnlyHandoff = false; if (recoveringExit) { exitParked = true; recoveringExit = false; }
                        publish(link, session, "waiting-appearance", "appearance-unconfirmed", budget.used(), resident.pid);
                    }
                } else if (config_.isIdle && config_.isIdle(session))
                    publish(link, session, "idle", "appearance-applied-backend-idle", budget.used());
                else publish(link, session, "waiting-appearance", "appearance-unconfirmed", budget.used());
            }
        }
        closeBackend(); // Directory subscription is also closed if parsing/IO throws.
    }
public:
    ~Runtime() { shutdown(); }
    bool initialize(Config config) {
        std::lock_guard lifecycle(lifecycle_);
        if (worker_.joinable()) return true;
        HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr), stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!wake || !stop) {
            if (wake) CloseHandle(wake); if (stop) CloseHandle(stop);
            std::lock_guard lock(stateMutex_); state_.phase = "failed"; state_.reason = "guard-events-unavailable"; return false;
        }
        { std::lock_guard lock(stateMutex_); config_ = std::move(config); stopping_ = false; uiRecheck_ = modePending_ = resumeRecheck_ = rebindPending_ = false; lastRebindGeneration_ = 0; rebindSession_ = {}; lastResumeGeneration_ = 0; resumeSession_ = {}; wake_ = wake; stop_ = stop; }
        try { worker_ = std::thread([this] { try { run(); } catch (...) {
            std::lock_guard lock(stateMutex_); state_.phase = "failed"; state_.reason = "guard-worker-failed";
        } }); }
        catch (...) {
            std::lock_guard lock(stateMutex_); CloseHandle(wake_); CloseHandle(stop_); wake_ = stop_ = nullptr;
            state_.phase = "failed"; state_.reason = "guard-thread-unavailable"; return false;
        }
        return true;
    }
    void observeSteam(const ymcc::steamsession::Session& session) {
        std::lock_guard lock(stateMutex_);
        if (sameSession(desired_, session)) return;
        desired_ = session; modePending_ = false; if (wake_) SetEvent(wake_);
    }
    // Called on the shared WinEvent observer thread: just latch and wake.
    // Actual mode/CEF IO stays on this worker, coalesced into a finite window.
    void requestUiRefresh() {
        std::lock_guard lock(stateMutex_);
        if (stopping_ || !desired_.pid || !state_.linked || !config_.readMode || uiRecheck_) return;
        uiRecheck_ = true; if (wake_) SetEvent(wake_);
    }
    // Only the shared verified-mode completion calls this. A latest-value
    // mailbox bounds memory/wakes; identity and link are rechecked on the worker.
    void observeMode(const ymcc::steamsession::Session& session, const ModeSample& mode) {
        std::lock_guard lock(stateMutex_);
        if (stopping_ || !desired_.pid || !state_.linked || !config_.readMode || !sameSession(desired_, session) ||
            (mode.mode != UiMode::Desktop && mode.mode != UiMode::BigPicture) || mode.epoch.size() > 96) return;
        const bool wake = !modePending_;
        pendingMode_ = mode; pendingModeSession_ = session; modePending_ = true;
        if (wake && wake_) SetEvent(wake_);
    }
    bool requestResumeRecovery(const ymcc::steamsession::Session& session, ULONGLONG generation) {
        std::lock_guard lock(stateMutex_);
        if (stopping_ || !generation || generation <= lastResumeGeneration_) return false;
        lastResumeGeneration_ = generation; // OFF/absent sessions also consume duplicates.
        if (!desired_.pid || !state_.linked || !config_.readMode || !config_.refreshMode || !sameSession(desired_, session)) return false;
        resumeSession_ = session; const bool wake = !resumeRecheck_; resumeRecheck_ = true;
        if (wake && wake_) SetEvent(wake_);
        return true;
    }
    bool requestReadOnlyRebind(const ymcc::steamsession::Session& session, ULONGLONG generation) {
        std::lock_guard lock(stateMutex_);
        if (stopping_ || !generation || generation <= lastRebindGeneration_ || !desired_.pid ||
            !sameSession(desired_, session) || !config_.confirmExisting) return false;
        lastRebindGeneration_ = generation; rebindSession_ = session;
        const bool wake = !rebindPending_; rebindPending_ = true;
        if (wake && wake_) SetEvent(wake_);
        return true;
    }
    void requestManualRecovery() {
        std::lock_guard lock(stateMutex_); if (stopping_ || !desired_.pid) return;
        manualRecheck_ = true; if (wake_) SetEvent(wake_);
    }
    State snapshot() { std::lock_guard lock(stateMutex_); return state_; }
    void shutdown() {
        std::lock_guard lifecycle(lifecycle_);
        { std::lock_guard lock(stateMutex_); stopping_ = true; if (stop_) SetEvent(stop_); }
        if (worker_.joinable()) worker_.join();
        { std::lock_guard lock(stateMutex_); if (wake_) CloseHandle(wake_); if (stop_) CloseHandle(stop_); wake_ = stop_ = nullptr; desired_ = {}; manualRecheck_ = uiRecheck_ = modePending_ = resumeRecheck_ = rebindPending_ = false; lastRebindGeneration_ = 0; rebindSession_ = {}; resumeSession_ = {}; lastResumeGeneration_ = 0; pendingMode_ = {}; pendingModeSession_ = {}; }
    }
};
} // namespace ymcc::steamcss
