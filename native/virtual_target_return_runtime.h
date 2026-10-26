#pragma once
#include "virtual_target_return_session.h"
#include "virtual_target_return_process.h"
#include "virtual_target_return_physical_path.h"

// Historical target duties only, on the existing g_inputHostMx owner. This is
// not a second execution cycle. Disk pins never enter State.accept/admission.
namespace ymcc::inputHost::targetReturnRuntime {
struct Target { ymcc::targetReturn::Origin origin; std::uint64_t sequence=0; };
struct Physical { ymcc::targetReturn::Origin origin; std::string journalId; };
struct State {
    std::unique_ptr<ymcc::targetReturn::Session> session;
    std::wstring path;
    std::vector<Target> targets;
    std::vector<Physical> physicals;
    std::vector<ymcc::targetReturn::Origin> proven;
    std::uint64_t serial=0;
    std::string lastError;
    bool coldHistoryPending=false;
};
static State state;
}

// Explicit environment leaves: production uses the title-independent product
// data root. Offline consumers bind these leaves to an owned private directory.
static std::wstring inputHostTargetReturnRecordPath() {
    const auto dir=fspath::path(yemancc_data_dir())/L"controller-return";
    std::error_code error;fspath::create_directories(dir,error);
    return (dir/L"target-return.json").wstring();
}
static std::string inputHostTargetReturnMachineBinding() {
    auto guid=sgRegSZ(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Cryptography",L"MachineGuid");
    while(!guid.empty()&&guid.back()=='\0')guid.pop_back();
    if(!guid.empty())return "windows-machine-guid:"+guid;
    wchar_t computer[MAX_COMPUTERNAME_LENGTH+1]{};DWORD length=MAX_COMPUTERNAME_LENGTH+1;
    if(GetComputerNameW(computer,&length)&&length)
        return "weak-computer-name:"+W2U(std::wstring(computer,length));
    // Missing optional model metadata never disables output. A future cold
    // consumer must keep this weak binding untrusted for cleanup authority.
    return "weak-local-user-store";
}
static std::optional<ymcc::targetReturn::ProcessIdentity> inputHostTargetReturnCaptureOwner(HANDLE process) {
    return ymcc::targetReturn::process::capture(process);
}

namespace ymcc::inputHost::targetReturnRuntime {
namespace duty=ymcc::targetReturn;
static void note(const char* phase,const char* reason,const duty::Origin* origin=nullptr,const std::string& path={}) noexcept {
    try {const auto* record=state.session?state.session->record():nullptr;
        appendNativeLifecycleLog("input-host-target-return-record",{
            {"phase",phase},{"reason",reason},{"recordPath",W2U(state.path)},
            {"ready",record!=nullptr},{"revision",record?json(record->revision):json(nullptr)},
            {"origins",record?record->obligations.size():0},
            {"originKey",origin?origin->transactionId:std::string{}},
            {"physicalRoot",origin?origin->physicalRoot:std::string{}},{"pathInstanceId",path},
            {"runId",origin?origin->runId:g_inputHostRunId},
            {"epoch",origin?origin->epoch:g_inputHostEpoch},
            {"historyOnly",true},{"currentAdmission",false},{"gameInputVerified",false},
            {"scope","target-and-owned-physical-write-ahead;current-owner-settlement;R3-bounded-cold-return"}});
    }catch(...){}
}
static bool contextCurrent() {
    if(inputHostExitFenceRaised())return false;
    const auto now=inputHostStartProtectionNow();
    if(g_inputHostSwitchRecoveryStartDeadline&&now>=g_inputHostSwitchRecoveryStartDeadline)return false;
    if(g_inputHostSwitchRecoveryStartRequestId&&
       (g_virtualStartProtection.view(now).requestId!=g_inputHostSwitchRecoveryStartRequestId||
        currentPowerGeneration()!=g_inputHostSwitchRecoveryStartPower))return false;
    return true;
}
static bool sameFlow(const duty::Origin& a,const duty::Origin& b) {
    return duty::detail::sameProcess(a.parent,b.parent)&&duty::detail::sameProcess(a.host,b.host)&&
        a.hostInstanceId==b.hostInstanceId&&a.runId==b.runId&&a.epoch==b.epoch&&
        a.targetId==b.targetId&&a.persona==b.persona&&a.physicalRoot==b.physicalRoot&&
        a.cliWriterProtocol==b.cliWriterProtocol;
}
static std::optional<duty::Origin> currentOwner() {
    if(!g_inputHostProcess||g_inputHostHostInstanceId.empty()||g_inputHostRunId.empty()||!g_inputHostEpoch)return std::nullopt;
    auto parent=inputHostTargetReturnCaptureOwner(GetCurrentProcess());
    auto host=inputHostTargetReturnCaptureOwner(g_inputHostProcess);
    if(!parent||!host)return std::nullopt;
    duty::Origin origin;origin.parent=*parent;origin.host=*host;
    origin.hostInstanceId=g_inputHostHostInstanceId;origin.runId=g_inputHostRunId;
    origin.epoch=g_inputHostEpoch;origin.targetId=kInputHostTargetId;
    origin.persona=inputHostBoundOrCurrentPersona();
    origin.cliWriterProtocol=1; // actual atomic-job/durable-writer producer, never an imported default
    return origin; // physicalRoot deliberately absent for source-free targets.
}
static bool open() {
    if(state.session&&state.session->ready())return true;
    state.coldHistoryPending=true;
    state.path=inputHostTargetReturnRecordPath();
    state.session.reset(new duty::Session(duty::Session::open(state.path,inputHostTargetReturnMachineBinding())));
    if(!state.session->ready()){
        state.lastError=state.session->error().reason;note("load-deferred",state.lastError.c_str());return false;
    }
    state.coldHistoryPending=!state.session->record()->obligations.empty();
    state.lastError.clear();note("loaded",state.session->openedFrom()==duty::LoadState::Missing?"record-missing":"historical-duties-loaded");
    return true;
}
static bool contains(const duty::Origin& origin) {
    const auto* record=state.session?state.session->record():nullptr;if(!record)return false;
    return std::any_of(record->obligations.begin(),record->obligations.end(),[&](const auto& item){return duty::sameOrigin(item.origin,origin);});
}
static bool flushProven(const char* basis="previously-proven-target-lifetime-complete") {
    if(!state.session||!state.session->ready())return false;
    for(auto it=state.proven.begin();it!=state.proven.end();) {
        if(contains(*it)) {
            const auto* record=state.session->record();
            const auto group=std::find_if(record->obligations.begin(),record->obligations.end(),[&](const auto& item){return duty::sameOrigin(item.origin,*it);});
            // This adapter never settles a physical path. Keep any future mixed
            // duty for its exact physical-return consumer instead of erasing it.
            if(group!=record->obligations.end()&&!group->paths.empty())return false;
            if(!state.session->consumeOrigin(record->revision,*it)) {
                state.lastError=state.session->error().reason;note("settlement-deferred",state.lastError.c_str(),&*it);return false;
            }
            note("target-settled",basis,&*it);
        }
        const auto consumed=*it;
        state.targets.erase(std::remove_if(state.targets.begin(),state.targets.end(),[&](const auto& item){return duty::sameOrigin(item.origin,consumed);}),state.targets.end());
        it=state.proven.erase(it);
    }
    return true;
}
static std::string newKey(const duty::Origin& origin) {
    for(std::size_t attempt=0;attempt<=duty::maxOrigins;++attempt) {
        if(state.serial==(std::numeric_limits<std::uint64_t>::max)())return {};
        const auto key="r-"+std::to_string(origin.parent.pid)+"-"+origin.parent.creationTime+"-"+
            std::to_string(origin.host.pid)+"-"+origin.host.creationTime+"-"+std::to_string(++state.serial);
        const auto* record=state.session->record();
        const bool used=std::any_of(record->obligations.begin(),record->obligations.end(),[&](const auto& item){return item.origin.transactionId==key;})||
            std::any_of(state.targets.begin(),state.targets.end(),[&](const auto& item){return item.origin.transactionId==key;});
        if(!used)return key;
    }
    return {};
}
}

// This hook is reached only after pipe connection/admission and immediately
// before a possible PREPARE/RECREATE wire write. Failed persistence emits no
// create command; committed intent survives lost ACK or a later context expiry.
static bool inputHostTargetReturnBeforeCreateLocked(const char* command,std::uint64_t sequence) {
    using namespace ymcc::inputHost::targetReturnRuntime;
    if(!command||(std::strcmp(command,"PREPARE_TARGET")!=0&&std::strcmp(command,"RECREATE_TARGET")!=0))return true;
    try {
        if(!sequence||!contextCurrent())return false;
        auto current=currentOwner();
        if(!current){state.lastError="current-owner-identity-unavailable";note("reservation-rejected",state.lastError.c_str());return false;}
        if(!open()||!flushProven()||!contextCurrent())return false;
        auto entry=std::find_if(state.targets.begin(),state.targets.end(),[&](const auto& item){return item.sequence==sequence&&sameFlow(item.origin,*current);});
        if(entry==state.targets.end()) {
            if(state.targets.size()>=duty::maxOrigins)return false;
            current->transactionId=newKey(*current);if(current->transactionId.empty())return false;
            state.targets.push_back({*current,sequence});entry=std::prev(state.targets.end());
        }
        const auto previous=state.session->record()->revision;
        if(!state.session->reserveTarget(previous,entry->origin,{true,true})){
            state.lastError=state.session->error().reason;note("reservation-rejected",state.lastError.c_str(),&entry->origin);return false;
        }
        if(state.session->record()->revision!=previous)note("target-reserved",command,&entry->origin);
        const auto after=currentOwner();
        return contextCurrent()&&after&&sameFlow(*after,*current);
    }catch(...){state.lastError="target-record-exception";note("reservation-rejected",state.lastError.c_str());return false;}
}

// Call only AFTER the existing in-memory State.accept. An optional absent pin
// leaves targetMayExist intact; file history is never fed back into admission.
static void inputHostTargetReturnAcceptPsLocked(const json& receipt) {
    using namespace ymcc::inputHost::targetReturnRuntime;
    try {
        if(state.targets.empty()||!state.session||!state.session->ready())return;
        const auto pin=g_psEndpointOwnership.snapshot();if(!pin.valid)return;
        const auto owner=currentOwner();if(!owner)return;
        std::uint64_t sequence=0;
        if(receipt.is_object())sequence=receipt.value("sequence",0ULL);
        auto selected=state.targets.rend();
        for(auto it=state.targets.rbegin();it!=state.targets.rend();++it)if(sameFlow(it->origin,*owner)) {
            if(selected==state.targets.rend())selected=it;
            if(it->sequence==sequence){selected=it;break;}
        }
        if(selected==state.targets.rend()||!contains(selected->origin))return;
        const auto revision=state.session->record()->revision;
        const duty::AcceptedPsPin accepted{pin.rootInstanceId,pin.hidInstanceIds,pin.generation};
        if(!state.session->attachAcceptedPsPin(revision,selected->origin,accepted)){
            state.lastError=state.session->error().reason;note("pin-store-deferred",state.lastError.c_str(),&selected->origin);return;
        }
        if(state.session->record()->revision!=revision)note("accepted-pin-recorded","existing-State-accepted",&selected->origin);
    }catch(...){note("pin-store-deferred","accepted-pin-record-exception");}
}

// The accepted RECREATE reply follows the existing Host's serialized old
// release and complete pre-create endpoint reconciliation. Keep the new reserved
// target; only older target-only duties from this exact live Host flow are proven
// retired. A failed/lost reply never enters here. File failure keeps proven debt
// for the existing before-create flush, without undoing an actual new target.
static void inputHostTargetReturnAcceptedRecreateLocked(const json& receipt) {
    using namespace ymcc::inputHost::targetReturnRuntime;
    try {
        if(!g_inputHostMx.ownedByCurrentThread() || !receipt.is_object() ||
            !receipt.value("ok",false) || receipt.value("command",std::string{})!="RECREATE_TARGET" ||
            receipt.value("status",std::string{})!="target-prepared" ||
            !state.session || !state.session->ready())return;
        const auto owner=currentOwner();if(!owner)return;
        if(receipt.value("hostInstanceId",std::string{})!=owner->hostInstanceId ||
            receipt.value("runId",std::string{})!=owner->runId ||
            receipt.value("epoch",0ULL)!=owner->epoch ||
            receipt.value("targetId",std::string{})!=owner->targetId ||
            receipt.value("persona",std::string{})!=owner->persona)return;
        const auto sequence=receipt.find("sequence");
        if(sequence==receipt.end() || !sequence->is_number_integer() ||
            (!sequence->is_number_unsigned() && sequence->get<std::int64_t>()<=0))return;
        const auto accepted=sequence->get<std::uint64_t>();if(!accepted)return;
        const auto replacement=std::find_if(state.targets.begin(),state.targets.end(),
            [&](const auto& item){return item.sequence==accepted&&sameFlow(item.origin,*owner);});
        if(replacement==state.targets.end() || !contains(replacement->origin))return;
        for(const auto& item:state.targets)if(item.sequence<accepted && sameFlow(item.origin,*owner) &&
            std::none_of(state.proven.begin(),state.proven.end(),[&](const auto& old){return duty::sameOrigin(old,item.origin);})) {
            state.proven.push_back(item.origin);
            note("target-retirement-proven","accepted-recreate-completed-previous-target",&item.origin);
        }
        flushProven("accepted-recreate-completed-previous-target");
    }catch(...){note("settlement-deferred","recreate-target-settlement-exception");}
}

// Caller has actual RELEASE+SHUTDOWN receipts and this exact old Host's exit.
// Only origins published by this runtime are eligible. Loaded historical duties
// are deliberately untouched until the cold-return consumer proves them.
static void inputHostTargetReturnTargetExitedLocked(const std::string& runId,std::uint64_t epoch) {
    using namespace ymcc::inputHost::targetReturnRuntime;
    try {
        if(runId.empty()||!epoch||!state.session||!state.session->ready())return;
        for(const auto& entry:state.targets)if(entry.origin.runId==runId&&entry.origin.epoch==epoch&&
            std::none_of(state.proven.begin(),state.proven.end(),[&](const auto& old){return duty::sameOrigin(old,entry.origin);}))
                state.proven.push_back(entry.origin);
        flushProven("release-shutdown-and-exact-host-exit");
    }catch(...){note("settlement-deferred","target-settlement-exception");}
}

// Called only after the existing owner proved this exact retired PS target
// absent and physical return complete. Commit its current target-only duty
// before clearing that proof; no PnP/driver/mode or admission work is added.
static bool inputHostTargetReturnAfterPsQuarantineLocked(
        const ymcc::psEndpointOwnership::Retirement& retired,bool qualifiedAbsence) {
    using namespace ymcc::inputHost::targetReturnRuntime;
    try {
        if(!g_inputHostMx.ownedByCurrentThread() || !qualifiedAbsence || !retired.required ||
            g_inputHostProcess || retired.owner.persona!="dualsense-edge" ||
            !retired.owner.epoch || retired.owner.epoch!=g_inputHostEpoch ||
            retired.owner.runId.empty() || retired.owner.runId!=g_inputHostRunId)return false;
        // A previously accepted RECREATE may have proved its predecessor gone
        // while the file commit failed. Finish that existing exact proof before
        // comparing the new target's retirement pin; never relabel pin-A as B.
        if(!state.proven.empty() && !flushProven("previously-proven-recreate-before-PS-return"))return false;
        const auto entries=state.targets;
        for(const auto& entry:entries) {
            const auto& origin=entry.origin;
            if(origin.persona!=retired.owner.persona || origin.hostInstanceId!=retired.owner.hostInstanceId ||
                origin.runId!=retired.owner.runId || origin.epoch!=retired.owner.epoch ||
                origin.targetId!=retired.owner.targetId)continue;
            if(!state.session || !state.session->ready())return false;
            const auto parent=inputHostTargetReturnCaptureOwner(GetCurrentProcess());
            if(!parent || !duty::detail::sameProcess(origin.parent,*parent))return false;
            const auto* record=state.session->record();
            const auto group=std::find_if(record->obligations.begin(),record->obligations.end(),
                [&](const auto& item){return duty::sameOrigin(item.origin,origin);});
            if(group!=record->obligations.end()) {
                if(!origin.physicalRoot.empty() || !group->paths.empty())return false;
                if(group->acceptedPsPin && retired.target.valid) {
                    const duty::AcceptedPsPin pin{retired.target.rootInstanceId,
                        retired.target.hidInstanceIds,retired.target.generation};
                    if(!duty::detail::samePin(*group->acceptedPsPin,pin)) {
                        note("settlement-deferred","retired-PS-pin-does-not-match-origin",&origin);return false;
                    }
                }
                if(!state.session->consumeOrigin(record->revision,origin)) {
                    state.lastError=state.session->error().reason;
                    note("settlement-deferred",state.lastError.c_str(),&origin);return false;
                }
                note("target-settled","exact-retired-PS-absence-and-owned-Host-exit",&origin);
            }
            state.targets.erase(std::remove_if(state.targets.begin(),state.targets.end(),
                [&](const auto& value){return duty::sameOrigin(value.origin,origin);}),state.targets.end());
            state.proven.erase(std::remove_if(state.proven.begin(),state.proven.end(),
                [&](const auto& value){return duty::sameOrigin(value,origin);}),state.proven.end());
        }
        return true;
    }catch(...){note("settlement-deferred","PS-quarantine-record-exception");return false;}
}

// Exact old writer identity only. This is not target absence or return proof.
static ymcc::targetReturn::process::Probe inputHostTargetReturnProbePhysicalWriter(
        const ymcc::targetReturn::ProcessIdentity& owner) {
    return ymcc::targetReturn::process::probeRecordedOwner(owner);
}

// One read-only CM ancestry observation. Tests replace this explicit OS leaf,
// never the reservation/commit algorithm or the physical lifecycle consumer.
static ymcc::targetReturn::physicalPath::Result inputHostTargetReturnVerifyPhysicalPath(
        const std::string& root,const std::string& target) {
    return ymcc::targetReturn::physicalPath::verify(root,target);
}
namespace ymcc::inputHost::targetReturnRuntime {
struct PhysicalStamp {
    std::uint64_t power=0,topology=0,identity=0,source=0;
    bool operator==(const PhysicalStamp&) const = default;
};
static PhysicalStamp physicalStamp() {
    return {currentPowerGeneration(),g_xinputTopologyEpoch.load(std::memory_order_acquire),
        g_hidResolveCacheGeneration.load(std::memory_order_acquire),
        g_msiMainSourceRevision.load(std::memory_order_acquire)};
}
static bool physicalWritersQuiescent() {
    // A possible old native CLI can outlive its timeout. Its exact handle is
    // still owned by the existing CLI entry; observe only, never close/relabel.
    // A transport-faulted live Host can likewise still finish an old request.
    if(g_inputHostProcess&&g_inputHostTransportFault)return false;
    std::unique_lock<std::mutex> cliOwner(g_hidHideMx,std::try_to_lock);
    if(!cliOwner.owns_lock())return false;
    if(!hidHideCliReapPendingLocked(g_inputHostSwitchRecoveryStartDeadline))return false;
    return ymcc::hidHideCliWriterRuntime::quiescent();
}
static bool physicalContext(const duty::Origin& owner,const PhysicalStamp& stamp) {
    if(!contextCurrent()||!(physicalStamp()==stamp)||!physicalWritersQuiescent())return false;
    auto current=currentOwner();if(!current)return false;
    current->physicalRoot=owner.physicalRoot;
    return sameFlow(owner,*current);
}
static bool physicalOriginWriterSettled(const duty::Origin& origin) {
    const auto current=currentOwner();
    if(current&&duty::detail::sameProcess(current->host,origin.host)&&
       current->hostInstanceId==origin.hostInstanceId&&!g_inputHostTransportFault)return true;
    return inputHostTargetReturnProbePhysicalWriter(origin.host).state==duty::process::State::Dead;
}
static void pruneEmptyPhysicals() {
    state.physicals.erase(std::remove_if(state.physicals.begin(),state.physicals.end(),
        [&](const auto& item){return !contains(item.origin);}),state.physicals.end());
}
}

// One reached hide path, before either pipe or CLI dispatch. Its durable origin
// is independent of a later target lifetime. Carry/borrow reuse the old origin
// without re-labelling it as the new Host's responsibility.
static bool inputHostTargetReturnBeforeHideLocked(
        const std::vector<HidHidePhysicalIdentity>& sources,
        const HidHideVisibilitySnapshot& current,const HidHideVisibilitySnapshot& original,
        const std::string& target,const std::string& transactionId,
        bool* responsibilityReserved=nullptr,bool nativeOwnedReassertion=false) {
    using namespace ymcc::inputHost::targetReturnRuntime;
    if(responsibilityReserved)*responsibilityReserved=false;
    try {
        const bool alreadyHidden=hidHideContainsPath(current.hidden,target);
        if(!g_inputHostMx.ownedByCurrentThread()||!contextCurrent()||!current.valid||
            (nativeOwnedReassertion?!alreadyHidden:alreadyHidden)||!duty::detail::instance(target)||
            sources.empty()||sources.size()>duty::maxOrigins||transactionId.empty())return false;
        auto owner=currentOwner();if(!owner)return false;
        const auto stamp=physicalStamp();
        // Only the native owner's exact terminal-helper proof can supply a
        // prehidden path's original baseline. The caller's original snapshot
        // and this internal switch are never donor ownership authority.
        HidHideVisibilitySnapshot nativeBaseline;
        if(nativeOwnedReassertion&&
           (!msiNativeForegroundReassertionBaseline(target,nativeBaseline)||!nativeBaseline.valid||
            hidHideContainsPath(nativeBaseline.hidden,target)))return false;
        const auto& baseline=nativeOwnedReassertion?nativeBaseline:original;
        const auto nativeBaselineCurrent=[&]() {
            if(!nativeOwnedReassertion)return true;
            HidHideVisibilitySnapshot observed;
            const bool proven=msiNativeForegroundReassertionBaseline(target,observed)&&observed.valid&&
                !hidHideContainsPath(observed.hidden,target)&&observed.cloaking==nativeBaseline.cloaking&&
                observed.hidden==nativeBaseline.hidden&&observed.applications==nativeBaseline.applications&&
                observed.cloakRaw==nativeBaseline.cloakRaw&&observed.hiddenRaw==nativeBaseline.hiddenRaw&&
                observed.applicationsRaw==nativeBaseline.applicationsRaw&&observed.inverseRaw==nativeBaseline.inverseRaw;
            if(!proven)note("physical-reservation-rejected","native-donor-proof-retired-or-baseline-changed",nullptr,target);
            return proven;
        };
        std::string root;
        std::set<std::string> attemptedRoots;
        for(const auto& source:sources) {
            if(!source.resolved)continue;
            const auto candidate=source.baseContainerDeviceInstanceId.empty()
                ? source.deviceInstanceId : source.baseContainerDeviceInstanceId;
            if(!duty::detail::instance(candidate)||hidHideIsVirtualIdentity(candidate,{}))continue;
            if(!attemptedRoots.insert(duty::detail::fold(candidate)).second)continue;
            const auto proof=inputHostTargetReturnVerifyPhysicalPath(candidate,target);
            if(!contextCurrent()||!(physicalStamp()==stamp))return false;
            if(!proof.proven())continue;
            if(!root.empty()&&duty::detail::fold(root)!=duty::detail::fold(candidate)) {
                note("physical-reservation-rejected","ambiguous-selected-physical-roots",nullptr,target);return false;
            }
            root=candidate;
        }
        if(root.empty()){note("physical-reservation-rejected","selected-root-ancestry-unproven",nullptr,target);return false;}
        owner->physicalRoot=root;
        if(!physicalContext(*owner,stamp)||!open()||!flushProven()||!physicalContext(*owner,stamp))return false;
        pruneEmptyPhysicals();
        // File history alone is never a new origin authority. An in-process
        // carried path already published by this runtime keeps its old origin.
        for(const auto& group:state.session->record()->obligations)for(const auto& path:group.paths)
            if(duty::detail::fold(path.instanceId)==duty::detail::fold(target)) {
                const bool local=std::any_of(state.physicals.begin(),state.physicals.end(),
                    [&](const auto& item){return duty::sameOrigin(item.origin,group.origin);});
                if(!local||duty::detail::fold(group.origin.physicalRoot)!=duty::detail::fold(root)) {
                    note("physical-reservation-rejected","historical-or-other-root-path-not-admitted",&group.origin,target);return false;
                }
                if(responsibilityReserved)*responsibilityReserved=true;
                if(!physicalOriginWriterSettled(group.origin)) {
                    note("physical-reservation-rejected","old-origin-host-writer-unproven",&group.origin,target);return false;
                }
                return nativeBaselineCurrent()&&physicalContext(*owner,stamp);
            }
        if(!baseline.valid||hidHideContainsPath(baseline.hidden,target)) {
            note("physical-reservation-rejected","foreign-prehidden-or-baseline-unknown",nullptr,target);return false;
        }
        auto selected=std::find_if(state.physicals.begin(),state.physicals.end(),[&](const auto& item){
            return item.journalId==transactionId&&sameFlow(item.origin,*owner);});
        if(selected==state.physicals.end()) {
            if(state.physicals.size()>=duty::maxOrigins)return false;
            owner->transactionId=newKey(*owner);if(owner->transactionId.empty())return false;
            state.physicals.push_back({*owner,transactionId});selected=std::prev(state.physicals.end());
        }
        const auto revision=state.session->record()->revision;
        if(!state.session->reservePath(revision,selected->origin,target,{true,true,true})) {
            state.lastError=state.session->error().reason;
            note("physical-reservation-rejected",state.lastError.c_str(),&selected->origin,target);return false;
        }
        // A context may retire during file IO. The caller still retains the
        // now-durable exact path in its local return journal, but emits no hide.
        if(responsibilityReserved)*responsibilityReserved=true;
        if(state.session->record()->revision!=revision)
            note("physical-path-reserved","reached-own-hide-before-dispatch",&selected->origin,target);
        // Reassertion must still be dispatched as the same hide by the caller.
        // If native ownership changed during commit, keep the durable duty and
        // responsibilityReserved=true while withholding dispatch/adoption.
        return nativeBaselineCurrent()&&physicalContext(*owner,stamp);
    }catch(...){note("physical-reservation-rejected","physical-record-exception",nullptr,target);return false;}
}

// Save the complete set ALREADY hidden by this native owner before the first
// virtual handoff re-hide. No future ordinary hide tail is claimed here. Native
// ownership remains held until the caller's final readback/Accept succeeds.
static bool inputHostTargetReturnBeforeNativeHandoffLocked(
        const std::vector<HidHidePhysicalIdentity>& sources,
        const HidHideVisibilitySnapshot& current,const std::string& journalId,
        std::vector<std::wstring>* responsibilityReserved) {
    using namespace ymcc::inputHost::targetReturnRuntime;
    if(responsibilityReserved)responsibilityReserved->clear();
    try {
        if(!responsibilityReserved||!g_inputHostMx.ownedByCurrentThread()||!contextCurrent()||
           !current.valid||sources.empty()||sources.size()>duty::maxOrigins||journalId.empty())return false;
        auto owner=currentOwner();if(!owner)return false;
        const auto stamp=physicalStamp();
        std::vector<std::string> exactOwned;
        HidHideVisibilitySnapshot baseline;
        if(!msiNativeForegroundNativeHandoffSnapshot(exactOwned,baseline)||!baseline.valid||
           exactOwned.empty()||exactOwned.size()>16)return false;
        std::set<std::string> exactSet;
        std::vector<std::wstring> reservedPaths;reservedPaths.reserve(exactOwned.size());
        for(const auto& target:exactOwned) {
            if(!duty::detail::instance(target)||!exactSet.insert(duty::detail::fold(target)).second||
               !hidHideContainsPath(current.hidden,target)||hidHideContainsPath(baseline.hidden,target))return false;
            reservedPaths.push_back(U2W(target));
        }
        const auto nativeSetCurrent=[&]() {
            std::vector<std::string> observedOwned;HidHideVisibilitySnapshot observed;
            if(!msiNativeForegroundNativeHandoffSnapshot(observedOwned,observed)||!observed.valid||
               observedOwned.empty()||observedOwned.size()>16)return false;
            std::set<std::string> observedSet;
            for(const auto& target:observedOwned)
                if(!duty::detail::instance(target)||!observedSet.insert(duty::detail::fold(target)).second||
                   hidHideContainsPath(observed.hidden,target))return false;
            return observedSet==exactSet&&observed.cloaking==baseline.cloaking&&
                observed.hidden==baseline.hidden&&observed.applications==baseline.applications&&
                observed.cloakRaw==baseline.cloakRaw&&observed.hiddenRaw==baseline.hiddenRaw&&
                observed.applicationsRaw==baseline.applicationsRaw&&observed.inverseRaw==baseline.inverseRaw;
        };
        // Every member must have unambiguous ancestry to the same selected
        // physical root, under one captured owner/power/source/topology stamp.
        std::string commonRoot;
        for(const auto& target:exactOwned) {
            std::string root;std::set<std::string> attemptedRoots;
            for(const auto& source:sources) {
                if(!source.resolved)continue;
                const auto candidate=source.baseContainerDeviceInstanceId.empty()
                    ? source.deviceInstanceId : source.baseContainerDeviceInstanceId;
                if(!duty::detail::instance(candidate)||hidHideIsVirtualIdentity(candidate,{})||
                   !attemptedRoots.insert(duty::detail::fold(candidate)).second)continue;
                const auto proof=inputHostTargetReturnVerifyPhysicalPath(candidate,target);
                if(!contextCurrent()||!(physicalStamp()==stamp))return false;
                if(!proof.proven())continue;
                if(!root.empty()&&duty::detail::fold(root)!=duty::detail::fold(candidate)) {
                    note("native-owned-set-rejected","ambiguous-selected-physical-roots",nullptr,target);return false;
                }
                root=candidate;
            }
            if(root.empty()||(!commonRoot.empty()&&duty::detail::fold(commonRoot)!=duty::detail::fold(root))) {
                note("native-owned-set-rejected","complete-same-root-ancestry-unproven",nullptr,target);return false;
            }
            commonRoot=root;
        }
        owner->physicalRoot=commonRoot;
        if(!physicalContext(*owner,stamp)||!open()||!flushProven()||!physicalContext(*owner,stamp))return false;
        pruneEmptyPhysicals();
        // Preserve a single existing local origin exactly. File-only history,
        // mixed origins or an unsettled old writer cannot authorize this set.
        std::optional<duty::Origin> carried;bool hasNewPath=false;
        for(const auto& target:exactOwned) {
            bool found=false;
            for(const auto& group:state.session->record()->obligations)for(const auto& path:group.paths)
                if(duty::detail::fold(path.instanceId)==duty::detail::fold(target)) {
                    found=true;
                    const bool local=std::any_of(state.physicals.begin(),state.physicals.end(),
                        [&](const auto& item){return duty::sameOrigin(item.origin,group.origin);});
                    if(!local||duty::detail::fold(group.origin.physicalRoot)!=duty::detail::fold(commonRoot)||
                       (carried&&!duty::sameOrigin(*carried,group.origin))||!physicalOriginWriterSettled(group.origin)) {
                        note("native-owned-set-rejected","historical-cross-origin-or-writer-unproven",&group.origin,target);return false;
                    }
                    carried=group.origin;
                }
            if(!found)hasNewPath=true;
        }
        // An already complete carried set may stay under its old origin; never
        // append a new owner's paths to a retired Host's responsibility.
        if(carried&&hasNewPath&&!sameFlow(*carried,*owner))return false;
        auto selected=std::find_if(state.physicals.begin(),state.physicals.end(),[&](const auto& item){
            return carried?duty::sameOrigin(item.origin,*carried):item.journalId==journalId&&sameFlow(item.origin,*owner);});
        if(selected==state.physicals.end()) {
            if(state.physicals.size()>=duty::maxOrigins)return false;
            owner->transactionId=newKey(*owner);if(owner->transactionId.empty())return false;
            state.physicals.push_back({*owner,journalId});selected=std::prev(state.physicals.end());
        }
        if(!nativeSetCurrent()||!physicalContext(*owner,stamp))return false;
        const auto revision=state.session->record()->revision;
        if(!state.session->reserveNativeOwnedSet(revision,selected->origin,exactOwned,{true,true,true})) {
            state.lastError=state.session->error().reason;
            note("native-owned-set-rejected",state.lastError.c_str(),&selected->origin);return false;
        }
        // Preallocated output publishes EVERY committed member before any
        // callback/authority recheck can retire the request. Caller journals it
        // even on false, then sends zero re-hides unless this function returns true.
        responsibilityReserved->swap(reservedPaths);
        if(state.session->record()->revision!=revision)
            note("native-owned-set-reserved","existing-native-hidden-set-before-rehide",&selected->origin);
        return nativeSetCurrent()&&physicalContext(*owner,stamp);
    }catch(...){note("native-owned-set-rejected","native-owned-set-record-exception");return false;}
}

// No new read/driver operation. The actual return consumer has already proved
// endpoint absence and supplies its complete owned path readback. Failed durable
// settlement keeps the caller's journal; known focus borrow never calls this.
static bool inputHostTargetReturnAfterPhysicalRestoreLocked(
        const HidHideVisibilitySnapshot& after,bool readback,
        const std::vector<std::wstring>& ownedTargets) {
    using namespace ymcc::inputHost::targetReturnRuntime;
    try {
        if(!g_inputHostMx.ownedByCurrentThread())return false;
        if(!state.session||!state.session->ready()||state.physicals.empty())return true;
        if(!physicalWritersQuiescent()) {
            note("physical-settlement-deferred","old-physical-writer-not-yet-quiescent");return false;
        }
        const auto physicals=state.physicals;
        for(const auto& item:physicals) {
            const auto* record=state.session->record();
            const auto group=std::find_if(record->obligations.begin(),record->obligations.end(),
                [&](const auto& value){return duty::sameOrigin(value.origin,item.origin);});
            if(group==record->obligations.end())continue;
            const auto paths=group->paths;
            for(const auto& path:paths) {
                if(!hidHideContainsPath(ownedTargets,path.instanceId))continue;
                if(!physicalOriginWriterSettled(item.origin)) {
                    note("physical-settlement-deferred","old-origin-host-writer-unproven",&item.origin,path.instanceId);return false;
                }
                if(!readback||!after.valid||hidHideContainsPath(after.hidden,path.instanceId)) {
                    note("physical-settlement-deferred","owned-path-still-hidden-or-readback-unknown",&item.origin,path.instanceId);return false;
                }
                if(!state.session->consumePath(state.session->record()->revision,item.origin,path.instanceId)) {
                    state.lastError=state.session->error().reason;
                    note("physical-settlement-deferred",state.lastError.c_str(),&item.origin,path.instanceId);return false;
                }
                note("physical-path-settled","exact-owned-path-readback-not-hidden",&item.origin,path.instanceId);
            }
        }
        pruneEmptyPhysicals();return true;
    }catch(...){note("physical-settlement-deferred","physical-settlement-exception");return false;}
}

#include "virtual_target_return_cold_runtime.h"
