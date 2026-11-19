#pragma once
#include "steam_game_group_lease.h"
#include "steam_native_read_scope.h"
#include "steam_native_record_diagnostic.h"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ymcc::steamOriginalRecovery { class EngineQualifiedReadAdapter; }
namespace ymcc::steamgamefacts {
// PRIVATE r4, UNBUILT. Module facts come only from the actual Engine adapter.
// These DTOs alone cannot construct a frame or mint a role binding.
struct QualifiedNativeModule {
    std::uint64_t base=0,actualSize=0,referenceSize=0;
    std::wstring path;
    std::string diskSha256;
};
struct QualifiedNativeReadSite {
    ProcessWitness steam,selected;
    DWORD windowsSession=0;
    QualifiedNativeModule client,ui;
    std::uint32_t uiProfile=0;
    std::uint64_t getterRva=0,publisherRva=0;
    bool fixedOriginalQualification=false,legacyRoute=false;
};

inline bool ProcessGroupLease::nativeReadScopeCurrent(ymcc::steamnative::ReadScope& scope)const noexcept{
    const auto signalsCurrent=[this]{
        const auto group=cancellationSignal_.load(std::memory_order_acquire);
        const auto process=selected_->request_->cancellationSignal_.load(std::memory_order_acquire);
        return (!group||group->current())&&(!process||process->current());
    };
    return signalsCurrent()&&scope.issuedFor(*selected_->request_->observer_,*selected_->identitySnapshot())&&
        processCurrent()&&scope.current()&&processCurrent()&&signalsCurrent();
}

class BoundQualifiedRead;
// Only an actual launcher read can create this object. current() retains the
// ORIGINAL owning callback, never the snapshot's borrowed read/current ports.
// It checks the held lifetime; the next existing read must be consumed to
// establish current tuple/module/profile facts. No extra reader lives here.
class GroupNativeRoleBinding final {
    friend class BoundQualifiedRead;
    const std::shared_ptr<ProcessGroupLease> group_;
    const std::shared_ptr<ymcc::steamnative::ReadScope> scope_;
    const std::shared_ptr<RequestCancellation> business_;
    const std::function<bool()> originalCurrent_;
    const ymcc::steamrecord::Query query_;
    const QualifiedNativeReadSite site_;
    const ymcc::steamrecord::TypedNativeRoleObservation first_;
    const std::pair<std::uint32_t,std::uint32_t> tuple_;
    mutable std::atomic<bool> revoked_{false};
    std::atomic<bool> completed_{false};

    GroupNativeRoleBinding(std::shared_ptr<ProcessGroupLease> group,
        std::shared_ptr<ymcc::steamnative::ReadScope> scope,
        std::shared_ptr<RequestCancellation> business,std::function<bool()> originalCurrent,
        QualifiedNativeReadSite site,ymcc::steamrecord::TypedNativeRoleObservation first)
        :group_(std::move(group)),scope_(std::move(scope)),business_(std::move(business)),
         originalCurrent_(std::move(originalCurrent)),query_(first.query()),site_(std::move(site)),
         first_(std::move(first)),tuple_{first_.nativeRoles()->primaryLivenessPid,
                                      first_.nativeRoles()->foregroundComparisonPid}{}
    static bool sameQuery(const ymcc::steamrecord::Query& a,const ymcc::steamrecord::Query& b)noexcept{
        return a.moduleBase==b.moduleBase&&a.moduleSize==b.moduleSize&&a.account64==b.account64&&
            a.gameId64==b.gameId64&&a.expectedPid==b.expectedPid&&a.profileVerified==b.profileVerified&&
            a.profile==b.profile;
    }
    static bool sameWitness(const ProcessWitness& a,const ProcessWitness& b){
        return a.pid==b.pid&&a.created==b.created&&samePath(a.image,b.image);
    }
    static bool sameModule(const QualifiedNativeModule& a,const QualifiedNativeModule& b){
        return a.base==b.base&&a.actualSize==b.actualSize&&a.referenceSize==b.referenceSize&&
            samePath(a.path,b.path)&&a.diskSha256==b.diskSha256;
    }
    static bool sameSite(const QualifiedNativeReadSite& a,const QualifiedNativeReadSite& b){
        return sameWitness(a.steam,b.steam)&&sameWitness(a.selected,b.selected)&&a.windowsSession==b.windowsSession&&
            sameModule(a.client,b.client)&&sameModule(a.ui,b.ui)&&a.uiProfile==b.uiProfile&&
            a.getterRva==b.getterRva&&a.publisherRva==b.publisherRva&&
            a.fixedOriginalQualification==b.fixedOriginalQualification&&a.legacyRoute==b.legacyRoute;
    }
    bool heldCurrent()const noexcept{
        try{
            return !revoked_.load(std::memory_order_acquire)&&group_&&scope_&&business_&&originalCurrent_&&
                business_->current()&&originalCurrent_()&&group_->nativeReadScopeCurrent(*scope_)&&
                business_->current()&&originalCurrent_()&&!revoked_.load(std::memory_order_acquire);
        }catch(...){return false;}
    }
    // Only the synchronous frame may compare provisional tail facts. Public
    // consumers must wait for its final original-current guard to complete.
    void compareFinishedRead(const BoundQualifiedRead& read)const;
public:
    GroupNativeRoleBinding(const GroupNativeRoleBinding&)=delete;
    GroupNativeRoleBinding& operator=(const GroupNativeRoleBinding&)=delete;
    void cancel()const noexcept{revoked_.store(true,std::memory_order_release);}
    bool current()const noexcept{
        if(!completed_.load(std::memory_order_acquire))return false;
        if(!heldCurrent()){cancel();return false;}
        return !revoked_.load(std::memory_order_acquire);
    }
    // The argument can only be minted by the actual qualified Engine adapter.
    // It reuses that frame's completed reads; no read or foreign typed input.
    void consumeExistingExactRead(const BoundQualifiedRead& read)const;
    const auto& processGroup()const noexcept{return group_;}
    const auto& readScope()const noexcept{return scope_;}
    const auto& businessCancellation()const noexcept{return business_;}
    const auto& query()const noexcept{return query_;}
    const auto& modulesAndProcesses()const noexcept{return site_;}
    const auto& firstNativeObservation()const noexcept{return first_;}
    const auto& firstTuple()const noexcept{return tuple_;}
    bool nativePrimaryExact()const noexcept{return false;}
    bool nativeForegroundExact()const noexcept{return false;}
};

// A synchronous borrowed frame: neither copy nor move nor a public constructor.
// The private Engine adapter supplies the already qualified views and actual q.
// Only complete() returns a retainable binding, with no borrowed callbacks.
class BoundQualifiedRead final {
    friend class ymcc::steamOriginalRecovery::EngineQualifiedReadAdapter;
    friend class GroupNativeRoleBinding;
    enum class Phase { Fresh, Begun, Finished, Completed, Failed };
    const ymcc::steamdiagnostic::ReadViews* views_;
    std::function<bool()> localCurrent_;
    const ymcc::steamrecord::Query query_;
    const QualifiedNativeReadSite site_;
    const std::shared_ptr<ProcessGroupLease> group_;
    const std::shared_ptr<ymcc::steamnative::ReadScope> scope_;
    const std::shared_ptr<RequestCancellation> business_;
    const std::function<bool()> originalCurrent_;
    const std::shared_ptr<GroupNativeRoleBinding> previous_;
    std::shared_ptr<GroupNativeRoleBinding> binding_;
    std::optional<ymcc::steamrecord::TypedNativeRoleObservation> native_;
    ymcc::steamrecord::detail::Selection selected_;
    ymcc::steamrecord::detail::Header records_;
    std::vector<ymcc::steamrecord::detail::Target> targets_;
    Phase phase_=Phase::Fresh;
    static void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
    void fail()noexcept{
        phase_=Phase::Failed;
        if(previous_)previous_->cancel();
        if(binding_)binding_->cancel();
    }

    BoundQualifiedRead(const ymcc::steamdiagnostic::ReadViews& views,std::function<bool()> localCurrent,
        ymcc::steamrecord::Query query,QualifiedNativeReadSite site,
        std::shared_ptr<ProcessGroupLease> group,std::shared_ptr<ymcc::steamnative::ReadScope> scope,
        std::shared_ptr<RequestCancellation> business,std::function<bool()> originalCurrent,
        std::shared_ptr<GroupNativeRoleBinding> previous)
        :views_(&views),localCurrent_(std::move(localCurrent)),query_(query),site_(std::move(site)),
         group_(std::move(group)),scope_(std::move(scope)),business_(std::move(business)),
         originalCurrent_(std::move(originalCurrent)),previous_(std::move(previous)){}

    void qualifyLauncher(){
        require(group_&&scope_&&business_&&originalCurrent_,"group-native-launcher-request-unavailable");
        const auto& source=group_->source();
        // selected_ is the original private owner object; never reacquire it.
        const auto& selected=*group_->selected_;
        require(query_.profileVerified&&query_.account64&&query_.gameId64&&query_.expectedPid&&
            query_.moduleBase==site_.client.base&&query_.moduleSize==site_.client.referenceSize&&
            source.fullGameId==query_.gameId64&&source.selectedPid==query_.expectedPid&&
            exactGameId(source.appid,query_.gameId64),"group-native-launcher-query-mismatch");
        require(GroupNativeRoleBinding::sameWitness(site_.selected,selected.gameWitness())&&
            GroupNativeRoleBinding::sameWitness(site_.steam,selected.steamWitness())&&
            site_.windowsSession==selected.windowsSession(),"group-native-launcher-owner-mismatch");
        // This checks the actual publisher + group account-candidate epochs.
        // Actual account64 remains this reader's q, including a live UI account
        // different from/unknown to the registry. No positive registry gate.
        // nativeReadScopeCurrent performs issuedFor against the private actual
        // Request publisher/receipt; this frame cannot supply another receipt.
        require(group_->nativeReadScopeCurrent(*scope_)&&business_->current()&&originalCurrent_(),
            "group-native-launcher-scope-revoked");
        const auto& roles=*native_->nativeRoles();
        require(ymcc::steamgameroles::directLauncherTuple(group_->graph(),
            roles.primaryLivenessPid,roles.foregroundComparisonPid),"group-native-launcher-topology-required");
        if(previous_){previous_->compareFinishedRead(*this);binding_=previous_;}
        else binding_=std::shared_ptr<GroupNativeRoleBinding>(new GroupNativeRoleBinding(
            group_,scope_,business_,originalCurrent_,site_,*native_));
    }
public:
    BoundQualifiedRead(const BoundQualifiedRead&)=delete;
    BoundQualifiedRead& operator=(const BoundQualifiedRead&)=delete;
    BoundQualifiedRead(BoundQualifiedRead&&)=delete;
    BoundQualifiedRead& operator=(BoundQualifiedRead&&)=delete;
    ~BoundQualifiedRead(){
        if(phase_!=Phase::Completed){if(previous_)previous_->cancel();if(binding_)binding_->cancel();}
    }
    // In the original TypedRecord timer: exactly its original reader call.
    nlohmann::json begin(){
        require(phase_==Phase::Fresh,"qualified-native-read-begin-order");
        phase_=Phase::Begun;
        try{return ymcc::steamrecord::observeWithReadViews(query_,*views_,localCurrent_,&native_);}
        catch(...){fail();throw;}
    }
    // In the original RecordBinding timer: exactly these original three reads.
    // No typed observation, target vector or verified field is accepted here.
    void finish(){
        require(phase_==Phase::Begun,"qualified-native-read-finish-order");
        try{
        ymcc::steamrecord::detail::Reader reader(*views_);
        selected_=ymcc::steamrecord::detail::selectUser(reader,query_);
        records_=ymcc::steamrecord::detail::readHeader(reader,selected_.user,query_.profile);
        targets_=ymcc::steamrecord::detail::readTargets(reader,query_,records_);
        phase_=Phase::Finished;
        if(!group_)return;
        const bool launcher=native_&&native_->matchingTargetRecordCount()==1&&native_->nativeRoles()&&
            native_->nativeRoles()->primaryLivenessPid!=query_.expectedPid&&
            native_->nativeRoles()->primaryLivenessPid==native_->nativeRoles()->foregroundComparisonPid;
        if(previous_)require(launcher,"group-native-launcher-frozen-tuple-changed");
        if(!launcher||group_->graph().count!=2)return; // exact/split keep their original gates
        require(targets_.size()==1&&native_->nativeRoles()->primaryLivenessPid==targets_[0].primary&&
            native_->nativeRoles()->foregroundComparisonPid==targets_[0].foreground,
            "group-native-role-tuple-changed-since-typed-read");
        qualifyLauncher();
        }catch(...){fail();throw;}
    }
    auto takeSelection(){require(phase_==Phase::Finished,"qualified-native-selection-order");return std::move(selected_);}
    const auto& header()const noexcept{return records_;}
    const auto& targets()const noexcept{return targets_;}
    const auto& typedObservation()const noexcept{return native_;}
    // Existing root/root refusal can only be satisfied by this real frame's
    // pending object. current() stays false until the original final guard.
    const GroupNativeRoleBinding& requireLauncherProof(const ProcessGroupLease& expected)const{
        require(phase_==Phase::Finished&&binding_&&binding_->group_.get()==&expected,
            "group-native-selected-role-required");
        return *binding_;
    }
    // Replaces only the original owned() at the original post-diagnostic site.
    // No ReadViews or localCurrent callback is copied into the returned object.
    std::shared_ptr<GroupNativeRoleBinding> complete(){
        require(phase_==Phase::Finished,"qualified-native-read-complete-order");
        try{
        require(localCurrent_&&localCurrent_(),"binding-after-native-read");
        if(binding_)binding_->completed_.store(true,std::memory_order_release);
        phase_=Phase::Completed;views_=nullptr;localCurrent_={};
        if(binding_)binding_->consumeExistingExactRead(*this);
        return binding_;
        }catch(...){fail();throw;}
    }
};

inline void GroupNativeRoleBinding::compareFinishedRead(const BoundQualifiedRead& read)const{
    try{
        const bool completedRead=read.phase_==BoundQualifiedRead::Phase::Finished||
            read.phase_==BoundQualifiedRead::Phase::Completed;
        const auto& native=read.native_;
        if(!completedRead||!native||native->matchingTargetRecordCount()!=1||!native->nativeRoles()||
            read.group_!=group_||read.scope_!=scope_||read.business_!=business_||
            !sameQuery(query_,native->query())||!sameQuery(query_,read.query_)||!sameSite(site_,read.site_)||
            read.targets_.size()!=1||read.targets_[0].primary!=tuple_.first||read.targets_[0].foreground!=tuple_.second||
            native->nativeRoles()->primaryLivenessPid!=tuple_.first||
            native->nativeRoles()->foregroundComparisonPid!=tuple_.second||!current()){
            cancel();throw std::runtime_error("group-native-launcher-frozen-read-changed");
        }
    }catch(...){cancel();throw;}
}
inline void GroupNativeRoleBinding::consumeExistingExactRead(const BoundQualifiedRead& read)const{
    if(read.phase_!=BoundQualifiedRead::Phase::Completed){
        cancel();throw std::runtime_error("group-native-launcher-read-not-completed");
    }
    compareFinishedRead(read);
}
} // namespace ymcc::steamgamefacts
