#pragma once
#include "virtual_target_return_cold_execution.h"
#include "virtual_target_return_pnp_absence.h"

// Included after targetReturnRuntime's real producer/normal-return definitions.
// All writes remain on g_inputHostMx and the original R3 absolute deadline.
// The continuation lives in virtual_switch_execution_return_runtime.h.
static bool inputHostNativeVisibilityHistoryPending();
static bool inputHostTargetReturnHistorySettledLocked() {
    return !ymcc::inputHost::targetReturnRuntime::state.coldHistoryPending && !inputHostNativeVisibilityHistoryPending();
}
static ymcc::targetReturn::pnpAbsence::Result inputHostTargetReturnColdAbsence(
        const std::vector<std::string>& knownIds) {
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.coldCensus){
        using namespace ymcc::targetReturn::pnpAbsence;
        const int result=g_msiNativeForegroundTestIo.coldCensus(knownIds);
        return {result==0?State::Absent:result==1?State::Present:State::Unknown,
            result==0?Reason::CompleteAbsence:result==1?Reason::ExactIdPresent:Reason::ReaderException};
    }
#endif
    return ymcc::targetReturn::pnpAbsence::verify(knownIds);
}
static bool inputHostTargetReturnColdHidden(std::vector<std::string>& hidden) {
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.coldHidden)return g_msiNativeForegroundTestIo.coldHidden(hidden);
#endif
    std::string raw;DWORD code=1;
    return hidHideRun(L"--dev-list",&raw,&code)&&code==ERROR_SUCCESS&&
        ymcc::targetReturn::coldExecution::parseHidden(raw,hidden);
}
namespace ymcc::inputHost::targetReturnRuntime {
struct ColdEnvironment {
    std::uint64_t ticket=0,power=0;
    std::string persona;
    std::uint64_t intent=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
    bool nativeWriterObserved=false,hostWriterObserved=false;
    duty::process::Probe nativeWriterProbe{},hostWriterProbe{};
    bool absenceObserved=false;
    ymcc::targetReturn::pnpAbsence::Result absenceResult;
    bool physicalAbsenceObserved=false;
    ymcc::targetReturn::pnpAbsence::Result physicalAbsenceResult;
    bool current() const {
        if(!g_inputHostMx.ownedByCurrentThread() || !ticket ||
            !g_virtualStartProtection.canContinueExecution(ticket,persona,inputHostStartProtectionNow()) ||
            currentPowerGeneration()!=power || inputHostConfiguredPersona()!=persona ||
            g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)!=intent || !contextCurrent() ||
            inputHostExitFenceRaised() || g_exitRequested.load(std::memory_order_acquire) ||
            g_inputCaptureStop.load(std::memory_order_acquire) ||
            g_inputHostProcess || g_inputHostHello || g_inputHostPrepared || g_inputHostPrepareMayHaveReachedHost ||
            g_inputHostNeutralized || g_inputHostActive || g_hidHideVisibilityJournal.active ||
            g_inputHostReleaseQuarantined || g_msiSteamDeckReleaseDebt.required() ||
            g_psEndpointOwnership.retirementRequired() || msiForegroundIsolationBorrowBusy() ||
            g_externalPriorityVirtualSuspended.load(std::memory_order_acquire) ||
            g_shortcutRecordingActive.load(std::memory_order_acquire) ||
            g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready ||
            !g_inputReady.load(std::memory_order_acquire) || !sgVirtualWriterAllowedNow() ||
            g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire))return false;
        // Native foreground has an intentional physical lease of its own. A
        // cold duty must never unhide through that current user's ownership.
        std::unique_lock<std::mutex> lease(g_msiNativeForegroundMx,std::try_to_lock);
        if(!lease.owns_lock())return false;
        if(msiColdReturnLeaseMatchesLocked(ticket,power))return msiColdReturnContextCurrentLocked();
        return !g_msiNativeForegroundLease.active && !g_msiNativeForegroundLease.cycleProcess &&
            !g_msiNativeForegroundLease.nativeRefreshWriter && !g_msiNativeForegroundLease.restorePending;
    }
    const duty::Record* record()const{return state.session?state.session->record():nullptr;}
    bool nativeDead(const duty::ProcessIdentity& value){
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
        if(g_msiNativeForegroundTestIo.coldWriterDead)return g_msiNativeForegroundTestIo.coldWriterDead(value);
#endif
        nativeWriterObserved=true;nativeWriterProbe=inputHostTargetReturnProbePhysicalWriter(value);
        return nativeWriterProbe.state==duty::process::State::Dead;
    }
    bool hostDead(const duty::ProcessIdentity& value){
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
        if(g_msiNativeForegroundTestIo.coldWriterDead)return g_msiNativeForegroundTestIo.coldWriterDead(value);
#endif
        hostWriterObserved=true;hostWriterProbe=inputHostTargetReturnProbePhysicalWriter(value);
        return hostWriterProbe.state==duty::process::State::Dead;
    }
    bool writersQuiet(){
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
        if(g_msiNativeForegroundTestIo.coldWritersQuiet)return g_msiNativeForegroundTestIo.coldWritersQuiet();
#endif
        return physicalWritersQuiescent();
    }
    bool targetAbsent(const std::vector<std::string>& ids){
        absenceObserved=true;absenceResult=inputHostTargetReturnColdAbsence(ids);return absenceResult.absent();
    }
    // Diagnostic copy of the reads the return consumer already performs.
    // No new device query, refresh authority or condition of completion.
    bool visibilityObserved=false,lastVisibilityReadbackSucceeded=false,lastHiddenCaptured=false;
    ULONGLONG visibilityObservedTick=0;
    size_t lastHiddenCount=0;
    std::vector<std::string> lastHidden;
    bool visibility(std::vector<std::string>& hidden){
        const bool ok=inputHostTargetReturnColdHidden(hidden);
        visibilityObserved=true;lastVisibilityReadbackSucceeded=ok;
        visibilityObservedTick=GetTickCount64();
        lastHiddenCount=ok?hidden.size():0;lastHiddenCaptured=false;
        try{lastHidden.clear();if(ok){
            const auto count=(std::min)(hidden.size(),size_t{16});
            lastHidden.assign(hidden.begin(),hidden.begin()+count);lastHiddenCaptured=true;
        }}catch(...){lastHidden.clear();} // Evidence allocation cannot change return authority.
        return ok;
    }
    bool pathProven(const std::string& root,const std::string& path){
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
        if(g_msiNativeForegroundTestIo.coldPathProven)return g_msiNativeForegroundTestIo.coldPathProven(root,path);
#endif
        const auto ancestry=inputHostTargetReturnVerifyPhysicalPath(root,path);
        if(ancestry.proven())return true;
        // A reboot/mode change may remove the old devnode while its exact
        // owned HidHide configuration key remains. This operation only removes
        // that recorded key; it does not cycle/remove/reconfigure a device.
        // CM failure alone is insufficient: require a COMPLETE present-device
        // census with this exact old path absent as well as all virtual targets.
        if(ancestry.reason!=duty::physicalPath::Reason::TargetLocateFailed ||
            ancestry.configurationReturn!=CR_NO_SUCH_DEVNODE)return false;
        physicalAbsenceObserved=true;
        physicalAbsenceResult=inputHostTargetReturnColdAbsence({path});
        return physicalAbsenceResult.absent();
    }
    bool unhide(const std::string& path){
        if(!current())return false;
        // Both callers already observed this key hidden and proved exact
        // ancestry. Remember the real root-return duty BEFORE issuing unhide;
        // ordinary producer reservations are promoted only for this observed root.
        if(persona=="disabled" && nativeForegroundProfile()==ymcc::nativeForeground::Profile::Gpd){
            const auto* live=record();if(!live)return false;
            std::optional<duty::Origin> observedOrigin;
            if(live->machineBinding.rfind("windows-machine-guid:",0)==0)
                for(const auto& group:live->obligations)if(group.origin.cliWriterProtocol==1 &&
                    duty::detail::fold(group.origin.physicalRoot)==duty::detail::fold(path) &&
                    std::any_of(group.paths.begin(),group.paths.end(),[&](const auto& debt){
                        return duty::detail::fold(debt.instanceId)==duty::detail::fold(path);}))observedOrigin=group.origin;
            if(observedOrigin){
                {
                std::lock_guard lease(g_msiNativeForegroundMx);auto& owner=g_msiNativeForegroundLease;
                if(owner.active && !msiColdReturnLeaseMatchesLocked(ticket,power))return false;
                auto& receipt=owner.coldReturnReceipt;
                if(receipt.ticket==ticket){
                    // A completed same-OFF settings writer may refresh only the
                    // observation intent. Never rebind an active old helper.
                    if(receipt.power!=power || duty::detail::fold(receipt.root)!=duty::detail::fold(path) ||
                        (receipt.observedOrigin && !duty::sameOrigin(*receipt.observedOrigin,*observedOrigin)))return false;
                    if(receipt.intent!=intent){
                        if(owner.active || g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) ||
                            g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)!=intent)return false;
                        receipt.intent=intent;
                    }
                    receipt.observedRootHidden=true;
                }else receipt={ticket,power,intent,path,false,false,true};
                receipt.observedOrigin=*observedOrigin;
                }
                // Commit the existing hide-duty stage before removing its last
                // external witness. An interrupted process must not lose it.
                if(!rememberObservedColdRoot(path)){rootProofCommitFailed=true;return false;}
                if(!current())return false;
            }
        }
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
        if(g_msiNativeForegroundTestIo.coldUnhide)return g_msiNativeForegroundTestIo.coldUnhide(path);
#endif
        DWORD code=1;return hidHideRun(L"--dev-unhide \""+U2W(path)+L"\"",nullptr,&code)&&code==ERROR_SUCCESS;
    }
    bool refreshWaiting=false,refreshAttempted=false,rootProofCommitFailed=false;
    bool rememberObservedColdRoot(const std::string& root){
        const auto* live=record();
        if(!current() || !live || !state.session || !state.session->ready() ||
            live->machineBinding.rfind("windows-machine-guid:",0)!=0)return false;
        std::optional<duty::Origin> observedOrigin;
        {
            std::lock_guard lease(g_msiNativeForegroundMx);const auto& receipt=g_msiNativeForegroundLease.coldReturnReceipt;
            if(!receipt.observedRootHidden || duty::detail::fold(receipt.root)!=duty::detail::fold(root))return false;
            observedOrigin=receipt.observedOrigin;
        }
        if(!observedOrigin)return false;
        for(const auto& group:live->obligations)if(group.origin.cliWriterProtocol==1 &&
            duty::sameOrigin(group.origin,*observedOrigin) &&
            duty::detail::fold(group.origin.physicalRoot)==duty::detail::fold(root))
            for(const auto& debt:group.paths)if(duty::detail::fold(debt.instanceId)==duty::detail::fold(root)){
                if(debt.stage==duty::DispatchStage::MayHaveIssued)return true;
                const auto origin=group.origin;const auto revision=live->revision;
                return state.session->markMayHaveIssued(revision,origin,root);
            }
        return false;
    }
    int pollOwnColdRefresh(){
        std::lock_guard lease(g_msiNativeForegroundMx);
        auto& owner=g_msiNativeForegroundLease;
        if(!msiColdReturnLeaseMatchesLocked(ticket,power))return 0;
        refreshAttempted=owner.coldReturnReceipt.spent;
        if(!msiColdReturnContextCurrentLocked())return 2;
        if(g_msiNativeForegroundCyclePending)(void)msiNativeForegroundPollCycleLocked();
        if(g_msiNativeForegroundCyclePending || owner.cycleProcess || owner.nativeRefreshWriter){refreshWaiting=true;return 1;}
        if(msiColdReturnPreMutationRejectionProvenLocked()){
            (void)msiColdReturnCleanupLocked("cold-pre-mutation-rejection");
            if(owner.coldReturnReceipt.preMutationResumes<1){refreshWaiting=true;return 1;}
        }
        const bool accepted=owner.cycleIssued && owner.cycleAccepted && owner.helperExitCode==0;
        owner.coldReturnReceipt.accepted=accepted;
        return accepted?0:2;
    }
    bool prepareColdRoot(const duty::Origin& origin){
        if(persona!="disabled")return true;
        if(rootProofCommitFailed)return false;
        const auto profile=nativeForegroundProfile();
        if(profile!=ymcc::nativeForeground::Profile::Gpd && profile!=ymcc::nativeForeground::Profile::Unsupported)return true;
        if(profile==ymcc::nativeForeground::Profile::Unsupported && g_machineIdentity.resolved &&
            g_machineIdentity.family!=YmccFamily::Gpd)return true;
        const auto* live=record();if(!live)return false;
        bool refreshUnreturned=false;
        {
            std::lock_guard lease(g_msiNativeForegroundMx);const auto& receipt=g_msiNativeForegroundLease.coldReturnReceipt;
            // Root-return duty survives a request or power generation. Only
            // resume authority below remains bound to the original period.
            refreshUnreturned=receipt.spent && !receipt.accepted &&
                duty::detail::fold(receipt.root)==duty::detail::fold(origin.physicalRoot);
        }
        // Real producers reserve paths without marking MayHaveIssued. The
        // exact-root hidden observation captured by unhide is a same-ticket
        // in-memory duty; retain historical MayHaveIssued support as well.
        // Neither source changes the generic Record protocol.
        std::string selected;bool durableRootDuty=false;
        for(const auto& group:live->obligations)for(const auto& debt:group.paths)
            if(debt.stage==duty::DispatchStage::MayHaveIssued &&
                ymcc::nativeForeground::rootCandidate(ymcc::nativeForeground::Profile::Gpd,group.origin.physicalRoot) &&
                duty::detail::fold(debt.instanceId)==duty::detail::fold(group.origin.physicalRoot)){
                if(!selected.empty() && duty::detail::fold(selected)!=duty::detail::fold(group.origin.physicalRoot))return false;
                selected=group.origin.physicalRoot;durableRootDuty=true;
            }
        {
            std::lock_guard lease(g_msiNativeForegroundMx);
            const auto& receipt=g_msiNativeForegroundLease.coldReturnReceipt;
            if(receipt.observedRootHidden &&
                ((receipt.ticket==ticket && receipt.power==power && (receipt.intent==intent || receipt.spent)) ||
                    (receipt.spent && !receipt.accepted))){
                if(!selected.empty() && duty::detail::fold(selected)!=duty::detail::fold(receipt.root))return false;
                selected=receipt.root; // survives unhide/readback=false and another cold pass
            }
        }
        bool currentRefreshAccepted=false;
        {
            std::lock_guard lease(g_msiNativeForegroundMx);const auto& receipt=g_msiNativeForegroundLease.coldReturnReceipt;
            currentRefreshAccepted=receipt.ticket==ticket && receipt.power==power && receipt.accepted &&
                duty::detail::fold(receipt.root)==duty::detail::fold(selected);
        }
        if(!selected.empty() && !durableRootDuty && !currentRefreshAccepted){
            if(!rememberObservedColdRoot(selected)){rootProofCommitFailed=true;return false;}
            durableRootDuty=true;live=record();if(!live)return false;
        }
        refreshUnreturned=refreshUnreturned || (durableRootDuty && !currentRefreshAccepted &&
            duty::detail::fold(selected)==duty::detail::fold(origin.physicalRoot));
        if(nativeForegroundProfile()!=ymcc::nativeForeground::Profile::Gpd)return !refreshUnreturned;
        if(selected.empty())return true; // Reserved AND root never observed hidden: no device refresh
        if(duty::detail::fold(selected)!=duty::detail::fold(origin.physicalRoot))return true; // another root's batch
        // A different already-resolved selected source only opts OUT of this
        // optional refresh. It grants neither cleanup authority nor a new root.
        // Unknown cache state never counts as absence or successful refresh.
        {
            std::lock_guard identity(g_hidHidePhysicalIdentityMx);
            const auto& selectedNow=g_hidHidePhysicalIdentity;
            if(selectedNow.resolved && !selectedNow.baseContainerDeviceInstanceId.empty() &&
                duty::detail::fold(selectedNow.baseContainerDeviceInstanceId)!=duty::detail::fold(selected))return !refreshUnreturned;
        }
        const auto groups=live->obligations;
        // All same-root origins must be retired before this batch can issue IO.
        for(const auto& group:groups)if(duty::detail::fold(group.origin.physicalRoot)==duty::detail::fold(selected)){
            if(!current() || group.origin.cliWriterProtocol!=1 ||
                !nativeDead(group.origin.parent) || !hostDead(group.origin.host) || !writersQuiet() || !targetAbsent({}))return false;
        }
        // A COMPLETE census excluding this exact old root permits the old
        // exact-key return to finish. It never authorizes finding another root.
        const auto rootPresence=inputHostTargetReturnColdAbsence({selected});
        if(!current())return false;
        if(rootPresence.absent()){
            std::lock_guard lease(g_msiNativeForegroundMx);const auto& receipt=g_msiNativeForegroundLease.coldReturnReceipt;
            if((durableRootDuty && !currentRefreshAccepted) ||
                (receipt.spent && !receipt.accepted && duty::detail::fold(receipt.root)==duty::detail::fold(selected))){
                // Our attempted refresh must not turn a transient absence into
                // successful restoration. Wait only inside this original R3.
                refreshAttempted=receipt.spent;refreshWaiting=durableRootDuty || (receipt.preMutationRejected && receipt.preMutationResumes<1);
                return false;
            }
            return true; // no attempted refresh: exact absent old key only, external unknown
        }
        if(rootPresence.state!=ymcc::targetReturn::pnpAbsence::State::Present ||
            rootPresence.reason!=ymcc::targetReturn::pnpAbsence::Reason::ExactIdPresent)return false;
        // Generic run is path-at-a-time. Its first consume reaches this throat
        // before other origins' paths are returned; finish ONLY this same-root
        // batch first. Re-read hidden state and issue only still-hidden keys.
        std::vector<std::string> hidden;if(!visibility(hidden))return false;
        for(const auto& group:groups)if(duty::detail::fold(group.origin.physicalRoot)==duty::detail::fold(selected))
            for(const auto& debt:group.paths)if(ymcc::targetReturn::coldExecution::containsPath(hidden,debt.instanceId)){
                if(!current() || !pathProven(selected,debt.instanceId) || !writersQuiet())return false;
                (void)unhide(debt.instanceId);
                if(!current() || !visibility(hidden) || ymcc::targetReturn::coldExecution::containsPath(hidden,debt.instanceId))return false;
            }
        if(!current() || !writersQuiet() || !targetAbsent({}) || !visibility(hidden))return false;
        for(const auto& group:groups)if(duty::detail::fold(group.origin.physicalRoot)==duty::detail::fold(selected))
            for(const auto& debt:group.paths)if(ymcc::targetReturn::coldExecution::containsPath(hidden,debt.instanceId))return false;
        std::lock_guard lease(g_msiNativeForegroundMx);
        const int result=msiColdReturnCycleStepLocked(ticket,power,
            intent,g_inputHostSwitchRecoveryStartDeadline,selected);
        refreshWaiting=result==1;refreshAttempted=g_msiNativeForegroundLease.coldReturnReceipt.spent;
        return result==0;
    }
    bool consumePath(const duty::Origin& origin,const std::string& path){
        if(!current() || !state.session || !state.session->ready() || !prepareColdRoot(origin) || !current())return false;
        if(!state.session->consumePath(state.session->record()->revision,origin,path))return false;
        const auto* live=record();
        const bool rootRemaining=live && std::any_of(live->obligations.begin(),live->obligations.end(),[&](const auto& group){
            return duty::detail::fold(group.origin.physicalRoot)==duty::detail::fold(origin.physicalRoot) && !group.paths.empty();});
        if(!rootRemaining){
            std::lock_guard lease(g_msiNativeForegroundMx);
            if(msiColdReturnLeaseMatchesLocked(ticket,power) &&
                duty::detail::fold(g_msiNativeForegroundLease.root)==duty::detail::fold(origin.physicalRoot))
                msiNativeForegroundClearLeaseLocked(); // terminal helper/Job already retired above
        }
        return true;
    }
    bool consumeOrigin(const duty::Origin& origin){
        return current()&&state.session&&state.session->ready()&&
            state.session->consumeOrigin(state.session->record()->revision,origin);
    }
};
}
// VC-MSI-COLD-ENTRY-20261024-R1: queue only the original Elite Start so its
// cold-return consumer can run before source discovery. No BUS IO or admission.
static bool inputHostTargetReturnMayQueueMsiColdStart() {
    if(!msiClawKnownBoardAuthority())return false;
    std::unique_lock<InputHostMutex> host(g_inputHostMx,std::try_to_lock);
    if(!host.owns_lock() || g_inputHostStartBlocked ||
        g_inputHostFaultHoldsStart.load(std::memory_order_acquire) ||
        g_inputHostSwitchExecutionReturnPending.load(std::memory_order_acquire) ||
        g_inputHostSwitchRecoveryPending.load(std::memory_order_acquire))return false;
    using namespace ymcc::inputHost::targetReturnRuntime;
    if(!state.coldHistoryPending)return false;
    const auto* record=state.session?state.session->record():nullptr;
    if(!record || record->obligations.empty() ||
        record->machineBinding.rfind("windows-machine-guid:",0)!=0 ||
        !std::all_of(record->obligations.begin(),record->obligations.end(),
            [](const auto& item){return item.origin.cliWriterProtocol==1;}))return false;
    const auto request=g_virtualStartProtection.view(inputHostStartProtectionNow());
    // Bootstrap only the saved Elite startup before this process has any R3
    // request. Start, not BUS, consumes the first ticket and its 30s budget.
    if(!request.requestId && request.phase=="idle")
        return g_startupInputState.load(std::memory_order_acquire)==ymcc::StartupInputState::Waiting &&
            g_realStickTest.enabled && g_realStickTest.virtualTargetEnabled &&
            inputHostConfiguredPersona()=="elite" &&
            !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire);
    if(!request.requestId || !request.locked || request.phase!="pending" ||
        request.persona!="elite" || !request.remainingMs ||
        !g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled ||
        inputHostConfiguredPersona()!=request.persona ||
        g_inputHostVirtualIntentWriters.load(std::memory_order_acquire))return false;
    // A pending continuation owns its three existing observations. Never turn
    // retained history into another Start cycle, ticket, fallback or deadline.
    const ColdEnvironment environment{request.requestId,currentPowerGeneration(),request.persona};
    return environment.current();
}

static ymcc::targetReturn::coldExecution::Result inputHostTargetReturnColdPassLocked(
        std::uint64_t ticket,std::uint64_t power,const std::string& persona) {
    using namespace ymcc::inputHost::targetReturnRuntime;
    using namespace ymcc::targetReturn::coldExecution;
    if(!g_inputHostMx.ownedByCurrentThread())return {ymcc::targetReturn::coldExecution::State::Deferred,Reason::ContextRetired};
    // Loading history is read-only with respect to the device. A clean policy
    // return/idempotent Start needs no cold work and may have no active player
    // period. Require the original R3 period only when historical duties exist.
    if(!open())return {ymcc::targetReturn::coldExecution::State::Deferred,Reason::StoreUnavailable};
    if(inputHostNativeVisibilityHistoryPending()){
        std::lock_guard leaseLock(g_msiNativeForegroundMx);
        if(!inputHostNativeVisibilityColdReturnLocked("original-cold-return-continuation"))
            return {ymcc::targetReturn::coldExecution::State::Deferred,Reason::VisibilityUnproven};
    }
    if(!state.coldHistoryPending)
        return {ymcc::targetReturn::coldExecution::State::Clean,Reason::Complete};
    const auto clock=inputHostStartProtectionNow();
    const auto view=g_virtualStartProtection.view(clock);
    const auto oldDeadline=g_inputHostSwitchRecoveryStartDeadline;
    const auto oldRequest=g_inputHostSwitchRecoveryStartRequestId,oldPower=g_inputHostSwitchRecoveryStartPower;
    struct Restore {std::uint64_t deadline,request,power;~Restore(){g_inputHostSwitchRecoveryStartDeadline=deadline;
        g_inputHostSwitchRecoveryStartRequestId=request;g_inputHostSwitchRecoveryStartPower=power;}}
        restore{oldDeadline,oldRequest,oldPower};
    if(view.requestId!=ticket||!view.locked||view.persona!=persona||!view.remainingMs||
        view.remainingMs>(std::numeric_limits<std::uint64_t>::max)()-clock)
        return {ymcc::targetReturn::coldExecution::State::Deferred,Reason::ContextRetired};
    const auto original=clock+view.remainingMs;
    g_inputHostSwitchRecoveryStartDeadline=oldDeadline&&oldDeadline<original?oldDeadline:original;
    g_inputHostSwitchRecoveryStartRequestId=ticket;g_inputHostSwitchRecoveryStartPower=power;
    ColdEnvironment environment{ticket,power,persona};
    std::vector<std::string> diagnosticOwnedRoots;
    bool diagnosticRootsComplete=true;
    try{if(const auto* record=environment.record())for(const auto& group:record->obligations){
        const auto& root=group.origin.physicalRoot;
        if(!root.empty() && std::find(diagnosticOwnedRoots.begin(),diagnosticOwnedRoots.end(),root)==diagnosticOwnedRoots.end())
            if(diagnosticOwnedRoots.size()<16)diagnosticOwnedRoots.push_back(root);else diagnosticRootsComplete=false;
    }}catch(...){diagnosticRootsComplete=false;diagnosticOwnedRoots.clear();}
    // Do not re-enter any generic unhide/readback while our one helper is live.
    const int ownRefresh=environment.pollOwnColdRefresh();
    auto result=ownRefresh?Result{ymcc::targetReturn::coldExecution::State::Deferred,
        ownRefresh==1?Reason::ContextRetired:Reason::PathCommitFailed}:run(environment);
    // Waiting on the same helper is not another failed cold observation.
    if(environment.refreshWaiting)result.reason=Reason::ContextRetired;
    const auto boundedPaths=[](const std::vector<std::string>& paths){
        json out=json::array();for(size_t i=0;i<paths.size() && i<16;++i)out.push_back(paths[i]);return out;
    };
    if(result.clean())state.coldHistoryPending=false;
    const auto writerProof=[](bool observed,const duty::process::Probe& probe)->json {
        if(!observed)return {{"observed",false}};
        return {{"observed",true},{"state",duty::process::stateName(probe.state)},
            {"reason",duty::process::reasonName(probe.reason)},{"win32Error",probe.win32Error},
            {"directWin32Error",probe.directWin32Error},{"censusAttempts",probe.censusAttempts},
            {"censusBytes",probe.censusBytes},{"censusRows",probe.censusRows},{"censusNtStatus",probe.censusNtStatus}};
    };
    try{appendNativeLifecycleLog("input-host-cold-return",{
        {"lifecycleVersion",ymcc::VirtualStartProtection::lifecycleVersion},{"requestId",ticket},
        {"powerGeneration",power},{"persona",persona},{"stage",result.clean()?"execution-complete":"execution-deferred"},
        {"reason",reasonName(result.reason)},{"originKey",result.originKey},{"pathInstanceId",result.pathInstanceId},
        {"writerIdentityPolicy","VC-COLD-HOST-IDENTITY-20261026-R1"},
        {"nativeWriterProof",writerProof(environment.nativeWriterObserved,environment.nativeWriterProbe)},
        {"hostWriterProof",writerProof(environment.hostWriterObserved,environment.hostWriterProbe)},
        {"targetAbsenceObserved",environment.absenceObserved},
        {"targetAbsenceState",environment.absenceObserved?ymcc::targetReturn::pnpAbsence::stateName(environment.absenceResult.state):"unobserved"},
        {"targetAbsenceReason",environment.absenceObserved?ymcc::targetReturn::pnpAbsence::reasonName(environment.absenceResult.reason):"unobserved"},
        {"targetAbsenceScannedNodes",environment.absenceResult.scannedNodes},
        {"targetAbsenceWin32Error",environment.absenceResult.win32Error},
        {"targetAbsenceConfigurationReturn",environment.absenceResult.configurationReturn},
        {"targetAbsenceFailedInstance",environment.absenceResult.failedInstanceId},
        {"targetAbsenceDeviceIdChars",environment.absenceResult.deviceIdChars},
        {"targetAbsenceDeviceIdHead",environment.absenceResult.deviceIdHead},
        {"targetAbsenceDeviceIdTail",environment.absenceResult.deviceIdTail},
        {"targetAbsenceHardwareIdsBytes",environment.absenceResult.hardwareIdsBytes},
        {"targetAbsenceHardwareIdsType",environment.absenceResult.hardwareIdsType},
        {"targetAbsenceHardwareIdsHead",environment.absenceResult.hardwareIdsHead},
        {"targetAbsenceHardwareIdsTail",environment.absenceResult.hardwareIdsTail},
        {"physicalPathAbsenceObserved",environment.physicalAbsenceObserved},
        {"physicalPathAbsenceState",environment.physicalAbsenceObserved?ymcc::targetReturn::pnpAbsence::stateName(environment.physicalAbsenceResult.state):"unobserved"},
        {"physicalPathAbsenceReason",environment.physicalAbsenceObserved?ymcc::targetReturn::pnpAbsence::reasonName(environment.physicalAbsenceResult.reason):"unobserved"},
        {"unhideAttempts",result.unhideAttempts},
        {"configurationObservation",{
            {"scope","last-existing-dev-list-read;not-new-export-query-or-external-delivery-proof"},
            {"observed",environment.visibilityObserved},
            {"readbackSucceeded",environment.lastVisibilityReadbackSucceeded},
            {"observedTick",environment.visibilityObservedTick},
            {"hiddenCount",environment.lastVisibilityReadbackSucceeded?json(environment.lastHiddenCount):json(nullptr)},
            {"hiddenPaths",environment.lastHiddenCaptured?boundedPaths(environment.lastHidden):json(nullptr)},
            {"hiddenPathsTruncated",environment.lastHiddenCount>16},
            {"ownedRootsBeforeReturn",boundedPaths(diagnosticOwnedRoots)},
            {"ownedRootsCaptureComplete",diagnosticRootsComplete},
            {"deviceRefreshAttemptedByColdPass",environment.refreshAttempted},{"sameRefreshHelperPending",environment.refreshWaiting},{"externalControllerReopened",nullptr}}},
        {"settledPaths",result.settledPaths},{"settledOrigins",result.settledOrigins},
        {"originsRemaining",state.session&&state.session->record()?state.session->record()->obligations.size():0},
        {"gameInputVerified",false},{"fallbackAttempt",false},{"deadlineRenewed",false}});}catch(...){}
    // Old history must return before native foreground can borrow the source.
    // Replay the existing UI intent only on the clean edge and still-current
    // original OFF. This queues existing work; no new owner/timer/device IO.
    if(result.clean() && persona=="disabled" && environment.current())
        msiNativeForegroundUiEvent(true);
    return result;
}
#include "native_visibility_cold_runtime.h"

// Bootstrap loads provenance before the input BUS can attempt a target. It
// enqueues only; the existing lifecycle worker owns any later device operation.
static void inputHostTargetReturnQueueColdStartup() {
    using namespace ymcc::inputHost::targetReturnRuntime;
    std::lock_guard<InputHostMutex> lock(g_inputHostMx);
    (void)open();
    if(state.coldHistoryPending || nativeForegroundProfile()==ymcc::nativeForeground::Profile::Gpd)
        inputHostQueueLifecycleWork("startup-owned-return");
}
