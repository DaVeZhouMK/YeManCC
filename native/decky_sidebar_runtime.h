#pragma once
#include "worker_thread_name.h"
// YMCC Decky sidebar: opt-in process ownership only. No game/fan/TDP actions.
// One worker owns event/receipt-driven recovery; idle30s health fallback, active5s shared readonly evidence, no business replay.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "steam_session_observer.h"
#include "decky_sidebar_temp.h"
#include "decky_sidebar_launch.h"
#include "decky_sidebar_recovery.h"
#include "decky_bigpicture_recovery.h"
#include "steam_command_coordinator.h"
#include "decky_sidebar_health.h"
#include "steam_failure_diagnostics.h"
#include "steam_ui_mode_cache_miss.h"
#include "steam_ui_epoch.h"
#include "decky_binding_process_owner.h"
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
#include "decky_binding_client.h"
#endif
#include <array>
#include <memory>
#include <filesystem>
#include <functional>
#include <atomic>
#include <stdexcept>
#include <cwctype>
#include <cwchar>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <cstring>
#include <thread>
#include <vector>

namespace ymcc::deckysidebar {
enum class LoaderPathState { unknown, absent, file };
inline LoaderPathState observeLoaderPath(const std::wstring& path) noexcept {
    const auto attrs=GetFileAttributesW(path.c_str());
    if(attrs!=INVALID_FILE_ATTRIBUTES){
        return (attrs&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))?LoaderPathState::unknown:LoaderPathState::file;
    }
    const auto error=GetLastError();
    return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?LoaderPathState::absent:LoaderPathState::unknown;
}
// This is a cold-start observation only. Do not change the shared launcher:
// an unknown token must still reach its existing checks, and no result grants launch.
inline bool coldDefaultElevatedShellObserved() noexcept {
    try {
        ymcc::deckylaunch::Handle caller;
        DWORD size=0;TOKEN_ELEVATION elevation{};TOKEN_ELEVATION_TYPE type{};
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&caller.value)||
           !GetTokenInformation(caller.value,TokenElevation,&elevation,sizeof(elevation),&size)||!elevation.TokenIsElevated||
           !GetTokenInformation(caller.value,TokenElevationType,&type,sizeof(type),&size)||type!=TokenElevationTypeDefault)return false;
        const auto window=GetShellWindow();DWORD pid=0;
        if(!window||!GetWindowThreadProcessId(window,&pid)||!pid)return false;
        ymcc::deckylaunch::Handle process,token;
        process.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
        if(!process.value)return false;
        wchar_t image[32768],windows[MAX_PATH];DWORD imageSize=static_cast<DWORD>(_countof(image));
        if(!QueryFullProcessImageNameW(process.value,0,image,&imageSize))return false;
        const auto windowsSize=GetWindowsDirectoryW(windows,static_cast<UINT>(_countof(windows)));
        if(!windowsSize||windowsSize>=_countof(windows)||
           _wcsicmp(image,(std::filesystem::path(windows)/L"explorer.exe").c_str()))return false;
        TOKEN_TYPE tokenType{};DWORD observed=0;
        return OpenProcessToken(process.value,TOKEN_QUERY,&token.value)&&
            GetTokenInformation(token.value,TokenType,&tokenType,sizeof(tokenType),&size)&&tokenType==TokenPrimary&&
            GetTokenInformation(token.value,TokenElevation,&elevation,sizeof(elevation),&size)&&elevation.TokenIsElevated&&
            GetShellWindow()==window&&GetWindowThreadProcessId(window,&observed)&&observed==pid&&
            WaitForSingleObject(process.value,0)==WAIT_TIMEOUT;
    }catch(...){return false;}
}
// Host-side launch configuration for the OFFICIAL Loader; no changes to upstream Python.
// Its optional interpreter probe must not activate Microsoft Store aliases. This changes
// only our child PATH, keeps real Python/search entries, and never edits the user's PATH.
inline std::wstring officialLoaderSearchPath(const std::wstring& source) {
    std::wstring result; bool first=true;
    for(size_t at=0;at<=source.size();) {
        const auto end=source.find(L';',at);
        const auto part=source.substr(at,end==std::wstring::npos?source.size()-at:end-at);
        auto normalized=part;
        if(normalized.size()>=2&&normalized.front()==L'"'&&normalized.back()==L'"')normalized=normalized.substr(1,normalized.size()-2);
        for(auto& ch:normalized){if(ch==L'/')ch=L'\\';ch=static_cast<wchar_t>(towlower(ch));}
        while(!normalized.empty()&&normalized.back()==L'\\')normalized.pop_back();
        if(!normalized.ends_with(L"\\microsoft\\windowsapps")) {
            if(!first)result+=L';';result+=part;first=false;
        }
        if(end==std::wstring::npos)break;at=end+1;
    }
    return result;
}

// Read-only owned Job membership hints. Callers must verify PID/birth/path.
struct DiagnosticProcessIds {
    static constexpr DWORD capacity = 16;
    bool available = false, truncated = false;
    DWORD error = ERROR_NOT_READY, count = 0;
    std::array<DWORD, capacity> pids{};
};
// Snapshot of the already-held Loader HANDLE at diagnostic receipt time only.
// No process search/open, ownership grant, recovery decision or durable state.
struct DiagnosticLoaderOwner {
    const char* state="unknown"; DWORD error=ERROR_NOT_READY,pid=0;
    ULONGLONG birth=0; bool exitCodeKnown=false; DWORD exitCode=0;
};
struct DiagnosticRuntimeThreadCpu {
    DWORD id=0;
    ULONGLONG birth=0,kernel=0,user=0;
};
struct AdmissionWaitLog {
    DWORD steamPid=0;
    ULONGLONG steamBirth=0, power=0;
    unsigned repeated=0;
    static constexpr unsigned checkpoint=64;
    bool detailOnly() const noexcept { return repeated>0&&repeated<checkpoint; }
};
struct State {
    bool enabled = false;
    bool steamKnown = false;
    bool steamRunning = false;
    DWORD pid = 0;
    unsigned long long revision = 0;
    std::string phase = "disabled";
    std::string reason;
    DWORD error = 0;
    bool ownedBindingDestroyed = true;
    DWORD ownedBindingCleanupError = 0;
    bool bindingRetry = false;
    unsigned bindingAttempts = 0;
    std::string launchStage;
    std::string launchMode;
    bool admissionRetry = false;
    unsigned admissionAttempts = 0; // Initial check plus scheduled reads; event wakes do not refill the fast window.
    std::string admissionDetail;
    ULONGLONG admissionElapsedMs = 0;
    DWORD admissionNextRetryMs = 0;
    bool admissionRecovery = false;
    std::optional<ULONGLONG> connectionExhaustedAtPower;
    bool connectionExhaustedOfficialBackend = false;
    AdmissionWaitLog admissionWaitLog; // Notification copy only; never part of the UI snapshot.
};
// Proof to reuse work, never permission to launch. No wire or log fields.
struct LaunchBindingScope {
    steamsession::Session steam;
    DWORD account=0;
    ULONGLONG power=0;
    std::string modeEpoch;
    bool verified() const {return steam.pid&&steam.created&&!steam.root.empty()&&account&&power&&steamui::parseModeEpoch(modeEpoch).valid;}
    bool operator==(const LaunchBindingScope&) const = default;
};
struct LaunchAdmission {
    bool allowed;
    std::string reason;
    bool retryable;
    std::string detail;
    bool bindingOnly = false; // Exact retained frontend: no official Loader process authority.
    ymcc::steamui::ModeCacheMiss modeCacheMiss;
    DWORD modeSteamPid = 0;
    ULONGLONG modeSteamBirth = 0;
    ULONGLONG modePowerGeneration = 0;
    LaunchBindingScope bindingScope;
    // Legacy bool callbacks remain observation-only when false.
    LaunchAdmission(bool permit = true, std::string why = {}, bool retry = false, std::string context = {}, bool retained = false)
        : allowed(permit), reason(why.empty() && !permit ? "ymcc-restart-observation-only" : std::move(why)),
          retryable(retry), detail(std::move(context)), bindingOnly(retained) {}
};
class AdmissionWindow {
    ymcc::steamsession::RetryWindow retries_;
    unsigned long long epoch_ = 0;
    bool known_ = false, active_ = false, parked_ = false;
    ULONGLONG opened_ = 0, slowDue_ = 0;
    unsigned attempts_ = 0;
    AdmissionWaitLog logScope_;
    unsigned logKinds_ = 0, logRepeats_ = 0;
    void resetLog() noexcept { logScope_={}; logKinds_=logRepeats_=0; }
public:
    static constexpr DWORD recoveryIntervalMs = 30000;
    void blocked(unsigned long long epoch, ULONGLONG now, bool retryable) {
        if (!known_ || epoch_ != epoch) {
            resetLog();
            known_ = true; epoch_ = epoch; attempts_ = 1; retries_ = {};
            opened_ = now; slowDue_ = 0; active_ = retryable; parked_ = !retryable;
            if (retryable) retries_.begin(now);
        } else if (!retryable) {
            // A loaded/desktop permission refusal parks only this observer's
            // clock. It grants no work while refused, and cannot erase retry
            // debt before a later independently verified pending mode.
            if(active_){active_=false;parked_=true;}
        } else if(parked_) {
            parked_=false;active_=true;
            // An initially terminal mode has no fast window to replenish.
            // Continue at the existing slow recovery cadence instead.
            if(retries_.timeout(now)==INFINITE&&!slowDue_)slowDue_=now+recoveryIntervalMs;
        }
        // Window events do not refill or postpone either recovery stage.
    }
    DWORD timeout(ULONGLONG now) const {
        if (!active_) return INFINITE;
        const auto fast = retries_.timeout(now);
        if (fast != INFINITE) return fast;
        return !slowDue_ ? INFINITE : slowDue_ <= now ? 0 :
            static_cast<DWORD>((std::min)(slowDue_ - now, static_cast<ULONGLONG>(MAXDWORD - 1)));
    }
    bool take(ULONGLONG now) {
        if (!active_) return false;
        if (retries_.take(now)) {
            ++attempts_;
            if (retries_.timeout(now) == INFINITE) slowDue_ = now + recoveryIntervalMs;
            return true;
        }
        if (!slowDue_ || now < slowDue_) return false;
        slowDue_ = now + recoveryIntervalMs; ++attempts_; return true;
    }
    unsigned attempts() const { return attempts_; }
    ULONGLONG elapsed(ULONGLONG now) const { return active_ && now >= opened_ ? now - opened_ : 0; }
    bool recovering() const { return active_ && slowDue_ != 0; }
    AdmissionWaitLog logWait(const LaunchAdmission& observation,bool continuing) noexcept {
        const unsigned kind=observation.reason=="steam-ui-state-pending"&&observation.detail=="shared-mode-pending"&&observation.retryable?1u:
            observation.reason=="steam-bigpicture-required"&&observation.detail=="desktop"&&!observation.retryable?2u:0u;
        // Parking on a confirmed desktop preserves the existing slow window.
        if(!slowDue_||!kind||observation.allowed||observation.bindingOnly||!observation.modeSteamPid||
           !observation.modeSteamBirth||!observation.modePowerGeneration){resetLog();return {};}
        if(!continuing||logScope_.steamPid!=observation.modeSteamPid||logScope_.steamBirth!=observation.modeSteamBirth||
           logScope_.power!=observation.modePowerGeneration){
            resetLog();logScope_={observation.modeSteamPid,observation.modeSteamBirth,observation.modePowerGeneration};
        }
        auto log=logScope_;
        if(logKinds_&kind){
            log.repeated=++logRepeats_;
            if(logRepeats_==AdmissionWaitLog::checkpoint)logRepeats_=0;
        }else logKinds_|=kind;
        return log;
    }
    void stop() { retries_ = {}; slowDue_ = 0; active_ = false; parked_ = false; resetLog(); }
    void completed() { stop(); known_ = false; attempts_ = 0; }
};

enum class AdmissionRefreshReceipt { none, notified, retainedRequested, retainedPending, finalQueued, finalPending };
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
enum class BindingMode { prepare, refresh, retained, retainedExact };
struct BindingPlan {
    nlohmann::json request; std::string reason;
    std::shared_ptr<ymcc::steamsession::IdentityDelegation> delegation;
    LaunchBindingScope launchScope;
};
#endif
struct Config {
    std::wstring executable;
    std::wstring home;
    std::function<std::string()> preflight; // Source/hash/plugin/debug-flag checks, startup only.
    std::function<void(const State&)> changed;
    std::function<void()> connectionPresentationChanged;
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
    // Historical callback regressions only; this branch is absent from the product.
    std::function<std::string()> refreshBinding;
    std::function<void()> beginPending;
    std::function<void()> cancelPending;
    std::function<std::string()> prepareBinding; // Before launch, separate from resource validation.
    std::function<HANDLE()> takeContextProcess; // Transfers a verified read-only process lease; never a kill target.
#else
    std::shared_ptr<ymcc::deckybinding::Client> ownedBinding;
    // Parent broker changes must check the borrowed current-intent predicate.
    // Bootstrap/watch transport still executes in the owned child.
    std::function<BindingPlan(BindingMode, std::uint64_t, const std::function<bool()>&)> bindingPlan;
    std::function<void(const nlohmann::json&)> bindingDiagnostic; // Legacy owned-row callback, retained for existing consumers.
    // Preferred in-process consumer: both arguments are borrowed for this call only.
    // Keep the original packet immutable; append metadata only in an already-owned output.
    std::function<void(const nlohmann::json&,const char*)> bindingDiagnosticBorrowed;
#endif
    std::function<LaunchAdmission()> launchAdmission; // Verify current Steam session and actual UI mode; never start Steam.
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
    std::function<std::string()> retainedBinding; // Transport-only attach; must recheck exact actual-target frontend.
#endif
    std::function<void()> connectionRecovery; // Existing worker, bounded document budget.
    std::function<DWORD()> connectionRecoveryTimeout; // Finite only for active cleanup grace or owned-backend proof; healthy state INFINITE.
    std::function<void()> cancelConnectionRecovery; // Latch the exact final-cleanup child before OFF/session/exit joins.
    // Existing worker only. Resource/debug preparation grants no Loader or binding authority.
    // Callback must honor canContinue before each side effect and bound its own IO.
    std::function<std::string(const std::function<bool()>&)> prepareEnvironment;
    // Append-only optional port: preserve existing Config aggregate consumers.
    // Cached verified mode + authenticated broker only; no external IO on healthy reads.
    std::function<ConnectionHealth()> connectionHealth;
    std::function<nlohmann::json(ConnectionRecoveryWindow::Action)> connectionStep; // Original inert/legacy consumer.
    std::function<std::shared_ptr<steamcommand::Reservation>(ConnectionRecoveryWindow::Action)> reserveConnectionStep;
    // Reserved private recovery calls beforeEffect synchronously before releasing the first Steam command grant.
    std::function<nlohmann::json(ConnectionRecoveryWindow::Action,const std::shared_ptr<steamcommand::Reservation>&,const std::function<bool()>&)> connectionStepReserved;
    // Optional diagnostics only: cached shared identity, no discovery or authority.
    // Appended for existing aggregate callers. Exceptions never affect recovery.
    std::function<ymcc::steamfailure::FailureContext()> failureContext;
    std::function<void(const nlohmann::json&,const ymcc::steamfailure::FailureContext&)> failureDiagnostic;
    // Borrowed for this synchronous call only. Stops IO when its launch is revoked.
    std::function<std::string(const std::function<bool()>&)> scopedPreflight;
};
class Runtime {
    std::mutex lifecycleMutex_, stateMutex_, ownedLeaseMutex_;
    // Duplicated handles to THIS Runtime's suspended/Job-assigned child only.
    // Stop can revoke the child before joining an uncooperative binding callback;
    // worker-owned handles and its stack still require cooperative completion.
    HANDLE stopJob_ = nullptr, stopProcess_ = nullptr;
    std::thread worker_;
    struct WakeState {
        std::mutex mutex; // Publication/signal/close only; never held across Client calls or joins.
        HANDLE event = nullptr; // Non-owning alias; wake_ retains the original HANDLE ownership.
        std::uint64_t generation = 0;
    };
    const std::shared_ptr<WakeState> wakeState_ = std::make_shared<WakeState>();
    HANDLE wake_ = nullptr;
    bool stopping_ = false;
    std::atomic<bool> reporting_{true};
    std::atomic<bool> exitFenced_{false}; // Confirmed process/session exit only; never a recoverable outage.
    std::atomic<bool> ownedBindingDestroyed_{true};
    std::atomic<DWORD> ownedBindingCleanupError_{ERROR_SUCCESS};
    State lastPublished_; bool hasPublished_ = false;
    unsigned long long intent_ = 0;
    unsigned long long lastBindingGeneration_ = 0;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
    std::uint64_t bindingRequestGeneration_ = 0; // Per-attempt IPC identity; never a retry-budget or power-generation reset.
    ymcc::deckybinding::DiagnosticContextProcess diagnosticContext_; // Protected by stateMutex_.
    unsigned long long diagnosticContextIntent_=0;
#endif
    unsigned long long lastPreparationGeneration_ = 0, preparationEpoch_ = 0;
    bool preparationActive_ = false; // Existing worker callback only; guarded by stateMutex_.
    ymcc::deckyhealth::PendingStop pendingSafetyStop_; // Exact current binding; normal owner-thread OFF still owns persistence.
    std::string activeBindingRun_; // Published only after typed Client admits this binding request.
    ymcc::steamfailure::FailureContext activeBindingFailure_; // Original admitted action, guarded by stateMutex_.
    bool transportRecoveryRequested_ = false;
    bool connectionCheckRequested_ = false;
    std::optional<unsigned long long> connectionPublicationIntent_; // Current read or parked for shared proof; guarded by stateMutex_.
    inline static std::atomic<std::uint64_t> failureActionSerial_{0};
    std::uint64_t failureEpisode_=0; // Existing owner worker only; diagnostic correlation.
    bool retainedAdmissionRefreshRequested_ = false; // Observation only; worker owns admission/launch.
    enum class FinalAdmissionWake {idle, reading, queued};
    std::atomic<FinalAdmissionWake> finalAdmissionWake_{FinalAdmissionWake::idle};
    unsigned long long launchIntent_ = 0; // Only user enable or a genuine Steam state/session change.
    unsigned long long crashResetEpoch_ = 0;
    LoaderSessionBudget crashSessions_; // Last verified instance/account, guarded by lifecycleMutex_.
    unsigned long long admissionEpoch_ = 0; // Explicit/session intents renew admission; a completed start can later have a new outage.
    std::atomic<std::uint64_t> retainedLoaderNativeGeneration_{0}; // Existing Owner queue; no cleanup timer.
    Config config_;
    State state_;
    std::string disabledReason_; // Only a reviewed owner-thread safety close can persist a fault reason.
    void closeWake() noexcept {
        std::lock_guard lock(wakeState_->mutex);
        wakeState_->event = nullptr;
        ++wakeState_->generation;
        if(wake_){CloseHandle(wake_);wake_=nullptr;}
    }
    static std::vector<wchar_t> environment(const std::wstring& home,const std::wstring& temporary) {
        std::map<std::wstring, std::wstring> vars;
        LPWCH block = GetEnvironmentStringsW();
        if (!block) throw std::runtime_error("environment-unavailable");
        for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
            std::wstring entry(p); const auto split = entry.find(L'=', entry[0] == L'=' ? 1 : 0);
            if (split != std::wstring::npos) {
                std::wstring key = entry.substr(0, split);
                for (auto& c : key) c = towupper(c);
                vars[key] = entry.substr(split + 1);
            }
        }
        FreeEnvironmentStringsW(block);
        for(auto it=vars.begin();it!=vars.end();) {
            if(_wcsicmp(it->first.c_str(),L"PATH")==0)it->second=officialLoaderSearchPath(it->second);
            if(_wcsicmp(it->first.c_str(),L"DECKY_REPO")==0)it=vars.erase(it);else ++it;
        }
        vars[L"DECKY_REPO"]=L"SteamDeckHomebrew/decky-loader"; // No inherited fork override.
        vars[L"PRIVILEGED_PATH"] = home;
        vars[L"UNPRIVILEGED_PATH"] = home;
        vars[L"SERVER_HOST"] = L"127.0.0.1";
        vars[L"SERVER_PORT"] = L"1337";
        vars[L"LIVE_RELOAD"] = L"0";
        vars[L"TEMP"] = temporary;vars[L"TMP"] = temporary; // Own extraction scope, not global TEMP.
        std::vector<wchar_t> result;
        for (const auto& [key, value] : vars) {
            const auto entry = key + L"=" + value;
            result.insert(result.end(), entry.begin(), entry.end()); result.push_back(L'\0');
        }
        result.push_back(L'\0'); return result;
    }
    void publish(std::string phase, std::string reason = {}, DWORD error = 0, DWORD pid = 0, bool retry = false, unsigned attempts = 0, std::string launchStage = {}, std::string launchMode = {}, bool admissionRetry = false, unsigned admissionAttempts = 0, std::string admissionDetail = {}, ULONGLONG admissionElapsedMs = 0, DWORD admissionNextRetryMs = 0, bool admissionRecovery = false, AdmissionWindow* waitWindow = nullptr, const LaunchAdmission* waitObservation = nullptr) {
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
        // Preserve the former watch owner's inactive-phase teardown. A rejected
        // final admission must not leave a healthy bootstrap child waiting on CEF.
        if(phase!="starting"&&phase!="loader-started"&&phase!="reconnecting"&&phase!="frontend-attached")stopOwnedBinding();
#endif
        State copy;
        { std::lock_guard lock(stateMutex_);
          if(phase=="disabled"&&!state_.enabled&&reason.empty())reason=disabledReason_;
          state_.ownedBindingDestroyed=ownedBindingDestroyed_.load();state_.ownedBindingCleanupError=ownedBindingCleanupError_.load();
          // Registry/window event storms cannot broadcast identical waiting
          // states repeatedly. Include every presentation field, not revision.
          if (hasPublished_ && lastPublished_.enabled == state_.enabled && lastPublished_.steamKnown == state_.steamKnown &&
              lastPublished_.steamRunning == state_.steamRunning && lastPublished_.phase == phase && lastPublished_.reason == reason &&
              lastPublished_.error == error && lastPublished_.pid == pid && lastPublished_.bindingRetry == retry &&
              lastPublished_.bindingAttempts == attempts && lastPublished_.launchStage == launchStage && lastPublished_.launchMode == launchMode &&
              lastPublished_.admissionRetry == admissionRetry && lastPublished_.admissionAttempts == admissionAttempts &&
              lastPublished_.admissionDetail == admissionDetail && lastPublished_.admissionRecovery == admissionRecovery &&
              lastPublished_.connectionExhaustedAtPower==state_.connectionExhaustedAtPower &&
              lastPublished_.connectionExhaustedOfficialBackend==state_.connectionExhaustedOfficialBackend &&
              lastPublished_.ownedBindingDestroyed==state_.ownedBindingDestroyed && lastPublished_.ownedBindingCleanupError==state_.ownedBindingCleanupError) return;
          state_.phase = std::move(phase); state_.reason = std::move(reason);
          state_.error = error; state_.pid = pid; state_.bindingRetry=retry; state_.bindingAttempts=attempts;state_.launchStage=std::move(launchStage);state_.launchMode=std::move(launchMode);
          state_.admissionRetry=admissionRetry;state_.admissionAttempts=admissionAttempts;state_.admissionDetail=std::move(admissionDetail);
          state_.admissionElapsedMs=admissionElapsedMs;state_.admissionNextRetryMs=admissionNextRetryMs;state_.admissionRecovery=admissionRecovery;
          ++state_.revision; copy = state_;
          if(waitWindow&&waitObservation&&copy.enabled&&copy.steamKnown&&copy.steamRunning&&
             copy.ownedBindingDestroyed&&!copy.ownedBindingCleanupError)
              copy.admissionWaitLog=waitWindow->logWait(*waitObservation,hasPublished_&&lastPublished_.admissionWaitLog.steamPid!=0);
          lastPublished_ = copy; hasPublished_ = true; }
        try { if (reporting_.load() && config_.changed) config_.changed(copy); } catch (...) {} // Best-effort presentation only.
    }
    bool launchAllowed(unsigned long long intent) {
        std::lock_guard lock(stateMutex_);
        return !exitFenced_.load(std::memory_order_acquire) && !pendingSafetyStop_.active(intent_) && !stopping_ && state_.enabled && state_.steamKnown && state_.steamRunning && intent_ == intent;
    }
    std::string runLaunchPreflight(unsigned long long intent) {
        try {
            if(config_.scopedPreflight){
                const std::function<bool()> current=[this,intent]{return launchAllowed(intent);};
                return current()?config_.scopedPreflight(current):"preparation-cancelled";
            }
            return config_.preflight?config_.preflight():std::string{};
        } catch(...) { return "preflight-failed"; }
    }
    bool beginPending(unsigned long long intent) {
        std::lock_guard lock(stateMutex_);
        if(exitFenced_.load(std::memory_order_acquire) || pendingSafetyStop_.active(intent_) || stopping_ || !state_.enabled || !state_.steamKnown || !state_.steamRunning || intent_!=intent)return false;
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
        if(config_.beginPending)config_.beginPending();return true;
#else
        return config_.ownedBinding && config_.ownedBinding->enable(intent);
#endif
    }
    void cancelPending() noexcept {
        try {if(config_.cancelConnectionRecovery)config_.cancelConnectionRecovery();}catch(...){}
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
        try { if(config_.cancelPending)config_.cancelPending(); } catch (...) {}
#else
        try { if(config_.ownedBinding)config_.ownedBinding->cancel(); } catch (...) {}
#endif
        // Only this dedicated worker's file checks, never the existing hardware/Steam observer thread.
        // Completion is still checked before any launch; cancellation is not proof an IO was canceled.
        if(worker_.joinable())CancelSynchronousIo(worker_.native_handle());
    }
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
    struct BindingWaitLeases {
        HANDLE ready=nullptr,child=nullptr;
        explicit BindingWaitLeases(const std::shared_ptr<ymcc::deckybinding::Client>& client) {
            if(client)client->duplicateWaitHandles(ready,child);
        }
        ~BindingWaitLeases(){if(ready)CloseHandle(ready);if(child)CloseHandle(child);}
        BindingWaitLeases(const BindingWaitLeases&)=delete;
    };
    bool stopOwnedBinding(DWORD waitMs=1500) noexcept {
        {std::lock_guard lock(stateMutex_);activeBindingFailure_={};diagnosticContext_={};}
        if(!config_.ownedBinding)return true;
        try {
            const auto stopped=config_.ownedBinding->stop(waitMs);
            ownedBindingDestroyed_.store(stopped.fullyDestroyed);
            ownedBindingCleanupError_.store(stopped.fullyDestroyed?ERROR_SUCCESS:stopped.error);
            return stopped.fullyDestroyed;
        }catch(...){ownedBindingDestroyed_.store(false);ownedBindingCleanupError_.store(ERROR_GEN_FAILURE);return false;}
    }
    void retainTerminalForOwner(const nlohmann::json& detail,unsigned long long intent) noexcept {
        try{std::lock_guard lock(stateMutex_);
            if(!stopping_&&state_.enabled&&state_.steamKnown&&state_.steamRunning&&intent_==intent&&
                pendingSafetyStop_.capture(detail,intent,activeBindingRun_)){
                transportRecoveryRequested_=false;retainedAdmissionRefreshRequested_=false;
            }
        }catch(...){}
    }
    void bindingDiagnostic(const nlohmann::json& detail,const char* kind) noexcept {
        // Post-start watch errors belong to the original binding, even if a new
        // shared Steam/power snapshot is already published. Facts only: never
        // change admission, recovery timing, or a packet's normal handling.
        try{if(std::strcmp(kind,"diagnostic")==0&&detail.contains("steamFailure")&&detail["steamFailure"].is_object()){
            ymcc::steamfailure::FailureContext context;
            {std::lock_guard lock(stateMutex_);
                if(activeBindingFailure_.actionId&&detail.value("runId",std::string{})==activeBindingRun_)context=activeBindingFailure_;
            }
            if(context.actionId){context.phase=ymcc::steamfailure::Phase::Observation;failureDiagnostic(detail,context);}
        }}catch(...){}
        try {
            if(config_.bindingDiagnosticBorrowed)config_.bindingDiagnosticBorrowed(detail,kind);
            else {auto row=detail;row["bindingPacketKind"]=kind;if(config_.bindingDiagnostic)config_.bindingDiagnostic(row);}
        }catch(...){}
    }
    // Evaluate optional JSON construction inside this boundary. Existing packet
    // factories return references; logging cannot replace the binding outcome.
    template<class Make>
    void optionalBindingDiagnostic(Make&& make,const char* kind) noexcept {
        try{bindingDiagnostic(make(),kind);}catch(...){}
    }
#endif // Owned binding helpers above; correlation is shared by both build modes.
    ymcc::steamfailure::FailureContext failureContext(ymcc::steamfailure::Operation operation,
        ymcc::steamfailure::Phase phase,std::uint64_t generation,std::uint64_t episode) noexcept {
        ymcc::steamfailure::FailureContext value;
        try{if(config_.failureContext)value=config_.failureContext();}catch(...){}
        value.operation=operation;value.phase=phase;value.generation=generation;value.recoveryEpisode=episode;
        value.actionId=0;return value;
    }
    void failureDiagnostic(const nlohmann::json& receipt,const ymcc::steamfailure::FailureContext& context) noexcept {
        try{if(config_.failureDiagnostic)config_.failureDiagnostic(receipt,context);}catch(...){}
    }
    struct FailureTrace {
        Runtime& owner;ymcc::steamfailure::FailureContext context;bool finished=false;
        FailureTrace(Runtime& value,ymcc::steamfailure::Operation operation,std::uint64_t generation)
            :owner(value),context(owner.failureContext(operation,ymcc::steamfailure::Phase::Started,generation,owner.failureEpisode_)) {
            if(owner.config_.failureDiagnostic){context.actionId=++failureActionSerial_;try{owner.failureDiagnostic(nlohmann::json::object(),context);}catch(...){}}
        }
        void complete(const nlohmann::json& receipt) noexcept {
            if(finished)return;finished=true;context.phase=ymcc::steamfailure::Phase::Finished;
            if(context.actionId)owner.failureDiagnostic(receipt,context);
        }
        ~FailureTrace() noexcept {
            // The operation was entered but its final receipt is missing. This
            // is uncertainty only; never a failure/replay/retry decision.
            if(context.actionId&&!finished){try{complete({{"ok",false},{"uncertain",true},{"reason","action-result-unconfirmed"}});}catch(...){}}
        }
    };
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
    std::string performOwnedBinding(unsigned long long intent,BindingMode mode,bool& revalidateSession,
                                    BindingPlan* completed=nullptr) {
        revalidateSession=false;
        if(!config_.ownedBinding||!config_.bindingPlan)return "owned-binding-required";
        const std::function<bool()> current=[this,intent]{return launchAllowed(intent);};
        if(!current())return "mirror-bootstrap-canceled";
        BindingPlan plan;
        // Demand-only markers isolate parent plan/resource work from the child
        // bootstrap deadline. An unfinished pair diagnoses a blocked parent;
        // neither marker starts recovery or grants any operation authority.
        const char* scope=mode==BindingMode::retainedExact?"binding-plan-retained-exact":mode==BindingMode::retained?"binding-plan-retained":mode==BindingMode::prepare?"binding-plan-prepare":"binding-plan-refresh";
        optionalBindingDiagnostic([&]{return nlohmann::json{{"bootstrapStage","plan-started"},{"scope",scope}};},"diagnostic");
        const auto planStarted=GetTickCount64();
        try {plan=config_.bindingPlan(mode,++bindingRequestGeneration_,current);}
        catch(...){optionalBindingDiagnostic([&]{return nlohmann::json{{"bootstrapStage","plan-finished"},{"scope",scope},{"elapsedMs",GetTickCount64()-planStarted},{"ok",false},{"reason","mirror-binding-request-failed"}};},"diagnostic");return "mirror-binding-request-failed";}
        optionalBindingDiagnostic([&]{return nlohmann::json{{"bootstrapStage","plan-finished"},{"scope",scope},{"elapsedMs",GetTickCount64()-planStarted},{"ok",plan.reason.empty()},{"reason",plan.reason}};},"diagnostic");
        if(!plan.reason.empty())return plan.reason;
        if(mode==BindingMode::retainedExact&&(!plan.request.value("retainedFrontendOnly",false)||
           !plan.request.contains("bootstrap")||plan.request.at("bootstrap").contains("retainedFrontendBuildId")))
            return "mirror-retained-frontend-changed";
        if(!launchAllowed(intent))return "mirror-bootstrap-canceled";
        FailureTrace trace(*this,ymcc::steamfailure::Operation::Binding,intent);
        const auto started=config_.ownedBinding->begin(plan.request,intent,plan.delegation);
        if(!started){
            // This exact status is emitted only before Client attempts the
            // bootstrap request write. The original caller must still prove
            // prior cleanup before using the existing retry.
            if(started.code==ymcc::deckybinding::Code::notStarted&&started.error==ERROR_NOT_ENOUGH_MEMORY){
                revalidateSession=true;
                trace.complete({{"ok",false},{"mutated",false},{"reason","mirror-binding-allocation-unavailable"},{"error",ERROR_NOT_ENOUGH_MEMORY}});
                return "mirror-binding-allocation-unavailable";
            }
            return "mirror-binding-worker-start-failed";
        }
        ownedBindingDestroyed_.store(false);ownedBindingCleanupError_.store(ERROR_SUCCESS);
        {std::lock_guard lock(stateMutex_);
            if(stopping_||!state_.enabled||intent_!=intent)return "mirror-bootstrap-canceled";
            activeBindingRun_=plan.request.at("bootstrap").at("runId").get<std::string>();
            activeBindingFailure_=trace.context;
        }
        const auto deadline=GetTickCount64()+9000; // Existing bootstrap deadline (7s) plus IPC/exit grace.
        for(;;){
            if(!launchAllowed(intent))return "mirror-bootstrap-canceled";
            const auto packet=config_.ownedBinding->next();
            using Packet=decltype(packet);
            if(packet.kind==Packet::Kind::message){
                const auto kind=packet.value.value("kind",std::string{});
                const auto& detail=packet.value.at("detail");
                if(kind=="diagnostic"){retainTerminalForOwner(detail,intent);optionalBindingDiagnostic([&]() -> const nlohmann::json& {return detail;},"diagnostic");continue;}
                if(kind=="result"){
                    trace.complete(detail);
                    optionalBindingDiagnostic([&]() -> const nlohmann::json& {return detail;},"result");
                    // Validate optional reason text without copying before the success return.
                    const auto reasonEntry=detail.find("reason");
                    if(reasonEntry!=detail.end())(void)reasonEntry->get_ref<const std::string&>();
                    if(detail.value("ok",false)){
                        if(completed){completed->launchScope=std::move(plan.launchScope);completed->delegation=plan.delegation;}
                        return {};
                    }
                    const auto reason=detail.value("reason",std::string("mirror-bootstrap-failed"));
                    // A rejected pre-write lease can become valid without a new
                    // observer identity (e.g. ActiveUser briefly unavailable).
                    // Rebuild the plan and verify every identity again. Never
                    // replay a changed/unknown write or grant old-session trust.
                    // These finite-child setup/cleanup failures happened before
                    // the first write. Reuse this existing retry only after the
                    // caller below has proved the prior Client/child returned.
                    const bool beforeWriteFailure=reason=="mirror-bootstrap-session-changed"||
                        reason=="mirror-bootstrap-context-failed"||reason=="mirror-bootstrap-callback-failed"||
                        reason=="mirror-bootstrap-cleanup-incomplete"||
                        ymcc::deckybinding::Client::listenerTransientFailure(reason);
                    revalidateSession=!detail.value("ok",false)&&beforeWriteFailure&&
                        detail.contains("mutated")&&detail["mutated"].is_boolean()&&!detail["mutated"].get<bool>();
                    return detail.value("ok",false)?std::string{}:reason;
                }
                return "mirror-binding-worker-protocol-invalid";
            }
            if(packet.kind==Packet::Kind::ended||packet.kind==Packet::Kind::failed){
                trace.complete({{"ok",false},{"uncertain",true},{"reason",packet.reason}});
                optionalBindingDiagnostic([&]{return nlohmann::json{{"ok",false},{"mutated",nullptr},{"mutationState","unknown"},{"reason",packet.reason}};},"result");
                return packet.reason.empty()?"mirror-bootstrap-context-exited":packet.reason;
            }
            const auto now=GetTickCount64();if(now>=deadline)return "mirror-bootstrap-deadline";
            BindingWaitLeases leases(config_.ownedBinding);
            if(!leases.ready||!leases.child)return "mirror-binding-worker-unavailable";
            HANDLE waits[]{wake_,leases.ready,leases.child};
            const auto result=WaitForMultipleObjects(3,waits,FALSE,static_cast<DWORD>(deadline-now));
            if(result==WAIT_TIMEOUT)return "mirror-bootstrap-deadline";
            if(result==WAIT_FAILED)return "mirror-binding-worker-wait-failed";
        }
    }
#endif
    bool retainOwnedStopLease(HANDLE job, HANDLE process) {
        HANDLE jobLease=nullptr,processLease=nullptr;
        const auto self=GetCurrentProcess();
        if(!DuplicateHandle(self,job,self,&jobLease,0,FALSE,DUPLICATE_SAME_ACCESS))return false;
        if(!DuplicateHandle(self,process,self,&processLease,0,FALSE,DUPLICATE_SAME_ACCESS)){
            const DWORD error=GetLastError();CloseHandle(jobLease);SetLastError(error);return false;
        }
        std::lock_guard lock(ownedLeaseMutex_);
        stopJob_=jobLease;stopProcess_=processLease;return true;
    }
    void releaseOwnedStopLease(bool revoke) noexcept {
        HANDLE job=nullptr,process=nullptr;
        { std::lock_guard lock(ownedLeaseMutex_);
          job=stopJob_;stopJob_=nullptr;process=stopProcess_;stopProcess_=nullptr; }
        if(revoke && job && !TerminateJobObject(job,0) && process){
            // Precise retained process HANDLE, never a PID/name or someone else's Loader.
            // If Job termination fails, the root can still be revoked before join.
            // Descendant/worker-handle cleanup still belongs to closeOwned below.
            TerminateProcess(process,1);
        }
        if(job)CloseHandle(job);
        if(process){if(revoke)WaitForSingleObject(process,1500);CloseHandle(process);}
    }
    static bool retryableBinding(const std::string& reason) {
        // Steam can be present before its own CDP listener exists. Retry only this unavailable connection with capped backoff.
        return recoverableWatchFailure(reason) || reason=="live-debug-unavailable" || reason=="mirror-bootstrap-context-unavailable" || reason=="live-context-unavailable" ||
            reason=="mirror-bootstrap-async-error" || reason=="mirror-bootstrap-deadline" ||
            reason=="mirror-bootstrap-discovery-timeout" || reason=="mirror-bootstrap-context-exited" ||
            reason=="mirror-bootstrap-steam-unavailable" || reason=="steam-session-unconfirmed";
    }
    void run() {
        ymcc::threadRole::describe(L"YMCC.DeckyRuntime");
        HANDLE process=nullptr,job=nullptr,context=nullptr;
        std::unique_ptr<ymcc::deckybinding::detail::State> loaderNativeCleanup;
        LoaderTemporaryRun temporary;
        unsigned long long attempted=~0ULL,launchAttempted=~0ULL;
        unsigned long long preparedEpoch=~0ULL, preparationAttemptEpoch=~0ULL, preparationAttemptIntent=~0ULL;
        std::optional<std::pair<unsigned long long,unsigned long long>> parkedPreparation;
        const auto runPreparation=[&](unsigned long long intent,unsigned long long epoch,unsigned long long admissionEpoch){
            preparationAttemptEpoch=epoch;preparationAttemptIntent=intent;
            const std::function<bool()> canContinue=[this,intent,epoch]{
                std::lock_guard lock(stateMutex_);
                return !stopping_ && state_.enabled && state_.steamKnown && intent_==intent && preparationEpoch_==epoch;
            };
            if(!canContinue())return false;
            // Keep same-source UI hints from retiring this intent between the
            // callback return and recording its preparation result.
            struct PreparationActivity {
                Runtime& owner;
                explicit PreparationActivity(Runtime& value):owner(value){std::lock_guard lock(owner.stateMutex_);owner.preparationActive_=true;}
                ~PreparationActivity(){std::lock_guard lock(owner.stateMutex_);owner.preparationActive_=false;}
            } active(*this);
            if(!canContinue())return false;
            std::string result;
            try { result=config_.prepareEnvironment(canContinue); }
            catch (...) { result="preparation-failed"; }
            if(!canContinue())return false;
            if(result.empty())preparedEpoch=epoch;
            else if(result=="resource-worker-deadline"||result=="resource-worker-cleanup-incomplete"||
                    result=="resource-package-mismatch")
                parkedPreparation=std::pair{epoch,admissionEpoch};
            return true;
        };
        ymcc::steamsession::RetryWindow retries; // Reuse unchanged 1/3/8/20-second budget.
        AdmissionWindow admissionChecks;
        // A cold resource worker may miss its 2.5s deadline even with valid
        // bytes. Reuse the finite 1/3/8/20s budget, only after an admitted launch
        // failed. Do not poll healthy/absent/retained sessions, reset crash
        // budgets, or retry bad hashes/incomplete worker cleanup.
        ymcc::steamsession::RetryWindow resourceRetries;
        unsigned long long resourceAdmissionEpoch=~0ULL,resourcePreparationEpoch=~0ULL;
        bool resourceWindowOpened=false;
        const char* pendingResourceFailure=nullptr;
        // A pending mode read parks the timer, not its absolute deadlines.
        // Consume a slot only at the admitted launch branch, never on a wake.
        const auto cancelResourceRetry=[&]{pendingResourceFailure=nullptr;};
        const auto resourceRetryQueued=[&](ULONGLONG now){
            return pendingResourceFailure&&resourceRetries.timeout(now)!=INFINITE;
        };
        // Both the initial read and the recheck AFTER a deferred bootstrap share
        // this one finite window. A network delay must not turn a later clean
        // resource-worker timeout into a permanent unavailable state, or refill
        // the window on each binding attempt. Only a cleaned timeout or typed
        // memory failure before the resource request write may retry.
        const auto rejectedResources=[&](const std::string& failure){
            if(failure=="resource-worker-deadline"||failure=="resource-worker-prewrite-memory-unavailable"){
                if(!resourceWindowOpened){resourceWindowOpened=true;resourceRetries.begin(GetTickCount64());}
                pendingResourceFailure=failure=="resource-worker-deadline"?"resource-worker-deadline":"resource-worker-prewrite-memory-unavailable";
            }else cancelResourceRetry();
        };
        ConnectionRetryWindow outage,loaderRecovery;
        ConnectionRecoveryWindow connectionFallback;
        bool healthReadUnavailable=false; // Diagnostic edge only; never an action budget.
        LaunchPolicyStop launchPolicy;bool launchPolicyReported=false;
        [[maybe_unused]] bool launchPolicyObservedBeforeCreate=false;
        [[maybe_unused]] bool launchPermissionProbeReported=false;
        bool loaderPathObservedAbsent=false;
        unsigned long long parkedPreparationSeen=~0ULL;
        ymcc::deckylaunch::Result pathLaunchFailure;
        const auto publishLaunchStop=[&]{
            if(launchPolicy.cause()==LaunchPolicyStop::Cause::loaderPathUnavailable)
                publish("failed","launch-failed",pathLaunchFailure.error,0,false,0,pathLaunchFailure.stage,pathLaunchFailure.mode);
            else publish("failed","launch-failed",ERROR_ACCESS_DENIED,0,false,0,"shell-token-elevated");
        };
        bool connectionExhaustionObserved=false;
        const auto reportConnection=[&](ConnectionRecoveryWindow::Action action,const ConnectionHealth& health,ULONGLONG now,unsigned long long intent,bool blocked=false,bool bindingReused=false){
            const bool exhausted=connectionFallback.exhausted();
            if(connectionExhaustionObserved!=exhausted){
                bool changed=false;
                {
                    std::lock_guard lock(stateMutex_);
                    if(intent_==intent&&(!exhausted||(!stopping_&&!exitFenced_.load()&&state_.enabled&&!pendingSafetyStop_.active(intent)))){
                        const auto value=exhausted?std::optional<ULONGLONG>{health.powerGeneration}:std::nullopt;
                        const bool officialBackend=exhausted&&health.reason=="official-backend-unavailable";
                        changed=state_.connectionExhaustedAtPower!=value||state_.connectionExhaustedOfficialBackend!=officialBackend;
                        state_.connectionExhaustedAtPower=value;
                        state_.connectionExhaustedOfficialBackend=officialBackend;
                        if(hasPublished_){lastPublished_.connectionExhaustedAtPower=value;lastPublished_.connectionExhaustedOfficialBackend=officialBackend;}
                    }
                }
                connectionExhaustionObserved=exhausted;
                // Presentation follows the existing worker without revoking a manual repair revision.
                try{if(changed&&reporting_.load()&&config_.connectionPresentationChanged)config_.connectionPresentationChanged();}catch(...){}
            }
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            using Action=ConnectionRecoveryWindow::Action;if(action==Action::none)return;
            try{
            const auto observed=snapshot();const bool recovering=action==Action::reconnect||action==Action::recycle||action==Action::rebuild||action==Action::reopen;
            failureEpisode_=recovering?connectionFallback.episode():0;
            if(config_.failureDiagnostic){
                using Op=ymcc::steamfailure::Operation;using Phase=ymcc::steamfailure::Phase;
                const auto operation=action==Action::reconnect?Op::Reconnect:action==Action::recycle?Op::RecycleLoader:
                    action==Action::rebuild?Op::RebuildDocument:action==Action::reopen?Op::ReopenBigPicture:Op::Observe;
                const auto context=failureContext(operation,recovering?(blocked?Phase::Rejected:Phase::Selected):Phase::Observation,observed.revision,connectionFallback.episode());
                failureDiagnostic({{"reason",blocked&&recovering?"owner-safety-stop":health.reason},{"runtimeReason",observed.reason},
                    {"recoveryTrigger",connectionFallback.trigger()},{"stageBudgetMs",connectionFallback.stageBudget()},
                    {"stageRemainingMs",connectionFallback.remaining(now)},{"connected",health.connected}},context);
            }
            // Verified YMCC controls can end a recovery without an open official socket.
            const char* recoveredStage=health.reason=="sidebar-native-confirmed-official-backend-unavailable"?"decky-sidebar-only":"decky-channels";
            nlohmann::json connectionDetail={{"scope","global-connection"},{"recoveryEpisode",connectionFallback.episode()},
                {"recoveryPhase",action==Action::recovered?"connected":action==Action::stopped?"stopped":action==Action::exhausted?"manual-required":blocked?"blocked":"recovering"},
                {"stage",action==Action::recovered?recoveredStage:action==Action::stopped?"eligibility-lost":action==Action::exhausted?"recovery-exhausted":blocked?"owner-safety-stop":action==Action::reopen?"reopen-bigpicture":action==Action::rebuild?"rebuild-ui-document":action==Action::recycle?(process?"recycle-owned-loader":"no-owned-loader"):"reconnect"},
                {"reason",blocked&&recovering?"owner-safety-stop":health.reason},{"runtimeReason",observed.reason},
                // attempted keeps its original stage-selection meaning. This
                // exact branch fact distinguishes waiting on an existing bind
                // from permission to create another transport or restart UI.
                {"phase",observed.phase},{"pid",observed.pid},{"attempted",recovering&&!blocked},{"bindingReused",bindingReused},
                {"connected",health.connected},{"bindingAttempts",connectionFallback.attempts()},
                {"elapsedMs",connectionFallback.elapsed(now)},{"nextRetryMs",connectionFallback.timeout(now)},
                {"stageBudgetMs",connectionFallback.stageBudget()},{"stageRemainingMs",connectionFallback.remaining(now)},{"recoveryTrigger",connectionFallback.trigger()}};
            if(action==Action::recovered&&health.authenticatedPeerAgeMs)
                connectionDetail["authenticatedPeerAgeMs"]=*health.authenticatedPeerAgeMs;
            if(action==Action::recovered&&health.sharedModeObservationAgeMs)
                connectionDetail["sharedModeObservationAgeMs"]=*health.sharedModeObservationAgeMs;
            if((action==Action::rebuild||action==Action::reopen)&&health.sidebarVerifiedDuringBackendOutage.has_value())
                connectionDetail["sidebarVerifiedDuringBackendOutage"]=*health.sidebarVerifiedDuringBackendOutage;
            bindingDiagnostic(connectionDetail,"diagnostic");
            }catch(...){} // Optional reporting cannot interrupt recovery or teardown.
#endif
        };
        LoaderCrashBudget crashBudget;
        unsigned long long crashEpoch=~0ULL;DWORD lastLoaderExit=0;
        bool outageDue=false,loaderRecoveryDue=false;
        bool outageWatchRequested=false; // Existing retry lease also carries an independent watch fault.
        ULONGLONG loaderStarted=0;bool loaderWasHealthy=false;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
        // Credit a completed, still-live binding to the same reconnect window.
        // Never restart its absolute 7s clock or reuse an older intent/watch.
        ULONGLONG boundStarted=0;unsigned long long boundIntent=~0ULL,boundGeneration=0;DWORD boundPid=0;
#endif
        bool launchPending=false,deferredLaunch=false,retryDue=false;
        unsigned attempts=0;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
        BindingPlan preparedLaunch;
        ULONGLONG heldLaunchUntil=0;
        unsigned long long preparedAt=0;
        const auto pendingPreparedMode=[](const LaunchAdmission& admission){
            if(admission.allowed||!admission.retryable||admission.reason!="steam-ui-state-pending"||
               !admission.modeCacheMiss.sequence||!admission.modeCacheMiss.reason)return false;
            const std::string_view cause=admission.modeCacheMiss.reason;
            // A typed native mutex miss never read Steam. An untyped live-busy
            // (transport/renderer) is not interchangeable with this receipt.
            if(admission.detail=="live-busy")return cause=="native-read-lock";
            // A QAM/probe refresh also clears the observation, not the bound
            // document. Wait for a fresh same-scope admission; never launch on a miss.
            return admission.detail=="shared-mode-pending"&&(cause=="mode-refresh"||cause=="connection-refresh");
        };
#endif
        auto closeContext=[&]{
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            heldLaunchUntil=0;preparedLaunch={};
            if(context){std::lock_guard lock(stateMutex_);diagnosticContext_={};}
#endif
            if(context){CloseHandle(context);context=nullptr;}
        };
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
        ULONGLONG watchAttachedAt=0;
        unsigned long long watchBindingGeneration=0,watchContextGeneration=0;
#endif
        auto stopWindow=[&]{retries={};outage.reset();outageWatchRequested=false;outageDue=false;launchPending=false;deferredLaunch=false;retryDue=false;attempts=0;};
        auto closeOwned=[&](const char* stopCause){
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            // Teardown observations only. The same held root/Job still follows
            // the original stop path; optional logging cannot skip its return.
            const DWORD stopPid=process?GetProcessId(process):0;
            const auto stopStarted=GetTickCount64();const bool stopHadJob=job!=nullptr;
            DWORD stopWait=WAIT_FAILED,stopWaitError=0,stopExitCode=0;bool stopExitKnown=false;
            std::string stopBirth;
            if(process){FILETIME born{},exited{},kernel{},user{};
                if(GetProcessTimes(process,&born,&exited,&kernel,&user)){
                    try{stopBirth=std::to_string((static_cast<ULONGLONG>(born.dwHighDateTime)<<32)|born.dwLowDateTime);}catch(...){}
                }
            }
            const auto reportStop=[&](bool finished,bool jobEmpty) noexcept {
                if(!stopPid)return;
                try{
                    nlohmann::json detail={{"scope","loader-stop"},{"bootstrapStage",finished?"loader-stop-finished":"loader-stop-started"},
                        {"pid",stopPid},{"loaderStopCause",stopCause}};
                    if(!stopBirth.empty())detail["loaderBirth100ns"]=stopBirth;
                    if(finished){
                        detail["elapsedMs"]=GetTickCount64()-stopStarted;
                        detail["loaderStopWait"]=stopWait;detail["loaderStopWaitError"]=stopWaitError;
                        detail["loaderStopRootExited"]=stopWait==WAIT_OBJECT_0;detail["loaderStopExitKnown"]=stopExitKnown;
                        if(stopExitKnown)detail["loaderStopExitCode"]=stopExitCode;
                        detail["loaderStopJobEmptyConfirmed"]=stopHadJob&&jobEmpty;
                        detail["loaderStopBindingDestroyed"]=ownedBindingDestroyed_.load();
                    }
                    bindingDiagnostic(std::move(detail),"diagnostic");
                }catch(...){}
            };
            reportStop(false,false);
#else
            (void)stopCause;
#endif
            releaseOwnedStopLease(false); // Do not keep KILL_ON_JOB_CLOSE alive via a duplicate.
            closeContext();stopWindow();admissionChecks.stop();cancelResourceRetry();
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            stopOwnedBinding();
#endif
            bool empty=true;
            if(job){
                empty=false;
                if(!TerminateJobObject(job,0)){
                    // Last owned Job handle triggers the real kill-on-close fallback NOW,
                    // before the bounded process wait. Do not wait on a live child first.
                    if(CloseHandle(job))job=nullptr;
                }
                const auto until=GetTickCount64()+1500;
                while(job){
                    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
                    if(!QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr))break;
                    if(!info.ActiveProcesses){empty=true;break;}
                    Sleep(5); // Bounded owned-process shutdown only, never idle status polling.
                    if(GetTickCount64()>=until)break;
                }
            }
            if(process){
                const DWORD returned=WaitForSingleObject(process,1500);
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                stopWait=returned;stopWaitError=returned==WAIT_FAILED?GetLastError():ERROR_SUCCESS;
                stopExitKnown=returned==WAIT_OBJECT_0&&GetExitCodeProcess(process,&stopExitCode)!=FALSE;
#else
                (void)returned;
#endif
                CloseHandle(process);process=nullptr;
            }
            if(job){
                if(!CloseHandle(job)){
                    // Allocate before owning the Job, so a refused final close
                    // cannot lose its original native HANDLE during cleanup.
                    auto retained=std::move(loaderNativeCleanup);
                    retained->job.value=job;retained->assigned=true;retained->childExited=true;
                    retained->cleanupError=GetLastError();
                    retained->generation=++ymcc::deckybinding::detail::serial;
                    retainedLoaderNativeGeneration_=retained->generation;
                    ymcc::deckybinding::detail::retain(retained.release());
                }
                job=nullptr;
            }
            // Image sections may outlive exit notifications briefly. Cap cleanup attempts.
            bool cleaned=temporary.path().empty();
            for(unsigned retry=0;empty&&!cleaned&&retry<20;++retry){
                cleaned=temporary.cleanupAfterJobEmpty(true);if(!cleaned)Sleep(50);
            }
            if(!cleaned)temporary.abandon(); // Unknown/live Job or locked file: retain, never widen deletion scope.
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            reportStop(true,empty);
#endif
        };
        auto takeContext=[&](std::string& reason,unsigned long long bindingIntent){
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            (void)bindingIntent;
            if(!config_.takeContextProcess)return;
            HANDLE next=config_.takeContextProcess();
#else
            ymcc::deckybinding::DiagnosticContextProcess diagnostic;
            HANDLE next=config_.ownedBinding?config_.ownedBinding->takeContextProcess(&diagnostic):nullptr;
            if(!next&&reason.empty()){reason="mirror-bootstrap-context-watch-unavailable";return;}
#endif
            if(!next)return;
            if(!reason.empty()){CloseHandle(next);return;}
            if(WaitForSingleObject(next,0)!=WAIT_TIMEOUT){CloseHandle(next);reason="mirror-bootstrap-context-exited";return;}
            closeContext();context=next;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            // Reloading the document does not change this WebHelper process.
            std::lock_guard lock(stateMutex_);diagnosticContext_=diagnostic;diagnosticContextIntent_=bindingIntent;
#endif
        };
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
        // A null blocker is the original reuse decision. Report the first
        // failed check from that same pass, without re-reading any live owner.
        const auto preparedBindingBlocker=[&](unsigned long long intent,ULONGLONG now,bool requireIdleWatch=false,bool* watchFacts=nullptr)->const char*{
            if(!boundStarted||boundIntent!=intent)return "binding-intent-unavailable";
            if(now<boundStarted||now-boundStarted>=ConnectionRecoveryWindow::transportGraceMs)return "binding-window-closed";
            if(!preparedLaunch.launchScope.verified())return "binding-proof-unverified";
            if(!context||WaitForSingleObject(context,0)!=WAIT_TIMEOUT)return "binding-context-unavailable";
            if(!config_.ownedBinding)return "binding-owner-unavailable";
            if(preparedLaunch.delegation&&!preparedLaunch.delegation->current())return "binding-delegation-revoked";
            {std::lock_guard lock(stateMutex_);if(preparedAt!=preparationEpoch_)return "binding-preparation-changed";}
            try{
                const auto binding=config_.ownedBinding->snapshot();BindingWaitLeases leases(config_.ownedBinding);
                if(!binding.enabled||binding.canceled||!binding.started||binding.stopRequested||binding.childExited)return "binding-owner-inactive";
                if(binding.generation!=boundGeneration||binding.processId!=boundPid)return "binding-owner-replaced";
                if(!leases.child||WaitForSingleObject(leases.child,0)!=WAIT_TIMEOUT)return "binding-child-unavailable";
                if(!requireIdleWatch)return nullptr;
                if(!leases.ready)return "binding-watch-not-idle";
                const auto ready=WaitForSingleObject(leases.ready,0);
                const bool pending=ready==WAIT_OBJECT_0||(ready==WAIT_TIMEOUT&&config_.ownedBinding->listenerReplyReady());
                if(watchFacts)*watchFacts=pending;
                return ready==WAIT_TIMEOUT&&!pending?nullptr:"binding-watch-not-idle";
            }catch(...){return "binding-owner-check-failed";}
        };
        const auto releasePreparedBinding=[&]{closeContext();stopOwnedBinding();launchAttempted=~0ULL;};
#endif
        bool finalAdmissionRecheckDue=false;
        auto startOwned=[&](unsigned long long intent,[[maybe_unused]] bool resuming=false){
            // Resource preparation and bootstrap IO can outlive an admission.
            // Recheck before creating a child; a loaded/unknown/newer context
            // must not become an official Loader reinjection/RestartJSContext.
            if (config_.launchAdmission) {
                // A mode completion can arrive before this refusal is published.
                // Keep one observation hint; only the original worker can use it.
                struct FinalRead {
                    std::atomic<FinalAdmissionWake>& wake;
                    explicit FinalRead(std::atomic<FinalAdmissionWake>& value):wake(value){wake.store(FinalAdmissionWake::reading);}
                    ~FinalRead(){wake.store(FinalAdmissionWake::idle);}
                    bool take(){return wake.exchange(FinalAdmissionWake::idle)==FinalAdmissionWake::queued;}
                } finalRead(finalAdmissionWake_);
                LaunchAdmission finalAdmission;
                try { finalAdmission = config_.launchAdmission(); }
                catch (...) { finalAdmission = {false, "steam-ui-state-pending", true}; }
                if (!launchAllowed(intent)) return;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                const auto now=GetTickCount64();
                // A ready/terminal watch frame may have arrived alongside the
                // shared mode wake or during preflight. Never launch from work
                // whose original watch has unconsumed facts at this boundary.
                if(resuming){
                    bool watchFacts=false;
                    const bool idle=!preparedBindingBlocker(intent,now,true,&watchFacts);
                    if((!idle&&!watchFacts)||!launchAllowed(intent)||
                       (finalAdmission.allowed&&!finalAdmission.bindingOnly&&!(preparedLaunch.launchScope==finalAdmission.bindingScope))){
                        releasePreparedBinding();notifyListenerFacts();return;
                    }
                    // The existing wait below drains the authenticated packet.
                    // A queued packet blocks launch, but is not proof of loss.
                    if(watchFacts)return;
                }
#endif
                if (!finalAdmission.allowed || finalAdmission.bindingOnly) {
                    unsigned long long epoch=0;
                    {std::lock_guard lock(stateMutex_);if(stopping_||!state_.enabled||intent_!=intent)return;epoch=admissionEpoch_;}
                    // A successful binding already consumed the launch attempt.
                    // Keep the normal permission retry alive after this late
                    // refusal; no new window event is required. Exact retained
                    // admission hands off to transport only, never a Loader.
                    bool hold=false;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                    const char* holdBlocker="not-eligible";
                    if(pendingPreparedMode(finalAdmission)){
                        const auto& scope=preparedLaunch.launchScope;
                        if(!scope.verified())holdBlocker="binding-proof-unverified";
                        else if(scope.steam.pid!=finalAdmission.modeSteamPid||scope.steam.created!=finalAdmission.modeSteamBirth)
                            holdBlocker="binding-steam-scope-mismatch";
                        else holdBlocker=preparedBindingBlocker(intent,now);
                    }
                    hold=!holdBlocker;
                    if(hold)heldLaunchUntil=boundStarted+ConnectionRecoveryWindow::transportGraceMs;
#else
                    const auto now=GetTickCount64();
#endif
                    if(!hold){
                        launchAttempted=~0ULL;
                        if(finalAdmission.bindingOnly)attempted=~0ULL;
                        closeContext();
                    }
                    admissionChecks.blocked(epoch,now,finalAdmission.retryable||finalAdmission.bindingOnly);
                    publish(hold?"starting":"waiting-manual-steam", finalAdmission.reason,0,0,false,hold?attempts:0,{}, {},
                        admissionChecks.timeout(now)!=INFINITE,admissionChecks.attempts(),finalAdmission.detail,
                        admissionChecks.elapsed(now),admissionChecks.timeout(now)==INFINITE?0:admissionChecks.timeout(now),admissionChecks.recovering());
                    if(finalRead.take()&&finalAdmission.retryable&&finalAdmission.reason=="steam-ui-state-pending"&&
                       (finalAdmission.detail=="shared-mode-pending"||finalAdmission.detail=="live-busy")&&launchAllowed(intent))
                        finalAdmissionRecheckDue=true;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                    if (finalAdmission.modeCacheMiss.reason && (config_.bindingDiagnosticBorrowed || config_.bindingDiagnostic)) {
                        optionalBindingDiagnostic([&]{
                            const auto& miss=finalAdmission.modeCacheMiss;
                            return nlohmann::json{{"scope","final-launch-admission"},{"bootstrapStage","mode-cache-miss"},
                                {"reason",finalAdmission.detail},{"modeCacheCause",miss.reason},
                                {"modeCacheOriginLine",miss.originLine},{"modeCacheSequence",miss.sequence},{"preparedBindingHold",holdBlocker?holdBlocker:"held"},
                                {"steamPid",finalAdmission.modeSteamPid},{"steamBirth",std::to_string(finalAdmission.modeSteamBirth)}};
                        },"diagnostic");
                    }
#endif
                    return;
                }
            }
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            heldLaunchUntil=0;preparedLaunch={};
#endif
            // One zero-wait pass at the existing launch ingress. A retired
            // generation remains a debt until the actual native close succeeds.
            ymcc::deckybinding::Owner::retryRetainedCleanup(0);
            const auto retainedGeneration=retainedLoaderNativeGeneration_.load();
            if(retainedGeneration){
                bool pending=false;
                {
                    std::lock_guard retainedLock(ymcc::deckybinding::detail::retainedMutex);
                    for(auto* value=ymcc::deckybinding::detail::retained;value;value=value->retainedNext)
                        if(value->generation==retainedGeneration){pending=true;break;}
                }
                if(pending){
                    publish("failed","owned-loader-cleanup-pending",ERROR_BUSY);return;
                }
                retainedLoaderNativeGeneration_=0;
            }
            if(!loaderNativeCleanup){
                try{loaderNativeCleanup=std::make_unique<ymcc::deckybinding::detail::State>();}
                catch(...){publish("failed","environment-failed",ERROR_NOT_ENOUGH_MEMORY);return;}
            }
            job=CreateJobObjectW(nullptr,nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){
                const DWORD error=GetLastError();closeOwned("job-create-failed");publish("failed","job-create-failed",error);return;
            }
            STARTUPINFOW startup{};startup.cb=sizeof(startup);
            startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
            PROCESS_INFORMATION child{};
            std::wstring command=L"\""+config_.executable+L"\"";
            try{
                const auto temp=temporary.prepare(config_.home);
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                bindingDiagnostic({{"scope","loader-environment"},{"bootstrapStage","loader-temp-prepared"},
                    {"tempPathUnits",temp.size()},{"tempShortRoot",temporary.usedShortRoot()}},"diagnostic");
#endif
                auto env=environment(config_.home,temp);
                const auto directory=std::filesystem::path(config_.executable).parent_path().wstring();
                const auto launch=ymcc::deckylaunch::create(config_.executable,command,env.data(),directory,startup,child);
                if(!launch.created){
                    closeOwned("launch-failed");
                    if(launchPolicy.failed(launch.stage,launch.error,GetTickCount64())){
                        if(launchPolicy.cause()==LaunchPolicyStop::Cause::loaderPathUnavailable){
                            pathLaunchFailure=launch;
                            loaderPathObservedAbsent=observeLoaderPath(config_.executable)==LoaderPathState::absent;
                        }
                        launchPolicyObservedBeforeCreate=false;launchPolicyReported=false;launchPermissionProbeReported=false;notifyListenerFacts();
                    }
                    publish("failed","launch-failed",launch.error,0,false,attempts,launch.stage,launch.mode);
                    return;
                }
                process=child.hProcess;
                const bool assigned=AssignProcessToJobObject(job,process)!=FALSE;
                bool resumed=false;
                {
                    std::lock_guard consent(stateMutex_);
                    if(assigned&&!stopping_&&state_.enabled&&state_.steamKnown&&state_.steamRunning&&intent_==intent&&retainOwnedStopLease(job,process))
                        resumed=ResumeThread(child.hThread)!=static_cast<DWORD>(-1);
                }
                if(!resumed){
                    const DWORD error=GetLastError();TerminateProcess(process,1);
                    CloseHandle(child.hThread);closeOwned("ownership-failed");publish("failed","ownership-failed",error);
                }else{
                    CloseHandle(child.hThread);
                    loaderStarted=GetTickCount64();loaderWasHealthy=false;
                    publish("loader-started","mirror-handshake-pending",0,child.dwProcessId,false,attempts,launch.stage,launch.mode);
                    if(connectionFallback.serviceStarted(loaderStarted)){
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                        try{bindingDiagnostic({{"scope","stage-window"},{"stage","cold-loader-grace"},
                            {"stageBudgetMs",ConnectionRecoveryWindow::coldStartGraceMs},{"reason","new-owned-loader"}},"diagnostic");
                        }catch(...){} // Optional cold-start logging cannot stop the running Loader.
#endif
                    }
                }
            }catch(const std::exception& failure){
                // Keep only a closed set of our own stages, never arbitrary OS/
                // filesystem exception text (which may contain private paths).
                const std::string stage=failure.what();
                const bool tempFailure=stage=="loader-temp-short-root-unavailable"||stage=="loader-temp-short-root-not-ordinary"||
                    stage=="loader-temp-short-root-create-failed"||stage=="loader-temp-path-too-long"||stage=="loader-temp-base-failed"||
                    stage=="loader-temp-base-not-ordinary"||stage=="loader-temp-home-not-ordinary"||stage=="loader-temp-run-create-failed"||
                    stage=="loader-temp-random-failed"||stage=="loader-temp-name-collision"||stage=="loader-temp-run-still-owned";
                closeOwned("environment-failed");publish("failed","environment-failed",stage=="loader-temp-path-too-long"?ERROR_FILENAME_EXCED_RANGE:0,0,false,attempts,tempFailure?stage:"");
            }catch(...){closeOwned("environment-failed");publish("failed","environment-failed");}
        };
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
        bool listenerBoundaryBlocked=false,listenerColdAfterPolicy=false;unsigned long long listenerBoundaryIntent=0;
        BindingMode listenerBoundaryMode=BindingMode::retained;
        const auto parkListenerBoundary=[&](const std::string& reason,unsigned long long intent,BindingMode mode,bool retrySafe,bool beforeWrite=false){
            if(!ymcc::deckybinding::Client::listenerBoundaryFailure(reason))return false;
            if(!launchAllowed(intent))return true;
            unsigned long long observationEpoch=0;
            {std::lock_guard lock(stateMutex_);observationEpoch=admissionEpoch_;}
            // Reuse this owner's existing 1/3/8/20s then 30s read window.
            // A source refusal never starts outage/recycle or refills a write budget.
            const bool mayRead=retrySafe&&ymcc::deckybinding::Client::listenerTransientFailure(reason)&&
                ownedBindingCleanupError_.load()==ERROR_SUCCESS;
            admissionChecks.blocked(observationEpoch,GetTickCount64(),mayRead);
            listenerColdAfterPolicy=mayRead&&beforeWrite&&mode==BindingMode::retained&&launchPolicy.blocked();
            listenerBoundaryBlocked=true;listenerBoundaryIntent=intent;listenerBoundaryMode=mode;closeContext();
            publish(process?"loader-started":"unavailable",reason,0,process?GetProcessId(process):0,false,attempts);
            return true;
        };
#endif
        auto bind=[&](unsigned long long intent,bool beforeLaunch,bool renew){
            if(renew&&!beginPending(intent))return;
            const auto bindingBegan=GetTickCount64();
            connectionFallback.started(ConnectionRecoveryWindow::Action::reconnect,bindingBegan);
            std::string reason;bool revalidateSession=false;
            ++attempts;
            try{
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                reason=beforeLaunch?config_.prepareBinding():config_.refreshBinding();
#else
                BindingPlan completed;
                {std::lock_guard lock(stateMutex_);preparedAt=preparationEpoch_;}
                reason=performOwnedBinding(intent,beforeLaunch?BindingMode::prepare:BindingMode::refresh,revalidateSession,beforeLaunch?&completed:nullptr);
#endif
                takeContext(reason,intent);
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                if(reason.empty()&&beforeLaunch)preparedLaunch=std::move(completed);
#endif
            }
            catch(...){reason="mirror-binding-failed";}
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            if(!reason.empty()){
                const bool returned=stopOwnedBinding();
                if(!returned)revalidateSession=false;
            }
#endif
            if(!launchAllowed(intent))return;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            if(parkListenerBoundary(reason,intent,beforeLaunch?(revalidateSession?BindingMode::prepare:BindingMode::retained):BindingMode::refresh,true))return;
#endif
            if(reason.empty()){
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                boundStarted=0;
                try{const auto currentBinding=config_.ownedBinding->snapshot();
                    boundStarted=bindingBegan;boundIntent=intent;boundGeneration=currentBinding.generation;boundPid=currentBinding.processId;
                }catch(...){/* Unknown optional freshness follows normal recovery. */}
#endif
                retries={};outage.reset();outageWatchRequested=false;outageDue=false;retryDue=false;
                if(beforeLaunch){
                    // A delayed launch revalidates resources once, not once per network retry.
                    if(deferredLaunch)reason=runLaunchPreflight(intent);
                    if(!launchAllowed(intent))return;
                    launchPending=false;deferredLaunch=false;
                    if(reason.empty())startOwned(intent);
                    else{
                        closeContext();
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                        if(!stopOwnedBinding())reason="mirror-binding-cleanup-incomplete";
#endif
                        if(!launchAllowed(intent))return;
                        rejectedResources(reason);
                        publish("unavailable",reason,0,0,false,attempts);
                    }
                }else publish("loader-started","mirror-handshake-pending",0,GetProcessId(process),false,attempts);
            }else{
                connectionFallback.failed(ConnectionRecoveryWindow::Action::reconnect,GetTickCount64());
                const bool pending=revalidateSession||retryableBinding(reason);
                if(pending){retries={};outageWatchRequested=true;outage.begin(GetTickCount64());}
                if(!pending){retries={};outage.reset();outageWatchRequested=false;launchPending=false;deferredLaunch=false;}
                else if(beforeLaunch){launchPending=true;deferredLaunch=true;}
                publish(beforeLaunch?"unavailable":"loader-started",reason,0,process?GetProcessId(process):0,pending,attempts);
            }
        };
        const auto attachRetained=[&](unsigned long long intent,unsigned long long launchIntent,bool exactOnly=false){
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            (void)exactOnly;
#endif
            cancelResourceRetry();
                    if(intent!=attempted)stopWindow();
                    attempted=intent;launchAttempted=launchIntent;retryDue=false;outageDue=false;
                    loaderRecovery.reset();loaderRecoveryDue=false;closeContext();
                    publish("reconnecting", "retained-frontend-rebind");
                    const auto bindingBegan=GetTickCount64();

                    connectionFallback.started(ConnectionRecoveryWindow::Action::reconnect,bindingBegan);
                    std::string failure;bool revalidateSession=false;
                    try {
                        if(!beginPending(intent))return;
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                        failure=config_.retainedBinding?config_.retainedBinding():"mirror-retained-binding-unavailable";
#else
                        failure=performOwnedBinding(intent,exactOnly?BindingMode::retainedExact:BindingMode::retained,revalidateSession);
#endif
                        takeContext(failure,intent);
                    }catch(...){failure="mirror-binding-failed";}
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                    if(!failure.empty()){
                        const bool returned=stopOwnedBinding();
                        if(!returned)revalidateSession=false;
                    }
#endif
                    if(!launchAllowed(intent))return;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE

                    // Retained frontends have no owned Loader. Credit their actual

                    // successful binding to the same absolute handshake window too.

                    boundStarted=0;

                    if(failure.empty())try{const auto binding=config_.ownedBinding->snapshot();

                        boundStarted=bindingBegan;boundIntent=intent;boundGeneration=binding.generation;boundPid=binding.processId;

                    }catch(...){/* Unknown freshness follows normal recovery. */}

#endif

#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                    if(parkListenerBoundary(failure,intent,BindingMode::retained,true,revalidateSession))return;
#endif
                    if(!failure.empty())connectionFallback.failed(ConnectionRecoveryWindow::Action::reconnect,GetTickCount64());
                    const bool retry=!failure.empty()&&(revalidateSession||retryableBinding(failure));
                    if(retry){outageWatchRequested=true;outage.begin(GetTickCount64());}else {outage.reset();outageWatchRequested=false;}
                    publish(failure.empty()?"frontend-attached":retry?"reconnecting":"unavailable",
                            failure.empty()?"retained-frontend-connected":failure,0,0,retry,1);
        };
        for(;;){
            bool stop,enabled,known,steam,recoveryRequested,healthRequested,retainedAdmissionRequested,safetyPending,retainedFrontend;
            unsigned long long intent,launchIntent,admissionEpoch,crashResetEpoch,preparationEpoch;
            {std::lock_guard lock(stateMutex_);stop=stopping_||exitFenced_.load(std::memory_order_acquire);enabled=state_.enabled;known=state_.steamKnown;
                steam=state_.steamRunning;intent=intent_;launchIntent=launchIntent_;admissionEpoch=admissionEpoch_;crashResetEpoch=crashResetEpoch_;preparationEpoch=preparationEpoch_;
                retainedFrontend=state_.phase=="frontend-attached"||(state_.phase=="reconnecting"&&state_.reason=="retained-frontend-rebind");
                recoveryRequested=transportRecoveryRequested_;transportRecoveryRequested_=false;
                healthRequested=connectionCheckRequested_;connectionCheckRequested_=false;
                retainedAdmissionRequested=retainedAdmissionRefreshRequested_;retainedAdmissionRefreshRequested_=false;safetyPending=pendingSafetyStop_.active(intent_); }
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            if(listenerBoundaryBlocked&&listenerBoundaryIntent!=intent)listenerBoundaryBlocked=false;
#endif
            if(recoveryRequested){
                outageWatchRequested=true;
                // Retry only this pre-launch native-cleanup refusal on an
                // existing watch request; do not renew any recovery budget.
                if(retainedLoaderNativeGeneration_.load()){
                    attempted=~0ULL;launchAttempted=~0ULL;
                }
            }
            if(stop){
                const auto now=GetTickCount64();const ConnectionHealth health{false,false,"decky-stopped"};
                reportConnection(connectionFallback.observe(now,health),health,now,intent);closeOwned("runtime-stop");break;
            }
            if(resourceAdmissionEpoch!=admissionEpoch||resourcePreparationEpoch!=preparationEpoch){
                resourceAdmissionEpoch=admissionEpoch;resourcePreparationEpoch=preparationEpoch;
                // Ready may arrive during this already admitted resource wait.
                // Retain the pending attempt, then reread admission; Ready alone
                // gives no new launch authority and cannot wake healthy owners.
                if(pendingResourceFailure)launchAttempted=~0ULL;
                cancelResourceRetry();resourceRetries={};resourceWindowOpened=false;
            }
            if(safetyPending){
                const auto now=GetTickCount64();const ConnectionHealth health{false,false,"terminal-safety-stop"};
                reportConnection(connectionFallback.observe(now,health),health,now,intent);
                cancelResourceRetry();
                // The terminal page was live-read before its packet was posted.
                // Stop only its owned binding now; retain Loader/broker authority
                // for normal owner OFF. No timer, admission/resource work or new
                // binding may race ahead of that queued decision. Explicit OFF
                // and a genuinely new user/session intent still wake this wait.
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                stopOwnedBinding();
#endif
                closeContext();stopWindow();admissionChecks.stop();
                loaderRecovery.reset();loaderRecoveryDue=false;
                WaitForSingleObject(wake_,INFINITE);continue;
            }
            if(crashEpoch!=crashResetEpoch){
                const auto now=GetTickCount64();const ConnectionHealth health{false,false,"steam-session-changed"};
                reportConnection(connectionFallback.observe(now,health),health,now,intent);
                launchPolicy.reset();launchPolicyReported=false;loaderPathObservedAbsent=false;parkedPreparationSeen=~0ULL;
                crashEpoch=crashResetEpoch;crashBudget.reset();loaderRecovery.reset();loaderRecoveryDue=false;healthRequested=true; // New verified session must check now, not wait for the idle backstop.
            }
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            const bool listenerBoundaryCurrent=listenerBoundaryBlocked&&listenerBoundaryIntent==intent&&enabled&&known&&steam&&
                (!process||WaitForSingleObject(process,0)!=WAIT_OBJECT_0);
            const bool policyOwnsListenerRetry=listenerBoundaryCurrent&&!process&&launchPolicy.blocked()&&
                listenerBoundaryMode==BindingMode::retained&&ownedBindingCleanupError_.load()==ERROR_SUCCESS&&
                admissionChecks.timeout(GetTickCount64())!=INFINITE;
            if(listenerBoundaryCurrent&&!policyOwnsListenerRetry){
                if(!process&&launchPolicy.blocked()){
                    // Terminal or unreturned listeners cannot borrow the permission retry.
                    WaitForSingleObject(wake_,INFINITE);continue;
                }
                const auto now=GetTickCount64();
                if(admissionChecks.take(now)){
                    LaunchAdmission permission;
                    try{permission=config_.launchAdmission?config_.launchAdmission():LaunchAdmission{false};}
                    catch(...){permission={false,"steam-ui-state-pending",true};}
                    if(!launchAllowed(intent))continue;
                    // A returned, explicitly pre-write retained attempt has nothing to replay.
                    // After a launch stop clears, a verified unloaded document can take the normal cold path.
                    if(listenerColdAfterPolicy&&!process&&permission.allowed&&!permission.bindingOnly&&
                       permission.bindingScope.verified()&&ownedBindingCleanupError_.load()==ERROR_SUCCESS){
                        listenerBoundaryBlocked=false;admissionChecks.completed();
                        attempted=~0ULL;launchAttempted=~0ULL;
                        optionalBindingDiagnostic([&]{return nlohmann::json{{"scope","global-connection"},
                            {"stage","listener-cold-admission"},{"reason","retained-binding-no-write"},{"attempted",false}};},"diagnostic");
                        continue;
                    }
                    // Keep the originally selected operation. Unknown initial
                    // mutation cannot become a fresh-context prepare replay.
                    const bool permitted=permission.allowed&&
                        (listenerBoundaryMode!=BindingMode::retained||permission.bindingOnly);
                    if(permitted&&ownedBindingCleanupError_.load()==ERROR_SUCCESS){
                        listenerBoundaryBlocked=false;
                        if(listenerBoundaryMode==BindingMode::retained)attachRetained(intent,launchIntent);
                        else bind(intent,listenerBoundaryMode==BindingMode::prepare,true);
                        if(!listenerBoundaryBlocked){
                            admissionChecks.completed();
                            const auto resumed=snapshot();
                            if(listenerBoundaryMode==BindingMode::retained&&resumed.phase=="frontend-attached"&&resumed.reason=="retained-frontend-connected"){
                                // A confirmed existing frontend consumes the old
                                // cold-start continuation; never replay its due tick.
                                retries={};retryDue=false;outageDue=false;launchPending=false;deferredLaunch=false;
                            }
                        }
                    }
                    continue;
                }
                HANDLE blockedWaits[]{wake_,process};
                const auto blockedResult=WaitForMultipleObjects(process?2:1,blockedWaits,FALSE,admissionChecks.timeout(now));
                if(blockedResult==WAIT_FAILED){const auto error=GetLastError();closeOwned("wait-failed");publish("failed","wait-failed",error);break;}
                continue;
            }
#endif
            LaunchAdmission admission{};
            const auto readAdmission=[&]{
                if(enabled && known && steam && !process && config_.launchAdmission){
                    try { admission=config_.launchAdmission(); }
                    catch (...) { admission={false,"steam-ui-state-pending",true,"admission-read-failed"}; }
                }
            };
            const auto exactRetained=[&]{return admission.allowed&&admission.bindingOnly&&admission.reason=="retained-frontend-rebind";};
            bool sampledAdmission=false,transportOnly=false;
            // Keep the cold-launch stop even when an exact existing frontend
            // can attach. Requiring a connected mirror here would block its own attach.
            if(launchPolicy.blocked()&&enabled&&known&&steam&&!process){
                const bool pathStopped=launchPolicy.cause()==LaunchPolicyStop::Cause::loaderPathUnavailable;
                const bool liveRetained=retainedFrontend&&context&&WaitForSingleObject(context,0)==WAIT_TIMEOUT;
                bool pathPrepared=false;
                if(pathStopped&&loaderPathObservedAbsent&&!context&&!retainedFrontend&&config_.prepareEnvironment&&
                   preparedEpoch!=preparationEpoch&&preparationAttemptEpoch!=preparationEpoch&&parkedPreparationSeen!=preparationEpoch&&
                   ownedBindingCleanupError_.load()==ERROR_SUCCESS
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                   &&!listenerBoundaryCurrent
#endif
                   ){
                    // An admitted Ready can repair the missing file using the original
                    // preparation owner. An idle retry or a same-Ready wake cannot.
                    parkedPreparationSeen=preparationEpoch;
                    readAdmission();sampledAdmission=true;
                    if(!launchAllowed(intent))continue;
                    if(!(admission.allowed&&admission.bindingOnly)){
                        if(!runPreparation(intent,preparationEpoch,admissionEpoch))continue;
                        sampledAdmission=false;
                        pathPrepared=preparedEpoch==preparationEpoch&&observeLoaderPath(config_.executable)==LoaderPathState::file;
                        if(pathPrepared)launchPolicy.reset();
                    }
                }
                const auto now=GetTickCount64();
                if(pathPrepared||launchPolicy.recheck(now,[&]{
                    if(pathStopped){
                        // An already-present file can still have failed on a missing
                        // dependency. Do not retry on its mere continued existence.
                        const auto path=observeLoaderPath(config_.executable);
                        if(path==LoaderPathState::file&&loaderPathObservedAbsent)return true;
                        if(path==LoaderPathState::absent)loaderPathObservedAbsent=true;
                    }else{
                        const bool ordinary=ymcc::deckylaunch::ordinarySourceObserved();
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                        // Report the first negative check and a later positive edge, not every idle retry.
                        if((ordinary||!launchPermissionProbeReported)&&launchAllowed(intent)){
                            launchPermissionProbeReported=true;
                            optionalBindingDiagnostic([&]{return nlohmann::json{{"scope","global-connection"},
                                {"stage","launch-permission-recheck"},
                                {"reason",ordinary?"ordinary-source-observed":"ordinary-source-unconfirmed"},
                                {"attempted",false}};},"diagnostic");
                        }
#endif
                        if(ordinary)return true;
                    }
                    // Keep the existing shared read renewal; its mirror result
                    // provides neither document admission nor Loader authority.
                    try{if(config_.connectionHealth)(void)config_.connectionHealth();}catch(...){}
                    if(!liveRetained){readAdmission();sampledAdmission=true;}
                    return false;
                })){
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                    if(policyOwnsListenerRetry)continue; // Resume the original listener gate, not cold start.
#endif
                    // Permission to cold-launch does not invalidate a live retained binding.
                    // A newly admitted cold document still needs the original launch attempt.
                    if(liveRetained&&attempted==intent&&launchAttempted==launchIntent){
                        readAdmission();sampledAdmission=true;
                        if(!launchAllowed(intent))continue;
                    }
                    if(!sampledAdmission||!liveRetained||!exactRetained()){
                        attempted=~0ULL;launchAttempted=~0ULL;
                    }
                    healthRequested=true;
                }else if(liveRetained||(sampledAdmission&&exactRetained())){
                    transportOnly=true;
                    if(!liveRetained){attempted=~0ULL;launchAttempted=~0ULL;}
                }else{
                    if(!launchAllowed(intent))continue;
                    if(retainedFrontend){
                        closeOwned("launch-policy-blocked");
                        publishLaunchStop();
                    }
                    const ConnectionHealth health{false,false,"launch-policy-blocked"};
                    reportConnection(connectionFallback.observe(now,health),health,now,intent);
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                    if(!launchPolicyReported)bindingDiagnostic({{"scope","global-connection"},{"stage","launch-policy-blocked"},
                        {"reason",pathStopped?"loader-path-unavailable":"shell-token-elevated"},{"recoveryPhase","blocked"},{"attempted",false},
                        {"launchPolicySource",launchPolicyObservedBeforeCreate?"cold-preflight-observed":"loader-create-refused"},
                        {"nextRetryMs",launchPolicy.timeout(now)}},"diagnostic");
#endif
                    launchPolicyReported=true;
                    WaitForSingleObject(wake_,launchPolicy.timeout(now));continue;
                }
            }
            if(process&&loaderStarted&&loaderWasHealthy&&GetTickCount64()-loaderStarted>=LoaderCrashBudget::stableUptimeMs){crashBudget.healthy(GetTickCount64()-loaderStarted);loaderRecovery.reset();}
            if(process&&WaitForSingleObject(process,0)==WAIT_OBJECT_0){
                DWORD code=0;GetExitCodeProcess(process,&code);lastLoaderExit=code;
                const auto uptime=GetTickCount64()-loaderStarted;
                const bool retry=crashBudget.exited(uptime,loaderWasHealthy);
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                try {
                bindingDiagnostic({{"scope","loader-exit"},{"bootstrapStage","loader-exited"},{"pid",GetProcessId(process)},
                    {"error",code},{"loaderUptimeMs",uptime},{"crashFailures",crashBudget.failures()},
                    {"tempPathUnits",temporary.path().wstring().size()},{"tempShortRoot",temporary.usedShortRoot()}},"diagnostic");
                }catch(...){} // Optional exit diagnostics cannot bypass Loader cleanup.

#endif
                closeOwned("loader-exited");attempted=intent;
                if(retry)loaderRecovery.begin(GetTickCount64());
                publish("failed",retry?"loader-exited":"loader-crash-loop",code);continue;
            }
            // ON + verified existing Big Picture + disconnected => recover,
            // including faults nobody has named yet. Reuse the original owner;
            // UI tiers are separate bounded callbacks, never a Steam process restart,
            // business-write replay, or automatic refund of a durable reservation.
            const auto healthNow=GetTickCount64();
            const bool genericRebindDue=outageDue&&!outageWatchRequested;
            if(!transportOnly&&config_.connectionHealth&&(healthRequested||recoveryRequested||genericRebindDue||connectionFallback.timeout(healthNow)==0||!enabled||!known||!steam)){
                ConnectionHealth health;bool readUnavailable=false;
                if(enabled&&known&&steam){
                    {
                        std::lock_guard lock(stateMutex_);
                        if(intent_!=intent)continue;
                        // Arm before sampling the cache so publication cannot fall between
                        // observing a miss and parking. The callback only queues a reread.
                        connectionPublicationIntent_=intent;
                    }
                    try{health=config_.connectionHealth();}catch(...){
                        // A failed read is uncertainty, not OFF. Keep the original
                        // episode/stage/deadline; fresh health must still prove recovery.
                        health.paused=true;readUnavailable=true;
                        try{health.reason="connection-health-unavailable";}catch(...){} // Optional text cannot abort observation.
                    }
                    // Exceptions also park this check. Keep the same-intent
                    // publication wake armed until a conclusive read returns.
                    if(!health.paused){
                        std::lock_guard lock(stateMutex_);
                        connectionPublicationIntent_.reset();
                    }
                }
                else health.reason=!enabled?"decky-off":!known?"steam-state-unknown":"steam-not-running";
                // Never let a read that outlived its intent finish the old recovery episode.
                if(enabled&&known&&steam&&!launchAllowed(intent))continue;
                if(enabled&&known&&steam&&readUnavailable!=healthReadUnavailable){
                    healthReadUnavailable=readUnavailable;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                    try{
                    bindingDiagnostic({{"scope","connection-health"},{"bootstrapStage",readUnavailable?"health-read-unavailable":"health-read-returned"},
                        {"recoveryEpisode",connectionFallback.episode()},{"reason",readUnavailable?"connection-health-unavailable":"connection-health-read-returned"},
                        {"connected",health.connected},{"attempted",false}},"diagnostic");
                    }catch(...){} // Optional read diagnostics cannot interrupt connection observation.
#endif
                }
                if(process&&health.connected)loaderWasHealthy=true;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                // Complete current UI proof ends first-handshake credit. A new
                // fault must use normal recovery, not this binding's old grace.
                if(health.connected)boundStarted=0;
#endif
                // A peer can return and vanish before the queued retry fires.
                // Full UI proof still cancels early; transport alone cancels
                // only at the original due check. An independent watch fault wins.
                if(!outageWatchRequested&&(health.connected||(genericRebindDue&&health.required&&health.transportConnected))){outage.reset();outageDue=false;}
                // Local native cleanup is not a Steam page failure. Park the
                // existing ladder without spending a page step or reservation.
                if(retainedLoaderNativeGeneration_.load()&&health.required&&!health.connected)health.paused=true;
                // A cleaned local preflight failure already owns a finite retry.
                // Do not rebuild Steam's page before that resource check can run.
                if(!process&&health.required&&!health.connected&&resourceRetryQueued(healthNow))health.paused=true;
                health.coldLoaderStarted=process&&!loaderWasHealthy?loaderStarted:0;
                if(health.coldLoaderStarted&&connectionFallback.awaitingRefusedRebuildLoader()&&
                   WaitForSingleObject(process,0)!=WAIT_TIMEOUT)health.coldLoaderStarted=0;
                const bool blocked=crashBudget.blocked()||ownedBindingCleanupError_.load()!=ERROR_SUCCESS;
                // Cleanup or the existing crash fuse grants no recovery step.
                // Park the same deadline until the original owner can act again.
                if(blocked&&health.required&&!health.connected)health.paused=true;
                bool sameFreshBinding=false;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                // The verified watch already writes the new document on CDP
                // navigation. Give its missing mirror peer the original
                // transport window without replacing that same live owner.
                if(health.connected||recoveryRequested||outageWatchRequested)watchAttachedAt=0;
                const auto bindingStart=boundStarted?boundStarted:
                    (!health.transportConnected&&watchBindingGeneration==boundGeneration?watchAttachedAt:0);
                if(!blocked&&health.required&&!health.connected&&!health.paused&&!recoveryRequested&&!outageWatchRequested&&
                   bindingStart&&boundIntent==intent&&healthNow>=bindingStart&&healthNow-bindingStart<ConnectionRecoveryWindow::transportGraceMs&&
                   context&&WaitForSingleObject(context,0)==WAIT_TIMEOUT&&config_.ownedBinding){
                    try{const auto binding=config_.ownedBinding->snapshot();BindingWaitLeases leases(config_.ownedBinding);
                        // Borrow the original same-generation wait handles. A first
                        // peer is transport only; it cannot certify full UI health.
                        sameFreshBinding=binding.enabled&&!binding.canceled&&binding.started&&!binding.stopRequested&&!binding.childExited&&
                            binding.generation==boundGeneration&&binding.processId==boundPid&&leases.child&&WaitForSingleObject(leases.child,0)==WAIT_TIMEOUT;
                    }catch(...){sameFreshBinding=false;}
                }
                health.freshBindingStarted=sameFreshBinding?bindingStart:0;
#endif
                std::shared_ptr<steamcommand::Reservation> commandOperation;
                const auto action=connectionFallback.observe(healthNow,health,[&](auto requested){
                    // Health may return after OFF, exit, or a session change. Do
                    // not claim a shared command slot for the retired intent.
                    if(!launchAllowed(intent))return false;
                    if(!config_.reserveConnectionStep)return true;
                    commandOperation=config_.reserveConnectionStep(requested);return bool(commandOperation);
                });
                using Action=ConnectionRecoveryWindow::Action;
                const bool recovering=action==Action::reconnect||action==Action::recycle||action==Action::rebuild||action==Action::reopen;
                const bool reuseFreshBinding=action==Action::reconnect&&sameFreshBinding;
                // An official-only outage cannot recycle a Loader we do not
                // own. Keep its still-live retained mirror at this empty rung;
                // an independent watch request or later page operation keeps
                // its original replacement path and observation budget.
                const bool preserveRetainedTransport=action==Action::recycle&&!process&&retainedFrontend&&
                    health.transportConnected&&!recoveryRequested&&!outageWatchRequested&&
                    context&&WaitForSingleObject(context,0)==WAIT_TIMEOUT;
                // Do not refill the absolute window on a peer/proof hint. A real
                // watch failure, new owner, OFF or the original deadline still wins.
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                if(reuseFreshBinding){connectionFallback.started(action,bindingStart);watchAttachedAt=0;}
#endif
                // The owned Loader's cold wait can outlast the fresh-binding
                // window. A connected mirror still needs no replacement at this
                // rung unless a separate watch recovery is already pending.
                const bool observeOwnedTransport=action==Action::reconnect&&process&&health.transportConnected&&
                    !recoveryRequested&&!outageWatchRequested;
                reportConnection(action,health,healthNow,intent,blocked,reuseFreshBinding||preserveRetainedTransport||observeOwnedTransport);
                if(recovering&&!blocked&&launchAllowed(intent)){
                    bool keptTransport=preserveRetainedTransport;
                    if(action==Action::recycle){
                        if(process){
                            FailureTrace trace(*this,ymcc::steamfailure::Operation::RecycleLoader,intent);
                            closeOwned("connection-recycle");trace.complete({{"ok",ownedBindingCleanupError_.load()==ERROR_SUCCESS},
                                {"mutated",true},{"uncertain",ownedBindingCleanupError_.load()!=ERROR_SUCCESS},
                                {"reason",ownedBindingCleanupError_.load()==ERROR_SUCCESS?"owned-loader-returned":"owned-binding-cleanup-pending"}});
                            publish("reconnecting","global-connection-recovery");
                        }
                        else connectionFallback.failed(Action::recycle,GetTickCount64()); // No owned service to restart: do not wait20s.
                    }
                    if((action==Action::rebuild||action==Action::reopen)&&(config_.connectionStep||config_.connectionStepReserved)){
                        // A retained watch is not an owned Loader. Leave this
                        // usable mirror up during the private child's read-only
                        // checks, but return it before sending the command grant.
                        const bool defer=config_.connectionStepReserved&&!process&&retainedFrontend&&context&&
                            WaitForSingleObject(context,0)==WAIT_TIMEOUT&&intent==attempted&&
                            health.transportConnected&&!recoveryRequested&&!outageWatchRequested;
                        const char* cause=action==Action::rebuild?"connection-rebuild":"connection-reopen";
                        bool teardownEntered=false;
                        const auto retire=[&]{teardownEntered=true;closeOwned(cause);};
                        if(!defer)retire();
                        if(ownedBindingCleanupError_.load()==ERROR_SUCCESS&&launchAllowed(intent)){
                            if(!defer)publish("waiting-manual-steam","decky-context-already-loaded");
                            try{
                                const std::function<bool()> beforeEffect=defer?std::function<bool()>([&]{
                                    if(!launchAllowed(intent)||teardownEntered)return false;
                                    // Do not publish a new revision while the child's exact-revision guard owns the call.
                                    retire();return ownedBindingCleanupError_.load()==ERROR_SUCCESS&&launchAllowed(intent);
                                }):std::function<bool()>{};
                                FailureTrace trace(*this,action==Action::reopen?ymcc::steamfailure::Operation::ReopenBigPicture:
                                    ymcc::steamfailure::Operation::RebuildDocument,intent);
                                const auto receipt=config_.connectionStepReserved?config_.connectionStepReserved(action,commandOperation,beforeEffect):config_.connectionStep(action);
                                trace.complete(receipt);
                                const bool noEffect=receipt.contains("ok")&&receipt["ok"]==false&&receipt.contains("mutated")&&receipt["mutated"]==false&&
                                    (!receipt.contains("uncertain")||receipt["uncertain"]==false);
                                const bool awaitObservedHealth=action==Action::reopen&&noEffect&&
                                    receipt.value("reason",nlohmann::json{})==deckyrepair::bigpicture::alreadyHealthy;
                                const bool identityDeferred=noEffect&&receipt.value("awaitCurrentIdentity",nlohmann::json{})==true&&
                                    receipt.value("cleanupNotStarted",nlohmann::json{})==true&&
                                    receipt.value("reason",nlohmann::json{})=="decky-cleanup-account-unconfirmed";
                                keptTransport=defer&&!teardownEntered&&noEffect;
                                if(receipt.value("ok",false)&&receipt.value("mutated",false)&&!receipt.value("uncertain",false))
                                    connectionFallback.started(action,GetTickCount64());
                                else if(keptTransport&&identityDeferred)
                                    connectionFallback.parkUnstartedPage(action,GetTickCount64());
                                // The child vetoed the mode switch. Keep the original deadline
                                // until the shared health read confirms recovery or expires.
                                else if(noEffect&&!awaitObservedHealth)
                                    connectionFallback.failed(action,GetTickCount64(),identityDeferred);
                            }catch(...){/* Possible dispatch: leave the bounded observation, never replay. */}
                            if(!launchAllowed(intent))continue;
                            if(defer&&!keptTransport){
                                if(!teardownEntered)retire(); // Unknown completion keeps the original fail-safe cleanup.
                                if(ownedBindingCleanupError_.load()==ERROR_SUCCESS)publish("waiting-manual-steam","decky-context-already-loaded");
                                else publish("failed","owned-binding-cleanup-pending",ownedBindingCleanupError_.load());
                            }
                        }else publish("failed","owned-binding-cleanup-pending",ownedBindingCleanupError_.load());
                    }
                    // Re-arm the validated owner. Preflight/admission/identity
                    // run again; no alternate launch path bypasses them.
                    // A live retained binding already owns this reconnect attempt.
                    // Re-arming it here would replace the listener before its first peer.
                    if(!process&&!launchPending&&!pendingResourceFailure&&!reuseFreshBinding&&!keptTransport){attempted=~0ULL;launchAttempted=~0ULL;}
                    // A newly created Loader can still be opening its official
                    // backend after the authenticated mirror is already usable.
                    // Keep the original cold-start observation; rebuilding that
                    // healthy transport here only manufactures a disconnect.
                    // An independently requested watch recovery remains set.
                    if(!keptTransport&&(action!=Action::reconnect||(!health.transportConnected&&!reuseFreshBinding)))recoveryRequested=true;
                }
            }
            // An already verified frontend only needs transport attachment; its
            // original retained-binding worker rechecks that exact document.
            // Do not put disk preparation in front of this existing consumer.
            if(!sampledAdmission)readAdmission();
            if(enabled&&known&&steam&&!process&&!launchAllowed(intent))continue;
            const auto pendingRetained=[&]{return retainedFrontend&&context&&intent==attempted&&!admission.allowed&&admission.retryable&&
                admission.reason=="steam-ui-state-pending"&&(admission.detail=="shared-mode-pending"||admission.detail=="live-busy");};
            if(transportOnly&&!exactRetained()&&!pendingRetained()){
                closeOwned("launch-policy-blocked");
                publishLaunchStop();
                continue;
            }
            // A terminal admission cannot execute a reconnect. Hand that known
            // failure to the existing ladder instead of spending its 7s grace.
            // Pending mode reads and valid retained bindings keep their wait.
            if(!process&&!admission.allowed&&!admission.retryable)
                connectionFallback.failed(ConnectionRecoveryWindow::Action::reconnect,GetTickCount64());
            if(!process&&(admission.bindingOnly||(!admission.allowed&&!admission.retryable)))
                connectionFallback.failed(ConnectionRecoveryWindow::Action::recycle,GetTickCount64());
            bool preparationRan=false;
            // One preparation per enable/committed Ready, even when Steam is explicitly absent.
            // Unknown presence waits for the existing observer. A live retained
            // CEF context is as important as an owned Loader: defer resource IO
            // while either lease exists, including committed Ready refreshes.
            // A requested retained rebind may briefly close the old context
            // lease before the new watch arrives; that handoff still has
            // priority over optional resource maintenance on this worker.
            // Deferred preparation grants no rebind, restart or launch authority.
            // A mode hint is not new resource demand after a deadline,
            // unconfirmed cleanup, or verified package mismatch. Steam source
            // and Ready changes and admitted launch keep independent checks.
            if(enabled && known && !process && !context && !retainedFrontend && !(admission.allowed&&admission.bindingOnly) && config_.prepareEnvironment && preparedEpoch!=preparationEpoch &&
                parkedPreparation!=std::pair{preparationEpoch,admissionEpoch} &&
                (preparationAttemptEpoch!=preparationEpoch || preparationAttemptIntent!=intent)) {
                if(!runPreparation(intent,preparationEpoch,admissionEpoch))continue;
                preparationRan=true;
            }
            // Preparation may yield across a Steam mode/session change. Keep
            // the original fresh admission read after any resource work; it
            // must not reuse an earlier permission to create a new Loader.
            if(preparationRan){readAdmission();if(enabled&&known&&steam&&!process&&!launchAllowed(intent))continue;}
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            // A cache invalidation does not itself invalidate this binding. Do
            // not defer separately observed watch, user, or power revocation.
            const auto preparedNow=GetTickCount64();
            const auto stillPendingForBinding=[&]{return pendingPreparedMode(admission)&&
                admission.modeSteamPid==preparedLaunch.launchScope.steam.pid&&admission.modeSteamBirth==preparedLaunch.launchScope.steam.created;};
            if(heldLaunchUntil&&(process||recoveryRequested||preparedNow>=heldLaunchUntil||!launchAllowed(intent)||
                (!stillPendingForBinding()&&(!admission.allowed||admission.bindingOnly||!(admission.bindingScope==preparedLaunch.launchScope)))))
                releasePreparedBinding();
#endif
            if(retainedAdmissionRequested&&admission.allowed&&!admission.bindingOnly){
                // Only this worker calls launchAdmission (it owns its read budget).
                // A fresh document + existing consent can reopen the consumed
                // launch intent. Duplicate retained facts and OFF cannot do so.
                std::lock_guard lock(stateMutex_);
                if(!stopping_&&state_.enabled&&state_.steamKnown&&state_.steamRunning&&intent_==intent&&!state_.pid&&
                    state_.phase=="frontend-attached"&&state_.reason=="retained-frontend-connected"){
                    ++intent_;++launchIntent_;continue;
                }
            }
            if(admission.allowed)admissionChecks.completed();
            if(recoveryRequested&&(process||admission.bindingOnly)&&enabled&&known&&steam)outage.begin(GetTickCount64());
            if(!enabled||!known||!steam){
                loaderRecovery.reset();loaderRecoveryDue=false;closeOwned(!enabled?"explicit-off":!known?"steam-unknown":"steam-absent");publish(!enabled?"disabled":"waiting-steam",known?"":"steam-state-unknown");
            }else if(!process && admission.allowed && admission.bindingOnly && !crashBudget.blocked()){
                cancelResourceRetry();
                // Never fall through to process creation, including a failed binding.
                // Exact-target validation runs inside retainedBinding before credentials change.
                if(intent!=attempted||launchIntent!=launchAttempted||retryDue||outageDue){
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                    if(policyOwnsListenerRetry){
                        if(!exactRetained()||ownedBindingCleanupError_.load()!=ERROR_SUCCESS)continue;
                        listenerBoundaryBlocked=false;
                    }
#endif
                    attachRetained(intent,launchIntent,transportOnly);
                }
            }else if(!process && pendingRetained()){
                // A shared refresh or local try-lock miss is not evidence that
                // this attached document/transport was lost. live-busy performs
                // no Steam IO. Keep the original context/watch and admission
                // schedule; publishing waiting-manual-steam would stop the broker
                // in the owner callback and manufacture a real disconnect.
                // New intent, context exit, OFF, desktop and actual read failure
                // still take their original cleanup/revalidation branches.
                const auto now=GetTickCount64();admissionChecks.blocked(admissionEpoch,now,true);
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            }else if(heldLaunchUntil){
                if(admission.allowed){
                    const auto failure=runLaunchPreflight(intent);
                    if(!launchAllowed(intent))continue;
                    if(failure.empty()){cancelResourceRetry();startOwned(intent,true);}
                    else{releasePreparedBinding();rejectedResources(failure);publish("unavailable",failure,0,0,false,attempts);}
                }
#endif
            }else if(!process && !admission.allowed){
                // Preserve a pending owned-loader restart while its UI mode is temporarily unavailable.
                if(!admission.retryable){
                    if(pendingResourceFailure)launchAttempted=~0ULL;
                    cancelResourceRetry();loaderRecovery.reset();loaderRecoveryDue=false;
                }
                // If mode confirmation disappeared during binding, its recovery starts a fresh validated attempt.
                if(launchPending)launchAttempted=~0ULL;
                stopWindow(); closeContext();
                const auto now=GetTickCount64();admissionChecks.blocked(admissionEpoch,now,admission.retryable);
                publish("waiting-manual-steam", admission.reason, 0, 0, false, 0, {}, {},
                    admissionChecks.timeout(now)!=INFINITE, admissionChecks.attempts(), admission.detail,
                    admissionChecks.elapsed(now),admissionChecks.timeout(now)==INFINITE?0:admissionChecks.timeout(now),admissionChecks.recovering(),
                    &admissionChecks,&admission);
            }else if(process&&intent!=attempted){
                attempted=intent;launchAttempted=launchIntent;stopWindow();
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                if(config_.refreshBinding){retries.begin(GetTickCount64());bind(intent,false,true);}
#else
                if(config_.ownedBinding){retries.begin(GetTickCount64());bind(intent,false,true);}
#endif
                else publish("loader-started","mirror-handshake-pending",0,GetProcessId(process));
            }else if(!process&&crashBudget.blocked()){
                loaderRecovery.cancel();loaderRecoveryDue=false;publish("failed","loader-crash-loop",lastLoaderExit);
            }else if(!process&&(pendingResourceFailure?resourceRetries.take(GetTickCount64()):(launchIntent!=launchAttempted||loaderRecoveryDue))){
                loaderRecoveryDue=false;cancelResourceRetry();
                attempted=intent;launchAttempted=launchIntent;closeContext();stopWindow();
                // A confirmed refusal cannot be fixed by a preflight or a Steam page bootstrap.
                // Retained attachment has its own preceding branch and never uses this probe.
                if(coldDefaultElevatedShellObserved()){
                    if(!launchAllowed(intent))continue;
                    cancelResourceRetry();
                    publish("failed","launch-failed",ERROR_ACCESS_DENIED,0,false,0,"shell-token-elevated","verified-shell-primary-default-token");
                    if(launchPolicy.failed("shell-token-elevated",ERROR_ACCESS_DENIED,GetTickCount64())){
                        launchPolicyObservedBeforeCreate=true;launchPolicyReported=false;launchPermissionProbeReported=false;notifyListenerFacts();
                    }
                    continue;
                }
                publish("starting");
                std::string failure;
                try{if(!beginPending(intent))continue;failure=runLaunchPreflight(intent);}
                catch(...){failure="preflight-failed";}
                if(!launchAllowed(intent))continue;
                if(failure.empty())cancelResourceRetry();
                if(!failure.empty()){
                    // The worker adapter reports a retryable timeout only after
                    // exact cleanup; malformed/hash/cleanup failures stay terminal.
                    // Admission and preflight are reread before any child launch.
                    rejectedResources(failure);
                    publish("unavailable",failure);
                }
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                else if(config_.prepareBinding){launchPending=true;retries.begin(GetTickCount64());bind(intent,true,false);}
#else
                else {launchPending=true;retries.begin(GetTickCount64());bind(intent,true,false);}
#endif
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                else startOwned(intent);
#endif
            }else if(!process&&pendingResourceFailure){
                // Mode is back, but a retry is not yet due (or is exhausted).
                // Leave the admission-wait phase so repeated mode hints go quiet.
                publish("unavailable",pendingResourceFailure);
            }else if(retryDue||outageDue){
                retryDue=false;outageDue=false;
                if(launchPending)bind(intent,true,true);
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                else if(process&&config_.refreshBinding)bind(intent,false,true);
#else
                else if(process&&config_.ownedBinding)bind(intent,false,true);
#endif
            }
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            // A bind above can fail in this iteration: skip both legacy callback
            // recovery and the following generic failed/unavailable acceleration.
            if(listenerBoundaryBlocked&&listenerBoundaryIntent==intent)continue;
#endif
            // Route a fresh creation refusal to its park before page recovery.
            // Steam absence/unknown still needs to reach the original idle wait.
            if(!transportOnly&&enabled&&known&&steam&&!process&&launchPolicy.blocked())continue;
            // Automatic and manual share the same repair orchestrator/budget.
            // Its exact state/session/resource checks run only on this owner.
            if(!transportOnly&&enabled&&known&&steam&&!exitFenced_.load(std::memory_order_acquire)&&config_.connectionRecovery){try{config_.connectionRecovery();}catch(...){}}
            // This branch consumes only phase. Keep its original fresh lock;
            // copying every presentation string adds allocations on watch wakes.
            bool failedOrUnavailable=false;
            { std::lock_guard lock(stateMutex_);
                failedOrUnavailable=state_.phase=="failed"||state_.phase=="unavailable";
            }
            if(failedOrUnavailable&&!resourceRetryQueued(GetTickCount64())){
                connectionFallback.failed(ConnectionRecoveryWindow::Action::reconnect,GetTickCount64());
                connectionFallback.failed(ConnectionRecoveryWindow::Action::recycle,GetTickCount64());
            }
            HANDLE handles[5]{wake_};DWORD count=1;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            BindingWaitLeases bindingLeases(config_.ownedBinding);
#endif
            const DWORD processIndex=process?count++:MAXDWORD;
            if(process)handles[processIndex]=process;
            const DWORD contextIndex=context?count++:MAXDWORD;
            if(context)handles[contextIndex]=context;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            // Keep the original Loader/CEF exit priority. A CEF exit must reopen
            // the validated binding intent immediately, not become a delayed
            // watch-transport outage just because its child exits at the same time.
            const DWORD bindingEventIndex=bindingLeases.ready?count++:MAXDWORD;
            if(bindingLeases.ready)handles[bindingEventIndex]=bindingLeases.ready;
            const DWORD bindingChildIndex=bindingLeases.child?count++:MAXDWORD;
            if(bindingLeases.child)handles[bindingChildIndex]=bindingLeases.child;
#endif
            const auto now=GetTickCount64();
            const DWORD resourceTimeout=pendingResourceFailure&&admission.allowed&&!admission.bindingOnly&&!crashBudget.blocked()?resourceRetries.timeout(now):INFINITE;
            DWORD timeout=(std::min)({retries.timeout(now),admissionChecks.timeout(now),outage.timeout(now),loaderRecovery.timeout(now),resourceTimeout});
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            if(heldLaunchUntil)timeout=(std::min)(timeout,heldLaunchUntil<=now?0u:static_cast<DWORD>(heldLaunchUntil-now));
#endif
            if(transportOnly)timeout=(std::min)(timeout,launchPolicy.timeout(now));
            else{
                if(enabled&&known&&steam&&config_.connectionRecoveryTimeout)
                    timeout=(std::min)(timeout,config_.connectionRecoveryTimeout());
                if(enabled&&known&&steam&&config_.connectionHealth)
                    timeout=(std::min)(timeout,connectionFallback.timeout(now));
            }
            bool factsReady=false;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            factsReady=config_.ownedBinding&&config_.ownedBinding->listenerReplyReady();
#endif
            // Consume the early mode hint without raising wake_ ahead of an
            // already signaled owned process or binding event.
            const bool earlyRecheck=std::exchange(finalAdmissionRecheckDue,false);
            const DWORD result=WaitForMultipleObjects(count,handles,FALSE,(factsReady||earlyRecheck)?0:timeout);
            // Cleanup APIs may replace the thread last error; preserve the wait cause.
            const DWORD waitError=result==WAIT_FAILED?GetLastError():ERROR_SUCCESS;
            // The OS may end a child during WM_ENDSESSION before owner shutdown
            // joins. Retire it as exit cleanup, never as another recovery input.
            if(exitFenced_.load(std::memory_order_acquire))continue;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            // A completion can arrive while the ordinary timed wait returns.
            if(result==WAIT_TIMEOUT&&!factsReady&&config_.ownedBinding)factsReady=config_.ownedBinding->listenerReplyReady();
#endif
            if(result==WAIT_TIMEOUT&&!factsReady){if(earlyRecheck)continue;const auto due=GetTickCount64();retryDue=retries.take(due);outageDue=outage.take(due);loaderRecoveryDue=loaderRecovery.take(due);admissionChecks.take(due);continue;}
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            if((result==WAIT_TIMEOUT&&factsReady)||(bindingLeases.ready&&result==WAIT_OBJECT_0+bindingEventIndex)||(bindingLeases.child&&result==WAIT_OBJECT_0+bindingChildIndex)){
                auto packet=config_.ownedBinding->next();using Packet=decltype(packet);
                if(packet.kind==Packet::Kind::message){
                    const auto kind=packet.value.value("kind",std::string{});
                    auto detail=std::move(packet.value.at("detail"));
                    if(heldLaunchUntil){
                        const auto stage=detail.find("bootstrapStage");
                        if(stage!=detail.end()&&stage->is_string()&&(*stage=="context-invalidated"||*stage=="context-replaced"))releasePreparedBinding();
                    }
                    if(kind=="diagnostic"){
                        retainTerminalForOwner(detail,intent);
                        // The Client has authenticated the packet's binding
                        // generation. A receipt is only wait evidence, not
                        // health, launch authority or a budget renewal.
                        try{
                            const auto stage=detail.find("bootstrapStage");
                            if(process&&loaderWasHealthy&&context&&stage!=detail.end()&&stage->is_string()&&
                               boundIntent==intent&&launchAllowed(intent)){
                                const auto& name=stage->get_ref<const std::string&>();
                                const auto tick=detail.find("contextAttachedTick");
                                if(name=="context-attached"&&tick!=detail.end()&&tick->is_number_unsigned()&&
                                   ymcc::deckybinding::worker::decimal(packet.value.at("generation"))==bindingRequestGeneration_&&
                                   ymcc::deckybinding::worker::number(detail.at("contextPid"))==GetProcessId(context)){
                                    const auto generation=detail.at("contextGeneration").get<unsigned long long>();
                                    const auto at=tick->get<ULONGLONG>(),seen=GetTickCount64();
                                    std::lock_guard lock(stateMutex_);
                                    if(detail.at("contextGeneration").is_number_unsigned()&&generation>1&&at&&at<=seen&&
                                       seen-at<ConnectionRecoveryWindow::transportGraceMs&&
                                       intent_==intent&&detail.at("runId")==activeBindingRun_&&
                                       (watchBindingGeneration!=boundGeneration||generation>watchContextGeneration)){
                                        watchBindingGeneration=boundGeneration;watchContextGeneration=generation;watchAttachedAt=at;
                                    }
                                }else if((name=="context-invalidated"||name=="context-replaced")&&watchAttachedAt&&
                                         watchBindingGeneration==boundGeneration&&
                                         ymcc::deckybinding::worker::decimal(packet.value.at("generation"))==bindingRequestGeneration_){
                                    watchAttachedAt=0;
                                }
                            }
                        }catch(...){/* Missing or malformed evidence uses ordinary recovery. */}
                    }
                    bindingDiagnostic(std::move(detail),kind.c_str());
                }else if(packet.kind==Packet::Kind::ended||packet.kind==Packet::Kind::failed){
                    if(heldLaunchUntil)launchAttempted=~0ULL;
                    stopOwnedBinding();closeContext();
                    const auto reason=packet.reason.empty()?std::string("mirror-bootstrap-context-exited"):packet.reason;
                    try{bindingDiagnostic({{"bootstrapStage","failed"},{"reason",reason},{"mutated",nullptr},{"mutationState","unknown"}},"diagnostic");}catch(...){} // Optional exit reporting cannot interrupt the queued watch repair.
                    if(parkListenerBoundary(reason,intent,process?BindingMode::refresh:BindingMode::retained,true))continue;
                    if(enabled&&known&&steam){
                        connectionFallback.failed(ConnectionRecoveryWindow::Action::reconnect,GetTickCount64());
                        connectionFallback.failed(ConnectionRecoveryWindow::Action::recycle,GetTickCount64());
                        // The watch has actually ended. Page/socket health cannot
                        // replace this missing worker or cancel its queued repair.
                        outageWatchRequested=true;outage.begin(GetTickCount64());
                    }
                }
                continue;
            }
#endif
            if(process&&result==WAIT_OBJECT_0+processIndex){
                DWORD code=0;GetExitCodeProcess(process,&code);lastLoaderExit=code;
                const auto uptime=GetTickCount64()-loaderStarted;
                const bool retry=crashBudget.exited(uptime,loaderWasHealthy);
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                try {
                bindingDiagnostic({{"scope","loader-exit"},{"bootstrapStage","loader-exited"},{"pid",GetProcessId(process)},
                    {"error",code},{"loaderUptimeMs",uptime},{"crashFailures",crashBudget.failures()},
                    {"tempPathUnits",temporary.path().wstring().size()},{"tempShortRoot",temporary.usedShortRoot()}},"diagnostic");
                }catch(...){} // Optional exit diagnostics cannot bypass Loader cleanup.

#endif
                closeOwned("loader-exited");
                if(retry)loaderRecovery.begin(GetTickCount64());publish("failed",retry?"loader-exited":"loader-crash-loop",code);
            }else if(context&&result==WAIT_OBJECT_0+contextIndex){
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                if(heldLaunchUntil)releasePreparedBinding();
#endif
                closeContext();
                // No lifecycleMutex: UI shutdown may hold it while joining this worker.
                std::lock_guard lock(stateMutex_);
                if(!stopping_&&state_.enabled&&state_.steamKnown&&state_.steamRunning&&(process||state_.phase=="frontend-attached")){
                    ++intent_;cancelPending(); // Observe only; NEVER opens launchIntent_.
                }
            }else if(result==WAIT_FAILED){closeOwned("wait-failed");publish("failed","wait-failed",waitError);break;}
        }
        publish("disabled");
    }
public:
    Runtime() = default;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    ~Runtime() {
        reporting_.store(false);
        {
            std::lock_guard lock(wakeState_->mutex);
            wakeState_->event = nullptr;
            ++wakeState_->generation;
        }
        shutdown();
    }
    void initialize(Config config) { std::lock_guard lock(lifecycleMutex_); config_ = std::move(config); }
    // A listener completion carries facts only; never changes intent or recovery authority.
    // Call this factory for each scheduled query, after the current event is published.
    // Returned callbacks can outlive Runtime and cannot signal a later event generation.
    std::function<void()> listenerFactsWake() {
        const std::weak_ptr<WakeState> weak = wakeState_;
        std::uint64_t generation = 0;
        {
            std::lock_guard lock(wakeState_->mutex);
            generation = wakeState_->generation;
        }
        return [weak,generation]() noexcept {
            const auto state = weak.lock();
            if(!state)return;
            std::lock_guard lock(state->mutex);
            if(state->generation==generation&&state->event)SetEvent(state->event);
        };
    }
    // Synchronous internal calls require a live Runtime, including its shutdown wake.
    void notifyListenerFacts() noexcept {
        std::lock_guard lock(wakeState_->mutex);
        if(wake_)SetEvent(wake_);
    }
    State snapshot() { std::lock_guard lock(stateMutex_); return state_; }
    // Reuse the existing explicit-enable/verified-session epoch. This is only
    // observation lifetime, never a new recovery permission or budget refund.
    unsigned long long connectionEvidenceEpoch() { std::lock_guard lock(stateMutex_); return crashResetEpoch_; }
    bool tryDiagnosticState(State& result
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
        ,ymcc::deckybinding::DiagnosticContextProcess* context=nullptr
#endif
    ) {
        std::unique_lock lock(stateMutex_, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        result = state_;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
        if(context)*context=state_.enabled&&!stopping_&&!exitFenced_.load(std::memory_order_acquire)&&diagnosticContextIntent_==intent_
            ?diagnosticContext_:ymcc::deckybinding::DiagnosticContextProcess{};
#endif
        return true;
    }
    // Sample only the already-held Runtime thread. Never wait for shutdown,
    // duplicate a handle, or use diagnostic counters to drive recovery.
    std::optional<DiagnosticRuntimeThreadCpu> diagnosticRuntimeThreadCpu() noexcept {
        const DWORD savedError=GetLastError();
        struct RestoreError{DWORD value;~RestoreError(){SetLastError(value);}} restore{savedError};
        try{
            std::unique_lock lock(lifecycleMutex_,std::try_to_lock);
            if(!lock.owns_lock()||!worker_.joinable())return std::nullopt;
            const HANDLE thread=worker_.native_handle();
            const DWORD id=GetThreadId(thread);
            FILETIME born{},exited{},kernel{},user{};
            if(!id||!GetThreadTimes(thread,&born,&exited,&kernel,&user)||WaitForSingleObject(thread,0)!=WAIT_TIMEOUT)return std::nullopt;
            const auto ticks=[](const FILETIME& t){return (static_cast<ULONGLONG>(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
            if(!ticks(born))return std::nullopt;
            return DiagnosticRuntimeThreadCpu{id,ticks(born),ticks(kernel),ticks(user)};
        }catch(...){return std::nullopt;}
    }
    DiagnosticLoaderOwner diagnosticLoaderOwner() noexcept {
        const DWORD savedError=GetLastError();
        struct RestoreError{DWORD value;~RestoreError(){SetLastError(value);}} restore{savedError};
        DiagnosticLoaderOwner result;
        try{
            std::unique_lock lock(ownedLeaseMutex_,std::try_to_lock);
            if(!lock.owns_lock()){result.error=ERROR_BUSY;return result;}
            if(!stopProcess_){result.state="absent";result.error=ERROR_SUCCESS;return result;}
            result.pid=GetProcessId(stopProcess_);
            if(!result.pid){result.error=GetLastError();return result;}
            FILETIME born{},exited{},kernel{},user{};
            if(!GetProcessTimes(stopProcess_,&born,&exited,&kernel,&user)){result.error=GetLastError();return result;}
            result.birth=(static_cast<ULONGLONG>(born.dwHighDateTime)<<32)|born.dwLowDateTime;
            if(!result.birth){result.error=ERROR_INVALID_DATA;return result;}
            const DWORD wait=WaitForSingleObject(stopProcess_,0);
            if(wait==WAIT_TIMEOUT){result.state="running";result.error=ERROR_SUCCESS;}
            else if(wait==WAIT_OBJECT_0){
                result.state="exited";result.error=ERROR_SUCCESS;
                result.exitCodeKnown=GetExitCodeProcess(stopProcess_,&result.exitCode)!=FALSE;
                if(!result.exitCodeKnown)result.error=GetLastError();
            }else result.error=wait==WAIT_FAILED?GetLastError():ERROR_INVALID_DATA;
        }catch(...){result={};result.error=ERROR_GEN_FAILURE;}
        return result;
    }
    DiagnosticProcessIds diagnosticProcessIds() {
        DiagnosticProcessIds result;
        std::unique_lock lock(ownedLeaseMutex_, std::try_to_lock);
        if (!lock.owns_lock()) { result.error = ERROR_BUSY; return result; }
        if (!stopJob_) return result; // No owned Job is unknown, never a zero-CPU sample.
        // Fixed layout of JOBOBJECT_BASIC_PROCESS_ID_LIST with room for 16 IDs.
        struct {
            DWORD NumberOfAssignedProcesses, NumberOfProcessIdsInList;
            ULONG_PTR ProcessIdList[DiagnosticProcessIds::capacity];
        } list{};
        const bool ok = QueryInformationJobObject(stopJob_, JobObjectBasicProcessIdList,
            &list, sizeof(list), nullptr) != FALSE;
        result.error = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && result.error != ERROR_MORE_DATA) return result;
        result.available = true; // ERROR_MORE_DATA still supplies a bounded prefix.
        result.count = (std::min)(list.NumberOfProcessIdsInList, DiagnosticProcessIds::capacity);
        result.truncated = !ok || list.NumberOfAssignedProcesses > result.count ||
            list.NumberOfProcessIdsInList > result.count;
        for (DWORD i = 0; i < result.count; ++i) result.pids[i] = static_cast<DWORD>(list.ProcessIdList[i]);
        return result;
    }
    // Native recovery receipts require actual membership in THIS live Loader
    // Job. A PID list or matching executable name alone cannot certify ownership.
    bool ownsLiveProcess(HANDLE candidate) {
        if(!candidate||WaitForSingleObject(candidate,0)!=WAIT_TIMEOUT)return false;
        std::lock_guard lock(ownedLeaseMutex_);BOOL owned=FALSE;
        return stopJob_&&stopProcess_&&WaitForSingleObject(stopProcess_,0)==WAIT_TIMEOUT&&
            IsProcessInJob(candidate,stopJob_,&owned)&&owned;
    }
    bool acceptsSafetyStop(const nlohmann::json& event,const std::string& currentBrokerRun) {
        std::lock_guard lock(stateMutex_);
        return !stopping_&&state_.enabled&&state_.steamKnown&&state_.steamRunning&&
            pendingSafetyStop_.matches(event,intent_,currentBrokerRun);
    }
    bool setEnabled(bool enabled, const std::string& disabledReason = {},
        const std::function<void(const std::function<void()>&)>& withOwner = {}) {
        std::lock_guard lock(lifecycleMutex_);
        if(enabled&&exitFenced_.load(std::memory_order_acquire))return false;
        const auto publishIntent = [&] {
        { std::lock_guard stateLock(stateMutex_); state_.enabled = enabled; state_.connectionExhaustedAtPower.reset(); state_.connectionExhaustedOfficialBackend=false; disabledReason_=enabled?std::string{}:disabledReason; pendingSafetyStop_.clear();activeBindingRun_.clear();activeBindingFailure_={}; ++intent_; if(enabled){++launchIntent_;++admissionEpoch_;++crashResetEpoch_;++preparationEpoch_;} }
        };
        // Serialize only intent publication with the owning settings transaction.
        // Stop/join and worker startup must run after that settings lock is released.
        if (withOwner) withOwner(publishIntent); else publishIntent();
        if (!enabled) {
            { std::lock_guard stateLock(stateMutex_); stopping_ = true; }
            cancelPending();
            notifyListenerFacts();
            releaseOwnedStopLease(true); // Exact owned Loader remains independent from binding cancellation.
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            stopOwnedBinding();
#endif
            if (worker_.joinable()) worker_.join();
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            stopOwnedBinding(0); // Reap the completed stop; preserve actual remaining resources on failure.
#endif
            closeWake();
            publish("disabled"); return ownedBindingDestroyed_.load();
        }
        if (worker_.joinable()) { notifyListenerFacts(); return true; }
        bool wakeCreated=false;DWORD wakeError=ERROR_SUCCESS;
        {std::lock_guard wakeLock(wakeState_->mutex);
            ++wakeState_->generation;
            wake_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
            wakeState_->event=wake_;
            wakeCreated=wake_!=nullptr;if(!wakeCreated)wakeError=GetLastError();
        }
        if(!wakeCreated){publish("failed","event-create-failed",wakeError);return false;}
        { std::lock_guard stateLock(stateMutex_); stopping_ = false; }
        try { worker_ = std::thread([this] { run(); }); }
        catch (...) { closeWake(); publish("failed", "worker-create-failed"); return false; }
        return true;
    }
private:
    void observeSteamLocked(int presence, bool newSession, bool renewCrashBudget) {
        if(exitFenced_.load(std::memory_order_acquire))return;
        { std::lock_guard stateLock(stateMutex_);
          const bool known = presence >= 0, running = presence == 1;
          if (!newSession && state_.steamKnown == known && state_.steamRunning == running) return;
          // Publish the new intent and cancel its predecessor atomically with beginPending.
          // Otherwise the worker can renew first and this cancellation kills the NEW token.
          if(renewCrashBudget&&known&&running)++crashResetEpoch_;
          if(renewCrashBudget||(known&&!running)){state_.connectionExhaustedAtPower.reset();state_.connectionExhaustedOfficialBackend=false;}
          // Returning shared Steam facts need one check on the existing worker.
          // Same-session gaps do not renew crash budgets; their stopped health
          // window may otherwise retain the healthy 30s idle backstop. This
          // signal grants no action and keeps the original fresh health gates.
          if(state_.enabled&&!stopping_&&known&&running&&(!state_.steamKnown||!state_.steamRunning))
              connectionCheckRequested_=true;
          state_.steamKnown = known; state_.steamRunning = running; pendingSafetyStop_.clear();activeBindingRun_.clear();activeBindingFailure_={}; ++intent_;++launchIntent_;++admissionEpoch_;
          cancelPending(); }
        notifyListenerFacts();
    }
public:
    // Trusted explicit actions retain the original budget renewal. Automatic
    // session notifications use the typed overload below, including account gaps.
    void observeSteam(int presence, bool newSession = false) {
        std::lock_guard lock(lifecycleMutex_);
        observeSteamLocked(presence,newSession,newSession);
    }
    void observeSteam(int presence, const steamsession::Session& session) {
        std::lock_guard lock(lifecycleMutex_);
        const bool renew=presence==1&&crashSessions_.observe(session.root,session.pid,session.created,session.account);
        // Every raw session edge still revokes old binding/admission intent.
        // Missing/repeated account evidence must not refund the crash budget.
        observeSteamLocked(presence,true,renew);
    }
    // A mode switch is acknowledged before the target UI necessarily exists.
    // The shared Steam window events can recheck ONLY a deferred admission.
    // Healthy loaders never rebind/reinject from this wake; scheduled admission recovery is never postponed.
    bool requestLaunchAdmissionRefresh(bool retainedFrontendVerified = false, AdmissionRefreshReceipt* receipt = nullptr) {
        if(receipt)*receipt=AdmissionRefreshReceipt::none;
        auto observation=AdmissionRefreshReceipt::notified;
        std::lock_guard lock(lifecycleMutex_);
        if(exitFenced_.load(std::memory_order_acquire))return false;
        { std::lock_guard stateLock(stateMutex_);
          if(pendingSafetyStop_.active(intent_) || stopping_ || !state_.enabled || !state_.steamKnown || !state_.steamRunning || state_.pid) return false;
          if(state_.phase=="frontend-attached"&&state_.reason=="retained-frontend-connected"){
              // An unchanged proof of the attached frontend cannot admit a new
              // Loader. Changed facts and explicit refreshes still recheck it.
              if(retainedFrontendVerified)return false;
              observation=retainedAdmissionRefreshRequested_?AdmissionRefreshReceipt::retainedPending:
                  AdmissionRefreshReceipt::retainedRequested;
              retainedAdmissionRefreshRequested_=true; // No permission is granted here.
          }else if(state_.phase=="starting"&&state_.reason=="steam-ui-state-pending"&&
                   (state_.admissionDetail=="shared-mode-pending"||state_.admissionDetail=="live-busy")){
              // Existing prepared binding: request a fresh read without a new intent.
          }else{
              if(state_.phase!="waiting-manual-steam"){
                  if(state_.phase!="starting"||!state_.reason.empty())return false;
                  auto expected=FinalAdmissionWake::reading;
                  const bool queued=finalAdmissionWake_.compare_exchange_strong(expected,FinalAdmissionWake::queued);
                  if(queued){if(receipt)*receipt=AdmissionRefreshReceipt::finalQueued;return true;}
                  if(expected!=FinalAdmissionWake::queued)return false;
                  if(receipt)*receipt=AdmissionRefreshReceipt::finalPending;
                  return true;
              }
              // A repeated same-UI readiness hint must not cancel resource IO
              // already on this worker. OFF/session/new Ready keep their
              // original revocation. Fresh launch admission is still reread.
              if(!preparationActive_)++intent_;
              ++launchIntent_;
          }
        }
        notifyListenerFacts();
        if(receipt)*receipt=observation;
        return true;
    }
    bool requestConnectionCheck(bool waitingForPublicationOnly=false) {
        std::lock_guard lock(stateMutex_);
        if(exitFenced_.load(std::memory_order_acquire))return false;
        if(pendingSafetyStop_.active(intent_) || stopping_||!state_.enabled||!state_.steamKnown||!state_.steamRunning)return false;
        // A healthy owned Loader may still await its first native completion
        // receipt. Wake only its observer; never open launch/binding authority.
        if(state_.phase!="frontend-attached"&&!(state_.pid&&state_.phase=="loader-started")&&
           !(state_.phase=="waiting-manual-steam"&&state_.reason=="decky-context-already-loaded")&&
           !(retainedLoaderNativeGeneration_.load()&&state_.phase=="failed"&&state_.reason=="owned-loader-cleanup-pending"))return false;
        if(waitingForPublicationOnly&&connectionPublicationIntent_!=intent_)return false;
        // The worker consumes and clears this flag under the same lock.
        if(connectionCheckRequested_)return true;
        connectionCheckRequested_=true;
        notifyListenerFacts();return true;
    }
    bool requestTransportRecovery() {
        // Called by the watch worker: shutdown may join it while holding lifecycleMutex_.
        std::lock_guard stateLock(stateMutex_);
        if(exitFenced_.load(std::memory_order_acquire))return false;
        if(pendingSafetyStop_.active(intent_) || stopping_||!state_.enabled||!state_.steamKnown||!state_.steamRunning)return false;
        transportRecoveryRequested_=true;
        notifyListenerFacts();return true;
    }
    // Resource-only demand from committed Ready. No launch intent, admission/crash budget,
    // thread creation, or OFF revival. A healthy Loader defers this work until it is absent.
    bool requestPreparationRefresh(unsigned long long generation) {
        std::lock_guard lock(lifecycleMutex_);
        if(exitFenced_.load(std::memory_order_acquire))return false;
        {
            std::lock_guard stateLock(stateMutex_);
            if(pendingSafetyStop_.active(intent_) || !generation || generation<=lastPreparationGeneration_ || stopping_ || !state_.enabled)return false;
            lastPreparationGeneration_=generation;++preparationEpoch_;
        }
        notifyListenerFacts();
        return true;
    }
    // Transport-only demand from the EXISTING committed resume event.
    // No opt-in change, loader restart, settings write, or hardware action.
    bool requestBindingRefresh(unsigned long long generation) {
        std::lock_guard lock(lifecycleMutex_);
        if(exitFenced_.load(std::memory_order_acquire))return false;
        {
            std::lock_guard stateLock(stateMutex_);
            if(pendingSafetyStop_.active(intent_) || !generation || generation<=lastBindingGeneration_ || stopping_ ||
                !state_.enabled || !state_.steamKnown || !state_.steamRunning ||
                (!state_.pid && state_.phase!="frontend-attached") || (state_.phase!="loader-started" && state_.phase!="frontend-attached")) return false;
            lastBindingGeneration_=generation;++intent_;
            cancelPending(); // Same atomic token handoff as observeSteam.
        }
        notifyListenerFacts();
        return true;
    }
    // UI exit edge: atomic admission barrier only. No waits, locks, external
    // calls or early resource claims; the existing shutdown owner still joins
    // and returns every lease. Query/cancelled session end must not call this.
    void requestExitFence() noexcept { exitFenced_.store(true,std::memory_order_release); }
    bool exitFenced() const noexcept { return exitFenced_.load(std::memory_order_acquire); }
    void shutdown() { setEnabled(false); }
};
} // namespace ymcc::deckysidebar





