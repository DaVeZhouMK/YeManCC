#pragma once
#include "input_host_process_retirement.h"
#include <algorithm>
#include <cstdint>

// Exit-only fallback for the exact process HANDLE returned by our own
// CreateProcess. Never opens a PID, kills a peer process, grants endpoint/physical
// return or authorizes another Start. Caller serializes owner/stamp/spent under
// HostMx and has already attempted ordinary SHUTDOWN after producer retirement.
namespace ymcc::inputHost::exitRetirement {
struct Identity {
    DWORD pid=0;
    std::uint64_t creation=0;
    DWORD error=ERROR_SUCCESS;
    bool valid=false;
};
inline Identity identity(HANDLE process) noexcept {
    Identity result;
    if(!process){result.error=ERROR_INVALID_HANDLE;return result;}
    result.pid=GetProcessId(process);
    if(!result.pid){result.error=GetLastError();return result;}
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(process,&created,&exited,&kernel,&user)){
        result.error=GetLastError();return result;
    }
    ULARGE_INTEGER value{};value.LowPart=created.dwLowDateTime;value.HighPart=created.dwHighDateTime;
    result.creation=value.QuadPart;result.valid=result.creation!=0;
    if(!result.valid)result.error=ERROR_INVALID_DATA;
    return result;
}
struct Authority {
    bool appExit=false,exitFence=false,producerReturned=false,ordinaryStopAttempted=false;
    bool permits()const noexcept{return appExit&&exitFence&&producerReturned&&ordinaryStopAttempted;}
};
struct ForceResult {bool accepted=false;DWORD error=ERROR_SUCCESS;};
struct Receipt {
    bool authorityAccepted=false,hadProcess=false,identityVerified=false;
    DWORD initialWaitState=WAIT_FAILED,initialWaitError=ERROR_SUCCESS;
    bool forceAttempted=false,forceAccepted=false,forceBudgetSpent=false;
    DWORD forceError=ERROR_SUCCESS,waitBudgetMs=0;
    Identity expected{},observed{};
    processRetirement::Receipt process{};
};
struct WindowsPort {
    processRetirement::WaitResult observe(HANDLE h,DWORD ms)const noexcept{return processRetirement::waitOnce(h,ms);}
    Identity inspect(HANDLE h)const noexcept{return identity(h);}
    ForceResult force(HANDLE h)const noexcept{
        const bool ok=TerminateProcess(h,ERROR_PROCESS_ABORTED)!=FALSE;
        return {ok,ok?ERROR_SUCCESS:GetLastError()};
    }
    processRetirement::CloseResult close(HANDLE h)const noexcept{return processRetirement::closeOnce(h);}
};
// The test port must operate on the supplied exact same owned handle. A request
// accepted by TerminateProcess is not an exit or Close receipt. Unknown wait or
// identity never authorizes force; failed Close leaves the sole owner untouched.
template<class Port>
Receipt retire(HANDLE& owner,const Identity& expected,const Authority& authority,
               bool& forceSpent,DWORD forceWaitMs,Port& port) {
    Receipt r;r.authorityAccepted=authority.permits();r.hadProcess=owner!=nullptr;
    r.expected=expected;r.forceBudgetSpent=forceSpent;
    if(!r.authorityAccepted||!owner)return r;
    const auto initial=port.observe(owner,0);
    r.initialWaitState=initial.state;r.initialWaitError=initial.error;
    auto terminal=initial;
    if(initial.state==WAIT_TIMEOUT&&!forceSpent){
        r.observed=port.inspect(owner);
        r.identityVerified=expected.valid&&r.observed.valid&&expected.pid==r.observed.pid&&
            expected.creation==r.observed.creation;
        if(r.identityVerified){
            forceSpent=true;r.forceBudgetSpent=true;r.forceAttempted=true;
            const auto forced=port.force(owner);r.forceAccepted=forced.accepted;r.forceError=forced.error;
            // A failed request can race natural exit. Recheck without waiting;
            // a successful request gets one bounded observation, never a loop.
            r.waitBudgetMs=forced.accepted?(std::min)(forceWaitMs,DWORD{2000}):0;
            terminal=port.observe(owner,r.waitBudgetMs);
        }
    }
    r.process=processRetirement::retire(owner,[&](HANDLE){return terminal;},
        [&](HANDLE h){return port.close(h);});
    return r;
}
inline Receipt retire(HANDLE& owner,const Identity& expected,const Authority& authority,
                      bool& forceSpent,DWORD forceWaitMs=1000) {
    WindowsPort port;return retire(owner,expected,authority,forceSpent,forceWaitMs,port);
}
} // namespace ymcc::inputHost::exitRetirement
