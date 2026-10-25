#pragma once
#include "monitor_frame_time.h"
#include "monitor_rtss_frame.h"
#include "monitor_freshness.h"
#include <optional>
#include <memory>
namespace ymcc::monitorframe {
// Display-only grace. Hardware health/control proofs remain immediate. Both
// carriers consume this state through the existing coherent unified snapshot.
class Output {
    std::uint64_t pid_=0,birth_=0,boundAt_=0,hardwareAt_=0;
    json hardware_=json::object();
    // The fast lane reads this retained packet many times between hardware
    // receipts. Keep one scratch object and refresh its full base only when
    // the hardware receipt changes; dynamic rescue/history fields are updated
    // in place below.
    std::uint64_t hardwareRevision_=0,packetHardwareRevision_=0,packetLastNow_=0;
    json packetScratch_=json(nullptr);
    std::optional<json::object_t::node_type> frameTimesNode_;
    std::array<std::optional<json::object_t::node_type>,5> frameMetaNodes_;
    struct PacketTextAdapter final : nlohmann::detail::output_adapter_protocol<char>{
        std::string* target=nullptr;
        void write_character(char c) override {target->push_back(c);}
        void write_characters(const char* value,std::size_t length) override {target->append(value,length);}
    };
    std::unique_ptr<std::string> packetTextScratch_;
    std::shared_ptr<PacketTextAdapter> packetTextAdapter_;
    std::unique_ptr<nlohmann::detail::serializer<json>> packetSerializer_;
    bool packetUsingRescue_=false,packetHadHistory_=false;
    History hardwareHistory_,rtssHistory_;
    void touchHardware() noexcept { if (++hardwareRevision_==0) hardwareRevision_=1; }
    void clearPacketHistory() {
        if (!packetHadHistory_ || !packetScratch_.is_object()) { packetHadHistory_=false; return; }
        if(packetScratch_.is_object()){
            auto& object=packetScratch_.get_ref<json::object_t&>();
            const char* const keys[]={"frametime","frameTimeSource","frameTimeTs","frameTimeValidUntil","frameTimePollPeriodMs"};
            for(std::size_t i=0;i<5;++i){const auto it=object.find(keys[i]);if(it!=object.end())frameMetaNodes_[i]=object.extract(it);}
            const auto it=object.find("frameTimes");
            if(it!=object.end())frameTimesNode_=object.extract(it);
        }
        packetHadHistory_=false;
    }
    // A backwards producer timestamp is a new HWiNFO producer generation. Do
    // not retain a curve or validity proof from the newer timeline; the next
    // accepted receipt establishes a fresh timeline without adding a reader.
    static constexpr std::uint64_t hardwareRollbackGraceMs = 1000;
    void resetHardwareProofForRollback() {
        hardwareAt_=0;hardwareLow_=-1;hardwareLowAt_=hardwareLowEnd_=hardwareLowPeriod_=0;
        hardwareFrameLifetime_=missingGraceMs;hardwareFramePeriod_=0;hardwareHistory_.reset();
        if(!hardware_.empty()){
            *hardware_.find("fpsObservedAt")=0ULL;*hardware_.find("fpsValidUntil")=0ULL;*hardware_.find("fpsPollPeriodMs")=0ULL;
            *hardware_.find("fps1")=-1;*hardware_.find("fps1ObservedAt")=0ULL;*hardware_.find("fps1ValidUntil")=0ULL;*hardware_.find("fps1PollPeriodMs")=0ULL;
            *hardware_.find("hardwareObservedAt")=0ULL;*hardware_.find("hardwareValidUntil")=0ULL;*hardware_.find("hardwarePollPeriodMs")=0ULL;
            *hardware_.find("gpu")=nullptr;*hardware_.find("packagePower")=-1;
        }
        touchHardware();
    }
    std::uint64_t hardwareFrameLifetime_=missingGraceMs,hardwareFramePeriod_=0;
    RtssReader rtss_;
    double rtssFps_=-1,hardwareLow_=-1;
    std::uint64_t hardwareLowAt_=0,hardwareLowEnd_=0,hardwareLowPeriod_=0;
    std::uint64_t rtssFpsAt_=0;
    bool hardwareFpsFresh(std::uint64_t now)const{
        const auto fps=hardware_.value("fps",-1.0);
        const auto observed=hardware_.value("fpsObservedAt",0ULL),end=hardware_.value("fpsValidUntil",0ULL);
        return observed&&now>=observed&&now<end&&std::isfinite(fps)&&fps>=1;
    }
    void prepareHardware(const std::string& game,std::uint64_t now){
        if(hardware_.empty()){
            hardware_={{"ts",now},{"fps",-1},{"fps1",-1},
                {"fps1Source","hwinfo-presented-1percent-low"},{"fps1ObservedAt",0ULL},
                {"fps1ValidUntil",0ULL},{"fps1PollPeriodMs",0ULL},
                {"gpu",nullptr},{"packagePower",-1},{"game",game},{"pid",pid_},
                {"processCreated",std::to_string(birth_)},{"fpsSource","hwinfo-presented"},
                {"fpsObservedAt",0ULL},{"fpsValidUntil",0ULL},{"fpsPollPeriodMs",0ULL},
                {"hardwareObservedAt",0ULL},{"hardwareValidUntil",0ULL},{"hardwarePollPeriodMs",0ULL}};
        }else hardware_.find("game")->get_ref<json::string_t&>()=game;
    }
public:
    // These metrics come from the same ordinary hardware receipt, independently
    // of Presented FPS. RTSS publication time must never renew this proof.
    void hardwareMetrics(std::uint64_t poll,double gpu,double power,const std::string& game,std::uint64_t now,std::uint64_t periodMs=0){
        if(!pid_ || !birth_)return;
        if(!poll){prepareHardware(game,now);invalidateHardwareMetrics();return;}
        if(!monitorProducerTimeRepresentable(poll) || poll*1000ULL<=boundAt_ || poll*1000ULL>now)return;
        const auto observed=poll*1000ULL;
        if(observed<hardware_.value("hardwareObservedAt",0ULL)){
            if(now<observed || now-observed>hardwareRollbackGraceMs)return;
            resetHardwareProofForRollback();
        }
        prepareHardware(game,now);
        auto& gpuValue=*hardware_.find("gpu");
        if(std::isfinite(gpu)&&gpu>=0&&gpu<=100)gpuValue=static_cast<int>(std::round(gpu));else gpuValue=nullptr;
        auto& powerValue=*hardware_.find("packagePower");
        if(std::isfinite(power)&&power>=0&&power<2000)powerValue=std::round(power*10)/10;else powerValue=-1;
        // Repeated reads/corrections of one poll retain its original deadline.
        if(observed!=hardware_.value("hardwareObservedAt",0ULL)){
            *hardware_.find("hardwareObservedAt")=observed;
            *hardware_.find("hardwareValidUntil")=observed+monitorFreshnessThresholdMs(periodMs);
            *hardware_.find("hardwarePollPeriodMs")=periodMs;
        }
        touchHardware();
    }
    void invalidateHardwareMetrics(){
        if(hardware_.empty())return;
        *hardware_.find("gpu")=nullptr;*hardware_.find("packagePower")=-1;
        touchHardware();
        // Keep the original proof for diagnostics and monotonic admission. A
        // failed read withdraws the values immediately without touching FPS.
    }
    void reset(){
        hardwareLow_=-1;hardwareLowAt_=hardwareLowEnd_=hardwareLowPeriod_=0;
        hardwareFrameLifetime_=missingGraceMs;hardwareFramePeriod_=0;
        pid_=birth_=boundAt_=hardwareAt_=0;hardware_=json::object();
        hardwareRevision_=packetHardwareRevision_=packetLastNow_=0;
        packetScratch_=json(nullptr);frameTimesNode_.reset();for(auto& node:frameMetaNodes_)node.reset();packetSerializer_.reset();packetTextAdapter_.reset();packetTextScratch_.reset();packetUsingRescue_=false;packetHadHistory_=false;
        hardwareHistory_.reset();rtssHistory_.reset();rtss_.reset();rtssFps_=-1;rtssFpsAt_=0;
    }
    void bind(std::uint64_t pid,std::uint64_t birth,std::uint64_t selectedAt=0){
        if(pid!=pid_ || birth!=birth_ || !pid || !birth){
            reset();pid_=pid;birth_=birth;boundAt_=selectedAt;
            // A cached observation from the previous target cannot acquire a
            // new process identity. FILETIME also bounds PID-reuse admission.
            constexpr std::uint64_t unixEpochFileTime=116444736000000000ULL;
            if(birth>unixEpochFileTime)boundAt_=(std::max)(boundAt_,(birth-unixEpochFileTime)/10000ULL);
        }
    }
    void hardware(std::uint64_t poll,double fps,double fps1,double gpu,double power,const std::string& game,std::uint64_t now,double ms,std::uint64_t periodMs=0){
        if(!pid_ || !birth_ || !poll || poll>UINT64_MAX/1000ULL || poll*1000ULL<=boundAt_ || !std::isfinite(fps) || fps<0 || fps>1000000)return;
        const auto observed=poll*1000ULL;
        if(observed<hardwareAt_){
            if(now<observed || now-observed>hardwareRollbackGraceMs)return;
            resetHardwareProofForRollback();
        }
        hardwareAt_=observed;
        const bool lowValid=std::isfinite(fps1)&&fps1>=0&&fps1<=1000000;
        // Producer timestamps have 1-second resolution; native checks hardware
        // once per second. These are transport/clock bounds, not fabricated
        // observations. Rereading one poll never extends this deadline.
        const auto lifetime=hardwareLifetimeMs(periodMs);
        // One observed LOW has one deadline. Invalid observations do not renew
        // it; newer genuine values replace it, including a genuine zero.
        if(lowValid && poll*1000ULL>=hardwareLowAt_){
            if(poll*1000ULL!=hardwareLowAt_ || hardwareLowAt_==0){hardwareLowAt_=poll*1000ULL;hardwareLowEnd_=hardwareLowAt_+lifetime;hardwareLowPeriod_=periodMs;}
            hardwareLow_=std::round(fps1*10)/10;
        }
        hardwareMetrics(poll,gpu,power,game,now,periodMs);
        if(hardware_.empty())prepareHardware(game,now);
        const auto update=[this](const char* key,auto value){*hardware_.find(key)=value;};
        update("ts",now);update("fps",std::round(fps*10)/10);update("fps1",hardwareLow_);
        update("fps1ObservedAt",hardwareLowAt_);update("fps1ValidUntil",hardwareLowEnd_);
        update("fps1PollPeriodMs",hardwareLowPeriod_);
        update("fpsObservedAt",poll*1000ULL);update("fpsValidUntil",poll*1000ULL+lifetime);
        update("fpsPollPeriodMs",periodMs);
        if(fps==0)hardwareHistory_.reset();
        else {
            const bool newProof=hardwareHistory_.timestamp()!=poll*1000ULL;
            if(hardwareHistory_.observe(pid_,birth_,poll,ms,lifetime)&&newProof){hardwareFrameLifetime_=lifetime;hardwareFramePeriod_=periodMs;}
        }
        touchHardware();
    }
    bool observeRtss(const RtssFrame& frame){
        if(!frame.valid || !pid_ || !birth_ || !frame.observedAt || frame.observedAt<boundAt_ || frame.observedAt<rtssFpsAt_ || frame.observedAt<rtssHistory_.timestamp() || !valid(frame.milliseconds))return false;
        rtssHistory_.observeAt(pid_,birth_,frame.observedAt,frame.milliseconds);
        if(frame.fps>=0 && frame.fps<=1000000){rtssFps_=frame.fps;rtssFpsAt_=frame.observedAt;}
        return true;
    }
    // The fast lane uses this receipt to avoid rebuilding/publishing an
    // unchanged packet. Invalid/unchanged RTSS reads retain the existing
    // bounded grace packet; process exit is handled by gameExited().
    bool sampleRtss(std::uint64_t tick,std::uint64_t now){return observeRtss(rtss_.sample(static_cast<std::uint32_t>(pid_),birth_,tick,now));}
    bool gameExited()const{return rtss_.terminated();}
    // FPS rescue runs on the existing ordinary worker even when the curve is
    // off. A valid presented-FPS proof avoids this extra read altogether.
    bool needsRtssRescue(std::uint64_t now)const{return pid_&&birth_&&!hardwareFpsFresh(now);}
    const json& packetView(std::uint64_t now){
        clearPacketHistory();
        if(packetLastNow_ && now<packetLastNow_){
            packetScratch_=hardware_;packetHardwareRevision_=hardwareRevision_;
            packetUsingRescue_=false;packetHadHistory_=false;
        }
        packetLastNow_=now;
        if(!pid_ || !birth_){packetScratch_=nullptr;packetUsingRescue_=false;return packetScratch_;}
        const auto fresh=[now](std::uint64_t ts){return ts && now>=ts && now-ts<missingGraceMs;};
        // Keep the presented FPS (including generated frames) authoritative.
        // RTSS is a rescue source only when that proof is absent, expired or
        // below one FPS; fast RTSS frame-time history is chosen independently.
        const bool rescue=!hardwareFpsFresh(now)&&fresh(rtssFpsAt_);
        // Reject an unusable family before copying its retained JSON nodes.
        if(!rescue&&(hardware_.empty() || now<hardware_.value("fpsObservedAt",0ULL) || now>=hardware_.value("fpsValidUntil",0ULL))){
            packetScratch_=nullptr;packetUsingRescue_=false;return packetScratch_;
        }
        if(packetHardwareRevision_!=hardwareRevision_){
            packetScratch_=hardware_;packetHardwareRevision_=hardwareRevision_;
            packetUsingRescue_=false;packetHadHistory_=false;
        }
        const bool rescueTransition=rescue!=packetUsingRescue_;
        if(rescueTransition){
            if(rescue){
                if(packetScratch_.is_null() || packetScratch_.empty())
                    packetScratch_=hardware_.empty()?json::object():hardware_;
            }else{
                packetScratch_=hardware_;packetHardwareRevision_=hardwareRevision_;packetHadHistory_=false;
            }
            packetUsingRescue_=rescue;
        }
        if(rescue){
            if(packetScratch_.empty())packetScratch_={{"fps1",-1},{"gpu",nullptr},{"packagePower",-1},{"game",nullptr}};
            packetScratch_["ts"]=rtssFpsAt_;packetScratch_["fps"]=std::round(rtssFps_*10)/10;
            packetScratch_["fpsObservedAt"]=rtssFpsAt_;packetScratch_["fpsValidUntil"]=rtssFpsAt_+missingGraceMs;
            if(rescueTransition){
                packetScratch_["pid"]=pid_;packetScratch_["processCreated"]=std::to_string(birth_);
                packetScratch_["fpsSource"]="rtss";packetScratch_["fpsPollPeriodMs"]=0;
            }
        }
        const auto metricAt=packetScratch_.value("hardwareObservedAt",0ULL),metricEnd=packetScratch_.value("hardwareValidUntil",0ULL);
        if(!metricAt || now<metricAt || now>=metricEnd){packetScratch_["gpu"]=nullptr;packetScratch_["packagePower"]=-1;}
        const auto lowAt=packetScratch_.value("fps1ObservedAt",0ULL),lowEnd=packetScratch_.value("fps1ValidUntil",0ULL);
        if(!lowAt || now<lowAt || now>=lowEnd)packetScratch_["fps1"]=-1;
        if(packetScratch_.value("fps",0.0)<=0)return packetScratch_;
        const bool rtssFresh=rtssHistory_.retain(pid_,birth_,now);
        const bool hwFresh=hardwareHistory_.retain(pid_,birth_,now,hardwareFrameLifetime_);
        const bool preferRtss=rtssFresh && (rtssHistory_.size()>=2 || !hwFresh || hardwareHistory_.size()<2);
        const History* history=preferRtss?&rtssHistory_:hwFresh?&hardwareHistory_:nullptr;
        if(history){
            json::array_t* samples=nullptr;
            auto& object=packetScratch_.get_ref<json::object_t&>();
            if(frameTimesNode_){
                auto inserted=object.insert(std::move(*frameTimesNode_));
                frameTimesNode_.reset();
                samples=&inserted.position->second.get_ref<json::array_t&>();
            }else{
                auto& value=object["frameTimes"];
                if(!value.is_array())value=json::array();
                samples=&value.get_ref<json::array_t&>();
            }
            samples->clear();samples->reserve(history->size());
            for(std::size_t i=0;i<history->size();++i)samples->push_back(history->at(i));
            auto setMeta=[&](std::size_t index,const char* key,auto value){
                auto it=object.find(key);
                if(frameMetaNodes_[index]){auto inserted=object.insert(std::move(*frameMetaNodes_[index]));frameMetaNodes_[index].reset();it=inserted.position;}
                if(it==object.end())it=object.emplace(key,json(value)).first;else it->second=value;
            };
            setMeta(0,"frametime",history->last());
            const char* const source=preferRtss?"rtss-current-frame":"hwinfo-presented-avg";
            auto sourceIt=object.find("frameTimeSource");
            if(frameMetaNodes_[1]){auto inserted=object.insert(std::move(*frameMetaNodes_[1]));frameMetaNodes_[1].reset();sourceIt=inserted.position;}
            if(sourceIt==object.end())sourceIt=object.emplace("frameTimeSource",json(source)).first;
            else if(sourceIt->second.get_ref<json::string_t&>()!=source)sourceIt->second=source;
            setMeta(2,"frameTimeTs",history->timestamp());setMeta(3,"frameTimeValidUntil",history->timestamp()+(preferRtss?missingGraceMs:hardwareFrameLifetime_));
            setMeta(4,"frameTimePollPeriodMs",preferRtss?0ULL:hardwareFramePeriod_);packetHadHistory_=true;
        }
        return packetScratch_;
    }
    json packet(std::uint64_t now){return packetView(now);}
    std::string& packetTextView(std::uint64_t now){
        const json& value=packetView(now);
        if(!packetTextScratch_){
            packetTextScratch_=std::make_unique<std::string>();
            packetTextAdapter_=std::make_shared<PacketTextAdapter>();packetTextAdapter_->target=packetTextScratch_.get();
            packetSerializer_=std::make_unique<nlohmann::detail::serializer<json>>(
                nlohmann::detail::output_adapter_t<char>(packetTextAdapter_),' ',nlohmann::detail::error_handler_t::strict);
        }
        auto& text=*packetTextScratch_;text.clear();
        if(value.is_null())return text;
        packetSerializer_->dump(value,false,false,0);return text;
    }
};
}

