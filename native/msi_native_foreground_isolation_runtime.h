#pragma once
#include <string_view>
#include <charconv>
#include "process_efficiency.h"
#include "msi_native_foreground_isolation.h"
#include "native_foreground_source_policy.h"
#include "one_shot_helper_diagnostic.h"
#include "native_refresh_writer_runtime.h"
#include "virtual_target_return_physical_path.h"
#include <memory>
#include "msi_focus_visibility_evidence_20261012_fa1322.h"
#include "controller_isolation_hid_permit.h"

// All physical work uses the EXISTING serialized lifecycle owner. Window,
// source and power events only enqueue; no steady-state discovery/poller.
using MsiNativeForegroundXusbInventory=ymcc::msi::NativeForegroundXusbInventory;
static ymcc::nativeForeground::Profile nativeForegroundProfile() {
    using ymcc::nativeForeground::Profile;
    if(msiForegroundIsolationFamilyAllowed())return Profile::Msi;
    if(!g_machineIdentity.resolved)return Profile::Unsupported;
    // Other families cannot pass either final branch; skip vendor/model strings.
    if(!ymccIsRogFamily() && g_machineIdentity.family!=YmccFamily::Gpd)return Profile::Unsupported;
    const auto profile=ymcc::nativeForeground::machineProfile(g_machineIdentity.manufacturer,g_machineIdentity.product);
    if(profile==Profile::Rog && ymccIsRogFamily())return profile;
    if(profile==Profile::Gpd && g_machineIdentity.family==YmccFamily::Gpd)return profile;
    return Profile::Unsupported;
}
static bool nativeForegroundFamilyAllowed() {return nativeForegroundProfile()!=ymcc::nativeForeground::Profile::Unsupported;}
static bool nativeForegroundRootCandidate(const std::string& root) {return ymcc::nativeForeground::rootCandidate(nativeForegroundProfile(),root);}
static bool nativeForegroundGameInputAdmitted(bool sameRoot,bool virtualIdentity,unsigned vendor,unsigned product,unsigned page,unsigned usage) {
    return ymcc::nativeForeground::gameInputAdmitted(nativeForegroundProfile(),sameRoot,virtualIdentity,vendor,product,page,usage);
}
static bool nativeForegroundCurrentXusbTargetAdmitted(const std::string& root,const ymcc::msi::NativeForegroundXusbInterface& item) {
    return ymcc::nativeForeground::xusbTargetAdmitted(nativeForegroundProfile(),root,item);
}
static std::wstring nativeRefreshWriterRecordPath() {
    const auto directory=fspath::path(yemancc_data_dir())/L"controller-return";
    std::error_code error;fspath::create_directories(directory,error);
    return (directory/L"native-refresh-writer.json").wstring();
}
static json nativeRefreshWriterEvidence(const ymcc::nativeRefreshWriter::LaunchScope& scope,const char* phase) noexcept {
    try {
        const auto* attempt=scope.attempt();
        return {{"phase",phase},{"recordPath",W2U((fspath::path(yemancc_data_dir())/L"controller-return"/L"native-refresh-writer.json").wstring())},
            {"attemptId",attempt?attempt->attemptId:std::string{}},
            {"jobName",attempt?attempt->jobName:std::string{}},
            {"parentPid",attempt?attempt->parent.pid:0},
            {"childPid",attempt&&attempt->child?attempt->child->pid:0},
            {"reserved",scope.reserved()},{"settled",scope.settled()},
            {"error",scope.error()},{"reason",scope.reason()},
            {"writerFence","inherited-exclusive-owner-file-v1"},
            {"scope","helper writer only; no node-enabled or external input delivery proof"}};
    }catch(...){return nullptr;}
}
struct NativeForegroundRefreshTarget {HidHidePhysicalIdentity source;bool xusb=false;};
struct MsiColdReturnReceipt {
    uint64_t ticket=0,power=0,intent=0;
    std::string root;
    bool spent=false,accepted=false;
    bool observedRootHidden=false; // exact historical root seen hidden BEFORE its existing unhide
    bool preMutationRejected=false; // typed helper13 + exact writer terminal; never infer from generic3
    unsigned preMutationResumes=0; // at most one same-period helper resume; original deadline is unchanged
    std::optional<ymcc::targetReturn::Origin> observedOrigin; // never transfer an old hidden witness to a new same-root origin
};
struct MsiNativeForegroundLease {
    static constexpr int kPowerDeferred=3;
    ymcc::nativeForeground::Profile profile=ymcc::nativeForeground::Profile::Msi;
    std::vector<NativeForegroundRefreshTarget> refreshTargets;
    std::size_t nextRefreshTarget=0;
    bool refreshSeriesActive=false;
    bool nativeRootPortCycle=false; // ROG: existing exact-root USB CyclePort helper
    uint64_t refreshPlanAttemptEdge=0;
    bool refreshPlanForRestore=false;
    bool active=false,cloakChanged=false,restorePending=false,cycleForRestore=false;
    bool cycleIssued=false,cycleAccepted=false,syntheticCycle=false,unissuedWriterRetirement=false;
    bool configurationReturned=false,refreshFailed=false;
    bool virtualHandoffHeld=false;
    bool virtualContinuationEnded=false; // Start already rejected/ended or mandatory fault; not waiting work
    // Isolated selected-source HC cycle substep, owned by the original R3.
    bool coldReturnCycle=false,coldReturnAborted=false;
    uint64_t coldReturnTicket=0,coldReturnIntent=0,coldReturnDeadline=0;
    MsiColdReturnReceipt coldReturnReceipt; // one original OFF/root; retained across lease cleanup
    bool postHideCycle=false,postHideAborted=false,postHideReadmitAttempted=false,postHideReadmitted=false;
    uint64_t postHideTicket=0,postHideEpoch=0,postHideTerminalTick=0,postHideBaselineSequence=0,postHideSourceAfterTick=0;
    unsigned postHideValidatedTopology=0;
    HANDLE postHideHostProcess=nullptr,postHideCancelEvent=nullptr;
    uint64_t postHideDeadline=0;
    uint64_t postHideWakeSerial=0; // exact ordinary wake; never filled/renewed by observers
    std::string postHideRunId,postHidePersona;
    uint64_t virtualHandoffRequest=0; // original R3 request; never a new timeout/retry
    uint64_t refreshAttemptEdge=0;
    bool helperExitQueryAttempted=false,helperExitQuerySucceeded=false;
    DWORD helperExitQueryError=0;
    DWORD helperExitCode=STILL_ACTIVE,helperError=0;
    std::string helperFailure;
    std::string helperExePath,helperExeSha256,helperDllPath,helperDllSha256;
    std::vector<std::string> deferredVirtualWork;
    uint64_t power=0,edge=0,attemptedEdge=0,suspendSerial=0;
    std::string root;
    std::vector<std::wstring> locations,owned;
    std::vector<std::string> xusbTargets;
    std::vector<std::pair<HidHidePhysicalIdentity,uint64_t>> pendingGameSources;
    std::vector<std::pair<std::string,uint64_t>> gameSourceAttempts;
    std::string helperGameTarget;
    bool helperXusbTarget=false;
    std::string gameSourceCandidate;
    uint64_t gameSourceCacheGeneration=0;
    HidHideVisibilitySnapshot before;
    std::vector<std::wstring> diagnosticSelectedPaths;
    bool diagnosticSelectedPathsComplete=false;
    json diagnosticBeforeConfiguration,diagnosticHiddenConfiguration,diagnosticReturnConfiguration; // null = not captured, no proof
    HANDLE cycleProcess=nullptr;
    std::unique_ptr<ymcc::nativeRefreshWriter::LaunchScope> nativeRefreshWriter; // GPD only, same root owner
    json nativeRefreshWriterDiagnostic;
    std::string retiredNativeWriterAttempt; // set only after exact durable writer retirement; never diagnostic authority
    // Same owner file as the helper, retained from BEFORE hide until readback
    // return or a verified durable Hosted transfer. Never infer this from a list.
    std::shared_ptr<ymcc::nativeRefreshWriter::Session> nativeVisibilitySession;
    std::string nativeVisibilityTransaction;
    std::unique_ptr<ymcc::input::OneShotHelperDiagnostic> helperDiagnosticCapture;
    json helperDiagnostic;
};
static std::mutex g_msiNativeForegroundMx;
static MsiNativeForegroundLease g_msiNativeForegroundLease;
static std::atomic<bool> g_postHideCycleOwned{false};
static std::atomic<bool> g_coldReturnCycleOwned{false}; // cheap metadata gate; same sole lease
static thread_local std::shared_ptr<ymcc::nativeRefreshWriter::Session> g_postHideSharedWriterSession;
static std::atomic<uint64_t> g_msiNativeForegroundEdge{1};
static std::atomic<bool> g_msiNativeForegroundCyclePending{false};
static std::atomic<bool> g_msiNativeForegroundWorkPowerDeferred{false};
static std::atomic<bool> g_msiNativeForegroundResumePending{false};
static std::atomic<bool> g_msiNativeForegroundDesiredUi{false};
static std::atomic<bool> g_msiNativeForegroundSourceRetryNeeded{false};
static std::atomic<bool> g_msiNativeForegroundVirtualYieldPending{false};
static std::atomic<bool> g_msiNativeForegroundWatchGameSources{false};
static std::atomic<uint64_t> g_msiNativeForegroundSourceRetryGeneration{0};
static std::atomic<bool> g_msiNativeForegroundPowerHold{false};
// UI power broadcasts and lifecycle reconciliation share only this short scalar
// transaction. Never call UI/queue/device/Host operations while it is locked.
static std::mutex g_msiNativeForegroundPowerMx;
static uint64_t g_msiNativeForegroundLastReadySuspendSerial=0;
// A late suspend broadcast can belong to the generation already resuming.
// Remember the actual shared transition; generation inequality alone is not
// a resume proof and must not permanently latch this private output gate.
static std::atomic<bool> g_msiNativeForegroundPowerTransitionObserved{false};
static std::atomic<uint64_t> g_msiNativeForegroundPowerHoldCommitTick{0};
static std::atomic<uint64_t> g_msiNativeForegroundSuspendedGeneration{0};
static std::atomic<uint64_t> g_msiNativeForegroundLastResumeGeneration{currentPowerGeneration()};
// Foreground changes of the owned library child do not send WM_ACTIVATE to
// the already-inactive main window. One MSI-only OS event hook closes that
// gap without periodic discovery, another thread, or changing the shared gate.
static constexpr UINT kMsiNativeForegroundObserverMessage=WM_APP+178;
static HWINEVENTHOOK g_msiNativeForegroundObserver=nullptr; // HWND owner thread only
static std::atomic<bool> g_msiNativeForegroundObserverRequested{false};
static std::atomic<bool> g_msiNativeForegroundObserverActive{false};
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
struct MsiNativeForegroundTestIo {
    std::function<std::vector<HidHidePhysicalIdentity>()> candidates;
    std::function<std::vector<HidHideInterfaceRecord>(const HidHidePhysicalIdentity&)> inventory;
    std::function<MsiNativeForegroundXusbInventory(const HidHidePhysicalIdentity&)> xusbInventory;
    std::function<bool(HidHideVisibilitySnapshot&)> snapshot;
    std::function<ULONGLONG()> clock;
    std::function<bool()> readerExemption;
    std::function<bool()> readerInverseOff;
    std::function<bool(const std::wstring&,bool)> visibility;
    std::function<bool(const HidHidePhysicalIdentity&,const std::vector<std::string>&,const HidHideVisibilitySnapshot&)> durableHide;
    std::function<void(const HidHideVisibilitySnapshot&)> durableReturn;
    bool nativeVisibilityPersistence=false; // opt-in real private-store consumer
    std::function<bool(const char*)> coldReturn; // no product store/device IO by default offline
    std::function<bool(const ymcc::targetReturn::ProcessIdentity&)> coldWriterDead;
    std::function<bool()> coldWritersQuiet;
    std::function<int(const std::vector<std::string>&)> coldCensus; // 0 absent,1 exact present,2 unknown
    std::function<bool(std::vector<std::string>&)> coldHidden;
    std::function<bool(const std::string&)> coldUnhide;
    std::function<bool(const std::string&,const std::string&)> coldPathProven;
    std::function<bool(const std::string&,const std::string&)> handoffPathProven;
    std::function<bool()> inventoryComplete;
    std::function<int(const HidHidePhysicalIdentity&,const std::wstring&)> gameRefresh;
    std::function<int(const HidHidePhysicalIdentity&,const std::wstring&)> xusbRefresh;
    std::function<bool(HANDLE,HidHidePhysicalIdentity&,uint64_t&)> arrivalSource;
    std::function<bool(bool)> cloak;
    std::function<void()> beforeCycleDispatch;
    std::function<int(const std::string&,const std::wstring&,bool)> cycle; // 0=accepted,1=pending,2=failed
    std::function<int()> poll; // -1=still live,0=accepted,2=failed
    std::function<ymcc::input::recoveryHealth::Source()> postHideSource;
    std::function<bool(const std::string&)> postHideRootStarted;
    std::function<int()> postHideReadmit; // offline IO boundary:0=admitted,1=read-only wait,2=failed
    std::function<bool()> postHideAdmitSelectedSource; // only the existing standard-admission leaf, never real device IO offline
    std::function<bool()> ui;
    std::function<void()> queue;
    std::function<void(const std::string&)> virtualQueue;
    std::function<void()> observerQueue;
    std::function<bool()> observerStart;
    std::function<void()> observerStop;
    std::function<void(const json&)> readOnlyAuditLog;
};
static MsiNativeForegroundTestIo g_msiNativeForegroundTestIo;
#endif
struct MsiNativeForegroundRetry {
    uint64_t power=0,edge=0;
    ULONGLONG started=0,due=0;
    unsigned attempts=0;
    uint64_t virtualYieldPower=0,virtualYieldEdge=0,virtualYieldRequest=0,virtualYieldSuspendSerial=0;
    ymcc::nativeForeground::Profile virtualYieldProfile=ymcc::nativeForeground::Profile::Unsupported;
};
static MsiNativeForegroundRetry g_msiNativeForegroundRetry;
static ULONGLONG msiNativeForegroundRetryTick() {
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.clock)return g_msiNativeForegroundTestIo.clock();
#endif
    return GetTickCount64();
}
static void msiNativeForegroundScheduleRetryLocked() {
    if(nativeForegroundProfile()!=ymcc::nativeForeground::Profile::Msi)return;
    auto& retry=g_msiNativeForegroundRetry;
    const auto power=currentPowerGeneration(),edge=g_msiNativeForegroundEdge.load(std::memory_order_acquire);
    const auto now=msiNativeForegroundRetryTick();
    if(retry.power!=power || retry.edge!=edge){
        retry.power=power;retry.edge=edge;retry.started=now;retry.due=0;retry.attempts=0;
    }
    if(retry.due || retry.attempts>=5 || now<retry.started || now-retry.started>=30000)return;
    retry.due=now+(1000ULL<<retry.attempts);
}
static ymcc::msi::NativeForegroundFacts msiNativeForegroundFacts();
static bool msiNativeForegroundRepairReaderLocked(uint64_t power,uint64_t edge) {
    const auto current=[&] {
        const auto facts=msiNativeForegroundFacts();
        return ymcc::msi::nativeForegroundMayEnter(facts) &&
            facts.currentPower==power && facts.currentEdge==edge;
    };
    if(!current())return false;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(!g_msiNativeForegroundTestIo.readerInverseOff || !g_msiNativeForegroundTestIo.readerInverseOff() || !current())return false;
    return g_msiNativeForegroundTestIo.readerExemption && g_msiNativeForegroundTestIo.readerExemption();
#else
    std::string inverse;DWORD exit=ERROR_GEN_FAILURE;
    if(!hidHideRun(L"--inv-state",&inverse,&exit) || exit!=ERROR_SUCCESS ||
        hidHideInverseApplicationListState(inverse)!="off" || !current())return false;
    const auto module=hidHideCurrentModulePath();
    return !module.empty() && hidHideRun(L"--app-reg \""+module+L"\"");
#endif
}
static bool msiNativeForegroundUiEligible() {
    if ((focusLightIsolationCurrent() || steamLightGamepadAttemptActive()) && !steamLightGamepadFallbackRequired() &&
        !radialHasInputOwner() && !customSteamLibraryInputSuppressed()) return false;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.ui) return g_controllerFrontendIsolationEnabled.load(std::memory_order_acquire) && g_msiNativeForegroundTestIo.ui();
#endif
    return gamepadUiOutputIsolationEligible(); // the SAME authoritative boundary, never an MSI alternative
}
static bool msiNativeForegroundSnapshot(HidHideVisibilitySnapshot& snapshot) {
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.snapshot) return g_msiNativeForegroundTestIo.snapshot(snapshot);
#endif
    snapshot={};
    if(ymStartSeamActive()){ymStartSeamTrace("visibility-cli-skip");return false;}
    std::string output;
    DWORD exitCode=1;
    if(!hidHideRun(L"--dev-list --app-list --cloak-state --inv-state --cancel",&output,&exitCode) || exitCode!=ERROR_SUCCESS)return false;
    HidHideVisibilitySnapshot captured;
    unsigned section=0;
    bool cloakObserved=false,inverseObserved=false;
    std::istringstream lines(output);
    std::string line;
    while(std::getline(lines,line)){
        line=trim_ascii(line);
        if(line.empty())continue;
        if(line.find('\0')!=std::string::npos)return false;
        const auto pathCommand=[&](const char* command){
            const auto prefix=std::string(command)+" \"";
            return line.rfind(prefix,0)==0 && line.size()>prefix.size()+1 && line.back()=='"' &&
                line.find('"',prefix.size())==line.size()-1;
        };
        if(section==0 && pathCommand("--dev-hide"))captured.hiddenRaw+=line+"\n";
        else if(section<=1 && pathCommand("--app-reg")){
            section=1;captured.applicationsRaw+=line+"\n";
        }else if(section<=1 && (line=="--cloak-on" || line=="--cloak-off")){
            section=2;cloakObserved=true;captured.cloakRaw=line;captured.cloaking=line=="--cloak-on";
        }else if(section==2 && (line=="--inv-on" || line=="--inv-off")){
            section=3;inverseObserved=true;captured.inverseRaw=line;
        }else return false;
    }
    if(!cloakObserved || !inverseObserved)return false;
    captured.hidden=hidHideParseCommandPaths(captured.hiddenRaw,"--dev-hide");
    captured.applications=hidHideParseCommandPaths(captured.applicationsRaw,"--app-reg");
    captured.valid=true;
    snapshot=std::move(captured);
    return true;
}
static bool msiNativeForegroundVisibility(const std::wstring& path,bool hide) {
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.visibility) return g_msiNativeForegroundTestIo.visibility(path,hide);
#endif
    return hidHideRun(std::wstring(hide?L"--dev-hide \"":L"--dev-unhide \"")+path+L"\"");
}
// VC-GPD-NATIVE-VISIBILITY-20261111: memory lease and durable responsibility
// have the same serial owner. Reserve -> may-have-issued -> readback return,
// or reserve Hosted replacement -> verify it -> transfer. Helper terminal is
// separate: configuration returns first even when the old helper is still live.
static bool msiNativeForegroundDurableHide(const HidHidePhysicalIdentity& source,
        const std::vector<std::string>& targets,const HidHideVisibilitySnapshot& before) {
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.durableHide)
        return g_msiNativeForegroundTestIo.durableHide(source,targets,before);
#endif
    const auto profile=nativeForegroundProfile();
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(profile==ymcc::nativeForeground::Profile::Gpd && !g_msiNativeForegroundTestIo.nativeVisibilityPersistence)return true;
#endif
    if(profile==ymcc::nativeForeground::Profile::Gpd){
        using namespace ymcc::nativeRefreshWriter;
        auto& lease=g_msiNativeForegroundLease;
        if(!before.valid || lease.root.empty() || lease.locations.empty() ||
            ascii_lower(source.baseContainerDeviceInstanceId)!=ascii_lower(lease.root))return false;
        std::vector<std::string> own;
        for(const auto& target:targets)if(!hidHideContainsPath(before.hidden,target))hidHideAppendTarget(own,target);
        if(own.empty())return true; // inherited configuration is never adopted
        if(!lease.nativeVisibilitySession){
            const auto machine=inputHostTargetReturnMachineBinding();
            // Do not create responsibility that a later cold owner cannot prove.
            // Missing metadata only skips this native isolation write.
            if(machine.rfind("windows-machine-guid:",0)!=0)return false;
            auto session=std::shared_ptr<Session>(new Session(Session::open(nativeRefreshWriterRecordPath(),machine)));
            DWORD error=0;std::string reason;
            if(!session->ready() || !LaunchScope::reconcilePrevious(*session,error,reason) || session->record()->nativeVisibility)return false;
            const auto parent=ymcc::targetReturn::process::capture(GetCurrentProcess());
            if(!parent)return false;
            static std::atomic<uint64_t> sequence{0};
            Visibility v;v.transactionId="nv-"+std::to_string(parent->pid)+"-"+parent->creationTime+"-"+
                std::to_string(GetTickCount64())+"-"+std::to_string(sequence.fetch_add(1));
            v.parent=*parent;v.physicalRoot=lease.root;v.beforeCloaking=before.cloaking;
            for(const auto& location:lease.locations)v.locationPaths.push_back(W2U(location));
            for(const auto& target:own)v.paths.push_back({target,VisibilityPathPhase::Reserved});
            if(!session->reserveVisibility(session->record()->revision,v))return false;
            lease.nativeVisibilityTransaction=v.transactionId;lease.nativeVisibilitySession=std::move(session);
            // Even context retirement immediately after persistence must retain
            // this owner, including reservations for which no IO was dispatched.
            lease.active=true;
        }else{
            auto& session=*lease.nativeVisibilitySession;
            const auto* record=session.record();
            if(!record || !record->nativeVisibility || record->nativeVisibility->transactionId!=lease.nativeVisibilityTransaction ||
                ascii_lower(record->nativeVisibility->physicalRoot)!=ascii_lower(lease.root))return false;
            for(const auto& target:own){
                const auto& paths=session.record()->nativeVisibility->paths;
                const bool recorded=std::any_of(paths.begin(),paths.end(),[&](const auto& p){return ascii_lower(p.instanceId)==ascii_lower(target);});
                if(!recorded && !session.reserveVisibilityPath(session.record()->revision,lease.nativeVisibilityTransaction,target))return false;
            }
        }
        return true;
    }
    if(profile!=ymcc::nativeForeground::Profile::Msi)return true;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    return true;
#else
    return msiCrashRecoveryBeforeNativeHide(source,targets,before,"native-foreground:"+
        std::to_string(g_msiNativeForegroundLease.power)+":"+std::to_string(g_msiNativeForegroundLease.edge));
#endif
}
static bool msiNativeForegroundBeforeHideDispatch(const std::string& target){
    auto& lease=g_msiNativeForegroundLease;
    if(lease.profile!=ymcc::nativeForeground::Profile::Gpd)return true;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(!g_msiNativeForegroundTestIo.nativeVisibilityPersistence)return true;
#endif
    auto session=lease.nativeVisibilitySession;
    return session && session->ready() && session->markVisibilityMayHaveIssued(
        session->record()->revision,lease.nativeVisibilityTransaction,target);
}
static bool msiNativeForegroundDurableReturn(const HidHideVisibilitySnapshot& returned) {
    auto& lease=g_msiNativeForegroundLease;
    if(lease.nativeVisibilitySession){
        auto& session=*lease.nativeVisibilitySession;
        const auto* record=session.record();
        if(!returned.valid || !record || !record->nativeVisibility ||
            record->nativeVisibility->transactionId!=lease.nativeVisibilityTransaction)return false;
        const auto paths=record->nativeVisibility->paths;
        for(const auto& path:paths){
            if(path.phase==ymcc::nativeRefreshWriter::VisibilityPathPhase::MayHaveIssued && hidHideContainsPath(returned.hidden,path.instanceId))return false;
            if(!session.settleVisibilityPath(session.record()->revision,lease.nativeVisibilityTransaction,path.instanceId))return false;
        }
        // A live helper still retains this SAME durable owner after config return.
        if(!session.record()->active && !session.completeVisibility(session.record()->revision,lease.nativeVisibilityTransaction))return false;
        return true;
    }
    if(nativeForegroundProfile()!=ymcc::nativeForeground::Profile::Msi)return true;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.durableReturn)g_msiNativeForegroundTestIo.durableReturn(returned);
#else
    msiCrashRecoveryAfterOwnedRestore(returned,returned.valid);
#endif
    return true;
}
static bool msiNativeForegroundCloak(bool enabled) {
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.cloak) return g_msiNativeForegroundTestIo.cloak(enabled);
#endif
    return hidHideRun(enabled?L"--cloak-on":L"--cloak-off");
}
// Decode only already-captured snapshots. Never claims inherited entries or
// alters hide/restore admission, targets, refresh budgets or external proof.
static json msiNativeForegroundConfigurationEvidence(const HidHideVisibilitySnapshot& snap,
    const std::vector<std::wstring>& selected,bool complete,const std::vector<std::wstring>& owned) noexcept {
    try {
    using namespace ymcc::msi::focus_evidence_20261012_fa1322;
    Facts facts;facts.snapshotRead=snap.valid;facts.selectedPathsComplete=complete;
    facts.cloakKnown=snap.valid;facts.cloakOn=snap.cloaking;
    const auto inverse=hidHideInverseApplicationListState(snap.inverseRaw);
    facts.inverseKnown=inverse=="off"||inverse=="on";facts.inverseOn=inverse=="on";
    facts.readerRegistrationKnown=snap.valid;
    facts.ymccReaderExempt=snap.valid&&hidHideContainsPath(snap.applications,W2U(hidHideCurrentModulePath()));
    facts.selectedPaths=selected;facts.hiddenPaths=snap.hidden;facts.ownedPaths=owned;
    const auto e=decode(facts);
    auto knowledge=[](Knowledge value){return value==Knowledge::Yes?"yes":value==Knowledge::No?"no":"unknown";};
    const char* configuration=e.selectedConfiguration==SelectedConfiguration::NoFilterForSelectedPaths?"no-filter-for-selected-paths":
        e.selectedConfiguration==SelectedConfiguration::HiddenFromUnlistedReaders?"hidden-from-unlisted-readers":"unknown";
    return {{"schema",1},{"selectedPathsComplete",complete},{"snapshotRead",snap.valid},
        {"ownedEntriesCurrentlyAbsent",owned.empty()?"no-owned-entries":knowledge(e.ownedEntriesReturned)},
        {"selectedConfiguration",configuration},{"selectedHiddenCount",e.selectedHiddenCount},
        {"unownedSelectedHiddenCount",e.unownedSelectedHiddenCount},{"ownedStillHiddenCount",e.ownedStillHiddenCount},
        {"ymccExemptReaderCanBypassConfiguration",knowledge(e.ymccExemptReaderCanBypassConfiguration)},
        {"externalInputDeliveryProven",false},{"inheritedEntryOwnershipProven",false},
        {"scope","selected-source configuration only; no authority to clear inherited/foreign entries"}};
    } catch(...) { return nullptr; }
}
static void msiNativeForegroundAppendCycleDetails(json& detail,const MsiNativeForegroundLease& s) {
    // Version in the field name; bits 1/2 replace only the exact inactive defaults.
    unsigned defaults=0;
    detail["coldReturnCycle"]=s.coldReturnCycle;
    detail["postHideCycle"]=s.postHideCycle;
    if(!s.coldReturnCycle && !s.coldReturnTicket && !s.coldReturnDeadline && !s.coldReturnAborted &&
        !s.coldReturnReceipt.spent && !s.coldReturnReceipt.accepted &&
        !s.coldReturnReceipt.preMutationRejected && !s.coldReturnReceipt.preMutationResumes)defaults|=1;
    else {
        detail["coldReturnTicket"]=s.coldReturnTicket;
        detail["coldReturnDeadline"]=s.coldReturnDeadline;
        detail["coldReturnAborted"]=s.coldReturnAborted;
        detail["coldReturnRefreshSpent"]=s.coldReturnReceipt.spent;
        detail["coldReturnRefreshAccepted"]=s.coldReturnReceipt.accepted;
        detail["coldReturnPreMutationRejected"]=s.coldReturnReceipt.preMutationRejected;
        detail["coldReturnPreMutationResumes"]=s.coldReturnReceipt.preMutationResumes;
    }
    if(!s.postHideCycle && !s.postHideTicket && !s.postHideTerminalTick && !s.postHideBaselineSequence &&
        !s.postHideReadmitted && !s.postHideSourceAfterTick && !s.postHideDeadline && !s.postHideCancelEvent)defaults|=2;
    else {
        detail["postHideTicket"]=s.postHideTicket;
        detail["postHideTerminalTick"]=s.postHideTerminalTick;
        detail["postHideSourceBaselineSequence"]=s.postHideBaselineSequence;
        detail["postHideReadmitted"]=s.postHideReadmitted;
        detail["postHideSourceAfterTick"]=s.postHideSourceAfterTick;
        detail["postHideDeadline"]=s.postHideDeadline;
        detail["postHideCancellationAvailable"]=s.postHideCancelEvent!=nullptr;
    }
    if(defaults)detail["cycleDetailsDefaultsV1"]=defaults;
}

static void msiNativeForegroundReceipt(const char* phase,const char* reason) {
    if(strcmp(phase,"restore-failed")==0 || strcmp(phase,"deferred")==0)msiNativeForegroundScheduleRetryLocked();
    else if(strcmp(phase,"restored")==0 || strcmp(phase,"config-cycle-accepted")==0)g_msiNativeForegroundRetry.due=0;
    const auto& s=g_msiNativeForegroundLease; // caller owns native lease lock
    json targets=json::array(),locations=json::array();
    for(const auto& path:s.owned)targets.push_back(W2U(path));
    for(const auto& path:s.locations)locations.push_back(W2U(path));
    json detail={
        {"family",ymcc::nativeForeground::name(s.profile)},{"rootPortCycleAllowed",s.profile==ymcc::nativeForeground::Profile::Msi || s.postHideCycle || s.coldReturnCycle || s.nativeRootPortCycle},
        {"refreshSeriesActive",s.refreshSeriesActive},{"refreshTargetIndex",s.nextRefreshTarget},{"refreshTargetCount",s.refreshTargets.size()},
        {"phase",phase},{"reason",reason},{"virtualTargetCreated",false},{"modeWrite",false},
        {"selfHealAttempts",g_msiNativeForegroundRetry.attempts},{"selfHealDueTick",g_msiNativeForegroundRetry.due},
        {"selfHealMaxAttempts",5},{"selfHealMaxMs",30000},
        {"isolationMode",s.nativeRootPortCycle?"visibility-and-root-port-cycle":"visibility-and-device-refresh"},
        {"edge",s.edge},{"powerGeneration",s.power},{"sourceRoot",s.root},
        {"powerHold",g_msiNativeForegroundPowerHold.load(std::memory_order_acquire)},
        {"resumePending",g_msiNativeForegroundResumePending.load(std::memory_order_acquire)},
        {"sharedPowerPhase",powerLifecycleName(g_powerLifecycle.load(std::memory_order_acquire))},
        {"currentPowerGeneration",g_powerGeneration.load(std::memory_order_acquire)},
        {"suspendedGeneration",g_msiNativeForegroundSuspendedGeneration.load(std::memory_order_acquire)},
        {"powerTransitionObserved",g_msiNativeForegroundPowerTransitionObserved.load(std::memory_order_acquire)},
        {"powerHoldCommitBaseline",g_msiNativeForegroundPowerHoldCommitTick.load(std::memory_order_acquire)},
        {"sharedResumeReadyGeneration",g_resumeReadyGeneration.load(std::memory_order_acquire)},
        {"sharedResumeCommitTick",g_lastPowerResumeCommitTick.load(std::memory_order_acquire)},
        {"ownedPaths",targets},{"locationPaths",locations},{"cycleAccepted",s.cycleAccepted},
        {"xusbGameTargets",s.xusbTargets},{"xusbTargetEvidence","active-XUSB-interface-and-exact-PnP-ancestor"},
        {"helperOperation",s.coldReturnCycle?"selected-xusb-cold-return-port-cycle":s.nativeRootPortCycle?"verified-physical-root-port-cycle":s.postHideCycle?"selected-xusb-post-hide-port-cycle":s.helperGameTarget.empty()?"physical-root-cycle":s.helperXusbTarget?"xusb-game-interface-refresh":"game-interface-refresh"},
        {"helperGameTarget",s.helperGameTarget},{"gameSourceAttemptCount",s.gameSourceAttempts.size()},
        {"externalSuppressionVerified",false},
        {"gameSourceCandidate",s.gameSourceCandidate},{"gameSourceCacheGeneration",s.gameSourceCacheGeneration},
        {"virtualHandoffHeld",s.virtualHandoffHeld},{"virtualHandoffRequest",s.virtualHandoffRequest},
        {"configurationReturned",s.configurationReturned},{ "beforeConfigurationEvidence",s.diagnosticBeforeConfiguration},{"hiddenConfigurationEvidence",s.diagnosticHiddenConfiguration},{"returnConfigurationEvidence",s.diagnosticReturnConfiguration},{"refreshFailed",s.refreshFailed},
        {"helperPending",g_msiNativeForegroundCyclePending.load(std::memory_order_acquire)},
        {"inputBoundary",controllerIsolationPhysicalEvidence()},{"gameReleaseReceiptProven",false},
        {"helperExitCode",s.helperExitCode},{"helperError",s.helperError},{"helperFailure",s.helperFailure},
        {"helperExitQuery",{{"schema",1},{"api","GetExitCodeProcess"},{"attempted",s.helperExitQueryAttempted},
            {"succeeded",s.helperExitQueryAttempted?json(s.helperExitQuerySucceeded):json(nullptr)},
            {"errorDomain",s.helperExitQueryAttempted&&!s.helperExitQuerySucceeded?json("win32-last-error"):json(nullptr)},
            {"error",s.helperExitQueryAttempted&&!s.helperExitQuerySucceeded?json(s.helperExitQueryError):json(nullptr)}}},
        {"helperDiagnostic",s.helperDiagnostic},{"nativeRefreshWriter",s.nativeRefreshWriterDiagnostic},
        {"helperExePath",s.helperExePath},{"helperExeSha256",s.helperExeSha256},
        {"helperDllPath",s.helperDllPath},{"helperDllSha256",s.helperDllSha256},
        {"helperIdentityScope","on-disk-before-launch-not-loaded-image-proof"},
        {"cycleForRestore",s.cycleForRestore},{"refreshAttemptEdge",s.refreshAttemptEdge},
        {"deferredVirtualWork",s.deferredVirtualWork},{"virtualContinuationEnded",s.virtualContinuationEnded},
        {"verificationScope","config-and-cycle-command-only"},{"externalInputBlocked","unverified"}};
    msiNativeForegroundAppendCycleDetails(detail,s);
    appendNativeLifecycleLog(s.profile==ymcc::nativeForeground::Profile::Msi?"msi-native-foreground-isolation":"native-foreground-isolation",detail);
}
static void msiNativeForegroundQueue() {
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.queue){g_msiNativeForegroundTestIo.queue();return;}
#endif
    (void)inputHostQueueLifecycleWork("msi-native-foreground");
}
static void msiNativeForegroundIdentityReady() {
    if(!ymcc::msi::kNativeForegroundHidHideBorrowEnabled)return;
    if(!nativeForegroundFamilyAllowed() || g_msiNativeForegroundObserverRequested.load(std::memory_order_acquire))return;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.observerQueue){
        if(!g_msiNativeForegroundObserverRequested.exchange(true))g_msiNativeForegroundTestIo.observerQueue();return;
    }
#endif
    if(!g_hwnd)return; // boot calls again after the real HWND exists
    if(g_msiNativeForegroundObserverRequested.exchange(true))return;
    if(!PostMessageW(g_hwnd,kMsiNativeForegroundObserverMessage,0,0))g_msiNativeForegroundObserverRequested=false;
}
static void msiNativeForegroundUiEvent(bool force=false,bool sourceReturn=false) {
    if(!nativeForegroundFamilyAllowed())return;
    msiNativeForegroundIdentityReady();
    const bool owned=msiNativeForegroundUiEligible();
    const bool previous=g_msiNativeForegroundDesiredUi.exchange(owned,std::memory_order_acq_rel);
    if(!force && previous==owned)return; // resize/show duplicates do not cycle an active pad
    if(previous!=owned || (force && !sourceReturn))g_msiNativeForegroundSourceRetryGeneration=0; // UI/config/power boundary, not source-forced retry
    g_msiNativeForegroundEdge.fetch_add(1,std::memory_order_acq_rel);
    msiNativeForegroundQueue();
}
// Existing RawInput identity resolution supplies metadata, not a hide command.
// Warm source return can retry a failed discovery ONCE per cache generation,
// even if foreground did not change. No steady PnP scan/driver I/O is added.
static void msiNativeForegroundObserveSource(const HidHidePhysicalIdentity& source,uint64_t sourceGeneration) {
    if((!g_msiNativeForegroundSourceRetryNeeded.load(std::memory_order_acquire) &&
        !g_msiNativeForegroundWatchGameSources.load(std::memory_order_acquire)) || !sourceGeneration ||
        !nativeForegroundFamilyAllowed())return;
    if(!source.resolved || !nativeForegroundGameInputAdmitted(true,
        hidHideIsVirtualIdentity(source.deviceInstanceId,source.baseContainerDeviceInstanceId),
        source.vendorId,source.productId,source.usagePage,source.usage) ||
        !nativeForegroundRootCandidate(source.baseContainerDeviceInstanceId))return;
    if(inputHostConfiguredPersona()!="disabled" || g_msiNativeForegroundPowerHold || !g_inputReady ||
        g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready || inputHostExitFenceRaised())return;
    // Never block the input reader behind a slow CLI/port operation. Recheck
    // under the same lease lock so a stale 'needed' observation cannot cycle an
    // already accepted lease. All identity/cache locks at the caller are free.
    std::unique_lock<std::mutex> leaseLock(g_msiNativeForegroundMx,std::try_to_lock);
    if(!leaseLock.owns_lock() || !msiNativeForegroundUiEligible())return;
    auto& lease=g_msiNativeForegroundLease;
    if(lease.active){
        if(lease.restorePending || lease.configurationReturned ||
            ascii_lower(source.baseContainerDeviceInstanceId)!=ascii_lower(lease.root) ||
            hidHideContainsPath(lease.diagnosticSelectedPaths,source.deviceInstanceId) ||
            lease.gameSourceAttempts.size()>=16)return;
        const auto instance=ascii_lower(source.deviceInstanceId);
        for(const auto& attempted:lease.gameSourceAttempts)
            if(attempted.first==instance && sourceGeneration<=attempted.second)return;
        for(const auto& pending:lease.pendingGameSources)
            if(ascii_lower(pending.first.deviceInstanceId)==instance)return;
        lease.gameSourceAttempts.emplace_back(instance,sourceGeneration);
        lease.pendingGameSources.emplace_back(source,sourceGeneration);
        msiNativeForegroundQueue();return;
    }
    if(!g_msiNativeForegroundSourceRetryNeeded)return;
    const auto consumed=g_msiNativeForegroundSourceRetryGeneration.load(std::memory_order_acquire);
    if(sourceGeneration<=consumed)return;
    g_msiNativeForegroundSourceRetryGeneration=sourceGeneration;
    appendNativeLifecycleLog("msi-native-foreground-source-retry",{{"sourceCacheGeneration",sourceGeneration},
        {"sourceRoot",source.baseContainerDeviceInstanceId},{"origin","resolved-source-return-not-user-input"},
        {"driverWrite",false},{"boundedPerSourceGeneration",true}});
    msiNativeForegroundUiEvent(true,true); // source retry must not reset its own generation budget
}
static void msiNativeForegroundSourceArrival(HANDLE rawDevice) {
    if((!g_msiNativeForegroundSourceRetryNeeded.load(std::memory_order_acquire) &&
        !g_msiNativeForegroundWatchGameSources.load(std::memory_order_acquire)) ||
        !nativeForegroundFamilyAllowed() || inputHostConfiguredPersona()!="disabled" ||
        g_realStickTest.virtualTargetEnabled || g_msiNativeForegroundPowerHold || !g_inputReady ||
        g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready || inputHostExitFenceRaised() ||
        !msiNativeForegroundUiEligible())return;
    HidHidePhysicalIdentity source;uint64_t generation=0;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.arrivalSource){
        if(g_msiNativeForegroundTestIo.arrivalSource(rawDevice,source,generation))
            msiNativeForegroundObserveSource(source,generation);
        return;
    }
#endif
    RID_DEVICE_INFO information{};information.cbSize=sizeof(information);UINT bytes=sizeof(information);
    if(GetRawInputDeviceInfoW(rawDevice,RIDI_DEVICEINFO,&information,&bytes)==static_cast<UINT>(-1) ||
        information.dwType!=RIM_TYPEHID || !nativeForegroundGameInputAdmitted(true,false,
            information.hid.dwVendorId,information.hid.dwProductId,information.hid.usUsagePage,information.hid.usUsage))return;
    if(hidHideResolveRawInputIdentity(rawDevice,source,&generation))msiNativeForegroundObserveSource(source,generation);
}
static void CALLBACK msiNativeForegroundObserved(HWINEVENTHOOK,DWORD event,HWND,LONG,LONG,DWORD,DWORD) {
    if(event==EVENT_SYSTEM_FOREGROUND && g_msiNativeForegroundObserverActive.load(std::memory_order_acquire)) {
        focusLightForegroundChanged();
        focusLightRecheckOnUi(true);
    }
}
static void msiNativeForegroundStartObserver() {
    if(!nativeForegroundFamilyAllowed() || g_msiNativeForegroundObserverActive.load(std::memory_order_acquire))return;
    bool active=false;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.observerStart)active=g_msiNativeForegroundTestIo.observerStart();else
#endif
    {
        g_msiNativeForegroundObserver=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,
            msiNativeForegroundObserved,0,0,WINEVENT_OUTOFCONTEXT);
        active=g_msiNativeForegroundObserver!=nullptr;
    }
    g_msiNativeForegroundObserverActive=active;
    appendNativeLifecycleLog("msi-native-foreground-observer",{{"active",active},{"error",active?0:GetLastError()},
        {"eventDriven",true},{"sharedForegroundPolicyChanged",false}});
    if(active)msiNativeForegroundUiEvent(true);
}
static void msiNativeForegroundStopObserver() {
    const bool wasActive=g_msiNativeForegroundObserverActive.exchange(false);
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.observerStop){if(wasActive)g_msiNativeForegroundTestIo.observerStop();return;}
#endif
    if(g_msiNativeForegroundObserver){UnhookWinEvent(g_msiNativeForegroundObserver);g_msiNativeForegroundObserver=nullptr;}
}
static void msiNativeForegroundPowerEvent(WPARAM event) {
    if(!nativeForegroundFamilyAllowed())return;
    if(event!=PBT_APMSUSPEND && event!=PBT_APMRESUMEAUTOMATIC &&
       event!=PBT_APMRESUMESUSPEND && event!=PBT_APMRESUMECRITICAL)return;
    std::unique_lock powerLock(g_msiNativeForegroundPowerMx);
    if(event==PBT_APMSUSPEND){
        // This hook precedes the shared power handler: close our private gate
        // immediately, not after its Ready/Suspending publication races us.
        g_msiNativeForegroundPowerHold=true;
        g_msiNativeForegroundSuspendedGeneration=g_powerGeneration.load(std::memory_order_acquire);
        g_msiNativeForegroundResumePending=false;
        g_msiNativeForegroundPowerHoldCommitTick=g_lastPowerResumeCommitTick.load(std::memory_order_acquire);
        g_msiNativeForegroundPowerTransitionObserved=
            g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready;
    }else if(event==PBT_APMRESUMEAUTOMATIC || event==PBT_APMRESUMESUSPEND || event==PBT_APMRESUMECRITICAL){
        // Resume notification runs AFTER the unchanged shared power handler.
        // Several PBT notifications in one generation are one event, not a
        // request to unhide/re-hide/cycle the same physical source again.
        const auto generation=g_powerGeneration.load(std::memory_order_acquire);
        if(!g_msiNativeForegroundPowerHold && !g_msiNativeForegroundResumePending)return;
        if(g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready)
            g_msiNativeForegroundPowerTransitionObserved=true;
        if(g_msiNativeForegroundResumePending.load(std::memory_order_acquire))return;
        g_msiNativeForegroundPowerHold=true;
        if(g_msiNativeForegroundSuspendedGeneration.load(std::memory_order_acquire)==0)
            g_msiNativeForegroundSuspendedGeneration=generation?generation-1:0;
        g_msiNativeForegroundResumePending=true;
    }
    powerLock.unlock();
    msiNativeForegroundUiEvent(true);
}
// Positive CURRENT common-owner completion; a missed or reordered raw resume
// cannot retain a private gate after this authoritative Ready. Only power
// scalars are cleared here: helper/target/configuration return debts remain.
static bool msiNativeForegroundSharedPowerReady(const ymcc::sleeppower::SharedPowerV1& power) {
    if(!nativeForegroundFamilyAllowed())return true;
    std::unique_lock powerLock(g_msiNativeForegroundPowerMx);
    if(g_exitRequested.load(std::memory_order_acquire))return true;
    if(!sharedPowerLifecycleCurrent(power))return false;
    const bool changed=g_msiNativeForegroundPowerHold.load(std::memory_order_acquire) ||
        g_msiNativeForegroundResumePending.load(std::memory_order_acquire) ||
        g_msiNativeForegroundLastResumeGeneration.load(std::memory_order_acquire)!=power.generation ||
        g_msiNativeForegroundLastReadySuspendSerial!=power.suspendSerial;
    g_msiNativeForegroundLastReadySuspendSerial=power.suspendSerial;
    g_msiNativeForegroundPowerHold=false;g_msiNativeForegroundResumePending=false;
    g_msiNativeForegroundPowerTransitionObserved=false;
    g_msiNativeForegroundLastResumeGeneration=power.generation;
    powerLock.unlock();
    if(changed)msiNativeForegroundUiEvent(true);
    else if(g_inputReady.load(std::memory_order_acquire) &&
        g_msiNativeForegroundWorkPowerDeferred.exchange(false,std::memory_order_acq_rel))msiNativeForegroundQueue();
    return true;
}

// Existing lifecycle maintenance only; no device reads, new timer or mode write.
// The shared Ready/reader facts own readiness. A real same-generation resume
// after an observed transition is as valid as a different-generation resume.
// A raw resume while the pre-suspend shared Ready is unchanged still waits.
static bool msiNativeForegroundTryReleasePowerHold() {
    if(!g_msiNativeForegroundPowerHold.load(std::memory_order_acquire) &&
       !g_msiNativeForegroundResumePending.load(std::memory_order_acquire))return false;
    std::unique_lock powerLock(g_msiNativeForegroundPowerMx,std::try_to_lock);
    if(!powerLock.owns_lock())return false;
    if(g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready){
        g_msiNativeForegroundPowerTransitionObserved.store(true,std::memory_order_release);
        return false;
    }
    if(!g_inputReady.load(std::memory_order_acquire))return false;
    const auto generation=g_powerGeneration.load(std::memory_order_acquire);
    const auto committed=g_lastPowerResumeCommitTick.load(std::memory_order_acquire);
    // Missing raw resume callbacks cannot retain this private latch after the
    // existing shared owner actually commits a NEW Ready for this generation.
    // An unchanged pre-suspend Ready/commit is deliberately not enough.
    const bool sharedCommit=committed &&
        committed!=g_msiNativeForegroundPowerHoldCommitTick.load(std::memory_order_acquire) &&
        g_resumeReadyGeneration.load(std::memory_order_acquire)==generation;
    const bool broadcastResume=g_msiNativeForegroundResumePending.load(std::memory_order_acquire) &&
        (generation!=g_msiNativeForegroundSuspendedGeneration.load(std::memory_order_acquire) ||
         g_msiNativeForegroundPowerTransitionObserved.load(std::memory_order_acquire));
    if(!broadcastResume && !sharedCommit)return false;
    // Local Ready and input readiness can lead the public write gate or lag
    // a newer suspend receipt. Keep this hold until the common owner admits it.
    ymcc::sleeppower::SharedPowerV1 power;
    if(!captureSharedPowerLifecycle(power) || power.generation!=generation)return false;
    g_msiNativeForegroundLastReadySuspendSerial=power.suspendSerial;
    g_msiNativeForegroundResumePending=false;g_msiNativeForegroundPowerHold=false;
    g_msiNativeForegroundPowerTransitionObserved=false;
    g_msiNativeForegroundLastResumeGeneration=power.generation;
    powerLock.unlock();
    msiNativeForegroundUiEvent(true);
    return true;
}
// Cached historical ownership is read under the same Host mutex. Defined by
// the existing target-return owner later in this translation unit.
static bool inputHostTargetReturnHistorySettledLocked();
static bool msiNativeForegroundVirtualWorkHasPriority() {
    return g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)!=0 ||
        g_virtualStartProtection.view(inputHostStartProtectionNow()).locked;
}
// caller holds Host mutex: native-only cannot race a target build/teardown.
static ymcc::msi::NativeForegroundFacts msiNativeForegroundFacts() {
    ymcc::msi::NativeForegroundFacts f;
    f.knownBoard=nativeForegroundFamilyAllowed();
    f.nativeChoice=inputHostConfiguredPersona()=="disabled";
    f.virtualEnabled=g_realStickTest.virtualTargetEnabled;
    f.hostAbsent=g_inputHostProcess==nullptr && !g_hidHideVisibilityJournal.active && !g_inputHostReleaseQuarantined &&
        inputHostTargetReturnHistorySettledLocked(); // the historical return owner goes first
    f.powerReady=!g_msiNativeForegroundPowerHold.load(std::memory_order_acquire) &&
        g_inputReady.load(std::memory_order_acquire) && hardwareWriteAllowed();
    f.uiOwned=g_msiNativeForegroundObserverActive.load(std::memory_order_acquire) && msiNativeForegroundUiEligible();f.exitFence=inputHostExitFenceRaised();
    f.borrowBusy=msiForegroundIsolationBorrowBusy() || msiNativeForegroundVirtualWorkHasPriority();
    f.power=f.currentPower=g_powerGeneration.load(std::memory_order_acquire);
    f.edge=f.currentEdge=g_msiNativeForegroundEdge.load(std::memory_order_acquire);
    return f;
}

// Lifecycle-owner, MSI private native-off/managed foreground ONLY. A separate XUSB devnode can expose XInput
// even when its HID game collection is hidden. Do not change the public HID
// inventory/targets, use interface-class proof and actual PnP parents instead.
static MsiNativeForegroundXusbInventory msiNativeForegroundXusbInventory(
        const HidHidePhysicalIdentity& source) {
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.xusbInventory)return g_msiNativeForegroundTestIo.xusbInventory(source);
    return {false,"test-xusb-inventory-not-injected",{}}; // NEVER real I/O in regression
#endif
    MsiNativeForegroundXusbInventory result;
    if(!nativeForegroundRootCandidate(source.baseContainerDeviceInstanceId)){
        result.reason="xusb-source-root-unverified";return result;
    }
    static const GUID xusbGuid={0xEC87F1E3,0xC13B,0x4100,{0xB5,0xF7,0x8B,0x84,0xD5,0x42,0x60,0xCB}};
    HDEVINFO set=SetupDiGetClassDevsW(&xusbGuid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE){result.reason="xusb-class-enumeration-failed";return result;}
    for(DWORD index=0;index<128;++index){
        SP_DEVICE_INTERFACE_DATA iface{};iface.cbSize=sizeof(iface);
        if(!SetupDiEnumDeviceInterfaces(set,nullptr,&xusbGuid,index,&iface)){
            result.complete=GetLastError()==ERROR_NO_MORE_ITEMS;
            result.reason=result.complete?"complete-active-XUSB-inventory":"xusb-interface-enumeration-incomplete";break;
        }
        if(!(iface.Flags&SPINT_ACTIVE))continue;
        DWORD bytes=0;SetupDiGetDeviceInterfaceDetailW(set,&iface,nullptr,0,&bytes,nullptr);
        if(bytes<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || bytes>65536){result.reason="xusb-detail-size-invalid";break;}
        std::vector<BYTE> buffer(bytes);
        auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());detail->cbSize=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA info{};info.cbSize=sizeof(info);
        if(!SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,bytes,nullptr,&info)){result.reason="xusb-detail-unverified";break;}
        const auto instance=hidHideDeviceInstanceId(set,info);
        if(instance.empty()){result.reason="xusb-instance-unverified";break;}
        if(hidHideIsVirtualIdentity(instance,W2U(detail->DevicePath)))continue;
        ymcc::msi::NativeForegroundXusbInterface record;
        record.activeInterface=true;record.instanceId=instance;
        DEVINST node=info.DevInst;bool ancestryReadable=true;
        for(unsigned depth=0;depth<16;++depth){
            WCHAR id[MAX_DEVICE_ID_LEN]{};
            if(CM_Get_Device_IDW(node,id,MAX_DEVICE_ID_LEN,0)!=CR_SUCCESS){ancestryReadable=false;break;}
            const auto ancestor=W2U(id);
            if(hidHideIsVirtualIdentity(ancestor,{})){record.virtualDevice=true;break;}
            if(ascii_lower(ancestor)==ascii_lower(source.baseContainerDeviceInstanceId)){
                record.ancestorMatched=true;record.ancestorRoot=ancestor;break;
            }
            DEVINST parent=0;
            const auto rc=CM_Get_Parent(&parent,node,0);
            if(rc==CR_NO_SUCH_DEVNODE)break; // reached top; this endpoint belongs elsewhere
            if(rc!=CR_SUCCESS || parent==node){ancestryReadable=false;break;}
            node=parent;
            if(depth==15)ancestryReadable=false;
        }
        if(!ancestryReadable){result.reason="xusb-parent-chain-unverified";break;}
        if(record.virtualDevice || !record.ancestorMatched)continue; // never hide another port/bus
        if(!nativeForegroundCurrentXusbTargetAdmitted(source.baseContainerDeviceInstanceId,record)){
            result.reason="xusb-same-root-instance-not-admitted";break;
        }
        const bool duplicate=std::any_of(result.interfaces.begin(),result.interfaces.end(),[&](const auto& previous){
            return ascii_lower(previous.instanceId)==ascii_lower(record.instanceId);
        });
        if(!duplicate)result.interfaces.push_back(std::move(record));
        if(result.interfaces.size()>16){result.reason="xusb-source-target-limit";break;}
    }
    SetupDiDestroyDeviceInfoList(set);
    if(result.reason.empty())result.reason="xusb-enumeration-budget-exhausted";
    return result;
}

static bool nativeForegroundVirtualTargetsAllowed() {
    const auto profile=nativeForegroundProfile();
    return (profile==ymcc::nativeForeground::Profile::Rog || profile==ymcc::nativeForeground::Profile::Gpd) &&
        g_realStickTest.enabled && g_realStickTest.virtualTargetEnabled && g_inputReady &&
        g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready && !inputHostExitFenceRaised();
}
static void nativeForegroundAppendVirtualTargets(const HidHidePhysicalIdentity& physical,
        const HidHideVisibilitySnapshot& before,size_t sourceCount,const char* site,std::vector<std::string>& targets) {
    const auto profile=nativeForegroundProfile();
    if(!nativeForegroundVirtualTargetsAllowed())return;
    auto receipt=[&](const char* reason,bool complete){
        appendNativeLifecycleLog("hidhide.family-game-visibility-plan",{{"family",ymcc::nativeForeground::name(profile)},
            {"reason",reason},{"site",site?site:"unknown"},{"sourceRoot",physical.baseContainerDeviceInstanceId},
            {"physicalVisibilityTargets",targets},{"complete",complete},{"driverWrite",false},
            {"externalInputBlocked","unverified"},{"owner","existing-virtual-visibility-journal"}});
    };
    if(sourceCount!=1 || !before.valid || hidHideInverseApplicationListState(before.inverseRaw)!="off" ||
        !physical.resolved || !nativeForegroundGameInputAdmitted(true,
            hidHideIsVirtualIdentity(physical.deviceInstanceId,physical.baseContainerDeviceInstanceId),
            physical.vendorId,physical.productId,physical.usagePage,physical.usage) || !nativeForegroundRootCandidate(physical.baseContainerDeviceInstanceId)){
        receipt("source-or-visibility-unverified",false);return;
    }
    const auto locations=msiForegroundIsolationLocations(physical.baseContainerDeviceInstanceId);
    if(locations.empty()){receipt("physical-location-unverified",false);return;}
    bool complete=false;std::vector<HidHideInterfaceRecord> inventory;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.inventory){
        inventory=g_msiNativeForegroundTestIo.inventory(physical);
        complete=!g_msiNativeForegroundTestIo.inventoryComplete || g_msiNativeForegroundTestIo.inventoryComplete();
    }else
#endif
    inventory=hidHideBuildSourceInterfaceInventory(physical.baseContainerDeviceInstanceId,physical.deviceInstanceId,&complete);
    bool sourcePresent=false;auto proposed=targets;
    for(const auto& item:inventory)if(nativeForegroundGameInputAdmitted(ascii_lower(item.baseContainerDeviceInstanceId)==ascii_lower(physical.baseContainerDeviceInstanceId),
        hidHideIsVirtualIdentity(item.instanceId,item.baseContainerDeviceInstanceId),item.vendorId,item.productId,item.usagePage,item.usage)){
        sourcePresent=sourcePresent || ascii_lower(item.instanceId)==ascii_lower(physical.deviceInstanceId);
        hidHideAppendTarget(proposed,item.instanceId);
    }
    if(!complete || !sourcePresent){receipt("current-game-inventory-unverified",false);return;}
    const auto xusb=msiNativeForegroundXusbInventory(physical);unsigned xusbCount=0;
    for(const auto& item:xusb.interfaces)if(nativeForegroundCurrentXusbTargetAdmitted(physical.baseContainerDeviceInstanceId,item)){
        hidHideAppendTarget(proposed,item.instanceId);++xusbCount;
    }
    if(!xusb.complete || !xusbCount || proposed.size()>16 ||
        !ymcc::msi::samePhysicalLocation(locations,msiForegroundIsolationLocations(physical.baseContainerDeviceInstanceId))){
        receipt("current-xusb-inventory-unverified",false);return;
    }
    targets=std::move(proposed);receipt("same-root-hid-and-xusb-plan",true);
}
static bool msiNativeForegroundLeaseScopeCurrent(ymcc::msi::NativeForegroundFacts facts) {
    const auto& lease=g_msiNativeForegroundLease;
    facts.edge=lease.edge;facts.power=lease.power;
    return nativeForegroundProfile()==lease.profile && ymcc::msi::nativeForegroundScopeCurrent(facts) &&
        lease.suspendSerial==g_sharedPowerSuspendSerial.load(std::memory_order_acquire);
}
static bool msiNativeForegroundLeaseCurrent() {
    const auto facts=msiNativeForegroundFacts();
    return facts.powerReady && msiNativeForegroundLeaseScopeCurrent(facts);
}
static bool msiNativeForegroundWin5SettledLeaseMayWaitForSuspendAdmission() {
    const auto& lease=g_msiNativeForegroundLease;
    if(lease.profile!=ymcc::nativeForeground::Profile::Gpd ||
        g_machineIdentity.deviceClass!="HandheldCompanion.Devices.GPDWin5" ||
        !lease.cycleAccepted || lease.restorePending || lease.configurationReturned ||
        lease.cycleForRestore || lease.refreshSeriesActive || lease.refreshFailed ||
        g_msiNativeForegroundCyclePending.load(std::memory_order_acquire))return false;
    return msiNativeForegroundLeaseScopeCurrent(msiNativeForegroundFacts()) &&
        !g_aiFanMockSession.isolated && g_inputReady.load(std::memory_order_acquire) &&
        g_hardwareWriteGate.load(std::memory_order_acquire) &&
        g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready &&
        !g_msiNativeForegroundPowerHold.load(std::memory_order_acquire);
}
// Reuse installed InputHost only as a target-FREE, parent-bound one-shot. No
// pipe/global Host tuple is touched, and timeouts retain the SAME live handle.
static void msiNativeForegroundSettleHelperDiagnosticLocked(DWORD exitCode) noexcept {
    auto& s=g_msiNativeForegroundLease;
    if(!s.helperDiagnosticCapture)return;
    auto capture=std::move(s.helperDiagnosticCapture);
    try {
        s.helperDiagnostic=capture->terminal(exitCode);
        // Emit before a following return operation can replace this helper's
        // fields. The existing native lifecycle file is part of normal export.
        if(exitCode!=0)appendNativeLifecycleLog("input-host.native-helper-diagnostic", {
            {"family",ymcc::nativeForeground::name(s.profile)},{"helperExitCode",exitCode},
            {"helperGameTarget",s.helperGameTarget},{"helperXusbTarget",s.helperXusbTarget},
            {"cycleForRestore",s.cycleForRestore},{"sourceRoot",s.root},{"edge",s.edge},{"powerGeneration",s.power},
            {"helperIdentityScope","on-disk-before-launch-not-loaded-image-proof"},
            {"helperExeSha256",s.helperExeSha256},{"helperDllSha256",s.helperDllSha256},
            {"helperDiagnostic",s.helperDiagnostic},{"deviceResultChanged",false}
        });
    }catch(...){} // Evidence failure never changes the operation's exit result.
}
static int msiNativeForegroundPollCycleLocked();
static bool msiPostHideContextCurrentLocked();
static bool msiPostHideExecutionCurrentLocked();
static bool msiColdReturnContextCurrentLocked();
static bool msiColdReturnCleanupLocked(const char* reason);
static bool msiColdReturnMaintainOwner(const char* reason,bool exiting=false);
static int msiNativeForegroundCycleLocked(bool restoring,const HidHidePhysicalIdentity* gameSource=nullptr,bool xusbSource=false) {
    auto& s=g_msiNativeForegroundLease;
    if(s.nativeRootPortCycle && gameSource){
        msiNativeForegroundReceipt("refresh-deferred","root-cycle-owner-cannot-refresh-a-hid-leaf");return 2;
    }
    // The leaf owns unresolved helper lifetime as well as its callers. A second
    // request must not replace its target, direction, hashes or evidence before
    // the existing lifecycle poll consumes this same operation's terminal state.
    if(s.cycleProcess || s.nativeRefreshWriter || s.syntheticCycle || g_msiNativeForegroundCyclePending.load(std::memory_order_acquire)) {
        g_msiNativeForegroundCyclePending.store(true,std::memory_order_release);
        return 1;
    }
    const bool ordinary=!s.coldReturnCycle && !s.postHideCycle;
    const auto boundary=ordinary?g_sleepPowerDispatch.observedSleepBoundarySequence():0;
    const auto powerCurrent=[&] {
        return !ordinary || (hardwareWriteAllowed() && s.power==currentPowerGeneration() &&
            g_inputReady.load(std::memory_order_acquire) && !g_msiNativeForegroundPowerHold.load(std::memory_order_acquire) &&
            g_sleepPowerDispatch.observedSleepBoundarySequence()==boundary &&
            g_sharedPowerSuspendSerial.load(std::memory_order_acquire)==s.suspendSerial);
    };
    if(!powerCurrent())return MsiNativeForegroundLease::kPowerDeferred;
    std::string gameTarget=gameSource?gameSource->deviceInstanceId:std::string();
    bool gameTargetXusb=xusbSource;
    std::string exePath,exeHash,dllPath,dllHash;
    if(gameSource && !ymcc::nativeForeground::gameNodeRefreshMutationAllowed(s.profile,s.root,gameSource->deviceInstanceId)){
        s.helperFailure="rog-hid-game-node-refresh-unvalidated";
        msiNativeForegroundReceipt("refresh-deferred",s.helperFailure.c_str());return 2;
    }
    if(!gameSource && s.profile!=ymcc::nativeForeground::Profile::Msi &&
        !(s.nativeRootPortCycle && ymcc::nativeForeground::nativeRootPortCycleAllowed(s.profile,s.root))){
        s.helperFailure="root-port-cycle-not-admitted";return 2;
    }
    if(s.locations.empty()){s.helperFailure="helper-location-missing";return 2;}
    if(s.nativeRootPortCycle && !restoring && !msiNativeForegroundLeaseCurrent()){
        s.helperFailure="native-root-cycle-context-ended";return 2;
    }
    const auto markIssued=[&] {
        s.retiredNativeWriterAttempt.clear();
        s.helperDiagnostic=nullptr;
        s.helperExitCode=STILL_ACTIVE;s.helperError=0;s.helperFailure.clear();
        s.helperExitQueryAttempted=false;s.helperExitQuerySucceeded=false;s.helperExitQueryError=0;
        s.helperGameTarget=std::move(gameTarget);s.helperXusbTarget=gameTargetXusb;
        s.helperExePath=std::move(exePath);s.helperExeSha256=std::move(exeHash);
        s.helperDllPath=std::move(dllPath);s.helperDllSha256=std::move(dllHash);
        s.cycleForRestore=restoring;s.cycleIssued=true;s.cycleAccepted=false;
        s.refreshAttemptEdge=g_msiNativeForegroundEdge.load(std::memory_order_acquire);
    };
    if(!ordinary){
        s.cycleForRestore=restoring;s.cycleIssued=true;s.cycleAccepted=false;
        s.refreshAttemptEdge=g_msiNativeForegroundEdge.load(std::memory_order_acquire);
    }
    if(s.coldReturnCycle && !msiColdReturnContextCurrentLocked()){s.helperFailure="cold-return-context-ended-before-helper";return 2;}
    if(s.postHideCycle && !msiPostHideExecutionCurrentLocked()){s.helperFailure="post-hide-context-ended-before-helper";return 2;}
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(ordinary && g_msiNativeForegroundTestIo.beforeCycleDispatch)g_msiNativeForegroundTestIo.beforeCycleDispatch();
    if(!powerCurrent())return MsiNativeForegroundLease::kPowerDeferred;
    if(ordinary && !restoring && !msiNativeForegroundLeaseCurrent())return 2;
    if(gameSource && xusbSource && g_msiNativeForegroundTestIo.xusbRefresh){
        markIssued();
        const int result=g_msiNativeForegroundTestIo.xusbRefresh(*gameSource,s.locations.front());
        s.syntheticCycle=result==1;s.cycleAccepted=result==0;
        s.helperExitCode=result==1?STILL_ACTIVE:(result==0?0:6);
        if(result==2)s.helperFailure="test-terminal-xusb-refresh-failure";
        g_msiNativeForegroundCyclePending=result==1;return result;
    }
    if(gameSource && g_msiNativeForegroundTestIo.gameRefresh){
        markIssued();
        const int result=g_msiNativeForegroundTestIo.gameRefresh(*gameSource,s.locations.front());
        s.syntheticCycle=result==1;s.cycleAccepted=result==0;
        s.helperExitCode=result==1?STILL_ACTIVE:(result==0?0:6);
        if(result==2)s.helperFailure="test-terminal-game-refresh-failure";
        g_msiNativeForegroundCyclePending=result==1;return result;
    }
    if(g_msiNativeForegroundTestIo.cycle){
        markIssued();
        const int result=g_msiNativeForegroundTestIo.cycle(s.root,s.locations.front(),restoring);
        s.syntheticCycle=result==1;s.cycleAccepted=result==0;
        s.helperExitCode=result==1?STILL_ACTIVE:(result==0?0:6);
        if(result==2)s.helperFailure="test-terminal-helper-failure";
        g_msiNativeForegroundCyclePending=result==1;return result;
    }
#endif
    const auto exe=inputHostExecutablePath();
    if(GetFileAttributesW(exe.c_str())==INVALID_FILE_ATTRIBUTES){s.helperError=GetLastError();s.helperFailure="helper-executable-missing";return 2;}
    // Hash only when a real one-shot is launched, never on receipt/poll/report.
    // The apphost EXE alone is not the C# business identity. Record both, while
    // keeping missing/unreadable identity diagnostic-only, not a new authority.
    exePath=W2U(exe);exeHash=sha256File(exe);
    const auto dll=fspath::path(exe).replace_extension(L".dll").wstring();
    dllPath=W2U(dll);dllHash=sha256File(dll);
    if(s.nativeRootPortCycle && !restoring && !msiNativeForegroundLeaseCurrent()){
        s.helperFailure="native-root-cycle-context-ended-after-identity-capture";return 2;
    }
    const std::wstring root=U2W(s.root),location=s.locations.front();
    if(root.find(L'"')!=std::wstring::npos || location.find(L'"')!=std::wstring::npos){s.helperFailure="invalid-cycle-arguments";return 2;}
    auto command=L"\""+exe+L"\" --msi-native-cycle \""+root+L"\" --location \""+location+
        L"\" --parent-pid "+std::to_wstring(GetCurrentProcessId())+L" --once";
    if(s.nativeRootPortCycle){
        if(s.xusbTargets.empty()){s.helperFailure="root-cycle-current-xusb-target-missing";return 2;}
        const auto target=U2W(s.xusbTargets.front());
        if(target.find(L'"')!=std::wstring::npos){s.helperFailure="invalid-root-cycle-target";return 2;}
        gameTarget=s.xusbTargets.front();gameTargetXusb=true;
        command+=L" --rog-native-xusb-root-cycle \""+target+L"\"";
    }else if(gameSource){
        const auto target=U2W(gameSource->deviceInstanceId);
        if(target.find(L'"')!=std::wstring::npos){s.helperFailure="invalid-game-refresh-arguments";return 2;}
        command+=((s.postHideCycle || s.coldReturnCycle)?L" --game-xusb-cycle \"":xusbSource?L" --game-xusb \"":L" --game-hid \"")+target+L"\"";
    }
    if(s.postHideCycle || s.coldReturnCycle){
        const auto parent=ymcc::targetReturn::process::capture(GetCurrentProcess());
        if(!parent || parent->pid!=GetCurrentProcessId() || !(s.coldReturnCycle?s.coldReturnDeadline:s.postHideDeadline)){s.helperFailure="post-hide-parent-binding-unavailable";return 2;}
        SECURITY_ATTRIBUTES inheritedEvent{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
        s.postHideCancelEvent=CreateEventW(&inheritedEvent,TRUE,FALSE,nullptr);
        if(!s.postHideCancelEvent){s.helperError=GetLastError();s.helperFailure="post-hide-cancel-event-unavailable";return 2;}
        command+=L" --parent-birth "+U2W(parent->creationTime)+L" --deadline-tick "+std::to_wstring(s.coldReturnCycle?s.coldReturnDeadline:s.postHideDeadline)+
            L" --cancel-handle "+std::to_wstring(reinterpret_cast<uintptr_t>(s.postHideCancelEvent));
    }
    std::vector<wchar_t> buffer(command.begin(),command.end());buffer.push_back(0);
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi{};
    const auto dir=fspath::path(exe).parent_path().wstring();
    // Optional evidence; an unavailable sink preserves the original launch.
    std::unique_ptr<ymcc::input::OneShotHelperDiagnostic> capture;
    try {if(gameSource || s.nativeRootPortCycle){capture=std::make_unique<ymcc::input::OneShotHelperDiagnostic>();if(!capture->prepare())capture.reset();}}catch(...){capture.reset();}
    if(s.profile==ymcc::nativeForeground::Profile::Gpd){
        // A GPD refresh may still be returning a disabled node after native
        // death. No kill-on-close, no post-create association, no bare fallback.
        try {
            std::vector<std::string> positions;for(const auto& position:s.locations)positions.push_back(W2U(position));
            if(s.postHideCycle && !g_postHideSharedWriterSession){s.helperFailure="post-hide-start-fence-missing";return 2;}
            auto scope=s.postHideCycle?
                std::make_unique<ymcc::nativeRefreshWriter::LaunchScope>(g_postHideSharedWriterSession,s.root,gameTarget,positions):
                s.nativeVisibilitySession?
                std::make_unique<ymcc::nativeRefreshWriter::LaunchScope>(s.nativeVisibilitySession,s.root,gameTarget,positions):
                std::make_unique<ymcc::nativeRefreshWriter::LaunchScope>(nativeRefreshWriterRecordPath(),
                    inputHostTargetReturnMachineBinding(),s.root,gameTarget,positions);
            if(!scope->ready()){
                s.helperError=scope->error();s.helperFailure=scope->reason();
                s.nativeRefreshWriterDiagnostic=nativeRefreshWriterEvidence(*scope,"prepare-deferred");return 2;
            }
            std::vector<HANDLE> inheritedHandles{scope->inheritedLease()};
            if(s.postHideCycle || s.coldReturnCycle)inheritedHandles.push_back(s.postHideCancelEvent);
            const bool guarded=capture?capture->prepareTrackedLaunch(scope->job(),scope->jobName(),inheritedHandles):
                scope->job().prepareTracked(si,scope->jobName(),inheritedHandles);
            if(!guarded){s.helperError=scope->job().error();s.helperFailure="native-writer-job-prepare-failed";return 2;}
            if(!scope->reserve()){
                s.helperError=scope->error();s.helperFailure=scope->reason();
                s.nativeRefreshWriterDiagnostic=nativeRefreshWriterEvidence(*scope,"reserve-deferred");return 2;
            }
            s.nativeRefreshWriterDiagnostic=nativeRefreshWriterEvidence(*scope,"reserved-before-create");
            s.nativeRefreshWriter=std::move(scope); // own the record before CreateProcess, including failures
        }catch(...){s.helperFailure="native-writer-prepare-exception";return 2;}
        auto& scope=*s.nativeRefreshWriter;
        if((s.postHideCycle && !msiPostHideExecutionCurrentLocked()) ||
            (s.coldReturnCycle && !msiColdReturnContextCurrentLocked())){
            s.helperFailure="bounded-cycle-context-ended-before-create";
            (void)scope.creationReturned(nullptr);g_msiNativeForegroundCyclePending=true;
            const int terminal=msiNativeForegroundPollCycleLocked();return terminal<0?1:terminal;
        }
        const bool cancelled=ordinary && !restoring && !msiNativeForegroundLeaseCurrent();
        if(cancelled || !powerCurrent()){
            s.unissuedWriterRetirement=true;
            (void)scope.creationReturned(nullptr);g_msiNativeForegroundCyclePending=true;
            (void)msiNativeForegroundPollCycleLocked();
            return cancelled?2:MsiNativeForegroundLease::kPowerDeferred;
        }
        markIssued();
        if(!ymcc::processEfficiency::createProcess(exe.c_str(),buffer.data(),nullptr,nullptr,TRUE,
            CREATE_NO_WINDOW|scope.job().flags(),nullptr,dir.c_str(),scope.job().startup(),&pi)){
            s.helperError=GetLastError();s.helperFailure="helper-launch-failed";
            (void)scope.creationReturned(nullptr);g_msiNativeForegroundCyclePending=true;
            // A failed durable settlement remains pending under this owner.
            const int terminal=msiNativeForegroundPollCycleLocked();return terminal<0?1:terminal;
        }
        if(capture)capture->started();s.helperDiagnosticCapture=std::move(capture);
        CloseHandle(pi.hThread);s.cycleProcess=pi.hProcess;g_msiNativeForegroundCyclePending=true;
        // Loss of this post-create PID receipt cannot release the inherited
        // lease or replace the same process. Its terminal path still owns it.
        (void)scope.creationReturned(pi.hProcess);
        s.nativeRefreshWriterDiagnostic=nativeRefreshWriterEvidence(scope,"created-in-exact-job");
        const int terminal=msiNativeForegroundPollCycleLocked();return terminal<0?1:terminal;
    }
    const DWORD launchFlags=CREATE_NO_WINDOW|(capture?EXTENDED_STARTUPINFO_PRESENT:0);
    if(ordinary && !restoring && !msiNativeForegroundLeaseCurrent())return 2;
    if(!powerCurrent())return MsiNativeForegroundLease::kPowerDeferred;
    markIssued();
    if(!ymcc::processEfficiency::createProcess(exe.c_str(),buffer.data(),nullptr,nullptr,capture?TRUE:FALSE,launchFlags,nullptr,dir.c_str(),capture?capture->startup():&si,&pi)){s.helperError=GetLastError();s.helperFailure="helper-launch-failed";return 2;}
    if(capture)capture->started();
    s.helperDiagnosticCapture=std::move(capture);
    CloseHandle(pi.hThread);
    const auto wait=WaitForSingleObject(pi.hProcess,0);
    if(wait!=WAIT_OBJECT_0){
        s.cycleProcess=pi.hProcess;g_msiNativeForegroundCyclePending=true;return 1;
    }
    DWORD exitCode=1;
    s.helperExitQueryAttempted=true;
    s.helperExitQuerySucceeded=GetExitCodeProcess(pi.hProcess,&exitCode)!=FALSE;
    s.helperExitQueryError=s.helperExitQuerySucceeded?ERROR_SUCCESS:GetLastError();
    const bool ok=s.helperExitQuerySucceeded && exitCode==0;
    msiNativeForegroundSettleHelperDiagnosticLocked(exitCode);
    CloseHandle(pi.hProcess);s.cycleAccepted=ok;s.helperExitCode=exitCode;
    if(!ok)s.helperFailure="helper-terminal-nonzero-exit";return ok?0:2;
}
static int msiNativeForegroundPollCycleLocked() {
    auto& s=g_msiNativeForegroundLease;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(s.syntheticCycle && g_msiNativeForegroundTestIo.poll){
        const int result=g_msiNativeForegroundTestIo.poll();if(result<0)return -1;
        s.syntheticCycle=false;s.cycleAccepted=result==0;s.helperExitCode=result==0?0:6;
        if(result!=0)s.helperFailure="test-terminal-helper-failure";g_msiNativeForegroundCyclePending=false;return result;
    }
#endif
    if(s.nativeRefreshWriter){
        if(s.cycleProcess && WaitForSingleObject(s.cycleProcess,0)!=WAIT_OBJECT_0)return -1;
        if(!s.nativeRefreshWriter->retire(s.cycleProcess)){
            const auto& reason=s.nativeRefreshWriter->reason();
            s.nativeRefreshWriterDiagnostic=nativeRefreshWriterEvidence(*s.nativeRefreshWriter,"retirement-deferred");
            if(s.helperFailure!=reason){s.helperFailure=reason;msiNativeForegroundReceipt("refresh-retirement-deferred",reason.c_str());}
            return -1;
        }
        s.nativeRefreshWriterDiagnostic=nativeRefreshWriterEvidence(*s.nativeRefreshWriter,"durably-settled");
        if(s.unissuedWriterRetirement){
            s.unissuedWriterRetirement=false;s.nativeRefreshWriter.reset();g_msiNativeForegroundCyclePending=false;
            return MsiNativeForegroundLease::kPowerDeferred;
        }
        if(const auto* retired=s.nativeRefreshWriter->attempt())s.retiredNativeWriterAttempt=retired->attemptId;
        if(!s.cycleProcess){
            s.nativeRefreshWriter.reset();s.cycleAccepted=false;s.helperExitCode=1;
            g_msiNativeForegroundCyclePending=false;
            msiNativeForegroundReceipt("refresh-failed","same-helper-create-failure-durably-settled");return 2;
        }
        s.nativeRefreshWriter.reset(); // root HANDLE remains owned below, already terminal
        s.helperFailure.clear();
    }
    if(!s.cycleProcess)return -1;
    if(WaitForSingleObject(s.cycleProcess,0)!=WAIT_OBJECT_0)return -1;
    DWORD code=1;
    s.helperExitQueryAttempted=true;
    s.helperExitQuerySucceeded=GetExitCodeProcess(s.cycleProcess,&code)!=FALSE;
    s.helperExitQueryError=s.helperExitQuerySucceeded?ERROR_SUCCESS:GetLastError();
    const bool ok=s.helperExitQuerySucceeded && code==0;
    msiNativeForegroundSettleHelperDiagnosticLocked(code);
    if(!CloseHandle(s.cycleProcess)){
        g_msiNativeForegroundCyclePending=true;
        return -1;
    }
    s.cycleProcess=nullptr;s.cycleAccepted=ok;s.helperExitCode=code;
    if(!ok)s.helperFailure="helper-terminal-nonzero-exit";
    g_msiNativeForegroundCyclePending=false;
    msiNativeForegroundReceipt(ok?"refresh-completed":"refresh-failed","same-helper-terminal-receipt");
    return ok?0:2;
}
static int nativeForegroundRefreshLocked(bool restoring) {
    auto& lease=g_msiNativeForegroundLease;
    if(lease.nativeRootPortCycle){
        lease.refreshPlanAttemptEdge=g_msiNativeForegroundEdge.load(std::memory_order_acquire);
        lease.refreshPlanForRestore=restoring;
        if(!ymcc::nativeForeground::nativeRootPortCycleAllowed(lease.profile,lease.root))return 2;
        return msiNativeForegroundCycleLocked(restoring);
    }
    if(lease.profile==ymcc::nativeForeground::Profile::Msi)return msiNativeForegroundCycleLocked(restoring);
    if(g_msiNativeForegroundCyclePending)return 1;
    lease.refreshPlanAttemptEdge=g_msiNativeForegroundEdge.load(std::memory_order_acquire);
    lease.refreshPlanForRestore=restoring;
    // A pending leaf already advanced nextRefreshTarget at launch. After its
    // terminal poll, an exhausted list is not a successful series unless that
    // same leaf was accepted. Preserve failed/unknown return evidence across
    // a new focus edge; changing direction still runs the existing restore.
    // GPD only: preserve the currently accepted ROG/MSI execution policy.
    if(lease.profile==ymcc::nativeForeground::Profile::Gpd && lease.refreshSeriesActive && lease.cycleForRestore==restoring &&
        lease.nextRefreshTarget>0 && !lease.cycleAccepted){
        lease.refreshSeriesActive=false;return 2;
    }
    if(!lease.refreshSeriesActive || lease.cycleForRestore!=restoring){
        lease.refreshTargets.clear();lease.nextRefreshTarget=0;lease.refreshSeriesActive=false;
        bool complete=false;std::vector<HidHideInterfaceRecord> inventory;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
        if(g_msiNativeForegroundTestIo.inventory){
            HidHidePhysicalIdentity anchor;anchor.resolved=true;anchor.baseContainerDeviceInstanceId=lease.root;
            inventory=g_msiNativeForegroundTestIo.inventory(anchor);
            complete=!g_msiNativeForegroundTestIo.inventoryComplete || g_msiNativeForegroundTestIo.inventoryComplete();
        }else
#endif
        inventory=hidHideBuildSourceInterfaceInventory(lease.root,{},&complete);
        if(!complete)return 2;
        HidHidePhysicalIdentity anchor;
        for(const auto& item:inventory)if(nativeForegroundGameInputAdmitted(ascii_lower(item.baseContainerDeviceInstanceId)==ascii_lower(lease.root),
            hidHideIsVirtualIdentity(item.instanceId,item.baseContainerDeviceInstanceId),item.vendorId,item.productId,item.usagePage,item.usage)){
            HidHidePhysicalIdentity source;source.resolved=true;source.baseContainerDeviceInstanceId=lease.root;
            source.deviceInstanceId=item.instanceId;source.vendorId=item.vendorId;source.productId=item.productId;source.usagePage=item.usagePage;source.usage=item.usage;
            if(anchor.deviceInstanceId.empty())anchor=source;
            lease.refreshTargets.push_back({source,false});
        }
        if(anchor.deviceInstanceId.empty())return 2;
        const auto xusb=msiNativeForegroundXusbInventory(anchor);
        if(!xusb.complete)return 2;
        unsigned xusbCount=0;
        for(const auto& item:xusb.interfaces)if(nativeForegroundCurrentXusbTargetAdmitted(lease.root,item)){
            auto source=anchor;source.deviceInstanceId=item.instanceId;
            bool duplicate=false;
            for(auto& target:lease.refreshTargets)if(ascii_lower(target.source.deviceInstanceId)==ascii_lower(source.deviceInstanceId)){
                if(!target.xusb){target.xusb=true;++xusbCount;}
                duplicate=true;
            }
            if(!duplicate){lease.refreshTargets.push_back({source,true});++xusbCount;}
        }
        if(!xusbCount || lease.refreshTargets.size()>16)return 2;
        lease.refreshSeriesActive=true;lease.cycleForRestore=restoring;
    }
    while(lease.nextRefreshTarget<lease.refreshTargets.size()){
        if(restoring && (!hardwareWriteAllowed() || !g_inputReady || g_msiNativeForegroundPowerHold))
            return MsiNativeForegroundLease::kPowerDeferred;
        if((!restoring && !msiNativeForegroundLeaseCurrent()) || nativeForegroundProfile()!=lease.profile ||
            !ymcc::msi::samePhysicalLocation(lease.locations,msiForegroundIsolationLocations(lease.root))){lease.refreshSeriesActive=false;return 2;}
        const auto target=lease.refreshTargets[lease.nextRefreshTarget];
        const int result=msiNativeForegroundCycleLocked(restoring,&target.source,target.xusb);
        if(result==MsiNativeForegroundLease::kPowerDeferred)return result;
        ++lease.nextRefreshTarget;
        if(result!=0){if(result==2)lease.refreshSeriesActive=false;return result;}
    }
    lease.refreshSeriesActive=false;lease.cycleAccepted=true;return 0;
}
// Keep unresolved refresh evidence after exact configuration return. A failed
// or timed-out refresh is NEVER quietly converted into physical restoration.
static void msiNativeForegroundClearLeaseLocked() {
    g_msiNativeForegroundWatchGameSources=false;
    auto& s=g_msiNativeForegroundLease;
    // Every profile retains its exact root and pending writer responsibility
    // until the existing lifecycle poll has actually returned those resources.
    if(s.nativeRefreshWriter || s.cycleProcess ||
        g_msiNativeForegroundCyclePending.load(std::memory_order_acquire)){
        try{msiNativeForegroundReceipt("clear-deferred","native-refresh-writer-not-retired");}catch(...){}
        return;
    }
    if(s.nativeVisibilitySession && s.nativeVisibilitySession->record()->nativeVisibility){
        auto& session=*s.nativeVisibilitySession;
        if(!session.completeVisibility(session.record()->revision,s.nativeVisibilityTransaction)){
            try{msiNativeForegroundReceipt("clear-deferred","native-visibility-responsibility-not-settled");}catch(...){}
            return;
        }
    }
    if(s.postHideCancelEvent){
        if(!CloseHandle(s.postHideCancelEvent)){
            try{msiNativeForegroundReceipt("clear-deferred","cancel-event-close-failed");}catch(...){}
            return;
        }
        s.postHideCancelEvent=nullptr;
    }
    g_postHideCycleOwned.store(false,std::memory_order_release);
    g_coldReturnCycleOwned.store(false,std::memory_order_release);
    g_msiNativeForegroundWorkPowerDeferred.store(false,std::memory_order_release);
    const auto spent=s.attemptedEdge;
    auto coldReceipt=std::move(s.coldReturnReceipt);
    auto deferred=std::move(s.deferredVirtualWork);
    s=MsiNativeForegroundLease{};s.attemptedEdge=spent;s.coldReturnReceipt=std::move(coldReceipt);
    for(const auto& reason:deferred){
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
        if(g_msiNativeForegroundTestIo.virtualQueue){g_msiNativeForegroundTestIo.virtualQueue(reason);continue;}
#endif
        (void)inputHostQueueLifecycleWork(reason.c_str());
    }
}
// R3 current-intent facts do not carry the old native donor's ticket. A
// configuration already read back returned is not a donor transfer. This
// predicate neither creates a request nor renews its original 30s budget.
static bool msiNativeForegroundGpdVirtualIntentCurrentLocked() {
    const auto& s=g_msiNativeForegroundLease;
    if(s.profile!=ymcc::nativeForeground::Profile::Gpd || nativeForegroundProfile()!=s.profile ||
        !s.active || s.power!=currentPowerGeneration() ||
        s.suspendSerial!=g_sharedPowerSuspendSerial.load(std::memory_order_acquire) ||
        g_msiNativeForegroundPowerHold.load(std::memory_order_acquire) ||
        !hardwareWriteAllowed() || !g_inputReady ||
        inputHostExitFenceRaised() || g_exitRequested.load(std::memory_order_acquire) ||
        !g_realStickTest.enabled || !g_realStickTest.virtualTargetEnabled || inputHostConfiguredPersona()=="disabled")return false;
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    return view.requestId && view.locked && view.remainingMs &&
        (view.phase=="pending" || view.phase=="verifying") && view.persona==inputHostConfiguredPersona();
}
// Preserving/transferring still-owned hiding remains bound to its ORIGINAL
// request. A newer ON request must never inherit another period's donor.
static bool msiNativeForegroundGpdVirtualRequestCurrentLocked() {
    if(!msiNativeForegroundGpdVirtualIntentCurrentLocked())return false;
    const auto& s=g_msiNativeForegroundLease;
    const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
    return !s.virtualHandoffHeld || s.virtualHandoffRequest==view.requestId;
}
// A terminal helper is not an enable acknowledgement. Only an acknowledged
// complete refresh, or explicit failure BEFORE any acknowledged Disable, can
// take this shortcut. Missing/unknown facts and failed Enable stay on return.
// No claim of external visibility or Steam consumption is made here.
static bool msiPostHideRootStartedFor(const std::string& sourceRoot,const std::vector<std::wstring>& positions);
static bool msiNativeForegroundGpdTerminalGameWriterReturnedLocked() {
    const auto& s=g_msiNativeForegroundLease;
    if(s.nativeRefreshWriter || s.cycleProcess || g_msiNativeForegroundCyclePending.load(std::memory_order_acquire) ||
        s.helperGameTarget.empty())return false;
    if(s.cycleAccepted && s.helperExitCode==0)return true;
    try {
        const auto& report=s.helperDiagnostic;
        if(s.helperExitCode!=6 || !report.is_object() || report.value("state",std::string{})!="record-accepted" ||
            !report.contains("reportedFacts") || !report["reportedFacts"].is_object())return false;
        const auto& facts=report["reportedFacts"];
        // This exact native GPD root route never calls CM Disable/Enable.
        // A failed port command is still a failure. Only current started/root
        // readback may retire its no-disable duty; normal Start must freshly
        // re-admit and hide the source before any virtual output is accepted.
        if(facts.contains("RootPortCycle") && facts["RootPortCycle"]==true){
            if(s.profile!=ymcc::nativeForeground::Profile::Gpd || !s.helperXusbTarget ||
                !nativeForegroundRootCandidate(s.root) || ascii_lower(s.helperGameTarget)!=ascii_lower(s.root) ||
                facts.value("Kind",std::string{})!="ymcc.gameRefresh.failure" ||
                !facts.contains("Xusb") || facts["Xusb"]!=true ||
                !facts.contains("DisableCallEntered") || facts["DisableCallEntered"]!=false ||
                !facts.contains("DisableResult") || !facts["DisableResult"].is_null() ||
                !facts.contains("EnableCallEntered") || facts["EnableCallEntered"]!=false ||
                !facts.contains("EnableResult") || !facts["EnableResult"].is_null() ||
                !facts.contains("CompensationAttempted") || facts["CompensationAttempted"]!=false)return false;
            const auto stage=facts.value("Stage",std::string{});
            if(stage!="prepare" && stage!="revalidate" && stage!="root-cycle")return false;
            return msiPostHideRootStartedFor(s.root,s.locations);
        }
        if(facts.value("Kind",std::string{})!="ymcc.gameRefresh.failure" || facts.value("Stage",std::string{})!="disable" ||
            !facts.contains("DisableCallEntered") || facts["DisableCallEntered"]!=true ||
            !facts.contains("DisableResult") || !facts["DisableResult"].is_number_integer() ||
            !facts.contains("EnableCallEntered") || facts["EnableCallEntered"]!=false ||
            !facts.contains("EnableResult") || !facts["EnableResult"].is_null() ||
            (facts.contains("CompensationAttempted") && facts["CompensationAttempted"]!=false))return false;
        const auto& result=facts["DisableResult"];
        return result.is_number_unsigned()?result.get<unsigned long long>()>0 && result.get<unsigned long long>()<=0xffffffffULL:
            result.get<long long>()>0 && result.get<long long>()<=0xffffffffLL;
    }catch(...){return false;}
}
// Experimental HC selected-XUSB cycle. This reuses the existing sole helper
// owner/fence; it is not a parallel switch, CM-disable policy, or hardware PASS.
static ymcc::input::recoveryHealth::Source msiPostHideLatestSourceLocked() {
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.postHideSource)return g_msiNativeForegroundTestIo.postHideSource();
#endif
    return inputHostRecoveryHealthLatestSourceLocked();
}
static bool msiPostHideContextCurrentLocked() {
    const auto& s=g_msiNativeForegroundLease;
    const auto now=inputHostStartProtectionNow();
    const auto view=g_virtualStartProtection.view(now);
    const auto remaining=s.postHideWakeSerial?
        inputHostGpdPostHideWakeRemaining(s.postHideTicket,s.postHidePersona,now,s.postHideWakeSerial):
        (view.locked?view.remainingMs:0);
    return s.postHideCycle && !s.postHideAborted && s.profile==ymcc::nativeForeground::Profile::Gpd &&
        nativeForegroundProfile()==s.profile && s.power==currentPowerGeneration() &&
        !g_msiNativeForegroundPowerHold && g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready &&
        g_inputReady && hardwareWriteAllowed() && !g_inputHostPowerAdmissionRevoked && !g_inputCaptureStop &&
        !g_inputHostTransportFault && !g_inputHostPipeRetired &&
        !inputHostExitFenceRaised() && !g_exitRequested &&
        g_realStickTest.enabled && g_realStickTest.virtualTargetEnabled &&
        view.requestId==s.postHideTicket && remaining && inputHostStartProtectionNow()<s.postHideDeadline &&
        view.persona==s.postHidePersona && inputHostConfiguredPersona()==s.postHidePersona &&
        g_inputHostEpoch==s.postHideEpoch && g_inputHostRunId==s.postHideRunId && g_inputHostProcess==s.postHideHostProcess &&
        !g_inputHostPrepared && !g_inputHostPrepareMayHaveReachedHost && !g_inputHostActive;
}
static bool msiPostHideExecutionCurrentLocked() {
    if(!msiPostHideContextCurrentLocked())return false;
    const auto& s=g_msiNativeForegroundLease;
    if(!s.postHideWakeSerial)return true; // unchanged original player execution
    std::uint64_t serial=0;
    return inputHostGpdStartRemaining(s.postHideTicket,s.postHidePersona,inputHostStartProtectionNow(),&serial) &&
        serial==s.postHideWakeSerial;
}
static bool msiPostHideRootStartedFor(const std::string& sourceRoot,const std::vector<std::wstring>& positions) {
    if(!ymcc::msi::samePhysicalLocation(positions,msiForegroundIsolationLocations(sourceRoot)))return false;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    return g_msiNativeForegroundTestIo.postHideRootStarted && g_msiNativeForegroundTestIo.postHideRootStarted(sourceRoot);
#else
    const auto root=U2W(sourceRoot);DEVINST node=0;ULONG status=0,problem=0;
    return CM_Locate_DevNodeW(&node,const_cast<wchar_t*>(root.c_str()),CM_LOCATE_DEVNODE_NORMAL)==CR_SUCCESS &&
        CM_Get_DevNode_Status(&status,&problem,node,0)==CR_SUCCESS && (status&DN_STARTED) &&
        !(status&DN_HAS_PROBLEM) && problem==0;
#endif
}

// Private cold substep of the existing lease. HostMx then foreground mutex.
// No native UI hide/restore, target admission, second worker or renewed period.
static bool msiColdReturnLeaseMatchesLocked(uint64_t ticket,uint64_t power) {
    const auto& s=g_msiNativeForegroundLease;
    return s.active && s.coldReturnCycle && s.coldReturnTicket==ticket && s.power==power &&
        s.coldReturnIntent==g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
}
static bool msiColdReturnContextCurrentLocked() {
    const auto& s=g_msiNativeForegroundLease;
    const auto now=inputHostStartProtectionNow();const auto view=g_virtualStartProtection.view(now);
    return msiColdReturnLeaseMatchesLocked(view.requestId,currentPowerGeneration()) && !s.coldReturnAborted &&
        nativeForegroundProfile()==ymcc::nativeForeground::Profile::Gpd &&
        view.locked && view.remainingMs && view.persona=="disabled" && now<s.coldReturnDeadline &&
        g_virtualStartProtection.canContinueExecution(s.coldReturnTicket,"disabled",now) &&
        inputHostConfiguredPersona()=="disabled" && !g_realStickTest.virtualTargetEnabled &&
        !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) &&
        !g_msiNativeForegroundPowerHold && g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready &&
        g_inputReady && hardwareWriteAllowed() && !g_inputHostPowerAdmissionRevoked &&
        !inputHostExitFenceRaised() && !g_exitRequested && !g_inputCaptureStop &&
        !g_externalPriorityVirtualSuspended && !g_shortcutRecordingActive &&
        !physicalReleaseWindowActive() && !msiForegroundIsolationBorrowBusy() &&
        !g_inputHostProcess && !g_inputHostHello && !g_inputHostPrepared && !g_inputHostPrepareMayHaveReachedHost &&
        !g_inputHostNeutralized && !g_inputHostActive && !g_hidHideVisibilityJournal.active &&
        !g_inputHostReleaseQuarantined && !g_msiSteamDeckReleaseDebt.required();
}
// Contract13 is emitted only by the lifetime-bound Managed cycle before its
// sole CyclePort call. Require the actual exact writer to have returned too.
static bool msiColdReturnPreMutationRejectionProvenLocked() {
    const auto& s=g_msiNativeForegroundLease;const auto& r=s.coldReturnReceipt;
    return s.coldReturnCycle && s.profile==ymcc::nativeForeground::Profile::Gpd &&
        s.cycleIssued && s.helperXusbTarget && !g_msiNativeForegroundCyclePending &&
        !s.cycleProcess && !s.nativeRefreshWriter && s.helperExitQueryAttempted &&
        s.helperExitQuerySucceeded && s.helperExitCode==13 && r.spent &&
        r.ticket==s.coldReturnTicket && r.power==s.power &&
        ascii_lower(r.root)==ascii_lower(s.root) && ascii_lower(s.helperGameTarget)==ascii_lower(s.root);
}
static bool msiColdReturnCleanupLocked(const char* reason) {
    auto& s=g_msiNativeForegroundLease;if(!s.coldReturnCycle)return true;
    s.coldReturnAborted=true;
    if(s.postHideCancelEvent)(void)SetEvent(s.postHideCancelEvent);
    if(g_msiNativeForegroundCyclePending)(void)msiNativeForegroundPollCycleLocked();
    if(g_msiNativeForegroundCyclePending || s.cycleProcess || s.nativeRefreshWriter)return false;
    // Cancellation never borrows a successful terminal result as completion.
    s.coldReturnReceipt.preMutationRejected=msiColdReturnPreMutationRejectionProvenLocked();
    s.coldReturnReceipt.accepted=false;
    msiNativeForegroundReceipt("cold-return-writer-retired",reason);
    msiNativeForegroundClearLeaseLocked();return !s.active;
}
static bool msiColdReturnMaintainOwner(const char* reason,bool exiting) {
    if(!g_coldReturnCycleOwned.load(std::memory_order_acquire))return false;
    std::unique_lock<InputHostMutex> host(g_inputHostMx,std::try_to_lock);if(!host.owns_lock())return true;
    std::unique_lock lease(g_msiNativeForegroundMx,std::try_to_lock);if(!lease.owns_lock())return true;
    auto& s=g_msiNativeForegroundLease;if(!s.coldReturnCycle)return false;
    if(g_msiNativeForegroundCyclePending)(void)msiNativeForegroundPollCycleLocked();
    if(exiting || !msiColdReturnContextCurrentLocked())(void)msiColdReturnCleanupLocked(reason);
    return true;
}
// 0: same helper + exact Job durably retired; 1: that helper is pending;
// 2: unavailable/failed. Only a proven pre-mutation rejection may resume once.
static int msiColdReturnCycleStepLocked(uint64_t ticket,uint64_t power,uint64_t intent,
        uint64_t deadline,const std::string& root) {
    auto& s=g_msiNativeForegroundLease;
    const auto sameReceipt=[&](){const auto& r=s.coldReturnReceipt;
        // Spend is immutable for the original ticket/power/root. Intent is only
        // an admission constraint, never a refund key. Other roots cannot borrow it.
        return r.spent && r.ticket==ticket;
    };
    if(s.active){
        if(!msiColdReturnLeaseMatchesLocked(ticket,power) || ascii_lower(s.root)!=ascii_lower(root))return 2;
        if(!msiColdReturnContextCurrentLocked()){(void)msiColdReturnCleanupLocked("cold-context-retired");return 2;}
        if(g_msiNativeForegroundCyclePending)(void)msiNativeForegroundPollCycleLocked();
        if(g_msiNativeForegroundCyclePending || s.cycleProcess || s.nativeRefreshWriter)return 1;
        if(msiColdReturnPreMutationRejectionProvenLocked()){
            (void)msiColdReturnCleanupLocked("cold-pre-mutation-rejection");
            return s.coldReturnReceipt.preMutationResumes<1?1:2;
        }
        const bool accepted=s.cycleIssued && s.cycleAccepted && s.helperExitCode==0;
        s.coldReturnReceipt.accepted=accepted;return accepted?0:2;
    }
    const bool same=sameReceipt();
    const bool resume=same && s.coldReturnReceipt.power==power &&
        ascii_lower(s.coldReturnReceipt.root)==ascii_lower(root) &&
        s.coldReturnReceipt.preMutationRejected && s.coldReturnReceipt.preMutationResumes<1;
    if(same && !resume)return s.coldReturnReceipt.power==power && s.coldReturnReceipt.intent==intent &&
        ascii_lower(s.coldReturnReceipt.root)==ascii_lower(root) && s.coldReturnReceipt.accepted?0:2;
    if(s.cycleProcess || s.nativeRefreshWriter || s.nativeVisibilitySession || s.restorePending ||
        g_msiNativeForegroundCyclePending || g_postHideCycleOwned ||
        nativeForegroundProfile()!=ymcc::nativeForeground::Profile::Gpd || !nativeForegroundRootCandidate(root))return 2;
    const auto positions=msiForegroundIsolationLocations(root);
    HidHidePhysicalIdentity anchor;anchor.resolved=true;anchor.baseContainerDeviceInstanceId=root;
    const auto inventory=msiNativeForegroundXusbInventory(anchor);
    unsigned exact=0;for(const auto& item:inventory.interfaces)
        if(nativeForegroundCurrentXusbTargetAdmitted(root,item) && ascii_lower(item.instanceId)==ascii_lower(root))++exact;
    if(positions.empty() || !inventory.complete || inventory.interfaces.size()!=1 || exact!=1 ||
        !msiPostHideRootStartedFor(root,positions))return 2;
    // Spend before preparation/launch; record commit failure cannot refund it.
    const auto attemptedEdge=s.attemptedEdge;
    auto spentReceipt=std::move(s.coldReturnReceipt);
    s=MsiNativeForegroundLease{}; // discard all stale native direction/helper fields
    s.attemptedEdge=attemptedEdge;s.coldReturnReceipt=std::move(spentReceipt);
    // The original ticket may resume once after proven pre-mutation rejection;
    // every other spent attempt stays spent. Set GPD before activating the lease.
    const bool observedRootHidden=s.coldReturnReceipt.observedRootHidden;
    const unsigned resumes=resume?s.coldReturnReceipt.preMutationResumes+1:0;
    auto observedOrigin=std::move(s.coldReturnReceipt.observedOrigin);
    s.coldReturnReceipt={ticket,power,intent,root,true,false,observedRootHidden,false,resumes,std::move(observedOrigin)};
    s.profile=ymcc::nativeForeground::Profile::Gpd;
    s.active=true;s.coldReturnCycle=true;s.configurationReturned=true;
    g_coldReturnCycleOwned.store(true,std::memory_order_release);
    s.coldReturnTicket=ticket;s.power=power;s.coldReturnIntent=intent;s.coldReturnDeadline=deadline;
    s.root=root;s.locations=positions;
    if(!msiColdReturnContextCurrentLocked())return 2;
    if(resume)msiNativeForegroundReceipt("cold-pre-mutation-resume","same-OFF-period;exact-writer-returned;no-device-call");
    anchor.deviceInstanceId=root;
    const int result=msiNativeForegroundCycleLocked(true,&anchor,true);
    msiNativeForegroundReceipt("cold-return-cycle-started","owned-current-exact-root;original-OFF-period");
    if(result==0)s.coldReturnReceipt.accepted=s.cycleAccepted && s.helperExitCode==0;
    return result;
}

static bool msiPostHideRootStartedLocked() {
    const auto& s=g_msiNativeForegroundLease;return msiPostHideRootStartedFor(s.root,s.locations);
}
static int msiPostHideSkip(uint64_t ticket,const char* reason) noexcept {
    try{appendNativeLifecycleLog("input-host-post-hide-cycle",{{"stage","not-required-or-unavailable"},
        {"requestId",ticket},{"reason",reason},{"driverOperationIssued",false},
        {"standardVirtualRouteRetained",true},{"externalSuppressionVerified",false}});}catch(...){}
    return 0;
}
// No node-enable claim: this operation never issues CM Disable/Enable. Cleanup
// waits for the original exact writer before the visibility journal may return.
static bool msiPostHideCleanupLocked(const char* reason) {
    auto& s=g_msiNativeForegroundLease;
    if(!s.postHideCycle)return true;
    s.postHideAborted=true;
    if(s.postHideCancelEvent)(void)SetEvent(s.postHideCancelEvent); // revoke unentered SDK work; never kill an in-flight cycle
    if(g_msiNativeForegroundCyclePending)(void)msiNativeForegroundPollCycleLocked();
    if(g_msiNativeForegroundCyclePending || s.cycleProcess || s.nativeRefreshWriter)return false;
    try{msiNativeForegroundReceipt("post-hide-writer-retired",reason);}catch(...){}
    msiNativeForegroundClearLeaseLocked();return !s.active;
}
// A delayed cleanup may repay only the same unfinished Host. It cannot borrow
// a later target or clear the visibility journal merely because the player
// request expired. This marker is private to the GPD post-hide candidate.
static constexpr std::string_view kMsiPostHideCleanupBase="fault-stop:post-hide-cycle-cancelled";
static constexpr std::string_view kMsiPostHideCleanupPrefix="fault-stop:post-hide-cycle-cancelled:";
static bool msiPostHideCleanupIsWork(std::string_view reason) noexcept {
    return reason==kMsiPostHideCleanupBase || reason.starts_with(kMsiPostHideCleanupPrefix);
}
static std::string msiPostHideCleanupWork(uint64_t epoch,const std::string& runId) {
    return std::string(kMsiPostHideCleanupPrefix)+std::to_string(epoch)+":"+runId;
}
// Caller owns HostMx. Original UI expiry does not erase the old return duty.
// Pre-target state also protects a later target that might share a kept Host.
static bool msiPostHideCleanupWorkCurrentLocked(std::string_view reason) noexcept {
    if(!msiPostHideCleanupIsWork(reason))return true;
    if(!reason.starts_with(kMsiPostHideCleanupPrefix) || g_inputHostPrepared ||
        g_inputHostPrepareMayHaveReachedHost || g_inputHostActive)return false;
    const auto identity=reason.substr(kMsiPostHideCleanupPrefix.size());
    const auto separator=identity.find(':');if(separator==std::string_view::npos)return false;
    char epoch[32];const auto encoded=std::to_chars(epoch,epoch+sizeof(epoch),g_inputHostEpoch);
    return encoded.ec==std::errc{} &&
        identity.substr(0,separator)==std::string_view(epoch,static_cast<std::size_t>(encoded.ptr-epoch)) &&
        identity.substr(separator+1)==g_inputHostRunId;
}
static void msiPostHideCleanupWorkRetiredLocked(std::string_view reason,const char* throat) noexcept {
    try{appendNativeLifecycleLog("input-host-post-hide-cleanup-retired",{
        {"reason",std::string(reason)},{"throat",throat?throat:"unknown"},
        {"hostEpoch",g_inputHostEpoch},{"runId",g_inputHostRunId},
        {"hostPrepared",g_inputHostPrepared},{"prepareMayHaveReachedHost",g_inputHostPrepareMayHaveReachedHost},
        {"hostActive",g_inputHostActive},{"stopInvoked",false},{"ownershipCleared",false},
        {"scope","original-pre-target-Host-tuple"}});}catch(...){}
}

static bool msiPostHideCancelForStop(const char* reason) {
    // Caller owns HostMx; no new lock on unchanged ROG/MSI/standard paths.
    if(!g_postHideCycleOwned.load(std::memory_order_acquire))return true;
    std::lock_guard leaseLock(g_msiNativeForegroundMx);
    if(msiPostHideCleanupLocked(reason))return true;
    auto& deferred=g_msiNativeForegroundLease.deferredVirtualWork;
    const auto cleanup=msiPostHideCleanupWork(g_msiNativeForegroundLease.postHideEpoch,g_msiNativeForegroundLease.postHideRunId);
    if(std::find(deferred.begin(),deferred.end(),cleanup)==deferred.end())deferred.push_back(cleanup);
    return false;
}
// Returns0 only after the SDK helper and a newer current source read. It does
// not assert old external handles closed. Caller holds HostMx and R3 ownership.
static int msiPostHideCycleStep(uint64_t ticket) {
    std::lock_guard leaseLock(g_msiNativeForegroundMx);
    auto& s=g_msiNativeForegroundLease;
    if(!s.postHideCycle && nativeForegroundProfile()!=ymcc::nativeForeground::Profile::Gpd)return 0;
    if(s.postHideCycle && !msiPostHideContextCurrentLocked())return 2;
    if(!s.postHideCycle){
        if(s.active || g_msiNativeForegroundCyclePending || s.cycleProcess || s.nativeRefreshWriter)return 2;
        const auto originalNow=inputHostStartProtectionNow();
        const auto view=g_virtualStartProtection.view(originalNow);
        auto& j=g_hidHideVisibilityJournal;
        std::uint64_t wakeSerial=0;
        const auto remaining=inputHostGpdStartRemaining(ticket,view.persona,originalNow,&wakeSerial);
        if(!ticket || view.requestId!=ticket || !remaining ||
            view.persona!=inputHostConfiguredPersona() || !j.active || !j.before.valid ||
            g_inputHostPrepared || g_inputHostPrepareMayHaveReachedHost || g_inputHostActive)return 2;
        // Missing optional refresh recognition is not a new prerequisite for
        // standard virtual output. Only an already-started owned action may
        // require cancellation/return instead of this pre-mutation no-op.
        if(j.physicalSources.size()!=1 || !j.physical.resolved ||
            !nativeForegroundRootCandidate(j.physical.baseContainerDeviceInstanceId) ||
            !nativeForegroundGameInputAdmitted(true,hidHideIsVirtualIdentity(j.physical.deviceInstanceId,j.physical.baseContainerDeviceInstanceId),
                j.physical.vendorId,j.physical.productId,j.physical.usagePage,j.physical.usage))
            return msiPostHideSkip(ticket,"selected-refresh-class-not-proven");
        // HC skips re-cycling an already hidden selected source. A carried
        // continuous-persona journal does not reopen consumers; foreign hiding
        // grants no restart ownership to this candidate.
        // A newly transferred native hide has not completed this virtual
        // execution's post-hide cycle. Only its exact authenticated journal
        // stamp removes this skip; all original mutation proofs below remain.
        const bool nativeHandoffCycle=j.nativeHandoffCycleRequest==ticket &&
            j.nativeHandoffCyclePower==currentPowerGeneration() && j.nativeHandoffCycleEpoch==g_inputHostEpoch &&
            j.transactionId==g_inputHostRunId &&
            hidHideContainsPath(j.hiddenByUs,j.physical.baseContainerDeviceInstanceId) &&
            hidHideContainsPath(j.hiddenByUs,j.physical.deviceInstanceId);
        if(!nativeHandoffCycle && (hidHideContainsPath(j.before.hidden,j.physical.baseContainerDeviceInstanceId) ||
            hidHideContainsPath(j.before.hidden,j.physical.deviceInstanceId)))return msiPostHideSkip(ticket,"selected-source-already-hidden");
        if(!hidHideContainsPath(j.hiddenByUs,j.physical.baseContainerDeviceInstanceId) ||
            !hidHideContainsPath(j.hiddenByUs,j.physical.deviceInstanceId))return 2;
        if(!j.physicalSources.front().resolved ||
            ascii_lower(j.physicalSources.front().deviceInstanceId)!=ascii_lower(j.physical.deviceInstanceId) ||
            ascii_lower(j.physicalSources.front().baseContainerDeviceInstanceId)!=ascii_lower(j.physical.baseContainerDeviceInstanceId))return 2;
        const auto expectedPower=currentPowerGeneration();const auto expectedEpoch=g_inputHostEpoch;
        const auto expectedRun=g_inputHostRunId;const auto expectedProcess=g_inputHostProcess;
        const auto root=j.physical.baseContainerDeviceInstanceId;
        const auto positions=msiForegroundIsolationLocations(root);
        const auto xusb=msiNativeForegroundXusbInventory(j.physical);
        if(positions.empty() || !xusb.complete)return msiPostHideSkip(ticket,"selected-xusb-location-or-inventory-unproven");
        unsigned exact=0;
        for(const auto& item:xusb.interfaces)if(nativeForegroundCurrentXusbTargetAdmitted(root,item) &&
            ascii_lower(item.instanceId)==ascii_lower(root))++exact;
        if(exact!=1)return msiPostHideSkip(ticket,"exact-selected-xusb-node-not-proven");
        if(!msiPostHideRootStartedFor(root,positions))return msiPostHideSkip(ticket,"selected-node-readiness-unproven-no-cycle");
        HidHideVisibilitySnapshot after;
        if(!msiNativeForegroundSnapshot(after) || !after.valid || !after.cloaking ||
            hidHideInverseApplicationListState(after.inverseRaw)!="off" ||
            !hidHideContainsPath(after.applications,W2U(hidHideCurrentModulePath())) ||
            !hidHideContainsPath(after.hidden,root) || !hidHideContainsPath(after.hidden,j.physical.deviceInstanceId))return 2;
        if(currentPowerGeneration()!=expectedPower || g_inputHostEpoch!=expectedEpoch || g_inputHostRunId!=expectedRun ||
            g_inputHostProcess!=expectedProcess || g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId!=ticket)return 2;
        // The ordinary journal already owns durable hide/return responsibility.
        // This lease owns only the one exact helper, never a second hide writer.
        auto coldReceipt=std::move(s.coldReturnReceipt);
        s=MsiNativeForegroundLease{};s.coldReturnReceipt=std::move(coldReceipt);s.profile=ymcc::nativeForeground::Profile::Gpd;
        s.active=true;s.configurationReturned=true;s.postHideCycle=true;
        g_postHideCycleOwned.store(true,std::memory_order_release);
        s.root=root;s.locations=positions;s.power=expectedPower;s.postHideHostProcess=expectedProcess;
        s.postHideTicket=ticket;s.postHideEpoch=g_inputHostEpoch;s.postHideRunId=g_inputHostRunId;s.postHidePersona=view.persona;
        s.postHideWakeSerial=wakeSerial;
        s.postHideDeadline=originalNow+remaining;
        if(g_inputHostSwitchRecoveryStartDeadline)s.postHideDeadline=(std::min)(s.postHideDeadline,g_inputHostSwitchRecoveryStartDeadline);
        if(!msiPostHideContextCurrentLocked())return 2;
        // Consume once when the original bounded helper owner is established,
        // before dispatch. Failure/late work cannot grant another activation.
        if(nativeHandoffCycle){
            j.nativeHandoffCycleRequest=0;j.nativeHandoffCyclePower=0;j.nativeHandoffCycleEpoch=0;
        }
        auto target=j.physical;target.deviceInstanceId=root;
        const int begin=msiNativeForegroundCycleLocked(false,&target,true);
        msiNativeForegroundReceipt("post-hide-cycle-started",nativeHandoffCycle?
            "owned-native-handoff;original-GPD-cycle;external-input-unverified":
            "HC-selected-root-cycle;SDK-only-no-external-proof");
        if(begin==2)return 2;
    }
    if(s.postHideTicket!=ticket || !msiPostHideContextCurrentLocked())return 2;
    if(g_msiNativeForegroundCyclePending)(void)msiNativeForegroundPollCycleLocked();
    if(g_msiNativeForegroundCyclePending || s.cycleProcess || s.nativeRefreshWriter)return 1;
    if(!s.cycleAccepted || s.helperExitCode!=0)return 2;
    if(!s.postHideTerminalTick){
        s.postHideTerminalTick=GetTickCount64();s.postHideBaselineSequence=msiPostHideLatestSourceLocked().sequence;
        xinputSlotsMarkDiscoveryDirty();xinputSlotsRequestIdentityReprobe(0xFu,"gpd-post-hide-cycle-returned");
        g_xinputPreTargetBoundaryScanned.store(false,std::memory_order_relaxed);
        g_xinputSlotFullScanEpochStart.store(g_xinputTopologyEpoch.load(std::memory_order_acquire),std::memory_order_release);
        g_xinputSlotFullScanOnce.store(true,std::memory_order_relaxed);
        msiNativeForegroundReceipt("post-hide-source-wait","same-R3;newer-source-read-required");return 1;
    }
    if(!s.postHideReadmitted)return 1;
    const auto topology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
    if(s.postHideValidatedTopology!=topology){
        if(!msiPostHideRootStartedLocked())return 1;
        s.postHideValidatedTopology=topology;
    }
    const auto source=msiPostHideLatestSourceLocked();
    if(source.route!=ymcc::input::recoveryHealth::Route::XInput || !source.live || !source.stable ||
        !source.incarnation || !source.continuity || source.continuity==(std::numeric_limits<uint64_t>::max)() ||
        source.sequence<=s.postHideBaselineSequence || source.started<s.postHideSourceAfterTick ||
        source.identity!=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire) ||
        !ymcc::input::recoveryHealth::fresh(source,GetTickCount64(),s.power,topology))return 1;
    if(!msiPostHideContextCurrentLocked())return 2;
    msiNativeForegroundReceipt("post-hide-source-returned","new-current-source-read;external-visibility-unverified");
    msiNativeForegroundClearLeaseLocked();return s.active?2:0;
}

// An already-owned writer survives temporary loss of the model/profile result.
// Poll only that owner, even when new OEM activation is no longer permitted.
static bool msiPostHideMaintainOwner(const char* reason,bool exiting=false) {
    if(!g_postHideCycleOwned.load(std::memory_order_acquire))return false;
    std::unique_lock<InputHostMutex> hostLock(g_inputHostMx,std::try_to_lock);if(!hostLock.owns_lock())return true;
    std::unique_lock leaseLock(g_msiNativeForegroundMx,std::try_to_lock);if(!leaseLock.owns_lock())return true;
    auto& state=g_msiNativeForegroundLease;
    if(!state.postHideCycle){g_postHideCycleOwned.store(false,std::memory_order_release);return false;}
    if(exiting){state.postHideAborted=true;state.deferredVirtualWork.clear();}
    if(g_msiNativeForegroundCyclePending)(void)msiNativeForegroundPollCycleLocked();
    if(exiting || !msiPostHideContextCurrentLocked())(void)msiPostHideCleanupLocked(reason);
    return true;
}

static bool msiNativeForegroundRestoreLocked(const char* reason) {
    controllerIsolationHidWithdrawPermit();
    if(g_msiNativeForegroundLease.coldReturnCycle)return msiColdReturnCleanupLocked(reason);
    if(g_msiNativeForegroundLease.postHideCycle)return msiPostHideCleanupLocked(reason);
    auto& s=g_msiNativeForegroundLease;
    if(!s.active)return true;
    g_msiNativeForegroundWatchGameSources=false;
    s.pendingGameSources.clear();
    s.restorePending=true;
    if(!s.configurationReturned){
        try{msiNativeForegroundReceipt("before-return","native-input-before-visibility-return");}catch(...){}
        // Durable issued paths are authoritative even if allocation/exception
        // interrupted the local owned-vector update after the write-ahead mark.
        if(s.nativeVisibilitySession && s.nativeVisibilitySession->record()->nativeVisibility)
            for(const auto& path:s.nativeVisibilitySession->record()->nativeVisibility->paths)
                if(path.phase==ymcc::nativeRefreshWriter::VisibilityPathPhase::MayHaveIssued && !hidHideContainsPath(s.owned,path.instanceId))
                    s.owned.push_back(U2W(path.instanceId));
        HidHideVisibilitySnapshot before;
        const bool beforeRead=msiNativeForegroundSnapshot(before) && before.valid;
        if(!beforeRead && s.profile!=ymcc::nativeForeground::Profile::Msi){msiNativeForegroundReceipt("restore-failed","visibility-query-failed");return false;}
        bool ok=true;
        for(const auto& path:s.owned)if(!beforeRead || hidHideContainsPath(before.hidden,W2U(path)))
            ok=msiNativeForegroundVisibility(path,false) && ok;
        HidHideVisibilitySnapshot after;
        if(!ok || !msiNativeForegroundSnapshot(after) || !after.valid){msiNativeForegroundReceipt("restore-failed","unhide-or-readback-failed");return false;}
        for(const auto& path:s.owned)if(hidHideContainsPath(after.hidden,W2U(path))){msiNativeForegroundReceipt("restore-failed","own-path-still-hidden");return false;}
        bool cloakRestored=false;
        if(s.cloakChanged){
            auto normalize=[](std::vector<std::wstring> list){std::sort(list.begin(),list.end());return list;};
            if(normalize(after.hidden)==normalize(s.before.hidden) && normalize(after.applications)==normalize(s.before.applications)){
                if(!msiNativeForegroundCloak(s.before.cloaking)){msiNativeForegroundReceipt("restore-failed","cloak-restore-failed");return false;}
                cloakRestored=true;
            }else msiNativeForegroundReceipt("cloak-retained","third-party-state-changed");
        }
        HidHideVisibilitySnapshot returned;
        if(!msiNativeForegroundSnapshot(returned) || !returned.valid){msiNativeForegroundReceipt("restore-failed","final-readback-failed");return false;}
        for(const auto& path:s.owned)if(hidHideContainsPath(returned.hidden,W2U(path))){msiNativeForegroundReceipt("restore-failed","returned-path-hidden-again");return false;}
        if(cloakRestored && returned.cloaking!=s.before.cloaking){msiNativeForegroundReceipt("restore-failed","cloak-readback-mismatch");return false;}
        s.diagnosticReturnConfiguration=msiNativeForegroundConfigurationEvidence(returned,
            s.diagnosticSelectedPaths,s.diagnosticSelectedPathsComplete,s.owned);
        if(!msiNativeForegroundDurableReturn(returned)){
            msiNativeForegroundReceipt("restore-failed","durable-visibility-settlement-pending");return false;
        }
        s.configurationReturned=true;
        msiNativeForegroundReceipt("config-returned",reason);
    }
    // Configuration can be returned during a live helper, including exit and
    // suspend. Never keep our hidden entries stranded just because it is slow.
    // But no replacement helper/target is allowed until this SAME handle ends.
    if(g_msiNativeForegroundCyclePending.load(std::memory_order_acquire)){
        msiNativeForegroundReceipt("restore-wait","same-cycle-helper-still-live");return false;
    }

    if(s.cycleForRestore && s.cycleIssued && s.cycleAccepted && !s.refreshSeriesActive){
        msiNativeForegroundReceipt("restored",reason);msiNativeForegroundClearLeaseLocked();return true;
    }
    if(s.power!=currentPowerGeneration() || s.suspendSerial!=g_sharedPowerSuspendSerial.load(std::memory_order_acquire) ||
        g_msiNativeForegroundPowerHold.load(std::memory_order_acquire) ||
        g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready){
        msiNativeForegroundReceipt("restored-config","no-port-refresh-during-power-transition");
        msiNativeForegroundClearLeaseLocked();return !s.active;
    }
    const auto current=msiForegroundIsolationLocations(s.root);
    if(!ymcc::msi::samePhysicalLocation(s.locations,current)){
        msiNativeForegroundReceipt("restored-old-config","old-root-no-longer-current");msiNativeForegroundClearLeaseLocked();return true;
    }
    // The original native helper is truly retired and our exact configuration
    // was read back returned above. A CURRENT virtual request is the next owner;
    // do not create an inverse native HID/XUSB writer in front of that request.
    // OFF/expired intents and unproven enable debt retain the original restore.
    if(s.configurationReturned && !s.virtualContinuationEnded && !s.deferredVirtualWork.empty() &&
        msiNativeForegroundGpdVirtualIntentCurrentLocked() &&
        msiNativeForegroundGpdTerminalGameWriterReturnedLocked() && !g_inputHostProcess &&
        !g_hidHideVisibilityJournal.active && !g_inputHostReleaseQuarantined){
        msiNativeForegroundReceipt("handoff-config-returned","current-virtual-request-no-inverse-native-refresh");
        msiNativeForegroundClearLeaseLocked();return !s.active;
    }
    // The exact live writer has now retired and our own configuration returned.
    // A still-current pre-Host observation is independent of the expired R3.
    // It may settle this old owner without issuing another inverse writer;
    // only the ordinary dispatcher/recovery may later admit a target.
    // Prior failed-series/Enable debt, OFF, unknown retirement and other OEMs
    // retain the existing return route. No old request is made valid again.
    if(s.profile==ymcc::nativeForeground::Profile::Gpd && s.configurationReturned && !s.refreshFailed &&
        !s.retiredNativeWriterAttempt.empty() && msiNativeForegroundGpdTerminalGameWriterReturnedLocked() &&
        !g_inputHostProcess && !g_hidHideVisibilityJournal.active && !g_inputHostReleaseQuarantined &&
        inputHostRuntimeRecoveryNativeWriterMayReturnLocked(s.retiredNativeWriterAttempt)){
        msiNativeForegroundReceipt("handoff-config-returned","exact-retired-writer-current-prehost-demand;old-period-unchanged");
        msiNativeForegroundClearLeaseLocked();return !s.active;
    }
    const auto edge=g_msiNativeForegroundEdge.load(std::memory_order_acquire);
    if(s.profile!=ymcc::nativeForeground::Profile::Msi && s.refreshFailed && s.refreshPlanForRestore && s.refreshPlanAttemptEdge==edge)return false;
    if(s.cycleForRestore && s.cycleIssued && !s.cycleAccepted && s.refreshAttemptEdge==edge){
        s.refreshFailed=true;return false; // no automatic retry on this consumed edge
    }
    const int cycle=hardwareWriteAllowed() && g_inputReady.load(std::memory_order_acquire)?
        nativeForegroundRefreshLocked(true):MsiNativeForegroundLease::kPowerDeferred;
    if(cycle==MsiNativeForegroundLease::kPowerDeferred){
        if(!g_msiNativeForegroundWorkPowerDeferred.exchange(true,std::memory_order_acq_rel))
            msiNativeForegroundReceipt("restore-wait","public-power-refresh-not-admitted");
        return false;
    }
    if(cycle==1){msiNativeForegroundReceipt("restore-wait","port-refresh-pending");return false;}
    if(cycle!=0){
        s.refreshFailed=true;msiNativeForegroundReceipt("restore-refresh-unverified",reason);return false;
    }
    msiNativeForegroundReceipt("restored",reason);msiNativeForegroundClearLeaseLocked();return true;
}
// A terminal failed refresh is different from a live/unknown helper. Once our
// own config is proven returned, the existing virtual lifecycle may take over
// the device; it does its normal mode/target protocol, not an MSI replacement.
static bool msiNativeForegroundVirtualHandoffLocked() {
    auto& s=g_msiNativeForegroundLease;
    if(s.active && s.configurationReturned && s.refreshFailed && !g_msiNativeForegroundCyclePending){
        // The legacy handoff must not bypass the same GPD writer/enable guard
        // used by the current-request shortcut above. Other OEM return protocols
        // retain their original admission; no new refresh or budget is created.
        if(s.profile==ymcc::nativeForeground::Profile::Gpd &&
            !msiNativeForegroundGpdTerminalGameWriterReturnedLocked())return false;
        msiNativeForegroundReceipt("handoff-config-returned","physical-refresh-unverified-terminal-helper");
        msiNativeForegroundClearLeaseLocked();return !s.active;
    }
    return !s.active;
}
// VC-GPD-NATIVE-VIRTUAL-HANDOFF-20261025-R1.
// Native UI isolation and virtual hiding are consecutive owners of the SAME
// selected game paths. Returning configuration between them lets a consumer
// open a new physical handle before PREPARE. Preserve private configuration
// while the original helper retires; the normal virtual hide/readback is the
// only recipient. No root reset, replacement helper, timer or mode write.
// ROG/MSI retain their hardware-specific return protocols. OneX/AYA do not
// gain native mutation authority from this transport handoff.
static bool msiNativeForegroundVirtualHandoffContextLocked() {
    const auto& s=g_msiNativeForegroundLease;
    return msiNativeForegroundGpdVirtualRequestCurrentLocked() && !s.configurationReturned &&
        !s.restorePending && !s.cloakChanged && !s.owned.empty() && s.diagnosticSelectedPathsComplete;
}
static bool msiNativeForegroundHoldForVirtualLocked(const char* reason) {
    auto& s=g_msiNativeForegroundLease;
    if(!msiNativeForegroundVirtualHandoffContextLocked())return false;
    if(!s.virtualHandoffHeld){
        // A live virtual session/journal has its own owner; never turn this
        // repair into another route for stealing an existing transaction.
        if(g_inputHostProcess || g_hidHideVisibilityJournal.active ||
            !ymcc::msi::samePhysicalLocation(s.locations,msiForegroundIsolationLocations(s.root)))return false;
        s.virtualHandoffRequest=g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId;
        s.virtualHandoffHeld=true;
        try { msiNativeForegroundReceipt("handoff-preserved",reason); } catch(...) {}
    }
    return true;
}
static uint64_t msiNativeForegroundVirtualHandoffTicket() {
    std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
    const auto& s=g_msiNativeForegroundLease;
    return s.virtualHandoffHeld?s.virtualHandoffRequest:0;
}
static bool msiNativeForegroundVirtualHandoffRequired(uint64_t ticket) {
    std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
    return g_msiNativeForegroundLease.virtualHandoffHeld && g_msiNativeForegroundLease.virtualHandoffRequest==ticket;
}
// Current donor authority for one actual idempotent hide dispatch. This is
// separate from a fresh-hidden-list observation: only our original native
// baseline can prove the already-hidden path was not borrowed from a third party.
static bool msiNativeForegroundReassertionBaseline(const std::string& target,HidHideVisibilitySnapshot& baseline) {
    std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
    const auto& s=g_msiNativeForegroundLease;
    const auto& journal=g_hidHideVisibilityJournal;
    if(!s.virtualHandoffHeld || !msiNativeForegroundVirtualHandoffContextLocked() ||
        g_inputHostStartProtectionBoundTicket!=s.virtualHandoffRequest || g_msiNativeForegroundCyclePending || s.cycleProcess ||
        !journal.active || journal.physicalSources.size()!=1 ||
        ascii_lower(journal.physical.baseContainerDeviceInstanceId)!=ascii_lower(s.root) ||
        !s.before.valid || hidHideContainsPath(s.before.hidden,target) || !hidHideContainsPath(s.owned,target) ||
        !ymcc::msi::samePhysicalLocation(s.locations,msiForegroundIsolationLocations(s.root)))return false;
    baseline=s.before;
    return true;
}
// A complete preexisting owned set, authenticated by the same current native
// owner. Atomic persistence of this already-hidden set closes a forced-stop
// gap between per-path reassertions; it never preclaims future hide requests.
static bool msiNativeForegroundNativeHandoffSnapshot(std::vector<std::string>& owned,HidHideVisibilitySnapshot& baseline) {
    std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
    const auto& s=g_msiNativeForegroundLease;
    const auto& journal=g_hidHideVisibilityJournal;
    if(!s.virtualHandoffHeld || !msiNativeForegroundVirtualHandoffContextLocked() ||
        g_inputHostStartProtectionBoundTicket!=s.virtualHandoffRequest || g_msiNativeForegroundCyclePending || s.cycleProcess ||
        !journal.active || journal.physicalSources.size()!=1 || !journal.physical.resolved ||
        ascii_lower(journal.physical.baseContainerDeviceInstanceId)!=ascii_lower(s.root) ||
        !s.before.valid || s.owned.empty() || s.owned.size()>16 ||
        !ymcc::msi::samePhysicalLocation(s.locations,msiForegroundIsolationLocations(s.root)))return false;
    std::vector<std::string> exact;
    for(const auto& path:s.owned){
        const auto target=W2U(path);
        if(target.empty() || hidHideContainsPath(s.before.hidden,target) || hidHideIsVirtualIdentity(target,{}))return false;
        hidHideAppendTarget(exact,target);
    }
    if(exact.size()!=s.owned.size())return false;
    auto original=s.before;
    owned.swap(exact);baseline=std::move(original);
    return true;
}
static bool msiNativeForegroundAppendHandoffTargets(std::vector<std::string>& targets) {
    std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
    const auto& s=g_msiNativeForegroundLease;
    if(!s.virtualHandoffHeld)return true;
    const auto& journal=g_hidHideVisibilityJournal;
    if(!msiNativeForegroundVirtualHandoffContextLocked() || g_msiNativeForegroundCyclePending || s.cycleProcess ||
        g_inputHostStartProtectionBoundTicket!=s.virtualHandoffRequest || !journal.active ||
        journal.physicalSources.size()!=1 || ascii_lower(journal.physical.baseContainerDeviceInstanceId)!=ascii_lower(s.root))return false;
    auto proposed=targets;
    for(const auto& path:s.owned)hidHideAppendTarget(proposed,W2U(path));
    if(proposed.size()>16)return false;
    targets.swap(proposed);
    return true;
}
static std::shared_ptr<ymcc::nativeRefreshWriter::Session> msiNativeForegroundHandoffWriterSession(){
    std::lock_guard leaseLock(g_msiNativeForegroundMx);
    const auto& lease=g_msiNativeForegroundLease;
    if(!msiNativeForegroundVirtualHandoffContextLocked() || g_msiNativeForegroundCyclePending || lease.cycleProcess ||
        !lease.nativeVisibilitySession || !lease.nativeVisibilitySession->ready())return {};
    const auto* record=lease.nativeVisibilitySession->record();
    if(record->active || !record->nativeVisibility || record->nativeVisibility->transactionId!=lease.nativeVisibilityTransaction)return {};
    return lease.nativeVisibilitySession;
}
static bool msiNativeForegroundAcceptVirtualJournal(const HidHideVisibilitySnapshot& after) {
    std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
    auto& s=g_msiNativeForegroundLease;
    if(!s.virtualHandoffHeld)return true;
    auto& journal=g_hidHideVisibilityJournal;
    if(!msiNativeForegroundVirtualHandoffContextLocked() || g_msiNativeForegroundCyclePending || s.cycleProcess ||
        g_inputHostStartProtectionBoundTicket!=s.virtualHandoffRequest || !journal.active || journal.physicalSources.size()!=1 || !journal.physical.resolved ||
        ascii_lower(journal.physical.baseContainerDeviceInstanceId)!=ascii_lower(s.root) ||
        !nativeForegroundGameInputAdmitted(true,hidHideIsVirtualIdentity(journal.physical.deviceInstanceId,s.root),
            journal.physical.vendorId,journal.physical.productId,journal.physical.usagePage,journal.physical.usage) ||
        !after.valid || !after.cloaking || hidHideInverseApplicationListState(after.inverseRaw)!="off" ||
        !hidHideContainsPath(after.applications,W2U(hidHideCurrentModulePath())) ||
        !ymcc::msi::samePhysicalLocation(s.locations,msiForegroundIsolationLocations(s.root)))return false;
    // Every donor path must already have passed the actual durable reservation
    // and idempotent hide dispatch. Merely copying memory ownership here would
    // omit cold-return debt after a forced restart.
    for(const auto& path:s.owned)if(!hidHideContainsPath(after.hidden,W2U(path)) ||
        !hidHideContainsPath(journal.hiddenByUs,W2U(path)))return false;
    if(s.nativeVisibilitySession){
        // Confirm the complete Hosted ledger before removing native provenance.
        // Failure leaves BOTH duties retained; normal Stop owns compensation.
        if(!inputHostTargetReturnNativeHandoffDurableLocked(s.root,s.owned))return false;
        auto& session=*s.nativeVisibilitySession;
        if(!session.transferVisibility(session.record()->revision,s.nativeVisibilityTransaction))return false;
        // Durable transfer proves this exact current donor's provenance. It
        // does not prove its native refresh succeeded or old readers closed.
        // Preserve the existing virtual post-hide execution across that handoff.
        if(s.profile==ymcc::nativeForeground::Profile::Gpd){
            journal.nativeHandoffCycleRequest=s.virtualHandoffRequest;
            journal.nativeHandoffCyclePower=currentPowerGeneration();
            journal.nativeHandoffCycleEpoch=g_inputHostEpoch;
        }
    }
    try { msiNativeForegroundReceipt("handoff-transferred","virtual-journal-readback-accepted;external-input-unverified"); } catch(...) {}
    msiNativeForegroundClearLeaseLocked();
    return true;
}
static void msiNativeForegroundVirtualStartFinishedLocked(uint64_t ticket,bool keepForContinuation) noexcept {
    try {
        std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
        const auto& s=g_msiNativeForegroundLease;
        if(!s.virtualHandoffHeld || s.virtualHandoffRequest!=ticket)return;
        if(keepForContinuation && msiNativeForegroundVirtualHandoffContextLocked())return;
        g_msiNativeForegroundLease.virtualContinuationEnded=true;
        // An early Start rejection/exception cannot strand a private native
        // hide. Successful admission has already transferred it to journal.
        (void)msiNativeForegroundRestoreLocked("virtual-start-ended-before-ownership-transfer");
    } catch(...) { /* original lease/debt remains; never forge a return */ }
}

// Read-only diagnosis after a native/OFF UI boundary. It does NOT hide,
// activate cloak, cycle a device, clear inherited entries, or claim ownership.
// No resident polling: at most 32 explicit boundary snapshots per process.
static uint64_t g_msiNativeOffFilterAuditEdge=0;
static unsigned g_msiNativeOffFilterAuditReads=0;
static void msiNativeForegroundReadOnlyFilterAuditLocked(const ymcc::msi::NativeForegroundFacts& f) noexcept {
    if(!f.knownBoard || !f.nativeChoice || f.virtualEnabled || !domainLogEnabled("virtual") ||
        f.edge==0 || f.edge==g_msiNativeOffFilterAuditEdge || g_msiNativeOffFilterAuditReads>=32)return;
    g_msiNativeOffFilterAuditEdge=f.edge;++g_msiNativeOffFilterAuditReads;
    try {
        HidHideVisibilitySnapshot snapshot;
        const bool read=msiNativeForegroundSnapshot(snapshot) && snapshot.valid;
        json hidden=json::array();
        if(read)for(const auto& path:snapshot.hidden)hidden.push_back(W2U(path));
        const auto& lease=g_msiNativeForegroundLease;
        const json evidence = {
            {"readOnly",true},{"uiOwned",f.uiOwned},{"edge",f.edge},{"powerGeneration",f.currentPower},
            {"snapshotRead",read},{"cloakOn",read?json(snapshot.cloaking):json(nullptr)},
            {"inverseState",read?json(hidHideInverseApplicationListState(snapshot.inverseRaw)):json(nullptr)},
            {"ymccReaderExempt",read?json(hidHideContainsPath(snapshot.applications,W2U(hidHideCurrentModulePath()))):json(nullptr)},
            {"hiddenPaths",hidden},{"inheritedOwnership","unknown-do-not-adopt-or-clear"},
            {"nativeLeaseActive",lease.active},{"nativeOwnedPathCount",lease.owned.size()},
            {"borrowEnabled",ymcc::msi::kNativeForegroundHidHideBorrowEnabled},{"filterWrites",false},{"deviceRefresh",false},
            {"scope","configuration-only;not-current-Steam-handle-or-input-delivery"}};
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
        if(g_msiNativeForegroundTestIo.readOnlyAuditLog){g_msiNativeForegroundTestIo.readOnlyAuditLog(evidence);return;}
#endif
        appendDomainLog("virtual",nativeForegroundProfile()==ymcc::nativeForeground::Profile::Msi?"msi-native-off-filter-state":"native-off-filter-state",evidence);
    } catch (...) { /* diagnostics must not change return/cleanup decisions */ }
}
static void msiNativeForegroundRefreshGameSourceLocked() {
    auto& lease=g_msiNativeForegroundLease;
    if(lease.pendingGameSources.empty() || g_msiNativeForegroundCyclePending)return;
    const auto observed=lease.pendingGameSources.front();
    lease.pendingGameSources.erase(lease.pendingGameSources.begin());
    lease.gameSourceCandidate=observed.first.deviceInstanceId;lease.gameSourceCacheGeneration=observed.second;
    if(!msiNativeForegroundLeaseCurrent() || lease.configurationReturned || lease.restorePending)return;
    std::vector<HidHidePhysicalIdentity> discovered;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.candidates)discovered=g_msiNativeForegroundTestIo.candidates();else
#endif
    hidHideResolvePhysicalIdentities(discovered);
    std::vector<std::string> nativeRoots;bool mixed=false;HidHidePhysicalIdentity source;
    for(const auto& candidate:discovered){
        const bool physical=candidate.resolved && !hidHideIsVirtualIdentity(candidate.deviceInstanceId,candidate.baseContainerDeviceInstanceId);
        if(lease.profile==ymcc::nativeForeground::Profile::Msi && physical && candidate.vendorId==0x0db0 && candidate.productId==0x1902 &&
            candidate.usagePage==1 && (candidate.usage==4 || candidate.usage==5))mixed=true;
        if(physical && nativeForegroundGameInputAdmitted(true,false,candidate.vendorId,candidate.productId,candidate.usagePage,candidate.usage)){
            nativeRoots.push_back(candidate.baseContainerDeviceInstanceId);source=candidate;
        }
    }
    if(mixed || !ymcc::nativeForeground::uniqueSourceRoot(lease.profile,nativeRoots) || ascii_lower(source.baseContainerDeviceInstanceId)!=ascii_lower(lease.root)){
        msiNativeForegroundReceipt("game-source-deferred","current-source-not-unique");return;
    }
    bool complete=false;std::vector<HidHideInterfaceRecord> inventory;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.inventory){
        inventory=g_msiNativeForegroundTestIo.inventory(source);
        complete=!g_msiNativeForegroundTestIo.inventoryComplete || g_msiNativeForegroundTestIo.inventoryComplete();
    }else
#endif
    inventory=hidHideBuildSourceInterfaceInventory(lease.root,source.deviceInstanceId,&complete);
    bool admitted=false;
    for(const auto& candidate:inventory)if(ascii_lower(candidate.instanceId)==ascii_lower(observed.first.deviceInstanceId) &&
        nativeForegroundGameInputAdmitted(ascii_lower(candidate.baseContainerDeviceInstanceId)==ascii_lower(lease.root),
            hidHideIsVirtualIdentity(candidate.instanceId,candidate.baseContainerDeviceInstanceId),
            candidate.vendorId,candidate.productId,candidate.usagePage,candidate.usage))admitted=true;
    if(!complete || !admitted || lease.diagnosticSelectedPaths.size()>=16 || !ymcc::msi::samePhysicalLocation(lease.locations,msiForegroundIsolationLocations(lease.root))){
        msiNativeForegroundReceipt("game-source-deferred","current-game-interface-unverified");return;
    }
    HidHideVisibilitySnapshot before;
    if(!msiNativeForegroundSnapshot(before) || !before.valid || !before.cloaking ||
        hidHideInverseApplicationListState(before.inverseRaw)!="off" ||
        !hidHideContainsPath(before.applications,W2U(hidHideCurrentModulePath()))){
        msiNativeForegroundReceipt("game-source-deferred","current-visibility-contract-unverified");return;
    }
    const auto& target=observed.first.deviceInstanceId;
    if(hidHideContainsPath(lease.diagnosticSelectedPaths,target))return;
    if(!msiNativeForegroundLeaseCurrent())return;
    lease.diagnosticSelectedPaths.push_back(U2W(target));
    if(hidHideContainsPath(before.hidden,target)){
        msiNativeForegroundReceipt("game-source-covered","inherited-current-game-interface");return;
    }
    if(!msiNativeForegroundDurableHide(observed.first,{target},before)){
        msiNativeForegroundReceipt("game-source-deferred","durable-native-hide-not-admitted");return;
    }
    if(!msiNativeForegroundLeaseCurrent()){
        if(lease.nativeVisibilitySession)(void)msiNativeForegroundRestoreLocked("game-source-owner-retired-after-reservation");
        return;
    }
    if(!msiNativeForegroundBeforeHideDispatch(target)){
        (void)msiNativeForegroundRestoreLocked("durable-game-source-issue-not-admitted");return;
    }
    lease.owned.push_back(U2W(target));
    bool hidden=hardwareWriteAllowed() && lease.suspendSerial==g_sharedPowerSuspendSerial.load(std::memory_order_acquire) &&
        msiNativeForegroundVisibility(U2W(target),true);
    HidHideVisibilitySnapshot after;
    hidden=hidden && msiNativeForegroundSnapshot(after) && after.valid && after.cloaking &&
        hidHideInverseApplicationListState(after.inverseRaw)=="off" &&
        hidHideContainsPath(after.applications,W2U(hidHideCurrentModulePath())) && hidHideContainsPath(after.hidden,target);
    if(!hidden || !msiNativeForegroundLeaseCurrent()){
        (void)msiNativeForegroundRestoreLocked("game-source-hide-failed-or-owner-changed");return;
    }
    lease.diagnosticHiddenConfiguration=msiNativeForegroundConfigurationEvidence(after,
        lease.diagnosticSelectedPaths,lease.diagnosticSelectedPathsComplete,lease.owned);
    const int refresh=lease.nativeRootPortCycle?nativeForegroundRefreshLocked(false):
        msiNativeForegroundCycleLocked(false,&observed.first);
    if(refresh==2 || refresh==MsiNativeForegroundLease::kPowerDeferred){(void)msiNativeForegroundRestoreLocked("game-source-refresh-failed");return;}
    msiNativeForegroundReceipt(refresh==0?"game-source-refresh-accepted":"game-source-refresh-wait","current-same-root-game-interface");
    if(refresh==0 && !lease.pendingGameSources.empty())msiNativeForegroundQueue();
}
static void msiNativeForegroundHandleWork() {
    if(msiColdReturnMaintainOwner("cold-native-work"))return;
    if(msiPostHideMaintainOwner("post-hide-maintenance"))return;
    if(!nativeForegroundFamilyAllowed())return;
    std::lock_guard hostLock(g_inputHostMx);
    std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
    auto& s=g_msiNativeForegroundLease;
    if(s.coldReturnCycle)return; // only its original OFF consumer may progress it
    const auto suspendSerial=g_sharedPowerSuspendSerial.load(std::memory_order_acquire);
    auto facts=msiNativeForegroundFacts();
    // VC-GPD-LIVE-HOST-NOT-COLD-20261111. Virtual/UI observations must not
    // manufacture native return duty. Do not use hostAbsent/MayEnter here:
    // it includes the very historical duty this cold observer must settle.
    if(!s.active && facts.nativeChoice && !facts.virtualEnabled && facts.uiOwned &&
        facts.powerReady && !facts.exitFence){
        if(!inputHostNativeVisibilityColdReturnOrQueueLocked("native-ui-before-hide"))return;
        facts=msiNativeForegroundFacts(); // cold return may have settled history
    }
    if(!s.postHideCycle && facts.nativeChoice && !facts.virtualEnabled && facts.uiOwned &&
        facts.powerReady && !facts.exitFence && msiNativeForegroundVirtualWorkHasPriority()){
        auto& retry=g_msiNativeForegroundRetry;
        retry.virtualYieldPower=facts.currentPower;retry.virtualYieldEdge=facts.currentEdge;
        retry.virtualYieldRequest=g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId;
        retry.virtualYieldSuspendSerial=suspendSerial;
        retry.virtualYieldProfile=nativeForegroundProfile();
        g_msiNativeForegroundVirtualYieldPending.store(true,std::memory_order_release);
    }
    if(s.postHideCycle){
        if(g_msiNativeForegroundCyclePending)(void)msiNativeForegroundPollCycleLocked();
        if(!msiPostHideContextCurrentLocked())(void)msiPostHideCleanupLocked("post-hide-context-ended");
        return;
    }
    msiNativeForegroundReadOnlyFilterAuditLocked(facts);
    if(s.active){
        if(msiNativeForegroundHoldForVirtualLocked("native-owner-awaits-original-virtual-request")){
            if(!g_msiNativeForegroundCyclePending){
                // The same helper has ended. Resume each parked original work
                // item once while leaving its hide with the donor until apply.
                auto deferred=std::move(s.deferredVirtualWork);
                s.deferredVirtualWork.clear();
                for(const auto& reason:deferred){
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
                    if(g_msiNativeForegroundTestIo.virtualQueue){g_msiNativeForegroundTestIo.virtualQueue(reason);continue;}
#endif
                    (void)inputHostQueueLifecycleWork(reason.c_str());
                }
            }
            return;
        }
        const bool suspendPending=g_sleepPowerDispatch.pendingSuspend();
        const bool leaseCurrent=!s.restorePending && msiNativeForegroundLeaseCurrent();
        if(!leaseCurrent && (suspendPending || g_sleepPowerDispatch.pendingSuspend()) &&
            msiNativeForegroundWin5SettledLeaseMayWaitForSuspendAdmission()){
            if(!s.pendingGameSources.empty())g_msiNativeForegroundWorkPowerDeferred.store(true,std::memory_order_release);
            return;
        }
        if(s.restorePending || !leaseCurrent || (s.cycleIssued && !s.cycleAccepted && !g_msiNativeForegroundCyclePending)){
            const bool restored=msiNativeForegroundRestoreLocked("foreground-or-config-transition");
            if(!restored && !s.deferredVirtualWork.empty())(void)msiNativeForegroundVirtualHandoffLocked();
            if(!s.active && ymcc::msi::nativeForegroundMayEnter(msiNativeForegroundFacts()) &&
                s.attemptedEdge!=g_msiNativeForegroundEdge.load(std::memory_order_acquire))msiNativeForegroundQueue();
        }else{
            if(s.refreshSeriesActive){
                const int refresh=nativeForegroundRefreshLocked(false);
                if(refresh==2 || refresh==MsiNativeForegroundLease::kPowerDeferred)
                    (void)msiNativeForegroundRestoreLocked("game-interface-series-failed");
                else if(refresh==0){
                    msiNativeForegroundReceipt("config-cycle-accepted","native-foreground-entered");
                    if(!s.pendingGameSources.empty())msiNativeForegroundQueue();
                }
            }else msiNativeForegroundRefreshGameSourceLocked();
        }
        return;
    }
    if(!ymcc::msi::nativeForegroundMayEnter(facts) || s.attemptedEdge==facts.edge)return;
    g_msiNativeForegroundSourceRetryNeeded=false;
    s.attemptedEdge=facts.edge;s.edge=facts.edge;s.power=facts.power;s.profile=nativeForegroundProfile();s.suspendSerial=suspendSerial;
    if(s.profile==ymcc::nativeForeground::Profile::Msi && g_msiNativeForegroundRetry.attempts && g_msiNativeForegroundRetry.edge==facts.edge &&
        g_msiNativeForegroundRetry.power==facts.power){
        HidHideVisibilitySnapshot control;
        const bool repair=!msiNativeForegroundSnapshot(control) || !control.valid ||
            !hidHideContainsPath(control.applications,W2U(hidHideCurrentModulePath()));
        const auto controlFacts=msiNativeForegroundFacts();
        if(!ymcc::msi::nativeForegroundMayEnter(controlFacts) ||
            controlFacts.currentPower!=facts.power || controlFacts.currentEdge!=facts.edge)return;
        if(repair)
            (void)msiNativeForegroundRepairReaderLocked(facts.power,facts.edge);
        if(!ymcc::msi::nativeForegroundMayEnter(msiNativeForegroundFacts()))return;
    }
    s.xusbTargets.clear();
    std::vector<HidHidePhysicalIdentity> discovered;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.candidates)discovered=g_msiNativeForegroundTestIo.candidates();else
#endif
    hidHideResolvePhysicalIdentities(discovered);
    std::vector<HidHidePhysicalIdentity> candidates;
    bool otherPhysicalMsiGameSource=false;
    for(const auto& source:discovered){
        const bool physical=source.resolved && !hidHideIsVirtualIdentity(source.deviceInstanceId,source.baseContainerDeviceInstanceId);
        if(s.profile==ymcc::nativeForeground::Profile::Msi && physical && source.vendorId==0x0db0 && source.productId==0x1902 && source.usagePage==1 &&
            (source.usage==4 || source.usage==5))otherPhysicalMsiGameSource=true;
        if(source.resolved && nativeForegroundGameInputAdmitted(true,!physical,
            source.vendorId,source.productId,source.usagePage,source.usage))candidates.push_back(source);
    }
    // A unique 1901 alone is insufficient if a CURRENT 1902 gaming source is
    // also present (transient two-mode topology or another MSI controller).
    // Do not assume which one is this machine's integrated controller.
    std::vector<std::string> candidateRoots;
    for(const auto& candidate:candidates)candidateRoots.push_back(candidate.baseContainerDeviceInstanceId);
    facts.currentUniqueSource=ymcc::nativeForeground::uniqueSourceRoot(s.profile,candidateRoots) && !otherPhysicalMsiGameSource;
    if(!facts.currentUniqueSource){g_msiNativeForegroundSourceRetryNeeded=true;msiNativeForegroundReceipt("deferred",otherPhysicalMsiGameSource?
        "mixed-msi-native-and-dinput-sources":"native-source-not-unique");return;}
    const auto source=candidates.front();s.root=source.baseContainerDeviceInstanceId;
    if(!nativeForegroundRootCandidate(s.root)){
        g_msiNativeForegroundSourceRetryNeeded=true;msiNativeForegroundReceipt("deferred","native-source-usb-root-unverified");return;
    }
    s.nativeRootPortCycle=ymcc::nativeForeground::nativeRootPortCycleAllowed(s.profile,s.root);
    s.locations=msiForegroundIsolationLocations(s.root);
    if(s.locations.empty()){g_msiNativeForegroundSourceRetryNeeded=true;msiNativeForegroundReceipt("deferred","physical-location-unresolved");return;}
    bool inventoryComplete=false;std::vector<HidHideInterfaceRecord> inventory;
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
    if(g_msiNativeForegroundTestIo.inventory){
        inventory=g_msiNativeForegroundTestIo.inventory(source);
        inventoryComplete=!g_msiNativeForegroundTestIo.inventoryComplete || g_msiNativeForegroundTestIo.inventoryComplete();
    }else
#endif
    inventory=hidHideBuildSourceInterfaceInventory(s.root,source.deviceInstanceId,&inventoryComplete);
    if(s.profile==ymcc::nativeForeground::Profile::Rog && !inventoryComplete){g_msiNativeForegroundSourceRetryNeeded=true;msiNativeForegroundReceipt("deferred","game-interface-inventory-incomplete");return;}
    auto targets=s.profile==ymcc::nativeForeground::Profile::Msi?hidHidePhysicalTargets(source):std::vector<std::string>{source.deviceInstanceId};bool mainCurrent=false;
    for(const auto& item:inventory)if(nativeForegroundGameInputAdmitted(
        ascii_lower(item.baseContainerDeviceInstanceId)==ascii_lower(s.root),
        hidHideIsVirtualIdentity(item.instanceId,item.baseContainerDeviceInstanceId),item.vendorId,item.productId,item.usagePage,item.usage)){
        if(ascii_lower(item.instanceId)==ascii_lower(source.deviceInstanceId))mainCurrent=true;
        hidHideAppendTarget(targets,item.instanceId);
    }
    const auto xusb=msiNativeForegroundXusbInventory(source);
    if(!xusb.complete){msiNativeForegroundReceipt("deferred",xusb.reason.c_str());return;}
    s.xusbTargets.clear();
    for(const auto& item:xusb.interfaces)if(nativeForegroundCurrentXusbTargetAdmitted(s.root,item)){
        hidHideAppendTarget(targets,item.instanceId);
        hidHideAppendTarget(s.xusbTargets,item.instanceId);
    }
    // The ROG root-port route never disables a HID leaf. Keep the leaf
    // quarantine for every route that actually requests HID-node refresh.
    for(const auto& target:targets)if(!s.nativeRootPortCycle && !ymcc::nativeForeground::gameNodeRefreshMutationAllowed(s.profile,s.root,target)){
        g_msiNativeForegroundSourceRetryNeeded=false;
        msiNativeForegroundReceipt("deferred","rog-hid-game-node-refresh-unvalidated");return;
    }
    if(s.profile!=ymcc::nativeForeground::Profile::Msi && s.xusbTargets.empty()){
        g_msiNativeForegroundSourceRetryNeeded=true;msiNativeForegroundReceipt("deferred","current-xusb-game-interface-unverified");return;
    }

    if(!ymcc::msi::samePhysicalLocation(s.locations,msiForegroundIsolationLocations(s.root))){
        g_msiNativeForegroundSourceRetryNeeded=true;
        msiNativeForegroundReceipt("deferred","xusb-source-location-changed");return;
    }
    facts.gameInput=mainCurrent && targets.size()<=16;
    facts.virtualSource=hidHideIsVirtualIdentity(source.deviceInstanceId,s.root);
    if(!msiNativeForegroundSnapshot(s.before)){msiNativeForegroundReceipt("deferred","visibility-query-failed");return;}
    try {
        s.diagnosticSelectedPaths.clear();
        for(const auto& target:targets)s.diagnosticSelectedPaths.push_back(U2W(target));
        s.diagnosticSelectedPathsComplete=facts.gameInput&&!facts.virtualSource;
        s.diagnosticBeforeConfiguration=msiNativeForegroundConfigurationEvidence(s.before,
            s.diagnosticSelectedPaths,s.diagnosticSelectedPathsComplete,{});
    } catch(...) {
        s.diagnosticSelectedPaths.clear();s.diagnosticSelectedPathsComplete=false;
        s.diagnosticBeforeConfiguration=nullptr;
    }
    facts.readerExempt=hidHideContainsPath(s.before.applications,W2U(hidHideCurrentModulePath()));
    facts.inverseOff=hidHideInverseApplicationListState(s.before.inverseRaw)=="off";
    const auto now=msiNativeForegroundFacts();facts.currentEdge=now.currentEdge;facts.currentPower=now.currentPower;
    facts.uiOwned=now.uiOwned;facts.powerReady=now.powerReady;facts.nativeChoice=now.nativeChoice;
    facts.virtualEnabled=now.virtualEnabled;facts.hostAbsent=now.hostAbsent;facts.exitFence=now.exitFence;facts.borrowBusy=now.borrowBusy;
    if(!ymcc::msi::nativeForegroundMayHide(facts) || s.suspendSerial!=g_sharedPowerSuspendSerial.load(std::memory_order_acquire)){
        // A missing current gaming interface can be retried on source return,
        // but broken/unknown HidHide ownership is NOT a source-retry signal.
        g_msiNativeForegroundSourceRetryNeeded=!mainCurrent && targets.size()<=16 &&
            ymcc::msi::nativeForegroundMayEnter(facts) && facts.readerExempt && facts.inverseOff;
        msiNativeForegroundReceipt("deferred","native-visibility-contract-not-admitted");return;
    }
    try{msiNativeForegroundReceipt("before-hide","native-input-before-visibility-write");}catch(...){}
    if(!msiNativeForegroundDurableHide(source,targets,s.before)){
        msiNativeForegroundReceipt("deferred","durable-native-hide-not-admitted");return;
    }
    if(!msiNativeForegroundLeaseCurrent()){
        if(s.nativeVisibilitySession)(void)msiNativeForegroundRestoreLocked("native-owner-retired-after-reservation");
        return;
    }
    s.active=true;
    if(!s.before.cloaking){
        s.cloakChanged=true;
        if(!hardwareWriteAllowed() || s.suspendSerial!=g_sharedPowerSuspendSerial.load(std::memory_order_acquire) ||
            !msiNativeForegroundCloak(true)){
            (void)msiNativeForegroundRestoreLocked("cloak-enable-failed");return;
        }
    }
    bool ok=true;
    for(const auto& target:targets){
        if(!msiNativeForegroundLeaseCurrent()){ok=false;break;}
        if(hidHideContainsPath(s.before.hidden,target))continue;
        if(!msiNativeForegroundBeforeHideDispatch(target)){ok=false;break;}
        const auto wide=U2W(target);s.owned.push_back(wide);
        if(!hardwareWriteAllowed() || s.suspendSerial!=g_sharedPowerSuspendSerial.load(std::memory_order_acquire) ||
            !msiNativeForegroundVisibility(wide,true)){ok=false;break;}
    }
    HidHideVisibilitySnapshot after;
    if(ok)ok=msiNativeForegroundSnapshot(after) && after.cloaking &&
        hidHideInverseApplicationListState(after.inverseRaw)=="off" &&
        hidHideContainsPath(after.applications,W2U(hidHideCurrentModulePath()));
    if(ok)for(const auto& target:targets)ok=hidHideContainsPath(after.hidden,target) && ok;
    if(!ok || !msiNativeForegroundLeaseCurrent()){(void)msiNativeForegroundRestoreLocked("hide-failed-or-owner-changed");return;}
    s.diagnosticHiddenConfiguration=msiNativeForegroundConfigurationEvidence(after,
        s.diagnosticSelectedPaths,s.diagnosticSelectedPathsComplete,s.owned);
    if(s.profile==ymcc::nativeForeground::Profile::Gpd &&
        g_machineIdentity.deviceClass=="HandheldCompanion.Devices.GPDWin5") {
        try {
            auto permit=std::make_shared<ymcc::isolationhid::Permit>();
            permit->power=s.power;permit->edge=s.edge;permit->suspendSerial=s.suspendSerial;
            permit->root=U2W(s.root);permit->preferredPath=U2W(source.sourcePath);
            g_controllerIsolationHidPermit.store(std::move(permit),std::memory_order_release);
        } catch(...) {
            (void)msiNativeForegroundRestoreLocked("native-hid-reader-permit-unavailable");return;
        }
    }
    // Existing consumers can keep reading through pre-hide handles. The ROG
    // route now uses the exact-root USB port cycle instead of a HID-leaf
    // disable/enable. Helper completion remains separate from input proof.
    // Refresh is necessary for consumers with handles opened before this hide.
    // It requests the SAME physical root, not a blanket restart or mode toggle.
    const int cycle=nativeForegroundRefreshLocked(false);
    if(cycle==2 || cycle==MsiNativeForegroundLease::kPowerDeferred){(void)msiNativeForegroundRestoreLocked("hide-refresh-failed");return;}
    g_msiNativeForegroundWatchGameSources=true;
    msiNativeForegroundReceipt(cycle==0?"config-cycle-accepted":"hide-wait",cycle==0?"native-foreground-entered":"port-refresh-pending");
}
// A late same-root game leaf can replace an interface still owned by native
// isolation. Return the whole original set rather than starting a Host that
// cannot durably accept it. This observation never admits a virtual hide.
static bool msiNativeForegroundUntransferableGpdHandoffLocked(const std::string& reason) noexcept {
    try {
        const auto& s=g_msiNativeForegroundLease;
        if(!s.virtualHandoffHeld || g_inputHostProcess || g_hidHideVisibilityJournal.active ||
            !msiNativeForegroundVirtualHandoffContextLocked() ||
            !msiNativeForegroundGpdTerminalGameWriterReturnedLocked() ||
            s.gameSourceCandidate.empty() || !hidHideContainsPath(s.owned,s.gameSourceCandidate))return false;
        ymcc::inputHost::queuedPlayerStart::Work work;
        if(!ymcc::inputHost::queuedPlayerStart::decode(reason,work) || !work.bound ||
            work.request!=s.virtualHandoffRequest || ymcc::inputHost::queuedPlayerStart::stale(work))return false;
        const auto topology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
        const auto identity=g_hidResolveCacheGeneration.load(std::memory_order_acquire);
        const auto source=g_msiMainSourceRevision.load(std::memory_order_acquire);
        const auto proven=[&](const std::string& path){
#ifdef YMCC_MSI_FOREGROUND_ISOLATION_TEST
            if(!g_msiNativeForegroundTestIo.handoffPathProven)return false;
            return g_msiNativeForegroundTestIo.handoffPathProven(s.root,path);
#else
            return ymcc::targetReturn::physicalPath::verify(s.root,path).proven();
#endif
        };
        if(!proven(s.gameSourceCandidate))return false;
        for(const auto& path:s.owned){
            const auto target=W2U(path);
            if(ascii_lower(target)==ascii_lower(s.gameSourceCandidate) || proven(target))continue;
            return topology==g_xinputTopologyEpoch.load(std::memory_order_acquire) &&
                identity==g_hidResolveCacheGeneration.load(std::memory_order_acquire) &&
                source==g_msiMainSourceRevision.load(std::memory_order_acquire) &&
                msiNativeForegroundVirtualHandoffContextLocked() && !ymcc::inputHost::queuedPlayerStart::stale(work);
        }
        return false;
    }catch(...){return false;}
}
static bool msiNativeForegroundBeforeVirtualWork(const std::string& reason) {
    // Retire a stale private cleanup before it can restore another family lease.
    if(msiPostHideCleanupIsWork(reason)){
        std::lock_guard hostLock(g_inputHostMx);
        if(!msiPostHideCleanupWorkCurrentLocked(reason)){
            msiPostHideCleanupWorkRetiredLocked(reason,"before-lifecycle-work");return false;
        }
    }
    if(!nativeForegroundFamilyAllowed() && !g_postHideCycleOwned.load(std::memory_order_acquire))return true;
    std::lock_guard hostLock(g_inputHostMx);
    // Delayed partial-Start cleanup owns its captured Host tuple. Reject it
    // under this same lock before native lease, hide or helper side effects.
    if(!ymcc::inputHost::partialStartCleanup::current(reason,g_inputHostEpoch,g_inputHostRunId)){
        try{appendNativeLifecycleLog("input-host-lifecycle-work",{{"action","retired-cleanup"},
            {"reason","partial-start-owner-retired-or-malformed"},{"stage","before-native-foreground-work"},
            {"queuedReason",reason.substr(0,256)},{"currentHostEpoch",g_inputHostEpoch},
            {"nativeLeaseTouched",false},{"targetCommandsSent",false}});}catch(...){}
        return false;
    }
    std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
    if(g_msiNativeForegroundSourceRetryNeeded && inputHostConfiguredPersona()!="disabled")
        g_msiNativeForegroundSourceRetryNeeded=false; // virtual route clears native-only retry work
    if(g_msiNativeForegroundLease.coldReturnCycle){
        const bool fault=reason=="frame-receipt-fault" || reason.rfind("fault-stop:",0)==0;
        if(!fault && msiColdReturnContextCurrentLocked())return false;
        return msiColdReturnCleanupLocked("cold-before-new-work");
    }
    if(!g_msiNativeForegroundLease.active){
        // Normal Stop must remain reachable while the current Host/journal owns
        // the device. Cold responsibility is consumed only after that return.
        if(g_inputHostProcess || g_hidHideVisibilityJournal.active)return true;
        // Exact retained return work owns this endpoint debt, not cold history.
        if(reason=="quarantine-endpoint-absent" &&
                (g_inputHostReleaseQuarantined || g_msiSteamDeckReleaseDebt.required()))return true;
        return inputHostNativeVisibilityColdReturnOrQueueLocked("before-virtual-work");
    }
    const bool mandatoryFault=reason=="frame-receipt-fault" || reason.rfind("fault-stop:",0)==0;
    if(!g_realStickTest.virtualTargetEnabled && inputHostConfiguredPersona()=="disabled" &&
        !g_msiNativeForegroundLease.postHideCycle && !mandatoryFault && !msiNativeForegroundVirtualWorkHasPriority())return true;
    if(g_msiNativeForegroundLease.postHideCycle){
        if(msiPostHideContextCurrentLocked())return false; // original Start is the sole waiter
        if(msiPostHideCleanupLocked("post-hide-before-new-work"))return true;
        auto& pending=g_msiNativeForegroundLease.deferredVirtualWork;
        if(std::find(pending.begin(),pending.end(),reason)==pending.end())pending.push_back(reason);
        return false;
    }
    auto& lease=g_msiNativeForegroundLease;
    if(mandatoryFault && lease.profile!=ymcc::nativeForeground::Profile::Gpd){
        auto& pending=lease.deferredVirtualWork;
        pending.erase(std::remove_if(pending.begin(),pending.end(),[](const std::string& queued){
            ymcc::inputHost::queuedPlayerStart::Work work;
            return ymcc::inputHost::queuedPlayerStart::decode(queued,work) &&
                ymcc::inputHost::queuedPlayerStart::startReason(work.bound?work.reason:queued);
        }),pending.end());
    }
    if(mandatoryFault && lease.profile==ymcc::nativeForeground::Profile::Gpd)lease.virtualContinuationEnded=true;
    const bool cancelHandoff=mandatoryFault && lease.profile==ymcc::nativeForeground::Profile::Gpd &&
        (lease.virtualHandoffHeld || msiNativeForegroundVirtualHandoffContextLocked());
    if(cancelHandoff){
        lease.virtualHandoffHeld=false;lease.virtualHandoffRequest=0;
        lease.deferredVirtualWork.clear();
    }
    const bool preserved=!cancelHandoff && msiNativeForegroundHoldForVirtualLocked("virtual-request-awaits-native-helper");
    bool returnBeforeStart=false;
    if(preserved && !g_msiNativeForegroundCyclePending){
        returnBeforeStart=msiNativeForegroundUntransferableGpdHandoffLocked(reason);
        if(!returnBeforeStart)return true;
        lease.virtualContinuationEnded=true; // no transfer or inverse-refresh shortcut for this donor
    }
    if((!preserved || returnBeforeStart) &&
        (msiNativeForegroundRestoreLocked(returnBeforeStart?"native-handoff-ancestry-unproven-before-host":"handoff-to-virtual-route") ||
         msiNativeForegroundVirtualHandoffLocked()))return true;
    auto& deferred=g_msiNativeForegroundLease.deferredVirtualWork;
    if(std::find(deferred.begin(),deferred.end(),reason)==deferred.end())deferred.push_back(reason);
    msiNativeForegroundReceipt("virtual-work-deferred","same-native-ownership-still-unresolved");
    return false;
}
static void msiNativeForegroundMaintenance() {
    if(msiColdReturnMaintainOwner("cold-period-ended"))return;
    if(msiPostHideMaintainOwner("post-hide-period-ended"))return;
    if(!nativeForegroundFamilyAllowed())return;
    if(g_msiNativeForegroundVirtualYieldPending.load(std::memory_order_acquire)){
        std::unique_lock<InputHostMutex> hostLock(g_inputHostMx,std::try_to_lock);
        if(hostLock.owns_lock()){
            std::unique_lock leaseLock(g_msiNativeForegroundMx,std::try_to_lock);
            if(leaseLock.owns_lock()){
                auto& retry=g_msiNativeForegroundRetry;
                const auto facts=msiNativeForegroundFacts();
                const auto view=g_virtualStartProtection.view(inputHostStartProtectionNow());
                const bool retired=retry.virtualYieldProfile!=nativeForegroundProfile() ||
                    retry.virtualYieldPower!=facts.currentPower || retry.virtualYieldEdge!=facts.currentEdge ||
                    retry.virtualYieldSuspendSerial!=g_sharedPowerSuspendSerial.load(std::memory_order_acquire) ||
                    retry.virtualYieldRequest!=view.requestId || !facts.nativeChoice || facts.virtualEnabled ||
                    !facts.uiOwned || facts.exitFence ||
                    !g_msiNativeForegroundDesiredUi.load(std::memory_order_acquire) ||
                    g_exitRequested.load(std::memory_order_acquire);
                const bool ready=!retired && ymcc::msi::nativeForegroundMayEnter(facts);
                if(retired || ready){
                    retry.virtualYieldPower=retry.virtualYieldEdge=retry.virtualYieldRequest=retry.virtualYieldSuspendSerial=0;
                    retry.virtualYieldProfile=ymcc::nativeForeground::Profile::Unsupported;
                    g_msiNativeForegroundVirtualYieldPending.store(false,std::memory_order_release);
                    if(ready){
                        if(!g_msiNativeForegroundLease.active)g_msiNativeForegroundLease.attemptedEdge=0;
                        msiNativeForegroundQueue();
                    }
                }
            }
        }
    }
    (void)msiNativeForegroundTryReleasePowerHold();
    if(g_msiNativeForegroundWorkPowerDeferred.load(std::memory_order_acquire) &&
        hardwareWriteAllowed() && g_inputReady.load(std::memory_order_acquire) &&
        g_msiNativeForegroundWorkPowerDeferred.exchange(false,std::memory_order_acq_rel))msiNativeForegroundQueue();
    {
        std::unique_lock<std::mutex> lock(g_msiNativeForegroundMx,std::try_to_lock);
        if(lock.owns_lock() && nativeForegroundProfile()==ymcc::nativeForeground::Profile::Msi){
            auto& retry=g_msiNativeForegroundRetry;
            const auto now=msiNativeForegroundRetryTick();
            if(retry.due && now>=retry.due && !inputHostExitFenceRaised() &&
                g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready &&
                !g_msiNativeForegroundPowerHold.load(std::memory_order_acquire)){
                retry.due=0;
                if(retry.power==currentPowerGeneration() && retry.edge==g_msiNativeForegroundEdge.load(std::memory_order_acquire) &&
                    now>=retry.started && now-retry.started<30000 && retry.attempts<5){
                    ++retry.attempts;
                    if(!g_msiNativeForegroundLease.active)g_msiNativeForegroundLease.attemptedEdge=0;
                    msiNativeForegroundQueue();
                }else msiNativeForegroundReceipt("retry-exhausted","original-context-ended-or-budget-exhausted");
            }
        }
    }
    if(!g_msiNativeForegroundCyclePending.load(std::memory_order_acquire))return;
    std::lock_guard<std::mutex> lock(g_msiNativeForegroundMx);
    if(msiNativeForegroundPollCycleLocked()>=0)msiNativeForegroundQueue();
}
static void msiNativeForegroundExitCleanup() {
    if(msiColdReturnMaintainOwner("cold-exit",true))return;
    if(msiPostHideMaintainOwner("post-hide-exit",true))return;
    if(!nativeForegroundFamilyAllowed())return;
    std::lock_guard hostLock(g_inputHostMx);
    std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
    if(g_msiNativeForegroundCyclePending && g_msiNativeForegroundLease.cycleProcess){
        (void)WaitForSingleObject(g_msiNativeForegroundLease.cycleProcess,5000);
        (void)msiNativeForegroundPollCycleLocked();
    }
    g_msiNativeForegroundLease.deferredVirtualWork.clear(); // exit never replays target commands
    (void)msiNativeForegroundRestoreLocked("app-exit");
    if(g_msiNativeForegroundLease.active)msiNativeForegroundReceipt("exit-unverified","config-and-helper-evidence-retained");
}
