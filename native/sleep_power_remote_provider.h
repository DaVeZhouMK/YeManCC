#pragma once
#include <windows.h>
#include <atomic>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include "sleep_power_remote_contract.h"
#include "sleep_power_revocation.h"

namespace ymcc::sleeppower::remote {
inline bool sameKey(Key128 a,Key128 b) noexcept {return a.low==b.low&&a.high==b.high;}
inline bool nonzeroKey(Key128 value) noexcept {return value.low||value.high;}
inline bool sameProcess(ProcessIdentityV1 a,ProcessIdentityV1 b) noexcept {
    return a.pid==b.pid&&a.session==b.session&&a.birth100ns==b.birth100ns;
}
inline bool processIdentity(HANDLE process,ProcessIdentityV1& output) noexcept {
    output={};
    if(!process||process==INVALID_HANDLE_VALUE||WaitForSingleObject(process,0)!=WAIT_TIMEOUT)return false;
    ProcessIdentityV1 value;value.pid=GetProcessId(process);
    FILETIME created{},exit{},kernel{},user{};DWORD session=MAXDWORD;
    if(!value.pid||!ProcessIdToSessionId(value.pid,&session)||
       !GetProcessTimes(process,&created,&exit,&kernel,&user))return false;
    value.session=static_cast<std::uint32_t>(session);
    value.birth100ns=(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    if(!value.birth100ns||WaitForSingleObject(process,0)!=WAIT_TIMEOUT)return false;
    output=value;return true;
}
class ProcessHold final {
    HANDLE value_=nullptr;
public:
    ProcessHold() noexcept=default;
    ProcessHold(const ProcessHold&)=delete;
    ProcessHold& operator=(const ProcessHold&)=delete;
    ~ProcessHold(){if(value_)CloseHandle(value_);}
    bool bind(HANDLE source) noexcept {
        if(value_)return false;
        return DuplicateHandle(GetCurrentProcess(),source,GetCurrentProcess(),&value_,
            PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0)!=FALSE;
    }
    HANDLE get() const noexcept{return value_;}
};
using PurposeAdmission=std::function<bool(Purpose)>;
using CancellationEventFactory=std::function<HANDLE(const DescriptorV1&)>;
inline std::atomic<std::uint64_t> issuedScopeIdentities{0};

// Source-side qualification/ledger. Transport authenticates the actual peer
// before passing its held process. No claimed PID or client-supplied power is
// accepted. Capture is called by the original Native capability owner only.
class NativeOwnedTaskBorrow;
class NativeProvider;
struct NativeTaskOwnerLifetime {std::mutex gate;NativeProvider* owner=nullptr;};

class NativeScope final {
    friend class NativeProvider;
    friend class NativeOwnedTaskBorrow;
    const DescriptorV1 binding_;
    const Reader reader_;
    const PurposeAdmission purposeAdmission_;
    const CancellationEventFactory eventFactory_;
    const std::shared_ptr<std::atomic<bool>> providerOpen_;
    ProcessHold consumer_;
    RevocationMirrors::Registration registration_;
    std::atomic<bool> cancelled_{false},returned_{false};
    std::atomic<bool> remoteImportClaimed_{false};
    std::atomic<SettlementOutcome> settlement_{SettlementOutcome::Unknown};
    std::mutex settlementMutex_;
    std::uint32_t settlementFlags_=0;
    std::mutex childrenMutex_;
    std::vector<std::weak_ptr<NativeScope>> children_;
    std::shared_ptr<NativeScope> parent_;
    bool childIssued_=false;
    // Native-owned ordinary observation borrow only;never a device/SDK scope.
    bool nativeTaskBorrow_=false,nativeTaskKernelReturned_=false;
    std::atomic<bool> nativeTaskPublished_{false},nativeTaskReturned_{false};
    HANDLE nativeTaskJob_=nullptr;
    NativeScope(DescriptorV1 binding,Reader reader,PurposeAdmission admission,
                std::shared_ptr<std::atomic<bool>> providerOpen,CancellationEventFactory eventFactory)
        :binding_(binding),reader_(reader),purposeAdmission_(std::move(admission)),eventFactory_(std::move(eventFactory)),providerOpen_(std::move(providerOpen)){}
    bool purposeAdmitted() const noexcept {
        try{return purposeAdmission_&&purposeAdmission_(binding_.purpose);}catch(...){return false;}
    }
    bool peerCurrent(HANDLE authenticatedPeer) const noexcept {
        ProcessIdentityV1 held,peer;
        return processIdentity(consumer_.get(),held)&&processIdentity(authenticatedPeer,peer)&&
            sameProcess(held,binding_.consumer)&&sameProcess(peer,binding_.consumer);
    }
    bool live() const noexcept {
        ProcessIdentityV1 held;
        return providerOpen_&&providerOpen_->load(std::memory_order_acquire)&&(!parent_||parent_->live())&&
            !cancelled_.load(std::memory_order_acquire)&&!returned_.load(std::memory_order_acquire)&&
            registration_&&!registration_.cancelled()&&
            processIdentity(consumer_.get(),held)&&sameProcess(held,binding_.consumer);
    }
public:
    NativeScope(const NativeScope&)=delete;
    NativeScope& operator=(const NativeScope&)=delete;
    ~NativeScope(){cancel();if(nativeTaskJob_)CloseHandle(nativeTaskJob_);}
    const DescriptorV1& binding() const noexcept{return binding_;}
    // Borrowed from the original Registry. A transport must retain this scope
    // until every exported borrower returns, not just until HTTP completes.
    HANDLE cancellationEvent() const noexcept{return registration_.event();}
    bool read(HANDLE authenticatedPeer,SharedPowerV1& output) noexcept {
        output=SharedPowerV1{};
        if(!peerCurrent(authenticatedPeer)||!live())return false;
        SharedPowerV1 value;
        if(!capturePowerObservation(reader_,value)||!live()||!peerCurrent(authenticatedPeer)){cancel();return false;}
        output=value;return true;
    }
    bool current(HANDLE authenticatedPeer,Purpose required) noexcept {
        if(required!=binding_.purpose||!peerCurrent(authenticatedPeer)||!live())return false;
        if(!purposeAdmitted()){cancel();return false;}
        SharedPowerV1 value;
        if(!read(authenticatedPeer,value)||(value.flags&kNotificationPending)||
           !samePowerObservation(binding_.captured,value)){cancel();return false;}
        // Purpose callbacks run outside the registry/provider locks. A second
        // Native current read closes a change made while the owner was checked.
        if(purposeAdmitted()&&powerObservationCurrent(reader_,binding_.captured)&&
            live()&&peerCurrent(authenticatedPeer))return true;
        cancel();return false;
    }
    void cancel() noexcept {
        cancelled_.store(true,std::memory_order_release);
        registration_.cancel(); // Keep event/consumer ownership; never reset here.
        try{std::lock_guard<std::mutex> lock(childrenMutex_);
            for(const auto& weak:children_)if(auto child=weak.lock())child->cancel();
        }catch(...){/* Child current also checks the retained parent's sticky state. */}
    }
    SettlementOutcome lastSettlement() const noexcept{return settlement_.load(std::memory_order_acquire);}
};

class NativeProvider final {
    friend class NativeRemoteTransport;
    friend class NativeOwnedTaskBorrow;
public:
    // The original IPC worker waits on stop, IO, provider-change and these
    // exact held processes. This is a kernel wait limit, not a polling budget.
    static constexpr std::size_t maximumWaitableConsumers=MAXIMUM_WAIT_OBJECTS-3;
    struct ConsumerWait {
        std::shared_ptr<NativeScope> scope;
        HANDLE process=nullptr; // Borrowed while scope is retained by the wait.
    };
    struct ExitedConsumerDebt {
        DescriptorV1 binding;
        SettlementOutcome lastOutcome=SettlementOutcome::Unknown;
        std::uint32_t lastFlags=0;
        bool kernelExitObserved=false,nativeBorrowerStillHeld=false;
    };
private:
    struct ExitDebtRecord {
        DescriptorV1 binding;
        SettlementOutcome lastOutcome=SettlementOutcome::Unknown;
        std::uint32_t lastFlags=0;
        std::weak_ptr<NativeScope> nativeBorrowers;
    };
    const Reader reader_;
    RevocationMirrors& mirrors_;
    const std::size_t maximumOutstanding_;
    ProcessIdentityV1 provider_;
    std::shared_ptr<std::atomic<bool>> providerOpen_;
    std::atomic<bool> closed_{false},admissionSealed_{false};
    std::mutex mutex_;
    std::map<std::uint64_t,std::shared_ptr<NativeScope>> scopes_;
    std::map<std::uint64_t,ExitDebtRecord> exitedConsumerDebts_;
    HANDLE changed_=nullptr;
    std::mutex nativeTaskLifetimeMutex_;
    std::shared_ptr<NativeTaskOwnerLifetime> nativeTaskLifetime_;
    std::shared_ptr<NativeTaskOwnerLifetime> taskLifetime() {
        std::lock_guard lock(nativeTaskLifetimeMutex_);if(!nativeTaskLifetime_){nativeTaskLifetime_=std::make_shared<NativeTaskOwnerLifetime>();nativeTaskLifetime_->owner=this;}return nativeTaskLifetime_;
    }
    bool prepareOwnedTaskBorrow(NativeOwnedTaskBorrow&,HANDLE,HANDLE,Key128,PurposeAdmission,CancellationEventFactory)noexcept;
    bool returnOwnedTaskBorrow(const std::shared_ptr<NativeScope>& scope)noexcept {
        if(!scope||!scope->nativeTaskBorrow_||!scope->nativeTaskKernelReturned_||scope->nativeTaskJob_||!sameProcess(scope->binding().provider,provider_))return false;
        scope->cancel();
        try{std::lock_guard lock(mutex_);const auto key=scope->binding().scope;
            const auto live=scopes_.find(key.low);const auto debt=exitedConsumerDebts_.find(key.low);
            if(live!=scopes_.end()){if(live->second!=scope)return false;scopes_.erase(live);}
            else if(debt!=exitedConsumerDebts_.end()){if(!sameKey(debt->second.binding.scope,key)||debt->second.nativeBorrowers.lock()!=scope)return false;exitedConsumerDebts_.erase(debt);}
            else return false;
            scope->nativeTaskReturned_.store(true,std::memory_order_release);
        }catch(...){return false;}
        changed();return true;
    }
    void changed() noexcept {if(changed_)SetEvent(changed_);}
    bool canPublishLocked(ProcessIdentityV1 consumer) const noexcept {
        // An exited process is not a Returned ACK and does not refund a slot.
        if(scopes_.size()+exitedConsumerDebts_.size()>=maximumOutstanding_)return false;
        for(const auto& entry:scopes_)if(sameProcess(entry.second->binding().consumer,consumer))return true;
        std::size_t unique=0;
        for(auto i=scopes_.begin();i!=scopes_.end();++i){
            bool first=true;
            for(auto j=scopes_.begin();j!=i;++j)if(sameProcess(j->second->binding().consumer,i->second->binding().consumer)){first=false;break;}
            if(first)++unique;
        }
        return unique<maximumWaitableConsumers;
    }
    std::shared_ptr<NativeScope> find(Key128 key) noexcept {
        try{std::lock_guard<std::mutex> lock(mutex_);const auto i=scopes_.find(key.low);
            return i!=scopes_.end()&&sameKey(i->second->binding().scope,key)?i->second:nullptr;
        }catch(...){return {};}
    }
public:
    NativeProvider(Reader reader,RevocationMirrors& mirrors,std::size_t maximumOutstanding=256) noexcept
        :reader_(reader),mirrors_(mirrors),maximumOutstanding_(maximumOutstanding){
        try{providerOpen_=std::make_shared<std::atomic<bool>>(true);}catch(...){closed_=true;}
        changed_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!changed_)closed_=true;
        ProcessHold self;
        if(!self.bind(GetCurrentProcess())||!processIdentity(self.get(),provider_))closed_=true;
        if(closed_&&providerOpen_)providerOpen_->store(false,std::memory_order_release);
    }
    NativeProvider(const NativeProvider&)=delete;
    NativeProvider& operator=(const NativeProvider&)=delete;
    ~NativeProvider(){
        if(nativeTaskLifetime_){std::lock_guard lock(nativeTaskLifetime_->gate);nativeTaskLifetime_->owner=nullptr;}
        close();if(changed_)CloseHandle(changed_);
    }
private:
    std::shared_ptr<NativeScope> captureImpl(HANDLE heldConsumer,Key128 request,Purpose purpose,
                                           PurposeAdmission admission,bool publish,CancellationEventFactory eventFactory={},
                                           HANDLE nativeTaskJob=nullptr,std::shared_ptr<NativeScope>* nativeTaskOwner=nullptr) noexcept {
        try{
            if(!admissionOpen()||!reader_||!maximumOutstanding_||!nonzeroKey(request)||
               (purpose!=Purpose::Observation&&purpose!=Purpose::FanResume&&purpose!=Purpose::FanWrite)||!admission)return {};
            DescriptorV1 binding;binding.provider=provider_;binding.request=request;binding.purpose=purpose;
            if(!processIdentity(heldConsumer,binding.consumer)||
               !capturePowerObservation(reader_,binding.captured)||(binding.captured.flags&kNotificationPending)||!admission(purpose)||
               (nativeTaskOwner&&(purpose!=Purpose::Observation||!nativeTaskJob||!powerAdmitted(binding.captured))))return {};
            {std::lock_guard<std::mutex> lock(mutex_);
                if(!admissionOpen()||!canPublishLocked(binding.consumer))return {};
            }
            auto scopeIdentity=issuedScopeIdentities.load(std::memory_order_acquire);
            do {if(scopeIdentity==(std::numeric_limits<std::uint64_t>::max)())return {};}
            while(!issuedScopeIdentities.compare_exchange_weak(scopeIdentity,scopeIdentity+1,std::memory_order_acq_rel));
            binding.scope={scopeIdentity+1,provider_.birth100ns};
            // No Reader, purpose callback or Win32 allocation runs under the
            // provider lock. Original observeRead closes its registration race.
            auto scope=std::shared_ptr<NativeScope>(new NativeScope(binding,reader_,std::move(admission),providerOpen_,eventFactory));
            if(nativeTaskOwner){
                scope->nativeTaskBorrow_=true;*nativeTaskOwner=scope; // Retain every partial owned duplicate before another fallible step.
                BOOL member=FALSE;JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
                if(!IsProcessInJob(heldConsumer,nativeTaskJob,&member)||!member||
                   !QueryInformationJobObject(nativeTaskJob,JobObjectExtendedLimitInformation,&limits,sizeof limits,nullptr)||
                   !(limits.BasicLimitInformation.LimitFlags&JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)||
                   !DuplicateHandle(GetCurrentProcess(),nativeTaskJob,GetCurrentProcess(),&scope->nativeTaskJob_,JOB_OBJECT_QUERY|SYNCHRONIZE,FALSE,0))return {};
            }
            if(!scope->consumer_.bind(heldConsumer))return {};
            scope->registration_=eventFactory?mirrors_.observeReadWithEvent(reader_,binding.captured,eventFactory(binding)):
                mirrors_.observeRead(reader_,binding.captured);
            if(!scope->current(heldConsumer,purpose))return {};
            if(!publish)return scope;
            {std::lock_guard<std::mutex> lock(mutex_);
                if(!admissionOpen()||!canPublishLocked(binding.consumer))return {};
                scopes_.emplace(binding.scope.low,scope);
                if(scope->nativeTaskBorrow_)scope->nativeTaskPublished_.store(true,std::memory_order_release);
            }
            changed();
            // close() may have copied its scopes before/after insertion.
            if(!admissionOpen()||!scope->current(heldConsumer,purpose)){
                scope->cancel();
                {std::lock_guard<std::mutex> lock(mutex_);scopes_.erase(binding.scope.low);}
                changed(); // A waiter may already hold the published scope.
                return {};
            }
            return scope;
        }catch(...){return {};}
    }
    // Transport builds and checks an offer privately. Only this original
    // ledger publication makes it remotely importable; a setup failure owns
    // no published slot and needs no invented consumer Returned receipt.
    bool publishTransportScope(const std::shared_ptr<NativeScope>& scope) noexcept {
        if(!scope||!sameProcess(scope->binding().provider,provider_))return false;
        try{
            {std::lock_guard<std::mutex> lock(mutex_);
                if(!admissionOpen()||!scope->live()||!canPublishLocked(scope->binding().consumer))return false;
                if(!scopes_.emplace(scope->binding().scope.low,scope).second)return false;
                if(scope->nativeTaskBorrow_)scope->nativeTaskPublished_.store(true,std::memory_order_release);
            }
            changed();return true;
        }catch(...){return false;}
    }
public:
    std::shared_ptr<NativeScope> capture(HANDLE heldConsumer,Key128 request,Purpose purpose,
                                      PurposeAdmission admission,CancellationEventFactory eventFactory={}) noexcept {
        return captureImpl(heldConsumer,request,purpose,std::move(admission),true,std::move(eventFactory));
    }
    // Native-only withdrawal before any remote importer wins the claim. This
    // seals the same atomic claim, so a racing Bind cannot borrow a removed
    // event. It is not an authenticated Returned receipt or debt forgiveness.
    bool withdrawUnclaimed(Key128 key,HANDLE authenticatedPeer) noexcept {
        auto scope=find(key);if(!scope||scope->nativeTaskBorrow_||!scope->peerCurrent(authenticatedPeer))return false;
        bool expected=false;
        if(!scope->remoteImportClaimed_.compare_exchange_strong(expected,true,std::memory_order_acq_rel))return false;
        scope->cancel();
        try{std::lock_guard<std::mutex> lock(mutex_);const auto i=scopes_.find(key.low);
            if(i==scopes_.end()||i->second!=scope)return false;scopes_.erase(i);
        }catch(...){return false;}
        changed();return true;
    }
    bool describe(Key128 key,HANDLE authenticatedPeer,DescriptorV1& output) noexcept {
        output=DescriptorV1{};auto scope=find(key);
        if(!scope||!scope->peerCurrent(authenticatedPeer))return false;
        output=scope->binding();return true;
    }
    bool claimRemoteImport(Key128 key,HANDLE authenticatedPeer,Purpose required) noexcept {
        auto scope=find(key);if(!scope||!scope->current(authenticatedPeer,required))return false;
        bool expected=false;
        if(!scope->remoteImportClaimed_.compare_exchange_strong(expected,true,std::memory_order_acq_rel))return false;
        return scope->current(authenticatedPeer,required);
    }
    bool read(Key128 key,HANDLE authenticatedPeer,SharedPowerV1& output) noexcept {
        output=SharedPowerV1{};auto scope=find(key);
        return !closed_.load(std::memory_order_acquire)&&scope&&scope->read(authenticatedPeer,output);
    }
    bool current(Key128 key,HANDLE authenticatedPeer,Purpose required) noexcept {
        auto scope=find(key);return !closed_.load(std::memory_order_acquire)&&scope&&scope->current(authenticatedPeer,required);
    }
    // Native owner only: one FanResume -> FanWrite child, same request/current
    // public cohort, and a fresh evaluation of the original Write purpose.
    // Transport cannot request an upgrade merely by changing an enum.
private:
    std::shared_ptr<NativeScope> handoffToFanWriteImpl(Key128 parentKey,HANDLE authenticatedPeer,
                                                     PurposeAdmission writeAdmission,bool publish) noexcept {
        auto parent=find(parentKey);
        if(!parent||parent->binding().purpose!=Purpose::FanResume||
           !parent->current(authenticatedPeer,Purpose::FanResume))return {};
        auto child=captureImpl(authenticatedPeer,parent->binding().request,Purpose::FanWrite,std::move(writeAdmission),false,parent->eventFactory_);
        if(!child)return {};
        const auto discard=[&]() noexcept {
            child->cancel();try{std::lock_guard<std::mutex> lock(mutex_);scopes_.erase(child->binding().scope.low);}catch(...){}
        };
        try{
            if(!samePowerObservation(parent->binding().captured,child->binding().captured)||
               !parent->current(authenticatedPeer,Purpose::FanResume)){discard();return {};}
            bool attached=false;
            {std::lock_guard<std::mutex> lock(parent->childrenMutex_);
                if(parent->childIssued_||parent->returned_.load(std::memory_order_acquire)||!parent->live()){
                    attached=false;
                }else{
                    child->parent_=parent;
                    parent->children_.push_back(child);
                    parent->childIssued_=true;
                    attached=true;
                }
            }
            if(!attached){discard();return {};}
            if(!parent->current(authenticatedPeer,Purpose::FanResume)||
               !child->current(authenticatedPeer,Purpose::FanWrite)){discard();return {};}
            if(!publish)return child;
            {std::lock_guard<std::mutex> lock(mutex_);
                if(!admissionOpen()||!canPublishLocked(child->binding().consumer)){child->cancel();return {};}
                scopes_.emplace(child->binding().scope.low,child);
            }
            changed();
            return child;
        }catch(...){discard();return {};}
    }
public:
    std::shared_ptr<NativeScope> handoffToFanWrite(Key128 parentKey,HANDLE authenticatedPeer,
                                                 PurposeAdmission writeAdmission) noexcept {
        return handoffToFanWriteImpl(parentKey,authenticatedPeer,std::move(writeAdmission),true);
    }
    bool cancel(Key128 key) noexcept {auto scope=find(key);if(!scope)return false;scope->cancel();return true;}
    bool settle(HANDLE authenticatedPeer,const SettlementV1& receipt) noexcept {
        auto scope=find(receipt.scope);
        if(!scope||scope->nativeTaskBorrow_||receipt.size!=sizeof(receipt)||receipt.version!=kVersion||
           !sameKey(receipt.request,scope->binding().request)||
           !sameProcess(receipt.provider,scope->binding().provider)||
           !sameProcess(receipt.consumer,scope->binding().consumer)||!scope->peerCurrent(authenticatedPeer)||
           (receipt.flags&~std::uint32_t(AllReturned)))return false;
        if(receipt.outcome!=SettlementOutcome::Unknown&&receipt.outcome!=SettlementOutcome::RetainedResources&&
           receipt.outcome!=SettlementOutcome::Returned)return false;
        if(receipt.outcome==SettlementOutcome::Returned&&receipt.flags!=AllReturned)return false;
        try{std::lock_guard<std::mutex> lock(scope->settlementMutex_);
            if(scope->returned_.load(std::memory_order_acquire))return false;
            if(receipt.outcome==SettlementOutcome::Returned){
                std::lock_guard<std::mutex> childrenLock(scope->childrenMutex_);
                for(const auto& weak:scope->children_)if(auto child=weak.lock())
                    if(!child->returned_.load(std::memory_order_acquire))return false;
                scope->returned_.store(true,std::memory_order_release);
            }
            scope->settlement_.store(receipt.outcome,std::memory_order_release);
            scope->settlementFlags_=receipt.flags;
            if(receipt.outcome!=SettlementOutcome::Returned)return true; // Honest debt remains registered.
        }catch(...){return false;}
        scope->cancel();
        try{std::lock_guard<std::mutex> lock(mutex_);const auto i=scopes_.find(receipt.scope.low);
            if(i!=scopes_.end()&&i->second==scope)scopes_.erase(i);
        }catch(...){return false;}
        changed();
        return true;
    }
    HANDLE changeEvent() const noexcept{return changed_;} // Borrowed; provider outlives its IPC worker.
    bool admissionOpen() const noexcept{return !admissionSealed_.load(std::memory_order_acquire)&&!closed_.load(std::memory_order_acquire);}
    bool isOpen() const noexcept{return !closed_.load(std::memory_order_acquire);}
    bool consumerWaits(std::vector<ConsumerWait>& output) noexcept {
        output.clear();
        try{
            std::vector<std::shared_ptr<NativeScope>> snapshot;
            {std::lock_guard<std::mutex> lock(mutex_);
                for(const auto& entry:scopes_)snapshot.push_back(entry.second);
            }
            for(const auto& scope:snapshot){
                bool found=false;
                for(const auto& peer:output)if(sameProcess(peer.scope->binding().consumer,scope->binding().consumer)){found=true;break;}
                if(found)continue;
                if(output.size()>=maximumWaitableConsumers)return false;
                output.push_back({scope,scope->consumer_.get()});
            }
            return true;
        }catch(...){output.clear();return false;}
    }
    std::size_t retireExitedConsumer(Key128 representative) noexcept {
        const auto trigger=find(representative);
        if(!trigger||WaitForSingleObject(trigger->consumer_.get(),0)!=WAIT_OBJECT_0)return 0;
        std::size_t retired=0;
        try{
            std::vector<std::shared_ptr<NativeScope>> snapshot;
            {std::lock_guard<std::mutex> lock(mutex_);
                for(const auto& entry:scopes_)if(sameProcess(entry.second->binding().consumer,trigger->binding().consumer))snapshot.push_back(entry.second);
            }
            for(const auto& scope:snapshot){
                // Only a signaled kernel-held process retires Native ownership.
                // A timeout, inaccessible PID or lost reply never gets here.
                if(WaitForSingleObject(scope->consumer_.get(),0)!=WAIT_OBJECT_0)continue;
                scope->cancel();
                std::lock_guard<std::mutex> settlementLock(scope->settlementMutex_);
                if(scope->returned_.load(std::memory_order_acquire))continue;
                ExitDebtRecord debt{scope->binding(),scope->settlement_.load(std::memory_order_acquire),scope->settlementFlags_,scope};
                std::lock_guard<std::mutex> lock(mutex_);
                const auto current=scopes_.find(scope->binding().scope.low);
                if(current==scopes_.end()||current->second!=scope)continue;
                exitedConsumerDebts_.emplace(scope->binding().scope.low,std::move(debt));
                scopes_.erase(current);++retired;
            }
        }catch(...){close();} // Preserve unrecorded debt and close admission on allocation failure.
        if(retired)changed();
        return retired;
    }
    std::size_t exitedConsumerDebtCount() noexcept {
        try{std::lock_guard<std::mutex> lock(mutex_);return exitedConsumerDebts_.size();}catch(...){return maximumOutstanding_;}
    }
    bool exitedConsumerDebt(Key128 key,ExitedConsumerDebt& output) noexcept {
        output={};
        try{std::lock_guard<std::mutex> lock(mutex_);const auto found=exitedConsumerDebts_.find(key.low);
            if(found==exitedConsumerDebts_.end()||!sameKey(found->second.binding.scope,key))return false;
            const auto& value=found->second;
            output={value.binding,value.lastOutcome,value.lastFlags,true,!value.nativeBorrowers.expired()};return true;
        }catch(...){return false;}
    }
    std::size_t outstanding() noexcept {
        try{std::lock_guard<std::mutex> lock(mutex_);return scopes_.size();}catch(...){return maximumOutstanding_;}
    }
    // Terminal admission barrier only. The original IPC worker remains alive
    // to accept authenticated final settlement; no lock, join or ledger refund.
    void sealAdmission() noexcept {
        admissionSealed_.store(true,std::memory_order_release);
        if(providerOpen_)providerOpen_->store(false,std::memory_order_release);
        changed();
    }
    void close() noexcept {
        sealAdmission();
        closed_.store(true,std::memory_order_release);
        // Keep the ledger and registrations. Caller must still settle accepted
        // work; shutdown/cancel is never a synthetic resource-return receipt.
        try{std::lock_guard<std::mutex> lock(mutex_);
            for(const auto& entry:scopes_)entry.second->cancel();
        }catch(...){/* Original Registry/provider process lifetime still closes admission. */}
        changed();
    }
};

// Native source owner only. This capsule is embedded in the original task/Job
// ledger before capture. Remote callers cannot construct its source identity,
// discard generic SDK debts or send its completion over the wire.
class NativeOwnedTaskBorrow final {
    friend class NativeProvider;friend class NativeRemoteTransport;
    std::shared_ptr<NativeTaskOwnerLifetime> owner_;
    std::shared_ptr<NativeScope> scope_;
public:
    NativeOwnedTaskBorrow()noexcept=default;
    NativeOwnedTaskBorrow(const NativeOwnedTaskBorrow&)=delete;NativeOwnedTaskBorrow& operator=(const NativeOwnedTaskBorrow&)=delete;
    // Abandonment never claims Returned or refunds admission. Original owner
    // keeps the capsule through failed close,like its existing retained Job.
    ~NativeOwnedTaskBorrow(){cancel();}
    explicit operator bool()const noexcept{return bool(scope_);}
    DescriptorV1 binding()const noexcept{return scope_?scope_->binding():DescriptorV1{};}
    HANDLE cancellationEvent()const noexcept{return scope_?scope_->cancellationEvent():nullptr;}
    bool current(HANDLE authenticatedConsumer)noexcept{return scope_&&scope_->current(authenticatedConsumer,Purpose::Observation);}
    void cancel()noexcept{if(scope_)scope_->cancel();}
    // Original task ledger calls AFTER its child/Job drain and its original
    // local handles/copies return. This closes only this ordinary observation
    // borrow;it says nothing about any generic SDK/device scope.
    bool closeOwned()noexcept{
        if(!scope_){owner_.reset();return true;}scope_->cancel();
        if(scope_->nativeTaskPublished_.load(std::memory_order_acquire)&&!scope_->nativeTaskKernelReturned_){
            const auto process=scope_->consumer_.get();FILETIME born{},exit{},kernel{},user{};
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
            if(!process||!scope_->nativeTaskJob_||WaitForSingleObject(process,0)!=WAIT_OBJECT_0||
               GetProcessId(process)!=scope_->binding().consumer.pid||!GetProcessTimes(process,&born,&exit,&kernel,&user)||
               ((std::uint64_t(born.dwHighDateTime)<<32)|born.dwLowDateTime)!=scope_->binding().consumer.birth100ns||
               !QueryInformationJobObject(scope_->nativeTaskJob_,JobObjectBasicAccountingInformation,&accounting,sizeof accounting,nullptr)||
               accounting.ActiveProcesses||WaitForSingleObject(process,0)!=WAIT_OBJECT_0)return false;
            scope_->nativeTaskKernelReturned_=true;
        }
        if(scope_->nativeTaskJob_){if(!CloseHandle(scope_->nativeTaskJob_))return false;scope_->nativeTaskJob_=nullptr;}
        if(scope_->nativeTaskPublished_.load(std::memory_order_acquire)&&!scope_->nativeTaskReturned_.load(std::memory_order_acquire)){
            if(!owner_)return false;std::lock_guard lock(owner_->gate);
            if(!owner_->owner||!owner_->owner->returnOwnedTaskBorrow(scope_))return false;
        }
        scope_.reset();owner_.reset();return true;
    }
};
inline bool NativeProvider::prepareOwnedTaskBorrow(NativeOwnedTaskBorrow& target,HANDLE child,HANDLE job,Key128 request,
        PurposeAdmission admission,CancellationEventFactory eventFactory)noexcept{
    if(target.scope_||target.owner_)return false;
    try{target.owner_=taskLifetime();
        const auto scope=captureImpl(child,request,Purpose::Observation,std::move(admission),false,std::move(eventFactory),job,&target.scope_);
        return scope&&scope==target.scope_&&scope->nativeTaskBorrow_;
    }catch(...){return false;}
}
}
