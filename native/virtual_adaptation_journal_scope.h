#pragma once
#include <string_view>
#include "json.hpp"

namespace ymcc::adaptlog::journalScope {
inline constexpr char markerField[]="journalScopeSchema";
inline constexpr unsigned version=1;
inline constexpr char timestampScope[]="journal-row-creation;sample-capturedTickMs-is-boot-monotonic";
inline constexpr char virtualPolicy[]="first-step-within-operation;generation-binds-causality;not-per-frame-sequence";
inline constexpr char scope[]="observed-evidence-only;not-action-execution-or-hardware-success-proof";
inline constexpr char sequenceScope[]="adaptation-journals-in-process;record-creation;gaps-allowed";

inline constexpr char frameSampleMarker[]="virtualFrameSampleSchema";
inline constexpr unsigned frameSampleVersion=1;
inline constexpr const char* frameSampleCategories[]={"virtual-submit-first-witness","virtual-sdk-first-witness"};
inline const nlohmann::json& frameSampleFields() {
    static const nlohmann::json fields={{"transportSecretsRetained",false},
        {"requestFields","allowlisted-controls-and-tuple;startupNonce-and-transport-auth-omitted"},
        {"bindingCapturedBeforeRequest",true},
        {"frameScope","actual-submit-control-values;transport-auth-omitted;not-driver-report-bytes"},
        {"hostSdkDeliveryProven",false},{"physicalSuppressionProven",false},
        {"steamConsumptionProven",false},{"gameConsumptionProven",false}};
    return fields;
}
inline bool compactFrameSample(nlohmann::json& row,std::string_view category) {
    if(category!=frameSampleCategories[0] && category!=frameSampleCategories[1])return false;
    const auto sample=row.find("firstSample");
    if(sample==row.end() || !sample->is_object() || row.contains(frameSampleMarker))return false;
    const auto& fields=frameSampleFields();
    for(auto it=fields.begin();it!=fields.end();++it){
        const auto value=sample->find(it.key());
        if(value==sample->end() || value->type()!=it->type() || *value!=*it)return false;
    }
    row[frameSampleMarker]=frameSampleVersion;
    for(auto it=fields.begin();it!=fields.end();++it)sample->erase(it.key());
    return true;
}
}
