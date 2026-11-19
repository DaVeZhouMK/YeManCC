#pragma once
// One global document. Artwork never selects an input source or output persona.
#include <windows.h>
#include <array>
#include <atomic>
#include <vector>
#include <string>
#include <mutex>
#include <algorithm>
#include <cmath>
#include "sleep_power_state.h"
namespace ymcc::customtouch {
using Json=nlohmann::json;
inline std::atomic<bool> editing{false},live{false},gamepadEnabled{false};
inline std::atomic<bool> inheritedMasterEnabled{true},feedbackContact{false};
inline bool (*resolveKey)(const std::string&,const std::string&,WORD&,bool&)=nullptr;
inline DWORD (*keyFlags)(WORD)=nullptr;
inline Json defaults(){return {{"version",1},{"enabled",true},{"gamepad",false},{"template","steamdeck"},
    {"buttonScale",100},{"stickScale",100},{"keys",Json::array({{{"id","key-1"},{"label","Ins"},
    {"keys",Json::array({{{"actionId","keyboard.rightFunction"},{"code","Insert"}}})}}})},{"positions",Json::object()}};}
inline Json config=defaults(); // HWND owner thread; the BUS only reads the POD below.
inline bool parse(const Json& patch,const Json& base,Json& result){
    if(!patch.is_object()||!base.is_object())return false;
    Json next=base;
    for(auto it=patch.begin();it!=patch.end();++it){const auto& k=it.key();const auto& v=it.value();
        if(k=="version"){if(!v.is_number_integer()||v!=1)return false;}
        else if(k=="enabled"||k=="gamepad"){if(!v.is_boolean())return false;}
        else if(k=="template"){if(v!="steamdeck"&&v!="ps5"&&v!="xbox")return false;}
        else if(k=="buttonScale"||k=="stickScale"){if(!v.is_number_integer()||v<50||v>150||v.get<int>()%5)return false;}
        else if(k=="keys"){
            if(!v.is_array())return false;std::vector<std::string> ids;
            for(const auto& row:v){
                if(!row.is_object()||row.size()!=3||!row.contains("id")||!row["id"].is_string()||!row.contains("label")||!row["label"].is_string()||!row.contains("keys")||!row["keys"].is_array()||row["keys"].empty()||row["keys"].size()>4)return false;
                const auto id=row["id"].get<std::string>();
                if(id.rfind("key-",0)!=0||id.size()>100||row["label"].get_ref<const std::string&>().size()>256||std::find(ids.begin(),ids.end(),id)!=ids.end())return false;
                ids.push_back(id);std::vector<WORD> chord;
                for(const auto& key:row["keys"]){
                    if(!key.is_object()||key.size()!=2||!key.contains("actionId")||!key["actionId"].is_string()||!key.contains("code")||!key["code"].is_string()||!resolveKey)return false;
                    WORD vk=0;bool shift=false;if(!resolveKey(key["actionId"].get<std::string>(),key["code"].get<std::string>(),vk,shift)||!vk||vk>255)return false;
                    if(shift&&std::find(chord.begin(),chord.end(),VK_LSHIFT)==chord.end())chord.push_back(VK_LSHIFT);
                    if(std::find(chord.begin(),chord.end(),vk)==chord.end())chord.push_back(vk);
                }
                if(chord.size()>4)return false;
            }
        }else if(k=="positions"){
            if(!v.is_object())return false;
            for(auto mode=v.begin();mode!=v.end();++mode){
                if(mode.key().size()>40||!mode.value().is_object())return false;
                for(auto p=mode.value().begin();p!=mode.value().end();++p){const auto& point=p.value();
                    if(p.key().size()>100||!point.is_object()||point.size()!=2||!point.contains("x")||!point.contains("y")||!point["x"].is_number()||!point["y"].is_number())return false;
                    for(const auto* axis:{"x","y"}){const double n=point[axis].get<double>();if(!std::isfinite(n)||n<0||n>1)return false;}
                }
            }
        }else return false;
        next[k]=v;
    }
    result=std::move(next);return true;
}
inline std::string resolution(const RECT& r){return std::to_string(r.right-r.left)+"x"+std::to_string(r.bottom-r.top);}
inline RECT place(const char* id,const RECT& monitor,RECT r){
    if(!live.load(std::memory_order_relaxed))return r;
    const auto group=config["positions"].find(resolution(monitor));if(group==config["positions"].end())return r;
    const auto p=group->find(id);if(p==group->end())return r;
    const LONG width=r.right-r.left,height=r.bottom-r.top;
    const LONG x=std::clamp(monitor.left+(LONG)std::lround((*p)["x"].get<double>()*(monitor.right-monitor.left)),monitor.left,std::max(monitor.left,monitor.right-width));
    const LONG y=std::clamp(monitor.top+(LONG)std::lround((*p)["y"].get<double>()*(monitor.bottom-monitor.top)),monitor.top,std::max(monitor.top,monitor.bottom-height));
    return {x,y,x+width,y+height};
}
struct Pad {WORD buttons=0;short lx=0,ly=0,rx=0,ry=0;BYTE lt=0,rt=0;};
inline bool active(const Pad& p){return p.buttons||p.lx||p.ly||p.rx||p.ry||p.lt||p.rt;}
template<class Frame> inline void merge(Frame& frame,const Pad& p){
    const auto axis=[](short physical,short touch){return std::abs((int)touch)>std::abs((int)physical)?touch:physical;};
    frame.buttons|=p.buttons;frame.lxShort=axis(frame.lxShort,p.lx);frame.lyShort=axis(frame.lyShort,p.ly);frame.rxShort=axis(frame.rxShort,p.rx);frame.ryShort=axis(frame.ryShort,p.ry);
    frame.rawLtByte=std::max(frame.rawLtByte,static_cast<decltype(frame.rawLtByte)>(p.lt));frame.rawRtByte=std::max(frame.rawRtByte,static_cast<decltype(frame.rawRtByte)>(p.rt));
    frame.ltRaw=std::max(frame.ltRaw,p.lt/255.f);frame.rtRaw=std::max(frame.rtRaw,p.rt/255.f);frame.lt=std::max(frame.lt,p.lt/255.f);frame.rt=std::max(frame.rt,p.rt/255.f);
}
inline std::mutex padMutex;
inline Pad pad;
inline unsigned long long transportEpoch=0,inputGeneration=1;
inline bool transportReady=false;
inline ymcc::sleeppower::SharedPowerV1 touchPower;
inline bool powerBound=false;
inline bool (*capturePower)(ymcc::sleeppower::SharedPowerV1&)=nullptr;
inline bool (*powerCurrent)(const ymcc::sleeppower::SharedPowerV1&)=nullptr;
inline std::atomic<bool> hasPadInput{false};
inline void invalidate(){std::lock_guard<std::mutex> lock(padMutex);pad={};hasPadInput.store(false,std::memory_order_release);transportReady=false;powerBound=false;++inputGeneration;}
void release();void refresh();void quiesce();void shutdown();unsigned openSources();unsigned liveWindows();
void drain();
void releaseMapped();void cancelNativeInputs();void repaint();void syncVisibility(bool);
bool visible();bool contactHeld();bool inputReady();
inline unsigned feedbackMask() noexcept{return feedbackContact.load(std::memory_order_relaxed)?1u<<11:0u;}
bool needsRefresh();bool detachInputs();
inline bool wantsInput(){return live.load()&&(config["gamepad"].get<bool>()||(config["enabled"].get<bool>()&&!config["keys"].empty()));}
bool layoutPointer(HWND,const char*,UINT,UINT32,float,float,bool=false);
void editorAction(const char*);
void editorButtons(WORD,short,short,ULONGLONG);
unsigned heldKeys();
bool ownsKey(WORD) noexcept;
Pad sample(unsigned long long,bool);
}
