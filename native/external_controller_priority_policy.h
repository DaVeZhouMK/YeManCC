#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
namespace ymcc::externalpad {
struct Candidate {
    std::string root, location;
    unsigned slot = 4;
    bool physical = false, external = false, fresh = false;
    unsigned topology = 0;
    std::uint64_t power = 0;
};
inline bool ready(const Candidate& c) {
    return c.physical && c.external && c.fresh && c.slot < 4 &&
        !c.root.empty() && !c.location.empty();
}
// Time/identity stability, rather than button activity, owns arbitration. Neutral
// samples qualify; stick drift never chooses a different connected controller.
class Arbitration {
    std::string pendingRoot_, selectedRoot_, pendingLocation_, selectedLocation_;
    std::uint64_t since_ = 0, missingSince_ = 0, power_ = 0;
    unsigned observations_ = 0;
public:
    static constexpr std::uint64_t arrivalMs = 750, absenceMs = 2000;
    struct Result { Candidate selected; bool available=false, release=false; };
    Result observe(bool enabled, bool powerReady, std::uint64_t power,
                   std::uint64_t now, const std::vector<Candidate>& candidates) {
        if (!enabled) { reset(); return {{},false,true}; }
        if (!powerReady) return {}; // suspend is not a physical disconnect
        if (power_ != power) { power_=power; pendingRoot_.clear(); pendingLocation_.clear(); since_=0; observations_=0; missingSince_=0; }
        auto found=std::find_if(candidates.begin(),candidates.end(),[&](const auto& c){return ready(c)&&c.root==selectedRoot_&&c.location==selectedLocation_;});
        if(found!=candidates.end()) { missingSince_=0; return {*found,true,false}; }
        if(!selectedRoot_.empty()) {
            if(!missingSince_) missingSince_=now;
            if(now-missingSince_<absenceMs) return {};
        }
        std::vector<Candidate> eligible;
        for(const auto& c:candidates)if(ready(c))eligible.push_back(c);
        std::sort(eligible.begin(),eligible.end(),[](const auto& a,const auto& b){return a.root<b.root;});
        if(eligible.empty()) {
            pendingRoot_.clear();pendingLocation_.clear();observations_=0;
            if(!selectedRoot_.empty()){selectedRoot_.clear();selectedLocation_.clear();return {{},false,true};}
            return {};
        }
        const auto& next=eligible.front();
        if(pendingRoot_!=next.root || pendingLocation_!=next.location){pendingRoot_=next.root;pendingLocation_=next.location;since_=now;observations_=1;return {};}
        ++observations_;
        if(observations_<3 || now-since_<arrivalMs)return {};
        selectedRoot_=next.root;selectedLocation_=next.location;pendingRoot_.clear();pendingLocation_.clear();missingSince_=0;observations_=0;
        return {next,true,false};
    }
    void reset(){pendingRoot_.clear();selectedRoot_.clear();pendingLocation_.clear();selectedLocation_.clear();since_=missingSince_=power_=0;observations_=0;}
};
} // namespace ymcc::externalpad
