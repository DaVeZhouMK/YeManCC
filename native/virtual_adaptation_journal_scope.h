#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include "json.hpp"

namespace ymcc::adaptlog::journalScope {
inline constexpr char markerField[]="journalScopeSchema";
inline constexpr unsigned version=1;
inline constexpr char timestampScope[]="journal-row-creation;sample-capturedTickMs-is-boot-monotonic";
inline constexpr char virtualPolicy[]="first-step-within-operation;generation-binds-causality;not-per-frame-sequence";
inline constexpr char scope[]="observed-evidence-only;not-action-execution-or-hardware-success-proof";
inline constexpr char sequenceScope[]="adaptation-journals-in-process;record-creation;gaps-allowed";

inline constexpr char writerCopiesMarker[]="journalWriterCopiesV1";
inline constexpr char knownWriterStatus[]="pid-and-process-creation-known";
inline void writeWriterIdentity(nlohmann::json& row,bool virtualStep,std::uint32_t pid,std::uint64_t birth) {
    row["writerPid"]=pid;
    row["writerProcessCreation100ns"]=birth?nlohmann::json(std::to_string(birth)):nlohmann::json(nullptr);
    if(virtualStep&&pid&&birth){
        row[writerCopiesMarker]=1;
    }else{
        row["writerInstanceId"]=birth?nlohmann::json("native:"+std::to_string(pid)+":"+std::to_string(birth)):nlohmann::json(nullptr);
        row["writerIdentityStatus"]=birth?knownWriterStatus:"pid-only;process-creation-unavailable";
    }
}

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

inline constexpr char producerCopiesMarker[]="producerIdentityCopiesV1";
// Stable bit order in the wire format. Existing bits must never be reassigned.
inline constexpr const char* producerCopyFields[]={"runId","epoch","phase","hostInstanceId","reason","requestId",
    "sourceGeneration","sourcePath","ok","command","powerGeneration","stage","hostEpoch","status","policy",
    "persona","wakeSerial","generation","candidateOutcome","outcome","currentBindingValid"};
inline bool compactStepProducer(nlohmann::json& row) {
    if(row.contains(producerCopiesMarker))return false;
    const auto key=row.find("uniqueKey"),sample=row.find("firstSample");
    if(key==row.end()||sample==row.end()||!key->is_object()||!sample->is_object())return false;
    const auto identity=key->find("identity"),producer=sample->find("producer");
    if(identity==key->end()||producer==sample->end()||!identity->is_object()||!producer->is_object())return false;
    unsigned index=0,mask=0;
    size_t savedAtLeast=0;
    for(const auto* name:producerCopyFields){
        const auto bit=1u<<index++;const auto from=identity->find(name),old=producer->find(name);
        if(from==identity->end()||old==producer->end()||from->type()!=old->type()||*from!=*old)continue;
        if(old->is_string())savedAtLeast+=old->get_ref<const std::string&>().size()+2;
        else if(old->is_boolean()||old->is_number_integer())savedAtLeast+=1;
        else continue;
        savedAtLeast+=std::string_view(name).size()+3;
        mask|=bit;
    }
    if(savedAtLeast<=sizeof(producerCopiesMarker)-1+4+10)return false;
    row[producerCopiesMarker]=mask;index=0;
    for(const auto* name:producerCopyFields)if(mask&(1u<<index++))producer->erase(name);
    return true;
}
}
