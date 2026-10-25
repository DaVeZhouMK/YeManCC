// Local ACK identity projection only; never authorizes input, Steam state or recovery.
#pragma once
#include <string>
#include <cstdint>
#include <json.hpp>
namespace ymcc::inputChainAck {
using Json = nlohmann::json;
struct View {
    bool nativeAccepted = false;
    bool requestBindingCaptured = false;
    bool fastReceiptUsed = false;
    std::uint64_t protocolPowerGeneration = 0;
    std::uint64_t hostEpoch = 0;
    std::uint64_t requestedSequence = 0;
    const std::string* runId = nullptr;
    const std::string* boundHostInstanceId = nullptr;
    const std::string* preparedRequestId = nullptr;
    const std::string* parsedFastHostInstanceId = nullptr;
    const Json* parsedDomReceipt = nullptr;
};
inline bool unsignedValue(const Json& object, const char* name, std::uint64_t& out) {
    const auto it=object.find(name);
    if(it==object.end())return false;
    if(it->is_number_unsigned()){out=it->get<std::uint64_t>();return true;}
    if(it->is_number_integer()){const auto value=it->get<std::int64_t>();if(value>=0){out=static_cast<std::uint64_t>(value);return true;}}
    return false;
}
inline const std::string* stringValue(const Json& object, const char* name) {
    const auto it=object.find(name);
    return it!=object.end()&&it->is_string()?&it->get_ref<const std::string&>():nullptr;
}
// Call only at existing bounded evidence events, under the submission mutex.
// Fast identity comes from the strict validator's proven equality plus its
// parsed host ID. DOM identity comes from the already accepted parsed reply.
// Current shared power/epoch samples must never stand in for this request view.
inline Json project(const View& view) noexcept {
    try {
        Json result={{"schema",1},{"authority","native-local-ack-diagnostic"},
            {"nativeAccepted",view.nativeAccepted},{"tupleVerified",false},
            {"status",view.nativeAccepted?"unknown":"not-accepted"},{"identityOrigin","none"},
            {"gameInputUnverified",true},{"steamConsumptionProven",false}};
        if(!view.nativeAccepted)return result;
        if(!view.requestBindingCaptured||view.protocolPowerGeneration==0||view.hostEpoch==0||view.requestedSequence==0||
           !view.runId||view.runId->empty()||!view.boundHostInstanceId||view.boundHostInstanceId->empty())return result;
        const std::string* requestId=nullptr;
        const std::string* hostId=nullptr;
        if(view.fastReceiptUsed) {
            if(!view.preparedRequestId||view.preparedRequestId->empty()||!view.parsedFastHostInstanceId||
               *view.parsedFastHostInstanceId!=*view.boundHostInstanceId)return result;
            requestId=view.preparedRequestId;hostId=view.parsedFastHostInstanceId;
            result["identityOrigin"]="strict-fast-validator-equality-and-parsed-host";
        } else {
            const auto* reply=view.parsedDomReceipt;
            if(!reply||!reply->is_object())return result;
            const auto ok=reply->find("ok");
            const auto* command=stringValue(*reply,"command");const auto* status=stringValue(*reply,"status");
            const auto* run=stringValue(*reply,"runId");hostId=stringValue(*reply,"hostInstanceId");requestId=stringValue(*reply,"requestId");
            std::uint64_t protocol=0,power=0,epoch=0,sequence=0;
            if(ok==reply->end()||!ok->is_boolean()||!ok->get<bool>()||!command||*command!="SUBMIT_FRAME"||!status||*status!="frame-accepted"||
               !requestId||requestId->empty()||!run||*run!=*view.runId||!hostId||*hostId!=*view.boundHostInstanceId||
               !unsignedValue(*reply,"protocolVersion",protocol)||protocol!=1||
               !unsignedValue(*reply,"powerGeneration",power)||power!=view.protocolPowerGeneration||
               !unsignedValue(*reply,"epoch",epoch)||epoch!=view.hostEpoch||
               !unsignedValue(*reply,"sequence",sequence)||sequence!=view.requestedSequence||
               (view.preparedRequestId&&!view.preparedRequestId->empty()&&*requestId!=*view.preparedRequestId))return result;
            result["identityOrigin"]="existing-validator-and-parsed-dom-reply";
        }
        result["status"]="verified-local-ack";result["tupleVerified"]=true;
        result["requestBindingCapturedBeforeRequest"]=true;
        result["protocolPowerGeneration"]=view.protocolPowerGeneration;
        result["hostEpoch"]=view.hostEpoch;result["wireSequence"]=view.requestedSequence;
        result["runId"]=*view.runId;result["hostInstanceId"]=*hostId;result["requestId"]=*requestId;
        result["protocolGenerationScope"]="input-host-bound-protocol;not-current-shared-power";
        return result;
    } catch(...) { return Json(nullptr); }
}
}
