#pragma once
#include "input_host_diagnostic_dispatch.h"
// Process-owned because an existing pool sink can outlive a bounded exit wait.
// Records contain values only; they cannot recreate a Host or submit device I/O.
namespace inputHostCriticalDiagnostics {
enum class Kind { Native, Mirror, Domain, App };
struct Record {
    Kind kind=Kind::Native;
    std::string name,domain,time,text,encoded;
    json detail;
    bool baseEvent=false;
};
using PostRejection=ymcc::inputHost::diagnostics::RejectedPost;
// One successful claimant per reason for this process lifetime. Never scan the
// record body or allocate, lock, log, retry, or reset from this rejection path.
struct PostWitness {
    std::atomic<unsigned char> state{0}; // 0 empty, 1 publishing, 2 immutable
    unsigned char kind=0,prefixBytes=0,submissionKind=0;
    std::uint64_t nameBytes=0;
    bool truncated=false,sanitized=false;
    char prefix[95]{};
};
static std::array<PostWitness,6> postWitnesses;
static std::size_t postRejectionIndex(PostRejection reason) noexcept {
    switch(reason) {
    case PostRejection::Busy:return 0;
    case PostRejection::Closed:return 1;
    case PostRejection::Oversized:return 2;
    case PostRejection::Cooldown:return 3;
    case PostRejection::Full:return 4;
    case PostRejection::SubmissionNotAccepted:
    case PostRejection::SubmissionAllocationException:
    case PostRejection::SubmissionStandardException:
    case PostRejection::SubmissionUnknownException:return 5;
    }
    return 6;
}
static const char* postWitnessKind(unsigned char kind) noexcept {
    switch(kind) {
    case 1:return "native-resident";
    case 2:return "native-detail";
    case 3:return "mirror";
    case 4:return "domain";
    case 5:return "app";
    default:return "unknown";
    }
}
static const char* postSubmissionFailureKind(unsigned char kind) noexcept {
    switch(kind) {
    case 1:return "returned-false";
    case 2:return "allocation-exception";
    case 3:return "standard-exception";
    case 4:return "unknown-exception";
    default:return "unknown";
    }
}
static void observeRejectedPost(const Record& record,PostRejection reason) noexcept {
    const auto index=postRejectionIndex(reason);if(index>=postWitnesses.size())return;
    auto& slot=postWitnesses[index];unsigned char expected=0;
    if(!slot.state.compare_exchange_strong(expected,1,std::memory_order_acq_rel,std::memory_order_relaxed))return;
    switch(reason) {
    case PostRejection::SubmissionNotAccepted:slot.submissionKind=1;break;
    case PostRejection::SubmissionAllocationException:slot.submissionKind=2;break;
    case PostRejection::SubmissionStandardException:slot.submissionKind=3;break;
    case PostRejection::SubmissionUnknownException:slot.submissionKind=4;break;
    default:break;
    }
    switch(record.kind) {
    case Kind::Native:slot.kind=record.baseEvent?1:2;break;
    case Kind::Mirror:slot.kind=3;break;
    case Kind::Domain:slot.kind=4;break;
    case Kind::App:slot.kind=5;break;
    default:break;
    }
    slot.nameBytes=record.name.size();slot.truncated=record.name.size()>sizeof(slot.prefix);
    slot.prefixBytes=static_cast<unsigned char>(slot.truncated?sizeof(slot.prefix):record.name.size());
    for(std::size_t i=0;i<slot.prefixBytes;++i) {
        const auto byte=static_cast<unsigned char>(record.name[i]);
        if(byte>=32 && byte<=126)slot.prefix[i]=static_cast<char>(byte);
        else {slot.prefix[i]='?';slot.sanitized=true;}
    }
    slot.state.store(2,std::memory_order_release);
}
static json postRejectionWitnessEvidence() noexcept {
    try {
        static constexpr const char* reasons[]={"busy","closed","oversized","cooldown","full","submission-not-accepted"};
        json rows=json::array();
        for(std::size_t i=0;i<postWitnesses.size();++i) {
            const auto& slot=postWitnesses[i];const auto state=slot.state.load(std::memory_order_acquire);
            json row={{"reason",reasons[i]},{"state",state==0?"empty":state==1?"publishing":"published"}};
            if(state==2) {
                row["recordKind"]=postWitnessKind(slot.kind);row["nameBytes"]=slot.nameBytes;
                row["namePrefix"]=std::string(slot.prefix,slot.prefixBytes);
                row["nameTruncated"]=slot.truncated;row["namePrefixSanitized"]=slot.sanitized;
                if(i==5)row["submissionFailureKind"]=postSubmissionFailureKind(slot.submissionKind);
            }
            rows.push_back(std::move(row));
        }
        return {{"schemaVersion",2},{"claimPolicy","first-successful-claim-per-post-rejection-reason;not-chronology-complete-history-or-loss-count"},
            {"nameEncoding","ascii-printable-replaced-prefix"},{"namePrefixByteLimit",95},
            {"resetWithinProcess",false},{"snapshotAtomicAcrossReasons",false},{"rows",std::move(rows)}};
    }catch(...){return nullptr;}
}

using Queue=ymcc::inputHost::diagnostics::Dispatcher<Record>;
static std::atomic<Queue*> queue{nullptr};
static std::atomic<unsigned long long> unavailable{0},preparationFailed{0};
static thread_local const std::string* replayTime=nullptr;
static thread_local const Record* replayRecord=nullptr;
struct ReplayTime {
    const std::string* prior=replayTime;
    const Record* priorRecord=replayRecord;
    explicit ReplayTime(const Record& record) noexcept {replayTime=&record.time;replayRecord=&record;}
    ~ReplayTime(){replayTime=prior;replayRecord=priorRecord;}
};
static void write(const Record& r) {
    ReplayTime producerTime(r);
    if(r.kind==Kind::Native)appendNativeLifecyclePrepared(r.name.c_str(),r.detail,r.baseEvent,&r.encoded);
    else if(r.kind==Kind::Mirror)mirrorLifecycleToDomains(r.name.c_str(),r.detail);
    else if(r.kind==Kind::Domain)appendDomainLog(r.domain.c_str(),r.name.c_str(),r.detail);
    else writeLog(r.name,r.text);
}
static void offer(Record record) noexcept {
    try {
        auto* current=queue.load(std::memory_order_acquire);
        if(!current){unavailable.fetch_add(1,std::memory_order_relaxed);return;}
        // The charge limits serialized diagnostic data, not exact allocator use.
        const auto charge=record.name.size()+record.domain.size()+record.time.size()+record.text.size()+record.encoded.size();
        (void)current->tryPost(std::move(record),charge,GetTickCount64());
    }catch(...){preparationFailed.fetch_add(1,std::memory_order_relaxed);}
}
}
static const std::string* inputHostCriticalDiagnosticReplayTime() noexcept {
    return inputHostCriticalDiagnostics::replayTime;
}
static const std::string* inputHostCriticalDiagnosticReplayDomainPayload() noexcept {
    const auto* record=inputHostCriticalDiagnostics::replayRecord;
    return record && record->kind==inputHostCriticalDiagnostics::Kind::Domain?&record->encoded:nullptr;
}
static void inputHostDeferNativeDiagnostic(const char* event,json detail,bool baseEvent,bool mirrorOnly) noexcept {
    try {
        using namespace inputHostCriticalDiagnostics;
        Record record;record.kind=mirrorOnly?Kind::Mirror:Kind::Native;
        record.name=event?event:"unknown";record.time=mirrorOnly?nativeLifecycleTimestamp():detail.value("time",std::string{});
        record.baseEvent=baseEvent;record.encoded=detail.dump();record.detail=std::move(detail);
        offer(std::move(record));
    }catch(...){inputHostCriticalDiagnostics::preparationFailed.fetch_add(1,std::memory_order_relaxed);}
}
static void inputHostDeferDomainDiagnostic(const char* domain,const char* event,const json& detail) noexcept {
    try {
        using namespace inputHostCriticalDiagnostics;
        Record record;record.kind=Kind::Domain;record.domain=domain;
        record.name=event?event:"unknown";record.time=nativeLifecycleTimestamp();
        record.detail=detail.is_object()?detail:json::object();
        decorateDomainLogWriter(record.detail);record.detail["time"]=record.time;record.detail["event"]=record.name;
        record.encoded=record.detail.dump();offer(std::move(record));
    }catch(...){inputHostCriticalDiagnostics::preparationFailed.fetch_add(1,std::memory_order_relaxed);}
}
static void inputHostDeferAppDiagnostic(const std::string& level,const std::string& message) noexcept {
    try {
        using namespace inputHostCriticalDiagnostics;
        SYSTEMTIME st;GetLocalTime(&st);char time[32];
        snprintf(time,sizeof(time),"%04d-%02d-%02d %02d:%02d:%02d.%03d",
            st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,st.wMilliseconds);
        Record record;record.kind=Kind::App;record.name=level;record.time=time;record.text=message;
        offer(std::move(record));
    }catch(...){inputHostCriticalDiagnostics::preparationFailed.fetch_add(1,std::memory_order_relaxed);}
}
static void inputHostCriticalDiagnosticsStartup() noexcept {
    using namespace inputHostCriticalDiagnostics;
    // Called once from actual cold pool startup under its original mutex. A
    // control/logging offer never allocates this owner or starts the executor.
    if(queue.load(std::memory_order_acquire))return;
    try {queue.store(new Queue([](std::function<void()> job){return poolTrySubmitExisting(std::move(job));},write,64*1024,256*1024,observeRejectedPost),std::memory_order_release);}
    catch(...){preparationFailed.fetch_add(1,std::memory_order_relaxed);}
}
static void inputHostCriticalDiagnosticsClose() noexcept {
    if(auto* queue=inputHostCriticalDiagnostics::queue.load(std::memory_order_acquire))queue->close();
}
static json inputHostCriticalDiagnosticsEvidence() noexcept {
    try {
        using namespace inputHostCriticalDiagnostics;
        auto* current=queue.load(std::memory_order_acquire);
        const auto s=current?current->snapshot():ymcc::inputHost::diagnostics::Snapshot{};
        return {{"policy","VC-CRITICAL-DIAGNOSTIC-HANDOFF-20261109-R1"},{"initialized",current!=nullptr},
            {"closed",s.closed},{"offered",s.offered},{"accepted",s.accepted},{"rejectedBusy",s.rejectedBusy},
            {"rejectedClosed",s.rejectedClosed},{"rejectedFull",s.rejectedFull},{"rejectedOversized",s.rejectedOversized},
            {"rejectedCooldown",s.rejectedCooldown},{"submitFailed",s.submitFailed},{"sinkReturned",s.sinkReturned},
            {"sinkThrew",s.sinkThrew},{"callbackAbandoned",s.callbackAbandoned},{"pendingDiscarded",s.pendingDiscarded},
            {"pendingRows",s.pendingRows},{"sinkActive",s.sinkActive},{"payloadBytes",s.payloadBytes},
            {"unavailable",unavailable.load(std::memory_order_relaxed)},
            {"preparationFailed",preparationFailed.load(std::memory_order_relaxed)},
            {"postRejectionWitnesses",postRejectionWitnessEvidence()},
            {"maxRowsIncludingActive",32},{"maxRecordPayloadBytes",65536},{"maxPayloadBytesIncludingActive",262144},
            {"newWorkerThreads",0},{"pollingTimers",0},{"snapshotAtomicAcrossCounters",false},
            {"durableWriteProven",false},{"gameInputVerified",false},
            {"scope","diagnostic-value-queue;callback-return-not-file-durability-or-input-recovery"}};
    }catch(...){return nullptr;}
}
