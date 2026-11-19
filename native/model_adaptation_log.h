#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <cstring>
#include <type_traits>
#include <exception>
#include <new>
#include <limits>
#include <ostream>
#include <streambuf>
#include <stdexcept>
#include <string_view>
#include <windows.h>
#include "json.hpp"
#include "log_jsonl_boundary.h"
#include "sleep_fact_log_record.h"
#include "virtual_adaptation_journal_scope.h"

// Log evidence only. No device queries, input decisions, timers or dynamic
// buffers on a hook/producer. Counts and press/release are never type identity.
namespace ymcc::adaptlog {
inline std::atomic<unsigned long long> journalWriterSequence{0};
using Json = nlohmann::json;
inline constexpr size_t signalCapacity = 128, journalCapacity = 256;
inline constexpr unsigned long long journalMaxBytes = 1024 * 1024;
inline constexpr size_t recordMaxBytes = 8192;
struct SignalType {
    std::uint32_t source = 0, profile = 0, command = 0, code = 0, shape = 0;
    std::uint64_t canonicalMask = 0;
    std::uint32_t reportId = 0;
    bool operator==(const SignalType&) const = default;
};
struct Signal {
    SignalType type;
    std::array<unsigned char, 512> raw{};
    unsigned rawBytes = 0;
    ULONGLONG capturedAt = 0;
    bool pressed = false, mapped = false;
};
class Signals {
    // One immutable POD type per refusal reason. No raw report, sample or clock
    // is retained here, and publishing never changes the rejected admission.
    struct RejectedType {
        std::atomic<unsigned> state{0}; // absent, publishing, immutable
        SignalType type{};
    };
    std::array<RejectedType,2> rejected_{};
    void retainRejected(const SignalType& type,size_t reason) noexcept {
        static_assert(std::atomic<unsigned>::is_always_lock_free);
        static_assert(std::is_trivially_copyable_v<SignalType>);
        auto& slot=rejected_[reason];
        if(slot.state.load(std::memory_order_relaxed)!=0)return;
        unsigned empty=0;
        if(!slot.state.compare_exchange_strong(empty,1,std::memory_order_acquire,std::memory_order_relaxed))return;
        slot.type=type;slot.state.store(2,std::memory_order_release);
    }
    void appendRejectedEvidence(Json& status) const noexcept {
        try {
            Json evidence=Json::object();
            evidence["schema"]=1;evidence["maximumWitnesses"]=2;
            evidence["scope"]="first-successful-claim-per-reason;not-all-lost-types-or-input-actions";
            evidence["rawReportRetained"]=false;evidence["snapshotAtomicWithCounters"]=false;
            evidence["attemptTickKnown"]=false;evidence["writerLifetimeKnown"]=false;evidence["historyComplete"]=false;
            evidence["witnesses"]=Json::array();auto& witnesses=evidence["witnesses"];
            for(size_t i=0;i<rejected_.size();++i){
                const auto& slot=rejected_[i];const auto state=slot.state.load(std::memory_order_acquire);
                witnesses.push_back(Json::object());auto& item=witnesses.back();
                item["reason"]=i==0?"busy":"capacity";item["state"]=state==0?"not-observed":state==1?"publishing":"retained";
                if(state==2){const auto& t=slot.type;
                    item["type"]=Json::object();auto& type=item["type"];
                    type["source"]=t.source;type["profile"]=t.profile;type["command"]=t.command;
                    type["code"]=t.code;type["shape"]=t.shape;type["canonicalMask"]=t.canonicalMask;type["reportId"]=t.reportId;
                }
            }
            status["rejectedTypeEvidence"]=std::move(evidence);
        } catch (...) {} // Optional export evidence cannot replace the original status.
    }
    struct Slot { Signal value; unsigned phase = 0; }; // empty, pending, in-flight, committed
    std::array<Slot, signalCapacity> slots_{};
    std::mutex mx_;
    std::atomic<unsigned long long> contention_{0}, capacity_{0};
    std::atomic<unsigned> pending_{0};
public:
    void offer(const Signal& value) noexcept {
        std::unique_lock<std::mutex> lock(mx_, std::try_to_lock);
        if (!lock) { ++contention_; retainRejected(value.type,0); return; }
        Slot* free = nullptr;
        for (auto& slot : slots_) {
            if (slot.phase && slot.value.type == value.type) return;
            if (!slot.phase && !free) free = &slot;
        }
        if (!free) { ++capacity_; retainRejected(value.type,1); return; }
        free->value = value;free->value.capturedAt=GetTickCount64(); free->phase = 1;pending_.fetch_add(1,std::memory_order_release);
    }
    bool take(size_t& index, Signal& value) noexcept {
        std::unique_lock<std::mutex> lock(mx_, std::try_to_lock);
        if (!lock) return false;
        for (size_t i=0;i<slots_.size();++i) if (slots_[i].phase==1) {
            slots_[i].phase=2; index=i; value=slots_[i].value; return true;
        }
        return false;
    }
    void finish(size_t index, bool committed) noexcept {
        std::lock_guard<std::mutex> lock(mx_);
        if(committed&&slots_[index].phase==2)pending_.fetch_sub(1,std::memory_order_release);
        slots_[index].phase=committed?3:1;
    }
    bool pending() const noexcept {return pending_.load(std::memory_order_acquire)!=0;}
    // Export only. Immutable first samples stay in their original slots after drain.
    // Capture stable addresses/phases under a nonblocking lock; encode after release.
    struct ExportView {const Signal* value=nullptr;unsigned phase=0;};
    struct ExportSnapshot {
        std::array<ExportView,signalCapacity> entries{};
        size_t known=0;bool available=false;
        unsigned long long busyDrops=0,capacityDrops=0;
    };
    ExportSnapshot snapshotForExport() {
        ExportSnapshot result;
        std::unique_lock<std::mutex> lock(mx_,std::try_to_lock);
        if(!lock)return result;
        result.available=true;
        result.busyDrops=contention_.load(std::memory_order_relaxed);
        result.capacityDrops=capacity_.load(std::memory_order_relaxed);
        for(const auto& slot:slots_)if(slot.phase)result.entries[result.known++]={&slot.value,slot.phase};
        return result;
    }
    Json status() {
        Json result;
        {std::lock_guard<std::mutex> lock(mx_);unsigned known=0,pending=0;result=Json::object();
            for(const auto& slot:slots_){known+=slot.phase!=0;pending+=slot.phase==1||slot.phase==2;}
            result["capacity"]=signalCapacity;result["knownTypes"]=known;result["pendingTypes"]=pending;
            result["contentionDrops"]=contention_.load();result["capacityDrops"]=capacity_.load();
            result["eviction"]=false;result["scope"]="bounded-process-type-witnesses;not-total-physical-input-count";
        }
        appendRejectedEvidence(result);return result;
    }
};

// Persistent exact JSON keys, compared without a digest. The shared mutex and
// fresh bounded journal read avoid duplicate first-types from overlapping natives.
enum class RecordResult { saved, retry, omitted };
enum class JournalDrainOutcome : unsigned char { unknown, appendReported, keyAlreadyPresent, budgetOmitted };
// Bounded failure/defer metadata only. Original Journal return values, counters,
// backoff and file policy remain independent. No strings, clocks or allocation
// are needed to retain a first observation on a failure path.
class JournalFailureEvidence {
public:
    enum class Stage : size_t { localBusy,backoff,mutexCreate,mutexWait,exists,size,time,readOpen,read,appendOpen,flush,close,exception,committedSize,committedTime,count };
    enum class Domain { none,win32,generic,system,other };
    enum class Exception { none,allocation,standard,unknown };
private:
    struct Slot {
        std::atomic<unsigned long long> count{0};std::atomic<unsigned> state{0};
        Domain domain=Domain::none;std::int64_t code=0;DWORD wait=0;bool waitKnown=false;
        Exception exception=Exception::none;
    };
    std::array<Slot,static_cast<size_t>(Stage::count)> slots_{};
public:
    void observe(Stage stage,Domain domain=Domain::none,std::int64_t code=0,
            bool waitKnown=false,DWORD wait=0,Exception exception=Exception::none) noexcept {
        auto& slot=slots_[static_cast<size_t>(stage)];auto count=slot.count.load(std::memory_order_relaxed);
        while(count!=~0ULL&&!slot.count.compare_exchange_weak(count,count+1,std::memory_order_relaxed)){}
        unsigned empty=0;
        if(!slot.state.compare_exchange_strong(empty,1,std::memory_order_acquire,std::memory_order_relaxed))return;
        slot.domain=domain;slot.code=code;slot.waitKnown=waitKnown;slot.wait=wait;slot.exception=exception;
        slot.state.store(2,std::memory_order_release);
    }
    void filesystem(Stage stage,const std::error_code& error) noexcept {
        observe(stage,error.category()==std::generic_category()?Domain::generic:
            error.category()==std::system_category()?Domain::system:Domain::other,error.value());
    }
    Json snapshot() const {
        static constexpr const char* names[]={"local-lock-busy","retry-backoff","named-mutex-create","named-mutex-wait",
            "file-exists-query","file-size-query","file-time-query","file-open-read","file-read","file-open-append",
            "file-write-flush","file-close","exception","committed-file-size-query","committed-file-time-query"};
        static_assert(std::size(names)==static_cast<size_t>(Stage::count));
        // Build structured values in their final tree. Temporary initializer-list
        // containers would allocate again in the JSON noexcept destructor.
        Json result=Json::object();
        result["schema"]=1;
        result["scope"]="current-Journal-instance-original-branches;first-successful-claim-per-stage";
        result["maximumStages"]=slots_.size();result["counterLimit"]=~0ULL;
        result["snapshotAtomicWithCounters"]=false;result["observationsAreUniqueFailedOperations"]=false;
        result["allErrorKindsRetained"]=false;result["streamLastErrorQueried"]=false;result["durableWriteProven"]=false;
        result["stages"]=Json::array();auto& rows=result["stages"];
        for(size_t i=0;i<slots_.size();++i){const auto& slot=slots_[i];const auto state=slot.state.load(std::memory_order_acquire);
            rows.push_back(Json::object());auto& row=rows.back();
            row["stage"]=names[i];row["observations"]=slot.count.load(std::memory_order_relaxed);
            row["firstState"]=state==0?"not-observed":state==1?"publishing":"retained";
            if(state==2){
                const char* domain=slot.domain==Domain::win32?"win32-last-error":slot.domain==Domain::generic?"std-error-code:generic":
                    slot.domain==Domain::system?"std-error-code:system":slot.domain==Domain::other?"std-error-code:other-category":nullptr;
                row["first"]=Json::object();auto& first=row["first"];
                first["codeKnown"]=domain!=nullptr;first["codeDomain"]=domain?Json(domain):Json(nullptr);
                first["code"]=domain?Json(slot.code):Json(nullptr);first["waitStatusKnown"]=slot.waitKnown;
                first["waitStatus"]=slot.waitKnown?Json(slot.wait):Json(nullptr);
                first["exceptionKind"]=slot.exception==Exception::none?Json(nullptr):Json(slot.exception==Exception::allocation?"allocation-exception":slot.exception==Exception::standard?"standard-exception":"unknown-exception");
            }
        }
        return result;
    }
};

class Journal {
    std::mutex mx_;
    JournalFailureEvidence failureEvidence_;
    // Protected by the existing Journal mutex. These are branch observations,
    // not unique omitted types; simultaneous row/byte limits may overlap.
    std::array<unsigned long long,5> budgetObservations_{};
    void observeBudget(size_t index) noexcept {
        auto& count=budgetObservations_[index];
        if(count!=(std::numeric_limits<unsigned long long>::max)())++count;
    }
    Json budgetEvidence() const {
        Json result=Json::object();
        result["schema"]=1;
        result["scope"]="current-Journal-instance-budget-branch-observations";
        result["counterLimit"]=(std::numeric_limits<unsigned long long>::max)();
        result["counterPolicy"]="saturating;overlapping-limits-not-summed";
        result["snapshotAtomicWithBudgetOmissions"]=true;
        result["observationsAreUniqueRecords"]=false;
        result["durableHistoryProven"]=false;
        auto& counts=result["observations"];counts=Json::object();
        counts["cachedFullPath"]=budgetObservations_[0];
        counts["observedFileOverMaxBytes"]=budgetObservations_[1];
        counts["recordCountLimit"]=budgetObservations_[2];
        counts["recordBytesLimit"]=budgetObservations_[3];
        counts["appendBytesLimit"]=budgetObservations_[4];
        return result;
    }
    const unsigned long long maxBytes_;
    const size_t maxRecords_,maxRowBytes_;
    std::atomic<unsigned long long> failures_{0};
    unsigned long long omitted_=0, invalid_=0, bytes_=0, committed_=0;
    ULONGLONG retryAt_=0;
    std::filesystem::path budgetPath_;
    bool budgetFull_=false,cacheKnown_=false;
    std::filesystem::path cachePath_;
    std::filesystem::file_time_type cacheModified_{};
    unsigned long long cacheBytes_=0,readScans_=0;
    std::unordered_set<std::string> keys_;
    unsigned rows_=0;
public:
    explicit Journal(unsigned long long maxBytes=journalMaxBytes,size_t maxRecords=journalCapacity,size_t maxRowBytes=recordMaxBytes)
        :maxBytes_(maxBytes),maxRecords_(maxRecords),maxRowBytes_(maxRowBytes){}
    RecordResult recordResult(const std::filesystem::path& path, const Json& model, const char* category,
            const Json& identity, const Json& sample, const std::string& time,
            JournalDrainOutcome* outcome=nullptr) noexcept {
        if(outcome)*outcome=JournalDrainOutcome::unknown;
        const auto resolved=[outcome](RecordResult result,JournalDrainOutcome detail) noexcept {
            if(outcome)*outcome=detail;
            return result;
        };
        try {
            std::unique_lock<std::mutex> lock(mx_,std::try_to_lock);
            if (!lock) {failureEvidence_.observe(JournalFailureEvidence::Stage::localBusy);return RecordResult::retry;}
            if(GetTickCount64()<retryAt_){failureEvidence_.observe(JournalFailureEvidence::Stage::backoff);return RecordResult::retry;}
            if(budgetFull_&&budgetPath_==path){++omitted_;observeBudget(0);return resolved(RecordResult::omitted,JournalDrainOutcome::budgetOmitted);}
            HANDLE named=CreateMutexW(nullptr,FALSE,L"Local\\YeManCC.ModelAdaptationLog.v1");
            if(!named){const DWORD error=GetLastError();failureEvidence_.observe(JournalFailureEvidence::Stage::mutexCreate,JournalFailureEvidence::Domain::win32,error);++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
            const DWORD ownership=WaitForSingleObject(named,0);
            if(ownership!=WAIT_OBJECT_0&&ownership!=WAIT_ABANDONED){const DWORD error=ownership==WAIT_FAILED?GetLastError():0;
                failureEvidence_.observe(JournalFailureEvidence::Stage::mutexWait,ownership==WAIT_FAILED?JournalFailureEvidence::Domain::win32:JournalFailureEvidence::Domain::none,error,true,ownership);CloseHandle(named);return RecordResult::retry;}
            struct Release {HANDLE h;~Release(){ReleaseMutex(h);CloseHandle(h);}} release{named};
            const Json key={{"schema",1},{"model",model},{"category",category},{"identity",identity}};
            const std::string signature=key.dump();
            std::error_code ec; const bool exists=std::filesystem::exists(path,ec);
            if(ec){failureEvidence_.filesystem(JournalFailureEvidence::Stage::exists,ec);++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
            bytes_=exists?std::filesystem::file_size(path,ec):0;
            if(ec){failureEvidence_.filesystem(JournalFailureEvidence::Stage::size,ec);++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
            if(bytes_>maxBytes_){++omitted_;observeBudget(1);budgetPath_=path;budgetFull_=true;return resolved(RecordResult::omitted,JournalDrainOutcome::budgetOmitted);}
            const auto modified=exists?std::filesystem::last_write_time(path,ec):std::filesystem::file_time_type{};
            if(ec){failureEvidence_.filesystem(JournalFailureEvidence::Stage::time,ec);++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
            if(!cacheKnown_||cachePath_!=path||cacheBytes_!=bytes_||cacheModified_!=modified){
                cacheKnown_=false;keys_.clear();rows_=0;invalid_=0;
                if(exists){++readScans_;std::ifstream in(path,std::ios::binary);if(!in){failureEvidence_.observe(JournalFailureEvidence::Stage::readOpen);++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
                    unsigned long long readBytes=0;
                    for(std::string line;std::getline(in,line);){
                        readBytes+=static_cast<unsigned long long>(line.size());if(!in.eof())++readBytes;
                        if(line.empty())continue;auto old=Json::parse(line,nullptr,false);
                        if(old.is_discarded()||!old.is_object()||!old.contains("uniqueKey")){++invalid_;continue;}
                        ++rows_;keys_.insert(old["uniqueKey"].dump());
                    }
                    // A failed CRT read can look like EOF without badbit.
                    // Count physical bytes,including delimiters,before caching keys.
                    if(in.bad()||readBytes!=bytes_){failureEvidence_.observe(JournalFailureEvidence::Stage::read);++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
                }
                cachePath_=path;cacheBytes_=bytes_;cacheModified_=modified;cacheKnown_=true;
            }
            if(keys_.contains(signature))return resolved(RecordResult::saved,JournalDrainOutcome::keyAlreadyPresent);
            SYSTEMTIME utc{};GetSystemTime(&utc);char absoluteTime[40]{};
            std::snprintf(absoluteTime,sizeof(absoluteTime),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,utc.wSecond,utc.wMilliseconds);
            const bool virtualStep=category&&std::strncmp(category,"virtual-",8)==0;
            Json row={{"schema","ymcc.model-adaptation/1"},{"event",virtualStep?"virtual-adaptation-witness":"model-adaptation-unique"},
                {"category",category},{"uniqueKey",key},{"firstSample",sample},{"recordedAtLocal",time},{"timestampUtc",absoluteTime}};
            if(virtualStep){
                row[journalScope::markerField]=journalScope::version;
                journalScope::compactFrameSample(row,category);
            }
            else {
                row["timestampScope"]=journalScope::timestampScope;
                row["policy"]="first-exact-type-across-restarts;no-press-count-or-power-generation-in-key";
                row["scope"]=journalScope::scope;
                row["writerSequenceScope"]=journalScope::sequenceScope;
            }
            row["priorInvalidRows"]=invalid_;
            const auto& writer=ymcc::sleepfactlog::writerIdentity(); // existing cached self identity
            row["writerPid"]=writer.pid;
            row["writerProcessCreation100ns"]=writer.creation100ns?Json(std::to_string(writer.creation100ns)):Json(nullptr);
            row["writerInstanceId"]=writer.creation100ns?Json("native:"+std::to_string(writer.pid)+":"+std::to_string(writer.creation100ns)):Json(nullptr);
            row["writerIdentityStatus"]=writer.creation100ns?"pid-and-process-creation-known":"pid-only;process-creation-unavailable";
            row["writerSequence"]=journalWriterSequence.fetch_add(1,std::memory_order_relaxed)+1;
            const auto serialized=row.dump();
            const bool countLimit=rows_>=maxRecords_,recordLimit=serialized.size()+1>maxRowBytes_,
                appendLimit=bytes_+serialized.size()+2>maxBytes_;
            if(countLimit||recordLimit||appendLimit){++omitted_;
                if(countLimit)observeBudget(2);
                if(recordLimit)observeBudget(3);
                if(appendLimit)observeBudget(4);
                // Append rejection depends on this row; smaller later types may still fit.
                // Admitted facts/signals already finish omitted types once in their bounded stores.
                if(countLimit){budgetPath_=path;budgetFull_=true;}
                return resolved(RecordResult::omitted,JournalDrainOutcome::budgetOmitted);}
            std::ofstream out;
            if(!ymcc::logging::openJsonLineAppend(out,path)){failureEvidence_.observe(JournalFailureEvidence::Stage::appendOpen);++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
            out<<serialized<<'\n';out.flush();
            if(!out){failureEvidence_.observe(JournalFailureEvidence::Stage::flush);++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
            out.close();if(!out){failureEvidence_.observe(JournalFailureEvidence::Stage::close);++failures_;cacheKnown_=false;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
            ++committed_;++rows_;keys_.insert(signature);
            bytes_=std::filesystem::file_size(path,ec);if(ec){failureEvidence_.filesystem(JournalFailureEvidence::Stage::committedSize,ec);cacheKnown_=false;return resolved(RecordResult::saved,JournalDrainOutcome::appendReported);}
            cacheBytes_=bytes_;cacheModified_=std::filesystem::last_write_time(path,ec);if(ec){failureEvidence_.filesystem(JournalFailureEvidence::Stage::committedTime,ec);cacheKnown_=false;}
            return resolved(RecordResult::saved,JournalDrainOutcome::appendReported);
        } catch (const std::bad_alloc&) {failureEvidence_.observe(JournalFailureEvidence::Stage::exception,JournalFailureEvidence::Domain::none,0,false,0,JournalFailureEvidence::Exception::allocation);++failures_;return RecordResult::retry;}
        catch (const std::exception&) {failureEvidence_.observe(JournalFailureEvidence::Stage::exception,JournalFailureEvidence::Domain::none,0,false,0,JournalFailureEvidence::Exception::standard);++failures_;return RecordResult::retry;}
        catch (...) {failureEvidence_.observe(JournalFailureEvidence::Stage::exception,JournalFailureEvidence::Domain::none,0,false,0,JournalFailureEvidence::Exception::unknown);++failures_;return RecordResult::retry;}
    }
    bool record(const std::filesystem::path& path,const Json& model,const char* category,const Json& identity,const Json& sample,const std::string& time) noexcept {
        return recordResult(path,model,category,identity,sample,time)==RecordResult::saved;
    }
    Json status() {
        std::lock_guard<std::mutex> lock(mx_);
        // Keep the original fields in the final object too: destroying temporary
        // initializer-list containers may allocate inside the JSON destructor.
        Json result=Json::object();
        result["maxBytes"]=maxBytes_;result["maxUniqueRecords"]=maxRecords_;result["maxRecordBytes"]=maxRowBytes_;
        result["lastObservedJournalBytes"]=bytes_;result["lastObservedUniqueRecords"]=rows_;result["priorInvalidRows"]=invalid_;
        result["journalReadScans"]=readScans_;result["cachePolicy"]="exact-keys;refresh-on-path-size-or-write-time-change-under-cross-process-mutex";
        result["committedThisProcess"]=committed_;result["writeFailures"]=failures_.load();result["budgetOmissions"]=omitted_;
        result["budgetFull"]=budgetFull_;result["retryBackoffMs"]=5000;result["completeHistory"]=false;
        result["budgetPolicy"]="keep-original-first-types;no-eviction;gaps-explicit-in-export-status";
        try {result["failureEvidence"]=failureEvidence_.snapshot();}catch(...){}
        try {result["budgetEvidence"]=budgetEvidence();}catch(...){}
        return result;
    }
};
}

namespace ymcc::adaptlog {
struct Fact { Json model, identity, sample; std::string category; ULONGLONG capturedAt=0; };
// Export-only descriptor serialization sink. Count the complete JSON stream,
// but retain only its original first2048 bytes. No producer/key decisions here.
class DescriptorPrefixBuffer final : public std::streambuf {
    std::array<char,2048> prefix_{};
    size_t total_=0,retained_=0;
    void append(const char* data,size_t size){
        if(size>(std::numeric_limits<size_t>::max)()-total_)throw std::length_error("descriptor length overflow");
        const auto kept=(std::min)(size,prefix_.size()-retained_);
        if(kept)std::memcpy(prefix_.data()+retained_,data,kept);
        retained_+=kept;total_+=size;
    }
    std::streamsize xsputn(const char* data,std::streamsize size) override {
        if(size<0)throw std::length_error("negative descriptor write");
        append(data,static_cast<size_t>(size));return size;
    }
    int_type overflow(int_type value) override {
        if(traits_type::eq_int_type(value,traits_type::eof()))return traits_type::not_eof(value);
        const char byte=traits_type::to_char_type(value);append(&byte,1);return value;
    }
public:
    size_t total() const noexcept{return total_;}
    size_t retained() const noexcept{return retained_;}
    const char* data() const noexcept{return prefix_.data();}
};
// Strict UTF8 category quoting matches Json(category).dump(false-ensure-ascii)
// without copying the full immutable category into a temporary JSON string.
inline void writeDescriptorCategory(std::ostream& output,std::string_view text){
    output.put('"');constexpr char digits[]="0123456789abcdef";
    for(size_t i=0;i<text.size();){
        const auto byte=static_cast<unsigned char>(text[i]);
        if(byte>=0x80){
            unsigned continuation=0;unsigned char low=0x80,high=0xbf;
            if(byte>=0xc2&&byte<=0xdf)continuation=1;
            else if(byte>=0xe0&&byte<=0xef){continuation=2;if(byte==0xe0)low=0xa0;if(byte==0xed)high=0x9f;}
            else if(byte>=0xf0&&byte<=0xf4){continuation=3;if(byte==0xf0)low=0x90;if(byte==0xf4)high=0x8f;}
            else throw std::runtime_error("invalid descriptor category UTF8");
            if(text.size()-i-1<continuation)throw std::runtime_error("incomplete descriptor category UTF8");
            const auto second=static_cast<unsigned char>(text[i+1]);
            if(second<low||second>high)throw std::runtime_error("invalid descriptor category UTF8");
            for(unsigned j=2;j<=continuation;++j){const auto next=static_cast<unsigned char>(text[i+j]);if(next<0x80||next>0xbf)throw std::runtime_error("invalid descriptor category UTF8");}
            output.write(text.data()+i,static_cast<std::streamsize>(continuation+1));i+=continuation+1;continue;
        }
        ++i;
        switch(byte){
            case '\b':output.write("\\b",2);break;
            case '\f':output.write("\\f",2);break;
            case '\n':output.write("\\n",2);break;
            case '\r':output.write("\\r",2);break;
            case '\t':output.write("\\t",2);break;
            case '"':output.write("\\\"",2);break;
            case '\\':output.write("\\\\",2);break;
            default:
                if(byte<0x20){const char escaped[]={'\\','u','0','0',digits[byte>>4],digits[byte&15]};output.write(escaped,6);}
                else output.put(static_cast<char>(byte));
        }
    }
    output.put('"');
}

template<size_t Capacity> class BasicFacts {
    // Two immutable rejected descriptors only. The original offer still fails;
    // publishing these never queues, retries or rebuilds its first sample.
    struct RejectedDescriptor {
        std::atomic<unsigned> state{0}; // 0 absent, 1 publishing, 2 immutable
        Json model, identity;
        std::string category;
    };
    std::array<RejectedDescriptor,2> rejected_{};
    void retainRejected(Json&& model,Json&& identity,std::string&& category,size_t reason) noexcept {
        static_assert(std::is_nothrow_move_assignable_v<Json>);
        static_assert(std::is_nothrow_move_assignable_v<std::string>);
        auto& slot=rejected_[reason];
        if(slot.state.load(std::memory_order_relaxed)!=0)return;
        unsigned empty=0;
        if(!slot.state.compare_exchange_strong(empty,1,std::memory_order_acquire,std::memory_order_relaxed))return;
        slot.model=std::move(model);slot.identity=std::move(identity);slot.category=std::move(category);
        slot.state.store(2,std::memory_order_release);
    }
    void appendRejectedEvidence(Json& status) const noexcept {
        try {
            constexpr size_t prefixBytes=2048;
            Json evidence=Json::object();evidence["schema"]=1;
            evidence["scope"]="first-rejected-offer-descriptor-per-reason;not-unique-lost-types-or-input-actions";
            evidence["maximumWitnesses"]=2;evidence["maximumPrefixBytes"]=prefixBytes;evidence["sampleRetained"]=false;
            evidence["snapshotAtomicWithCounters"]=false;evidence["attemptTickKnown"]=false;evidence["attemptTickMs"]=nullptr;
            evidence["writerLifetimeKnown"]=false;evidence["historyComplete"]=false;
            evidence["witnesses"]=Json::array();auto& witnesses=evidence["witnesses"];
            for(size_t i=0;i<rejected_.size();++i){
                const auto& slot=rejected_[i];const auto state=slot.state.load(std::memory_order_acquire);
                witnesses.push_back(Json::object());auto& item=witnesses.back();
                item["reason"]=i==0?"busy":"capacity";item["state"]=state==0?"not-observed":state==1?"publishing":"retained";
                if(state==2){
                    try {
                        DescriptorPrefixBuffer prefix;std::ostream output(&prefix);
                        output.exceptions(std::ios::badbit|std::ios::failbit);
                        output<<"{\"category\":";writeDescriptorCategory(output,slot.category);
                        output<<",\"identity\":"<<slot.identity<<",\"model\":"<<slot.model<<'}';
                        output.flush();const auto count=prefix.retained();
                        std::string hex;hex.reserve(count*2);constexpr char digits[]="0123456789abcdef";
                        for(size_t n=0;n<count;++n){const auto byte=static_cast<unsigned char>(prefix.data()[n]);hex+=digits[byte>>4];hex+=digits[byte&15];}
                        item["descriptorEncoding"]="JSON-UTF8-prefix-as-hex";
                        item["descriptorBytes"]=prefix.total();item["prefixBytes"]=count;
                        item["prefixHex"]=std::move(hex);item["descriptorComplete"]=count==prefix.total();
                    } catch (...) {item["state"]="descriptor-serialization-failed";}
                }
            }
            status["rejectedOfferEvidence"]=std::move(evidence);
        } catch (...) {} // A failed optional diagnostic cannot replace original status.
    }
    struct Slot { Fact value; unsigned phase=0;JournalDrainOutcome journalOutcome=JournalDrainOutcome::unknown; };
    std::array<Slot,Capacity> slots_{};
    std::mutex mx_;
    std::atomic<unsigned long long> busyDrops_{0};
    unsigned long long capacityDrops_=0;
    std::size_t known_=0; // Slots are append-only; committed facts remain known.
    std::atomic<unsigned> pending_{0};
    bool knownLocked(const Json& model,const Json& identity,const std::string& category) const {
        for(std::size_t i=0;i<known_;++i){const auto& value=slots_[i].value;
            if(value.category==category&&value.model==model&&value.identity==identity)return true;}
        return false;
    }
public:
    // Slot values are immutable after offer. Snapshot only their stable addresses
    // and drain metadata; all JSON copying/serialization happens after this lock.
    struct ExportView {const Fact* value=nullptr;unsigned phase=0;JournalDrainOutcome journalOutcome=JournalDrainOutcome::unknown;};
    struct ExportSnapshot {
        std::array<ExportView,Capacity> entries{};
        size_t known=0;bool available=false;
        unsigned long long busyDrops=0,capacityDrops=0;
    };
    ExportSnapshot snapshotForExport() {
        ExportSnapshot result;
        std::unique_lock<std::mutex> lock(mx_,std::try_to_lock);
        if(!lock)return result;
        result.available=true;result.known=known_;
        result.busyDrops=busyDrops_.load(std::memory_order_relaxed);result.capacityDrops=capacityDrops_;
        for(size_t i=0;i<known_;++i)result.entries[i]={&slots_[i].value,slots_[i].phase,slots_[i].journalOutcome};
        return result;
    }
    void offer(Fact value) {
        std::unique_lock<std::mutex> lock(mx_,std::try_to_lock);
        if(!lock){busyDrops_.fetch_add(1,std::memory_order_relaxed);retainRejected(std::move(value.model),std::move(value.identity),std::move(value.category),0);return;}
        if(knownLocked(value.model,value.identity,value.category))return;
        if(known_==Capacity){++capacityDrops_;retainRejected(std::move(value.model),std::move(value.identity),std::move(value.category),1);return;}
        auto& slot=slots_[known_];
        slot.value=std::move(value);slot.value.capturedAt=GetTickCount64();slot.phase=1;++known_;
        pending_.fetch_add(1,std::memory_order_release);
    }
    // The duplicate fast path avoids building a full first sample. A busy
    // precheck falls through to the original nonblocking offer; it is not a drop.
    // Build outside the mutex; offer rechecks identity/capacity after a race.
    template<class BuildSample> void offerLazy(Json model,Json identity,std::string category,BuildSample&& build) {
        {std::unique_lock<std::mutex> lock(mx_,std::try_to_lock);
            if(lock){
                if(knownLocked(model,identity,category))return;
                if(known_==Capacity){++capacityDrops_;retainRejected(std::move(model),std::move(identity),std::move(category),1);return;}
            }
        }
        offer({std::move(model),std::move(identity),std::forward<BuildSample>(build)(),std::move(category)});
    }
    bool take(size_t& index,Fact& value){
        std::unique_lock<std::mutex> lock(mx_,std::try_to_lock);if(!lock)return false;
        for(size_t i=0;i<known_;++i)if(slots_[i].phase==1){value=slots_[i].value;index=i;slots_[i].phase=2;return true;}
        return false;
    }
    void finish(size_t index,bool drainResolved,JournalDrainOutcome outcome=JournalDrainOutcome::unknown) noexcept {
        std::lock_guard<std::mutex> lock(mx_);auto& slot=slots_[index];
        if(drainResolved&&slot.phase==2)pending_.fetch_sub(1,std::memory_order_release);
        slot.phase=drainResolved?3:1;slot.journalOutcome=drainResolved?outcome:JournalDrainOutcome::unknown;
    }
    bool pending() const noexcept {return pending_.load(std::memory_order_acquire)!=0;}
    Json status(){
        Json result;
        {std::lock_guard<std::mutex> lock(mx_);result=Json::object();
            const auto busy=busyDrops_.load(std::memory_order_relaxed);
            result["capacity"]=Capacity;result["knownFacts"]=known_;result["pendingFacts"]=pending_.load(std::memory_order_acquire);
            result["boundedDrops"]=busy+capacityDrops_;result["busyDrops"]=busy;result["capacityDrops"]=capacityDrops_;
            result["dropScope"]="offer-attempts;busy-is-not-capacity;repeated-dropped-types-may-repeat";result["eviction"]=false;
        }
        appendRejectedEvidence(result);return result;
    }
};
using Facts=BasicFacts<128>;
using VirtualFacts=BasicFacts<512>;
}
