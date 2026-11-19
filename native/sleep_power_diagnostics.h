#pragma once
#include "json.hpp"
#include "sleep_power_state.h"
#include <string_view>

namespace ymcc::sleeppower {
// Passive export evidence. Renderer claims never grant native power authority.
inline nlohmann::json exportDiagnostics(bool coherent,const SharedPowerV1& native,
                                      const nlohmann::json& renderer) {
    using Json=nlohmann::json;
    auto copyFields=[](const Json& from,Json& to,std::initializer_list<const char*> fields){
        if(!from.is_object())return;
        for(const auto* key:fields){
            const auto it=from.find(key);if(it==from.end())continue;
            const std::string_view field=key;
            const bool boolean=field=="active"||field=="known"||field=="inputReady"||field=="hardwareWritesAllowed"||
                field=="notificationPending"||field=="inFlight"||field=="queued"||field=="followup"||
                field=="demand"||field=="retryTimerActive";
            if(it->is_null())to[key]=*it;
            else if(boolean){if(it->is_boolean())to[key]=*it;}
            else if(it->is_number_unsigned()&&it->get<std::uint64_t>()<=9007199254740991ULL)to[key]=*it;
            else if(it->is_number_integer()&&it->get<std::int64_t>()>=0&&it->get<std::int64_t>()<=9007199254740991LL)to[key]=*it;
        }
    };
    Json state={{"known",coherent},{"source","native-cached-shared-power-reader"}};
    if(coherent){
        state["generation"]=native.generation;state["publicationRevision"]=native.publicationRevision;
        state["observedNotificationSequence"]=native.observedNotificationSequence;
        state["phase"]=native.phase;state["suspendSerial"]=native.suspendSerial;
        state["hardwareWritesAllowed"]=(native.flags&kHardwareAllowed)!=0;
        state["notificationPending"]=(native.flags&kNotificationPending)!=0;
    }
    Json frontend={{"present",renderer.is_object()},{"scope","renderer-request-cache;not-native-power-authority"}};
    if(renderer.is_object()){
        copyFields(renderer,frontend,{"schemaVersion","active","lifetimeEpoch","subscriberCount"});
        for(const auto* group:{"snapshot","read","counters","lastObservation"}){
            const auto at=renderer.find(group);if(at==renderer.end()||!at->is_object())continue;
            Json value=Json::object();
            copyFields(*at,value,{"known","generation","publicationRevision","observedNotificationSequence",
                "inputReady","hardwareWritesAllowed","notificationPending","inFlight","queued","followup","demand",
                "retryStep","retryTimerActive","pendingReadyConfirmations","readStarted","readKnown","readUnknown",
                "readStale","readFailed","authorityEvents","published","confirmationDelivered","requestVersion","lifetimeEpoch"});
            for(const auto* key:{"phase","source","outcome"}){
                const auto text=at->find(key);
                if(text==at->end()||!text->is_string())continue;
                const auto& label=text->get_ref<const std::string&>();if(label.size()>48)continue;
                bool safe=true;for(const auto c:label)if(!((c>='a'&&c<='z')||c=='-'||c==':')){safe=false;break;}
                if(safe)value[key]=label;
            }
            frontend[group]=std::move(value);
        }
    }
    return {{"schemaVersion",1},{"evidenceScope","export-request-cache;not-atomic-across-components"},
        {"native",std::move(state)},{"renderer",std::move(frontend)},{"grantsPowerAuthority",false}};
}
}
