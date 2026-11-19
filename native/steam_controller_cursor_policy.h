#pragma once
#include <cstdint>

namespace ymcc::steamcursor {
// UI-owned per process lifetime. Neither focus changes nor preference toggles
// refund a mouse veto. Only a new process or the shared sleep owner resets it.
struct Budget {
    std::uint64_t started=0,due=0,lastMouse=0;
    bool padSeen=false,mouseStopped=false;
    void reset(std::uint64_t now) noexcept {
        started=lastMouse=now;due=now+5000;padSeen=mouseStopped=false;
    }
    void mouse(std::uint64_t now) noexcept {lastMouse=now;mouseStopped=true;}
    void end() noexcept {mouseStopped=true;}
    void gamepad() noexcept {if(!mouseStopped)padSeen=true;}
    bool check(std::uint64_t now) noexcept {
        if(mouseStopped||now<due)return false;
        // Late delivery consumes one observation, never a burst of catch-up work.
        due=now<started+60000?(now+5000<started+60000?now+5000:started+60000):now+30000;
        return true;
    }
    bool mayHide(std::uint64_t now,bool foreground,bool confirmed,bool enabled,bool ready) const noexcept {
        return enabled&&ready&&foreground&&confirmed&&padSeen&&!mouseStopped&&now>=lastMouse&&now-lastMouse>=3000;
    }
};
// The existing physical sample supplies meaningful changes; connection and
// neutral/heartbeat reports are not evidence of controller operation.
struct PadSample {
    std::uint16_t buttons=0;
    std::int16_t lx=0,ly=0,rx=0,ry=0;
    std::uint8_t lt=0,rt=0;
    bool operator==(const PadSample&) const = default;
};
inline bool padActive(const PadSample& p) noexcept {
    const auto outside=[](int value,int deadzone){return value>deadzone||value< -deadzone;};
    return p.buttons||p.lt>30||p.rt>30||outside(p.lx,7849)||outside(p.ly,7849)||outside(p.rx,8689)||outside(p.ry,8689);
}
} // namespace ymcc::steamcursor
