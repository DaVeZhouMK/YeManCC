#pragma once
// Existing HWiNFO row consumer only: fixed storage, no readers/timers/IO.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <type_traits>
#include "monitor_text_fold.h"
namespace ymcc::monitormemory {
constexpr std::size_t maxGroups = 256;
inline bool amount(double v) { return std::isfinite(v) && v >= 0 && v <= 1e9; }
inline bool percent(double v) { return std::isfinite(v) && v >= 0 && v <= 100; }
inline double toMb(const MonitorFoldedText& unit, double v) {
    if (!std::isfinite(v) || v < 0) return -1;
    double result = -1;
    if (unit == "mb" || unit == "mib") result = v;
    else if (unit == "gb" || unit == "gib") result = v * 1024;
    else if (unit == "kb" || unit == "kib") result = v / 1024;
    else if (unit == "b" || unit == "byte" || unit == "bytes") result = v / 1048576;
    return amount(result) ? result : -1;
}
inline double toMBps(const MonitorFoldedText& unit, double v, std::string_view rawUnit = {}) {
    if (!std::isfinite(v) || v < 0) return -1;
    // Mb/s is bits, MB/s is bytes; folding must not erase that distinction.
    if(rawUnit.size()>=4&&rawUnit[1]=='b'&&(rawUnit[0]=='K'||rawUnit[0]=='M'||rawUnit[0]=='G'))return -1;
    double result = -1;
    // Match RTSS template: HWiNFO Current DL/UP KB/s divided by 1000.
    if (unit == "kb/s") result = v / 1000;
    else if (unit == "mb/s") result = v;
    else if (unit == "gb/s") result = v * 1000;
    else if (unit == "b/s" || unit == "bytes/s") result = v / 1e6;
    else if (unit == "kib/s") result = v * 1024 / 1e6;
    else if (unit == "mib/s") result = v * 1048576 / 1e6;
    return std::isfinite(result) && result >= 0 && result <= 1e6 ? result : -1;
}
struct Memory { double used=-1,available=-1,total=-1,load=-1; };
inline Memory normalize(Memory v) {
    if(!amount(v.used))v.used=-1;
    if(!amount(v.available))v.available=-1;
    if(!amount(v.total))v.total=-1;
    if(!percent(v.load))v.load=-1;
    if(v.used>=0&&v.total>=0&&v.used>v.total){v.used=-1;v.available=-1;v.load=-1;return v;}
    if(v.available>=0&&v.total>=0&&v.available>v.total){v.used=-1;v.available=-1;v.load=-1;return v;}
    if(v.used>=0&&v.available>=0){const double sum=v.used+v.available;
        if(!amount(sum)||(v.total>=0&&std::abs(sum-v.total)>(std::max)(1.0,v.total*.001))){v.used=-1;v.available=-1;v.load=-1;return v;}
        if(v.total<0)v.total=sum;
    }else if(v.total>=0&&v.used>=0)v.available=v.total-v.used;
    else if(v.total>=0&&v.available>=0)v.used=v.total-v.available;
    if(v.used>=0&&v.total>0){const double derived=100*v.used/v.total;
        // Preserve direct-load rounding, reject a contradictory percentage.
        if(v.load<0||std::abs(v.load-derived)>1)v.load=derived;
    }
    return v;
}
// Same-pass D3D used counters for partial AMD data and diagnostics.
// Neither dedicated nor dynamic usage substitutes physical VRAM capacity.
struct D3DMemory { double dedicated=-1,dynamic=-1; };
// Bounded by distinct groups, not the numerical HWiNFO sensor index.
// Only live entries are initialized; collector copies are deliberately disabled.
class D3DRows {
    struct Entry {std::uint32_t group;double dedicated,dynamic;};
    std::array<Entry,maxGroups> entries_;
    std::size_t count_=0,last_=0;
    bool overflow_=false;
public:
    static constexpr std::uint32_t sensorLimit=4096;
    D3DRows()=default;
    D3DRows(const D3DRows&)=delete;
    D3DRows& operator=(const D3DRows&)=delete;
    bool overflow()const{return overflow_;}
    void add(std::uint32_t group,bool dedicated,double mb){
        if(group>=sensorLimit||!amount(mb))return;
        std::size_t slot=count_;
        if(last_<count_&&entries_[last_].group==group)slot=last_;
        else for(std::size_t i=0;i<count_;++i)if(entries_[i].group==group){slot=i;break;}
        if(slot==count_){if(count_==maxGroups){overflow_=true;return;}entries_[count_++]={group,-1,-1};}
        last_=slot;if(dedicated)entries_[slot].dedicated=mb;else entries_[slot].dynamic=mb;
    }
    D3DMemory get(std::uint32_t group)const{
        for(std::size_t i=0;i<count_;++i)if(entries_[i].group==group)return {entries_[i].dedicated,entries_[i].dynamic};
        return {};
    }
};

// Explicit owner ordering preserves the former ascending-index selection even
// when original rows arrive interleaved or a sparse overflow needs replay.
class GroupSelection {
    Memory ram_,gpu_;
    double down_=-1,up_=-1,networkBest_=-1;
    std::uint32_t ramOwner_=UINT32_MAX,gpuOwner_=UINT32_MAX,networkOwner_=UINT32_MAX;
    int gpuRank_=-1;
public:
    template<class RawGroup,class Priority> void consider(const RawGroup& g,Priority priority){
        if(g.ram.used>=0||g.ram.available>=0||g.ram.load>=0||g.ram.total>=0){
            if(g.index<ramOwner_){ram_=normalize(g.ram);ramOwner_=g.index;}
        }
        if(g.gpu.used>=0||g.gpu.available>=0||g.gpu.load>=0||g.gpu.total>=0){
            const int rank=priority(g.index)*10+(g.gpu.used>=0?2:0)+(g.gpu.available>=0?2:0)+(g.gpu.load>=0?1:0);
            if(rank>gpuRank_||(rank==gpuRank_&&g.index<gpuOwner_)){gpu_=normalize(g.gpu);gpuRank_=rank;gpuOwner_=g.index;}
        }
        // Both directions always come from one active adapter. Lower index
        // wins exact traffic ties, including idle zero; never sum duplicates.
        const double traffic=(g.down>=0?g.down:0)+(g.up>=0?g.up:0);
        if((g.down>=0||g.up>=0)&&(traffic>networkBest_||(traffic==networkBest_&&g.index<networkOwner_))){
            networkBest_=traffic;networkOwner_=g.index;down_=g.down;up_=g.up;
        }
    }
    void publish(Memory& ram,Memory& gpu,double& down,double& up)const{
        if(ramOwner_!=UINT32_MAX)ram=ram_;
        if(gpuOwner_!=UINT32_MAX)gpu=gpu_;
        if(networkOwner_!=UINT32_MAX){down=down_;up=up_;}
    }
};
// Trivial storage: only inserted rows are initialized. No per-packet clearing
// of 256 complete RAM/GPU/network tuples, and no identity/time cache.
struct StoredMemory {
    double used,available,total,load;
    operator Memory()const{return {used,available,total,load};}
};
struct Group {std::uint32_t index;StoredMemory ram,gpu;double down,up;int gpuUsedPriority;};
static_assert(std::is_trivially_default_constructible_v<Group>&&std::is_trivially_copyable_v<Group>);
class Readings {
    std::array<Group,maxGroups> groups_;
    static constexpr std::size_t lookupCapacity=maxGroups*2;
    std::array<std::uint16_t,lookupCapacity> lookupSlots_;
    std::array<std::uint64_t,lookupCapacity/64> lookupOccupied_{};
    std::size_t count_=0,last_=0;
    std::uint32_t windows_=0;
    bool groupsOverflow_=false;
    D3DRows diagnosticRows_;
    bool collectD3D_=false;
    std::uint32_t sourceGroups_=D3DRows::sensorLimit;
    Group* findOrInsert(std::uint32_t group){
        if(last_<count_&&groups_[last_].index==group)return &groups_[last_];
        std::size_t bucket=std::size_t(group*UINT32_C(0x9e3779b1))&(lookupCapacity-1);
        // Fixed 50%-full maximum table. Only 64 occupancy bytes are cleared;
        // slot/value storage is read only after its occupied bit is set.
        for(std::size_t probes=0;probes<lookupCapacity;++probes){
            auto& occupied=lookupOccupied_[bucket/64];const auto bit=std::uint64_t(1)<<(bucket%64);
            if(!(occupied&bit)){
                if(count_==maxGroups){groupsOverflow_=true;return nullptr;}
                const auto slot=count_;
                groups_[count_++]={group,{-1,-1,-1,-1},{-1,-1,-1,-1},-1,-1,0};
                lookupSlots_[bucket]=static_cast<std::uint16_t>(slot);occupied|=bit;last_=slot;return &groups_[slot];
            }
            const auto slot=lookupSlots_[bucket];
            if(groups_[slot].index==group){last_=slot;return &groups_[slot];}
            bucket=(bucket+1)&(lookupCapacity-1);
        }
        groupsOverflow_=true;return nullptr;
    }
public:
    static constexpr std::uint32_t sensorLimit=D3DRows::sensorLimit;
    explicit Readings(bool diagnostic=false,std::uint64_t sourceGroups=sensorLimit):collectD3D_(diagnostic),sourceGroups_(static_cast<std::uint32_t>((std::min)(sourceGroups,std::uint64_t(sensorLimit)))){}
    bool add(std::uint32_t group,const MonitorFoldedText& name,const MonitorFoldedText& unit,double value,bool valid,std::string_view rawUnit={},bool includeD3D=true){
        // Route by the existing disjoint name families before exact matches.
        // CPU/core/drive rows never need the RAM/GPU/network comparison lists.
        bool ram=false,gpu=false,net=false,d3d=false;
        if(name.startsWith("physical ")){
            ram=name=="physical memory used"||name=="physical memory available"||name=="physical memory load"||name=="physical memory total";
        }else if(name.startsWith("gpu ")){
            gpu=name=="gpu memory allocated"||name=="gpu memory available"||name=="gpu memory usage"||name=="gpu memory total"||name=="gpu memory load";
            d3d=includeD3D&&collectD3D_&&(name=="gpu d3d memory dedicated"||name=="gpu d3d memory dynamic");
        }else if(name.startsWith("current ")){
            net=name=="current dl rate"||name=="current up rate";
        }
        if(!ram&&!gpu&&!net&&!d3d)return false;
        if(group>=sourceGroups_)return true;
        if(d3d){if(valid)diagnosticRows_.add(group,name=="gpu d3d memory dedicated",toMb(unit,value));return true;}
        if(group>=sensorLimit||!valid)return true;
        const bool load=unit=="%";
        const bool validLoad=name=="physical memory load"||name=="gpu memory usage"||name=="gpu memory load";
        if(!load&&(name=="physical memory load"||name=="gpu memory load"))return true;
        const double admitted=net?toMBps(unit,value,rawUnit):load?(validLoad&&percent(value)?value:-1):toMb(unit,value);
        if(admitted<0)return true;
        windows_|=std::uint32_t(1)<<(group/maxGroups);
        auto* found=findOrInsert(group);if(!found)return true;auto& g=*found;
        if(net){if(name=="current dl rate")g.down=admitted;else g.up=admitted;return true;}
        auto& m=ram?g.ram:g.gpu;
        if(load){m.load=admitted;return true;}
        if(name=="physical memory used")m.used=admitted;
        else if(name=="gpu memory allocated"||name=="gpu memory usage"){const int rank=name=="gpu memory allocated"?2:1;if(rank>=g.gpuUsedPriority){m.used=admitted;g.gpuUsedPriority=rank;}}
        else if(name=="physical memory available"||name=="gpu memory available")m.available=admitted;
        else if(name=="physical memory total"||name=="gpu memory total")m.total=admitted;
        return true;
    }
    D3DMemory diagnosticD3D(std::uint32_t group)const{return diagnosticRows_.get(group);}
    bool diagnosticOverflow()const{return diagnosticRows_.overflow();}
    bool groupsOverflow()const{return groupsOverflow_;}
    std::uint32_t groupWindows()const{return windows_;}
    void resetGroups(){count_=0;last_=0;groupsOverflow_=false;windows_=0;lookupOccupied_.fill(0);}
    template<class Priority> void appendTo(GroupSelection& selected,Priority priority)const{
        for(std::size_t i=0;i<count_;++i)selected.consider(groups_[i],priority);
    }
    template<class Priority> void finish(Memory& ram,Memory& gpu,double& down,double& up,Priority priority)const{
        GroupSelection selected;appendTo(selected,priority);selected.publish(ram,gpu,down,up);
    }
};
}
