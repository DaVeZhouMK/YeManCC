#pragma once
#include "json.hpp"
#include <algorithm>
#include <cstdint>
#include <string>

// GameSessions app-lifetime notifications are a Steam-side lifecycle witness.
// They carry AppID, Steam's nInstanceID and bRunning only: no OS PID, process
// birth, path, Windows session, full GameID, input proof or Continue ACK. Never
// use this result to fabricate Running or authorize a wake action.
namespace ymcc::steamlink {
using LifetimeJson = nlohmann::json;
inline bool lifetimeUInt(const LifetimeJson& row,const char* name,std::uint32_t& out) {
    if(!row.is_object()||!row.contains(name))return false;
    const auto& value=row[name];
    if(!value.is_number_integer()&&!value.is_number_unsigned())return false;
    if(value.is_number_integer()&&!value.is_number_unsigned()&&value.get<std::int64_t>()<0)return false;
    const auto n=value.get<std::uint64_t>();if(n>UINT32_MAX)return false;
    out=static_cast<std::uint32_t>(n);return true;
}
inline bool lifetimeInstanceId(const LifetimeJson& row,std::string& out) {
    if(!row.is_object()||!row.contains("nInstanceID"))return false;
    const auto& value=row["nInstanceID"];
    if(value.is_string()){
        const auto text=value.get<std::string>();
        if(text.empty()||text.size()>20||text.find_first_not_of("0123456789")!=std::string::npos)return false;
        out=text;return true;
    }
    if(value.is_number_unsigned()){out=std::to_string(value.get<std::uint64_t>());return true;}
    if(value.is_number_integer()&&value.get<std::int64_t>()>=0){out=std::to_string(value.get<std::uint64_t>());return true;}
    return false;
}
inline LifetimeJson compareNativeAppLifetime(const LifetimeJson& read,std::uint32_t appid) {
    LifetimeJson result={{"implementation","steam-app-lifetime-feedback-v1"},{"authority","diagnostic-only-no-admission"},
        {"available",false},{"eventRelation","unknown"},{"instanceRelation","unknown"},
        {"originalAppRunningKnown",false},{"originalAppRunning",false},{"latestEventSequence",0},
        {"originalEventCount",0},{"otherAppEventCount",0},{"schemaInvalidEvents",nullptr},
        {"observerInstanceId",nullptr},{"observerCreatedAtUnixMs",nullptr},{"diagnosticCountersAvailable",false},
        {"rawCallbacksEntered",nullptr},{"accountUnavailableCallbacks",nullptr},{"accountMismatchCallbacks",nullptr},
        {"nativeSubscriptionHealth","unknown"},
        {"fullGameIdCarried",false},{"processBirthCarried",false},{"gameInputVerified",false},{"continueVerified",false}};
    const auto yes=[](const LifetimeJson& x,const char* key){return x.is_object()&&x.contains(key)&&x[key].is_boolean()&&x[key].get<bool>();};
    if(!appid||!yes(read,"ok")||!read.contains("mutated")||!read["mutated"].is_boolean()||yes(read,"mutated")||
        !read.contains("nativeAppLifetimeFeedback")||!read["nativeAppLifetimeFeedback"].is_object())return result;
    const auto& f=read["nativeAppLifetimeFeedback"];
    const auto text=[](const LifetimeJson& x,const char* key,const char* expected){return x.contains(key)&&x[key].is_string()&&x[key].get<std::string>()==expected;};
    result["receipt"]=f;
    // These are local observer diagnostics only. They never create a lifecycle
    // event or establish native registration health, Running, input or Continue.
    const auto counter=[&](const char* key)->LifetimeJson {
        if(!f.contains(key))return nullptr;
        const auto& value=f[key];
        if(!value.is_number_integer()&&!value.is_number_unsigned())return nullptr;
        if(value.is_number_integer()&&!value.is_number_unsigned()&&value.get<std::int64_t>()<0)return nullptr;
        try {const auto n=value.get<std::uint64_t>();return n<=9007199254740991ULL?LifetimeJson(n):LifetimeJson(nullptr);}
        catch(...){return nullptr;}
    };
    result["schemaInvalidEvents"]=counter("schemaInvalidEvents");
    result["observerCreatedAtUnixMs"]=counter("observerCreatedAtUnixMs");
    if(f.contains("observerInstanceId")&&f["observerInstanceId"].is_string()&&f["observerInstanceId"].get_ref<const std::string&>().size()<=128)
        result["observerInstanceId"]=f["observerInstanceId"];
    const auto entered=counter("rawCallbacksEntered"),unavailable=counter("accountUnavailableCallbacks"),mismatch=counter("accountMismatchCallbacks");
    const bool haveCounters=yes(f,"diagnosticCountersAvailable")&&!entered.is_null()&&!unavailable.is_null()&&!mismatch.is_null()&&!result["schemaInvalidEvents"].is_null();
    result["diagnosticCountersAvailable"]=haveCounters;
    if(haveCounters){result["rawCallbacksEntered"]=entered;result["accountUnavailableCallbacks"]=unavailable;result["accountMismatchCallbacks"]=mismatch;}

    if(!yes(f,"available")||!yes(f,"observerInstalled")||!f.contains("events")||!f["events"].is_array()||f["events"].size()>64||
        !text(f,"implementation","steam-app-lifetime-feedback-v1")||
        !text(f,"source","steam-game-sessions-app-lifetime-subscription"))return result;
    result["available"]=true;
    std::uint64_t latest=0;unsigned original=0,other=0;bool haveOriginal=false,lastRunning=false;LifetimeJson last=LifetimeJson();
    std::string firstInstance,lastInstance;bool multipleInstances=false;
    for(const auto& event:f["events"]){
        std::uint32_t eventApp=0;std::string instance;
        if(!lifetimeUInt(event,"unAppID",eventApp)||!event.contains("bRunning")||!event["bRunning"].is_boolean()||!lifetimeInstanceId(event,instance)){result["available"]=false;return result;}
        std::uint64_t sequence=0;
        if(!event.contains("sequence")||(!event["sequence"].is_number_unsigned()&&!event["sequence"].is_number_integer())){result["available"]=false;return result;}
        try{sequence=event["sequence"].get<std::uint64_t>();}catch(...){result["available"]=false;return result;}
        latest=std::max(latest,sequence);
        if(eventApp==appid){
            ++original;haveOriginal=true;lastRunning=event["bRunning"].get<bool>();last=event;
            if(firstInstance.empty())firstInstance=instance;else if(firstInstance!=instance)multipleInstances=true;lastInstance=instance;
        } else ++other;
    }
    result["latestEventSequence"]=latest;result["originalEventCount"]=original;result["otherAppEventCount"]=other;
    result["originalAppRunningKnown"]=haveOriginal;result["originalAppRunning"]=haveOriginal&&lastRunning;
    if(original==0)result["eventRelation"]=other?"other-app-only":"no-events";
    else if(lastRunning&&original==1)result["eventRelation"]="original-app-running-event";
    else if(!lastRunning&&original==1)result["eventRelation"]="original-app-stopped-event";
    else result["eventRelation"]=lastRunning?"original-app-latest-running":"original-app-latest-stopped";
    if(original==0)result["instanceRelation"]="no-original-app-event";
    else if(multipleInstances)result["instanceRelation"]="multiple-steam-instance-ids";
    else result["instanceRelation"]="single-steam-instance-id";
    if(haveOriginal){result["lastOriginalEvent"]=last;result["lastOriginalSteamInstanceID"]=lastInstance;}
    return result;
}
} // namespace ymcc::steamlink
