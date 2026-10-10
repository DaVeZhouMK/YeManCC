#pragma once
// YMCC Decky sidebar: opt-in process ownership only. No game/fan/TDP actions.
// No idle polling: one worker waits on events/process exits; finite binding windows only.
#include <windows.h>
#include "steam_session_observer.h"
#include "decky_sidebar_temp.h"
#include "decky_sidebar_launch.h"
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
struct State {
    bool enabled = false;
    bool steamKnown = false;
    bool steamRunning = false;
    DWORD pid = 0;
    unsigned long long revision = 0;
    std::string phase = "disabled";
    std::string reason;
    DWORD error = 0;
    bool bindingRetry = false;
    unsigned bindingAttempts = 0;
    std::string launchStage;
    std::string launchMode;
};
struct Config {
    std::wstring executable;
    std::wstring home;
    std::function<std::string()> preflight; // Source/hash/plugin/debug-flag checks, startup only.
    std::function<void(const State&)> changed;
    std::function<std::string()> refreshBinding;
    std::function<void()> beginPending;
    std::function<void()> cancelPending;
    std::function<std::string()> prepareBinding; // Before launch, separate from resource validation.
    std::function<HANDLE()> takeContextProcess; // Transfers a verified read-only process lease; never a kill target.
};
class Runtime {
    std::mutex lifecycleMutex_, stateMutex_;
    std::thread worker_;
    HANDLE wake_ = nullptr;
    bool stopping_ = false;
    std::atomic<bool> reporting_{true};
    unsigned long long intent_ = 0;
    unsigned long long lastBindingGeneration_ = 0;
    unsigned long long launchIntent_ = 0; // Only user enable or a genuine Steam state/session change.
    Config config_;
    State state_;
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
    void publish(std::string phase, std::string reason = {}, DWORD error = 0, DWORD pid = 0, bool retry = false, unsigned attempts = 0, std::string launchStage = {}, std::string launchMode = {}) {
        State copy;
        { std::lock_guard lock(stateMutex_); state_.phase = std::move(phase); state_.reason = std::move(reason);
          state_.error = error; state_.pid = pid; state_.bindingRetry=retry; state_.bindingAttempts=attempts;state_.launchStage=std::move(launchStage);state_.launchMode=std::move(launchMode); ++state_.revision; copy = state_; }
        try { if (reporting_.load() && config_.changed) config_.changed(copy); } catch (...) {} // Best-effort presentation only.
    }
    bool launchAllowed(unsigned long long intent) {
        std::lock_guard lock(stateMutex_);
        return !stopping_ && state_.enabled && state_.steamKnown && state_.steamRunning && intent_ == intent;
    }
    bool beginPending(unsigned long long intent) {
        std::lock_guard lock(stateMutex_);
        if(stopping_ || !state_.enabled || !state_.steamKnown || !state_.steamRunning || intent_!=intent)return false;
        if(config_.beginPending)config_.beginPending();return true;
    }
    void cancelPending() noexcept {
        try { if(config_.cancelPending)config_.cancelPending(); } catch (...) {}
        // Only this dedicated worker's file checks, never the existing hardware/Steam observer thread.
        // Completion is still checked before any launch; cancellation is not proof an IO was canceled.
        if(worker_.joinable())CancelSynchronousIo(worker_.native_handle());
    }
    static bool retryableBinding(const std::string& reason) {
        // Steam can be present before its own CDP listener exists. Only the existing finite startup/resume window retries this.
        return reason=="live-debug-unavailable" || reason=="mirror-bootstrap-context-unavailable" || reason=="live-context-unavailable" ||
            reason=="mirror-bootstrap-async-error" || reason=="mirror-bootstrap-deadline" ||
            reason=="mirror-bootstrap-discovery-timeout" || reason=="mirror-bootstrap-context-exited" ||
            reason=="mirror-bootstrap-steam-unavailable";
    }
    void run() {
        HANDLE process=nullptr,job=nullptr,context=nullptr;
        LoaderTemporaryRun temporary;
        unsigned long long attempted=~0ULL,launchAttempted=~0ULL;
        ymcc::steamsession::RetryWindow retries; // Reuse unchanged 1/3/8/20-second budget.
        bool launchPending=false,deferredLaunch=false,retryDue=false;
        unsigned attempts=0;
        auto closeContext=[&]{if(context){CloseHandle(context);context=nullptr;}};
        auto stopWindow=[&]{retries={};launchPending=false;deferredLaunch=false;retryDue=false;attempts=0;};
        auto closeOwned=[&]{
            closeContext();stopWindow();
            bool empty=true;
            if(job){
                TerminateJobObject(job,0);empty=false;
                const auto until=GetTickCount64()+1500;
                do{
                    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
                    if(!QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr))break;
                    if(!info.ActiveProcesses){empty=true;break;}
                    Sleep(5); // Bounded owned-process shutdown only, never idle status polling.
                }while(GetTickCount64()<until);
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
            if(!config_.takeContextProcess)return;
            HANDLE next=config_.takeContextProcess();
            if(!next)return;
            if(!reason.empty()){CloseHandle(next);return;}
            if(WaitForSingleObject(next,0)!=WAIT_TIMEOUT){CloseHandle(next);reason="mirror-bootstrap-context-exited";return;}
            closeContext();context=next;
        };
        auto startOwned=[&](unsigned long long intent){
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
                    if(assigned&&!stopping_&&state_.enabled&&state_.steamKnown&&state_.steamRunning&&intent_==intent)
                        resumed=ResumeThread(child.hThread)!=static_cast<DWORD>(-1);
                }
                if(!resumed){
                    const DWORD error=GetLastError();TerminateProcess(process,1);
                    CloseHandle(child.hThread);closeOwned();publish("failed","ownership-failed",error);
                }else{
                    CloseHandle(child.hThread);
                    publish("loader-started","mirror-handshake-pending",0,child.dwProcessId,false,attempts,launch.stage,launch.mode);
                }
            }catch(...){closeOwned();publish("failed","environment-failed");}
        };
        auto bind=[&](unsigned long long intent,bool beforeLaunch,bool renew){
            if(renew&&!beginPending(intent))return;
            std::string reason;
            ++attempts;
            try{reason=beforeLaunch?config_.prepareBinding():config_.refreshBinding();takeContext(reason);}
            catch(...){reason="mirror-binding-failed";}
            if(!launchAllowed(intent))return;
            if(reason.empty()){
                retries={};retryDue=false;
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
                const bool pending=retryableBinding(reason)&&retries.timeout(GetTickCount64())!=INFINITE;
                if(!pending){retries={};launchPending=false;deferredLaunch=false;}
                else if(beforeLaunch){launchPending=true;deferredLaunch=true;}
                publish(beforeLaunch?"unavailable":"loader-started",reason,0,process?GetProcessId(process):0,pending,attempts);
            }
        };
        for(;;){
            bool stop,enabled,known,steam;
            unsigned long long intent,launchIntent;
            {std::lock_guard lock(stateMutex_);stop=stopping_;enabled=state_.enabled;known=state_.steamKnown;
                steam=state_.steamRunning;intent=intent_;launchIntent=launchIntent_;}
            if(stop){closeOwned();break;}
            if(process&&WaitForSingleObject(process,0)==WAIT_OBJECT_0){
                DWORD code=0;GetExitCodeProcess(process,&code);closeOwned();attempted=intent;
                publish("failed","loader-exited",code);continue;
            }
            if(!enabled||!known||!steam){
                closeOwned();publish(!enabled?"disabled":"waiting-steam",known?"":"steam-state-unknown");
            }else if(process&&intent!=attempted){
                attempted=intent;launchAttempted=launchIntent;stopWindow();
                if(config_.refreshBinding){retries.begin(GetTickCount64());bind(intent,false,true);}
                else publish("loader-started","mirror-handshake-pending",0,GetProcessId(process));
            }else if(!process&&launchIntent!=launchAttempted){
                attempted=intent;launchAttempted=launchIntent;closeContext();stopWindow();publish("starting");
                std::string failure;
                try{if(!beginPending(intent))continue;if(config_.preflight)failure=config_.preflight();}
                catch(...){failure="preflight-failed";}
                if(!launchAllowed(intent))continue;
                if(!failure.empty())publish("unavailable",failure);
                else if(config_.prepareBinding){launchPending=true;retries.begin(GetTickCount64());bind(intent,true,false);}
                else startOwned(intent);
            }else if(retryDue){
                retryDue=false;
                if(launchPending)bind(intent,true,true);
                else if(process&&config_.refreshBinding)bind(intent,false,true);
            }
            HANDLE handles[3]{wake_};DWORD count=1;
            const DWORD processIndex=process?count++:MAXDWORD;
            if(process)handles[processIndex]=process;
            const DWORD contextIndex=context?count++:MAXDWORD;
            if(context)handles[contextIndex]=context;
            const DWORD result=WaitForMultipleObjects(count,handles,FALSE,retries.timeout(GetTickCount64()));
            if(result==WAIT_TIMEOUT){retryDue=retries.take(GetTickCount64());continue;}
            if(process&&result==WAIT_OBJECT_0+processIndex){
                DWORD code=0;GetExitCodeProcess(process,&code);closeOwned();publish("failed","loader-exited",code);
            }else if(context&&result==WAIT_OBJECT_0+contextIndex){
                closeContext();
                // No lifecycleMutex: UI shutdown may hold it while joining this worker.
                std::lock_guard lock(stateMutex_);
                if(!stopping_&&state_.enabled&&state_.steamKnown&&state_.steamRunning&&process){
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
    bool setEnabled(bool enabled) {
        std::lock_guard lock(lifecycleMutex_);
        { std::lock_guard stateLock(stateMutex_); state_.enabled = enabled; ++intent_; if(enabled)++launchIntent_; }
        if (!enabled) {
            { std::lock_guard stateLock(stateMutex_); stopping_ = true; }
            cancelPending();
            if (wake_) SetEvent(wake_);
            if (worker_.joinable()) worker_.join();
            if (wake_) { CloseHandle(wake_); wake_ = nullptr; }
            publish("disabled"); return true;
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
          state_.steamKnown = known; state_.steamRunning = running; ++intent_;++launchIntent_;
          cancelPending(); }
        if (wake_) SetEvent(wake_);
    }
    // Transport-only demand from the EXISTING committed resume event.
    // No opt-in change, loader restart, settings write, or hardware action.
    bool requestBindingRefresh(unsigned long long generation) {
        std::lock_guard lock(lifecycleMutex_);
        {
            std::lock_guard stateLock(stateMutex_);
            if (!generation || generation<=lastBindingGeneration_ || stopping_ ||
                !state_.enabled || !state_.steamKnown || !state_.steamRunning ||
                !state_.pid || state_.phase!="loader-started") return false;
            lastBindingGeneration_=generation;++intent_;
            cancelPending(); // Same atomic token handoff as observeSteam.
        }
        if(wake_)SetEvent(wake_);
        return true;
    }
    void shutdown() { setEnabled(false); }
};
} // namespace ymcc::deckysidebar





