#pragma once
#include "worker_thread_name.h"
#include "steam_listener_executor.h"
#include "steam_source_owner.h"
// Shared, event-driven Steam session observer. No WMI polling, no recurring timer.
#include <windows.h>
#include "process_inventory.h"
#include "steam_session_registry_watch.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cwctype>
#include <functional>
#include <mutex>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "steam_window_event_budget.h"

namespace ymcc::steamsession {
struct Session {
    std::wstring root;
    DWORD pid = 0, account = 0;
    ULONGLONG created = 0; // PID reuse is a new session, too.
    bool operator==(const Session&) const = default;
};
// One immutable publication from the existing event-driven producer. Registry
// readability is distinct from a positive current account: zero is Unknown,
// never proof of logout, and a retained receipt never authorizes a write.
struct IdentitySnapshot {
    Session session;
    int presence = -1;
    DWORD registeredPid = 0;
    LSTATUS rootReadError = ERROR_NOT_READY, registeredPidReadError = ERROR_NOT_READY,
        accountReadError = ERROR_NOT_READY;
    DWORD windowsSession = 0, windowsSessionError = ERROR_NOT_READY;
    bool windowsSessionKnown = false;
    std::uint64_t revision = 0, processEpoch = 0, accountEpoch = 0;
    ULONGLONG observedAt = 0;
    // Raw spelling from the same publisher read; display/offline target lookup only.
    // It is not a current UI account or permission to act on a retained receipt.
    std::wstring registryRoot;
    bool registryAccountCandidateKnown() const noexcept {
        return accountReadError == ERROR_SUCCESS && session.account != 0;
    }
    bool registeredProcessMatches() const noexcept {
        return presence == 1 && session.pid && session.created && !session.root.empty() &&
            rootReadError == ERROR_SUCCESS && registeredPidReadError == ERROR_SUCCESS && registeredPid == session.pid;
    }
    // This is a registry/process correlation only, never verified UI/CM login.
    bool currentRegistryAccountKnown() const noexcept {
        return registryAccountCandidateKnown() && registeredProcessMatches();
    }
};
using IdentityPublished = std::function<void(std::shared_ptr<const IdentitySnapshot>)>;
// A per-request revocation signal, created by the sole in-process publisher.
// It carries facts only: UI/document and final held-process proofs remain with
// the actual operation. It is never reset, renewed or shared across requests.
class IdentityDelegation {
    friend class Observer;
    const std::shared_ptr<const IdentitySnapshot> expected_;
    const DWORD account_;
    const bool liveUiAccount_;
    HANDLE cancel_ = nullptr;
    std::atomic<bool> revoked_{false};
    IdentityDelegation(std::shared_ptr<const IdentitySnapshot> expected,DWORD account,bool liveUiAccount)
        : expected_(std::move(expected)),account_(account),liveUiAccount_(liveUiAccount),
          cancel_(CreateEventW(nullptr,TRUE,FALSE,nullptr)) {}
    bool compatible(const std::shared_ptr<const IdentitySnapshot>& next) const noexcept {
        if(!expected_||!next||!cancel_||revoked_.load(std::memory_order_acquire)||
           next->presence!=1||!next->session.pid||!next->session.created||next->session.root.empty()||
           next->rootReadError!=ERROR_SUCCESS||!next->windowsSessionKnown||
           next->processEpoch!=expected_->processEpoch||
           next->session.root!=expected_->session.root||next->session.pid!=expected_->session.pid||
           next->session.created!=expected_->session.created||next->windowsSession!=expected_->windowsSession)return false;
        // A typed current-UI request retains its independent live account watch.
        // Candidate-only requests require the same readable positive candidate.
        return account_&&(liveUiAccount_||
            (next->accountReadError==ERROR_SUCCESS&&next->session.account==account_));
    }
    void revoke() noexcept {
        revoked_.store(true,std::memory_order_release);
        if(cancel_)SetEvent(cancel_);
    }
public:
    ~IdentityDelegation(){revoke();if(cancel_)CloseHandle(cancel_);}
    IdentityDelegation(const IdentityDelegation&)=delete;
    HANDLE cancellationEvent() const noexcept {return cancel_;}
    bool current() const noexcept {
        return !revoked_.load(std::memory_order_acquire)&&cancel_&&WaitForSingleObject(cancel_,0)==WAIT_TIMEOUT;
    }
    const IdentitySnapshot& expected() const noexcept {return *expected_;}
    DWORD resolvedAccount() const noexcept {return account_;}
    bool liveUiAccount() const noexcept {return liveUiAccount_;}
    bool matches(const std::wstring& root,DWORD pid,ULONGLONG created,DWORD account,DWORD windowsSession,bool liveUiAccount) const noexcept {
        return current()&&expected_&&expected_->windowsSessionKnown&&root==expected_->session.root&&
            pid==expected_->session.pid&&created==expected_->session.created&&account==account_&&
            windowsSession==expected_->windowsSession&&liveUiAccount==liveUiAccount_;
    }
};
// Native qualification reads bind the actual publisher's process/account
// candidate epochs. This is deliberately distinct from a UI-account lease:
// it cannot resolve an account, attest CM/native login, or admit publication.
class NativeReadDelegation final {
    friend class Observer;
    const std::shared_ptr<const IdentitySnapshot> expected_;
    HANDLE cancel_=nullptr;
    std::atomic<bool> revoked_{false};
    explicit NativeReadDelegation(std::shared_ptr<const IdentitySnapshot> expected)
        :expected_(std::move(expected)),cancel_(CreateEventW(nullptr,TRUE,FALSE,nullptr)){}
    bool compatible(const std::shared_ptr<const IdentitySnapshot>& next) const noexcept {
        if(!expected_||!next||!cancel_||revoked_.load(std::memory_order_acquire))return false;
        return next->presence==1&&next->session.pid&&next->session.created&&!next->session.root.empty()&&
            next->rootReadError==ERROR_SUCCESS&&next->windowsSessionKnown&&
            next->processEpoch==expected_->processEpoch&&next->accountEpoch==expected_->accountEpoch&&
            next->session.root==expected_->session.root&&next->session.pid==expected_->session.pid&&
            next->session.created==expected_->session.created&&next->windowsSession==expected_->windowsSession&&
            next->session.account==expected_->session.account&&next->accountReadError==expected_->accountReadError&&
            next->registeredPid==expected_->registeredPid&&next->registeredPidReadError==expected_->registeredPidReadError;
    }
    void revoke() noexcept {revoked_.store(true,std::memory_order_release);if(cancel_)SetEvent(cancel_);}
public:
    ~NativeReadDelegation(){revoke();if(cancel_)CloseHandle(cancel_);}
    NativeReadDelegation(const NativeReadDelegation&)=delete;
    NativeReadDelegation& operator=(const NativeReadDelegation&)=delete;
    // Borrowed, sticky event. Do not CloseHandle or ResetEvent it.
    HANDLE cancellationEvent()const noexcept{return cancel_;}
    bool current()const noexcept{return !revoked_.load(std::memory_order_acquire)&&cancel_&&WaitForSingleObject(cancel_,0)==WAIT_TIMEOUT;}
    const IdentitySnapshot& expected()const noexcept{return *expected_;}
    void cancel()noexcept{revoke();}
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
inline DWORD regNumber(const std::wstring& keyPath, const wchar_t* name, LSTATUS* error = nullptr) {
    DWORD value = 0, size = sizeof(value);
    const auto status = RegGetValueW(HKEY_CURRENT_USER, (keyPath + L"\\ActiveProcess").c_str(), name,
        RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (error) *error = status;
    return status == ERROR_SUCCESS ? value : 0;
}
struct InstallRootFact {
    std::wstring path;
    LSTATUS error=ERROR_NOT_READY;
    bool knownForAction() const noexcept {
        return error==ERROR_SUCCESS||error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND;
    }
};
inline InstallRootFact readRegistryInstallRoot(const std::wstring& keyPath) {
    wchar_t value[32768]{}; DWORD size = sizeof(value);
    const auto status = RegGetValueW(HKEY_CURRENT_USER, keyPath.c_str(), L"SteamPath",
        RRF_RT_REG_SZ, nullptr, value, &size);
    return {status==ERROR_SUCCESS?std::wstring(value):std::wstring{},status};
}
inline std::wstring steamRoot(const std::wstring& keyPath, LSTATUS* error = nullptr, std::wstring* registryRoot = nullptr) {
    auto fact=readRegistryInstallRoot(keyPath);
    if(error)*error=fact.error;
    if(registryRoot)*registryRoot=fact.path;
    auto root=std::move(fact.path);
    std::replace(root.begin(), root.end(), L'/', L'\\');
    std::transform(root.begin(), root.end(), root.begin(), [](wchar_t c) { return std::towlower(c); });
    while (!root.empty() && root.back() == L'\\') root.pop_back();
    return root;
}
inline HANDLE verifiedProcess(DWORD pid, const std::wstring& root, ULONGLONG* created) {
    if (!pid || root.empty()) return nullptr;
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return nullptr;
    std::unique_ptr<void, decltype(&CloseHandle)> owned(h, &CloseHandle);
    wchar_t image[32768]; DWORD length = static_cast<DWORD>(std::size(image));
    FILETIME birth{}, exit{}, kernel{}, user{};
    bool ok = QueryFullProcessImageNameW(h, 0, image, &length) && GetProcessTimes(h, &birth, &exit, &kernel, &user) &&
        WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    if (ok) {
        std::wstring path(image, length);
        std::replace(path.begin(), path.end(), L'/', L'\\');
        ok = _wcsicmp(path.c_str(), (root + L"\\steam.exe").c_str()) == 0;
    }
    if (!ok) return nullptr;
    *created = (static_cast<ULONGLONG>(birth.dwHighDateTime) << 32) | birth.dwLowDateTime;
    return owned.release();
}
// Tri-state process observation for display only: 1=running, 0=confirmed absent,
// -1=unknown. Enumeration failures must never be presented as "Steam stopped".
struct PresenceScan {
    std::shared_ptr<const processInventory::Snapshot> table;
    bool seen = false;
    bool matches(const processInventory::Entry& row) {
        if (_wcsicmp(row.name.c_str(), L"steam.exe")) return false;
        seen = true;
        return true;
    }
};
inline int probeSteamPresence(const PresenceScan& scanned = {}) {
    try {
        const auto topology = processInventory::read();
        if (!topology || topology->status != processInventory::Status::Complete) return -1;
        // Let the owner check its existing freshness and invalidation rules.
        // Only the exact same immutable table can reuse the completed name scan.
        if (topology == scanned.table) return scanned.seen ? 1 : 0;
        for (const auto& row : topology->rows) if (!_wcsicmp(row.name.c_str(), L"steam.exe")) return 1;
        return 0;
    } catch (...) { return -1; } // Discovery failure is Unknown, never stopped.

}
// Memory-only view of the same publication. Queued tasks may keep this cell,
// never an Observer address. Retirement clears it before owner destruction.
class IdentityReadView {
    friend class Observer;
    std::shared_ptr<const std::atomic<std::shared_ptr<const IdentitySnapshot>>> publication_;
    explicit IdentityReadView(std::shared_ptr<const std::atomic<std::shared_ptr<const IdentitySnapshot>>> value)
        :publication_(std::move(value)){}
public:
    std::shared_ptr<const IdentitySnapshot> identitySnapshot() const noexcept {
        return publication_?publication_->load(std::memory_order_acquire):nullptr;
    }
};
using StartupWindowFact=std::function<void(WindowProcessCache&,DWORD,DWORD,const std::wstring&,std::shared_ptr<const IdentitySnapshot>)>;
class Observer {
    const std::wstring registryPath; // Alternate key is used by isolated Win32 tests only.
    std::mutex lifecycle;
    HANDLE stopEvent = nullptr, pokeEvent = nullptr;
    SourceOwner sourceOwner;
    std::mutex exitFinish;
    std::atomic<bool> sourceRunning{false};
    ListenerExecutor factReads;
    std::atomic<bool> exitRetired{false};
    std::thread thread;
    std::atomic<bool> openWindow{false};
    std::atomic<int> presence{-1};
    mutable std::mutex sessionMutex;
    Session cachedSession; // Shared verified PID/birth identity for addon lifecycle consumers.
    std::shared_ptr<const IdentitySnapshot> cachedIdentity;
    // Non-owning: run() clears this under sessionMutex before closing its wait handle.
    HANDLE cachedIdentityProcess = nullptr;
    const std::shared_ptr<std::atomic<std::shared_ptr<const IdentitySnapshot>>> identityPublication =
        std::make_shared<std::atomic<std::shared_ptr<const IdentitySnapshot>>>();
    std::vector<std::weak_ptr<IdentityDelegation>> identityDelegations;
    std::vector<std::weak_ptr<NativeReadDelegation>> nativeReadDelegations;
    std::uint64_t identityRevision = 0, processEpoch = 0, accountEpoch = 0;
    void revokeIncompatibleLocked(const std::shared_ptr<const IdentitySnapshot>& next) noexcept {
        identityDelegations.erase(std::remove_if(identityDelegations.begin(),identityDelegations.end(),
            [&](const auto& weak){const auto lease=weak.lock();if(!lease)return true;
                if(!lease->compatible(next)){lease->revoke();return true;}return false;}),identityDelegations.end());
        nativeReadDelegations.erase(std::remove_if(nativeReadDelegations.begin(),nativeReadDelegations.end(),
            [&](const auto& weak){const auto lease=weak.lock();if(!lease)return true;
                if(!lease->compatible(next)){lease->revoke();return true;}return false;}),nativeReadDelegations.end());
    }
    void retireIdentityLocked() noexcept {
        revokeIncompatibleLocked({});
        cachedIdentityProcess = nullptr;
        cachedIdentity.reset();
        identityPublication->store({},std::memory_order_release);
        ++identityRevision; ++processEpoch; ++accountEpoch;
    }
    void publishIdentity(const Session& next, int observedPresence, DWORD registeredPid,
        LSTATUS rootError, LSTATUS pidError, LSTATUS accountError, HANDLE process, const std::wstring& registryRoot = {}) {
        auto receipt = std::make_shared<IdentitySnapshot>();
        receipt->session = next; receipt->presence = observedPresence;
        receipt->registeredPid = registeredPid; receipt->rootReadError = rootError;
        receipt->registeredPidReadError = pidError; receipt->accountReadError = accountError;
        receipt->registryRoot = registryRoot;
        receipt->observedAt = GetTickCount64();
        std::lock_guard lock(sessionMutex);
        const auto old = cachedIdentity;
        const bool sameProcess = old && old->session.root == next.root &&
            old->session.pid == next.pid && old->session.created == next.created;
        if (!sameProcess) ++processEpoch;
        if (!old || old->session.account != next.account || old->accountReadError != accountError ||
            old->registeredPid != registeredPid || old->registeredPidReadError != pidError ||
            old->rootReadError != rootError) ++accountEpoch;
        if (sameProcess && old->windowsSessionKnown) {
            receipt->windowsSession = old->windowsSession;
            receipt->windowsSessionKnown = true; receipt->windowsSessionError = ERROR_SUCCESS;
        } else if (process && next.pid) {
            DWORD session = 0; SetLastError(ERROR_SUCCESS);
            if (ProcessIdToSessionId(next.pid, &session) && WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
                receipt->windowsSession = session; receipt->windowsSessionKnown = true;
                receipt->windowsSessionError = ERROR_SUCCESS;
            } else receipt->windowsSessionError = GetLastError() ? GetLastError() : ERROR_NOT_READY;
        }
        receipt->revision = ++identityRevision;
        receipt->processEpoch = processEpoch; receipt->accountEpoch = accountEpoch;
        if (!(cachedSession == next)) cachedUiWindow = {};
        revokeIncompatibleLocked(receipt); // Revoke before a replacement publication becomes observable.
        cachedSession = next;
        cachedIdentity = std::move(receipt);
        cachedIdentityProcess = process;
        identityPublication->store(cachedIdentity,std::memory_order_release);
        presence.store(observedPresence, std::memory_order_release);
    }
    UiWindowCandidate cachedUiWindow; // Guarded by sessionMutex, never used to authorize actions.
    WindowProcessCache windowPids; std::wstring windowRoot;
    WindowHintWindow uiHints;
    ULONGLONG windowPokedAt = 0; bool windowPoked = false;
    bool windowRegistryBacked = false; // Observer thread only.
    std::function<void()> uiChanged; // Lightweight notification only; never CDP on this observer thread.
    using Changed = std::function<void(bool)>; // true=session change, false=bounded retry
    // Some Steam starts keep ActiveProcess.pid at zero and never change the
    // account/root values. A verified top-level window event is a startup wake,
    // not consent to write registry state or a recurring process scan.
    inline static thread_local Observer* windowObserver = nullptr;
    inline static thread_local const StartupWindowFact* startupWindowCallback=nullptr;
    static bool windowPathPartEquals(std::wstring_view path, std::wstring_view expected) noexcept {
        if (path.size() != expected.size()) return false;
        for (size_t i = 0; i < path.size(); ++i) {
            if ((path[i] == L'/' ? L'\\' : std::towlower(path[i])) != static_cast<wint_t>(expected[i])) return false;
        }
        return true;
    }
    static void CALLBACK windowEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) {
        try {
        auto* self = windowObserver;
        if (!self || !window || object != OBJID_WINDOW || child != CHILDID_SELF || GetAncestor(window, GA_ROOT) != window) return;
        const auto identity = self->identitySnapshot();
        if (!identity) return;
        const auto& session = identity->session;
        const auto& root = session.root;
        if (root.empty()) return; // Registry subscription initializes root; no per-window registry search.
        if (root != self->windowRoot) { self->windowRoot = root; self->windowPids.clear(); self->windowPoked = false; }
        DWORD pid = 0; GetWindowThreadProcessId(window, &pid); if (!pid) return;
        const auto now = GetTickCount64(); unsigned kind = pid == session.pid ? 1 : 0;
        if (!kind) kind = self->windowPids.classifyHeld(pid, now, [&](std::wstring_view path, HANDLE process) -> unsigned {
            constexpr std::wstring_view steam = L"\\steam.exe", cef = L"\\bin\\cef\\", helper = L"\\steamwebhelper.exe";
            if (path.size() < root.size()) return 0;
            const auto relative = path.substr(root.size());
            // The receipt root is already normalized. Reject unrelated suffixes
            // before reading the root; keep the cached raw image for CREATE.
            if (windowPathPartEquals(relative, steam) && windowPathPartEquals(path.substr(0, root.size()), root)) {
                FILETIME birth{}, exit{}, kernel{}, user{};
                if (GetProcessTimes(process, &birth, &exit, &kernel, &user) &&
                    WaitForSingleObject(process, 0) == WAIT_TIMEOUT) return 1;
            } else if (self->uiChanged && relative.size() >= helper.size() &&
                windowPathPartEquals(relative.substr(relative.size() - helper.size()), helper) &&
                windowPathPartEquals(relative.substr(0, cef.size()), cef) &&
                windowPathPartEquals(path.substr(0, root.size()), root)) return 2;
            return 0;
        });
        // This cache admits NOTIFICATIONS only. All actions still verify the
        // session/process birth/owner. No token/image query for every name event.
        if (!kind) {
            const auto* callback=startupWindowCallback;
            if(event==EVENT_OBJECT_CREATE)ymcc::steamgamefacts::startupdiagnostic::ledger().note(pid,ymcc::steamgamefacts::startupdiagnostic::Stage::Observer,callback&&*callback?"create-callback-present":"create-callback-missing");
            if(callback&&*callback)self->windowPids.knownCreate(pid,event,[&](const std::wstring& image){(*callback)(self->windowPids,pid,event,image,identity);});
            return;
        }
        if (kind == 2) {
            std::lock_guard lock(self->sessionMutex);
            if (self->cachedSession == session) {
                if (event == EVENT_OBJECT_DESTROY) {
                    if (self->cachedUiWindow.window == window) self->cachedUiWindow = {};
                } else if (event != EVENT_OBJECT_HIDE) self->cachedUiWindow = {window, pid, now};
                // HIDE retains the background candidate; another hidden window cannot replace it.
            }
        }
        // A current registration has both exact registry notifications and a
        // process exit wait. Keep the window as a UI hint, not a second read.
        const bool redundantDiscovery = self->windowRegistryBacked && pid == session.pid &&
            identity->currentRegistryAccountKnown();
        if (kind == 1 && !redundantDiscovery && (event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_SHOW) && self->pokeEvent &&
            (!self->windowPoked || now - self->windowPokedAt >= 1000)) {
            self->windowPoked = true; self->windowPokedAt = now; SetEvent(self->pokeEvent);
        }
        if (self->uiChanged) self->uiHints.request(now);
        } catch (...) {
            // A failed notification is not action authority. A later event may retry.
        }
    }
    struct WindowSubscription {
        HWINEVENTHOOK hook = nullptr, names = nullptr;
        Observer* owner;
        explicit WindowSubscription(Observer* value,const StartupWindowFact& fact) : owner(value) {
            startupWindowCallback=&fact;
            windowObserver = owner; owner->uiHints.clear(); owner->windowPids.clear(); owner->windowRegistryBacked = false;
            hook = SetWinEventHook(EVENT_OBJECT_CREATE, owner->uiChanged ? EVENT_OBJECT_HIDE : EVENT_OBJECT_SHOW, nullptr, windowEvent,
                0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            if (owner->uiChanged) names = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE,
                nullptr, windowEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        }
        ~WindowSubscription() { if (names) UnhookWinEvent(names); if (hook) UnhookWinEvent(hook); owner->uiHints.clear(); owner->windowPids.clear(); owner->windowRegistryBacked = false; windowObserver = nullptr; startupWindowCallback=nullptr; }
    };
    void dropRunProcess(HANDLE& process) noexcept {
        if (!process) return;
        {
            std::lock_guard lock(sessionMutex);
            cachedIdentityProcess = nullptr;
        }
        CloseHandle(std::exchange(process, nullptr));
    }
    static bool matchesEpoch(const IdentitySnapshot& current, const IdentitySnapshot& expected) noexcept {
        return expected.processEpoch == current.processEpoch && expected.accountEpoch == current.accountEpoch &&
            expected.session == current.session && expected.presence == current.presence;
    }
    void run(const Changed& changed, const std::function<bool()>& needsRetry, const IdentityPublished& onIdentityPublished,const StartupWindowFact& onStartupWindowFact) {
        ymcc::threadRole::describe(L"YMCC.SteamSession");
        RegistryWatch rootWatch(registryPath);
        RegistryWatch rootNames(registryPath, REG_NOTIFY_CHANGE_NAME, true);
        RegistryWatch activeWatch(registryPath + L"\\ActiveProcess");
        auto armRootFallback = [&] {
            rootWatch.armWithChildFallback(!activeWatch.waitHandle(), !rootNames.exactArmed());
        };
        HANDLE process = nullptr;
        struct RunResources {
            Observer& owner;
            HANDLE& process;
            ~RunResources() { owner.dropRunProcess(process); }
        } resources{*this,process};
        Session previous;
        bool havePrevious = false;
        DWORD previousRegisteredPid = 0;
        RetryWindow retries;
        std::shared_ptr<const IdentitySnapshot> lastSuccessfullyNotified;
        auto notifyIdentity = [&](const std::shared_ptr<const IdentitySnapshot>& receipt) {
            if (!onIdentityPublished || !receipt) return;
            const auto& last = lastSuccessfullyNotified;
            if (last && last->session == receipt->session && last->presence == receipt->presence &&
                last->processEpoch == receipt->processEpoch && last->accountEpoch == receipt->accountEpoch &&
                last->windowsSessionKnown == receipt->windowsSessionKnown &&
                (!receipt->windowsSessionKnown || last->windowsSession == receipt->windowsSession)) return;
            // Only a successful delivery advances the semantic notification receipt.
            // Existing inspect events can retry the same immutable cached publication.
            try { onIdentityPublished(receipt); lastSuccessfullyNotified = receipt; } catch (...) {}
        };
        auto inspect = [&](bool force = false) {
            LSTATUS rootError = ERROR_NOT_READY, accountError = ERROR_NOT_READY, pidError = ERROR_NOT_READY;
            std::wstring registryRoot;
            Session next{steamRoot(registryPath, &rootError, &registryRoot)};
            next.account = regNumber(registryPath, L"ActiveUser", &accountError);
            const DWORD registeredPid = regNumber(registryPath, L"pid", &pidError);
            const auto oldReceipt = identitySnapshot();
            const bool sameRegistryStatus = oldReceipt && oldReceipt->rootReadError == rootError &&
                oldReceipt->registeredPidReadError == pidError && oldReceipt->accountReadError == accountError &&
                oldReceipt->registryRoot == registryRoot;
            if (!force && havePrevious && sameRegistryStatus && next.root == previous.root && next.account == previous.account &&
                registeredPid == previousRegisteredPid && (!process || WaitForSingleObject(process, 0) == WAIT_TIMEOUT)) {
                notifyIdentity(oldReceipt);
                return false;
            }
            previousRegisteredPid = registeredPid;
            PresenceScan scanned;
            if (process && previous.root == next.root && (!registeredPid || registeredPid == previous.pid) &&
                WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
                next.pid = previous.pid; next.created = previous.created;
            } else {
                dropRunProcess(process);
                process = verifiedProcess(registeredPid, next.root, &next.created);
                if (process) next.pid = registeredPid;
                else if (!next.root.empty()) {
                    // Discovery only on startup/notification/user action, never on an idle clock.
                    try { scanned.table = processInventory::read(); } catch (...) {}
                    if (scanned.table && scanned.table->status == processInventory::Status::Complete) {
                        for (const auto& row : scanned.table->rows) {
                            if (!scanned.matches(row)) continue;
                            ULONGLONG birth = 0;
                            HANDLE candidate = verifiedProcess(row.pid, next.root, &birth);
                            if (!candidate) continue;
                            if (process) { // Ambiguous same-install sessions: do not guess.
                                CloseHandle(candidate); CloseHandle(process); process = nullptr;
                                next.pid = 0; next.created = 0; break;
                            }
                            process = candidate; next.pid = row.pid; next.created = birth;
                        }
                    }
                }
            }
            // Reuse the verified live handle; otherwise probe only on session events,
            // startup or bounded retries. The input/render path only reads this cache.
            publishIdentity(next, process ? 1 : probeSteamPresence(scanned), registeredPid, rootError, pidError, accountError, process, registryRoot);
            const auto receipt = identitySnapshot();
            if (!havePrevious || !(next == previous)) {
                previous = next; havePrevious = true;
                { std::lock_guard lock(sessionMutex); cachedUiWindow = {}; }
                windowPids.clear(); // Same-install PID/birth/account changes invalidate event classification too.
                retries.begin(GetTickCount64());
                try { changed(true); } catch (...) {}
                notifyIdentity(receipt);
                return true;
            }
            notifyIdentity(receipt);
            return false;
        };
        WindowSubscription windows(this,onStartupWindowFact); // Its callbacks run only on this existing observer thread.
        rootNames.arm();
        activeWatch.arm();
        // Keep combined/recursive coverage when a narrower registration is unavailable.
        armRootFallback();
        inspect(true);
        for (;;) {
            // A busy message stream must not starve stop, pending hints or retries.
            if (WaitForSingleObject(stopEvent, 0) != WAIT_TIMEOUT) break;
            if (uiHints.take(GetTickCount64()) && uiChanged) { try { uiChanged(); } catch (...) {} }
            if (retries.take(GetTickCount64()) && needsRetry()) {
                // Re-probe once even if Windows messages keep the queue busy.
                if (!inspect(true)) { try { changed(false); } catch (...) {} }
            }
            HANDLE handles[6]{stopEvent, pokeEvent}; DWORD count = 2;
            auto addWatch = [&](const RegistryWatch& watch) -> DWORD {
                if (const HANDLE handle = watch.waitHandle()) { handles[count] = handle; return count++; }
                return MAXDWORD;
            };
            const DWORD namesIndex = addWatch(rootNames);
            const DWORD rootIndex = addWatch(rootWatch);
            const DWORD activeIndex = addWatch(activeWatch);
            const DWORD processIndex = process ? count++ : MAXDWORD;
            if (process) handles[processIndex] = process;
            windowRegistryBacked = process && rootWatch.exactArmed() && activeWatch.exactArmed();
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
            if (result == WAIT_OBJECT_0 + namesIndex) {
                // A renamed child can leave the previous key handle valid.
                rootNames.arm();
                activeWatch.rebind();
                armRootFallback();
            } else if (result == WAIT_OBJECT_0 + rootIndex) {
                rootWatch.arm();
                if (!rootNames.exactArmed()) {
                    // The combined fallback can still report a name change.
                    rootNames.arm();
                    activeWatch.rebind();
                }
                armRootFallback();
            } else if (result == WAIT_OBJECT_0 + activeIndex) {
                activeWatch.arm();
                armRootFallback();
            } else if (result == WAIT_OBJECT_0 + 1) {
                if (!activeWatch.waitHandle()) activeWatch.arm();
                armRootFallback();
                if (openWindow.exchange(false)) retries.begin(GetTickCount64());
            }
            inspect(result == WAIT_OBJECT_0 + 1);
        }
    }
    // Called with lifecycle held, after the prior worker actually terminated.
    // A rejected close keeps its exact handle and blocks a replacement run.
    bool closeRunEvents() noexcept {
        bool closed = true;
        for (HANDLE* event : {&stopEvent, &pokeEvent}) {
            if (!*event) continue;
            if (CloseHandle(*event)) *event = nullptr;
            else closed = false;
        }
        return closed;
    }
public:
    explicit Observer(std::wstring keyPath = L"Software\\Valve\\Steam") : registryPath(std::move(keyPath)) {}
    ~Observer() { finishFactReadsForExit(); }
    bool start(Changed changed, std::function<bool()> needsRetry, std::function<void()> onUiChanged = {}, IdentityPublished onIdentityPublished = {},StartupWindowFact onStartupWindowFact={}) {
        std::lock_guard<std::mutex> lock(lifecycle);
        if(exitRetired.load(std::memory_order_acquire))return false;
        if (thread.joinable()) {
            if (sourceRunning.load(std::memory_order_acquire)) return true;
            // sourceRunning is revoked before retirement callbacks finish.
            // Observe actual termination; never wait for them under lifecycle.
            if (WaitForSingleObject(thread.native_handle(), 0) != WAIT_OBJECT_0) return false;
            thread.join();
            openWindow.store(false);
        }
        if (!closeRunEvents()) return false;
        if(!sourceOwner.claim())return false;
        presence.store(-1, std::memory_order_release);
        uiChanged = std::move(onUiChanged);
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        pokeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!stopEvent || !pokeEvent) {
            closeRunEvents(); return false;
        }
        if(!factReads.resume()){closeRunEvents();return false;}
        sourceOwner.resumeReads();
        sourceRunning.store(true,std::memory_order_release);
        try { thread = std::thread([this, changed = std::move(changed), needsRetry = std::move(needsRetry),
            onIdentityPublished = std::move(onIdentityPublished),onStartupWindowFact=std::move(onStartupWindowFact)] {
            try { run(changed, needsRetry, onIdentityPublished,onStartupWindowFact); } catch (...) {}
            sourceOwner.suspendReads();
            sourceRunning.store(false,std::memory_order_release);
            factReads.suspend();
            presence.store(-1, std::memory_order_release);
            { std::lock_guard lock(sessionMutex); cachedSession = {}; cachedUiWindow = {}; retireIdentityLocked(); }
            // Worker retirement is Unknown, never a logout; complete before stop joins.
            if (onIdentityPublished) { try { onIdentityPublished(nullptr); } catch (...) {} }
        }); }
        catch (...) { sourceOwner.suspendReads();sourceRunning.store(false,std::memory_order_release);factReads.suspend(); closeRunEvents(); return false; }
        return true;
    }
    bool ownsSource()const noexcept{return sourceOwner.owned();}
    bool canRetryOwnedStartup()const noexcept{return sourceOwner.owned()&&!exitRetired.load(std::memory_order_acquire);}
    SourceOwner::Read sourceRead(){
        std::unique_lock owner(lifecycle,std::try_to_lock);
        if(!owner.owns_lock()||!sourceActive()||exitRetired.load(std::memory_order_acquire))return {};
        return sourceOwner.read();
    }
    bool sourceReadCurrent(const SourceOwner::Read& reading){
        std::unique_lock owner(lifecycle,std::try_to_lock);
        return owner.owns_lock()&&sourceActive()&&!exitRetired.load(std::memory_order_acquire)&&reading.currentFor(sourceOwner);
    }
    // One fresh registry fact from the resident owner. Startup need not wait
    // for identity publication, and this does not refresh process inventory.
    InstallRootFact readInstallRootForAction(){
        const auto reading=sourceRead();
        if(!sourceReadCurrent(reading))return {};
        auto fact=readRegistryInstallRoot(registryPath);
        if(!sourceReadCurrent(reading))return {};
        return fact;
    }
    bool sourceActive()const noexcept{return sourceOwner.owned()&&sourceRunning.load(std::memory_order_acquire);}
    // Unknown before startup/after stop or an observer failure. No scan or locks here.
    int steamPresence() const { return presence.load(std::memory_order_acquire); }
    Session sessionSnapshot() const { std::lock_guard lock(sessionMutex); return cachedSession; }
    IdentityReadView identityReadView() const noexcept {return IdentityReadView(identityPublication); }
    // Memory-only consumption; nullptr is Unknown before startup/after teardown.
    std::shared_ptr<const IdentitySnapshot> identitySnapshot() const {
        std::lock_guard lock(sessionMutex); return cachedIdentity;
    }
    bool sameIdentityEpoch(const std::shared_ptr<const IdentitySnapshot>& expected) const {
        const auto current = identitySnapshot();
        return expected && current && matchesEpoch(*current,*expected);
    }
    // Advisory only. Never transfers a handle or authorizes a Steam action.
    bool liveIdentityHint(const SourceOwner::Read& reading,
        const std::shared_ptr<const IdentitySnapshot>& expected) const {
        if (!expected || expected->presence != 1 || !expected->processEpoch) return false;
        std::lock_guard lock(sessionMutex);
        return cachedIdentityProcess && cachedIdentity && matchesEpoch(*cachedIdentity,*expected) &&
            WaitForSingleObject(cachedIdentityProcess,0)==WAIT_TIMEOUT &&
            sourceActive() && reading.currentFor(sourceOwner);
    }
    // Called while constructing one already-admitted typed request. UI evidence
    // is supplied by that operation, never invented by this registry publisher.
    std::shared_ptr<IdentityDelegation> delegateIdentity(
        const std::shared_ptr<const IdentitySnapshot>& expected,DWORD resolvedAccount,bool liveUiAccount) {
        try {
            if(!expected||!resolvedAccount||!expected->processEpoch||!expected->windowsSessionKnown)return {};
            auto lease=std::shared_ptr<IdentityDelegation>(new IdentityDelegation(expected,resolvedAccount,liveUiAccount));
            std::lock_guard lock(sessionMutex);
            if(!lease->compatible(cachedIdentity))return {};
            if(!liveUiAccount&&expected->accountEpoch!=cachedIdentity->accountEpoch)return {};
            identityDelegations.erase(std::remove_if(identityDelegations.begin(),identityDelegations.end(),
                [](const auto& weak){return weak.expired();}),identityDelegations.end());
            identityDelegations.push_back(lease);
            return lease;
        } catch(...) {return {};}
    }
    // A separate process/account-candidate lifetime for Native qualification.
    // Unknown/zero account candidates remain Unknown; no UI or Native login is
    // manufactured. Revocation follows the actual process/account epochs.
    std::shared_ptr<NativeReadDelegation> delegateNativeRead(
        const std::shared_ptr<const IdentitySnapshot>& expected) {
        try {
            if(!expected||!expected->processEpoch||!expected->accountEpoch||!expected->windowsSessionKnown)return {};
            const auto published=identitySnapshot();
            if(!published||!published->processEpoch||!published->accountEpoch||!published->windowsSessionKnown)return {};
            auto lease=std::shared_ptr<NativeReadDelegation>(new NativeReadDelegation(published));
            if(!lease->compatible(expected))return {}; // A caller cannot replace publisher metadata.
            std::lock_guard lock(sessionMutex);
            if(!lease->compatible(cachedIdentity))return {};
            nativeReadDelegations.erase(std::remove_if(nativeReadDelegations.begin(),nativeReadDelegations.end(),
                [](const auto& weak){return weak.expired();}),nativeReadDelegations.end());
            nativeReadDelegations.push_back(lease);return lease;
        }catch(...){return {};}
    }
    // One source slot, separate from identity publication. Read/done captures
    // must own their state: only the caller stack may borrow this Observer.
    bool postFactRead(std::function<void()> read,std::function<void()> done={}) {
        std::unique_lock owner(lifecycle,std::try_to_lock);
        return owner.owns_lock()&&sourceActive()&&thread.joinable()&&factReads.post(std::move(read),std::move(done));
    }
    // Coalesced identity hints never refill startup/recovery budgets.
    bool requestIdentityRefresh() {
        // A coalesced hint may be declined while start/stop owns lifecycle.
        // Never make a producer callback wait for the thread that joins it.
        std::unique_lock lock(lifecycle, std::try_to_lock);
        return lock.owns_lock() && sourceActive() && pokeEvent && thread.joinable() && SetEvent(pokeEvent) != FALSE;
    }
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
    // Process exit only. Ordinary OFF/restart keeps the single live slot owned.
    void finishFactReadsForExit(){std::lock_guard finishing(exitFinish);exitRetired.store(true,std::memory_order_release);stop();factReads.finishForExit();sourceOwner.drain();sourceOwner.close();}
    void stop() {
        std::lock_guard<std::mutex> lock(lifecycle);
        sourceOwner.suspendReads();
        sourceRunning.store(false,std::memory_order_release);
        factReads.suspend();
        if (stopEvent) SetEvent(stopEvent);
        if (thread.joinable()) thread.join();
        closeRunEvents(); openWindow.store(false);
        { std::lock_guard sessionLock(sessionMutex); cachedSession = {}; cachedUiWindow = {}; retireIdentityLocked(); }
        presence.store(-1, std::memory_order_release);
    }
};
}
