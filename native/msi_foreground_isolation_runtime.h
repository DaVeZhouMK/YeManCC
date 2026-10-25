#pragma once
#include "msi_foreground_isolation.h"
#include "msi_native_foreground_isolation.h"

// Shared MSI-only read-only collector, defined by the following native runtime.
static ymcc::msi::NativeForegroundXusbInventory msiNativeForegroundXusbInventory(const HidHidePhysicalIdentity& source);

// Included after the existing inventory, query and selected-source helpers.
// No driver API, new service/thread, periodic PnP scan, mode write, or alternate
// foreground decision is introduced. Existing journal owns every added path.
static bool msiForegroundIsolationFamilyAllowed() {
    return g_machineIdentity.resolved && msiClawKnownBoardAuthority();
}
struct MsiForegroundIsolationJob {
    uint64_t epoch=0,power=0,edge=0,revision=0,token=0;
    HidHidePhysicalIdentity source;
    ULONGLONG observedTick=0;
    std::vector<std::wstring> ownedLocations;
    bool journalContinuation=false;
};
struct MsiForegroundIsolationState {
    uint64_t epoch=0, power=0, edge=0, revision=0, nextToken=0, pendingToken=0;
    unsigned mainGeneration=0;
    std::string mainRoot, attemptedKey;
    std::vector<std::wstring> mainLocations;
    HidHidePhysicalIdentity observed;
    ULONGLONG observedTick=0;
    std::optional<MsiForegroundIsolationJob> journalWait;
    bool journalWakeQueued=false;
};
static std::mutex g_msiForegroundIsolationMx;
static MsiForegroundIsolationState g_msiForegroundIsolation;
static std::atomic<bool> g_msiForegroundUiOwned{false};
static std::atomic<uint64_t> g_msiForegroundUiEdge{0};
static std::atomic<uint64_t> g_msiForegroundAnchorRevision{0};
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
// Only compiled into the offline regression binary. None of these seams are
// enabled or present in a shipped production binary.
struct MsiForegroundIsolationTestIo {
    std::function<std::vector<std::wstring>(const std::string&)> locations;
    std::function<std::vector<HidHideInterfaceRecord>(const HidHidePhysicalIdentity&)> inventory;
    std::function<std::vector<HidHidePhysicalIdentity>()> nativeCandidates;
    std::function<ymcc::msi::NativeForegroundXusbInventory(const HidHidePhysicalIdentity&)> xusbInventory;
    std::function<bool(HidHideVisibilitySnapshot&)> snapshot;
    std::function<bool(const std::wstring&)> hide;
    std::function<bool()> ui;
    std::function<void(const std::string&)> queue;
};
static MsiForegroundIsolationTestIo g_msiForegroundTestIo;
#endif
static bool msiForegroundIsolationUiEligible() {
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiForegroundTestIo.ui) return g_msiForegroundTestIo.ui();
#endif
    return gamepadUiInputEligible();
}
static bool msiForegroundIsolationSnapshot(HidHideVisibilitySnapshot& snapshot) {
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiForegroundTestIo.snapshot) return g_msiForegroundTestIo.snapshot(snapshot);
#endif
    return hidHideCaptureVisibilitySnapshot(snapshot);
}
static bool msiForegroundIsolationHide(const std::wstring& path) {
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiForegroundTestIo.hide) return g_msiForegroundTestIo.hide(path);
#endif
    return inputHostHidHideApplyViaPipeLocked(L"hide",path) || hidHideRun(L"--dev-hide \""+path+L"\"");
}

static std::vector<std::wstring> msiForegroundIsolationLocations(const std::string& root) {
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiForegroundTestIo.locations) return g_msiForegroundTestIo.locations(root);
#endif
    std::vector<std::wstring> result;
    if(root.empty()) return result;
    DEVINST node=0;
    std::wstring wide=U2W(root);
    if(CM_Locate_DevNodeW(&node,wide.data(),CM_LOCATE_DEVNODE_NORMAL)!=CR_SUCCESS) return result;
    DEVPROPTYPE type=0; ULONG bytes=0;
    const auto sizeRc=CM_Get_DevNode_PropertyW(node,&DEVPKEY_Device_LocationPaths,&type,nullptr,&bytes,0);
    if(sizeRc!=CR_BUFFER_SMALL || bytes<sizeof(wchar_t)*2 || bytes>65536) return result;
    std::vector<wchar_t> paths((bytes+sizeof(wchar_t)-1)/sizeof(wchar_t)+2,0);
    if(CM_Get_DevNode_PropertyW(node,&DEVPKEY_Device_LocationPaths,&type,
        reinterpret_cast<PBYTE>(paths.data()),&bytes,0)!=CR_SUCCESS || type!=DEVPROP_TYPE_STRING_LIST) return result;
    const size_t limit=bytes/sizeof(wchar_t);
    for(size_t i=0;i<limit && paths[i];) {
        size_t end=i;while(end<limit && paths[end])++end;
        if(end==limit) return {};
        const std::wstring path(paths.data()+i,end-i);
        // Locations are normalized as ASCII PnP paths; serial strings or PID
        // text are never used as guessed cross-mode ownership.
        result.push_back(U2W(hidHideUpper(W2U(path)))); i=end+1;
    }
    return result;
}

static void msiForegroundIsolationTryQueue(bool allowCurrentDiscovery=false);

static void msiForegroundIsolationRememberMain(const std::string& root) {
    if(!msiForegroundIsolationFamilyAllowed() || root.empty() || !g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled) return;
    const auto epoch=inputHostEpochToken();
    const auto power=g_powerGeneration.load(std::memory_order_acquire);
    if(!epoch || !power) return;
    unsigned generation=0;
    {
        std::lock_guard<std::mutex> lock(g_msiMainSourceMx);
        if(!g_msiMainSource.valid || g_msiMainSource.usbInstance!=root ||
            g_msiMainSource.vendorId!=0x0DB0 || g_msiMainSource.productId!=0x1902) return;
        generation=g_msiMainSource.generation;
    }
    if(!generation) return;
    uint64_t revision=0;
    {
        std::lock_guard<std::mutex> lock(g_msiForegroundIsolationMx);
        auto& state=g_msiForegroundIsolation;
        if(state.epoch==epoch && state.power==power && state.mainRoot==root &&
            state.mainGeneration==generation && !state.mainLocations.empty()) return;
        // Reconfirmed mode-2 episodes get their own bounded attempt. Invalidate
        // OLD work before reading CM, even if the USB instance string is reused.
        revision=g_msiForegroundAnchorRevision.fetch_add(1,std::memory_order_acq_rel)+1;
        state.revision=revision;state.mainGeneration=generation;
        state.epoch=epoch;state.power=power;state.mainRoot=root;
        state.mainLocations.clear();state.attemptedKey.clear();state.pendingToken=0;
        state.journalWait.reset();state.journalWakeQueued=false;
    }
    // Exactly once per authoritative source generation, never on stable frames.
    auto locations=msiForegroundIsolationLocations(root);
    const bool locationKnown=!locations.empty();
    json locationEvidence=json::array();
    for(const auto& location:locations) locationEvidence.push_back(W2U(location));
    {
        // Reject late CM results after a source/Host/power change. Lock order is
        // main-source -> foreground-state; no inverse nested lock is used.
        std::lock_guard<std::mutex> mainLock(g_msiMainSourceMx);
        if(!g_msiMainSource.valid || g_msiMainSource.usbInstance!=root ||
            g_msiMainSource.generation!=generation ||
            g_inputHostEpochMirror.load(std::memory_order_acquire)!=epoch ||
            g_powerGeneration.load(std::memory_order_acquire)!=power) return;
        std::lock_guard<std::mutex> lock(g_msiForegroundIsolationMx);
        auto& state=g_msiForegroundIsolation;
        if(state.revision!=revision || g_msiForegroundAnchorRevision.load(std::memory_order_acquire)!=revision) return;
        state.mainLocations=std::move(locations);
    }
    appendNativeLifecycleLog("msi-foreground-isolation", {
        {"state",locationKnown?"physical-anchor-ready":"deferred"},
        {"reason",locationKnown?"current-leased-msi-location":"physical-location-unresolved"},
        {"hostEpoch",epoch},{"powerGeneration",power},{"mainSourceGeneration",generation},{"sourceRoot",root},{"physicalLocationPaths",locationEvidence},
        {"verificationScope","identity-only"},{"externalPhysicalSuppression","unverified"}});
    msiForegroundIsolationTryQueue(true); // initial/reconfirmed lease while UI already owns focus
}

static void msiForegroundIsolationEnqueueToken(uint64_t token) {
    const auto reason="msi-foreground-isolation:"+std::to_string(token);
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiForegroundTestIo.queue) {g_msiForegroundTestIo.queue(reason);return;}
#endif
    (void)inputHostQueueLifecycleWork(reason.c_str());
}

static void msiForegroundIsolationTryQueue(bool allowCurrentDiscovery) {
    if(!g_msiForegroundUiOwned.load(std::memory_order_acquire) ||
        !msiForegroundIsolationFamilyAllowed() || inputHostExitFenceRaised()) return;
    const auto epoch=inputHostEpochToken();
    const auto power=g_powerGeneration.load(std::memory_order_acquire);
    const auto edge=g_msiForegroundUiEdge.load(std::memory_order_acquire);
    uint64_t token=0;
    {
        std::lock_guard<std::mutex> lock(g_msiForegroundIsolationMx);
        auto& s=g_msiForegroundIsolation;
        if(!epoch || s.epoch!=epoch || s.power!=power || s.mainLocations.empty()) return;
        const bool fresh=s.observed.resolved && s.observedTick && GetTickCount64()>=s.observedTick &&
            GetTickCount64()-s.observedTick<=2000;
        if(!fresh) {
            if(!allowCurrentDiscovery) return;
            s.observed=HidHidePhysicalIdentity{};
            s.observedTick=GetTickCount64(); // freshness of the FOREGROUND job, not invented input
        }
        const auto key=std::to_string(epoch)+":"+std::to_string(power)+":"+
            std::to_string(edge)+":"+std::to_string(s.revision)+":"+s.observed.baseContainerDeviceInstanceId;
        if(s.attemptedKey==key) return;
        s.journalWait.reset();s.journalWakeQueued=false;
        s.attemptedKey=key;s.edge=edge;token=++s.nextToken;s.pendingToken=token;
    }
    // Existing lifecycle owner serializes this with mode, hide and stop work.
    // One attempt per foreground/source identity; no unlimited auto-retry.
    msiForegroundIsolationEnqueueToken(token);
}

static void msiForegroundIsolationJournalReady() {
    uint64_t token=0;
    {
        std::lock_guard<std::mutex> lock(g_msiForegroundIsolationMx);
        auto& state=g_msiForegroundIsolation;
        if(!state.journalWait || state.journalWakeQueued || state.pendingToken)return;
        const auto& job=*state.journalWait;
        const auto now=GetTickCount64();
        if(inputHostExitFenceRaised() || !g_msiForegroundUiOwned.load(std::memory_order_acquire) ||
            g_msiForegroundUiEdge.load(std::memory_order_acquire)!=job.edge ||
            g_msiForegroundAnchorRevision.load(std::memory_order_acquire)!=job.revision ||
            g_inputHostEpochMirror.load(std::memory_order_acquire)!=job.epoch ||
            g_powerGeneration.load(std::memory_order_acquire)!=job.power ||
            !job.observedTick || now<job.observedTick || now-job.observedTick>2000){
            state.journalWait.reset();return;
        }
        token=job.token;state.pendingToken=token;state.journalWakeQueued=true;
    }
    msiForegroundIsolationEnqueueToken(token);
}

static void msiForegroundIsolationObserveNative(const HidHidePhysicalIdentity& source) {
    if(!g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled ||
        !msiForegroundIsolationFamilyAllowed() || !source.resolved || source.vendorId!=0x0DB0 ||
        source.productId!=0x1901 || source.usagePage!=1 || (source.usage!=4 && source.usage!=5) ||
        source.baseContainerDeviceInstanceId.empty() ||
        hidHideIsVirtualIdentity(source.deviceInstanceId,source.baseContainerDeviceInstanceId)) return;
    {
        std::lock_guard<std::mutex> lock(g_msiForegroundIsolationMx);
        g_msiForegroundIsolation.observed=source;
        g_msiForegroundIsolation.observedTick=GetTickCount64();
    }
    msiForegroundIsolationTryQueue();
}

static void msiForegroundIsolationSetUiOwned(bool owned) {
    // A single atomic comparison on the existing frame path; no per-frame
    // mutex, PnP query, diagnostic row or extra input polling.
    if(g_msiForegroundUiOwned.exchange(owned,std::memory_order_acq_rel)==owned) return;
    g_msiForegroundUiEdge.fetch_add(1,std::memory_order_acq_rel);
    if(owned) msiForegroundIsolationTryQueue(true);
    else {
        std::lock_guard<std::mutex> lock(g_msiForegroundIsolationMx);
        if(g_msiForegroundIsolation.journalWakeQueued)g_msiForegroundIsolation.pendingToken=0;
        g_msiForegroundIsolation.journalWait.reset();g_msiForegroundIsolation.journalWakeQueued=false;
    }
}

static bool msiForegroundIsolationJobCurrent(const MsiForegroundIsolationJob& job,
                                            ymcc::msi::ForegroundIsolationFacts& facts) {
    facts.knownBoard=msiForegroundIsolationFamilyAllowed();
    facts.targetEnabled=g_realStickTest.enabled && g_realStickTest.virtualTargetEnabled;
    facts.uiOwnershipFlag=g_msiForegroundUiOwned.load(std::memory_order_acquire);
    facts.uiEligibilityEvaluated=facts.uiOwnershipFlag;
    facts.uiEligibility=facts.uiOwnershipFlag && msiForegroundIsolationUiEligible();
    facts.uiOwned=facts.uiOwnershipFlag && facts.uiEligibility;
    facts.modeArmed=g_msiDinputRouteDinputArmed.load(std::memory_order_acquire);
    facts.returnOwedEvaluated=facts.modeArmed;
    facts.returnOwed=facts.modeArmed && g_msiDinputReturnOwed.load(std::memory_order_acquire);
    facts.modeOwned=facts.modeArmed && facts.returnOwed;
    facts.exitFence=inputHostExitFenceRaised();facts.releaseWindow=msiForegroundIsolationBorrowBusy();
    facts.epoch=job.epoch;facts.power=job.power;facts.edge=job.edge;
    facts.currentPower=g_powerGeneration.load(std::memory_order_acquire);
    facts.currentEdge=g_msiForegroundUiEdge.load(std::memory_order_acquire);
    facts.observedTick=job.observedTick;facts.now=GetTickCount64();
    {
        std::lock_guard lock(g_inputHostMx);
        facts.currentEpoch=g_inputHostEpoch;
        facts.boundTarget=msiUiBoundTargetAdmitted(g_inputHostBoundPersona);
        facts.hostGuardObserved=0;facts.hostGuardPassed=0;
        using Guard=ymcc::msi::ForegroundHostGuard;
        const auto capture=[&](Guard guard,bool value){return ymcc::msi::foregroundObserveHostGuard(facts,guard,value);};
        // Same order and short-circuit behavior as the previous expression.
        // Unreached guards remain UNKNOWN in the receipt, not falsely "off".
        facts.hostReady=capture(Guard::InputReady,g_inputReady.load(std::memory_order_acquire)) &&
            capture(Guard::WriterAllowed,sgVirtualWriterAllowedNow()) &&
            capture(Guard::ProcessPresent,g_inputHostProcess!=nullptr) &&
            capture(Guard::Prepared,g_inputHostPrepared) &&
            capture(Guard::Neutralized,g_inputHostNeutralized) &&
            capture(Guard::TransportHealthy,!g_inputHostTransportFault) &&
            capture(Guard::PowerAdmitted,!g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire)) &&
            capture(Guard::JournalActive,g_hidHideVisibilityJournal.active);
    }
    facts.anchorRevision=job.revision;
    facts.currentAnchorRevision=g_msiForegroundAnchorRevision.load(std::memory_order_acquire);
    return facts.currentAnchorRevision==job.revision && ymcc::msi::foregroundIsolationCurrent(facts);
}

static json msiForegroundIsolationGateFacts(const ymcc::msi::ForegroundIsolationFacts& f) {
    static constexpr const char* guardNames[]={"inputReady","virtualWriterAllowed","processPresent",
        "prepared","neutralized","transportHealthy","powerAdmitted","journalActive"};
    static constexpr const char* failureNames[]={"unknown-or-unsupported-board","target-disabled",
        "bound-target-not-admitted","ui-not-owned-or-not-eligible","host-not-ready","mode-not-owned",
        "exit-fence","physical-borrow-window","host-epoch-mismatch","power-generation-mismatch",
        "foreground-edge-mismatch","observed-timestamp-invalid","observed-sample-stale"};
    json host=json::object(),failures=json::array();
    const auto bits=ymcc::msi::foregroundIsolationFailureBits(f);
    for(unsigned i=0;i<8;++i)host[guardNames[i]]=(f.hostGuardObserved&(1u<<i))
        ?json((f.hostGuardPassed&(1u<<i))!=0):json(nullptr);
    for(unsigned i=0;i<13;++i)if(bits&(1u<<i))failures.push_back(failureNames[i]);
    if(f.currentAnchorRevision!=f.anchorRevision)failures.push_back("anchor-revision-mismatch");
    const bool stampValid=f.observedTick && f.now>=f.observedTick;
    return {{"formatVersion",1},{"knownBoard",f.knownBoard},{"targetEnabled",f.targetEnabled},
        {"boundTarget",f.boundTarget},{"uiOwned",f.uiOwned},{"uiOwnershipFlag",f.uiOwnershipFlag},
        {"uiEligibility",f.uiEligibilityEvaluated?json(f.uiEligibility):json(nullptr)},
        {"modeOwned",f.modeOwned},{"modeArmed",f.modeArmed},
        {"returnOwed",f.returnOwedEvaluated?json(f.returnOwed):json(nullptr)},
        {"hostReady",f.hostReady},{"hostGuards",host},{"hostGuardObservedBits",f.hostGuardObserved},
        {"hostGuardPassedBits",f.hostGuardPassed},{"exitFence",f.exitFence},{"releaseWindow",f.releaseWindow},
        {"jobEpoch",f.epoch},{"currentEpoch",f.currentEpoch},{"jobPower",f.power},{"currentPower",f.currentPower},
        {"jobEdge",f.edge},{"currentEdge",f.currentEdge},{"jobAnchorRevision",f.anchorRevision},
        {"currentAnchorRevision",f.currentAnchorRevision},{"observedTick",f.observedTick},{"evaluatedAtTick",f.now},
        {"observedAgeMs",stampValid?json(f.now-f.observedTick):json(nullptr)},{"maxObservedAgeMs",2000},
        {"failedGuards",failures},{"scope","latest-prerequisite-evaluation-only;not-external-input-proof"}};
}

static void msiForegroundIsolationHandleWork(const std::string& reason) {
    uint64_t token=0;
    try { token=std::stoull(reason.substr(std::string("msi-foreground-isolation:").size())); }
    catch(...) { return; }
    MsiForegroundIsolationJob job;
    {
        std::lock_guard<std::mutex> lock(g_msiForegroundIsolationMx);
        auto& s=g_msiForegroundIsolation;
        if(!token || s.pendingToken!=token) return;
        if(s.journalWakeQueued && s.journalWait && s.journalWait->token==token){
            job=*s.journalWait;job.journalContinuation=true;
            s.journalWait.reset();s.journalWakeQueued=false;
        }else job={s.epoch,s.power,s.edge,s.revision,token,s.observed,s.observedTick,s.mainLocations};
        s.pendingToken=0;
    }
    json observedLocationsEvidence=json::array();
    json targetsEvidence=json::array();
    json xusbTargetsEvidence=json::array();bool xusbInventoryVerified=false;
    json ownedLocationsEvidence=json::array();
    for(const auto& location:job.ownedLocations) ownedLocationsEvidence.push_back(W2U(location));
    ymcc::msi::ForegroundIsolationFacts facts;
    const auto receipt=[&](const char* state,const char* detail,size_t count=0) {
        appendNativeLifecycleLog("msi-foreground-isolation", {
            {"state",state},{"reason",detail},{"hostEpoch",job.epoch},{"powerGeneration",job.power},
            {"foregroundEdge",job.edge},{"targets",count},{"sourceRoot",job.source.baseContainerDeviceInstanceId},
            {"journalContinuation",job.journalContinuation},
            {"ownedLocationPaths",ownedLocationsEvidence},{"observedLocationPaths",observedLocationsEvidence},
            {"targetInstancePaths",targetsEvidence},{"xusbGameTargets",xusbTargetsEvidence},
            {"xusbInventoryVerified",xusbInventoryVerified},
            {"xusbTargetEvidence",xusbInventoryVerified?"active-XUSB-interface-and-exact-PnP-ancestor":"not-examined"},
            {"gateFacts",msiForegroundIsolationGateFacts(facts)},
            {"verificationScope","config-only"},{"externalPhysicalSuppression","unverified"}});
    };
    if(!msiForegroundIsolationJobCurrent(job,facts)) {
        constexpr auto journalBit=1u<<static_cast<unsigned>(ymcc::msi::ForegroundHostGuard::JournalActive);
        constexpr auto hostBits=(journalBit<<1)-1;
        const bool onlyJournal=!job.journalContinuation && facts.currentAnchorRevision==job.revision &&
            ymcc::msi::foregroundIsolationFailureBits(facts)==(1u<<4) &&
            facts.hostGuardObserved==hostBits && facts.hostGuardPassed==(hostBits&~journalBit);
        if(onlyJournal){
            std::lock_guard<std::mutex> lock(g_msiForegroundIsolationMx);
            auto& state=g_msiForegroundIsolation;
            if(state.nextToken==job.token && !state.pendingToken && state.revision==job.revision)
                state.journalWait=job;
        }
        receipt("skipped","stale-or-not-foreground");return;
    }
    if(!job.source.resolved) {
        // Foreground entry must cover a present native pad BEFORE the first
        // button leaks. One current-PnP discovery per edge; it authorizes hide
        // only, never live-source input or a firmware mode recovery.
        std::vector<HidHidePhysicalIdentity> candidates;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
        if(g_msiForegroundTestIo.nativeCandidates) candidates=g_msiForegroundTestIo.nativeCandidates();
        else
#endif
        hidHideResolvePhysicalIdentities(candidates);
        std::vector<HidHidePhysicalIdentity> matches;
        for(const auto& candidate:candidates) {
            if(!ymcc::msi::foregroundSiblingGameInputAdmitted(true,
                hidHideIsVirtualIdentity(candidate.deviceInstanceId,candidate.baseContainerDeviceInstanceId),
                candidate.vendorId,candidate.productId,candidate.usagePage,candidate.usage)) continue;
            if(ymcc::msi::samePhysicalLocation(job.ownedLocations,
                msiForegroundIsolationLocations(candidate.baseContainerDeviceInstanceId))) matches.push_back(candidate);
        }
        if(matches.size()!=1) {receipt("deferred","foreground-native-root-not-unique");return;}
        job.source=matches.front();
    }
    facts.sourceResolved=job.source.resolved;
    facts.sourceVirtual=hidHideIsVirtualIdentity(job.source.deviceInstanceId,job.source.baseContainerDeviceInstanceId);
    facts.vendorId=job.source.vendorId;facts.productId=job.source.productId;
    facts.usagePage=job.source.usagePage;facts.usage=job.source.usage;
    const auto currentLocations=msiForegroundIsolationLocations(job.source.baseContainerDeviceInstanceId);
    for(const auto& location:currentLocations) observedLocationsEvidence.push_back(W2U(location));
    facts.samePhysicalLocation=ymcc::msi::samePhysicalLocation(job.ownedLocations,currentLocations);
    if(!ymcc::msi::foregroundNativeInputAdmitted(facts)) {receipt("deferred","current-physical-location-unproven");return;}
    std::vector<HidHideInterfaceRecord> inventory;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiForegroundTestIo.inventory) inventory=g_msiForegroundTestIo.inventory(job.source);
    else
#endif
    inventory=hidHideBuildSourceInterfaceInventory(job.source.baseContainerDeviceInstanceId,job.source.deviceInstanceId);
    auto targets=hidHidePhysicalTargets(job.source);
    bool observedStillPresent=false;
    for(const auto& item:inventory) {
        const bool sameRoot=ascii_lower(item.baseContainerDeviceInstanceId)==ascii_lower(job.source.baseContainerDeviceInstanceId);
        if(!ymcc::msi::foregroundSiblingGameInputAdmitted(sameRoot,
            hidHideIsVirtualIdentity(item.instanceId,item.baseContainerDeviceInstanceId),
            item.vendorId,item.productId,item.usagePage,item.usage)) continue;
        if(ascii_lower(item.instanceId)==ascii_lower(job.source.deviceInstanceId)) observedStillPresent=true;
        hidHideAppendTarget(targets,item.instanceId);
    }
    // Same exact-ancestor game-interface proof as native-off, but this caller
    // retains its existing Host/source/power/UI/borrow gates and journal. No
    // primary source refresh, mode write or generic target-policy change.
    ymcc::msi::NativeForegroundXusbInventory xusb;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiForegroundTestIo.xusbInventory)xusb=g_msiForegroundTestIo.xusbInventory(job.source);
    else xusb.reason="test-managed-xusb-inventory-not-injected";
#else
    xusb=msiNativeForegroundXusbInventory(job.source);
#endif
    if(!xusb.complete){receipt("deferred",xusb.reason.c_str());return;}
    xusbInventoryVerified=true;
    for(const auto& item:xusb.interfaces)if(ymcc::msi::nativeForegroundXusbTargetAdmitted(job.source.baseContainerDeviceInstanceId,item)){
        hidHideAppendTarget(targets,item.instanceId);
        xusbTargetsEvidence.push_back(item.instanceId);
    }
    const auto recheckedLocations=msiForegroundIsolationLocations(job.source.baseContainerDeviceInstanceId);
    if(!ymcc::msi::samePhysicalLocation(job.ownedLocations,recheckedLocations)){
        receipt("deferred","xusb-physical-location-changed");return;
    }

    targetsEvidence=targets;
    if(!observedStillPresent || targets.empty() || targets.size()>16) {receipt("deferred","live-gaming-interface-unproven");return;}
    if(!msiForegroundIsolationJobCurrent(job,facts)) {receipt("skipped","ownership-changed-before-hide");return;}
    std::lock_guard hostLock(g_inputHostMx);
    // No generic transaction is rewritten; add only current gaming identities
    // of the SAME leased MSI physical device. Existing third-party hidden paths
    // are not claimed, and ordinary stop/disable journal cleanup restores ours.
    if(g_inputHostEpoch!=job.epoch || !g_hidHideVisibilityJournal.active || inputHostExitFenceRaised()) return;
    HidHideVisibilitySnapshot before;
    if(!msiForegroundIsolationSnapshot(before)) {receipt("deferred","visibility-query-failed");return;}
    facts.cloakOn=before.cloaking;
    facts.inverseOff=hidHideInverseApplicationListState(before.inverseRaw)=="off";
    facts.readerExempt=hidHideContainsPath(before.applications,W2U(hidHideCurrentModulePath()));
    facts.now=GetTickCount64();
    if(!ymcc::msi::foregroundHideAdmitted(facts)) {receipt("deferred","hide-contract-not-admitted");return;}
    bool ok=true;
    for(const auto& target:targets) {
        if(!g_msiForegroundUiOwned.load(std::memory_order_acquire) ||
            g_msiForegroundUiEdge.load(std::memory_order_acquire)!=job.edge ||
            g_msiForegroundAnchorRevision.load(std::memory_order_acquire)!=job.revision ||
            g_powerGeneration.load(std::memory_order_acquire)!=job.power ||
            !g_realStickTest.virtualTargetEnabled || !g_inputReady.load(std::memory_order_acquire) ||
            !sgVirtualWriterAllowedNow() || GetTickCount64()-job.observedTick>2000 ||
            !msiForegroundIsolationUiEligible() || msiForegroundIsolationBorrowBusy() || inputHostExitFenceRaised()) {ok=false;break;}
        if(hidHideIsVirtualIdentity(target,{})) {ok=false;break;}
        if(hidHideContainsPath(before.hidden,target)) continue;
        const auto wide=U2W(target);
        if(std::find(g_hidHideVisibilityJournal.hiddenByUs.begin(),g_hidHideVisibilityJournal.hiddenByUs.end(),wide)==g_hidHideVisibilityJournal.hiddenByUs.end())
            g_hidHideVisibilityJournal.hiddenByUs.push_back(wide);
        ok=msiForegroundIsolationHide(wide) && ok;
    }
    HidHideVisibilitySnapshot after;
    const bool readback=ok && msiForegroundIsolationSnapshot(after) && after.cloaking &&
        hidHideInverseApplicationListState(after.inverseRaw)=="off" &&
        hidHideContainsPath(after.applications,W2U(hidHideCurrentModulePath()));
    bool verified=readback;
    if(readback) for(const auto& target:targets) verified=hidHideContainsPath(after.hidden,target) && verified;
    receipt(verified?"config-verified":"failed",verified?"same-msi-native-gaming-paths-hidden":"hide-or-readback-failed",targets.size());
}

// The legacy reconnect helper replaces hiddenByUs with its primary-source
// targets. Preserve MSI-owned aliases across that bookkeeping-only rewrite,
// without changing the common helper or issuing a second physical hide.
struct MsiForegroundReconcileOwnership {
    uint64_t epoch=0;std::string transaction;std::vector<std::wstring> paths;
};
static MsiForegroundReconcileOwnership msiForegroundIsolationCaptureOwnedPaths() {
    MsiForegroundReconcileOwnership saved;
    if(!msiForegroundIsolationFamilyAllowed()) return saved;
    std::lock_guard lock(g_inputHostMx);
    if(!g_hidHideVisibilityJournal.active) return saved;
    saved.epoch=g_inputHostEpoch;saved.transaction=g_hidHideVisibilityJournal.transactionId;
    saved.paths=g_hidHideVisibilityJournal.hiddenByUs;
    return saved;
}
static void msiForegroundIsolationRetainOwnedPaths(const MsiForegroundReconcileOwnership& saved) {
    if(!saved.epoch || saved.paths.empty() || !msiForegroundIsolationFamilyAllowed()) return;
    std::lock_guard lock(g_inputHostMx);
    auto& journal=g_hidHideVisibilityJournal;
    if(g_inputHostEpoch!=saved.epoch || !journal.active || journal.transactionId!=saved.transaction) return;
    for(const auto& path:saved.paths)
        if(std::find(journal.hiddenByUs.begin(),journal.hiddenByUs.end(),path)==journal.hiddenByUs.end())
            journal.hiddenByUs.push_back(path);
}
