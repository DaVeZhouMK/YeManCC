#pragma once
#include <cstdint>
#include <bit>
#include <cmath>
#include <optional>
#include <tuple>
#include <type_traits>
#include "gyro_telemetry_record.h"

// UI delivery only: retain an owning value and the exact renderer interval.
// Never authorizes or skips physical reads, mapping, Host work, or sensor work.
namespace ymcc::gyro_telemetry {
struct EmptyDeliveryContext {
    std::uintptr_t view = 0;
    std::uint64_t generation = 0, navigation = 0;
    bool ready = false, hidden = false, childPresent = false, diagnostics = false;
    bool operator==(const EmptyDeliveryContext&) const = default;
};
class EmptyDelivery {
    std::optional<Record> delivered_;
    EmptyDeliveryContext context_;
    std::uint64_t serial_ = 0;
    bool exhausted_ = false;
    template<std::size_t N> static bool sameAxes(const std::array<double,N>& a,const std::array<double,N>& b) {
        for(std::size_t i=0;i<N;++i)if(!std::isfinite(a[i])||!std::isfinite(b[i])||
            std::bit_cast<std::uint64_t>(a[i])!=std::bit_cast<std::uint64_t>(b[i]))return false;
        return true;
    }
    static bool sameJson(const json& a,const json& b) {
        if(a.type()!=b.type())return false;
        if(a.is_number_float()) {
            const auto x=a.get<double>(),y=b.get<double>();
            return std::isfinite(x)&&std::isfinite(y)&&std::bit_cast<std::uint64_t>(x)==std::bit_cast<std::uint64_t>(y);
        }
        if(a.is_structured()) {
            if(a.size()!=b.size())return false;
            auto i=a.begin(),j=b.begin();
            for(;i!=a.end();++i,++j)if((a.is_object()&&i.key()!=j.key())||!sameJson(i.value(),j.value()))return false;
            return true;
        }
        return a==b;
    }
    static bool sameValues(const Record& a,const Record& b) {
        if(std::tie(a.sensorPresent,a.targetStick,a.motionAdmission,a.motionTriggerResolved,a.aimingSightsTriggerResolved)!=
           std::tie(b.sensorPresent,b.targetStick,b.motionAdmission,b.motionTriggerResolved,b.aimingSightsTriggerResolved)||
           !sameAxes(a.gyro,b.gyro)||!sameAxes(a.accel,b.accel)||!sameAxes(a.rawAccel,b.rawAccel)||
           !sameAxes(a.gravity,b.gravity)||!sameAxes(a.processedAcceleration,b.processedAcceleration)||
           !sameAxes(a.playerSpace,b.playerSpace)||!sameAxes(a.worldSpace,b.worldSpace)||!sameAxes(a.orientation,b.orientation))return false;
        for(const auto pair:{std::pair{&a.gamepadMotionPlane,&b.gamepadMotionPlane},std::pair{&a.outputValue(),&b.outputValue()},
            std::pair{&a.leftStickValue(),&b.leftStickValue()},std::pair{&a.rightStickValue(),&b.rightStickValue()},std::pair{&a.motionContributionValue(),&b.motionContributionValue()},
            std::pair{&a.layoutContributionValue(),&b.layoutContributionValue()},std::pair{&a.motionManagerValue(),&b.motionManagerValue()},
            std::pair{&a.layoutManagerValue(),&b.layoutManagerValue()},std::pair{&a.latencyValue(),&b.latencyValue()},std::pair{&a.hostFrameValue(),&b.hostFrameValue()}})
            if(!sameJson(*pair.first,*pair.second))return false;
        return true;
    }
    static bool falseField(const json& object,const char* name) {
        const auto it=object.find(name);
        return it!=object.end()&&it->is_boolean()&&!it->get<bool>();
    }
    static bool eligible(const Record& r,const EmptyDeliveryContext& c) {
        if(!c.view||!c.ready||!c.hidden||c.childPresent||c.diagnostics||r.sensorPresent||
           !(r.gamepadMotionPlane.is_null()||(r.gamepadMotionPlane.is_object()&&r.gamepadMotionPlane.empty()))||
           !r.hostFrameValue().is_object())return false;
        const auto submission=r.hostFrameValue().find("hostSubmission");
        const auto reason=r.hostFrameValue().find("publicationReason");
        return submission!=r.hostFrameValue().end()&&submission->is_string()&&submission->get_ref<const std::string&>()=="publication-suppressed"&&
            reason!=r.hostFrameValue().end()&&reason->is_string()&&reason->get_ref<const std::string&>()=="virtual-target-disabled"&&
            falseField(r.hostFrameValue(),"hostActive")&&falseField(r.hostFrameValue(),"firstFrame");
    }
public:
    explicit EmptyDelivery(std::uint64_t prior = 0) noexcept : serial_(prior) {}
    std::uint64_t begin() noexcept {
        if(exhausted_)return 0;
        if(serial_==UINT64_MAX){exhausted_=true;delivered_.reset();return 0;}
        return ++serial_;
    }
    void clear() noexcept {delivered_.reset();}
    bool shouldPost(const Record& record,const EmptyDeliveryContext& context) {
        if(exhausted_||!eligible(record,context)){delivered_.reset();return true;}
        return !delivered_||context_!=context||!sameValues(*delivered_,record);
    }
    void posted(Record&& record,const EmptyDeliveryContext& context,bool success,std::uint64_t attempt) noexcept {
        // A nested UI delivery owns the later renderer state. Its result must
        // survive this older call returning, even if both captured one context.
        if(!attempt||exhausted_||attempt!=serial_)return;
        // Caller supplies only an eligible context after the unchanged send.
        // A failed send or a nonquiet interval never installs a comparison hit.
        if(!success||!eligible(record,context)){
            delivered_.reset();return;
        }
        static_assert(std::is_nothrow_move_constructible_v<Record>);
        delivered_.emplace(std::move(record));context_=context;
    }
};
}
