#pragma once
#include "json.hpp"
#include <cmath>
#include <limits>

// Constants are checked against PowerControl/settings-baseline.json at build time.
namespace ymcc::settingsBaseline {
using Json = nlohmann::json;
inline constexpr const char* kId = "ymcc-settings";
inline constexpr long long kRevision = 1;
inline bool integer(const Json& value) {
    if (!value.is_number()) return false;
    const auto number = value.get<double>();
    return std::isfinite(number) && std::floor(number) == number;
}
// A malformed persisted revision is never an implicit JSON number conversion.
// Expected-revision CAS still rejects malformed disk revisions separately.
inline long long integerOr(const Json& object,const char* key,long long fallback) noexcept {
    try {
        if(!object.is_object())return fallback;
        const auto it=object.find(key);if(it==object.end())return fallback;
        if(it->is_number_unsigned()){
            const auto value=it->get<unsigned long long>();
            return value<=static_cast<unsigned long long>((std::numeric_limits<long long>::max)())?static_cast<long long>(value):fallback;
        }
        return it->is_number_integer()?it->get<long long>():fallback;
    }catch(...){return fallback;}
}
inline Json parseDocument(const std::string& raw) noexcept {
    // Abort before building a recursive value that cannot be safely normalized,
    // copied or serialized. The caller still owns the untouched raw preimage.
    struct DepthLimit {};
    try {
        return Json::parse(raw,[](int depth,Json::parse_event_t,Json&){
            if(depth>64)throw DepthLimit{};return true;
        },false);
    }catch(...){return Json(Json::value_t::discarded);}
}
inline bool supported(const Json& document) {
    if (!document.is_object()) return false;
    const auto schema = document.find("schemaVersion");
    if (schema != document.end()) {
        if (!integer(*schema) || *schema < 0 || *schema > 1) return false;
    }
    const auto stamp = document.find("configurationBaseline");
    if (stamp == document.end()) return true;
    if (!stamp->is_object()) return false;
    const auto id = stamp->find("id"), revision = stamp->find("revision"), imported = stamp->find("legacyImported");
    return id != stamp->end() && *id == kId && revision != stamp->end() && integer(*revision) &&
        *revision >= 1 && *revision <= kRevision && imported != stamp->end() && *imported == true;
}
// Native recovery history is opaque to ordinary settings writers. Combining
// conflicting copies cannot select a ticket and accidentally grant a refund.
inline Json cleanupHistoryUnconfirmed() {
    return {{"schemaVersion", 1}, {"history", "unconfirmed"}};
}
inline void preserveNativeCleanupHistory(Json& next, const Json& primary,
    const Json& backup, bool unconfirmed = false) {
    for(const char* key:{"deckyContextCleanup","deckyBigPictureRecovery"}){
        const auto left = primary.find(key);
        const auto right = backup.find(key);
        const auto unknown = cleanupHistoryUnconfirmed();
        if (unconfirmed) { next[key] = unknown; continue; }
        if (left != primary.end() && right != backup.end() && *left != *right) {
            next[key] = unknown;
        } else if (left != primary.end()) {
            next[key] = *left;
        } else if (right != backup.end()) {
            next[key] = *right;
        } else {
            next.erase(key);
        }
    }
}
// A damaged document has no proof of applied hardware or automatic ownership.
inline Json& recoveryObject(Json& parent,const char* key) {
    if(!parent.is_object())parent=Json::object();
    auto& value=parent[key];if(!value.is_object())value=Json::object();return value;
}
inline void deactivateRecoveredState(Json& next) {
    next["deckyContextCleanup"] = cleanupHistoryUnconfirmed();
    next["deckyBigPictureRecovery"] = cleanupHistoryUnconfirmed();
    auto& startup=recoveryObject(next,"startupDesired");
    startup["virtualGamepadPersona"]="disabled";startup["gyroPreset"]="off";
    for(const auto* key:{"fanControl","rtss","energyStar","memoryCleanup","steamCommunity"})startup[key]=false;
    recoveryObject(next,"performanceSchedule")["enabled"]=false;
    auto& tdp=recoveryObject(next,"tdp");recoveryObject(tdp,"autoApply")={{"boot",false},{"wake",false}};
    auto& cpu=recoveryObject(next,"cpu");recoveryObject(cpu,"autoEnable")["mode"]="off";
    auto& autostart=recoveryObject(cpu,"autostart");recoveryObject(autostart,"ccd")["enabled"]=false;recoveryObject(autostart,"uv")["enabled"]=false;
    auto& fan=recoveryObject(next,"fan");fan["featureEnabled"]=false;fan["motionEnabled"]=false;
    auto& sleep=recoveryObject(next,"sleep");for(const auto* key:{"autoWakeRecovery","staticWakeAutoScanEnabled","sleepPowerPlanOptimizationEnabled"})sleep[key]=false;
    auto& input=recoveryObject(next,"input");auto& target=recoveryObject(input,"outputTarget");
    target["persona"]="disabled";target["buttonMappingEnabled"]=false;target["gyroEnabled"]=false;target["descriptorHash"]=nullptr;target["closure"]="UNENCLOSED";
    auto& gyro=recoveryObject(input,"gyroMotion");gyro["enabled"]=false;gyro["outputMode"]="disabled";gyro["motionMode"]="off";
    for(const auto* key:{"appliedRevision","appliedConfigHash","pendingConfigHash","hostAcknowledgedRevision","hostAcknowledgedConfigHash"})input[key]=nullptr;
    input["applyStatus"]="safe-stop";
}
// Disk commit and runtime refresh have different outcomes. A failed consumer
// cannot retract a durable transaction or prevent independent consumers running.
template<class Consumer>
inline void attemptRefresh(Json& warnings,const char* name,Consumer&& consumer) noexcept {
    try {consumer();}catch(...) {try {warnings.push_back(name);}catch(...) {}}
}
inline void preserveReceipt(Json& next, const Json& current) {
    if (current.is_object() && current.contains("configurationBaseline") && !next.contains("configurationBaseline"))
        next["configurationBaseline"] = current["configurationBaseline"];
}
}
