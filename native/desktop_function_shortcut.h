#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "sleep_power_state.h"

namespace ymcc::input::desktopFunction {
// Explicit keyboard source only. These are Windows F13..F24 virtual-key codes;
// the mask is not an OEM identity, capability row or physical-device proof.
inline constexpr std::uint32_t keyBit(unsigned vk) noexcept {
    return vk>=0x7c && vk<=0x87 ? (1u<<(vk-0x7c)) : 0;
}
// System keyboard snapshot; VK codes preserve saved sides and generic aliases.
struct KeyboardKeys {
    std::array<std::uint64_t,4> words{};
    void set(unsigned vk) noexcept {if(vk<256)words[vk/64]|=std::uint64_t(1)<<(vk%64);}
    void clear(unsigned vk) noexcept {if(vk<256)words[vk/64]&=~(std::uint64_t(1)<<(vk%64));}
    bool has(unsigned vk) const noexcept {return vk<256&&(words[vk/64]&(std::uint64_t(1)<<(vk%64)));}
    bool mentions(unsigned vk) const noexcept {return has(vk)||((vk==0xa2||vk==0xa3)&&has(0x11))||
        ((vk==0xa0||vk==0xa1)&&has(0x10))||((vk==0xa4||vk==0xa5)&&has(0x12));}
    bool contains(const KeyboardKeys& other) const noexcept {for(unsigned i=0;i<4;++i)if((words[i]&other.words[i])!=other.words[i])return false;return true;}
    bool intersects(const KeyboardKeys& other) const noexcept {for(unsigned i=0;i<4;++i)if(words[i]&other.words[i])return true;return false;}
    bool hasMouse() const noexcept {return (words[0]&std::uint64_t(0x76))!=0;}
    bool hasKeyboard() const noexcept {return (words[0]&~std::uint64_t(0x76))||words[1]||words[2]||words[3];}
};
struct KeyboardChord {KeyboardKeys keys;bool armed=false;};
struct Witness {
    unsigned vk=0;
    bool keyboard=false, mouse=false, keyboardInput=false;
    std::uint64_t cancellation=0, mouseCancellation=0, keyboardCancellation=0;
    ymcc::sleeppower::SharedPowerV1 power;
};
struct Press {
    bool active=false, blocked=false;
    std::uint32_t epoch=0;
    Witness witness;
};
struct Edge {
    bool keyboard=false;
    KeyboardKeys keyboardKeys;
    std::uint32_t keys=0, vk=0, epoch=0;
    std::uint64_t capturedAt=0;
    bool consumed=false, up=false;
    // Actual hook return: 0 unknown, 1 forwarded, 2 prefix buffered, 3 suppressed.
    // Buffering may later replay; none of these proves an external OEM action stopped.
    std::uint8_t hookDecision=0;
    Witness witness;
    ymcc::sleeppower::SharedPowerV1 power;
};
// The existing hook and WndProc share the UI thread. No heap, worker, borrowed
// handle or retry timer. One posted wake owns the bounded FIFO until drained.
class EdgeQueue {
    std::array<Edge,64> rows_{};
    std::size_t head_=0, size_=0;
public:
    std::size_t size() const noexcept {return size_;}
    template<class Wake> bool publish(const Edge& value,Wake&& wake) {
        if(size_==rows_.size())return false;
        rows_[(head_+size_)%rows_.size()]=value;
        if(!size_ && !wake())return false;
        ++size_;return true;
    }
    // Same UI callback only. Annotate its newly published rows after the real
    // delivery decision, without changing their input/witness or posting a wake.
    void observeHookDecision(std::size_t first,unsigned vk,std::uint64_t capturedAt,
                             bool up,bool suppressed,unsigned char visibility) noexcept {
        if(first>size_)return;
        for(auto i=first;i<size_;++i){
            auto& row=rows_[(head_+i)%rows_.size()];
            if(row.vk!=vk||row.capturedAt!=capturedAt)continue;
            row.up=up;row.consumed=suppressed;
            row.hookDecision=!suppressed?1:visibility==2?2:3;
        }
    }
    void observeHookOutcome(std::size_t first,std::uint64_t capturedAt,bool suppressed) noexcept {
        if(first>size_)return;
        for(auto i=first;i<size_;++i){
            auto& row=rows_[(head_+i)%rows_.size()];
            if(row.capturedAt!=capturedAt)continue;
            row.consumed=suppressed;
            row.hookDecision=!suppressed?1:row.hookDecision==2?2:3;
        }
    }
    bool pop(Edge& value) noexcept {
        if(!size_)return false;
        value=rows_[head_];head_=(head_+1)%rows_.size();--size_;return true;
    }
};
}
