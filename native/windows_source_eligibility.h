#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "windows_sensor_probe.h"

namespace ymcc::sensors::selection {
enum class TransformPolicy { Unspecified, ExplicitCurrentModel };
struct ModelRecipe {
    std::string deviceClass,gyroMatrixId,accelMatrixId,gyroSha256,accelSha256;
    bool windowsRoute=false,bound=false;
    static bool digest(const std::string& value) noexcept {
        if(value.size()!=64)return false;
        for(unsigned char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;
        return true;
    }
    bool valid() const noexcept {
        return windowsRoute&&bound&&!deviceClass.empty()&&!gyroMatrixId.empty()&&!accelMatrixId.empty()&&
            digest(gyroSha256)&&digest(accelSha256);
    }
};
inline std::string field(const std::string& value){return std::to_string(value.size())+":"+value;}
inline std::string fingerprint(const ModelRecipe& recipe) {
    return field(recipe.deviceClass)+field(recipe.gyroMatrixId)+field(recipe.accelMatrixId)+
        field(probe::canonical(recipe.gyroSha256))+field(probe::canonical(recipe.accelSha256))+"model-native-v1";
}
struct Source {
    inventory::Kind kind=inventory::Kind::Gyro;
    std::string id,provider;
    bool standardUnits=false;
    probe::Series observations;
    std::string api="winrt";
};
struct Evidence {
    ModelRecipe model;
    std::vector<Source> sources;
    std::uint64_t probeSerial=0,now=0;
    bool recorded=false,contextCurrent=false;
};
struct PairChoice {std::string gyroId,accelId;};
inline bool validSensorApiId(const std::string& id) {
    const auto value=probe::canonical(id);
    if(value.size()!=48||value.compare(0,11,"SENSORAPI:{")!=0||value.back()!='}')return false;
    bool nonzero=false;
    for(unsigned i=11;i<47;++i){
        if(i==19||i==24||i==29||i==34){if(value[i]!='-')return false;continue;}
        const char c=value[i];if(!((c>='0'&&c<='9')||(c>='A'&&c<='F')))return false;
        nonzero=nonzero||c!='0';
    }
    return nonzero;
}
inline std::string sourceApi(const std::string& id) {
    if(id.empty())return {};
    if(probe::canonical(id).compare(0,10,"SENSORAPI:")==0)return validSensorApiId(id)?"sensorapi":std::string{};
    return "winrt";
}
inline std::array<std::string,2> sourceApis(const PairChoice& pair){return {sourceApi(pair.gyroId),sourceApi(pair.accelId)};}
inline const char* standardSdkLayout(unsigned kind) noexcept {return kind==0?"sdk-motion-gyro-dps":"sdk-motion-accel-g";}
inline bool nativeReportedFrame(const std::string& api,unsigned kind,const probe::Series& series) {
    if(kind>1)return false;
    if(api=="winrt")return series.readingTransform&&*series.readingTransform==0&&!series.readingTransformError&&
        series.standardSdkLayout.empty()&&!series.standardSdkLayoutError;
    return api=="sensorapi"&&!series.readingTransform&&!series.readingTransformError&&
        !series.standardSdkLayoutError&&series.standardSdkLayout==standardSdkLayout(kind);
}
inline bool same(const std::string& a,const std::string& b){return probe::canonical(a)==probe::canonical(b);}
inline bool same(const PairChoice& a,const PairChoice& b){return same(a.gyroId,b.gyroId)&&same(a.accelId,b.accelId);}
struct Decision {
    PairChoice pair;
    std::string provider;
    bool observedComplete=false,rawCurrent=false,nativeFrame=false,modelAvailable=false,configurable=false;
    const char* reason="not-evaluated";
};
inline bool completeObservedSeries(const Source& value) noexcept {
    const auto& series=value.observations;
    return value.standardUnits&&series.requested&&series.opened&&!series.openError&&series.lastReportUsable&&
        series.distinct>=2&&series.first&&series.last&&series.first->timestamp100ns>0&&
        series.last->timestamp100ns>series.first->timestamp100ns&&series.first->receipt>0&&
        series.last->receipt>=series.first->receipt&&probe::finite(*series.first)&&probe::finite(*series.last);
}
inline bool completeReportedSeries(const Source& value,std::uint64_t now,std::uint64_t minReceipt=0) noexcept {
    return completeObservedSeries(value)&&probe::ready(value.observations,now)&&value.observations.first->receipt>=minReceipt;
}
inline Decision evaluate(const Evidence& evidence,const PairChoice& pair) {
    Decision result;result.pair=pair;result.modelAvailable=evidence.model.valid();
    if(!evidence.recorded||!evidence.probeSerial){result.reason="probe-unobserved";return result;}
    if(!evidence.contextCurrent){result.reason="source-context-not-current";return result;}
    if(pair.gyroId.empty()||pair.accelId.empty()){result.reason="explicit-pair-required";return result;}
    std::array<const Source*,2> sources{};std::array<unsigned,2> counts{};
    for(const auto& source:evidence.sources){if(source.kind!=inventory::Kind::Gyro&&source.kind!=inventory::Kind::Accel)continue;const unsigned kind=source.kind==inventory::Kind::Gyro?0u:1u;
        if(same(source.id,kind==0?pair.gyroId:pair.accelId)){sources[kind]=&source;++counts[kind];}}
    if(!counts[0]||!counts[1]){result.reason="source-id-not-observed";return result;}
    if(counts[0]!=1||counts[1]!=1){result.reason="duplicate-source-evidence";return result;}
    const auto& gyro=*sources[0];const auto& accel=*sources[1];
    const auto apis=sourceApis(pair);
    if(apis[0].empty()||apis[1].empty()||apis[0]!=apis[1]||gyro.api!=apis[0]||accel.api!=apis[1]){
        result.reason="source-api-not-qualified";return result;}
    if(gyro.provider.empty()||accel.provider.empty()||!same(gyro.provider,accel.provider)){
        result.reason="different-or-unknown-providers";return result;}
    result.provider=gyro.provider;
    result.observedComplete=completeObservedSeries(gyro)&&completeObservedSeries(accel);
    if(!result.observedComplete){result.reason="complete-reported-observation-required";return result;}
    if(evidence.now<gyro.observations.last->receipt||evidence.now<accel.observations.last->receipt){result.reason="observation-clock-invalid";return result;}
    result.rawCurrent=completeReportedSeries(gyro,evidence.now)&&completeReportedSeries(accel,evidence.now);
    for(unsigned kind=0;kind<2;++kind){const auto& series=sources[kind]->observations;
        if(apis[kind]=="winrt"&&(!series.readingTransform||series.readingTransformError)){
            result.reason="reading-frame-unobserved";return result;}
        if(apis[kind]=="sensorapi"&&!nativeReportedFrame(apis[kind],kind,series)){
            result.reason="standard-sdk-native-frame-unobserved";return result;}}
    result.nativeFrame=nativeReportedFrame(apis[0],0,gyro.observations)&&nativeReportedFrame(apis[1],1,accel.observations);
    if(!result.nativeFrame){result.reason="reading-frame-is-remapped";return result;}
    if(!result.modelAvailable){result.reason="reviewed-model-recipe-unavailable";return result;}
    result.configurable=true;result.reason="explicit-model-policy-can-be-staged;fresh-data-required-before-adoption";return result;
}
inline std::vector<Decision> choices(const Evidence& evidence) {
    std::vector<Decision> result;
    for(const auto& gyro:evidence.sources)if(gyro.kind==inventory::Kind::Gyro&&!gyro.id.empty())
        for(const auto& accel:evidence.sources)if(accel.kind==inventory::Kind::Accel&&!accel.id.empty()){
            if(gyro.provider.empty()||!same(gyro.provider,accel.provider))continue;
            const PairChoice pair{gyro.id,accel.id};bool duplicate=false;
            for(const auto& prior:result)duplicate=duplicate||same(prior.pair,pair);
            if(!duplicate&&result.size()<64)result.push_back(evaluate(evidence,pair));
        }
    return result;
}
struct Proposal {
    std::uint64_t requestId=0,probeSerial=0;
    PairChoice pair;
    TransformPolicy policy=TransformPolicy::Unspecified;
    std::string expectedRecipeFingerprint;
};
inline const char* authorizeIntent(const ModelRecipe& model,const Proposal& proposal) {
    if(!proposal.requestId)return "request-id-required";
    if(proposal.policy!=TransformPolicy::ExplicitCurrentModel)return "explicit-transform-policy-required";
    if(proposal.pair.gyroId.empty()||proposal.pair.accelId.empty())return "explicit-pair-required";
    const auto apis=sourceApis(proposal.pair);
    if(apis[0].empty()||apis[1].empty())return "source-api-id-invalid";
    if(apis[0]!=apis[1])return "mixed-source-api-not-supported";
    if(!model.valid()||proposal.expectedRecipeFingerprint!=fingerprint(model))return "model-recipe-changed";
    return nullptr;
}
inline const char* authorize(const Evidence& evidence,const Proposal& proposal) {
    if(const auto* reason=authorizeIntent(evidence.model,proposal))return reason;
    if(proposal.probeSerial!=evidence.probeSerial||!proposal.probeSerial)return "probe-replaced";
    const auto decision=evaluate(evidence,proposal.pair);return decision.configurable?nullptr:decision.reason;
}
inline std::string calibrationKey(const ModelRecipe& model,const PairChoice& pair,TransformPolicy policy) {
    if(!model.valid()||policy!=TransformPolicy::ExplicitCurrentModel||pair.gyroId.empty()||pair.accelId.empty())return {};
    const auto apis=sourceApis(pair);if(apis[0].empty()||apis[0]!=apis[1])return {};
    return "YMCC-WINDOWS-EXPLICIT-V1|"+field(fingerprint(model))+field(probe::canonical(pair.gyroId))+
        field(probe::canonical(pair.accelId))+(apis[0]=="sensorapi"?"SENSORAPI-DPS-G-NATIVE":"WINRT-DPS-G-NATIVE");
}
} // namespace ymcc::sensors::selection
