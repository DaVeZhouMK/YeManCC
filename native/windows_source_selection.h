#pragma once
#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "windows_source_eligibility.h"

// Serialized transaction contract. Platform callbacks own actual source routing;
// success requires their current value/lease snapshot, never an API write flag.
namespace ymcc::sensors::selection {
class SelectionTransaction;
enum class AuthorityKind { ExplicitModel, RestoredRoute };
class SourceAuthority final {
    friend class SelectionTransaction;
    SourceAuthority(std::uint64_t idValue,std::uint64_t powerValue,std::uint64_t lifetimeValue,std::uint64_t minimum,
        std::string modelValue,PairChoice pairValue,std::array<std::string,2> apiValue,std::string profileValue,
        std::string keyValue,TransformPolicy policyValue,AuthorityKind kindValue,
        const std::shared_ptr<const std::atomic<std::uint64_t>>& ownerValue):
        id(idValue),power(powerValue),lifetime(lifetimeValue),minimumGeneration(minimum),model(std::move(modelValue)),
        pair(std::move(pairValue)),api(std::move(apiValue)),profile(std::move(profileValue)),key(std::move(keyValue)),
        policy(policyValue),kind(kindValue),owner(ownerValue){}
public:
    const std::uint64_t id,power,lifetime,minimumGeneration;
    const std::string model;
    const PairChoice pair;
    const std::array<std::string,2> api;
    const std::string profile,key;
    const TransformPolicy policy;
    const AuthorityKind kind;
    const std::weak_ptr<const std::atomic<std::uint64_t>> owner;
};
using Authority=std::shared_ptr<const SourceAuthority>;
using CaptureLease=std::shared_ptr<const std::atomic<bool>>;
inline bool sameCaptureLease(const CaptureLease& a,const CaptureLease& b) noexcept {
    return (!a&&!b)||(a&&b&&a.get()==b.get()&&!a.owner_before(b)&&!b.owner_before(a));
}
inline bool sameAuthority(const Authority& a,const Authority& b) noexcept {
    return (!a&&!b)||(a&&b&&a.get()==b.get()&&!a.owner_before(b)&&!b.owner_before(a));
}
struct LiveValue {
    CaptureLease captureLease;
    std::optional<inventory::Ticket> captureScope;
    inventory::Ticket scope;std::uint64_t lifetime=0;Authority authority,captureAuthority;
    inventory::Kind kind=inventory::Kind::Gyro;
    std::string id,provider,api;
    probe::Axes sample;
    std::uint64_t sequence=0,rejected=0;std::uint32_t intervalMs=0;bool intervalObserved=false;
    std::optional<std::int32_t> readingTransform;
    bool standardUnits=false,usable=false;
    std::string standardSdkLayout;
};
struct LiveFrame {
    CaptureLease captureLease;
    std::optional<inventory::Ticket> captureScope;
    Authority authority,captureAuthority;
    inventory::Ticket scope;
    std::uint64_t lifetime=0,selectionId=0;
    PairChoice pair;
    std::string profile,calibrationKey;
    std::array<LiveValue,2> values;
};
struct ActiveSource {
    CaptureLease captureLease;
    inventory::Ticket scope;
    ModelRecipe model;
    PairChoice pair;
    std::array<std::string,2> api;
    std::string profile,calibrationKey;
    std::uint64_t lifetime=0,currentLifetime=0,currentPower=0,selectionId=0,resource=0;
    TransformPolicy policy=TransformPolicy::Unspecified;Authority authority,captureAuthority;
    bool acceptsChanges=false;
    std::optional<LiveFrame> frame;
};
inline bool sameOwner(const inventory::Ticket& a,const inventory::Ticket& b) noexcept {
    return a.binding.activeBinding&&b.binding.activeBinding&&a.binding.activeBinding.get()==b.binding.activeBinding.get()&&
        !a.binding.activeBinding.owner_before(b.binding.activeBinding)&&!b.binding.activeBinding.owner_before(a.binding.activeBinding);
}
inline bool sameParent(const ActiveSource& a,const ActiveSource& b) {
    return a.scope.power!=0&&a.scope.power==b.scope.power&&a.currentPower==b.currentPower&&
        a.currentPower==a.scope.power&&b.currentPower==b.scope.power&&a.lifetime!=0&&a.lifetime==b.lifetime&&
        a.currentLifetime==b.currentLifetime&&a.currentLifetime==a.lifetime&&b.currentLifetime==b.lifetime&&
        sameOwner(a.scope,b.scope)&&a.model.valid()&&b.model.valid()&&fingerprint(a.model)==fingerprint(b.model);
}
// captureAuthority identifies the immutable private capture creation. A later
// restoration gets a fresh choice authority while retaining this same origin.
inline bool captureAuthorityMatches(const ActiveSource& value) {
    if(!value.captureAuthority)return value.policy==TransformPolicy::Unspecified;
    const auto& captured=*value.captureAuthority;const auto owner=captured.owner.lock();
    return value.policy==TransformPolicy::ExplicitCurrentModel&&owner&&value.scope.binding.activeBinding&&
        owner.get()==value.scope.binding.activeBinding.get()&&!owner.owner_before(value.scope.binding.activeBinding)&&
        !value.scope.binding.activeBinding.owner_before(owner)&&captured.kind==AuthorityKind::ExplicitModel&&
        captured.power==value.scope.power&&captured.lifetime==value.lifetime&&
        captured.minimumGeneration<value.scope.binding.bindingGeneration&&captured.model==fingerprint(value.model)&&
        same(captured.pair,value.pair)&&captured.api==value.api&&captured.profile==value.profile&&
        captured.key==value.calibrationKey&&captured.policy==value.policy;
}
inline bool authorityMatches(const ActiveSource& value) {
    if(!value.authority)return value.policy==TransformPolicy::Unspecified&&!value.captureAuthority;
    const auto& authority=*value.authority;const auto owner=authority.owner.lock();
    return captureAuthorityMatches(value)&&owner&&value.scope.binding.activeBinding&&owner.get()==value.scope.binding.activeBinding.get()&&
        !owner.owner_before(value.scope.binding.activeBinding)&&!value.scope.binding.activeBinding.owner_before(owner)&&
        authority.id==value.selectionId&&authority.power==value.scope.power&&authority.lifetime==value.lifetime&&
        authority.minimumGeneration<value.scope.binding.bindingGeneration&&authority.model==fingerprint(value.model)&&
        same(authority.pair,value.pair)&&authority.api==value.api&&authority.profile==value.profile&&authority.key==value.calibrationKey&&
        authority.policy==value.policy&&(authority.kind!=AuthorityKind::ExplicitModel||
            (value.policy==TransformPolicy::ExplicitCurrentModel&&value.api==sourceApis(value.pair)&&
                value.profile==fingerprint(value.model)));
}
inline bool live(const ActiveSource& value) {
    return value.acceptsChanges&&value.model.valid()&&value.scope.power&&value.lifetime&&value.scope.power==value.currentPower&&
        value.lifetime==value.currentLifetime&&windows_motion::current(value.scope.binding)&&authorityMatches(value);
}
inline bool sameRoute(const ActiveSource& a,const ActiveSource& b) {
    return same(a.pair,b.pair)&&a.api==b.api&&a.profile==b.profile&&a.calibrationKey==b.calibrationKey;
}
inline bool unchanged(const ActiveSource& original,const ActiveSource& current) {
    return live(current)&&sameParent(original,current)&&inventory::sameScope(original.scope,current.scope)&&
        original.resource==current.resource&&original.selectionId==current.selectionId&&
        original.policy==current.policy&&sameAuthority(original.authority,current.authority)&&
        sameAuthority(original.captureAuthority,current.captureAuthority)&&sameCaptureLease(original.captureLease,current.captureLease)&&sameRoute(original,current);
}
inline bool frameCurrent(const ActiveSource& source,std::uint64_t now,std::uint64_t minimumReceipt=0) {
    if(!live(source)||!source.frame||source.pair.gyroId.empty()||source.pair.accelId.empty())return false;
    const auto& frame=*source.frame;
    if(source.captureLease){if(!source.captureLease->load(std::memory_order_acquire))return false;}
    else if(source.policy==TransformPolicy::ExplicitCurrentModel)return false;
    if(source.policy==TransformPolicy::ExplicitCurrentModel&&(!frame.captureScope||!frame.captureAuthority))return false;
    if(!sameAuthority(frame.captureAuthority,source.captureAuthority)||!sameCaptureLease(frame.captureLease,source.captureLease))return false;
    if(frame.captureScope&&(frame.captureScope->power!=source.scope.power||!windows_motion::current(frame.captureScope->binding)))return false;
    if(!inventory::sameScope(source.scope,frame.scope)||!windows_motion::current(frame.scope.binding)||
        frame.lifetime!=source.lifetime||frame.selectionId!=source.selectionId||!same(frame.pair,source.pair)||
        frame.profile!=source.profile||frame.calibrationKey!=source.calibrationKey||!sameAuthority(frame.authority,source.authority))return false;
    for(unsigned kind=0;kind<2;++kind){const auto& value=frame.values[kind];
        if(!sameAuthority(value.captureAuthority,frame.captureAuthority)||!sameCaptureLease(value.captureLease,frame.captureLease))return false;
        if(frame.captureScope){if(!value.captureScope||!inventory::sameScope(*frame.captureScope,*value.captureScope)||
            !windows_motion::current(value.captureScope->binding))return false;}
        else if(value.captureScope)return false;
        if(!inventory::sameScope(value.scope,source.scope)||!windows_motion::current(value.scope.binding)||
            value.lifetime!=source.lifetime||!sameAuthority(value.authority,source.authority)||value.kind!=(kind==0?inventory::Kind::Gyro:inventory::Kind::Accel)||
            !same(value.id,kind==0?source.pair.gyroId:source.pair.accelId)||value.api!=source.api[kind]||
            value.provider.empty()||(value.api!="winrt"&&value.api!="sensorapi")||
            !value.standardUnits||!value.usable||!probe::finite(value.sample)||
            !value.sample.timestamp100ns||!value.sample.receipt||value.sample.receipt<minimumReceipt||
            value.sample.receipt>now||now-value.sample.receipt>1500)return false;
        if(source.policy==TransformPolicy::ExplicitCurrentModel){
            if(sourceApi(value.id)!=value.api)return false;
            if(value.api=="winrt"&&(!value.readingTransform||*value.readingTransform!=0||!value.standardSdkLayout.empty()))return false;
            if(value.api=="sensorapi"&&(value.readingTransform||value.standardSdkLayout!=standardSdkLayout(kind)))return false;
        }
    }
    return same(frame.values[0].provider,frame.values[1].provider);
}
struct PreparedSource {
    CaptureLease captureLease;
    std::optional<inventory::Ticket> captureScope;
    Authority authority,captureAuthority;
    std::uint64_t resource=0,requestId=0,parentLifetime=0;
    inventory::Ticket parentScope;
    PairChoice pair;
    std::string profile,calibrationKey;
    Evidence evidence;
};
struct Adoption {
    bool reportedSuccess=false,ownershipTransferred=false;
    std::uint64_t reservedGeneration=0;
};
enum class Outcome { Idle, Busy, Preparing, Adopting, Rejected, NoOp, Committed, RolledBack, Retired, RecoveryRequired };
inline const char* outcomeName(Outcome value) noexcept {
    switch(value){case Outcome::Busy:return "busy";case Outcome::Preparing:return "preparing";case Outcome::Adopting:return "adopting";
    case Outcome::Rejected:return "rejected";case Outcome::NoOp:return "already-selected";case Outcome::Committed:return "committed-input-observed";
    case Outcome::RolledBack:return "rolled-back-input-observed";case Outcome::Retired:return "source-retired";
    case Outcome::RecoveryRequired:return "recovery-required";default:return "idle";}
}
struct Receipt {
    std::uint64_t requestId=0,beforeGeneration=0,afterGeneration=0;
    bool quarantined=false;
    Outcome outcome=Outcome::Idle;
    std::string reason;
    bool adoptionReportedSuccess=false,recoveryReportedSuccess=false,resourceTransferred=false;
    bool currentInputObserved=false,oldLeaseRetired=false;
};
struct Operations {
    void* state=nullptr;
    std::uint64_t(*now)(void*) noexcept=nullptr;
    bool(*cancelled)(void*) noexcept=nullptr;
    ActiveSource(*current)(void*)=nullptr; // Coherent value snapshot, no UI handle exposure.
    bool(*prepare)(void*,const Proposal&,const ActiveSource&,const std::string&,const Authority&,PreparedSource&) noexcept=nullptr;
    Adoption(*adopt)(void*,const Proposal&,const ActiveSource&,const PreparedSource&) noexcept=nullptr;
    bool(*recover)(void*,const Proposal&,const ActiveSource&,std::uint64_t,const Authority&) noexcept=nullptr;
    void(*quarantine)(void*,const ActiveSource&,std::uint64_t) noexcept=nullptr;
    void(*release)(void*,std::uint64_t) noexcept=nullptr; // Caller-owned candidates only.
    void(*finalize)(void*,const Receipt&) noexcept=nullptr; // Transport-owned inactive resources only.
    bool complete() const noexcept {return now&&cancelled&&current&&prepare&&adopt&&recover&&quarantine&&release&&finalize;}
};
class SelectionTransaction final {
    std::atomic<bool> running_{false};
    mutable std::mutex receiptMutex_;
    Receipt receipt_;
    std::uint64_t highWater_=0;
    struct KnownKey {std::string model;PairChoice pair;std::array<std::string,2> api;std::string profile,key;TransformPolicy policy=TransformPolicy::Unspecified;};
    std::vector<KnownKey> keys_; // Bounded values only, no source/native lifetime retained.
    void publish(const Receipt& value){std::lock_guard<std::mutex> lock(receiptMutex_);receipt_=value;}
    void remember(const ActiveSource& source){
        if(!source.model.valid()||source.pair.gyroId.empty()||source.pair.accelId.empty()||source.profile.empty()||source.calibrationKey.empty())return;
        const auto model=fingerprint(source.model);
        for(auto& known:keys_)if(known.model==model&&same(known.pair,source.pair)&&known.api==source.api&&known.profile==source.profile&&known.policy==source.policy){known.key=source.calibrationKey;return;}
        if(keys_.size()==32)keys_.erase(keys_.begin());keys_.push_back({model,source.pair,source.api,source.profile,source.calibrationKey,source.policy});
    }
    std::string keyFor(const ModelRecipe& model,const Proposal& proposal) const {
        const auto profile=fingerprint(model);
        for(const auto& known:keys_)if(known.model==profile&&same(known.pair,proposal.pair)&&
            known.api==sourceApis(proposal.pair)&&known.profile==profile&&known.policy==proposal.policy)return known.key;
        return calibrationKey(model,proposal.pair,proposal.policy);
    }
public:
    Receipt snapshot() const {std::lock_guard<std::mutex> lock(receiptMutex_);return receipt_;}
    Receipt apply(const Proposal& proposal,const Operations& ops) {
        Receipt result;result.requestId=proposal.requestId;
        bool expected=false;if(!running_.compare_exchange_strong(expected,true)){result.outcome=Outcome::Busy;result.reason="single-source-writer-busy";return result;}
        struct Running {std::atomic<bool>& value;~Running(){value.store(false);}} running{running_};
        struct CandidateOwner {const Operations& ops;std::uint64_t token=0;bool owned=true;~CandidateOwner(){if(token&&owned&&ops.release)ops.release(ops.state,token);}} candidateOwner{ops};
        const auto finish=[&](Outcome outcome,const char* reason){result.outcome=outcome;result.reason=reason;publish(result);return result;};
        if(!ops.complete())return finish(Outcome::Rejected,"platform-operations-unavailable");
        if(!proposal.requestId||proposal.requestId<=highWater_)return finish(Outcome::Rejected,"request-id-retired");
        highWater_=proposal.requestId;
        bool adoptionCalled=false;std::optional<ActiveSource> originForFailure;
        try {
            const auto original=ops.current(ops.state);originForFailure=original;result.beforeGeneration=original.scope.binding.bindingGeneration;
            if(!live(original)||ops.cancelled(ops.state))return finish(Outcome::Retired,"source-owner-not-live");
            if(const auto* reason=authorizeIntent(original.model,proposal))return finish(Outcome::Rejected,reason);
            const auto profile=fingerprint(original.model);
            if(same(original.pair,proposal.pair)&&original.api==sourceApis(proposal.pair)&&
                original.profile==profile&&original.policy==proposal.policy&&frameCurrent(original,ops.now(ops.state))){result.currentInputObserved=true;result.afterGeneration=result.beforeGeneration;return finish(Outcome::NoOp,"current-source-already-matches");}
            remember(original);const auto key=keyFor(original.model,proposal);
            result.outcome=Outcome::Preparing;publish(result);const auto started=ops.now(ops.state);
            const Authority authority(new SourceAuthority(proposal.requestId,original.scope.power,original.lifetime,original.scope.binding.bindingGeneration,
                profile,proposal.pair,sourceApis(proposal.pair),profile,key,proposal.policy,AuthorityKind::ExplicitModel,original.scope.binding.activeBinding));
            PreparedSource candidate;const bool prepared=ops.prepare(ops.state,proposal,original,key,authority,candidate);candidateOwner.token=candidate.resource;
            if(!prepared||!candidate.resource)return finish(Outcome::Rejected,"candidate-preparation-failed");
            const auto afterPrepare=ops.current(ops.state);
            if(ops.cancelled(ops.state)||!unchanged(original,afterPrepare))return finish(Outcome::Retired,"source-changed-during-preparation");
            if(!sameAuthority(candidate.authority,authority)||!sameAuthority(candidate.captureAuthority,authority)||candidate.requestId!=proposal.requestId||candidate.parentLifetime!=original.lifetime||
                !inventory::sameScope(candidate.parentScope,original.scope)||!same(candidate.pair,proposal.pair)||
                candidate.profile!=profile||candidate.calibrationKey!=key||!candidate.captureLease||!candidate.captureLease->load(std::memory_order_acquire)||!candidate.captureScope||
                candidate.captureScope->power!=original.scope.power||!windows_motion::current(candidate.captureScope->binding))
                return finish(Outcome::Rejected,"prepared-source-identity-mismatch");
            auto preparedEvidence=candidate.evidence;preparedEvidence.now=ops.now(ops.state);
            Proposal preparedProposal=proposal;preparedProposal.probeSerial=proposal.requestId;
            if(preparedEvidence.probeSerial!=proposal.requestId||fingerprint(preparedEvidence.model)!=profile||
                authorize(preparedEvidence,preparedProposal))return finish(Outcome::Rejected,"prepared-data-not-qualified");
            unsigned fresh=0;for(const auto& source:preparedEvidence.sources)if(
                (source.kind==inventory::Kind::Gyro&&same(source.id,proposal.pair.gyroId))||
                (source.kind==inventory::Kind::Accel&&same(source.id,proposal.pair.accelId)))
                if(completeReportedSeries(source,preparedEvidence.now,started))++fresh;
            if(fresh!=2)return finish(Outcome::Rejected,"fresh-prepare-window-reports-required");
            // A last source/cancellation check precedes the single adoption call.
            if(ops.cancelled(ops.state)||!unchanged(original,ops.current(ops.state))||!windows_motion::current(candidate.captureScope->binding)||!candidate.captureLease->load(std::memory_order_acquire))
                return finish(Outcome::Retired,"source-changed-before-adoption");
            result.outcome=Outcome::Adopting;publish(result);adoptionCalled=true;
            const auto attempt=ops.adopt(ops.state,proposal,original,candidate);result.adoptionReportedSuccess=attempt.reportedSuccess;
            if(attempt.ownershipTransferred)candidateOwner.owned=false;
            auto current=ops.current(ops.state);
            if(current.resource==candidate.resource)candidateOwner.owned=false;
            result.resourceTransferred=!candidateOwner.owned;result.afterGeneration=current.scope.binding.bindingGeneration;
            if(ops.cancelled(ops.state)||!sameParent(original,current)||!current.acceptsChanges){
                result=finish(Outcome::Retired,"lifecycle-authority-changed-after-adoption");ops.finalize(ops.state,result);return result;}
            const bool adopted=current.selectionId==proposal.requestId&&current.resource==candidate.resource&&
                attempt.reservedGeneration>original.scope.binding.bindingGeneration&&current.scope.binding.bindingGeneration==attempt.reservedGeneration&&
                sameAuthority(current.authority,authority)&&sameAuthority(current.captureAuthority,candidate.captureAuthority)&&sameCaptureLease(current.captureLease,candidate.captureLease)&&current.policy==proposal.policy&&live(current)&&
                same(current.pair,proposal.pair)&&current.api==sourceApis(proposal.pair)&&
                current.profile==profile&&current.calibrationKey==key&&current.frame&&current.frame->captureScope&&
                inventory::sameScope(*candidate.captureScope,*current.frame->captureScope)&&frameCurrent(current,ops.now(ops.state),started);
            if(adopted){result.currentInputObserved=true;result.oldLeaseRetired=!windows_motion::current(original.scope.binding);
                remember(current);result=finish(Outcome::Committed,"current-source-and-frame-observed");ops.finalize(ops.state,result);return result;}
            if(unchanged(original,current)){result=finish(Outcome::Rejected,"adoption-not-observed;old-source-retained");ops.finalize(ops.state,result);return result;}
            if(current.selectionId!=proposal.requestId){result=finish(Outcome::Retired,"different-source-writer-observed");ops.finalize(ops.state,result);return result;}
            const auto floor=(std::max)({original.scope.binding.bindingGeneration,current.scope.binding.bindingGeneration,attempt.reservedGeneration});
            // An invalid tentative route is never left publishing during recovery.
            ops.quarantine(ops.state,current,proposal.requestId);result.quarantined=true;
            const Authority recoveryAuthority(new SourceAuthority(proposal.requestId,original.scope.power,original.lifetime,floor,
                profile,original.pair,original.api,original.profile,original.calibrationKey,original.policy,AuthorityKind::RestoredRoute,original.scope.binding.activeBinding));
            const auto recoveryStarted=ops.now(ops.state);result.recoveryReportedSuccess=ops.recover(ops.state,proposal,original,floor,recoveryAuthority);
            const auto restored=ops.current(ops.state);result.afterGeneration=restored.scope.binding.bindingGeneration;
            bool reportsAdvanced=true;
            if(original.frame&&restored.frame)for(unsigned kind=0;kind<2;++kind)
                reportsAdvanced=reportsAdvanced&&restored.frame->values[kind].sample.timestamp100ns>original.frame->values[kind].sample.timestamp100ns;
            const bool recovered=!ops.cancelled(ops.state)&&sameParent(original,restored)&&restored.selectionId==proposal.requestId&&
                restored.scope.binding.bindingGeneration>floor&&sameRoute(original,restored)&&sameAuthority(restored.authority,recoveryAuthority)&&
                sameAuthority(restored.captureAuthority,original.captureAuthority)&&sameCaptureLease(restored.captureLease,original.captureLease)&&
                restored.policy==original.policy&&
                !windows_motion::current(original.scope.binding)&&frameCurrent(restored,ops.now(ops.state),recoveryStarted)&&reportsAdvanced;
            if(recovered){result.currentInputObserved=true;result.oldLeaseRetired=true;remember(restored);result=finish(Outcome::RolledBack,"original-source-observed-under-fresh-lease");}
            else {if(sameParent(original,restored)){ops.quarantine(ops.state,restored,proposal.requestId);result.quarantined=true;}
                result=finish(Outcome::RecoveryRequired,"fresh-original-source-not-observed");}
            ops.finalize(ops.state,result);return result;
        }catch(...){if(adoptionCalled&&originForFailure){ops.quarantine(ops.state,*originForFailure,proposal.requestId);result.quarantined=true;}
            result=finish(adoptionCalled?Outcome::RecoveryRequired:Outcome::Rejected,"platform-snapshot-failed");if(adoptionCalled)ops.finalize(ops.state,result);return result;}
    }
    // Returning the retained default route is its own transaction. Its old
    // choice is retired; only the native capture/model remain reusable.
    Receipt returnDefault(std::uint64_t requestId,const ActiveSource& saved,const Operations& ops) {
        Receipt result;result.requestId=requestId;
        bool expected=false;if(!running_.compare_exchange_strong(expected,true)){result.outcome=Outcome::Busy;result.reason="single-source-writer-busy";return result;}
        struct Running {std::atomic<bool>& value;~Running(){value.store(false);}} running{running_};
        const auto finish=[&](Outcome outcome,const char* reason){result.outcome=outcome;result.reason=reason;publish(result);return result;};
        if(!ops.complete())return finish(Outcome::Rejected,"platform-operations-unavailable");
        if(!requestId||requestId<=highWater_)return finish(Outcome::Rejected,"request-id-retired");
        highWater_=requestId;std::optional<ActiveSource> original;bool routed=false;
        try {
            original=ops.current(ops.state);result.beforeGeneration=original->scope.binding.bindingGeneration;
            if(!live(*original)||ops.cancelled(ops.state))return finish(Outcome::Retired,"source-owner-not-live");
            if(!sameParent(saved,*original)||saved.policy!=TransformPolicy::Unspecified||saved.captureAuthority||saved.captureLease||
                saved.pair.gyroId.empty()||saved.pair.accelId.empty()||saved.profile.empty()||saved.calibrationKey.empty())
                return finish(Outcome::Rejected,"retained-default-route-not-qualified");
            Proposal intent;intent.requestId=requestId;
            const auto floor=(std::max)(saved.scope.binding.bindingGeneration,original->scope.binding.bindingGeneration);
            const auto makeAuthority=[&](const ActiveSource& target,std::uint64_t minimum){return Authority(new SourceAuthority(requestId,
                target.scope.power,target.lifetime,minimum,fingerprint(target.model),target.pair,target.api,target.profile,
                target.calibrationKey,target.policy,AuthorityKind::RestoredRoute,target.scope.binding.activeBinding));};
            const auto defaultAuthority=makeAuthority(saved,floor);const auto started=ops.now(ops.state);
            result.outcome=Outcome::Adopting;publish(result);routed=true;
            result.adoptionReportedSuccess=ops.recover(ops.state,intent,saved,floor,defaultAuthority);
            auto restored=ops.current(ops.state);result.afterGeneration=restored.scope.binding.bindingGeneration;
            const auto observed=[&](const ActiveSource& target,const ActiveSource& current,const Authority& authority,std::uint64_t minimum,std::uint64_t since){
                return !ops.cancelled(ops.state)&&sameParent(target,current)&&current.selectionId==requestId&&
                    current.scope.binding.bindingGeneration>minimum&&sameRoute(target,current)&&sameAuthority(current.authority,authority)&&
                    sameAuthority(current.captureAuthority,target.captureAuthority)&&sameCaptureLease(current.captureLease,target.captureLease)&&
                    current.policy==target.policy&&!windows_motion::current(original->scope.binding)&&frameCurrent(current,ops.now(ops.state),since);
            };
            if(observed(saved,restored,defaultAuthority,floor,started)){
                result.currentInputObserved=result.oldLeaseRetired=true;remember(restored);
                result=finish(Outcome::Committed,"default-source-observed-under-fresh-lease");ops.finalize(ops.state,result);return result;
            }
            if(ops.cancelled(ops.state)||!sameParent(*original,restored)){
                ops.quarantine(ops.state,restored,requestId);result.quarantined=true;
                result=finish(Outcome::Retired,"default-restoration-lifecycle-retired");ops.finalize(ops.state,result);return result;
            }
            if(unchanged(*original,restored)){
                result=finish(Outcome::Rejected,"default-restoration-not-observed;old-source-retained");ops.finalize(ops.state,result);return result;
            }
            // A failed default restoration may recover a retained private
            // source, with a newly minted choice and its original capture.
            ops.quarantine(ops.state,restored,requestId);result.quarantined=true;
            const auto recoveryFloor=(std::max)(floor,restored.scope.binding.bindingGeneration);
            const auto recoveryAuthority=makeAuthority(*original,recoveryFloor);const auto recoveryStarted=ops.now(ops.state);
            result.recoveryReportedSuccess=ops.recover(ops.state,intent,*original,recoveryFloor,recoveryAuthority);
            restored=ops.current(ops.state);result.afterGeneration=restored.scope.binding.bindingGeneration;
            if(observed(*original,restored,recoveryAuthority,recoveryFloor,recoveryStarted)){
                result.currentInputObserved=result.oldLeaseRetired=true;remember(restored);
                result=finish(Outcome::RolledBack,"private-source-restored-after-default-failure");
            }else {ops.quarantine(ops.state,restored,requestId);result.quarantined=true;result=finish(Outcome::RecoveryRequired,"fresh-default-and-recovery-not-observed");}
            ops.finalize(ops.state,result);return result;
        }catch(...){if(routed&&original){ops.quarantine(ops.state,*original,requestId);result.quarantined=true;}
            result=finish(routed?Outcome::RecoveryRequired:Outcome::Rejected,"default-platform-snapshot-failed");if(routed)ops.finalize(ops.state,result);return result;}
    }

};
} // namespace ymcc::sensors::selection
