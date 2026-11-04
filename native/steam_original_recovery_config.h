#pragma once
#include "json.hpp"
#include <cstdint>
#include <limits>
namespace ymcc::steamOriginal {
// A portable product follows the ordinary automatic-recovery preference. An
// explicit local OFF, unreadable override or malformed override stays OFF.
// Known v1 files predate targetMode; obsolete game/profile pins are metadata,
// not a reason to disable recovery or silently restrict the generic target.
inline nlohmann::json configuration(nlohmann::json config,bool exists,bool lookupError){
    if(!exists&&!lookupError)config={{"schema","ymcc.steam-original-recovery-enabled.v2"},
        {"enabled",true},{"targetMode","generic"},{"source","built-in-automatic-preference"}};
    const bool validEnabled=config.is_object()&&config.contains("enabled")&&config["enabled"].is_boolean();
    const bool validMode=config.is_object()&&(!config.contains("targetMode")||
        (config["targetMode"].is_string()&&(config["targetMode"]=="generic"||config["targetMode"]=="fixed")));
    const bool legacySchema=config.is_object()&&config.contains("schema")&&
        config["schema"].is_string()&&config["schema"]=="ymcc.steam-original-recovery-enabled.v1";
    const bool validSchema=config.is_object()&&(!config.contains("schema")||
        (config["schema"].is_string()&&(config["schema"]=="ymcc.steam-original-recovery-enabled.v2"||legacySchema)));
    bool validTarget=true;
    if(validMode&&config.value("targetMode",std::string("generic"))=="fixed"){
        validTarget=config.contains("targetGameId")&&config["targetGameId"].is_string();
        std::uint64_t id=0;
        if(validTarget){const auto text=config["targetGameId"].get<std::string>();
            validTarget=!text.empty()&&text.size()<=20&&text[0]!='0';
            for(const auto c:text){if(c<'0'||c>'9'||id>((std::numeric_limits<std::uint64_t>::max)()-static_cast<unsigned>(c-'0'))/10){validTarget=false;break;}id=id*10+static_cast<unsigned>(c-'0');}
            validTarget=validTarget&&(id&0xffffffffULL)==0x02000000ULL&&(id>>32)!=0;
        }
        if(validTarget&&config.contains("targetAppId")){
            const auto& app=config["targetAppId"];validTarget=app.is_number_integer()&&
                (app.is_number_unsigned()||app.get<std::int64_t>()>=0)&&app.get<std::uint64_t>()==(id>>32);
        }
        // The appid is encoded in the full shortcut GameID; this grants no instance identity.
        if(validTarget&&!config.contains("targetAppId"))config["targetAppId"]=static_cast<std::uint32_t>(id>>32);
    }
    if(lookupError||!validEnabled||!validMode||!validSchema||!validTarget)return {{"enabled",false},{"targetMode","generic"},{"source","invalid-local-override"}};
    if(!config.contains("targetMode"))config["targetMode"]="generic";
    if(legacySchema){config["schema"]="ymcc.steam-original-recovery-enabled.v2";config["source"]="legacy-v1-compatible";}
    if(!config.contains("source")||!config["source"].is_string())config["source"]="local-override";
    return config;
}
}
