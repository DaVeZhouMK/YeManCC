#pragma once
#include "json.hpp"
#include "steamdeck_mouse_vdf.h"
#include <cmath>
#include <array>
// Evidence: Steam's own settings UI (chunk~2dcc5aaf7.js, Bs/bs, module 4563)
// and steamui.dll's InGameOverlayShowFPS* keys. No invented positions/ranges.
namespace ymcc::steamsettings {
using Json=nlohmann::json;
struct Field {const char* name;const char* client;const char* vdf;double min,max,fallback;bool integer;};
inline constexpr std::array<Field,5> fields{{
    {"position","overlay_fps_counter_corner","InGameOverlayShowFPSCorner",0,6,0,true},
    {"detail","overlay_fps_counter_detail_level","InGameOverlayShowFPSDetailLevel",1,4,1,true},
    {"scale","overlay_fps_counter_scale_factor","InGameOverlayShowFPSScaling",.2,1.4,1,false},
    {"saturation","overlay_fps_counter_saturation_factor","InGameOverlayShowFPSSaturation",0,1,1,false},
    {"opacity","overlay_fps_counter_bgopacity","InGameOverlayShowFPSBGOpacity",0,1,1,false}
}};
inline const Field* field(const std::string& name) {for(const auto& f:fields)if(name==f.name)return &f;return nullptr;}
inline bool valid(const Json& patch) {
    if(!patch.is_object() || patch.empty())return false;
    for(auto it=patch.begin();it!=patch.end();++it){
        const auto* f=field(it.key());if(!f || !it->is_number())return false;
        const double value=it->get<double>();
        if(!std::isfinite(value) || value<f->min-1e-6 || value>f->max+1e-6)return false;
        if(f->integer && value!=std::round(value))return false;
        if(!f->integer && std::abs(value*10-std::round(value*10))>1e-5)return false;
    }return true;
}
inline const steamdeck::Node& system(const std::vector<steamdeck::Node>& roots) {
    const auto* root=steamdeck::unique(roots,"UserLocalConfigStore");
    if(!root)root=steamdeck::unique(roots,"userlocalconfigstore");
    if(!root || !root->object)throw std::runtime_error("localconfig-root-not-found");
    const auto* node=steamdeck::unique(root->children,"system");
    if(!node || !node->object)throw std::runtime_error("localconfig-system-not-found");
    return *node;
}
inline Json read(const std::string& text) {
    const auto roots=steamdeck::Vdf(text).parse();const auto& node=system(roots);Json result=Json::object();
    for(const auto& f:fields){
        const auto* scalar=steamdeck::unique(node.children,f.vdf);
        double value=f.fallback;
        if(scalar){if(scalar->object)throw std::runtime_error("invalid-monitor-value");size_t used=0;value=std::stod(scalar->value,&used);if(used!=scalar->value.size())throw std::runtime_error("invalid-monitor-value");}
        if(!std::isfinite(value) || value<f.min-1e-6 || value>f.max+1e-6 || (f.integer&&value!=std::round(value)))throw std::runtime_error("invalid-monitor-value");
        result[f.name]=f.integer?Json(static_cast<int>(value)):Json(value);
    }return result;
}
inline int overlayValue(const std::string& text){
    const auto roots=steamdeck::Vdf(text).parse();const auto& node=system(roots);
    const auto* value=steamdeck::unique(node.children,"EnableGameOverlay");
    if(!value)return 1;
    if(value->object || (value->value!="0" && value->value!="1"))throw std::runtime_error("invalid-overlay-value");
    return value->value=="1"?1:0;
}
inline std::string patch(const std::string& text,const Json& monitor,int overlay=-1) {
    if(!monitor.empty() && !valid(monitor))throw std::runtime_error("invalid-monitor-settings");
    if(overlay < -1 || overlay > 1)throw std::runtime_error("invalid-overlay-value");
    const auto roots=steamdeck::Vdf(text).parse();const auto& node=system(roots);
    std::vector<std::pair<size_t,std::pair<size_t,std::string>>> edits;
    std::string insert;
    const auto add=[&](const char* key,const std::string& value){
        const auto* n=steamdeck::unique(node.children,key);
        if(n){if(n->object)throw std::runtime_error("invalid-localconfig-setting");if(n->value!=value)edits.push_back({n->valueBegin,{n->valueEnd-n->valueBegin,"\""+value+"\""}});}
        else insert+="\n\t\t\""+std::string(key)+"\"\t\t\""+value+"\"";
    };
    for(const auto& f:fields)if(monitor.contains(f.name))add(f.vdf,monitor[f.name].dump());
    if(overlay>=0)add("EnableGameOverlay",std::to_string(overlay));
    if(!insert.empty())edits.push_back({node.close,{0,insert+"\n\t"}});
    std::sort(edits.begin(),edits.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    std::string result=text;for(const auto& e:edits)result.replace(e.first,e.second.first,e.second.second);
    (void)read(result);return result;
}
inline Json clientPatch(const Json& monitor,int overlay=-1){Json out=Json::object();for(const auto& f:fields)if(monitor.contains(f.name))out[f.client]=monitor[f.name];if(overlay>=0)out["enable_overlay"]=overlay==1;return out;}
inline bool matches(const Json& current,const Json& desired){for(auto it=desired.begin();it!=desired.end();++it)if(!current.contains(it.key()) || !current[it.key()].is_number() || std::abs(current[it.key()].get<double>()-it->get<double>())>1e-5)return false;return true;}
}
