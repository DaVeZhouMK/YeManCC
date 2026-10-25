// Included by main.cpp after the STA entry points. Native sensor resources in
// this file are created, polled and destroyed solely on that apartment.
static bool inputHostLifecycleBusy();
struct WindowsSelectionRequest {
    ymcc::sensors::selection::Proposal proposal;
    bool returnDefault=false;
    std::shared_ptr<std::atomic<bool>> cancelled=std::make_shared<std::atomic<bool>>(false);
};
static std::mutex g_windowsSelectionMx;
static bool g_windowsSelectionStaOpen=false,g_windowsSelectionRunning=false;
static std::uint64_t g_windowsSelectionNextId=0;
static std::optional<WindowsSelectionRequest> g_windowsSelectionPending,g_windowsSelectionActive;
static std::atomic<std::size_t> g_windowsSelectionLeaseCount{0};
static json g_windowsSelectionStatus={{"schemaVersion",1},{"selectionBackendIntegrated",true},{"phase","idle"},
    {"automaticSelectionAllowed",false},{"physicalAxesVerified",false},{"hostSubmissionProven",false},{"gameConsumptionProven",false}};
static json inputCaptureWindowsSourceSelectionStatus() {
    std::lock_guard<std::mutex> lock(g_windowsSelectionMx);auto value=g_windowsSelectionStatus;
    value["busy"]=g_windowsSelectionRunning||bool(g_windowsSelectionPending);
    value["acceptingRequests"]=g_windowsSelectionStaOpen;
    value["nativeLeaseCount"]=g_windowsSelectionLeaseCount.load(std::memory_order_acquire);
    const auto context=std::atomic_load_explicit(&g_windowsSelectionWorkerContext,std::memory_order_acquire);
    value["workerContextCurrent"]=inputCaptureWindowsSelectionContextCurrent(context);
    const auto cache=std::atomic_load_explicit(&g_windowsProcessingReceipt,std::memory_order_acquire);
    const auto processed=cache?cache->read(GetTickCount64(),currentPowerGeneration()):std::nullopt;
    value["processingReceiptCurrent"]=bool(processed);value["sourcePublicationAllowed"]=nullptr;value["committedChoiceCurrent"]=false;
    if(processed){auto stamp=processed->identity.source;stamp.publication=processed->frame.publication;
        value["sourcePublicationAllowed"]=ymcc::windows_motion::currentForOutput(stamp);
        value["committedChoiceCurrent"]=value["workerContextCurrent"]==true&&
            value.value("committedChoiceRequestId",std::uint64_t{0})==processed->identity.choiceRequest&&processed->identity.choiceRequest!=0;}
    value["processingOwnerBusy"]=inputHostLifecycleBusy();
    value["queryDoesNotSample"]=true;return value;
}
static void inputCaptureWindowsSelectionSetStaOpen(bool value) {
    std::lock_guard<std::mutex> lock(g_windowsSelectionMx);g_windowsSelectionStaOpen=value;
    if(!value){if(g_windowsSelectionPending)g_windowsSelectionPending->cancelled->store(true);if(g_windowsSelectionActive)g_windowsSelectionActive->cancelled->store(true);}
}
static bool inputCaptureWindowsSelectionHasWork() {
    if(g_windowsSelectionLeaseCount.load(std::memory_order_acquire))return true;
    std::lock_guard<std::mutex> lock(g_windowsSelectionMx);return bool(g_windowsSelectionPending);
}
static json inputCaptureWindowsSourceSelectionRequest(const json& args,bool returnDefault) {
    using namespace ymcc::sensors;
    try {
        const auto context=std::atomic_load_explicit(&g_windowsSelectionWorkerContext,std::memory_order_acquire);
        if(!inputCaptureWindowsSelectionContextCurrent(context))return {{"ok",false},{"reason","processing-context-not-current"}};
        WindowsSelectionRequest request;request.returnDefault=returnDefault;
        if(!returnDefault){
            if(args.value("transformPolicy",std::string{})!="explicit-current-model")return {{"ok",false},{"reason","explicit-transform-policy-required"}};
            request.proposal.pair={args.value("gyroDeviceId",std::string{}),args.value("accelDeviceId",std::string{})};
            request.proposal.policy=selection::TransformPolicy::ExplicitCurrentModel;
            request.proposal.expectedRecipeFingerprint=args.value("recipeFingerprint",std::string{});
        }
        std::lock_guard<std::mutex> worker(g_sensorThreadMx);
        std::lock_guard<std::mutex> lock(g_windowsSelectionMx);
        if(!g_windowsSelectionStaOpen||!g_sensorThreadWake||!g_sensorThreadMailbox.acceptsWork()||g_sensorThreadMailbox.hasQueuedWork()||
            inputCaptureSensorStopRequested()||g_windowsSourceProbeBusy.load(std::memory_order_acquire)||!inputCaptureWindowsSelectionContextCurrent(context))return {{"ok",false},{"reason","sensor-thread-unavailable"}};
        if(g_windowsSelectionRunning||g_windowsSelectionPending)return {{"ok",false},{"reason","single-source-writer-busy"}};
        if(g_windowsSelectionNextId==(std::numeric_limits<std::uint64_t>::max)())return {{"ok",false},{"reason","request-id-exhausted"}};
        request.proposal.requestId=g_windowsSelectionNextId+1;
        if(!returnDefault)if(const auto* reason=selection::authorizeIntent(context->model,request.proposal))return {{"ok",false},{"reason",reason}};
        g_windowsSelectionNextId=request.proposal.requestId;g_windowsSelectionPending=request;
        g_windowsSelectionStatus["phase"]="queued";g_windowsSelectionStatus["requestId"]=request.proposal.requestId;
        g_windowsSelectionStatus["operation"]=returnDefault?"return-default":"select-explicit-pair";
        g_windowsSelectionStatus["inputProcessingCommitted"]=false;
        if(!SetEvent(g_sensorThreadWake)){g_windowsSelectionPending.reset();g_windowsSelectionStatus["phase"]="wake-failed";
            return {{"ok",false},{"reason","sensor-thread-wake-failed"}};}
        return {{"ok",true},{"accepted",true},{"requestId",request.proposal.requestId},{"phase","queued"},
            {"inputProcessingCommitted",false},{"hostSubmissionProven",false},{"gameConsumptionProven",false}};
    }catch(...){return {{"ok",false},{"reason","invalid-selection-request"}};}
}
static json inputCaptureWindowsSourceSelectionCancel(const json& args) {
    try {
        const auto id=args.value("requestId",std::uint64_t{0});if(!id)return {{"ok",false},{"reason","request-id-required"}};
        std::lock_guard<std::mutex> lock(g_windowsSelectionMx);
        for(auto* request:{&g_windowsSelectionPending,&g_windowsSelectionActive})if(*request&&(*request)->proposal.requestId==id){
            (*request)->cancelled->store(true,std::memory_order_release);return {{"ok",true},{"requestId",id},{"cancellationRequested",true}};}
        return {{"ok",false},{"reason","request-not-pending"},{"requestId",id}};
    }catch(...){return {{"ok",false},{"reason","invalid-cancel-request"}};}
}
class WindowsSelectionStaOwner final {
    using Active=ymcc::sensors::selection::ActiveSource;
    using Receipt=ymcc::sensors::selection::Receipt;
    ymcc::sensors::selection::SelectionTransaction transaction_;
    std::map<std::uint64_t,std::unique_ptr<WindowsPrivateSourceLease>> leases_;
    std::shared_ptr<const WindowsSelectionWorkerContext> context_;
    std::optional<WindowsSelectionRequest> request_;
    Active route_,savedDefault_;
    std::uint64_t nextResource_=0,lastNativePoll_=0,readinessWaitUsedMs_=0;
    static constexpr std::uint64_t readinessWaitBudgetMs_=30000;
    bool managed_=false,processingConfirmed_=false;
    std::optional<ymcc::sensors::processing::Receipt> committedProof_;
    std::string phase_="idle",failure_;
    std::optional<std::int64_t> hresult_;
    void phase(const char* name){phase_=name;std::lock_guard<std::mutex> lock(g_windowsSelectionMx);
        g_windowsSelectionStatus["phase"]=name;}
    bool cancelled()const noexcept {
        return !request_||request_->cancelled->load(std::memory_order_acquire)||inputCaptureSensorStopRequested()||
            g_sensorThreadMailbox.hasQueuedWork()||g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready||
            !inputCaptureWindowsSelectionContextCurrent(context_)||
            std::atomic_load_explicit(&g_windowsSelectionWorkerContext,std::memory_order_acquire)!=context_;
    }
    ymcc::sensors::stream::CaptureParent parent()const {
        const auto now=std::atomic_load_explicit(&g_windowsSelectionWorkerContext,std::memory_order_acquire);
        ymcc::sensors::stream::CaptureParent result;
        if(now){result.scope=now->capture;result.lifetime=now->lifetime;result.currentLifetime=g_windowsSourceModuleLifetime.load();
            result.currentPower=currentPowerGeneration();result.model=ymcc::sensors::selection::fingerprint(now->model);
            result.modelBound=inputCaptureWindowsSelectionContextCurrent(now)&&now==context_;}return result;
    }
    Active defaultRoute()const {
        Active value;if(!context_)return value;
        const auto choice=g_windowsChoiceCounter->activeBindingGeneration.load(std::memory_order_acquire);
        value.scope={ymcc::windows_motion::retain(g_windowsChoiceCounter,choice),context_->capture.power};
        value.model=context_->model;value.pair={context_->metadata.gyroDeviceId,context_->metadata.accelDeviceId};
        value.api={context_->metadata.gyroFamily,context_->metadata.accelFamily};value.profile=context_->defaultProfile;
        value.calibrationKey=context_->defaultKey;value.lifetime=context_->lifetime;value.currentLifetime=g_windowsSourceModuleLifetime.load();
        value.currentPower=currentPowerGeneration();value.acceptsChanges=inputCaptureWindowsSelectionContextCurrent(context_);return value;
    }
    Active current() {
        using namespace ymcc::sensors;
        if(!context_)return {};
        auto value=route_;value.frame.reset();value.currentPower=currentPowerGeneration();
        value.currentLifetime=g_windowsSourceModuleLifetime.load(std::memory_order_acquire);
        value.acceptsChanges=inputCaptureWindowsSelectionContextCurrent(context_)&&parent().modelBound;
        if(value.resource){const auto found=leases_.find(value.resource);
            if(found!=leases_.end())value.frame=found->second->source()->frame(value,parent(),GetTickCount64());
        }else {
            const auto raw=inputCaptureWindowsDefaultViewSnapshot(false);
            if(!raw.coherent||!ymcc::windows_motion::same(raw.capture,context_->capture.binding)||
                !selection::same(value.pair.gyroId,raw.metadata.gyroDeviceId)||!selection::same(value.pair.accelId,raw.metadata.accelDeviceId))return value;
            selection::LiveFrame frame;frame.scope=value.scope;frame.captureScope=context_->capture;frame.authority=value.authority;
            frame.lifetime=value.lifetime;frame.selectionId=value.selectionId;frame.pair=value.pair;frame.profile=value.profile;frame.calibrationKey=value.calibrationKey;
            for(unsigned kind=0;kind<2;++kind){auto& part=frame.values[kind];const auto& sampled=raw.parts[kind];
                part.scope=value.scope;part.captureScope=context_->capture;part.authority=value.authority;part.lifetime=value.lifetime;
                part.kind=kind==0?inventory::Kind::Gyro:inventory::Kind::Accel;part.id=kind==0?value.pair.gyroId:value.pair.accelId;
                part.provider=inputCaptureSensorProviderKey(part.id);part.api=value.api[kind];part.standardUnits=true;
                part.usable=sampled.usable&&sampled.generation==context_->capture.binding.bindingGeneration;
                part.sample={sampled.axes[0],sampled.axes[1],sampled.axes[2],sampled.timestamp100ns,sampled.receipt};
                part.sequence=sampled.sequence;part.rejected=sampled.rejected;
                // Default sources retain their pre-existing native transform policy;
                // private WinRT sources require an actual ReadingTransform read.
                // No fabricated default ReadingTransform observation.
            }value.frame=std::move(frame);
        }return value;
    }
    void revokePublication() noexcept {
        const auto projection=std::atomic_load_explicit(&g_windowsPrivateConsumerProjection,std::memory_order_acquire);
        if(projection&&projection->publication)projection->publication->revoke();
        const auto fence=std::atomic_load_explicit(&g_windowsDefaultPublicationFence,std::memory_order_acquire);if(fence)fence->revoke();
    }
    bool reserve(std::uint64_t floor,std::uint64_t& generation) {
        auto observed=g_windowsChoiceCounter->activeBindingGeneration.load(std::memory_order_acquire);
        if(observed<floor||observed==(std::numeric_limits<std::uint64_t>::max)())return false;
        generation=observed+1;return g_windowsChoiceCounter->activeBindingGeneration.compare_exchange_strong(observed,generation,std::memory_order_acq_rel);
    }
    ymcc::windows_motion::Stamp rawStamp()const {
        return ymcc::windows_motion::guarded(context_->capture.binding,route_.scope.binding,route_.captureLease);
    }
    bool publishPending(const Active& target,std::uint64_t floor,const ymcc::sensors::selection::Authority& authority) {
        if(cancelled())return false;std::uint64_t generation=0;
        const auto safety=ymcc::windows_motion::PublicationFence::pending(ymcc::windows_motion::guarded(context_->capture.binding,
            ymcc::windows_motion::retain(g_windowsChoiceCounter,g_windowsChoiceCounter->activeBindingGeneration.load())));
        if(!safety){failure_="default-publication-safety-fence-not-created";return false;}
        revokePublication();std::atomic_store_explicit(&g_windowsDefaultPublicationFence,safety,std::memory_order_release);managed_=true;
        if(!reserve(floor,generation)){failure_="choice-generation-not-reserved";return false;}
        auto next=target;next.scope.binding=ymcc::windows_motion::retain(g_windowsChoiceCounter,generation);next.scope.power=context_->capture.power;
        next.authority=authority;next.selectionId=request_->proposal.requestId;next.currentPower=currentPowerGeneration();
        next.currentLifetime=g_windowsSourceModuleLifetime.load();next.acceptsChanges=true;next.frame.reset();
        const auto stamp=ymcc::windows_motion::guarded(context_->capture.binding,next.scope.binding,next.captureLease);
        const auto fence=ymcc::windows_motion::PublicationFence::pending(stamp);
        if(!fence){failure_="pending-publication-fence-not-created";return false;}
        if(next.resource){const auto found=leases_.find(next.resource);if(found==leases_.end())return false;
            auto projection=std::make_shared<WindowsPrivateConsumerProjection>();projection->source=found->second->source();projection->selected=next;
            projection->context=context_;projection->parent=parent();projection->processingOwnerAccepted=true;projection->publication=fence;
            std::atomic_store_explicit(&g_windowsPrivateConsumerProjection,std::shared_ptr<const WindowsPrivateConsumerProjection>(projection),std::memory_order_release);
        }else {
            // Closed permission is installed before removing the private route.
            std::atomic_store_explicit(&g_windowsDefaultPublicationFence,fence,std::memory_order_release);
            std::atomic_store_explicit(&g_windowsDefaultSelectedRoute,std::make_shared<const Active>(next),std::memory_order_release);
            std::atomic_store_explicit(&g_windowsPrivateConsumerProjection,std::shared_ptr<const WindowsPrivateConsumerProjection>{},std::memory_order_release);
        }
        route_=std::move(next);processingConfirmed_=false;return true;
    }
    // BUS deliberately skips its motion producer while Host lifecycle work owns
    // the downstream transaction. Keep this bounded readiness wait separate
    // from the 2s fresh-processing budget; it never grants publication.
    bool waitForProcessingOwner() {
        if(cancelled())return false;
        if(!inputHostLifecycleBusy())return true;
        if(readinessWaitUsedMs_>=readinessWaitBudgetMs_){failure_="processing-owner-busy-timeout";return false;}
        const auto started=GetTickCount64(),before=readinessWaitUsedMs_;
        const auto remaining=readinessWaitBudgetMs_-before;
        if(started>(std::numeric_limits<std::uint64_t>::max)()-remaining){failure_="processing-owner-wait-clock-overflow";return false;}
        const auto deadline=started+remaining;const auto resume=phase_;
        phase("awaiting-processing-owner");
        const auto note=[&]{const auto now=GetTickCount64();
            const auto elapsed=now>=started?(std::min)(remaining,now-started):remaining;
            readinessWaitUsedMs_=before+elapsed;
            std::lock_guard<std::mutex> lock(g_windowsSelectionMx);
            g_windowsSelectionStatus["processingOwnerWaitMs"]=readinessWaitUsedMs_;
            g_windowsSelectionStatus["processingOwnerWaitBudgetMs"]=readinessWaitBudgetMs_;
        };
        bool pumpOk=true;
        while(inputHostLifecycleBusy()&&!cancelled()&&GetTickCount64()<deadline){
            poll();
            if(GetTickCount64()>=deadline)break;
            if(!pump(deadline)){pumpOk=false;break;}
            note();
        }
        note();
        if(cancelled()){if(failure_.empty())failure_="selection-cancelled-or-parent-retired";return false;}
        if(inputHostLifecycleBusy()){if(failure_.empty())failure_="processing-owner-busy-timeout";return false;}
        if(!pumpOk){if(failure_.empty())failure_="processing-owner-wait-interrupted";return false;}
        phase(resume.c_str());return true;
    }
    bool processed(std::uint64_t since) {
        using namespace ymcc::sensors;
        phase("awaiting-actual-processing");const auto started=GetTickCount64();
        if(started>(std::numeric_limits<std::uint64_t>::max)()-2000){failure_="processing-clock-overflow";return false;}
        auto deadline=started+2000;
        const auto pendingView=std::atomic_load_explicit(&g_windowsPrivateConsumerProjection,std::memory_order_acquire);
        const auto pending=pendingView?pendingView->publication:std::atomic_load_explicit(&g_windowsDefaultPublicationFence,std::memory_order_acquire);
        while(!cancelled()){
            if(inputHostLifecycleBusy()){
                const auto paused=GetTickCount64();if(!waitForProcessingOwner())return false;
                const auto resumed=GetTickCount64();
                if(resumed<paused||deadline>(std::numeric_limits<std::uint64_t>::max)()-(resumed-paused)){
                    failure_="processing-clock-overflow";return false;}
                deadline+=resumed-paused;
            }
            if(GetTickCount64()>=deadline)break;
            poll();const auto now=GetTickCount64();const auto active=current();
            const auto cache=std::atomic_load_explicit(&g_windowsProcessingReceipt,std::memory_order_acquire);
            const auto receipt=cache?cache->read(now,currentPowerGeneration()):std::nullopt;
            if(receipt&&selection::frameCurrent(active,now,since)){
                const auto& id=receipt->identity;const auto& proof=receipt->frame;const auto& recipe=id.recipe;
                bool match=ymcc::windows_motion::sameInput(id.source,rawStamp())&&id.privateSource==bool(route_.resource)&&
                    id.captureRequest==(route_.captureAuthority?route_.captureAuthority->id:0)&&id.choiceRequest==route_.selectionId&&
                    id.profile==route_.profile&&id.key==route_.calibrationKey&&id.power==context_->capture.power&&
                    recipe.bound&&recipe.deviceClass==context_->deviceClass&&recipe.selector==context_->selector&&recipe.gyro==context_->gyro&&
                    recipe.accel==context_->accel&&recipe.digest==context_->digest&&proof.processedTick>=since&&
                    proof.gyroReceipt>=since&&proof.accelReceipt>=since&&proof.publication==pending&&
                    processing::sameId(id.metadata.gyroId,route_.pair.gyroId)&&processing::sameId(id.metadata.accelId,route_.pair.accelId)&&
                    id.metadata.gyroApi==route_.api[0]&&id.metadata.accelApi==route_.api[1]&&
                    id.metadata.gyroLegacy==(!route_.resource&&context_->metadata.gyroFamily=="sensorapi"&&context_->metadata.gyroHcLegacyFields)&&
                    id.metadata.accelLegacy==(!route_.resource&&context_->metadata.accelFamily=="sensorapi"&&context_->metadata.accelHcLegacyFields);
                const std::array<std::uint64_t,2> sequences={proof.gyroSequence,proof.accelSequence},times={proof.gyroTimestamp,proof.accelTimestamp};
                for(unsigned kind=0;kind<2;++kind){const auto& part=active.frame->values[kind];
                    match=match&&sequences[kind]<=part.sequence&&times[kind]<=part.sample.timestamp100ns;}
                if(match){const auto fence=ymcc::windows_motion::PublicationFence::committed(id.source);
                    if(!fence||cancelled())return false;
                    if(route_.resource){const auto prior=std::atomic_load_explicit(&g_windowsPrivateConsumerProjection,std::memory_order_acquire);
                        if(!prior||prior->publication!=pending)return false;auto next=std::make_shared<WindowsPrivateConsumerProjection>(*prior);next->publication=fence;
                        std::atomic_store_explicit(&g_windowsPrivateConsumerProjection,std::shared_ptr<const WindowsPrivateConsumerProjection>(next),std::memory_order_release);
                    }else std::atomic_store_explicit(&g_windowsDefaultPublicationFence,fence,std::memory_order_release);
                    if(cancelled()){fence->revoke();return false;}processingConfirmed_=true;committedProof_=receipt;return true;
                }
            }
            if(!pump(deadline))return false;
        }
        failure_=cancelled()?"selection-cancelled-or-parent-retired":"fresh-matching-processing-receipt-not-observed";return false;
    }
    bool pump(std::uint64_t deadline) {
        const auto now=GetTickCount64();if(now>=deadline||cancelled())return false;
        if(MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>((std::min)(std::uint64_t{16},deadline-now)),QS_ALLINPUT,MWMO_INPUTAVAILABLE)==WAIT_FAILED){
            failure_="selection-message-wait-failed";return false;}
        for(MSG message{};!cancelled()&&PeekMessageW(&message,nullptr,0,0,PM_REMOVE);){TranslateMessage(&message);DispatchMessageW(&message);}return !cancelled();
    }
    bool prepare(const ymcc::sensors::selection::Proposal& proposal,const Active& original,const std::string& key,
        const ymcc::sensors::selection::Authority& authority,ymcc::sensors::selection::PreparedSource& output) noexcept {
        try {phase("preparing-native-source");if(++nextResource_==0)return false;
            auto prepared=inputCapturePrepareWindowsPrivateSource(proposal,original,key,authority,nextResource_,[this]{return parent();},[this]{
                const auto now=GetTickCount64();if(now<lastNativePoll_||now-lastNativePoll_>=16)poll();return cancelled();});
            if(!prepared.source){failure_=prepared.reason;phase_=prepared.failurePhase;hresult_=prepared.hresult;
                if(prepared.evidence){json parts=json::array();for(const auto& source:prepared.evidence->sources){const auto& series=source.observations;
                    parts.push_back({{"api",source.api},{"standardSdkLayout",series.standardSdkLayout.empty()?json(nullptr):json(series.standardSdkLayout)},{"kind",source.kind==ymcc::sensors::inventory::Kind::Gyro?"gyro":"accel"},{"deviceId",source.id},
                        {"opened",series.opened},{"openHresult",series.opened||series.openError?json(series.openError):json(nullptr)},
                        {"distinctReports",series.last?json(series.distinct):json(nullptr)},{"lastReportUsable",series.lastReportUsable},
                        {"readingTransform",series.readingTransform?json(*series.readingTransform):json(nullptr)},
                        {"readingTransformHresult",series.readingTransform||series.readingTransformError?json(series.readingTransformError):json(nullptr)},
                        {"reportIntervalMs",series.intervalObserved?json(series.intervalMs):json(nullptr)},
                        {"readErrors",series.readErrors},{"rejectedNumeric",series.rejectedNumeric},{"rejectedTime",series.rejectedTime}});}
                    std::lock_guard<std::mutex> lock(g_windowsSelectionMx);g_windowsSelectionStatus["preparationEvidence"]={{"scope","candidate-raw-reports-only"},{"parts",parts}};
                }return false;}
            output=prepared.prepared;leases_.emplace(output.resource,std::move(prepared.source));g_windowsSelectionLeaseCount.store(leases_.size());return true;
        }catch(const winrt::hresult_error& error){hresult_=error.code().value;failure_="native-prepare-hresult";return false;}
        catch(...){failure_="native-prepare-exception";return false;}
    }
    void release(std::uint64_t resource)noexcept {leases_.erase(resource);g_windowsSelectionLeaseCount.store(leases_.size(),std::memory_order_release);}
    ymcc::sensors::selection::Operations operations() {
        using namespace ymcc::sensors::selection;
        Operations ops;ops.state=this;ops.now=[](void*)noexcept->std::uint64_t{return GetTickCount64();};
        ops.cancelled=[](void* p)noexcept{return static_cast<WindowsSelectionStaOwner*>(p)->cancelled();};
        ops.current=[](void* p){auto& self=*static_cast<WindowsSelectionStaOwner*>(p);auto value=self.current();
            if(self.managed_&&!self.processingConfirmed_)value.frame.reset();return value;};
        ops.prepare=[](void* p,const Proposal& proposal,const ActiveSource& original,const std::string& key,const Authority& authority,PreparedSource& output)noexcept{
            return static_cast<WindowsSelectionStaOwner*>(p)->prepare(proposal,original,key,authority,output);};
        ops.adopt=[](void* p,const Proposal&,const ActiveSource& original,const PreparedSource& prepared)noexcept{
            auto& self=*static_cast<WindowsSelectionStaOwner*>(p);Adoption result;
            try {Active target=original;target.pair=prepared.pair;target.api=prepared.authority->api;target.profile=prepared.profile;target.calibrationKey=prepared.calibrationKey;
                target.resource=prepared.resource;target.policy=TransformPolicy::ExplicitCurrentModel;target.captureAuthority=prepared.captureAuthority;target.captureLease=prepared.captureLease;
                if(!self.waitForProcessingOwner())return result;
                const auto started=GetTickCount64();if(!self.publishPending(target,original.scope.binding.bindingGeneration,prepared.authority))return result;
                result.ownershipTransferred=true;result.reservedGeneration=self.route_.scope.binding.bindingGeneration;
                result.reportedSuccess=self.processed(started);return result;
            }catch(...){self.failure_="native-adoption-exception";return result;}};
        ops.recover=[](void* p,const Proposal&,const ActiveSource& target,std::uint64_t floor,const Authority& authority)noexcept{
            auto& self=*static_cast<WindowsSelectionStaOwner*>(p);try {if(!self.waitForProcessingOwner())return false;const auto started=GetTickCount64();
                return self.publishPending(target,floor,authority)&&self.processed(started);
            }catch(...){self.failure_="native-recovery-exception";return false;}};
        ops.quarantine=[](void* p,const ActiveSource&,std::uint64_t)noexcept{static_cast<WindowsSelectionStaOwner*>(p)->revokePublication();};
        ops.release=[](void* p,std::uint64_t resource)noexcept{static_cast<WindowsSelectionStaOwner*>(p)->release(resource);};
        ops.finalize=[](void* p,const Receipt& receipt)noexcept{auto& self=*static_cast<WindowsSelectionStaOwner*>(p);
            const bool restored=receipt.outcome==Outcome::Committed||receipt.outcome==Outcome::RolledBack;
            if(!restored&&(receipt.quarantined||receipt.outcome==Outcome::RecoveryRequired||
                (receipt.outcome==Outcome::Retired&&self.route_.selectionId==receipt.requestId)))self.revokePublication();
            if(receipt.outcome==Outcome::Retired&&self.route_.selectionId==receipt.requestId&&self.route_.resource)self.release(self.route_.resource);
        };return ops;
    }
public:
    void poll()noexcept {
        lastNativePoll_=GetTickCount64();
        for(auto it=leases_.begin();it!=leases_.end();)if(!it->second->poll()){
            if(it->first==route_.resource)revokePublication();it->second->close();it=leases_.erase(it);
        }else ++it;
        g_windowsSelectionLeaseCount.store(leases_.size(),std::memory_order_release);
    }
    void reset()noexcept {
        revokePublication();leases_.clear();g_windowsSelectionLeaseCount.store(0);
        if(context_&&context_->active)context_->active->store(false,std::memory_order_release);
        if(managed_){auto generation=g_windowsChoiceCounter->activeBindingGeneration.load();
            if(generation!=(std::numeric_limits<std::uint64_t>::max)())g_windowsChoiceCounter->activeBindingGeneration.store(generation+1,std::memory_order_release);
            std::atomic_store(&g_windowsPrivateConsumerProjection,std::shared_ptr<const WindowsPrivateConsumerProjection>{});
            std::atomic_store(&g_windowsDefaultSelectedRoute,std::shared_ptr<const Active>{});
            std::atomic_store(&g_windowsDefaultPublicationFence,std::shared_ptr<const ymcc::windows_motion::PublicationFence>{});}
        managed_=processingConfirmed_=false;lastNativePoll_=0;committedProof_.reset();context_.reset();request_.reset();route_={};savedDefault_={};g_windowsSourceModuleLifetime.fetch_add(1,std::memory_order_acq_rel);
    }
    void run(const WindowsSelectionRequest& request) {
        using namespace ymcc::sensors::selection;
        request_=request;failure_.clear();hresult_.reset();readinessWaitUsedMs_=0;
        {std::lock_guard<std::mutex> lock(g_windowsSelectionMx);g_windowsSelectionStatus["processingOwnerWaitMs"]=0;g_windowsSelectionStatus["processingOwnerWaitBudgetMs"]=readinessWaitBudgetMs_;}
        {std::lock_guard<std::mutex> lock(g_windowsSelectionMx);g_windowsSelectionStatus["preparationEvidence"]=nullptr;}
        const auto context=std::atomic_load_explicit(&g_windowsSelectionWorkerContext,std::memory_order_acquire);
        if(managed_&&context&&context!=context_&&inputCaptureWindowsSelectionContextCurrent(context)){
            // A new capture/model lifetime cannot inherit the old authority.
            // Start from a closed default projection; this explicit request
            // still has to obtain a fresh transaction and processing proof.
            revokePublication();leases_.clear();g_windowsSelectionLeaseCount.store(0);context_=context;
            std::uint64_t generation=0;if(!reserve(g_windowsChoiceCounter->activeBindingGeneration.load(),generation))throw std::runtime_error("context-choice-generation-exhausted");
            route_=defaultRoute();savedDefault_=route_;processingConfirmed_=false;
            const auto pending=ymcc::windows_motion::PublicationFence::pending(rawStamp());
            if(!pending)throw std::runtime_error("replacement-context-pending-fence-failed");
            std::atomic_store(&g_windowsDefaultPublicationFence,pending);
            std::atomic_store(&g_windowsDefaultSelectedRoute,std::make_shared<const Active>(route_));
            std::atomic_store(&g_windowsPrivateConsumerProjection,std::shared_ptr<const WindowsPrivateConsumerProjection>{});
        }
        if(!managed_){context_=context;route_=defaultRoute();savedDefault_=route_;}
        Receipt result;
        if(!context||context!=context_||cancelled()){result.requestId=request.proposal.requestId;result.outcome=Outcome::Retired;result.reason="worker-context-retired";}
        else if(!waitForProcessingOwner()){
            result.requestId=request.proposal.requestId;result.outcome=cancelled()?Outcome::Retired:Outcome::Rejected;
            result.reason=failure_.empty()?"processing-owner-unavailable":failure_;
        }
        else result=request.returnDefault?transaction_.returnDefault(request.proposal.requestId,savedDefault_,operations()):transaction_.apply(request.proposal,operations());
        const bool restored=result.outcome==Outcome::Committed||result.outcome==Outcome::RolledBack;
        if(!restored&&(result.quarantined||result.outcome==Outcome::RecoveryRequired||
            (result.outcome==Outcome::Retired&&route_.selectionId==result.requestId)))revokePublication();
        // Once the transaction is settled, retain only its active capture.
        for(auto it=leases_.begin();it!=leases_.end();)if(it->first!=route_.resource)it=leases_.erase(it);else ++it;
        g_windowsSelectionLeaseCount.store(leases_.size(),std::memory_order_release);
        const auto active=current();
        std::lock_guard<std::mutex> lock(g_windowsSelectionMx);
        g_windowsSelectionStatus["requestId"]=result.requestId;g_windowsSelectionStatus["phase"]=outcomeName(result.outcome);
        g_windowsSelectionStatus["reason"]=result.reason;g_windowsSelectionStatus["failurePhase"]=failure_.empty()?json(nullptr):json(phase_);
        g_windowsSelectionStatus["failureReason"]=failure_.empty()?json(nullptr):json(failure_);g_windowsSelectionStatus["hresult"]=hresult_?json(*hresult_):json(nullptr);
        g_windowsSelectionStatus["routingGeneration"]=active.scope.binding.bindingGeneration;
        g_windowsSelectionStatus["inputProcessingCommitted"]=result.outcome==Outcome::Committed||result.outcome==Outcome::RolledBack;
        if(result.outcome==Outcome::Committed||result.outcome==Outcome::RolledBack){
            g_windowsSelectionStatus["committedChoiceRequestId"]=active.selectionId;g_windowsSelectionStatus["committedChoiceGeneration"]=active.scope.binding.bindingGeneration;
            if(committedProof_){const auto& proof=*committedProof_;g_windowsSelectionStatus["committedProcessingReceipt"]={
                {"scope","actual-processed-input;not-host-or-game-consumption"},{"captureRequestId",proof.identity.captureRequest},
                {"choiceRequestId",proof.identity.choiceRequest},{"processorEpoch",proof.identity.epoch},{"processedUptimeMs",proof.frame.processedTick},
                {"gyroSequence",proof.frame.gyroSequence},{"accelSequence",proof.frame.accelSequence},
                {"gyroReceiptUptimeMs",proof.frame.gyroReceipt},{"accelReceiptUptimeMs",proof.frame.accelReceipt}};}}
        g_windowsSelectionStatus["selected"]={{"privateSource",bool(active.resource)},{"gyroDeviceId",active.pair.gyroId},{"accelDeviceId",active.pair.accelId},
            {"profile",active.profile},{"calibrationKey",active.calibrationKey},{"captureRequestId",active.captureAuthority?active.captureAuthority->id:0},{"choiceRequestId",active.selectionId}};
        request_.reset();
    }
};
static WindowsSelectionStaOwner g_windowsSelectionOwner;
static void inputCaptureWindowsSelectionLifecycle() {
    {std::lock_guard<std::mutex> lock(g_windowsSelectionMx);
        if(g_windowsSelectionPending){g_windowsSelectionPending->cancelled->store(true);g_windowsSelectionStatus["phase"]="lifecycle-cancelled";
            g_windowsSelectionStatus["requestId"]=g_windowsSelectionPending->proposal.requestId;g_windowsSelectionPending.reset();}
        if(g_windowsSelectionActive)g_windowsSelectionActive->cancelled->store(true);g_windowsSelectionStatus["inputProcessingCommitted"]=false;}
    g_windowsSelectionOwner.reset();
}
static void inputCaptureRunWindowsSelection() {
    g_windowsSelectionOwner.poll();std::optional<WindowsSelectionRequest> request;
    {std::lock_guard<std::mutex> lock(g_windowsSelectionMx);if(!g_windowsSelectionPending)return;
        request=std::move(g_windowsSelectionPending);g_windowsSelectionPending.reset();g_windowsSelectionActive=request;g_windowsSelectionRunning=true;}
    try {g_windowsSelectionOwner.run(*request);}
    catch(...){std::lock_guard<std::mutex> lock(g_windowsSelectionMx);g_windowsSelectionStatus["phase"]="failed";
        g_windowsSelectionStatus["reason"]="native-selection-owner-exception";g_windowsSelectionStatus["inputProcessingCommitted"]=false;}
    {std::lock_guard<std::mutex> lock(g_windowsSelectionMx);g_windowsSelectionActive.reset();g_windowsSelectionRunning=false;}
}
