#pragma once
#include "worker_thread_name.h"
// CSSLoader uses the shared Steam session and finite startup probes.
// A bounded startup use reuses the single resident backend or starts a missing one.
// No Steam launch or recurring discovery. Abnormal verified exits have a capped recovery window.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include "process_inventory.h"
#include "steam_session_observer.h"
#include "steam_cssloader_mode.h"
#include "sleep_power_state.h"
#include "decky_sidebar_launch.h"
#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
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
    constexpr DWORD shortCapacity = 512, maxCapacity = 32768;
    wchar_t shortPath[shortCapacity];
    DWORD count = GetFullPathNameW(text.c_str(), shortCapacity, shortPath, nullptr);
    if (!count || count > maxCapacity) return {false, {}, "link-invalid"};
    std::wstring full;
    if (count < shortCapacity) full.assign(shortPath, count);
    else {
        // Insufficient capacity returns the required size including the NUL.
        full.resize(count);
        count = GetFullPathNameW(text.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
        if (!count || count >= full.size()) return {false, {}, "link-invalid"};
        full.resize(count);
    }
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
    constexpr DWORD maxTableBytes = 16 * 1024 * 1024;
    alignas(DWORD) unsigned char local[8192];
    std::vector<unsigned char> grown;
    for (const ULONG family : {2UL, 23UL}) {
        // Share capacity, never rows, between the two independent reads.
        auto* bytes = grown.empty() ? local : grown.data();
        DWORD capacity = grown.empty() ? static_cast<DWORD>(sizeof(local)) : static_cast<DWORD>(grown.size());
        DWORD size = capacity;
        auto status = GetExtendedTcpTable(bytes, &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (status == ERROR_INSUFFICIENT_BUFFER && size > capacity && size <= maxTableBytes) {
            grown.resize(size); bytes = grown.data(); capacity = size;
            status = GetExtendedTcpTable(bytes, &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
        }
        if (status != NO_ERROR || size < sizeof(DWORD) || size > capacity)
            return {nullptr, 0, "blocked", "loader-port-unverified"};
        bool blocked = false;
        const auto accept = [&](DWORD pid, DWORD port) {
            const auto decoded = static_cast<unsigned short>(((port & 0xff) << 8) | ((port >> 8) & 0xff));
            if (decoded != targetPort || pid == exclude) return;
            // Multiple addresses can name the same listener process. Only
            // reuse the held, verified instance while it is still alive.
            if (found.value && pid == foundPid) {
                if (WaitForSingleObject(found.value, 0) != WAIT_TIMEOUT) blocked = true;
                return;
            }
            auto candidate = loaderLease(pid);
            if (!candidate.process) { blocked = true; return; }
            if (found.value) CloseHandle(candidate.process);
            else { found.value = candidate.process; foundPid = candidate.pid; }
        };
        const auto readRows = [&](const auto* table) {
            using Table = std::remove_cv_t<std::remove_pointer_t<decltype(table)>>;
            constexpr auto offset = offsetof(Table, table);
            if (size < offset || table->dwNumEntries > (size - offset) / sizeof(table->table[0])) return false;
            for (DWORD i = 0; i < table->dwNumEntries; ++i) accept(table->table[i].dwOwningPid, table->table[i].dwLocalPort);
            return true;
        };
        const bool read = family == 2 ? readRows(reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(bytes)) :
            readRows(reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(bytes));
        if (!read) return {nullptr, 0, "blocked", "loader-port-unverified"};
        if (blocked) return {nullptr, 0, "blocked", "loader-port-occupied"};
    }
    if (found.value) { HANDLE lease = found.value; found.value = nullptr; return {lease, foundPid, "running", {}, false}; }
    if (listenersOnly) return {nullptr, 0, "missing", {}, false};
    // Candidate discovery belongs to YMCC's shared inventory owner. No private
    // Toolhelp/WMI walk; fresh owner work is confined to this use/event boundary.
    const auto topology = processInventory::read();
    if (!topology || topology->status != processInventory::Status::Complete)
        return {nullptr, 0, "blocked", "loader-processes-unverified"};
    DWORD ourSession = 0; if (!ProcessIdToSessionId(GetCurrentProcessId(), &ourSession)) return {nullptr, 0, "blocked", "loader-owner-unverified"};
    for (const auto& entry : topology->rows) {
        if (entry.pid == exclude || _wcsicmp(entry.name.c_str(), L"CssLoader-Standalone-Headless.exe")) continue;
        DWORD targetSession = 0;
        if (!ProcessIdToSessionId(entry.pid, &targetSession)) return {nullptr, 0, "blocked", "loader-owner-unverified"};
        if (targetSession != ourSession) continue;
        return loaderLease(entry.pid); // PyInstaller parent/child form one family.
    }
    return {nullptr, 0, "missing", {}, false};
}
class Budget {
    std::array<DWORD, 3> delays_;
    ULONGLONG due_ = 0; bool armed_ = false; unsigned used_ = 0;
public:
    explicit Budget(std::array<DWORD, 3> delays = {10000, 30000, 35000}) : delays_(delays) {}
    void begin(ULONGLONG now, bool immediate = false) { used_ = 0; resume(now); if (immediate) due_ = now; }
    void pause() { armed_ = false; }
    void resume(ULONGLONG now, bool immediate = false) { armed_ = used_ < delays_.size(); if (armed_) due_ = immediate ? now : now + delays_[used_]; }
    DWORD timeout(ULONGLONG now) const { return !armed_ ? INFINITE : now >= due_ ? 0 : static_cast<DWORD>(due_ - now); }
    bool take(ULONGLONG now) { if (timeout(now) != 0) return false; ++used_; resume(now); return true; }
    unsigned used() const { return used_; }
};
struct State {
    bool linked = false, steamRunning = false, started = false;
    DWORD pid = 0; unsigned attempts = 0;
    std::string phase = "waiting-steam", reason;
    unsigned automaticRecoveries = 0;
    DWORD steamPid = 0; ULONGLONG steamBirth = 0;
    struct ModeHandoff {
        ModeHandoffPath path = ModeHandoffPath::None;
        std::uint64_t generation = 0, suspendSerial = 0, notificationSequence = 0;
        std::uint64_t readerReturnedTick = 0, publishedTick = 0, enqueuedTick = 0, acceptedTick = 0;
    } modeHandoff;
    bool notifyCard = true;
};
struct ObservedUse {
    HANDLE completion = nullptr;
    std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> identity;
};
struct Config {
    std::wstring linkFile;
    std::function<Result(const Link&, const ymcc::steamsession::Session&, const std::function<bool()>&)> ensure;
    std::array<DWORD, 3> delays{10000, 30000, 35000}; // Test fixtures alone shorten these delays.
    std::function<bool(const ymcc::steamsession::Session&)> isIdle;
    std::function<HANDLE(const ymcc::steamsession::Session&, bool, bool)> confirmIdle;
    std::function<bool(const ymcc::steamsession::Session&)> isResidentConfirmed;
    std::function<ObservedUse(const ymcc::steamsession::Session&, bool, bool, const ymcc::sleeppower::SharedPowerV1*)> confirmObserved;
    std::function<bool(const std::shared_ptr<const ymcc::steamsession::IdentitySnapshot>&)> identityCurrent;
    std::function<Result()> probeExisting; // Same read-only backend boundary; isolated fixtures replace IO.
    std::function<bool(const ymcc::steamsession::Session&)> invalidateBackend; // Revoke old helper/results; retain CSS family.
    std::function<HANDLE(const ymcc::steamsession::Session&)> confirmExisting; // Read-only, never Ensure/reapply/start.
    std::array<DWORD, 3> resumeDelays{0, 30000, 30000}; // Ready now/+30/+60s; fresh shared mode still gates CSS.
    std::function<ULONGLONG()> powerGeneration; // Reject work from an older power episode.
    ymcc::sleeppower::Reader powerReader = nullptr; // Current public scope; no private observation.
    DWORD crashDelayMs = 30000; // Exit-triggered one-shot; isolated fixtures shorten it.
    std::function<bool()> mayLaunch; // Existing application exit fence; never changes user consent.
    std::function<void(const State&)> changed; // Event-only UI recovery; no frontend timer.
    std::function<ModeSample(const ymcc::steamsession::Session&)> readMode;
    std::function<bool(const ymcc::steamsession::Session&)> refreshMode; // Invalidate old cache and schedule the existing shared reader.
    std::function<bool(const ymcc::steamsession::Session&)> hasAccountModeObservation; // Exact-session read already owned by the shared layer.
    std::function<bool(const ymcc::steamsession::Session&)> accountPublicationAhead;
    std::array<DWORD, 3> modeDelays{600, 5000, 30000}; // Debounce / read gap / exhausted-failure gap.
    std::function<bool(const ymcc::steamsession::Session&, const ymcc::sleeppower::SharedPowerV1&, std::string&)> refreshPublicResumeMode;
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
    ymcc::sleeppower::SharedPowerV1 lastResumePower_, resumePower_;
    ymcc::steamsession::Session resumeSession_;
    ModeSample pendingMode_;
    ymcc::steamsession::Session pendingModeSession_;
    void queueMode(const ymcc::steamsession::Session& session, const ModeSample& mode, bool waitingOnly, bool preservePending) {
        std::lock_guard lock(stateMutex_);
        if (stopping_ || !desired_.pid || !state_.linked || !config_.readMode || !(desired_ == session) ||
            (preservePending && modePending_) ||
            (waitingOnly && state_.phase != "waiting-steam-ui" &&
                !(state_.phase == "applying" && state_.reason == "resume-css-recheck")) ||
            (mode.mode != UiMode::Desktop && mode.mode != UiMode::BigPicture) || mode.epoch.size() > 96) return;
        const bool wake = !modePending_;
        pendingMode_ = mode; pendingModeSession_ = session; modePending_ = true;
        // Hand-off replay is not a new observer publication.
        pendingMode_.path = ModeHandoffPath::None;
        pendingMode_.enqueuedTick = 0;
        if (!preservePending && mode.mode == UiMode::BigPicture && lastResumePower_.generation) {
            pendingMode_.path = waitingOnly ? ModeHandoffPath::StableNotice : ModeHandoffPath::ChangedNotice;
            if (mode.timing.matches(lastResumePower_)) pendingMode_.enqueuedTick = GetTickCount64();
        }
        if (wake && wake_) SetEvent(wake_);
    }
    void publish(const Link& link, const ymcc::steamsession::Session& session, const std::string& phase,
                 const std::string& reason, unsigned attempts, DWORD pid = 0, bool started = false,
                 const State::ModeHandoff& handoff = {}, bool notifyCard = true) {
        State copy;
        { std::lock_guard lock(stateMutex_);
          state_ = {link.enabled, session.pid != 0, started, pid, attempts, phase, reason, recoveryCount_, session.pid, session.created, handoff, notifyCard}; copy = state_; }
        try { if (config_.changed) config_.changed(copy); } catch (...) {} // No observer/UI blockage.
    }
    void run() {
        ymcc::threadRole::describe(L"YMCC.CssGuard");
        HANDLE directoryEvent = INVALID_HANDLE_VALUE, backend = nullptr, idleHelper = nullptr;
        bool idleRequested = false, modeCycle = false, linkDirty = true;
        // Keep an exact historical backend receipt for crash admission only.
        // Current DOM readiness is revoked at sleep; that must not erase the
        // fact that THIS same process was previously confirmed in this Steam.
        std::pair<DWORD, ULONGLONG> confirmedBackend{};
        const auto backendIdentity = [](HANDLE process) -> std::pair<DWORD, ULONGLONG> {
            if (!process) return {};
            const auto pid = GetProcessId(process); FILETIME birth{}, exit{}, kernel{}, user{};
            if (!pid || !GetProcessTimes(process, &birth, &exit, &kernel, &user)) return {};
            return {pid, (ULONGLONG(birth.dwHighDateTime) << 32) | birth.dwLowDateTime};
        };
        ExitRecovery recovery(config_.crashDelayMs);
        Budget resumeBudget(config_.resumeDelays);
        ULONGLONG resumeGeneration = 0;
        ymcc::sleeppower::SharedPowerV1 resumePower;
        bool resumeNeedsFreshMode = false, resumeHelperActive = false;
        ymcc::steamsession::Session resumeTarget;
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
        ULONGLONG helperAccount = 0; bool unconfirmedUse = false, accountRetryPending = false;
        std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> helperIdentity;
        // Subscribe before reading the link so an enable/disable cannot be lost.
        watchDirectory();
        if (directoryEvent == INVALID_HANDLE_VALUE) { publish({}, {}, "failed", "link-notification-unavailable", 0); return; }
        for (;;) {
            ymcc::steamsession::Session next, confirmedSession; ModeSample confirmedMode;
            bool manualRecheck = false, uiRecheck = false, hasConfirmedMode = false, resumeRequested = false;
            ymcc::steamsession::Session resumedSession, reboundSession; bool rebindRequested = false;
            ULONGLONG requestedGeneration = 0;
            ymcc::sleeppower::SharedPowerV1 requestedPower;
            {
                std::lock_guard lock(stateMutex_);
                if (stopping_) break;
                next = desired_; manualRecheck = manualRecheck_; manualRecheck_ = false; uiRecheck = uiRecheck_;
                hasConfirmedMode = modePending_;
                if (hasConfirmedMode) {
                    confirmedMode = std::move(pendingMode_); confirmedSession = std::move(pendingModeSession_); modePending_ = false;
                }
                resumeRequested = resumeRecheck_;
                if (resumeRequested) {
                    resumedSession = std::move(resumeSession_); requestedGeneration = lastResumeGeneration_; requestedPower = resumePower_; resumeRecheck_ = false;
                }
                rebindRequested = rebindPending_;
                if (rebindRequested) {
                    reboundSession = std::move(rebindSession_); rebindPending_ = false;
                }
            }
            // Synchronize same-process account metadata without resetting mode,
            // startup/crash budgets, helper ownership or the Steam observation.
            if (initialized && sameSession(next, session)) session.account = next.account;
            // Window/Steam/helper wakes cannot change this file. Refresh on its
            // directory notification; revalidate explicitly at action boundaries.
            const auto nextLink = linkDirty ? readLink(config_.linkFile) : link;
            linkDirty = false;
            if (!initialized || !sameSession(next, session)) confirmedBackend = {};
            if (!initialized || !sameSession(next, session) || manualRecheck) {
                recovery.reset(); recoveryCount_ = 0; exitParked = recoveringExit = exitAwaitingReady = false;
            }
            if (!next.pid || !nextLink.enabled || !(nextLink == link)) { recovery.cancel(); recoveringExit = exitAwaitingReady = false; }
            if (manualRecheck || !next.pid || !nextLink.enabled || !sameSession(next, resumeTarget) ||
                (initialized && !(nextLink == link)) ||
                (config_.powerGeneration && resumeGeneration != config_.powerGeneration()) ||
                (!resumePower.generation && config_.mayLaunch && !config_.mayLaunch())) {
                resumeBudget.pause(); resumeGeneration = 0; resumeNeedsFreshMode = resumeHelperActive = false; resumePower = {};
            }
            if (resumeRequested && (!exitParked || exitAwaitingReady) && next.pid && nextLink.enabled && sameSession(next, resumedSession) &&
                (!config_.powerGeneration || requestedGeneration == config_.powerGeneration()) &&
                (!requestedPower.generation || ymcc::sleeppower::powerCurrent(config_.powerReader, requestedPower))) {
                resumeTarget = next; resumeGeneration = requestedGeneration; resumePower = requestedPower; resumeNeedsFreshMode = true; resumeHelperActive = false;
                resumeBudget.begin(GetTickCount64());
                budget.pause(); modeWindow.park(); hasConfirmedMode = false;
                publish(nextLink, next, "waiting-steam-ui", "resume-css-settle-delay", 0);
            }
            const auto awaitingResumeDesktop = [&] {
                return resumeGeneration && resumePower.generation && !resumeNeedsFreshMode && resumeBudget.used() &&
                    resumeBudget.timeout(GetTickCount64()) != INFINITE && config_.readMode && next.pid && nextLink.enabled &&
                    sameSession(next, resumeTarget) && nextLink == link && !manualRecheck && !rebindRequested &&
                    !idleHelper && !readOnlyHandoff && !unconfirmedUse && !exitParked && !recoveringExit && !recovery.pending();
            };
            const auto parkDesktopResume = [&](const ModeSample& mode) {
                if (mode.mode != UiMode::Desktop || !mode.timing.matches(resumePower)) return false;
                std::lock_guard lock(stateMutex_);
                if (stopping_ || !(desired_ == next) || resumeRecheck_ || manualRecheck_ || rebindPending_ ||
                    !ymcc::sleeppower::powerCurrent(config_.powerReader, mode.timing.power)) return false;
                // Stop only the remaining CSS timers, not the public observer,
                // the mode edge tracker or an independently requested use.
                resumeBudget.pause();
                return true;
            };
            // An unchanged Desktop may publish after the last cache probe and
            // send no mode-change callback. Try the cache before invalidating it.
            if (awaitingResumeDesktop() && resumeBudget.timeout(GetTickCount64()) == 0 &&
                !(hasConfirmedMode && confirmedSession == next && confirmedMode.mode == UiMode::BigPicture)) {
                try {
                    auto cached = config_.readMode(next);
                    if (parkDesktopResume(cached)) {
                        hasConfirmedMode = true; confirmedSession = next; confirmedMode = std::move(cached);
                    }
                } catch (...) {}
            }
            // A completed manual edit gets its existing read-only handoff first.
            // Refused handoff leaves the original resume slot untouched.
            const bool resumeRecheck = !rebindRequested && resumeGeneration && resumeBudget.take(GetTickCount64()) &&
                (!exitParked || exitAwaitingReady) &&
                next.pid && nextLink.enabled && sameSession(next, resumeTarget);
            if (resumeRecheck && exitAwaitingReady) { exitAwaitingReady = false; exitParked = false; recoveringExit = true; }
            if (resumeRecheck && resumePower.generation && !ymcc::sleeppower::powerCurrent(config_.powerReader, resumePower)) {
                // UNKNOWN consumes only the existing finite slot; it cannot authorize
                // stale work, erase remaining slots or mint a new retry wave.
                publish(nextLink, next, "waiting-steam-ui", "resume-public-power-unavailable", resumeBudget.used());
                continue;
            }
            if (resumeRecheck) {
                // Require a current public observation before any ensure.
                // Window/power callbacks never run CDP or launch a process.
                bool requested = false;
                std::string reusedEpoch;
                try {
                    if (resumePower.generation && config_.refreshPublicResumeMode)
                        requested = config_.refreshPublicResumeMode(next, resumePower, reusedEpoch);
                    else requested = config_.refreshMode && config_.refreshMode(next);
                } catch (...) {}
                if (!requested) {
                    budget.pause(); modeWindow.reset();
                    if (resumeBudget.timeout(GetTickCount64()) == INFINITE) resumeNeedsFreshMode = false;
                    publish(nextLink, next, "waiting-steam-ui", "resume-mode-refresh-unavailable", resumeBudget.used());
                    continue;
                }
                // Record the same current document that this Ready use may apply.
                // A late delivery of that document must not create a new edge.
                hasConfirmedMode = !reusedEpoch.empty(); resumeNeedsFreshMode = false;
                if (hasConfirmedMode) {
                    confirmedSession = next;
                    confirmedMode = {UiMode::BigPicture, std::move(reusedEpoch)};
                }
            }
            if (!initialized || manualRecheck || resumeRecheck || !(nextLink == link) || !sameSession(next, session)) {
                const bool changed = !initialized || !(nextLink == link) || !sameSession(next, session);
                if (manualRecheck || resumeRecheck) { unconfirmedUse = accountRetryPending = false; readOnlyHandoff = false; handoffModePending = false; idleRequested = false; if (idleHelper) { CloseHandle(idleHelper); idleHelper = nullptr; } }
                if (changed) { unconfirmedUse = accountRetryPending = false; readOnlyHandoff = false; handoffModePending = false; modeWindow.reset(); waitingForMode = false; modeCycle = false; idleRequested = false; if (idleHelper) { CloseHandle(idleHelper); idleHelper = nullptr; } }
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
            const auto allowed = [&](const ymcc::sleeppower::SharedPowerV1* attempt = nullptr) {
                std::lock_guard lock(stateMutex_);
                return !stopping_ && sameSession(desired_, session) && (!config_.mayLaunch || config_.mayLaunch()) &&
                    (!resumePower.generation || ymcc::sleeppower::powerCurrent(config_.powerReader, resumePower)) &&
                    (!attempt || (!resumeRecheck_ && ymcc::sleeppower::powerCurrent(config_.powerReader, *attempt)));
            };
            if (rebindRequested && session.pid && link.enabled && sameSession(session, reboundSession) &&
                allowed() && config_.confirmExisting && readLink(config_.linkFile) == link) {
                HANDLE confirmation = nullptr;
                try { confirmation = config_.confirmExisting(session); } catch (...) {}
                // A newer click may already own the use. A refused handoff must
                // not discard the backend HANDLE we were successfully observing.
                if (confirmation) {
                    // The user's later edit and read-only DOM verification now
                    // own completion. A delayed wake cannot invalidate that use.
                    resumeBudget.pause(); resumeGeneration = 0; resumeNeedsFreshMode = resumeHelperActive = false;
                    budget.pause(); modeWindow.park(); recovery.cancel(); waitingForMode = false;
                    exitAwaitingReady = recoveringExit = false; exitParked = true; modeCycle = false;
                    if (idleHelper) CloseHandle(idleHelper);
                    closeBackend(); idleRequested = true; idleHelper = confirmation;
                    unconfirmedUse = accountRetryPending = false;
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
            // The exact use can lose its public identity while account/PID and
            // document remain unchanged. Its existing completion wakes this worker.
            bool identityRetired = false;
            bool awaitingAccountPublication = false;
            if (unconfirmedUse && helperIdentity && config_.identityCurrent) {
                try { identityRetired = !config_.identityCurrent(helperIdentity); } catch (...) {}
            }
            if (unconfirmedUse && budget.used() < config_.delays.size() &&
                (helperAccount != session.account || identityRetired) && session.pid && link.enabled &&
                !exitParked && !readOnlyHandoff && !resumeNeedsFreshMode && config_.readMode && allowed()) {
                const auto accountAhead = [&] {
                    try { return identityRetired && config_.accountPublicationAhead && config_.accountPublicationAhead(session); }
                    catch (...) { return false; }
                };
                awaitingAccountPublication = accountAhead();
                bool joined = false;
                bool refreshed = !identityRetired;
                if (identityRetired && !awaitingAccountPublication) {
                    if (helperAccount != session.account && config_.hasAccountModeObservation) {
                        try { joined = config_.hasAccountModeObservation(session); } catch (...) {}
                    }
                    if (joined) refreshed = true;
                    else { try { refreshed = config_.refreshMode && config_.refreshMode(session); } catch (...) {} }
                    // The public account can advance during the rejected refresh.
                    if (!refreshed) awaitingAccountPublication = accountAhead();
                }
                unconfirmedUse = awaitingAccountPublication;
                idleRequested = false; budget.pause();
                accountRetryPending = waitingForMode = refreshed;
                if (awaitingAccountPublication) {
                    // Keep the unused attempt for observeSteam's existing wake.
                    // The old account's mode is not usable while delivery catches up.
                    hasConfirmedMode = false; modeWindow.park();
                    publish(link, session, "waiting-appearance", "appearance-awaiting-account-observation", budget.used());
                } else if (refreshed) {
                    if (identityRetired) hasConfirmedMode = false; // A pre-refresh mailbox cannot admit the successor.
                    if (joined) {
                        // The public owner already retired the old account's mode.
                        // Its new result may have arrived before the old helper finished.
                        try {
                            const auto mode = config_.readMode(session);
                            if (mode.mode == UiMode::BigPicture || mode.mode == UiMode::Desktop) {
                                confirmedMode = mode; confirmedSession = session; hasConfirmedMode = true;
                            }
                        } catch (...) {}
                    }
                    modeWindow.request(GetTickCount64());
                } else publish(link, session, "waiting-appearance", "appearance-identity-refresh-unavailable", budget.used());
            }
            if (!awaitingAccountPublication && session.pid && link.enabled && !exitParked && !resumeNeedsFreshMode && config_.readMode) {
                const bool delivered = hasConfirmedMode && confirmedSession == session;
                if (uiRecheck && !delivered) modeWindow.request(GetTickCount64());
                if (delivered || modeWindow.take(GetTickCount64())) {
                    { std::lock_guard lock(stateMutex_); uiRecheck_ = false; }
                    ModeSample mode;
                    if (allowed()) { if (delivered) mode = confirmedMode; else { try { mode = config_.readMode(session); } catch (...) {} } }
                    // Reading can overlap exit/disable. Never accept a stale read
                    // or let a notification authorize a different Steam process.
                    if (!allowed()) { modeWindow.reset(); continue; }
                    const bool entered = delivered ? modeWindow.confirmed(GetTickCount64(), mode) : modeWindow.finish(GetTickCount64(), mode);
                    if (mode.mode == UiMode::Desktop && awaitingResumeDesktop()) {
                        if (delivered && !mode.timing.publishedTick) {
                            // The ordinary Desktop notification has no Ready
                            // provenance; only the exact-session cache can settle it.
                            try { (void)parkDesktopResume(config_.readMode(session)); } catch (...) {}
                        } else (void)parkDesktopResume(mode);
                    }
                    // A manual request can arrive before Steam reaches BPM.
                    // Its old-mode/unknown probe parked the budget; the first
                    // confirmed BPM must resume it even when ModeWindow already
                    // coalesced that manual edge. No second use after success.
                    if (entered || (waitingForMode && mode.mode == UiMode::BigPicture)) {
                        waitingForMode = false;
                        if (!(readLink(config_.linkFile) == link)) { linkDirty = true; modeWindow.reset(); continue; }
                        readOnlyHandoff = false; handoffModePending = false; modeCycle = true; idleRequested = false;
                        if (idleHelper) { CloseHandle(idleHelper); idleHelper = nullptr; }
                        closeBackend();
                        if (accountRetryPending && !entered) {
                            budget.resume(GetTickCount64(), true);
                            publish(link, session, budget.timeout(GetTickCount64()) == INFINITE ? "waiting-appearance" : "applying",
                                "appearance-account-recheck", budget.used());
                        } else {
                            unconfirmedUse = accountRetryPending = false; budget.begin(GetTickCount64(), true);
                            State::ModeHandoff handoff;
                            if (resumeGeneration && resumePower.generation && mode.mode == UiMode::BigPicture &&
                                (!delivered || mode.path != ModeHandoffPath::None)) {
                                const bool sameScope = mode.timing.matches(resumePower);
                                handoff = {delivered ? mode.path : ModeHandoffPath::CacheFallback, resumePower.generation,
                                    resumePower.suspendSerial, resumePower.observedNotificationSequence,
                                    sameScope ? mode.timing.readerReturnedTick : 0, sameScope ? mode.timing.publishedTick : 0,
                                    delivered && sameScope ? mode.enqueuedTick : 0, GetTickCount64()};
                            }
                            // This edge immediately enters the bounded guard probe below.
                            publish(link, session, "applying", "bigpicture-entered", 0, 0, false, handoff, false);
                        }
                    }
                }
            }
            if (!exitParked && !resumeNeedsFreshMode && budget.take(GetTickCount64())) {
                if (!(readLink(config_.linkFile) == link)) { linkDirty = true; continue; }
                ymcc::sleeppower::SharedPowerV1 attemptPower;
                const bool scoped = config_.confirmObserved && config_.powerReader;
                if (scoped && (!allowed() || !ymcc::sleeppower::capturePower(config_.powerReader, attemptPower))) {
                    publish(link, session, "waiting-steam-ui", "appearance-public-power-unavailable", budget.used());
                    continue;
                }
                // The next Ready can arrive while ensure probes the existing
                // backend. This attempt must not consume its newer mode proof.
                const auto attemptAllowed = [&] {
                    return allowed(scoped ? &attemptPower : nullptr);
                };
                Result result;
                try { result = config_.ensure ? config_.ensure(link, session, attemptAllowed) : probeBackend(); }
                catch (...) { result = {nullptr, 0, "failed", "guard-check-failed"}; }
                if (!allowed()) {
                    if (result.process) CloseHandle(result.process); budget.pause();
                    publish(link, session, "canceled", "lifecycle-changed", budget.used()); continue;
                }
                if (scoped && !attemptAllowed()) {
                    if (result.process) CloseHandle(result.process);
                    publish(link, session, "canceled", "appearance-ready-scope-changed", budget.used());
                    continue;
                }
                closeBackend(); backend = result.process;
                waitingForMode = result.phase == "waiting-steam-ui";
                if (!modeCycle && !backend && result.phase == "missing" && config_.isIdle && config_.isIdle(session)) {
                    budget.pause(); result.phase = "idle"; result.reason = "appearance-applied-backend-idle";
                }
                if (backend || result.phase == "waiting-manual-steam" || result.phase == "waiting-steam-ui") budget.pause();
                const bool startupCandidate = backend || result.phase == "missing";
                if (!backend && result.phase != "idle" && result.phase != "waiting-manual-steam" && result.phase != "waiting-steam-ui" && budget.timeout(GetTickCount64()) == INFINITE) result.phase = "exhausted";
                if (recoveringExit && result.phase == "exhausted") { exitParked = true; recoveringExit = false; budget.pause(); }
                const bool helperCandidate = startupCandidate && !idleRequested && (config_.confirmObserved || config_.confirmIdle);
                const bool probeOnly = helperCandidate && (result.phase == "running" || result.phase == "missing");
                publish(link, session, result.phase, result.reason, budget.used(), result.pid, result.started, {}, !probeOnly);
                // Exactly one startup use for either external or YMCC Steam.
                // Missing CSS is started only for an enabled verified session;
                // an idle receipt never replenishes it; abnormal exit recovery has its own fixed cap.
                if (helperCandidate && attemptAllowed() && readLink(config_.linkFile) == link) {
                    idleRequested = true; budget.pause();
                    helperIdentity.reset();
                    try {
                        if (config_.confirmObserved) {
                            auto observed = config_.confirmObserved(session, !backend, modeCycle, scoped ? &attemptPower : nullptr);
                            idleHelper = observed.completion; helperIdentity = std::move(observed.identity);
                        } else idleHelper = config_.confirmIdle(session, !backend, modeCycle);
                    } catch (...) { idleHelper = nullptr; helperIdentity.reset(); }
                    if (idleHelper) {
                        helperAccount = session.account; unconfirmedUse = accountRetryPending = false;
                        modeCycle = false;
                        // A fresh helper owns a bounded use, not another wake
                        // attempt. Wait for its real completion without consuming
                        // retry slots, overlapping work or a zero-timeout loop.
                        if (resumeGeneration && !resumeNeedsFreshMode) { resumeBudget.pause(); resumeHelperActive = true; }
                        publish(link, session, "applying", "temporary-css-use", budget.used(), result.pid, !backend);
                    } else {
                        idleRequested = false; budget.resume(GetTickCount64());
                        publish(link, session, "waiting-appearance", "appearance-helper-unavailable", budget.used(), result.pid);
                    }
                } else if (probeOnly) {
                    // No helper owns a later result: retain the original card observation.
                    publish(link, session, result.phase, result.reason, budget.used(), result.pid, result.started);
                }
            }
            HANDLE handles[5]{stop_, wake_}; DWORD count = 2;
            const DWORD directoryIndex = directoryEvent != INVALID_HANDLE_VALUE ? count++ : MAXDWORD;
            if (directoryIndex != MAXDWORD) handles[directoryIndex] = directoryEvent;
            const DWORD backendIndex = backend ? count++ : MAXDWORD;
            if (backend) handles[backendIndex] = backend;
            const DWORD helperIndex = idleHelper ? count++ : MAXDWORD;
            if (idleHelper) handles[helperIndex] = idleHelper;
            const auto result = WaitForMultipleObjects(count, handles, FALSE, std::min({budget.timeout(GetTickCount64()), modeWindow.timeout(GetTickCount64()), recovery.timeout(GetTickCount64()), resumeBudget.timeout(GetTickCount64())}));
            if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) break;
            if (directoryIndex != MAXDWORD && result == WAIT_OBJECT_0 + directoryIndex) {
                linkDirty = true;
                if (!FindNextChangeNotification(directoryEvent)) { FindCloseChangeNotification(directoryEvent); directoryEvent = INVALID_HANDLE_VALUE; watched.clear(); }
                watchDirectory(); // Directory deletion/recreation heals from its existing ancestor, no timer.
            }
            if (backendIndex != MAXDWORD && result == WAIT_OBJECT_0 + backendIndex) {
                DWORD exitCode = 0; const bool exitKnown = GetExitCodeProcess(backend, &exitCode) != FALSE;
                const auto exitedIdentity = backendIdentity(backend);
                bool confirmed = exitedIdentity.first && exitedIdentity == confirmedBackend;
                if (!confirmed) { try { confirmed = config_.isResidentConfirmed && config_.isResidentConfirmed(session); } catch (...) {} }
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
                const bool completedResumeUse = resumeHelperActive; resumeHelperActive = false;
                if (!allowed() || !(readLink(config_.linkFile) == link)) {
                    readOnlyHandoff = handoffModePending = idleRequested = false;
                    resumeBudget.pause(); resumeGeneration = 0; resumeNeedsFreshMode = false;
                    modeWindow.reset();
                    publish(link, session, "canceled", "helper-completion-lifecycle-changed", budget.used());
                    continue;
                }
                const bool completedReadOnly = readOnlyHandoff;
                if (completedReadOnly) {
                    readOnlyHandoff = false; exitParked = false;
                    // The saved sample predates any new delivery still in the mailbox.
                    if (handoffModePending) queueMode(session, handoffMode, false, true);
                    handoffModePending = false;
                }
                if (config_.isResidentConfirmed) {
                    // One rebind at helper completion keeps an exit HANDLE wait,
                    // not a recurring lookup. A failed helper never earns Ready.
                    auto resident = config_.probeExisting ? config_.probeExisting() : probeBackend(); backend = resident.process;
                    if (backend && config_.isResidentConfirmed(session)) {
                        unconfirmedUse = accountRetryPending = false;
                        confirmedBackend = backendIdentity(backend);
                        // A pre-resume helper may finish during the settle
                        // delay. Its old DOM receipt cannot consume the new wave.
                        if (!resumeNeedsFreshMode) { resumeBudget.pause(); resumeGeneration = 0; }
                        if (completedReadOnly) { recovery.reset(); recoveryCount_ = 0; }
                        recoveringExit = false;
                        publish(link, session, resumeNeedsFreshMode ? "waiting-steam-ui" : "ready",
                            resumeNeedsFreshMode ? "resume-css-settle-delay" : "appearance-applied-resident", budget.used(), resident.pid);
                    } else {
                        unconfirmedUse = !completedReadOnly;
                        readOnlyHandoff = false; if (recoveringExit) { exitParked = true; recoveringExit = false; }
                        if (completedResumeUse && resumeGeneration) resumeBudget.resume(GetTickCount64());
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
        { std::lock_guard lock(stateMutex_); config_ = std::move(config); stopping_ = false; uiRecheck_ = modePending_ = resumeRecheck_ = rebindPending_ = false; lastRebindGeneration_ = 0; rebindSession_ = {}; lastResumeGeneration_ = 0; lastResumePower_ = {}; resumePower_ = {}; resumeSession_ = {}; wake_ = wake; stop_ = stop; }
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
        if (desired_ == session) return;
        const bool processChanged = !sameSession(desired_, session);
        // Account discovery is not a new process/startup budget. Still retain
        // the FULL session: the shared mode owner requires the current account.
        desired_ = session; if (processChanged || (modePending_ && !(pendingModeSession_ == session))) modePending_ = false;
        if (wake_) SetEvent(wake_);
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
    void observeMode(const ymcc::steamsession::Session& session, const ModeSample& mode, bool waitingOnly = false) {
        queueMode(session, mode, waitingOnly, false);
    }
    bool requestResumeRecovery(const ymcc::steamsession::Session& session, ULONGLONG generation) {
        std::lock_guard lock(stateMutex_);
        if (stopping_ || !generation || generation <= lastResumeGeneration_) return false;
        lastResumeGeneration_ = generation; lastResumePower_ = {}; resumePower_ = {}; // Legacy generation-only caller.
        if (!desired_.pid || !state_.linked || !config_.readMode || !config_.refreshMode || !sameSession(desired_, session)) return false;
        resumeSession_ = session; const bool wake = !resumeRecheck_; resumeRecheck_ = true;
        if (wake && wake_) SetEvent(wake_);
        return true;
    }
    bool requestPublicResumeRecovery(const ymcc::steamsession::Session& session, const ymcc::sleeppower::SharedPowerV1& power) {
        std::lock_guard lock(stateMutex_);
        if (stopping_ || power.generation < lastResumeGeneration_) return false;
        if (lastResumePower_.generation) {
            if (power.suspendSerial < lastResumePower_.suspendSerial ||
                power.observedNotificationSequence < lastResumePower_.observedNotificationSequence ||
                (power.generation == lastResumePower_.generation &&
                 power.suspendSerial == lastResumePower_.suspendSerial &&
                 power.observedNotificationSequence == lastResumePower_.observedNotificationSequence)) return false;
        }
        if (!ymcc::sleeppower::powerCurrent(config_.powerReader, power)) return false;
        // Consume verified OFF/absent scopes too. Revision-only publication is
        // metadata; only the public notification/suspend boundary grants a wave.
        lastResumeGeneration_ = power.generation; lastResumePower_ = power;
        // The initial Ready publication has no observed sleep boundary. Keep it
        // as a baseline without interrupting the normal startup appearance use.
        if (!power.suspendSerial && !power.observedNotificationSequence) return false;
        if (!desired_.pid || !state_.linked || !config_.readMode || !config_.refreshMode || !sameSession(desired_, session)) return false;
        resumePower_ = power; resumeSession_ = session;
        const bool wake = !resumeRecheck_; resumeRecheck_ = true;
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
        { std::lock_guard lock(stateMutex_); if (wake_) CloseHandle(wake_); if (stop_) CloseHandle(stop_); wake_ = stop_ = nullptr; desired_ = {}; manualRecheck_ = uiRecheck_ = modePending_ = resumeRecheck_ = rebindPending_ = false; lastRebindGeneration_ = 0; rebindSession_ = {}; resumeSession_ = {}; lastResumeGeneration_ = 0; lastResumePower_ = {}; resumePower_ = {}; pendingMode_ = {}; pendingModeSession_ = {}; }
    }
};
} // namespace ymcc::steamcss
