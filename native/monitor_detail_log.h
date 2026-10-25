#pragma once
// High-frequency monitor snapshot diagnostics only. Existing monitor worker ->
// bounded memory -> existing IPC pool. This is deliberately separate from the
// low-frequency native lifecycle stream: no hardware reads, polling thread,
// UI-thread disk I/O, or low-frequency log mirroring.
#include "../deps/json/json.hpp"
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
namespace ymcc::monitordetail {
using json=nlohmann::json;
struct Limits {
    std::size_t recordBytes=32*1024, queueBytes=256*1024, queueRows=16, fileBytes=2*1024*1024;
    std::uint64_t sampleMs=1000, flushMs=5000;
    unsigned flushRows=16;
};
inline constexpr std::size_t rawSnapshotBytes=12*1024;
// A read-only view used by the native monitor worker. The string fields borrow
// the already published cache while snapshotRecord() materializes exactly one
// diagnostic JSON row. This avoids the intermediate IPC envelope and its extra
// top/fps string copies on the 1 Hz diagnostic lane.
struct SnapshotView {
    bool available=false;
    unsigned long long generation=0,seq=0,session=0,relayEpoch=0;
    long long collectedAt=0,heartbeatAt=0,hwinfoOkAt=0;
    bool topActive=false,fpsActive=false,hwDown=true;
    std::string_view top{},fps{};
};
inline json snapshotRecord(const SnapshotView& cache,long long observedAtMs,unsigned pid,json counters={}) {
    json data={{"schema",1},{"available",cache.available},{"session",cache.session},
        {"generation",cache.generation},{"relayEpoch",cache.relayEpoch},{"seq",cache.seq},
        {"topActive",cache.topActive},{"fpsActive",cache.fpsActive},{"hwDown",cache.available?cache.hwDown:true}};
    if(cache.available){
        data["collectedAt"]=cache.collectedAt;data["heartbeatAt"]=cache.heartbeatAt;
        data["hwinfoOkAt"]=cache.hwinfoOkAt;
    }
    const bool fits=cache.top.size()<=rawSnapshotBytes&&cache.fps.size()<=rawSnapshotBytes-cache.top.size();
    if(fits){
        data["top"]=cache.top.empty()?json(nullptr):json(std::string(cache.top));
        data["fps"]=cache.fps.empty()?json(nullptr):json(std::string(cache.fps));
    }else{
        data["rawPayloadOmitted"]=true;data["omissionReason"]="raw-snapshot-byte-limit";
        data["topBytes"]=cache.top.size();data["fpsBytes"]=cache.fps.size();
    }
    return {{"schemaVersion",1},{"event","monitor-data-snapshot"},{"lane","high-frequency"},
        {"observedAtMs",observedAtMs},{"writerPid",pid},{"snapshot",std::move(data)},
        {"diagnosticCounters",std::move(counters)},{"payloadMaxBytes",rawSnapshotBytes},
        {"evidenceScope","existing-uniform-cache-not-new-measurements"}};
}inline json snapshotRecord(const json& cache,long long observedAtMs,unsigned pid,const json& counters={}) {
    json data=json::object();
    for(const char* key:{"schema","available","session","generation","relayEpoch","seq","collectedAt","heartbeatAt","hwinfoOkAt","topActive","fpsActive","hwDown"}) {
        const auto it=cache.find(key);if(it!=cache.end()&&(it->is_boolean()||it->is_number()))data[key]=*it;
    }
    const auto textSize=[&](const char* key)->std::size_t{const auto it=cache.find(key);return it!=cache.end()&&it->is_string()?it->get_ref<const std::string&>().size():0;};
    const auto top=textSize("top"),fps=textSize("fps");
    const bool fits=top<=rawSnapshotBytes&&fps<=rawSnapshotBytes-top;
    if(fits){for(const char* key:{"top","fps"}){const auto it=cache.find(key);data[key]=it!=cache.end()&&it->is_string()?*it:json(nullptr);}}
    else{data["rawPayloadOmitted"]=true;data["omissionReason"]="raw-snapshot-byte-limit";data["topBytes"]=top;data["fpsBytes"]=fps;}
    return {{"schemaVersion",1},{"event","monitor-data-snapshot"},{"lane","high-frequency"},{"observedAtMs",observedAtMs},
        {"writerPid",pid},{"snapshot",std::move(data)},{"diagnosticCounters",counters},
        {"payloadMaxBytes",rawSnapshotBytes},{"evidenceScope","existing-uniform-cache-not-new-measurements"}};
}
// One writer-owned file pair. Preflight before every write; rotation failure is
// a dropped diagnostic record, never permission to grow past the configured cap.
class StrictFile {
    std::function<std::filesystem::path()> path_;
    Limits limits_;
    std::ofstream stream_;
    std::filesystem::path active_;
    std::size_t bytes_=0;
    std::uint64_t lastFlush_=0,retryAt_=0;
    unsigned pending_=0;
    bool tailNeedsNewline_=false;
    std::atomic<std::uint64_t> rotations_{0},failures_{0},flushes_{0};
    void closeImpl(){if(stream_.is_open()){stream_.flush();stream_.close();}stream_.clear();pending_=0;}
    struct Failure { std::uint64_t tick=0; const char* stage="not-observed"; int code=0; };
    mutable std::mutex failureMx_;
    Failure firstFailure_, lastFailure_;
    bool failureKnown_=false;
    bool failed(std::uint64_t now,const char* stage,int code=0){
        {std::lock_guard<std::mutex> lock(failureMx_);lastFailure_={now,stage,code};
            if(!failureKnown_){firstFailure_=lastFailure_;failureKnown_=true;}}
        closeImpl();retryAt_=now+1000;failures_.fetch_add(1);return false;
    }
    bool open(std::size_t nextBytes,std::uint64_t now){
        if(now<retryAt_)return false;
        if(active_.empty())active_=path_();
        if(active_.empty())return failed(now,"path-empty");
        std::error_code ec;const bool exists=std::filesystem::exists(active_,ec);
        if(ec)return failed(now,"file-status",ec.value());
        bytes_=0;tailNeedsNewline_=false;
        if(exists){const auto size=std::filesystem::file_size(active_,ec);if(ec||size>limits_.fileBytes)return failed(now,ec?"file-size":"existing-file-over-budget",ec.value());bytes_=static_cast<std::size_t>(size);
            if(bytes_){std::ifstream in(active_,std::ios::binary);in.seekg(-1,std::ios::end);char last=0;if(!in.get(last))return failed(now,"tail-read");tailNeedsNewline_=last!='\n';}}
        if(nextBytes>limits_.fileBytes-bytes_||(tailNeedsNewline_&&nextBytes==limits_.fileBytes-bytes_)){
            closeImpl();auto backup=active_;backup+=L".1";
            std::filesystem::remove(backup,ec);if(ec)return failed(now,"backup-remove",ec.value());
            if(exists){std::filesystem::rename(active_,backup,ec);if(ec)return failed(now,"active-rename",ec.value());rotations_.fetch_add(1);}
            bytes_=0;tailNeedsNewline_=false;
        }
        stream_.open(active_,std::ios::binary|std::ios::app);if(!stream_)return failed(now,"stream-write-or-open");
        if(tailNeedsNewline_){stream_.put('\n');++bytes_;tailNeedsNewline_=false;}
        return stream_.good();
    }
public:
    explicit StrictFile(std::function<std::filesystem::path()> path,Limits limits={}):path_(std::move(path)),limits_(limits){}
    bool append(std::string_view line,std::uint64_t now){
        if(line.empty()||line.size()+1>limits_.recordBytes||line.size()+1>limits_.fileBytes)return false;
        if(!stream_.is_open()){if(!open(line.size()+1,now))return false;}
        else if(line.size()+1>limits_.fileBytes-bytes_){closeImpl();if(!open(line.size()+1,now))return false;}
        stream_.write(line.data(),static_cast<std::streamsize>(line.size()));stream_.put('\n');
        if(!stream_)return failed(now,"stream-write-or-open");
        bytes_+=line.size()+1;++pending_;
        if(pending_>=limits_.flushRows||(now>=lastFlush_&&now-lastFlush_>=limits_.flushMs)){if(!flush())return failed(now,"stream-flush");lastFlush_=now;}
        return true;
    }
    bool flush(){if(stream_.is_open()&&pending_){stream_.flush();flushes_.fetch_add(1);pending_=0;}return !stream_.is_open()||stream_.good();}
    void close(){closeImpl();}
    json failureEvidence()const{
        std::lock_guard<std::mutex> lock(failureMx_);
        const auto row=[](const Failure& f){return json{{"tick",f.tick},{"stage",f.stage},
            {"errorCodeObserved",f.code!=0},{"errorCode",f.code?json(f.code):json(nullptr)}};};
        return {{"schemaVersion",1},{"observed",failureKnown_},{"clock","native-monotonic-ms"},
            {"sourcePathsIncluded",false},{"first",failureKnown_?row(firstFailure_):json(nullptr)},
            {"last",failureKnown_?row(lastFailure_):json(nullptr)}};
    }
    std::uint64_t failures()const{return failures_.load();}
    std::uint64_t rotations()const{return rotations_.load();}
    std::uint64_t flushes()const{return flushes_.load();}
};
struct QueueDeps {
    std::function<bool()> enabled;
    std::function<bool(std::function<void()>)> submit;
    std::function<bool(std::string_view)> write;
    std::function<void()> flush,close;
};
class Queue {
    struct Row {std::string line;std::uint64_t epoch;std::size_t charge;};
    QueueDeps deps_;Limits limits_;
    std::mutex queueMx_,writerMx_;
    std::deque<Row> rows_;
    bool scheduled_=false;
    std::atomic<bool> enabled_,closeWanted_{false};
    std::atomic<std::uint64_t> epoch_{1},nextSample_{0};
    std::atomic<std::size_t> pendingBytes_{0},pendingRows_{0};
    std::atomic<std::uint64_t> accepted_{0},written_{0},busyDrops_{0},fullDrops_{0},oversizeDrops_{0},gateDrops_{0},ioDrops_{0},submitFailures_{0};
    bool enabled()const{return enabled_.load(std::memory_order_acquire)&&deps_.enabled();}
    bool startJob(){
        bool ok=false;try{ok=deps_.submit([this]{drain();});}catch(...){}
        if(!ok){std::lock_guard<std::mutex> lock(queueMx_);scheduled_=false;submitFailures_.fetch_add(1);}
        return ok;
    }
    void kick(){bool submit=false;{std::unique_lock<std::mutex> lock(queueMx_,std::try_to_lock);if(!lock)return;
        if(!scheduled_&&(!rows_.empty()||closeWanted_.load())){scheduled_=true;submit=true;}}
        if(submit)startJob();
    }
    void release(std::size_t charge){pendingBytes_.fetch_sub(charge);pendingRows_.fetch_sub(1);}
public:
    explicit Queue(QueueDeps deps,Limits limits={}):deps_(std::move(deps)),limits_(limits),enabled_(deps_.enabled()){}
    // Called at the existing detailed-log gate edge; no queue wait or file I/O.
    void gateChanged(bool value){if(enabled_.exchange(value)!=value){epoch_.fetch_add(1);if(!value){closeWanted_.store(true);kick();}}}
    // Flush/close a partial batch on the existing idle/stop edge, without disabling the gate.
    void requestClose(){closeWanted_.store(true);kick();}
    std::optional<std::uint64_t> permit(std::uint64_t now){
        if(!enabled())return std::nullopt;
        auto next=nextSample_.load();if(now<next)return std::nullopt;
        if(!nextSample_.compare_exchange_strong(next,now+limits_.sampleMs))return std::nullopt;
        // Reject known-full pressure before the producer copies/encodes its
        // cache. Keep the 1Hz budget consumed: retries must not become a loop.
        // If submit previously failed until the queue filled, offer() cannot
        // schedule a drain any more. This existing sample tick can recover it.
        if(pendingRows_.load()>=limits_.queueRows || pendingBytes_.load()>=limits_.queueBytes){
            fullDrops_.fetch_add(1);kick();return std::nullopt;
        }
        return epoch_.load();
    }
    bool offer(std::string line,std::uint64_t permitEpoch){
        if(line.empty()||line.size()+1>limits_.recordBytes){oversizeDrops_.fetch_add(1);return false;}
        const auto charge=line.capacity()+1;
        bool submit=false;
        {std::unique_lock<std::mutex> lock(queueMx_,std::try_to_lock);if(!lock){busyDrops_.fetch_add(1);return false;}
            if(!enabled()||permitEpoch!=epoch_.load()){gateDrops_.fetch_add(1);return false;}
            // Includes the currently writing row: moving a row out of the deque
            // never releases its memory budget before the sink finishes with it.
            if(pendingRows_.load()>=limits_.queueRows||charge>limits_.queueBytes-pendingBytes_.load()){fullDrops_.fetch_add(1);return false;}
            rows_.push_back({std::move(line),permitEpoch,charge});
            pendingBytes_.fetch_add(charge);pendingRows_.fetch_add(1);accepted_.fetch_add(1);
            if(!scheduled_){scheduled_=true;submit=true;}}
        if(submit)startJob();return true;
    }
    // Only the existing background pool / export worker may call this method.
    void drain(bool ownsScheduledJob=true){
        std::lock_guard<std::mutex> writer(writerMx_);
        for(std::size_t n=0;n<limits_.queueRows;++n){std::optional<Row> row;
            {std::lock_guard<std::mutex> lock(queueMx_);if(rows_.empty())break;row=std::move(rows_.front());rows_.pop_front();}
            if(!enabled()||row->epoch!=epoch_.load())gateDrops_.fetch_add(1);
            else {bool ok=false;try{ok=deps_.write(row->line);}catch(...){}if(ok)written_.fetch_add(1);else ioDrops_.fetch_add(1);}
            const auto charge=row->charge;row.reset();release(charge);
        }
        if(closeWanted_.exchange(false)){try{deps_.close();}catch(...){}}
        if(ownsScheduledJob){{std::lock_guard<std::mutex> lock(queueMx_);scheduled_=false;}kick();}
    }
    void flushForExport(){drain(false);std::lock_guard<std::mutex> writer(writerMx_);deps_.flush();}
    json stats()const{return {{"enabled",enabled()},{"sampleIntervalMs",limits_.sampleMs},{"recordMaxBytes",limits_.recordBytes},
        {"queueMaxBytes",limits_.queueBytes},{"queueMaxRows",limits_.queueRows},{"fileMaxBytes",limits_.fileBytes},
        {"retainedFileCount",2},{"pendingBytes",pendingBytes_.load()},{"pendingRows",pendingRows_.load()},
        {"accepted",accepted_.load()},{"written",written_.load()},{"droppedBusy",busyDrops_.load()},
        {"droppedQueueFull",fullDrops_.load()},{"droppedOversize",oversizeDrops_.load()},{"droppedGate",gateDrops_.load()},
        {"droppedIo",ioDrops_.load()},{"submitFailures",submitFailures_.load()}};}
};
}

