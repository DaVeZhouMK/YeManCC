#pragma once
// Export metadata only. Raw periodic snapshots live exclusively in the detailed log.
#include "../deps/json/json.hpp"
#include <cmath>
#include <initializer_list>
#include <string>
namespace ymcc::monitorexport {
using json=nlohmann::json;
inline constexpr std::size_t maxSummaryBytes=32*1024;
inline json selectedScalars(const json& raw,std::initializer_list<const char*> keys,std::size_t maxString=128) {
    json out=json::object();if(!raw.is_object())return out;
    for(const auto* key:keys){const auto it=raw.find(key);if(it==raw.end())continue;
        if(it->is_boolean()||it->is_number_integer()||it->is_number_unsigned())out[key]=*it;
        else if(it->is_number_float()&&std::isfinite(it->get<double>()))out[key]=*it;
        else if(it->is_string()){const auto& text=it->get_ref<const std::string&>();out[key]=text.substr(0,maxString);}}
    return out;
}
inline json snapshot(const json& cache,const json& machine,const json& settings,long long nowMs,const char* settingsStatus) {
    const bool hasRtss=settings.is_object()&&settings.contains("rtss")&&settings["rtss"].is_object();
    const bool hasPrefs=hasRtss&&settings["rtss"].contains("monitorOutput")&&settings["rtss"]["monitorOutput"].is_object();
    json prefs=nullptr;
    if(hasPrefs){const auto& raw=settings["rtss"]["monitorOutput"];
        prefs=selectedScalars(raw,{"version","enabled","output","ymccCarrier","rtssScale","rtssTemplate","steamPosition"});
        if(raw.contains("ymcc")&&raw["ymcc"].is_object()){const auto& y=raw["ymcc"];
            auto style=selectedScalars(y,{"position","scale","saturation","opacity","layout","radius","gap","hideUnavailable","fpsEmphasis","crosshair","dataPreset","fieldGroup","underlay","underlayPreset","underlayOpacity"});
            if(y.contains("fields")&&y["fields"].is_object()){json fields=json::object();std::size_t count=0,scanned=0;
                for(auto it=y["fields"].begin();it!=y["fields"].end()&&count<64&&scanned<128;++it,++scanned){const auto& key=it.key();if(key.empty()||key.size()>48)continue;bool ascii=true;
                    for(unsigned char ch:key)if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'))ascii=false;
                    if(ascii&&it.value().is_boolean()){fields[key]=it.value();++count;}}
                style["fields"]=std::move(fields);}
            const auto image=y.find("underlayImage");const auto imageBytes=image!=y.end()&&image->is_string()?image->get_ref<const std::string&>().size():0;
            style["underlayImageBytes"]=imageBytes;style["underlayImageOmitted"]=imageBytes>0;
            style["savedPresetDefinitionsIncluded"]=false;prefs["ymcc"]=std::move(style);}}
    auto meta=selectedScalars(cache,{"schema","available","session","generation","relayEpoch","seq","collectedAt","heartbeatAt","hwinfoOkAt","topActive","fpsActive","hwDown"});
    // Native scalar keys above are numeric/bool. Never accept a caller string as
    // a way of smuggling a raw snapshot into the small export summary.
    for(auto it=meta.begin();it!=meta.end();)if(it.value().is_string())it=meta.erase(it);else ++it;
    for(const auto* key:{"top","fps"}){const auto it=cache.find(key);const bool present=it!=cache.end()&&it->is_string()&&!it->get_ref<const std::string&>().empty();meta[std::string(key)+"Present"]=present;meta[std::string(key)+"Bytes"]=present?it->get_ref<const std::string&>().size():0;}
    json age=nullptr;
    if(cache.is_object()&&cache.contains("collectedAt")&&cache["collectedAt"].is_number_integer()){const auto at=cache["collectedAt"].get<long long>();if(at>0&&at<=nowMs)age=nowMs-at;}
    return {{"schemaVersion",2},{"observedAtMs",nowMs},{"evidenceScope","bounded-export-metadata-only-raw-snapshots-in-detailed-log"},
        {"hardwareProbes",false},{"newCollectionDemand",false},{"historicalSamplesIncluded",false},{"cacheAndSettingsAtomic",false},
        {"cacheAgeMs",age},{"machineIdentitySource","existing-native-startup-cache"},
        {"machine",selectedScalars(machine,{"manufacturer","board","systemProduct","processor","deviceClass","resolved"},256)},
        {"snapshot",std::move(meta)},{"settingsStatus",std::string(settingsStatus?settingsStatus:"unknown").substr(0,64)},
        {"savedMonitorPreferencesPresent",hasPrefs},{"savedMonitorPreferences",std::move(prefs)},
        {"detailedSnapshotFile","monitor-detail.log"},{"rawSnapshotIncluded",false},{"summaryMaxBytes",maxSummaryBytes},
        {"tdpProofScope","backend-acknowledgement-not-continuous-register-read"}};
}
inline std::string encode(json state){
    auto text=state.dump(2,' ',false,json::error_handler_t::replace);
    if(text.size()+1<=maxSummaryBytes)return text;
    // Complete JSON fallback, never cut the serialized byte stream mid-record.
    return json({{"schemaVersion",2},{"summaryOmitted",true},{"reason","summary-byte-limit"},
        {"detailedSnapshotFile","monitor-detail.log"},{"summaryMaxBytes",maxSummaryBytes}}).dump();
}
}
