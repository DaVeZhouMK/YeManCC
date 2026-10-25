#pragma once
#include "json.hpp"
namespace ymcc::steamOriginal {
// A portable product follows the ordinary automatic-recovery preference. An
// explicit local OFF, unreadable override or malformed override stays OFF.
inline nlohmann::json configuration(nlohmann::json config,bool exists,bool lookupError){
    if(!exists&&!lookupError)config={{"schema","ymcc.steam-original-recovery-enabled.v2"},
        {"enabled",true},{"targetMode","generic"},{"source","built-in-automatic-preference"}};
    const bool validEnabled=config.is_object()&&config.contains("enabled")&&config["enabled"].is_boolean();
    const bool validMode=config.is_object()&&(!config.contains("targetMode")||
        (config["targetMode"].is_string()&&(config["targetMode"]=="generic"||config["targetMode"]=="fixed")));
    if(lookupError||!validEnabled||!validMode)return {{"enabled",false},{"targetMode","generic"},{"source","invalid-local-override"}};
    if(!config.contains("targetMode"))config["targetMode"]="generic";
    if(!config.contains("source")||!config["source"].is_string())config["source"]="local-override";
    return config;
}
}
