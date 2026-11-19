#pragma once
#include "sleep_power_remote_transport.h"
#include <new>
namespace ymcc::sleeppower::remote {
// Synchronous finite client of the ONE Native transport. No state producer,
// cancellation mirror thread, retry timer, action or resource-return receipt.
// The caller authenticates its original creator/application pair separately.
struct NativeObservationImportDiagnostic {const char* stage="shape";DWORD error=0;};
class NativeObservationClient final {
    struct State {
        RemoteOfferV1 offer;HANDLE self=nullptr,parent=nullptr,cancel=nullptr,pipe=nullptr,io=nullptr,metadata=nullptr;void* metadataView=nullptr;
        std::mutex gate;bool retired=false;const char* stage="initial";State* next=nullptr;
    };
    State* state_=nullptr;
    inline static std::mutex retainedMutex_;
    inline static State* retained_=nullptr;
    static bool close(HANDLE& h)noexcept{if(!h||h==INVALID_HANDLE_VALUE){h=nullptr;return true;}if(!CloseHandle(h))return false;h=nullptr;return true;}
    static bool closeTransient(State& s)noexcept{const bool a=close(s.pipe),b=close(s.io);return a&&b;}
    static bool closeOwned(State& s)noexcept{if(s.metadataView){if(!UnmapViewOfFile(s.metadataView))return false;s.metadataView=nullptr;}const bool a=closeTransient(s),b=close(s.cancel),c=close(s.parent),d=close(s.self),e=close(s.metadata);return a&&b&&c&&d&&e;}
    static void retain(State* s)noexcept{std::lock_guard lock(retainedMutex_);s->next=retained_;retained_=s;}
    static void release(State* s)noexcept{if(!s)return;s->retired=true;if(closeOwned(*s))delete s;else retain(s);}
    static bool sameDescriptor(const DescriptorV1& a,const DescriptorV1& b)noexcept{return std::memcmp(&a,&b,sizeof a)==0;}
    static bool shape(const RemoteOfferV1& o)noexcept{
        const auto& b=o.binding;
        return validTransportStem(o.pipeName)&&b.size==sizeof b&&b.version==1&&!b.reserved&&
            b.purpose==Purpose::Observation&&nonzeroKey(b.scope)&&nonzeroKey(b.request)&&
            b.provider.pid&&b.provider.birth100ns&&b.consumer.pid&&b.consumer.birth100ns&&powerAdmitted(b.captured);
    }
    static bool localCurrent(const State& s)noexcept{
        ProcessIdentityV1 p,c;
        return !s.retired&&s.parent&&s.cancel&&WaitForSingleObject(s.parent,0)==WAIT_TIMEOUT&&
            WaitForSingleObject(s.cancel,0)==WAIT_TIMEOUT&&processIdentity(s.parent,p)&&
            processIdentity(s.self,c)&&sameProcess(p,s.offer.binding.provider)&&sameProcess(c,s.offer.binding.consumer);
    }
    static bool transfer(State& s,void* buffer,DWORD bytes,bool write,ULONGLONG deadline)noexcept{
        if(!ResetEvent(s.io))return false;OVERLAPPED operation{};operation.hEvent=s.io;DWORD count=0;
        const auto started=write?WriteFile(s.pipe,buffer,bytes,&count,&operation):ReadFile(s.pipe,buffer,bytes,&count,&operation);
        const auto error=started?ERROR_SUCCESS:GetLastError();
        if(!started&&error!=ERROR_IO_PENDING)return false;
        if(!started){const auto tick=GetTickCount64();HANDLE wait[]={s.parent,s.cancel,s.io};
            const auto result=tick<deadline?WaitForMultipleObjects(3,wait,FALSE,static_cast<DWORD>(deadline-tick)):WAIT_TIMEOUT;
            if(result!=WAIT_OBJECT_0+2){
                CancelIoEx(s.pipe,&operation);DWORD ignored=0;
                // The exact buffer/OVERLAPPED stay alive through real kernel
                // completion even on timeout/cancellation. No detached debt.
                (void)GetOverlappedResult(s.pipe,&operation,&ignored,TRUE);return false;
            }
        }
        return GetOverlappedResult(s.pipe,&operation,&count,FALSE)&&count==bytes;
    }
    static bool exchange(State& s,WireCommand command,WireResponseV1& output)noexcept{
        output={};s.stage="local-current";if(!localCurrent(s)||s.pipe||s.io)return false;
        const auto deadline=GetTickCount64()+1000;
        try{
            s.stage="connect";const auto name=L"\\\\.\\pipe\\"+s.offer.pipeName;
            for(unsigned attempt=0;attempt<3;++attempt){const auto tick=GetTickCount64();
                if(tick>=deadline||!localCurrent(s)||!WaitNamedPipeW(name.c_str(),static_cast<DWORD>(deadline-tick)))break;
                s.pipe=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_ANONYMOUS,nullptr);
                if(s.pipe!=INVALID_HANDLE_VALUE)break;const auto error=GetLastError();s.pipe=nullptr;
                if(error!=ERROR_PIPE_BUSY)break; // Connect acquisition only; no sent request replay.
            }
            if(!s.pipe)return false;
            const auto serverCurrent=[&]{ULONG pid=0;ProcessIdentityV1 p;return GetNamedPipeServerProcessId(s.pipe,&pid)&&
                pid==s.offer.binding.provider.pid&&processIdentity(s.parent,p)&&sameProcess(p,s.offer.binding.provider);};
            s.stage="server-identity";if(!serverCurrent()){closeTransient(s);return false;}
            s.stage="io-event";s.io=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!s.io){closeTransient(s);return false;}
            WireRequestV1 request;request.command=command;request.purpose=Purpose::Observation;
            request.scope=s.offer.binding.scope;request.request=s.offer.binding.request;
            s.stage="write";bool ok=transfer(s,&request,sizeof request,true,deadline);
            if(ok){s.stage="read";ok=transfer(s,&output,sizeof output,false,deadline);}
            if(ok)s.stage="response-shape";ok=ok&&
                output.size==sizeof output&&output.version==1&&output.command==command&&output.accepted<=1&&serverCurrent();
            if(ok){s.stage="ack";request.command=WireCommand::Acknowledge;ok=transfer(s,&request,sizeof request,true,deadline);}
            if(ok)s.stage="reply-complete";return closeTransient(s)&&ok;
        }catch(...){closeTransient(s);return false;}
    }
    explicit NativeObservationClient(State* s)noexcept:state_(s){}
public:
    NativeObservationClient(const NativeObservationClient&)=delete;NativeObservationClient& operator=(const NativeObservationClient&)=delete;
    ~NativeObservationClient(){release(state_);}
    static std::shared_ptr<NativeObservationClient> import(const RemoteOfferV1& offer,HANDLE trustedNativeParent,bool creatorPairAuthenticated,NativeObservationImportDiagnostic* diagnostic=nullptr)noexcept{
        State* s=nullptr;const auto failed=[&]{if(diagnostic){diagnostic->stage=s?s->stage:"shape";diagnostic->error=GetLastError();}release(s);s=nullptr;};
        try{
            ProcessIdentityV1 p,c;
            if(!creatorPairAuthenticated||!shape(offer))return {};
            s=new(std::nothrow)State;if(!s)return {};s->offer=offer;
            if(!DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&s->self,PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0)){failed();return {};}
            if(!processIdentity(trustedNativeParent,p)||!processIdentity(s->self,c)||!sameProcess(p,offer.binding.provider)||
               !sameProcess(c,offer.binding.consumer)||p.session!=c.session){failed();return {};}
            if(!DuplicateHandle(GetCurrentProcess(),trustedNativeParent,GetCurrentProcess(),&s->parent,
                PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0)){failed();return {};}
            s->cancel=OpenEventW(SYNCHRONIZE,FALSE,cancellationEventName(offer.pipeName,offer.binding).c_str());
            WireResponseV1 response;
            if(!s->cancel||!exchange(*s,WireCommand::Bind,response)||response.accepted!=1||
               !sameDescriptor(offer.binding,response.binding)){failed();return {};}
            auto owner=std::unique_ptr<NativeObservationClient>(new NativeObservationClient(s));s=nullptr;
            std::shared_ptr<NativeObservationClient> result(std::move(owner));
            if(!result->current()){if(diagnostic){diagnostic->stage=result->state_->stage;diagnostic->error=GetLastError();}return {};}if(diagnostic){diagnostic->stage="imported";diagnostic->error=0;}return result;
        }catch(...){failed();return {};}
    }
    // Metadata only;uses the same exact failed-close ledger,no action grant.
    static bool readInheritedMetadata(HANDLE handle,void* output,SIZE_T bytes)noexcept{State* state=new(std::nothrow)State;if(!state)return false;state->metadata=handle;if(!SetHandleInformation(handle,HANDLE_FLAG_INHERIT,0)){release(state);return false;}state->metadataView=MapViewOfFile(handle,FILE_MAP_READ,0,0,bytes);const bool read=state->metadataView!=nullptr;if(read)std::memcpy(output,state->metadataView,bytes);const bool closed=closeOwned(*state);if(closed)delete state;else release(state);return read&&closed;}
    HANDLE cancellationEvent()const noexcept{return state_?state_->cancel:nullptr;} // borrowed,never reset
    bool current()noexcept{
        if(!state_)return false;try{std::lock_guard lock(state_->gate);WireResponseV1 response;
            if(localCurrent(*state_)&&exchange(*state_,WireCommand::Current,response)&&response.accepted==1&&
               sameDescriptor(state_->offer.binding,response.binding)&&powerAdmitted(response.current)&&
               samePowerObservation(state_->offer.binding.captured,response.current)&&localCurrent(*state_))return true;
            state_->retired=true;return false;
        }catch(...){state_->retired=true;return false;}
    }
    // A failed CloseHandle remains owned here. Original owner may retry at its
    // existing cleanup point; this class never starts a reap thread or poller.
    static unsigned reap()noexcept{
        State* list=nullptr;{std::lock_guard lock(retainedMutex_);std::swap(list,retained_);}
        while(list){auto* s=list;list=list->next;s->next=nullptr;release(s);}
        std::lock_guard lock(retainedMutex_);unsigned n=0;for(auto* s=retained_;s;s=s->next)++n;return n;
    }
};
}
