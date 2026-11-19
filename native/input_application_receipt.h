#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace ymcc {
// Observation only. No settings/device/transport owner and no background work.
// Native refresh supplies the durable identity; only its actual accepted-frame
// branch can confirm it. Transport protocol revisions are deliberately absent.
class InputApplicationReceipt {
public:
    struct Identity {
        std::int64_t revision = -1;
        std::string hash, boot, persona;
        std::uint64_t intent = 0;
        bool target = false, gyro = false;
    };
    struct Frame {
        std::uint64_t intent = 0, host = 0, power = 0, sequence = 0, sourceSequence = 0;
        unsigned topology = 0, identity = 0;
        bool accepted = false, sourceCurrent = false, neutral = true, sensorCurrent = false;
    };
    struct Context {
        std::uint64_t intent = 0, host = 0, power = 0;
        unsigned topology = 0, identity = 0;
        bool changing = false, observed = false, sourceCurrent = false;
        bool outputActive = false, targetPrepared = false, returnPending = false, returnFailed = false;
        bool operator==(const Context&) const = default;
    };
    struct Snapshot {
        std::uint64_t serial = 0;
        Identity config;
        Frame frame;
        std::string runId, hostInstanceId;
        const char* input = "waiting";
        const char* gyro = "waiting";
    };
private:
    std::mutex mx_;
    Identity config_;
    Frame frame_;
    std::string run_, host_;
    bool eligible_ = false, input_ = false, gyro_ = false;
    std::atomic<unsigned> pending_{0};
    std::atomic<std::uint64_t> serial_{1};
    static bool valid(const Identity& v) noexcept {
        if (v.revision < 0 || v.revision > 9007199254740991LL || !v.intent ||
            v.boot.empty() || v.persona.empty() || v.hash.size() != 71 || v.hash.compare(0,7,"sha256:") != 0) return false;
        for (std::size_t i=7;i<v.hash.size();++i) if (!((v.hash[i]>='0'&&v.hash[i]<='9')||(v.hash[i]>='a'&&v.hash[i]<='f'))) return false;
        return true;
    }
    static bool same(const Identity& a,const Identity& b) noexcept {
        return a.revision==b.revision && a.hash==b.hash && a.boot==b.boot && a.persona==b.persona && a.target==b.target && a.gyro==b.gyro;
    }
    void pending() noexcept {
        pending_.store(eligible_&&config_.target ? (input_?0u:1u)|(config_.gyro&&!gyro_?2u:0u) : 0u,std::memory_order_release);
    }
    void clear() noexcept {input_=gyro_=false;frame_={};run_.clear();host_.clear();pending();}
public:
    std::uint64_t serial() const noexcept {return serial_.load(std::memory_order_acquire);}
    bool needsFrame(bool sensorCurrent) const noexcept {
        const auto pending=pending_.load(std::memory_order_acquire);
        return (pending&1u) || ((pending&2u)&&sensorCurrent);
    }
    void invalidate() {
        std::lock_guard<std::mutex> lock(mx_);
        eligible_=false;clear();serial_.fetch_add(1,std::memory_order_release);
    }
    void configure(Identity value,bool eligible) {
        std::lock_guard<std::mutex> lock(mx_);
        eligible=eligible&&valid(value);
        const bool unchanged=eligible==eligible_&&same(value,config_);
        config_=std::move(value);eligible_=eligible;
        if (unchanged) {
            // ACK bookkeeping does not change the durable fingerprint. The
            // same configuration can keep its observed frame across that refresh.
            if (input_) frame_.intent=config_.intent;
            pending();return;
        }
        clear();serial_.fetch_add(1,std::memory_order_release);
    }
    bool accepted(const Frame& value,const std::string& run,const std::string& host) {
        if (!needsFrame(value.sensorCurrent) || !value.accepted || !value.sourceCurrent || value.neutral ||
            !value.host || !value.power || !value.sequence || !value.sourceSequence || run.empty() || host.empty()) return false;
        std::lock_guard<std::mutex> lock(mx_);
        if (!eligible_ || !config_.target || value.intent!=config_.intent || (input_ && (!config_.gyro || gyro_ || !value.sensorCurrent))) return false;
        frame_=value;run_=run;host_=host;input_=true;
        if (config_.gyro && value.sensorCurrent) gyro_=true;
        pending();serial_.fetch_add(1,std::memory_order_release);return true;
    }
    Snapshot snapshot(const Context& current) {
        std::lock_guard<std::mutex> lock(mx_);
        const bool configured=eligible_&&!current.changing&&current.intent==config_.intent;
        const bool output=configured&&current.observed&&current.outputActive&&current.sourceCurrent&&
            !current.returnPending&&!current.returnFailed&&frame_.intent==current.intent&&
            frame_.host==current.host&&frame_.power==current.power&&frame_.identity==current.identity&&frame_.topology==current.topology;
        if (input_&&!output) {clear();serial_.fetch_add(1,std::memory_order_release);}
        Snapshot result;result.serial=serial();result.config=config_;result.frame=frame_;result.runId=run_;result.hostInstanceId=host_;
        if (configured && !config_.target && current.observed && !current.outputActive && !current.targetPrepared &&
            !current.returnPending && !current.returnFailed) result.input="disabled";
        else if (output&&input_) result.input="active";
        if (configured&&!config_.gyro) result.gyro="disabled";
        else if (output&&input_&&gyro_) result.gyro="active";
        return result;
    }
};
}
