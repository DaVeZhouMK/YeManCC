#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <array>
#include <atomic>
#include <limits>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"

// Never acquire the installed Steam source slot, even by accident.
static HANDLE WINAPI fixtureSourceMutex(LPSECURITY_ATTRIBUTES attributes,BOOL initialOwner,LPCWSTR requested){
    if(!requested||!std::wstring_view(requested).starts_with(L"Local\\YeManCC.SteamSource.v1.")){
        SetLastError(ERROR_INVALID_NAME);return nullptr;
    }
    const auto name=L"Local\\YMCCFixture.Action.Source."+std::to_wstring(GetCurrentProcessId());
    return ::CreateMutexW(attributes,initialOwner,name.c_str());
}
#pragma push_macro("CreateMutexW")
#undef CreateMutexW
#define CreateMutexW fixtureSourceMutex
#include "steam_source_owner.h"
#undef CreateMutexW
#pragma pop_macro("CreateMutexW")
static auto& fixtureSource(){static ymcc::steamsession::SourceOwner source;return source;}

// Observe the actual kernel result; no identity/API return is substituted.
static ULONG observedServer=0;
static unsigned observedQueries=0;
static BOOL observedPipeServer(HANDLE pipe,PULONG pid){
    const auto ok=::GetNamedPipeServerProcessId(pipe,pid);
    ++observedQueries;if(ok)observedServer=*pid;return ok;
}
// Transport observations preserve the real API result, error, timeout and flags.
static unsigned observedSends=0,observedActionSends=0;
static DWORD observedSendError=0,observedSendTimeout=0,observedSendFlags=0;
static bool observedSendSucceeded=false;
static DWORD_PTR observedSendAck=0;
static LRESULT observedSend(HWND w,UINT m,WPARAM p,LPARAM l,UINT flags,UINT timeout,PDWORD_PTR ack){
    const auto result=::SendMessageTimeoutW(w,m,p,l,flags,timeout,ack);const auto error=GetLastError();
    if(m==WM_COPYDATA&&l&&reinterpret_cast<const COPYDATASTRUCT*>(l)->dwData==0x594d5341)++observedActionSends;
    ++observedSends;observedSendSucceeded=result!=0;observedSendError=error;
    observedSendTimeout=timeout;observedSendFlags=flags;observedSendAck=ack?*ack:0;SetLastError(error);return result;
}
#ifdef ACTION_NATIVE_COMPOSITION
#include "native_resource_observation.inc"
static void nativeObserveActionCAS(volatile LONG*,LONG,LONG,LONG) noexcept;
static DWORD nativeRegistryActivePid=0;
static unsigned nativeRegistryReads=0,nativeUnexpectedRegistryReads=0;
static LSTATUS WINAPI nativePresenceRegRead(HKEY hive,LPCWSTR key,LPCWSTR name,DWORD flags,LPDWORD type,PVOID data,LPDWORD bytes){
    ++nativeRegistryReads;
    if(hive!=HKEY_CURRENT_USER||!key||!name||std::wstring(key)!=L"Software\\Valve\\Steam\\ActiveProcess"||std::wstring(name)!=L"pid"||flags!=RRF_RT_REG_DWORD){++nativeUnexpectedRegistryReads;return ERROR_FILE_NOT_FOUND;}
    if(!bytes||!data||*bytes<sizeof(DWORD))return ERROR_MORE_DATA;
    std::memcpy(data,&nativeRegistryActivePid,sizeof(DWORD));*bytes=sizeof(DWORD);if(type)*type=REG_DWORD;return ERROR_SUCCESS;
}
#define RegGetValueW nativePresenceRegRead
static unsigned nativeSuccessfulCAS=0;
static bool nativeInjectAfterClaim=false;
static void revokeNativeAfterClaim() noexcept;
static LONG observedNativeCAS(volatile LONG* target,LONG exchange,LONG comparand){
    const LONG result=::InterlockedCompareExchange(target,exchange,comparand);
    if(exchange==1&&comparand==0&&result==0)++nativeSuccessfulCAS;
    nativeObserveActionCAS(target,exchange,comparand,result);
    return result;
}
#pragma push_macro("InterlockedCompareExchange")
#undef InterlockedCompareExchange
#define InterlockedCompareExchange observedNativeCAS
#define OpenProcess nativeObserveOpenProcess
#define OpenProcessToken nativeObserveOpenProcessToken
#define CreateFileW nativeObserveCreateFile
#define CreateNamedPipeW nativeObserveCreatePipe
#define OpenFileMappingW nativeObserveOpenMapping
#define CreateFileMappingW nativeObserveCreateMapping
#define OpenEventW nativeObserveOpenEvent
#define CreateEventW nativeObserveCreateEvent
#define MapViewOfFile nativeObserveMap
#define UnmapViewOfFile nativeObserveUnmap
#define CloseHandle nativeObserveClose
#define CreateWindowExW nativeObserveCreateWindow
#define DestroyWindow nativeObserveDestroyWindow
#endif
#define SendMessageTimeoutW observedSend
#define GetNamedPipeServerProcessId observedPipeServer
#include "steam_client_action_producer.h"
#ifdef ACTION_NATIVE_COMPOSITION
#include "steam_information_producer.h"
#endif
#undef GetNamedPipeServerProcessId
#undef SendMessageTimeoutW
#ifdef ACTION_NATIVE_COMPOSITION
#undef InterlockedCompareExchange
#undef RegGetValueW
#pragma pop_macro("InterlockedCompareExchange")
#undef OpenProcess
#undef OpenProcessToken
#undef CreateFileW
#undef CreateNamedPipeW
#undef OpenFileMappingW
#undef CreateFileMappingW
#undef OpenEventW
#undef CreateEventW
#undef MapViewOfFile
#undef UnmapViewOfFile
#undef CloseHandle
#undef CreateWindowExW
#undef DestroyWindow
#include "sleep_power_state.h"
#include "sleep_power_dispatch.h"
#include "steam_command_coordinator.h"
#endif

namespace fs=std::filesystem;
namespace sa=ymcc::steamaction;
namespace si=ymcc::steaminfo;
using Json=nlohmann::json;
static_assert(sa::messageTag==0x594d5341);
#ifdef ACTION_NATIVE_COMPOSITION
static bool nativeCompositionMode(const std::wstring&);
static int nativeCompositionClient(const std::wstring&,DWORD,std::uint64_t,const fs::path&);
static int nativeTargetMain(const std::wstring&);
#endif
static void need(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}
static std::wstring module(){std::wstring s(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,s.data(),DWORD(s.size()));need(n&&n<s.size(),"module path");s.resize(n);return s;}
static std::wstring quote(const std::wstring& v){return L"\""+v+L"\"";}
static void writeNew(const fs::path& file,const Json& value){
    const auto body=value.dump(2);si::Handle h(CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    need(h.value&&h.value!=INVALID_HANDLE_VALUE,"exclusive result create");DWORD written=0;
    need(WriteFile(h.value,body.data(),DWORD(body.size()),&written,nullptr)&&written==body.size()&&FlushFileBuffers(h.value),"result write/flush");
}
static Json readJson(const fs::path& file){
    si::Handle h(CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));need(h.value&&h.value!=INVALID_HANDLE_VALUE,"result read");
    LARGE_INTEGER n{};need(GetFileSizeEx(h.value,&n)&&n.QuadPart>0&&n.QuadPart<65536,"result size");std::string b(size_t(n.QuadPart),'\0');DWORD read=0;
    need(ReadFile(h.value,b.data(),DWORD(b.size()),&read,nullptr)&&read==b.size(),"result bytes");return Json::parse(b);
}
static unsigned exceptionalJoins=0,exceptionalCleanupFailures=0;
static DWORD exceptionalExit=STILL_ACTIVE;
struct Child {
    si::Handle process,thread;
    DWORD pid=0,exit=STILL_ACTIVE;bool joined=false;
    ~Child() noexcept {
        if(!process.value)return;
        if(!joined){DWORD code=STILL_ACTIVE;
            if(WaitForSingleObject(process.value,20000)==WAIT_OBJECT_0&&GetExitCodeProcess(process.value,&code)){joined=true;exit=code;exceptionalExit=code;++exceptionalJoins;}
            else ++exceptionalCleanupFailures;
        }
        if(thread.value){const auto handle=thread.value;thread.value=nullptr;if(!CloseHandle(handle))++exceptionalCleanupFailures;}
        if(process.value){const auto handle=process.value;process.value=nullptr;if(!CloseHandle(handle))++exceptionalCleanupFailures;}
    }

    void start(const fs::path& exe,const std::vector<std::wstring>& args,const std::wstring& desktop=L""){
        std::wstring command=quote(exe.wstring());for(const auto& a:args)command+=L" "+quote(a);std::vector<wchar_t> buffer(command.begin(),command.end());buffer.push_back(0);
        STARTUPINFOW startup{sizeof(startup)};startup.lpDesktop=desktop.empty()?nullptr:const_cast<LPWSTR>(desktop.c_str());startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION p{};need(CreateProcessW(exe.c_str(),buffer.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,exe.parent_path().c_str(),&startup,&p)!=FALSE,"owned child launch");
        process.value=p.hProcess;thread.value=p.hThread;pid=p.dwProcessId;
    }
    bool done(){return WaitForSingleObject(process.value,0)==WAIT_OBJECT_0;}
    void join(DWORD ms){need(WaitForSingleObject(process.value,ms)==WAIT_OBJECT_0,"owned child did not return");need(GetExitCodeProcess(process.value,&exit)!=FALSE,"child exit receipt");joined=true;}
    void close(){need(joined,"child close before join");if(thread.value){need(CloseHandle(thread.value)!=FALSE,"child thread close");thread.value=nullptr;}if(process.value){need(CloseHandle(process.value)!=FALSE,"child process close");process.value=nullptr;}}
};
static void pump(){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
struct Desktop {
    HDESK original=GetThreadDesktop(GetCurrentThreadId()),owned=nullptr;std::wstring name;
    Desktop(){name=L"YMCC.ActionFixture."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());owned=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);need(owned&&SetThreadDesktop(owned),"private desktop");}
    ~Desktop(){if(owned){SetThreadDesktop(original);CloseDesktop(owned);}}
    std::wstring full()const{return L"winsta0\\"+name;}
};
struct Control {volatile LONG ready=0;DWORD pid=0;std::uint64_t birth=0,window=0;};
struct MappingControl {
    si::Handle mapping;si::View view;
    Control* get(){return static_cast<Control*>(view.value);}
    void create(const std::wstring& name){mapping.value=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Control),name.c_str());need(mapping.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"control create");view.value=MapViewOfFile(mapping.value,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Control));need(view.value,"control view");std::memset(view.value,0,sizeof(Control));}
    void open(const std::wstring& name){mapping.value=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,name.c_str());need(mapping.value,"control open");view.value=MapViewOfFile(mapping.value,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Control));need(view.value,"control map");}
};
static LRESULT CALLBACK victimProc(HWND w,UINT m,WPARAM p,LPARAM l){return DefWindowProcW(w,m,p,l);}
static HWND makeWindow(const wchar_t* name,WNDPROC proc,bool messageOnly=false){
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=name;RegisterClassW(&cls);
    HWND w=CreateWindowExW(0,name,L"inert action fixture",WS_POPUP,0,0,1,1,messageOnly?HWND_MESSAGE:nullptr,nullptr,cls.hInstance,nullptr);need(w,"owned hidden window");return w;
}
static int victim(const std::wstring& name){
    MappingControl control;control.open(name);si::Handle stop(OpenEventW(SYNCHRONIZE,FALSE,(name+L".Stop").c_str()));need(stop.value,"victim stop");
    HWND window=makeWindow(L"YMCC.ActionFixture.Victim",victimProc,true);si::Peer self;need(self.load(GetCurrentProcessId()),"victim actual peer");
    control.get()->pid=self.pid;control.get()->birth=self.birth;control.get()->window=reinterpret_cast<UINT_PTR>(window);MemoryBarrier();InterlockedExchange(&control.get()->ready,1);
    const auto until=GetTickCount64()+10000;while(GetTickCount64()<until&&WaitForSingleObject(stop.value,20)==WAIT_TIMEOUT)pump();
    const bool stopped=WaitForSingleObject(stop.value,0)==WAIT_OBJECT_0;DestroyWindow(window);need(stopped,"victim bounded stop");return 0;
}
static int attacker(const std::wstring& controlName,HWND provider,const fs::path& output){
    MappingControl control;control.open(controlName);auto& c=*control.get();MemoryBarrier();need(c.ready==1&&c.pid&&c.birth&&c.window,"victim actual identity");
    sa::Request r;r.callerPid=c.pid;r.callerBirth=c.birth;r.targetPid=GetCurrentProcessId();r.targetBirth=1;r.powerGeneration=7;r.powerSequence=5;r.powerSerial=2;r.deadline=GetTickCount64()+2500;
    need(BCryptGenRandom(nullptr,r.nonce.data(),DWORD(r.nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"attacker nonce");
    si::Handle proof(CreateNamedPipeW(sa::proofPipeName(r).c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,256,256,0,nullptr));need(proof.value&&proof.value!=INVALID_HANDLE_VALUE,"attacker owned pipe");
    sa::Mapping map;map.handle.value=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(sa::Wire),sa::mappingName(r).c_str());need(map.handle.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"attacker map");map.view.value=MapViewOfFile(map.handle.value,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(sa::Wire));need(map.view.value,"attacker map view");
    sa::Wire initial;initial.request=r;std::memcpy(map.view.value,&initial,sizeof(initial));si::Handle done(CreateEventW(nullptr,TRUE,FALSE,(sa::mappingName(r)+L".Done").c_str()));need(done.value,"attacker done");
    COPYDATASTRUCT data{sa::messageTag,sizeof(r),&r};DWORD_PTR ack=0;
    const auto sent=SendMessageTimeoutW(provider,WM_COPYDATA,WPARAM(c.window),reinterpret_cast<LPARAM>(&data),SMTO_ABORTIFHUNG|SMTO_BLOCK,1500,&ack);
    const auto* wire=static_cast<const sa::Wire*>(map.view.value);MemoryBarrier();
    writeNew(output,{{"actualCallerPid",GetCurrentProcessId()},{"claimedHostPid",c.pid},{"claimedHostBirth",std::to_string(c.birth)},{"sent",bool(sent)},{"ack",ack},{"accepted",LONG(wire->accepted)},{"productExecuted",false}});
    need(sent&&ack==0&&wire->accepted==0,"spoofed Host HWND reached action queue");return 0;
}
static bool ackTimeoutMode(const std::wstring& mode){return mode==L"ack-timeout-cancel"||mode==L"ack-timeout-submitted";}
static std::wstring ackEventName(DWORD pid,const wchar_t* suffix){return L"Local\\YMCC.ActionFixture.Ack."+std::to_wstring(pid)+suffix;}
static int client(const std::wstring& mode,DWORD target,std::uint64_t born,const fs::path& output){
#ifdef ACTION_NATIVE_COMPOSITION
    if(nativeCompositionMode(mode))return nativeCompositionClient(mode,target,born,output);
#endif
    si::Handle returned,drained;
    if(ackTimeoutMode(mode)){
        returned.value=OpenEventW(EVENT_MODIFY_STATE,FALSE,ackEventName(target,L".Returned").c_str());
        drained.value=OpenEventW(SYNCHRONIZE,FALSE,ackEventName(target,L".Drained").c_str());
        need(returned.value&&drained.value,"ack handshake open");
    }
    const auto began=GetTickCount64();const auto deadline=began+(mode==L"queued-expiry"?250:4500);
    const auto current=[&]{return mode!=L"queued-cancel"||GetTickCount64()-began<100;};
    auto result=sa::Outcome::Unknown;
    const auto repeats=mode==L"repeated-roundtrip"?20:1;
    for(int i=0;i<repeats;++i){result=sa::shutdown(target,born,mode==L"repeated-roundtrip"?GetTickCount64()+4500:deadline,7,5,2,current);
        if(mode!=L"repeated-roundtrip")break;
        need(result==sa::Outcome::Submitted,"repeated current request rejected");Sleep(5);}
    const auto returnedAt=GetTickCount64();bool drainedBeforeExit=false;
    if(ackTimeoutMode(mode)){
        need(SetEvent(returned.value)!=FALSE,"actual client return notification");
        drainedBeforeExit=WaitForSingleObject(drained.value,3000)==WAIT_OBJECT_0;
        need(drainedBeforeExit,"provider drain before client exit");
    }
    writeNew(output,{{"outcome",LONG(result)},{"pid",GetCurrentProcessId()},{"elapsedMs",GetTickCount64()-began},
        {"shutdownElapsedMs",returnedAt-began},{"sends",observedSends},{"sendSucceeded",observedSendSucceeded},
        {"sendError",observedSendError},{"sendTimeoutMs",observedSendTimeout},{"sendFlags",observedSendFlags},
        {"sendAck",observedSendAck},{"providerDrainedBeforeClientExit",drainedBeforeExit},{"productExecuted",false}});return 0;
}

static std::wstring providerMode;
static std::vector<std::function<void()>> pending;
static bool nativeCurrent=true;
static unsigned submissions=0,dispatches=0,claims=0,effects=0;
static ULONGLONG drainAt=0;
static HANDLE ackReturned=nullptr,ackDrained=nullptr; // borrowed from provider locals
static bool clientReturnedDuringHandler=false;
static DWORD clientReturnWait=WAIT_FAILED;
static LONG lateCancelled=-1,lateClaimed=-1,lateCompleted=-1;
static ULONGLONG ackHandlerWaitMs=0;
#ifdef ACTION_NATIVE_COMPOSITION
#include "native_composition_adapter.inc"
#endif
static LRESULT CALLBACK providerProc(HWND w,UINT m,WPARAM p,LPARAM l){
#ifdef ACTION_NATIVE_COMPOSITION
    if(m==WM_COPYDATA&&nativeCompositionMode(providerMode))return nativeCompositionServe(w,reinterpret_cast<HWND>(p),reinterpret_cast<const COPYDATASTRUCT*>(l));
#endif
    if(m==WM_COPYDATA){
        const auto response=sa::serve(w,reinterpret_cast<HWND>(p),reinterpret_cast<const COPYDATASTRUCT*>(l),
            [](std::function<void()> work){++submissions;if(providerMode==L"submit-throws")throw std::runtime_error("owned submit exception");if(providerMode==L"queue-reject")return false;pending.push_back(std::move(work));drainAt=GetTickCount64()+((providerMode==L"queued-cancel"||providerMode==L"queued-expiry"||providerMode==L"native-revoked")?350:0);return true;},
            [](const sa::Request&,const std::function<bool()>& current,const std::function<bool()>& claim){
                ++dispatches;if(providerMode==L"dispatch-throws")throw std::runtime_error("owned dispatcher exception");if(!current())return false;++claims;if(!claim())return false;
                if(providerMode==L"dispatch-false")return false;++effects;return true;
            },[]{return nativeCurrent;},[]{return fixtureSource().read();});
        if(ackTimeoutMode(providerMode)){
            const auto* data=reinterpret_cast<const COPYDATASTRUCT*>(l);
            need(response==sa::protocol&&data&&data->lpData&&data->cbData==sizeof(sa::Request),"ack request accepted");
            sa::Request request;std::memcpy(&request,data->lpData,sizeof(request));
            sa::Mapping observation;
            observation.handle.value=OpenFileMappingW(FILE_MAP_READ,FALSE,sa::mappingName(request).c_str());need(observation.handle.value,"read-only ack map");
            observation.view.value=MapViewOfFile(observation.handle.value,FILE_MAP_READ,0,0,sizeof(sa::Wire));need(observation.view.value,"read-only ack view");
            if(providerMode==L"ack-timeout-submitted"){
                auto work=std::move(pending);pending.clear();for(auto& task:work)task();
            }
            const auto before=GetTickCount64();clientReturnWait=WaitForSingleObject(ackReturned,3000);
            ackHandlerWaitMs=GetTickCount64()-before;clientReturnedDuringHandler=clientReturnWait==WAIT_OBJECT_0;
            const auto* wire=static_cast<const sa::Wire*>(observation.view.value);MemoryBarrier();
            lateCancelled=wire->cancelled;lateClaimed=wire->claimed;lateCompleted=wire->completed;
        }
        return response;
    }
    return DefWindowProcW(w,m,p,l);
}
static int provider(const std::wstring& mode,const fs::path& output){
    providerMode=mode;Desktop desktop;HWND window=makeWindow(L"YMCC.ActionFixture.Provider",providerProc);need(SetPropW(window,si::property,reinterpret_cast<HANDLE>(ULONG_PTR(si::protocol))),"provider marker");
    need(fixtureSource().claim(),"isolated Steam source slot");fixtureSource().resumeReads();
    HWND duplicate=nullptr;if(mode==L"ambiguous-provider"){duplicate=makeWindow(L"YMCC.ActionFixture.Provider",providerProc);need(SetPropW(duplicate,si::property,reinterpret_cast<HANDLE>(ULONG_PTR(si::protocol))),"duplicate marker");}
    si::Peer self;need(self.load(GetCurrentProcessId()),"provider actual peer");
    si::Handle returned,drained;
    if(ackTimeoutMode(mode)){
        returned.value=CreateEventW(nullptr,TRUE,FALSE,ackEventName(self.pid,L".Returned").c_str());need(returned.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"ack returned event");
        drained.value=CreateEventW(nullptr,TRUE,FALSE,ackEventName(self.pid,L".Drained").c_str());need(drained.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"ack drained event");
        ackReturned=returned.value;ackDrained=drained.value;
    }
    const auto root=fs::path(module()).parent_path(),clientFile=root/L"CustomSteamLibrary"/(mode==L"worker-image"?L"SteamArtworkLab.exe":L"CustomSteamLibrary.exe");
#ifdef ACTION_NATIVE_COMPOSITION
    ymcc::steamcommand::Coordinator coordinator;
    ymcc::steamcommand::Provider commandProvider(coordinator);
    need(bool(commandProvider),"isolated command coordinator");
    Child nativeTarget;si::Handle nativeStop;
    if(nativeCompositionMode(mode)){nativeCompositionBegin(root,desktop,nativeTarget,nativeStop);nativeObserveResourceBaseline();}
#endif
    const auto started=GetTickCount64();DWORD baseline=0;need(GetProcessHandleCount(GetCurrentProcess(),&baseline)!=FALSE,"actual process handle count failed");
    Child normal,victimChild,attackerChild;si::Handle stop;MappingControl control;std::wstring controlName;
    if(mode==L"forged-host-window"){
        controlName=L"Local\\YMCC.ActionFixture."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(started);control.create(controlName);stop.value=CreateEventW(nullptr,TRUE,FALSE,(controlName+L".Stop").c_str());need(stop.value,"victim stop create");
        victimChild.start(clientFile,{L"--victim",controlName},desktop.full());
        const auto ready=GetTickCount64()+3000;while(GetTickCount64()<ready&&!control.get()->ready){pump();Sleep(5);}need(control.get()->ready==1,"victim readiness");
        attackerChild.start(root/L"NotHost.exe",{L"--attacker",controlName,std::to_wstring(reinterpret_cast<UINT_PTR>(window)),(root/L"attacker.json").wstring()},desktop.full());
    }else{
        const auto file=mode==L"wrong-client-image"?root/L"NotHost.exe":clientFile;
        #ifdef ACTION_NATIVE_COMPOSITION
        const auto targetPid=nativeCompositionMode(mode)?nativeTarget.pid:self.pid;
        const auto targetBirth=nativeCompositionMode(mode)?nativeTargetBirth:self.birth;
#else
        const auto targetPid=self.pid;const auto targetBirth=self.birth;
#endif
        normal.start(file,{L"--client",mode,std::to_wstring(targetPid),std::to_wstring(targetBirth),(root/L"client.json").wstring()},desktop.full());
    }
    auto& active=mode==L"forged-host-window"?attackerChild:normal;
    DWORD warmedHandles=0,beforeJoinHandles=0;
    const auto until=GetTickCount64()+10000;
    while(GetTickCount64()<until&&(!active.done()||!pending.empty())){
        pump();if(!pending.empty()&&GetTickCount64()>=drainAt){
#ifdef ACTION_NATIVE_COMPOSITION
            if(nativeCompositionMode(mode))nativeCompositionBeforeDrain();
#endif
            if(mode==L"native-revoked")nativeCurrent=false;auto work=std::move(pending);pending.clear();for(auto& task:work)task();}
        if(ackTimeoutMode(mode)&&clientReturnedDuringHandler&&pending.empty())need(SetEvent(ackDrained)!=FALSE,"ack provider drain notification");
        if(mode==L"repeated-roundtrip"&&effects==1&&pending.empty()&&!warmedHandles)need(GetProcessHandleCount(GetCurrentProcess(),&warmedHandles)!=FALSE,"actual process handle count failed");
        Sleep(2);
    }
    need(GetProcessHandleCount(GetCurrentProcess(),&beforeJoinHandles)!=FALSE,"actual process handle count failed");
    active.join(1000);const auto childExit=active.exit,actualClient=active.pid;active.close();
    if(victimChild.pid){need(SetEvent(stop.value),"stop victim");victimChild.join(1000);need(victimChild.exit==0,"victim result");victimChild.close();}
    const auto result=readJson(root/(mode==L"forged-host-window"?L"attacker.json":L"client.json"));
#ifdef ACTION_NATIVE_COMPOSITION
    if(nativeCompositionMode(mode))nativeCompositionEnd(nativeTarget,nativeStop);
#endif
    DWORD after=0;need(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE,"actual process handle count failed");
    if(duplicate){RemovePropW(duplicate,si::property);DestroyWindow(duplicate);}RemovePropW(window,si::property);DestroyWindow(window);
    bool good=childExit==0&&pending.empty();
#ifdef ACTION_NATIVE_COMPOSITION
    if(nativeCompositionMode(mode)){
        const auto composition=nativeCompositionReceipt(result,actualClient,baseline,after);
        good=good&&composition.value("pass",false);
        writeNew(output,{{"pass",good},{"mode",si::utf8(mode)},{"providerPid",self.pid},{"actualChildPid",actualClient},
            {"composition",composition},{"childrenJoined",true},{"pendingEmpty",pending.empty()},
            {"handleCountBefore",baseline},{"handleCountAfter",after},{"productExecuted",false},{"privateDesktop",true}});
        return good?0:1;
    }
#endif
    if(mode==L"repeated-roundtrip")good=good&&result.value("outcome",-1)==LONG(sa::Outcome::Submitted)&&effects==20&&claims==20&&submissions==20&&dispatches==20&&observedQueries==20&&warmedHandles>0&&beforeJoinHandles==warmedHandles;
    else if(mode==L"normal")good=good&&result.value("outcome",-1)==LONG(sa::Outcome::Submitted)&&effects==1&&claims==1&&submissions==1&&observedQueries==1&&observedServer==actualClient;
    else if(mode==L"queued-cancel"||mode==L"queued-expiry")good=good&&result.value("outcome",-1)==LONG(sa::Outcome::Unknown)&&submissions==1&&dispatches==0&&effects==0;
    else if(mode==L"native-revoked")good=good&&result.value("outcome",-1)==LONG(sa::Outcome::Rejected)&&submissions==1&&dispatches==0&&effects==0;
    else if(ackTimeoutMode(mode))good=good&&clientReturnedDuringHandler&&clientReturnWait==WAIT_OBJECT_0&&lateCancelled==1&&
        result.value("outcome",-1)==LONG(sa::Outcome::Unknown)&&result.value("sends",0)==1&&!result.value("sendSucceeded",true)&&
        result.value("providerDrainedBeforeClientExit",false)&&result.value("sendTimeoutMs",0)>0&&result.value("sendTimeoutMs",0)<=1500&&
        submissions==1&&observedQueries==1&&observedServer==actualClient&&
        (mode==L"ack-timeout-submitted"?(lateClaimed==1&&lateCompleted==1&&dispatches==1&&claims==1&&effects==1):
            (lateClaimed==0&&lateCompleted==0&&dispatches==0&&claims==0&&effects==0));
    else if(mode==L"submit-throws")good=good&&result.value("outcome",-1)==LONG(sa::Outcome::Unknown)&&result.value("sends",0)==1&&result.value("sendSucceeded",false)&&result.value("sendAck",-1)==0&&submissions==1&&dispatches==0&&effects==0;
    else if(mode==L"queue-reject")good=good&&result.value("outcome",-1)==LONG(sa::Outcome::Rejected)&&submissions==1&&dispatches==0&&effects==0;
    else if(mode==L"dispatch-throws")good=good&&result.value("outcome",-1)==LONG(sa::Outcome::Unknown)&&dispatches==1&&effects==0;
    else if(mode==L"dispatch-false")good=good&&result.value("outcome",-1)==LONG(sa::Outcome::Failed)&&dispatches==1&&claims==1&&effects==0;
    else if(mode==L"ambiguous-provider"||mode==L"wrong-client-image"||mode==L"worker-image")good=good&&result.value("outcome",-1)==LONG(sa::Outcome::Rejected)&&submissions==0&&observedQueries==0&&effects==0;
    else if(mode==L"forged-host-window")good=good&&submissions==0&&effects==0&&observedQueries==1&&observedServer==actualClient&&observedServer!=control.get()->pid&&result.value("accepted",-1)==0;
    else good=false;
    ackReturned=nullptr;ackDrained=nullptr;
    writeNew(output,{{"clientReturnedDuringHandler",clientReturnedDuringHandler},{"clientReturnWait",clientReturnWait},{"ackHandlerWaitMs",ackHandlerWaitMs},{"lateCancelled",LONG(lateCancelled)},{"lateClaimed",LONG(lateClaimed)},{"lateCompleted",LONG(lateCompleted)},{"pass",good},{"mode",si::utf8(mode)},{"providerPid",self.pid},{"actualChildPid",actualClient},{"kernelServerPid",observedServer},{"kernelQueries",observedQueries},{"submissions",submissions},{"dispatches",dispatches},{"claims",claims},{"effects",effects},{"childrenJoined",true},{"pendingEmpty",pending.empty()},{"handleCountBefore",baseline},{"handleCountAfter",after},{"warmedHandles",warmedHandles},{"beforeJoinHandles",beforeJoinHandles},{"elapsedMs",GetTickCount64()-started},{"client",result},{"productExecuted",false},{"privateDesktop",true},{"desktopSwitchedVisible",false}});
    return good?0:1;
}
int wmain(int argc,wchar_t** argv){
    try{
#ifdef ACTION_NATIVE_COMPOSITION
        if(argc==3&&std::wstring(argv[1])==L"--native-target")return nativeTargetMain(argv[2]);
#endif
        if(argc==2&&std::wstring(argv[1])==L"--idle-child"){Sleep(100);return 0;}
        if(argc==3&&std::wstring(argv[1])==L"--victim")return victim(argv[2]);
        if(argc==5&&std::wstring(argv[1])==L"--attacker")return attacker(argv[2],reinterpret_cast<HWND>(std::stoull(argv[3])),argv[4]);
        if(argc==6&&std::wstring(argv[1])==L"--client")return client(argv[2],DWORD(std::stoul(argv[3])),std::stoull(argv[4]),argv[5]);
        if(argc==4&&std::wstring(argv[1])==L"--provider")return provider(argv[2],argv[3]);
#ifdef ACTION_NATIVE_COMPOSITION
        const std::vector<std::wstring> modes={L"native-compose-identity-queue-process",L"native-compose-identity-queue-account",L"native-compose-identity-lookup-epoch",L"native-compose-identity-after-claim",L"native-compose-identity-publication",L"native-compose-identity-unknown",L"native-compose-identity-post-submit",L"native-compose-current",L"native-compose-queue-pending",L"native-compose-queue-aba",L"native-compose-publication",L"native-compose-after-claim-gate",L"native-compose-target-exited",L"native-compose-shell-reject",L"native-compose-expected-power-changed",L"native-compose-presence-absence",L"native-compose-presence-replacement-preflight",L"native-compose-presence-replacement-after",L"native-compose-presence-stale-after",L"native-compose-presence-incomplete-after",L"native-compose-presence-birth-mismatch",L"native-compose-source-queue-rotate",L"native-compose-source-info-revoke"};
        const std::wstring scope=L"--native-composition";
#else
        const std::vector<std::wstring> modes={L"normal",L"exception-child-return",L"repeated-roundtrip",L"queued-cancel",L"queued-expiry",L"native-revoked",L"queue-reject",L"dispatch-throws",L"dispatch-false",L"ambiguous-provider",L"wrong-client-image",L"worker-image",L"forged-host-window",L"ack-timeout-cancel",L"ack-timeout-submitted",L"submit-throws"};
        const std::wstring scope=L"--cross-process";
#endif
        const auto reject=[]{std::cout<<Json{{"status","SELECTION_REJECTED"},{"casesStarted",0},{"checks",0},{"productExecuted",false}}.dump()<<std::endl;return 2;};
        if(argc<3||argc>4||std::wstring(argv[1])!=scope)return reject();
        const fs::path directory=fs::absolute(argv[2]);const std::wstring selected=argc==4?argv[3]:L"";
        if(argc==4&&(selected.empty()||std::find(modes.begin(),modes.end(),selected)==modes.end()))return reject();
        need(directory.parent_path()==fs::path(module()).parent_path().parent_path(),"fixture output outside own run");need(!fs::exists(directory)&&fs::create_directory(directory),"new private fixture root");
        unsigned started=0,passed=0;Json cases=Json::array();
        for(const auto& mode:modes){if(argc==4&&mode!=selected)continue;++started;const auto root=directory/mode;fs::create_directories(root/L"CustomSteamLibrary");
            for(const auto& relative:{fs::path(L"YeManCC.exe"),fs::path(L"NotHost.exe"),fs::path(L"CustomSteamLibrary/CustomSteamLibrary.exe"),fs::path(L"CustomSteamLibrary/SteamArtworkLab.exe")})need(fs::copy_file(module(),root/relative,fs::copy_options::none),"copy inert PE");
            if(mode==L"exception-child-return"){
                Child warm;warm.start(root/L"NotHost.exe",{L"--idle-child"});const auto warmPid=warm.pid;warm.join(2000);need(warm.exit==0,"warm child return");warm.close();
                DWORD oldHandles=0,newHandles=0;need(GetProcessHandleCount(GetCurrentProcess(),&oldHandles)!=FALSE,"actual process handle count failed");
                DWORD childPid=0;bool unwound=false;
                try{Child owned;owned.start(root/L"NotHost.exe",{L"--idle-child"});childPid=owned.pid;throw std::runtime_error("owned fixture control");}
                catch(const std::exception&){unwound=true;}
                need(GetProcessHandleCount(GetCurrentProcess(),&newHandles)!=FALSE,"actual process handle count failed");
                const bool good=unwound&&childPid&&exceptionalJoins==1&&!exceptionalCleanupFailures&&exceptionalExit==0&&oldHandles==newHandles;
                std::cout<<Json{{"kind","observation"},{"case","exception-child-return"},{"actualChildPid",childPid},{"warmChildPid",warmPid},{"warmChildJoined",true},{"exceptionalJoins",exceptionalJoins},{"actualChildExitCode",exceptionalExit},{"handleCountApiSucceeded",true},{"cleanupFailures",exceptionalCleanupFailures},{"handlesBefore",oldHandles},{"handlesAfter",newHandles},{"productExecuted",false}}.dump()<<std::endl;
                std::cout<<Json{{"kind","case"},{"case","exception-child-return"},{"status",good?"PASS":"FAIL"},{"checks",1}}.dump()<<std::endl;
                if(good)++passed;else break;continue;
            }
            Child child;child.start(root/L"YeManCC.exe",{L"--provider",mode,(root/L"provider.json").wstring()});child.join(15000);const auto exit=child.exit;child.close();const auto row=readJson(root/L"provider.json");const auto name=si::utf8(mode);
            std::cout<<Json{{"kind","observation"},{"case",name},{"result",row},{"productExecuted",false}}.dump()<<std::endl;
            const bool pass=exit==0&&row.value("pass",false);if(pass)++passed;cases.push_back({{"name",name},{"pass",pass}});
            std::cout<<Json{{"kind","case"},{"case",name},{"status",pass?"PASS":"FAIL"},{"checks",1}}.dump()<<std::endl;
            if(!pass)break;
        }
        std::cout<<Json{{"status",passed==started?"PASS":"FAIL"},{"casesStarted",started},{"casesPassed",passed},{"checks",started},{"defaultExecuted",false},{"productExecuted",false}}.dump()<<std::endl;return passed==started?0:1;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 3;}
}
