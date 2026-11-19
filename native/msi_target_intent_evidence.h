#pragma once
#include <json.hpp>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
namespace ymcc::msi::intentEvidence {
using Json=nlohmann::json;
inline bool enabled(bool build,bool knownBoard,bool detailed) noexcept {return build&&knownBoard&&detailed;}
// Metadata only. Missing/invalid fields remain unknown; never invent disabled.
inline Json scalar(const Json& object,const char* key,bool boolean=false){
    if(!object.is_object()||!object.contains(key))return nullptr;
    const auto& v=object[key];
    if(boolean)return v.is_boolean()?v:Json(nullptr);
    if(v.is_string()&&v.get_ref<const std::string&>().size()<=32)return v;
    return nullptr;
}
inline Json target(const Json& input){
    const auto it=input.is_object()?input.find("outputTarget"):input.end();
    const Json t=it!=input.end()&&it->is_object()?*it:Json(nullptr);
    return {{"inputObjectKnown",input.is_object()},{"targetObjectKnown",t.is_object()},
        {"persona",scalar(t,"persona")},{"buttonMappingEnabled",scalar(t,"buttonMappingEnabled",true)},
        {"gyroEnabled",scalar(t,"gyroEnabled",true)}};
}
inline Json revision(const Json& input){
    if(!input.is_object()||!input.contains("revision")||!input["revision"].is_number_integer())return nullptr;
    const auto& r=input["revision"];if(r.is_number_unsigned()&&r.get<uint64_t>()>9007199254740991ULL)return nullptr;
    if(r.is_number_integer()&&!r.is_number_unsigned()&&r.get<int64_t>()<0)return nullptr;
    return r;
}
inline Json observation(const Json& saved,const Json& effective,bool shortcutActive,bool gameOverrideActive){
    const auto s=target(saved),e=target(effective);
    return {{"formatVersion",1},{"stage","effective-input-observation"},{"savedTarget",s},{"effectiveTarget",e},
        {"savedEffectiveTargetEqual",s==e},{"savedInputRevision",revision(saved)},
        {"temporaryShortcutActive",shortcutActive},{"gameOverrideActive",gameOverrideActive},
        {"source","existing-settings-read-plus-existing-runtime-overlay"},{"isSettingsWriteReceipt",false},
        {"humanGestureProven",false},{"legacyOperatorSettingProof","queue-reason-derived-not-human-proof"},
        {"legacyRequestBeatProof","effective-refresh-not-UI-click-proof"},{"diagnosticOnly",true},
        {"writesControls",false},{"crossSystemAtomicSnapshot",false}};
}
inline Json documentWrite(const Json& requested,const Json& baseline,const Json& committed,
                          bool ok,bool conflict,int64_t staleDropped,int64_t expectedRevision){
    return {{"formatVersion",1},{"stage","ipc-document-write-result"},{"source","native-ipc.settings.write-handler"},
        {"sourceHandlerProven",true},{"requestBaselineIsAuthoritativeBefore",false},
        {"requestedTarget",target(requested)},{"requestBaselineTarget",target(baseline)},
        {"committedTarget",target(committed)},{"committedInputRevision",revision(committed)},
        {"writeOk",ok},{"inputRevisionConflict",conflict},{"staleInputDropped",staleDropped>=0},
        {"staleDroppedRevision",staleDropped>=0?Json(staleDropped):Json(nullptr)},
        {"expectedInputRevision",expectedRevision>=0?Json(expectedRevision):Json(nullptr)},
        {"requestAcceptedAsHumanAction",false},{"humanGestureProven",false},
        {"diagnosticOnly",true},{"writesControls",false}};
}
class BoundedObserver {
    std::mutex mx;Json last=nullptr;uint64_t sequence=0,rows=0,dropped=0;
    uint64_t lastDeliveryEpoch=0;
    static const Json* field(const Json& object,const char* key) noexcept {
        if(!object.is_object())return nullptr;
        const auto it=object.find(key);return it==object.end()?nullptr:&*it;
    }
    static bool booleanField(const Json& object,const char* key,bool expected) noexcept {
        const auto* value=field(object,key);return value&&value->is_boolean()&&value->get<bool>()==expected;
    }
    static bool textField(const Json& object,const char* key,std::string_view expected) noexcept {
        const auto* value=field(object,key);return value&&value->is_string()&&value->get_ref<const std::string&>()==expected;
    }
    // Match the original target() normalization by borrowed scalar values.
    // Any unfamiliar stored shape falls through to the original JSON path.
    static bool normalizedTargetMatches(const Json& stored,const Json& input) noexcept {
        const auto* raw=field(input,"outputTarget");const auto* target=raw&&raw->is_object()?raw:nullptr;
        if(!stored.is_object()||stored.size()!=5||!booleanField(stored,"inputObjectKnown",input.is_object())||
                !booleanField(stored,"targetObjectKnown",target!=nullptr))return false;
        const auto* persona=target?field(*target,"persona"):nullptr;
        const auto* prior=field(stored,"persona");if(!prior)return false;
        const bool knownPersona=persona&&persona->is_string()&&persona->get_ref<const std::string&>().size()<=32;
        if(knownPersona){if(!prior->is_string()||prior->get_ref<const std::string&>()!=persona->get_ref<const std::string&>())return false;}
        else if(!prior->is_null())return false;
        for(const auto* key:{"buttonMappingEnabled","gyroEnabled"}) {
            const auto* value=target?field(*target,key):nullptr;const auto* old=field(stored,key);if(!old)return false;
            if(value&&value->is_boolean()){if(!old->is_boolean()||old->get<bool>()!=value->get<bool>())return false;}
            else if(!old->is_null())return false;
        }
        return true;
    }
public:
    static constexpr uint64_t maxRows=128;
    Json statistics(){std::lock_guard<std::mutex> lock(mx);
        return {{"reservedRows",rows},{"rowLimitPerProcess",maxRows},{"capReached",rows>=maxRows},
            {"droppedForCap",dropped},{"reservedRowsAreDurableLogProof",false},
            {"coverageMeaning","current-process-reservations-not-export-snapshot-atomicity-or-durable-write-proof"}};
    }
    // Read-only preflight against the SAME last signature. The original take()
    // alone updates it and reserves rows; no state is consumed on preparation failure.
    bool repeatsObservation(const Json& saved,const Json& effective,bool shortcut,bool gameOverride,uint64_t deliveryEpoch=0) {
        std::lock_guard<std::mutex> lock(mx);
        if(lastDeliveryEpoch!=deliveryEpoch)return false;
        if(!last.is_object()||last.size()!=15)return false;
        const auto* version=field(last,"formatVersion");
        const auto* oldSaved=field(last,"savedTarget");const auto* oldEffective=field(last,"effectiveTarget");
        if(!version||!version->is_number_integer()||*version!=1||!oldSaved||!oldEffective||
                !normalizedTargetMatches(*oldSaved,saved)||!normalizedTargetMatches(*oldEffective,effective))return false;
        return textField(last,"stage","effective-input-observation")&&
            booleanField(last,"savedEffectiveTargetEqual",*oldSaved==*oldEffective)&&
            booleanField(last,"temporaryShortcutActive",shortcut)&&booleanField(last,"gameOverrideActive",gameOverride)&&
            textField(last,"source","existing-settings-read-plus-existing-runtime-overlay")&&
            booleanField(last,"isSettingsWriteReceipt",false)&&booleanField(last,"humanGestureProven",false)&&
            textField(last,"legacyOperatorSettingProof","queue-reason-derived-not-human-proof")&&
            textField(last,"legacyRequestBeatProof","effective-refresh-not-UI-click-proof")&&
            booleanField(last,"diagnosticOnly",true)&&booleanField(last,"writesControls",false)&&
            booleanField(last,"crossSystemAtomicSnapshot",false);
    }
    Json take(bool eligible,Json row,bool deduplicate,uint64_t deliveryEpoch=0){
        if(!eligible)return nullptr;
        std::lock_guard<std::mutex> lock(mx);
        if(deduplicate){auto signature=row;signature.erase("savedInputRevision");
            if(signature==last&&lastDeliveryEpoch==deliveryEpoch)return nullptr;
            last=std::move(signature);lastDeliveryEpoch=deliveryEpoch;}
        if(rows>=maxRows){++dropped;return nullptr;}
        row["receiptSequence"]=++sequence;row["rowLimitPerProcess"]=maxRows;
        row["droppedReceipts"]=dropped;++rows;return row;
    }
};
}
