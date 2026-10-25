#pragma once
#include <json.hpp>
#include <cstdint>
#include <mutex>
#include <string>
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
public:
    static constexpr uint64_t maxRows=128;
    Json statistics(){std::lock_guard<std::mutex> lock(mx);
        return {{"reservedRows",rows},{"rowLimitPerProcess",maxRows},{"capReached",rows>=maxRows},
            {"droppedForCap",dropped},{"reservedRowsAreDurableLogProof",false},
            {"coverageMeaning","current-process-reservations-not-export-snapshot-atomicity-or-durable-write-proof"}};
    }
    Json take(bool eligible,Json row,bool deduplicate){
        if(!eligible)return nullptr;
        std::lock_guard<std::mutex> lock(mx);
        if(deduplicate){auto signature=row;signature.erase("savedInputRevision");
            if(signature==last)return nullptr;last=std::move(signature);}
        if(rows>=maxRows){++dropped;return nullptr;}
        row["receiptSequence"]=++sequence;row["rowLimitPerProcess"]=maxRows;
        row["droppedReceipts"]=dropped;++rows;return row;
    }
};
}
