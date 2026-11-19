#pragma once
// Native action ingress. The installed Host has no shell/SDK dispatch authority.
#include "steam_client_action_channel.h"
#include "steam_command_disposition.h"
#include "steam_client_peer_ownership.h"
#include "steam_source_owner.h"
#include <functional>
#include <memory>

namespace ymcc::steamaction {
struct Pending final {
    steaminfo::Peer provider,caller;
    steaminfo::Ancestry ancestry;
    Mapping mapped;
    steaminfo::Handle done,proof,ready;
    HWND providerWindow=nullptr,senderWindow=nullptr;
    const Request request;
    explicit Pending(Request value):request(value){}
    Wire* wire()const noexcept{return static_cast<Wire*>(mapped.view.value);}
    bool envelopeCurrent()const noexcept{
        const auto* w=wire();const auto now=GetTickCount64();
        return w&&now<request.deadline&&matches(w->request,request)&&
            (w->accepted==1||(request.version==homeProtocol&&w->accepted==2))&&!w->cancelled&&!w->completed&&
            provider.live()&&caller.live()&&ancestry.live();
    }
    bool current()const noexcept{
        if(!envelopeCurrent())return false;
        if(request.version!=homeProtocol)return true;
        if(wire()->accepted!=2||!homeCurrentMessage())return false;
        DWORD pid=0;if(!GetWindowThreadProcessId(senderWindow,&pid)||pid!=caller.pid)return false;
        const auto now=GetTickCount64();if(now>=request.deadline)return false;
        DWORD_PTR answer=0;
        const auto sent=SendMessageTimeoutW(senderWindow,homeCurrentMessage(),reinterpret_cast<WPARAM>(providerWindow),
            static_cast<LPARAM>(homeCurrentNonce(request)),SMTO_ABORTIFHUNG|SMTO_BLOCK,
            static_cast<UINT>((std::min)(request.deadline-now,ULONGLONG{1500})),&answer);
        return sent&&answer==static_cast<DWORD_PTR>(homeCurrentAck)&&envelopeCurrent()&&wire()->accepted==2;
    }
    bool claim()noexcept{
        return current()&&InterlockedCompareExchange(&wire()->claimed,1,0)==0&&current();
    }
    void finish(Outcome result)noexcept{
        auto* w=wire();if(!w)return;
        w->outcome=static_cast<LONG>(result);w->providerPid=provider.pid;w->providerBirth=provider.birth;
        MemoryBarrier();InterlockedExchange(&w->completed,1);if(done.value)SetEvent(done.value);
    }
};
using AcquireRead=std::function<steamsession::SourceOwner::Read()>;
using Submit=std::function<bool(std::function<void()>)>;
using Dispatch=std::function<bool(const Request&,const std::function<bool()>&,const std::function<bool()>&)>;
using TypedDispatch=std::function<steamcommand::DispatchResult(const Request&,const std::function<bool()>&,const std::function<bool()>&)>;
using BindHomeRuntime=std::function<std::function<bool()>(std::shared_ptr<const steaminfo::Peer>)>;
inline LRESULT serveTyped(HWND window,HWND sender,const COPYDATASTRUCT* data,const Submit& submit,
        const TypedDispatch& dispatch,const std::function<bool()>& nativeCurrent,const AcquireRead& acquireRead,
        const BindHomeRuntime& bindHome={})noexcept{
    try{
        if(!submit||!dispatch||!ownerCurrent(nativeCurrent)||!data||data->dwData!=messageTag||
            data->cbData!=sizeof(Request)||!data->lpData||!sender)return FALSE;
        if(!acquireRead)return FALSE;
        const auto ingressRead=acquireRead();if(!ingressRead||!ingressRead.current())return FALSE;
        const auto source=ingressRead.view();
        std::function<bool()> sourceCurrent=[source,nativeCurrent]{return source.current()&&ownerCurrent(nativeCurrent);};
        Request request;std::memcpy(&request,data->lpData,sizeof(request));if(!valid(request,GetTickCount64()))return FALSE;
        DWORD providerPid=0,callerPid=0;GetWindowThreadProcessId(window,&providerPid);GetWindowThreadProcessId(sender,&callerPid);
        if(providerPid!=GetCurrentProcessId()||callerPid!=request.callerPid)return FALSE;
        auto pending=std::make_shared<Pending>(request);
        pending->providerWindow=window;pending->senderWindow=sender;
        const bool home=request.version==homeProtocol;
        if(bindHome&&!home)return FALSE;
        if(!pending->provider.load(providerPid)||!pending->caller.load(callerPid)||pending->caller.birth!=request.callerBirth)return FALSE;
        if(home){
            if(!pending->provider.sameUser(pending->caller)||!steaminfo::gamingHomePackagePeer(pending->caller))return FALSE;
        }else if(!steaminfo::authorizeLibraryCaller(pending->provider,pending->caller,pending->ancestry,
            steaminfo::LibraryCaller::HostOnly))return FALSE;
        pending->proof.value=CreateFileW(proofPipeName(request).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,
            OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
        ULONG actualServer=0;
        if(!pending->proof.value||pending->proof.value==INVALID_HANDLE_VALUE||
            !GetNamedPipeServerProcessId(pending->proof.value,&actualServer)||actualServer!=pending->caller.pid||
            !pending->caller.live()||!ownerCurrent(sourceCurrent))return FALSE;
        const auto name=mappingName(request);
        pending->mapped.handle.value=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,name.c_str());if(!pending->mapped.handle.value)return FALSE;
        pending->mapped.view.value=MapViewOfFile(pending->mapped.handle.value,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Wire));if(!pending->mapped.view.value)return FALSE;
        pending->done.value=OpenEventW(EVENT_MODIFY_STATE,FALSE,(name+L".Done").c_str());if(!pending->done.value)return FALSE;
        if(home){pending->ready.value=OpenEventW(SYNCHRONIZE,FALSE,(name+L".Ready").c_str());if(!pending->ready.value)return FALSE;}
        auto* wire=pending->wire();
        const auto pristine=[&]{return matches(wire->request,request)&&!wire->claimed&&!wire->cancelled&&!wire->completed&&
            !wire->outcome&&!wire->providerPid&&!wire->providerBirth;};
        if(!pristine())return FALSE;
        if(bindHome){
            if(wire->accepted||!valid(request,GetTickCount64())||!ownerCurrent(sourceCurrent))return FALSE;
            // The queued closure owns this alias; Pending never owns the closure.
            auto runtime=bindHome(std::shared_ptr<const steaminfo::Peer>(pending,&pending->caller));
            if(!runtime)return FALSE;
            sourceCurrent=[source,nativeCurrent,runtime=std::move(runtime)]{
                return source.current()&&ownerCurrent(nativeCurrent)&&ownerCurrent(runtime);
            };
            if(!ownerCurrent(sourceCurrent)||!valid(request,GetTickCount64())||!pristine())return FALSE;
        }
        if(InterlockedCompareExchange(&wire->accepted,1,0)!=0)return FALSE;
        if(!pending->envelopeCurrent()||!ownerCurrent(sourceCurrent)){pending->finish(Outcome::Rejected);return protocol;}
        if(!submit([pending,dispatch,sourceCurrent,acquireRead]{
            if(!ownerCurrent(sourceCurrent)){pending->finish(Outcome::Rejected);return;}
            // A queued request owns only its revocation cell. An actual dispatch
            // acquires one read scope, so final shutdown drains in-flight work
            // without waiting for an unexecuted queue to release a reader.
            if(pending->request.version==homeProtocol){
                const auto now=GetTickCount64();
                if(!pending->envelopeCurrent()||!ownerCurrent(sourceCurrent)||now>=pending->request.deadline||
                    WaitForSingleObject(pending->ready.value,static_cast<DWORD>(pending->request.deadline-now))!=WAIT_OBJECT_0){
                    pending->finish(Outcome::Rejected);return;
                }
            }
            // Peer readiness is not an active Steam action. Retain only the
            // revocation view until it returns, then acquire the counted scope.
            if(!ownerCurrent(sourceCurrent)){pending->finish(Outcome::Rejected);return;}
            const auto dispatchRead=acquireRead();
            if(!dispatchRead||!dispatchRead.current()){pending->finish(Outcome::Rejected);return;}
            const auto current=[pending,sourceCurrent,&dispatchRead]{return dispatchRead.current()&&ownerCurrent(sourceCurrent)&&pending->current();};
            if(!current()){pending->finish(Outcome::Rejected);return;}
            steamcommand::DispatchResult result;
            try{result=dispatch(pending->request,current,[&current,pending]{return current()&&pending->claim()&&current();});}
            catch(...){pending->finish(Outcome::Unknown);return;}
            // Only this dispatch result can report submission, even after revocation.
            if(result.submitted()){pending->finish(Outcome::Submitted);return;}
            switch(result.disposition){
                case steamcommand::Disposition::Busy:pending->finish(Outcome::Busy);break;
                case steamcommand::Disposition::Unavailable:
                case steamcommand::Disposition::Revoked:pending->finish(Outcome::Rejected);break;
                case steamcommand::Disposition::Failed:pending->finish(Outcome::Failed);break;
                default:pending->finish(Outcome::Unknown);break;
            }
        }))pending->finish(Outcome::Rejected);
        return protocol;
    }catch(...){return FALSE;}
}
// Preserve the original bool dispatch contract for existing callers.
inline LRESULT serve(HWND window,HWND sender,const COPYDATASTRUCT* data,const Submit& submit,
        const Dispatch& dispatch,const std::function<bool()>& nativeCurrent,const AcquireRead& acquireRead)noexcept{
    try{
        if(!dispatch)return FALSE;
        const TypedDispatch typed=[dispatch](const Request& request,const std::function<bool()>& current,const std::function<bool()>& claim){
            if(dispatch(request,current,claim))return steamcommand::DispatchResult{steamcommand::Disposition::Submitted};
            return steamcommand::DispatchResult{current()?steamcommand::Disposition::Failed:steamcommand::Disposition::Revoked};
        };
        return serveTyped(window,sender,data,submit,typed,nativeCurrent,acquireRead);
    }catch(...){return FALSE;}
}
} // namespace ymcc::steamaction
