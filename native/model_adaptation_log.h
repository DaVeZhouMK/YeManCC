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
#include <cstring>
#include <windows.h>
#include "json.hpp"
#include "log_jsonl_boundary.h"
#include "sleep_fact_log_record.h"

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
    struct Slot { Signal value; unsigned phase = 0; }; // empty, pending, in-flight, committed
    std::array<Slot, signalCapacity> slots_{};
    std::mutex mx_;
    std::atomic<unsigned long long> contention_{0}, capacity_{0};
    std::atomic<unsigned> pending_{0};
public:
    void offer(const Signal& value) noexcept {
        std::unique_lock<std::mutex> lock(mx_, std::try_to_lock);
        if (!lock) { ++contention_; return; }
        Slot* free = nullptr;
        for (auto& slot : slots_) {
            if (slot.phase && slot.value.type == value.type) return;
            if (!slot.phase && !free) free = &slot;
        }
        if (!free) { ++capacity_; return; }
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
    Json status() {
        std::lock_guard<std::mutex> lock(mx_); unsigned known=0,pending=0;
        for (const auto& slot:slots_) { known+=slot.phase!=0; pending+=slot.phase==1||slot.phase==2; }
        return {{"capacity",signalCapacity},{"knownTypes",known},{"pendingTypes",pending},
            {"contentionDrops",contention_.load()},{"capacityDrops",capacity_.load()},
            {"eviction",false},{"scope","bounded-process-type-witnesses;not-total-physical-input-count"}};
    }
};

// Persistent exact JSON keys, compared without a digest. The shared mutex and
// fresh bounded journal read avoid duplicate first-types from overlapping natives.
enum class RecordResult { saved, retry, omitted };
class Journal {
    std::mutex mx_;
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
            const Json& identity, const Json& sample, const std::string& time) noexcept {
        try {
            std::unique_lock<std::mutex> lock(mx_,std::try_to_lock);
            if (!lock) return RecordResult::retry;
            if(GetTickCount64()<retryAt_)return RecordResult::retry;
            if(budgetFull_&&budgetPath_==path){++omitted_;return RecordResult::omitted;}
            HANDLE named=CreateMutexW(nullptr,FALSE,L"Local\\YeManCC.ModelAdaptationLog.v1");
            if(!named){++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
            const DWORD ownership=WaitForSingleObject(named,0);
            if(ownership!=WAIT_OBJECT_0&&ownership!=WAIT_ABANDONED){CloseHandle(named);return RecordResult::retry;}
            struct Release {HANDLE h;~Release(){ReleaseMutex(h);CloseHandle(h);}} release{named};
            const Json key={{"schema",1},{"model",model},{"category",category},{"identity",identity}};
            const std::string signature=key.dump();
            SYSTEMTIME utc{};GetSystemTime(&utc);char absoluteTime[40]{};
            std::snprintf(absoluteTime,sizeof(absoluteTime),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,utc.wSecond,utc.wMilliseconds);
            const bool virtualStep=category&&std::strncmp(category,"virtual-",8)==0;
            Json row={{"schema","ymcc.model-adaptation/1"},{"event",virtualStep?"virtual-adaptation-witness":"model-adaptation-unique"},
                {"category",category},{"uniqueKey",key},{"firstSample",sample},{"recordedAtLocal",time},{"timestampUtc",absoluteTime},{"timestampScope","journal-row-creation;sample-capturedTickMs-is-boot-monotonic"},
                {"writerPid",GetCurrentProcessId()},{"policy",virtualStep?"first-step-within-operation;generation-binds-causality;not-per-frame-sequence":"first-exact-type-across-restarts;no-press-count-or-power-generation-in-key"},
                {"scope","observed-evidence-only;not-action-execution-or-hardware-success-proof"}};
            std::error_code ec; const bool exists=std::filesystem::exists(path,ec);
            if(ec){++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
            bytes_=exists?std::filesystem::file_size(path,ec):0;
            if(ec){++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
            if(bytes_>maxBytes_){++omitted_;budgetPath_=path;budgetFull_=true;return RecordResult::omitted;}
            const auto modified=exists?std::filesystem::last_write_time(path,ec):std::filesystem::file_time_type{};
            if(ec){++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
            if(!cacheKnown_||cachePath_!=path||cacheBytes_!=bytes_||cacheModified_!=modified){
                cacheKnown_=false;keys_.clear();rows_=0;invalid_=0;
                if(exists){++readScans_;std::ifstream in(path,std::ios::binary);if(!in){++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
                    for(std::string line;std::getline(in,line);){if(line.empty())continue;auto old=Json::parse(line,nullptr,false);
                        if(old.is_discarded()||!old.is_object()||!old.contains("uniqueKey")){++invalid_;continue;}
                        ++rows_;keys_.insert(old["uniqueKey"].dump());
                    }
                    if(in.bad()){++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
                }
                cachePath_=path;cacheBytes_=bytes_;cacheModified_=modified;cacheKnown_=true;
            }
            if(keys_.contains(signature))return RecordResult::saved;
            row["priorInvalidRows"]=invalid_;
            const auto& writer=ymcc::sleepfactlog::writerIdentity(); // existing cached self identity
            row["writerPid"]=writer.pid;
            row["writerProcessCreation100ns"]=writer.creation100ns?Json(std::to_string(writer.creation100ns)):Json(nullptr);
            row["writerInstanceId"]=writer.creation100ns?Json("native:"+std::to_string(writer.pid)+":"+std::to_string(writer.creation100ns)):Json(nullptr);
            row["writerIdentityStatus"]=writer.creation100ns?"pid-and-process-creation-known":"pid-only;process-creation-unavailable";
            row["writerSequence"]=journalWriterSequence.fetch_add(1,std::memory_order_relaxed)+1;
            row["writerSequenceScope"]="adaptation-journals-in-process;record-creation;gaps-allowed";
            const auto serialized=row.dump();
            if(rows_>=maxRecords_||serialized.size()+1>maxRowBytes_||
                bytes_+serialized.size()+2>maxBytes_){++omitted_;
                if(rows_>=maxRecords_||bytes_+serialized.size()+2>maxBytes_){budgetPath_=path;budgetFull_=true;}
                return RecordResult::omitted;}
            std::ofstream out;
            if(!ymcc::logging::openJsonLineAppend(out,path)){++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
            out<<serialized<<'\n';out.flush();
            if(!out){++failures_;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
            out.close();if(!out){++failures_;cacheKnown_=false;retryAt_=GetTickCount64()+5000;return RecordResult::retry;}
            ++committed_;++rows_;keys_.insert(signature);
            bytes_=std::filesystem::file_size(path,ec);if(ec){cacheKnown_=false;return RecordResult::saved;}
            cacheBytes_=bytes_;cacheModified_=std::filesystem::last_write_time(path,ec);if(ec)cacheKnown_=false;
            return RecordResult::saved;
        } catch (...) { ++failures_;return RecordResult::retry; }
    }
    bool record(const std::filesystem::path& path,const Json& model,const char* category,const Json& identity,const Json& sample,const std::string& time) noexcept {
        return recordResult(path,model,category,identity,sample,time)==RecordResult::saved;
    }
    Json status() {
        std::lock_guard<std::mutex> lock(mx_);
        return {{"maxBytes",maxBytes_},{"maxUniqueRecords",maxRecords_},{"maxRecordBytes",maxRowBytes_},
            {"lastObservedJournalBytes",bytes_},{"lastObservedUniqueRecords",rows_},{"priorInvalidRows",invalid_},
            {"journalReadScans",readScans_},{"cachePolicy","exact-keys;refresh-on-path-size-or-write-time-change-under-cross-process-mutex"},{"committedThisProcess",committed_},{"writeFailures",failures_.load()},{"budgetOmissions",omitted_},
            {"budgetFull",budgetFull_},{"retryBackoffMs",5000},{"completeHistory",false},{"budgetPolicy","keep-original-first-types;no-eviction;gaps-explicit-in-export-status"}};
    }
};
}

namespace ymcc::adaptlog {
struct Fact { Json model, identity, sample; std::string category; ULONGLONG capturedAt=0; };
template<size_t Capacity> class BasicFacts {
    struct Slot { Fact value; unsigned phase=0; };
    std::array<Slot,Capacity> slots_{};
    std::mutex mx_;
    std::atomic<unsigned long long> dropped_{0};
    std::atomic<unsigned> pending_{0};
public:
    void offer(Fact value) {
        std::unique_lock<std::mutex> lock(mx_,std::try_to_lock);
        if(!lock){++dropped_;return;}
        Slot* free=nullptr;
        for(auto& slot:slots_){
            if(slot.phase&&slot.value.model==value.model&&slot.value.category==value.category&&slot.value.identity==value.identity)return;
            if(!slot.phase&&!free)free=&slot;
        }
        if(!free){++dropped_;return;}
        free->value=std::move(value);free->value.capturedAt=GetTickCount64();free->phase=1;pending_.fetch_add(1,std::memory_order_release);
    }
    bool take(size_t& index,Fact& value){
        std::unique_lock<std::mutex> lock(mx_,std::try_to_lock);if(!lock)return false;
        for(size_t i=0;i<slots_.size();++i)if(slots_[i].phase==1){value=slots_[i].value;index=i;slots_[i].phase=2;return true;}
        return false;
    }
    void finish(size_t index,bool saved) noexcept {std::lock_guard<std::mutex> lock(mx_);if(saved&&slots_[index].phase==2)pending_.fetch_sub(1,std::memory_order_release);slots_[index].phase=saved?3:1;}
    bool pending() const noexcept {return pending_.load(std::memory_order_acquire)!=0;}
    Json status(){std::lock_guard<std::mutex> lock(mx_);unsigned known=0,pending=0;
        for(const auto& slot:slots_){known+=slot.phase!=0;pending+=slot.phase==1||slot.phase==2;}
        return {{"capacity",Capacity},{"knownFacts",known},{"pendingFacts",pending},{"boundedDrops",dropped_.load()},{"eviction",false}};}
};
using Facts=BasicFacts<128>;
using VirtualFacts=BasicFacts<512>;
}
