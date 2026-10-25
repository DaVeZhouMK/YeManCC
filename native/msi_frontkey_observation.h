#pragma once
// MSI-only observation plan. Memory/clock facts only: no hardware, registry,
// process, service, input, timer or action APIs. Bounded for one process lifetime.
#include <array>
#include <cstdint>
#include <mutex>
namespace ymcc::msi::frontObservation {
enum class Origin { Click, Takeover };
enum class Phase { Immediate, Settled };
struct Request {
    Origin origin=Origin::Click;Phase phase=Phase::Immediate;
    uint64_t power=0,source=0,sequence=0,capturedAt=0,vendorIntent=0,due=0,expires=0;
    uint32_t rule=0;int code=0;bool configured=false,actionDispatchAccepted=false,takeoverApplied=false;
};
struct Context {bool knownMsi=false,powerReady=false,exitRequested=false,recording=false;uint64_t power=0;uint32_t rule=0;};
inline bool current(const Request& r,const Context& c,bool sourceCurrent,bool vendorCurrent) {
    if(!c.knownMsi||!c.powerReady||c.exitRequested||!r.power||r.power!=c.power||r.rule!=c.rule)return false;
    if(r.origin==Origin::Click)return !c.recording&&sourceCurrent&&r.source!=0&&r.sequence!=0&&(r.code==41||r.code==88);
    return r.origin==Origin::Takeover&&r.vendorIntent!=0&&vendorCurrent;
}
inline bool sameEpisode(const Request& a,const Request& b){
    if(a.origin!=b.origin||a.power!=b.power||a.rule!=b.rule)return false;
    return a.origin==Origin::Click?a.source==b.source&&a.code==b.code:
        a.vendorIntent==b.vendorIntent&&a.takeoverApplied==b.takeoverApplied;
}
struct Stats {unsigned episodes=0,duplicate=0,budgetDenied=0,queueDrops=0,expired=0;};
class Plan {
    std::mutex mx_;std::array<Request,8> seen_{};std::array<Request,16> pending_{};
    std::array<bool,16> used_{};Stats stats_{};
public:
    bool offer(Request r,uint64_t now){
        std::lock_guard<std::mutex> l(mx_);
        if((r.origin!=Origin::Click&&r.origin!=Origin::Takeover)||!r.power||now==0||now>UINT64_MAX-6000||
            (r.origin==Origin::Click&&(!r.source||!r.sequence||(r.code!=41&&r.code!=88)))||
            (r.origin==Origin::Takeover&&!r.vendorIntent))return false;
        for(unsigned i=0;i<stats_.episodes;++i)if(sameEpisode(seen_[i],r)){++stats_.duplicate;return false;}
        if(stats_.episodes>=seen_.size()){++stats_.budgetDenied;return false;}
        unsigned free=0;for(bool u:used_)free+=!u;if(free<2){++stats_.queueDrops;return false;}
        seen_[stats_.episodes++]=r;
        for(unsigned phase=0;phase<2;++phase){
            auto copy=r;copy.phase=phase?Phase::Settled:Phase::Immediate;copy.due=now+(phase?1500:0);copy.expires=now+6000;
            for(unsigned i=0;i<used_.size();++i)if(!used_[i]){pending_[i]=copy;used_[i]=true;break;}
        }
        return true;
    }
    bool take(uint64_t now,Request& out){
        std::lock_guard<std::mutex> l(mx_);
        for(unsigned i=0;i<used_.size();++i)if(used_[i]){
            if(now>pending_[i].expires||now<pending_[i].due-(pending_[i].phase==Phase::Settled?1500:0)){used_[i]=false;++stats_.expired;continue;}
            if(now>=pending_[i].due){out=pending_[i];used_[i]=false;return true;}
        }
        return false;
    }
    void queueDropped(){std::lock_guard<std::mutex> l(mx_);++stats_.queueDrops;}
    Stats stats(){std::lock_guard<std::mutex> l(mx_);return stats_;}
};
} // namespace ymcc::msi::frontObservation