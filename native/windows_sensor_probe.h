#pragma once
#include <array>
#include <algorithm>
#include <set>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "sensor_inventory_cache.h"

// Explicit observation only. A metadata provider match is not an axis recipe,
// a physical pairing proof, or permission to replace the selected input.
namespace ymcc::sensors::probe {
using Ticket=inventory::Ticket;
inline std::string canonical(std::string value) {
    for(auto& c:value)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
    return value;
}
struct Pair {std::string provider,gyro,accel;};
struct Plan {
    std::vector<Pair> pairs;
    unsigned eligiblePairs=0,ambiguousProviders=0,unpairedProviders=0,observableSources=0,plannedSources=0;
    bool truncated=false;
};
inline Plan plan(const std::array<std::vector<inventory::Device>,2>& devices,bool complete,bool current) {
    Plan out;if(!complete||!current)return out;
    struct Group {std::array<std::vector<const inventory::Device*>,2> enabled;bool duplicate=false;};
    std::map<std::string,Group> groups;
    std::array<std::map<std::string,unsigned>,2> counts;
    std::array<std::set<std::string>,2> seen;
    for(unsigned kind=0;kind<2;++kind)for(const auto& device:devices[kind])++counts[kind][canonical(device.id)];
    for(unsigned kind=0;kind<2;++kind)for(const auto& device:devices[kind]){
        if(device.id.empty()||device.provider.empty())continue;
        auto& group=groups[canonical(device.provider)];
        group.duplicate=group.duplicate||counts[kind][canonical(device.id)]!=1;
        if(device.enabled)group.enabled[kind].push_back(&device);
    }
    const auto append=[&](Pair ids){
        const unsigned sources=static_cast<unsigned>(!ids.gyro.empty())+static_cast<unsigned>(!ids.accel.empty());
        out.observableSources+=sources;
        if(out.plannedSources+sources>8){out.truncated=true;return;}
        out.plannedSources+=sources;out.pairs.push_back(std::move(ids));
    };
    for(auto& [provider,group]:groups){
        for(auto& kind:group.enabled)std::sort(kind.begin(),kind.end(),[](auto a,auto b){return canonical(a->id)<canonical(b->id);});
        const bool unique=!group.duplicate&&group.enabled[0].size()==1&&group.enabled[1].size()==1;
        if(unique){
            ++out.eligiblePairs;
            const auto* gyro=group.enabled[0][0];const auto* accel=group.enabled[1][0];
            if(seen[0].insert(canonical(gyro->id)).second&&seen[1].insert(canonical(accel->id)).second)
                append({provider,gyro->id,accel->id});
        }else{
            if(group.duplicate||group.enabled[0].size()>1||group.enabled[1].size()>1)++out.ambiguousProviders;
            else ++out.unpairedProviders;
            const auto size=(std::max)(group.enabled[0].size(),group.enabled[1].size());
            // Observe ambiguous and unpaired streams independently. Never invent
            // gyro/accel association from order, provider count or equal names.
            for(std::size_t i=0;i<size;++i)for(unsigned kind=0;kind<2;++kind){
                if(i>=group.enabled[kind].size())continue;
                const auto* value=group.enabled[kind][i];
                if(seen[kind].insert(canonical(value->id)).second)
                    append({provider,kind==0?value->id:std::string{},kind==1?value->id:std::string{}});
            }
        }
    }
    return out;
}
struct Axes {double x=0,y=0,z=0;std::uint64_t timestamp100ns=0,receipt=0;};
inline bool finite(const Axes& value) noexcept {
    const double limit=static_cast<double>((std::numeric_limits<float>::max)());
    return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z)&&
        std::abs(value.x)<=limit&&std::abs(value.y)<=limit&&std::abs(value.z)<=limit;
}
struct Series {
    bool requested=false,opened=false,intervalObserved=false,lastReportUsable=false;
    std::uint32_t intervalMs=0;
    std::optional<std::int32_t> readingTransform;
    std::int64_t readingTransformError=0;
    std::int64_t openError=0;
    std::uint64_t callbacks=0,distinct=0,duplicates=0,rejectedNumeric=0,rejectedTime=0,readErrors=0;
    // Last reported device time also includes a bad numeric report. A later
    // read of that same report cannot masquerade as new progressing data.
    std::uint64_t latestReportTimestamp100ns=0,latestReportReceipt=0;
    std::optional<Axes> first,last;
    // Actual standard SensorAPI decoder layout; no WinRT transform is implied.
    std::string standardSdkLayout;
    std::int64_t standardSdkLayoutError=0;
};
struct PairEvidence {Pair ids;std::array<Series,2> parts;};
struct Snapshot {std::vector<PairEvidence> pairs;std::uint64_t started=0,deadline=0;bool accepting=false;};
inline bool ready(const Series& series,std::uint64_t now,std::uint64_t window=1500) noexcept {
    return series.requested&&series.opened&&series.lastReportUsable&&!series.openError&&series.distinct>=2&&series.first&&series.last&&
        series.last->timestamp100ns>series.first->timestamp100ns&&series.last->receipt>=series.first->receipt&&
        now>=series.last->receipt&&now-series.last->receipt<=window;
}
class Collector final {
    mutable std::mutex mutex_;
    Ticket scope_;
    Snapshot values_;
public:
    Collector(Ticket scope,const std::vector<Pair>& pairs,std::uint64_t now,std::uint64_t budget=1500):scope_(std::move(scope)) {
        values_.started=now;values_.deadline=now+budget;values_.accepting=true;
        for(const auto& ids:pairs){if(values_.pairs.size()==8)break;values_.pairs.push_back({ids,{}});
            values_.pairs.back().parts[0].requested=!ids.gyro.empty();values_.pairs.back().parts[1].requested=!ids.accel.empty();}
    }
    void opened(unsigned pair,unsigned kind) {
        std::lock_guard<std::mutex> lock(mutex_);if(pair<values_.pairs.size()&&kind<2)values_.pairs[pair].parts[kind].opened=true;
    }
    void interval(unsigned pair,unsigned kind,std::uint32_t ms) {
        std::lock_guard<std::mutex> lock(mutex_);if(pair<values_.pairs.size()&&kind<2){auto& part=values_.pairs[pair].parts[kind];part.intervalObserved=true;part.intervalMs=ms;}
    }
    void readingFrame(unsigned pair,unsigned kind,std::int32_t value) {
        std::lock_guard<std::mutex> lock(mutex_);if(pair<values_.pairs.size()&&kind<2){auto& part=values_.pairs[pair].parts[kind];part.readingTransform=value;part.readingTransformError=0;}
    }
    void readingFrameError(unsigned pair,unsigned kind,std::int64_t error) {
        std::lock_guard<std::mutex> lock(mutex_);if(pair<values_.pairs.size()&&kind<2){auto& part=values_.pairs[pair].parts[kind];part.readingTransform.reset();part.readingTransformError=error?error:-1;}
    }
    void error(unsigned pair,unsigned kind,std::int64_t code) {
        std::lock_guard<std::mutex> lock(mutex_);if(pair<values_.pairs.size()&&kind<2)values_.pairs[pair].parts[kind].openError=code?code:-1;
    }
    void readError(unsigned pair,unsigned kind) {
        std::lock_guard<std::mutex> lock(mutex_);if(values_.accepting&&pair<values_.pairs.size()&&kind<2)++values_.pairs[pair].parts[kind].readErrors;
    }
    bool observe(unsigned pair,unsigned kind,const Axes& value,const Ticket& current) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!values_.accepting||pair>=values_.pairs.size()||kind>1||!inventory::sameScope(scope_,current)||
            !windows_motion::current(scope_.binding)||!windows_motion::current(current.binding))return false;
        auto& series=values_.pairs[pair].parts[kind];
        if(!series.requested||!series.opened||series.openError||!value.receipt||value.receipt<values_.started||value.receipt>=values_.deadline)return false;
        ++series.callbacks;
        if(!value.timestamp100ns||value.timestamp100ns<series.latestReportTimestamp100ns||value.receipt<series.latestReportReceipt){
            series.lastReportUsable=false;++series.rejectedTime;return false;}
        const bool duplicate=value.timestamp100ns==series.latestReportTimestamp100ns;
        if(!duplicate){series.latestReportTimestamp100ns=value.timestamp100ns;series.latestReportReceipt=value.receipt;}
        if(!finite(value)){series.lastReportUsable=false;++series.rejectedNumeric;return false;}
        if(duplicate){++series.duplicates;return false;}
        if(!series.first)series.first=value;
        series.last=value;series.lastReportUsable=true;++series.distinct;return true;
    }
    bool terminal(std::uint64_t now) const {
        std::lock_guard<std::mutex> lock(mutex_);if(values_.pairs.empty())return true;
        for(const auto& pair:values_.pairs)for(const auto& series:pair.parts)if(series.requested&&!series.openError&&!ready(series,now))return false;
        return true;
    }
    Snapshot snapshot() const {std::lock_guard<std::mutex> lock(mutex_);return values_;}
    void finish() {std::lock_guard<std::mutex> lock(mutex_);values_.accepting=false;}
};
} // namespace ymcc::sensors::probe
