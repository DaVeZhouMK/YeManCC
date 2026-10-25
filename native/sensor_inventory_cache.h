#pragma once
#include <mutex>
#include <optional>
#include <utility>
#include "sensor_inventory.h"

namespace ymcc::sensors::inventory {
inline bool sameScope(const Ticket& a,const Ticket& b) noexcept {
    return a.power!=0&&a.power==b.power&&a.binding.bindingGeneration!=0&&
        a.binding.bindingGeneration==b.binding.bindingGeneration&&
        a.binding.activeBinding&&b.binding.activeBinding&&
        a.binding.activeBinding.get()==b.binding.activeBinding.get()&&
        !a.binding.activeBinding.owner_before(b.binding.activeBinding)&&
        !b.binding.activeBinding.owner_before(a.binding.activeBinding);
}
// An observation belongs to its original owner, binding and power scope.
// Cached metadata never retains a retired sensor sink or opens a device.
template<class Payload> class ObservationStore {
    struct Record {
        std::weak_ptr<const std::atomic<std::uint64_t>> owner;
        std::uint64_t generation=0,power=0,finished=0,serial=0;
        State state=State::NotEnumerated;
        bool sourceCurrent=false;
        Payload payload;
    };
    mutable std::mutex mutex_;
    std::optional<Record> record_;
    std::uint64_t serial_=0;
public:
    struct View {
        std::optional<Payload> lastObservation;
        std::optional<std::uint64_t> age;
        std::uint64_t generation=0,power=0,serial=0;
        State state=State::NotEnumerated;
        bool metadataContextCurrent=false;
        const char* reason="not-observed";
    };
    void clear() {std::lock_guard<std::mutex> lock(mutex_);record_.reset();}
    void publish(const Ticket& ticket,const Snapshot& observation,Payload payload) {
        std::lock_guard<std::mutex> lock(mutex_);
        record_=Record{ticket.binding.activeBinding,ticket.binding.bindingGeneration,ticket.power,
            observation.finished,++serial_,observation.state,observation.sourceCurrent,std::move(payload)};
    }
    View inspect(const Ticket& current,std::uint64_t now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        View result;if(!record_)return result;
        const auto& record=*record_;
        result.lastObservation=record.payload;result.generation=record.generation;result.power=record.power;
        result.serial=record.serial;result.state=record.state;
        if(now>=record.finished)result.age=now-record.finished;
        const auto owner=record.owner.lock();
        if(!current.power)result.reason="power-unknown";
        else if(record.power!=current.power)result.reason="power-changed";
        else if(!current.binding.activeBinding||!current.binding.bindingGeneration)result.reason="no-active-binding";
        else if(!owner)result.reason="recorded-owner-retired";
        else if(owner.get()!=current.binding.activeBinding.get()||
            owner.owner_before(current.binding.activeBinding)||current.binding.activeBinding.owner_before(owner))result.reason="owner-changed";
        else if(record.generation!=current.binding.bindingGeneration)result.reason="binding-changed";
        else if(!windows_motion::current(current.binding))result.reason="binding-inactive";
        else if(!record.sourceCurrent)result.reason="observation-source-retired";
        else if(now<record.finished)result.reason="clock-regressed";
        else {result.metadataContextCurrent=true;result.reason="observation-current-context";}
        return result;
    }
};
} // namespace ymcc::sensors::inventory
