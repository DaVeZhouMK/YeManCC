#pragma once

// Game-owner actions operate on the selected game, never on a Steam role or
// a process-group member. Keep the exact kernel object through the action/wait.
namespace GameProcessControl {
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value)CloseHandle(value);}
    Handle()=default;Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
static bool identityArguments(const json& args,DWORD& pid,ULONGLONG& birth) {
    if(!args.is_object()||!args.contains("pid")||!args["pid"].is_number_integer()||
        !args.contains("processCreated")||!args["processCreated"].is_string())return false;
    const auto raw=args["pid"].get<long long>();
    if(raw<=4||raw>0xffffffffLL||raw==GetCurrentProcessId())return false;
    pid=static_cast<DWORD>(raw);birth=nativeJsonProcessCreated(args["processCreated"]);
    return birth!=0&&std::to_string(birth)==args["processCreated"].get<std::string>();
}
static bool heldIdentity(HANDLE process,ULONGLONG birth,const std::wstring& path={}) {
    if(WaitForSingleObject(process,0)!=WAIT_TIMEOUT||nativeProcessCreatedFromHandle(process)!=birth)return false;
    if(path.empty())return true;
    std::wstring live(32768,L'\0');DWORD count=static_cast<DWORD>(live.size());
    if(!QueryFullProcessImageNameW(process,0,live.data(),&count)||count==0||count>=live.size())return false;
    live.resize(count);return _wcsicmp(live.c_str(),path.c_str())==0;
}
static json close(const json& args) {
    auto result=[](bool ok,bool attempted,bool exited,const char* reason,DWORD error=0){
        return json{{"ok",ok},{"attempted",attempted},{"exited",exited},{"reason",reason},{"win32Error",error}};
    };
    DWORD pid=0;ULONGLONG birth=0;
    if(!identityArguments(args,pid,birth)||!args.contains("path")||!args["path"].is_string()||
        !args.contains("identityGeneration")||!args["identityGeneration"].is_number_unsigned())
        return result(false,false,false,"invalid-identity");
    const auto path=U2W(args["path"].get<std::string>());
    if(path.empty()||path.size()>32767||path.find(L'\0')!=path.npos)return result(false,false,false,"invalid-identity");
    NativeGameSelectionReceipt selected;
    ymcc::sleeppower::SharedPowerV1 power;
    if(!captureSharedPowerLifecycle(power)||!nativeValveReadCachedSelection(&selected))return result(false,false,false,"game-not-current");
    const auto& game=selected.target.game;
    if(game.pid!=pid||game.processCreated!=birth||_wcsicmp(game.path.c_str(),path.c_str())!=0||
        selected.target.identityGeneration!=args["identityGeneration"].get<unsigned long long>())return result(false,false,false,"game-changed");
    auto cancellation=observeSharedPowerRevocation(power);
    if(!cancellation.event()||cancellation.cancelled())return result(false,false,false,"power-not-current");
    Handle process;process.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE|SYNCHRONIZE,FALSE,pid);
    if(!process.value)return result(false,false,false,"open-failed",GetLastError());
    DWORD ownSession=0,targetSession=0;
    if(!heldIdentity(process.value,birth,path)||!ProcessIdToSessionId(GetCurrentProcessId(),&ownSession)||
        !ProcessIdToSessionId(pid,&targetSession)||ownSession!=targetSession)return result(false,false,false,"identity-changed");
    bool attempted=false;DWORD error=0;
    const bool requested=nativeValveWithCurrentSelection(selected,[&]{
        if(!heldIdentity(process.value,birth,path)||!sharedPowerLifecycleCurrent(power)||
            cancellation.cancelled()||g_exitRequested.load(std::memory_order_acquire))return false;
        attempted=true;const bool ok=TerminateProcess(process.value,ERROR_CANCELLED)!=FALSE;
        if(!ok)error=GetLastError();return ok;
    });
    if(!requested)return result(false,attempted,false,attempted?"terminate-failed":"game-changed",error);
    const HANDLE waits[]={process.value,cancellation.event()};
    const bool exited=WaitForMultipleObjects(2,waits,FALSE,5000)==WAIT_OBJECT_0;
    return result(exited,true,exited,exited?"exited":"exit-pending");
}
static json waitExit(const json& args) {
    auto result=[](bool known,bool exited,const char* reason){return json{{"known",known},{"exited",exited},{"reason",reason}};};
    DWORD pid=0;ULONGLONG birth=0;
    if(!identityArguments(args,pid,birth))return result(false,false,"invalid-identity");
    DWORD timeout=10000;
    if(args.contains("timeoutMs")){
        if(!args["timeoutMs"].is_number_integer())return result(false,false,"invalid-timeout");
        const auto raw=args["timeoutMs"].get<long long>();if(raw<0||raw>10000)return result(false,false,"invalid-timeout");timeout=static_cast<DWORD>(raw);
    }
    ymcc::sleeppower::SharedPowerV1 power;
    if(!captureSharedPowerLifecycle(power))return result(false,false,"power-not-current");
    auto cancellation=observeSharedPowerRevocation(power);
    if(!cancellation.event()||cancellation.cancelled())return result(false,false,"power-not-current");
    Handle process;process.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
    if(!process.value)return GetLastError()==ERROR_INVALID_PARAMETER?result(true,true,"not-found"):result(false,false,"open-failed");
    const auto created=nativeProcessCreatedFromHandle(process.value);
    if(!created)return result(false,false,"identity-unknown");
    if(created!=birth)return result(true,true,"original-instance-exited");
    if(!sharedPowerLifecycleCurrent(power)||cancellation.cancelled())return result(false,false,"power-not-current");
    const HANDLE waits[]={process.value,cancellation.event()};
    const DWORD observed=WaitForMultipleObjects(2,waits,FALSE,timeout);
    if(observed==WAIT_OBJECT_0)return result(true,true,"exited");
    if(observed==WAIT_TIMEOUT)return result(true,false,"still-running");
    return result(false,false,"wait-cancelled-or-failed");
}
static void focusResumed(const json& requested,const json& result,const ymcc::sleeppower::SharedPowerV1& power) {
    if(!requested.is_array()||!result.contains("resumedPids")||!result["resumedPids"].is_array()||
        result["resumedPids"].size()!=1||!result["resumedPids"][0].is_number_integer()||
        !sharedPowerLifecycleCurrent(power))return;
    const auto raw=result["resumedPids"][0].get<long long>();
    if(raw<=4||raw>0xffffffffLL)return;
    const DWORD resumed=static_cast<DWORD>(raw);DWORD matched=0;ULONGLONG birth=0;unsigned matches=0;
    const auto& identities=result.contains("resumedProcesses")&&result["resumedProcesses"].is_array()
        ? result["resumedProcesses"] : requested;
    for(const auto& value:identities){
        DWORD pid=0;ULONGLONG created=0;
        if(identityArguments(value,pid,created)&&pid==resumed){matched=pid;birth=created;++matches;}
    }
    // Unknown old records may still be resumed by their native markers. They
    // cannot authorize focus by taking a new birth from a reused PID.
    if(matches==1)focusSingleResumedGame(std::vector<DWORD>{matched},birth,power);
}
}
