#pragma once
#include <cstdint>

namespace ymcc::msi::forceGamepad {
class ShoulderEdge {
    bool armed_=true;
public:
    bool observe(bool live,uint16_t buttons) {
        if(!live)return false;
        const auto shoulders=buttons&0x0300u;
        if(!shoulders){armed_=true;return false;}
        if(shoulders!=0x0300u||!armed_)return false;
        armed_=false;return true;
    }
};
enum class Trigger { Startup, WindowOpen, Shoulders, BeforeVirtual, Wake };
struct Request {
    uint64_t revision=0, sequence=0, power=0, due=0, deadline=0;
    Trigger trigger=Trigger::Startup;
};
// Pure event coalescing only. The existing lifecycle owner supplies time/power.
class Plan {
    bool enabled_=false, pending_=false;
    uint64_t revision_=0, sequence_=0;
    Request request_{};
public:
    bool enabled() const {return enabled_;}
    bool pending() const {return pending_;}
    uint64_t revision() const {return revision_;}
    uint64_t sequence() const {return sequence_;}
    bool setEnabled(bool value) {
        if(enabled_==value)return false;
        enabled_=value;++revision_;pending_=false;return true;
    }
    bool request(Trigger trigger,uint64_t now,uint64_t power,uint64_t delay=0) {
        if(!enabled_||!power||delay>3000)return false;
        // Showing the window is part of the same shoulder summon. Do not
        // advance its explicitly requested three-second settling period.
        if(pending_ && request_.power==power && request_.trigger==Trigger::Shoulders &&
           trigger==Trigger::WindowOpen && now<request_.due)return false;
        request_={revision_,++sequence_,power,now+delay,now+delay+30000,trigger};
        pending_=true;return true;
    }
    bool take(uint64_t now,uint64_t power,bool ready,Request& out) {
        if(!pending_)return false;
        if(!enabled_||request_.revision!=revision_||request_.power!=power||now>request_.deadline){pending_=false;return false;}
        if(now<request_.due||!ready)return false;
        out=request_;pending_=false;return true;
    }
    bool current(const Request& request,uint64_t power) const {
        return enabled_ && request.revision==revision_ && request.sequence==sequence_ && request.power==power;
    }
};
} // namespace ymcc::msi::forceGamepad
