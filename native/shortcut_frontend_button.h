#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
namespace ymcc {
enum class FrontendButton { Invalid=0, Steam=1, PS=2, Xbox=3, Gamebar=4 };
inline FrontendButton frontendButton(const std::string& name) {
    if(name=="steam")return FrontendButton::Steam;
    if(name=="ps")return FrontendButton::PS;
    if(name=="xbox")return FrontendButton::Xbox;
    if(name=="gamebar")return FrontendButton::Gamebar;
    return FrontendButton::Invalid;
}
inline bool frontendButtonPersona(FrontendButton button,const std::string& persona) {
    if(button==FrontendButton::Steam)return persona=="steamdeck";
    if(button==FrontendButton::PS)return persona=="dualsense-edge" || persona=="dualsense";
    if(button==FrontendButton::Xbox)return persona=="elite" || persona=="xbox360";
    return false;
}
// Feed the existing screenButtons Guide bit, not a second virtual-device writer.
// Idle costs one atomic load on existing frames; no thread/timer/poll is added.
class FrontendButtonPulse {
    std::atomic<bool> active_{false};
    std::mutex mutex_;
    uint64_t epoch_=0, requested_=0, pressed_=0;
    bool began_=false;
public:
    bool pending() const {return active_.load(std::memory_order_acquire);}
    bool request(uint64_t now,uint64_t epoch) {
        if(!epoch)return false;
        std::lock_guard<std::mutex> lock(mutex_);
        // Do not indefinitely extend a held/repeating shortcut into a stuck Guide.
        if(active_.load(std::memory_order_relaxed) && epoch_==epoch &&
           now>=requested_ && now-requested_<750)return false;
        epoch_=epoch;requested_=now;pressed_=0;began_=false;
        active_.store(true,std::memory_order_release);return true;
    }
    void clear() {std::lock_guard<std::mutex> lock(mutex_);active_.store(false,std::memory_order_release);began_=false;}
    unsigned sample(uint64_t now,uint64_t epoch,bool admitted) {
        if(!pending())return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!active_.load(std::memory_order_relaxed))return 0;
        if(epoch!=epoch_ || now<requested_ || now-requested_>=750 ||
           (began_ && (!admitted || now-pressed_>=120))) {
            active_.store(false,std::memory_order_release);return 0;
        }
        // Wait briefly for the existing YMCC hide/focus isolation to finish.
        // This never bypasses the neutral/power/physical-source admission gates.
        if(!admitted)return 0;
        if(!began_){began_=true;pressed_=now;}
        return 1u;
    }
};
}
