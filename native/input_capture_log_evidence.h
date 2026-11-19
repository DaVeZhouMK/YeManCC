#pragma once
// Caller holds the existing input-capture writer mutex. No IO or allocation
// while observing a failure;JSON is created only for an explicit log export.
#include <array>
#include <cstdint>
#include "json.hpp"
namespace ymcc::logging {
enum class CaptureLogFailureStage : unsigned { OpenOrTail,NotReady,Write,Flush,ExportFlush,ExportException,Count };
inline constexpr std::array<const char*,6> captureLogFailureNames={
    "stream-open-or-tail","stream-not-ready","stream-write","stream-flush","export-flush","export-flush-exception"};
template<std::uint64_t Limit=9007199254740991ULL>
class BasicCaptureLogFailureEvidence {
    static_assert(Limit>0&&Limit<=9007199254740991ULL);
public:
    struct Witness { CaptureLogFailureStage stage=CaptureLogFailureStage::Count;std::uint64_t tick=0; };
    struct Snapshot {
        std::uint64_t count=0;std::array<std::uint64_t,6> byStage{};
        Witness first,last;bool streamOpen=false,streamGood=false;
    };
    void observe(CaptureLogFailureStage stage,std::uint64_t tick) noexcept {
        const auto index=static_cast<unsigned>(stage);if(index>=captureLogFailureNames.size())return;
        if(first_.stage==CaptureLogFailureStage::Count)first_={stage,tick};
        last_={stage,tick};if(count_<Limit)++count_;if(byStage_[index]<Limit)++byStage_[index];
    }
    Snapshot snapshot(bool open,bool good) const noexcept {return {count_,byStage_,first_,last_,open,good};}
    static nlohmann::json describe(const Snapshot& view,unsigned long writerPid) {
        const auto witness=[](const Witness& value)->nlohmann::json {
            const auto index=static_cast<unsigned>(value.stage);
            if(index>=captureLogFailureNames.size())return nullptr;
            return {{"stage",captureLogFailureNames[index]},{"tickMs",value.tick}};
        };
        nlohmann::json stages=nlohmann::json::object();
        for(size_t i=0;i<captureLogFailureNames.size();++i)stages[captureLogFailureNames[i]]=view.byStage[i];
        return {{"schema","ymcc.input-capture-log-failures/1"},{"writerPid",writerPid},{"writerLifetimeKnown",false},
            {"failureObservations",view.count},{"counterLimit",Limit},{"counterSaturated",view.count==Limit},
            {"countsByStage",std::move(stages)},{"first",witness(view.first)},{"last",witness(view.last)},
            {"streamOpen",view.streamOpen},{"streamGood",view.streamGood},
            {"osErrorCodeKnown",false},{"osErrorCode",nullptr},
            {"clockScope","tickMs=original-failure-observation;Windows-boot-monotonic;zero-is-valid"},
            {"scope","current-Native-input-capture-IO-failure-observations;not-lost-row-or-complete-delivery-proof"},
            {"serializationExceptionsCounted",false},{"completeHistoryProven",false}};
    }
private:
    std::uint64_t count_=0;std::array<std::uint64_t,6> byStage_{};Witness first_,last_;
};
using CaptureLogFailureEvidence=BasicCaptureLogFailureEvidence<>;

struct CaptureQueueExportSnapshot {
    std::uint64_t queuedRows=0,capacityRows=0,evictionsSinceLastDrainOrReset=0;
    bool pendingSignal=false;
};
inline nlohmann::json describeCaptureQueueExport(const CaptureQueueExportSnapshot& view,unsigned long writerPid) {
    return {{"schema","ymcc.input-capture-queue-export/1"},{"writerPid",writerPid},{"writerLifetimeKnown",false},
        {"queuedRows",view.queuedRows},{"capacityRows",view.capacityRows},{"pendingSignal",view.pendingSignal},
        {"evictionsSinceLastDrainOrReset",view.evictionsSinceLastDrainOrReset},{"counterSaturationProtected",false},
        {"evictionCounterIsLifetimeHistory",false},{"detachedRowsKnown",false},{"detachedRows",nullptr},
        {"snapshotTickKnown",false},{"snapshotTickMs",nullptr},
        {"queueDrainedByExport",false},{"atomicWithStreamFlushOrFileCopy",false},{"completeDeliveryProven",false},
        {"scope","current-Native-queue-under-original-mutex;not-in-flight-or-file-delivery"}};
}
}
