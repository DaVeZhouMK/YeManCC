#pragma once
#include "json.hpp"

namespace ymcc::inputCaptureRecipeLog {
inline constexpr char marker[]="accelSharedRecipeSchema";
inline constexpr unsigned version=1;
inline constexpr const char* texts[]={"sourceSha256","sourceScope","provider","validationScope"};
inline constexpr const char* paths[]={"sourcePath","sourceBundle"};
inline constexpr char legacy[]="legacyFieldOverride";

inline void compact(nlohmann::json& receipt) {
    if(!receipt.is_object()||receipt.contains(marker))return;
    const auto gyro=receipt.find("gyro"),accel=receipt.find("accel");
    if(gyro==receipt.end()||accel==receipt.end()||!gyro->is_object()||!accel->is_object())return;
    const auto gyroId=gyro->find("id"),accelId=accel->find("id");
    if(gyroId==gyro->end()||accelId==accel->end()||!gyroId->is_string()||!accelId->is_string())return;
    const auto matches=[&](const char* key,auto type,bool optional=false) {
        const auto g=gyro->find(key),a=accel->find(key);
        if(g==gyro->end()||a==accel->end())return optional&&g==gyro->end()&&a==accel->end();
        return g->type()==type&&a->type()==type&&*g==*a;
    };
    using Type=nlohmann::json::value_t;
    for(const auto key:texts)if(!matches(key,Type::string))return;
    for(const auto key:paths)if(!matches(key,Type::string,true))return;
    if(!matches(legacy,Type::boolean))return;
    receipt[marker]=version;
    for(const auto key:texts)accel->erase(key);
    for(const auto key:paths)accel->erase(key);
    accel->erase(legacy);
}
}
