#pragma once
// OneX ordinary Start only. All hardware work stays in the existing serialized
// Host owner and R3 ticket. This does not grant native-OFF isolation, change OEM
// protocols or target preferences, or borrow GPD/ROG recovery authority.
// VC-ONEX-START-20261105-R1 is a T1 sub-step of VC-LIFECYCLE-20261020-R3:
// original ticket -> HELLO + owned hide -> prove exact selected owned root/slot ->
// one possible cycle dispatch -> same-root discovery + fresh canonical sample ->
// rehide only that returned source -> PREPARE in the original Start caller.
// All pipe stages share the original absolute cutoff. Unknown cycle receipts
// never replay. Configuration/SDK success is not Steam single-device proof.
// Unselected/unproven roots retain the generic path. Standard GAME metadata
// alone never authorizes refresh; current selection/root/slot ownership does.
// No new owner, worker, recovery attempt, native-OFF profile or preference.
// 093203 follow-up: query USB connector topology with zero desired access,
// negotiate ActualLength, and retain bounded per-hop evidence. Unselected/foreign
// roots remain untouched; our exact carried hidden pair is not third-party ownership.
// 112441/HC f2e58468 ControllerManager.SetTargetController -> Hide(true):
// USB connector classification is diagnostic, not selected-source refresh authority.
// Exact selected source/root/location, journal ownership and current epochs still
// guard the single dispatch and same-root fresh read. No ancestor/all-device reset.
// Source-pending motion/touch targets get one bounded pre-hide observation in
// the same original ticket; absence still retains the generic source-free path.
static bool externalPrioritySlot(const HidHidePhysicalIdentity&,unsigned&,unsigned*,uint64_t*);
static bool ymStartAdmitSource();
static std::vector<std::wstring> inputHostOneXLocations(const std::string& root) {return msiForegroundIsolationLocations(root);}
static int externalPriorityUsbPortKindWithEvidence(const std::string&,json*,DWORD);
static thread_local json t_inputHostOneXPortEvidence=json::object();
static int inputHostOneXPortKind(const std::string& root) {return externalPriorityUsbPortKindWithEvidence(root,&t_inputHostOneXPortEvidence,0);}
static bool inputHostOneXSourceSlot(const HidHidePhysicalIdentity& source,unsigned& slot) {return externalPrioritySlot(source,slot,nullptr,nullptr);}
static bool inputHostOneXRootStarted(const std::string& root) {
    auto wide=U2W(root);DEVINST node=0;ULONG status=0,problem=0;
    return CM_Locate_DevNodeW(&node,wide.data(),CM_LOCATE_DEVNODE_NORMAL)==CR_SUCCESS &&
        CM_Get_DevNode_Status(&status,&problem,node,0)==CR_SUCCESS &&
        (status&DN_STARTED) && !(status&DN_HAS_PROBLEM) && problem==0;
}
static bool inputHostOneXResolveSources(std::vector<HidHidePhysicalIdentity>& sources,bool* complete) {return hidHideResolvePhysicalIdentities(sources,complete);}
// Retain the common resolver's ungrouped children only for pre-cycle cache
// alignment. The default root-preferred source view cannot prove child uniqueness.
static bool inputHostOneXResolveSelectedChildren(std::vector<HidHidePhysicalIdentity>& children,bool* complete) {
    std::vector<HidHidePhysicalIdentity> roots;
    if(!hidHideResolvePhysicalIdentities(roots,complete,&children) || roots.size()!=1)return false;
    std::sort(children.begin(),children.end(),[](const auto& a,const auto& b){return hidHideUpper(a.deviceInstanceId)<hidHideUpper(b.deviceInstanceId);});
    children.erase(std::unique(children.begin(),children.end(),[](const auto& a,const auto& b){
        return hidHideUpper(a.deviceInstanceId)==hidHideUpper(b.deviceInstanceId) && a.physicalIdentity==b.physicalIdentity &&
            hidHideUpper(a.sourcePath)==hidHideUpper(b.sourcePath) && hidHideUpper(a.baseContainerDevicePath)==hidHideUpper(b.baseContainerDevicePath);
    }),children.end());
    return true;
}
static bool inputHostOneXVisibility(HidHideVisibilitySnapshot& snapshot) {return hidHideCaptureVisibilitySnapshot(snapshot);}
// Immutable library classification does not grant device authority. The exact
// root below must agree with this resolved physical GAME identity; later native
// current-source, owned-journal, location and reverse-slot checks still apply.
static bool inputHostOneXGameSource(const ymcc::capabilities::onexVirtual::Binding& binding,const HidHidePhysicalIdentity& source) {
    if(!source.resolved || source.physicalIdentity.empty() ||
       !ymcc::capabilities::onexVirtual::standardGameCollection(binding,source.usagePage,source.usage) ||
       !source.vendorId || !source.productId || hidHideIsVirtualIdentity(source.deviceInstanceId,source.baseContainerDeviceInstanceId))return false;
    char prefix[32]{};
    const int count=std::snprintf(prefix,sizeof(prefix),"USB\\VID_%04X&PID_%04X\\",
        static_cast<unsigned>(source.vendorId),static_cast<unsigned>(source.productId));
    const auto root=hidHideUpper(source.baseContainerDeviceInstanceId);
    return count>0 && static_cast<size_t>(count)<sizeof(prefix) && root.rfind(prefix,0)==0 &&
        root.size()>static_cast<size_t>(count) && root.size()<512 && root.find('\\',static_cast<size_t>(count))==std::string::npos;
}
// The original reverse binder is stable during its call. Surround it with
// monotonic epochs as well, so that proof and the later canonical sample must
// belong to the same interval rather than merely using the same slot number.
struct OneXSourceSlotProof {unsigned slot=4,topology=0,identity=0;uint64_t power=0;};
static bool inputHostOneXBindSourceSlot(const HidHidePhysicalIdentity& source,OneXSourceSlotProof& proof) {
    const auto topology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
    const auto identity=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire);
    const auto power=currentPowerGeneration();unsigned slot=4;
    if(!inputHostOneXSourceSlot(source,slot) || slot>=4 || topology!=g_xinputTopologyEpoch.load(std::memory_order_acquire) ||
       identity!=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire) || power!=currentPowerGeneration())return false;
    proof={slot,topology,identity,power};return true;
}
struct OneXVirtualStartEpisode {
    ymcc::capabilities::onexVirtual::Binding binding;
    std::string deviceClass,persona,runId,root,sourceIdentity,sourceInstance;
    std::vector<std::wstring> locations;
    uint64_t ticket=0,deadline=0,power=0,epoch=0,intent=0,baselineSequence=0;
    unsigned topology=0,identity=0,slot=4;
    HANDLE host=nullptr;
    bool issueReserved=false,cycleDispatchActive=false,refreshing=false;
    uint64_t returnedTick=0;
    unsigned freshSlot=4,freshTopology=0,freshIdentity=0;
    bool rehideUnissuedRefusal=false;
    HidHidePhysicalIdentity freshSource;
};
static bool inputHostOneXStartCurrent() {
    const auto* e=t_inputHostOneXStartWrite;if(!e)return true;
    const auto now=inputHostStartProtectionNow();const auto view=g_virtualStartProtection.view(now);
    if(!g_inputHostMx.ownedByCurrentThread() || !e->ticket || !view.locked || !view.remainingMs ||
       view.requestId!=e->ticket || (view.phase!="pending" && view.phase!="verifying") || now>=e->deadline ||
       view.persona!=e->persona || inputHostConfiguredPersona()!=e->persona ||
       !g_machineIdentity.resolved || g_machineIdentity.family!=YmccFamily::OneXPlayer ||
       g_machineIdentity.deviceClass!=e->deviceClass ||
       !ymcc::capabilities::onexVirtual::recognized(e->binding,e->deviceClass) ||
       !g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled ||
       g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) ||
       g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)!=e->intent ||
       g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready || currentPowerGeneration()!=e->power ||
       g_inputHostBoundPowerGeneration!=e->power || g_inputHostPowerAdmissionRevoked || !hardwareWriteAllowed() ||
       inputHostExitFenceRaised() || g_exitRequested || g_inputCaptureStop ||
       g_externalPriorityVirtualSuspended || g_shortcutRecordingActive || physicalReleaseWindowActive() ||
       g_inputHostProcess!=e->host || !e->host || g_inputHostEpoch!=e->epoch || g_inputHostRunId!=e->runId ||
       !g_inputHostHello || g_inputHostPrepared || g_inputHostPrepareMayHaveReachedHost || g_inputHostActive ||
       g_inputHostTransportFault || g_inputHostPipeRetired || g_inputHostReleaseQuarantined)return false;
    const auto& j=g_hidHideVisibilityJournal;
    return j.active && j.transactionId==e->runId && j.physicalSources.size()==1 &&
        inputHostOneXGameSource(e->binding,j.physical) &&
        hidHideUpper(j.physical.baseContainerDeviceInstanceId)==e->root &&
        hidHideUpper(j.physicalSources.front().baseContainerDeviceInstanceId)==e->root &&
        hidHideContainsPath(j.hiddenByUs,j.physical.baseContainerDeviceInstanceId) &&
        hidHideContainsPath(j.hiddenByUs,j.physical.deviceInstanceId) &&
        (e->issueReserved || (j.physical.physicalIdentity==e->sourceIdentity &&
            hidHideUpper(j.physical.deviceInstanceId)==e->sourceInstance));
}
static uint64_t inputHostOneXStartDeadline() {return t_inputHostOneXStartWrite?t_inputHostOneXStartWrite->deadline:0;}
static bool inputHostOneXSameReturnedSource(const OneXVirtualStartEpisode& e,const HidHidePhysicalIdentity& p) {
    return inputHostOneXGameSource(e.binding,p) && hidHideUpper(p.baseContainerDeviceInstanceId)==e.root &&
        p.physicalIdentity==e.freshSource.physicalIdentity &&
        hidHideUpper(p.deviceInstanceId)==hidHideUpper(e.freshSource.deviceInstanceId);
}
// Caller holds the canonical sampling and physical-identity locks. Matching a
// slot number is insufficient if a stale journal belongs to another root.
static bool inputHostOneXOriginalSelectedSource(const OneXVirtualStartEpisode& e,const HidHidePhysicalIdentity& selected) {
    return inputHostOneXGameSource(e.binding,selected) && selected.physicalIdentity==e.sourceIdentity &&
        hidHideUpper(selected.baseContainerDeviceInstanceId)==e.root && hidHideUpper(selected.deviceInstanceId)==e.sourceInstance;
}
static bool inputHostOneXFreshSample(const OneXVirtualStartEpisode& e,const InputCaptureGamepadSnapshot& sample,unsigned slot) {
    const auto now=GetTickCount64();
    return slot<4 && g_xinputPhysicalSourceLocked && g_xinputPrimarySlot==slot &&
        g_xinputPhysicalSourceMask==(1u<<slot) && xinputNonVirtualConnectedMask()==(1u<<slot) &&
        !g_xinputSlotUnknownMask && g_xinputPreTargetBoundaryScanned &&
        g_xinputBoundaryScanEpoch.load()==g_xinputTopologyEpoch.load() &&
        g_xinputTopologyEpoch.load()==e.freshTopology && g_xinputSlotIdentityEpoch.load()==e.freshIdentity &&
        sample.live && sample.readContextStable && sample.primarySlot==slot &&
        sample.readAttemptSequence>e.baselineSequence && sample.readContinuityGeneration &&
        sample.readContinuityGeneration!=(std::numeric_limits<uint64_t>::max)() &&
        sample.readStartedTick>=e.returnedTick && sample.readCompletedTick>=sample.readStartedTick &&
        now>=sample.readCompletedTick && now-sample.readCompletedTick<=200 && sample.readPowerGeneration==e.power &&
        sample.readTopologyGeneration==e.freshTopology && sample.readIdentityGeneration==e.freshIdentity &&
        inputCaptureBindStandardInput(&sample).known();
}
static bool inputHostOneXReturnedSourceCurrent() {
    const auto* e=t_inputHostOneXStartWrite;
    if(!e || !e->refreshing || !e->issueReserved || !e->returnedTick || !inputHostOneXStartCurrent())return false;
    std::unique_lock sample(g_gamepadReadMx,std::try_to_lock);if(!sample.owns_lock())return false;
    std::unique_lock identity(g_hidHidePhysicalIdentityMx,std::try_to_lock);if(!identity.owns_lock())return false;
    std::unique_lock snapshot(g_inputCaptureGamepadSnapshotMx,std::try_to_lock);if(!snapshot.owns_lock())return false;
    return inputHostOneXSameReturnedSource(*e,g_hidHidePhysicalIdentity) &&
        inputHostOneXFreshSample(*e,g_inputCaptureGamepadSnapshot,e->freshSlot);
}
static bool inputHostOneXRefreshSourcesCurrent(const std::vector<HidHidePhysicalIdentity>& sources) {
    const auto* e=t_inputHostOneXStartWrite;if(!e)return true;
    return e->refreshing && inputHostOneXStartCurrent() && sources.size()==1 &&
        inputHostOneXSameReturnedSource(*e,sources.front());
}
static bool inputHostOneXRefreshTargetsCurrent(const std::vector<std::string>& targets) {
    const auto* e=t_inputHostOneXStartWrite;if(!e)return true;
    return e->refreshing && inputHostOneXStartCurrent() && targets.size()==2 &&
        std::all_of(targets.begin(),targets.end(),[&](const auto& p){const auto id=hidHideUpper(p);
            return id==e->root || id==hidHideUpper(e->freshSource.deviceInstanceId);});
}
static bool inputHostOneXHideTargetCurrent(const std::wstring& operation,const std::wstring& target) {
    const auto* e=t_inputHostOneXStartWrite;if(!e)return true;
    const auto id=hidHideUpper(W2U(target));
    // An invalid/foreign target is a hard refusal, with no readiness state
    // mutation. Only a matching target with a temporarily unready source may wait.
    if(operation!=L"hide" || !e->refreshing ||
       (id!=e->root && id!=hidHideUpper(e->freshSource.deviceInstanceId)))return false;
    const bool allowed=inputHostOneXReturnedSourceCurrent();
    if(!allowed)t_inputHostOneXStartWrite->rehideUnissuedRefusal=true;
    return allowed;
}
// The hide inventory may resolve a new GAME child of the selected USB root
// before RawInput delivers it. Reconcile that cache only from a complete unique
// inventory and reverse slot proof; the final exact selected-source guard below
// remains authoritative. No foreign root, unknown selection or slot-only match.
// 1 unchanged/exact-or-foreign (the final guard still decides), 2 aligned,
// 0 transient context/lock; -1 current-child inventory unproven.
static int inputHostOneXAlignSelectedBeforeCycle(const HidHidePhysicalIdentity& physical,const OneXSourceSlotProof& proof,
                                                json& evidence){
    auto* e=t_inputHostOneXStartWrite;if(!e || e->issueReserved || !inputHostOneXStartCurrent())return 0;
    HidHidePhysicalIdentity before;uint64_t inventoryGeneration=0;
    {
        std::unique_lock<std::mutex> sampling(g_gamepadReadMx,std::defer_lock),
            cache(g_hidResolveCacheMx,std::defer_lock),raw(g_hidHidePhysicalIdentityMx,std::defer_lock);
        if(std::try_lock(sampling,cache,raw)>=0)return 0;
        before=g_hidHidePhysicalIdentity;inventoryGeneration=g_hidResolveCacheGeneration;
        if(inputHostOneXOriginalSelectedSource(*e,before))return 1;
        if(!inputHostOneXGameSource(e->binding,before) ||
           !inputHostOneXOriginalSelectedSource(*e,g_hidHideVisibilityJournal.physicalSources.front()) ||
           hidHideUpper(before.baseContainerDeviceInstanceId)!=e->root ||
           before.vendorId!=physical.vendorId || before.productId!=physical.productId)return 1;
    }
    bool complete=false;std::vector<HidHidePhysicalIdentity> sources;
    if(!inputHostOneXResolveSelectedChildren(sources,&complete) || !complete || sources.size()!=1 ||
       !inputHostOneXOriginalSelectedSource(*e,sources.front()) ||
       sources.front().vendorId!=physical.vendorId || sources.front().productId!=physical.productId ||
       hidHideUpper(sources.front().sourcePath)!=hidHideUpper(physical.sourcePath) ||
       hidHideUpper(sources.front().baseContainerDevicePath)!=hidHideUpper(physical.baseContainerDevicePath)){
        evidence={{"reason","current-selected-child-inventory-unproven"},{"inventoryComplete",complete},
            {"inventoryCount",sources.size()},{"selectedChildBefore",before.deviceInstanceId},
            {"ownedChild",physical.deviceInstanceId},{"sameRoot",true},{"published",false}};
        return -1;
    }
    OneXSourceSlotProof current;
    if(!inputHostOneXBindSourceSlot(sources.front(),current) || current.slot!=proof.slot ||
       current.topology!=proof.topology || current.identity!=proof.identity || current.power!=e->power ||
       !inputHostOneXRootStarted(e->root) || !ymcc::msi::samePhysicalLocation(e->locations,inputHostOneXLocations(e->root)))return 0;
    std::unique_lock<std::mutex> sampling(g_gamepadReadMx,std::defer_lock),
        cache(g_hidResolveCacheMx,std::defer_lock),raw(g_hidHidePhysicalIdentityMx,std::defer_lock);
    if(std::try_lock(sampling,cache,raw)>=0 || !inputHostOneXStartCurrent() || e->issueReserved ||
       inventoryGeneration!=g_hidResolveCacheGeneration || current.topology!=g_xinputTopologyEpoch.load() ||
       current.identity!=g_xinputSlotIdentityEpoch.load() || current.power!=currentPowerGeneration() ||
       !g_xinputPhysicalSourceLocked || g_xinputPrimarySlot!=current.slot || current.slot>=4 ||
       g_xinputPhysicalSourceMask!=(1u<<current.slot) || xinputNonVirtualConnectedMask()!=(1u<<current.slot) ||
       g_xinputSlotUnknownMask || g_xinputVirtualEndpointMask)return 0;
    const auto& selected=g_hidHidePhysicalIdentity;
    if(selected.physicalIdentity!=before.physicalIdentity || selected.deviceInstanceId!=before.deviceInstanceId ||
       selected.baseContainerDeviceInstanceId!=before.baseContainerDeviceInstanceId ||
       !inputHostOneXGameSource(e->binding,selected) || hidHideUpper(selected.baseContainerDeviceInstanceId)!=e->root ||
       selected.vendorId!=physical.vendorId || selected.productId!=physical.productId)return 0;
    legionMotionSelectedIdentityChanging(g_hidHidePhysicalIdentity,sources.front());
    g_hidHidePhysicalIdentity=sources.front();
    evidence={{"reason","current-same-root-child-published-before-cycle"},{"inventoryComplete",true},{"inventoryCount",1},
        {"selectedChildBefore",before.deviceInstanceId},{"ownedChild",physical.deviceInstanceId},
        {"sameRoot",true},{"published",true},{"reverseSlot",current.slot},{"topologyEpoch",current.topology},
        {"identityEpoch",current.identity},{"newUsbCycle",false},{"targetPrepared",false}};
    return 2;
}
// Invoked by the existing transport immediately before its possible WriteFile.
// Sampling busy is an unissued refusal, never a spent USB operation.
static bool inputHostOneXStartMayIssue() {
    auto* e=t_inputHostOneXStartWrite;
    if(!e || !inputHostOneXStartCurrent())return false;
    if(!e->cycleDispatchActive){
        const bool allowed=!t_inputHostOneXHideTargetWrite || inputHostOneXReturnedSourceCurrent();
        if(!allowed && t_inputHostOneXHideTargetWrite)e->rehideUnissuedRefusal=true;
        return allowed;
    }
    // Read-only queries do not spend the cycle quota. A rehide still proves
    // its returned source at this final transport boundary.
    if(e->issueReserved)return false;
    std::unique_lock sampling(g_gamepadReadMx,std::try_to_lock);if(!sampling.owns_lock())return false;
    if(e->slot>=4 || !g_xinputPhysicalSourceLocked || g_xinputPrimarySlot!=e->slot ||
       g_xinputPhysicalSourceMask!=(1u<<e->slot) || !(g_xinputConnectedMask&(1u<<e->slot)) ||
       g_xinputSlotUnknownMask || g_xinputVirtualEndpointMask ||
       g_xinputTopologyEpoch.load()!=e->topology || g_xinputSlotIdentityEpoch.load()!=e->identity)return false;
    std::unique_lock identity(g_hidHidePhysicalIdentityMx,std::try_to_lock);
    if(!identity.owns_lock() || !inputHostOneXOriginalSelectedSource(*e,g_hidHidePhysicalIdentity))return false;
    e->issueReserved=true;return true; // No lock retained across pipe or device I/O.
}
// A persona handoff can return its physical source milliseconds after HELLO.
// Give the existing canonical owner a short pre-hide observation opportunity,
// rather than creating a target first and hiding an already-open source later.
// This only waits/admits; it grants no new device selection or port authority.
static int inputHostOneXAwaitPreHideSource(uint64_t ticket,std::unique_lock<InputHostMutex>& hostLock){
    if(!g_machineIdentity.resolved || g_machineIdentity.family!=YmccFamily::OneXPlayer ||
       !ymcc::capabilities::onexVirtual::bind(g_machineIdentity.deviceClass).known())return 0;
    const auto begin=inputHostStartProtectionNow();const auto view=g_virtualStartProtection.view(begin);
    const auto epoch=g_inputHostEpoch,power=currentPowerGeneration(),intent=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
    const auto run=g_inputHostRunId,persona=inputHostConfiguredPersona();const auto process=g_inputHostProcess;
    auto deadline=begin+(std::min)(view.remainingMs,1000ULL);
    if(g_inputHostSwitchRecoveryStartDeadline)deadline=(std::min)(deadline,g_inputHostSwitchRecoveryStartDeadline);
    const auto current=[&](){const auto now=inputHostStartProtectionNow();const auto v=g_virtualStartProtection.view(now);
        return ticket && v.requestId==ticket && v.locked && v.remainingMs && v.persona==persona &&
            (v.phase=="pending" || v.phase=="verifying") &&
            g_machineIdentity.family==YmccFamily::OneXPlayer && g_realStickTest.enabled && g_realStickTest.virtualTargetEnabled &&
            !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) &&
            intent==g_inputHostVirtualIntentRevision.load(std::memory_order_acquire) && inputHostConfiguredPersona()==persona &&
            currentPowerGeneration()==power && g_inputHostBoundPowerGeneration==power && g_inputHostEpoch==epoch &&
            g_inputHostRunId==run && g_inputHostProcess==process && process && g_inputHostHello &&
            !g_inputHostPrepared && !g_inputHostPrepareMayHaveReachedHost && !g_inputHostReleaseQuarantined &&
            !g_hidHideVisibilityJournal.active && g_inputReady && !g_inputHostPowerAdmissionRevoked &&
            g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready && !inputHostExitFenceRaised() &&
            !g_exitRequested && !g_inputCaptureStop && !physicalReleaseWindowActive();};
    const auto note=[&](const char* reason){appendNativeLifecycleLog("input-host-onex-start-refresh",{
        {"policy","VC-ONEX-START-20261105-R1"},{"repairRevision","VC-ONEX-FAMILY-GAME-CANDIDATE-R1"},
        {"stage","pre-hide-source-observation"},{"reason",reason},{"requestId",ticket},{"hostEpoch",epoch},{"runId",run},
        {"persona",persona},{"elapsedMs",inputHostStartProtectionNow()-begin},{"observationMaxMs",1000},
        {"driverOperationMayHaveIssued",false},{"freshSameRootSource",false},{"externalSuppressionVerified",false},
        {"filterAttachmentProven",false},{"newTargetCreatedHere",false}});};
    if(!current()){note("original-execution-not-current");return -1;}
    while(inputHostStartProtectionNow()<deadline){
        hostLock.unlock();try{Sleep(8);if(g_inputCaptureOwnerDispatchesBus)inputCaptureOwnerFallbackBusTick();}
        catch(...){hostLock.lock();throw;}hostLock.lock();
        if(!current()){note("original-execution-not-current");return -1;}
        if(ymStartAdmitSource()){note("selected-source-ready-before-hide");return 1;}
    }
    note("source-still-pending-generic-path-retained");return 0;
}
// Returns 0 optional no-op, 1 completed with a fresh same-root source, -1 failed.
static int inputHostOneXPostHideBeforeTarget(uint64_t ticket,std::unique_lock<InputHostMutex>& hostLock) {
    using namespace ymcc::capabilities::onexVirtual;
    if(!g_machineIdentity.resolved || g_machineIdentity.family!=YmccFamily::OneXPlayer)return 0;
    const auto binding=ymcc::capabilities::onexVirtual::bind(g_machineIdentity.deviceClass);
    t_inputHostOneXPortEvidence=json::object();size_t locationCount=0;
    unsigned boundaryRequests=0,boundaryObservedEpoch=0,boundaryRequestedEpoch=0;
    bool boundaryEpochRequested=false;
    json selectedCacheAlignment=nullptr;
    const auto note=[&](const char* stage,const char* reason,bool issued,bool sourceReady){
        appendNativeLifecycleLog("input-host-onex-start-refresh",{{"policy","VC-ONEX-START-20261105-R1"},{"repairRevision","VC-ONEX-SELECTED-CHILD-20261105-R1"},
            {"stage",stage},{"reason",reason},{"requestId",ticket},{"persona",inputHostConfiguredPersona()},
            {"deviceId",std::string(binding.deviceId())},{"locationPathCount",locationCount},{"usbTopology",t_inputHostOneXPortEvidence},
            {"sourceInterfacePathKnown",!g_hidHideVisibilityJournal.physical.baseContainerDevicePath.empty()},
            {"reverseBindingApiResolved",g_ymccOpenXInputAvailable.load(std::memory_order_acquire)},
            {"hostEpoch",g_inputHostEpoch},{"runId",g_inputHostRunId},{"driverOperationMayHaveIssued",issued},
            {"operationEvidenceScope","selected-owned-root-refresh;not-internal-classification;pre-refresh-stop-rumble-excluded"},
            {"freshSameRootSource",sourceReady},{"externalSuppressionVerified",false},
            {"filterAttachmentProven",false},{"newTargetCreatedHere",false},
            {"selectedCacheAlignment",selectedCacheAlignment},
            {"boundaryScan",boundaryRequests?json{{"requests",boundaryRequests},{"observedEpoch",boundaryObservedEpoch},
                {"requestedTopologyEpoch",boundaryRequestedEpoch},{"requestOnly",true},{"newUsbCycle",false}}:json(nullptr)}});
    };
    const auto skip=[&](const char* reason){note("not-required-or-unavailable",reason,false,false);return 0;};
    if(!binding.known())return skip("family-capability-not-registered");
    const auto& journal=g_hidHideVisibilityJournal;
    if(!journal.active || journal.physicalSources.size()!=1 || !inputHostOneXGameSource(binding,journal.physical))
        return skip("unique-standard-game-root-not-proven");
    const auto physical=journal.physical;
    const bool preHidden=hidHideContainsPath(journal.before.hidden,physical.deviceInstanceId) ||
       hidHideContainsPath(journal.before.hidden,physical.baseContainerDeviceInstanceId);
    const bool ownedPair=hidHideContainsPath(journal.hiddenByUs,physical.deviceInstanceId) &&
       hidHideContainsPath(journal.hiddenByUs,physical.baseContainerDeviceInstanceId);
    // Our previous persona's carried responsibility is not somebody else's
    // pre-hidden configuration, nor evidence that a root refresh completed.
    if(preHidden && !ownedPair)return skip("already-hidden-source-no-new-refresh");
    const auto begin=inputHostStartProtectionNow();const auto view=g_virtualStartProtection.view(begin);
    OneXVirtualStartEpisode e;
    e.binding=binding;e.deviceClass=g_machineIdentity.deviceClass;e.persona=inputHostConfiguredPersona();
    e.runId=g_inputHostRunId;e.root=hidHideUpper(physical.baseContainerDeviceInstanceId);
    e.sourceIdentity=physical.physicalIdentity;e.sourceInstance=hidHideUpper(physical.deviceInstanceId);
    e.ticket=ticket;e.deadline=begin+view.remainingMs;e.power=currentPowerGeneration();e.epoch=g_inputHostEpoch;e.host=g_inputHostProcess;
    e.intent=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
    if(g_inputHostSwitchRecoveryStartDeadline)e.deadline=(std::min)(e.deadline,g_inputHostSwitchRecoveryStartDeadline);
    struct Scope {OneXVirtualStartEpisode* previous=t_inputHostOneXStartWrite;
        explicit Scope(OneXVirtualStartEpisode& e){t_inputHostOneXStartWrite=&e;}
        ~Scope(){t_inputHostOneXStartWrite=previous;}} scope{e};
    // A bus callback while the Host lock is released must not inherit this
    // Start's scoped permissions or block an independent safety cleanup.
    struct SuspendScope {OneXVirtualStartEpisode* previous=t_inputHostOneXStartWrite;
        SuspendScope(){t_inputHostOneXStartWrite=nullptr;}
        ~SuspendScope(){t_inputHostOneXStartWrite=previous;}};
    if(!inputHostOneXStartCurrent()){note("failed","original-execution-not-current",false,false);return -1;}
    e.locations=inputHostOneXLocations(e.root);locationCount=e.locations.size();
    // HC acts on the selected physical controller, not its removable-port label.
    // Retain the exact query evidence without converting 1/unknown into internal.
    try {(void)inputHostOneXPortKind(e.root);}catch(...) {t_inputHostOneXPortEvidence=json{{"reason","diagnostic-query-exception"}};}
    if(e.locations.empty())return skip("selected-source-location-unproven");
    OneXSourceSlotProof initialProof;
    if(!inputHostOneXBindSourceSlot(physical,initialProof) || initialProof.power!=e.power)return skip("selected-source-root-slot-unproven");
    e.slot=initialProof.slot;e.topology=initialProof.topology;e.identity=initialProof.identity;
    HidHideVisibilitySnapshot hidden;
    const auto configuration=[&](const HidHideVisibilitySnapshot& s,const HidHidePhysicalIdentity& p){
        const auto module=hidHideCurrentModulePath();return s.valid && s.cloaking && !module.empty() &&
            hidHideContainsPath(s.applications,W2U(module)) && hidHideInverseApplicationListState(s.inverseRaw)=="off" &&
            hidHideContainsPath(s.hidden,p.deviceInstanceId) && hidHideContainsPath(s.hidden,p.baseContainerDeviceInstanceId);
    };
    if(!inputHostOneXVisibility(hidden) || !configuration(hidden,physical)){
        note("failed","hidden-configuration-unproven",false,false);return -1;
    }
    if(!inputHostOneXStartCurrent() || !ymcc::msi::samePhysicalLocation(e.locations,inputHostOneXLocations(e.root))){
        note("failed","context-changed-before-refresh",false,false);return -1;
    }
    {std::lock_guard sample(g_inputCaptureGamepadSnapshotMx);e.baselineSequence=g_inputCaptureGamepadSnapshot.readAttemptSequence;}
    hidHideCycleClearBusyIfStale("onex-start-hide-setup");
    if(g_hidHideCycleInFlight.load())return skip("existing-root-refresh-owner-busy");
    note("ready","exact-selected-owned-hidden-source",false,false);
    // Charge only at the final possible pipe write. No retry after an ACK loss.
    bool cycleReturned=false;
    while(inputHostOneXStartCurrent() && !e.issueReserved){
        const auto now=inputHostStartProtectionNow();if(now>=e.deadline)break;
        const DWORD budget=static_cast<DWORD>((std::min)(5000ULL,e.deadline-now));
        OneXSourceSlotProof proof;
        if(!ymcc::msi::samePhysicalLocation(e.locations,inputHostOneXLocations(e.root)) ||
           !inputHostOneXBindSourceSlot(physical,proof) ||
           proof.slot!=e.slot || proof.power!=e.power){
            note("failed","root-location-or-source-changed-before-dispatch",false,false);return -1;
        }
        e.topology=proof.topology;e.identity=proof.identity;
        const int selectedReady=inputHostOneXAlignSelectedBeforeCycle(physical,proof,selectedCacheAlignment);
        if(selectedReady<0){note("failed","current-selected-child-inventory-unproven",false,false);return -1;}
        if(selectedReady==2)note("selected-source-aligned","current-same-root-child-before-cycle",false,false);
        if(selectedReady>0){
        struct CycleDispatch {OneXVirtualStartEpisode& e;explicit CycleDispatch(OneXVirtualStartEpisode& value):e(value){e.cycleDispatchActive=true;}
            ~CycleDispatch(){e.cycleDispatchActive=false;}};
        {CycleDispatch dispatch(e);cycleReturned=hidHideCycleInstance(U2W(e.root),"onex-start-post-hide",budget);}
        }
        if(e.issueReserved)break;
        if(!inputHostOneXStartCurrent())break;
        hostLock.unlock();try{SuspendScope suspend;Sleep(8);if(g_inputCaptureOwnerDispatchesBus)inputCaptureOwnerFallbackBusTick();}catch(...){hostLock.lock();throw;}hostLock.lock();
    }
    if(!e.issueReserved || !cycleReturned || !inputHostOneXStartCurrent()){
        note("failed",e.issueReserved?"refresh-result-unproven-no-reissue":"refresh-unissued-context-retired",e.issueReserved,false);return -1;
    }
    const auto returned=inputHostStartProtectionNow();e.returnedTick=returned;
    xinputSlotsMarkDiscoveryDirty();xinputSlotsRequestIdentityReprobe(0xFu,"onex-post-hide-refresh-returned");
    g_xinputPreTargetBoundaryScanned=false;g_xinputSlotFullScanEpochStart=g_xinputTopologyEpoch.load();g_xinputSlotFullScanOnce=true;
    note("waiting-source","same-original-ticket-and-deadline",true,false);
    bool readmitted=false;
    while(inputHostOneXStartCurrent()){
        bool boundaryRequestedThisIteration=false;
        uint64_t inventoryGeneration=0;bool inventoryAdmitted=false;
        {std::unique_lock cache(g_hidResolveCacheMx,std::try_to_lock);
            if(cache.owns_lock()){inventoryGeneration=g_hidResolveCacheGeneration;inventoryAdmitted=true;}}
        bool complete=false;std::vector<HidHidePhysicalIdentity> sources;
        if(inventoryAdmitted && inputHostOneXRootStarted(e.root) &&
           ymcc::msi::samePhysicalLocation(e.locations,inputHostOneXLocations(e.root)) &&
           inputHostOneXResolveSources(sources,&complete) && complete && sources.size()==1 &&
           inputHostOneXGameSource(binding,sources.front()) && hidHideUpper(sources.front().baseContainerDeviceInstanceId)==e.root){
            OneXSourceSlotProof proof;
            if(inputHostOneXBindSourceSlot(sources.front(),proof) && proof.power==e.power){
                const auto slot=proof.slot;
                std::unique_lock<std::mutex> sampling(g_gamepadReadMx,std::defer_lock),
                    cache(g_hidResolveCacheMx,std::defer_lock),raw(g_hidHidePhysicalIdentityMx,std::defer_lock);
                if(std::try_lock(sampling,cache,raw)<0 && inputHostOneXStartCurrent() &&
                   inventoryGeneration==g_hidResolveCacheGeneration && proof.topology==g_xinputTopologyEpoch.load() &&
                   proof.identity==g_xinputSlotIdentityEpoch.load() && proof.power==currentPowerGeneration() &&
                   xinputNonVirtualConnectedMask()==(1u<<slot) && !g_xinputSlotUnknownMask){
                    const auto& selected=g_hidHidePhysicalIdentity;
                    const bool originalChild=selected.physicalIdentity==e.sourceIdentity &&
                        hidHideUpper(selected.deviceInstanceId)==e.sourceInstance;
                    const bool returnedChild=selected.physicalIdentity==sources.front().physicalIdentity &&
                        hidHideUpper(selected.deviceInstanceId)==hidHideUpper(sources.front().deviceInstanceId);
                    const bool sameSelectedRoot=!selected.resolved ||
                        (hidHideUpper(selected.baseContainerDeviceInstanceId)==e.root && (originalChild || returnedChild));
                    if(sameSelectedRoot){
                        // A late arrival can retire the four-slot proof AFTER source
                        // readmission, without another disconnect. PreTargetSlots is
                        // then nonzero, so the ordinary admission owner will not ask
                        // for this boundary again. Ask its existing sampler once per
                        // observed topology; never advance topology/identity here or
                        // waive the original fresh scan, source or deadline gates.
                        const bool boundaryCurrent=g_xinputPreTargetBoundaryScanned &&
                            g_xinputBoundaryScanEpoch.load()==proof.topology;
                        if(!boundaryCurrent){
                            if(!g_xinputSlotFullScanOnce.load(std::memory_order_acquire) &&
                               (!boundaryEpochRequested || boundaryRequestedEpoch!=proof.topology)){
                                boundaryObservedEpoch=g_xinputBoundaryScanEpoch.load();
                                boundaryRequestedEpoch=proof.topology;boundaryEpochRequested=true;++boundaryRequests;
                                g_xinputSlotFullScanEpochStart.store(proof.topology,std::memory_order_release);
                                g_xinputSlotFullScanOnce.store(true,std::memory_order_release);
                                boundaryRequestedThisIteration=true;
                            }
                        }else{
                            // Existing identity-owner locks publish only the proven returned
                            // child of the original root. Quiet RawInput may still name A1.
                            legionMotionSelectedIdentityChanging(g_hidHidePhysicalIdentity,sources.front());
                            g_hidHidePhysicalIdentity=sources.front();raw.unlock();cache.unlock();
                            if(!readmitted || !g_xinputPhysicalSourceLocked || g_xinputPrimarySlot!=slot ||
                               g_xinputPhysicalSourceMask!=(1u<<slot)){
                                g_xinputPhysicalSourceLocked=false;g_xinputPhysicalSourceMask=0;g_xinputPreTargetSlots=0;
                                g_xinputAdmittedMask=0;g_xinputLoggedAdmittedMask=0;g_xinputPrimarySlot=slot;
                                readmitted=ymStartAdmitSource();
                            }
                            e.freshSource=sources.front();e.freshSlot=slot;e.freshTopology=proof.topology;e.freshIdentity=proof.identity;
                            InputCaptureGamepadSnapshot sample;{std::lock_guard snapshot(g_inputCaptureGamepadSnapshotMx);sample=g_inputCaptureGamepadSnapshot;}
                            if(readmitted && inputHostOneXFreshSample(e,sample,slot)){
                                e.refreshing=true;sampling.unlock();
                                // Only a proved unissued readiness refusal may wait. A lost
                                // receipt or real failure cannot replay a possible operation.
                                e.rehideUnissuedRefusal=false;
                                const bool refreshed=inputHostOneXStartCurrent() &&
                                    hidHideRefreshPhysicalHide(e.runId,"onex-post-refresh-same-source");
                                if(!refreshed){if(!e.rehideUnissuedRefusal || !inputHostOneXStartCurrent())break;}
                                else {
                                    if(!inputHostOneXVisibility(hidden) || !configuration(hidden,e.freshSource) ||
                                       !inputHostOneXSameReturnedSource(e,g_hidHideVisibilityJournal.physical) ||
                                       !ymcc::msi::samePhysicalLocation(e.locations,inputHostOneXLocations(e.root)) ||
                                       !inputHostOneXRootStarted(e.root))break;
                                    // All slow PnP queries precede the final liveness/freshness
                                    // check. Contended sampling waits within this same ticket.
                                    if(!inputHostOneXStartCurrent())break;
                                    if(inputHostOneXReturnedSourceCurrent()){
                                        note("completed","fresh-same-root-source-before-target",true,true);return 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        if(boundaryRequestedThisIteration && boundaryRequests<=4)
            note("waiting-source","current-topology-boundary-scan-requested",true,false);
        const auto now=inputHostStartProtectionNow();if(now>=e.deadline)break;
        hostLock.unlock();try{SuspendScope suspend;Sleep(static_cast<DWORD>((std::min)(8ULL,e.deadline-now)));
            if(g_inputCaptureOwnerDispatchesBus)inputCaptureOwnerFallbackBusTick();}catch(...){hostLock.lock();throw;}hostLock.lock();
    }
    note("failed","same-root-source-return-unproven",true,false);return -1;
}
