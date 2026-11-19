#pragma once
#include <windows.h>
#include <Xinput.h>
#include <array>
#include <cstdint>
#include <span>

namespace ymcc::backgroundui {
// UI acquisition only. No Host/BUS output or saved controller configuration.
// Qualified from Win5's actual XUSB HID descriptor, not the old ROG parser.
struct ValueCap {
    unsigned index=0, usage=0, link=0, bits=0, report=0;
    long minimum=0, maximum=0;
};
struct Descriptor {
    unsigned vendor=0,product=0,page=0,usage=0,reportBytes=0,dataIndices=0;
    unsigned buttonCaps=0,buttonFirst=0,buttonLast=0,buttonIndex=0,buttonEndIndex=0,buttonReport=0,buttonLink=0;
    std::array<ValueCap,6> values{};
    unsigned valueCount=0;
};
inline bool qualified(const Descriptor& d) noexcept {
    if(d.vendor!=0x045e || d.product!=0x028e || d.page!=1 || d.usage!=5 ||
       d.reportBytes!=15 || d.dataIndices!=16 || d.buttonCaps!=1 ||
       d.buttonFirst!=1 || d.buttonLast!=10 || d.buttonIndex!=5 || d.buttonEndIndex!=14 ||
       d.buttonReport!=0 || d.buttonLink!=0 || d.valueCount!=6)return false;
    constexpr unsigned usages[]={0x31,0x30,0x34,0x33,0x32,0x39};
    constexpr unsigned links[]={1,1,2,2,3,0};
    unsigned seen=0;
    for(const auto& c:d.values) {
        const unsigned n=c.index==15?5:c.index;
        if(n>=6 || (seen&(1u<<n)) || c.usage!=usages[n] || c.link!=links[n] || c.report!=0)return false;
        if(n==5) {if(c.bits!=4 || c.minimum!=1 || c.maximum!=8)return false;}
        else if(c.bits!=16 || c.minimum!=0 || (c.maximum!=-1 && c.maximum!=65535))return false;
        seen|=1u<<n;
    }
    return seen==63;
}
struct Datum { unsigned index=0,value=0; bool down=false; };
inline bool decode(std::span<const Datum> data,XINPUT_GAMEPAD& out) noexcept {
    if(data.size()>16)return false;
    XINPUT_GAMEPAD p{};
    unsigned seen=0,hat=0;
    constexpr WORD buttons[]={XINPUT_GAMEPAD_A,XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_X,XINPUT_GAMEPAD_Y,
        XINPUT_GAMEPAD_LEFT_SHOULDER,XINPUT_GAMEPAD_RIGHT_SHOULDER,XINPUT_GAMEPAD_BACK,
        XINPUT_GAMEPAD_START,XINPUT_GAMEPAD_LEFT_THUMB,XINPUT_GAMEPAD_RIGHT_THUMB};
    for(const auto& v:data) {
        if(v.index>=16 || (seen&(1u<<v.index)))return false;
        seen|=1u<<v.index;
        if(v.index>=5 && v.index<=14) {if(v.down)p.wButtons|=buttons[v.index-5];continue;}
        if(v.index==15) {if(v.value>8)return false;hat=v.value;continue;}
        if(v.value>65535)return false;
        const int axis=static_cast<int>(v.value)-32768;
        const auto invert=[](int x){return static_cast<SHORT>(x==-32768?32767:-x);};
        switch(v.index) {
        case 0:p.sThumbLY=invert(axis);break;
        case 1:p.sThumbLX=static_cast<SHORT>(axis);break;
        case 2:p.sThumbRY=invert(axis);break;
        case 3:p.sThumbRX=static_cast<SHORT>(axis);break;
        case 4:break; // Combined Z cannot prove independent LT/RT; retain native triggers.
        }
    }
    // Complete single-report layout only. Missing values are not neutral.
    if((seen&0x801f)!=0x801f)return false;
    constexpr WORD hats[]={0,XINPUT_GAMEPAD_DPAD_UP,
        XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_RIGHT,XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_DPAD_DOWN|XINPUT_GAMEPAD_DPAD_RIGHT,XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_DOWN|XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_DPAD_LEFT,
        XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_LEFT};
    p.wButtons|=hats[hat];out=p;return true;
}
inline bool active(const XINPUT_GAMEPAD& p) noexcept {
    return p.wButtons || p.sThumbLX>1000 || p.sThumbLX< -1000 || p.sThumbLY>1000 || p.sThumbLY< -1000 ||
        p.sThumbRX>1000 || p.sThumbRX< -1000 || p.sThumbRY>1000 || p.sThumbRY< -1000;
}
struct Epoch {
    std::uint64_t power=0,topology=0,identity=0,episode=0;
    unsigned slot=4;
    bool operator==(const Epoch&) const = default;
};
// Existing UI thread owns this state. Held HID state is event driven: its
// age alone is not a release. The current connected source/epoch authorizes
// every read; unplug, power/topology/occupant changes retire it immediately.
class Lane {
    Epoch epoch_{};
    XINPUT_GAMEPAD pad_{};
    bool have_=false,armed_=false;
public:
    bool hasSample() const noexcept {return have_;}
    void clear() noexcept {epoch_={};pad_={};have_=armed_=false;}
    void observe(const Epoch& e,const XINPUT_GAMEPAD& p) noexcept {
        if(!have_ || !(epoch_==e))clear();
        epoch_=e;pad_=p;have_=true;
    }
    bool apply(const Epoch& e,bool eligible,const XINPUT_GAMEPAD& native,XINPUT_GAMEPAD& ui) noexcept {
        if(!eligible || !have_ || !(epoch_==e)){clear();return false;}
        // Capture already proves a fresh, post-registration report from the
        // current exact physical source. Do not eat the user's first chord
        // merely because an event-driven device sent no prior neutral report.
        if(!armed_ && active(pad_) && !active(native))armed_=true;
        if(!armed_)return false;
        ui=pad_;
        // Never infer a combined axis's physical left/right trigger polarity.
        ui.bLeftTrigger=native.bLeftTrigger;ui.bRightTrigger=native.bRightTrigger;
        return true;
    }
};
} // namespace ymcc::backgroundui
