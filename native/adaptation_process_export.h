#pragma once
// Explicit log export only. Reuses immutable samples already retained in RAM;
// never drains the queue, appends to permanent history, or probes a device.
#include "model_adaptation_log.h"
#include <memory>
#include <vector>
#include <stdexcept>

namespace ymcc::adaptlog {
inline constexpr unsigned long long processExportMaxBytes=4ULL*1024*1024;
inline constexpr unsigned long long processExportFooterBytes=65536;
inline constexpr unsigned long long processExportMaxRowBytes=32768;
inline constexpr wchar_t processExportName[]=L"virtual-adaptation-session.jsonl";
inline constexpr wchar_t modelFactsExportName[]=L"model-adaptation-session.jsonl";
inline constexpr wchar_t modelSignalsExportName[]=L"model-adaptation-signals-session.jsonl";
enum class ProcessExportKind { virtualFacts,modelFacts,modelSignals };

struct ProcessExportCleanup {
    std::filesystem::path directory,file;
    ~ProcessExportCleanup() {
        // Only paths exclusively created by this export. No recursive deletion:
        // an unexpected extra member leaves the directory rather than deleting it.
        std::error_code error;
        if(!file.empty())std::filesystem::remove(file,error);
        if(!directory.empty())std::filesystem::remove(directory,error);
    }
};
struct ProcessExportFile {
    std::unique_ptr<ProcessExportCleanup> cleanup;
    std::filesystem::path file;
    Json receipt={{"schemaVersion",1},{"saved",false},{"file","virtual-adaptation-session.jsonl"},
        {"maxBytes",processExportMaxBytes},{"maxRecordBytes",processExportMaxRowBytes},
        {"scope","current-native-process-retained-first-witnesses;not-permanent-history-or-complete-session"}};
};

template<size_t Capacity,class Capture,class Encode>
ProcessExportFile exportRetainedSnapshot(const std::filesystem::path& root,ProcessExportKind kind,Capture&& capture,Encode&& encode) {
    static_assert(Capacity<=512); // Omission ordinals and footer also stay bounded.
    ProcessExportFile result;
    result.receipt["maxKnownFacts"]=Capacity;
    const wchar_t* leaf=nullptr;const wchar_t* prefix=nullptr;
    switch(kind){
        case ProcessExportKind::virtualFacts:leaf=processExportName;prefix=L".virtual-evidence-export-";break;
        case ProcessExportKind::modelFacts:leaf=modelFactsExportName;prefix=L".model-evidence-export-";result.receipt["file"]="model-adaptation-session.jsonl";break;
        case ProcessExportKind::modelSignals:leaf=modelSignalsExportName;prefix=L".model-signal-evidence-export-";result.receipt["file"]="model-adaptation-signals-session.jsonl";break;
        default:result.receipt["file"]=nullptr;result.receipt["reason"]="unsupported-export-kind";return result;
    }
    try {
        result.cleanup=std::make_unique<ProcessExportCleanup>();
        static std::atomic<unsigned long long> sequence{0};
        const auto now=GetTickCount64();
        const auto& writer=ymcc::sleepfactlog::writerIdentity();
        for(unsigned attempt=0;attempt<8;++attempt){
            auto directory=root/(std::wstring(prefix)+std::to_wstring(writer.pid)+L"-"+
                std::to_wstring(now)+L"-"+std::to_wstring(sequence.fetch_add(1,std::memory_order_relaxed)));
            std::error_code error;
            if(std::filesystem::create_directory(directory,error)){
                result.cleanup->directory=std::move(directory);break;
            }
            if(error){result.receipt["reason"]="snapshot-directory-create-failed";return result;}
        }
        if(result.cleanup->directory.empty()){result.receipt["reason"]="snapshot-directory-collision";return result;}
        const auto path=result.cleanup->directory/leaf;
        // The directory was created exclusively. Keep this exact file owned even
        // on a partial write, and never add an earlier snapshot to the archive.
        result.cleanup->file=path;
        std::ofstream stream(path,std::ios::binary|std::ios::trunc);
        if(!stream){result.receipt["reason"]="snapshot-open-failed";return result;}
        const auto snapshot=capture();
        const auto begin=Json{{"schema","ymcc.adaptation-process-export/1"},{"record","process_begin"},
            {"writerPid",writer.pid},{"writerProcessCreation100ns",writer.creation100ns?Json(std::to_string(writer.creation100ns)):Json(nullptr)},
            {"snapshotTickMs",now},{"snapshotAvailable",snapshot.available},{"knownFactsAtSnapshot",snapshot.available?Json(snapshot.known):Json(nullptr)},
            {"capacity",Capacity},{"busyDropsAtSnapshot",snapshot.available?Json(snapshot.busyDrops):Json(nullptr)},
            {"capacityDropsAtSnapshot",snapshot.available?Json(snapshot.capacityDrops):Json(nullptr)},
            {"clockScope","snapshotTickMs=export-request;capturedTickMs=original-first-sample;Windows-boot-monotonic"},
            {"historyScope","current-native-process-only;original-permanent-journal-unchanged"},
            {"durabilityInferred",false},{"completeHistory",false}}.dump();
        unsigned long long bytes=begin.size()+1;
        stream<<begin<<'\n';
        size_t written=0;
        Json omissions=Json::array();
        // The footer already has reserved space. Keep outcome metadata out of
        // the sample and file budgets that determine which witnesses are copied.
        std::array<JournalDrainOutcome,Capacity> journalOutcomes{};
        for(size_t i=0;i<snapshot.known;++i){
            const char* omission=nullptr;
            try {
                const auto row=encode(snapshot.entries[i],i+1).dump();
                if(row.size()+1>processExportMaxRowBytes)omission="row-byte-budget";
                else if(row.size()+1>processExportMaxBytes-processExportFooterBytes-bytes)omission="file-byte-budget";
                else{stream<<row<<'\n';bytes+=row.size()+1;++written;
                    if constexpr(requires{snapshot.entries[i].journalOutcome;}){
                        if(kind==ProcessExportKind::virtualFacts&&snapshot.entries[i].phase==3)
                            journalOutcomes[i]=snapshot.entries[i].journalOutcome;
                    }
                }
            }catch(...){omission="sample-serialization-failed";}
            if(omission)omissions.push_back({{"slotOrdinal",i+1},{"reason",omission}});
        }
        const bool rowsComplete=snapshot.available&&omissions.empty();
        auto footer=Json{{"record","process_end"},{"snapshotAvailable",snapshot.available},
            {"knownFactsAtSnapshot",snapshot.available?Json(snapshot.known):Json(nullptr)},
            {"writtenFacts",written},{"omittedFacts",omissions.size()},
            {"omissions",std::move(omissions)},{"retainedSnapshotRowsComplete",rowsComplete},
            {"sourceFirstWitnessCoverageComplete",false},{"completeHistory",false},
            {"scope","snapshot rows only;busy/capacity/frame-selector gaps and prior-process evidence remain separate"}};
        // At most one small ordinal per copied row; omitted rows only use the
        // existing omission entry. Both share the 512-ordinal upper bound.
        for(size_t i=0;i<snapshot.known;++i){
            const char* label=nullptr;
            switch(journalOutcomes[i]){
                case JournalDrainOutcome::appendReported:label="append-reported";break;
                case JournalDrainOutcome::keyAlreadyPresent:label="key-already-present";break;
                case JournalDrainOutcome::budgetOmitted:label="budget-omitted";break;
                case JournalDrainOutcome::unknown:break;
            }
            if(label)footer["journalDrainOutcomes"][label].push_back(i+1);
        }
        const auto end=footer.dump();
        if(end.size()+1>processExportFooterBytes||bytes+end.size()+1>processExportMaxBytes){
            result.receipt["reason"]="snapshot-footer-byte-budget";return result;
        }
        stream<<end<<'\n';bytes+=end.size()+1;stream.close();
        if(!stream){result.receipt["reason"]="snapshot-write-or-close-failed";return result;}
        result.file=path;result.receipt["saved"]=true;result.receipt["bytes"]=bytes;
        result.receipt["snapshotAvailable"]=snapshot.available;
        result.receipt["knownFactsAtSnapshot"]=snapshot.available?Json(snapshot.known):Json(nullptr);
        result.receipt["writtenFacts"]=written;result.receipt["omittedFacts"]=snapshot.known-written;
        result.receipt["retainedSnapshotRowsComplete"]=rowsComplete;
        result.receipt["sourceFirstWitnessCoverageComplete"]=false;
        if(!snapshot.available)result.receipt["reason"]="facts-lock-busy;payload-unavailable";
    }catch(...){result.receipt["saved"]=false;result.receipt["reason"]="snapshot-export-exception";result.file.clear();}
    return result;
}

template<size_t Capacity>
ProcessExportFile exportRetainedFacts(const std::filesystem::path& root,BasicFacts<Capacity>& facts,ProcessExportKind kind=ProcessExportKind::virtualFacts) {
    return exportRetainedSnapshot<Capacity>(root,kind,[&]{return facts.snapshotForExport();},[](const auto& view,size_t ordinal){
        const auto& value=*view.value;
        return Json{{"record","first_witness"},{"slotOrdinal",ordinal},
                    {"queuePhaseAtSnapshot",view.phase==1?"pending":view.phase==2?"in-flight":"drain-resolved"},
                    {"journalPersistence","not-inferred;drain-resolved-can-mean-budget-omitted"},
                    {"capturedTickMs",value.capturedAt},{"category",value.category},
                    {"uniqueKey",{{"schema",1},{"model",value.model},{"category",value.category},{"identity",value.identity}}},
                    {"firstSample",value.sample}};
    });
}
inline ProcessExportFile exportRetainedSignals(const std::filesystem::path& root,Signals& signals) {
    return exportRetainedSnapshot<signalCapacity>(root,ProcessExportKind::modelSignals,[&]{return signals.snapshotForExport();},[](const auto& view,size_t ordinal){
        const auto& value=*view.value;const auto& type=value.type;
        if(value.rawBytes>value.raw.size())throw std::runtime_error("invalid retained raw length");
        std::string hex;hex.reserve(value.rawBytes*2);constexpr char digits[]="0123456789abcdef";
        for(unsigned i=0;i<value.rawBytes;++i){hex+=digits[value.raw[i]>>4];hex+=digits[value.raw[i]&15];}
        return Json{{"record","first_witness"},{"slotOrdinal",ordinal},{"category","oem-signal-type"},
            {"queuePhaseAtSnapshot",view.phase==1?"pending":view.phase==2?"in-flight":"drain-resolved"},
            {"journalPersistence","not-inferred;drain-resolved-can-mean-budget-omitted"},
            {"capturedTickMs",value.capturedAt},{"modelAtCaptureKnown",false},
            {"modelBindingScope","signal-store-does-not-capture-model;do-not-backfill-from-exporter-or-journal"},
            {"retainedType",{{"source",type.source},{"profile",type.profile},{"command",type.command},{"code",type.code},
                {"shape",type.shape},{"reportId",type.reportId},{"canonicalMask",type.canonicalMask}}},
            {"firstSample",{{"pressedFlag",value.pressed},{"mappedFlag",value.mapped},{"rawBytes",value.rawBytes},{"firstRawReportHex",std::move(hex)}}},
            {"interpretation","raw retained POD fields;profile may be protocol offset;flags are not physical direction or consumer-action proof"}};
    });
}
}
