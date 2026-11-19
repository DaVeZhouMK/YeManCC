#pragma once
#include "legacy_native_visibility_parser.h"

// VC-GPD-NATIVE-VISIBILITY-20261111. Included after the shared target-return
// runtime. Caller owns host AND native-lease mutexes on the existing lifecycle
// worker. No new thread, timer, device cycle, global cloak or broad unhide.
namespace ymcc::inputHost::nativeVisibilityReturn {
using namespace ymcc::nativeRefreshWriter;
static bool legacyExamined=false; // schema1 migration is a single bounded scan
static std::optional<Visibility> legacyCandidate;
static bool legacyRejected=false;
static bool pending=false; // original g_inputHostMx; not a device/admission claim
static void note(const char* phase,const char* reason,const Visibility* v=nullptr,
        const std::string& path={},bool migrated=false) noexcept {
    try{appendNativeLifecycleLog("native-foreground-isolation",{
        {"phase",phase},{"reason",reason},{"policy","VC-GPD-NATIVE-VISIBILITY-20261111"},
        {"family","gpd"},{"sourceRoot",v?v->physicalRoot:std::string{}},
        {"transactionId",v?v->transactionId:std::string{}},{"pathInstanceId",path},
        {"legacyMigration",migrated},{"configurationOnly",true},{"physicalInputObserved",false},
        {"externalAccess","unverified"}});}catch(...){}
}
struct Context {
    std::uint64_t power=0,intent=0,request=0;
    std::string persona;
};
static bool current(const Context& context){
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    return g_inputHostMx.ownedByCurrentThread() && nativeForegroundProfile()==ymcc::nativeForeground::Profile::Gpd &&
        g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)==context.intent &&
        inputHostConfiguredPersona()==context.persona && view.requestId==context.request &&
        (!g_inputHostSwitchRecoveryStartDeadline || inputHostStartProtectionNow()<g_inputHostSwitchRecoveryStartDeadline) &&
        currentPowerGeneration()==context.power && g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready &&
        !g_externalPriorityVirtualSuspended.load(std::memory_order_acquire) && !g_shortcutRecordingActive.load(std::memory_order_acquire) &&
        g_inputReady.load(std::memory_order_acquire) && !g_msiNativeForegroundPowerHold.load(std::memory_order_acquire) &&
        !inputHostExitFenceRaised() && !g_exitRequested.load(std::memory_order_acquire) &&
        !g_inputCaptureStop.load(std::memory_order_acquire) && !g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire) &&
        !g_inputHostProcess && !g_inputHostHello && !g_inputHostPrepared && !g_inputHostPrepareMayHaveReachedHost &&
        !g_inputHostActive && !g_inputHostNeutralized && !g_hidHideVisibilityJournal.active &&
        !g_inputHostReleaseQuarantined && !g_msiSteamDeckReleaseDebt.required() &&
        !g_psEndpointOwnership.retirementRequired() && !msiForegroundIsolationBorrowBusy() &&
        !g_msiNativeForegroundLease.active && !g_msiNativeForegroundLease.restorePending &&
        !g_msiNativeForegroundLease.cycleProcess && !g_msiNativeForegroundLease.nativeVisibilitySession &&
        !g_msiNativeForegroundCyclePending.load(std::memory_order_acquire) && !g_postHideCycleOwned.load(std::memory_order_acquire);
}
static bool readLog(const std::wstring& path,std::string& out,bool& missing){
    missing=false;using ymcc::targetReturn::fileDetail::Handle;
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
        OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!file.valid()){missing=GetLastError()==ERROR_FILE_NOT_FOUND;return missing;}
    BY_HANDLE_FILE_INFORMATION before{},after{};DWORD error=0;
    if(!ymcc::targetReturn::fileDetail::info(file.value,before,error) || before.nFileSizeHigh ||
        before.nFileSizeLow>ymcc::legacyNativeVisibility::maxBytes)return false;
    out.assign(before.nFileSizeLow,'\0');DWORD read=0;
    if(!ReadFile(file.value,out.data(),before.nFileSizeLow,&read,nullptr)||read!=before.nFileSizeLow ||
        !ymcc::targetReturn::fileDetail::info(file.value,after,error) ||
        !ymcc::targetReturn::fileDetail::sameInfo(before,after))return false;
    return true;
}
static bool prepareLegacy(const HidHideVisibilitySnapshot& snapshot){
    if(legacyExamined)return !legacyRejected;
    // No hidden matching root requires no historical parsing or permanent gate.
    std::vector<std::string> roots;
    for(const auto& path:snapshot.hidden){const auto id=W2U(path);
        if(detail::visibilityInstance(id,true))hidHideAppendTarget(roots,id);
    }
    if(roots.empty()){legacyExamined=true;return true;}
    // Multiple current exact roots cannot be disambiguated by log filenames.
    if(roots.size()!=1){legacyExamined=true;note("legacy-not-adopted","legacy-root-ambiguous");return true;}
    std::vector<std::string> locations;
    for(const auto& value:msiForegroundIsolationLocations(roots.front()))locations.push_back(W2U(value));
    if(locations.empty()){note("legacy-not-adopted","legacy-current-location-unavailable");return true;}
    const auto dir=fspath::path(app_data_dir());std::string oldBytes,currentBytes;bool oldMissing=false,currentMissing=false;
    if(!readLog((dir/L"native-lifecycle.log.1").wstring(),oldBytes,oldMissing) ||
       !readLog((dir/L"native-lifecycle.log").wstring(),currentBytes,currentMissing)){
        note("legacy-not-adopted","legacy-log-read-unstable");return true;
    }
    std::vector<std::string_view> views;
    if(!oldMissing&&!oldBytes.empty())views.push_back(oldBytes);
    if(!currentMissing&&!currentBytes.empty())views.push_back(currentBytes);
    if(views.empty()){legacyExamined=true;return true;} // unknown foreign hiding remains untouched
    const auto result=ymcc::legacyNativeVisibility::extract(views,roots.front(),locations);
    legacyExamined=true;
    if(result.status==ymcc::legacyNativeVisibility::Status::Rejected){
        // Bad historical logs grant no cleanup authority. They also cannot
        // turn arbitrary inherited rules into a permanent global Start gate.
        note("legacy-not-adopted",result.reason.c_str());return true;
    }
    if(result.status==ymcc::legacyNativeVisibility::Status::Candidate)legacyCandidate=result.visibility;
    return true;
}
}
static bool inputHostNativeVisibilityColdReturnLocked(const char* reason){
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    // Original offline coordinator uses explicit inert leaves; it must never
    // accidentally open the user's store or device configuration in a test.
    if(g_msiNativeForegroundTestIo.coldReturn)return g_msiNativeForegroundTestIo.coldReturn(reason);
    if(!g_msiNativeForegroundTestIo.nativeVisibilityPersistence)return true;
#endif
    using namespace ymcc::inputHost::nativeVisibilityReturn;
    using ymcc::targetReturn::process::State;
    // Other families have no native-UI writer on this route. Their existing
    // common virtual/priority ledgers and hardware policies stay authoritative.
    if(nativeForegroundProfile()!=ymcc::nativeForeground::Profile::Gpd)return true;
    const Context context{currentPowerGeneration(),g_inputHostVirtualIntentRevision.load(std::memory_order_acquire),
        g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId,inputHostConfiguredPersona()};
    if(!current(context))return false;
    const auto machine=inputHostTargetReturnMachineBinding();
    // Cleanup authority requires the strong local-machine binding, never model
    // name, current PID alone or the weak optional discovery fallback.
    auto session=std::shared_ptr<Session>(new Session(Session::open(nativeRefreshWriterRecordPath(),machine)));
    if(!session->ready()){note("cold-return-deferred",session->error().reason.c_str());return false;}
    // A live ordinary Host or an unavailable observation is not evidence of
    // historical visibility duty. Preserve known debt; only the actual record
    // or authenticated legacy delta below can establish new pending duty.
    if(session->record()->nativeVisibility)pending=true;
    DWORD writerError=0;std::string writerReason;
    if(!LaunchScope::reconcilePrevious(*session,writerError,writerReason)){
        note("cold-return-deferred",writerReason.c_str());return false;
    }
    HidHideVisibilitySnapshot snapshot;
    if(session->record()->schemaVersion==kVisibilitySchemaVersion || session->openedFrom()!=LoadState::Valid)legacyExamined=true;
    if(!session->record()->nativeVisibility && !legacyExamined){
        if(!msiNativeForegroundSnapshot(snapshot)||!snapshot.valid)return false;
        if(!prepareLegacy(snapshot))return false;
    }
    const bool migration=!session->record()->nativeVisibility && legacyCandidate.has_value();
    const auto candidate=session->record()->nativeVisibility?session->record()->nativeVisibility:legacyCandidate;
    if(!candidate){pending=false;return true;}
    pending=true; // exact durable duty or authenticated legacy delta only
    if(machine.rfind("windows-machine-guid:",0)!=0){note("cold-return-deferred","strong-machine-binding-unavailable");return false;}
    const auto duty=*candidate;
    // An empty persisted set has no remaining path or device action to return.
    // Keep the original machine/session, exact dead owner and shared writer
    // fences; a removed/re-enumerated device cannot block metadata settlement.
    if(!migration && duty.paths.empty()){
        if(ymcc::targetReturn::process::probeRecordedOwner(duty.parent).state!=State::Dead){
            note("cold-return-deferred","empty-native-owner-live-or-unknown",&duty);return false;
        }
        if(!current(context) || !ymcc::inputHost::targetReturnRuntime::physicalWritersQuiescent()){
            note("cold-return-deferred","empty-native-context-or-writer-not-retired",&duty);return false;
        }
        if(!session->completeVisibility(session->record()->revision,duty.transactionId)){
            note("cold-return-deferred",session->error().reason.c_str(),&duty);return false;
        }
        pending=false;note("cold-empty-record-completed","empty-owned-path-set;no-device-action",&duty);return true;
    }
    const auto validScope=[&](){
        std::vector<std::wstring> locations;for(const auto& p:duty.locationPaths)locations.push_back(U2W(p));
        return current(context) && ymcc::msi::samePhysicalLocation(locations,msiForegroundIsolationLocations(duty.physicalRoot));
    };
    if(!validScope() || ymcc::targetReturn::process::probeRecordedOwner(duty.parent).state!=State::Dead){
        note("cold-return-deferred","native-owner-live-unknown-or-location-changed",&duty);return false;
    }
    if(!ymcc::inputHost::targetReturnRuntime::physicalWritersQuiescent()){
        note("cold-return-deferred","shared-physical-writer-not-retired",&duty);return false;
    }
    const auto absence=inputHostTargetReturnColdAbsence({});
    if(!absence.absent() || !validScope()){
        note("cold-return-deferred","virtual-target-absence-unproven",&duty);return false;
    }
    // Establish exact ancestry BEFORE persisting legacy debt. No path is
    // authorized by VID/PID grammar or a matching hidden-list key alone.
    for(const auto& path:duty.paths){
        const auto proof=inputHostTargetReturnVerifyPhysicalPath(duty.physicalRoot,path.instanceId);
        const bool missing=proof.reason==ymcc::targetReturn::physicalPath::Reason::TargetLocateFailed &&
            proof.configurationReturn==CR_NO_SUCH_DEVNODE && inputHostTargetReturnColdAbsence({path.instanceId}).absent();
        if((!proof.proven()&&!missing) || !validScope()){
            note("cold-return-deferred","exact-current-physical-path-unproven",&duty,path.instanceId);return false;
        }
    }
    if(migration){
        // One atomic commit records the WHOLE already-issued legacy set. A
        // partial reserve/mark migration must never lose the still-hidden tail.
        if(!session->migrateLegacyVisibility(session->record()->revision,duty))return false;
        legacyCandidate.reset();note("legacy-responsibility-persisted","owned-before-after-snapshot-delta;hide-api-receipt-unavailable",&duty,{},true);
    }
    for(const auto& path:duty.paths){
        if(!validScope() || !ymcc::inputHost::targetReturnRuntime::physicalWritersQuiescent())return false;
        if(path.phase==VisibilityPathPhase::MayHaveIssued){
            if(!msiNativeForegroundSnapshot(snapshot)||!snapshot.valid)return false;
            if(hidHideContainsPath(snapshot.hidden,path.instanceId)){
                // Persistence and an earlier observation are not permission to
                // write a newly rebound leaf. Recheck the exact path at dispatch.
                const auto proof=inputHostTargetReturnVerifyPhysicalPath(duty.physicalRoot,path.instanceId);
                const bool missing=proof.reason==ymcc::targetReturn::physicalPath::Reason::TargetLocateFailed &&
                    proof.configurationReturn==CR_NO_SUCH_DEVNODE && inputHostTargetReturnColdAbsence({path.instanceId}).absent();
                if(!validScope() || (!proof.proven()&&!missing))return false;
                // A lost CLI ACK is handled by the actual readback, never by
                // repeating a device cycle or asserting external visibility.
                (void)msiNativeForegroundVisibility(U2W(path.instanceId),false);
            }
            if(!validScope() || !ymcc::inputHost::targetReturnRuntime::physicalWritersQuiescent() ||
                !msiNativeForegroundSnapshot(snapshot)||!snapshot.valid ||
                hidHideContainsPath(snapshot.hidden,path.instanceId)){
                note("cold-return-deferred","owned-path-readback-not-returned",&duty,path.instanceId,migration);return false;
            }
        }
        if(!validScope() || !ymcc::inputHost::targetReturnRuntime::physicalWritersQuiescent() ||
            !session->settleVisibilityPath(session->record()->revision,duty.transactionId,path.instanceId))return false;
        note("cold-path-returned",reason,&duty,path.instanceId,migration);
    }
    if(!session->completeVisibility(session->record()->revision,duty.transactionId))return false;
    pending=false;note("cold-config-returned",reason,&duty,{},migration);return true;
}
static bool inputHostNativeVisibilityHistoryPending(){
    return ymcc::inputHost::nativeVisibilityReturn::pending;
}
