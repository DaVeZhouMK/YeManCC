#pragma once
// The installed library Host can request only an explicit, fixed Steam exit.
// Read-only Steam information remains a separate protocol and grants no action.
#include "steam_information_client.h"
#include <functional>
#include <memory>

namespace ymcc::steamaction {
inline constexpr ULONG_PTR messageTag=0x594d5341;
inline constexpr std::uint32_t protocol=1;
inline constexpr std::uint32_t homeProtocol=2;
inline constexpr ULONGLONG requestBudgetMs=5000;
enum class Command : std::uint32_t { Shutdown=1, HomeStart=2, HomeShow=3, HomeFocus=4 };
enum class Outcome : LONG { Unknown=0, Submitted=1, Rejected=2, Failed=3, Busy=4 };
struct Request {
    std::uint32_t version=protocol,size=sizeof(Request),command=static_cast<std::uint32_t>(Command::Shutdown),callerPid=0;
    std::uint64_t callerBirth=0;
    std::uint32_t targetPid=0,reserved=0;
    std::uint64_t targetBirth=0,deadline=0,powerGeneration=0,powerSequence=0,powerSerial=0;
    std::array<unsigned char,16> nonce{};
};
static_assert(sizeof(Request)==88);
struct Wire {
    Request request;
    volatile LONG accepted=0,claimed=0,cancelled=0,completed=0;
    LONG outcome=static_cast<LONG>(Outcome::Unknown);
    DWORD providerPid=0;
    std::uint64_t providerBirth=0;
};
static_assert(sizeof(Wire)==120);
inline bool matches(const Request& a,const Request& b)noexcept{
    return a.version==b.version&&a.size==b.size&&a.command==b.command&&a.callerPid==b.callerPid&&
        a.callerBirth==b.callerBirth&&a.targetPid==b.targetPid&&a.reserved==b.reserved&&
        a.targetBirth==b.targetBirth&&a.deadline==b.deadline&&a.powerGeneration==b.powerGeneration&&
        a.powerSequence==b.powerSequence&&a.powerSerial==b.powerSerial&&a.nonce==b.nonce;
}
inline bool valid(const Request& r,ULONGLONG now)noexcept{
    const bool legacy=r.version==protocol&&r.command==static_cast<std::uint32_t>(Command::Shutdown)&&r.targetPid&&r.targetBirth;
    const bool home=r.version==homeProtocol&&
        ((r.command==static_cast<std::uint32_t>(Command::HomeStart)&&!r.targetPid&&!r.targetBirth)||
         ((r.command==static_cast<std::uint32_t>(Command::HomeShow)||r.command==static_cast<std::uint32_t>(Command::HomeFocus))&&r.targetPid&&r.targetBirth));
    return (legacy||home)&&r.size==sizeof(Request)&&
        r.callerPid&&r.callerBirth&&r.powerGeneration&&!r.reserved&&r.deadline>now&&
        r.deadline-now<=requestBudgetMs&&!std::all_of(r.nonce.begin(),r.nonce.end(),[](auto n){return n==0;});
}
inline std::wstring mappingName(const Request& r){
    static constexpr wchar_t hex[]=L"0123456789abcdef";
    std::wstring name=L"Local\\YeManCC.SteamAction.v1."+std::to_wstring(r.callerPid)+L".";
    for(const auto b:r.nonce){name+=hex[b>>4];name+=hex[b&15];}return name;
}
inline std::wstring proofPipeName(const Request& r){
    return L"\\\\.\\pipe\\"+mappingName(r).substr(6);
}
struct Mapping {steaminfo::Handle handle;steaminfo::View view;};
inline bool ownerCurrent(const std::function<bool()>& current)noexcept{
    try{return current&&current();}catch(...){return false;}
}
inline UINT homeCurrentMessage()noexcept{
    static const UINT value=RegisterWindowMessageW(L"YeManCC.SteamAction.Home.Current.v2");return value;
}
inline constexpr LRESULT homeCurrentAck=0x594d4832;
inline std::uint64_t homeCurrentNonce(const Request& request)noexcept{
    std::uint64_t value=0;std::memcpy(&value,request.nonce.data(),sizeof(value));return value;
}
struct HomeCurrentProbe {
    const std::function<bool()>* current=nullptr;
    const steaminfo::Peer* provider=nullptr;
    Wire* wire=nullptr;
    std::uint64_t nonce=0;
    WNDPROC previous=nullptr;
};
inline LRESULT CALLBACK homeCurrentWindow(HWND window,UINT message,WPARAM providerWindow,LPARAM nonce){
    const auto query=homeCurrentMessage();
    const auto* state=reinterpret_cast<const HomeCurrentProbe*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(query&&message==query){
        DWORD pid=0;
        if(!state||!state->current||!state->provider||!state->wire||
            static_cast<std::uint64_t>(static_cast<std::uintptr_t>(nonce))!=state->nonce||
            !GetWindowThreadProcessId(reinterpret_cast<HWND>(providerWindow),&pid)||pid!=state->provider->pid||
            !state->provider->live())return 0;
        if(!ownerCurrent(*state->current)){
            InterlockedExchange(&state->wire->cancelled,1);return 0;
        }
        return state->wire->accepted==2&&!state->wire->cancelled?homeCurrentAck:0;
    }
    return state&&state->previous?CallWindowProcW(state->previous,window,message,providerWindow,nonce):
        DefWindowProcW(window,message,providerWindow,nonce);
}
// Exactly one send. Unknown means it may have been submitted; never replay it.
inline Outcome submit(Request request,const std::function<bool()>& current,
        DWORD expectedProviderPid=0,std::uint64_t expectedProviderBirth=0)noexcept{
    try{
        if(!ownerCurrent(current))return Outcome::Rejected;
        const auto deadline=request.deadline;
        steaminfo::Peer caller;if(!caller.load(GetCurrentProcessId()))return Outcome::Rejected;
        const auto own=std::filesystem::path(caller.path);
        const bool home=request.version==homeProtocol;
        const auto leaf=steaminfo::pathKey(own.filename().wstring());
        if(home){if(leaf!=L"yemangaminghome.exe"||!steaminfo::gamingHomePackagePeer(caller))return Outcome::Rejected;}
        else if(request.version!=protocol||leaf!=L"customsteamlibrary.exe")return Outcome::Rejected;
        const auto expected=home?std::wstring(LR"(c:\soft\yeman\yemancc\yemancc.exe)"):
            steaminfo::pathKey((own.parent_path().parent_path()/L"YeManCC.exe").wstring());
        const auto endpoint=steaminfo::findProvider(caller,expected);
        if(!endpoint.peer)return Outcome::Rejected;
        const auto& provider=*endpoint.peer;
        if((expectedProviderPid&&provider.pid!=expectedProviderPid)||
            (expectedProviderBirth&&provider.birth!=expectedProviderBirth))return Outcome::Rejected;
        request.callerPid=caller.pid;request.callerBirth=caller.birth;
        if(BCryptGenRandom(nullptr,request.nonce.data(),static_cast<ULONG>(request.nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0||!valid(request,GetTickCount64()))return Outcome::Rejected;
        const auto name=mappingName(request);
        // Kernel-owned local server identity proves the real requesting process.
        // No I/O is started: there is no borrowed OVERLAPPED or pending buffer.
        steaminfo::Handle proof(CreateNamedPipeW(proofPipeName(request).c_str(),
            PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,256,256,0,nullptr));
        if(proof.value==INVALID_HANDLE_VALUE||!proof.value)return Outcome::Rejected;
        SetLastError(ERROR_SUCCESS);
        Mapping mapped;mapped.handle.value=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Wire),name.c_str());
        if(!mapped.handle.value||GetLastError()==ERROR_ALREADY_EXISTS)return Outcome::Rejected;
        mapped.view.value=MapViewOfFile(mapped.handle.value,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Wire));if(!mapped.view.value)return Outcome::Rejected;
        auto* wire=static_cast<Wire*>(mapped.view.value);Wire initial;initial.request=request;std::memcpy(wire,&initial,sizeof(initial));
        steaminfo::Handle ready;
        if(home){
            SetLastError(ERROR_SUCCESS);ready.value=CreateEventW(nullptr,FALSE,FALSE,(name+L".Ready").c_str());
            if(!ready.value||GetLastError()==ERROR_ALREADY_EXISTS)return Outcome::Rejected;
        }
        struct CancelOnReturn {Wire* wire;HANDLE ready;~CancelOnReturn(){
            InterlockedExchange(&wire->cancelled,1);if(ready)SetEvent(ready);
        }} cancelOnReturn{wire,ready.value};
        SetLastError(ERROR_SUCCESS);steaminfo::Handle done(CreateEventW(nullptr,TRUE,FALSE,(name+L".Done").c_str()));
        if(!done.value||GetLastError()==ERROR_ALREADY_EXISTS)return Outcome::Rejected;
        HomeCurrentProbe probe{&current,&provider,wire,homeCurrentNonce(request)};
        HWND sender=CreateWindowExW(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!sender)return Outcome::Rejected;
        struct Window {HWND value;~Window(){DestroyWindow(value);}} closeWindow{sender};
        if(home){
            if(!homeCurrentMessage())return Outcome::Rejected;
            SetLastError(ERROR_SUCCESS);
            if(!SetWindowLongPtrW(sender,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&probe))&&GetLastError()!=ERROR_SUCCESS)return Outcome::Rejected;
            const auto previous=SetWindowLongPtrW(sender,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(homeCurrentWindow));
            if(!previous)return Outcome::Rejected;
            probe.previous=reinterpret_cast<WNDPROC>(previous);
        }
        const auto stillCurrent=[&]{return ownerCurrent(current)&&caller.live()&&provider.live()&&GetTickCount64()<deadline;};
        if(!stillCurrent())return Outcome::Rejected;
        if(home&&request.command==static_cast<std::uint32_t>(Command::HomeFocus)){
            if(!stillCurrent())return Outcome::Rejected;
            (void)AllowSetForegroundWindow(provider.pid);
        }
        COPYDATASTRUCT data{messageTag,sizeof(request),&request};DWORD_PTR acknowledged=0;
        // SMTO timeout never authorizes another send. Cancel the shared request
        // even if the receiver is still inspecting or queued behind other work.
        if(!endpoint.current())return Outcome::Rejected;
        const auto beforeSend=GetTickCount64();
        const auto timeout=beforeSend<deadline?static_cast<UINT>((std::min)(deadline-beforeSend,ULONGLONG{1500})):0;
        const auto sent=timeout&&SendMessageTimeoutW(endpoint.window,WM_COPYDATA,reinterpret_cast<WPARAM>(sender),reinterpret_cast<LPARAM>(&data),SMTO_ABORTIFHUNG|SMTO_BLOCK,timeout,&acknowledged);
        if(!sent||acknowledged!=protocol){InterlockedExchange(&wire->cancelled,1);return Outcome::Unknown;}
        if(home){
            // No Home effect can run while this caller is blocked in Send.
            if(!stillCurrent())return Outcome::Unknown;
            if(InterlockedCompareExchange(&wire->accepted,2,1)!=1||!SetEvent(ready.value))return Outcome::Unknown;
        }
        for(;;){
            if(!stillCurrent()){InterlockedExchange(&wire->cancelled,1);return Outcome::Unknown;}
            const auto now=GetTickCount64();if(now>=deadline){InterlockedExchange(&wire->cancelled,1);return Outcome::Unknown;}
            const auto remaining=static_cast<DWORD>((std::min)(deadline-now,ULONGLONG{100}));
            const auto wait=home?MsgWaitForMultipleObjectsEx(1,&done.value,remaining,QS_SENDMESSAGE,MWMO_INPUTAVAILABLE):
                WaitForSingleObject(done.value,remaining);
            // ownerCurrent's targeted PeekMessage handles the read-only probe
            // and activation cancellation. Unrelated queued messages stay put.
            if(wait==WAIT_TIMEOUT||(home&&wait==WAIT_OBJECT_0+1))continue;
            if(wait!=WAIT_OBJECT_0){InterlockedExchange(&wire->cancelled,1);return Outcome::Unknown;}
            MemoryBarrier();
            if(!stillCurrent()||!endpoint.current()||!matches(wire->request,request)||wire->completed!=1||
                wire->providerPid!=provider.pid||wire->providerBirth!=provider.birth)return Outcome::Unknown;
            const auto result=static_cast<Outcome>(wire->outcome);
            return result==Outcome::Submitted||result==Outcome::Rejected||result==Outcome::Failed||result==Outcome::Busy?result:Outcome::Unknown;
        }
    }catch(...){return Outcome::Unknown;}
}
// Existing library API and v1 bytes retain their original role and meaning.
inline Outcome shutdown(DWORD targetPid,std::uint64_t targetBirth,ULONGLONG deadline,
        std::uint64_t powerGeneration,std::uint64_t powerSequence,std::uint64_t powerSerial,
        const std::function<bool()>& current)noexcept{
    if(!targetPid||!targetBirth)return Outcome::Rejected;
    Request request;request.targetPid=targetPid;request.targetBirth=targetBirth;request.deadline=deadline;
    request.powerGeneration=powerGeneration;request.powerSequence=powerSequence;request.powerSerial=powerSerial;
    return submit(request,current);
}
} // namespace ymcc::steamaction
