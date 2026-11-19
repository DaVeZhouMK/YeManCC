#pragma once
#include "rtss_process_runtime.h"
#include <utility>

namespace ymcc::rtssstop {
struct Target {
    rtssprocess::Identity identity;
    bool attempted=false,requested=false,exited=false;
    DWORD error=ERROR_SUCCESS;
    const char* reason="not-attempted";
};
struct Result {
    bool ok=false;
    unsigned attempted=0,stopped=0,pending=0;
    DWORD error=ERROR_SUCCESS;
    const char* reason="not-attempted";
    std::vector<Target> targets;
};
class Handle {
    HANDLE value_=nullptr;
public:
    explicit Handle(HANDLE value):value_(value){}
    ~Handle(){if(value_)CloseHandle(value_);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other)noexcept:value_(std::exchange(other.value_,nullptr)){}
    HANDLE get()const{return value_;}
};
inline bool heldIdentity(HANDLE process,const rtssprocess::Identity& expected,DWORD session) {
    DWORD actualSession=0;FILETIME born{},ended{},kernel{},user{};
    wchar_t path[32768]{};DWORD count=static_cast<DWORD>(std::size(path));
    return GetProcessId(process)==expected.pid&&
        ProcessIdToSessionId(expected.pid,&actualSession)&&actualSession==session&&
        GetProcessTimes(process,&born,&ended,&kernel,&user)&&
        ((ULONGLONG(born.dwHighDateTime)<<32)|born.dwLowDateTime)==expected.processCreated&&
        QueryFullProcessImageNameW(process,0,path,&count)&&count&&count<std::size(path)&&
        _wcsicmp(path,expected.path.c_str())==0;
}
inline const std::vector<std::wstring>& familyNames() {
    static const std::vector<std::wstring> names{
        L"RTSS",L"RTSSHooksLoader64",L"RTSSHooksLoader32",L"EncoderServer64",L"EncoderServer"};
    return names;
}

// The caller owns the unique no-game receipt and shared power cancellation.
// Inventory hints are never action authority. Prepare every write handle first,
// refresh the one game owner, then dispatch under that owner's existing lock.
template<class Allowed,class Refresh,class WithNoGame>
Result stopFamily(Allowed&& allowed,Refresh&& refresh,WithNoGame&& withNoGame,
                  HANDLE powerCancelled,ULONGLONG deadline) {
    Result result;
    struct Prepared {
        Handle process;size_t target;
        Prepared(Handle&& value,size_t index):process(std::move(value)),target(index){}
        Prepared(Prepared&&)=default;Prepared(const Prepared&)=delete;
    };
    std::vector<Prepared> prepared;
    const auto summarize=[&] {
        // A partial dispatch or exception still owes an exact held-object receipt.
        for(auto& entry:prepared){
            auto& target=result.targets[entry.target];
            if(!target.exited&&WaitForSingleObject(entry.process.get(),0)==WAIT_OBJECT_0){
                target.exited=true;target.reason="exited";
            }
        }
        result.stopped=result.pending=0;
        for(const auto& target:result.targets){if(target.exited)++result.stopped;else ++result.pending;}
    };
    const auto finish=[&]() -> Result {summarize();return std::move(result);};
    const auto current=[&] {
        const auto live=[&] {return powerCancelled&&powerCancelled!=INVALID_HANDLE_VALUE&&
            GetTickCount64()<deadline&&WaitForSingleObject(powerCancelled,0)==WAIT_TIMEOUT;};
        return live()&&allowed()&&live();
    };
    try {
        if(!current()){result.reason="context-changed";result.error=ERROR_CANCELLED;return finish();}
        auto observation=rtssprocess::observe(familyNames());
        if(!observation.known&&observation.error==ERROR_INVALID_PARAMETER&&current())
            observation=rtssprocess::observe(familyNames()); // one exact exit-race read, no action replay
        if(!observation.known){result.reason="process-observation-unknown";result.error=observation.error;return finish();}
        DWORD session=0;
        if(!ProcessIdToSessionId(GetCurrentProcessId(),&session)){
            result.reason="session-unknown";result.error=rtssprocess::failure();return finish();
        }
        result.targets.reserve(observation.processes.size());
        prepared.reserve(observation.processes.size());
        for(const auto& identity:observation.processes){
            if(!current()){result.reason="context-changed";result.error=ERROR_CANCELLED;return finish();}
            const auto index=result.targets.size();result.targets.push_back({identity});
            auto& target=result.targets.back();
            Handle process(OpenProcess(PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,identity.pid));
            if(!process.get()){
                const auto error=GetLastError();
                if(error==ERROR_INVALID_PARAMETER){target.exited=true;target.reason="already-exited";continue;}
                target.error=error;target.reason="process-unavailable";
                result.reason=target.reason;result.error=error;return finish();
            }
            const auto live=WaitForSingleObject(process.get(),0);
            if(live==WAIT_OBJECT_0){target.exited=true;target.reason="already-exited";continue;}
            if(live!=WAIT_TIMEOUT||!heldIdentity(process.get(),identity,session)){
                target.reason="identity-changed";target.error=ERROR_INVALID_DATA;
                result.reason=target.reason;result.error=target.error;return finish();
            }
            prepared.emplace_back(std::move(process),index);
        }
        if(!current()||!refresh()||!current()){
            result.reason="game-or-context-changed";result.error=ERROR_CANCELLED;return finish();
        }
        bool entered=false,validated=false,revoked=false;
        const bool admitted=withNoGame([&] {
            if(entered){revoked=true;return false;}
            entered=true;
            if(!current()){revoked=true;return false;}
            // Validate the whole prepared set before the first Kernel action.
            for(auto& entry:prepared){
                auto& target=result.targets[entry.target];
                const auto live=WaitForSingleObject(entry.process.get(),0);
                if(live==WAIT_OBJECT_0){target.exited=true;target.reason="already-exited";continue;}
                if(live!=WAIT_TIMEOUT||!heldIdentity(entry.process.get(),target.identity,session)){
                    target.reason="identity-changed";target.error=ERROR_INVALID_DATA;
                    result.reason=target.reason;result.error=target.error;return false;
                }
            }
            validated=true;
            for(auto& entry:prepared){
                auto& target=result.targets[entry.target];
                if(target.exited)continue;
                if(!current()){revoked=true;break;}
                target.attempted=true;++result.attempted;
                target.requested=TerminateProcess(entry.process.get(),ERROR_CANCELLED)!=FALSE;
                if(!target.requested){
                    target.error=GetLastError();target.reason="termination-rejected";
                    target.exited=WaitForSingleObject(entry.process.get(),0)==WAIT_OBJECT_0;
                }else target.reason="termination-requested";
            }
            return true;
        });
        if(!admitted||!entered||!validated||revoked){
            if(result.error==ERROR_SUCCESS){result.reason="game-or-context-changed";result.error=ERROR_CANCELLED;}
            return finish();
        }
        // Waiting never holds the game lock. All processes retain their exact
        // kernel objects; a replacement with the same PID is never adopted.
        for(auto& entry:prepared){
            auto& target=result.targets[entry.target];
            if(target.exited||!target.requested)continue;
            if(!current()){revoked=true;break;}
            const auto admittedAt=GetTickCount64();
            if(admittedAt>=deadline){revoked=true;break;}
            const auto remaining=deadline-admittedAt;
            const DWORD waitMs=static_cast<DWORD>((std::min)(remaining,ULONGLONG{MAXDWORD-1}));
            const HANDLE waits[]{entry.process.get(),powerCancelled};
            const auto observed=WaitForMultipleObjects(2,waits,FALSE,waitMs);
            if(observed==WAIT_OBJECT_0){target.exited=true;target.reason="exited";}
            else {
                target.reason=observed==WAIT_TIMEOUT?"exit-pending":"wait-cancelled-or-failed";
                if(observed==WAIT_FAILED)target.error=GetLastError();
                revoked=true;break;
            }
        }
        for(const auto& target:result.targets){if(target.exited)++result.stopped;else ++result.pending;}
        if(revoked||!current()){
            result.reason="context-changed-or-exit-pending";result.error=ERROR_CANCELLED;return finish();
        }
        const auto final=rtssprocess::observe(familyNames());
        if(!final.known){result.reason="exit-observation-unknown";result.error=final.error;return finish();}
        if(!final.processes.empty()){
            result.reason=result.pending?"termination-rejected":"rtss-restarted";
            result.error=ERROR_BUSY;return finish();
        }
        // Confirmation is a second, read-only use of the same owner receipt.
        // Waiting and the final inventory read may have revoked that owner.
        bool confirmed=false;
        if(!current()||!withNoGame([&]{confirmed=true;return current();})||!confirmed||!current()){
            result.reason="game-or-context-changed";result.error=ERROR_CANCELLED;return finish();
        }
        summarize();
        result.ok=result.pending==0;
        result.reason=result.ok?(result.attempted?"stopped":"already-stopped"):"exit-pending";
        return finish();
    }catch(const std::bad_alloc&){result.reason="allocation-failed";result.error=ERROR_NOT_ENOUGH_MEMORY;return finish();}
    catch(...){result.reason="stop-exception";result.error=ERROR_GEN_FAILURE;return finish();}
}
}
