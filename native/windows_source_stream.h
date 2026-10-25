#pragma once
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include "windows_source_selection.h"

// A private typed source keeps collecting independently of the chosen output
// route. Capture-parent lifetime and choice generation are different fences:
// selecting another route must not revive an old output stamp or stop the
// original SensorAPI/WinRT route needed for a possible restoration.
namespace ymcc::sensors::stream {
struct CaptureParent {
    inventory::Ticket scope;
    std::uint64_t lifetime=0,currentLifetime=0,currentPower=0;
    std::string model;
    bool modelBound=false;
};
inline bool current(const CaptureParent& value) noexcept {
    return value.modelBound&&!value.model.empty()&&value.lifetime&&value.currentLifetime==value.lifetime&&value.scope.power&&
        value.currentPower==value.scope.power&&windows_motion::current(value.scope.binding);
}
inline bool same(const CaptureParent& expected,const CaptureParent& actual) {
    return current(expected)&&current(actual)&&inventory::sameScope(expected.scope,actual.scope)&&
        expected.lifetime==actual.lifetime&&expected.model==actual.model;
}
class PrivateSource final {
    struct Record {
        probe::Series reports;
        CaptureParent lastParent;
        selection::Authority lastAuthority;
        std::uint64_t rejectedOrigin=0,rejectedFrame=0;
    };
    mutable std::mutex mutex_;
    const std::shared_ptr<std::atomic<bool>> captureLease_=std::make_shared<std::atomic<bool>>(false);
    const selection::Proposal proposal_;
    const selection::ModelRecipe model_;
    const inventory::Ticket requestScope_;
    const std::uint64_t lifetime_,started_;
    const CaptureParent parent_;
    const selection::Authority authority_;
    const std::array<std::string,2> ids_,providers_;
    std::array<Record,2> records_{};
    bool initialized_=false,accepting_=true;
    bool context(const CaptureParent& parent) const {
        return initialized_&&accepting_&&captureLease_->load(std::memory_order_acquire)&&same(parent_,parent);
    }
    bool origin(unsigned kind,const std::string& id,const selection::Authority& authority,
        const CaptureParent& parent) const {
        return kind<2&&context(parent)&&selection::same(ids_[kind],id)&&
            selection::sameAuthority(authority_,authority);
    }
public:
    PrivateSource(const selection::Proposal& proposal,const selection::ActiveSource& original,
        const std::string& key,const selection::Authority& authority,const CaptureParent& parent,
        std::array<std::string,2> providers,std::uint64_t started):
        proposal_(proposal),model_(original.model),requestScope_(original.scope),lifetime_(original.lifetime),
        started_(started),parent_(parent),authority_(authority),ids_{proposal.pair.gyroId,proposal.pair.accelId},providers_(std::move(providers)) {
        if(!authority_||!started_||!selection::live(original)||selection::authorizeIntent(original.model,proposal)||
            !current(parent_)||parent_.model!=selection::fingerprint(original.model)||
            parent_.scope.power!=original.scope.power||parent_.lifetime!=original.lifetime||
            providers_[0].empty()||!selection::same(providers_[0],providers_[1]))return;
        const auto owner=authority_->owner.lock();
        initialized_=owner&&requestScope_.binding.activeBinding&&owner.get()==requestScope_.binding.activeBinding.get()&&
            !owner.owner_before(requestScope_.binding.activeBinding)&&!requestScope_.binding.activeBinding.owner_before(owner)&&
            authority_->kind==selection::AuthorityKind::ExplicitModel&&authority_->id==proposal.requestId&&
            authority_->minimumGeneration==requestScope_.binding.bindingGeneration&&authority_->power==original.scope.power&&
            authority_->lifetime==lifetime_&&authority_->model==parent_.model&&selection::same(authority_->pair,proposal.pair)&&
            authority_->api==selection::sourceApis(proposal.pair)&&authority_->profile==parent_.model&&
            !key.empty()&&authority_->key==key&&authority_->policy==proposal.policy;
        captureLease_->store(initialized_,std::memory_order_release);
        for(auto& record:records_)record.reports.requested=true;
    }
    bool contextCurrent(const CaptureParent& parent) const {std::lock_guard<std::mutex> lock(mutex_);return context(parent);}
    bool opened(unsigned kind,const std::string& id,const CaptureParent& parent) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!origin(kind,id,authority_,parent))return false;
        auto& value=records_[kind].reports;value.opened=true;value.openError=0;return true;
    }
    void openError(unsigned kind,std::int64_t code) {
        std::lock_guard<std::mutex> lock(mutex_);if(!accepting_||kind>=2)return;
        auto& value=records_[kind].reports;value.openError=code?code:-1;value.lastReportUsable=false;
    }
    void interval(unsigned kind,std::uint32_t value) {
        std::lock_guard<std::mutex> lock(mutex_);if(!accepting_||kind>=2)return;
        records_[kind].reports.intervalObserved=true;records_[kind].reports.intervalMs=value;
    }
    bool readingFrame(unsigned kind,std::optional<std::int32_t> value,std::int64_t error,
        const CaptureParent& parent) {
        std::lock_guard<std::mutex> lock(mutex_);if(kind>=2||!context(parent))return false;
        auto& record=records_[kind].reports;
        if(authority_->api[kind]!="winrt"){record.standardSdkLayout.clear();record.standardSdkLayoutError=-1;record.lastReportUsable=false;return false;}
        record.readingTransform=value;
        record.readingTransformError=error?error:(value?0:-1);
        if(error||!value||*value!=0)record.lastReportUsable=false;
        return !error&&value&&*value==0;
    }
    bool standardSdkFrame(unsigned kind,const std::string& id,const std::string& layout,const CaptureParent& parent) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(kind>=2||!origin(kind,id,authority_,parent))return false;
        auto& record=records_[kind].reports;
        if(authority_->api[kind]!="sensorapi")return false;
        record.standardSdkLayout=layout;
        record.standardSdkLayoutError=layout==selection::standardSdkLayout(kind)?0:-1;
        if(record.standardSdkLayoutError)record.lastReportUsable=false;
        return selection::nativeReportedFrame("sensorapi",kind,record);
    }
    void readError(unsigned kind,const CaptureParent& parent) {
        std::lock_guard<std::mutex> lock(mutex_);if(kind<2&&context(parent))++records_[kind].reports.readErrors;
    }
    bool observe(unsigned kind,const std::string& id,const selection::Authority& authority,
        const probe::Axes& sample,const CaptureParent& parent) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(kind>=2)return false;
        auto& record=records_[kind];auto& reports=record.reports;
        if(!origin(kind,id,authority,parent)){if(accepting_)++record.rejectedOrigin;return false;}
        if(!reports.opened||reports.openError||!sample.receipt||sample.receipt<started_)return false;
        ++reports.callbacks;
        const auto& apis=authority_->api;
        if(!selection::nativeReportedFrame(apis[kind],kind,reports)){
            reports.lastReportUsable=false;++record.rejectedFrame;return false;}
        if(!sample.timestamp100ns||sample.timestamp100ns<reports.latestReportTimestamp100ns||sample.receipt<reports.latestReportReceipt){
            reports.lastReportUsable=false;++reports.rejectedTime;return false;}
        const bool duplicate=sample.timestamp100ns==reports.latestReportTimestamp100ns;
        if(!duplicate){reports.latestReportTimestamp100ns=sample.timestamp100ns;reports.latestReportReceipt=sample.receipt;}
        if(!probe::finite(sample)){reports.lastReportUsable=false;++reports.rejectedNumeric;return false;}
        if(duplicate){++reports.duplicates;return false;}
        if(!reports.first)reports.first=sample;
        reports.last=sample;reports.lastReportUsable=true;++reports.distinct;
        record.lastParent=parent;record.lastAuthority=authority;return true;
    }
    selection::Evidence evidence(std::uint64_t now,const CaptureParent& parent) const {
        std::lock_guard<std::mutex> lock(mutex_);
        selection::Evidence result;result.model=model_;result.now=now;result.probeSerial=proposal_.requestId;
        result.recorded=initialized_;result.contextCurrent=context(parent);
        if(!initialized_)return result;
        for(unsigned kind=0;kind<2;++kind){selection::Source value;value.kind=kind==0?inventory::Kind::Gyro:inventory::Kind::Accel;
            value.id=ids_[kind];value.provider=providers_[kind];value.standardUnits=true;value.observations=records_[kind].reports;value.api=authority_->api[kind];
            result.sources.push_back(std::move(value));}
        return result;
    }
    selection::PreparedSource prepared(std::uint64_t resource,std::uint64_t now,const CaptureParent& parent) const {
        selection::PreparedSource result;if(!resource)return result;
        auto reported=evidence(now,parent);auto proposal=proposal_;proposal.probeSerial=proposal.requestId;
        if(selection::authorize(reported,proposal))return result;
        for(const auto& source:reported.sources)if(!selection::completeReportedSeries(source,now,started_))return result;
        result.captureLease=captureLease_;result.captureScope=parent_.scope;result.authority=result.captureAuthority=authority_;result.resource=resource;result.requestId=proposal_.requestId;result.parentLifetime=lifetime_;
        result.parentScope=requestScope_;result.pair=proposal_.pair;result.profile=authority_->profile;result.calibrationKey=authority_->key;
        result.evidence=std::move(reported);return result;
    }
    std::optional<selection::LiveFrame> frame(const selection::ActiveSource& selected,
        const CaptureParent& parent,std::uint64_t now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!context(parent)||!selection::live(selected)||!selection::sameCaptureLease(selected.captureLease,captureLease_)||!selection::sameAuthority(selected.captureAuthority,authority_)||
            !selected.authority||selected.selectionId!=selected.authority->id||selected.scope.power!=parent_.scope.power||
            selected.lifetime!=lifetime_||selection::fingerprint(selected.model)!=parent_.model||
            !selection::same(selected.pair,proposal_.pair)||selected.profile!=authority_->profile||
            selected.calibrationKey!=authority_->key||selected.api!=authority_->api)return std::nullopt;
        selection::LiveFrame result;result.captureLease=captureLease_;result.captureScope=parent_.scope;result.authority=selected.authority;result.captureAuthority=authority_;result.scope=selected.scope;result.lifetime=lifetime_;
        result.selectionId=selected.selectionId;result.pair=proposal_.pair;result.profile=authority_->profile;result.calibrationKey=authority_->key;
        for(unsigned kind=0;kind<2;++kind){const auto& record=records_[kind];const auto& reports=record.reports;
            if(!selection::sameAuthority(record.lastAuthority,authority_)||!same(record.lastParent,parent)||
                !probe::ready(reports,now)||!reports.last)return std::nullopt;
            auto& value=result.values[kind];value.captureLease=captureLease_;value.captureScope=record.lastParent.scope;value.scope=selected.scope;value.lifetime=lifetime_;value.authority=selected.authority;value.captureAuthority=record.lastAuthority;
            value.kind=kind==0?inventory::Kind::Gyro:inventory::Kind::Accel;value.id=ids_[kind];value.provider=providers_[kind];
            value.api=authority_->api[kind];value.sample=*reports.last;value.readingTransform=reports.readingTransform;value.standardSdkLayout=reports.standardSdkLayout;
            value.sequence=reports.distinct;value.rejected=reports.rejectedNumeric+reports.rejectedTime;
            value.intervalMs=reports.intervalMs;value.intervalObserved=reports.intervalObserved;
            value.standardUnits=value.usable=true;
        }
        auto check=selected;check.frame=result;
        return selection::frameCurrent(check,now)?std::optional<selection::LiveFrame>{std::move(result)}:std::nullopt;
    }
    void retire() noexcept {std::lock_guard<std::mutex> lock(mutex_);accepting_=false;captureLease_->store(false,std::memory_order_release);}
};
} // namespace ymcc::sensors::stream
