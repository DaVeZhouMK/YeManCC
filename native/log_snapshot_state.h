#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <string_view>
#include "json.hpp"

namespace ymcc::logging {
using SnapshotJson = nlohmann::json;
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
    struct Context {std::string attempt,root,family;unsigned long long generation=0,power=0,edge=0,refreshEdge=0,ticket=0,childPid=0;bool known=false;};
    std::mutex mutex;
    std::array<Context,2> contexts;
    std::array<JsonSnapshotLogState,4> slots;
    unsigned long long deliveryEpoch=0;
    static int eventIndex(const char* event) noexcept {
        if(!event)return -1;const std::string_view name(event);
        return name=="native-foreground-isolation"?0:name=="msi-native-foreground-isolation"?1:-1;
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
            return !slots[event*2+static_cast<size_t>(phaseSlot)].observe(detail,generation);
        } catch(...){forget(event);throw;}
    }
};


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
