#pragma once
#include <windows.h>
#include <sddl.h>
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include "sleep_power_remote_provider.h"
namespace ymcc::sleeppower::remote {
enum class WireCommand : std::uint32_t { Bind=1, Read=2, Current=3, Cancel=4, Settle=5, Acknowledge=6 };
struct WireRequestV1 {
    std::uint32_t size=sizeof(WireRequestV1),version=1;
    WireCommand command=WireCommand::Bind;
    Purpose purpose=Purpose::Observation;
    Key128 scope,request;
    SettlementV1 settlement;
};
struct WireResponseV1 {
    std::uint32_t size=sizeof(WireResponseV1),version=1,accepted=0;
    WireCommand command=WireCommand::Bind;
    DescriptorV1 binding;
    SharedPowerV1 current;
};
static_assert(sizeof(WireRequestV1)==128);
static_assert(sizeof(WireResponseV1)==192);
struct RemoteOfferV1 {std::wstring pipeName;DescriptorV1 binding;};
static_assert(std::is_nothrow_move_assignable_v<RemoteOfferV1>);

class KernelHandle final {
    HANDLE value_=nullptr;
public:
    KernelHandle() noexcept=default;
    explicit KernelHandle(HANDLE value) noexcept:value_(value){}
    KernelHandle(const KernelHandle&)=delete;
    KernelHandle& operator=(const KernelHandle&)=delete;
    ~KernelHandle(){reset();}
    HANDLE get() const noexcept{return value_;}
    explicit operator bool() const noexcept{return value_&&value_!=INVALID_HANDLE_VALUE;}
    void reset(HANDLE value=nullptr) noexcept {if(*this)CloseHandle(value_);value_=value;}
};
class LocalSecurity final {
    PSECURITY_DESCRIPTOR descriptor_=nullptr;
public:
    LocalSecurity() noexcept=default;
    LocalSecurity(const LocalSecurity&)=delete;
    ~LocalSecurity(){if(descriptor_)LocalFree(descriptor_);}
    bool create(const std::wstring& sid,const wchar_t* rights) noexcept {
        try{const auto sddl=std::wstring(L"D:P(A;;")+rights+L";;;"+sid+L")";
            return ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor_,nullptr)!=FALSE;
        }catch(...){return false;}
    }
    SECURITY_ATTRIBUTES attributes() const noexcept{return {sizeof(SECURITY_ATTRIBUTES),descriptor_,FALSE};}
};
inline std::wstring currentUserSid() {
    HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return {};
    KernelHandle held(token);DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
    if(!bytes)return {};std::vector<unsigned char> data(bytes);
    if(!GetTokenInformation(token,TokenUser,data.data(),bytes,&bytes))return {};
    LPWSTR text=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&text))return {};
    std::wstring out;try{out=text;}catch(...){LocalFree(text);throw;}LocalFree(text);return out;
}
inline bool validTransportStem(const std::wstring& stem) noexcept {
    if(stem.size()<60||stem.size()>160||stem.rfind(L"YeManCC.SharedPower.v1.",0)!=0)return false;
    for(const auto c:stem)if(!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||c==L'.'||
        (c>=L'A'&&c<=L'Z')||(c>=L'g'&&c<=L'z')))return false;
    return true;
}
inline std::wstring cancellationEventName(const std::wstring& stem,const DescriptorV1& binding) {
    return L"Local\\"+stem+L"."+std::to_wstring(binding.scope.low)+L"."+std::to_wstring(binding.scope.high)+L".Cancel";
}

// Formal metadata only. Authentication still uses the original pipe/provider.
inline constexpr const char* kRemotePowerOfferHeaderV1="X-YMCC-Shared-Power";
inline bool encodeRemotePowerOfferHeaderV1(const RemoteOfferV1& offer,std::string& output) noexcept {
    output.clear();const auto& b=offer.binding;
    if(!validTransportStem(offer.pipeName)||b.size!=128||b.version!=1||b.reserved!=0||
       !nonzeroKey(b.scope)||!nonzeroKey(b.request)||!b.provider.pid||!b.provider.birth100ns||
       !b.consumer.pid||!b.consumer.birth100ns||
       (b.purpose!=Purpose::Observation&&b.purpose!=Purpose::FanResume&&b.purpose!=Purpose::FanWrite&&!inputPurpose(b.purpose))||
       b.captured.size!=48||b.captured.version!=1||!b.captured.generation||b.captured.phase>3||
       !(b.captured.flags&kKnown)||(b.captured.flags&kNotificationPending))return false;
    try{std::string encoded="1 ";for(const auto c:offer.pipeName)encoded+=static_cast<char>(c);encoded+=' ';
        static constexpr char hex[]="0123456789abcdef";const auto* bytes=reinterpret_cast<const unsigned char*>(&b);
        for(std::size_t i=0;i<sizeof(b);++i){encoded+=hex[bytes[i]>>4];encoded+=hex[bytes[i]&15];}
        output=std::move(encoded);return true;
    }catch(...){output.clear();return false;}
}

// One lazy IPC worker; no Windows power source, sampling loop or new business
// retry. Each request reads the original provider. No remote mint/upgrade API.
class NativeRemoteTransport final {
    NativeProvider& provider_;
    mutable std::mutex lifecycle_;
    std::mutex offersMutex_;
    KernelHandle stop_,pipe_,io_;
    std::thread thread_;
    std::wstring stem_;
    std::shared_ptr<LocalSecurity> eventSecurity_;
    std::vector<std::weak_ptr<NativeScope>> offered_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> activeRequests_{0},servedRequests_{0},pendingIo_{0};
    bool trackOffered(const std::shared_ptr<NativeScope>& scope) noexcept {
        try{std::lock_guard<std::mutex> lock(offersMutex_);
            // Worker exit publishes running=false before taking this lock.
            // Either this offer is included in cancellation, or it is rejected.
            if(!running_.load(std::memory_order_acquire)){scope->cancel();return false;}
            std::size_t kept=0;
            for(const auto& weak:offered_){
                const auto previous=weak.lock();
                if(previous&&!terminalSettlement(previous->lastSettlement()))
                    offered_[kept++]=weak;
            }
            offered_.resize(kept);offered_.push_back(scope);return true;
        }catch(...){scope->cancel();return false;}
    }
    void cancelOffered() noexcept {
        try{std::lock_guard<std::mutex> lock(offersMutex_);
            for(const auto& weak:offered_)if(auto scope=weak.lock())scope->cancel();
            offered_.clear();
        }catch(...){provider_.close();}
    }
    bool observeExitedConsumers() noexcept {
        if(!provider_.isOpen()){SetEvent(stop_.get());return false;}
        std::vector<NativeProvider::ConsumerWait> consumers;
        if(!provider_.consumerWaits(consumers)){SetEvent(stop_.get());return false;}
        for(const auto& consumer:consumers){
            const auto wait=WaitForSingleObject(consumer.process,0);
            if(wait==WAIT_OBJECT_0)provider_.retireExitedConsumer(consumer.scope->binding().scope);
            else if(wait==WAIT_FAILED){SetEvent(stop_.get());return false;}
        }
        if(!provider_.isOpen()){SetEvent(stop_.get());return false;}
        return true;
    }
    bool complete(OVERLAPPED& operation,BOOL initiated,DWORD error,DWORD& transferred,DWORD maximumWait) noexcept {
        if(initiated)return observeExitedConsumers();
        if(error!=ERROR_IO_PENDING)return false;
        struct PendingIo {std::atomic<std::uint64_t>& count;explicit PendingIo(std::atomic<std::uint64_t>& value) noexcept:count(value){count.fetch_add(1,std::memory_order_acq_rel);}~PendingIo(){count.fetch_sub(1,std::memory_order_acq_rel);}} pending(pendingIo_);
        const auto deadline=maximumWait==INFINITE?0:GetTickCount64()+maximumWait;
        for(;;){
            if(!observeExitedConsumers())break;
            std::vector<NativeProvider::ConsumerWait> consumers;
            if(!provider_.consumerWaits(consumers)){SetEvent(stop_.get());break;}
            std::array<HANDLE,MAXIMUM_WAIT_OBJECTS> waits{};
            waits[0]=stop_.get();waits[1]=operation.hEvent;waits[2]=provider_.changeEvent();
            for(std::size_t i=0;i<consumers.size();++i)waits[i+3]=consumers[i].process;
            const auto now=GetTickCount64();
            if(maximumWait!=INFINITE&&now>=deadline)break;
            const auto remaining=maximumWait==INFINITE?INFINITE:static_cast<DWORD>(deadline-now);
            const auto wait=WaitForMultipleObjects(static_cast<DWORD>(consumers.size()+3),waits.data(),FALSE,remaining);
            if(wait==WAIT_OBJECT_0+1){
                const bool completed=GetOverlappedResult(pipe_.get(),&operation,&transferred,FALSE)!=FALSE;
                return observeExitedConsumers()&&completed;
            }
            if(wait==WAIT_OBJECT_0+2)continue; // New/returned scopes; same IO and original deadline.
            if(wait>=WAIT_OBJECT_0+3&&wait<WAIT_OBJECT_0+3+consumers.size()){
                provider_.retireExitedConsumer(consumers[wait-WAIT_OBJECT_0-3].scope->binding().scope);
                continue;
            }
            if(wait==WAIT_FAILED)SetEvent(stop_.get());
            break;
        }
        // Keep the exact OVERLAPPED and buffers alive until kernel completion.
        // No detach/timeout is claimed as resource return.
        CancelIoEx(pipe_.get(),&operation);DWORD ignored=0;
        GetOverlappedResult(pipe_.get(),&operation,&ignored,TRUE);
        return false;
    }
    bool transfer(bool write,void* data,DWORD bytes) noexcept {
        if(WaitForSingleObject(stop_.get(),0)!=WAIT_TIMEOUT)return false;
        if(!ResetEvent(io_.get()))return false;
        OVERLAPPED operation{};operation.hEvent=io_.get();DWORD transferred=0;
        const BOOL initiated=write?WriteFile(pipe_.get(),data,bytes,&transferred,&operation):
            ReadFile(pipe_.get(),data,bytes,&transferred,&operation);
        const DWORD error=initiated?ERROR_SUCCESS:GetLastError();
        return complete(operation,initiated,error,transferred,1000)&&transferred==bytes;
    }
    void serve() noexcept {
        while(WaitForSingleObject(stop_.get(),0)==WAIT_TIMEOUT){
            if(!ResetEvent(io_.get()))break;
            OVERLAPPED operation{};operation.hEvent=io_.get();DWORD transferred=0;
            const BOOL initiated=ConnectNamedPipe(pipe_.get(),&operation);
            const DWORD error=initiated?ERROR_SUCCESS:GetLastError();
            const bool connected=error==ERROR_PIPE_CONNECTED||complete(operation,initiated,error,transferred,INFINITE);
            if(!connected){if(WaitForSingleObject(stop_.get(),0)!=WAIT_TIMEOUT)break;DisconnectNamedPipe(pipe_.get());continue;}
            activeRequests_.fetch_add(1,std::memory_order_acq_rel);
            WireRequestV1 request;WireResponseV1 response;
            if(transfer(false,&request,sizeof(request))){
                response.command=request.command;
                ULONG clientPid=0;
                // Only the kernel pipe client identity is used to open peer.
                if(GetNamedPipeClientProcessId(pipe_.get(),&clientPid)&&clientPid){
                    KernelHandle peer(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,clientPid));
                    DescriptorV1 binding;
                    if(peer&&request.size==sizeof(request)&&request.version==1&&
                       provider_.describe(request.scope,peer.get(),binding)&&sameKey(request.request,binding.request)&&
                       request.purpose==binding.purpose){
                        response.binding=binding;
                        if(request.command==WireCommand::Bind)
                            response.accepted=provider_.claimRemoteImport(request.scope,peer.get(),request.purpose)?1u:0u;
                        else if(request.command==WireCommand::Current)
                            response.accepted=provider_.current(request.scope,peer.get(),request.purpose)?1u:0u;
                        else if(request.command==WireCommand::Read)
                            response.accepted=provider_.read(request.scope,peer.get(),response.current)?1u:0u;
                        else if(request.command==WireCommand::Cancel)
                            response.accepted=provider_.cancel(request.scope)?1u:0u;
                        else if(request.command==WireCommand::Settle&&sameKey(request.scope,request.settlement.scope)&&
                                sameKey(request.request,request.settlement.request))
                            response.accepted=provider_.settle(peer.get(),request.settlement)?1u:0u;
                        if(response.accepted&&(request.command==WireCommand::Bind||request.command==WireCommand::Current))
                            provider_.read(request.scope,peer.get(),response.current);
                    }
                }
                if(transfer(true,&response,sizeof(response))){
                    // DisconnectNamedPipe may discard unread response bytes.
                    // A bounded transport-only ACK proves the client consumed
                    // them; it is never a resource-return or action receipt.
                    WireRequestV1 acknowledgement;
                    if(transfer(false,&acknowledgement,sizeof(acknowledgement))&&
                       acknowledgement.size==sizeof(acknowledgement)&&acknowledgement.version==1&&
                       acknowledgement.command==WireCommand::Acknowledge&&
                       acknowledgement.purpose==request.purpose&&sameKey(acknowledgement.scope,request.scope)&&
                       sameKey(acknowledgement.request,request.request))
                        servedRequests_.fetch_add(1,std::memory_order_acq_rel);
                }
            }
            DisconnectNamedPipe(pipe_.get());activeRequests_.fetch_sub(1,std::memory_order_acq_rel);
        }
        running_.store(false,std::memory_order_release);
        // No lifecycle_ lock here: stop/start may own it while joining us.
        cancelOffered();
    }
public:
    explicit NativeRemoteTransport(NativeProvider& provider) noexcept:provider_(provider){}
    NativeRemoteTransport(const NativeRemoteTransport&)=delete;
    ~NativeRemoteTransport(){stop();}
    bool start() noexcept {
        try{std::lock_guard<std::mutex> lock(lifecycle_);
            if(thread_.joinable()){
                if(running_.load(std::memory_order_acquire))return provider_.isOpen();
                thread_.join();
                pipe_.reset();io_.reset();stop_.reset();
            }
            if(!provider_.isOpen())return false;
            const auto sid=currentUserSid();if(sid.empty())return false;
            ProcessHold self;ProcessIdentityV1 identity;
            if(!self.bind(GetCurrentProcess())||!processIdentity(self.get(),identity))return false;
            std::array<unsigned char,16> nonce{};
            if(BCryptGenRandom(nullptr,nonce.data(),static_cast<ULONG>(nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return false;
            static constexpr wchar_t hex[]=L"0123456789abcdef";
            stem_=L"YeManCC.SharedPower.v1."+std::to_wstring(identity.pid)+L"."+std::to_wstring(identity.birth100ns)+L".";
            for(const auto byte:nonce){stem_+=hex[byte>>4];stem_+=hex[byte&15];}
            if(!validTransportStem(stem_))return false;
            LocalSecurity pipeSecurity;eventSecurity_=std::make_shared<LocalSecurity>();
            if(!pipeSecurity.create(sid,L"GA")||!eventSecurity_->create(sid,L"0x00100000"))return false;
            auto security=pipeSecurity.attributes();
            pipe_.reset(CreateNamedPipeW((L"\\\\.\\pipe\\"+stem_).c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,sizeof(WireResponseV1),sizeof(WireRequestV1),0,&security));
            stop_.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));io_.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));
            if(!pipe_||!stop_||!io_){pipe_.reset();stop_.reset();io_.reset();return false;}
            running_.store(true,std::memory_order_release);
            try{thread_=std::thread([this]{serve();});}catch(...){running_=false;pipe_.reset();stop_.reset();io_.reset();return false;}
            return true;
        }catch(...){return false;}
    }
    std::wstring pipeName() const {std::lock_guard<std::mutex> lock(lifecycle_);return stem_;}
    std::shared_ptr<NativeScope> capture(HANDLE authenticatedConsumer,Key128 request,Purpose purpose,
                                        PurposeAdmission admission,RemoteOfferV1& output,
                                        std::function<std::shared_ptr<void>(HANDLE)> originalCancellation={}) noexcept {
        output=RemoteOfferV1{};
        if(!start())return {};
        try{std::wstring stem;std::shared_ptr<LocalSecurity> security;
            {std::lock_guard<std::mutex> lock(lifecycle_);if(!running_)return {};stem=stem_;security=eventSecurity_;}
            auto eventFactory=[stem,security](const DescriptorV1& binding)->HANDLE{
                auto attributes=security->attributes();SetLastError(ERROR_SUCCESS);
                const HANDLE event=CreateEventW(&attributes,TRUE,FALSE,cancellationEventName(stem,binding).c_str());
                const auto error=GetLastError();if(event&&error==ERROR_ALREADY_EXISTS){CloseHandle(event);return nullptr;}return event;
            };
            auto scope=provider_.captureImpl(authenticatedConsumer,request,purpose,std::move(admission),false,eventFactory);
            if(!scope)return {};
            if(originalCancellation){
                scope->inheritedCancellation_=originalCancellation(scope->cancellationEvent());
                if(!scope->inheritedCancellation_){scope->cancel();return {};}
            }
            RemoteOfferV1 prepared{stem,scope->binding()};
            if(!scope->current(authenticatedConsumer,purpose)){scope->cancel();return {};}
            {std::lock_guard<std::mutex> lock(lifecycle_);
                if(!running_||stem_!=stem||!trackOffered(scope)||!provider_.publishTransportScope(scope)){scope->cancel();return {};}
                output=std::move(prepared); // Proven nothrow after ledger publication.
            }
            return scope;
        }catch(...){return {};}
    }
    std::shared_ptr<NativeScope> handoffToFanWrite(Key128 parent,HANDLE authenticatedConsumer,
                                                PurposeAdmission admission,RemoteOfferV1& output) noexcept {
        output=RemoteOfferV1{};if(!running_.load(std::memory_order_acquire))return {};
        try{std::wstring stem;
            {std::lock_guard<std::mutex> lock(lifecycle_);if(!running_)return {};stem=stem_;}
            auto scope=provider_.handoffToFanWriteImpl(parent,authenticatedConsumer,std::move(admission),false);
            if(!scope)return {};
            RemoteOfferV1 prepared{stem,scope->binding()};
            if(!scope->current(authenticatedConsumer,Purpose::FanWrite)){scope->cancel();return {};}
            {std::lock_guard<std::mutex> lock(lifecycle_);
                if(!running_||stem_!=stem||!trackOffered(scope)||!provider_.publishTransportScope(scope)){scope->cancel();return {};}
                output=std::move(prepared);
            }
            return scope;
        }catch(...){return {};}
    }
    // Caller embeds output in its original ledger before every fallible step.
    bool captureOwnedTask(NativeOwnedTaskBorrow& output,HANDLE child,HANDLE ownedJob,Key128 request,
                         PurposeAdmission admission,RemoteOfferV1& offer)noexcept{
        offer={};if(!start())return false;
        try{std::wstring stem;std::shared_ptr<LocalSecurity> security;
            {std::lock_guard lock(lifecycle_);if(!running_)return false;stem=stem_;security=eventSecurity_;}
            auto eventFactory=[stem,security](const DescriptorV1& binding)->HANDLE{
                auto attributes=security->attributes();SetLastError(ERROR_SUCCESS);
                const auto event=CreateEventW(&attributes,TRUE,FALSE,cancellationEventName(stem,binding).c_str());
                const auto error=GetLastError();if(event&&error==ERROR_ALREADY_EXISTS){CloseHandle(event);return nullptr;}return event;
            };
            if(!provider_.prepareOwnedTaskBorrow(output,child,ownedJob,request,std::move(admission),eventFactory))return false;
            auto scope=output.scope_;RemoteOfferV1 prepared{stem,scope->binding()};
            if(!output.current(child))return false;
            {std::lock_guard lock(lifecycle_);
                if(!running_||stem_!=stem||!trackOffered(scope)||!provider_.publishTransportScope(scope)){output.cancel();return false;}
                offer=std::move(prepared);
            }
            return true;
        }catch(...){output.cancel();return false;}
    }
    bool withdrawUnclaimed(Key128 key,HANDLE authenticatedPeer) noexcept {
        return provider_.withdrawUnclaimed(key,authenticatedPeer);
    }
    void stop() noexcept {
        try{std::lock_guard<std::mutex> lock(lifecycle_);
            cancelOffered();
            if(stop_)SetEvent(stop_.get());
            if(thread_.joinable())thread_.join();
            pipe_.reset();io_.reset();stop_.reset();running_=false;
        }catch(...){/* Never detach the worker or free pending IO buffers. */}
    }
    // Read-only Native diagnostics; never a power or resource-return grant.
    std::size_t trackedOffers() noexcept {try{std::lock_guard<std::mutex> lock(offersMutex_);return offered_.size();}catch(...){return static_cast<std::size_t>(-1);}}
    std::size_t trackedOfferCapacity() noexcept {try{std::lock_guard<std::mutex> lock(offersMutex_);return offered_.capacity();}catch(...){return static_cast<std::size_t>(-1);}}
    bool running() const noexcept{return running_.load(std::memory_order_acquire);}
    std::uint64_t activeRequests() const noexcept{return activeRequests_.load(std::memory_order_acquire);}
    std::uint64_t servedRequests() const noexcept{return servedRequests_.load(std::memory_order_acquire);}
    std::uint64_t pendingIo() const noexcept{return pendingIo_.load(std::memory_order_acquire);}
};

// Process lifetime owner: provider precedes its IPC worker and outlives it.
// Requesting exit seals work; the existing cleanup owner performs the join.
class NativeRemoteRuntime final {
    NativeProvider provider_;
    NativeRemoteTransport transport_;
public:
    NativeRemoteRuntime(Reader reader,RevocationMirrors& mirrors) noexcept:provider_(reader,mirrors),transport_(provider_){}
    NativeRemoteRuntime(const NativeRemoteRuntime&)=delete;
    ~NativeRemoteRuntime(){finishStop();}
    NativeProvider& provider() noexcept{return provider_;}
    NativeRemoteTransport* transport() noexcept{return provider_.admissionOpen()?&transport_:nullptr;}
    void requestStop() noexcept {provider_.sealAdmission();}
    void finishStop() noexcept {requestStop();transport_.stop();provider_.close();}
};
class NativeRemoteRuntimeGate final {
    std::atomic<bool> closed_{false};
    std::atomic<NativeRemoteRuntime*> runtime_{nullptr};
public:
    NativeRemoteRuntimeGate() noexcept=default;
    NativeRemoteRuntimeGate(const NativeRemoteRuntimeGate&)=delete;
    bool closed() const noexcept{return closed_.load();}
    NativeRemoteRuntime* published() const noexcept{return runtime_.load();}
    NativeRemoteRuntime* publish(NativeRemoteRuntime& runtime) noexcept {
        NativeRemoteRuntime* expected=nullptr;
        if(!runtime_.compare_exchange_strong(expected,&runtime)&&expected!=&runtime){runtime.requestStop();return nullptr;}
        // Sequential consistency closes the store/load publication race with
        // requestStop without a mutex or allocating a runtime on cold exit.
        if(closed_.load()){runtime.requestStop();return nullptr;}
        return &runtime;
    }
    void requestStop() noexcept {
        closed_.store(true);
        if(auto* runtime=runtime_.load())runtime->requestStop();
    }
    void finishStop() noexcept {
        requestStop();
        if(auto* runtime=runtime_.load())runtime->finishStop();
    }
};
}
