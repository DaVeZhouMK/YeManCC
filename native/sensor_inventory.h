#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "windows_motion_submission.h"

// Metadata observation only. No source is opened, selected, calibrated or published here.
namespace ymcc::sensors::inventory {
enum class Kind { Gyro=0, Accel=1 };
enum class State { NotEnumerated, Complete, Failed, Cancelled, TimedOut, Retired, Partial, Truncated };
inline const char* stateName(State state) noexcept {
    switch(state){case State::Complete:return "complete";case State::Failed:return "failed";
    case State::Cancelled:return "cancelled";case State::TimedOut:return "timed-out";
    case State::Retired:return "source-retired";case State::Partial:return "partial";
    case State::Truncated:return "truncated";default:return "not-enumerated";}
}
struct Device { std::string id,name,provider; bool enabled=false; };
struct Listing { std::vector<Device> devices; std::uint64_t reportedCount=0; bool truncated=false; };
struct Part { State state=State::NotEnumerated; Listing listing; std::int64_t error=0; };
struct Snapshot {
    State state=State::NotEnumerated;
    std::array<Part,2> parts;
    std::uint64_t started=0,finished=0,budget=0;
    bool sourceCurrent=false;
};
struct Error { State state=State::Failed; std::int64_t code=0; };
struct Ticket { windows_motion::Stamp binding; std::uint64_t power=0; };
// A weak owner key never keeps a retired sensor sink alive between observations.
class Schedule {
    std::weak_ptr<const std::atomic<std::uint64_t>> owner_;
    std::uint64_t generation_=0,power_=0;
public:
    void reset() noexcept {owner_.reset();generation_=power_=0;}
    bool claim(const windows_motion::Stamp& binding,std::uint64_t power) noexcept {
        if(!power||!windows_motion::current(binding)){reset();return false;}
        const bool sameOwner=!owner_.owner_before(binding.activeBinding)&&!binding.activeBinding.owner_before(owner_);
        if(sameOwner&&generation_==binding.bindingGeneration&&power_==power)return false;
        owner_=binding.activeBinding;generation_=binding.bindingGeneration;power_=power;return true;
    }
};
template<class Enumerate,class Current,class Cancel,class Clock>
Snapshot observe(Enumerate enumerate,Current current,Cancel cancelled,Clock clock,std::uint64_t budget=1500) {
    Snapshot result;result.started=clock();result.budget=budget;
    const auto deadline=result.started+budget;
    for(unsigned i=0;i<2;++i){
        auto& part=result.parts[i];
        if(!current()){result.state=State::Retired;break;}
        if(cancelled()){result.state=State::Cancelled;break;}
        if(clock()>=deadline){part.state=State::TimedOut;result.state=State::TimedOut;break;}
        try {
            auto listing=enumerate(i==0?Kind::Gyro:Kind::Accel,deadline,cancelled);
            if(!current()){result.state=State::Retired;break;}
            if(cancelled()){part.state=State::Cancelled;result.state=State::Cancelled;break;}
            if(clock()>=deadline){part.state=State::TimedOut;result.state=State::TimedOut;break;}
            if(listing.reportedCount<listing.devices.size())throw Error{State::Failed,0};
            part.listing=std::move(listing);
            part.state=part.listing.truncated?State::Truncated:State::Complete;
        } catch(const Error& error){
            part.state=error.state;part.error=error.code;
        } catch(...){part.state=State::Failed;}
        if(part.state==State::Cancelled||part.state==State::TimedOut){result.state=part.state;break;}
    }
    result.finished=clock();result.sourceCurrent=current();
    if(result.state==State::Retired||!result.sourceCurrent){result.state=State::Retired;result.sourceCurrent=false;}
    else if(result.state!=State::Cancelled&&result.state!=State::TimedOut){
        const auto a=result.parts[0].state,b=result.parts[1].state;
        result.state=(a==State::Complete&&b==State::Complete)?State::Complete:
            (a==State::Failed&&b==State::Failed)?State::Failed:
            (a==State::Truncated||b==State::Truncated)?State::Truncated:State::Partial;
    }
    return result;
}
struct Group {
    std::string provider;
    unsigned enabledGyros=0,enabledAccels=0;
    bool duplicateIds=false,enumerationComplete=false;
};
inline std::vector<Group> groups(const Snapshot& snapshot) {
    std::map<std::string,Group> byProvider;
    std::array<std::map<std::string,unsigned>,2> ids;
    for(unsigned kind=0;kind<2;++kind)for(const auto& d:snapshot.parts[kind].listing.devices){
        if(d.provider.empty())continue;
        auto& group=byProvider[d.provider];group.provider=d.provider;
        if(++ids[kind][d.id]>1)group.duplicateIds=true;
        if(d.enabled){if(kind==0)++group.enabledGyros;else ++group.enabledAccels;}
    }
    const bool complete=snapshot.parts[0].state==State::Complete&&snapshot.parts[1].state==State::Complete;
    std::vector<Group> result;
    for(auto& [provider,group]:byProvider){group.enumerationComplete=complete;result.push_back(group);}
    return result;
}
} // namespace ymcc::sensors::inventory
