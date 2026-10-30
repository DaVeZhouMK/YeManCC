#pragma once
// YMCC Decky sidebar: opt-in process ownership only. No game/fan/TDP actions.
// No idle polling: one worker waits on events/process exits; startup checks followed by slow admission recovery, and capped outage recovery only.
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
#include "decky_sidebar_health.h"
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
#include <string>
#include <thread>
#include <vector>

namespace ymcc::deckysidebar {
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
};
struct LaunchAdmission {
    bool allowed;
    std::string reason;
    bool retryable;
    std::string detail;
    bool bindingOnly = false; // Exact retained frontend: no official Loader process authority.
    // Legacy bool callbacks remain observation-only when false.
    LaunchAdmission(bool permit = true, std::string why = {}, bool retry = false, std::string context = {}, bool retained = false)
        : allowed(permit), reason(why.empty() && !permit ? "ymcc-restart-observation-only" : std::move(why)),
          retryable(retry), detail(std::move(context)), bindingOnly(retained) {}
};
class AdmissionWindow {
    ymcc::steamsession::RetryWindow retries_;
    unsigned long long epoch_ = 0;
    bool known_ = false, active_ = false;
    ULONGLONG opened_ = 0, slowDue_ = 0;
    unsigned attempts_ = 0;
public:
    static constexpr DWORD recoveryIntervalMs = 30000;
    void blocked(unsigned long long epoch, ULONGLONG now, bool retryable) {
        if (!known_ || epoch_ != epoch) {
            known_ = true; epoch_ = epoch; attempts_ = 1; retries_ = {};
            opened_ = now; slowDue_ = 0; active_ = retryable;
            if (retryable) retries_.begin(now);
        } else if (!retryable) stop();
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
    void stop() { retries_ = {}; slowDue_ = 0; active_ = false; }
    void completed() { stop(); known_ = false; attempts_ = 0; }
};

#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
enum class BindingMode { prepare, refresh, retained };
struct BindingPlan { nlohmann::json request; std::string reason; };
#endif
struct Config {
    std::wstring executable;
    std::wstring home;
    std::function<std::string()> preflight; // Source/hash/plugin/debug-flag checks, startup only.
    std::function<void(const State&)> changed;
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
    // Historical callback regressions only; this branch is absent from the product.
    std::function<std::string()> refreshBinding;
    std::function<void()> beginPending;
    std::function<void()> cancelPending;
    std::function<std::string()> prepareBinding; // Before launch, separate from resource validation.
    std::function<HANDLE()> takeContextProcess; // Transfers a verified read-only process lease; never a kill target.
#else
    std::shared_ptr<ymcc::deckybinding::Client> ownedBinding;
    // Typed request construction only. Transport/bootstrap/watch execute in the owned child.
    std::function<BindingPlan(BindingMode, std::uint64_t)> bindingPlan;
    std::function<void(const nlohmann::json&)> bindingDiagnostic;
#endif
    std::function<LaunchAdmission()> launchAdmission; // Verify current Steam session and actual UI mode; never start Steam.
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
    std::function<std::string()> retainedBinding; // Transport-only attach; must recheck exact actual-target frontend.
#endif
    std::function<void()> connectionRecovery; // Existing worker, bounded document budget, no idle polling.
    // Existing worker only. Resource/debug preparation grants no Loader or binding authority.
    // Callback must honor canContinue before each side effect and bound its own IO.
    std::function<std::string(const std::function<bool()>&)> prepareEnvironment;
};
class Runtime {
    std::mutex lifecycleMutex_, stateMutex_, ownedLeaseMutex_;
    // Duplicated handles to THIS Runtime's suspended/Job-assigned child only.
    // Stop can revoke the child before joining an uncooperative binding callback;
    // worker-owned handles and its stack still require cooperative completion.
    HANDLE stopJob_ = nullptr, stopProcess_ = nullptr;
    std::thread worker_;
    HANDLE wake_ = nullptr;
    bool stopping_ = false;
    std::atomic<bool> reporting_{true};
    std::atomic<bool> ownedBindingDestroyed_{true};
    std::atomic<DWORD> ownedBindingCleanupError_{ERROR_SUCCESS};
    State lastPublished_; bool hasPublished_ = false;
    unsigned long long intent_ = 0;
    unsigned long long lastBindingGeneration_ = 0;
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
    std::uint64_t bindingRequestGeneration_ = 0; // Per-attempt IPC identity; never a retry-budget or power-generation reset.
#endif
    unsigned long long lastPreparationGeneration_ = 0, preparationEpoch_ = 0;
    ymcc::deckyhealth::PendingStop pendingSafetyStop_; // Exact current binding; normal owner-thread OFF still owns persistence.
    std::string activeBindingRun_; // Published only after typed Client admits this binding request.
    bool transportRecoveryRequested_ = false;
    bool retainedAdmissionRefreshRequested_ = false; // Observation only; worker owns admission/launch.
    unsigned long long launchIntent_ = 0; // Only user enable or a genuine Steam state/session change.
    unsigned long long crashResetEpoch_ = 0;
    unsigned long long admissionEpoch_ = 0; // Explicit/session intents renew admission; a completed start can later have a new outage.
    Config config_;
    State state_;
    std::string disabledReason_; // Only a reviewed owner-thread safety close can persist a fault reason.
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
    void publish(std::string phase, std::string reason = {}, DWORD error = 0, DWORD pid = 0, bool retry = false, unsigned attempts = 0, std::string launchStage = {}, std::string launchMode = {}, bool admissionRetry = false, unsigned admissionAttempts = 0, std::string admissionDetail = {}, ULONGLONG admissionElapsedMs = 0, DWORD admissionNextRetryMs = 0, bool admissionRecovery = false) {
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
              lastPublished_.ownedBindingDestroyed==state_.ownedBindingDestroyed && lastPublished_.ownedBindingCleanupError==state_.ownedBindingCleanupError) return;
          state_.phase = std::move(phase); state_.reason = std::move(reason);
          state_.error = error; state_.pid = pid; state_.bindingRetry=retry; state_.bindingAttempts=attempts;state_.launchStage=std::move(launchStage);state_.launchMode=std::move(launchMode);
          state_.admissionRetry=admissionRetry;state_.admissionAttempts=admissionAttempts;state_.admissionDetail=std::move(admissionDetail);
          state_.admissionElapsedMs=admissionElapsedMs;state_.admissionNextRetryMs=admissionNextRetryMs;state_.admissionRecovery=admissionRecovery;
          ++state_.revision; copy = state_;
          lastPublished_ = copy; hasPublished_ = true; }
        try { if (reporting_.load() && config_.changed) config_.changed(copy); } catch (...) {} // Best-effort presentation only.
    }
    bool launchAllowed(unsigned long long intent) {
        std::lock_guard lock(stateMutex_);
        return !pendingSafetyStop_.active(intent_) && !stopping_ && state_.enabled && state_.steamKnown && state_.steamRunning && intent_ == intent;
    }
    bool beginPending(unsigned long long intent) {
        std::lock_guard lock(stateMutex_);
        if(pendingSafetyStop_.active(intent_) || stopping_ || !state_.enabled || !state_.steamKnown || !state_.steamRunning || intent_!=intent)return false;
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
        if(config_.beginPending)config_.beginPending();return true;
#else
        return config_.ownedBinding && config_.ownedBinding->enable(intent);
#endif
    }
    void cancelPending() noexcept {
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
    void bindingDiagnostic(nlohmann::json detail,const char* kind) noexcept {
        try {detail["bindingPacketKind"]=kind;if(config_.bindingDiagnostic)config_.bindingDiagnostic(detail);}catch(...){}
    }
    std::string performOwnedBinding(unsigned long long intent,BindingMode mode,bool& revalidateSession) {
        revalidateSession=false;
        if(!config_.ownedBinding||!config_.bindingPlan)return "owned-binding-required";
        BindingPlan plan;
        // Demand-only markers isolate parent plan/resource work from the child
        // bootstrap deadline. An unfinished pair diagnoses a blocked parent;
        // neither marker starts recovery or grants any operation authority.
        const char* scope=mode==BindingMode::retained?"binding-plan-retained":mode==BindingMode::prepare?"binding-plan-prepare":"binding-plan-refresh";
        bindingDiagnostic({{"bootstrapStage","plan-started"},{"scope",scope}},"diagnostic");
        const auto planStarted=GetTickCount64();
        try {plan=config_.bindingPlan(mode,++bindingRequestGeneration_);}
        catch(...){bindingDiagnostic({{"bootstrapStage","plan-finished"},{"scope",scope},{"elapsedMs",GetTickCount64()-planStarted},{"ok",false},{"reason","mirror-binding-request-failed"}},"diagnostic");return "mirror-binding-request-failed";}
        bindingDiagnostic({{"bootstrapStage","plan-finished"},{"scope",scope},{"elapsedMs",GetTickCount64()-planStarted},{"ok",plan.reason.empty()},{"reason",plan.reason}},"diagnostic");
        if(!plan.reason.empty())return plan.reason;
        if(!launchAllowed(intent))return "mirror-bootstrap-canceled";
        const auto started=config_.ownedBinding->begin(plan.request,intent);
        if(!started)return "mirror-binding-worker-start-failed";
        ownedBindingDestroyed_.store(false);ownedBindingCleanupError_.store(ERROR_SUCCESS);
        {std::lock_guard lock(stateMutex_);
            if(stopping_||!state_.enabled||intent_!=intent)return "mirror-bootstrap-canceled";
            activeBindingRun_=plan.request.at("bootstrap").at("runId").get<std::string>();
        }
        const auto deadline=GetTickCount64()+9000; // Existing bootstrap deadline (7s) plus IPC/exit grace.
        for(;;){
            if(!launchAllowed(intent))return "mirror-bootstrap-canceled";
            const auto packet=config_.ownedBinding->next();
            using Packet=decltype(packet);
            if(packet.kind==Packet::Kind::message){
                const auto kind=packet.value.value("kind",std::string{});
                const auto detail=packet.value.value("detail",nlohmann::json::object());
                if(kind=="diagnostic"){retainTerminalForOwner(detail,intent);bindingDiagnostic(detail,"diagnostic");continue;}
                if(kind=="result"){
                    bindingDiagnostic(detail,"result");
                    const auto reason=detail.value("reason",std::string("mirror-bootstrap-failed"));
                    // A rejected pre-write lease can become valid without a new
                    // observer identity (e.g. ActiveUser briefly unavailable).
                    // Rebuild the plan and verify every identity again. Never
                    // replay a changed/unknown write or grant old-session trust.
                    revalidateSession=!detail.value("ok",false)&&reason=="mirror-bootstrap-session-changed"&&
                        detail.contains("mutated")&&detail["mutated"].is_boolean()&&!detail["mutated"].get<bool>();
                    return detail.value("ok",false)?std::string{}:reason;
                }
                return "mirror-binding-worker-protocol-invalid";
            }
            if(packet.kind==Packet::Kind::ended||packet.kind==Packet::Kind::failed){
                bindingDiagnostic({{"ok",false},{"mutated",nullptr},{"mutationState","unknown"},{"reason",packet.reason}},"result");
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
        HANDLE process=nullptr,job=nullptr,context=nullptr;
        LoaderTemporaryRun temporary;
        unsigned long long attempted=~0ULL,launchAttempted=~0ULL;
        unsigned long long preparedEpoch=~0ULL, preparationAttemptEpoch=~0ULL, preparationAttemptIntent=~0ULL;
        ymcc::steamsession::RetryWindow retries; // Reuse unchanged 1/3/8/20-second budget.
        AdmissionWindow admissionChecks;
        // A cold resource worker may miss its 2.5s deadline even with valid
        // bytes. Reuse the finite 1/3/8/20s budget, only after an admitted launch
        // failed. Do not poll healthy/absent/retained sessions, reset crash
        // budgets, or retry bad hashes/incomplete worker cleanup.
        ymcc::steamsession::RetryWindow resourceRetries;
        unsigned long long resourceAdmissionEpoch=~0ULL,resourcePreparationEpoch=~0ULL;
        bool resourceWindowOpened=false,resourcePending=false,resourceDue=false;
        // Park scheduling while admission is absent; retain the original clock
        // and remaining attempts. Clearing it here makes a later verified mode
        // return lose recovery forever, or tempts a refill on every UI event.
        const auto cancelResourceRetry=[&]{resourcePending=false;resourceDue=false;};
        ConnectionRetryWindow outage,loaderRecovery;
        LoaderCrashBudget crashBudget;
        unsigned long long crashEpoch=~0ULL;DWORD lastLoaderExit=0;
        bool outageDue=false,loaderRecoveryDue=false;
        ULONGLONG loaderStarted=0;
        bool launchPending=false,deferredLaunch=false,retryDue=false;
        unsigned attempts=0;
        auto closeContext=[&]{if(context){CloseHandle(context);context=nullptr;}};
        auto stopWindow=[&]{retries={};outage.reset();outageDue=false;launchPending=false;deferredLaunch=false;retryDue=false;attempts=0;};
        auto closeOwned=[&]{
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
                    CloseHandle(job);job=nullptr;
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
            if(process){WaitForSingleObject(process,1500);CloseHandle(process);process=nullptr;}
            if(job){CloseHandle(job);job=nullptr;}
            // Image sections may outlive exit notifications briefly. Cap cleanup attempts.
            bool cleaned=temporary.path().empty();
            for(unsigned retry=0;empty&&!cleaned&&retry<20;++retry){
                cleaned=temporary.cleanupAfterJobEmpty(true);if(!cleaned)Sleep(50);
            }
            if(!cleaned)temporary.abandon(); // Unknown/live Job or locked file: retain, never widen deletion scope.
        };
        auto takeContext=[&](std::string& reason){
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            if(!config_.takeContextProcess)return;
            HANDLE next=config_.takeContextProcess();
#else
            HANDLE next=config_.ownedBinding?config_.ownedBinding->takeContextProcess():nullptr;
            if(!next&&reason.empty()){reason="mirror-bootstrap-context-watch-unavailable";return;}
#endif
            if(!next)return;
            if(!reason.empty()){CloseHandle(next);return;}
            if(WaitForSingleObject(next,0)!=WAIT_TIMEOUT){CloseHandle(next);reason="mirror-bootstrap-context-exited";return;}
            closeContext();context=next;
        };
        auto startOwned=[&](unsigned long long intent){
            // Resource preparation and bootstrap IO can outlive an admission.
            // Recheck before creating a child; a loaded/unknown/newer context
            // must not become an official Loader reinjection/RestartJSContext.
            if (config_.launchAdmission) {
                LaunchAdmission finalAdmission;
                try { finalAdmission = config_.launchAdmission(); }
                catch (...) { finalAdmission = {false, "steam-ui-state-pending", true}; }
                if (!launchAllowed(intent)) return;
                if (!finalAdmission.allowed || finalAdmission.bindingOnly) {
                    unsigned long long epoch=0;
                    {std::lock_guard lock(stateMutex_);if(stopping_||!state_.enabled||intent_!=intent)return;epoch=admissionEpoch_;}
                    // A successful binding already consumed the launch attempt.
                    // Keep the normal permission retry alive after this late
                    // refusal; no new window event is required. Exact retained
                    // admission hands off to transport only, never a Loader.
                    launchAttempted=~0ULL;
                    if(finalAdmission.bindingOnly)attempted=~0ULL;
                    closeContext();const auto now=GetTickCount64();
                    admissionChecks.blocked(epoch,now,finalAdmission.retryable||finalAdmission.bindingOnly);
                    publish("waiting-manual-steam", finalAdmission.reason,0,0,false,0,{}, {},
                        admissionChecks.timeout(now)!=INFINITE,admissionChecks.attempts(),finalAdmission.detail,
                        admissionChecks.elapsed(now),admissionChecks.timeout(now)==INFINITE?0:admissionChecks.timeout(now),admissionChecks.recovering());
                    return;
                }
            }
            job=CreateJobObjectW(nullptr,nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){
                const DWORD error=GetLastError();closeOwned();publish("failed","job-create-failed",error);return;
            }
            STARTUPINFOW startup{};startup.cb=sizeof(startup);
            startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
            PROCESS_INFORMATION child{};
            std::wstring command=L"\""+config_.executable+L"\"";
            try{
                const auto temp=temporary.prepare(config_.home);
                auto env=environment(config_.home,temp);
                const auto directory=std::filesystem::path(config_.executable).parent_path().wstring();
                const auto launch=ymcc::deckylaunch::create(config_.executable,command,env.data(),directory,startup,child);
                if(!launch.created){
                    closeOwned();publish("failed","launch-failed",launch.error,0,false,attempts,launch.stage,launch.mode);return;
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
                    CloseHandle(child.hThread);closeOwned();publish("failed","ownership-failed",error);
                }else{
                    CloseHandle(child.hThread);
                    loaderStarted=GetTickCount64();
                    publish("loader-started","mirror-handshake-pending",0,child.dwProcessId,false,attempts,launch.stage,launch.mode);
                }
            }catch(...){closeOwned();publish("failed","environment-failed");}
        };
        auto bind=[&](unsigned long long intent,bool beforeLaunch,bool renew){
            if(renew&&!beginPending(intent))return;
            std::string reason;bool revalidateSession=false;
            ++attempts;
            try{
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                reason=beforeLaunch?config_.prepareBinding():config_.refreshBinding();
#else
                reason=performOwnedBinding(intent,beforeLaunch?BindingMode::prepare:BindingMode::refresh,revalidateSession);
#endif
                takeContext(reason);
            }
            catch(...){reason="mirror-binding-failed";}
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            if(!reason.empty())stopOwnedBinding();
#endif
            if(!launchAllowed(intent))return;
            if(reason.empty()){
                retries={};outage.reset();outageDue=false;retryDue=false;
                if(beforeLaunch){
                    // A delayed launch revalidates resources once, not once per network retry.
                    if(deferredLaunch&&config_.preflight){
                        try{reason=config_.preflight();}catch(...){reason="preflight-failed";}
                    }
                    if(!launchAllowed(intent))return;
                    launchPending=false;deferredLaunch=false;
                    if(reason.empty())startOwned(intent);
                    else{closeContext();publish("unavailable",reason,0,0,false,attempts);}
                }else publish("loader-started","mirror-handshake-pending",0,GetProcessId(process),false,attempts);
            }else{
                const bool pending=revalidateSession||retryableBinding(reason);
                if(pending){retries={};outage.begin(GetTickCount64());}
                if(!pending){retries={};outage.reset();launchPending=false;deferredLaunch=false;}
                else if(beforeLaunch){launchPending=true;deferredLaunch=true;}
                publish(beforeLaunch?"unavailable":"loader-started",reason,0,process?GetProcessId(process):0,pending,attempts);
            }
        };
        for(;;){
            bool stop,enabled,known,steam,recoveryRequested,retainedAdmissionRequested,safetyPending,retainedFrontend;
            unsigned long long intent,launchIntent,admissionEpoch,crashResetEpoch,preparationEpoch;
            {std::lock_guard lock(stateMutex_);stop=stopping_;enabled=state_.enabled;known=state_.steamKnown;
                steam=state_.steamRunning;intent=intent_;launchIntent=launchIntent_;admissionEpoch=admissionEpoch_;crashResetEpoch=crashResetEpoch_;preparationEpoch=preparationEpoch_;
                retainedFrontend=state_.phase=="frontend-attached"||(state_.phase=="reconnecting"&&state_.reason=="retained-frontend-rebind");
                recoveryRequested=transportRecoveryRequested_;transportRecoveryRequested_=false;
                retainedAdmissionRequested=retainedAdmissionRefreshRequested_;retainedAdmissionRefreshRequested_=false;safetyPending=pendingSafetyStop_.active(intent_); }
            if(stop){closeOwned();break;}
            if(resourceAdmissionEpoch!=admissionEpoch||resourcePreparationEpoch!=preparationEpoch){
                resourceAdmissionEpoch=admissionEpoch;resourcePreparationEpoch=preparationEpoch;
                // Ready may arrive during this already admitted resource wait.
                // Retain the pending attempt, then reread admission; Ready alone
                // gives no new launch authority and cannot wake healthy owners.
                if(resourcePending)launchAttempted=~0ULL;
                cancelResourceRetry();resourceRetries={};resourceWindowOpened=false;
            }
            if(safetyPending){
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
            if(crashEpoch!=crashResetEpoch){crashEpoch=crashResetEpoch;crashBudget.reset();loaderRecovery.reset();loaderRecoveryDue=false;}
            if(process&&loaderStarted&&GetTickCount64()-loaderStarted>=LoaderCrashBudget::stableUptimeMs){crashBudget.healthy(GetTickCount64()-loaderStarted);loaderRecovery.reset();}
            if(process&&WaitForSingleObject(process,0)==WAIT_OBJECT_0){
                DWORD code=0;GetExitCodeProcess(process,&code);lastLoaderExit=code;
                const bool retry=crashBudget.exited(GetTickCount64()-loaderStarted);closeOwned();attempted=intent;
                if(retry)loaderRecovery.begin(GetTickCount64());
                publish("failed",retry?"loader-exited":"loader-crash-loop",code);continue;
            }
            LaunchAdmission admission;
            const auto readAdmission=[&]{
                if(enabled && known && steam && !process && config_.launchAdmission){
                    try { admission=config_.launchAdmission(); }
                    catch (...) { admission={false,"steam-ui-state-pending",true,"admission-read-failed"}; }
                }
            };
            // An already verified frontend only needs transport attachment; its
            // original retained-binding worker rechecks that exact document.
            // Do not put disk preparation in front of this existing consumer.
            readAdmission();if(enabled&&known&&steam&&!process&&!launchAllowed(intent))continue;
            bool preparationRan=false;
            // One preparation per enable/committed Ready, even when Steam is explicitly absent.
            // Unknown presence waits for the existing observer. A live retained
            // CEF context is as important as an owned Loader: defer resource IO
            // while either lease exists, including committed Ready refreshes.
            // A requested retained rebind may briefly close the old context
            // lease before the new watch arrives; that handoff still has
            // priority over optional resource maintenance on this worker.
            // Deferred preparation grants no rebind, restart or launch authority.
            // Unfinished/failed work can retry on a NEW intent/Ready, never an idle timer or spin.
            if(enabled && known && !process && !context && !retainedFrontend && !(admission.allowed&&admission.bindingOnly) && config_.prepareEnvironment && preparedEpoch!=preparationEpoch &&
                (preparationAttemptEpoch!=preparationEpoch || preparationAttemptIntent!=intent)) {
                preparationAttemptEpoch=preparationEpoch;preparationAttemptIntent=intent;
                const std::function<bool()> canContinue=[this,intent,preparationEpoch]{
                    std::lock_guard lock(stateMutex_);
                    return !stopping_ && state_.enabled && state_.steamKnown && intent_==intent && preparationEpoch_==preparationEpoch;
                };
                if(!canContinue())continue;
                std::string result;
                preparationRan=true;
                try { result=config_.prepareEnvironment(canContinue); }
                catch(...) { result="preparation-failed"; }
                if(!canContinue())continue;
                if(result.empty())preparedEpoch=preparationEpoch;
            }
            // Preparation may yield across a Steam mode/session change. Keep
            // the original fresh admission read after any resource work; it
            // must not reuse an earlier permission to create a new Loader.
            if(preparationRan){readAdmission();if(enabled&&known&&steam&&!process&&!launchAllowed(intent))continue;}
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
                loaderRecovery.reset();loaderRecoveryDue=false;closeOwned();publish(!enabled?"disabled":"waiting-steam",known?"":"steam-state-unknown");
            }else if(!process && admission.allowed && admission.bindingOnly && !crashBudget.blocked()){
                cancelResourceRetry();
                // Never fall through to process creation, including a failed binding.
                // Exact-target validation runs inside retainedBinding before credentials change.
                if(intent!=attempted||retryDue||outageDue){
                    if(intent!=attempted)stopWindow();
                    attempted=intent;launchAttempted=launchIntent;retryDue=false;outageDue=false;
                    loaderRecovery.reset();loaderRecoveryDue=false;closeContext();
                    publish("reconnecting", "retained-frontend-rebind");
                    std::string failure;bool revalidateSession=false;
                    try {
                        if(!beginPending(intent))continue;
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                        failure=config_.retainedBinding?config_.retainedBinding():"mirror-retained-binding-unavailable";
#else
                        failure=performOwnedBinding(intent,BindingMode::retained,revalidateSession);
#endif
                        takeContext(failure);
                    }catch(...){failure="mirror-binding-failed";}
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                    if(!failure.empty())stopOwnedBinding();
#endif
                    if(!launchAllowed(intent))continue;
                    const bool retry=!failure.empty()&&(revalidateSession||retryableBinding(failure));
                    if(retry)outage.begin(GetTickCount64());else outage.reset();
                    publish(failure.empty()?"frontend-attached":retry?"reconnecting":"unavailable",
                            failure.empty()?"retained-frontend-connected":failure,0,0,retry,1);
                }
            }else if(!process && !admission.allowed){
                if(resourcePending)launchAttempted=~0ULL;
                cancelResourceRetry();
                // Preserve a pending owned-loader restart while its UI mode is temporarily unavailable.
                if(!admission.retryable){loaderRecovery.reset();loaderRecoveryDue=false;}
                // If mode confirmation disappeared during binding, its recovery starts a fresh validated attempt.
                if(launchPending)launchAttempted=~0ULL;
                stopWindow(); closeContext();
                const auto now=GetTickCount64();admissionChecks.blocked(admissionEpoch,now,admission.retryable);
                publish("waiting-manual-steam", admission.reason, 0, 0, false, 0, {}, {},
                    admissionChecks.timeout(now)!=INFINITE, admissionChecks.attempts(), admission.detail,
                    admissionChecks.elapsed(now),admissionChecks.timeout(now)==INFINITE?0:admissionChecks.timeout(now),admissionChecks.recovering());
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
            }else if(!process&&(launchIntent!=launchAttempted||loaderRecoveryDue||(resourcePending&&resourceDue))){
                loaderRecoveryDue=false;resourceDue=false;resourcePending=false;
                attempted=intent;launchAttempted=launchIntent;closeContext();stopWindow();publish("starting");
                std::string failure;
                try{if(!beginPending(intent))continue;if(config_.preflight)failure=config_.preflight();}
                catch(...){failure="preflight-failed";}
                if(!launchAllowed(intent))continue;
                if(failure.empty())cancelResourceRetry();
                if(!failure.empty()){
                    if(failure=="resource-worker-deadline"){
                        // The worker adapter reports this only after exact Job,
                        // reader and handle cleanup; otherwise it returns
                        // resource-worker-cleanup-incomplete. Recheck admission
                        // and preflight before any binding or Loader creation.
                        if(!resourceWindowOpened){resourceWindowOpened=true;resourceRetries.begin(GetTickCount64());}
                        resourcePending=true;
                    }else cancelResourceRetry();
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
            }else if(retryDue||outageDue){
                retryDue=false;outageDue=false;
                if(launchPending)bind(intent,true,true);
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
                else if(process&&config_.refreshBinding)bind(intent,false,true);
#else
                else if(process&&config_.ownedBinding)bind(intent,false,true);
#endif
            }
            // Automatic and manual share the same repair orchestrator/budget.
            // Its exact state/session/resource checks run only on this owner.
            if(!process&&enabled&&known&&steam&&config_.connectionRecovery){try{config_.connectionRecovery();}catch(...){}}
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
            const DWORD timeout=(std::min)({retries.timeout(now),admissionChecks.timeout(now),outage.timeout(now),loaderRecovery.timeout(now),resourcePending?resourceRetries.timeout(now):INFINITE});
            const DWORD result=WaitForMultipleObjects(count,handles,FALSE,timeout);
            if(result==WAIT_TIMEOUT){const auto due=GetTickCount64();retryDue=retries.take(due);outageDue=outage.take(due);loaderRecoveryDue=loaderRecovery.take(due);admissionChecks.take(due);resourceDue=resourcePending&&resourceRetries.take(due);continue;}
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            if((bindingLeases.ready&&result==WAIT_OBJECT_0+bindingEventIndex)||(bindingLeases.child&&result==WAIT_OBJECT_0+bindingChildIndex)){
                const auto packet=config_.ownedBinding->next();using Packet=decltype(packet);
                if(packet.kind==Packet::Kind::message){
                    const auto kind=packet.value.value("kind",std::string{});
                    const auto detail=packet.value.value("detail",nlohmann::json::object());
                    if(kind=="diagnostic")retainTerminalForOwner(detail,intent);
                    bindingDiagnostic(detail,kind.c_str());
                }else if(packet.kind==Packet::Kind::ended||packet.kind==Packet::Kind::failed){
                    stopOwnedBinding();closeContext();
                    const auto reason=packet.reason.empty()?std::string("mirror-bootstrap-context-exited"):packet.reason;
                    bindingDiagnostic({{"bootstrapStage","failed"},{"reason",reason},{"mutated",nullptr},{"mutationState","unknown"}},"diagnostic");
                    if(enabled&&known&&steam)outage.begin(GetTickCount64());
                }
                continue;
            }
#endif
            if(process&&result==WAIT_OBJECT_0+processIndex){
                DWORD code=0;GetExitCodeProcess(process,&code);lastLoaderExit=code;
                const bool retry=crashBudget.exited(GetTickCount64()-loaderStarted);closeOwned();
                if(retry)loaderRecovery.begin(GetTickCount64());publish("failed",retry?"loader-exited":"loader-crash-loop",code);
            }else if(context&&result==WAIT_OBJECT_0+contextIndex){
                closeContext();
                // No lifecycleMutex: UI shutdown may hold it while joining this worker.
                std::lock_guard lock(stateMutex_);
                if(!stopping_&&state_.enabled&&state_.steamKnown&&state_.steamRunning&&(process||state_.phase=="frontend-attached")){
                    ++intent_;cancelPending(); // Observe only; NEVER opens launchIntent_.
                }
            }else if(result==WAIT_FAILED){closeOwned();publish("failed","wait-failed",GetLastError());break;}
        }
        publish("disabled");
    }
public:
    Runtime() = default;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    ~Runtime() { reporting_.store(false); shutdown(); }
    void initialize(Config config) { std::lock_guard lock(lifecycleMutex_); config_ = std::move(config); }
    State snapshot() { std::lock_guard lock(stateMutex_); return state_; }
    bool tryDiagnosticState(State& result) {
        std::unique_lock lock(stateMutex_, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        result = state_;
        return true;
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
    bool acceptsSafetyStop(const nlohmann::json& event,const std::string& currentBrokerRun) {
        std::lock_guard lock(stateMutex_);
        return !stopping_&&state_.enabled&&state_.steamKnown&&state_.steamRunning&&
            pendingSafetyStop_.matches(event,intent_,currentBrokerRun);
    }
    bool setEnabled(bool enabled, const std::string& disabledReason = {}) {
        std::lock_guard lock(lifecycleMutex_);
        { std::lock_guard stateLock(stateMutex_); state_.enabled = enabled; disabledReason_=enabled?std::string{}:disabledReason; pendingSafetyStop_.clear();activeBindingRun_.clear(); ++intent_; if(enabled){++launchIntent_;++admissionEpoch_;++crashResetEpoch_;++preparationEpoch_;} }
        if (!enabled) {
            { std::lock_guard stateLock(stateMutex_); stopping_ = true; }
            cancelPending();
            if (wake_) SetEvent(wake_);
            releaseOwnedStopLease(true); // Exact owned Loader remains independent from binding cancellation.
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            stopOwnedBinding();
#endif
            if (worker_.joinable()) worker_.join();
#ifndef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
            stopOwnedBinding(0); // Reap the completed stop; preserve actual remaining resources on failure.
#endif
            if (wake_) { CloseHandle(wake_); wake_ = nullptr; }
            publish("disabled"); return ownedBindingDestroyed_.load();
        }
        if (worker_.joinable()) { if (wake_) SetEvent(wake_); return true; }
        wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!wake_) { publish("failed", "event-create-failed", GetLastError()); return false; }
        { std::lock_guard stateLock(stateMutex_); stopping_ = false; }
        try { worker_ = std::thread([this] { run(); }); }
        catch (...) { CloseHandle(wake_); wake_ = nullptr; publish("failed", "worker-create-failed"); return false; }
        return true;
    }
    void observeSteam(int presence, bool newSession = false) {
        std::lock_guard lock(lifecycleMutex_);
        { std::lock_guard stateLock(stateMutex_);
          const bool known = presence >= 0, running = presence == 1;
          if (!newSession && state_.steamKnown == known && state_.steamRunning == running) return;
          // Publish the new intent and cancel its predecessor atomically with beginPending.
          // Otherwise the worker can renew first and this cancellation kills the NEW token.
          if(newSession&&known&&running)++crashResetEpoch_;
          state_.steamKnown = known; state_.steamRunning = running; pendingSafetyStop_.clear();activeBindingRun_.clear(); ++intent_;++launchIntent_;++admissionEpoch_;
          cancelPending(); }
        if (wake_) SetEvent(wake_);
    }
    // A mode switch is acknowledged before the target UI necessarily exists.
    // The shared Steam window events can recheck ONLY a deferred admission.
    // Healthy loaders never rebind/reinject from this wake; scheduled admission recovery is never postponed.
    bool requestLaunchAdmissionRefresh() {
        std::lock_guard lock(lifecycleMutex_);
        { std::lock_guard stateLock(stateMutex_);
          if(pendingSafetyStop_.active(intent_) || stopping_ || !state_.enabled || !state_.steamKnown || !state_.steamRunning || state_.pid) return false;
          if(state_.phase=="frontend-attached"&&state_.reason=="retained-frontend-connected"){
              retainedAdmissionRefreshRequested_=true; // No permission is granted here.
          }else{
              if(state_.phase!="waiting-manual-steam")return false;
              ++intent_;++launchIntent_;
          }
        }
        if (wake_) SetEvent(wake_); return true;
    }
    bool requestConnectionCheck() {
        std::lock_guard lock(stateMutex_);
        if(pendingSafetyStop_.active(intent_) || stopping_||!state_.enabled||!state_.steamKnown||!state_.steamRunning||state_.pid)return false;
        if(state_.phase!="frontend-attached"&&!(state_.phase=="waiting-manual-steam"&&state_.reason=="decky-context-already-loaded"))return false;
        if(wake_)SetEvent(wake_);return true;
    }
    bool requestTransportRecovery() {
        // Called by the watch worker: shutdown may join it while holding lifecycleMutex_.
        std::lock_guard stateLock(stateMutex_);
        if(pendingSafetyStop_.active(intent_) || stopping_||!state_.enabled||!state_.steamKnown||!state_.steamRunning)return false;
        transportRecoveryRequested_=true;
        if(wake_)SetEvent(wake_);return true;
    }
    // Resource-only demand from committed Ready. No launch intent, admission/crash budget,
    // thread creation, or OFF revival. A healthy Loader defers this work until it is absent.
    bool requestPreparationRefresh(unsigned long long generation) {
        std::lock_guard lock(lifecycleMutex_);
        {
            std::lock_guard stateLock(stateMutex_);
            if(pendingSafetyStop_.active(intent_) || !generation || generation<=lastPreparationGeneration_ || stopping_ || !state_.enabled)return false;
            lastPreparationGeneration_=generation;++preparationEpoch_;
        }
        if(wake_)SetEvent(wake_);
        return true;
    }
    // Transport-only demand from the EXISTING committed resume event.
    // No opt-in change, loader restart, settings write, or hardware action.
    bool requestBindingRefresh(unsigned long long generation) {
        std::lock_guard lock(lifecycleMutex_);
        {
            std::lock_guard stateLock(stateMutex_);
            if(pendingSafetyStop_.active(intent_) || !generation || generation<=lastBindingGeneration_ || stopping_ ||
                !state_.enabled || !state_.steamKnown || !state_.steamRunning ||
                (!state_.pid && state_.phase!="frontend-attached") || (state_.phase!="loader-started" && state_.phase!="frontend-attached")) return false;
            lastBindingGeneration_=generation;++intent_;
            cancelPending(); // Same atomic token handoff as observeSteam.
        }
        if(wake_)SetEvent(wake_);
        return true;
    }
    void shutdown() { setEnabled(false); }
};
} // namespace ymcc::deckysidebar





