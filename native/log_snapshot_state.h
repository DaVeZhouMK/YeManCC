#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"

namespace ymcc::logging {
using SnapshotJson = nlohmann::json;
inline constexpr char commonFrameCopiesField[]="frameCopiesV1";
inline constexpr unsigned commonFrameEqualReadTicks=1, commonFrameEqualReceiptSequence=2;

inline void compactCommonFrameCopies(SnapshotJson& row) {
    if (!row.is_object() || row.contains(commonFrameCopiesField)) return;
    const auto equalUnsigned=[](const SnapshotJson& a, const SnapshotJson& b) {
        const auto valid=[](const SnapshotJson& x) {
            return x.is_number_unsigned() || (x.is_number_integer() && x.get<long long>() >= 0);
        };
        return valid(a) && valid(b) && a.get<unsigned long long>() == b.get<unsigned long long>();
    };
    unsigned mask=0;
    const auto source=row.find("source");
    if (source!=row.end() && source->is_object()) {
        const auto begin=source->find("readStartedTick"), end=source->find("readCompletedTick");
        if (begin!=source->end() && end!=source->end() && equalUnsigned(*begin,*end)) mask|=commonFrameEqualReadTicks;
    }
    const auto proof=row.find("frameProof");
    if (proof!=row.end() && proof->is_object()) {
        const auto sample=proof->find("sampleSequence"), ack=proof->find("hostReceipt");
        if (sample!=proof->end() && ack!=proof->end() && ack->is_object()) {
            const auto acknowledged=ack->find("sampleSequence");
            if (acknowledged!=ack->end() && equalUnsigned(*sample,*acknowledged)) mask|=commonFrameEqualReceiptSequence;
        }
    }
    if (!mask) return;
    row.emplace(commonFrameCopiesField,mask);
    if (mask & commonFrameEqualReadTicks) row["source"].erase("readCompletedTick");
    if (mask & commonFrameEqualReceiptSequence) row["frameProof"]["hostReceipt"].erase("sampleSequence");
}

// Classify only bounded exact strings already returned by the HTTP command.
// No original error text, endpoint,header,credential or response is retained.
inline SnapshotJson httpFailureLogDetail(const SnapshotJson& error) {
    const auto result=[](const char* kind,const char* phase=nullptr,const char* api=nullptr,
                         bool codeKnown=false,unsigned long code=0) {
        return SnapshotJson{{"schema",1},{"kind",kind},{"phase",phase?SnapshotJson(phase):SnapshotJson(nullptr)},
            {"api",api?SnapshotJson(api):SnapshotJson(nullptr)},{"codeKnown",codeKnown},
            {"codeDomain",codeKnown?SnapshotJson("winhttp-error-from-reported-text"):SnapshotJson(nullptr)},
            {"code",codeKnown?SnapshotJson(code):SnapshotJson(nullptr)},
            {"scope","exact-existing-error-string;not-independent-api-observation"}};
    };
    if(!error.is_string())return result("unknown");
    const std::string_view text=error.get_ref<const std::string&>();
    if(text.size()>256)return result("unknown");
    struct Literal {const char* text;const char* kind;const char* phase;const char* api;};
    static constexpr Literal known[]={
        {"url is required","invalid-request","url",nullptr},
        {"Invalid URL","invalid-request","url",nullptr},
        {"Only http and https URLs are supported","invalid-request","scheme",nullptr},
        {"Invalid HTTP method","invalid-request","method",nullptr},
        {"Invalid HTTP header","invalid-request","request-headers",nullptr},
        {"WinHttpOpen failed","transport-setup-failed","session","WinHttpOpen"},
        {"WinHttpConnect failed","transport-setup-failed","connect","WinHttpConnect"},
        {"WinHttpOpenRequest failed","transport-setup-failed","request","WinHttpOpenRequest"},
        {"WinHTTP callback registration failed","transport-setup-failed","callback","WinHttpSetStatusCallback"},
        {"WinHTTP request context failed","transport-setup-failed","context","WinHttpSetOption"},
        {"Fan request headers rejected","request-headers-rejected","request-headers",nullptr},
        {"HTTP response exceeded 8 MiB limit","response-limit","body",nullptr},
        {"HTTP request cancelled during shutdown","cancelled","async-request",nullptr},
        {"POWER_RESUMING: Fan request power context retired before transport","power-admission-refused","before-transport",nullptr},
        {"POWER_RESUMING: Fan request power context retired before send","power-admission-refused","before-send",nullptr},
        {"FAN_PUBLIC_POWER_UNAVAILABLE: original Fan operation has no current write grant","power-admission-refused","write-grant",nullptr},
        {"AI_FAN_OWNED_MOCK_REQUIRED","mock-admission-refused","before-transport",nullptr},
        {"native error: http.request","opaque-exception",nullptr,nullptr}
    };
    for(const auto& value:known)if(text==value.text)return result(value.kind,value.phase,value.api);
    const auto phase=[](std::string_view value)->const char* {
        if(value=="queue")return "queue";
        if(value=="send")return "send";
        if(value=="headers")return "headers";
        return value=="body"?"body":nullptr;
    };
    constexpr std::string_view deadline="HTTP request deadline exceeded (phase=";
    if(text.starts_with(deadline)&&text.ends_with(')')) {
        const auto selected=phase(text.substr(deadline.size(),text.size()-deadline.size()-1));
        return selected?result("deadline-exceeded",selected):result("unknown");
    }
    constexpr std::string_view winhttp="HTTP request failed (WinHTTP ";
    if(text.starts_with(winhttp)&&text.ends_with(')')) {
        const auto split=text.find(", phase=",winhttp.size());
        if(split==std::string_view::npos)return result("unknown");
        const auto selected=phase(text.substr(split+8,text.size()-split-9));
        if(!selected||std::string_view(selected)=="queue")return result("unknown");
        const auto digits=text.substr(winhttp.size(),split-winhttp.size());
        if(digits.empty()||digits.size()>10||(digits.size()>1&&digits.front()=='0'))return result("unknown");
        unsigned long code=0;
        for(const char digit:digits) {
            if(digit<'0'||digit>'9'||code>(0xffffffffUL-static_cast<unsigned long>(digit-'0'))/10)return result("unknown");
            code=code*10+static_cast<unsigned long>(digit-'0');
        }
        return result("winhttp-failed",selected,nullptr,true,code);
    }
    return result("unknown");
}

// Original session-marker optional-log catches only. Independent of file-sink
// failure buckets and their recovery epoch; no reoffer, file IO or healthy work.
struct SessionMarkerDiagnosticExceptionEvidence {
    enum Kind : size_t { cleanMarker,failedMarker,Capacity };
    mutable std::mutex mutex;
    std::array<unsigned long long,Capacity> counts{};
    std::atomic<bool> recordingContention{false},recordingException{false};
    void record(Kind kind) noexcept {
        if(kind>=Capacity)return;
        try {
            std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);
            if(!lock){recordingContention.store(true,std::memory_order_relaxed);return;}
            if(counts[kind]!=~0ULL)++counts[kind];
        }catch(...){recordingException.store(true,std::memory_order_relaxed);}
    }
    SnapshotJson snapshot() const {
        std::array<unsigned long long,Capacity> copy{};
        {
            std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);
            if(!lock)return {{"schemaVersion",1},{"snapshotKnown",false},{"reason","contended"}};
            copy=counts;
        }
        auto rows=SnapshotJson::array();
        static constexpr const char* events[]={"session-clean-exit","session-clean-exit-failed"};
        for(size_t i=0;i<Capacity;++i)rows.push_back({{"event",events[i]},
            {"observedExceptions",copy[i]},{"counterLimitReached",copy[i]==~0ULL}});
        return {{"schemaVersion",1},{"snapshotKnown",true},
            {"scope","session-marker-optional-log-call-catches;argument-preparation-or-propagated-sink;independent-of-file-failure-buckets"},
            {"rows",std::move(rows)},{"maxRows",Capacity},{"counterLimit",~0ULL},
            {"recordingContentionObserved",recordingContention.load(std::memory_order_relaxed)},
            {"recordingExceptionObserved",recordingException.load(std::memory_order_relaxed)},
            {"flagsAtomicWithCounts",false},{"countsAreUniqueLostRows",false},
            {"zeroProvesCompleteHistory",false},{"fileWriteFailureProven",false},
            {"markerWriteOutcomeProvenByCounts",false},{"processHostOrDeviceRetirementProven",false}};
    }
};
inline SessionMarkerDiagnosticExceptionEvidence& sessionMarkerDiagnosticExceptionEvidence() noexcept {
    static SessionMarkerDiagnosticExceptionEvidence value;return value;
}

// Failure-only accounting for the original Native file exits. No successful
// row counter, source read, retry or additional log write is performed here.
struct NativeLogFailureEvidence {
    enum Kind : size_t { preparationException, eventOpen, detailOpen, eventWrite, detailWrite,
        eventFlush, detailFlush, eventUnavailable, detailUnavailable, eventException, detailException, Capacity };
    enum class PreparationSource : size_t { none,residentRecord,detailRecord,residentSerialization,detailSerialization,residentPath,detailPath };
    enum class PreselectionStage : size_t { none,steamProjection,nativeClassifier };
    struct Bucket {
        unsigned long long count=0;
        std::array<char,96> firstEvent{};
        size_t length=0;
        bool firstKnown=false,truncated=false,sanitized=false;
    };
    mutable std::mutex mutex;
    std::array<Bucket,Capacity> buckets{};
    std::array<Bucket,2> preselectionStages{};
    std::atomic<bool> eventFailurePending{false};
    std::atomic<bool> screenPreparationFailurePending{false};
    std::atomic<bool> sensorRetirementPreparationFailurePending{false};
    std::atomic<bool> msiFeedbackPreparationFailurePending{false};
    std::atomic<bool> residentRecordPreparationFailurePending{false};
    std::array<unsigned long long,4> recordPreparationSources{};
    std::atomic<bool> residentPathPreparationFailurePending{false};
    std::array<unsigned long long,2> pathPreparationSources{};
    std::atomic<unsigned long long> eventRecoveryEpoch{0};
    unsigned long long recoveryEpoch() const noexcept {return eventRecoveryEpoch.load(std::memory_order_acquire);}
    void observeEventAppendSuccess() noexcept {
        if(!eventFailurePending.load(std::memory_order_acquire)&&!screenPreparationFailurePending.load(std::memory_order_acquire)&&!sensorRetirementPreparationFailurePending.load(std::memory_order_acquire)&&!msiFeedbackPreparationFailurePending.load(std::memory_order_acquire)&&!residentRecordPreparationFailurePending.load(std::memory_order_acquire)&&!residentPathPreparationFailurePending.load(std::memory_order_acquire))return;
        try{
            std::lock_guard<std::mutex> lock(mutex);
            if(!eventFailurePending.load(std::memory_order_relaxed)&&!screenPreparationFailurePending.load(std::memory_order_relaxed)&&!sensorRetirementPreparationFailurePending.load(std::memory_order_relaxed)&&!msiFeedbackPreparationFailurePending.load(std::memory_order_relaxed)&&!residentRecordPreparationFailurePending.load(std::memory_order_relaxed)&&!residentPathPreparationFailurePending.load(std::memory_order_relaxed))return;
            const auto current=eventRecoveryEpoch.load(std::memory_order_relaxed);
            if(current!=~0ULL)eventRecoveryEpoch.store(current+1,std::memory_order_release);
            eventFailurePending.store(false,std::memory_order_release);
            screenPreparationFailurePending.store(false,std::memory_order_release);
            sensorRetirementPreparationFailurePending.store(false,std::memory_order_release);
            msiFeedbackPreparationFailurePending.store(false,std::memory_order_release);
            residentRecordPreparationFailurePending.store(false,std::memory_order_release);
            residentPathPreparationFailurePending.store(false,std::memory_order_release);
        }catch(...){}
    }

    void record(Kind kind,const char* event,PreparationSource source=PreparationSource::none,PreselectionStage stage=PreselectionStage::none) noexcept {
        try {
            if(kind>=Capacity)return;
            std::lock_guard<std::mutex> lock(mutex);
            const bool residentPreparation=source==PreparationSource::none||source==PreparationSource::residentRecord||source==PreparationSource::residentSerialization;
            const auto preparationIndex=static_cast<size_t>(source);
            if(kind==preparationException&&preparationIndex>0&&preparationIndex<=recordPreparationSources.size()){
                auto& count=recordPreparationSources[preparationIndex-1];if(count!=~0ULL)++count;
                if(residentPreparation)residentRecordPreparationFailurePending.store(true,std::memory_order_release);
            }
            if(kind==preparationException&&(source==PreparationSource::residentPath||source==PreparationSource::detailPath)){
                auto& count=pathPreparationSources[source==PreparationSource::residentPath?0:1];if(count!=~0ULL)++count;
                if(source==PreparationSource::residentPath)residentPathPreparationFailurePending.store(true,std::memory_order_release);
            }
            if(kind==eventOpen||kind==eventWrite||kind==eventFlush||kind==eventUnavailable||kind==eventException)
                eventFailurePending.store(true,std::memory_order_release);
            if(kind==preparationException&&residentPreparation&&event&&std::string_view(event)=="screen-touchpads-state")
                screenPreparationFailurePending.store(true,std::memory_order_release);
            if(kind==preparationException&&residentPreparation&&event&&(std::string_view(event)=="gyro-power-unsubscribe-failed"||
                std::string_view(event)=="gyro-power-interval-reset-failed"))
                sensorRetirementPreparationFailurePending.store(true,std::memory_order_release);
            if(kind==preparationException&&residentPreparation&&event&&std::string_view(event)=="msi-dinput-feedback")
                msiFeedbackPreparationFailurePending.store(true,std::memory_order_release);
            const auto stageIndex=static_cast<size_t>(stage);
            if(kind==preparationException&&stageIndex>0&&stageIndex<=preselectionStages.size()){
                auto& observed=preselectionStages[stageIndex-1];
                if(observed.count!=~0ULL)++observed.count;
                if(observed.count==1&&event){
                    observed.firstKnown=true;
                    for(;observed.length<observed.firstEvent.size()-1&&event[observed.length];++observed.length){
                        const auto byte=static_cast<unsigned char>(event[observed.length]);const bool ascii=byte>=32&&byte<=126;
                        observed.firstEvent[observed.length]=ascii?static_cast<char>(byte):'?';observed.sanitized=observed.sanitized||!ascii;
                    }
                    observed.truncated=event[observed.length]!='\0';
                }
            }
            auto& bucket=buckets[kind];
            if(bucket.count!=~0ULL)++bucket.count;
            if(bucket.count!=1||!event)return;
            bucket.firstKnown=true;
            for(;bucket.length<bucket.firstEvent.size()-1&&event[bucket.length];++bucket.length){
                const auto byte=static_cast<unsigned char>(event[bucket.length]);
                const bool ascii=byte>=32&&byte<=126;
                bucket.firstEvent[bucket.length]=ascii?static_cast<char>(byte):'?';
                bucket.sanitized=bucket.sanitized||!ascii;
            }
            bucket.truncated=event[bucket.length]!='\0';
        } catch (...) {} // Failure accounting cannot replace the original outcome.
    }
    enum BoundaryKind : size_t { closeFailure,closePriorBadState,flushWrapperException,BoundaryCapacity };
    std::array<unsigned long long,BoundaryCapacity> boundary{};
    void recordBoundary(BoundaryKind kind) noexcept {
        try{
            if(kind>=BoundaryCapacity)return;
            std::lock_guard<std::mutex> lock(mutex);
            if(boundary[kind]!=~0ULL)++boundary[kind];
        }catch(...){}
    }

    SnapshotJson snapshot() const {
        static constexpr const char* lanes[]={"unassigned","event","detail","event","detail","event","detail","event","detail","event","detail"};
        static constexpr const char* stages[]={"record-wrapper-exception","open","open","write","write","flush","flush","unusable-open-stream","unusable-open-stream","sink-exception","sink-exception"};
        std::lock_guard<std::mutex> lock(mutex);
        auto rows=SnapshotJson::array();
        for(size_t i=0;i<Capacity;++i){
            const auto& bucket=buckets[i];
            rows.push_back({{"lane",lanes[i]},{"stage",stages[i]},{"observedFailures",bucket.count},
                {"counterLimitReached",bucket.count==~0ULL},{"firstEventKnown",bucket.firstKnown},
                {"firstEvent",bucket.firstKnown?SnapshotJson(std::string(bucket.firstEvent.data(),bucket.length)):SnapshotJson(nullptr)},
                {"firstEventTruncated",bucket.truncated},{"firstEventAsciiSanitized",bucket.sanitized}});
        }
        auto preselectionRows=SnapshotJson::array();
        static constexpr const char* preselectionNames[]={"steam-log-projection","native-log-classification"};
        for(size_t i=0;i<preselectionStages.size();++i){
            const auto& observed=preselectionStages[i];
            preselectionRows.push_back({{"stage",preselectionNames[i]},{"observedFailures",observed.count},
                {"counterLimitReached",observed.count==~0ULL},{"firstEventKnown",observed.firstKnown},
                {"firstEvent",observed.firstKnown?SnapshotJson(std::string(observed.firstEvent.data(),observed.length)):SnapshotJson(nullptr)},
                {"firstEventTruncated",observed.truncated},{"firstEventAsciiSanitized",observed.sanitized}});
        }
        return {{"schemaVersion",1},{"scope","current-Native-original-file-exit-failure-observations"},
            {"flushCloseBoundary",{{"schemaVersion",1},{"knownCloseFailures",boundary[closeFailure]},
                {"closePriorBadStateObservations",boundary[closePriorBadState]},{"unassignedWrapperExceptions",boundary[flushWrapperException]},
                {"scope","original-flush-wrapper;close-result-unknown-after-prior-bad-state;outside-Native-file-exceptions-unassigned"}}},
            {"eventAppendRecovery",{{"schemaVersion",7},{"epoch",eventRecoveryEpoch.load(std::memory_order_relaxed)},
                {"unrecoveredFailureObserved",eventFailurePending.load(std::memory_order_relaxed)},
                {"unrecoveredPreparationFailureObserved",screenPreparationFailurePending.load(std::memory_order_relaxed)},
                {"unrecoveredSensorRetirementPreparationFailureObserved",sensorRetirementPreparationFailurePending.load(std::memory_order_relaxed)},
                {"unrecoveredMsiFeedbackPreparationFailureObserved",msiFeedbackPreparationFailurePending.load(std::memory_order_relaxed)},
                {"unrecoveredResidentRecordPreparationFailureObserved",residentRecordPreparationFailurePending.load(std::memory_order_relaxed)},
                {"recordPreparationSources",{{"schemaVersion",1},{"residentRecord",recordPreparationSources[0]},{"detailRecord",recordPreparationSources[1]},
                    {"residentSerialization",recordPreparationSources[2]},{"detailSerialization",recordPreparationSources[3]},
                    {"counterLimit",~0ULL},{"countsAreUniqueRows",false},{"fileFailureProven",false}}},
                {"unrecoveredResidentPathPreparationFailureObserved",residentPathPreparationFailurePending.load(std::memory_order_relaxed)},
                {"pathPreparationSources",{{"schemaVersion",1},{"residentPath",pathPreparationSources[0]},{"detailPath",pathPreparationSources[1]},
                    {"counterLimit",~0ULL},{"countsAreUniqueRows",false},{"fileFailureProven",false}}},
                {"epochLimitReached",eventRecoveryEpoch.load(std::memory_order_relaxed)==~0ULL},
                {"scope","native-selected-record-serialization-and-path-preparation-with-classifier-snapshot-diagnostic-screen-sensor-msi-rearm-after-original-good-event-append;not-file-or-delivery-proof"},
                {"oldLostRowRestored",false},{"currentFileHealthProven",false}}},
            {"preselectionExceptionStages",{{"schemaVersion",1},{"scope","current-Native-preselection-projection-and-classifier-catches;not-lane-file-or-delivery-proof"},
                {"stages",std::move(preselectionRows)},{"maxStages",2},{"maxEventPrefixBytes",95},{"counterLimit",~0ULL},
                {"countsAreUniqueRows",false},{"countsSummedWithPreparationBucket",false},{"fileFailureProven",false}}},
            {"counterLimit",~0ULL},{"buckets",std::move(rows)},{"maxBuckets",Capacity},{"maxEventPrefixBytes",95},
            {"snapshotAtomicWithLogFiles",false},{"successfulAttemptsCounted",false},{"osErrorCodeAvailable",false},
            {"failureCountsAreUniqueRows",false},{"zeroProvesCompleteHistory",false},{"deduplicationIsDeliveryProof",false},
            {"scopeExcludes","domain mirrors,deferred queue delivery,external processes,process termination and unentered calls"}};
    }
};
inline NativeLogFailureEvidence& nativeLogFailureEvidence(){static NativeLogFailureEvidence value;return value;}
// Failure-only observations for the original Decky sidecar. This does not
// participate in Native event recovery or certify delivery/history/health.
struct DeckyLogFailureEvidence {
    enum Kind : size_t { preparationException,openFailure,openException,writeFailure,writeException,flushFailure,flushException,Capacity };
    struct Bucket {
        unsigned long long count=0;
        std::array<char,96> firstEvent{};
        size_t length=0;
        bool known=false,truncated=false,sanitized=false;
    };
    mutable std::mutex mutex;
    std::array<Bucket,Capacity> buckets{};
    void record(Kind kind,const char* event) noexcept {
        try {
            if(kind>=Capacity)return;
            std::lock_guard<std::mutex> lock(mutex);
            auto& bucket=buckets[kind];
            if(bucket.count!=~0ULL)++bucket.count;
            if(bucket.count!=1||!event)return;
            bucket.known=true;
            for(;bucket.length<bucket.firstEvent.size()-1&&event[bucket.length];++bucket.length){
                const auto byte=static_cast<unsigned char>(event[bucket.length]);
                const bool ascii=byte>=32&&byte<=126;
                bucket.firstEvent[bucket.length]=ascii?static_cast<char>(byte):'?';
                bucket.sanitized=bucket.sanitized||!ascii;
            }
            bucket.truncated=event[bucket.length]!='\0';
        }catch(...){} // Accounting cannot replace the original logging result.
    }
    SnapshotJson snapshot() const {
        static constexpr const char* stages[]={"preparation-exception","open-or-boundary-failure","open-or-boundary-exception",
            "write-failure","write-exception","flush-failure","flush-exception"};
        std::lock_guard<std::mutex> lock(mutex);
        auto rows=SnapshotJson::array();
        for(size_t i=0;i<Capacity;++i){
            const auto& bucket=buckets[i];
            rows.push_back({{"stage",stages[i]},{"observedFailures",bucket.count},{"counterLimitReached",bucket.count==~0ULL},
                {"firstEventKnown",bucket.known},{"firstEvent",bucket.known?SnapshotJson(std::string(bucket.firstEvent.data(),bucket.length)):SnapshotJson(nullptr)},
                {"firstEventTruncated",bucket.truncated},{"firstEventAsciiSanitized",bucket.sanitized}});
        }
        return {{"schemaVersion",1},{"scope","current-Native-Decky-sidecar-failure-observations"},
            {"buckets",std::move(rows)},{"maxBuckets",Capacity},{"maxEventPrefixBytes",95},{"counterLimit",~0ULL},
            {"snapshotAtomicWithLogFiles",false},{"successfulAttemptsCounted",false},{"osErrorCodeAvailable",false},
            {"failureCountsAreUniqueRows",false},{"zeroProvesCompleteHistory",false},{"currentFileHealthProven",false},
            {"scopeExcludes","destructor-close,rotation-internals,Native-detail-mirror,other-processes,termination,unentered-calls"}};
    }
};
inline DeckyLogFailureEvidence& deckyLogFailureEvidence(){static DeckyLogFailureEvidence value;return value;}

// The existing bounded failure recorder is reused by a separate Fan instance.
// No Fan failure can advance Native recovery or alter Decky observations.
struct FanLogFailureEvidence : DeckyLogFailureEvidence {
    SnapshotJson snapshot() const {
        auto value=DeckyLogFailureEvidence::snapshot();
        value["scope"]="current-Native-Fan-lifecycle-failure-observations";
        value["scopeExcludes"]="by-value-argument-preparation,destructor-close,rotation-internals,Fan-detail,other-processes,termination,unentered-calls";
        return value;
    }
};
inline FanLogFailureEvidence& fanLifecycleLogFailureEvidence(){static FanLogFailureEvidence value;return value;}

// Detailed fan-api writer only;original destructor flushing is not instrumented.
struct FanDetailLogFailureEvidence : DeckyLogFailureEvidence {
    SnapshotJson snapshot() const {
        auto value=DeckyLogFailureEvidence::snapshot();
        value["scope"]="current-Native-Fan-api-detail-failure-observations";
        value["explicitFlushPerformed"]=false;
        value["openBoundaryRepairPerformed"]=false;
        value["scopeExcludes"]="disabled-or-empty-path,arguments-before-entry,destructor-flush-close,rotation-internals,coordinator-rewrite,FanHost,other-processes,termination,unentered-calls";
        return value;
    }
};
inline FanDetailLogFailureEvidence& fanDetailLogFailureEvidence(){static FanDetailLogFailureEvidence value;return value;}

// Same fixed recorder layout;these names belong to the coordinator read/rewrite.
struct CoordinatorLogFailureEvidence : DeckyLogFailureEvidence {
    enum class Stage : size_t { preparationException,sourceOpenOrPresenceFailure,sourceReadFailure,
        rewriteOpenFailure,rewriteWriteFailure,sourceReadException,rewriteException };
    void record(Stage stage,const char* event) noexcept {
        DeckyLogFailureEvidence::record(static_cast<DeckyLogFailureEvidence::Kind>(stage),event);
    }
    SnapshotJson snapshot() const {
        auto value=DeckyLogFailureEvidence::snapshot();
        static constexpr const char* names[]={"preparation-exception","source-open-or-presence-failure","source-read-failure",
            "rewrite-open-failure","rewrite-write-failure","source-read-exception","rewrite-open-or-write-exception"};
        for(size_t i=0;i<7;++i)value["buckets"][i]["stage"]=names[i];
        value["scope"]="current-Native-Coordinator-read-rewrite-failure-observations";
        value["existingSourceReadRequiredBeforeRewrite"]=true;
        value["newFileRequiresObservedAbsence"]=true;
        value["explicitFlushPerformed"]=false;
        value["readSnapshotAtomicWithRewrite"]=false;
        value["rewriteBackend"]="same-directory-staged-transient";
        value["scopeExcludes"]="disabled-or-unselected,arguments-before-entry,destructor-flush-close,rotation-internals,after-check-races,other-processes,termination,unentered-calls";
        return value;
    }
};
inline CoordinatorLogFailureEvidence& coordinatorLogFailureEvidence(){static CoordinatorLogFailureEvidence value;return value;}

// Separate commit marginals;never add them to the source/legacy summary counts.
struct CoordinatorRewriteCommitEvidence : DeckyLogFailureEvidence {
    enum class Stage : size_t { temporaryCreateFailure,temporaryWriteFailure,temporaryCloseFailure,
        promotionFailure,cleanupFailure,newRowOverBudget,commitException };
    void record(Stage stage,const char* event) noexcept {
        DeckyLogFailureEvidence::record(static_cast<DeckyLogFailureEvidence::Kind>(stage),event);
    }
    SnapshotJson snapshot() const {
        auto value=DeckyLogFailureEvidence::snapshot();
        static constexpr const char* names[]={"temporary-create-failure","temporary-write-failure","temporary-close-failure",
            "promotion-failure","cleanup-failure","new-row-over-budget","commit-exception"};
        for(size_t i=0;i<7;++i)value["buckets"][i]["stage"]=names[i];
        value["scope"]="current-Native-Coordinator-staged-commit-failure-observations";
        value["temporaryCreateDisposition"]="CREATE_NEW";
        value["promotionFlags"]="MOVEFILE_REPLACE_EXISTING";
        value["explicitFlushPerformed"]=false;
        value["maxCommitAttemptsPerCall"]=1;
        value["sourceAndCommitAtomic"]=false;
        value["maxOutstandingTemporaryPerWriter"]=1;
        value["scopeExcludes"]="rotation-internals,concurrent-source-writers,after-check-races,crash-durability,other-processes,termination,unentered-calls";
        return value;
    }
};
inline CoordinatorRewriteCommitEvidence& coordinatorRewriteCommitEvidence(){static CoordinatorRewriteCommitEvidence value;return value;}




// Four original sensor-API failure sites. This is log selection memory only;
// a successful API return rearms its own witness and proves no physical return.
inline size_t sensorRetirementLogSlot(std::string_view event,std::string_view sensor) noexcept {
    const size_t family=event=="gyro-power-unsubscribe-failed"?0:
        event=="gyro-power-interval-reset-failed"?2:4;
    if(family==4)return 4;
    return sensor=="gyro"?family:sensor=="accel"?family+1:4;
}
// Old sensor-only rows keep their original key. Malformed/future error shapes
// stay resident; they cannot masquerade as a known repeated API exception.
struct SensorRetirementErrorKey {
    unsigned kind=0;
    unsigned long code=0;
    static bool read(const SnapshotJson& detail,SensorRetirementErrorKey& out) {
        out={};
        if(detail.size()==1&&!detail.contains("failure"))return true;
        const auto failure=detail.find("failure");
        if(detail.size()!=2||failure==detail.end()||!failure->is_object()||failure->size()!=5)return false;
        const auto schema=failure->find("schema"),kind=failure->find("kind"),known=failure->find("codeKnown"),
            domain=failure->find("codeDomain"),code=failure->find("code");
        if(schema==failure->end()||!schema->is_number_integer()||*schema!=1||kind==failure->end()||!kind->is_string()||
            known==failure->end()||!known->is_boolean()||domain==failure->end()||code==failure->end())return false;
        const auto& name=kind->get_ref<const std::string&>();
        if(name=="winrt-hresult") {
            if(!known->get<bool>()||!domain->is_string()||domain->get_ref<const std::string&>()!="HRESULT-from-caught-WinRT-exception")return false;
            unsigned long long value=0;
            if(code->is_number_unsigned())value=code->get<unsigned long long>();
            else if(code->is_number_integer()&&code->get<long long>()>=0)value=static_cast<unsigned long long>(code->get<long long>());
            else return false;
            if(value>0xffffffffULL)return false;
            out.kind=1;out.code=static_cast<unsigned long>(value);return true;
        }
        if(known->get<bool>()||!domain->is_null()||!code->is_null())return false;
        out.kind=name=="allocation-exception"?2:name=="standard-exception"?3:name=="unknown-exception"?4:0;
        return out.kind!=0;
    }
};

struct SensorRetirementLogState {
    struct Slot{unsigned long long power=0;unsigned kind=0;unsigned long code=0;bool known=false;};
    std::mutex mutex;
    std::array<Slot,4> slots{};
    unsigned long long deliveryEpoch=0;
    bool retain(size_t slot,unsigned long long power,unsigned kind=0,unsigned long code=0) noexcept {
        if(slot>=slots.size())return true;
        try{
            std::lock_guard<std::mutex> lock(mutex);
            const auto epoch=nativeLogFailureEvidence().recoveryEpoch();
            if(epoch!=deliveryEpoch){for(auto& value:slots)value.known=false;deliveryEpoch=epoch;}
            auto& value=slots[slot];
            if(value.known&&value.power==power&&value.kind==kind&&value.code==code)return false;
            value.power=power;value.kind=kind;value.code=code;value.known=true;return true;
        }catch(...){return true;}
    }
    // Read only: the existing Native classifier still owns first selection.
    bool repeatKnown(size_t slot,unsigned long long power,unsigned kind=0,unsigned long code=0) noexcept {
        if(slot>=slots.size())return false;
        try{
            std::lock_guard<std::mutex> lock(mutex);
            if(nativeLogFailureEvidence().recoveryEpoch()!=deliveryEpoch)return false;
            const auto& value=slots[slot];
            return value.known&&value.power==power&&value.kind==kind&&value.code==code;
        }catch(...){return false;}
    }

    void succeeded(std::string_view event,std::string_view sensor) noexcept {
        const auto slot=sensorRetirementLogSlot(event,sensor);
        if(slot>=slots.size())return;
        try{std::lock_guard<std::mutex> lock(mutex);slots[slot].known=false;}catch(...){}
    }
    void unknown(std::string_view event) noexcept {
        const auto first=sensorRetirementLogSlot(event,"gyro");
        if(first>=slots.size())return;
        try{std::lock_guard<std::mutex> lock(mutex);slots[first].known=false;slots[first+1].known=false;}catch(...){}
    }
};
inline SensorRetirementLogState& sensorRetirementLogState(){static SensorRetirementLogState value;return value;}


// Failure/refusal observations for the existing touch-summary producer only.
// No live plain Summary fields are read outside its original serial owner.
struct TouchHapticSummaryPreparationEvidence {
    enum Kind:size_t {submitRefused,submitException,materializationOrOfferException,attemptEvidenceOmission,queueClearPendingObserved,Capacity};
    std::array<std::atomic<unsigned long long>,Capacity> observations{};
    void record(Kind kind) noexcept {
        if(kind>=Capacity)return;auto& value=observations[kind];auto current=value.load(std::memory_order_relaxed);
        while(current!=~0ULL&&!value.compare_exchange_weak(current,current+1,std::memory_order_release,std::memory_order_relaxed)){}
    }
    SnapshotJson snapshot() const {
        SnapshotJson out=SnapshotJson::object();out["schema"]=2;
        out["scope"]="current-Native-touch-summary-original-submission-materialization-and-queue-clear-observations";
        out["countsAreUniqueRows"]=false;out["countsAreLostRows"]=false;
        out["snapshotAtomicAcrossCounters"]=false;out["pendingPlainSummaryRead"]=false;
        out["nativeFileDeliveryProven"]=false;out["automaticRetryScheduled"]=false;out["counterLimit"]=~0ULL;
        out["observations"]=SnapshotJson::object();bool saturated=false;
        static constexpr const char* names[]={"submitRefused","submitException","materializationOrOfferException","attemptEvidenceOmission","queueClearPendingObserved"};
        for(size_t i=0;i<Capacity;++i){const auto count=observations[i].load(std::memory_order_acquire);out["observations"][names[i]]=count;saturated|=count==~0ULL;}
        out["counterSaturated"]=saturated;return out;
    }
};
inline TouchHapticSummaryPreparationEvidence& touchHapticSummaryPreparationEvidence(){static TouchHapticSummaryPreparationEvidence value;return value;}

// Only completely described successful per-gesture telemetry may repeat quietly.
// Failures,drops and unfamiliar evidence remain resident and rearm the next success.
struct TouchHapticSuccessLogKey {
    std::array<unsigned long long,4> settings{};
    double distanceStep=0;
    unsigned backendMask=0,strengthMask=0;
    bool operator==(const TouchHapticSuccessLogKey&)const noexcept=default;
    static bool read(const SnapshotJson& row,TouchHapticSuccessLogKey& key) noexcept {
        if(!row.is_object()||row.size()!=17)return false;
        const auto number=[](const SnapshotJson& object,const char* name,unsigned long long& out) noexcept {
            const auto v=object.find(name);if(v==object.end())return false;
            if(v->is_number_unsigned()){out=v->get<unsigned long long>();return true;}
            if(v->is_number_integer()){const auto n=v->get<long long>();if(n>=0){out=static_cast<unsigned long long>(n);return true;}}
            return false;
        };
        std::array<unsigned long long,10> counts{};
        static constexpr const char* names[]={"sent","dropped","failed","xinput","msi","minimal","medium","strong","outputBaselineAttempts","outputBaselineAccepted"};
        for(size_t i=0;i<counts.size();++i)if(!number(row,names[i],counts[i]))return false;
        const auto sent=counts[0];if(!sent||counts[1]||counts[2]||counts[3]>sent||counts[4]!=sent-counts[3]||
            counts[5]>sent||counts[6]>sent-counts[5]||counts[7]!=sent-counts[5]-counts[6]||
            counts[8]>sent||counts[9]!=counts[8])return false;
        const auto flag=[](const SnapshotJson& object,const char* name) noexcept {
            const auto value=object.find(name);return value!=object.end()&&value->is_boolean()&&!value->get<bool>();
        };
        if(!flag(row,"hardwareFeelVerified"))return false;
        for(const char* name:{"reasons","errors"}){const auto v=row.find(name);if(v==row.end()||!v->is_object()||!v->empty())return false;}
        const auto attempt=row.find("failureAttemptEvidence");if(attempt==row.end()||!attempt->is_object()||attempt->size()!=7)return false;
        unsigned long long schema=0,n=0;
        if(!number(*attempt,"schema",schema)||schema!=1||!flag(*attempt,"outputApiFailureProven")||!flag(*attempt,"physicalFailureProven"))return false;
        for(const char* name:{"attempted","notAttempted","unknown"})if(!number(*attempt,name,n)||n!=0)return false;
        const auto scope=attempt->find("scope");if(scope==attempt->end()||!scope->is_string()||
            scope->get_ref<const std::string&>()!="unsuccessful-receipts-original-output-attempt-flag;excludes-dropped")return false;
        const auto strengths=row.find("strengthPercent");if(strengths==row.end()||!strengths->is_object()||strengths->size()!=3)return false;
        if(!number(row,"durationMs",key.settings[0])||!key.settings[0]||key.settings[0]>60000)return false;
        size_t i=1;for(const char* name:{"minimal","medium","strong"})if(!number(*strengths,name,key.settings[i])||!key.settings[i]||key.settings[i++]>100)return false;
        const auto distance=row.find("distanceStep");if(distance==row.end()||!distance->is_number())return false;
        key.distanceStep=distance->get<double>();if(!std::isfinite(key.distanceStep)||key.distanceStep<=0||key.distanceStep>1)return false;
        key.backendMask=(counts[3]?1u:0u)|(counts[4]?2u:0u);key.strengthMask=(counts[5]?1u:0u)|(counts[6]?2u:0u)|(counts[7]?4u:0u);
        return true;
    }
};
struct TouchHapticSuccessLogState {
    TouchHapticSuccessLogKey key{};unsigned long long generation=0,deliveryEpoch=0;bool known=false;
    bool repeated(const SnapshotJson& row,unsigned long long power,unsigned long long epoch,bool pending) noexcept {
        TouchHapticSuccessLogKey next;
        if(!TouchHapticSuccessLogKey::read(row,next)){known=false;return false;}
        if(pending)known=false;
        if(known&&generation==power&&deliveryEpoch==epoch&&key==next)return true;
        key=next;generation=power;deliveryEpoch=epoch;known=true;return false;
    }
};

enum class SnapshotKind { full, oemWake, hud, screen, sleepInspection, throttle, queue, revision, settle, dequeue };
// Existing public screen profiles only. Unknown names keep the legacy memo.
inline constexpr std::array<std::string_view,4> screenObservationPersonas={"disabled","steamdeck","dualsense-edge","elite"};
inline std::size_t screenObservationSlot(std::string_view persona) noexcept {
    for(std::size_t i=0;i<screenObservationPersonas.size();++i)if(persona==screenObservationPersonas[i])return i;
    return screenObservationPersonas.size();
}

// Exact structured comparison, no hashes or serialized copies on unchanged
// observations. Ignore only documented counters at their specified object level.
inline bool snapshotCounter(std::string_view key, SnapshotKind kind) noexcept {
    if (kind == SnapshotKind::oemWake) return key == "requestedEpoch" || key == "durationMs";
    if (kind == SnapshotKind::hud) return key == "seq";
    if (kind == SnapshotKind::revision) return key == "revision";
    if (kind == SnapshotKind::throttle) return key == "waitMs";
    if (kind == SnapshotKind::queue) return key == "enqueueCount" || key == "sinceRequestMs" || key == "queueDepth";
    if (kind != SnapshotKind::screen) return false;
    for (const auto name : {"events", "injectedInputs", "repaints", "cursorFallbackShows",
        "cursorFallbackHides", "cursorFallbackHandoffs", "cursorFallbackSyncs", "cursorFallbackUploads",
        "standaloneSpecialRequests", "standaloneSpecialCompleted"}) if (key == name) return true;
    return false;
}
inline bool snapshotValueEqual(const SnapshotJson& a, const SnapshotJson& b) {
    // Keep type changes visible (JSON's ordinary numeric == accepts 1 == 1.0).
    if (a.type() != b.type()) return false;
    if (a.is_primitive()) {
        if (a.is_number_float() && a == b)
            return std::signbit(a.get<double>()) == std::signbit(b.get<double>());
        return a == b;
    }
    if (a.size() != b.size()) return false;
    auto ai = a.begin(), bi = b.begin();
    for (; ai != a.end(); ++ai, ++bi) {
        if (a.is_object() && ai.key() != bi.key()) return false;
        if (!snapshotValueEqual(ai.value(), bi.value())) return false;
    }
    return true;
}
inline void compactControllerBoundaryWindows(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 'c' || std::string_view(event) != "controller-isolation-boundary") return;
    const auto schema = detail.find("schema");
    if (schema == detail.end() || !schema->is_number_integer() || *schema != 1) return;
    const auto presentation = detail.find("presentation");
    if (presentation == detail.end() || !presentation->is_object() || presentation->contains("windowCopiesV1")) return;
    const auto main = presentation->find("main");
    if (main == presentation->end() || !main->is_object() || !main->contains("hwnd") || !main->contains("valid")) return;
    const auto display = presentation->find("display"), foreground = presentation->find("foreground");
    const unsigned mask = (display != presentation->end() && snapshotValueEqual(*main, *display) ? 1u : 0u) |
                          (foreground != presentation->end() && snapshotValueEqual(*main, *foreground) ? 2u : 0u);
    if (!mask) return;
    // The marker shares bytes within one receipt; the windows were still observed separately.
    try { presentation->emplace("windowCopiesV1", mask); }
    catch (const std::bad_alloc&) { return; }
    if (mask & 1u) presentation->erase(display);
    if (mask & 2u) presentation->erase(foreground);
}
inline bool hidHideCliReceiptAttemptId(std::string_view id) noexcept {
    return !id.empty() && id.size() <= 100 && id != "." && id != ".." &&
        id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_{}:.-") == std::string_view::npos;
}
inline void markInputHostSwitchWaitSample(const char* event, SnapshotJson& detail) {
    if (!event || (event[0] != 'i' && event[0] != 'l')) return;
    const std::string_view name(event);
    if (name == "input-host-switch-settle") {
        detail["switchWaitSample"] = true;
    } else if (name == "input-host-switch-audit") {
        const auto beat = detail.find("beat");
        if (beat == detail.end() || !beat->is_string()) return;
        const auto& phase = beat->get_ref<const std::string&>();
        if (phase == "throttled" || phase == "enqueue" || phase == "dequeue")
            detail["switchWaitSample"] = true;
    } else if (name == "input-host-player-start-work") {
        const auto action = detail.find("action");
        if (action != detail.end() && action->is_string() && *action == "consuming")
            detail["switchWaitSample"] = true;
    } else if (name == "late-hide-enqueue") {
        const auto outcome = detail.find("outcome"), coalesced = detail.find("coalesced");
        if (outcome != detail.end() && outcome->is_string() && *outcome == "coalesced" &&
            coalesced != detail.end() && coalesced->is_boolean() && coalesced->get<bool>())
            detail["switchWaitSample"] = true;
    }
}

inline void markScreenTouchpadReadRepeat(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 's' || std::string_view(event) != "screen-touchpads-state") return;
    const auto site = detail.find("callSite");
    if (site != detail.end() && site->is_string() && site->get_ref<const std::string&>() == "screenTouchpads.get")
        detail["screenReadRepeat"] = true;
}

inline void compactHidHideCliReceipt(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 'h' || std::string_view(event) != "hidhide-cli-writer" || !detail.is_object() ||
        detail.contains("writerReceiptDefaultsV1") || detail.contains("writerReceiptDefaultsV2")) return;
    const auto text=[&](const char* key)->std::string_view {
        const auto value=detail.find(key);
        return value!=detail.end() && value->is_string()?value->get_ref<const std::string&>():std::string_view{};
    };
    if (text("phase") != "settled" || text("scope") != "CLI writer only; no target/physical-return/game proof") return;
    const auto reason=text("reason");
    if (reason!="exact-owned-CLI-root-and-subtree-exited" && reason!="exact-native-child-and-job-writer-quiescent") return;
    const auto attempt=text("attemptId"), job=text("jobName");
    constexpr std::string_view prefix="Local\\YMCC.HidHide.Cli.";
    if (!hidHideCliReceiptAttemptId(attempt) || !job.starts_with(prefix) || job.substr(prefix.size()) != attempt) return;
    bool parentCopied = false;
    if (reason == "exact-owned-CLI-root-and-subtree-exited") {
        const auto writer = detail.find("pid"), parent = detail.find("parentPid");
        const auto birth = text("writerProcessCreation100ns");
        if (writer != detail.end() && writer->is_number_integer() && parent != detail.end() &&
            parent->is_number_integer() && *writer == *parent &&
            writer->get<unsigned long long>() > 0 && writer->get<unsigned long long>() <= 0xffffffffULL &&
            !birth.empty() && birth.size() <= 20 && birth.front() != '0' && birth == text("parentBirth")) {
            unsigned long long value = 0;
            const auto end = birth.data() + birth.size();
            const auto parsed = std::from_chars(birth.data(), end, value);
            parentCopied = parsed.ec == std::errc{} && parsed.ptr == end;
        }
    }
    try { detail.emplace(parentCopied ? "writerReceiptDefaultsV2" : "writerReceiptDefaultsV1", parentCopied ? 2 : 1); }
    catch (const std::bad_alloc&) { return; }
    detail.erase("scope");
    detail.erase("jobName");
    if (parentCopied) {
        detail.erase("parentPid");
        detail.erase("parentBirth");
    }
}
inline void compactGameOverlayFrameBounds(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 'g' || std::string_view(event) != "game-overlay-surface") return;
    const auto presentation = detail.find("presentation");
    if (presentation == detail.end() || !presentation->is_object() || presentation->contains("frameBoundsCopiesV1")) return;
    const auto isI32 = [](const SnapshotJson& value) {
        return value.is_number_unsigned() ? value.get<unsigned long long>() <= 2147483647ULL :
            value.is_number_integer() && value.get<long long>() >= -2147483648LL && value.get<long long>() <= 2147483647LL;
    };
    constexpr std::array<const char*, 3> windows{"main", "display", "foreground"};
    unsigned mask = 0;
    for (unsigned i = 0; i < windows.size(); ++i) {
        const auto win = presentation->find(windows[i]);
        if (win == presentation->end() || !win->is_object()) continue;
        const auto read = win->find("boundsRead"), hr = win->find("frameHresult");
        const auto bounds = win->find("bounds"), frame = win->find("frameBounds");
        if (read == win->end() || !read->is_boolean() || !read->get<bool>() ||
            hr == win->end() || !isI32(*hr) || hr->get<long long>() < 0 ||
            bounds == win->end() || !bounds->is_array() || bounds->size() != 4 || frame == win->end()) continue;
        bool rect = true;
        for (const auto& coordinate : *bounds) if (!isI32(coordinate)) rect = false;
        if (rect && snapshotValueEqual(*bounds, *frame)) mask |= 1u << i;
    }
    if (!mask) return;
    try { presentation->emplace("frameBoundsCopiesV1", mask); }
    catch (const std::bad_alloc&) { return; }
    for (unsigned i = 0; i < windows.size(); ++i) {
        if (mask & (1u << i)) presentation->find(windows[i])->erase("frameBounds");
    }
}
inline void compactGameOverlayObservationLimits(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 'g' || std::string_view(event) != "game-overlay-surface") return;
    const auto presentation = detail.find("presentation");
    constexpr const char* marker = "observationLimitsV1";
    if (presentation == detail.end() || !presentation->is_object() || presentation->contains(marker)) return;
    struct Target { const char* child; const char* field; };
    constexpr std::array<Target,6> targets{{
        {nullptr,"crossSourceAtomic"}, {nullptr,"screenVisibleVerified"}, {"main","screenVisibleVerified"},
        {"display","screenVisibleVerified"}, {"foreground","screenVisibleVerified"}, {"zOrder","pixelOcclusionProven"}
    }};
    std::array<SnapshotJson*,targets.size()> owners{};
    unsigned mask = 0;
    for (size_t i = 0; i < targets.size(); ++i) {
        const auto& [child, field] = targets[i];
        const auto owner = child ? presentation->find(child) : presentation;
        if ((child && owner == presentation->end()) || !owner->is_object()) continue;
        const auto value = owner->find(field);
        if (value == owner->end() || !value->is_boolean() || value->get<bool>()) continue;
        owners[i] = &*owner;
        mask |= 1u << i;
    }
    if (!mask) return;
    // Record the exact omissions before removing any explicit false values.
    try { presentation->emplace(marker, mask); }
    catch (const std::bad_alloc&) { return; }
    for (size_t i = 0; i < targets.size(); ++i) if (owners[i]) owners[i]->erase(targets[i].field);
}
inline void compactSteamLightLeaseReceipt(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 's' || std::string_view(event) != "summon-light-gamepad" || !detail.is_object()) return;
    constexpr auto marker = "lightLeaseReceiptV1";
    if (detail.contains(marker)) return;
    const auto ack = detail.find("acknowledged"), result = detail.find("result"), scope = detail.find("scope"),
        lease = detail.find("leaseMs"), renewal = detail.find("renewalMs"), visibility = detail.find("deviceVisibilityChanged");
    if (ack == detail.end() || !ack->is_boolean() || result == detail.end() ||
        *result != (ack->get<bool>() ? "acknowledged" : "unconfirmed") ||
        scope == detail.end() || *scope != "steam-gamepad-navigation" ||
        lease == detail.end() || !lease->is_number_integer() || *lease != 3000 ||
        renewal == detail.end() || !renewal->is_number_integer() || *renewal != 750 ||
        visibility == detail.end() || !visibility->is_boolean() || visibility->get<bool>()) return;
    try { detail.emplace(marker, 1); }
    catch (const std::bad_alloc&) { return; }
    detail.erase(result);
    detail.erase(scope);
    detail.erase(lease);
    detail.erase(renewal);
    detail.erase(visibility);
}
inline constexpr auto targetReturnRecordScope = "target-and-owned-physical-write-ahead;current-owner-settlement;R3-bounded-cold-return";
inline constexpr auto targetReturnRecordScopeMarker = "returnRecordScopeV1";
inline void compactTargetReturnRecordScope(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 'i' || std::string_view(event) != "input-host-target-return-record" || !detail.is_object() ||
        detail.contains(targetReturnRecordScopeMarker)) return;
    const auto scope = detail.find("scope"), history = detail.find("historyOnly"),
        admission = detail.find("currentAdmission"), game = detail.find("gameInputVerified");
    if (scope == detail.end() || *scope != targetReturnRecordScope ||
        history == detail.end() || !history->is_boolean() || !history->get<bool>() ||
        admission == detail.end() || !admission->is_boolean() || admission->get<bool>() ||
        game == detail.end() || !game->is_boolean() || game->get<bool>()) return;
    try { detail.emplace(targetReturnRecordScopeMarker, 1); }
    catch (const std::bad_alloc&) { return; }
    detail.erase(scope);
    detail.erase(history);
    detail.erase(admission);
    detail.erase(game);
}
inline void compactOemHidReadFlags(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 'o' || std::string_view(event) != "oem-hid-read-lifecycle" || !detail.is_object()) return;
    constexpr auto marker = "hidReadFlagsV1";
    if (detail.contains(marker)) return;
    const auto status = detail.find("status"), pending = detail.find("readPending"), terminal = detail.find("resourceTerminal");
    if (status == detail.end() || terminal == detail.end() || !terminal->is_boolean()) return;
    const bool drain = *status == "drain-complete", close = *status == "closed";
    const bool waiting = *status == "cancel-requested" || *status == "retained";
    if ((!drain && !close && !waiting) || terminal->get<bool>() != (drain || close)) return;
    if (drain ? pending != detail.end() :
        pending == detail.end() || !pending->is_boolean() || pending->get<bool>() != waiting) return;
    try { detail.emplace(marker, 1); }
    catch (const std::bad_alloc&) { return; }
    detail.erase(terminal);
    if (!drain) detail.erase(pending);
}
inline void compactLateHideStageDefaults(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 'l' || !detail.is_object()) return;
    const std::string_view name(event);
    const bool dequeue = name == "late-hide-dequeue";
    constexpr auto marker = "lateHideStageDefaultsV1";
    if ((!dequeue && name != "late-hide-exec-decision") || detail.contains(marker)) return;
    const auto reason = detail.find("reason"), owner = detail.find("owner");
    if (reason == detail.end() || *reason != "physical-source-returned-after-skip" ||
        (dequeue && (owner == detail.end() || *owner != "capture-lifecycle-thread"))) return;
    try { detail.emplace(marker, 1); }
    catch (const std::bad_alloc&) { return; }
    detail.erase(reason);
    if (dequeue) detail.erase(owner);
}
inline void compactGyroCalibrationStorage(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 'g' || std::string_view(event) != "gyro-calibration-result" || !detail.is_object()) return;
    constexpr auto marker = "gyroStorageDefaultsV1";
    if (detail.contains(marker)) return;
    const auto reason = detail.find("storageReason"), qualified = detail.find("sourceQualified"), saved = detail.find("saved");
    if (reason == detail.end() || *reason != "not-attempted" || qualified == detail.end() ||
        !qualified->is_boolean() || !qualified->get<bool>() || saved == detail.end() ||
        !saved->is_boolean() || saved->get<bool>()) return;
    const auto replaced = detail.find("storageReplaced"), verified = detail.find("storageReadbackVerified");
    const auto error = detail.find("storageError"), recovery = detail.find("recoveryPath");
    if (replaced == detail.end() || !replaced->is_boolean() || replaced->get<bool>() ||
        verified == detail.end() || !verified->is_boolean() || verified->get<bool>() ||
        error == detail.end() || !error->is_number_integer() || *error != 0 ||
        recovery == detail.end() || *recovery != "") return;
    try { detail.emplace(marker, 1); }
    catch (const std::bad_alloc&) { return; }
    detail.erase(replaced);
    detail.erase(verified);
    detail.erase(error);
    detail.erase(recovery);
}
inline void compactUiOutputScope(const char* event, SnapshotJson& detail) {
    if (!event || (event[0] != 'v' && event[0] != 'd') || !detail.is_object()) return;
    const std::string_view name(event);
    const auto expected = name == "virtual-ui-output" ? "final-virtual-output-only" :
                          name == "dsu-ui-output" ? "dsu-feed-only" : nullptr;
    constexpr auto marker = "uiOutputScopeV1";
    if (!expected || detail.contains(marker)) return;
    const auto scope = detail.find("verificationScope"), reoffer = detail.find("diagnosticRecoveryReoffer");
    if (scope == detail.end() || !scope->is_string() || scope->get_ref<const std::string&>() != expected ||
        reoffer == detail.end() || !reoffer->is_boolean() || *reoffer != false) return;
    try { detail.emplace(marker, 1); }
    catch (const std::bad_alloc&) { return; }
    detail.erase(scope);
    detail.erase(reoffer);
}
inline void compactConnectionTruthScope(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 'd' || std::string_view(event) != "domain-pad-truth" || !detail.is_object()) return;
    constexpr auto marker = "connectionTruthScopeV1";
    if (detail.contains(marker)) return;
    const auto scope = detail.find("observationScope"), reoffer = detail.find("diagnosticRecoveryReoffer");
    if (scope == detail.end() || *scope != "current-connection-truth;not-input-edge-or-device-recovery" ||
        reoffer == detail.end() || !reoffer->is_boolean()) return;
    try { detail.emplace(marker, 1); }
    catch (const std::bad_alloc&) { return; }
    detail.erase(scope);
}
inline void compactPhysicalIdentityScope(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 'p' || std::string_view(event) != "physical-controller-identity" || !detail.is_object()) return;
    constexpr auto marker = "physicalIdentityScopeV1";
    if (detail.contains(marker)) return;
    const auto reason = detail.find("snapshotReason"), source = detail.find("source"), scope = detail.find("observationScope");
    if (reason == detail.end() || (*reason != "state-change" && *reason != "repeat") ||
        source == detail.end() || *source != "raw-input-pnp-resolution" ||
        scope == detail.end() || *scope != "per-interface-identity;selected-interface-trajectory-in-detail") return;
    try { detail.emplace(marker, 1); }
    catch (const std::bad_alloc&) { return; }
    detail.erase(source);
    detail.erase(scope);
}
inline void compactRogDiscoveryScope(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 'r' || std::string_view(event) != "rog.hid-discovery-summary" || !detail.is_object()) return;
    constexpr auto marker = "rogDiscoveryScopeV1";
    if (detail.contains(marker)) return;
    const auto schema = detail.find("schema"), coverage = detail.find("interfaceInspectionCoverage"), absence = detail.find("physicalAbsenceProven");
    if (schema == detail.end() || !schema->is_number_integer() || *schema != 2 ||
        coverage == detail.end() || *coverage != "not-measured" || absence == detail.end() ||
        !absence->is_boolean() || absence->get<bool>()) return;
    try { detail.emplace(marker, 1); }
    catch (const std::bad_alloc&) { return; }
    detail.erase(coverage);
    detail.erase(absence);
}
inline void compactSummonPreflightDefaults(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 's' || std::string_view(event) != "summon-isolation-preflight" || !detail.is_object()) return;
    constexpr auto marker = "summonPreflightDefaultsV1";
    if (detail.contains(marker)) return;
    const auto authority = detail.find("authority"), phase = detail.find("phase"),
        foreground = detail.find("foregroundIsGameAuthority"), work = detail.find("deviceWorkIssued");
    if (authority == detail.end() || *authority != "unique-game-observation" ||
        phase == detail.end() || *phase != "before-window-show" ||
        foreground == detail.end() || !foreground->is_boolean() || foreground->get<bool>() ||
        work == detail.end() || !work->is_boolean() || work->get<bool>()) return;
    try { detail.emplace(marker, 1); }
    catch (const std::bad_alloc&) { return; }
    detail.erase(authority);
    detail.erase(phase);
    detail.erase(foreground);
    detail.erase(work);
}
inline void compactSummonSelectionDefaults(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 's' || std::string_view(event) != "summon-isolation-selected" || !detail.is_object()) return;
    constexpr auto marker = "summonSelectionDefaultsV1";
    if (detail.contains(marker)) return;
    const auto mode = detail.find("mode"), authority = detail.find("authority");
    const auto exemption = detail.find("nativeBorrowExemptionRequested"), policy = detail.find("physicalIsolationPolicy");
    if (mode == detail.end() || authority == detail.end() || *authority != "unique-game-observation" ||
        exemption == detail.end() || !exemption->is_boolean() || policy == detail.end()) return;
    const bool light = *mode == "light-no-game";
    if ((!light && *mode != "original") || *exemption != light ||
        *policy != (light ? "keep-device-steam-navigation-lease" : "original-owner")) return;
    try { detail.emplace(marker, 1); }
    catch (const std::bad_alloc&) { return; }
    detail.erase(authority);
    detail.erase(exemption);
    detail.erase(policy);
}
inline void compactScreenMenuOrigin(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 's' || std::string_view(event) != "screen-standalone-special" || !detail.is_object()) return;
    constexpr auto marker = "screenMenuOriginPowerV1";
    const auto phase = detail.find("phase"), origin = detail.find("menuActionOrigin");
    if (phase == detail.end() || *phase != "completed" || detail.contains(marker) ||
        origin == detail.end() || !origin->is_object() || origin->size() != 6) return;
    const auto schema = origin->find("schema"), channel = origin->find("channel"), source = origin->find("powerSource");
    const auto power = origin->find("powerGeneration");
    if (schema == origin->end() || !schema->is_number_integer() || *schema != 1 ||
        channel == origin->end() || *channel != "screen" ||
        source == origin->end() || *source != "native-before-action-queue" ||
        power == origin->end() || !power->is_string()) return;
    const auto& powerText = power->get_ref<const std::string&>();
    if (powerText.empty() || powerText.size() > 20 || (powerText.size() > 1 && powerText.front() == '0')) return;
    unsigned long long powerValue = 0;
    const auto powerEnd = powerText.data() + powerText.size();
    const auto parsed = std::from_chars(powerText.data(), powerEnd, powerValue);
    if (parsed.ec != std::errc{} || parsed.ptr != powerEnd) return;
    for (const auto key : {"generation", "requestId"}) {
        const auto value = detail.find(key), copy = origin->find(key);
        if (value == detail.end() || !value->is_number_integer() ||
            copy == origin->end() || !copy->is_string()) return;
        if (!value->is_number_unsigned() && value->get<long long>() <= 0) return;
        const auto number = value->get<unsigned long long>();
        if (!number) return;
        char decimal[20];
        const auto written = std::to_chars(decimal, decimal + sizeof(decimal), number);
        if (written.ec != std::errc{} || copy->get_ref<const std::string&>() !=
            std::string_view(decimal, static_cast<size_t>(written.ptr - decimal))) return;
    }
    // Keep the pre-queue power value independent of the later writer envelope.
    try { detail.emplace(marker, *power); }
    catch (const std::bad_alloc&) { return; }
    detail.erase(origin);
}
inline void compactXInputSlotResults(const char* event, SnapshotJson& detail) {
    if (!event || event[0] != 'x' || std::string_view(event) != "xinput-slot-devices") return;
    constexpr const char* compactKey = "slotResultsV1";
    const auto slots = detail.find("slots");
    if (slots == detail.end() || !slots->is_array() || slots->size() != 4 || detail.contains(compactKey)) return;
    constexpr unsigned notConnected = 1167; // ERROR_DEVICE_NOT_CONNECTED
    unsigned mask = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const auto& entry = (*slots)[i];
        if (!entry.is_object()) return;
        const auto index = entry.find("slot"), result = entry.find("result");
        if (index == entry.end() || !index->is_number_integer() || *index != i ||
            result == entry.end() || !result->is_number_integer() || *result < 0 || *result > 0xffffffffULL) return;
        if (entry.size() == 2 && *result == notConnected) mask |= 1u << i;
    }
    if (!mask) return;
    // Allocate the new key before changing the payload. SnapshotJson uses a
    // map, so insertion leaves the original array iterator valid.
    try {
        auto& packed = *detail.emplace(compactKey, nullptr).first;
        packed.swap(*slots);
        for (unsigned i = 0; i < 4; ++i) if (mask & (1u << i)) packed[i] = notConnected;
        detail.erase(slots);
    } catch (const std::bad_alloc&) { return; }
}
// Telemetry paths are exact, not recursive name wildcards. Unknown fields and
// failures remain semantic. Complete trajectories are retained in detail rows.
inline bool observationTelemetryKey(std::string_view path, std::string_view key, SnapshotKind kind) noexcept {
    if (kind == SnapshotKind::sleepInspection)
        return path == "nativePrimary.log" && (key == "copiedBytes" || key == "originalBytes");
    if (path.empty() && kind == SnapshotKind::settle) return key == "deferMs";
    if (path.empty() && kind == SnapshotKind::dequeue) return key == "waitedMs" || key == "deferCount";
    if (kind != SnapshotKind::screen) return false;
    if (path.empty()) {
        if (snapshotCounter(key, kind)) return true;
        for (const auto name : {"cursorFallbackQueries", "cursorDeckReportPulseTick", "cursorDeckDeferredMoves",
            "cursorDeckMovePostReportDeferrals", "contactGeometryHits", "contactGeometryInvalidations",
            "contactGeometryReads", "contactVisualMoves", "contactVisualUploads", "outlineUploads",
            "autoHideWakeups", "autoHideTransitions", "cursorDeckForcedShows", "cursorDeckForcedHiddenSamples"})
            if (key == name) return true;
    }
    if (path == "cursorTransitionTrace") return key == "entries" || key == "total";
    if (path == "cursorTransitionTrace.placements")
        return key == "unchanged" || key == "lastRaiseTick" || key == "deckRaises" || key == "setWindowPos";
    return path == "cursorVisibilityContext" && key == "gestures";
}
inline bool observationEqual(const SnapshotJson& a, const SnapshotJson& b, SnapshotKind kind, std::string_view path = {}) {
    if (!a.is_object() || !b.is_object()) return snapshotValueEqual(a, b);
    auto ai = a.begin(), bi = b.begin();
    const auto skip = [&](const auto& it, const SnapshotJson& peer) {
        if (kind == SnapshotKind::sleepInspection && path == "nativePrimary.log" && it.key() == "sourceRevision") {
            // The tracker increments this for every source read, even if the observed state is unchanged.
            const auto complete = [](const SnapshotJson& log) {
                const auto available = log.find("available"), truncated = log.find("truncated");
                const auto copied = log.find("copiedBytes"), original = log.find("originalBytes"), limit = log.find("limitBytes");
                return available != log.end() && *available == true && truncated != log.end() && *truncated == false &&
                    copied != log.end() && copied->is_number_unsigned() && original != log.end() && original->is_number_unsigned() &&
                    *copied == *original && limit != log.end() && limit->is_number_unsigned() &&
                    *limit > 0 && *copied <= *limit && !log.contains("reason") && !log.contains("win32Error");
            };
            const auto other = peer.find(it.key());
            return it.value().is_number_unsigned() && it.value() != 0 && other != peer.end() &&
                other->is_number_unsigned() && *other != 0 && complete(a) && complete(b);
        }
        if (!observationTelemetryKey(path, it.key(), kind)) return false;
        const auto other = peer.find(it.key());
        if (other == peer.end() || it.value().type() != other->type()) return false;
        if (path == "cursorTransitionTrace" && it.key() == "entries") return it.value().is_array();
        // A missing/wrong-type/unavailable counter is evidence, not noise.
        return it.value().is_number_unsigned() ||
            (it.value().is_number_integer() && it.value().template get<long long>() >= 0);
    };
    for (;;) {
        while (ai != a.end() && skip(ai, b)) ++ai;
        while (bi != b.end() && skip(bi, a)) ++bi;
        if (ai == a.end() || bi == b.end()) return ai == a.end() && bi == b.end();
        if (ai.key() != bi.key()) return false;
        // Recurse only through known containers: no per-record path allocation.
        std::string_view child;
        if (kind == SnapshotKind::screen) {
            if (path.empty() && ai.key() == "cursorTransitionTrace") child = "cursorTransitionTrace";
            else if (path == "cursorTransitionTrace" && ai.key() == "placements") child = "cursorTransitionTrace.placements";
            else if (path.empty() && ai.key() == "cursorVisibilityContext") child = "cursorVisibilityContext";
        } else if (kind == SnapshotKind::sleepInspection) {
            if (path.empty() && ai.key() == "nativePrimary") child = "nativePrimary";
            else if (path == "nativePrimary" && ai.key() == "log") child = "nativePrimary.log";
        }
        if (child.empty() ? !snapshotValueEqual(ai.value(), bi.value()) : !observationEqual(ai.value(), bi.value(), kind, child)) return false;
        ++ai; ++bi;
    }
}
inline bool snapshotEqual(const SnapshotJson& a, const SnapshotJson& b, SnapshotKind kind, bool root = true) {
    if (kind == SnapshotKind::sleepInspection || kind == SnapshotKind::settle || kind == SnapshotKind::dequeue) return observationEqual(a, b, kind);
    if (!root && kind == SnapshotKind::screen) return observationEqual(a, b, kind);
    if (!a.is_object() || !b.is_object()) return snapshotValueEqual(a, b);
    auto ai = a.begin(), bi = b.begin();
    const auto skipKind = root ? (kind == SnapshotKind::oemWake || kind == SnapshotKind::throttle || kind == SnapshotKind::queue || kind == SnapshotKind::revision ? kind : SnapshotKind::full) : kind;
    for (; ai != a.end() && bi != b.end(); ++ai, ++bi) {
        if (ai.key() != bi.key()) return false;
        // Objects are ordered: matching iterator keys already prove both
        // fields exist. No peer lookup or second counter-name pass is needed.
        if (snapshotCounter(ai.key(), skipKind) && ai.value().type() == bi.value().type() &&
            (ai.value().is_number_unsigned() ||
             (ai.value().is_number_integer() && ai.value().template get<long long>() >= 0 &&
              bi.value().template get<long long>() >= 0))) continue;
        const bool nested = root && ((kind == SnapshotKind::hud && ai.key() == "hud") ||
                                     (kind == SnapshotKind::screen && ai.key() == "state"));
        if (nested ? !snapshotEqual(ai.value(), bi.value(), kind, false) : !snapshotValueEqual(ai.value(), bi.value())) return false;
    }
    return ai == a.end() && bi == b.end();
}
// IPC failures are observed only after an existing delivery failed. Keep first
// exact failure types for this Native lifetime; request/child IDs are witnesses,
// not type identities. Full repeated payloads still belong to the detail lane.
struct IpcFailureLogState {
    static constexpr size_t Capacity=64;
    struct Decision { bool selected=false;const char* reason="not-ipc-failure";size_t index=Capacity; };
    struct Entry { int event=-1;SnapshotJson value;unsigned long long witnessEpoch=0; };
    std::array<Entry,Capacity> entries{};
    std::array<bool,3> capacityWitness{},unclassifiedWitness{};
    std::array<unsigned long long,3> capacityWitnessEpoch{},unclassifiedWitnessEpoch{};
    unsigned long long reoffered=0,lastObservedRecoveryEpoch=0;
    size_t types=0;
    unsigned long long offered=0,repeated=0,untrackedRows=0,unclassifiedRows=0;
    static void increment(unsigned long long& value) noexcept {if(value!=~0ULL)++value;}
    static int eventIndex(const char* event) noexcept {
        if(!event||std::string_view(event).substr(0,13)!="ipc-response-")return -1;
        const std::string_view name(event);
        if(name=="ipc-response-delivery-failed")return 0;
        if(name=="ipc-response-delivery-exception")return 1;
        return name=="ipc-response-serialization-failed"?2:-1;
    }
    static bool integer(const SnapshotJson& value) noexcept {return value.is_number_integer();}
    static bool validUtf8(std::string_view text) noexcept {
        // The memo is exported later. Never retain a string that can poison
        // that independent JSON serialization; bounded at the caller (128B).
        for(size_t i=0;i<text.size();){
            const auto first=static_cast<unsigned char>(text[i++]);
            if(first<0x80)continue;
            unsigned continuation=0;unsigned char low=0x80,high=0xbf;
            if(first>=0xc2&&first<=0xdf)continuation=1;
            else if(first>=0xe0&&first<=0xef){continuation=2;if(first==0xe0)low=0xa0;if(first==0xed)high=0x9f;}
            else if(first>=0xf0&&first<=0xf4){continuation=3;if(first==0xf0)low=0x90;if(first==0xf4)high=0x8f;}
            else return false;
            if(text.size()-i<continuation)return false;
            const auto second=static_cast<unsigned char>(text[i]);
            if(second<low||second>high)return false;
            for(unsigned j=1;j<continuation;++j){const auto byte=static_cast<unsigned char>(text[i+j]);if(byte<0x80||byte>0xbf)return false;}
            i+=continuation;
        }
        return true;
    }
    static bool supported(const SnapshotJson& row,int event) {
        if(!row.is_object()||row.size()!=size_t(event==1?8:9))return false;
        for(auto it=row.begin();it!=row.end();++it){
            const std::string_view key=it.key();const auto& value=it.value();
            if(key=="childId"){if(!integer(value))return false;}
            else if(key=="requestId"){if(!value.is_null()&&!integer(value))return false;}
            else if(key=="command"||key=="messageKind"){
                if(!value.is_string()||value.get_ref<const std::string&>().size()>128||!validUtf8(value.get_ref<const std::string&>()))return false;
            }else if(key=="eventName"){
                if(!value.is_null()&&(!value.is_string()||value.get_ref<const std::string&>().size()>128||!validUtf8(value.get_ref<const std::string&>())))return false;
            }else if(key=="requestIdPresent"||key=="eventNamePresent"||key=="eventNameOmitted"){
                if(!value.is_boolean())return false;
            }else if(key==(event==0?"hresult":event==2?"jsonError":"")){
                if(!integer(value))return false;
            }else return false;
        }
        // Exact producer shape, not a prefix or a permissive future-field rule.
        for(const auto* key:{"childId","command","messageKind","requestId","requestIdPresent","eventName","eventNamePresent","eventNameOmitted"})
            if(!row.contains(key))return false;
        return event==1||row.contains(event==0?"hresult":"jsonError");
    }
    static bool sameType(const SnapshotJson& a,const SnapshotJson& b) {
        if(a.size()!=b.size())return false;
        auto ai=a.begin(),bi=b.begin();
        for(;ai!=a.end();++ai,++bi){
            if(ai.key()!=bi.key())return false;
            if(ai.key()=="childId"||ai.key()=="requestId")continue;
            if(!snapshotValueEqual(ai.value(),bi.value()))return false;
        }
        return true;
    }
    Decision observe(const char* event,const SnapshotJson& row,unsigned long long epoch=0) {
        const int kind=eventIndex(event);
        if(kind<0)return {};
        increment(offered);
        if(epoch<lastObservedRecoveryEpoch)epoch=lastObservedRecoveryEpoch;
        lastObservedRecoveryEpoch=epoch;
        if(!supported(row,kind)){
            increment(unclassifiedRows);
            const bool first=!unclassifiedWitness[kind];
            const bool reoffer=!first&&unclassifiedWitnessEpoch[kind]!=epoch;
            unclassifiedWitness[kind]=true;unclassifiedWitnessEpoch[kind]=epoch;
            if(reoffer)increment(reoffered);
            return {first||reoffer,first?"unclassified-first-witness":reoffer?"unclassified-recovery-witness":"unclassified-detail",Capacity};
        }
        for(size_t i=0;i<types;++i)if(entries[i].event==kind&&sameType(entries[i].value,row)){
            increment(repeated);
            const bool reoffer=entries[i].witnessEpoch!=epoch;
            if(reoffer){entries[i].witnessEpoch=epoch;increment(reoffered);}
            return {reoffer,reoffer?"type-recovery-witness":"repeat-type",i};
        }
        if(types<Capacity){
            const size_t index=types;
            entries[index].value=row;entries[index].event=kind;entries[index].witnessEpoch=epoch;++types;
            return {true,"first-type",index};
        }
        increment(untrackedRows);
        const bool first=!capacityWitness[kind];
        const bool reoffer=!first&&capacityWitnessEpoch[kind]!=epoch;
        capacityWitness[kind]=true;capacityWitnessEpoch[kind]=epoch;
        if(reoffer)increment(reoffered);
        return {first||reoffer,first?"capacity-first-witness":reoffer?"capacity-recovery-witness":"capacity-detail",Capacity};
    }
    SnapshotJson snapshot() const {
        SnapshotJson descriptor=SnapshotJson::array();
        const char* names[]={"ipc-response-delivery-failed","ipc-response-delivery-exception","ipc-response-serialization-failed"};
        for(size_t i=0;i<types;++i){
            auto value=entries[i].value;value.erase("childId");value.erase("requestId");
            descriptor.push_back({{"typeIndex",i},{"event",names[entries[i].event]},{"descriptor",std::move(value)}});
        }
        return {{"schema",2},{"scope","current-Native-process-selection-and-reoffer;not-durable-or-operation-history"},
            {"capacity",Capacity},{"typesRetained",types},{"offered",offered},{"repeated",repeated},
            {"untrackedRows",untrackedRows},{"unclassifiedRows",unclassifiedRows},{"counterLimit",~0ULL},
            {"fileRecoveryReoffer",{{"schemaVersion",1},{"scope","original-good-event-append-epoch;selected-type-or-event-witness;not-delivery-proof"},
                {"lastObservedEpoch",lastObservedRecoveryEpoch},{"selected",reoffered},{"typeCapacityReset",false},{"counterReset",false},{"deliveryProven",false}}},
            {"capacityWitnessSelectedByEvent",capacityWitness},{"unclassifiedWitnessSelectedByEvent",unclassifiedWitness},
            {"descriptors",std::move(descriptor)},{"firstSelectionMeansFileWrite",false},
            {"requestOrRendererOrPowerOrPhysicalCoverage",false},{"untrackedRowsAreUniqueTypes",false}};
    }
};

struct IpcFailureLogHolder {
    std::mutex mutex;
    IpcFailureLogState value;
};
inline IpcFailureLogHolder& ipcFailureLogState() {
    static IpcFailureLogHolder state;return state;
}
inline SnapshotJson ipcFailureLogStats() {
    auto& state=ipcFailureLogState();std::lock_guard<std::mutex> lock(state.mutex);
    return state.value.snapshot();
}

// The existing 32-row failure window is diagnostic admission, not a CLI,
// durable-write, device-return or whole-call failure counter.
struct HidHideFailureReceiptState {
    static constexpr unsigned Limit=32;
    static constexpr unsigned long long CounterLimit=~0ULL;
    unsigned long long power=0,observed=0,admitted=0,omitted=0,exceptions=0;
    unsigned long long windowObserved=0,windowOmitted=0;
    unsigned windowAdmitted=0;
    bool known=false,counterSaturated=false;
    void increment(unsigned long long& value) noexcept {
        if(value!=CounterLimit)++value;
        if(value==CounterLimit)counterSaturated=true;
    }
    unsigned observe(unsigned long long nextPower) noexcept {
        if(!known||power!=nextPower){power=nextPower;windowObserved=windowOmitted=0;windowAdmitted=0;known=true;}
        increment(observed);increment(windowObserved);
        if(windowAdmitted>=Limit){increment(omitted);increment(windowOmitted);return 0;}
        ++windowAdmitted;increment(admitted);return windowAdmitted;
    }
    void diagnosticException() noexcept {increment(exceptions);}
    SnapshotJson snapshot() const {
        return {{"schemaVersion",1},{"snapshotAvailable",true},{"failureReceiptLimit",Limit},
            {"observedFailureReceipts",observed},{"admittedReceipts",admitted},{"budgetOmissions",omitted},
            {"diagnosticBodyExceptions",exceptions},{"counterSaturated",counterSaturated},{"counterLimit",CounterLimit},
            {"lastWindow",{{"powerKnown",known},{"powerGeneration",known?SnapshotJson(power):SnapshotJson(nullptr)},
                {"observedFailureReceipts",windowObserved},{"admittedReceipts",windowAdmitted},{"budgetOmissions",windowOmitted}}},
            {"scope","entered-failed-receipt-destructors;not-all-HidHide-call-failures"},
            {"windowScope","last-observed-power-at-receipt;not-live-export-power-or-request-start"},
            {"admissionProvesFileWrite",false},{"exceptionsCoverSinkFailures",false},
            {"wholeCallFailureHistoryComplete",false},{"physicalReturnProven",false}};
    }
};
struct HidHideFailureReceiptHolder {
    std::mutex mutex;
    HidHideFailureReceiptState value;
};
inline HidHideFailureReceiptHolder& hidHideFailureReceiptState() {
    static HidHideFailureReceiptHolder state;return state;
}
inline SnapshotJson hidHideFailureReceiptLogStats() {
    auto& state=hidHideFailureReceiptState();
    std::unique_lock<std::mutex> lock(state.mutex,std::try_to_lock);
    if(!lock.owns_lock())return {{"schemaVersion",1},{"snapshotAvailable",false},{"reason","busy"},
        {"failureReceiptLimit",HidHideFailureReceiptState::Limit},
        {"scope","entered-failed-receipt-destructors;not-all-HidHide-call-failures"}};
    return state.value.snapshot();
}


struct JsonSnapshotLogState {
    SnapshotJson value;
    unsigned long long generation = 0;
    bool known = false;
    bool observe(const SnapshotJson& next, unsigned long long nextGeneration, SnapshotKind kind = SnapshotKind::full) {
        if (known && generation == nextGeneration && snapshotEqual(value, next, kind)) return false;
        value = next; generation = nextGeneration; known = true;
        return true;
    }
};

// Two existing live-helper wait observations can alternate. Keep their memos
// separate, while every action/terminal/unknown receipt rearms this event.
struct ForegroundWaitLogState {
    static constexpr size_t MaxNodes=1024,MaxTextBytes=32768,MaxDepth=8;
    static constexpr unsigned long long MaxBoundaryRepeatMs=1000;
    struct Context {std::string attempt,root,family;unsigned long long generation=0,power=0,edge=0,refreshEdge=0,ticket=0,childPid=0;bool known=false;};
    struct BoundaryClock {unsigned long long begin=0,end=0,sequence=0,started=0,completed=0;};
    std::mutex mutex;
    std::array<Context,2> contexts;
    std::array<JsonSnapshotLogState,4> slots;
    std::array<BoundaryClock,4> lastBoundary{};
    unsigned long long deliveryEpoch=0;
    static int eventIndex(const char* event) noexcept {
        if(!event)return -1;const std::string_view name(event);
        return name=="native-foreground-isolation"?0:name=="msi-native-foreground-isolation"?1:-1;
    }
    static void compactHelperEmptyStdout(SnapshotJson& detail) {
        constexpr auto marker = "foregroundHelperEmptyStdoutV1";
        const auto report = detail.find("helperDiagnostic");
        if (report == detail.end() || !report->is_object() || report->size() != 5 || detail.contains(marker)) return;
        const auto state = report->find("state"), transport = report->find("transport"), scope = report->find("scope");
        const auto bytes = report->find("stdoutBytes"), hardware = report->find("hardwareSuccessProven");
        if (state == report->end() || *state != "record-missing" ||
            transport == report->end() || *transport != "delete-on-close-stdout" ||
            scope == report->end() || *scope != "helper-self-reported-existing-call-facts" ||
            bytes == report->end() || !bytes->is_number_integer() || *bytes != 0 ||
            hardware == report->end() || !hardware->is_boolean() || *hardware != false) return;
        try {
            detail.emplace(marker, 1);
            detail.erase(report);
        } catch (const std::bad_alloc&) { /* Keep the complete report if the marker cannot be allocated. */ }
    }
    static void compactReceiptScopes(const char* event,SnapshotJson& detail) {
        if(!event||(event[0]!='n'&&event[0]!='m')||eventIndex(event)<0||!detail.is_object())return;
        compactHelperEmptyStdout(detail);
        constexpr const char* idleQueryKey = "helperExitQueryIdleV1";
        const auto query = detail.find("helperExitQuery");
        const auto reason = detail.find("reason");
        // Older readers consult the explicit query before interpreting this terminal receipt.
        const bool terminalReport = reason != detail.end() && *reason == "same-helper-terminal-receipt";
        if (!terminalReport && query != detail.end() && query->is_object() && query->size() == 6 && !detail.contains(idleQueryKey)) {
            const auto schema = query->find("schema"), api = query->find("api"), attempted = query->find("attempted");
            const auto isNull = [&](const char* key) {
                const auto field = query->find(key);
                return field != query->end() && field->is_null();
            };
            if (schema != query->end() && schema->is_number_integer() && *schema == 1 &&
                api != query->end() && api->is_string() && *api == "GetExitCodeProcess" &&
                attempted != query->end() && attempted->is_boolean() && *attempted == false &&
                isNull("succeeded") && isNull("errorDomain") && isNull("error")) {
                try {
                    detail.emplace(idleQueryKey, 1);
                    detail.erase(query);
                } catch (const std::bad_alloc&) { /* Keep the full query receipt. */ }
            }
        }
        if (!detail.contains("foregroundXusbRootCopyV1")) {
            const auto root = detail.find("sourceRoot");
            const auto targets = detail.find("xusbGameTargets");
            if (root != detail.end() && root->is_string() && targets != detail.end() && targets->is_array() &&
                targets->size() == 1 && (*targets)[0] == *root) {
                const auto& text = root->get_ref<const std::string&>();
                if (text.size() >= 24 && text.size() <= 2048 &&
                    std::all_of(text.begin(), text.end(), [](unsigned char ch) { return ch >= 0x20 && ch <= 0x7e; })) {
                    try {
                        detail.emplace("foregroundXusbRootCopyV1", 1);
                        detail.erase("xusbGameTargets");
                    } catch (const std::bad_alloc&) { /* Keep the targets if marker allocation fails. */ }
                }
            }
        }
        if (!detail.contains("foregroundHelperTargetCopyV1")) {
            const auto target = detail.find("helperGameTarget");
            if (target != detail.end() && target->is_string()) {
                const auto& text = target->get_ref<const std::string&>();
                if (text.size() >= 24 && text.size() <= 2048) {
                    const auto matches = [&](const char* key) {
                        const auto from = detail.find(key);
                        return from != detail.end() && from->is_string() && from->get_ref<const std::string&>() == text;
                    };
                    const unsigned source = matches("sourceRoot") ? 1u : matches("gameSourceCandidate") ? 2u : 0u;
                    if (source) {
                        try {
                            detail.emplace("foregroundHelperTargetCopyV1", source);
                            detail.erase("helperGameTarget");
                        } catch (const std::bad_alloc&) { /* Keep the original target if marker allocation fails. */ }
                    }
                }
            }
        }
        if(!detail.contains("foregroundStaticScopesV1")){
            struct StaticScope {const char* key;std::string_view value;};
            static constexpr StaticScope scopes[]={
                {"verificationScope","config-and-cycle-command-only"},
                {"helperIdentityScope","on-disk-before-launch-not-loaded-image-proof"},
                {"xusbTargetEvidence","active-XUSB-interface-and-exact-PnP-ancestor"},
            };
            bool matched=true;
            for(const auto& [key,value]:scopes){
                const auto field=detail.find(key);
                if(field==detail.end()||!field->is_string()||field->get_ref<const std::string&>()!=value){matched=false;break;}
            }
            if(matched){
                try {
                    detail.emplace("foregroundStaticScopesV1",1);
                    for(const auto& scope:scopes)detail.erase(scope.key);
                } catch(const std::bad_alloc&) { /* Leave the full scope fields if marker allocation fails. */ }
            }
        }
        compactInputBoundaryClocks(detail);
        compactInputBoundaryZeroFields(detail);
        compactConfigurationLimits(detail);
        if(detail.contains("foregroundScopeDefaultsV1"))return;
        constexpr auto configuration="selected-source configuration only; no authority to clear inherited/foreign entries";
        struct KnownScope {const char* key;const char* value;};
        static constexpr KnownScope known[]={
            {"inputBoundary","existing-canonical-cache-only;no-device-read-or-game-receipt"},
            {"nativeRefreshWriter","helper writer only; no node-enabled or external input delivery proof"},
            {"beforeConfigurationEvidence",configuration},
            {"hiddenConfigurationEvidence",configuration},
            {"returnConfigurationEvidence",configuration},
        };
        unsigned mask=0;
        SnapshotJson* matches[std::size(known)]{};
        for(std::size_t i=0;i<std::size(known);++i){
            auto node=detail.find(known[i].key);
            if(node==detail.end()||!node->is_object())continue;
            const auto scope=node->find("scope");
            if(scope==node->end()||!scope->is_string()||scope->get_ref<const std::string&>()!=known[i].value)continue;
            matches[i]=&*node;
            mask|=1u<<i;
        }
        if(!mask)return;
        // Keep the full receipt if the optional marker cannot be allocated.
        try {detail.emplace("foregroundScopeDefaultsV1",mask);}
        catch(const std::bad_alloc&){return;}
        for(auto* node:matches)if(node)node->erase(node->find("scope"));
    }
    static void compactInputBoundaryZeroFields(SnapshotJson& detail) {
        const auto boundary = detail.find("inputBoundary");
        constexpr auto marker = "zeroFieldsV1";
        if (boundary == detail.end() || !boundary->is_object() || boundary->contains(marker)) return;
        const auto captured = boundary->find("captured");
        if (captured == boundary->end() || !captured->is_boolean() || !captured->get<bool>()) return;
        const auto zero = [&](const char* key) {
            const auto value = boundary->find(key);
            return value != boundary->end() && value->is_number_integer() && *value == 0;
        };
        static constexpr const char* axes[] = {"lx", "ly", "rx", "ry", "lt", "rt"};
        for (const auto* key : axes) if (!zero(key)) return;
        const auto guide = boundary->find("guide");
        const bool buttons = zero("buttons") && guide != boundary->end() && guide->is_boolean() && !guide->get<bool>();
        try { boundary->emplace(marker, buttons ? 3 : 1); }
        catch (const std::bad_alloc&) { return; }
        for (const auto* key : axes) boundary->erase(key);
        if (buttons) { boundary->erase("buttons"); boundary->erase("guide"); }
    }
    static void compactInputBoundaryClocks(SnapshotJson& detail) {
        const auto boundary=detail.find("inputBoundary");
        constexpr auto marker="clockCopiesV1";
        if(boundary==detail.end()||!boundary->is_object()||boundary->contains(marker))return;
        const auto captured=boundary->find("captured");
        if(captured==boundary->end()||!captured->is_boolean()||!captured->get<bool>())return;
        struct Copy {const char* from;const char* to;};
        static constexpr Copy copies[]={
            {"captureBeginTick","captureEndTick"}, {"readStartedTick","readCompletedTick"},
            {"powerGeneration","currentPowerGeneration"}, {"topologyGeneration","currentTopologyGeneration"},
        };
        const auto u64=[](const SnapshotJson& value){
            return value.is_number_unsigned()||(value.is_number_integer()&&value.get<long long>()>=0);
        };
        unsigned mask=0;
        for(size_t i=0;i<std::size(copies);++i){
            const auto from=boundary->find(copies[i].from),to=boundary->find(copies[i].to);
            if(from!=boundary->end()&&to!=boundary->end()&&u64(*from)&&u64(*to)&&
                from->get<unsigned long long>()==to->get<unsigned long long>())mask|=1u<<i;
        }
        if(!mask)return;
        try {boundary->emplace(marker,mask);}
        catch(const std::bad_alloc&){return;}
        for(size_t i=0;i<std::size(copies);++i)if(mask&(1u<<i))boundary->erase(boundary->find(copies[i].to));
    }
    static void compactConfigurationLimits(SnapshotJson& detail) {
        constexpr auto marker="foregroundConfigurationLimitsV1";
        if(detail.contains(marker))return;
        constexpr const char* paths[]={"beforeConfigurationEvidence","hiddenConfigurationEvidence","returnConfigurationEvidence"};
        constexpr auto delivery="externalInputDeliveryProven", ownership="inheritedEntryOwnershipProven";
        unsigned mask=0;
        SnapshotJson* matches[std::size(paths)]{};
        for(size_t i=0;i<std::size(paths);++i){
            auto node=detail.find(paths[i]);
            if(node==detail.end()||!node->is_object())continue;
            const auto schema=node->find("schema"),a=node->find(delivery),b=node->find(ownership);
            if(schema==node->end()||!schema->is_number_integer()||*schema!=1||
                a==node->end()||!a->is_boolean()||a->get<bool>()||
                b==node->end()||!b->is_boolean()||b->get<bool>())continue;
            matches[i]=&*node;mask|=1u<<i;
        }
        if(!mask)return;
        try {detail.emplace(marker,mask);}
        catch(const std::bad_alloc&){return;}
        for(auto* node:matches)if(node){node->erase(node->find(delivery));node->erase(node->find(ownership));}
    }
    void forget(size_t event) noexcept {contexts[event].known=false;slots[event*2].known=slots[event*2+1].known=false;}
    static bool bounded(const SnapshotJson& value) {
        size_t nodes=0,textBytes=0;
        const auto text=[&](size_t bytes){if(bytes>MaxTextBytes-textBytes)return false;textBytes+=bytes;return true;};
        const auto visit=[&](auto&& self,const SnapshotJson& item,size_t depth)->bool {
            if(depth>MaxDepth||nodes==MaxNodes)return false;++nodes;
            if(item.is_string())return text(item.get_ref<const std::string&>().size());
            if(item.is_object()){for(auto it=item.begin();it!=item.end();++it)if(!text(it.key().size())||!self(self,it.value(),depth+1))return false;return true;}
            if(item.is_array()){for(const auto& child:item)if(!self(self,child,depth+1))return false;return true;}
            if(item.is_number_float())return std::isfinite(item.get<double>());
            return item.is_null()||item.is_boolean()||item.is_number_integer();
        };
        return visit(visit,value,0);
    }
    static BoundaryClock boundaryClock(const SnapshotJson& detail) {
        const auto sample=detail.find("inputBoundary");
        if(sample==detail.end()||!sample->is_object())return {};
        const auto number=[&](const char* key)->unsigned long long {
            const auto it=sample->find(key);
            if(it==sample->end())return 0;
            if(it->is_number_unsigned())return it->get<unsigned long long>();
            if(it->is_number_integer()){
                const auto n=it->get<long long>();if(n>0)return static_cast<unsigned long long>(n);
            }
            return 0;
        };
        const auto yes=[&](const char* key){const auto it=sample->find(key);return it!=sample->end()&&it->is_boolean()&&*it==true;};
        const auto age=sample->find("readAgeMs"),scope=sample->find("scope"),source=sample->find("inputSource");
        const auto power=number("powerGeneration"),topology=number("topologyGeneration"),identity=number("identityGeneration");
        BoundaryClock clock{number("captureBeginTick"),number("captureEndTick"),number("sampleSequence"),number("readStartedTick"),number("readCompletedTick")};
        if(!yes("captured")||!yes("live")||!yes("readContextStable")||!yes("readWindowCurrent")||
            source==sample->end()||!source->is_string()||source->get_ref<const std::string&>().empty()||
            !power||power!=number("currentPowerGeneration")||!topology||topology!=number("currentTopologyGeneration")||
            !identity||identity!=number("currentIdentityGeneration")||
            scope==sample->end()||*scope!="existing-canonical-cache-only;no-device-read-or-game-receipt"||
            !clock.begin||clock.end<clock.begin||!clock.sequence||!clock.started||clock.completed<clock.started||
            clock.completed>clock.end||age==sample->end()||!age->is_number_integer()||*age!=clock.end-clock.completed)return {};
        return clock;
    }
    static bool boundaryProgressOnly(const SnapshotJson& baseline,const SnapshotJson& detail,
            const BoundaryClock& initial,const BoundaryClock& last,const BoundaryClock& current) {
        if(!initial.end||!last.end||current.end<last.end||current.end-initial.end>=MaxBoundaryRepeatMs||
            current.begin<last.begin||current.sequence<last.sequence||current.started<last.started||current.completed<last.completed||
            (current.end==last.end&&(current.begin!=last.begin||current.sequence!=last.sequence||current.started!=last.started||current.completed!=last.completed))||
            (current.sequence==last.sequence&&(current.started!=last.started||current.completed!=last.completed))||
            current.end-current.begin!=initial.end-initial.begin||
            current.completed-current.started!=initial.completed-initial.started||baseline.size()!=detail.size())return false;
        auto left=baseline.begin(),right=detail.begin();
        for(;left!=baseline.end();++left,++right){
            if(left.key()!=right.key())return false;
            if(left.key()!="inputBoundary"){
                if(!snapshotValueEqual(left.value(),right.value()))return false;
                continue;
            }
            if(left.value().size()!=right.value().size())return false;
            auto a=left.value().begin(),b=right.value().begin();
            for(;a!=left.value().end();++a,++b){
                const std::string_view key=a.key();
                if(key!=b.key())return false;
                if(key=="captureBeginTick"||key=="captureEndTick"||key=="sampleSequence"||key=="readStartedTick"||key=="readCompletedTick"||key=="readAgeMs"){
                    if(a.value().type()!=b.value().type())return false;
                }else if(!snapshotValueEqual(a.value(),b.value()))return false;
            }
        }
        return true;
    }
    bool repeated(size_t event,const SnapshotJson& detail,unsigned long long generation,
            unsigned long long currentPid,const NativeLogFailureEvidence& failures) {
        if(event>=contexts.size())return false;std::lock_guard<std::mutex> lock(mutex);
        const auto view=[](const SnapshotJson& object,const char* key)->std::string_view {
            const auto value=object.find(key);return value!=object.end()&&value->is_string()?std::string_view(value->get_ref<const std::string&>()):std::string_view{};
        };
        const auto flag=[](const SnapshotJson& object,const char* key,bool expected){const auto value=object.find(key);return value!=object.end()&&value->is_boolean()&&value->get<bool>()==expected;};
        const auto number=[](const SnapshotJson& object,const char* key,unsigned long long& out,unsigned long long maximum=~0ULL){
            const auto value=object.find(key);if(value==object.end())return false;
            if(value->is_number_unsigned()){out=value->get<unsigned long long>();return out<=maximum;}
            if(value->is_number_integer()){const auto n=value->get<long long>();if(n<0)return false;out=static_cast<unsigned long long>(n);return out<=maximum;}
            return false;
        };
        const auto phase=view(detail,"phase"),reason=view(detail,"reason"),family=view(detail,"family");
        const int phaseSlot=phase=="restore-wait"&&reason=="same-cycle-helper-still-live"?0:
            phase=="virtual-work-deferred"&&reason=="same-native-ownership-still-unresolved"?1:-1;
        const auto writer=detail.find("nativeRefreshWriter"),query=detail.find("helperExitQuery"),diagnostic=detail.find("helperDiagnostic"),deferred=detail.find("deferredVirtualWork");
        unsigned long long power=0,edge=0,ticket=0,currentPower=0,refreshEdge=0,parent=0,child=0,error=0,reportedExit=0,writerError=0,querySchema=0;
        const auto root=view(detail,"sourceRoot");
        bool supported=detail.is_object()&&phaseSlot>=0&&(event==1?family=="msi":family=="gpd"||family=="rog")&&
            !root.empty()&&root.size()<=2048&&flag(detail,"helperPending",true)&&flag(detail,"refreshFailed",false)&&flag(detail,"virtualContinuationEnded",false)&&
            number(detail,"helperError",error,0xffffffffULL)&&error==0&&number(detail,"helperExitCode",reportedExit,0xffffffffULL)&&reportedExit==259&&
            detail.contains("helperFailure")&&detail.find("helperFailure")->is_string()&&view(detail,"helperFailure").empty()&&
            number(detail,"powerGeneration",power)&&number(detail,"edge",edge)&&edge>0&&number(detail,"refreshAttemptEdge",refreshEdge)&&refreshEdge>0&&
            number(detail,"virtualHandoffRequest",ticket)&&(phaseSlot==0||ticket>0)&&number(detail,"currentPowerGeneration",currentPower)&&currentPower==generation&&
            diagnostic!=detail.end()&&diagnostic->is_null()&&deferred!=detail.end()&&deferred->is_array()&&(phaseSlot==0||!deferred->empty())&&
            query!=detail.end()&&query->is_object()&&number(*query,"schema",querySchema)&&querySchema==1&&view(*query,"api")=="GetExitCodeProcess"&&flag(*query,"attempted",false)&&
            query->contains("succeeded")&&query->find("succeeded")->is_null()&&query->contains("error")&&query->find("error")->is_null()&&query->contains("errorDomain")&&query->find("errorDomain")->is_null()&&
            writer!=detail.end()&&writer->is_object()&&view(*writer,"phase")=="created-in-exact-job"&&flag(*writer,"reserved",true)&&flag(*writer,"settled",false)&&
            number(*writer,"parentPid",parent,0xffffffffULL)&&parent==currentPid&&parent>0&&number(*writer,"childPid",child,0xffffffffULL)&&child>0&&
            number(*writer,"error",writerError,0xffffffffULL)&&writerError==0&&writer->contains("reason")&&writer->find("reason")->is_string()&&view(*writer,"reason").empty();
        const auto attempt=writer!=detail.end()&&writer->is_object()?view(*writer,"attemptId"):std::string_view{};
        supported=supported&&!attempt.empty()&&attempt.size()<=128&&bounded(detail);
        if(!supported){forget(event);return false;}
        const auto epoch=failures.recoveryEpoch();
        if(epoch!=deliveryEpoch||failures.eventFailurePending.load(std::memory_order_acquire)||failures.residentRecordPreparationFailurePending.load(std::memory_order_acquire)||failures.residentPathPreparationFailurePending.load(std::memory_order_acquire)){forget(0);forget(1);deliveryEpoch=epoch;}
        auto& context=contexts[event];
        try {
            if(!context.known||context.attempt!=attempt||context.root!=root||context.family!=family||context.childPid!=child||context.generation!=generation||context.power!=power||context.edge!=edge||context.refreshEdge!=refreshEdge||context.ticket!=ticket){
                forget(event);context.attempt=attempt;context.root=root;context.family=family;context.childPid=child;context.generation=generation;context.power=power;context.edge=edge;context.refreshEdge=refreshEdge;context.ticket=ticket;context.known=true;
            }
            const auto index=event*2+static_cast<size_t>(phaseSlot);
            auto& slot=slots[index];
            auto& last=lastBoundary[index];
            const auto current=boundaryClock(detail);
            if(slot.known&&slot.generation==generation){
                if(current.end>=last.end&&snapshotEqual(slot.value,detail,SnapshotKind::full))return true;
                // Keep a resident anchor while fresh cache timing alone advances.
                if(current.end&&boundaryProgressOnly(slot.value,detail,boundaryClock(slot.value),last,current)){
                    last=current;return true;
                }
            }
            slot.value=detail;slot.generation=generation;slot.known=true;last=current;
            return false;
        } catch(...){forget(event);throw;}
    }
};


// One failed quarantine visibility snapshot, separate from actual restore receipts.
struct MouseCachedWindowLogState {
    SnapshotJson baseline;
    unsigned long long sequence=0, window=0, power=0, delivery=0;
    bool known=false;

    static unsigned long long positive(const SnapshotJson& row,const char* key,bool decimal=false) {
        const auto item=row.find(key);
        if(item==row.end())return 0;
        if(!decimal)return item->is_number_unsigned()?item->get<unsigned long long>():0;
        if(!item->is_string())return 0;
        const auto& text=item->get_ref<const std::string&>();
        if(text.empty()||text.size()>20||text[0]=='0')return 0;
        unsigned long long number=0;
        const auto end=text.data()+text.size();
        const auto parsed=std::from_chars(text.data(),end,number);
        return parsed.ec==std::errc{}&&parsed.ptr==end?number:0;
    }
    static bool candidate(const SnapshotJson& row) {
        const auto flag=[&](const char* key,bool value){
            auto v=row.find(key);return v!=row.end()&&v->is_boolean()&&*v==value;
        };
        const auto provider=row.find("provider"),image=row.find("foregroundImage"),on=row.find("on");
        return flag("backendReadbackFresh",false)&&flag("ok",true)&&flag("stateKnown",true)&&flag("controlAllowed",true)&&
            provider!=row.end()&&provider->is_string()&&*provider=="ymcc"&&on!=row.end()&&on->is_boolean()&&
            image!=row.end()&&image->is_string()&&!image->get_ref<const std::string&>().empty()&&
            positive(row,"backendObservedAt")&&positive(row,"observedAt")&&positive(row,"foregroundPid")&&
            positive(row,"foregroundCreated",true)&&positive(row,"foregroundWindow",true)&&positive(row,"sequence");
    }
    static bool sameDecision(const SnapshotJson& a,const SnapshotJson& b) {
        if(a.size()!=b.size())return false;
        for(auto i=a.begin(),j=b.begin();i!=a.end();++i,++j){
            if(i.key()!=j.key())return false;
            const auto& key=i.key();
            if(key=="foregroundWindow"||key=="sequence"||key=="observedAt")continue;
            if(!snapshotValueEqual(i.value(),j.value()))return false;
        }
        return true;
    }
    bool observe(const SnapshotJson& row,unsigned long long generation,const NativeLogFailureEvidence& failures) {
        if(!candidate(row)){known=false;return false;}
        const auto nextSequence=positive(row,"sequence"),nextWindow=positive(row,"foregroundWindow",true);
        const auto epoch=failures.recoveryEpoch();
        const bool pending=failures.eventFailurePending.load(std::memory_order_acquire)||
            failures.residentRecordPreparationFailurePending.load(std::memory_order_acquire)||
            failures.residentPathPreparationFailurePending.load(std::memory_order_acquire);
        if(!pending&&known&&power==generation&&delivery==epoch&&nextSequence>sequence&&nextSequence-sequence==1&&
                nextWindow!=window&&sameDecision(baseline,row)){
            sequence=nextSequence;window=nextWindow;
            return true;
        }
        known=false;
        if(!ForegroundWaitLogState::bounded(row))return false;
        baseline=row;sequence=nextSequence;window=nextWindow;power=generation;delivery=epoch;known=true;
        return false;
    }
};

struct HidHideQuarantineResultLogState {
    struct Scope {
        unsigned long long power=0, topology=0, host=0, intent=0;
        bool operator==(const Scope&) const = default;
    };
    struct Decision { bool repeat=false; unsigned long long count=0; };
    SnapshotJson receipt;
    std::string transaction, run;
    std::vector<std::wstring> targets, applications;
    Scope scope;
    unsigned long long delivery=0, lastLow=0, count=0;
    bool known=false;

    static bool candidate(const SnapshotJson& row, bool sweep) {
        const auto text=[&](const char* key, const char* value) {
            const auto v=row.find(key);return v!=row.end()&&v->is_string()&&*v==value;
        };
        const auto flag=[&](const char* key, bool value) {
            const auto v=row.find(key);return v!=row.end()&&v->is_boolean()&&*v==value;
        };
        if(!text("reason","quarantine-endpoint-absent"))return false;
        if(sweep)return flag("listAttempted",true)&&flag("listOk",false);
        return flag("unhideAttempted",true)&&text("status","release-unproven")&&
            text("commandResult","cli-fallback-failed")&&text("configReadback","UNKNOWN");
    }
    static bool boundedPaths(const std::vector<std::wstring>& paths) noexcept {
        if(paths.size()>64)return false;
        size_t left=8192;
        for(const auto& path:paths){if(path.empty()||path.size()>left)return false;left-=path.size();}
        return true;
    }
    Decision offer(const SnapshotJson& row, bool sweep, std::string_view id, std::string_view hostRun,
            const std::vector<std::wstring>& currentTargets, const std::vector<std::wstring>& currentApplications,
            Scope started, Scope completed, unsigned long long tick, const NativeLogFailureEvidence& failures) {
        if(id.empty()||id.size()>128||hostRun.size()>128||started!=completed||!candidate(row,sweep)){
            known=false;return {};
        }
        const auto epoch=failures.recoveryEpoch();
        const bool pending=failures.eventFailurePending.load(std::memory_order_acquire)||
            failures.residentRecordPreparationFailurePending.load(std::memory_order_acquire)||
            failures.residentPathPreparationFailurePending.load(std::memory_order_acquire);
        const bool same=known&&scope==started&&transaction==id&&run==hostRun&&delivery==epoch&&
            targets==currentTargets&&applications==currentApplications&&snapshotValueEqual(receipt,row);
        if(same){
            ++count;
            if(!pending&&tick-lastLow<60000)return {true,0};
            const auto observed=count;count=0;lastLow=tick;return {false,observed};
        }
        known=false;
        if(!boundedPaths(currentTargets)||!boundedPaths(currentApplications)||!ForegroundWaitLogState::bounded(row))return {};
        receipt=row;transaction=id;run=hostRun;targets=currentTargets;applications=currentApplications;
        scope=started;delivery=epoch;lastLow=tick;count=0;known=true;return {false,1};
    }
};

struct HidHideRestoreSnapshotLogState {
    struct Selection { unsigned long long sequence=0;bool repeat=false; };
    std::mutex mutex;
    SnapshotJson value;
    std::string transaction;
    unsigned long long power=0,topology=0,delivery=0,sequence=0;
    std::atomic<unsigned long long> written{0};
    bool known=false;
    Selection select(const SnapshotJson& next,std::string_view id,unsigned long long generation,
            unsigned long long topologyGeneration,bool ready,const NativeLogFailureEvidence& failures) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto phase=next.find("phase"),readback=next.find("readback"),pnp=next.find("pnpDeviceStackEvidence");
        bool stable=false;
        if(pnp!=next.end()&&pnp->is_object()){
            const auto flag=pnp->find("contextStableAcrossRead"),p=pnp->find("powerAfter"),t=pnp->find("topologyAfter");
            stable=flag!=pnp->end()&&flag->is_boolean()&&*flag==true&&
                p!=pnp->end()&&p->is_number_unsigned()&&*p==generation&&
                t!=pnp->end()&&t->is_number_unsigned()&&*t==topologyGeneration;
        }
        if(!ready||id.empty()||id.size()>128||phase==next.end()||!phase->is_string()||*phase!="restore"||
            readback==next.end()||!readback->is_boolean()||*readback!=false||!stable){
            known=false;return {};
        }
        const auto epoch=failures.recoveryEpoch();
        const bool pending=failures.eventFailurePending.load(std::memory_order_acquire)||
            failures.residentRecordPreparationFailurePending.load(std::memory_order_acquire)||
            failures.residentPathPreparationFailurePending.load(std::memory_order_acquire);
        if(!pending&&known&&transaction==id&&power==generation&&topology==topologyGeneration&&delivery==epoch&&snapshotValueEqual(value,next))
            return {sequence,written.load(std::memory_order_acquire)==sequence};
        known=false;
        if(sequence==~0ULL||!ForegroundWaitLogState::bounded(next))return {};
        value=next;transaction=id;power=generation;topology=topologyGeneration;delivery=epoch;
        known=true;return {++sequence,false};
    }
    void acknowledge(unsigned long long appendedSequence) noexcept {
        auto previous=written.load(std::memory_order_relaxed);
        while(previous<appendedSequence&&!written.compare_exchange_weak(previous,appendedSequence,std::memory_order_release,std::memory_order_relaxed)){}
    }
};
inline HidHideRestoreSnapshotLogState& hidHideRestoreSnapshotLogState() {
    static HidHideRestoreSnapshotLogState state;
    return state;
}

// RawInput collections can alternate on every packet. Keep a bounded identity
// observation per interface, not a single last-interface value. This state has
// no effect on selected devices, ownership, probing or business callbacks.
struct IdentityObservationView {
    std::array<std::string_view, 3> text; // path, instance, base container
    std::array<unsigned long long, 6> numbers; // VID/PID/usage/page/power/source generation
};
struct IdentityObservationLogState {
    struct Entry {
        std::array<std::string, 3> text;
        std::array<unsigned long long, 6> numbers{};
        bool known = false;
    };
    std::array<Entry, 16> entries{};
    size_t evict = 0;
    void invalidate(std::string_view path) noexcept {
        for (auto& entry : entries) if (path.empty() || entry.text[0] == path) entry.known = false;
    }
    bool observe(const IdentityObservationView& next) {
        Entry* chosen = nullptr;
        Entry* free = nullptr;
        for (auto& entry : entries) {
            if (entry.known && entry.text[0] == next.text[0]) { chosen = &entry; break; }
            if (!entry.known && !free) free = &entry;
        }
        if (!chosen) chosen = free ? free : &entries[evict++ % entries.size()];
        bool changed = !chosen->known || chosen->numbers != next.numbers;
        for (size_t i = 0; i < next.text.size(); ++i) changed |= chosen->text[i] != next.text[i];
        if (!changed) return false;
        for (size_t i = 0; i < next.text.size(); ++i) chosen->text[i].assign(next.text[i]);
        chosen->numbers = next.numbers; chosen->known = true;
        return true;
    }
};

inline bool frontendCanonicalMessageV1(const SnapshotJson& trace) {
    if (!trace.is_object() || trace.empty() || trace.size() > 40) return false;
    constexpr long long maxExact = 9007199254740991LL;
    // Restrict byte reconstruction to flat JSON with no float formatting.
    for (const auto& value : trace) {
        if (value.is_null() || value.is_boolean() || value.is_string()) continue;
        if (value.is_number_unsigned()) {
            if (value.get<unsigned long long>() <= static_cast<unsigned long long>(maxExact)) continue;
        } else if (value.is_number_integer()) {
            const auto number = value.get<long long>();
            if (number >= -maxExact && number <= maxExact) continue;
        }
        return false;
    }
    return true;
}

inline SnapshotJson parseFrontendPreviewMessage(const std::string& message, bool& restorable) {
    restorable = false;
    if (message.size() > 8192 || message.empty() || message.front() != '{' || message.find('\0') != std::string::npos)
        return SnapshotJson::parse(message, nullptr, false);
    size_t parsedKeys = 0;
    const auto countKeys = [&](int, SnapshotJson::parse_event_t event, SnapshotJson&) {
        if (event == SnapshotJson::parse_event_t::key) ++parsedKeys;
        return true;
    };
    auto trace = SnapshotJson::parse(message, countKeys, false);
    size_t nodes = 0, retainedKeys = 0;
    const auto bounded = [&](auto&& self, const SnapshotJson& value, unsigned depth) -> bool {
        if (depth > 10 || ++nodes > 512) return false;
        if (value.is_object()) retainedKeys += value.size();
        if (value.is_structured()) {
            for (const auto& child : value)
                if (!self(self, child, depth + 1)) return false;
        } else if (value.is_number()) {
            // Integer tokens beyond this range can become floats in the Native parser.
            const auto number = value.get<double>();
            if (!std::isfinite(number) || std::abs(number) >= 9007199254740992.0) return false;
        } else if (!value.is_null() && !value.is_boolean() && !value.is_string()) return false;
        return true;
    };
    // Duplicate keys can occur at any object depth, including within arrays.
    restorable = trace.is_object() && bounded(bounded, trace, 0) && parsedKeys == retainedKeys;
    if (restorable) return trace;
    return SnapshotJson::parse(message, nullptr, false);
}

inline std::string_view logJsonText(const SnapshotJson& object, const char* key, std::string_view fallback = {}) {
    const auto it = object.find(key);
    // Match json.value string semantics: a wrong type must reach the existing
    // diagnostic exception counter, never masquerade as a missing field.
    return it != object.end() ? std::string_view(it->get_ref<const std::string&>()) : fallback;
}
// References live only for the current observation. Owned strings are updated
// only after an emitted row. The hot unchanged-frame path allocates nothing.
struct FrameEvidenceView {
    std::array<std::string_view, 4> text;
    std::array<unsigned long long, 5> numbers;
    std::array<bool, 3> flags;
};
struct FrameEvidenceLogState {
    std::array<std::string, 4> text;
    std::array<unsigned long long, 5> numbers{};
    std::array<bool, 3> flags{};
    bool known = false;
    bool changed(const FrameEvidenceView& next) const noexcept {
        if (!known || numbers != next.numbers || flags != next.flags) return true;
        for (size_t i = 0; i < text.size(); ++i) if (text[i] != next.text[i]) return true;
        return false;
    }
    void remember(const FrameEvidenceView& next) {
        for (size_t i = 0; i < text.size(); ++i) text[i].assign(next.text[i]);
        numbers = next.numbers; flags = next.flags; known = true;
    }
};
}
