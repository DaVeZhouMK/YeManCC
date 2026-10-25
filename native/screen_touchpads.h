#pragma once
// Event-driven screen touchpads. No worker, periodic timer, input hook or WebView.
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
#include "controller_persona_settings.h"
#include "screen_control_glyphs.h"
#include "touch_haptic_dispatch.h"
#include "screen_touchpad_display_policy.h"
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
#include "screen_touchpad_input_surface.h"
#endif

namespace ymcc::screenpads {
using Json = nlohmann::json;
inline constexpr UINT kRefreshMessage = WM_APP + 172;
inline constexpr UINT kSummonMessage = WM_APP + 173;
inline constexpr UINT kNativeOutputResetMessage = WM_APP + 179;
inline constexpr ULONG_PTR kInjectedTag = 0x594d5450;
enum class Mode { Deck, DualSense, Wasd, Arrows, Mouse, Off };
enum class SummonPosition { Off, Left, Right };
enum class StandaloneSpecialMode { Off, SteamDeck, PS5 };
enum class Layout { Dual, Single };
enum class TouchFeedbackMode { Off, Minimal, Medium, Strong };
inline const char* touchFeedbackName(TouchFeedbackMode mode) {
    switch(mode){case TouchFeedbackMode::Minimal:return "minimal";case TouchFeedbackMode::Medium:return "medium";
        case TouchFeedbackMode::Strong:return "strong";default:return "off";}
}
inline constexpr int touchFeedbackStrength(TouchFeedbackMode mode) noexcept {
    switch(mode){case TouchFeedbackMode::Minimal:return ymcc::touchhaptic::kStrengthPercent;
        case TouchFeedbackMode::Medium:return ymcc::touchhaptic::kMediumStrengthPercent;
        case TouchFeedbackMode::Strong:return ymcc::touchhaptic::kStrongStrengthPercent;default:return 0;}
}
inline bool parseTouchFeedback(const std::string& value,TouchFeedbackMode& mode) {
    if(value=="off") mode=TouchFeedbackMode::Off;
    else if(value=="minimal") mode=TouchFeedbackMode::Minimal;
    else if(value=="medium") mode=TouchFeedbackMode::Medium;
    else if(value=="strong") mode=TouchFeedbackMode::Strong;
    else return false;
    return true;
}
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
struct Config { bool masterEnabled=true; bool enabled=false; Mode left=Mode::Deck, right=Mode::Deck; int transparency=80; int mouseSensitivity=100; int scale=100; Layout layout=Layout::Dual; Mode single=Mode::Mouse; TouchFeedbackMode touchFeedback=TouchFeedbackMode::Medium; SummonPosition summonPosition=SummonPosition::Off; StandaloneSpecialMode standaloneSpecialMode=StandaloneSpecialMode::Off; unsigned specialMask=0,rearMask=0; bool summonEnabled=false,specialEnabled=false,rearEnabled=false; int autoHideSeconds=60; };
inline bool validAutoHideSeconds(long long seconds) noexcept {return seconds==0 || seconds==15 || seconds==30 || seconds==45 || seconds==60 || seconds==120;}
inline const char* summonPositionName(SummonPosition p) {return p==SummonPosition::Left?"left":p==SummonPosition::Right?"right":"off";}
inline bool parseSummonPosition(const std::string& s,SummonPosition& p) {if(s=="off")p=SummonPosition::Off;else if(s=="left")p=SummonPosition::Left;else if(s=="right")p=SummonPosition::Right;else return false;return true;}
inline SummonPosition effectiveSummonPosition(const Config& c) {return c.summonPosition!=SummonPosition::Off?c.summonPosition:c.summonEnabled?SummonPosition::Right:SummonPosition::Off;}
inline unsigned effectiveSpecialMask(const Config& c) {return c.specialMask?c.specialMask:(c.specialEnabled?3u:0u);}
inline unsigned effectiveRearMask(const Config& c) {return c.rearMask?c.rearMask:(c.rearEnabled?15u:0u);}
inline const char* standaloneSpecialName(StandaloneSpecialMode mode) {
    return mode==StandaloneSpecialMode::SteamDeck?"steamdeck":mode==StandaloneSpecialMode::PS5?"ps5":"off";
}
inline bool parseStandaloneSpecial(const std::string& value,StandaloneSpecialMode& mode) {
    if(value=="off")mode=StandaloneSpecialMode::Off;
    else if(value=="steamdeck")mode=StandaloneSpecialMode::SteamDeck;
    else if(value=="ps5")mode=StandaloneSpecialMode::PS5;
    else return false;return true;
}
// This chooses artwork/visible dedicated keys only. It never changes the virtual
// output persona, availability flags, profile bank or BUS report admission.
inline int standaloneSpecialProfile(const Config& cfg) {
    return cfg.standaloneSpecialMode==StandaloneSpecialMode::SteamDeck?1:cfg.standaloneSpecialMode==StandaloneSpecialMode::PS5?2:0;
}
inline void buttonsNativeInputCancel();
inline void buttonsNativeInputDisable();
inline bool buttonsNativeInputReady();
inline void buttonsRelease();
inline void buttonsReleaseMapped();
inline unsigned buttonFeedbackMask();
inline bool buttonsRefresh(const Config&,const RECT&,const RECT&,bool force=false);
inline bool buttonsNeedRefresh(const Config&);
inline void buttonsInitialize();
inline void buttonsShutdown();
inline void buttonsQuiesce();
inline void buttonsRepaint(int);
inline bool buttonsVisible();
inline Json buttonVisibilityState(const Config& cfg);
inline unsigned buttonSnapshot();
inline Json configJson(const Config& c) {
    const auto summon=effectiveSummonPosition(c);const unsigned special=effectiveSpecialMask(c),rear=effectiveRearMask(c);
    return {{"summonPosition",summonPositionName(summon)},{"standaloneSpecialMode",standaloneSpecialName(c.standaloneSpecialMode)},{"specialMask",special},{"rearMask",rear},
        {"summonEnabled",summon!=SummonPosition::Off},{"specialEnabled",special!=0},{"rearEnabled",rear!=0},
        {"masterEnabled",c.masterEnabled},{"enabled",c.enabled},{"leftMode",modeName(c.left)},{"rightMode",modeName(c.right)},
        {"transparency",c.transparency},{"mouseSensitivity",c.mouseSensitivity},{"scale",c.scale},
        {"touchFeedback",touchFeedbackName(c.touchFeedback)},{"autoHideSeconds",c.autoHideSeconds},
        {"layout",!c.enabled?"off":c.layout==Layout::Single?"single":"dual"},{"singleMode",modeName(c.single)}};
}
inline bool parseConfig(const Json& j, const Config& old, Config& c) {
    Config next=old;bool legacySummon=false,legacySpecial=false,legacyRear=false,hasSummon=false,hasSpecial=false,hasRear=false;
    if(!j.is_object())return false;
    for(const auto& [key,v]:j.items()) {
        if(key=="masterEnabled") {if(!v.is_boolean())return false;next.masterEnabled=v.get<bool>();}
        else if(key=="enabled") {if(!v.is_boolean())return false;next.enabled=v.get<bool>();}
        else if(key=="summonEnabled" || key=="specialEnabled" || key=="rearEnabled") {
            if(!v.is_boolean())return false;const bool value=v.get<bool>();
            if(key=="summonEnabled"){legacySummon=true;next.summonEnabled=value;}
            else if(key=="specialEnabled"){legacySpecial=true;next.specialEnabled=value;}
            else {legacyRear=true;next.rearEnabled=value;}
        } else if(key=="summonPosition") {
            if(!v.is_string() || !parseSummonPosition(v.get<std::string>(),next.summonPosition))return false;hasSummon=true;
        } else if(key=="standaloneSpecialMode") {
            if(!v.is_string() || !parseStandaloneSpecial(v.get<std::string>(),next.standaloneSpecialMode))return false;
        } else if(key=="touchFeedback") {
            if(!v.is_string() || !parseTouchFeedback(v.get<std::string>(),next.touchFeedback))return false;
        } else if(key=="autoHideSeconds") {
            if(!v.is_number_integer())return false;const auto n=v.get<long long>();
            if(!validAutoHideSeconds(n))return false;next.autoHideSeconds=(int)n;
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
    return ymcc::controllerPersona::screenProfile(persona);
}
inline const char* profileName(int index) {
    return ymcc::controllerPersona::screenPersona(index);
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
    explicit ProfileBank(const Json& savedSection=Json::object()) {
        Json section=savedSection;ymcc::controllerPersona::migrateSavedScreenProfiles(section);
        for(int i=0;i<4;i++) slots[i]=profileDefault(i);
        const auto readSaved=[](const Json& saved,Config& slot) {
            Config seed=slot;
            // Before layouts existed, a saved profile was dual (or disabled).
            // Do not reinterpret old PS5 settings as the new single-pad preset.
            if(saved.is_object() && !saved.contains("layout")) {seed.enabled=false;seed.layout=Layout::Dual;}
            // Retired experiment: discard only its old saved field. Do not
            // reject the whole profile and lose the user's other settings.
            if(saved.is_object() && saved.contains("deckSmoothing")) {
                Json migrated=saved;migrated.erase("deckSmoothing");parseConfig(migrated,seed,slot);
            } else parseConfig(saved,seed,slot);
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
inline constexpr LONG kControlBottomClearance=12,kControlHorizontalGap=10,kControlVerticalGap=40,kControlHeight=54,kControlLiftPercent=30;
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
struct Contact { bool down=false, click=false; short x=0,y=0; unsigned short pressure=0; bool operator==(const Contact&)const noexcept=default; };
struct Snapshot { bool present=false; Contact left{},right{}; };
inline Json snapshotJson(const Snapshot& s) {
    auto pad=[](const Contact& p)->Json {
        Json value = Json::object();
        value["active"] = p.down;
        value["click"] = p.click;
        value["x"] = p.down ? p.x : 0;
        value["y"] = p.down ? p.y : 0;
        value["pressure"] = p.down ? p.pressure : 0;
        return value;
    };
    Json value = Json::object();
    value["left"] = pad(s.left);
    value["right"] = pad(s.right);
    return value;
}
// POINTER_INFO.dwTime is a wrapping 32-bit event tick. Expand it in the
// current 64-bit uptime epoch; reject future/ambiguous values rather than
// manufacturing a days-long hold. Delivery latency must not turn a real
// long press into a tap when DOWN and UP are drained in the same UI turn.
inline ULONGLONG pointerEventTick(DWORD sourceTime,ULONGLONG dispatchTick) {
    const DWORD age=(DWORD)dispatchTick-sourceTime;
    return age<0x80000000u && (ULONGLONG)age<=dispatchTick?dispatchTick-age:dispatchTick;
}
struct Action { unsigned keys=0; int dx=0,dy=0; bool heldClick=false,tap=false; unsigned hapticPulses=0,hapticContacts=0; int hapticStrengthPercent=0; };
using TouchHapticPulseSink = void (*)(unsigned contacts,bool pulse,int strength);
inline std::atomic<TouchHapticPulseSink> touchHapticPulseSink{nullptr};
inline ymcc::touchhaptic::FeedbackOnlyContacts feedbackOnlyContacts; // Guarded by mutex below.
inline bool* feedbackOnlyForegroundFixture=nullptr; // Dry-run fixture only; never installed by startup.
inline ymcc::touchhaptic::EventGate touchHapticEventGate; // HWND owner thread only.
inline void setTouchHapticPulseSink(TouchHapticPulseSink sink) {
    const auto previous=touchHapticPulseSink.exchange(sink,std::memory_order_acq_rel);
    touchHapticEventGate.reset();if(previous&&previous!=sink)previous(0,false,0); // revoke old pending work, never replay through a new sink
}
struct Pad {
    UINT32 id=0; bool down=false,click=false; float x=0,y=0,lastX=0,lastY=0,startX=0,startY=0;
    double remainderX=0,remainderY=0; float travelSquared=0; ymcc::touchhaptic::Motion haptic; ULONGLONG started=0; unsigned keys=0;
};
struct TapClick { bool active=false,published=false; float x=0,y=0; ULONGLONG until=0; };
// Fixed-size pure state machine shared by production and simulation tests.
class Core {
public:
    Config config{}; std::array<Pad,2> pads{}; std::array<TapClick,2> clicks{};
    bool nativeOutputBlocked=false; ULONGLONG nativeReadyAt=0;
    Action cancel() { pads={}; clicks={}; return {}; }
    Action current() const {
        Action a;if(!config.masterEnabled)return a;const int strength=config.enabled?touchFeedbackStrength(config.touchFeedback):0;
        for(int i=0;i<2;i++) {
            const auto& p=pads[i];a.keys|=p.keys;if(!p.down)continue;
            const auto m=mode(i);if(strength&&m!=Mode::Off)a.hapticContacts|=1u<<i;
            if(m==Mode::Mouse&&p.click)a.heldClick=true;
        }
        if(a.hapticContacts)a.hapticStrengthPercent=strength;return a;
    }
    Mode mode(int side) const { return surfaceMode(config,side); }
    Action event(int side, UINT32 id, int phase, float x,float y, int width,int height,ULONGLONG now,ULONGLONG delivered=0) {
        // Successful events publish current state once, after mutating the contact.
        // Rejected events still return the complete unchanged state.
        Action a;const auto eventMode=side>=0&&side<2?mode(side):Mode::Off;
        const int feedbackStrength=touchFeedbackStrength(config.touchFeedback);
        if(side<0 || side>1 || phase<0 || phase>2 || !std::isfinite(x) || !std::isfinite(y) ||
            width<=0 || height<=0 || !config.masterEnabled || !config.enabled || eventMode==Mode::Off) return current();
        // Recheck under the adapter mutex: a lifecycle failure may have arrived
        // after handlePointer selected its route. UPDATE/UP never create a contact.
        if(nativePad(eventMode) && (nativeOutputBlocked || (phase==0 && now<nativeReadyAt)))return current();
        Pad& p=pads[side];
        if(phase==0) {
            if(p.down) return current(); // One independent contact per Deck pad; never swap fingers.
            // A new Deck gesture must never inherit an earlier 60ms tap's
            // coordinates, click bit or pressure, even if BUS delivery was late.
            // Prefer dropping an undelivered old tap to pressing during a swipe.
            if(eventMode==Mode::Deck) clicks[side]={};
            p={}; p.id=id;p.down=true;p.started=now;
            p.x=p.lastX=p.startX=std::clamp(x,0.f,(float)width);
            p.y=p.lastY=p.startY=std::clamp(y,0.f,(float)height);
            if(feedbackStrength>0) {
                p.haptic.begin(p.x,p.y,width,height,now);a.hapticPulses=1;
            }
            // The whole pad is one gesture surface; no bottom hold/drag zone.
        } else {
            if(!p.down || p.id!=id) return current();
            if(phase==2) {
                const float limit=std::max(1.f,std::min(width,height)*0.03f);
                const float upDx=x-p.startX,upDy=y-p.startY;
                const bool tap=now>=p.started && now-p.started<=250 &&
                    std::max(p.travelSquared,upDx*upDx+upDy*upDy)<limit*limit;
                if(tap && nativePad(eventMode))
                    clicks[side]={true,false,p.x,p.y,std::max(now,delivered)+60}; // Classify by source time; publish for a full delivery-time pulse.
                p={}; a=current();a.tap=tap && eventMode==Mode::Mouse;return a;
            }
            const float nextX=std::clamp(x,0.f,(float)width),nextY=std::clamp(y,0.f,(float)height);
            p.x=nextX;p.y=nextY;
        }
        // Directional keyboard mappings never classify taps. Other modes compare
        // squared displacement, retaining the maximum even after returning home.
        if(eventMode!=Mode::Wasd && eventMode!=Mode::Arrows) {
            const float fromX=p.x-p.startX,fromY=p.y-p.startY;
            p.travelSquared=std::max(p.travelSquared,fromX*fromX+fromY*fromY);
        }
        if(phase==1 && feedbackStrength>0 && p.haptic.move(p.x,p.y,now))
            a.hapticPulses=1;
        if(eventMode==Mode::Wasd || eventMode==Mode::Arrows) {
            const float nx=2*p.x/std::max(width,1)-1, ny=2*p.y/std::max(height,1)-1;
            // Independent axial Schmitt triggers permit diagonals without boundary chatter.
            const unsigned bank=p.keys >> (eventMode==Mode::Arrows?4:0);
            auto on=[&](float v,unsigned bit){return v>((bank & bit)?0.16f:0.22f);};
            unsigned bits=0;if(on(-ny,1))bits|=1;if(on(ny,2))bits|=2;
            if(on(-nx,4))bits|=4;if(on(nx,8))bits|=8;
            p.keys=bits << (eventMode==Mode::Arrows?4:0);
        }
        const unsigned hapticPulses=a.hapticPulses;
        a=current();
        a.hapticPulses=hapticPulses;
        if(eventMode==Mode::Mouse && phase==1) {
            const double gain=config.mouseSensitivity/100.0;
            p.remainderX+=(p.x-p.lastX)*gain;p.remainderY+=(p.y-p.lastY)*gain;
            a.dx=(int)p.remainderX;a.dy=(int)p.remainderY;
            p.remainderX-=a.dx;p.remainderY-=a.dy;
        }
        p.lastX=p.x;p.lastY=p.y;return a;
    }
    struct Projection {bool valid=false,click=false;float x=0,y=0;Contact contact{};Mode mode=Mode::Off;};
    std::array<Projection,2> projections{};
    Snapshot snapshot(ULONGLONG now=0) {
        Snapshot s;if(!config.masterEnabled || nativeOutputBlocked)return s;
        for(int i=0;i<2;i++) {
            auto& tap=clicks[i];
            // Expire in the existing BUS snapshot path, without a new timer.
            // Even after a delayed tick, expose at least one pressed report.
            if(tap.active && tap.published && now>=tap.until) tap={};
            if(!config.enabled || !nativePad(mode(i)) || (!pads[i].down && !tap.active)) continue;
            const Pad& p=pads[i];auto& cached=projections[i];const auto contactMode=mode(i);
            const float x=tap.active?tap.x:p.x,y=tap.active?tap.y:p.y;
            if(!cached.valid||cached.click!=tap.active||cached.x!=x||cached.y!=y||cached.mode!=contactMode){
                Contact c;c.down=true;c.click=tap.active;
                c.x=(short)std::clamp(std::lround(x),-32768L,32767L);
                c.y=(short)std::clamp(std::lround(y),-32768L,32767L);
                // Deck touch (bits 19/20), click (17/18), and force (56/58)
                // are independent in Valve's protocol. A screen swipe supplies
                // touch + XY only, not artificial force that can activate bindings.
                c.pressure=c.click?32767:(contactMode==Mode::Deck?0:8192);
                cached={true,tap.active,x,y,c,contactMode};
            }
            (i==0?s.left:s.right)=cached.contact;s.present=true;
            if(tap.active) tap.published=true;
        }return s;
    }
};
inline std::mutex mutex;
inline Core core;
inline std::atomic<bool> available{false}, psAvailable{false}, enabled{false}, nativeMode{false};
inline std::atomic<HWND> owner{nullptr};
inline std::atomic<int> activeProfile{0}, appliedProfile{0};
inline HINSTANCE instance=nullptr;
struct NativeTransportObservation {ULONGLONG epoch=0;bool ready=false;};
struct NativeTransportState {
    NativeTransportObservation last{};bool observed=false;
    unsigned long long changes=0,discardedGestures=0;
};
inline NativeTransportState nativeTransport; // guarded by mutex; no timer/worker
inline void observeNativeTransportLocked(NativeTransportObservation next) {
    next.ready=next.ready && next.epoch!=0;
    if(nativeTransport.observed && nativeTransport.last.epoch==next.epoch && nativeTransport.last.ready==next.ready)return;
    nativeTransport.observed=true;nativeTransport.last=next;++nativeTransport.changes;
    core.nativeOutputBlocked=!next.ready;
    if(next.ready)core.nativeReadyAt=GetTickCount64();
    bool changed=false;
    const bool shared=core.config.layout==Layout::Single && core.config.single==Mode::DualSense;
    for(int i=0;i<2;++i)if(nativePad(core.mode(i))) {
        auto& p=core.pads[i];
        if(p.down) {
            // Keep physical touch/haptic ownership through UP, but never carry
            // its coordinates/click into the replacement or recovered target.
            feedbackOnlyContacts.adopt(i,shared?0:i,p.id,p.haptic,p.x,p.y);
            ++nativeTransport.discardedGestures;changed=true;
        }
        changed=changed||core.clicks[i].active;p={};core.clicks[i]={};
    }
    // Lifecycle/BUS threads never draw or inject. The UI re-evaluates its current
    // cursor/outline state; a delayed notification cannot cancel a newer gesture.
    if(changed)if(HWND h=owner.load(std::memory_order_acquire))PostMessageW(h,kNativeOutputResetMessage,0,0);
}
inline void observeNativeTransport(ULONGLONG epoch,bool ready) {
    std::lock_guard<std::mutex> l(mutex);observeNativeTransportLocked({epoch,ready});
}
// A no-redirection input host is visually empty but retains touch hit-testing.
// The separate outline has per-pixel alpha and is always click-through.
inline std::array<HWND,2> windows{}, outlines{};
inline BYTE outlineAlpha(int transparency) {return (BYTE)(255*(100-transparency)/100);}
inline int outlineFeedbackTransparency(int transparency,bool pressed) noexcept {
    const int visible=100-std::clamp(transparency,0,100);
    // Settings store transparency; lift visibility by 50%, capped at 100%.
    return 100-(pressed?std::min(100,visible*3/2):visible);
}
inline bool surfacePressed(int surface) {
    if(surface<0||surface>1)return false;
    bool pressed=false;
    {std::lock_guard<std::mutex> l(mutex);
        if(core.config.masterEnabled && core.config.enabled && (core.config.layout!=Layout::Single || surface==0)) {
            const bool shared=core.config.layout==Layout::Single && core.config.single==Mode::DualSense;
            pressed=core.pads[surface].down || (shared && core.pads[1].down);
            // Visual contact feedback also works on the own-UI route and with
            // vibration disabled; a single Sony surface remains lit for either finger.
            for(const auto& contact:feedbackOnlyContacts.contacts)
                pressed=pressed || (contact.active && contact.surface==surface);
        }
    }
    return pressed;
}
inline int surfaceOutlineTransparency(int surface,int transparency) {
    return outlineFeedbackTransparency(transparency,surfacePressed(surface));
}
// All auto-hide state belongs to the HWND owner thread, never BUS snapshot().
struct AutoHideState {
    bool hidden=false;unsigned visibleMask=0;int seconds=60;
    std::array<UINT32,2> specialContacts{}; // Physical holds, including foreground-suppressed keys.
    ULONGLONG lastTouch=0;HWND timerWindow=nullptr;UINT_PTR timer=0,serial=0x59540000;
    unsigned long long hides=0,wakes=0;
};
inline AutoHideState autoHide;
inline void autoHideSync(bool reset=false);
inline void autoHideTouch();
inline void autoHideStop();
struct AutoHideTouchScope {
    bool enabled=true;
    ~AutoHideTouchScope(){if(enabled)autoHideTouch();}
};
inline void syncOutlineFeedback(int surface=-1);
inline void syncContactVisuals(int surface=-1,bool force=false);
inline void shutdownContactVisuals();
inline std::array<SIZE,2> sizes{};
struct OutlineUpload { bool ready=false,interiorClear=false; DWORD centre=0,edge=0; int width=0,height=0,transparency=-1; bool pressed=false;int padding=0; };
inline unsigned long long outlineUploadCount=0,outlineCacheHits=0,outlinePositionMoves=0,outlineBitmapBuilds=0;
// Two owner-thread resources. Geometry is immutable between resize events;
// press/release and transparency changes reuse it with SourceConstantAlpha.
struct OutlineBitmap {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;
    DWORD* pixels=nullptr;int width=0,height=0;
    OutlineBitmap()=default;OutlineBitmap(const OutlineBitmap&)=delete;OutlineBitmap& operator=(const OutlineBitmap&)=delete;
    void reset() noexcept {
        if(dc&&previous)SelectObject(dc,previous);
        if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);
        dc=nullptr;bitmap=nullptr;previous=nullptr;pixels=nullptr;width=height=0;
    }
    ~OutlineBitmap(){reset();}
};
inline std::array<OutlineBitmap,2> outlineBitmaps{};
inline std::array<std::array<OutlineBitmap,2>,2> outlineFrames{};
inline std::array<OutlineUpload,2> outlineUploads{};
// HWND owner thread: count-based inserted-prefix ownership, not consumer receipt.
inline unsigned sentKeys=0; inline bool sentClick=false,dryRun=false;
// Configure on the owner thread before use. Synchronous observation only: no
// mutation/reentry; INPUT is borrowed until return. simulated means no OS send.
// error is the raw post-send GetLastError on short sends (possibly zero/stale),
// zero on full/simulated sends. The callback must not allocate, log or throw.
inline void (*outputEvidenceObserver)(const INPUT*,UINT requested,UINT inserted,DWORD error,bool simulated) noexcept=nullptr;
inline unsigned long long eventCount=0, injectedCount=0, repaintCount=0;
inline DWORD inputError=0,windowError=0;
inline bool initialized=false,inputTeardown=false; // HWND owner thread only.
// Fixed, terminal-only evidence. Original handles survive nulling of owner
// arrays, so a failed DestroyWindow cannot be mistaken for successful cleanup.
inline std::array<HWND,32> exitInputWindows{};
inline size_t exitInputWindowCount=0;
struct ExitInputCheck {unsigned keys=0,buttons=0,liveWindows=0,openSources=0;bool mouse=false,contacts=false;DWORD error=0;bool ok=false;};
inline constexpr unsigned kExitInputRequiredCleanRounds=3;
inline std::array<ExitInputCheck,6> exitInputChecks{};
inline unsigned exitInputCheckCount=0;
inline void rememberExitInputWindow(HWND h) {
    if(!h)return;for(size_t i=0;i<exitInputWindowCount;++i)if(exitInputWindows[i]==h)return;
    if(exitInputWindowCount<exitInputWindows.size())exitInputWindows[exitInputWindowCount++]=h;
}
inline unsigned buttonsOpenInputSources();
inline void (*visibilityRefreshObserver)()=nullptr; // Owner adapter only; no callback in pure Core tests.
inline bool nativeInputCandidateRequested=false;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
inline ymcc::screeninput::Runtime nativeInputRuntime;
inline std::array<ymcc::screeninput::Surface,2> nativeInputSurfaces;
#endif
inline void nativeInputCancel() {
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    for(auto& surface:nativeInputSurfaces)surface.cancel();buttonsNativeInputCancel();
#endif
}
inline void nativeInputClose(int side) {
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    if(side>=0&&side<2)nativeInputSurfaces[side].close();
#endif
}
inline void setNativeInputCandidate(bool value) {
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    nativeInputCandidateRequested=value;if(!value){for(auto& surface:nativeInputSurfaces)surface.close();buttonsNativeInputDisable();}
#else
    nativeInputCandidateRequested=false;
#endif
}
inline Json* testTrace=nullptr; // Only the self-test sets this; no production trace allocations.
#include "screen_touchpad_cursor.h"
inline Config getConfig() {std::lock_guard<std::mutex> l(mutex);return core.config;}
#include "screen_standalone_special.h"
inline std::atomic<bool> radialHapticContact{false};
inline std::atomic<bool> monitorHapticContact{false}; // UI-thread owner, merged with the existing pad/button dispatch.
inline void outputHaptic(const Action& a) {
    if(dryRun&&!feedbackOnlyForegroundFixture)return;
    const auto sink=touchHapticPulseSink.load(std::memory_order_acquire);if(!sink)return;
    unsigned contacts=0;int strength=0;
    {std::lock_guard<std::mutex> l(mutex);strength=touchFeedbackStrength(core.config.touchFeedback);
        // Bits 0/1 belong to pads; 2..8 belong to physical button contacts.
        // A pad UP must not revoke a held button, or vice versa. Off revokes all.
        if(strength)contacts=a.hapticContacts|feedbackOnlyContacts.mask()|core.current().hapticContacts|buttonFeedbackMask();}
    if(radialHapticContact.load(std::memory_order_relaxed)&&strength)contacts|=1u<<10;
    if(monitorHapticContact.load(std::memory_order_relaxed)&&strength)contacts|=1u<<9;
    const bool pulse=contacts && a.hapticPulses!=0;
    if(touchHapticEventGate.publish(contacts,pulse,strength))sink(contacts,pulse,strength);
}
inline void radialHaptic(bool contact,bool pulse){radialHapticContact.store(contact,std::memory_order_relaxed);Action action;action.hapticPulses=pulse?1u:0u;outputHaptic(action);}
inline void monitorHaptic(bool contact,bool pulse) {
    monitorHapticContact.store(contact,std::memory_order_relaxed);Action action;action.hapticPulses=pulse?1u:0u;outputHaptic(action);
}
inline void output(const Action& requested) {
    const Action neutral{};
    const Action& a=inputTeardown?neutral:requested; // Terminal release cannot replay a queued down/MOVE.
    outputHaptic(a);
    // Native-only moves and unchanged keys/clicks have no OS input batch.
    // Keep haptic publication above this gate, including contact release/pulses.
    const unsigned changedKeys=(sentKeys^a.keys)&0xffu;
    const bool sendTap=a.tap && !a.heldClick && !sentClick;
    if(!changedKeys && !a.dx && !a.dy && a.heldClick==sentClick && !sendTap)return;
    static constexpr WORD vk[8]={'W','S','A','D',VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT};
    INPUT input[12]{};unsigned keyBits[12]{};UINT n=0;
    for(int i=0;i<8;i++) if(changedKeys&(1u<<i)) {
        keyBits[n]=1u<<i;
        INPUT& p=input[n++];p.type=INPUT_KEYBOARD;p.ki.wScan=(WORD)MapVirtualKeyW(vk[i],MAPVK_VK_TO_VSC);
        p.ki.dwFlags=KEYEVENTF_SCANCODE|((a.keys&(1u<<i))?0:KEYEVENTF_KEYUP)|
            (i>=4?KEYEVENTF_EXTENDEDKEY:0);p.ki.dwExtraInfo=kInjectedTag;
    }
    if(a.dx || a.dy) { INPUT& p=input[n++];p.type=INPUT_MOUSE;p.mi.dx=a.dx;p.mi.dy=a.dy;
        p.mi.dwFlags=MOUSEEVENTF_MOVE;p.mi.dwExtraInfo=kInjectedTag; }
    if(a.heldClick!=sentClick) {INPUT& p=input[n++];p.type=INPUT_MOUSE;
        p.mi.dwFlags=a.heldClick?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP;p.mi.dwExtraInfo=kInjectedTag;}
    if(sendTap) for(DWORD flag:{MOUSEEVENTF_LEFTDOWN,MOUSEEVENTF_LEFTUP}) {
        INPUT& p=input[n++];p.type=INPUT_MOUSE;p.mi.dwFlags=flag;p.mi.dwExtraInfo=kInjectedTag;
    }
    if(!n) return;
    DWORD failure=0;
    const auto sendBatch=[&]() {
        const bool simulated=dryRun;
        const UINT inserted=simulated?n:SendInput(n,input,sizeof(INPUT));
        const DWORD lastError=GetLastError(); // Capture before any observer call.
        // SendInput is serial: account its count as an inserted prefix. This is
        // sender-side ownership, not proof that a hook/application consumed it.
        const UINT accepted=std::min(inserted,n);
        for(UINT i=0;i<accepted;++i) {
            if(keyBits[i]) {
                if(input[i].ki.dwFlags&KEYEVENTF_KEYUP)sentKeys&=~keyBits[i];
                else sentKeys|=keyBits[i];
            } else if(input[i].type==INPUT_MOUSE) {
                if(input[i].mi.dwFlags&MOUSEEVENTF_LEFTDOWN)sentClick=true;
                if(input[i].mi.dwFlags&MOUSEEVENTF_LEFTUP)sentClick=false;
            }
        }
        injectedCount+=accepted;
        if(inserted!=n && !failure)failure=lastError?lastError:ERROR_ACCESS_DENIED;
        if(const auto observer=outputEvidenceObserver) {
            observer(input,n,inserted,simulated||inserted==n?0:lastError,simulated);
            SetLastError(lastError);
        }
        return inserted==n;
    };
    if(!sendBatch()) {
        // At most one compensating batch per output(), only for our inserted
        // downs no longer desired. Never replay movement or an uninserted down.
        // Failed ups stay owned for subsequent input/release/lifecycle calls.
        const unsigned releaseKeys=sentKeys&~a.keys&0xffu;
        n=0;
        for(int i=0;i<8;++i) if(releaseKeys&(1u<<i)) {
            keyBits[n]=1u<<i;
            INPUT& p=input[n++];p={};p.type=INPUT_KEYBOARD;p.ki.wScan=(WORD)MapVirtualKeyW(vk[i],MAPVK_VK_TO_VSC);
            p.ki.dwFlags=KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP|(i>=4?KEYEVENTF_EXTENDEDKEY:0);p.ki.dwExtraInfo=kInjectedTag;
        }
        if(sentClick && !a.heldClick) {
            keyBits[n]=0;
            INPUT& p=input[n++];p={};p.type=INPUT_MOUSE;p.mi.dwFlags=MOUSEEVENTF_LEFTUP;p.mi.dwExtraInfo=kInjectedTag;
        }
        if(n)sendBatch();
    }
    inputError=failure;
}
inline void releaseMappedState() {
    buttonsReleaseMapped();cursorStop();
    bool changed=sentKeys!=0 || sentClick;Action a;
    {std::lock_guard<std::mutex> l(mutex);changed=changed || core.pads[0].down || core.pads[1].down || core.clicks[0].active || core.clicks[1].active;a=core.cancel();}
    output(a);syncOutlineFeedback();syncContactVisuals();if(changed) for(HWND h:windows) if(h) InvalidateRect(h,nullptr,FALSE);
}
inline void release() {
    {std::lock_guard<std::mutex> l(mutex);feedbackOnlyContacts.cancel();}
    nativeInputCancel();buttonsRelease();releaseMappedState();autoHide.specialContacts={};autoHideSync();
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
struct NormalizedProjection {Contact input{},output{};int width=0,height=0,half=-2;bool valid=false;};
inline std::array<NormalizedProjection,2> normalizedProjections{}; // Guarded by mutex; tap expiry still runs every read.
inline Snapshot snapshot(const NativeTransportObservation* transport=nullptr) {
    cursorRequestBeforeReport(); // Deck uses the same BUS request after its report attempt.
    const bool usable=enabled.load(std::memory_order_relaxed) && nativeMode.load(std::memory_order_relaxed) &&
        (available.load(std::memory_order_acquire) || psAvailable.load(std::memory_order_acquire)) && activeProfile.load()==appliedProfile.load();
    // Inactive snapshots retain their atomic-only fast path. Explicit fault/stop
    // edges revoke state separately; the next usable read observes its target
    // under this mutex before exposing any contact.
    if(!usable)return {};
    std::lock_guard<std::mutex> l(mutex);
    if(transport)observeNativeTransportLocked(*transport);
    Snapshot s=core.snapshot(GetTickCount64());
    // Normalize either square pads or both contacts on the same wide surface.
    const auto mode=surfaceMode(core.config,0);
    if((mode==Mode::DualSense && !psAvailable.load()) || (mode==Mode::Deck && !available.load())) return {};
    for(int i=0;i<2;i++) {
        auto& c=i==0?s.left:s.right;if(!c.down) continue;
        const auto contactMode=surfaceMode(core.config,i);
        if((contactMode==Mode::Deck && !available.load()) || (contactMode==Mode::DualSense && !psAvailable.load())){c={};continue;}
        const int half=core.config.layout==Layout::Dual&&contactMode==Mode::DualSense?i:-1;
        auto& cached=normalizedProjections[i];
        if(!cached.valid||cached.input!=c||cached.width!=sizes[i].cx||cached.height!=sizes[i].cy||cached.half!=half){
            cached={c,normalizeContact(c,sizes[i].cx,sizes[i].cy,half),(int)sizes[i].cx,(int)sizes[i].cy,half,true};
        }
        c=cached.output;
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
    targetEnabled=targetEnabled && ymcc::controllerPersona::currentOutput(persona);
    const bool controlChanged=controlTargetEnabled.exchange(targetEnabled)!=targetEnabled;
    const bool edgeChanged=controlEdgeEnabled.exchange(persona=="dualsense-edge" && targetEnabled)!=(persona=="dualsense-edge" && targetEnabled);
    const int next=std::max(0,profileIndex(persona));
    const bool changed=activeProfile.exchange(next)!=next;
    const bool deck=next==1 && targetEnabled,ps=next==2 && targetEnabled;
    const bool deckChanged=available.exchange(deck)!=deck,psChanged=psAvailable.exchange(ps)!=ps;
    const bool availabilityChanged=deckChanged || psChanged || controlChanged || edgeChanged;
    if(changed || availabilityChanged) {screenButtonMask.store(0,std::memory_order_release);standaloneCancel();}
    if(changed || availabilityChanged) if(HWND h=owner.load()) PostMessageW(h,kRefreshMessage,0,0);
}
inline bool permitted(Mode mode) {return mode!=Mode::Off && (mode!=Mode::Deck || available.load()) && (mode!=Mode::DualSense || psAvailable.load());}
// Premultiplied BGRA for UpdateLayeredWindow/AC_SRC_ALPHA. Only a one-pixel
// rounded white stroke and a thin centre cross have alpha; the background stays clear.
inline bool outlineCrossPixel(int x,int y,int width,int height) noexcept {
    const int inset=std::max(12,std::min(width,height)/10);
    return (x==width/2 && y>=inset && y<height-inset) ||
        (y==height/2 && x>=inset && x<width-inset);
}
// Shared border only: virtual buttons must not inherit touchpad decorations.
inline DWORD outlineBorderPixel(int x,int y,int width,int height,int transparency) {
    if(x>=12 && x<width-12 && y>=12 && y<height-12) return 0;
    const double radius=std::min(10.0,std::min(width,height)/2.0-2.0);
    const double qx=std::abs(x+0.5-width/2.0)-(width/2.0-1.5-radius);
    const double qy=std::abs(y+0.5-height/2.0)-(height/2.0-1.5-radius);
    const double distance=std::hypot(std::max(qx,0.0),std::max(qy,0.0))+std::min(std::max(qx,qy),0.0)-radius;
    const auto alpha=(DWORD)std::lround(outlineAlpha(transparency)*std::clamp(1.0-std::abs(distance),0.0,1.0));
    if(!alpha) return 0;
    return (alpha<<24)|(alpha<<16)|(alpha<<8)|alpha;
}
// Only the touchpad renderer adds the internal cross.
inline DWORD outlinePixel(int x,int y,int width,int height,int transparency) {
    if(outlineCrossPixel(x,y,width,height)) {
        const DWORD alpha=outlineAlpha(transparency)/2;
        return (alpha<<24)|(alpha<<16)|(alpha<<8)|alpha;
    }
    return outlineBorderPixel(x,y,width,height,transparency);
}
#include "screen_touchpad_press_visual.h"
inline bool renderOutline(int side,int transparency,bool force=false) {
    if(!windows[side] || !outlines[side]) return false;
    const bool pressed=surfacePressed(side);
    transparency=outlineFeedbackTransparency(transparency,pressed);
    RECT rc{};if(!GetWindowRect(windows[side],&rc)) return false;
    const int width=rc.right-rc.left,height=rc.bottom-rc.top;
    if(width<=0 || height<=0) return false;
    const int padding=pressGlowPadding(getConfig().scale),frameWidth=width+padding*2,frameHeight=height+padding*2;
    const auto& prior=outlineUploads[side];
    if(!force && prior.ready && prior.width==width && prior.height==height && prior.transparency==transparency && prior.pressed==pressed && prior.padding==padding) {
        // Windows retains the last layered bitmap. Move that frame without
        // replacing it with an empty/identical surface or reallocating GDI.
        RECT visual{};if(!GetWindowRect(outlines[side],&visual)){windowError=GetLastError();return false;}
        if(visual.left!=rc.left-padding || visual.top!=rc.top-padding || visual.right-visual.left!=frameWidth || visual.bottom-visual.top!=frameHeight) {
            if(!SetWindowPos(outlines[side],nullptr,rc.left-padding,rc.top-padding,frameWidth,frameHeight,SWP_NOACTIVATE|SWP_NOZORDER)) {windowError=GetLastError();return false;}
            ++outlinePositionMoves;
        }
        ++outlineCacheHits;return true;
    }
    auto& image=outlineBitmaps[side];
    if(!image.dc || image.width!=width || image.height!=height) {
        for(auto& frame:outlineFrames[side])frame.reset();
        image.reset();image.dc=CreateCompatibleDC(nullptr);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
        info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr;
        if(image.dc)image.bitmap=CreateDIBSection(image.dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!image.dc || !image.bitmap || !bits) {
            windowError=GetLastError();if(!windowError)windowError=ERROR_NOT_ENOUGH_MEMORY;
            image.reset();outlineUploads[side]={};return false;
        }
        const auto previous=SelectObject(image.dc,image.bitmap);
        if(!previous || previous==HGDI_ERROR) {
            windowError=GetLastError();if(!windowError)windowError=ERROR_INVALID_HANDLE;
            image.reset();outlineUploads[side]={};return false;
        }
        image.previous=previous;image.pixels=static_cast<DWORD*>(bits);image.width=width;image.height=height;
        std::fill_n(image.pixels,(size_t)width*height,0u);
        // The interior is already zero. Rasterize only the narrow border once,
        // instead of running a shape calculation for every pixel on every tap.
        for(int y=0;y<height;++y) {
            const bool middle=y>=12 && y<height-12 && width>24;
            const int firstEnd=middle?12:width;
            for(int x=0;x<firstEnd;++x)image.pixels[(size_t)y*width+x]=outlinePixel(x,y,width,height,0);
            if(middle)for(int x=width-12;x<width;++x)image.pixels[(size_t)y*width+x]=outlinePixel(x,y,width,height,0);
        }
        const int inset=std::max(12,std::min(width,height)/10);
        for(int y=inset;y<height-inset;++y)image.pixels[(size_t)y*width+width/2]=outlinePixel(width/2,y,width,height,0);
        for(int x=inset;x<width-inset;++x)image.pixels[(size_t)(height/2)*width+x]=outlinePixel(x,height/2,width,height,0);
        ++outlineBitmapBuilds;
    }
    if(!buildPressFrame(image,outlineFrames[side][0],padding,false) || !buildPressFrame(image,outlineFrames[side][1],padding,true))return false;
    auto& frame=outlineFrames[side][pressed?1:0];
    POINT destination{rc.left-padding,rc.top-padding},source{};SIZE size{frameWidth,frameHeight};
    BLENDFUNCTION blend{AC_SRC_OVER,0,outlineAlpha(transparency),AC_SRC_ALPHA};
    const bool ok=UpdateLayeredWindow(outlines[side],nullptr,&destination,&size,frame.dc,&source,0,&blend,ULW_ALPHA)!=FALSE;
    outlineUploads[side]={ok,outlineAlpha(transparency)==0,outlinePixel(width/2,height/2,width,height,transparency),
        outlinePixel(1,height/2,width,height,transparency),width,height,transparency,pressed,padding};
    ++outlineUploadCount;
    if(!ok) {windowError=GetLastError();if(!windowError)windowError=ERROR_INVALID_DATA;}
    ++repaintCount;return ok;
}
inline void syncOutlineFeedback(int surface) {
    if(surface>1 || (!outlines[0] && !outlines[1]))return;
    const int transparency=getConfig().transparency;
    const int first=surface<0?0:surface,last=surface<0?1:surface;
    for(int i=first;i<=last;++i) {
        if(!windows[i] || !outlines[i])continue;
        // Owner-thread DOWN/UP/cancel only. No MOVE paints, timer, BUS work,
        // or allocations when the already-uploaded effective alpha is unchanged.
        const auto& prior=outlineUploads[i];
        const bool pressed=surfacePressed(i);
        if(prior.ready && prior.pressed==pressed && prior.transparency==outlineFeedbackTransparency(transparency,pressed))continue;
        renderOutline(i,transparency);
    }
}
inline void repairButtonOutline(HWND h,bool contentLost);
inline LRESULT CALLBACK outlineProc(HWND h,UINT m,WPARAM w,LPARAM lp) {
    switch(m) {
    case WM_STYLECHANGED:case WM_ENABLE:{
        const bool contentLost=m==WM_STYLECHANGED&&w==GWL_EXSTYLE&&lp&&
            !(reinterpret_cast<const STYLESTRUCT*>(lp)->styleNew&WS_EX_LAYERED);
        if(!ymcc::screeninput::ensureDisplay(h)){ShowWindow(h,SW_HIDE);return 0;}
        if(contentLost){
            bool pad=false;for(int i=0;i<2;++i)if(outlines[i]==h){pad=true;outlineUploads[i]={};
                if(!renderOutline(i,getConfig().transparency,true))ShowWindow(h,SW_HIDE);}
            if(!pad)repairButtonOutline(h,true);
        }
        return 0;}
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_POINTERACTIVATE:return PA_NOACTIVATE;
    case WM_NCHITTEST:return HTTRANSPARENT;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    }return DefWindowProcW(h,m,w,lp);
}
#include "screen_touchpad_contact_visual.h"
// Both native input routes normalize once, then use the same permission/output core.
inline LRESULT handlePointer(HWND h,int surface,UINT m,UINT32 id,float x,float y,DWORD flags,DWORD sourceTime=0,bool timeKnown=false) {
    if(surface<0||surface>1||!std::isfinite(x)||!std::isfinite(y))return 0;int side=surface;
    if(!enabled.load() || activeProfile.load()!=appliedProfile.load()) return 0;
    Config cfg;AutoHideTouchScope activity{false};
    // Foreground only gates mapping. Haptics keep their gesture route through UP;
    // no focus changes, second Core, synthetic click or BUS output are allowed.
    const bool ownUi=dryRun&&feedbackOnlyForegroundFixture?*feedbackOnlyForegroundFixture:
        (!dryRun&&owner.load()==GetForegroundWindow());
    bool onlyRoute=false,mappedActive=false,outputBlocked=false;{std::lock_guard<std::mutex> l(mutex);
        // Read config and contact routing under the same already-required lock.
        cfg=core.config;if(!permitted(surfaceMode(cfg,surface)) || (cfg.layout==Layout::Single && surface!=0))return 0;
        onlyRoute=feedbackOnlyContacts.find(id,surface)>=0;
        activity.enabled=(m==WM_POINTERDOWN && !(flags&POINTER_FLAG_CANCELED)) || onlyRoute ||
            (core.pads[0].down && core.pads[0].id==id) || (core.pads[1].down && core.pads[1].id==id);
        outputBlocked=nativePad(surfaceMode(cfg,surface)) && core.nativeOutputBlocked;
        mappedActive=core.pads[0].down||core.pads[1].down||core.clicks[0].active||core.clicks[1].active||sentKeys||sentClick;
    }
    if(ownUi||onlyRoute||outputBlocked){
        if((ownUi&&!onlyRoute)||(ownUi&&mappedActive)){
            {std::lock_guard<std::mutex> l(mutex);
                const bool shared=cfg.layout==Layout::Single&&cfg.single==Mode::DualSense;
                for(int i=0;i<2;++i)if(core.pads[i].down)
                    feedbackOnlyContacts.adopt(i,shared?0:i,core.pads[i].id,core.pads[i].haptic,core.pads[i].x,core.pads[i].y);}
            releaseMappedState();
        }
        Action feedback;{std::lock_guard<std::mutex> l(mutex);
            const auto tick=GetTickCount64();
            const int phase=(flags&POINTER_FLAG_CANCELED)||m==WM_POINTERUP?2:m==WM_POINTERDOWN?0:1;
            const int strength=touchFeedbackStrength(cfg.touchFeedback);
            const bool pulse=feedbackOnlyContacts.event(surface,id,phase,x,y,(int)sizes[surface].cx,(int)sizes[surface].cy,
                timeKnown?pointerEventTick(sourceTime,tick):tick,cfg.layout==Layout::Single&&cfg.single==Mode::DualSense,strength>0);
            if(strength){feedback.hapticContacts=feedbackOnlyContacts.mask();feedback.hapticPulses=pulse?1u:0u;feedback.hapticStrengthPercent=strength;}
        }
        ++eventCount;outputHaptic(feedback);syncContactVisuals(surface);
        if(m!=WM_POINTERUPDATE || (flags&POINTER_FLAG_CANCELED))syncOutlineFeedback(surface);
        return 0;
    }
    if(m==WM_POINTERDOWN&&!(cfg.layout==Layout::Single&&cfg.single==Mode::DualSense)){
        std::lock_guard<std::mutex> l(mutex);if(feedbackOnlyContacts.mask()&(1u<<surface))return 0;
    }
    if(cfg.layout==Layout::Single && cfg.single==Mode::DualSense) {
        std::lock_guard<std::mutex> l(mutex);side=-1;
        for(int i=0;i<2;i++)if(core.pads[i].down && core.pads[i].id==id){side=i;break;}
        if(side<0 && m==WM_POINTERDOWN)for(int i=0;i<2;i++)if(!core.pads[i].down&&!feedbackOnlyContacts.contacts[i].active){side=i;break;}
        if(side<0)return 0;
    }
    if(flags&POINTER_FLAG_CANCELED) {
        Action a;{std::lock_guard<std::mutex> l(mutex);
            if(core.pads[side].down && core.pads[side].id==id) {core.pads[side]={};core.clicks[side]={};}a=core.current();}
        output(a);syncOutlineFeedback(surface);syncContactVisuals(surface);cursorContactChanged(h);InvalidateRect(h,nullptr,FALSE);return 0;
    }

    ULONGLONG dispatchTick=0;
    Action a;{std::lock_guard<std::mutex> l(mutex);
        dispatchTick=GetTickCount64();
        a=core.event(side,id,m==WM_POINTERDOWN?0:m==WM_POINTERUP?2:1,x,y,
            (int)sizes[side].cx,(int)sizes[side].cy,timeKnown?pointerEventTick(sourceTime,dispatchTick):dispatchTick,dispatchTick);}
    ++eventCount;output(a);syncContactVisuals(surface);
    cursorContactChanged(h,m==WM_POINTERUP,m==WM_POINTERUPDATE && surfaceMode(cfg,surface)==Mode::Deck,dispatchTick);
    if(testTrace) testTrace->push_back({{"message",m},{"side",side},{"id",id},{"flags",flags},
        {"mode",modeName(core.mode(side))},{"x",x},{"y",y},{"down",core.pads[side].down},{"keys",sentKeys},{"tick",GetTickCount64()},{"snapshot",snapshotJson(snapshot())}});
    if(m!=WM_POINTERUPDATE) {syncOutlineFeedback(surface);InvalidateRect(h,nullptr,FALSE);}
    return 0;

}
inline LRESULT handleCaptureLoss(HWND h,int surface,UINT32 id) {
    if(surface<0||surface>1)return 0;int side=surface;constexpr UINT m=WM_POINTERCAPTURECHANGED;
    AutoHideTouchScope activity{false};
    Action feedback;bool only=false;{std::lock_guard<std::mutex> l(mutex);
        const int slot=feedbackOnlyContacts.find(id,surface);only=slot>=0;
        if(only){feedbackOnlyContacts.contacts[slot]={};const int strength=touchFeedbackStrength(core.config.touchFeedback);
            if(strength){feedback.hapticContacts=feedbackOnlyContacts.mask();feedback.hapticStrengthPercent=strength;}}
    }
    if(only){activity.enabled=true;outputHaptic(feedback);syncOutlineFeedback(surface);syncContactVisuals(surface);return 0;}
    {std::lock_guard<std::mutex> l(mutex);if(core.config.layout==Layout::Single && core.config.single==Mode::DualSense)
        for(int i=0;i<2;i++)if(core.pads[i].down && core.pads[i].id==id){side=i;break;}}
    if(testTrace) testTrace->push_back({{"message",m},{"side",side},{"id",id},
        {"oldId",core.pads[side].id},{"oldDown",core.pads[side].down}});
    Action a;bool lostActive=false;{std::lock_guard<std::mutex> l(mutex);
        if(core.pads[side].down && core.pads[side].id==id) {core.pads[side]={};core.clicks[side]={};lostActive=true;}
        a=core.current();}output(a);
    // Windows can notify capture loss after a normal UP. That stale edge
    // must not erase the passive cursor retained between swipes.
    if(lostActive){activity.enabled=true;syncOutlineFeedback(surface);syncContactVisuals(surface);cursorContactChanged(h);}InvalidateRect(h,nullptr,FALSE);return 0;

}
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
inline void nativeInputSink(HWND root,const ymcc::screeninput::Event& event,void*) {
    if(inputTeardown)return;
    if(event.phase==ymcc::screeninput::Phase::Cancel){handleCaptureLoss(root,event.surface,event.id);return;}
    const UINT message=event.phase==ymcc::screeninput::Phase::Down?WM_POINTERDOWN:event.phase==ymcc::screeninput::Phase::Up?WM_POINTERUP:WM_POINTERUPDATE;
    handlePointer(root,event.surface,message,event.id,event.x,event.y,event.cancelled?POINTER_FLAG_CANCELED:0,event.sourceTime,event.timeKnown);
}
#endif
inline bool nativeInputSync(int side,bool wanted,bool force) {
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    if(!nativeInputCandidateRequested){nativeInputClose(side);return true;}
    if(!wanted){nativeInputSurfaces[side].cancel();return true;}
    auto& surface=nativeInputSurfaces[side];
    const bool ok=surface.root()==windows[side]?surface.sync(force):surface.attach(nativeInputRuntime,windows[side],side,nativeInputSink,nullptr);
    if(!ok){const auto failure=surface.error;surface.close();windowError=(DWORD)failure;}
    return ok;
#else
    return true;
#endif
}
inline LRESULT CALLBACK windowProc(HWND h,UINT m,WPARAM w,LPARAM lp) {
    if(m==WM_NCCREATE) {SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);return TRUE;}
    if(inputTeardown&&(m==WM_POINTERDOWN||m==WM_POINTERUPDATE||m==WM_POINTERUP||m==WM_POINTERCAPTURECHANGED||
       m==WM_CANCELMODE||m==WM_STYLECHANGED||m==WM_ENABLE))return 0;
    const int surface=(int)GetWindowLongPtrW(h,GWLP_USERDATA);
    int side=surface;
    switch(m) {
    case WM_WINDOWPOSCHANGED:case WM_DPICHANGED:case WM_DISPLAYCHANGE:case WM_SHOWWINDOW:case WM_NCDESTROY:
        invalidateContactGeometry(h);break; // Keep DefWindowProc's MOVE/SIZE/lifecycle handling.
    case WM_STYLECHANGED:case WM_ENABLE:
        invalidateContactGeometry(h);
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
        if(surface>=0&&surface<2&&nativeInputSurfaces[surface].root()==h&&nativeInputSurfaces[surface].policyEnabled()&&
            !nativeInputSurfaces[surface].ensureMousePass()){
            windowError=(DWORD)nativeInputSurfaces[surface].error;ShowWindow(h,SW_HIDE);
            if(outlines[surface])ShowWindow(outlines[surface],SW_HIDE);release();
        }
#endif
        return 0;
    case kCursorSyncMessage:cursorSyncPending.store(false,std::memory_order_release);cursorSync();return 0;
    case cursorvisibility::kMessage:cursorVisibilitySync(h,w);return 0;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_POINTERACTIVATE:return PA_NOACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {
        // No redirection surface and no drawing. Visual alpha never controls input.
        ++repaintCount;PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;
    }
    case WM_POINTERDOWN:case WM_POINTERUPDATE:case WM_POINTERUP: {
        // Preserve fail-closed legacy admission before querying/deferring input.
        if(surface<0||surface>1||!enabled.load()||activeProfile.load()!=appliedProfile.load())return 0;
        const auto cfg=getConfig();if(!permitted(surfaceMode(cfg,surface))||(cfg.layout==Layout::Single&&surface!=0))return 0;
        POINTER_INFO pi{};const UINT32 id=GET_POINTERID_WPARAM(w);
        if(!GetPointerInfo(id,&pi)||(pi.pointerType!=PT_TOUCH&&pi.pointerType!=PT_PEN)) {
            if(testTrace)testTrace->push_back({{"message",m},{"side",surface},{"id",id},{"error",GetLastError()}});break;
        }
        POINT point=pi.ptPixelLocation;ScreenToClient(h,&point);
        return handlePointer(h,surface,m,id,(float)point.x,(float)point.y,pi.pointerFlags,pi.dwTime,true);
    }
    case WM_POINTERCAPTURECHANGED:return handleCaptureLoss(h,surface,GET_POINTERID_WPARAM(w));
    case WM_CANCELMODE:if(testTrace)testTrace->push_back({{"message",m},{"tick",GetTickCount64()},{"cancel",true}});release();return 0;
    }
    return DefWindowProcW(h,m,w,lp);
}
inline void refresh(bool forceVisuals=false) {
    if(!initialized) return;
    // Re-evaluate the whole window set. Keep failures from this pass, but a
    // successful recovery must not inherit an error from an earlier attachment.
    const DWORD previousWindowError=windowError;windowError=0;const Config cfg=getConfig();release();
    const bool show=cfg.masterEnabled && cfg.enabled;
    POINT cursor{};GetCursorPos(&cursor);MONITORINFO mi{sizeof(mi)};
    const HWND parent=owner.load(std::memory_order_acquire);
    if(!GetMonitorInfoW(parent?MonitorFromWindow(parent,MONITOR_DEFAULTTOPRIMARY):
        MonitorFromPoint(cursor,MONITOR_DEFAULTTOPRIMARY),&mi)){windowError=GetLastError();if(!windowError)windowError=previousWindowError?previousWindowError:ERROR_NOT_READY;return;}
    // One monitor/work-area snapshot feeds every input and visual window.
    if(!buttonsRefresh(cfg,mi.rcMonitor,mi.rcWork,forceVisuals))return;
    auto rects=panelRects(mi.rcMonitor,cfg.scale,&mi.rcWork);
    if(cfg.layout==Layout::Single) {rects[0]=singlePanelRect(mi.rcMonitor,cfg.scale,&mi.rcWork);rects[1]=rects[0];}
    {std::lock_guard<std::mutex> l(mutex);for(int i=0;i<2;i++)sizes[i]={rects[i].right-rects[i].left,rects[i].bottom-rects[i].top};}
    const int panelW=rects[0].right-rects[0].left,panelH=rects[0].bottom-rects[0].top;
    for(int i=0;i<2;i++) {
        const Mode mode=surfaceMode(cfg,i);
        if(cfg.layout==Layout::Single && i==1) {
            nativeInputClose(i);
            if(outlines[i]){DestroyWindow(outlines[i]);outlines[i]=nullptr;outlineUploads[i]={};}outlineBitmaps[i].reset();for(auto& frame:outlineFrames[i])frame.reset();
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
                L"YeManScreenTouchpadOutline",L"YeManCC Touchpad Outline",WS_POPUP|WS_DISABLED,0,0,panelW,panelH,windows[i],nullptr,instance,nullptr);
            if(!outlines[i]) windowError=GetLastError();
        }
        if(windows[i]) {
            {std::lock_guard<std::mutex> l(mutex);sizes[i]={panelW,panelH};}
            SetWindowPos(windows[i],HWND_TOPMOST,rects[i].left,rects[i].top,panelW,panelH,SWP_NOACTIVATE);
            const bool inputReady=nativeInputSync(i,show&&permitted(mode),forceVisuals);
            // Upload the clear background, border and cross before either window is
            // shown, including the first display. No colour-key/black-fill phase.
            const bool visualReady=outlines[i] && ymcc::screeninput::ensureDisplay(outlines[i]) && renderOutline(i,cfg.transparency,forceVisuals);
            if(show && permitted(mode) && (!inputReady || !visualReady) && !windowError)windowError=ERROR_NOT_READY;
            ShowWindow(windows[i],show && permitted(mode) && inputReady?SW_SHOWNOACTIVATE:SW_HIDE);
            if(outlines[i]) ShowWindow(outlines[i],show && permitted(mode) && inputReady && visualReady && !autoHide.hidden?SW_SHOWNOACTIVATE:SW_HIDE);
            InvalidateRect(windows[i],nullptr,FALSE);
            if(show && permitted(mode)) UpdateWindow(windows[i]);
        }
    }
    autoHideSync();syncContactVisuals(-1,forceVisuals);if(visibilityRefreshObserver)visibilityRefreshObserver();
}
inline bool configure(const Config& cfg,int profile=-1,bool revealFromSettings=false) {
    if(inputTeardown)return false; // A retiring owner cannot accept queued configuration.
    const int configuredProfile=profile<0?activeProfile.load():profile;
    // Geometry changes release contacts before resizing, preventing stale coordinates/held keys.
    // Transparency-only drags still preserve active fingers.
    Config old=getConfig();const bool reset=configuredProfile!=appliedProfile.load() || old.masterEnabled!=cfg.masterEnabled || old.enabled!=cfg.enabled || old.left!=cfg.left || old.right!=cfg.right || old.scale!=cfg.scale || old.layout!=cfg.layout || old.single!=cfg.single || old.summonPosition!=cfg.summonPosition || old.specialMask!=cfg.specialMask || old.rearMask!=cfg.rearMask || old.touchFeedback!=cfg.touchFeedback || old.standaloneSpecialMode!=cfg.standaloneSpecialMode || old.summonEnabled!=cfg.summonEnabled || old.specialEnabled!=cfg.specialEnabled || old.rearEnabled!=cfg.rearEnabled;
    if(reset) release();
    {std::lock_guard<std::mutex> l(mutex);core.config=cfg;}
    appliedProfile.store(configuredProfile);
    nativeMode.store(cfg.masterEnabled && cfg.enabled && (cfg.layout==Layout::Single?nativePad(cfg.single):nativePad(cfg.left) || nativePad(cfg.right)),std::memory_order_release);
    enabled.store(cfg.masterEnabled && cfg.enabled && (cfg.layout==Layout::Single || cfg.left!=Mode::Off || cfg.right!=Mode::Off),std::memory_order_release);
    // Re-selecting an unchanged setting must also repair a missing live button.
    const bool refreshed=reset || (revealFromSettings && buttonsNeedRefresh(cfg));
    if(refreshed) refresh();else if(old.transparency!=cfg.transparency) {
        for(int i=0;i<2;i++) if(outlines[i] && !renderOutline(i,cfg.transparency)) ShowWindow(outlines[i],SW_HIDE);
        buttonsRepaint(cfg.transparency);syncContactVisuals();
    }
    // An explicit selection also previews an unchanged, already-hidden choice.
    // Owner refresh/rebind calls keep the default and preserve idle hiding.
    autoHideSync(revealFromSettings || reset || old.autoHideSeconds!=cfg.autoHideSeconds || old.transparency!=cfg.transparency);
    if(quickAccessContextObserver)quickAccessContextObserver();
    return refreshed; // Owner can avoid running a second identical refresh.
}
inline void initialize(HINSTANCE hi,HWND parent,const Config& cfg,int profile=-1) {
    inputTeardown=false;exitInputWindows={};exitInputWindowCount=0;exitInputChecks={};exitInputCheckCount=0;instance=hi;owner=parent;initialized=true;WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=hi;
    wc.lpszClassName=L"YeManScreenTouchpad";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) windowError=GetLastError();
    wc.lpfnWndProc=outlineProc;wc.lpszClassName=L"YeManScreenTouchpadOutline";
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) windowError=GetLastError();
    buttonsInitialize();
    // shutdown preserves the selected config. A same-config reinitialize still
    // has to recreate its HWNDs; configure() refreshes only when it resets.
    if(!configure(cfg,profile))refresh();
}
inline void quiesce() {
    if(inputTeardown)return;
    // Close admission before the first synchronous window notification.
    inputTeardown=true;initialized=false;
    enabled.store(false);nativeMode.store(false);available.store(false);psAvailable.store(false);
    controlTargetEnabled.store(false);controlEdgeEnabled.store(false);screenButtonMask.store(0,std::memory_order_release);
    autoHideStop();
    for(HWND h:windows)rememberExitInputWindow(h);for(HWND h:outlines)rememberExitInputWindow(h);
    for(const auto& visual:contactVisuals)rememberExitInputWindow(visual.window);rememberExitInputWindow(cursorWindow);
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    for(auto& surface:nativeInputSurfaces){rememberExitInputWindow(surface.child());if(!surface.quiesce()&&!windowError)windowError=ERROR_ACCESS_DENIED;}
#endif
    // Every pad and dedicated key is hidden/disabled before any cancel sink,
    // controller Close or other overlay cleanup can wait or reenter the owner.
    for(HWND h:windows)if(h&&IsWindow(h)){ShowWindow(h,SW_HIDE);EnableWindow(h,FALSE);}
    buttonsQuiesce();
    for(HWND h:outlines)if(h)ShowWindow(h,SW_HIDE);
    for(auto& visual:contactVisuals)hideContactVisual(visual);
    cursorStop();
}
inline ExitInputCheck checkExitInputs() {
    ExitInputCheck check;
    check.keys=sentKeys;check.mouse=sentClick;check.buttons=screenButtonMask.load(std::memory_order_acquire);
    {std::lock_guard<std::mutex> lock(mutex);check.contacts=core.pads[0].down||core.pads[1].down||core.clicks[0].active||core.clicks[1].active||feedbackOnlyContacts.mask()!=0;}
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    for(const auto& surface:nativeInputSurfaces)if(!surface.terminalReleased())++check.openSources;
#endif
    check.openSources+=buttonsOpenInputSources();
    for(size_t i=0;i<exitInputWindowCount;++i)if(IsWindow(exitInputWindows[i]))++check.liveWindows;
    check.error=inputError?inputError:windowError;
    check.ok=inputTeardown&&!check.keys&&!check.mouse&&!check.buttons&&!check.contacts&&!check.openSources&&!check.liveWindows;
    return check;
}
inline void confirmExitInputs() {
    if(!inputTeardown)return; // This audit cannot release an active session.
    // Each round observes sender-owned signal state and original HWNDs afresh.
    // Retry only our outstanding releases/destruction, never a down or MOVE.
    unsigned consecutive=0;
    for(unsigned round=0;round<exitInputChecks.size();++round){
        if(sentKeys||sentClick)output(Action{});
        for(size_t i=0;i<exitInputWindowCount;++i){
            const HWND h=exitInputWindows[i];if(!IsWindow(h))continue;
            DWORD pid=0;if(GetWindowThreadProcessId(h,&pid)==GetCurrentThreadId()&&pid==GetCurrentProcessId()){
                if(!DestroyWindow(h)&&!windowError)windowError=GetLastError();
            }
        }
        exitInputChecks[round]=checkExitInputs();exitInputCheckCount=round+1;
        consecutive=exitInputChecks[round].ok?consecutive+1:0;
        if(consecutive>=kExitInputRequiredCleanRounds)break;
    }
}
inline Json exitInputReleaseState(){
    Json checks=Json::array();unsigned consecutive=0;
    for(unsigned i=0;i<exitInputCheckCount;++i){const auto& c=exitInputChecks[i];consecutive=c.ok?consecutive+1:0;
        checks.push_back({{"round",i+1},{"ok",c.ok},{"heldKeys",c.keys},{"heldMouseButton",c.mouse},{"screenButtons",c.buttons},
            {"contactsPending",c.contacts},{"openSources",c.openSources},{"liveOwnedWindows",c.liveWindows},{"error",c.error}});}
    return {{"version",1},{"terminalAdmissionClosed",inputTeardown},{"rounds",exitInputCheckCount},{"requiredConsecutiveCleanRounds",kExitInputRequiredCleanRounds},{"verified",consecutive>=kExitInputRequiredCleanRounds},
        {"checks",std::move(checks)},{"scope","Touchpad sender-owned signals, controlled sources and original window handles only"},
        {"driverReceiptProven",false},{"downstreamConsumptionProven",false}};
}
inline void shutdown() {
    quiesce();autoHideStop();initialized=false;
    cursorShutdown();buttonsShutdown();controlTargetEnabled.store(false);controlEdgeEnabled.store(false);
    release();shutdownContactVisuals();enabled.store(false);nativeMode.store(false);available.store(false);psAvailable.store(false);
    for(HWND& h:outlines){if(h)DestroyWindow(h);h=nullptr;}outlineUploads={};
    for(auto& image:outlineBitmaps)image.reset();for(auto& pair:outlineFrames)for(auto& frame:pair)frame.reset();
    for(int side=0;side<2;++side){nativeInputClose(side);if(windows[side])DestroyWindow(windows[side]);windows[side]=nullptr;}
    confirmExitInputs();owner=nullptr;
}
inline Json state() {
    const Config cfg=getConfig();Json j=configJson(cfg);j["ok"]=!windowError && !inputError;j["available"]=true;
    j["activePersona"]=profileName(activeProfile.load());j["appliedPersona"]=profileName(appliedProfile.load());
    j["controlTargetEnabled"]=controlTargetEnabled.load();j["buttonVisibility"]=buttonVisibilityState(cfg);
    j["persona"]=profileName(appliedProfile.load());j["steamDeckAvailable"]=available.load();j["ps5Available"]=psAvailable.load();
    j["visible"]=(windows[0] && IsWindowVisible(windows[0])) || (windows[1] && IsWindowVisible(windows[1])) || buttonsVisible();
    j["autoHidden"]=autoHide.hidden;j["autoHideTimerArmed"]=autoHide.timer!=0;
    j["autoHideTransitions"]=autoHide.hides;j["autoHideWakeups"]=autoHide.wakes;
    j["inputTeardown"]=inputTeardown;j["exitInputWithdrawalVersion"]=1;j["exitInputRelease"]=exitInputReleaseState();j["nativeMousePassRequested"]=nativeInputCandidateRequested;j["nativeMousePassActive"]=false;j["nativeMousePassCompiled"]=false;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    j["nativeMousePassCompiled"]=true;bool any=false,ready=true;
    for(int side=0;side<2;++side)if(windows[side]&&IsWindowVisible(windows[side])){any=true;ready=ready&&nativeInputSurfaces[side].active();}
    j["nativeMousePassActive"]=nativeInputCandidateRequested&&(any||buttonsVisible())&&ready&&buttonsNativeInputReady();j["nativeMousePassRuntimeError"]=(long)nativeInputRuntime.error;
#endif
    j["events"]=eventCount;j["injectedInputs"]=injectedCount;j["repaints"]=repaintCount;j["outlineBitmapBuilds"]=outlineBitmapBuilds;j["pressFrameBuilds"]=pressFrameBuilds;j["outlineUploads"]=outlineUploadCount;j["contactVisualUploads"]=contactVisualUploads;j["contactVisualMoves"]=contactVisualMoves;
    {std::lock_guard<std::mutex> l(mutex);
        j["nativeTransportObserved"]=nativeTransport.observed;j["nativeTransportReady"]=nativeTransport.last.ready;
        j["nativeTransportEpoch"]=nativeTransport.last.epoch;j["nativeTransportChanges"]=nativeTransport.changes;
        j["nativeDiscardedGestures"]=nativeTransport.discardedGestures;}
    j["contactGeometryCacheVersion"]=1;j["contactGeometryReads"]=contactGeometryReads;j["contactGeometryHits"]=contactGeometryHits;j["contactGeometryInvalidations"]=contactGeometryInvalidations;
    j["cursorPolicyVersion"]=17;j["cursorDeckForceVisibleVersion"]=1;j["cursorDeckForcedHiddenSamples"]=cursorDeckForcedHiddenSamples;j["cursorDeckForcedShows"]=cursorDeckForcedShows;j["cursorDeckContactShapeBridgeVersion"]=1;j["cursorDeckContactShapeBridgeShows"]=cursorDeckContactShapeBridgeShows;j["cursorDeckMoveSubmitOrderVersion"]=1;j["cursorDeckMotionGateVersion"]=1;j["cursorHiddenQueryBudgetVersion"]=1;j["cursorSubmitOrderVersion"]=1;j["cursorHandoffGraceMs"]=kCursorHandoffMs;j["cursorRetainedPollMs"]=kCursorRetainedPollMs;j["cursorRetainedAfterUp"]=cursorRetained.load();j["cursorFallbackShows"]=cursorShows;j["cursorFallbackHides"]=cursorHides;j["cursorFallbackHandoffs"]=cursorHandoffs;
    j["cursorFallbackVisible"]=cursorVisible;j["cursorFallbackSyncs"]=cursorSyncs;j["cursorFallbackQueries"]=cursorQueries;j["cursorActiveFallbackMs"]=kCursorActiveFallbackMs;j["cursorFallbackUploads"]=cursorUploads;j["cursorFallbackError"]=cursorError;
    j["cursorDeckOnlyTracking"]=cursorDeckOnlyTracking.load(std::memory_order_acquire);j["cursorDeckSystemHandoffs"]=cursorDeckSystemHandoffs;
    j["cursorDeckReportPulseTick"]=cursorDeckReportPulseTick.load(std::memory_order_acquire);j["cursorDeckMoveFallbackMs"]=kCursorDeckMoveFallbackMs;
    j["cursorDeckMovePostReportDeferrals"]=cursorDeckMovePostReportDeferrals;j["cursorDeckMoveFallbackRequests"]=cursorDeckMoveFallbackRequests;
    j["cursorDeckHandoffGraceMs"]=0;
    j["cursorVisibilityObserver"]=cursorvisibility::state();j["cursorTransitionTrace"]=cursorTransitionState();j["cursorVisibilityContext"]=cursorcontext::state();
    j["cursorImageUploaded"]=cursorImageUploaded;j["cursorDeckPositionOnlyEnabled"]=cursorDeckPositionOnlyEnabled;
    j["cursorLayeredResetPending"]=cursorLayeredResetPending;j["cursorLayeredResets"]=cursorLayeredResets;j["cursorLayeredResetFailures"]=cursorLayeredResetFailures;
    j["cursorDeckPositionOnlyMoves"]=cursorDeckPositionOnlyMoves;j["cursorDeckPositionFallbacks"]=cursorDeckPositionFallbacks;
    j["cursorDeckPositionLastError"]=cursorDeckPositionLastError;
    j["cursorDeckStationaryDeferrals"]=cursorDeckStationaryDeferrals;j["cursorDeckMotionStarts"]=cursorDeckMotionStarts;j["cursorDeckPresentRechecks"]=cursorDeckPresentRechecks;j["cursorDeckPresentDeferrals"]=cursorDeckPresentDeferrals;
    j["cursorDeckDeferredMoves"]=cursorDeckDeferredMoves;j["cursorFlagsObserved"]=cursorFlagsObserved;
    j["cursorLastFlags"]=cursorLastFlags;j["cursorFlagTransitions"]=cursorFlagTransitions;
    j["cursorQueryFailures"]=cursorQueryFailures;j["cursorHandoffGapResets"]=cursorHandoffGapResets;
    j["cursorMaxHandoffSampleGapMs"]=kCursorMaxHandoffSampleGapMs;
    const auto specialStatus=standaloneStatus.load(std::memory_order_acquire);
    j["standaloneSpecialStatus"]=standaloneStatusName(specialStatus);
    j["standaloneSpecialRequests"]=standaloneRequests.load();j["standaloneSpecialCompleted"]=standaloneCompleted.load();
    j["error"]=inputError?inputError:windowError;j["reason"]="";return j;
}
inline Json profileState(const Config& cfg,int profile) {
    Json j=state();const bool current=profile==activeProfile.load() && profile==appliedProfile.load();
    j.update(configJson(cfg));j["persona"]=profileName(profile);
    j["steamDeckAvailable"]=profile==1 && available.load();j["ps5Available"]=profile==2 && psAvailable.load();
    if(!current) {j["buttonVisibility"]=nullptr;j["autoHidden"]=false;j["autoHideTimerArmed"]=false;j["visible"]=false;j["error"]=0;j["ok"]=true;j["reason"]="";j["standaloneSpecialStatus"]="idle";}return j;
}
#include "screen_button_overlay.h"
#include "screen_touchpad_auto_hide.h"

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
    Core h;h.config=c.config;h.config.touchFeedback=TouchFeedbackMode::Minimal;
    check("minimal-haptic-on-touch",h.event(0,1,0,100,100,200,200,0).hapticPulses==1);
    check("minimal-haptic-after-distance",h.event(0,1,1,115,100,200,200,100).hapticPulses==1);
    check("minimal-haptic-throttles-short-interval",h.event(0,1,1,130,100,200,200,120).hapticPulses==0);
    check("minimal-haptic-resumes-after-interval",h.event(0,1,1,170,100,200,200,170).hapticPulses==1);
    check("stationary-updates-have-no-haptic-backlog",h.event(0,1,1,170,100,200,200,250).hapticPulses==0);
    h.cancel();check("minimal-haptic-cancel-clears-distance",!h.current().hapticContacts && h.pads[0].haptic.distance==0);
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
    Config haptic=cfg;
    check("touch-feedback-medium-by-default",haptic.touchFeedback==TouchFeedbackMode::Medium && configJson(haptic).value("touchFeedback","")=="medium");
    check("accept-minimal-touch-feedback",parseConfig({{"touchFeedback","minimal"}},haptic,haptic) && haptic.touchFeedback==TouchFeedbackMode::Minimal);
    check("accept-medium-touch-feedback",parseConfig({{"touchFeedback","medium"}},haptic,haptic) && haptic.touchFeedback==TouchFeedbackMode::Medium);
    check("accept-strong-touch-feedback",parseConfig({{"touchFeedback","strong"}},haptic,haptic) && haptic.touchFeedback==TouchFeedbackMode::Strong);
    check("reject-unknown-touch-feedback",!parseConfig({{"touchFeedback","maximal"}},haptic,haptic));
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
        if(x>=12 && x<188 && y>=12 && y<188 && x!=100 && y!=100 && p!=0) pixelsValid=false;
        if(t==100 && p!=0) pixelsValid=false;
    }
    check("per-pixel-outline-cross-premultiplied-alpha-and-clear-background",pixelsValid);
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
    // Self-test ONLY: observe foreground events scoped to this helper PID.
    // External apps may legitimately change focus during a 12-second run;
    // comparing arbitrary before/after HWNDs did not test overlay activation.
    // Count every own-PID event so even a transient steal followed by return
    // fails. No input hook or foreground manipulation enters production.
    static unsigned ownForegroundEvents=0;ownForegroundEvents=0;
    struct FocusObserver {HWINEVENTHOOK hook=nullptr;~FocusObserver(){if(hook)UnhookWinEvent(hook);}} focusObserver;
    focusObserver.hook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,
        [](HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD){++ownForegroundEvents;},GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);
    check("own-process-foreground-observer-registered",focusObserver.hook!=nullptr);
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
        check(side==0?"left-uploaded-thin-cross":"right-uploaded-thin-cross",!upload.interiorClear && (upload.centre>>24)==25);
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
                outlineUploads[0].ready && outlineUploads[1].ready && !outlineUploads[0].interiorClear && !outlineUploads[1].interiorClear &&
                (outlineUploads[0].centre>>24)==25 && (outlineUploads[1].centre>>24)==25 && (outlineUploads[0].edge>>24)==51 && (outlineUploads[1].edge>>24)==51);
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
            check(("PS5-single-scale-"+std::to_string(percent)+"-hollow").c_str(),outlineUploads[0].ready && !outlineUploads[0].interiorClear && (outlineUploads[0].centre>>24)==25);
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
        // same PS5 surface, with independent IDs and target gating.
        for(const char* persona:{"dualsense-edge"}) {
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
                RECT expectedVisual=input;const int padding=pressGlowPadding(cfg.scale);InflateRect(&expectedVisual,padding,padding);
                visualAligned=haveVisual && IsWindowVisible(buttonOutlines[n]) && EqualRect(&expectedVisual,&visual) && visualAligned;
                visualArt.push_back({{"index",n},{"rect",{visual.left,visual.top,visual.right,visual.bottom}}});
            }
            result["buttonVisualArt"][persona]=visualArt;
            check((std::string(persona)+"-visible-outline-wraps-actual-touch-window-with-glow-margin").c_str(),visualAligned);
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
                    RECT expectedVisual=expected;const int padding=pressGlowPadding(cfg.scale);InflateRect(&expectedVisual,padding,padding);
                    positioned=positioned && EqualRect(&input,&expected) && EqualRect(&visual,&expectedVisual);
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
                    RECT expectedVisual=input;const int padding=pressGlowPadding(cfg.scale);InflateRect(&expectedVisual,padding,padding);
                    aligned=aligned && buttonOutlines[n] && GetWindowRect(buttonOutlines[n],&visual) &&
                        IsWindowVisible(buttonOutlines[n]) && EqualRect(&expectedVisual,&visual) &&
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
                const LONG gap=MulDiv(40,percent,100);
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
        result["externalForegroundChangedDuringTest"]=GetForegroundWindow()!=foreground;
        // The ownership check is made after the final pump/shutdown below.

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
    pump(30);DWORD foregroundPid=0;
    if(HWND finalForeground=GetForegroundWindow())GetWindowThreadProcessId(finalForeground,&foregroundPid);
    result["ownForegroundEvents"]=ownForegroundEvents;result["focusObserverProcessId"]=GetCurrentProcessId();
    result["focusObserverScope"]="selftest-process-only";
    check("overlay-does-not-steal-focus",focusObserver.hook && ownForegroundEvents==0 && foregroundPid!=GetCurrentProcessId());
    testTrace=nullptr;result["trace"]=trace;
    result["injectionErrors"]=injectionErrors;result["pointerEvents"]=events;result["cases"]=cases;bool ok=true;
    for(const auto& c:cases)ok=ok&&c["ok"].get<bool>();result["ok"]=ok;
    result["systemInputInjected"]=false;result["windowsTouchInjected"]=true;
    return result;
}

} // namespace ymcc::screenpads
