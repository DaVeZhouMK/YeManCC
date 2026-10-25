#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

// Retain only the exact source binding counter. The alias owns the original
// sensor cache through the existing shared_ptr control block; no allocation,
// source lock, clock or device API is needed at final pipe admission.
namespace ymcc::windows_motion {
class PublicationFence;
struct Stamp {
    std::shared_ptr<const std::atomic<std::uint64_t>> activeBinding;
    std::uint64_t bindingGeneration = 0;
    std::shared_ptr<const std::atomic<std::uint64_t>> choiceBinding;
    std::uint64_t choiceGeneration = 0;
    std::shared_ptr<const std::atomic<bool>> captureLease;
    std::shared_ptr<const std::atomic<bool>> processingLease;
    std::shared_ptr<const PublicationFence> publication;
};
template<class SensorOwner>
Stamp retain(const std::shared_ptr<SensorOwner>& owner, std::uint64_t binding) noexcept {
    return {owner ? std::shared_ptr<const std::atomic<std::uint64_t>>(owner, &owner->activeBindingGeneration)
                  : std::shared_ptr<const std::atomic<std::uint64_t>>{}, binding};
}
template<class Value>
inline bool sameOwner(const std::shared_ptr<const std::atomic<Value>>& a,
    const std::shared_ptr<const std::atomic<Value>>& b) noexcept {
    return (!a&&!b)||(a&&b&&a.get()==b.get()&&!a.owner_before(b)&&!b.owner_before(a));
}
inline bool same(const Stamp& a,const Stamp& b) noexcept {
    return a.bindingGeneration==b.bindingGeneration&&sameOwner(a.activeBinding,b.activeBinding)&&
        a.choiceGeneration==b.choiceGeneration&&sameOwner(a.choiceBinding,b.choiceBinding)&&sameOwner(a.captureLease,b.captureLease)&&sameOwner(a.processingLease,b.processingLease);
}
inline bool current(const Stamp& stamp) noexcept {
    return stamp.bindingGeneration != 0 && stamp.activeBinding &&
        stamp.activeBinding->load(std::memory_order_acquire) == stamp.bindingGeneration&&
        (stamp.choiceBinding?(stamp.choiceGeneration!=0&&stamp.choiceBinding->load(std::memory_order_acquire)==stamp.choiceGeneration):stamp.choiceGeneration==0)&&
        (!stamp.captureLease||stamp.captureLease->load(std::memory_order_acquire))&&
        (!stamp.processingLease||stamp.processingLease->load(std::memory_order_acquire));
}
// A raw capture ticket remains canonical. Only a consumer/submission stamp gets
// the additional choice and native-lifetime guards; existing guards are never
// silently replaced by newer ones.
inline Stamp guarded(Stamp capture,const Stamp& choice,
    std::shared_ptr<const std::atomic<bool>> lifetime={}) noexcept {
    if(!capture.activeBinding||!capture.bindingGeneration||capture.choiceBinding||capture.choiceGeneration||capture.captureLease||capture.processingLease||capture.publication||
        !choice.activeBinding||!choice.bindingGeneration||choice.choiceBinding||choice.choiceGeneration||choice.captureLease||choice.processingLease||choice.publication)return {};
    capture.choiceBinding=choice.activeBinding;capture.choiceGeneration=choice.bindingGeneration;capture.captureLease=std::move(lifetime);
    return capture;
}
inline bool sameInput(const Stamp& a,const Stamp& b) noexcept {
    return a.bindingGeneration==b.bindingGeneration&&sameOwner(a.activeBinding,b.activeBinding)&&
        a.choiceGeneration==b.choiceGeneration&&sameOwner(a.choiceBinding,b.choiceBinding)&&sameOwner(a.captureLease,b.captureLease);
}
inline Stamp forProcessing(Stamp raw,const std::shared_ptr<const std::atomic<bool>>& lease) noexcept {
    if(!lease||!current(raw))return {};
    if(raw.processingLease)return sameOwner(raw.processingLease,lease)?raw:Stamp{};
    raw.processingLease=lease;return raw;
}
// Publication is independent of sampling/CPU/calibration identity. Pending fences
// are permanently closed; commitment mints a different fence for fresh frames.
// A committed fence can only revoke and is bound to the actual processing lease.
class PublicationFence final {
    const Stamp source_;
    mutable std::atomic<bool> open_;
    PublicationFence(Stamp source,bool open):source_(std::move(source)),open_(open){}
public:
    static std::shared_ptr<const PublicationFence> pending(Stamp source) {
        if(!current(source)||source.processingLease||source.publication||!source.choiceBinding)return {};
        return std::shared_ptr<const PublicationFence>(new PublicationFence(std::move(source),false));
    }
    static std::shared_ptr<const PublicationFence> committed(Stamp processed) {
        if(!current(processed)||!processed.processingLease||!processed.choiceBinding)return {};
        processed.publication.reset(); // The proof retains tokens, never another fence.
        return std::shared_ptr<const PublicationFence>(new PublicationFence(std::move(processed),true));
    }
    bool matchesInput(const Stamp& value) const noexcept {return sameInput(source_,value);}
    bool allows(const Stamp& value) const noexcept {
        return open_.load(std::memory_order_acquire)&&current(source_)&&current(value)&&sameInput(source_,value)&&
            source_.processingLease&&sameOwner(source_.processingLease,value.processingLease);
    }
    void revoke() const noexcept {open_.store(false,std::memory_order_release);}
};
inline Stamp forPublication(Stamp raw,std::shared_ptr<const PublicationFence> fence) noexcept {
    if(!current(raw)||raw.processingLease||raw.publication)return {};
    if(fence&&!fence->matchesInput(raw))return {};
    raw.publication=std::move(fence);return raw;
}
inline bool currentForOutput(const Stamp& stamp) noexcept {
    return current(stamp)&&(stamp.publication?stamp.publication->allows(stamp):!stamp.captureLease);
}
} // namespace ymcc::windows_motion
