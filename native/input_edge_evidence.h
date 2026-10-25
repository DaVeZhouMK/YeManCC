#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

// Fixed memory only. No Win32 calls, allocation, text decoding or file I/O.
namespace ymcc::inputEdges {
enum class Channel : std::uint8_t { HookKeyboard, RawKeyboard, HookMouse, RawMouse, SelfSend };
struct Event {
    Channel channel{};
    std::uint64_t tick=0, sequence=0, powerGeneration=0, deviceGeneration=0;
    std::uintptr_t device=0, extraInfo=0;
    std::uint32_t code=0, scan=0, flags=0, message=0, eventTime=0, source=0;
    std::uint32_t requested=0, returned=0, inputIndex=0;
    std::int32_t wheel=0;
    bool down=false;
};
inline constexpr std::size_t capacity=256;
struct Snapshot {
    std::array<Event,capacity> events{};
    std::size_t count=0;
    std::uint64_t epoch=0, repeats=0, overwritten=0, contention=0, discardedOnConfig=0;
    bool enabled=false, drained=false;
};
class Recorder {
    struct Key {bool known=false,down=false;std::uintptr_t device=0;std::uint32_t scan=0,flags=0;};
    std::mutex mutex_;
    std::array<Event,capacity> events_{};
    std::array<Key,1024> keys_{};
    std::size_t begin_=0,count_=0;
    std::uint64_t sequence_=0,repeats_=0,overwritten_=0,discarded_=0;
    std::atomic<std::uint64_t> state_{0},contention_{0};
public:
    bool enabled() const noexcept {return (state_.load(std::memory_order_acquire)&1)!=0;}
    bool enable(bool wanted) noexcept {
        if(enabled()==wanted)return true;
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock){contention_.fetch_add(1,std::memory_order_relaxed);return false;}
        const auto old=state_.load(std::memory_order_relaxed);
        if(bool(old&1)==wanted)return true;
        discarded_+=count_;begin_=count_=0;keys_={};repeats_=overwritten_=0;
        state_.store(((old>>1)+1)*2+(wanted?1:0),std::memory_order_release);return true;
    }
    void record(Event event) noexcept {
        const auto epoch=state_.load(std::memory_order_acquire);if(!(epoch&1))return;
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock){contention_.fetch_add(1,std::memory_order_relaxed);return;}
        if(state_.load(std::memory_order_relaxed)!=epoch){++discarded_;return;}
        if((event.channel==Channel::HookKeyboard||event.channel==Channel::RawKeyboard)&&event.code<256){
            const auto extended=event.channel==Channel::HookKeyboard ? (event.flags&1u)!=0 : (event.flags&6u)!=0;
            auto& key=keys_[(event.channel==Channel::RawKeyboard?512:0)+(extended?256:0)+event.code];
            if(event.down&&key.known&&key.down&&key.device==event.device&&key.scan==event.scan&&key.flags==event.flags){++repeats_;return;}
            key={true,event.down,event.device,event.scan,event.flags};
        }
        event.sequence=++sequence_;
        if(count_==capacity){begin_=(begin_+1)%capacity;--count_;++overwritten_;}
        events_[(begin_+count_)%capacity]=event;++count_;
    }
    Snapshot take() noexcept {
        Snapshot out{};std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock){contention_.fetch_add(1,std::memory_order_relaxed);return out;}
        const auto state=state_.load(std::memory_order_relaxed);out.enabled=(state&1)!=0;out.epoch=state>>1;out.drained=true;
        out.count=count_;for(std::size_t i=0;i<count_;++i)out.events[i]=events_[(begin_+i)%capacity];
        out.repeats=repeats_;out.overwritten=overwritten_;out.contention=contention_.exchange(0,std::memory_order_relaxed);
        out.discardedOnConfig=discarded_;begin_=count_=0;repeats_=overwritten_=discarded_=0;return out;
    }
};
}