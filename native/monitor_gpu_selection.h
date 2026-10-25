#pragma once
// Pure selection inside YMCC's existing HWiNFO parse/worker. No hardware reader,
// enumeration API, timer, file persistence, or owned strings per sensor row.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include "monitor_memory_network.h"
#include "monitor_voltage.h"
namespace ymcc::monitorgpu {
constexpr std::size_t maxDevices = 64;
struct Identity {
    std::uint32_t id=0, instance=0, anonymousIndex=0;
    std::array<char,128> name{};
    bool stable()const { return id!=0 || instance!=0 || name[0]!=0; }
    std::string_view deviceName()const {
        const auto value=monitorTextFieldView(name.data(),name.size());
        const auto prefix=value.find("]: ");
        return prefix==std::string_view::npos?value:value.substr(prefix+3);
    }
    bool same(const Identity& other)const {
        return id==other.id && instance==other.instance && deviceName()==other.deviceName() &&
            (stable() || anonymousIndex==other.anonymousIndex);
    }
    int fallbackRank()const {
        const MonitorFoldedText low(monitorTextFieldView(name.data(),name.size()));
        return (low.find("dgpu")!=std::string::npos?3:0)+(low.find("[#0]")!=std::string::npos?2:0);
    }
};
struct Device {
    Identity key; std::uint32_t sensorIndex=0;
    double power=-1,clock=-1,load=-1,temperature=-1;
    int powerRank=0,loadRank=0,memoryUsedRank=0;
    monitorvoltage::Reading voltage;
    monitormemory::Memory memory;
    int fallbackRank()const{return key.fallbackRank()*10+(memory.used>=0?2:0)+(memory.available>=0?2:0)+(memory.load>=0?1:0);}
};
struct MemoryResult {
    monitormemory::Memory value;
    const char* source="unavailable";
};
// AMD uses same-group D3D dedicated first, shared only when dedicated is
// unavailable. Valid zero is a reading. Generic capacity tuples are not mixed.
inline bool prefersD3DUsage(const Device& device){
    const MonitorFoldedText name(device.key.deviceName());
    return name.find("nvidia")==std::string::npos&&name.find("intel")==std::string::npos&&
        (name.find("amd")!=std::string::npos||name.find("radeon")!=std::string::npos);
}
class Readings {
    // Empty packets must not initialize all 64 possible GPU values. Each
    // occupied prefix slot starts its Device lifetime only on first admission.
    // Only the occupied prefix is observable; a shrinking assignment can leave
    // old trivial values outside it until a later admission replaces the slot.
    static_assert(std::is_trivially_copyable_v<Device> && std::is_trivially_destructible_v<Device>);
    union Slot {
        Device value;
        Slot() noexcept {} // Deliberately leave an unused slot without a value.
    };
    static_assert(std::is_trivially_copyable_v<Slot>);
    std::array<Slot,maxDevices> devices_;
    std::size_t count_=0, lastSlot_=maxDevices;
    std::uint32_t lastIndex_=0;
    enum class Metric { None, Memory, Voltage, Temperature, Power, Clock, Load };
    static Metric classify(const MonitorFoldedText& name) {
        // add() already checked the GPU prefix. Inspect the next two bytes once
        // and compare suffixes, rather than folding that prefix for every alias.
        if(name.asciiAt(3)==' ') {
            const auto tail=name.after(5);
            switch(name.asciiAt(4)) {
            case 'm':
                if(tail=="emory allocated"||tail=="emory available"||tail=="emory usage"||tail=="emory total"||tail=="emory load")return Metric::Memory;
                break;
            case 'c':
                if(tail=="ore voltage"||tail=="ore voltage (vddcr_gfx)"||tail=="ore voltage (vddc)")return Metric::Voltage;
                if(tail=="ore temperature")return Metric::Temperature;
                if(tail=="lock"||tail=="ore clock"||tail=="lock (effective)")return Metric::Clock;
                if(tail=="ore load")return Metric::Load;
                break;
            case 'v':
                if(tail=="oltage")return Metric::Voltage;
                break;
            case 'a':
                if(tail=="sic voltage")return Metric::Voltage;
                break;
            case 't':
                if(tail=="emperature"||tail=="emperature (edge)"||tail=="emperature (diode)")return Metric::Temperature;
                break;
            case 'u':
                if(tail=="tilization")return Metric::Load;
                break;
            case 'd':
                if(tail=="3d usage")return Metric::Load;
                break;
            }
        }
        // Substring-based power aliases may appear in any family. Retain this
        // final fallback and every exclusion; the hint alone never admits data.
        if((name.find("gpu power")!=std::string::npos||name=="gpu asic power"||name=="gpu chip power"||name=="total graphics power") &&
            name.find("limit")==std::string::npos && name.find("rated")==std::string::npos && name.find("maximum")==std::string::npos && name.find("capability")==std::string::npos && name.find("tgp")==std::string::npos && name.find("tbp")==std::string::npos)return Metric::Power;
        return Metric::None;
    }
    static void choose(double& old,int& rank,double value,int nextRank) {
        if(nextRank>rank || (nextRank==rank && value>old)){old=value;rank=nextRank;}
    }
    // Only active GPU evidence rows have a lifetime. Keep the same fixed
    // capacity and one shared immutable allocation; unused slots are never read.
    union EvidenceSlot {
        monitormemory::D3DMemory value;
        EvidenceSlot() noexcept {}
    };
    struct EvidenceRows {
        std::array<EvidenceSlot,maxDevices> slots;
        EvidenceRows() noexcept {}
        EvidenceRows(const EvidenceRows&)=delete;
        EvidenceRows(const EvidenceRows& other,std::size_t count) noexcept {
            for(std::size_t i=0;i<count;++i)set(i,other[i]);
        }
        void set(std::size_t index,monitormemory::D3DMemory value) noexcept {
            ::new(static_cast<void*>(&slots[index].value)) monitormemory::D3DMemory(value);
        }
        const monitormemory::D3DMemory& operator[](std::size_t i)const noexcept{return slots[i].value;}
        const void* data()const noexcept{return slots.data();}
    };
    static_assert(std::is_trivially_copyable_v<EvidenceSlot> && std::is_trivially_destructible_v<EvidenceSlot>);
    static_assert(sizeof(EvidenceSlot)==sizeof(monitormemory::D3DMemory));
    static_assert(sizeof(EvidenceRows)==maxDevices*sizeof(monitormemory::D3DMemory));
    static_assert(std::is_trivially_copyable_v<monitormemory::D3DMemory> && std::is_trivially_destructible_v<monitormemory::D3DMemory>);
    std::shared_ptr<const EvidenceRows> memoryEvidence_;
    void copyActiveValues(const Readings& other) noexcept {
        // Union assignment starts the corresponding active member lifetime.
        // Keep the full-capacity case as one existing array assignment.
        if(other.count_==maxDevices)devices_=other.devices_;
        else for(std::size_t i=0;i<other.count_;++i)devices_[i]=other.devices_[i];
        count_=other.count_;lastSlot_=other.lastSlot_;lastIndex_=other.lastIndex_;
    }
public:
    // User-provided: value-initializing Readings{} must also leave slots empty.
    Readings() noexcept {}
    Readings(const Readings& other) noexcept : memoryEvidence_(other.memoryEvidence_) {
        copyActiveValues(other);
    }
    Readings& operator=(const Readings& other) noexcept {
        if(this!=&other){copyActiveValues(other);memoryEvidence_=other.memoryEvidence_;}
        return *this;
    }
    Readings(Readings&& other) noexcept : memoryEvidence_(std::move(other.memoryEvidence_)) {
        copyActiveValues(other);
    }
    Readings& operator=(Readings&& other) noexcept {
        if(this!=&other){copyActiveValues(other);memoryEvidence_=std::move(other.memoryEvidence_);}
        return *this;
    }
    std::size_t size()const{return count_;}
    const Device& at(std::size_t i)const{return devices_[i].value;}
    template<class Evidence> void retainMemoryEvidence(Evidence evidence)noexcept{
        if(!count_)return;
        try{
            using Rows=EvidenceRows;
            Rows rows;
            for(std::size_t i=0;i<count_;++i)rows.set(i,evidence(devices_[i].value.sensorIndex));
            // One immutable value-only slot per existing parser thread. Identity
            // and time remain owned by this packet; an equal numeric payload
            // cannot renew an observation or transfer a device identity.
            static thread_local std::shared_ptr<const Rows> cached;
            static thread_local std::size_t cachedCount=0;
            static_assert(sizeof(monitormemory::D3DMemory)==2*sizeof(double));
            if(cached&&cachedCount==count_&&std::memcmp(rows.data(),cached->data(),count_*sizeof(monitormemory::D3DMemory))==0){
                memoryEvidence_=cached;return;
            }
            auto owned=std::make_shared<const Rows>(rows,count_);
            cached=owned;cachedCount=count_;memoryEvidence_=std::move(owned);
        }catch(...){memoryEvidence_.reset();} // Optional data cannot break collection.
    }
    bool hasMemoryEvidence()const{return bool(memoryEvidence_);}
    monitormemory::D3DMemory diagnosticD3D(const Device* device,bool* captured=nullptr)const{
        if(captured)*captured=false;
        if(memoryEvidence_&&device)for(std::size_t i=0;i<count_;++i)if(&devices_[i].value==device){if(captured)*captured=true;return (*memoryEvidence_)[i];}
        return {};
    }
    bool needsD3DUsage()const{
        for(std::size_t i=0;i<count_;++i)if(monitorgpu::prefersD3DUsage(devices_[i].value))return true;
        return false;
    }
    MemoryResult memoryFor(const Device* device)const{
        if(!device)return {};
        if(monitorgpu::prefersD3DUsage(*device)){
            bool captured=false;const auto d3d=diagnosticD3D(device,&captured);
            if(captured&&monitormemory::amount(d3d.dedicated))
                return {{d3d.dedicated,-1,-1,-1},"hwinfo-d3d-dedicated"};
            if(captured&&monitormemory::amount(d3d.dynamic))
                return {{d3d.dynamic,-1,-1,-1},"hwinfo-d3d-shared"};
            const auto raw=monitormemory::normalize(device->memory);
            if(raw.total<0&&raw.load<0)return {}; // No valid D3D or coherent tuple.
        }
        const auto memory=monitormemory::normalize(device->memory);
        return {memory,(memory.used>=0||memory.total>=0||memory.available>=0||memory.load>=0)?"hwinfo":"unavailable"};
    }
    const Device* find(const Identity& key)const {
        for(std::size_t i=0;i<count_;++i)if(devices_[i].value.key.same(key))return &devices_[i].value;
        return nullptr;
    }
    const Device* best()const {
        const Device* best=nullptr;
        for(std::size_t i=0;i<count_;++i){const auto& d=devices_[i].value;
            if(!best || d.power>best->power || (d.power==best->power && d.fallbackRank()>best->fallbackRank()))best=&d;
        }
        return best;
    }
    template<class Resolve> bool add(std::uint32_t index,const MonitorFoldedText& name,
        const MonitorFoldedText& unit,double value,bool readable,Resolve resolve) {
        // Most rows are CPU/system/network; reject them with a 1-char prefix
        // mismatch before any GPU classification or candidate lookup.
        if(!name.startsWith("gpu") && name!="total graphics power")return false;
        const auto metric=classify(name);
        if(metric==Metric::None)return false;
        if(!readable||!std::isfinite(value))return true;
        double admitted=-1; int priority=1;
        if(metric==Metric::Memory){admitted=unit=="%"?((name=="gpu memory usage"||name=="gpu memory load")&&monitormemory::percent(value)?value:-1):monitormemory::toMb(unit,value);}
        else if(metric==Metric::Voltage){admitted=unit=="mv"?value/1000.0:unit=="v"?value:-1;if(!monitorvoltage::valid(admitted))admitted=-1;priority=name=="gpu voltage"?1:2;}
        else if(metric==Metric::Temperature){if((unit=="c"||unit=="\xc2\xb0" "c"||unit=="\xb0" "c"||unit=="\xe2\x84\x83"||unit=="\xa1\xe6")&&value>=0&&value<=150)admitted=value;}
        else if(metric==Metric::Power){if((unit=="w"||unit.find("watt")!=std::string::npos)&&value>=0&&value<2000)admitted=value;priority=(name=="gpu power"||name=="total graphics power")?4:name=="gpu asic power"?2:name=="gpu chip power"?1:3;}
        else if(metric==Metric::Clock){if(unit=="mhz"&&value>=0&&value<=10000)admitted=value;}
        else if(metric==Metric::Load){if(unit=="%"&&monitormemory::percent(value))admitted=value;priority=name=="gpu core load"?3:name=="gpu utilization"?2:1;}
        if(admitted<0)return true;
        std::size_t slot=maxDevices;
        if(lastSlot_<count_&&lastIndex_==index)slot=lastSlot_;
        else for(std::size_t i=0;i<count_;++i)if(devices_[i].value.sensorIndex==index){slot=i;break;}
        if(slot==maxDevices){
            if(count_==maxDevices)return true;
            slot=count_;
            auto* added=::new(static_cast<void*>(&devices_[slot].value)) Device{};
            added->sensorIndex=index;added->key=resolve(index);++count_;
        }
        lastSlot_=slot;lastIndex_=index;auto& d=devices_[slot].value;
        if(metric==Metric::Memory){if(unit=="%")d.memory.load=admitted;
            else if(name=="gpu memory allocated"||name=="gpu memory usage"){const int rank=name=="gpu memory allocated"?2:1;if(rank>=d.memoryUsedRank){d.memory.used=admitted;d.memoryUsedRank=rank;}}
            else if(name=="gpu memory available")d.memory.available=admitted;
            else if(name=="gpu memory total")d.memory.total=admitted;
        }else if(metric==Metric::Voltage)monitorvoltage::Readings::select(d.voltage,admitted,priority);
        else if(metric==Metric::Temperature)d.temperature=(std::max)(d.temperature,admitted);
        else if(metric==Metric::Power)choose(d.power,d.powerRank,admitted,priority);
        else if(metric==Metric::Clock)d.clock=(std::max)(d.clock,admitted);
        else if(metric==Metric::Load)choose(d.load,d.loadRank,admitted,priority);
        return true;
    }
};
// Process-lifetime identity and observed CURRENT-power high-water mark only.
// Do not cache old readings. Missing/unplugged winner => unknown, not lower card.
// A higher observation can promote a new device; idling never demotes it.
class StickySelection {
    Identity selected_{}; bool selectedValid_=false; double peak_=-1;
public:
    bool pinned()const{return selectedValid_;}
    double peak()const{return peak_;}
    const Device* select(const Readings& readings,bool fresh) {
        const auto* best=readings.best();
        if(fresh){
            if(!selectedValid_ && best && best->key.stable()){selected_=best->key;selectedValid_=true;peak_=best->power;}
            // Only fresh, identified rows affect the session latch. No HWiNFO
            // historical maximum/average, old frames, or unstable retries.
            for(std::size_t i=0;i<readings.size();++i){const auto& d=readings.at(i);
                if(d.key.stable() && d.power>=0 && d.power>peak_){selected_=d.key;selectedValid_=true;peak_=d.power;}
            }
        }
        return selectedValid_?readings.find(selected_):(fresh?best:nullptr);
    }
};
} // namespace ymcc::monitorgpu
