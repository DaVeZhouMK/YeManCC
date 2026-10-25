#pragma once
#include "../deps/json/json.hpp"
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
namespace ymcc::noticelog {
using json=nlohmann::json;
inline bool oneOf(const std::string& value,std::initializer_list<const char*> allowed){for(const auto* item:allowed)if(value==item)return true;return false;}
inline json project(const json& raw){
 if(!raw.is_object()||raw.size()>40||!raw.contains("stage")||!raw["stage"].is_string())return nullptr;
 const auto stage=raw["stage"].get<std::string>();
 if(!oneOf(stage,{"prepare-blocked","attempt-start","status-result","reserve-result","create-start","create-result","create-stale","attempt-error","ready-accepted","ready-rejected","owner-lost","closed-rejected","close-start","close-result","retire-result","retire-unconfirmed","retire-confirmed","retry-scheduled","retry-cancelled","retry-fired","retry-exhausted","ready-timeout","ready-wait-start","ready-wait-cancelled","healthy","power-held","power-rejected","power-committed","intent-on","intent-off","stop","feedback-rejected","frame-confirmed","native-reserved","native-created","native-create-error","native-retired","native-retire-error","native-close","native-ready","native-closed","native-unavailable"}))return nullptr;
 json out={{"stage",stage}};
 for(auto it=raw.begin();it!=raw.end();++it){const auto& key=it.key();const auto& v=it.value();if(key=="stage")continue;
  if(key=="reason"||key=="phase"){
   if(!v.is_string())return nullptr;const auto value=v.get<std::string>();
   if(key=="reason"?!oneOf(value,{"none","off","ended","power-held","ambiguous","carrier-blocked","retiring","budget","already-ready","timeout","remote-error","transport-error","stale-owner","stale-generation"}):!oneOf(value,{"status","close","reserve","create"}))return nullptr;
  }else if(oneOf(key,{"powerHeld","enabled","ended","ownerReady","ambiguousCreate","carrierBlocked","ok","fallback","statusReady","available","recoveryBlocked"})){
   if(!v.is_boolean())return nullptr;
  }else if(oneOf(key,{"leaseRetired","resourcesDestroyed","ownedOnly","obsolete"})){
   if(!v.is_null()&&!v.is_boolean())return nullptr;
  }else if(oneOf(key,{"hostId","epoch","ownerEpoch","leaseId","hudId","attempts","maxAttempts","readyTimeoutMs","powerGeneration","committedGeneration","retireAttempt","delayMs","elapsedMs","timeoutMs"})){
   if(v.is_null()){if(!oneOf(key,{"ownerEpoch","leaseId","hudId","powerGeneration","committedGeneration"}))return nullptr;}
   else {if(!v.is_number())return nullptr;const double n=v.get<double>();if(!std::isfinite(n)||n<0||n>9007199254740991.0||std::floor(n)!=n)return nullptr;
    if((key=="attempts"&&n>3)||(key=="maxAttempts"&&n!=3)||(key=="readyTimeoutMs"&&n!=10000)||(key=="retireAttempt"&&(n<1||n>2))||(key=="delayMs"&&(n!=250&&n!=1000)))return nullptr;}
  }else return nullptr; // No nonce, token, route, payload or free-form errors.
  out[key]=v;
 }
 return out;
}
// Diagnostic flood cap only. No timer or change to lifecycle retry policy.
struct Budget {std::uint64_t start=0;unsigned count=0;bool known=false;
 bool admit(std::uint64_t now){if(!known||now<start||now-start>=1000){known=true;start=now;count=0;}if(count>=256)return false;++count;return true;}
};
}
