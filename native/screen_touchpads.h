#pragma once
// Event-driven screen touchpads. No worker, periodic timer, hook or WebView.
// UI thread owns HWND/SendInput; the existing BUS reads one bounded POD snapshot.
#include <windows.h>
#include <windowsx.h>
#include <array>
#include <atomic>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <string>
#include <fstream>
#include <filesystem>
#include <vector>
#include <chrono>
#include "json.hpp"
#include "screen_control_glyphs.h"

namespace ymcc::screenpads {
using Json = nlohmann::json;
inline constexpr UINT kRefreshMessage = WM_APP + 172;
inline constexpr UINT kSummonMessage = WM_APP + 173;
inline constexpr ULONG_PTR kInjectedTag = 0x594d5450;
enum class Mode { Deck, DualSense, Wasd, Arrows, Mouse, Off };
enum class SummonPosition { Off, Left, Right };
enum class Layout { Dual, Single };
inline bool nativePad(Mode m) {return m==Mode::Deck || m==Mode::DualSense;}
inline const char* modeName(Mode m) {
    switch (m) { case Mode::Deck: return "steamdeck"; case Mode::DualSense: return "dualsense"; case Mode::Wasd: return "wasd";
    case Mode::Arrows: return "arrows"; case Mode::Mouse: return "mouse"; default: return "off"; }
}
inline bool parseMode(const std::string& s, bool left, Mode& m) {
    if (s == "steamdeck") m=Mode::Deck; else if (s == "dualsense") m=Mode::DualSense; else if (s == "mouse") m=Mode::Mouse;
    else if (s == "off") m=Mode::Off; else if (left && s == "wasd") m=Mode::Wasd;
    else if (left && s == "arrows") m=Mode::Arrows; else return false;
    return true;
}
struct Config { bool enabled=false; Mode left=Mode::Deck, right=Mode::Deck; int transparency=80; int mouseSensitivity=100; int scale=100; Layout layout=Layout::Dual; Mode single=Mode::Mouse; SummonPosition summonPosition=SummonPosition::Off; unsigned specialMask=0,rearMask=0; bool summonEnabled=false,specialEnabled=false,rearEnabled=false; };
inline const char* summonPositionName(SummonPosition p) {return p==SummonPosition::Left?"left":p==SummonPosition::Right?"right":"off";}
inline bool parseSummonPosition(const std::string& s,SummonPosition& p) {if(s=="off")p=SummonPosition::Off;else if(s=="left")p=SummonPosition::Left;else if(s=="right")p=SummonPosition::Right;else return false;return true;}
inline SummonPosition effectiveSummonPosition(const Config& c) {return c.summonPosition!=SummonPosition::Off?c.summonPosition:c.summonEnabled?SummonPosition::Right:SummonPosition::Off;}
inline unsigned effectiveSpecialMask(const Config& c) {return c.specialMask?c.specialMask:(c.specialEnabled?3u:0u);}
inline unsigned effectiveRearMask(const Config& c) {return c.rearMask?c.rearMask:(c.rearEnabled?15u:0u);}
inline void buttonsRelease();
inline void buttonsRefresh(const Config&,const RECT&,const RECT&);
inline void buttonsInitialize();
inline void buttonsShutdown();
inline void buttonsRepaint(int);
inline bool buttonsVisible();
inline unsigned buttonSnapshot();
inline Json configJson(const Config& c) {
    const auto summon=effectiveSummonPosition(c);const unsigned special=effectiveSpecialMask(c),rear=effectiveRearMask(c);
    return {{"summonPosition",summonPositionName(summon)},{"specialMask",special},{"rearMask",rear},
        {"summonEnabled",summon!=SummonPosition::Off},{"specialEnabled",special!=0},{"rearEnabled",rear!=0},
        {"enabled",c.enabled},{"leftMode",modeName(c.left)},{"rightMode",modeName(c.right)},
        {"transparency",c.transparency},{"mouseSensitivity",c.mouseSensitivity},{"scale",c.scale},
        {"layout",!c.enabled?"off":c.layout==Layout::Single?"single":"dual"},{"singleMode",modeName(c.single)}};
}
inline bool parseConfig(const Json& j, const Config& old, Config& c) {
    Config next=old;bool legacySummon=false,legacySpecial=false,legacyRear=false,hasSummon=false,hasSpecial=false,hasRear=false;
    if(!j.is_object())return false;
    for(const auto& [key,v]:j.items()) {
        if(key=="enabled") {if(!v.is_boolean())return false;next.enabled=v.get<bool>();}
        else if(key=="summonEnabled" || key=="specialEnabled" || key=="rearEnabled") {
            if(!v.is_boolean())return false;const bool value=v.get<bool>();
            if(key=="summonEnabled"){legacySummon=true;next.summonEnabled=value;}
            else if(key=="specialEnabled"){legacySpecial=true;next.specialEnabled=value;}
            else {legacyRear=true;next.rearEnabled=value;}
        } else if(key=="summonPosition") {
            if(!v.is_string() || !parseSummonPosition(v.get<std::string>(),next.summonPosition))return false;hasSummon=true;
        } else if(key=="specialMask") {
            if(!v.is_number_integer())return false;const auto n=v.get<long long>();if(n<0||n>3)return false;next.specialMask=(unsigned)n;hasSpecial=true;
        } else if(key=="rearMask") {
            if(!v.is_number_integer())return false;const auto n=v.get<long long>();if(n<0||n>15)return false;next.rearMask=(unsigned)n;hasRear=true;
        } else if(key=="layout") {
            if(!v.is_string())return false;const auto s=v.get<std::string>();if(s=="off")next.enabled=false;else if(s=="dual"||s=="single"){next.enabled=true;next.layout=s=="single"?Layout::Single:Layout::Dual;}else return false;
        } else if(key=="singleMode") {
            if(!v.is_string())return false;Mode m;if(!parseMode(v.get<std::string>(),true,m)||m==Mode::Deck||m==Mode::Off)return false;next.single=m;
        } else if(key=="leftMode"||key=="rightMode") {
            if(!v.is_string())return false;Mode m;if(!parseMode(v.get<std::string>(),key=="leftMode",m))return false;(key=="leftMode"?next.left:next.right)=m;
        } else if(key=="transparency"||key=="mouseSensitivity"||key=="scale") {
            if(!v.is_number_integer())return false;const auto n=v.get<long long>();
            if(key=="transparency"){if(n<0||n>100)return false;next.transparency=(int)n;}
            else if(key=="mouseSensitivity"){if(n<10||n>300)return false;next.mouseSensitivity=(int)n;}
            else {if(n<50||n>200||n%5!=0)return false;next.scale=(int)n;}
        } else return false;
    }
    if(!hasSummon && legacySummon){if(next.summonEnabled){if(next.summonPosition==SummonPosition::Off)next.summonPosition=SummonPosition::Right;}else next.summonPosition=SummonPosition::Off;}
    if(!hasSpecial && legacySpecial){if(next.specialEnabled && next.specialMask==0)next.specialMask=3;else if(!next.specialEnabled)next.specialMask=0;}
    if(!hasRear && legacyRear){if(next.rearEnabled && next.rearMask==0)next.rearMask=15;else if(!next.rearEnabled)next.rearMask=0;}
    if(hasSummon)next.summonEnabled=next.summonPosition!=SummonPosition::Off;
    if(hasSpecial)next.specialEnabled=next.specialMask!=0;
    if(hasRear)next.rearEnabled=next.rearMask!=0;
    if(j.contains("layout")&&j.contains("enabled")&&j["enabled"].is_boolean()&&j["enabled"].get<bool>()!=next.enabled)return false;
    c=next;return true;
}
inline int profileIndex(const std::string& persona) {
    if(persona=="disabled") return 0;if(persona=="steamdeck") return 1;
    if(persona=="dualsense-edge" || persona=="dualsense" || persona=="dualshock4") return 2;
    if(persona=="elite" || persona=="xbox360") return 3;return -1;
}
inline const char* profileName(int index) {
    constexpr const char* names[]={"disabled","steamdeck","dualsense-edge","elite"};
    return names[std::clamp(index,0,3)];
}
inline Config profileDefault(int index) {
    Config c;
    if(index!=1) {c.left=Mode::Wasd;c.right=Mode::Mouse;}
    if(index==1) {c.enabled=true;c.layout=Layout::Dual;}
    if(index==2) {c.enabled=true;c.layout=Layout::Single;c.single=Mode::DualSense;c.left=c.right=Mode::DualSense;}
    if(index>0 && index<4) {c.specialMask=index==3?1u:3u;c.specialEnabled=true;}
    return c;
}
struct ProfileBank {
    std::array<Config,4> slots;
    explicit ProfileBank(const Json& section=Json::object()) {
        for(int i=0;i<4;i++) slots[i]=profileDefault(i);
        const auto readSaved=[](const Json& saved,Config& slot) {
            Config seed=slot;
            // Before layouts existed, a saved profile was dual (or disabled).
            // Do not reinterpret old PS5 settings as the new single-pad preset.
            if(saved.is_object() && !saved.contains("layout")) {seed.enabled=false;seed.layout=Layout::Dual;}
            parseConfig(saved,seed,slot);
        };
        if(section.is_object() && section.contains("profiles") && section["profiles"].is_object()) {
            for(int i=0;i<4;i++) if(section["profiles"].contains(profileName(i)))
                readSaved(section["profiles"][profileName(i)],slots[i]);
        } else if(section.is_object() && !section.empty()) readSaved(section,slots[1]); // Preserve the former SteamDeck-only configuration.
    }
    Json json() const {
        Json profiles=Json::object();for(int i=0;i<4;i++) profiles[profileName(i)]=configJson(slots[i]);
        return {{"version",3},{"profiles",profiles}};
    }
};
// Horizontal inset stays 3x the original 24px; Y retains the old top inset.
// Bottom rows now anchor 12px above the monitor work-area edge (taskbar safe).
// Reserve both key rows even when hidden, so independent toggles never jump pads.
inline constexpr LONG kPanelInsetX=24*3,kPanelInsetY=40*3;
inline int basePanelSide(const RECT& monitor) {
    const int width=monitor.right-monitor.left,height=monitor.bottom-monitor.top;
    return std::min(std::clamp(width/6,150,270),std::clamp(height/4,150,250));
}
inline constexpr LONG kControlBottomClearance=12,kControlHorizontalGap=10,kControlVerticalGap=20,kControlHeight=54,kControlLiftPercent=30;
struct PanelRows { LONG gapX=0,gapY=0,keyHeight=0,padBottom=0,specialBottom=0,lift=0; };
inline PanelRows panelRows(const RECT& monitor,int scale,const RECT* workArea=nullptr) {
    const int percent=std::clamp(scale,50,200);RECT work=monitor,intersection{};
    if(workArea && IntersectRect(&intersection,&monitor,workArea))work=intersection;
    PanelRows rows;rows.gapX=MulDiv(kControlHorizontalGap,percent,100);rows.gapY=MulDiv(kControlVerticalGap,percent,100);
    const LONG padSide=MulDiv(basePanelSide(monitor),percent,100);
    // Limit the two row heights together on short screens, not the pad's scale.
    const LONG room=(work.bottom-work.top-padSide-2*rows.gapY-2*kControlBottomClearance)/2;
    rows.keyHeight=std::min((LONG)MulDiv(kControlHeight,percent,100),std::max(12L,room));
    const LONG desiredLift=MulDiv(padSide,kControlLiftPercent,100);
    const LONG maxLift=std::max(0L,work.bottom-work.top-kControlBottomClearance-padSide-2*rows.gapY-2*rows.keyHeight);
    rows.lift=std::min(desiredLift,maxLift);
    rows.specialBottom=work.bottom-kControlBottomClearance-rows.lift;
    rows.padBottom=rows.specialBottom-rows.keyHeight-rows.gapY;
    return rows;
}
inline std::array<RECT,2> panelRects(const RECT& monitor,int scale,const RECT* workArea=nullptr) {
    const LONG side=MulDiv(basePanelSide(monitor),std::clamp(scale,50,200),100);
    const LONG left=monitor.left+kPanelInsetX,right=monitor.right-kPanelInsetX,bottom=panelRows(monitor,scale,workArea).padBottom;
    return {{{left,bottom-side,left+side,bottom},{right-side,bottom-side,right,bottom}}};
}
// DualSense logical touch surface is 1920x1080 (Sony-authored Linux driver).
// Height at 100% equals the previous square; no unverified millimetre claim.
inline RECT singlePanelRect(const RECT& monitor,int scale,const RECT* workArea=nullptr) {
    const LONG height=MulDiv(basePanelSide(monitor),std::clamp(scale,50,200),100);
    const LONG width=MulDiv(height,1920,1080),centre=monitor.left+(monitor.right-monitor.left)/2;
    const LONG bottom=panelRows(monitor,scale,workArea).padBottom,left=centre-width/2;
    return {left,bottom-height,left+width,bottom};
}
inline Mode surfaceMode(const Config& c,int side) {return c.layout==Layout::Single?c.single:(side==0?c.left:c.right);}
struct Contact { bool down=false, click=false; short x=0,y=0; unsigned short pressure=0; };
struct Snapshot { bool present=false; Contact left{},right{}; };
inline Json snapshotJson(const Snapshot& s) {
    auto pad=[](const Contact& p)->Json {return {{"active",p.down},{"click",p.click},
        {"x",p.down?p.x:0},{"y",p.down?p.y:0},{"pressure",p.down?p.pressure:0}};};
    return {{"left",pad(s.left)},{"right",pad(s.right)}};
}
struct Action { unsigned keys=0; int dx=0,dy=0; bool heldClick=false,tap=false; };
struct Pad {
    UINT32 id=0; bool down=false,click=false; float x=0,y=0,lastX=0,lastY=0,startX=0,startY=0;
    double remainderX=0,remainderY=0; float travel=0; ULONGLONG started=0; unsigned keys=0;
};
struct TapClick { bool active=false,published=false; float x=0,y=0; ULONGLONG until=0; };
// Fixed-size pure state machine shared by production and simulation tests.
class Core {
public:
    Config config{}; std::array<Pad,2> pads{}; std::array<TapClick,2> clicks{};
    Action cancel() { pads={}; clicks={}; return {}; }
    Action current() const {
        Action a; for(int i=0;i<2;i++) {
            a.keys |= pads[i].keys;
            if(mode(i)==Mode::Mouse && pads[i].down && pads[i].click) a.heldClick=true;
        } return a;
    }
    Mode mode(int side) const { return surfaceMode(config,side); }
    Action event(int side, UINT32 id, int phase, float x,float y, int width,int height,ULONGLONG now) {
        Action a=current(); if(side<0 || side>1 || !config.enabled || mode(side)==Mode::Off) return a;
        Pad& p=pads[side];
        if(phase==0) {
            if(p.down) return a; // One independent contact per Deck pad; never swap fingers.
            p={}; p.id=id;p.down=true;p.started=now;
            p.x=p.lastX=p.startX=std::clamp(x,0.f,(float)width);
            p.y=p.lastY=p.startY=std::clamp(y,0.f,(float)height);
            // The whole pad is one gesture surface; no bottom hold/drag zone.
        } else {
            if(!p.down || p.id!=id) return a;
            if(phase==2) {
                const float limit=std::max(1.f,std::min(width,height)*0.03f);
                const bool tap=now-p.started<=250 &&
                    std::max(p.travel,std::hypot(x-p.startX,y-p.startY))<limit;
                if(tap && nativePad(mode(side)))
                    clicks[side]={true,false,p.x,p.y,now+60};
                p={}; a=current();a.tap=tap && mode(side)==Mode::Mouse;return a;
            }
            p.x=std::clamp(x,0.f,(float)width);p.y=std::clamp(y,0.f,(float)height);
        }
        p.travel=std::max(p.travel,std::hypot(p.x-p.startX,p.y-p.startY));
        if(mode(side)==Mode::Wasd || mode(side)==Mode::Arrows) {
            const float nx=2*p.x/std::max(width,1)-1, ny=2*p.y/std::max(height,1)-1;
            // Independent axial Schmitt triggers permit diagonals without boundary chatter.
            const unsigned bank=p.keys >> (mode(side)==Mode::Arrows?4:0);
            auto on=[&](float v,unsigned bit){return v>((bank & bit)?0.16f:0.22f);};
            unsigned bits=0;if(on(-ny,1))bits|=1;if(on(ny,2))bits|=2;
            if(on(-nx,4))bits|=4;if(on(nx,8))bits|=8;
            p.keys=bits << (mode(side)==Mode::Arrows?4:0);
        }
        a=current();
        if(mode(side)==Mode::Mouse && phase==1) {
            const double gain=config.mouseSensitivity/100.0;
            p.remainderX+=(p.x-p.lastX)*gain;p.remainderY+=(p.y-p.lastY)*gain;
            a.dx=(int)p.remainderX;a.dy=(int)p.remainderY;
            p.remainderX-=a.dx;p.remainderY-=a.dy;
        }
        p.lastX=p.x;p.lastY=p.y;return a;
    }
    Snapshot snapshot(ULONGLONG now=0) {
        Snapshot s;
        for(int i=0;i<2;i++) {
            auto& tap=clicks[i];
            // Expire in the existing BUS snapshot path, without a new timer.
            // Even after a delayed tick, expose at least one pressed report.
            if(tap.active && tap.published && now>=tap.until) tap={};
            if(!config.enabled || !nativePad(mode(i)) || (!pads[i].down && !tap.active)) continue;
            const Pad& p=pads[i];Contact c;c.down=true;c.click=tap.active;
            c.x=(short)std::clamp(std::lround(tap.active?tap.x:p.x),-32768L,32767L);
            c.y=(short)std::clamp(std::lround(tap.active?tap.y:p.y),-32768L,32767L);
            c.pressure=c.click?32767:8192;(i==0?s.left:s.right)=c;s.present=true;
            if(tap.active) tap.published=true;
        }return s;
    }
};
inline std::mutex mutex;
inline Core core;
inline std::atomic<bool> available{false}, psAvailable{false}, ps4Target{false}, enabled{false}, nativeMode{false};
inline std::atomic<HWND> owner{nullptr};
inline std::atomic<int> activeProfile{0}, appliedProfile{0};
inline HINSTANCE instance=nullptr;
// A no-redirection input host is visually empty but retains touch hit-testing.
// The separate outline has per-pixel alpha and is always click-through.
inline std::array<HWND,2> windows{}, outlines{};
inline BYTE outlineAlpha(int transparency) {return (BYTE)(255*(100-transparency)/100);}
inline std::array<SIZE,2> sizes{};
struct OutlineUpload { bool ready=false,interiorClear=false; DWORD centre=0,edge=0; };
inline std::array<OutlineUpload,2> outlineUploads{};
inline unsigned sentKeys=0; inline bool sentClick=false,dryRun=false;
inline unsigned long long eventCount=0, injectedCount=0, repaintCount=0;
inline DWORD inputError=0,windowError=0;
inline bool initialized=false; // HWND owner thread only.
inline Json* testTrace=nullptr; // Only the self-test sets this; no production trace allocations.
inline Config getConfig() {std::lock_guard<std::mutex> l(mutex);return core.config;}
inline void output(const Action& a) {
    // Called only on the HWND owner thread. Diff edges, never repeat held keys.
    static constexpr WORD vk[8]={'W','S','A','D',VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT};
    INPUT input[12]{};UINT n=0;
    for(int i=0;i<8;i++) if((sentKeys^a.keys)&(1u<<i)) {
        INPUT& p=input[n++];p.type=INPUT_KEYBOARD;p.ki.wScan=(WORD)MapVirtualKeyW(vk[i],MAPVK_VK_TO_VSC);
        p.ki.dwFlags=KEYEVENTF_SCANCODE|((a.keys&(1u<<i))?0:KEYEVENTF_KEYUP)|
            (i>=4?KEYEVENTF_EXTENDEDKEY:0);p.ki.dwExtraInfo=kInjectedTag;
    }
    if(a.dx || a.dy) { INPUT& p=input[n++];p.type=INPUT_MOUSE;p.mi.dx=a.dx;p.mi.dy=a.dy;
        p.mi.dwFlags=MOUSEEVENTF_MOVE;p.mi.dwExtraInfo=kInjectedTag; }
    if(a.heldClick!=sentClick) {INPUT& p=input[n++];p.type=INPUT_MOUSE;
        p.mi.dwFlags=a.heldClick?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP;p.mi.dwExtraInfo=kInjectedTag;}
    if(a.tap && !a.heldClick && !sentClick) for(DWORD flag:{MOUSEEVENTF_LEFTDOWN,MOUSEEVENTF_LEFTUP}) {
        INPUT& p=input[n++];p.type=INPUT_MOUSE;p.mi.dwFlags=flag;p.mi.dwExtraInfo=kInjectedTag;
    }
    if(!n) return;
    if(dryRun || SendInput(n,input,sizeof(INPUT))==n) {sentKeys=a.keys;sentClick=a.heldClick;injectedCount+=n;inputError=0;}
    else {
        // A partial batch may already have pressed a key. Remember all potentially-held
        // downs so cancellation sends compensating ups rather than leaving them stuck.
        sentKeys |= a.keys; sentClick = sentClick || a.heldClick;
        inputError=GetLastError();if(!inputError) inputError=ERROR_ACCESS_DENIED;
    }
}
inline void release() {
    buttonsRelease();
    bool changed=sentKeys!=0 || sentClick;Action a;
    {std::lock_guard<std::mutex> l(mutex);changed=changed || core.pads[0].down || core.pads[1].down || core.clicks[0].active || core.clicks[1].active;a=core.cancel();}
    output(a);if(changed) for(HWND h:windows) if(h) InvalidateRect(h,nullptr,FALSE);
}
// Dual PS surfaces share one native touchpad: contact IDs are not left/right pads.
// Keep each screen surface in its own half, with a one-pixel-safe centre split.
inline Contact normalizeContact(Contact c,LONG width,LONG height,int half=-1) {
    if(!c.down)return c;
    const double nx=std::clamp((double)c.x/std::max(1L,width),0.0,1.0);
    const double ny=std::clamp((double)c.y/std::max(1L,height),0.0,1.0);
    const double x=half<0?(2*nx-1)*32767:half==0?-32767+nx*32750:17+nx*32750;
    c.x=(short)std::clamp(std::lround(x),-32767L,32767L);
    c.y=(short)std::clamp(std::lround((1-2*ny)*32767),-32767L,32767L);
    return c;
}
inline Snapshot snapshot() {
    if(!enabled.load(std::memory_order_relaxed) || !nativeMode.load(std::memory_order_relaxed) ||
        (!available.load(std::memory_order_acquire) && !psAvailable.load(std::memory_order_acquire)) || activeProfile.load()!=appliedProfile.load()) return {};
    std::lock_guard<std::mutex> l(mutex);Snapshot s=core.snapshot(GetTickCount64());
    // Normalize either square pads or both contacts on the same wide surface.
    const auto mode=surfaceMode(core.config,0);
    if((mode==Mode::DualSense && !psAvailable.load()) || (mode==Mode::Deck && !available.load())) return {};
    for(int i=0;i<2;i++) {
        auto& c=i==0?s.left:s.right;if(!c.down) continue;
        const auto contactMode=surfaceMode(core.config,i);
        if((contactMode==Mode::Deck && !available.load()) || (contactMode==Mode::DualSense && !psAvailable.load())){c={};continue;}
        c=normalizeContact(c,sizes[i].cx,sizes[i].cy,
            core.config.layout==Layout::Dual && contactMode==Mode::DualSense?i:-1);
    }s.present=s.left.down || s.right.down;return s;
}
inline void setAvailable(bool value) {
    if(available.exchange(value,std::memory_order_acq_rel)!=value) {
        if(HWND h=owner.load(std::memory_order_acquire)) PostMessageW(h,kRefreshMessage,0,0);
    }
}
inline std::atomic<bool> controlTargetEnabled{false},controlEdgeEnabled{false};
inline std::atomic<unsigned> screenButtonMask{0};
inline void setContext(const std::string& persona,bool targetEnabled) {
    const bool controlChanged=controlTargetEnabled.exchange(targetEnabled)!=targetEnabled;
    const bool edgeChanged=controlEdgeEnabled.exchange(persona=="dualsense-edge" && targetEnabled)!=(persona=="dualsense-edge" && targetEnabled);
    const bool ps4Changed=ps4Target.exchange(persona=="dualshock4")!=(persona=="dualshock4");
    const int next=std::max(0,profileIndex(persona));
    const bool changed=activeProfile.exchange(next)!=next;
    const bool deck=next==1 && targetEnabled,ps=(persona=="dualsense" || persona=="dualsense-edge" || persona=="dualshock4") && targetEnabled;
    const bool deckChanged=available.exchange(deck)!=deck,psChanged=psAvailable.exchange(ps)!=ps;
    const bool availabilityChanged=deckChanged || psChanged || ps4Changed || controlChanged || edgeChanged;
    if(changed || availabilityChanged)screenButtonMask.store(0,std::memory_order_release);
    if(changed || availabilityChanged) if(HWND h=owner.load()) PostMessageW(h,kRefreshMessage,0,0);
}
inline bool permitted(Mode mode) {return mode!=Mode::Off && (mode!=Mode::Deck || available.load()) && (mode!=Mode::DualSense || psAvailable.load());}
// Premultiplied BGRA for UpdateLayeredWindow/AC_SRC_ALPHA. Only a one-pixel
// rounded blue stroke has alpha; all interior and outer-corner pixels are zero.
inline DWORD outlinePixel(int x,int y,int width,int height,int transparency) {
    if(x>=12 && x<width-12 && y>=12 && y<height-12) return 0;
    const double radius=std::min(10.0,std::min(width,height)/2.0-2.0);
    const double qx=std::abs(x+0.5-width/2.0)-(width/2.0-1.5-radius);
    const double qy=std::abs(y+0.5-height/2.0)-(height/2.0-1.5-radius);
    const double distance=std::hypot(std::max(qx,0.0),std::max(qy,0.0))+std::min(std::max(qx,qy),0.0)-radius;
    const auto alpha=(DWORD)std::lround(outlineAlpha(transparency)*std::clamp(1.0-std::abs(distance),0.0,1.0));
    if(!alpha) return 0;
    const DWORD red=(46*alpha+127)/255,green=(166*alpha+127)/255,blue=alpha;
    return (alpha<<24)|(red<<16)|(green<<8)|blue;
}
inline bool renderOutline(int side,int transparency) {
    if(!windows[side] || !outlines[side]) return false;
    RECT rc{};if(!GetWindowRect(windows[side],&rc)) return false;
    const int width=rc.right-rc.left,height=rc.bottom-rc.top;
    if(width<=0 || height<=0) return false;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);void* bits=nullptr;
    HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!screen || !memory || !bitmap || !bits) {
        windowError=GetLastError();if(!windowError) windowError=ERROR_NOT_ENOUGH_MEMORY;
        if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);if(screen)ReleaseDC(nullptr,screen);
        outlineUploads[side]={};return false;
    }
    const auto old=SelectObject(memory,bitmap);auto pixels=static_cast<DWORD*>(bits);
    bool clear=true;for(int y=0;y<height;y++) for(int x=0;x<width;x++) {
        const DWORD pixel=outlinePixel(x,y,width,height,transparency);pixels[y*width+x]=pixel;
        if(x>=12 && x<width-12 && y>=12 && y<height-12 && pixel!=0) clear=false;
    }
    POINT destination{rc.left,rc.top},source{};SIZE size{width,height};
    BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    const bool ok=UpdateLayeredWindow(outlines[side],screen,&destination,&size,memory,&source,0,&blend,ULW_ALPHA)!=FALSE;
    outlineUploads[side]={ok,clear,pixels[(height/2)*width+width/2],pixels[(height/2)*width+1]};
    if(!ok) {windowError=GetLastError();if(!windowError)windowError=ERROR_INVALID_DATA;}
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);
    ++repaintCount;return ok;
}
inline LRESULT CALLBACK outlineProc(HWND h,UINT m,WPARAM w,LPARAM lp) {
    switch(m) {
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_POINTERACTIVATE:return PA_NOACTIVATE;
    case WM_NCHITTEST:return HTTRANSPARENT;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    }return DefWindowProcW(h,m,w,lp);
}
inline LRESULT CALLBACK windowProc(HWND h,UINT m,WPARAM w,LPARAM lp) {
    if(m==WM_NCCREATE) {SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);return TRUE;}
    const int surface=(int)GetWindowLongPtrW(h,GWLP_USERDATA);
    int side=surface;
    switch(m) {
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_POINTERACTIVATE:return PA_NOACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {
        // No redirection surface and no drawing. Visual alpha never controls input.
        ++repaintCount;PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;
    }
    case WM_POINTERDOWN:case WM_POINTERUPDATE:case WM_POINTERUP: {
        if(!enabled.load() || activeProfile.load()!=appliedProfile.load()) return 0;
        const auto cfg=getConfig();if(!permitted(surfaceMode(cfg,surface)) || (cfg.layout==Layout::Single && surface!=0)) return 0;
        // Never type synthetic game bindings back into YeManCC's own settings UI.
        if(!dryRun && owner.load()==GetForegroundWindow()) {release();return 0;}
        POINTER_INFO pi{};const UINT32 id=GET_POINTERID_WPARAM(w);
        if(!GetPointerInfo(id,&pi) || (pi.pointerType!=PT_TOUCH && pi.pointerType!=PT_PEN)) {
            if(testTrace) testTrace->push_back({{"message",m},{"side",side},{"id",id},{"error",GetLastError()}});
            break;
        }
        if(cfg.layout==Layout::Single && cfg.single==Mode::DualSense) {
            std::lock_guard<std::mutex> l(mutex);side=-1;
            for(int i=0;i<2;i++)if(core.pads[i].down && core.pads[i].id==id){side=i;break;}
            if(side<0 && m==WM_POINTERDOWN)for(int i=0;i<2;i++)if(!core.pads[i].down){side=i;break;}
            if(side<0)return 0;
        }
        if(pi.pointerFlags&POINTER_FLAG_CANCELED) {
            Action a;{std::lock_guard<std::mutex> l(mutex);
                if(core.pads[side].down && core.pads[side].id==id) {core.pads[side]={};core.clicks[side]={};}a=core.current();}
            output(a);InvalidateRect(h,nullptr,FALSE);return 0;
        }
        POINT point=pi.ptPixelLocation;ScreenToClient(h,&point);
        Action a;{std::lock_guard<std::mutex> l(mutex);
            a=core.event(side,id,m==WM_POINTERDOWN?0:m==WM_POINTERUP?2:1,(float)point.x,(float)point.y,
                (int)sizes[side].cx,(int)sizes[side].cy,GetTickCount64());}
        ++eventCount;output(a);
        if(testTrace) testTrace->push_back({{"message",m},{"side",side},{"id",id},{"flags",pi.pointerFlags},
            {"mode",modeName(core.mode(side))},{"x",point.x},{"y",point.y},{"down",core.pads[side].down},{"keys",sentKeys},{"tick",GetTickCount64()},{"snapshot",snapshotJson(snapshot())}});
        if(m!=WM_POINTERUPDATE) InvalidateRect(h,nullptr,FALSE);
        return 0;
    }
    case WM_POINTERCAPTURECHANGED: {
        const UINT32 id=GET_POINTERID_WPARAM(w);
        {std::lock_guard<std::mutex> l(mutex);if(core.config.layout==Layout::Single && core.config.single==Mode::DualSense)
            for(int i=0;i<2;i++)if(core.pads[i].down && core.pads[i].id==id){side=i;break;}}
        if(testTrace) testTrace->push_back({{"message",m},{"side",side},{"id",GET_POINTERID_WPARAM(w)},
            {"oldId",core.pads[side].id},{"oldDown",core.pads[side].down}});
        Action a;{std::lock_guard<std::mutex> l(mutex);
            if(core.pads[side].down && core.pads[side].id==GET_POINTERID_WPARAM(w)) {core.pads[side]={};core.clicks[side]={};}
            a=core.current();}output(a);InvalidateRect(h,nullptr,FALSE);return 0;
    }
    case WM_CANCELMODE:if(testTrace)testTrace->push_back({{"message",m},{"tick",GetTickCount64()},{"cancel",true}});release();return 0;
    }
    return DefWindowProcW(h,m,w,lp);
}
inline void refresh() {
    if(!initialized) return;
    const Config cfg=getConfig();release();
    const bool show=cfg.enabled;
    POINT cursor{};GetCursorPos(&cursor);MONITORINFO mi{sizeof(mi)};
    const HWND parent=owner.load(std::memory_order_acquire);
    if(!GetMonitorInfoW(parent?MonitorFromWindow(parent,MONITOR_DEFAULTTOPRIMARY):
        MonitorFromPoint(cursor,MONITOR_DEFAULTTOPRIMARY),&mi)){windowError=GetLastError();return;}
    // One monitor/work-area snapshot feeds every input and visual window.
    buttonsRefresh(cfg,mi.rcMonitor,mi.rcWork);
    auto rects=panelRects(mi.rcMonitor,cfg.scale,&mi.rcWork);
    if(cfg.layout==Layout::Single) {rects[0]=singlePanelRect(mi.rcMonitor,cfg.scale,&mi.rcWork);rects[1]=rects[0];}
    {std::lock_guard<std::mutex> l(mutex);for(int i=0;i<2;i++)sizes[i]={rects[i].right-rects[i].left,rects[i].bottom-rects[i].top};}
    const int panelW=rects[0].right-rects[0].left,panelH=rects[0].bottom-rects[0].top;
    for(int i=0;i<2;i++) {
        const Mode mode=surfaceMode(cfg,i);
        if(cfg.layout==Layout::Single && i==1) {
            if(outlines[i]){DestroyWindow(outlines[i]);outlines[i]=nullptr;outlineUploads[i]={};}
            if(windows[i]){DestroyWindow(windows[i]);windows[i]=nullptr;}
            continue;
        }
        if(show && permitted(mode) && !windows[i]) {
            windows[i]=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,
                L"YeManScreenTouchpad",L"YeManCC Touchpad",WS_POPUP,0,0,panelW,panelH,nullptr,nullptr,instance,(void*)(INT_PTR)i);
            if(!windows[i]) windowError=GetLastError();
        }
        if(windows[i] && !outlines[i]) {
            outlines[i]=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,
                L"YeManScreenTouchpadOutline",L"YeManCC Touchpad Outline",WS_POPUP,0,0,panelW,panelH,windows[i],nullptr,instance,nullptr);
            if(!outlines[i]) windowError=GetLastError();
        }
        if(windows[i]) {
            {std::lock_guard<std::mutex> l(mutex);sizes[i]={panelW,panelH};}
            SetWindowPos(windows[i],HWND_TOPMOST,rects[i].left,rects[i].top,panelW,panelH,SWP_NOACTIVATE);
            // Upload all alpha-zero interior pixels before either window is
            // shown, including the first display. No colour-key/black-fill phase.
            const bool visualReady=outlines[i] && renderOutline(i,cfg.transparency);
            ShowWindow(windows[i],show && permitted(mode)?SW_SHOWNOACTIVATE:SW_HIDE);
            if(outlines[i]) ShowWindow(outlines[i],show && permitted(mode) && visualReady?SW_SHOWNOACTIVATE:SW_HIDE);
            InvalidateRect(windows[i],nullptr,FALSE);
            if(show && permitted(mode)) UpdateWindow(windows[i]);
        }
    }
}
inline void configure(const Config& cfg,int profile=-1) {
    const int configuredProfile=profile<0?activeProfile.load():profile;
    // Geometry changes release contacts before resizing, preventing stale coordinates/held keys.
    // Transparency-only drags still preserve active fingers.
    Config old=getConfig();const bool reset=configuredProfile!=appliedProfile.load() || old.enabled!=cfg.enabled || old.left!=cfg.left || old.right!=cfg.right || old.scale!=cfg.scale || old.layout!=cfg.layout || old.single!=cfg.single || old.summonPosition!=cfg.summonPosition || old.specialMask!=cfg.specialMask || old.rearMask!=cfg.rearMask || old.summonEnabled!=cfg.summonEnabled || old.specialEnabled!=cfg.specialEnabled || old.rearEnabled!=cfg.rearEnabled;
    if(reset) release();
    {std::lock_guard<std::mutex> l(mutex);core.config=cfg;}
    appliedProfile.store(configuredProfile);
    nativeMode.store(cfg.enabled && (cfg.layout==Layout::Single?nativePad(cfg.single):nativePad(cfg.left) || nativePad(cfg.right)),std::memory_order_release);
    enabled.store(cfg.enabled && (cfg.layout==Layout::Single || cfg.left!=Mode::Off || cfg.right!=Mode::Off),std::memory_order_release);
    if(reset) refresh();else if(old.transparency!=cfg.transparency) {
        for(int i=0;i<2;i++) if(outlines[i] && !renderOutline(i,cfg.transparency)) ShowWindow(outlines[i],SW_HIDE);
        buttonsRepaint(cfg.transparency);
    }
}
inline void initialize(HINSTANCE hi,HWND parent,const Config& cfg,int profile=-1) {
    instance=hi;owner=parent;initialized=true;WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=hi;
    wc.lpszClassName=L"YeManScreenTouchpad";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) windowError=GetLastError();
    wc.lpfnWndProc=outlineProc;wc.lpszClassName=L"YeManScreenTouchpadOutline";
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) windowError=GetLastError();
    buttonsInitialize();
    configure(cfg,profile);
}
inline void shutdown() {buttonsShutdown();controlTargetEnabled.store(false);controlEdgeEnabled.store(false);initialized=false;release();enabled.store(false);nativeMode.store(false);available.store(false);psAvailable.store(false);for(HWND& h:outlines){if(h)DestroyWindow(h);h=nullptr;}outlineUploads={};for(HWND& h:windows){if(h)DestroyWindow(h);h=nullptr;}owner=nullptr;}
inline Json state() {
    Json j=configJson(getConfig());j["ok"]=!windowError && !inputError;j["available"]=true;
    j["persona"]=profileName(appliedProfile.load());j["steamDeckAvailable"]=available.load();j["ps5Available"]=psAvailable.load() && !ps4Target.load();j["ps4Available"]=psAvailable.load() && ps4Target.load();
    j["visible"]=(windows[0] && IsWindowVisible(windows[0])) || (windows[1] && IsWindowVisible(windows[1])) || buttonsVisible();
    j["events"]=eventCount;j["injectedInputs"]=injectedCount;j["repaints"]=repaintCount;
    j["error"]=inputError?inputError:windowError;j["reason"]="";return j;
}
inline Json profileState(const Config& cfg,int profile) {
    Json j=state();const bool current=profile==activeProfile.load() && profile==appliedProfile.load();
    j.update(configJson(cfg));j["persona"]=profileName(profile);
    j["steamDeckAvailable"]=profile==1 && available.load();j["ps5Available"]=profile==2 && psAvailable.load() && !ps4Target.load();j["ps4Available"]=profile==2 && psAvailable.load() && ps4Target.load();
    if(!current) {j["visible"]=false;j["error"]=0;j["ok"]=true;}return j;
}
#include "screen_button_overlay.h"

inline Json selfTest() {
    Json cases=Json::array();auto check=[&](const char* name,bool ok){cases.push_back({{"name",name},{"ok",ok}});};
    Core c;c.config.enabled=true;
    c.event(0,11,0,20,30,200,200,1);c.event(1,22,0,180,150,200,200,1);
    check("independent-two-contacts",c.snapshot().left.down && c.snapshot().right.down);
    c.event(0,99,2,20,30,200,200,2);check("ignore-other-finger",c.pads[0].down);
    c.event(0,11,2,20,30,200,200,3);check("independent-release",!c.pads[0].down && c.pads[1].down);
    c.cancel();check("cancel-releases-native",!c.snapshot().left.down && !c.snapshot().right.down);
    check("idle-no-extra-wire-fields",!c.snapshot().present);
    c.config.left=Mode::Wasd;c.config.right=Mode::Mouse;
    auto a=c.event(0,1,0,25,25,200,200,5);check("wasd-diagonal",a.keys==(1|4));
    a=c.event(0,1,1,100,100,200,200,6);check("keyboard-deadzone-release",a.keys==0);
    c.event(1,2,0,50,50,200,200,7);a=c.event(1,2,1,62,45,200,200,8);
    check("relative-mouse",a.dx==12 && a.dy==-5 && a.keys==0);
    a=c.event(1,2,2,62,45,200,200,9);check("drag-does-not-tap",!a.tap);
    c.event(1,2,0,50,50,200,200,10);a=c.event(1,2,2,50,50,200,200,20);check("mouse-tap",a.tap);
    c.event(1,2,0,100,190,200,200,30);check("no-bottom-hold-zone",!c.current().heldClick);
    check("cancel-releases-key-and-mouse",c.cancel().keys==0 && !c.current().heldClick);
    c.config.left=Mode::Arrows;a=c.event(0,1,0,170,30,200,200,40);check("arrows-diagonal",a.keys==((1|8)<<4));
    a=c.event(0,1,1,119,100,200,200,41);check("arrows-hysteresis-retains-right",a.keys==(8<<4));
    a=c.event(0,1,1,114,100,200,200,42);check("arrows-hysteresis-releases-right",a.keys==0);
    c.cancel();c.config.left=Mode::Deck;c.config.right=Mode::Deck;
    c.event(0,1,0,100,190,200,200,43);c.event(1,2,0,100,50,200,200,43);
    check("native-touch-is-not-click",!c.snapshot(44).left.click && !c.snapshot(44).right.click);
    c.event(0,1,2,100,190,200,200,50);
    auto tap=c.snapshot(51);check("native-independent-tap-click",tap.left.click && !tap.right.click);
    tap=c.snapshot(111);check("native-tap-release",!tap.left.down && tap.right.down);
    c.cancel();c.event(0,1,0,100,100,200,200,120);c.event(0,1,2,100,100,200,200,130);
    check("delayed-bus-still-publishes-click",c.snapshot(1000).left.click);
    check("delayed-bus-publishes-release",!c.snapshot(1001).left.down);
    c.cancel();c.event(0,1,0,100,100,200,200,1100);c.event(0,1,1,150,100,200,200,1101);
    c.event(0,1,2,100,100,200,200,1102);check("swipe-return-is-not-tap",!c.snapshot(1103).present);
    c.cancel();c.config.left=Mode::Deck;c.config.enabled=false;
    c.event(0,1,0,25,25,200,200,50);check("disabled-input-is-no-op",!c.pads[0].down && !c.snapshot().present);
    Config cfg;check("reject-invalid-config",!parseConfig({{"rightMode","wasd"}},cfg,cfg));
    check("default-transparency-80",cfg.transparency==80);
    check("default-scale-100",cfg.scale==100);
    Config scaled=cfg;
    check("accept-scale-50-to-200-in-5-percent-steps",parseConfig({{"scale",50}},cfg,scaled) && scaled.scale==50 &&
        parseConfig({{"scale",105}},scaled,scaled) && scaled.scale==105 && parseConfig({{"scale",200}},scaled,scaled) && scaled.scale==200);
    check("reject-scale-bounds-or-off-grid",!parseConfig({{"scale",49}},cfg,scaled) && !parseConfig({{"scale",201}},cfg,scaled) &&
        !parseConfig({{"scale",52}},cfg,scaled) && !parseConfig({{"scale",100.5}},cfg,scaled));
    bool geometryValid=true;
    const std::array<RECT,7> monitors={{{0,0,800,600},{0,0,1280,800},{0,0,1920,1080},
        {0,0,2560,1600},{0,0,3840,2160},{-1920,-1080,0,0},{1920,120,4480,1720}}};
    for(const auto& monitor:monitors) for(int percent=50;percent<=200;percent+=5) {
        const auto rects=panelRects(monitor,percent);const int side=MulDiv(basePanelSide(monitor),percent,100);
        geometryValid=geometryValid && rects[0].left-monitor.left==72 && monitor.right-rects[1].right==72 &&
            rects[0].bottom==panelRows(monitor,percent).padBottom && rects[0].bottom==rects[1].bottom;
        for(const auto& rect:rects) geometryValid=geometryValid && rect.right-rect.left==side && rect.bottom-rect.top==side;
    }
    check("seven-resolutions-all-scale-steps-preserve-horizontal-insets-and-bottom-row-anchor",geometryValid);
    check("outline-can-be-fully-invisible",outlineAlpha(100)==0 && outlineAlpha(80)==51);
    const Config before=cfg;
    const bool valid=parseConfig({{"enabled",true},{"transparency",101}},cfg,cfg);
    check("invalid-config-does-not-partially-apply",!valid && configJson(cfg)==configJson(before));
    bool pixelsValid=true;for(int t:{0,55,80,100}) for(int y=0;y<200;y++) for(int x=0;x<200;x++) {
        const DWORD p=outlinePixel(x,y,200,200,t),alpha=p>>24;
        if(alpha>(DWORD)outlineAlpha(t) || ((p>>16)&255)>alpha || ((p>>8)&255)>alpha || (p&255)>alpha) pixelsValid=false;
        if(x>=12 && x<188 && y>=12 && y<188 && p!=0) pixelsValid=false;
        if(t==100 && p!=0) pixelsValid=false;
    }
    check("per-pixel-outline-premultiplied-alpha-and-transparent-interior",pixelsValid);
    ProfileBank bank;check("four-profile-defaults",!bank.slots[0].enabled && !bank.slots[3].enabled &&
        bank.slots[1].enabled && bank.slots[1].layout==Layout::Dual && bank.slots[1].left==Mode::Deck && bank.slots[1].right==Mode::Deck &&
        bank.slots[2].enabled && bank.slots[2].layout==Layout::Single && bank.slots[2].single==Mode::DualSense &&
        bank.slots[0].left==Mode::Wasd && bank.slots[2].left==Mode::DualSense && bank.slots[2].right==Mode::DualSense && bank.slots[3].transparency==80 &&
        bank.slots[0].scale==100 && bank.slots[1].scale==100 && bank.slots[2].scale==100 && bank.slots[3].scale==100);
    bank.slots[0].enabled=true;bank.slots[0].transparency=35;bank.slots[1].transparency=90;
    bank.slots[2].mouseSensitivity=175;bank.slots[3].right=Mode::Off;
    bank.slots[0].scale=50;bank.slots[1].scale=105;bank.slots[2].scale=150;bank.slots[3].scale=200;
    ProfileBank readback(bank.json());check("four-profiles-persist-independently",readback.json()==bank.json());
    ProfileBank migrated({{"enabled",true},{"transparency",55},{"leftMode","steamdeck"}});
    check("migrate-old-config-only-to-SteamDeck",migrated.slots[1].enabled && migrated.slots[1].transparency==55 &&
        !migrated.slots[0].enabled && migrated.slots[0].transparency==80 && migrated.slots[2].enabled &&
        migrated.slots[2].layout==Layout::Single && migrated.slots[2].single==Mode::DualSense && migrated.slots[2].transparency==80);
    check("legacy-config-without-scale-defaults-to-100",migrated.slots[1].scale==100);
    ProfileBank oldBank({{"version",2},{"profiles",{{"elite",{{"enabled",true},{"leftMode","arrows"},{"transparency",70}}}}}});
    check("legacy-profile-without-scale-preserves-settings",oldBank.slots[3].scale==100 && oldBank.slots[3].enabled &&
        oldBank.slots[3].left==Mode::Arrows && oldBank.slots[3].transparency==70);
    const ProfileBank missingProfiles({{"version",3},{"profiles",Json::object()}});
    check("missing-profiles-use-native-presets",missingProfiles.json()==ProfileBank().json());
    const ProfileBank legacyPs({{"version",2},{"profiles",{{"dualsense-edge",{{"enabled",true},{"leftMode","arrows"},{"transparency",65}}}}}});
    check("saved-legacy-PS5-remains-dual",legacyPs.slots[2].enabled && legacyPs.slots[2].layout==Layout::Dual &&
        legacyPs.slots[2].left==Mode::Arrows && legacyPs.slots[2].transparency==65);
    const ProfileBank legacyOff({{"profiles",{{"steamdeck",{{"enabled",false}}},{"dualsense-edge",{{"enabled",false}}}}}});
    check("saved-legacy-off-is-not-enabled-by-preset",!legacyOff.slots[1].enabled && !legacyOff.slots[2].enabled);
    const ProfileBank legacyPartial({{"profiles",{{"dualsense-edge",{{"transparency",55}}}}}});
    check("saved-legacy-partial-retains-old-off-default",!legacyPartial.slots[2].enabled && legacyPartial.slots[2].transparency==55);
    ProfileBank optedOut;optedOut.slots[1].enabled=false;optedOut.slots[1].left=Mode::Mouse;optedOut.slots[1].scale=145;
    optedOut.slots[2].enabled=false;optedOut.slots[2].single=Mode::Arrows;optedOut.slots[2].transparency=35;
    const ProfileBank preservedOff(optedOut.json());
    check("saved-layout-off-and-custom-settings-override-presets",preservedOff.json()==optedOut.json() &&
        !preservedOff.slots[1].enabled && !preservedOff.slots[2].enabled);
    const ProfileBank customSingle({{"profiles",{{"dualsense-edge",{{"layout","single"},{"singleMode","mouse"},{"scale",150}}}}}});
    check("saved-single-mouse-not-replaced-by-PS5-preset",customSingle.slots[2].enabled && customSingle.slots[2].layout==Layout::Single &&
        customSingle.slots[2].single==Mode::Mouse && customSingle.slots[2].scale==150);
    Config enablePatch=profileDefault(2);enablePatch.enabled=false;
    check("legacy-enable-patch-does-not-change-single-layout",parseConfig({{"enabled",true}},enablePatch,enablePatch) && enablePatch.layout==Layout::Single);
    Config single;check("accept-single-layout-and-native-DS-mode",parseConfig({{"layout","single"},{"singleMode","dualsense"}},single,single) &&
        single.enabled && single.layout==Layout::Single && single.single==Mode::DualSense);
    check("reject-inconsistent-layout-enable",!parseConfig({{"enabled",false},{"layout","single"}},single,single));
    check("single-mode-rejects-Deck-or-off",!parseConfig({{"singleMode","steamdeck"}},single,single) && !parseConfig({{"singleMode","off"}},single,single));
    Config psDual=single;
    check("dual-mode-accepts-PS-native",parseConfig({{"layout","dual"},{"leftMode","dualsense"},{"rightMode","dualsense"}},single,psDual) &&
        psDual.layout==Layout::Dual && psDual.left==Mode::DualSense && psDual.right==Mode::DualSense);
    bool halfSafe=true,fullSafe=true;
    for(int size:{75,125,200,250,500})for(int x=0;x<=size;x++) {
        const Contact p{true,false,(short)x,(short)(size/2),8192};
        const auto left=normalizeContact(p,size,size,0),right=normalizeContact(p,size,size,1),full=normalizeContact(p,size,size);
        halfSafe=halfSafe && left.x>=-32767 && left.x<=-17 && right.x>=17 && right.x<=32767;
        fullSafe=fullSafe && full.x>=-32767 && full.x<=32767;
    }
    check("PS-dual-normalized-left-right-halves-never-cross-centre-at-any-scale",halfSafe);
    check("single-and-Deck-full-surface-normalization-preserved",fullSafe);
    Core ds;ds.config=single;
    ds.event(0,10,0,80,50,360,200,1);ds.event(1,11,0,280,150,360,200,1);
    check("DS-single-surface-supports-two-independent-contacts",ds.snapshot(2).left.down && ds.snapshot(2).right.down);
    ds.event(0,10,2,80,50,360,200,10);check("DS-single-native-tap-not-Windows-mouse",ds.snapshot(11).left.click);
    check("DS-single-independent-finger-release",!ds.snapshot(80).left.down && ds.snapshot(80).right.down);
    bool singleGeometry=true;
    for(const auto& monitor:monitors)for(int scale=50;scale<=200;scale+=5) {
        const auto rect=singlePanelRect(monitor,scale);const LONG height=MulDiv(basePanelSide(monitor),scale,100);
        const LONG centre=monitor.left+(monitor.right-monitor.left)/2;
        singleGeometry=singleGeometry && rect.bottom==panelRows(monitor,scale).padBottom && rect.bottom-rect.top==height &&
            std::abs((rect.left+rect.right)-2*centre)<=1 && std::abs((rect.right-rect.left)*1080-height*1920)<=540;
    }
    check("single-wide-pad-all-scales-fixed-bottom-midpoint-and-DS-ratio",singleGeometry);
    ProfileBank layouts;layouts.slots[0].enabled=true;layouts.slots[0].layout=Layout::Single;layouts.slots[0].single=Mode::Arrows;
    layouts.slots[2].enabled=true;layouts.slots[2].layout=Layout::Single;layouts.slots[2].single=Mode::DualSense;
    const ProfileBank savedLayouts(layouts.json());check("all-profiles-remember-layout-and-single-mode",savedLayouts.json()==layouts.json());
    check("legacy-enabled-config-migrates-to-dual",migrated.slots[1].enabled && migrated.slots[1].layout==Layout::Dual);
    check("reject-unknown-profile",profileIndex("unexpected")==-1);
    for(const auto& test:buttonCoreCases())cases.push_back(test);
    bool ok=true;for(auto& t:cases)ok=ok&&t["ok"].get<bool>();return {{"ok",ok},{"cases",cases},{"systemInputInjected",false}};
}

inline Json pointerSelfTest(HINSTANCE hi) {
    // Test-only real WM_POINTER traffic. No driver, persisted settings or injected
    // keyboard/mouse actions; dryRun exercises the very same output diff engine.
    Json result=selfTest();Json cases=result["cases"];
    auto check=[&](const char* name,bool ok){cases.push_back({{"name",name},{"ok",ok}});};
    Config cfg;cfg.enabled=true;dryRun=true;Json trace=Json::array();testTrace=&trace;
    buttonEventCount=buttonSummonCount=buttonSummonPostCount=0;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    unsigned summonSinkCounts[2]{};WNDCLASSW summonSinkClass{};
    summonSinkClass.hInstance=hi;summonSinkClass.lpszClassName=L"YeManScreenButtonSummonTestSink";
    summonSinkClass.lpfnWndProc=[](HWND h,UINT m,WPARAM w,LPARAM lp)->LRESULT {
        if(m==WM_NCCREATE){SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);return TRUE;}
        if(m==kSummonMessage){auto counts=(unsigned*)GetWindowLongPtrW(h,GWLP_USERDATA);++counts[0];if(w!=0)++counts[1];return 0;}
        return DefWindowProcW(h,m,w,lp);
    };
    RegisterClassW(&summonSinkClass);
    buttonSummonTestOwner=CreateWindowExW(0,summonSinkClass.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,hi,summonSinkCounts);
    check("summon-test-has-isolated-owner-message-receiver",buttonSummonTestOwner!=nullptr);
    const HWND foreground=GetForegroundWindow();
    initialize(hi,nullptr,cfg);available.store(true);refresh();
    auto pump=[](DWORD ms) {
        const ULONGLONG deadline=GetTickCount64()+ms;MSG msg;
        do {
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            if(GetTickCount64()>=deadline) break;
            MsgWaitForMultipleObjectsEx(0,nullptr,1,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }while(true);
    };
    auto waitFor=[&](auto predicate) {
        const ULONGLONG deadline=GetTickCount64()+500;
        do {pump(2);if(predicate()) return true;}while(GetTickCount64()<deadline);
        return false;
    };
    pump(30); // Compose the uploaded outline. No visible test backdrop.
    for(int side=0;side<2;side++) {
        check(side==0?"left-square-geometry":"right-square-geometry",sizes[side].cx==sizes[side].cy);
        const auto hostStyle=GetWindowLongPtrW(windows[side],GWL_EXSTYLE);
        check(side==0?"left-transparent-input-host":"right-transparent-input-host",
            (hostStyle&WS_EX_NOREDIRECTIONBITMAP)!=0 && (hostStyle&WS_EX_LAYERED)==0);
        const auto visualStyle=GetWindowLongPtrW(outlines[side],GWL_EXSTYLE);
        const auto upload=outlineUploads[side];
        check(side==0?"left-hollow-outline":"right-hollow-outline",outlines[side] &&
            (visualStyle&WS_EX_TRANSPARENT)!=0 && (visualStyle&WS_EX_LAYERED)!=0 && upload.ready && (upload.edge>>24)==51);
        POINT centre{sizes[side].cx/2,sizes[side].cy/2};ClientToScreen(windows[side],&centre);
        check(side==0?"left-transparent-centre-hit-test":"right-transparent-centre-hit-test",
            WindowFromPoint(centre)==windows[side]);
        check(side==0?"left-uploaded-interior-alpha-zero":"right-uploaded-interior-alpha-zero",upload.interiorClear && upload.centre==0);
        result[side==0?"leftCentreArgb":"rightCentreArgb"]=upload.centre;
        result[side==0?"leftEdgeAlpha":"rightEdgeAlpha"]=upload.edge>>24;
    }
    // Compare actual composed desktop pixels, not just the source DIB. Hide/show
    // the real production HWNDs; never create a filled test window underneath.
    auto visibility=[&](bool value) {
        for(HWND h:windows) if(h) ShowWindow(h,value?SW_SHOWNOACTIVATE:SW_HIDE);
        for(HWND h:outlines) if(h) ShowWindow(h,value?SW_SHOWNOACTIVATE:SW_HIDE);
        pump(40);
    };
    auto sampleDesktop=[&]() {
        std::array<std::array<COLORREF,9>,2> pixels{};
        HDC dc=GetDC(nullptr);
        for(int side=0;side<2;side++) for(int y=0;y<3;y++) for(int x=0;x<3;x++) {
            POINT p{sizes[side].cx*(x+1)/4,sizes[side].cy*(y+1)/4};
            ClientToScreen(windows[side],&p);
            pixels[side][y*3+x]=dc?GetPixel(dc,p.x,p.y):CLR_INVALID;
        }
        if(dc) ReleaseDC(nullptr,dc);
        return pixels;
    };
    visibility(false);const auto underlyingBefore=sampleDesktop();
    visibility(true);const auto displayed=sampleDesktop();
    visibility(false);const auto underlyingAfter=sampleDesktop();
    Json desktopSamples=Json::array();
    for(int side=0;side<2;side++) {
        bool stable=true,transparent=true;
        for(int n=0;n<9;n++) {
            stable=stable && underlyingBefore[side][n]!=CLR_INVALID && underlyingBefore[side][n]==underlyingAfter[side][n];
            transparent=transparent && displayed[side][n]==underlyingBefore[side][n];
            desktopSamples.push_back({{"side",side},{"sample",n},{"before",underlyingBefore[side][n]},
                {"shown",displayed[side][n]},{"after",underlyingAfter[side][n]}});
        }
        check(side==0?"left-desktop-baseline-stable":"right-desktop-baseline-stable",stable);
        check(side==0?"left-composited-interior-fully-transparent":"right-composited-interior-fully-transparent",stable && transparent);
    }
    result["desktopTransparencySamples"]=desktopSamples;
    visibility(true);
    const bool initialized=InitializeTouchInjection(2,TOUCH_FEEDBACK_NONE)!=FALSE;
    check("windows-touch-injection-initialized",initialized);
    std::array<POINTER_TOUCH_INFO,2> touch{};
    auto contact=[&](int side,UINT32 id,float nx,float ny,DWORD flags) {
        const POINT prior=touch[side].pointerInfo.ptPixelLocation;
        auto& t=touch[side];t={};t.pointerInfo.pointerType=PT_TOUCH;t.pointerInfo.pointerId=id;
        POINT point{(LONG)(sizes[side].cx*nx),(LONG)(sizes[side].cy*ny)};
        ClientToScreen(windows[getConfig().layout==Layout::Single?0:side],&point);
        if(flags==POINTER_FLAG_UP)point=prior; // Windows requires UP at the last injected position, even after resizing.
        t.pointerInfo.ptPixelLocation=point;t.pointerInfo.pointerFlags=flags;
        t.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_ORIENTATION|TOUCH_MASK_PRESSURE;
        t.rcContact={point.x-2,point.y-2,point.x+2,point.y+2};t.orientation=90;t.pressure=512;
    };
    Json injectionErrors=Json::array();
    auto inject=[&](UINT n) {
        const bool ok=InjectTouchInput(n,touch.data())!=FALSE;
        const DWORD error=ok?0:GetLastError();
        if(!ok) injectionErrors.push_back({{"error",error},{"count",n},
            {"x",touch[0].pointerInfo.ptPixelLocation.x},{"y",touch[0].pointerInfo.ptPixelLocation.y},
            {"id",touch[0].pointerInfo.pointerId},{"flags",touch[0].pointerInfo.pointerFlags}});
        pump(20);return ok;
    };
    if(initialized && windows[0] && windows[1]) {
        const DWORD down=POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;
        const DWORD move=POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;
        contact(0,0,.2f,.2f,down);contact(1,1,.8f,.6f,down);
        check("inject-two-down",inject(2));
        auto snap=snapshot();check("wm-pointer-two-native-contacts",waitFor([] {auto s=snapshot();return s.left.down && s.right.down && s.left.x<0 && s.right.x>0;}));
        contact(0,0,.6f,.4f,move);contact(1,1,.2f,.7f,move);check("inject-two-move",inject(2));
        snap=snapshot();check("wm-pointer-independent-move",waitFor([] {auto s=snapshot();return s.left.x>0 && s.right.x<0;}));
        contact(0,0,.6f,.4f,POINTER_FLAG_UP);contact(1,1,.2f,.7f,move);
        check("inject-left-up",inject(2));snap=snapshot();check("wm-pointer-independent-up",waitFor([] {auto s=snapshot();return !s.left.down && s.right.down;}));
        contact(1,1,.2f,.7f,POINTER_FLAG_UP);
        touch[0]=touch[1];check("inject-right-up",inject(1));
        check("wm-pointer-native-all-released",waitFor([] {auto s=snapshot();return !s.left.down && !s.right.down;}));
        contact(0,0,.5f,.5f,down);check("inject-native-tap-down",inject(1));
        contact(0,0,.5f,.5f,move);check("inject-native-tap-hold-update",inject(1));
        check("native-tap-contact-before-up",waitFor([] {auto s=snapshot();return s.left.down && !s.left.click;}));
        contact(0,0,.5f,.5f,POINTER_FLAG_UP);check("inject-native-tap-up",inject(1));
        check("hollow-native-tap-click",snapshot().left.click);
        pump(80);check("hollow-native-tap-release",!snapshot().left.down);
        cfg.left=Mode::Wasd;cfg.right=Mode::Mouse;configure(cfg);pump(10);
        contact(0,0,.1f,.1f,down);contact(1,1,.5f,.5f,down);check("inject-keyboard-mouse-down",inject(2));
        check("wm-pointer-wasd-down",waitFor([] {return sentKeys==5;}));
        cfg.transparency=80;configure(cfg);check("transparency-keeps-held-key",sentKeys==5);
        const auto before=injectedCount;
        contact(0,0,.1f,.1f,move);contact(1,1,.6f,.4f,move);check("inject-mouse-move",inject(2));
        check("wm-pointer-mouse-diff",waitFor([&] {return injectedCount>before;}));
        contact(0,0,.1f,.1f,POINTER_FLAG_UP);contact(1,1,.6f,.4f,POINTER_FLAG_UP);check("inject-keyboard-mouse-up",inject(2));
        check("wm-pointer-keys-released",waitFor([] {return sentKeys==0 && !sentClick;}));
        cfg.left=Mode::Arrows;configure(cfg);pump(10);
        contact(0,0,.9f,.1f,down);check("inject-arrows-down",inject(1));
        contact(0,0,.9f,.1f,move);check("inject-arrows-hold-update",inject(1));
        check("wm-pointer-arrows-down",waitFor([] {return sentKeys==((1|8)<<4);}));
        cfg.enabled=false;configure(cfg);check("disable-releases-held-keys",sentKeys==0 && !snapshot().present);
        cfg.enabled=true;configure(cfg);pump(10);
        contact(0,0,.9f,.1f,POINTER_FLAG_UP);check("inject-arrows-up",inject(1));
        cfg.left=Mode::Deck;cfg.right=Mode::Deck;cfg.transparency=100;configure(cfg);pump(10);
        check("100-percent-outline-all-zero-alpha",outlineUploads[0].ready && outlineUploads[1].ready &&
            outlineUploads[0].centre==0 && outlineUploads[0].edge==0 && outlineUploads[1].edge==0);
        contact(0,0,.5f,.5f,down);check("inject-transparent-down",inject(1));
        contact(0,0,.5f,.5f,move);check("inject-transparent-hold-update",inject(1));
        check("wm-pointer-transparent-hit-test",waitFor([] {return snapshot().left.down;}));
        result["transparentHitOwnWindow"]=(WindowFromPoint(touch[0].pointerInfo.ptPixelLocation)==windows[0]);
        result["transparentContactState"]=snapshotJson(snapshot());
        contact(0,0,.5f,.5f,POINTER_FLAG_UP);check("inject-transparent-up",inject(1));
        check("fully-invisible-outline-still-clicks",snapshot().left.click);
        pump(80);check("fully-invisible-outline-tap-releases",!snapshot().left.down);
        setContext("disabled",false);cfg.left=Mode::Wasd;cfg.right=Mode::Mouse;configure(cfg);pump(10);
        check("standalone-pads-visible-with-virtual-controller-disabled",IsWindowVisible(windows[0]) && IsWindowVisible(windows[1]));
        contact(0,0,.1f,.1f,down);contact(1,1,.5f,.5f,down);check("inject-standalone-pads-down",inject(2));
        contact(0,0,.1f,.1f,move);contact(1,1,.6f,.4f,move);check("inject-standalone-pads-move",inject(2));
        check("standalone-keyboard-without-SteamDeck",waitFor([] {return sentKeys==5;}));
        contact(0,0,.1f,.1f,POINTER_FLAG_UP);contact(1,1,.6f,.4f,POINTER_FLAG_UP);check("inject-standalone-pads-up",inject(2));
        check("standalone-keyboard-releases",sentKeys==0);
        setContext("steamdeck",false);cfg.left=Mode::Deck;cfg.right=Mode::Deck;configure(cfg);refresh();pump(10);
        check("native-Deck-pads-blocked-until-enabled",!IsWindowVisible(windows[0]) && !IsWindowVisible(windows[1]) && !snapshot().present);
        setContext("steamdeck",true);configure(cfg);refresh();pump(10);
        check("native-Deck-pads-return-when-enabled",IsWindowVisible(windows[0]) && IsWindowVisible(windows[1]));
        RECT originalLeft{},originalRight{};GetWindowRect(windows[0],&originalLeft);GetWindowRect(windows[1],&originalRight);
        const int originalSide=originalLeft.right-originalLeft.left;
        for(int percent:{50,100,200}) {
            cfg.scale=percent;cfg.transparency=80;configure(cfg);pump(10);
            RECT left{},right{};GetWindowRect(windows[0],&left);GetWindowRect(windows[1],&right);
            const int expectedSide=MulDiv(originalSide,percent,100);
            MONITORINFO scaleMonitor{sizeof(scaleMonitor)};GetMonitorInfoW(MonitorFromWindow(windows[0],MONITOR_DEFAULTTOPRIMARY),&scaleMonitor);
            const auto expectedRects=panelRects(scaleMonitor.rcMonitor,percent,&scaleMonitor.rcWork);
            check(("actual-scale-"+std::to_string(percent)+"-workarea-anchored-lower-row").c_str(),
                EqualRect(&left,&expectedRects[0]) && EqualRect(&right,&expectedRects[1]) &&
                left.right-left.left==expectedSide && right.right-right.left==expectedSide && sizes[0].cx==expectedSide && sizes[0].cy==expectedSide);
            check(("actual-scale-"+std::to_string(percent)+"-outline-remains-hollow").c_str(),
                outlineUploads[0].ready && outlineUploads[1].ready && outlineUploads[0].interiorClear && outlineUploads[1].interiorClear &&
                outlineUploads[0].centre==0 && outlineUploads[1].centre==0 && (outlineUploads[0].edge>>24)==51 && (outlineUploads[1].edge>>24)==51);
            contact(0,0,.25f,.25f,down);contact(1,1,.75f,.75f,down);const bool downOk=inject(2);
            contact(0,0,.25f,.25f,move);contact(1,1,.75f,.75f,move);const bool moveOk=inject(2);
            const auto normalized=snapshot();
            check(("actual-scale-"+std::to_string(percent)+"-two-touch-normalization").c_str(),downOk && moveOk &&
                normalized.left.down && normalized.right.down && std::abs((int)normalized.left.x+16384)<=900 &&
                std::abs((int)normalized.left.y-16384)<=900 && std::abs((int)normalized.right.x-16384)<=900 && std::abs((int)normalized.right.y+16384)<=900);
            contact(0,0,.25f,.25f,POINTER_FLAG_UP);contact(1,1,.75f,.75f,POINTER_FLAG_UP);const bool upOk=inject(2);
            const auto clicks=snapshot();
            check(("actual-scale-"+std::to_string(percent)+"-tap-click").c_str(),upOk && clicks.left.click && clicks.right.click);
            pump(80);check(("actual-scale-"+std::to_string(percent)+"-tap-release").c_str(),!snapshot().present);
        }
        contact(0,0,.2f,.2f,down);inject(1);contact(0,0,.2f,.2f,move);inject(1);
        const bool touchedBeforeScale=snapshot().left.down;
        cfg.scale=100;configure(cfg);check("scale-change-cancels-active-native-contact",touchedBeforeScale && !snapshot().present);
        contact(0,0,.2f,.2f,POINTER_FLAG_UP);inject(1);
        cfg.left=Mode::Wasd;cfg.right=Mode::Mouse;configure(cfg);
        contact(0,0,.1f,.1f,down);inject(1);contact(0,0,.1f,.1f,move);inject(1);
        const bool keysBeforeScale=sentKeys!=0;cfg.scale=150;configure(cfg);
        check("scale-change-releases-held-keyboard",keysBeforeScale && sentKeys==0 && !sentClick);
        contact(0,0,.1f,.1f,POINTER_FLAG_UP);inject(1);
        cfg.left=Mode::Deck;cfg.right=Mode::Deck;cfg.scale=100;configure(cfg);
        setContext("dualsense-edge",true);cfg.layout=Layout::Single;cfg.single=Mode::DualSense;cfg.transparency=80;configure(cfg);pump(20);
        MONITORINFO dsMonitor{sizeof(dsMonitor)};GetMonitorInfoW(MonitorFromWindow(windows[0],MONITOR_DEFAULTTOPRIMARY),&dsMonitor);
        check("PS5-single-creates-only-one-input-and-outline",windows[0] && outlines[0] && !windows[1] && !outlines[1]);
        for(int percent:{50,100,200}) {
            cfg.scale=percent;configure(cfg);pump(20);RECT actual{};GetWindowRect(windows[0],&actual);
            const auto expected=singlePanelRect(dsMonitor.rcMonitor,percent,&dsMonitor.rcWork);
            check(("PS5-single-scale-"+std::to_string(percent)+"-bottom-centre-geometry").c_str(),EqualRect(&actual,&expected) &&
                sizes[0].cx==sizes[1].cx && sizes[0].cy==sizes[1].cy && sizes[0].cx>sizes[0].cy);
            check(("PS5-single-scale-"+std::to_string(percent)+"-hollow").c_str(),outlineUploads[0].ready && outlineUploads[0].interiorClear && outlineUploads[0].centre==0);
            const auto traceStart=trace.size();
            contact(0,0,.25f,.25f,down);contact(1,1,.75f,.75f,down);
            const HWND hit0=WindowFromPoint(touch[0].pointerInfo.ptPixelLocation),hit1=WindowFromPoint(touch[1].pointerInfo.ptPixelLocation);
            result["DShit"+std::to_string(percent)]={{"window",(ULONG_PTR)windows[0]},{"leftHit",(ULONG_PTR)hit0},{"rightHit",(ULONG_PTR)hit1},
                {"rect",{actual.left,actual.top,actual.right,actual.bottom}},
                {"leftPoint",{touch[0].pointerInfo.ptPixelLocation.x,touch[0].pointerInfo.ptPixelLocation.y}},
                {"rightPoint",{touch[1].pointerInfo.ptPixelLocation.x,touch[1].pointerInfo.ptPixelLocation.y}}};
            check(("PS5-single-scale-"+std::to_string(percent)+"-injected-points-hit-input-window").c_str(),hit0==windows[0] && hit1==windows[0]);
            const bool downOk=inject(2);
            // Windows coalesces tiny synthetic multi-touch updates on one HWND.
            // Use a real swipe and inspect the dispatched DOWN snapshots too,
            // instead of starving the injector while waiting for a static sample.
            contact(0,0,.4f,.35f,move);contact(1,1,.65f,.6f,move);const bool swipeOk=inject(2);
            const auto swiped=snapshot();result["DSswiped"+std::to_string(percent)]=snapshotJson(swiped);
            bool independentDown=false;
            for(size_t n=traceStart;n<trace.size();n++) {
                const auto& event=trace[n];
                if(event.value("message",0u)==WM_POINTERDOWN && event.contains("snapshot")) {
                    const auto& snap=event["snapshot"];
                    independentDown=independentDown || (snap["left"]["active"].get<bool>() && snap["right"]["active"].get<bool>() &&
                        std::abs(snap["left"]["x"].get<int>()+16384)<1000 && std::abs(snap["right"]["x"].get<int>()-16384)<1000);
                }
            }
            check(("PS5-single-scale-"+std::to_string(percent)+"-two-fingers-one-window").c_str(),downOk && swipeOk && independentDown && swiped.left.down && swiped.right.down);
            check(("PS5-single-scale-"+std::to_string(percent)+"-swipe-is-not-click").c_str(),swipeOk && !swiped.left.click && !swiped.right.click &&
                std::abs((int)swiped.left.x+6553)<1000 && std::abs((int)swiped.right.x-9830)<1000);
            contact(0,0,.4f,.35f,POINTER_FLAG_UP);contact(1,1,.65f,.6f,POINTER_FLAG_UP);inject(2);
            check(("PS5-single-scale-"+std::to_string(percent)+"-release").c_str(),!snapshot().present);
        }
        cfg.scale=100;configure(cfg);pump(20); // Let the resized input HWND enter Windows hit-testing before a new contact.
        contact(0,0,.5f,.5f,down);check("PS5-single-tap-inject-down",inject(1));
        contact(0,0,.5f,.5f,move);check("PS5-single-tap-inject-update",inject(1));
        check("PS5-single-tap-contact-before-up",waitFor([] {const auto s=snapshot();return s.left.down && !s.left.click;}));
        contact(0,0,.5f,.5f,POINTER_FLAG_UP);check("PS5-single-tap-inject-up",inject(1));
        check("PS5-single-tap-produces-native-click",waitFor([] {return snapshot().left.click;}));
        pump(80);check("PS5-single-tap-click-releases",!snapshot().present);
        contact(0,0,.5f,.5f,down);inject(1);contact(0,0,.5f,.5f,move);inject(1);const bool beforeDisable=snapshot().left.down;
        setContext("dualsense-edge",false);refresh();check("PS5-single-native-gated-by-target-enabled",beforeDisable && !snapshot().present && !IsWindowVisible(windows[0]));
        contact(0,0,.5f,.5f,POINTER_FLAG_UP);inject(1);
        for(const char* persona:{"disabled","steamdeck","elite"}) {
            setContext(persona,false);cfg.single=Mode::Arrows;configure(cfg);pump(10);
            check((std::string(persona)+"-single-keyboard-without-virtual-target").c_str(),IsWindowVisible(windows[0]) && !windows[1]);
            contact(0,0,.1f,.1f,down);inject(1);contact(0,0,.1f,.1f,move);inject(1);const bool keys=sentKeys!=0;
            cfg.layout=Layout::Dual;configure(cfg);check((std::string(persona)+"-single-to-dual-releases-input").c_str(),keys && sentKeys==0);
            contact(0,0,.1f,.1f,POINTER_FLAG_UP);inject(1);cfg.layout=Layout::Single;configure(cfg);
        }
        // Production two-HWND PS path: each HWND maps to one half of the
        // same PS4/PS5 surface, with independent IDs and target gating.
        for(const char* persona:{"dualsense-edge","dualsense","dualshock4"}) {
            setContext(persona,true);cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::DualSense;
            for(int percent:{50,100,200}) {
                cfg.scale=percent;configure(cfg);pump(20);
                const std::string label=std::string(persona)+"-dual-"+std::to_string(percent);
                check((label+"-two-input-HWNDs-visible").c_str(),windows[0] && windows[1] && IsWindowVisible(windows[0]) && IsWindowVisible(windows[1]));
                contact(0,0,.2f,.25f,down);contact(1,1,.8f,.75f,down);const bool downOk=inject(2);
                contact(0,0,.4f,.4f,move);contact(1,1,.6f,.6f,move);const bool moveOk=inject(2);
                const auto s=snapshot();
                check((label+"-two-real-contacts-stay-in-native-left-right-halves").c_str(),downOk && moveOk &&
                    s.left.down && s.right.down && s.left.x<0 && s.right.x>0 && s.left.y>0 && s.right.y<0);
                contact(0,0,.4f,.4f,POINTER_FLAG_UP);contact(1,1,.6f,.6f,move);inject(2);
                check((label+"-left-releases-while-right-remains-native").c_str(),!snapshot().left.down && snapshot().right.down);
                contact(1,1,.6f,.6f,POINTER_FLAG_UP);touch[0]=touch[1];inject(1);
                check((label+"-both-release").c_str(),!snapshot().present);
                contact(0,0,.5f,.5f,down);inject(1);contact(0,0,.5f,.5f,move);inject(1);
                contact(0,0,.5f,.5f,POINTER_FLAG_UP);inject(1);
                check((label+"-left-half-tap-shared-native-click").c_str(),snapshot().left.click && snapshot().left.x<0);
                pump(80);check((label+"-tap-releases").c_str(),!snapshot().present);
            }
            cfg.scale=100;cfg.left=Mode::Wasd;configure(cfg);pump(20);
            contact(1,1,.5f,.5f,down);touch[0]=touch[1];inject(1);
            contact(1,1,.5f,.5f,move);touch[0]=touch[1];inject(1);
            check((std::string(persona)+"-right-native-works-with-left-keyboard").c_str(),snapshot().right.down && snapshot().right.x>0);
            contact(1,1,.5f,.5f,POINTER_FLAG_UP);touch[0]=touch[1];inject(1);pump(80);
            setContext(persona,false);refresh();pump(5);
            check((std::string(persona)+"-dual-native-hidden-when-virtual-target-off").c_str(),!snapshot().present && !IsWindowVisible(windows[1]));
        }
        cfg.left=cfg.right=Mode::Deck;
        setContext("steamdeck",true);cfg.layout=Layout::Dual;cfg.scale=100;configure(cfg);refresh();pump(10);
        check("return-to-Deck-dual-restores-both-windows",windows[0] && windows[1] && IsWindowVisible(windows[0]) && IsWindowVisible(windows[1]));
        // Native screen-button windows use the same InjectTouchInput stream.
        cfg.enabled=false;cfg.summonEnabled=cfg.specialEnabled=cfg.rearEnabled=true;cfg.transparency=80;
        auto pressButton=[&](int index,DWORD flags,int finger=0) {
            const POINT prior=touch[finger].pointerInfo.ptPixelLocation;
            auto& t=touch[finger];t={};t.pointerInfo.pointerType=PT_TOUCH;t.pointerInfo.pointerId=finger;
            // Touch where the user actually sees the frame, never its assumed input rect.
            RECT visual{};GetWindowRect(buttonOutlines[index],&visual);POINT p{(visual.left+visual.right)/2,(visual.top+visual.bottom)/2};
            t.pointerInfo.ptPixelLocation=flags==POINTER_FLAG_UP?prior:p;t.pointerInfo.pointerFlags=flags;
            t.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_ORIENTATION|TOUCH_MASK_PRESSURE;
            const auto point=t.pointerInfo.ptPixelLocation;t.rcContact={point.x-2,point.y-2,point.x+2,point.y+2};t.orientation=90;t.pressure=512;
        };
        wchar_t testExePath[MAX_PATH]{};GetModuleFileNameW(nullptr,testExePath,MAX_PATH);const auto testArtDir=std::filesystem::path(testExePath).parent_path();
        for(const char* persona:{"steamdeck","dualsense-edge","elite","disabled"}) {
            buttonArtTestPrefix=testArtDir/(std::string("screen-button-art-")+persona);
            setContext(persona,true);configure(cfg);refresh();pump(15);
            const int index=profileIndex(persona),count=index==1 || index==2?7:index==3?2:1;int shown=0;
            for(HWND h:buttonWindows)if(h && IsWindowVisible(h))++shown;
            check((std::string(persona)+"-correct-native-button-window-count").c_str(),shown==count);
            bool artwork=true;
            for(int n=0;n<7;n++)if(buttonWindows[n] && IsWindowVisible(buttonWindows[n])) {
                const auto u=buttonUploads[n];artwork=artwork && u.ready && u.whiteIcon && u.hasIcon && u.maxAlpha==51 && u.transparentBackground==0;
            }
            check((std::string(persona)+"-white-icons-transparent-centres-opacity-80").c_str(),artwork);
            bool centred=true,psHalf=true;
            for(int n=0;n<7;n++)if(buttonWindows[n] && IsWindowVisible(buttonWindows[n])) {
                const auto u=buttonUploads[n];
                centred=centred && u.ink.left>1 && u.ink.top>1 && u.ink.right<u.width-1 && u.ink.bottom<u.height-1 &&
                    std::abs(u.ink.left+u.ink.right-u.width)<=2 && std::abs(u.ink.top+u.ink.bottom-u.height)<=2;
                if(index==2 && n>0)psHalf=psHalf && u.ink.bottom-u.ink.top<=(LONG)std::ceil(u.height*.33);
            }
            check((std::string(persona)+"-all-glyphs-centred-with-no-border-cropping").c_str(),centred);
            if(index==2)check("PS5-glyphs-half-height-with-full-RB-silhouette",psHalf);
            result["buttonInk"][persona]=Json::array();
            for(int n=0;n<7;n++)if(buttonWindows[n] && IsWindowVisible(buttonWindows[n])) {
                const auto u=buttonUploads[n];result["buttonInk"][persona].push_back({{"index",n},{"width",u.width},{"height",u.height},
                    {"ink",{u.ink.left,u.ink.top,u.ink.right,u.ink.bottom}},{"maxAlpha",u.maxAlpha}});
            }
            Json art=Json::array();for(int n=0;n<7;n++)if(buttonWindows[n] && IsWindowVisible(buttonWindows[n])){RECT r{};GetWindowRect(buttonWindows[n],&r);art.push_back({{"index",n},{"rect",{r.left,r.top,r.right,r.bottom}}});}
            result["buttonArt"][persona]=art;
            bool visualAligned=true;Json visualArt=Json::array();
            for(int n=0;n<7;n++)if(buttonWindows[n] && IsWindowVisible(buttonWindows[n])) {
                RECT input{},visual{};GetWindowRect(buttonWindows[n],&input);
                const bool haveVisual=buttonOutlines[n] && GetWindowRect(buttonOutlines[n],&visual);
                visualAligned=haveVisual && IsWindowVisible(buttonOutlines[n]) && EqualRect(&input,&visual) && visualAligned;
                visualArt.push_back({{"index",n},{"rect",{visual.left,visual.top,visual.right,visual.bottom}}});
            }
            result["buttonVisualArt"][persona]=visualArt;
            check((std::string(persona)+"-visible-outline-equals-actual-touch-window").c_str(),visualAligned);
            MONITORINFO artMonitor{sizeof(artMonitor)};GetMonitorInfoW(MonitorFromWindow(buttonWindows[0],MONITOR_DEFAULTTOPRIMARY),&artMonitor);
            result["buttonMonitor"][persona]={artMonitor.rcMonitor.left,artMonitor.rcMonitor.top,artMonitor.rcMonitor.right,artMonitor.rcMonitor.bottom};
            const auto expectedButtons=buttonRects(artMonitor.rcMonitor,cfg.scale,&artMonitor.rcWork,effectiveSummonPosition(cfg),effectiveRearMask(cfg));
            const auto expectedPads=panelRects(artMonitor.rcMonitor,cfg.scale,&artMonitor.rcWork);
            result["buttonWorkArea"][persona]={artMonitor.rcWork.left,artMonitor.rcWork.top,artMonitor.rcWork.right,artMonitor.rcWork.bottom};
            result["buttonVerticalGap"][persona]=panelRows(artMonitor.rcMonitor,cfg.scale,&artMonitor.rcWork).gapY;
            result["buttonScale"][persona]=cfg.scale;
            result["buttonPads"][persona]=Json::array();
            for(const auto& pad:expectedPads)result["buttonPads"][persona].push_back({pad.left,pad.top,pad.right,pad.bottom});
            RECT yActual{};GetWindowRect(buttonWindows[0],&yActual);check((std::string(persona)+"-Y-selected-screen-top-inset-position").c_str(),EqualRect(&yActual,&expectedButtons[0]) && yActual.top==artMonitor.rcMonitor.top+kPanelInsetY);
            bool order=true;
            for(int n=1;n<7;n++)if(buttonWindows[n] && IsWindowVisible(buttonWindows[n])) {
                RECT actual{};GetWindowRect(buttonWindows[n],&actual);const int side=n<3?n-1:(n-3)/2;
                order=order && EqualRect(&actual,&expectedButtons[n]) && (n<3?actual.top>expectedPads[side].bottom:actual.bottom<expectedPads[side].top);
            }
            check((std::string(persona)+"-rear-pad-special-order-with-taskbar-safe-anchor").c_str(),order);
            if(index==1 || index==2) {
                bool safe=true;const auto rows=panelRows(artMonitor.rcMonitor,cfg.scale,&artMonitor.rcWork);
                for(int side=0;side<2;side++) {
                    RECT special{},rear{};GetWindowRect(buttonWindows[1+side],&special);GetWindowRect(buttonWindows[3+2*side],&rear);
                    safe=safe && special.bottom==artMonitor.rcWork.bottom-kControlBottomClearance-panelRows(artMonitor.rcMonitor,cfg.scale,&artMonitor.rcWork).lift &&
                        special.top-expectedPads[side].bottom==rows.gapY && expectedPads[side].top-rear.bottom==rows.gapY &&
                        rear.top>=artMonitor.rcWork.top;
                }
                check((std::string(persona)+"-actual-special-above-taskbar-two-vertical-gaps-doubled").c_str(),safe);
                bool aligned=true;
                for(int side=0;side<2;side++) {
                    RECT special{},inner{},outer{};GetWindowRect(buttonWindows[1+side],&special);
                    GetWindowRect(buttonWindows[side==0?4:5],&inner);GetWindowRect(buttonWindows[side==0?3:6],&outer);
                    aligned=aligned && special.left==inner.left && special.right==inner.right &&
                        std::abs((inner.right-inner.left)-(outer.right-outer.left))<=1;
                }
                check((std::string(persona)+"-actual-special-width-equals-inner-rear-key-cell").c_str(),aligned);
            }
            const int limit=index==1 || index==2?7:index==3?2:1;
            for(int n=1;n<limit;n++) {
                pressButton(n,down);const bool downOk=inject(1);pressButton(n,move);const bool updateOk=inject(1);
                check((std::string(persona)+"-touch-button-"+std::to_string(n)+"-down-to-BUS").c_str(),downOk && updateOk && buttonSnapshot()==(1u<<(n-1)));
                pressButton(n,POINTER_FLAG_UP);inject(1);
                check((std::string(persona)+"-touch-button-"+std::to_string(n)+"-up-releases").c_str(),buttonSnapshot()==0);
            }
            const auto beforeSummon=buttonSummonCount,beforePosts=buttonSummonPostCount,beforeReceived=summonSinkCounts[0];
            pressButton(0,down);inject(1);pressButton(0,move);inject(1);pressButton(0,POINTER_FLAG_UP);inject(1);
            check((std::string(persona)+"-Y-touch-calls-summon-once-without-gamepad-bit").c_str(),buttonSummonCount==beforeSummon+1 && buttonSnapshot()==0);
            check((std::string(persona)+"-visible-Y-touch-posts-standard-summon-to-owner-UI").c_str(),buttonSummonPostCount==beforePosts+1 && summonSinkCounts[0]==beforeReceived+1 && summonSinkCounts[1]==0);
        }
        buttonArtTestPrefix.clear();
        for(const char* persona:{"steamdeck","dualsense-edge"}) {
            setContext(persona,true);cfg.enabled=false;
            for(int mask=0;mask<8;mask++) {
                cfg.summonEnabled=(mask&1)!=0;cfg.specialEnabled=(mask&2)!=0;cfg.rearEnabled=(mask&4)!=0;
                configure(cfg);refresh();pump(5);int shown=0;for(HWND h:buttonWindows)if(h && IsWindowVisible(h))++shown;
                const int expected=((mask&1)?1:0)+((mask&2)?2:0)+((mask&4)?4:0);
                check((std::string(persona)+"-pad-off-independent-toggle-combination-"+std::to_string(mask)).c_str(),shown==expected &&
                    (!windows[0] || !IsWindowVisible(windows[0])) && (!windows[1] || !IsWindowVisible(windows[1])));
            }
        }
        // Four menu choices: either named pair, all four keys, or none.
        // Inject at the actual visual rectangles, checking IDs as well as placement.
        for(const char* persona:{"steamdeck","dualsense-edge"}) {
            setContext(persona,true);cfg.enabled=false;cfg.summonEnabled=false;cfg.summonPosition=SummonPosition::Off;
            cfg.specialEnabled=false;cfg.specialMask=0;
            for(unsigned mask:{0u,5u,10u,15u}) {
                cfg.rearMask=mask;cfg.rearEnabled=mask!=0;configure(cfg);pump(20);
                bool matching=true,positioned=true;
                MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(buttonWindows[3],MONITOR_DEFAULTTOPRIMARY),&mi);
                const auto full=buttonRects(mi.rcMonitor,cfg.scale,&mi.rcWork,SummonPosition::Right,15u);
                for(int n=0;n<7;n++) matching=matching &&
                    ((buttonWindows[n] && IsWindowVisible(buttonWindows[n]))==(n>=3 && (mask&(1u<<(n-3)))!=0));
                for(int n=3;n<7;n++)if(mask&(1u<<(n-3))) {
                    const int side=(n-3)/2,inner=side==0?4:5;
                    const auto expected=mask==15u?full[n]:full[inner];
                    RECT input{},visual{};GetWindowRect(buttonWindows[n],&input);GetWindowRect(buttonOutlines[n],&visual);
                    positioned=positioned && EqualRect(&input,&expected) && EqualRect(&visual,&expected);
                    if(mask!=15u) {
                        const auto outer=full[side==0?3:6];POINT p{(outer.left+outer.right)/2,(outer.top+outer.bottom)/2};
                        positioned=positioned && WindowFromPoint(p)!=buttonWindows[n];
                    }
                    pressButton(n,down);const bool downOk=inject(1);pressButton(n,move);const bool updateOk=inject(1);
                    check((std::string(persona)+"-rear-mask-"+std::to_string(mask)+"-key-"+std::to_string(n)+"-keeps-real-BUS-bit").c_str(),
                        downOk && updateOk && waitFor([&] {return buttonSnapshot()==(1u<<(n-1));}));
                    pressButton(n,POINTER_FLAG_UP);inject(1);
                    check((std::string(persona)+"-rear-mask-"+std::to_string(mask)+"-key-"+std::to_string(n)+"-release").c_str(),buttonSnapshot()==0);
                }
                check((std::string(persona)+"-rear-selector-mask-"+std::to_string(mask)+"-shows-exact-keys").c_str(),matching);
                check((std::string(persona)+"-rear-selector-mask-"+std::to_string(mask)+"-single-inner-double-fixed-and-visual-hitboxes-aligned").c_str(),positioned);
            }
        }
        cfg.rearMask=0;cfg.rearEnabled=false;setContext("steamdeck",true);
        for(auto position:{SummonPosition::Left,SummonPosition::Right}) {
            cfg.summonPosition=position;cfg.summonEnabled=true;configure(cfg);pump(20);
            RECT actual{};GetWindowRect(buttonWindows[0],&actual);
            MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(buttonWindows[0],MONITOR_DEFAULTTOPRIMARY),&mi);
            const LONG x=position==SummonPosition::Left?mi.rcMonitor.left+kPanelInsetX:
                mi.rcMonitor.right-kPanelInsetX-(actual.right-actual.left);
            const std::string name=position==SummonPosition::Left?"left":"right";
            check(("Y-selector-"+name+"-uses-selected-edge-and-full-top-inset").c_str(),
                actual.left==x && actual.top==mi.rcMonitor.top+kPanelInsetY && IsWindowVisible(buttonWindows[0]));
            const auto before=summonSinkCounts[0];
            pressButton(0,down);inject(1);pressButton(0,move);inject(1);pressButton(0,POINTER_FLAG_UP);inject(1);
            check(("Y-selector-"+name+"-touch-posts-default-summon-without-forced-maximize").c_str(),
                summonSinkCounts[0]==before+1 && summonSinkCounts[1]==0);
        }
        cfg.summonPosition=SummonPosition::Off;
        cfg.summonEnabled=cfg.specialEnabled=cfg.rearEnabled=true;
        setContext("steamdeck",true);cfg.enabled=true;cfg.layout=Layout::Dual;
        for(int percent:{50,75,100,125,150,175,200}) {
            cfg.scale=percent;configure(cfg);refresh();pump(3);bool aligned=true;
            const auto verifyRects=[&]() {
                for(int n=0;n<7;n++)if(buttonWindows[n] && IsWindowVisible(buttonWindows[n])) {
                    RECT input{},visual{};GetWindowRect(buttonWindows[n],&input);
                    aligned=aligned && buttonOutlines[n] && GetWindowRect(buttonOutlines[n],&visual) &&
                        IsWindowVisible(buttonOutlines[n]) && EqualRect(&input,&visual) &&
                        buttonUploads[n].width==input.right-input.left && buttonUploads[n].height==input.bottom-input.top;
                }
            };
            verifyRects();cfg.transparency=35;configure(cfg);pump(2);verifyRects();
            cfg.transparency=80;configure(cfg);pump(2);verifyRects();
            check(("actual-button-scale-"+std::to_string(percent)+"-visual-input-size-position-and-repaint-stay-aligned").c_str(),aligned);
            MONITORINFO rowMonitor{sizeof(rowMonitor)};GetMonitorInfoW(MonitorFromWindow(buttonWindows[1],MONITOR_DEFAULTTOPRIMARY),&rowMonitor);
            bool taskbarSafe=true;
            for(int side=0;side<2;side++) {
                RECT pad{},special{},rear{};GetWindowRect(windows[side],&pad);GetWindowRect(buttonWindows[1+side],&special);
                GetWindowRect(buttonWindows[3+2*side],&rear);
                const LONG gap=MulDiv(20,percent,100);
                taskbarSafe=taskbarSafe && special.bottom==rowMonitor.rcWork.bottom-kControlBottomClearance-panelRows(rowMonitor.rcMonitor,percent,&rowMonitor.rcWork).lift &&
                    special.top-pad.bottom==gap && pad.top-rear.bottom==gap && rear.top>=rowMonitor.rcWork.top;
            }
            check(("actual-stack-scale-"+std::to_string(percent)+"-two-doubled-gaps-special-above-taskbar").c_str(),taskbarSafe);
        }
        cfg.scale=100;configure(cfg);refresh();pump(15);
        RECT dualButtons{};GetWindowRect(buttonWindows[1],&dualButtons);
        cfg.layout=Layout::Single;configure(cfg);pump(15);RECT singleButtons{};GetWindowRect(buttonWindows[1],&singleButtons);
        check("single-pad-does-not-reposition-special-or-back-rows",EqualRect(&dualButtons,&singleButtons));
        cfg.layout=Layout::Dual;configure(cfg);pump(15);
        pressButton(1,down);pressButton(6,down,1);inject(2);pressButton(1,move);pressButton(6,move,1);inject(2);
        check("two-simultaneous-special-and-rear-button-touches",buttonSnapshot()==33);
        pressButton(1,POINTER_FLAG_UP);pressButton(6,POINTER_FLAG_UP,1);inject(2);check("two-button-release-clears-BUS-bits",buttonSnapshot()==0);
        contact(0,0,.3f,.3f,down);pressButton(1,down,1);inject(2);contact(0,0,.4f,.4f,move);pressButton(1,move,1);inject(2);
        check("real-pad-and-special-button-touch-coexist",snapshot().left.down && buttonSnapshot()==1);
        contact(0,0,.4f,.4f,POINTER_FLAG_UP);pressButton(1,POINTER_FLAG_UP,1);inject(2);
        check("pad-and-special-button-release-independently",!snapshot().present && buttonSnapshot()==0);
        pressButton(3,down);inject(1);pressButton(3,move);inject(1);cfg.scale=105;configure(cfg);pump(10);
        check("button-scale-change-releases-held-input",buttonSnapshot()==0);pressButton(3,POINTER_FLAG_UP);inject(1);
        cfg.scale=100;configure(cfg);pump(10);
        pressButton(3,down);inject(1);pressButton(3,move);inject(1);cfg.transparency=100;configure(cfg);pump(10);
        check("button-transparency-update-preserves-held-input",buttonSnapshot()==4 && buttonUploads[3].maxAlpha==0);
        pressButton(3,POINTER_FLAG_UP);inject(1);check("invisible-button-still-releases",buttonSnapshot()==0);
        pressButton(1,down);inject(1);pressButton(1,move);inject(1);setContext("steamdeck",false);refresh();pump(10);
        check("target-off-hides-native-buttons-keeps-Y",buttonSnapshot()==0 && IsWindowVisible(buttonWindows[0]) && !IsWindowVisible(buttonWindows[1]) && !IsWindowVisible(buttonWindows[3]));
        pressButton(1,POINTER_FLAG_UP);inject(1);
        cfg.summonEnabled=cfg.specialEnabled=cfg.rearEnabled=false;configure(cfg);check("three-toggles-off-hide-buttons",!buttonsVisible());
        setContext("steamdeck",true);cfg.enabled=true;configure(cfg);refresh();pump(10);
        result["buttonPointerEvents"]=buttonEventCount;result["buttonSummonCalls"]=buttonSummonCount;
        result["buttonSummonPosts"]=buttonSummonPostCount;result["buttonSummonOwnerMessages"]=summonSinkCounts[0];
        result["buttonSummonBadShowCommands"]=summonSinkCounts[1];
        const auto buttonBenchStart=std::chrono::steady_clock::now();unsigned buttonChecksum=0;
        for(int n=0;n<1000000;n++)buttonChecksum+=buttonSnapshot();
        result["disabledMillionButtonSnapshotsUs"]=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-buttonBenchStart).count();
        check("disabled-button-snapshot-hot-path-no-extra-input",buttonChecksum==0);
        result["foregroundBefore"]=(unsigned long long)(uintptr_t)foreground;result["foregroundAfter"]=(unsigned long long)(uintptr_t)GetForegroundWindow();
        check("overlay-does-not-steal-focus",GetForegroundWindow()==foreground);
        pump(50);const auto paints=repaintCount,events=eventCount;
        auto processCpu=[] {
            FILETIME created{},exited{},kernel{},user{};
            if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) return 0ULL;
            ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
            u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return k.QuadPart+u.QuadPart;
        };
        const auto cpuStart=processCpu();const auto wallStart=GetTickCount64();
        MSG msg{};const auto idleDeadline=GetTickCount64()+1500;
        while(GetTickCount64()<idleDeadline) {
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            const auto now=GetTickCount64();if(now>=idleDeadline) break;
            MsgWaitForMultipleObjectsEx(0,nullptr,(DWORD)(idleDeadline-now),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        result["visibleIdleWallMs"]=GetTickCount64()-wallStart;
        result["visibleIdleCpuMs"]=(processCpu()-cpuStart)/10000.0;
        check("idle-no-repaint-or-input",paints==repaintCount && events==eventCount);
        // Time the production disabled snapshot fast path, not an optimized-away stub.
        enabled.store(false);
        const auto start=std::chrono::steady_clock::now();unsigned checksum=0;
        for(int i=0;i<1000000;i++) checksum+=snapshot().present;
        const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
        result["disabledMillionSnapshotsUs"]=us;check("disabled-no-contact",checksum==0);
        enabled.store(true);nativeMode.store(true);
        const auto activeStart=std::chrono::steady_clock::now();
        for(int i=0;i<100000;i++)checksum+=snapshot().present;
        result["nativeIdleHundredThousandSnapshotsUs"]=std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-activeStart).count();
        check("native-idle-omits-extra-wire",checksum==0);
    } else check("overlay-windows-created",false);
    const auto events=eventCount;shutdown();check("shutdown-clears-input",sentKeys==0 && !sentClick);
    check("shutdown-destroys-input-and-outline-windows",!windows[0] && !windows[1] && !outlines[0] && !outlines[1]);
    bool buttonsDestroyed=true;for(HWND h:buttonWindows)buttonsDestroyed=buttonsDestroyed && !h;for(HWND h:buttonOutlines)buttonsDestroyed=buttonsDestroyed && !h;
    check("shutdown-destroys-all-button-input-and-icon-windows",buttonsDestroyed && buttonSnapshot()==0);
    if(buttonSummonTestOwner)DestroyWindow(buttonSummonTestOwner);buttonSummonTestOwner=nullptr;
    testTrace=nullptr;result["trace"]=trace;
    result["injectionErrors"]=injectionErrors;result["pointerEvents"]=events;result["cases"]=cases;bool ok=true;
    for(const auto& c:cases)ok=ok&&c["ok"].get<bool>();result["ok"]=ok;
    result["systemInputInjected"]=false;result["windowsTouchInjected"]=true;
    return result;
}

} // namespace ymcc::screenpads
