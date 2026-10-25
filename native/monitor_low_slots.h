#pragma once
// Read-only working set inside the existing HWiNFO parse. No sensor reads,
// polling, allocation or persistent state; overflow requests the original scan.
#include <array>
#include <cstddef>
#include <cstdint>
namespace ymcc::monitorlow {
class Slots {
public:
    struct Entry { std::uint32_t slot=0; unsigned priority=0; double value=0; };
    static constexpr std::size_t capacity=16;
private:
    std::array<Entry,capacity> entries_{};
    std::size_t size_=0;
    bool overflow_=false;
public:
    void observe(std::uint32_t slot,unsigned priority,double value) {
        if(!priority || overflow_)return;
        for(std::size_t i=0;i<size_;++i)if(entries_[i].slot==slot){
            // Exact presented LOW outranks the legacy label; equal priority
            // retains the last valid row, matching the original second pass.
            if(priority>=entries_[i].priority)entries_[i]={slot,priority,value};
            return;
        }
        if(size_==capacity){overflow_=true;return;}
        entries_[size_++]={slot,priority,value};
    }
    bool overflow()const { return overflow_; }
    const Entry* find(std::uint32_t slot)const {
        for(std::size_t i=0;i<size_;++i)if(entries_[i].slot==slot)return &entries_[i];
        return nullptr;
    }
};
} // namespace ymcc::monitorlow
